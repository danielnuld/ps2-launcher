## Why

The user wants every PS2 game to use a virtual memory card by default, and games loaded from more than the USB:
internal HDD, network, MX4SIO and the rest of Neutrino's backing stores (2026-10-02), before the Jellyfin phase.
Until now the launcher only read the physical cards and only listed `mass0:/DVD` and `mass0:/CD`.

## What Changes

- **Virtual memory cards (default)**: each PS2 game gets `VMC/<serial>.bin` on its own device, an 8 MB card
  created and formatted by the launcher at the first launch, passed to Neutrino as `-mc0=` (slot 1).
  `config.ini [memorycard] modo` = `juego` (default) | `compartida` (`VMC/ORBIT.bin`) | `fisica`; per game in the
  △ panel (`juegos.ini mc =`). The saves card and the 3D icon read the virtual card when the game uses one.
- **Game sources**: `config.ini [juegos] origen` takes a list: `usb, hdd, mx4sio, ilink, mmce, udpbd, udpfs`.
  `hdd` covers an exFAT/FAT disk and an APA disk with HD Loader partitions. Drivers load only for the listed
  sources; the source chip shows where each game lives.
- Neutrino arguments per source (`-dvd=ata0:DVD/x.iso`, `-bsd=ata -bsdfs=hdl -dvd=hdl:<part>` without `-qb`, ...).
- The loader takes `""` as "argv[0] = the file", 28 bytes less of arguments for the extra `-mc0`; launches over
  Neutrino's 255-byte argument limit are refused with a toast.

## Capabilities

### New Capabilities
- `vmc`: virtual memory card per game, shared or physical.
- `game-sources`: PS2 games from several devices.

### Modified Capabilities
- `games`: discovery and launch per source.

## Impact

- New: `vmc.c`/`.h` (host selftest, checked against mymcplus), `sources.c`/`.h`.
- `launcher.c`: config, discovery, saves card, △ panel row, source chip, launch arguments.
- `net.c`: ps2dev9 shared with the HDD / UDP drivers; no cover download while udpbd/udpfs own the adapter.
- `loader/loader.c`: `""` argv[0]. The boot stub embeds the loader, so the launcher rewrites `BOOT/ORBIT.ELF` once.
- Makefile: ata_bd, ps2hdd-bdm, mx4sio_bd_mini, iLinkman, IEEE1394_bd_mini, mmceman embedded in the launcher.
- UDP sources need Neutrino's `modules/` (smap, ministack, udpbd / udpfs_ioman) and a fixed `[red] ip`.
