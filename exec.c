// Run another ELF through loader/loader.elf (embedded): copy its PT_LOAD segments to their addresses (0x84000..)
// and jump. The loader loads argv[0] without resetting the IOP (Neutrino -qb needs our USB modules; ps2sdk's
// elf-loader resets it) and runs it with argv + 1 (phase 11). Shared by the launcher and the boot stub (phase 12).
#include <string.h>
#include <kernel.h>
#include <sifrpc.h>
#include "exec.h"

extern unsigned char loader_elf[];

void run_loader(int argc, char *argv[])
{
	const u8 *e = loader_elf;
	if (e[0] != 0x7F || e[1] != 'E' || e[2] != 'L' || e[3] != 'F') return;
	u32 entry = *(u32 *)(e + 24), phoff = *(u32 *)(e + 28);
	u16 phnum = *(u16 *)(e + 44), phsz = *(u16 *)(e + 42);
	memset((void *)0x84000, 0, 0x100000 - 0x84000); // the loader's region, BSS and stack included
	for (int k = 0; k < phnum; k++) {
		const u32 *ph = (const u32 *)(e + phoff + k * phsz); // type, offset, vaddr, paddr, filesz, memsz
		if (ph[0] == 1) memcpy((void *)ph[2], e + ph[1], ph[4]);
	}
	SifExitRpc();
	FlushCache(0);
	FlushCache(2);
	ExecPS2((void *)entry, NULL, argc, argv);
}
