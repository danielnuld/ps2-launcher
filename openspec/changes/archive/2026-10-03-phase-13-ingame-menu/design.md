## Context

Phase 12 left an IGR in the ORBIT fork of Neutrino (`third_party/neutrino-igr`): a VBLANK handler watches the
game's libpad buffer, and a thread reboots (rom0:OSDSYS) or the handler powers off. The console run showed how
fragile this ground is: kernel patches, the game's DMAC handlers, the SPU2, the caches. PCSX2 with the 5 GB Black
image is a good bench for logic, but it does not model the caches and does not show GS background colours.

The GS privileged registers (DISPFB, DISPLAY, PMODE) and the general drawing registers (FRAME, TEST, SCISSOR...)
are write-only. A menu over a running game therefore cannot read the game's GS state to restore it.

## Goals / Non-Goals

**Goals:**
- Prove on the console that a paused game can be drawn over and resumed (spike).
- Then a three-item menu in every video mode, with the phase 12 actions behind it.

**Non-Goals:**
- Muting the game's sound while paused.
- Save states, screenshots, cheats, settings changed from the menu.
- Apps and PS1 games (POPStarter has its own IGR).

## Decisions

- **Image transfers only.** The menu never sets FRAME, TEST, ALPHA, SCISSOR, XYOFFSET or any drawing register. It
  uses BITBLTBUF / TRXPOS / TRXREG / TRXDIR, which every game rewrites before each upload. Local→host reads save
  the area; host→local writes put up the menu and the saved area. Alternative: draw sprites with the GS. It would
  look better, but the game's drawing state could not be put back.
- **Rendering on the EE.** The saved pixels are darkened for a translucent panel, and the text is drawn with a small
  bitmap font, all in the frame's own format (CT32/CT24/CT16/CT16S). The result is uploaded as one rectangle.
- **Where the shown frame is.** GSM's breakpoint on GS privileged writes (`gsm_api.c`, `ee_exception_l2.S`) already
  sees every DISPLAY write; it also records DISPFB1/2. Without `-gsm` the fork arms the same breakpoint in a
  capture-only mode that rewrites nothing. Alternative: guess the frame from common addresses. Rejected: games
  double-buffer at arbitrary addresses.
- **Pause.** The VBLANK handler sees the menu combo and wakes a menu thread at priority 0, after:
  - suspending the game threads, remembering which ones it suspended;
  - saving the INTC mask and the DMAC channel interrupt masks, then holding all of them but SBUS.
  
  The thread waits for the GIF and VIF1 DMA (and the GIF FIFO) to go idle before any transfer. Cancelar undoes it
  in reverse order. Unlike the reboot, nothing is reset.
- **Input while paused.** The game's libpad buffer keeps being filled by the IOP's padman over SIF DMA, with no game
  EE thread involved. The menu thread polls it as the handler does today, with edge detection.
- **Combos.** A new `-igrmenu=<mask>` loader option and eecore field. The launcher passes it only to a fork that
  knows it (same text-sniffing as `-igrexit`).
- **Cache discipline.** Every packet and pixel buffer is written back (`FlushCache(0)` / `SifWriteBackDCache`) or
  built in uncached memory before DMA. Lesson of phase 12: the console punishes what PCSX2 forgives.

## Risks / Trade-offs

- [Where the saved pixels live] → see Open Questions. The spike measures it.
- [A game redraws only part of its frame] → only matters if the saved pixels are not put back. With the save, it
  does not matter.
- [A game flips DISPFB from its own interrupt handler] → interrupts are held while paused, and the capture gives the
  latest DISPFB at pause time.
- [GSM rewrites DISPFB for 480p/1080i] → the capture records the game's values and GSM's own, and the spike checks
  which one the shown frame uses.
- [Pausing mid-DMA] → waiting for idle completes the transfer; its completion interrupt stays pending until resume.
- [Timers and audio drift during the pause] → games may stutter for a frame after resuming; accepted.
- [ee_core budget, 64 KB with 31 KB used] → font about 1 KB, menu code a few KB.

## Open Questions

- **Saved pixel storage.** A 200×80 box is 64 KB in CT32 and 32 KB in CT16, but `ee_core` only has about 19 KB
  free between `_end` and its stack. Candidates for the spike, in order:
  1. A smaller box for the first spike (one line of text).
  2. IOP RAM, reached directly from the EE in kernel mode at 0xBC000000, as `sbv_patches`' smem does, in a block
     the game's IOP heap does not use.
  3. Not saving at all and letting the game's next frames overwrite the menu (most games redraw every frame).
- Which INTC and DMAC channels must stay enabled for padman's SIF DMA to keep filling the pad buffer: SBUS
  for sure, and possibly the SIF0 channel interrupt.
