## MODIFIED Requirements

### Requirement: Text
The engine SHALL draw text from anti-aliased glyph atlases baked on the PC by `tools/font.py`:
- Unbounded 700 title, Sora 500 UI, Space Mono label, Unbounded 800 wordmark ("ORBIT" only). All OFL.
- Characters: ASCII 32-126 plus á é í ó ú ñ Á É Í Ó Ú Ñ ü ¿ ¡ ·, read from UTF-8 strings.
- The atlases ship in the ELF as one PSMT4 texture with a 16-step alpha CLUT. Each glyph is one alpha-blended
  textured quad with a proportional advance; letter tracking is optional.
- Glyphs can take a vertical chrome gradient.
- `gfx_text_width` SHALL return the pixel width of a string, so text can be centred.

#### Scenario: Title line
- **WHEN** a frame draws a line of 60 characters
- **THEN** it costs at most 2 quads per character in the packet, and the glyph edges are blended with the background

#### Scenario: Centring
- **WHEN** a string is drawn at x = (1280 - gfx_text_width(s)) / 2
- **THEN** its left and right margins differ by at most 1 pixel

#### Scenario: Accents
- **WHEN** "Datos técnicos" is drawn
- **THEN** the é shows with its accent, and the advance matches the font
