#pragma once
// Phase-1 render engine: 1280x720 CT16 double buffer, PATH3, one GIF packet per frame.
// Colours are 0xRRGGBB. Textured sprites modulate: 0x808080 draws the texture unchanged (GS MODULATE, 0x80 = 1.0).
#define GFX_W 1280
#define GFX_H 720

typedef struct { unsigned long long tex0; } gfx_tex;

int gfx_init(void); // video mode + font atlas; returns 0 if the KROM font could not be loaded (text is then skipped)
// Uploads a texture into the VRAM pool. psm: GS_PSM_8 (clut = 256 ABGR words), GS_PSM_4 (clut = 16), GS_PSM_16
// (clut = NULL). pix and clut must be 16-byte aligned. Returns 0, changing nothing, if it does not fit.
int gfx_tex_upload(gfx_tex *t, const void *pix, int w, int h, int psm, const unsigned *clut);

void gfx_begin(void); // starts the frame on the back buffer
void gfx_rect(int x, int y, int w, int h, unsigned rgb);
void gfx_grad(int x, int y, int w, int h, unsigned a, unsigned b, int vertical); // a -> b, left-right or top-bottom
void gfx_sprite(const gfx_tex *t, int x, int y, int w, int h, int u, int v, int uw, int vh, unsigned rgb);
// Streams a CT16 image (w <= 256, h <= 384) from EE RAM and draws it 1:1, pixel-exact. pix must be 16-byte aligned
// and already written back from the D-cache (SyncDCache once after loading); it is read by DMA after this call.
void gfx_image(const void *pix, int w, int h, int x, int y);
void gfx_text(int x, int y, const char *s, unsigned rgb); // ASCII 32-126, 8x16 cell, '\n' starts a new line
void gfx_end(void);   // sends the frame and waits until the GS has drawn it
void gfx_flip(void);  // waits for vsync and shows the frame just drawn
