// PS2 save icons (phase 9). Formats in docs/sources.md: icon.sys = ps2sdk mcIcon; the icon file = header, per-vertex
// shapes/normal/UV/colour, animation frames with keys, 128x128 CT16 texture raw or RLE. Shading follows the mymc++
// viewer (reference only): y and z flipped, colour x texture x (ambient + sum max(0, L.n) C), keys lerped cyclically.
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>
#include "icon.h"

#define TEX 128

typedef struct { const unsigned char *d; int n, o; } rd;
static int need(rd *r, int k) { return k >= 0 && r->o <= r->n - k; }
static unsigned u32(rd *r) { unsigned v; memcpy(&v, r->d + r->o, 4); r->o += 4; return v; }
static float f32(rd *r) { float v; memcpy(&v, r->d + r->o, 4); r->o += 4; return v; }

int icon_sys_parse(const unsigned char *d, int n, icon *ic, char *list_name, int name_n)
{
	if (n < 964 || memcmp(d, "PS2D", 4)) return 0;
	for (int i = 0; i < 3; i++)
		for (int c = 0; c < 3; c++) {
			memcpy(&ic->light_dir[i][c], d + 0x50 + i * 16 + c * 4, 4);
			memcpy(&ic->light_col[i][c], d + 0x80 + i * 16 + c * 4, 4);
		}
	for (int c = 0; c < 3; c++) memcpy(&ic->ambient[c], d + 0xB0 + c * 4, 4);
	for (int i = 0; i < 3; i++) { // normalised once (the viewer does too); a zero direction stays zero
		float *l = ic->light_dir[i], m = sqrtf(l[0] * l[0] + l[1] * l[1] + l[2] * l[2]);
		if (m > 1e-4f) l[0] /= m, l[1] /= m, l[2] /= m;
	}
	int k = 0;
	for (; k < name_n - 1 && k < 64 && d[0x104 + k]; k++) list_name[k] = d[0x104 + k];
	list_name[k] = 0;
	return k > 0;
}

void icon_free(icon *ic)
{
	if (ic->frames)
		for (int f = 0; f < ic->nframes; f++) free(ic->frames[f].t), free(ic->frames[f].v);
	free(ic->pos), free(ic->nrm), free(ic->uv), free(ic->rgba), free(ic->frames), free(ic->tex);
	ic->pos = ic->nrm = ic->uv = NULL, ic->rgba = NULL, ic->frames = NULL, ic->tex = NULL;
}

static int rle(rd *r, unsigned short *tex) // u32 size, then u16 codes: bit 15 -> literals, else a repeated u16
{
	if (!need(r, 4)) return 0;
	int size = u32(r), out = 0;
	if (size & 1 || !need(r, size)) return 0;
	int end = r->o + size;
	while (r->o < end) {
		if (end - r->o < 2) return 0;
		unsigned code = r->d[r->o] | r->d[r->o + 1] << 8;
		r->o += 2;
		int lit = code & 0x8000, cnt = lit ? 0x10000 - code : code, bytes = lit ? cnt * 2 : 2;
		if (out + cnt > TEX * TEX || end - r->o < bytes) return 0;
		for (int i = 0; i < cnt; i++) memcpy(&tex[out + i], r->d + r->o + (lit ? i * 2 : 0), 2);
		r->o += cnt ? bytes : 0;
		out += cnt;
	}
	return out == TEX * TEX;
}

int icon_parse(const unsigned char *d, int n, icon *ic)
{
	rd r = {d, n, 0};
	ic->pos = ic->nrm = ic->uv = NULL, ic->rgba = NULL, ic->frames = NULL, ic->tex = NULL, ic->nframes = 0;
	if (!need(&r, 20) || u32(&r) != 0x010000) return 0;
	int shapes = u32(&r), type = u32(&r);
	r.o += 4;
	int nv = u32(&r);
	if (shapes < 1 || shapes > 64 || nv < 3 || nv % 3 || nv > 20000 || !need(&r, nv * (8 * shapes + 16))) return 0;
	ic->nv = nv, ic->shapes = shapes;
	ic->pos = malloc(sizeof(short) * shapes * nv * 3), ic->nrm = malloc(sizeof(short) * nv * 3);
	ic->uv = malloc(sizeof(short) * nv * 2), ic->rgba = malloc(nv * 4);
	if (!ic->pos || !ic->nrm || !ic->uv || !ic->rgba) goto bad;
	for (int i = 0; i < nv; i++) {
		for (int s = 0; s < shapes; s++, r.o += 8) memcpy(&ic->pos[(s * nv + i) * 3], d + r.o, 6);
		memcpy(&ic->nrm[i * 3], d + r.o, 6), r.o += 8;
		memcpy(&ic->uv[i * 2], d + r.o, 4), r.o += 4;
		memcpy(&ic->rgba[i * 4], d + r.o, 4), r.o += 4;
	}
	if (!need(&r, 20) || u32(&r) != 1) goto bad;
	ic->frame_length = u32(&r), ic->speed = f32(&r);
	r.o += 4;
	int nf = u32(&r);
	if (nf < 0 || nf > 1024) goto bad;
	ic->frames = calloc(nf ? nf : 1, sizeof(icon_frame));
	if (!ic->frames) goto bad;
	ic->nframes = nf; // calloc'd: icon_free can free a partly read list
	for (int f = 0; f < nf; f++) {
		if (!need(&r, 16)) goto bad;
		icon_frame *fr = &ic->frames[f];
		fr->shape = u32(&r);
		int keys = (int)u32(&r) - 1; // the count includes one more than the keys stored (mymc++ issue 26: 0 means none)
		r.o += 8;
		if (keys < 0) keys = 0;
		if (keys > 4096 || !need(&r, keys * 8)) goto bad;
		fr->nkeys = keys;
		fr->t = malloc(sizeof(float) * (keys ? keys : 1)), fr->v = malloc(sizeof(float) * (keys ? keys : 1));
		if (!fr->t || !fr->v) goto bad;
		for (int k = 0; k < keys; k++) fr->t[k] = f32(&r), fr->v[k] = f32(&r);
	}
	if (type & 4) { // texture present: required, so a file cut before it is rejected
		ic->tex = memalign(64, TEX * TEX * 2);
		if (!ic->tex) goto bad;
		if (type & 8) { if (!rle(&r, ic->tex)) goto bad; }
		else if (need(&r, TEX * TEX * 2)) memcpy(ic->tex, d + r.o, TEX * TEX * 2);
		else goto bad;
	}
	float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
	for (int i = 0; i < nv * 3; i++) {
		float p = ic->pos[i] / 4096.f;
		if (p < lo[i % 3]) lo[i % 3] = p;
		if (p > hi[i % 3]) hi[i % 3] = p;
	}
	ic->cx = (lo[0] + hi[0]) / 2, ic->cy = (lo[1] + hi[1]) / 2, ic->cz = (lo[2] + hi[2]) / 2;
	float xz = 0, h = (hi[1] - lo[1]) / 2;
	for (int i = 0; i < nv; i++) { // radius about the spin axis, so the turning icon never leaves its box
		float x = ic->pos[i * 3] / 4096.f - ic->cx, z = ic->pos[i * 3 + 2] / 4096.f - ic->cz;
		if (x * x + z * z > xz * xz) xz = sqrtf(x * x + z * z);
	}
	ic->half = xz > h ? xz : h;
	if (ic->half <= 0) goto bad;
	return 1;
bad:
	icon_free(ic);
	return 0;
}

static float key_value(const icon_frame *f, float t, float len) // cyclic lerp between the keys around t
{
	int last = -1, next = -1;
	float lt = 0, nt = 0;
	for (int k = 0; k < f->nkeys; k++) {
		float a = f->t[k] <= t ? f->t[k] : f->t[k] - len, b = f->t[k] >= t ? f->t[k] : f->t[k] + len;
		if (last < 0 || a > lt) last = k, lt = a;
		if (next < 0 || b < nt) next = k, nt = b;
	}
	if (last < 0) return 0;
	float p = nt > lt ? (t - lt) / (nt - lt) : 0;
	return f->v[last] * (1 - p) + f->v[next] * p;
}

typedef struct { float z; int i; } tri_key;
static int far_first(const void *a, const void *b)
{
	float d = ((const tri_key *)a)->z - ((const tri_key *)b)->z;
	return d < 0 ? -1 : d > 0;
}

int icon_draw_list(const icon *ic, float t, float angle, float x0, float y0, float size, icon_vert *out)
{
	float w[64] = {0}, sum = 0, len = ic->frame_length > 0 ? ic->frame_length : 1;
	t = fmodf(t, len);
	for (int f = 0; f < ic->nframes; f++)
		if ((unsigned)ic->frames[f].shape < (unsigned)ic->shapes && ic->frames[f].nkeys)
			w[ic->frames[f].shape] = key_value(&ic->frames[f], t, len);
	for (int s = 0; s < ic->shapes; s++) sum += w[s];
	if (sum <= 0) memset(w, 0, sizeof(w)), w[0] = sum = 1;
	float c = cosf(angle), sn = sinf(angle), k = size / 2 / ic->half, mx = x0 + size / 2, my = y0 + size / 2;
	int nt = ic->nv / 3;
	tri_key *keys = malloc(sizeof(tri_key) * nt);
	icon_vert *tmp = malloc(sizeof(icon_vert) * ic->nv);
	if (!keys || !tmp) { free(keys); free(tmp); return 0; }
	for (int i = 0; i < ic->nv; i++) {
		float p[3] = {0, 0, 0};
		for (int s = 0; s < ic->shapes; s++)
			if (w[s] != 0)
				for (int a = 0; a < 3; a++) p[a] += w[s] / sum * ic->pos[(s * ic->nv + i) * 3 + a] / 4096.f;
		// viewer space: y and z flipped, centred, turned about y; camera on +z
		float x = p[0] - ic->cx, y = -(p[1] - ic->cy), z = -(p[2] - ic->cz);
		float rx = x * c + z * sn, rz = -x * sn + z * c;
		float nx = ic->nrm[i * 3], ny = -ic->nrm[i * 3 + 1], nz = -ic->nrm[i * 3 + 2], m = sqrtf(nx * nx + ny * ny + nz * nz);
		if (m > 0) nx /= m, ny /= m, nz /= m;
		float rnx = nx * c + nz * sn, rnz = -nx * sn + nz * c, lum[3];
		for (int ch = 0; ch < 3; ch++) lum[ch] = ic->ambient[ch];
		for (int l = 0; l < 3; l++) {
			float d = ic->light_dir[l][0] * rnx + ic->light_dir[l][1] * ny + ic->light_dir[l][2] * rnz;
			if (d > 0)
				for (int ch = 0; ch < 3; ch++) lum[ch] += d * ic->light_col[l][ch];
		}
		icon_vert *v = &tmp[i];
		v->x = mx + rx * k, v->y = my - y * k, v->z = 0.5f + rz / (2 * ic->half); // fit radius: stays in 0..1
		v->u = ic->uv[i * 2] / 4096.f * 128, v->v = ic->uv[i * 2 + 1] / 4096.f * 128;
		unsigned char *col = &v->r;
		for (int ch = 0; ch < 3; ch++) {
			float g = ic->rgba[i * 4 + ch] / 255.f * lum[ch] * 128; // GS: 0x80 = 1.0
			col[ch] = g > 255 ? 255 : g < 0 ? 0 : (unsigned char)g;
		}
	}
	for (int i = 0; i < nt; i++) keys[i].i = i, keys[i].z = tmp[i * 3].z + tmp[i * 3 + 1].z + tmp[i * 3 + 2].z;
	qsort(keys, nt, sizeof(tri_key), far_first); // painter's order. ponytail: no Z buffer; add one if icons need it
	for (int i = 0; i < nt; i++) memcpy(&out[i * 3], &tmp[keys[i].i * 3], sizeof(icon_vert) * 3);
	free(keys);
	free(tmp);
	return nt * 3;
}

#ifdef SELFTEST // host check: `make test`; ICON_CHECK=<dir with icon.sys + icon.ico> also parses a real save
#include <assert.h>
#include <stdio.h>
static unsigned char *put(unsigned char *p, const void *v, int n) { memcpy(p, v, n); return p + n; }
static unsigned char *u(unsigned char *p, unsigned v) { return put(p, &v, 4); }
static unsigned char *f(unsigned char *p, float v) { return put(p, &v, 4); }
static unsigned char *h(unsigned char *p, int v) { short s = v; return put(p, &s, 2); }

static int synth(unsigned char *b, int overflow) // one triangle, 2 shapes, keyed animation, RLE texture
{
	unsigned char *p = b;
	p = u(p, 0x010000), p = u(p, 2), p = u(p, 0x0F), p = f(p, 1), p = u(p, 3);
	for (int i = 0; i < 3; i++) {
		for (int s = 0; s < 2; s++) p = h(p, (i - 1) * 4096 * (s + 1)), p = h(p, -i * 4096), p = h(p, 0), p = h(p, 0);
		p = h(p, 0), p = h(p, 0), p = h(p, -4096), p = h(p, 0);   // normal facing the viewer (-z native)
		p = h(p, i * 2048), p = h(p, 0);
		p = put(p, "\xFF\xFF\xFF\x80", 4);
	}
	p = u(p, 1), p = u(p, 10), p = f(p, 1), p = u(p, 0), p = u(p, 2);
	p = u(p, 0), p = u(p, 3), p = u(p, 0), p = u(p, 0), p = f(p, 0), p = f(p, 1), p = f(p, 5), p = f(p, 0); // shape 0
	p = u(p, 1), p = u(p, 3), p = u(p, 0), p = u(p, 0), p = f(p, 0), p = f(p, 0), p = f(p, 5), p = f(p, 1); // shape 1
	unsigned char *sz = p;
	p += 4;
	p = h(p, 0x10000 - 2), p = h(p, 0x1111), p = h(p, 0x2222);                // 2 literals
	p = h(p, TEX * TEX - 2 + overflow), p = h(p, 0x7FFF);                     // the rest repeated
	u(sz, p - sz - 4);
	return p - b;
}

int main(int argc, char **argv)
{
	static unsigned char b[512];
	static icon_vert out[30000];
	icon ic = {0};
	int n = synth(b, 0);
	assert(icon_parse(b, n, &ic) && ic.nv == 3 && ic.shapes == 2 && ic.nframes == 2 && ic.frames[1].nkeys == 2);
	assert(ic.tex[0] == 0x1111 && ic.tex[1] == 0x2222 && ic.tex[TEX * TEX - 1] == 0x7FFF);
	ic.ambient[0] = ic.ambient[1] = ic.ambient[2] = 1;
	int nv = icon_draw_list(&ic, 0, 0, 100, 200, 56, out);          // t = 0: shape 0 only
	assert(nv == 3);
	float x5 = out[0].x;
	for (int i = 0; i < 3; i++) assert(out[i].x >= 100 - 1e-3f && out[i].x <= 156 + 1e-3f && out[i].r == 128);
	icon_draw_list(&ic, 5, 0, 100, 200, 56, out);                   // t = 5: shape 1 only, twice as wide
	assert(fabsf(out[0].x - 128 - (x5 - 128) * 2) < 1e-3f);
	icon_free(&ic);
	for (int k = 0; k < n; k++) assert(!icon_parse(b, k, &ic));     // every truncation rejected
	assert(!icon_parse(b, synth(b, 1), &ic));                        // RLE past 128x128 rejected

	icon t = {0};
	if (argc == 2) { // real save: <dir>/icon.sys + its list icon
		static unsigned char sys[4096], ico[1 << 20];
		char path[512], name[65];
		snprintf(path, sizeof(path), "%s/icon.sys", argv[1]);
		FILE *fp = fopen(path, "rb");
		int ns = fread(sys, 1, sizeof(sys), fp);
		fclose(fp);
		assert(icon_sys_parse(sys, ns, &t, name, sizeof(name)));
		snprintf(path, sizeof(path), "%s/%s", argv[1], name);
		fp = fopen(path, "rb");
		int ni = fread(ico, 1, sizeof(ico), fp);
		fclose(fp);
		assert(icon_parse(ico, ni, &t));
		int nd = icon_draw_list(&t, 0, 0.7f, 0, 0, 56, out);
		for (int i = 0; i < nd; i++) assert(out[i].x >= -0.01f && out[i].x <= 56.01f && out[i].y >= -0.01f && out[i].y <= 56.01f);
		for (int i = 3; i < nd; i += 3) // back to front
			assert(out[i - 3].z + out[i - 2].z + out[i - 1].z <= out[i].z + out[i + 1].z + out[i + 2].z + 1e-4f);
		printf("%s: list icon %s, %d vertices, %d shapes, %d frames, texture %s\n", argv[1], name, t.nv, t.shapes,
		       t.nframes, t.tex ? "128x128" : "none");
		icon_free(&t);
		for (int k = 0; k < ni; k += 97) assert(!icon_parse(ico, k, &t)); // truncations of the real file
	}
	puts("icon selftest ok");
	return 0;
}
#endif
