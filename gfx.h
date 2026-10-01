#pragma once
// Render engine: 1280x720 CT16 double buffer, PATH3, one GIF packet per frame. Colours are 0xRRGGBB everywhere
// (textured draws modulate a white texel by that colour). Alpha: 0..0x80, set by gfx_alpha (0x80 = opaque).
#define GFX_W 1280
#define GFX_H 720
#define GFX_GLYPHS 111 // ASCII 32-126, then GFX_EXTRAS Latin-1 letters (font_extras, tools/font.py)
#define GFX_EXTRAS 16

typedef struct { unsigned long long tex0; } gfx_tex;
typedef struct { unsigned short u, v; unsigned char w, h; signed char xo, yo; unsigned char adv; } gfx_glyph;
typedef struct { const gfx_glyph *g; int line_h, asc; } gfx_font;
// Unbounded 700 38 px, Sora 500 17 px, Space Mono 14 px, Unbounded 800 60 px ("ORBIT" only) — font_data.c
extern const gfx_font gfx_font_title, gfx_font_ui, gfx_font_mono, gfx_font_logo;

int gfx_init(void); // video mode, black screen, atlases, vsync semaphore; returns 0 if VRAM could not hold them
// Uploads a texture into the VRAM pool. psm: GS_PSM_8 (clut = 256 ABGR words), GS_PSM_4 (clut = 16), GS_PSM_16
// (clut = NULL). pix and clut must be 16-byte aligned. Returns 0, changing nothing, if it does not fit.
int gfx_tex_upload(gfx_tex *t, const void *pix, int w, int h, int psm, const unsigned *clut);

void gfx_begin(void);        // starts the frame on the back buffer
void gfx_alpha(int a);       // alpha for what follows: 0x80 = opaque, less = blended
void gfx_tracking(int px);   // extra pixels after every glyph (letter spacing), 0 = font default
void gfx_rect(int x, int y, int w, int h, unsigned rgb);
void gfx_grad(int x, int y, int w, int h, unsigned a, unsigned b, int vertical); // a -> b, left-right or top-bottom
void gfx_rrect(int x, int y, int w, int h, int r, unsigned top, unsigned bottom); // rounded rect; pill: r = h / 2
void gfx_line(float x0, float y0, float x1, float y1, unsigned rgb, int a0, int a1); // anti-aliased, alpha a0 -> a1
void gfx_ribbon(const float *px, const float *py, int n, float width, unsigned rgb, int a); // soft thick polyline
// UI atlas entry (ui_data.h UI_*) at native size, or stretched to w x h with a vertical gradient (glows, sparkles)
void gfx_icon(int id, int x, int y, unsigned rgb);
void gfx_icon_scaled(int id, int x, int y, int w, int h, unsigned top, unsigned bottom);
void gfx_orb(int x, int y, int size); // chrome orb, size px wide (pixel-exact at 56 and 110)
void gfx_glow(int x, int y, int w, int h, unsigned top, unsigned bottom); // soft radial glow, 256 alpha levels
void gfx_dither(int on); // GS dither for what follows: soft gradients and glows on, text / covers / orb off
void gfx_sprite(const gfx_tex *t, int x, int y, int w, int h, int u, int v, int uw, int vh, unsigned rgb);
// Streams a CT16 image (w <= 256, any h) from EE RAM in 128-line bands and draws it at (x, y) sized dw x dh:
// pixel-exact when dw x dh == w x h, bilinear otherwise. pix must be 16-byte aligned and written back from the
// D-cache (SyncDCache once after loading); it is read by DMA during this call.
void gfx_image(const void *pix, int w, int h, int x, int y, int dw, int dh);
// UTF-8 text; y = top of the line. chrome: the canvas's chrome fill (white over steel, split at 55 % of the ascent)
void gfx_text(const gfx_font *f, int x, int y, const char *s, unsigned rgb);
void gfx_text_chrome(const gfx_font *f, int x, int y, const char *s);
int gfx_text_width(const gfx_font *f, const char *s); // first line, in pixels, with the current tracking
void gfx_end(void);   // sends the frame and waits until the GS has drawn it
void gfx_flip(void);  // sleeps until vsync (other threads run meanwhile) and shows the frame just drawn
