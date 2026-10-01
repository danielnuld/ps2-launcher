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

## Console (SCPH-75001): pending

Gate: max ≤ 8 333 µs, 0 missed vsyncs, and the user's verdict on the TV with one of the two sets.
