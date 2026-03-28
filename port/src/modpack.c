/**
 * modpack.c -- D3R-10: Mod pack export/import (.pdpack format)
 *
 * See modpack.h for the PDPK wire format.
 *
 * PDCA archive builder and extractor are adapted from D3R-9 (netdistrib.c)
 * and kept as file-local statics to avoid coupling. The hot-registration
 * path follows the identical pattern to netDistribClientHandleEnd().
 *
 * Dependencies (all already linked):
 *   - zlib  : compress2 / uncompress
 *   - dirent: opendir / readdir (MinGW/MSYS2)
 *
 * Note: C11 game-code conventions apply here:
 *   - bool is s32 (never include <stdbool.h>)
 *   - u8/u16/u32/s32/f32 from PR/ultratypes.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>
#include <time.h>

#include <PR/ultratypes.h>
#include "types.h"
#include "modpack.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "system.h"
#include "fs.h"

/* PDCA archive magic — same as D3R-9 netdistrib.c */
#define PDCA_MAGIC 0x41434450u   /* "PDCA" little-endian */

/* ========================================================================
 * INI filename lookup (mirrors pdgui_menu_moddinghub.cpp iniNameForType)
 * ======================================================================== */

static const char *iniFilenameForType(asset_type_e type)
{
    switch (type) {
        case ASSET_MAP:          return "map.ini";
        case ASSET_CHARACTER:    return "character.ini";
        case ASSET_SKIN:         return "skin.ini";
        case ASSET_BOT_VARIANT:  return "bot.ini";
        case ASSET_WEAPON:       return "weapon.ini";
        case ASSET_TEXTURES:     return "textures.ini";
        case ASSET_SFX:          return "sfx.ini";
        case ASSET_MUSIC:        return "music.ini";
        case ASSET_PROP:         return "prop.ini";
        case ASSET_VEHICLE:      return "vehicle.ini";
        case ASSET_MISSION:      return "mission.ini";
        case ASSET_UI:           return "ui.ini";
        case ASSET_TOOL:         return "tool.ini";
        default:                 return NULL;
    }
}

/* Ordered list of INI filenames tried during hot-registration */
static const char *s_KnownIniNames[] = {
    "map.ini", "character.ini", "bot.ini", "textures.ini",
    "skin.ini", "weapon.ini", "sfx.ini", "music.ini",
    "prop.ini", "vehicle.ini", "mission.ini", "ui.ini", "tool.ini",
    NULL
};

/* ========================================================================
 * Catalog lookup — ignores enabled state (iterate finds all entries)
 * ======================================================================== */

typedef struct {
    const char *id;
    const asset_entry_t *found;
} find_ctx_t;

static void findByIdCb(const asset_entry_t *e, void *ud)
{
    find_ctx_t *ctx = (find_ctx_t *)ud;
    if (!ctx->found && strcmp(e->id, ctx->id) == 0) {
        ctx->found = e;
    }
}

static const asset_entry_t *catalogFindByIdAny(const char *id)
{
    static const asset_type_e kTypes[] = {
        ASSET_MAP, ASSET_CHARACTER, ASSET_SKIN, ASSET_BOT_VARIANT,
        ASSET_WEAPON, ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC,
        ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_UI, ASSET_TOOL,
        ASSET_NONE
    };
    find_ctx_t ctx;
    ctx.id    = id;
    ctx.found = NULL;
    for (s32 i = 0; kTypes[i] != ASSET_NONE && !ctx.found; i++) {
        assetCatalogIterateByType(kTypes[i], findByIdCb, &ctx);
    }
    return ctx.found;
}

/* ========================================================================
 * PDCA Archive Builder — adapted from D3R-9 buildArchiveDir
 *
 * Recursively walks abspath, appending file entries to a heap buffer.
 * relprefix is the path prefix relative to the component root (empty for root).
 * ======================================================================== */

static u8 *buildArchiveDir(u8 *buf, u32 *buf_len, u32 *buf_cap,
                            const char *abspath, const char *relprefix)
{
    DIR *d = opendir(abspath);
    if (!d) return buf;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;   /* skip . .. hidden files */

        char childabs[FS_MAXPATH];
        char childrel[FS_MAXPATH];
        snprintf(childabs, sizeof(childabs), "%s/%s", abspath, ent->d_name);
        if (relprefix[0]) {
            snprintf(childrel, sizeof(childrel), "%s/%s", relprefix, ent->d_name);
        } else {
            snprintf(childrel, sizeof(childrel), "%s", ent->d_name);
        }

        struct stat st;
        if (stat(childabs, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            buf = buildArchiveDir(buf, buf_len, buf_cap, childabs, childrel);
            continue;
        }

        if (!S_ISREG(st.st_mode)) continue;

        FILE *fp = fopen(childabs, "rb");
        if (!fp) {
            sysLogPrintf(LOG_WARNING, "MODPACK: can't open %s", childabs);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        u32 fsize = (u32)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        u16 path_len  = (u16)(strlen(childrel) + 1);
        u32 entry_sz  = 2u + path_len + 4u + fsize;

        /* Grow buffer to fit entry */
        while (*buf_len + entry_sz > *buf_cap) {
            u32 newcap = (*buf_cap < 65536u) ? 65536u : *buf_cap * 2u;
            u8 *nb = (u8 *)realloc(buf, newcap);
            if (!nb) {
                sysLogPrintf(LOG_ERROR, "MODPACK: OOM building archive");
                fclose(fp);
                closedir(d);
                return buf;
            }
            buf      = nb;
            *buf_cap = newcap;
        }

        u8 *p = buf + *buf_len;
        memcpy(p, &path_len, 2);       p += 2;
        memcpy(p, childrel, path_len); p += path_len;
        memcpy(p, &fsize, 4);          p += 4;

        if (fread(p, 1, fsize, fp) != fsize) {
            sysLogPrintf(LOG_WARNING, "MODPACK: short read %s", childabs);
            fclose(fp);
            continue;
        }
        *buf_len += entry_sz;
        fclose(fp);
    }

    closedir(d);
    return buf;
}

/**
 * Build a PDCA archive from a component directory.
 * Returns heap-allocated buffer (caller must free), or NULL on error.
 * Sets *out_len to the archive size.
 */
static u8 *buildComponentArchive(const char *dirpath, u32 *out_len)
{
    u32 buf_cap = 65536u;
    u32 buf_len = 6u;       /* magic(4) + file_count(2) placeholder */
    u8 *buf = (u8 *)malloc(buf_cap);
    if (!buf) return NULL;

    memset(buf, 0, 6);
    u32 magic = PDCA_MAGIC;
    memcpy(buf, &magic, 4);

    buf = buildArchiveDir(buf, &buf_len, &buf_cap, dirpath, "");

    /* Count files by scanning data section (offset 6 onward) */
    u16 file_count = 0;
    {
        const u8 *p   = buf + 6;
        const u8 *end = buf + buf_len;
        while (p < end) {
            u16 plen;
            if (p + 2 > end) break;
            memcpy(&plen, p, 2); p += 2;
            if (p + plen > end) break;
            p += plen;
            if (p + 4 > end) break;
            u32 dlen;
            memcpy(&dlen, p, 4);
            p += 4 + dlen;
            file_count++;
        }
    }
    memcpy(buf + 4, &file_count, 2);

    *out_len = buf_len;
    sysLogPrintf(LOG_NOTE, "MODPACK: archive %u files, %u bytes raw",
                 file_count, buf_len);
    return buf;
}

/* ========================================================================
 * PDCA Archive Extractor — adapted from D3R-9 extractArchive
 * ======================================================================== */

static s32 extractArchive(const u8 *data, u32 data_len, const char *destdir)
{
    if (data_len < 6) return 0;

    u32 magic;
    memcpy(&magic, data, 4);
    if (magic != PDCA_MAGIC) {
        sysLogPrintf(LOG_ERROR, "MODPACK: bad archive magic 0x%08x", magic);
        return 0;
    }

    u16 file_count;
    memcpy(&file_count, data + 4, 2);

    const u8 *p   = data + 6;
    const u8 *end = data + data_len;
    s32 extracted = 0;

    for (u16 i = 0; i < file_count; i++) {
        if (p + 2 > end) break;
        u16 path_len;
        memcpy(&path_len, p, 2); p += 2;

        if (p + path_len > end) break;
        const char *relpath = (const char *)p; p += path_len;

        if (p + 4 > end) break;
        u32 dlen;
        memcpy(&dlen, p, 4); p += 4;

        if (p + dlen > end) break;
        const u8 *fdata = p; p += dlen;

        char outpath[FS_MAXPATH];
        snprintf(outpath, sizeof(outpath), "%s/%s", destdir, relpath);

        /* Create parent directory if needed */
        char dirpath[FS_MAXPATH];
        snprintf(dirpath, sizeof(dirpath), "%s", outpath);
        char *slash = strrchr(dirpath, '/');
        if (slash) { *slash = '\0'; fsCreateDir(dirpath); }

        FILE *fp = fopen(outpath, "wb");
        if (!fp) {
            sysLogPrintf(LOG_WARNING, "MODPACK: can't write %s", outpath);
            continue;
        }
        fwrite(fdata, 1, dlen, fp);
        fclose(fp);
        extracted++;
    }

    sysLogPrintf(LOG_NOTE, "MODPACK: extracted %d/%d files to %s",
                 extracted, file_count, destdir);
    return (extracted > 0) ? 1 : 0;
}

/* ========================================================================
 * Hot-register a newly extracted component in the Asset Catalog.
 * Follows the identical pattern to netDistribClientHandleEnd().
 * ======================================================================== */

static void hotRegisterComponent(const char *id, const char *category,
                                 const char *destdir, s32 temporary)
{
    ini_section_t ini;
    char inipath[FS_MAXPATH];
    s32 registered = 0;

    for (s32 k = 0; s_KnownIniNames[k] && !registered; k++) {
        snprintf(inipath, sizeof(inipath), "%s/%s", destdir, s_KnownIniNames[k]);
        if (iniParse(inipath, &ini)) {
            asset_entry_t *e = assetCatalogRegister(id, ASSET_NONE);
            if (e) {
                strncpy(e->id,       id,       sizeof(e->id) - 1);
                strncpy(e->category, category, sizeof(e->category) - 1);
                strncpy(e->dirpath,  destdir,  sizeof(e->dirpath) - 1);
                e->enabled    = 1;
                e->temporary  = temporary;
                e->bundled    = 0;
                e->model_scale = iniGetFloat(&ini, "model_scale", 1.0f);
                sysLogPrintf(LOG_NOTE, "MODPACK: hot-registered '%s'", id);
                registered = 1;
            }
        }
    }

    if (!registered) {
        sysLogPrintf(LOG_WARNING,
                     "MODPACK: no recognized INI for '%s' -- catalog entry skipped", id);
    }
}

/* ========================================================================
 * PDPK String I/O helpers
 *
 * Format: u16 len (including null terminator) + char data[len]
 * ======================================================================== */

static s32 writeStr(FILE *f, const char *s)
{
    u16 len = (u16)(strlen(s) + 1);
    if (fwrite(&len, 2, 1, f) != 1) return 0;
    if (fwrite(s, 1, len, f) != 1) return 0;
    return 1;
}

static s32 readStr(FILE *f, char *out, s32 maxlen)
{
    u16 len;
    if (fread(&len, 2, 1, f) != 1) return 0;
    if (len == 0 || (s32)len > maxlen) {
        /* Skip over-long or zero-length string */
        if (len > 0) fseek(f, len, SEEK_CUR);
        if (out) out[0] = '\0';
        return 0;
    }
    if (fread(out, 1, len, f) != (size_t)len) return 0;
    out[len - 1] = '\0';   /* ensure null termination even if malformed */
    return 1;
}

/* ========================================================================
 * Manifest text parser (simple key=value, INI-style)
 * ======================================================================== */

static void parseManifestText(const char *text, u32 len, modpack_manifest_t *out)
{
    char buf[4096];
    u32 cplen = (len < sizeof(buf) - 1) ? len : (u32)(sizeof(buf) - 1);
    memcpy(buf, text, cplen);
    buf[cplen] = '\0';

    char *p = buf;
    while (*p) {
        /* Find line end */
        char *line_end = p;
        while (*line_end && *line_end != '\n') line_end++;
        char saved = *line_end;
        *line_end = '\0';

        /* Strip trailing \r */
        s32 llen = (s32)strlen(p);
        if (llen > 0 && p[llen - 1] == '\r') p[llen - 1] = '\0';

        /* Parse key=value (skip comments and section headers) */
        if (*p != '#' && *p != ';' && *p != '[' && *p != '\0') {
            char *eq = strchr(p, '=');
            if (eq) {
                *eq = '\0';
                const char *key = p;
                const char *val = eq + 1;
                if      (strcmp(key, "name")    == 0)
                    strncpy(out->name,    val, sizeof(out->name) - 1);
                else if (strcmp(key, "author")  == 0)
                    strncpy(out->author,  val, sizeof(out->author) - 1);
                else if (strcmp(key, "version") == 0)
                    strncpy(out->version, val, sizeof(out->version) - 1);
                /* component_count is also in per-component records; ignore here */
            }
        }

        if (saved == '\0') break;
        p = line_end + 1;
    }
}

/* ========================================================================
 * Public API — Export
 * ======================================================================== */

s32 modpackExport(const char * const *component_ids, s32 count,
                  const char *pack_name, const char *pack_author,
                  const char *pack_version,
                  const char *output_path,
                  char *out_error, s32 error_buf_len)
{
#define ERRF(fmt, ...) \
    do { if (out_error) snprintf(out_error, error_buf_len, fmt, ##__VA_ARGS__); } while (0)

    if (!component_ids || count <= 0 || !output_path || !output_path[0]) {
        ERRF("Invalid arguments"); return -1;
    }

    FILE *f = fopen(output_path, "wb");
    if (!f) { ERRF("Cannot create: %s", output_path); return -1; }

    /* --- PDPK header --- */
    {
        u32 magic   = PDPK_MAGIC;
        u32 version = PDPK_VERSION;
        u32 ccount  = (u32)count;
        fwrite(&magic,   4, 1, f);
        fwrite(&version, 4, 1, f);
        fwrite(&ccount,  4, 1, f);
    }

    /* --- Manifest text --- */
    {
        char date[32] = "unknown";
        time_t t = time(NULL);
        struct tm *ti = localtime(&t);
        if (ti) strftime(date, sizeof(date), "%Y-%m-%d", ti);

        /* Build comma-separated component list */
        char complist[2048] = "";
        for (s32 i = 0; i < count; i++) {
            if (i > 0)
                strncat(complist, ",", sizeof(complist) - strlen(complist) - 1);
            strncat(complist, component_ids[i],
                    sizeof(complist) - strlen(complist) - 1);
        }

        char manifest[4096];
        s32 mlen = snprintf(manifest, sizeof(manifest),
            "[pdpack]\n"
            "name=%s\n"
            "author=%s\n"
            "version=%s\n"
            "created=%s\n"
            "component_count=%d\n"
            "components=%s\n",
            pack_name    ? pack_name    : "Unnamed Pack",
            pack_author  ? pack_author  : "Unknown",
            pack_version ? pack_version : "1.0.0",
            date, count, complist);

        u32 manifest_len = (mlen > 0) ? (u32)mlen : 0u;
        fwrite(&manifest_len, 4, 1, f);
        if (manifest_len > 0) fwrite(manifest, 1, manifest_len, f);
    }

    /* --- Component records --- */
    s32 exported = 0;
    for (s32 i = 0; i < count; i++) {
        const char *id = component_ids[i];
        const asset_entry_t *entry = catalogFindByIdAny(id);
        if (!entry) {
            sysLogPrintf(LOG_WARNING,
                         "MODPACK: export: '%s' not in catalog -- skipped", id);
            continue;
        }
        if (entry->bundled) {
            sysLogPrintf(LOG_NOTE,
                         "MODPACK: export: '%s' is base-game bundled -- skipped", id);
            continue;
        }

        /* Try to read version from component's .ini */
        char ver[MODPACK_VERSION_LEN] = "1.0.0";
        const char *ini_fname = iniFilenameForType(entry->type);
        if (ini_fname) {
            char ini_path[FS_MAXPATH + 32];
            snprintf(ini_path, sizeof(ini_path), "%s/%s", entry->dirpath, ini_fname);
            ini_section_t ini;
            if (iniParse(ini_path, &ini)) {
                const char *v = iniGet(&ini, "version", NULL);
                if (v && v[0]) {
                    strncpy(ver, v, sizeof(ver) - 1);
                    ver[sizeof(ver) - 1] = '\0';
                }
            }
        }

        /* Write component ID / category / version strings */
        if (!writeStr(f, id))              goto write_error;
        if (!writeStr(f, entry->category)) goto write_error;
        if (!writeStr(f, ver))             goto write_error;

        /* Build PDCA archive for this component's directory */
        u32 raw_len = 0;
        u8 *raw = buildComponentArchive(entry->dirpath, &raw_len);
        if (!raw || !raw_len) {
            sysLogPrintf(LOG_ERROR, "MODPACK: export: archive failed for '%s'", id);
            /* Write a zero-size placeholder so the file stays parseable */
            if (raw) free(raw);
            u32 zero = 0; u8 no_comp = 0;
            fwrite(&zero, 4, 1, f); fwrite(&zero, 4, 1, f);
            fwrite(&no_comp, 1, 1, f);
            continue;
        }

        /* Compress PDCA with zlib */
        uLongf cmp_cap = compressBound((uLong)raw_len);
        u8 *cmp = (u8 *)malloc(cmp_cap);

        u8        compression  = 0;
        const u8 *stored       = raw;
        u32       stored_size  = raw_len;

        if (cmp) {
            uLongf cmp_len = cmp_cap;
            if (compress2(cmp, &cmp_len, raw, (uLong)raw_len,
                          Z_DEFAULT_COMPRESSION) == Z_OK
                && cmp_len < raw_len) {
                stored       = cmp;
                stored_size  = (u32)cmp_len;
                compression  = 1;
            }
        }

        fwrite(&raw_len,     4, 1, f);
        fwrite(&stored_size, 4, 1, f);
        fwrite(&compression, 1, 1, f);
        fwrite(stored, 1, stored_size, f);

        sysLogPrintf(LOG_NOTE,
                     "MODPACK: export: '%s' raw=%u stored=%u comp=%d",
                     id, raw_len, stored_size, (s32)compression);

        free(raw);
        if (cmp) free(cmp);
        exported++;
        continue;

write_error:
        sysLogPrintf(LOG_ERROR, "MODPACK: export: write error at '%s'", id);
        free(raw);
        fclose(f);
        remove(output_path);
        ERRF("Write error while exporting '%s'", id);
        return -1;
    }

    fclose(f);

    if (exported == 0) {
        ERRF("No components were exportable (all bundled or missing)");
        remove(output_path);
        return -1;
    }

    sysLogPrintf(LOG_NOTE, "MODPACK: exported %d components to %s",
                 exported, output_path);
    return 0;

#undef ERRF
}

/* ========================================================================
 * Public API — Read Manifest
 * ======================================================================== */

s32 modpackReadManifest(const char *pack_path, modpack_manifest_t *out)
{
    if (!pack_path || !out) return 0;
    memset(out, 0, sizeof(*out));

    FILE *f = fopen(pack_path, "rb");
    if (!f) return 0;

    u32 magic, version, component_count;
    if (fread(&magic,           4, 1, f) != 1 || magic   != PDPK_MAGIC)  goto fail;
    if (fread(&version,         4, 1, f) != 1 || version != PDPK_VERSION) goto fail;
    if (fread(&component_count, 4, 1, f) != 1) goto fail;
    if (component_count > (u32)MODPACK_MAX_COMPONENTS * 4) goto fail;  /* sanity cap */

    /* Read manifest text */
    {
        u32 manifest_len;
        if (fread(&manifest_len, 4, 1, f) != 1) goto fail;
        if (manifest_len > 65536u) goto fail;   /* sanity cap */

        char *manifest = (char *)malloc(manifest_len + 1);
        if (!manifest) goto fail;

        if (fread(manifest, 1, manifest_len, f) != (size_t)manifest_len) {
            free(manifest); goto fail;
        }
        manifest[manifest_len] = '\0';
        parseManifestText(manifest, manifest_len, out);
        free(manifest);
    }

    /* Read per-component records — id/category/version + skip archive data */
    u32 max_comp = (component_count < (u32)MODPACK_MAX_COMPONENTS)
                   ? component_count : (u32)MODPACK_MAX_COMPONENTS;

    for (u32 i = 0; i < component_count; i++) {
        char id[CATALOG_ID_LEN]        = "";
        char cat[CATALOG_CATEGORY_LEN] = "";
        char ver[MODPACK_VERSION_LEN]  = "";

        if (!readStr(f, id,  sizeof(id)))  goto fail;
        if (!readStr(f, cat, sizeof(cat))) goto fail;
        if (!readStr(f, ver, sizeof(ver))) goto fail;

        if (i < max_comp) {
            modpack_component_info_t *ci = &out->components[out->component_count++];
            strncpy(ci->id,       id,  sizeof(ci->id) - 1);
            strncpy(ci->category, cat, sizeof(ci->category) - 1);
            strncpy(ci->version,  ver, sizeof(ci->version) - 1);
        }

        /* Skip archive blob: raw_size(4) + stored_size(4) + compression(1) + data */
        u32 raw_size, stored_size;
        u8  compression;
        if (fread(&raw_size,    4, 1, f) != 1) goto fail;
        if (fread(&stored_size, 4, 1, f) != 1) goto fail;
        if (fread(&compression, 1, 1, f) != 1) goto fail;
        if (stored_size > 0 && fseek(f, stored_size, SEEK_CUR) != 0) goto fail;
    }

    fclose(f);
    return 1;

fail:
    fclose(f);
    return 0;
}

/* ========================================================================
 * Public API — Validate
 * ======================================================================== */

s32 modpackValidate(const char *pack_path, modpack_validate_result_t *out)
{
    if (!pack_path || !out) return 0;
    memset(out, 0, sizeof(*out));
    out->valid = 1;

    modpack_manifest_t mf;
    if (!modpackReadManifest(pack_path, &mf)) {
        snprintf(out->warnings, sizeof(out->warnings),
                 "Cannot read pack file or wrong format");
        out->valid = 0;
        return 0;
    }

    char wbuf[512] = "";
    for (s32 i = 0; i < mf.component_count; i++) {
        const char *id = mf.components[i].id;
        if (assetCatalogHasEntry(id)) {
            out->conflict_count++;
            if (wbuf[0])
                strncat(wbuf, ", ", sizeof(wbuf) - strlen(wbuf) - 1);
            strncat(wbuf, id, sizeof(wbuf) - strlen(wbuf) - 1);
        }
    }

    if (out->conflict_count > 0) {
        snprintf(out->warnings, sizeof(out->warnings),
                 "Already installed: %s", wbuf);
    }

    return 1;   /* conflicts are warnings, not hard blocks */
}

/* ========================================================================
 * Public API — Import
 * ======================================================================== */

s32 modpackImport(const char *pack_path, s32 session_only,
                  modpack_import_result_t *out)
{
    modpack_import_result_t local_out;
    if (!out) out = &local_out;
    memset(out, 0, sizeof(*out));

    if (!pack_path || !pack_path[0]) {
        snprintf(out->error_msg, sizeof(out->error_msg), "No pack path given");
        return -1;
    }

    FILE *f = fopen(pack_path, "rb");
    if (!f) {
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "Cannot open: %s", pack_path);
        return -1;
    }

    /* Verify header */
    u32 magic, version, component_count;
    if (fread(&magic,           4, 1, f) != 1 || magic   != PDPK_MAGIC)  goto bad_file;
    if (fread(&version,         4, 1, f) != 1 || version != PDPK_VERSION) goto bad_file;
    if (fread(&component_count, 4, 1, f) != 1) goto bad_file;
    if (component_count > (u32)MODPACK_MAX_COMPONENTS * 4) goto bad_file;

    /* Skip manifest text */
    {
        u32 manifest_len;
        if (fread(&manifest_len, 4, 1, f) != 1) goto bad_file;
        if (manifest_len > 65536u) goto bad_file;
        if (manifest_len > 0 && fseek(f, manifest_len, SEEK_CUR) != 0) goto bad_file;
    }

    const char *modsdir = fsGetModDir();
    if (!modsdir) {
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "mods/ directory not found");
        fclose(f);
        return -1;
    }

    /* Process each component */
    u32 import_limit = (component_count < (u32)MODPACK_MAX_COMPONENTS)
                       ? component_count : (u32)MODPACK_MAX_COMPONENTS;

    for (u32 i = 0; i < import_limit; i++) {
        char id[CATALOG_ID_LEN]        = "";
        char category[CATALOG_CATEGORY_LEN] = "";
        char ver[MODPACK_VERSION_LEN]  = "";

        if (!readStr(f, id,       sizeof(id)))       goto bad_file;
        if (!readStr(f, category, sizeof(category))) goto bad_file;
        if (!readStr(f, ver,      sizeof(ver)))       goto bad_file;

        u32 raw_size, stored_size;
        u8  compression;
        if (fread(&raw_size,    4, 1, f) != 1) goto bad_file;
        if (fread(&stored_size, 4, 1, f) != 1) goto bad_file;
        if (fread(&compression, 1, 1, f) != 1) goto bad_file;

        /* Safety: skip zero-size entries (export wrote placeholder on error) */
        if (raw_size == 0 || stored_size == 0) {
            out->skipped_count++;
            continue;
        }

        /* Read stored archive data */
        u8 *stored = (u8 *)malloc(stored_size);
        if (!stored) {
            sysLogPrintf(LOG_ERROR, "MODPACK: OOM for '%s' (%u bytes)", id, stored_size);
            out->error_count++;
            fseek(f, stored_size, SEEK_CUR);
            continue;
        }
        if (fread(stored, 1, stored_size, f) != (size_t)stored_size) {
            free(stored); out->error_count++; continue;
        }

        /* Decompress if needed */
        u8 *raw;
        if (compression == 1) {
            raw = (u8 *)malloc(raw_size);
            if (!raw) {
                sysLogPrintf(LOG_ERROR, "MODPACK: OOM decompress '%s'", id);
                free(stored); out->error_count++; continue;
            }
            uLongf actual = (uLongf)raw_size;
            if (uncompress(raw, &actual, stored, (uLong)stored_size) != Z_OK) {
                sysLogPrintf(LOG_ERROR, "MODPACK: decompress failed for '%s'", id);
                free(raw); free(stored); out->error_count++; continue;
            }
            free(stored);
        } else {
            /* Raw (uncompressed) */
            raw    = stored;
            stored = NULL;
        }

        /* Build destination directory path */
        char destdir[FS_MAXPATH];
        if (session_only) {
            snprintf(destdir, sizeof(destdir), "%s/.temp/%s/%s",
                     modsdir, category, id);
        } else {
            snprintf(destdir, sizeof(destdir), "%s/%s/%s",
                     modsdir, category, id);
        }

        /* Ensure intermediate directories exist */
        {
            char parent[FS_MAXPATH];
            if (session_only) {
                snprintf(parent, sizeof(parent), "%s/.temp", modsdir);
                fsCreateDir(parent);
                snprintf(parent, sizeof(parent), "%s/.temp/%s", modsdir, category);
            } else {
                snprintf(parent, sizeof(parent), "%s/%s", modsdir, category);
            }
            fsCreateDir(parent);
        }
        fsCreateDir(destdir);

        /* Extract files and hot-register */
        if (extractArchive(raw, raw_size, destdir)) {
            hotRegisterComponent(id, category, destdir, session_only);
            out->imported_count++;
        } else {
            sysLogPrintf(LOG_WARNING, "MODPACK: extract failed for '%s'", id);
            out->error_count++;
        }

        free(raw);
    }

    /* Skip remaining components if component_count > import_limit */
    fclose(f);

    sysLogPrintf(LOG_NOTE, "MODPACK: import done: %d imported, %d skipped, %d errors",
                 out->imported_count, out->skipped_count, out->error_count);
    return out->imported_count;

bad_file:
    snprintf(out->error_msg, sizeof(out->error_msg),
             "Pack file corrupt or wrong version (expected PDPK v%u)", PDPK_VERSION);
    fclose(f);
    return -1;
}
