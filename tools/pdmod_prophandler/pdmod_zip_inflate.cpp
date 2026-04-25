/**
 * pdmod_zip_inflate.cpp -- Priority M / B-238 / M-2.1
 *
 * Standalone minimal zip reader (in-memory) for PD2ModPropHandler.dll.
 * Mirrors the central-directory + raw-deflate logic in the engine's
 * port/src/modarchive.c at a much smaller surface (single-entry lookup,
 * no writer, no name pool, no trust checks beyond traversal refusal).
 */

#include "pdmod_zip_inflate.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <zlib.h>

#define EOCD_SIG       0x06054B50u
#define CDH_SIG        0x02014B50u
#define LFH_SIG        0x04034B50u
#define EOCD_MIN       22
#define MAX_ZIP_COMMENT 0xFFFF

#define COMPRESSION_STORED   0
#define COMPRESSION_DEFLATED 8
#define ZIP_FLAG_ENCRYPTED   0x0001u

typedef struct pdmod_zip {
	uint8_t *bytes;       /* heap copy owned by us */
	uint32_t size;
	uint32_t cd_offset;
	uint32_t cd_size;
	uint32_t entry_count;
} pdmod_zip_t;

static uint16_t rdU16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rdU32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int safeName(const char *name, uint16_t len)
{
	if (len == 0) return 0;
	if (name[0] == '/' || name[0] == '\\') return 0;
	if (len >= 2 && isalpha((unsigned char)name[0]) && name[1] == ':') return 0;
	for (uint16_t i = 0; i < len; i++) {
		if (name[i] == 0) return 0;
		if (name[i] == '\\') {
			/* Reader treats `\` as forward slash for comparison purposes
			 * but we just refuse archives that mix separators rather than
			 * normalising in this minimal reader. */
			return 0;
		}
		if (name[i] == '.' && i + 1 < len && name[i + 1] == '.') {
			/* `..` segment somewhere in the path -- refuse. */
			if ((i == 0 || name[i - 1] == '/') &&
			    (i + 2 == len || name[i + 2] == '/')) {
				return 0;
			}
		}
	}
	return 1;
}

pdmod_zip_t *pdmodZipOpenMemory(const void *bytes, uint32_t size)
{
	if (!bytes || size < EOCD_MIN) return NULL;

	pdmod_zip_t *z = (pdmod_zip_t *)calloc(1, sizeof(*z));
	if (!z) return NULL;

	z->bytes = (uint8_t *)malloc(size);
	if (!z->bytes) { free(z); return NULL; }
	memcpy(z->bytes, bytes, size);
	z->size = size;

	/* Locate EOCD by scanning the last 64 KiB + 22 bytes. */
	uint32_t scanLen = (size < (uint32_t)(EOCD_MIN + MAX_ZIP_COMMENT))
		? size : (uint32_t)(EOCD_MIN + MAX_ZIP_COMMENT);
	uint32_t scanStart = size - scanLen;
	int found = 0;
	uint32_t eocdOff = 0;
	for (int64_t i = (int64_t)scanLen - EOCD_MIN; i >= 0; i--) {
		const uint8_t *p = &z->bytes[scanStart + i];
		if (rdU32(p) == EOCD_SIG) {
			uint16_t commentLen = rdU16(p + 20);
			if ((uint32_t)i + EOCD_MIN + commentLen == scanLen) {
				found = 1;
				eocdOff = scanStart + (uint32_t)i;
				break;
			}
		}
	}
	if (!found) {
		pdmodZipClose(z);
		return NULL;
	}

	const uint8_t *eocd = &z->bytes[eocdOff];
	uint16_t totalEntries = rdU16(eocd + 10);
	uint32_t cdSize  = rdU32(eocd + 12);
	uint32_t cdOff   = rdU32(eocd + 16);

	if (totalEntries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) {
		pdmodZipClose(z);
		return NULL;
	}
	if ((uint64_t)cdOff + cdSize > size) {
		pdmodZipClose(z);
		return NULL;
	}

	z->cd_offset = cdOff;
	z->cd_size = cdSize;
	z->entry_count = totalEntries;
	return z;
}

void pdmodZipClose(pdmod_zip_t *z)
{
	if (!z) return;
	free(z->bytes);
	free(z);
}

int pdmodZipExtractToMalloc(pdmod_zip_t *z, const char *name,
                             void **outBuf, uint32_t *outSize)
{
	if (!z || !name || !outBuf || !outSize) return 0;
	*outBuf = NULL;
	*outSize = 0;

	uint32_t walked = 0;
	size_t targetLen = strlen(name);
	while (walked + 46 <= z->cd_size) {
		const uint8_t *p = &z->bytes[z->cd_offset + walked];
		if (rdU32(p) != CDH_SIG) break;

		uint16_t flags    = rdU16(p + 8);
		uint16_t method   = rdU16(p + 10);
		uint32_t csize    = rdU32(p + 20);
		uint32_t usize    = rdU32(p + 24);
		uint16_t nameLen  = rdU16(p + 28);
		uint16_t extraLen = rdU16(p + 30);
		uint16_t cmtLen   = rdU16(p + 32);
		uint32_t lfhOff   = rdU32(p + 42);
		uint32_t recordLen = (uint32_t)46 + nameLen + extraLen + cmtLen;
		if (walked + recordLen > z->cd_size) break;

		const char *entryName = (const char *)(p + 46);
		if (!(flags & ZIP_FLAG_ENCRYPTED) &&
		    (method == COMPRESSION_STORED || method == COMPRESSION_DEFLATED) &&
		    safeName(entryName, nameLen) &&
		    nameLen == targetLen &&
		    memcmp(entryName, name, targetLen) == 0) {

			/* Locate compressed payload via LFH at lfhOff. */
			if ((uint64_t)lfhOff + 30 > z->size) return 0;
			const uint8_t *lfh = &z->bytes[lfhOff];
			if (rdU32(lfh) != LFH_SIG) return 0;
			uint16_t lfhNameLen  = rdU16(lfh + 26);
			uint16_t lfhExtraLen = rdU16(lfh + 28);
			uint64_t dataOff = (uint64_t)lfhOff + 30 + lfhNameLen + lfhExtraLen;
			if (dataOff + csize > z->size) return 0;

			void *out = malloc((size_t)usize + 1);
			if (!out) return 0;

			if (method == COMPRESSION_STORED) {
				if (csize != usize) { free(out); return 0; }
				memcpy(out, z->bytes + dataOff, usize);
			} else {
				z_stream zs;
				memset(&zs, 0, sizeof(zs));
				if (inflateInit2(&zs, -15) != Z_OK) { free(out); return 0; }
				zs.next_in = (Bytef *)(z->bytes + dataOff);
				zs.avail_in = csize;
				zs.next_out = (Bytef *)out;
				zs.avail_out = usize;
				int r = inflate(&zs, Z_FINISH);
				inflateEnd(&zs);
				if (r != Z_STREAM_END || zs.total_out != usize) { free(out); return 0; }
			}
			((uint8_t *)out)[usize] = 0;
			*outBuf = out;
			*outSize = usize;
			return 1;
		}

		walked += recordLen;
	}

	return 0;
}
