## Why

The launcher had no sound. The user asked for sounds for the splash, button presses and game-selection transitions,
in the cybercore Y2K style adapted to 2026, and approved the synthesized previews on the PC (2026-10-01).

## What Changes

- `tools/sfx.py` synthesizes 5 original sounds (no samples) into `sfx/*.wav` (48 kHz mono):
  - `splash`: synced to the splash timeline.
  - `move`, `edge`: carousel step and end of the row.
  - `confirm`: X.
  - `panel`: SELECT.

  The WAVs are committed (WSL has no NumPy).
- `Makefile`: each WAV becomes SPU2 ADPCM with ps2sdk `adpenc`, then a C array (`bin2c`), linked into
  `launcher.elf`.
- IOP: `libsd` + `audsrv` modules. `iop_init` is split into `iop_load` + `usb_wait`, so sound is ready before the
  USB mount.
- `launcher.c`:
  - The loader thread inits audsrv and uploads the samples to SPU2 RAM.
  - The splash starts with its sound once audio is ready.
  - UI events play their sounds.

## Capabilities

### New Capabilities
- `sound`: synthesized UI sound set, ADPCM pipeline, playback on SPU2 voices, splash sync.

### Modified Capabilities

## Impact

New `tools/sfx.py` and `sfx/*.wav`. Changed `iop.c` / `iop.h`, `Makefile`, `launcher.c`. The ELF grows about
154 KB of ADPCM, which is also what it takes in SPU2 RAM (2 MB).
