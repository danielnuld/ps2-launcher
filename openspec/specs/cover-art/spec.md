# cover-art Specification

## Purpose
TBD - created by archiving change phase-2-covers. Update Purpose after archive.
## Requirements
### Requirement: Cover file format
A `.c16` file SHALL be a 16-byte header (`"C16\0"`, then width and height as little-endian u32, then 4 zero bytes)
followed by width × height little-endian RGB555 pixels (R bits 0-4, G 5-9, B 10-14, bit 15 = 1), the GS CT16 layout.
Each cover SHALL exist as `<serial>.c16` (256×368) and `<serial>_s.c16` (184×264).

#### Scenario: Wrong file
- **WHEN** the UI reads a cover whose header does not match the expected size for its name, or that is shorter than its pixel data
- **THEN** that cover is skipped, a message is printed, and the other covers still load

### Requirement: PC conversion
`tools/covers.py` SHALL resize each input image to exactly 256×368 and to 184×264, in linear light with a Lanczos
filter, then quantize to RGB555, with Floyd–Steinberg error diffusion by default and plain rounding with
`--no-dither`. It SHALL write `<input name>.c16` and `<input name>_s.c16` to the output folder.

#### Scenario: Round trip
- **WHEN** the script's self-check converts a flat colour that RGB555 can represent exactly
- **THEN** every output pixel is that colour, with and without dithering

### Requirement: Pixel-exact drawing
The engine SHALL draw a cover 1:1 at integer screen coordinates with point sampling whenever the destination size
equals the image size, so that each framebuffer pixel gets exactly one cover pixel, unchanged. Only when the size
differs (during animation) SHALL it use bilinear filtering.

#### Scenario: No scaling
- **WHEN** a 256×368 cover is drawn at 256×368, or a 184×264 one at 184×264
- **THEN** it covers exactly that many screen pixels and no GS filtering is involved

### Requirement: Streaming through one VRAM slot
The engine SHALL upload each cover into one reserved CT16 VRAM slot of 256×192 right before drawing it, in horizontal
bands of at most 192 lines, each band uploaded and then drawn. VRAM never holds more than one band, whatever the
number of covers on screen.

#### Scenario: Many covers per frame
- **WHEN** 6 different covers are on screen in the same frame
- **THEN** each one shows its own image, with no visible seam between bands at rest

### Requirement: Gate
The demo SHALL measure as in phase 1 (median and max frame time, missed vsyncs over 600 frames, `mass0:/demo.txt`)
with every visible cover streamed each frame. The phase SHALL pass if, on the console, the max is ≤ 8 333 µs, there
are 0 missed vsyncs, and the user judges the covers good on the TV with one of the two sets.

#### Scenario: Gate recorded
- **WHEN** console numbers and the user's TV verdict exist
- **THEN** `docs/phase2-results.md` records the times, the measured upload cost per cover, and the chosen set (dither or not)

