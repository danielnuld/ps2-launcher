// Jellyfin client (phase 15). Endpoints and fields as Jellyfin 12.1 answers them (checked against a live server,
// docs/sources.md): POST /Users/AuthenticateByName, GET /UserViews, /Items, /Shows/{id}/Episodes,
// /Items/{id}/Images/Primary, /Videos/{id}/stream.mpeg, POST /Sessions/Playing[/Progress|/Stopped]. Requests carry
// the "Authorization: MediaBrowser ..." header (Client, Device, DeviceId, Version, Token).
// The transcoded stream comes with Transfer-Encoding: chunked, so the HTTP reader decodes chunks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <netdb.h>
#ifdef _EE
#include <ps2ip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include "jellyfin.h"

#define CLIENT "Authorization: MediaBrowser Client=\"ORBIT\", Device=\"PlayStation 2\", DeviceId=\"orbit-ps2\", " \
	"Version=\"0.15\""

// ---- HTTP ----
static int fill(http_stream *h) // more raw bytes into buf (after compacting); 0 at EOF / error
{
	if (h->pos > 0) memmove(h->buf, h->buf + h->pos, h->len - h->pos), h->len -= h->pos, h->pos = 0;
	if (h->len == (int)sizeof(h->buf)) return 1;
	int r = recv(h->s, h->buf + h->len, sizeof(h->buf) - h->len, 0);
	if (r <= 0) return 0;
	h->len += r;
	return 1;
}

static int line(http_stream *h, char *out, int max) // one header / chunk-size line without CRLF; 0 if none
{
	for (;;) {
		for (int i = h->pos; i + 1 < h->len; i++)
			if (h->buf[i] == '\r' && h->buf[i + 1] == '\n') {
				int n = i - h->pos < max - 1 ? i - h->pos : max - 1;
				memcpy(out, h->buf + h->pos, n), out[n] = 0;
				h->pos = i + 2;
				return 1;
			}
		if (h->len - h->pos >= (int)sizeof(h->buf) - 1 || !fill(h)) return 0; // a line longer than the buffer
	}
}

int http_open(http_stream *h, const char *host, int port, const char *method, const char *path, const char *extra,
              const char *body)
{
	char req[1536], l[256], ps[8];
	struct addrinfo hints, *ai;
	memset(h, 0, sizeof(*h));
	h->s = -1;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET, hints.ai_socktype = SOCK_STREAM;
	snprintf(ps, sizeof(ps), "%d", port);
	if (getaddrinfo(host, ps, &hints, &ai) != 0) return JF_ERR_CONNECT;
	h->s = socket(AF_INET, SOCK_STREAM, 0);
	int r = h->s < 0 ? -1 : connect(h->s, ai->ai_addr, ai->ai_addrlen);
	freeaddrinfo(ai);
	if (r < 0) { http_close(h); return JF_ERR_CONNECT; }
	struct timeval tv = {15, 0}; // a stalled server must not hang the caller forever
	setsockopt(h->s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	int n = snprintf(req, sizeof(req), "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n%s%s", method, path, host,
	                 port, extra ? extra : "", body ? "Content-Type: application/json\r\n" : "");
	if (body) n += snprintf(req + n, sizeof(req) - n, "Content-Length: %d\r\n", (int)strlen(body));
	n += snprintf(req + n, sizeof(req) - n, "\r\n");
	if (n >= (int)sizeof(req) || send(h->s, req, n, 0) != n || (body && send(h->s, body, strlen(body), 0) != (int)strlen(body))) {
		http_close(h);
		return JF_ERR_HTTP;
	}
	if (!line(h, l, sizeof(l)) || strncmp(l, "HTTP/1.", 7) || strlen(l) < 12) { http_close(h); return JF_ERR_HTTP; }
	h->status = atoi(l + 9);
	h->left = -1;
	while (line(h, l, sizeof(l)) && *l) {
		if (!strncasecmp(l, "content-length:", 15)) h->left = atoll(l + 15);
		if (!strncasecmp(l, "transfer-encoding:", 18) && strstr(l + 18, "chunked")) h->chunked = 1;
	}
	if (h->chunked) h->left = 0; // read the first chunk size on the first read
	return h->status;
}

int http_read(http_stream *h, void *dst, int n)
{
	char l[32];
	if (h->eof || n <= 0) return 0;
	if (h->chunked && h->left == 0) {
		if (!line(h, l, sizeof(l))) return -1;
		if (!*l && !line(h, l, sizeof(l))) return -1; // the CRLF that ends the previous chunk
		h->left = strtoll(l, NULL, 16);
		if (h->left == 0) { h->eof = 1; return 0; }
	}
	if (h->left == 0) { h->eof = 1; return 0; } // Content-Length reached
	if (h->pos == h->len && !fill(h)) {
		h->eof = 1;
		return h->left == -1 ? 0 : -1; // the end of a to-EOF body, or cut short
	}
	int k = h->len - h->pos;
	if (k > n) k = n;
	if (h->left >= 0 && k > h->left) k = (int)h->left;
	memcpy(dst, h->buf + h->pos, k);
	h->pos += k;
	if (h->left > 0) h->left -= k;
	return k;
}

void http_close(http_stream *h)
{
	if (h->s >= 0) close(h->s);
	h->s = -1;
}

static int http_all(http_stream *h, char *buf, int max) // whole body; length, or -1 if it did not fit / broke
{
	int n = 0, r;
	while ((r = http_read(h, buf + n, max - 1 - n)) > 0) n += r;
	http_close(h);
	if (r < 0 || n >= max - 1) return -1;
	buf[n] = 0;
	return n;
}

// ---- JSON ----
static int jsp(const char *s, int n, int *i, jtok *t, int max, int *nt, int depth);

static int jtoken(jtok *t, int max, int *nt, int type, int start)
{
	if (*nt >= max) return -1;
	t[*nt].type = type, t[*nt].start = start, t[*nt].end = start, t[*nt].size = 0;
	return (*nt)++;
}

static void ws(const char *s, int n, int *i) { while (*i < n && (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r')) (*i)++; }

static int jsp(const char *s, int n, int *i, jtok *t, int max, int *nt, int depth) // one value at s[*i]
{
	ws(s, n, i);
	if (*i >= n || depth > 32) return -1;
	char c = s[*i];
	if (c == '{' || c == '[') {
		int k = jtoken(t, max, nt, c == '{' ? J_OBJ : J_ARR, *i);
		if (k < 0) return -1;
		(*i)++;
		ws(s, n, i);
		if (*i < n && s[*i] == (c == '{' ? '}' : ']')) { t[k].end = ++*i; return 0; }
		for (;;) {
			if (c == '{') { // key, colon
				ws(s, n, i);
				if (*i >= n || s[*i] != '"' || jsp(s, n, i, t, max, nt, depth + 1) < 0) return -1;
				ws(s, n, i);
				if (*i >= n || s[*i] != ':') return -1;
				(*i)++;
			}
			if (jsp(s, n, i, t, max, nt, depth + 1) < 0) return -1;
			t[k].size++;
			ws(s, n, i);
			if (*i < n && s[*i] == ',') { (*i)++; continue; }
			if (*i < n && s[*i] == (c == '{' ? '}' : ']')) { t[k].end = ++*i; return 0; }
			return -1;
		}
	}
	if (c == '"') {
		int k = jtoken(t, max, nt, J_STR, *i + 1);
		if (k < 0) return -1;
		for ((*i)++; *i < n && s[*i] != '"'; (*i)++)
			if (s[*i] == '\\') (*i)++;
		if (*i >= n) return -1;
		t[k].end = (*i)++;
		return 0;
	}
	int k = jtoken(t, max, nt, J_PRIM, *i);
	if (k < 0) return -1;
	while (*i < n && s[*i] != ',' && s[*i] != '}' && s[*i] != ']' && s[*i] != ' ' && s[*i] != '\n' && s[*i] != '\r' &&
	       s[*i] != '\t') (*i)++;
	t[k].end = *i;
	return t[k].end > t[k].start ? 0 : -1;
}

int json_parse(const char *s, int n, jtok *t, int max)
{
	int i = 0, nt = 0;
	return jsp(s, n, &i, t, max, &nt, 0) < 0 ? -1 : nt;
}

int json_skip(const jtok *t, int n, int i)
{
	int j = i + 1;
	while (j < n && t[j].start < t[i].end) j++;
	return j;
}

int json_key(const char *s, const jtok *t, int n, int obj, const char *key)
{
	if (obj < 0 || obj >= n || t[obj].type != J_OBJ) return -1;
	int len = strlen(key);
	for (int i = obj + 1, k = 0; k < t[obj].size && i < n; k++) {
		int v = i + 1; // tokens: key string, then its value
		if (t[i].end - t[i].start == len && !memcmp(s + t[i].start, key, len)) return v;
		i = json_skip(t, n, v);
	}
	return -1;
}

static int utf8(unsigned c, char *o) // code point -> UTF-8 bytes
{
	if (c < 0x80) return o[0] = c, 1;
	if (c < 0x800) return o[0] = 0xC0 | c >> 6, o[1] = 0x80 | (c & 63), 2;
	return o[0] = 0xE0 | c >> 12, o[1] = 0x80 | (c >> 6 & 63), o[2] = 0x80 | (c & 63), 3;
}

int json_str(const char *s, const jtok *t, int i, char *out, int max)
{
	int n = 0;
	if (i < 0 || t[i].type != J_STR || max < 1) { if (max > 0) *out = 0; return 0; }
	for (int p = t[i].start; p < t[i].end && n < max - 1; p++) {
		char c = s[p];
		if (c != '\\') { out[n++] = c; continue; }
		c = s[++p];
		if (c == 'u' && p + 4 < t[i].end) {
			unsigned u = strtoul((char[5]){s[p + 1], s[p + 2], s[p + 3], s[p + 4], 0}, NULL, 16);
			char b[3];
			p += 4;
			if (u >= 0xD800 && u < 0xE000) u = '?'; // surrogate pairs: outside the font anyway
			int k = utf8(u, b);
			if (n + k > max - 1) break; // never half a character
			memcpy(out + n, b, k), n += k;
		} else out[n++] = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' || c == 'f' ? ' ' : c;
	}
	out[n] = 0;
	return 1;
}

int json_num10(const char *s, const jtok *t, int i) // no strtod: "7.4" by hand (newlib's pulls in a lot)
{
	if (i < 0 || t[i].type != J_PRIM) return 0;
	const char *p = s + t[i].start, *e = s + t[i].end;
	int neg = p < e && *p == '-', v = 0, frac = -1;
	for (p += neg; p < e; p++) {
		if (*p == '.') { if (frac >= 0) break; frac = 0; continue; }
		if (*p < '0' || *p > '9') break;
		if (frac < 0) v = v * 10 + (*p - '0');
		else if (frac++ == 0) v = v * 10 + (*p - '0');
		else if (frac == 2 && *p >= '5') v++; // round on the second decimal
	}
	if (frac <= 0) v *= 10;
	return neg ? -v : v;
}

long long json_num(const char *s, const jtok *t, int i)
{
	return i >= 0 && t[i].type == J_PRIM ? strtoll(s + t[i].start, NULL, 10) : 0;
}

// ---- API ----
static int auth_hdr(const jf_conn *c, char *out, int n)
{
	return snprintf(out, n, CLIENT "%s%s%s\r\n", *c->token ? ", Token=\"" : "", c->token, *c->token ? "\"" : "");
}

static int get_json(jf_conn *c, const char *path, char **text, jtok **tok, int *ntok) // GET -> parsed body
{
	http_stream *h = malloc(sizeof(http_stream));
	char hdr[256], full[512];
	int max = 1 << 20; // ponytail: 1 MB answers, a few thousand items with the trimmed fields
	*text = malloc(max), *tok = NULL;
	if (!h || !*text) { free(h); free(*text); return JF_ERR_MEM; }
	auth_hdr(c, hdr, sizeof(hdr));
	snprintf(full, sizeof(full), "%s%s", c->base, path);
	int st = http_open(h, c->host, c->port, "GET", full, hdr, NULL), n = st == 200 ? http_all(h, *text, max) : -1;
	if (st >= 0 && st != 200) http_close(h);
	free(h);
	if (st < 0 || n < 0) { free(*text); return st < 0 ? st : st == 401 ? JF_ERR_AUTH : JF_ERR_HTTP; }
	int cap = n / 6 + 64; // a token needs at least ~6 bytes of JSON
	*tok = malloc(cap * sizeof(jtok));
	*ntok = *tok ? json_parse(*text, n, *tok, cap) : -1;
	if (*ntok < 0) { free(*text); free(*tok); return JF_ERR_JSON; }
	return 0;
}

static void item(const char *s, const jtok *t, int n, int o, jf_item *it)
{
	memset(it, 0, sizeof(*it));
	json_str(s, t, json_key(s, t, n, o, "Name"), it->name, sizeof(it->name));
	json_str(s, t, json_key(s, t, n, o, "Id"), it->id, sizeof(it->id));
	json_str(s, t, json_key(s, t, n, o, "Type"), it->type, sizeof(it->type));
	json_str(s, t, json_key(s, t, n, o, "CollectionType"), it->collection, sizeof(it->collection));
	it->year = json_num(s, t, json_key(s, t, n, o, "ProductionYear"));
	it->season = json_num(s, t, json_key(s, t, n, o, "ParentIndexNumber"));
	it->episode = json_num(s, t, json_key(s, t, n, o, "IndexNumber"));
	it->ticks = json_num(s, t, json_key(s, t, n, o, "RunTimeTicks"));
	it->resume = json_num(s, t, json_key(s, t, n, json_key(s, t, n, o, "UserData"), "PlaybackPositionTicks"));
	it->has_image = json_key(s, t, n, json_key(s, t, n, o, "ImageTags"), "Primary") >= 0;
	json_str(s, t, json_key(s, t, n, o, "OfficialRating"), it->rating, sizeof(it->rating));
	it->score = json_num10(s, t, json_key(s, t, n, o, "CommunityRating"));
	json_str(s, t, json_key(s, t, n, o, "Overview"), it->overview, sizeof(it->overview));
	int g = json_key(s, t, n, o, "Genres");
	if (g >= 0 && t[g].type == J_ARR)
		for (int i = g + 1, k = 0; k < t[g].size && k < 3; k++, i = json_skip(t, n, i)) {
			char one[24];
			int len = strlen(it->genres);
			if (!json_str(s, t, i, one, sizeof(one))) continue;
			if (len + (int)strlen(one) + 4 >= (int)sizeof(it->genres)) break; // only whole names
			snprintf(it->genres + len, sizeof(it->genres) - len, "%s%s", len ? " \xC2\xB7 " : "", one);
		}
}

static int items(jf_conn *c, const char *path, jf_item *out, int max) // the "Items" array of a GET
{
	char *s;
	jtok *t;
	int n, k = 0, r = get_json(c, path, &s, &t, &n);
	if (r < 0) return r;
	int a = json_key(s, t, n, 0, "Items");
	if (a < 0 || t[a].type != J_ARR) k = JF_ERR_JSON;
	else
		for (int i = a + 1, e = 0; e < t[a].size && k < max; e++, i = json_skip(t, n, i))
			if (t[i].type == J_OBJ) item(s, t, n, i, &out[k++]);
	free(s), free(t);
	return k;
}

int jf_login(jf_conn *c, const char *url, const char *user, const char *pw)
{
	char body[256], hdr[256], esc_u[96], esc_p[96], path[64];
	const char *h = url;
	memset(c, 0, sizeof(*c));
	c->port = 8096;
	if (!strncasecmp(h, "http://", 7)) h += 7;
	else if (strstr(h, "://")) return JF_ERR_URL; // https: not on the LAN client (no TLS here)
	int n = strcspn(h, ":/");
	if (!n || n >= (int)sizeof(c->host)) return JF_ERR_URL;
	memcpy(c->host, h, n), h += n;
	if (*h == ':') c->port = strtol(h + 1, (char **)&h, 10);
	snprintf(c->base, sizeof(c->base), "%s", *h == '/' ? h : ""); // reverse-proxy prefix, e.g. /jellyfin
	n = strlen(c->base);
	if (n && c->base[n - 1] == '/') c->base[n - 1] = 0;
	for (int k = 0; k < 2; k++) { // JSON-escape " and backslash
		const char *in = k ? pw : user;
		char *o = k ? esc_p : esc_u;
		int m = 0;
		for (; *in && m < 90; in++) {
			if (*in == '"' || *in == '\\') o[m++] = '\\';
			o[m++] = *in;
		}
		o[m] = 0;
	}
	snprintf(body, sizeof(body), "{\"Username\":\"%s\",\"Pw\":\"%s\"}", esc_u, esc_p);
	auth_hdr(c, hdr, sizeof(hdr));
	snprintf(path, sizeof(path), "%s/Users/AuthenticateByName", c->base);
	http_stream *hs = malloc(sizeof(http_stream));
	char *buf = malloc(64 << 10);
	int st = hs && buf ? http_open(hs, c->host, c->port, "POST", path, hdr, body) : JF_ERR_MEM, r = JF_ERR_JSON;
	if (st == 200 && http_all(hs, buf, 64 << 10) > 0) {
		jtok *t = malloc(4096 * sizeof(jtok));
		int nt = t ? json_parse(buf, strlen(buf), t, 4096) : -1;
		if (nt > 0) {
			json_str(buf, t, json_key(buf, t, nt, 0, "AccessToken"), c->token, sizeof(c->token));
			json_str(buf, t, json_key(buf, t, nt, json_key(buf, t, nt, 0, "User"), "Id"), c->user, sizeof(c->user));
			if (*c->token && *c->user) r = 0;
		}
		free(t);
	} else if (st >= 0) {
		http_close(hs);
		r = st == 401 ? JF_ERR_AUTH : JF_ERR_HTTP;
	} else r = st;
	free(hs), free(buf);
	return r;
}

int jf_views(jf_conn *c, jf_item *out, int max)
{
	char p[128];
	snprintf(p, sizeof(p), "/UserViews?userId=%s", c->user);
	return items(c, p, out, max);
}

// genres and synopsis; no people, media streams, chapters... (ratings and runtime come without asking)
#define TRIM "&EnableImageTypes=Primary&ImageTypeLimit=1&Fields=Genres,Overview"
int jf_items(jf_conn *c, const char *parent, jf_item *out, int max)
{
	char p[320];
	snprintf(p, sizeof(p), "/Items?userId=%s&ParentId=%s&Recursive=true&IncludeItemTypes=Movie,Series&SortBy=SortName"
	         "&SortOrder=Ascending" TRIM, c->user, parent);
	return items(c, p, out, max);
}

int jf_episodes(jf_conn *c, const char *series, jf_item *out, int max)
{
	char p[256];
	snprintf(p, sizeof(p), "/Shows/%s/Episodes?userId=%s" TRIM, series, c->user);
	return items(c, p, out, max);
}

int jf_image(jf_conn *c, const char *id, int w, char *buf, int max)
{
	char p[192], hdr[256];
	http_stream *h = malloc(sizeof(http_stream));
	if (!h) return JF_ERR_MEM;
	snprintf(p, sizeof(p), "%s/Items/%s/Images/Primary?maxWidth=%d&quality=90&format=Jpg", c->base, id, w);
	auth_hdr(c, hdr, sizeof(hdr));
	int st = http_open(h, c->host, c->port, "GET", p, hdr, NULL), n = st == 200 ? http_all(h, buf, max) : -1;
	if (st >= 0 && st != 200) http_close(h);
	free(h);
	return st < 0 ? st : n < 0 ? JF_ERR_HTTP : n;
}

int jf_tracks(jf_conn *c, const char *id, char *source, jf_track *out, int max)
{
	char p[160], *s;
	jtok *t;
	int n, k = 0;
	snprintf(p, sizeof(p), "/Items/%s?userId=%s&Fields=MediaSources", id, c->user);
	int r = get_json(c, p, &s, &t, &n);
	if (r < 0) return r;
	int ms = json_key(s, t, n, 0, "MediaSources"), src = ms >= 0 && t[ms].type == J_ARR && t[ms].size ? ms + 1 : -1;
	json_str(s, t, json_key(s, t, n, src, "Id"), source, 33);
	int st = json_key(s, t, n, src, "MediaStreams");
	if (st >= 0 && t[st].type == J_ARR)
		for (int i = st + 1, e = 0; e < t[st].size && k < max; e++, i = json_skip(t, n, i)) {
			char type[16];
			json_str(s, t, json_key(s, t, n, i, "Type"), type, sizeof(type));
			int text = json_key(s, t, n, i, "IsTextSubtitleStream");
			if (strcmp(type, "Subtitle") || text < 0 || strncmp(s + t[text].start, "true", 4)) continue;
			jf_track *o = &out[k++];
			memset(o, 0, sizeof(*o));
			o->index = json_num(s, t, json_key(s, t, n, i, "Index"));
			json_str(s, t, json_key(s, t, n, i, "Language"), o->lang, sizeof(o->lang));
			json_str(s, t, json_key(s, t, n, i, "DisplayTitle"), o->title, sizeof(o->title));
			int d = json_key(s, t, n, i, "IsDefault"), f = json_key(s, t, n, i, "IsForced");
			o->deflt = d >= 0 && !strncmp(s + t[d].start, "true", 4);
			o->forced = f >= 0 && !strncmp(s + t[f].start, "true", 4);
		}
	free(s), free(t);
	return k;
}

int jf_subtitle(jf_conn *c, const char *id, const char *source, int index, char *buf, int max)
{
	char p[192], hdr[256];
	http_stream *h = malloc(sizeof(http_stream));
	if (!h) return JF_ERR_MEM;
	snprintf(p, sizeof(p), "%s/Videos/%s/%s/Subtitles/%d/0/Stream.srt", c->base, id, source, index);
	auth_hdr(c, hdr, sizeof(hdr));
	int st = http_open(h, c->host, c->port, "GET", p, hdr, NULL), n = st == 200 ? http_all(h, buf, max) : -1;
	if (st >= 0 && st != 200) http_close(h);
	free(h);
	return st < 0 ? st : n < 0 ? JF_ERR_HTTP : n;
}

static int srt_time(const char *p, int *ms) // "hh:mm:ss,mmm" (or '.'); 1 if read
{
	int h, m, sec, f;
	char sep;
	if (sscanf(p, "%d:%d:%d%c%d", &h, &m, &sec, &sep, &f) != 5 || (sep != ',' && sep != '.')) return 0;
	*ms = ((h * 60 + m) * 60 + sec) * 1000 + f;
	return 1;
}

int srt_parse(char *srt, sub_cue *out, int max)
{
	int n = 0;
	char *p = srt;
	if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3; // BOM
	while (*p && n < max) {
		char *arrow = strstr(p, "-->");
		if (!arrow) break;
		char *ls = arrow; // start of the timing line
		while (ls > p && ls[-1] != '\n') ls--;
		sub_cue c;
		if (!srt_time(ls, &c.start) || !srt_time(arrow + 3 + strspn(arrow + 3, " "), &c.end)) { p = arrow + 3; continue; }
		char *q = strchr(arrow, '\n');
		if (!q) break;
		q++;
		char *o = q, *text = q; // copy the text lines down in place, without tags, until an empty line
		int lines = 0, intag = 0;
		while (*q) {
			char *e = q;
			while (*e && *e != '\n' && *e != '\r') e++;
			if (e == q) break; // empty line: end of the cue
			if (lines < 3) {
				if (lines) *o++ = '\n';
				for (char *r = q; r < e; r++) {
					if (*r == '<' || *r == '{') { intag = *r == '<' ? '>' : '}'; continue; }
					if (intag) { if (*r == intag) intag = 0; continue; }
					*o++ = *r;
				}
				lines++;
			}
			q = e;
			if (*q == '\r') q++;
			if (*q == '\n') q++;
		}
		char *rest = q;
		while (*rest == '\r' || *rest == '\n') rest++;
		*o = 0;
		while (o > text && (o[-1] == ' ' || o[-1] == '\n')) *--o = 0;
		c.text = text;
		if (*text) out[n++] = c;
		p = rest;
	}
	for (int i = 1; i < n; i++) // in time order (sources are, mostly; ASS conversions not always)
		for (int j = i; j > 0 && out[j - 1].start > out[j].start; j--) { sub_cue x = out[j]; out[j] = out[j - 1]; out[j - 1] = x; }
	return n;
}

int jf_stream(jf_conn *c, http_stream *h, const char *id, long long start, int vbr)
{
	char p[600], hdr[256];
	static unsigned n;
	// a new PlaySessionId each time: without one Jellyfin hands this device an earlier transcode of the same item
	// (seen in the selftest's server: a 2 MB one, so playback froze after 5 s); jf_report("/Stopped") ends it
	snprintf(c->session, sizeof(c->session), "orbit%08x%04x", (unsigned)time(NULL), ++n & 0xFFFF);
	// mpeg = MPEG-2 program stream; Jellyfin pairs it with MP2 audio whatever audioCodec says (checked: pcm asked,
	// mp2 sent). 640x368 max: ps2sdk libmpeg converts colour in runs of 1023 macroblocks, and its DMA handler that
	// starts the next run swaps the two QWC registers (libmpeg_core.c _mpeg_dmac_handler), so pictures over 1023
	// macroblocks never finish (640x432 = 1080, 640x480 = 1200 hung in PCSX2; 640x368 = 920 plays); 16:9 stays
	// 640x360, 4:3 becomes 496x368. 48 kHz stereo for audsrv.
	snprintf(p, sizeof(p), "%s/Videos/%s/stream.mpeg?static=false&container=mpeg&videoCodec=mpeg2video&audioCodec=mp2"
	         "&maxWidth=640&maxHeight=368&videoBitRate=%d&audioBitRate=192000&audioChannels=2&audioSampleRate=48000"
	         "&startTimeTicks=%lld&SubtitleStreamIndex=-1&PlaySessionId=%s&api_key=%s", c->base, id, vbr, start, c->session, c->token);
	auth_hdr(c, hdr, sizeof(hdr));
	int st = http_open(h, c->host, c->port, "GET", p, hdr, NULL);
	if (st >= 0 && st != 200) http_close(h);
	return st;
}

int jf_report(jf_conn *c, const char *what, const char *id, long long pos)
{
	char p[160], hdr[256], body[192];
	http_stream *h = malloc(sizeof(http_stream));
	if (!h) return JF_ERR_MEM;
	snprintf(p, sizeof(p), "%s/Sessions/Playing%s", c->base, what);
	snprintf(body, sizeof(body), "{\"ItemId\":\"%s\",\"PositionTicks\":%lld,\"PlayMethod\":\"Transcode\","
	         "\"PlaySessionId\":\"%s\"}", id, pos, c->session);
	auth_hdr(c, hdr, sizeof(hdr));
	int st = http_open(h, c->host, c->port, "POST", p, hdr, body);
	if (st >= 0) http_close(h);
	if (!strcmp(what, "/Stopped") && *c->session) { // and the transcode behind it (ffmpeg would run on)
		snprintf(p, sizeof(p), "%s/Videos/ActiveEncodings?deviceId=orbit-ps2&playSessionId=%s", c->base, c->session);
		if (http_open(h, c->host, c->port, "DELETE", p, hdr, NULL) >= 0) http_close(h);
	}
	free(h);
	return st;
}

#ifdef SELFTEST // host check: `make test`; JF_CHECK="http://host:8096 user password" also runs against a live server
#include <assert.h>
int main(int argc, char **argv)
{
	static const char js[] = "{\"Items\":[{\"Name\":\"Pel\\u00EDcula \\u00D1and\\u00FA (1999)\",\"Id\":\"8716\","
		"\"ImageTags\":{\"Primary\":\"7e\"},\"ProductionYear\":1999,\"OfficialRating\":\"B15\",\"CommunityRating\":7.4,"
		"\"Genres\":[\"Acci\\u00F3n\",\"Ciencia ficci\\u00F3n\"],\"UserData\":{\"PlaybackPositionTicks\":42},"
		"\"Empty\":{},\"List\":[1,[2,3],{}],\"Esc\":\"a\\\"b\\\\c\",\"Null\":null}],\"TotalRecordCount\":1}";
	jtok t[64];
	int n = json_parse(js, sizeof(js) - 1, t, 64);
	assert(n > 0 && t[0].type == J_OBJ && t[0].size == 2);
	int a = json_key(js, t, n, 0, "Items"), o = a + 1;
	assert(a > 0 && t[a].type == J_ARR && t[a].size == 1);
	jf_item it;
	item(js, t, n, o, &it);
	assert(!strcmp(it.name, "Pel\xC3\xAD" "cula \xC3\x91" "and\xC3\xBA (1999)") && !strcmp(it.id, "8716"));
	char sm[5];
	assert(json_str(js, t, json_key(js, t, n, o, "Name"), sm, sizeof(sm)) && !strcmp(sm, "Pel")); // no half of í
	assert(json_str(js, t, json_key(js, t, n, o, "Id"), sm, sizeof(sm)) && !strcmp(sm, "8716"));   // exact fit
	assert(it.year == 1999 && it.resume == 42 && it.has_image);
	assert(!strcmp(it.rating, "B15") && it.score == 74 && !strcmp(it.genres, "Acci\xC3\xB3n \xC2\xB7 Ciencia ficci\xC3\xB3n"));
	char e[16];
	assert(json_str(js, t, json_key(js, t, n, o, "Esc"), e, sizeof(e)) && !strcmp(e, "a\"b\\c"));
	assert(json_num(js, t, json_key(js, t, n, 0, "TotalRecordCount")) == 1 && json_key(js, t, n, o, "Nope") < 0);
	assert(json_parse("{\"a\":[1,2}", 10, t, 64) < 0 && json_parse(js, sizeof(js) - 1, t, 5) < 0);
	char srt[] = "\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:04,500\r\n<i>Hola</i> desde\r\n{\\an8}Jellyfin\r\n\r\n"
		"2\n00:01:02.250 --> 00:01:03.000\nUna, dos,\ntres, cuatro\nlineas\n\n"
		"3\n00:00:00,500 --> 00:00:00,900\n\n" // no text: dropped
		"4\n00:00:00,100 --> 00:00:00,400\nantes\n";
	sub_cue q[8];
	int nq = srt_parse(srt, q, 8);
	assert(nq == 3 && q[0].start == 100 && !strcmp(q[0].text, "antes"));
	assert(q[1].start == 1000 && q[1].end == 4500 && !strcmp(q[1].text, "Hola desde\nJellyfin"));
	assert(q[2].start == 62250 && !strcmp(q[2].text, "Una, dos,\ntres, cuatro\nlineas"));
	if (argc > 3) { // live: login, libraries, items, episodes, a poster, the first 2 MB of a stream
		jf_conn c;
		jf_item v[16], m[64];
		static char buf[1 << 20];
		int r = jf_login(&c, argv[1], argv[2], argv[3]);
		printf("login %d user %s\n", r, c.user);
		assert(r == 0);
		int nv = jf_views(&c, v, 16);
		assert(nv > 0);
		for (int i = 0; i < nv; i++) {
			int nm = jf_items(&c, v[i].id, m, 64);
			printf("view %s (%s): %d items\n", v[i].name, v[i].collection, nm);
			for (int k = 0; k < nm; k++) {
				printf("  %s %s %d img %d %lld s\n", m[k].type, m[k].name, m[k].year, m[k].has_image, m[k].ticks / 10000000);
				if (!strcmp(m[k].type, "Series")) {
					jf_item ep[32];
					int ne = jf_episodes(&c, m[k].id, ep, 32);
					for (int j = 0; j < ne; j++) printf("    S%02dE%02d %s\n", ep[j].season, ep[j].episode, ep[j].name);
					assert(ne > 0);
				}
				if (!strcmp(m[k].type, "Movie")) {
					jf_track tr[8];
					char src[33];
					int nt = jf_tracks(&c, m[k].id, src, tr, 8);
					for (int j = 0; j < nt; j++) {
						int sz = jf_subtitle(&c, m[k].id, src, tr[j].index, buf, sizeof(buf));
						sub_cue cq[64];
						int ncq = sz > 0 ? srt_parse(buf, cq, 64) : 0;
						printf("    subtitle %d %s \"%s\": %d bytes, %d cues, first \"%s\"\n", tr[j].index, tr[j].lang, tr[j].title,
						       sz, ncq, ncq ? cq[0].text : "");
						assert(sz > 0 && ncq > 0);
					}
				}
				if (m[k].has_image) {
					int ni = jf_image(&c, m[k].id, 384, buf, sizeof(buf));
					printf("    poster %d bytes%s\n", ni, ni > 2 && (unsigned char)buf[0] == 0xFF && (unsigned char)buf[1] == 0xD8 ? " (JPEG)" : "");
					assert(ni > 2 && (unsigned char)buf[0] == 0xFF);
				}
				if (!strcmp(m[k].type, "Movie") && argc > 4) { // JF_OUT: where to write the stream's first 2 MB
					http_stream h;
					int st = jf_stream(&c, &h, m[k].id, 0, 3000000), got = 0, rr;
					while (st == 200 && got < (2 << 20) && (rr = http_read(&h, buf, sizeof(buf))) > 0) {
						FILE *f = fopen(argv[4], got ? "ab" : "wb");
						fwrite(buf, 1, rr, f), fclose(f);
						got += rr;
					}
					if (st == 200) http_close(&h);
					printf("    stream %d: %d bytes, pack header %s\n", st, got, got ? "written" : "-");
					assert(st == 200 && got > 0);
					assert(jf_report(&c, "", m[k].id, 0) == 204 && jf_report(&c, "/Stopped", m[k].id, 10000000) == 204);
					argc = 4; // one stream is enough
				}
			}
		}
		c.token[0] = 'x';
		assert(jf_views(&c, v, 16) == JF_ERR_AUTH);
	}
	puts("jellyfin selftest ok");
	return 0;
}
#endif
