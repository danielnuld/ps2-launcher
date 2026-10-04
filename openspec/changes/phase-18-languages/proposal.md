## Why

ORBIT is written in Spanish. The user wants a Spanish and an English version, ahead of publishing the launcher and
Jellyfin (the release planned after phase 16).

## What Changes

- **`config.ini [ui] idioma = es | en`** (default `es`): one `launcher.elf` with both languages.
- **Launcher:** every text on screen (chips, hints, panels, toasts, launch screen, overlays) in the chosen language.
- **config.ini comments** follow the language: the template exists in both; when `idioma` no longer matches the
  language the file was written in, the launcher writes it again in the new language, keeping every value. Keys stay
  in Spanish in both (`origen`, `servidor`, ...), so existing files keep working.
- **In-game menu** (Neutrino fork): REINICIAR / APAGAR / CANCELAR or RESTART / POWER OFF / CANCEL, the language
  passed by the loader (`-igrlang=en`). ee_core has 56 bytes free; the menu's 16 KB save buffer moves out of
  ee_core into module storage (as the achievements watch block), which also leaves room for phases 16c and 16d.
- **Jellyfin (jfplay, its own repo):** same switch, read from the same `config.ini`.

## Capabilities

### New Capabilities
- `languages`: the language setting and what follows it.

### Modified Capabilities
<!-- config: a new key and the template's comments; covered under languages -->

## Impact

- `launcher.c` (texts through `L(es, en)`), `ini.c` if needed for rewriting the file; `third_party/neutrino-igr`
  (`menu.c` glyphs and labels, loader option, save buffer in module storage); `orbit-jellyfin/jfplay.c`.
- Docs (README, results) stay as they are; the release notes of the public version decide their language later.
