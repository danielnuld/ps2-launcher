## ADDED Requirements

### Requirement: Game discovery
The launcher SHALL list every `*.iso` in `mass0:/DVD` and `mass0:/CD` whose serial can be found (SYSTEM.CNF
`BOOT2`, else an OPL `XXXX_NNN.NN.` file-name prefix), sorted by title. Each entry SHALL show its cover
(`covers/<SERIAL>.c16`, dash form) or a generic cover with its serial and title. The header SHALL show DVD or CD
from the folder.

#### Scenario: ISO larger than 2 GB
- **WHEN** an ISO's SYSTEM.CNF lies past 2 GB (Persona 4: LBA 1 761 296)
- **THEN** its serial is still read (64-bit seek)

#### Scenario: PS1 or non-game ISO
- **WHEN** an ISO has no PS2 BOOT2 line and no OPL serial in its name
- **THEN** it is skipped with a printed message

### Requirement: Launch
X on a game SHALL start `mass0:/neutrino/neutrino.elf -dvd=usb:<DVD|CD>/<file>.iso -qb` with ps2sdk
`LoadELFFromFile`, after the confirm sound and a short launch screen, with the vsync interrupt handler removed. If
Neutrino is missing, a toast SHALL say where to put it.

#### Scenario: Game starts
- **WHEN** the user presses X on a real game on the console
- **THEN** Neutrino boots and the game starts

### Requirement: Covers from the PC
`tools/fetch_covers.py <usb root>` SHALL write both cover sizes for every game on the USB that has a cover in
xlenore/ps2-covers, and keep existing covers unless `--force`.

#### Scenario: Fetch
- **WHEN** it runs on a USB with a game that has no cover yet
- **THEN** `covers/<SERIAL>.c16` and `_s.c16` appear and the launcher shows them
