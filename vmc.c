// Virtual memory cards (phase 14). Layout of a fresh 8 MB card as OPL's genvmc writes it (modules/vmc/genvmc/genvmc.c,
// reference only, docs/sources.md): 1 KB clusters of 2 pages, 16-page blocks, 8192 clusters; superblock in cluster 0,
// the one indirect FAT cluster at 8 (blocksize / 2), 32 FAT clusters at 9..40, root directory at 41 = alloc_offset;
// the last two blocks are the backup blocks, everything unused is erased (0xFF). Neutrino's mc_emu computes the ECC
// itself, so the image has none (8 388 608 bytes).
// Reading: superblock -> ifc_list -> FAT chains; directory entries are 512 bytes, counted by the directory's entry in
// its parent (the root's by its own ".").
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include "vmc.h"

#define CSIZE 1024
#define NCL (VMC_SIZE / CSIZE) // 8192 clusters
#define PER (CSIZE / 4)        // FAT entries per cluster
#define IFC 8
#define FAT0 9
#define FATN 32                // (NCL * 4 - 1) / CSIZE + 1
#define ALLOC (FAT0 + FATN)    // 41
#define ALLOC_END (((NCL / 8) - 2) * 8 - ALLOC) // 8135: up to the backup blocks

static void put16(unsigned char *p, unsigned v) { p[0] = v, p[1] = v >> 8; }
static void put32(unsigned char *p, unsigned v) { p[0] = v, p[1] = v >> 8, p[2] = v >> 16, p[3] = v >> 24; }
static unsigned get32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned get16(const unsigned char *p) { return p[0] | p[1] << 8; }

static void now(unsigned char *t) // sceMcStDateTime from the C clock (cosmetic: the root directory's dates)
{
	time_t s = time(NULL);
	struct tm *tm = gmtime(&s);
	memset(t, 0, 8);
	if (!tm) { t[4] = 1, t[5] = 1, put16(t + 6, 2000); return; }
	t[1] = tm->tm_sec, t[2] = tm->tm_min, t[3] = tm->tm_hour, t[4] = tm->tm_mday, t[5] = tm->tm_mon + 1;
	put16(t + 6, tm->tm_year + 1900);
}

static void dir_entry(unsigned char *e, unsigned mode, const char *name, const unsigned char *t)
{
	memset(e, 0, 512);
	put16(e, mode);
	put32(e + 4, 2);   // length: the root holds "." and ".."
	memcpy(e + 8, t, 8);  // created
	memcpy(e + 24, t, 8); // modified; cluster 0, dir_entry 0
	strcpy((char *)e + 64, name);
}

static void cluster(unsigned c, unsigned char *b, const unsigned char *t)
{
	memset(b, 0xFF, CSIZE);
	if (c == 0) { // superblock (genvmc MCDevInfo, 384 bytes)
		memset(b, 0, 384);
		memcpy(b, "Sony PS2 Memory Card Format 1.2.0.0", 35);
		put16(b + 0x28, 512), put16(b + 0x2A, 2), put16(b + 0x2C, 16);
		put32(b + 0x30, NCL), put32(b + 0x34, ALLOC), put32(b + 0x38, ALLOC_END), put32(b + 0x3C, 0);
		put32(b + 0x40, NCL / 8 - 1), put32(b + 0x44, NCL / 8 - 2);
		for (int i = 0; i < 32; i++) put32(b + 0x50 + 4 * i, i ? 0xFFFFFFFF : IFC), put32(b + 0xD0 + 4 * i, 0xFFFFFFFF);
		b[0x150] = 2, b[0x151] = 0x2B; // PS2 card; the flags genvmc and Neutrino's mc_emu report
		put32(b + 0x154, CSIZE), put32(b + 0x158, PER), put32(b + 0x15C, 8), put32(b + 0x160, 1);
		put32(b + 0x170, NCL / 1000 * 1000 + 1); // max_allocatable_clusters as genvmc computes it (8001)
		put32(b + 0x17C, 0xFFFFFFFF);
	} else if (c == IFC) {
		memset(b, 0, CSIZE);
		for (int i = 0; i < FATN; i++) put32(b + 4 * i, FAT0 + i);
	} else if (c >= FAT0 && c < FAT0 + FATN) {
		memset(b, 0, CSIZE);
		for (int i = 0; i < PER; i++) {
			unsigned e = (c - FAT0) * PER + i;
			if (e < ALLOC_END) put32(b + 4 * i, e ? 0x7FFFFFFF : 0xFFFFFFFF); // free; the root's one cluster ends
		}
	} else if (c == ALLOC) {
		dir_entry(b, 0x8427, ".", t);       // exists | 0400 | subdir | rwx
		dir_entry(b + 512, 0xA426, "..", t); // exists | hidden | 0400 | subdir | wx
	}
}

int vmc_create(const char *path)
{
	FILE *f = fopen(path, "wb");
	unsigned char t[8], *buf = malloc(32 * CSIZE);
	int ok = f && buf;
	now(t);
	for (unsigned c = 0; ok && c < NCL; c += 32) { // 32 KB writes
		for (unsigned k = 0; k < 32; k++) cluster(c + k, buf + k * CSIZE, t);
		ok = fwrite(buf, 32 * CSIZE, 1, f) == 1;
	}
	if (f) ok &= fclose(f) == 0;
	free(buf);
	if (!ok) remove(path); // a short card would be served to the game as is
	return ok;
}

typedef struct { FILE *f; unsigned csize, alloc, ifc[32], fat_cl, fat_ok; unsigned char b[CSIZE], fat[CSIZE]; } card;

static int rd(card *v, unsigned c, unsigned char *b) // absolute cluster
{
	return fseek(v->f, (long)c * v->csize, SEEK_SET) == 0 && fread(b, v->csize, 1, v->f) == 1;
}

static int open_card(card *v, const char *path)
{
	unsigned char s[512];
	v->f = fopen(path, "rb");
	v->fat_ok = 0;
	if (!v->f) return 0;
	if (fread(s, 512, 1, v->f) != 1 || memcmp(s, "Sony PS2 Memory Card Format ", 28) || get16(s + 0x28) != 512 ||
	    get16(s + 0x2A) != 2 || get32(s + 0x154) != CSIZE) { fclose(v->f), v->f = NULL; return 0; } // ponytail: 1 KB clusters only
	v->csize = CSIZE, v->alloc = get32(s + 0x34);
	for (int i = 0; i < 32; i++) v->ifc[i] = get32(s + 0x50 + 4 * i);
	return 1;
}

static unsigned next(card *v, unsigned rel) // FAT entry of a relative cluster: bit 31 set + next, 0xFFFFFFFF = end
{
	unsigned per = v->csize / 4, idx = rel / per;
	if (idx / per >= 32 || !rd(v, v->ifc[idx / per], v->b)) return 0xFFFFFFFF;
	unsigned fc = get32(v->b + 4 * (idx % per));
	if (!v->fat_ok || v->fat_cl != fc) {
		if (!rd(v, fc, v->fat)) return 0xFFFFFFFF;
		v->fat_cl = fc, v->fat_ok = 1;
	}
	unsigned e = get32(v->fat + 4 * (rel % per));
	return e == 0xFFFFFFFF || !(e & 0x80000000) ? 0xFFFFFFFF : e & 0x7FFFFFFF;
}

// entry k of the directory starting at cluster rel (2 entries a cluster); 0 if the chain ends first
static int dir_get(card *v, unsigned rel, unsigned k, unsigned char *e)
{
	for (unsigned n = k / 2; n; n--)
		if ((rel = next(v, rel)) == 0xFFFFFFFF) return 0;
	if (!rd(v, v->alloc + rel, v->b)) return 0;
	memcpy(e, v->b + (k & 1) * 512, 512);
	return 1;
}

static void to_ent(const unsigned char *e, vmc_ent *o)
{
	memcpy(o->name, e + 64, 32);
	o->name[32] = 0;
	o->mode = get16(e);
	memcpy(o->modified, e + 24, 8);
	o->cluster = get32(e + 16), o->length = get32(e + 4);
}

// entries of a directory, without . and ..; count: from the parent's entry (a subdirectory's own "." says 0), or 0
// for the root, whose "." holds it
static int list(card *v, unsigned rel, unsigned count, vmc_ent *out, int max)
{
	unsigned char e[512];
	int n = 0;
	if (!dir_get(v, rel, 0, e)) return -1;
	if (!count) count = get32(e + 4);
	for (unsigned k = 2; k < count && n < max; k++) {
		if (!dir_get(v, rel, k, e)) break;
		if (get16(e) & 0x8000) to_ent(e, &out[n++]); // deleted entries lose the "exists" bit
	}
	return n;
}

int vmc_list(const char *path, vmc_ent *out, int max)
{
	card *v = malloc(sizeof(card));
	int n = -1;
	if (v && open_card(v, path)) n = list(v, 0, 0, out, max), fclose(v->f);
	free(v);
	return n;
}

void *vmc_read(const char *path, const char *file, int *size)
{
	char dir[33];
	const char *slash = strchr(file, '/');
	card *v = malloc(sizeof(card));
	vmc_ent *e = malloc(sizeof(vmc_ent) * 64);
	unsigned char *data = NULL;
	if (!v || !e || !slash || slash - file > 32 || !open_card(v, path)) goto out;
	snprintf(dir, sizeof(dir), "%.*s", (int)(slash - file), file);
	int n = list(v, 0, 0, e, 64), i, d = -1, f = -1;
	for (i = 0; i < n; i++)
		if (e[i].mode & 0x20 && !strcmp(e[i].name, dir)) d = i;
	if (d < 0) goto close;
	n = list(v, e[d].cluster, e[d].length, e, 64);
	for (i = 0; i < n; i++)
		if (!(e[i].mode & 0x20) && !strcmp(e[i].name, slash + 1)) f = i;
	if (f < 0 || !e[f].length || e[f].length > (2u << 20)) goto close;
	data = memalign(64, (e[f].length + CSIZE + 63) & ~63);
	unsigned rel = e[f].cluster, got = 0;
	while (data && got < e[f].length) {
		if (!rd(v, v->alloc + rel, data + got)) { free(data), data = NULL; break; } // reads a whole cluster
		got += v->csize;
		if (got < e[f].length && (rel = next(v, rel)) == 0xFFFFFFFF) { free(data), data = NULL; }
	}
	if (data) *size = e[f].length;
close:
	fclose(v->f);
out:
	free(v), free(e);
	return data;
}

#ifdef SELFTEST // host check: `make test`; VMC_CHECK=<card.bin> also lists a card made elsewhere (mymcplus, PCSX2)
#include <assert.h>
int main(int argc, char **argv)
{
	const char *p = "/tmp/vmc_selftest.bin";
	assert(vmc_create(p));
	FILE *f = fopen(p, "rb");
	fseek(f, 0, SEEK_END);
	assert(ftell(f) == VMC_SIZE);
	fclose(f);
	vmc_ent e[8];
	assert(vmc_list(p, e, 8) == 0); // formatted and empty
	assert(vmc_list("/tmp/does-not-exist.bin", e, 8) == -1);
	assert(!vmc_read(p, "BASLUS-21376/icon.sys", &argc));
	if (argc > 1) {
		vmc_ent big[64];
		int n = vmc_list(argv[1], big, 64);
		printf("%s: %d entries\n", argv[1], n);
		for (int i = 0; i < n; i++) {
			printf("  %s mode %04x length %u %02d/%02d/%04d\n", big[i].name, big[i].mode, big[i].length, big[i].modified[4],
			       big[i].modified[5], get16(big[i].modified + 6));
			if (argc > 2 && big[i].mode & 0x20) {
				char path[80];
				int sz = 0;
				snprintf(path, sizeof(path), "%s/%s", big[i].name, argv[2]);
				void *d = vmc_read(argv[1], path, &sz);
				printf("    %s: %d bytes%s\n", path, d ? sz : -1, d && sz > 4 && !memcmp(d, "PS2D", 4) ? " (PS2D icon.sys)" : "");
				free(d);
			}
		}
	}
	puts("vmc selftest ok");
	return 0;
}
#endif
