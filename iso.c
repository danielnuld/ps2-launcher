// PS2 game ISO identification: serial from SYSTEM.CNF (ISO9660), title from the file name.
// ISO9660: primary volume descriptor at sector 16; root directory record at offset 156 (extent LBA at +2, size at
// +10, little-endian); directory records: length at +0, name length at +32, name at +33 ("SYSTEM.CNF;1").
// SYSTEM.CNF: "BOOT2 = cdrom0:\SLUS_209.46;1" (PS2; PS1 discs use BOOT and are skipped). Spec: phase-6-games.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "iso.h"

#define SECTOR 2048

static unsigned le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// SYSTEM.CNF can sit past 2 GB (Persona 4: LBA 1 761 296 = 3.6 GB) and the EE's long is 32 bits, so the caller
// supplies a 64-bit-capable reader (fileXioLseek64 on the PS2, stdio on the host)
int iso_serial(iso_read_fn read_at, void *f, char *out) // out: >= 16 bytes, "SLUS_209.46"; returns 1 if found
{
	static unsigned char buf[SECTOR * 2] __attribute__((aligned(64)));
	if (!read_at(f, 16, buf, SECTOR) || buf[0] != 1 || memcmp(buf + 1, "CD001", 5)) return 0;
	unsigned lba = le32(buf + 156 + 2), size = le32(buf + 156 + 10);
	unsigned cnf_lba = 0, cnf_size = 0;
	for (unsigned off = 0; off < size && !cnf_lba; off += SECTOR) { // root directory, one sector at a time
		if (!read_at(f, lba + off / SECTOR, buf, SECTOR)) return 0;
		for (unsigned p = 0; p < SECTOR && buf[p];) {
			unsigned len = buf[p], nl = buf[p + 32];
			if (nl >= 10 && !memcmp(buf + p + 33, "SYSTEM.CNF", 10)) {
				cnf_lba = le32(buf + p + 2), cnf_size = le32(buf + p + 10);
				break;
			}
			p += len;
		}
	}
	if (!cnf_lba) return 0;
	if (cnf_size > sizeof(buf) - 1) cnf_size = sizeof(buf) - 1;
	if (!read_at(f, cnf_lba, buf, cnf_size)) return 0;
	buf[cnf_size] = 0;
	const char *b = strstr((const char *)buf, "BOOT2");
	if (!b || !(b = strpbrk(b, "\\:"))) return 0;
	while (*b == '\\' || *b == ':' || *b == '/') b++;
	if (!strncmp(b, "cdrom0", 6)) b += 6;
	while (*b == '\\' || *b == ':') b++;
	int n = 0;
	while (b[n] && b[n] != ';' && !isspace((unsigned char)b[n]) && n < 15) n++;
	memcpy(out, b, n);
	out[n] = 0;
	return n > 0;
}

void serial_dash(const char *in, char *out) // "SLUS_209.46" -> "SLUS-20946" (cover and save names)
{
	for (; *in; in++)
		if (*in == '_') *out++ = '-';
		else if (*in != '.') *out++ = *in;
	*out = 0;
}

int name_serial(const char *file, char *out) // OPL naming "SLUS_209.46.Title.iso" -> "SLUS_209.46"; 1 if present
{
	if (strlen(file) < 12 || file[4] != '_' || file[8] != '.' || file[11] != '.') return 0;
	for (int i = 0; i < 11; i++)
		if (i != 4 && i != 8 && !isalnum((unsigned char)file[i])) return 0;
	memcpy(out, file, 11);
	out[11] = 0;
	return 1;
}

void iso_title(const char *file, char *out, int n) // "SLUS_209.46.God of War.iso" -> "God of War"
{
	const char *t = file;
	char tmp[16];
	if (name_serial(t, tmp)) t += 12; // OPL "SERIAL.Title.iso" naming
	snprintf(out, n, "%s", t);
	char *dot = strrchr(out, '.');
	if (dot && !strcasecmp(dot, ".iso")) *dot = 0;
}

#ifdef SELFTEST // host check: `make test`
#include <assert.h>
#include <stdlib.h>
static int read_file(void *f, unsigned lba, void *buf, unsigned n)
{
	return fseek(f, (long)lba * SECTOR, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}
#define iso_serial(f, s) iso_serial(read_file, f, s)
int main(void)
{
	// minimal ISO: PVD at 16, root dir at 18 with SYSTEM.CNF -> sector 20
	static unsigned char iso[SECTOR * 21];
	unsigned char *pvd = iso + 16 * SECTOR, *root = iso + 18 * SECTOR, *rec;
	pvd[0] = 1; memcpy(pvd + 1, "CD001", 5);
	pvd[156 + 2] = 18; pvd[156 + 10] = SECTOR & 255; pvd[156 + 11] = SECTOR >> 8;
	rec = root; rec[0] = 34; rec[32] = 1;                      // "." entry
	rec = root + 34; rec[0] = 46; rec[2] = 20; rec[10] = 40; rec[32] = 12; memcpy(rec + 33, "SYSTEM.CNF;1", 12);
	memcpy(iso + 20 * SECTOR, "BOOT2 = cdrom0:\\SLUS_209.46;1\r\nVER = 1.00\r\n", 40);
	FILE *f = tmpfile();
	fwrite(iso, 1, sizeof(iso), f);
	char s[16], d[16], t[64];
	assert(iso_serial(f, s) && !strcmp(s, "SLUS_209.46"));
	memcpy(iso + 20 * SECTOR, "BOOT = cdrom:\\SLPS_123.45;1\r\n\0\0\0\0\0\0\0\0\0\0\0", 40); // PS1: no BOOT2
	rewind(f); fwrite(iso, 1, sizeof(iso), f);
	assert(!iso_serial(f, s));
	pvd[1] = 'X'; rewind(f); fwrite(iso, 1, sizeof(iso), f);
	assert(!iso_serial(f, s));                                  // not an ISO
	fclose(f);
	serial_dash("SLUS_209.46", d);
	assert(!strcmp(d, "SLUS-20946"));
	iso_title("SLUS_209.46.God of War.iso", t, sizeof(t));
	assert(!strcmp(t, "God of War"));
	iso_title("Okami (USA).ISO", t, sizeof(t));
	assert(!strcmp(t, "Okami (USA)"));
	assert(name_serial("SLUS_209.46.God of War.iso", s) && !strcmp(s, "SLUS_209.46"));
	assert(!name_serial("God of War.iso", s) && !name_serial("SLUS 209.46.X.iso", s));
	puts("iso selftest ok");
	return 0;
}
#endif
