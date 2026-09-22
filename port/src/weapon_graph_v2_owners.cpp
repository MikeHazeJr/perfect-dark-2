#include "weapon_graph_v2_owners.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace {
struct Slot {
    wg_v2_catalog_entry *entry = nullptr;
    wg_v2_instance *instance = nullptr;
    void *host = nullptr;
    wg_v2_host_destroy destroy_host = nullptr;
    void retire(const char *reason) {
        if (instance) {
            wgV2Cancel(instance, reason);
            wgV2Destroy(instance);
            instance = nullptr;
        }
        if (destroy_host) { destroy_host(host); destroy_host = nullptr; host = nullptr; }
        wgV2CatalogEntryRelease(entry); entry = nullptr;
    }
    ~Slot() { retire("owner registry storage retired"); }
};
struct Owner {
    uint64_t generation = 0;
    std::unique_ptr<Slot> hands[2];
};
std::atomic<uint64_t> owner_generations{0};
void fail(char *error, size_t cap, const char *message) {
    if (error && cap) std::snprintf(error, cap, "%s", message);
}
}
struct wg_v2_owners { std::unordered_map<const void *, std::unique_ptr<Owner>> owners; };
namespace {
Owner *find(wg_v2_owners *registry, const void *owner, uint64_t generation) {
    if (!registry || !owner || !generation) return nullptr;
    auto found = registry->owners.find(owner);
    return found != registry->owners.end() && found->second->generation == generation
        ? found->second.get() : nullptr;
}
}
extern "C" wg_v2_owners *wgV2OwnersCreate(void) {
    try { return new wg_v2_owners; } catch (...) { return nullptr; }
}
extern "C" uint64_t wgV2OwnersBeginOwner(wg_v2_owners *registry, const void *owner) {
    if (!registry || !owner || registry->owners.count(owner)) return 0;
    try {
        auto record = std::make_unique<Owner>();
        uint64_t prior = owner_generations.load();
        do { if (prior == UINT64_MAX) return 0; }
        while (!owner_generations.compare_exchange_weak(prior, prior + 1));
        record->generation = prior + 1;
        registry->owners.emplace(owner, std::move(record));
        return prior + 1;
    } catch (...) { return 0; }
}
extern "C" uint64_t wgV2OwnersGeneration(const wg_v2_owners *registry, const void *owner) {
    if (!registry || !owner) return 0;
    const auto found = registry->owners.find(owner);
    return found == registry->owners.end() ? 0 : found->second->generation;
}
extern "C" wg_v2_instance *wgV2OwnersBind(wg_v2_owners *registry, const void *owner,
        uint64_t generation, int hand, wg_v2_catalog_entry *entry, const wg_v2_host *ops,
        void *host, wg_v2_host_destroy destroy_host, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    Owner *record = find(registry, owner, generation);
    if (!record || hand < 0 || hand > 1 || !entry || !ops || !host || !destroy_host ||
            !wgV2CatalogEntryAccepting(entry)) {
        fail(error, cap, "v2 bind requires live owner/hand and accepting exact source generation");
        return nullptr;
    }
    try {
        auto candidate = std::make_unique<Slot>();
        candidate->instance = wgV2Bind(wgV2CatalogEntryProgram(entry), ops, host,
            generation, error, cap);
        if (!candidate->instance) return nullptr;
        wgV2CatalogEntryRetain(entry); candidate->entry = entry;
        candidate->host = host; candidate->destroy_host = destroy_host;
        /* Preparation is complete and has caused no native gameplay side effect.
         * Retire the old binding before the new host becomes discoverable. */
        if (record->hands[hand]) record->hands[hand]->retire("hand source/function replacement");
        record->hands[hand] = std::move(candidate);
        return record->hands[hand]->instance;
    } catch (...) { fail(error, cap, "v2 owner binding allocation failed"); return nullptr; }
}
extern "C" wg_v2_instance *wgV2OwnersFind(wg_v2_owners *registry, const void *owner,
        uint64_t generation, int hand, uint64_t source_generation, void **out_host) {
    if (out_host) *out_host = nullptr;
    Owner *record = find(registry, owner, generation);
    if (!record || hand < 0 || hand > 1) return nullptr;
    Slot *slot = record->hands[hand].get();
    if (!slot || !wgV2CatalogEntryAccepting(slot->entry) ||
            wgV2CatalogEntryGeneration(slot->entry) != source_generation) return nullptr;
    if (out_host) *out_host = slot->host;
    return slot->instance;
}
extern "C" void wgV2OwnersRetireHand(wg_v2_owners *registry, const void *owner,
        uint64_t generation, int hand, const char *reason) {
    Owner *record = find(registry, owner, generation);
    if (!record || hand < 0 || hand > 1 || !record->hands[hand]) return;
    record->hands[hand]->retire(reason); record->hands[hand].reset();
}
extern "C" void wgV2OwnersRetireOwner(wg_v2_owners *registry, const void *owner,
        uint64_t generation, const char *reason) {
    Owner *record = find(registry, owner, generation);
    if (!record) return;
    for (auto &slot : record->hands) if (slot) slot->retire(reason);
    registry->owners.erase(owner);
}
extern "C" void wgV2OwnersRetireSource(void *value, const char *id, uint64_t generation) {
    auto *registry = static_cast<wg_v2_owners *>(value);
    if (!registry || !id || !generation) return;
    for (auto &owner : registry->owners) for (auto &slot : owner.second->hands) {
        if (slot && wgV2CatalogEntryGeneration(slot->entry) == generation &&
                std::strcmp(wgV2CatalogEntryAssetId(slot->entry), id) == 0) {
            slot->retire("catalog source generation retired"); slot.reset();
        }
    }
}
extern "C" void wgV2OwnersRetireAll(wg_v2_owners *registry, const char *reason) {
    if (!registry) return;
    for (auto &owner : registry->owners)
        for (auto &slot : owner.second->hands) if (slot) slot->retire(reason);
    registry->owners.clear();
}
extern "C" void wgV2OwnersDestroy(wg_v2_owners *registry) {
    wgV2OwnersRetireAll(registry, "owner registry destroyed"); delete registry;
}
