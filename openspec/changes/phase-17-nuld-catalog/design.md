## Context

- Phase 14: `udpfs` is a source; `src_init` loads Neutrino's `smap.irx` and `ministack.irx ip=<fixed IP>` and
  `udpfs_ioman` into the launcher, mounts `udpfs0:` and lists `DVD/`, `CD/` like any device; `net_busy` then keeps
  the launcher's lwIP down (`net.c`). Games launch with `-dvd=udpfs0:/DVD/<file> -qb`, reusing those modules.
- Phase 16 needs the launcher's lwIP for UDP to the achievements client, HTTPS to RetroAchievements (game id) and
  HTTP to the client's page (○ panel); the cover download needs HTTPS. Two drivers cannot share the adapter, and
  ministack has no TCP.
- nuld (Ubuntu, always on) already runs `udpfs_server.py` (Neutrino dev, `/srv/data/ps2`, UDP 62966) and the
  xeRAbora client as user services. 2026-10-03: game loading over udpfs failed on the console with nuld on WiFi
  (UDPRDMA does not recover from a lost packet; session notes).
- Neutrino without `-qb` boots its own load environment from the config tomls (README); ORBIT's HD Loader launches
  already go that way (phase 14). `bsd-udpfs.toml` loads smap, ministack `ip=`, udpfs_ioman, udpfs_fhi.
- The launcher's hash (`achievements.c ra_hash`) builds on the host (`make test`) and matched rcheevos on Black.

## Goals / Non-Goals

**Goals:** nuld games listed, launched, and with achievements, covers and the ○ panel, as USB games.

**Non-Goals:** udpbd; the saves card / 3D icons for nuld games (their VMC is on nuld: the card says so); browsing
folders other than `DVD/` and `CD/`; more than one game server.

## Decisions

1. **Catalog over HTTP from nuld, not a udpfs mount in the launcher.** Keeps the adapter with lwIP. Alternative
   (an IOP UDP proxy on ministack plus an HTTP translator on nuld) is more code on both ends and still reads each
   executable over the network to hash it.
2. **Text format**, one tab-separated line per ISO (`path size serial hash title`): trivial to parse on the EE, no
   JSON reader needed, and readable with curl.
3. **Hash on nuld with the launcher's own C code**: a small host tool built from `achievements.c` + `iso.c` (the
   selftest already does it; libcrypto is on Ubuntu), called by the service for new or changed ISOs; results cached
   in a file keyed by path, size and mtime. One implementation of `rc_hash_ps2` instead of a second one in Python.
4. **Service in Python stdlib** (`http.server`), port 18290 (our choice), started as a user service; `POST
   /vmc/<serial>` creates the card with the launcher's `vmc_create` (same host tool), since Neutrino needs the
   file to exist and the launcher no longer mounts udpfs.
5. **Launch without `-qb`**: `-bsd=udpfs -dvd=udpfs:<path> -mc0=udpfs:VMC/<serial>.bin`; Neutrino resets the IOP and
   loads its own modules from `mass0:/neutrino/` (still the USB). `net_down` first, as for every launch.
6. **`ra.toml` variant**: for udpfs games only `raagent.irx` (smap + ministack come from `bsd-udpfs`), with the same
   `srv=` / `me=` arguments; the IP is the fixed one udpfs needs anyway.

## Risks / Trade-offs

- [udpfs loading still fails with nuld on a cable] → gate 0 first; if it fails, it is a Neutrino / UDPRDMA problem
  to solve or report upstream before this change goes on.
- [Neutrino's full boot (no `-qb`) does not find its modules or the server] → tried on PCSX2 is impossible (no
  udpfs server reachable with sockets networking: relay only covers UDP to 18194/18195); first console test is that.
- [ministack's udptty broadcasts IOP printf to UDP 18194, the client's port] → the client "learns" a console at port
  0 (seen 2026-10-03); harmless for telemetry, a concern for unlock notices (phase-16c): a ministack built without
  udptty, or the client told the console's port again, decided there.
- [Agent traffic next to udpfs' read stream] → one ~400-byte packet per frame; measure loading time of Black from
  nuld with and without achievements.

## Open Questions

- Whether `udpfs_ioman` / `udpfs_fhi` find the server by broadcast or need its address in the toml.
