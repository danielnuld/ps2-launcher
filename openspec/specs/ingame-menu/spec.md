# ingame-menu Specification

## Purpose
TBD - created by archiving change phase-13-ingame-menu. Update Purpose after archive.
## Requirements
### Requirement: Feasibility spike
Before the menu is built, a spike in the ORBIT fork SHALL show, on PCSX2 and then on the console, that the fork can:
- pause Black on a combo;
- draw a filled box with one line of text over the paused picture;
- put the original pixels back and resume the game on another button.

The rest of the phase SHALL go ahead only if the spike passes on the console. Its result goes to
`docs/phase13-results.md`.

#### Scenario: Spike passes
- **WHEN** the spike build is held on the combo in Black, then released with the resume button
- **THEN** the box shows over the game, and after resuming the game picture shows no trace of it and the game runs on

### Requirement: Display capture
The fork SHALL record the game's latest DISPFB1 and DISPFB2 writes (base, width, pixel format, offsets) in every
video mode: with `-gsm` and without it (native). The menu SHALL be drawn into the frame those registers show.

#### Scenario: Native video
- **WHEN** a game started in "Nativo" opens the menu
- **THEN** the menu shows over the game picture, the same as in 480p

### Requirement: Pause and resume
Holding exactly the `menu` combo SHALL pause the game: its threads stop and its interrupts are held, so the shown
frame does not change. Cancelar SHALL resume the game where it stopped, with its interrupts as they were. The game's
sound MAY keep looping while paused.

#### Scenario: Resume
- **WHEN** the user opens the menu in Black's gameplay and picks Cancelar
- **THEN** the game goes on from the same moment and keeps responding to the pad

### Requirement: Drawing over the game
The menu SHALL be drawn without changing the game's GS drawing state: the fork SHALL only use image transfers
(BITBLTBUF, TRXPOS, TRXREG, TRXDIR) to save the area under the menu to EE memory, to upload the rendered menu, and
to upload the saved area back on Cancelar. It SHALL support the CT32, CT24, CT16 and CT16S frame formats.

#### Scenario: Picture restored
- **WHEN** the menu closes with Cancelar
- **THEN** the area it covered shows the game's pixels again, byte for byte

### Requirement: Menu items and input
The menu SHALL list Reiniciar, Apagar and Cancelar, with Cancelar selected first. The D-pad SHALL move the selection,
X SHALL choose, and ○ or START SHALL act as Cancelar. Reiniciar SHALL run the phase 12 reboot, and Apagar the phase
12 power off.

#### Scenario: Reboot from the menu
- **WHEN** the user selects Reiniciar and presses X
- **THEN** the console reboots as with the phase 12 reboot combo

### Requirement: Menu combo in the config
`config.ini [igr]` SHALL accept `menu`, a combo in the same format as `reiniciar` and `apagar`, with the default
L1+L2+R1+R2+START+SELECT. The default for `reiniciar` SHALL become empty (off). The launcher SHALL pass the menu combo
to the fork only when the fork supports it.

#### Scenario: Defaults
- **WHEN** a fresh `config.ini` is written
- **THEN** `menu = L1+L2+R1+R2+START+SELECT`, `reiniciar =` (empty) and `apagar = L1+L2+R1+R2+L3+R3`

### Requirement: Gate
On the console, the phase SHALL pass if, in Black, in 480p and in native video:
- the menu opens during gameplay and during the intro movies;
- Cancelar resumes the game with the picture intact;
- Reiniciar and Apagar work from the menu.

Results go to `docs/phase13-results.md`.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file lists each check, the video mode and the result

