## Why

The user wants RetroAchievements on the real console, which is why the in-game menu (phase 13) was built: a way to
draw over a running game. RetroAchievements already has sets for many PS2 games, and xerabora (MIT client, AFL OPL
fork) proved the approach on a real PS2: the console streams the watched memory over UDP and a PC client evaluates
it with rcheevos. Reusing its wire protocol and client means ORBIT only builds the console side.

## What Changes

- **Client on nuld:** xerabora's client runs on the user's home server, always on, logged in to the user's
  RetroAchievements account. It answers the console on UDP 18194 (xerabora `protocol/PROTOCOL.md`) and posts the
  unlocks. Setup only, no code of ours.
- **Step 1, the launcher (detailed in this change, implemented first):**
  - computes the RetroAchievements PS2 hash of each game (rcheevos `rc_hash_ps2`: MD5 of the BOOT2 executable name
    followed by the executable's contents, read from the ISO) and caches it on the USB, so it is computed once;
  - finds the client (`RAP1` / `RAO1`) and asks it about the selected game (`RAQ1` / `RAA1`);
  - shows the result on the game's detail card: a "Logros" chip with the achievement count, or no chip when the
    game has no set, or a "sin servidor" note when the client does not answer;
  - `[logros]` in `config.ini`: on / off (default on) and an optional fixed client address.
- **Steps 2-4 (designed here, specified and planned in later changes):**
  2. an in-game agent in the Neutrino fork that fetches the watch list (`RAG1` / `RAC1`) and streams the watched
     addresses every frame (`RA15`), over the IOP's smap + ministack that Neutrino already ships;
  3. an unlock notice over the running game (`RAU1`), reusing the phase 13 drawing;
  4. a "Logros" entry in the pause menu with the unlocked and pending achievements.

## Capabilities

### New Capabilities
- `achievements`: identifying a game for RetroAchievements (hash, cache, client discovery and query) and showing
  its set in the launcher (step 1).

### Modified Capabilities
- `config`: the config file gains a `[logros]` section (on / off, client address).

## Impact

- `launcher.c` (detail card chip, query on selection), new small `achievements.c` (hash, cache, protocol client),
  `iso.c` (locate the BOOT2 executable and read it), `net.c` (UDP send / receive on lwIP, reused from the cover
  download's network bring-up).
- `mass0:/orbit/ra/<serial>.txt`: cached hashes.
- nuld: xerabora client installed as a user service, next to `udpfs`.
- Later steps touch `third_party/neutrino-igr` and `ee_core.elf`, whose 64 KB region has only ~1 KB free: the
  per-frame agent must live outside it (design.md).
- Licenses: xerabora client MIT, run as a separate program; rcheevos MIT; no code copied into ORBIT.
