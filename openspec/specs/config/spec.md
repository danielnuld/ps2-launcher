# config Specification

## Purpose
TBD - created by archiving change phase-7-config-video. Update Purpose after archive.
## Requirements
### Requirement: Config file
At boot the launcher SHALL read `mass0:/orbit/config.ini`. If it is missing, it SHALL write it with the defaults
and a comment per key:
- `[juegos] origen = usb`;
- `[video] modo = 480p`;
- `[sonido] volumen = 100`.

Unknown keys SHALL be ignored, and a source other than usb SHALL show a notice that it is not available yet.

#### Scenario: First boot
- **WHEN** the USB has no `orbit/config.ini`
- **THEN** the launcher creates it with the defaults and uses them

#### Scenario: Volume
- **WHEN** `volumen = 50`
- **THEN** every UI sound plays at half its default level

### Requirement: Per-game options
△ on a game SHALL open an options panel in the ORBIT style:
- Video: Predeterminado / Nativo / 480p / 1080i.
- Compatibilidad de video: 0-3.
- Modos de compatibilidad: on/off for each of Neutrino `-gc` 0, 2, 3, 5, 7.

↑↓ moves between rows, ←→ (or X on a toggle) changes the value, and △/○ closes. The values SHALL be saved per
serial in `mass0:/orbit/juegos.ini` when the panel closes.

#### Scenario: Remembered
- **WHEN** Black is set to 1080i and the launcher restarts
- **THEN** Black still shows 1080i and launches with `-gsm=1080ix2`

### Requirement: Video chip
The header SHALL show the effective game video mode (480p, 1080i or Nativo) as a chip.

#### Scenario: Default
- **WHEN** a game has no per-game override and config says 480p
- **THEN** the chip reads 480p

