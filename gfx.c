// Render engine. Specs: openspec/specs/render-engine, cover-art; changes/phase-3-ui. Mode: docs/phase0-results.md.
#include "gfx.h"

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

int gfx_text_width(const gfx_font *f, const char *s)
{
	int w = 0;
	for (; *s && *s != '\n'; s++)
		if (*s >= 32 && *s < 127) w += f->g[*s - 32].adv;
	return w;
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

	const gfx_font *f = &gfx_font_title;
	assert(gfx_text_width(f, "AB") == f->g['A' - 32].adv + f->g['B' - 32].adv);
	assert(gfx_text_width(f, "AB\nCCCC") == gfx_text_width(f, "AB")); // first line only
	assert(gfx_text_width(f, "") == 0 && f->g[0].w == 0 && f->g[0].adv > 0); // space: no sprite, has advance
	for (int c = 33; c < 127; c++) assert(f->g[c - 32].w > 0 && f->g[c - 32].u + f->g[c - 32].w <= 512);
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
#define PACKET_QW 16384
// cover slot: one 256x192 CT16 band at a time = 4x3 CT16 pages (design: phase-3-ui)
#define SLOT_W 256
#define SLOT_H 192

extern const unsigned char font_atlas[];
extern const int font_atlas_h;

static framebuffer_t fb[2];
static zbuffer_t z;
static int back = 1;
static packet_t *pk;
static qword_t *q, *tag, *qend;
static unsigned long long cur_tex0, font_tex0;
static int slot = -1, alpha = 0x80;

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
	// TEX1 is not set by draw_setup_environment: the console keeps what the previous program left (PCSX2 starts at 0).
	// Point sampling, fixed LOD 0 (LCM=1, MXL=0), no mipmaps. Text showed a line above the glyphs on the console only.
	PACK_GIFTAG(e, GS_SET_TEX1(1, 0, 0, 0, 0, 0, 0), GS_REG_TEX1); e++;
	send(e);
	for (int i = 0; i < 2; i++) { // black in both buffers before output starts: VRAM garbage looked like a crash
		gfx_begin();
		gfx_rect(0, 0, GFX_W, GFX_H, 0);
		gfx_end();
		back ^= 1;
	}
	graph_enable_output();

	slot = vram_alloc(SLOT_W / 64 * (SLOT_H / 64) * 2048, 0);
	static unsigned pal[16] __attribute__((aligned(16))); // white, alpha ramp 0..0x80 (font.py: index = alpha / 17)
	for (int i = 0; i < 16; i++) pal[i] = (unsigned)(i * 0x80 / 15) << 24 | 0xFFFFFF;
	gfx_tex t;
	if (slot < 0 || !gfx_tex_upload(&t, font_atlas, 512, font_atlas_h, GS_PSM_4, pal)) return 0;
	font_tex0 = t.tex0;
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

static int room(int n) { return q + n <= qend; } // a full packet drops the rest of the frame instead of overflowing

static void prim(int type, int gouraud, int textured, int abe)
{
	PACK_GIFTAG(q, GIF_SET_PRIM(type, gouraud, textured, 0, abe, 0, textured /*FST: UV*/, 0, 0), GIF_REG_PRIM); q++;
}
static void rgba(unsigned rgb)
{
	PACK_GIFTAG(q, GIF_SET_RGBAQ(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, alpha, 0x3F800000), GIF_REG_RGBAQ); q++;
}
static void xyz16(int x16, int y16) // coordinates in 1/16 pixel
{
	PACK_GIFTAG(q, GIF_SET_XYZ(x16 + (2048 << 4), y16 + (2048 << 4), 0), GIF_REG_XYZ2); q++;
}

void gfx_rect(int x, int y, int w, int h, unsigned rgb)
{
	if (!room(6)) return;
	prim(GS_PRIM_SPRITE, 0, 0, alpha < 0x80);
	rgba(rgb);
	xyz16(x << 4, y << 4);
	xyz16((x + w) << 4, (y + h) << 4);
}

void gfx_grad(int x, int y, int w, int h, unsigned a, unsigned b, int vertical)
{
	if (!room(9)) return;
	prim(GS_PRIM_TRIANGLE_STRIP, 1, 0, alpha < 0x80);
	rgba(a); xyz16(x << 4, y << 4);
	rgba(vertical ? b : a); xyz16(x << 4, (y + h) << 4);
	rgba(vertical ? a : b); xyz16((x + w) << 4, y << 4);
	rgba(b); xyz16((x + w) << 4, (y + h) << 4);
}

// textured sprite, screen rect in 1/16 px, texels (u, v)-(u2, v2)
static void sprite16(unsigned long long tex0, int x0, int y0, int x1, int y1, int u, int v, int u2, int v2, int abe,
                     unsigned rgb)
{
	if (!tex0 || !room(8)) return;
	if (tex0 != cur_tex0) { PACK_GIFTAG(q, tex0, GS_REG_TEX0); q++; cur_tex0 = tex0; }
	prim(GS_PRIM_SPRITE, 0, 1, abe);
	rgba(rgb);
	PACK_GIFTAG(q, GIF_SET_UV(u << 4, v << 4), GIF_REG_UV); q++;
	xyz16(x0, y0);
	PACK_GIFTAG(q, GIF_SET_UV(u2 << 4, v2 << 4), GIF_REG_UV); q++;
	xyz16(x1, y1);
}

void gfx_sprite(const gfx_tex *t, int x, int y, int w, int h, int u, int v, int uw, int vh, unsigned rgb)
{
	sprite16(t->tex0, x << 4, y << 4, (x + w) << 4, (y + h) << 4, u, v, u + uw, v + vh, alpha < 0x80, rgb);
}

static void filter(int linear) // TEX1_1 MMAG/MMIN: bilinear only while an image is scaled
{
	if (!room(1)) return;
	PACK_GIFTAG(q, GS_SET_TEX1(1, 0, linear, linear, 0, 0, 0), GS_REG_TEX1); q++; // LCM=1: fixed LOD 0
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
	// TCC=0: alpha from the vertex; MODULATE by 0x80 = passthrough. PATH3 keeps the order, so each band's upload
	// cannot overwrite the slot before the previous band's sprite is drawn (phase-2 design). DMA waits only.
	unsigned long long tex0 = GS_SET_TEX0(slot >> 6, SLOT_W / 64, GS_PSM_16, log2up(SLOT_W), log2up(SLOT_H), 0, 0, 0,
	                                      0, 0, 0, 0);
	for (int r0 = 0; r0 < h; r0 += SLOT_H) {
		int bh = h - r0 < SLOT_H ? h - r0 : SLOT_H;
		flush();
		qword_t *e = draw_texture_transfer(pk->data, (u8 *)pix + r0 * w * 2, w, bh, GS_PSM_16, slot, SLOT_W);
		e = draw_texture_flush(e);
		dma_channel_send_chain(DMA_CHANNEL_GIF, pk->data, e - pk->data, 0, 0);
		dma_wait_fast();
		if (scaled) filter(1);
		sprite16(tex0, x << 4, (y << 4) + r0 * dh * 16 / h, (x + dw) << 4, (y << 4) + (r0 + bh) * dh * 16 / h, 0, 0,
		         w, bh, alpha < 0x80, 0x808080);
		if (scaled) filter(0);
	}
}

void gfx_text(const gfx_font *f, int x, int y, const char *s, unsigned rgb)
{
	for (int x0 = x; *s; s++) {
		if (*s == '\n') { x = x0; y += f->line_h; continue; }
		if (*s < 32 || *s > 126) continue;
		const gfx_glyph *g = &f->g[*s - 32];
		if (g->w)
			sprite16(font_tex0, (x + g->xo) << 4, (y + g->yo) << 4, (x + g->xo + g->w) << 4, (y + g->yo + g->h) << 4,
			         g->u, g->v, g->u + g->w, g->v + g->h, 1, rgb);
		x += g->adv;
	}
}

void gfx_end(void)
{
	PACK_GIFTAG(tag, GIF_SET_TAG(q - tag - 1, 1, 0, 0, 0, 1), GIF_REG_AD);
	send(q);
}

void gfx_flip(void)
{
	graph_wait_vsync();
	graph_set_framebuffer_filtered(fb[back].address, GFX_W, GS_PSM_16, 0, 0);
	back ^= 1;
}
#endif
