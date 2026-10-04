## ADDED Requirements

### Requirement: Language setting
`config.ini [ui] idioma` SHALL accept `es` and `en`; anything else or a missing key SHALL mean `es`. The launcher,
the in-game menu and Jellyfin SHALL show every text of their screens in that language. Config keys and values SHALL
stay the same in both languages.

#### Scenario: English
- **WHEN** `[ui] idioma = en` and the launcher starts
- **THEN** the hints read "Play", "Options", "View", the chip "ACHIEVEMENTS 56", and a launch says "STARTING WITH NEUTRINO"

#### Scenario: Unknown value
- **WHEN** `[ui] idioma = fr`
- **THEN** everything is in Spanish

### Requirement: Config comments follow the language
The config template SHALL exist in both languages. A missing `config.ini` SHALL be written in the language of
`idioma` (Spanish when there is no file to read it from). When the file's language differs from `idioma`, the launcher
SHALL write it again in `idioma`'s language with every current value kept, including keys the template does not
have.

#### Scenario: Switch to English
- **WHEN** the user changes `idioma = es` to `idioma = en` in a Spanish config.ini and reboots
- **THEN** config.ini's comments are in English and every value the user had set is unchanged

### Requirement: In-game menu language
The in-game menu SHALL list RESTART, POWER OFF and CANCEL in English and REINICIAR, APAGAR and CANCELAR in Spanish,
as the launcher passes it to the fork.

#### Scenario: English menu
- **WHEN** a game launched with `idioma = en` opens the menu
- **THEN** it reads RESTART / POWER OFF / CANCEL
