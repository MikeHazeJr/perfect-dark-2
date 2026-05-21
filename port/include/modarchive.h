/**
 * modarchive.h -- Priority M / B-238: read + write `.pdmod` archives.
 *
 * `.pdmod` is a deflate-compressed zip archive with `mod.json` at its root.
 * Plain `.zip` is accepted as a fallback when the manifest is present.
 *
 * This module is the canonical zip reader/writer for the mod loader. It
 * routes decompression through the already-linked system zlib, avoids any
 * on-disk extraction during normal load, and keeps one FILE* open per mounted
 * archive so the engine can lazily fetch entries on demand.
 *
 * Public API:
 *   - modArchiveOpen / Close / SuspendIO / ResumeIO (lifecycle + file handle)
 *   - modArchiveFindEntry / ExtractAlloc / ReadManifest (read path)
 *   - modArchiveBegin / AddFileMem / AddFileDisk / SetComment / Finish (write path)
 *   - modArchiveSha256 (file-level sha256 helper for transfer integrity)
 *
 * Trust:
 *   - All entry names are sanitised inside the reader. Names containing `..`
 *     segments, absolute paths, or backslashes are rejected. Callers can rely
 *     on every returned name being a relative, forward-slash-separated path.
 *
 * Limits:
 *   - ZIP64 archives are not supported (cap = 4 GiB / 65535 entries).
 *   - Encrypted / unsupported compression methods are rejected per-entry.
 */
#ifndef _IN_MODARCHIVE_H
#define _IN_MODARCHIVE_H

#include <PR/ultratypes.h>
#include <stddef.h>
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque archive handle. */
typedef struct mod_archive mod_archive_t;

/* Opaque writer handle. */
typedef struct mod_archive_writer mod_archive_writer_t;

/** Callback used when walking entries in an in-memory zip archive. */
typedef s32 (*mod_archive_mem_entry_cb)(const char *entryName,
                                        u32 uncompressedSize,
                                        void *user);

/* Result codes for write operations. */
#define MODARCHIVE_OK             0
#define MODARCHIVE_ERR_OPEN      -1   /* fopen / fseek failed */
#define MODARCHIVE_ERR_FORMAT    -2   /* not a zip / corrupt central directory */
#define MODARCHIVE_ERR_NOTFOUND  -3   /* entry not present */
#define MODARCHIVE_ERR_DECOMP    -4   /* zlib inflate failed / unknown method */
#define MODARCHIVE_ERR_MEM       -5   /* allocation failure */
#define MODARCHIVE_ERR_IO        -6   /* short read / write */
#define MODARCHIVE_ERR_TRUST     -7   /* path-traversal or encrypted entry */
#define MODARCHIVE_ERR_LIMIT     -8   /* archive exceeds supported size / count */

/* ------------------------------------------------------------------ Reader */

/**
 * Open a zip / .pdmod archive at `path`. Reads + parses the central
 * directory; the FILE* stays open until modArchiveClose so per-entry
 * extraction is cheap.
 *
 * Returns a handle on success, NULL on failure. On NULL the caller may
 * inspect modArchiveLastError() for the failure code.
 */
mod_archive_t *modArchiveOpen(const char *path);

/** Release the archive handle and close its file. Safe to pass NULL. */
void modArchiveClose(mod_archive_t *arc);

/** Last-error code from the most recent open / read call (per thread). */
s32 modArchiveLastError(void);

/** On-disk path the archive was opened from. Returns "" if NULL. */
const char *modArchiveGetPath(const mod_archive_t *arc);

/** Number of entries indexed in the central directory. */
s32 modArchiveGetEntryCount(const mod_archive_t *arc);

/** Look up an entry index by sanitised name (forward slashes). Returns -1. */
s32 modArchiveFindEntry(const mod_archive_t *arc, const char *name);

/** Sanitised entry name at index. Returns NULL on out-of-range. */
const char *modArchiveGetEntryName(const mod_archive_t *arc, s32 idx);

/** Uncompressed size of the entry at index. */
u32 modArchiveGetEntrySize(const mod_archive_t *arc, s32 idx);

/** CRC32 of the entry's uncompressed bytes (as stored in the central dir). */
u32 modArchiveGetEntryCrc32(const mod_archive_t *arc, s32 idx);

/**
 * Decompress the entry at `idx` into a freshly allocated buffer.
 * `*outSize` receives the uncompressed length on success. The buffer is
 * allocated with malloc and is the caller's responsibility to free().
 *
 * On entries with uncompressed size 0 the function still returns a valid
 * 1-byte heap allocation containing a NUL byte (so callers can pass it to
 * strlen-style code without a null-pointer check).
 *
 * Returns NULL on error (modArchiveLastError() carries the reason).
 */
void *modArchiveExtractAlloc(mod_archive_t *arc, s32 idx, u32 *outSize);

/**
 * Convenience: locate `mod.json` at the archive root and return its bytes.
 * Allocated buffer is null-terminated for safe use with the JSON parser.
 * `*outSize` is the JSON byte count (excluding the appended NUL).
 *
 * Returns NULL if the archive lacks a root mod.json or extraction fails.
 */
char *modArchiveReadManifest(mod_archive_t *arc, u32 *outSize);

/**
 * Extract one entry from a ZIP archive already loaded in memory. This is used
 * for typed asset archives embedded inside a `.pdmod` transport archive, so
 * callers can read `asset.pdhead::model.gltf` without extracting either
 * archive to the mods folder.
 *
 * The returned buffer is allocated with malloc and must be freed by caller.
 */
void *modArchiveExtractMemAlloc(const void *archiveBytes, u32 archiveSize,
                                const char *entryName, u32 *outSize);

/**
 * Return 1 if an in-memory ZIP archive contains any entry whose name includes
 * a forbidden authored `.bin` payload segment. `outName` receives the first
 * matching sanitized entry name when provided.
 */
s32 modArchiveMemFindForbiddenBinPayload(const void *archiveBytes, u32 archiveSize,
                                         char *outName, u32 outNameCap);

/**
 * Iterate sanitized entries inside an in-memory ZIP archive.
 *
 * This mirrors the trust rules used by modArchiveOpen(): unsafe names,
 * encrypted entries, unsupported compression methods, and ZIP64 sentinel
 * entries are skipped. The callback receives sanitized forward-slash entry
 * names and the uncompressed byte size. Return non-zero from the callback to
 * stop early; otherwise this returns MODARCHIVE_OK on success.
 */
s32 modArchiveMemForEachEntry(const void *archiveBytes, u32 archiveSize,
                              mod_archive_mem_entry_cb cb, void *user);

/**
 * Zip-level "comment" field from the EOCD record. Returns the static
 * archive-owned buffer (NUL-terminated). The defensive mirror per design
 * Section 4.5.5 lives here as a small JSON blob with name/creator/version.
 *
 * Returns "" when the archive has no comment.
 */
const char *modArchiveGetComment(const mod_archive_t *arc);

/**
 * Compute the sha256 digest of the archive file on disk (transfer-integrity
 * hash, NOT a content hash). Returns 0 on success, non-zero on file error.
 * Caller provides a 32-byte digest buffer.
 */
s32 modArchiveSha256(const char *path, u8 digest[SHA256_DIGEST_SIZE]);

/* ------------------------------------------------------------------ Writer */

/**
 * Begin writing a new archive at `path`. The writer streams entries to a
 * `<path>.tmp` sibling and renames atomically on Finish, so a crash mid-save
 * cannot corrupt an existing `.pdmod`.
 *
 * Returns a writer handle on success, NULL on error.
 */
mod_archive_writer_t *modArchiveBegin(const char *path);

/**
 * Add an entry from an in-memory buffer. `name` must be a forward-slash
 * relative path; the writer rejects anything with `..` segments or a
 * leading slash. `data` may be NULL when `len == 0` (empty entry).
 *
 * Returns MODARCHIVE_OK on success.
 */
s32 modArchiveAddFileMem(mod_archive_writer_t *w, const char *name,
                         const void *data, u32 len);

/** Same as AddFileMem but reads the contents from an on-disk file. */
s32 modArchiveAddFileDisk(mod_archive_writer_t *w, const char *name,
                          const char *srcpath);

/**
 * Set the zip-level comment string (truncated to 65535 bytes if longer).
 * Used for the defensive metadata mirror per design 4.5.5.
 */
void modArchiveSetComment(mod_archive_writer_t *w, const char *comment);

/**
 * Finalise the archive: write the central directory + EOCD, close the temp
 * file, and atomically rename it to the target path. On error the temp file
 * is removed and no rename occurs (the existing `<path>` is left intact).
 *
 * Returns MODARCHIVE_OK on success. The writer is freed regardless.
 */
s32 modArchiveFinish(mod_archive_writer_t *w);

/** Cancel a write, deleting the temp file. Frees the writer. Safe on NULL. */
void modArchiveAbort(mod_archive_writer_t *w);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODARCHIVE_H */
