# orbit-style Specification

## Purpose
TBD - created by archiving change phase-4-orbit-style. Update Purpose after archive.
## Requirements
### Requirement: Design source
The launcher's look SHALL follow the approved ORBIT design canvas (https://claude.ai/artifact/94TdvSa23Yh4veqXDpLv7E):
its palette (night #04060E, navy #0D1736, ice #7FE7FF, iris #C08BFF, chrome gradient, text #EEF3FF / #A9B6D3), its
fonts, and its home-screen and splash layouts at 1280×720. It SHALL NOT draw Sony or PlayStation logos.

#### Scenario: Side by side
- **WHEN** a PCSX2 snapshot of the home screen is compared with the canvas's "Inicio" board
- **THEN** every element of the board is present at its position (± a few pixels), except effects the design notes
  as approximated: blur becomes baked glows, and the 3D save icon is a phase-5 placeholder

### Requirement: Icons
Actions (play, technical data), game sources (HDD, USB, network, MX4SIO, MMCE, iLink, disc), the memory card and the
pad buttons (cross, circle, triangle, square, SELECT) SHALL be drawn from a baked anti-aliased alpha atlas and tinted
per draw.

#### Scenario: Footer hints
- **WHEN** the home screen is shown
- **THEN** each hint shows the pad button in a chrome disc or pill, then an action icon, then its label

### Requirement: Animated splash
From the first frame, the launcher SHALL play the splash timeline at 60 Hz:
- 0–0.5 s: spark.
- 0.5–1.6 s: the orbit draws itself and light towers rise.
- 1.6–2.4 s: the orb appears with a moving highlight.
- 2.4–3.2 s: "ORBIT" appears with its tracking closing, then the tagline.
- From 3.2 s: the progress bar fills with the real loading progress.

Meanwhile a loader thread brings up the IOP and USB, reads the memory cards and loads the covers. When both the
timeline and the loading are done, the splash SHALL fade to the home screen.

#### Scenario: Smooth while loading
- **WHEN** covers load during the splash
- **THEN** the animation does not freeze while a file is read (the render thread is never blocked by file I/O)

### Requirement: Gate
On the console the home screen SHALL keep max frame time ≤ 8 333 µs with 0 missed vsyncs (overlay on, 30 s
hands-free), the splash SHALL show no visible stutter, and the user SHALL approve the look on the TV.

#### Scenario: Gate recorded
- **WHEN** console numbers and the user's verdict exist
- **THEN** `docs/phase4-results.md` records them

