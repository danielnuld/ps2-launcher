## Why

"Extreme optimization" needs a baseline: without measured GS/DMA throughput per video mode, any engine design is a guess. Phase 0 measures what the hardware path costs before we write the engine.

## What Changes

- `bench.elf`: runs a fixed set of micro-benchmarks in each candidate video mode and shows results on screen with 7-segment digits (no font dependency) and via `printf` for PCSX2 logs.
- Results recorded in `docs/phase0-results.md`, split PCSX2 vs console.
- A go/no-go gate that fixes the engine's first architecture choices (video mode, color depth, buffering).

## Capabilities

### New Capabilities
- `render-bench`: measured cost of GS fill, sprite throughput, texture upload and textured draws per video mode.

### Modified Capabilities

## Impact

- New repo `Documents/ps2-launcher` (git + OpenSpec). Code starts from `../ps2-hdtest/hdtest.c` (mode setup + DISPLAY fix).
- Depends only on ps2sdk libs already installed (graph, draw, dma, packet).
- Console numbers need the user's SCPH-75001; PCSX2 numbers are marked as such.
