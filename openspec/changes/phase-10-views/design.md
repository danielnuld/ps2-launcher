## Context

The carousel computes each cover's rect from one eased scalar `s` (launcher.c `cover`). Covers stream through one
VRAM slot per draw (cover-art spec). Phase 2 measured about 620 µs per 256×368 `gfx_image` call on the console. That
is an upper bound and includes GS draw time. Grid tiles must therefore be small textures, not big ones scaled down.

## Goals / Non-Goals

**Goals:**
- Three views with smooth, consistent motion, and pixel-exact rest positions.
- One mechanism for every animation: in-view moves, focus, and view-switch morphs.

**Non-Goals:**
- Sorting or filtering, and per-source grouping: later.
- Changing the carousel's look.

## Decisions

- **Per-cover animated state.** Each game keeps a current rect, alpha and focus. Each view only computes the
  target for each game; every frame the current values ease toward the targets.
  - Easing: 0.2 per frame, as the carousel's `s`; 0.16 during a view switch.
  - Rest: values snap to the target when close, so rests are pixel-exact.
  - Why: the same code gives the carousel slide, the grid and list focus, and the cover flights between views.
  - Alternative: a crossfade between views. It is cheaper, but it shows no relation between the layouts.
- **Stagger.** On a switch, each cover waits `min(|i − sel|, 10) × 2` frames before it starts easing (design
  choice, D).
- **Source texture by size.**
  - At rest: the image whose size equals the rect, 1:1.
  - While animating: the smallest image at least as large as the rect, bilinear, so the GS never minifies by more
    than 2× without mipmaps.
- **Half-size covers on the EE.** 2×2 means in linear light (32-entry LUT, then powf back) through
  `gfx_fs_dither`: about 47 KB each in RAM.
  - Grid cost estimate (E, from phase 2's 620 µs per 188 KB): 21 tiles × ~155 µs ≈ 3.3 ms. The gate measures it.
- **Edge fades instead of scissoring.** Alpha falls to 0 outside the visible area over 60 px (grid) or 26 px (list rows; 60 left ghosts over the header in PCSX2). Covers below
  1 % alpha are skipped.
- **Grid layout.** 7 × 128 px tiles with 40 px gaps (1136 px, centred). Rows start at y = 176 with a pitch of
  220; scroll keeps the selection's row among the two visible rows. The focused tile scales ×1.12.
- **List layout.**
  - Rows are 52 px from x = 64 to 720, starting at y = 166, with 8 visible (a 9th met the footer status line in PCSX2).
  - Scroll keeps the selection 4 rows from the top when it can.
  - The highlight pill eases to the selected row.
  - The big cover sits at (880, 182).
  - Non-selected covers target their row's left edge at 36×52 with alpha 0, so they shrink into the list.
- **State file.** `estado.ini [ui] vista`. It is rewritten when the view changes. The user's `config.ini` is only
  read, never rewritten.

## Risks / Trade-offs

- [Grid frame time on the console] → measured in the gate. If it is over budget, drop to 6 columns or make
  smaller tiles.
- [The morph draws big textures scaled for a few frames] → those frames may cost more. The gate counts missed
  vsyncs during switches.
- [RAM: 47 KB per game for half covers] → 128 games ≈ 6 MB of 32 MB.
