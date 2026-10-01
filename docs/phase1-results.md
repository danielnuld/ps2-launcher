# Phase 1 results

`demo.elf` (`make APP=demo`): launcher mock-up on the engine (`gfx.c`). 12 procedural 128×128 PSMT8 cards with their
own CLUT, drawn at 256×256, a KROM-atlas text line per card, a title, the selection frame and a full-height bar that
moves 16 px per frame. With no input for 5 s the selection moves by itself. Every 600 frames it shows the median and
maximum frame time (EE build + GS draw, COP0.Count), plus the missed vsyncs, and appends the first 3 windows to
`mass0:/demo.txt`.

## PCSX2 (2026-10-01): correctness only

- Text from the PSMT4 KROM atlas renders correctly (nibble order and glyph UVs are right). The cards show their PSMT8
  CLUT patterns as smooth ramps, so the CSM1 swizzle is right. No garbage in VRAM. The bar is never cut.
- Window #2: median 159-163 µs, max 172-175 µs, 0 missed vsyncs. These are **not** performance data (PCSX2 does not
  model GS draw time, phase-0 finding).
- On the first cold start PCSX2 showed "SIN USB"; on the second run it showed "GUARDADO en USB". `iop.c` waits 10 s for
  `mass0:`, and the emulated USB attach can take about that long. This is not a demo bug.

## Console (SCPH-75001, HDMI adapter) — 2026-10-01

Raw file: `docs/console/demo-2026-10-01.txt` (3 windows of 600 frames, hands-free auto-select).

| Window | Median | Max | Missed vsyncs |
|---|---|---|---|
| 1 | 1609 µs | 1740 µs | 0 |
| 2 | 1683 µs | 1792 µs | 0 |
| 3 | 1688 µs | 1719 µs | 0 |

**Gate: PASS.** Worst max 1792 µs = 21.5 % of the 8 333 µs gate (10.8 % of a 16 667 µs frame); 0 missed vsyncs in
1800 frames. Heaviest part (estimate, not broken down in this run): the full-screen background gradient. Phase-0
console B1 put a 1280×720 CT16 fill at 492 µs, about 30 % of this frame. The 5 visible 256×256 cards fill
~330 k px more.
