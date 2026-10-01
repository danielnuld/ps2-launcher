## Why

ORBIT showed a demo set of covers. To be a launcher it has to list the user's real games and start them. Game
loading is delegated to Neutrino (project rule: no code reuse, external backends launched as ELFs). The user keeps
ISOs on the USB in the OPL layout and asked us to fetch Neutrino.

## What Changes

- `iso.c`: PS2 serial from SYSTEM.CNF (ISO9660) through a 64-bit-capable reader, with the OPL file-name serial
  as a fallback. Title from the file name. Host selftest with a synthetic ISO.
- `launcher.c`:
  - Scans `mass0:/DVD/*.iso` and `mass0:/CD/*.iso`.
  - Per game: title, serial, media (DVD/CD), optional covers `covers/<SERIAL>.c16` + `_s`, and a drawn generic
    cover when there is none.
  - X launches Neutrino (`mass0:/neutrino/neutrino.elf -dvd=usb:<path> -qb`) through ps2sdk `elf-loader`, after a
    short "INICIANDO CON NEUTRINO" screen and removing the vsync interrupt handler (`gfx_shutdown`).
- `tools/fetch_covers.py`: on the PC, reads every ISO's serial on the USB, downloads its cover from
  xlenore/ps2-covers and converts it with `covers.py`.
- Neutrino v1.8.0 (ps2max32/neutrino, formerly rickgaiser/neutrino) placed by us at `mass0:/neutrino/` on the
  user's USB. It is not committed.

## Capabilities

### New Capabilities
- `games`: game discovery, identification, covers and launching through Neutrino.

### Modified Capabilities

## Impact

New `iso.c` / `iso.h` and `tools/fetch_covers.py`. Changed `launcher.c`, `gfx.c` / `gfx.h` (`gfx_shutdown`), and
`Makefile` (`iso.o`, `-lelf-loader`, iso selftest). Only the USB backend for now; HDD, network, MX4SIO and MMCE
come later (the icons already exist).
