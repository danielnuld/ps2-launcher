## 1. Tools

- [x] 1.1 `tools/font.py`: Inter title 36/700 + body 22/500 → shelf-packed 512-wide PSMT4 atlas + glyph tables → `font_data.c`; `--selftest` (packing has no overlaps, atlas fits 8 pages)
- [x] 1.2 `tools/covers.py`: write `<name>.c16` 256×368 and `<name>_s.c16` 184×264; regenerate the sample set

## 2. Engine

- [x] 2.1 Replace the KROM atlas with `font_data.c`: `gfx_text(font, x, y, s, rgb)`, `gfx_text_width`, alpha-blended glyph sprites
- [x] 2.2 `gfx_alpha(a)`: vertex alpha + ABE for the primitives that follow
- [x] 2.3 `gfx_image(pix, w, h, x, y, dw, dh)`: 256×192 slot, bands of ≤ 192 lines, point sampling at 1:1 and bilinear only when scaled

## 3. UI

- [x] 3.1 Splash with progress bar; load `covers/<serial>.c16` + `_s.c16` only
- [x] 3.2 Carousel with eased scroll, grow factor, snap to pixel-exact rest; selection frame faded by f
- [x] 3.3 Header, info panel (title + serial, fade), button hints
- [x] 3.4 SELECT debug overlay; timing log as in phase 1

## 4. Header and saves (user request)

- [x] 4.1 Load `mcman`/`mcserv`; read both cards' root dirs once at boot; per-game save count, card, newest date
- [x] 4.2 Header: title + serial + save info with fade; footer hints at the bottom right in key boxes, position at the bottom left; carousel re-centred

## 5. Gate

- [x] 5.1 PCSX2: layout, fades, text quality, no band seam at rest; record in `docs/phase3-results.md`
- [ ] 5.2 Console: max ≤ 8 333 µs, 0 missed vsyncs, user approves the look on the TV
