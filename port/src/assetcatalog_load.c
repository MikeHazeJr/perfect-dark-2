/**
 * assetcatalog_load.c -- Catalog intercept layer (C-4 through C-7)
 *
 * Builds four reverse-index arrays (filenum/texnum/animnum/soundnum → pool
 * index) from the populated catalog, then answers O(1) override queries from
 * the four gateway functions (romdataFileLoad, texLoad, animLoadFrame,
 * sndStart).
 *
 * Only non-bundled entries enter the override arrays. Bundled base-game
 * entries have source_filenum set for informational purposes but are never
 * routed through the override path.
 *
 * Auto-discovered by CMake glob. No build system changes needed.
 */

#include <PR/ultratypes.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "system.h"
#include "fs.h"

/* ========================================================================
 * Array bounds (conservative limits — actual ROM counts are smaller)
 * ======================================================================== */

#define LOAD_MAX_FILES   2048   /* matches ROMDATA_MAX_FILES in romdata.c */
#define LOAD_MAX_TEXTURES 4096
#define LOAD_MAX_ANIMS   2048
#define LOAD_MAX_SOUNDS  4096

/* ========================================================================
 * Reverse-index arrays
 *
 * s_FilenumOverride[filenum]   = pool index of the mod entry that overrides
 *                                ROM file <filenum>, or -1 if no override.
 * Parallel arrays for tex/anim/sound.
 *
 * Storing pool indices (not entry pointers) keeps the arrays stable across
 * pool growth — the pool base pointer may change on realloc but the index
 * stays valid.
 * ======================================================================== */

static s32 s_FilenumOverride[LOAD_MAX_FILES];
static s32 s_TexnumOverride[LOAD_MAX_TEXTURES];
static s32 s_AnimnumOverride[LOAD_MAX_ANIMS];
static s32 s_SoundnumOverride[LOAD_MAX_SOUNDS];

static s32 s_Initialized = 0;

/* ========================================================================
 * Initialization
 * ======================================================================== */

void catalogLoadInit(void)
{
    /* Reset all override arrays to "no override" */
    for (s32 i = 0; i < LOAD_MAX_FILES;    i++) { s_FilenumOverride[i]   = -1; }
    for (s32 i = 0; i < LOAD_MAX_TEXTURES; i++) { s_TexnumOverride[i]    = -1; }
    for (s32 i = 0; i < LOAD_MAX_ANIMS;    i++) { s_AnimnumOverride[i]   = -1; }
    for (s32 i = 0; i < LOAD_MAX_SOUNDS;   i++) { s_SoundnumOverride[i]  = -1; }

    s32 override_count = 0;
    s32 total = assetCatalogGetCount();

    for (s32 i = 0; i < total; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || !e->occupied || !e->enabled || e->bundled) {
            continue;
        }

        /* C-4: file override */
        if (e->source_filenum >= 0 && e->source_filenum < LOAD_MAX_FILES) {
            s_FilenumOverride[e->source_filenum] = i;
            override_count++;
        }

        /* C-5: texture override */
        if (e->source_texnum >= 0 && e->source_texnum < LOAD_MAX_TEXTURES) {
            s_TexnumOverride[e->source_texnum] = i;
            override_count++;
        }

        /* C-6: animation override */
        if (e->source_animnum >= 0 && e->source_animnum < LOAD_MAX_ANIMS) {
            s_AnimnumOverride[e->source_animnum] = i;
            override_count++;
        }

        /* C-7: sound override */
        if (e->source_soundnum >= 0 && e->source_soundnum < LOAD_MAX_SOUNDS) {
            s_SoundnumOverride[e->source_soundnum] = i;
            override_count++;
        }
    }

    s_Initialized = 1;
    sysLogPrintf(LOG_NOTE, "catalogLoadInit: %d override(s) indexed from %d catalog entries",
                 override_count, total);
}

/* ========================================================================
 * Override path helper
 *
 * Given a pool index and a type, return the best file path for that entry.
 * For ASSET_CHARACTER entries the bodyfile path is the override path;
 * for ASSET_TEXTURE the file_path field is used, etc.
 * ======================================================================== */

static const char *entryGetFilePath(const asset_entry_t *e)
{
    if (!e) {
        return NULL;
    }

    switch (e->type) {
    case ASSET_CHARACTER:
        return e->ext.character.bodyfile[0] ? e->ext.character.bodyfile : NULL;
    case ASSET_TEXTURE:
        return e->ext.texture.file_path[0] ? e->ext.texture.file_path : NULL;
    case ASSET_AUDIO:
        return e->ext.audio.file_path[0] ? e->ext.audio.file_path : NULL;
    default:
        /* For other types (MAP, PROP, WEAPON, etc.) the override path
         * is the component dirpath; the specific file name is resolved
         * by the caller using the INI fields. Return dirpath as a base. */
        return e->dirpath[0] ? e->dirpath : NULL;
    }
}

/* ========================================================================
 * Intercept Queries
 * ======================================================================== */

const char *catalogGetFileOverride(s32 filenum)
{
    if (!s_Initialized || filenum < 0 || filenum >= LOAD_MAX_FILES) {
        return NULL;
    }

    s32 idx = s_FilenumOverride[filenum];
    if (idx < 0) {
        return NULL;
    }

    return entryGetFilePath(assetCatalogGetByIndex(idx));
}

const char *catalogGetTextureOverride(s32 texnum)
{
    if (!s_Initialized || texnum < 0 || texnum >= LOAD_MAX_TEXTURES) {
        return NULL;
    }

    s32 idx = s_TexnumOverride[texnum];
    if (idx < 0) {
        return NULL;
    }

    return entryGetFilePath(assetCatalogGetByIndex(idx));
}

const char *catalogGetAnimOverride(s32 animnum)
{
    if (!s_Initialized || animnum < 0 || animnum >= LOAD_MAX_ANIMS) {
        return NULL;
    }

    s32 idx = s_AnimnumOverride[animnum];
    if (idx < 0) {
        return NULL;
    }

    return entryGetFilePath(assetCatalogGetByIndex(idx));
}

const char *catalogGetSoundOverride(s32 soundnum)
{
    if (!s_Initialized || soundnum < 0 || soundnum >= LOAD_MAX_SOUNDS) {
        return NULL;
    }

    s32 idx = s_SoundnumOverride[soundnum];
    if (idx < 0) {
        return NULL;
    }

    return entryGetFilePath(assetCatalogGetByIndex(idx));
}

/* ========================================================================
 * MEM-2: Asset Lifecycle API
 *
 * catalogLoadAsset  — load file data into entry.loaded_data, inc ref_count
 * catalogUnloadAsset — dec ref_count; free when it hits 0 (non-bundled only)
 * catalogRetainAsset — inc ref_count on an already-loaded entry
 *
 * Bundled (base-game) entries carry ref_count = ASSET_REF_BUNDLED and are
 * never loaded or evicted through these functions — their data lives in the
 * ROM segment for the lifetime of the process.
 *
 * Logging prefixes:
 *   "CATALOG:" — base-game bundled retain (informational)
 *   "MOD:"     — mod asset actually loaded or freed
 * ======================================================================== */

s32 catalogLoadAsset(const char *assetId)
{
    if (!assetId) {
        return 0;
    }

    asset_entry_t *entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        sysLogPrintf(LOG_WARNING, "catalogLoadAsset: '%s' not found in catalog", assetId);
        return 0;
    }

    /* Bundled assets are ROM-resident; treat as an implicit retain. */
    if (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED) {
        sysLogPrintf(LOG_NOTE, "CATALOG: retain bundled '%s'", assetId);
        return 1;
    }

    /* Already loaded — just bump the ref count. */
    if (entry->load_state >= ASSET_STATE_LOADED && entry->loaded_data) {
        entry->ref_count++;
        return 1;
    }

    /* Must be ENABLED to load. */
    if (!entry->enabled) {
        sysLogPrintf(LOG_WARNING, "MOD: catalogLoadAsset: '%s' is not enabled", assetId);
        return 0;
    }

    const char *path = entryGetFilePath(entry);
    if (!path || !path[0]) {
        sysLogPrintf(LOG_WARNING, "MOD: catalogLoadAsset: no file path for '%s'", assetId);
        return 0;
    }

    u32 size = 0;
    void *data = fsFileLoad(path, &size);
    if (!data || size == 0) {
        sysLogPrintf(LOG_WARNING, "MOD: catalogLoadAsset: failed to load '%s' from '%s'",
                     assetId, path);
        if (data) {
            sysMemFree(data);
        }
        return 0;
    }

    entry->loaded_data      = data;
    entry->data_size_bytes  = size;
    entry->load_state       = ASSET_STATE_LOADED;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE, "MOD: loaded '%s' (%u bytes) from '%s'", assetId, size, path);
    return 1;
}

void catalogUnloadAsset(const char *assetId)
{
    if (!assetId) {
        return;
    }

    asset_entry_t *entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return;
    }

    /* Never evict bundled assets. */
    if (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED) {
        return;
    }

    if (entry->ref_count > 0) {
        entry->ref_count--;
    }

    if (entry->ref_count <= 0 && entry->loaded_data) {
        sysLogPrintf(LOG_NOTE, "MOD: unloaded '%s' (%u bytes)", assetId, entry->data_size_bytes);
        sysMemFree(entry->loaded_data);
        entry->loaded_data     = NULL;
        entry->data_size_bytes = 0;
        entry->load_state      = ASSET_STATE_ENABLED;
        entry->ref_count       = 0;
    }
}

void catalogRetainAsset(const char *assetId)
{
    if (!assetId) {
        return;
    }

    asset_entry_t *entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return;
    }

    if (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED) {
        return;
    }

    if (entry->load_state >= ASSET_STATE_LOADED) {
        entry->ref_count++;
    }
}

/* ========================================================================
 * MEM-3 / C-9: Stage Transition Diff
 *
 * The diff separates the full entry pool into three buckets:
 *   - "currently loaded non-bundled" (load_state >= LOADED, !bundled)
 *   - "needed for new stage" (same category as newStageId's map entry,
 *                             enabled, !bundled)
 *   - shared = intersection → no-op (assets remain resident)
 *   - toUnload = loaded \ needed  → catalogUnloadAsset each
 *   - toLoad   = needed \ loaded  → catalogLoadAsset each
 *
 * When newStageId is NULL (transitioning to a base-game-only stage),
 * the "needed" set is empty, so all loaded non-bundled assets go to
 * toUnload.
 *
 * toLoad ordering: ASSET_MAP entries first, then ASSET_CHARACTER, then
 * everything else — matching the stage load sequence in lv.c / romdata.c.
 * ======================================================================== */

/* Maximum concurrent entries we track in the needed / loaded sets.
 * Mod catalogs are small (<256 entries), so 256 is more than sufficient. */
#define DIFF_MAX_TRACKED 256

s32 catalogComputeStageDiff(const char *newStageId,
                            const char **toLoad,  s32 *loadCount,
                            const char **toUnload, s32 *unloadCount,
                            s32 maxItems)
{
    if (!toLoad || !loadCount || !toUnload || !unloadCount || maxItems <= 0) {
        return -1;
    }

    *loadCount   = 0;
    *unloadCount = 0;

    /* ------------------------------------------------------------------ */
    /* Step 1: resolve the destination stage's category string             */
    /* ------------------------------------------------------------------ */

    char needCategory[CATALOG_CATEGORY_LEN] = "";

    if (newStageId && newStageId[0]) {
        const asset_entry_t *mapEntry = assetCatalogResolve(newStageId);
        if (mapEntry && mapEntry->category[0]) {
            strncpy(needCategory, mapEntry->category, CATALOG_CATEGORY_LEN - 1);
            needCategory[CATALOG_CATEGORY_LEN - 1] = '\0';
        }
    }

    /* ------------------------------------------------------------------ */
    /* Step 2: collect "currently loaded non-bundled" asset IDs            */
    /* ------------------------------------------------------------------ */

    /* Small scratch arrays on the stack — IDs are 64-byte strings, so     */
    /* 256 × 64 = 16 KB total; within reason for a stage-transition call.  */
    static const char *s_LoadedIds[DIFF_MAX_TRACKED];
    s32 loadedCount = 0;

    s32 total = assetCatalogGetCount();
    for (s32 i = 0; i < total && loadedCount < DIFF_MAX_TRACKED; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || !e->occupied || e->bundled) {
            continue;
        }
        if (e->load_state >= ASSET_STATE_LOADED) {
            s_LoadedIds[loadedCount++] = e->id;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Step 3: collect "needed for new stage" asset IDs (by category)      */
    /* ------------------------------------------------------------------ */

    static const char *s_NeededIds[DIFF_MAX_TRACKED];
    s32 neededCount = 0;

    /* Split into two passes so ASSET_MAP entries sort first in the output. */
    for (s32 pass = 0; pass < 2 && neededCount < DIFF_MAX_TRACKED; pass++) {
        for (s32 i = 0; i < total && neededCount < DIFF_MAX_TRACKED; i++) {
            const asset_entry_t *e = assetCatalogGetByIndex(i);
            if (!e || !e->occupied || !e->enabled || e->bundled) {
                continue;
            }
            if (!needCategory[0] ||
                    strncmp(e->category, needCategory, CATALOG_CATEGORY_LEN) != 0) {
                continue;
            }
            /* Pass 0: MAP+CHARACTER first (match load order)              */
            /* Pass 1: everything else                                     */
            s32 isPriority = (e->type == ASSET_MAP || e->type == ASSET_CHARACTER);
            if ((pass == 0) != (isPriority != 0)) {
                continue;
            }
            s_NeededIds[neededCount++] = e->id;
        }
    }

    /* ------------------------------------------------------------------ */
    /* Step 4: compute toUnload = loaded \ needed                          */
    /* ------------------------------------------------------------------ */

    for (s32 i = 0; i < loadedCount && *unloadCount < maxItems; i++) {
        s32 inNeeded = 0;
        for (s32 j = 0; j < neededCount; j++) {
            if (strncmp(s_LoadedIds[i], s_NeededIds[j], CATALOG_ID_LEN) == 0) {
                inNeeded = 1;
                break;
            }
        }
        if (!inNeeded) {
            toUnload[(*unloadCount)++] = s_LoadedIds[i];
        }
    }

    /* ------------------------------------------------------------------ */
    /* Step 5: compute toLoad = needed \ loaded                            */
    /* ------------------------------------------------------------------ */

    for (s32 i = 0; i < neededCount && *loadCount < maxItems; i++) {
        s32 inLoaded = 0;
        for (s32 j = 0; j < loadedCount; j++) {
            if (strncmp(s_NeededIds[i], s_LoadedIds[j], CATALOG_ID_LEN) == 0) {
                inLoaded = 1;
                break;
            }
        }
        if (!inLoaded) {
            toLoad[(*loadCount)++] = s_NeededIds[i];
        }
    }

    return *loadCount + *unloadCount;
}
