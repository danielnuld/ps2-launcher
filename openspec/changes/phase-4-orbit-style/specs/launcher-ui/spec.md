## MODIFIED Requirements

### Requirement: Splash
From the first frame until loading ends, the UI SHALL show the animated ORBIT splash (orbit-style spec). Its progress
bar and status line ("INICIANDO USB", "LEYENDO MEMORY CARDS", "CARGANDO PORTADAS NN / NN") follow a loader thread.
When loading ends and the timeline has reached its last key, the splash SHALL fade to the home screen.

#### Scenario: Boot
- **WHEN** the ELF starts with N covers on the USB
- **THEN** the screen is never undrawn VRAM, and the bar reaches full width after the N-th cover
