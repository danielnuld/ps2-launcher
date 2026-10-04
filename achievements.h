#pragma once
#include "iso.h"
// RetroAchievements (phase 16, step 1)
void ra_exe_name(const char *boot2, char *out, int n); // "cdrom0:\SLUS_209.46;1" -> "SLUS_209.46", as rcheevos
// rcheevos rc_hash_ps2: MD5 of the BOOT2 executable's name, then its bytes. out: 33 bytes, lowercase hex; 1 if done
int ra_hash(iso_read_fn read_at, void *ctx, char *out, unsigned *exe_size);

extern char ra_me[16], ra_srv_ip[16]; // after a query: the PS2's IP and the client's
extern char ra_note[96];     // what the client discovery found, for the log
extern volatile int ra_abort; // set before running another program: ra_hash and ra_query give up within 1 s

enum { RA_SET = 2, RA_NOSET, RA_NOSERVER, RA_FAIL }; // ra_query results (0, 1: the launcher's not asked / working)
// asks the client (xerabora, UDP 18194) about a hash: RA_SET with the achievement count and the set's title. The
// first call finds the client (server "" = broadcast); after RA_NOSERVER the client is never asked again
int ra_query(const char *server, const char *hash, const char *serial, int *count, char *title, int tn);

// a game's achievements as the client's page lists them (GET /game?id=<RA id>), for the launcher's Logros panel
typedef struct { char title[64], desc[128]; short points; char earned; } ra_ach;
int ra_parse_game(const char *json, int n, ra_ach *out, int max); // achievements read (UTF-8 text), 0 if none
int ra_game_id(const char *hash);                    // RetroAchievements' id for the hash (HTTPS), <= 0 if unknown
int ra_game_list(int id, ra_ach *out, int max);      // from the client's page (port 18280); < 0 on error

#define RA_CHUNK 896 // bytes of watch list per RAC1 (xeRAbora client protocol.h XERABORA_CHUNK)
// the game's watch list from the client (RAQ1, then RAG1 per chunk): its length, < 0 if none or incomplete
int ra_watchlist(const char *server, const char *hash, const char *serial, unsigned char *out, int max);
int ra_wl_check(const unsigned char *list, int n); // entries in a valid watch list file (ra_watch.h), else 0
