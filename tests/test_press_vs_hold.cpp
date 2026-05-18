/*
 * test_press_vs_hold.cpp -- Phase 2 fix #1 invariants.
 *
 * Locks down the canonical Press-vs-Hold contract Mike named in the
 * 2026-05-01 directive:
 *
 *   "press X, start timer, if it goes over a 'held threshold', it
 *    triggers the 'Held X' input, if it is released before that
 *    threshold is met, it triggers as Press X."
 *
 * The actionmap_pure helpers ampWasTap / ampHoldConsumed /
 * ampLastGestureHoldMs / ampSetGesture mirror the production
 * actionWasTap / actionHoldConsumed / actionLastGestureHoldMs /
 * timing fields. Tests assert:
 *
 *   - Tap fires only on release frame, only when held < max_hold_ms,
 *     only when not consumed.
 *   - Long-hold-then-release does NOT fire Tap (because consumed or
 *     elapsed >= threshold).
 *   - Mid-hold (not yet released) never fires Tap.
 *   - Consumed gesture never fires Tap (the long-hold handler claimed it).
 *   - down_time_ms == 0 (no gesture started) never fires Tap.
 *   - Multi-player isolation: a tap on player 0 does not leak to player 1.
 *
 * @SYNC port/src/actionmap.cpp:1619 (actionWasTap)
 * @SYNC src/game/bondmove.c (X_BUTTON tap synthesis, hold-only interact)
 *
 * Logging channel reserved for runtime diagnostics: INPUT.ACTION.TAP
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

extern "C" {
#include "actionmap_pure.h"
}

namespace {

void resetWorld()
{
    ampReset();
}

std::string readTextFile(const char *path)
{
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    std::string::size_type pos = 0;
    while ((pos = text.find('\r', pos)) != std::string::npos) {
        text.erase(pos, 1);
    }
    return text;
}

} /* namespace */

TEST_CASE("press-hold: short release fires Tap", "[press-hold][tap]")
{
    resetWorld();
    /* Press at t=0, release at t=120, threshold 250 -> tap. */
    ampSetGesture(0, AMP_ACTION_USE, /*down*/ 1, /*up*/ 121);
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 1);
    REQUIRE(ampLastGestureHoldMs(0, AMP_ACTION_USE) == 120);
}

TEST_CASE("press-hold: release exactly at threshold is NOT a Tap", "[press-hold][tap][edge]")
{
    /* Boundary: elapsed < max_hold_ms. Equal-to-threshold means Hold. */
    resetWorld();
    ampSetGesture(0, AMP_ACTION_USE, /*down*/ 100, /*up*/ 350);
    REQUIRE(ampLastGestureHoldMs(0, AMP_ACTION_USE) == 250);
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
}

TEST_CASE("press-hold: release past threshold is NOT a Tap", "[press-hold][tap]")
{
    resetWorld();
    ampSetGesture(0, AMP_ACTION_USE, /*down*/ 100, /*up*/ 500);
    REQUIRE(ampLastGestureHoldMs(0, AMP_ACTION_USE) == 400);
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
}

TEST_CASE("press-hold: consumed gesture is NOT a Tap (long-hold handler claimed it)", "[press-hold][tap][consume]")
{
    /* This is the load-bearing case: a tap-length gesture that was
     * consumed by a long-hold handler must NOT also fire Tap. The
     * "Press XOR Hold" invariant. */
    resetWorld();
    ampSetGesture(0, AMP_ACTION_USE, /*down*/ 1, /*up*/ 100);
    ampConsumeHold(0, AMP_ACTION_USE);
    REQUIRE(ampHoldConsumed(0, AMP_ACTION_USE) == 1);
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
}

TEST_CASE("press-hold: mid-hold (not released) never fires Tap", "[press-hold][tap]")
{
    resetWorld();
    /* Set held WITHOUT setting released. */
    ampSetHeld(0, AMP_ACTION_USE);
    /* held=1, released=0, down_time_ms=1, up_time_ms=0 -- gesture in progress. */
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
    REQUIRE(ampLastGestureHoldMs(0, AMP_ACTION_USE) == 0);
}

TEST_CASE("press-hold: idle slot never fires Tap", "[press-hold][tap][safety]")
{
    resetWorld();
    /* No gesture set; everything zero. */
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
}

TEST_CASE("press-hold: out-of-range player / action returns 0", "[press-hold][tap][safety]")
{
    resetWorld();
    REQUIRE(ampWasTap(-1, AMP_ACTION_USE, 250) == 0);
    REQUIRE(ampWasTap(99, AMP_ACTION_USE, 250) == 0);
    REQUIRE(ampWasTap(0, (AmpInputAction)-1, 250) == 0);
    REQUIRE(ampWasTap(0, (AmpInputAction)AMP_ACTION_COUNT, 250) == 0);
    REQUIRE(ampHoldConsumed(-1, AMP_ACTION_USE) == 0);
    REQUIRE(ampLastGestureHoldMs(99, AMP_ACTION_USE) == 0);
}

TEST_CASE("press-hold: per-player isolation", "[press-hold][tap][multi]")
{
    /* Tap on player 0; player 1 must not see it. */
    resetWorld();
    ampSetGesture(0, AMP_ACTION_USE, 1, 100);
    REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 1);
    REQUIRE(ampWasTap(1, AMP_ACTION_USE, 250) == 0);
    REQUIRE(ampWasTap(2, AMP_ACTION_USE, 250) == 0);
    REQUIRE(ampWasTap(3, AMP_ACTION_USE, 250) == 0);
}

TEST_CASE("press-hold: action isolation", "[press-hold][tap][multi]")
{
    /* Tap on USE; CANCEL_USE / FIRE_PRIMARY must be unaffected. */
    resetWorld();
    ampSetGesture(0, AMP_ACTION_USE, 1, 100);
    REQUIRE(ampWasTap(0, AMP_ACTION_USE,         250) == 1);
    REQUIRE(ampWasTap(0, AMP_ACTION_CANCEL_USE,  250) == 0);
    REQUIRE(ampWasTap(0, AMP_ACTION_FIRE_PRIMARY,250) == 0);
}

TEST_CASE("press-hold: bondmove site 1 (X_BUTTON tap-only) regression", "[press-hold][tap][bondmove]")
{
    /* Mirrors the bondmove.c:1130 site after fix #1. The prior code
     * fired X_BUTTON for any release >= 80ms or any consumed long-hold;
     * the fix routes through ampWasTap so X_BUTTON fires only on a
     * true tap (released before threshold and not consumed).
     *
     * Prior behavior (broken): consumed long-hold ALSO fired X_BUTTON.
     * Fixed behavior: consumed long-hold does NOT fire X_BUTTON.
     *
     * Threshold 250ms is propGetActionUseHoldThresholdMs() default. */
    resetWorld();

    SECTION("genuine tap (50ms) -> X_BUTTON fires") {
        ampSetGesture(0, AMP_ACTION_USE, 1, 51);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 1);
    }
    SECTION("medium hold (200ms) but below threshold -> X_BUTTON fires") {
        ampSetGesture(0, AMP_ACTION_USE, 1, 201);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 1);
    }
    SECTION("long hold (400ms) -> X_BUTTON does NOT fire") {
        ampSetGesture(0, AMP_ACTION_USE, 1, 401);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
    }
    SECTION("long hold consumed -> X_BUTTON does NOT fire (regression of prior bug)") {
        /* Pre-fix this fired X_BUTTON via the actionHoldConsumed branch. */
        ampSetGesture(0, AMP_ACTION_USE, 1, 100);
        ampConsumeHold(0, AMP_ACTION_USE);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
    }
    SECTION("instant tap (0ms) does NOT fire (degenerate)") {
        /* down=0, up=0 means no gesture. ampWasTap rejects on
         * down_time_ms == 0. */
        ampSetGesture(0, AMP_ACTION_USE, 0, 0);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
    }
}

TEST_CASE("press-hold: bondmove site 2 (tap never activates interact) regression", "[press-hold][tap][interact]")
{
    /* ACTION_USE tap remains a valid Tap gesture, but bondmove must route it
     * only to X_BUTTON/reload. Interact is hold-only. */
    resetWorld();

    SECTION("crisp tap is a Tap gesture") {
        ampSetGesture(0, AMP_ACTION_USE, 1, 100);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 1);
    }
    SECTION("long hold is not a Tap gesture") {
        ampSetGesture(0, AMP_ACTION_USE, 1, 500);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
    }
    SECTION("consumed gesture is not a Tap gesture") {
        ampSetGesture(0, AMP_ACTION_USE, 1, 100);
        ampConsumeHold(0, AMP_ACTION_USE);
        REQUIRE(ampWasTap(0, AMP_ACTION_USE, 250) == 0);
    }
}

TEST_CASE("press-hold: bondmove no longer dispatches interact from USE tap", "[press-hold][tap][static][b340]")
{
    const std::string bondmove = readTextFile("src/game/bondmove.c");
    const std::string propobjHeader = readTextFile("src/include/game/propobj.h");

    REQUIRE_FALSE(bondmove.empty());
    REQUIRE_FALSE(propobjHeader.empty());

    REQUIRE(bondmove.find("propobjPcHoverbikeTapMountOnUseRelease") == std::string::npos);
    REQUIRE(propobjHeader.find("propobjPcHoverbikeTapMountOnUseRelease") == std::string::npos);
    const size_t tapOnlyComment = bondmove.find("PC ACTION_USE tap is reload-only");
    REQUIRE(tapOnlyComment != std::string::npos);
    const size_t nextPitchBlock = bondmove.find("if (!movedata.invertpitch)", tapOnlyComment);
    REQUIRE(nextPitchBlock != std::string::npos);
    const std::string tapOnlyBlock = bondmove.substr(tapOnlyComment, nextPitchBlock - tapOnlyComment);
    REQUIRE(tapOnlyBlock.find("JO_ACTION_ACTIVATE") == std::string::npos);
    REQUIRE(tapOnlyBlock.find("bmoveHandleActivate") == std::string::npos);
}

TEST_CASE("press-hold: consumed hold progress behaves like release after full-ring pin", "[press-hold][hold][static][b340]")
{
    const std::string actionmap = readTextFile("port/src/actionmap.cpp");
    const std::string prompt = readTextFile("port/fast3d/pdgui_interact_prompt.cpp");

    REQUIRE_FALSE(actionmap.empty());
    REQUIRE_FALSE(prompt.empty());

    const size_t progress = actionmap.find("f32 actionHoldProgress");
    REQUIRE(progress != std::string::npos);
    const size_t consumed = actionmap.find("if (st->hold_consumed)", progress);
    const size_t held = actionmap.find("if (!st->held || st->down_time_ms == 0)", progress);
    REQUIRE(consumed != std::string::npos);
    REQUIRE(held != std::string::npos);
    REQUIRE(consumed < held);

    REQUIRE(prompt.find("actionHeld(actionPlayer, ACTION_USE) && actionHoldConsumed") == std::string::npos);
}
