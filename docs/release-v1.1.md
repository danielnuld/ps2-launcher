Setup without a PC, updates without a PC, and CRT TVs.

**Website, screenshots, setup guide:** https://danielnuld.github.io/ps2-launcher/

## New

- **Setup wizard** (#15). It opens by itself on the first boot, and with START any time after. In one screen:
  language, game sources (checkboxes), memory card mode, video, network, and installing ORBIT on a memory card with
  FreeMcBoot 1.966 set to start it (autoboot, a button, or an item in FMCB's menu). FMCB's other settings stay as
  they were, and its original config is kept as `FREEMCB.CNF.bak`.
- **ORBIT updates itself** (#15). With the network cable in, ORBIT checks for a newer release at boot; START opens
  the update. Every file is checked against a manifest signed with the release key before anything is replaced,
  and the old files are kept as `.old`. Turn it off with `[actualizaciones] buscar = no`.
- **CRT TVs**: the menu was 720p only, which a CRT shows as a black screen. Press **L2+R2** together (it works
  blind) to switch the menu to 480i (576i on PAL consoles) and back; it is saved as `[video] pantalla` in
  `config.ini`. In 480i the games default to their native video mode too.
- **Run ORBIT from an MMCE SD card, no USB drive needed** (#8). Settings, covers, apps and Neutrino live on the SD
  card; a USB drive becomes an optional extra game source.
- **udpfs without ORBIT's catalog** (#10): with `[juegos] servidor` empty, any udpfs server (Server2PS2...) works,
  and its DVD/CD folders are listed like any other source.
- **SELECT shows every game source's status** (#4): how many games each one found, or why it failed.

## Fixed

- **The in-game menu ignored the controller on an 8BitDo adapter** (#11): it read only one of the two copies of the
  pad data that the driver writes in turn.

## Updating

Copy `launcher.elf` **and** `neutrino/` from the zip over the ones on your USB drive (the in-game menu fix is in
`neutrino/modules/ee_core.elf`). Your `orbit/` folder, covers and virtual memory cards stay as they are. From this
version on, ORBIT can update itself.

## Known issues

- "Native" video mode can give a black screen on some HDTV setups: there, keep the default 480p.
- Loading over udpfs needs a wired network on both ends; Neutrino's UDP transport does not recover from WiFi
  packet loss.

## Checksums (SHA-256)

```
ORBIT-v1.1.zip    30ED0EEBEDB4F469145646F2A7B28F430C867839E69E0E4AC73B49E06931B892
launcher.elf      49E0C0B2868DDE33B789FF88EAEB60A09A268210BFBCE8C670181AB66CC8B2A3
```

GPL-3.0. Ships a fork of [Neutrino](https://github.com/rickgaiser/neutrino) (AFL-3.0) with the in-game menu and the
achievements agent. Developed with [Claude Code](https://claude.com/claude-code) as the coding assistant.
