# Phase 13 results: in-game menu

## Spike (go / no-go)

The menu combo pauses the game, a 96×24 box with one word is drawn over the shown frame, and X puts the pixels back
and resumes. Only image transfers touch the GS; the frame comes from GSM's breakpoint, which now records DISPFB.

### Console (SCPH-75001, HDMI adapter), 2026-10-01

- Black, 480p (default): the box said **"PAUSA"**, so the DISPFB capture works on hardware. Pause, box, restore
  and resume worked. **GO.**
- Native video: not tried yet; it uses GSM's new capture-only mode. Moved to the gate.

### PCSX2 (5 GB image with Black)

- PCSX2 has no EE data breakpoints: GSM never sees DISPLAY or DISPFB writes there, so 480p shows half a picture
  and the spike draws into a guessed frame (Black's: FBP 0, 640 wide, CT16S) and writes "UPS". Pause, draw,
  restore and resume work at the title screen and during the intro movies.
- Bugs found on the way, all in the resume:
  1. Suspending the interrupted thread from the VBLANK handler (`iSuspendThread`, as OPL's IGR does) left Black's
     main thread READY but never scheduled: `ResumeThread` returned -1. The threads are now suspended by the IGR
     thread at priority 0, when none of them is running.
  2. OPL's VRAM readback unmasks PATH3 at the end; the menu no longer touches the game's PATH3 mask (FLUSHA waits
     for PATH3, and the game's GIF DMA is idle).
  3. Our VIF1 transfers flag channel 1 done in D_STAT; that flag is cleared before resuming, unless the game's own
     completion was already pending.
- Black's frame: CT16S, 640 wide, double-buffered from FBP 0.

### Saved-pixel storage

`ee_core` has about 19 KB free between `_end` and its stack, plus the 4 KB stack once the game runs. The spike box
is 96×24 (4.5 KB in CT16S, 9 KB in CT32). The full menu needs a larger box: see the decision in the tasks.

## Menu (commit 4ab2219), console 2026-10-01

- Black, 480p (default): menu opens, D-pad, Cancelar with the picture intact, Reiniciar and Apagar. **PASS.**

## Known issues

1. **"Nativo" video: black screen.** It was black before phase 13 too (once, phase 12 builds). With the menu, the
   capture-only GSM arms its breakpoint as ee_core starts, so it also catches the kernel's own GS setup while the
   game boots; an access it cannot emulate ends in GSM's BGERROR loop. A fix (arm after the game's SetGsCrt, never
   hang in capture-only mode, 75d8b02) could not be checked: that build did not boot at all (issue 2).
2. **Some ee_core builds never start on the console** (black screen, no Neutrino debug colours) while others built
   from nearly the same code do, and PCSX2 runs them all. The difference follows the binary's layout, not code that
   runs at boot. Tried without success: a cache flush before ee_core starts (kept, patch step 4), and a D-cache
   flush around the game's ELF load and after the IGR's code patches (1c24018, reverted). Seen with: the USB
   controller reset (phase 12), the first kernel-unpatch build, 75d8b02 and 1c24018. Workaround: keep the last
   build that boots (4ab2219's ee_core) and check every new ee_core on the console before relying on it.
3. **A game started after a Reiniciar from the menu stayed black once.** Not reproduced; it may have been issue 1
   (Black's video setting at the time is unknown).

The fork's sources are back at 4ab2219; 75d8b02 and 1c24018 stay in the history for when issue 2 is understood.

**2026-10-05 (v1.0.1, #12):** a likely cause for issue 2. ee_core's stack (and the IGR thread's) is 0x94000-0x95000,
right above its BSS. With the menu's 16 KB save buffer in the BSS, the BSS ended 56 to 632 bytes under the stack, and
the build whose BSS ended at 0x93d88 booted but never showed the menu (the BSS tail held libkernel's `smem_buf` and
`_slib_cur_exp_lib_list`). Phase 18 had moved the buffer to module storage, which broke USB (`-qb`) games instead.
The menu now saves nothing (the game redraws over the box): ee_core ends at 0x8fe48 and both work on the console.
Keep ee_core's `_end` well under 0x94000.
