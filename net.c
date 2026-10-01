// Network for the cover download (phase 8): IOP ps2dev9 + netman + smap, lwIP on the EE, DHCP or a fixed IP,
// HTTPS through wolfSSL (ps2sdk ports). Bring-up as ps2sdk samples/network/tcpip-dhcp/ps2ip.c (docs/sources.md);
// HTTP/1.0, one connection per file, body read to EOF and checked against Content-Length.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "net.h"

int http_parse(const char *buf, int n, int *status, int *body, int *clen)
{
	int end = -1;
	for (int i = 0; i + 3 < n; i++)
		if (!memcmp(buf + i, "\r\n\r\n", 4)) { end = i; break; }
	if (end < 0 || n < 12 || memcmp(buf, "HTTP/1.", 7) || !isdigit((unsigned char)buf[9])) return 0;
	*status = atoi(buf + 9);
	*body = end + 4;
	*clen = -1;
	for (int i = 0; i < end; i++) // header lines: "\r\n" + name, case-insensitive
		if (buf[i] == '\n' && i + 16 < end && !strncasecmp(buf + i + 1, "content-length:", 15))
			*clen = atoi(buf + i + 16);
	return 1;
}

#ifndef SELFTEST
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <netman.h>
#include <ps2ip.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

// wolfSSL seeds its RNG from /dev/urandom, which the PS2 lacks. The link wraps open/read (Makefile -Wl,--wrap) and
// serves that one path from COP0.Count jitter. ponytail: weak entropy, fine for public cover images over TLS;
// real entropy (pad and network timings) if TLS ever carries anything private.
#define URANDOM_FD 4093
int __real_open(const char *path, int flags, ...);
int __real_read(int fd, void *buf, size_t n);
int __wrap_open(const char *path, int flags, ...)
{
	if (!strcmp(path, "/dev/urandom") || !strcmp(path, "/dev/random")) return URANDOM_FD;
	va_list ap;
	va_start(ap, flags);
	int mode = va_arg(ap, int);
	va_end(ap);
	return __real_open(path, flags, mode);
}
int __wrap_read(int fd, void *buf, size_t n)
{
	if (fd != URANDOM_FD) return __real_read(fd, buf, n);
	static unsigned x = 0x9E3779B9;
	x ^= (unsigned)time(NULL) ^ (unsigned)clock();
	for (size_t i = 0; i < n; i++) {
		unsigned c;
		for (int k = 0; k < 8; k++) { // the count's low bits after uneven work: cache, interrupts, other threads
			__asm__ volatile("mfc0 %0, $9" : "=r"(c));
			x = (x ^ c) * 0x01000193;
			for (volatile unsigned j = c & 31; j; j--) {}
		}
		((unsigned char *)buf)[i] = x >> 24;
	}
	return n;
}

#define IRX(m) extern unsigned char m##_irx[]; extern unsigned int size_##m##_irx
IRX(ps2dev9); IRX(netman); IRX(smap);

static int wait_for(int (*ok)(void)) // 10 s, as the ps2sdk sample
{
	for (int t = 0; t < 10; t++) {
		if (ok()) return 1;
		sleep(1);
	}
	return ok();
}
static int link_up(void) { return NetManIoctl(NETMAN_NETIF_IOCTL_GET_LINK_STATUS, NULL, 0, NULL, 0) == NETMAN_NETIF_ETH_LINK_STATE_UP; }
static int dhcp_bound(void)
{
	t_ip_info ip;
	return ps2ip_getconfig("sm0", &ip) >= 0 && ip.dhcp_status == DHCP_STATE_BOUND;
}

int net_up(const char *ip, const char *mask, const char *gw, const char *dns)
{
	struct { unsigned char *irx; unsigned *size; } mods[] = {{ps2dev9_irx, &size_ps2dev9_irx},
		{netman_irx, &size_netman_irx}, {smap_irx, &size_smap_irx}};
	for (int i = 0; i < 3; i++) {
		int r = 0;
		if (SifExecModuleBuffer(mods[i].irx, *mods[i].size, 0, NULL, &r) < 0 || r == 1) return NET_ERR_MODULES;
	}
	NetManInit();
	int dhcp = !strcasecmp(ip, "dhcp");
	struct ip4_addr a, m, g, d;
	ip4_addr_set_zero(&a), ip4_addr_set_zero(&m), ip4_addr_set_zero(&g), ip4_addr_set_zero(&d);
	if (!dhcp) inet_aton(ip, &a), inet_aton(mask, &m), inet_aton(gw, &g), inet_aton(dns, &d);
	ps2ipInit(&a, &m, &g);
	if (dhcp) { // the lease brings the DNS server too
		t_ip_info info;
		if (ps2ip_getconfig("sm0", &info) < 0) return NET_ERR_MODULES;
		info.dhcp_enabled = 1;
		ps2ip_setconfig(&info);
	} else dns_setserver(0, &d);
	if (!wait_for(link_up)) return NET_ERR_LINK;
	if (dhcp && !wait_for(dhcp_bound)) return NET_ERR_DHCP;
	return 0;
}

int https_get(const char *host, const char *path, char *buf, int max, int *body, int *len)
{
	static WOLFSSL_CTX *ctx;
	if (!ctx) {
		wolfSSL_Init();
		ctx = wolfSSL_CTX_new(wolfSSLv23_client_method());
		if (!ctx) return NET_ERR_TLS;
		// ponytail: no certificate check (no CA store, and the RTC may be off); the data is public cover art and every
		// image is size-checked after decoding. Embed the host's root CA if anything private ever goes over this.
		wolfSSL_CTX_set_verify(ctx, WOLFSSL_VERIFY_NONE, NULL);
	}
	struct addrinfo hints = {0}, *ai;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, "443", &hints, &ai) != 0) return NET_ERR_DNS;
	int s = socket(AF_INET, SOCK_STREAM, 0), r = s < 0 ? -1 : connect(s, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
	if (r < 0) { if (s >= 0) close(s); return NET_ERR_CONNECT; }
	struct timeval tv = {15, 0}; // a stalled server must not hang the download thread forever
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	WOLFSSL *ssl = wolfSSL_new(ctx);
	int st = NET_ERR_TLS, n = snprintf(buf, max, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
	if (ssl && wolfSSL_UseSNI(ssl, WOLFSSL_SNI_HOST_NAME, host, strlen(host)) == WOLFSSL_SUCCESS &&
	    wolfSSL_set_fd(ssl, s) == WOLFSSL_SUCCESS && wolfSSL_connect(ssl) == WOLFSSL_SUCCESS &&
	    wolfSSL_write(ssl, buf, n) == n) {
		for (n = 0; n < max && (r = wolfSSL_read(ssl, buf + n, max - n)) > 0;) n += r;
		int clen;
		st = NET_ERR_PROTO;
		if (http_parse(buf, n, &st, body, &clen)) {
			*len = n - *body;
			if (st == 200 && clen != *len) st = NET_ERR_PROTO; // truncated, or bigger than buf
		}
	}
	if (ssl) wolfSSL_shutdown(ssl), wolfSSL_free(ssl);
	close(s);
	return st;
}

#else // host check: `make test`
#include <assert.h>
int main(void)
{
	static const char ok[] = "HTTP/1.0 200 OK\r\nServer: x\r\ncontent-length: 5\r\n\r\nhello";
	int st, body, clen;
	assert(http_parse(ok, sizeof(ok) - 1, &st, &body, &clen) && st == 200 && clen == 5 && !memcmp(ok + body, "hello", 5));
	static const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
	assert(http_parse(nf, sizeof(nf) - 1, &st, &body, &clen) && st == 404 && clen == 0 && body == sizeof(nf) - 1);
	assert(!http_parse(ok, 20, &st, &body, &clen));                       // header cut short
	assert(!http_parse("FTP/1.0 200\r\n\r\n", 15, &st, &body, &clen));    // not HTTP
	static const char nolen[] = "HTTP/1.0 200 OK\r\n\r\nabc";
	assert(http_parse(nolen, sizeof(nolen) - 1, &st, &body, &clen) && clen == -1);
	puts("net selftest ok");
	return 0;
}
#endif
