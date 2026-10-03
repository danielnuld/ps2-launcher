# library Specification

## Purpose
TBD - created by archiving change phase-11-library. Update Purpose after archive.
## Requirements
### Requirement: Entry kinds
The home screen SHALL list, sorted by title:
- **PS2 games**, as today;
- **PS1 games**: every `mass0:/POPS/*.VCD`, with its serial from the image's SYSTEM.CNF `BOOT` line, else from
  an OPL-style name prefix, else none, and its title from the file name;
- **Apps**:
  - every `mass0:/APPS/<dir>` holding a `title.cfg` with `title=` and `boot=`, the ELF being in that folder;
  - every loose `mass0:/APPS/*.ELF`, titled by its file name;
- **the disc entry**, first in the list.

#### Scenario: Mixed USB
- **WHEN** the USB holds PS2 ISOs, two VCDs and one app folder
- **THEN** all of them appear, each with its kind chip, and the app shows the title from `title.cfg`

### Requirement: Disc entry
The disc entry SHALL follow the drive live:
- it reads "Sin disco" when the tray is empty;
- it reads "Leyendo..." while the disc spins up;
- for a PS2 or PS1 game disc, it shows the serial from `cdrom0:\SYSTEM.CNF`, plus the cover when the USB has one;
- any other disc reads "Disco no compatible".

The drive SHALL be polled off the render thread, and changes SHALL be applied between frames.

#### Scenario: Insert a game
- **WHEN** a PS2 game disc is inserted while the launcher runs
- **THEN** within a few seconds the disc entry shows its serial, and X starts it

### Requirement: Launch per kind
X SHALL start the selected entry as follows:
- **PS2 ISO**: Neutrino, as today;
- **PS1**: `mass0:/POPS/POPSTARTER.ELF` through the loader, with `argv[0] = mass:/POPS/XX.<name>.ELF` for
  `<name>.VCD`;
- **app**: its ELF through the loader, with `argv[0]` = the ELF's own path;
- **PS2 disc**: `rom0:PS2LOGO` with the `BOOT2` path;
- **PS1 disc**: `rom0:PS1DRV` with the `BOOT` file name and version.

If POPStarter or `POPS_IOX.PAK` is missing, a toast SHALL say where they go and nothing is launched.

#### Scenario: PS1 without POPS
- **WHEN** the user presses X on a PS1 game and `mass0:/POPS/POPS_IOX.PAK` is missing
- **THEN** a toast says to copy POPS_IOX.PAK and POPSTARTER.ELF to mass0:/POPS/

### Requirement: Filters
L1 and R1 SHALL cycle the filter: Todo, PS2, PS1, Apps. The disc entry shows under Todo, and under PS2 or PS1 when
it holds that kind of disc.
- Every view SHALL lay out only the matching entries: the others fade out, the rest fly to their new places.
- The selection SHALL stay on the same entry when that entry is still shown.
- The footer SHALL show the filters with the active one highlighted, and the position counter within the filter.

#### Scenario: Only PS1
- **WHEN** the user presses R1 until the filter reads PS1
- **THEN** only PS1 games remain, laid out as if they were the whole list

### Requirement: Kind-specific UI
- The header SHALL show a kind chip: PS2 DVD, PS2 CD, PS1, APP or DISCO.
- The saves card, the video chip and the △ options SHALL show only for PS2 entries.
- PS1 covers SHALL be fetched from xlenore/psx-covers by serial, like PS2 covers.

#### Scenario: App selected
- **WHEN** an app is selected
- **THEN** the header shows its title and an APP chip, and the footer has no △ Opciones hint

### Requirement: Gate
On the console, the phase SHALL pass if all of the following start:
- a PS2 ISO;
- a PS1 VCD, when the user provides POPS;
- an app;
- a PS2 game disc and a PS1 game disc.

The disc entry SHALL follow insert and eject. Results go to `docs/phase11-results.md`.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file lists what started and how

