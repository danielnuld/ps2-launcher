# launcher-ui Specification

## Purpose
TBD - created by archiving change phase-3-ui. Update Purpose after archive.
## Requirements
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

### Requirement: Header and hints
The header SHALL show the selected game's title in the large font with its serial under it, fading in after a
selection change. On the right it SHALL show that game's saves. The button hints SHALL sit at the bottom right,
each key in a box, with the position in the list ("3 / 15") at the bottom left. The title SHALL NOT be repeated
under the carousel.

#### Scenario: Selection change
- **WHEN** the selection changes
- **THEN** the header title, serial and save info of the new game replace the old ones with a fade, and never show
  the old title at full opacity together with the new one

### Requirement: Save info
At boot the UI SHALL list the root directories of both memory cards once. A game's saves are the directories whose
name contains its serial (for example `BASLUS-20946...` for `SLUS-20946`). The header SHALL show the count and the
card ("2 saves en Memory Card 1"), the newest modification date, or "Sin saves" when there are none, or
"Sin memory card" when no card is inserted.

#### Scenario: No saves
- **WHEN** the selected game has no directory on either card
- **THEN** the header says "Sin saves"

### Requirement: Splash
From the first frame until loading ends, the UI SHALL show the animated ORBIT splash (orbit-style spec). Its progress
bar and status line ("INICIANDO USB", "LEYENDO MEMORY CARDS", "CARGANDO PORTADAS NN / NN") follow a loader thread.
When loading ends and the timeline has reached its last key, the splash SHALL fade to the home screen.

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

