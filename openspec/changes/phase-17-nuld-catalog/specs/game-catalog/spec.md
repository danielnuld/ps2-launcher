## ADDED Requirements

### Requirement: Catalog service
A service on the game server SHALL answer `GET /catalog` over HTTP with one line per ISO under its `DVD/` and `CD/`
folders: relative path, size in bytes, serial (SYSTEM.CNF BOOT2, `SLUS_213.76` form), title (file name without the
OPL serial prefix and extension) and RetroAchievements hash, tab-separated. Hashes SHALL be the rcheevos
`rc_hash_ps2` value and SHALL be cached by path, size and modification time, so only new or changed ISOs are read.

#### Scenario: Black on nuld
- **WHEN** the launcher asks for the catalog and `DVD/Black.iso` is on the server
- **THEN** its line carries `SLUS_213.76` and the hash `8ee64a784fa9c9ac0a828be9a24f3679`

#### Scenario: New ISO
- **WHEN** an ISO is copied to the server's `DVD/` folder
- **THEN** the next catalog request lists it, hashing only that ISO

### Requirement: Virtual memory card on the server
The service SHALL create a game's virtual memory card (`VMC/<serial>.bin`, the launcher's 8 MB format) on
`POST /vmc/<serial>` when it does not exist, and leave an existing one untouched.

#### Scenario: First launch of a nuld game
- **WHEN** a nuld game with the default card mode is launched for the first time
- **THEN** its card file exists on the server before Neutrino starts

### Requirement: Launcher reads the catalog
With `udpfs` in `[juegos] origen`, the launcher SHALL list the server's games from the catalog, using its own
network stack, and SHALL NOT load Neutrino's network modules. The server address SHALL be `[juegos] servidor` (the
host that runs udpfs and the catalog); a missing catalog SHALL be named in a toast and the other sources still listed.

#### Scenario: Network kept
- **WHEN** the launcher starts with `origen = udpfs`
- **THEN** covers download, the LOGROS chip appears for Black from nuld, and the ○ panel opens
