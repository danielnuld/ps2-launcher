#pragma once
#define COVER_W 256  // <serial>.c16 (cover-art spec)
#define COVER_H 368
#define COVER_SW 184 // <serial>_s.c16
#define COVER_SH 264
// JPEG bytes -> both cover sizes as CT16 pixels (no .c16 header); 0 if the file cannot be decoded
int cover_from_jpeg(const unsigned char *jpg, int n, unsigned short *big, unsigned short *small);
