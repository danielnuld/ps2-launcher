## Context

The PS2's SPU2 has 24 hardware voices and 2 MB of RAM and plays PS-ADPCM natively. ps2sdk provides `libsd.irx`,
`audsrv.irx` (EE `libaudsrv`), and the `adpenc` WAV→ADPCM encoder with the 16-byte `APCM` header that
`audsrv_load_adpcm` reads (ps2sdk `iop/sound/audsrv/src/adpcm.c`: pitch in word 2, channels and loop in word 1).

## Goals / Non-Goals

**Goals:**
- Sound for the splash, navigation, confirm and panel.
- No CPU mixing.
- Sync with the splash timeline.

**Non-Goals:**
- Background music, streaming PCM, stereo panning per event.
- A volume setting (future settings screen).

## Decisions

- **Synthesized offline** in Python (FM, supersaw + resonant SVF, bitcrush, stutter, sub drop, a synced delay),
  so the sounds are original and can be tweaked in code. The WAVs are committed because the PS2 build in WSL has
  no NumPy.
- **ADPCM on SPU2 voices (audsrv)** instead of PCM streaming: no EE mixing and no feeding thread, and overlapping
  sounds come free. 48 kHz gives pitch 4096 (SPU2 native rate). Total about 154 KB.
- **Init order:** `iop_load` (all modules) → `sound_init` → `usb_wait`. The USB mount can take seconds, and the
  splash must not wait for it. The splash start waits for `sound != 0`, at most 6 s past the black hold.
- **Sample upload:** the bin2c arrays are not aligned, so each sample is copied into `memalign(64)` and written
  back with SyncDCache, uploaded, then freed (as ps2sdk `playadpcm.c` does).
- **Thread use:** the loader thread calls audsrv only during init. After `sound == 1`, only the render thread
  calls it.

## Risks / Trade-offs

- [audsrv RPC per event costs render time] → one RPC per press, measured in the frame gate.
- [Loudness on the TV] → per-event volume constants in `launcher.c` (move 70, edge 80, panel 70, confirm 85,
  splash 100). The user tunes them on the TV.
