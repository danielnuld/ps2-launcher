# Phase 0 results

`bench.elf` cycles 4 modes; per mode it prints (PCSX2 log) and shows on screen the median of 16 runs.
On screen: white squares = mode, yellow squares = benchmark row, digits = µs.

| Mode | Buffer |
|---|---|
| 1 | 720p from 640×360 CT32 (×2) |
| 2 | 720p native 1280×720 CT16 |
| 3 | 720p native 1280×720 CT32 |
| 4 | 1080i from 960×540 CT32 (×2, field) |

## PCSX2 (2026-09-30, PCSX2 at C:\Program Files\PCSX2)

| Bench | Mode 1 | Mode 2 | Mode 3 | Mode 4 |
|---|---|---|---|---|
| B1 full-screen clear | 1891 cy / 6 µs | 1931 / 6 | 1931 / 6 | 1911 / 6 |
| B2 1000 flat 16×16 | 7836 / 26 | 7836 / 26 | 7836 / 26 | 7836 / 26 |
| B3 upload 256×256 CT32 (256 KiB) | 36538 / 123 | same | same | same |
| B3 upload 256×256 PSMT8 (64 KiB) | 11962 / 40 | same | same | same |
| B4 1000 textured 16×16 | 9841 / 33 | same | same | same |

**Finding: PCSX2 does not model GS draw time.** Clearing 1280×720 costs the same as 640×360, and every mode gives identical numbers; only the amount of DMA data changes the result (B3 CT32 ≈ 3× B3 T8 for 4× the bytes). Consequence: PCSX2 is usable for correctness, **not** for any performance decision in this project. Inferred from these measurements, not from PCSX2 documentation.

## Console (SCPH-75001)

Pending. Needs `bench.elf` run on the console; read the digits per mode (photo of the TV is enough).

## Gate

Not decidable from PCSX2 (see finding). Waits for console numbers.
