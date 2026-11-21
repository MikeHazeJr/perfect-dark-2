/**
 * modpack.c -- D3R-10: Mod pack export/import (.pdpack format)
 *
 * See modpack.h for the PDPK wire format.
 *
 * Component archives are built by modpack_archive.c: members in sorted path
 * order and no wall-clock field anywhere, so the same components always give
 * the same pack bytes (CAT-C02, T-MODDING-023). They are extracted only
 * through pdcaExtractArchiveBegin (pdca_extract_transaction.c), and every
 * folder name a pack supplies (category, component id) must be one safe path
 * component under the writable mods folder, so a pack can write nowhere else
 * (CAT-C01, T-MODDING-022). An import is all or nothing: if any component is
 * refused, every component already published is rolled back. The
 * hot-registration path follows netDistribClientHandleEnd().
 *
 * Dependencies (all already linked):
 *   - zlib  : compress2 / uncompress
 *
 * Note: C11 game-code conventions apply here:
 *   - bool is s32 (never include <stdbool.h>)
 *   - u8/u16/u32/s32/f32 from PR/ultratypes.h
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <PR/ultratypes.h>
#include "types.h"
#include "modpack.h"
#include "modpack_archive.h"
#include "modmgr.h"
#include "pdca_extract_transaction.h"
#include "asset_path_contract.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "system.h"
#include "fs.h"

/* Largest component a pack may carry, stored or unpacked. */
#define MODPACK_MAX_COMPONENT_BYTES (512u * 1024u * 1024u)

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
        case ASSET_PROJECTILE:   return "projectile.ini";
        case ASSET_ENTITY:       return "entity.ini";
        case ASSET_ARENA:        return "arena.ini";
        case ASSET_BODY:         return "body.ini";
        case ASSET_HEAD:         return "head.ini";
        case ASSET_MODEL:        return "mesh.ini";
        case ASSET_ANIMATION:    return "animation.ini";
        case ASSET_MATERIAL:     return "material.ini";
        case ASSET_TEXTURES:     return "textures.ini";
        case ASSET_TEXTURE:      return "texture.ini";
        case ASSET_SFX:          return "sfx.ini";
        case ASSET_MUSIC:        return "music.ini";
        case ASSET_AUDIO:        return "audio.ini";
        case ASSET_PROP:         return "prop.ini";
        case ASSET_VEHICLE:      return "vehicle.ini";
        case ASSET_MISSION:      return "mission.ini";
        case ASSET_GAMEMODE:     return "gamemode.ini";
        case ASSET_UI:           return "ui.ini";
        case ASSET_FONT:         return "font.ini";
        case ASSET_LANG:         return "lang.ini";
        case ASSET_HUD:          return "hud.ini";
        case ASSET_EFFECT:       return "effect.ini";
        case ASSET_BOT_PROFILE:  return "botprofile.ini";
        case ASSET_SCENARIO:     return "scenario.ini";
        case ASSET_THEME:        return "theme.ini";
        case ASSET_AI_LISTS:     return "ai.ini";
        case ASSET_TOOL:         return "tool.ini";
        default:                 return NULL;
    }
}

/* Ordered list of INI filenames tried during hot-registration */
static const char *s_KnownIniNames[] = {
    "map.ini", "character.ini", "bot.ini", "textures.ini",
    "skin.ini", "weapon.ini", "projectile.ini", "entity.ini",
    "arena.ini", "body.ini", "head.ini", "mesh.ini", "model.ini",
    "animation.ini",
    "material.ini", "texture.ini", "effect.ini", "sfx.ini", "music.ini",
    "audio.ini", "sound.ini", "voice.ini",
    "prop.ini", "vehicle.ini", "mission.ini", "gamemode.ini",
    "botprofile.ini", "scenario.ini", "hud.ini", "ui.ini", "font.ini",
    "lang.ini", "theme.ini", "ai.ini", "tool.ini",
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
        ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY,
        ASSET_ARENA, ASSET_BODY, ASSET_HEAD, ASSET_MODEL,
        ASSET_ANIMATION,
        ASSET_TEXTURES, ASSET_TEXTURE, ASSET_MATERIAL, ASSET_EFFECT,
        ASSET_SFX, ASSET_MUSIC, ASSET_AUDIO,
        ASSET_PROP, ASSET_VEHICLE, ASSET_MISSION, ASSET_GAMEMODE,
        ASSET_BOT_PROFILE, ASSET_SCENARIO, ASSET_HUD, ASSET_UI,
        ASSET_FONT, ASSET_LANG, ASSET_THEME, ASSET_AI_LISTS, ASSET_TOOL,
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
 * Component folders named by a pack (CAT-C01)
 * ======================================================================== */

static s32 lowerEquals(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ca != cb) return 0;
    }
    return *a == *b;
}

/**
 * Why a pack's component list cannot be installed, or NULL when it can.
 * Every category and id must be one safe folder name, and no two components
 * may land in the same folder (Windows ignores case).
 */
static const char *modpackFindUnsafeComponent(const modpack_manifest_t *mf,
                                              const char **which)
{
    *which = "";
    for (s32 i = 0; i < mf->component_count; i++) {
        const modpack_component_info_t *ci = &mf->components[i];
        *which = ci->id;
        if (!modpackArchiveNameIsSafe(ci->category)) return "names an unsafe category folder";
        if (!modpackArchiveNameIsSafe(ci->id)) return "names an unsafe component folder";
        for (s32 j = 0; j < i; j++) {
            if (lowerEquals(mf->components[j].category, ci->category)
                    && lowerEquals(mf->components[j].id, ci->id)) {
                return "appears twice in the pack";
            }
        }
    }
    *which = "";
    return NULL;
}

static const char *modpackExtractReason(pdca_extract_result_t result)
{
    switch (result) {
    case PDCA_EXTRACT_INVALID:           return "the archive is malformed";
    case PDCA_EXTRACT_PATH_REJECTED:     return "a file path would leave its folder";
    case PDCA_EXTRACT_STAGE_FAILED:      return "the folder could not be prepared";
    case PDCA_EXTRACT_OPEN_FAILED:       return "a file could not be created";
    case PDCA_EXTRACT_WRITE_FAILED:      return "a file could not be written";
    case PDCA_EXTRACT_PUBLISH_FAILED:    return "the folder could not be published";
    case PDCA_EXTRACT_RECOVERY_REQUIRED: return "an earlier import left a backup that needs recovery";
    default:                             return "extraction failed";
    }
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
        if (fwrite(&magic,   4, 1, f) != 1 ||
            fwrite(&version, 4, 1, f) != 1 ||
            fwrite(&ccount,  4, 1, f) != 1) {
            fclose(f); remove(output_path);
            ERRF("Write error in PDPK header"); return -1;
        }
    }

    /* --- Manifest text --- */
    /* No date or other wall-clock value: the same components must give the
     * same pack (CAT-C02). Readers ignore keys they do not know, so packs
     * written earlier with a created= line still read the same. */
    {
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
            "component_count=%d\n"
            "components=%s\n",
            pack_name    ? pack_name    : "Unnamed Pack",
            pack_author  ? pack_author  : "Unknown",
            pack_version ? pack_version : "1.0.0",
            count, complist);

        u32 manifest_len = (mlen > 0) ? (u32)mlen : 0u;
        if (fwrite(&manifest_len, 4, 1, f) != 1) {
            fclose(f); remove(output_path);
            ERRF("Write error in manifest header"); return -1;
        }
        if (manifest_len > 0 && fwrite(manifest, 1, manifest_len, f) != manifest_len) {
            fclose(f); remove(output_path);
            ERRF("Write error in manifest body"); return -1;
        }
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
        char build_err[256];
        u32 raw_len = 0;
        u8 *raw = modpackArchiveBuild(entry->dirpath, &raw_len, build_err, sizeof(build_err));
        if (!raw || !raw_len) {
            sysLogPrintf(LOG_ERROR, "MODPACK: export: archive failed for '%s': %s", id,
                         build_err[0] ? build_err : "empty archive");
            /* Write a zero-size placeholder so the file stays parseable */
            if (raw) free(raw);
            u32 zero = 0; u8 no_comp = 0;
            if (fwrite(&zero, 4, 1, f) != 1 || fwrite(&zero, 4, 1, f) != 1 ||
                fwrite(&no_comp, 1, 1, f) != 1) {
                fclose(f); remove(output_path);
                ERRF("Write error in zero placeholder for '%s'", id); return -1;
            }
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

        if (fwrite(&raw_len,     4, 1, f) != 1 ||
            fwrite(&stored_size, 4, 1, f) != 1 ||
            fwrite(&compression, 1, 1, f) != 1 ||
            fwrite(stored, 1, stored_size, f) != stored_size) goto write_error;

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

    /* A pack whose folder names could reach outside the mods folder is not
     * importable at all (CAT-C01); modpackImport refuses it the same way. */
    const char *unsafe_id = "";
    const char *unsafe = modpackFindUnsafeComponent(&mf, &unsafe_id);
    if (unsafe) {
        snprintf(out->warnings, sizeof(out->warnings),
                 "Refused: component '%s' %s", unsafe_id, unsafe);
        out->valid = 0;
        return 1;
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
 * Public API: Import
 * ======================================================================== */

/* One component published but not yet committed. */
typedef struct modpack_pending {
    pdca_extract_transaction_t transaction;
    char id[CATALOG_ID_LEN];
    char category[CATALOG_CATEGORY_LEN];
    char destdir[FS_MAXPATH];
} modpack_pending_t;

/* <mods>/<category>/<id>, or <mods>/.temp/<category>/<id> for a session. */
static s32 modpackDestination(char *out, size_t out_cap, const char *modsdir,
                              s32 session_only, const char *category, const char *id)
{
    char parent[FS_MAXPATH];
    if (session_only) {
        if (!assetPathJoinChecked(parent, sizeof(parent), modsdir, "/", ".temp")) return 0;
    } else if (!assetPathCopyChecked(parent, sizeof(parent), modsdir)) {
        return 0;
    }
    char category_dir[FS_MAXPATH];
    return assetPathJoinChecked(category_dir, sizeof(category_dir), parent, "/", category)
        && assetPathJoinChecked(out, out_cap, category_dir, "/", id);
}

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

    /* Every folder name the pack supplies is checked before anything is
     * written (CAT-C01). */
    modpack_manifest_t *mf = (modpack_manifest_t *)calloc(1, sizeof(*mf));
    if (!mf) {
        snprintf(out->error_msg, sizeof(out->error_msg), "Out of memory");
        return -1;
    }
    if (!modpackReadManifest(pack_path, mf)) {
        free(mf);
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "Pack file corrupt or wrong version (expected PDPK v%u)", PDPK_VERSION);
        return -1;
    }
    const char *unsafe_id = "";
    const char *unsafe = modpackFindUnsafeComponent(mf, &unsafe_id);
    if (unsafe) {
        sysLogPrintf(LOG_WARNING, "MODPACK: refused %s: component '%s' %s",
                     pack_path, unsafe_id, unsafe);
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "Pack refused: component '%s' %s", unsafe_id, unsafe);
        free(mf);
        return -1;
    }
    free(mf);

    /* The writable mods folder, never an enabled mod's own folder. */
    const char *modsdir = modmgrGetModsDir();
    if (!modsdir || !modsdir[0]) {
        snprintf(out->error_msg, sizeof(out->error_msg), "mods/ directory not found");
        return -1;
    }

    FILE *f = fopen(pack_path, "rb");
    if (!f) {
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "Cannot open: %s", pack_path);
        return -1;
    }

    modpack_pending_t *pending = NULL;
    s32 pending_count = 0;
    s32 refused = 0;

    /* Verify header */
    u32 magic, version, component_count;
    if (fread(&magic,           4, 1, f) != 1 || magic   != PDPK_MAGIC)  goto bad_file;
    if (fread(&version,         4, 1, f) != 1 || version != PDPK_VERSION) goto bad_file;
    if (fread(&component_count, 4, 1, f) != 1) goto bad_file;
    if (component_count > (u32)MODPACK_MAX_COMPONENTS) {
        snprintf(out->error_msg, sizeof(out->error_msg),
                 "Pack refused: %u components, the limit is %d",
                 component_count, MODPACK_MAX_COMPONENTS);
        fclose(f);
        return -1;
    }

    /* Skip manifest text */
    {
        u32 manifest_len;
        if (fread(&manifest_len, 4, 1, f) != 1) goto bad_file;
        if (manifest_len > 65536u) goto bad_file;
        if (manifest_len > 0 && fseek(f, manifest_len, SEEK_CUR) != 0) goto bad_file;
    }

    pending = (modpack_pending_t *)calloc(MODPACK_MAX_COMPONENTS, sizeof(*pending));
    if (!pending) {
        snprintf(out->error_msg, sizeof(out->error_msg), "Out of memory");
        fclose(f);
        return -1;
    }

    for (u32 i = 0; i < component_count && !refused; i++) {
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

        /* The names were checked above; this pass reads the same file again,
         * so check what it actually read. */
        if (!modpackArchiveNameIsSafe(category) || !modpackArchiveNameIsSafe(id)) {
            snprintf(out->error_msg, sizeof(out->error_msg),
                     "Pack refused: component '%s' names an unsafe folder", id);
            refused = 1;
            break;
        }
        if (raw_size > MODPACK_MAX_COMPONENT_BYTES || stored_size > MODPACK_MAX_COMPONENT_BYTES
                || compression > 1 || (compression == 0 && stored_size != raw_size)) {
            snprintf(out->error_msg, sizeof(out->error_msg),
                     "Pack refused: component '%s' has an invalid size or compression", id);
            refused = 1;
            break;
        }

        /* Read stored archive data */
        u8 *stored = (u8 *)malloc(stored_size);
        if (!stored) {
            snprintf(out->error_msg, sizeof(out->error_msg),
                     "Out of memory for component '%s'", id);
            refused = 1;
            break;
        }
        if (fread(stored, 1, stored_size, f) != (size_t)stored_size) {
            free(stored);
            goto bad_file;
        }

        /* Decompress if needed */
        u8 *raw;
        if (compression == 1) {
            raw = (u8 *)malloc(raw_size);
            uLongf actual = (uLongf)raw_size;
            if (!raw || uncompress(raw, &actual, stored, (uLong)stored_size) != Z_OK
                    || actual != (uLongf)raw_size) {
                free(raw);
                free(stored);
                snprintf(out->error_msg, sizeof(out->error_msg),
                         "Pack refused: component '%s' does not decompress", id);
                refused = 1;
                break;
            }
            free(stored);
        } else {
            /* Raw (uncompressed) */
            raw    = stored;
            stored = NULL;
        }

        modpack_pending_t *slot = &pending[pending_count];
        if (!modpackDestination(slot->destdir, sizeof(slot->destdir), modsdir,
                                session_only, category, id)) {
            free(raw);
            snprintf(out->error_msg, sizeof(out->error_msg),
                     "Pack refused: the folder for component '%s' is too long", id);
            refused = 1;
            break;
        }

        /* Publish the component through the checked extractor: no member can
         * leave slot->destdir, and a prior copy is kept until commit. */
        pdca_extract_result_t extracted = pdcaExtractArchiveBegin(raw, raw_size,
            slot->destdir, NULL, &slot->transaction);
        free(raw);
        if (extracted != PDCA_EXTRACT_OK) {
            sysLogPrintf(LOG_WARNING, "MODPACK: refused component '%s' (%s): %s",
                         id, slot->destdir, modpackExtractReason(extracted));
            snprintf(out->error_msg, sizeof(out->error_msg),
                     "Pack refused: component '%s': %s", id, modpackExtractReason(extracted));
            refused = 1;
            break;
        }
        snprintf(slot->id, sizeof(slot->id), "%s", id);
        snprintf(slot->category, sizeof(slot->category), "%s", category);
        pending_count++;
    }
    fclose(f);

    if (refused) {
        /* All or nothing: undo every component this pack already published,
         * restoring whatever was there before. */
        for (s32 k = pending_count - 1; k >= 0; k--) {
            if (!pdcaExtractTransactionRollback(&pending[k].transaction)) {
                sysLogPrintf(LOG_ERROR, "MODPACK: could not undo '%s' at %s; "
                             "its earlier copy is kept beside it for recovery",
                             pending[k].id, pending[k].destdir);
            }
        }
        out->error_count++;
        free(pending);
        return -1;
    }

    for (s32 k = 0; k < pending_count; k++) {
        if (pdcaExtractTransactionCommit(&pending[k].transaction) ==
                PDCA_EXTRACT_OK_BACKUP_RETAINED) {
            sysLogPrintf(LOG_WARNING, "MODPACK: '%s' installed; its earlier copy could not "
                         "be removed", pending[k].id);
        }
        hotRegisterComponent(pending[k].id, pending[k].category, pending[k].destdir,
                             session_only);
        out->imported_count++;
    }
    free(pending);

    sysLogPrintf(LOG_NOTE, "MODPACK: import done: %d imported, %d skipped, %d errors",
                 out->imported_count, out->skipped_count, out->error_count);
    return out->imported_count;

bad_file:
    for (s32 k = pending_count - 1; k >= 0; k--) {
        if (!pdcaExtractTransactionRollback(&pending[k].transaction)) {
            sysLogPrintf(LOG_ERROR, "MODPACK: could not undo '%s' at %s; "
                         "its earlier copy is kept beside it for recovery",
                         pending[k].id, pending[k].destdir);
        }
    }
    free(pending);
    snprintf(out->error_msg, sizeof(out->error_msg),
             "Pack file corrupt or wrong version (expected PDPK v%u)", PDPK_VERSION);
    fclose(f);
    return -1;
}
