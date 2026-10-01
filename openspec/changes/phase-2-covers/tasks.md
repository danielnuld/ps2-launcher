## 1. PC converter

- [x] 1.1 `tools/covers.py`: gamma-correct Lanczos to 256×368, Floyd–Steinberg (or `--no-dither`) to RGB555, `.c16` writer; `--selftest` for flat-colour round trip
- [x] 1.2 Convert the 15 sample covers (xlenore/ps2-covers) into both sets; keep them out of git

## 2. Engine

- [x] 2.1 Reserve the CT16 cover slot in `gfx_init`; `gfx_image(pix, w, h, x, y)` = flush block → upload chain → TEX0 + sprite
- [x] 2.2 PCSX2: 6 different covers in one frame show their own images

## 3. Demo

- [x] 3.1 Load `mass0:/covers/*.c16` and `mass0:/covers_nd/*.c16` with header checks; print load time
- [x] 3.2 Cover row replaces the cards; SELECT switches sets, the name of the set on screen; timing as in phase 1, plus upload cost per cover

## 4. Gate

- [x] 4.1 PCSX2 correctness, record in `docs/phase2-results.md`
- [ ] 4.2 Console: max ≤ 8 333 µs, 0 missed vsyncs, user's TV verdict and chosen set recorded in `docs/phase2-results.md`
