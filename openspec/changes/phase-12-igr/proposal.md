## Why

The user wants to return to the launcher from a game with a button combo they can set, and to power the console off
the same way (2026-10-01). Later, phase 13 adds a drawn in-game menu. Neutrino has no In Game Reset; OPL has one.
Both are AFL-3.0, so OPL's can be ported. The user starts the launcher from uLaunchELF today and wants it to become
the autoboot.

## What Changes

- **ORBIT fork of Neutrino v1.8.0**, kept as patches plus new files in `third_party/neutrino-igr`, built by
  `tools/build_neutrino.sh` into `dist/neutrino/`:
  - `ee_core.elf` gets OPL's IGR: the libpad open hook, a VBLANK check, and a high-priority thread;
  - the loader `neutrino.elf` gets three options: `-igr=<mask>` returns, `-igroff=<mask>` powers off,
    `-igrexit=<elf>` names the ELF to return to.
- **Boot stub** `boot.elf`, installed by the launcher at `mc?:/BOOT/ORBIT.ELF`. It brings up the USB and runs
  `mass0:/launcher.elf`. It is where the IGR returns to (after the IOP reset only the ROM and the memory card are
  left), and it is also the file OSDMenu / FMCB can autoboot. The launcher itself stays on the USB.
- **`config.ini [igr]`**: `volver` and `apagar` combos, by button name joined with `+`. They default to OPL's
  combos, L1+L2+R1+R2 with START+SELECT, or with L3+R3.
- The launcher passes the IGR options only to the ORBIT fork, which it recognizes by the `-igrexit` text inside
  `neutrino.elf`. The official build would reject them.
- The loader's run-another-ELF code moves to `exec.c`, shared by the launcher and the stub.

## Capabilities

### New Capabilities
- `igr`: combos, return and power off from a game, and the boot stub.

### Modified Capabilities
None.

## Impact

- New files:
  - `boot.c`, `exec.c`/`.h`, `combo.c`/`.h` (with a host selftest);
  - `third_party/neutrino-igr/` (`igr.c`, `igr.h`, `padpatterns.h`, `patch_neutrino.py`);
  - `tools/build_neutrino.sh`.
- `launcher.c`:
  - stub install (icon thread, libmc);
  - IGR config;
  - fork detection;
  - launch arguments.
- The Makefile gets the `APP=boot` target and embeds `boot.elf` in the launcher.
- The USB needs the fork's `neutrino.elf` and `modules/ee_core.elf` over the official v1.8.0 files.
