## Context

- phase-16b gives the game an IOP agent on ministack and a 64-byte mailbox in the watch block that the agent fills by
  SIF DMA and ee_core reads through `UNCACHED_SEG` every VBLANK.
- xeRAbora's notice (`ee_core/src/ra_overlay.c` @f4d559a, AFL-3.0): the agent DMAs a `struct ra_event` (magic, seq,
  kind, arg); the VBLANK handler, on a new seq, blends circuit 1 against BGCOLOR by writing PMODE (`SLBG 1`, `ALP`)
  and BGCOLOR: 12 frames down to half weight, 48 frames back up, then `ALP 0xFF` and black. Its comment: the game
  rewrites PMODE on its next flip, VRAM cannot be written under a running game, so a flash and not a picture.
- Phase 13 draws only over a paused game (image transfers while the game's threads are stopped).

## Goals / Non-Goals

**Goals:** a visible, game-safe sign of each unlock; the ids kept for phase-16d.

**Non-Goals:** text or badge pictures over the running game; sound (SPU2 belongs to the game); `RAR1` (reset from
the PC; the IGR menu already does it).

## Decisions

1. **Pulse, not pause-and-draw.** Proven on hardware by xeRAbora, no pause, a handful of register writes from the
   handler phase 12 already runs. Pause-and-draw (phase 16's first plan) would stop play for seconds per unlock.
2. **Timing and colour as xeRAbora** (12 + 48 frames, floor ALP 0x80, gold) to start; tuned on the TV if it reads
   badly (a calibration knob: two constants).
3. **Events through the mailbox** of phase-16b: the agent writes an event record after the snapshot-buffer word;
   ee_core compares the sequence number with the last one seen. Duplicate ids within 60 frames are ignored.
4. **Session list**: up to the set's size of ids in the watch block after the mailbox, appended by ee_core on each
   new event; phase-16d reads it from the menu.

## Risks / Trade-offs

- [PMODE writes clash with games that use both read circuits or change PMODE mid-frame] → the game's own next write
  wins (the pulse may look shorter); test with Black and one more game.
- [The DMA'd event lands while ee_core reads it] → seq written last by the agent (trailer pattern, as snapshots).
- [ee_core space] → same answer as phase-16b decision 4 (blob if needed).

## Open Questions

- Whether the pulse reads well over 480p (`-gsm=fp2`), where GSM also writes the display registers.
