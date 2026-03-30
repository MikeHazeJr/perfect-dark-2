#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <PR/ultratypes.h>
#include "platform.h"
#include "system.h"
#include "utils.h"

static inline bool isSingleCharToken(const s32 ch)
{
	switch (ch) {
		case '{':
		case '}':
		case '[':
		case ']':
		case '(':
		case ')':
		case ',':
		case '\'':
		case '=':
			return true;
		default:
			return false;
	}
}

/* CRC32 lookup table — standard 0x04C11DB7 reflected polynomial */
static const u32 s_Crc32Table[256] = {
    0x00000000,0x77073096,0xee0e612c,0x990951ba,0x076dc419,0x706af48f,0xe963a535,0x9e6495a3,
    0x0edb8832,0x79dcb8a4,0xe0d5e91b,0x97d2d988,0x09b64c2b,0x7eb17cbf,0xe7b82d09,0x90bf1595,
    0x1db71064,0x6ab020f2,0xf3b97148,0x84be41de,0x1adad47d,0x6ddde4eb,0xf4d4b551,0x83d385c7,
    0x136c9856,0x646ba8c0,0xfd62f97a,0x8a65c9ec,0x14015c4f,0x63066cd9,0xfa0f3d63,0x8d080df5,
    0x3b6e20c8,0x4c69105e,0xd56041e4,0xa2677172,0x3c03e4d1,0x4b04d447,0xd20d85fd,0xa50ab56b,
    0x35b5a8fa,0x42b2986c,0xdbbbc9d6,0xacbcb9c0,0x32d86ce3,0x45df5c75,0xdcd60dcf,0xabd13d59,
    0x26d930ac,0x51de003a,0xc8d75180,0xbfd06116,0x21b4f928,0x56b3c9be,0xcfba9599,0xb8bda50f,
    0x2802b89e,0x5f058808,0xc60cd9b2,0xb10be924,0x2f6f7c87,0x58684c11,0xc1611dab,0xb6662d3d,
    0x76dc4190,0x01db7106,0x98d220bc,0xefd5102a,0x71b18589,0x06b6b51f,0x9fbfe4a5,0xe8b8d433,
    0x7807c9a2,0x0f00f934,0x9609a88e,0xe10e9818,0x7f6ad9bb,0x086d3d2d,0x91646c97,0xe6635c01,
    0x6b6b51f4,0x1c6c6162,0x856530d8,0xf262004e,0x6c0695ed,0x1b01a57b,0x8208f4c1,0xf50fc457,
    0x65b0d9c6,0x12b7e950,0x8bbeb8ea,0xfcb9887c,0x62dd1ddf,0x15da2d49,0x8cd37cf3,0xfbd44c65,
    0x4db26158,0x3ab551ce,0xa3bc0074,0xd4bb30e2,0x4adfa541,0x3dd895d7,0xa4d1c46d,0xd3d6f4fb,
    0x4369e96a,0x346ed9fc,0xad678846,0xda60b8d0,0x44042d73,0x33031de5,0xaa0a4c5f,0xdd0d7cc9,
    0x5005713c,0x270241aa,0xbe0b1010,0xc90c2086,0x5768b525,0x206f85b3,0xb966d409,0xce61e49f,
    0x5edef90e,0x29d9c998,0xb0d09822,0xc7d7a8b4,0x59b33d17,0x2eb40d81,0xb7bd5c3b,0xc0ba6cad,
    0xedb88320,0x9abfb3b6,0x03b6e20c,0x74b1d29a,0xead54739,0x9dd277af,0x04db2615,0x73dc1683,
    0xe3630b12,0x94643b84,0x0d6d6a3e,0x7a6a5aa8,0xe40ecf0b,0x9309ff9d,0x0a00ae27,0x7d079eb1,
    0xf00f9344,0x8708a3d2,0x1e01f268,0x6906c2fe,0xf762575d,0x806567cb,0x196c3671,0x6e6b06e7,
    0xfed41b76,0x89d32be0,0x10da7a5a,0x67dd4acc,0xf9b9df6f,0x8ebeeff9,0x17b7be43,0x60b08ed5,
    0xd6d6a3e8,0xa1d1937e,0x38d8c2c4,0x4fdff252,0xd1bb67f1,0xa6bc5767,0x3fb506dd,0x48b2364b,
    0xd80d2bda,0xaf0a1b4c,0x36034af6,0x41047a60,0xdf60efc3,0xa8670955,0x316658ef,0x46616879,
    0xb40bbe37,0xc30c8ea1,0x5a05df1b,0x2d02ef8d
};

u32 utilCrc32(const char *str)
{
    u32 crc = 0xffffffff;
    while (*str) {
        crc = (crc >> 8) ^ s_Crc32Table[(crc ^ (u8)*str++) & 0xff];
    }
    return crc ^ 0xffffffff;
}

char *strFmt(const char *fmt, ...)
{
	static char buf[4096];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	return buf;
}

char *strRightTrim(char *str)
{
	if (!str) {
		return NULL;
	}

	const s32 len = strlen(str);
	for (s32 i = len - 1; i >= 0 && isspace(str[i]); --i) {
		str[i] = '\0';
	}

	return str;
}

char *strTrim(char *str)
{
	if (!str) {
		return NULL;
	}

	// left trim
	while (*str && (u8)*str < ' ') {
		++str;
	}

	// right trim
	const s32 len = strlen(str);
	for (s32 i = len - 1; i > 0 && isspace(str[i]); --i) {
		str[i] = '\0';
	}

	return str;
}

char *strUnquote(char *str)
{
	if (!str) {
		return NULL;
	}

	if (*str == '"') {
		++str;
	}

	char *end = strrchr(str, '"');
	if (end && end[1] == '\0') {
		*end = '\0';
	}

	return str;
}

char *strParseToken(char *str, char *out, s32 *outCount)
{
	if (outCount) {
		*outCount = 0;
	}

	if (!out) {
		return NULL;
	}

	out[0] = '\0';

	if (!str) {
		return NULL;
	}

	s32 cnt = 0;
	while (*str) {
		// skip whitespace and other garbage
		while (*str && (u8)*str <= ' ') {
			++str;
		}
		if (!*str) {
			// just whitespace
			return NULL;
		}

		// skip #, ; and // comments
		if (*str == ';' || *str == '#' || (str[0] == '/' && str[1] == '/')) {
			while (*str && *str != '\n') {
				++str;
			}
			continue;
		}

		if (*str == '"') {
			// quoted string; treat it as an entire token, including the quotes
			out[cnt++] = *str++;
			while (true) {
				if (*str == '\0') {
					// unterminated quoted string
					break;
				}

				if (str[0] == '\\' && str[1] == '"') {
					// escaped quote; add to token
					if (cnt + 1 < UTIL_MAX_TOKEN) {
						out[cnt++] = str[1];
					}
					str += 2;
					continue;
				}

				// add char to token
				const s32 ch = (u8)*str++;
				if (cnt + 1 < UTIL_MAX_TOKEN) {
					out[cnt++] = ch;
				}

				if (ch == '"') {
					// ch was the closing quote
					break;
				}
			}
			break;
		}

		if (isSingleCharToken(*str)) {
			// single char token
			out[cnt++] = *str++;
			break;
		}

		// regular single word
		do {
			if (cnt + 1 < UTIL_MAX_TOKEN) {
				out[cnt++] = *str++;
			}
			if (isSingleCharToken(*str)) {
				break;
			}
		} while ((u8)*str > ' ');
		break;
	}

	out[cnt] = '\0';
	if (outCount) {
		*outCount = cnt;
	}

	return str;
}

char *strDuplicate(const char *str)
{
	if (!str) {
		return NULL;
	}
	const u32 len = strlen(str);
	char *out = sysMemAlloc(len + 1);
	if (out) {
		memcpy(out, str, len + 1);
	}
	return out;
}