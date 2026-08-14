#include "catch.hpp"

extern "C" {
#include "action_read_authority.h"
}

static ActionReadAuthorityInput inheritedSuppressedGameplay(void)
{
    ActionReadAuthorityInput input = {};
    input.aperture_decision = ACTION_READ_APERTURE_INHERIT;
    input.gameplay_suppressed = 1;
    input.gameplay_only = 1;
    return input;
}

TEST_CASE("action read authority honors explicit typed apertures",
    "[input][action-read-authority][b1085]")
{
    ActionReadAuthorityInput input = inheritedSuppressedGameplay();
    input.aperture_decision = ACTION_READ_APERTURE_ALLOW;
    REQUIRE(actionReadAuthorityAllows(&input) == 1);

    input.aperture_decision = ACTION_READ_APERTURE_DENY;
    input.smoke_owned = 1;
    input.gameplay_context = 1;
    input.focus_lost = 1;
    REQUIRE(actionReadAuthorityAllows(&input) == 0);
}

TEST_CASE("ordinary and non-gameplay reads retain production inheritance",
    "[input][action-read-authority][b1085]")
{
    ActionReadAuthorityInput input = {};
    input.aperture_decision = ACTION_READ_APERTURE_INHERIT;
    REQUIRE(actionReadAuthorityAllows(&input) == 1);

    input.gameplay_suppressed = 1;
    REQUIRE(actionReadAuthorityAllows(&input) == 1);
}

TEST_CASE("focus loss never admits an unowned gameplay action",
    "[input][action-read-authority][b1085]")
{
    ActionReadAuthorityInput input = inheritedSuppressedGameplay();
    input.gameplay_context = 1;
    input.focus_lost = 1;
    REQUIRE(actionReadAuthorityAllows(&input) == 0);
}

TEST_CASE("exact smoke ownership admits only focus-transition suppression",
    "[input][action-read-authority][b1085]")
{
    ActionReadAuthorityInput input = inheritedSuppressedGameplay();
    input.smoke_owned = 1;
    input.gameplay_context = 1;

    input.focus_lost = 1;
    REQUIRE(actionReadAuthorityAllows(&input) == 1);

    input.focus_lost = 0;
    input.focus_settle_active = 1;
    REQUIRE(actionReadAuthorityAllows(&input) == 1);

    input.focus_settle_active = 0;
    REQUIRE(actionReadAuthorityAllows(&input) == 0);
}

TEST_CASE("smoke ownership cannot bypass context or malformed decisions",
    "[input][action-read-authority][b1085]")
{
    ActionReadAuthorityInput input = inheritedSuppressedGameplay();
    input.smoke_owned = 1;
    input.focus_lost = 1;
    REQUIRE(actionReadAuthorityAllows(&input) == 0);

    input.gameplay_context = 1;
    input.aperture_decision = 2;
    REQUIRE(actionReadAuthorityAllows(&input) == 0);
    REQUIRE(actionReadAuthorityAllows(nullptr) == 0);
}
