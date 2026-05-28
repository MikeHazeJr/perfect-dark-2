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
#include "assetcatalog_deps.h"
#include "asset_source_debug.h"
#include "asset_runtime.h"
#include "assetprovider.h"
#include "assetload.h"
#include "modasset_compiler.h"
#include "weapon_graph_runtime.h"
#include "data.h"
#include "game/modeldef.h"
#include "lib/meshcollision.h"
#include "langmanifest.h"
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

static s32 s_FileQueryCount = 0;
static s32 s_TexQueryCount  = 0;
static s32 s_AnimQueryCount = 0;
static s32 s_SndQueryCount  = 0;

typedef struct catalog_animation_clip_payload {
    struct animtableentry entry;
    u32 data_size;
    u8 *data;
} catalog_animation_clip_payload_t;

extern struct animtableentry *g_RomAnims;
extern u8 **g_AnimReplacements;

static s32 s_catalogTypeUsesMetadataRuntimePayload(asset_type_e type);
static s32 s_catalogLoadEntryMetadataPayload(asset_entry_t *entry);

/* ========================================================================
 * Initialization
 * ======================================================================== */

void catalogLoadInit(void)
{
    /* Reset all reverse-index arrays to "no entry" */
    for (s32 i = 0; i < LOAD_MAX_FILES;    i++) { s_FilenumOverride[i]   = -1; }
    for (s32 i = 0; i < LOAD_MAX_TEXTURES; i++) { s_TexnumOverride[i]    = -1; }
    for (s32 i = 0; i < LOAD_MAX_ANIMS;    i++) { s_AnimnumOverride[i]   = -1; }
    for (s32 i = 0; i < LOAD_MAX_SOUNDS;   i++) { s_SoundnumOverride[i]  = -1; }

    s32 bundled_count  = 0;
    s32 override_count = 0;
    s32 total = assetCatalogGetCount();

    for (s32 i = 0; i < total; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || !e->occupied) {
            continue;
        }
        /* Index bundled (base-game) entries regardless of enabled state.
         * Skip disabled non-bundled (mod) entries — a disabled mod should
         * fall through to ROM, not appear in the reverse-index at all.
         * Because base-game entries are registered first (lower pool indices)
         * and mod entries second (higher pool indices), a mod entry that maps
         * to the same source_filenum will overwrite the bundled entry and win. */
        if (!e->bundled && !e->enabled) {
            continue;
        }

        /* C-4: file reverse-index */
        if (e->source_filenum >= 0 && e->source_filenum < LOAD_MAX_FILES) {
            s_FilenumOverride[e->source_filenum] = i;
            if (e->bundled) { bundled_count++; } else { override_count++; }
        }

        /* C-5: texture reverse-index */
        if (e->source_texnum >= 0 && e->source_texnum < LOAD_MAX_TEXTURES) {
            s_TexnumOverride[e->source_texnum] = i;
            if (e->bundled) { bundled_count++; } else { override_count++; }
        }

        /* C-6: animation reverse-index */
        if (e->source_animnum >= 0 && e->source_animnum < LOAD_MAX_ANIMS) {
            s_AnimnumOverride[e->source_animnum] = i;
            if (e->bundled) { bundled_count++; } else { override_count++; }
        }

        /* C-7: sound reverse-index */
        if (e->source_soundnum >= 0 && e->source_soundnum < LOAD_MAX_SOUNDS) {
            s_SoundnumOverride[e->source_soundnum] = i;
            if (e->bundled) { bundled_count++; } else { override_count++; }
        }

        if (e->bundled && e->source.primary.provider == fileProvider()
                && s_catalogTypeUsesMetadataRuntimePayload(e->type)) {
            asset_entry_t *mutable_entry = assetCatalogGetMutable(e->id);
            if (mutable_entry && mutable_entry->load_state < ASSET_STATE_ACTIVE) {
                (void)s_catalogLoadEntryMetadataPayload(mutable_entry);
            }
        }
    }

    s_Initialized = 1;
    sysLogPrintf(LOG_NOTE, "catalogLoadInit: %d base-game + %d mod override(s) indexed from %d catalog entries",
                 bundled_count, override_count, total);
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

    /* Direct File Access (Phase 2): the declarative source-of-truth for
     * "which file backs this asset?" is `entry->source.primary` when it
     * holds a FileProvider handle. Consult that first so the override
     * path tracks the provider abstraction rather than the type-specific
     * ext.* fields. The ext.* fallback below stays in place for entries
     * that haven't been populated with an asset_source_t yet. */
    if (e->source.primary.provider == fileProvider()) {
        const char *p = fileProviderPath(e->source.primary);
        if (p && p[0]) {
            return p;
        }
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

static void s_catalogApplySourceOnlyDebug(CatalogResolveResult *r,
                                          const asset_entry_t *e)
{
    if (r && assetSourceDebugEntryRequiresPublicFileSource(e)) {
        r->source_only_blocked = 1;
    }
}

static void s_catalogFatalSourceOnlyFallback(const CatalogResolveResult *r,
                                             const char *kind,
                                             s32 numeric_id)
{
    const asset_entry_t *entry;
    const char *asset_id;
    const char *type_label;

    if (!r || !r->source_only_blocked) {
        return;
    }

    entry = assetCatalogGetByIndex(r->catalog_id);
    asset_id = entry ? entry->id : "?";
    type_label = entry ? assetSourceDebugTypeLabel(entry->type) : "?";
    sysFatalError("ASSET.SOURCE_ONLY: %s %d maps to %s '%s' but has no "
                  "public FileProvider source; refusing ROM/static fallback.",
                  kind ? kind : "asset", numeric_id, type_label, asset_id);
}

/* ========================================================================
 * Resolve API  (primary interface)
 *
 * Each function looks up the numeric ID in the appropriate reverse-index
 * array and classifies the result:
 *
 *   catalog_id >= 0, is_mod_override=1  →  non-bundled (mod) entry; load
 *                                          from path
 *   catalog_id >= 0, is_mod_override=0  →  bundled (base-game) entry; load
 *                                          from ROM (path is NULL)
 *   catalog_id < 0                      →  no catalog entry; load from ROM
 *
 * The query counters (s_File/Tex/Anim/SndQueryCount) are incremented here
 * so they reflect total resolve() calls, including those coming through the
 * backward-compatible catalogGet*Override() wrappers below.
 * ======================================================================== */

CatalogResolveResult catalogResolveFile(s32 filenum)
{
    CatalogResolveResult r = { NULL, -1, 0, 0 };

    if (!s_Initialized || filenum < 0 || filenum >= LOAD_MAX_FILES) {
        return r;
    }

    if (s_FileQueryCount++ == 0) {
        sysLogPrintf(LOG_NOTE, "CATALOG: C-4 intercept live (first filenum=%d query)", filenum);
    }

    s32 idx = s_FilenumOverride[filenum];
    if (idx < 0) {
        return r;  /* catalog_id = -1, not cataloged */
    }

    const asset_entry_t *e = assetCatalogGetByIndex(idx);
    if (!e) {
        return r;
    }

    r.catalog_id = idx;
    /* Phase 3 Pass B (2026-05-02): disk-load routing now considers
     * the source.primary provider, not just the bundled flag.  An
     * entry whose primary handle is FileProvider routes to disk
     * regardless of bundled state -- this is how base-game content
     * migrates from RomProvider to FileProvider per slice without
     * requiring a separate "bundled-on-disk" flag.  is_mod_override
     * keeps its name for backward compat with consumers that just
     * check the boolean to decide "do I have a disk path"; the more
     * accurate name would be is_disk_load.  Renaming is a follow-up. */
    if (!e->bundled || e->source.primary.provider == fileProvider()) {
        r.path           = entryGetFilePath(e);
        if (r.path) {
            r.is_mod_override = 1;
        }
    }
    s_catalogApplySourceOnlyDebug(&r, e);
    /* Otherwise: bundled with RomProvider primary (or null primary);
     * caller falls through to the legacy ROM read path. */
    return r;
}

CatalogResolveResult catalogResolveTexture(s32 texnum)
{
    CatalogResolveResult r = { NULL, -1, 0, 0 };

    if (!s_Initialized || texnum < 0 || texnum >= LOAD_MAX_TEXTURES) {
        return r;
    }

    s_TexQueryCount++;

    s32 idx = s_TexnumOverride[texnum];
    if (idx < 0) {
        return r;
    }

    const asset_entry_t *e = assetCatalogGetByIndex(idx);
    if (!e) {
        return r;
    }

    r.catalog_id = idx;
    /* Phase 3 Pass B: same disk-load broadening as catalogResolveFile. */
    if (!e->bundled || e->source.primary.provider == fileProvider()) {
        r.path           = entryGetFilePath(e);
        if (r.path) {
            r.is_mod_override = 1;
        }
    }
    s_catalogApplySourceOnlyDebug(&r, e);
    return r;
}

CatalogResolveResult catalogResolveAnim(s32 animnum)
{
    CatalogResolveResult r = { NULL, -1, 0, 0 };

    if (!s_Initialized || animnum < 0 || animnum >= LOAD_MAX_ANIMS) {
        return r;
    }

    s_AnimQueryCount++;

    s32 idx = s_AnimnumOverride[animnum];
    if (idx < 0) {
        return r;
    }

    const asset_entry_t *e = assetCatalogGetByIndex(idx);
    if (!e) {
        return r;
    }

    r.catalog_id = idx;
    /* Phase 3 Pass B: same disk-load broadening as catalogResolveFile. */
    if (!e->bundled || e->source.primary.provider == fileProvider()) {
        r.path           = entryGetFilePath(e);
        if (r.path) {
            r.is_mod_override = 1;
        }
    }
    s_catalogApplySourceOnlyDebug(&r, e);
    return r;
}

CatalogResolveResult catalogResolveSound(s32 soundnum)
{
    CatalogResolveResult r = { NULL, -1, 0, 0 };

    if (!s_Initialized || soundnum < 0 || soundnum >= LOAD_MAX_SOUNDS) {
        return r;
    }

    s_SndQueryCount++;

    s32 idx = s_SoundnumOverride[soundnum];
    if (idx < 0) {
        return r;
    }

    const asset_entry_t *e = assetCatalogGetByIndex(idx);
    if (!e) {
        return r;
    }

    r.catalog_id = idx;
    /* Phase 3 Pass B: same disk-load broadening as catalogResolveFile. */
    if (!e->bundled || e->source.primary.provider == fileProvider()) {
        r.path           = entryGetFilePath(e);
        if (r.path) {
            r.is_mod_override = 1;
        }
    }
    s_catalogApplySourceOnlyDebug(&r, e);
    return r;
}

/* ========================================================================
 * Legacy Override Queries  (backward-compatible wrappers)
 *
 * These call catalogResolve*() and return just the path (NULL = ROM).
 * The query counters are incremented inside the resolve functions, so
 * callers of these wrappers are counted correctly.
 * ======================================================================== */

const char *catalogGetFileOverride(s32 filenum)
{
    CatalogResolveResult r = catalogResolveFile(filenum);
    s_catalogFatalSourceOnlyFallback(&r, "file", filenum);
    return (r.is_mod_override && r.path) ? r.path : NULL;
}

const char *catalogGetTextureOverride(s32 texnum)
{
    CatalogResolveResult r = catalogResolveTexture(texnum);
    s_catalogFatalSourceOnlyFallback(&r, "texture", texnum);
    return (r.is_mod_override && r.path) ? r.path : NULL;
}

const char *catalogGetAnimOverride(s32 animnum)
{
    CatalogResolveResult r = catalogResolveAnim(animnum);
    s_catalogFatalSourceOnlyFallback(&r, "animation", animnum);
    return (r.is_mod_override && r.path) ? r.path : NULL;
}

const char *catalogGetSoundOverride(s32 soundnum)
{
    CatalogResolveResult r = catalogResolveSound(soundnum);
    s_catalogFatalSourceOnlyFallback(&r, "sound", soundnum);
    return (r.is_mod_override && r.path) ? r.path : NULL;
}

void catalogLoadLogStats(void)
{
    sysLogPrintf(LOG_NOTE,
        "CATALOG: intercept stats: file=%d tex=%d anim=%d snd=%d queries",
        s_FileQueryCount, s_TexQueryCount, s_AnimQueryCount, s_SndQueryCount);
}

/* ========================================================================
 * MEM-2: Asset Lifecycle API
 *
 * catalogLoadTypedAsset    — activate entry using its typed lifecycle policy
 * catalogReleaseTypedAsset — dec ref_count; free when it hits 0 (non-bundled only)
 * catalogRetainTypedAsset  — inc ref_count on an already-loaded entry
 *
 * Bundled (base-game) entries carry ref_count = ASSET_REF_BUNDLED and are
 * never loaded or evicted through these functions — their data lives in the
 * ROM segment for the lifetime of the process.
 *
 * Logging prefixes:
 *   "CATALOG:" — base-game bundled retain (informational)
 *   "CATALOG.LIFECYCLE." — typed lifecycle activation/release diagnostics
 * ======================================================================== */

static const char *s_catalogPayloadKind(asset_type_e type)
{
    switch (type) {
    case ASSET_MODEL:     return "model";
    case ASSET_BODY:      return "body";
    case ASSET_HEAD:      return "head";
    case ASSET_WEAPON:    return "weapon";
    case ASSET_MAP:       return "map";
    case ASSET_ARENA:     return "arena";
    case ASSET_CHARACTER: return "character";
    case ASSET_PROP:      return "prop";
    case ASSET_ANIMATION: return "animation";
    case ASSET_TEXTURE:   return "texture";
    case ASSET_TEXTURES:  return "texture-pack";
    case ASSET_AUDIO:     return "audio";
    case ASSET_SFX:       return "sfx";
    case ASSET_MUSIC:     return "music";
    case ASSET_LANG:      return "language";
    case ASSET_HUD:       return "hud";
    case ASSET_EFFECT:    return "effect";
    case ASSET_SKIN:      return "skin";
    case ASSET_GAMEMODE:  return "gamemode";
    case ASSET_BOT_PROFILE: return "bot-profile";
    case ASSET_PROJECTILE: return "projectile";
    case ASSET_ENTITY:    return "entity";
    case ASSET_MATERIAL:  return "material";
    case ASSET_FONT:      return "font";
    case ASSET_SCENARIO:  return "scenario";
    case ASSET_THEME:     return "theme";
    default:              return "generic";
    }
}

static s32 s_catalogTypeUsesModelPayload(asset_type_e type)
{
    switch (type) {
    case ASSET_MODEL:
    case ASSET_BODY:
    case ASSET_HEAD:
    case ASSET_PROP:
        return 1;
    default:
        return 0;
    }
}

static s32 s_catalogLoadEntryModelPayload(asset_entry_t *entry, asset_data_handle_t handle)
{
    char desc[128];
    modasset_compiled_result_t compiled;
    const char *source_path = NULL;
    struct modeldef *modeldef;
    s32 loaded_size;

    if (handle.provider == fileProvider()) {
        source_path = fileProviderPath(handle);
    }

    if (modAssetCompilerIsExternalSource(source_path)) {
        s32 cache_result = modAssetCompilerCompileReadable(entry,
            s_catalogPayloadKind(entry->type), source_path, &compiled);
        if (cache_result < 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' external %s source validation failed (%s)",
                         entry->id, s_catalogPayloadKind(entry->type),
                         assetDescribe(handle, desc, sizeof(desc)));
            return 0;
        }

        if (modAssetCompilerBuildModeldef(entry, source_path, &modeldef) <= 0
                || !modeldef) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' external %s modeldef conversion failed (%s)",
                         entry->id, s_catalogPayloadKind(entry->type),
                         assetDescribe(handle, desc, sizeof(desc)));
            return 0;
        }

        entry->loaded_data      = modeldef;
        entry->data_size_bytes  = (u32)(sizeof(*modeldef)
            + compiled.vertex_count * (s32)sizeof(Vtx)
            + compiled.triangle_count * 2 * (s32)sizeof(Gfx));
        entry->payload_kind     = ASSET_PAYLOAD_STAGE_MODELDEF;
        entry->load_state       = ASSET_STATE_ACTIVE;
        entry->ref_count        = 1;

        sysLogPrintf(LOG_NOTE,
                     "CATALOG.LIFECYCLE.ACTIVATE: activated external %s modeldef '%s' vertices=%d tris=%d cache=%s normalized=%s",
                     s_catalogPayloadKind(entry->type), entry->id,
                     compiled.vertex_count, compiled.triangle_count,
                     compiled.descriptor_path[0] ? compiled.descriptor_path : "(no descriptor)",
                     compiled.normalized_path[0] ? compiled.normalized_path : "(no normalized model)");
        return 1;
    }

    modeldef = modeldefLoadToNewFromHandle(handle, entry->source_filenum);
    if (!modeldef) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' model payload activation failed (%s)",
                     entry->id,
                     assetDescribe(handle, desc, sizeof(desc)));
        return 0;
    }

    loaded_size = assetLoadGetLoadedSize(handle);
    if (loaded_size <= 0) {
        loaded_size = assetLoadGetInflatedSize(handle, LOADTYPE_MODEL);
    }

    entry->loaded_data      = modeldef;
    entry->data_size_bytes  = loaded_size > 0 ? (u32)loaded_size : 0;
    entry->payload_kind     = ASSET_PAYLOAD_STAGE_MODELDEF;
    entry->load_state       = ASSET_STATE_ACTIVE;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.ACTIVATE: activated %s model payload '%s' (%u bytes) from %s",
                 s_catalogPayloadKind(entry->type), entry->id, entry->data_size_bytes,
                 assetDescribe(handle, desc, sizeof(desc)));
    return 1;
}

static s32 s_catalogLoadEntryLangPayload(asset_entry_t *entry)
{
    if (!langManifestEnsureId(entry->id)) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' language payload activation failed",
                     entry->id);
        return 0;
    }

    entry->loaded_data      = entry;
    entry->data_size_bytes  = 0;
    entry->payload_kind     = ASSET_PAYLOAD_RUNTIME_ACTIVE;
    entry->load_state       = ASSET_STATE_ACTIVE;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.ACTIVATE: activated language payload '%s' bank=%d",
                 entry->id, entry->ext.lang.bank_id);
    return 1;
}

static s32 s_catalogTypeUsesAudioRuntimePayload(asset_type_e type)
{
    return type == ASSET_AUDIO;
}

static s32 s_catalogLoadEntryAudioPayload(asset_entry_t *entry, asset_data_handle_t handle)
{
    char desc[128];

    if (entry->type == ASSET_AUDIO && assetHandleIsNull(handle)) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' audio payload has no provider handle",
                     entry->id);
        return 0;
    }

    entry->loaded_data      = entry;
    entry->data_size_bytes  = 0;
    entry->payload_kind     = ASSET_PAYLOAD_RUNTIME_ACTIVE;
    entry->load_state       = ASSET_STATE_ACTIVE;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.ACTIVATE: activated %s runtime payload '%s' (%s)",
                 s_catalogPayloadKind(entry->type), entry->id,
                 assetDescribe(handle, desc, sizeof(desc)));
    return 1;
}

static s32 s_catalogTypeUsesMetadataRuntimePayload(asset_type_e type)
{
    return type == ASSET_MAP
        || type == ASSET_WEAPON
        || type == ASSET_CHARACTER
        || type == ASSET_ANIMATION
        || type == ASSET_TEXTURES
        || type == ASSET_SFX
        || type == ASSET_MUSIC
        || type == ASSET_UI
        || type == ASSET_TOOL
        || type == ASSET_VEHICLE
        || type == ASSET_MISSION
        || type == ASSET_HUD
        || type == ASSET_BOT_PROFILE
        || type == ASSET_ARENA
        || type == ASSET_GAMEMODE
        || type == ASSET_SKIN
        || type == ASSET_BOT_VARIANT
        || type == ASSET_PROJECTILE
        || type == ASSET_ENTITY
        || type == ASSET_EFFECT
        || type == ASSET_MATERIAL
        || type == ASSET_FONT
        || type == ASSET_SCENARIO
        || type == ASSET_THEME;
}

static s32 s_catalogTypeCanUseObjColmeshPayload(asset_type_e type)
{
    return type == ASSET_ARENA
        || type == ASSET_GAMEMODE
        || type == ASSET_SCENARIO
        || type == ASSET_MAP;
}

static s32 s_catalogTypeUsesWeaponGraphRuntime(asset_type_e type)
{
    return type == ASSET_PROJECTILE || type == ASSET_ENTITY;
}

static s32 s_catalogActivateWeaponGraphRuntime(asset_entry_t *entry,
                                               const char *source_path)
{
    u32 graph_size = 0;
    char err[256];
    char *graph = NULL;
    s32 result;

    if (!entry || !source_path || !source_path[0]) {
        return 0;
    }

    graph = (char *)fsFileLoad(source_path, &graph_size);
    if (!graph || graph_size == 0) {
        if (graph) {
            free(graph);
        }
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' graph runtime missing %s",
                     entry->id, source_path);
        return 0;
    }

    err[0] = '\0';
    result = weaponGraphRuntimeRegisterBehaviorGraphJson(entry->type,
        entry->id, graph, graph_size, err, sizeof(err));
    free(graph);

    if (result != 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' graph runtime compile failed: %s",
                     entry->id, err[0] ? err : "unknown error");
        return 0;
    }

    return 1;
}

static void s_catalogInstallAnimationClip(asset_entry_t *entry,
                                          const struct animtableentry *anim)
{
    s32 anim_id;

    if (!entry || !anim) {
        return;
    }

    anim_id = entry->ext.anim.anim_id;
    if (anim_id < 0 || anim_id >= g_NumAnimations || !g_Anims) {
        return;
    }

    g_Anims[anim_id] = *anim;
    g_Anims[anim_id].data = 0xffffffff;

    if (g_AnimReplacements) {
        g_AnimReplacements[anim_id] = NULL;
    }
}

static s32 s_catalogLoadEntryAnimationPayload(asset_entry_t *entry)
{
    const char *source_path = entryGetFilePath(entry);
    modasset_compiled_result_t compiled;
    catalog_animation_clip_payload_t *payload;
    struct animtableentry anim;
    u8 *clip_data = NULL;
    u32 clip_size = 0;
    s32 compile_result;
    s32 clip_result;

    if (!modAssetCompilerIsExternalSource(source_path)) {
        return 0;
    }

    compile_result = modAssetCompilerCompileReadable(entry, "animation",
        source_path, &compiled);
    if (compile_result < 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' external animation cache failed",
                     entry->id);
        return -1;
    }

    clip_result = modAssetCompilerBuildAnimationClip(entry, source_path,
        &anim, &clip_data, &clip_size);
    if (clip_result <= 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' external animation clip build failed",
                     entry->id);
        return -1;
    }

    payload = malloc(sizeof(*payload));
    if (!payload) {
        modAssetCompilerFreeAnimationClip(clip_data);
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' animation payload allocation failed",
                     entry->id);
        return -1;
    }

    payload->entry = anim;
    payload->data_size = clip_size;
    payload->data = clip_data;

    s_catalogInstallAnimationClip(entry, &payload->entry);

    entry->loaded_data      = payload;
    entry->data_size_bytes  = (u32)(sizeof(*payload) + clip_size);
    entry->payload_kind     = ASSET_PAYLOAD_ANIMATION_CLIP;
    entry->load_state       = ASSET_STATE_ACTIVE;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.ACTIVATE: activated animation clip '%s' frames=%d cache=%s generated=%s",
                 entry->id, payload->entry.numframes,
                 compiled.descriptor_path[0] ? compiled.descriptor_path : "(no descriptor)",
                 compiled.normalized_path[0] ? compiled.normalized_path : "(no normalized animation)");
    return 1;
}

static s32 s_catalogLoadEntryMetadataPayload(asset_entry_t *entry)
{
    const char *source_path = entryGetFilePath(entry);
    modasset_compiled_result_t compiled;

    if (modAssetCompilerIsExternalSource(source_path)) {
        s32 cache_result = modAssetCompilerCompileReadable(entry,
            s_catalogPayloadKind(entry->type), source_path, &compiled);
        if (cache_result < 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' external %s source validation failed",
                         entry->id, s_catalogPayloadKind(entry->type));
            return 0;
        }

        if (s_catalogTypeCanUseObjColmeshPayload(entry->type)) {
            struct colmesh *mesh = malloc(sizeof(*mesh));
            s32 mesh_result;
            const char *colmesh_source_path = source_path;

            if (!mesh) {
                sysLogPrintf(LOG_WARNING,
                             "CATALOG.LIFECYCLE.ACTIVATE: '%s' colmesh allocation failed",
                             entry->id);
                return 0;
            }

            if (entry->type == ASSET_SCENARIO
                    && entry->ext.scenario.collision_file[0]) {
                colmesh_source_path = entry->ext.scenario.collision_file;
            }

            mesh_result = modAssetCompilerBuildColmesh(colmesh_source_path, mesh);
            if (mesh_result < 0) {
                free(mesh);
                sysLogPrintf(LOG_WARNING,
                             "CATALOG.LIFECYCLE.ACTIVATE: '%s' authored colmesh build failed",
                             entry->id);
                return 0;
            }
            if (mesh_result > 0) {
                entry->loaded_data      = mesh;
                entry->data_size_bytes  = (u32)(sizeof(*mesh)
                    + (mesh->numtris * (s32)sizeof(struct meshtri)));
                entry->payload_kind     = ASSET_PAYLOAD_COLMESH;
                entry->load_state       = ASSET_STATE_ACTIVE;
                entry->ref_count        = 1;

                sysLogPrintf(LOG_NOTE,
                             "CATALOG.LIFECYCLE.ACTIVATE: activated %s authored colmesh '%s' source=%s tris=%d cache=%s mesh=%s",
                             s_catalogPayloadKind(entry->type), entry->id,
                             colmesh_source_path ? colmesh_source_path : "(none)",
                             mesh->numtris,
                             compiled.descriptor_path[0] ? compiled.descriptor_path : "(no descriptor)",
                             compiled.normalized_path[0] ? compiled.normalized_path : "(no normalized mesh)");
                return 1;
            }

            free(mesh);
        }

        sysLogPrintf(LOG_NOTE,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' external %s source ready via private cache %s",
                     entry->id, s_catalogPayloadKind(entry->type),
                     compiled.descriptor_path[0] ? compiled.descriptor_path : "(no cache path)");
    }

    entry->loaded_data      = entry;
    entry->data_size_bytes  = 0;
    entry->payload_kind     = ASSET_PAYLOAD_RUNTIME_ACTIVE;
    entry->load_state       = ASSET_STATE_ACTIVE;
    entry->ref_count        = 1;

    if (s_catalogTypeUsesWeaponGraphRuntime(entry->type)
            && !s_catalogActivateWeaponGraphRuntime(entry, source_path)) {
        entry->loaded_data     = NULL;
        entry->payload_kind    = ASSET_PAYLOAD_NONE;
        entry->load_state      = ASSET_STATE_ENABLED;
        entry->ref_count       = 0;
        return 0;
    }

    if (assetRuntimeSupportsType(entry->type)
            && !assetRuntimeActivateCatalogEntry(entry, source_path)) {
        entry->loaded_data     = NULL;
        entry->payload_kind    = ASSET_PAYLOAD_NONE;
        entry->load_state      = ASSET_STATE_ENABLED;
        entry->ref_count       = 0;
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' %s runtime adapter rejected missing authored payload",
                     entry->id, s_catalogPayloadKind(entry->type));
        return 0;
    }

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.ACTIVATE: activated %s metadata payload '%s'",
                 s_catalogPayloadKind(entry->type), entry->id);
    return 1;
}

static s32 s_catalogLoadEntryTexturePayload(asset_entry_t *entry, asset_data_handle_t handle)
{
    char desc[128];
    s32 size;
    s32 loaded;
    void *data;

    if (assetHandleIsNull(handle)) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' texture payload has no provider handle",
                     entry->id);
        return 0;
    }

    size = assetLoadGetInflatedSize(handle, 0);
    if (size <= 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' texture provider reports zero size (%s)",
                     entry->id,
                     assetDescribe(handle, desc, sizeof(desc)));
        return 0;
    }

    data = sysMemAlloc((u32)size);
    if (!data) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' could not allocate texture payload (%d bytes)",
                     entry->id, size);
        return 0;
    }

    loaded = assetLoad(handle, data, size);
    if (loaded <= 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' texture provider load failed (%s)",
                     entry->id,
                     assetDescribe(handle, desc, sizeof(desc)));
        sysMemFree(data);
        return 0;
    }

    entry->loaded_data      = data;
    entry->data_size_bytes  = (u32)loaded;
    entry->payload_kind     = ASSET_PAYLOAD_SYSMEM_BYTES;
    entry->load_state       = ASSET_STATE_ACTIVE;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.ACTIVATE: activated texture payload '%s' (%d bytes) from %s",
                 entry->id, loaded,
                 assetDescribe(handle, desc, sizeof(desc)));
    return 1;
}

static s32 s_catalogLoadEntryFromProvider(asset_entry_t *entry, asset_type_e expected_type)
{
    asset_data_handle_t handle = catalogEffectiveHandle(entry);
    char desc[128];
    s32 size;
    void *data;
    s32 loaded;

    if (assetHandleIsNull(handle)) {
        return 0;
    }

    size = assetLoadGetInflatedSize(handle, expected_type == ASSET_MODEL ? LOADTYPE_MODEL : 0);
    if (size <= 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.LOAD: '%s' %s provider reports zero size (%s)",
                     entry->id, s_catalogPayloadKind(entry->type),
                     assetDescribe(handle, desc, sizeof(desc)));
        return 0;
    }

    data = sysMemAlloc((u32)size);
    if (!data) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.LOAD: '%s' could not allocate %d bytes",
                     entry->id, size);
        return 0;
    }

    loaded = assetLoad(handle, data, size);
    if (loaded <= 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.LOAD: '%s' provider load failed (%s)",
                     entry->id,
                     assetDescribe(handle, desc, sizeof(desc)));
        sysMemFree(data);
        return 0;
    }

    entry->loaded_data      = data;
    entry->data_size_bytes  = (u32)loaded;
    entry->payload_kind     = ASSET_PAYLOAD_SYSMEM_BYTES;
    entry->load_state       = ASSET_STATE_LOADED;
    entry->ref_count        = 1;

    sysLogPrintf(LOG_NOTE,
                 "CATALOG.LIFECYCLE.LOAD: loaded %s '%s' (%d bytes) from %s",
                 s_catalogPayloadKind(entry->type), entry->id, loaded,
                 assetDescribe(handle, desc, sizeof(desc)));
    return 1;
}

static s32 s_catalogLoadEntry(asset_entry_t *entry, asset_type_e expected_type)
{
    asset_data_handle_t handle = catalogEffectiveHandle(entry);

    if (assetSourceDebugEntryRequiresPublicFileSource(entry)) {
        sysLogPrintf(LOG_WARNING,
                     "ASSET.SOURCE_ONLY: typed '%s' %s has no public FileProvider source; refusing fallback",
                     entry->id, assetSourceDebugTypeLabel(entry->type));
        return 0;
    }

    if ((entry->bundled || entry->ref_count == ASSET_REF_BUNDLED)
            && entry->source.primary.provider == fileProvider()
            && s_catalogTypeUsesMetadataRuntimePayload(entry->type)) {
        if (entry->load_state >= ASSET_STATE_ACTIVE && entry->loaded_data) {
            return 1;
        }
        return s_catalogLoadEntryMetadataPayload(entry);
    }

    if (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED) {
        sysLogPrintf(LOG_NOTE, "CATALOG: retain bundled '%s'", entry->id);
        return 1;
    }

    if (entry->load_state >= ASSET_STATE_LOADED && entry->loaded_data) {
        entry->ref_count++;
        return 1;
    }

    if (!entry->enabled) {
        sysLogPrintf(LOG_WARNING, "CATALOG.LIFECYCLE.LOAD: '%s' is not enabled", entry->id);
        return 0;
    }

    if (s_catalogTypeUsesModelPayload(entry->type)) {
        if (assetHandleIsNull(handle)) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' model payload has no provider handle",
                         entry->id);
            return 0;
        }
        return s_catalogLoadEntryModelPayload(entry, handle);
    }

    if (entry->type == ASSET_LANG) {
        return s_catalogLoadEntryLangPayload(entry);
    }

    if (s_catalogTypeUsesAudioRuntimePayload(entry->type)) {
        return s_catalogLoadEntryAudioPayload(entry, handle);
    }

    if (entry->type == ASSET_ANIMATION) {
        s32 animation_payload = s_catalogLoadEntryAnimationPayload(entry);
        if (animation_payload != 0) {
            return animation_payload > 0;
        }
    }

    if (s_catalogTypeUsesMetadataRuntimePayload(entry->type)) {
        return s_catalogLoadEntryMetadataPayload(entry);
    }

    if (entry->type == ASSET_TEXTURE) {
        return s_catalogLoadEntryTexturePayload(entry, handle);
    }

    if (expected_type != ASSET_NONE) {
        if (assetHandleIsNull(handle)) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.LOAD: typed '%s' %s payload has no provider handle",
                         entry->id, s_catalogPayloadKind(entry->type));
            return 0;
        }
        return s_catalogLoadEntryFromProvider(entry, expected_type);
    }

    sysLogPrintf(LOG_WARNING,
                 "CATALOG.LIFECYCLE.LOAD: '%s' has no typed lifecycle policy",
                 entry->id);
    return 0;
}

static s32 s_catalogValidateTypedLifecycle(const char *op, asset_type_e expected_type, const char *assetId)
{
    const asset_entry_t *entry;

    if (!assetId) {
        return 0;
    }

    if (expected_type == ASSET_NONE) {
        return 1;
    }

    entry = assetCatalogResolve(assetId);
    if (!entry) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.%s: '%s' not found for expected type %d",
                     op, assetId, expected_type);
        return 0;
    }

    if (entry->type != expected_type) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.%s: '%s' type mismatch expected=%d actual=%d",
                     op, assetId, expected_type, entry->type);
        return 0;
    }

    return 1;
}

s32 catalogLoadTypedAsset(asset_type_e expected_type, const char *assetId)
{
    asset_entry_t *entry;

    if (!s_catalogValidateTypedLifecycle("LOAD", expected_type, assetId)) {
        return 0;
    }

    entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return 0;
    }

    return s_catalogLoadEntry(entry, expected_type);
}

struct modeldef *catalogGetLoadedModeldef(const char *assetId)
{
    const asset_entry_t *entry;

    if (!assetId) {
        return NULL;
    }

    entry = assetCatalogResolve(assetId);
    if (!entry
            || entry->load_state < ASSET_STATE_LOADED
            || entry->payload_kind != ASSET_PAYLOAD_STAGE_MODELDEF) {
        return NULL;
    }

    return (struct modeldef *)entry->loaded_data;
}

struct colmesh *catalogGetLoadedColmesh(const char *assetId)
{
    const asset_entry_t *entry;

    if (!assetId) {
        return NULL;
    }

    entry = assetCatalogResolve(assetId);
    if (!entry
            || entry->load_state < ASSET_STATE_LOADED
            || entry->payload_kind != ASSET_PAYLOAD_COLMESH) {
        return NULL;
    }

    return (struct colmesh *)entry->loaded_data;
}

const void *catalogGetLoadedAnimationClip(const char *assetId,
                                          const struct animtableentry **out_entry,
                                          u32 *out_size)
{
    const asset_entry_t *entry;
    const catalog_animation_clip_payload_t *payload;

    if (out_entry) {
        *out_entry = NULL;
    }
    if (out_size) {
        *out_size = 0;
    }

    if (!assetId) {
        return NULL;
    }

    entry = assetCatalogResolve(assetId);
    if (!entry
            || entry->load_state < ASSET_STATE_LOADED
            || entry->payload_kind != ASSET_PAYLOAD_ANIMATION_CLIP
            || !entry->loaded_data) {
        return NULL;
    }

    payload = (const catalog_animation_clip_payload_t *)entry->loaded_data;
    if (out_entry) {
        *out_entry = &payload->entry;
    }
    if (out_size) {
        *out_size = payload->data_size;
    }
    return payload->data;
}

static void s_catalogUnloadEntry(const char *assetId, asset_entry_t *entry);

/**
 * Dep cascade callback for typed catalog release.
 *
 * When a parent asset's ref_count hits zero and its data is freed, this
 * callback is invoked for each dependency registered under that parent.
 * Each dep's own ref_count is decremented; if it also reaches zero, the dep
 * is freed recursively.
 *
 * This handles callers that release the parent directly (outside the manifest).
 * When the manifest also lists the dep in to_unload, the direct manifest call
 * arrives after the cascade and finds loaded_data == NULL (dep already freed),
 * making it a safe no-op — no double-free can occur.
 */
static void s_catalogUnloadDepCallback(const char *dep_id, void *userdata)
{
    asset_entry_t *entry;

    (void)userdata;
    entry = assetCatalogGetMutable(dep_id);
    if (entry) {
        s_catalogUnloadEntry(dep_id, entry);
    }
}

static void s_catalogUnloadEntry(const char *assetId, asset_entry_t *entry)
{
    s32 old_ref;
    s32 new_ref;

    /* Never evict bundled assets — they are ROM-resident for the process lifetime. */
    if (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED) {
        return;
    }

    old_ref = entry->ref_count;

    if (entry->ref_count > 0) {
        entry->ref_count--;
    }

    new_ref = entry->ref_count;

    if (new_ref <= 0 && entry->loaded_data) {
        /* ref_count reached zero — cascade to registered deps before freeing.
         * For each dep: decrement its ref_count; free if that also hits zero.
         * Bundled dep pairs are skipped by catalogDepForEach (they are always
         * ROM-resident and never registered with is_bundled=0). */
        catalogDepForEach(assetId, s_catalogUnloadDepCallback, NULL);

        sysLogPrintf(LOG_NOTE,
                     "MANIFEST: unload '%s' ref=%d->%d (freed)",
                     assetId, old_ref, new_ref);
        if (entry->payload_kind == ASSET_PAYLOAD_SYSMEM_BYTES) {
            sysMemFree(entry->loaded_data);
        } else if (entry->payload_kind == ASSET_PAYLOAD_STAGE_MODELDEF) {
            asset_data_handle_t handle = catalogEffectiveHandle(entry);
            const char *source_path = NULL;

            if (handle.provider == fileProvider()) {
                source_path = fileProviderPath(handle);
            }

            if (modAssetCompilerIsExternalSource(source_path)) {
                modAssetCompilerFreeModeldef((struct modeldef *)entry->loaded_data);
            } else {
                assetUnload(handle);
            }
        } else if (entry->payload_kind == ASSET_PAYLOAD_COLMESH) {
            meshFree((struct colmesh *)entry->loaded_data);
            free(entry->loaded_data);
        } else if (entry->payload_kind == ASSET_PAYLOAD_ANIMATION_CLIP) {
            catalog_animation_clip_payload_t *payload =
                (catalog_animation_clip_payload_t *)entry->loaded_data;
            s32 anim_id = entry->ext.anim.anim_id;

            if (anim_id >= 0 && anim_id < g_NumAnimations) {
                if (g_RomAnims && g_Anims) {
                    g_Anims[anim_id] = g_RomAnims[anim_id];
                }
                if (g_AnimReplacements) {
                    g_AnimReplacements[anim_id] = NULL;
                }
            }
            if (payload) {
                modAssetCompilerFreeAnimationClip(payload->data);
                free(payload);
            }
        } else if (entry->payload_kind == ASSET_PAYLOAD_RUNTIME_ACTIVE) {
            if (s_catalogTypeUsesWeaponGraphRuntime(entry->type)) {
                weaponGraphRuntimeClearAsset(assetId);
            }
            assetRuntimeReleaseCatalogEntry(assetId);
            /* Runtime-owned activation (for example language banks) is
             * detached from this catalog reference. The owning subsystem
             * releases its memory on its normal reset/reload path. */
        }
        entry->loaded_data     = NULL;
        entry->data_size_bytes = 0;
        entry->payload_kind    = ASSET_PAYLOAD_NONE;
        entry->load_state      = ASSET_STATE_ENABLED;
        entry->ref_count       = 0;
    } else if (old_ref != new_ref) {
        /* ref_count decremented but still > 0: asset retained by other holders. */
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST: unload '%s' ref=%d->%d (retained)",
                     assetId, old_ref, new_ref);
    }
    /* old_ref == new_ref == 0: already fully unloaded — silent no-op. */
}

void catalogReleaseTypedAsset(asset_type_e expected_type, const char *assetId)
{
    asset_entry_t *entry;

    if (!s_catalogValidateTypedLifecycle("RELEASE", expected_type, assetId)) {
        return;
    }

    entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return;
    }

    s_catalogUnloadEntry(assetId, entry);
}

static void s_catalogRetainEntry(asset_entry_t *entry)
{
    if (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED) {
        return;
    }

    if (entry->load_state >= ASSET_STATE_LOADED) {
        entry->ref_count++;
    }
}

void catalogRetainTypedAsset(asset_type_e expected_type, const char *assetId)
{
    asset_entry_t *entry;

    if (!s_catalogValidateTypedLifecycle("RETAIN", expected_type, assetId)) {
        return;
    }

    entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return;
    }

    s_catalogRetainEntry(entry);
}

/* ========================================================================
 * MEM-3 / C-9: Stage Transition Diff
 *
 * The diff separates the full entry pool into three buckets:
 *   - "currently loaded non-bundled" (load_state >= LOADED, !bundled)
 *   - "needed for new stage" (same category as newStageId's map entry,
 *                             enabled, !bundled)
 *   - shared = intersection → no-op (assets remain resident)
 *   - toUnload = loaded \ needed  -> release typed asset
 *   - toLoad   = needed \ loaded  -> load typed asset
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
