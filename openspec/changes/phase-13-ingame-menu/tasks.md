## 1. Spike: pause, draw, restore (go / no-go)

- [x] 1.1 DISPFB capture: record DISPFB1/2 in GSM's privileged-write breakpoint (`patch_neutrino.py` on `gsm_api.c`), and a capture-only mode armed without `-gsm`
- [x] 1.2 Pause / resume in `igr.c`: menu combo in the VBLANK handler; suspend the game threads (remember which), save and hold the INTC and DMAC masks (keep SBUS and whatever padman's SIF DMA needs); wait for GIF / VIF1 idle; resume in reverse order
- [x] 1.3 VRAM save and upload with image transfers only (local→host and host→local), cache written back before every DMA; one-line box with a bitmap font, in the frame's PSM
- [x] 1.4 PCSX2 bench (5 GB Black image): pause at the title and in the intro movies, box shows, picture restored, game resumes. PCSX2 has no EE data breakpoints, so DISPFB is never captured there: the spike falls back to Black's frame (FBP 0, 640, CT16S) and writes "UPS" instead of "PAUSA"
- [x] 1.5 Console spike: Black 480p shows "PAUSA" (DISPFB captured), pause, restore and resume work: GO (`docs/phase13-results.md`). Native moves to the gate (4.1)

## 2. Menu (only after a go)

- [x] 2.1 Saved-pixel storage (user's choice: big menu, saved when it fits): 144×56 box, 16 KB `saved` for 16-bit frames; 24/32-bit frames are redrawn by the game. Room made in the 64 KB ee_core region: the IGR thread stack moved to ee_core's main stack (0x94000), no u64 division (libgcc), 2-line upload strips
- [x] 2.2 Three items (Reiniciar / Apagar / Cancelar, Cancelar selected), D-pad, X, ○/START = Cancelar; solid panel in ORBIT colours (no read of the game's pixels needed to draw), 5x7 font at 2x, centred on the frame (DISPLAY and SMODE2 now captured too)
- [x] 2.3 Reiniciar and Apagar call the phase 12 paths (reboot from the menu thread: DMA stop, GS reset, ResetEE 0x7E; power off). PCSX2: Reiniciar reaches the BIOS menu

## 3. Launcher and config

- [x] 3.1 `-igrmenu=<mask>` loader option and eecore field (`patch_neutrino.py`); fork detection by its text
- [x] 3.2 `config.ini [igr] menu` (default L1+L2+R1+R2+START+SELECT); `reiniciar` default becomes empty; launch argument; update the phase 12 `igr` spec defaults

## 4. Gate

- [ ] 4.1 Console, Black, 480p and native: menu in gameplay and intro movies, Cancelar resumes with the picture intact, Reiniciar and Apagar work; `docs/phase13-results.md`
