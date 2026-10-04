## ADDED Requirements

### Requirement: Set list before launch
When a game with a set is launched, the launcher SHALL read the set from the client's page (`GET /game?id=<id>` on
the client's address, port 18280) and render the "Logros" panel: a header with the game's title and the earned /
total count, then one row per achievement with its title and points, earned ones first. The panel SHALL be passed to
the fork with the watch list. When the page does not answer, the game SHALL start with telemetry but without the
panel.

#### Scenario: Black's panel
- **WHEN** Black is launched with 0 of 56 earned
- **THEN** the panel holds 56 rows, the first "Veblensk City Street · 5"

#### Scenario: Page closed to the LAN
- **WHEN** the client's page only listens on localhost
- **THEN** Black starts with telemetry and the pause menu has no "Logros" item
