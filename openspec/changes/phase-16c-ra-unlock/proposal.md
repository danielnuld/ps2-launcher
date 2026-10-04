## Why

With telemetry (phase-16b) achievements unlock on the profile, but the player only finds out on the PC or the phone.
The TV should show the moment of an unlock, without stopping the game.

## What Changes

- **raagent:** keeps listening on its port; on `RAU1 <id> <points>` it DMAs a small event record (sequence number,
  achievement id, points) into the mailbox that phase-16b set up.
- **ee_core (VBLANK handler):** a new event sequence number starts a gold pulse over the running game: about a second
  of the picture blended towards gold and back, written only through the GS display registers (PMODE and BGCOLOR).
  Two unlocks close together give two pulses. The game is not paused.
- **ee_core keeps the ids** of the achievements unlocked this session in the mailbox area, for the pause-menu list
  (phase-16d).
- **Changes the step 3 plan of phase 16's design**, which had a 2-3 s pause-and-draw notice with text: pausing in the
  middle of play is intrusive, and drawing without pausing has no proven method (phase 13 can only draw over a paused
  game). xeRAbora's pulse is proven on a real PS2 and costs a few register writes. The achievement's name shows in
  the pause menu instead (phase-16d).

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `achievements`: adds the unlock notice on the TV.

## Impact

- `third_party/neutrino-igr`: raagent (receive side), ee_core `ra.c` (event check + pulse), shared event layout.
- Depends on phase-16b (agent, mailbox). No launcher change.
- Reference: xeRAbora `ee_core/src/ra_overlay.c` (AFL-3.0) for the pulse.
