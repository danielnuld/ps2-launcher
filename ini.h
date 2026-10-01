#pragma once
#define INI_MAX 512
#define INI_SEC 24
#define INI_KEY 24
#define INI_VAL 64
typedef struct { char sec[INI_SEC], key[INI_KEY], val[INI_VAL]; } ini_kv;
typedef struct { ini_kv kv[INI_MAX]; int n; } ini;

int ini_load(ini *d, const char *path);                 // 1 if the file existed (d is empty otherwise)
void ini_parse(ini *d, const char *text);
const char *ini_get(const ini *d, const char *sec, const char *key, const char *def); // names case-insensitive
void ini_set(ini *d, const char *sec, const char *key, const char *val);              // val NULL removes
int ini_save(const ini *d, const char *path, const char *header);                     // header may be NULL
