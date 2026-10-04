## 1. Launcher

- [x] 1.1 `[ui] idioma` read first in load_config; `L(es, en)` over every on-screen text (chips, hints, panels △ ○, toasts, launch and VMC screens, saves card, filters, views, overlay, cover states)
- [x] 1.2 config template in both languages; rewrite on language change keeping every value (host selftest of the rewrite)
- [x] 1.3 PCSX2: screenshots of home, △, ○, launch, toasts in English; no Spanish literal left (grep)

## 2. In-game menu (Neutrino fork)

- [ ] 2.1 Move `menu.c`'s save buffer to module storage (loader reserves it, `ee_core_data` pointer); console boot + menu check in Spanish
- [ ] 2.2 `-igrlang=en`, English glyphs and labels; launcher passes it; PCSX2 + console check

## 3. Jellyfin

- [x] 3.1 orbit-jellyfin: `[ui] idioma`, `L(es, en)` over jfplay's texts; PCSX2 screenshots

## 4. Gate (console)

- [ ] 4.1 `idioma = en`: launcher, in-game menu and Jellyfin in English; back to `es`: config comments back in Spanish with the same values; `docs/phase18-results.md`
