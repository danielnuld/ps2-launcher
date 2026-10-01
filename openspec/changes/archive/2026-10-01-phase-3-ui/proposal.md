## Why

The engine and the covers pass on the console. The next step is the launcher's real interface. The user chose a
horizontal carousel with the selected cover bigger in the centre and the game's info under it, smooth animation, and
the dark-blue style (2026-10-01). The 8×15 BIOS font is too small and pixelated for a 720p UI, and the cover quality
reached in phase 2 must not be lost when the selected cover grows.

## What Changes

- **BREAKING (engine API):** `gfx_text` draws with an anti-aliased Inter font baked on the PC into a PSMT4 atlas
  that ships inside the ELF, in two sizes (title, body). It has per-glyph advance widths, alpha blending, and
  `gfx_text_width` for centring. The KROM atlas is removed.
- `tools/font.py`: renders Inter (OFL, `tools/fonts/`) into `font_data.c`.
- `tools/covers.py` writes two sizes per cover: `<serial>.c16` (256×368, the selected size) and `<serial>_s.c16`
  (184×264, the carousel size).
- `gfx_image` takes a destination size. It stays point-sampled when the size matches and becomes bilinear only while
  scaling. It streams in bands of 192 lines, so the cover slot shrinks and leaves VRAM for the font atlas.
- `gfx_alpha()` sets the alpha for the primitives that follow (fades, translucent frames).
- `demo.c` becomes the UI:
  - Header ("PS2 LAUNCHER", game count).
  - Carousel. Each cover grows into the centre as the selection moves, and at rest everything is pixel-exact.
  - Title and serial of the selected game, fading in.
  - Button hints.
  - Splash with a progress bar.
  - SELECT toggles a technical overlay with the phase-1 timing; timing is still logged to `mass0:/demo.txt`.
  - Only the Floyd-Steinberg set is loaded (user's choice, phase 2).

## Capabilities

### New Capabilities
- `launcher-ui`: carousel layout, animation, info panel, hints, splash, debug overlay.

### Modified Capabilities
- `render-engine`: Text requirement (baked Inter font instead of the KROM atlas).
- `cover-art`: two sizes per cover; banded streaming; pixel-exact at rest with bilinear filtering only while scaling.

## Impact

- New: `tools/font.py`, `tools/fonts/Inter.ttf` + `OFL.txt`, `font_data.c` (generated, committed).
- Changed: `gfx.c` / `gfx.h`, `tools/covers.py`, `demo.c`, `Makefile`.
- Covers on the USB must be regenerated: both sizes go in `covers/`, and `covers_nd/` is no longer used.
