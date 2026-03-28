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
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "system.h"

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
