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
#include "game/explosions.h"
#include "game/smoke.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/propobj.h"
#include "effect_gameplay_runtime.h"
#include "effect_presentation_runtime.h"

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

static struct explosiontype s_TestExplosionProfiles[EXPLOSIONTYPE_BASE_COUNT];
static struct smoketype s_TestSmokeProfiles[SMOKETYPE_BASE_COUNT];

static s32 s_TestGameplayExplosionCapacity = 1;
static s32 s_TestGameplaySmokeCapacity = 1;
static s32 s_TestGameplayPropCapacity = 1;
static s32 s_TestGameplayAudioCapacity = 1;
static s32 s_TestGameplayExplosions;
static s32 s_TestGameplaySparks;
static s32 s_TestGameplaySmokes;
static s32 s_TestGameplaySounds;
static s32 s_TestGameplayLastExplosionType;
static s32 s_TestGameplayLastSparkType;
static s32 s_TestGameplayLastSmokeType;
static s32 s_TestGameplayLastSound;

void testStubEffectGameplayReset(void)
{
    s_TestGameplayExplosionCapacity = 1;
    s_TestGameplaySmokeCapacity = 1;
    s_TestGameplayPropCapacity = 1;
    s_TestGameplayAudioCapacity = 1;
    s_TestGameplayExplosions = 0;
    s_TestGameplaySparks = 0;
    s_TestGameplaySmokes = 0;
    s_TestGameplaySounds = 0;
    s_TestGameplayLastExplosionType = -1;
    s_TestGameplayLastSparkType = -1;
    s_TestGameplayLastSmokeType = -1;
    s_TestGameplayLastSound = -1;
    s_TestExplosionProfiles[EXPLOSIONTYPE_EYESPY].sound = 321;
}

void testStubEffectGameplayCapacity(s32 explosions, s32 smokes,
    s32 props, s32 sounds)
{
    s_TestGameplayExplosionCapacity = explosions;
    s_TestGameplaySmokeCapacity = smokes;
    s_TestGameplayPropCapacity = props;
    s_TestGameplayAudioCapacity = sounds;
}

s32 testStubEffectGameplayCount(s32 kind)
{
    if (kind == 1) return s_TestGameplayExplosions;
    if (kind == 2) return s_TestGameplaySparks;
    if (kind == 3) return s_TestGameplaySmokes;
    if (kind == 4) return s_TestGameplaySounds;
    return 0;
}

s32 testStubEffectGameplayLast(s32 kind)
{
    if (kind == 1) return s_TestGameplayLastExplosionType;
    if (kind == 2) return s_TestGameplayLastSparkType;
    if (kind == 3) return s_TestGameplayLastSmokeType;
    if (kind == 4) return s_TestGameplayLastSound;
    return -1;
}

s32 explosionsReserveCreateCount(s32 count)
{
    return count <= s_TestGameplayExplosionCapacity;
}

s32 smokesReserveCreateCount(s32 count)
{
    return count <= s_TestGameplaySmokeCapacity;
}

s32 propsReserveCreateCount(s32 count)
{
    return count <= s_TestGameplayPropCapacity;
}

s32 psReserveCreateCount(s32 count)
{
    return count <= s_TestGameplayAudioCapacity;
}

const struct explosiontype *explosionTypeFor(s32 type)
{
    if (type < 0 || type >= EXPLOSIONTYPE_BASE_COUNT) type = 0;
    return &s_TestExplosionProfiles[type];
}

bool explosionCreateComplexWithSound(struct prop *prop, struct coord *pos,
    RoomNum *rooms, s16 type, s32 playernum, s16 soundnum)
{
    (void)prop; (void)pos; (void)rooms; (void)playernum; (void)soundnum;
    s_TestGameplayExplosions++;
    s_TestGameplayLastExplosionType = type;
    return true;
}

void sparksCreate(s32 room, struct prop *prop, struct coord *pos,
    struct coord *arg3, struct coord *arg4, s32 type)
{
    (void)room; (void)prop; (void)pos; (void)arg3; (void)arg4;
    s_TestGameplaySparks++;
    s_TestGameplayLastSparkType = type;
}

struct smoke *smokeCreate(struct coord *pos, RoomNum *rooms, s16 type)
{
    (void)pos; (void)rooms;
    s_TestGameplaySmokes++;
    s_TestGameplayLastSmokeType = type;
    return (struct smoke *)&s_TestGameplaySmokes;
}

s16 psCreate(struct pschannel *channel, struct prop *prop, s16 soundnum,
    s16 padnum, s32 vol, u16 flags, u16 flags2, s32 type,
    struct coord *pos, f32 pitch, RoomNum *rooms, s32 room,
    f32 dist1, f32 dist2, f32 dist3)
{
    (void)channel; (void)prop; (void)padnum; (void)vol; (void)flags;
    (void)flags2; (void)type; (void)pos; (void)pitch; (void)rooms;
    (void)room; (void)dist1; (void)dist2; (void)dist3;
    s_TestGameplaySounds++;
    s_TestGameplayLastSound = soundnum;
    return 1;
}

bool propExplode(struct prop *prop, s32 explosiontype)
{
    (void)prop;
    s_TestGameplayExplosions++;
    s_TestGameplayLastExplosionType = explosiontype;
	return true;
}

s32 propResolveExplosionSpatial(struct prop *prop, struct coord *pos,
	RoomNum rooms[8])
{
	s32 i;
	if (!prop || !pos || !rooms) return 0;
	*pos = prop->pos;
	for (i = 0; i < 7 && prop->rooms[i] >= 0; i++) rooms[i] = prop->rooms[i];
	rooms[i] = -1;
	return rooms[0] >= 0;
}

bool propExplodeWithSound(struct prop *prop, s32 explosiontype, s16 soundnum)
{
	(void)soundnum;
	return propExplode(prop, explosiontype);
}

void explosionsSetProfileOverride(const struct explosiontype *rows, s32 count)
{
    if (rows && count == EXPLOSIONTYPE_BASE_COUNT) {
        memcpy(s_TestExplosionProfiles, rows, sizeof(s_TestExplosionProfiles));
    }
}

void explosionsClearProfileOverride(void)
{
    memset(s_TestExplosionProfiles, 0, sizeof(s_TestExplosionProfiles));
}

void smokesSetProfileOverride(const struct smoketype *rows, s32 count)
{
    if (rows && count == SMOKETYPE_BASE_COUNT) {
        memcpy(s_TestSmokeProfiles, rows, sizeof(s_TestSmokeProfiles));
    }
}

void smokesClearProfileOverride(void)
{
    memset(s_TestSmokeProfiles, 0, sizeof(s_TestSmokeProfiles));
}

f32 testStubEffectExplosionRangeH(s32 index)
{
    return index >= 0 && index < EXPLOSIONTYPE_BASE_COUNT
        ? s_TestExplosionProfiles[index].rangeh : 0;
}

s32 testStubEffectExplosionSmokeType(s32 index)
{
    return index >= 0 && index < EXPLOSIONTYPE_BASE_COUNT
        ? s_TestExplosionProfiles[index].smoketype : -1;
}

s32 testStubEffectSmokeDuration(s32 index)
{
    return index >= 0 && index < SMOKETYPE_BASE_COUNT
        ? s_TestSmokeProfiles[index].duration : 0;
}

s32 testStubEffectExplosionRow(s32 index, void *out, size_t size)
{
    if (!out || size != sizeof(struct explosiontype)
            || index < 0 || index >= EXPLOSIONTYPE_BASE_COUNT) return 0;
    memcpy(out, &s_TestExplosionProfiles[index], size);
    return 1;
}

s32 testStubEffectSparkRow(s32 index, void *out, size_t size)
{
    struct sparktype *row = sparkTypeFor(index);
    if (!out || !row || size != sizeof(*row)
            || index < 0 || index >= SPARKTYPE_BASE_COUNT) return 0;
    memcpy(out, row, size);
    return 1;
}

s32 testStubEffectSmokeRow(s32 index, void *out, size_t size)
{
    if (!out || size != sizeof(struct smoketype)
            || index < 0 || index >= SMOKETYPE_BASE_COUNT) return 0;
    memcpy(out, &s_TestSmokeProfiles[index], size);
    return 1;
}

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
static const asset_entry_t *s_TestResolvedAsset2;

void testStubAssetCatalogResolveWith(const asset_entry_t *entry)
{
    s_TestResolvedAsset = entry;
    s_TestResolvedAsset2 = NULL;
}

void testStubAssetCatalogResolvePair(const asset_entry_t *first,
        const asset_entry_t *second)
{
    s_TestResolvedAsset = first;
    s_TestResolvedAsset2 = second;
}

const asset_entry_t *assetCatalogResolve(const char *id)
{
    if (s_TestResolvedAsset && id &&
            strcmp(s_TestResolvedAsset->id, id) == 0) {
        return s_TestResolvedAsset;
    }
    if (s_TestResolvedAsset2 && id &&
            strcmp(s_TestResolvedAsset2->id, id) == 0) {
        return s_TestResolvedAsset2;
    }
    return NULL;
}

const asset_entry_t *assetCatalogResolveAny(const char *id)
{
    if (s_TestResolvedAsset && id &&
            strcmp(s_TestResolvedAsset->id, id) == 0) {
        return s_TestResolvedAsset;
    }
    if (s_TestResolvedAsset2 && id &&
            strcmp(s_TestResolvedAsset2->id, id) == 0) {
        return s_TestResolvedAsset2;
    }
    return NULL;
}

void sysMemFree(void *ptr)
{
    free(ptr);
}

asset_entry_t *assetCatalogGetMutable(const char *id)
{
    if (s_TestResolvedAsset && id &&
            strcmp(s_TestResolvedAsset->id, id) == 0) {
        return (asset_entry_t *)s_TestResolvedAsset;
    }
    if (s_TestResolvedAsset2 && id &&
            strcmp(s_TestResolvedAsset2->id, id) == 0) {
        return (asset_entry_t *)s_TestResolvedAsset2;
    }
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

static char s_TestEffectAudioId[CATALOG_ID_LEN];
static s32 s_TestEffectAudioCategory;
static s32 s_TestEffectAudioSoundId;

void testStubEffectAudio(const char *id, s32 category, s32 sound_id)
{
    snprintf(s_TestEffectAudioId, sizeof(s_TestEffectAudioId), "%s", id ? id : "");
    s_TestEffectAudioCategory = category;
    s_TestEffectAudioSoundId = sound_id;
}

s32 catalogResolveAudio(const char *id, catalog_audio_result_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
	if (!id || !out || strcmp(id, s_TestEffectAudioId) != 0) {
		return 0;
	}
	out->category = s_TestEffectAudioCategory;
	out->sound_id = s_TestEffectAudioSoundId;
	return 1;
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

/* The full GBI renderer is deliberately not linked into pd-tests. Mirror its
 * read-only particle eligibility seam so focused runtime tests still exercise
 * the real committed gameplay snapshots and presentation shader contract. */
size_t effectPresentationRenderableParticleCount(void)
{
    size_t renderable = 0;
    const size_t count = effectGameplayRuntimeParticleCount();

    for (size_t i = 0; i < count; i++) {
        effect_gameplay_particle_snapshot_t particle;

        if (effectGameplayRuntimeParticleSnapshot(i, &particle)
                && particle.texture_num >= 0
                && particle.texture_id[0]
                && effectPresentationShaderSupported("effect.particle",
                    particle.shader_id)) {
            renderable++;
        }
    }

    return renderable;
}
