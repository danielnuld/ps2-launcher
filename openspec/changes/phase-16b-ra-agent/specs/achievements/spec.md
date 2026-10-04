## ADDED Requirements

### Requirement: Watch list before launch
When a game with a set is launched with `[logros]` on, the launcher SHALL fetch the game's watch list from the client
(`RAG1 <hash> <index> <ip> <port>`, one `RAC1` chunk per index until all chunks of `RAA1 OK` arrived), save it as
`mass0:/orbit/ra/<serial>.wl`, and pass it to Neutrino. A chunk that does not arrive SHALL be asked again; if the
list is still incomplete the game SHALL start without telemetry rather than not start.

#### Scenario: Black launched
- **WHEN** Black is launched and the client answered `RAA1 OK 220 1 ...`
- **THEN** `mass0:/orbit/ra/SLUS-21376.wl` holds the 220-byte list and the launch line carries `-cfg=ra` and `-ra=`

#### Scenario: Client gone at launch
- **WHEN** the client stops answering between the selection and the launch
- **THEN** the game starts as it does today, with no telemetry modules

### Requirement: Snapshots while playing
While the game runs, the fork SHALL read every watched address once per frame (after a start delay that lets the
game load) and send the values to the client as `RA15` snapshots in watch-list order, following xeRAbora's
`ra_snap.h` layout (header, values, trailer word repeating the sequence number). A frame whose previous snapshot is
still being copied SHALL be skipped, never waited for. Addresses outside the game's RAM SHALL be sent as zero and
never read.

#### Scenario: Client LIVE
- **WHEN** Black has been running for a minute
- **THEN** the client's LIVE tab shows the console connected, the set loaded and snapshots arriving

#### Scenario: Achievement unlocks
- **WHEN** the player completes Black's first level
- **THEN** "Veblensk City Street" appears unlocked on the user's RetroAchievements profile

### Requirement: Agent announces the console
The IOP agent SHALL announce the console to the client with `RAP1 <ip> <port>` when the game starts, repeating it
until `RAO1` arrives, and SHALL send `RAH1` heartbeats while the game runs. Telemetry SHALL stay silent until the
client answered.

#### Scenario: Client started after the game
- **WHEN** the client on nuld is started while Black is already running
- **THEN** within a minute the client shows the console connected and snapshots arriving

### Requirement: Game unaffected
The telemetry SHALL NOT change how the game runs: no added loading failures and no visible slowdown. A game launched
without a set, or with `[logros] activos = no`, SHALL get neither the agent nor the extra modules.

#### Scenario: Game without a set
- **WHEN** a game whose `RAQ1` answer was `RAA1 NO` is launched
- **THEN** its launch line has no `-cfg=ra` nor `-ra=`
