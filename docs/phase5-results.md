# Phase 5 results: sound

- 5 synthesized cybercore Y2K sounds (`tools/sfx.py` → `sfx/*.wav`), approved on the PC.
- Built into the ELF as SPU2 ADPCM (`adpenc`): 154 KB, played by audsrv on free SPU2 voices.
- The splash starts in sync with its sound after the black hold. UI events: ← → `move` / `edge`, X `confirm`,
  SELECT `panel`.
- PCSX2 (2026-10-01): audsrv inits, splash at 60 Hz (16 625 µs/frame by COP0), no errors.
- Console (2026-10-01): the user says "quedó perfecto". **Gate: PASS.** That run left no USB log, so frame times with
  sound were not measured; the last log before sound was max 4805 µs, 0 missed. Sound adds one audsrv RPC per
  button event.
