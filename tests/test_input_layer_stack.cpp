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
    ilpPush(&def, (void *)(intptr_t)0xc0ffee);
    REQUIRE(s_observed == 1);
    REQUIRE(s_argpayload == 0xc0ffee);
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
