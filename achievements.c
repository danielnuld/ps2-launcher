// RetroAchievements (phase 16, step 1): the game's RA hash and the client on the home server.
// Hash: rcheevos src/rhash/hash_disc.c rc_hash_ps2 + rc_hash_find_playstation_executable (MIT): MD5 over the BOOT2
// executable's name (the SYSTEM.CNF path without "cdrom0:" and leading '\', up to ';' or a space, so subdirectories
// stay in it) followed by the executable's first 64 MB at most; only the first 2047 bytes of SYSTEM.CNF are read.
// Client: xerabora's wire protocol (github.com/hacan359/xerabora protocol/PROTOCOL.md): text over UDP 18194, the
// console's own address in every request, replies padded with spaces to 64-byte multiples.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "achievements.h"

#ifdef SELFTEST
#include <openssl/md5.h>
typedef MD5_CTX md5_ctx;
#define md5_init(c) MD5_Init(c)
#define md5_add(c, p, n) MD5_Update(c, p, n)
#define md5_end(c, d) MD5_Final(d, c)
#else
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/md5.h>
typedef wc_Md5 md5_ctx;
#define md5_init(c) wc_InitMd5(c)
#define md5_add(c, p, n) wc_Md5Update(c, p, n)
#define md5_end(c, d) wc_Md5Final(c, d)
#endif

#define MAX_EXE (64u << 20) // rcheevos MAX_BUFFER_SIZE
volatile int ra_abort;
char ra_note[96];

void ra_exe_name(const char *boot2, char *out, int n)
{
	if (!strncmp(boot2, "cdrom0:", 7)) boot2 += 7;
	while (*boot2 == '\\') boot2++;
	int m = 0;
	while (boot2[m] && !isspace((unsigned char)boot2[m]) && boot2[m] != ';') m++;
	snprintf(out, n, "%.*s", m, boot2);
}

int ra_hash(iso_read_fn read_at, void *f, char *out, unsigned *exe_size)
{
	static char cnf[2048];
	static unsigned char buf[64 << 10] __attribute__((aligned(64)));
	char file[16], raw[128], name[64]; // name: rcheevos' exe_name[64]
	unsigned lba, size;
	if (!iso_cnf(read_at, f, cnf, sizeof(cnf)) || !cnf_boot(cnf, 0, file, raw, sizeof(raw))) return 0;
	ra_exe_name(raw, name, sizeof(name));
	if (!iso_find(read_at, f, name, &lba, &size)) return 0;
	if (size > MAX_EXE) size = MAX_EXE;
	md5_ctx c;
	unsigned char d[16];
	md5_init(&c);
	md5_add(&c, (const unsigned char *)name, strlen(name));
	for (unsigned done = 0; done < size;) {
		if (ra_abort) return 0;
		unsigned n = size - done < sizeof(buf) ? size - done : sizeof(buf);
		if (!read_at(f, lba + done / 2048, buf, n)) return 0;
		md5_add(&c, buf, n);
		done += n;
	}
	md5_end(&c, d);
	for (int i = 0; i < 16; i++) sprintf(out + 2 * i, "%02x", d[i]);
	*exe_size = size;
	return 1;
}

#ifndef SELFTEST
#include <stdlib.h>
#include <unistd.h>
#include <ps2ip.h>
#include <arpa/inet.h>

#define RA_PORT 18194
static int sock = -1, found; // found: 0 not asked yet, 1 client known, -1 none
static struct sockaddr_in srv;
static char me[16];

// ps2sdk's lwIP has no SO_RCVTIMEO (lwip_setsockopt knows no 0x1006), so a lost reply blocked recvfrom forever: the
// chip stayed at "LOGROS ·" on the console. Polled instead with lwIP's own MSG_DONTWAIT (0x08; newlib's sys/socket.h
// says 0x80, and libcglue passes the flags through unchanged)
#define LWIP_DONTWAIT 0x08

// sends msg to `to`, then waits up to 1 s for a datagram starting with tag; its length (NUL-terminated), else -1
static int ask(const struct sockaddr_in *to, const char *msg, const char *tag, char *r, int max, struct sockaddr_in *from)
{
	sendto(sock, msg, strlen(msg), 0, (const struct sockaddr *)to, sizeof(*to));
	for (int ms = 0; ms < 1000 && !ra_abort;) { // the client sends RAO1 twice (unicast and broadcast): skip others
		socklen_t fl = sizeof(*from);
		int n = recvfrom(sock, r, max - 1, LWIP_DONTWAIT, (struct sockaddr *)from, &fl);
		if (n < 0) { usleep(20000), ms += 20; continue; }
		r[n] = 0;
		while (n && r[n - 1] == ' ') r[--n] = 0; // the padding
		if (!strncmp(r, tag, strlen(tag))) return n;
	}
	return -1;
}

static int find(const char *server)
{
	t_ip_info ip;
	if (ps2ip_getconfig("sm0", &ip) < 0) return -1;
	snprintf(me, sizeof(me), "%s", inet_ntoa(ip.ipaddr));
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET, a.sin_port = htons(RA_PORT), a.sin_addr.s_addr = htonl(INADDR_ANY);
	int on = 1;
	if (sock < 0 || bind(sock, (struct sockaddr *)&a, sizeof(a)) < 0) {
		snprintf(ra_note, sizeof(ra_note), "socket %d, bind failed", sock);
		return -1;
	}
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
	a.sin_addr.s_addr = *server ? inet_addr(server) : htonl(INADDR_BROADCAST);
	char msg[64], r[256];
	snprintf(msg, sizeof(msg), "RAP1 %s %d", me, RA_PORT);
	for (int t = 0; t < 3 && !ra_abort; t++) // UDP: a lost datagram is asked again
		if (ask(&a, msg, "RAO1 OK", r, sizeof(r), &srv) > 0) {
			srv.sin_port = htons(RA_PORT);
			snprintf(ra_note, sizeof(ra_note), "client %.20s at %s, me %s", r + 8, inet_ntoa(srv.sin_addr), me);
			return 1;
		}
	snprintf(ra_note, sizeof(ra_note), "no client answered %s, me %s", *server ? server : "the broadcast", me);
	return -1;
}

int ra_query(const char *server, const char *hash, const char *serial, int *count, char *title, int tn)
{
	if (!found) found = find(server);
	if (found < 0) return RA_NOSERVER;
	char msg[128], r[256];
	struct sockaddr_in from;
	snprintf(msg, sizeof(msg), "RAQ1 %s %s %s %d", hash, serial, me, RA_PORT);
	for (int t = 0; t < 20 && !ra_abort; t++) { // WAIT: the client is loading the set from RetroAchievements (seconds)
		if (ask(&srv, msg, "RAA1", r, sizeof(r), &from) < 0) continue;
		if (!strncmp(r, "RAA1 NO", 7)) return RA_NOSET;
		if (strncmp(r, "RAA1 OK", 7)) { sleep(1); continue; } // WAIT
		int bytes, chunks, unlocked, unsupported, at = 0; // "RAA1 OK <bytes> <chunks> <total> <unlocked> <unsup> <title>"
		*count = 0, *title = 0;
		if (sscanf(r + 7, "%d %d %d %d %d %n", &bytes, &chunks, count, &unlocked, &unsupported, &at) >= 3 && at)
			snprintf(title, tn, "%s", r + 7 + at);
		return *count > 0 ? RA_SET : RA_NOSET;
	}
	return RA_FAIL;
}

#else // host check: `make test`, RA_CHECK=<iso> prints our hash of a real ISO
#include <assert.h>
static int read_file(void *f, unsigned lba, void *buf, unsigned n)
{
	return fseeko(f, (off_t)lba * 2048, SEEK_SET) == 0 && fread(buf, 1, n, f) == n;
}
int main(int argc, char **argv)
{
	char s[64], h[33];
	ra_exe_name("cdrom0:\\SLUS_209.46;1", s, sizeof(s)), assert(!strcmp(s, "SLUS_209.46"));
	ra_exe_name("cdrom0:\\\\DATA\\MAIN.ELF;1", s, sizeof(s)), assert(!strcmp(s, "DATA\\MAIN.ELF"));
	ra_exe_name("cdrom:\\X.ELF", s, sizeof(s)), assert(!strcmp(s, "cdrom:\\X.ELF")); // rcheevos: only cdrom0: goes
	ra_exe_name("cdrom0:SLES_123.45 ;1", s, sizeof(s)), assert(!strcmp(s, "SLES_123.45"));
	// minimal ISO (as iso.c's selftest): SYSTEM.CNF at 20 boots SLUS_209.46 at 19, "\x7f" "ELF!" (5 bytes)
	static unsigned char iso[2048 * 21];
	unsigned char *pvd = iso + 16 * 2048, *root = iso + 18 * 2048, *rec;
	pvd[0] = 1; memcpy(pvd + 1, "CD001", 5);
	pvd[156 + 2] = 18; pvd[156 + 11] = 2048 >> 8;
	rec = root; rec[0] = 34; rec[32] = 1;
	rec = root + 34; rec[0] = 46; rec[2] = 20; rec[10] = 40; rec[32] = 12; memcpy(rec + 33, "SYSTEM.CNF;1", 12);
	rec = root + 80; rec[0] = 46; rec[2] = 19; rec[10] = 5; rec[32] = 13; memcpy(rec + 33, "SLUS_209.46;1", 13);
	memcpy(iso + 20 * 2048, "BOOT2 = cdrom0:\\SLUS_209.46;1\r\nVER = 1.00\r\n", 40);
	memcpy(iso + 19 * 2048, "\x7f" "ELF!", 5);
	FILE *f = tmpfile();
	fwrite(iso, 1, sizeof(iso), f);
	unsigned n;
	assert(ra_hash(read_file, f, h, &n) && n == 5);
	assert(!strcmp(h, "db0955160c64a064f346b8c098996b21")); // python: md5(b"SLUS_209.46\x7fELF!")
	fclose(f);
	if (argc > 1) { // a real ISO: compare by hand with rcheevos' rc_hash_ps2 (docs/phase16-results.md)
		assert((f = fopen(argv[1], "rb")));
		assert(ra_hash(read_file, f, h, &n));
		printf("ra_hash %s: %s (executable %u bytes)\n", argv[1], h, n);
		fclose(f);
	}
	puts("achievements selftest ok");
	return 0;
}
#endif
