## 1. Client on nuld

- [ ] 1.1 Install xerabora's client on nuld (Linux x86_64 build, pinned version) as a systemd user service next to `udpfs`, bound to the LAN; first login to the user's RetroAchievements account through its web UI
- [ ] 1.2 From the PC, check the client answers `RAP1` with `RAO1` and `RAQ1` (Black's hash) with `RAA1` (a small Python probe in the scratchpad, not in the repo)

## 2. Hash (host first)

- [ ] 2.1 `iso.c`: locate the BOOT2 executable (sector, size) and read it in chunks
- [ ] 2.2 `achievements.c`: `ra_hash` = MD5(exe name as rcheevos extracts it + exe bytes); wolfSSL's MD5 if available in the ps2sdk build, else RFC 1321
- [ ] 2.3 Host selftest (`make test`): our hash of Black.iso equals rcheevos' `rc_hash_ps2` built on the host from its sources (`RA_CHECK=<iso>`)
- [ ] 2.4 Cache `mass0:/orbit/ra/<serial>.txt` (hash, ISO size, exe size); hashing in the background loader thread

## 3. Client protocol in the launcher

- [ ] 3.1 UDP on the launcher's lwIP: discovery (`RAP1` broadcast / `[logros] servidor`), `RAQ1` with retries on `WAIT`, session memory of answers; network brought up when `[logros]` is on even if covers are off
- [ ] 3.2 `config.ini [logros]` (`activos`, `servidor`) with defaults and comments

## 4. UI

- [ ] 4.1 Detail card chip "LOGROS <n>" / "LOGROS ·" / none; "Logros: sin servidor" status line once
- [ ] 4.2 PCSX2 with the network on: chip appears for Black (client on nuld), frame windows unchanged

## 5. Gate (console)

- [ ] 5.1 Console: Black shows its achievement count; a game without a set shows no chip; second boot uses the cache; 0 missed vsyncs while hashing; `docs/phase16-results.md`

## 6. Next changes (not in this one)

- [ ] 6.1 Propose step 2 (in-game agent: watch list and per-frame snapshots, ee_core space decision), step 3 (unlock notice) and step 4 (pause-menu list) as their own changes
