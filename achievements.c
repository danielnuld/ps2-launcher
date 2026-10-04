// RetroAchievements (phase 16, step 1): the game's RA hash and the client on the home server.
// Hash: rcheevos src/rhash/hash_disc.c rc_hash_ps2 + rc_hash_find_playstation_executable (MIT): MD5 over the BOOT2
// executable's name (the SYSTEM.CNF path without "cdrom0:" and leading '\', up to ';' or a space, so subdirectories
// stay in it) followed by the executable's first 64 MB at most; only the first 2047 bytes of SYSTEM.CNF are read.
// Client: xerabora's wire protocol (github.com/hacan359/xerabora protocol/PROTOCOL.md): text over UDP 18194, the
// console's own address in every request, replies padded with spaces to 64-byte multiples.
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include "achievements.h"
#include "third_party/neutrino-igr/raagent/ra_snap.h" // ra_watch.h too

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

// the list arrives over the network and the fork reads it inside the game: header, every entry 1, 2 or 4 bytes adding
// up to the header's total, and the optional chain tail exactly as long as it says (ra_watch.h)
int ra_wl_check(const unsigned char *b, int n)
{
	struct ra_watch_file h;
	if (n < (int)sizeof(h)) return 0;
	memcpy(&h, b, sizeof(h));
	if (h.magic != RA_WATCH_MAGIC || h.version != RA_WATCH_VERSION || h.count == 0 || h.count > RA_WATCH_MAX ||
	    n < (int)(sizeof(h) + 4 * h.count) || h.bytes > RA_SNAP_MAX_BYTES) return 0;
	unsigned sum = 0, e;
	for (unsigned i = 0; i < h.count; i++) {
		memcpy(&e, b + sizeof(h) + 4 * i, 4);
		unsigned s = RA_WATCH_SIZE(e);
		if (s != 1 && s != 2 && s != 4) return 0;
		sum += s;
	}
	int at = sizeof(h) + 4 * h.count;
	if (sum != h.bytes) return 0;
	if (n == at) return h.count;
	struct ra_node_file t;
	if (n < at + (int)sizeof(t)) return 0;
	memcpy(&t, b + at, sizeof(t));
	return t.magic == RA_NODE_MAGIC && t.count <= RA_NODE_MAX &&
	       n == at + (int)(sizeof(t) + t.count * sizeof(struct ra_node)) ? (int)h.count : 0;
}

// JSON string at p (after the opening quote) into out as UTF-8: \" \\ \/ \n \t \uXXXX (BMP); returns past the closing
// quote. Only what the client's /game answer holds: no surrogate pairs (shown as '?')
static const char *json_str(const char *p, const char *end, char *out, int n)
{
	int k = 0;
	for (; p < end && *p != '"'; p++) {
		unsigned c = (unsigned char)*p;
		if (c == '\\' && p + 1 < end) {
			c = (unsigned char)*++p;
			if (c == 'n' || c == 't') c = ' ';
			else if (c == 'u' && p + 4 < end) {
				char hex[5] = {p[1], p[2], p[3], p[4], 0};
				c = strtoul(hex, NULL, 16), p += 4;
				if (c >= 0xD800 && c < 0xE000) c = '?';
			}
		}
		char u[3];
		int m = c < 0x80 ? (u[0] = c, 1) : c < 0x800 ? (u[0] = 0xC0 | c >> 6, u[1] = 0x80 | (c & 63), 2)
		                                             : (u[0] = '?', 1);
		if (k + m < n) memcpy(out + k, u, m), k += m;
	}
	out[k] = 0;
	return p < end ? p + 1 : end;
}

static const char *json_key(const char *p, const char *end, const char *key) // the value after "key": in [p, end)
{
	int kl = strlen(key);
	for (; p + kl + 3 <= end; p++)
		if (p[0] == '"' && !memcmp(p + 1, key, kl) && p[kl + 1] == '"' && p[kl + 2] == ':') return p + kl + 3;
	return NULL;
}

int ra_parse_game(const char *json, int n, ra_ach *out, int max)
{
	const char *end = json + n, *p = json_key(json, end, "achievements"), *q;
	int count = 0;
	if (!p || *p != '[') return 0;
	for (p++; p < end && *p != ']' && count < max;) {
		if (*p != '{') { p++; continue; }
		const char *o = p; // the object's end: the first '}' outside a string (the objects are flat)
		for (int in = 0; p < end && (in || *p != '}'); p++)
			if (*p == '\\' && in) p++;
			else if (*p == '"') in = !in;
		ra_ach *a = &out[count++];
		memset(a, 0, sizeof(*a));
		if ((q = json_key(o, p, "title")) && *q == '"') json_str(q + 1, p, a->title, sizeof(a->title));
		if ((q = json_key(o, p, "description")) && *q == '"') json_str(q + 1, p, a->desc, sizeof(a->desc));
		if ((q = json_key(o, p, "points"))) a->points = atoi(q);
		a->earned = (q = json_key(o, p, "earned")) && q[0] == '"' && q[1] != '"';
		p++;
	}
	return count;
}

#ifndef SELFTEST
#include <stdlib.h>
#include <unistd.h>
#include "net.h"

int ra_game_id(const char *hash) // RetroAchievements' public connect API: {"Success":true,"GameID":19040}
{
	static char buf[4096];
	char path[96];
	int body, len;
	snprintf(path, sizeof(path), "/dorequest.php?r=gameid&m=%s", hash);
	if (https_get("retroachievements.org", path, buf, sizeof(buf) - 1, &body, &len) != 200) return -1;
	buf[body + len] = 0;
	const char *q = json_key(buf + body, buf + body + len, "GameID");
	return q ? atoi(q) : -1;
}

int ra_game_list(int id, ra_ach *out, int max)
{
	int body, len, max_json = 1 << 20; // ~300 bytes per achievement; the largest sets have a few hundred
	char path[48], *buf = malloc(max_json);
	if (!buf) return -1;
	snprintf(path, sizeof(path), "/game?id=%d", id);
	int st = http_get(ra_srv_ip, 18280, path, buf, max_json, &body, &len);
	int n = st == 200 ? ra_parse_game(buf + body, len, out, max) : st < 0 ? st : -st;
	free(buf);
	return n;
}
#include <ps2ip.h>
#include <arpa/inet.h>

#define RA_PORT 18194
static int sock = -1, found; // found: 0 not asked yet, 1 client known, -1 none
static struct sockaddr_in srv;
char ra_me[16], ra_srv_ip[16]; // the PS2's IP as lwIP has it, and the client's once found

// ps2sdk's lwIP has no SO_RCVTIMEO (lwip_setsockopt knows no 0x1006), so a lost reply blocked recvfrom forever: the
// chip stayed at "LOGROS ·" on the console. Polled instead with lwIP's own MSG_DONTWAIT (0x08; newlib's sys/socket.h
// says 0x80, and libcglue passes the flags through unchanged)
#define LWIP_DONTWAIT 0x08

// sends msg to `to`, then waits up to 1 s for a datagram starting with tag; its length (NUL-terminated), else -1.
// Text replies lose the padding spaces; a raw one (RAC1: list bytes may end in 0x20) keeps them
static int ask_ll(const struct sockaddr_in *to, const char *msg, const char *tag, char *r, int max,
                  struct sockaddr_in *from, int raw)
{
	sendto(sock, msg, strlen(msg), 0, (const struct sockaddr *)to, sizeof(*to));
	for (int ms = 0; ms < 1000 && !ra_abort;) { // the client sends RAO1 twice (unicast and broadcast): skip others
		socklen_t fl = sizeof(*from);
		int n = recvfrom(sock, r, max - 1, LWIP_DONTWAIT, (struct sockaddr *)from, &fl);
		if (n < 0) { usleep(20000), ms += 20; continue; }
		r[n] = 0;
		while (!raw && n && r[n - 1] == ' ') r[--n] = 0; // the padding
		if (!strncmp(r, tag, strlen(tag))) return n;
	}
	return -1;
}
#define ask(to, msg, tag, r, max, from) ask_ll(to, msg, tag, r, max, from, 0)

static int find(const char *server)
{
	t_ip_info ip;
	if (ps2ip_getconfig("sm0", &ip) < 0) return -1;
	snprintf(ra_me, sizeof(ra_me), "%s", inet_ntoa(ip.ipaddr));
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
	snprintf(msg, sizeof(msg), "RAP1 %s %d", ra_me, RA_PORT);
	for (int t = 0; t < 3 && !ra_abort; t++) // UDP: a lost datagram is asked again
		if (ask(&a, msg, "RAO1 OK", r, sizeof(r), &srv) > 0) {
			srv.sin_port = htons(RA_PORT);
			snprintf(ra_srv_ip, sizeof(ra_srv_ip), "%s", inet_ntoa(srv.sin_addr));
			snprintf(ra_note, sizeof(ra_note), "client %.20s at %s, me %s", r + 8, inet_ntoa(srv.sin_addr), ra_me);
			return 1;
		}
	snprintf(ra_note, sizeof(ra_note), "no client answered %s, me %s", *server ? server : "the broadcast", ra_me);
	return -1;
}

static int raq(const char *server, const char *hash, const char *serial, int *bytes, int *chunks, int *count,
               char *title, int tn)
{
	if (!found) found = find(server);
	if (found < 0) return RA_NOSERVER;
	char msg[128], r[256];
	struct sockaddr_in from;
	snprintf(msg, sizeof(msg), "RAQ1 %s %s %s %d", hash, serial, ra_me, RA_PORT);
	for (int t = 0; t < 20 && !ra_abort; t++) { // WAIT: the client is loading the set from RetroAchievements (seconds)
		if (ask(&srv, msg, "RAA1", r, sizeof(r), &from) < 0) continue;
		if (!strncmp(r, "RAA1 NO", 7)) return RA_NOSET;
		if (strncmp(r, "RAA1 OK", 7)) { sleep(1); continue; } // WAIT
		int unlocked, unsupported, at = 0; // "RAA1 OK <bytes> <chunks> <total> <unlocked> <unsup> <title>"
		*count = 0, *title = 0;
		if (sscanf(r + 7, "%d %d %d %d %d %n", bytes, chunks, count, &unlocked, &unsupported, &at) >= 3 && at)
			snprintf(title, tn, "%s", r + 7 + at);
		return *count > 0 ? RA_SET : RA_NOSET;
	}
	return RA_FAIL;
}

int ra_query(const char *server, const char *hash, const char *serial, int *count, char *title, int tn)
{
	int bytes, chunks;
	return raq(server, hash, serial, &bytes, &chunks, count, title, tn);
}

int ra_watchlist(const char *server, const char *hash, const char *serial, unsigned char *out, int max)
{
	int bytes = 0, chunks = 0, count;
	char title[64], msg[128];
	static char r[RA_CHUNK + 128];
	struct sockaddr_in from;
	if (raq(server, hash, serial, &bytes, &chunks, &count, title, sizeof(title)) != RA_SET) return -1;
	if (bytes <= 0 || bytes > max || chunks != (bytes + RA_CHUNK - 1) / RA_CHUNK) return -2;
	for (int i = 0; i < chunks; i++) {
		int got = 0;
		snprintf(msg, sizeof(msg), "RAG1 %s %d %s %d", hash, i, ra_me, RA_PORT);
		for (int t = 0; t < 5 && !got && !ra_abort; t++) { // a lost chunk is asked again
			int n = ask_ll(&srv, msg, "RAC1 ", r, sizeof(r), &from, 1), idx = -1, len = -1, at = 0;
			if (n > 0 && sscanf(r + 5, "%d %d %n", &idx, &len, &at) == 2 && at && idx == i &&
			    len == (i < chunks - 1 ? RA_CHUNK : bytes - i * RA_CHUNK) && 5 + at + len <= n)
				memcpy(out + i * RA_CHUNK, r + 5 + at, len), got = 1;
		}
		if (!got) return -3;
	}
	return ra_wl_check(out, bytes) ? bytes : -4;
}

#elif !defined(ORBIT_HASH) // host check: `make test`, RA_CHECK=<iso> prints our hash of a real ISO
#include <assert.h>
#include <stdlib.h>
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
	// watch lists: header + entries (+ chain tail)
	unsigned wl[16] = {RA_WATCH_MAGIC, RA_WATCH_VERSION, 3, 7, RA_WATCH_PACK(0x100000, 4), RA_WATCH_PACK(0x200001, 1),
	                   RA_WATCH_PACK(0x300002, 2), RA_NODE_MAGIC, 1, RA_NODE_PACK(0, 0, 4), 0x10};
	assert(ra_wl_check((unsigned char *)wl, 28) == 3);           // entries only
	assert(ra_wl_check((unsigned char *)wl, 44) == 3);           // with one node
	assert(!ra_wl_check((unsigned char *)wl, 40) && !ra_wl_check((unsigned char *)wl, 27)); // cut short
	wl[3] = 8, assert(!ra_wl_check((unsigned char *)wl, 28)), wl[3] = 7; // total does not add up
	wl[5] = RA_WATCH_PACK(0x200001, 3), assert(!ra_wl_check((unsigned char *)wl, 28)); // size 3
	wl[5] = RA_WATCH_PACK(0x200001, 1), wl[0] = 0, assert(!ra_wl_check((unsigned char *)wl, 28)); // magic
	// the client's /game answer: escapes, an earned one, a "title" outside the list that must not be taken
	static const char js[] = "{\"id\":1,\"title\":\"Game\",\"achievements\":[{\"id\":7,\"title\":\"Caf\\u00e9 \\\"Bar\\\"\","
	                         "\"description\":\"a\\/b {x}\",\"earned\":\"2026-10-03 12:00:00\",\"points\":10},"
	                         "{\"id\":8,\"title\":\"Two\",\"description\":\"\",\"earned\":\"\",\"points\":25}],\"x\":1}";
	ra_ach ach[4];
	assert(ra_parse_game(js, sizeof(js) - 1, ach, 4) == 2);
	assert(!strcmp(ach[0].title, "Caf\xc3\xa9 \"Bar\"") && !strcmp(ach[0].desc, "a/b {x}") && ach[0].points == 10);
	assert(ach[0].earned && !ach[1].earned && ach[1].points == 25 && !strcmp(ach[1].title, "Two"));
	assert(ra_parse_game(js, sizeof(js) - 1, ach, 1) == 1);          // capped
	assert(!ra_parse_game("{\"achievements\":[]}", 19, ach, 4));     // empty set
	const char *game = getenv("RA_GAME"); // a saved /game?id= answer (`make test RA_GAME=<file>`)
	if (game && *game) {
		static char gj[1 << 20];
		static ra_ach all[1000];
		assert((f = fopen(game, "rb")));
		int n = fread(gj, 1, sizeof(gj), f), k = ra_parse_game(gj, n, all, 1000), pts = 0;
		fclose(f);
		for (int i = 0; i < k; i++) pts += all[i].points;
		printf("ra_parse_game %s: %d achievements, %d points, first \"%s\" (%s)\n", game, k, pts, k ? all[0].title : "",
		       k ? all[0].desc : "");
		assert(k > 0);
	}
	const char *real = getenv("RA_WL"); // Black's list from the client (`make test RA_WL=<file>`)
	if (real && *real) {
		static unsigned char b[1 << 16];
		assert((f = fopen(real, "rb")));
		int n = fread(b, 1, sizeof(b), f);
		fclose(f);
		printf("ra_wl_check %s: %d entries in %d bytes\n", real, ra_wl_check(b, n), n);
		assert(ra_wl_check(b, n) > 0);
	}
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
