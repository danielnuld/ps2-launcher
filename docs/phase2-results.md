# Phase 2 results: real covers

## How to make covers

```
python tools/covers.py <folder with PNG/JPG> <usb>/covers              # Floyd-Steinberg (default)
python tools/covers.py <folder with PNG/JPG> <usb>/covers_nd --no-dither
```
The file name (without extension) is the game's serial. Sample set: 15 covers from xlenore/ps2-covers
(`covers/default/<serial>.jpg`, 512×736), not committed (copyright). The PCSX2 USB image's OPL `ART/*_COV.png`
files are ~20 KB each, so probably low resolution (not measured).

## PCSX2 (2026-10-01): correctness only

- 15 covers × 2 sets loaded. 5 different covers in one frame each show their own image, so streaming through one
  VRAM slot keeps its order (spec scenario "Many covers per frame").
- Sorted by title. The selection frame and the titles are right.
- Times (not performance data, phase-0 finding): load 1577 ms, frame median 582-699 µs, max ≤ 805 µs, upload 94 µs
  per cover, 0 missed vsyncs.

## Console (SCPH-75001, HDMI adapter) — 2026-10-01

Raw file: `docs/console/covers-2026-10-01.txt` (3 windows of 600 frames, dithered set, 15 covers, 5-6 visible).

| Window | Median | Max | Missed vsyncs | gfx_image per cover |
|---|---|---|---|---|
| 1 | 3064 µs | 3635 µs | 0 | 644 µs |
| 2 | 3475 µs | 3669 µs | 0 | 600 µs |
| 3 | 3111 µs | 3649 µs | 0 | 632 µs |

Load: 15 covers × 2 sets (5.5 MB) in 3432 ms from USB.

- Times: max 3669 µs = 44 % of the 8 333 µs gate (22 % of a frame), 0 missed vsyncs. **Times pass.**
- `gfx_image` takes ~620 µs, not the ~180 µs estimated from the phase-0 upload rate. The timed span includes the DMA
  of everything queued before it: the first call carries the full-screen background, and each later call carries the
  previous cover's 256×368 sprite. The DMA cannot finish before the GIF has taken the data, so GS draw time is
  inside this number. It is an upper bound on upload cost, not the upload alone (not separated in this run).
- TV verdict (user): covers look sharp and good with the default dithered set (Floyd-Steinberg). The undithered
  set was not compared on the TV. **Chosen: Floyd-Steinberg (default).** **Gate: PASS.**

## Boot splash (user report 2026-10-01)

At boot the TV showed random colours for seconds, which looked as if the ELF had not loaded. Cause: `demo.c` reset
the IOP and waited for USB before setting up video, then `gfx_init` turned output on showing a never-drawn buffer
(VRAM garbage), and the 3.4 s cover load drew nothing. Fix: `gfx_init` clears both buffers before
`graph_enable_output`. The demo sets up video first and shows a splash ("Iniciando USB...", "Cargando portadas... N").
PCSX2: black → splash → counter → covers, no garbage. The time before `main` (the loader reading the ELF) is not
ours to draw.
