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
#include <libcdvd.h>
#include <unistd.h>
#include <audsrv.h>
#include "gfx.h"
#include "ui_data.h"
#include "iop.h"
#include "iso.h"
#include "ini.h"
#include "net.h"
#include "cover.h"
#include "icon.h"
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

// one entry per runnable thing (phase 11): cv[0] is the disc drive; then PS2 ISOs (mass0:/DVD, mass0:/CD, OPL layout),
// PS1 VCDs (mass0:/POPS) and apps (mass0:/APPS), sorted by title. serial = "SLUS-20946" (dash form, as cover files and
// save dirs use it; "" if none); path relative to mass0:/; big/small = covers/<serial>.c16 / _s.c16, NULL = drawn card
enum { K_PS2, K_PS1, K_APP, K_DISC };
typedef struct {
	char title[64], serial[16], path[160], boot[64]; // boot: disc only, the SYSTEM.CNF BOOT2 path / PS1 file name
	char kind, cd, disc;                               // cd: PS2 CD media; disc: K_DISC's content (D_*)
	void *big, *small, *half;                          // half: grid
} entry;
static entry cv[MAXC];
enum { D_NONE, D_READING, D_PS2, D_PS1, D_OTHER };
static int is_ps2(int i) { return cv[i].kind == K_PS2 || (cv[i].kind == K_DISC && cv[i].disc == D_PS2); }
static int is_ps1(int i) { return cv[i].kind == K_PS1 || (cv[i].kind == K_DISC && cv[i].disc == D_PS1); }
static int kind_icon(int i) { return cv[i].kind == K_APP ? UI_CHIP_18 : cv[i].cd || is_ps1(i) ? UI_CD_18 : UI_DVD_18; }
static const char *kind_label(int i)
{
	return cv[i].kind == K_DISC ? "DISCO" : cv[i].kind == K_APP ? "APP" : cv[i].kind == K_PS1 ? "PS1" : cv[i].cd ? "CD" : "DVD";
}

// views (phase 10): the one shown, restored from estado.ini, which the icon thread rewrites when state_dirty is set
enum { V_CAROUSEL, V_GRID, V_LIST, V_N };
static const char *view_name[V_N] = {"CARRUSEL", "CUADRÍCULA", "LISTA"}, *view_key[V_N] = {"carrusel", "cuadricula", "lista"};
static const int view_icon[V_N] = {UI_CAROUSEL_18, UI_GRID_18, UI_LIST_18};
static int view;
static volatile int state_dirty;
#define STATE_INI "mass0:/orbit/estado.ini"

static void *make_half(const void *big) // 128x184 grid tile from the 256x368 cover, in RAM only (phase 10)
{
	unsigned short *h = memalign(64, COVER_HW * COVER_HH * 2);
	if (h) cover_half(big, h), SyncDCache(h, h + COVER_HW * COVER_HH);
	return h; // NULL: the grid draws the small cover scaled instead
}
static volatile int ncv, stage, done_n, total_n, usb, load_ms, neutrino; // written by the loader thread
#define NEUTRINO "mass0:/neutrino/neutrino.elf"
#define POPSTARTER "mass0:/POPS/POPSTARTER.ELF"

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

// ---- 3D save icons (phase 9): newest save's icon.sys + list icon, loaded by the icon thread on first selection ----
static icon *gicon[MAXC];
static volatile char gicon_state[MAXC]; // 0 not asked, 1 loading, 2 done (gicon NULL if it failed)
static volatile int icon_want = -1;
static int icon_sema = -1;

static u64 when(const sceMcStDateTime *t)
{
	return (u64)t->Year << 40 | (u64)t->Month << 32 | (u64)t->Day << 24 | t->Hour << 16 | t->Min << 8 | t->Sec;
}

static const char *newest_save(const char *serial, int *port) // the save save_info describes, or NULL
{
	const sceMcTblGetDir *best = NULL;
	for (int p = 0; p < 2; p++)
		for (int i = 0; i < mcn[p]; i++) {
			const sceMcTblGetDir *e = &mcdir[p][i];
			if (!(e->AttrFile & MC_ATTR_SUBDIR) || !strstr((const char *)e->EntryName, serial)) continue;
			if (!best || when(&e->_Modify) > when(&best->_Modify)) best = e, *port = p;
		}
	return best ? (const char *)best->EntryName : NULL;
}

static void *mc_read(int port, const char *path, int *size) // whole file, 64-aligned; NULL if missing
{
	int fd, n, r;
	mcOpen(port, 0, path, FIO_O_RDONLY); // the IOP's mode bits: newlib's O_RDONLY is 0, and mcRead then gives -5
	mcSync(0, NULL, &fd);
	if (fd < 0) return NULL;
	mcSeek(fd, 0, SEEK_END);
	mcSync(0, NULL, &n);
	mcSeek(fd, 0, SEEK_SET);
	mcSync(0, NULL, &r);
	void *buf = n > 0 && n < (2 << 20) ? memalign(64, (n + 63) & ~63) : NULL;
	if (buf) {
		mcRead(fd, buf, n);
		mcSync(0, NULL, &r);
		if (r != n) free(buf), buf = NULL;
	}
	mcClose(fd);
	mcSync(0, NULL, &r);
	*size = n;
	return buf;
}

static icon *load_icon(int i)
{
	int port, n;
	char path[128], name[65];
	const char *dir = newest_save(cv[i].serial, &port);
	if (!dir) return NULL;
	snprintf(path, sizeof(path), "/%s/icon.sys", dir);
	unsigned char *sys = mc_read(port, path, &n), *ico = NULL;
	icon *ic = calloc(1, sizeof(icon));
	int ok = ic && sys && icon_sys_parse(sys, n, ic, name, sizeof(name));
	if (ok) {
		snprintf(path, sizeof(path), "/%s/%s", dir, name);
		ico = mc_read(port, path, &n);
		ok = ico && icon_parse(ico, n, ic);
	}
	free(sys), free(ico);
	if (!ok) {
		printf("icon %s: %s not loaded (icon.sys or its list icon missing or invalid)\n", cv[i].serial, dir);
		free(ic);
		return NULL;
	}
	if (ic->tex) SyncDCache(ic->tex, ic->tex + 128 * 128); // gfx_mesh DMAs it
	printf("icon %s: %s/%s, %d vertices, %d shapes\n", cv[i].serial, dir, name, ic->nv, ic->shapes);
	return ic;
}

static u8 icon_stack[0x10000] __attribute__((aligned(16)));
static void icon_thread(void *arg) // the only libmc user after the boot scan; woken by the render thread
{
	(void)arg;
	for (;;) {
		WaitSema(icon_sema);
		if (state_dirty && usb) { // the chosen view, for the next boot (phase 10)
			state_dirty = 0;
			FILE *f = fopen(STATE_INI, "w");
			if (f) fprintf(f, "; ORBIT - estado de la interfaz (lo escribe el launcher)\n[ui]\nvista = %s\n",
			               view_key[view]), fclose(f);
		}
		int i = icon_want;
		if (i < 0 || gicon_state[i]) continue;
		gicon_state[i] = 1;
		gicon[i] = load_icon(i);
		gicon_state[i] = 2;
	}
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

// VCD (POPStarter): a 1 MB header, then the disc's raw 2352-byte Mode 2 sectors, data 24 bytes in (cue2pops layout;
// sources.md E). ponytail: Mode 2 assumed; Mode 1 PS1 data tracks would need offset 16
static int vcd_read(void *ctx, unsigned lba, void *buf, unsigned n)
{
	int fd = *(int *)ctx;
	for (unsigned k = 0; k * 2048 < n; k++) {
		unsigned part = n - k * 2048 < 2048 ? n - k * 2048 : 2048;
		if (fileXioLseek64(fd, 0x100000 + (s64)(lba + k) * 2352 + 24, FIO_SEEK_SET) < 0 ||
		    fileXioRead(fd, (u8 *)buf + k * 2048, part) != (int)part) return 0;
	}
	return 1;
}

static int has_ext(const char *name, const char *ext)
{
	int n = strlen(name), e = strlen(ext);
	return n > e && !strcasecmp(name + n - e, ext);
}

static void load_covers(entry *e) // covers/<serial>.c16 + _s.c16, both or none; the grid's half from the big one
{
	char path[64];
	if (!*e->serial) return;
	snprintf(path, sizeof(path), "mass0:/covers/%s.c16", e->serial);
	e->big = load_c16(path, LW, LH);
	snprintf(path, sizeof(path), "mass0:/covers/%s_s.c16", e->serial);
	e->small = e->big ? load_c16(path, SW, SH) : NULL;
	if (!e->small) free(e->big), e->big = NULL;
	e->half = e->big ? make_half(e->big) : NULL;
}

static int count_dir(const char *dir, const char *ext)
{
	char path[64];
	struct dirent *e;
	int n = 0;
	snprintf(path, sizeof(path), "mass0:/%s", dir);
	DIR *d = opendir(path);
	while (d && (e = readdir(d))) n += ext ? has_ext(e->d_name, ext) : e->d_name[0] != '.';
	if (d) closedir(d);
	return n;
}

static void load_games(void) // PS2 ISOs, PS1 VCDs and apps: serial from SYSTEM.CNF, title from the name, covers
{
	static const char *dirs[3] = {"DVD", "CD", "POPS"};
	static ini cfg_app; // APPS/<dir>/title.cfg: "title=" and "boot=" (OPL)
	struct dirent *e;
	char path[300], raw[16];
	int n = 1; // cv[0]: the disc drive, filled in by the disc thread
	cv[0].kind = K_DISC, cv[0].disc = D_NONE;
	snprintf(cv[0].title, sizeof(cv[0].title), "Sin disco");
	total_n = count_dir("DVD", ".iso") + count_dir("CD", ".iso") + count_dir("POPS", ".vcd") + count_dir("APPS", NULL);
	for (int k = 0; k < 3; k++) {
		snprintf(path, sizeof(path), "mass0:/%s", dirs[k]);
		DIR *d = opendir(path);
		while (d && (e = readdir(d)) && n < MAXC) {
			int ps1 = k == 2;
			if (!has_ext(e->d_name, ps1 ? ".vcd" : ".iso")) continue;
			snprintf(cv[n].path, sizeof(cv[n].path), "%s/%s", dirs[k], e->d_name);
			snprintf(path, sizeof(path), "mass0:/%s", cv[n].path);
			int fd = fileXioOpen(path, FIO_O_RDONLY);
			int ok = fd >= 0 && iso_serial(ps1 ? vcd_read : fx_read, &fd, ps1, raw); // SYSTEM.CNF; the OPL name after
			if (fd >= 0) fileXioClose(fd);
			if (!ok) ok = name_serial(e->d_name, raw);
			done_n++;
			if (!ok && !ps1) { printf("%s: no PS2 SYSTEM.CNF nor OPL serial in the name, skipped\n", path); continue; }
			if (ok) serial_dash(raw, cv[n].serial); // a PS1 game without one is still listed, without covers
			iso_title(e->d_name, cv[n].title, sizeof(cv[n].title));
			cv[n].kind = ps1 ? K_PS1 : K_PS2, cv[n].cd = k == 1;
			load_covers(&cv[n++]);
		}
		if (d) closedir(d);
	}
	DIR *d = opendir("mass0:/APPS");
	while (d && (e = readdir(d)) && n < MAXC) {
		if (e->d_name[0] == '.') continue;
		done_n++;
		if (has_ext(e->d_name, ".elf")) { // a loose ELF: titled by its name
			snprintf(cv[n].path, sizeof(cv[n].path), "APPS/%s", e->d_name);
			iso_title(e->d_name, cv[n].title, sizeof(cv[n].title));
		} else { // a folder with title.cfg
			snprintf(path, sizeof(path), "mass0:/APPS/%s/title.cfg", e->d_name);
			if (!ini_load(&cfg_app, path) || !*ini_get(&cfg_app, "", "boot", "")) continue;
			snprintf(cv[n].path, sizeof(cv[n].path), "APPS/%s/%s", e->d_name, ini_get(&cfg_app, "", "boot", ""));
			snprintf(cv[n].title, sizeof(cv[n].title), "%s", ini_get(&cfg_app, "", "title", e->d_name));
		}
		cv[n].kind = K_APP;
		n++;
	}
	if (d) closedir(d);
	for (int i = 2; i < n; i++) // readdir order is the FAT order: sort by title for a stable row (the disc stays first)
		for (int j = i; j > 1 && strcasecmp(cv[j - 1].title, cv[j].title) > 0; j--) {
			entry t = cv[j]; cv[j] = cv[j - 1]; cv[j - 1] = t;
		}
	ncv = n;
}

// ---- disc drive (phase 11): polled once a second off the render thread; a change is staged here and copied into
// cv[0] by the render thread between frames, so cv[0] has one writer after boot ----
static volatile int disc_new, disc_tid = -1;
static entry disc_stage;
static u8 disc_stack[0x8000] __attribute__((aligned(16)));

static int disc_state(int t)
{
	if (t == SCECdNODISC) return D_NONE;
	if ((t >= SCECdDETCT && t <= SCECdDETCTDVDD) || t == SCECdUNKNOWN) return D_READING;
	if (t == SCECdPS2CD || t == SCECdPS2CDDA || t == SCECdPS2DVD) return D_PS2;
	if (t == SCECdPSCD || t == SCECdPSCDDA) return D_PS1;
	return D_OTHER; // DVD video, audio CD, ...: not launched (phase-11 non-goal)
}

static int disc_cnf(char *cnf, int max) // cdrom0:\SYSTEM.CNF;1 through libcdvd (the cdrom0: device is not in iomanX)
{
	static u8 sec[2 * 2048] __attribute__((aligned(64)));
	sceCdlFILE f;
	sceCdRMode mode = {5, SCECdSpinNom, SCECdSecS2048, 0};
	if (!sceCdSearchFile(&f, "\\SYSTEM.CNF;1")) return 0;
	unsigned n = f.size < sizeof(sec) - 1 ? f.size : sizeof(sec) - 1;
	SyncDCache(sec, sec + sizeof(sec));
	if (!sceCdRead(f.lsn, (n + 2047) / 2048, sec, &mode)) return 0;
	sceCdSync(0);
	InvalidDCache(sec, sec + sizeof(sec));
	snprintf(cnf, max, "%.*s", (int)n, (const char *)sec);
	return 1;
}

static void disc_thread(void *arg)
{
	(void)arg;
	int last = -1;
	sceCdInit(SCECdINoD);
	for (;; sleep(1)) {
		int st = disc_state(sceCdGetDiskType());
		if (st == last || disc_new) continue;
		entry *d = &disc_stage;
		memset(d, 0, sizeof(*d));
		d->kind = K_DISC, d->disc = st;
		char cnf[4096], file[16];
		if ((st == D_PS2 || st == D_PS1) && sceCdDiskReady(0) == 2 /* SCECdComplete */ && disc_cnf(cnf, sizeof(cnf)) &&
		    cnf_boot(cnf, st == D_PS1, file, d->boot, sizeof(d->boot))) {
			if (st == D_PS1) snprintf(d->boot, sizeof(d->boot), "%s", file); // PS1DRV takes the bare file name
			serial_dash(file, d->serial);
		}
		const char *what = st == D_NONE ? "Sin disco" : st == D_READING ? "Leyendo disco..." : st == D_OTHER ?
		                   "Disco no compatible" : st == D_PS2 ? "Disco de PS2" : "Disco de PS1";
		snprintf(d->title, sizeof(d->title), "%s", what);
		for (int i = 1; i < ncv && *d->serial; i++) // the same game on the USB lends its title
			if (!strcmp(cv[i].serial, d->serial)) snprintf(d->title, sizeof(d->title), "%s", cv[i].title);
		load_covers(d); // by serial, if the USB has them
		printf("disc: state %d, %s %s boot %s\n", st, d->serial, d->title, d->boot);
		last = st;
		disc_new = 1;
	}
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
	static ini state;
	ini_load(&state, STATE_INI);
	for (int v = 0; v < V_N; v++)
		if (!strcasecmp(ini_get(&state, "ui", "vista", ""), view_key[v])) view = v;
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
#define COVER_PATH "/xlenore/%s/main/covers/default/%s.jpg" // ps2-covers as tools/fetch_covers.py; psx-covers (PS1)
static int wants_cover(int i) { return !cv[i].big && *cv[i].serial && (cv[i].kind == K_PS2 || cv[i].kind == K_PS1); }
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
	for (int i = 1; i < ncv; i++) dl_total += wants_cover(i); // not the disc entry: its covers come from the USB
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
		if (i == 0 || !wants_cover(i)) continue;
		char path[96];
		int body, len;
		snprintf(path, sizeof(path), COVER_PATH, cv[i].kind == K_PS1 ? "psx-covers" : "ps2-covers", cv[i].serial);
		int st = https_get(COVER_HOST, path, buf, max, &body, &len);
		if (st < 0) { dl_state = st; break; } // network or TLS: the rest would fail the same way
		unsigned short *big = memalign(64, LW * LH * 2), *small = memalign(64, SW * SH * 2);
		if (st == 200 && big && small && cover_from_jpeg((unsigned char *)buf + body, len, big, small)) {
			save_c16(cv[i].serial, "", big, LW, LH); // shown even if the USB write fails; fetched again next boot
			save_c16(cv[i].serial, "_s", small, SW, SH);
			SyncDCache(big, big + LW * LH);
			SyncDCache(small, small + SW * SH);
			cv[i].half = make_half(big);
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
	ee_sema_t sema = { .init_count = 0, .max_count = 1 };
	icon_sema = CreateSema(&sema);
	ee_thread_t it = { .func = icon_thread, .stack = icon_stack, .stack_size = sizeof(icon_stack), .gp_reg = &_gp,
	                   .initial_priority = 0x40 };
	int itid = icon_sema >= 0 ? CreateThread(&it) : -1;
	if (itid >= 0) StartThread(itid, NULL);
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
	ee_thread_t dt = { .func = disc_thread, .stack = disc_stack, .stack_size = sizeof(disc_stack), .gp_reg = &_gp,
	                   .initial_priority = 0x40 };
	if ((disc_tid = CreateThread(&dt)) >= 0) StartThread(disc_tid, NULL);
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

// ---- home ambience (phase 10, user 2026-10-01: "algo animado"): everything slow and dim, behind the covers ----
static float hash01(int i) { unsigned h = (unsigned)i * 2654435761u; h ^= h >> 15; return (h & 0xFFFF) / 65535.f; }

static void ambient(int fr, float par) // fr: frames since the home screen; par: eased selection (floor parallax)
{
	float t = fr / 60.f;
	gfx_dither(1);
	gfx_alpha(0x2A); // aurora: two soft glows on slow Lissajous paths
	gfx_glow((int)(160 + 240 * sinf(t * 0.11f)), (int)(60 + 60 * sinf(t * 0.07f + 1)), 800, 440, IRIS, 0x3040A0);
	gfx_alpha(0x22);
	gfx_glow((int)(460 + 260 * sinf(t * 0.09f + 2)), (int)(90 + 70 * cosf(t * 0.08f)), 740, 400, ICE, 0x2050A0);

	const int hz = 418;          // floor: horizontals flow towards the viewer, one line every ~2.9 s;
	float ph = fmodf(t * 0.35f, 1); // verticals slide against the selection (parallax)
	for (int k = -1; k < 13; k++) {
		float kk = k + 1 - ph, y = hz + (GFX_H + 40 - hz) / (1 + 0.55f * (kk > 0 ? kk : 0));
		int al = (int)(0x1C * (y - hz) / (GFX_H - hz) * clampf(12 - kk));
		if (y < GFX_H && al > 0) gfx_line(0, y, GFX_W, y, ICE, al, al);
	}
	float off = fmodf(par * 46, 150);
	for (int i = -15; i <= 15; i++)
		gfx_line(GFX_W / 2 + i * 150.f - off, GFX_H, GFX_W / 2 + (i * 150.f - off) * 0.28f, hz, ICE, 0x1C, 0);
	gfx_line(0, hz, 640, hz, ICE, 0, 0x46);
	gfx_line(640, hz, GFX_W, hz, IRIS, 0x46, 0);
	float sw = fmodf(t, 7) / 2.4f; // light sweep along the horizon every 7 s
	if (sw < 1) {
		float x = -240 + (GFX_W + 480) * ease(sw), a = sinf(sw * 3.14159f);
		gfx_line(x - 220, hz, x, hz, 0xFFFFFF, 0, (int)(0x90 * a));
		gfx_alpha((int)(0x50 * a));
		gfx_glow((int)x - 50, hz - 22, 100, 44, 0xFFFFFF, ICE);
	}
	gfx_alpha((int)(0x20 + 0x08 * sinf(t * 0.8f))); // the floor glow breathes
	gfx_glow(340, 470, 600, 120, ICE, ICE);

	for (int i = 0; i < 28; i++) { // motes: small sparkles rising, swaying and twinkling
		float sp = 10 + 14 * hash01(i * 3 + 1), y = GFX_H + 20 - fmodf(t * sp + 640 * hash01(i * 3 + 2), 600);
		float x = GFX_W * hash01(i * 3) + 16 * sinf(t * 0.5f + i), tw = 0.5f + 0.5f * sinf(t * (1.3f + hash01(i)) + i * 2);
		int sz = 6 + (int)(8 * hash01(i * 7)), a = (int)(0x68 * tw * clampf((y - 140) / 120) * clampf((GFX_H - y) / 80));
		if (a <= 0) continue;
		gfx_alpha(a);
		unsigned c = i % 3 == 0 ? IRIS : i % 3 == 1 ? ICE : 0xDDEBFF;
		gfx_icon_scaled(UI_SPARKLE_12, (int)x, (int)y, sz, sz, c, c);
	}
	static const short st[3][3] = {{1116, 146, 0}, {150, 196, 1}, {1194, 378, 2}}; // the design's three, twinkling
	for (int i = 0; i < 3; i++) {
		gfx_alpha((int)(0x80 * (0.65f + 0.35f * sinf(t * 1.1f + st[i][2] * 2.1f))));
		gfx_icon(i ? UI_SPARKLE_12 : UI_SPARKLE_24, st[i][0], st[i][1], i == 0 ? 0xDDEBFF : i == 1 ? 0xB7C3FF : ICE);
	}
	gfx_dither(0);
}

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
	gfx_icon(kind_icon(i), x + 12, y + 12, LABEL);
	if (w >= 160) gfx_text(&gfx_font_mono, x + 36, y + 13, *cv[i].serial ? cv[i].serial : kind_label(i), LABEL); // grid: no room
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

// ---- views (phase 10): every view only gives each cover a target box; each frame the drawn box eases towards it
// (0.2, as the carousel always did) and snaps when close, so rests are pixel-exact and a view switch is the same
// motion: every cover flies from its old place to its new one, started in a wave from the selection ----
typedef struct { float x, y, w, h, a, f; } box; // a: alpha 0..1, f: focus (frame + glow) 0..1
static box shown_box[MAXC];
static unsigned char wave[MAXC]; // frames before a cover starts moving after a view switch
static int gtop, ltop;           // first grid row / first list row on screen (targets)
// filters (phase 11, L1/R1): views lay out positions, not indices; pos[i] < 0 = hidden (fades where it is)
enum { F_ALL, F_PS2, F_PS1, F_APP, F_N };
static const char *filter_name[F_N] = {"TODO", "PS2", "PS1", "APPS"};
static int filter, pos[MAXC], order[MAXC], nord;

static int shown_in(int i, int fl)
{
	return fl == F_ALL || (fl == F_PS2 && is_ps2(i)) || (fl == F_PS1 && is_ps1(i)) || (fl == F_APP && cv[i].kind == K_APP);
}

static int filter_has(int fl)
{
	for (int i = 0; i < ncv; i++)
		if (shown_in(i, fl)) return 1;
	return 0;
}

static void refilter(void)
{
	nord = 0;
	for (int i = 0; i < ncv; i++) pos[i] = shown_in(i, filter) ? (order[nord] = i, nord++) : -1;
}
static float lscroll, pill_y, lpres; // list: eased first row, highlight y, presence 0..1 (rows slide in / out)
static int vlabel, label_filter; // frames left showing the view's (or, label_filter, the filter's) name
static void fit(const gfx_font *f, const char *s, int max_w, char *out, int n);

#define GCOLS 7      // grid: 7 x 128 px tiles, 40 px gaps, centred; rows every 220 px from y 176, two on screen
#define GPX 168
#define GX0 72
#define GY0 176
#define GPY 220
#define LROW 52      // list: 52 px rows from x 64 to 720, y 166, 8 on screen; the selected cover on the right
#define LX 64
#define LWID 656
#define LY0 166
#define LVIS 8
#define LCX 880
#define LCY 182

static float edge_fade(float y, float h, float top, float bottom, float soft) // 1 inside [top, bottom], 0 soft px out
{
	float a = 1;
	if (y < top) a = 1 - (top - y) / soft;
	if (y + h > bottom && 1 - (y + h - bottom) / soft < a) a = 1 - (y + h - bottom) / soft;
	return clampf(a);
}

static box target(int i, int sel)
{
	box b = {0};
	if (pos[i] < 0) { b = shown_box[i]; b.a = 0, b.f = 0; return b; } // filtered out: fades where it is
	if (view == V_CAROUSEL) { // the phase-3 row at rest: selected 256x368 at x 512, the others 184x264, D apart
		int di = pos[i] - pos[sel];
		b.w = di ? SW : LW, b.h = di ? SH : LH, b.a = di ? 0.82f : 1, b.f = !di; // others at 82 % (design)
		b.x = GFX_W / 2 + di * D + (di < 0 ? -E : di > 0 ? E : 0) - b.w / 2, b.y = CY - b.h / 2;
	} else if (view == V_GRID) {
		int r = pos[i] / GCOLS - gtop, focus = i == sel;
		float sc = focus ? 1.12f : 1, ty = GY0 + r * GPY;
		b.w = COVER_HW * sc, b.h = COVER_HH * sc, b.f = focus;
		b.x = GX0 + pos[i] % GCOLS * GPX + (COVER_HW - b.w) / 2, b.y = ty + (COVER_HH - b.h) / 2;
		b.a = (focus ? 1 : 0.82f) * edge_fade(ty, COVER_HH, GY0 - 8, GY0 + GPY + COVER_HH + 8, 60);
	} else if (i == sel) b.x = LCX, b.y = LCY, b.w = LW, b.h = LH, b.a = 1, b.f = 1;
	else b.x = LX, b.y = LY0 + (pos[i] - ltop) * LROW, b.w = 36, b.h = 52; // folded into its row, invisible
	return b;
}

static void ease_to(float *v, float t, float rate, float snap) { *v += (t - *v) * rate; if (fabsf(t - *v) < snap) *v = t; }

static void animate(int sel, float rate)
{
	for (int i = 0; i < ncv; i++) {
		if (wave[i]) { wave[i]--; continue; }
		box t = target(i, sel), *c = &shown_box[i];
		ease_to(&c->x, t.x, rate, 0.3f), ease_to(&c->y, t.y, rate, 0.3f);
		ease_to(&c->w, t.w, rate, 0.3f), ease_to(&c->h, t.h, rate, 0.3f);
		ease_to(&c->a, t.a, rate, 0.004f), ease_to(&c->f, t.f, rate, 0.004f);
	}
}

static void draw_cover(int i)
{
	const box *b = &shown_box[i];
	int x = (int)lroundf(b->x), y = (int)lroundf(b->y), w = (int)lroundf(b->w), h = (int)lroundf(b->h);
	if (b->a < 0.01f || w < 8 || x + w + 60 < 0 || x - 60 > GFX_W || y > GFX_H || y + h < 0) return;
	if (b->f > 0.01f) { // chrome frame + ice/iris glow, faded with the focus; the glow margin scales with the cover
		float f = b->f * b->a;
		int g = 90 * w / LW;
		gfx_alpha((int)(0x40 * f));
		gfx_dither(1);
		gfx_glow(x - g, y - g, w + 2 * g, h + 2 * g, 0x9070FF, ICE);
		gfx_dither(0);
		gfx_alpha((int)(0x4C * f));
		gfx_rrect(x - 5, y - 5, w + 10, h + 10, 7, ICE, ICE);
		gfx_alpha((int)(0x80 * f));
		gfx_rrect(x - 4, y - 4, w + 8, h + 8, 6, 0xFFFFFF, 0x8D9BBB);
	}
	gfx_alpha((int)(0x80 * b->a));
	if (!cv[i].big) { generic_cover(i, x, y, w, h); return; }
	if (w == LW && h == LH) gfx_image(cv[i].big, LW, LH, x, y, w, h); // at rest: pixel-exact
	else if (w == SW && h == SH) gfx_image(cv[i].small, SW, SH, x, y, w, h);
	else if (cv[i].half && w == COVER_HW && h == COVER_HH) gfx_image(cv[i].half, COVER_HW, COVER_HH, x, y, w, h);
	else if (w > SW) gfx_image(cv[i].big, LW, LH, x, y, w, h); // moving: the smallest image not below the size,
	else if (w > COVER_HW || !cv[i].half) gfx_image(cv[i].small, SW, SH, x, y, w, h); // bilinear (no mipmaps:
	else gfx_image(cv[i].half, COVER_HW, COVER_HH, x, y, w, h);                     // never minify past 2x)
}

static void list_rows(int sel, float lscroll, float pill_y, float pres) // the list's text side; pres: 0..1 presence
{
	char t[96];
	if (pres < 0.01f) return;
	float pa = pres * edge_fade(pill_y, LROW - 6, LY0 - 4, LY0 + LVIS * LROW + 4, 26);
	gfx_alpha((int)(0x26 * pa)); // highlight pill, gliding to the selected row
	gfx_rrect(LX - 1, (int)pill_y - 1, LWID + 2, LROW - 4, (LROW - 6) / 2 + 1, ICE, IRIS);
	gfx_alpha((int)(0x80 * pa));
	gfx_rrect(LX, (int)pill_y, LWID, LROW - 6, (LROW - 6) / 2, 0x1B2850, 0x0D1530);
	int first = (int)lscroll - 1, last = (int)lscroll + LVIS + 1;
	for (int p = first < 0 ? 0 : first; p < nord && p <= last; p++) {
		int i = order[p];
		float y = LY0 + (p - lscroll) * LROW, row = clampf(pres * 1.6f - (p - (int)lscroll) * 0.06f); // entrance wave
		float a = ease(row) * edge_fade(y, LROW - 6, LY0 - 4, LY0 + LVIS * LROW + 4, 26);
		if (a < 0.01f) continue;
		int x = LX + (int)(-40 * (1 - ease(row))), cy = (int)y + (LROW - 6) / 2;
		gfx_alpha((int)(0x80 * a));
		gfx_icon(kind_icon(i), x + 20, cy - 9, i == sel ? ICE : LABEL);
		fit(&gfx_font_ui, cv[i].title, LWID - 190, t, sizeof(t));
		gfx_text(&gfx_font_ui, x + 52, cy - 11, t, i == sel ? TEXT : TEXT2);
		gfx_text(&gfx_font_mono, x + LWID - 20 - gfx_text_width(&gfx_font_mono, cv[i].serial), cy - 9, cv[i].serial,
		         i == sel ? TEXT2 : LABEL);
	}
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
	static int amb;
	ambient(amb++, s);

	list_rows(sel, lscroll, pill_y, lpres);
	for (int i = 0; i < ncv; i++)
		if (i != sel) draw_cover(i);
	if (ncv) draw_cover(sel); // last: its frame stays on top while it grows

	// header: orb, chrome title, chips; saves card on the right — always the selected game, fading in
	gfx_alpha(0x80);
	gfx_alpha(0x30);
	gfx_dither(1);
	gfx_glow(44, 25, 96, 96, ICE, ICE);
	gfx_dither(0);
	gfx_alpha(0x80);
	gfx_orb(64, 45, 56);
	if (ncv && k > 0) {
		int cx = GFX_W - 64;
		if (is_ps2(sel) && *cv[sel].serial) { // saves card: PS2 games only (PS1 saves live in POPS VMCs, apps have none)
		int sc = save_info(cv[sel].serial, a, b, sizeof(a));
		int cw = 18 + 64 + 14 + (gfx_text_width(&gfx_font_ui, a) > gfx_text_width(&gfx_font_mono, b) ?
		                         gfx_text_width(&gfx_font_ui, a) : gfx_text_width(&gfx_font_mono, b)) + 18;
		cx = GFX_W - 64 - cw;
		int sx = cx + 18, sy = 48; // card: padding 12 / 18, 64x64 icon slot (design)
		gfx_alpha((int)(0x26 * k));
		gfx_rrect(cx - 1, 35, cw + 2, 90, 19, ICE, ICE);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(cx, 36, cw, 88, 18, 0x1B2850, 0x0D1530);
		gfx_alpha((int)(0x40 * k));
		gfx_dither(1);
		gfx_glow(sx + 4, sy + 50, 56, 12, ICE, ICE); // the icon's floor glow
		gfx_dither(0);
		gfx_alpha((int)(0x80 * k));
		if (sc && icon_sema >= 0 && !gicon_state[sel] && icon_want != sel) icon_want = sel, SignalSema(icon_sema);
		icon *ic = sc && gicon_state[sel] == 2 ? gicon[sel] : NULL;
		static gfx_vtx *mesh;
		static int mesh_n, frame;
		frame++;
		if (ic && mesh_n < ic->nv) mesh = realloc(mesh, sizeof(gfx_vtx) * ic->nv), mesh_n = mesh ? ic->nv : 0;
		// one animation frame per video frame x its speed (estimate, sources.md); 1/8 turn a second (design: D)
		int drawn = ic && mesh && gfx_mesh(ic->tex, mesh, icon_draw_list(ic, frame * (ic->speed > 0 ? ic->speed : 1),
		                                                                 frame * 6.28318f / 480, 4, 0, 56, mesh), sx, sy);
		if (!drawn) gfx_icon(UI_MEMCARD_48, sx + 8, sy + 4, sc ? ICE : 0x5A6787); // loading, failed, or no save
		gfx_text(&gfx_font_ui, cx + 96, 58, a, TEXT);
		gfx_text(&gfx_font_mono, cx + 96, 82, b, TEXT2);
		}

		fit(&gfx_font_title, cv[sel].title, cx - 40 - 140, a, sizeof(a));
		gfx_text_chrome(&gfx_font_title, 140, 34, a);
		int x = 140, w;
		if (*cv[sel].serial) { // serial chip
			w = gfx_text_width(&gfx_font_mono, cv[sel].serial) + 24;
			gfx_alpha((int)(0x60 * k));
			gfx_rrect(x, 86, w, 26, 13, TEXT2, TEXT2);
			gfx_alpha((int)(0x80 * k));
			gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x0E1838, 0x0D1634);
			gfx_text(&gfx_font_mono, x + 12, 90, cv[sel].serial, TEXT2);
			x += w + 10;
		}
		const char *media = kind_label(sel);
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, media) + 12;               // kind chip (chrome)
		gfx_rrect(x, 86, w, 26, 13, CHROME_T, 0xBAC6DE);
		gfx_icon(kind_icon(sel), x + 8, 90, INK);
		gfx_text(&gfx_font_ui, x + 32, 89, media, INK);
		x += w + 10;
		const char *src = cv[sel].kind == K_DISC ? "UNIDAD" : "USB";
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, src) + 12;                 // source chip (iris)
		gfx_alpha((int)(0x8C * k / 2));
		gfx_rrect(x, 86, w, 26, 13, IRIS, IRIS);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x241C52, 0x1E1746);
		gfx_icon(cv[sel].kind == K_DISC ? UI_DVD_18 : UI_USB_18, x + 8, 90, IRIS);
		gfx_text(&gfx_font_ui, x + 32, 89, src, TEXT);
		x += w + 10;
		if (cv[sel].kind == K_PS2) { // video chip: what Neutrino will force
		const char *vm = vid_label[game_video(sel)];
		w = 8 + 18 + 6 + gfx_text_width(&gfx_font_ui, vm) + 12;                  // video chip (ice)
		gfx_alpha((int)(0x8C * k / 2));
		gfx_rrect(x, 86, w, 26, 13, ICE, ICE);
		gfx_alpha((int)(0x80 * k));
		gfx_rrect(x + 1, 87, w - 2, 24, 12, 0x0E2440, 0x0B1C36);
		gfx_icon(UI_CHIP_18, x + 8, 90, ICE);
		gfx_text(&gfx_font_ui, x + 32, 89, vm, TEXT);
		}
	}
	gfx_line(64, 138, 1216, 138, TEXT2, 0x40, 0x13);

	gfx_alpha(0x80);
	if (ncv) { // footer: position in the filter, then the filters (L1 / R1) on the left; hints on the right
		snprintf(a, sizeof(a), "%02d / %02d", pos[sel] + 1, nord);
		gfx_text(&gfx_font_mono, 64, 658, a, TEXT);
		int tx = 64 + gfx_text_width(&gfx_font_mono, a) + 16;
		for (int fl = -1; fl <= F_N; fl++) {
			if (fl < 0 || fl == F_N) { // L1 / R1 key caps at both ends
				const char *key = fl < 0 ? "L1" : "R1";
				int kw = gfx_text_width(&gfx_font_mono, key) + 12;
				gfx_alpha(0x80);
				gfx_rrect(tx, 655, kw, 22, 11, CHROME_T, CHROME_B);
				gfx_text(&gfx_font_mono, tx + 6, 658, key, INK);
				tx += kw + 6;
				continue;
			}
			int pw = gfx_text_width(&gfx_font_mono, filter_name[fl]) + 18, on = fl == filter;
			gfx_alpha(on ? 0x80 : 0x30);
			gfx_rrect(tx, 655, pw, 22, 11, on ? ICE : TEXT2, on ? ICE : TEXT2);
			if (!on) gfx_alpha(0x80), gfx_rrect(tx + 1, 656, pw - 2, 20, 10, 0x0B1430, 0x0A1128);
			gfx_alpha(0x80);
			gfx_text(&gfx_font_mono, tx + 9, 658, filter_name[fl], on ? INK : TEXT2);
			tx += pw + 6;
		}
	}
	if (opt >= 0) {
		options_panel(sel, opt);
		hint(hint(GFX_W - 64, UI_TRIANGLE_14, NULL, UI_GEAR_18, "Guardar"), UI_CROSS_14, NULL, UI_CHIP_18, "Cambiar");
	} else if (ncv) { // "Datos" (not "Datos técnicos"): the filters need the room on the left
		int x = hint(hint(GFX_W - 64, -1, "SELECT", UI_CHIP_18, "Datos"), UI_SQUARE_14, NULL, view_icon[(view + 1) % V_N],
		             "Vista");
		if (cv[sel].kind == K_PS2) x = hint(x, UI_TRIANGLE_14, NULL, UI_GEAR_18, "Opciones"); // Neutrino options only
		hint(x, UI_CROSS_14, NULL, UI_PLAY_18, "Jugar");
	} else hint(GFX_W - 64, -1, "SELECT", UI_CHIP_18, "Datos");
	if (vlabel > 0) { // the view's or filter's name, a chrome-edged pill centred over the footer, ~1 s after a change
		float va = vlabel > 50 ? ease((70 - vlabel) / 20.f) : vlabel / 50.f;
		if (label_filter) snprintf(a, sizeof(a), "MOSTRAR  %s", filter_name[filter]);
		else snprintf(a, sizeof(a), "VISTA  %s", view_name[view]);
		gfx_tracking(3);
		int w = 18 + 18 + 10 + gfx_text_width(&gfx_font_mono, a) + 20, x = (GFX_W - w) / 2, y = 590 + (int)(8 * (1 - va));
		gfx_alpha((int)(0x40 * va));
		gfx_rrect(x - 1, y - 1, w + 2, 38, 19, ICE, IRIS);
		gfx_alpha((int)(0x80 * va));
		gfx_rrect(x, y, w, 36, 18, 0x1B2850, 0x0D1530);
		gfx_icon(label_filter ? UI_GRID_18 : view_icon[view], x + 18, y + 9, ICE);
		gfx_text(&gfx_font_mono, x + 46, y + 10, a, TEXT);
		gfx_tracking(0);
	}
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

static int exists(const char *path) { FILE *f = fopen(path, "rb"); if (f) fclose(f); return f != NULL; }

static const char *launch_problem(int i) // why X cannot start entry i, or NULL (shown as a toast)
{
	if (cv[i].kind == K_PS2 && !neutrino) return "Falta Neutrino: cópialo a mass0:/neutrino/";
	if (cv[i].kind == K_PS1 && (!exists(POPSTARTER) || !exists("mass0:/POPS/POPS_IOX.PAK")))
		return "Faltan POPSTARTER.ELF y POPS_IOX.PAK en mass0:/POPS/";
	if (cv[i].kind == K_DISC && (cv[i].disc == D_NONE || cv[i].disc == D_READING)) return "No hay un disco listo";
	if (cv[i].kind == K_DISC && (cv[i].disc == D_OTHER || !*cv[i].boot)) return "Este disco no se puede iniciar";
	return NULL;
}

static void launch(int i) // per kind (phase 11): Neutrino, POPStarter, an app ELF, or the BIOS for discs
{
	static const char *how[4] = {"INICIANDO CON NEUTRINO", "INICIANDO CON POPSTARTER", "INICIANDO APLICACIÓN",
	                             "INICIANDO DISCO"};
	for (int t = 0; t < 40; t++) { // let the confirm sound play while the screen fades to the entry's name
		gfx_begin();
		gfx_alpha(0x80);
		gfx_rect(0, 0, GFX_W, GFX_H, 0);
		gfx_alpha((int)(0x80 * span(t, 0, 20)));
		gfx_orb(640 - 28, 250, 56);
		gfx_text_chrome(&gfx_font_title, (GFX_W - gfx_text_width(&gfx_font_title, cv[i].title)) / 2, 340, cv[i].title);
		gfx_tracking(3);
		text_c(&gfx_font_mono, 400, how[(int)cv[i].kind], LABEL);
		gfx_tracking(0);
		gfx_end();
		gfx_flip();
	}
	if (disc_tid >= 0) TerminateThread(disc_tid); // no libcdvd call left half-way under the next program
	if (cv[i].kind == K_DISC) { // the BIOS: PS2LOGO checks and runs the BOOT2 path; PS1DRV takes file name + version
		char *a[2] = {cv[i].boot, "???"}; // ponytail: version "???", as the OSD libraries fall back to
		printf("launch: %s %s\n", cv[i].disc == D_PS2 ? "rom0:PS2LOGO" : "rom0:PS1DRV", cv[i].boot);
		gfx_shutdown();
		SifExitRpc();
		LoadExecPS2(cv[i].disc == D_PS2 ? "rom0:PS2LOGO" : "rom0:PS1DRV", cv[i].disc == D_PS2 ? 1 : 2, a);
		return;
	}
	static char file[200], arg0[200];
	if (cv[i].kind != K_PS2) { // loader argv: the file, then the program's argv
		char *argv[2] = {file, arg0};
		if (cv[i].kind == K_PS1) { // POPStarter finds <name>.VCD from an argv[0] of mass:/POPS/XX.<name>.ELF
			char name[160];
			snprintf(name, sizeof(name), "%s", cv[i].path + 5); // after "POPS/"
			name[strlen(name) - 4] = 0;                          // without ".VCD"
			snprintf(file, sizeof(file), "%s", POPSTARTER);
			snprintf(arg0, sizeof(arg0), "mass:/POPS/XX.%s.ELF", name);
		} else snprintf(file, sizeof(file), "mass0:/%s", cv[i].path), snprintf(arg0, sizeof(arg0), "%s", file);
		printf("launch: %s as %s\n", file, arg0);
		gfx_shutdown();
		run_loader(2, argv);
		printf("launch failed\n");
		return;
	}
	static char dvd[200], gsm[24], gc[12] = "-gc=";
	char *argv[6];
	int argc = 0, v = game_video(i), c = game_compat(i), n = 4;
	snprintf(dvd, sizeof(dvd), "-dvd=usb:%s", cv[i].path);
	argv[argc++] = NEUTRINO; // the file to load
	argv[argc++] = NEUTRINO; // Neutrino's argv[0]
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
	int sel = 0, n = 0, idle = 0, overlay = 0, toast = 0, opt = -1, switching = 0, booted = 0;
	if (!source_ok) toast = 300, toast_msg = "Ese origen de juegos aún no está disponible: usando USB";
	float s = 0;
	unsigned prev = 0;
	const char *saved = usb ? "" : "  SIN USB";
	char text[256];

	for (int f = 0;; f++) {
		unsigned b = pad_buttons(), pressed = b & ~prev;
		prev = b;
		if (disc_new) { // the disc thread staged a change: apply it between frames (the GS is done with the old covers)
			void *ob = cv[0].big, *os = cv[0].small, *oh = cv[0].half;
			cv[0] = disc_stage;
			free(ob), free(os), free(oh);
			if (gicon_state[0] == 2) gicon_state[0] = 0, gicon[0] = NULL; // ponytail: the old icon leaks (rare, small)
			disc_new = 0;
		}
		refilter();
		if (nord && pos[sel] < 0) sel = order[0]; // e.g. the disc changed kind under a PS1 filter
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
			// steps per view: carousel ←→ 1; grid ←→ 1, ↑↓ a row; list ↑↓ 1, ←→ 8 (clamped; the edge sound at the ends)
			int step = 0;
			if (pressed & PAD_RIGHT) step = view == V_LIST ? 8 : 1;
			if (pressed & PAD_LEFT) step = view == V_LIST ? -8 : -1;
			if (pressed & PAD_DOWN && view != V_CAROUSEL) step = view == V_GRID ? GCOLS : 1;
			if (pressed & PAD_UP && view != V_CAROUSEL) step = view == V_GRID ? -GCOLS : -1;
			if (step && nord) { // through the filter's order
				int p = pos[sel] + step < 0 ? 0 : pos[sel] + step > nord - 1 ? nord - 1 : pos[sel] + step;
				if (order[p] != sel) sel = order[p], play(S_MOVE, 70); else play(S_EDGE, 80);
			}
			int fstep = (pressed & PAD_R1 ? 1 : 0) - (pressed & PAD_L1 ? 1 : 0);
			if (fstep) { // next / previous filter that shows something; the selection stays if it still shows
				int fl = filter;
				do fl = (fl + fstep + F_N) % F_N; while (fl != F_ALL && !filter_has(fl));
				filter = fl;
				refilter();
				if (pos[sel] < 0) { // the nearest shown entry in list order
					int best = order[0];
					for (int p = 0; p < nord; p++) if (abs(order[p] - sel) < abs(best - sel)) best = order[p];
					sel = best;
				}
				for (int i = 0; i < ncv; i++) wave[i] = pos[i] < 0 ? 0 : (abs(pos[i] - pos[sel]) < 10 ? abs(pos[i] - pos[sel]) : 10) * 2;
				switching = 50, vlabel = 70, label_filter = 1;
				play(S_PANEL, 70);
			}
			if (pressed & PAD_SQUARE && ncv) { // next view: every cover flies to its new place, in a wave from sel
				view = (view + 1) % V_N;
				for (int i = 0; i < ncv; i++) wave[i] = pos[i] < 0 ? 0 : (abs(pos[i] - pos[sel]) < 10 ? abs(pos[i] - pos[sel]) : 10) * 2;
				switching = 50, vlabel = 70, label_filter = 0, state_dirty = 1;
				if (icon_sema >= 0) SignalSema(icon_sema);
				play(S_PANEL, 70);
			}
			if (pressed & PAD_SELECT) overlay ^= 1, play(S_PANEL, 70);
			if (pressed & PAD_TRIANGLE && ncv && cv[sel].kind == K_PS2) opt = 0, play(S_PANEL, 70);
			if (pressed & PAD_CROSS && ncv) {
				const char *why = launch_problem(sel);
				play(why ? S_EDGE : S_CONFIRM, 85);
				if (!why) launch(sel); // does not return when the program loads
				toast = 150, toast_msg = why ? why : "No se pudo iniciar";
			}
		}
		if (toast > 0) toast--;
		idle = b ? 0 : idle + 1;
		if (overlay && idle > 300 && nord) sel = order[f / 90 % nord]; // overlay + 5 s idle: hands-free gate run
		int ps = pos[sel] < 0 ? 0 : pos[sel]; // the selection's place in the filter
		s += (ps - s) * 0.2f;
		if (fabsf(ps - s) < 0.002f) s = ps; // snap: rest positions are whole pixels
		float k = 1 - 3 * fabsf(ps - s);
		if (ps / GCOLS < gtop) gtop = ps / GCOLS;            // grid: the selection's row stays one of the two shown
		if (ps / GCOLS > gtop + 1) gtop = ps / GCOLS - 1;
		ltop = ps - 4 > nord - LVIS ? nord - LVIS : ps - 4;  // list: the selection 4 rows down when it can be
		if (ltop < 0) ltop = 0;
		if (!booted) { // first home frame: covers in place but transparent, fading in as a wave from the selection
			booted = 1;
			for (int i = 0; i < ncv; i++) shown_box[i] = target(i, sel), shown_box[i].a = 0,
			                              wave[i] = (abs(i - sel) < 12 ? abs(i - sel) : 12) * 2;
			lscroll = ltop, pill_y = LY0 + (ps - ltop) * LROW;
		}
		animate(sel, switching > 0 ? 0.16f : 0.2f);
		if (switching > 0) switching--;
		if (vlabel > 0) vlabel--;
		ease_to(&lscroll, ltop, 0.2f, 0.002f);
		ease_to(&pill_y, LY0 + (ps - lscroll) * LROW, 0.35f, 0.3f);
		ease_to(&lpres, view == V_LIST, 0.12f, 0.004f);

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
