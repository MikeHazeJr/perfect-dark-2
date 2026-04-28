/*
 * test_actionmap_flush.cpp -- Cohort 1 invariant tests.
 *
 * Locks down the existing actionmapFlushGameplayState semantics that
 * Cohort 4's cutscene-flash fix depends on:
 *
 *   - gameplay-only actions clear (held/pressed/released/value/timing)
 *   - shared / menu / system actions persist (USE, CANCEL_USE, PAUSE,
 *     SCORECARD, SCREENSHOT, CONSOLE_TOGGLE, DEBUG_TOGGLE, CHEAT_ENTER,
 *     SCORECARD_HOLD, MENU_*, TEXT_PASTE)
 *   - any action that was held synthesizes a released edge so consumers
 *     latched on press see the corresponding release
 *
 * Pure-C mirror at tests/actionmap_pure.{c,h}.
 *
 * Logging channel reserved for runtime diagnostics: INPUT.ACTION.FLUSH
 * (per the input-universality-and-transitions design).
 */

#include "catch.hpp"

extern "C" {
#include "actionmap_pure.h"
}

namespace {

void resetWorld()
{
    ampReset();
}

} /* namespace */

TEST_CASE("actionmap classifier: gameplay-only actions are gameplay-only", "[actionmap][classifier]")
{
    resetWorld();

    SECTION("movement actions are gameplay-only") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MOVE_FORWARD)  == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MOVE_BACKWARD) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MOVE_LEFT)     == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MOVE_RIGHT)    == 1);
    }
    SECTION("aim axes are gameplay-only") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_AXIS_AIM_X) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_AXIS_AIM_Y) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_AIM_UP)     == 1);
    }
    SECTION("combat actions are gameplay-only") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_FIRE_PRIMARY)   == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_FIRE_SECONDARY) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_RELOAD)         == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_THROW_WEAPON)   == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_JUMP)           == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_CROUCH)         == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_SPRINT)         == 1);
    }
    SECTION("weapon selection is gameplay-only") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_WEAPON_PREV) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_WEAPON_NEXT) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_WEAPON_1)    == 1);
    }
    SECTION("vehicle actions are gameplay-only") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_VEHICLE_ACCELERATE) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_VEHICLE_BRAKE)      == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_VEHICLE_EXIT)       == 1);
    }
    SECTION("forge actions are gameplay-only") {
        /* Forge fires from gameplay IMC; suppression is delegated to the
         * actionIsBlockedInFreefly classifier, not this one. */
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_FORGE_TOGGLE) == 1);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_FORGE_ASCEND) == 1);
    }
}

TEST_CASE("actionmap classifier: shared actions are NOT gameplay-only", "[actionmap][classifier]")
{
    resetWorld();

    SECTION("USE and CANCEL_USE alias menu accept/cancel") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_USE)        == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_CANCEL_USE) == 0);
    }
    SECTION("system hotkeys are shared") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_PAUSE)          == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_SCREENSHOT)     == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_CONSOLE_TOGGLE) == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_DEBUG_TOGGLE)   == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_CHEAT_ENTER)    == 0);
    }
    SECTION("menu navigation is owned by menu layer") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MENU_UP)        == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MENU_DOWN)      == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MENU_LEFT)      == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MENU_RIGHT)     == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MENU_TAB_PREV)  == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_MENU_TAB_NEXT)  == 0);
    }
    SECTION("scorecard variants are shared (UI also uses them)") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_SCORECARD)      == 0);
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_SCORECARD_HOLD) == 0);
    }
    SECTION("Cohort 1: ACTION_TEXT_PASTE is shared (text input only)") {
        REQUIRE(ampIsGameplayOnly(AMP_ACTION_TEXT_PASTE) == 0);
    }
}

TEST_CASE("actionmap classifier: out-of-range returns 0", "[actionmap][classifier]")
{
    REQUIRE(ampIsGameplayOnly((AmpInputAction)-1)             == 0);
    REQUIRE(ampIsGameplayOnly((AmpInputAction)AMP_ACTION_COUNT) == 0);
    REQUIRE(ampIsGameplayOnly((AmpInputAction)9999)           == 0);
}

TEST_CASE("flush: clears every gameplay-only action across every player", "[actionmap][flush]")
{
    resetWorld();

    /* Hold a gameplay-only action on every player. */
    for (int p = 0; p < AMP_MAX_PLAYERS; p++) {
        ampSetHeld(p, AMP_ACTION_FIRE_PRIMARY);
        ampSetHeld(p, AMP_ACTION_JUMP);
        ampSetHeld(p, AMP_ACTION_SPRINT);
    }

    ampFlushGameplayState();

    for (int p = 0; p < AMP_MAX_PLAYERS; p++) {
        const AmpActionState *fp   = ampGetState(p, AMP_ACTION_FIRE_PRIMARY);
        const AmpActionState *jp   = ampGetState(p, AMP_ACTION_JUMP);
        const AmpActionState *spr  = ampGetState(p, AMP_ACTION_SPRINT);

        REQUIRE(fp->held    == 0);
        REQUIRE(fp->pressed == 0);
        REQUIRE(fp->value   == 0.0f);
        REQUIRE(jp->held    == 0);
        REQUIRE(jp->pressed == 0);
        REQUIRE(jp->value   == 0.0f);
        REQUIRE(spr->held   == 0);
        REQUIRE(spr->value  == 0.0f);
    }
}

TEST_CASE("flush: leaves shared actions intact", "[actionmap][flush]")
{
    resetWorld();

    /* Hold ACTION_USE (the cutscene-flash trigger), ACTION_PAUSE, and
     * a menu-nav action. Flush must NOT zero these. */
    ampSetHeld(0, AMP_ACTION_USE);
    ampSetHeld(0, AMP_ACTION_PAUSE);
    ampSetHeld(0, AMP_ACTION_MENU_DOWN);
    ampSetHeld(0, AMP_ACTION_TEXT_PASTE);

    /* Also hold a gameplay-only on the same player to confirm partial flush. */
    ampSetHeld(0, AMP_ACTION_FIRE_PRIMARY);

    ampFlushGameplayState();

    REQUIRE(ampGetState(0, AMP_ACTION_USE)->held         == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->value        == 1.0f);
    REQUIRE(ampGetState(0, AMP_ACTION_PAUSE)->held       == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_MENU_DOWN)->held   == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_TEXT_PASTE)->held  == 1);

    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->held == 0);
}

TEST_CASE("flush: synthesizes released edge for held actions", "[actionmap][flush]")
{
    resetWorld();

    ampSetHeld(0, AMP_ACTION_FIRE_PRIMARY);
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->held     == 1);
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->released == 0);

    ampFlushGameplayState();

    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->held     == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->released == 1);
}

TEST_CASE("flush: does not synthesize released edge for unheld gameplay actions", "[actionmap][flush]")
{
    resetWorld();

    /* No state set; flush must be a no-op for an idle slot. */
    ampFlushGameplayState();

    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->released == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_JUMP)->released         == 0);
}

TEST_CASE("flush: clears hold_consumed flag", "[actionmap][flush]")
{
    resetWorld();

    /* Mark a hold-consume on a gameplay action, simulating a long-press
     * handler that already fired its effect. */
    ampSetHeld(0, AMP_ACTION_USE); /* shared, must persist */
    ampSetHeld(0, AMP_ACTION_RELOAD); /* gameplay, must clear */
    ((AmpActionState *)ampGetState(0, AMP_ACTION_RELOAD))->hold_consumed = 1;
    ((AmpActionState *)ampGetState(0, AMP_ACTION_USE))->hold_consumed    = 1;

    ampFlushGameplayState();

    REQUIRE(ampGetState(0, AMP_ACTION_RELOAD)->hold_consumed == 0);
    /* Shared action's hold_consumed is preserved since flush skips shared. */
    REQUIRE(ampGetState(0, AMP_ACTION_USE)->hold_consumed    == 1);
}

TEST_CASE("flush: bug invariant (the Mission 1 obj 2 cutscene flash)", "[actionmap][flush][bug]")
{
    /* Repro: held ACTION_USE from a menu-accept must clear from gameplay
     * state when the layer-stack push fires (Cohort 4 will wire this).
     * Today, ACTION_USE is shared so flush leaves it intact -- but the
     * gameplay-only siblings (FIRE_PRIMARY, RELOAD, etc.) that the
     * cutscene skip check polls at player.c:3059,3072,3084 must clear.
     *
     * This test asserts the slice that's load-bearing for the fix:
     * gameplay-only inputs that get held via key-mapping aliasing
     * across the menu-accept transition do NOT survive the flush. */
    resetWorld();

    ampSetHeld(0, AMP_ACTION_FIRE_PRIMARY);
    ampSetHeld(0, AMP_ACTION_FIRE_SECONDARY);
    ampSetHeld(0, AMP_ACTION_FIRE_MODE);
    ampSetHeld(0, AMP_ACTION_RELOAD);
    ampSetHeld(0, AMP_ACTION_WEAPON_NEXT);

    ampFlushGameplayState();

    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_PRIMARY)->held   == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_SECONDARY)->held == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_FIRE_MODE)->held      == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_RELOAD)->held         == 0);
    REQUIRE(ampGetState(0, AMP_ACTION_WEAPON_NEXT)->held    == 0);
}
