## Why

The approved design shows the selected game's 3D save icon in the saves card of the header, where today there is a
flat memory-card glyph. This is roadmap item 4. The icon makes the saves card read at a glance, the way the PS2
browser does.

## What Changes

- Read the newest save of the selected game:
  - its `icon.sys` (title, lights, list icon name);
  - its list icon file (geometry, animation shapes, texture).
- Parse the icon format, including RLE textures and animation keys. A host selftest covers the parser.
- Draw the icon in the saves card's 64×64 slot (design: 56 px icon over an ice glow):
  - textured, Gouraud-lit triangles with the save's own lights;
  - shape animation;
  - a slow spin.
- Icons load in the background on first selection and are cached per serial. Until then the card shows today's
  glyph. Games without saves keep the outline glyph.

## Capabilities

### New Capabilities
- `save-icons`: the icon.sys and icon file parser, background loading, and 3D drawing in the saves card.

### Modified Capabilities
- `launcher-ui`: the saves card shows the 3D icon when the game has a save.

## Impact

- New `icon.c` / `icon.h`: parser, animation, lighting and projection, with a host selftest.
- `gfx.c` gets one primitive: textured Gouraud triangles from a 128×128 CT16 texture streamed through the cover
  slot.
- `launcher.c` gets an icon thread (libmc reads after the boot scan) and the saves card drawing.
- No new libraries.
