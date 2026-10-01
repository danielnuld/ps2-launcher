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

## Console (SCPH-75001): pending

Gate: max ≤ 8 333 µs, 0 missed vsyncs (overlay on, 30 s hands-free), no splash stutter, user approves the look.
