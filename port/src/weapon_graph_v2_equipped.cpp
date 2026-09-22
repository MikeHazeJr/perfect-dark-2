#include "weapon_graph_v2_equipped.h"
#include "modasset_gltf_document.h"
#include "crude_json.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "types.h"
extern "C" {
#include "constants.h"
}
#undef bool

namespace {
using Json = crude_json::value;
[[noreturn]] void bad(const std::string &s) { throw std::runtime_error(s); }
const Json &field(const Json &v, const char *key) {
    if (!v.is_object() || !v.contains(key)) bad(std::string("missing equipped field: ") + key);
    return v[key];
}
void keys(const Json &v, const std::vector<std::string> &allowed) {
    if (!v.is_object()) bad("equipped field requires an object");
    for (const auto &p : v.get<crude_json::object>())
        if (std::find(allowed.begin(), allowed.end(), p.first) == allowed.end())
            bad("unsupported equipped field: " + p.first);
}
std::string string(const Json &v) {
    if (!v.is_string()) bad("equipped field requires a string");
    const auto &s = v.get<crude_json::string>();
    if (s.empty() || s.find('\0') != std::string::npos) bad("empty or NUL equipped string");
    return s;
}
const crude_json::array &array(const Json &v) {
    if (!v.is_array()) bad("equipped field requires an array");
    return v.get<crude_json::array>();
}
int integer(const Json &v, int low, int high) {
    if (!v.is_number()) bad("equipped field requires an integer");
    const double n = v.get<crude_json::number>();
    if (!(n >= low && n <= high) || std::floor(n) != n) bad("equipped integer outside native range");
    return static_cast<int>(n);
}
float number(const Json &v, double low, double high) {
    if (!v.is_number()) bad("equipped field requires a number");
    const double n = v.get<crude_json::number>();
    if (!(n >= low && n <= high)) bad("equipped number outside native range");
    const float result = static_cast<float>(n);
    if (n != 0 && result == 0) bad("equipped number underflows native float");
    return result;
}
bool hash(const char *s) {
    if (!s || std::strlen(s) != 64) return false;
    for (; *s; ++s) if (!(*s >= '0' && *s <= '9') && !(*s >= 'a' && *s <= 'f')) return false;
    return true;
}
bool catalogId(const char *s) {
    if (!s || !*s || std::strlen(s) >= CATALOG_ID_LEN) return false;
    const char *c = std::strchr(s, ':');
    return c && c != s && c[1] && !std::strchr(c + 1, ':');
}
struct Named { const char *name; int value; };
int named(const Json &v, std::initializer_list<Named> allowed) {
    const auto name = string(v);
    for (const auto &item : allowed) if (name == item.name) return item.value;
    bad("unsupported equipped enum: " + name);
}
u32 flags(const Json &v, std::initializer_list<Named> allowed) {
    u32 result = 0;
    for (const auto &item : array(v)) {
        const u32 bit = static_cast<u32>(named(item, allowed));
        if (result & bit) bad("duplicate equipped flag");
        result |= bit;
    }
    return result;
}
void hashField(sha256_ctx &ctx, const char *s) { sha256Update(&ctx, s, std::strlen(s) + 1); }
struct Command {
    wg_v2_native_dependency value{};
    std::string id, digest;
    ~Command() { if (value.lease && value.release) value.release(value.lease); }
};
}
struct wg_v2_equipped {
    std::atomic<size_t> refs{1};
    struct weapon weapon{};
    struct inventory_ammo ammo{};
    struct invaimsettings aim{};
    std::vector<struct gunviscmd> visibility;
    std::vector<struct modelpartvisibility> parts;
    std::vector<std::unique_ptr<Command>> commands;
    wg_v2_native_bundle *actions = nullptr;
    void *model_lease = nullptr;
    void (*model_release)(void *) = nullptr;
    char closure[SHA256_HEX_SIZE]{};
    ~wg_v2_equipped() {
        wgV2NativeRelease(actions);
        if (model_lease) model_release(model_lease);
    }
};
namespace {
struct guncmd *command(wg_v2_equipped &out, const Json &v,
        wg_v2_native_resolver resolver, void *host) {
    if (v.is_null()) return nullptr;
    const auto id = string(v);
    if (!catalogId(id.c_str())) bad("equipped animation must use an exact catalog ID");
    for (const auto &old : out.commands) if (old->id == id) return const_cast<guncmd *>(old->value.commands);
    if (!resolver) bad("equipped animation needs a source resolver");
    auto dep = std::make_unique<Command>(); dep->id = id;
    char error[512]{};
    if (!resolver(host, WG_V2_COMMAND_SOURCE, id.c_str(), &dep->value, error, sizeof(error)))
        bad(error[0] ? error : "equipped animation preparation failed");
    const auto &d = dep->value;
    if (!d.catalog_id || id != d.catalog_id || !hash(d.source_closure_sha256) ||
            !d.lease || !d.release || !d.commands) bad("equipped animation dependency is not owned or has wrong identity");
    dep->digest = d.source_closure_sha256;
    dep->value.catalog_id = dep->id.c_str();
    dep->value.source_closure_sha256 = dep->digest.c_str();
    auto *result = const_cast<guncmd *>(d.commands);
    out.commands.push_back(std::move(dep));
    return result;
}
void prepare(wg_v2_equipped &out, const Json &root, wg_v2_native_resolver resolver, void *host) {
    keys(root, {"schema", "asset_id", "equipped"});
    if (string(field(root, "schema")) != "pd.weapon_settings.v2") bad("equipped source requires pd.weapon_settings.v2");
    if (string(field(root, "asset_id")) != wgV2ProgramAssetId(wgV2NativeProgram(out.actions))) bad("equipped source asset identity mismatch");
    const auto &e = field(root, "equipped");
    keys(e, {"ammo", "aim", "sway", "flags", "equip_animation", "unequip_animation", "visibility", "parts"});
    const auto &a = field(e, "ammo");
    keys(a, {"type", "casing", "clip_size", "flags", "reload_animation"});
    out.ammo.type = named(field(a, "type"), {
        {"pistol", AMMOTYPE_PISTOL}, {"smg", AMMOTYPE_SMG}, {"rifle", AMMOTYPE_RIFLE},
        {"shotgun", AMMOTYPE_SHOTGUN}, {"magnum", AMMOTYPE_MAGNUM}, {"reaper", AMMOTYPE_REAPER}});
    out.ammo.casingeject = named(field(a, "casing"), {{"none", CASING_NONE}, {"standard", CASING_STANDARD},
        {"rifle", CASING_RIFLE}, {"shotgun", CASING_SHOTGUN}, {"reaper", CASING_REAPER}});
    out.ammo.clipsize = static_cast<s16>(integer(field(a, "clip_size"), 1, INT16_MAX));
    out.ammo.flags = static_cast<u8>(flags(field(a, "flags"), {
        {"no_reserve", AMMOFLAG_NORESERVE}, {"equipped_is_reserve", AMMOFLAG_EQUIPPEDISRESERVE},
        {"incremental_reload", AMMOFLAG_INCREMENTALRELOAD}, {"quantity_controls_visibility", AMMOFLAG_QTYAFFECTSPARTVIS}}));
    out.ammo.reload_animation = command(out, field(a, "reload_animation"), resolver, host);
    out.weapon.ammos[0] = &out.ammo;
    const auto &aim = field(e, "aim");
    keys(aim, {"zoom_fov", "transition_up", "transition_down", "transition_side", "damping_pal", "damping", "flags"});
    out.aim.zoomfov = number(field(aim, "zoom_fov"), 1, 179);
    out.aim.guntransup = number(field(aim, "transition_up"), -FLT_MAX, FLT_MAX);
    out.aim.guntransdown = number(field(aim, "transition_down"), -FLT_MAX, FLT_MAX);
    out.aim.guntransside = number(field(aim, "transition_side"), -FLT_MAX, FLT_MAX);
    out.aim.aimdamppal = number(field(aim, "damping_pal"), 0, 1);
    out.aim.aimdamp = number(field(aim, "damping"), 0, 1);
    out.aim.flags = flags(field(aim, "flags"), {{"manual_zoom", INVAIMFLAG_MANUALZOOM},
        {"auto_aim", INVAIMFLAG_AUTOAIM}, {"accurate_single_shot", INVAIMFLAG_ACCURATESINGLESHOT}});
    out.weapon.aimsettings = &out.aim;
    out.weapon.sway = number(field(e, "sway"), 0, FLT_MAX);
    /* The compatibility bit enables the ordinary held-weapon path. It is an
     * adapter rule, not an editable numeric native reference. */
    out.weapon.flags = WEAPONFLAG_00000040 | flags(field(e, "flags"), {
        {"one_handed", WEAPONFLAG_ONEHANDED}, {"dual_wield", WEAPONFLAG_DUALWIELD},
        {"flip_left_hand", WEAPONFLAG_DUALFLIP}, {"track_time_used", WEAPONFLAG_TRACKTIMEUSED},
        {"hide_inventory_model", WEAPONFLAG_HIDEMENUMODEL}, {"sideways_close_aim", WEAPONFLAG_GANGSTA},
        {"track_aim_target", WEAPONFLAG_AIMTRACK}});
    out.weapon.equip_animation = command(out, field(e, "equip_animation"), resolver, host);
    out.weapon.unequip_animation = command(out, field(e, "unequip_animation"), resolver, host);
    for (const auto &v : array(field(e, "visibility"))) {
        keys(v, {"condition", "operation", "part", "upgrade_bit"});
        struct gunviscmd cmd{};
        cmd.type = static_cast<u8>(named(field(v, "condition"), {{"always", GUNVISCMD_ALWAYSTRUE},
            {"upgrade", GUNVISCMD_CHECKUPGRADE}, {"left_hand", GUNVISCMD_CHECKINLEFTHAND}, {"right_hand", GUNVISCMD_CHECKINRIGHTHAND}}));
        if (cmd.type == GUNVISCMD_CHECKUPGRADE) cmd.param = static_cast<u16>(integer(field(v, "upgrade_bit"), 0, 7));
        else if (v.contains("upgrade_bit")) bad("upgrade_bit requires the upgrade visibility condition");
        cmd.op = static_cast<u8>(named(field(v, "operation"), {{"show", GUNVISOP_IFTRUE_SETVISIBLE},
            {"hide", GUNVISOP_IFTRUE_SETHIDDEN}, {"show_else_hide", GUNVISOP_SETVISIBILITY}}));
        cmd.partnum = static_cast<u16>(integer(field(v, "part"), 0, INT16_MAX));
        out.visibility.push_back(cmd);
    }
    // The native consumer checks the NEXT command for END. An empty array must
    // therefore be NULL, not a single terminator that it would execute/read past.
    if (!out.visibility.empty()) {
        out.visibility.push_back({}); out.weapon.gunviscmds = out.visibility.data();
    }
    for (const auto &v : array(field(e, "parts"))) {
        keys(v, {"part", "visible"});
        const int part = integer(field(v, "part"), 0, 254); // 255 terminates the native array.
        if (!field(v, "visible").is_boolean()) bad("part visibility requires boolean");
        if (std::any_of(out.parts.begin(), out.parts.end(), [part](const auto &p) { return p.part == part; })) bad("duplicate equipped model part");
        struct modelpartvisibility p{}; p.part = static_cast<u8>(part);
        p.visible = field(v, "visible").get<crude_json::boolean>() ? 1 : 0;
        out.parts.push_back(p);
    }
    struct modelpartvisibility end{}; end.part = 255; out.parts.push_back(end);
    out.weapon.partvisibility = out.parts.data();
}
}
extern "C" wg_v2_equipped *wgV2EquippedPrepare(wg_v2_native_bundle *actions,
        const weapon_graph_archive_descriptor_t *descriptor, const char *source_hash,
        const char *json, size_t size, const wg_v2_equipped_model *model,
        wg_v2_native_resolver resolver, void *host, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    try {
        if (!actions || !descriptor || descriptor->type != ASSET_WEAPON || !hash(source_hash)) bad("equipped source requires exact weapon descriptor and source hash");
        if (!std::memchr(descriptor->catalog_id, 0, sizeof(descriptor->catalog_id)) ||
                std::strcmp(descriptor->catalog_id, wgV2ProgramAssetId(wgV2NativeProgram(actions)))) bad("equipped descriptor identity mismatch");
        if (!std::memchr(descriptor->model_file, 0, sizeof(descriptor->model_file)) || !descriptor->model_file[0] ||
                !model || !model->source_reference || std::strcmp(model->source_reference, descriptor->model_file) ||
                !catalogId(model->catalog_id) || !hash(model->source_closure_sha256) ||
                model->file_id <= 0 || model->file_id > UINT16_MAX || !model->lease || !model->release) bad("equipped model requires an exact source reference and pinned native binding");
        if (!descriptor->has_muzzlez || !descriptor->has_posx || !descriptor->has_posy || !descriptor->has_posz ||
                !descriptor->has_track_type || !std::isfinite(descriptor->muzzlez) || !std::isfinite(descriptor->posx) ||
                !std::isfinite(descriptor->posy) || !std::isfinite(descriptor->posz) || descriptor->track_type < 0 ||
                descriptor->track_type > 5) bad("equipped descriptor requires finite placement and supported track type");
        Json root;
        if (!json || !modAssetJsonReadValue(json, size, root)) bad("equipped settings require complete strict JSON");
        auto out = std::make_unique<wg_v2_equipped>();
        wgV2NativeRetain(actions); out->actions = actions;
        prepare(*out, root, resolver, host);
        auto &w = out->weapon;
        w.hi_model = w.lo_model = static_cast<u16>(model->file_id);
        w.muzzlez = descriptor->muzzlez; w.posx = descriptor->posx; w.posy = descriptor->posy; w.posz = descriptor->posz;
        out->aim.tracktype = static_cast<u32>(descriptor->track_type);
        std::sort(out->commands.begin(), out->commands.end(), [](const auto &a, const auto &b) { return a->id < b->id; });
        sha256_ctx ctx; sha256Init(&ctx);
        hashField(ctx, "pd.weapon_graph.equipped.v2.1"); hashField(ctx, source_hash);
        hashField(ctx, wgV2NativeClosureHash(actions));
        /* Exact settings bytes are included even if a caller reuses an archive
         * hash during candidate preparation. Runtime slot integers are not IDs. */
        sha256Update(&ctx, json, size); const char zero = 0; sha256Update(&ctx, &zero, 1);
        hashField(ctx, model->source_reference); hashField(ctx, model->catalog_id); hashField(ctx, model->source_closure_sha256);
        for (const auto &dep : out->commands) { hashField(ctx, dep->id.c_str()); hashField(ctx, dep->digest.c_str()); }
        u8 digest[SHA256_DIGEST_SIZE]; sha256Final(&ctx, digest); sha256ToHex(digest, out->closure);
        out->model_lease = model->lease; out->model_release = model->release;
        return out.release();
    } catch (const std::exception &e) { if (error && cap) std::snprintf(error, cap, "%s", e.what()); }
    catch (...) { if (error && cap) std::snprintf(error, cap, "%s", "equipped source allocation failed"); }
    return nullptr;
}
extern "C" void wgV2EquippedRetain(wg_v2_equipped *v) { if (v) ++v->refs; }
extern "C" void wgV2EquippedRelease(wg_v2_equipped *v) { if (v && --v->refs == 0) delete v; }
extern "C" const struct weapon *wgV2EquippedWeapon(const wg_v2_equipped *v) { return v ? &v->weapon : nullptr; }
extern "C" wg_v2_native_bundle *wgV2EquippedActions(const wg_v2_equipped *v) { return v ? v->actions : nullptr; }
extern "C" const char *wgV2EquippedClosureHash(const wg_v2_equipped *v) { return v ? v->closure : ""; }
namespace {
struct Factory {
    const weapon_graph_archive_descriptor_t *descriptor;
    const char *source_hash, *settings;
    size_t size;
    const wg_v2_equipped_model *model;
    wg_v2_native_resolver resolver;
    void *host;
    wg_v2_equipped *result = nullptr;
};
void *prepareLease(void *v, wg_v2_program *program, char *error, size_t cap) {
    auto &f = *static_cast<Factory *>(v);
    std::unique_ptr<wg_v2_native_bundle, decltype(&wgV2NativeRelease)> actions(
        wgV2NativePrepare(program, f.resolver, f.host, error, cap), wgV2NativeRelease);
    if (!actions) return nullptr;
    f.result = wgV2EquippedPrepare(actions.get(), f.descriptor, f.source_hash,
        f.settings, f.size, f.model, f.resolver, f.host, error, cap);
    return f.result;
}
void releaseLease(void *v) { wgV2EquippedRelease(static_cast<wg_v2_equipped *>(v)); }
}
extern "C" wg_v2_catalog_entry *wgV2EquippedCatalogPrepare(wg_v2_catalog *catalog, const char *id,
        const char *graph, size_t graph_size, const char *dependency_hash, const wg_v2_use *use,
        const weapon_graph_archive_descriptor_t *descriptor, const char *source_hash,
        const char *settings, size_t settings_size, const wg_v2_equipped_model *model,
        wg_v2_native_resolver resolver, void *host, wg_v2_equipped **out_equipped, char *error, size_t cap) {
    if (out_equipped) *out_equipped = nullptr;
    Factory factory{descriptor, source_hash, settings, settings_size, model, resolver, host};
    auto *entry = wgV2CatalogPrepareWithLease(catalog, id, graph, graph_size, dependency_hash,
        use, prepareLease, &factory, releaseLease, error, cap);
    if (entry && out_equipped) *out_equipped = factory.result;
    return entry;
}
