## 1. Pieces (host-checked)

- [x] 1.1 `jellyfin.c`: HTTP (chunked), JSON, API; selftest offline + live against Jellyfin 12.1 in Docker
- [x] 1.2 `mpegps.c`: demuxer; selftest + identical to `ffmpeg -c copy` on a real Jellyfin stream in uneven chunks
- [x] 1.3 `jfplay.c` spike builds (ps2dev v2.0.0)

## 2. Gate (console)

- [ ] 2.1 `jfplay.elf` lists the movies; X plays the test movie with sound
- [ ] 2.2 `mass0:/jfplay.txt`: shown/s close to the stream's fps, late pictures under 1 %, a-v within ±100 ms,
      network KB/s above the stream rate; O stops and Jellyfin shows the position (triángulo resumes)

## 3. After a GO

- [ ] 3.1 Launcher: Jellyfin libraries as filters, posters as covers (`cover_from_jpeg`), series → episode panel
- [ ] 3.2 Player inside `launcher.elf`: pause, ±30 s seek (new request with startTimeTicks), resume
