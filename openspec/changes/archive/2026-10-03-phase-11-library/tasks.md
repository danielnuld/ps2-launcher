## 1. Parsing and loader

- [x] 1.1 `iso.c`: shared SYSTEM.CNF parser (BOOT2 / BOOT, file name + raw path); VCD sector reader; host selftest
- [x] 1.2 `loader.c`: load argv[0], run with argv + 1; launcher callers updated (Neutrino unchanged in effect)

## 2. Entries

- [x] 2.1 Entry kind; PS1 scan (POPS/*.VCD), apps scan (APPS/<dir>/title.cfg, APPS/*.ELF); sorted with the PS2 list
- [x] 2.2 Disc entry at index 0 + disc thread (libcdvd), staged and applied on the render thread
- [x] 2.3 Launch per kind (POPStarter argv0, app ELF, PS2LOGO, PS1DRV) with missing-file toasts
- [x] 2.4 PS1 covers from xlenore/psx-covers in the download step

## 3. UI

- [x] 3.1 Filters (L1/R1): order/pos, views use positions, footer filter pills + counter
- [x] 3.2 Kind chip in the header; saves card, video chip and △ only for PS2; app/disc/PS1 generic cards

## 4. Checks

- [x] 4.1 PCSX2 with test content added to the USB image (an app folder, a synthetic VCD): listing, filters, app launch

## 5. Gate

- [x] 5.1 Console: PS2 ISO, PS1 VCD (with the user's POPS), app, PS2 and PS1 discs start; disc insert/eject; `docs/phase11-results.md`
