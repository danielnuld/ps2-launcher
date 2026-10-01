## Context

While a game runs, the launcher is gone: Neutrino's `ee_core` (from OPL's, AFL-3.0) stays resident at 0x84000. It
has a 64 KB budget and the stock core uses 25 KB. OPL's IGR (`ee_core/src/padhook.c`, AFL-3.0) works like this:
- it finds the game's libpad open function by byte patterns;
- it redirects its callers to a hook that records the pad buffer;
- a VBLANK handler compares two raw pad bytes against fixed combos;
- a thread then resets the IOP and runs an exit ELF.

Those raw bytes are libpad's active-low button word, the same bits as ps2sdk's `PAD_*` (0xF0 / 0xF6 =
L1+L2+R1+R2 / START+SELECT).

## Goals / Non-Goals

**Goals:**
- Return to the launcher and power off from a game, with combos the user sets.
- The same stub file serves as the autoboot target.

**Non-Goals:**
- The drawn in-game menu: phase 13.
- IGS screenshots, cheats UI.
- IGR for apps or PS1 games: POPStarter has its own IGR.

## Decisions

- **Fork as patches.** `third_party/neutrino-igr` holds the new files and `patch_neutrino.py`, which makes exact,
  asserted edits on a v1.8.0 checkout. Upstream updates stay a re-run of the script, not a merge.
  - Only `ee_core.elf` and `neutrino.elf` are rebuilt; the IOP modules stay the official release files. v1.8.0's
    `cdvdfsv` fails the current ps2sdk's IRX check (`_retonly` at text 0), which is unrelated to this fork.
  - `ps2-packer` cannot find its stubs, so the unpacked loader ships: 141 KB instead of 85 KB.
- **Combos as masks.** The core compares `~(two pad bytes)` against the mask, exact match only, so an extra button
  held in play never triggers it. The launcher parses names into masks (`combo.c`, host selftest).
- **Hook points.** The libpad call is patched right after the game ELF is loaded (`main.c`, before
  `CleanExecPS2`) and on the game's IOP reset (`New_SifSetDma`), as OPL does. OPL's CreateThread / ExecPS2
  re-hooks are left out (`ponytail:` some multi-ELF games may lose the IGR; add them if one does).
- **Return path.**
  1. Put back the hooked syscalls.
  2. Disable GSM and cheats.
  3. Reset the IOP to ROM, re-init the TLB, stop the performance counters (OPL's GT4 notes).
  4. Load ROM SIO2MAN + MCMAN, then `SifLoadElf(IgrExitPath)` from the memory card.

  The USB is not reachable there, hence the stub on the memory card.
- **Power off.** CDVD S-command 0x0F, the one cdvdman's `sceCdPowerOff` sends, written from the EE in kernel mode,
  as OPL already does with 0x1B to cancel a power-button press.
- **Fork detection.** The launcher passes `-igr*` only when `neutrino.elf` contains the text `-igrexit`, because
  the official loader exits on unknown options.
- **Stub install** runs in the icon thread, the only libmc user after the boot scan. It compares the file byte for
  byte and writes only on a difference (443 KB). The stub embeds the same IOP modules as the launcher.

## Risks / Trade-offs

- [Games whose libpad does not match OPL's 7 patterns get no IGR] → same coverage as OPL. The power button
  still works.
- [Game audio may keep sounding after the return: there is no SPU reset module] → the launcher's audsrv init
  restarts the SPU2 voices. Watch on the console.
- [PCSX2 cannot boot games through Neutrino here] → the IGR itself is checked only on the console (gate). PCSX2
  checked the stub install, the arguments, the fork accepting them, and the stub booting the launcher.
