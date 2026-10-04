## MODIFIED Requirements

### Requirement: Menu items and input
The menu SHALL list Reiniciar, Apagar and Cancelar, with Cancelar selected first, and "Logros" between Apagar and
Cancelar when the launcher passed a Logros panel for the game. The D-pad SHALL move the selection, X SHALL choose,
and ○ or START SHALL act as Cancelar. Reiniciar SHALL run the phase 12 reboot, and Apagar the phase 12 power off.
Logros SHALL open the Logros panel.

#### Scenario: Reboot from the menu
- **WHEN** the user selects Reiniciar and presses X
- **THEN** the console reboots as with the phase 12 reboot combo

#### Scenario: No panel
- **WHEN** the menu opens in a game launched without a Logros panel
- **THEN** it lists Reiniciar, Apagar and Cancelar only

## ADDED Requirements

### Requirement: Logros panel
The Logros panel SHALL show the launcher's rendered rows over the paused game, as many as fit, and the D-pad SHALL
scroll them. A row SHALL be marked earned when it was earned before the game started or its id is in the session's
unlock list. ○ SHALL return to the menu. The panel SHALL use only image transfers, as the menu does, and the game's
next frames SHALL repaint the area it covered after Cancelar.

#### Scenario: Unlocked this session
- **WHEN** "Veblensk City Street" unlocked during play and the user opens Logros
- **THEN** its row shows as earned

#### Scenario: Scroll
- **WHEN** the user holds down on the D-pad in Black's 56-row panel
- **THEN** the rows scroll to the last one and stop there
