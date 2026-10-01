# Phase 3 results: launcher UI

`demo.elf` is now the launcher screen:
- Header, then a carousel: selected cover 256×368, others 184×264.
- Title (Inter 36 bold) and serial (Inter 22) of the selected game, with a fade.
- Hints at the bottom. X shows a "next phase" toast.
- Splash with a progress bar while the covers load.
- SELECT toggles the technical overlay. With it on and 5 s idle, the selection moves by itself for the gate run.

Covers: `python tools/covers.py <PNG/JPG folder> <usb>/covers` writes `<serial>.c16` + `<serial>_s.c16`.
Font: `python tools/font.py` regenerates `font_data.c` from `tools/fonts/Inter.ttf` (OFL). The atlas is
512×256 PSMT4 = 8 pages.

VRAM pool (32 pages): cover band slot 12 pages (256×192 CT16) + font 8 pages + CLUT.

## PCSX2 (2026-10-01): correctness only

PCSX2 F8 snapshots (full frame; PCSX2 saves them stretched to 4:3).
- Splash: title, "Cargando portadas... N de 15", progress bar advancing.
- At rest: the layout matches the design. The title and serial are centred, and the text is anti-aliased with no
  hard edges.
- Right arrow: the row slides one place, DMC3 shrinks to the left, FFX grows into the centre and the title changes
  to FFX. It settles at rest. The left end does not wrap.
- X: toast. SELECT: overlay.
- Pixel-exactness and band seams cannot be judged on stretched snapshots: that is for the TV.
- Load 15 covers × 2 sizes: 238 ms in PCSX2 (USB image on an SSD), so it is not representative.

## Header + saves (user request 2026-10-01)

The header now shows the selected game's title, its serial and its saves; the hints moved to the bottom right in key
boxes, with "N / 15" at the bottom left. PCSX2: card 1 holds only `OPL`, card 2 lists nothing, so every game shows
"Sin saves". The fade and the layout are right. The "N saves en Memory Card X - date" path needs a card with saves
(console).

## Console (SCPH-75001, HDMI adapter) — 2026-10-01

Raw file: `docs/console/ui-2026-10-01.txt` (build `9d82c24`: header + saves + side hints).

| Window | Overlay | Median | Max | Missed vsyncs |
|---|---|---|---|---|
| 1 | off | 2284 µs | 2612 µs | 0 |
| 2 | off | 2918 µs | 3049 µs | 0 |
| 3 | on (auto-move) | 3177 µs | 3302 µs | 0 |

Load: 15 covers × 2 sizes in 684 ms. **Times pass** (max 3302 µs = 40 % of the gate).

TV (user): the text shows a thin line along the top of the glyphs; it does not affect reading. It does not show in
PCSX2 (zoomed F8 snapshot is clean). Suspected cause: TEX1 was never initialised. `draw_setup_environment` does not
set it, the console keeps the previous program's value (filtering/LOD), and PCSX2 starts at 0. Fix (commit after
`9d82c24`): TEX1 = point sampling, LCM=1 (fixed LOD 0), MXL=0, written in `gfx_init`; glyph padding in the atlas
raised from 1 to 2 texels. Pending: the user's check on the TV.
