// Self-update (#15): the release's update.txt lists ORBIT's own files, each a loose release asset:
//   version = v1.1
//   file = <asset> <path on the USB> <size> <sha256 hex>
// update.sig is an ECDSA P-256 signature (DER, over SHA-256) of update.txt, made with the release key on the
// game server (tools/make_release.py). The launcher's TLS checks no certificate (net.c), so nothing installs unless
// this signature verifies against the public key below and every file matches its size and hash.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"

static int vnum(const char *v, int *n) // "v1.0.2" -> {1, 0, 2}; how many numbers
{
	int k = 0;
	if (*v == 'v' || *v == 'V') v++;
	while (k < 4 && *v >= '0' && *v <= '9') {
		n[k++] = (int)strtol(v, (char **)&v, 10);
		if (*v != '.') break;
		v++;
	}
	return k;
}

int upd_newer(const char *a, const char *b)
{
	int x[4] = {0}, y[4] = {0};
	if (!vnum(a, x) || !vnum(b, y)) return 0;
	for (int k = 0; k < 4; k++)
		if (x[k] != y[k]) return x[k] > y[k];
	return 0;
}

int upd_parse(const char *t, upd_manifest *m)
{
	memset(m, 0, sizeof(*m));
	while (*t) {
		const char *e = t + strcspn(t, "\r\n");
		char line[200];
		int n = e - t < (int)sizeof(line) - 1 ? (int)(e - t) : (int)sizeof(line) - 1;
		memcpy(line, t, n), line[n] = 0;
		t = *e ? e + 1 : e;
		upd_file *f = &m->f[m->n];
		if (sscanf(line, "version = %15s", m->version) == 1) continue;
		if (m->n < UPD_MAXF && sscanf(line, "file = %31s %63s %d %64s", f->asset, f->dest, &f->size, f->sha) == 4) {
			if (f->size <= 0 || strlen(f->sha) != 64 || strstr(f->dest, "..") || strchr(f->asset, '/')) return 0;
			m->n++;
		}
	}
	return m->version[0] == 'v' && m->n > 0;
}

void upd_hex(const unsigned char *h, char *out) // 32 bytes -> 64 hex digits
{
	for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

#ifndef SELFTEST
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/sha256.h>

static const unsigned char release_key[65] = { // X9.63 public key of nuld:~/.orbit/update-key.pem (2026-10-06)
	0x04, 0x10, 0xce, 0xd5, 0xa1, 0x3f, 0x5c, 0x2d, 0xaf, 0x6c, 0x14, 0x56, 0x31, 0x0e, 0x76, 0x98, 0x3e, 0x1d, 0xfd, 0xc5,
	0x53, 0xe5, 0x90, 0xb3, 0x03, 0x67, 0x2c, 0x89, 0x25, 0x46, 0xf0, 0xae, 0x04, 0x02, 0xa1, 0xe1, 0x4a, 0xdd, 0xa8, 0xf4,
	0x1f, 0xda, 0xd1, 0x36, 0x9d, 0xc1, 0x7c, 0x3a, 0x13, 0xe2, 0xf8, 0xf3, 0xfe, 0x0f, 0xb9, 0x3f, 0x85, 0x01, 0x9f, 0xd3,
	0x60, 0xc2, 0x39, 0x66, 0x41};

int upd_verify(const char *text, int len, const unsigned char *sig, int siglen)
{
	unsigned char h[32];
	ecc_key key;
	int ok = 0;
	if (wc_Sha256Hash((const unsigned char *)text, len, h) != 0 || wc_ecc_init(&key) != 0) return 0;
	if (wc_ecc_import_x963(release_key, sizeof(release_key), &key) == 0 &&
	    wc_ecc_verify_hash(sig, siglen, h, sizeof(h), &ok, &key) != 0)
		ok = 0;
	wc_ecc_free(&key);
	return ok == 1;
}

void upd_sha256(const void *data, int len, char *hex)
{
	unsigned char h[32];
	wc_Sha256Hash(data, len, h);
	upd_hex(h, hex);
}
#endif

#ifdef SELFTEST
#include <assert.h>
int main(void)
{
	assert(upd_newer("v1.1", "v1.0.2") && upd_newer("v1.0.10", "v1.0.9") && upd_newer("v2", "v1.9.9"));
	assert(!upd_newer("v1.0.2", "v1.0.2") && !upd_newer("v1.0", "v1.0.0") && !upd_newer("v1.0.2", "v1.1"));
	assert(!upd_newer("junk", "v1.0"));
	upd_manifest m;
	const char *ok = "version = v1.1\r\nfile = launcher.elf launcher.elf 3093364 "
	                 "88d0391f0b9e8c4edd72ff5d30630e21e8e2c86ee20578aa45323c3c0cdd07d6\r\n"
	                 "file = ee_core.elf neutrino/modules/ee_core.elf 36192 "
	                 "cdc7c1a0449b9a4a9a6a6e5dc04cd95b65d76c9822e514ed01e8e7a7b5fa9e99\n";
	assert(upd_parse(ok, &m) && m.n == 2 && !strcmp(m.version, "v1.1"));
	assert(!strcmp(m.f[1].dest, "neutrino/modules/ee_core.elf") && m.f[1].size == 36192);
	assert(!upd_parse("version = v1.1\nfile = x ../evil.elf 10 "
	                  "88d0391f0b9e8c4edd72ff5d30630e21e8e2c86ee20578aa45323c3c0cdd07d6\n", &m)); // no way out of the home
	assert(!upd_parse("version = v1.1\n", &m)); // no files
	puts("update selftest ok");
	return 0;
}
#endif
