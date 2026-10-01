## Context

The design canvas is the source of truth for the look. Phase 3 left:
- A PSMT4 font atlas (8 pages) and a 256×192 cover band slot (12 pages).
- Max frame time 3302 µs on the console.
- A splash that blocks on file I/O: the main thread loads, so nothing animates during a read.

The GS has no blur and no real clipping shapes, so every soft or rounded element is a baked alpha texture tinted per
vertex.

## Goals / Non-Goals

**Goals:**
- The canvas's home screen and splash on the TV.
- Every element is built from a few engine primitives.
- The splash animates while loading.
- The frame gate is kept.

**Non-Goals:**
- 3D save icons (phase 5). The saves card shows the memory-card icon.
- Real game sources: the source chip shows USB, where the covers come from.
- Settings, sounds.

## Decisions

- **One generic primitive: the textured gradient quad** (`gfx_tquad`). It is a TRIANGLE_STRIP with UV, top and
  bottom colours and alpha (MODULATE, ABE). It covers:
  - Chrome glyphs: two quads split at the line's mid height, white→#E3EAF8 over #9FADCB→#F2F6FF, which matches the
    canvas `.chrome-text` stops.
  - Tinted icons, glows, and the rounded corners.
  - A sprite cannot do this: it takes one colour.
- **Rounded shapes:** `gfx_rrect(x, y, w, h, r, top, bottom)` is a 9-slice. It uses four quarter-disc tquads from a
  baked 64×64 disc and three gradient rects; a pill is `r = h / 2`. Outlined chips are an outer rrect in the border
  colour plus an inset rrect in the fill colour, because the design's fills are flat or near-flat under the chips.
- **Lines:** GS LINE with AA1 for the perspective grid, the horizon and the splash orbit. The 3 px orbit is 3
  concentric ellipses, rotated −12°, 96 segments, of which the first k are drawn. The grid is computed once on the
  EE: horizontals at perspective-spaced y, verticals to one vanishing point, alpha fading with depth.
- **UI atlas** (`tools/ui_art.py`, Pillow at 4× then downsampled, no new dependency):
  - One PSMT4 alpha atlas with every icon, the disc, a radial glow and the sparkle. Icons are single-colour, tinted per draw. The home screen uses one colour per icon, so the canvas sheet's
    two-tone accents were not needed.
  - The orb is a 96×96 RGBA image quantized to PSMT8 + an RGBA CLUT (radial chrome: smooth enough with 256 colours).
  - Icons are drawn from primitive calls that transcribe the canvas SVGs on the same 24-unit grid.
- **Fonts** (`tools/font.py` with a font list):
  - title: Unbounded 700 at 38 px.
  - ui: Sora 500 at 17 px.
  - mono: Space Mono at 14 px.
  - logo: Unbounded 800 at 60 px, only "ORBIT".

  Measured with Pillow, about 144 k glyph pixels go into one 512-wide PSMT4 atlas, ≤ 12 pages, checked by the
  tool's selftest. UTF-8: a 2-byte decode to a code point, then ASCII direct or a 16-entry extras table. Tracking is
  a global `gfx_tracking(px)`, like `gfx_alpha`.
- **VRAM (32 pages after the framebuffers):** cover band slot 256×128 = 8 pages, fonts ≤ 12, UI atlas ≤ 4, orb 1,
  plus CLUTs. A 368-line cover takes 3 bands. 128-line offsets keep the 16-byte alignment (65 536 and 47 104 bytes).
- **Loader thread and vsync semaphore:**
  - ps2sdk `graph_wait_vsync` busy-polls CSR, which would starve any other thread. The engine installs a VBLANK_S
    interrupt handler that signals a semaphore, and `gfx_flip` drains it and waits on it, so the render thread sleeps
    until the vblank.
  - The loader thread (lower priority) runs `iop_init`, the memory card scan and the cover loads. Its blocking SIF
    RPC waits on a semaphore too, so the render thread preempts it at every vblank.
  - Progress is shared through volatile ints. Nothing touches the pad before the loader marks the IOP ready.
- **Splash timeline:** frame-based at 60 Hz, using the canvas keyframes (spark, ring, towers, orb, shine, wordmark
  tracking 34→10 px, tagline, bar). The orb's highlight is a glow sprite moving across the orb instead of a clipped
  band (no clip shapes on the GS). The end is a 20-frame fade to black, then the home screen fades in over 20 frames.
- **Files:**
  - `demo.c` becomes `launcher.c` (`make APP=launcher`); it holds the screen, splash and loader code.
  - Engine additions stay in `gfx.c`.
  - `ponytail:` one app file until a second screen exists (settings); split then.

## Risks / Trade-offs

- [Thread + IOP reset from a non-main thread] → the PCSX2 run checks it first. The fallback is to run `iop_init` on
  the main thread before the splash and thread only the cover loads.
- [AA1 lines differ between PCSX2 and the hardware] → judged on the TV. The fallback is 1 px gouraud quads.
- [More draw calls per frame (≈ 300 quads + 6 cover streams)] → gate measured; phase 3 used 40 % of the gate.
- [The line above glyphs (phase-3 known issue) also affects the new fonts] → if it persists, take a TV photo, then
  the next tests from `docs/phase3-results.md`.
