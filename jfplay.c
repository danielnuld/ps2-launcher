// Jellyfin playback spike (phase 15, go / no-go): can the PS2 play what Jellyfin transcodes for it?
// - mass0:/orbit/config.ini [jellyfin] servidor / usuario / clave, [red] as the launcher; network via net.c (lwIP)
// - the movies of every library are listed; X plays one, from the start (△: from where Jellyfin left it)
// - stream: jf_stream (MPEG-2 video + MP2 audio in an MPEG program stream, chunked HTTP) -> mpegps.c
//   video: ring -> IPU through ps2sdk libmpeg (RGBA32 pictures in 16x16 macroblocks) -> gfx_mb32 (DMA to the GS
//          in CT32 bands, bilinear, fitted into 1280x720; the first version converted to CT16 on the EE and showed
//          only ~8 of 24 pictures a second in PCSX2)
//   audio: ring -> libmad (Layer II) -> audsrv PCM; the audio clock drives the pictures (late ones are not drawn)
// - X / START pause, L1 / R1 (or left / right) -10 / +30 s, □ subtitles, △ shows the bar, O back to the list. A pause
//   or a seek ends the stream and the next request starts at the new position (start ticks)
// - every 5 s a line goes to mass0:/jfplay.txt (and the EE serial port), and with SELECT to the screen: pictures
//   decoded / shown / late, network KB/s, ring levels, audio-video gap. Jellyfin gets start / stop reports.
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
#include "cover.h"
#include "ui_data.h"
#include <math.h>

#define TEXT 0xEEF3FF
#define TEXT2 0xA9B6D3
#define LABEL 0x8FA0C4
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
static volatile int a_queued, a_done; // a_done: the audio thread has left (no audsrv call in flight)
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
			if (stop) break;
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
	a_done = 1;
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
		if (pressed() & PAD_CIRCLE) stop = 1; // O works while the network is late too
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

// ---- subtitles (phase 15): text tracks from Jellyfin as SRT, drawn over the picture (not burned in). The track
// follows config.ini [jellyfin] subtitulos (languages in order, "no" = off); □ cycles them while playing. Every
// track is fetched before the stream starts: a download mid-movie (the video ring full, so the stream's socket sat on
// a closed window) never finished on the console and □ froze the player ----
static jf_track tracks[16];
static int ntracks, cur_track = -1, ncues, cue_at;
static char sub_src[33];
static sub_cue *cues;
static struct { char *buf; sub_cue *cues; int n; } subs[16];
static char sub_pref[64] = "spa";
static char toast[112];
static clock_t toast_until;

static const char *lang3(const char *l) // 2-letter codes to Jellyfin's ISO 639-2
{
	static const char *map[][2] = {{"es", "spa"}, {"en", "eng"}, {"pt", "por"}, {"fr", "fre"}, {"it", "ita"},
	                               {"de", "ger"}, {"ja", "jpn"}, {"ko", "kor"}, {"zh", "chi"}};
	for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++)
		if (!strcasecmp(l, map[i][0])) return map[i][1];
	return l;
}

static int sub_pick(void) // the first preferred language present (its default track first); -1 = none / "no"
{
	char w[8];
	const char *p = sub_pref;
	while (*p) {
		int n = 0;
		while (*p == ' ' || *p == ',') p++;
		while (*p && *p != ' ' && *p != ',' && n < 7) w[n++] = *p++;
		w[n] = 0;
		if (!n) break;
		if (!strcasecmp(w, "no")) return -1;
		const char *l = lang3(w);
		int any = -1;
		for (int k = 0; k < ntracks; k++)
			if (!strcasecmp(tracks[k].lang, l) || (!strcasecmp(l, "fre") && !strcasecmp(tracks[k].lang, "fra")) ||
			    (!strcasecmp(l, "ger") && !strcasecmp(tracks[k].lang, "deu"))) {
				if (tracks[k].deflt && !tracks[k].forced) return k;
				if (any < 0 || tracks[any].forced) any = k;
			}
		if (any >= 0) return any;
	}
	return -1;
}

static void plain(char *t) // UTF-8 punctuation the font lacks -> ASCII (curly quotes, dashes, ellipsis)
{
	char *o = t;
	for (unsigned char *r = (unsigned char *)t; *r;) {
		if (r[0] == 0xE2 && r[1] == 0x80) {
			unsigned c = r[2];
			const char *rep = c == 0x98 || c == 0x99 ? "'" : c == 0x9C || c == 0x9D ? "\"" : c == 0x93 || c == 0x94 ? "-" :
			                  c == 0xA6 ? "..." : "";
			while (*rep) *o++ = *rep++;
			r += 3;
		} else *o++ = *r++;
	}
	*o = 0;
}

static void sub_select(int k) // track k (or -1: none), already in memory
{
	cur_track = k, cue_at = 0;
	cues = k >= 0 ? subs[k].cues : NULL, ncues = k >= 0 ? subs[k].n : 0;
}

static void subs_free(void)
{
	for (int k = 0; k < 16; k++) free(subs[k].buf), free(subs[k].cues), subs[k].buf = NULL, subs[k].cues = NULL, subs[k].n = 0;
	ntracks = 0;
	sub_select(-1);
}

static void subs_fetch(jf_conn *c, const jf_item *it) // every text track, each kept at its own size
{
	subs_free();
	ntracks = jf_tracks(c, it->id, sub_src, tracks, 16);
	if (ntracks < 0) ntracks = 0;
	char *b = malloc(512 << 10); // ~2 h of dense SRT
	sub_cue *q = malloc(6000 * sizeof(sub_cue));
	for (int k = 0; k < ntracks && b && q; k++) {
		int n = jf_subtitle(c, it->id, sub_src, tracks[k].index, b, 512 << 10), nc = n > 0 ? srt_parse(b, q, 6000) : 0;
		for (int i = 0; i < nc; i++) plain((char *)q[i].text);
		if (nc && (subs[k].buf = malloc(n + 1)) && (subs[k].cues = malloc(nc * sizeof(sub_cue)))) {
			memcpy(subs[k].buf, b, n + 1); // the cues point into the text: moved with it
			for (int i = 0; i < nc; i++) subs[k].cues[i] = q[i], subs[k].cues[i].text = subs[k].buf + (q[i].text - b);
			subs[k].n = nc;
		}
		logf_("jfplay: subtitles %d (%s): %d bytes, %d cues\n", tracks[k].index, tracks[k].title, n, subs[k].n);
	}
	free(b), free(q);
}

static const char *sub_at(int ms) // the cue on screen at ms (cue_at walks forward; back after a seek)
{
	if (!ncues) return NULL;
	if (cue_at >= ncues || cues[cue_at].start > ms) cue_at = 0;
	while (cue_at < ncues - 1 && cues[cue_at].end <= ms) cue_at++;
	return cues[cue_at].start <= ms && ms < cues[cue_at].end ? cues[cue_at].text : NULL;
}

static void say(const char *m) { snprintf(toast, sizeof(toast), "%s", m), toast_until = clock() + 2 * CLOCKS_PER_SEC; }

static void sub_next(void) // □: the next track that has cues, then off
{
	char m[96];
	if (!ntracks) { say("Sin subtítulos de texto"); return; }
	int k = cur_track;
	do k = k + 1 < ntracks ? k + 1 : -1;
	while (k >= 0 && !subs[k].n);
	sub_select(k);
	if (k < 0) say("Subtítulos: no");
	else snprintf(m, sizeof(m), "Subtítulos: %.70s", tracks[k].title), say(m);
}

// ---- player bar: title, time, progress and the buttons, as the launcher's footer. Shown while paused or loading,
// and for 3 s after a key ----
#define CHROME_T 0xF4F8FF
#define CHROME_B 0xAAB7D2
#define IRIS 0xC08BFF
#define INK 0x0A1026
static const char *hud_title;
static int hud_ms, hud_len_ms, paused, loading, pic_w, pic_h; // pic_w / pic_h: the last picture shown (0: none yet)
static clock_t hud_until;
static void hud_show(void) { hud_until = clock() + 3 * CLOCKS_PER_SEC; }

// a button hint pill, right-aligned at x (launcher.c hint(), with y; action_icon -1 = none). Returns the next x
static int hint(int x, int y, int key_icon, const char *key_text, int action_icon, const char *label)
{
	int lw = gfx_text_width(&gfx_font_ui, label);
	int kw = key_icon >= 0 ? 30 : gfx_text_width(&gfx_font_mono, key_text) + 20, aw = action_icon >= 0 ? 28 : 0;
	int w = 8 + kw + 10 + aw + lw + 18, x0 = x - w;
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
	if (aw) gfx_icon(action_icon, kx + kw + 10, y + 13, ICE);
	gfx_text(&gfx_font_ui, kx + kw + 10 + aw, y + 11, label, TEXT);
	return x0 - 12;
}

static void clock_str(char *o, int n, int ms) // h:mm:ss or m:ss
{
	int s = ms < 0 ? 0 : ms / 1000;
	if (s >= 3600) snprintf(o, n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
	else snprintf(o, n, "%d:%02d", s / 60, s % 60);
}

static void draw_hud(void)
{
	char a[24], b[24], t[64];
	gfx_alpha(0x58);
	gfx_rect(0, 548, GFX_W, GFX_H - 548, 0x04060E);
	gfx_alpha(0x80);
	snprintf(t, sizeof(t), "%.63s", hud_title ? hud_title : "");
	gfx_text(&gfx_font_ui, 64, 562, t, TEXT);
	clock_str(a, sizeof(a), hud_ms), clock_str(b, sizeof(b), hud_len_ms);
	char tm[56];
	snprintf(tm, sizeof(tm), hud_len_ms > 0 ? "%s / %s" : "%s", a, b);
	gfx_text(&gfx_font_mono, GFX_W - 64 - gfx_text_width(&gfx_font_mono, tm), 562, tm, TEXT2);
	gfx_rect(64, 600, GFX_W - 128, 4, 0x3A4570);
	if (hud_len_ms > 0) {
		int f = (int)((long long)(GFX_W - 128) * (hud_ms < 0 ? 0 : hud_ms > hud_len_ms ? hud_len_ms : hud_ms) / hud_len_ms);
		gfx_rect(64, 600, f, 4, ICE);
		gfx_rrect(64 + f - 7, 595, 14, 14, 7, CHROME_T, CHROME_B);
	}
	int x = hint(GFX_W - 64, 636, UI_CIRCLE_14, NULL, -1, "Salir");
	if (ntracks) x = hint(x, 636, UI_SQUARE_14, NULL, -1, "Subtítulos");
	x = hint(x, 636, -1, "R1", -1, "+30 s");
	x = hint(x, 636, -1, "L1", -1, "-10 s");
	hint(x, 636, UI_CROSS_14, NULL, paused ? UI_PLAY_18 : -1, paused ? "Seguir" : "Pausa");
	if (paused || loading) { // a chrome-edged pill, top centre (the launcher's view label)
		const char *s = paused ? "EN PAUSA" : "CARGANDO";
		gfx_tracking(3);
		int w = gfx_text_width(&gfx_font_mono, s) + 40, px = (GFX_W - w) / 2;
		gfx_alpha(0x40);
		gfx_rrect(px - 1, 39, w + 2, 38, 19, ICE, IRIS);
		gfx_alpha(0x80);
		gfx_rrect(px, 40, w, 36, 18, 0x1B2850, 0x0D1530);
		gfx_text(&gfx_font_mono, px + 20, 50, s, TEXT);
		gfx_tracking(0);
	}
}

static void draw_lines(const char *text, int bottom) // centred, a dark band behind each line
{
	char line[200];
	int nl = 1;
	for (const char *p = text; *p; p++) nl += *p == '\n';
	int lh = gfx_font_ui.line_h + 10, y = bottom - nl * lh;
	for (const char *p = text; *p;) {
		int n = strcspn(p, "\n");
		snprintf(line, sizeof(line), "%.*s", n < 199 ? n : 199, p);
		int w = gfx_text_width(&gfx_font_ui, line), x = (GFX_W - w) / 2;
		gfx_alpha(0x50);
		gfx_rrect(x - 14, y - 4, w + 28, lh - 2, 8, 0, 0);
		gfx_alpha(0x80);
		gfx_text(&gfx_font_ui, x + 1, y + 1, line, 0); // shadow
		gfx_text(&gfx_font_ui, x, y, line, 0xFFFFFF);
		y += lh, p += n + (p[n] == '\n');
	}
}

static void draw_picture(int w, int h, const char *osd, const char *sub)
{
	gfx_begin();
	gfx_alpha(0x80);
	gfx_rect(0, 0, GFX_W, GFX_H, 0);
	if (w > 0 && h > 0) {
		int dh = GFX_H, dw = w * GFX_H / h; // square pixels (Jellyfin scales to them), fitted to 720 lines
		if (dw > GFX_W) dw = GFX_W, dh = h * GFX_W / w;
		gfx_mb32(pic, w, h, (GFX_W - dw) / 2, (GFX_H - dh) / 2, dw, dh); // straight from the IPU's output by DMA
	}
	int hud = paused || loading || clock() < hud_until;
	if (osd) { // SELECT: the gate numbers, at the top
		gfx_alpha(0x50);
		gfx_rect(40, 96, 1200, 40, 0);
		gfx_alpha(0x80);
		gfx_text(&gfx_font_mono, 56, 104, osd, TEXT);
	}
	if (hud) draw_hud();
	if (sub) draw_lines(sub, hud ? 540 : 690);
	if (clock() < toast_until) { // track changes
		int tw = gfx_text_width(&gfx_font_ui, toast);
		gfx_alpha(0x60);
		gfx_rrect(GFX_W - 64 - tw - 32, 40, tw + 32, 40, 20, 0, 0);
		gfx_alpha(0x80);
		gfx_text(&gfx_font_ui, GFX_W - 64 - tw - 16, 48, toast, ICE);
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

enum { ACT_END, ACT_STOP, ACT_PAUSE, ACT_BACK, ACT_FWD }; // why play() returned
static int show_osd; // SELECT: the gate numbers (kept across seeks and pauses)

// one stream from start (100 ns) to the end, O, a pause or a seek: each of those is a new stream request, so the
// player holds no paused connection (Jellyfin restarts the transcode at start). Returns the position reached
static long long play(jf_conn *c, const jf_item *it, long long start, int *act)
{
	static u8 vbuf[4 << 20] __attribute__((aligned(64))), abuf[1 << 20] __attribute__((aligned(64))); // audio: ~30 s of MP2 at 256 kbit/s
	char osd[160];
	*act = ACT_END;
	vring = (ring){vbuf, sizeof(vbuf), 0, 0}, aring = (ring){abuf, sizeof(abuf), 0, 0};
	net_eof = stop = net_kb = pics = main_at = 0, a_pts0 = -1, v_end_pts = 0, v_pts_n = 0, a_sent = 0, a_on = 0, a_played = 0, a_when = 0, a_queued = 0, a_done = 0, pending = 0, cur_pts = 0;
	ps_init(&dmx, on_video, on_audio, NULL);
	int st = jf_stream(c, &hs, it->id, start, VBR);
	logf_("jfplay: %s (%s) from %lld s: HTTP %d\n", it->name, it->id, start / 10000000, st);
	if (st != 200) { snprintf(osd, sizeof(osd), "HTTP %d", st); screen("No se pudo abrir el video", osd); sleep(3); return start; }
	jf_report(c, "", it->id, start);
	extern void *_gp;
	ee_thread_t nt = {.func = net_thread, .stack = net_stack, .stack_size = sizeof(net_stack), .gp_reg = &_gp, .initial_priority = 0x30};
	ee_thread_t at = {.func = audio_thread, .stack = audio_stack, .stack_size = sizeof(audio_stack), .gp_reg = &_gp, .initial_priority = 0x28};
	int ntid = CreateThread(&nt), atid = CreateThread(&at);
	StartThread(ntid, NULL);
	// ~1 MB of video first (10 s max), or until the audio ring is nearly full: the audio thread starts after this,
	// and a 1.2 Mbit/s video with 256 kbit/s audio filled a 256 KB audio ring first and stalled the download
	loading = 1, hud_ms = (int)(start / 10000);
	for (int t = 0; t < 600 && ring_used(&vring) < (1 << 20) && ring_used(&aring) < aring.size * 3 / 4 && !net_eof; t++) {
		if (t % 120 == 119) stall("prebuffer");
		if (pic_w) draw_picture(pic_w, pic_h, NULL, NULL); // after a seek or a pause: the last picture, under the bar
		else snprintf(osd, sizeof(osd), "CARGANDO  %d KB  %d KB/s", ring_used(&vring) >> 10, net_kb), screen(it->name, osd);
	}
	loading = 0;
	StartThread(atid, NULL);
	ee_thread_t wt = {.func = watchdog, .stack = dog_stack, .stack_size = sizeof(dog_stack), .gp_reg = &_gp, .initial_priority = 0x20};
	int wtid = CreateThread(&wt);
	StartThread(wtid, NULL);
	dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
	MPEG_Initialize(video_data, NULL, video_init, NULL, &cur_pts);
	long long wall0 = 0, last_pts = -1, gap = 0, first_pts = -1;
	int decoded = 0, shown = 0, late = 0, win_dec = 0, win_shown = 0, last_ms = -1;
	clock_t w0 = clock(), last_report = clock();
	for (;;) {
		unsigned p = pressed();
		if (p & PAD_CIRCLE) { *act = ACT_STOP; break; }
		if (p & (PAD_CROSS | PAD_START) && last_ms >= 0) { *act = ACT_PAUSE; break; } // a picture to pause on
		if (p & (PAD_L1 | PAD_LEFT)) { *act = ACT_BACK; break; }
		if (p & (PAD_R1 | PAD_RIGHT)) { *act = ACT_FWD; break; }
		if (p & PAD_SELECT) show_osd ^= 1;
		if (p & PAD_TRIANGLE) hud_show(); // just the bar
		if (p & PAD_SQUARE) sub_next(), hud_show();
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
		int media_ms = (int)(start / 10000 + (t - first_pts) / 90); // the picture's time in the movie
		hud_ms = last_ms = media_ms, pic_w = seq->m_Width, pic_h = seq->m_Height;
		draw_picture(seq->m_Width, seq->m_Height, show_osd ? osd : NULL, sub_at(media_ms));
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
	// the audio thread leaves by itself: terminated inside audsrv_wait_audio, the next audsrv call (stop) hung
	// when O was pressed mid-movie. Its wait ends as audsrv drains, well within 2 s
	for (int t = 0; t < 200 && !a_done && atid >= 0; t++) usleep(10000);
	TerminateThread(ntid), TerminateThread(wtid);
	if (!a_done) TerminateThread(atid), logf_("jfplay: audio thread did not stop\n");
	audsrv_stop_audio();
	long long pos = last_ms >= 0 ? last_ms * 10000LL : start; // the last picture shown, as the bar and subtitles count
	e0 = clock();
	jf_report(c, "/Stopped", it->id, pos);
	logf_("jfplay: stop report %d ms\n", (int)((long long)(clock() - e0) * 1000 / CLOCKS_PER_SEC));
	logf_("jfplay: stopped: %d decoded, %d shown, %d late, demux skipped %d bytes\n", decoded, shown, late, dmx.skipped);
	DeleteThread(ntid), DeleteThread(atid), DeleteThread(wtid);
	return pos;
}

// ---- browse (phase 15): libraries as tabs (L1 / R1), posters in a row as the launcher's carousel, the selected
// item's age rating, score, genres and synopsis; series open their episodes. Posters come from Jellyfin
// (/Items/{id}/Images/Primary) through cover.c, the same JPEG -> CT16 path as the game covers, in a low-priority
// thread that only runs while the render thread sleeps on the vsync, and never during playback ----
#define MAXI 200
#define POSTERS 14          // pairs kept (big 256x368 + small 184x264 CT16 = 282 KB each): about 4 MB
#define NAVY 0x0D1736
#define NIGHT 0x04060E
#define IRIS 0xC08BFF
#define INK 0x0A1026
static jf_conn conn;
static jf_item lib[16], items[MAXI], eps[128];
static int nlib, cur_lib, nitems, sel, neps = -1, ep_sel; // neps >= 0: the episode panel is open
static unsigned short *pbig[MAXI], *psmall[MAXI];
static volatile char pstate[MAXI];  // 0 none, 1 loading, 2 ready, 3 failed
static volatile int want = -1, loader_pause, loader_busy, list_gen;
static int poster_sema = -1;
static u8 poster_stack[0x10000] __attribute__((aligned(16)));

static void poster_thread(void *arg) // nearest missing poster to the selection, one at a time
{
	(void)arg;
	static char jpg[256 << 10];
	for (;;) {
		WaitSema(poster_sema);
		for (;;) {
			if (loader_pause) break;
			int s = want, gen = list_gen, best = -1;
			for (int d = 0; d < 8 && best < 0; d++) // the selection first, then outwards
				for (int k = d ? -1 : 1; k <= 1 && best < 0; k += 2) {
					int i = s + d * k;
					if (i >= 0 && i < nitems && !pstate[i] && items[i].has_image) best = i;
				}
			if (best < 0) break;
			int have = 0, far = -1; // keep at most POSTERS pairs: drop the farthest
			for (int i = 0; i < nitems; i++)
				if (pstate[i] == 2) { have++; if (far < 0 || abs(i - s) > abs(far - s)) far = i; }
			if (have >= POSTERS) {
				if (abs(far - s) <= abs(best - s)) break;
				pstate[far] = 0;
				free(pbig[far]), free(psmall[far]), pbig[far] = psmall[far] = NULL;
			}
			pstate[best] = 1, loader_busy = 1;
			unsigned short *b = memalign(64, COVER_W * COVER_H * 2), *sm = memalign(64, COVER_SW * COVER_SH * 2);
			int n = b && sm ? jf_image(&conn, items[best].id, COVER_W, jpg, sizeof(jpg)) : -1;
			int ok = n > 0 && cover_from_jpeg((u8 *)jpg, n, b, sm);
			loader_busy = 0;
			if (!ok || gen != list_gen) { free(b), free(sm); if (gen == list_gen) pstate[best] = 3; continue; }
			SyncDCache(b, b + COVER_W * COVER_H), SyncDCache(sm, sm + COVER_SW * COVER_SH);
			pbig[best] = b, psmall[best] = sm;
			__asm__ volatile("" ::: "memory");
			pstate[best] = 2;
		}
	}
}

static void wait_loader(void) // before playback or a new list: the loader leaves the network and RAM alone
{
	loader_pause = 1;
	while (loader_busy) usleep(10000);
}

static void load_library(int l)
{
	wait_loader();
	list_gen++;
	for (int i = 0; i < nitems; i++) free(pbig[i]), free(psmall[i]), pbig[i] = psmall[i] = NULL, pstate[i] = 0;
	nitems = jf_items(&conn, lib[l].id, items, MAXI);
	if (nitems < 0) nitems = 0;
	sel = 0, neps = -1;
	logf_("jfplay: library %s: %d items\n", lib[l].name, nitems);
	loader_pause = 0;
	want = 0;
	SignalSema(poster_sema);
}

static void wrap(const gfx_font *f, int x, int y, int w, int lines, const char *s, unsigned rgb) // word wrap, "..."
{
	char line[200];
	for (int l = 0; l < lines && *s; l++) {
		int n = 0, cut = 0;
		while (s[n] && n < (int)sizeof(line) - 4) {
			int k = n;
			while (s[k] && s[k] != ' ') k++;
			snprintf(line, sizeof(line), "%.*s", k, s);
			if (gfx_text_width(f, line) > w) break;
			cut = n = k;
			if (s[n] == ' ') n++;
		}
		if (!cut) cut = n ? n : (int)strlen(s); // one word wider than the box
		snprintf(line, sizeof(line), "%.*s%s", cut, s, l == lines - 1 && s[cut] ? "..." : "");
		gfx_text(f, x, y + l * f->line_h, line, rgb);
		s += cut;
		while (*s == ' ') s++;
	}
}

static int chip(int x, int y, const char *txt, unsigned fill_t, unsigned fill_b, unsigned ink) // a pill; returns its end
{
	int w = gfx_text_width(&gfx_font_ui, txt) + 24;
	gfx_rrect(x, y, w, 28, 14, fill_t, fill_b);
	gfx_text(&gfx_font_ui, x + 12, y + 3, txt, ink);
	return x + w + 10;
}

static void poster(int i, int x, int y, int big) // the poster, or a drawn card with the title while it loads
{
	int w = big ? COVER_W : COVER_SW, h = big ? COVER_H : COVER_SH;
	if (pstate[i] == 2) gfx_image(big ? pbig[i] : psmall[i], w, h, x, y, w, h);
	else {
		char t[64];
		gfx_rrect(x, y, w, h, 10, 0x1B2850, 0x0D1530);
		snprintf(t, sizeof(t), "%.63s", items[i].name);
		wrap(&gfx_font_ui, x + 14, y + h - 70, w - 28, 2, t, TEXT2);
	}
	if (items[i].resume > 0 && items[i].ticks > 0) { // how far you got
		gfx_rect(x + 8, y + h - 10, w - 16, 4, 0x3A4570);
		gfx_rect(x + 8, y + h - 10, (int)((w - 16) * items[i].resume / items[i].ticks), 4, ICE);
	}
}

static void draw_browse(float s)
{
	char a[160];
	gfx_begin();
	gfx_alpha(0x80);
	gfx_dither(1); // soft gradient: dithered (CT16 bands otherwise)
	gfx_grad(0, 0, GFX_W, GFX_H, NAVY, NIGHT, 1);
	gfx_dither(0);
	gfx_orb(64, 40, 56);
	int tx = 140; // library tabs
	for (int l = 0; l < nlib; l++) {
		gfx_tracking(2);
		snprintf(a, sizeof(a), "%.95s", lib[l].name);
		for (unsigned char *p = (unsigned char *)a; *p; p++)
			if (*p >= 'a' && *p <= 'z') *p -= 32;
			else if (p[0] == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) p[1] -= 0x20, p++; // á é í ó ú ñ ü...
		gfx_text(&gfx_font_mono, tx, 52, a, l == cur_lib ? ICE : LABEL);
		int w = gfx_text_width(&gfx_font_mono, a);
		if (l == cur_lib) gfx_rect(tx, 74, w, 2, ICE);
		gfx_tracking(0);
		tx += w + 36;
	}
	if (nlib > 1) hint(GFX_W - 64, 34, -1, "L1 R1", -1, "Biblioteca");
	if (!nitems) {
		gfx_text(&gfx_font_ui, 140, 300, "Esta biblioteca está vacía", TEXT2);
		gfx_end(), gfx_flip();
		return;
	}
	jf_item *it = &items[sel];
	gfx_text(&gfx_font_title, 64, 98, it->name, TEXT); // title, then the facts row
	int x = 64, y = 150;
	if (*it->rating) x = chip(x, y, it->rating, 0xF4F8FF, 0xAAB7D2, INK);
	if (it->score) snprintf(a, sizeof(a), "%d.%d / 10", it->score / 10, it->score % 10), x = chip(x, y, a, 0x2A2060, 0x1E1746, TEXT);
	a[0] = 0;
	if (it->year) snprintf(a, sizeof(a), "%d", it->year);
	if (it->ticks > 0) snprintf(a + strlen(a), sizeof(a) - strlen(a), "%s%lld min", *a ? "   " : "",
	                            it->ticks < 600000000 ? 1 : (it->ticks + 300000000) / 600000000); // under a minute: 1
	if (!strcmp(it->type, "Series")) snprintf(a + strlen(a), sizeof(a) - strlen(a), "%sSERIE", *a ? "   " : "");
	gfx_text(&gfx_font_ui, x + 4, y + 3, a, TEXT2);
	if (*it->genres) gfx_text(&gfx_font_mono, 64, 190, it->genres, LABEL);
	// the row: selected poster big at x 64, the rest small to its right, sliding with s (eased selection)
	int py = 232;
	for (int i = 0; i < nitems; i++) {
		float d = i - s;
		int big = i == sel && fabsf(d) < 0.5f;
		int px = 64 + (d <= 0 ? (int)(d * (COVER_SW + 24)) : COVER_W + 32 + (int)((d - 1) * (COVER_SW + 24)));
		if (px > GFX_W || px + COVER_W < 0) continue;
		poster(i, px, big ? py : py + (COVER_H - COVER_SH) / 2, big);
		if (big) gfx_rrect(px - 4, py - 4, COVER_W + 8, 3, 1, ICE, ICE);
	}
	if (*it->overview) wrap(&gfx_font_ui, 64, 606, GFX_W - 128, 2, it->overview, TEXT2);
	snprintf(a, sizeof(a), "%02d / %02d", sel + 1, nitems);
	gfx_text(&gfx_font_mono, 64, 664, a, LABEL);
	if (!strcmp(it->type, "Series")) hint(GFX_W - 64, 652, UI_CROSS_14, NULL, UI_LIST_18, "Episodios");
	else {
		int x = GFX_W - 64;
		if (it->resume) x = hint(x, 652, UI_TRIANGLE_14, NULL, UI_PLAY_18, "Continuar");
		hint(x, 652, UI_CROSS_14, NULL, UI_PLAY_18, "Reproducir");
	}
	if (neps >= 0) { // episode panel over the browse screen
		int pw = 760, ph = 520, px = (GFX_W - pw) / 2, pyy = 110;
		gfx_alpha(0x60);
		gfx_rect(0, 0, GFX_W, GFX_H, NIGHT);
		gfx_alpha(0x80);
		gfx_rrect(px, pyy, pw, ph, 18, 0x16224A, 0x0A1128);
		gfx_text(&gfx_font_ui, px + 28, pyy + 20, it->name, TEXT);
		if (!neps) gfx_text(&gfx_font_ui, px + 28, pyy + 80, "Sin episodios", TEXT2);
		int top = ep_sel > 5 ? ep_sel - 5 : 0;
		for (int e = top; e < neps && e < top + 8; e++) {
			int ey = pyy + 70 + (e - top) * 44;
			if (e == ep_sel) gfx_alpha(0x28), gfx_rrect(px + 12, ey - 6, pw - 24, 40, 20, ICE, ICE), gfx_alpha(0x80);
			if (eps[e].episode) snprintf(a, sizeof(a), "T%d · E%02d", eps[e].season, eps[e].episode);
			else snprintf(a, sizeof(a), "T%d", eps[e].season); // no number in the server's metadata
			gfx_text(&gfx_font_mono, px + 28, ey + 2, a, e == ep_sel ? ICE : LABEL);
			snprintf(a, sizeof(a), "%.60s", eps[e].name);
			gfx_text(&gfx_font_ui, px + 150, ey, a, e == ep_sel ? TEXT : TEXT2);
			snprintf(a, sizeof(a), "%lld min", eps[e].ticks < 600000000 ? 1 : (eps[e].ticks + 300000000) / 600000000);
			gfx_text(&gfx_font_mono, px + pw - 28 - gfx_text_width(&gfx_font_mono, a), ey + 2, a, LABEL);
			if (eps[e].resume > 0 && eps[e].ticks > 0) gfx_rect(px + 150, ey + 30, (int)(300 * eps[e].resume / eps[e].ticks), 3, ICE);
		}
		int x = hint(px + pw - 20, pyy + ph - 62, UI_CIRCLE_14, NULL, -1, "Cerrar");
		if (neps > 0) {
			if (eps[ep_sel].resume) x = hint(x, pyy + ph - 62, UI_TRIANGLE_14, NULL, UI_PLAY_18, "Continuar");
			hint(x, pyy + ph - 62, UI_CROSS_14, NULL, UI_PLAY_18, "Reproducir");
		}
	}
	gfx_end();
	gfx_flip();
}

static long long seek_to(long long pos, long long d, long long len) // 100 ns; stays 10 s short of the end
{
	pos += d;
	if (len > 0 && pos > len - 100000000LL) pos = len - 100000000LL;
	return pos < 0 ? 0 : pos;
}

static int pause_screen(long long *pos, long long len) // the last picture under the bar; 1 = play on from *pos
{
	paused = 1;
	for (;;) {
		unsigned p = pressed();
		if (p & PAD_CIRCLE) break;
		if (p & (PAD_CROSS | PAD_START)) { paused = 0; return 1; }
		if (p & (PAD_L1 | PAD_LEFT)) *pos = seek_to(*pos, -100000000LL, len);
		if (p & (PAD_R1 | PAD_RIGHT)) *pos = seek_to(*pos, 300000000LL, len);
		if (p & PAD_SQUARE) sub_next();
		hud_ms = (int)(*pos / 10000);
		draw_picture(pic_w, pic_h, NULL, sub_at(hud_ms));
	}
	paused = 0;
	return 0;
}

static void play_and_back(jf_item *it, long long start)
{
	wait_loader();
	screen(it->name, "Preparando...");
	subs_fetch(&conn, it);
	sub_select(sub_pick());
	toast_until = 0, pic_w = pic_h = 0;
	if (cur_track >= 0) { char m[112]; snprintf(m, sizeof(m), "Subtítulos: %.60s  (Cuadrado cambia)", tracks[cur_track].title); say(m); }
	else if (ntracks) say("Subtítulos: no  (Cuadrado cambia)");
	hud_title = it->name, hud_len_ms = (int)(it->ticks / 10000);
	long long pos = start;
	for (;;) { // a pause or a seek ends the stream; the next one starts where it left
		int act;
		hud_show();
		pos = play(&conn, it, pos, &act);
		if (act == ACT_BACK || act == ACT_FWD) pos = seek_to(pos, act == ACT_FWD ? 300000000LL : -100000000LL, it->ticks);
		else if (act != ACT_PAUSE || !pause_screen(&pos, it->ticks)) break;
	}
	subs_free();
	// the bar under the poster at once (Jellyfin's own rule: past 90 % it counts as watched, no resume point)
	it->resume = it->ticks > 0 && pos > it->ticks * 9 / 10 ? 0 : pos;
	loader_pause = 0;
	SignalSema(poster_sema);
}

int main(void)
{
	static ini cfg;
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
	snprintf(sub_pref, sizeof(sub_pref), "%s", ini_get(&cfg, "jellyfin", "subtitulos", "spa"));
	screen("Jellyfin", url);
	r = jf_login(&c, url, ini_get(&cfg, "jellyfin", "usuario", ""), ini_get(&cfg, "jellyfin", "clave", ""));
	logf_("jfplay: mtu %d, login %s: %d (%s)\n", net_mtu, url, r, jf_login_why);
	if (r < 0) {
		snprintf(msg, sizeof(msg), "No se pudo entrar a %s (%d)\nRevisa [jellyfin] servidor, usuario y clave en config.ini",
		         *url ? url : "(sin servidor)", r);
		screen("Jellyfin", msg);
		SleepThread();
	}
	conn = c;
	jf_item views[16];
	int nv = jf_views(&conn, views, 16);
	for (int v = 0; v < nv && nlib < 16; v++) // libraries with movies or series (not music, photos...)
		if (!strcmp(views[v].collection, "movies") || !strcmp(views[v].collection, "tvshows") || !*views[v].collection)
			lib[nlib++] = views[v];
	logf_("jfplay: %d libraries, %d with video\n", nv, nlib);
	ee_sema_t ps = {.init_count = 0, .max_count = 1};
	poster_sema = CreateSema(&ps);
	extern void *_gp;
	ee_thread_t pt = {.func = poster_thread, .stack = poster_stack, .stack_size = sizeof(poster_stack), .gp_reg = &_gp,
	                  .initial_priority = 0x70}; // below the render thread (0x60): runs only while it sleeps
	StartThread(CreateThread(&pt), NULL);
	if (nlib) load_library(0);
	float s = 0;
	for (;;) {
		unsigned p = pressed();
		if (neps >= 0) { // episode panel
			if (p & PAD_DOWN && ep_sel < neps - 1) ep_sel++;
			if (p & PAD_UP && ep_sel > 0) ep_sel--;
			if (p & PAD_CIRCLE) neps = -1;
			else if (p & (PAD_CROSS | PAD_TRIANGLE) && neps > 0)
				play_and_back(&eps[ep_sel], p & PAD_TRIANGLE ? eps[ep_sel].resume : 0);
		} else if (nlib) {
			if (p & PAD_RIGHT && sel < nitems - 1) sel++;
			if (p & PAD_LEFT && sel > 0) sel--;
			if (p & (PAD_R1 | PAD_L1) && nlib > 1)
				cur_lib = (cur_lib + (p & PAD_R1 ? 1 : nlib - 1)) % nlib, load_library(cur_lib), s = 0;
			if (p & (PAD_CROSS | PAD_TRIANGLE) && nitems) {
				if (!strcmp(items[sel].type, "Series")) {
					wait_loader();
					neps = jf_episodes(&conn, items[sel].id, eps, 128), ep_sel = 0;
					if (neps < 0) neps = 0;
					loader_pause = 0;
				} else play_and_back(&items[sel], p & PAD_TRIANGLE ? items[sel].resume : 0);
			}
			if (want != sel) want = sel, SignalSema(poster_sema);
		}
		s += (sel - s) * 0.22f;
		if (fabsf(sel - s) < 0.002f) s = sel;
		draw_browse(s);
	}
	return 0;
}
