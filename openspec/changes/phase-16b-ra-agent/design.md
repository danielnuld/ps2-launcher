## Context

- Phase 16 step 1: the launcher hashes the game, finds xeRAbora's client on nuld (UDP 18194) and gets
  `RAA1 OK <bytes> <chunks> <total> ...` (Black: 220 bytes, 1 chunk, 56 achievements, 51 addresses; client log
  2026-10-03). The launcher's lwIP is shut down before a launch (`net_down`, phase 16).
- xeRAbora proved the data path on a real PS2 inside OPL (`opl` submodule @f4d559a, AFL-3.0): the launcher loads the
  watch list before the game and places it behind the IOP modules in EE memory (`src/rawatch.c` PlaceWatchBlock);
  ee_core's VBLANK handler reads the addresses through `UNCACHED_SEG` and sends the snapshot with `isceSifSetDma`,
  skipping a frame while the previous DMA runs (`ee_core/src/ra.c`); an IOP module (`raudp`) turns snapshots into
  UDP packets and polls for `RAU1` (`modules/network/raudp/raudp.c`). It waits 600 frames before the first read
  (`RA_START_DELAY`: "the game must load its own ELF first").
- Neutrino (fork base dev 7be8de2) already runs network modules inside games for udpfs: `smap.irx` and
  `ministack.irx ip=<ip>` with `env = ["LE", "EE"]` (`ee/loader/config/bsd-udpfs.toml`). ministack exports
  `udp_bind`, `udp_packet_init`, `udp_packet_send_ll` (`iop/ministack/src/exports.tab`) and does ARP itself.
  `-cfg=<file>` loads an extra config toml (Neutrino README).
- Neutrino's loader keeps the EE-environment IOP modules in "module storage" from 0x95000 (`ModStorageStart` /
  `ModStorageEnd` in `struct ee_core_data`, `common/include/eecore_config.h`; ee_core linkfile `modules84`).
- ee_core lives in 0x84000-0x94000; the fork's build ends at 0x93C08 (about 1 KB free; session notes 2026-10-03,
  `ee_core.map`). Some ee_core builds never started on the console for layout reasons (phase 13 known issue).

## Goals / Non-Goals

**Goals:**
- Unlocks for a game played from the USB, through the client on nuld, with the game unaffected.
- Reuse what is proven: xeRAbora's data path and wire format, Neutrino's in-game smap + ministack.

**Non-Goals:**
- The notice on the TV (phase-16c) and the list in the pause menu (phase-16d).
- Pointer chains (`RANL` tail of the watch list) in the first version: direct addresses only; chains if ee_core
  space allows (decision 4). Sets that need chains lose those achievements until then.
- udpbd / udpfs / HD Loader sources (the adapter is Neutrino's or there is no ISO file to hash), PS1 games.

## Decisions

1. **The launcher fetches the watch list, not the game.** It already talks to the client; the agent then needs no
   request/response code, only sending. Alternative: the agent fetches `RAG1` in-game (more IOP code, and the game
   would start before the list exists). Same choice as xeRAbora.
2. **Watch list into the fork through `-ra=<file>`; modules through `-cfg=ra`.** The launcher writes
   `mass0:/neutrino/config/ra.toml` (smap, ministack `ip=<the PS2's IP>`, raagent) per launch: the IP is the one the
   launcher used (fixed or the DHCP lease), since ministack has no DHCP. The loader patch reads the `.wl` file,
   checks it (sizes 1/2/4, total = header bytes, count within `RA_WATCH_MAX`), and places it after
   `ModStorageEnd`, followed by the 64-byte-aligned snapshot buffer and a 64-byte mailbox; new `ee_core_data`
   fields point at them.
3. **Mailbox handshake instead of a heap allocation in ee_core.** The loader appends `mbox=<address>` to raagent's
   arguments. The agent DMAs its IOP snapshot-buffer address into the mailbox when it starts; ee_core reads the
   mailbox through `UNCACHED_SEG` each VBLANK and starts sending once it is set. Step 3 reuses the mailbox for
   `RAU1` events. xeRAbora calls `SifAllocIopHeap` from ee_core instead; that costs ee_core code we do not have
   room for.
4. **ee_core space.** The per-frame code is a loop over the list (three load widths, the out-of-RAM check) plus the
   DMA call, estimated at a few hundred bytes (estimate, to be measured from `ee_core.map`). First try: inside
   ee_core next to the IGR handler. If it does not fit, the code goes into the watch block as a small
   position-independent blob that ee_core calls through one pointer (the loader places it), so ee_core only grows by
   the call. Moving to the `ee_core96` layout is the last resort (it changes which games fit).
5. **Agent sends through ministack, not hand-built frames.** xeRAbora builds raw frames because SMSTCPIP's mailbox
   stalled its game; ministack has no such thread (it is what udpbd streams through). The agent polls the snapshot
   buffer every few milliseconds from its own thread and sends a new sequence number only once; a torn copy
   (trailer ≠ header) is dropped, as the protocol says.
6. **Start delay** of 600 frames before the first read, as xeRAbora (`RA_START_DELAY`), so the game's ELF is loaded.

## Risks / Trade-offs

- [ee_core no longer starts on the console after the change (layout-dependent, phase 13)] → console check of every
  ee_core build before anything else; the blob option (decision 4) keeps ee_core's own size almost unchanged.
- [IOP RAM: a game that needs all of it fails to load with the extra modules] → only games with a set get them;
  `[logros] activos = no` removes them; note the failing game.
- [SIF DMA from the VBLANK handler competes with the game's own SIF traffic] → one transfer per frame, skipped while
  busy (xeRAbora's measured choice); the snapshot header reports skips and read cycles to the client.
- [UDP loss on WiFi (nuld)] → snapshots are idempotent per frame; a lost one is replaced by the next.
- [DEV9 is shown to the game as absent (Neutrino's `i_dev9_hidden`)] → online-capable games lose their network
  options while achievements are on.

## Open Questions

- Whether the client accepts snapshots from a console whose `RAQ1` came from the launcher (step 1) rather than from
  the agent itself, or needs a fresh `RAQ1` from the agent at game start.
- Measured per-frame read cost for Black's 51 addresses (from the snapshot header's `read_cycles`).
