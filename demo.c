// Phase-1 demo: launcher mock-up on the engine + frame-time gate. Spec: openspec/changes/phase-1-engine.
// Frame time = COP0.Count (ps2tek:1117-1121) from gfx_begin to the GS FINISH of gfx_end (EE build + GS draw).
// A vsync-to-vsync gap above 1.5 frames counts as a missed vsync.
#include <stdio.h>
#include <stdlib.h>
#include <tamtypes.h>
#include <gs_psm.h>
#include <libpad.h>
#include "gfx.h"
#include "iop.h"

#define NCARDS 12
#define CARD 256   // on screen: 128x128 PSMT8 texture drawn x2
#define GAP 32
#define WINDOW 600 // frames per measurement (10 s at 60 Hz)
#define FRAME_US 16667

static inline u32 cycles(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
static u32 to_us(u32 c) { return (u32)((u64)c * 1000 / 294912); } // 294.912 MHz (ps2tek:345)
static int cmp(const void *a, const void *b) { return *(const u32 *)a > *(const u32 *)b ? 1 : *(const u32 *)a < *(const u32 *)b ? -1 : 0; }

static gfx_tex cards[NCARDS];

static void make_cards(void) // procedural PSMT8 patterns, one CLUT each: no assets in the ELF
{
	static u8 pix[128 * 128] __attribute__((aligned(64)));
	static unsigned clut[256] __attribute__((aligned(64)));
	for (int k = 0; k < NCARDS; k++) {
		for (int y = 0; y < 128; y++)
			for (int x = 0; x < 128; x++) {
				int dx = x - 64, dy = y - 64;
				pix[y * 128 + x] = k % 3 == 0 ? (x ^ y) * 2 : k % 3 == 1 ? (dx * dx + dy * dy) / 16 : x + y;
			}
		for (int i = 0; i < 256; i++) { // ABGR, alpha 0x80 = opaque
			int r = (i * (k + 3)) & 255, g = (i * 2 + k * 40) & 255, b = 255 - i;
			clut[i] = 0x80000000u | b << 16 | g << 8 | r;
		}
		if (!gfx_tex_upload(&cards[k], pix, 128, 128, GS_PSM_8, clut)) printf("card %d: VRAM pool full\n", k);
	}
}

int main(void)
{
	int usb = iop_init();
	int font = gfx_init();
	if (!font) printf("KROM font not loaded\n");
	make_cards();

	static u32 build[WINDOW];
	u32 med = 0, max = 0, missed = 0, win_missed = 0, windows = 0, last_vsync = 0;
	int sel = 0, n = 0, idle = 0;
	float scroll = 0;
	unsigned prev = 0;
	const char *saved = usb ? "" : "  SIN USB";
	char text[512];

	for (int t = 0;; t++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		if (pressed & PAD_RIGHT && sel < NCARDS - 1) sel++;
		if (pressed & PAD_LEFT && sel > 0) sel--;
		idle = b ? 0 : idle + 1;
		if (idle > 300) sel = t / 90 % NCARDS; // 5 s without input: auto-select so the gate runs hands-free
		float target = sel * (CARD + GAP) - (GFX_W - CARD) / 2;
		scroll += (target - scroll) * 0.15f;

		u32 t0 = cycles();
		gfx_begin();
		gfx_grad(0, 0, GFX_W, GFX_H, 0x101830, 0x000000, 1);
		for (int k = 0; k < NCARDS; k++) {
			int x = k * (CARD + GAP) - (int)scroll, y = 200;
			if (x + CARD < 0 || x > GFX_W) continue;
			if (k == sel) gfx_rect(x - 6, y - 6, CARD + 12, CARD + 12, 0xFFFFFF);
			gfx_sprite(&cards[k], x, y, CARD, CARD, 0, 0, 128, 128, 0x808080);
			snprintf(text, sizeof(text), "Juego %d", k + 1);
			gfx_text(x, y + CARD + 12, text, k == sel ? 0x808080 : 0x404040);
		}
		gfx_rect((t * 16) % (GFX_W - 16), 0, 16, GFX_H, 0x606060); // tearing check: must never look cut
		snprintf(text, sizeof(text),
		         "PS2 LAUNCHER - demo fase 1 (720p CT16, doble buffer)\n"
		         "Cruceta izq/der: elegir juego. Sin tocar nada se mueve solo.\n"
		         "Ventana de %d frames #%u: mediana %u us  max %u us  vsync perdidos %u  (meta: max <= 8333 us, 0 perdidos)%s",
		         WINDOW, windows, med, max, win_missed, saved);
		gfx_text(64, 48, text, 0x808080);
		gfx_end();
		build[n] = to_us(cycles() - t0);

		gfx_flip();
		u32 now = cycles();
		if (n > 0 && to_us(now - last_vsync) > FRAME_US * 3 / 2) missed++; // n == 0: gap includes the file write
		last_vsync = now;

		if (++n == WINDOW) {
			qsort(build, WINDOW, sizeof(u32), cmp);
			med = build[WINDOW / 2], max = build[WINDOW - 1];
			windows++;
			printf("window %u: median %u us max %u us missed %u\n", windows, med, max, missed);
			FILE *f = usb && windows <= 3 ? fopen("mass0:/demo.txt", "a") : NULL;
			if (f) {
				fprintf(f, "demo window %u (%d frames): median %u us  max %u us  missed vsync %u\n", windows, WINDOW,
				        med, max, missed);
				saved = fclose(f) == 0 ? "  GUARDADO en USB" : "  ERROR al guardar";
			}
			win_missed = missed, n = 0, missed = 0;
		}
	}
	return 0;
}
