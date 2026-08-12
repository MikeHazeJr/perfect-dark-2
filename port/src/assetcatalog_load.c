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
#include "constants.h"
#include "types.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_deps.h"
#include "catalog_stage_ownership.h"
#include "catalog_dep_activation_plan.h"
#include "catalog_activation_ledger.h"
#include "asset_source_debug.h"
#include "asset_runtime.h"
#include "assetprovider.h"
#include "assetload.h"
#include "mod.h"
#include "modasset_compiler.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_heads.h"
#include "loader_pool.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"
#include "effect_graph_runtime.h"  /* c3849 Unit 8: ASSET_EFFECT activation/clear hooks */
#include "data.h"
#include "game/modeldef.h"
#include "lib/anim.h"   /* c3849 Wave 2: animGetTotalCount */
#include "lib/meshcollision.h"
#include "langmanifest.h"
#include "system.h"
#include "fs.h"
#include "voice_locale_source.h"

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
static s32 s_catalogTypePreloadsBundledMetadataPayload(asset_type_e type);
static s32 s_catalogLoadEntryMetadataPayload(asset_entry_t *entry);
static s32 s_catalogPreloadBundledMetadataPayload(asset_entry_t *entry);
static s32 s_catalogLedgerReload(
	const catalog_activation_root_t *root, void *userdata);

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
                && s_catalogTypePreloadsBundledMetadataPayload(e->type)) {
            asset_entry_t *mutable_entry = assetCatalogGetMutable(e->id);
            if (mutable_entry && mutable_entry->load_state < ASSET_STATE_ACTIVE) {
                (void)s_catalogPreloadBundledMetadataPayload(mutable_entry);
            }
        }
    }

    s_Initialized = 1;
    sysLogPrintf(LOG_NOTE, "catalogLoadInit: %d base-game + %d mod override(s) indexed from %d catalog entries",
                 bundled_count, override_count, total);

    /* c3849 Wave 2: seed catalog-owned custom anim rows (covers mod-rebuild
     * reloads; the boot path calls this from animsInit instead, because the
     * first catalogLoadInit runs before g_Anims exists). */
    catalogSeedCustomAnimRows();

    /* Registration has now published complete source/provider metadata.
     * Replacement-invalidated parents may re-enter production only through
     * the same typed activation transaction used on first load. */
    (void)catalogActivationLedgerReloadPending(s_catalogLedgerReload, NULL);
}

/* c3849 Wave 2: write the g_Anims rows for catalog-owned custom anim slots
 * from catalog metadata. The 0xffffffff data sentinel routes playback through
 * the clip-replacement machinery (animLoadHeader/Frame -> modAnimationLoadData
 * -> clip compile installs exact values over this seed); custom rows never
 * touch the ROM segment. Safe no-op before animsInit. */
void catalogSeedCustomAnimRows(void)
{
    s32 total_entries;
    s32 i;

    if (!g_Anims) {
        return;
    }

    total_entries = assetCatalogGetCount();

    for (i = 0; i < total_entries; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        s32 anim_id;

        if (!e || !e->occupied || !e->enabled || e->type != ASSET_ANIMATION) {
            continue;
        }

        anim_id = e->ext.anim.anim_id;
        if (anim_id < g_NumAnimations || anim_id >= animGetTotalCount()) {
            continue;
        }

        g_Anims[anim_id].numframes = (u16)e->ext.anim.frame_count;
        g_Anims[anim_id].bytesperframe = (u16)e->ext.anim.bytes_per_frame;
        g_Anims[anim_id].data = 0xffffffff;
        g_Anims[anim_id].headerlen = (u16)e->ext.anim.header_len;
        g_Anims[anim_id].framelen = (u8)e->ext.anim.framelen;
        g_Anims[anim_id].flags = (u8)e->ext.anim.flags;

        if (g_AnimReplacements) {
            g_AnimReplacements[anim_id] = NULL;
        }
    }
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
        r.path           = voiceLocaleSelectAudioPath(e);
        if (!r.path) {
            r.path = entryGetFilePath(e);
        }
        if (r.path) {
            r.is_mod_override = 1;
        }
    }
    s_catalogApplySourceOnlyDebug(&r, e);
    return r;
}

CatalogResolveResult catalogResolveMusicSequence(s32 tracknum)
{
    CatalogResolveResult r = { NULL, -1, 0, 0 };
	const char *virtual_id = modSequenceVirtualTrackId(tracknum);

    if (!s_Initialized || tracknum < 0) {
        return r;
    }

    for (s32 i = 0; i < assetCatalogGetPoolSize(); i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e || !e->enabled || e->type != ASSET_AUDIO) {
            continue;
        }
        if (e->ext.audio.category != AUDIO_CAT_MUSIC) {
            continue;
        }
		if (virtual_id) {
			if (strcmp(e->id, virtual_id) != 0) {
				continue;
			}
		} else if (e->ext.audio.sound_id != tracknum) {
			continue;
		}

        r.catalog_id = i;
    }

    if (r.catalog_id < 0) {
        return r;
    }

	const asset_entry_t *entry = assetCatalogGetByIndex(r.catalog_id);
	if (entry) {
		r.path = entryGetFilePath(entry);
		if (r.path) {
			r.is_mod_override = 1;
		}
		s_catalogApplySourceOnlyDebug(&r, entry);
	}
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
		return 1;
	default:
		return 0;
    }
}

static s32 s_catalogPathEndsWithNoCase(const char *path, const char *suffix)
{
    size_t path_len;
    size_t suffix_len;

    if (!path || !suffix) {
        return 0;
    }

    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (path_len < suffix_len) {
        return 0;
    }

    path += path_len - suffix_len;
    for (size_t i = 0; i < suffix_len; i++) {
        char a = path[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b - 'A' + 'a');
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static s32 s_catalogModelPayloadSourcePath(const asset_entry_t *entry,
                                           const char *source_path,
                                           char *out,
                                           size_t out_cap)
{
    static const char *const members[] = {
        "model.obj",
        "model.gltf",
        "model.glb",
    };

    if (!out || out_cap == 0) {
        return 0;
    }
    out[0] = '\0';

    if (!source_path || !source_path[0]) {
        return 0;
    }

    if (modAssetCompilerIsExternalSource(source_path)) {
        snprintf(out, out_cap, "%s", source_path);
        out[out_cap - 1] = '\0';
        return 1;
    }

    if (!s_catalogPathEndsWithNoCase(source_path, ".pdmesh")) {
        return 0;
    }

    for (size_t i = 0; i < sizeof(members) / sizeof(members[0]); i++) {
        char candidate[FS_MAXPATH + 1];
        snprintf(candidate, sizeof(candidate), "%s::%s", source_path, members[i]);
        candidate[sizeof(candidate) - 1] = '\0';
        if (fsFileSize(candidate) > 0) {
            snprintf(out, out_cap, "%s", candidate);
            out[out_cap - 1] = '\0';
            return 1;
        }
    }

    sysLogPrintf(LOG_WARNING,
                 "CATALOG.LIFECYCLE.ACTIVATE: '%s' mesh archive source has no model.obj/model.gltf/model.glb member (%s)",
                 entry ? entry->id : "?", source_path);
    return 0;
}

static s32 s_catalogLoadEntryModelPayload(asset_entry_t *entry, asset_data_handle_t handle)
{
    char desc[128];
    modasset_compiled_result_t compiled;
    const char *source_path = NULL;
    char resolved_source_path[FS_MAXPATH + 1];
    struct modeldef *modeldef;
    s32 loaded_size;

    if (handle.provider == fileProvider()) {
        source_path = fileProviderPath(handle);
    }

    if (s_catalogModelPayloadSourcePath(entry, source_path,
            resolved_source_path, sizeof(resolved_source_path))) {
        s32 cache_result = modAssetCompilerCompileReadable(entry,
            s_catalogPayloadKind(entry->type), resolved_source_path, &compiled);
        if (cache_result < 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' external %s source validation failed (%s)",
                         entry->id, s_catalogPayloadKind(entry->type),
                         assetDescribe(handle, desc, sizeof(desc)));
            return 0;
        }

        if (modAssetCompilerBuildModeldef(entry, resolved_source_path, &modeldef) <= 0
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
    const char *source_path = entryGetFilePath(entry);

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

    if (!assetRuntimeActivateCatalogEntry(entry, source_path)) {
        entry->loaded_data     = NULL;
        entry->payload_kind    = ASSET_PAYLOAD_NONE;
        entry->load_state      = ASSET_STATE_ENABLED;
        entry->ref_count       = 0;
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' language runtime adapter rejected missing strings source",
                     entry->id);
        return 0;
    }

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
        || type == ASSET_TEXTURES
        || type == ASSET_SFX
        || type == ASSET_MUSIC
        || type == ASSET_UI
        || type == ASSET_TOOL
        || type == ASSET_PROP
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

static s32 s_catalogTypePreloadsBundledMetadataPayload(asset_type_e type)
{
    if (type == ASSET_ANIMATION || type == ASSET_SCENARIO) {
        return 0;
    }
    return s_catalogTypeUsesMetadataRuntimePayload(type);
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
    return type == ASSET_WEAPON || type == ASSET_PROJECTILE || type == ASSET_ENTITY;
}

/* c3849 Unit 8: ASSET_EFFECT entries register into the effect graph runtime
 * (effect_graph_runtime.c), keyed by asset_id string. Sibling of the
 * weapon-graph hook above; same loud-fail activation contract. */
static s32 s_catalogTypeUsesEffectGraphRuntime(asset_type_e type)
{
    return type == ASSET_EFFECT;
}

static s32 s_catalogExtractTypedArchiveRoot(const char *source_path,
                                            const char *archive_ext,
                                            char *out,
                                            size_t out_cap)
{
    size_t ext_len;
    const char *segment;
    const char *source_end;

    if (!source_path || !source_path[0] || !archive_ext || !archive_ext[0]
            || !out || out_cap == 0) {
        return 0;
    }

    ext_len = strlen(archive_ext);
    source_end = source_path + strlen(source_path);
    segment = source_path;

    while (segment < source_end) {
        const char *member_sep = strstr(segment, "::");
        const char *segment_end = member_sep ? member_sep : source_end;
        size_t segment_len = (size_t)(segment_end - segment);

        if (segment_len >= ext_len
                && strncmp(segment_end - ext_len, archive_ext, ext_len) == 0) {
            size_t root_len = (size_t)(segment_end - source_path);

            if (root_len == 0 || root_len >= out_cap) {
                return 0;
            }

            memcpy(out, source_path, root_len);
            out[root_len] = '\0';
            return 1;
        }

        if (!member_sep) {
            break;
        }
        segment = member_sep + 2;
    }

    return 0;
}

static s32 s_catalogLoadTextSource(const char *source_path,
                                   char **out,
                                   u32 *out_size,
                                   char *err,
                                   size_t err_cap)
{
    char *text;
    u32 size = 0;

    if (out) {
        *out = NULL;
    }
    if (out_size) {
        *out_size = 0;
    }
    if (!source_path || !source_path[0] || !out) {
        snprintf(err, err_cap, "missing source path");
        return 0;
    }

    text = (char *)fsFileLoad(source_path, &size);
    if (!text || size == 0) {
        if (text) {
            sysMemFree(text);
        }
        snprintf(err, err_cap, "could not load %s", source_path);
        return 0;
    }

    *out = text;
    if (out_size) {
        *out_size = size;
    }
    return 1;
}

static s32 s_catalogActivateLooseWeaponGraphRuntime(asset_entry_t *entry)
{
    char err[256];
    char *primary = NULL;
    char *secondary = NULL;
    char *shared = NULL;
    char *settings = NULL;
    char *variables = NULL;
    char *presentation = NULL;
    u32 primary_size = 0;
    u32 secondary_size = 0;
    u32 shared_size = 0;
    u32 settings_size = 0;
    u32 variables_size = 0;
    u32 presentation_size = 0;
    s32 result = 0;

    if (!entry) {
        return 0;
    }

    if (!entry->ext.weapon.primary_graph[0]
            || !entry->ext.weapon.secondary_graph[0]
            || !entry->ext.weapon.shared_context[0]
            || !entry->ext.weapon.settings_file[0]
            || !entry->ext.weapon.variables_file[0]) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph source is incomplete; refusing selected public weapon source",
                     entry->id);
        return 0;
    }

    if (entry->runtime_index < 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph source has no runtime weapon slot",
                     entry->id);
        return 0;
    }

    err[0] = '\0';
    if (!s_catalogLoadTextSource(entry->ext.weapon.primary_graph,
            &primary, &primary_size, err, sizeof(err))
            || !s_catalogLoadTextSource(entry->ext.weapon.secondary_graph,
            &secondary, &secondary_size, err, sizeof(err))
            || !s_catalogLoadTextSource(entry->ext.weapon.shared_context,
            &shared, &shared_size, err, sizeof(err))
            /* c3849 Wave 5f Unit 2: the tunables pair is part of the held
             * source contract (refused above when unnamed); a named-but-
             * unreadable file is the same loud failure as a graph source. */
            || !s_catalogLoadTextSource(entry->ext.weapon.settings_file,
            &settings, &settings_size, err, sizeof(err))
            || !s_catalogLoadTextSource(entry->ext.weapon.variables_file,
            &variables, &variables_size, err, sizeof(err))) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph source missing split graph source: %s",
                     entry->id, err[0] ? err : "unknown error");
        if (primary) sysMemFree(primary);
        if (secondary) sysMemFree(secondary);
        if (shared) sysMemFree(shared);
        if (settings) sysMemFree(settings);
        if (variables) sysMemFree(variables);
        return 0;
    }
    /* presentation.json is optional on the loose path (older loose mods
     * predate the bindings trio); named-but-unreadable stays loud. */
    if (entry->ext.weapon.presentation_file[0]
            && !s_catalogLoadTextSource(entry->ext.weapon.presentation_file,
            &presentation, &presentation_size, err, sizeof(err))) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph presentation source unreadable: %s",
                     entry->id, err[0] ? err : "unknown error");
        if (primary) sysMemFree(primary);
        if (secondary) sysMemFree(secondary);
        if (shared) sysMemFree(shared);
        if (settings) sysMemFree(settings);
        if (variables) sysMemFree(variables);
        return 0;
    }
    result = weaponGraphRuntimeRegisterWeaponSourceJson(
        entry->runtime_index, entry->id, primary, primary_size,
        secondary, secondary_size, shared, shared_size,
        settings, settings_size, variables, variables_size,
        presentation, presentation_size, err, sizeof(err));
    if (primary) sysMemFree(primary);
    if (secondary) sysMemFree(secondary);
    if (shared) sysMemFree(shared);
    if (settings) sysMemFree(settings);
    if (variables) sysMemFree(variables);
    if (presentation) sysMemFree(presentation);

    if (result != 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph source compile failed: %s",
                     entry->id, err[0] ? err : "unknown error");
        return 0;
    }

    return 1;
}

static s32 s_catalogPreloadBundledMetadataPayload(asset_entry_t *entry)
{
    if (assetSourceDebugEntryRequiresPublicFileSource(entry)) {
        sysLogPrintf(LOG_WARNING,
                     "ASSET.SOURCE_ONLY: bundled metadata preload for typed '%s' %s has no public FileProvider source; refusing activation",
                     entry->id, assetSourceDebugTypeLabel(entry->type));
        return 0;
    }

    return s_catalogLoadEntryMetadataPayload(entry);
}

static s32 s_catalogActivateWeaponGraphRuntime(asset_entry_t *entry,
                                               const char *source_path)
{
    u32 graph_size = 0;
    char err[256];
    char archive_path[FS_MAXPATH + 1];
    char full_buf[FS_MAXPATH + 1];
    char *graph = NULL;
    const char *full;
    const char *active_archive;
    s32 result;

    if (!entry || !source_path || !source_path[0]) {
        return 0;
    }

    if (entry->type == ASSET_WEAPON) {
        if (!s_catalogExtractTypedArchiveRoot(source_path, ".pdweapon",
                archive_path, sizeof(archive_path))) {
            return s_catalogActivateLooseWeaponGraphRuntime(entry);
        }

        if (entry->runtime_index < 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph archive has no runtime weapon slot",
                         entry->id);
            return 0;
        }

        active_archive = archive_path;
        err[0] = '\0';
        result = weaponGraphRuntimeRegisterWeaponArchive(entry->runtime_index,
            archive_path, err, sizeof(err));
        if (result != 0 && !fsPathIsAbsolute(archive_path)) {
            full = fsFullPath(archive_path, full_buf, sizeof(full_buf));
            if (full && strcmp(full, archive_path) != 0) {
                err[0] = '\0';
                result = weaponGraphRuntimeRegisterWeaponArchive(entry->runtime_index,
                    full, err, sizeof(err));
                if (result == 0) {
                    active_archive = full;
                }
            }
        }
        if (result != 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' held graph archive compile failed: %s",
                         entry->id, err[0] ? err : "unknown error");
            return 0;
        }

		/* B-1021: graph activation alone is not enough. The legacy held-model
		 * path enters through loader_pool's struct weapon slot, so install a
		 * stable adapter derived only from public weapon.ini plus the compiled
		 * public held graphs. A private manifest is never behavior authority. */
		if (entry->runtime_index >= WEAPON_CUSTOM_START) {
			weapon_graph_archive_descriptor_t descriptor;
			err[0] = '\0';
			if (weaponGraphArchiveReadDescriptorFile(active_archive, ASSET_WEAPON,
					&descriptor, err, sizeof(err)) != 0 ||
					strcmp(descriptor.catalog_id, entry->id) != 0) {
				weaponGraphRuntimeClearWeapon(entry->runtime_index);
				effectGraphRuntimeReleaseOwner(entry->id);
				sysLogPrintf(LOG_WARNING,
					"CATALOG.LIFECYCLE.ACTIVATE: '%s' public weapon adapter descriptor rejected: %s",
					entry->id, err[0] ? err : "catalog identity mismatch");
				return 0;
			}
			err[0] = '\0';
			if (!loaderPoolInstallPublicWeaponAdapter(entry->runtime_index,
					entry->id, entry->source_filenum,
					entry->ext.weapon.dual_wieldable, &descriptor,
					weaponGraphRuntimeGetHeldFunction(entry->runtime_index, 0),
					weaponGraphRuntimeGetHeldFunction(entry->runtime_index, 1),
					err, sizeof(err))) {
				loaderPoolClearPublicWeaponAdapter(entry->runtime_index);
				weaponGraphRuntimeClearWeapon(entry->runtime_index);
				effectGraphRuntimeReleaseOwner(entry->id);
				sysLogPrintf(LOG_WARNING,
					"CATALOG.LIFECYCLE.ACTIVATE: '%s' public weapon adapter install failed: %s",
					entry->id, err[0] ? err : "unknown error");
				return 0;
			}
		}

        return 1;
    }

    graph = (char *)fsFileLoad(source_path, &graph_size);
    if (!graph || graph_size == 0) {
        if (graph) {
            sysMemFree(graph);
        }
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' graph runtime missing %s",
                     entry->id, source_path);
        return 0;
    }

    err[0] = '\0';
    result = weaponGraphRuntimeRegisterBehaviorGraphJson(entry->type,
        entry->id, graph, graph_size, err, sizeof(err));
    sysMemFree(graph);

    if (result != 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' graph runtime compile failed: %s",
                     entry->id, err[0] ? err : "unknown error");
        return 0;
    }

    return 1;
}

static void s_catalogClearWeaponGraphRuntime(asset_entry_t *entry,
                                             const char *assetId)
{
    if (!entry) {
        return;
    }

    if (entry->type == ASSET_WEAPON) {
        if (entry->runtime_index >= 0) {
			loaderPoolClearPublicWeaponAdapter(entry->runtime_index);
            weaponGraphRuntimeClearWeapon(entry->runtime_index);
        }
		/* T-ASSETS-020: nested effects are owned by the parent weapon ID and
		 * share its catalog lifecycle rather than leaking until process exit. */
		effectGraphRuntimeReleaseOwner(assetId);
        return;
    }

    if (entry->type == ASSET_PROJECTILE || entry->type == ASSET_ENTITY) {
        weaponGraphRuntimeClearAsset(assetId);
    }

}

/* c3849 Unit 8: ASSET_EFFECT activation. Archive primaries (.pdeffect,
 * possibly behind a "::" member chain) register through the descriptor path;
 * a loose graph file (ext.effect.effect_file holding a real path) registers
 * through fsFileLoad + RegisterGraphJson, mirroring how weapons handle the
 * archive/loose split. Loud-fail: a compile failure refuses activation. */
static s32 s_catalogActivateEffectGraphRuntime(asset_entry_t *entry,
                                               const char *source_path)
{
    char archive_path[FS_MAXPATH + 1];
    char full_buf[FS_MAXPATH + 1];
    char err[256];
    const char *full;
    char *graph = NULL;
    u32 graph_size = 0;
    s32 result;

    if (!entry || !source_path || !source_path[0]) {
        return 0;
    }

	/* A reload is a new selected-source transaction. Remove this catalog
	 * owner's prior program first so a missing/corrupt edit cannot continue
	 * using stale last-good behavior after activation is rejected. */
	effectGraphRuntimeReleaseOwner(entry->id);

    if (s_catalogExtractTypedArchiveRoot(source_path, ".pdeffect",
            archive_path, sizeof(archive_path))) {
        full = fsFullPath(archive_path, full_buf, sizeof(full_buf));
        err[0] = '\0';
        result = effectGraphRuntimeRegisterArchive(full ? full : archive_path,
            err, sizeof(err));
        if (result != 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' effect graph archive compile failed: %s",
                         entry->id, err[0] ? err : "unknown error");
            return 0;
        }
        return 1;
    }

    if (!entry->ext.effect.effect_file[0]) {
        sysLogPrintf(LOG_NOTE,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' effect entry has no graph source; skipping effect runtime registration",
                     entry->id);
        return 1;
    }

    graph = (char *)fsFileLoad(entry->ext.effect.effect_file, &graph_size);
    if (!graph || graph_size == 0) {
        if (graph) {
            sysMemFree(graph);
        }
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' effect graph source missing %s",
                     entry->id, entry->ext.effect.effect_file);
        return 0;
    }

    err[0] = '\0';
    result = effectGraphRuntimeRegisterGraphJson(entry->id, graph, graph_size,
        err, sizeof(err));
    sysMemFree(graph);

    if (result != 0) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' effect graph compile failed: %s",
                     entry->id, err[0] ? err : "unknown error");
        return 0;
    }

    return 1;
}

/* c3849 Unit 8: sibling of s_catalogClearWeaponGraphRuntime. */
static void s_catalogClearEffectGraphRuntime(asset_entry_t *entry,
                                             const char *assetId)
{
    if (!entry || entry->type != ASSET_EFFECT) {
        return;
    }
    effectGraphRuntimeReleaseOwner(assetId);
}

static void s_catalogInstallAnimationClip(asset_entry_t *entry,
                                          const struct animtableentry *anim)
{
    s32 anim_id;

    if (!entry || !anim) {
        return;
    }

    anim_id = entry->ext.anim.anim_id;
    /* c3849 Wave 2: custom slots install too. */
    if (anim_id < 0 || anim_id >= animGetTotalCount() || !g_Anims) {
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

    if (!modAssetCompilerIsAnimationSource(source_path)) {
        return 0;
    }

    memset(&compiled, 0, sizeof(compiled));
    if (modAssetCompilerIsExternalSource(source_path)) {
        compile_result = modAssetCompilerCompileReadable(entry, "animation",
            source_path, &compiled);
        if (compile_result < 0) {
            sysLogPrintf(LOG_WARNING,
                         "CATALOG.LIFECYCLE.ACTIVATE: '%s' external animation cache failed",
                         entry->id);
            return -1;
        }
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

    if (s_catalogTypeUsesEffectGraphRuntime(entry->type)
            && !s_catalogActivateEffectGraphRuntime(entry, source_path)) {
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

    /*
     * B-959: descriptor registration is not runtime utilization. Families
     * with structured public source must parse and validate their selected
     * archive members before the catalog row is considered active. The
     * hydration call is a no-op success for families whose dedicated runtime
     * loader already owns their source.
     */
    if (assetRuntimeSupportsType(entry->type)
            && !assetRuntimeHydrateCatalogEntry(entry)) {
        assetRuntimeReleaseCatalogEntry(entry->id);
        entry->loaded_data     = NULL;
        entry->payload_kind    = ASSET_PAYLOAD_NONE;
        entry->load_state      = ASSET_STATE_ENABLED;
        entry->ref_count       = 0;
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.ACTIVATE: '%s' %s public source failed hydration",
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

    /* Loaded payloads retain before any type-specific activation. Audio and
     * animation activators initialise ref_count to one; calling them again
     * would otherwise clobber an explicit manifest reference when a weapon
     * subsequently loads the same dependency closure. */
    if (entry->load_state >= ASSET_STATE_LOADED && entry->loaded_data) {
        if (!entry->bundled && entry->ref_count != ASSET_REF_BUNDLED) {
            entry->ref_count++;
        }
        return 1;
    }

    if (entry->type == ASSET_ANIMATION) {
        s32 animation_payload = s_catalogLoadEntryAnimationPayload(entry);
        if (animation_payload != 0) {
            return animation_payload > 0;
        }
    }

    if ((entry->bundled || entry->ref_count == ASSET_REF_BUNDLED)
            && entry->source.primary.provider == fileProvider()
            && s_catalogTypeUsesMetadataRuntimePayload(entry->type)) {
        if (entry->load_state >= ASSET_STATE_ACTIVE && entry->loaded_data) {
            return 1;
        }
        return s_catalogLoadEntryMetadataPayload(entry);
    }

    if ((entry->bundled || entry->ref_count == ASSET_REF_BUNDLED)
            && entry->source.primary.provider == fileProvider()
            && s_catalogTypeUsesModelPayload(entry->type)) {
        if (entry->load_state >= ASSET_STATE_LOADED && entry->loaded_data) {
            entry->ref_count++;
            return 1;
        }
        return s_catalogLoadEntryModelPayload(entry, handle);
    }

    if (s_catalogTypeUsesAudioRuntimePayload(entry->type)) {
        return s_catalogLoadEntryAudioPayload(entry, handle);
    }

    if (entry->type == ASSET_TEXTURE) {
        return s_catalogLoadEntryTexturePayload(entry, handle);
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

    if (s_catalogTypeUsesMetadataRuntimePayload(entry->type)) {
        return s_catalogLoadEntryMetadataPayload(entry);
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

static void s_catalogUnloadEntryRawMode(const char *assetId,
        asset_entry_t *entry, s32 force_bundled);

static void s_catalogUnloadEntryRaw(const char *assetId, asset_entry_t *entry)
{
    s_catalogUnloadEntryRawMode(assetId, entry, 0);
}

static s32 s_catalogResolveActivationNode(const char *asset_id,
        asset_type_e *out_actual_type, void *userdata)
{
    const asset_entry_t *entry = assetCatalogResolve(asset_id);
    (void)userdata;
    if (!entry || !entry->enabled) return 0;
    if (out_actual_type) *out_actual_type = entry->type;
    return 1;
}

static s32 s_catalogResolveDeactivationNode(const char *asset_id,
        asset_type_e *out_actual_type, void *userdata)
{
    const asset_entry_t *entry = assetCatalogResolve(asset_id);
    (void)userdata;
    if (!entry) return 0;
    if (out_actual_type) *out_actual_type = entry->type;
    return 1;
}

static void s_catalogRollbackActivationNode(const char *asset_id,
        asset_type_e expected_type, void *userdata)
{
    asset_entry_t *entry = assetCatalogGetMutable(asset_id);
    (void)expected_type;
    (void)userdata;
    if (entry) s_catalogUnloadEntryRaw(asset_id, entry);
}

s32 catalogLoadTypedAsset(asset_type_e expected_type, const char *assetId)
{
    catalog_dep_activation_plan_t plan = {0};
    char error[256];
    size_t loaded = 0;
    u8 *was_resident = NULL;

    if (!s_catalogValidateTypedLifecycle("LOAD", expected_type, assetId)) {
        return 0;
    }
    if (!catalogDepActivationPlanBuild(&plan, assetId, expected_type,
            s_catalogResolveActivationNode, NULL, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.LOAD: '%s' dependency preflight failed: %s",
            assetId, error[0] ? error : "unknown dependency error");
        return 0;
    }
    if (!catalogActivationLedgerReserve(assetId)) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.LOAD: '%s' could not reserve exact root ownership",
            assetId);
        catalogDepActivationPlanFree(&plan);
        return 0;
    }
    was_resident = (u8 *)calloc(plan.count ? plan.count : 1, sizeof(*was_resident));
    if (!was_resident) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.LOAD: '%s' could not snapshot activation transaction",
            assetId);
        catalogDepActivationPlanFree(&plan);
        return 0;
    }
    for (; loaded < plan.count; loaded++) {
        asset_entry_t *entry = assetCatalogGetMutable(plan.nodes[loaded].id);
        if (entry) {
            /* Bundled registration historically labels ROM-backed rows
             * LOADED before any catalog-owned payload exists. Residence is
             * therefore proven by a payload, not by that legacy state bit. */
            was_resident[loaded] = entry->loaded_data != NULL
                || entry->payload_kind != ASSET_PAYLOAD_NONE;
        }
        if (!entry || !s_catalogLoadEntry(entry,
                plan.nodes[loaded].expected_type)) break;
        if (entry->bundled) {
            /* A bundled row reactivated after an explicit disable resumes its
             * process-pinned ownership contract. */
            entry->ref_count = ASSET_REF_BUNDLED;
        }
    }
    if (loaded != plan.count) {
        size_t failed_at = loaded;
        while (loaded > 0) {
            asset_entry_t *entry;
            loaded--;
            entry = assetCatalogGetMutable(plan.nodes[loaded].id);
            if (!entry) continue;
            if (entry->bundled && !was_resident[loaded]) {
                /* A process-pinned row activated by this transaction did not
                 * exist before it. Forced rollback restores the exact prior
                 * empty state instead of leaking a half-published adapter. */
                s_catalogUnloadEntryRawMode(plan.nodes[loaded].id, entry, 1);
            } else {
                s_catalogUnloadEntryRaw(plan.nodes[loaded].id, entry);
            }
        }
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.LOAD: '%s' activation failed at closure row %zu",
            assetId, failed_at);
        free(was_resident);
        catalogDepActivationPlanFree(&plan);
        return 0;
    }
    free(was_resident);
    {
        const asset_entry_t *root = assetCatalogResolve(assetId);
        if (!catalogActivationLedgerRecord(assetId,
                root ? root->type : expected_type,
                root && root->bundled)) {
            catalogDepActivationPlanRollback(&plan, plan.count,
                s_catalogRollbackActivationNode, NULL);
            sysLogPrintf(LOG_WARNING,
                "CATALOG.LIFECYCLE.LOAD: '%s' could not publish exact root ownership",
                assetId);
            catalogDepActivationPlanFree(&plan);
            return 0;
        }
    }
    catalogDepActivationPlanFree(&plan);
    return 1;
}

s32 catalogLoadStageAsset(asset_type_e expected_type, const char *assetId)
{
    asset_entry_t *entry;

    if (!s_catalogValidateTypedLifecycle("STAGE_LOAD", expected_type, assetId)) {
        return 0;
    }
    entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return 0;
    }

    /* One stage can own at most one reference to an entry. A repeated diff is
     * therefore a truthful no-op rather than aggregate ref-count growth. */
    if (catalogStageOwnershipHasRef(entry)) {
        return 1;
    }
    if (!catalogLoadTypedAsset(expected_type, assetId)) {
        return 0;
    }
    if (catalogStageOwnershipAcquire(entry) != 1) {
        /* Single-threaded catalog code should never race this branch. Keep the
         * aggregate reference balanced if the ledger still rejects ownership. */
        catalogReleaseTypedAsset(expected_type, assetId);
        return 0;
    }
    catalogActivationLedgerSetStageOwned(assetId, 1);
    return 1;
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

static void s_catalogDetachRuntimeAdapters(asset_entry_t *entry,
        const char *assetId)
{
    if (!entry || !assetId) return;
    if (s_catalogTypeUsesWeaponGraphRuntime(entry->type)) {
        s_catalogClearWeaponGraphRuntime(entry, assetId);
    }
    if (s_catalogTypeUsesEffectGraphRuntime(entry->type)) {
        s_catalogClearEffectGraphRuntime(entry, assetId);
    }
    assetRuntimeReleaseCatalogEntry(assetId);
}

static void s_catalogUnloadEntryRawMode(const char *assetId,
        asset_entry_t *entry, s32 force_bundled)
{
    s32 old_ref;
    s32 new_ref;

    /* Never evict bundled assets — their catalog-owned source remains process-lifetime. */
    if (!force_bundled
            && (entry->bundled || entry->ref_count == ASSET_REF_BUNDLED)) {
        return;
    }

    old_ref = entry->ref_count;

    if (force_bundled && (entry->bundled
            || entry->ref_count == ASSET_REF_BUNDLED)) {
        /* A full identity rebuild owns the process-pinned reference itself.
         * Collapse it to one so the normal family-specific payload teardown
         * below runs exactly once. */
        old_ref = ASSET_REF_BUNDLED;
        entry->ref_count = 1;
    }

    if (entry->ref_count > 0) {
        entry->ref_count--;
    }

    new_ref = entry->ref_count;

    if (new_ref <= 0 && entry->loaded_data) {
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

            if (entry->type == ASSET_BODY && entry->runtime_index >= 0) {
                catalogManagerResetBodyModeldef(entry->runtime_index);
            } else if (entry->type == ASSET_HEAD && entry->runtime_index >= 0) {
                catalogManagerResetHeadModeldef(entry->runtime_index);
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
            } else if (anim_id >= g_NumAnimations &&
                    anim_id < animGetTotalCount()) {
                /* c3849 Wave 2: a custom slot has no ROM row to restore;
                 * zero it (re-seeded from catalog metadata on next init). */
                if (g_Anims) {
                    memset(&g_Anims[anim_id], 0, sizeof(g_Anims[anim_id]));
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
            s_catalogDetachRuntimeAdapters(entry, assetId);
            /* Runtime-owned activation (for example language banks) is
             * detached from this catalog reference. The owning subsystem
             * releases its memory on its normal reset/reload path. */
        }
        entry->loaded_data     = NULL;
        entry->data_size_bytes = 0;
        entry->payload_kind    = ASSET_PAYLOAD_NONE;
        entry->load_state      = entry->enabled
            ? ASSET_STATE_ENABLED : ASSET_STATE_REGISTERED;
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
    catalog_dep_activation_plan_t plan = {0};
    char error[256];

    if (!s_catalogValidateTypedLifecycle("RELEASE", expected_type, assetId)) {
        return;
    }

    if (!catalogDepActivationPlanBuild(&plan, assetId, expected_type,
            s_catalogResolveActivationNode, NULL, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.RELEASE: '%s' dependency preflight failed: %s",
            assetId, error[0] ? error : "unknown dependency error");
        return;
    }
    catalogDepActivationPlanRollback(&plan, plan.count,
        s_catalogRollbackActivationNode, NULL);
    (void)catalogActivationLedgerRelease(assetId);
    catalogDepActivationPlanFree(&plan);
}

s32 catalogCanDeactivateTypedAsset(asset_type_e expected_type, const char *assetId)
{
    catalog_dep_activation_plan_t plan = {0};
    char error[256];

    if (!s_catalogValidateTypedLifecycle("DEACTIVATE_PREFLIGHT",
            expected_type, assetId)) {
        return 0;
    }
    if (!catalogDepActivationPlanBuild(&plan, assetId, expected_type,
            s_catalogResolveDeactivationNode, NULL, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.DEACTIVATE_PREFLIGHT: '%s' dependency preflight failed: %s",
            assetId, error[0] ? error : "unknown dependency error");
        return 0;
    }
    catalogDepActivationPlanFree(&plan);
    return 1;
}

s32 catalogDeactivateTypedAsset(asset_type_e expected_type, const char *assetId)
{
    catalog_dep_activation_plan_t plan = {0};
    asset_entry_t *root;
    char error[256];
    s32 root_refs;

    if (!s_catalogValidateTypedLifecycle("DEACTIVATE", expected_type, assetId)) {
        return 0;
    }
    root = assetCatalogGetMutable(assetId);
    if (!root || root->bundled || root->ref_count == ASSET_REF_BUNDLED) {
        return root != NULL;
    }
    root_refs = root->ref_count > 0 ? root->ref_count : 0;
    if (root_refs == 0) {
        /* The raw path intentionally also cleans an inconsistent payload with
         * ref_count==0; never null a pointer without running typed teardown. */
        s_catalogUnloadEntryRaw(assetId, root);
        /* An interrupted or legacy activation can leave a family adapter
         * without a payload marker. Detach every adapter class, not only the
         * effect executor owner. */
        s_catalogDetachRuntimeAdapters(root, assetId);
        (void)catalogStageOwnershipRelease(root);
        root->load_state = root->enabled
            ? ASSET_STATE_ENABLED : ASSET_STATE_REGISTERED;
        catalogActivationLedgerForgetActive(assetId);
        return 1;
    }

    if (!catalogDepActivationPlanBuild(&plan, assetId, expected_type,
            s_catalogResolveDeactivationNode, NULL, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.DEACTIVATE: '%s' dependency preflight failed: %s",
            assetId, error[0] ? error : "unknown dependency error");
        /* Never leave the selected runtime reachable merely because stale
         * dependency metadata prevented reconstructing its old closure. */
        effectGraphRuntimeReleaseOwner(assetId);
        return 0;
    }

    while (root_refs-- > 0) {
        catalogDepActivationPlanRollback(&plan, plan.count,
            s_catalogRollbackActivationNode, NULL);
    }
    (void)catalogStageOwnershipRelease(root);

    /* An interrupted activation can own a runtime record without a live
     * payload/ref. Owner release is idempotent and preserves other owners. */
    s_catalogDetachRuntimeAdapters(root, assetId);
    root->ref_count = 0;
    root->load_state = root->enabled
        ? ASSET_STATE_ENABLED : ASSET_STATE_REGISTERED;
    catalogActivationLedgerForgetActive(assetId);
    catalogDepActivationPlanFree(&plan);
    return 1;
}

s32 catalogDeactivateTypedAssetForReset(asset_type_e expected_type,
                                        const char *assetId)
{
    asset_entry_t *root;

    if (!s_catalogValidateTypedLifecycle("RESET_DEACTIVATE",
            expected_type, assetId)) {
        return 0;
    }
    root = assetCatalogGetMutable(assetId);
    if (!root) {
        return 0;
    }
    if (!root->bundled && root->ref_count != ASSET_REF_BUNDLED) {
        return catalogDeactivateTypedAsset(expected_type, assetId);
    }

    /* Bundled dependency rows are independently represented in the reset
     * snapshot and will each be retired exactly once. Do not reinterpret the
     * process-pinned sentinel as an aggregate dependency count. */
    s_catalogUnloadEntryRawMode(assetId, root, 1);
    s_catalogDetachRuntimeAdapters(root, assetId);
    (void)catalogStageOwnershipRelease(root);
    root->load_state = root->enabled
        ? ASSET_STATE_ENABLED : ASSET_STATE_REGISTERED;
    catalogActivationLedgerForgetActive(assetId);
    return root->loaded_data == NULL
        && root->payload_kind == ASSET_PAYLOAD_NONE;
}

void catalogReleaseStageAsset(asset_type_e expected_type, const char *assetId)
{
    asset_entry_t *entry;

    if (!s_catalogValidateTypedLifecycle("STAGE_RELEASE", expected_type, assetId)) {
        return;
    }
    entry = assetCatalogGetMutable(assetId);
    if (!entry) {
        return;
    }
    if (catalogStageOwnershipRelease(entry) != 1) {
        sysLogPrintf(LOG_WARNING,
                     "CATALOG.LIFECYCLE.STAGE_RELEASE: '%s' has no stage-owned reference",
                     assetId);
        return;
    }
    catalogActivationLedgerSetStageOwned(assetId, 0);
    catalogReleaseTypedAsset(expected_type, assetId);
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
    catalog_dep_activation_plan_t plan = {0};
    char error[256];
    const asset_entry_t *root;

    if (!s_catalogValidateTypedLifecycle("RETAIN", expected_type, assetId)) {
        return;
    }
    root = assetCatalogResolve(assetId);
    if (!root || (root->load_state < ASSET_STATE_LOADED
            && root->ref_count != ASSET_REF_BUNDLED)) {
        return;
    }

    if (!catalogDepActivationPlanBuild(&plan, assetId, expected_type,
            s_catalogResolveActivationNode, NULL, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.RETAIN: '%s' dependency preflight failed: %s",
            assetId, error[0] ? error : "unknown dependency error");
        return;
    }
    if (!catalogActivationLedgerReserve(assetId)) {
        catalogDepActivationPlanFree(&plan);
        return;
    }
    for (size_t i = 0; i < plan.count; i++) {
        const asset_entry_t *entry = assetCatalogResolve(plan.nodes[i].id);
        if (!entry || (!entry->bundled
                && entry->ref_count != ASSET_REF_BUNDLED
                && (entry->load_state < ASSET_STATE_LOADED
                    || entry->ref_count <= 0))) {
            sysLogPrintf(LOG_WARNING,
                "CATALOG.LIFECYCLE.RETAIN: '%s' closure row '%s' is not resident; unchanged",
                assetId, plan.nodes[i].id);
            catalogDepActivationPlanFree(&plan);
            return;
        }
    }
    for (size_t i = 0; i < plan.count; i++) {
        asset_entry_t *entry = assetCatalogGetMutable(plan.nodes[i].id);
        if (entry) s_catalogRetainEntry(entry);
    }
    {
        (void)catalogActivationLedgerRecord(assetId,
            root ? root->type : expected_type,
            root && root->bundled);
    }
    catalogDepActivationPlanFree(&plan);
}

static s32 s_catalogLedgerContains(
        const catalog_activation_root_t *root, const char *dependency_id,
        s32 *out_contains, void *userdata)
{
    catalog_dep_activation_plan_t plan = {0};
    char error[256];
    (void)userdata;
    if (out_contains) *out_contains = 0;
    if (!root || !dependency_id || !out_contains) return 0;
    if (!catalogDepActivationPlanBuild(&plan, root->id, root->type,
            s_catalogResolveDeactivationNode, NULL, error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.INVALIDATE: root '%s' preflight failed: %s",
            root->id, error[0] ? error : "unknown dependency error");
        return 0;
    }
    for (size_t i = 0; i < plan.count; i++) {
        if (strcmp(plan.nodes[i].id, dependency_id) == 0) {
            *out_contains = 1;
            break;
        }
    }
    catalogDepActivationPlanFree(&plan);
    return 1;
}

static void s_catalogLedgerRetire(
        const catalog_activation_root_t *root, void *userdata)
{
    catalog_dep_activation_plan_t plan = {0};
    char error[256];
    (void)userdata;
    if (!root || !catalogDepActivationPlanBuild(&plan, root->id, root->type,
            s_catalogResolveDeactivationNode, NULL, error, sizeof(error))) {
        return; /* Complete-ledger preflight made this branch unreachable. */
    }
    for (s32 ref = 0; ref < root->references; ref++) {
        for (size_t i = plan.count; i-- > 0;) {
            asset_entry_t *entry = assetCatalogGetMutable(plan.nodes[i].id);
            if (!entry) continue;
            if (root->bundled && strcmp(plan.nodes[i].id, root->id) == 0) {
                s_catalogUnloadEntryRawMode(plan.nodes[i].id, entry, 1);
            } else {
                s_catalogUnloadEntryRaw(plan.nodes[i].id, entry);
            }
        }
    }
    {
        asset_entry_t *entry = assetCatalogGetMutable(root->id);
        if (entry) {
            s_catalogDetachRuntimeAdapters(entry, root->id);
            (void)catalogStageOwnershipRelease(entry);
            entry->load_state = entry->enabled
                ? ASSET_STATE_ENABLED : ASSET_STATE_REGISTERED;
        }
    }
    catalogDepActivationPlanFree(&plan);
}

static s32 s_catalogLedgerReload(
        const catalog_activation_root_t *root, void *userdata)
{
    s32 loaded = 0;
    (void)userdata;
    if (!root || root->references <= 0) return 0;
    for (; loaded < root->references; loaded++) {
        if (!catalogLoadTypedAsset(root->type, root->id)) break;
    }
    if (loaded != root->references) {
        while (loaded-- > 0) catalogReleaseTypedAsset(root->type, root->id);
        return 0;
    }
    if (root->stage_owned) {
        asset_entry_t *entry = assetCatalogGetMutable(root->id);
        if (!entry || catalogStageOwnershipAcquire(entry) != 1) {
            while (loaded-- > 0) catalogReleaseTypedAsset(root->type, root->id);
            return 0;
        }
        catalogActivationLedgerSetStageOwned(root->id, 1);
    }
    return 1;
}

s32 catalogInvalidateTypedAssetDependents(const char *dependency_id)
{
    if (!dependency_id || !dependency_id[0]) return 0;
    if (!catalogActivationLedgerInvalidateDependents(dependency_id,
            s_catalogLedgerContains, s_catalogLedgerRetire, NULL)) {
        sysLogPrintf(LOG_WARNING,
            "CATALOG.LIFECYCLE.INVALIDATE: '%s' dependent transaction rejected; runtime unchanged",
            dependency_id);
        return 0;
    }
    return 1;
}

s32 catalogCanInvalidateTypedAssetDependents(const char *dependency_id)
{
    if (!dependency_id || !dependency_id[0]) return 0;
    return catalogActivationLedgerCanInvalidateDependents(dependency_id,
        s_catalogLedgerContains, NULL);
}

s32 catalogReloadInvalidatedTypedAssets(void)
{
    return catalogActivationLedgerReloadPending(s_catalogLedgerReload, NULL);
}

s32 catalogPrepareTypedAssetReplacement(asset_type_e replacement_type,
        const char *asset_id)
{
    const asset_entry_t *prior;
    asset_type_e prior_type;
    (void)replacement_type;
    if (!asset_id || !asset_id[0]) return 0;
    prior = assetCatalogResolve(asset_id);
    if (!prior) return 1;
    prior_type = prior->type;
    if (!catalogCanDeactivateTypedAsset(prior_type, asset_id)
            || !catalogActivationLedgerInvalidateReplacementRoots(asset_id,
                s_catalogLedgerContains, s_catalogLedgerRetire, NULL)) {
        return 0;
    }
    if (!catalogDeactivateTypedAssetForReset(prior_type, asset_id)) return 0;
    return 1;
}

void catalogTypedLifecycleClearMods(void)
{
    catalogActivationLedgerClearMods();
}

void catalogTypedLifecycleClear(void)
{
    catalogActivationLedgerClear();
}

/* ========================================================================
 * MEM-3 / C-9: Stage Transition Diff
 *
 * The diff separates the full entry pool into three buckets:
 *   - "currently stage-owned non-bundled" (stage_ref_count == 1, !bundled)
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
        if (catalogStageOwnershipHasRef(e)) {
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
