## 1. Launcher

- [ ] 1.1 `achievements.c`: `ra_watchlist` (`RAG1` per chunk, retries, checks against `RAA1 OK` bytes / chunks), saved to `mass0:/orbit/ra/<serial>.wl`
- [ ] 1.2 Launch: fetch the list for a game with a set, write `mass0:/neutrino/config/ra.toml` (smap, ministack `ip=<launcher's IP>`, raagent), add `-cfg=ra -ra=<file>`; without a list, launch as today
- [ ] 1.3 Host selftest: watch-list check (sizes, total, count) on Black's real list and on broken ones

## 2. Neutrino fork

- [ ] 2.1 `patch_neutrino.py`: `-ra=` loader option, new `ee_core_data` fields, the watch block (list, snapshot buffer, mailbox) after `ModStorageEnd`, `mbox=` appended to raagent's arguments
- [ ] 2.2 ee_core `ra.c`: VBLANK read loop + `isceSifSetDma`, skip while busy, start delay; measure its size in `ee_core.map` and decide inside ee_core vs blob (design decision 4)
- [ ] 2.3 `raagent/`: IOP module on ministack: mailbox handshake, `RAP1` until `RAO1`, `RA15` per new snapshot, `RAH1` every 10 s; `ra_snap.h` / `ra_watch.h` copied from xeRAbora's `protocol/`
- [ ] 2.4 `tools/build_neutrino.sh`: build and install `raagent.irx` with the fork

## 3. Checks

- [ ] 3.1 PCSX2: Black boots with `-cfg=ra -ra=`, the fork's log shows the watch block and the mailbox set; snapshots leave the emulated PS2 (sockets networking)
- [ ] 3.2 Console: the new ee_core boots Black (before any telemetry test)

## 4. Gate (console)

- [ ] 4.1 Black from the USB: client LIVE shows the console, snapshot rate and losses over 5 minutes of play (numbers in `docs/phase16b-results.md`), `read_cycles` per frame recorded; no visible slowdown
- [ ] 4.2 First level completed: "Veblensk City Street" unlocked on the RetroAchievements profile
- [ ] 4.3 A game without a set launches without the modules and runs as before
