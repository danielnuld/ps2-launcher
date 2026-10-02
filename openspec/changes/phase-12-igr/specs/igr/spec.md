## ADDED Requirements

### Requirement: Combos in the config
`config.ini [igr]` SHALL accept:
- `reiniciar` and `apagar`, each a list of buttons joined with `+`;
- Spanish or English names: L1 L2 R1 R2 L3 R3 START SELECT ARRIBA/UP ABAJO/DOWN IZQUIERDA/LEFT DERECHA/RIGHT
  TRIANGULO/TRIANGLE CIRCULO/CIRCLE X/CROSS CUADRADO/SQUARE, in any case.

An empty value or an unknown name SHALL disable that combo. The defaults SHALL be empty for the reboot (phase 13
gives L1+L2+R1+R2+START+SELECT to its menu, which has Reiniciar) and L1+L2+R1+R2+L3+R3 for power off.

#### Scenario: Parsing
- **WHEN** the selftest parses "L1+L2+R1+R2+START+SELECT"
- **THEN** it gets the libpad mask 0x0F09, and "L1+PATADA" gives 0

### Requirement: Boot stub
The launcher SHALL keep a copy of its boot stub at `mc?:/BOOT/ORBIT.ELF`:
- on the card that already has `BOOT/`, else on the first card;
- written only when missing or different;
- with a toast when it is written.

The stub SHALL bring up the USB and run `mass0:/launcher.elf`, and SHALL say on screen when the USB or the file is
missing, retrying by itself.

#### Scenario: Autoboot path
- **WHEN** the stub is started with the USB inserted
- **THEN** the launcher starts

### Requirement: In Game Reset
When the USB's Neutrino is the ORBIT fork and the combos are set, every PS2 game SHALL be launched with
`-igr=<mask> -igrexit=rom0:OSDSYS -igroff=<mask>`. During the game:
- holding exactly the reboot combo SHALL reset the IOP, stop the game's sound, undo Neutrino's kernel patches and
  run rom0:OSDSYS through LoadExecPS2, as a reboot (FMCB then autoboots the stub, so the launcher comes back);
- holding exactly the power-off combo SHALL power the console off;
- the power button SHALL power off when pressed once, and return when pressed twice within about 1 s, as in OPL.

With the official Neutrino, no IGR option SHALL be passed.

#### Scenario: Reboot
- **WHEN** the user holds L1+L2+R1+R2+START+SELECT in a game started from the launcher
- **THEN** the console reboots into FMCB, which autoboots the launcher when its Auto key points at the stub

### Requirement: Gate
On the console, the phase SHALL pass if:
- Black reboots with the reboot combo, also during its intro movies;
- the console powers off with the power-off combo;
- a custom combo set in `config.ini` works;
- FMCB autoboots the stub after the reboot.

Results go to `docs/phase12-results.md`.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file says which games returned and powered off, and any that did not
