## ADDED Requirements

### Requirement: nuld games launch with Neutrino's own network
A game from the `udpfs` source SHALL be launched with `-bsd=udpfs -dvd=udpfs:<path>` and without `-qb`, after the
launcher's network is shut down, so Neutrino reboots the IOP into its load environment with its own smap, ministack
(the configured fixed IP) and udpfs modules.

#### Scenario: Black from nuld
- **WHEN** X is pressed on Black from the `udpfs` source
- **THEN** the launch line has `-dvd=udpfs:DVD/Black.iso` and no `-qb`, and Black boots
