# Phase 16b results: RetroAchievements in-game agent

Coded on 2026-10-03: launcher builds without warnings, host selftests pass, the fork builds with
`tools/build_neutrino.sh` (now on Neutrino dev 7be8de2). **Works end to end in PCSX2; the console is pending.**

## Sizes

- ee_core with `ra.c`: `_end` 0x93FC8, 56 bytes short of the 64 KB region (0x94000). Direct reads only, no chains.
- `raagent.irx` 3 809 bytes. Black's watch block: 51 entries at 0xAFFF0, mailbox 0xB5000 (ModStorageStart +
  128 KB, past the module checksum), snapshot buffer behind it.

## PCSX2 (2026-10-03)

- Launch line: `... -igroff=0x0f06 -cfg=ra -ra=mass0:/orbit/ra/SLUS-21376.wl -qb`; the loader logs
  `ORBIT ra: 51 entries (192 bytes) at 0xafff0, mailbox 0xb5000`; raagent starts (twice: Black resets the IOP).
- Client on nuld: `console started SLUS_213.76`, `watch list: 51 addresses, 192 bytes per snapshot`,
  `achievements: 56 total, 0 unlocked, 0 cannot unlock here`; `/state` after 2 minutes: 6 762 frames, 0 torn,
  0 duplicates, 2 gaps; `3600 snapshots | 0 skipped by console`. One packet per snapshot (376 bytes).
- How it was tested: PCSX2's sockets networking NATs the PS2 (DHCP 192.0.2.100) while the client answers to the IP
  inside each request, so a PC relay (scratchpad `relay.py`) stood in for the client: `[logros] servidor` = the PC,
  requests passed on with the PS2's IP rewritten, answers returned to the NAT address. Not needed on the console.

## Found on the way

- The client pairs a snapshot's serial (Neutrino's GameID, `SLUS_213.76`) with the hash of a `RAQ1` that carried the
  same serial: the launcher now sends `SLUS_213.76`, not the dash form.
- A 16 KB static array in Neutrino's loader (its BSS) made every launch fail at start-up (TLB misses at address 0,
  garbled `argv`), with or without `-ra`. The list is now read straight into module storage.
- `RAC1` carries raw bytes: the reply's padding spaces are only trimmed for text replies.

## Logros panel in the launcher (user request, 2026-10-03)

- ○ on a game with a set opens a panel in the options panel's style: trophy icon (new `UI_TROPHY_18` in
  `tools/ui_art.py`), title, "earned / total · points / total PTS", one row per achievement (title, description,
  points; gold trophy when earned), earned first; ↑↓ move, ←→ a page, ○ / △ close. "○ Logros" replaces the
  "SELECT Datos" hint (SELECT still works) so the footer fits.
- Data: RetroAchievements' game id from the public `dorequest.php?r=gameid&m=<hash>` (HTTPS, no login: Black =
  19040), then the client's `GET /game?id=` (plain HTTP, port 18280, the user's progress included). PCSX2: 56
  achievements, 600 points, list in a few seconds. Selftest parses a saved answer (`make test RA_GAME=<file>`).
- Needs the client's page open to the LAN (it is). HTTP reads are polled with a 5 s limit (no SO_RCVTIMEO in lwIP).

## Console checklist (gate)

1. Copy `launcher.elf`, and `dist/neutrino/neutrino.elf`, `modules/ee_core.elf`, `modules/raagent.irx` over the
   USB's `neutrino/` (keep the old ones as `.bak-16a`). Black must boot as before (new ee_core layout).
2. Black with the cable in: the client's LIVE tab shows the console within a minute; note snapshots / skipped /
   gaps after 5 minutes and `rc` (read cycles) in the RA15 header.
3. First level: "Veblensk City Street" unlocks on the profile.
4. A game without a set: launch line without `-cfg=ra`.
5. ○ on Black: the Logros panel lists 56 achievements; after an unlock, it shows gold and first.
