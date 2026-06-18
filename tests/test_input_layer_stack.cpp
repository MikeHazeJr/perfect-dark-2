/*
 * test_input_layer_stack.cpp -- Cohort 2 invariants for the typed
 * Input Layer Stack.
 *
 * Locks down push / pop / abort cascade / exclusivity / handle validity
 * before Cohort 3 (Scene Manager) and Cohort 4 (cutscene flash fix)
 * start wiring real callsites onto this scaffolding.
 *
 * @SYNC port/src/inputlayer.c
 * @SYNC context/designs/input-universality-and-transitions-2026-04-27.md SC
 *
 * Logging channel reserved for runtime diagnostics: INPUT.LAYER.*
 */

#include "catch.hpp"
#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "inputlayer_pure.h"
}

namespace {

void resetWorld()
{
    ilpShutdown();
    ilpInstrumentReset(nullptr);
}

std::string readTextFile(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} /* namespace */

TEST_CASE("inputlayer: empty stack invariants", "[inputlayer][stack]")
{
    resetWorld();
    REQUIRE(ilpDepth()   == 0);
    REQUIRE(ilpTop()     == nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_TYPE_COUNT);
    REQUIRE(ilpHas(ILP_LAYER_GAMEPLAY) == 0);
    REQUIRE(ilpHas(ILP_LAYER_BOOT)     == 0);
}

TEST_CASE("inputlayer: init pushes BOOT to depth 1", "[inputlayer][lifecycle]")
{
    resetWorld();
    ilpInit();
    REQUIRE(ilpDepth()   == 1);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
    REQUIRE(ilpHas(ILP_LAYER_BOOT) == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_BOOT)->push_count == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_BOOT)->pop_count  == 0);
}

TEST_CASE("inputlayer: init is idempotent (re-init aborts prior stack)", "[inputlayer][lifecycle]")
{
    resetWorld();
    ilpInit();
    REQUIRE(ilpPush(&g_IlpGameplay, nullptr) != nullptr);
    REQUIRE(ilpDepth() == 2);

    ilpInstrumentReset(nullptr);
    ilpInit();
    REQUIRE(ilpDepth()   == 1);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
    /* Re-init aborts the prior stack -- both BOOT and GAMEPLAY abort, then
     * a fresh BOOT push runs (push_count == 1 for BOOT). */
    REQUIRE(ilpInstrumentGet(ILP_LAYER_GAMEPLAY)->abort_count == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_BOOT)->abort_count     == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_BOOT)->push_count      == 1);
}

TEST_CASE("inputlayer: push and pop advance / retract the top", "[inputlayer][stack]")
{
    resetWorld();
    ilpInit();

    IlpLayerHandle *gp = ilpPush(&g_IlpGameplay, nullptr);
    REQUIRE(gp != nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);

    IlpLayerHandle *menu = ilpPush(&g_IlpMenu, nullptr);
    REQUIRE(menu != nullptr);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);
    REQUIRE(ilpDepth()   == 3);

    /* Menu, Gameplay, and Boot all in stack. */
    REQUIRE(ilpHas(ILP_LAYER_MENU)     == 1);
    REQUIRE(ilpHas(ILP_LAYER_GAMEPLAY) == 1);
    REQUIRE(ilpHas(ILP_LAYER_BOOT)     == 1);

    REQUIRE(ilpPop(menu, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    REQUIRE(ilpHas(ILP_LAYER_MENU) == 0);

    REQUIRE(ilpPop(gp, nullptr) == 0);
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
    REQUIRE(ilpDepth()   == 1);
}

TEST_CASE("inputlayer: pop with mismatched handle is rejected", "[inputlayer][safety]")
{
    resetWorld();
    ilpInit();
    IlpLayerHandle *gp   = ilpPush(&g_IlpGameplay, nullptr);
    IlpLayerHandle *menu = ilpPush(&g_IlpMenu,     nullptr);

    /* Try to pop the gameplay layer without first popping the menu on top. */
    REQUIRE(ilpPop(gp, nullptr) == -2);
    /* Stack untouched. */
    REQUIRE(ilpDepth()   == 3);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);

    /* Pop in the right order. */
    REQUIRE(ilpPop(menu, nullptr) == 0);
    REQUIRE(ilpPop(gp,   nullptr) == 0);
}

TEST_CASE("inputlayer: pop with NULL handle is rejected", "[inputlayer][safety]")
{
    resetWorld();
    ilpInit();
    REQUIRE(ilpPop(nullptr, nullptr) == -1);
    REQUIRE(ilpDepth() == 1);
}

TEST_CASE("inputlayer: handle invalidates on pop (no use-after-pop)", "[inputlayer][safety]")
{
    resetWorld();
    ilpInit();
    IlpLayerHandle *gp = ilpPush(&g_IlpGameplay, nullptr);
    REQUIRE(ilpHandleDef(gp) == &g_IlpGameplay);

    REQUIRE(ilpPop(gp, nullptr) == 0);
    /* Same pointer: now invalid because generation was bumped to 0. */
    REQUIRE(ilpHandleDef(gp) == nullptr);
    /* A second pop with the same stale handle errors instead of corrupting. */
    REQUIRE(ilpPop(gp, nullptr) == -1);
}

TEST_CASE("inputlayer: abort cascade unwinds N layers in LIFO order", "[inputlayer][abort]")
{
    resetWorld();
    ilpInit();
    ilpPush(&g_IlpGameplay, nullptr);
    ilpPush(&g_IlpCutscene, nullptr);
    ilpPush(&g_IlpMenu,     nullptr);
    REQUIRE(ilpDepth() == 4);

    ilpInstrumentReset(nullptr);
    ilpAbort(2, /* reason */ 42);

    /* Top two (Menu, Cutscene) aborted, Gameplay and Boot remain. */
    REQUIRE(ilpDepth()   == 2);
    REQUIRE(ilpTopType() == ILP_LAYER_GAMEPLAY);
    REQUIRE(ilpHas(ILP_LAYER_MENU)     == 0);
    REQUIRE(ilpHas(ILP_LAYER_CUTSCENE) == 0);
    REQUIRE(ilpHas(ILP_LAYER_GAMEPLAY) == 1);
    REQUIRE(ilpHas(ILP_LAYER_BOOT)     == 1);

    /* Both abort-target layers received on_abort with reason 42. */
    REQUIRE(ilpInstrumentGet(ILP_LAYER_MENU)->abort_count        == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_CUTSCENE)->abort_count    == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_MENU)->last_abort_reason  == 42);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_CUTSCENE)->last_abort_reason == 42);
    /* Untouched layers did not receive abort. */
    REQUIRE(ilpInstrumentGet(ILP_LAYER_GAMEPLAY)->abort_count == 0);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_BOOT)->abort_count     == 0);
}

TEST_CASE("inputlayer: abort with from_top > depth clamps to depth", "[inputlayer][abort]")
{
    resetWorld();
    ilpInit();
    ilpPush(&g_IlpGameplay, nullptr);

    ilpAbort(99, 0);
    REQUIRE(ilpDepth() == 0);
    REQUIRE(ilpTop()   == nullptr);
}

TEST_CASE("inputlayer: abort with from_top <= 0 is no-op", "[inputlayer][abort]")
{
    resetWorld();
    ilpInit();
    ilpPush(&g_IlpGameplay, nullptr);

    ilpAbort(0,  0);
    REQUIRE(ilpDepth() == 2);
    ilpAbort(-5, 0);
    REQUIRE(ilpDepth() == 2);
}

TEST_CASE("inputlayer: capacity overflow returns NULL without mutating stack", "[inputlayer][safety]")
{
    resetWorld();
    ilpInit();
    /* Fill to capacity (16), then attempt one more push. */
    while (ilpDepth() < ILP_MAX_DEPTH) {
        REQUIRE(ilpPush(&g_IlpMenu, nullptr) != nullptr);
    }
    REQUIRE(ilpDepth() == ILP_MAX_DEPTH);

    REQUIRE(ilpPush(&g_IlpMenu, nullptr) == nullptr);
    REQUIRE(ilpDepth() == ILP_MAX_DEPTH);
}

TEST_CASE("inputlayer: NULL def push is rejected", "[inputlayer][safety]")
{
    resetWorld();
    ilpInit();
    REQUIRE(ilpPush(nullptr, nullptr) == nullptr);
    REQUIRE(ilpDepth() == 1);
}

TEST_CASE("inputlayer: same-type stacking is allowed (multiple menus)", "[inputlayer][stack]")
{
    /* Per design SC.3 / Section F: the menu graph permits push-on-push of
     * the same LayerType (e.g., menu opens sub-menu opens confirm-modal).
     * The Layer Stack itself does not enforce type uniqueness; the menu
     * pool (port/src/menupool.c) handles that policy at a higher level. */
    resetWorld();
    ilpInit();
    REQUIRE(ilpPush(&g_IlpMenu, nullptr) != nullptr);
    REQUIRE(ilpPush(&g_IlpMenu, nullptr) != nullptr);
    REQUIRE(ilpPush(&g_IlpMenu, nullptr) != nullptr);
    REQUIRE(ilpDepth() == 4);
    REQUIRE(ilpTopType() == ILP_LAYER_MENU);
    REQUIRE(ilpHas(ILP_LAYER_MENU) == 1);
}

TEST_CASE("inputlayer: payload is threaded into on_push", "[inputlayer][callback]")
{
    static int s_observed = 0;
    static int s_argpayload = 0;
    struct local {
        static int onPush(void *p) {
            s_observed   = 1;
            s_argpayload = (int)(intptr_t)p;
            return 0;
        }
    };
    IlpLayerDef def = { ILP_LAYER_MENU, "TestMenu", local::onPush, nullptr, nullptr };

    resetWorld();
    ilpInit();
    s_observed = 0;
    s_argpayload = 0;
    IlpLayerHandle *h = ilpPush(&def, (void *)(intptr_t)0xc0ffee);
    REQUIRE(h != nullptr);
    REQUIRE(s_observed == 1);
    REQUIRE(s_argpayload == 0xc0ffee);
    REQUIRE(ilpPop(h, nullptr) == 0);
}

TEST_CASE("inputlayer: top type returns LAYER_TYPE_COUNT when stack empty", "[inputlayer][query]")
{
    resetWorld();
    REQUIRE(ilpTopType() == ILP_LAYER_TYPE_COUNT);
    ilpInit();
    REQUIRE(ilpTopType() == ILP_LAYER_BOOT);
    ilpShutdown();
    REQUIRE(ilpTopType() == ILP_LAYER_TYPE_COUNT);
}

TEST_CASE("inputlayer: shutdown aborts every remaining layer", "[inputlayer][lifecycle]")
{
    resetWorld();
    ilpInit();
    ilpPush(&g_IlpGameplay, nullptr);
    ilpPush(&g_IlpCutscene, nullptr);
    ilpPush(&g_IlpMenu,     nullptr);

    ilpInstrumentReset(nullptr);
    ilpShutdown();

    REQUIRE(ilpDepth() == 0);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_MENU)->abort_count     == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_CUTSCENE)->abort_count == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_GAMEPLAY)->abort_count == 1);
    REQUIRE(ilpInstrumentGet(ILP_LAYER_BOOT)->abort_count     == 1);
}

TEST_CASE("inputctx bridge: non-gameplay input ownership publishes LAYER_MENU", "[inputctx][inputlayer][static]")
{
    const std::string inputctx = readTextFile("port/src/inputctx.c");

    REQUIRE_FALSE(inputctx.empty());
    REQUIRE(inputctx.find("#include \"inputlayer.h\"") != std::string::npos);
    REQUIRE(inputctx.find("static LayerHandle *s_MenuLayerHandle") != std::string::npos);
    REQUIRE(inputctx.find("static void inputctxSyncMenuLayerBridge(void)") != std::string::npos);
    REQUIRE(inputctx.find("InputContext *top = inputCtxGetTop()") != std::string::npos);
    REQUIRE(inputctx.find("top && top != &g_CtxGameplay") != std::string::npos);
    REQUIRE(inputctx.find("inputLayerPush(&g_LayerMenu, NULL)") != std::string::npos);
    REQUIRE(inputctx.find("inputLayerPop(s_MenuLayerHandle, NULL)") != std::string::npos);
    REQUIRE(inputctx.find("inputLayerAbort(from_top, /* inputctx nested menu close */ 0)") != std::string::npos);

    size_t calls = 0;
    size_t pos = 0;
    while ((pos = inputctx.find("inputctxSyncMenuLayerBridge();", pos)) != std::string::npos) {
        calls++;
        pos += strlen("inputctxSyncMenuLayerBridge();");
    }
    REQUIRE(calls >= 5);
}

TEST_CASE("actionmap: query reads honor layer-declared gameplay aperture", "[inputlayer][actionmap][static]")
{
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");

    REQUIRE(actionmap.find("#include \"inputlayer.h\"") != std::string::npos);
    REQUIRE(actionmap.find("static s32 actionLayerAllows(InputAction a)") != std::string::npos);
    REQUIRE(actionmap.find("inputLayerTop()") != std::string::npos);
    REQUIRE(actionmap.find("inputLayerHandleDef(top)") != std::string::npos);
    REQUIRE(actionmap.find("def->action_set && def->action_set_count > 0 && actionIsGameplayOnly(a)") != std::string::npos);
    REQUIRE(actionmap.find("def->action_set[i] == a") != std::string::npos);
    REQUIRE(actionmap.find("gameplayInputSuppressed() && actionIsGameplayOnly(a)") != std::string::npos);

    size_t calls = 0;
    size_t pos = 0;
    while ((pos = actionmap.find("actionLayerAllows(action)", pos)) != std::string::npos) {
        calls++;
        pos += strlen("actionLayerAllows(action)");
    }
    REQUIRE(calls >= 8);
}

TEST_CASE("actionmap: dispatch writes honor layer-declared gameplay aperture", "[inputlayer][actionmap][static]")
{
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");

    REQUIRE(actionmap.find("static s32 actionLayerAllows(InputAction a);") != std::string::npos);

    const size_t gate = actionmap.find("if (!actionLayerAllows((InputAction)best_a))");
    REQUIRE(gate != std::string::npos);

    const size_t stateWrite = actionmap.find("ActionState *st = &s_State[player][best_a]", gate);
    REQUIRE(stateWrite != std::string::npos);
    REQUIRE(gate < stateWrite);
}

TEST_CASE("actionmap: analog axes honor layer-declared gameplay aperture", "[inputlayer][actionmap][static]")
{
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");

    REQUIRE(actionmap.find("static void actionmapZeroGameplayAxes(s32 player, s32 zero_move, s32 zero_aim)") != std::string::npos);
    REQUIRE(actionmap.find("actionLayerAllows(ACTION_AXIS_MOVE_X)") != std::string::npos);
    REQUIRE(actionmap.find("actionLayerAllows(ACTION_AXIS_MOVE_Y)") != std::string::npos);
    REQUIRE(actionmap.find("actionLayerAllows(ACTION_AXIS_AIM_X)") != std::string::npos);
    REQUIRE(actionmap.find("actionLayerAllows(ACTION_AXIS_AIM_Y)") != std::string::npos);
    REQUIRE(actionmap.find("if (!moveAxesAllowed || !aimAxesAllowed)") != std::string::npos);
    REQUIRE(actionmap.find("actionmapZeroGameplayAxes(0, !moveAxesAllowed, !aimAxesAllowed)") != std::string::npos);
    REQUIRE(actionmap.find("if (!moveAxesAllowed)") != std::string::npos);
}

TEST_CASE("actionmap: hold bookkeeping honors layer-declared gameplay aperture", "[inputlayer][actionmap][static]")
{
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");

    const size_t consume = actionmap.find("void actionConsumeHold(s32 player, InputAction action)");
    REQUIRE(consume != std::string::npos);
    const size_t consumeGate = actionmap.find("if (!actionLayerAllows(action)) return;", consume);
    REQUIRE(consumeGate != std::string::npos);
    const size_t consumeWrite = actionmap.find("st->hold_consumed = 1;", consume);
    REQUIRE(consumeWrite != std::string::npos);
    REQUIRE(consumeGate < consumeWrite);

    const size_t read = actionmap.find("s32 actionHoldConsumed(s32 player, InputAction action)");
    REQUIRE(read != std::string::npos);
    const size_t readGate = actionmap.find("if (!actionLayerAllows(action)) return 0;", read);
    REQUIRE(readGate != std::string::npos);
    const size_t readState = actionmap.find("return s_State[player][action].hold_consumed;", read);
    REQUIRE(readState != std::string::npos);
    REQUIRE(readGate < readState);
}

TEST_CASE("inputReadController mirrors action-map axes", "[inputlayer][input][static]")
{
    const std::string input = readTextFile("port/src/input.c");

    const size_t start = input.find("s32 inputReadController(s32 idx, OSContPad *npad)");
    REQUIRE(start != std::string::npos);
    const size_t end = input.find("/* M0.2 Phase D: inputKeyBind/inputKeyGetBinds removed", start);
    REQUIRE(end != std::string::npos);
    const std::string body = input.substr(start, end - start);

    REQUIRE(input.find("static s8 inputAxisValueToStick(f32 value)") != std::string::npos);
    REQUIRE(body.find("actionValue(idx, ACTION_AXIS_MOVE_X)") != std::string::npos);
    REQUIRE(body.find("actionValue(idx, ACTION_AXIS_MOVE_Y)") != std::string::npos);
    REQUIRE(body.find("actionValue(idx, ACTION_AXIS_AIM_X)") != std::string::npos);
    REQUIRE(body.find("actionValue(idx, ACTION_AXIS_AIM_Y)") != std::string::npos);
    REQUIRE(body.find("SDL_GameControllerGetAxis") == std::string::npos);
}
