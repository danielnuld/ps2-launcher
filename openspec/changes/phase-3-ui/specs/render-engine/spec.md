## MODIFIED Requirements

### Requirement: Text
The engine SHALL draw ASCII text from anti-aliased glyph atlases baked on the PC from Inter (OFL) by `tools/font.py`,
in a title size and a body size. The atlases ship in the ELF as one PSMT4 texture with a 16-step alpha CLUT, drawn
with one alpha-blended textured sprite per glyph and proportional advance widths. `gfx_text_width` SHALL return the
pixel width of a string, so text can be centred.

#### Scenario: Title line
- **WHEN** a frame draws a line of 60 characters
- **THEN** it costs at most 60 sprites in the packet, and the glyph edges are blended with the background (no hard
  pixel steps)

#### Scenario: Centring
- **WHEN** a string is drawn at x = (1280 - gfx_text_width(s)) / 2
- **THEN** its left and right margins differ by at most 1 pixel
