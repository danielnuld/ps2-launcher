// Jellyfin playback spike (phase 15, go / no-go): can the PS2 play what Jellyfin transcodes for it?
// - mass0:/orbit/config.ini [jellyfin] servidor / usuario / clave, [red] as the launcher; network via net.c (lwIP)
// - the movies of every library are listed; X plays one, from the start (△: from where Jellyfin left it)
// - stream: jf_stream (MPEG-2 video + MP2 audio in an MPEG program stream, chunked HTTP) -> mpegps.c
//   video: ring -> IPU through ps2sdk libmpeg (RGBA32 pictures in 16x16 macroblocks) -> CT16 strips on the EE ->
//          gfx_image (bilinear, fitted into 1280x720)
//   audio: ring -> libmad (Layer II) -> audsrv PCM; the audio clock drives the pictures (late ones are not drawn)
// - O / START stops; every 5 s a line goes to mass0:/jfplay.txt and to the screen: pictures decoded / shown / late,
//   network KB/s, ring levels, audio-video gap. Jellyfin gets start / progress / stop reports ("continue watching").
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <unistd.h>
#include <kernel.h>
#include <tamtypes.h>
#include <dma.h>
#include <libpad.h>
#include <libmpeg.h>
#include <mad.h>
#include <audsrv.h>
#include "gfx.h"
#include "iop.h"
#include "ini.h"
#include "net.h"
#include "jellyfin.h"
#include "mpegps.h"

#define TEXT 0xEEF3FF
#define TEXT2 0xA9B6D3
#define ICE 0x7FE7FF
#define LOG "mass0:/jfplay.txt"
#define VBR 3000000 // bits/s asked of Jellyfin; ~2.9 Mbit/s measured with the test movie (docs/phase15-results.md)

static void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void logf_(const char *fmt, ...)
{
	char b[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);
	va_end(ap);
	printf("%s", b);
	FILE *f = fopen(LOG, "a");
	if (f) fputs(b, f), fclose(f);
}

// ---- rings: one writer (network thread), one reader; sizes are powers of two ----
typedef struct { u8 *b; int size; volatile unsigned rd, wr; } ring; // free-running counters: differences wrap safely
static int ring_used(ring *r) { return (int)(r->wr - r->rd); }
static int ring_put(ring *r, const u8 *p, int n, volatile int *stop) // blocks while full
{
	while (n > 0 && !*stop) {
		int room = r->size - ring_used(r);
		if (!room) { usleep(2000); continue; }
		int k = n < room ? n : room, o = r->wr & (r->size - 1), first = r->size - o < k ? r->size - o : k;
		memcpy(r->b + o, p, first), memcpy(r->b, p + first, k - first);
		__asm__ volatile("" ::: "memory");
		r->wr += k, p += k, n -= k;
	}
	return !*stop;
}

static ring vring, aring;
static volatile int net_eof, stop, net_kb;
static volatile long long a_pts0 = -1, v_pts_q[256]; // first audio PTS; video PTS by ring offset >> 11 (2 KB units)
static volatile unsigned v_pts_off[256];
static volatile int v_pts_n;

static void on_video(void *c, const u8 *p, int n, long long t)
{
	(void)c;
	if (t >= 0) { int k = v_pts_n & 255; v_pts_off[k] = vring.wr, v_pts_q[k] = t; v_pts_n++; }
	ring_put(&vring, p, n, &stop);
}
static void on_audio(void *c, const u8 *p, int n, long long t)
{
	(void)c;
	if (t >= 0 && a_pts0 < 0) a_pts0 = t;
	ring_put(&aring, p, n, &stop);
}

static http_stream hs;
static ps_demux dmx;
static u8 net_stack[0x8000] __attribute__((aligned(16)));
static void net_thread(void *arg) // HTTP -> demuxer -> rings
{
	(void)arg;
	static u8 buf[32 << 10];
	int n;
	clock_t c0 = clock();
	long long total = 0;
	while (!stop && (n = http_read(&hs, buf, sizeof(buf))) > 0) {
		if (ps_feed(&dmx, buf, n) < 0) { logf_("jfplay: not a program stream\n"); break; }
		total += n;
		int ms = (int)((clock() - c0) * 1000 / CLOCKS_PER_SEC);
		if (ms > 0) net_kb = (int)(total * 1000 / 1024 / ms);
	}
	net_eof = 1;
	ExitThread();
}

// ---- audio: MP2 -> PCM -> audsrv; clock = first PTS + samples played ----
static volatile long long a_sent; // samples handed to audsrv
static volatile int a_rate = 48000, a_on;
static u8 audio_stack[0x10000] __attribute__((aligned(16)));
static s16 clamp16(mad_fixed_t s)
{
	s += 1L << (MAD_F_FRACBITS - 16);
	if (s >= MAD_F_ONE) s = MAD_F_ONE - 1;
	if (s < -MAD_F_ONE) s = -MAD_F_ONE;
	return s >> (MAD_F_FRACBITS + 1 - 16);
}
static void audio_thread(void *arg)
{
	(void)arg;
	static u8 in[8192 + MAD_BUFFER_GUARD];
	static s16 pcm[1152 * 2];
	struct mad_stream st;
	struct mad_frame fr;
	struct mad_synth sy;
	int have = 0;
	mad_stream_init(&st), mad_frame_init(&fr), mad_synth_init(&sy);
	while (!stop) {
		int k = ring_used(&aring); // top up the decoder's input from the ring
		if (k > (int)sizeof(in) - MAD_BUFFER_GUARD - have) k = sizeof(in) - MAD_BUFFER_GUARD - have;
		for (int i = 0; i < k; i++) in[have + i] = aring.b[(aring.rd + i) & (aring.size - 1)];
		aring.rd += k, have += k;
		if (!have) { if (net_eof) break; usleep(5000); continue; }
		mad_stream_buffer(&st, in, have);
		for (;;) {
			if (mad_frame_decode(&fr, &st)) {
				if (st.error == MAD_ERROR_BUFLEN || !MAD_RECOVERABLE(st.error)) break;
				continue; // a damaged frame: skip it
			}
			mad_synth_frame(&sy, &fr);
			if (!a_on) {
				struct audsrv_fmt_t f = {sy.pcm.samplerate, 16, 2};
				a_rate = sy.pcm.samplerate;
				a_on = audsrv_set_format(&f) == 0;
				audsrv_set_volume(MAX_VOLUME);
			}
			for (int i = 0; i < sy.pcm.length; i++)
				pcm[2 * i] = clamp16(sy.pcm.samples[0][i]), pcm[2 * i + 1] = clamp16(sy.pcm.samples[sy.pcm.channels > 1][i]);
			audsrv_wait_audio(sy.pcm.length * 4);
			audsrv_play_audio((char *)pcm, sy.pcm.length * 4);
			a_sent += sy.pcm.length;
		}
		int used = st.next_frame ? st.next_frame - in : 0; // keep the partial frame
		memmove(in, in + used, have - used);
		have -= used;
		if (have >= (int)sizeof(in) - MAD_BUFFER_GUARD) have = 0; // garbage that never decodes
	}
	mad_synth_finish(&sy); // an empty macro in libmad: separate statements
	mad_frame_finish(&fr);
	mad_stream_finish(&st);
	ExitThread();
}

static long long clock90(long long wall0) // the presentation clock, 90 kHz: audio if it plays, else the wall clock
{
	if (a_on && a_pts0 >= 0) {
		long long played = a_sent - audsrv_queued() / 4;
		return a_pts0 + (played < 0 ? 0 : played) * 90000 / a_rate;
	}
	return wall0 + (long long)clock() * 90000 / CLOCKS_PER_SEC;
}

// ---- video: ring -> IPU in 2 KB DMA blocks (libmpeg pulls them) ----
static s64 cur_pts;
static int pending;    // bytes of the block the IPU DMA may still be reading
static int video_data(void *u)
{
	(void)u;
	dma_channel_wait(DMA_CHANNEL_toIPU, 0);
	vring.rd += pending, pending = 0;
	while (ring_used(&vring) < 2048 && !(net_eof && ring_used(&vring) > 0) && !stop) {
		if (net_eof && !ring_used(&vring)) return 0;
		usleep(1000);
	}
	if (stop || !ring_used(&vring)) return 0;
	int o = vring.rd & (vring.size - 1), n = ring_used(&vring) < 2048 ? (ring_used(&vring) + 15) & ~15 : 2048;
	for (int k = v_pts_n - 1; k >= 0 && k >= v_pts_n - 256; k--) // the newest PES start inside this block
		if ((int)(v_pts_off[k & 255] - vring.rd) >= 0 && (int)(v_pts_off[k & 255] - vring.rd) < n) { cur_pts = v_pts_q[k & 255]; break; }
	SyncDCache(vring.b + o, vring.b + o + n);
	dma_channel_send_normal(DMA_CHANNEL_toIPU, vring.b + o, n >> 4, 0, 0);
	pending = n;
	return 1;
}

static MPEGSequenceInfo *seq;
static u8 *pic;               // decoded picture, RGBA32 in macroblocks
static u16 *strip[3];         // CT16, 256 / 256 / 128 wide
static void *video_init(void *u, MPEGSequenceInfo *si)
{
	(void)u;
	static int pic_size;
	int mbw = (si->m_Width + 15) >> 4, mbh = (si->m_Height + 15) >> 4;
	seq = si;
	if (pic_size < mbw * mbh * 1024) free(pic), pic = memalign(64, pic_size = mbw * mbh * 1024); // ponytail: no NULL check, libmpeg has none either
	for (int s = 0; s < 3 && !strip[s]; s++) strip[s] = memalign(64, 256 * 576 * 2);
	logf_("jfplay: sequence %dx%d, %d ms per picture, profile %d level %d\n", si->m_Width, si->m_Height,
	      si->m_MSPerFrame, si->m_Profile, si->m_Level);
	return pic;
}

static void to_ct16(int w, int h) // macroblock RGBA32 -> three CT16 strips (gfx_image takes w <= 256)
{
	int mbw = (w + 15) >> 4;
	InvalidDCache(pic, pic + mbw * ((h + 15) >> 4) * 1024);
	for (int s = 0; s * 256 < w; s++) {
		int x0 = s * 256, sw = w - x0 < 256 ? w - x0 : 256;
		u16 *o = strip[s];
		for (int y = 0; y < h; y++) {
			const u32 *row = (const u32 *)(pic + ((y >> 4) * mbw) * 1024) + (y & 15) * 16;
			for (int x = x0; x < x0 + sw; x++) {
				u32 c = row[(x >> 4) * 256 + (x & 15)];
				*o++ = (c >> 3 & 0x1F) | (c >> 6 & 0x3E0) | (c >> 9 & 0x7C00) | 0x8000;
			}
		}
		SyncDCache(strip[s], (u8 *)strip[s] + sw * h * 2);
	}
}

static void draw_picture(int w, int h, const char *osd)
{
	int dh = GFX_H, dw = w * GFX_H / h; // square pixels (Jellyfin scales to them), fitted to 720 lines
	if (dw > GFX_W) dw = GFX_W, dh = h * GFX_W / w;
	int x = (GFX_W - dw) / 2, y = (GFX_H - dh) / 2;
	gfx_begin();
	gfx_alpha(0x80);
	gfx_rect(0, 0, GFX_W, GFX_H, 0);
	for (int s = 0; s * 256 < w; s++) {
		int sw = w - s * 256 < 256 ? w - s * 256 : 256;
		gfx_image(strip[s], sw, h, x + s * 256 * dw / w, y, sw * dw / w, dh);
	}
	if (osd) {
		gfx_alpha(0x50);
		gfx_rect(40, 620, 1200, 64, 0);
		gfx_alpha(0x80);
		gfx_text(&gfx_font_mono, 56, 632, osd, TEXT);
	}
	gfx_end();
	gfx_flip();
}

static void screen(const char *title, const char *body)
{
	gfx_begin();
	gfx_alpha(0x80);
	gfx_rect(0, 0, GFX_W, GFX_H, 0x04060E);
	gfx_text(&gfx_font_title, 64, 48, title, TEXT);
	gfx_text(&gfx_font_mono, 64, 120, body, TEXT2);
	gfx_end();
	gfx_flip();
}

static unsigned pressed(void)
{
	static unsigned prev;
	unsigned b = pad_buttons(), p = b & ~prev;
	prev = b;
	return p;
}

static void play(jf_conn *c, const jf_item *it, long long start)
{
	static u8 vbuf[4 << 20] __attribute__((aligned(64))), abuf[256 << 10] __attribute__((aligned(64)));
	char osd[160];
	vring = (ring){vbuf, sizeof(vbuf), 0, 0}, aring = (ring){abuf, sizeof(abuf), 0, 0};
	net_eof = stop = net_kb = 0, a_pts0 = -1, v_pts_n = 0, a_sent = 0, a_on = 0, pending = 0, cur_pts = 0;
	ps_init(&dmx, on_video, on_audio, NULL);
	int st = jf_stream(c, &hs, it->id, start, VBR);
	logf_("jfplay: %s (%s) from %lld s: HTTP %d\n", it->name, it->id, start / 10000000, st);
	if (st != 200) { snprintf(osd, sizeof(osd), "HTTP %d", st); screen("No se pudo abrir el video", osd); sleep(3); return; }
	jf_report(c, "", it->id, start);
	extern void *_gp;
	ee_thread_t nt = {.func = net_thread, .stack = net_stack, .stack_size = sizeof(net_stack), .gp_reg = &_gp, .initial_priority = 0x30};
	ee_thread_t at = {.func = audio_thread, .stack = audio_stack, .stack_size = sizeof(audio_stack), .gp_reg = &_gp, .initial_priority = 0x28};
	int ntid = CreateThread(&nt), atid = CreateThread(&at);
	StartThread(ntid, NULL);
	for (int t = 0; t < 600 && ring_used(&vring) < (1 << 20) && !net_eof; t++) { // ~1 MB of video first (10 s max)
		snprintf(osd, sizeof(osd), "CARGANDO  %d KB  %d KB/s", ring_used(&vring) >> 10, net_kb);
		screen(it->name, osd);
	}
	StartThread(atid, NULL);
	dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
	MPEG_Initialize(video_data, NULL, video_init, NULL, &cur_pts);
	long long wall0 = 0, last_pts = -1, gap = 0;
	int decoded = 0, shown = 0, late = 0, win_dec = 0, win_shown = 0, show_osd = 1;
	clock_t w0 = clock(), last_report = clock();
	for (;;) {
		unsigned p = pressed();
		if (p & (PAD_CIRCLE | PAD_START)) { stop = 1; break; }
		if (p & PAD_SELECT) show_osd ^= 1;
		s64 t;
		if (!MPEG_Picture(pic, &t)) { logf_("jfplay: end of video (eof %d)\n", seq ? seq->m_fEOF : -1); break; }
		decoded++, win_dec++;
		int ms = seq->m_MSPerFrame > 0 ? seq->m_MSPerFrame : 33;
		if (t <= last_pts) t = last_pts + ms * 90; // pictures without their own PTS (and B-frame order)
		last_pts = t;
		if (decoded == 1) wall0 = t - (long long)clock() * 90000 / CLOCKS_PER_SEC;
		long long now = clock90(wall0);
		gap = now - t;
		if (gap > 2 * ms * 90) { late++; continue; } // behind the audio by more than two pictures: not drawn
		to_ct16(seq->m_Width, seq->m_Height);
		clock_t wait0 = clock(); // until its time (within 8 ms); 2 s at most, in case the audio clock stalls
		while ((now = clock90(wall0)) < t - 90 * 8 && !stop && clock() - wait0 < 2 * CLOCKS_PER_SEC) usleep(2000);
		if (clock() - wait0 >= 2 * CLOCKS_PER_SEC) logf_("jfplay: waited 2 s for picture %lld (clock %lld)\n", t, now);
		int secs = (int)((clock() - w0) / CLOCKS_PER_SEC);
		snprintf(osd, sizeof(osd), "%02d:%02d  %dx%d  dec %d  vis %d  tarde %d  red %d KB/s  buf %d KB  av %+d ms",
		         secs / 60, secs % 60, seq->m_Width, seq->m_Height, decoded, shown, late, net_kb, ring_used(&vring) >> 10,
		         (int)(gap / 90));
		draw_picture(seq->m_Width, seq->m_Height, show_osd ? osd : NULL);
		shown++, win_shown++;
		if (clock() - last_report > 5 * CLOCKS_PER_SEC) { // gate numbers, every 5 s
			float sec = (float)(clock() - last_report) / CLOCKS_PER_SEC;
			logf_("jfplay: %.1f s: %.1f decoded/s, %.1f shown/s (stream %.1f/s), late %d, net %d KB/s, video ring %d KB, "
			      "audio queued %d B, a-v %+d ms\n", sec, win_dec / sec, win_shown / sec, 1000.f / ms, late, net_kb,
			      ring_used(&vring) >> 10, a_on ? audsrv_queued() : -1, (int)(gap / 90));
			win_dec = win_shown = 0, last_report = clock();
			jf_report(c, "/Progress", it->id, start + (last_pts - (a_pts0 >= 0 ? a_pts0 : 0)) * 1000 / 9);
		}
	}
	stop = 1;
	MPEG_Destroy();
	http_close(&hs);    // unblocks the network thread's recv
	audsrv_stop_audio();
	usleep(100000);     // the threads see stop and leave; whatever is still blocked is ended below
	TerminateThread(ntid), TerminateThread(atid);
	long long pos = start + (last_pts > 0 && a_pts0 >= 0 ? (last_pts - a_pts0) * 1000 / 9 : 0);
	jf_report(c, "/Stopped", it->id, pos);
	logf_("jfplay: stopped: %d decoded, %d shown, %d late, demux skipped %d bytes\n", decoded, shown, late, dmx.skipped);
	DeleteThread(ntid), DeleteThread(atid);
}

int main(void)
{
	static ini cfg;
	static jf_item views[16], movies[64];
	char msg[256];
	// priorities: audio 0x28 and the HTTP reader 0x30 block most of the time; lwIP's tcpip thread (0x58) and netman
	// (0x56-0x59, ps2sdk lwipopts.h / netman) must preempt the decode + conversion, which only this thread does
	ChangeThreadPriority(GetThreadId(), 0x60);
	if (!gfx_init()) printf("gfx_init: VRAM pool too small\n");
	screen("Jellyfin", "Cargando módulos...");
	if (!iop_init()) { screen("Jellyfin", "Sin USB (mass0:)"); SleepThread(); }
	audsrv_init();
	ini_load(&cfg, "mass0:/orbit/config.ini");
	logf_("jfplay: start\n");
	screen("Jellyfin", "Conectando a la red...");
	int r = net_up(ini_get(&cfg, "red", "ip", "dhcp"), ini_get(&cfg, "red", "mascara", "255.255.255.0"),
	               ini_get(&cfg, "red", "puerta", ""), ini_get(&cfg, "red", "dns", ""));
	if (r < 0) { snprintf(msg, sizeof(msg), "Error de red %d (cable, DHCP o [red] de config.ini)", r); logf_("%s\n", msg); screen("Jellyfin", msg); SleepThread(); }
	jf_conn c;
	const char *url = ini_get(&cfg, "jellyfin", "servidor", "");
	screen("Jellyfin", url);
	r = jf_login(&c, url, ini_get(&cfg, "jellyfin", "usuario", ""), ini_get(&cfg, "jellyfin", "clave", ""));
	logf_("jfplay: login %s: %d\n", url, r);
	if (r < 0) {
		snprintf(msg, sizeof(msg), "No se pudo entrar a %s (%d)\nRevisa [jellyfin] servidor, usuario y clave en config.ini",
		         *url ? url : "(sin servidor)", r);
		screen("Jellyfin", msg);
		SleepThread();
	}
	int nv = jf_views(&c, views, 16), nm = 0;
	for (int v = 0; v < nv && nm < 64; v++)
		if (!strcmp(views[v].collection, "movies")) {
			int k = jf_items(&c, views[v].id, movies + nm, 64 - nm);
			if (k > 0) nm += k;
		}
	logf_("jfplay: %d libraries, %d movies\n", nv, nm);
	int sel = 0;
	for (;;) {
		unsigned p = pressed();
		if (p & PAD_DOWN && sel < nm - 1) sel++;
		if (p & PAD_UP && sel > 0) sel--;
		if ((p & (PAD_CROSS | PAD_TRIANGLE)) && nm) play(&c, &movies[sel], p & PAD_TRIANGLE ? movies[sel].resume : 0);
		gfx_begin();
		gfx_alpha(0x80);
		gfx_rect(0, 0, GFX_W, GFX_H, 0x04060E);
		gfx_text(&gfx_font_title, 64, 40, "Jellyfin - prueba de video", TEXT);
		snprintf(msg, sizeof(msg), "%d películas.  X reproducir  /  triángulo continuar  /  O durante el video: salir", nm);
		gfx_text(&gfx_font_mono, 64, 100, msg, TEXT2);
		int top = sel > 8 ? sel - 8 : 0;
		for (int i = top; i < nm && i < top + 12; i++) {
			snprintf(msg, sizeof(msg), "%s%s (%d)  %lld min%s", i == sel ? "> " : "  ", movies[i].name, movies[i].year,
			         movies[i].ticks / 600000000, movies[i].resume ? "  [a medias]" : "");
			gfx_text(&gfx_font_ui, 64, 150 + (i - top) * 40, msg, i == sel ? ICE : TEXT);
		}
		gfx_end();
		gfx_flip();
	}
	return 0;
}
