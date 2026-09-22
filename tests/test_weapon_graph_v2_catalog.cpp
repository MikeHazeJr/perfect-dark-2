/* Real draft compiler + registry execution. Leases model only dependency
 * ownership; no fake shot is presented as native weapon gameplay. */
#include "catch.hpp"
#include "weapon_graph_v2_catalog.h"
#include "weapon_graph_v2_owners.h"
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char *source = R"({"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",
"asset_id":"mod:single","graph_id":"primary","mode":"primary",
"nodes":[{"id":"press","kind":"event.trigger_pressed"},
{"id":"ammo","kind":"gate.ammo_available","params":{"ammo_slot":0,"required":1}},
{"id":"shot","kind":"fire.hitscan","module":"og.fire.hitscan","module_version":1,
"params":{"mode":"primary","function_type":"shoot_single","ammo_slot":0,"flags":0,"damage":1.5}}],
"edges":[{"from":"press","output":"exec","to":"ammo","input":"exec"},
{"from":"ammo","output":"pass","to":"shot","input":"exec"}],
"exports":[{"name":"trigger_pressed","node":"press"}]})";
struct Lease { int *released; ~Lease() { ++*released; } };
void release(void *v) { delete static_cast<Lease *>(v); }
struct Host {
    std::vector<uint64_t> retired;
    int released = 0;
    int live_during_retire = 0;
    bool wrong_id = false;
    Host() { retired.reserve(8); }
};
void retire(void *v, const char *id, uint64_t generation) {
    auto &h = *static_cast<Host *>(v);
    h.wrong_id = h.wrong_id || std::strcmp(id, "mod:single") != 0;
    h.retired.push_back(generation);
    h.live_during_retire = h.released;
}
using Registry = std::unique_ptr<wg_v2_catalog, decltype(&wgV2CatalogDestroy)>;
using Entry = std::unique_ptr<wg_v2_catalog_entry, decltype(&wgV2CatalogEntryRelease)>;
Registry registry(Host &host) { return Registry(wgV2CatalogCreate(retire, &host), wgV2CatalogDestroy); }
Entry prepare(wg_v2_catalog *c, Host &h) {
    auto lease = std::make_unique<Lease>(); lease->released = &h.released;
    wg_v2_use use{}; char error[256]{};
    Entry result(wgV2CatalogPrepare(c, "mod:single", source, std::char_traits<char>::length(source),
        digest, &use, lease.get(), release, error, sizeof(error)), wgV2CatalogEntryRelease);
    INFO(error);
    REQUIRE(result);
    lease.release(); return result;
}
Entry prepareSecondary(wg_v2_catalog *c, Host &h) {
    std::string graph(source);
    const auto change = [&graph](const char *from, const char *to) {
        const auto at = graph.find(from);
        REQUIRE(at != std::string::npos);
        graph.replace(at, std::strlen(from), to);
    };
    change("\"graph_id\":\"primary\"", "\"graph_id\":\"secondary\"");
    change("\"mode\":\"primary\"", "\"mode\":\"secondary\"");
    change("\"mode\":\"primary\"", "\"mode\":\"secondary\"");
    auto lease = std::make_unique<Lease>(); lease->released = &h.released;
    wg_v2_use use{}; use.function = 1; char error[256]{};
    Entry result(wgV2CatalogPrepare(c, "mod:single", graph.data(), graph.size(),
        digest, &use, lease.get(), release, error, sizeof(error)), wgV2CatalogEntryRelease);
    INFO(error); REQUIRE(result);
    lease.release(); return result;
}
}

TEST_CASE("v2 primary and secondary catalog functions retain and retire independently",
        "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h); REQUIRE(c);
    auto primary = prepare(c.get(), h), secondary = prepareSecondary(c.get(), h);
    char error[256]{};
    REQUIRE(wgV2CatalogPublish(c.get(), primary.get(), error, sizeof(error)));
    REQUIRE(wgV2CatalogPublish(c.get(), secondary.get(), error, sizeof(error)));
    REQUIRE(wgV2CatalogCount(c.get()) == 2);
    Entry selectedPrimary(wgV2CatalogAcquire(c.get(), "mod:single"), wgV2CatalogEntryRelease);
    Entry selectedSecondary(wgV2CatalogAcquireFunction(c.get(), "mod:single", 1),
        wgV2CatalogEntryRelease);
    REQUIRE(selectedPrimary.get() == primary.get());
    REQUIRE(selectedSecondary.get() == secondary.get());
    REQUIRE_FALSE(wgV2CatalogAcquireFunction(c.get(), "mod:single", 2));
    REQUIRE(wgV2CatalogRetireFunction(c.get(), "mod:single", 1));
    REQUIRE(wgV2CatalogCount(c.get()) == 1);
    REQUIRE_FALSE(wgV2CatalogAcquireFunction(c.get(), "mod:single", 1));
    Entry remaining(wgV2CatalogAcquire(c.get(), "mod:single"), wgV2CatalogEntryRelease);
    REQUIRE(remaining.get() == primary.get());
    REQUIRE(wgV2CatalogRetire(c.get(), "mod:single"));
    REQUIRE(wgV2CatalogCount(c.get()) == 0);
    REQUIRE_FALSE(wgV2CatalogEntryAccepting(selectedPrimary.get()));
    REQUIRE_FALSE(wgV2CatalogEntryAccepting(selectedSecondary.get()));
}

TEST_CASE("v2 catalog accepts extracted base weapon identity without weakening exact ID checks",
        "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h); REQUIRE(c);
    std::string graph(source);
    const auto at = graph.find("mod:single"); REQUIRE(at != std::string::npos);
    graph.replace(at, std::strlen("mod:single"), "base:falcon2");
    wg_v2_use use{}; char error[256]{};
    auto lease = std::make_unique<Lease>(); lease->released = &h.released;
    Entry base(wgV2CatalogPrepare(c.get(), "base:falcon2", graph.data(), graph.size(),
        digest, &use, lease.get(), release, error, sizeof(error)), wgV2CatalogEntryRelease);
    INFO(error); REQUIRE(base); lease.release();
    REQUIRE(wgV2CatalogEntryFunction(base.get()) == 0);
    REQUIRE(wgV2CatalogPublish(c.get(), base.get(), error, sizeof(error)));
    Entry acquired(wgV2CatalogAcquire(c.get(), "base:falcon2"), wgV2CatalogEntryRelease);
    REQUIRE(acquired.get() == base.get());
    REQUIRE_FALSE(wgV2CatalogAcquire(c.get(), "base:falcon3"));
    REQUIRE(wgV2CatalogRetire(c.get(), "base:falcon2"));
    REQUIRE_FALSE(wgV2CatalogEntryAccepting(acquired.get()));
    auto rejected = std::make_unique<Lease>(); rejected->released = &h.released;
    REQUIRE_FALSE(wgV2CatalogPrepare(c.get(), "base::falcon2", graph.data(), graph.size(),
        digest, &use, rejected.get(), release, error, sizeof(error)));
    REQUIRE(std::string(error).find("catalog ID") != std::string::npos);
}

TEST_CASE("v2 catalog replacement retains exact old program lease through consumer ownership", "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h); REQUIRE(c);
    auto first = prepare(c.get(), h);
    const uint64_t first_generation = wgV2CatalogEntryGeneration(first.get());
    char error[256]{};
    REQUIRE(wgV2CatalogPublish(c.get(), first.get(), error, sizeof(error)));
    Entry hand(wgV2CatalogAcquire(c.get(), "mod:single"), wgV2CatalogEntryRelease);
    REQUIRE(hand);
    first.reset();
    auto second = prepare(c.get(), h);
    REQUIRE(wgV2CatalogEntryGeneration(second.get()) > first_generation);
    REQUIRE(wgV2CatalogPublish(c.get(), second.get(), error, sizeof(error)));
    REQUIRE(h.retired == std::vector<uint64_t>{first_generation});
    REQUIRE(h.live_during_retire == 0);
    REQUIRE_FALSE(wgV2CatalogEntryAccepting(hand.get()));
    REQUIRE(wgV2ProgramNodeCount(wgV2CatalogEntryProgram(hand.get())) == 3);
    REQUIRE(h.released == 0);
    hand.reset(); REQUIRE(h.released == 1);
    second.reset();
    c.reset(); REQUIRE(h.released == 2);
    REQUIRE(h.retired.size() == 2);
    REQUIRE_FALSE(h.wrong_id);
}

TEST_CASE("v2 rejected compile retains caller lease and current catalog generation", "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h); auto current = prepare(c.get(), h);
    char error[256]{}; wg_v2_use use{};
    REQUIRE(wgV2CatalogPublish(c.get(), current.get(), error, sizeof(error)));
    const auto generation = wgV2CatalogEntryGeneration(current.get());
    auto lease = std::make_unique<Lease>(); lease->released = &h.released;
    REQUIRE_FALSE(wgV2CatalogPrepare(c.get(), "mod:single", "{", 1, digest,
        &use, lease.get(), release, error, sizeof(error)));
    REQUIRE(std::string(error).find("strict JSON") != std::string::npos);
    REQUIRE(h.released == 0);
    Entry acquired(wgV2CatalogAcquire(c.get(), "mod:single"), wgV2CatalogEntryRelease);
    REQUIRE(wgV2CatalogEntryGeneration(acquired.get()) == generation);
    REQUIRE(h.retired.empty());
    lease.reset(); REQUIRE(h.released == 1);
}

TEST_CASE("v2 retirement cannot revive an older prepared candidate", "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h);
    auto older = prepare(c.get(), h); auto newer = prepare(c.get(), h);
    char error[256]{};
    REQUIRE(wgV2CatalogPublish(c.get(), newer.get(), error, sizeof(error)));
    REQUIRE(wgV2CatalogRetire(c.get(), "mod:single"));
    REQUIRE_FALSE(wgV2CatalogPublish(c.get(), older.get(), error, sizeof(error)));
    REQUIRE(std::string(error).find("older") != std::string::npos);
    REQUIRE(wgV2CatalogCount(c.get()) == 0);
    REQUIRE_FALSE(wgV2CatalogAcquire(c.get(), "mod:single"));
    REQUIRE_FALSE(wgV2CatalogPublish(c.get(), newer.get(), error, sizeof(error)));
}

TEST_CASE("v2 source generations survive registry recreation and foreign candidates reject", "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h); auto old = prepare(c.get(), h);
    auto generation = wgV2CatalogEntryGeneration(old.get());
    c.reset(); c = registry(h);
    auto fresh = prepare(c.get(), h); char error[256]{};
    REQUIRE(wgV2CatalogEntryGeneration(fresh.get()) > generation);
    REQUIRE_FALSE(wgV2CatalogPublish(c.get(), old.get(), error, sizeof(error)));
    REQUIRE(wgV2CatalogPublish(c.get(), fresh.get(), error, sizeof(error)));
}

TEST_CASE("v2 admission checks both new candidates and active mode transitions", "[graph-v2-draft][graph-v2-catalog]") {
    Host h; auto c = registry(h); char error[256]{};
    wg_v2_use local{};
    wg_v2_use prohibited[] = {{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,2}};
    for (const auto &use : prohibited) {
        REQUIRE_FALSE(wgV2CheckUse(&use, error, sizeof(error)));
        REQUIRE(wgV2CatalogCanEnterUse(c.get(), &use, error, sizeof(error)));
    }
    auto current = prepare(c.get(), h);
    REQUIRE(wgV2CatalogPublish(c.get(), current.get(), error, sizeof(error)));
    for (const auto &use : prohibited)
        REQUIRE_FALSE(wgV2CatalogCanEnterUse(c.get(), &use, error, sizeof(error)));
    REQUIRE(wgV2CatalogCanEnterUse(c.get(), &local, error, sizeof(error)));
    REQUIRE(wgV2CatalogRetire(c.get(), "mod:single"));
    REQUIRE(h.retired.size() == 1);
    REQUIRE(wgV2CatalogCanEnterUse(c.get(), &prohibited[0], error, sizeof(error)));
}

namespace {
struct OwnedNative {
    uint64_t generation;
    int *cancelled, *cleaned, *destroyed;
    int phase = 0;
};
wg_v2_host ownedCallbacks() {
    wg_v2_host ops{};
    ops.alive = [](void *v, uint64_t generation) { return static_cast<OwnedNative *>(v)->generation == generation ? 1 : 0; };
    ops.prepare = [](void *, const wg_v2_action *, char *, size_t) { return 1; };
    ops.gate = [](void *, wg_v2_gate, int, int, char *, size_t) { return 1; };
    ops.start = [](void *v, const wg_v2_action *, uint64_t, void **state, char *, size_t) {
        *state = v; return WG_V2_RUNNING;
    };
    ops.tick = [](void *v, const wg_v2_action *, uint64_t, void *, double, char *, size_t) {
        /* Separate native animation, commitment, recovery phases. */
        return ++static_cast<OwnedNative *>(v)->phase == 3 ? WG_V2_COMPLETED : WG_V2_RUNNING;
    };
    ops.wake = [](void *, const wg_v2_action *, uint64_t, void *, uint32_t, char *, size_t) { return WG_V2_FAILED; };
    ops.cancel = [](void *v, const wg_v2_action *, uint64_t, void *, const char *) { ++*static_cast<OwnedNative *>(v)->cancelled; };
    ops.cleanup = [](void *v, const wg_v2_action *, uint64_t, void *) { ++*static_cast<OwnedNative *>(v)->cleaned; };
    return ops;
}
void destroyOwnedHost(void *v) {
    auto *host = static_cast<OwnedNative *>(v); ++*host->destroyed; delete host;
}
using Owners = std::unique_ptr<wg_v2_owners, decltype(&wgV2OwnersDestroy)>;
}

TEST_CASE("source retirement cancels real running instance before releasing host and lease", "[graph-v2-draft][graph-v2-catalog][graph-v2-owners]") {
    Host h;
    Owners owners(wgV2OwnersCreate(), wgV2OwnersDestroy); REQUIRE(owners);
    Registry c(wgV2CatalogCreate(wgV2OwnersRetireSource, owners.get()), wgV2CatalogDestroy);
    int owner = 0, cancelled = 0, cleaned = 0, destroyed = 0;
    uint64_t generation = wgV2OwnersBeginOwner(owners.get(), &owner); REQUIRE(generation);
    auto entry = prepare(c.get(), h); char error[256]{};
    REQUIRE(wgV2CatalogPublish(c.get(), entry.get(), error, sizeof(error)));
    auto ops = ownedCallbacks();
    auto host = std::make_unique<OwnedNative>(OwnedNative{generation, &cancelled, &cleaned, &destroyed});
    auto *instance = wgV2OwnersBind(owners.get(), &owner, generation, 0, entry.get(),
        &ops, host.get(), destroyOwnedHost, error, sizeof(error));
    INFO(error); REQUIRE(instance); host.release();
    const auto source_generation = wgV2CatalogEntryGeneration(entry.get());
    wgV2Press(instance, 1, generation, source_generation);
    wgV2NativeIdle(instance, 1, 1, generation, source_generation);
    REQUIRE(wgV2Busy(instance));
    REQUIRE(wgV2CatalogRetire(c.get(), "mod:single"));
    REQUIRE(cancelled == 1); REQUIRE(cleaned == 1); REQUIRE(destroyed == 1);
    REQUIRE_FALSE(wgV2OwnersFind(owners.get(), &owner, generation, 0, source_generation, nullptr));
    REQUIRE(h.released == 0); // Caller still retains the immutable candidate.
    entry.reset(); REQUIRE(h.released == 1);
    wgV2OwnersRetireOwner(owners.get(), &owner, generation, "owner ended");
    REQUIRE(cancelled == 1); REQUIRE(cleaned == 1); REQUIRE(destroyed == 1);
}

TEST_CASE("owner address reuse and stale source retirement cannot affect a newer hand", "[graph-v2-draft][graph-v2-catalog][graph-v2-owners]") {
    Host h;
    Owners owners(wgV2OwnersCreate(), wgV2OwnersDestroy);
    Registry c(wgV2CatalogCreate(wgV2OwnersRetireSource, owners.get()), wgV2CatalogDestroy);
    int owner = 0, cancelled = 0, cleaned = 0, destroyed = 0;
    auto generation1 = wgV2OwnersBeginOwner(owners.get(), &owner);
    REQUIRE_FALSE(wgV2OwnersBeginOwner(owners.get(), &owner));
    wgV2OwnersRetireOwner(owners.get(), &owner, generation1, "first owner ended");
    auto generation2 = wgV2OwnersBeginOwner(owners.get(), &owner);
    REQUIRE(generation2 > generation1);
    auto entry = prepare(c.get(), h); char error[256]{};
    REQUIRE(wgV2CatalogPublish(c.get(), entry.get(), error, sizeof(error)));
    auto ops = ownedCallbacks();
    auto host = std::make_unique<OwnedNative>(OwnedNative{generation2, &cancelled, &cleaned, &destroyed});
    auto *instance = wgV2OwnersBind(owners.get(), &owner, generation2, 1, entry.get(),
        &ops, host.get(), destroyOwnedHost, error, sizeof(error));
    REQUIRE(instance); host.release();
    auto source_generation = wgV2CatalogEntryGeneration(entry.get());
    wgV2OwnersRetireOwner(owners.get(), &owner, generation1, "stale owner event");
    wgV2OwnersRetireSource(owners.get(), "mod:single", source_generation + 1);
    REQUIRE(wgV2OwnersFind(owners.get(), &owner, generation2, 1, source_generation, nullptr) == instance);
    REQUIRE(destroyed == 0);
    wgV2OwnersRetireHand(owners.get(), &owner, generation2, 1, "equip replacement");
    REQUIRE(destroyed == 1);
    REQUIRE(cancelled == 0); REQUIRE(cleaned == 0); // Never started a native job.
}
