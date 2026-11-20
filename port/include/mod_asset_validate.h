/**
 * mod_asset_validate.h -- one in-game Validate for typed asset archives.
 *
 * Release validation (asset_archive_policy.h) checks an archive's layout,
 * forbidden members and references. It does not read what the members say,
 * so an archive whose settings file is not JSON passes it and is then refused
 * by its loader. This is the check the in-game tools run before they save,
 * pack or enable: release validation first, then the content.
 *
 *   1. Release validation of the archive, as the catalog scanner runs it.
 *   2. Every public text member, in the archive and in every typed archive
 *      embedded in it: `.json` and `.gltf` must be one strict JSON value
 *      (the shared reader: UTF-8, no byte order mark, no duplicate keys, no
 *      trailing text); `.ini` must be UTF-8 with no NUL byte, and every line
 *      inside a section must be a comment, a section header or `key = value`.
 *   3. The family's own reader where one can run without game state: body,
 *      head and character source readers, and weapon, projectile and entity
 *      graph compilers. Split v1 weapon graphs use the descriptor-selected
 *      variables from their archive, including when embedded.
 *
 * Validation does not register assets or write files.
 */
#ifndef _IN_MOD_ASSET_VALIDATE_H
#define _IN_MOD_ASSET_VALIDATE_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOD_ASSET_VALIDATE_OK           0
#define MOD_ASSET_VALIDATE_ERR_ARGS     1   /* nothing to validate, or not a typed archive name */
#define MOD_ASSET_VALIDATE_ERR_POLICY   2   /* release validation refused the archive */
#define MOD_ASSET_VALIDATE_ERR_TEXT     3   /* a public text member is malformed */
#define MOD_ASSET_VALIDATE_ERR_FAMILY   4   /* the family's own reader refused it */

/* Typed archives may nest this deep, the root being depth 0. */
#define MOD_ASSET_VALIDATE_MAX_DEPTH    6

/**
 * Validate typed archive bytes. `archive_name` is the name the archive
 * carries or will carry; its extension selects the family.
 *
 * Returns MOD_ASSET_VALIDATE_OK or one MOD_ASSET_VALIDATE_ERR_* code. `err`
 * (optional) receives a short reason that names the member at fault, with
 * embedded archives written as "outer.pdxxx::member".
 */
s32 modAssetValidateArchiveBytes(const void *bytes, u32 size, const char *archive_name,
		char *err, size_t err_cap);

/** Read `path` and validate it under its own name. */
s32 modAssetValidateArchiveFile(const char *path, char *err, size_t err_cap);

/**
 * The same for an archive that is on disk under one name and will be used
 * under another, the way a save stages `<name>.candidate` before it swaps it
 * in: read from `disk_path`, judged as `archive_name`.
 *
 * The file variants also retain the file compiler checks for legacy v1
 * weapons with a single combined behavior graph.
 */
s32 modAssetValidateArchiveStaged(const char *disk_path, const char *archive_name,
		char *err, size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MOD_ASSET_VALIDATE_H */
