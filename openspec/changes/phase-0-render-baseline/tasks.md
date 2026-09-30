## 1. Setup

- [x] 1.1 `Makefile` + `build.sh` (same as ../ps2-hdtest)
- [x] 1.2 `bench.c`: mode table (4 modes) + `set_mode` with the DISPLAY fix from hdtest

## 2. Benchmarks

- [x] 2.1 COP0.Count timer + median of 16 helper, with an assert-based self-check of the median
- [x] 2.2 B1 clear, B2 flat sprites
- [x] 2.3 B3 texture upload CT32 and PSMT8
- [x] 2.4 B4 textured sprites

## 3. Output

- [x] 3.1 7-segment digit renderer (flat sprites)
- [x] 3.2 Per-mode result screen held 10 s; `printf` of the same numbers

## 4. Gate

- [x] 4.1 Run in PCSX2, record in `docs/phase0-results.md` (tagged PCSX2) — finding: PCSX2 does not model GS draw time
- [ ] 4.2 Run on SCPH-75001, record (tagged console)
- [ ] 4.3 Decide mode/depth per spec gate; open `phase-1-engine` change
