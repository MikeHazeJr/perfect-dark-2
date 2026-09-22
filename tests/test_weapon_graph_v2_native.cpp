/* Strict compiler/native record and copy-registry tests. Dependency doubles
 * witness leases, not animation parsing or gameplay. */
#include "catch.hpp"
#include "weapon_graph_v2_native.h"
#include "weapon_graph_v2_gset.h"
#include <cstring>
#include <memory>
#include <string>
#include "types.h"
extern "C" {
#include "constants.h"
}
#undef bool

namespace {
const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char *source = R"({"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",
"asset_id":"mod:single","graph_id":"primary","mode":"primary",
"nodes":[{"id":"press","kind":"event.trigger_pressed"},
{"id":"shot","kind":"fire.hitscan","module":"og.fire.hitscan","module_version":1,
"params":{"mode":"primary","function_type":"shoot_single","ammo_slot":0,"flags":0,
"damage":1.5,"spread":2,"recoil_anim_unk24":4,"recoil_anim_unk25":8,
"recoil_anim_unk26":-1,"recoil_anim_unk27":-1,"recoverytime_ticks60":12,
"recoildist":20,"recoilangle":30,"slidemax":4,"impactforce":5,
"duration_ticks60":7,"penetration":2,
"noisesettings":{"minradius":1,"maxradius":100,"incradius":3,"decbasespeed":2,"decremspeed":8},
"recoilsettings":{"xrange":4,"yrange":5,"zrange":6},
"fire_animation":"mod:fire","shoot_sound_catalog_id":"mod:shot"}}],
"edges":[{"from":"press","output":"exec","to":"shot","input":"exec"}],
"exports":[{"name":"trigger_pressed","node":"press"}]})";
using Program = std::unique_ptr<wg_v2_program, decltype(&wgV2ProgramRelease)>;
using Native = std::unique_ptr<wg_v2_native_bundle, decltype(&wgV2NativeRelease)>;
using Catalog = std::unique_ptr<wg_v2_catalog, decltype(&wgV2CatalogDestroy)>;
using Entry = std::unique_ptr<wg_v2_catalog_entry, decltype(&wgV2CatalogEntryRelease)>;
using Gsets = std::unique_ptr<wg_v2_gsets, decltype(&wgV2GsetsDestroy)>;
struct Resolver { int acquired = 0, released = 0; bool fail_sound = false, wrong_id = false; int sound = 61; };
struct DepLease { Resolver *owner; struct guncmd commands[1]{}; ~DepLease() { ++owner->released; } };
void depRelease(void *v) { delete static_cast<DepLease *>(v); }
int resolve(void *v, wg_v2_dependency_kind kind, const char *id,
        wg_v2_native_dependency *out, char *, size_t) {
    auto &host = *static_cast<Resolver *>(v);
    if (kind == WG_V2_SOUND_SOURCE && host.fail_sound) return 0;
    auto lease = std::make_unique<DepLease>(); lease->owner = &host;
    out->catalog_id = host.wrong_id ? "other:wrong" : id;
    out->source_closure_sha256 = digest;
    out->commands = lease->commands; out->sound_id = host.sound;
    out->lease = lease.release(); out->release = depRelease;
    ++host.acquired; return 1;
}
Program compile(const std::string &text) {
    char error[512]{};
    Program p(wgV2Compile(text.data(), text.size(), 101, digest, error, sizeof(error)), wgV2ProgramRelease);
    INFO(error); REQUIRE(p); return p;
}
Native native(wg_v2_program *p, Resolver &resolver) {
    char error[512]{};
    Native n(wgV2NativePrepare(p, resolve, &resolver, error, sizeof(error)), wgV2NativeRelease);
    INFO(error); REQUIRE(n); return n;
}
std::string replace(std::string text, const std::string &before, const std::string &after) {
    const auto at = text.find(before); REQUIRE(at != std::string::npos);
    text.replace(at, before.size(), after); return text;
}
void retire(void *, const char *, uint64_t) {}
Entry candidate(wg_v2_catalog *catalog, const std::string &text, Resolver &resolver) {
    char error[512]{}; wg_v2_use use{}; wg_v2_native_bundle *native = nullptr;
    Entry e(wgV2NativeCatalogPrepare(catalog, "mod:single", text.data(), text.size(), digest,
        &use, resolve, &resolver, &native, error, sizeof(error)), wgV2CatalogEntryRelease);
    INFO(error); REQUIRE(e); REQUIRE(native);
    REQUIRE(wgV2CatalogPublish(catalog, e.get(), error, sizeof(error))); return e;
}
Entry secondaryCandidate(wg_v2_catalog *catalog, const std::string &text, Resolver &resolver) {
    char error[512]{}; wg_v2_use use{}; use.function = 1;
    wg_v2_native_bundle *prepared = nullptr;
    Entry e(wgV2NativeCatalogPrepare(catalog, "mod:single", text.data(), text.size(), digest,
        &use, resolve, &resolver, &prepared, error, sizeof(error)), wgV2CatalogEntryRelease);
    INFO(error); REQUIRE(e); REQUIRE(prepared);
    REQUIRE(wgV2CatalogEntryFunction(e.get()) == 1);
    REQUIRE(wgV2CatalogPublish(catalog, e.get(), error, sizeof(error))); return e;
}
std::string secondarySource() {
    auto graph = replace(source, "\"graph_id\":\"primary\"", "\"graph_id\":\"secondary\"");
    graph = replace(graph, "\"mode\":\"primary\"", "\"mode\":\"secondary\"");
    graph = replace(graph, "\"mode\":\"primary\"", "\"mode\":\"secondary\"");
    return replace(graph, "\"ammo_slot\":0", "\"ammo_slot\":1");
}
Native nativeLease(wg_v2_catalog_entry *e) {
    /* Only called with an entry returned by the typed factory above. */
    auto *n = static_cast<wg_v2_native_bundle *>(wgV2CatalogEntryLease(e));
    wgV2NativeRetain(n); return Native(n, wgV2NativeRelease);
}
const char *classify(void *, int weapon) { return weapon == 100 ? "mod:single" : nullptr; }
wg_v2_gset_token open(wg_v2_gsets *r, struct gset *g, const void *owner, uint64_t generation) {
    char error[256]{}; const auto token = wgV2GsetOpen(r, g, owner, generation, error, sizeof(error));
    INFO(error); REQUIRE(token); return token;
}
}

TEST_CASE("v2 native action consumes all declared shoot noise recoil and dependency records", "[graph-v2-draft][graph-v2-native]") {
    Resolver resolver; auto p = compile(source); auto n = native(p.get(), resolver);
    p.reset(); // Native records own program and original source spans.
    const auto *a = wgV2NativeFind(n.get(), "shot"); REQUIRE(a);
    const auto *f = reinterpret_cast<const struct weaponfunc_shoot *>(wgV2NativeFunction(a));
    const auto *h = wgV2NativeHeld(a);
    REQUIRE(f->base.type == INVENTORYFUNCTYPE_SHOOT_SINGLE);
    REQUIRE(f->base.ammoindex == 0); REQUIRE(f->base.flags == 0);
    REQUIRE(f->damage == Approx(1.5)); REQUIRE(f->spread == Approx(2));
    REQUIRE(f->unk24 == 4); REQUIRE(f->unk25 == 8); REQUIRE(f->unk26 == -1); REQUIRE(f->unk27 == -1);
    REQUIRE(f->recoverytime60 == 12); REQUIRE(f->recoildist == Approx(20));
    REQUIRE(f->recoilangle == Approx(30)); REQUIRE(f->slidemax == Approx(4));
    REQUIRE(f->impactforce == Approx(5)); REQUIRE(f->duration60 == 7); REQUIRE(f->penetration == 2);
    REQUIRE(f->base.noisesettings->minradius == Approx(1));
    REQUIRE(f->base.noisesettings->maxradius == Approx(100));
    REQUIRE(f->base.noisesettings->incradius == Approx(3));
    REQUIRE(f->base.noisesettings->decbasespeed == Approx(2));
    REQUIRE(f->base.noisesettings->decremspeed == Approx(8));
    REQUIRE(f->recoilsettings->xrange == Approx(4)); REQUIRE(f->recoilsettings->yrange == Approx(5));
    REQUIRE(f->recoilsettings->zrange == Approx(6)); REQUIRE(f->base.fire_animation);
    REQUIRE(f->shootsound == 61); REQUIRE(h->has_shootsound); REQUIRE(h->shootsound == f->shootsound);
    REQUIRE(h->damage == f->damage); REQUIRE(std::strlen(wgV2NativeClosureHash(n.get())) == 64);
    REQUIRE(resolver.acquired == 2); REQUIRE(resolver.released == 0);
    n.reset(); REQUIRE(resolver.released == 2);
}

TEST_CASE("v2 native secondary action preserves mode and authored ammo slot",
        "[graph-v2-draft][graph-v2-native]") {
    auto text = replace(source, "\"graph_id\":\"primary\"", "\"graph_id\":\"secondary\"");
    text = replace(text, "\"mode\":\"primary\"", "\"mode\":\"secondary\"");
    text = replace(text, "\"mode\":\"primary\"", "\"mode\":\"secondary\"");
    text = replace(text, "\"ammo_slot\":0", "\"ammo_slot\":1");
    Resolver resolver; auto p = compile(text); auto n = native(p.get(), resolver);
    const auto *a = wgV2NativeFind(n.get(), "shot"); REQUIRE(a);
    const auto *f = reinterpret_cast<const struct weaponfunc_shoot *>(wgV2NativeFunction(a));
    REQUIRE(f->base.ammoindex == 1);
    REQUIRE(std::strcmp(wgV2NativeHeld(a)->mode, "secondary") == 0);
}

TEST_CASE("v2 native null dependencies are explicit silent records without global defaults", "[graph-v2-draft][graph-v2-native]") {
    auto text = replace(source, "\"fire_animation\":\"mod:fire\"", "\"fire_animation\":null");
    text = replace(text, "\"shoot_sound_catalog_id\":\"mod:shot\"", "\"shoot_sound_catalog_id\":null");
    text = replace(text, "{\"minradius\":1,\"maxradius\":100,\"incradius\":3,\"decbasespeed\":2,\"decremspeed\":8}", "null");
    text = replace(text, "{\"xrange\":4,\"yrange\":5,\"zrange\":6}", "null");
    auto p = compile(text); char error[512]{};
    Native n(wgV2NativePrepare(p.get(), nullptr, nullptr, error, sizeof(error)), wgV2NativeRelease);
    INFO(error); REQUIRE(n);
    const auto *f = reinterpret_cast<const struct weaponfunc_shoot *>(wgV2NativeFunction(wgV2NativeFind(n.get(), "shot")));
    REQUIRE(f->base.noisesettings); REQUIRE(f->base.noisesettings->maxradius == 0);
    REQUIRE(f->base.noisesettings->incradius == 0); REQUIRE(f->base.noisesettings->decbasespeed > 0);
    REQUIRE(f->base.noisesettings->decremspeed > 0); REQUIRE_FALSE(f->recoilsettings);
    REQUIRE_FALSE(f->base.fire_animation); REQUIRE(f->shootsound == 0);
}

TEST_CASE("v2 native owns complete original structured parameters and decoded keys", "[graph-v2-draft][graph-v2-native]") {
    Resolver resolver;
    auto text = replace(source, "\"noisesettings\":{", "\"noisesettings\":{" + std::string(400, ' '));
    text = replace(text, "\"xrange\":4", "\"\\u0078range\":4");
    auto p = compile(text);
    text.assign(text.size(), '?'); // The program must own the original spans.
    auto n = native(p.get(), resolver);
    const auto *f = reinterpret_cast<const struct weaponfunc_shoot *>(wgV2NativeFunction(wgV2NativeFind(n.get(), "shot")));
    REQUIRE(f->base.noisesettings->maxradius == Approx(100));
    REQUIRE(f->recoilsettings->xrange == Approx(4));
}

TEST_CASE("v2 native rejects invalid structured records missing scalars and unsupported flags", "[graph-v2-draft][graph-v2-native]") {
    const auto text = GENERATE_COPY(
        replace(source, "\"spread\":2,", ""),
        replace(source, "\"decbasespeed\":2", "\"decbasespeed\":0"),
        replace(source, "\"decremspeed\":8", "\"decremspeed\":1e-300"),
        replace(source, "\"minradius\":1", "\"minradius\":101"),
        replace(source, "\"xrange\":4", "\"xrange\":true"),
        replace(source, "\"zrange\":6", "\"zrange\":6,\"ignored\":1"),
        replace(source, "\"flags\":0", "\"flags\":16384"));
    Resolver resolver; auto p = compile(text); char error[512]{};
    Native n(wgV2NativePrepare(p.get(), resolve, &resolver, error, sizeof(error)), wgV2NativeRelease);
    REQUIRE_FALSE(n); REQUIRE(std::string(error).find("shot:") != std::string::npos);
    REQUIRE(resolver.acquired == resolver.released);
}

TEST_CASE("v2 native dependency failure rolls back leases and exact identity mismatch rejects", "[graph-v2-draft][graph-v2-native]") {
    Resolver resolver; auto p = compile(source); char error[512]{};
    SECTION("second dependency fails") { resolver.fail_sound = true; }
    SECTION("resolver returned another ID") { resolver.wrong_id = true; }
    SECTION("resolved sound exceeds native slot") { resolver.sound = 65536; }
    Native n(wgV2NativePrepare(p.get(), resolve, &resolver, error, sizeof(error)), wgV2NativeRelease);
    REQUIRE_FALSE(n); REQUIRE(resolver.acquired > 0); REQUIRE(resolver.released == resolver.acquired);
}

TEST_CASE("v2 selected damage copy survives source replacement and hand reselection", "[graph-v2-draft][graph-v2-gset]") {
    Resolver resolver;
    Catalog c(wgV2CatalogCreate(retire, nullptr), wgV2CatalogDestroy); REQUIRE(c);
    auto first = candidate(c.get(), source, resolver); auto a = nativeLease(first.get());
    Gsets r(wgV2GsetsCreate(classify, nullptr), wgV2GsetsDestroy); REQUIRE(r);
    struct gset hand{100, 2, 3, 0}, delayed{}; int owner = 0, chr = 0;
    const auto handtoken = open(r.get(), &hand, &owner, 5), copytoken = open(r.get(), &delayed, &chr, 9);
    char error[512]{}; const wg_v2_native_action *selected = nullptr;
    REQUIRE(wgV2GsetSelect(r.get(), handtoken, first.get(), a.get(), wgV2NativeFind(a.get(), "shot"), error, sizeof(error)));
    REQUIRE(wgV2GsetCopy(r.get(), copytoken, &hand, error, sizeof(error)));
    REQUIRE(std::memcmp(&hand, &delayed, sizeof(hand)) == 0);
    auto second = candidate(c.get(), replace(source, "\"damage\":1.5", "\"damage\":7"), resolver);
    auto b = nativeLease(second.get());
    REQUIRE_FALSE(wgV2GsetSelect(r.get(), handtoken, first.get(), a.get(), wgV2NativeFind(a.get(), "shot"), error, sizeof(error)));
    REQUIRE(wgV2GsetSelect(r.get(), handtoken, second.get(), b.get(), wgV2NativeFind(b.get(), "shot"), error, sizeof(error)));
    a.reset(); first.reset();
    REQUIRE(wgV2GsetLookup(r.get(), &hand, &selected) == WG_V2_GSET_SELECTED);
    REQUIRE(wgV2NativeHeld(selected)->damage == Approx(7));
    REQUIRE(wgV2GsetLookup(r.get(), &delayed, &selected) == WG_V2_GSET_SELECTED);
    REQUIRE(wgV2NativeHeld(selected)->damage == Approx(1.5));
    REQUIRE(resolver.released == 0);
    wgV2GsetClose(r.get(), copytoken); REQUIRE(resolver.released == 2);
}

TEST_CASE("v2 secondary selection and copied action retain exact function generation",
        "[graph-v2-draft][graph-v2-gset]") {
    Resolver resolver;
    Catalog c(wgV2CatalogCreate(retire, nullptr), wgV2CatalogDestroy); REQUIRE(c);
    auto primary = candidate(c.get(), source, resolver); auto primary_native = nativeLease(primary.get());
    const auto secondary_graph = secondarySource();
    auto first = secondaryCandidate(c.get(), secondary_graph, resolver);
    auto first_native = nativeLease(first.get());
    Gsets r(wgV2GsetsCreate(classify, nullptr), wgV2GsetsDestroy); REQUIRE(r);
    struct gset hand{100, 0, 0, 1}, delayed{}, wrong_mode{100, 0, 0, 0};
    int owner = 0, chr = 0, other = 0; char error[512]{};
    const auto hand_token = open(r.get(), &hand, &owner, 5);
    const auto copy_token = open(r.get(), &delayed, &chr, 6);
    const auto wrong_token = open(r.get(), &wrong_mode, &other, 7);
    const auto *primary_action = wgV2NativeFind(primary_native.get(), "shot");
    const auto *first_action = wgV2NativeFind(first_native.get(), "shot");
    REQUIRE_FALSE(wgV2GsetSelect(r.get(), hand_token, primary.get(), primary_native.get(),
        primary_action, error, sizeof(error)));
    REQUIRE_FALSE(wgV2GsetSelect(r.get(), wrong_token, first.get(), first_native.get(),
        first_action, error, sizeof(error)));
    REQUIRE(wgV2GsetSelect(r.get(), hand_token, first.get(), first_native.get(),
        first_action, error, sizeof(error)));
    REQUIRE(wgV2GsetCopy(r.get(), copy_token, &hand, error, sizeof(error)));
    const wg_v2_native_action *selected = nullptr;
    REQUIRE(wgV2GsetLookup(r.get(), &delayed, &selected) == WG_V2_GSET_SELECTED);
    REQUIRE(wgV2NativeHeld(selected)->damage == Approx(1.5));
    REQUIRE(wgV2NativeHeld(selected)->ammo_slot == 1);
    auto second = secondaryCandidate(c.get(), replace(secondary_graph,
        "\"damage\":1.5", "\"damage\":7"), resolver);
    auto second_native = nativeLease(second.get());
    REQUIRE_FALSE(wgV2GsetSelect(r.get(), hand_token, first.get(), first_native.get(),
        first_action, error, sizeof(error)));
    REQUIRE(wgV2GsetSelect(r.get(), hand_token, second.get(), second_native.get(),
        wgV2NativeFind(second_native.get(), "shot"), error, sizeof(error)));
    first_native.reset(); first.reset();
    REQUIRE(wgV2GsetLookup(r.get(), &hand, &selected) == WG_V2_GSET_SELECTED);
    REQUIRE(wgV2NativeHeld(selected)->damage == Approx(7));
    REQUIRE(wgV2GsetLookup(r.get(), &delayed, &selected) == WG_V2_GSET_SELECTED);
    REQUIRE(wgV2NativeHeld(selected)->damage == Approx(1.5));
    wgV2GsetClose(r.get(), copy_token);
    REQUIRE(wgV2GsetsCount(r.get()) == 2);
}

TEST_CASE("v2 gset copy is explicit fails closed and base overwrite releases action", "[graph-v2-draft][graph-v2-gset]") {
    Resolver resolver; Catalog c(wgV2CatalogCreate(retire, nullptr), wgV2CatalogDestroy);
    auto e = candidate(c.get(), source, resolver); auto n = nativeLease(e.get());
    Gsets r(wgV2GsetsCreate(classify, nullptr), wgV2GsetsDestroy);
    struct gset hand{100, 2, 3, 0}, copy{}, unregistered{100, 0, 0, 0}, base{2, 4, 5, 0}; int owner;
    const auto h = open(r.get(), &hand, &owner, 1), d = open(r.get(), &copy, &copy, 2);
    char error[256]{}; const wg_v2_native_action *selected = nullptr;
    REQUIRE(wgV2GsetLookup(r.get(), &hand, &selected) == WG_V2_GSET_UNSELECTED);
    REQUIRE(wgV2GsetLookup(r.get(), &unregistered, &selected) == WG_V2_GSET_INVALID);
    REQUIRE(wgV2GsetSelect(r.get(), h, e.get(), n.get(), wgV2NativeFind(n.get(), "shot"), error, sizeof(error)));
    REQUIRE(wgV2GsetCopy(r.get(), d, &hand, error, sizeof(error)));
    const auto before = copy;
    REQUIRE_FALSE(wgV2GsetCopy(r.get(), d, &unregistered, error, sizeof(error)));
    REQUIRE(std::memcmp(&copy, &before, sizeof(copy)) == 0);
    REQUIRE(wgV2GsetCopy(r.get(), d, &base, error, sizeof(error)));
    REQUIRE(wgV2GsetLookup(r.get(), &copy, &selected) == WG_V2_GSET_BASE); REQUIRE_FALSE(selected);
    hand.weaponfunc = 1;
    REQUIRE(wgV2GsetLookup(r.get(), &hand, &selected) == WG_V2_GSET_INVALID);
}

TEST_CASE("v2 gset old owner retirement and scope token cannot close reused storage", "[graph-v2-draft][graph-v2-gset]") {
    Gsets r(wgV2GsetsCreate(classify, nullptr), wgV2GsetsDestroy);
    struct gset value{100, 0, 0, 0}; int owner; char error[256]{};
    const auto old = open(r.get(), &value, &owner, 1);
    REQUIRE_FALSE(wgV2GsetOpen(r.get(), &value, &owner, 1, error, sizeof(error)));
    wgV2GsetsRetireOwner(r.get(), &owner, 1); REQUIRE(wgV2GsetsCount(r.get()) == 0);
    const auto fresh = open(r.get(), &value, &owner, 2); REQUIRE(fresh != old);
    wgV2GsetClose(r.get(), old); wgV2GsetsRetireOwner(r.get(), &owner, 1);
    REQUIRE(wgV2GsetsCount(r.get()) == 1);
    wgV2GsetsRetireAll(r.get()); REQUIRE(wgV2GsetsCount(r.get()) == 0);
}

TEST_CASE("v2 native candidate rejects a failed replacement before catalog publication", "[graph-v2-draft][graph-v2-native]") {
    Resolver resolver; Catalog c(wgV2CatalogCreate(retire, nullptr), wgV2CatalogDestroy);
    auto prior = candidate(c.get(), source, resolver);
    const auto generation = wgV2CatalogEntryGeneration(prior.get());
    resolver.fail_sound = true;
    char error[512]{}; wg_v2_use use{}; wg_v2_native_bundle *bundle = nullptr;
    Entry failed(wgV2NativeCatalogPrepare(c.get(), "mod:single", source, std::strlen(source), digest,
        &use, resolve, &resolver, &bundle, error, sizeof(error)), wgV2CatalogEntryRelease);
    REQUIRE_FALSE(failed); REQUIRE_FALSE(bundle);
    REQUIRE(wgV2CatalogEntryAccepting(prior.get()));
    Entry current(wgV2CatalogAcquire(c.get(), "mod:single"), wgV2CatalogEntryRelease);
    REQUIRE(wgV2CatalogEntryGeneration(current.get()) == generation);
    REQUIRE(resolver.acquired == 3); REQUIRE(resolver.released == 1);
}
