# game-views Specification

## Purpose
TBD - created by archiving change phase-10-views. Update Purpose after archive.
## Requirements
### Requirement: Views
The home screen SHALL offer three views of the same games, header and footer:
- **Carrusel**, as the launcher-ui spec describes;
- **Cuadrícula**: 7 columns of 128×184 covers, two rows on screen;
- **Lista**: one row per game (media icon, title, serial) on the left, and the selected game's 256×368 cover on
  the right with the chrome frame.

In each view, the focused cover SHALL carry the chrome frame and glow, and the others SHALL be drawn at 82 % alpha.

#### Scenario: Grid at rest
- **WHEN** the grid is idle
- **THEN** every visible tile is drawn 1:1 from its 128×184 image at integer coordinates, and the focused one is
  scaled up with its frame

### Requirement: Navigation per view
- In the carousel, ←→ SHALL move by one game.
- In the grid, ←→ SHALL move by one game and ↑↓ by one row of 7, clamped to the list.
- In the list, ↑↓ SHALL move by one game and ←→ by 8.

The grid and the list SHALL scroll smoothly so that the selection stays visible. Rows entering or leaving the
visible area SHALL fade instead of being cut off.

#### Scenario: Grid down
- **WHEN** the selection is on the second visible row of the grid and the user presses ↓
- **THEN** the rows slide up by one row, eased, and the new focused tile grows

### Requirement: View switch
□ SHALL switch to the next view (Carrusel → Cuadrícula → Lista → Carrusel):
- every cover SHALL move, resize and fade from its rect in the old view to its rect in the new one, eased;
- the covers SHALL start in a stagger by distance from the selection;
- the view's name SHALL show for about 1 s.

The chosen view SHALL be written to `mass0:/orbit/estado.ini` and restored at the next boot.

#### Scenario: Remembered
- **WHEN** the user picks the list and restarts the launcher
- **THEN** it opens in the list

### Requirement: Half-size covers
For every game with a cover pair, the launcher SHALL make a 128×184 image from the 256×368 one:
- each pixel the mean of 2×2 texels in linear light;
- then Floyd–Steinberg to RGB555.

It SHALL do this at load and after a cover download, and keep the image in RAM only. Games without covers SHALL use
the drawn generic cover at any size.

#### Scenario: Flat colour
- **WHEN** a flat-colour cover is halved
- **THEN** every output texel is that colour

### Requirement: Gate
On the console, the phase SHALL pass if all of the following hold:
- the three views and the switch run with 0 missed vsyncs and max ≤ 8 333 µs in each view's frame windows;
- the user judges the views and transitions good on the TV.

`docs/phase10-results.md` SHALL record the times per view.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file holds each view's median and max frame time and the missed vsyncs

### Requirement: Animated background
Behind every view, the home screen SHALL animate a slow, dim ambience in the ORBIT palette (user request,
2026-10-01):
- two ice/iris aurora glows drifting on slow paths;
- the floor grid's horizontal lines flowing towards the viewer, its vertical lines sliding with the selection
  (parallax);
- a light sweep along the horizon every 7 s and a breathing floor glow;
- small sparkles rising and twinkling, and the design's three sparkles twinkling in place.

Nothing in it SHALL cover the header, the covers or the footer text, and it SHALL stay within the frame-time gate.

#### Scenario: Idle screen
- **WHEN** the home screen is left untouched
- **THEN** the background keeps moving while every cover stays at rest and pixel-exact

