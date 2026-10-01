// ORBIT boot stub (phase 12), installed by the launcher at mc?:/BOOT/ORBIT.ELF. Two uses:
// - the In Game Reset of the ORBIT Neutrino fork returns here (after an IOP reset only the ROM modules are left,
//   so the way back has to start on a memory card);
// - autoboot: OSDMenu / FMCB can start this file.
// It brings up the USB (iop.c, same modules as the launcher) and runs mass0:/launcher.elf through the loader, so the
// launcher itself is only ever updated on the USB. Without the USB it says so and keeps waiting.
#include <stdio.h>
#include <kernel.h>
#include <debug.h>
#include "iop.h"
#include "exec.h"

#define LAUNCHER "mass0:/launcher.elf"

int main(void)
{
	int shown = 0;
	if (!iop_load()) { init_scr(); scr_printf("\n\n  ORBIT: no se pudieron cargar los modulos del IOP\n"); SleepThread(); }
	for (;;) {
		if (usb_wait()) {
			FILE *f = fopen(LAUNCHER, "rb");
			if (f) {
				fclose(f);
				char *argv[2] = {LAUNCHER, LAUNCHER};
				run_loader(2, argv);
			}
		}
		if (!shown) { // ponytail: libdebug text, this screen is rare
			init_scr();
			scr_printf("\n\n  ORBIT\n\n  Conecta la USB con launcher.elf en la raiz.\n  Se reintenta solo.\n");
			shown = 1;
		}
	}
	return 0;
}
