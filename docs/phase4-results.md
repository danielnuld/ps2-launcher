# Phase 4 results: ORBIT style

`launcher.elf` (`make APP=launcher`, formerly `demo.elf`) implements the approved design canvas
(https://claude.ai/artifact/94TdvSa23Yh4veqXDpLv7E): the animated splash and the home screen.

Art pipeline (PC, Pillow, no new dependency):
- `python tools/font.py` writes `font_data.c`: Unbounded 700 38 px, Sora 500 17 px, Space Mono 14 px, Unbounded
  800 60 px ("ORBIT"), plus á é í ó ú ñ Á É Í Ó Ú Ñ ü ¿ ¡ ·. 512×384 PSMT4 = 12 pages.
- `python tools/ui_art.py` writes `ui_data.c` / `ui_data.h`: 21 entries (icons for actions and game sources, the
  memory card, pad symbols, sparkles, a disc for rounded shapes, a glow), 256×128 PSMT4 = 2 pages; the orb is 96×96
  PSMT8 + an RGBA CLUT.

VRAM (32 pages after the framebuffers): cover band slot 256×128 = 8, fonts 12, UI 2, orb 2, plus CLUTs.

Engine additions:
- `gfx_tquad` (textured gradient quad; chrome text, tinted icons, glows).
- `gfx_rrect` (9-slice from the disc), AA `gfx_line`, `gfx_tracking`, UTF-8 text.
- vsync on a semaphore (VBLANK_S handler) instead of the busy-polling `graph_wait_vsync`, so a loader thread
  (priority 0x40, render 0x20) loads while the splash animates.

## PCSX2 (2026-10-01): correctness only

- Splash timeline as in the storyboard: spark, the orbit drawing itself while the towers rise, the orb with its
  highlight, then "ORBIT" in chrome with the tracking closing, the tagline and the bar. The orbit's lower half
  passes in front of the orb.
- The loader thread loaded 15 covers × 2 sizes while the splash ran; the splash kept animating.
- Home screen matches the "Inicio" board:
  - Orb and chrome title.
  - SLUS / DVD / USB chips and the "Sin saves" card (PCSX2's card holds only OPL).
  - Perspective floor, horizon, sparkles.
  - Chrome frame on the selected cover.
  - Position ticks, and hints with the pad symbol, the action icon and the label ("Datos técnicos" with its accent).
- Right arrow: the carousel moves and the header changes to FFX.
- Times (not representative): median 875 µs, max 982 µs, 0 missed.

## Console, first run (SCPH-75001, build `d4e8ec8`) — 2026-10-01

Raw file: `docs/console/orbit-2026-10-01.txt`.

| Window | Overlay | Median | Max | Missed vsyncs |
|---|---|---|---|---|
| 1 | off | 3877 µs | 4795 µs | 0 |
| 2 | off | 4327 µs | 4805 µs | 0 |
| 3 | on | 4007 µs | 4802 µs | 0 |

Load: 15 covers × 2 sizes in 974 ms, inside the loader thread. Times pass: max 4805 µs = 58 % of the 8 333 µs gate
(phase 3: 3302 µs; the extra ~1.5 ms is the new art: grid lines, rounded shapes, chrome text, glows).

User report: it works, but (1) the text still shows the thin line above the glyphs (phase-3 known issue) and (2) the
splash showed only the static logo, with no animation.

- (2) Most likely cause: after the mode change the HDMI adapter and the TV take seconds to show a picture, and the
  timeline (0-3.2 s) had already run by then. PCSX2 has no such delay. Fix: 150 black frames (2.5 s) before the
  timeline, while the loader keeps loading. `launcher.txt` now also logs the splash's frames against wall-clock
  ms, which shows whether the render thread ran at 60 Hz while the loader worked.
- (1) START opens a test pattern. It has solid white bars 1/2/4/20 px (no texture), the title font in white and in
  chrome, Sora and Mono, dark text on white, and icons. If the line also shows above the solid bars, the cause is
  the video chain (scaler sharpening), not the font. Also to try: TV sharpness at 0.

## Console, second run (build `d82ac04`) — 2026-10-01

Raw file: `docs/console/orbit-b-2026-10-01.txt`.
- Splash: the user saw the full animation. The 2.5 s black hold fixed it (cause: HDMI/TV relock).
- The log said "696 frames in 3006 ms = 4 ms/frame". That reading comes from `clock()`, which on the PS2 runs
  about 4× short: the animation the user saw ran at 60 Hz. Load times measured with `clock()` (684, 954, 974,
  3432 ms) are not reliable either. The splash log now also uses COP0.
- Home (overlay on): max 5172 µs, 0 missed vsyncs.
- Glyph line: on the START pattern it shows **only on the letters, not above the solid bars**, so the video chain
  is ruled out. The START pattern now draws the same text 4 ways to find the cause: A normal, B binary-alpha CLUT
  (no anti-aliasing), C UV +1/2 texel, D PABE=0 + COLCLAMP=1 written first.

## Glyph line: cause found (2026-10-01)

User, test pattern on the console: row **C (UV +1/2 texel)** is the best. The console's GS samples point-filtered
quads half a texel above where PCSX2 does, so the row above each glyph showed as a line (the 2-texel padding did
not help because the shift reads into the glyph's own top edge).
- Fix: `tquad16` adds +8 (1/16 texel units) to every UV when point sampling. That covers text, icons and covers at
  rest, and also removes a possible line at the covers' band edges.
- The diagnostic variants (binary CLUT, `gfx_reg`) are removed.
- Known difference: PCSX2 may now show glyphs half a texel lower. The console is the reference.

## Banding (user: "the gradient shows a lot in the splash and the header orb, it looks old")

Three causes, three fixes:
- The radial glow was in the PSMT4 atlas: 16 alpha levels gave rings. It is now its own 64×64 PSMT8 texture with
  256 alpha levels (`gfx_glow`).
- The orb was quantized to 256 colours and then truncated to the CT16 framebuffer's 5 bits per channel. It is now
  baked at its two drawn sizes (56 header, 110 splash), Floyd-Steinberg dithered straight to the RGB555 levels
  (same method as the covers), palettized ≤ 255 + transparent with the rarest colours merged. It is pixel-exact at
  those sizes.
- Large gradients on CT16: GS dithering (DTHE + gsKit DIMX) is now switched on per draw (`gfx_dither`), only for
  background gradients, glows and towers. Text, covers, icons and the pre-dithered orb stay undithered.

VRAM now: slot 8 + fonts 12 + UI 2 + glow 1 + orbs 3 = 26 of 32 pages.

## Console: pending

Gate: max ≤ 8 333 µs, 0 missed vsyncs (overlay on, 30 s hands-free), no splash stutter, user approves the look.
