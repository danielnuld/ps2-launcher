## 1. Engine core

- [x] 1.1 `gfx.h` / `gfx.c`: mode setup (720p CT16 ×2, DTHE off, DX=300 DY=27), `gfx_begin` / `gfx_end` with vsync flip, from modetest
- [x] 1.2 Primitives: `gfx_rect`, `gfx_grad`, `gfx_sprite` into one A+D packet per frame (one packet: see design)
- [x] 1.3 Texture pool: bump allocator over the words left after the framebuffers, page-aligned; host self-check (`make test`) for fit / refuse
- [x] 1.4 Texture upload for PSMT8 / PSMT4 / CT16 + CLUT

## 2. Text

- [x] 2.1 Rasterize KROM ASCII glyphs into a PSMT4 atlas at startup; record the glyph size in `docs/sources.md`
- [x] 2.2 `gfx_text`: one sprite per glyph; host self-check of the glyph → atlas UV mapping

## 3. Demo

- [x] 3.1 `demo.c` (`make APP=demo`): 12 procedural PSMT8 cards, title text, D-pad scroll, moving full-height bar
- [x] 3.2 Frame timer: median / max / missed vsyncs over 600 frames, shown on screen and appended to `mass0:/demo.txt`

## 4. Gate

- [x] 4.1 Run in PCSX2: correctness only (textures, text, no garbage in VRAM); record in `docs/phase1-results.md`
- [x] 4.2 Run on SCPH-75001: max frame ≤ 8 333 µs and 0 missed vsyncs over 600 frames → go/no-go recorded in `docs/phase1-results.md` — PASS: max 1792 µs, 0 missed (console 2026-10-01)
