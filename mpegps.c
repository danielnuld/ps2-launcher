// MPEG-2 program stream demuxer (phase 15), push style: the HTTP reader feeds whatever it got, packets may be split
// anywhere. Pack header 0x000001BA (MPEG-2: 14 bytes + stuffing, low 3 bits of byte 13; MPEG-1: 12 bytes), system
// header 0x000001BB and every other stream skipped by their 16-bit length; PES 0xE0-0xEF video and 0xC0-0xDF audio
// with the MPEG-2 PES header (flags byte 7, header length byte 8, PTS in 5 bytes). ISO/IEC 13818-1 2.5.3, 2.4.3.6.
#include <string.h>
#include "mpegps.h"

void ps_init(ps_demux *d, ps_out video, ps_out audio, void *ctx)
{
	memset(d, 0, sizeof(*d));
	d->video = video, d->audio = audio, d->ctx = ctx;
}

static long long pts(const unsigned char *p) // 33 bits in 5 bytes with marker bits
{
	return (long long)(p[0] >> 1 & 7) << 30 | (p[1] << 7 | p[2] >> 1) << 15 | (p[3] << 7 | p[4] >> 1);
}

static int packet(ps_demux *d, const unsigned char *p, int n) // bytes of one unit at p (start code first), 0 = need more
{
	if (n < 6) return 0;
	unsigned id = p[3];
	if (id == 0xBA) { // pack header
		if (n < 14) return 0;
		int len = (p[4] & 0xC0) == 0x40 ? 14 + (p[13] & 7) : 12;
		return n < len ? 0 : len;
	}
	if (id == 0xB9) return 4; // program end
	int len = 6 + (p[4] << 8 | p[5]);
	if (n < len) return 0;
	if ((id & 0xF0) == 0xE0 || (id & 0xE0) == 0xC0) {
		int h = 6;
		long long t = -1;
		if ((p[6] & 0xC0) == 0x80) { // MPEG-2 PES header
			if (p[7] & 0x80 && len >= 14) t = pts(p + 9);
			h = 9 + p[8];
		} else { // MPEG-1: stuffing 0xFF, optional STD buffer, then PTS / PTS+DTS / 0x0F
			while (h < len && p[h] == 0xFF) h++;
			if (h < len && (p[h] & 0xC0) == 0x40) h += 2;
			if (h < len && (p[h] & 0xE0) == 0x20) t = pts(p + h), h += (p[h] & 0x10) ? 10 : 5;
			else h++;
		}
		if (h < len) ((id & 0xF0) == 0xE0 ? d->video : d->audio)(d->ctx, p + h, len - h, t);
	}
	return len; // 0xBB system header, 0xBD/0xBF private, 0xBE padding...: skipped
}

int ps_feed(ps_demux *d, const unsigned char *data, int n)
{
	while (n > 0) {
		int k = (int)sizeof(d->buf) - d->len < n ? (int)sizeof(d->buf) - d->len : n;
		memcpy(d->buf + d->len, data, k);
		d->len += k, data += k, n -= k;
		int pos = 0;
		for (;;) {
			while (pos + 4 <= d->len && !(d->buf[pos] == 0 && d->buf[pos + 1] == 0 && d->buf[pos + 2] == 1)) pos++, d->skipped++;
			if (pos + 4 > d->len) break;
			int used = packet(d, d->buf + pos, d->len - pos);
			if (!used) break;
			pos += used;
		}
		if (pos == 0 && d->len == (int)sizeof(d->buf)) return -1; // a unit larger than the buffer: not a PS
		memmove(d->buf, d->buf + pos, d->len - pos);
		d->len -= pos;
	}
	return 0;
}

#ifdef SELFTEST // host check: `make test`; PS_CHECK="<stream.mpg> <video.m2v> <audio.mp2>" compares with ffmpeg -c copy
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef struct { FILE *v, *a; long long first_v, first_a; int nv, na; } outs;
static void ov(void *c, const unsigned char *p, int n, long long t) { outs *o = c; fwrite(p, 1, n, o->v); if (o->first_v < 0) o->first_v = t; o->nv++; }
static void oa(void *c, const unsigned char *p, int n, long long t) { outs *o = c; fwrite(p, 1, n, o->a); if (o->first_a < 0) o->first_a = t; o->na++; }
static int same(FILE *a, const char *ref)
{
	FILE *b = fopen(ref, "rb");
	int ok = b != NULL, x, y;
	rewind(a);
	while (ok && ((x = fgetc(a)) != EOF) | ((y = fgetc(b)) != EOF)) ok = x == y;
	if (b) fclose(b);
	return ok;
}
int main(int argc, char **argv)
{
	static ps_demux d;
	// synthetic: pack header, a video PES with a PTS of 90000 split over feeds, padding, an audio PES
	static const unsigned char s[] = {0, 0, 1, 0xBA, 0x44, 0, 4, 0, 4, 1, 0, 0, 3, 0xF8,
		0, 0, 1, 0xE0, 0, 11, 0x80, 0x80, 5, 0x21, 0x00, 0x05, 0xBF, 0x21, 'V', 'I', 'D',
		0, 0, 1, 0xBE, 0, 2, 0xFF, 0xFF, 0, 0, 1, 0xC0, 0, 5, 0x80, 0, 0, 'A', 'U', 0, 0, 1, 0xB9};
	outs o = {tmpfile(), tmpfile(), -1, -1, 0, 0};
	ps_init(&d, ov, oa, &o);
	for (unsigned i = 0; i < sizeof(s); i++) assert(ps_feed(&d, s + i, 1) == 0); // byte by byte
	assert(o.nv == 1 && o.na == 1 && o.first_v == 90000 && o.first_a == -1 && d.skipped == 0);
	char b[4] = {0};
	rewind(o.v), assert(fread(b, 1, 3, o.v) == 3 && !strcmp(b, "VID"));
	if (argc > 3) { // a real stream in uneven chunks against ffmpeg's elementary streams
		FILE *f = fopen(argv[1], "rb");
		static unsigned char buf[70000];
		outs r = {tmpfile(), tmpfile(), -1, -1, 0, 0};
		ps_init(&d, ov, oa, &r);
		int n, k = 1;
		while (f && (n = fread(buf, 1, (k = k * 7919 % 65521 + 1), f)) > 0) assert(ps_feed(&d, buf, n) == 0);
		fflush(r.v), fflush(r.a);
		printf("%s: %d video / %d audio packets, first PTS %lld / %lld, %d bytes skipped; video %s, audio %s\n", argv[1],
		       r.nv, r.na, r.first_v, r.first_a, d.skipped, same(r.v, argv[2]) ? "identical" : "DIFFERENT",
		       same(r.a, argv[3]) ? "identical" : "DIFFERENT");
		assert(same(r.v, argv[2]) && same(r.a, argv[3]));
	}
	puts("mpegps selftest ok");
	return 0;
}
#endif
