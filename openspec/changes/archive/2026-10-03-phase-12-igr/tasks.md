## 1. Neutrino fork

- [x] 1.1 `third_party/neutrino-igr/igr.c`: OPL padhook port with mask combos, ROM IOP reset + memory-card exit, S-command power off; `patch_neutrino.py` (eec fields, hooks, loader options)
- [x] 1.2 `tools/build_neutrino.sh`: v1.8.0 checkout, patch, build `ee_core.elf` + loader into `dist/neutrino/`

## 2. Launcher side

- [x] 2.1 `boot.c` stub (USB + mass0:/launcher.elf, on-screen retry), `exec.c` shared loader runner, `APP=boot`, embedded in the launcher
- [x] 2.2 `combo.c` + selftest; `config.ini [igr]`; stub install at mc?:/BOOT/ORBIT.ELF; fork detection; `-igr*` launch options

## 3. Checks

- [x] 3.1 PCSX2: stub written to the memory card, launch options in the log, the fork accepts them, the stub boots the launcher

## 4. Gate

- [x] 4.1 Console: Black reboots to FMCB with the combo (title and intro movies) and powers off with the other; `docs/phase12-results.md`. Changed on the console: the return is a reboot through rom0:OSDSYS (user's choice), with resetspu and the kernel unpatch
- [x] 4.2 Console: a custom combo; FMCB autoboot of mc?:/BOOT/ORBIT.ELF after the reboot
