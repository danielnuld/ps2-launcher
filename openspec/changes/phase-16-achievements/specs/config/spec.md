## ADDED Requirements

### Requirement: Achievements settings
The config file SHALL have a `[logros]` section: `activos = si | no` (default si) and `servidor = <ip>` (default
empty: find the client by broadcast). When the file is written with defaults, the section SHALL be included with a
comment per key. A launcher reading a config without the section SHALL use the defaults.

#### Scenario: Off
- **WHEN** `[logros] activos = no`
- **THEN** the launcher neither hashes games nor sends any achievements traffic

#### Scenario: Fixed client
- **WHEN** `[logros] servidor = 192.168.100.81`
- **THEN** the launcher queries that address directly without a broadcast
