#include "catch.hpp"
#include "weapon_graph_v2_equipped.h"
#include "modarchive.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "types.h"
extern "C" {
#include "constants.h"
}
#undef bool
namespace {
const char *digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
const char *graph = R"GRAPH({"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",
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
"fire_animation":null,"shoot_sound_catalog_id":null}}],
"edges":[{"from":"press","output":"exec","to":"shot","input":"exec"}],
"exports":[{"name":"trigger_pressed","node":"press"}]})GRAPH";
const char *settings = R"({"schema":"pd.weapon_settings.v2","asset_id":"mod:single","equipped":{
"ammo":{"type":"pistol","casing":"none","clip_size":13,"flags":["incremental_reload"],"reload_animation":"mod:reload"},
"aim":{"zoom_fov":55,"transition_up":-2,"transition_down":3,"transition_side":4,"damping_pal":0.9,"damping":0.8,"flags":["auto_aim"]},
"sway":0.5,"flags":["one_handed","dual_wield"],"equip_animation":"mod:equip","unequip_animation":"mod:equip",
"visibility":[{"condition":"left_hand","operation":"show_else_hide","part":10}],"parts":[{"part":2,"visible":false}]}})";
using Program = std::unique_ptr<wg_v2_program, decltype(&wgV2ProgramRelease)>;
using Native = std::unique_ptr<wg_v2_native_bundle, decltype(&wgV2NativeRelease)>;
using Equipped = std::unique_ptr<wg_v2_equipped, decltype(&wgV2EquippedRelease)>;
struct Host { int acquired = 0, released = 0, models = 0; bool fail_equip = false, wrong_id = false; };
struct Lease { Host *host; struct guncmd commands[1]{}; ~Lease() { ++host->released; } };
void release(void *v) { delete static_cast<Lease *>(v); }
void modelRelease(void *v) { ++static_cast<Host *>(v)->models; }
int resolve(void *v, wg_v2_dependency_kind kind, const char *id, wg_v2_native_dependency *out, char *, size_t) {
    auto &host = *static_cast<Host *>(v);
    if (kind != WG_V2_COMMAND_SOURCE || (host.fail_equip && !std::strcmp(id, "mod:equip"))) return 0;
    auto lease = std::make_unique<Lease>(); lease->host = &host;
    out->commands = lease->commands; out->catalog_id = host.wrong_id ? "mod:wrong" : id;
    out->source_closure_sha256 = digest; out->release = release; out->lease = lease.release();
    ++host.acquired; return 1;
}
struct Fixture {
    Host host;
    weapon_graph_archive_descriptor_t descriptor{};
    wg_v2_equipped_model model{};
    Native native{nullptr, wgV2NativeRelease};
    void actions(const std::string &source) {
        char error[512]{};
        Program p(wgV2Compile(source.data(), source.size(), 102, digest, error, sizeof(error)), wgV2ProgramRelease);
        INFO(error); REQUIRE(p);
        native.reset(wgV2NativePrepare(p.get(), nullptr, nullptr, error, sizeof(error)));
        INFO(error); REQUIRE(native);
    }
    Fixture() {
        actions(graph);
        descriptor.type = ASSET_WEAPON; std::strcpy(descriptor.catalog_id, "mod:single");
        std::strcpy(descriptor.model_file, "meshes/held.pdmodel");
        descriptor.has_muzzlez = descriptor.has_posx = descriptor.has_posy = descriptor.has_posz = descriptor.has_track_type = 1;
        descriptor.muzzlez = -25; descriptor.posx = 3; descriptor.posy = -4; descriptor.posz = -6; descriptor.track_type = 1;
        model = {"meshes/held.pdmodel", "mod:single/held", digest, 123, &host, modelRelease};
    }
    Equipped prepare(const std::string &source, bool valid = true) {
        char error[512]{};
        Equipped out(wgV2EquippedPrepare(native.get(), &descriptor, digest, source.data(), source.size(), &model,
            resolve, &host, error, sizeof(error)), wgV2EquippedRelease);
        INFO(error);
        if (valid) REQUIRE(out); else { REQUIRE_FALSE(out); REQUIRE(error[0]); }
        return out;
    }
};
std::string replace(std::string text, const std::string &from, const std::string &to) {
    const auto at = text.find(from); REQUIRE(at != std::string::npos); text.replace(at, from.size(), to); return text;
}
}

TEST_CASE("v2 equipped validates every action ammo dependency before publication", "[graph-v2-equipped][modding][pdxxx][c3842]") {
    Fixture f;
    f.actions(replace(graph, "\"ammo_slot\":0", "\"ammo_slot\":1"));
    f.prepare(settings, false);
    REQUIRE(f.host.models == 0);
    REQUIRE(f.host.acquired == f.host.released);
}

TEST_CASE("v2 equipped two-slot ammo retains independent definitions and null semantics", "[graph-v2-equipped][modding][pdxxx][c3842]") {
    const std::string ammo = R"({"type":"pistol","casing":"none","clip_size":13,"flags":["incremental_reload"],"reload_animation":"mod:reload"})";
    Fixture f;
    f.actions(replace(graph, "\"ammo_slot\":0", "\"ammo_slot\":1"));
    const auto second = replace(replace(ammo, "\"pistol\"", "\"rifle\""), "\"clip_size\":13", "\"clip_size\":7");
    SECTION("both slots") {
        auto prepared = f.prepare(replace(settings, "\"ammo\":" + ammo, "\"ammo\":[" + ammo + "," + second + "]"));
        const auto *weapon = wgV2EquippedWeapon(prepared.get());
        REQUIRE(weapon->ammos[0]->clipsize == 13);
        REQUIRE(weapon->ammos[1]->clipsize == 7);
        REQUIRE(weapon->ammos[1]->type == AMMOTYPE_RIFLE);
        REQUIRE(weapon->ammos[0]->reload_animation == weapon->ammos[1]->reload_animation);
        REQUIRE(f.host.acquired == 2); // shared reload plus shared equip command.
    }
    SECTION("secondary-only") {
        auto prepared = f.prepare(replace(settings, "\"ammo\":" + ammo, "\"ammo\":[null," + second + "]"));
        REQUIRE_FALSE(wgV2EquippedWeapon(prepared.get())->ammos[0]);
        REQUIRE(wgV2EquippedWeapon(prepared.get())->ammos[1]->clipsize == 7);
    }
    SECTION("no ammo is explicit") {
        f.actions(replace(graph, "\"ammo_slot\":0", "\"ammo_slot\":-1"));
        auto prepared = f.prepare(replace(settings, "\"ammo\":" + ammo, "\"ammo\":[null,null]"));
        REQUIRE_FALSE(wgV2EquippedWeapon(prepared.get())->ammos[0]);
        REQUIRE_FALSE(wgV2EquippedWeapon(prepared.get())->ammos[1]);
    }
    SECTION("bad slot count or absent referenced slot") {
        for (const auto &slots : {"[]", "[null]", "[null,null,null]", "[null,null]"})
            f.prepare(replace(settings, "\"ammo\":" + ammo, std::string("\"ammo\":") + slots), false);
        REQUIRE(f.host.models == 0);
        REQUIRE(f.host.acquired == f.host.released);
    }
}

TEST_CASE("v2 equipped validates ammo gates even when the action uses another slot", "[graph-v2-equipped][modding][pdxxx][c3842]") {
    Fixture f;
    auto source = replace(graph, "\"nodes\":[", R"("nodes":[{"id":"ammo","kind":"gate.ammo_available","params":{"ammo_slot":1,"required":1}},)");
    source = replace(source, R"({"from":"press","output":"exec","to":"shot","input":"exec"})",
        R"({"from":"press","output":"exec","to":"ammo","input":"exec"},{"from":"ammo","output":"pass","to":"shot","input":"exec"})");
    f.actions(source);
    f.prepare(settings, false);
    REQUIRE(f.host.models == 0);
    REQUIRE(f.host.acquired == f.host.released);
}

namespace {
using ArchiveEntries = std::vector<std::pair<std::string, std::string>>;
std::string snapshot(const ArchiveEntries &entries) {
    static unsigned serial = 0;
    const auto path = std::filesystem::temp_directory_path() / ("pd2-equipped-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
        "-" + std::to_string(serial++) + ".pdweapon");
    struct Remove { std::filesystem::path path; ~Remove() { std::error_code e; std::filesystem::remove(path, e); } } cleanup{path};
    auto *writer = modArchiveBegin(path.string().c_str()); REQUIRE(writer);
    for (const auto &entry : entries) {
        if (modArchiveAddFileMem(writer, entry.first.c_str(), entry.second.data(), static_cast<u32>(entry.second.size())) != MODARCHIVE_OK) {
            modArchiveAbort(writer); FAIL("failed to write equipped source fixture");
        }
    }
    REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
    std::ifstream input(path, std::ios::binary); REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
const char *descriptorSource = "[weapon]\ncatalog_id=mod:single\nprimary_graph=graphs/primary.json\n"
    "secondary_graph=graphs/secondary.json\nsettings_file=bindings/settings.json\n"
    "model_file=meshes/held.pdmodel\nmuzzlez=-25\nposx=3\nposy=-4\nposz=-6\ntrack_type=default\n";
ArchiveEntries archiveEntries() {
    return {{"weapon.ini", descriptorSource}, {"graphs/primary.json", graph},
        {"graphs/secondary.json", replace(replace(graph, "\"mode\":\"primary\"", "\"mode\":\"secondary\""),
            "\"mode\":\"primary\"", "\"mode\":\"secondary\"")}, {"bindings/settings.json", settings}};
}
}

TEST_CASE("v2 captured public archive prepares selected graph and equipment atomically", "[graph-v2-equipped][modding][pdxxx][c3842]") {
    Fixture f; char error[512]{}; wg_v2_use use{};
    using Catalog = std::unique_ptr<wg_v2_catalog, decltype(&wgV2CatalogDestroy)>;
    using Entry = std::unique_ptr<wg_v2_catalog_entry, decltype(&wgV2CatalogEntryRelease)>;
    Catalog catalog(wgV2CatalogCreate([](void *, const char *, uint64_t) {}, nullptr), wgV2CatalogDestroy);
    auto entries = archiveEntries();
    auto prepare = [&](const ArchiveEntries &sources, bool valid) {
        auto bytes = snapshot(sources); wg_v2_equipped *equipped = nullptr;
        Entry candidate(wgV2EquippedCatalogPrepareArchive(catalog.get(), "mod:single", bytes.data(),
            static_cast<u32>(bytes.size()), digest, &use, &f.model, resolve, &f.host,
            &equipped, error, sizeof(error)), wgV2CatalogEntryRelease);
        INFO(error);
        if (valid) { REQUIRE(candidate); REQUIRE(equipped); }
        else { REQUIRE_FALSE(candidate); REQUIRE_FALSE(equipped); REQUIRE(error[0]); }
        return candidate;
    };
    auto first = prepare(entries, true);
    auto *old = static_cast<wg_v2_equipped *>(wgV2CatalogEntryLease(first.get()));
    REQUIRE(wgV2CatalogCount(catalog.get()) == 0);
    REQUIRE(wgV2CatalogPublish(catalog.get(), first.get(), error, sizeof(error)));
    entries[1].second = replace(entries[1].second, "\"damage\":1.5", "\"damage\":6");
    entries[3].second = replace(entries[3].second, "\"clip_size\":13", "\"clip_size\":21");
    auto next = prepare(entries, true);
    auto *fresh = static_cast<wg_v2_equipped *>(wgV2CatalogEntryLease(next.get()));
    REQUIRE(std::string(wgV2EquippedClosureHash(old)) != wgV2EquippedClosureHash(fresh));
    REQUIRE(wgV2NativeHeld(wgV2NativeFind(wgV2EquippedActions(old), "shot"))->damage == Approx(1.5));
    REQUIRE(wgV2NativeHeld(wgV2NativeFind(wgV2EquippedActions(fresh), "shot"))->damage == 6);
    REQUIRE(wgV2EquippedWeapon(old)->ammos[0]->clipsize == 13);
    REQUIRE(wgV2EquippedWeapon(fresh)->ammos[0]->clipsize == 21);
    auto broken = entries; broken[3].second = "{";
    prepare(broken, false);
    Entry active(wgV2CatalogAcquire(catalog.get(), "mod:single"), wgV2CatalogEntryRelease);
    REQUIRE(wgV2CatalogEntryGeneration(active.get()) == wgV2CatalogEntryGeneration(first.get()));
    use.function = 1;
    auto secondary = prepare(entries, true);
    REQUIRE(wgV2CatalogEntryFunction(secondary.get()) == 1);
    REQUIRE(std::string(wgV2ProgramMode(wgV2CatalogEntryProgram(secondary.get()))) == "secondary");
    wgV2CatalogRetireAll(catalog.get()); active.reset(); first.reset(); next.reset(); secondary.reset();
    REQUIRE(f.host.models == 3);
    REQUIRE(f.host.acquired == f.host.released);
}

TEST_CASE("v2 archive rejects missing mismatched or unsupported authored sources", "[graph-v2-equipped][modding][pdxxx][c3842]") {
    const int variant = GENERATE(0, 1, 2, 3, 4, 5, 6, 7);
    Fixture f; auto entries = archiveEntries(); wg_v2_use use{}; char error[512]{};
    switch (variant) {
    case 0: entries[0].second = replace(entries[0].second, "mod:single", "mod:wrong"); break;
    case 1: entries.erase(entries.begin() + 1); break;
    case 2: entries[0].second = replace(entries[0].second, "graphs/primary.json", "../graphs/primary.json"); break;
    case 3: entries[0].second += "variables_file=bindings/variables.json\n"; break;
    case 4: entries[1].second = replace(entries[1].second, "pd.weapon_graph.v2", "pd.weapon_graph.v1"); break;
    case 5: entries[3].second = replace(entries[3].second, "mod:single", "mod:wrong"); break;
    case 6: entries[0].second = replace(entries[0].second, "bindings/settings.json", "missing.json"); break;
    case 7: use.network_active = 1; break;
    }
    const auto bytes = snapshot(entries);
    using Catalog = std::unique_ptr<wg_v2_catalog, decltype(&wgV2CatalogDestroy)>;
    Catalog catalog(wgV2CatalogCreate([](void *, const char *, uint64_t) {}, nullptr), wgV2CatalogDestroy);
    wg_v2_equipped *out = nullptr;
    auto *candidate = wgV2EquippedCatalogPrepareArchive(catalog.get(), "mod:single", bytes.data(),
        static_cast<u32>(bytes.size()), digest, &use, &f.model, resolve, &f.host, &out, error, sizeof(error));
    INFO(error); REQUIRE_FALSE(candidate); REQUIRE_FALSE(out); REQUIRE(error[0]);
    REQUIRE(wgV2CatalogCount(catalog.get()) == 0);
    REQUIRE(f.host.models == 0);
    REQUIRE(f.host.acquired == f.host.released);
}
TEST_CASE("v2 equipped source owns ammo aim visibility placement and exact command dependencies", "[graph-v2-equipped]") {
    Fixture f; std::string source = settings; auto out = f.prepare(source);
    source.assign(source.size(), '?'); f.native.reset();
    const auto *w = wgV2EquippedWeapon(out.get()); REQUIRE(w);
    REQUIRE(w->hi_model == 123); REQUIRE(w->lo_model == 123);
    REQUIRE(w->posx == 3); REQUIRE(w->posy == -4); REQUIRE(w->posz == -6); REQUIRE(w->muzzlez == -25);
    REQUIRE(w->ammos[0]->type == AMMOTYPE_PISTOL); REQUIRE(w->ammos[0]->casingeject == static_cast<u32>(CASING_NONE));
    REQUIRE(w->ammos[0]->clipsize == 13); REQUIRE(w->ammos[0]->flags == AMMOFLAG_INCREMENTALRELOAD);
    REQUIRE(w->ammos[0]->reload_animation); REQUIRE_FALSE(w->ammos[1]);
    REQUIRE(w->aimsettings->zoomfov == 55); REQUIRE(w->aimsettings->guntransup == -2);
    REQUIRE(w->aimsettings->guntransdown == 3); REQUIRE(w->aimsettings->guntransside == 4);
    REQUIRE(w->aimsettings->aimdamppal == Approx(0.9)); REQUIRE(w->aimsettings->aimdamp == Approx(0.8));
    REQUIRE(w->aimsettings->tracktype == 1); REQUIRE(w->aimsettings->flags == INVAIMFLAG_AUTOAIM);
    REQUIRE(w->sway == Approx(0.5)); REQUIRE(w->flags == (WEAPONFLAG_00000040 | WEAPONFLAG_ONEHANDED | WEAPONFLAG_DUALWIELD));
    REQUIRE(w->equip_animation == w->unequip_animation); REQUIRE(w->equip_animation != w->ammos[0]->reload_animation);
    REQUIRE(w->gunviscmds[0].type == GUNVISCMD_CHECKINLEFTHAND); REQUIRE(w->gunviscmds[0].op == GUNVISOP_SETVISIBILITY);
    REQUIRE(w->gunviscmds[0].partnum == 10); REQUIRE(w->gunviscmds[1].type == GUNVISCMD_END);
    REQUIRE(w->partvisibility[0].part == 2); REQUIRE(w->partvisibility[0].visible == 0); REQUIRE(w->partvisibility[1].part == 255);
    REQUIRE_FALSE(w->functions[0]); REQUIRE_FALSE(w->functions[1]); REQUIRE(wgV2EquippedActions(out.get()));
    REQUIRE(f.host.acquired == 2); REQUIRE(f.host.released == 0); REQUIRE(f.host.models == 0);
    out.reset(); REQUIRE(f.host.released == 2); REQUIRE(f.host.models == 1);
}
TEST_CASE("v2 equipped failure releases prepared commands without consuming caller model lease", "[graph-v2-equipped]") {
    Fixture f; f.host.fail_equip = true; f.prepare(settings, false);
    REQUIRE(f.host.acquired == 1); REQUIRE(f.host.released == 1); REQUIRE(f.host.models == 0);
    f.host.fail_equip = false; f.host.wrong_id = true; f.prepare(settings, false);
    REQUIRE(f.host.acquired == 2); REQUIRE(f.host.released == 2); REQUIRE(f.host.models == 0);
}
TEST_CASE("v2 equipped null commands and empty visibility preserve native empty meanings", "[graph-v2-equipped]") {
    Fixture f;
    auto source = replace(settings, "\"mod:reload\"", "null");
    source = replace(source, "\"equip_animation\":\"mod:equip\"", "\"equip_animation\":null");
    source = replace(source, "\"unequip_animation\":\"mod:equip\"", "\"unequip_animation\":null");
    source = replace(source, "[{\"condition\":\"left_hand\",\"operation\":\"show_else_hide\",\"part\":10}]", "[]");
    source = replace(source, "[{\"part\":2,\"visible\":false}]", "[]");
    auto out = f.prepare(source); const auto *w = wgV2EquippedWeapon(out.get());
    REQUIRE_FALSE(w->gunviscmds); REQUIRE(w->partvisibility[0].part == 255);
    REQUIRE_FALSE(w->equip_animation); REQUIRE_FALSE(w->unequip_animation); REQUIRE_FALSE(w->ammos[0]->reload_animation);
    REQUIRE(f.host.acquired == 0);
}
TEST_CASE("v2 equipped source rejects malformed duplicate missing and narrowing fields", "[graph-v2-equipped]") {
    const auto source = GENERATE_COPY(
        replace(settings, "\"clip_size\":13", "\"clip_size\":13.5"),
        replace(settings, "\"clip_size\":13", "\"clip_size\":32768"),
        replace(settings, "\"clip_size\":13", "\"clip_size\":0"),
        replace(settings, "\"clip_size\":13", "\"clip_size\":13,\"clip_size\":14"),
        replace(settings, "\"clip_size\":13,", ""),
        replace(settings, "\"zoom_fov\":55", "\"zoom_fov\":0"),
        replace(settings, "\"sway\":0.5", "\"sway\":1e-300"),
        replace(settings, "\"flags\":[\"auto_aim\"]", "\"flags\":[\"auto_aim\",\"auto_aim\"]"),
        replace(settings, "\"part\":2", "\"part\":255"),
        replace(settings, "\"visible\":false", "\"visible\":0"),
        replace(settings, "\"part\":10", "\"part\":32768"),
        replace(settings, "\"type\":\"pistol\"", "\"type\":1"),
        replace(settings, "\"mod:reload\"", "\"reload\""),
        replace(settings, "\"sway\":0.5", "\"sway\":0.5,\"posx\":5"),
        std::string(settings) + " garbage");
    Fixture f; f.prepare(source, false); REQUIRE(f.host.acquired == f.host.released); REQUIRE(f.host.models == 0);
}
TEST_CASE("v2 equipped descriptor and model reject mismatched identity or unowned slots", "[graph-v2-equipped]") {
    const int variant = GENERATE(0, 1, 2, 3, 4, 5, 6);
    Fixture f;
    switch (variant) {
    case 0: f.model.source_reference = "wrong.pdmodel"; break;
    case 1: f.model.release = nullptr; break;
    case 2: f.model.file_id = 65536; break;
    case 3: f.descriptor.has_posx = 0; break;
    case 4: f.descriptor.track_type = 16; break;
    case 5: std::strcpy(f.descriptor.catalog_id, "mod:other"); break;
    case 6: f.model.source_closure_sha256 = "wrong"; break;
    }
    f.prepare(settings, false); REQUIRE(f.host.acquired == 0); REQUIRE(f.host.models == 0);
}
TEST_CASE("v2 equipped retained candidates preserve old settings and closure across replacement", "[graph-v2-equipped]") {
    Fixture f; auto old = f.prepare(settings); const std::string old_hash = wgV2EquippedClosureHash(old.get());
    auto next = f.prepare(replace(settings, "\"clip_size\":13", "\"clip_size\":21"));
    REQUIRE(old_hash.size() == 64); REQUIRE(old_hash != wgV2EquippedClosureHash(next.get()));
    REQUIRE(wgV2EquippedWeapon(old.get())->ammos[0]->clipsize == 13);
    REQUIRE(wgV2EquippedWeapon(next.get())->ammos[0]->clipsize == 21);
    auto *retained = old.get(); wgV2EquippedRetain(retained); old.reset();
    REQUIRE(f.host.models == 0); REQUIRE(wgV2EquippedWeapon(retained)->ammos[0]->clipsize == 13);
    wgV2EquippedRelease(retained); REQUIRE(f.host.models == 1);
    next.reset(); REQUIRE(f.host.models == 2); REQUIRE(f.host.acquired == f.host.released);
}
TEST_CASE("v2 equipped catalog candidate retains exact program and failed replacement preserves active source", "[graph-v2-equipped]") {
    Fixture f; char error[512]{}; wg_v2_use use{}; wg_v2_equipped *equipped = nullptr;
    std::unique_ptr<wg_v2_catalog, decltype(&wgV2CatalogDestroy)> catalog(
        wgV2CatalogCreate([](void *, const char *, uint64_t) {}, nullptr), wgV2CatalogDestroy);
    REQUIRE(catalog);
    using Entry = std::unique_ptr<wg_v2_catalog_entry, decltype(&wgV2CatalogEntryRelease)>;
    Entry entry(wgV2EquippedCatalogPrepare(catalog.get(), "mod:single", graph, std::strlen(graph), digest, &use,
        &f.descriptor, digest, settings, std::strlen(settings), &f.model, resolve, &f.host, &equipped, error, sizeof(error)), wgV2CatalogEntryRelease);
    INFO(error); REQUIRE(entry); REQUIRE(equipped);
    REQUIRE(wgV2CatalogEntryLease(entry.get()) == equipped);
    REQUIRE(wgV2CatalogEntryProgram(entry.get()) == wgV2NativeProgram(wgV2EquippedActions(equipped)));
    REQUIRE(wgV2CatalogCount(catalog.get()) == 0);
    REQUIRE(wgV2CatalogPublish(catalog.get(), entry.get(), error, sizeof(error)));
    const auto generation = wgV2CatalogEntryGeneration(entry.get());
    f.host.fail_equip = true; wg_v2_equipped *failed = equipped;
    Entry next(wgV2EquippedCatalogPrepare(catalog.get(), "mod:single", graph, std::strlen(graph), digest, &use,
        &f.descriptor, digest, settings, std::strlen(settings), &f.model, resolve, &f.host, &failed, error, sizeof(error)), wgV2CatalogEntryRelease);
    REQUIRE_FALSE(next); REQUIRE_FALSE(failed); REQUIRE(f.host.models == 0);
    Entry active(wgV2CatalogAcquire(catalog.get(), "mod:single"), wgV2CatalogEntryRelease);
    REQUIRE(wgV2CatalogEntryGeneration(active.get()) == generation);
    REQUIRE(wgV2CatalogRetire(catalog.get(), "mod:single"));
    REQUIRE_FALSE(wgV2CatalogEntryAccepting(entry.get())); REQUIRE(f.host.models == 0);
    REQUIRE(wgV2EquippedWeapon(equipped)->ammos[0]->clipsize == 13);
    active.reset(); entry.reset(); REQUIRE(f.host.models == 1); REQUIRE(f.host.acquired == f.host.released);
}
