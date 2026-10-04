## ADDED Requirements

### Requirement: Unlock notice
When the client sends `RAU1` for the running game, the TV SHALL show a gold pulse over the game picture within a
second, lasting about a second, and the game SHALL keep running during it. Each `RAU1` the agent acts on SHALL give
one pulse; a repeat of the same achievement id within a second SHALL be ignored (the protocol allows duplicates).
After the pulse the game's picture SHALL be exactly as before.

#### Scenario: First level done
- **WHEN** the player completes Black's first level and "Veblensk City Street" unlocks
- **THEN** the picture pulses gold once while the game goes on

#### Scenario: Duplicate notice
- **WHEN** the client sends the same `RAU1` twice in a row
- **THEN** only one pulse shows

### Requirement: Session unlocks remembered
The fork SHALL keep the ids of the achievements unlocked during the session, in memory the game does not use, for
the pause-menu list.

#### Scenario: Two unlocks
- **WHEN** two achievements unlock during a session
- **THEN** both ids are in the session list, in unlock order
