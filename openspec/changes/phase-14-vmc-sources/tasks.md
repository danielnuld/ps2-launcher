## 1. Virtual memory cards

- [x] 1.1 `vmc.c`: create (genvmc layout), list root, read `DIR/file`; selftest in `make test`; cross-check with mymcplus
- [x] 1.2 `[memorycard] modo`, per-game `mc` in the △ panel, `-mc0=` at launch, card created on first launch
- [x] 1.3 Saves card + 3D icon from the virtual card (icon thread lists it once per entry)

## 2. Sources

- [x] 2.1 `sources.c`: drivers per source, mounts, HD Loader scan, MMCE game ID, Neutrino toml IP
- [x] 2.2 Discovery of DVD/CD on every source, source chip, Neutrino arguments per source, argument-length check
- [x] 2.3 Builds with ps2dev v2.0.0 (launcher, bench, modetest), no warnings; host selftests pass

## 3. Gate (console)

- [ ] 3.1 Black from the USB with the default: `VMC/SLUS-21376.bin` created, the game saves, the saves card shows it
- [ ] 3.2 `fisica` on one game: saves go to the card in slot 1
- [ ] 3.3 Each source the user has: games listed with the right chip and started (`hdd` exFAT and/or HD Loader,
      `mx4sio`, `mmce`, `udpbd`/`udpfs` with `pc/udpfs_server.py`, `ilink`)
