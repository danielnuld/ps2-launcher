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

## PCSX2 2.4 (BIOS SCPH-70012, DEV9 "Sockets" network to the Jellyfin in Docker), 2026-10-02

Bugs the emulator found in the spike, all fixed:
1. CT16 conversion on the EE: only ~8 of 24 pictures/s were shown → `gfx_mb32` (DMA straight from libmpeg's output).
2. `gfx_mb32`'s DMA packet was 62 qwords too small: heap corruption, freeze or exit to the browser ~5 s in.
3. audsrv called from two threads (not thread-safe); the audio thread spun on half an MP2 frame (starved lwIP).
4. Jellyfin handed the device an earlier, cut transcode: fresh `PlaySessionId` per playback + DELETE ActiveEncodings.
5. A 1.2 Mbit/s movie filled the 256 KB audio ring during the prebuffer and stalled: 1 MB ring, prebuffer ends early.
6. ps2sdk libmpeg: its colour-conversion DMA handler swaps the two QWC registers for the second run of 1023
   macroblocks, so pictures over 1023 macroblocks never finish (640x432, 640x480 hung): streams capped at 368 lines.
7. libmpeg hung on the stream's tail: the loop ends between pictures 0.5 s before the runtime.
8. O mid-movie terminated the audio thread inside audsrv and the next audsrv call hung: it now leaves by itself.

Result: the 60 s test movie plays to the end, 1 434 of 1 438 pictures shown, 3 late, a-v +18 ms; 4:3 (496x368),
3:2 (552x368) and a 29.97 fps 5.1 source (downmixed by Jellyfin) play; O returns to the list, Jellyfin keeps the
position. Timing in PCSX2 is not cycle-accurate: the console gate still decides.
