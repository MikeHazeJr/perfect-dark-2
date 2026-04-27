/*
 * tests/test_smoke.cpp -- Sanity check that the framework links and runs.
 *
 * If this case fails, the build is broken in a way that makes every other
 * test result meaningless. So this is the canary.
 */

#include "catch.hpp"

TEST_CASE("smoke: arithmetic still works", "[smoke]") {
    REQUIRE(1 + 1 == 2);
    REQUIRE(2 * 3 == 6);
}

TEST_CASE("smoke: REQUIRE failure path is reachable (positive case)", "[smoke]") {
    /* Verifies that a passing REQUIRE actually evaluates the expression. */
    int x = 0;
    REQUIRE((x = 5) == 5);
    REQUIRE(x == 5);
}

TEST_CASE("smoke: SECTION isolation", "[smoke]") {
    int counter = 0;

    SECTION("first section starts at zero") {
        counter += 1;
        REQUIRE(counter == 1);
    }

    SECTION("second section also starts at zero") {
        counter += 2;
        REQUIRE(counter == 2);
    }
}
