## Why

The user approved the ORBIT Y2K-2026 design (canvas https://claude.ai/artifact/94TdvSa23Yh4veqXDpLv7E, 2026-10-01): an
identity, chrome type, icons for actions and game sources, and an animated splash that pays homage to the PS2 boot.
The phase-3 screen works and passes the frame gate, but it looks generic and its splash is a static text line.

## What Changes

- **Fonts:** Unbounded 700 (titles, chrome fill), Sora 500 (UI), Space Mono (serials and numbers), and Unbounded 800
  for the "ORBIT" wordmark only. They replace Inter. The atlases add Spanish accents (á é í ó ú ñ ü ¿ ¡ ·) and
  `gfx_text` decodes UTF-8.
- **UI art tool** `tools/ui_art.py`: draws the icons (actions, game sources, pad buttons), a disc (for rounded
  shapes), a soft glow and a sparkle into a PSMT4 alpha atlas, and the chrome orb into PSMT8 + CLUT. Output:
  `ui_data.c`.
- **Engine:**
  - Textured gradient quads (glyph chrome, tinted icons, glows).
  - Rounded rectangles and pills.
  - Anti-aliased lines.
  - Letter tracking.
  - A cover band slot of 256×128.
  - vsync wait on a semaphore (no busy loop), so a loader thread can run.
- **Launcher (`launcher.c`, renamed from `demo.c`):**
  - The design's home screen: perspective grid floor, horizon line, glow and sparkles.
  - Header: orb, chrome title, serial / DVD / source chips, saves card.
  - Chrome frame and glow on the selected cover.
  - Footer: position ticks and icon hints.
  - The animated splash while a loader thread brings up USB, reads the memory cards and loads covers, then a fade
    into the home screen.
- **Out of scope:** the 3D save icons (phase 5) and real game sources (the source chip shows USB).

## Capabilities

### New Capabilities
- `orbit-style`: the ORBIT look: fonts, UI art, home screen layout, animated splash.

### Modified Capabilities
- `render-engine`: Text requirement (four baked fonts, UTF-8 Latin-1 subset, chrome fill).
- `cover-art`: streaming slot bands go from 192 to 128 lines.
- `launcher-ui`: Splash requirement (animated splash, loading in a thread).

## Impact

- New: `tools/ui_art.py`, `ui_data.c`, `tools/fonts/{Unbounded,Sora,SpaceMono-Regular}.ttf` + OFL files.
- Removed: Inter.
- Changed: `gfx.c` / `gfx.h`, `tools/font.py`, `font_data.c`. `demo.c` is renamed to `launcher.c`. `Makefile`.
- Log file: `mass0:/launcher.txt`.
