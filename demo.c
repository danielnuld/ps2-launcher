// Phase-2 demo: real covers streamed 1:1 on the engine + frame-time gate. Spec: openspec/changes/phase-2-covers.
// Covers: .c16 files (tools/covers.py) in mass0:/covers (Floyd-Steinberg) and mass0:/covers_nd (no dither);
// SELECT switches the set. Frame time = COP0.Count (ps2tek:1117-1121) from gfx_begin to the GS FINISH of gfx_end.
// A vsync-to-vsync gap above 1.5 frames counts as a missed vsync.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <time.h>
#include <kernel.h>
#include <tamtypes.h>
#include <libpad.h>
#include "gfx.h"
#include "iop.h"

#define CW 256 // cover size on screen = file size (tools/covers.py)
#define CH 368
#define GAP 32
#define MAXC 64
#define WINDOW 600 // frames per measurement (10 s at 60 Hz)
#define FRAME_US 16667

static inline u32 cycles(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
static u32 to_us(u32 c) { return (u32)((u64)c * 1000 / 294912); } // 294.912 MHz (ps2tek:345)
static int cmp(const void *a, const void *b) { return *(const u32 *)a > *(const u32 *)b ? 1 : *(const u32 *)a < *(const u32 *)b ? -1 : 0; }

static struct { char serial[16]; void *pix[2]; } cv[MAXC];
static int ncv;
static const char *set_names[2] = {"con tramado (Floyd-Steinberg)", "sin tramado"};

static const char *title(const char *serial) // sample covers from xlenore/ps2-covers
{
	static const char *t[][2] = {
		{"SCUS-97113", "ICO"}, {"SCUS-97124", "Jak and Daxter"}, {"SCUS-97199", "Ratchet & Clank"},
		{"SCUS-97328", "Gran Turismo 4"}, {"SCUS-97399", "God of War"}, {"SCUS-97472", "Shadow of the Colossus"},
		{"SLUS-20228", "Silent Hill 2"}, {"SLUS-20312", "Final Fantasy X"}, {"SLUS-20370", "Kingdom Hearts"},
		{"SLUS-20915", "Metal Gear Solid 3"}, {"SLUS-20946", "GTA San Andreas"}, {"SLUS-20964", "Devil May Cry 3"},
		{"SLUS-21115", "Okami"}, {"SLUS-21134", "Resident Evil 4"}, {"SLUS-21782", "Persona 4"}};
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (!strcmp(serial, t[i][0])) return t[i][1];
	return serial;
}

static void splash(const char *msg) // loading screen: shows the ELF is alive while the IOP and covers load
{
	gfx_begin();
	gfx_grad(0, 0, GFX_W, GFX_H, 0x101830, 0x000000, 1);
	const char *t = "PS2 LAUNCHER";
	gfx_text((GFX_W - (int)strlen(t) * 8) / 2, 320, t, 0x808080);
	gfx_text((GFX_W - (int)strlen(msg) * 8) / 2, 360, msg, 0x606060);
	gfx_end();
	gfx_flip();
}

static void *load_c16(const char *path) // header check as in the cover-art spec; NULL if wrong
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	unsigned hdr[4];
	void *pix = NULL;
	if (fread(hdr, 16, 1, f) == 1 && !memcmp(hdr, "C16", 4) && hdr[1] == CW && hdr[2] == CH) {
		pix = memalign(64, CW * CH * 2);
		if (pix && fread(pix, CW * CH * 2, 1, f) != 1) { free(pix); pix = NULL; }
	}
	fclose(f);
	if (pix) SyncDCache(pix, (u8 *)pix + CW * CH * 2); // gfx_image DMAs it later
	else printf("%s: not a 256x368 .c16, skipped\n", path);
	return pix;
}

static void load_covers(void)
{
	DIR *d = opendir("mass0:/covers");
	struct dirent *e;
	char path[300];
	while (d && (e = readdir(d)) && ncv < MAXC) {
		int n = strlen(e->d_name);
		if (n < 5 || n - 4 >= 16 || strcasecmp(e->d_name + n - 4, ".c16")) continue;
		snprintf(path, sizeof(path), "mass0:/covers/%s", e->d_name);
		void *a = load_c16(path);
		if (!a) continue;
		snprintf(path, sizeof(path), "mass0:/covers_nd/%s", e->d_name);
		cv[ncv].pix[0] = a;
		cv[ncv].pix[1] = load_c16(path);
		memcpy(cv[ncv].serial, e->d_name, n - 4);
		cv[ncv].serial[n - 4] = 0;
		ncv++;
		snprintf(path, sizeof(path), "Cargando portadas... %d", ncv);
		splash(path);
	}
	if (d) closedir(d);
	for (int i = 1; i < ncv; i++) // readdir order is the FAT order: sort by title for a stable row
		for (int j = i; j > 0 && strcmp(title(cv[j - 1].serial), title(cv[j].serial)) > 0; j--) {
			__typeof__(cv[0]) t = cv[j]; cv[j] = cv[j - 1]; cv[j - 1] = t;
		}
}

int main(void)
{
	if (!gfx_init()) printf("KROM font not loaded\n"); // video first: black screen + splash from the start
	splash("Iniciando USB...");
	int usb = iop_init();
	splash("Cargando portadas...");
	clock_t c0 = clock();
	if (usb) load_covers();
	unsigned load_ms = (unsigned)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
	printf("%d covers loaded in %u ms\n", ncv, load_ms);

	static u32 build[WINDOW];
	u32 med = 0, max = 0, missed = 0, win_missed = 0, windows = 0, last_vsync = 0;
	u32 up_cycles = 0, up_count = 0, up_us = 0; // gfx_image cost per cover, averaged over the window
	int sel = 0, n = 0, idle = 0, set = 0;
	float scroll = 0;
	unsigned prev = 0;
	const char *saved = usb ? "" : "  SIN USB";
	char text[512];

	for (int t = 0;; t++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		if (pressed & PAD_RIGHT && sel < ncv - 1) sel++;
		if (pressed & PAD_LEFT && sel > 0) sel--;
		if (pressed & PAD_SELECT) set ^= 1;
		idle = b ? 0 : idle + 1;
		if (idle > 300 && ncv) sel = t / 90 % ncv; // 5 s without input: auto-select so the gate runs hands-free
		float target = sel * (CW + GAP) - (GFX_W - CW) / 2;
		scroll += (target - scroll) * 0.15f;

		u32 t0 = cycles();
		gfx_begin();
		gfx_grad(0, 0, GFX_W, GFX_H, 0x101830, 0x000000, 1);
		for (int k = 0; k < ncv; k++) {
			int x = k * (CW + GAP) - (int)scroll, y = 190;
			if (x + CW < 0 || x > GFX_W) continue;
			if (k == sel) gfx_rect(x - 6, y - 6, CW + 12, CH + 12, 0xFFFFFF);
			void *pix = cv[k].pix[set] ? cv[k].pix[set] : cv[k].pix[0];
			u32 u0 = cycles();
			gfx_image(pix, CW, CH, x, y);
			up_cycles += cycles() - u0, up_count++;
			gfx_text(x, y + CH + 14, title(cv[k].serial), k == sel ? 0x808080 : 0x404040);
		}
		if (!ncv) gfx_text(64, 300, "No hay portadas en mass0:/covers (convierte con tools/covers.py)", 0x808080);
		snprintf(text, sizeof(text),
		         "PS2 LAUNCHER - demo fase 2: portadas reales 256x368, 1:1\n"
		         "Cruceta izq/der: elegir juego.  SELECT: cambiar version.  Version: %s\n"
		         "%d portadas cargadas en %u ms.  Subida por portada: %u us\n"
		         "Ventana de %d frames #%u: mediana %u us  max %u us  vsync perdidos %u  (meta: max <= 8333 us, 0 perdidos)%s",
		         set_names[set], ncv, load_ms, up_us, WINDOW, windows, med, max, win_missed, saved);
		gfx_text(64, 40, text, 0x808080);
		gfx_end();
		build[n] = to_us(cycles() - t0);

		gfx_flip();
		u32 now = cycles();
		if (n > 0 && to_us(now - last_vsync) > FRAME_US * 3 / 2) missed++; // n == 0: gap includes the file write
		last_vsync = now;

		if (++n == WINDOW) {
			qsort(build, WINDOW, sizeof(u32), cmp);
			med = build[WINDOW / 2], max = build[WINDOW - 1];
			up_us = up_count ? to_us(up_cycles / up_count) : 0;
			windows++;
			printf("window %u: median %u us max %u us missed %u upload/cover %u us\n", windows, med, max, missed, up_us);
			FILE *f = usb && windows <= 3 ? fopen("mass0:/demo.txt", "a") : NULL;
			if (f) {
				fprintf(f, "covers window %u (%d frames, %d covers, set %d): median %u us  max %u us  missed vsync %u  "
				        "upload/cover %u us  load %u ms\n", windows, WINDOW, ncv, set, med, max, missed, up_us, load_ms);
				saved = fclose(f) == 0 ? "  GUARDADO en USB" : "  ERROR al guardar";
			}
			win_missed = missed, n = 0, missed = 0, up_cycles = 0, up_count = 0;
		}
	}
	return 0;
}
