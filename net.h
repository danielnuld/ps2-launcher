#pragma once
enum { NET_ERR_MODULES = -1, NET_ERR_LINK = -2, NET_ERR_DHCP = -3, NET_ERR_DNS = -4, NET_ERR_CONNECT = -5,
       NET_ERR_TLS = -6, NET_ERR_PROTO = -7, NET_ERR_BUSY = -8 };

int net_up(const char *ip, const char *mask, const char *gw, const char *dns); // ip "dhcp" or dotted; 0 or NET_ERR_*
// GET https://<host><path>, whole response into buf. Returns the HTTP status, or NET_ERR_*; on 200 the body is
// buf[*body .. *body + *len) and was checked against Content-Length.
int https_get(const char *host, const char *path, char *buf, int max, int *body, int *len);
int http_parse(const char *buf, int n, int *status, int *body, int *clen); // 1 if a full header was found
