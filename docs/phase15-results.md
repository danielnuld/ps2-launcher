# Phase 15 results: Jellyfin (spike)

Coded on 2026-10-02. Host-checked; **the spike has not run on the console yet.**

## Checked off the console (Jellyfin 12.1.0, official Docker image, in the build container)

- Library: "Big Test (2020)" 1080p24 H.264 + AAC, 60 s; "Película Ñandú (1999)" 480p H.264 + AC3 5.1;
  series "Show Prueba" S01E01-02 (720p).
- `make test JF_CHECK="http://127.0.0.1:8096 orbit ps2"`: login, 2 libraries, 2 movies + 1 series with 2 episodes,
  posters as JPEG (15 354 / 7 891 bytes at 384 px), 2 MB of stream, start / stop reports (204), 401 with a bad token.
- Stream asked as the spike does: MPEG-2 Main 640x360 24 fps + MP2 48 kHz stereo 256 kbit/s, program stream,
  2 885 kbit/s overall; chunked, no ranges. PCM audio cannot be asked for (`container=mpeg` always gives MP2).
- `make test PS_CHECK=...`: 9 628 video / 944 audio packets, first PTS 48 750 / 47 848, 0 bytes skipped; video and
  audio identical to `ffmpeg -c copy`.
- Found on the way: `json_str` kept 4 bytes free for any character, so 32-character ids came back 29 long.

## Console gate (to do)

1. Add to `mass0:/orbit/config.ini`:
   ```
   [jellyfin]
   servidor = http://<IP de la PC>:8096
   usuario = <usuario>
   clave = <clave>
   ```
   The network settings are the `[red]` ones of the launcher (DHCP works).
2. `wsl bash build.sh APP=jfplay`, copy `jfplay.elf` to the USB, start it from uLaunchELF.
3. Play a movie for a minute (X), then O. Send `mass0:/jfplay.txt` back. GO if shown/s ≈ the stream's fps, late
   under 1 %, a-v within ±100 ms.
