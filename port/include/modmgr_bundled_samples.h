/**
 * modmgr_bundled_samples.h -- the first-party sample mods an install carries
 * in mods/bundled/, and which of them a fresh profile starts with enabled
 * (T-RELEASE-007).
 *
 * The staging step (tools/stage_bundled_samples.py, used by the release
 * script) packs every dev-mods.json entry with "release": true into
 * <install>/mods/bundled/<id>.pdmod and then writes the inventory beside the
 * client, <install>/bundled-mods.sha256, one line per package in dev-mods.json
 * order, in the sha256sum binary format:
 *
 *   <64 lowercase hex digits> *<id>.pdmod\n
 *
 * With no saved selection (no mods-enabled.json and an empty pd.ini list) and
 * no --moddir, the mod manager enables each package the inventory names when
 * it is the only mod with that id, a valid permanent archive at exactly
 * <install>/mods/bundled/<id>.pdmod, and its bytes still match the digest. A
 * user's own copy with the same id therefore wins and is never auto-enabled,
 * and a changed package is not enabled. Saved choices always take over.
 *
 * This module is pure: parsing and the choice of row. Reading files, hashing
 * and the registry belong to modmgr.c.
 */
#ifndef _IN_MODMGR_BUNDLED_SAMPLES_H
#define _IN_MODMGR_BUNDLED_SAMPLES_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "sha256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install-relative names the staging step writes. */
#define MODMGR_BUNDLED_INVENTORY_NAME "bundled-mods.sha256"
#define MODMGR_BUNDLED_DIR_NAME       "mods/bundled"
#define MODMGR_BUNDLED_EXT            ".pdmod"

#define MODMGR_BUNDLED_MAX            16
/* A mod id, as modinfo_t holds it (MODMGR_ID_LEN; modmgr.c checks they agree). */
#define MODMGR_BUNDLED_ID_LEN         64
/* An inventory larger than this is refused unread. */
#define MODMGR_BUNDLED_MAX_BYTES      4096

typedef struct modmgr_bundled_entry {
	char id[MODMGR_BUNDLED_ID_LEN];
	char digest[SHA256_HEX_SIZE];
} modmgr_bundled_entry_t;

typedef struct modmgr_bundled_inventory {
	modmgr_bundled_entry_t entries[MODMGR_BUNDLED_MAX];
	s32 count;
} modmgr_bundled_inventory_t;

/**
 * Parse inventory text. Strict, all or nothing: every line is
 * "<64 lowercase hex> *<id>.pdmod" ending in '\n' (a '\r' before it is
 * refused), the id is one safe folder name (modpackArchiveNameIsSafe) that
 * fits MODMGR_BUNDLED_ID_LEN, no id appears twice (ignoring case), at most
 * MODMGR_BUNDLED_MAX lines. Empty text is a valid inventory of nothing.
 *
 * Returns 1 and fills `out`, or 0 with `out` emptied and a reason in `err`.
 */
s32 modmgrBundledParse(const char *text, size_t len, modmgr_bundled_inventory_t *out,
	char *err, size_t err_cap);

/** Index of `id` in the inventory (exact, case-sensitive), or -1. */
s32 modmgrBundledIndexOf(const modmgr_bundled_inventory_t *inv, const char *id);

/** A registry row as the policy sees it. */
typedef struct modmgr_bundled_candidate {
	const char *id;
	const char *archive_path; /* empty for a loose folder */
	s32 valid;
	s32 is_archive;
	s32 session_only;
} modmgr_bundled_candidate_t;

/** 1 when the file at `path` has SHA-256 `digest` (lowercase hex). */
typedef s32 (*modmgr_bundled_verify_fn)(const char *path, const char *digest, void *user);

/**
 * The row that is inventory entry `index`'s package, or -1: exactly one row
 * carries the entry's id, and that row is valid, an archive, not session
 * only, at `expected_path` (compared ignoring ASCII case, '/' equal to '\\'),
 * and `verify` accepts its bytes.
 */
s32 modmgrBundledFindVerified(const modmgr_bundled_inventory_t *inv, s32 index,
	const modmgr_bundled_candidate_t *rows, s32 row_count, const char *expected_path,
	modmgr_bundled_verify_fn verify, void *user);

#ifdef __cplusplus
}
#endif

#endif
