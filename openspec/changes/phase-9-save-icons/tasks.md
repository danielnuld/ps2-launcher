## 1. Parser

- [x] 1.1 `icon.c` / `icon.h`: parse `icon.sys` (lights, list icon name) and the icon file (shapes, normals, UVs, colours, animation, raw/RLE texture) with bounds checks
- [x] 1.2 Host selftest in `make test`: synthetic icon (2 shapes, RLE runs and literals, overflow, every truncation); with `ICON_CHECK=<dir>` also apollo-ps2's real files (36 vertices, 1 shape, 128×128 texels)

## 2. Drawing

- [x] 2.1 `icon.c`: per-frame shape blend, Y spin, lighting, fit to a box → screen triangles (sorted far to near)
- [x] 2.2 `gfx.c`: `gfx_mesh`: textured Gouraud triangles with a Z buffer in an own 64×64 target, texture streamed through the slot
- [x] 2.3 Host check of 2.1: the projected icon stays inside its box and the triangle order is back-to-front

## 3. Launcher

- [x] 3.1 Icon thread after the card scan: newest save → `icon.sys` → list icon, per-serial cache, semaphore from the render thread
- [x] 3.2 Saves card: 64×64 slot with the ice glow, 3D icon when loaded, glyphs otherwise
- [x] 3.3 PCSX2 with a test memory card holding a save (a separate card file, the user's cards untouched): icon shows and animates

## 4. Gate

- [ ] 4.1 Console with the user's memory cards: icons appear and animate, the user compares them with the PS2 browser; frame windows 0 missed vsyncs and max ≤ 8 333 µs; record in `docs/phase9-results.md`
