## Why

Step 1 (phase 16) shows a game's achievement count, but nothing unlocks yet: the client on nuld needs the game's
memory every frame to evaluate the set. This change is the in-game agent that streams it, so achievements unlock on
the user's RetroAchievements profile while playing on the console.

## What Changes

- **Launcher, before launching a game with a set:** fetches the watch list from the client (`RAG1` / `RAC1`, chunks of
  896 bytes), keeps it in `mass0:/orbit/ra/<serial>.wl`, and starts Neutrino with two more arguments: `-cfg=ra` (an
  extra Neutrino config the launcher writes, `mass0:/neutrino/config/ra.toml`, that loads `smap.irx`,
  `ministack.irx` with the PS2's IP, and our agent into the game) and `-ra=<watch list file>`. Its own network is
  shut down first, as now.
- **Neutrino fork, loader (`-ra=`):** reads the watch list and places it, with a snapshot buffer and a small mailbox,
  in the module storage behind the IOP modules (memory the kernel keeps from the game), and tells ee_core where.
- **Neutrino fork, ee_core:** in the existing VBLANK handler, after a start delay, reads the watched addresses into
  the snapshot buffer and SIF-DMAs it to the agent; a frame whose previous DMA is still running is skipped.
- **New IOP module `raagent.irx`** (ours, `third_party/neutrino-igr/raagent`): on Neutrino's ministack, announces
  the console to the client (`RAP1`), sends each snapshot as `RA15` packets and a heartbeat (`RAH1`); writes its
  buffer address into the mailbox so ee_core can DMA to it.
- **Result:** the client's LIVE tab shows the console streaming, and unlocks reach the user's profile. The notice on
  the TV (step 3) and the list in the pause menu (step 4) are separate changes.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `achievements`: adds the in-game telemetry (watch list fetch, snapshots every frame, agent) to the capability
  started by phase 16.

## Impact

- `launcher.c` / `achievements.c`: watch list fetch (`RAG1` / `RAC1`), `ra.toml`, `-cfg=ra -ra=` in the launch line.
- `third_party/neutrino-igr`: `patch_neutrino.py` (loader option, eecore_config fields, VBLANK hook), new
  `ra.c` for ee_core, new `raagent/` IOP module; `tools/build_neutrino.sh` builds and installs `raagent.irx`.
- ee_core's 64 KB region has about 1 KB free (design.md): the per-frame code must fit there or move out.
- IOP RAM in the game: smap + ministack + agent, the same modules Neutrino already loads for udpfs.
- Reference: xeRAbora's OPL agent (`ee_core/src/ra.c`, `modules/network/raudp`, AFL-3.0) for the data path; its
  protocol header `ra_snap.h` / `ra_watch.h` (the contract with the client) copied as is.
