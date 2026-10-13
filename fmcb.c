// FreeMcBoot 1.966's mc?:/SYS-CONF/FREEMCB.CNF (#15, setup wizard): "key = value" lines, the launch keys
// LK_<button>_E1..E3 and the OSDSYS menu items name_/path1_OSDSYS_ITEM_<n> (n 1-100; FMCB's own use 98-100).
// Format from the stock 1.966 file (israpps/FreeMcBoot-Installer installer_res/1966). Edits keep every other line
// as it is, and new lines take the file's own line ending.
#include <stdio.h>
#include <string.h>
#include "fmcb.h"

static char *find_key(char *text, const char *key) // start of the line "key = ...", or NULL
{
	size_t k = strlen(key);
	for (char *l = text; *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : l + strlen(l)) {
		char *p = l;
		while (*p == ' ' || *p == '\t') p++;
		if (strncmp(p, key, k)) continue;
		p += k;
		while (*p == ' ' || *p == '\t') p++;
		if (*p == '=') return l;
	}
	return NULL;
}

const char *cnf_get(const char *text, const char *key, char *out, int n)
{
	char *l = find_key((char *)text, key);
	if (!l || n < 1) return NULL;
	char *p = strchr(l, '=') + 1;
	while (*p == ' ' || *p == '\t') p++;
	int i = 0;
	while (p[i] && p[i] != '\r' && p[i] != '\n' && i < n - 1) out[i] = p[i], i++;
	while (i > 0 && (out[i - 1] == ' ' || out[i - 1] == '\t')) i--;
	out[i] = 0;
	return out;
}

int cnf_set(char *text, int max, const char *key, const char *val)
{
	const char *eol = strstr(text, "\r\n") ? "\r\n" : "\n";
	char line[160];
	int ln = snprintf(line, sizeof(line), "%s = %s", key, val), len = strlen(text);
	if (ln >= (int)sizeof(line)) return 0;
	char *l = find_key(text, key);
	if (l) { // replace up to the line's end (its line ending stays)
		char *e = l + strcspn(l, "\r\n");
		if (len - (e - l) + ln >= max) return 0;
		memmove(l + ln, e, strlen(e) + 1);
		memcpy(l, line, ln);
		return 1;
	}
	int add = (len && text[len - 1] != '\n' ? (int)strlen(eol) : 0) + ln + (int)strlen(eol);
	if (len + add >= max) return 0;
	if (len && text[len - 1] != '\n') strcat(text, eol);
	strcat(text, line), strcat(text, eol);
	return 1;
}

int cnf_menu_add(char *text, int max, const char *name, const char *path)
{
	char key[32], v[128];
	int free_n = 0;
	for (int n = 1; n <= 97; n++) {
		snprintf(key, sizeof(key), "path1_OSDSYS_ITEM_%d", n);
		if (cnf_get(text, key, v, sizeof(v)) && !strcmp(v, path)) return n; // already there
		snprintf(key, sizeof(key), "name_OSDSYS_ITEM_%d", n);
		if (!free_n && !cnf_get(text, key, v, sizeof(v))) free_n = n;
	}
	if (!free_n) return 0;
	snprintf(key, sizeof(key), "name_OSDSYS_ITEM_%d", free_n);
	if (!cnf_set(text, max, key, name)) return 0;
	snprintf(key, sizeof(key), "path1_OSDSYS_ITEM_%d", free_n);
	return cnf_set(text, max, key, path) ? free_n : 0;
}

#ifdef SELFTEST
#include <assert.h>
int main(void)
{
	char t[512] = "# FMCB\nCNF_version = 1\nLK_Auto_E1 = OSDSYS\nLK_R1_E1 = mass:/BOOT/BOOT.ELF\n"
	              "name_OSDSYS_ITEM_1 = wLaunchELF\npath1_OSDSYS_ITEM_1 = mass:/BOOT/BOOT.ELF\n"
	              "name_OSDSYS_ITEM_3 = OPL\nname_OSDSYS_ITEM_98 = Configurator\n", v[64];
	assert(!strcmp(cnf_get(t, "LK_Auto_E1", v, sizeof(v)), "OSDSYS"));
	assert(!cnf_get(t, "LK_Auto", v, sizeof(v)));                 // a prefix is not the key
	assert(cnf_set(t, sizeof(t), "LK_Auto_E1", "mc?:/BOOT/ORBIT.ELF"));
	assert(!strcmp(cnf_get(t, "LK_Auto_E1", v, sizeof(v)), "mc?:/BOOT/ORBIT.ELF"));
	assert(strstr(t, "LK_R1_E1 = mass:/BOOT/BOOT.ELF\n"));        // the rest untouched
	assert(cnf_set(t, sizeof(t), "LK_L1_E1", "mc?:/BOOT/ORBIT.ELF")); // a new key goes at the end
	assert(!strcmp(cnf_get(t, "LK_L1_E1", v, sizeof(v)), "mc?:/BOOT/ORBIT.ELF"));
	assert(cnf_menu_add(t, sizeof(t), "ORBIT", "mc?:/BOOT/ORBIT.ELF") == 2); // the first free number
	assert(cnf_menu_add(t, sizeof(t), "ORBIT", "mc?:/BOOT/ORBIT.ELF") == 2); // once only
	assert(!strcmp(cnf_get(t, "name_OSDSYS_ITEM_2", v, sizeof(v)), "ORBIT"));
	char w[128] = "A = 1\r\nB = 2\r\n";                            // CRLF kept, also for new lines
	assert(cnf_set(w, sizeof(w), "A", "10") && cnf_set(w, sizeof(w), "C", "3"));
	assert(!strcmp(w, "A = 10\r\nB = 2\r\nC = 3\r\n"));
	char s[24] = "A = 1\n";
	assert(!cnf_set(s, sizeof(s), "LONGER_KEY", "a long value")); // never cut
	assert(!strcmp(s, "A = 1\n"));
	puts("fmcb selftest ok");
	return 0;
}
#endif
