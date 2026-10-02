"""Apply the ORBIT IGR integration edits to a Neutrino v1.8.0 checkout (cwd = neutrino root)."""
import re
from pathlib import Path


def edit(path, old, new, count=1):
    p = Path(path)
    s = p.read_text()
    assert s.count(old) == count, (path, old[:60], s.count(old))
    p.write_text(s.replace(old, new))


# 1. ee_core <-> loader interface: the IGR settings
edit("common/include/eecore_config.h",
     "    uint32_t mod_checksum_4k[EEC_MOD_CHECKSUM_COUNT];\n} __attribute__((packed, aligned(4)));",
     "    uint32_t mod_checksum_4k[EEC_MOD_CHECKSUM_COUNT];\n\n"
     "    // ORBIT: In Game Reset (0 = off). libpad button masks (PAD_* bits), and the ELF to return to\n"
     "    uint16_t IgrExitCombo;\n    uint16_t IgrOffCombo;\n    char IgrExitPath[64];\n"
     "    uint16_t IgrMenuCombo; // phase 13: opens the in-game menu\n"
     "} __attribute__((packed, aligned(4)));")

# 2. ee_core: build igr.c; hook libpad when the game asks for an IOP reset and right after its ELF is loaded
edit("ee/ee_core/Makefile", "CHEATCORE_EE_OBJS = cheat_engine.o cheat_api.o",
     "CHEATCORE_EE_OBJS = cheat_engine.o cheat_api.o igr.o menu.o resetspu_irx.o # ORBIT In Game Reset")
edit("ee/ee_core/src/iopmgr.c", '#include "eecore_config.h"\n', '#include "eecore_config.h"\n#include "igr.h"\n')
edit("ee/ee_core/src/iopmgr.c",
     "    struct _iop_reset_pkt *reset_pkt = (struct _iop_reset_pkt *)sdd->src;\n\n    New_Reset_Iop2(",
     "    struct _iop_reset_pkt *reset_pkt = (struct _iop_reset_pkt *)sdd->src;\n\n"
     "    // ORBIT IGR: the game's code is in memory now, hook its libpad open call (as OPL syshook.c)\n"
     "    if (IGR_Enabled() && padOpen_hooked == 0)\n"
     "        padOpen_hooked = Install_PadOpen_Hook(0x00100000, 0x01ff0000, PADOPEN_HOOK);\n\n"
     "    New_Reset_Iop2(")
edit("ee/ee_core/src/iopmgr.c",
     "//---------------------------------------------------------------------------\n// Replace SifSetDma, SifSetReg and SifGetReg syscalls in kernel\n",
     "//---------------------------------------------------------------------------\n"
     "// ORBIT IGR: put the original syscalls back before the IGR resets the IOP to the ROM modules\n"
     "void Remove_Kernel_Hooks(void)\n{\n"
     "    SetSyscall(__NR_SifSetDma, Old_SifSetDma);\n"
     "    SetSyscall(__NR_SifSetReg, Old_SifSetReg);\n"
     "    SetSyscall(__NR_SifGetReg, Old_SifGetReg);\n}\n\n"
     "//---------------------------------------------------------------------------\n// Replace SifSetDma, SifSetReg and SifGetReg syscalls in kernel\n")
edit("ee/ee_core/src/main.c", '#include "eecore_config.h"\n', '#include "eecore_config.h"\n#include "igr.h"\n')
edit("ee/ee_core/src/main.c",
     "            apply_patches(argv[0]);\n",
     "            apply_patches(argv[0]);\n"
     "            if (IGR_Enabled()) // ORBIT IGR: hook libpad in the freshly loaded game\n"
     "                padOpen_hooked = Install_PadOpen_Hook(0x00100000, 0x01ff0000, PADOPEN_HOOK);\n")

# 3. loader: -igr=<mask> (exit), -igroff=<mask> (power off), -igrexit=<elf path>
edit("ee/loader/src/main.c",
     '        else if (!strncmp(argv[i], "-qb", 3))\n',
     '        else if (!strncmp(argv[i], "-igr=", 5)) // ORBIT IGR\n'
     '            igr_exit = strtoul(&argv[i][5], NULL, 0);\n'
     '        else if (!strncmp(argv[i], "-igroff=", 8))\n'
     '            igr_off = strtoul(&argv[i][8], NULL, 0);\n'
     '        else if (!strncmp(argv[i], "-igrexit=", 9))\n'
     '            igr_path = &argv[i][9];\n'
     '        else if (!strncmp(argv[i], "-igrmenu=", 9))\n'
     '            igr_menu = strtoul(&argv[i][9], NULL, 0);\n'
     '        else if (!strncmp(argv[i], "-qb", 3))\n')
edit("ee/loader/src/main.c",
     "static int parse_cmdline_args(int argc, char *argv[], int *out_iELFArgcStart)\n",
     "static unsigned igr_exit, igr_off, igr_menu; // ORBIT IGR settings, copied into sys.eecore before ee_core starts\n"
     "static const char *igr_path = \"\";\n\n"
     "static int parse_cmdline_args(int argc, char *argv[], int *out_iELFArgcStart)\n")
edit("ee/loader/src/main.c",
     "    *set_ee_core = sys.eecore;",
     "    sys.eecore.IgrExitCombo = igr_exit; // ORBIT IGR\n"
     "    sys.eecore.IgrOffCombo  = igr_off;\n"
     "    sys.eecore.IgrMenuCombo = igr_menu;\n"
     "    strncpy(sys.eecore.IgrExitPath, igr_path, sizeof(sys.eecore.IgrExitPath) - 1);\n"
     "    *set_ee_core = sys.eecore;")
edit("ee/loader/src/main.c",
     '    printf("  -qb               Quick-Boot directly into load environment\\n");\n',
     '    printf("  -qb               Quick-Boot directly into load environment\\n");\n'
     '    printf("  -igr=<mask>       ORBIT: libpad button mask that returns to -igrexit (0 = off)\\n");\n'
     '    printf("  -igroff=<mask>    ORBIT: libpad button mask that powers the console off\\n");\n'
     '    printf("  -igrexit=<elf>    ORBIT: ELF to run on return, read by the ROM IOP (rom0:OSDSYS = reboot)\\n");\n'
     '    printf("  -igrmenu=<mask>   ORBIT: libpad button mask that opens the in-game menu\\n");\n')
# 4. loader: write the D-cache back before ee_core starts. It copies ee_core and patches the kernel's code through
#    the cache, then ExecPS2s without a flush (ps2sdk's own ELF loaders flush first). On the console some builds of
#    ee_core never started (black screen) while PCSX2, which does not model the caches, ran them all
edit("ee/loader/src/main.c",
     "    ExecPS2((void *)eh->entry, NULL, ee_core_argc, ee_core_argv);",
     "    FlushCache(0); // ORBIT: see patch_neutrino.py\n"
     "    FlushCache(2);\n"
     "    ExecPS2((void *)eh->entry, NULL, ee_core_argc, ee_core_argv);")

# 5. phase 13 (in-game menu): GSM records the game's DISPFB1/2 and PMODE writes, so the menu knows the shown frame.
#    The breakpoint mask widens from PMODE/SMODE2/DISPLAY1/2 to the whole 0x00-0xF0 block (DISPFB1 is 0x70,
#    DISPFB2 0x90); the extra registers pass through. Without -gsm, GSM runs capture-only when the menu is on: the
#    SetGsCrt hook only arms the breakpoint, every value is written as is
G = "ee/ee_core/src/gsm_api.c"
edit(G, "    u64 last_display1;\n    u64 last_display2;\n};",
     "    u64 last_display1;\n    u64 last_display2;\n"
     "    u64 last_dispfb1; // ORBIT: the shown frame, for the in-game menu\n"
     "    u64 last_dispfb2;\n"
     "    u64 last_pmode;\n"
     "    u64 last_smode2;\n};")
edit(G, "            pstate->last_display1 = value;\n            *dest = mod_DISPLAY(pstate, value);",
     "            pstate->last_display1 = value;\n"
     "            *dest = pstate->GsmVideoMode == EECORE_GSM_VMODE_NONE ? value : mod_DISPLAY(pstate, value);")
edit(G, "            pstate->last_display2 = value;\n            *dest = mod_DISPLAY(pstate, value);",
     "            pstate->last_display2 = value;\n"
     "            *dest = pstate->GsmVideoMode == EECORE_GSM_VMODE_NONE ? value : mod_DISPLAY(pstate, value);")
edit(G, "        case (u32)GS_REG_SMODE2:\n            // Store game requested mode\n",
     "        case (u32)GS_REG_SMODE2:\n"
     "            pstate->last_smode2 = value; // ORBIT\n"
     "            if (pstate->GsmVideoMode == EECORE_GSM_VMODE_NONE) { // ORBIT: capture only\n"
     "                *dest = value;\n"
     "                break;\n"
     "            }\n"
     "            // Store game requested mode\n")
edit(G, "        case (u32)GS_REG_PMODE:\n        case (u32)GS_REG_SIGLBLID:\n            *dest = value;\n"
        "            break;\n        default:\n            BGERROR(COLOR_FUNC_GSM, 4);",
     "        case (u32)GS_REG_PMODE:\n"
     "            pstate->last_pmode = value; // ORBIT\n"
     "            *dest = value;\n"
     "            break;\n"
     "        case (u32)GS_REG_DISPFB1: // ORBIT: the shown frame, for the in-game menu\n"
     "            pstate->last_dispfb1 = value;\n"
     "            *dest = value;\n"
     "            break;\n"
     "        case (u32)GS_REG_DISPFB2:\n"
     "            pstate->last_dispfb2 = value;\n"
     "            *dest = value;\n"
     "            break;\n"
     "        case (u32)GS_REG_SIGLBLID:\n            *dest = value;\n"
     "            break;\n        default:\n"
     "            if (((u32)dest & 0x1fffef0f) == 0x12000000) { // ORBIT: the rest of the wider mask passes through\n"
     "                *dest = value;\n"
     "                break;\n"
     "            }\n"
     "            BGERROR(COLOR_FUNC_GSM, 4);")
edit(G, "        default:\n            BGERROR(COLOR_FUNC_GSM, 3);",
     "        default:\n"
     "            if (((u32)source & 0x1fffef0f) == 0x12000000) { // ORBIT: wider mask, pass through\n"
     "                regs->gpr[rt] = *source;\n"
     "                break;\n"
     "            }\n"
     "            BGERROR(COLOR_FUNC_GSM, 3);")
# Capture-only arms the breakpoint where GSM does, after the game's SetGsCrt: armed at once, it caught the kernel's
# own GS setup while the game booted and hung in BGERROR (console: black screen in "Nativo")
edit(G, "    //printf(\"%s(%d, 0x%x, %d)\\n\", __FUNCTION__, interlace, mode, ffmd);\n",
     "    //printf(\"%s(%d, 0x%x, %d)\\n\", __FUNCTION__, interlace, mode, ffmd);\n\n"
     "    if (pstate->GsmVideoMode == EECORE_GSM_VMODE_NONE) { // ORBIT: capture only: the game's mode as is, then arm\n"
     "        _ee_disable_bpc();\n"
     "        pstate->org_SetGsCrt(interlace, mode, ffmd);\n"
     "        pstate->game.mode = mode;\n"
     "        _ee_enable_bpc(EE_BPC_DWE | EE_BPC_DUE | EE_BPC_DKE);\n"
     "        return;\n"
     "    }\n")
# ... and never hangs: an access it cannot emulate runs as is with the breakpoint off, re-armed at the next VBLANK
edit(G, "        _ee_disable_bpc();\n        BGERROR(COLOR_FUNC_GSM, 2);\n",
     "        _ee_disable_bpc();\n"
     "        if (pstate->GsmVideoMode == EECORE_GSM_VMODE_NONE) // ORBIT: capture only: ErrorEPC unchanged, it re-runs\n"
     "            return;\n"
     "        BGERROR(COLOR_FUNC_GSM, 2);\n")
edit(G, "_ee_mtdabm(0x1fffef5f);", "_ee_mtdabm(0x1fffef0f); // ORBIT: was 0x1fffef5f")
edit(G, "_ee_mtdabm(0x1fffff5f);", "_ee_mtdabm(0x1fffff0f); // ORBIT: was 0x1fffff5f")
edit(G, "    SetSyscall(__NR_SetGsCrt, pstate->org_SetGsCrt);\n}\n",
     "    SetSyscall(__NR_SetGsCrt, pstate->org_SetGsCrt);\n}\n\n"
     "// ORBIT: capture only, called by the IGR's VBLANK handler (kernel mode): arm again after an access that switched\n"
     "// the breakpoint off, once the game's SetGsCrt has run\n"
     "void GSM_Rearm(void)\n{\n"
     "    if (state.GsmVideoMode == EECORE_GSM_VMODE_NONE && state.org_SetGsCrt != NULL && state.game.mode != 0)\n"
     "        _ee_enable_bpc(EE_BPC_DWE | EE_BPC_DUE | EE_BPC_DKE);\n}\n\n"
     "// ORBIT: the game's latest writes (0 = not seen yet): DISPFB1, DISPFB2, PMODE, DISPLAY1, DISPLAY2, SMODE2\n"
     "void GSM_GetDisplay(u64 out[6])\n{\n"
     "    out[0] = state.last_dispfb1;\n"
     "    out[1] = state.last_dispfb2;\n"
     "    out[2] = state.last_pmode;\n"
     "    out[3] = state.last_display1;\n"
     "    out[4] = state.last_display2;\n"
     "    out[5] = state.last_smode2;\n}\n")
edit("ee/ee_core/src/main.c",
     "if ((eec.GsmVideoMode != EECORE_GSM_VMODE_NONE) && ((eec.flags & EECORE_FLAG_UNHOOK) == 0)) {",
     "if ((eec.GsmVideoMode != EECORE_GSM_VMODE_NONE || eec.IgrMenuCombo) && ((eec.flags & EECORE_FLAG_UNHOOK) == 0)) {")
print("patched")
