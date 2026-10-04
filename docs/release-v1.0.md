ORBIT's first public release: a game launcher for a real PlayStation 2.

**Website, screenshots, setup guide:** https://danielnuld.github.io/ps2-launcher/

![ORBIT](https://danielnuld.github.io/ps2-launcher/img/home-carousel.jpg)

## What's in it

- **Three views** (carousel, grid, list) at 60 fps, filters All / PS2 / PS1 / Apps, animated splash.
- **Covers downloaded by the PS2** over HTTPS (xlenore/ps2-covers), decoded and saved on the console.
- **3D save icons** of each game's newest save, read from the memory card.
- **Virtual memory cards** per game or shared, created on first play; or the physical card, per game.
- **Per-game options** (△): 480p / 1080i, compatibility modes, memory card.
- **In-game menu** (L1+L2+R1+R2+START+SELECT): restart into ORBIT or power off; direct combos too.
- **RetroAchievements on real hardware**: rcheevos-compatible game hash, ○ lists the achievements with your
  progress, and an agent inside the game feeds the [xeRAbora](https://github.com/hacan359/xerabora) client.
- **Home game server**: udpfs + ORBIT's catalog service; server games keep covers, achievements and save icons.
- **Sources**: USB, internal HDD (exFAT, HD Loader), MX4SIO, iLink, MMCE, UDPBD, udpfs; discs, PS1 (POPStarter),
  homebrew from `APPS/`.
- **Spanish or English** (`[ui] idioma`), including the in-game menu and config.ini's comments.

## Quick start

1. Unzip `ORBIT-v1.0.zip` to the root of a FAT32 / exFAT USB drive; put ISOs in `DVD/` (CD games in `CD/`).
2. Start `mass0:/launcher.elf` from uLaunchELF or FreeMcBoot.
3. Pick a game, press ✕. Settings: `mass0:/orbit/config.ini` (written on first boot, commented).

`QUICKSTART.txt` in the zip covers covers, the way back to ORBIT, achievements and the game server
(`server/README.txt`).

## Known issues

- "Native" video mode can give a black screen on some setups: keep the default 480p.
- Loading over udpfs needs a wired network on both ends; Neutrino's UDP transport does not recover from WiFi
  packet loss.
- Achievement unlocks depend on the xeRAbora client being reachable on the same network.

## Checksums (SHA-256)

```
ORBIT-v1.0.zip  D0345C5AF801B60653565BC981DA2E9EA237FCB106D85F6E98778421FA40B30C
launcher.elf    CC7966E42D534F968999C4BDCC162484DB187733A899CB44AF763CFA15F1D95B
```

GPL-3.0. Ships a fork of [Neutrino](https://github.com/rickgaiser/neutrino) (AFL-3.0) with the in-game menu and the
achievements agent. Developed with [Claude Code](https://claude.com/claude-code) as the coding assistant.
