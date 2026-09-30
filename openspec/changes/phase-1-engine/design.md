## Context

In phase 0, all four candidate modes passed the GS/DMA gate with more than 93 % of the frame left (console,
`docs/phase0-results.md`). The mode was therefore chosen on the TV: CT16 double buffer looked best, and CT32 single
buffer flickered and cut the text. `modetest.c` already has working code for the mode setup, the DIMX/DTHE writes,
the A+D primitives and the vsync flip. The engine starts from that code.

## Goals / Non-Goals

**Goals:** a small engine API that the launcher UI will use. A launcher-like demo scene. A measured frame time on
the console, checked against the gate.

**Non-Goals:** VU1 microcode or EE asm. No measurement shows a hotspot yet: phase-0 draw costs are ≤ 6 % of a frame,
and the project rule allows asm/VU1 only after a COP0.Count measurement. Also out of scope: loading real cover art
from storage, launching games, animations and easing, and 1080i.

## Decisions

- **API = immediate mode over one packet per frame.** `gfx_begin()`, `gfx_rect/grad/sprite/text(...)`, and
  `gfx_end()`, which sends the packet, waits for FINISH, waits for vsync and flips. This is the same path that phase 0
  measured (PATH3, `dma_channel_send_normal` + `draw_finish`), so the phase-0 numbers still apply. Rejected: a retained
  scene graph, because nothing needs it yet.
- **Two packets, alternating.** The EE writes one packet while the DMA may still read the other. The first version
  waits for the send before returning (as modetest does). Overlapping EE build and GS draw comes later, and only if the
  gate shows that the frame time needs it.
- **Texture pool = bump allocator over the 65 536 words after the framebuffers, with no free.** The UI loads its
  textures at startup. PSMT8 128×128 = 2 pages = 4096 words (sources: GS page table), so 12 cards use 49 152 words and
  the font atlas and CLUTs fit in the rest (estimate, confirmed by the allocator at run time). An allocation that does
  not fit returns 0. `ponytail:` no eviction; add an LRU when the UI shows more covers than fit in the pool.
- **Font atlas = KROM glyphs rasterized on the EE into one PSMT4 texture** with a 16-colour CLUT (entries 0/1 =
  transparent/white), uploaded once. This replaces fontx's one point per pixel (sources: fontx row), which phase 0
  could only afford once per screen. The KROM glyph size is read from the loaded font header at run time and not
  hard-coded (not yet in `docs/sources.md`: add a row when it is measured).
- **Demo cards are generated procedurally** (PSMT8 patterns + CLUT) so the ELF needs no assets. Cover loading belongs
  to a later phase.
- **Frame timing:** COP0.Count from `gfx_begin` to after FINISH (EE build + GS draw), plus the time between two vsync
  returns. A delta above 1.5 × 16 667 µs counts as a missed vsync. Median and maximum over 600 frames.
- **Files:** `gfx.c` + `gfx.h` (engine), `demo.c` (app), and `iop.c` reused. `modetest.c` and `bench.c` are not ported
  to the engine; they are finished tools.

## Risks / Trade-offs

- [PSMT4/PSMT8 upload needs the GS's swizzled page layout or the correct TRXPOS/BITBLTBUF widths] → check in PCSX2
  first against a known pattern. PCSX2 is trusted for correctness only (phase-0 finding).
- [256 KiB pool is small for real covers] → out of scope here. The number is recorded and the next phase decides
  between streaming uploads (~250 µs per 256 KiB, console B3) and smaller textures.
- [CT16 banding on gradients] → the user accepted it on the TV (variant 1 over variant 2). Dithering can be turned on
  again with one DTHE write if the UI needs it.
