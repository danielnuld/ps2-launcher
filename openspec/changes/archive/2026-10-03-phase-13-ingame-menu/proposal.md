## Why

Phase 12 reboots or powers off the console the moment a combo is held, with no way to back out and no feedback on
screen. The user wants a small menu drawn over the paused game (Reiniciar / Apagar / Cancelar), as agreed when
phase 12 was planned. Nothing like it exists in Neutrino or OPL, and the risky part (drawing over a game whose GS
state cannot be read back) is unproven, so the phase starts with a feasibility spike.

## What Changes

- **Spike first:** in the ORBIT Neutrino fork, pause Black, draw a box with text over its picture, and put the
  picture back on Cancelar, on PCSX2 and then the console. The rest of the phase only goes on if the spike passes.
- **Display capture:** the fork records the game's DISPFB1/2 writes, so it knows where the shown frame sits in
  VRAM and its pixel format. It does this in every video mode, native included.
- **Pause and resume:** the menu combo freezes the game like the IGR does (game threads suspended, interrupts held)
  and Cancelar resumes it where it stopped. The game's sound may keep looping while paused; this is accepted for
  now.
- **Drawing without touching the game's GS state:** the area under the menu is copied from VRAM to EE memory, the
  menu is rendered on the EE and uploaded with image transfers only. Cancelar uploads the saved pixels back.
- **Menu:** three items, moved with the D-pad, chosen with X; ○ or START closes it like Cancelar.
  - Reiniciar runs the phase 12 reboot;
  - Apagar runs the phase 12 power off;
  - Cancelar returns to the game.
- **`config.ini [igr]`:** a new `menu` combo opens the menu (default L1+L2+R1+R2+START+SELECT, which moves off
  `reiniciar`). `reiniciar` and `apagar` stay as direct combos, empty by default for `reiniciar`.

## Capabilities

### New Capabilities
- `ingame-menu`: the paused-game menu (display capture, pause/resume, drawing and restoring, items and input,
  config, gate).

### Modified Capabilities
None. The phase 12 `igr` capability is not archived yet; the combo defaults it describes are changed in that
change's spec when this one is applied.

## Impact

- `third_party/neutrino-igr`: `igr.c` (menu combo, pause/resume), new menu code (VRAM save/restore, EE rendering,
  a small bitmap font), `patch_neutrino.py` (DISPFB capture in `gsm_api.c`, capture-only mode without `-gsm`, the
  new loader option and `eecore_config.h` field).
- `ee_core.elf` grows: its budget is 64 KB, 31 KB is used today.
- `launcher.c`: `[igr] menu` in the config and the new launch option.
- PCSX2 bench: the 5 GB image with Black from phase 12.
