## 1. Launcher

- [ ] 1.1 `net.c`: plain `http_get` (shares `http_parse`); host selftest unchanged
- [ ] 1.2 `achievements.c`: `/state` game id (checked against the `RAA1` title), `/game?id=` field reader (id, title, points, earned); host selftest on a saved JSON from nuld
- [ ] 1.3 Panel renderer: header + rows in 2-bit coverage with ORBIT's font, earned first; host selftest writes a PNG to look at
- [ ] 1.4 Panel + row ids + earned flags into the `.wl` side file passed with `-ra=`

## 2. Neutrino fork

- [ ] 2.1 Loader: place the panel in the watch block; log its size and `ModStorageEnd`
- [ ] 2.2 menu.c: "Logros" item when a panel exists; panel blit in the frame's format, D-pad scroll, ○ back; earned marks from the flags + phase-16c session list
- [ ] 2.3 `ee_core.map` size check; console boot check

## 3. Gate (console)

- [ ] 3.1 Black: menu shows Logros; 56 rows scroll, readable at 480p and native; an achievement unlocked this session shows earned; Cancelar resumes; `docs/phase16d-results.md` with the panel size and the module storage left
