// PS2 game ISO identification: serial from SYSTEM.CNF (ISO9660), title from the file name.
// ISO9660: primary volume descriptor at sector 16; root directory record at offset 156 (extent LBA at +2, size at
// +10, little-endian); directory records: length at +0, name length at +32, name at +33 ("SYSTEM.CNF;1").
// SYSTEM.CNF: "BOOT2 = cdrom0:\SLUS_209.46;1" (PS2) or "BOOT = cdrom:\SLUS_007.47;1" (PS1). Spec: phase-6-games,
// phase-11-library.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "iso.h"

#define SECTOR 2048

static unsigned le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

// SYSTEM.CNF can sit past 2 GB (Persona 4: LBA 1 761 296 = 3.6 GB) and the EE's long is 32 bits, so the caller
// supplies a 64-bit-capable reader (fileXioLseek64 on the PS2, stdio on the host)
int iso_find(iso_read_fn read_at, void *f, const char *path, unsigned *lba_out, unsigned *size_out)
{
	static unsigned char buf[SECTOR] __attribute__((aligned(64)));
	if (!read_at(f, 16, buf, SECTOR) || buf[0] != 1 || memcmp(buf + 1, "CD001", 5)) return 0;
	unsigned lba = le32(buf + 156 + 2), size = le32(buf + 156 + 10);
	while (*path == '\\') path++;
	for (;;) { // one path component per directory, from the root
		unsigned n = strcspn(path, "\\"), found = 0;
		for (unsigned off = 0; off < size && !found; off += SECTOR) { // the directory, one sector at a time
			if (!read_at(f, lba + off / SECTOR, buf, SECTOR)) return 0;
			for (unsigned p = 0; p + 33 < SECTOR && buf[p] && !found; p += buf[p]) { // "NAME;1" or "DIR"
				unsigned nl = buf[p + 32];
				if ((nl == n || (nl > n && buf[p + 33 + n] == ';')) && !strncasecmp((char *)buf + p + 33, path, n))
					lba = le32(buf + p + 2), size = le32(buf + p + 10), found = 1;
			}
		}
		if (!found) return 0;
		if (!path[n]) break;
		path += n + 1;
	}
	*lba_out = lba, *size_out = size;
	return 1;
}

int iso_cnf(iso_read_fn read_at, void *f, char *out, unsigned max)
{
	static unsigned char buf[SECTOR * 2] __attribute__((aligned(64)));
	unsigned lba, size;
	if (!iso_find(read_at, f, "SYSTEM.CNF", &lba, &size)) return 0;
	if (size > max - 1) size = max - 1;
	if (size > sizeof(buf)) size = sizeof(buf);
	if (!read_at(f, lba, buf, size)) return 0;
	memcpy(out, buf, size);
	out[size] = 0;
	return 1;
}

int iso_serial(iso_read_fn read_at, void *f, int ps1, char *out) // out: >= 16 bytes, "SLUS_209.46"; 1 if found
{
	static char cnf[SECTOR * 2];
	return iso_cnf(read_at, f, cnf, sizeof(cnf)) && cnf_boot(cnf, ps1, out, NULL, 0);
}

int cnf_boot(const char *cnf, int ps1, char *file, char *raw, int raw_n)
{
	const char *b = cnf;
	for (;; b += 4) { // the key at a line start: BOOT2 (PS2) or BOOT followed by spaces or '=' (PS1)
		if (!(b = strstr(b, "BOOT"))) return 0;
		if ((b == cnf || b[-1] == '\n') && (ps1 ? b[4] != '2' : b[4] == '2')) break;
	}
	if (!(b = strchr(b, '='))) return 0;
	for (b++; *b == ' ' || *b == '\t'; b++) {}
	int n = strcspn(b, "\r\n");
	while (n && isspace((unsigned char)b[n - 1])) n--;
	if (raw) snprintf(raw, raw_n, "%.*s", n, b);
	const char *f = b;
	for (int i = 0; i < n; i++)
		if (b[i] == '\\' || b[i] == ':' || b[i] == '/') f = b + i + 1;
	int m = 0;
	while (f + m < b + n && f[m] != ';' && m < 15) m++;
	memcpy(file, f, m);
	file[m] = 0;
	return m > 0;
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
	if (dot && (!strcasecmp(dot, ".iso") || !strcasecmp(dot, ".vcd") || !strcasecmp(dot, ".elf"))) *dot = 0;
}

#if defined(SELFTEST) && !defined(SELFTEST_LIB) // host check: `make test` (SELFTEST_LIB: linked into another's)
#include <assert.h>
#include <stdlib.h>
static int read_file(void *f, unsigned lba, void *buf, unsigned n)
{
	return fseek(f, (long)lba * SECTOR, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}
#define iso_serial(f, s) iso_serial(read_file, f, 0, s)
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
	rec = root + 80; rec[0] = 38; rec[2] = 17; rec[11] = SECTOR >> 8; rec[32] = 4; memcpy(rec + 33, "DATA", 4); // dir
	rec = iso + 17 * SECTOR; rec[0] = 44; rec[2] = 19; rec[10] = 5; rec[32] = 10; memcpy(rec + 33, "MAIN.ELF;1", 10);
	FILE *f = tmpfile();
	fwrite(iso, 1, sizeof(iso), f);
	char s[16], d[16], t[64];
	unsigned lba, size;
	assert(iso_find(read_file, f, "\\data\\main.elf", &lba, &size) && lba == 19 && size == 5);
	assert(iso_find(read_file, f, "SYSTEM.CNF", &lba, &size) && lba == 20 && size == 40);
	assert(!iso_find(read_file, f, "DATA\\MAIN.EL", &lba, &size) && !iso_find(read_file, f, "MAIN.ELF", &lba, &size));
	assert(iso_serial(f, s) && !strcmp(s, "SLUS_209.46"));
	memcpy(iso + 20 * SECTOR, "BOOT = cdrom:\\SLPS_123.45;1\r\n\0\0\0\0\0\0\0\0\0\0\0", 40); // PS1: no BOOT2
	rewind(f); fwrite(iso, 1, sizeof(iso), f);
	assert(!iso_serial(f, s));
	assert((iso_serial)(read_file, f, 1, s) && !strcmp(s, "SLPS_123.45")); // PS1 mode (bypasses the macro)
	char raw[64];
	assert(cnf_boot("VER = 1\r\nBOOT2 = cdrom0:\\DATA\\MAIN.ELF;1\r\n", 0, s, raw, sizeof(raw)));
	assert(!strcmp(s, "MAIN.ELF") && !strcmp(raw, "cdrom0:\\DATA\\MAIN.ELF;1"));
	assert(!cnf_boot("BOOT2 = cdrom0:\\SLUS_209.46;1\n", 1, s, NULL, 0)); // PS2 file: no PS1 line
	assert(!cnf_boot("BOOT = cdrom:\\SCUS_941.63;1\n", 0, s, NULL, 0)); // PS1 file: no PS2 line
	assert(cnf_boot("BOOT=cdrom:SCUS_941.63;1\n", 1, s, NULL, 0) && !strcmp(s, "SCUS_941.63"));
	pvd[1] = 'X'; rewind(f); fwrite(iso, 1, sizeof(iso), f);
	assert(!iso_serial(f, s));                                  // not an ISO
	fclose(f);
	serial_dash("SLUS_209.46", d);
	assert(!strcmp(d, "SLUS-20946"));
	iso_title("SLUS_209.46.God of War.iso", t, sizeof(t));
	assert(!strcmp(t, "God of War"));
	iso_title("Crash Bandicoot.VCD", t, sizeof(t));
	assert(!strcmp(t, "Crash Bandicoot"));
	iso_title("Okami (USA).ISO", t, sizeof(t));
	assert(!strcmp(t, "Okami (USA)"));
	assert(name_serial("SLUS_209.46.God of War.iso", s) && !strcmp(s, "SLUS_209.46"));
	assert(!name_serial("God of War.iso", s) && !name_serial("SLUS 209.46.X.iso", s));
	puts("iso selftest ok");
	return 0;
}
#endif
