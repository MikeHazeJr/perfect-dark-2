#ifndef _IN_UTILS_H
#define _IN_UTILS_H

#define UTIL_MAX_TOKEN 1024

#include <PR/ultratypes.h>

/* CRC32 checksum of a null-terminated string (standard 0x04C11DB7 polynomial).
 * Used for ROM identity validation in network auth. */
u32 utilCrc32(const char *str);

char *strRightTrim(char *str);
char *strTrim(char *str);
char *strUnquote(char *str);
char *strParseToken(char *str, char *out, s32 *outCount);
char *strFmt(const char *fmt, ...);
char *strDuplicate(const char *str);

static inline f32 lerpf(const f32 a, const f32 b, const f32 t) {
	return a + (b - a) * t;
}

static inline f32 lerpanglef(const f32 a, const f32 b, const f32 t) {
	f32 d = b - a;
	if (d > 180.f) {
		d -= 360.f;
	} else if (d < -180.f) {
    d += 360.f;
  }
	return a + d * t;
}

#endif
