## Context

- The phase 13 menu (`third_party/neutrino-igr/menu.c`) is a 144×56 panel with 9 hand-made 5×7 glyphs, because
  ee_core's 64 KB region is nearly full; the area under it is saved in ee_core only for 16-bit frames
  (`saved[BOX_W * BOX_H * 2]`), otherwise the game's next frames repaint it.
- The client's page answers `GET /game?id=19040` with JSON: `title`, `total`, `awarded`, and per achievement `id`,
  `title`, `description`, `points`, `earned` (date or empty), `type` (read from nuld 2026-10-03). The RA game id is in
  `GET /state` (`game.id`) once the client identified the game.
- The launcher has the full ORBIT font (Sora / Space Mono, Latin-1 extras, `tools/font.py`) and draws text on the EE.
- phase-16b places a watch block behind the IOP modules (module storage from 0x95000, 428 KB region in Neutrino's
  `modules84` layout, shared with the modules); phase-16c keeps the session's unlocked ids there.

## Goals / Non-Goals

**Goals:** the set's titles, points and earned state readable in the pause menu, Spanish text included.

**Non-Goals:** descriptions (one line per row; maybe a later detail view), badges (pictures), leaderboards, progress
("3 of 10").

## Decisions

1. **The launcher renders, the fork blits.** Text needs a font and layout code the fork has no room for; the
   launcher already has both. The panel is a 2-bit coverage image (4 levels of text antialiasing), 448 px wide,
   one 20-px row per achievement plus a header (estimate for 56 rows: 448 × 1 140 × 2 bits ≈ 128 KB; to be checked
   against the module storage left after the modules, and rows halved to 1 bit if it does not fit).
2. **Colours in the fork.** menu.c expands coverage to the shown frame's format (CT32 / CT24 / CT16 / CT16S, as it
   does for the current panel) with the ORBIT palette; earned rows in gold, others grey. The earned mark per row is
   a flag array beside the image, updated from the session list when the panel opens.
3. **Read the set over plain HTTP from the client's page**, not from RetroAchievements: the client already holds
   the user's login and Web API key; the PS2 has no CA store and the cover code skips certificate checks
   (`net.c`). `GET /state` gives the id after `RAQ1`. A small field reader, not a JSON library.
4. **No save-under for the big panel.** The game is paused, and resuming repaints (phase 13 behaviour for 24/32-bit
   frames). The panel draws the rows window plus a header and a hint line.

## Risks / Trade-offs

- [Module storage too small for the panel with the modules of `-cfg=ra`] → 1-bit rows, or fewer rows with the
  earned ones dropped; measured from the loader's `ModStorageEnd` log line.
- [The page is closed to the LAN or the client restarts] → no panel, the rest works (spec).
- [`/state`'s game id belongs to a game identified earlier] → checked against the title of `RAA1 OK`.
- [menu.c growth in ee_core] → the scroll and the expand loop are small; the panel and flags live in the watch
  block; same fallback as phase-16b decision 4.

## Open Questions

- Whether the 2-bit panel reads well on the TV at 480p and native, or needs 4-bit.
