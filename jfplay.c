// Jellyfin playback spike (phase 15, go / no-go): can the PS2 play what Jellyfin transcodes for it?
// - mass0:/orbit/config.ini [jellyfin] servidor / usuario / clave, [red] as the launcher; network via net.c (lwIP)
// - the movies of every library are listed; X plays one, from the start (△: from where Jellyfin left it)
// - stream: jf_stream (MPEG-2 video + MP2 audio in an MPEG program stream, chunked HTTP) -> mpegps.c
//   video: ring -> IPU through ps2sdk libmpeg (RGBA32 pictures in 16x16 macroblocks) -> gfx_mb32 (DMA to the GS
//          in CT32 bands, bilinear, fitted into 1280x720; the first version converted to CT16 on the EE and showed
//          only ~8 of 24 pictures a second in PCSX2)
//   audio: ring -> libmad (Layer II) -> audsrv PCM; the audio clock drives the pictures (late ones are not drawn)
// - O / START stops; every 5 s a line goes to mass0:/jfplay.txt (and the EE serial port) and to the screen: pictures decoded / shown / late,
//   network KB/s, ring levels, audio-video gap. Jellyfin gets start / stop reports ("continue watching").
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
#include <sio.h>
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
	sio_putsn(b); // the EE serial port too: PCSX2 logs it, and it works when the USB does not
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
static volatile int net_eof, stop, net_kb, net_at, audio_at, net_total_kb, main_at, pics; // *_at: where each thread is (stall reports)
static volatile long long a_pts0 = -1, v_pts_q[256]; // first audio PTS; video PTS by ring offset >> 11 (2 KB units)
static volatile unsigned v_pts_off[256];
static volatile int v_pts_n;

static void on_video(void *c, const u8 *p, int n, long long t)
{
	(void)c;
	// drop sequence_end_code (00 00 01 B7): libmpeg hung on it at the end of a stream instead of returning 0
	// (PCSX2); without it the data simply runs out, which ends MPEG_Picture cleanly. ponytail: a code split
	// across two PES packets passes
	for (int i = 0; i + 4 <= n; i++)
		if (!p[i] && !p[i + 1] && p[i + 2] == 1 && p[i + 3] == 0xB7) {
			if (i) on_video(c, p, i, t);
			p += i + 4, n -= i + 4, i = -1, t = -1;
		}
	if (t >= 0) { int k = v_pts_n & 255; v_pts_off[k] = vring.wr, v_pts_q[k] = t; v_pts_n++; }
	net_at = 2;
	ring_put(&vring, p, n, &stop);
	net_at = 1;
}
static void on_audio(void *c, const u8 *p, int n, long long t)
{
	(void)c;
	if (t >= 0 && a_pts0 < 0) a_pts0 = t;
	net_at = 3;
	ring_put(&aring, p, n, &stop);
	net_at = 1;
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
	while (!stop && (net_at = 0, n = http_read(&hs, buf, sizeof(buf))) > 0) {
		net_at = 1;
		if (ps_feed(&dmx, buf, n) < 0) { logf_("jfplay: not a program stream\n"); break; }
		total += n;
		net_total_kb = (int)(total >> 10);
		int ms = (int)((long long)(clock() - c0) * 1000 / CLOCKS_PER_SEC); // 64-bit: x1000 overflowed clock_t (96 MB/s shown)
		if (ms > 0) net_kb = (int)(total * 1000 / 1024 / ms);
	}
	net_eof = 1;
	ExitThread();
}

// ---- audio: MP2 -> PCM -> audsrv; clock = first PTS + samples played ----
static volatile long long a_sent;   // samples handed to audsrv
static volatile int a_rate = 48000, a_on;
// audsrv's RPC client is not thread-safe (the main thread asking audsrv_queued while this thread played made the
// spike exit to the browser in PCSX2): only the audio thread calls it, and publishes samples played + when
static volatile long long a_played;
static volatile clock_t a_when;
static volatile int a_queued;
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
		int frames = 0;
		for (;; frames++) {
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
			audio_at = 2;
			audsrv_wait_audio(sy.pcm.length * 4);
			audio_at = 3;
			audsrv_play_audio((char *)pcm, sy.pcm.length * 4);
			a_sent += sy.pcm.length;
			a_queued = audsrv_queued();
			a_when = clock(), a_played = a_sent - a_queued / 4;
			audio_at = 1;
		}
		int used = st.next_frame ? st.next_frame - in : 0; // keep the partial frame
		memmove(in, in + used, have - used);
		have -= used;
		if (have >= (int)sizeof(in) - MAD_BUFFER_GUARD) have = 0; // garbage that never decodes
		if (!frames && !k) { // half a frame and nothing new: wait for the network (this thread outranks lwIP; a
			if (net_eof) break; // spin here starved it and froze playback after 4 s in PCSX2)
			usleep(5000);
		}
	}
	mad_synth_finish(&sy); // an empty macro in libmad: separate statements
	mad_frame_finish(&fr);
	mad_stream_finish(&st);
	ExitThread();
}

static long long clock90(long long wall0) // the presentation clock, 90 kHz: audio if it plays, else the wall clock
{
	if (a_on && a_pts0 >= 0 && a_when) { // the last published position, moved on by the time since (at most the queue)
		long long since = (long long)(clock() - a_when) * a_rate / CLOCKS_PER_SEC, played = a_played;
		played += since < a_queued / 4 ? since : a_queued / 4;
		return a_pts0 + (played < 0 ? 0 : played) * 90000 / a_rate;
	}
	return wall0 + (long long)clock() * 90000 / CLOCKS_PER_SEC;
}

static void stall(const char *where) // what every thread is doing, when the video waits
{
	logf_("jfplay: stall in %s: video ring %d KB, audio ring %d KB, net at %d (0 recv, 1 demux, 2 video put, 3 audio "
	      "put), audio at %d (1 decode, 2 wait, 3 play), sent %lld samples, audsrv queued %d, eof %d, received %d KB\n", where,
	      ring_used(&vring) >> 10, ring_used(&aring) >> 10, net_at, audio_at, a_sent, a_queued, net_eof, net_total_kb);
}

static u8 dog_stack[0x4000] __attribute__((aligned(16)));
static void watchdog(void *arg) // every 3 s while playing: where the decode loop is, so a hang leaves a trace
{
	(void)arg;
	int last = -1;
	while (!stop) {
		sleep(3);
		if (pics == last) { char w[32]; snprintf(w, sizeof(w), "main at %d", main_at); stall(w); }
		last = pics;
	}
	ExitThread();
}

// ---- video: ring -> IPU in 2 KB DMA blocks (libmpeg pulls them) ----
static s64 cur_pts;
static long long v_end_pts; // 90 kHz PTS where the movie ends (first PTS + runtime), 0 = unknown
static int pending;    // bytes of the block the IPU DMA may still be reading
static unsigned pressed(void);
static int video_data(void *u)
{
	(void)u;
	dma_channel_wait(DMA_CHANNEL_toIPU, 0);
	vring.rd += pending, pending = 0;
	for (clock_t t0 = clock(); ring_used(&vring) < 2048 && !(net_eof && ring_used(&vring) > 0) && !stop;) {
		if (net_eof && !ring_used(&vring)) return 0;
		if (clock() - t0 > 3 * CLOCKS_PER_SEC) stall("video data"), t0 = clock(); // every 3 s while it lasts
		if (pressed() & (PAD_CIRCLE | PAD_START)) stop = 1; // O works while the network is late too
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
static void *video_init(void *u, MPEGSequenceInfo *si)
{
	(void)u;
	static int pic_size;
	int mbw = (si->m_Width + 15) >> 4, mbh = (si->m_Height + 15) >> 4;
	seq = si;
	if (pic_size < mbw * mbh * 1024) free(pic), pic = memalign(64, pic_size = mbw * mbh * 1024); // ponytail: no NULL check, libmpeg has none either
	logf_("jfplay: sequence %dx%d, %d ms per picture, profile %d level %d\n", si->m_Width, si->m_Height,
	      si->m_MSPerFrame, si->m_Profile, si->m_Level);
	return pic;
}

static void draw_picture(int w, int h, const char *osd)
{
	int dh = GFX_H, dw = w * GFX_H / h; // square pixels (Jellyfin scales to them), fitted to 720 lines
	if (dw > GFX_W) dw = GFX_W, dh = h * GFX_W / w;
	int x = (GFX_W - dw) / 2, y = (GFX_H - dh) / 2;
	gfx_begin();
	gfx_alpha(0x80);
	gfx_rect(0, 0, GFX_W, GFX_H, 0);
	gfx_mb32(pic, w, h, x, y, dw, dh); // straight from the IPU's output by DMA
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
	static u8 vbuf[4 << 20] __attribute__((aligned(64))), abuf[1 << 20] __attribute__((aligned(64))); // audio: ~30 s of MP2 at 256 kbit/s
	char osd[160];
	vring = (ring){vbuf, sizeof(vbuf), 0, 0}, aring = (ring){abuf, sizeof(abuf), 0, 0};
	net_eof = stop = net_kb = pics = main_at = 0, a_pts0 = -1, v_end_pts = 0, v_pts_n = 0, a_sent = 0, a_on = 0, a_played = 0, a_when = 0, a_queued = 0, pending = 0, cur_pts = 0;
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
	// ~1 MB of video first (10 s max), or until the audio ring is nearly full: the audio thread starts after this,
	// and a 1.2 Mbit/s video with 256 kbit/s audio filled a 256 KB audio ring first and stalled the download
	for (int t = 0; t < 600 && ring_used(&vring) < (1 << 20) && ring_used(&aring) < aring.size * 3 / 4 && !net_eof; t++) {
		if (t % 120 == 119) stall("prebuffer");
		snprintf(osd, sizeof(osd), "CARGANDO  %d KB  %d KB/s", ring_used(&vring) >> 10, net_kb);
		screen(it->name, osd);
	}
	StartThread(atid, NULL);
	ee_thread_t wt = {.func = watchdog, .stack = dog_stack, .stack_size = sizeof(dog_stack), .gp_reg = &_gp, .initial_priority = 0x20};
	int wtid = CreateThread(&wt);
	StartThread(wtid, NULL);
	dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
	MPEG_Initialize(video_data, NULL, video_init, NULL, &cur_pts);
	long long wall0 = 0, last_pts = -1, gap = 0, first_pts = -1;
	int decoded = 0, shown = 0, late = 0, win_dec = 0, win_shown = 0, show_osd = 1;
	clock_t w0 = clock(), last_report = clock();
	for (;;) {
		unsigned p = pressed();
		if (p & (PAD_CIRCLE | PAD_START)) { stop = 1; break; }
		if (p & PAD_SELECT) show_osd ^= 1;
		s64 t;
		main_at = 1; // 1 decode, 2 draw, 3 wait, 4 report
		if (!MPEG_Picture(pic, &t)) { logf_("jfplay: end of video (eof %d)\n", seq ? seq->m_fEOF : -1); break; }
		decoded++, win_dec++, pics++;
		main_at = 3;
		int ms = seq->m_MSPerFrame > 0 ? seq->m_MSPerFrame : 33;
		if (t <= last_pts) t = last_pts + ms * 90; // pictures without their own PTS (and B-frame order)
		last_pts = t;
		if (first_pts < 0) first_pts = t, v_end_pts = it->ticks > 0 ? t + (it->ticks - start) * 9 / 1000 - 45000 : 0; // 0.5 s early
		if (decoded == 1) wall0 = t - (long long)clock() * 90000 / CLOCKS_PER_SEC;
		long long now = clock90(wall0);
		gap = now - t;
		if (gap > 2 * ms * 90) { late++; continue; } // behind the audio by more than two pictures: not drawn
		clock_t wait0 = clock(); // until its time (within 8 ms); 2 s at most, in case the audio clock stalls
		while ((now = clock90(wall0)) < t - 90 * 8 && !stop && clock() - wait0 < 2 * CLOCKS_PER_SEC) usleep(2000);
		if (clock() - wait0 >= 2 * CLOCKS_PER_SEC) logf_("jfplay: waited 2 s for picture %lld (clock %lld)\n", t, now), stall("picture wait");
		int secs = (int)((clock() - w0) / CLOCKS_PER_SEC);
		snprintf(osd, sizeof(osd), "%02d:%02d  %dx%d  dec %d  vis %d  tarde %d  red %d KB/s  buf %d KB  av %+d ms",
		         secs / 60, secs % 60, seq->m_Width, seq->m_Height, decoded, shown, late, net_kb, ring_used(&vring) >> 10,
		         (int)(gap / 90));
		main_at = 2;
		draw_picture(seq->m_Width, seq->m_Height, show_osd ? osd : NULL);
		main_at = 4;
		shown++, win_shown++;
		// the tail: libmpeg never returned after the last picture (12 KB unread; the picture stayed, O did nothing),
		// and ending its data early hung it too. So the loop ends between pictures, half a second before the runtime
		// Jellyfin gave. ponytail: the last 0.5 s of a movie is not shown
		if (v_end_pts > 0 && t >= v_end_pts) { logf_("jfplay: end of the movie\n"); break; }
		if (clock() - last_report > 5 * CLOCKS_PER_SEC) { // gate numbers, every 5 s
			float sec = (float)(clock() - last_report) / CLOCKS_PER_SEC;
			logf_("jfplay: %.1f s: %.1f decoded/s, %.1f shown/s (stream %.1f/s), late %d, net %d KB/s, video ring %d KB, "
			      "audio queued %d B, a-v %+d ms\n", sec, win_dec / sec, win_shown / sec, 1000.f / ms, late, net_kb,
			      ring_used(&vring) >> 10, a_queued, (int)(gap / 90));
			win_dec = win_shown = 0, last_report = clock();
			// no /Progress here: a second connection from this thread mid-stream reset the spike after ~5 s in PCSX2
			// (DEV9 sockets) and blocks the decode loop for a round trip anyway; start + /Stopped carry the position
		}
	}
	stop = 1;
	clock_t e0 = clock();
	MPEG_Destroy();
	logf_("jfplay: MPEG_Destroy %d ms\n", (int)((long long)(clock() - e0) * 1000 / CLOCKS_PER_SEC));
	http_close(&hs);    // unblocks the network thread's recv
	usleep(100000);     // the threads see stop and leave; whatever is still blocked is ended below
	TerminateThread(ntid), TerminateThread(atid), TerminateThread(wtid);
	audsrv_stop_audio(); // only once the audio thread is gone (audsrv is not thread-safe)
	long long pos = start + (last_pts > 0 && a_pts0 >= 0 ? (last_pts - a_pts0) * 1000 / 9 : 0);
	e0 = clock();
	jf_report(c, "/Stopped", it->id, pos);
	logf_("jfplay: stop report %d ms\n", (int)((long long)(clock() - e0) * 1000 / CLOCKS_PER_SEC));
	logf_("jfplay: stopped: %d decoded, %d shown, %d late, demux skipped %d bytes\n", decoded, shown, late, dmx.skipped);
	DeleteThread(ntid), DeleteThread(atid), DeleteThread(wtid);
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
			char yr[8] = "";
			if (movies[i].year && !strstr(movies[i].name, "(")) snprintf(yr, sizeof(yr), " (%d)", movies[i].year);
			snprintf(msg, sizeof(msg), "%s%s%s  %lld min%s", i == sel ? "> " : "  ", movies[i].name, yr,
			         movies[i].ticks / 600000000, movies[i].resume ? "  [a medias]" : "");
			gfx_text(&gfx_font_ui, 64, 150 + (i - top) * 40, msg, i == sel ? ICE : TEXT);
		}
		gfx_end();
		gfx_flip();
	}
	return 0;
}
