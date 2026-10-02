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
