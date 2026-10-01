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
#include <sifrpc.h>
#include <tamtypes.h>
#include <libpad.h>
#include <libmc.h>
#include <audsrv.h>
#include "gfx.h"
#include "ui_data.h"
#include "iop.h"
#include "iso.h"
#include "ini.h"
#include "net.h"
#include "cover.h"
#include <sys/stat.h>
#define NEWLIB_PORT_AWARE // fileXio for the 64-bit ISO seek only; the rest goes through stdio
#include <fileXio_rpc.h>
#include <io_common.h>

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

// one entry per game ISO on the USB: mass0:/DVD/*.iso, mass0:/CD/*.iso (OPL layout). serial = "SLUS-20946" (dash form,
// as cover files and save dirs use it); big/small = covers/<serial>.c16 / _s.c16, NULL = drawn generic cover
static struct { char title[64], serial[16], path[160]; char cd; void *big, *small; } cv[MAXC];
static volatile int ncv, stage, done_n, total_n, usb, load_ms, neutrino; // written by the loader thread
#define NEUTRINO "mass0:/neutrino/neutrino.elf"

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

static int fx_read(void *ctx, unsigned lba, void *buf, unsigned n) // 64-bit seek: ISOs are up to 8 GB
{
	int fd = *(int *)ctx;
	return fileXioLseek64(fd, (s64)lba * 2048, FIO_SEEK_SET) >= 0 && fileXioRead(fd, buf, n) == (int)n;
}

static int is_iso(const char *name)
{
	int n = strlen(name);
	return n > 4 && !strcasecmp(name + n - 4, ".iso");
}

static void load_games(void) // DVD/ and CD/: serial from SYSTEM.CNF, title from the file name, optional covers
{
	static const char *dirs[2] = {"DVD", "CD"};
	struct dirent *e;
	char path[300], raw[16];
	int total = 0, n = 0;
	for (int k = 0; k < 2; k++) {
		snprintf(path, sizeof(path), "mass0:/%s", dirs[k]);
		DIR *d = opendir(path);
		while (d && (e = readdir(d))) total += is_iso(e->d_name);
		if (d) closedir(d);
	}
	total_n = total;
	for (int k = 0; k < 2; k++) {
		snprintf(path, sizeof(path), "mass0:/%s", dirs[k]);
		DIR *d = opendir(path);
		while (d && (e = readdir(d)) && n < MAXC) {
			if (!is_iso(e->d_name)) continue;
			snprintf(cv[n].path, sizeof(cv[n].path), "%s/%s", dirs[k], e->d_name);
			snprintf(path, sizeof(path), "mass0:/%s", cv[n].path);
			int fd = fileXioOpen(path, FIO_O_RDONLY);
			int ok = fd >= 0 && iso_serial(fx_read, &fd, raw); // SYSTEM.CNF first; the OPL file-name prefix after
			if (fd >= 0) fileXioClose(fd);
			if (!ok) ok = name_serial(e->d_name, raw);
			done_n++;
			if (!ok) { printf("%s: no PS2 SYSTEM.CNF nor OPL serial in the name, skipped\n", path); continue; }
			serial_dash(raw, cv[n].serial);
			iso_title(e->d_name, cv[n].title, sizeof(cv[n].title));
			cv[n].cd = k == 1;
			snprintf(path, sizeof(path), "mass0:/covers/%s.c16", cv[n].serial);
			cv[n].big = load_c16(path, LW, LH);
			snprintf(path, sizeof(path), "mass0:/covers/%s_s.c16", cv[n].serial);
			cv[n].small = cv[n].big ? load_c16(path, SW, SH) : NULL;
			if (!cv[n].small) free(cv[n].big), cv[n].big = NULL; // both or none: generic cover otherwise
			n++;
		}
		if (d) closedir(d);
	}
	for (int i = 1; i < n; i++) // readdir order is the FAT order: sort by title for a stable row
		for (int j = i; j > 0 && strcasecmp(cv[j - 1].title, cv[j].title) > 0; j--) {
			__typeof__(cv[0]) t = cv[j]; cv[j] = cv[j - 1]; cv[j - 1] = t;
		}
	ncv = n;
}

// ---- config (phase 7): mass0:/orbit/config.ini, created from this template when missing; per-game options in
// juegos.ini, one section per dash serial with only the keys that differ from the defaults ----
#define CONFIG "mass0:/orbit/config.ini"
#define GAMES_INI "mass0:/orbit/juegos.ini"
static const char config_template[] =
	"; ORBIT - configuración, se lee al arrancar\n"
	"\n[juegos]\n; origen de los juegos: usb (hdd, mx4sio, mmce, udpbd, udpfs, ilink: aún no disponibles)\norigen = usb\n"
	"\n[video]\n; modo de video de los juegos: nativo, 480p o 1080i (cada juego lo cambia con triángulo)\nmodo = 480p\n"
	"\n[sonido]\n; volumen de los sonidos del menú, 0-100\nvolumen = 100\n"
	"\n[red]\n; ip = dhcp, o una IP fija con su mascara, puerta (de enlace) y dns\nip = dhcp\nmascara = 255.255.255.0\n"
	"puerta =\ndns =\n"
	"\n[portadas]\n; si = descargar de internet (github xlenore/ps2-covers) las que falten, con el cable de red conectado\n"
	"descargar = si\n";
static ini cfg, games;
static volatile int cfg_volume = 100, source_ok = 1;

static void load_config(void) // loader thread, before the splash sound
{
	mkdir("mass0:/orbit", 0777);
	if (!ini_load(&cfg, CONFIG)) {
		FILE *f = fopen(CONFIG, "wb");
		if (f) fputs(config_template, f), fclose(f);
		ini_parse(&cfg, config_template);
	}
	int v = atoi(ini_get(&cfg, "sonido", "volumen", "100"));
	cfg_volume = v < 0 ? 0 : v > 100 ? 100 : v;
	source_ok = !strcasecmp(ini_get(&cfg, "juegos", "origen", "usb"), "usb"); // ponytail: one source until more drivers
	ini_load(&games, GAMES_INI);
}

// video: 0 = default (config), 1 nativo, 2 480p (-gsm=fp2), 3 1080i (-gsm=1080ix2); Neutrino README for -gsm / -gc
static const char *vid_key[4] = {NULL, "nativo", "480p", "1080i"}, *vid_label[4] = {"Predeterminado", "Nativo", "480p", "1080i"};
static const char gc_modes[] = "02357";
static const char *gc_names[5] = {"Lectura rápida (0)", "Lectura síncrona (2)", "Sin hooks de syscalls (3)",
                                  "Emular DVD-DL (5)", "Corregir buffer overrun (7)"};
enum { OPT_VIDEO, OPT_COMPAT, OPT_GC, OPT_ROWS = OPT_GC + 5 };

static int vid_index(const char *s)
{
	for (int v = 1; v < 4; v++)
		if (!strcasecmp(s, vid_key[v])) return v;
	return 0;
}
static int game_vid(int i) { return vid_index(ini_get(&games, cv[i].serial, "video", "")); }
static int game_video(int i) // effective mode, 1..3
{
	int v = game_vid(i);
	if (!v) v = vid_index(ini_get(&cfg, "video", "modo", "480p"));
	return v ? v : 2;
}
static int game_compat(int i) { return atoi(ini_get(&games, cv[i].serial, "compat", "0")) & 3; }
static int game_gc(int i, int k) { return strchr(ini_get(&games, cv[i].serial, "gc", ""), gc_modes[k]) != NULL; }

static void opt_change(int i, int row, int dir)
{
	const char *s = cv[i].serial;
	char v[8];
	if (row == OPT_VIDEO) ini_set(&games, s, "video", vid_key[(game_vid(i) + dir + 4) % 4]); // NULL = default
	else if (row == OPT_COMPAT) {
		int c = (game_compat(i) + dir + 4) % 4;
		snprintf(v, sizeof(v), "%d", c);
		ini_set(&games, s, "compat", c ? v : NULL);
	} else {
		int n = 0;
		for (int k = 0; k < 5; k++)
			if (game_gc(i, k) != (k == row - OPT_GC)) v[n++] = gc_modes[k];
		v[n] = 0;
		ini_set(&games, s, "gc", n ? v : NULL);
	}
}

static void opt_value(int i, int row, char *out, int n)
{
	if (row == OPT_VIDEO) {
		int v = game_vid(i);
		if (v) snprintf(out, n, "%s", vid_label[v]);
		else snprintf(out, n, "Predet. (%s)", vid_label[game_video(i)]);
	} else if (row == OPT_COMPAT) {
		if (game_compat(i)) snprintf(out, n, "%d", game_compat(i));
		else snprintf(out, n, "No");
	} else snprintf(out, n, "%s", game_gc(i, row - OPT_GC) ? "Sí" : "No");
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
	if (ch >= 0) audsrv_adpcm_set_volume_and_pan(ch, vol * cfg_volume / 100, 0);
}

// ---- cover download (phase 8, option A): missing covers straight from xlenore/ps2-covers over HTTPS, decoded,
// resized and dithered on the EE (cover.c), saved as .c16 pairs; after the splash, in the loader thread ----
#define COVER_HOST "raw.githubusercontent.com"
#define COVER_PATH "/xlenore/ps2-covers/main/covers/default/%s.jpg" // as tools/fetch_covers.py
static volatile int dl_state, dl_done, dl_total, dl_got; // state: 0 idle, 1 connecting, 2 downloading, 3 done, NET_ERR_*

static int save_c16(const char *serial, const char *suffix, const void *px, unsigned w, unsigned h)
{
	char path[64];
	unsigned hdr[4] = {0, w, h, 0};
	memcpy(hdr, "C16", 4);
	snprintf(path, sizeof(path), "mass0:/covers/%s%s.c16", serial, suffix);
	FILE *f = fopen(path, "wb");
	int ok = f && fwrite(hdr, 16, 1, f) == 1 && fwrite(px, w * h * 2, 1, f) == 1;
	if (f) ok &= fclose(f) == 0;
	if (!ok) remove(path); // load_c16 would skip a short file anyway; keep the USB clean
	return ok;
}

static void download_covers(void)
{
	if (strcasecmp(ini_get(&cfg, "portadas", "descargar", "si"), "si")) return;
	for (int i = 0; i < ncv; i++) dl_total += !cv[i].big;
	if (!dl_total) return;
	dl_state = 1;
	int r = net_up(ini_get(&cfg, "red", "ip", "dhcp"), ini_get(&cfg, "red", "mascara", "255.255.255.0"),
	               ini_get(&cfg, "red", "puerta", ""), ini_get(&cfg, "red", "dns", ""));
	if (r < 0) { dl_state = r; return; }
	dl_state = 2;
	int max = 1 << 20; // a JPG + headers; xlenore covers are about 135 KB (SLUS-21376)
	char *buf = malloc(max);
	mkdir("mass0:/covers", 0777);
	clock_t c0 = clock();
	for (int i = 0; buf && i < ncv && dl_state == 2; i++) {
		if (cv[i].big) continue;
		char path[96];
		int body, len;
		snprintf(path, sizeof(path), COVER_PATH, cv[i].serial);
		int st = https_get(COVER_HOST, path, buf, max, &body, &len);
		if (st < 0) { dl_state = st; break; } // network or TLS: the rest would fail the same way
		unsigned short *big = memalign(64, LW * LH * 2), *small = memalign(64, SW * SH * 2);
		if (st == 200 && big && small && cover_from_jpeg((unsigned char *)buf + body, len, big, small)) {
			save_c16(cv[i].serial, "", big, LW, LH); // shown even if the USB write fails; fetched again next boot
			save_c16(cv[i].serial, "_s", small, SW, SH);
			SyncDCache(big, big + LW * LH);
			SyncDCache(small, small + SW * SH);
			cv[i].small = small; // render thread: big == NULL means generic cover, so small goes first
			__asm__ volatile("" ::: "memory");
			cv[i].big = big;
			dl_got++;
		} else free(big), free(small); // 404: xlenore has no cover for it, the generic one stays
		dl_done++;
	}
	free(buf);
	int ms = (int)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
	printf("covers: %d of %d downloaded in %d ms, state %d\n", dl_got, dl_total, ms, dl_state);
	FILE *fp = fopen("mass0:/launcher.txt", "a"); // gate: time per pair
	if (fp) fprintf(fp, "orbit covers: %d of %d downloaded in %d ms (%d ms per pair), state %d\n", dl_got, dl_total, ms,
	                dl_got ? ms / dl_got : 0, dl_state), fclose(fp);
	if (dl_state == 2) dl_state = 3;
}

static u8 loader_stack[0x20000] __attribute__((aligned(16)));
extern void *_gp;

static void loader(void *arg) // lower priority than the render thread: runs while gfx_flip sleeps
{
	(void)arg;
	int ok = iop_load();
	sound = ok ? sound_init() : -1;
	usb = ok && usb_wait();
	if (usb) load_config();
	stage = 1; // the splash sound waits for this: the volume is known
	scan_cards();
	stage = 2;
	clock_t c0 = clock();
	if (usb) {
		FILE *f = fopen(NEUTRINO, "rb");
		neutrino = f != NULL;
		if (f) fclose(f);
		load_games();
	}
	load_ms = (int)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
	printf("%d games loaded in %d ms, neutrino %d\n", ncv, load_ms, neutrino);
	stage = 3;
	if (usb) download_covers(); // the home screen is already up: covers pop in as they arrive
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

// ---- backgrounds, Floyd-Steinberg dithered on the EE while the screen is still black (docs/image-quality.md) ----
static unsigned short *splash_bg, *home_bg; // 1280x720 in 256x128 tiles; 64x720 strip

static unsigned chan(unsigned c, int s) { return c >> s & 255; }
static void home_col(int x, int y, float *rgb, void *u) // canvas: navy 0% -> #080D22 48% -> night 100%
{
	(void)x, (void)u;
	unsigned a = y < 346 ? NAVY : 0x080D22, b = y < 346 ? 0x080D22 : NIGHT;
	float t = y < 346 ? y / 346.f : (y - 346) / (float)(GFX_H - 346);
	for (int c = 0; c < 3; c++) rgb[c] = chan(a, 16 - 8 * c) + (chan(b, 16 - 8 * c) - (float)chan(a, 16 - 8 * c)) * t;
}
static void splash_col(int x, int y, float *rgb, void *u) // radial #0B1A3E glow centred at 50% 44% over black
{
	(void)u;
	float dx = (x - 640) / 640.f, dy = (y - 320) / 360.f, g = 1 - sqrtf(dx * dx + dy * dy);
	g = g > 0 ? g * g : 0;
	rgb[0] = 0x0B * g, rgb[1] = 0x1A * g, rgb[2] = 0x3E * g;
}

static void make_backgrounds(void)
{
	u32 c0 = cycles();
	home_bg = memalign(64, 64 * GFX_H * 2);
	splash_bg = memalign(64, GFX_W * GFX_H * 2);
	unsigned short *lin = malloc(GFX_W * GFX_H * 2);
	if (home_bg) gfx_fs_dither(home_bg, 64, GFX_H, home_col, NULL), SyncDCache(home_bg, home_bg + 64 * GFX_H);
	if (splash_bg && lin) {
		gfx_fs_dither(lin, GFX_W, GFX_H, splash_col, NULL);
		gfx_tiles_from(splash_bg, lin, GFX_W, GFX_H);
		SyncDCache(splash_bg, splash_bg + GFX_W * GFX_H);
	}
	free(lin);
	printf("backgrounds dithered in %u ms\n", to_us(cycles() - c0) / 1000);
}

static void splash(int t, float fade)
{
	gfx_begin();
	gfx_alpha(0x80);
	gfx_image_tiled(splash_bg, GFX_W, GFX_H, 0, 0); // radial navy glow, Floyd-Steinberg dithered at boot
	gfx_dither(1); // remaining soft glows: GS dither; text and the baked orb are not
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

static void generic_cover(int i, int x, int y, int w, int h) // no cover file: ORBIT-style card with the title
{
	gfx_rrect(x, y, w, h, 6, 0x23306A, 0x0A1026);
	gfx_icon(cv[i].cd ? UI_CD_18 : UI_DVD_18, x + 12, y + 12, LABEL);
	gfx_text(&gfx_font_mono, x + 36, y + 13, cv[i].serial, LABEL);
	char line[64], word[64];
	const char *t = cv[i].title;
	int lines = 0, ly = y + h - 16 - 4 * 24;
	while (*t && lines < 4) { // greedy word wrap into the card width
		line[0] = 0;
		while (*t) {
			int n = strcspn(t, " ");
			snprintf(word, sizeof(word), "%s%s%.*s", line, *line ? " " : "", n, t);
			if (*line && gfx_text_width(&gfx_font_ui, word) > w - 24) break;
			strcpy(line, word);
			t += n;
			while (*t == ' ') t++;
		}
		gfx_text(&gfx_font_ui, x + 12, ly + lines++ * 24, line, TEXT);
	}
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
	if (!cv[i].big) { generic_cover(i, x, y, w, h); return; }
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

static void options_panel(int i, int row) // △ menu (design: ORBIT panel): dimmed home, rrect panel, value pills
{
	char a[96];
	int x = 300, y = 150, w = 680, h = 70 + 2 * 46 + 34 + 5 * 46 + 16;
	gfx_alpha(0x50);
	gfx_rect(0, 0, GFX_W, GFX_H, NIGHT);
	gfx_alpha(0x30);
	gfx_rrect(x - 1, y - 1, w + 2, h + 2, 19, ICE, IRIS);
	gfx_alpha(0x80);
	gfx_rrect(x, y, w, h, 18, 0x16224A, 0x0A1128);
	gfx_icon(UI_GEAR_18, x + 24, y + 24, ICE);
	fit(&gfx_font_ui, cv[i].title, w - 220, a, sizeof(a));
	gfx_text(&gfx_font_ui, x + 52, y + 21, a, TEXT);
	gfx_text(&gfx_font_mono, x + w - 24 - gfx_text_width(&gfx_font_mono, cv[i].serial), y + 24, cv[i].serial, TEXT2);
	gfx_line(x + 24, y + 58, x + w - 24, y + 58, TEXT2, 0x30, 0x10);
	int ry = y + 70;
	for (int r = 0; r < OPT_ROWS; r++) {
		if (r == OPT_GC) { // section label
			gfx_tracking(3);
			gfx_text(&gfx_font_mono, x + 28, ry + 8, "MODOS DE COMPATIBILIDAD (NEUTRINO -GC)", LABEL);
			gfx_tracking(0);
			ry += 34;
		}
		if (r == row) {
			gfx_alpha(0x28);
			gfx_rrect(x + 12, ry, w - 24, 40, 20, ICE, ICE);
			gfx_alpha(0x80);
		}
		const char *label = r == OPT_VIDEO ? "Video" : r == OPT_COMPAT ? "Compatibilidad de video" : gc_names[r - OPT_GC];
		gfx_text(&gfx_font_ui, x + 28, ry + 8, label, r == row ? TEXT : TEXT2);
		opt_value(i, r, a, sizeof(a));
		int on = r < OPT_GC || game_gc(i, r - OPT_GC), pw = gfx_text_width(&gfx_font_ui, a) + 64, px = x + w - 24 - pw;
		if (on) gfx_rrect(px, ry + 5, pw, 30, 15, r == row ? CHROME_T : 0x2A3A6E, r == row ? CHROME_B : 0x1B2850);
		else gfx_rrect(px, ry + 5, pw, 30, 15, 0x1B2340, 0x141B33);
		unsigned ink = on && r == row ? INK : on ? TEXT : LABEL;
		if (r < OPT_GC) {
			gfx_text(&gfx_font_ui, px + 12, ry + 8, "<", ink);
			gfx_text(&gfx_font_ui, px + pw - 12 - gfx_text_width(&gfx_font_ui, ">"), ry + 8, ">", ink);
		}
		gfx_text(&gfx_font_ui, px + 32, ry + 8, a, ink);
		ry += 46;
	}
}

static const char *toast_msg;

static void home(int sel, float s, float k, int toast, int opt, const char *overlay, float fade) // opt: panel row, -1 closed
{
	char a[96], b[96];
	gfx_begin();
	gfx_alpha(0x80);
	gfx_hstrip(home_bg, 64, GFX_H, 0); // navy -> night, Floyd-Steinberg dithered at boot, repeated across
	gfx_dither(1);
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

		fit(&gfx_font_title, cv[sel].title, cx - 40 - 140, a, sizeof(a));
		gfx_text_chrome(&gfx_font_title, 140, 34, a);
		int x = 140, w = gfx_text_width(&gfx_font_mono, cv[sel].serial) + 24; // serial chip
		gfx_alpha((int)(0x60 * k));
		gfx_rrect(x, 86, w, 26, 13, TEXT2, TEXT2);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x0E1838, 0x0D1634);
		gfx_text(&gfx_font_mono, x + 12, 90, cv[sel].serial, TEXT2);
		x += w + 10;
		const char *media = cv[sel].cd ? "CD" : "DVD";
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, media) + 12;               // media chip (chrome)
		gfx_rrect(x, 86, w, 26, 13, CHROME_T, 0xBAC6DE);
		gfx_icon(cv[sel].cd ? UI_CD_18 : UI_DVD_18, x + 8, 90, INK);
		gfx_text(&gfx_font_ui, x + 32, 89, media, INK);
		x += w + 10;
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, "USB") + 12;               // source chip (iris)
		gfx_alpha((int)(0x8C * k / 2));
		gfx_rrect(x, 86, w, 26, 13, IRIS, IRIS);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x241C52, 0x1E1746);
		gfx_icon(UI_USB_18, x + 8, 90, IRIS);
		gfx_text(&gfx_font_ui, x + 32, 89, "USB", TEXT);
		x += w + 10;
		const char *vm = vid_label[game_video(sel)];
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, vm) + 12;                  // video chip (ice)
		gfx_alpha((int)(0x8C * k / 2));
		gfx_rrect(x, 86, w, 26, 13, ICE, ICE);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x0E2440, 0x0B1C36);
		gfx_icon(UI_CHIP_18, x + 8, 90, ICE);
		gfx_text(&gfx_font_ui, x + 32, 89, vm, TEXT);
	}
	gfx_line(64, 138, 1216, 138, TEXT2, 0x40, 0x13);

	gfx_alpha(0x80);
	if (!ncv) text_c(&gfx_font_ui, 320, "No hay juegos: copia tus ISO a mass0:/DVD o mass0:/CD", TEXT);
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
	if (opt >= 0) {
		options_panel(sel, opt);
		hint(hint(GFX_W - 64, UI_TRIANGLE_14, NULL, UI_GEAR_18, "Guardar"), UI_CROSS_14, NULL, UI_CHIP_18, "Cambiar");
	} else if (ncv)
		hint(hint(hint(GFX_W - 64, -1, "SELECT", UI_CHIP_18, "Datos técnicos"), UI_TRIANGLE_14, NULL, UI_GEAR_18,
		          "Opciones"), UI_CROSS_14, NULL, UI_PLAY_18, "Jugar");
	else hint(GFX_W - 64, -1, "SELECT", UI_CHIP_18, "Datos técnicos");
	static int dl_fade = 240; // frames the line stays after a successful run (4 s), the last 30 fading
	if (dl_state == 3 && dl_fade > 0) dl_fade--;
	if (dl_state && (dl_state != 3 || dl_fade > 0)) { // cover download status, left side of the toast row
		char st[80];
		switch (dl_state) {
		case 1: snprintf(st, sizeof(st), "PORTADAS: CONECTANDO A LA RED"); break;
		case 2: snprintf(st, sizeof(st), "PORTADAS: DESCARGANDO  %d / %d", dl_done, dl_total); break;
		case 3: snprintf(st, sizeof(st), "PORTADAS: %d NUEVAS DE %d", dl_got, dl_total); break;
		case NET_ERR_LINK: snprintf(st, sizeof(st), "PORTADAS: SIN ENLACE DE RED (CABLE?)"); break;
		case NET_ERR_DHCP: snprintf(st, sizeof(st), "PORTADAS: SIN DIRECCIÓN IP (DHCP)"); break;
		case NET_ERR_DNS: snprintf(st, sizeof(st), "PORTADAS: SIN DNS (¿HAY INTERNET?)"); break;
		case NET_ERR_CONNECT: snprintf(st, sizeof(st), "PORTADAS: GITHUB INALCANZABLE"); break;
		case NET_ERR_TLS: snprintf(st, sizeof(st), "PORTADAS: ERROR TLS"); break;
		default: snprintf(st, sizeof(st), "PORTADAS: ERROR DE RED (%d)", dl_state);
		}
		gfx_alpha(dl_state == 3 && dl_fade < 30 ? dl_fade * 0x80 / 30 : 0x80);
		gfx_tracking(2);
		gfx_text(&gfx_font_mono, 64, 614, st, dl_state < 0 ? 0xFF9FB0 : LABEL);
		gfx_tracking(0);
	}
	if (toast > 0 && toast_msg) {
		const char *m = toast_msg;
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

// loader/loader.elf (embedded): copy its PT_LOAD segments to their addresses (0x84000..) and jump. It loads argv[0]
// without resetting the IOP (Neutrino -qb needs our USB modules; ps2sdk's elf-loader resets it).
extern unsigned char loader_elf[];
static void run_loader(int argc, char *argv[])
{
	const u8 *e = loader_elf;
	if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F') return;
	u32 entry = *(u32 *)(e + 24), phoff = *(u32 *)(e + 28);
	u16 phnum = *(u16 *)(e + 44), phsz = *(u16 *)(e + 42);
	memset((void *)0x84000, 0, 0x100000 - 0x84000); // the loader's region, BSS and stack included
	for (int k = 0; k < phnum; k++) {
		const u32 *ph = (const u32 *)(e + phoff + k * phsz); // type, offset, vaddr, paddr, filesz, memsz
		if (ph[0] == 1) memcpy((void *)ph[2], e + ph[1], ph[4]);
	}
	SifExitRpc();
	FlushCache(0);
	FlushCache(2);
	ExecPS2((void *)entry, NULL, argc, argv);
}

static void launch(int i) // Neutrino on the ISO: -dvd=usb:<path> (BSD from the prefix), -qb as nhddl does
{
	for (int t = 0; t < 40; t++) { // let the confirm sound play while the screen fades to the game's name
		gfx_begin();
		gfx_alpha(0x80);
		gfx_rect(0, 0, GFX_W, GFX_H, 0);
		gfx_alpha((int)(0x80 * span(t, 0, 20)));
		gfx_orb(640 - 28, 250, 56);
		gfx_text_chrome(&gfx_font_title, (GFX_W - gfx_text_width(&gfx_font_title, cv[i].title)) / 2, 340, cv[i].title);
		gfx_tracking(3);
		text_c(&gfx_font_mono, 400, "INICIANDO CON NEUTRINO", LABEL);
		gfx_tracking(0);
		gfx_end();
		gfx_flip();
	}
	static char dvd[200], gsm[24], gc[12] = "-gc=";
	char *argv[5];
	int argc = 0, v = game_video(i), c = game_compat(i), n = 4;
	snprintf(dvd, sizeof(dvd), "-dvd=usb:%s", cv[i].path);
	argv[argc++] = NEUTRINO;
	argv[argc++] = dvd;
	if (v > 1) { // 480p / 1080i forced by Neutrino's GS mode selector; native: no -gsm
		snprintf(gsm, sizeof(gsm), "-gsm=%s", v == 2 ? "fp2" : "1080ix2");
		if (c) snprintf(gsm + strlen(gsm), sizeof(gsm) - strlen(gsm), ":%d", c);
		argv[argc++] = gsm;
	}
	for (int k = 0; k < 5; k++)
		if (game_gc(i, k)) gc[n++] = gc_modes[k];
	gc[n] = 0;
	if (n > 4) argv[argc++] = gc;
	argv[argc++] = "-qb";
	printf("launch:");
	for (int k = 0; k < argc; k++) printf(" %s", argv[k]);
	printf("\n");
	gfx_shutdown(); // our vsync handler must not outlive this ELF
	run_loader(argc, argv);
	printf("launch failed\n"); // only reached if the ELF could not be loaded
}

int main(void)
{
	if (!gfx_init()) printf("gfx_init: VRAM pool too small\n");
	make_backgrounds(); // ~0.5 s, screen black (also inside the HDMI relock time)
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
		if (start < 0 && t >= HOLD && (stage >= 1 || t >= HOLD + 360)) { // audio + config ready (or 6 s more): go together
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
	int sel = 0, n = 0, idle = 0, overlay = 0, toast = 0, opt = -1;
	if (!source_ok) toast = 300, toast_msg = "Ese origen de juegos aún no está disponible: usando USB";
	float s = 0;
	unsigned prev = 0;
	const char *saved = usb ? "" : "  SIN USB";
	char text[256];

	for (int f = 0;; f++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		if (opt >= 0) { // options panel owns the pad until △/○
			if (pressed & PAD_DOWN) { if (opt < OPT_ROWS - 1) opt++, play(S_MOVE, 70); else play(S_EDGE, 80); }
			if (pressed & PAD_UP) { if (opt > 0) opt--, play(S_MOVE, 70); else play(S_EDGE, 80); }
			if (pressed & (PAD_RIGHT | PAD_CROSS)) opt_change(sel, opt, 1), play(S_MOVE, 70);
			if (pressed & PAD_LEFT) opt_change(sel, opt, -1), play(S_MOVE, 70);
			if (pressed & (PAD_TRIANGLE | PAD_CIRCLE)) {
				play(S_PANEL, 70);
				opt = -1;
				if (usb && !ini_save(&games, GAMES_INI, "; ORBIT - opciones por juego (menú de triángulo)\n"
				                     "; video = nativo | 480p | 1080i, compat = 1-3, gc = modos de Neutrino (0 2 3 5 7)\n"))
					toast = 180, toast_msg = "No se pudieron guardar las opciones en el USB";
			}
		} else {
			if (pressed & PAD_RIGHT) { if (sel < ncv - 1) sel++, play(S_MOVE, 70); else play(S_EDGE, 80); }
			if (pressed & PAD_LEFT) { if (sel > 0) sel--, play(S_MOVE, 70); else play(S_EDGE, 80); }
			if (pressed & PAD_SELECT) overlay ^= 1, play(S_PANEL, 70);
			if (pressed & PAD_TRIANGLE && ncv) opt = 0, play(S_PANEL, 70);
			if (pressed & PAD_CROSS && ncv) {
				play(S_CONFIRM, 85);
				if (neutrino) launch(sel); // does not return when Neutrino loads
				toast = 120, toast_msg = neutrino ? "Iniciando..." : "Falta Neutrino: cópialo a mass0:/neutrino/";
			}
		}
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
		home(sel, s, k, toast, opt, overlay ? text : NULL, 1 - span(f, 0, 20));
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
