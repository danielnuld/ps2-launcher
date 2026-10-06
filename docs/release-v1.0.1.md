Fixes for what people found in v1.0. Thanks to everyone who opened an issue!

**Website, screenshots, setup guide:** https://danielnuld.github.io/ps2-launcher/

## Fixed

- **USB games didn't boot on a real PS2** (#12). The in-game menu now draws over the game without saving what's
  under it (the game's next frame redraws it), which frees the memory that broke the boot. The menu still works on
  every source, in Spanish or English.
- **128 game limit** (#3). Now 1024. Covers are loaded only while they're on screen, and each view reads just the
  size it draws; the grid keeps a small thumbnail per game on the USB (`covers/<serial>_h.c16`, made the first time).
- **Covers: "No DNS"** (#7). `[red] dns` now works with DHCP too, and if the router gives no DNS server ORBIT uses
  1.1.1.1. The first download retries once if the connection fails right after the network comes up.
- **MemCard PRO 2 / SD2PSX didn't switch cards** (#2). ORBIT now sends the game ID on every launch, whatever the
  game's source, as NHDDL does. To save on the card instead of the USB's virtual one, set the game's memory card to
  `fisica` (△, or `[memorycard] modo = fisica` for every game).
- **xeRAbora version** (#5). The docs point to the client release ORBIT is tested with:
  [v0.1.0-alpha.9](https://github.com/hacan359/xerabora/releases/tag/v0.1.0-alpha.9).

## Also

- `tools/covers.py` is in the zip: it turns PNG/JPG files named by serial (`SLUS-21376.png`) into covers for
  `mass0:/covers/` (`python3 covers.py IN_DIR OUT_DIR`, needs numpy and Pillow).
- `mass0:/launcher.txt` now says why a game source or the network did not come up, and why a cover download failed.

## Updating from v1.0

Copy `launcher.elf` and `neutrino/` from the zip over the ones on your USB drive. Your `orbit/` folder (settings,
per-game options), covers and virtual memory cards stay as they are.

## Known issues

- The in-game menu doesn't react to a DualSense on an 8BitDo adapter; a wired controller works (#11).
- "Native" video mode can give a black screen on some setups: keep the default 480p.
- Loading over udpfs needs a wired network on both ends; Neutrino's UDP transport does not recover from WiFi
  packet loss.

## Checksums (SHA-256)

```
ORBIT-v1.0.1.zip  174C662C52C94458986D41616E7CA559E79C805D79768BE6C6D146C9BB4D8D1B
launcher.elf      B673517EBAFD0F961599A7FCF3FB889393D47CB8EF0FC5BAE058F4F0560B85EF
```

GPL-3.0. Ships a fork of [Neutrino](https://github.com/rickgaiser/neutrino) (AFL-3.0) with the in-game menu and the
achievements agent. Developed with [Claude Code](https://claude.com/claude-code) as the coding assistant.
