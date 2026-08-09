#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_deps.h"
#include "assetcatalog_scanner.h"
#include "fs.h"
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

static void harnessCleanup(const char *owner, const harness_deps_t *deps)
{
    if (deps) {
        for (size_t i = deps->count; i-- > 0; ) {
            harness_deps_t children = {0};
            catalogDepForEach(deps->ids[i], harnessCollectDep, &children);
            for (size_t j = children.count; j-- > 0; ) {
                catalogDepUnregister(deps->ids[i], children.ids[j]);
            }
            free(children.ids);
            free(children.types);
            catalogDepUnregister(owner, deps->ids[i]);
            assetCatalogUnregister(deps->ids[i]);
        }
    }
    assetCatalogUnregister(owner);
    if (deps) {
        free(deps->ids);
        free(deps->types);
    }
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
    s32 ok = 0;
    if (expected <= 64 || expected > INT32_MAX
            || expected_effects < 2 || expected_effects > INT32_MAX
            || expected_effect_deps <= 64 || expected_effect_deps > INT32_MAX
            ) goto done;
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
    ok = 1;
done:
    if (!ok) {
        sysLogPrintf(LOG_WARNING,
            "PDWEAPON.NESTED.HARNESS.FAIL: mode=capacity owner=%s archive=%s error=%s",
            owner, archive, err[0] ? err : "capacity assertion failed");
    }
    free(effect_deps.ids);
    free(effect_deps.types);
    harnessCleanup(owner, &deps);
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
    harnessCleanup(owner, &deps);
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
    harnessCleanup(owner, &deps);
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
    harnessCleanup(owner, &deps);
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
    }
    fclose(f);
    sysLogPrintf(passed == cases && cases > 0 ? LOG_NOTE : LOG_WARNING,
        "PDWEAPON.NESTED.HARNESS: passed=%d cases=%d result=%s",
        passed, cases, passed == cases && cases > 0 ? "PASS" : "FAIL");
    return passed == cases && cases > 0;
}
