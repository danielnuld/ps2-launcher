// IOP setup shared by the test ELFs: USB mass storage (mass0:) and pad.
// Sequence as in pcm720/nhddl src/module_init.c:70-192 @821b6c9 (reference only, see docs/sources.md):
// IOP reset -> RPC -> sbv patches (load modules from EE RAM) -> iomanX, fileXio (+fileXioInit), sio2man,
// freepad, bdm, bdmfs_fatfs, usbd_mini, usbmass_bd_mini; then poll mass0: while the stick mounts.
#include <dirent.h>
#include <unistd.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <libpad.h>
#define NEWLIB_PORT_AWARE // only fileXioInit() is used; file I/O goes through stdio (fileXio_rpc.h guard)
#include <fileXio_rpc.h>
#include "iop.h"

#define IRX(m) extern unsigned char m##_irx[]; extern unsigned int size_##m##_irx
IRX(iomanX); IRX(fileXio); IRX(sio2man); IRX(freepad); IRX(bdm); IRX(bdmfs_fatfs); IRX(usbd_mini); IRX(usbmass_bd_mini);

static unsigned char pad_buf[256] __attribute__((aligned(64))); // size/alignment as nhddl src/pad.c:8

int iop_init(void)
{
	while (!SifIopReset("", 0)) {}
	while (!SifIopSync()) {}
	SifInitRpc(0);
	sbv_patch_enable_lmb();
	sbv_patch_disable_prefix_check();
	struct { unsigned char *irx; unsigned int *size; } mods[] = {
		{iomanX_irx, &size_iomanX_irx}, {fileXio_irx, &size_fileXio_irx}, {sio2man_irx, &size_sio2man_irx},
		{freepad_irx, &size_freepad_irx}, {bdm_irx, &size_bdm_irx}, {bdmfs_fatfs_irx, &size_bdmfs_fatfs_irx},
		{usbd_mini_irx, &size_usbd_mini_irx}, {usbmass_bd_mini_irx, &size_usbmass_bd_mini_irx}};
	for (unsigned i = 0; i < sizeof(mods) / sizeof(mods[0]); i++) {
		int iopret = 0;
		if (SifExecModuleBuffer(mods[i].irx, *mods[i].size, 0, NULL, &iopret) < 0 || iopret == 1)
			return 0;
		if (mods[i].irx == fileXio_irx)
			fileXioInit();
	}
	padInit(0);
	padPortOpen(0, 0, pad_buf);
	for (int attempt = 0; attempt < 10; attempt++) { // nhddl src/devices_bdm.c:100-115
		DIR *d = opendir("mass0:/");
		if (d) { closedir(d); return 1; }
		sleep(1);
	}
	return 0;
}

unsigned pad_buttons(void) // pressed = 1 (buttons are active-low, nhddl src/pad.c:35)
{
	struct padButtonStatus b;
	return padRead(0, 0, &b) != 0 ? (0xffff ^ b.btns) & 0xffff : 0;
}
