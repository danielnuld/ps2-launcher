## Why

The user wants several ways to browse the games (2026-10-01): the carousel, a grid and a list. Every view must
animate in the launcher's style and look good. One horizontal row is slow to scan with many games, and a list reads
titles fastest.

## What Changes

- Three views:
  - **Carrusel**: today's row.
  - **Cuadrícula**: 7 columns of 128×184 covers. The rows scroll smoothly and the focused tile grows with the
    chrome frame and glow.
  - **Lista**: title rows on the left, with a highlight pill that glides; the selected game's 256×368 cover sits on
    the right.
- □ cycles the views. Every cover flies from its place in the old view to its place in the new one, staggered
  outward from the selection. The view's name shows briefly.
- The chosen view is saved in `mass0:/orbit/estado.ini` and restored at boot.
- A third cover size, 128×184, is made on the EE from the 256×368 cover: a 2×2 average in linear light, then
  Floyd–Steinberg. It is kept in RAM only.
- New UI icons for the views. The footer gets a □ "Vista" hint.

## Capabilities

### New Capabilities
- `game-views`: the grid and list views, view switching with its transition, and the remembered view.

### Modified Capabilities
None at requirement level. The carousel keeps its launcher-ui requirements (layout, easing, pixel-exact rest); it is
rebuilt on the shared per-cover animation without visible change.

## Impact

- `launcher.c`:
  - a layout function per view;
  - per-cover animated rect, alpha and focus;
  - input per view;
  - half-size covers.
- `tools/ui_art.py`: carousel, grid and list icons; `ui_data.c` regenerated.
- No new files on the USB besides `estado.ini`.
