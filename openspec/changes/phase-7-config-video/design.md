## Context

Neutrino v1.8.0 `-gsm=v:c` (`fp2` = 480p/576p, `1080ix2` = 1080i at ×2; `c` = 1/2/3 field-flipping compat) and
`-gc=<digits>` (0 fast reads, 2 sync reads, 3 unhook syscalls, 5 emulate DVD-DL, 7 fix buffer overrun) come from
its README. The user's HDMI adapter showed 480p and 1080i ×2 in phase 0's hdtest.

## Decisions

- **INI, not TOML:** the files are hand-edited on a PC. INI with `;` comments is the least surprising, and the
  parser is a few dozen lines with a selftest. The reader keeps the lines so the writer can rewrite the file.
- **Per-game file** `juegos.ini`: one section per dash serial, holding only the keys that differ from the
  defaults. It is rewritten when the options panel closes.
- **Effective video mode** = per-game `video`, else `[video] modo`. Default 480p, as agreed with the user.
- **Options panel:** an rrect panel over a dimmed home screen, rows with value pills and ‹ › hints, and the panel
  sound on open and close. Input goes to the panel while it is open.
- **Sources:** only `usb` has drivers now. Other values are accepted and shown as "no disponible aún" in a toast
  at boot. `ponytail:` a single source; a list of sources comes with their drivers.

## Risks / Trade-offs

- [Forced 480p/1080i breaks some games] → per-game Nativo and GSM compat, remembered per serial.
- [Writing to the USB while the user may remove it] → files are tiny and written only when the panel closes or the
  defaults are created.
