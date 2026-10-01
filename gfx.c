// Phase-1 render engine. Spec: openspec/changes/phase-1-engine. Mode chosen in docs/phase0-results.md.
#include <string.h>
#include "gfx.h"
#ifdef SELFTEST
typedef unsigned char u8;
#else
#include <tamtypes.h>
#endif

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

// ---- font atlas: KROM single-byte glyphs are 8x15, one byte per row, MSB = left pixel (ps2sdk fontx.c:102-168).
// ASCII 32-126 in a 16x6 grid of 8x16 cells, PSMT4 with the even-x pixel in the low nibble. ----
#define ATLAS_W 128
#define ATLAS_H 96
#define GLYPH_H 15

static void atlas_build(const u8 *glyphs, u8 *out) // glyphs: 95 x 15 bytes starting at ' '
{
	memset(out, 0, ATLAS_W * ATLAS_H / 2);
	for (int i = 0; i < 95; i++)
		for (int y = 0; y < GLYPH_H; y++)
			for (int x = 0; x < 8; x++)
				if (glyphs[i * GLYPH_H + y] & 0x80 >> x) {
					int px = i % 16 * 8 + x, py = i / 16 * 16 + y;
					out[(py * ATLAS_W + px) / 2] |= 1 << (px & 1) * 4;
				}
}
static int glyph_u(int c) { return (c - 32) % 16 * 8; }
static int glyph_v(int c) { return (c - 32) / 16 * 16; }

#ifdef SELFTEST // host check: `make test`
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

	static u8 g[95 * GLYPH_H], a[ATLAS_W * ATLAS_H / 2];
	g[('A' - 32) * GLYPH_H + 2] = 0x81;                  // 'A' row 2: leftmost and rightmost pixel
	atlas_build(g, a);
	int u = glyph_u('A'), v = glyph_v('A');              // 'A' = 33 -> cell (1, 2)
	assert(u == 8 && v == 32);
	int p0 = (v + 2) * ATLAS_W + u, p7 = p0 + 7;
	assert(a[p0 / 2] == 0x01 && a[p7 / 2] == 0x10);      // even x low nibble, odd x high nibble
	int set = 0;
	for (unsigned i = 0; i < sizeof(a); i++) set += __builtin_popcount(a[i]);
	assert(set == 2);
	assert(glyph_u('~') == 14 * 8 && glyph_v('~') == 5 * 16);
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
#include <font.h>

#define DISPLAY_DX 300 // calibrated on SCPH-75001 + HDMI adapter with modetest (docs/phase0-results.md)
#define DISPLAY_DY 27
#define PACKET_QW 16384

static framebuffer_t fb[2];
static zbuffer_t z;
static int back = 1;
static packet_t *pk;
static qword_t *q, *tag, *qend;
static unsigned long long cur_tex0;
static gfx_tex font;
// cover slot: one CT16 image streamed per gfx_image call (design: phase-2-covers). 256x384 = 4x6 CT16 pages.
#define SLOT_W 256
#define SLOT_H 384
static int slot = -1;

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

	// alpha test NOTEQUAL 0 comes from draw_setup_environment: CLUT alpha 0 is transparent (text background)
	qword_t *e = draw_setup_environment(pk->data, 0, &fb[back], &z);
	PACK_GIFTAG(e, GIF_SET_TAG(1, 0, 0, 0, 0, 1), GIF_REG_AD); e++;
	PACK_GIFTAG(e, GS_SET_DTHE(0), GS_REG_DTHE); e++; // no dither: user's choice on the TV (phase-0 modetest)
	send(e);
	for (int i = 0; i < 2; i++) { // black in both buffers before output starts: VRAM garbage looked like a crash
		gfx_begin();
		gfx_rect(0, 0, GFX_W, GFX_H, 0);
		gfx_end();
		back ^= 1;
	}
	graph_enable_output();

	slot = vram_alloc(SLOT_W / 64 * (SLOT_H / 64) * 2048, 0);

	fontx_t krom;
	if (fontx_load("rom0:KROM", &krom, SINGLE_BYTE, 0, 0, 0) < 0) return 0;
	static u8 atlas[ATLAS_W * ATLAS_H / 2] __attribute__((aligned(64)));
	atlas_build((u8 *)krom.font + krom.offset + 32 * krom.charsize, atlas);
	fontx_unload(&krom);
	static const unsigned pal[16] __attribute__((aligned(16))) = {0, 0x80FFFFFF}; // ABGR, 0x80 = alpha 1.0
	return gfx_tex_upload(&font, atlas, ATLAS_W, ATLAS_H, GS_PSM_4, pal);
}

// ---- frame: one A+D block, PRIM written per primitive ----
void gfx_begin(void)
{
	q = draw_framebuffer(pk->data, 0, &fb[back]);
	tag = q++;
	qend = pk->data + PACKET_QW - 4; // room for draw_finish
	cur_tex0 = 0;
}

static int room(int n) { return q + n <= qend; } // a full packet drops the rest of the frame instead of overflowing

static void prim(int type, int gouraud, int textured)
{
	PACK_GIFTAG(q, GIF_SET_PRIM(type, gouraud, textured, 0, 0, 0, textured /*FST: UV*/, 0, 0), GIF_REG_PRIM); q++;
}
static void vtx(int x, int y, unsigned rgb)
{
	PACK_GIFTAG(q, GIF_SET_RGBAQ(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, 0x80, 0x3F800000), GIF_REG_RGBAQ); q++;
	PACK_GIFTAG(q, GIF_SET_XYZ((x + 2048) << 4, (y + 2048) << 4, 0), GIF_REG_XYZ2); q++;
}

void gfx_rect(int x, int y, int w, int h, unsigned rgb)
{
	if (!room(5)) return;
	prim(GS_PRIM_SPRITE, 0, 0);
	vtx(x, y, rgb);
	vtx(x + w, y + h, rgb);
}

void gfx_grad(int x, int y, int w, int h, unsigned a, unsigned b, int vertical)
{
	if (!room(9)) return;
	prim(GS_PRIM_TRIANGLE_STRIP, 1, 0);
	vtx(x, y, a);
	vtx(x, y + h, vertical ? b : a);
	vtx(x + w, y, vertical ? a : b);
	vtx(x + w, y + h, b);
}

void gfx_sprite(const gfx_tex *t, int x, int y, int w, int h, int u, int v, int uw, int vh, unsigned rgb)
{
	if (!t->tex0 || !room(8)) return;
	if (t->tex0 != cur_tex0) { PACK_GIFTAG(q, t->tex0, GS_REG_TEX0); q++; cur_tex0 = t->tex0; }
	prim(GS_PRIM_SPRITE, 0, 1);
	PACK_GIFTAG(q, GIF_SET_RGBAQ(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, 0x80, 0x3F800000), GIF_REG_RGBAQ); q++;
	PACK_GIFTAG(q, GIF_SET_UV(u << 4, v << 4), GIF_REG_UV); q++;
	PACK_GIFTAG(q, GIF_SET_XYZ((x + 2048) << 4, (y + 2048) << 4, 0), GIF_REG_XYZ2); q++;
	PACK_GIFTAG(q, GIF_SET_UV((u + uw) << 4, (v + vh) << 4), GIF_REG_UV); q++;
	PACK_GIFTAG(q, GIF_SET_XYZ((x + w + 2048) << 4, (y + h + 2048) << 4, 0), GIF_REG_XYZ2); q++;
}

void gfx_image(const void *pix, int w, int h, int x, int y)
{
	if (slot < 0 || w > SLOT_W || h > SLOT_H) return;
	// send what is queued, then the upload; PATH3 keeps the order, so this upload cannot overwrite the slot
	// before the previous image's sprite is drawn. Waits for the DMA only, not for the GS.
	PACK_GIFTAG(tag, GIF_SET_TAG(q - tag - 1, 1, 0, 0, 0, 1), GIF_REG_AD);
	dma_channel_send_normal(DMA_CHANNEL_GIF, pk->data, q - pk->data, 0, 0);
	dma_wait_fast();
	qword_t *e = draw_texture_transfer(pk->data, (void *)pix, w, h, GS_PSM_16, slot, SLOT_W);
	e = draw_texture_flush(e);
	dma_channel_send_chain(DMA_CHANNEL_GIF, pk->data, e - pk->data, 0, 0);
	dma_wait_fast();
	q = pk->data;
	tag = q++;
	// TCC=0: alpha from the vertex (0x80); MODULATE by 0x80 = passthrough; TEX1 default = point sampling
	gfx_tex t = { GS_SET_TEX0(slot >> 6, SLOT_W / 64, GS_PSM_16, log2up(SLOT_W), log2up(SLOT_H), 0, 0, 0, 0, 0, 0, 0) };
	cur_tex0 = 0;
	gfx_sprite(&t, x, y, w, h, 0, 0, w, h, 0x808080);
}

void gfx_text(int x, int y, const char *s, unsigned rgb)
{
	for (int x0 = x; *s; s++) {
		unsigned char c = *s;
		if (c == '\n') { x = x0; y += 16; continue; }
		if (c > 32 && c < 127) gfx_sprite(&font, x, y, 8, GLYPH_H, glyph_u(c), glyph_v(c), 8, GLYPH_H, rgb);
		x += 8;
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
