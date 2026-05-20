/**
 * modpack_pdmod.h -- Priority M / B-238 shared `.pdmod` save helper.
 *
 * Two entry points used by every code path that authors a `.pdmod`:
 *
 *   modpackPdmodFromFolder(src_folder, out_path)
 *     - Validates the external authoring layout, generates missing commented
 *       INI templates for canonical asset folders, then recursively packs
 *       every accepted file under `src_folder` into the archive, preserving
 *       relative paths. mod.json must already exist at the folder root. Used
 *       by M-4 first-run auto-migration and Modding Hub folder export.
 *
 *   modpackPdmodWriteSingle(out_path, manifest_json, manifest_len,
 *                            entries[], entry_count)
 *     - Writes a single archive from an in-memory manifest plus an
 *       explicit entry list. Used by mod tools (M-3) that author a
 *       single-theme / single-skin / single-asset mod.
 *
 * Both helpers:
 *   - Atomic: streams to <out_path>.tmp via modarchive's writer, renames
 *     on success, removes the temp on any failure.
 *   - Compute the zip-level comment "defensive mirror" automatically from
 *     the manifest headline fields (name, creator/author, version, tags).
 *     See pdmod-unified-mod-format.md Section 4.5.5 for the rationale.
 *   - Return 0 on success, non-zero on failure (see error codes below).
 */
#ifndef _IN_MODPACK_PDMOD_H
#define _IN_MODPACK_PDMOD_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODPACK_PDMOD_OK              0
#define MODPACK_PDMOD_ERR_OPEN       -1   /* could not open destination */
#define MODPACK_PDMOD_ERR_NO_MFST    -2   /* mod.json not found in source */
#define MODPACK_PDMOD_ERR_BAD_MFST   -3   /* mod.json failed to parse */
#define MODPACK_PDMOD_ERR_IO         -4   /* read/write failure during pack */
#define MODPACK_PDMOD_ERR_TOO_BIG    -5   /* source exceeds 4 GiB total */
#define MODPACK_PDMOD_ERR_LAYOUT     -6   /* invalid external layout */
#define MODPACK_PDMOD_ERR_TEMPLATE   -7   /* failed to generate template */

/* In-memory entry for the bulk writer. */
typedef struct modpack_entry {
	const char *entry_name;     /* forward-slash path inside the archive */
	const void *data;           /* may be NULL when len==0 */
	u32         len;
} modpack_entry_t;

/**
 * Recursively pack a folder mod into a single .pdmod archive.
 *
 * Reads `<src_folder>/mod.json` to compute the comment mirror. Skips dot-
 * prefixed files (.modstate, .DS_Store, etc.) and recursive symlink loops.
 *
 * Returns MODPACK_PDMOD_OK on success.
 */
s32 modpackPdmodFromFolder(const char *src_folder, const char *out_path);

/**
 * Human-readable detail for the most recent failure from this module.
 *
 * Returns an empty string when the last operation did not publish detail.
 */
const char *modpackPdmodLastError(void);

/**
 * Write a single .pdmod archive from an in-memory manifest plus an
 * explicit list of entries.
 *
 * `manifest_json` MUST be the full mod.json payload to embed at the root
 * of the archive. The writer also derives the comment mirror from it.
 *
 * `entries` may be NULL when `entry_count == 0` (manifest-only mod).
 *
 * Returns MODPACK_PDMOD_OK on success.
 */
s32 modpackPdmodWriteSingle(const char *out_path,
                             const char *manifest_json, u32 manifest_len,
                             const modpack_entry_t *entries, s32 entry_count);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODPACK_PDMOD_H */
