#pragma once
// Render engine: 1280x720 CT16 double buffer, PATH3, one GIF packet per frame.
// Colours are 0xRRGGBB. Textured sprites modulate: 0x808080 draws the texture unchanged (GS MODULATE, 0x80 = 1.0).
#define GFX_W 1280
#define GFX_H 720

typedef struct { unsigned long long tex0; } gfx_tex;
typedef struct { unsigned short u, v; unsigned char w, h; signed char xo, yo; unsigned char adv; } gfx_glyph;
typedef struct { const gfx_glyph *g; int line_h; } gfx_font; // ASCII 32-126, baked by tools/font.py
extern const gfx_font gfx_font_title, gfx_font_body;      // Inter 36 px bold, 22 px medium (font_data.c)

int gfx_init(void); // video mode, black screen, font atlas; returns 0 if VRAM could not hold the slot or the font
// Uploads a texture into the VRAM pool. psm: GS_PSM_8 (clut = 256 ABGR words), GS_PSM_4 (clut = 16), GS_PSM_16
// (clut = NULL). pix and clut must be 16-byte aligned. Returns 0, changing nothing, if it does not fit.
int gfx_tex_upload(gfx_tex *t, const void *pix, int w, int h, int psm, const unsigned *clut);

void gfx_begin(void);  // starts the frame on the back buffer
void gfx_alpha(int a); // vertex alpha for what follows: 0x80 = opaque (no blending), less = blended
void gfx_rect(int x, int y, int w, int h, unsigned rgb);
void gfx_grad(int x, int y, int w, int h, unsigned a, unsigned b, int vertical); // a -> b, left-right or top-bottom
void gfx_sprite(const gfx_tex *t, int x, int y, int w, int h, int u, int v, int uw, int vh, unsigned rgb);
// Streams a CT16 image (w <= 256, any h) from EE RAM in 192-line bands and draws it at (x, y) sized dw x dh:
// pixel-exact when dw x dh == w x h, bilinear otherwise. pix must be 16-byte aligned and written back from the
// D-cache (SyncDCache once after loading); it is read by DMA during this call.
void gfx_image(const void *pix, int w, int h, int x, int y, int dw, int dh);
void gfx_text(const gfx_font *f, int x, int y, const char *s, unsigned rgb); // y = top of the line
int gfx_text_width(const gfx_font *f, const char *s);                         // first line, in pixels
void gfx_end(void);   // sends the frame and waits until the GS has drawn it
void gfx_flip(void);  // waits for vsync and shows the frame just drawn
