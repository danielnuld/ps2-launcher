## Context

The engine (phase 1, `openspec/specs/render-engine`) draws to a 1280×720 CT16 framebuffer with about 11 % of the frame
used (console: max 1792 µs). The free VRAM pool is 65 536 words, minus the font atlas. The sample covers
(xlenore/ps2-covers, `covers/default/<serial>.jpg`) are 512×736 JPEGs.

## Goals / Non-Goals

**Goals:** covers that look as good as a CT16 framebuffer allows; no image decoding on the PS2; a measured stream cost.

**Non-Goals:** zoom or scaling animations (they go through GS bilinear filtering, which softens the image; decide
later on the TV), mapping covers to installed games (later phase), loading on demand for big libraries, and 3D covers.

## Decisions

- **Quality ceiling = the CT16 framebuffer.** A 24-bit cover is reduced to RGB555 when it is drawn, so a CT16
  texture loses nothing compared with CT32. The quality comes from doing the reduction offline and well:
  - Gamma-correct Lanczos: resizing in sRGB darkens edges and fine detail.
  - Floyd–Steinberg error diffusion to 5 bits per channel. It has no pattern, unlike the GS 4×4 ordered dither the
    user rejected on gradients (phase-0 modetest).
  - The undithered set also ships, because the user decides on the TV.
  - Rejected: PNG/JPG decoding on the PS2 (decoders in the ELF, slow, and GS scaling instead of Lanczos).
- **Display size 256×368** keeps the source aspect (512×736, halved exactly). Five covers with a 32 px gap fill a
  1280-wide row.
- **One streamed slot instead of resident textures.** Phase 0 measured EE→GS upload at about 1050 MB/s on the console
  (256 KiB CT32 in ~250 µs). A 256×368 CT16 cover is 188 416 bytes, so it should take about 180 µs. With 6 visible,
  that is about 1.1 ms per frame (estimate, measured in this phase). The slot is 4×6 CT16 pages = 49 152 words (page
  64×64, docs/sources.md), so it fits the pool with the font atlas. Rejected: PSMT8 covers (palette loss on photos)
  and resident textures (only one CT16 cover fits).
- **How a frame streams:** `gfx_image()` closes the current A+D block and sends it. Then it sends the cover's image
  transfer as a DMA chain that REFs the pixels in EE RAM (`draw_texture_transfer`), with TEXFLUSH. Then it opens a new
  block with TEX0 for the slot and one sprite. It waits for the DMA only, not FINISH: PATH3 is in-order, so the next
  upload cannot overtake the previous draw. The pixels are written back once at load time (`SyncDCache`).
- **TEX0 for the slot:** CT16, TCC=0, MODULATE with vertex 0x80 (passthrough), point sampling (`draw_setup_environment`
  default TEX1). TEXA already maps the CT16 alpha bit to 0x80 (ps2sdk `draw.c`), so the alpha test never drops cover
  pixels.
- **Load at startup:** every `.c16` in `mass0:/covers/` and `mass0:/covers_nd/` goes into EE RAM (64-byte aligned
  `memalign`). `ponytail:` load all; add on-demand loading when a library outgrows RAM (~150 covers per set).

## Risks / Trade-offs

- [PATH3 ordering assumption is wrong, so a later upload overwrites the slot before an earlier draw] → the spec
  scenario "6 different covers" catches it in PCSX2 and on the console. The fallback is to wait for FINISH after each
  draw.
- [USB 1.1 load time for ~5.5 MB] → it is measured and printed. The demo loads once.
- [Copyrighted sample covers] → kept out of the repo (`.gitignore`). Only the converter and the instructions are
  committed.
