## ADDED Requirements

### Requirement: SYSTEM.CNF boot line
`iso.c` SHALL parse a SYSTEM.CNF for either the PS2 `BOOT2` line or the PS1 `BOOT` line:
- the file name, as in "SLUS_209.46" from `cdrom0:\SLUS_209.46;1` or "SLUS_007.47" from `cdrom:\SLUS_007.47;1`;
- the raw path.

It SHALL read PS1 VCD images, which hold a 1 MB header followed by 2352-byte Mode 2 sectors whose 2048 data bytes
start 24 bytes into each sector.

#### Scenario: PS1 line
- **WHEN** the parser gets `BOOT = cdrom:\SCUS_941.63;1`
- **THEN** in PS1 mode it returns "SCUS_941.63", and in PS2 mode it finds nothing
