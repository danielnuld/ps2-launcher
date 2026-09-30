// modetest: compare 720p variants on the TV and calibrate the picture position.
//   1) 1280x720 CT16, double buffer   2) same + GS dithering   3) 1280x720 CT32, single buffer
// Gradients show 16-bit banding; the fast white bar shows tearing. D-pad moves the picture (DISPLAY DX/DY).
// Sources: docs/sources.md (GS pages, DISPLAY fields, dither matrix, OPL/ps2sdk 720p offsets).
#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <tamtypes.h>
#include <gif_tags.h>
#include <gs_gp.h>
#include <gs_psm.h>
#include <gs_privileged.h>
#include <dma.h>
#include <draw.h>
#include <graph.h>
#include <packet.h>
#include <font.h>
#include <libpad.h>
#include "iop.h"

#define W 1280
#define H 720
enum { V_CT16, V_CT16_DITHER, V_CT32, NVAR };
static const char *var_names[NVAR] = {"720p 16 bits, doble buffer", "720p 16 bits + dither, doble buffer",
                                      "720p 32 bits, un buffer"};
// 720p DISPLAY offsets: ps2sdk graph_mode.c:26 (x=420, y=40) vs OPL src/gsm.c:86 makeDISPLAY(..., DY=24, DX=302).
static const int preset_dx[2] = {420, 302}, preset_dy[2] = {40, 24};
static const char *preset_names[2] = {"ps2sdk", "OPL GSM"};

static framebuffer_t fb[2];
static zbuffer_t z;
static int nbuf, back;
static fontx_t krom;

static int round_page_h(int h, int psm) { int ph = psm == GS_PSM_16 ? 64 : 32; return (h + ph - 1) / ph * ph; } // gsKit gsTexture.c

static void set_display(int dx, int dy)
{
	u64 d = GS_SET_DISPLAY(dx, dy, 0, 0, W - 1, H - 1); // native 1280x720: MAGH = MAGV = x1
	*GS_REG_DISPLAY1 = d;
	*GS_REG_DISPLAY2 = d;
}

// DIMX: 16 entries of 3 bits (signed -4..3) every 4 bits. Matrix from gsKit gsInit.c:472-473 (signed form in its
// comment). ps2sdk GS_SET_DIMX masks entries to 2 bits and gsKit's GS_SETREG_DIMX has a typo at bit 56, so pack here.
static u64 dimx(void)
{
	static const s8 m[16] = {-4, 2, -3, 3, 0, -2, 1, -1, -3, 3, -4, 2, 1, -1, 0, -2};
	u64 v = 0;
	for (int i = 0; i < 16; i++) v |= (u64)(m[i] & 7) << (i * 4);
	return v;
}

static void send(packet_t *p, qword_t *q)
{
	q = draw_finish(q);
	dma_channel_send_normal(DMA_CHANNEL_GIF, p->data, q - p->data, 0, 0);
	draw_wait_finish();
	dma_wait_fast();
}

static void apply_variant(packet_t *p, int var, int dx, int dy)
{
	int psm = var == V_CT32 ? GS_PSM_32 : GS_PSM_16;
	nbuf = var == V_CT32 ? 1 : 2; // CT32 720p = 942 080 words: only one fits in 1 048 576
	graph_vram_clear();
	for (int i = 0; i < nbuf; i++) {
		fb[i] = (framebuffer_t){ .width = W, .height = H, .psm = psm, .mask = 0 };
		fb[i].address = graph_vram_allocate(W, round_page_h(H, psm), psm, GRAPH_ALIGN_PAGE);
	}
	z = (zbuffer_t){ 0 };
	back = nbuf - 1; // single buffer draws on the displayed one
	graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_HDTV_720P, GRAPH_MODE_FRAME, GRAPH_DISABLE);
	graph_set_screen(0, 0, W, H);
	set_display(dx, dy);
	graph_set_bgcolor(0, 0, 0);
	graph_set_framebuffer_filtered(fb[0].address, W, psm, 0, 0);
	graph_enable_output();

	qword_t *q = draw_setup_environment(p->data, 0, &fb[back], &z);
	PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
	PACK_GIFTAG(q, dimx(), GS_REG_DIMX); q++;
	PACK_GIFTAG(q, GS_SET_DTHE(var == V_CT16_DITHER), GS_REG_DTHE); q++;
	send(p, q);
}

// ---- drawing (A+D mode, PRIM written per primitive) ----
static qword_t *prim(qword_t *q, int type, int gouraud)
{
	PACK_GIFTAG(q, GIF_SET_PRIM(type, gouraud, 0, 0, 0, 0, 0, 0, 0), GIF_REG_PRIM);
	return q + 1;
}
static qword_t *vtx(qword_t *q, int x, int y, u32 rgb)
{
	PACK_GIFTAG(q, GIF_SET_RGBAQ(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, 0x80, 0x3F800000), GIF_REG_RGBAQ); q++;
	PACK_GIFTAG(q, GIF_SET_XYZ((x + 2048) << 4, (y + 2048) << 4, 0), GIF_REG_XYZ2);
	return q + 1;
}
static qword_t *rect(qword_t *q, int x, int y, int w, int h, u32 rgb)
{
	q = prim(q, GS_PRIM_SPRITE, 0);
	q = vtx(q, x, y, rgb);
	return vtx(q, x + w, y + h, rgb);
}
// Gouraud-shaded rectangle as a 4-vertex triangle strip: left colour a, right colour b (horizontal) or top/bottom.
static qword_t *grad(qword_t *q, int x, int y, int w, int h, u32 a, u32 b, int vertical)
{
	q = prim(q, GS_PRIM_TRIANGLE_STRIP, 1);
	q = vtx(q, x, y, a);
	q = vtx(q, x, y + h, vertical ? b : a);
	q = vtx(q, x + w, y, vertical ? a : b);
	return vtx(q, x + w, y + h, b);
}

static void frame(packet_t *p, int var, int t, int dx, int dy, int preset, const char *status, u32 last_us)
{
	qword_t *q = p->data;
	q = draw_framebuffer(q, 0, &fb[back]);
	qword_t *tag = q++;
	q = grad(q, 0, 0, W, H, 0x101830, 0x000000, 1);                         // background: dark blue -> black
	q = grad(q, 64, 240, W - 128, 60, 0x000000, 0xFFFFFF, 0);                // grey ramp (banding test)
	q = grad(q, 64, 310, W - 128, 40, 0x000000, 0xFF0000, 0);
	q = grad(q, 64, 360, W - 128, 40, 0x000000, 0x00FF00, 0);
	q = grad(q, 64, 410, W - 128, 40, 0x000000, 0x0000FF, 0);
	for (int i = 0; i < 6; i++)                                               // scrolling cards (smoothness)
		q = rect(q, 64 + ((t * 3 + i * 220) % (W - 128 - 180)), 480 + (i % 2) * 70, 180, 60, 0x3060A0 + i * 0x101000);
	q = rect(q, (t * 16) % (W - 16), 0, 16, H, 0xFFFFFF);                    // fast full-height bar (tearing test)
	// 1-px frame + 32-px corner marks: all four edges must touch the TV's borders after calibration.
	q = rect(q, 0, 0, W, 1, 0xFFFF00); q = rect(q, 0, H - 1, W, 1, 0xFFFF00);
	q = rect(q, 0, 0, 1, H, 0xFFFF00); q = rect(q, W - 1, 0, 1, H, 0xFFFF00);
	for (int c = 0; c < 4; c++) {
		int x = c & 1 ? W - 32 : 0, y = c & 2 ? H - 4 : 0;
		q = rect(q, x, y, 32, 4, 0xFF4040);
		q = rect(q, c & 1 ? W - 4 : 0, c & 2 ? H - 32 : 0, 4, 32, 0xFF4040);
	}
	PACK_GIFTAG(tag, GIF_SET_TAG(q - tag - 1, 1, 0, 0, 0, 1), GIF_REG_AD);

	char text[768]; // plain ASCII: KROM shows '~' as an overline
	snprintf(text, sizeof(text),
	         "MODETEST - variante %d de %d: %s\n"
	         "L1 / R1: cambiar variante     Cruceta: mover la imagen     SELECT: posicion ps2sdk / OPL\n"
	         "Triangulo: guardar variante y posicion en la USB (mass0:/modetest.txt)\n"
	         "Mira los degradados (escalones = bandeado) y la barra blanca (cortada = tearing).\n"
	         "Mueve con la cruceta hasta ver el borde amarillo y las 4 esquinas rojas completas.\n"
	         "DX=%d DY=%d (base %s)   dibujo del frame anterior: %u us   %s",
	         var + 1, NVAR, var_names[var], dx, dy, preset_names[preset], (unsigned)last_us, status);
	vertex_t v = { .x = 80.0f, .y = 40.0f, .z = 0 };
	color_t col = { .r = 255, .g = 255, .b = 255, .a = 0x80, .q = 1.0f };
	if (krom.font)
		q = fontx_print_ascii(q, 0, (const unsigned char *)text, LEFT_ALIGN, &v, &col, &krom);
	send(p, q);
}

static inline u32 cycles(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }

int main(void)
{
	int usb = iop_init();
	if (fontx_load("rom0:KROM", &krom, SINGLE_BYTE, 0, 2, 0) < 0) krom.font = NULL;
	packet_t *p = packet_init(65536, PACKET_NORMAL);
	dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
	dma_channel_fast_waits(DMA_CHANNEL_GIF);

	int var = V_CT16, preset = 0, dx = preset_dx[0], dy = preset_dy[0], repeat = 0;
	unsigned prev = 0;
	u32 last_us = 0;
	const char *status = usb ? "USB lista" : "SIN USB";
	apply_variant(p, var, dx, dy);
	for (int t = 0;; t++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		int change = 0;
		if (pressed & PAD_R1) { var = (var + 1) % NVAR; change = 1; }
		if (pressed & PAD_L1) { var = (var + NVAR - 1) % NVAR; change = 1; }
		if (pressed & PAD_SELECT) { preset ^= 1; dx = preset_dx[preset]; dy = preset_dy[preset]; set_display(dx, dy); }
		if (b & (PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN)) {
			if (pressed || ++repeat > 20) { // one step per press, then auto-repeat while held
				dx += (b & PAD_RIGHT ? 4 : 0) - (b & PAD_LEFT ? 4 : 0);
				dy += (b & PAD_DOWN ? 1 : 0) - (b & PAD_UP ? 1 : 0);
				set_display(dx, dy);
			}
		} else repeat = 0;
		if (pressed & PAD_TRIANGLE) {
			FILE *f = usb ? fopen("mass0:/modetest.txt", "a") : NULL;
			if (f) {
				fprintf(f, "variant %d (%s)  DX=%d DY=%d  base=%s  frame_us=%u\n", var + 1, var_names[var], dx, dy,
				        preset_names[preset], (unsigned)last_us);
				status = fclose(f) == 0 ? "GUARDADO en USB" : "ERROR al guardar";
			} else status = "SIN USB: no se guardo";
		}
		if (change) apply_variant(p, var, dx, dy);

		u32 t0 = cycles();
		frame(p, var, t, dx, dy, preset, status, last_us);
		last_us = (u32)((u64)(cycles() - t0) * 1000 / 294912); // EE build + GS draw, 294.912 MHz (ps2tek:345)
		graph_wait_vsync();
		if (nbuf == 2) { // show what was just drawn, draw the next frame on the other buffer
			graph_set_framebuffer_filtered(fb[back].address, W, fb[back].psm, 0, 0);
			back ^= 1;
		}
	}
	return 0;
}
