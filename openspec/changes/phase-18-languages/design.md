## Context

- Every launcher text is a Spanish literal in `launcher.c` (~200); jfplay has ~80; the in-game menu draws three
  labels from 9 hand-made 5×7 glyphs (`menu.c`), and ee_core ends at 0x93FC8 of 0x94000 (phase 16b).
- `menu.c` keeps the area under the menu in `saved[BOX_W * BOX_H * 2]`, 16 128 bytes inside ee_core.
- Phase 16b places a watch block in module storage after the modules; the loader grows `irxptr_end` and the
  kernel's user-memory clear is patched to keep it.

## Goals / Non-Goals

**Goals:** Spanish and English, chosen in config.ini, everywhere the user reads text on the console.

**Non-Goals:** more languages (the macro allows them later, with a table); translating docs or code comments;
a settings screen to switch language.

## Decisions

1. **`L(es, en)` at each use** (a macro picking one of two literals by a global set from the config). The two
   texts sit together, nothing to keep in sync elsewhere, no table or IDs. Alternative: string tables per language,
   worth it only with a third language.
2. **Keys stay Spanish.** Translating keys would break every existing config and the docs; comments carry the
   meaning.
3. **Rewrite on language change.** The first line of the template names the language (`; ORBIT - configuración` /
   `; ORBIT - configuration`); when it does not match `idioma`, the template of `idioma` is written with each
   `key =` line filled from the loaded values, and keys the template lacks appended to their section. One rewrite,
   then the marker matches.
4. **Menu labels from the loader.** `-igrlang=en` sets a byte in `ee_core_data`; `menu.c` gets the glyphs it lacks
   (B, F, O, S, T, W: for RESTART, POWER OFF) and an English label set. Room: the `saved` buffer moves to module
   storage after the watch block (the loader reserves it and passes the pointer), freeing 16 KB of ee_core.
5. **jfplay** reads `[ui] idioma` from the same file with the same macro (copied code, as the shared files are).

## Risks / Trade-offs

- [ee_core layout change (saved buffer out) stops it booting on the console, as some layouts did in phase 13]
  → console boot check first; the buffer move is its own step, tested before the English labels.
- [A text missed stays in Spanish] → grep for Spanish literals after the change; PCSX2 screenshots of every screen
  in English.
- [Text width: English or Spanish longer in a fixed layout] → every fitted text already goes through `fit()`;
  hints and chips checked on screenshots.
