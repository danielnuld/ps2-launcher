#pragma once
// FreeMcBoot 1.966 config text (#15): text is the whole file, NUL-terminated, in a buffer of max bytes
const char *cnf_get(const char *text, const char *key, char *out, int n); // the value, or NULL if the key is missing
int cnf_set(char *text, int max, const char *key, const char *val);       // replaces or appends; 0 if it does not fit
int cnf_menu_add(char *text, int max, const char *name, const char *path); // OSDSYS menu item number, 0 if no room
