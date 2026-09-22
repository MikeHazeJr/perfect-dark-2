/* Executable graph compiler and asynchronous DAG executor.
 * Native callbacks own gameplay admission and action effects. */
#include "weapon_graph_v2.h"
#include "modasset_json.h"
#include "modasset_gltf_document.h"
#include "crude_json.h" /* existing port/external/imgui-node-editor include path */
#include "sha256.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
extern "C" {
#include "constants.h"
}

namespace {
using Json = crude_json::value;
enum class Kind { Press, NativeReadyHeld, Ammo, Cooldown, StateGate, StateSet, Hitscan };
enum class Port { Exec, Pass, Completed, Blocked, Failed, Cancelled };
struct IrDelete { void operator()(weapon_graph_ir_t *ir) const {
    if (ir) { weaponGraphIrFree(ir); delete ir; }
} };
struct Node {
    std::string id;
    std::string params_json;
    Kind kind;
    int ammo_slot = 0, required = 1;
    size_t state_index = 0;
    bool state_value = false;
    std::unique_ptr<weapon_graph_ir_t, IrDelete> typed;
    struct Edge { size_t target; Port port; };
    std::vector<Edge> outgoing;
};
struct Span { const char *begin, *end; };
void error(char *out, size_t cap, const std::string &text) {
    if (out && cap) std::snprintf(out, cap, "%s", text.c_str());
}
[[noreturn]] void bad(const std::string &text) { throw std::runtime_error(text); }
std::string string(const Json &v, const std::string &where) {
    if (!v.is_string() || v.get<crude_json::string>().empty() ||
            v.get<crude_json::string>().find('\0') != std::string::npos)
        bad(where + ": expected non-empty string without embedded NUL");
    return v.get<crude_json::string>();
}
bool boolean(const Json &v, const std::string &where) {
    if (!v.is_boolean()) bad(where + ": expected boolean");
    return v.get<crude_json::boolean>();
}
std::string stateKey(const Json &v, const std::string &where) {
    const std::string key = string(v, where);
    if (key.size() > 64 || !((key[0] >= 'a' && key[0] <= 'z') ||
            (key[0] >= 'A' && key[0] <= 'Z'))) bad(where + ": expected short named state key");
    for (const char c : key) if (!((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '.' || c == '-')) bad(where + ": invalid state key character");
    return key;
}
const Json &field(const Json &v, const char *name, const std::string &where) {
    if (!v.is_object() || !v.contains(name)) bad(where + ": missing " + name);
    return v[name];
}
const crude_json::array &array(const Json &v, const std::string &where) {
    if (!v.is_array()) bad(where + ": expected array");
    return v.get<crude_json::array>();
}
void keys(const Json &v, const std::vector<std::string> &allowed, const std::string &where) {
    if (!v.is_object()) bad(where + ": expected object");
    for (const auto &entry : v.get<crude_json::object>())
        if (std::find(allowed.begin(), allowed.end(), entry.first) == allowed.end())
            bad(where + ": unsupported member " + entry.first);
}
bool hash(const char *text) {
    if (!text || std::strlen(text) != 64) return false;
    for (const char *p = text; *p; ++p)
        if (!(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f')) return false;
    return true;
}
/* These are span boundaries over a document already accepted by the shared
 * strict Reader. They never interpret numbers or invent a second JSON lexer.
 * Raw parameter bytes are passed to the existing typed graph compiler. */
const char *ws(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    return p;
}
const char *quoted(const char *p, const char *end) {
    if (p == end || *p != '"') bad("internal validated string span mismatch");
    for (++p; p < end; ++p) {
        if (*p == '\\') { if (++p == end) break; }
        else if (*p == '"') return p + 1;
    }
    bad("internal validated string span incomplete");
}
const char *valueEnd(const char *p, const char *end) {
    p = ws(p, end);
    if (p == end) bad("internal validated value span missing");
    if (*p == '"') return quoted(p, end);
    if (*p == '{' || *p == '[') {
        size_t depth = 0;
        for (; p < end; ++p) {
            if (*p == '"') { p = quoted(p, end) - 1; continue; }
            if (*p == '{' || *p == '[') ++depth;
            else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
        }
        bad("internal validated container span incomplete");
    }
    while (p < end && *p != ',' && *p != '}' && *p != ']') ++p;
    return p;
}
Span member(Span object, const char *name) {
    const char *p = ws(object.begin, object.end);
    if (p == object.end || *p++ != '{') bad("internal object span expected");
    while ((p = ws(p, object.end)) < object.end && *p != '}') {
        const char *keyEnd = quoted(p, object.end);
        const int match = modAssetJsonStringEquals(p, keyEnd, name);
        const char *colon = ws(keyEnd, object.end);
        if (match < 0 || colon == object.end || *colon != ':') bad("internal member span mismatch");
        const char *begin = ws(colon + 1, object.end), *end = valueEnd(begin, object.end);
        if (match) return { begin, end };
        p = ws(end, object.end);
        if (p < object.end && *p == ',') ++p;
    }
    return { nullptr, nullptr };
}
std::vector<Span> elements(Span arraySpan) {
    std::vector<Span> out;
    const char *p = ws(arraySpan.begin, arraySpan.end);
    if (p == arraySpan.end || *p++ != '[') bad("internal array span expected");
    while ((p = ws(p, arraySpan.end)) < arraySpan.end && *p != ']') {
        const char *end = valueEnd(p, arraySpan.end);
        out.push_back({ p, end }); p = ws(end, arraySpan.end);
        if (p < arraySpan.end && *p == ',') ++p;
    }
    return out;
}
int exactInt(Span span, int low, int high, const std::string &where) {
    s32 result = 0; const char *end = nullptr;
    if (!span.begin || !modAssetJsonParseS32Token(span.begin, span.end, &result, &end) ||
            ws(end, span.end) != span.end || result < low || result > high)
        bad(where + ": expected integer in [" + std::to_string(low) + "," + std::to_string(high) + "]");
    return result;
}
std::unique_ptr<weapon_graph_ir_t, IrDelete> compileAction(Span source, const Json &node,
        const std::string &id, const std::string &mode) {
    if (string(field(node, "module", id), id + ".module") != "og.fire.hitscan")
        bad(id + ": unsupported native module (expected og.fire.hitscan)");
    exactInt(member(source, "module_version"), 1, 1, id + ".module_version");
    const Json &params = field(node, "params", id);
    keys(params, { "mode", "function_type", "ammo_slot", "flags", "damage", "spread",
        "recoil_anim_unk24", "recoil_anim_unk25", "recoil_anim_unk26", "recoil_anim_unk27",
        "recoildist", "recoilangle", "slidemax", "impactforce", "duration_ticks60",
        "shoot_sound_catalog_id", "penetration", "recoverytime_ticks60",
        "noisesettings", "recoilsettings", "fire_animation" }, id + ".params");
    if (string(field(params, "mode", id), id + ".params.mode") != mode ||
            string(field(params, "function_type", id), id + ".params.function_type") != "shoot_single")
        bad(id + ": first executor profile requires its declared mode and shoot_single");
    Span raw = member(source, "params");
    exactInt(member(raw, "ammo_slot"), -1, 1, id + ".params.ammo_slot");
    if (!params.contains("flags")) bad(id + ".params: missing flags");
    std::string one = "{\"schema\":\"pd.weapon_graph.v1\",\"graph_id\":\"native_validation\","
        "\"nodes\":[{\"id\":\"selected_action\",\"kind\":\"fire.hitscan\",\"params\":";
    /* Structured native dependencies keep their complete original bytes in
     * Node. The existing scalar compiler receives only scalar members, with
     * their original token spelling; no JSON stringify round trip. */
    one += "{";
    bool comma = false;
    for (const auto &entry : params.get<crude_json::object>()) {
        if (entry.first == "noisesettings" || entry.first == "recoilsettings" ||
                entry.first == "fire_animation") continue;
        Span value = member(raw, entry.first.c_str());
        if (comma) one += ",";
        comma = true;
        one += "\"" + entry.first + "\":";
        one.append(value.begin, value.end);
    }
    one += "}}]}";
    auto ir = std::unique_ptr<weapon_graph_ir_t, IrDelete>(new weapon_graph_ir_t{});
    char diagnostic[512]{};
    if (one.size() > UINT32_MAX || weaponGraphCompileJson(ASSET_WEAPON, one.data(),
            static_cast<u32>(one.size()), ir.get(), diagnostic, sizeof(diagnostic)) != 0)
        bad(id + ": " + diagnostic);
    for (s32 i = 0; i < ir->param_count; ++i) {
        const auto &p = ir->params[i];
        if (std::strcmp(p.key, "flags") == 0 && (p.type != WEAPON_GRAPH_PARAM_INT ||
                (static_cast<u32>(p.i_value) & (FUNCFLAG_BURST2 | FUNCFLAG_BURST3 | FUNCFLAG_BURST5 | FUNCFLAG_BURST50))))
            bad(id + ": burst flags require a later native module profile");
    }
    return ir;
}
const char *portName(Port p) {
    switch (p) { case Port::Exec: return "exec"; case Port::Pass: return "pass";
        case Port::Completed: return "completed"; case Port::Blocked: return "blocked";
        case Port::Failed: return "failed"; case Port::Cancelled: return "cancelled"; }
    return "invalid";
}
Port output(Kind kind, const std::string &value, const std::string &where) {
    const bool event = kind == Kind::Press || kind == Kind::NativeReadyHeld;
    if (event && value == "exec") return Port::Exec;
    if (kind == Kind::StateSet && value == "exec") return Port::Exec;
    if ((kind == Kind::Ammo || kind == Kind::Cooldown || kind == Kind::StateGate) && value == "pass") return Port::Pass;
    if (kind == Kind::Hitscan && value == "completed") return Port::Completed;
    if (kind == Kind::Hitscan && value == "cancelled") return Port::Cancelled;
    if (kind != Kind::StateSet && !event && value == "blocked") return Port::Blocked;
    if (kind != Kind::StateSet && kind != Kind::StateGate && !event && value == "failed") return Port::Failed;
    bad(where + ": unsupported output port " + value);
}
}

struct wg_v2_program {
    std::atomic<size_t> refs{1};
    std::string asset, graph, mode, source_hash, dependency_hash;
    uint64_t generation = 0;
    std::vector<Node> nodes;
    size_t state_count = 0;
    std::vector<size_t> press_roots;
    std::vector<size_t> native_ready_held_roots;
};

extern "C" wg_v2_program *wgV2Compile(const char *json, size_t size, uint64_t generation,
        const char *dependencies, char *err, size_t cap) {
    if (err && cap) err[0] = 0;
    try {
        Json root;
        if (!generation || !hash(dependencies)) bad("program requires nonzero generation and dependency SHA256");
        if (!modAssetJsonReadValue(json, size, root) || !root.is_object()) bad("graph: invalid complete strict JSON object");
        keys(root, { "schema", "profile", "asset_id", "graph_id", "mode", "nodes", "edges", "exports" }, "graph");
        if (string(field(root, "schema", "graph"), "schema") != "pd.weapon_graph.v2" ||
                string(field(root, "profile", "graph"), "profile") != "held_single_shot.v1")
            bad("graph: expected pd.weapon_graph.v2 / held_single_shot.v1; v1 migration must be explicit");
        auto program = std::make_unique<wg_v2_program>();
        program->generation = generation; program->dependency_hash = dependencies;
        program->asset = string(field(root, "asset_id", "graph"), "asset_id");
        auto colon = program->asset.find(':');
        if (program->asset.size() >= CATALOG_ID_LEN || colon == 0 || colon == std::string::npos || colon + 1 == program->asset.size())
            bad("asset_id: expected catalog identity within native byte domain");
        program->graph = string(field(root, "graph_id", "graph"), "graph_id");
        program->mode = string(field(root, "mode", "graph"), "mode");
        if (program->mode != "primary" && program->mode != "secondary") bad("mode: expected primary or secondary");
        const auto &rows = array(field(root, "nodes", "graph"), "nodes");
        const auto spans = elements(member({json, json + size}, "nodes"));
        if (rows.empty() || spans.size() != rows.size()) bad("nodes: expected non-empty node table");
        std::unordered_map<std::string, size_t> ids;
        std::unordered_map<std::string, size_t> state_ids;
        std::vector<uint8_t> state_has_setter;
        for (size_t i = 0; i < rows.size(); ++i) {
            const Json &row = rows[i];
            const std::string id = string(field(row, "id", "node"), "node.id");
            if (!ids.emplace(id, i).second) bad(id + ": duplicate decoded node ID");
            const auto kind = string(field(row, "kind", id), id + ".kind");
            Node node; node.id = id;
            if (kind == "fire.hitscan") {
                keys(row, { "id", "kind", "module", "module_version", "params" }, id);
                node.kind = Kind::Hitscan;
                node.typed = compileAction(spans[i], row, id, program->mode);
                const Span params = member(spans[i], "params");
                node.params_json.assign(params.begin, params.end);
            } else if (kind == "event.trigger_pressed") {
                keys(row, { "id", "kind" }, id); node.kind = Kind::Press;
            } else if (kind == "event.native_ready_while_held") {
                keys(row, { "id", "kind" }, id); node.kind = Kind::NativeReadyHeld;
            } else if (kind == "gate.cooldown_ready") {
                keys(row, { "id", "kind" }, id); node.kind = Kind::Cooldown;
            } else if (kind == "gate.ammo_available") {
                keys(row, { "id", "kind", "params" }, id); node.kind = Kind::Ammo;
                keys(field(row, "params", id), { "ammo_slot", "required" }, id + ".params");
                Span params = member(spans[i], "params");
                node.ammo_slot = exactInt(member(params, "ammo_slot"), -1, 1, id + ".ammo_slot");
                node.required = exactInt(member(params, "required"), 1, INT32_MAX, id + ".required");
            } else if (kind == "gate.state_bool" || kind == "state.bool_set") {
                keys(row, { "id", "kind", "params" }, id);
                const Json &params = field(row, "params", id);
                const bool setter = kind == "state.bool_set";
                keys(params, setter ? std::vector<std::string>{ "key", "value" } :
                    std::vector<std::string>{ "key", "equals" }, id + ".params");
                const std::string key = stateKey(field(params, "key", id), id + ".params.key");
                auto [state, inserted] = state_ids.emplace(key, state_ids.size());
                if (inserted) state_has_setter.push_back(0);
                node.kind = setter ? Kind::StateSet : Kind::StateGate;
                node.state_index = state->second;
                node.state_value = boolean(field(params, setter ? "value" : "equals", id),
                    id + (setter ? ".params.value" : ".params.equals"));
                if (setter) state_has_setter[node.state_index] = 1;
            } else bad(id + ": module " + kind + " is outside executable profile held_single_shot.v1");
            program->nodes.push_back(std::move(node));
        }
        for (const Node &node : program->nodes)
            if (node.kind == Kind::StateGate && !state_has_setter[node.state_index])
                bad(node.id + ": state gate has no authored setter");
        program->state_count = state_ids.size();
        std::vector<size_t> indegree(rows.size(), 0);
        for (const Json &edge : array(field(root, "edges", "graph"), "edges")) {
            keys(edge, { "from", "output", "to", "input" }, "edge");
            const auto from = string(field(edge, "from", "edge"), "edge.from");
            const auto to = string(field(edge, "to", "edge"), "edge.to");
            if (!ids.count(from) || !ids.count(to)) bad("edge " + from + " -> " + to + ": missing node");
            Node &source = program->nodes[ids.at(from)];
            const Kind targetKind = program->nodes[ids.at(to)].kind;
            if (targetKind == Kind::Press || targetKind == Kind::NativeReadyHeld ||
                    string(field(edge, "input", "edge"), "edge.input") != "exec")
                bad("edge to " + to + ": expected executable input exec; events have no input");
            const Port port = output(source.kind, string(field(edge, "output", "edge"), "edge.output"), from);
            for (const auto &old : source.outgoing)
                if (old.port == port && old.target == ids.at(to)) bad(from + ": duplicate edge to " + to);
            source.outgoing.push_back({ ids.at(to), port }); ++indegree[ids.at(to)];
        }
        std::unordered_map<std::string, size_t> exports;
        for (const Json &entry : array(field(root, "exports", "graph"), "exports")) {
            keys(entry, { "name", "node" }, "export");
            auto name = string(field(entry, "name", "export"), "export.name");
            auto node = string(field(entry, "node", "export"), "export.node");
            if (!exports.emplace(name, 0).second) bad("duplicate decoded export " + name);
            if (!ids.count(node)) bad("export " + name + ": missing selected event node " + node);
            const Kind selected = program->nodes[ids.at(node)].kind;
            if (name == "trigger_pressed" && selected == Kind::Press)
                program->press_roots.push_back(ids.at(node));
            else if (name == "native_ready_while_held" && selected == Kind::NativeReadyHeld)
                program->native_ready_held_roots.push_back(ids.at(node));
            else bad("export " + name + ": expected matching trigger_pressed or native_ready_while_held event root");
        }
        if (program->press_roots.empty()) bad("exports: missing trigger_pressed event root");
        std::vector<size_t> work;
        for (size_t i = 0; i < indegree.size(); ++i) if (!indegree[i]) work.push_back(i);
        for (size_t i = 0; i < work.size(); ++i)
            for (auto edge : program->nodes[work[i]].outgoing)
                if (--indegree[edge.target] == 0) work.push_back(edge.target);
        if (work.size() != rows.size()) bad("edges: cycle; this profile requires an acyclic activation graph");
        std::vector<uint8_t> reached(rows.size(), 0); work = program->press_roots;
        work.insert(work.end(), program->native_ready_held_roots.begin(), program->native_ready_held_roots.end());
        for (auto i : work) reached[i] = 1;
        for (size_t i = 0; i < work.size(); ++i)
            for (auto edge : program->nodes[work[i]].outgoing)
                if (!reached[edge.target]) { reached[edge.target] = 1; work.push_back(edge.target); }
        if (work.size() != rows.size()) bad("nodes: node unreachable from an exported event root");
        u8 digest[SHA256_DIGEST_SIZE]; char hexadecimal[SHA256_HEX_SIZE];
        sha256Hash(json, size, digest); sha256ToHex(digest, hexadecimal);
        program->source_hash = hexadecimal;
        return program.release();
    } catch (const std::exception &e) { error(err, cap, e.what()); return nullptr; }
    catch (...) { error(err, cap, "graph compile allocation failure"); return nullptr; }
}

extern "C" void wgV2ProgramRetain(wg_v2_program *p) { if (p) ++p->refs; }
extern "C" void wgV2ProgramRelease(wg_v2_program *p) { if (p && --p->refs == 0) delete p; }
extern "C" const char *wgV2ProgramSourceHash(const wg_v2_program *p) { return p ? p->source_hash.c_str() : ""; }
extern "C" const char *wgV2ProgramDependencyHash(const wg_v2_program *p) { return p ? p->dependency_hash.c_str() : ""; }
extern "C" const char *wgV2CompilerVersion(void) { return "pd.weapon_graph.executor.v2.2"; }
extern "C" size_t wgV2ProgramNodeCount(const wg_v2_program *p) { return p ? p->nodes.size() : 0; }
extern "C" const char *wgV2ProgramAssetId(const wg_v2_program *p) { return p ? p->asset.c_str() : ""; }
extern "C" const char *wgV2ProgramMode(const wg_v2_program *p) { return p ? p->mode.c_str() : ""; }
extern "C" uint64_t wgV2ProgramGeneration(const wg_v2_program *p) { return p ? p->generation : 0; }
extern "C" int wgV2ProgramAction(const wg_v2_program *p, size_t i, wg_v2_action *out) {
    if (!p || !out || i >= p->nodes.size() || p->nodes[i].kind != Kind::Hitscan) return 0;
    const Node &node = p->nodes[i];
    *out = {node.id.c_str(), p->asset.c_str(), p->mode.c_str(), "og.fire.hitscan", 1,
        node.typed.get(), node.params_json.data(), node.params_json.size()};
    return 1;
}

struct wg_v2_instance {
    struct Job { wg_v2_result status = WG_V2_IGNORED; uint64_t ticket = 0; void *state = nullptr; };
    wg_v2_program *program;
    wg_v2_host ops;
    void *host;
    uint64_t owner, last_press = 0, pending_press = 0, activation = 0, next_ticket = 0, last_step = 0;
    uint64_t last_idle_visit = 0;
    bool active = false, cancelled = false, entered = false;
    wg_v2_result result = WG_V2_COMPLETED;
    char diagnostic[512]{};
    std::vector<uint8_t> visited;
    std::vector<uint8_t> bool_state;
    std::vector<size_t> ready;
    size_t head = 0, jobs_active = 0;
    std::vector<Job> jobs;
    wg_v2_instance(wg_v2_program *p, const wg_v2_host &h, void *v, uint64_t o)
        : program(p), ops(h), host(v), owner(o), visited(p->nodes.size()),
          bool_state(p->state_count), jobs(p->nodes.size()) {
        ready.reserve(p->nodes.size()); wgV2ProgramRetain(p);
    }
    ~wg_v2_instance() { wgV2ProgramRelease(program); }
    wg_v2_action action(size_t i) const {
        wg_v2_action out{};
        wgV2ProgramAction(program, i, &out);
        return out;
    }
    void enqueue(size_t node) { if (!visited[node]) { visited[node] = 1; ready.push_back(node); } }
    void route(size_t node, Port port) {
        if (ops.trace) ops.trace(host, program->nodes[node].id.c_str(), portName(port), activation);
        for (auto edge : program->nodes[node].outgoing) if (edge.port == port) enqueue(edge.target);
    }
    void finish(size_t i, wg_v2_result value) {
        Job &job = jobs[i];
        if (value == WG_V2_RUNNING || value == WG_V2_WAITING) { job.status = value; return; }
        auto a = action(i);
        ops.cleanup(host, &a, job.ticket, job.state);
        job.state = nullptr; job.status = WG_V2_IGNORED; --jobs_active;
        if (value == WG_V2_COMPLETED) route(i, Port::Completed);
        else if (value == WG_V2_BLOCKED) { result = WG_V2_BLOCKED; route(i, Port::Blocked); }
        else if (value == WG_V2_CANCELLED) { result = WG_V2_CANCELLED; route(i, Port::Cancelled); }
        else { result = WG_V2_FAILED; route(i, Port::Failed); }
    }
    void pump() {
        while (head < ready.size() && !cancelled) {
            const size_t i = ready[head++]; const Node &node = program->nodes[i];
            if (node.kind == Kind::Press || node.kind == Kind::NativeReadyHeld) { route(i, Port::Exec); continue; }
            if (node.kind == Kind::StateSet) {
                bool_state[node.state_index] = node.state_value;
                route(i, Port::Exec); continue;
            }
            if (node.kind == Kind::StateGate) {
                if (bool_state[node.state_index] == node.state_value) route(i, Port::Pass);
                else { result = WG_V2_BLOCKED; route(i, Port::Blocked); }
                continue;
            }
            if (node.kind != Kind::Hitscan) {
                const int pass = ops.gate(host, node.kind == Kind::Ammo ? WG_V2_AMMO_AVAILABLE : WG_V2_COOLDOWN_READY,
                    node.ammo_slot, node.required, diagnostic, sizeof(diagnostic));
                if (pass == 1) route(i, Port::Pass);
                else if (pass == 0) { result = WG_V2_BLOCKED; route(i, Port::Blocked); }
                else { result = WG_V2_FAILED; route(i, Port::Failed); }
                continue;
            }
            if (next_ticket == UINT64_MAX) {
                result = WG_V2_FAILED;
                error(diagnostic, sizeof(diagnostic), "native ticket exhaustion");
                route(i, Port::Failed);
                continue;
            }
            Job &job = jobs[i]; job.ticket = ++next_ticket; job.state = nullptr; ++jobs_active;
            auto a = action(i);
            finish(i, ops.start(host, &a, job.ticket, &job.state, diagnostic, sizeof(diagnostic)));
        }
        if (!jobs_active && head == ready.size()) active = false;
    }
    void begin(const std::vector<size_t> &roots) {
        std::fill(visited.begin(), visited.end(), 0); ready.clear(); head = 0;
        ++activation; active = true; result = WG_V2_COMPLETED; diagnostic[0] = 0;
        for (auto root : roots) enqueue(root);
        pump();
    }
};

extern "C" wg_v2_instance *wgV2Bind(wg_v2_program *program, const wg_v2_host *ops, void *host,
        uint64_t owner, char *err, size_t cap) {
    try {
        if (!program || !ops || !owner || !ops->alive || !ops->prepare || !ops->gate ||
                !ops->start || !ops->tick || !ops->wake || !ops->cancel || !ops->cleanup)
            bad("bind: complete native lifecycle callbacks and owner generation required");
        auto instance = std::make_unique<wg_v2_instance>(program, *ops, host, owner);
        if (!ops->alive(host, owner)) bad("bind: host generation already retired");
        for (size_t i = 0; i < program->nodes.size(); ++i) if (program->nodes[i].kind == Kind::Hitscan) {
            auto action = instance->action(i);
            if (ops->prepare(host, &action, err, cap) != 1) {
                if (!err || !cap || !err[0]) error(err, cap,
                    std::string(action.source_node_id) + ": native dependency preparation failed");
                return nullptr;
            }
        }
        return instance.release();
    } catch (const std::exception &e) { error(err, cap, e.what()); return nullptr; }
    catch (...) { error(err, cap, "bind: allocation failure"); return nullptr; }
}
extern "C" void wgV2Cancel(wg_v2_instance *i, const char *reason) {
    if (!i || i->cancelled) return;
    i->cancelled = true; i->pending_press = 0; i->active = false; i->result = WG_V2_CANCELLED;
    std::snprintf(i->diagnostic, sizeof(i->diagnostic), "%s", reason ? reason : "cancelled");
    for (size_t n = 0; n < i->jobs.size(); ++n) {
        auto &job = i->jobs[n];
        if (job.status != WG_V2_RUNNING && job.status != WG_V2_WAITING) continue;
        auto a = i->action(n); i->ops.cancel(i->host, &a, job.ticket, job.state, i->diagnostic);
        i->ops.cleanup(i->host, &a, job.ticket, job.state);
        job.state = nullptr; job.status = WG_V2_IGNORED;
    }
    i->jobs_active = 0;
}
namespace {
bool valid(wg_v2_instance *i, uint64_t owner, uint64_t source) {
    if (!i || i->cancelled || i->entered) return false;
    if (owner != i->owner || source != i->program->generation || !i->ops.alive(i->host, i->owner)) {
        wgV2Cancel(i, "owner or source generation retired"); return false;
    }
    return true;
}
struct Enter { wg_v2_instance &i; explicit Enter(wg_v2_instance &v) : i(v) { i.entered = true; }
    ~Enter() { i.entered = false; } };
wg_v2_result status(const wg_v2_instance *i) {
    if (!i) return WG_V2_FAILED;
    if (i->cancelled) return WG_V2_CANCELLED;
    if (!i->active) return i->result;
    for (const auto &job : i->jobs) if (job.status == WG_V2_RUNNING) return WG_V2_RUNNING;
    return WG_V2_WAITING;
}
}
extern "C" wg_v2_result wgV2Press(wg_v2_instance *i, uint64_t sequence, uint64_t owner, uint64_t source) {
    if (!valid(i, owner, source)) return status(i);
    if (!sequence || sequence <= i->last_press) return WG_V2_IGNORED;
    Enter enter(*i); i->last_press = sequence; i->pending_press = sequence;
    return WG_V2_QUEUED;
}
extern "C" void wgV2ReleasePendingPress(wg_v2_instance *i) { if (i) i->pending_press = 0; }
extern "C" wg_v2_result wgV2NativeIdle(wg_v2_instance *i, uint64_t visit, int held,
        uint64_t owner, uint64_t source) {
    if (!valid(i, owner, source)) return status(i);
    if (!visit || visit <= i->last_idle_visit) return WG_V2_IGNORED;
    Enter enter(*i); i->last_idle_visit = visit;
    if (!held) { i->pending_press = 0; return WG_V2_IGNORED; }
    if (i->active) return WG_V2_IGNORED;
    const auto &roots = i->pending_press ? i->program->press_roots : i->program->native_ready_held_roots;
    i->pending_press = 0;
    if (roots.empty()) return WG_V2_IGNORED;
    if (i->activation == UINT64_MAX) { wgV2Cancel(i, "activation identity exhausted"); return WG_V2_FAILED; }
    i->begin(roots); return status(i);
}
extern "C" wg_v2_result wgV2Advance(wg_v2_instance *i, uint64_t step, double delta,
        uint64_t owner, uint64_t source) {
    if (!valid(i, owner, source)) return status(i);
    if (!std::isfinite(delta) || delta < 0) { wgV2Cancel(i, "invalid simulation delta"); return WG_V2_FAILED; }
    if (!step || step <= i->last_step || delta == 0) return WG_V2_IGNORED;
    Enter enter(*i); i->last_step = step;
    /* New starts routed by completion are not ticked in this same advance. */
    const uint64_t lastTicket = i->next_ticket;
    for (size_t n = 0; n < i->jobs.size(); ++n) {
        auto &job = i->jobs[n];
        if (job.status != WG_V2_RUNNING || job.ticket > lastTicket) continue;
        auto a = i->action(n);
        i->finish(n, i->ops.tick(i->host, &a, job.ticket, job.state, delta, i->diagnostic, sizeof(i->diagnostic)));
    }
    i->pump();
    /* Completion never begins pending input. Only the real idle seam may do
     * so, after native statecycles/frames and switch/reload housekeeping. */
    return status(i);
}
extern "C" wg_v2_result wgV2Wake(wg_v2_instance *i, uint64_t ticket, uint32_t event,
        uint64_t owner, uint64_t source) {
    if (!valid(i, owner, source)) return status(i);
    Enter enter(*i);
    for (size_t n = 0; n < i->jobs.size(); ++n) {
        auto &job = i->jobs[n];
        if (job.status != WG_V2_WAITING || job.ticket != ticket) continue;
        auto a = i->action(n);
        i->finish(n, i->ops.wake(i->host, &a, ticket, job.state, event, i->diagnostic, sizeof(i->diagnostic)));
        i->pump(); return status(i);
    }
    return WG_V2_IGNORED;
}
extern "C" void wgV2Destroy(wg_v2_instance *i) { if (i) { wgV2Cancel(i, "destroyed"); delete i; } }
extern "C" const char *wgV2Error(const wg_v2_instance *i) { return i ? i->diagnostic : "null instance"; }
extern "C" uint64_t wgV2ActiveTicket(const wg_v2_instance *i, const char *id) {
    if (!i || !id) return 0;
    for (size_t n = 0; n < i->jobs.size(); ++n)
        if (i->program->nodes[n].id == id && (i->jobs[n].status == WG_V2_RUNNING || i->jobs[n].status == WG_V2_WAITING))
            return i->jobs[n].ticket;
    return 0;
}
extern "C" int wgV2Busy(const wg_v2_instance *i) { return i && i->active; }
