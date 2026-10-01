## Context

First phase of our own launcher. Only measurement; no engine yet. PCSX2 does not emulate caches (noted in the pokeemerald-ps2 work), so its timings are indicative only; the gate uses console numbers.

## Goals / Non-Goals

**Goals:** per-mode cost of fill, flat sprites, texture upload, textured sprites; a readable result on a TV.

**Non-Goals:** VU1 microcode (phase 1 compares against this PATH3 baseline), fonts, input, launching games.

## Decisions

- **PATH3 via ps2sdk `dma_channel_send_normal` + `draw_finish`/`draw_wait_finish`** as the baseline path: simplest path that already works in ../ps2-hdtest. VU1/PATH1 is the obvious next candidate and is measured against this number, not assumed faster.
- **Timing = COP0.Count** (ps2tek:1117-1121) read with `mfc0 $9`. 32-bit counter at 294.912 MHz wraps every ~14.6 s; every measured span is far shorter, so unsigned subtraction is enough.
- **Median of 16** to drop outliers from interrupts (vsync handler).
- **Text with the BIOS font** (`fontx_load("rom0:KROM")`, ps2sdk `samples/font/font.c:192`): zero assets. Replaced the first 7-segment version. fontx draws one point per glyph pixel, so the result screen is drawn once per mode.
- **Framebuffer height rounded to whole GS pages** (CT32 64×32, CT16 64×64, gsKit `gsKit_texture_size`): `graph_vram_size` does not, and the texture allocated next overlapped the framebuffer's last page row.
- **Single file `bench.c`** plus a copy of hdtest's `set_mode` (with the DISPLAY fix). No shared library yet; YAGNI until phase 1 has two users.

## Risks / Trade-offs

- [PCSX2 timings differ from hardware] → every number is tagged; gate waits for console numbers.
- [draw_wait_finish includes GS completion only for GIF paths; texture upload finish semantics may differ] → B3 also waits on FINISH after the image transfer; noted in results if the numbers look implausible.
- [1080i field mode halves effective vertical resolution per field] → reported as-is; quality judged on the TV, not assumed.

## Open Questions

- Which modes the HDMI adapter accepts (hdtest pending).
