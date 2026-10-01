## ADDED Requirements

### Requirement: Carousel
The UI SHALL show the games as one horizontal row. The selected cover is centred at 256×368 and the others are
184×264, vertically centred on the same line. D-pad left/right moves the selection by one game, and the row SHALL
not wrap.

#### Scenario: At rest
- **WHEN** no animation is running
- **THEN** every cover is drawn 1:1 at integer coordinates (pixel-exact, cover-art spec), with the selected one at x = 512

### Requirement: Smooth animation
Moving the selection SHALL animate the scroll and the size change, easing towards the target each frame. When it is
close enough it SHALL snap exactly to the rest positions, so that the image settles pixel-exact.

#### Scenario: One press
- **WHEN** the user presses right once
- **THEN** the row slides one position, the old selection shrinks and the new one grows, and within 0.5 s every
  cover is back at its rest size and position

### Requirement: Info panel and hints
Under the carousel the UI SHALL show the selected game's title in the large font and its serial in the body font,
centred, fading in after a selection change. At the bottom it SHALL show the button hints.

#### Scenario: Selection change
- **WHEN** the selection changes
- **THEN** the title and serial of the new game replace the old ones with a fade, and never show the old title at
  full opacity together with the new one

### Requirement: Splash
From the first frame until the covers are loaded, the UI SHALL show a splash: the launcher name, a status line, and
a progress bar that advances with every cover loaded.

#### Scenario: Boot
- **WHEN** the ELF starts with N covers on the USB
- **THEN** the screen is never undrawn VRAM, and the bar reaches full width after the N-th cover

### Requirement: Debug overlay and gate
SELECT SHALL toggle an overlay with the phase-1 measurements (median / max frame time, missed vsyncs). The measurements
SHALL keep being appended to `mass0:/demo.txt` for the first 3 windows. The phase SHALL pass if, on the console, the
max is ≤ 8 333 µs, there are 0 missed vsyncs, and the user approves the look on the TV.

#### Scenario: Gate recorded
- **WHEN** console numbers and the user's verdict exist
- **THEN** `docs/phase3-results.md` records them
