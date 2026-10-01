## Why

Black started through Neutrino (phase 6), but always in the game's native video mode (480i on the user's TV). The
user wants a config file to choose the game source, and agreed (2026-10-01) to 480p (Neutrino `-gsm=fp2`) as the
default game video mode, with 1080i ×2 and native per game. Some games need compatibility switches. True HD
rendering is impossible on the real hardware; 720p output is impossible with integer GS magnification.

## What Changes

- `ini.c`: a small INI reader/writer (sections, `key = value`, `;` comments) with a host selftest.
- `mass0:/orbit/config.ini`, created with commented defaults when missing:
  - `[juegos] origen` (usb; other sources need their own drivers and come later, so the launcher says so);
  - `[video] modo` = 480p | 1080i | nativo;
  - `[sonido] volumen` 0-100.
- Per-game options (△): video mode (default / native / 480p / 1080i), GSM compatibility (0-3), Neutrino
  game-compat modes `-gc` (0, 2, 3, 5, 7). Saved per serial in `mass0:/orbit/juegos.ini`.
- Launch arguments built from them: `-gsm=fp2[:c]`, `-gsm=1080ix2[:c]`, `-gc=<digits>`.
- Header chip with the game's video mode.

## Capabilities

### New Capabilities
- `config`: config file and per-game options.

### Modified Capabilities
- `games`: Launch requirement (video mode and compat arguments).

## Impact

New `ini.c` / `ini.h`. Changed `launcher.c` (options panel, launch arguments, volume) and `Makefile` (ini.o, ini
selftest).
