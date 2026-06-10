/*
 * tests/test_asset_fallback_telemetry.cpp -- c3849 Wave 1: normal-play asset
 * fallback telemetry.
 *
 * Source under test: port/src/asset_fallback_telemetry.c
 *   assetFallbackRecord / assetFallbackReportAndReset /
 *   assetFallbackPendingTotal / assetFallbackCountFor
 *
 * The c3844 constraint says post-extraction ROM/cache/legacy fallback must
 * fail loudly AND be tracked. Pinned behaviors: O(1) counting, per-family
 * buckets, first-offender snapshot semantics (count only -- the snapshot text
 * is log-only), out-of-range family bucketing to ASSET_NONE, quiet-when-zero
 * report, report consumes and clears.
 */

#include "catch.hpp"

extern "C" {
#include "asset_fallback_telemetry.h"
}

TEST_CASE("fallback telemetry: record increments family and total",
          "[catalog][fallback][telemetry][c3849]") {
	assetFallbackReportAndReset("test-clear");
	REQUIRE(assetFallbackPendingTotal() == 0);

	assetFallbackRecord(ASSET_TEXTURE, 42, "test fallback");
	assetFallbackRecord(ASSET_TEXTURE, 43, "test fallback 2");
	assetFallbackRecord(ASSET_ANIMATION, 7, "anim fallback");

	REQUIRE(assetFallbackPendingTotal() == 3);
	REQUIRE(assetFallbackCountFor(ASSET_TEXTURE) == 2);
	REQUIRE(assetFallbackCountFor(ASSET_ANIMATION) == 1);
	REQUIRE(assetFallbackCountFor(ASSET_SFX) == 0);
	assetFallbackReportAndReset("test-clear");
}

TEST_CASE("fallback telemetry: report consumes and clears",
          "[catalog][fallback][telemetry][c3849]") {
	assetFallbackReportAndReset("test-clear");
	assetFallbackRecord(ASSET_MUSIC, 11, "seq fallback");
	assetFallbackRecord(ASSET_MUSIC, 12, "seq fallback");

	REQUIRE(assetFallbackReportAndReset("test-checkpoint") == 2);
	REQUIRE(assetFallbackPendingTotal() == 0);
	REQUIRE(assetFallbackCountFor(ASSET_MUSIC) == 0);
	/* Quiet-when-zero contract: a second report returns 0. */
	REQUIRE(assetFallbackReportAndReset("test-checkpoint") == 0);
}

TEST_CASE("fallback telemetry: out-of-range family buckets to ASSET_NONE",
          "[catalog][fallback][telemetry][c3849]") {
	assetFallbackReportAndReset("test-clear");
	assetFallbackRecord((asset_type_e)-5, 1, "bogus family");
	assetFallbackRecord((asset_type_e)9999, 2, "bogus family");

	REQUIRE(assetFallbackCountFor(ASSET_NONE) == 2);
	REQUIRE(assetFallbackPendingTotal() == 2);
	/* The pin accessor buckets identically. */
	REQUIRE(assetFallbackCountFor((asset_type_e)9999) == 2);
	assetFallbackReportAndReset("test-clear");
}

TEST_CASE("fallback telemetry: null what is tolerated",
          "[catalog][fallback][telemetry][c3849]") {
	assetFallbackReportAndReset("test-clear");
	assetFallbackRecord(ASSET_SCENARIO, 3, NULL);
	REQUIRE(assetFallbackCountFor(ASSET_SCENARIO) == 1);
	REQUIRE(assetFallbackReportAndReset(NULL) == 1);
}
