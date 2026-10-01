## ADDED Requirements

### Requirement: Combos in the config
`config.ini [igr]` SHALL accept:
- `volver` and `apagar`, each a list of buttons joined with `+`;
- Spanish or English names: L1 L2 R1 R2 L3 R3 START SELECT ARRIBA/UP ABAJO/DOWN IZQUIERDA/LEFT DERECHA/RIGHT
  TRIANGULO/TRIANGLE CIRCULO/CIRCLE X/CROSS CUADRADO/SQUARE, in any case.

An empty value or an unknown name SHALL disable that combo. The defaults SHALL be L1+L2+R1+R2+START+SELECT
(return) and L1+L2+R1+R2+L3+R3 (power off).

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
`-igr=<mask> -igrexit=<stub path> -igroff=<mask>`. During the game:
- holding exactly the return combo SHALL reset the IOP to the ROM modules and run the stub (back to the launcher);
- holding exactly the power-off combo SHALL power the console off;
- the power button SHALL power off when pressed once, and return when pressed twice within about 1 s, as in OPL.

With the official Neutrino, no IGR option SHALL be passed.

#### Scenario: Back to the launcher
- **WHEN** the user holds L1+L2+R1+R2+START+SELECT in a game started from the launcher
- **THEN** the console returns to the launcher home screen without a power cycle

### Requirement: Gate
On the console, the phase SHALL pass if:
- Black returns to the launcher with the return combo;
- the console powers off with the power-off combo;
- a custom combo set in `config.ini` works;
- the stub boots the launcher.

Results go to `docs/phase12-results.md`.

#### Scenario: Gate recorded
- **WHEN** the console run is done
- **THEN** the results file says which games returned and powered off, and any that did not
