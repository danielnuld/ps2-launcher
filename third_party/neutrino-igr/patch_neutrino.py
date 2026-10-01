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
     "} __attribute__((packed, aligned(4)));")

# 2. ee_core: build igr.c; hook libpad when the game asks for an IOP reset and right after its ELF is loaded
edit("ee/ee_core/Makefile", "CHEATCORE_EE_OBJS = cheat_engine.o cheat_api.o",
     "CHEATCORE_EE_OBJS = cheat_engine.o cheat_api.o igr.o # igr.o: ORBIT In Game Reset")
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
     '        else if (!strncmp(argv[i], "-qb", 3))\n')
edit("ee/loader/src/main.c",
     "static int parse_cmdline_args(int argc, char *argv[], int *out_iELFArgcStart)\n",
     "static unsigned igr_exit, igr_off; // ORBIT IGR settings, copied into sys.eecore before ee_core starts\n"
     "static const char *igr_path = \"\";\n\n"
     "static int parse_cmdline_args(int argc, char *argv[], int *out_iELFArgcStart)\n")
edit("ee/loader/src/main.c",
     "    *set_ee_core = sys.eecore;",
     "    sys.eecore.IgrExitCombo = igr_exit; // ORBIT IGR\n"
     "    sys.eecore.IgrOffCombo  = igr_off;\n"
     "    strncpy(sys.eecore.IgrExitPath, igr_path, sizeof(sys.eecore.IgrExitPath) - 1);\n"
     "    *set_ee_core = sys.eecore;")
edit("ee/loader/src/main.c",
     '    printf("  -qb               Quick-Boot directly into load environment\\n");\n',
     '    printf("  -qb               Quick-Boot directly into load environment\\n");\n'
     '    printf("  -igr=<mask>       ORBIT: libpad button mask that returns to -igrexit (0 = off)\\n");\n'
     '    printf("  -igroff=<mask>    ORBIT: libpad button mask that powers the console off\\n");\n'
     '    printf("  -igrexit=<elf>    ORBIT: ELF to run on return, on a memory card (mc0:/...)\\n");\n')
print("patched")
