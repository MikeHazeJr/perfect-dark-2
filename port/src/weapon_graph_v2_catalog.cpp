/* Executable graph catalog: immutable program candidate ownership and exact retirement.
 * Single main-thread mutation owner. No native adapter or gameplay claim. */
#include "weapon_graph_v2_catalog.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>

struct wg_v2_catalog_entry {
    size_t refs = 1;
    uint64_t registry_identity = 0;
    std::string id;
    int function = 0;
    wg_v2_program *program = nullptr;
    void *lease = nullptr;
    wg_v2_lease_release release = nullptr;
    bool accepting = false, published_once = false;
    ~wg_v2_catalog_entry() {
        wgV2ProgramRelease(program);
        if (release) release(lease);
    }
};
struct wg_v2_catalog {
    struct Slot { wg_v2_catalog_entry *active = nullptr; uint64_t last_published = 0; };
    struct Modes { Slot functions[2]; };
    uint64_t identity = 0;
    size_t active_count = 0;
    wg_v2_retire_hook retire;
    void *host;
    std::unordered_map<std::string, Modes> slots;
};
namespace {
std::atomic<uint64_t> registry_ids{0};
std::atomic<uint64_t> source_generations{0};
uint64_t nextIdentity(std::atomic<uint64_t> &counter) {
    uint64_t prior = counter.load();
    do {
        if (prior == UINT64_MAX) return 0;
    } while (!counter.compare_exchange_weak(prior, prior + 1));
    return prior + 1;
}
void fail(char *error, size_t cap, const char *message) {
    if (error && cap) std::snprintf(error, cap, "%s", message);
}
bool idValid(const char *id) {
    if (!id) return false;
    size_t n = 0;
    while (n < CATALOG_ID_LEN && id[n]) ++n;
    if (!n || n == CATALOG_ID_LEN) return false;
    const char *colon = std::strchr(id, ':');
    return colon && colon != id && colon[1] && !std::strchr(colon + 1, ':');
}
void retireEntry(wg_v2_catalog *catalog, wg_v2_catalog_entry *entry) {
    entry->accepting = false;
    catalog->retire(catalog->host, entry->id.c_str(), wgV2ProgramGeneration(entry->program));
    wgV2CatalogEntryRelease(entry);
}
}
extern "C" wg_v2_catalog *wgV2CatalogCreate(wg_v2_retire_hook hook, void *host) {
    if (!hook) return nullptr;
    try {
        auto value = std::make_unique<wg_v2_catalog>();
        value->identity = nextIdentity(registry_ids);
        if (!value->identity) return nullptr;
        value->retire = hook; value->host = host;
        return value.release();
    } catch (...) { return nullptr; }
}
extern "C" int wgV2CheckUse(const wg_v2_use *use, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!use) { fail(error, cap, "v2 use requires explicit execution context"); return 0; }
    if (use->network_active) { fail(error, cap, "held_single_shot.v1: network execution is not implemented"); return 0; }
    if (use->recording_or_replaying) { fail(error, cap, "held_single_shot.v1: Theater execution is not implemented"); return 0; }
    if (use->bot_owner) { fail(error, cap, "held_single_shot.v1: bot execution is not implemented"); return 0; }
    if (use->function < 0 || use->function > 1) {
        fail(error, cap, "held_single_shot.v1: expected primary or secondary function"); return 0;
    }
    return 1;
}
extern "C" int wgV2CatalogCanEnterUse(const wg_v2_catalog *catalog,
        const wg_v2_use *use, char *error, size_t cap) {
    if (!catalog || !use) { fail(error, cap, "invalid v2 request context"); return 0; }
    if (!catalog->active_count) { if (error && cap) error[0] = 0; return 1; }
    return wgV2CheckUse(use, error, cap);
}
static wg_v2_catalog_entry *prepareCandidate(wg_v2_catalog *catalog,
        const char *id, const char *json, size_t size, const char *digest,
        const wg_v2_use *use, void *lease, wg_v2_lease_release release,
        wg_v2_lease_prepare prepare, void *prepare_host,
        char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!catalog || !idValid(id) || (!lease && !prepare) || !release) {
        fail(error, cap, "v2 candidate requires catalog ID and prepared dependency lease");
        return nullptr;
    }
    if (!wgV2CheckUse(use, error, cap)) return nullptr;
    const uint64_t generation = nextIdentity(source_generations);
    if (!generation) {
        fail(error, cap, "v2 source generation exhausted"); return nullptr;
    }
    try {
        auto entry = std::make_unique<wg_v2_catalog_entry>();
        entry->id = id; entry->registry_identity = catalog->identity;
        entry->function = use->function;
        /* Failed candidates still consume generation identities. */
        entry->program = wgV2Compile(json, size, generation, digest, error, cap);
        if (!entry->program) return nullptr;
        const char *mode = use->function == 0 ? "primary" : "secondary";
        if (std::strcmp(wgV2ProgramAssetId(entry->program), id) ||
                std::strcmp(wgV2ProgramMode(entry->program), mode)) {
            fail(error, cap, "v2 program identity/mode differs from selected catalog function");
            return nullptr;
        }
        /* Reserve publication storage before ownership transfer. Empty slots
         * are tombstones only and never appear as admitted active assets. */
        catalog->slots.emplace(entry->id, wg_v2_catalog::Modes{});
        if (prepare) {
            lease = prepare(prepare_host, entry->program, error, cap);
            if (!lease) return nullptr;
        }
        entry->lease = lease; entry->release = release;
        return entry.release();
    } catch (...) { fail(error, cap, "v2 candidate allocation failed"); return nullptr; }
}
extern "C" wg_v2_catalog_entry *wgV2CatalogPrepare(wg_v2_catalog *catalog,
        const char *id, const char *json, size_t size, const char *digest,
        const wg_v2_use *use, void *lease, wg_v2_lease_release release,
        char *error, size_t cap) {
    return prepareCandidate(catalog, id, json, size, digest, use, lease, release,
        nullptr, nullptr, error, cap);
}
extern "C" wg_v2_catalog_entry *wgV2CatalogPrepareWithLease(wg_v2_catalog *catalog,
        const char *id, const char *json, size_t size, const char *digest,
        const wg_v2_use *use, wg_v2_lease_prepare prepare, void *host,
        wg_v2_lease_release release, char *error, size_t cap) {
    return prepareCandidate(catalog, id, json, size, digest, use, nullptr, release,
        prepare, host, error, cap);
}
extern "C" int wgV2CatalogCanPublish(const wg_v2_catalog *catalog, const wg_v2_catalog_entry *entry,
        char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!catalog || !entry || entry->registry_identity != catalog->identity || entry->published_once) {
        fail(error, cap, "v2 publication requires a fresh candidate owned by this catalog"); return 0;
    }
    const auto prior = catalog->slots.find(entry->id);
    if (prior == catalog->slots.end()) {
        fail(error, cap, "v2 candidate publication slot was not prepared"); return 0;
    }
    if (prior->second.functions[entry->function].last_published >=
            wgV2ProgramGeneration(entry->program)) {
        fail(error, cap, "v2 candidate is older than the active generation"); return 0;
    }
    return 1;
}
extern "C" int wgV2CatalogPublish(wg_v2_catalog *catalog, wg_v2_catalog_entry *entry,
        char *error, size_t cap) {
    if (!wgV2CatalogCanPublish(catalog, entry, error, cap)) return 0;
    {
        auto prior = catalog->slots.find(entry->id);
        auto &slot = prior->second.functions[entry->function];
        wg_v2_catalog_entry *old = slot.active;
        wgV2CatalogEntryRetain(entry);
        entry->accepting = true; entry->published_once = true;
        slot.active = entry;
        slot.last_published = wgV2ProgramGeneration(entry->program);
        if (!old) ++catalog->active_count;
        if (old) retireEntry(catalog, old);
        return 1;
    }
}
extern "C" wg_v2_catalog_entry *wgV2CatalogAcquire(wg_v2_catalog *catalog, const char *id) {
    return wgV2CatalogAcquireFunction(catalog, id, 0);
}
extern "C" wg_v2_catalog_entry *wgV2CatalogAcquireFunction(wg_v2_catalog *catalog,
        const char *id, int function) {
    if (!catalog || !id || function < 0 || function > 1) return nullptr;
    try {
        const auto found = catalog->slots.find(id);
        if (found == catalog->slots.end() || !found->second.functions[function].active) return nullptr;
        auto *entry = found->second.functions[function].active;
        wgV2CatalogEntryRetain(entry);
        return entry;
    } catch (...) { return nullptr; }
}
extern "C" int wgV2CatalogRetire(wg_v2_catalog *catalog, const char *id) {
    const int primary = wgV2CatalogRetireFunction(catalog, id, 0);
    const int secondary = wgV2CatalogRetireFunction(catalog, id, 1);
    return primary || secondary;
}
extern "C" int wgV2CatalogRetireFunction(wg_v2_catalog *catalog,
        const char *id, int function) {
    if (!catalog || !id || function < 0 || function > 1) return 0;
    try {
        const auto found = catalog->slots.find(id);
        if (found == catalog->slots.end()) return 0;
        auto &slot = found->second.functions[function];
        if (!slot.active) return 0;
        wg_v2_catalog_entry *entry = slot.active;
        slot.active = nullptr;
        --catalog->active_count;
        retireEntry(catalog, entry);
        return 1;
    } catch (...) { return 0; }
}
extern "C" void wgV2CatalogRetireAll(wg_v2_catalog *catalog) {
    if (!catalog) return;
    for (auto &row : catalog->slots) for (auto &slot : row.second.functions) {
        auto *entry = slot.active;
        if (!entry) continue;
        slot.active = nullptr;
        --catalog->active_count;
        retireEntry(catalog, entry);
    }
}
extern "C" void wgV2CatalogDestroy(wg_v2_catalog *catalog) { wgV2CatalogRetireAll(catalog); delete catalog; }
extern "C" size_t wgV2CatalogCount(const wg_v2_catalog *c) { return c ? c->active_count : 0; }
extern "C" void wgV2CatalogEntryRetain(wg_v2_catalog_entry *e) { if (e) ++e->refs; }
extern "C" void wgV2CatalogEntryRelease(wg_v2_catalog_entry *e) { if (e && --e->refs == 0) delete e; }
extern "C" const char *wgV2CatalogEntryAssetId(const wg_v2_catalog_entry *e) { return e ? e->id.c_str() : ""; }
extern "C" int wgV2CatalogEntryFunction(const wg_v2_catalog_entry *e) { return e ? e->function : -1; }
extern "C" uint64_t wgV2CatalogEntryGeneration(const wg_v2_catalog_entry *e) { return e ? wgV2ProgramGeneration(e->program) : 0; }
extern "C" int wgV2CatalogEntryAccepting(const wg_v2_catalog_entry *e) { return e && e->accepting; }
extern "C" wg_v2_program *wgV2CatalogEntryProgram(const wg_v2_catalog_entry *e) { return e ? e->program : nullptr; }
extern "C" void *wgV2CatalogEntryLease(const wg_v2_catalog_entry *e) { return e ? e->lease : nullptr; }
