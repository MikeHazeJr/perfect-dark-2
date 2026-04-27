/*
 * tests/test_bodies_headcount.cpp -- INV-5 / Cohort E.2: pure stage_id ->
 * head-count override lookup for bodiesReset.
 *
 * Source under test:
 *   port/src/bodies_headcount.c::bodiesGetActiveHeadCountForStageId
 *
 * The bug shape, restated for the regression record. bodiesReset's
 * per-stage override map historically used a strcmp literal chain
 * (base:infiltration -> 5, base:rescue -> 4, base:escape -> 5,
 * default 8). When stage_id was empty, mod-defined, or any unknown
 * value, the lookup silently fell through to the default 8. Players
 * on a mod stage saw guards using the wrong head set with no
 * diagnostic.
 *
 * INV-5 / Cohort E.2: lift the strcmp chain into a pure helper so the
 * override map is locked by tests + the call site logs WARNING when
 * the lookup falls through to the default. The tests below pin the
 * map and the default-fallthrough behavior.
 */

#include "catch.hpp"

extern "C" {
#include "bodies_headcount.h"
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: known overrides return their value",
          "[bodies][headcount][regression]") {
	/* The three base solo missions whose head budget differs from the
	 * default 8. Pinned because changing these silently would alter the
	 * guard pool size for those missions in single-player. */
	REQUIRE(bodiesGetActiveHeadCountForStageId("base:infiltration", 8) == 5);
	REQUIRE(bodiesGetActiveHeadCountForStageId("base:rescue",       8) == 4);
	REQUIRE(bodiesGetActiveHeadCountForStageId("base:escape",       8) == 5);
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: unknown stage_id returns default",
          "[bodies][headcount][regression]") {
	/* Mod stages, AllInOne stages, or any future base mission falls
	 * through. The default is the caller's choice. */
	REQUIRE(bodiesGetActiveHeadCountForStageId("base:airbase",   8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId("aimods:gex_mission_1", 8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId("garbage_id_no_namespace", 8) == 8);
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: empty stage_id returns default",
          "[bodies][headcount][regression]") {
	/* Empty string is the canonical "stage_id not yet resolved" state
	 * (matchsetup hasn't run, or save migration left it empty). The
	 * lookup must not crash and must return the caller's default. */
	REQUIRE(bodiesGetActiveHeadCountForStageId("", 8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId("", 4) == 4);
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: NULL stage_id returns default",
          "[bodies][headcount][regression]") {
	/* Defensive: callers should never pass NULL but if a future caller
	 * does (e.g. uninitialized struct field after a load failure) the
	 * helper must not deref. */
	REQUIRE(bodiesGetActiveHeadCountForStageId(NULL, 8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId(NULL, 1) == 1);
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: default parameter is honored",
          "[bodies][headcount][regression]") {
	/* Default count is caller-provided so the helper is reusable for
	 * callers other than bodiesReset. Tests pin that the default is
	 * used as-is on miss. */
	REQUIRE(bodiesGetActiveHeadCountForStageId("unknown", 4)  == 4);
	REQUIRE(bodiesGetActiveHeadCountForStageId("unknown", 16) == 16);
	REQUIRE(bodiesGetActiveHeadCountForStageId("unknown", 1)  == 1);
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: case-sensitive match",
          "[bodies][headcount][regression]") {
	/* Match is case-sensitive (strcmp not strcasecmp). Catalog IDs are
	 * always lowercase by convention; uppercase variants are caller
	 * bugs and should fall through to the default. */
	REQUIRE(bodiesGetActiveHeadCountForStageId("BASE:INFILTRATION", 8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId("Base:Infiltration", 8) == 8);
}

TEST_CASE("bodiesGetActiveHeadCountForStageId: namespace-less id falls through",
          "[bodies][headcount][regression]") {
	/* The override IDs include the "base:" namespace prefix. A bare
	 * "infiltration" without prefix is a different ID and must fall
	 * through to the default rather than match opportunistically. */
	REQUIRE(bodiesGetActiveHeadCountForStageId("infiltration", 8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId("rescue",       8) == 8);
	REQUIRE(bodiesGetActiveHeadCountForStageId("escape",       8) == 8);
}
