/**
 * modpack.h -- D3R-10: Mod pack export/import (.pdpack format)
 *
 * .pdpack = PDPK container (Perfect Dark Pack):
 *
 *   [4]  magic          0x4B504450 ("PDPK" LE)
 *   [4]  version        1
 *   [4]  component_count
 *   [4]  manifest_len
 *   [manifest_len] INI-style manifest text (not null-terminated)
 *   --- for each component ---
 *   [2]  id_len         (including null terminator)
 *   [id_len] id
 *   [2]  cat_len
 *   [cat_len] category
 *   [2]  ver_len
 *   [ver_len] version
 *   [4]  raw_size       (PDCA uncompressed bytes)
 *   [4]  stored_size    (bytes stored in file)
 *   [1]  compression    (0 = raw, 1 = zlib)
 *   [stored_size] PDCA data
 *
 * PDCA format is the same archive used by D3R-9 network distribution:
 *   u32 magic "PDCA", u16 file_count, then entries:
 *   u16 path_len + char path[] + u32 data_len + u8 data[]
 *
 * Export: iterate catalog entries by ID, build PDCAs, compress, write PDPK.
 * Import: read PDPK, decompress PDCAs, extract files, hot-register in catalog.
 *
 * Note: bool is s32 in game code — never include <stdbool.h> in C files.
 */

#ifndef _IN_MODPACK_H
#define _IN_MODPACK_H

#include <PR/ultratypes.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Format identifiers
 * ======================================================================== */

#define PDPK_MAGIC    0x4B504450u   /* "PDPK" little-endian */
#define PDPK_VERSION  1u

/* ========================================================================
 * Types
 * ======================================================================== */

#define MODPACK_MAX_COMPONENTS  64
#define MODPACK_NAME_LEN        128
#define MODPACK_AUTHOR_LEN      64
#define MODPACK_VERSION_LEN     32
#define MODPACK_ERROR_LEN       256

/* Metadata for one component as parsed from a pack's manifest/records */
typedef struct modpack_component_info {
    char id[CATALOG_ID_LEN];
    char category[CATALOG_CATEGORY_LEN];
    char version[MODPACK_VERSION_LEN];
} modpack_component_info_t;

/* Full pack manifest — populated by modpackReadManifest() */
typedef struct modpack_manifest {
    char name[MODPACK_NAME_LEN];
    char author[MODPACK_AUTHOR_LEN];
    char version[MODPACK_VERSION_LEN];
    s32  component_count;
    modpack_component_info_t components[MODPACK_MAX_COMPONENTS];
} modpack_manifest_t;

/* Result from modpackImport() */
typedef struct modpack_import_result {
    s32  imported_count;      /* components successfully extracted + registered */
    s32  skipped_count;       /* components skipped (empty dir, etc.) */
    s32  error_count;         /* components that failed to extract */
    char error_msg[MODPACK_ERROR_LEN]; /* first fatal error, or empty */
} modpack_import_result_t;

/* Result from modpackValidate() */
typedef struct modpack_validate_result {
    s32  valid;               /* 1 = ok to import (may still have warnings) */
    s32  conflict_count;      /* components already registered in catalog */
    char warnings[512];       /* human-readable summary (comma list of IDs) */
} modpack_validate_result_t;

/* ========================================================================
 * API
 * ======================================================================== */

/**
 * Export selected catalog components to a .pdpack file.
 *
 * component_ids: array of catalog ID strings (from assetCatalogIterateByType)
 * count:         number of IDs
 * pack_name / pack_author / pack_version: manifest metadata
 * output_path:   destination file path (absolute or cwd-relative)
 * out_error:     optional buffer for error description (may be NULL)
 * error_buf_len: size of out_error buffer
 *
 * Bundled (base-game) components are silently skipped.
 * Returns 0 on success, -1 on error.
 */
s32 modpackExport(const char * const *component_ids, s32 count,
                  const char *pack_name, const char *pack_author,
                  const char *pack_version,
                  const char *output_path,
                  char *out_error, s32 error_buf_len);

/**
 * Read only the manifest from a .pdpack file (no extraction).
 * Populates *out with pack name/author/version and component list.
 * Returns 1 on success, 0 on parse error / wrong magic.
 */
s32 modpackReadManifest(const char *pack_path, modpack_manifest_t *out);

/**
 * Check a .pdpack for conflicts against the current catalog.
 * Sets out->conflict_count and out->warnings with installed IDs.
 * Returns 1 if readable and structurally valid, 0 on hard parse error.
 * A non-zero conflict_count is a warning, not a hard block.
 */
s32 modpackValidate(const char *pack_path, modpack_validate_result_t *out);

/**
 * Import a .pdpack file.
 *
 * pack_path:    path to the .pdpack file
 * session_only: 1 = install to mods/.temp/ (session only), 0 = permanent
 * out:          optional result struct (may be NULL)
 *
 * Each component is extracted to mods/{category}/{id}/ (or .temp/),
 * then hot-registered in the Asset Catalog via the same path used by
 * D3R-9 network distribution. Call modmgrCatalogChanged() after if needed.
 *
 * Returns number of components imported, -1 on fatal error.
 */
s32 modpackImport(const char *pack_path, s32 session_only,
                  modpack_import_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* _IN_MODPACK_H */
