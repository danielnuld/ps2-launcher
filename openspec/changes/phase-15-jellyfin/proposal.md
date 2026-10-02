## Why

After phase 14 the user wants the Jellyfin phase (2026-10-02): watch the movies and series of the home Jellyfin
server from the PS2, inside ORBIT. Playback is the risk: the PS2 cannot decode H.264 / HEVC, so the server has to
transcode, and the EE has to keep up with what comes back. This phase starts with a go / no-go spike.

## What Changes

- **`jellyfin.c`**: HTTP/1.1 client (Content-Length and chunked bodies) over BSD sockets (lwIP on the PS2), a small
  JSON tokenizer (\uXXXX to UTF-8), and the API calls: login, libraries, movies / series, episodes, poster JPEG,
  transcoded stream, playback reports. Host selftest offline and against a live server (`JF_CHECK`).
- **`mpegps.c`**: push-style MPEG program stream demuxer (video / audio payloads with PTS). Host selftest, and
  byte-for-byte against ffmpeg on a real Jellyfin stream (`PS_CHECK`).
- **Spike `jfplay.elf`** (`make APP=jfplay`): movie list → MPEG-2 through the IPU (ps2sdk `libmpeg`) + MP2 through
  `libmad` (ps2sdk ports) → audsrv, audio clock, numbers in `mass0:/jfplay.txt`.
- `config.ini [jellyfin] servidor / usuario / clave` (in the launcher's template too).
- After the gate: browsing in the launcher (libraries as filters, posters as covers, series → episodes), the player
  inside `launcher.elf`, pause / seek / resume.

## Capabilities

### New Capabilities
- `jellyfin`: browse and play the home Jellyfin server.

## Impact

- New: `jellyfin.c`/`.h`, `mpegps.c`/`.h`, `jfplay.c`; Makefile `APP=jfplay`, two selftests.
- `net.c`: `net_dev9` / `net_busy` (moved from `sources.c`, so the spike needs no disk drivers).
- Links ps2sdk `libmpeg` and ports `libmad` (spike only, for now).
