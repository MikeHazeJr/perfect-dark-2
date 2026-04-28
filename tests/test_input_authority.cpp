/*
 * test_input_authority.cpp -- IMC stack invariants (cohort 2).
 *
 * Drives the inputctx_pure module to assert the foolproof input-authority
 * contract Mike's directive names: "input ONLY in menus when menus are up,
 * and WORKS for gameplay when they are not."
 *
 * The real stack lives in port/src/inputctx.c with SDL coupling. This
 * cohort 2 suite exercises the algorithmic invariants via a pure mirror
 * (tests/inputctx_pure.{c,h}) so the runner stays SDL-free.
 *
 * Logging channel for any future runtime diagnostics around these
 * invariants: INPUT.CTX.* (per Mike's directive). The pure module here
 * does not log; it only asserts shape.
 *
 * @SYNC port/src/inputctx.c (push / pop / EndFrame / GetTop /
 *       gameplayInputSuppressed)
 * @SYNC context/designs/input-authority-and-menu-pool-2026-04-13.md §3
 */

#include "catch.hpp"

extern "C" {
#include "inputctx_pure.h"
}

namespace {

/* Test-side singletons that stand in for g_CtxGameplay / g_CtxImGuiMenu /
 * g_CtxPauseMenu / g_CtxDebugOverlay. Their addresses are the identity. */
InputContextPure k_Gameplay  = { "gameplay",  0, 0 };
InputContextPure k_ImguiMenu = { "imgui_menu", 0, 0 };
InputContextPure k_PauseMenu = { "pause_menu", 0, 0 };
InputContextPure k_DebugOvl  = { "debug_overlay", 0, 0 };
InputContextPure k_TextInput = { "text_input", 0, 0 };

void resetWorld()
{
    inputCtxPureReset();
    /* Re-initialise canonical contexts to defaults. */
    k_Gameplay  = { "gameplay",  0, 0 };
    k_ImguiMenu = { "imgui_menu", 0, 0 };
    k_PauseMenu = { "pause_menu", 0, 0 };
    k_DebugOvl  = { "debug_overlay", 0, 0 };
    k_TextInput = { "text_input", 0, 0 };
    inputCtxPureSetGameplayCtx(&k_Gameplay);
}

} /* namespace */

TEST_CASE("inputctx: empty stack has no top, no authority", "[inputctx][stack]")
{
    resetWorld();
    REQUIRE(inputCtxPureGetTop() == nullptr);
    REQUIRE(inputCtxPureDepth() == 0);
}

TEST_CASE("inputctx: gameplay-only stack does not suppress input", "[inputctx][authority]")
{
    resetWorld();
    REQUIRE(inputCtxPurePush(&k_Gameplay) == 1);
    REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
    REQUIRE(gameplayInputSuppressedPure() == 0);
}

TEST_CASE("inputctx: any non-gameplay context on top suppresses input", "[inputctx][authority]")
{
    resetWorld();
    REQUIRE(inputCtxPurePush(&k_Gameplay) == 1);
    REQUIRE(gameplayInputSuppressedPure() == 0);

    SECTION("imgui menu suppresses gameplay") {
        REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 1);
        REQUIRE(inputCtxPureGetTop() == &k_ImguiMenu);
        REQUIRE(gameplayInputSuppressedPure() == 1);
    }
    SECTION("pause menu suppresses gameplay") {
        REQUIRE(inputCtxPurePush(&k_PauseMenu) == 1);
        REQUIRE(gameplayInputSuppressedPure() == 1);
    }
    SECTION("debug overlay suppresses gameplay") {
        REQUIRE(inputCtxPurePush(&k_DebugOvl) == 1);
        REQUIRE(gameplayInputSuppressedPure() == 1);
    }
    SECTION("text input suppresses gameplay") {
        REQUIRE(inputCtxPurePush(&k_TextInput) == 1);
        REQUIRE(gameplayInputSuppressedPure() == 1);
    }
}

TEST_CASE("inputctx bridge: menu layer tracks effective non-gameplay top",
          "[inputctx][inputlayer][bridge]")
{
    resetWorld();
    REQUIRE(inputCtxPurePush(&k_Gameplay) == 1);
    REQUIRE(inputCtxPureMenuLayerActive() == 0);
    REQUIRE(inputCtxPureMenuLayerPushCount() == 0);
    REQUIRE(inputCtxPureMenuLayerPopCount() == 0);

    REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 1);
    REQUIRE(inputCtxPureMenuLayerActive() == 1);
    REQUIRE(inputCtxPureMenuLayerPushCount() == 1);
    REQUIRE(inputCtxPureMenuLayerPopCount() == 0);

    REQUIRE(inputCtxPurePush(&k_DebugOvl) == 1);
    REQUIRE(inputCtxPureMenuLayerActive() == 1);
    REQUIRE(inputCtxPureMenuLayerPushCount() == 1);

    inputCtxPurePopDeferred(&k_DebugOvl);
    REQUIRE(inputCtxPureGetTop() == &k_ImguiMenu);
    REQUIRE(inputCtxPureMenuLayerActive() == 1);
    inputCtxPureEndFrame();
    REQUIRE(inputCtxPureMenuLayerActive() == 1);

    inputCtxPurePopDeferred(&k_ImguiMenu);
    REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
    REQUIRE(inputCtxPureMenuLayerActive() == 0);
    REQUIRE(inputCtxPureMenuLayerPopCount() == 1);
    REQUIRE(inputCtxPureDepth() == 2);

    inputCtxPureEndFrame();
    REQUIRE(inputCtxPureDepth() == 1);
    REQUIRE(inputCtxPureMenuLayerActive() == 0);
}

TEST_CASE("inputctx bridge: resurrecting a marked menu restores menu layer",
          "[inputctx][inputlayer][bridge][resurrect]")
{
    resetWorld();
    REQUIRE(inputCtxPurePush(&k_Gameplay) == 1);
    REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 1);
    REQUIRE(inputCtxPureMenuLayerActive() == 1);

    inputCtxPurePopDeferred(&k_ImguiMenu);
    REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
    REQUIRE(inputCtxPureMenuLayerActive() == 0);

    REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 2);
    REQUIRE(inputCtxPureGetTop() == &k_ImguiMenu);
    REQUIRE(inputCtxPureMenuLayerActive() == 1);
    REQUIRE(inputCtxPureMenuLayerPushCount() == 2);
    REQUIRE(inputCtxPureMenuLayerPopCount() == 1);
}

TEST_CASE("inputctx: deferred pop unblocks gameplay only after EndFrame", "[inputctx][authority][deferred]")
{
    resetWorld();
    inputCtxPurePush(&k_Gameplay);
    inputCtxPurePush(&k_ImguiMenu);
    REQUIRE(gameplayInputSuppressedPure() == 1);

    /* PopDeferred(&menu) marks the entry but doesn't compact. The top
     * lookup skips marked entries, so authority returns to gameplay
     * THIS frame. EndFrame later removes the slot. */
    inputCtxPurePopDeferred(&k_ImguiMenu);
    REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
    REQUIRE(gameplayInputSuppressedPure() == 0);
    REQUIRE(inputCtxPureDepth() == 2);  /* still physically present */

    inputCtxPureEndFrame();
    REQUIRE(inputCtxPureDepth() == 1);
    REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
}

TEST_CASE("inputctx: double-push of same context is rejected", "[inputctx][stack]")
{
    resetWorld();
    REQUIRE(inputCtxPurePush(&k_Gameplay) == 1);
    REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 1);
    REQUIRE(inputCtxPureDepth() == 2);

    /* Second push of the same context: rejected, depth unchanged. */
    REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 0);
    REQUIRE(inputCtxPureDepth() == 2);
    REQUIRE(inputCtxPureGetTop() == &k_ImguiMenu);
}

TEST_CASE("inputctx: pushing a marked context resurrects it", "[inputctx][stack][resurrect]")
{
    resetWorld();
    inputCtxPurePush(&k_Gameplay);
    inputCtxPurePush(&k_ImguiMenu);
    inputCtxPurePopDeferred(&k_ImguiMenu);
    REQUIRE(k_ImguiMenu.marked_for_removal == 1);

    /* Push again before EndFrame: resurrects (un-marks); depth stays the
     * same. This is the S197a fix replicated. */
    REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 2);  /* 2 = resurrect */
    REQUIRE(k_ImguiMenu.marked_for_removal == 0);
    REQUIRE(inputCtxPureDepth() == 2);
    REQUIRE(inputCtxPureGetTop() == &k_ImguiMenu);
    REQUIRE(gameplayInputSuppressedPure() == 1);
}

TEST_CASE("inputctx: stack overflow returns -1, depth unchanged", "[inputctx][stack][overflow]")
{
    /* Static storage (not stack-local) so any pointer left in s_Stack[]
     * after this case is still valid when the next test's resetWorld()
     * compactor walks the array. The real inputctx.c contexts are file-
     * static globals (g_CtxGameplay, ...), so this matches the real
     * lifetime model. */
    static InputContextPure s_Dummies[INPUTCTX_PURE_MAX_STACK + 1];
    resetWorld();
    for (s32 i = 0; i < INPUTCTX_PURE_MAX_STACK + 1; i++) {
        s_Dummies[i] = { "dummy", 0, 0 };
    }
    s32 pushed = 0;
    for (s32 i = 0; i < INPUTCTX_PURE_MAX_STACK; i++) {
        if (inputCtxPurePush(&s_Dummies[i]) == 1) pushed++;
    }
    REQUIRE(pushed == INPUTCTX_PURE_MAX_STACK);
    REQUIRE(inputCtxPureDepth() == INPUTCTX_PURE_MAX_STACK);

    /* One more push must fail. */
    REQUIRE(inputCtxPurePush(&s_Dummies[INPUTCTX_PURE_MAX_STACK]) == -1);
    REQUIRE(inputCtxPureDepth() == INPUTCTX_PURE_MAX_STACK);
}

TEST_CASE("inputctx: focus-lost suppresses even when only gameplay is on stack",
          "[inputctx][authority][focus]")
{
    resetWorld();
    inputCtxPurePush(&k_Gameplay);
    REQUIRE(gameplayInputSuppressedPure() == 0);

    inputCtxPureNotifyFocus(0);  /* focus LOST */
    REQUIRE(inputCtxPureIsFocusLost() == 1);
    REQUIRE(gameplayInputSuppressedPure() == 1);
}

TEST_CASE("inputctx: focus-regain holds suppression until settle clears",
          "[inputctx][authority][focus][settle]")
{
    resetWorld();
    inputCtxPurePush(&k_Gameplay);
    inputCtxPureNotifyFocus(0);
    inputCtxPureNotifyFocus(1);  /* GAINED - should arm settle */

    /* Window has focus again, but settle is still pending. */
    REQUIRE(inputCtxPureIsFocusLost() == 0);
    REQUIRE(gameplayInputSuppressedPure() == 1);

    /* Once settle expires (modeled by explicit clear), gameplay reads
     * authority back. */
    inputCtxPureClearFocusSettle();
    REQUIRE(gameplayInputSuppressedPure() == 0);
}

TEST_CASE("inputctx: cursor authority collapses to the leaf",
          "[inputctx][authority][cursor]")
{
    /* The system-level invariant is that mouse mode is dictated by the
     * top context (gameplay = relative/captured; menu/pause/overlay =
     * absolute/visible). The authority predicate is the same predicate
     * that drives mouse-mode reconciliation. So checking
     * gameplayInputSuppressed correctness IS checking cursor-authority
     * collapse (one signal, two consumers). */
    resetWorld();
    inputCtxPurePush(&k_Gameplay);
    REQUIRE(gameplayInputSuppressedPure() == 0);

    inputCtxPurePush(&k_PauseMenu);     /* leaf is pause */
    REQUIRE(gameplayInputSuppressedPure() == 1);

    inputCtxPurePush(&k_DebugOvl);      /* leaf is debug */
    REQUIRE(gameplayInputSuppressedPure() == 1);

    inputCtxPurePopDeferred(&k_DebugOvl);
    inputCtxPureEndFrame();
    REQUIRE(inputCtxPureGetTop() == &k_PauseMenu);
    REQUIRE(gameplayInputSuppressedPure() == 1);  /* still pause on top */

    inputCtxPurePopDeferred(&k_PauseMenu);
    inputCtxPureEndFrame();
    REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
    REQUIRE(gameplayInputSuppressedPure() == 0);  /* gameplay restored */
}

TEST_CASE("inputctx: authority closes both ways round-trip", "[inputctx][authority][roundtrip]")
{
    /* Mike's directive: input ONLY in menus when menus are up, and WORKS
     * for gameplay when they are not. Drive a realistic open-close cycle
     * and verify both directions. */
    resetWorld();
    inputCtxPurePush(&k_Gameplay);

    for (int i = 0; i < 20; i++) {
        REQUIRE(gameplayInputSuppressedPure() == 0);

        /* Open menu */
        REQUIRE(inputCtxPurePush(&k_ImguiMenu) == 1);
        REQUIRE(gameplayInputSuppressedPure() == 1);

        /* Close menu (deferred + EndFrame) */
        inputCtxPurePopDeferred(&k_ImguiMenu);
        REQUIRE(gameplayInputSuppressedPure() == 0);  /* THIS frame */
        inputCtxPureEndFrame();
        REQUIRE(gameplayInputSuppressedPure() == 0);  /* and after */
        REQUIRE(inputCtxPureGetTop() == &k_Gameplay);
    }
}
