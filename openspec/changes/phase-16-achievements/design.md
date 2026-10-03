## Context

- Phase 13 gives the Neutrino fork a way to pause a game and draw over it; phase 12's VBLANK handler already runs
  ORBIT code every frame inside the game. `ee_core.elf` lives in a 64 KB region (0x84000-0x94000) with about 1 KB
  free (fork on Neutrino dev 7be8de2: load end 0x93C08).
- xerabora (github.com/hacan359/xerabora) runs RetroAchievements on a real PS2: an OPL fork streams watched memory
  every frame over UDP to a PC client (MIT) that evaluates it with rcheevos and posts unlocks. Its wire protocol is
  documented in `protocol/PROTOCOL.md`: UDP 18194, text verbs `RAP1` / `RAO1` (discovery), `RAQ1` / `RAA1` (game
  check), `RAG1` / `RAC1` (watch list chunks of 896 bytes), `RA15` (snapshot: 48-byte header, values, tear-check
  trailer), `RAU1` (unlock), `RAH1` (heartbeat every 10 s), `RAR1` (game reset).
- The launcher already brings the network up with lwIP (cover download, phase 8) and reads ISO 9660 directly
  (`iso.c`, SYSTEM.CNF / BOOT2 for the serial).
- The user's home server `nuld` (Ubuntu, always on, also Jellyfin and the UDPFS server) hosts the client. The PS2,
  nuld and the PC share 192.168.100.0/24; nuld is on WiFi for now (UDP loss happens, see Risks).

## Goals / Non-Goals

**Goals:**
- Step 1: the launcher knows whether the selected game has a RetroAchievements set and shows it, using the user's
  real account through the client on nuld.
- A design for steps 2-4 that fits the ee_core constraint, so step 1's choices do not block them.

**Non-Goals:**
- Writing our own rcheevos client or talking to the RetroAchievements web API from the PS2 (HTTPS, login, rich
  presence): the client does it.
- Hardcore-mode guarantees, leaderboards, rich presence on the TV.
- PS1 games (POPS) in this change.
- Steps 2-4 implementation details (later changes).

## Decisions

1. **Reuse xerabora's protocol and client** instead of a server of our own with rcheevos. The client already does
   login, evaluation, posting, and a web view of progress; the protocol is small and text-based. Alternative: a
   Python service on nuld with rcheevos bindings, more code to own for the same result. The client is run as a
   separate program (MIT), no code is copied into ORBIT, consistent with "reference projects are never
   dependencies".
2. **The launcher computes the hash** (rcheevos `rc_hash_ps2`): MD5 over the BOOT2 executable name as rcheevos
   extracts it, then the executable's bytes, read from the ISO through `iso.c`. Alternative: let the client hash the
   ISO over the network, impossible (the ISO is on the PS2's USB). The exact name string (with or without `;1` and
   the `cdrom0:\` prefix) follows rcheevos' `rc_hash_find_playstation_executable`; a host selftest compares our hash
   of a real ISO with rcheevos' own (built on the host from its sources).
3. **MD5:** wolfSSL is already linked into the launcher (cover download); use its MD5 if the ps2sdk build has it,
   otherwise a small RFC 1321 implementation in `achievements.c`.
4. **Cache** in `mass0:/orbit/ra/<serial>.txt`: hash, ISO size, exe size. A size mismatch recomputes. Reading a
   3-6 MB executable from USB takes seconds, so hashing runs in the existing low-priority loader thread (like covers
   and save icons) the first time a game is selected, never blocking the UI.
5. **Discovery:** broadcast `RAP1 <ps2-ip> 18194` and take the first `RAO1`; `[logros] servidor = <ip>` skips the
   broadcast. The answer is remembered for the session. Queries `RAQ1 <hash> <serial> <ps2-ip> 18194`; a `WAIT`
   answer is retried a few times, then shown as pending.
6. **UI:** a chip in the detail card's header row (next to DVD / USB / 480p): "LOGROS 24" when the game has a set
   (title in the saves-style card if room), nothing when it has none, "LOGROS ·" while hashing or asking, and a
   one-line status ("Logros: sin servidor") when the client never answers. Spanish text.
7. **Steps 2-4 (outline):**
   - Agent: the EE side is a tiny copy loop in the existing VBLANK handler (watched values into a buffer, then a SIF
     DMA to the IOP); the IOP side is a small module that owns the UDP traffic (`RAG1`, `RA15`, `RAU1`, `RAH1`) over
     Neutrino's smap + ministack, loaded in the game stage when achievements are on. The watch list arrives at the
     IOP and is DMA'd to an EE buffer. If even the copy loop does not fit in ee_core's free 1 KB, menu.c moves to a
     second region (ee_core96 at 0x96000 exists in Neutrino's linkfile) or is trimmed.
   - Unlock notice: a short pause-and-draw like the phase 13 menu (2-3 s), since drawing over a running game
     without pausing it has no proven method yet.
   - Pause menu: a "Logros" item listing the set fetched by the agent at game start.

## Risks / Trade-offs

- [Hash differs from RetroAchievements'] → host selftest against rcheevos on the user's Black.iso before the
  console; the cache stores the hash, so a fix only needs the cache cleared.
- [Executable read time on USB] → background thread + cache; the chip shows "·" until done.
- [UDP loss on WiFi (seen with UDPFS)] → queries are retried; snapshots in step 2 are idempotent per frame, a lost
  one is replaced by the next. No reliable transport needed.
- [xerabora client changes its protocol] → pin the client version installed on nuld; the protocol has version
  suffixes (`RAQ1`, `RA15`).
- [No space in ee_core for step 2] → decided in step 2's change; step 1 does not touch ee_core.
- [Client needs a first login through its web UI] → one-time setup on nuld, documented in the results file.

## Open Questions

- Which xerabora client build runs headless on nuld (Linux x86_64) and how it binds to the LAN, not localhost only.
- Whether `RAQ1` works without the console streaming afterwards (step 1 only queries).
- The exact `exe_name` string rcheevos hashes for PS2 (to confirm in the selftest).
