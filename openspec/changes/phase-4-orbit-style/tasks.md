## 1. Art pipeline

- [x] 1.1 `tools/font.py`: font list (title/ui/mono/logo), UTF-8 extras, ≤ 12-page check; regenerate `font_data.c`; remove Inter
- [x] 1.2 `tools/ui_art.py`: icons (actions, sources, memory card, pad buttons) + disc + glow + sparkle → PSMT4 atlas; orb → PSMT8 + CLUT; `ui_data.c`; `--selftest`

## 2. Engine

- [x] 2.1 `gfx_tquad`, `gfx_rrect` (pill = r h/2), `gfx_line` (AA1), `gfx_tracking`, chrome text, UTF-8 text/width
- [x] 2.2 Cover band slot 256×128
- [x] 2.3 vsync semaphore (VBLANK_S handler) in `gfx_flip`

## 3. Launcher

- [x] 3.1 Rename `demo.c` → `launcher.c` (`make APP=launcher`, `mass0:/launcher.txt`)
- [x] 3.2 Loader thread: IOP/USB, memory cards, covers; shared progress
- [x] 3.3 Animated splash timeline + fade to home
- [x] 3.4 Home screen per the canvas: background grid/horizon/glow/sparkles, header (orb, chrome title, chips, saves card), carousel frame + glow, footer ticks + icon hints, toast, overlay

## 4. Gate

- [x] 4.1 PCSX2: compare snapshots with the canvas boards; splash animates while loading; record in `docs/phase4-results.md`
- [ ] 4.2 Console: max ≤ 8 333 µs, 0 missed vsyncs, no splash stutter, user approves the look
