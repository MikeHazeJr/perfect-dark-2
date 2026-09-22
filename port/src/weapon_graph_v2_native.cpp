/* original public graph values -> immutable native shoot record.
 * No mutable global loader pool, implicit base record, or gameplay dispatch. */
#include "weapon_graph_v2_native.h"
#include "modasset_gltf_document.h"
#include "crude_json.h"
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
/* types.h defines the legacy bool macro; parse native structs with that ABI,
 * then restore the C++ keyword before declaring this implementation. */
#include "types.h"
extern "C" {
#include "constants.h"
}
#undef bool

namespace {
using Json = crude_json::value;
[[noreturn]] void bad(const std::string &text) { throw std::runtime_error(text); }
void error(char *out, size_t cap, const char *text) { if (out && cap) std::snprintf(out, cap, "%s", text); }
const Json &field(const Json &v, const char *key) {
    if (!v.is_object() || !v.contains(key)) bad(std::string("missing native parameter ") + key);
    return v[key];
}
void keys(const Json &v, const std::vector<std::string> &allowed, const char *where) {
    if (!v.is_object()) bad(std::string(where) + ": expected object or explicit null");
    for (const auto &entry : v.get<crude_json::object>())
        if (std::find(allowed.begin(), allowed.end(), entry.first) == allowed.end())
            bad(std::string(where) + ": unsupported member " + entry.first);
}
float number(const Json &v, const char *where, bool positive = false) {
    if (!v.is_number()) bad(std::string(where) + ": expected finite number");
    const double value = v.get<crude_json::number>();
    if (!(value >= 0.0 && value <= FLT_MAX) || (positive && !(value > 0.0)))
        bad(std::string(where) + ": expected representable nonnegative native float");
    const float result = static_cast<float>(value);
    if (positive && !(result > 0.0f)) bad(std::string(where) + ": underflows positive native float");
    return result;
}
const weapon_graph_ir_param_t &param(const wg_v2_action &a, const char *key) {
    if (!a.typed || a.typed->node_count != 1) bad("native action requires its typed single-node IR");
    for (s32 i = 0; i < a.typed->param_count; ++i)
        if (!std::strcmp(a.typed->params[i].key, key)) return a.typed->params[i];
    bad(std::string("missing native parameter ") + key);
}
int integer(const wg_v2_action &a, const char *key, int low, int high) {
    const auto &p = param(a, key);
    if (p.type != WEAPON_GRAPH_PARAM_INT || p.i_value < low || p.i_value > high)
        bad(std::string(key) + ": integer outside native profile range");
    return p.i_value;
}
float scalar(const wg_v2_action &a, const char *key) {
    const auto &p = param(a, key);
    float result;
    if (p.type == WEAPON_GRAPH_PARAM_INT) result = static_cast<float>(p.i_value);
    else if (p.type == WEAPON_GRAPH_PARAM_FLOAT) result = p.f_value;
    else bad(std::string(key) + ": expected numeric native parameter");
    if (!(result >= 0.0f && result <= FLT_MAX)) bad(std::string(key) + ": invalid nonnegative native float");
    return result;
}
bool hash(const char *value) {
    if (!value || std::strlen(value) != 64) return false;
    for (const char *p = value; *p; ++p)
        if (!(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f')) return false;
    return true;
}
std::string catalogId(const Json &v, const char *where) {
    if (!v.is_string()) bad(std::string(where) + ": expected catalog ID or explicit null");
    const auto &id = v.get<crude_json::string>();
    const auto colon = id.find(':');
    if (id.size() >= CATALOG_ID_LEN || id.find('\0') != std::string::npos ||
            colon == 0 || colon == std::string::npos || colon + 1 == id.size())
        bad(std::string(where) + ": invalid catalog identity");
    return id;
}
struct Dependency {
    wg_v2_native_dependency value{};
    std::string id, digest;
    wg_v2_dependency_kind kind{};
    ~Dependency() { if (value.lease && value.release) value.release(value.lease); }
};
void hashField(sha256_ctx &ctx, const char *text) {
    /* NUL terminators delimit decoded IDs unambiguously; embedded NUL rejects. */
    sha256Update(&ctx, text, std::strlen(text) + 1);
}
}

struct wg_v2_native_action {
    std::string id;
    struct weaponfunc_shootsingle function{};
    struct noisesettings noise{};
    struct recoilsettings recoil{};
    weapon_graph_held_function_t held{};
};
struct wg_v2_native_bundle {
    std::atomic<size_t> refs{1};
    wg_v2_program *program = nullptr;
    std::vector<std::unique_ptr<wg_v2_native_action>> actions;
    std::vector<std::unique_ptr<Dependency>> dependencies;
    char closure_hash[SHA256_HEX_SIZE]{};
    ~wg_v2_native_bundle() { wgV2ProgramRelease(program); }
};

namespace {
const wg_v2_native_dependency *resolve(wg_v2_native_bundle &bundle,
        const Json &source, const char *where, wg_v2_dependency_kind kind,
        wg_v2_native_resolver resolver, void *host) {
    if (source.is_null()) return nullptr;
    const auto id = catalogId(source, where);
    for (const auto &old : bundle.dependencies)
        if (old->id == id && old->kind == kind) return &old->value;
    if (!resolver) bad(std::string(where) + ": public dependency resolver is required");
    auto dep = std::make_unique<Dependency>(); dep->id = id; dep->kind = kind;
    char diagnostic[512]{};
    if (!resolver(host, kind, id.c_str(), &dep->value, diagnostic, sizeof(diagnostic)))
        bad(std::string(where) + ": " + (diagnostic[0] ? diagnostic : "dependency preparation failed"));
    auto &v = dep->value;
    if (!v.catalog_id || id != v.catalog_id || !hash(v.source_closure_sha256) ||
            !v.lease || !v.release ||
            (kind == WG_V2_COMMAND_SOURCE && !v.commands) ||
            (kind == WG_V2_SOUND_SOURCE && (v.sound_id <= 0 || v.sound_id > 65535)))
        bad(std::string(where) + ": resolver returned invalid identity, native binding or unowned dependency");
    dep->digest = v.source_closure_sha256;
    /* Rebind borrowed metadata to owned strings before the resolver can reuse it. */
    v.catalog_id = dep->id.c_str(); v.source_closure_sha256 = dep->digest.c_str();
    const auto *result = &dep->value;
    bundle.dependencies.push_back(std::move(dep));
    return result;
}
void prepareAction(wg_v2_native_bundle &bundle, const wg_v2_action &source,
        wg_v2_native_resolver resolver, void *host) {
    Json root;
    if (!modAssetJsonReadValue(source.params_json, source.params_size, root) || !root.is_object())
        bad("missing original strict action parameters");
    if ((std::strcmp(source.mode, "primary") && std::strcmp(source.mode, "secondary")) ||
            std::strcmp(source.module, "og.fire.hitscan") || source.module_version != 1)
        bad("native action is outside held og.fire.hitscan v1 profile");
    auto action = std::make_unique<wg_v2_native_action>(); action->id = source.source_node_id;
    auto &f = action->function.base;
    auto &h = action->held;
    f.base.type = INVENTORYFUNCTYPE_SHOOT_SINGLE;
    f.base.ammoindex = static_cast<s8>(integer(source, "ammo_slot", -1, 1));
    const u32 flags = static_cast<u32>(integer(source, "flags", 0, INT32_MAX));
    constexpr u32 supported = FUNCFLAG_NOAUTOAIM | FUNCFLAG_NOMUZZLEFLASH | FUNCFLAG_BLUNTIMPACT | FUNCFLAG_NOSTUN;
    if (flags & ~supported) bad("flags: outside audited plain single-shot native profile");
    f.base.flags = flags;
    h.valid = 1; h.opcode = WEAPON_GRAPH_OP_FIRE_HITSCAN;
    std::strcpy(h.parity_module, "og.fire.hitscan");
    std::strcpy(h.node_id, "selected_action"); /* full author ID is action.id */
    std::strcpy(h.mode, source.mode); std::strcpy(h.function_type, "shoot_single");
    h.has_function_type_id = 1; h.function_type_id = f.base.type;
    h.ammo_slot = f.base.ammoindex; h.flags = flags;
    #define FLOAT_FIELD(key, native) h.has_##key = 1; h.key = scalar(source, #key); f.native = h.key
    FLOAT_FIELD(damage, damage); FLOAT_FIELD(spread, spread);
    FLOAT_FIELD(recoildist, recoildist); FLOAT_FIELD(recoilangle, recoilangle);
    FLOAT_FIELD(slidemax, slidemax); FLOAT_FIELD(impactforce, impactforce);
    #undef FLOAT_FIELD
    #define INT_FIELD(key, native, low, high) h.has_##key = 1; h.key = integer(source, #key, low, high); f.native = static_cast<decltype(f.native)>(h.key)
    INT_FIELD(recoil_anim_unk24, unk24, 0, 127);
    INT_FIELD(recoil_anim_unk25, unk25, 0, 127);
    INT_FIELD(recoil_anim_unk26, unk26, -128, 127);
    INT_FIELD(recoil_anim_unk27, unk27, -128, 127);
    INT_FIELD(recoverytime_ticks60, recoverytime60, 0, 127);
    INT_FIELD(duration_ticks60, duration60, 0, 255);
    INT_FIELD(penetration, penetration, 1, 255);
    #undef INT_FIELD
    /* unk24/25 are nonnegative, so sum <=254, sum+recovery<=381; native
     * phase guards keep both divisors nonzero on every entered branch. */
    const Json &noise = field(root, "noisesettings");
    /* Authored null means silent. A NULL native pointer would consult the
     * mutable manager default in gsetGetNoiseSettings, so own the silent record. */
    action->noise.decbasespeed = 1.0f;
    action->noise.decremspeed = 6.0f;
    f.base.noisesettings = &action->noise;
    if (!noise.is_null()) {
        keys(noise, {"minradius", "maxradius", "incradius", "decbasespeed", "decremspeed"}, "noisesettings");
        action->noise.minradius = number(field(noise, "minradius"), "minradius");
        action->noise.maxradius = number(field(noise, "maxradius"), "maxradius");
        action->noise.incradius = number(field(noise, "incradius"), "incradius");
        action->noise.decbasespeed = number(field(noise, "decbasespeed"), "decbasespeed", true);
        action->noise.decremspeed = number(field(noise, "decremspeed"), "decremspeed", true);
        if (action->noise.minradius > action->noise.maxradius) bad("noisesettings: minradius exceeds maxradius");
    }
    const Json &recoil = field(root, "recoilsettings");
    if (!recoil.is_null()) {
        /* Native unused fields are not part of this profile's public contract. */
        keys(recoil, {"xrange", "yrange", "zrange"}, "recoilsettings");
        action->recoil.xrange = number(field(recoil, "xrange"), "xrange");
        action->recoil.yrange = number(field(recoil, "yrange"), "yrange");
        action->recoil.zrange = number(field(recoil, "zrange"), "zrange");
        f.recoilsettings = &action->recoil;
    }
    const auto *commands = resolve(bundle, field(root, "fire_animation"), "fire_animation", WG_V2_COMMAND_SOURCE, resolver, host);
    f.base.fire_animation = commands ? const_cast<guncmd *>(commands->commands) : nullptr;
    const auto *sound = resolve(bundle, field(root, "shoot_sound_catalog_id"), "shoot_sound_catalog_id", WG_V2_SOUND_SOURCE, resolver, host);
    h.has_shootsound = 1; h.shootsound = sound ? static_cast<u16>(sound->sound_id) : 0;
    f.shootsound = h.shootsound;
    bundle.actions.push_back(std::move(action));
}
}

extern "C" wg_v2_native_bundle *wgV2NativePrepare(wg_v2_program *program,
        wg_v2_native_resolver resolver, void *host, char *err, size_t cap) {
    if (err && cap) err[0] = 0;
    try {
        if (!program) bad("native source preparation requires a compiled program");
        auto bundle = std::make_unique<wg_v2_native_bundle>();
        wgV2ProgramRetain(program); bundle->program = program;
        for (size_t i = 0; i < wgV2ProgramNodeCount(program); ++i) {
            wg_v2_action action{};
            if (!wgV2ProgramAction(program, i, &action)) continue;
            try { prepareAction(*bundle, action, resolver, host); }
            catch (const std::exception &e) { bad(std::string(action.source_node_id) + ": " + e.what()); }
        }
        if (bundle->actions.empty()) bad("native profile requires at least one prepared action");
        std::sort(bundle->dependencies.begin(), bundle->dependencies.end(), [](const auto &a, const auto &b) {
            return a->kind != b->kind ? a->kind < b->kind : a->id < b->id;
        });
        sha256_ctx ctx; sha256Init(&ctx);
        hashField(ctx, "pd.weapon_graph.native_action.v2.2");
        hashField(ctx, wgV2CompilerVersion());
        hashField(ctx, wgV2ProgramSourceHash(program));
        hashField(ctx, wgV2ProgramDependencyHash(program));
        for (const auto &dep : bundle->dependencies) {
            hashField(ctx, dep->kind == WG_V2_COMMAND_SOURCE ? "command" : "sound");
            hashField(ctx, dep->id.c_str()); hashField(ctx, dep->digest.c_str());
        }
        u8 digest[SHA256_DIGEST_SIZE]; sha256Final(&ctx, digest);
        sha256ToHex(digest, bundle->closure_hash);
        return bundle.release();
    } catch (const std::exception &e) { error(err, cap, e.what()); return nullptr; }
    catch (...) { error(err, cap, "native action allocation failed"); return nullptr; }
}
extern "C" void wgV2NativeRetain(wg_v2_native_bundle *b) { if (b) ++b->refs; }
extern "C" void wgV2NativeRelease(wg_v2_native_bundle *b) { if (b && --b->refs == 0) delete b; }
extern "C" const wg_v2_native_action *wgV2NativeFind(const wg_v2_native_bundle *b, const char *id) {
    if (b && id) for (const auto &a : b->actions) if (a->id == id) return a.get();
    return nullptr;
}
extern "C" const struct weaponfunc *wgV2NativeFunction(const wg_v2_native_action *a) { return a ? &a->function.base.base : nullptr; }
extern "C" const weapon_graph_held_function_t *wgV2NativeHeld(const wg_v2_native_action *a) { return a ? &a->held : nullptr; }
extern "C" const char *wgV2NativeNodeId(const wg_v2_native_action *a) { return a ? a->id.c_str() : ""; }
extern "C" wg_v2_program *wgV2NativeProgram(const wg_v2_native_bundle *b) { return b ? b->program : nullptr; }
extern "C" const char *wgV2NativeClosureHash(const wg_v2_native_bundle *b) { return b ? b->closure_hash : ""; }
extern "C" int wgV2NativeContains(const wg_v2_native_bundle *b, const wg_v2_native_action *a) {
    if (b && a) for (const auto &candidate : b->actions) if (candidate.get() == a) return 1;
    return 0;
}
namespace {
struct NativeFactory { wg_v2_native_resolver resolver; void *host; wg_v2_native_bundle *result = nullptr; };
void *prepareLease(void *v, wg_v2_program *p, char *error, size_t cap) {
    auto &factory = *static_cast<NativeFactory *>(v);
    factory.result = wgV2NativePrepare(p, factory.resolver, factory.host, error, cap);
    return factory.result;
}
void releaseLease(void *value) { wgV2NativeRelease(static_cast<wg_v2_native_bundle *>(value)); }
}
extern "C" wg_v2_catalog_entry *wgV2NativeCatalogPrepare(wg_v2_catalog *catalog,
        const char *id, const char *json, size_t size, const char *digest,
        const wg_v2_use *use, wg_v2_native_resolver resolver, void *host,
        wg_v2_native_bundle **out_native, char *error, size_t cap) {
    if (out_native) *out_native = nullptr;
    NativeFactory factory{resolver, host};
    auto *entry = wgV2CatalogPrepareWithLease(catalog, id, json, size, digest, use,
        prepareLease, &factory, releaseLease, error, cap);
    if (entry && out_native) *out_native = factory.result;
    return entry;
}
