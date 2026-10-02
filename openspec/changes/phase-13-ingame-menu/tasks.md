## 1. Spike: pause, draw, restore (go / no-go)

- [x] 1.1 DISPFB capture: record DISPFB1/2 in GSM's privileged-write breakpoint (`patch_neutrino.py` on `gsm_api.c`), and a capture-only mode armed without `-gsm`
- [x] 1.2 Pause / resume in `igr.c`: menu combo in the VBLANK handler; suspend the game threads (remember which), save and hold the INTC and DMAC masks (keep SBUS and whatever padman's SIF DMA needs); wait for GIF / VIF1 idle; resume in reverse order
- [x] 1.3 VRAM save and upload with image transfers only (local→host and host→local), cache written back before every DMA; one-line box with a bitmap font, in the frame's PSM
- [x] 1.4 PCSX2 bench (5 GB Black image): pause at the title and in the intro movies, box shows, picture restored, game resumes. PCSX2 has no EE data breakpoints, so DISPFB is never captured there: the spike falls back to Black's frame (FBP 0, 640, CT16S) and writes "UPS" instead of "PAUSA"
- [x] 1.5 Console spike: Black 480p shows "PAUSA" (DISPFB captured), pause, restore and resume work: GO (`docs/phase13-results.md`). Native moves to the gate (4.1)

## 2. Menu (only after a go)

- [ ] 2.1 Saved-pixel storage for the full box, as chosen in 1.5
- [ ] 2.2 Three items (Reiniciar / Apagar / Cancelar, Cancelar selected), D-pad, X, ○/START = Cancelar; translucent panel in ORBIT colours
- [ ] 2.3 Reiniciar and Apagar call the phase 12 paths (reboot thread, power off)

## 3. Launcher and config

- [ ] 3.1 `-igrmenu=<mask>` loader option and eecore field (`patch_neutrino.py`); fork detection by its text
- [ ] 3.2 `config.ini [igr] menu` (default L1+L2+R1+R2+START+SELECT); `reiniciar` default becomes empty; launch argument; update the phase 12 `igr` spec defaults

## 4. Gate

- [ ] 4.1 Console, Black, 480p and native: menu in gameplay and intro movies, Cancelar resumes with the picture intact, Reiniciar and Apagar work; `docs/phase13-results.md`
