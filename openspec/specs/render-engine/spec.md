# render-engine Specification

## Purpose
TBD - created by archiving change phase-1-engine. Update Purpose after archive.
## Requirements
### Requirement: Video mode
The engine SHALL set up 1280×720 non-interlaced with two CT16 framebuffers, dithering off, and DISPLAY DX=300 DY=27
(chosen on the SCPH-75001 + HDMI adapter, `docs/phase0-results.md`). DX and DY SHALL be one constant each in `gfx.c`.

#### Scenario: Picture position
- **WHEN** the demo starts on the console
- **THEN** the TV shows the yellow 1-px border and all four corner marks, as in modetest variant 1

### Requirement: Double-buffered frame loop
The engine SHALL draw each frame into the back buffer and SHALL swap buffers only after a vsync, so the scan-out
never shows a buffer that is still being drawn.

#### Scenario: No tearing
- **WHEN** a full-height bar moves 16 px per frame
- **THEN** the bar is never cut on the TV

### Requirement: Sprite drawing
The engine SHALL draw flat rectangles, vertical and horizontal gradients, and textured rectangles, batched into one
GIF packet per frame and sent over PATH3.

#### Scenario: One send per frame
- **WHEN** a frame draws any mix of primitives that fits the packet
- **THEN** the frame makes exactly one GIF DMA send

### Requirement: Texture pool
The engine SHALL place textures (PSMT8 with CLUT, PSMT4 with CLUT, CT16) in the 65 536 VRAM words left after the
two framebuffers (`docs/phase0-results.md`, page-rounded), aligned as the GS requires, and SHALL refuse an allocation
that does not fit instead of overlapping another one.

#### Scenario: Pool full
- **WHEN** a texture larger than the space left is requested
- **THEN** the call returns failure and no resident texture changes

### Requirement: Text
The engine SHALL draw ASCII text from a glyph atlas built once at startup from the BIOS font `rom0:KROM`, with one
textured sprite per glyph and no font file in the ELF.

#### Scenario: Title line
- **WHEN** a frame draws a line of 60 characters
- **THEN** it costs at most 60 sprites in the packet, and none of the per-pixel points that fontx draws

### Requirement: Frame timing and gate
The demo SHALL measure the EE build time plus the GS draw time of each frame with COP0.Count (ps2tek:1117-1121),
show the median and the maximum on screen, and append them to `mass0:/demo.txt` when a USB stick is present.
The phase SHALL pass only if the maximum on the console is at most 8 333 µs (half of a 16 667 µs frame at 60 Hz,
the same gate as phase 0) and no vsync is missed during 600 frames of scrolling.

#### Scenario: Gate recorded
- **WHEN** console numbers exist
- **THEN** `docs/phase1-results.md` states pass or fail against 8 333 µs and names the heaviest part of the frame

