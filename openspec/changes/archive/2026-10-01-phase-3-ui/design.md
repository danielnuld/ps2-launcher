## Context

Phase 2 (console): 15 covers stream 1:1 at max 3669 µs per frame, and the user finds them sharp with Floyd-Steinberg.
The UI choices (2026-10-01) are a horizontal carousel with a bigger centred selection, smooth animation, and the dark
blue style. VRAM pool: 65 536 words after the two CT16 framebuffers (phase 0). The phase-2 cover slot takes 49 152 of
them.

## Goals / Non-Goals

**Goals:** a launcher screen that looks finished. Readable anti-aliased text. Covers pixel-exact whenever nothing
moves. The phase-1 frame budget kept.

**Non-Goals:** launching games and scanning real ISOs (next phase; X shows a "next phase" note), sounds, settings,
reflections, an animated background, and non-ASCII text.

## Decisions

- **Font = Inter (OFL), baked on the PC.**
  - `tools/font.py` renders ASCII 32-126 with Pillow at two sizes: title 36 px weight 700, body 22 px weight 500.
  - Glyphs are shelf-packed into one 512-wide PSMT4 atlas. 4-bit alpha has 16 levels, which is enough for
    anti-aliased edges.
  - It writes `font_data.c` (atlas bytes + per-glyph u, v, w, h, x/y offset, advance; line height per size). The
    file is generated and committed, so the PS2 build needs no Python.
  - Measured with Pillow: about 137 k glyph pixels at these sizes, so a 512×256 PSMT4 atlas = 8 pages.
  - Rejected: KROM scaled ×2 (blocky or blurry), and SJIS/other glyphs (not needed yet).
- **Alpha blending:** `draw_setup_environment` already sets ALPHA = (Cs − Cd)·As + Cd. Text sprites set ABE in PRIM.
  Vertex alpha × texel alpha (MODULATE, TCC=1) gives fades. `gfx_alpha(a)` sets the vertex alpha (0..128) for the
  primitives that follow, and turns ABE on while a < 128.
- **Banded cover slot:** the slot shrinks to 256×192 CT16 (4×3 pages = 24 576 words), which frees 12 pages for the
  font. `gfx_image` uploads and draws each band of ≤ 192 lines in order (PATH3 order, as in phase 2). A 368-line cover
  is 2 bands.
  - Pool: slot 12 pages + font atlas 8 pages + CLUT, out of 32 pages.
  - At rest the bands meet exactly (point sampling, integer edges).
  - While scaling, bilinear filtering clamps at each band edge, so a faint line can show for the ~0.3 s of the
    animation. Accepted.
- **Two cover sizes made offline** (256×368, 184×264: both keep 512:736 within 0.2 %). This is the phase-2 quality
  rule: Lanczos on the PC instead of the GS for every size seen at rest.
  - During the grow/shrink animation the image whose size is closest is scaled bilinearly (TEX1 MMAG/MMIN = linear,
    only for that sprite).
  - RAM: (188 + 97) KiB × covers. 15 covers = 4.2 MB; ~100 covers fit next to the ELF in 32 MB.
- **Animation:** a float scroll position `s` eases towards the selection (`s += (sel − s) · 0.2` per frame) and snaps
  when |sel − s| < 0.002, which settles in ~0.4 s at 60 Hz (0.8^25 ≈ 0.004).
  - Cover i has grow factor f = max(0, 1 − |i − s|). Its size is lerp(small, large, f).
  - Its centre is 640 + (i − s)·D + sign(i − s)·36·min(1, |i − s|), with D = 184 + 28 gap. The 36 px make room for
    the big cover.
  - Every value is rounded to whole pixels. The frame is drawn with alpha = f·128 (ABE).
  - Fixed per-frame steps assume 60 Hz. The phase-1/2 gates show no dropped frames.
- **Info fade:** title alpha = 128 · max(0, 1 − 3·|s − sel|), so it fades out while moving and back in as it settles.
  The text always belongs to `sel`, never to the old game.
- **Splash:** the first readdir pass counts the `.c16` files without `_s`. The bar advances per loaded cover.
- **Saves (user request 2026-10-01):** `mcman.irx` and `mcserv.irx` (ps2sdk, embedded like the other IRX) load after
  `sio2man`, then `mcInit(MC_TYPE_MC)`. Then `mcGetInfo` twice per port (the first call after boot reports a new card)
  and one `mcGetDir("/*")` per card into a 128-entry table (ps2sdk `libmc.h`, `samples/rpc/memorycard/mc_example.c`).
  - A game matches by `strstr(name, serial)`, which tolerates the region prefix (BA/BE/BI, estimate) and the
    game-chosen suffix.
  - The date is the newest `_Modify` shown as is. `ponytail:` the card stores JST, so the day can be off near
    midnight; convert when a clock or settings screen exists.
  - The cards are read once at boot; a card swapped later is not seen.
- **Header / footer (user request 2026-10-01):** the header carries the selected game's title (36 px), its serial
  and its save info. The footer hints sit at the bottom right inside key boxes, with the list position at the bottom
  left. The carousel moves down to the vertical centre.
- **Debug overlay:** shown only with SELECT. Logging to `mass0:/demo.txt` stays always on (first 3 windows).

## Risks / Trade-offs

- [PSMT4 alpha banding on large glyphs] → judged on the TV. A PSMT8 atlas does not fit the pool as well (16 pages), so
  the slot would need 128-line bands.
- [Text sprites ×2 per frame (title + body + hints + header ≈ 120 glyphs)] → negligible next to the measured cover
  stream; it is in the gate.
- [Committing Inter.ttf (~0.9 MB)] → OFL allows it with the licence file. The build is reproducible offline.
