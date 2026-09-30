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
| Which modes the user's HDMI adapter accepts | pending ../ps2-hdtest on console | E |

## Toolchain / SDK

| Claim | Source | St |
|---|---|---|
| USB mass init: IOP reset → SifInitRpc → sbv_patch_enable_lmb/disable_prefix_check → iomanX, fileXio (+fileXioInit) → bdm, bdmfs_fatfs, usbd_mini, usbmass_bd_mini; poll `opendir("massN:")` while the device mounts | pcm720/nhddl @821b6c9 `src/module_init.c:118-192`, `src/devices_bdm.c:100-115` (reference only) | V |
| `fileXio_rpc.h` refuses inclusion unless `NEWLIB_PORT_AWARE` is defined | installed ps2sdk `ee/include/fileXio_rpc.h:22-25` | V |
| `dma_channel_send_chain` syncs D-cache for the tag list only, not REF'd data | ps2sdk `ee/dma/src/dma.c` | V |
| GS page = 8192 B: CT32 64×32 px (8×4 blocks of 8×8), CT16 64×64 (4×8 blocks of 16×8), T8 128×64 | ps2dev/gsKit @8ef73d0 `ee/gs/src/gsTexture.c` `gsKit_texture_size`; CT32 also OPHTML `vram.py` `_PAGE_DIMS`, ps2tek:2324 | V |
| `graph_vram_size` rounds only the total to 2048 words, not the height to whole pages: a 720-line framebuffer followed by another allocation overlaps (garbage in the bottom rows: 212 bright px before, 0 after rounding, PCSX2) | ps2sdk `ee/graph/src/graph_vram.c`; this repo | V |
| BIOS font: `fontx_load("rom0:KROM", SINGLE_BYTE)`; fontx draws one point per glyph pixel; KROM shows `~` as an overline | ps2sdk `samples/font/font.c:192`, `ee/font/src/fontx.c`; PCSX2 screenshot | V |
| Written file verified: PCSX2 USB image (`DEV9hdd.raw`, Msd) contains the bench lines after a run | this repo, 2026-09-30 | V |

## Reference projects (not dependencies)

| Project | What we learn from it | Source |
|---|---|---|
| OPHTML | Offline layout → replayed draw list; VRAM model with 3 CT32 buffers | coffeedevsolutions/OPHTML @60810bd `docs/site/_facts/authoring/*.md` |
| Neutrino | Backend we launch as an ELF | rickgaiser/neutrino README |
| PS2BBL / OSDMenu | Boot layer that launches us | israpps/PlayStation2-Basic-BootLoader, pcm720/OSDMenu READMEs |
