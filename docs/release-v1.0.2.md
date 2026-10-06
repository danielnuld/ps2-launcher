A small one: covers show up right away again.

**Website, screenshots, setup guide:** https://danielnuld.github.io/ps2-launcher/

## Fixed

- **Covers took a long time to appear** with the network cable in. In v1.0.1 the covers on screen waited for the
  launcher's network work at boot (downloading missing covers, asking about achievements), so they could show up
  only when the achievements chip did. The thread that loads them now goes first.

Everything from [v1.0.1](https://github.com/danielnuld/ps2-launcher/releases/tag/v1.0.1) is in here too: USB games
booting on a real PS2, up to 1024 games, DNS with DHCP, MemCard PRO 2 game IDs.

## Updating

Copy `launcher.elf` from the zip over the one on your USB drive (from v1.0, copy `neutrino/` too). Your `orbit/`
folder, covers and virtual memory cards stay as they are.

## Known issues

- The in-game menu doesn't react to a DualSense on an 8BitDo adapter; a wired controller works (#11).
- "Native" video mode can give a black screen on some setups: keep the default 480p.
- Loading over udpfs needs a wired network on both ends; Neutrino's UDP transport does not recover from WiFi
  packet loss.

## Checksums (SHA-256)

```
ORBIT-v1.0.2.zip  A5CF88E2F27D3745B01C93D6D691B162972749B35452A38461046A02B3F178A5
launcher.elf      C7B900CDD9B0C6006872AC448FDE58B09F1E69950D357969CF438AD31B86F191
```

GPL-3.0. Ships a fork of [Neutrino](https://github.com/rickgaiser/neutrino) (AFL-3.0) with the in-game menu and the
achievements agent. Developed with [Claude Code](https://claude.com/claude-code) as the coding assistant.
