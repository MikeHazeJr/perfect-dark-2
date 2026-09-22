/* Actual draft program execution tests. Fake native state models acceptance,
 * animation wait, commit/debit, recovery, cancellation and cleanup. These are
 * NOT ordinary-client shot, animation, damage, audio, or network parity proof. */
#include "catch.hpp"
#include "weapon_graph_v2.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {
const char *deps = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
struct Host {
    bool alive = true, ready = true, busy = false, wait_start = false, fail_start = false;
    int ammo = 5, prepares = 0, starts = 0, ticks = 0, shots = 0, cancels = 0, cleanups = 0;
    float damage = 0;
    std::vector<std::string> selected, trace;
};
struct Native { int phase = 0; float damage = 0; };
float damage(const wg_v2_action *a) {
    for (s32 i = 0; i < a->typed->param_count; ++i) {
        const auto &p = a->typed->params[i];
        if (std::string(p.key) == "damage") return p.type == WEAPON_GRAPH_PARAM_FLOAT ? p.f_value : p.i_value;
    }
    return 0;
}
wg_v2_host callbacks() {
    wg_v2_host h{};
    h.alive = [](void *v, uint64_t owner) -> int { return static_cast<Host *>(v)->alive && owner == 41; };
    h.prepare = [](void *v, const wg_v2_action *a, char *, size_t) {
        auto &host = *static_cast<Host *>(v); ++host.prepares;
        return a->typed && a->typed->node_count == 1 && a->module_version == 1 ? 1 : 0;
    };
    h.gate = [](void *v, wg_v2_gate g, int slot, int required, char *, size_t) -> int {
        auto &host = *static_cast<Host *>(v);
        return g == WG_V2_AMMO_AVAILABLE ? (slot == -1 || host.ammo >= required) : (host.ready && !host.busy);
    };
    h.start = [](void *v, const wg_v2_action *a, uint64_t, void **state, char *, size_t) {
        auto &host = *static_cast<Host *>(v); ++host.starts;
        if (host.busy || host.ammo == 0) return WG_V2_BLOCKED;
        *state = new Native{0, damage(a)};
        host.selected.emplace_back(a->source_node_id);
        if (host.fail_start) return WG_V2_FAILED;
        host.busy = true;
        return host.wait_start ? WG_V2_WAITING : WG_V2_RUNNING;
    };
    h.tick = [](void *v, const wg_v2_action *, uint64_t, void *state, double, char *, size_t) {
        auto &host = *static_cast<Host *>(v); auto &native = *static_cast<Native *>(state);
        ++host.ticks; ++native.phase;
        if (native.phase == 2) { --host.ammo; ++host.shots; host.damage += native.damage; }
        return native.phase >= 4 ? WG_V2_COMPLETED : WG_V2_RUNNING;
    };
    h.wake = [](void *, const wg_v2_action *, uint64_t, void *, uint32_t event, char *, size_t) {
        return event == 7 ? WG_V2_RUNNING : WG_V2_WAITING;
    };
    h.cancel = [](void *v, const wg_v2_action *, uint64_t, void *, const char *) { ++static_cast<Host *>(v)->cancels; };
    h.cleanup = [](void *v, const wg_v2_action *, uint64_t, void *state) {
        auto &host = *static_cast<Host *>(v); ++host.cleanups;
        if (state) { host.busy = false; delete static_cast<Native *>(state); }
    };
    h.trace = [](void *v, const char *node, const char *port, uint64_t activation) {
        static_cast<Host *>(v)->trace.emplace_back(std::to_string(activation) + ":" + node + "." + port);
    };
    return h;
}
std::string shot(const std::string &id, int value) {
    return "{\"id\":\"" + id + "\",\"kind\":\"fire.hitscan\",\"module\":\"og.fire.hitscan\","
        "\"module_version\":1,\"params\":{\"mode\":\"primary\",\"function_type\":\"shoot_single\","
        "\"ammo_slot\":0,\"flags\":0,\"damage\":" + std::to_string(value) + "}}";
}
std::string graph(const std::string &extraNodes = "", const std::string &extraEdges = "") {
    return std::string(R"({"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",
        "asset_id":"mod:single","graph_id":"primary","mode":"primary","nodes":[
        {"id":"press","kind":"event.trigger_pressed"},
        {"id":"ammo","kind":"gate.ammo_available","params":{"ammo_slot":0,"required":2}},
        {"id":"cool","kind":"gate.cooldown_ready"},)") + shot("shot", 3) + extraNodes +
        R"(],"edges":[{"from":"press","output":"exec","to":"ammo","input":"exec"},
        {"from":"ammo","output":"pass","to":"cool","input":"exec"},
        {"from":"cool","output":"pass","to":"shot","input":"exec"})" + extraEdges +
        R"(],"exports":[{"name":"trigger_pressed","node":"press"}]})";
}
std::string stateGraph(const std::string &equals = "true", bool setFirst = false) {
    const std::string nodes = std::string(R"({"schema":"pd.weapon_graph.v2","profile":"held_single_shot.v1",
        "asset_id":"mod:single","graph_id":"primary","mode":"primary","nodes":[
        {"id":"press","kind":"event.trigger_pressed"},
        {"id":"armed","kind":"gate.state_bool","params":{"key":"armed","equals":)") + equals +
        R"(}},{"id":"arm","kind":"state.bool_set","params":{"key":"armed","value":true}},)" +
        shot("shot", 3) + R"(],"edges":[)";
    const std::string edges = setFirst
        ? R"({"from":"press","output":"exec","to":"arm","input":"exec"},
            {"from":"arm","output":"exec","to":"armed","input":"exec"},
            {"from":"armed","output":"pass","to":"shot","input":"exec"})"
        : R"({"from":"press","output":"exec","to":"armed","input":"exec"},
            {"from":"armed","output":"blocked","to":"arm","input":"exec"},
            {"from":"armed","output":"pass","to":"shot","input":"exec"})";
    return nodes + edges + R"(],"exports":[{"name":"trigger_pressed","node":"press"}]})";
}
struct ProgramFree { void operator()(wg_v2_program *p) const { wgV2ProgramRelease(p); } };
struct InstanceFree { void operator()(wg_v2_instance *i) const { wgV2Destroy(i); } };
using Program = std::unique_ptr<wg_v2_program, ProgramFree>;
using Instance = std::unique_ptr<wg_v2_instance, InstanceFree>;
Program compile(const std::string &text) {
    char error[512]{};
    Program p(wgV2Compile(text.data(), text.size(), 9, deps, error, sizeof(error)));
    INFO(error); REQUIRE(p != nullptr); return p;
}
Instance bind(Program &p, Host &host) {
    auto ops = callbacks(); char error[512]{};
    Instance i(wgV2Bind(p.get(), &ops, &host, 41, error, sizeof(error)));
    INFO(error); REQUIRE(i != nullptr); return i;
}
/* A physical press and native idle admission are separate production seams.
 * Existing executor tests use this helper only to arrange both in order. */
wg_v2_result pressAndIdle(wg_v2_instance *i, uint64_t sequence,
        uint64_t owner, uint64_t source) {
    const auto input = wgV2Press(i, sequence, owner, source);
    if (input != WG_V2_QUEUED || wgV2Busy(i)) return input;
    return wgV2NativeIdle(i, sequence, 1, owner, source);
}
std::string heldGraph(const std::string &destination = "ammo") {
    auto text = graph(",{\"id\":\"ready_held\",\"kind\":\"event.native_ready_while_held\"}",
        ",{\"from\":\"ready_held\",\"output\":\"exec\",\"to\":\"" + destination + "\",\"input\":\"exec\"}");
    const auto at = text.rfind("}]"); REQUIRE(at != std::string::npos);
    text.insert(at + 1, ",{\"name\":\"native_ready_while_held\",\"node\":\"ready_held\"}");
    return text;
}
}

TEST_CASE("v2 routes ammo and cooldown gates before native acceptance", "[graph-v2-draft]") {
    for (bool ready : {false, true}) for (int ammo : {0, 5}) {
        auto p = compile(graph()); Host h; h.ammo = ammo; h.ready = ready; auto i = bind(p, h);
        auto result = pressAndIdle(i.get(), 1, 41, 9);
        REQUIRE(h.starts == (ready && ammo >= 2 ? 1 : 0));
        REQUIRE(h.shots == 0); REQUIRE(h.ammo == ammo);
        REQUIRE(result == (ready && ammo >= 2 ? WG_V2_RUNNING : WG_V2_BLOCKED));
    }
}
TEST_CASE("v2 authored boolean state persists per hand and routes later activation",
        "[graph-v2-draft]") {
    auto source = stateGraph();
    const auto nodes_end = source.find("],\"edges\""); REQUIRE(nodes_end != std::string::npos);
    source.insert(nodes_end, R"(,{"id":"disarm","kind":"state.bool_set","params":{"key":"armed","value":false}})");
    const auto edges_end = source.find("],\"exports\""); REQUIRE(edges_end != std::string::npos);
    source.insert(edges_end, R"(,{"from":"shot","output":"completed","to":"disarm","input":"exec"})");
    auto p = compile(source); Host left, right;
    auto a = bind(p, left), b = bind(p, right);
    REQUIRE(pressAndIdle(a.get(), 1, 41, 9) == WG_V2_BLOCKED);
    REQUIRE(left.starts == 0); REQUIRE(left.shots == 0);
    REQUIRE(pressAndIdle(a.get(), 2, 41, 9) == WG_V2_RUNNING);
    REQUIRE(left.starts == 1);
    REQUIRE(pressAndIdle(b.get(), 1, 41, 9) == WG_V2_BLOCKED);
    REQUIRE(right.starts == 0); // A's state must not arm another hand.
    for (uint64_t step = 1; step <= 4; ++step) wgV2Advance(a.get(), step, 1.0, 41, 9);
    REQUIRE(left.shots == 1); REQUIRE(right.shots == 0);
    REQUIRE(pressAndIdle(a.get(), 3, 41, 9) == WG_V2_BLOCKED); // Completed shot authored the clear.
    auto replacement = bind(p, left);
    REQUIRE(pressAndIdle(replacement.get(), 1, 41, 9) == WG_V2_BLOCKED);
}
TEST_CASE("v2 authored state connections and strict boolean source change execution",
        "[graph-v2-draft]") {
    auto before = compile(stateGraph()); auto edited = compile(stateGraph("false"));
    REQUIRE(std::string(wgV2ProgramSourceHash(before.get())) != wgV2ProgramSourceHash(edited.get()));
    Host a, b, c; auto first = bind(before, a), changed = bind(edited, b);
    auto same_activation = compile(stateGraph("true", true)); auto connected = bind(same_activation, c);
    REQUIRE(pressAndIdle(first.get(), 1, 41, 9) == WG_V2_BLOCKED);
    REQUIRE(pressAndIdle(changed.get(), 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(pressAndIdle(connected.get(), 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(a.starts == 0); REQUIRE(b.starts == 1); REQUIRE(c.starts == 1);
    std::string wrong_key = stateGraph();
    const auto key = wrong_key.find("\"key\":\"armed\""); REQUIRE(key != std::string::npos);
    wrong_key.replace(key, std::strlen("\"key\":\"armed\""), "\"key\":\"missing\"");
    for (const auto &invalid : {stateGraph("\"true\""), wrong_key}) {
        char error[512]{};
        Program rejected(wgV2Compile(invalid.data(), invalid.size(), 9, deps, error, sizeof(error)));
        REQUIRE_FALSE(rejected);
        REQUIRE(std::string(error).find(invalid == wrong_key ? "no authored setter" : "expected boolean")
            != std::string::npos);
    }
}
TEST_CASE("v2 native action starts once then commits and recovers asynchronously", "[graph-v2-draft]") {
    auto p = compile(graph()); Host h; auto i = bind(p, h); p.reset(); // Instance owns immutable program.
    REQUIRE(pressAndIdle(i.get(), 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(pressAndIdle(i.get(), 1, 41, 9) == WG_V2_IGNORED);
    REQUIRE(h.starts == 1); REQUIRE(h.shots == 0);
    REQUIRE(wgV2Advance(i.get(), 1, 1.0, 41, 9) == WG_V2_RUNNING); // Animation gate.
    REQUIRE(h.shots == 0);
    REQUIRE(wgV2Advance(i.get(), 2, 1.0, 41, 9) == WG_V2_RUNNING); // Commit.
    REQUIRE(h.shots == 1); REQUIRE(h.ammo == 4); REQUIRE(h.damage == Approx(3));
    REQUIRE(wgV2Advance(i.get(), 2, 1.0, 41, 9) == WG_V2_IGNORED);
    REQUIRE(wgV2Advance(i.get(), 3, 0.0, 41, 9) == WG_V2_IGNORED);
    REQUIRE(wgV2Advance(i.get(), 3, 1.0, 41, 9) == WG_V2_RUNNING); // Recovery.
    REQUIRE(wgV2Advance(i.get(), 4, 1.0, 41, 9) == WG_V2_COMPLETED);
    REQUIRE(h.starts == 1); REQUIRE(h.cleanups == 1); REQUIRE(h.cancels == 0);
    i.reset(); REQUIRE(h.cleanups == 1);
}
TEST_CASE("v2 edited blocked edge reaches the authored alternative action", "[graph-v2-draft]") {
    auto p = compile(graph(',' + shot("alternate", 7),
        R"(,{"from":"ammo","output":"blocked","to":"alternate","input":"exec"})"));
    Host h; h.ammo = 1; auto i = bind(p, h);
    pressAndIdle(i.get(), 1, 41, 9);
    REQUIRE(h.selected == std::vector<std::string>{"alternate"});
    wgV2Advance(i.get(), 1, 1.0, 41, 9); wgV2Advance(i.get(), 2, 1.0, 41, 9);
    REQUIRE(h.damage == Approx(7)); REQUIRE(h.shots == 1); REQUIRE(h.ammo == 0);
}
TEST_CASE("v2 waiting modules require their live ticket notification", "[graph-v2-draft]") {
    auto p = compile(graph()); Host h; h.wait_start = true; auto i = bind(p, h);
    REQUIRE(pressAndIdle(i.get(), 1, 41, 9) == WG_V2_WAITING);
    const auto ticket = wgV2ActiveTicket(i.get(), "shot"); REQUIRE(ticket != 0);
    wgV2Advance(i.get(), 1, 1.0, 41, 9); REQUIRE(h.ticks == 0);
    REQUIRE(wgV2Wake(i.get(), ticket + 1, 7, 41, 9) == WG_V2_IGNORED);
    REQUIRE(wgV2Wake(i.get(), ticket, 3, 41, 9) == WG_V2_WAITING);
    REQUIRE(wgV2Wake(i.get(), ticket, 7, 41, 9) == WG_V2_RUNNING);
    wgV2Advance(i.get(), 2, 1.0, 41, 9); REQUIRE(h.ticks == 1); REQUIRE(h.shots == 0);
}
TEST_CASE("v2 cancellation retires native continuations exactly once", "[graph-v2-draft]") {
    for (bool changeSource : {false, true}) {
        auto p = compile(graph()); Host h; auto i = bind(p, h); pressAndIdle(i.get(), 1, 41, 9);
        REQUIRE(wgV2Advance(i.get(), 1, 1.0, changeSource ? 41 : 42, changeSource ? 10 : 9) == WG_V2_CANCELLED);
        REQUIRE(h.cancels == 1); REQUIRE(h.cleanups == 1); REQUIRE(h.shots == 0);
        wgV2Cancel(i.get(), "again"); i.reset(); REQUIRE(h.cleanups == 1);
    }
}
TEST_CASE("v2 hands retain independent native state on one shared program", "[graph-v2-draft]") {
    auto p = compile(graph()); Host left, right; auto a = bind(p, left), b = bind(p, right);
    pressAndIdle(a.get(), 1, 41, 9); pressAndIdle(b.get(), 1, 41, 9);
    wgV2Advance(a.get(), 1, 1.0, 41, 9); wgV2Advance(a.get(), 2, 1.0, 41, 9);
    REQUIRE(left.shots == 1); REQUIRE(right.shots == 0); REQUIRE(right.ammo == 5);
    wgV2Cancel(a.get(), "left unequipped"); REQUIRE(right.cancels == 0);
}
TEST_CASE("v2 busy presses coalesce and release cancels only the pending press", "[graph-v2-draft]") {
    for (bool released : {false, true}) {
        auto p = compile(graph()); Host h; auto i = bind(p, h); pressAndIdle(i.get(), 1, 41, 9);
        REQUIRE(pressAndIdle(i.get(), 2, 41, 9) == WG_V2_QUEUED);
        REQUIRE(pressAndIdle(i.get(), 3, 41, 9) == WG_V2_QUEUED);
        if (released) wgV2ReleasePendingPress(i.get());
        for (uint64_t step = 1; step <= 4; ++step) wgV2Advance(i.get(), step, 1.0, 41, 9);
        REQUIRE(h.shots == 1); REQUIRE(h.starts == 1); // Completion cannot skip native idle housekeeping.
        wgV2NativeIdle(i.get(), 4, !released, 41, 9);
        REQUIRE(h.starts == (released ? 1 : 2));
    }
}
TEST_CASE("v2 failed native acceptance cleans up without an ammunition debit", "[graph-v2-draft]") {
    auto p = compile(graph()); Host h; h.fail_start = true; auto i = bind(p, h);
    REQUIRE(pressAndIdle(i.get(), 1, 41, 9) == WG_V2_FAILED);
    REQUIRE(h.shots == 0); REQUIRE(h.ammo == 5); REQUIRE(h.cleanups == 1);
    i.reset(); REQUIRE(h.cleanups == 1); REQUIRE(h.cancels == 0);
}
TEST_CASE("v2 graph topology is dynamically sized beyond old fixed IR", "[graph-v2-draft]") {
    std::string nodes, edges;
    for (int n = 0; n < 96; ++n) {
        nodes += ",{\"id\":\"gate" + std::to_string(n) + "\",\"kind\":\"gate.cooldown_ready\"}";
        edges += ",{\"from\":\"" + (n ? "gate" + std::to_string(n - 1) : "shot") +
            "\",\"output\":\"" + (n ? "pass" : "completed") + "\",\"to\":\"gate" + std::to_string(n) + "\",\"input\":\"exec\"}";
    }
    auto p = compile(graph(nodes, edges)); REQUIRE(wgV2ProgramNodeCount(p.get()) == 100);
    Host h; auto i = bind(p, h); pressAndIdle(i.get(), 1, 41, 9);
    for (uint64_t step = 1; step <= 4; ++step) wgV2Advance(i.get(), step, 1.0, 41, 9);
    REQUIRE(h.trace.back() == "1:gate95.pass"); REQUIRE(h.shots == 1);
}
TEST_CASE("v2 compiler reports decoded IDs ports lexical and module mismatches", "[graph-v2-draft]") {
    const std::string original = graph();
    const std::vector<std::pair<std::string, std::string>> changes = {
        {"\"module_version\":1", "\"module_version\":1.0"},
        {"\"og.fire.hitscan\"", "\"og.fire.unknown\""},
        {"\"output\":\"pass\"", "\"output\":\"typo\""},
        {"\"node\":\"press\"", "\"node\":\"shot\""},
        {"\"required\":2", "\"required\":2oops"},
        {"\"flags\":0", "\"flags\":2"},
        {"\"damage\":3", "\"damage\":3,\"da\\u006dage\":4"},
    };
    for (const auto &change : changes) {
        auto invalid = original; const auto pos = invalid.find(change.first); REQUIRE(pos != std::string::npos);
        invalid.replace(pos, change.first.size(), change.second);
        char error[512]{}; Program p(wgV2Compile(invalid.data(), invalid.size(), 9, deps, error, sizeof(error)));
        INFO(error); REQUIRE(p == nullptr); REQUIRE(error[0] != 0);
    }
}

TEST_CASE("v2 native cancellation routes its authored port and cleans up", "[graph-v2-draft]") {
    auto p = compile(graph(",{\"id\":\"after_cancel\",\"kind\":\"gate.cooldown_ready\"}",
        R"(,{"from":"shot","output":"cancelled","to":"after_cancel","input":"exec"})"));
    Host h; auto ops = callbacks();
    ops.start = [](void *v, const wg_v2_action *, uint64_t, void **state, char *, size_t) {
        ++static_cast<Host *>(v)->starts; *state = nullptr; return WG_V2_CANCELLED;
    };
    char error[512]{}; Instance i(wgV2Bind(p.get(), &ops, &h, 41, error, sizeof(error)));
    REQUIRE(i != nullptr); REQUIRE(pressAndIdle(i.get(), 1, 41, 9) == WG_V2_CANCELLED);
    REQUIRE(h.trace.back() == "1:after_cancel.pass");
    REQUIRE(h.shots == 0); REQUIRE(h.cleanups == 1); REQUIRE(h.cancels == 0);
}

TEST_CASE("v2 dependency preparation fails before any action is accepted", "[graph-v2-draft]") {
    auto p = compile(graph()); Host h; auto ops = callbacks();
    ops.prepare = [](void *, const wg_v2_action *, char *, size_t) { return 0; };
    char error[512]{}; Instance i(wgV2Bind(p.get(), &ops, &h, 41, error, sizeof(error)));
    REQUIRE(i == nullptr); REQUIRE(h.starts == 0); REQUIRE(h.shots == 0); REQUIRE(h.cleanups == 0);
    REQUIRE(std::string(error).find("shot: native dependency preparation failed") != std::string::npos);
}

TEST_CASE("v2 decoded node identity and exact source bytes survive compilation", "[graph-v2-draft]") {
    auto source = graph(); const auto at = source.find("\"id\":\"shot\""); REQUIRE(at != std::string::npos);
    source.replace(at, std::string("\"id\":\"shot\"").size(), "\"id\":\"sh\\u006ft\"");
    auto original = compile(graph()), escaped = compile(source);
    REQUIRE(std::string(wgV2ProgramSourceHash(original.get())) != wgV2ProgramSourceHash(escaped.get()));
    REQUIRE(std::string(wgV2ProgramDependencyHash(escaped.get())) == deps);
    source.assign("replaced caller storage");
    Host h; auto i = bind(escaped, h); pressAndIdle(i.get(), 1, 41, 9);
    REQUIRE(h.selected == std::vector<std::string>{"shot"});
}

TEST_CASE("v2 physical input only queues and native idle owns admission", "[graph-v2-draft]") {
    auto p = compile(heldGraph()); Host h; auto i = bind(p, h);
    REQUIRE(wgV2Press(i.get(), 1, 41, 9) == WG_V2_QUEUED);
    REQUIRE(h.starts == 0); REQUIRE(h.trace.empty());
    wgV2Advance(i.get(), 1, 1.0, 41, 9);
    REQUIRE(h.starts == 0); REQUIRE(h.shots == 0);
    REQUIRE(wgV2NativeIdle(i.get(), 1, 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(h.trace.front() == "1:press.exec"); REQUIRE(h.starts == 1);
    REQUIRE(wgV2NativeIdle(i.get(), 1, 1, 41, 9) == WG_V2_IGNORED);
    REQUIRE(h.starts == 1); REQUIRE(h.shots == 0);
}

TEST_CASE("v2 held repetition starts only when native recovery returns to idle", "[graph-v2-draft]") {
    auto p = compile(heldGraph()); Host h; auto i = bind(p, h);
    pressAndIdle(i.get(), 1, 41, 9);
    for (uint64_t step = 1; step <= 4; ++step) wgV2Advance(i.get(), step, 1.0, 41, 9);
    REQUIRE(h.shots == 1); REQUIRE(h.starts == 1); REQUIRE(h.cleanups == 1);
    REQUIRE_FALSE(wgV2Busy(i.get()));
    REQUIRE(wgV2NativeIdle(i.get(), 2, 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(h.starts == 2); REQUIRE(h.shots == 1); // A new admission is not an immediate shot.
    REQUIRE(std::find(h.trace.begin(), h.trace.end(), "2:ready_held.exec") != h.trace.end());
    for (uint64_t step = 5; step <= 8; ++step) wgV2Advance(i.get(), step, 1.0, 41, 9);
    REQUIRE(h.shots == 2); REQUIRE(h.ammo == 3); REQUIRE(h.cleanups == 2);
}

TEST_CASE("v2 blocked physical branch cannot fall through to held event in same idle visit", "[graph-v2-draft]") {
    auto p = compile(heldGraph("shot")); Host h; h.ammo = 1; auto i = bind(p, h);
    wgV2Press(i.get(), 1, 41, 9);
    REQUIRE(wgV2NativeIdle(i.get(), 1, 1, 41, 9) == WG_V2_BLOCKED);
    REQUIRE(h.starts == 0); REQUIRE(h.shots == 0);
    REQUIRE(wgV2NativeIdle(i.get(), 1, 1, 41, 9) == WG_V2_IGNORED);
    REQUIRE(h.starts == 0);
    // A later real native idle admission is a new held event with its own authored route.
    REQUIRE(wgV2NativeIdle(i.get(), 2, 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(h.starts == 1); REQUIRE(h.trace[0] == "1:press.exec");
    REQUIRE(std::find(h.trace.begin(), h.trace.end(), "2:ready_held.exec") != h.trace.end());
}

TEST_CASE("v2 recovery repress waits for idle and takes priority over held readiness", "[graph-v2-draft]") {
    auto p = compile(heldGraph()); Host h; auto i = bind(p, h); pressAndIdle(i.get(), 1, 41, 9);
    wgV2Advance(i.get(), 1, 1.0, 41, 9); wgV2Advance(i.get(), 2, 1.0, 41, 9);
    wgV2ReleasePendingPress(i.get());
    REQUIRE(wgV2Press(i.get(), 2, 41, 9) == WG_V2_QUEUED);
    REQUIRE(wgV2Press(i.get(), 3, 41, 9) == WG_V2_QUEUED);
    wgV2Advance(i.get(), 3, 1.0, 41, 9); wgV2Advance(i.get(), 4, 1.0, 41, 9);
    REQUIRE(h.starts == 1); REQUIRE(h.cleanups == 1); // Native outer state bookkeeping still has to run.
    REQUIRE(wgV2NativeIdle(i.get(), 2, 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(h.starts == 2);
    REQUIRE(std::find(h.trace.begin(), h.trace.end(), "2:press.exec") != h.trace.end());
    REQUIRE(std::find(h.trace.begin(), h.trace.end(), "2:ready_held.exec") == h.trace.end());
    REQUIRE(wgV2NativeIdle(i.get(), 2, 1, 41, 9) == WG_V2_IGNORED);
}

TEST_CASE("v2 release during native attack preserves commit and prevents held restart", "[graph-v2-draft]") {
    auto p = compile(heldGraph()); Host h; auto i = bind(p, h); pressAndIdle(i.get(), 1, 41, 9);
    wgV2Press(i.get(), 2, 41, 9); wgV2ReleasePendingPress(i.get());
    for (uint64_t step = 1; step <= 4; ++step) wgV2Advance(i.get(), step, 1.0, 41, 9);
    REQUIRE(h.shots == 1); REQUIRE(h.cleanups == 1); REQUIRE(h.cancels == 0);
    REQUIRE(wgV2NativeIdle(i.get(), 2, 0, 41, 9) == WG_V2_IGNORED);
    REQUIRE(h.starts == 1);
}

TEST_CASE("v2 absent held export stays press only and fractional native time is not pause", "[graph-v2-draft]") {
    auto p = compile(graph()); Host h; auto i = bind(p, h); pressAndIdle(i.get(), 1, 41, 9);
    REQUIRE(wgV2Advance(i.get(), 1, 0.0, 41, 9) == WG_V2_IGNORED); REQUIRE(h.ticks == 0);
    REQUIRE(wgV2Advance(i.get(), 1, 0.25, 41, 9) == WG_V2_RUNNING); REQUIRE(h.ticks == 1);
    for (uint64_t step = 2; step <= 4; ++step) wgV2Advance(i.get(), step, 0.25, 41, 9);
    REQUIRE(h.shots == 1); REQUIRE(wgV2NativeIdle(i.get(), 2, 1, 41, 9) == WG_V2_IGNORED);
    REQUIRE(h.starts == 1); // Omission is an intentional authoring policy, not a native fallback.
}

TEST_CASE("v2 held readiness exports require their exact event and executable port", "[graph-v2-draft]") {
    for (const auto &change : std::vector<std::pair<std::string, std::string>>{
            {"\"node\":\"ready_held\"", "\"node\":\"press\""},
            {"\"from\":\"ready_held\",\"output\":\"exec\"", "\"from\":\"ready_held\",\"output\":\"blocked\""}}) {
        auto source = heldGraph(); const auto at = source.find(change.first); REQUIRE(at != std::string::npos);
        source.replace(at, change.first.size(), change.second); char error[512]{};
        Program p(wgV2Compile(source.data(), source.size(), 9, deps, error, sizeof(error)));
        INFO(error); REQUIRE(p == nullptr); REQUIRE(error[0] != 0);
    }
}

TEST_CASE("v2 native idle can resume an already held trigger without fabricating a press", "[graph-v2-draft]") {
    auto p = compile(heldGraph()); Host h; auto i = bind(p, h);
    REQUIRE(wgV2NativeIdle(i.get(), 1, 1, 41, 9) == WG_V2_RUNNING);
    REQUIRE(h.starts == 1); REQUIRE(h.shots == 0);
    REQUIRE(h.trace.front() == "1:ready_held.exec");
    REQUIRE(std::find(h.trace.begin(), h.trace.end(), "1:press.exec") == h.trace.end());
}
