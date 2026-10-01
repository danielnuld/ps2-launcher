# Sources ledger

Status: **V** verified · **E** estimate · **D** our design choice · **X** retracted.
`ps2tek` = tag-stripped https://raw.githubusercontent.com/PSI-Rockin/ps2tek/master/index.html (`curl -sL <url> | sed -e 's/<[^>]*>//g'`), line numbers of that text.
`ps2sdk` = github.com/ps2dev/ps2sdk master as read 2026-09-30.

## Hardware

| Claim | Source | St |
|---|---|---|
| EE 294.912 MHz; 16 KB I-cache, 8 KB D-cache, 16 KB scratchpad | ps2tek:345-350 | V |
| COP0.Count increments every EE cycle | ps2tek:1117-1121 | V |
| Main RAM 32 MB, first 1 MB kernel | ps2tek:75 | V |
| GS VRAM 4 MB = 1 048 576 32-bit words; page = 2048 words | ps2tek:110; ps2sdk `graph_vram.h:13`, `graph_vram.c` | V |
| VU1 16 KB code / 16 KB data | ps2tek:81-82 | V |
| GS texture formats incl. PSMT8 (13h), PSMT4 (14h); max 1024×1024 | ps2tek:2085-2102 | V |
| FPU single precision only, no NaN/Inf, denormals → 0 | ps2tek:1156-1174 | V |

## Video modes

| Claim | Source | St |
|---|---|---|
| ps2sdk modes: 480p, 576p (BIOS ≥ 2.20 in ps2sdk; add-on for < 2.10 in OPL), 720p 1280×720, 1080i 1920×540/field; no 1080p | ps2sdk `graph.h:11-55`, `graph_mode.c:20-27,155-168`; OPL `src/gsm.c:76-104,127-134` @3e3f34e9 | V |
| `graph_set_screen` sets DISPLAY.DH from the buffer, so vertical upscale needs DISPLAY rewritten | ps2sdk `graph_mode.c:238-269`; fixed and checked in PCSX2 in ../ps2-hdtest | V |
| DISPLAY: MAGH 4 bits, MAGV 2 bits | ps2sdk `gs_privileged.h:75-78` | V |
| RiptOPL "1080p" reuses the 1080i DISPLAY/SYNCV, "not HW-validated" | NathanNeurotic/Open-PS2-Loader `src/gsm.c:107-113` @1c895e4 | V |
| User's SCPH-75001 + HDMI adapter shows 480p, 720p (×2, native CT16, native CT32) and 1080i (×2) | ../ps2-hdtest/results/hdtest-2026-09-30.txt (user pressed X in every mode) | V |

## Toolchain / SDK

| Claim | Source | St |
|---|---|---|
| USB mass init: IOP reset → SifInitRpc → sbv_patch_enable_lmb/disable_prefix_check → iomanX, fileXio (+fileXioInit) → bdm, bdmfs_fatfs, usbd_mini, usbmass_bd_mini; poll `opendir("massN:")` while the device mounts | pcm720/nhddl @821b6c9 `src/module_init.c:118-192`, `src/devices_bdm.c:100-115` (reference only) | V |
| `fileXio_rpc.h` refuses inclusion unless `NEWLIB_PORT_AWARE` is defined | installed ps2sdk `ee/include/fileXio_rpc.h:22-25` | V |
| `dma_channel_send_chain` syncs D-cache for the tag list only, not REF'd data | ps2sdk `ee/dma/src/dma.c` | V |
| GS page = 8192 B: CT32 64×32 px (8×4 blocks of 8×8), CT16 64×64 (4×8 blocks of 16×8), T8 128×64 | ps2dev/gsKit @8ef73d0 `ee/gs/src/gsTexture.c` `gsKit_texture_size`; CT32 also OPHTML `vram.py` `_PAGE_DIMS`, ps2tek:2324 | V |
| `graph_vram_size` rounds only the total to 2048 words, not the height to whole pages: a 720-line framebuffer followed by another allocation overlaps (garbage in the bottom rows: 212 bright px before, 0 after rounding, PCSX2) | ps2sdk `ee/graph/src/graph_vram.c`; this repo | V |
| GS dithering: registers DIMX (44h) and DTHE (45h) | ps2tek:1854-1855 (names only); ps2sdk `gs_gp.h:99-101,187-197` | V |
| Dither matrix {-4,2,-3,3,0,-2,1,-1,-3,3,-4,2,1,-1,0,-2}, 3-bit signed entries every 4 bits | gsKit @8ef73d0 `ee/gs/src/gsInit.c:472-473` (values 0-7 in code, signed form in its comment), `gsInit.h:804-808` (4-bit stride) | V |
| ps2sdk `GS_SET_DIMX` masks entries to 2 bits (drops the sign bit); gsKit `GS_SETREG_DIMX` shifts a literal `0` at bit 56 instead of its O argument | ps2sdk `gs_gp.h:187-196`; gsKit `gsInit.h:808` | V |
| 720p DISPLAY origin: ps2sdk x=420, y=40; OPL GSM DX=302, DY=24 | ps2sdk `graph_mode.c:26`; OPL `src/gsm.c:86` @3e3f34e9 (makeDISPLAY args DH, DW, MAGV, MAGH, DY, DX per its header comment l.77) | V |
| The user sees the picture misplaced on the TV (reported 2026-09-30) | user report; which offset is right for this TV/adapter is measured with modetest | E |
| KROM single-byte glyphs: 8×15, 1 byte per row, MSB = left pixel, ASCII 32-126 read from offset 0x198DE | ps2sdk `ee/font/src/fontx.c:102-168` (master, 2026-10-01) | V |
| CT16 RGB555 expands to 8 bits as `v << 3` (31 → 248); `tools/covers.py` quantizes to those levels | not yet in a cited document | E |
| TEXA after `draw_setup_environment` = TA0 0x80, TA1 0x80: a CT16 texel's alpha is 0x80 whatever bit 15 says | ps2sdk `ee/draw/src/draw.c:113` (master, 2026-10-01) | V |
| Inter: SIL OFL 1.1, variable axes opsz 14-32 and wght 100-900; ASCII at 36 px/700 + 22 px/500 = 188 glyphs, packed into 512×165 (atlas 512×256) | google/fonts `ofl/inter/Inter[opsz,wght].ttf` + `OFL.txt` (2026-10-01); `tools/font.py --selftest` | V |
| Memory card: load sio2man, then mcman/mcserv; `mcInit(MC_TYPE_MC)`; every mc* call except mcInit is async and needs `mcSync`; the first `mcGetInfo` after boot reports a new card; `mcGetDir("/*")` lists the root | ps2sdk `ee/include/libmc.h:20-30,315-327`, `samples/rpc/memorycard/mc_example.c:51-111` (installed SDK) | V |
| A save directory's name = region prefix (BA/BE/BI) + product code + game suffix, e.g. `BASLUS-20946...` | common knowledge, not in a cited document; matching uses `strstr(serial)` so the prefix does not matter | E |
| PSMT4 page = 128×128 px; PSMT4 host data has the even-x pixel in the low nibble; CT32 CLUT for PSMT8 in CSM1 = 16×16 with index bits 3/4 swapped | GS page/CLUT layout; checked by rendering in PCSX2 (phase-1 demo, text + CLUT ramps correct) | V (PCSX2) |
| BIOS font: `fontx_load("rom0:KROM", SINGLE_BYTE)`; fontx draws one point per glyph pixel; KROM shows `~` as an overline | ps2sdk `samples/font/font.c:192`, `ee/font/src/fontx.c`; PCSX2 screenshot | V |
| Written file verified: PCSX2 USB image (`DEV9hdd.raw`, Msd) contains the bench lines after a run | this repo, 2026-09-30 | V |

## Reference projects (not dependencies)

| Project | What we learn from it | Source |
|---|---|---|
| OPHTML | Offline layout → replayed draw list; VRAM model with 3 CT32 buffers | coffeedevsolutions/OPHTML @60810bd `docs/site/_facts/authoring/*.md` |
| Neutrino | Backend we launch as an ELF | rickgaiser/neutrino README |
| PS2BBL / OSDMenu | Boot layer that launches us | israpps/PlayStation2-Basic-BootLoader, pcm720/OSDMenu READMEs |

## Sound

| Claim | Source | St |
|---|---|---|
| audsrv needs libsd loaded first; `audsrv_init`, `audsrv_adpcm_init`, `audsrv_load_adpcm(&s, buf, size incl. header)`, `audsrv_ch_play_adpcm(-1, &s)` picks a free voice of 24, `audsrv_adpcm_set_volume_and_pan(ch, 0-100, pan)` | ps2sdk `ee/include/audsrv.h:215-262`, `samples/rpc/audsrv/playadpcm/playadpcm.c` (installed SDK) | V |
| ADPCM header = 16 bytes `APCM`; word1 bits 8-15 channels, 16-23 loop; word2 pitch; samples are DMA'd into SPU2 RAM (2 MB limit checked) | ps2sdk `iop/sound/audsrv/src/adpcm.c:143-200` (master, 2026-10-01) | V |
| `adpenc` (ps2sdk bin) writes that header: 48 kHz WAV gives pitch 4096 | `sfx_*.adp` headers inspected, this repo | V |

## Games / Neutrino

| Claim | Source | St |
|---|---|---|
| Neutrino CLI: `-dvd=<bsd>:<path>` picks the BSD from the prefix (usb, ata, mx4sio, mmce, ilink, udpbd, udpfs); `-qb` quick-boots; FAT32/exFAT on block devices; at most 64 fragments | Neutrino v1.8.0 README (github.com/ps2max32/neutrino, formerly rickgaiser/neutrino; same repository id 657734267) | V |
| A frontend launches it with argv[0] = the Neutrino path, then `-bsd`, `-dvd`, `-qb` | pcm720/nhddl `src/launcher.c` @main 2026-10-01 (reference only) | V |
| `LoadELFFromFile(path, argc, argv)` | ps2sdk `ee/include/elf-loader.h:24` (installed) | V |
| PS2 discs: SYSTEM.CNF `BOOT2 = cdrom0:\SLUS_217.82;1`; on a real Persona 4 ISO it sits at LBA 1 761 296 (3.6 GB in) | ISO inspected, this repo | V |
| `fileXioLseek64(fd, s64, whence)` for offsets past 2 GB (the EE's `long` is 32 bits) | ps2sdk `ee/include/fileXio_rpc.h:60` | V |
