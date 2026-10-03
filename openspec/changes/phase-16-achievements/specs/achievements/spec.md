## ADDED Requirements

### Requirement: Game hash
For each PS2 ISO the launcher SHALL compute the RetroAchievements PS2 hash as rcheevos `rc_hash_ps2` does: the MD5
of the BOOT2 executable name (the string rcheevos extracts from SYSTEM.CNF) followed by the bytes of that
executable, read from the ISO. The hash SHALL be cached in `mass0:/orbit/ra/<serial>.txt` with the ISO and
executable sizes, and recomputed only when a size differs. Hashing SHALL run in the background loader thread and
SHALL NOT delay input or drop frames.

#### Scenario: Same hash as RetroAchievements
- **WHEN** the launcher hashes Black's ISO
- **THEN** the hash equals the one rcheevos computes for the same ISO on the host

#### Scenario: Cached
- **WHEN** a game whose hash is cached is selected again, after a reboot
- **THEN** the executable is not read again and the cached hash is used

### Requirement: Client discovery and game query
With `[logros]` on, the launcher SHALL find the RetroAchievements client with xerabora's discovery (`RAP1`
broadcast on UDP 18194, `RAO1` answer), or use `[logros] servidor` when set, and SHALL ask it about the selected
game with `RAQ1 <hash> <serial> <ip> <port>`. `RAA1 OK` gives the achievement count and title, `RAA1 NO` means no
set, and `RAA1 WAIT` SHALL be retried a few times. Answers SHALL be remembered for the session.

#### Scenario: Game with a set
- **WHEN** Black is selected and the client knows its set
- **THEN** the launcher learns the achievement count and the set's title

#### Scenario: No client
- **WHEN** no client answers the discovery within a few seconds
- **THEN** the launcher stops asking until the next boot and shows that the server is missing

### Requirement: Achievements on the detail card
The detail card SHALL show a "LOGROS <n>" chip in its header row for a game with a set, a "LOGROS ·" chip while the
hash or the answer is pending, and no chip for a game without a set. When the client is missing, a one-line status
"Logros: sin servidor" SHALL be shown once. None of this SHALL appear when `[logros]` is off.

#### Scenario: Chip shown
- **WHEN** a game with 24 achievements is selected
- **THEN** its detail card shows "LOGROS 24" next to the media and source chips

#### Scenario: No set
- **WHEN** a game without a set is selected
- **THEN** no achievements chip is shown
