#pragma once
#include <stdio.h>
typedef int (*iso_read_fn)(void *ctx, unsigned lba, void *buf, unsigned n); // read n bytes at sector lba; 1 = ok
// a file by its path ("SYSTEM.CNF", "DATA\MAIN.ELF"; case-insensitive, ";1" optional): extent sector and size; 1 if found
int iso_find(iso_read_fn read_at, void *ctx, const char *path, unsigned *lba, unsigned *size);
int iso_cnf(iso_read_fn read_at, void *ctx, char *out, unsigned max); // SYSTEM.CNF text, NUL-terminated, < max bytes
int iso_serial(iso_read_fn read_at, void *ctx, int ps1, char *out); // "SLUS_209.46" from SYSTEM.CNF BOOT2 (BOOT if ps1)
// SYSTEM.CNF text -> boot file name ("SLUS_209.46", up to 15 chars) and, if raw, the whole path; 1 if found
int cnf_boot(const char *cnf, int ps1, char *file, char *raw, int raw_n);
int name_serial(const char *file, char *out); // OPL "SLUS_209.46.Title.iso" prefix; 1 if present
void serial_dash(const char *in, char *out);        // "SLUS_209.46" -> "SLUS-20946"
void iso_title(const char *file, char *out, int n); // file name without OPL serial prefix and .iso/.vcd/.elf
// the game server's catalog (phase 17, tools/orbit_catalog.py): "path\tsize\tserial\thash\ttitle\n" per ISO. Cuts the
// buffer at tabs and newlines; e points into it. 1 per valid line, 0 at the end
typedef struct { char *path, *serial, *hash, *title; unsigned long long size; } cat_ent;
int catalog_next(char **p, cat_ent *e);
