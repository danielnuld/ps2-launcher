// Tiny INI files (phase 7): "[section]", "key = value", ';' or '#' comments. config.ini is only ever read (and
// created from a commented template); juegos.ini is rewritten from memory, so its comments are not kept.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "ini.h"

static void trim(char *s)
{
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
	char *b = s;
	while (isspace((unsigned char)*b)) b++;
	memmove(s, b, strlen(b) + 1);
}

static void copy(char *dst, const char *src, int n) // bounded; longer names/values are cut on purpose
{
	int i = 0;
	for (; i < n - 1 && src[i]; i++) dst[i] = src[i];
	dst[i] = 0;
}

void ini_parse(ini *d, const char *text)
{
	char sec[INI_SEC] = "", line[160];
	d->n = 0;
	while (*text) {
		int n = strcspn(text, "\r\n");
		snprintf(line, sizeof(line), "%.*s", n, text);
		text += n;
		while (*text == '\r' || *text == '\n') text++;
		char *c = strpbrk(line, ";#");
		if (c) *c = 0;
		trim(line);
		if (line[0] == '[') {
			char *e = strchr(line, ']');
			if (e) *e = 0;
			copy(sec, line + 1, sizeof(sec));
			trim(sec);
		} else if (strchr(line, '=')) {
			char *eq = strchr(line, '=');
			*eq = 0;
			trim(line);
			trim(eq + 1);
			ini_set(d, sec, line, eq + 1);
		}
	}
}

int ini_load(ini *d, const char *path)
{
	static char buf[16384];
	FILE *f = fopen(path, "rb");
	d->n = 0;
	if (!f) return 0;
	int n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n > 0 ? n : 0] = 0;
	ini_parse(d, buf);
	return 1;
}

static int find(const ini *d, const char *sec, const char *key)
{
	for (int i = 0; i < d->n; i++)
		if (!strcasecmp(d->kv[i].sec, sec) && !strcasecmp(d->kv[i].key, key)) return i;
	return -1;
}

const char *ini_get(const ini *d, const char *sec, const char *key, const char *def)
{
	int i = find(d, sec, key);
	return i < 0 ? def : d->kv[i].val;
}

void ini_set(ini *d, const char *sec, const char *key, const char *val) // val NULL removes the key
{
	int i = find(d, sec, key);
	if (!val) {
		if (i >= 0) d->kv[i] = d->kv[--d->n];
		return;
	}
	if (i < 0) {
		if (d->n >= INI_MAX) return;
		i = d->n++;
		copy(d->kv[i].sec, sec, INI_SEC);
		copy(d->kv[i].key, key, INI_KEY);
	}
	copy(d->kv[i].val, val, INI_VAL);
}

int ini_save(const ini *d, const char *path, const char *header) // sections in first-seen order
{
	FILE *f = fopen(path, "wb");
	if (!f) return 0;
	if (header) fputs(header, f);
	for (int i = 0; i < d->n; i++) {
		int first = 1;
		for (int j = 0; j < i; j++) first &= !!strcasecmp(d->kv[j].sec, d->kv[i].sec);
		if (!first) continue;
		fprintf(f, "\n[%s]\n", d->kv[i].sec);
		for (int j = i; j < d->n; j++)
			if (!strcasecmp(d->kv[j].sec, d->kv[i].sec)) fprintf(f, "%s = %s\n", d->kv[j].key, d->kv[j].val);
	}
	return fclose(f) == 0;
}

#ifdef SELFTEST // host check: `make test`
#include <assert.h>
int main(void)
{
	static ini d, e;
	ini_parse(&d, "; comment\r\n[video]\r\nmodo = 1080i ; trailing\n\n[ juegos ]\norigen=usb\n[SLUS-21376]\ngc = 23\n");
	assert(d.n == 3);
	assert(!strcmp(ini_get(&d, "video", "modo", "480p"), "1080i"));
	assert(!strcmp(ini_get(&d, "JUEGOS", "Origen", "x"), "usb"));       // case-insensitive names
	assert(!strcmp(ini_get(&d, "sonido", "volumen", "100"), "100"));   // default
	ini_set(&d, "SLUS-21376", "video", "nativo");
	ini_set(&d, "SLUS-21376", "gc", NULL);                             // remove
	assert(d.n == 3 && !strcmp(ini_get(&d, "SLUS-21376", "gc", "-"), "-"));
	assert(ini_save(&d, "/tmp/orbit_ini_test.ini", "; test\n") && ini_load(&e, "/tmp/orbit_ini_test.ini"));
	assert(e.n == 3 && !strcmp(ini_get(&e, "SLUS-21376", "video", ""), "nativo"));
	puts("ini selftest ok");
	return 0;
}
#endif
