## Context

Neutrino v1.8.0 emulates the memory card with `-mc0=<file>` (`emu-mc-file.toml` loads `mc_emu.irx` in the game
stage). Its `mc_emu` serves port 0 only (`vmcSpec[1]` inactive in `mcemu_var.c`), 512-byte pages and 16-page
blocks, computes the ECC itself and reads/writes the image through FHI (`fhi_read` / `fhi_write`): the file is a
raw card without ECC, 8 MB as OPL's. Files on a block device count against the 64-fragment limit together with the
ISO.

## Decisions

1. **Format on the EE.** `vmc.c` writes the same layout as OPL's genvmc (1 KB clusters, IFC at 8, FAT at 9..40,
   root at 41, erased 0xFF elsewhere) with stdio, 32 KB at a time, so it works on every source device. A card that
   fails to write is removed. Checked: `mymcplus df` reports 8 134 KB free, and saves written by mymcplus (one dir
   in a chained root, a 5 000-byte file over 5 clusters) read back byte for byte.
2. **Created at launch, not at boot**: only games actually played get 8 MB on the device; the launch screen says
   "CREANDO MEMORY CARD VIRTUAL". A freshly written 8 MB file is normally one fragment.
3. **Where**: on the game's own source, because Neutrino has one backing store per run (`-bsd`). `compartida`
   therefore means one `VMC/ORBIT.bin` per device.
4. **Exceptions**: HD Loader is read-only and DVD-only in Neutrino → physical card. An MMCE switches its own card
   per game (devctl 0x8 with the title ID, as nhddl) instead of a file VMC.
5. **Sources load after the USB is mounted**, so `mass0:` stays the launcher's USB (config, covers, Neutrino,
   POPS, APPS). Every BDM device is addressed by its driver's name (`ata0:`, `mx4sio0:`, `ilink0:`, `udpbd0:`),
   which ps2sdk's bdmfs_fatfs registers and Neutrino maps to its `-bsd` (`bsd_from_path`). MMCE and UDPFS use
   their own devices (`mmce0:/`, `udpfs0:/`, nhddl's form).
6. **Drivers**: the ps2sdk ones are embedded (same build as our bdm). smap / ministack / udpbd / udpfs_ioman exist
   only in Neutrino: read from `mass0:/neutrino/modules/`. ministack needs a fixed IP (`[red] ip`); the launcher
   copies it into Neutrino's `bsd-udpbd.toml` / `bsd-udpfs.toml`, which the game stage uses. Neutrino's smap then
   owns the adapter, so the lwIP cover download is skipped (`PORTADAS: LA RED LA USAN LOS JUEGOS`).
7. **HDD**: ata_bd first; when no FAT/exFAT volume shows up in 3 s, ps2hdd-bdm (`-o 4 -n 20`) and an APA check;
   HD Loader games are the 0x1337 main partitions with the 0xDEADFEED header (title, startup = serial).

## Risks

- `mc_emu` reports `CardSize` 8192 pages (4 MB) for any image while the superblock says 8 MB; mcman takes the
  geometry from the superblock, and 8 MB cards are what nhddl users pass, but it is unverified here.
- Neutrino's argument space (~255 bytes): `-mc0` adds ~28 bytes, recovered by the loader's `""` argv[0].
- None of the new sources could be tried: no console or PCSX2 in this session. Phase gate below.
