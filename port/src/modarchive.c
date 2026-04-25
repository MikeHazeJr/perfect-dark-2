/**
 * modarchive.c -- Priority M / B-238 zip reader+writer for `.pdmod`.
 *
 * Implements the public API in port/include/modarchive.h. Routes deflate /
 * inflate through the system zlib (already linked via libz.a in CMake).
 *
 * Design choices worth pointing out:
 *
 *   - Reader keeps a FILE* open per archive and seeks/reads on demand. The
 *     central directory is parsed once at open time (small, bounded cost);
 *     entry payloads are decompressed lazily on extract calls.
 *
 *   - All entry names are sanitised once at parse time. Anything containing
 *     `..` segments, a leading `/` or `\`, or a drive prefix is rejected at
 *     archive open. Callers downstream can trust the names returned by
 *     modArchiveGetEntryName / FindEntry are safe relative paths.
 *
 *   - Writer streams to `<path>.tmp` and renames on Finish so a crash mid-
 *     save cannot half-overwrite an existing `.pdmod`. This is the same
 *     pattern modpack.c uses for `.pdpack`.
 *
 *   - ZIP64 is unsupported. The EOCD parser refuses sentinel values with a
 *     MODARCHIVE_ERR_LIMIT code rather than misinterpreting them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <zlib.h>
#include <PR/ultratypes.h>

#include "fs.h"
#include "system.h"
#include "sha256.h"
#include "modarchive.h"

/* -------------------------------------------------------- Local definitions */

#define EOCD_SIG       0x06054B50u  /* "PK\x05\x06" */
#define CDH_SIG        0x02014B50u  /* "PK\x01\x02" */
#define LFH_SIG        0x04034B50u  /* "PK\x03\x04" */

#define EOCD_MIN_SIZE  22
#define MAX_ZIP_COMMENT 0xFFFF
#define EOCD_SCAN_MAX  (EOCD_MIN_SIZE + MAX_ZIP_COMMENT)

#define COMPRESSION_STORED   0
#define COMPRESSION_DEFLATED 8

#define ZIP_FLAG_ENCRYPTED   0x0001u

#define INFLATE_CHUNK 16384

struct mod_archive_entry {
	char *name;             /* points into names_pool, NUL-terminated */
	u32   size_compressed;
	u32   size_uncompressed;
	u32   crc32;
	u32   lfh_offset;
	u16   method;
};

struct mod_archive {
	char  path[FS_MAXPATH + 1];
	FILE *fp;
	u32   file_size;
	s32   entry_count;
	struct mod_archive_entry *entries;
	char *names_pool;        /* freed at close */
	u32   names_pool_used;
	char *comment;           /* malloc'd, NUL-terminated. May be empty string. */
};

/* Last-error storage. We keep a single thread-local-ish global since the
 * only call sites are the engine's main thread + the asset cache worker
 * (single-threaded for now). */
static __thread s32 s_lastError = MODARCHIVE_OK;

s32 modArchiveLastError(void)
{
	return s_lastError;
}

static void setError(s32 code)
{
	s_lastError = code;
}

/* Read u16/u32 little-endian from a byte buffer. */
static u16 readU16LE(const u8 *p)
{
	return (u16)(p[0] | (p[1] << 8));
}

static u32 readU32LE(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void writeU16LE(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xFF);
	p[1] = (u8)((v >> 8) & 0xFF);
}

static void writeU32LE(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xFF);
	p[1] = (u8)((v >> 8) & 0xFF);
	p[2] = (u8)((v >> 16) & 0xFF);
	p[3] = (u8)((v >> 24) & 0xFF);
}

/* ---------------------------------------------------- Path-name sanitisation */

/* Returns 1 if the name is safe to retain (no traversal, no absolute path,
 * no embedded NUL). The buffer is rewritten in place to use forward slashes
 * and any trailing slash is stripped. Empty names and pure-slash names are
 * rejected. The caller-allocated buffer must hold a NUL after position
 * `len`; this function never extends the string. */
static int sanitiseEntryName(char *name, u32 len)
{
	if (len == 0 || name[0] == '\0') return 0;
	if (name[0] == '/' || name[0] == '\\') return 0;
	if (len >= 2 && isalpha((unsigned char)name[0]) && name[1] == ':') return 0;

	/* Single forward pass: convert backslashes, validate each segment. */
	u32 segStart = 0;
	for (u32 i = 0; i <= len; i++) {
		if (i < len && name[i] == '\\') name[i] = '/';
		char c = (i < len) ? name[i] : '\0';
		if (c == '/' || c == '\0') {
			u32 segLen = i - segStart;
			/* Empty segment is only OK when it is the trailing slash
			 * of a directory entry (i.e. final iteration, name has at
			 * least one prior character). Otherwise it means `//` which
			 * we refuse. */
			if (segLen == 0) {
				if (c == '/' && i + 1 < len) return 0;
			}
			if (segLen == 1 && name[segStart] == '.') return 0;
			if (segLen == 2 && name[segStart] == '.' && name[segStart + 1] == '.') return 0;
			segStart = i + 1;
		}
	}

	/* Strip a single trailing slash. Multiple trailing slashes were already
	 * rejected by the empty-segment check above. */
	if (len > 0 && name[len - 1] == '/') {
		name[len - 1] = '\0';
		len--;
	}
	return len > 0;
}

/* -------------------------------------------------------------- EOCD search */

/* Scans the last (EOCD_MIN_SIZE + 65535) bytes of the file for the EOCD
 * signature, returning the offset where it was found. Sets last-error on
 * failure. */
static s64 findEocdOffset(FILE *fp, u32 fileSize)
{
	if (fileSize < EOCD_MIN_SIZE) {
		setError(MODARCHIVE_ERR_FORMAT);
		return -1;
	}

	u32 scanLen = (fileSize < EOCD_SCAN_MAX) ? fileSize : EOCD_SCAN_MAX;
	u32 scanStart = fileSize - scanLen;

	u8 *buf = (u8 *)malloc(scanLen);
	if (!buf) {
		setError(MODARCHIVE_ERR_MEM);
		return -1;
	}

	if (fseek(fp, (long)scanStart, SEEK_SET) != 0) {
		free(buf);
		setError(MODARCHIVE_ERR_IO);
		return -1;
	}
	if (fread(buf, 1, scanLen, fp) != scanLen) {
		free(buf);
		setError(MODARCHIVE_ERR_IO);
		return -1;
	}

	/* Walk backwards from the latest possible EOCD start position. */
	s64 found = -1;
	for (s64 i = (s64)scanLen - EOCD_MIN_SIZE; i >= 0; i--) {
		if (readU32LE(&buf[i]) != EOCD_SIG) {
			continue;
		}
		/* Verify the comment_len field is consistent with end-of-file. */
		u16 commentLen = readU16LE(&buf[i + 20]);
		if ((u32)i + EOCD_MIN_SIZE + commentLen == scanLen) {
			found = (s64)scanStart + i;
			break;
		}
	}

	free(buf);
	if (found < 0) {
		setError(MODARCHIVE_ERR_FORMAT);
	}
	return found;
}

/* ----------------------------------------------------------- Reader: open */

mod_archive_t *modArchiveOpen(const char *path)
{
	if (!path || !path[0]) {
		setError(MODARCHIVE_ERR_OPEN);
		return NULL;
	}

	FILE *fp = fopen(path, "rb");
	if (!fp) {
		setError(MODARCHIVE_ERR_OPEN);
		return NULL;
	}

	struct stat st;
	if (stat(path, &st) != 0 || st.st_size <= 0) {
		fclose(fp);
		setError(MODARCHIVE_ERR_OPEN);
		return NULL;
	}
	if ((u64)st.st_size > 0xFFFFFFFFull) {
		fclose(fp);
		setError(MODARCHIVE_ERR_LIMIT);
		return NULL;
	}
	u32 fileSize = (u32)st.st_size;

	s64 eocdOffset = findEocdOffset(fp, fileSize);
	if (eocdOffset < 0) {
		fclose(fp);
		return NULL;
	}

	/* Read the EOCD record + its trailing comment. */
	u8 eocd[EOCD_MIN_SIZE];
	if (fseek(fp, (long)eocdOffset, SEEK_SET) != 0 ||
	    fread(eocd, 1, EOCD_MIN_SIZE, fp) != EOCD_MIN_SIZE) {
		fclose(fp);
		setError(MODARCHIVE_ERR_IO);
		return NULL;
	}

	u16 totalEntries = readU16LE(&eocd[10]);
	u32 cdSize       = readU32LE(&eocd[12]);
	u32 cdOffset     = readU32LE(&eocd[16]);
	u16 commentLen   = readU16LE(&eocd[20]);

	/* ZIP64 sentinels -- refuse rather than misinterpret. */
	if (totalEntries == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu) {
		fclose(fp);
		setError(MODARCHIVE_ERR_LIMIT);
		return NULL;
	}

	/* Read the comment now while we have the file pointer near it. */
	char *commentBuf = (char *)malloc(commentLen + 1);
	if (!commentBuf) {
		fclose(fp);
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}
	commentBuf[0] = '\0';
	if (commentLen > 0) {
		if (fread(commentBuf, 1, commentLen, fp) != commentLen) {
			free(commentBuf);
			fclose(fp);
			setError(MODARCHIVE_ERR_IO);
			return NULL;
		}
	}
	commentBuf[commentLen] = '\0';

	/* Read the central directory in one shot. */
	u8 *cdBuf = (u8 *)malloc(cdSize);
	if (!cdBuf) {
		free(commentBuf);
		fclose(fp);
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}
	if (fseek(fp, (long)cdOffset, SEEK_SET) != 0 ||
	    fread(cdBuf, 1, cdSize, fp) != cdSize) {
		free(cdBuf);
		free(commentBuf);
		fclose(fp);
		setError(MODARCHIVE_ERR_IO);
		return NULL;
	}

	/* First pass: walk CDH records to count valid entries + total name bytes. */
	u32 walked = 0;
	u32 namesBytes = 0;
	s32 validEntries = 0;
	while (walked + 46 <= cdSize) {
		u8 *p = &cdBuf[walked];
		if (readU32LE(p) != CDH_SIG) {
			break;
		}
		u16 nameLen   = readU16LE(&p[28]);
		u16 extraLen  = readU16LE(&p[30]);
		u16 cmtLen    = readU16LE(&p[32]);
		u32 recordLen = (u32)46 + nameLen + extraLen + cmtLen;
		if (walked + recordLen > cdSize) {
			break;
		}
		validEntries++;
		namesBytes += (u32)nameLen + 1;
		walked += recordLen;
	}

	if (validEntries == 0) {
		free(cdBuf);
		free(commentBuf);
		fclose(fp);
		setError(MODARCHIVE_ERR_FORMAT);
		return NULL;
	}

	/* Allocate the archive struct + entry table + names pool. */
	mod_archive_t *arc = (mod_archive_t *)calloc(1, sizeof(*arc));
	if (!arc) {
		free(cdBuf);
		free(commentBuf);
		fclose(fp);
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}
	arc->entries = (struct mod_archive_entry *)calloc(validEntries, sizeof(*arc->entries));
	arc->names_pool = (char *)malloc(namesBytes);
	if (!arc->entries || !arc->names_pool) {
		free(arc->entries);
		free(arc->names_pool);
		free(arc);
		free(cdBuf);
		free(commentBuf);
		fclose(fp);
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}
	strncpy(arc->path, path, FS_MAXPATH);
	arc->path[FS_MAXPATH] = '\0';
	arc->fp = fp;
	arc->file_size = fileSize;
	arc->comment = commentBuf;

	/* Second pass: populate entries. Skip records with bad names / encryption /
	 * unsupported compression rather than failing the whole archive. */
	walked = 0;
	s32 keep = 0;
	for (s32 i = 0; i < validEntries; i++) {
		u8 *p = &cdBuf[walked];
		u16 flags     = readU16LE(&p[8]);
		u16 method    = readU16LE(&p[10]);
		u32 crc       = readU32LE(&p[16]);
		u32 csize     = readU32LE(&p[20]);
		u32 usize     = readU32LE(&p[24]);
		u16 nameLen   = readU16LE(&p[28]);
		u16 extraLen  = readU16LE(&p[30]);
		u16 cmtLen    = readU16LE(&p[32]);
		u32 lfhOffset = readU32LE(&p[42]);
		u32 recordLen = (u32)46 + nameLen + extraLen + cmtLen;

		if ((flags & ZIP_FLAG_ENCRYPTED) ||
		    (csize == 0xFFFFFFFFu) || (usize == 0xFFFFFFFFu) ||
		    (lfhOffset == 0xFFFFFFFFu)) {
			walked += recordLen;
			continue;
		}
		if (method != COMPRESSION_STORED && method != COMPRESSION_DEFLATED) {
			walked += recordLen;
			continue;
		}
		if (lfhOffset >= fileSize) {
			walked += recordLen;
			continue;
		}

		/* Copy + sanitise the name. Names pool grows linearly. */
		if (arc->names_pool_used + nameLen + 1 > namesBytes) {
			walked += recordLen;
			continue;
		}
		char *namePtr = &arc->names_pool[arc->names_pool_used];
		memcpy(namePtr, &p[46], nameLen);
		namePtr[nameLen] = '\0';
		if (!sanitiseEntryName(namePtr, nameLen)) {
			/* Drop the name; do not advance the pool cursor so the slot is
			 * reused by the next entry. */
			walked += recordLen;
			continue;
		}
		arc->names_pool_used += (u32)strlen(namePtr) + 1;

		struct mod_archive_entry *e = &arc->entries[keep++];
		e->name = namePtr;
		e->method = method;
		e->crc32 = crc;
		e->size_compressed = csize;
		e->size_uncompressed = usize;
		e->lfh_offset = lfhOffset;

		walked += recordLen;
	}
	arc->entry_count = keep;

	free(cdBuf);

	if (keep == 0) {
		modArchiveClose(arc);
		setError(MODARCHIVE_ERR_FORMAT);
		return NULL;
	}

	setError(MODARCHIVE_OK);
	return arc;
}

void modArchiveClose(mod_archive_t *arc)
{
	if (!arc) return;
	if (arc->fp) fclose(arc->fp);
	free(arc->entries);
	free(arc->names_pool);
	free(arc->comment);
	free(arc);
}

/* ---------------------------------------------------------- Reader: lookups */

s32 modArchiveGetEntryCount(const mod_archive_t *arc)
{
	return arc ? arc->entry_count : 0;
}

const char *modArchiveGetEntryName(const mod_archive_t *arc, s32 idx)
{
	if (!arc || idx < 0 || idx >= arc->entry_count) return NULL;
	return arc->entries[idx].name;
}

u32 modArchiveGetEntrySize(const mod_archive_t *arc, s32 idx)
{
	if (!arc || idx < 0 || idx >= arc->entry_count) return 0;
	return arc->entries[idx].size_uncompressed;
}

u32 modArchiveGetEntryCrc32(const mod_archive_t *arc, s32 idx)
{
	if (!arc || idx < 0 || idx >= arc->entry_count) return 0;
	return arc->entries[idx].crc32;
}

s32 modArchiveFindEntry(const mod_archive_t *arc, const char *name)
{
	if (!arc || !name) return -1;
	for (s32 i = 0; i < arc->entry_count; i++) {
		if (strcmp(arc->entries[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}

const char *modArchiveGetComment(const mod_archive_t *arc)
{
	return (arc && arc->comment) ? arc->comment : "";
}

/* -------------------------------------------------------- Reader: extract */

/* Read the LFH at offset and return the offset where the compressed data
 * begins (after the variable filename + extra fields). */
static s64 lfhDataOffset(FILE *fp, u32 lfhOffset)
{
	u8 lfh[30];
	if (fseek(fp, (long)lfhOffset, SEEK_SET) != 0) return -1;
	if (fread(lfh, 1, 30, fp) != 30) return -1;
	if (readU32LE(lfh) != LFH_SIG) return -1;
	u16 nameLen  = readU16LE(&lfh[26]);
	u16 extraLen = readU16LE(&lfh[28]);
	return (s64)lfhOffset + 30 + nameLen + extraLen;
}

void *modArchiveExtractAlloc(mod_archive_t *arc, s32 idx, u32 *outSize)
{
	if (!arc || idx < 0 || idx >= arc->entry_count) {
		setError(MODARCHIVE_ERR_NOTFOUND);
		return NULL;
	}
	struct mod_archive_entry *e = &arc->entries[idx];

	/* Empty entry: hand back a 1-byte allocation so callers don't need a
	 * NULL-vs-empty distinction. */
	if (e->size_uncompressed == 0) {
		void *empty = malloc(1);
		if (!empty) {
			setError(MODARCHIVE_ERR_MEM);
			return NULL;
		}
		((u8 *)empty)[0] = 0;
		if (outSize) *outSize = 0;
		setError(MODARCHIVE_OK);
		return empty;
	}

	s64 dataOff = lfhDataOffset(arc->fp, e->lfh_offset);
	if (dataOff < 0 || (u32)dataOff + e->size_compressed > arc->file_size) {
		setError(MODARCHIVE_ERR_FORMAT);
		return NULL;
	}

	u8 *out = (u8 *)malloc(e->size_uncompressed);
	if (!out) {
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}

	if (fseek(arc->fp, (long)dataOff, SEEK_SET) != 0) {
		free(out);
		setError(MODARCHIVE_ERR_IO);
		return NULL;
	}

	if (e->method == COMPRESSION_STORED) {
		if (e->size_compressed != e->size_uncompressed) {
			free(out);
			setError(MODARCHIVE_ERR_FORMAT);
			return NULL;
		}
		if (fread(out, 1, e->size_uncompressed, arc->fp) != e->size_uncompressed) {
			free(out);
			setError(MODARCHIVE_ERR_IO);
			return NULL;
		}
	} else {
		/* Method 8: raw deflate stream (no zlib wrapper). Use inflateInit2 with
		 * negative window bits so zlib does not expect the 2-byte zlib header. */
		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		if (inflateInit2(&zs, -15) != Z_OK) {
			free(out);
			setError(MODARCHIVE_ERR_DECOMP);
			return NULL;
		}

		u8 inBuf[INFLATE_CHUNK];
		u32 remaining = e->size_compressed;
		zs.next_out = out;
		zs.avail_out = e->size_uncompressed;

		while (remaining > 0 && zs.avail_out > 0) {
			u32 chunk = (remaining < INFLATE_CHUNK) ? remaining : INFLATE_CHUNK;
			size_t got = fread(inBuf, 1, chunk, arc->fp);
			if (got == 0) {
				inflateEnd(&zs);
				free(out);
				setError(MODARCHIVE_ERR_IO);
				return NULL;
			}
			zs.next_in = inBuf;
			zs.avail_in = (uInt)got;
			while (zs.avail_in > 0 && zs.avail_out > 0) {
				int r = inflate(&zs, Z_NO_FLUSH);
				if (r == Z_STREAM_END) {
					goto done_inflate;
				}
				if (r != Z_OK && r != Z_BUF_ERROR) {
					inflateEnd(&zs);
					free(out);
					setError(MODARCHIVE_ERR_DECOMP);
					return NULL;
				}
			}
			remaining -= (u32)got;
		}
done_inflate:
		inflateEnd(&zs);
		if (zs.total_out != e->size_uncompressed) {
			free(out);
			setError(MODARCHIVE_ERR_DECOMP);
			return NULL;
		}
	}

	/* CRC verification: cheap insurance against silent corruption. */
	uLong crc = crc32(0, out, e->size_uncompressed);
	if ((u32)crc != e->crc32) {
		free(out);
		setError(MODARCHIVE_ERR_DECOMP);
		return NULL;
	}

	if (outSize) *outSize = e->size_uncompressed;
	setError(MODARCHIVE_OK);
	return out;
}

char *modArchiveReadManifest(mod_archive_t *arc, u32 *outSize)
{
	if (!arc) {
		setError(MODARCHIVE_ERR_NOTFOUND);
		return NULL;
	}
	s32 idx = modArchiveFindEntry(arc, "mod.json");
	if (idx < 0) {
		setError(MODARCHIVE_ERR_NOTFOUND);
		return NULL;
	}
	u32 sz = 0;
	void *raw = modArchiveExtractAlloc(arc, idx, &sz);
	if (!raw) return NULL;
	/* Append a NUL so the JSON parser can treat the buffer as a C string. */
	char *out = (char *)realloc(raw, sz + 1);
	if (!out) {
		free(raw);
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}
	out[sz] = '\0';
	if (outSize) *outSize = sz;
	return out;
}

/* ---------------------------------------------------- File-level sha256 */

s32 modArchiveSha256(const char *path, u8 digest[SHA256_DIGEST_SIZE])
{
	if (!path || !digest) return -1;
	return sha256HashFile(path, digest);
}

/* ============================================================== Writer */

/* Per-entry record kept in memory until Finish() emits the central dir. */
typedef struct mawritten_entry {
	char *name;
	u32   lfh_offset;     /* offset of the LFH we just wrote */
	u32   size_compressed;
	u32   size_uncompressed;
	u32   crc32;
	u16   method;
	u16   nameLen;
} mawritten_entry_t;

struct mod_archive_writer {
	char  finalPath[FS_MAXPATH + 1];
	char  tempPath[FS_MAXPATH + 1];
	FILE *fp;
	u32   bytesWritten;
	mawritten_entry_t *entries;
	s32   entry_count;
	s32   entry_cap;
	char *comment;       /* malloc'd, NUL-terminated, or NULL */
	u32   commentLen;    /* clamped to 65535 */
	int   aborted;
};

static int writerGrowEntries(mod_archive_writer_t *w)
{
	s32 newCap = w->entry_cap ? w->entry_cap * 2 : 16;
	mawritten_entry_t *next = (mawritten_entry_t *)realloc(w->entries, (size_t)newCap * sizeof(*next));
	if (!next) return 0;
	w->entries = next;
	w->entry_cap = newCap;
	return 1;
}

mod_archive_writer_t *modArchiveBegin(const char *path)
{
	if (!path || !path[0]) {
		setError(MODARCHIVE_ERR_OPEN);
		return NULL;
	}
	mod_archive_writer_t *w = (mod_archive_writer_t *)calloc(1, sizeof(*w));
	if (!w) {
		setError(MODARCHIVE_ERR_MEM);
		return NULL;
	}
	strncpy(w->finalPath, path, FS_MAXPATH);
	w->finalPath[FS_MAXPATH] = '\0';
	snprintf(w->tempPath, sizeof(w->tempPath), "%s.tmp", path);

	w->fp = fopen(w->tempPath, "wb");
	if (!w->fp) {
		free(w);
		setError(MODARCHIVE_ERR_OPEN);
		return NULL;
	}
	setError(MODARCHIVE_OK);
	return w;
}

/* Validate a candidate entry name for the writer. Same rules as the reader's
 * sanitiser, applied to a const string. Returns 1 if safe to use as-is. */
static int writerNameSafe(const char *name)
{
	if (!name || !name[0]) return 0;
	if (name[0] == '/' || name[0] == '\\') return 0;
	if (isalpha((unsigned char)name[0]) && name[1] == ':') return 0;
	const char *p = name;
	const char *segStart = name;
	while (1) {
		char c = *p;
		if (c == '\\') return 0;  /* writer requires forward slashes */
		if (c == '/' || c == '\0') {
			size_t segLen = (size_t)(p - segStart);
			if (segLen == 0 && p != name && c == '/') return 0;
			if (segLen == 2 && segStart[0] == '.' && segStart[1] == '.') return 0;
			if (c == '\0') break;
			segStart = p + 1;
		}
		p++;
	}
	return 1;
}

/* Write `len` bytes to the writer, tracking position. Returns 0 on short
 * write (and flags the writer as unrecoverable). */
static int writerWrite(mod_archive_writer_t *w, const void *data, size_t len)
{
	if (w->aborted) return 0;
	if (fwrite(data, 1, len, w->fp) != len) {
		w->aborted = 1;
		return 0;
	}
	w->bytesWritten += (u32)len;
	return 1;
}

s32 modArchiveAddFileMem(mod_archive_writer_t *w, const char *name,
                         const void *data, u32 len)
{
	if (!w || w->aborted) return MODARCHIVE_ERR_IO;
	if (!writerNameSafe(name)) return MODARCHIVE_ERR_TRUST;
	if (w->entry_count >= 0xFFFF) return MODARCHIVE_ERR_LIMIT;
	if (w->entry_count == w->entry_cap && !writerGrowEntries(w)) return MODARCHIVE_ERR_MEM;

	u16 nameLen = (u16)strlen(name);
	u32 lfhOffset = w->bytesWritten;

	/* Compute deflate output up front so we can fill in the LFH sizes. */
	uLongf csizeBound = compressBound(len);
	u8 *cbuf = NULL;
	u32 csize = 0;
	u16 method = COMPRESSION_DEFLATED;
	u32 crc = (u32)crc32(0, (const Bytef *)(data ? data : (const void *)""), len);

	if (len > 0) {
		cbuf = (u8 *)malloc(csizeBound);
		if (!cbuf) return MODARCHIVE_ERR_MEM;

		z_stream zs;
		memset(&zs, 0, sizeof(zs));
		if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
		                 -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
			free(cbuf);
			return MODARCHIVE_ERR_DECOMP;
		}
		zs.next_in   = (Bytef *)(uintptr_t)data;
		zs.avail_in  = len;
		zs.next_out  = cbuf;
		zs.avail_out = csizeBound;
		int r = deflate(&zs, Z_FINISH);
		if (r != Z_STREAM_END) {
			deflateEnd(&zs);
			free(cbuf);
			return MODARCHIVE_ERR_DECOMP;
		}
		csize = (u32)zs.total_out;
		deflateEnd(&zs);

		/* If deflate didn't help (already-compressed data), store raw. */
		if (csize >= len) {
			free(cbuf);
			cbuf = NULL;
			method = COMPRESSION_STORED;
			csize = len;
		}
	} else {
		method = COMPRESSION_STORED;
		csize = 0;
	}

	/* Write the LFH. */
	u8 lfh[30];
	memset(lfh, 0, sizeof(lfh));
	writeU32LE(&lfh[0],  LFH_SIG);
	writeU16LE(&lfh[4],  20);          /* version needed */
	writeU16LE(&lfh[6],  0);           /* flags */
	writeU16LE(&lfh[8],  method);
	writeU16LE(&lfh[10], 0);           /* mod time */
	writeU16LE(&lfh[12], 0);           /* mod date */
	writeU32LE(&lfh[14], crc);
	writeU32LE(&lfh[18], csize);
	writeU32LE(&lfh[22], len);
	writeU16LE(&lfh[26], nameLen);
	writeU16LE(&lfh[28], 0);           /* extra len */

	if (!writerWrite(w, lfh, sizeof(lfh))) {
		free(cbuf);
		return MODARCHIVE_ERR_IO;
	}
	if (!writerWrite(w, name, nameLen)) {
		free(cbuf);
		return MODARCHIVE_ERR_IO;
	}
	if (csize > 0) {
		const void *payload = (method == COMPRESSION_STORED) ? data : cbuf;
		if (!writerWrite(w, payload, csize)) {
			free(cbuf);
			return MODARCHIVE_ERR_IO;
		}
	}
	free(cbuf);

	/* Stash entry record for the central directory. */
	mawritten_entry_t *e = &w->entries[w->entry_count++];
	e->name = (char *)malloc(nameLen + 1);
	if (!e->name) {
		w->entry_count--;
		w->aborted = 1;
		return MODARCHIVE_ERR_MEM;
	}
	memcpy(e->name, name, nameLen);
	e->name[nameLen] = '\0';
	e->lfh_offset = lfhOffset;
	e->size_compressed = csize;
	e->size_uncompressed = len;
	e->crc32 = crc;
	e->method = method;
	e->nameLen = nameLen;

	return MODARCHIVE_OK;
}

s32 modArchiveAddFileDisk(mod_archive_writer_t *w, const char *name,
                          const char *srcpath)
{
	if (!w || !name || !srcpath) return MODARCHIVE_ERR_IO;
	FILE *src = fopen(srcpath, "rb");
	if (!src) return MODARCHIVE_ERR_OPEN;

	struct stat st;
	if (stat(srcpath, &st) != 0 || st.st_size < 0) {
		fclose(src);
		return MODARCHIVE_ERR_OPEN;
	}
	if ((u64)st.st_size > 0xFFFFFFFFull) {
		fclose(src);
		return MODARCHIVE_ERR_LIMIT;
	}
	u32 size = (u32)st.st_size;

	void *buf = NULL;
	if (size > 0) {
		buf = malloc(size);
		if (!buf) {
			fclose(src);
			return MODARCHIVE_ERR_MEM;
		}
		if (fread(buf, 1, size, src) != size) {
			free(buf);
			fclose(src);
			return MODARCHIVE_ERR_IO;
		}
	}
	fclose(src);

	s32 r = modArchiveAddFileMem(w, name, buf, size);
	free(buf);
	return r;
}

void modArchiveSetComment(mod_archive_writer_t *w, const char *comment)
{
	if (!w) return;
	free(w->comment);
	w->comment = NULL;
	w->commentLen = 0;
	if (!comment || !comment[0]) return;
	size_t clen = strlen(comment);
	if (clen > MAX_ZIP_COMMENT) clen = MAX_ZIP_COMMENT;
	w->comment = (char *)malloc(clen + 1);
	if (!w->comment) return;
	memcpy(w->comment, comment, clen);
	w->comment[clen] = '\0';
	w->commentLen = (u32)clen;
}

s32 modArchiveFinish(mod_archive_writer_t *w)
{
	if (!w) return MODARCHIVE_ERR_IO;
	if (w->aborted) {
		modArchiveAbort(w);
		return MODARCHIVE_ERR_IO;
	}

	u32 cdOffset = w->bytesWritten;

	/* Emit central-directory headers. */
	for (s32 i = 0; i < w->entry_count; i++) {
		mawritten_entry_t *e = &w->entries[i];
		u8 cdh[46];
		memset(cdh, 0, sizeof(cdh));
		writeU32LE(&cdh[0],  CDH_SIG);
		writeU16LE(&cdh[4],  20);             /* version made by */
		writeU16LE(&cdh[6],  20);             /* version needed */
		writeU16LE(&cdh[8],  0);              /* flags */
		writeU16LE(&cdh[10], e->method);
		writeU16LE(&cdh[12], 0);              /* mod time */
		writeU16LE(&cdh[14], 0);              /* mod date */
		writeU32LE(&cdh[16], e->crc32);
		writeU32LE(&cdh[20], e->size_compressed);
		writeU32LE(&cdh[24], e->size_uncompressed);
		writeU16LE(&cdh[28], e->nameLen);
		writeU16LE(&cdh[30], 0);              /* extra len */
		writeU16LE(&cdh[32], 0);              /* comment len */
		writeU16LE(&cdh[34], 0);              /* disk no */
		writeU16LE(&cdh[36], 0);              /* internal attrs */
		writeU32LE(&cdh[38], 0);              /* external attrs */
		writeU32LE(&cdh[42], e->lfh_offset);
		if (!writerWrite(w, cdh, sizeof(cdh))) goto fail;
		if (!writerWrite(w, e->name, e->nameLen)) goto fail;
	}

	u32 cdSize = w->bytesWritten - cdOffset;

	/* EOCD. */
	u8 eocd[EOCD_MIN_SIZE];
	memset(eocd, 0, sizeof(eocd));
	writeU32LE(&eocd[0],  EOCD_SIG);
	writeU16LE(&eocd[4],  0);                  /* disk no */
	writeU16LE(&eocd[6],  0);                  /* disk with cd */
	writeU16LE(&eocd[8],  (u16)w->entry_count);
	writeU16LE(&eocd[10], (u16)w->entry_count);
	writeU32LE(&eocd[12], cdSize);
	writeU32LE(&eocd[16], cdOffset);
	writeU16LE(&eocd[20], (u16)w->commentLen);
	if (!writerWrite(w, eocd, sizeof(eocd))) goto fail;
	if (w->commentLen > 0 && !writerWrite(w, w->comment, w->commentLen)) goto fail;

	if (fclose(w->fp) != 0) goto fail_after_close;
	w->fp = NULL;

	/* Atomic rename: remove any existing target so rename succeeds on Win. */
	remove(w->finalPath);
	if (rename(w->tempPath, w->finalPath) != 0) {
		remove(w->tempPath);
		for (s32 i = 0; i < w->entry_count; i++) free(w->entries[i].name);
		free(w->entries);
		free(w->comment);
		free(w);
		setError(MODARCHIVE_ERR_IO);
		return MODARCHIVE_ERR_IO;
	}

	for (s32 i = 0; i < w->entry_count; i++) free(w->entries[i].name);
	free(w->entries);
	free(w->comment);
	free(w);
	setError(MODARCHIVE_OK);
	return MODARCHIVE_OK;

fail:
	if (w->fp) fclose(w->fp);
fail_after_close:
	w->fp = NULL;
	remove(w->tempPath);
	for (s32 i = 0; i < w->entry_count; i++) free(w->entries[i].name);
	free(w->entries);
	free(w->comment);
	free(w);
	setError(MODARCHIVE_ERR_IO);
	return MODARCHIVE_ERR_IO;
}

void modArchiveAbort(mod_archive_writer_t *w)
{
	if (!w) return;
	if (w->fp) {
		fclose(w->fp);
		w->fp = NULL;
	}
	remove(w->tempPath);
	for (s32 i = 0; i < w->entry_count; i++) free(w->entries[i].name);
	free(w->entries);
	free(w->comment);
	free(w);
}
