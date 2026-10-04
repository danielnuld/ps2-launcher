## ADDED Requirements

### Requirement: Achievements for nuld games
A game listed from the catalog SHALL take its RetroAchievements hash from the catalog line, never reading the
executable over the network. When such a game is launched with telemetry, the extra Neutrino config SHALL load only
the agent, since the game's source already loads smap and ministack.

#### Scenario: Chip without reading the ISO
- **WHEN** Black from nuld is selected for the first time
- **THEN** "LOGROS 56" appears with no executable read (no `hashed in` line in `launcher.txt`)

#### Scenario: Telemetry over udpfs
- **WHEN** Black from nuld runs with achievements on
- **THEN** the client shows the console streaming while the game loads its data over udpfs
