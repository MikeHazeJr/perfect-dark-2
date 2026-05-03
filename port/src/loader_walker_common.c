/**
 * loader_walker_common.c -- Catalog universality pivot Step 4 (2026-05-03).
 *
 * Scaffold for the per-kind directory walker. See loader_walker_common.h
 * for the contract. Iterates a per-kind subdirectory under data/<romid>/,
 * opens each *.<ext> file, peeks 2 bytes to decide between plain JSON and
 * ZIP compound, extracts the manifest envelope (pd_kind + id), and
 * dispatches to the per-kind registration callback.
 *
 * The envelope extractor is deliberately lightweight: it does not run a
 * full JSON parser, only enough to find the `"key": "value"` or
 * `"key": <number>` shape for top-level scalar fields. The 13 schemas
 * locked at Step 0 carry envelope fields at the top level of the manifest
 * JSON; cross-references and nested structures are the per-kind register
 * callback's responsibility, not the scaffold's.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "fs.h"
#include "loader_walker_common.h"
#include "modarchive.h"
#include "system.h"

/* ------------------------------------------------------------------ */
/* Envelope JSON helpers                                              */
/* ------------------------------------------------------------------ */

/* Find `"key"` (with surrounding quotes) inside `json`, return pointer to
 * the byte AFTER the closing quote of the key, NULL if not found. */
static const char *s_findKey(const char *json, size_t json_len,
                             const char *key)
{
    const char *end = json + json_len;
    size_t klen = strlen(key);
    /* Need at least: "<key>" -- 2 + klen bytes minimum. */
    if (klen == 0 || klen + 2 > json_len) return NULL;

    const char *p = json;
    while (p + klen + 2 <= end) {
        if (*p == '"') {
            if (memcmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
                return p + klen + 2;
            }
        }
        p++;
    }
    return NULL;
}

/* Skip whitespace + an optional ':' separator after a key. Returns pointer
 * to the first non-ws char after the colon, or NULL if no colon. */
static const char *s_skipKeyColon(const char *p, const char *end)
{
    while (p < end && isspace((unsigned char)*p)) p++;
    if (p >= end || *p != ':') return NULL;
    p++;
    while (p < end && isspace((unsigned char)*p)) p++;
    return (p < end) ? p : NULL;
}

s32 loaderWalkerEnvelopeStr(const char *json, size_t json_len,
                            const char *key,
                            const char **out_value, size_t *out_len)
{
    if (!json || !key || !out_value || !out_len) return 0;
    *out_value = NULL;
    *out_len = 0;

    const char *end = json + json_len;
    const char *after_key = s_findKey(json, json_len, key);
    if (!after_key) return 0;
    const char *p = s_skipKeyColon(after_key, end);
    if (!p || *p != '"') return 0;
    const char *vstart = p + 1;
    const char *q = vstart;
    while (q < end && *q != '"') {
        /* Skip over JSON escape sequences (\\", \\\\, \\n, etc.) */
        if (*q == '\\' && q + 1 < end) q += 2;
        else q++;
    }
    if (q >= end) return 0;
    *out_value = vstart;
    *out_len = (size_t)(q - vstart);
    return 1;
}

s32 loaderWalkerEnvelopeStrCopy(const char *json, size_t json_len,
                                 const char *key, char *out, size_t out_n)
{
    if (!out || out_n == 0) return 0;
    out[0] = '\0';
    const char *v = NULL;
    size_t vlen = 0;
    if (!loaderWalkerEnvelopeStr(json, json_len, key, &v, &vlen)) return 0;
    if (vlen >= out_n) vlen = out_n - 1;
    memcpy(out, v, vlen);
    out[vlen] = '\0';
    return 1;
}

s32 loaderWalkerEnvelopeInt(const char *json, size_t json_len,
                            const char *key, s64 *out_value)
{
    if (!json || !key || !out_value) return 0;
    *out_value = 0;

    const char *end = json + json_len;
    const char *after_key = s_findKey(json, json_len, key);
    if (!after_key) return 0;
    const char *p = s_skipKeyColon(after_key, end);
    if (!p) return 0;
    s32 sign = 1;
    if (*p == '-') { sign = -1; p++; }
    if (p >= end || !isdigit((unsigned char)*p)) return 0;
    s64 v = 0;
    while (p < end && isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        p++;
    }
    *out_value = v * sign;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Manifest extraction (plain JSON or ZIP)                            */
/* ------------------------------------------------------------------ */

/* Caller frees *out_buf with sysMemFree on success. */
static s32 s_loadManifest(const char *rel_path,
                          char **out_buf, size_t *out_len,
                          mod_archive_t **out_arc)
{
    *out_buf = NULL;
    *out_len = 0;
    if (out_arc) *out_arc = NULL;

    /* fsFileLoad reads via fsFullPath internally; works for the relative
     * data/<romid>/<class>/file form we pass in. */
    u32 size = 0;
    void *raw = fsFileLoad(rel_path, &size);
    if (!raw || size < 2) {
        if (raw) sysMemFree(raw);
        return 0;
    }

    const u8 *bytes = (const u8 *)raw;
    s32 is_zip = (bytes[0] == 'P' && bytes[1] == 'K');
    /* Plain JSON: fsFileLoad already null-terminates per its contract; reuse
     * the buffer. ASCII '{' / '[' both legal JSON top-levels. */
    if (!is_zip) {
        *out_buf = (char *)raw;
        *out_len = (size_t)size;
        return 1;
    }

    /* ZIP compound: free the raw bytes and re-open via modArchive so we
     * can extract just the "manifest.json" entry. */
    sysMemFree(raw);

    char fullBuf[FS_MAXPATH + 1];
    const char *full = fsFullPath(rel_path, fullBuf, sizeof(fullBuf));
    if (!full || !full[0]) return 0;

    mod_archive_t *arc = modArchiveOpen(full);
    if (!arc) return 0;

    s32 idx = modArchiveFindEntry(arc, "manifest.json");
    if (idx < 0) {
        modArchiveClose(arc);
        return 0;
    }
    u32 mlen = 0;
    void *mbytes = modArchiveExtractAlloc(arc, idx, &mlen);
    if (!mbytes) {
        modArchiveClose(arc);
        return 0;
    }
    /* modArchiveExtractAlloc uses malloc; we want sysMem semantics for the
     * outer caller. Copy into a sysMem buffer with a trailing NUL so the
     * envelope helpers stay null-terminated. */
    char *copy = (char *)sysMemAlloc(mlen + 1);
    if (!copy) {
        free(mbytes);
        modArchiveClose(arc);
        return 0;
    }
    memcpy(copy, mbytes, mlen);
    copy[mlen] = '\0';
    free(mbytes);

    *out_buf = copy;
    *out_len = (size_t)mlen;
    if (out_arc) *out_arc = arc;
    else         modArchiveClose(arc);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public scan                                                        */
/* ------------------------------------------------------------------ */

s32 loaderWalkerScanKind(
    const char                       *tier_dir,
    const loader_walker_kind_desc_t  *desc,
    loader_walker_register_fn         register_fn,
    loader_walker_kind_result_t      *out_result)
{
    loader_walker_kind_result_t local;
    memset(&local, 0, sizeof(local));
    if (out_result) memset(out_result, 0, sizeof(*out_result));

    if (!tier_dir || !desc || !register_fn || !desc->subdir || !desc->extension) {
        if (out_result) *out_result = local;
        return 0;
    }

    char dir_path[FS_MAXPATH];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", tier_dir, desc->subdir);

    char full_dir_buf[FS_MAXPATH + 1];
    const char *full_dir = fsFullPath(dir_path, full_dir_buf, sizeof(full_dir_buf));
    if (!full_dir || !full_dir[0]) {
        if (out_result) *out_result = local;
        return 0;
    }

    DIR *dp = opendir(full_dir);
    if (!dp) {
        /* Empty / missing dir is normal on first boot before extraction. */
        if (out_result) *out_result = local;
        return 0;
    }

    size_t ext_len = strlen(desc->extension);
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        const char *name = de->d_name;
        if (name[0] == '.') continue;
        size_t nlen = strlen(name);
        if (nlen <= ext_len) continue;
        if (strcmp(name + nlen - ext_len, desc->extension) != 0) continue;

        char rel_path[FS_MAXPATH];
        snprintf(rel_path, sizeof(rel_path), "%s/%s", dir_path, name);

        local.entries_scanned++;

        char *manifest = NULL;
        size_t manifest_len = 0;
        mod_archive_t *arc = NULL;
        if (!s_loadManifest(rel_path, &manifest, &manifest_len, &arc)) {
            sysLoudFailf("LOAD.UNIVERSAL.PARSE_FAIL",
                "manifest unreadable for kind=%s file=\"%s\"",
                desc->kind_str, rel_path);
            local.envelope_failures++;
            continue;
        }

        char kind_buf[32];
        char id_buf[128];
        if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "pd_kind",
                                          kind_buf, sizeof(kind_buf))) {
            sysLoudFailf("LOAD.UNIVERSAL.PARSE_FAIL",
                "missing pd_kind in \"%s\"", rel_path);
            local.envelope_failures++;
            goto next;
        }
        if (!loaderWalkerEnvelopeStrCopy(manifest, manifest_len, "id",
                                          id_buf, sizeof(id_buf))) {
            sysLoudFailf("LOAD.UNIVERSAL.PARSE_FAIL",
                "missing id in \"%s\"", rel_path);
            local.envelope_failures++;
            goto next;
        }
        if (strcmp(kind_buf, desc->kind_str) != 0) {
            sysLoudFailf("LOAD.SCHEMA.KIND_MISMATCH",
                "expected pd_kind=\"%s\" got \"%s\" in \"%s\"",
                desc->kind_str, kind_buf, rel_path);
            local.envelope_failures++;
            goto next;
        }

        /* Non-destructive overlay (default): if the catalog already
         * carries this id (registered by assetCatalogRegisterBaseGame +
         * RegisterStageSceneFiles + RegisterWeaponModelFiles +
         * ScanComponents before the walker runs), count it as success
         * without touching the existing row -- existing in-binary
         * registrations carry fields (model_file, langid pairings,
         * etc.) the .pd* envelope does not always re-supply.
         *
         * Pool kinds (weapon / head / body / arena) opt out via
         * desc->always_invoke so loader_pool can populate the typed
         * payload from .pd* content regardless of how the catalog row
         * was created. Their callbacks must internally guard against
         * destructive row overwrite (assetCatalogResolve check). */
        if (!desc->always_invoke && assetCatalogResolve(id_buf) != NULL) {
            local.entries_registered++;
            goto next;
        }

        s32 r = register_fn(manifest, manifest_len, kind_buf, id_buf, rel_path);
        if (r > 0)      local.entries_registered++;
        else if (r < 0) local.register_failures++;
        /* r == 0 is a benign skip. */

next:
        if (arc) modArchiveClose(arc);
        if (manifest) sysMemFree(manifest);
    }
    closedir(dp);

    sysLogPrintf(LOG_NOTE,
        "LOADER.UNIVERSAL.OK: kind=%s scanned=%d registered=%d "
        "envelope_failures=%d register_failures=%d",
        desc->kind_str, local.entries_scanned, local.entries_registered,
        local.envelope_failures, local.register_failures);

    if (out_result) *out_result = local;
    return 0;
}
