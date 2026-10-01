// Render engine. Specs: openspec/specs/render-engine, cover-art; changes/phase-4-orbit-style. Mode: docs/phase0-results.md.
#include "gfx.h"
#include "ui_data.h"

// ---- VRAM pool: free words [vram_lo, vram_hi). Textures grow up page-aligned (2048 words, ps2tek:110),
// CLUTs grow down block-aligned (64 words) so they do not cost a page each. ----
static int vram_lo, vram_hi;

static int vram_alloc(int words, int clut)
{
	int a = clut ? (vram_hi - words) & ~63 : (vram_lo + 2047) & ~2047;
	if (clut ? a < vram_lo : a + words > vram_hi) return -1;
	if (clut) vram_hi = a; else vram_lo = a + words;
	return a;
}

// ---- text: UTF-8 -> glyph index (ASCII 32-126 = 0..94, font_extras = 95..110), -1 = not in the fonts ----
extern const unsigned short font_extras[GFX_EXTRAS];
static int tracking;

static int next_glyph(const char **ps)
{
	const unsigned char *s = (const unsigned char *)*ps;
	if (*s < 0x80) { *ps += 1; return *s >= 32 && *s < 127 ? *s - 32 : -1; }
	if ((*s & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
		unsigned cp = (s[0] & 0x1F) << 6 | (s[1] & 0x3F);
		*ps += 2;
		for (int i = 0; i < GFX_EXTRAS; i++)
			if (font_extras[i] == cp) return 95 + i;
		return -1;
	}
	for (*ps += 1; (**(const unsigned char **)ps & 0xC0) == 0x80; *ps += 1) {} // skip other sequences
	return -1;
}

int gfx_text_width(const gfx_font *f, const char *s)
{
	int w = 0;
	while (*s && *s != '\n') {
		int g = next_glyph(&s);
		if (g >= 0) w += f->g[g].adv + tracking;
	}
	return w > 0 ? w - tracking : 0;
}

#ifdef SELFTEST // host check: `make test` (links font_data.c)
#include <assert.h>
#include <stdio.h>
int main(void)
{
	vram_lo = 1000, vram_hi = 4 * 2048;
	assert(vram_alloc(16, 1) == 4 * 2048 - 64);          // CLUT at the top, block-aligned
	assert(vram_alloc(2048, 0) == 2048);                 // texture page-aligned at the bottom
	assert(vram_alloc(2048, 0) == 4096);
	int lo = vram_lo, hi = vram_hi;
	assert(vram_alloc(2048, 0) == -1 && vram_lo == lo && vram_hi == hi); // would overlap the CLUT: refused, unchanged
	assert(vram_alloc(256, 1) == hi - 256);              // CLUT still fits between them
	assert(vram_alloc(64 * 30, 1) == -1);

	const gfx_font *f = &gfx_font_ui;
	const char *s = "a\xC3\xA9" "b";                    // "aéb"
	assert(next_glyph(&s) == 'a' - 32 && next_glyph(&s) == 95 + 1 && next_glyph(&s) == 'b' - 32 && !*s);
	s = "\xE2\x82\xAC!";                                 // 3-byte sequence (euro): skipped as one unknown
	assert(next_glyph(&s) == -1 && next_glyph(&s) == '!' - 32);
	assert(gfx_text_width(f, "AB") == f->g['A' - 32].adv + f->g['B' - 32].adv);
	assert(gfx_text_width(f, "AB\nCCCC") == gfx_text_width(f, "AB")); // first line only
	tracking = 4;
	assert(gfx_text_width(f, "AB") == f->g['A' - 32].adv + f->g['B' - 32].adv + 4); // between glyphs only
	tracking = 0;
	assert(gfx_text_width(f, "t\xC3\xA9") == f->g['t' - 32].adv + f->g[96].adv && f->g[96].w > 0); // é baked
	assert(gfx_font_logo.g['O' - 32].w > 0 && gfx_font_logo.g['A' - 32].w == 0); // logo: "ORBIT" only
	for (int c = 33; c < 127; c++) assert(gfx_font_title.g[c - 32].w > 0 && gfx_font_mono.g[c - 32].w > 0);
	puts("gfx selftest ok");
	return 0;
}
#else
#include <kernel.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <gs_privileged.h>
#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <packet.h>

#define DISPLAY_DX 300 // calibrated on SCPH-75001 + HDMI adapter with modetest (docs/phase0-results.md)
#define DISPLAY_DY 27
#define PACKET_QW 32768
// cover slot: one 256x128 CT16 band at a time = 4x2 CT16 pages (design: phase-4-orbit-style)
#define SLOT_W 256
#define SLOT_H 128

extern const unsigned char font_atlas[];
extern const int font_atlas_h;

static framebuffer_t fb[2];
static zbuffer_t z;
static int back = 1;
static packet_t *pk;
static qword_t *q, *tag, *qend;
static unsigned long long cur_tex0, font_tex0, font_tex0_bin, ui_tex0, orb_tex0;
static int text_mode; // test pattern only: 1 = binary alpha CLUT, 2 = UV +1/2 texel (glyph line on the console)
static int slot = -1, alpha = 0x80, cur_filter, vsync_sema = -1;

static void send(qword_t *e)
{
	e = draw_finish(e);
	dma_channel_send_normal(DMA_CHANNEL_GIF, pk->data, e - pk->data, 0, 0);
	draw_wait_finish();
	dma_wait_fast();
}

static void upload(const void *src, int bytes, int w, int h, int psm, int dest, int dest_w)
{
	SyncDCache((void *)src, (u8 *)src + bytes); // the chain REFs src; dma.c syncs only the tags (docs/sources.md)
	qword_t *e = draw_texture_transfer(pk->data, (void *)src, w, h, psm, dest, dest_w);
	e = draw_texture_flush(e);
	dma_channel_send_chain(DMA_CHANNEL_GIF, pk->data, e - pk->data, 0, 0);
	dma_wait_fast();
}

static int log2up(int n) { int l = 0; while (1 << l < n) l++; return l; }

int gfx_tex_upload(gfx_tex *t, const void *pix, int w, int h, int psm, const unsigned *clut)
{
	// page sizes in pixels: CT16 64x64, T8 128x64, T4 128x128 (gsKit gsTexture.c, docs/sources.md)
	int pw = psm == GS_PSM_16 ? 64 : 128, ph = psm == GS_PSM_8 ? 64 : psm == GS_PSM_4 ? 128 : 64;
	int ncl = psm == GS_PSM_8 ? 256 : psm == GS_PSM_4 ? 16 : 0;
	int bpp = psm == GS_PSM_4 ? 4 : psm == GS_PSM_8 ? 8 : 16;
	int tbw = (w + pw - 1) / pw * pw / 64; // T8/T4 widths are whole 128-px pages, so TBW is even for them
	int lo = vram_lo, hi = vram_hi;
	int ad = vram_alloc((w + pw - 1) / pw * ((h + ph - 1) / ph) * 2048, 0);
	int cl = ncl ? vram_alloc(ncl, 1) : 0;
	if (ad < 0 || cl < 0) { vram_lo = lo, vram_hi = hi; return 0; }

	upload(pix, w * h * bpp / 8, w, h, psm, ad, tbw * 64);
	if (ncl) { // CT32 CLUT, CSM1: 256 entries as 16x16 with index bits 3 and 4 swapped; 16 entries as 8x2
		static unsigned c[256] __attribute__((aligned(64)));
		for (int i = 0; i < ncl; i++) c[ncl == 256 ? (i & ~0x18) | (i & 8) << 1 | (i & 16) >> 1 : i] = clut[i];
		upload(c, ncl * 4, ncl == 256 ? 16 : 8, ncl == 256 ? 16 : 2, GS_PSM_32, cl, 64);
	}
	// TCC=1 (texture alpha), MODULATE, CLD=1: the CLUT is reloaded whenever this TEX0 is written
	t->tex0 = GS_SET_TEX0(ad >> 6, tbw, psm, log2up(w), log2up(h), 1, 0, cl >> 6, GS_PSM_32, 0, 0, ncl ? 1 : 0);
	return 1;
}

static int vsync_handler(int cause) // VBLANK_S: wake the render thread (gfx_flip); graph_wait_vsync busy-polls
{
	(void)cause;
	iSignalSema(vsync_sema);
	ExitHandler();
	return 0;
}

int gfx_init(void)
{
	pk = packet_init(PACKET_QW, PACKET_NORMAL);
	dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
	dma_channel_fast_waits(DMA_CHANNEL_GIF);

	graph_vram_clear();
	for (int i = 0; i < 2; i++) {
		fb[i] = (framebuffer_t){ .width = GFX_W, .height = GFX_H, .psm = GS_PSM_16, .mask = 0 };
		// height rounded to whole CT16 pages (64 lines): graph_vram_size does not round it (docs/sources.md)
		fb[i].address = graph_vram_allocate(GFX_W, (GFX_H + 63) / 64 * 64, GS_PSM_16, GRAPH_ALIGN_PAGE);
	}
	vram_lo = fb[1].address + GFX_W * 768 / 2; // CT16 = 2 px per word
	vram_hi = 1048576;                          // GS VRAM in words (ps2tek:110)
	z = (zbuffer_t){ 0 };

	graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_720P, GRAPH_MODE_FRAME, GRAPH_DISABLE);
	graph_set_screen(0, 0, GFX_W, GFX_H);
	u64 d = GS_SET_DISPLAY(DISPLAY_DX, DISPLAY_DY, 0, 0, GFX_W - 1, GFX_H - 1); // native 1280x720: MAGH = MAGV = x1
	*GS_REG_DISPLAY1 = d;
	*GS_REG_DISPLAY2 = d;
	graph_set_bgcolor(0, 0, 0);
	graph_set_framebuffer_filtered(fb[0].address, GFX_W, GS_PSM_16, 0, 0);

	// alpha test NOTEQUAL 0 and ALPHA = (Cs - Cd) * As + Cd come from draw_setup_environment (ps2sdk draw.c)
	qword_t *e = draw_setup_environment(pk->data, 0, &fb[back], &z);
	PACK_GIFTAG(e, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); e++;
	PACK_GIFTAG(e, GS_SET_DTHE(0), GS_REG_DTHE); e++; // no dither: user's choice on the TV (phase-0 modetest)
	// TEX1 is not set by draw_setup_environment: the console keeps the previous program's value (PCSX2 starts at 0).
	PACK_GIFTAG(e, GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0), GS_REG_TEX1); e++; // point sampling, fixed LOD 0
	send(e);
	cur_filter = 0;
	for (int i = 0; i < 2; i++) { // black in both buffers before output starts: VRAM garbage looked like a crash
		gfx_begin();
		gfx_rect(0, 0, GFX_W, GFX_H, 0);
		gfx_end();
		back ^= 1;
	}
	graph_enable_output();

	ee_sema_t sema = { .init_count = 0, .max_count = 1, .option = 0 };
	vsync_sema = CreateSema(&sema);
	AddIntcHandler(INTC_VBLANK_S, vsync_handler, 0);
	EnableIntc(INTC_VBLANK_S);

	slot = vram_alloc(SLOT_W / 64 * (SLOT_H / 64) * 2048, 0);
	static unsigned pal[16] __attribute__((aligned(16))); // white, alpha ramp 0..0x80 (font.py, ui_art.py: index = alpha / 17)
	for (int i = 0; i < 16; i++) pal[i] = (unsigned)(i * 0x80 / 15) << 24 | 0xFFFFFF;
	gfx_tex t;
	if (slot < 0 || !gfx_tex_upload(&t, font_atlas, 512, font_atlas_h, GS_PSM_4, pal)) return 0;
	font_tex0 = t.tex0;
	int cl = vram_alloc(16, 1); // same atlas, CLUT with alpha 0 or 0x80 only: font_tex0 with another CBP (TEX0 bits 37-50)
	static unsigned bin[16] __attribute__((aligned(16)));
	for (int i = 0; i < 16; i++) bin[i] = (i >= 8 ? 0x80u : 0) << 24 | 0xFFFFFF;
	if (cl >= 0) upload(bin, sizeof(bin), 8, 2, GS_PSM_32, cl, 64);
	font_tex0_bin = cl < 0 ? font_tex0 : (font_tex0 & ~(0x3FFFULL << 37)) | (unsigned long long)(cl >> 6) << 37;
	if (!gfx_tex_upload(&t, ui_atlas, UI_ATLAS_W, UI_ATLAS_H, GS_PSM_4, pal)) return 0;
	ui_tex0 = t.tex0;
	if (!gfx_tex_upload(&t, ui_orb, UI_ORB, UI_ORB, GS_PSM_8, ui_orb_clut)) return 0;
	orb_tex0 = t.tex0;
	return 1;
}

// ---- frame: one A+D block, PRIM written per primitive ----
void gfx_begin(void)
{
	q = draw_framebuffer(pk->data, 0, &fb[back]);
	tag = q++;
	qend = pk->data + PACKET_QW - 4; // room for draw_finish
	cur_tex0 = 0;
}

void gfx_alpha(int a) { alpha = a; }
void gfx_tracking(int px) { tracking = px; }
void gfx_text_mode(int m) { text_mode = m; }

static int room(int n) { return q + n <= qend; } // a full packet drops the rest of the frame instead of overflowing
void gfx_reg(int reg, unsigned long long v) { if (room(1)) { PACK_GIFTAG(q, v, reg); q++; } }

static void prim(int type, int gouraud, int textured, int abe, int aa)
{
	PACK_GIFTAG(q, GIF_SET_PRIM(type, gouraud, textured, 0, abe, aa, textured /*FST: UV*/, 0, 0), GIF_REG_PRIM); q++;
}
static void rgba_a(unsigned rgb, int a)
{
	PACK_GIFTAG(q, GIF_SET_RGBAQ(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, a, 0x3F800000), GIF_REG_RGBAQ); q++;
}
static void rgba(unsigned rgb) { rgba_a(rgb, alpha); }
static unsigned half(unsigned rgb) // textured draws: MODULATE by 0x80 = x1, so 0xFF must become 0x80
{
	return ((rgb >> 16 & 255) + 1) >> 1 << 16 | ((rgb >> 8 & 255) + 1) >> 1 << 8 | ((rgb & 255) + 1) >> 1;
}
static void xyz16(int x16, int y16) // coordinates in 1/16 pixel
{
	PACK_GIFTAG(q, GIF_SET_XYZ(x16 + (2048 << 4), y16 + (2048 << 4), 0), GIF_REG_XYZ2); q++;
}
static void uv16(int u16, int v16) { PACK_GIFTAG(q, GIF_SET_UV(u16, v16), GIF_REG_UV); q++; }
static unsigned lerp(unsigned a, unsigned b, int t, int n) // colour a -> b at t / n
{
	if (n <= 0) return a;
	t = t < 0 ? 0 : t > n ? n : t; // outside [0, n] the per-channel result would carry into the next channel
	unsigned r = 0;
	for (int s = 0; s < 24; s += 8) {
		int ca = a >> s & 255, cb = b >> s & 255;
		r |= (unsigned)(ca + (cb - ca) * t / n) << s;
	}
	return r;
}

static void filter(int linear) // TEX1_1 MMAG/MMIN, written only when it changes
{
	if (linear == cur_filter || !room(1)) return;
	PACK_GIFTAG(q, GS_SET_TEX1(1, 0, linear, linear, 0, 0, 0), GS_REG_TEX1); q++; // LCM=1: fixed LOD 0
	cur_filter = linear;
}

void gfx_rect(int x, int y, int w, int h, unsigned rgb)
{
	if (!room(6)) return;
	prim(GS_PRIM_SPRITE, 0, 0, alpha < 0x80, 0);
	rgba(rgb);
	xyz16(x << 4, y << 4);
	xyz16((x + w) << 4, (y + h) << 4);
}

void gfx_grad(int x, int y, int w, int h, unsigned a, unsigned b, int vertical)
{
	if (!room(9)) return;
	prim(GS_PRIM_TRIANGLE_STRIP, 1, 0, alpha < 0x80, 0);
	rgba(a); xyz16(x << 4, y << 4);
	rgba(vertical ? b : a); xyz16(x << 4, (y + h) << 4);
	rgba(vertical ? a : b); xyz16((x + w) << 4, y << 4);
	rgba(b); xyz16((x + w) << 4, (y + h) << 4);
}

void gfx_line(float x0, float y0, float x1, float y1, unsigned rgb, int a0, int a1)
{
	if (!room(5)) return;
	prim(GS_PRIM_LINE, 1, 0, 1, 1); // AA1: coverage-blended edges
	rgba_a(rgb, a0); xyz16((int)(x0 * 16), (int)(y0 * 16));
	rgba_a(rgb, a1); xyz16((int)(x1 * 16), (int)(y1 * 16));
}

// textured quad, screen rect in 1/16 px, texels (u, v)-(u2, v2) in 1/16, colour top -> bottom (true RGB), blended
static void tquad16(unsigned long long tex0, int x0, int y0, int x1, int y1, int u, int v, int u2, int v2,
                    unsigned top, unsigned bottom, int linear)
{
	if (!tex0 || !room(16)) return;
	filter(linear);
	if (tex0 != cur_tex0) { PACK_GIFTAG(q, tex0, GS_REG_TEX0); q++; cur_tex0 = tex0; }
	top = half(top), bottom = half(bottom);
	prim(GS_PRIM_TRIANGLE_STRIP, 1, 1, 1, 0);
	rgba(top); uv16(u, v); xyz16(x0, y0);
	rgba(bottom); uv16(u, v2); xyz16(x0, y1);
	rgba(top); uv16(u2, v); xyz16(x1, y0);
	rgba(bottom); uv16(u2, v2); xyz16(x1, y1);
}

void gfx_icon(int id, int x, int y, unsigned rgb)
{
	const unsigned short *r = ui_rect[id];
	tquad16(ui_tex0, x << 4, y << 4, (x + r[2]) << 4, (y + r[3]) << 4, r[0] << 4, r[1] << 4, (r[0] + r[2]) << 4,
	        (r[1] + r[3]) << 4, rgb, rgb, 0);
}

void gfx_icon_scaled(int id, int x, int y, int w, int h, unsigned top, unsigned bottom)
{
	const unsigned short *r = ui_rect[id];
	tquad16(ui_tex0, x << 4, y << 4, (x + w) << 4, (y + h) << 4, r[0] << 4, r[1] << 4, (r[0] + r[2]) << 4,
	        (r[1] + r[3]) << 4, top, bottom, w != r[2] || h != r[3]);
}

void gfx_orb(int x, int y, int size)
{
	tquad16(orb_tex0, x << 4, y << 4, (x + size) << 4, (y + size) << 4, 0, 0, UI_ORB << 4, UI_ORB << 4, 0xFFFFFF,
	        0xFFFFFF, size != UI_ORB);
}

void gfx_rrect(int x, int y, int w, int h, int r, unsigned top, unsigned bottom)
{
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	unsigned c1 = lerp(top, bottom, r, h), c2 = lerp(top, bottom, h - r, h);
	gfx_grad(x + r, y, w - 2 * r, h, top, bottom, 1);
	if (r <= 0) return;
	gfx_grad(x, y + r, r, h - 2 * r, c1, c2, 1);
	gfx_grad(x + w - r, y + r, r, h - 2 * r, c1, c2, 1);
	const unsigned short *d = ui_rect[UI_DISC_64];
	int u0 = d[0] << 4, v0 = d[1] << 4, um = (d[0] + d[2] / 2) << 4, vm = (d[1] + d[3] / 2) << 4;
	int u1 = (d[0] + d[2]) << 4, v1 = (d[1] + d[3]) << 4;
	tquad16(ui_tex0, x << 4, y << 4, (x + r) << 4, (y + r) << 4, u0, v0, um, vm, top, c1, 1);
	tquad16(ui_tex0, (x + w - r) << 4, y << 4, (x + w) << 4, (y + r) << 4, um, v0, u1, vm, top, c1, 1);
	tquad16(ui_tex0, x << 4, (y + h - r) << 4, (x + r) << 4, (y + h) << 4, u0, vm, um, v1, c2, bottom, 1);
	tquad16(ui_tex0, (x + w - r) << 4, (y + h - r) << 4, (x + w) << 4, (y + h) << 4, um, vm, u1, v1, c2, bottom, 1);
}

void gfx_sprite(const gfx_tex *t, int x, int y, int w, int h, int u, int v, int uw, int vh, unsigned rgb)
{
	tquad16(t->tex0, x << 4, y << 4, (x + w) << 4, (y + h) << 4, u << 4, v << 4, (u + uw) << 4, (v + vh) << 4, rgb,
	        rgb, w != uw || h != vh);
}

static void flush(void) // send what is queued (DMA only) and open a new A+D block at the packet start
{
	PACK_GIFTAG(tag, GIF_SET_TAG(q - tag - 1, 1, 0, 0, 0, 1), GIF_REG_AD);
	dma_channel_send_normal(DMA_CHANNEL_GIF, pk->data, q - pk->data, 0, 0);
	dma_wait_fast();
	q = pk->data;
	tag = q++;
	cur_tex0 = 0;
}

void gfx_image(const void *pix, int w, int h, int x, int y, int dw, int dh)
{
	if (slot < 0 || w > SLOT_W) return;
	int scaled = dw != w || dh != h;
	// TCC=0: alpha from the vertex. PATH3 keeps the order, so each band's upload cannot overwrite the slot before
	// the previous band's quad is drawn (phase-2 design). DMA waits only.
	unsigned long long tex0 = GS_SET_TEX0(slot >> 6, SLOT_W / 64, GS_PSM_16, log2up(SLOT_W), log2up(SLOT_H), 0, 0, 0,
	                                      0, 0, 0, 0);
	for (int r0 = 0; r0 < h; r0 += SLOT_H) {
		int bh = h - r0 < SLOT_H ? h - r0 : SLOT_H;
		flush();
		qword_t *e = draw_texture_transfer(pk->data, (u8 *)pix + r0 * w * 2, w, bh, GS_PSM_16, slot, SLOT_W);
		e = draw_texture_flush(e);
		dma_channel_send_chain(DMA_CHANNEL_GIF, pk->data, e - pk->data, 0, 0);
		dma_wait_fast();
		tquad16(tex0, x << 4, (y << 4) + r0 * dh * 16 / h, (x + dw) << 4, (y << 4) + (r0 + bh) * dh * 16 / h, 0, 0,
		        w << 4, bh << 4, 0xFFFFFF, 0xFFFFFF, scaled);
	}
}

static void text(const gfx_font *f, int x, int y, const char *s, unsigned rgb, int chrome)
{
	// chrome: canvas .chrome-text stops; white -> #E3EAF8 above the split, #9FADCB -> #F2F6FF below it
	int split = y + f->asc * 55 / 100, bot = y + f->line_h;
	for (int x0 = x; *s;) {
		if (*s == '\n') { x = x0; y += f->line_h; split += f->line_h; bot += f->line_h; s++; continue; }
		int gi = next_glyph(&s);
		if (gi < 0) continue;
		const gfx_glyph *g = &f->g[gi];
		int gy0 = y + g->yo, gy1 = gy0 + g->h, gx0 = x + g->xo, gx1 = gx0 + g->w;
		if (!g->w) { x += g->adv + tracking; continue; }
		if (!chrome) {
			int o = text_mode == 2 ? 8 : 0; // 1/16 texel units
			tquad16(text_mode == 1 ? font_tex0_bin : font_tex0, gx0 << 4, gy0 << 4, gx1 << 4, gy1 << 4, (g->u << 4) + o,
			        (g->v << 4) + o, ((g->u + g->w) << 4) + o, ((g->v + g->h) << 4) + o, rgb, rgb, 0);
		}
		else {
			int top = split - y, all = bot - y;
			unsigned ca = 0xFFFFFF, cb = 0xE3EAF8, cc = 0x9FADCB, cd = 0xF2F6FF;
			#define AT(yy) ((yy) < split ? lerp(ca, cb, (yy) - y, top) : lerp(cc, cd, (yy) - split, all - top))
			int ys = gy0 < split && gy1 > split ? split : gy1; // first part ends at the split when the glyph spans it
			tquad16(font_tex0, gx0 << 4, gy0 << 4, gx1 << 4, ys << 4, g->u << 4, g->v << 4, (g->u + g->w) << 4,
			        (g->v + ys - gy0) << 4, AT(gy0), ys == split ? cb : AT(gy1), 0);
			if (ys < gy1)
				tquad16(font_tex0, gx0 << 4, ys << 4, gx1 << 4, gy1 << 4, g->u << 4, (g->v + ys - gy0) << 4,
				        (g->u + g->w) << 4, (g->v + g->h) << 4, cc, AT(gy1), 0);
			#undef AT
		}
		x += g->adv + tracking;
	}
}
void gfx_text(const gfx_font *f, int x, int y, const char *s, unsigned rgb) { text(f, x, y, s, rgb, 0); }
void gfx_text_chrome(const gfx_font *f, int x, int y, const char *s) { text(f, x, y, s, 0, 1); }

void gfx_end(void)
{
	PACK_GIFTAG(tag, GIF_SET_TAG(q - tag - 1, 1, 0, 0, 0, 1), GIF_REG_AD);
	send(q);
}

void gfx_flip(void)
{
	while (PollSema(vsync_sema) >= 0) {} // drop a vblank that already passed: wait for the next one
	WaitSema(vsync_sema);
	graph_set_framebuffer_filtered(fb[back].address, GFX_W, GS_PSM_16, 0, 0);
	back ^= 1;
}
#endif
