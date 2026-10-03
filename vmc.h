#pragma once
// Virtual memory cards (phase 14): 8 MB PS2 memory card images without ECC, the format Neutrino's mc_emu serves
// (512-byte pages, 16-page blocks; OPL genvmc layout). Plain stdio, so any device path works (mass0:, mmce0:, ...).
#define VMC_SIZE (8 << 20)
typedef struct {
	char name[33];
	unsigned short mode;      // MC_ATTR_* bits (0x20 subdir, 0x8000 exists), as libmc's AttrFile
	unsigned char modified[8]; // sceMcStDateTime layout: resv, sec, min, hour, day, month, year (u16 LE)
	unsigned cluster, length;  // first cluster (relative to alloc_offset); entries (dirs) or bytes (files)
} vmc_ent;

int vmc_create(const char *path);                          // formatted, empty card; 1 if written whole
int vmc_list(const char *path, vmc_ent *out, int max);     // root entries without . and ..; -1 missing / not a card
void *vmc_read(const char *path, const char *file, int *size); // "DIR/name", malloc'd (64-aligned); NULL if missing
