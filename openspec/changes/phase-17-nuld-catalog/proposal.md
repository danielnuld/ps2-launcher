## Why

The user's games will live mainly on the home server nuld, served to Neutrino by udpfs. Today the launcher lists
those games by mounting udpfs itself, which loads Neutrino's network stack (smap + ministack) on the IOP: from then
on the adapter is Neutrino's, and the launcher's own stack (lwIP: TCP, HTTPS) cannot come up. Achievements (the
LOGROS chip, the ○ panel, the in-game agent's watch list), covers and anything else that needs TCP stop working
exactly for the games the user will play most.

## What Changes

- **A catalog service on nuld** (`orbit-catalog`, a systemd user service next to `udpfs` and `xerabora`): serves
  over HTTP a plain-text list of the ISOs under `/srv/data/ps2/{DVD,CD}`: path, size, serial, title and the
  RetroAchievements hash, computed there with the same algorithm as the launcher (rcheevos `rc_hash_ps2`) and
  cached by path, size and modification time. It also creates a game's virtual memory card file on request.
- **Launcher, `udpfs` source:** reads the catalog with its own network (lwIP) instead of mounting udpfs, so the
  network stays the launcher's: covers, achievements and the ○ panel work for nuld games. The hash comes from the
  catalog: no executable is read over the network.
- **Launch of a nuld game:** Neutrino starts without `-qb`, so it reboots the IOP into its own load environment
  with smap + ministack + udpfs from its `bsd-udpfs` config (as HD Loader games already do without `-qb`). The
  launcher's network is shut down first (`net_down`, phase 16).
- **Achievements agent with udpfs:** the launcher's `ra.toml` loads only `raagent.irx` when the game's source already
  brings smap + ministack.
- **Prerequisite (gate 0):** nuld on an Ethernet cable and a game loading over udpfs from the console. On 2026-10-03
  it failed over WiFi (Neutrino's UDPRDMA does not recover from a lost packet); nothing else here helps until it
  loads.
- `udpbd` (a block device) is left as it is: the user's server shares files (udpfs).

## Capabilities

### New Capabilities
- `game-catalog`: the nuld service (catalog, hashes, VMC creation) and the launcher reading it.

### Modified Capabilities
- `game-sources`: the `udpfs` source is discovered through the catalog and launched without `-qb`.
- `achievements`: hashes of nuld games come from the catalog; the in-game agent works with udpfs games.

## Impact

- New `tools/orbit_catalog.py` (the service; stdlib only) and its user service on nuld.
- `sources.c` (no udpfs mount), `launcher.c` (catalog entries, launch without `-qb`, `ra.toml` variant),
  `achievements.c` (hash from the catalog), `net.c` (plain HTTP already there).
- Saves card and 3D icons for nuld games: their VMC lives on nuld and is not read by the launcher in this change
  (the card shows that it is on nuld).
- Depends on phase 16b (agent) for the in-game part.
