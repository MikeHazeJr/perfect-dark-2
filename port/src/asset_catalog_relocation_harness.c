#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asset_catalog_relocation_harness.h"
#include "assetcatalog.h"
#include "smoke_harness.h"
#include "system.h"

typedef struct cache_witness {
    char id[CATALOG_ID_LEN];
    asset_type_e type;
    s32 pool_index;
    s32 runtime_index;
    s32 mp_index;
    uintptr_t old_address;
} cache_witness_t;

static s32 selectWitness(asset_type_e type, cache_witness_t *out)
{
    for (s32 i = 0; i < assetCatalogGetPoolSize(); ++i) {
        const asset_entry_t *entry = assetCatalogGetByIndex(i);
        if (!entry || entry->type != type || entry->runtime_index < 0
                || entry->runtime_index >= 1024) continue;
        const char *runtime_id = catalogIdByRuntime(type, entry->runtime_index);
        if (!runtime_id || strcmp(runtime_id, entry->id)) continue;
        if (type == ASSET_BODY || type == ASSET_HEAD) {
            const char *mp_id = type == ASSET_BODY
                ? catalogMpBodyId(entry->mp_index) : catalogMpHeadId(entry->mp_index);
            if (!mp_id || strcmp(mp_id, entry->id)) continue;
        }
        memset(out, 0, sizeof(*out));
        strcpy(out->id, entry->id);
        out->type = type;
        out->pool_index = i;
        out->runtime_index = entry->runtime_index;
        out->mp_index = entry->mp_index;
        out->old_address = (uintptr_t)entry;
        return 1;
    }
    return 0;
}

static s32 matchesFresh(const cache_witness_t *witness, const char *actual)
{
    const asset_entry_t *fresh = assetCatalogGetByIndex(witness->pool_index);
    /* Compare address before reading actual: a stale cached pointer must fail
     * deterministically even if freed bytes happen to retain the old string. */
    return fresh && (uintptr_t)fresh != witness->old_address
        && fresh->type == witness->type && actual == fresh->id
        && !strcmp(fresh->id, witness->id);
}

static s32 record(const char *name, s32 ok)
{
    sysLogPrintf(LOG_NOTE, "ASSET.CATALOG.RELOCATION: case=%s result=%s",
        name, ok ? "PASS" : "FAIL");
    return ok;
}

static s32 rehashCases(void)
{
    s32 capacity = 0, initial_hash = 0, occupied = 0, added = 0;
    char id[CATALOG_ID_LEN];
    if (!assetCatalogDebugStorageForSmoke(&capacity, &initial_hash) || initial_hash <= 0) return 0;
    for (s32 i = 0; i < assetCatalogGetPoolSize(); ++i)
        if (assetCatalogGetByIndex(i)) ++occupied;
    /* Register only enough real rows to reach the next actual occupied-slot
     * threshold. These inert rows have no native bindings or runtime payloads;
     * this standalone smoke exits immediately after the proof. */
    while (((uint64_t)occupied + 1) * 100 / (u32)initial_hash <= 70) {
        snprintf(id, sizeof(id), "smoke:catalog_rehash_%d", added);
        if (assetCatalogResolve(id) || !assetCatalogRegister(id, ASSET_MODEL)) return 0;
        ++occupied; ++added;
    }
    snprintf(id, sizeof(id), "smoke:catalog_rehash_%d", added);
    s32 before_size = assetCatalogGetPoolSize();
    s32 before_capacity = 0, before_hash = 0, after_capacity = 0, after_hash = 0;
    if (assetCatalogResolve(id) || !assetCatalogDebugStorageForSmoke(&before_capacity, &before_hash)
            || before_hash != initial_hash || !assetCatalogDebugFailNextHashGrowthForSmoke()) return 0;
    asset_entry_t *failed = assetCatalogRegister(id, ASSET_MODEL);
    if (!record("rehash_failure_publishes_no_row", !failed && !assetCatalogResolve(id)
            && assetCatalogGetPoolSize() == before_size
            && assetCatalogDebugStorageForSmoke(&after_capacity, &after_hash)
            && after_capacity == before_capacity && after_hash == before_hash)) return 0;
    asset_entry_t *entry = assetCatalogRegister(id, ASSET_MODEL);
    if (!entry || !entry->occupied || !entry->enabled || strcmp(entry->id, id)
            || assetCatalogResolve(id) != entry || entry->type != ASSET_MODEL
            || entry->runtime_index != -1 || entry->load_state != ASSET_STATE_REGISTERED
            || !assetCatalogDebugStorageForSmoke(&after_capacity, &after_hash)
            || after_hash != initial_hash * 2 || assetCatalogGetPoolSize() != before_size + 1) return 0;
    for (s32 i = 0; i <= added; ++i) {
        snprintf(id, sizeof(id), "smoke:catalog_rehash_%d", i);
        entry = assetCatalogGetMutable(id);
        if (!entry || !entry->occupied || entry->type != ASSET_MODEL || strcmp(entry->id, id)) return 0;
    }
    sysLogPrintf(LOG_NOTE, "ASSET.CATALOG.RELOCATION: rehash_rows=%d hash_before=%d hash_after=%d",
        added + 1, initial_hash, after_hash);
    return record("successful_threshold_rehash_keeps_every_row", 1);
}

int assetCatalogRelocationHarnessRun(void)
{
    static const char marker_id[] = "smoke:catalog_relocation_marker";
    static const char late_id[] = "smoke:catalog_relocation_late";
    cache_witness_t body, head, model;
    s32 runtime_index = -1, rebased = 0, ok = 0;
    s32 marker_registered = 0, late_registered = 0;
    if (!smokeHarnessIsActive() || assetCatalogResolve(marker_id)
            || assetCatalogResolve(late_id)) return -1;
    for (s32 i = 1023; i >= 0; --i) {
        if (!catalogIdByRuntime(ASSET_CHARACTER, i)) { runtime_index = i; break; }
    }
    if (runtime_index < 0) goto done;
    asset_entry_t *marker = assetCatalogRegister(marker_id, ASSET_CHARACTER);
    if (!marker) goto done;
    marker_registered = 1;
    marker->runtime_index = runtime_index;
    /* A controlled lifecycle marker fixture proves relocation ownership. It
     * deliberately makes no claim about character hydration or source parsing. */
    marker->loaded_data = marker;
    marker->payload_kind = ASSET_PAYLOAD_RUNTIME_ACTIVE;
    marker->load_state = ASSET_STATE_ACTIVE;
    marker->ref_count = 1;
    catalogBuildRuntimeCaches();
    if (!selectWitness(ASSET_BODY, &body) || !selectWitness(ASSET_HEAD, &head)
            || !selectWitness(ASSET_MODEL, &model)) goto done;
    u32 generation = assetCatalogGetGeneration();
    s32 pool_size = assetCatalogGetPoolSize();
    uintptr_t old_marker = (uintptr_t)marker;
    if (!assetCatalogDebugRelocatePoolForSmoke(&rebased)) goto done;
    /* No cache rebuild after relocation: these are the actual public getters. */
    if (!record("runtime_cache_relocation",
            matchesFresh(&body, catalogIdByRuntime(ASSET_BODY, body.runtime_index))
            && matchesFresh(&head, catalogIdByRuntime(ASSET_HEAD, head.runtime_index))
            && matchesFresh(&model, catalogIdByRuntime(ASSET_MODEL, model.runtime_index)))) goto done;
    if (!record("body_selector_relocation", matchesFresh(&body, catalogMpBodyId(body.mp_index)))) goto done;
    if (!record("head_selector_relocation", matchesFresh(&head, catalogMpHeadId(head.mp_index)))) goto done;
    marker = assetCatalogGetMutable(marker_id);
    if (!record("self_marker_and_catalog_identity", marker && (uintptr_t)marker != old_marker
            && marker->loaded_data == marker && marker->payload_kind == ASSET_PAYLOAD_RUNTIME_ACTIVE
            && marker->ref_count == 1 && marker->load_state == ASSET_STATE_ACTIVE && rebased >= 1
            && catalogIdByRuntime(ASSET_CHARACTER, runtime_index) == marker->id
            && assetCatalogGetPoolSize() == pool_size && assetCatalogGetGeneration() == generation)) goto done;
    /* Retire the controlled marker, then register a different row at the same
     * private index. Both occupied validation and the existing scan fallback
     * are necessary; the old encoded cache slot must not hide the late row. */
    marker->loaded_data = NULL;
    marker->payload_kind = ASSET_PAYLOAD_NONE;
    marker->load_state = ASSET_STATE_REGISTERED;
    marker->ref_count = 0;
    if (!assetCatalogUnregister(marker_id)) goto done;
    marker_registered = 0;
    if (catalogIdByRuntime(ASSET_CHARACTER, runtime_index)) goto done;
    asset_entry_t *late = assetCatalogRegister(late_id, ASSET_CHARACTER);
    if (!late) goto done;
    late_registered = 1;
    late->runtime_index = runtime_index;
    if (!record("removed_cache_slot_and_late_row_fallback",
            catalogIdByRuntime(ASSET_CHARACTER, runtime_index) == late->id)) goto done;
    /* Same-row borrowed IDs must be copied before override clears the row. */
    late = assetCatalogRegister(late->id, ASSET_CHARACTER);
    if (!record("borrowed_id_survives_same_row_override", late && !strcmp(late->id, late_id)
            && assetCatalogResolve(late_id) == late && late->runtime_index == -1)) goto done;
    if (!rehashCases()) goto done;
    ok = 1;
done:
    if (marker_registered) {
        asset_entry_t *entry = assetCatalogGetMutable(marker_id);
        if (entry) {
            entry->loaded_data = NULL; entry->payload_kind = ASSET_PAYLOAD_NONE;
            entry->load_state = ASSET_STATE_REGISTERED; entry->ref_count = 0;
        }
        assetCatalogUnregister(marker_id);
    }
    if (late_registered) assetCatalogUnregister(late_id);
    if (ok) sysLogPrintf(LOG_NOTE, "ASSET.CATALOG.RELOCATION: cases=8 result=PASS");
    else sysLogPrintf(LOG_NOTE, "ASSET.CATALOG.RELOCATION: result=FAIL");
    return ok ? 0 : -1;
}
