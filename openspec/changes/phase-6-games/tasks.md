## 1. Discovery

- [x] 1.1 `iso.c`: SYSTEM.CNF serial (64-bit reader callback), OPL name fallback, title; selftest
- [x] 1.2 `launcher.c`: scan DVD/ and CD/, per-game covers or a generic cover, DVD/CD chip, sort by title

## 2. Launch

- [x] 2.1 `LoadELFFromFile` into Neutrino (`-dvd=usb:<path> -qb`), launch screen, `gfx_shutdown`
- [x] 2.2 PCSX2: 17 test ISOs listed; X boots Neutrino (IOP reboots into its environment)
- [x] 2.3 `tools/fetch_covers.py` with selftest; Neutrino v1.8.0 placed on the user's USB

## 3. Gate

- [x] 3.1 Console: a real ISO is listed with its cover and X starts the game through Neutrino — PASS with Black (SLUS-21376, 3.92 GB) on 2026-10-01, after the own loader (d4fa493); Persona 4 (4.38 GB) does not fit FAT32
