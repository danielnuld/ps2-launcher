# Phase 17 results: nuld game catalog

## Gate 0: udpfs over a cable (2026-10-03)

- nuld moved to Ethernet: 192.168.100.81 now on `eno1` (1000 Mb/s, static, netplan via `~/netplan-cable.sh`, old
  config in `/etc/netplan/00-installer-config.yaml.pre-cable`); WiFi kept on DHCP (.83, route metric 600).
- Console (SCPH-75001, fixed IP .250), `origen = usb, udpfs`, Neutrino dev 7be8de2 fork: Black from udpfs boots.
- udpfs server log: listing at 20:54:43 (`DVD/Black.iso`; `CD/` missing, harmless); Neutrino opened the ISO at
  20:55:11, first block read 4 s later, then 686 reads = 11.5 MB in ~12 s with no error or repeated request.
- On WiFi (2026-10-02/03) the same load stalled: UDPRDMA does not recover a lost packet. A cable is required.
- With udpfs listed, the launcher has no network of its own: achievements show "sin red" (what this phase fixes).

## Catalog and nuld games (2026-10-03)

- `orbit-catalog` on nuld (user service, port 18290; `orbit_hash` built static in WSL from the launcher's own
  achievements.c / iso.c / vmc.c, nuld has no compiler): Black listed as `SLUS_213.76` / `8ee64a78...`. `POST /vmc`
  replaced a 5.9 MB card left by the WiFi attempt (kept as `SLUS-21376.bin.short-*`).
- udpfs no longer needs a fixed IP: the launcher writes its current address (fixed or the DHCP lease) into
  `bsd-udpfs.toml` once its network is up.
- Console: catalog read (`GET /catalog 200` from .250), LOGROS 56 with no executable read, Black launched with
  `-bsd=udpfs -dvd=udpfs:DVD/Black.iso` and no `-qb`, and it boots.
- Found on the console: Neutrino puts `-cfg` modules first, so raagent loaded before bsd-udpfs' ministack (fixed with
  `load_order = 30`); without `-qb` the IOP is rebooted before the watch list was read from `mass0:` (now read before
  the reboot, into the heap).
- Telemetry from a nuld game: heartbeat mailbox 0xb5000, magic RAS1, ~64 snapshots/s; client "3600 snapshots |
  1 skipped by console | 0 repeated | 0 incomplete", 6 050 frames, 0 torn, 1 gap; udpfs meanwhile 2 383 block reads,
  0 retransmits, 0 aborts.
