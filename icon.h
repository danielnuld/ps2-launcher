#pragma once
// PS2 save icons (phase 9): icon.sys + the list icon file -> lit, animated triangles for the saves card.
typedef struct { int shape, nkeys; float *t, *v; } icon_frame;
typedef struct {
	int nv, shapes, frame_length, nframes;
	float speed;
	short *pos;            // shapes x nv x 3, fixed point /4096, up = -y
	short *nrm, *uv;       // nv x 3, nv x 2
	unsigned char *rgba;   // nv x 4
	icon_frame *frames;
	unsigned short *tex;   // 128 x 128 CT16, 64-byte aligned; NULL = untextured (white)
	float light_dir[3][3], light_col[3][3], ambient[3];
	float cx, cy, cz, half; // shape 0: centre and half the larger of the XZ diameter and the height (fit)
} icon;

#include "gfx.h"
typedef gfx_vtx icon_vert; // x, y in pixels, z 0..1 (1 = nearest), u, v texels (0..128), GS colour (0x80 = x1)

int icon_sys_parse(const unsigned char *d, int n, icon *ic, char *list_name, int name_n); // lights + file name
int icon_parse(const unsigned char *d, int n, icon *ic); // 1 if valid; nothing is read past d + n
void icon_free(icon *ic);
// the icon at animation time t (frames) turned by angle (radians), fitted into a box x0, y0, size px; fills
// 3 * (nv / 3) vertices in triangle order, back to front. Returns the vertex count.
int icon_draw_list(const icon *ic, float t, float angle, float x0, float y0, float size, icon_vert *out);
