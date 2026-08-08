/*
 * tests/stubs.c -- Linker-required stub definitions for pd-tests.
 *
 * The test binary cherry-picks a small set of source files from src/ and
 * port/src/. Those files reference symbols defined elsewhere in the game
 * (asset catalog, mod manager, audio mod playlist, system log). Rather
 * than pull in the full subsystem (which cascades through globals), we
 * provide minimal stubs here that satisfy the linker.
 *
 * The stubs are designed to be SEMANTICALLY INERT: they return the value
 * that pushes the code under test down its synthetic-fallback path, so
 * the tests exercise pure logic without coupling to real catalog or mod
 * state.
 *
 * If a future test needs richer behavior than a NULL-returning stub, it
 * should be added here as a switchable mock (e.g., a global table of
 * "fake catalog entries") that the test sets up in its TEST_CASE prelude.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"
#include "types.h"
#include "system.h"
#include "config.h"
#include "assetcatalog.h"
#include "modmgr.h"
#include "audio.h"
#include "game/sparks.h"

/* -------------------------------------------------------------------------
 * c3849 Unit 8: base spark table for sparks_custom.c (the custom spark-row
 * registry under test). The game build defines this in src/game/sparks.c;
 * here only the SPARKTYPE_PROJECTILE row carries real values (the NTSC
 * literals from sparks.c row 0x10) because that is the row the effect
 * runtime clones for tinted custom sparks. Every other row stays zeroed --
 * the registry never reads them.
 * ------------------------------------------------------------------------- */
struct sparktype g_SparkTypes[SPARKTYPE_BASE_COUNT] = {
    [SPARKTYPE_PROJECTILE] =
        { 50, 28, 100, 1, 0, 0, 1, 60, 30, 10, 1, 0xffff80ff, 0xffffffff, 0.02f },
};

/* -------------------------------------------------------------------------
 * sysLogPrintf -- printf to stderr at debug level, drop everything else.
 * Keeps the test output readable while still surfacing genuine errors.
 * ------------------------------------------------------------------------- */
void sysLogPrintf(s32 level, const char *fmt, ...)
{
    if (level == LOG_ERROR || level == LOG_WARNING) {
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "[stub-log L%d] ", (int)level);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        va_end(ap);
    }
}

/* -------------------------------------------------------------------------
 * configRegisterInt -- accept the registration but never persist or
 * re-read. The test binary never loads pd.ini, so the var keeps whatever
 * default the caller set in its file-scope initializer.
 * ------------------------------------------------------------------------- */
void configRegisterInt(const char *key, s32 *var, s32 min, s32 max)
{
    (void)key;
    (void)var;
    (void)min;
    (void)max;
}

typedef struct test_fs_entry {
    const char *path;
    const void *bytes;
    u32 size;
} test_fs_entry_t;

static test_fs_entry_t s_TestFsEntries[8];
static s32 s_TestFsEntryCount;

void testStubFsFileLoadWith(const char *path, const void *bytes, u32 size)
{
    s_TestFsEntryCount = 0;
    if (path && bytes && size > 0) {
        s_TestFsEntries[s_TestFsEntryCount++] = (test_fs_entry_t){ path, bytes, size };
    }
}

void testStubFsFileLoadAdd(const char *path, const void *bytes, u32 size)
{
    if (path && bytes && size > 0 && s_TestFsEntryCount < 8) {
        s_TestFsEntries[s_TestFsEntryCount++] = (test_fs_entry_t){ path, bytes, size };
    }
}

void *fsFileLoad(const char *name, u32 *outSize)
{
    for (s32 i = 0; name && i < s_TestFsEntryCount; i++) {
        if (strcmp(name, s_TestFsEntries[i].path) == 0) {
            void *copy = malloc(s_TestFsEntries[i].size);
            if (!copy) {
                if (outSize) *outSize = 0;
                return NULL;
            }
            memcpy(copy, s_TestFsEntries[i].bytes, s_TestFsEntries[i].size);
            if (outSize) *outSize = s_TestFsEntries[i].size;
            return copy;
        }
    }
    if (outSize) {
        *outSize = 0;
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Asset catalog stubs.
 *
 * assetCatalogResolve returning NULL forces every netmanifest call site
 * down the synthetic-fallback path, which uses s_fnv1a(id) for net_hash
 * and stores the id string verbatim. That's the deterministic path we
 * want under test.
 * ------------------------------------------------------------------------- */
static const asset_entry_t *s_TestResolvedAsset;

void testStubAssetCatalogResolveWith(const asset_entry_t *entry)
{
    s_TestResolvedAsset = entry;
}

const asset_entry_t *assetCatalogResolve(const char *id)
{
    if (s_TestResolvedAsset && id &&
            strcmp(s_TestResolvedAsset->id, id) == 0) {
        return s_TestResolvedAsset;
    }
    return NULL;
}

void sysMemFree(void *ptr)
{
    free(ptr);
}

asset_entry_t *assetCatalogGetMutable(const char *id)
{
    (void)id;
    return NULL;
}

/* B-911 (c3848): weapon_graph_runtime.c's embedded-mesh ingest registers
 * custom weapon meshes into the catalog. pd-tests has no live catalog, so the
 * register stub returns NULL and the ingest takes its loud-skip path -- the
 * weapon-graph tests keep exercising the walk with the ingest inert. */
asset_entry_t *assetCatalogRegister(const char *id, asset_type_e type)
{
    (void)id;
    (void)type;
    return NULL;
}

void catalogSetPrimaryFile(asset_entry_t *entry, const char *path)
{
    (void)entry;
    (void)path;
}

const char *catalogIdByRuntime(asset_type_e type, s32 runtime_index)
{
    (void)type;
    (void)runtime_index;
    return NULL;
}

const char *catalogStageIdByStageTableIndex(s32 stage_table_index)
{
    (void)stage_table_index;
    return NULL;
}

const char *catalogStageIdBySoloStageIndex(s32 solo_stage_index)
{
    (void)solo_stage_index;
    return NULL;
}

const char *catalogStageIdByStagenum(s32 stagenum)
{
    (void)stagenum;
    return NULL;
}

const char *catalogGameModeIdByScenarioIndex(s32 scenario_index)
{
    (void)scenario_index;
    return NULL;
}

const char *catalogModelIdByModelnum(s32 modelnum)
{
    (void)modelnum;
    return NULL;
}

s32 catalogResolveModelByModelnum(s32 modelnum, catalog_model_result_t *out)
{
    (void)modelnum;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}

s32 catalogResolveModel(const char *id, catalog_model_result_t *out)
{
    (void)id;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}

s32 catalogResolveAudio(const char *id, catalog_audio_result_t *out)
{
    (void)id;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}

s32 catalogResolveWeapon(const char *id, catalog_weapon_result_t *out)
{
    (void)id;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}

asset_data_handle_t catalogGetModelHandle(s32 modelnum)
{
    asset_data_handle_t handle = ASSET_HANDLE_NULL_INIT;
    (void)modelnum;
    return handle;
}

s32 catalogGetModelFilenumByModelnum(s32 modelnum)
{
    (void)modelnum;
    return -1;
}

const char *catalogBodyIdByBodynum(s32 bodynum)
{
    (void)bodynum;
    return NULL;
}

const char *catalogHeadIdByHeadnum(s32 headnum)
{
    (void)headnum;
    return NULL;
}

const char *catalogIdBySourceFilenum(asset_type_e type, s32 source_filenum)
{
    (void)type;
    (void)source_filenum;
    return NULL;
}

asset_data_handle_t catalogHandleBySourceFilenum(asset_type_e type, s32 source_filenum)
{
    asset_data_handle_t handle = ASSET_HANDLE_NULL_INIT;
    (void)type;
    (void)source_filenum;
    return handle;
}

const char *catalogIdBySourceHandle(asset_type_e type, asset_data_handle_t handle)
{
    (void)type;
    (void)handle;
    return NULL;
}

/* -------------------------------------------------------------------------
 * Mod manager stubs -- no mods registered.
 * ------------------------------------------------------------------------- */
s32 modmgrGetCount(void)
{
    return 0;
}

modinfo_t *modmgrGetMod(s32 index)
{
    (void)index;
    return NULL;
}

/* -------------------------------------------------------------------------
 * Audio mod playlist stubs -- empty playlist.
 * ------------------------------------------------------------------------- */
s32 audioGetModPlaylistCount(void)
{
    return 0;
}

const char *audioGetModPlaylistEntry(s32 idx)
{
    (void)idx;
    return NULL;
}
