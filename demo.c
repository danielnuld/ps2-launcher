// Launcher UI (phase 3): carousel of real covers on the engine. Specs: openspec launcher-ui, cover-art.
// Covers: mass0:/covers/<serial>.c16 (256x368) + <serial>_s.c16 (184x264), made by tools/covers.py.
// Frame time = COP0.Count (ps2tek:1117-1121) from gfx_begin to the GS FINISH of gfx_end; a vsync-to-vsync gap
// above 1.5 frames counts as missed. SELECT shows it; the first 3 windows of 600 frames go to mass0:/demo.txt.
// Saves: both memory cards' root dirs are read once at boot (ps2sdk libmc). Text colours modulate the white
// glyphs: 0x808080 = white.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <dirent.h>
#include <time.h>
#include <kernel.h>
#include <tamtypes.h>
#include <libpad.h>
#include <libmc.h>
#include "gfx.h"
#include "iop.h"

#define LW 256 // selected cover = <serial>.c16
#define LH 368
#define SW 184 // carousel cover = <serial>_s.c16
#define SH 264
#define D (SW + 28)            // centre-to-centre distance of small covers
#define E ((LW - SW) / 2)      // extra room around the selected one
#define TOP 192                // top of the selected cover (carousel centred between header and footer)
#define CY (TOP + LH / 2)      // carousel centre line
#define MAXC 128
#define WINDOW 600             // frames per measurement (10 s at 60 Hz)
#define FRAME_US 16667
#define WHITE 0x808080
#define GREY 0x505050

static inline u32 cycles(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
static u32 to_us(u32 c) { return (u32)((u64)c * 1000 / 294912); } // 294.912 MHz (ps2tek:345)
static int cmp(const void *a, const void *b) { return *(const u32 *)a > *(const u32 *)b ? 1 : *(const u32 *)a < *(const u32 *)b ? -1 : 0; }

static struct { char serial[16]; void *big, *small; } cv[MAXC];
static int ncv;

// ---- saves: root dirs of both cards, read once (design: phase-3-ui). mc[p] < 0: no PS2 card in port p ----
#define MAXDIR 128
static sceMcTblGetDir mcdir[2][MAXDIR];
static int mcn[2] = {-1, -1};

static void scan_cards(void)
{
	if (mcInit(MC_TYPE_MC) < 0) return;
	for (int p = 0; p < 2; p++) {
		int type, free, format, ret;
		for (int i = 0; i < 2; i++) { // the first call after boot reports "new card" (mc_example.c)
			mcGetInfo(p, 0, &type, &free, &format);
			mcSync(0, NULL, &ret);
		}
		if ((ret != 0 && ret != -1) || type != MC_TYPE_PS2 || !format) continue;
		mcGetDir(p, 0, "/*", 0, MAXDIR, mcdir[p]);
		mcSync(0, NULL, &ret);
		mcn[p] = ret < 0 ? 0 : ret;
		for (int i = 0; i < mcn[p]; i++) printf("mc%d: %s\n", p, mcdir[p][i].EntryName);
	}
}

static void save_info(const char *serial, char *out, int n) // "2 saves en Memory Card 1  -  14/03/2026"
{
	int count = 0, card = -1;
	u64 best = 0;
	const sceMcStDateTime *bd = NULL;
	for (int p = 0; p < 2; p++)
		for (int i = 0; i < mcn[p]; i++) {
			const sceMcTblGetDir *e = &mcdir[p][i];
			if (!(e->AttrFile & MC_ATTR_SUBDIR) || !strstr((const char *)e->EntryName, serial)) continue;
			const sceMcStDateTime *t = &e->_Modify;
			u64 k = (u64)t->Year << 40 | (u64)t->Month << 32 | (u64)t->Day << 24 | t->Hour << 16 | t->Min << 8 | t->Sec;
			count++;
			if (card < 0 || k > best) best = k, bd = t, card = p;
		}
	if (mcn[0] < 0 && mcn[1] < 0) snprintf(out, n, "Sin memory card");
	else if (!count) snprintf(out, n, "Sin saves");
	else snprintf(out, n, "%d save%s en Memory Card %d  -  %02d/%02d/%04d", count, count > 1 ? "s" : "", card + 1,
	              bd->Day, bd->Month, bd->Year); // ponytail: JST as stored, day may be off near midnight
}

static const char *title(const char *serial) // sample covers from xlenore/ps2-covers (Escritorio/juegos-demo.txt)
{
	static const char *t[][2] = {
		{"SCUS-97113", "ICO"}, {"SCUS-97124", "Jak and Daxter"}, {"SCUS-97199", "Ratchet & Clank"},
		{"SCUS-97328", "Gran Turismo 4"}, {"SCUS-97399", "God of War"}, {"SCUS-97472", "Shadow of the Colossus"},
		{"SLUS-20228", "Silent Hill 2"}, {"SLUS-20312", "Final Fantasy X"}, {"SLUS-20370", "Kingdom Hearts"},
		{"SLUS-20915", "Metal Gear Solid 3: Snake Eater"}, {"SLUS-20946", "Grand Theft Auto: San Andreas"},
		{"SLUS-20964", "Devil May Cry 3"}, {"SLUS-21115", "Okami"}, {"SLUS-21134", "Resident Evil 4"},
		{"SLUS-21782", "Persona 4"}};
	for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (!strcmp(serial, t[i][0])) return t[i][1];
	return serial;
}

static void text_c(const gfx_font *f, int y, const char *s, unsigned rgb) // centred on the screen
{
	gfx_text(f, (GFX_W - gfx_text_width(f, s)) / 2, y, s, rgb);
}

// one footer hint, right-aligned at x: [KEY] label. Returns the x where the next hint (to its left) ends.
static int hint(int x, const char *key, const char *label)
{
	int lw = gfx_text_width(&gfx_font_body, label), kw = gfx_text_width(&gfx_font_body, key);
	x -= lw;
	gfx_alpha(0x80);
	gfx_text(&gfx_font_body, x, 664, label, GREY);
	x -= 12 + kw + 16;
	gfx_alpha(0x50);
	gfx_rect(x, 662, kw + 16, 30, 0x8090B0);
	gfx_alpha(0x80);
	gfx_text(&gfx_font_body, x + 8, 664, key, WHITE);
	return x - 36;
}

static void background(void)
{
	gfx_alpha(0x80);
	gfx_grad(0, 0, GFX_W, GFX_H, 0x101830, 0x000000, 1);
}

static void splash(const char *msg, int done, int total) // loading screen: the ELF is alive from the first frame
{
	gfx_begin();
	background();
	text_c(&gfx_font_title, 280, "PS2 LAUNCHER", WHITE);
	text_c(&gfx_font_body, 340, msg, GREY);
	if (total > 0) {
		gfx_rect(440, 384, 400, 6, 0x202838);
		gfx_rect(440, 384, 400 * done / total, 6, 0x6080C0);
	}
	gfx_end();
	gfx_flip();
}

static void *load_c16(const char *path, unsigned w, unsigned h) // header check (cover-art spec); NULL if wrong
{
	FILE *f = fopen(path, "rb");
	unsigned hdr[4];
	void *pix = NULL;
	if (f && fread(hdr, 16, 1, f) == 1 && !memcmp(hdr, "C16", 4) && hdr[1] == w && hdr[2] == h) {
		pix = memalign(64, w * h * 2);
		if (pix && fread(pix, w * h * 2, 1, f) != 1) { free(pix); pix = NULL; }
	}
	if (f) fclose(f);
	if (pix) SyncDCache(pix, (u8 *)pix + w * h * 2); // gfx_image DMAs it later
	else printf("%s: missing or not a %ux%u .c16, skipped\n", path, w, h);
	return pix;
}

static int is_big(const char *name) // "<serial>.c16", not "<serial>_s.c16"
{
	int n = strlen(name);
	return n >= 5 && n - 4 < 16 && !strcasecmp(name + n - 4, ".c16") && !(n >= 6 && !strncasecmp(name + n - 6, "_s", 2));
}

static void load_covers(void)
{
	int total = 0;
	DIR *d = opendir("mass0:/covers");
	struct dirent *e;
	while (d && (e = readdir(d))) total += is_big(e->d_name);
	if (d) closedir(d);
	d = opendir("mass0:/covers");
	char path[300], msg[64];
	for (int done = 0; d && (e = readdir(d)) && ncv < MAXC;) {
		if (!is_big(e->d_name)) continue;
		int n = strlen(e->d_name) - 4;
		memcpy(cv[ncv].serial, e->d_name, n);
		cv[ncv].serial[n] = 0;
		snprintf(path, sizeof(path), "mass0:/covers/%s", e->d_name);
		cv[ncv].big = load_c16(path, LW, LH);
		snprintf(path, sizeof(path), "mass0:/covers/%s_s.c16", cv[ncv].serial);
		cv[ncv].small = cv[ncv].big ? load_c16(path, SW, SH) : NULL;
		if (cv[ncv].small) ncv++;
		else free(cv[ncv].big);
		snprintf(msg, sizeof(msg), "Cargando portadas... %d de %d", ++done, total);
		splash(msg, done, total);
	}
	if (d) closedir(d);
	for (int i = 1; i < ncv; i++) // readdir order is the FAT order: sort by title for a stable row
		for (int j = i; j > 0 && strcmp(title(cv[j - 1].serial), title(cv[j].serial)) > 0; j--) {
			__typeof__(cv[0]) t = cv[j]; cv[j] = cv[j - 1]; cv[j - 1] = t;
		}
}

static void draw_cover(int i, float s) // grow factor f = 1 at the centre, 0 one step away (design: phase-3-ui)
{
	float di = i - s, a = fabsf(di), f = a < 1 ? 1 - a : 0;
	int w = (int)lroundf(SW + (LW - SW) * f), h = (int)lroundf(SH + (LH - SH) * f);
	int cx = (int)lroundf(GFX_W / 2 + di * D + (di < 0 ? -E : E) * (a < 1 ? a : 1));
	int x = cx - w / 2, y = CY - h / 2;
	if (x + w < 0 || x > GFX_W) return;
	if (f > 0) { // selection frame, faded with the grow factor
		gfx_alpha((int)(0x80 * f));
		gfx_rect(x - 5, y - 5, w + 10, h + 10, 0xE0E8FF);
	}
	gfx_alpha(0x80);
	if (w == LW && h == LH) gfx_image(cv[i].big, LW, LH, x, y, w, h);           // at rest: pixel-exact
	else if (w == SW && h == SH) gfx_image(cv[i].small, SW, SH, x, y, w, h);
	else if (f > 0.5f) gfx_image(cv[i].big, LW, LH, x, y, w, h);              // animating: bilinear
	else gfx_image(cv[i].small, SW, SH, x, y, w, h);
}

int main(void)
{
	if (!gfx_init()) printf("gfx_init: VRAM pool too small\n");
	splash("Iniciando USB...", 0, 0);
	int usb = iop_init();
	splash("Leyendo memory cards...", 0, 0);
	scan_cards();
	splash("Cargando portadas...", 0, 0);
	clock_t c0 = clock();
	if (usb) load_covers();
	unsigned load_ms = (unsigned)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
	printf("%d covers loaded in %u ms\n", ncv, load_ms);

	static u32 build[WINDOW];
	u32 med = 0, max = 0, missed = 0, win_missed = 0, windows = 0, last_vsync = 0;
	int sel = 0, n = 0, idle = 0, overlay = 0, toast = 0;
	float s = 0;
	unsigned prev = 0;
	const char *saved = usb ? "" : "  SIN USB";
	char text[256];

	for (int t = 0;; t++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		if (pressed & PAD_RIGHT && sel < ncv - 1) sel++;
		if (pressed & PAD_LEFT && sel > 0) sel--;
		if (pressed & PAD_SELECT) overlay ^= 1;
		if (pressed & PAD_CROSS && ncv) toast = 120;
		idle = b ? 0 : idle + 1;
		if (overlay && idle > 300 && ncv) sel = t / 90 % ncv; // overlay + 5 s idle: hands-free gate run
		s += (sel - s) * 0.2f;
		if (fabsf(sel - s) < 0.002f) s = sel; // snap: rest positions are whole pixels

		u32 t0 = cycles();
		gfx_begin();
		background();
		float k = 1 - 3 * fabsf(sel - s); // header fade: always the selected game, back in as the row settles
		if (ncv && k > 0) {
			gfx_alpha((int)(0x80 * k));
			gfx_text(&gfx_font_title, 64, 30, title(cv[sel].serial), WHITE);
			snprintf(text, sizeof(text), "%s   -   DVD", cv[sel].serial);
			gfx_text(&gfx_font_body, 64, 84, text, GREY);
			save_info(cv[sel].serial, text, sizeof(text));
			gfx_text(&gfx_font_body, GFX_W - 64 - gfx_text_width(&gfx_font_body, text), 46, text,
			         strncmp(text, "Sin", 3) ? 0x6078A0 : GREY);
		}
		gfx_alpha(0x30);
		gfx_rect(64, 128, GFX_W - 128, 1, 0xFFFFFF);
		for (int i = 0; i < ncv; i++)
			if (i != sel) draw_cover(i, s);
		if (ncv) draw_cover(sel, s); // last: its frame stays on top while it grows

		gfx_alpha(0x80);
		if (!ncv) text_c(&gfx_font_body, 320, "No hay portadas en mass0:/covers (genera con tools/covers.py)", WHITE);
		else {
			snprintf(text, sizeof(text), "%d / %d", sel + 1, ncv);
			gfx_text(&gfx_font_body, 64, 664, text, GREY);
		}
		hint(hint(GFX_W - 64, "SELECT", "Datos tecnicos"), "X", "Jugar"); // bottom right, laid out right to left
		if (toast > 0) {
			toast--;
			gfx_alpha(toast > 30 ? 0x80 : toast * 0x80 / 30);
			const char *m = "Lanzar juegos llega en la siguiente fase";
			gfx_text(&gfx_font_body, GFX_W - 64 - gfx_text_width(&gfx_font_body, m), 620, m, 0x6078A0);
		}
		if (overlay) {
			gfx_alpha(0x60);
			gfx_rect(48, 88, 760, 100, 0x000000);
			gfx_alpha(0x80);
			snprintf(text, sizeof(text), "Ventana %u (%d frames): mediana %u us   max %u us   vsync perdidos %u%s\n"
			         "Meta: max <= 8333 us y 0 perdidos.   Carga: %d portadas en %u ms\n"
			         "Sin tocar nada 5 s, la seleccion se mueve sola",
			         windows, WINDOW, med, max, win_missed, saved, ncv, load_ms);
			gfx_text(&gfx_font_body, 64, 98, text, WHITE);
		}
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
				fprintf(f, "ui window %u (%d frames, %d covers, overlay %d): median %u us  max %u us  missed vsync %u  "
				        "load %u ms\n", windows, WINDOW, ncv, overlay, med, max, missed, load_ms);
				saved = fclose(f) == 0 ? "   (guardado en USB)" : "   (ERROR al guardar)";
			}
			win_missed = missed, n = 0, missed = 0;
		}
	}
	return 0;
}
