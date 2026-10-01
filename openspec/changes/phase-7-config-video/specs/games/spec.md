## MODIFIED Requirements

### Requirement: Launch
X on a game SHALL start `mass0:/neutrino/neutrino.elf -dvd=usb:<DVD|CD>/<file>.iso -qb` with the launcher's own
second-stage loader (no IOP reset), after the confirm sound and a short launch screen, with the vsync interrupt
handler removed. The arguments SHALL also carry the game's effective video mode (`-gsm=fp2[:c]` for 480p,
`-gsm=1080ix2[:c]` for 1080i, nothing for native) and its `-gc=<digits>` when any compat mode is on. If Neutrino is
missing, a toast SHALL say where to put it.

#### Scenario: Game starts
- **WHEN** the user presses X on a real game on the console
- **THEN** Neutrino boots and the game starts in the chosen video mode
