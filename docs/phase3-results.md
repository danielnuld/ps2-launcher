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

## Console (SCPH-75001): pending

Gate: max ≤ 8 333 µs, 0 missed vsyncs (overlay on, 30 s hands-free), and the user approves the look on the TV.
