#pragma once
#include "iso.h"
// RetroAchievements (phase 16, step 1)
void ra_exe_name(const char *boot2, char *out, int n); // "cdrom0:\SLUS_209.46;1" -> "SLUS_209.46", as rcheevos
// rcheevos rc_hash_ps2: MD5 of the BOOT2 executable's name, then its bytes. out: 33 bytes, lowercase hex; 1 if done
int ra_hash(iso_read_fn read_at, void *ctx, char *out, unsigned *exe_size);

extern char ra_note[96];     // what the client discovery found, for the log
extern volatile int ra_abort; // set before running another program: ra_hash and ra_query give up within 1 s

enum { RA_SET = 2, RA_NOSET, RA_NOSERVER, RA_FAIL }; // ra_query results (0, 1: the launcher's not asked / working)
// asks the client (xerabora, UDP 18194) about a hash: RA_SET with the achievement count and the set's title. The
// first call finds the client (server "" = broadcast); after RA_NOSERVER the client is never asked again
int ra_query(const char *server, const char *hash, const char *serial, int *count, char *title, int tn);
