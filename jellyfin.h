#pragma once
// Jellyfin client (phase 15): plain HTTP/1.1 on the LAN (BSD sockets: lwIP on the PS2, the host's for the selftest),
// a small JSON tokenizer, and the few API calls the launcher needs. Checked against Jellyfin 12.1 (docs/sources.md).

// ---- HTTP ----
typedef struct {
	int s, status, chunked, eof;
	long long left;          // bytes left in the body (Content-Length) or in the current chunk; -1 = to EOF
	int pos, len;            // buffered bytes buf[pos..len)
	char buf[4096];
} http_stream;
// method "GET" / "POST"; extra: more header lines ("Name: v\r\n"...) or NULL; body NULL for none. Returns the status,
// or < 0 (JF_ERR_*). On >= 0 the body is read with http_read and the stream closed with http_close.
int http_open(http_stream *h, const char *host, int port, const char *method, const char *path, const char *extra,
              const char *body);
int http_read(http_stream *h, void *dst, int n); // body bytes (chunks decoded); 0 = end, < 0 = error
void http_close(http_stream *h);

// ---- JSON (jsmn-style tokens over the text, no copies) ----
enum { J_OBJ = 1, J_ARR, J_STR, J_PRIM };
typedef struct { unsigned char type; int start, end, size; } jtok; // size: members (objects: keys) / elements
int json_parse(const char *s, int n, jtok *t, int max);              // token count, or -1 (bad / too many)
int json_skip(const jtok *t, int n, int i);                          // the token after i's subtree
int json_key(const char *s, const jtok *t, int n, int obj, const char *key); // value token of key in obj, or -1
int json_str(const char *s, const jtok *t, int i, char *out, int max); // unescaped UTF-8 (\uXXXX too); 0 if not a string
long long json_num(const char *s, const jtok *t, int i);               // 0 if missing / not a number
int json_num10(const char *s, const jtok *t, int i);                   // a decimal x10, rounded (7.4 -> 74); 0 if missing

// ---- API ----
enum { JF_ERR_URL = -20, JF_ERR_CONNECT = -21, JF_ERR_HTTP = -22, JF_ERR_AUTH = -23, JF_ERR_JSON = -24, JF_ERR_MEM = -25 };
typedef struct {
	char host[64], base[32], token[48], user[48], session[24]; // session: PlaySessionId of the last jf_stream
	int port;
} jf_conn;
typedef struct {
	char name[96], id[33], type[16], collection[16]; // type: Movie, Series, Episode, CollectionFolder...
	int year, season, episode, has_image;
	long long ticks, resume; // runtime and saved position, 100 ns units
	char rating[12];         // OfficialRating, the age rating as the server has it ("B15", "PG-13", "TV-MA"); "" none
	int score;               // CommunityRating x10 (7.4 -> 74); 0 = none
	char genres[48];         // the first genres, joined with " · "
	char overview[320];      // synopsis, cut to fit
} jf_item;
int jf_login(jf_conn *c, const char *url, const char *user, const char *pw); // 0 or JF_ERR_*
int jf_views(jf_conn *c, jf_item *out, int max);                         // libraries; count or JF_ERR_*
int jf_items(jf_conn *c, const char *parent, jf_item *out, int max);     // movies and series of a library
int jf_episodes(jf_conn *c, const char *series, jf_item *out, int max);  // all episodes, in order
int jf_image(jf_conn *c, const char *id, int w, char *buf, int max);     // Primary image as JPEG, w px wide; bytes
// Transcoded stream the PS2 can decode: MPEG-2 video (IPU, libmpeg) + MP2 audio in an MPEG program stream, at most
// 640x368 (libmpeg hung on taller pictures in PCSX2) and vbr bits/s; start in 100 ns units. Returns the HTTP status (200) or JF_ERR_*.
int jf_stream(jf_conn *c, http_stream *h, const char *id, long long start, int vbr);
// Playback reports so Jellyfin keeps "continue watching": what = "" (start), "/Progress", "/Stopped"
int jf_report(jf_conn *c, const char *what, const char *id, long long pos);
