// JPEG cover -> .c16 pixels on the EE (phase 8, roadmap option A): the same path as tools/covers.py. Decode
// (libjpeg, ps2sdk ports), resize in linear light with PIL's Lanczos (Resample.c: support 3 x the downscale, taps
// normalised, horizontal pass first), back to sRGB, then gfx_fs_dither (Floyd-Steinberg to RGB555, as covers.py).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <jpeglib.h>
#include "gfx.h"
#include "cover.h"

static float lanczos(float x)
{
	if (x <= -3 || x >= 3) return 0;
	if (x == 0) return 1;
	float a = x * 3.14159265f, b = a / 3;
	return sinf(a) / a * sinf(b) / b;
}

// one axis: n_in samples (stride sin) -> n_out (stride sout), for `count` lines (line stride lin / lout), 3 channels
static void resample(const float *in, int n_in, int sin, int lin, float *out, int n_out, int sout, int lout, int count)
{
	float scale = (float)n_in / n_out, fs = scale > 1 ? scale : 1, support = 3 * fs, k[64];
	for (int o = 0; o < n_out; o++) {
		float center = (o + 0.5f) * scale, ww = 0;
		int lo = (int)(center - support + 0.5f), hi = (int)(center + support + 0.5f);
		if (lo < 0) lo = 0;
		if (hi > n_in) hi = n_in;
		if (hi - lo > 64) hi = lo + 64; // 3 x 2 x scale taps: room for a 10x downscale
		for (int i = lo; i < hi; i++) ww += k[i - lo] = lanczos((i - center + 0.5f) / fs);
		for (int l = 0; l < count; l++)
			for (int c = 0; c < 3; c++) {
				float v = 0;
				for (int i = lo; i < hi; i++) v += in[l * lin + i * sin + c] * k[i - lo];
				out[l * lout + o * sout + c] = v / ww;
			}
	}
}

static void from_buf(int x, int y, float *rgb, void *u) // gfx_fs_dither source: sRGB 0..255 floats, w = *(int *)u
{
	const int *hdr = u;
	const float *p = (const float *)(hdr + 1) + (y * hdr[0] + x) * 3;
	rgb[0] = p[0], rgb[1] = p[1], rgb[2] = p[2];
}

static int one_size(const float *lin, int w, int h, unsigned short *dst, int tw, int th)
{
	float *mid = malloc(sizeof(float) * tw * h * 3);
	int *out = malloc(sizeof(int) + sizeof(float) * tw * th * 3); // [tw][pixels]: from_buf's user data
	if (!mid || !out) { free(mid); free(out); return 0; }
	resample(lin, w, 3, w * 3, mid, tw, 3, tw * 3, h);           // rows
	float *px = (float *)(out + 1);
	resample(mid, h, tw * 3, 3, px, th, tw * 3, 3, tw);           // columns
	for (int i = 0; i < tw * th * 3; i++) { // linear -> sRGB 0..255 (covers.py to_srgb)
		float c = px[i] < 0 ? 0 : px[i] > 1 ? 1 : px[i];
		px[i] = 255 * (c <= 0.0031308f ? c * 12.92f : 1.055f * powf(c, 1 / 2.4f) - 0.055f);
	}
	out[0] = tw;
	gfx_fs_dither(dst, tw, th, from_buf, out);
	free(mid);
	free(out);
	return 1;
}

struct jerr { struct jpeg_error_mgr mgr; jmp_buf jmp; };
static void jpeg_fail(j_common_ptr c) { longjmp(((struct jerr *)c->err)->jmp, 1); } // default: exit()

int cover_from_jpeg(const unsigned char *jpg, int n, unsigned short *big, unsigned short *small)
{
	struct jpeg_decompress_struct d;
	struct jerr err;
	unsigned char *rgb = NULL;
	float *lin = NULL;
	int ok = 0;
	d.err = jpeg_std_error(&err.mgr);
	err.mgr.error_exit = jpeg_fail;
	jpeg_create_decompress(&d);
	if (setjmp(err.jmp)) goto done; // corrupt or truncated file: the game keeps its generic cover
	jpeg_mem_src(&d, (unsigned char *)jpg, n);
	jpeg_read_header(&d, TRUE);
	d.out_color_space = JCS_RGB;
	jpeg_start_decompress(&d);
	int w = d.output_width, h = d.output_height;
	if (w < 64 || h < 64 || w > 2048 || h > 2048) goto done; // ponytail: sanity bounds, xlenore covers are 512x736
	rgb = malloc(w * h * 3);
	lin = malloc(sizeof(float) * w * h * 3);
	if (!rgb || !lin) goto done;
	while ((int)d.output_scanline < h) {
		unsigned char *row = rgb + d.output_scanline * w * 3;
		jpeg_read_scanlines(&d, &row, 1);
	}
	jpeg_finish_decompress(&d);
	float lut[256]; // sRGB -> linear (covers.py to_linear)
	for (int i = 0; i < 256; i++) {
		float c = i / 255.f;
		lut[i] = c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
	}
	for (int i = 0; i < w * h * 3; i++) lin[i] = lut[rgb[i]];
	free(rgb), rgb = NULL;
	ok = one_size(lin, w, h, big, COVER_W, COVER_H) && one_size(lin, w, h, small, COVER_SW, COVER_SH);
done:
	jpeg_destroy_decompress(&d);
	free(rgb);
	free(lin);
	return ok;
}

#ifdef SELFTEST // host check: `make test`; with args, compare a real JPG against tools/covers.py output
#include <assert.h>
static int levels(unsigned short p, int c) { return p >> (5 * c) & 31; }
int main(int argc, char **argv)
{
	static float flat[100 * 120 * 3], mid[50 * 120 * 3], out[50 * 60 * 3];
	for (int i = 0; i < 100 * 120 * 3; i++) flat[i] = 0.25f;
	resample(flat, 100, 3, 300, mid, 50, 3, 150, 120);
	resample(mid, 120, 150, 3, out, 60, 150, 3, 50);
	for (int i = 0; i < 50 * 60 * 3; i++) assert(fabsf(out[i] - 0.25f) < 1e-5f); // normalised taps keep a flat colour
	if (argc == 4) { // cover_selftest <jpg> <ref .c16 256x368> <ref _s.c16>
		static unsigned char jpg[1 << 20];
		static unsigned short big[COVER_W * COVER_H], small[COVER_SW * COVER_SH], ref[COVER_W * COVER_H + 8];
		FILE *f = fopen(argv[1], "rb");
		int n = fread(jpg, 1, sizeof(jpg), f);
		fclose(f);
		assert(cover_from_jpeg(jpg, n, big, small));
		for (int s = 0; s < 2; s++) {
			unsigned short *p = s ? small : big;
			int np = s ? COVER_SW * COVER_SH : COVER_W * COVER_H, off = 0, sum = 0;
			f = fopen(argv[2 + s], "rb");
			assert(fread(ref, 2, np + 8, f) == (size_t)np + 8);
			fclose(f);
			for (int i = 0; i < np; i++)
				for (int c = 0; c < 3; c++) {
					int dv = abs(levels(p[i], c) - levels(ref[8 + i], c));
					off += dv > 1, sum += dv;
				}
			printf("%s: mean |diff| %.3f levels, %d channel values off by more than 1\n", s ? "small" : "big",
			       (double)sum / (np * 3), off);
		}
	}
	puts("cover selftest ok");
	return 0;
}
#endif
