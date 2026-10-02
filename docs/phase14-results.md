# Phase 14 results: virtual memory cards and game sources

Coded and built on 2026-10-02 (ps2dev v2.0.0: launcher, bench and modetest without warnings; host selftests pass).
**Not tried on the console or PCSX2 yet.**

## Checked off the console

- `vmc_create`: 8 388 608 bytes; mymcplus accepts it (`df`: 8 134 KB free, as a new 8 MB card).
- mymcplus wrote a save dir + `icon.sys` + a 5 000-byte file, and 6 more dirs (root chained over 4 clusters):
  `vmc_list` lists all 7, `vmc_read` returns `icon.sys` and the 5 000 bytes unchanged.
- Found on the way: a subdirectory's own "." entry says length 0; the count is in the parent's entry.

## Setup on the USB

- `config.ini` keeps its old text when it exists: add by hand, if wanted,
  `[memorycard]` `modo = juego` (the default even without the section) and the sources in `[juegos] origen`,
  e.g. `origen = usb, hdd`.
- UDP: `[red] ip` must be a fixed IP; run `pc/udpfs_server.py` from the Neutrino release on the PC
  (`-b <disk>` for udpbd, `-d <folder>` for udpfs). The launcher copies the IP into Neutrino's tomls.
- The boot stub changes (it embeds the loader): the launcher rewrites `BOOT/ORBIT.ELF` once (toast).

## Console checklist (gate)

1. Black from the USB, default mode: launch screen says "CREANDO MEMORY CARD VIRTUAL" once; `VMC/SLUS-21376.bin`
   appears; the game sees an empty formatted card in slot 1 and saves; back in the launcher the saves card says
   "1 save · VIRTUAL · <date>" with the 3D icon. `launcher.txt` shows the `-mc0=usb:VMC/SLUS-21376.bin` line.
2. △ → Memory card → Física: the game saves to the real card in slot 1.
3. Each source available: chip, listing, launch.

Known risk: Neutrino's mc_emu reports 8192 pages (4 MB) as the card size while the superblock says 8 MB. If a game
reports a damaged or 4 MB card, that is the place to look.
