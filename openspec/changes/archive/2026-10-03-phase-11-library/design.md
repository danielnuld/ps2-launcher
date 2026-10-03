## Context

The game list (`cv[]`) holds PS2 ISOs only. `sel` and every per-entry array (`shown_box`, `gicon`) index into it.
The views lay out entries by their index.

Facts from the sources ledger:
- POPStarter derives its VCD from `argv[0]` (`XX.<name>.ELF` → `<name>.VCD`; POPStarter wiki "usb-mode", V).
- The BIOS starts a PS2 disc with `LoadExecPS2("rom0:PS2LOGO", 1, {BOOT2 path})`, and a PS1 disc with
  `LoadExecPS2("rom0:PS1DRV", 2, {file name, version})`. Source: OSD-Initialization-Libraries `ps2.c` / `ps1.c`,
  GPL, read as reference only, V.

## Goals / Non-Goals

**Goals:**
- One list of everything runnable, filters, and a launch per kind.
- The views and their animation unchanged in kind.

**Non-Goals:**
- DVD video and audio CD.
- PS1 saves (POPS VMCs).
- App covers or icons: apps get the drawn card.
- Other game sources (HDD, MX4SIO, …).

## Decisions

- **Filters as positions, not a new list.**
  - `cv[]` keeps every entry; a filter computes `order[]` (the shown entries in title order) and `pos[i]`, which is
    −1 when hidden.
  - View targets use `pos[i]` and `pos[sel]` where they used `i` and `sel`. Hidden entries target alpha 0, so a
    filter change is the same flying reflow as a view switch.
  - `sel` stays an entry index, so the header, the options, the launch and the icon thread are untouched.
  - Alternative: rebuilding `cv` per filter. It would break every index-keyed array and the icon thread.
- **Disc entry at index 0, always present.**
  - Inserting an entry while the render and icon threads hold indices would mean shifting every array; a fixed
    slot avoids that.
  - A disc thread polls `sceCdGetDiskType` once a second. On a change it reads SYSTEM.CNF with stdio
    (`cdrom0:\SYSTEM.CNF;1`) into a staging struct.
  - The render thread copies that struct into `cv[0]` at the start of a frame, so it alone writes `cv[0]` after
    boot.
- **Loader argv.** `loader.elf` loads `argv[0]` and runs it with `argv + 1`. Callers pass
  `{file, argv0, args...}`, so POPStarter can get a virtual `argv[0]`.
- **POPStarter path.** It is `mass0:/POPS/POPSTARTER.ELF`. The virtual `argv[0]` uses `mass:` because POPStarter
  maps the device from that prefix (POPStarter wiki examples use `mass:`). This is unverified on the console; the
  gate checks it.
- **VCD reading.** The data of logical sector n sits at 0x100000 + n × 2352 + 24 (Mode 2), or + 16 when the
  sector's mode byte is 1. The layout is an estimate from cue2pops documentation, to be checked on a real VCD.
- **Disc boots go straight to the BIOS** (`SifExitRpc`, then `LoadExecPS2`), without the loader. The BIOS loaders
  reset the IOP themselves.
- **Kind on the entry:** `kind` is PS2, PS1, APP or DISC, with `cd` kept for PS2 media. The PS1 cover download uses
  `xlenore/psx-covers` with the same `cover_from_jpeg`.

## Risks / Trade-offs

- [POPStarter may need the IOP reset or a specific `argv[0]` device] → console gate. The fallback is a renamed
  `XX.<name>.ELF` per game, which POPStarter's docs describe.
- [libcdvd polling while Neutrino or apps are launched] → the disc thread is stopped with the rest by `ExecPS2`.
  Polling is one RPC a second.
- [PCSX2 cannot test the disc drive together with `-elf`] → only the console gate checks disc launch.
