## Why

The launcher has to show real game covers, and in good quality (user request, 2026-10-01). Phase 1 left 256 KiB of
VRAM for textures, and the framebuffer is CT16 (RGB555). So quality depends on how each cover is turned into 15-bit
colour and whether the GS has to scale it. VRAM size is not the limit: a cover can be streamed in per draw.

## What Changes

- `tools/covers.py` (PC, Python + Pillow + NumPy): converts PNG/JPG covers into `.c16` files. Each cover is resized
  once, gamma-correct with Lanczos, to the exact on-screen size (256×368), then reduced to RGB555 with Floyd–Steinberg
  error diffusion, or with plain rounding (`--no-dither`) for comparison.
- `gfx.c`: `gfx_image()` streams a CT16 image from EE RAM into one reserved VRAM slot and draws it 1:1 with no
  filtering, so the covers are pixel-exact. Several covers per frame reuse the slot, because the GIF processes
  transfers and draws in order.
- `demo.c`: the procedural cards are replaced with covers loaded from `mass0:/covers/*.c16` and
  `mass0:/covers_nd/*.c16`. SELECT switches between the dithered and non-dithered sets, so the user can pick on the TV.
- Results go in `docs/phase2-results.md`. Sample covers come from xlenore/ps2-covers and are kept out of git.

## Capabilities

### New Capabilities
- `cover-art`: PC-side conversion to `.c16`, loading from USB, and pixel-exact streamed drawing of covers.

### Modified Capabilities

## Impact

- New `tools/covers.py`. `gfx.c` / `gfx.h` gain `gfx_image()`. `demo.c` changes from cards to covers.
- PC needs Python with Pillow and NumPy (both installed). The PS2 side needs no new libraries and no image decoders.
- EE RAM: 184 KiB per cover per set, so 15 covers × 2 sets ≈ 5.5 MB out of 32 MB (ps2tek:75).
