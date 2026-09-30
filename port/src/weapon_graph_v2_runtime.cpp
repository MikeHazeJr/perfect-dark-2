/* Client-thread production host for held_single_shot.v1. Public sources are
 * prepared completely before publication. Native firing owns ammunition debit;
 * the executor retains each action through both noise and deferred hit use. */
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "weapon_graph_v2_runtime.h"
#include "weapon_graph_v2_ingress.h"
#include "weapon_graph_v2_equipped.h"
#include "weapon_graph_v2_gset.h"
#include "weapon_graph_v2_owners.h"
#include "catalog_command_generation.h"
#include "catalog_model_generation.h"
#include "body_head_source.h"
#include "assetprovider.h"
#include "modarchive.h"
#include "modasset_gltf_document.h"
#include "crude_json.h"
#include "fs.h"
#include "system.h"
#include "sha256.h"
#include "types.h"
#undef bool
extern "C" {
extern struct g_vars g_Vars;
extern s32 g_NetMode;
extern u16 g_CartFileNums[];
void bgunResetAnim(struct hand *);
s32 theaterIsRecording(void);
s32 theaterIsReplaying(void);
}
namespace {
using Entry = std::unique_ptr<wg_v2_catalog_entry, decltype(&wgV2CatalogEntryRelease)>;
struct Host;
struct Player {
    struct player *native = nullptr;
    uint64_t generation = 0, sequence[2]{};
    Host *hands[2]{};
};
struct Record {
    std::string id;
    wg_v2_catalog_entry *modes[2]{};
    void release() { for (auto &entry : modes) { wgV2CatalogEntryRelease(entry); entry = nullptr; } }
};
struct Runtime {
    wg_v2_owners *owners = wgV2OwnersCreate();
    wg_v2_catalog *catalog = wgV2CatalogCreate(wgV2OwnersRetireSource, owners);
    wg_v2_gsets *gsets = nullptr;
    std::array<Record, WEAPON_CUSTOM_END> records;
    std::unordered_map<struct player *, std::unique_ptr<Player>> players;
};
Runtime *runtime = nullptr;
int fail(char *error, size_t cap, const char *message) {
    if (error && cap) std::snprintf(error, cap, "%s", message);
    return 0;
}
void reject(const char *s) { throw std::runtime_error(s); }
Record *record(int weapon) {
    return runtime && weapon >= 0 && weapon < WEAPON_CUSTOM_END ? &runtime->records[weapon] : nullptr;
}
const char *classify(void *, int weapon) {
    const auto *r = record(weapon);
    return r && !r->id.empty() ? r->id.c_str() : nullptr;
}
bool initialize() {
    if (runtime) return runtime->owners && runtime->catalog && runtime->gsets;
    try {
        auto value = std::make_unique<Runtime>();
        value->gsets = wgV2GsetsCreate(classify, nullptr);
        if (!value->owners || !value->catalog || !value->gsets) {
            wgV2GsetsDestroy(value->gsets); wgV2CatalogDestroy(value->catalog); wgV2OwnersDestroy(value->owners);
            return false;
        }
        runtime = value.release(); return true;
    } catch (...) { return false; }
}
wg_v2_equipped *equipment(wg_v2_catalog_entry *entry) {
    return static_cast<wg_v2_equipped *>(wgV2CatalogEntryLease(entry));
}
Player *playerRecord(struct player *native) {
    if (!runtime || !native) return nullptr;
    const auto it = runtime->players.find(native);
    return it == runtime->players.end() ? nullptr : it->second.get();
}
struct Host {
    Player *owner;
    int hand;
    wg_v2_catalog_entry *entry;
    wg_v2_gset_token token = 0;
    wg_v2_instance *instance = nullptr;
    uint64_t visit = 0, step = 0, ticket = 0;
    bool pending = false, noise = false, shot = false, finished = false;
    struct hand &native() { return owner->native->hands[hand]; }
};
Host *hostFor(struct player *native, int hand) {
    auto *p = playerRecord(native);
    return p && hand >= 0 && hand < 2 ? p->hands[hand] : nullptr;
}
int alive(void *value, uint64_t generation) {
    auto &h = *static_cast<Host *>(value);
    return !h.owner->native->isdead && h.owner->generation == generation && h.owner->hands[h.hand] == &h &&
        wgV2CatalogEntryAccepting(h.entry) && wgV2GsetSource(runtime->gsets, &h.native().gset) == h.entry;
}
int prepare(void *value, const wg_v2_action *action, char *error, size_t cap) {
    auto &h = *static_cast<Host *>(value);
    return wgV2NativeFind(wgV2EquippedActions(equipment(h.entry)), action->source_node_id) != nullptr
        ? 1 : fail(error, cap, "native action was not prepared with this exact equipment source");
}
int gate(void *value, wg_v2_gate kind, int slot, int required, char *, size_t) {
    auto &h = *static_cast<Host *>(value);
    if (kind == WG_V2_COOLDOWN_READY) return h.native().state == HANDSTATE_IDLE && !h.pending;
    if (slot == -1) return 1;
    return slot >= 0 && slot < 2 && h.native().loadedammo[static_cast<size_t>(slot)] >= required;
}
wg_v2_result start(void *value, const wg_v2_action *action, uint64_t ticket,
        void **state, char *error, size_t cap) {
    auto &h = *static_cast<Host *>(value);
    auto *bundle = wgV2EquippedActions(equipment(h.entry));
    auto *native = wgV2NativeFind(bundle, action->source_node_id);
    const auto *function = wgV2NativeFunction(native);
    *state = nullptr;
    if (!function) return WG_V2_FAILED;
    if (function->ammoindex >= 0 && h.native().loadedammo[function->ammoindex] <= 0) return WG_V2_BLOCKED;
    if (!wgV2GsetSelect(runtime->gsets, h.token, h.entry, bundle, native, error, cap)) return WG_V2_FAILED;
    h.ticket = ticket; h.pending = h.noise = h.shot = h.finished = false;
    if (!bgunGraphV2Accept(h.owner->native, h.hand)) return WG_V2_BLOCKED;
    *state = &h; return WG_V2_RUNNING;
}
wg_v2_result tick(void *value, const wg_v2_action *, uint64_t, void *, double, char *, size_t) {
    auto &h = *static_cast<Host *>(value);
    h.finished = bgunGraphV2Tick(h.owner->native, h.hand) != 0;
    if (h.native().firing) {
        h.pending = true; h.noise = h.shot = false;
        return WG_V2_WAITING;
    }
    return h.finished ? WG_V2_COMPLETED : WG_V2_RUNNING;
}
wg_v2_result wake(void *value, const wg_v2_action *, uint64_t, void *, uint32_t, char *, size_t) {
    auto &h = *static_cast<Host *>(value);
    return h.pending ? WG_V2_WAITING : h.finished ? WG_V2_COMPLETED : WG_V2_RUNNING;
}
void cancel(void *value, const wg_v2_action *, uint64_t, void *, const char *) {
    auto &h = *static_cast<Host *>(value); bgunGraphV2Cancel(h.owner->native, h.hand);
    h.pending = false;
}
void cleanup(void *value, const wg_v2_action *, uint64_t, void *) {
    auto &h = *static_cast<Host *>(value);
    bgunGraphV2Cancel(h.owner->native, h.hand);
    h.pending = false; h.ticket = 0;
    if (wgV2CatalogEntryAccepting(h.entry)) wgV2GsetBindSource(runtime->gsets, h.token, h.entry, nullptr, 0);
}
void destroyHost(void *value) {
    std::unique_ptr<Host> h(static_cast<Host *>(value));
    bgunGraphV2Cancel(h->owner->native, h->hand);
    wgV2GsetClose(runtime->gsets, h->token);
    if (h->owner->hands[h->hand] == h.get()) h->owner->hands[h->hand] = nullptr;
}
const wg_v2_host operations{alive, prepare, gate, start, tick, wake, cancel, cleanup, nullptr};
void consumed(Host *h) {
    if (!h || !h->pending || !h->noise || !h->shot) return;
    h->pending = false; h->native().firing = false;
    wgV2Wake(h->instance, h->ticket, 1, h->owner->generation, wgV2CatalogEntryGeneration(h->entry));
}
std::string member(const void *archive, u32 size, const char *path) {
    u32 length = 0;
    std::unique_ptr<void, decltype(&std::free)> bytes(modArchiveExtractMemAlloc(archive, size, path, &length), std::free);
    if (!bytes || !length) reject("missing selected public source member");
    return std::string(static_cast<const char *>(bytes.get()), length);
}
struct Models {
    std::vector<catalog_model_generation_t *> values;
    ~Models() { for (auto *g : values) catalogModelGenerationRelease(g); }
    catalog_model_generation_t *add(const asset_entry_t *entry, const char *path, char *error, size_t cap) {
        auto *g = catalogModelGenerationAcquireSource(entry, path, error, cap);
        if (!g) reject(error && error[0] ? error : "public model source could not be prepared");
        try { values.push_back(g); } catch (...) { catalogModelGenerationRelease(g); throw; }
        return g;
    }
};
void releaseModels(void *v) { delete static_cast<Models *>(v); }
}
extern "C" int wgV2RuntimeIsWeapon(int weapon) { return classify(nullptr, weapon) != nullptr; }
extern "C" int wgV2RuntimeRegisterArchive(int weapon, const char *id, const char *path, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    try {
        // Base numeric weapons still carry special native behavior; activating
        // a v2 override there requires their remaining policy seams to be moved.
        if (weapon < WEAPON_CUSTOM_START || weapon >= WEAPON_CUSTOM_END || !id || !path)
            reject("held_single_shot.v1 production currently requires a private custom weapon slot");
        if (!initialize()) reject("could not allocate executable graph registries");
        auto &r = runtime->records[weapon];
        if (!r.id.empty() && r.id != id) reject("retained v2 slot cannot change catalog identity");
        u32 size = 0;
        std::unique_ptr<void, decltype(&sysMemFree)> bytes(fsFileLoad(path, &size), sysMemFree);
        weapon_graph_archive_descriptor_t descriptor{};
        if (wgV2ArchiveInspect(bytes.get(), size, &descriptor, error, cap) != 1) return 0;
        if (std::strcmp(id, descriptor.catalog_id)) reject("weapon catalog identity differs from captured public descriptor");
        auto models = std::make_unique<Models>();
        const auto mesh = member(bytes.get(), size, descriptor.model_file);
        const auto ini_bytes = member(mesh.data(), static_cast<u32>(mesh.size()), "mesh.ini");
        ini_section_t ini{};
        // Use the same public [model] descriptor as modelSourceResolvePath;
        // a permissive alternate section would pass admission then fail conversion.
        if (!bodyHeadSourceReadIniBytes(ini_bytes.data(), static_cast<u32>(ini_bytes.size()), "model", &ini, error, cap)) return 0;
        const char *model_id = iniGet(&ini, "catalog_id", iniGet(&ini, "id", ""));
        const auto *model_entry = assetCatalogResolve(model_id);
        if (!model_entry || model_entry->type != ASSET_MODEL) reject("selected held mesh has no prepared exact catalog row");
        const auto model_path = std::string(path) + "::" + descriptor.model_file;
        auto *held = models->add(model_entry, model_path.c_str(), error, cap);
        const auto settings = member(bytes.get(), size, descriptor.settings);
        crude_json::value json;
        if (!modAssetJsonReadValue(settings.data(), settings.size(), json)) reject("invalid equipped settings");
        struct modeldef *casings[2]{};
        const auto &ammo = json["equipped"]["ammo"];
        for (int slot = 0; slot < 2; ++slot) {
            const auto &a = ammo.is_array() && ammo.get<crude_json::array>().size() == 2 ? ammo[static_cast<size_t>(slot)] : slot == 0 ? ammo : crude_json::value{};
            if (!a.is_object() || !a.contains("casing") || !a["casing"].is_string()) continue;
            const auto &kind = a["casing"].get<crude_json::string>();
            const int index = kind == "standard" ? CASING_STANDARD : kind == "reaper" ? CASING_REAPER :
                kind == "rifle" ? CASING_RIFLE : kind == "shotgun" ? CASING_SHOTGUN : -1;
            if (index < 0) continue; // Full equipped parser rejects unknown values.
            const auto handle = catalogHandleByModelSourceFilenum(ASSET_MODEL, g_CartFileNums[index]);
            const char *casing_id = catalogIdBySourceHandle(ASSET_MODEL, handle);
            const auto *entry = casing_id ? assetCatalogResolve(casing_id) : nullptr;
            const char *source = fileProviderPath(handle);
            if (!entry || !source) reject("authored casing requires extracted public model source");
            auto *generation = models->add(entry, source, error, cap);
            casings[slot] = catalogModelGenerationModeldef(generation);
        }
        sha256_ctx hash; sha256Init(&hash);
        for (auto *g : models->values) {
            const char *a = catalogModelGenerationId(g), *b = catalogModelGenerationHash(g);
            sha256Update(&hash, a, std::strlen(a) + 1); sha256Update(&hash, b, std::strlen(b) + 1);
        }
        u8 raw[SHA256_DIGEST_SIZE]; char closure[SHA256_HEX_SIZE]; sha256Final(&hash, raw); sha256ToHex(raw, closure);
        wg_v2_equipped_model input{descriptor.model_file, catalogModelGenerationId(held), closure,
            0, models.get(), releaseModels, catalogModelGenerationModeldef(held), weapon, {casings[0], casings[1]}};
        const wg_v2_use use{g_NetMode != 0, theaterIsRecording() || theaterIsReplaying(), 0, 0};
        wg_v2_catalog_entry *candidates[2]{};
        if (!wgV2EquippedCatalogPrepareArchiveModes(runtime->catalog, id, bytes.get(), size, closure, &use,
                &input, catalogGraphNativeResolve, nullptr, candidates, error, cap)) return 0;
        models.release();
        Entry primary(candidates[0], wgV2CatalogEntryRelease), secondary(candidates[1], wgV2CatalogEntryRelease);
        u32 current_size = 0;
        std::unique_ptr<void, decltype(&sysMemFree)> current(fsFileLoad(path, &current_size), sysMemFree);
        if (!current || current_size != size || std::memcmp(bytes.get(), current.get(), size)) reject("weapon source changed during native preparation");
        if (!wgV2CatalogCanPublishModes(runtime->catalog, primary.get(), secondary.get(), error, cap)) return 0;
        std::string identity(id); // Finish all allocations before publication.
        if (!wgV2CatalogPublishModes(runtime->catalog, primary.get(), secondary.get(), error, cap)) return 0;
        r.release(); r.id.swap(identity); r.modes[0] = primary.release(); r.modes[1] = secondary.release();
        sysLogPrintf(LOG_NOTE, "GRAPH.V2.ACTIVATE: %s captured-source modes=%d", id, r.modes[1] ? 2 : 1);
        return 1;
    } catch (const std::exception &e) { return fail(error, cap, e.what()); }
    catch (...) { return fail(error, cap, "executable weapon preparation allocation failed"); }
}
extern "C" void wgV2RuntimeRetireWeapon(int weapon, const char *id) {
    auto *r = record(weapon);
    if (r && id && r->id == id) { wgV2CatalogRetire(runtime->catalog, id); r->release(); }
}
extern "C" void wgV2RuntimeRetirePlayers(void) {
    if (!runtime) return;
    wgV2OwnersRetireAll(runtime->owners, "player stage storage retired"); runtime->players.clear();
}
extern "C" void wgV2RuntimeReset(void) {
    if (!runtime) return;
    wgV2RuntimeRetirePlayers(); wgV2GsetsRetireAll(runtime->gsets); wgV2CatalogRetireAll(runtime->catalog);
    for (auto &r : runtime->records) { r.release(); r.id.clear(); }
}
extern "C" const struct weapon *wgV2RuntimeWeapon(int weapon) {
    auto *r = record(weapon);
    return r && wgV2CatalogEntryAccepting(r->modes[0]) ? wgV2EquippedWeapon(equipment(r->modes[0])) : nullptr;
}
extern "C" const struct weapon *wgV2RuntimeGsetWeapon(const struct gset *gset) {
    return runtime ? wgV2EquippedWeapon(equipment(wgV2GsetSource(runtime->gsets, gset))) : nullptr;
}
extern "C" const struct weaponfunc *wgV2RuntimeGsetFunction(const struct gset *gset) {
    if (!runtime || !gset) return nullptr;
    const wg_v2_native_action *action = nullptr;
    const auto status = wgV2GsetLookup(runtime->gsets, gset, &action);
    if (status == WG_V2_GSET_SELECTED) return wgV2NativeFunction(action);
    const auto *w = status == WG_V2_GSET_UNSELECTED ? wgV2RuntimeGsetWeapon(gset) : nullptr;
    return w && gset->weaponfunc < 2 ? static_cast<const weaponfunc *>(w->functions[gset->weaponfunc]) : nullptr;
}
extern "C" const weapon_graph_held_function_t *wgV2RuntimeGsetHeld(const struct gset *gset) {
    const wg_v2_native_action *action = nullptr;
    return runtime && wgV2GsetLookup(runtime->gsets, gset, &action) == WG_V2_GSET_SELECTED ? wgV2NativeHeld(action) : nullptr;
}
extern "C" struct modeldef *wgV2RuntimeModel(int weapon) {
    auto *r = record(weapon);
    return r && wgV2CatalogEntryAccepting(r->modes[0]) ? wgV2EquippedModeldef(equipment(r->modes[0])) : nullptr;
}
extern "C" struct modeldef *wgV2RuntimeCasing(const struct gset *gset) {
    const auto *f = wgV2RuntimeGsetFunction(gset);
    auto *model = f && runtime ? wgV2EquippedCasingModeldef(equipment(wgV2GsetSource(runtime->gsets, gset)), f->ammoindex) : nullptr;
    auto *generation = catalogModelGenerationForModeldef(model);
    return generation && catalogModelGenerationPinStage(generation) ? model : nullptr;
}
extern "C" int wgV2RuntimeCanEnterUse(int network, int theater, char *error, size_t cap) {
    if (!runtime) return 1;
    const wg_v2_use use{network, theater, 0, 0}; return wgV2CatalogCanEnterUse(runtime->catalog, &use, error, cap);
}
extern "C" void wgV2RuntimePlayerBegin(struct player *native) {
    if (!native || !initialize()) return;
    if (auto *prior = playerRecord(native)) wgV2OwnersRetireOwner(runtime->owners, native, prior->generation, "player lifetime replacement");
    runtime->players.erase(native);
    try {
        auto p = std::make_unique<Player>(); p->native = native;
        const auto inserted = runtime->players.emplace(native, std::move(p));
        inserted.first->second->generation = wgV2OwnersBeginOwner(runtime->owners, native);
        if (!inserted.first->second->generation) runtime->players.erase(native);
    } catch (...) { sysLogPrintf(LOG_WARNING, "GRAPH.V2.OWNER: explicit player lifetime could not be allocated"); }
}
extern "C" void wgV2RuntimeRetireHand(struct player *native, int hand, const char *reason) {
    if (auto *p = playerRecord(native)) wgV2OwnersRetireHand(runtime->owners, native, p->generation, hand, reason);
}
extern "C" int wgV2RuntimeEnsureHand(struct player *native, int hand) {
    auto *p = playerRecord(native);
    if (!p || hand < 0 || hand > 1) return 0;
    if (native->isdead) { wgV2RuntimeRetireHand(native, hand, "native owner died"); return 0; }
    auto &gset = native->hands[hand].gset;
    auto *r = record(gset.weaponnum);
    auto *entry = r && gset.weaponfunc < 2 ? r->modes[gset.weaponfunc] : nullptr;
    char error[512]{};
    if (!entry || !wgV2CatalogEntryAccepting(entry) || !wgV2RuntimeCanEnterUse(g_NetMode != 0,
            theaterIsRecording() || theaterIsReplaying(), error, sizeof(error))) {
        wgV2RuntimeRetireHand(native, hand, "hand source/use no longer admitted"); return 0;
    }
    if (auto *h = p->hands[hand]) {
        if (h->entry == entry && wgV2GsetSource(runtime->gsets, &gset) == entry) return 1;
        wgV2RuntimeRetireHand(native, hand, "hand identity/function changed");
    }
    try {
        auto h = std::make_unique<Host>(); h->owner = p; h->hand = hand; h->entry = entry;
        h->token = wgV2GsetOpen(runtime->gsets, &gset, native, p->generation, error, sizeof(error));
        if (!h->token) return 0;
        if (!wgV2GsetBindSource(runtime->gsets, h->token, entry, error, sizeof(error))) {
            wgV2GsetClose(runtime->gsets, h->token); return 0;
        }
        p->hands[hand] = h.get(); // Explicit lifetime is live during executor bind admission.
        h->instance = wgV2OwnersBind(runtime->owners, native, p->generation, hand, entry, &operations,
            h.get(), destroyHost, error, sizeof(error));
        if (!h->instance) { p->hands[hand] = nullptr; wgV2GsetClose(runtime->gsets, h->token); return 0; }
        p->hands[hand] = h.release(); return 1;
    } catch (...) { return 0; }
}
extern "C" void wgV2RuntimeInput(struct player *native, int hand, int pressed, int released) {
    if (!wgV2RuntimeEnsureHand(native, hand)) return;
    auto *h = hostFor(native, hand);
    if (released) wgV2ReleasePendingPress(h->instance);
    if (pressed && h->owner->sequence[hand] != UINT64_MAX) wgV2Press(h->instance, ++h->owner->sequence[hand],
        h->owner->generation, wgV2CatalogEntryGeneration(h->entry));
}
extern "C" int wgV2RuntimeIdle(struct player *native, int hand) {
    if (!wgV2RuntimeEnsureHand(native, hand)) return 0;
    auto *h = hostFor(native, hand);
    if (h->visit == UINT64_MAX) return 0;
    wgV2NativeIdle(h->instance, ++h->visit, h->native().triggeron,
        h->owner->generation, wgV2CatalogEntryGeneration(h->entry));
    return h->native().state == HANDSTATE_ATTACK;
}
extern "C" int wgV2RuntimeAdvance(struct player *native, int hand) {
    auto *h = hostFor(native, hand);
    if (!h || h->pending || h->step == UINT64_MAX) return 0;
    wgV2Advance(h->instance, ++h->step, g_Vars.lvupdate60freal,
        h->owner->generation, wgV2CatalogEntryGeneration(h->entry)); return 0;
}
extern "C" int wgV2RuntimeDeliveryPending(struct player *native, int hand) { auto *h = hostFor(native, hand); return h && h->pending; }
extern "C" int wgV2RuntimeNoiseAdmit(struct player *native, int hand) { auto *h = hostFor(native, hand); return !h || (h->pending && !h->noise); }
extern "C" void wgV2RuntimeNoiseConsumed(struct player *native, int hand) { auto *h = hostFor(native, hand); if (h && h->pending) { h->noise = true; consumed(h); } }
extern "C" void wgV2RuntimeShotConsumed(struct player *native, int hand) { auto *h = hostFor(native, hand); if (h && h->pending) { h->shot = true; consumed(h); } }
extern "C" uint64_t wgV2RuntimeScopeOpen(struct player *native, int hand, struct gset *destination) {
    auto *h = hostFor(native, hand);
    if (!h || !destination) return 0;
    char error[256]{};
    const auto token = wgV2GsetOpen(runtime->gsets, destination, destination, h->owner->generation, error, sizeof(error));
    if (token && wgV2GsetCopy(runtime->gsets, token, &h->native().gset, error, sizeof(error))) return token;
    if (token) wgV2GsetClose(runtime->gsets, token);
    sysLogPrintf(LOG_WARNING, "GRAPH.V2.COPY: %s", error); return 0;
}
extern "C" void wgV2RuntimeScopeClose(uint64_t token) { if (runtime && token) wgV2GsetClose(runtime->gsets, token); }
