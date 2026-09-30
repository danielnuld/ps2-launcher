## ADDED Requirements

### Requirement: Timing source
Every benchmark SHALL be timed with COP0.Count (one tick per EE cycle, ps2tek:1117-1121) around a DMA send plus the GS FINISH wait, and SHALL report EE cycles and microseconds at 294.912 MHz.

#### Scenario: Timed run
- **WHEN** a benchmark runs
- **THEN** it reports the median of 16 repetitions in cycles and microseconds

### Requirement: Benchmarks per mode
The bench SHALL run, in each of: 720p from 640×360 (×2 upscale, CT32), 720p native CT16, 720p native CT32, 1080i from 960×540 (×2, CT32):
- B1 full-screen flat clear
- B2 1000 flat 16×16 sprites
- B3 upload of a 256×256 CT32 and a 256×256 PSMT8 texture (EE → GS image transfer)
- B4 1000 textured 16×16 sprites from a 256×256 texture

#### Scenario: All modes covered
- **WHEN** the bench completes a cycle
- **THEN** it has one result row per (mode, benchmark) pair

### Requirement: On-screen results
Results SHALL be drawn with flat-sprite 7-segment digits so that reading them on a console needs no font, no storage and no host link.

#### Scenario: Reading on a TV
- **WHEN** a mode's benchmarks finish
- **THEN** the screen shows mode number and one line per benchmark with its median microseconds for at least 10 seconds

### Requirement: Gate
The phase-0 results SHALL state, from measured numbers, which video mode and color depth leave at least half of a 16 667 µs frame (60 Hz) free after B1 + B3(PSMT8) + B4, and SHALL mark every number as PCSX2 or console.

#### Scenario: Decision recorded
- **WHEN** console numbers exist
- **THEN** `docs/phase0-results.md` names the chosen mode/depth, or states that none passes and what to relax
