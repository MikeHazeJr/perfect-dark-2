/**
 * loader_walker_common.h -- Catalog universality pivot Step 4 (2026-05-03).
 *
 * Per-kind walker scaffold. The 13 per-kind sources (loader_walker_<kind>.c)
 * each register a single descriptor + a registration callback through
 * loaderWalkerScanKind, which performs the directory iteration, opens each
 * .pd<ext> file (auto-detecting plain JSON vs ZIP compound by 2-byte magic),
 * extracts the manifest envelope (pd_kind + id), and invokes the callback.
 *
 * Boot order: must run AFTER romdataInit (so data/<romid>/ exists), AFTER
 * the per-kind emitters (romextract_pd*.c) on first launch (so files exist
 * to walk on the SAME boot), AND inside the catalog row registration phase
 * so registered IDs land in the in-memory catalog. The walker overlay is
 * idempotent: catalog rows are last-write-wins per assetCatalogRegister
 * semantics, so re-runs simply refresh.
 *
 * Companion docs:
 *   context/designs/catalog/universality-pivot-schemas.md   per-kind schemas
 *   context/audits/catalog-universality-pivot-plan-2026-05-02.md  Step 4 plan
 */
#ifndef PD_LOADER_WALKER_COMMON_H
#define PD_LOADER_WALKER_COMMON_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-kind static descriptor. */
typedef struct {
    const char *kind_str;     /* "weapon" / "head" / ... (matches pd_kind field) */
    const char *subdir;       /* "weapons", "audio/sfx", ... (under tier_dir) */
    const char *extension;    /* ".pdwpn" / ".pdmesh" / ... (with leading dot) */
    /* When non-zero, the scaffold invokes register_fn even if the
     * catalog row already exists. Set by the four pool kinds (weapon,
     * head, body, arena) so the callback can populate the loader_pool
     * payload regardless of how the row was created. Defaults to 0
     * (Step 4 non-destructive overlay) for the nine row-only kinds. */
    s32         always_invoke;
} loader_walker_kind_desc_t;

/* Per-kind result counters. */
typedef struct {
    s32 entries_scanned;     /* number of *.<ext> files inspected */
    s32 entries_registered;  /* successful catalog row registrations */
    s32 envelope_failures;   /* parse / extract / mismatch on envelope */
    s32 register_failures;   /* register callback returned -1 */
} loader_walker_kind_result_t;

/* Per-kind registration callback.
 *
 * Receives:
 *   manifest_json        null-terminated JSON content of the manifest
 *                         (full JSON file for plain-kind .pd<ext>; the
 *                         "manifest.json" entry's bytes for ZIP compounds)
 *   manifest_json_len    bytes of JSON (excluding the appended NUL)
 *   pd_kind              value of "pd_kind" field from envelope
 *   id                   value of "id" field from envelope (catalog ID)
 *   file_path            relative path under data/<romid>/ for diagnostics
 *
 * Returns: 1 = registered, 0 = skip (e.g. duplicate / unknown), -1 = failure.
 */
typedef s32 (*loader_walker_register_fn)(
    const char *manifest_json,
    size_t      manifest_json_len,
    const char *pd_kind,
    const char *id,
    const char *file_path);

/* Scan one tier directory's per-kind subdir. For each *.<ext> file:
 *   - load full file bytes via fsFileLoad
 *   - if first 2 bytes are "PK", treat as ZIP: open via modArchiveOpen,
 *     extract "manifest.json" entry into a fresh heap buffer
 *   - if first byte is '{' (or '[' as defensive fallback), treat as plain JSON
 *   - extract envelope ("pd_kind" + "id") from manifest bytes
 *   - if envelope's pd_kind does not match desc->kind_str, log
 *     LOADER.UNIVERSAL.SCHEMA_MISMATCH and increment envelope_failures
 *   - else invoke register_fn(manifest, manifest_len, pd_kind, id, rel_path)
 *
 * Returns 0 always. Per-file failures captured in *out_result counters.
 *
 * tier_dir: a directory like fsDataDir() returns ("data/ntsc-final"). */
s32 loaderWalkerScanKind(
    const char                       *tier_dir,
    const loader_walker_kind_desc_t  *desc,
    loader_walker_register_fn         register_fn,
    loader_walker_kind_result_t      *out_result);

/* String-field extractor.  Locates `"key": "value"` inside `json` and
 * returns 1 with *out_value pointing to the start of the unescaped value
 * inside `json` and *out_len set to its byte length.  The pointer remains
 * valid only until the caller frees the source buffer.
 *
 * No JSON-escape decoding: catalog IDs and pd_kind values are constrained
 * to [a-z0-9_:-] which the JSON encoder leaves untouched.
 *
 * Returns 0 if the key is absent or the value is not a string. */
s32 loaderWalkerEnvelopeStr(const char *json, size_t json_len,
                            const char *key,
                            const char **out_value, size_t *out_len);

/* Integer-field extractor.  Locates `"key": <number>` inside `json` and
 * parses an integer (supports leading minus, decimal digits).  Returns
 * 1 on success with *out_value populated, 0 if absent or not a number. */
s32 loaderWalkerEnvelopeInt(const char *json, size_t json_len,
                            const char *key, s64 *out_value);

/* Convenience: copy an envelope string field into a fixed-size buffer.
 * Returns 1 on success (buffer NUL-terminated, possibly truncated to
 * out_n - 1 bytes), 0 if absent. */
s32 loaderWalkerEnvelopeStrCopy(const char *json, size_t json_len,
                                 const char *key, char *out, size_t out_n);

#ifdef __cplusplus
}
#endif

#endif /* PD_LOADER_WALKER_COMMON_H */
