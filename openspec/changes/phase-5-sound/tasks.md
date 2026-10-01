## 1. Sounds

- [x] 1.1 `tools/sfx.py`: cybercore Y2K synthesis, 5 sounds, `--selftest`; previews approved on the PC
- [x] 1.2 Makefile: `adpenc` + `bin2c` per WAV; libsd/audsrv IRX; `-laudsrv`

## 2. Playback

- [x] 2.1 `iop_load` / `usb_wait` split; `sound_init` in the loader thread (aligned copies, SPU2 upload)
- [x] 2.2 Splash start synced with the `splash` sound (6 s fallback); UI events play their sounds
- [x] 2.3 PCSX2: audsrv inits, splash at 60 Hz, no errors

## 3. Gate

- [ ] 3.1 Console: sounds play in sync and at good levels on the TV; frame gate holds (max ≤ 8 333 µs, 0 missed); user approves
