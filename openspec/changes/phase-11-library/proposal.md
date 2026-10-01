## Why

The user wants the launcher to list everything the console can run, not only PS2 ISOs (2026-10-01): PS1 games,
apps, and the disc in the drive.

## What Changes

- New entry kinds, each with its own scan and launch:
  - **PS1** — `mass0:/POPS/*.VCD`, launched with POPStarter;
  - **Apps** — `mass0:/APPS/<dir>/title.cfg` (OPL's `title=` / `boot=`) and loose `mass0:/APPS/*.ELF`, launched
    by the launcher's loader;
  - **Disc** — a PS2 or PS1 game disc in the drive, launched with the BIOS (`rom0:PS2LOGO` / `rom0:PS1DRV`).
    The entry tracks insert and eject live.
- PS2 ISOs from `DVD/` and `CD/` stay as they are, launched by Neutrino.
- POPStarter launch: a single `mass0:/POPS/POPSTARTER.ELF`, with `argv[0]` set to
  `mass:/POPS/XX.<name>.ELF`, from which POPStarter derives `<name>.VCD`. `POPS_IOX.PAK` is the user's to provide.
  Missing files raise a toast saying where they go.
- The second-stage loader takes the ELF to load separately from the program's `argv`.
- Filters with L1 / R1: Todo · PS2 · PS1 · Apps. The footer shows the active filter. Every view reflows with the
  same flying-cover animation.
- PS1 serials come from the VCD's SYSTEM.CNF (`BOOT`), PS1 covers from xlenore/psx-covers, and both go through the
  same download path as PS2 covers.
- The header chips name the kind (PS2 DVD / PS2 CD / PS1 / APP / DISCO). The saves card, the video chip and the △
  options show only for PS2 entries.

## Capabilities

### New Capabilities
- `library`: entry kinds, their scans and launches, the disc entry, and the filters.

### Modified Capabilities
- `games`: `iso.c` reads PS1 `BOOT` lines as well as PS2 `BOOT2`, and the loader's argument convention changes.

## Impact

- `iso.c` gets a shared SYSTEM.CNF parser and a raw 2352-byte sector reader for VCDs.
- `loader/loader.c`: the argv shift.
- `launcher.c`:
  - entry kinds, scans and launch per kind;
  - a disc thread (libcdvd);
  - filter order and positions;
  - footer filter pills and header chips.
- The Makefile links `-lcdvd`.
- Out of scope: DVD video and audio CDs, which need the ROM DVD player or the browser.
