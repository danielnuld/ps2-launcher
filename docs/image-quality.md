# Image quality on the real PS2: what we learned

Collected at the user's request (2026-10-01) for future work. Target: SCPH-75001 (ROM 2.20) + HDMI adapter,
1280×720 with a CT16 (RGB555) double-buffered framebuffer. Every item below was seen on the console, often not in
PCSX2. Each row gives the symptom, the cause, the technique that fixed it, and where it lives.

## 1. The console's GS is the reference, not PCSX2

| Symptom on the TV | Cause | Fix | Where |
|---|---|---|---|
| Thin line above every glyph (invisible in PCSX2) | With point sampling, the console's GS samples textured quads half a texel above where PCSX2 does, so it reads the row above the glyph | Add +½ texel (UV +8 in 1/16 units) to every point-sampled quad. Found with an on-console A/B pattern: normal / binary alpha / UV +½ / PABE+COLCLAMP; only UV +½ removed it | `gfx.c` `tquad16` |
| (Precaution) behaviour that depends on what ran before | The console keeps GS registers from the previous program (OSD, loader); PCSX2 starts at 0. `draw_setup_environment` does not set TEX1 | Write every register we depend on at init: TEX1 (point, LCM=1, MXL=0), DTHE, DIMX | `gfx.c` `gfx_init` |
| Splash animation invisible, only the final frame | After a video mode change the HDMI adapter and TV take seconds to show a picture | Hold 150 black frames (2.5 s) before any intro animation; keep loading meanwhile | `launcher.c` `HOLD` |
| "4 ms per frame" in logs while the animation ran at 60 Hz | `clock()` runs about 4× short on the PS2 | Time with COP0.Count (294.912 MHz), summed per frame to dodge its 14.6 s wrap | `launcher.c` splash loop |

## 2. 16-bit framebuffer: bands in smooth gradients

The framebuffer keeps 5 bits per channel, and the GS truncates (8-bit value → `v >> 3`). Any smooth gradient bands
unless it is dithered. The user rejected the GS ordered dither over the whole screen (phase-0 modetest), but
accepted it per element.

| Content | Technique | Where |
|---|---|---|
| Static images (covers, orb) | **Pre-dither offline**: gamma-correct Lanczos resize, then Floyd–Steinberg error diffusion straight to the RGB555 levels `k·8`, k = 0..31. Drawn 1:1 with point sampling, the pixels arrive unchanged | `tools/covers.py` `quantize`; `tools/ui_art.py` `palettize` |
| Paletted images (PSMT8) | Dither **first**, then build the palette from the exact RGB555 colours (+ alpha levels). Past 255 entries, merge the rarest colours into their nearest kept one. Palette entries must stay on `k·8` or the framebuffer truncation re-bands them | `tools/ui_art.py` `palettize` |
| Live gradients (background, glows, light towers) | **GS dither (DTHE + DIMX) per draw**, on only around these draws | `gfx_dither()`, used in `launcher.c` |
| Pre-dithered content | **Never** draw it with DTHE on: the GS adds −4..+3 before truncating and spoils exact `k·8` values | `launcher.c` (dither off around the orb, covers, text) |

## 3. Alpha and texture precision

| Symptom | Cause | Fix | Where |
|---|---|---|---|
| Rings in large glows | 4-bit alpha (PSMT4) = 16 steps, stretched over hundreds of pixels | 8-bit alpha: the glow gets its own 64×64 PSMT8 texture with a 256-level white alpha CLUT | `ui_art.py` `glow`; `gfx_glow` |
| (fine) glyph and icon edges | 4-bit alpha is enough for 1:1 anti-aliased edges a few pixels wide | Keep PSMT4 atlases for text and icons (they save VRAM) | `font.py`, `ui_art.py` |

## 4. Scaling softens: bake every size you show at rest

The GS only has bilinear filtering, and downscaling with it also aliases (few texels sampled).

| Element | Technique | Where |
|---|---|---|
| Covers | Two baked sizes (256×368 selected, 184×264 row). Pixel-exact at rest; bilinear only during the ~0.4 s grow/shrink animation, then snap to whole-pixel rest positions | `covers.py` `SIZES`; `launcher.c` `cover` |
| Orb | Baked at 56 px (header) and 110 px (splash); scaled only during the intro animation | `ui_art.py` `ORBS`; `gfx_orb` |
| Rounded corners | A disc baked at every diameter the UI uses (2 × each radius), so each corner is a 1:1 point-sampled quadrant. The 64 px disc squeezed to 3 px made the position dots jagged | `ui_art.py` `DISCS`; `gfx_rrect` |
| Resampling quality (PC) | Lanczos in linear light; resizing in sRGB darkens fine detail | `covers.py` `resize` |

## 5. Lines and outlines

| Symptom | Cause | Fix | Where |
|---|---|---|---|
| The splash orbit looked "lined", not smooth | Three 1 px AA1 lines stacked side by side show seams on the console | A **ribbon**: two gouraud triangle strips along the curve, alpha full at the centre and 0 at both edges over the half width | `gfx_ribbon` |
| (fine) Perspective grid, horizon | Single 1 px lines with an alpha gradient look right | `gfx_line` (AA1) | `launcher.c` `floor_grid` |

## 6. Text

- Bake the fonts offline (Pillow) into a PSMT4 alpha atlas; keep 2 blank texels between glyphs.
- Draw one quad per glyph, 1:1, at integer positions (+½ texel, section 1).
- Chrome text is two gradient quads per glyph, split at 55 % of the ascent (canvas `.chrome-text` stops).
- UTF-8 is decoded to ASCII plus a small extras table (á é í ó ú ñ ü ¿ ¡ ·).

## 7. How to debug image problems on the console

1. Reproduce in PCSX2 first. If PCSX2 is clean, suspect a hardware difference or the video chain.
2. Rule out the video chain with a pattern of solid untextured bars next to the suspect content. A halo on the bars
   means the TV or adapter (sharpening).
3. Bisect on the console with one screen showing labelled variants (A/B/C/D), each changing one hypothesis. One
   run answers several questions.
4. Log numbers to `mass0:/*.txt` with COP0 timing; never trust `clock()`.
