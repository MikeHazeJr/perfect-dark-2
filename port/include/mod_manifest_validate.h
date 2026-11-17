/**
 * mod_manifest_validate.h -- strict root mod.json admission for packaging.
 *
 * In-game counterpart of validate_manifest() in
 * tools/verify_pdxxx_modder_workflow.py (T-MODDING-012). A `.pdmod` may only
 * be produced from a root mod.json that:
 *
 *   - is one complete UTF-8 JSON value with no byte order mark,
 *   - is a JSON object,
 *   - carries `id`, `name` and `version` as strings that still contain text
 *     after surrounding whitespace is removed.
 *
 * Values are checked, never coerced or rewritten: padded text is accepted
 * unchanged, and numbers, booleans, null, arrays and objects never satisfy a
 * required string field.
 *
 * The reader is the shared strict JSON reader (modAssetJsonReadValue), so the
 * game is deliberately stricter than Python's json.loads in these cases:
 * duplicate object keys, "\u0000" and lone-surrogate escapes, non-finite or
 * NaN/Infinity numbers, nesting deeper than 64 levels, and input larger than
 * MOD_MANIFEST_MAX_BYTES are all refused. Anything accepted here is accepted
 * by the offline validator. tests/fixtures/modmanifest/cases.json holds the
 * shared verdict table that both validators are tested against.
 *
 * The mod loader (modmgr.c) keeps its lenient compatibility defaults for
 * mods that already exist on disk. This gate is for producing packages.
 */
#ifndef _IN_MOD_MANIFEST_VALIDATE_H
#define _IN_MOD_MANIFEST_VALIDATE_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_MANIFEST_OK              0
#define MOD_MANIFEST_ERR_UNREADABLE  1   /* not one complete strict UTF-8 JSON value */
#define MOD_MANIFEST_ERR_NOT_OBJECT  2   /* valid JSON, but not an object */
#define MOD_MANIFEST_ERR_FIELD       3   /* id, name or version is not a non-empty string */

/* Largest mod.json the packers read. */
#define MOD_MANIFEST_MAX_BYTES (8u * 1024u * 1024u)

/**
 * Validate the bytes of a root mod.json.
 *
 * Returns MOD_MANIFEST_OK or one MOD_MANIFEST_ERR_* code. `error` (optional)
 * receives a short description on failure and is emptied on success. The
 * text does not name the file; callers add their own path or context. A
 * field failure lists every failing field, in id, name, version order.
 *
 * Pure: no globals, no I/O, no logging. The input is not modified.
 */
s32 modManifestValidateBytes(const void *json, size_t size,
		char *error, size_t error_cap);

/**
 * Read one top-level string field of a root mod.json, for example `id`.
 *
 * Returns 1 and copies the value when the document passes the same strict
 * reader, is an object, and `key` holds a string that fits in `out` with its
 * terminator. Returns 0 and empties `out` in every other case. The value is
 * copied as written: it is not trimmed.
 *
 * Pure: no globals, no I/O, no logging.
 */
s32 modManifestReadString(const void *json, size_t size, const char *key,
		char *out, size_t out_cap);

/**
 * The same for a number: returns 1 and the value when `key` holds a finite
 * JSON number, 0 in every other case.
 */
s32 modManifestReadNumber(const void *json, size_t size, const char *key, double *out);

#ifdef __cplusplus
}
#endif

#endif
