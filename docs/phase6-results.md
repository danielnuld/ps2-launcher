# Phase 6 results: real games through Neutrino

- Discovery: `mass0:/DVD`, `mass0:/CD`. Serial from SYSTEM.CNF read with 64-bit seeks (`fileXioLseek64`; Persona 4's
  SYSTEM.CNF is 3.6 GB in), with the OPL file-name prefix as a fallback. Covers by serial, else a drawn generic card.
- Covers from the PC: `python tools/fetch_covers.py E:/` (xlenore/ps2-covers, same quality path as `covers.py`).
- Launch: `mass0:/neutrino/neutrino.elf -dvd=usb:DVD/<file>.iso -qb` (Neutrino v1.8.0, ps2max32/neutrino).

## Console (SCPH-75001), 2026-10-01

1. First try (build `7c9d52c`, ps2sdk `elf-loader`): Neutrino fell back to the PS2 browser (memory card view). Cause:
   `elf-loader` resets the IOP, and Neutrino with `-qb` expects the frontend's USB and fileXio modules to stay
   loaded. Fix: own second-stage loader at 0x84000 (`loader/`) that loads the ELF with `SifLoadElf` on the running
   IOP. PCSX2 then shows Neutrino reading its config from the USB and opening the ISO.
2. Second try (build `d4fa493`): **Black (SLUS-21376) started.** **Gate: PASS.**

Limits found:
- The user's USB is FAT32: ISOs must be under 4 GB. Persona 4 (4.38 GB) needs exFAT.
- Neutrino's block devices accept at most 64 fragments per image.
- The pad works in Black (menus, game); its intro videos simply cannot be skipped (user, 2026-10-01).
