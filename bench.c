// Phase-0 render baseline: GS/DMA cost per video mode. Spec: openspec/changes/phase-0-render-baseline.
// Timing = COP0.Count (1 tick per EE cycle, ps2tek:1117-1121) around DMA send + GS FINISH.
// Packets are built before the timer starts: numbers are GIF/GS cost, not EE packet-building cost.
#include <stdio.h>
#ifdef SELFTEST
typedef unsigned int u32;
#else
#include <tamtypes.h>
#endif

u32 median16(u32 *v) // insertion sort, returns v[8] of 16 (upper median)
{
	for (int i = 1; i < 16; i++)
		for (int j = i; j > 0 && v[j - 1] > v[j]; j--) { u32 t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
	return v[8];
}

#ifdef SELFTEST // host check: `make test`
#include <assert.h>
int main(void)
{
	u32 a[16] = {9, 1, 15, 3, 7, 11, 5, 13, 2, 16, 4, 14, 6, 12, 8, 10};
	assert(median16(a) == 9);
	u32 b[16] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 999};
	assert(median16(b) == 5);
	puts("selftest ok");
	return 0;
}
#else
#include <kernel.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <gs_privileged.h>
#include <dma.h>
#include <dma_tags.h>
#include <draw.h>
#include <graph.h>
#include <packet.h>

typedef struct { int mode, interlace, ffmd, w, h, fbw, psm; } test_mode_t;
static const test_mode_t modes[] = {
	{ GRAPH_MODE_HDTV_720P,  GRAPH_MODE_NONINTERLACED, GRAPH_MODE_FRAME,  640, 360,  640, GS_PSM_32 }, // 1
	{ GRAPH_MODE_HDTV_720P,  GRAPH_MODE_NONINTERLACED, GRAPH_MODE_FRAME, 1280, 720, 1280, GS_PSM_16 }, // 2
	{ GRAPH_MODE_HDTV_720P,  GRAPH_MODE_NONINTERLACED, GRAPH_MODE_FRAME, 1280, 720, 1280, GS_PSM_32 }, // 3
	{ GRAPH_MODE_HDTV_1080I, GRAPH_MODE_INTERLACED,    GRAPH_MODE_FIELD,  960, 540,  960, GS_PSM_32 }, // 4
};
#define NMODES (int)(sizeof(modes) / sizeof(modes[0]))
enum { B1_CLEAR, B2_FLAT, B3_UP32, B3_UP8, B4_TEX, NBENCH };
#define NSPRITES 1000
#define REPS 16

static framebuffer_t frame;
static zbuffer_t z;
static int tex32_addr, tex8_addr;
static u32 tex32[256 * 256] __attribute__((aligned(64)));
static u8 tex8[256 * 256] __attribute__((aligned(64)));

static inline u32 cycles(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
static u32 to_us(u32 c) { return (u32)((u64)c * 1000 / 294912); } // 294.912 MHz (ps2tek:345)

static void send_wait(packet_t *p, qword_t *end) // normal (tagless) transfer + FINISH wait
{
	dma_channel_send_normal(DMA_CHANNEL_GIF, p->data, end - p->data, 0, 0);
	draw_wait_finish();
	dma_wait_fast();
}

static void set_mode(packet_t *p, const test_mode_t *m) // from ../ps2-hdtest, incl. DISPLAY fix
{
	graph_vram_clear();
	frame = (framebuffer_t){ .width = m->fbw, .height = m->h, .psm = m->psm, .mask = 0 };
	frame.address = graph_vram_allocate(m->fbw, m->h, m->psm, GRAPH_ALIGN_PAGE);
	z = (zbuffer_t){ 0 };
	tex32_addr = graph_vram_allocate(256, 256, GS_PSM_32, GRAPH_ALIGN_BLOCK);
	tex8_addr = graph_vram_allocate(256, 256, GS_PSM_8, GRAPH_ALIGN_BLOCK);

	graph_set_mode(m->interlace, m->mode, m->ffmd, GRAPH_DISABLE);
	graph_set_screen(0, 0, m->w, m->h);
	const GRAPH_MODE *gm = &graph_mode[m->mode];
	int dy = gm->y, dh = gm->height;
	if (m->interlace && m->ffmd == GRAPH_MODE_FIELD) { dy = (dy - 1) * 2; dh *= 2; }
	u64 display = GS_SET_DISPLAY(gm->x, dy, gm->width / m->w - 1, dh / m->h - 1, gm->width - 1, dh - 1);
	*GS_REG_DISPLAY1 = display;
	*GS_REG_DISPLAY2 = display;
	graph_set_bgcolor(0, 0, 0);
	graph_set_framebuffer_filtered(frame.address, frame.width, frame.psm, 0, 0);
	graph_enable_output();

	qword_t *q = draw_setup_environment(p->data, 0, &frame, &z);
	q = draw_finish(q);
	send_wait(p, q);
}

static qword_t *xyz(qword_t *q, int x, int y)
{
	PACK_GIFTAG(q, GIF_SET_XYZ((x + 2048) << 4, (y + 2048) << 4, 0), GIF_REG_XYZ2);
	return q + 1;
}

static qword_t *flat(qword_t *q, int x, int y, int w, int h, int r, int g, int b)
{
	PACK_GIFTAG(q, GIF_SET_RGBAQ(r, g, b, 0x80, 0x3F800000), GIF_REG_RGBAQ); q++;
	q = xyz(q, x, y);
	return xyz(q, x + w, y + h);
}

// Opens an A+D GIF block for sprites; close() patches NLOOP.
static qword_t *open_sprites(qword_t *q, qword_t **tag, int textured)
{
	*tag = q++;
	PACK_GIFTAG(q, GIF_SET_PRIM(GS_PRIM_SPRITE, 0, textured, 0, 0, 0, textured /*FST: UV*/, 0, 0), GIF_REG_PRIM);
	return q + 1;
}
static qword_t *close_sprites(qword_t *q, qword_t *tag)
{
	PACK_GIFTAG(tag, GIF_SET_TAG(q - tag - 1, 1, 0, 0, 0, 1), GIF_REG_AD);
	return q;
}

// ---- benchmarks: each builds its packet, then times REPS sends ----
static u32 time_normal(packet_t *p, qword_t *end)
{
	u32 v[REPS];
	for (int r = 0; r < REPS; r++) {
		SyncDCache(p->data, end); // pre-clean so the timed span measures GIF/GS, not write-back
		u32 t0 = cycles();
		send_wait(p, end);
		v[r] = cycles() - t0;
	}
	return median16(v);
}

static u32 bench_clear(packet_t *p, const test_mode_t *m)
{
	qword_t *q = draw_clear(p->data, 0, 0, 0, m->w, m->h, 0, 0, 0);
	return time_normal(p, draw_finish(q));
}

static u32 bench_flat(packet_t *p, const test_mode_t *m)
{
	qword_t *tag, *q = open_sprites(p->data, &tag, 0);
	for (int i = 0; i < NSPRITES; i++)
		q = flat(q, (i * 37) % (m->w - 16), (i * 23) % (m->h - 16), 16, 16, i & 255, 128, 255 - (i & 255));
	q = close_sprites(q, tag);
	return time_normal(p, draw_finish(q));
}

static u32 bench_upload(packet_t *p, void *src, int psm, int dest)
{
	qword_t *q = draw_texture_transfer(p->data, src, 256, 256, psm, dest, 256);
	q = draw_texture_flush(q); // END tag closes the chain
	u32 v[REPS];
	packet_t *fin = packet_init(4, PACKET_NORMAL);
	qword_t *fq = draw_finish(fin->data);
	for (int r = 0; r < REPS; r++) {
		SyncDCache(p->data, q);
		u32 t0 = cycles();
		dma_channel_send_chain(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
		dma_wait_fast();
		send_wait(fin, fq); // FINISH after the image data: GS has consumed it
		v[r] = cycles() - t0;
	}
	packet_free(fin);
	return median16(v);
}

static u32 bench_textured(packet_t *p, const test_mode_t *m)
{
	qword_t *q = p->data;
	PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
	PACK_GIFTAG(q, GS_SET_TEX0(tex32_addr >> 6, 4, GS_PSM_32, 8, 8, 1, 1 /*DECAL*/, 0, 0, 0, 0, 0), GS_REG_TEX0); q++;
	qword_t *tag;
	q = open_sprites(q, &tag, 1);
	for (int i = 0; i < NSPRITES; i++) {
		int u = (i * 16) & 255, v = ((i / 16) * 16) & 255;
		int x = (i * 37) % (m->w - 16), y = (i * 23) % (m->h - 16);
		PACK_GIFTAG(q, GIF_SET_UV(u << 4, v << 4), GIF_REG_UV); q++;
		q = xyz(q, x, y);
		PACK_GIFTAG(q, GIF_SET_UV((u + 16) << 4, (v + 16) << 4), GIF_REG_UV); q++;
		q = xyz(q, x + 16, y + 16);
	}
	q = close_sprites(q, tag);
	return time_normal(p, draw_finish(q));
}

// ---- 7-segment output ----
static const u8 SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F}; // bits a..g
static qword_t *digit(qword_t *q, int x, int y, int d, int s) // s = scale (segment length)
{
	int t = s / 4 > 1 ? s / 4 : 1;
	int seg[7][4] = {{t, 0, s, t}, {s + t, t, t, s}, {s + t, 2 * t + s, t, s}, {t, 2 * s + 2 * t, s, t},
	                 {0, 2 * t + s, t, s}, {0, t, t, s}, {t, s + t, s, t}};
	for (int i = 0; i < 7; i++)
		if (SEG[d] & (1 << i))
			q = flat(q, x + seg[i][0], y + seg[i][1], seg[i][2], seg[i][3], 255, 255, 255);
	return q;
}
static qword_t *number(qword_t *q, int x, int y, u32 n, int s)
{
	char buf[12];
	int len = sprintf(buf, "%u", (unsigned)n);
	for (int i = 0; i < len; i++)
		q = digit(q, x + i * (s + s / 2 + 2), y, buf[i] - '0', s);
	return q;
}

static void show(packet_t *p, int idx, const test_mode_t *m, const u32 *us)
{
	int s = m->h / 36; // digit size scales with the buffer height
	qword_t *q = draw_clear(p->data, 0, 0, 0, m->w, m->h, 16, 16, 16), *tag;
	q = open_sprites(q, &tag, 0);
	for (int i = 0; i <= idx; i++) q = flat(q, s + i * s * 2, s, s, s, 255, 255, 255); // mode number
	for (int b = 0; b < NBENCH; b++) {
		int y = s * 4 + b * s * 4;
		for (int i = 0; i <= b; i++) q = flat(q, s + i * s, y + s / 2, s / 2, s / 2, 255, 200, 0); // bench number
		q = number(q, s * 8, y, us[b], s);
	}
	q = close_sprites(q, tag);
	send_wait(p, draw_finish(q));
}

int main(void)
{
	static const char *names[NBENCH] = {"B1 clear", "B2 1000 flat 16x16", "B3 upload 256x256 CT32",
	                                    "B3 upload 256x256 T8", "B4 1000 tex 16x16"};
	packet_t *p = packet_init(8192, PACKET_NORMAL);
	dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
	dma_channel_fast_waits(DMA_CHANNEL_GIF);

	for (int i = 0; i < 256 * 256; i++) { tex32[i] = 0x80000000u | (i * 2654435761u >> 8); tex8[i] = (u8)i; }
	SyncDCache(tex32, tex32 + 256 * 256); // source chain REFs these: write them back once (dma.c syncs tags only)
	SyncDCache(tex8, tex8 + 256 * 256);

	for (int idx = 0;; idx = (idx + 1) % NMODES) {
		const test_mode_t *m = &modes[idx];
		u32 c[NBENCH], us[NBENCH];
		set_mode(p, m);
		c[B1_CLEAR] = bench_clear(p, m);
		c[B2_FLAT] = bench_flat(p, m);
		c[B3_UP32] = bench_upload(p, tex32, GS_PSM_32, tex32_addr);
		c[B3_UP8] = bench_upload(p, tex8, GS_PSM_8, tex8_addr);
		c[B4_TEX] = bench_textured(p, m);
		printf("mode %d (%dx%d psm %d)\n", idx + 1, m->w, m->h, m->psm);
		for (int b = 0; b < NBENCH; b++) {
			us[b] = to_us(c[b]);
			printf("  %-24s %8u cycles %6u us\n", names[b], (unsigned)c[b], (unsigned)us[b]);
		}
		for (int t = 0; t < 600; t++) { show(p, idx, m, us); graph_wait_vsync(); } // ~10 s at 60 Hz
	}
	return 0;
}
#endif
