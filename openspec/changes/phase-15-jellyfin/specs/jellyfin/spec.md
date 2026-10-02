## ADDED Requirements

### Requirement: Server and login
`config.ini [jellyfin] servidor` SHALL take `http://host[:port][/prefix]` (port 8096 by default) and log in with
`usuario` / `clave`. An https URL, a wrong password or an unreachable server SHALL be reported on screen.

#### Scenario: Live login
- **WHEN** the selftest runs with `JF_CHECK="http://127.0.0.1:8096 orbit ps2"` against a server with that user
- **THEN** it gets a token and the user id, and a wrong token later gives 401

### Requirement: Library data
Libraries, their movies and series (by name), a series' episodes (season / episode numbers), the runtime, the saved
position and the poster SHALL be read; names SHALL be UTF-8.

#### Scenario: Non-ASCII title
- **WHEN** the server sends `"Película Ñandú (1999)"`
- **THEN** the item's name is "Película Ñandú (1999)" in UTF-8, and a short buffer never holds half a character

### Requirement: Playable stream
Playback SHALL ask Jellyfin for MPEG-2 video and MP2 audio in an MPEG program stream, at most 640x480, and demux it
from chunked HTTP into video and audio payloads with their PTS.

#### Scenario: Demux against ffmpeg
- **WHEN** a stream transcoded by Jellyfin is fed to the demuxer in uneven chunks
- **THEN** the video and audio payloads equal `ffmpeg -c copy`'s elementary streams byte for byte

### Requirement: Spike playback (gate)
`jfplay.elf` SHALL play a listed movie with sound, keep the pictures on the audio clock, report start / progress /
stop to Jellyfin, and log decoded, shown and late pictures, network rate and the audio-video gap every 5 s.

#### Scenario: Console
- **WHEN** the test movie plays for a minute on the SCPH-75001
- **THEN** shown pictures per second stay at the stream's rate with under 1 % late, and the gap stays within ±100 ms
