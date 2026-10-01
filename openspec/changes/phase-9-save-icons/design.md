## Context

The saves card (launcher.c `home`) draws `UI_MEMCARD_36` today. The design canvas (`project/Main.dc.html`) gives the
card a 64×64 slot: a 56×56 icon box at (4, 0), with an ice elliptical glow 56×12 at (4, 50) under it.
- File formats: `icon.sys` is ps2sdk's `mcIcon`; the icon file layout is in docs/sources.md (V, checked on a real
  icon).
- No game saves exist on the PC: the PCSX2 cards here are unformatted, apart from OPL's config. The real test data
  is apollo-ps2's `icon.sys` / `icon.ico` (GPL). It is used from the scratchpad for the host test and not committed.
- `gfx.c` streams CT16 images through one 256×128 VRAM slot; a 128×128 icon texture fits it in one band.

## Goals / Non-Goals

**Goals:**
- The selected game's icon, textured, lit with its own lights, animated and spinning, in the card.
- No effect on the 60 Hz frame and no wait on memory-card I/O.

**Non-Goals:**
- Copy/delete icons, and the icon.sys background colours (the card has its own).
- A save browser listing every save of a game: that is a later phase.
- Perspective-correct texturing: at 56 px, affine UVs are not visible.

## Decisions

- **EE transform, GS triangles.** Per frame, for each vertex:
  - blend the shapes;
  - rotate about Y;
  - light (ambient + Σ max(0, n·Lᵢ)·Cᵢ, as the mymc++ viewer, reference only);
  - project orthographically into the 56 px box.

  Triangles are still sorted far to near (cheap, and it helps blending) and sent as one PRIM triangle list: Gouraud on, texture on, FST UVs, the
  texture modulated by the vertex colour.
  - Why: hundreds of vertices per icon cost nothing on the EE next to a frame.
  - Alternative: VU1 microcode. It is not justified without a measured hotspot (project rule).
- **Own 64×64 target with a Z buffer, not painter's order.**
  - Why not painter's order: a host render of apollo-ps2's icon showed it failing. The icon is a thin slab; at a
    0.8 rad turn, a back triangle's centroid sorts in front of a front one.
  - Why not a screen Z buffer: one would need about 80 K words (40 pages for the header rows at 1280 px), and only
    about 7 400 words are free (estimate from gfx_init's allocations).
  - How: the mesh draws into its own 64×64 CT16 target with a 64×64 Z16 buffer (one page each) and FBA off. The
    target is then blended into the card as a texture, with TEXA mapping A = 0 to transparent.
  - If the pages are missing, `gfx_mesh` returns 0 and the card keeps its glyph.
- **Texture through the cover slot.** The icon is drawn after the covers. PATH3 keeps the upload behind earlier
  draws, as the cover bands already rely on.
- **Icon thread.**
  - When: it is created after the boot memory-card scan (so only it uses libmc from then on). It sleeps on a
    semaphore that the render thread signals when the selection lands on a game with saves whose icon is not
    cached yet.
  - What it reads: `icon.sys` and the list icon of the newest save, with `mcOpen` / `mcRead` / `mcSeek`.
  - How it publishes: it parses into a per-serial cache and sets the pointer last.
- **Animation clock.** Time advances one animation frame per video frame, times `anim_speed`, modulo
  `frame_length`. This is an estimate (sources: E), tuned on the console against the PS2 browser.
- **Spin.** About 1/8 turn per second (design choice, D). Tune with the user.

## Risks / Trade-offs

- [Icons that rely on back-face culling or a Z buffer may show inner faces] → seen on the console; a Z buffer is the
  fix if it matters.
- [Big icons: several thousand vertices] → per-frame cost measured with the COP0 frame time on the console (gate).
  If it shows, cache the transformed vertices while the animation has a single shape.
- [Memory-card reads compete with the cover download on the IOP] → both are background threads; the render
  thread never waits.

## Open Questions

- Playback speed and spin: compare with the PS2 browser on the console.
