## Context

The phase-5 launcher shows covers from `mass0:/covers`. Neutrino v1.8.0 is a command-line backend: `-dvd=<bsd>:<path>`
auto-selects the backing store driver from the prefix, and `-qb` quick-boots (README v1.8.0). nhddl
(reference only) launches it with an embedded ELF loader, argv[0] = the Neutrino path, then `-bsd`, `-dvd` and
`-qb`.

## Decisions

- **Identification:** the SYSTEM.CNF `BOOT2` serial, as nhddl and OPL do; for images without one, the OPL
  file-name prefix. Title from the file name (OPL prefix stripped).
  - The reader is a callback: on the console `fileXioLseek64`, because the EE `long` is 32 bits and SYSTEM.CNF
    can sit at 3.6 GB; on the host, stdio for the selftest.
- **Launch:** ps2sdk `elf-loader` (`LoadELFFromFile`) instead of our own loader stub. It keeps our IOP modules
  (USB mounted), so Neutrino reads its `config/` and `modules/` next to argv[0].
  - Before it, `gfx_shutdown` disables and removes our VBLANK handler, which would otherwise point into
    overwritten memory.
  - No other teardown: Neutrino resets the IOP.
- **Generic cover:** drawn at run time (rounded gradient, media icon, serial, title word-wrapped in Sora), so a
  game without a cover file still looks designed and costs no VRAM or file.
- **Covers:** fetched on the PC by serial (`fetch_covers.py`), with the same Lanczos + Floyd–Steinberg path as
  `covers.py`.

## Risks / Trade-offs

- [Fragmented ISOs: Neutrino's block devices allow at most 64 fragments] → documented by Neutrino; defragment the
  stick if a game fails.
- [USB only] → other sources later; the source chip is still "USB".
- [Saves are matched by the dash serial] → unchanged from phase 3; correct for ISO-derived serials.
