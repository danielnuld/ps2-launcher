## Why

The unlock pulse (phase-16c) says that something unlocked, not what, and nothing on the console says what is left
to get. The pause menu (phase 13) is the one place where ORBIT can draw over a game, so the set belongs there.

## What Changes

- **Launcher, before launching a game with a set:** reads the set from the client's page (plain HTTP JSON,
  `GET /game?id=<RA game id>` on port 18280: id, title, description, points, earned date per achievement) and
  renders a "Logros" panel with ORBIT's own font: one row per achievement (title and points), earned ones first,
  as a 2-bit image (text coverage only, colours applied in the game). Saved next to the watch list and passed to
  Neutrino with the watch block (`-ra=`).
- **Neutrino fork, in-game menu:** a new item "Logros" (only when the panel exists) between Apagar and Cancelar.
  It shows the panel over the paused game, a window of rows that the D-pad scrolls, each row marked as earned when
  it was earned before the game started or unlocked this session (phase-16c's session list). ○ goes back.
- **Font and text stay in the launcher:** the fork gets pixels, not strings, so ee_core needs no font and the
  Spanish / Latin-1 titles render as in the launcher.

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `achievements`: the set's list fetched and rendered by the launcher.
- `ingame-menu`: a "Logros" item and its panel.

## Impact

- `launcher.c` / `achievements.c` / `net.c`: plain HTTP GET (the cover download only has HTTPS), a small JSON reader
  for the fields used, the panel renderer on `gfx.c`'s font code.
- `third_party/neutrino-igr/menu.c`: item, panel blit from the watch block, scroll; the loader places the panel.
- Requires the client's page open to the LAN (it is on nuld since 2026-10-03, set from the page's SETTINGS).
- Depends on phase-16b (watch block, `-ra=`) and phase-16c (session list).
