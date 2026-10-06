// Game sources (phase 14). Every block device goes through the BDM the launcher already runs for its USB
// (bdm + bdmfs_fatfs, iop.c), which mounts each one under its driver's name as well as massN: (ps2sdk bdmfs_fatfs
// fs_driver.c: bd->path "usb", "ata", "mx4sio", "ilink"; Neutrino's udpbd "udpbd"). Those names are the paths
// Neutrino takes (-dvd=ata:DVD/x.iso, README "Usage examples") and maps to its -bsd. Drivers per source as Neutrino
// v1.8.0's config/bsd-*.toml and nhddl src/devices/init.c (reference only, docs/sources.md):
//   hdd     ps2dev9 + ata_bd (exFAT/FAT disk), else ps2hdd-bdm (-o 4 -n 20) for an APA disk with HD Loader games
//   mx4sio  mx4sio_bd_mini        ilink  iLinkman + IEEE1394_bd_mini        mmce  mmceman (mmce0: / mmce1:)
//   udpbd   ps2dev9 + smap + ministack ip= + udpbd          udpfs  nothing: listed from the server's catalog (phase 17)
// The ps2sdk ones are embedded (same build as our bdm); smap / ministack / udpbd / udpfs_ioman exist only in
// Neutrino, so they are read from its modules/ folder. With -qb these stay loaded under Neutrino's load stage.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <dirent.h>
#include <unistd.h>
#include <kernel.h>
#include <loadfile.h>
#include <hdd-ioctl.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include "sources.h"
#include "net.h"
#include "lang.h"

#define IRX(m) extern unsigned char m##_irx[]; extern unsigned int size_##m##_irx
IRX(ata_bd); IRX(ps2hdd_bdm); IRX(mx4sio_bd_mini); IRX(iLinkman); IRX(IEEE1394_bd_mini); IRX(mmceman);

const char *const src_key[SRC_N] = {"usb", "hdd", "hdl", "mx4sio", "ilink", "mmce", "udpbd", "udpfs"};
const char *const src_bsd[SRC_N] = {"usb", "ata", "ata", "mx4sio", "ilink", "mmce", "udpbd", "udpfs"};
const char *const src_label[SRC_N] = {"USB", "HDD", "HDD", "MX4SIO", "ILINK", "MMCE", "UDPBD", "UDPFS"};
source src[SRC_MAX];
int nsrc;

unsigned src_mask(const char *list)
{
	unsigned m = 0;
	char w[16];
	while (*list) {
		int n = 0;
		while (*list == ',' || *list == ' ' || *list == '\t') list++;
		while (*list && *list != ',' && *list != ' ' && *list != '\t' && n < 15) w[n++] = *list++;
		w[n] = 0;
		for (int s = 0; s < SRC_N; s++)
			if (n && !strcasecmp(w, src_key[s])) m |= 1u << s;
		while (*list && *list != ',' && *list != ' ' && *list != '\t') list++; // a word longer than 15
	}
	if (m & 1u << SRC_HDL) m |= 1u << SRC_HDD; // "hdl" is the same disk driver
	return m;
}

static int load(void *irx, unsigned size, const char *args, int argl)
{
	int r = 0;
	return SifExecModuleBuffer(irx, size, argl, args, &r) >= 0 && r != 1; // 1 = NO_RESIDENT_END: did not stay
}

static int load_file(const char *ndir, const char *name, const char *args, int argl) // one of Neutrino's modules
{
	char path[96];
	snprintf(path, sizeof(path), "%s/modules/%s", ndir, name);
	FILE *f = fopen(path, "rb");
	if (!f) { printf("sources: %s missing\n", path); return 0; }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	void *b = n > 0 && n < (1 << 20) ? memalign(64, n) : NULL;
	int ok = b && fread(b, n, 1, f) == 1;
	fclose(f);
	if (ok) ok = load(b, n, args, argl);
	free(b);
	printf("sources: %s %s\n", path, ok ? "loaded" : "failed");
	return ok;
}

static int mounted(const char *root, int quarters) // waits up to quarters x 250 ms for root to open
{
	char dir[16];
	snprintf(dir, sizeof(dir), strncmp(root, "hdd", 3) ? "%s/" : "%s", root); // ps2hdd lists partitions at "hdd0:"
	for (int t = 0;; t++) {
		DIR *d = opendir(dir);
		if (d) { closedir(d); return 1; }
		if (t >= quarters) return 0;
		usleep(250000);
	}
}

static int add(int type, const char *root, int quarters)
{
	if (nsrc >= SRC_MAX || !mounted(root, quarters)) return 0;
	src[nsrc].type = type, src[nsrc].unit = root[strlen(root) - 2] - '0';
	snprintf(src[nsrc].root, sizeof(src[nsrc].root), "%s", root);
	printf("sources: %s at %s\n", src_key[type], root);
	nsrc++;
	return 1;
}

static int apa_disk(void) // sector 0 of hdd0: carries "APA" at byte 4 (nhddl hdl.c checkAPAHeader)
{
	static u8 sec[512] __attribute__((aligned(64)));
	hddAtaTransfer_t a = {0, 1};
	return fileXioDevctl("hdd0:", HDIOC_READSECTOR, &a, sizeof(a), sec, 512) >= 0 && !memcmp(sec + 4, "APA", 3);
}

// Neutrino's ministack takes its IP from the toml for the game stage: keep it equal to ours (-qb loads ours only
// for the launcher's own stage). Rewrites "ip=..." in place when it differs.
static void neutrino_ip(const char *ndir, const char *name, const char *ip)
{
	char path[96], buf[4096], out[4200];
	snprintf(path, sizeof(path), "%s/config/%s", ndir, name);
	FILE *f = fopen(path, "rb");
	if (!f) return;
	int n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	if (n <= 0 || n == sizeof(buf) - 1) return; // ponytail: Neutrino's tomls are under 1 KB; never write a cut one
	buf[n] = 0;
	char *p = strstr(buf, "\"ip="), *e = p ? strchr(p + 4, '"') : NULL;
	if (!e || ((int)strlen(ip) == e - p - 4 && !strncmp(p + 4, ip, e - p - 4))) return;
	snprintf(out, sizeof(out), "%.*s\"ip=%s%s", (int)(p - buf), buf, ip, e);
	if ((f = fopen(path, "wb"))) fputs(out, f), fclose(f), printf("sources: %s now ip=%s\n", path, ip);
}

const char *src_init(unsigned mask, const char *ip, const char *ndir)
{
	const char *why = NULL;
	if (mask & 1u << SRC_HDD && (!net_dev9() || !load(ata_bd_irx, size_ata_bd_irx, NULL, 0)))
		why = L("No se pudo cargar el driver del disco duro", "Could not load the hard disk driver");
	else if (mask & 1u << SRC_HDD) {
		if (!add(SRC_HDD, "ata0:", 12)) { // no FAT / exFAT partition: an APA disk with HD Loader games?
			static const char a[] = "-o\0" "4\0" "-n\0" "20"; // 4 descriptors, 20 buffers (Neutrino bsdfs-hdl.toml)
			if (load(ps2hdd_bdm_irx, size_ps2hdd_bdm_irx, a, sizeof(a)) && mounted("hdd0:", 8) && apa_disk())
				src[nsrc].type = SRC_HDL, src[nsrc].unit = 0, strcpy(src[nsrc].root, "hdd0:"), nsrc++;
			else why = L("No se encontró el disco duro (exFAT o HD Loader)", "Hard disk not found (exFAT or HD Loader)");
		}
	}
	if (mask & 1u << SRC_MX4SIO && mask & 1u << SRC_MMCE) // both on the SIO2 of slot 2 (nhddl README)
		mask &= ~(1u << SRC_MMCE), why = L("MX4SIO y MMCE no van juntos: se usa MX4SIO", "MX4SIO and MMCE do not go together: using MX4SIO");
	if (mask & 1u << SRC_MX4SIO && (!load(mx4sio_bd_mini_irx, size_mx4sio_bd_mini_irx, NULL, 0) || !add(SRC_MX4SIO, "mx4sio0:", 12)))
		why = L("No se encontró la tarjeta del MX4SIO", "MX4SIO card not found");
	if (mask & 1u << SRC_ILINK && (!load(iLinkman_irx, size_iLinkman_irx, NULL, 0) ||
	                               !load(IEEE1394_bd_mini_irx, size_IEEE1394_bd_mini_irx, NULL, 0) || !add(SRC_ILINK, "ilink0:", 16)))
		why = L("No se encontró el disco iLink", "iLink disk not found");
	// mmceman also when the MMCE is no game source: a MemCard PRO 2 / SD2PSX gets the game ID (#2, as nhddl)
	int mmce = !(mask & 1u << SRC_MX4SIO) && load(mmceman_irx, size_mmceman_irx, NULL, 0);
	if (mask & 1u << SRC_MMCE && mmce) {
		int any = add(SRC_MMCE, "mmce0:", 4);
		if (!(any |= add(SRC_MMCE, "mmce1:", 0))) why = L("No se encontró ningún MMCE", "No MMCE found");
	} else if (mask & 1u << SRC_MMCE) why = L("No se pudo cargar el driver del MMCE", "Could not load the MMCE driver");
	if (mask & 1u << SRC_UDPBD && (!strcasecmp(ip, "dhcp") || !*ip))
		return L("UDPBD necesita una IP fija en [red] ip", "UDPBD needs a fixed IP in [red] ip"); // ministack has no DHCP (udpfs takes the lease, src_udpfs_ip)
	if (mask & 1u << SRC_UDPBD) {
		char arg[24];
		int n = snprintf(arg, sizeof(arg), "ip=%s", ip) + 1;
		net_busy = 1; // Neutrino's smap owns the adapter from here on
		if (!net_dev9() || !load_file(ndir, "smap.irx", NULL, 0) || !load_file(ndir, "ministack.irx", arg, n))
			return L("No se cargaron smap / ministack de neutrino/modules", "smap / ministack from neutrino/modules did not load");
		neutrino_ip(ndir, "bsd-udpbd.toml", ip);
		if (!load_file(ndir, "udpbd.irx", NULL, 0) || !add(SRC_UDPBD, "udpbd0:", 24)) why = L("No respondió el servidor UDPBD", "The UDPBD server did not answer");
	}
	if (mask & 1u << SRC_UDPFS && nsrc < SRC_MAX) // phase 17: listed from the server's catalog, nothing mounted
		src[nsrc].type = SRC_UDPFS, src[nsrc].unit = 0, strcpy(src[nsrc].root, "udpfs:"), nsrc++;
	return why;
}

// phase 17: the game stage's ministack (Neutrino boots it, no -qb) takes the address the launcher has, fixed or the
// DHCP lease: ministack itself has no DHCP
void src_udpfs_ip(const char *ndir, const char *ip)
{
	if (*ip) neutrino_ip(ndir, "bsd-udpfs.toml", ip);
}

// HD Loader header (hdlfs, as nhddl hdl.c): 1 KB at 4 KB + 1 MB into the partition, magic 0xDEADFEED
int src_hdl_scan(void (*cb)(void *ctx, const char *part, const char *title, const char *startup), void *ctx)
{
	static u8 h[1024] __attribute__((aligned(64)));
	iox_dirent_t de;
	int fd = fileXioDopen("hdd0:"), n = 0;
	if (fd < 0) return 0;
	while (fileXioDread(fd, &de) > 0) {
		if (de.stat.mode != 0x1337 || de.stat.attr & APA_FLAG_SUB) continue; // HDL partitions, main ones only
		hddAtaTransfer_t a = {de.stat.private_5 + (0x100000 + 4096) / 512, 2};
		if (fileXioDevctl("hdd0:", HDIOC_READSECTOR, &a, sizeof(a), h, 1024) != 0 || *(u32 *)h != 0xDEADFEED) continue;
		h[8 + 159] = 0, h[172 + 59] = 0; // gamename[160] at 8, startup[60] at 172
		cb(ctx, de.name, (const char *)h + 8, (const char *)h + 172);
		n++;
	}
	fileXioDclose(fd);
	return n;
}

void src_mmce_game(const char *startup) // every MMCE: devctl 0x1 ping, 0x8 set game ID, poll 0x2 until not busy (nhddl mmce.c)
{
	char dev[] = "mmce0:";
	for (int u = 0; u < 2; u++) {
		dev[4] = '0' + u;
		if (fileXioDevctl(dev, 0x1, NULL, 0, NULL, 0) < 0 || fileXioDevctl(dev, 0x8, (void *)startup, strlen(startup) + 1, NULL, 0) < 0) continue;
		for (int t = 0; t < 30 && (fileXioDevctl(dev, 0x2, NULL, 0, NULL, 0) & 1); t++) usleep(500000);
	}
}
