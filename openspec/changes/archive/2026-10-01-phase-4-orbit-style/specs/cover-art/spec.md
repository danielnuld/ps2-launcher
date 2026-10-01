## MODIFIED Requirements

### Requirement: Streaming through one VRAM slot
The engine SHALL upload each cover into one reserved CT16 VRAM slot of 256×128 right before drawing it, in horizontal
bands of at most 128 lines, each band uploaded and then drawn. VRAM never holds more than one band, whatever the
number of covers on screen.

#### Scenario: Many covers per frame
- **WHEN** 6 different covers are on screen in the same frame
- **THEN** each one shows its own image, with no visible seam between bands at rest
