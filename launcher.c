// ORBIT launcher (phase 4): animated splash + home screen of the approved design canvas
// (https://claude.ai/artifact/94TdvSa23Yh4veqXDpLv7E). Specs: openspec orbit-style, launcher-ui, cover-art.
// A loader thread brings up the IOP/USB, reads the memory cards and loads the covers
// (mass0:/covers/<serial>.c16 256x368 + <serial>_s.c16 184x264, tools/covers.py) while the render thread animates;
// gfx_flip sleeps on a vsync semaphore, so the loader runs in between. Frame time = COP0.Count (ps2tek:1117-1121)
// from gfx_begin to the GS FINISH of gfx_end; the first 3 windows of 600 frames go to mass0:/launcher.txt.
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
#include <audsrv.h>
#include "gfx.h"
#include "ui_data.h"
#include "iop.h"

// ---- palette (design canvas, Style board) ----
#define NIGHT 0x04060E
#define NAVY 0x0D1736
#define ICE 0x7FE7FF
#define IRIS 0xC08BFF
#define TEXT 0xEEF3FF
#define TEXT2 0xA9B6D3
#define LABEL 0x8FA0C4
#define INK 0x0A1026
#define CHROME_T 0xF4F8FF
#define CHROME_B 0xAAB7D2

#define LW 256 // selected cover = <serial>.c16
#define LH 368
#define SW 184 // carousel cover = <serial>_s.c16
#define SH 264
#define D (SW + 28)            // centre-to-centre distance of small covers
#define E ((LW - SW) / 2)      // extra room around the selected one
#define CY 376                 // carousel centre line (design: Inicio board)
#define MAXC 128
#define WINDOW 600             // frames per measurement (10 s at 60 Hz)
#define FRAME_US 16667
#define HOLD 150               // splash: frames of black before the timeline (2.5 s)

static inline u32 cycles(void) { u32 c; __asm__ volatile("mfc0 %0, $9" : "=r"(c)); return c; }
static u32 to_us(u32 c) { return (u32)((u64)c * 1000 / 294912); } // 294.912 MHz (ps2tek:345)
static int cmp(const void *a, const void *b) { return *(const u32 *)a > *(const u32 *)b ? 1 : *(const u32 *)a < *(const u32 *)b ? -1 : 0; }
static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static float ease(float v) { v = clampf(v); return 1 - (1 - v) * (1 - v) * (1 - v); } // ease-out cubic
static float span(int t, int a, int b) { return clampf((float)(t - a) / (b - a)); }  // 0..1 between frames a, b

static struct { char serial[16]; void *big, *small; } cv[MAXC];
static volatile int ncv, stage, done_n, total_n, usb, load_ms; // written by the loader thread

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

// ---- saves: root dirs of both cards, read once by the loader (design: phase-3-ui). mcn[p] < 0: no card ----
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
	}
}

static int save_info(const char *serial, char *line1, char *line2, int n) // returns the save count
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
	if (mcn[0] < 0 && mcn[1] < 0) snprintf(line1, n, "Sin memory card"), snprintf(line2, n, "INSERTA UNA EN MC1 / MC2");
	else if (!count) snprintf(line1, n, "Sin saves"), snprintf(line2, n, "NINGUNO EN MC1 · MC2");
	else {
		snprintf(line1, n, "%d save%s", count, count > 1 ? "s" : "");
		snprintf(line2, n, "MEMORY CARD %d · %02d/%02d/%04d", card + 1, bd->Day, bd->Month, bd->Year); // ponytail: JST
	}
	return count;
}

// ---- covers ----
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
	DIR *d = opendir("mass0:/covers");
	struct dirent *e;
	int total = 0;
	while (d && (e = readdir(d))) total += is_big(e->d_name);
	if (d) closedir(d);
	total_n = total;
	d = opendir("mass0:/covers");
	char path[300];
	int n = 0;
	while (d && (e = readdir(d)) && n < MAXC) {
		if (!is_big(e->d_name)) continue;
		int len = strlen(e->d_name) - 4;
		memcpy(cv[n].serial, e->d_name, len);
		cv[n].serial[len] = 0;
		snprintf(path, sizeof(path), "mass0:/covers/%s", e->d_name);
		cv[n].big = load_c16(path, LW, LH);
		snprintf(path, sizeof(path), "mass0:/covers/%s_s.c16", cv[n].serial);
		cv[n].small = cv[n].big ? load_c16(path, SW, SH) : NULL;
		if (cv[n].small) n++;
		else free(cv[n].big);
		done_n++;
	}
	if (d) closedir(d);
	for (int i = 1; i < n; i++) // readdir order is the FAT order: sort by title for a stable row
		for (int j = i; j > 0 && strcmp(title(cv[j - 1].serial), title(cv[j].serial)) > 0; j--) {
			__typeof__(cv[0]) t = cv[j]; cv[j] = cv[j - 1]; cv[j - 1] = t;
		}
	ncv = n;
}

// ---- sound: tools/sfx.py WAVs -> SPU2 ADPCM (adpenc, Makefile), played on free SPU2 voices by audsrv ----
#define SND(n) extern unsigned char sfx_##n[]; extern unsigned int size_sfx_##n
SND(splash); SND(move); SND(edge); SND(confirm); SND(panel);
enum { S_SPLASH, S_MOVE, S_EDGE, S_CONFIRM, S_PANEL, S_N };
static audsrv_adpcm_t snd[S_N];
static volatile int sound; // loader: 0 not ready yet, 1 ready, -1 unavailable

static int sound_init(void) // loader thread, right after the modules: the splash waits for it (in sync with audio)
{
	if (audsrv_init() != 0) return -1;
	audsrv_adpcm_init();
	audsrv_set_volume(MAX_VOLUME);
	struct { unsigned char *d; unsigned *n; } src[S_N] = {{sfx_splash, &size_sfx_splash}, {sfx_move, &size_sfx_move},
		{sfx_edge, &size_sfx_edge}, {sfx_confirm, &size_sfx_confirm}, {sfx_panel, &size_sfx_panel}};
	for (int i = 0; i < S_N; i++) { // the IOP DMAs the sample out of EE RAM: aligned, written-back copy
		void *b = memalign(64, *src[i].n);
		if (!b) return -1;
		memcpy(b, src[i].d, *src[i].n);
		SyncDCache(b, (u8 *)b + *src[i].n);
		int r = audsrv_load_adpcm(&snd[i], b, *src[i].n);
		free(b); // uploaded to SPU2 RAM (ps2sdk playadpcm.c frees it too)
		if (r < 0) return -1;
	}
	return 1;
}

static void play(int id, int vol) // render thread; vol 0-100
{
	if (sound != 1) return;
	int ch = audsrv_ch_play_adpcm(-1, &snd[id]);
	if (ch >= 0) audsrv_adpcm_set_volume_and_pan(ch, vol, 0);
}

static u8 loader_stack[0x20000] __attribute__((aligned(16)));
extern void *_gp;

static void loader(void *arg) // lower priority than the render thread: runs while gfx_flip sleeps
{
	(void)arg;
	int ok = iop_load();
	sound = ok ? sound_init() : -1;
	usb = ok && usb_wait();
	stage = 1;
	scan_cards();
	stage = 2;
	clock_t c0 = clock();
	if (usb) load_covers();
	load_ms = (int)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
	printf("%d covers loaded in %d ms\n", ncv, load_ms);
	stage = 3;
	ExitThread();
}

// ---- shared scenery ----
static void floor_grid(int horizon, int a) // perspective grid (design: rotateX floor), alpha a at the near edge
{
	for (int k = 0; k < 12; k++) {   // horizontals, closer together towards the horizon
		float y = horizon + (GFX_H + 40 - horizon) / (1 + 0.55f * k);
		int al = (int)(a * (y - horizon) / (GFX_H - horizon));
		if (y < GFX_H) gfx_line(0, y, GFX_W, y, ICE, al, al);
	}
	for (int i = -14; i <= 14; i++)  // verticals towards one vanishing point
		gfx_line(GFX_W / 2 + i * 150.f, GFX_H, GFX_W / 2 + i * 150.f * 0.28f, horizon, ICE, a, 0);
}

static void sparkle(int id, int x, int y, unsigned rgb) { gfx_alpha(0x80); gfx_icon(id, x, y, rgb); }

static void text_c(const gfx_font *f, int y, const char *s, unsigned rgb) // centred on the screen
{
	gfx_text(f, (GFX_W - gfx_text_width(f, s)) / 2, y, s, rgb);
}

// ---- splash (design: Loading + SplashStory boards), frame-based at 60 Hz ----
static float shown; // displayed progress, eased towards the real one

static void orbit(float ring, int front) // ellipse rx 210 ry 44 rotated -12 degrees, 3 px soft ribbon, drawn from
{                                         // the left; lower half (front) over the orb
	if (ring <= 0) return;
	float c = cosf(-12 * 3.14159f / 180), s = sinf(-12 * 3.14159f / 180), px[49], py[49];
	int segs = (int)(96 * ring), first = front ? 48 : 0, last = (front ? 96 : 48) < segs ? (front ? 96 : 48) : segs, n = 0;
	for (int i = first; i <= last; i++) {
		float a = i * 6.28318f / 96 + 3.14159f, x = 210 * cosf(a), y = 44 * sinf(a);
		px[n] = 640 + x * c - y * s, py[n] = 240 + x * s + y * c, n++;
	}
	gfx_ribbon(px, py, n, 3.5f, ICE, 0x80);
}

static void splash(int t, float fade)
{
	gfx_begin();
	gfx_alpha(0x80);
	gfx_rect(0, 0, GFX_W, GFX_H, 0x000000);
	gfx_dither(1); // soft gradients and glows dithered on the 16-bit framebuffer; text and the baked orb are not
	gfx_glow(0, -40, GFX_W, 720, 0x0B1A3E, 0x0B1A3E); // radial navy glow, centre 50% 44%
	floor_grid(500, 0x16);

	// light towers rise, then fade (design: 0.5-1.6 s)
	static const short towers[6][3] = {{300, 200, 10}, {420, 250, 14}, {846, 230, 12}, {968, 180, 9}, {540, 170, 8}, {730, 160, 8}};
	for (int i = 0; i < 6; i++) {
		float k = ease(span(t, 30 + i * 4, 80 + i * 4)), out = 1 - span(t, 90, 130);
		int h = (int)(towers[i][1] * k);
		if (h > 0 && out > 0) {
			gfx_alpha((int)(0x70 * out));
			gfx_grad(towers[i][0], 530 - h, towers[i][2], h, 0xFFFFFF, 0x0A1A40, 1);
		}
	}

	float sp = span(t, 0, 30), spo = 1 - span(t, 30, 66); // spark: grows, then dissolves into the ring
	if (spo > 0) {
		int s = (int)(80 * (0.2f + 1.6f * ease(sp)));
		gfx_alpha((int)(0x80 * ease(sp) * spo));
		gfx_glow(640 - s / 2, 240 - s / 2, s, s, 0xFFFFFF, 0xA0C8FF);
	}

	float ring = ease(span(t, 30, 96)); // orbit draws itself; back half behind the orb, front half over it
	orbit(ring, 0);
	float ob = ease(span(t, 96, 144)); // orb appears, a highlight sweeps across it
	if (ob > 0) {
		int s = (int)(110 * (0.6f + 0.4f * ob));
		gfx_alpha((int)(0x38 * ob));
		gfx_glow(640 - s * 2, 240 - s * 2, s * 4, s * 4, ICE, 0x7896FF);
		gfx_alpha((int)(0x80 * ob));
		gfx_dither(0);
		gfx_orb(640 - s / 2, 240 - s / 2, s);
		gfx_dither(1);
		float sh = span(t, 136, 196);
		if (sh > 0 && sh < 1) {
			gfx_alpha((int)(0x70 * sinf(sh * 3.14159f)));
			gfx_glow(640 - 55 + (int)(110 * sh) - 22, 240 - 40, 44, 44, 0xFFFFFF, 0xFFFFFF);
		}
	}

	orbit(ring, 1);
	gfx_dither(0);

	float wd = ease(span(t, 144, 192)); // "ORBIT": tracking closes 34 -> 10 px
	if (wd > 0) {
		gfx_tracking((int)(34 - 24 * wd));
		gfx_alpha((int)(0x80 * wd));
		gfx_text_chrome(&gfx_font_logo, (GFX_W - gfx_text_width(&gfx_font_logo, "ORBIT")) / 2, 330, "ORBIT");
		gfx_tracking(6);
		gfx_alpha((int)(0x80 * span(t, 180, 228)));
		text_c(&gfx_font_mono, 412, "PS2 LAUNCHER", LABEL);
		gfx_tracking(0);
	}

	float ba = span(t, 192, 228); // loading bar, real progress
	if (ba > 0) {
		float goal = stage < 2 ? 0.05f * stage : stage == 3 ? 1 : 0.1f + 0.9f * (total_n ? (float)done_n / total_n : 0);
		shown += (goal - shown) * 0.08f;
		int w = (int)(300 * shown);
		gfx_alpha((int)(0x2E * ba));
		gfx_rect(490, 520, 300, 2, TEXT2);
		gfx_alpha((int)(0x80 * ba));
		gfx_grad(490, 520, w, 2, 0x4C7BD9, 0xDDEBFF, 0);
		gfx_alpha((int)(0x60 * ba));
		gfx_glow(490 + w - 14, 507, 28, 28, 0xBFD8FF, 0xBFD8FF);
		char st[64];
		if (stage == 0) snprintf(st, sizeof(st), "INICIANDO USB");
		else if (stage == 1) snprintf(st, sizeof(st), "LEYENDO MEMORY CARDS");
		else snprintf(st, sizeof(st), "CARGANDO PORTADAS  %02d / %02d", done_n, total_n);
		gfx_tracking(3);
		gfx_alpha((int)(0x80 * ba));
		text_c(&gfx_font_mono, 536, st, 0x6F7E9E);
		gfx_tracking(0);
	}
	if (fade > 0) { gfx_alpha((int)(0x80 * fade)); gfx_rect(0, 0, GFX_W, GFX_H, 0); }
	gfx_end();
	gfx_flip();
}

// ---- home screen (design: Inicio board) ----
static int hint(int x, int key_icon, const char *key_text, int action_icon, const char *label) // right-aligned at x
{
	int lw = gfx_text_width(&gfx_font_ui, label);
	int kw = key_icon >= 0 ? 30 : gfx_text_width(&gfx_font_mono, key_text) + 20;
	int w = 8 + kw + 10 + 18 + 10 + lw + 18, x0 = x - w, y = 646;
	gfx_alpha(0x2D);
	gfx_rrect(x0, y, w, 44, 22, TEXT2, TEXT2);
	gfx_alpha(0x80);
	gfx_rrect(x0 + 1, y + 1, w - 2, 42, 21, 0x0B1430, 0x0A1128);
	int kx = x0 + 8;
	if (key_icon >= 0) {
		gfx_rrect(kx, y + 7, 30, 30, 15, CHROME_T, CHROME_B);
		gfx_icon(key_icon, kx + 8, y + 15, IRIS);
	} else {
		gfx_rrect(kx, y + 9, kw, 26, 13, CHROME_T, CHROME_B);
		gfx_text(&gfx_font_mono, kx + 10, y + 12, key_text, INK);
	}
	gfx_icon(action_icon, kx + kw + 10, y + 13, ICE);
	gfx_text(&gfx_font_ui, kx + kw + 38, y + 11, label, TEXT);
	return x0 - 12;
}

static void cover(int i, float s, int sel) // grow factor f = 1 at the centre, 0 one step away (design: phase-3-ui)
{
	float di = i - s, a = fabsf(di), f = a < 1 ? 1 - a : 0;
	int w = (int)lroundf(SW + (LW - SW) * f), h = (int)lroundf(SH + (LH - SH) * f);
	int cx = (int)lroundf(GFX_W / 2 + di * D + (di < 0 ? -E : E) * (a < 1 ? a : 1));
	int x = cx - w / 2, y = CY - h / 2;
	if (x + w + 60 < 0 || x - 60 > GFX_W) return;
	if (f > 0) { // chrome frame + ice/iris glow, faded with the grow factor
		gfx_alpha((int)(0x40 * f));
		gfx_dither(1);
		gfx_glow(x - 90, y - 90, w + 180, h + 180, 0x9070FF, ICE);
		gfx_dither(0);
		gfx_alpha((int)(0x4C * f));
		gfx_rrect(x - 5, y - 5, w + 10, h + 10, 7, ICE, ICE);
		gfx_alpha((int)(0x80 * f));
		gfx_rrect(x - 4, y - 4, w + 8, h + 8, 6, 0xFFFFFF, 0x8D9BBB);
	}
	gfx_alpha(i == sel ? 0x80 : (int)(0x69 + 0x17 * f)); // others at 82 % (design)
	if (w == LW && h == LH) gfx_image(cv[i].big, LW, LH, x, y, w, h);           // at rest: pixel-exact
	else if (w == SW && h == SH) gfx_image(cv[i].small, SW, SH, x, y, w, h);
	else if (f > 0.5f) gfx_image(cv[i].big, LW, LH, x, y, w, h);              // animating: bilinear
	else gfx_image(cv[i].small, SW, SH, x, y, w, h);
}

static void fit(const gfx_font *f, const char *s, int max_w, char *out, int n) // "..." when too wide
{
	snprintf(out, n, "%s", s);
	for (int len = strlen(out); gfx_text_width(f, out) > max_w && len > 4; len--)
		strcpy(out + len - 4, "...");
}

static void home(int sel, float s, float k, int toast, const char *overlay, float fade)
{
	char a[96], b[96];
	gfx_begin();
	gfx_alpha(0x80);
	gfx_dither(1);
	gfx_grad(0, 0, GFX_W, 346, NAVY, 0x080D22, 1);
	gfx_grad(0, 346, GFX_W, GFX_H - 346, 0x080D22, NIGHT, 1);
	floor_grid(418, 0x1C);
	gfx_line(0, 418, 640, 418, ICE, 0, 0x46);
	gfx_line(640, 418, GFX_W, 418, IRIS, 0x46, 0);
	gfx_alpha(0x24);
	gfx_glow(340, 470, 600, 120, ICE, ICE);
	sparkle(UI_SPARKLE_24, 1116, 146, 0xDDEBFF);
	sparkle(UI_SPARKLE_12, 150, 196, 0xB7C3FF);
	sparkle(UI_SPARKLE_12, 1194, 378, ICE);
	gfx_dither(0);

	for (int i = 0; i < ncv; i++)
		if (i != sel) cover(i, s, sel);
	if (ncv) cover(sel, s, sel); // last: its frame stays on top while it grows

	// header: orb, chrome title, chips; saves card on the right — always the selected game, fading in
	gfx_alpha(0x80);
	gfx_alpha(0x30);
	gfx_dither(1);
	gfx_glow(44, 25, 96, 96, ICE, ICE);
	gfx_dither(0);
	gfx_alpha(0x80);
	gfx_orb(64, 45, 56);
	if (ncv && k > 0) {
		int sc = save_info(cv[sel].serial, a, b, sizeof(a));
		int cw = 18 + 36 + 14 + (gfx_text_width(&gfx_font_ui, a) > gfx_text_width(&gfx_font_mono, b) ?
		                         gfx_text_width(&gfx_font_ui, a) : gfx_text_width(&gfx_font_mono, b)) + 20;
		int cx = GFX_W - 64 - cw;
		gfx_alpha((int)(0x26 * k));
		gfx_rrect(cx - 1, 35, cw + 2, 70, 19, ICE, ICE);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(cx, 36, cw, 68, 18, 0x1B2850, 0x0D1530);
		gfx_icon(UI_MEMCARD_36, cx + 18, 52, sc ? ICE : 0x5A6787); // phase 5: the game's 3D save icon
		gfx_text(&gfx_font_ui, cx + 68, 48, a, TEXT);
		gfx_text(&gfx_font_mono, cx + 68, 72, b, TEXT2);

		fit(&gfx_font_title, title(cv[sel].serial), cx - 40 - 140, a, sizeof(a));
		gfx_text_chrome(&gfx_font_title, 140, 34, a);
		int x = 140, w = gfx_text_width(&gfx_font_mono, cv[sel].serial) + 24; // serial chip
		gfx_alpha((int)(0x60 * k));
		gfx_rrect(x, 86, w, 26, 13, TEXT2, TEXT2);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x0E1838, 0x0D1634);
		gfx_text(&gfx_font_mono, x + 12, 90, cv[sel].serial, TEXT2);
		x += w + 10;
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, "DVD") + 12;               // DVD chip (chrome)
		gfx_rrect(x, 86, w, 26, 13, CHROME_T, 0xBAC6DE);
		gfx_icon(UI_DVD_18, x + 8, 90, INK);
		gfx_text(&gfx_font_ui, x + 32, 89, "DVD", INK);
		x += w + 10;
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, "USB") + 12;               // source chip (iris)
		gfx_alpha((int)(0x8C * k / 2));
		gfx_rrect(x, 86, w, 26, 13, IRIS, IRIS);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x241C52, 0x1E1746);
		gfx_icon(UI_USB_18, x + 8, 90, IRIS);
		gfx_text(&gfx_font_ui, x + 32, 89, "USB", TEXT);
	}
	gfx_line(64, 138, 1216, 138, TEXT2, 0x40, 0x13);

	gfx_alpha(0x80);
	if (!ncv) text_c(&gfx_font_ui, 320, "No hay portadas en mass0:/covers (genera con tools/covers.py)", TEXT);
	else { // footer: position + ticks on the left, hints on the right
		snprintf(a, sizeof(a), "%02d / %02d", sel + 1, ncv);
		gfx_text(&gfx_font_mono, 64, 658, a, TEXT);
		int tx = 64 + gfx_text_width(&gfx_font_mono, a) + 14;
		for (int i = 0; i < ncv && i < 40; i++) {
			gfx_alpha(i == sel ? 0x80 : 0x2D);
			gfx_rrect(tx, 665, i == sel ? 18 : 6, 6, 3, i == sel ? ICE : TEXT2, i == sel ? ICE : TEXT2);
			tx += (i == sel ? 18 : 6) + 3;
		}
	}
	hint(hint(GFX_W - 64, -1, "SELECT", UI_CHIP_18, "Datos técnicos"), UI_CROSS_14, NULL, UI_PLAY_18, "Jugar");
	if (toast > 0) {
		const char *m = "Lanzar juegos llega en la siguiente fase";
		gfx_alpha(toast > 30 ? 0x80 : toast * 0x80 / 30);
		gfx_text(&gfx_font_ui, GFX_W - 64 - gfx_text_width(&gfx_font_ui, m), 612, m, ICE);
	}
	if (overlay) {
		gfx_alpha(0x60);
		gfx_rrect(48, 150, 820, 92, 14, NIGHT, NIGHT);
		gfx_alpha(0x80);
		gfx_text(&gfx_font_mono, 64, 162, overlay, TEXT);
	}
	if (fade > 0) { gfx_alpha((int)(0x80 * fade)); gfx_rect(0, 0, GFX_W, GFX_H, 0); }
	gfx_end();
}

int main(void)
{
	if (!gfx_init()) printf("gfx_init: VRAM pool too small\n");
	ChangeThreadPriority(GetThreadId(), 0x20); // render thread above the loader
	ee_thread_t th = { .func = loader, .stack = loader_stack, .stack_size = sizeof(loader_stack), .gp_reg = &_gp,
	                   .initial_priority = 0x40 };
	int tid = CreateThread(&th);
	StartThread(tid, NULL);

	// HOLD frames of black first: after the mode change the HDMI adapter and the TV take seconds to show a picture,
	// and on the console the user saw only the final logo (the timeline had already run). ponytail: fixed guess,
	// make it a setting if other TVs need more or less.
	clock_t c0 = clock();
	u64 frame_us = 0;
	int t = 0, end = -1, start = -1; // splash until loaded and past the timeline's last key, then 20 frames to black
	for (;; t++) {
		if (start < 0 && t >= HOLD && (sound != 0 || t >= HOLD + 360)) { // audio ready (or 6 s more): go together
			start = t;
			play(S_SPLASH, 100);
		}
		int tt = start < 0 ? -1 : t - start;
		if (end < 0 && stage == 3 && tt >= 240 && shown > 0.98f) end = tt;
		if (end >= 0 && tt - end > 20) break;
		u32 f0 = cycles();
		if (tt >= 0) splash(tt, end < 0 ? 0 : span(tt, end, end + 20));
		else { gfx_begin(); gfx_alpha(0x80); gfx_rect(0, 0, GFX_W, GFX_H, 0); gfx_end(); gfx_flip(); }
		frame_us += to_us(cycles() - f0); // per frame, so the 14.6 s COP0 wrap does not matter
	}
	int splash_ms = (int)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
	printf("splash: %d frames, %u us/frame by COP0\n", t, t ? (unsigned)(frame_us / t) : 0);
	FILE *fp = usb ? fopen("mass0:/launcher.txt", "a") : NULL; // frames vs wall time: did the animation run at 60 Hz?
	if (fp) {
		fprintf(fp, "orbit splash: %d frames (%d black) in %d ms by clock(), %u ms by COP0 = %u us/frame (16667 expected)\n",
		        t, HOLD, splash_ms, (unsigned)(frame_us / 1000), t ? (unsigned)(frame_us / t) : 0);
		fclose(fp);
	}

	static u32 build[WINDOW];
	u32 med = 0, max = 0, missed = 0, win_missed = 0, windows = 0, last_vsync = 0;
	int sel = 0, n = 0, idle = 0, overlay = 0, toast = 0;
	float s = 0;
	unsigned prev = 0;
	const char *saved = usb ? "" : "  SIN USB";
	char text[256];

	for (int f = 0;; f++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		if (pressed & PAD_RIGHT) { if (sel < ncv - 1) sel++, play(S_MOVE, 70); else play(S_EDGE, 80); }
		if (pressed & PAD_LEFT) { if (sel > 0) sel--, play(S_MOVE, 70); else play(S_EDGE, 80); }
		if (pressed & PAD_SELECT) overlay ^= 1, play(S_PANEL, 70);
		if (pressed & PAD_CROSS && ncv) toast = 120, play(S_CONFIRM, 85);
		if (toast > 0) toast--;
		idle = b ? 0 : idle + 1;
		if (overlay && idle > 300 && ncv) sel = f / 90 % ncv; // overlay + 5 s idle: hands-free gate run
		s += (sel - s) * 0.2f;
		if (fabsf(sel - s) < 0.002f) s = sel; // snap: rest positions are whole pixels
		float k = 1 - 3 * fabsf(sel - s);

		snprintf(text, sizeof(text), "VENTANA %u (%d FRAMES): MEDIANA %u us  MAX %u us  VSYNC PERDIDOS %u%s\n"
		         "META: MAX <= 8333 us Y 0 PERDIDOS.  CARGA: %d PORTADAS EN %d ms\n"
		         "SIN TOCAR NADA 5 s, LA SELECCIÓN SE MUEVE SOLA",
		         windows, WINDOW, med, max, win_missed, saved, ncv, load_ms);
		u32 t0 = cycles();
		home(sel, s, k, toast, overlay ? text : NULL, 1 - span(f, 0, 20));
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
			FILE *fp = usb && windows <= 3 ? fopen("mass0:/launcher.txt", "a") : NULL;
			if (fp) {
				fprintf(fp, "orbit window %u (%d frames, %d covers, overlay %d): median %u us  max %u us  missed vsync %u  "
				        "load %d ms\n", windows, WINDOW, ncv, overlay, med, max, missed, load_ms);
				saved = fclose(fp) == 0 ? "  (GUARDADO EN USB)" : "  (ERROR AL GUARDAR)";
			}
			win_missed = missed, n = 0, missed = 0;
		}
	}
	return 0;
}
