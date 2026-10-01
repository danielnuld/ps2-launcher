## ADDED Requirements

### Requirement: Icon parsing
The launcher SHALL parse:
- `icon.sys` as ps2sdk's `mcIcon`: the "PS2D" magic, the 3 light directions and colours, the ambient light, and
  the list icon file name;
- the icon file:
  - header;
  - per-vertex positions for every animation shape, normals, UVs and colours;
  - the animation header, frames and keys;
  - the 128×128 CT16 texture, raw or RLE-compressed.

A file that is truncated, has a wrong magic, or whose RLE output overflows the texture SHALL be rejected without
reading past its end. That game keeps the glyph.

#### Scenario: Real icon
- **WHEN** the host selftest parses apollo-ps2's `icon.sys` and `icon.ico`
- **THEN** it gets 36 vertices, 1 shape, an RLE texture that decodes to exactly 128×128 texels, and the list icon
  name `icon.ico`

#### Scenario: Corrupt icon
- **WHEN** the selftest feeds every truncation of that icon file and an RLE run that overflows
- **THEN** each one is rejected and none reads out of bounds

### Requirement: Background loading
After the boot memory-card scan, a background thread SHALL load the selected game's icon the first time that game
is selected:
- it takes the newest save, the same one the saves card describes;
- the icon is kept in memory for the rest of the session.

The render thread SHALL never wait for memory-card I/O.

#### Scenario: Fast scrolling
- **WHEN** the user scrolls quickly across many games with saves
- **THEN** the carousel keeps 60 Hz and each card shows the glyph until its icon is loaded

### Requirement: 3D drawing
The saves card SHALL draw the icon in a 56×56 box inside its 64×64 slot, over the design's ice glow:
- scaled to fit its first shape's bounds, with up as −y;
- spinning slowly about the vertical axis;
- shape animation applied by interpolating the frame keys;
- lit per vertex by the save's 3 directional lights plus ambient, modulating the texture and vertex colour;
- triangles back-to-front (painter's order), with no depth buffer.

#### Scenario: Icon shown
- **WHEN** the selected game has a save whose icon loaded
- **THEN** the saves card shows that icon, textured and turning, where the glyph was

#### Scenario: Frame time
- **WHEN** the icon is drawn on the console
- **THEN** the frame windows still report 0 missed vsyncs and a max frame time within the 8 333 µs gate

### Requirement: Gate
The phase SHALL pass if, on the console with the user's memory cards:
- the icons of games with saves appear and animate;
- they look like the PS2 browser's to the user;
- the frame windows show 0 missed vsyncs and max ≤ 8 333 µs while icons are on screen.

`docs/phase9-results.md` SHALL record the times and the user's verdict.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file holds the frame times, the missed vsyncs and the verdict
