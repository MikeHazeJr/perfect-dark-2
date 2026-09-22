#include <stdio.h>
#include <stdint.h>
#include <float.h>
#include <SDL.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_scanner.h"
#include "assetcatalog_weapon_slots.h"
#include "assetcatalog_model_slots.h"
#include "assetprovider.h"
#include "assetprovider_checkpoint.h"
#include "assetcatalog_anim_slots.h"
#include "assetcatalog_sound_slots.h"
#include "catalog_entry_snapshot.h"
#include "catalog_animation_generation.h"
#include "catalog_audio_generation.h"
#include "catalog_command_generation.h"
#include "catalog_texture_generation.h"
#include "catalog_model_generation.h"
#include "texture_source_runtime.h"
#include "assetcatalog_texture_slots.h"
#include "game/tex.h"
#include "game/texdecompress.h"
#include "modasset_compiler.h"
#include "audio.h"
#include "mod.h"
#include "data.h"
#include "lib/anim.h"
#include "sha256.h"
#include "modarchive.h"
#include "types.h"
#include "weapon_graph_runtime.h"
#include "bss.h"
#include "constants.h"
#include "fs.h"
#include "lib/lib_317f0.h"
#include "lib/snd.h"
#include "game/bondgun.h"
#include "loader_pool.h"
#include "system.h"
#include "weapon_nested_runtime_harness.h"

typedef struct harness_deps {
    char (*ids)[CATALOG_ID_LEN];
    asset_type_e *types;
    size_t count;
    size_t capacity;
    s32 invalid;
} harness_deps_t;

static void harnessCollectDep(const char *id, void *userdata)
{
    harness_deps_t *deps = (harness_deps_t *)userdata;
    const asset_entry_t *entry;
    if (!deps || !id || !id[0]) {
        if (deps) deps->invalid = 1;
        return;
    }
    if (deps->count == deps->capacity) {
        size_t next = deps->capacity ? deps->capacity * 2 : 16;
        if (next < deps->count + 1
                || next > SIZE_MAX / sizeof(*deps->ids)
                || next > SIZE_MAX / sizeof(*deps->types)) {
            deps->invalid = 1;
            return;
        }
        char (*ids)[CATALOG_ID_LEN] = malloc(next * sizeof(*deps->ids));
        asset_type_e *types = malloc(next * sizeof(*deps->types));
        if (!ids || !types) {
            free(ids);
            free(types);
            deps->invalid = 1;
            return;
        }
        if (deps->count) {
            memcpy(ids, deps->ids, deps->count * sizeof(*deps->ids));
            memcpy(types, deps->types, deps->count * sizeof(*deps->types));
        }
        free(deps->ids);
        free(deps->types);
        deps->ids = ids;
        deps->types = types;
        deps->capacity = next;
    }
    entry = assetCatalogResolve(id);
    if (!entry) {
        deps->invalid = 1;
        return;
    }
    strncpy(deps->ids[deps->count], id, CATALOG_ID_LEN - 1);
    deps->ids[deps->count][CATALOG_ID_LEN - 1] = '\0';
    deps->types[deps->count] = entry->type;
    deps->count++;
}

static s32 harnessFind(const harness_deps_t *deps, const char *id,
                       asset_type_e type)
{
    if (!deps || !id) return -1;
    for (size_t i = 0; i < deps->count; i++) {
        if (deps->types[i] == type && strcmp(deps->ids[i], id) == 0) return i;
    }
    return -1;
}

static asset_entry_t *harnessRegisterOwner(const char *owner,
                                           const char *archive)
{
    const asset_entry_t *donor = assetCatalogResolve("base:ar34");
    if (!donor || donor->type != ASSET_WEAPON || donor->runtime_index < 0)
        return NULL;
    asset_entry_t *entry = assetCatalogRegisterWeapon(owner, donor->mp_index,
        "Nested runtime harness", "", 0);
    if (!entry) return NULL;
    donor = assetCatalogResolve("base:ar34");
    if (!donor) {
        assetCatalogUnregister(owner);
        return NULL;
    }
    /* The disposable alias borrows AR34's fully bound public-member metadata
     * so the generic runtime adapter can activate. Its primary graph source
     * remains the fixture archive below and the process exits after proof. */
    entry->ext.weapon = donor->ext.weapon;
    entry->runtime_index = donor->runtime_index;
    entry->mp_index = donor->mp_index;
    entry->bundled = 0;
    entry->enabled = 1;
    entry->temporary = 1;
    catalogSetPrimaryFile(entry, archive);
    return entry;
}

static s32 harnessDropDependency(const char *id, asset_type_e type, void *userdata)
{
    (void)id; (void)type; (void)userdata;
    return 0;
}

static s32 harnessCleanup(const char *owner, const harness_deps_t *deps)
{
    s32 ok = 1;
    const asset_entry_t *root = assetCatalogResolve(owner);
    /* Retire payloads while every typed edge still resolves. The nested
     * registration order is not a valid dependency teardown order. */
    if (root && !catalogDeactivateTypedAsset(root->type, owner)) ok = 0;
    if (deps) {
        for (size_t i = deps->count; i-- > 0; ) {
            const asset_entry_t *entry = assetCatalogResolve(deps->ids[i]);
            if (entry && !catalogDeactivateTypedAsset(entry->type, deps->ids[i])) ok = 0;
        }
    }
    /* All fixture rows are now inactive. Detach the complete fixture graph
     * before deleting rows so shared/deep children never leave dangling edges. */
    if (ok) {
        catalogDepPruneOwner(owner, harnessDropDependency, NULL);
        for (size_t i = 0; deps && i < deps->count; i++) {
            catalogDepPruneOwner(deps->ids[i], harnessDropDependency, NULL);
        }
        for (size_t i = 0; deps && i < deps->count; i++) {
            if (assetCatalogResolve(deps->ids[i]) && !assetCatalogUnregister(deps->ids[i])) ok = 0;
        }
        if (assetCatalogResolve(owner) && !assetCatalogUnregister(owner)) ok = 0;
    }
    if (!ok) sysLogPrintf(LOG_WARNING,
        "PDWEAPON.NESTED.HARNESS.FAIL: mode=cleanup owner=%s", owner);
    if (deps) {
        free(deps->ids);
        free(deps->types);
    }
    return ok;
}

static s32 harnessCapacityAccept(const char *owner, const char *archive,
                                 const char *expected_text,
                                 const char *effect_count_text,
                                 const char *effect_dep_text)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    harness_deps_t effect_deps = {0};
    const long expected = strtol(expected_text ? expected_text : "", NULL, 10);
    const long expected_effect_deps = strtol(
        effect_dep_text ? effect_dep_text : "", NULL, 10);
    const long expected_effects = strtol(
        effect_count_text ? effect_count_text : "", NULL, 10);
    const s32 dep_baseline = catalogDepCount();
    size_t effect_count = 0;
    s32 new_runtime_weapon_id = -1;
    s32 new_mp_weapon_id = -1;
    s32 ok = 0;
    if (expected <= 64 || expected > INT32_MAX
            || expected_effects < 2 || expected_effects > INT32_MAX
            || expected_effect_deps <= 64 || expected_effect_deps > INT32_MAX
            ) goto done;
    /* This runs after the complete base catalog has booted. A genuinely new
     * creator weapon must receive its own pair, rather than borrowing a donor
     * or failing because base-only inventory rows consumed custom capacity. */
    if (!assetCatalogResolveWeaponPrivateSlots("capacity:weapon_new", -1, 0,
            &new_runtime_weapon_id, &new_mp_weapon_id)
            || new_runtime_weapon_id < WEAPON_CUSTOM_START
            || new_runtime_weapon_id >= WEAPON_CUSTOM_END
            || new_mp_weapon_id < MPWEAPON_CUSTOM_START
            || new_mp_weapon_id >= MPWEAPON_CUSTOM_END) goto done;
    sysLogPrintf(LOG_NOTE,
        "PDWEAPON.NESTED.WEAPON_SLOT: id=capacity:weapon_new runtime=%d mp=%d result=PASS",
        new_runtime_weapon_id, new_mp_weapon_id);

    if (!harnessRegisterOwner(owner, archive)) goto done;
    if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
            err, sizeof(err)) != (s32)expected) goto done;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    for (size_t i = 0; i < deps.count; i++) {
        if (deps.types[i] != ASSET_EFFECT) continue;
        effect_count++;
        catalogDepForEach(deps.ids[i], harnessCollectDep, &effect_deps);
    }
    if (deps.invalid || effect_deps.invalid
            || deps.count != (size_t)expected
            || effect_count != (size_t)expected_effects
            || effect_deps.count != (size_t)expected_effect_deps) goto done;

    /* Rebuild the production reverse index after dynamic registration, then
     * play all 70 creator SFX through sndStart's normal catalog/file route. */
    {
        s32 high_soundnum = -1;
        catalogLoadInit();
        for (s32 i = 0; i < 70; i++) {
            char sfx_id[CATALOG_ID_LEN];
            const asset_entry_t *sfx;
            struct sndstate *state;
            CatalogResolveResult route;
            snprintf(sfx_id, sizeof(sfx_id), "capacity:sfx_%03d", i);
            sfx = assetCatalogResolve(sfx_id);
            if (!sfx || sfx->type != ASSET_AUDIO
                    || sfx->ext.audio.category != AUDIO_CAT_SFX
                    || sfx->source_soundnum < SND_CUSTOM_START
                    || sfx->source_soundnum >= SND_CUSTOM_END) goto done;
            route = catalogResolveSound(sfx->source_soundnum);
            if (i == 0 || i == 69) {
                sysLogPrintf(LOG_NOTE,
                    "PDWEAPON.NESTED.PLAYBACK_ROUTE: id=%s sound=%d disabled=%d catalog=%d override=%d blocked=%d path=%s",
                    sfx_id, sfx->source_soundnum, sndIsDisabled() ? 1 : 0,
                    route.catalog_id, route.is_mod_override,
                    route.source_only_blocked,
                    route.path ? route.path : "(null)");
            }
            if (sndIsDisabled() || route.catalog_id < 0
                    || !route.is_mod_override || !route.path
                    || route.source_only_blocked) goto done;
            state = sndStart(var80095200, (s16)sfx->source_soundnum,
                NULL, -1, -1, -1.0f, -1, -1);
            if (!state) goto done;
            audioStop(state);
            high_soundnum = sfx->source_soundnum;
        }
        if (high_soundnum < SND_CUSTOM_START + 64) goto done;
        sysLogPrintf(LOG_NOTE,
            "PDWEAPON.NESTED.PLAYBACK: count=70 high_id=capacity:sfx_069 high_sound=%d result=PASS",
            high_soundnum);
    }
    ok = 1;
done:
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "capacity assertion failed");
    }
    free(effect_deps.ids);
    free(effect_deps.types);
    if (!harnessCleanup(owner, &deps)) ok = 0;
    if (catalogDepCount() != dep_baseline) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity owner=%s leaked_edges=%d",
            owner, catalogDepCount() - dep_baseline);
        ok = 0;
    }
    return ok;
}

static s32 harnessCapacityReject(const char *owner, const char *archive,
                                 const char *id_prefix,
                                 const char *count_text,
                                 const char *effect_id)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    const long expected = strtol(count_text ? count_text : "", NULL, 10);
    const s32 dep_baseline = catalogDepCount();
    s32 ok = expected > 64 && expected <= INT32_MAX && id_prefix
        && id_prefix[0] && effect_id && effect_id[0];
    if (!ok || !harnessRegisterOwner(owner, archive)) return 0;
    ok = assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
        err, sizeof(err)) < 0;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    if (deps.count != 0 || deps.invalid) ok = 0;
    for (long i = 0; ok && i < expected; i++) {
        char id[CATALOG_ID_LEN];
        snprintf(id, sizeof(id), "%s%03ld", id_prefix, i);
        if (assetCatalogResolve(id)) ok = 0;
    }
    if (assetCatalogResolve(effect_id)) ok = 0;
    if (!harnessCleanup(owner, &deps)) ok = 0;
    if (catalogDepCount() != dep_baseline) ok = 0;
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity_reject owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "rollback assertion failed");
    }
    return ok;
}

static s32 harnessChildrenAreLoaded(const harness_deps_t *deps, s32 min_ref)
{
    for (size_t i = 0; deps && i < deps->count; i++) {
        const asset_entry_t *entry = assetCatalogResolve(deps->ids[i]);
        if (!entry || entry->load_state < ASSET_STATE_LOADED
                || entry->ref_count < min_ref) return 0;
    }
    return deps && !deps->invalid;
}

static s32 harnessChildrenAreReleased(const harness_deps_t *deps)
{
    for (size_t i = 0; deps && i < deps->count; i++) {
        const asset_entry_t *entry = assetCatalogResolve(deps->ids[i]);
        if (!entry || entry->ref_count != 0 || entry->loaded_data != NULL
                || entry->load_state >= ASSET_STATE_LOADED) return 0;
    }
    return deps && !deps->invalid;
}

static s32 harnessAccept(const char *owner, const char *archive,
                         const char *audio_id, const char *anim_id)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    s32 ok = 0;
    if (!harnessRegisterOwner(owner, archive)) goto done;
    if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
            err, sizeof(err)) < 0) goto done;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    s32 audio_pos = harnessFind(&deps, audio_id, ASSET_AUDIO);
    s32 anim_pos = harnessFind(&deps, anim_id, ASSET_ANIMATION);
    if (deps.invalid || audio_pos < 0 || anim_pos < 0 || audio_pos >= anim_pos) goto done;

    /* Direct production lifecycle: parent owns one child reference. */
    if (!catalogLoadTypedAsset(ASSET_WEAPON, owner)
            || !harnessChildrenAreLoaded(&deps, 1)) goto done;
    catalogReleaseTypedAsset(ASSET_WEAPON, owner);
    if (!harnessChildrenAreReleased(&deps)) goto done;

    /* Manifest-style lifecycle: explicit child references coexist with the
     * parent's own closure. Parent release leaves one, manifest release frees. */
    for (size_t i = 0; i < deps.count; i++) {
        if (!catalogLoadTypedAsset(deps.types[i], deps.ids[i])) goto done;
    }
    if (!catalogLoadTypedAsset(ASSET_WEAPON, owner)
            || !harnessChildrenAreLoaded(&deps, 2)) goto done;
    catalogReleaseTypedAsset(ASSET_WEAPON, owner);
    if (!harnessChildrenAreLoaded(&deps, 1)) goto done;
    for (size_t i = deps.count; i-- > 0; ) {
        catalogReleaseTypedAsset(deps.types[i], deps.ids[i]);
    }
    if (!harnessChildrenAreReleased(&deps)) goto done;
    ok = 1;
done:
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=accept owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "runtime assertion failed");
    }
    if (!harnessCleanup(owner, &deps)) ok = 0;
    return ok;
}

static s32 harnessReject(const char *owner, const char *archive,
                         char *absent_csv)
{
    char err[512] = {0};
    harness_deps_t deps = {0};
    s32 ok;
    if (!harnessRegisterOwner(owner, archive)) return 0;
    ok = assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0,
        err, sizeof(err)) < 0;
    catalogDepForEach(owner, harnessCollectDep, &deps);
    if (deps.count != 0 || deps.invalid) ok = 0;
    for (char *id = absent_csv; id && *id;) {
        char *next = strchr(id, ',');
        if (next) *next++ = '\0';
        if (id[0] && assetCatalogResolve(id)) ok = 0;
        id = next;
    }
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=reject owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "rollback assertion failed");
    }
    if (!harnessCleanup(owner, &deps)) ok = 0;
    return ok;
}

/* Production-backed source and native-record proof. This does not claim a
 * rendered frame, a fired projectile, or graph control-flow execution. */
static s32 harnessMeshVertices(const struct modeldef *model, s32 expected_edge)
{
	if (!model || !model->rootnode) return 0;
	for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
		if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
		const struct modelrodata_dl *dl = &node->rodata->dl;
		if (!dl->vertices || dl->numvertices != 3) continue;
		s32 max_x = 0, max_y = 0;
		for (s32 i = 0; i < dl->numvertices; i++) {
			if (dl->vertices[i].x > max_x) max_x = dl->vertices[i].x;
			if (dl->vertices[i].y > max_y) max_y = dl->vertices[i].y;
		}
		return max_x == expected_edge && max_y == expected_edge;
	}
	return 0;
}

static s32 harnessWriteBytes(const char *path, const void *bytes, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    s32 ok = fwrite(bytes, 1, size, file) == size;
    if (fclose(file)) ok = 0;
    return ok;
}

static s32 harnessGenerationFrame(s32 slot, f32 *translation_y)
{
    struct coord rotation, translation, scale;
    animLoadHeader((s16)slot);
    u8 frame = animLoadFrame((s16)slot, 1);
    if (frame >= ANIM_FRAME_CACHE_SIZE) return 0;
    animGetRotTranslateScale(0, 0, NULL, (s16)slot, frame, &rotation, &translation, &scale);
    *translation_y = translation.y;
    return 1;
}

typedef struct harness_model_inputs {
    char paths[4][FS_MAXPATH];
    u8 *bytes[4];
    u32 sizes[4];
    s32 reads, private_reads, textures, reject_texture;
    mod_texture_rgba32_source_t rgba;
    struct tex texture;
    struct texpool pool;
} harness_model_inputs_t;
static const char *harnessModelPath(const char *path)
{
    while (path[0] == '.' && path[1] == '/') path += 2;
    return path;
}
static void *harnessModelRead(void *context, const char *path, u32 *size)
{
    harness_model_inputs_t *inputs = context;
    *size = 0;
    if (strstr(path, "_meta/")) ++inputs->private_reads;
    for (size_t i = 0; i < 4; ++i) {
        if (strcmp(harnessModelPath(path), harnessModelPath(inputs->paths[i]))) continue;
        u8 *copy = malloc((size_t)inputs->sizes[i] + 1);
        if (!copy) return NULL;
        memcpy(copy, inputs->bytes[i], inputs->sizes[i]); copy[inputs->sizes[i]] = 0;
        *size = inputs->sizes[i]; ++inputs->reads;
        return copy;
    }
    return NULL; /* Snapshot records the optional sidecars as absent. */
}
static s32 harnessModelTexture(void *context, const char *source_path,
    const char *reference, s32 is_catalog, s32 secondary)
{
    harness_model_inputs_t *inputs = context;
    char error[160];
    if (inputs->reject_texture || is_catalog || secondary || !source_path
            || strcmp(reference, "sample.tga") || inputs->textures) return -1;
    if (modTextureDecodeRgba32Source(inputs->bytes[3], inputs->sizes[3],
            &inputs->rgba, error, sizeof(error)) <= 0) return -1;
    ++inputs->textures;
    inputs->texture.texturenum = 0;
    inputs->texture.data = inputs->rgba.pixels;
    inputs->texture.width = inputs->rgba.width;
    inputs->texture.height = inputs->rgba.height;
    inputs->texture.numlods = 1;
    inputs->texture.gbiformat = G_IM_FMT_RGBA;
    inputs->texture.depth = G_IM_SIZ_32b;
    inputs->pool.start = inputs->rgba.pixels;
    inputs->pool.leftpos = inputs->rgba.pixels + inputs->rgba.data_size;
    inputs->pool.rightpos = &inputs->texture;
    inputs->pool.end = &inputs->texture + 1;
    return 0;
}
static s32 harnessModelHasTexture(const struct modeldef *model, const void *pixels)
{
    if (!model || !model->rootnode) return 0;
    for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const Gfx *gdl = node->rodata->dl.opagdl;
        u32 bytes = 0;
        if (!gdl || !modAssetCompilerGeneratedGdlBytesRemaining(gdl, &bytes)) return 0;
        for (u32 i = 0; i < bytes / sizeof(Gfx); ++i) {
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_ENDDL) break;
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_SETTIMG
                    && gdl[i].words.w1 == (uintptr_t)pixels) return 1;
        }
    }
    return 0;
}
static const u32 *harnessModelFirstPixels(const struct modeldef *model)
{
    if (!model || !model->rootnode) return NULL;
    for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const Gfx *gdl = node->rodata->dl.opagdl;
        u32 bytes = 0;
        if (!gdl || !modAssetCompilerGeneratedGdlBytesRemaining(gdl, &bytes)) return NULL;
        for (u32 i = 0; i < bytes / sizeof(Gfx); ++i) {
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_ENDDL) break;
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_SETTIMG)
                return (const u32 *)(uintptr_t)gdl[i].words.w1;
        }
    }
    return NULL;
}
static s32 harnessModelInputs(const char *id, const char *folder)
{
    const char *names[] = {"model.obj", "model.mtl", "mesh.ini", "sample.tga"};
    harness_model_inputs_t first = {0}, second = {0}, rejected = {0};
    struct modeldef *a = NULL, *b = NULL, *bad = NULL;
    asset_entry_t *entry = calloc(1, sizeof(*entry));
    u8 *edit = NULL;
    const char *step = "capture";
    s32 ok = 0;
    if (!entry) goto done;
    strncpy(entry->id, id, sizeof(entry->id) - 1); entry->type = ASSET_MODEL;
    for (size_t i = 0; i < 4; ++i) {
        if (snprintf(first.paths[i], sizeof(first.paths[i]), "%s/%s", folder, names[i]) >= sizeof(first.paths[i])) goto done;
        first.bytes[i] = fsFileLoad(first.paths[i], &first.sizes[i]);
        if (!first.bytes[i] || !first.sizes[i]) goto done;
        strcpy(second.paths[i], first.paths[i]);
    }
    /* Edit disk AFTER capture. Both geometry/descriptor and image callbacks
     * must consume their captured bytes, with no hidden file reopen. */
    for (size_t i = 0; i < 4; ++i) {
        edit = malloc((size_t)first.sizes[i] + 1);
        if (!edit) goto done;
        memcpy(edit, first.bytes[i], first.sizes[i]); edit[first.sizes[i]] = 0;
        if (i == 0) {
            char *x = strstr((char *)edit, "v 2 0 0"), *y = strstr((char *)edit, "v 0 2 0");
            if (!x || !y) goto done;
            x[2] = '7'; y[4] = '7';
        }
        if (i == 2) {
            char *scale = strstr((char *)edit, "model_scale = 2");
            if (!scale) goto done;
            scale[strlen("model_scale = ")] = '3';
        }
        if (i == 3) {
            if (first.sizes[i] != 34) goto done;
            for (u32 c = 18; c < 34; c += 4) { edit[c] = 255; edit[c + 2] = 0; }
        }
        if (!harnessWriteBytes(first.paths[i], edit, first.sizes[i])) goto done;
        free(edit); edit = NULL;
        second.bytes[i] = fsFileLoad(second.paths[i], &second.sizes[i]);
        if (!second.bytes[i] || !second.sizes[i]) goto done;
    }
    step = "captured_compiler";
    modasset_model_inputs_t inputs = {&first, harnessModelRead, harnessModelTexture, &first.pool};
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, first.paths[0], &a) <= 0 || !a
            || !harnessMeshVertices(a, 2) || a->scale != 2.0f
            || a->skel != modAssetCompilerSkeletonForSymbol("SKEL_BASIC")
            || first.private_reads || first.reads < 3 || first.textures != 1
            || ((u32 *)first.rgba.pixels)[0] != 0xff0000ffu
            || !harnessModelHasTexture(a, first.rgba.pixels)) goto done;
    step = "edited_compiler";
    inputs.context = &second; inputs.texture_pool = &second.pool;
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, second.paths[0], &b) <= 0 || !b
            || !harnessMeshVertices(b, 7) || b->scale != 3.0f || second.private_reads
            || second.textures != 1 || ((u32 *)second.rgba.pixels)[0] != 0x0000ffffu
            || !harnessModelHasTexture(b, second.rgba.pixels)
            || !harnessMeshVertices(a, 2) || a->scale != 2.0f) goto done;
    step = "texture_reject";
    /* A source resolver failure cannot silently emit an untextured model. */
    memcpy(rejected.paths, first.paths, sizeof(first.paths));
    memcpy(rejected.bytes, first.bytes, sizeof(first.bytes));
    memcpy(rejected.sizes, first.sizes, sizeof(first.sizes));
    rejected.reject_texture = 1;
    inputs.context = &rejected; inputs.texture_pool = &rejected.pool;
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, entry, first.paths[0], &bad) >= 0 || bad) goto done;
    mod_texture_rgba32_source_t invalid;
    char error[128];
    if (modTextureDecodeRgba32Source("bad", 3, &invalid, error, sizeof(error)) >= 0 || invalid.pixels) goto done;
    ok = 1;
done:
    modAssetCompilerFreeModeldef(a); modAssetCompilerFreeModeldef(b); modAssetCompilerFreeModeldef(bad);
    modTextureFreeRgba32Source(&first.rgba); modTextureFreeRgba32Source(&second.rgba);
    for (size_t i = 0; i < 4; ++i) {
        if (first.bytes[i] && !harnessWriteBytes(first.paths[i], first.bytes[i], first.sizes[i])) ok = 0;
        free(first.bytes[i]); free(second.bytes[i]);
    }
    free(edit); free(entry);
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODEL.INPUTS.HARNESS: step=%s result=%s reads=%d,%d textures=%d,%d private_reads=%d,%d",
        step, ok ? "PASS" : "FAIL", first.reads, second.reads, first.textures, second.textures,
        first.private_reads, second.private_reads);
    return ok;
}



typedef struct harness_texture_model {
    harness_model_inputs_t *source;
    catalog_texture_generation_t *texture;
    s32 texture_calls;
} harness_texture_model_t;
static void *harnessTextureModelRead(void *context, const char *path, u32 *size)
{
    return harnessModelRead(((harness_texture_model_t *)context)->source, path, size);
}
static s32 harnessTextureModelBind(void *context, const char *source_path,
    const char *reference, s32 is_catalog, s32 secondary)
{
    harness_texture_model_t *model = context;
    if (!source_path || is_catalog || secondary || strcmp(reference, "sample.tga")) return -1;
    ++model->texture_calls;
    return catalogTextureGenerationSlot(model->texture);
}
static s32 harnessModelHasTile(const struct modeldef *model, u32 tmem, u32 mask_s, u32 mask_t)
{
    if (!model || !model->rootnode) return 0;
    for (const struct modelnode *node = model->rootnode->child; node; node = node->next) {
        if ((node->type & 0xff) != MODELNODETYPE_DL) continue;
        const Gfx *gdl = node->rodata->dl.opagdl;
        u32 bytes = 0;
        if (!gdl || !modAssetCompilerGeneratedGdlBytesRemaining(gdl, &bytes)) return 0;
        for (u32 i = 0; i < bytes / sizeof(Gfx); ++i) {
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_ENDDL) break;
            if ((u8)(gdl[i].words.w0 >> 24) == (u8)G_SETTILE
                    && ((gdl[i].words.w1 >> 24) & 7) == 0
                    && (gdl[i].words.w0 & 0x1ff) == tmem
                    && ((gdl[i].words.w1 >> 4) & 15) == mask_s
                    && ((gdl[i].words.w1 >> 14) & 15) == mask_t) return 1;
        }
    }
    return 0;
}
static s32 harnessModelGeneration(const char *id, const char *folder)
{
    char source[FS_MAXPATH], image[FS_MAXPATH], error[256] = {0}, prior_hash[65] = {0};
    u32 image_size = 0;
    u8 *original = NULL, *edited = NULL;
    asset_entry_t *entry = calloc(1, sizeof(*entry));
    catalog_model_generation_t *first = NULL, *alias = NULL, *second = NULL, *rejected = NULL;
    struct modeldef *old_model = NULL, *new_model = NULL;
    const u32 *old_pixels = NULL, *new_pixels = NULL;
    file_provider_checkpoint_t provider;
    const char *step = "source";
    s32 checkpoint = fileProviderCheckpointCreate(&provider);
    s32 registered = 0, stage_loaded = 0, ok = 0;
    if (!checkpoint || !entry || assetCatalogResolveAny(id) || strlen(id) >= sizeof(entry->id)
            || snprintf(source, sizeof(source), "%s/model.obj", folder) >= sizeof(source)
            || snprintf(image, sizeof(image), "%s/sample.tga", folder) >= sizeof(image)) goto done;
    strcpy(entry->id, id); entry->type = ASSET_MODEL;
    original = fsFileLoad(image, &image_size);
    if (!original || image_size != 34) goto done;
    edited = malloc(image_size);
    if (!edited) goto done;
    memcpy(edited, original, image_size);
    step = "dedup";
    first = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    alias = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    old_model = catalogModelGenerationModeldef(first);
    old_pixels = harnessModelFirstPixels(old_model);
    if (!first || alias != first || !old_model || !old_pixels
            || old_pixels[0] != 0xff0000ffu
            || catalogModelGenerationForModeldef(old_model) != first) goto done;
    strcpy(prior_hash, catalogModelGenerationHash(first));
    step = "edit";
    for (u32 i = 18; i < 34; i += 4) { edited[i] = 255; edited[i + 2] = 0; }
    if (!harnessWriteBytes(image, edited, image_size)) goto done;
    second = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    new_model = catalogModelGenerationModeldef(second);
    new_pixels = harnessModelFirstPixels(new_model);
    if (!second || second == first || new_model == old_model || !new_pixels
            || !strcmp(prior_hash, catalogModelGenerationHash(second))
            || new_pixels[0] != 0x0000ffffu || old_pixels[0] != 0xff0000ffu) goto done;
    step = "ordinary_catalog";
    asset_entry_t *catalog_entry = assetCatalogRegister(id, ASSET_MODEL);
    if (!catalog_entry) goto done;
    registered = 1;
    catalogSetPrimaryFile(catalog_entry, source);
    if (!catalogLoadStageAsset(ASSET_MODEL, id)) goto done;
    stage_loaded = 1;
    if (catalogGetLoadedModeldef(id) != new_model
            || catalogModelGenerationForModeldef(new_model) != second
            || new_pixels[0] != 0x0000ffffu) goto done;
    catalogReleaseStageAsset(ASSET_MODEL, id); stage_loaded = 0;
    if (catalogGetLoadedModeldef(id)
            || catalogModelGenerationForModeldef(new_model) != second) goto done;
    step = "rejection";
    if (!harnessWriteBytes(image, "bad", 3)) goto done;
    if (catalogLoadStageAsset(ASSET_MODEL, id) || catalogGetLoadedModeldef(id)) goto done;
    rejected = catalogModelGenerationAcquireSource(entry, source, error, sizeof(error));
    if (rejected || !error[0] || old_pixels[0] != 0xff0000ffu
            || new_pixels[0] != 0x0000ffffu) goto done;
    step = "retirement";
    catalogModelGenerationRelease(alias); alias = NULL;
    catalogModelGenerationRelease(first); first = NULL;
    if (catalogModelGenerationForModeldef(old_model)
            || catalogModelGenerationForModeldef(new_model) != second
            || new_pixels[0] != 0x0000ffffu) goto done;
    ok = 1;
done:
    if (stage_loaded) catalogReleaseStageAsset(ASSET_MODEL, id);
    if (registered && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    catalogModelGenerationRelease(alias);
    catalogModelGenerationRelease(first);
    catalogModelGenerationRelease(second);
    catalogModelGenerationRelease(rejected);
    if (original && !harnessWriteBytes(image, original, image_size)) ok = 0;
    free(original); free(edited); free(entry);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "MODEL.GENERATION.HARNESS: step=%s result=%s error=%s",
        step, ok ? "PASS" : "FAIL", error);
    return ok;
}

static s32 harnessTextureGeneration(const char *id, const char *folder)
{
    const char *names[] = {"model.obj", "model.mtl", "mesh.ini", "sample.tga"};
    harness_model_inputs_t sources = {0};
    char descriptor[FS_MAXPATH], error[256] = {0}, first_hash[65] = {0};
    char *ini = NULL, *ini_edit = NULL;
    u8 *image_edit = NULL;
    u32 ini_size = 0;
    catalog_texture_generation_t *first = NULL, *alias = NULL, *second = NULL,
        *third = NULL, *reused = NULL, *rejected = NULL;
    struct modeldef *model_a = NULL, *model_b = NULL;
    asset_entry_t *model_entry = calloc(1, sizeof(*model_entry));
    file_provider_checkpoint_t provider;
    void *slots = assetCatalogSnapshotCustomTextureSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), registered = 0, ok = 0;
    s32 first_slot = -1, second_slot = -1, third_slot = -1, changed_table = 0;
    struct texture saved_definition = {0};
    const char *step = "source";
    if (!model_entry || !slots || !checkpoint || assetCatalogResolveAny(id)) goto done;
    strcpy(model_entry->id, "modelinputs:mesh"); model_entry->type = ASSET_MODEL;
    if (snprintf(descriptor, sizeof(descriptor), "%s/texture.ini", folder) >= sizeof(descriptor)) goto done;
    ini = fsFileLoad(descriptor, &ini_size);
    if (!ini || !ini_size) goto done;
    ini_edit = malloc((size_t)ini_size + 1);
    if (!ini_edit) goto done;
    memcpy(ini_edit, ini, ini_size); ini_edit[ini_size] = 0;
    for (size_t i = 0; i < 4; ++i) {
        if (snprintf(sources.paths[i], sizeof(sources.paths[i]), "%s/%s", folder, names[i]) >= sizeof(sources.paths[i])) goto done;
        sources.bytes[i] = fsFileLoad(sources.paths[i], &sources.sizes[i]);
        if (!sources.bytes[i] || !sources.sizes[i]) goto done;
    }
    if (sources.sizes[3] != 34) goto done;
    image_edit = malloc(sources.sizes[3]);
    if (!image_edit) goto done;
    memcpy(image_edit, sources.bytes[3], sources.sizes[3]);
    asset_entry_t *entry = assetCatalogRegisterTexture(id, -1, 2, 2, G_IM_FMT_RGBA, sources.paths[3]);
    if (!entry) goto done;
    registered = 1; strcpy(entry->descriptor_path, descriptor);
    catalogSetPrimaryFile(entry, sources.paths[3]);
    step = "acquire";
    first = catalogTextureGenerationAcquire(id, error, sizeof(error));
    alias = catalogTextureGenerationAcquire(id, error, sizeof(error));
    if (!first || alias != first) goto done;
    first_slot = catalogTextureGenerationSlot(first);
    strcpy(first_hash, catalogTextureGenerationHash(first));
    struct tex *first_native = catalogTextureGenerationTexture(first);
    if (first_slot < TEXTURE_CUSTOM_START || *(s16 *)(first_native->data - 8) != first_slot
            || ((u32 *)first_native->data)[0] != 0xff0000ffu
            || texGetDefinition(first_slot)->surfacetype != SURFACETYPE_WOOD
            || texGetDefinition(first_slot)->soundsurfacetype != SURFACETYPE_METAL) goto done;
    step = "image_edit";
    for (u32 i = 18; i < 34; i += 4) { image_edit[i] = 255; image_edit[i + 2] = 0; }
    if (!harnessWriteBytes(sources.paths[3], image_edit, sources.sizes[3])) goto done;
    second = catalogTextureGenerationAcquire(id, error, sizeof(error));
    if (!second || second == first || !strcmp(first_hash, catalogTextureGenerationHash(second))) goto done;
    second_slot = catalogTextureGenerationSlot(second);
    if (second_slot == first_slot || ((u32 *)catalogTextureGenerationTexture(second)->data)[0] != 0x0000ffffu) goto done;
    step = "properties_edit";
    char *surface = strstr(ini_edit, "surface_type = wood");
    char *sound = strstr(ini_edit, "sound_surface_type = metal");
    char *offset = strstr(ini_edit, "tile_column_offset = 1");
    if (!surface || !sound || !offset) goto done;
    memcpy(surface + strlen("surface_type = "), "snow", 4);
    memcpy(sound + strlen("sound_surface_type = "), "glass", 5);
    offset[strlen("tile_column_offset = ")] = '2';
    if (!harnessWriteBytes(descriptor, ini_edit, ini_size)) goto done;
    third = catalogTextureGenerationAcquire(id, error, sizeof(error));
    if (!third || third == second || !strcmp(catalogTextureGenerationHash(second), catalogTextureGenerationHash(third))) goto done;
    third_slot = catalogTextureGenerationSlot(third);
    if (texGetDefinition(third_slot)->surfacetype != SURFACETYPE_SNOW
            || texGetDefinition(third_slot)->soundsurfacetype != SURFACETYPE_GLASS
            || texGetDefinition(first_slot)->surfacetype != SURFACETYPE_WOOD
            || texGetDefinition(second_slot)->surfacetype != SURFACETYPE_WOOD) goto done;
    step = "reject";
    s32 available = assetCatalogReserveTextureGenerationSlot();
    if (available < 0) goto done;
    assetCatalogReleaseTextureGenerationSlot(available);
    rejected = catalogTextureGenerationAcquireSource(id, "bad", 3, ini, ini_size, error, sizeof(error));
    if (rejected) goto done;
    char *mask = strstr(ini_edit, "mask_s_reduction = 1");
    if (!mask) goto done;
    mask[strlen("mask_s_reduction = ")] = '2';
    rejected = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], ini_edit, ini_size, error, sizeof(error));
    if (rejected) goto done;
    s32 after = assetCatalogReserveTextureGenerationSlot();
    if (after >= 0) assetCatalogReleaseTextureGenerationSlot(after);
    if (after != available) goto done;
    rejected = catalogTextureGenerationAcquireSource("texgen:wrong_identity", image_edit, sources.sizes[3], ini, ini_size, error, sizeof(error));
    if (rejected) goto done;
    char bad_descriptor[512];
    int bad_size = snprintf(bad_descriptor, sizeof(bad_descriptor), "[texture]\ncatalog_id = %s\nsurface_type = unknown_surface\n", id);
    if (bad_size <= 0 || bad_size >= sizeof(bad_descriptor)) goto done;
    rejected = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], bad_descriptor, bad_size, error, sizeof(error));
    if (rejected) goto done;
    bad_size = snprintf(bad_descriptor, sizeof(bad_descriptor), "[texture]\ncatalog_id = %s\nsurface_type = wood\nsurface_type = metal\n", id);
    if (bad_size <= 0 || bad_size >= sizeof(bad_descriptor)) goto done;
    rejected = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], bad_descriptor, bad_size, error, sizeof(error));
    if (rejected) goto done;
    step = "bundled_source_guard";
    bad_size = snprintf(bad_descriptor, sizeof(bad_descriptor), "[texture]\ncatalog_id = %s\ntexture_file = sample.tga\n", id);
    if (bad_size <= 0 || bad_size >= sizeof(bad_descriptor) || !harnessWriteBytes(descriptor, bad_descriptor, bad_size)) goto done;
    entry = assetCatalogGetMutable(id);
    if (!entry) goto done;
    entry->bundled = 1;
    rejected = catalogTextureGenerationAcquire(id, error, sizeof(error));
    entry = assetCatalogGetMutable(id);
    if (entry) entry->bundled = 0;
    if (rejected || !strstr(error, "extraction upgrade required")) goto done;
    step = "retirement";
    if (!assetCatalogUnregister(id)) goto done;
    registered = 0;
    assetCatalogResetCustomTextureSlots();
    if (assetCatalogResolveAny(id) || catalogTextureGenerationForSlot(first_slot) != first
            || catalogTextureGenerationForSlot(second_slot) != second
            || catalogTextureGenerationForSlot(third_slot) != third) goto done;
    saved_definition = g_Textures[first_slot]; changed_table = 1;
    g_Textures[first_slot].surfacetype = SURFACETYPE_DEEPWATER;
    g_Textures[first_slot].soundsurfacetype = SURFACETYPE_DEEPWATER;
    g_Textures[first_slot].unk04_00 = 15;
    struct texpool empty_pool = {0};
    texnum_t update = (texnum_t)(0xabcd0000u | (u32)first_slot);
    texLoad(&update, &empty_pool, 0);
    if (update != (texnum_t)(uintptr_t)first_native->data
            || texFindInPool(first_slot, &empty_pool) != first_native
            || texGetDefinition(first_slot)->surfacetype != SURFACETYPE_WOOD) goto done;
    step = "native_model";
    harness_texture_model_t binding = {&sources, first, 0};
    modasset_model_inputs_t inputs = {&binding, harnessTextureModelRead, harnessTextureModelBind, &empty_pool};
    texResetTiles();
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, model_entry, sources.paths[0], &model_a) <= 0
            || binding.texture_calls != 1 || !harnessModelHasTexture(model_a, first_native->data)
            || !harnessModelHasTile(model_a, 2, 0, 1)) goto done;
    binding.texture = third; binding.texture_calls = 0; texResetTiles();
    if (modAssetCompilerBuildModeldefWithInputs(&inputs, model_entry, sources.paths[0], &model_b) <= 0
            || binding.texture_calls != 1 || !harnessModelHasTexture(model_b, catalogTextureGenerationTexture(third)->data)
            || !harnessModelHasTile(model_b, 3, 0, 1)
            || !harnessModelHasTexture(model_a, first_native->data)
            || ((u32 *)first_native->data)[0] != 0xff0000ffu) goto done;
    modAssetCompilerFreeModeldef(model_a); model_a = NULL;
    catalogTextureGenerationRelease(alias); alias = NULL;
    if (catalogTextureGenerationForSlot(first_slot) != first) goto done;
    catalogTextureGenerationRelease(first); first = NULL;
    if (catalogTextureGenerationForSlot(first_slot)) goto done;
    step = "reuse";
    for (u32 i = 18; i < 34; i += 4) { image_edit[i] = 0; image_edit[i + 1] = 255; }
    reused = catalogTextureGenerationAcquireSource(id, image_edit, sources.sizes[3], ini, ini_size, error, sizeof(error));
    if (!reused || catalogTextureGenerationSlot(reused) != first_slot) goto done;
    update = (texnum_t)(0xabcd0000u | (u32)first_slot);
    texLoad(&update, &empty_pool, 0);
    if (((u32 *)(uintptr_t)update)[0] != 0x00ff00ffu) goto done;
    ok = 1;
done:
    modAssetCompilerFreeModeldef(model_a); modAssetCompilerFreeModeldef(model_b);
    if (changed_table) g_Textures[first_slot] = saved_definition;
    catalogTextureGenerationRelease(alias); catalogTextureGenerationRelease(first);
    catalogTextureGenerationRelease(second); catalogTextureGenerationRelease(third);
    catalogTextureGenerationRelease(reused); catalogTextureGenerationRelease(rejected);
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomTextureSlots(slots)) ok = 0;
    assetCatalogDestroyCustomTextureSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    if (ini && !harnessWriteBytes(descriptor, ini, ini_size)) ok = 0;
    if (sources.bytes[3] && !harnessWriteBytes(sources.paths[3], sources.bytes[3], sources.sizes[3])) ok = 0;
    for (size_t i = 0; i < 4; ++i) free(sources.bytes[i]);
    free(ini); free(ini_edit); free(image_edit); free(model_entry);
    texResetTiles(); catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "TEXTURE.GENERATION.HARNESS: step=%s result=%s slots=%d,%d,%d error=%s",
        step, ok ? "PASS" : "FAIL", first_slot, second_slot, third_slot, error);
    return ok;
}


static s32 harnessTextureRuntime(const char *id, const char *folder)
{
    extern s32 g_NotLoadMod;
    const s32 prior_not_load_mod = g_NotLoadMod;
    char descriptor[FS_MAXPATH], image[FS_MAXPATH], error[256] = {0}, graph[1024];
    char *original = NULL, *edited = NULL;
    u32 original_size = 0;
    void *slots = assetCatalogSnapshotCustomTextureSlots();
    file_provider_checkpoint_t provider;
    s32 checkpoint = fileProviderCheckpointCreate(&provider), registered = 0, ok = 0;
    s32 slot = -1;
    u8 *storage = sysMemAlloc(4096);
    struct texpool pool = {0};
    const char *step = "source";
    if (!slots || !checkpoint || !storage || assetCatalogResolveAny(id)) goto done;
    if (snprintf(descriptor, sizeof(descriptor), "%s/texture.ini", folder) >= sizeof(descriptor)
            || snprintf(image, sizeof(image), "%s/sample.tga", folder) >= sizeof(image)) goto done;
    original = fsFileLoad(descriptor, &original_size);
    if (!original || !original_size) goto done;
    edited = malloc((size_t)original_size + 1);
    if (!edited) goto done;
    memcpy(edited, original, original_size); edited[original_size] = 0;
    slot = assetCatalogResolveTexturePrivateSlot(id);
    if (slot < 0) goto done;
    asset_entry_t *entry = assetCatalogRegisterTexture(id, slot, 2, 2, G_IM_FMT_RGBA, image);
    if (!entry) goto done;
    registered = 1; entry->enabled = 1; strcpy(entry->descriptor_path, descriptor);
    catalogSetPrimaryFile(entry, image); catalogLoadInit();
    step = "sparse_reverse_index";
    const CatalogResolveResult resolved = catalogResolveTexture(slot);
    const asset_entry_t *selected = assetCatalogGetByIndex(resolved.catalog_id);
    if (!selected || strcmp(selected->id, id) || !resolved.path) goto done;
    textureSourceRuntimeResetStage();
    step = "ordinary_native";
    texInitPool(&pool, storage, 4096);
    texnum_t update = (texnum_t)(0xabcd0000u | (u32)slot);
    texLoad(&update, &pool, 0);
    struct tex *native = texFindInPool(slot, &pool);
    if (!native || update != (texnum_t)(uintptr_t)native->data
            || ((u32 *)native->data)[0] != 0xff0000ffu
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_WOOD
            || texGetDefinition(slot)->soundsurfacetype != SURFACETYPE_METAL) goto done;
    step = "descriptor_edit";
    char *surface = strstr(edited, "surface_type = wood");
    if (!surface) goto done;
    memcpy(surface + strlen("surface_type = "), "snow", 4);
    if (!harnessWriteBytes(descriptor, edited, original_size)) goto done;
    catalogLoadInit();
    if (texGetDefinition(slot)->surfacetype != SURFACETYPE_SNOW) goto done;
    step = "stage_modes";
    int length = snprintf(graph, sizeof(graph),
        "{\"texture_properties_version\":1,\"texture_properties\":{\"mode\":\"multiplayer\",\"entries\":[{\"texture\":\"%s\",\"surface_type\":\"glass\",\"sound_surface_type\":\"wood\",\"tile_column_offset\":2}]}}", id);
    if (length <= 0 || length >= sizeof(graph)
            || !textureSourceRuntimeStageGraph(graph, length, 0, 1, error, sizeof(error))
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_SNOW
            || !textureSourceRuntimeStageGraph(graph, length, 1, 1, error, sizeof(error))
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_GLASS
            || texGetDefinition(slot)->soundsurfacetype != SURFACETYPE_WOOD) goto done;
    Gfx commands[16]; texResetTiles();
    Gfx *end = texWriteTileFromDefinition(commands, native, 0, 0, 0, 0);
    s32 found_tile = 0;
    for (Gfx *command = commands; command < end; ++command)
        if ((command->words.w0 >> 24) == G_SETTILE && (command->words.w0 & 0x1ffu) == 3) found_tile = 1;
    if (!found_tile) goto done;
    step = "stage_reject_reset";
    const char *bad = "{\"texture_properties_version\":1,\"texture_properties\":{\"entries\":[{\"texture\":\"missing:texture\",\"surface_type\":\"wood\"}]}}";
    if (textureSourceRuntimeStageGraph(bad, strlen(bad), 1, 1, error, sizeof(error))
            || texGetDefinition(slot)->surfacetype != SURFACETYPE_GLASS) goto done;
    textureSourceRuntimeResetStage();
    if (texGetDefinition(slot)->surfacetype != SURFACETYPE_SNOW
            || texGetDefinition(slot)->soundsurfacetype != SURFACETYPE_METAL) goto done;
    step = "versioned_omission";
    length = snprintf(graph, sizeof(graph), "[texture]\ncatalog_id = %s\nproperties_version = 1\ntexture_file = sample.tga\n", id);
    if (length <= 0 || length >= sizeof(graph) || !harnessWriteBytes(descriptor, graph, length)) goto done;
    entry = assetCatalogGetMutable(id); if (!entry) goto done;
    entry->bundled = 1; textureSourceRuntimeInvalidate();
    const s32 defaults_ok = texGetDefinition(slot)->surfacetype == SURFACETYPE_DEFAULT;
    entry = assetCatalogGetMutable(id); if (entry) entry->bundled = 0;
    if (!defaults_ok) goto done;
    step = "base_overlay_selection";
    if (!harnessWriteBytes(descriptor, original, original_size)) goto done;
    entry = assetCatalogGetMutable(id); if (!entry) goto done;
    entry->source_texnum = 1; entry->ext.texture.texture_id = 1;
    catalogLoadInit();
    mod_texture_rgba32_source_t decoded;
    g_NotLoadMod = 0;
    if (modTextureLoadRgba32Source(1, &decoded) <= 0) goto done;
    const s32 overlay_ok = ((u32 *)decoded.pixels)[0] == 0xff0000ffu
        && texGetDefinition(1)->surfacetype == SURFACETYPE_WOOD;
    modTextureFreeRgba32Source(&decoded);
    if (!overlay_ok) goto done;
    g_NotLoadMod = 1;
    if (modTextureLoadRgba32Source(1, &decoded) <= 0) goto done;
    const asset_entry_t *base = assetCatalogGetByIndex(decoded.catalog_id);
    texture_source_properties_t base_properties;
    const s32 base_ok = base && base->bundled
        && textureSourceRuntimeReadSelected(base, decoded.path, &base_properties, error, sizeof(error))
        && texGetDefinition(1)->surfacetype == base_properties.surface_type;
    modTextureFreeRgba32Source(&decoded);
    if (!base_ok) goto done;
    g_NotLoadMod = 0;
    if (texGetDefinition(1)->surfacetype != SURFACETYPE_WOOD) goto done;
    ok = 1;
 done:
    g_NotLoadMod = prior_not_load_mod;
    textureSourceRuntimeResetStage();
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomTextureSlots(slots)) ok = 0;
    assetCatalogDestroyCustomTextureSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    if (original && !harnessWriteBytes(descriptor, original, original_size)) ok = 0;
    free(original); free(edited); sysMemFree(storage);
    texResetTiles(); catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "TEXTURE.SOURCE.RUNTIME.HARNESS: step=%s result=%s slot=%d error=%s",
        step, ok ? "PASS" : "FAIL", slot, error);
    return ok;
}

static s32 harnessAnimationGeneration(const char *id, const char *folder)
{
    catalog_animation_generation_t *first = NULL, *alias = NULL, *second = NULL, *reused = NULL;
    file_provider_checkpoint_t provider;
    void *slots = assetCatalogSnapshotCustomAnimSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), ok = 0, registered = 0;
    char path[FS_MAXPATH], error[256] = {0}, first_hash[65];
    const char *step = "source";
    u32 source_size = 0;
    u8 *source = NULL, *edited = NULL;
    u8 native_before[32], native_after[32];
    s32 first_slot = -1, second_slot = -1;
    f32 first_y = 0, second_y = 0, reused_y = 0;
    if (!slots || !checkpoint || assetCatalogResolveAny(id)
            || snprintf(path, sizeof(path), "%s/frames.bin", folder) >= sizeof(path)) goto done;
    source = fsFileLoad(path, &source_size);
    if (!source || source_size != 32) goto done;
    edited = malloc(source_size);
    if (!edited) goto done;
    memcpy(edited, source, source_size);
    if (assetCatalogScanExternalLayoutFolderDeferred("genproof", folder) != 1) goto done;
    registered = 1;
    step = "acquire";
    first = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    alias = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    if (!first || first != alias) goto done;
    first_slot = catalogAnimationGenerationSlot(first);
    strcpy(first_hash, catalogAnimationGenerationHash(first));
    const void *first_data = catalogAnimationGenerationData(first_slot);
    sha256Hash(first_data, catalogAnimationGenerationSize(first), native_before);
    if (!harnessGenerationFrame(first_slot, &first_y) || !(first_y > 0)) goto done;
    /* Only the declared external glTF buffer changes. JSON/descriptor stay exact. */
    f32 position = 600.0f;
    memcpy(edited + 24, &position, sizeof(position));
    if (!harnessWriteBytes(path, edited, source_size)) goto done;
    step = "source_edit";
    second = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    if (!second || second == first || !strcmp(first_hash, catalogAnimationGenerationHash(second))) goto done;
    second_slot = catalogAnimationGenerationSlot(second);
    if (first_slot == second_slot || !harnessGenerationFrame(second_slot, &second_y) || !(second_y > first_y)) goto done;
    if (!assetCatalogUnregister(id)) goto done;
    registered = 0;
    assetCatalogResetCustomAnimSlots();
    step = "retired_source";
    if (assetCatalogResolveAny(id) || modAnimationLoadData((u16)first_slot) != first_data
            || !catalogAnimationGenerationData(second_slot)
            || !harnessGenerationFrame(first_slot, &reused_y) || reused_y != first_y) goto done;
    sha256Hash(first_data, catalogAnimationGenerationSize(first), native_after);
    if (memcmp(native_before, native_after, sizeof(native_before))) goto done;
    catalogAnimationGenerationRelease(alias); alias = NULL;
    if (catalogAnimationGenerationData(first_slot) != first_data) goto done;
    catalogAnimationGenerationRelease(first); first = NULL;
    if (catalogAnimationGenerationData(first_slot)) goto done;
    /* Reuse the released ID with different bytes; cached frames must not survive. */
    position = 900.0f;
    memcpy(edited + 24, &position, sizeof(position));
    if (!harnessWriteBytes(path, edited, source_size)
            || assetCatalogScanExternalLayoutFolderDeferred("genproof", folder) != 1) goto done;
    registered = 1;
    step = "cache_reuse";
    reused = catalogAnimationGenerationAcquire(id, error, sizeof(error));
    if (!reused || catalogAnimationGenerationSlot(reused) != first_slot
            || !harnessGenerationFrame(first_slot, &reused_y) || !(reused_y > second_y)
            || !harnessGenerationFrame(second_slot, &first_y) || first_y != second_y) goto done;
    ok = 1;
done:
    if (source && !harnessWriteBytes(path, source, source_size)) ok = 0;
    catalogAnimationGenerationRelease(alias);
    catalogAnimationGenerationRelease(first);
    catalogAnimationGenerationRelease(second);
    catalogAnimationGenerationRelease(reused);
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomAnimSlots(slots)) ok = 0;
    assetCatalogDestroyCustomAnimSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    free(edited); free(source);
    catalogBuildRuntimeCaches();
    catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "ANIMATION.GENERATION.HARNESS: step=%s result=%s error=%s", step, ok ? "PASS" : "FAIL", error);
    return ok;
}

/* The actual ordinary file-voice mixer writes deterministic stereo samples;
 * SDL dummy output is initialized by this fixture, with no hardware claim. */
static void harnessAudioMixFrame(s16 output[2])
{
    output[0] = output[1] = 0;
    audioMixFileSoundsInto(output, 1);
}
static s32 harnessAudioGeneration(const char *id, const char *folder)
{
    catalog_audio_generation_t *first = NULL, *alias = NULL, *second = NULL, *reused = NULL;
    struct sndstate *first_voice = NULL, *second_voice = NULL, *reused_voice = NULL;
    file_provider_checkpoint_t provider;
    void *slots = assetCatalogSnapshotCustomSoundSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), registered = 0, ok = 0;
    const char *step = "source";
    char path[FS_MAXPATH], error[256] = {0}, hash[65];
    u8 *source = NULL, *edited = NULL, native_hash[32], retained_hash[32];
    u32 size = 0, frames = 0;
    s32 rate = 0, first_slot = -1, second_slot = -1;
    s16 first_mix[2] = {0}, second_mix[2] = {0}, mixed[2] = {0};
    s32 nonzero_frames = 0;
    if (g_SndDisabled || !slots || !checkpoint || assetCatalogResolveAny(id)
            || snprintf(path, sizeof(path), "%s/sample.wav", folder) >= sizeof(path)) goto done;
    source = fsFileLoad(path, &size);
    if (!source || size != 108) goto done;
    edited = malloc(size);
    if (!edited) goto done;
    memcpy(edited, source, size);
    if (assetCatalogScanExternalLayoutFolderDeferred("audiogenproof", folder) != 1) goto done;
    registered = 1; step = "acquire";
    first = catalogAudioGenerationAcquire(id, error, sizeof(error));
    alias = catalogAudioGenerationAcquire(id, error, sizeof(error));
    if (!first || alias != first) goto done;
    first_slot = catalogAudioGenerationSlot(first);
    strcpy(hash, catalogAudioGenerationHash(first));
    const s16 *pcm = catalogAudioGenerationPcm(first, &frames, &rate);
    if (!pcm || frames != 16 || rate != 22050) goto done;
    sha256Hash(pcm, frames * 2 * sizeof(s16), native_hash);
    audio_sample_parameters_t bounds = {0};
    bounds.base_pitch = 2.0f;
    const int allocations = SDL_GetNumAllocations();
    if (audioStartPcmSound(pcm, frames, rate, &bounds, 0x7fff, 64, FLT_MAX,
            0, 0, &first_voice) || first_voice || SDL_GetNumAllocations() != allocations) goto done;
    first_voice = sndStart(0, (s16)first_slot, &first_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!first_voice || !audioFileSoundOwnsHandle(first_voice)) goto done;
    harnessAudioMixFrame(first_mix);
    if (first_mix[0] <= 0 || first_mix[1] <= first_mix[0]) goto done;
    /* Edit only the original WAV frames. Existing native voice owns its PCM. */
    for (u32 i = 0; i < frames; ++i) {
        const s16 values[2] = {4000, 5000};
        memcpy(edited + 44 + i * 4, values, sizeof(values));
    }
    if (!harnessWriteBytes(path, edited, size)) goto done;
    step = "source_edit";
    second = catalogAudioGenerationAcquire(id, error, sizeof(error));
    if (!second || second == first || !strcmp(hash, catalogAudioGenerationHash(second))) goto done;
    second_slot = catalogAudioGenerationSlot(second);
    if (second_slot == first_slot || !assetCatalogUnregister(id)) goto done;
    registered = 0;
    assetCatalogResetCustomSoundSlots();
    step = "retired_source";
    if (assetCatalogResolveAny(id) || catalogAudioGenerationForSlot(first_slot) != first
            || catalogAudioGenerationForSlot(second_slot) != second) goto done;
    harnessAudioMixFrame(mixed);
    if (memcmp(mixed, first_mix, sizeof(mixed))) goto done;
    sha256Hash(pcm, frames * 2 * sizeof(s16), retained_hash);
    if (memcmp(native_hash, retained_hash, sizeof(native_hash))) goto done;
    audioFileSoundStop(first_voice); first_voice = NULL;
    second_voice = sndStart(0, (s16)second_slot, &second_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!second_voice) goto done;
    harnessAudioMixFrame(second_mix);
    if (second_mix[0] <= first_mix[0] || second_mix[1] <= first_mix[1]) goto done;
    audioFileSoundStop(second_voice); second_voice = NULL;
    first_voice = sndStart(0, (s16)first_slot, &first_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!first_voice) goto done;
    catalogAudioGenerationRelease(alias); alias = NULL;
    if (catalogAudioGenerationForSlot(first_slot) != first) goto done;
    catalogAudioGenerationRelease(first); first = NULL;
    if (catalogAudioGenerationForSlot(first_slot)) goto done;
    step = "playing_after_release";
    harnessAudioMixFrame(mixed);
    if (!first_voice || memcmp(mixed, first_mix, sizeof(mixed))) goto done;
    audioFileSoundStop(first_voice); first_voice = NULL;
    for (u32 i = 0; i < frames; ++i) {
        const s16 values[2] = {8000, 9000};
        memcpy(edited + 44 + i * 4, values, sizeof(values));
    }
    if (!harnessWriteBytes(path, edited, size)
            || assetCatalogScanExternalLayoutFolderDeferred("audiogenproof", folder) != 1) goto done;
    registered = 1; step = "slot_reuse_and_loop";
    reused = catalogAudioGenerationAcquire(id, error, sizeof(error));
    if (!reused || catalogAudioGenerationSlot(reused) != first_slot) goto done;
    reused_voice = sndStart(0, (s16)first_slot, &reused_voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!reused_voice || !assetCatalogUnregister(id)) goto done;
    registered = 0;
    catalogAudioGenerationRelease(reused); reused = NULL;
    harnessAudioMixFrame(mixed);
    if (mixed[0] <= second_mix[0] || mixed[1] <= second_mix[1]) goto done;
    s16 tail[64] = {0};
    audioMixFileSoundsInto(tail, 32);
    for (s32 i = 0; i < 32; ++i) if (tail[i * 2] || tail[i * 2 + 1]) ++nonzero_frames;
    /* Sixteen source frames plus one two-frame loop, less first frame above. */
    if (nonzero_frames != 17 || reused_voice) goto done;
    ok = 1;
done:
    if (first_voice) audioFileSoundStop(first_voice);
    if (second_voice) audioFileSoundStop(second_voice);
    if (reused_voice) audioFileSoundStop(reused_voice);
    if (source && !harnessWriteBytes(path, source, size)) ok = 0;
    catalogAudioGenerationRelease(alias); catalogAudioGenerationRelease(first);
    catalogAudioGenerationRelease(second); catalogAudioGenerationRelease(reused);
    if (registered && assetCatalogResolveAny(id) && !assetCatalogRollbackUnactivatedRegistration(id)) ok = 0;
    if (slots && !assetCatalogRestoreCustomSoundSlots(slots)) ok = 0;
    assetCatalogDestroyCustomSoundSlotSnapshot(slots);
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    free(edited); free(source);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "AUDIO.GENERATION.HARNESS: step=%s result=%s first=%d,%d second=%d,%d tail=%d error=%s",
        step, ok ? "PASS" : "FAIL", first_mix[0], first_mix[1], second_mix[0], second_mix[1], nonzero_frames, error);
    return ok;
}

static s32 harnessCommandGeneration(const char *id, const char *folder)
{
    const char *ids[] = {"cmdimport_generation:parent", "cmdimport_generation:left",
        "cmdimport_generation:right", "cmdimport_generation:clip", "cmdimport_generation:sound"};
    const char *files[] = {"animations/left/commands.json", "animations/right/commands.json",
        "animations/clip/frames.bin", "audio/sfx/sound/sample.wav", "behavior.graph.json"};
    const char *cycle = "{\"source_format\":\"weapon_animation_commands\",\"name\":\"Reload\",\"command_count\":2,\"commands\":[{\"command\":\"include_animation\",\"slot\":0,\"animation\":\"cmdimport_generation:parent\"},{\"command\":\"end\"}]}";
    const char *missing = "{\"source_format\":\"weapon_animation_commands\",\"name\":\"Reload\",\"command_count\":2,\"commands\":[{\"command\":\"include_animation\",\"slot\":0,\"animation\":\"absent:commands\"},{\"command\":\"end\"}]}";
    char paths[5][FS_MAXPATH], error[256] = {0};
    u8 *source[5] = {0}, *edit = NULL;
    u32 sizes[5] = {0};
    catalog_command_generation_t *generations[4] = {0}, *rejected = NULL;
    wg_v2_native_bundle *native = NULL, *latest = NULL;
    wg_v2_program *program = NULL;
    struct sndstate *voice = NULL;
    file_provider_checkpoint_t provider;
    loader_pool_snapshot_t *pool = loaderPoolSnapshotCreate();
    void *sound_slots = assetCatalogSnapshotCustomSoundSlots(), *anim_slots = assetCatalogSnapshotCustomAnimSlots();
    s32 checkpoint = fileProviderCheckpointCreate(&provider), scanned = 0, ok = 0;
    const char *step = "source";
    s32 old_clip = -1, new_clip = -1, old_sound = -1, new_sound = -1;
    f32 old_y = 0, new_y = 0, retained_y = 0;
    s16 old_mix[2] = {0}, new_mix[2] = {0};
    const struct guncmd *commands = NULL, *last_commands = NULL;
    const struct weaponfunc_shoot *function = NULL;
    for (size_t i = 0; i < 5; ++i) if (assetCatalogResolveAny(ids[i])) goto done;
    if (!pool || !sound_slots || !anim_slots || !checkpoint || g_SndDisabled) goto done;
    for (size_t i = 0; i < 5; ++i) {
        if (snprintf(paths[i], sizeof(paths[i]), "%s/%s", folder, files[i]) >= sizeof(paths[i])) goto done;
        source[i] = fsFileLoad(paths[i], &sizes[i]);
        if (!source[i] || !sizes[i]) goto done;
    }
    if (sizes[2] != 32 || sizes[3] != 108) goto done;
    scanned = 1;
    if (assetCatalogScanExternalLayoutFolderDeferred("cmdimport_generation", folder) != 5) goto done;
    step = "native_resolver";
    generations[0] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[0]) goto done;
    commands = catalogCommandGenerationCommands(generations[0]);
    const struct guncmd *left = (const struct guncmd *)commands[0].unk04;
    const struct guncmd *right = (const struct guncmd *)commands[1].unk04;
    if (!left || !right || left == right || left[0].unk04 != 11 || right[0].unk04 != 22
            || left[1].unk04 != (intptr_t)right || commands == loaderPoolFindAnimationByCatalogId(id)) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!rejected || strcmp(catalogCommandGenerationHash(rejected), catalogCommandGenerationHash(generations[0]))) goto done;
    catalogCommandGenerationRelease(rejected); rejected = NULL;
    program = wgV2Compile((const char *)source[4], sizes[4], 1,
        catalogCommandGenerationHash(generations[0]), error, sizeof(error));
    if (!program || !(native = wgV2NativePrepare(program, catalogGraphNativeResolve, NULL, error, sizeof(error)))) goto done;
    function = (const struct weaponfunc_shoot *)wgV2NativeFunction(wgV2NativeFind(native, "shot"));
    if (!function || !function->base.fire_animation || function->base.fire_animation == commands
            || function->shootsound != commands[3].unk04
            || ((const struct guncmd *)function->base.fire_animation[0].unk04)[0].unk04 != 11) goto done;
    old_clip = commands[2].unk02; old_sound = commands[3].unk04;
    if (!harnessGenerationFrame(old_clip, &old_y) || !(old_y > 0)) goto done;
    /* Change only one child command's public scalar. */
    step = "command_edit";
    edit = malloc(sizes[0] + 1);
    if (!edit) goto done;
    memcpy(edit, source[0], sizes[0]); edit[sizes[0]] = 0;
    char *ticks = strstr((char *)edit, "\"ticks\": 11");
    if (!ticks) goto done;
    ticks[strlen("\"ticks\": ")] = '9';
    if (!harnessWriteBytes(paths[0], edit, sizes[0])) goto done;
    free(edit); edit = NULL;
    generations[1] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[1] || !strcmp(catalogCommandGenerationHash(generations[0]), catalogCommandGenerationHash(generations[1]))
            || ((const struct guncmd *)catalogCommandGenerationCommands(generations[1])[0].unk04)[0].unk04 != 91
            || left[0].unk04 != 11) goto done;
    step = "clip_edit";
    edit = malloc(sizes[2]);
    if (!edit) goto done;
    memcpy(edit, source[2], sizes[2]);
    f32 position = 600.0f; memcpy(edit + 24, &position, sizeof(position));
    if (!harnessWriteBytes(paths[2], edit, sizes[2])) goto done;
    free(edit); edit = NULL;
    generations[2] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[2] || !strcmp(catalogCommandGenerationHash(generations[1]), catalogCommandGenerationHash(generations[2]))) goto done;
    new_clip = catalogCommandGenerationCommands(generations[2])[2].unk02;
    if (new_clip == old_clip || !harnessGenerationFrame(new_clip, &new_y) || !(new_y > old_y)) goto done;
    step = "audio_edit";
    edit = malloc(sizes[3]);
    if (!edit) goto done;
    memcpy(edit, source[3], sizes[3]);
    for (size_t i = 0; i < 16; ++i) {
        const s16 pcm[] = {4000, 5000}; memcpy(edit + 44 + i * 4, pcm, sizeof(pcm));
    }
    if (!harnessWriteBytes(paths[3], edit, sizes[3])) goto done;
    free(edit); edit = NULL;
    generations[3] = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (!generations[3] || !strcmp(catalogCommandGenerationHash(generations[2]), catalogCommandGenerationHash(generations[3]))) goto done;
    last_commands = catalogCommandGenerationCommands(generations[3]);
    new_sound = last_commands[3].unk04;
    if (new_sound == old_sound || last_commands[2].unk02 != new_clip) goto done;
    latest = wgV2NativePrepare(program, catalogGraphNativeResolve, NULL, error, sizeof(error));
    if (!latest || !strcmp(wgV2NativeClosureHash(native), wgV2NativeClosureHash(latest))) goto done;
    step = "cycle_and_missing_rejection";
    if (!harnessWriteBytes(paths[1], cycle, strlen(cycle))) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (rejected || !strstr(error, "cycle")) goto done;
    if (!harnessWriteBytes(paths[1], missing, strlen(missing))) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (rejected || !strstr(error, "absent:commands")) goto done;
    if (!harnessWriteBytes(paths[1], source[1], sizes[1])) goto done;
    wg_v2_native_dependency invalid = {0};
    if (catalogGraphNativeResolve(NULL, WG_V2_COMMAND_SOURCE, ids[4], &invalid, error, sizeof(error))
            || invalid.lease || invalid.commands || invalid.release || invalid.catalog_id) {
        if (invalid.release) invalid.release(invalid.lease);
        goto done;
    }
    step = "late_decode_rollback";
    if (!harnessWriteBytes(paths[3], "bad", 3)) goto done;
    rejected = catalogCommandGenerationAcquire(id, error, sizeof(error));
    if (rejected) goto done;
    /* Retire all source rows and reset ordinary slots. The real native action
     * still owns the original command closure, animation and sound generations. */
    step = "retired_native_consumers";
    for (size_t i = 0; i < 5; ++i) if (!assetCatalogUnregister(ids[i])) goto done;
    assetCatalogResetCustomAnimSlots(); assetCatalogResetCustomSoundSlots();
    for (size_t i = 0; i < 3; ++i) {
        catalogCommandGenerationRelease(generations[i]); generations[i] = NULL;
    }
    commands = function->base.fire_animation;
    if (((const struct guncmd *)commands[0].unk04)[0].unk04 != 11
            || !harnessGenerationFrame(commands[2].unk02, &retained_y) || retained_y != old_y
            || !harnessGenerationFrame(new_clip, &retained_y) || retained_y != new_y) goto done;
    voice = sndStart(0, function->shootsound, &voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!voice) goto done;
    harnessAudioMixFrame(old_mix); audioFileSoundStop(voice); voice = NULL;
    voice = sndStart(0, new_sound, &voice, 0x7fff, 64, 1.0f, 0, 0);
    if (!voice) goto done;
    harnessAudioMixFrame(new_mix); audioFileSoundStop(voice); voice = NULL;
    if (old_mix[0] <= 0 || old_mix[1] <= old_mix[0] || new_mix[0] <= old_mix[0] || new_mix[1] <= old_mix[1]) goto done;
    step = "release";
    wgV2NativeRelease(native); native = NULL;
    if (catalogAnimationGenerationData(old_clip) || catalogAudioGenerationForSlot(old_sound)) goto done;
    wgV2NativeRelease(latest); latest = NULL;
    catalogCommandGenerationRelease(generations[3]); generations[3] = NULL;
    if (catalogAnimationGenerationData(new_clip) || catalogAudioGenerationForSlot(new_sound)) goto done;
    error[0] = 0; ok = 1;
done:
    if (voice) audioFileSoundStop(voice);
    wgV2NativeRelease(native); wgV2NativeRelease(latest); wgV2ProgramRelease(program);
    catalogCommandGenerationRelease(rejected);
    for (size_t i = 0; i < 4; ++i) catalogCommandGenerationRelease(generations[i]);
    for (size_t i = 0; i < 5; ++i) {
        if (source[i] && !harnessWriteBytes(paths[i], source[i], sizes[i])) ok = 0;
        free(source[i]);
        if (scanned && assetCatalogResolveAny(ids[i]) && !assetCatalogRollbackUnactivatedRegistration(ids[i])) ok = 0;
    }
    free(edit);
    if (pool && !loaderPoolSnapshotRestore(pool)) ok = 0;
    if (sound_slots && !assetCatalogRestoreCustomSoundSlots(sound_slots)) ok = 0;
    if (anim_slots && !assetCatalogRestoreCustomAnimSlots(anim_slots)) ok = 0;
    if (checkpoint && !fileProviderCheckpointRestore(&provider)) ok = 0;
    loaderPoolSnapshotDestroy(pool);
    assetCatalogDestroyCustomSoundSlotSnapshot(sound_slots); assetCatalogDestroyCustomAnimSlotSnapshot(anim_slots);
    catalogBuildRuntimeCaches(); catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "COMMAND.GENERATION.HARNESS: step=%s result=%s old_y=%.1f new_y=%.1f old_mix=%d,%d new_mix=%d,%d error=%s",
        step, ok ? "PASS" : "FAIL", old_y, new_y, old_mix[0], old_mix[1], new_mix[0], new_mix[1], error);
    return ok;
}


static s32 harnessReadCommands(const char *directory, const char *file, const char *id)
{
	char path[FS_MAXPATH];
	if (snprintf(path, sizeof(path), "%s/%s", directory, file) >= sizeof(path)) return 0;
	u32 size = 0;
	char *json = fsFileLoad(path, &size);
	if (!json) return 0;
	s32 ok = loaderPoolParseAnimationCatalogSourceJson(json, size, path, id);
	free(json);
	return ok;
}

static s32 harnessStageCommands(loader_animation_source_batch_t *batch,
        const char *directory, const char *file, const char *id)
{
    char path[FS_MAXPATH];
    if (snprintf(path, sizeof(path), "%s/%s", directory, file) >= sizeof(path)) return 0;
    u32 size = 0;
    char *json = fsFileLoad(path, &size);
    s32 ok = json && loaderAnimationSourceBatchStage(batch, json, size, path, id);
    free(json);
    return ok;
}

static s32 harnessBatchBoundaries(const char *directory)
{
    const char *left_id = "command_a:reload", *right_id = "command_b:reload";
    const struct guncmd *left = loaderPoolFindAnimationByCatalogId(left_id);
    const struct guncmd *right = loaderPoolFindAnimationByCatalogId(right_id);
    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    s32 ok = batch && left && right;
    if (ok) ok = harnessStageCommands(batch, directory, "left.json", left_id)
        && harnessStageCommands(batch, directory, "left.json", left_id)
        && !harnessStageCommands(batch, directory, "right.json", left_id)
        && !loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    batch = loaderAnimationSourceBatchCreate();
    if (ok) ok = batch && harnessStageCommands(batch, directory, "left.json", left_id)
        && harnessStageCommands(batch, directory, "rejected.json", right_id)
        && loaderPoolFindAnimationByCatalogId(left_id) == left
        && !loaderAnimationSourceBatchCommit(batch)
        && loaderPoolFindAnimationByCatalogId(left_id) == left
        && loaderPoolFindAnimationByCatalogId(right_id) == right;
    loaderAnimationSourceBatchDestroy(batch);
    batch = loaderAnimationSourceBatchCreate();
    char path[FS_MAXPATH];
    u32 size = 0;
    char *original = NULL;
    if (ok && snprintf(path, sizeof(path), "%s/left.json", directory) < sizeof(path)) {
        original = fsFileLoad(path, &size);
        ok = batch && original && harnessStageCommands(batch, directory, "left.json", left_id);
        FILE *file = ok ? fopen(path, "ab") : NULL;
        if (!file) ok = 0;
        else {
            if (fputc(' ', file) == EOF) ok = 0;
            if (fclose(file)) ok = 0;
            if (ok) ok = !loaderAnimationSourceBatchCommit(batch);
            /* Restore the exact public fixture bytes even if the assertion failed. */
            file = fopen(path, "wb");
            if (!file) ok = 0;
            else {
                if (fwrite(original, 1, size, file) != size) ok = 0;
                if (fclose(file)) ok = 0;
            }
        }
    } else ok = 0;
    free(original);
    loaderAnimationSourceBatchDestroy(batch);
    if (loaderPoolFindAnimationByCatalogId(left_id) != left
            || loaderPoolFindAnimationByCatalogId(right_id) != right) ok = 0;
    /* Reverse an existing edge. New child is deliberately staged last; old
     * source dependencies must not veto the valid submitted replacement DAG. */
    batch = loaderAnimationSourceBatchCreate();
    if (ok) ok = batch && harnessStageCommands(batch, directory, "order_left.json", left_id)
        && harnessStageCommands(batch, directory, "right.json", right_id)
        && loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    batch = loaderAnimationSourceBatchCreate();
    if (ok) ok = batch && harnessStageCommands(batch, directory, "order_right.json", right_id)
        && harnessStageCommands(batch, directory, "left.json", left_id)
        && loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    if (ok) {
        left = loaderPoolFindAnimationByCatalogId(left_id);
        right = loaderPoolFindAnimationByCatalogId(right_id);
        ok = left && right && left[0].unk04 == 1 && right[0].unk04 == (intptr_t)left;
    }
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.COMMAND_BATCH.HARNESS: duplicate_edit_rollback=%s", ok ? "PASS" : "FAIL");
    return ok;
}

static s32 harnessScanCommandImport(const char *owner, const char *prefix,
        const char *kind, const char *variant)
{
    char path[FS_MAXPATH];
    s32 folder = !strcmp(kind, "folder");
    if (snprintf(path, sizeof(path), "%s_%s%s", prefix, variant, folder ? "" : ".pdmod") >= sizeof(path)) return -1;
    if (folder) return assetCatalogScanExternalLayoutFolderDeferred(owner, path);
    mod_archive_t *archive = modArchiveOpen(path);
    if (!archive) return -1;
    s32 count = assetCatalogScanComponentsFromArchive(owner, archive);
    modArchiveClose(archive);
    return count;
}

static s32 harnessCommandImport(const char *owner, const char *prefix, const char *kind)
{
    const char *names[] = {"parent", "left", "right", "clip", "sound", "late"};
    char ids[6][CATALOG_ID_LEN];
    for (size_t i = 0; i < 6; ++i) {
        if (snprintf(ids[i], sizeof(ids[i]), "%s:%s", owner, names[i]) >= sizeof(ids[i])
                || assetCatalogResolveAny(ids[i])) return 0;
    }
    file_provider_checkpoint_t provider_before, provider_good, provider_after;
    loader_pool_snapshot_t *pool = loaderPoolSnapshotCreate();
    void *sound_slots = assetCatalogSnapshotCustomSoundSlots();
    void *anim_slots = assetCatalogSnapshotCustomAnimSlots();
    catalog_entry_snapshot_t *rows = calloc(5, sizeof(*rows));
    s32 checkpoint = fileProviderCheckpointCreate(&provider_before);
    s32 ok = 0;
    const char *step = "admission";
    if (!pool || !sound_slots || !anim_slots || !rows || !checkpoint) goto done;
    if (harnessScanCommandImport(owner, prefix, kind, "good") < 5) goto done;
    const struct guncmd *parent = loaderPoolFindAnimationByCatalogId(ids[0]);
    const struct guncmd *left = loaderPoolFindAnimationByCatalogId(ids[1]);
    const struct guncmd *right = loaderPoolFindAnimationByCatalogId(ids[2]);
    const asset_entry_t *clip = assetCatalogResolve(ids[3]);
    const asset_entry_t *sound = assetCatalogResolve(ids[4]);
    step = "native_dependencies";
    if (!parent || !left || !right || left == right || left[0].unk04 != 11 || right[0].unk04 != 22
            || parent[0].unk04 != (intptr_t)left || parent[1].unk04 != (intptr_t)right
            || !clip || !sound || parent[2].unk02 != clip->ext.anim.anim_id
            || parent[3].unk04 != sound->ext.audio.sound_id
            || clip->ext.anim.anim_id < 0 || sound->ext.audio.sound_id <= 0) goto done;
    for (size_t i = 0; i < 5; ++i) {
        const asset_entry_t *row = assetCatalogResolve(ids[i]);
        if (!row) goto done;
        const char *source = fileProviderPath(row->source.primary);
        if (!source || !strstr(source, !strncmp(prefix, "./", 2) ? prefix + 2 : prefix)) goto done;
        if (strcmp(kind, "folder") && !strstr(source, "_good.pdmod::")) goto done;
        catalogEntrySnapshotCapture(&rows[i], row);
    }
    if (!fileProviderCheckpointCreate(&provider_good)) goto done;
    const char *rejects[] = {"late", "cycle"};
    for (size_t r = 0; r < 2; ++r) {
        step = rejects[r];
        if (harnessScanCommandImport(owner, prefix, kind, rejects[r]) >= 0
                || assetCatalogResolveAny(ids[5])
                || loaderPoolFindAnimationByCatalogId(ids[0]) != parent
                || loaderPoolFindAnimationByCatalogId(ids[1]) != left
                || loaderPoolFindAnimationByCatalogId(ids[2]) != right
                || parent[0].unk04 != (intptr_t)left || left[0].unk04 != 11
                || right[0].unk04 != 22) goto done;
        step = r == 0 ? "late_rows" : "cycle_rows";
        for (size_t i = 0; i < 5; ++i) {
            const asset_entry_t *row = assetCatalogResolve(ids[i]);
            if (!row || !catalogEntrySnapshotMatches(&rows[i], row)) {
                sysLogPrintf(LOG_WARNING, "PDWEAPON.COMMAND_IMPORT.ROW_DIFF: id=%s state=%d->%d ref=%d->%d",
                    ids[i], rows[i].row.load_state, row ? row->load_state : -1,
                    rows[i].row.ref_count, row ? row->ref_count : -1);
                goto done;
            }
        }
        step = r == 0 ? "late_provider" : "cycle_provider";
        if (!fileProviderCheckpointCreate(&provider_after)
                || provider_after.pool_used != provider_good.pool_used
                || provider_after.path_count != provider_good.path_count) goto done;
    }
    ok = 1;
done:
    for (size_t i = 6; i-- > 0;)
        if (assetCatalogResolveAny(ids[i]) && !assetCatalogRollbackUnactivatedRegistration(ids[i])) ok = 0;
    if (pool && !loaderPoolSnapshotRestore(pool)) ok = 0;
    if (sound_slots && !assetCatalogRestoreCustomSoundSlots(sound_slots)) ok = 0;
    if (anim_slots && !assetCatalogRestoreCustomAnimSlots(anim_slots)) ok = 0;
    if (checkpoint && !fileProviderCheckpointRestore(&provider_before)) ok = 0;
    loaderPoolSnapshotDestroy(pool);
    assetCatalogDestroyCustomSoundSlotSnapshot(sound_slots);
    assetCatalogDestroyCustomAnimSlotSnapshot(anim_slots);
    free(rows);
    catalogBuildRuntimeCaches();
    catalogLoadInit();
    sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.COMMAND_IMPORT.HARNESS: kind=%s step=%s result=%s", kind, step, ok ? "PASS" : "FAIL");
    return ok;
}

static s32 harnessCommandSources(const char *directory)
{
	const char *ids[] = {"command_a:reload", "command_b:reload", "commandproof:parent"};
	for (size_t i = 0; i < 3; ++i) if (assetCatalogResolveAny(ids[i])) return 0;
	loader_pool_snapshot_t *snapshot = loaderPoolSnapshotCreate();
	if (!snapshot) return 0;
	s32 created = 0, ok = 0;
	for (; created < 3; ++created) {
		if (!assetCatalogRegisterAnimation(ids[created], -1, "Reload", 0, NULL)) goto done;
		assetCatalogSetCategoryById(ids[created], "weapon_animation");
	}
	/* Load the parent first to exercise exact names through deferred binding. */
	if (!harnessReadCommands(directory, "parent.json", ids[2])
			|| !harnessReadCommands(directory, "left.json", ids[0])
			|| !harnessReadCommands(directory, "right.json", ids[1])) goto done;
	loaderPoolFinalize();
	const struct guncmd *left = loaderPoolFindAnimationByCatalogId(ids[0]);
	const struct guncmd *right = loaderPoolFindAnimationByCatalogId(ids[1]);
	const struct guncmd *parent = loaderPoolFindAnimationByCatalogId(ids[2]);
	if (!left || !right || !parent || left == right || left[0].unk04 != 1 || right[0].unk04 != 2
			|| parent[0].unk04 != (intptr_t)left || parent[1].unk04 != (intptr_t)right) goto done;
	if (harnessReadCommands(directory, "rejected.json", ids[0])
			|| loaderPoolFindAnimationByCatalogId(ids[0]) != left || left[0].unk04 != 1) goto done;
	if (harnessReadCommands(directory, "cycle.json", ids[0])
			|| loaderPoolFindAnimationByCatalogId(ids[0]) != left || parent[0].unk04 != (intptr_t)left) goto done;
	if (!harnessReadCommands(directory, "replaced.json", ids[0])) goto done;
	loaderPoolFinalize();
	const struct guncmd *replacement = loaderPoolFindAnimationByCatalogId(ids[0]);
	if (!replacement || replacement == left || replacement[0].unk04 != 3 || left[0].unk04 != 1
			|| parent[0].unk04 != (intptr_t)replacement || parent[1].unk04 != (intptr_t)right) goto done;
	if (!harnessBatchBoundaries(directory)) goto done;
	ok = 1;
done:
	while (created > 0) if (!assetCatalogRollbackUnactivatedRegistration(ids[--created])) ok = 0;
	if (!loaderPoolSnapshotRestore(snapshot)) ok = 0;
	loaderPoolSnapshotDestroy(snapshot);
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING, "PDWEAPON.COMMAND_SOURCE.HARNESS: result=%s", ok ? "PASS" : "FAIL");
	return ok;
}

static s32 harnessMeshAccept(const char *owner, const char *archive,
		const char *mesh_id, const char *payload_id, const char *expected_text,
		s32 reject_geometry)
{
	/* Exercise the ordinary native consumer without valid hand/model storage:
	 * both representations of an empty authored list must return before access. */
	struct gunviscmd empty_visibility = {0};
	empty_visibility.type = GUNVISCMD_END;
	bgunExecuteGunVisCommands(NULL, NULL, NULL);
	bgunExecuteGunVisCommands(NULL, NULL, &empty_visibility);
	sysLogPrintf(LOG_NOTE, "PDWEAPON.MESH.EMPTY_VISIBILITY: owner=%s result=PASS", owner);
	char err[512] = {0};
	harness_deps_t deps = {0};
	s32 weapon_slot = -1, mp_slot = -1, loaded = 0, ok = 0;
	char *count_text = NULL;
	long edge_value = strtol(expected_text ? expected_text : "", &count_text, 10);
	long expected_models = count_text && *count_text == ',' ? strtol(count_text + 1, NULL, 10) : 0;
	s32 expected_edge = edge_value > 0 && edge_value <= 32767 ? (s32)edge_value : -1;
	const s32 baseline_edges = catalogDepCount();
	void *model_slots = assetCatalogSnapshotCustomModelSlots();
	void *weapon_slots = assetCatalogSnapshotCustomWeaponSlots();
	if (!model_slots || !weapon_slots || expected_edge <= 0) goto done;
	if (!assetCatalogResolveWeaponPrivateSlots(owner, -1, 0, &weapon_slot, &mp_slot)
			|| weapon_slot < WEAPON_CUSTOM_START) goto done;
	asset_entry_t *entry = harnessRegisterOwner(owner, archive);
	if (!entry) goto done;
	entry->runtime_index = weapon_slot;
	entry->mp_index = mp_slot;
	entry->ext.weapon.weapon_id = mp_slot;
	if (assetCatalogRegisterWeaponNestedDependencies(owner, archive, 0, err, sizeof(err)) < 0) goto done;
	catalogDepForEach(owner, harnessCollectDep, &deps);
	if (deps.invalid || harnessFind(&deps, mesh_id, ASSET_MODEL) < 0
			|| !catalogDepContains(payload_id, mesh_id)) goto done;
	if (expected_models > 0) {
		size_t model_count = 0;
		for (size_t i = 0; i < deps.count; i++) if (deps.types[i] == ASSET_MODEL) model_count++;
		if (model_count != (size_t)expected_models) goto done;
	}
	const asset_entry_t *mesh = assetCatalogResolve(mesh_id);
	if (!mesh || mesh->runtime_index < MODEL_CUSTOM_START || mesh->source_filenum <= 0
			|| mesh->source.primary.provider != fileProvider()) goto done;
	const char *path = fileProviderPath(mesh->source.primary);
	if (!path || !strstr(path, ".pdprojectile::") || !strstr(path, ".pdmesh::")) goto done;
	/* Ordinary scanner callers rebuild their hot indexes after publication. */
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	if (weaponGraphRuntimeRegisterWeaponArchive(weapon_slot, archive, err, sizeof(err)) != 0) goto done;
	const weapon_graph_projectile_runtime_t *projectile = weaponGraphRuntimeGetProjectile(payload_id);
	mesh = assetCatalogResolve(mesh_id);
	if (!projectile || !projectile->has_projectile_modelnum || !mesh
			|| projectile->projectile_modelnum != mesh->runtime_index) goto done;
	const asset_entry_t *held = assetCatalogResolve(owner);
	char held_id[CATALOG_ID_LEN];
	snprintf(held_id, sizeof(held_id), "%s_held_selected", owner);
	const asset_entry_t *held_mesh = assetCatalogResolve(held_id);
	if (!held || !held_mesh || held->source_filenum != held_mesh->source_filenum) goto done;
	loaded = catalogLoadTypedAsset(ASSET_MODEL, mesh_id);
	if (reject_geometry) {
		mesh = assetCatalogResolve(mesh_id);
		ok = !loaded && mesh && mesh->load_state < ASSET_STATE_LOADED
			&& mesh->payload_kind == ASSET_PAYLOAD_NONE && !mesh->loaded_data;
	} else {
		ok = loaded && harnessMeshVertices(catalogGetLoadedModeldef(mesh_id), expected_edge);
	}
done:
	if (loaded) catalogReleaseTypedAsset(ASSET_MODEL, mesh_id);
	if (weapon_slot >= WEAPON_CUSTOM_START) weaponGraphRuntimeClearWeapon(weapon_slot);
	if (!harnessCleanup(owner, &deps)) ok = 0;
	if (model_slots && !assetCatalogRestoreCustomModelSlots(model_slots)) ok = 0;
	if (weapon_slots && !assetCatalogRestoreCustomWeaponSlots(weapon_slots)) ok = 0;
	assetCatalogDestroyCustomModelSlotSnapshot(model_slots);
	assetCatalogDestroyCustomWeaponSlotSnapshot(weapon_slots);
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	if (catalogDepCount() != baseline_edges) ok = 0;
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
		"PDWEAPON.MESH.HARNESS: owner=%s case=%s result=%s error=%s",
		owner, reject_geometry ? "malformed_geometry" : "source_to_native_record",
		ok ? "PASS" : "FAIL", err[0] ? err : "(none)");
	return ok;
}

static s32 harnessBaseMeshPreserve(const char *owner, const char *archive,
		const char *high_id, const char *low_id)
{
	const asset_entry_t *high = assetCatalogResolve(high_id);
	const asset_entry_t *low = assetCatalogResolve(low_id);
	if (!high || !low) return 0;
	const s32 high_runtime = high->runtime_index, low_runtime = low->runtime_index;
	const s32 high_file = high->source_filenum, low_file = low->source_filenum;
	const asset_data_handle_t high_source = high->source.primary, low_source = low->source.primary;
	char err[512] = {0};
	s32 ok = assetCatalogRegisterWeaponNestedDependencies(owner, archive, 1, err, sizeof(err)) >= 0;
	high = assetCatalogResolve(high_id);
	low = assetCatalogResolve(low_id);
	ok = ok && high && low && high->runtime_index == high_runtime && low->runtime_index == low_runtime
		&& high->source_filenum == high_file && low->source_filenum == low_file
		&& memcmp(&high->source.primary, &high_source, sizeof(high_source)) == 0
		&& memcmp(&low->source.primary, &low_source, sizeof(low_source)) == 0;
	sysLogPrintf(ok ? LOG_NOTE : LOG_WARNING,
		"PDWEAPON.MESH.HARNESS: owner=%s case=base_identity result=%s high=%d low=%d error=%s",
		owner, ok ? "PASS" : "FAIL", high_file, low_file, err);
	return ok;
}

static s32 harnessMeshSlotRollback(const char *owner, const char *archive, char *absent_csv)
{
	void *snapshot = assetCatalogSnapshotCustomModelSlots();
	if (!snapshot) return 0;
	s32 expected = assetCatalogResolveModelPrivateSlot("meshproof:rollback_probe");
	s32 ok = expected >= MODEL_CUSTOM_START && assetCatalogRestoreCustomModelSlots(snapshot);
	if (ok) ok = harnessReject(owner, archive, absent_csv);
	if (ok) ok = assetCatalogResolveModelPrivateSlot("meshproof:rollback_probe") == expected;
	if (!assetCatalogRestoreCustomModelSlots(snapshot)) ok = 0;
	assetCatalogDestroyCustomModelSlotSnapshot(snapshot);
	catalogBuildRuntimeCaches();
	catalogLoadInit();
	return ok;
}

s32 weaponNestedRuntimeHarnessRun(const char *plan_path)
{
    FILE *f;
    char line[4096];
    s32 cases = 0;
    s32 passed = 0;
    if (!plan_path || !plan_path[0]) return 0;
    f = fopen(plan_path, "rb");
    if (!f) f = fsFileOpenRead(plan_path);
    if (!f) return 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\r' || line[len - 1] == '\n'))
            line[--len] = '\0';
        if (!line[0] || line[0] == '#') continue;
        char *mode = line;
        char *owner = strchr(mode, '|');
        if (!owner) continue;
        *owner++ = '\0';
        char *archive = strchr(owner, '|');
        if (!archive) continue;
        *archive++ = '\0';
        char *arg1 = strchr(archive, '|');
        if (!arg1) continue;
        *arg1++ = '\0';
        char *arg2 = strchr(arg1, '|');
        if (arg2) *arg2++ = '\0';
        char *arg3 = arg2 ? strchr(arg2, '|') : NULL;
        if (arg3) *arg3++ = '\0';
        cases++;
        if (strcmp(mode, "accept") == 0 && arg2
                && harnessAccept(owner, archive, arg1, arg2)) passed++;
        else if (strcmp(mode, "reject") == 0
                && harnessReject(owner, archive, arg1)) passed++;
        else if (strcmp(mode, "capacity") == 0 && arg2 && arg3
                && harnessCapacityAccept(owner, archive, arg1, arg2, arg3)) passed++;
        else if (strcmp(mode, "capacity_reject") == 0 && arg2 && arg3
                && harnessCapacityReject(owner, archive, arg1, arg2, arg3)) passed++;
        else if (strcmp(mode, "mesh_accept") == 0 && arg2 && arg3
                && harnessMeshAccept(owner, archive, arg1, arg2, arg3, 0)) passed++;
        else if (strcmp(mode, "mesh_load_reject") == 0 && arg2 && arg3
                && harnessMeshAccept(owner, archive, arg1, arg2, arg3, 1)) passed++;
        else if (strcmp(mode, "mesh_slots_reject") == 0
                && harnessMeshSlotRollback(owner, archive, arg1)) passed++;
        else if (strcmp(mode, "mesh_base") == 0 && arg2
                && harnessBaseMeshPreserve(owner, archive, arg1, arg2)) passed++;
        else if (strcmp(mode, "model_inputs") == 0
                && harnessModelInputs(owner, archive)) passed++;
        else if (strcmp(mode, "model_generation") == 0
                && harnessModelGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "texture_generation") == 0
                && harnessTextureGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "texture_runtime") == 0
                && harnessTextureRuntime(owner, archive)) passed++;
        else if (strcmp(mode, "command_generation") == 0
                && harnessCommandGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "audio_generation") == 0
                && harnessAudioGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "animation_generation") == 0
                && harnessAnimationGeneration(owner, archive)) passed++;
        else if (strcmp(mode, "command_import") == 0
                && harnessCommandImport(owner, archive, arg1)) passed++;
        else if (strcmp(mode, "command_source") == 0
                && harnessCommandSources(archive)) passed++;
    }
    fclose(f);
    sysLogPrintf(passed == cases && cases > 0 ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.NESTED.HARNESS: passed=%d cases=%d result=%s",
        passed, cases, passed == cases && cases > 0 ? "PASS" : "FAIL");
    return passed == cases && cases > 0;
}
