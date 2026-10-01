#pragma once
#include <stdio.h>
typedef int (*iso_read_fn)(void *ctx, unsigned lba, void *buf, unsigned n); // read n bytes at sector lba; 1 = ok
int iso_serial(iso_read_fn read_at, void *ctx, char *out); // "SLUS_209.46" from SYSTEM.CNF BOOT2; 1 if found (PS2 only)
int name_serial(const char *file, char *out); // OPL "SLUS_209.46.Title.iso" prefix; 1 if present
void serial_dash(const char *in, char *out);        // "SLUS_209.46" -> "SLUS-20946"
void iso_title(const char *file, char *out, int n); // file name without OPL serial prefix and .iso
