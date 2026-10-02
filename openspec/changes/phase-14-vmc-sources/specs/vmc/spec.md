## ADDED Requirements

### Requirement: Virtual memory card by default
Every PS2 game SHALL be started with a virtual memory card in slot 1 unless its mode says otherwise. Modes, from
`juegos.ini [<serial>] mc`, else `config.ini [memorycard] modo`, else `juego`:
- `juego`: `<source>/VMC/<serial>.bin`;
- `compartida`: `<source>/VMC/ORBIT.bin`;
- `fisica`: no virtual card.

HD Loader games SHALL always use the physical card. MMCE games in `juego` mode SHALL switch the MMCE to the game's
own card instead.

#### Scenario: First launch
- **WHEN** a game whose card does not exist yet is started
- **THEN** an 8 MB formatted card is written there, and Neutrino gets `-mc0=<source>VMC/<serial>.bin`

#### Scenario: Card cannot be written
- **WHEN** the device is full or read-only
- **THEN** no partial file is left and a toast says the card could not be created

### Requirement: Card format
`vmc_create` SHALL write an 8 388 608-byte PS2 memory card without ECC, formatted and empty, that mymcplus accepts.

#### Scenario: Selftest
- **WHEN** `make test` runs
- **THEN** the card is created, has the right size and lists 0 entries

### Requirement: Saves card
For a game on a virtual card, the saves card SHALL count and date the saves on that card and show its 3D icon; when
the card does not exist yet it SHALL say it is created on the first launch.

#### Scenario: Saves read back
- **WHEN** a card holds a save directory and files written by another tool
- **THEN** the directory is listed and the files read back unchanged
