// orbit_hash: the launcher's own ISO and memory-card code, run on the game server by tools/orbit_catalog.py
// (phase 17), so a game listed from the catalog has the same serial and RetroAchievements hash as one read by the
// launcher, and its virtual memory card the same format.
//   orbit_hash hash <iso>   prints "<serial>\t<hash>" (SLUS_213.76 form; "-" for what cannot be read), exit 1 if none
//   orbit_hash vmc <file>   writes an empty formatted 8 MB card (the caller checks it does not exist)
// Built static in WSL (nuld has no compiler): make orbit_hash
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include "../iso.h"
#include "../achievements.h"
#include "../vmc.h"

static int rd(void *f, unsigned lba, void *buf, unsigned n)
{
	return fseeko(f, (off_t)lba * 2048, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "vmc")) return vmc_create(argv[2]) ? 0 : 1;
	if (argc != 3 || strcmp(argv[1], "hash")) {
		fprintf(stderr, "usage: orbit_hash hash <iso> | vmc <file>\n");
		return 2;
	}
	FILE *f = fopen(argv[2], "rb");
	char serial[16] = "-", hash[33] = "-";
	unsigned exe;
	if (!f) return 1;
	int ok = iso_serial(rd, f, 0, serial);
	if (!ok) strcpy(serial, "-");
	ok |= ra_hash(rd, f, hash, &exe);
	fclose(f);
	printf("%s\t%s\n", serial, hash);
	return ok ? 0 : 1;
}
