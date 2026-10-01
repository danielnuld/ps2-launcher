// Second-stage ELF loader (phase 6). The launcher copies this ELF into RAM the BIOS leaves free (0x84000-0x100000,
// linkfile) and runs it; it loads argv[0] through the IOP's LOADFILE with the modules the launcher left
// running, then hands over argv + 1. It does NOT reset the IOP: Neutrino started with -qb expects USB + fileXio already
// loaded, and ps2sdk's elf-loader resets the IOP (first console run fell back to the browser). Idea from nhddl
// (reference only, docs/sources.md); own code.
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <tamtypes.h>
#include <ps2sdkapi.h>

// no libc start-up or shutdown work: smaller, and nothing touches the IOP
void _libcglue_init(void) {}
void _libcglue_deinit(void) {}
void _libcglue_args_parse(int argc, char **argv) { (void)argc, (void)argv; }
DISABLE_PATCHED_FUNCTIONS();
DISABLE_EXTRA_TIMERS_FUNCTIONS();
PS2_DISABLE_AUTOSTART_PTHREAD();

int main(int argc, char *argv[])
{
	t_ExecData elf = {0};
	if (argc < 1) return -1;
	SifInitRpc(0);
	for (u32 a = 0x100000; a < (u32)GetMemorySize(); a += 16) *(volatile u128 *)a = 0; // nothing of the launcher left
	FlushCache(0);
	SifLoadFileInit();
	int r = SifLoadElf(argv[0], &elf); // argv[0]: the file; argv[1..]: the program's own argv (phase 11), so it can
	SifLoadFileExit();                 // get another argv[0] (POPStarter finds its VCD from that name)
	if (r != 0 || !elf.epc || argc < 2) { SifExitRpc(); return -1; }
	FlushCache(0);
	FlushCache(2);
	return ExecPS2((void *)elf.epc, (void *)elf.gp, argc - 1, argv + 1);
}
