## 0. Gate 0: udpfs over a cable

- [ ] 0.1 nuld on Ethernet (keeps 192.168.100.81 on eno1); Black from `udpfs` (today's phase 14 path) boots on the console and plays its intro; load time noted in `docs/phase17-results.md`

## 1. Catalog service on nuld

- [ ] 1.1 Host tool `orbit_hash` (achievements.c + iso.c + vmc.c): prints `serial hash` for an ISO, creates a VMC; host selftest reuses the existing ones
- [ ] 1.2 `tools/orbit_catalog.py`: `GET /catalog` (cached by path / size / mtime), `POST /vmc/<serial>`; user service `orbit-catalog` on nuld, port 18290
- [ ] 1.3 From the PC: `curl nuld:18290/catalog` lists Black with `8ee64a78...`; a new ISO appears hashed once

## 2. Launcher

- [ ] 2.1 `[juegos] servidor` (config template + default); `udpfs` source = catalog entries (no udpfs mount, lwIP stays up); toast when the catalog is missing
- [ ] 2.2 Hash and serial from the catalog (no executable read); covers and ○ panel as for USB games
- [ ] 2.3 Launch: `-bsd=udpfs -dvd=udpfs:<path>`, VMC via `POST /vmc`, no `-qb`; `ra.toml` with raagent only
- [ ] 2.4 Host selftest: catalog line parser (tabs, long titles, Latin-1)

## 3. Gate (console)

- [ ] 3.1 `origen = udpfs`: games listed from nuld, covers, LOGROS chip without a `hashed in` line, ○ panel
- [ ] 3.2 Black from nuld boots without `-qb`; the client shows it streaming; load time vs gate 0
- [ ] 3.3 A save in Black from nuld survives a reboot (VMC on nuld); `docs/phase17-results.md`
