/* explicit address lifetimes, immutable selected-action copying.
 * No change to four-byte native, AI, setup-cache, network or Theater layouts. */
#include "weapon_graph_v2_gset.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>
#include "types.h"
#undef bool
static_assert(sizeof(struct gset) == 4, "gset copy bridge preserves the native four-byte contract");

namespace {
std::atomic<uint64_t> token_counter{1};
uint64_t nextToken() {
    auto value = token_counter.load();
    while (value && value != UINT64_MAX) {
        if (token_counter.compare_exchange_weak(value, value + 1)) return value;
    }
    return 0;
}
int fail(char *error, size_t cap, const char *text) {
    if (error && cap) std::snprintf(error, cap, "%s", text);
    return 0;
}
struct Slot {
    wg_v2_gset_token token = 0;
    struct gset *address = nullptr;
    const void *owner = nullptr;
    uint64_t owner_generation = 0;
    int weapon = 0, function = 0;
    wg_v2_native_bundle *bundle = nullptr;
    const wg_v2_native_action *action = nullptr;
    ~Slot() { wgV2NativeRelease(bundle); }
    void assign(wg_v2_native_bundle *b, const wg_v2_native_action *a) {
        wgV2NativeRetain(b); wgV2NativeRelease(bundle); bundle = b; action = a;
    }
};
}
struct wg_v2_gsets {
    wg_v2_gset_classify classify;
    void *host;
    std::unordered_map<const struct gset *, std::unique_ptr<Slot>> slots;
};
namespace {
Slot *findToken(wg_v2_gsets *r, wg_v2_gset_token token) {
    if (r && token) for (auto &row : r->slots) if (row.second->token == token) return row.second.get();
    return nullptr;
}
bool identity(const Slot &s) {
    return s.address->weaponnum == s.weapon && s.address->weaponfunc == s.function;
}
}
extern "C" wg_v2_gsets *wgV2GsetsCreate(wg_v2_gset_classify classify, void *host) {
    if (!classify) return nullptr;
    try { auto result = std::make_unique<wg_v2_gsets>(); result->classify = classify; result->host = host; return result.release(); }
    catch (...) { return nullptr; }
}
extern "C" void wgV2GsetsDestroy(wg_v2_gsets *r) { delete r; }
extern "C" wg_v2_gset_token wgV2GsetOpen(wg_v2_gsets *r, struct gset *value,
        const void *owner, uint64_t generation, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (!r || !value || !owner || !generation) return fail(error, cap, "gset requires explicit live storage/owner generation");
    if (r->slots.count(value)) return fail(error, cap, "gset address already has a live lifetime");
    const auto token = nextToken();
    if (!token) return fail(error, cap, "gset lifetime identity exhausted");
    try {
        auto slot = std::make_unique<Slot>(); slot->token = token; slot->address = value;
        slot->owner = owner; slot->owner_generation = generation;
        slot->weapon = value->weaponnum; slot->function = value->weaponfunc;
        r->slots.emplace(value, std::move(slot)); return token;
    } catch (...) { return fail(error, cap, "gset lifetime allocation failed"); }
}
extern "C" wg_v2_gset_status wgV2GsetLookup(const wg_v2_gsets *r,
        const struct gset *value, const wg_v2_native_action **out) {
    if (out) *out = nullptr;
    if (!r || !value) return WG_V2_GSET_INVALID;
    const auto found = r->slots.find(value);
    const char *id = r->classify(r->host, value->weaponnum);
    if (found == r->slots.end()) return id ? WG_V2_GSET_INVALID : WG_V2_GSET_BASE;
    const Slot &slot = *found->second;
    if (!identity(slot)) return WG_V2_GSET_INVALID;
    if (!slot.action) return id ? WG_V2_GSET_UNSELECTED : WG_V2_GSET_BASE;
    const char *mode = slot.function == 0 ? "primary" : slot.function == 1 ? "secondary" : nullptr;
    if (!id || !slot.bundle || !mode ||
            std::strcmp(mode, wgV2ProgramMode(wgV2NativeProgram(slot.bundle))) ||
            std::strcmp(id, wgV2ProgramAssetId(wgV2NativeProgram(slot.bundle)))) return WG_V2_GSET_INVALID;
    if (out) *out = slot.action;
    return WG_V2_GSET_SELECTED;
}
extern "C" int wgV2GsetSelect(wg_v2_gsets *r, wg_v2_gset_token token,
        wg_v2_catalog_entry *entry, wg_v2_native_bundle *bundle,
        const wg_v2_native_action *action, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    Slot *slot = findToken(r, token);
    if (!slot || !identity(*slot) || !entry || !wgV2CatalogEntryAccepting(entry) ||
            wgV2CatalogEntryProgram(entry) != wgV2NativeProgram(bundle) ||
            !wgV2NativeContains(bundle, action) ||
            wgV2CatalogEntryFunction(entry) != slot->function)
        return fail(error, cap, "gset selection requires this live source generation and matching registered function lifetime");
    const char *id = r->classify(r->host, slot->weapon);
    if (!id || std::strcmp(id, wgV2CatalogEntryAssetId(entry)))
        return fail(error, cap, "gset numeric slot does not identify the selected v2 catalog source");
    slot->assign(bundle, action); return 1;
}
extern "C" int wgV2GsetCopy(wg_v2_gsets *r, wg_v2_gset_token token,
        const struct gset *source, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    Slot *destination = findToken(r, token);
    if (!destination || !source || !identity(*destination)) return fail(error, cap, "gset copy requires an unchanged registered destination");
    const wg_v2_native_action *action = nullptr;
    const auto status = wgV2GsetLookup(r, source, &action);
    if (status == WG_V2_GSET_INVALID) return fail(error, cap, "gset copy source lacks valid explicit v2 identity");
    wg_v2_native_bundle *bundle = nullptr;
    if (status == WG_V2_GSET_SELECTED) bundle = r->slots.find(source)->second->bundle;
    /* Capture bytes before assignment, permitting a self-copy. Retain precedes
     * release, so two slots already sharing the last bundle ref remain safe. */
    const struct gset bytes = *source;
    destination->assign(bundle, action);
    *destination->address = bytes;
    destination->weapon = bytes.weaponnum; destination->function = bytes.weaponfunc;
    return 1;
}
extern "C" void wgV2GsetClose(wg_v2_gsets *r, wg_v2_gset_token token) {
    Slot *slot = findToken(r, token);
    if (slot) r->slots.erase(slot->address); /* never dereference retired native storage */
}
extern "C" void wgV2GsetsRetireOwner(wg_v2_gsets *r, const void *owner, uint64_t generation) {
    if (!r || !owner || !generation) return;
    for (auto it = r->slots.begin(); it != r->slots.end(); ) {
        if (it->second->owner == owner && it->second->owner_generation == generation) it = r->slots.erase(it);
        else ++it;
    }
}
extern "C" void wgV2GsetsRetireAll(wg_v2_gsets *r) { if (r) r->slots.clear(); }
extern "C" size_t wgV2GsetsCount(const wg_v2_gsets *r) { return r ? r->slots.size() : 0; }
