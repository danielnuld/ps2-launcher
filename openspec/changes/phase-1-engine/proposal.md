## Why

Phase 0 fixed the video mode on measured numbers and on the user's TV: 1280×720 CT16, double buffer, no dither,
DISPLAY DX=300 DY=27 (`docs/phase0-results.md`). Right now every test ELF sets up video and draws on its own
(`bench.c` and `modetest.c`). The launcher UI needs one engine that owns video, drawing, textures, text and input,
and we need a measured frame time from a launcher-like scene before building the UI on top of it.

## What Changes

- `gfx.c` / `gfx.h`: the render engine. It sets up the phase-0 mode, runs the frame loop with a vsync flip, and draws
  batched flat, gradient and textured sprites over PATH3. It also handles the texture VRAM pool, text drawn from a
  KROM glyph atlas, and a frame timer.
- `demo.c` (`make APP=demo`): a launcher mock-up (grid of textured cards, title text, D-pad scrolling) that measures
  the frame time and appends it to `mass0:/demo.txt`.
- `modetest.c` stays as it is, as the calibration tool. `bench.c` stays as the PATH3 baseline.
- Results go in `docs/phase1-results.md`, with PCSX2 and console numbers kept apart.

## Capabilities

### New Capabilities
- `render-engine`: video setup in the chosen mode, frame loop, sprite drawing, texture residency, text, frame timing.

### Modified Capabilities

## Impact

- New files `gfx.c`, `gfx.h`, `demo.c`, and a `test` target in the `Makefile` that runs host self-checks.
  `iop.c` is reused unchanged.
- Only ps2sdk libraries that are already linked (graph, draw, dma, packet, font, pad).
- VRAM budget: 256 KiB (65 536 words) is left after the two CT16 framebuffers (phase-0 results), and every texture
  and the font atlas must fit in it.
