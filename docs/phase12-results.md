# Phase 12 results: In Game Reset

- Fork: `third_party/neutrino-igr` (OPL's padhook port + `patch_neutrino.py` on Neutrino v1.8.0), built by
  `tools/build_neutrino.sh` into `dist/neutrino/`. Copy `neutrino.elf` and `modules/ee_core.elf` over the official
  ones on the USB.
- Combos (`config.ini [igr]`): `reiniciar = L1+L2+R1+R2+START+SELECT`, `apagar = L1+L2+R1+R2+L3+R3`.
- `reiniciar` reboots: the launcher passes `-igrexit=rom0:OSDSYS`; FMCB comes up and autoboots ORBIT once its Auto
  key points at `mc?:/BOOT/ORBIT.ELF` (the stub the launcher installs).

## Console (SCPH-75001, HDMI adapter), 2026-10-01

1. Power off: the first build sent S-command 0x0F with a stray parameter byte (copied from OPL's 0x1B) and from the
   IGR thread, which never got that far. Sent with no parameter from the VBLANK handler, it powers off. **PASS.**
2. Reboot, in the order the console showed the problems (screen colour = stage, `igr.c` STAGE):
   - Game frozen, sound looping: the handler kept firing every VBLANK and its `iResetEE(0x7F)` reset the DMAC under
     the thread's SIF transfers. Now it fires once, silences the INTCs but SBUS, and uses 0x7E.
   - Teal (exit ELF load): the game's SIF0 DMAC handler ran on the IOP's replies after its code was wiped; then the
     wipe hit ee_core's stack (0x94000 lies above `_end`). Replaced by the path below.
   - Green to black: after the IOP reset, OSDSYS / FMCB started ee_core again, because Neutrino's loader patches
     the kernel's LoadExecPS2 to run ee_core instead of EELOAD (and moves the user memory clear). The IGR now puts
     both kernel patches back.
   - Final path: IOP reset to ROM, `services_start` + `sbv_patch_enable_lmb` + OPL's `resetspu.irx` (embedded),
     kernel unpatch, `LoadExecPS2("rom0:OSDSYS")`. Without resetspu, OSDSYS hangs in the BIOS's CLEARSPU (the game's
     SPU2 DMA, the looping sound, still runs); a bare ExecPS2 of OSDSYS hangs too.
   - Result: green, yellow, black, then FMCB, both at Black's title and during its intro movies. **PASS.**
3. Builds that never started (black screen, no Neutrino debug colours), although their new code only runs on the
   combo; PCSX2 ran all of them. Neutrino's loader copies ee_core and patches the kernel through the D-cache, then
   ExecPS2s without a flush. `patch_neutrino.py` step 4 adds `FlushCache(0)` / `FlushCache(2)`; the largest build
   so far then started. Likely the cause, not proven.

Not checked yet: a custom combo; FMCB autoboot of the stub (the reboot ends in FMCB's menu until it is set); Black
in "Nativo" (black screen once, before the cache fix).

## PCSX2

The old USB image only had 1 MiB placeholder ISOs, so Neutrino exited. A 5 GB image with Black runs the game and
the IGR; it logs `ohci_die: DMA error` at every IOP reset (USB DMA in flight), which PCSX2 survives. It shows no
GS background colours and does not model the caches.
