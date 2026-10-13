#pragma once
// Self-update (#15): signed manifest of ORBIT's files (update.c)
#define UPD_MAXF 8
typedef struct { char asset[32], dest[64], sha[65]; int size; } upd_file; // dest: relative to the home device
typedef struct { char version[16]; upd_file f[UPD_MAXF]; int n; } upd_manifest;

int upd_newer(const char *a, const char *b);                // 1 if version a ("v1.1") is newer than b ("v1.0.2")
int upd_parse(const char *text, upd_manifest *m);           // 1 if it names a version and at least one file
int upd_verify(const char *text, int len, const unsigned char *sig, int siglen); // the release key signed text
void upd_sha256(const void *data, int len, char *hex);      // 64 hex digits + NUL
void upd_hex(const unsigned char *h, char *out);
