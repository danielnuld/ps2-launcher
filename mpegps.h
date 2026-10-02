#pragma once
// MPEG-2 program stream demuxer (phase 15): elementary-stream payloads out, with their PTS (90 kHz, -1 if none).
typedef void (*ps_out)(void *ctx, const unsigned char *p, int n, long long pts);
typedef struct {
	ps_out video, audio;
	void *ctx;
	int len, skipped; // bytes buffered; bytes dropped while looking for a start code
	unsigned char buf[72 * 1024]; // the largest PES (65 541 bytes) plus room to append
} ps_demux;
void ps_init(ps_demux *d, ps_out video, ps_out audio, void *ctx);
int ps_feed(ps_demux *d, const unsigned char *data, int n); // 0, or -1 if the data is not a program stream
