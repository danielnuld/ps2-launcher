# Phase 16 results: RetroAchievements, step 1 (hash and LOGROS chip)

Coded on 2026-10-03 (launcher builds without warnings; host selftests pass). Tried in PCSX2 without network.
Client on nuld running and signed in. Console (SCPH-75001, fixed IP 192.168.100.250): Black shows LOGROS 56 in
under 10 s and then boots.

## Hash

- Black (`Downloads\Black\Black.iso`, SLUS-213.76): `8ee64a784fa9c9ac0a828be9a24f3679`, executable 3 371 868 bytes.
  rcheevos (f87c0de, `rc_hash_generate` with `RC_CONSOLE_PLAYSTATION_2`, built on the host from its sources) gives
  the same hash. `make test RA_CHECK=<iso>` prints ours.
- PCSX2 (5 GB USB image with Black): the launcher computes the same hash in 968 ms of emulated time, with 0 missed
  vsyncs. On the next boot it reads `mass0:/orbit/ra/SLUS-21376.txt` and does not read the executable again.
- The 1 MiB stub ISOs of the old test image have no executable: no hash, no chip.

## Client on nuld (from the PC, a Python probe speaking the PS2's side)

- `RAP1` (broadcast-free, to 192.168.100.81) -> `RAO1 OK xerabora/0.1.0-alpha.9`: the LAN reaches UDP 18194.
- `RAQ1 8ee64a78... SLUS-21376` -> `RAA1 WAIT`, then `RAA1 OK 220 1 56 0 0 Black` one second later: Black (RA game
  19040), 56 achievements, 51 watched addresses.
- PCSX2 cannot join the LAN here (no Npcap; its Sockets mode is NAT, and the client answers the IP inside the
  request), so the network path is checked on the console.

## PCSX2 (no network)

- While the worker hashes or asks: "LOGROS ·" chip. With no network (PCSX2's DEV9 is off) the chip goes away and the
  toast "Logros: sin red" shows once. With network but no client: "Logros: sin servidor".

## Setup

- `config.ini` keeps its old text when it exists. Without a `[logros]` section the defaults apply (`activos = si`,
  client found by broadcast). Add `[logros]` `servidor = 192.168.100.81` to skip the broadcast.
- nuld: xerabora's client (`xerabora-linux-x86_64`, release v0.1.0-alpha.9), signed in to the user's
  RetroAchievements account, UDP 18194 open. The UDP socket binds to every interface; the web page stays on
  localhost (`ssh -L 18280:localhost:18280 nuld`, then http://localhost:18280 to sign in). Systemd user service
  `xerabora` (`~/ps2/xerabora --no-sound`, `Restart=always`: the client exits when its page is closed), next to `udpfs`. Without a login it answers every `RAQ1` with `WAIT`.

## Console (2026-10-03)

- `launcher.txt`: `ra: SLUS-21376 hashed in 2526 ms: 8ee64a78...` (same hash as the PC), then
  `result 2, 56 achievements, Black (client xerabora/0.1.0-alpha at 192.168.100.81, me 192.168.100.250)`.
- Three bugs found on the way, all fixed:
  1. A fixed IP came out 0.0.0.0: `inet_aton` is `libcglue_inet_aton`, which needs the socket glue that `ps2ipInit`
     installs; `net_up` parsed before it. Now lwIP's `ip4addr_aton`, plus `ps2ip_setconfig` as ps2sdk's sample.
     (This also broke the cover download with a fixed IP.)
  2. ps2sdk's lwIP has no `SO_RCVTIMEO`: a lost reply blocked `recvfrom` forever ("LOGROS ·" for good). Polled with
     lwIP's `MSG_DONTWAIT` (0x08, not newlib's 0x80).
  3. Black did not start after the launcher's network was up: netman keeps DMAing received frames into EE RAM under
     Neutrino. `net_down` (ps2ipDeinit + NetManDeinit) before a launch; and a launch waits up to 1.5 s for the
     achievements worker (`ra_abort`) so no ISO read or socket call is left half-way.
- With `origen = usb, udpfs`, Neutrino's smap owns the adapter and achievements say "Logros: sin red".

## Console checklist (gate)

1. Black selected: "LOGROS ·", then "LOGROS <n>" with the client running on nuld. `launcher.txt` / the log shows
   `ra: client ... at ...` and `ra: SLUS-21376 <hash>: result 2, <n> achievements, <title>`.
2. A game without a set: no chip.
3. Second boot: no `hashed in` line for Black (the cache).
4. Client stopped: toast "Logros: sin servidor" once; no chips.
5. Frame windows in `launcher.txt`: 0 missed vsyncs while a game is hashed.
