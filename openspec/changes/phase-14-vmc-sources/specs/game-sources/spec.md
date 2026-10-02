## ADDED Requirements

### Requirement: Sources from the config
`config.ini [juegos] origen` SHALL take a comma-separated list of `usb`, `hdd`, `mx4sio`, `ilink`, `mmce`, `udpbd`,
`udpfs`. Drivers SHALL load only for the listed sources, after the launcher's USB is mounted. A source that does not
come up SHALL be named in a toast; the others still work. An empty or unknown list SHALL mean `usb`.

#### Scenario: MX4SIO and MMCE together
- **WHEN** both are listed
- **THEN** only MX4SIO is used and a toast says so

#### Scenario: UDP without a fixed IP
- **WHEN** `udpbd` or `udpfs` is listed and `[red] ip = dhcp`
- **THEN** a toast asks for a fixed IP and no UDP driver loads

### Requirement: Discovery per source
`DVD/*.iso` and `CD/*.iso` SHALL be listed on every source; `hdd` SHALL also list HD Loader partitions of an APA
disk (title and serial from the HD Loader header). PS1 VCDs and apps stay on the USB. Each game SHALL show its
source in the source chip.

#### Scenario: Same game on two devices
- **WHEN** a game is on the USB and on the HDD
- **THEN** both entries are listed, side by side, with different chips

### Requirement: Launch per source
Neutrino SHALL get `-dvd=<name><path>` with the source's driver name (`usb:`, `ata0:`, `mx4sio0:`, `ilink0:`,
`udpbd0:`, `mmce0:/`, `udpfs0:/`) and `-qb`; HD Loader games SHALL get `-bsd=ata -bsdfs=hdl -dvd=hdl:<partition>`
without `-qb`. Launches whose arguments exceed 255 bytes SHALL be refused with a toast.

#### Scenario: HDD game
- **WHEN** X is pressed on a game in `ata0:/DVD`
- **THEN** Neutrino starts with `-dvd=ata0:DVD/<file>.iso -mc0=ata0:VMC/<serial>.bin ... -qb`
