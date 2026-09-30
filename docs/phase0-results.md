# Phase 0 results

`bench.elf` cycles 4 modes; per mode it prints (PCSX2 log) and shows on screen the median of 16 runs.
On screen: text with the mode, instructions and a table of cycles/µs per benchmark (BIOS font); top-right square green = results appended to `mass0:/bench.txt` (red = no USB or write failed).

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

## Console (SCPH-75001, ROM 2.20, HDMI adapter) — 2026-09-30

Raw file: `docs/console/bench-2026-09-30.txt` (3 cycles of modes 1-2, 2 of modes 3-4). Median of the cycles below;
spread between cycles ≤ 0.75 % for every cell. µs = cycles / 294.912.

| Bench | Mode 1 (640×360 CT32 ×2) | Mode 2 (1280×720 CT16) | Mode 3 (1280×720 CT32) | Mode 4 (960×540 CT32 ×2, 1080i) |
|---|---|---|---|---|
| B1 full-screen clear | 126 µs | 492 µs | 570 µs | 236 µs |
| B2 1000 flat 16×16 | 258 | 245 | 296 | 261 |
| B3 upload 256×256 CT32 (256 KiB) | 249 | 250 | 249 | 247 |
| B3 upload 256×256 PSMT8 (64 KiB) | 104 | 106 | 117 | 105 |
| B4 1000 textured 16×16 | 327 | 331 | 368 | 313 |

Derived (measured numbers only):
- Clear fill: 1823 / 1874 / 1615 / 2194 Mpx/s (pixels ÷ B1 time).
- EE→GS image upload: ~1050 MB/s for 256 KiB CT32 in every mode; the 64 KiB PSMT8 upload is not 4× faster (≈104 µs), so a fixed per-transfer cost is visible.
- CT16 vs CT32 at 1280×720: clear 492 vs 570 µs, textured sprites 331 vs 368 µs.
- PCSX2 was 10-90× lower on these same benchmarks (e.g. clear 6 µs), confirming the PCSX2 finding above.

## Gate (spec: B1 + B3 PSMT8 + B4 ≤ 8 333 µs, half a 60 Hz frame)

| Mode | B1 + B3(T8) + B4 | Share of 16 667 µs | Pass |
|---|---|---|---|
| 1 | 557 µs | 3.3 % | yes |
| 2 | 928 µs | 5.6 % | yes |
| 3 | 1055 µs | 6.3 % | yes |
| 4 | 655 µs | 3.9 % | yes |

Every mode passes with > 93 % of the frame left, so GS/DMA time does not decide the mode; VRAM and image quality do.
The adapter shows all five hdtest modes (../ps2-hdtest/results/hdtest-2026-09-30.txt).

VRAM (page-rounded, 1 048 576 words total): 1280×720 CT32 = 942 080 words (one buffer only, 106 496 words left),
1280×720 CT16 = 491 520 words (two buffers = 983 040, 65 536 words = 256 KiB left). Mode/depth decision: pending user.


## Mode choice test (`make APP=modetest`)

User chose to compare on the TV before phase 1. `modetest.elf` shows three 720p variants with Gouraud gradients
(banding) and a fast full-height bar (tearing): 1) CT16 double buffer, 2) CT16 + GS dither, 3) CT32 single buffer.
L1/R1 switch variants, D-pad moves DISPLAY DX (±4) / DY (±1) to fix the position the user saw misplaced,
SELECT toggles the ps2sdk / OPL GSM origin, Triangle appends variant + DX/DY + last frame time to `mass0:/modetest.txt`.
PCSX2 (correctness only): controls and file write work; CT16 shows ~32 steps on the grey ramp, CT16+dither and CT32 look smooth.
Console result: pending.
