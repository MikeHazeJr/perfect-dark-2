/**
 * modpack_archive.h -- the component archives inside a .pdpack (modpack.h):
 * building them so the same files always give the same bytes, and the names
 * an import may turn into folders under the mods folder.
 *
 * A component archive is a PDCA envelope (pdca_extract_transaction.h):
 *   u32 magic "PDCA", u16 file_count, then per file:
 *   u16 path_len (with the NUL), char path[], u32 size, u8 bytes[size]
 * all little-endian. Import extracts it with pdcaExtractArchiveBegin, which
 * refuses any member path that is absolute, climbs with "..", names a
 * Windows device or aliases another member.
 *
 * This module is pure: no catalog, no mod registry, no logging. Problems come
 * back as a short reason in `err`.
 */
#ifndef _IN_MODPACK_ARCHIVE_H
#define _IN_MODPACK_ARCHIVE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The envelope counts files in a u16. */
#define MODPACK_ARCHIVE_MAX_FILES 0xFFFFu

/**
 * Build the component archive of every regular file under `dirpath`,
 * skipping names that start with '.' (".", "..", hidden files).
 *
 * Members are written in strcmp order of their '/'-separated paths relative
 * to `dirpath`, never in the order the filesystem lists them, so two builds
 * of the same files are byte-identical (CAT-C02). Every member path must be
 * one the extractor accepts (pdcaNormalizeMemberPath), so a build never makes
 * an archive its own import would refuse. A file that cannot be read whole
 * fails the build: nothing is silently left out.
 *
 * Returns a malloc'd buffer the caller frees and sets *out_len, or returns
 * NULL with a reason in `err` (optional).
 */
u8 *modpackArchiveBuild(const char *dirpath, u32 *out_len, char *err, size_t err_cap);

/**
 * 1 when `name` may become one folder under the mods folder: a pack's
 * category or component id (CAT-C01). Exactly one path component the
 * extractor would accept, and not hidden: no '/' or '\\', no drive or ':',
 * not "." or "..", no leading '.', no Windows device name, no trailing dot or
 * space, no wildcard or control characters.
 */
s32 modpackArchiveNameIsSafe(const char *name);

#ifdef __cplusplus
}
#endif

#endif
