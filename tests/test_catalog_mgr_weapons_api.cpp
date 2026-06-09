/*
 * tests/test_catalog_mgr_weapons_api.cpp -- S484 F1 catalog manager
 * weapon API contract spec.
 *
 * Source under test:
 *   port/src/catalog_mgr_weapons_pure.c (pure validators + variant spec)
 *
 * Live router (port/src/catalog_mgr_weapons.c) wraps the pure helpers and
 * adds g_Weapons[] / g_AibotWeaponPreferences[] dereferences. Live router
 * is exercised at runtime; pd-tests pin the contract via the pure layer
 * so the test stays globals-free.
 *
 * Coverage:
 *   - Bounds-check rule: in-range = ok, out-of-range = miss.
 *   - CATALOG_MGR_WEAPON_COUNT pin: 96 (base WEAPON_SUICIDEPILL + 1
 *     plus 10 catalog-owned private custom runtime slots).
 *   - EYESPY stage-index variant rule: AIRBASE -> DrugSpy,
 *     CHICAGO|MBR -> BombSpy, default -> CamSpy.
 *   - EYESPY variant spec tuple: each variant maps to (langid,
 *     set_flags, clear_flags) with the expected mutual exclusion.
 *   - Invalid variant returns 0 and zero-fills output.
 *
 * @SYNC: changes to bondgunreset.c::bgunReset EYESPY block or to
 *        the live mutator must be reflected here.
 */

#include "catch.hpp"

extern "C" {
#include "catalog_mgr_weapons_pure.h"
}

namespace {

/* Mirror of constants.h values verified at audit time. If any of these
 * drift, the spec mirror below drifts; pin them explicitly. */
constexpr s32 kSTAGEINDEX_AIRBASE = 0x13;
constexpr s32 kSTAGEINDEX_CHICAGO = 0x09;
constexpr s32 kSTAGEINDEX_MBR     = 0x23;

constexpr s32 kL_GUN_060_CAMSPY  = 60;  /* placeholder for langbank id */
constexpr s32 kL_GUN_061_DRUGSPY = 61;
constexpr s32 kL_GUN_062_BOMBSPY = 62;

constexpr u32 kWEAPONFLAG_DETERMINER_S_AN = 0x00200000;
constexpr u32 kWEAPONFLAG_DETERMINER_F_AN = 0x00400000;
constexpr u32 kEYESPY_AN_MASK = kWEAPONFLAG_DETERMINER_S_AN | kWEAPONFLAG_DETERMINER_F_AN;

constexpr s32 kWEAPON_SUICIDEPILL = 0x55;  /* src/include/constants.h:4539 */
constexpr s32 kWEAPON_CUSTOM_COUNT = 0x0a;
constexpr s32 kWEAPON_CUSTOM_END = kWEAPON_SUICIDEPILL + 1 + kWEAPON_CUSTOM_COUNT;

}  /* anonymous namespace */

TEST_CASE("catalog-mgr-weapon: count pin includes private custom slots",
          "[catalog-mgr-weapon][s484][f1]") {
    REQUIRE(CATALOG_MGR_WEAPON_COUNT_PURE == kWEAPON_CUSTOM_END);
    REQUIRE(CATALOG_MGR_WEAPON_COUNT_PURE == 96);
}

TEST_CASE("catalog-mgr-weapon: bounds-check in-range",
          "[catalog-mgr-weapon][s484][f1]") {
    REQUIRE(catalogMgrWeaponIsInRangePure(0) == 1);
    REQUIRE(catalogMgrWeaponIsInRangePure(1) == 1);
    REQUIRE(catalogMgrWeaponIsInRangePure(38) == 1);  /* WEAPON_EYESPY */
    REQUIRE(catalogMgrWeaponIsInRangePure(CATALOG_MGR_WEAPON_COUNT_PURE - 1) == 1);
}

TEST_CASE("catalog-mgr-weapon: bounds-check out-of-range",
          "[catalog-mgr-weapon][s484][f1]") {
    REQUIRE(catalogMgrWeaponIsInRangePure(-1) == 0);
    REQUIRE(catalogMgrWeaponIsInRangePure(-100) == 0);
    REQUIRE(catalogMgrWeaponIsInRangePure(CATALOG_MGR_WEAPON_COUNT_PURE) == 0);
    REQUIRE(catalogMgrWeaponIsInRangePure(CATALOG_MGR_WEAPON_COUNT_PURE + 1) == 0);
    REQUIRE(catalogMgrWeaponIsInRangePure(254) == 0);  /* SPAWNWEAPON_FIESTA_SENTINEL */
    REQUIRE(catalogMgrWeaponIsInRangePure(255) == 0);
}

TEST_CASE("catalog-mgr-weapon: eyespy from stage-index AIRBASE",
          "[catalog-mgr-weapon][s484][f1]") {
    auto v = catalogMgrWeaponEyespyFromStageIndexPure(
        kSTAGEINDEX_AIRBASE,
        kSTAGEINDEX_AIRBASE, kSTAGEINDEX_CHICAGO, kSTAGEINDEX_MBR);
    REQUIRE(v == EYESPY_VARIANT_PURE_DRUGSPY);
}

TEST_CASE("catalog-mgr-weapon: eyespy from stage-index CHICAGO + MBR",
          "[catalog-mgr-weapon][s484][f1]") {
    auto v1 = catalogMgrWeaponEyespyFromStageIndexPure(
        kSTAGEINDEX_CHICAGO,
        kSTAGEINDEX_AIRBASE, kSTAGEINDEX_CHICAGO, kSTAGEINDEX_MBR);
    REQUIRE(v1 == EYESPY_VARIANT_PURE_BOMBSPY);

    auto v2 = catalogMgrWeaponEyespyFromStageIndexPure(
        kSTAGEINDEX_MBR,
        kSTAGEINDEX_AIRBASE, kSTAGEINDEX_CHICAGO, kSTAGEINDEX_MBR);
    REQUIRE(v2 == EYESPY_VARIANT_PURE_BOMBSPY);
}

TEST_CASE("catalog-mgr-weapon: eyespy from stage-index default = CamSpy",
          "[catalog-mgr-weapon][s484][f1]") {
    /* Any stage that isn't Airbase/Chicago/MBR is CamSpy. */
    auto v = catalogMgrWeaponEyespyFromStageIndexPure(
        0,  /* arbitrary other stage */
        kSTAGEINDEX_AIRBASE, kSTAGEINDEX_CHICAGO, kSTAGEINDEX_MBR);
    REQUIRE(v == EYESPY_VARIANT_PURE_CAMSPY);

    auto v2 = catalogMgrWeaponEyespyFromStageIndexPure(
        99,
        kSTAGEINDEX_AIRBASE, kSTAGEINDEX_CHICAGO, kSTAGEINDEX_MBR);
    REQUIRE(v2 == EYESPY_VARIANT_PURE_CAMSPY);
}

TEST_CASE("catalog-mgr-weapon: eyespy variant spec CamSpy sets AN flags",
          "[catalog-mgr-weapon][s484][f1]") {
    eyespy_variant_spec_pure_t spec = {};
    s32 ok = catalogMgrWeaponEyespyVariantSpecPure(
        EYESPY_VARIANT_PURE_CAMSPY,
        kL_GUN_060_CAMSPY, kL_GUN_061_DRUGSPY, kL_GUN_062_BOMBSPY,
        kEYESPY_AN_MASK, &spec);
    REQUIRE(ok == 1);
    REQUIRE(spec.name_langid == kL_GUN_060_CAMSPY);
    REQUIRE(spec.set_flags == kEYESPY_AN_MASK);
    REQUIRE(spec.clear_flags == 0u);
}

TEST_CASE("catalog-mgr-weapon: eyespy variant spec DrugSpy clears AN flags",
          "[catalog-mgr-weapon][s484][f1]") {
    eyespy_variant_spec_pure_t spec = {};
    s32 ok = catalogMgrWeaponEyespyVariantSpecPure(
        EYESPY_VARIANT_PURE_DRUGSPY,
        kL_GUN_060_CAMSPY, kL_GUN_061_DRUGSPY, kL_GUN_062_BOMBSPY,
        kEYESPY_AN_MASK, &spec);
    REQUIRE(ok == 1);
    REQUIRE(spec.name_langid == kL_GUN_061_DRUGSPY);
    REQUIRE(spec.set_flags == 0u);
    REQUIRE(spec.clear_flags == kEYESPY_AN_MASK);
}

TEST_CASE("catalog-mgr-weapon: eyespy variant spec BombSpy clears AN flags",
          "[catalog-mgr-weapon][s484][f1]") {
    eyespy_variant_spec_pure_t spec = {};
    s32 ok = catalogMgrWeaponEyespyVariantSpecPure(
        EYESPY_VARIANT_PURE_BOMBSPY,
        kL_GUN_060_CAMSPY, kL_GUN_061_DRUGSPY, kL_GUN_062_BOMBSPY,
        kEYESPY_AN_MASK, &spec);
    REQUIRE(ok == 1);
    REQUIRE(spec.name_langid == kL_GUN_062_BOMBSPY);
    REQUIRE(spec.set_flags == 0u);
    REQUIRE(spec.clear_flags == kEYESPY_AN_MASK);
}

TEST_CASE("catalog-mgr-weapon: eyespy variant spec invalid -> 0 + zero-fill",
          "[catalog-mgr-weapon][s484][f1]") {
    eyespy_variant_spec_pure_t spec = {};
    spec.name_langid = 999;
    spec.set_flags = 0xFFFFFFFFu;
    spec.clear_flags = 0xFFFFFFFFu;

    s32 ok = catalogMgrWeaponEyespyVariantSpecPure(
        EYESPY_VARIANT_PURE_INVALID,
        kL_GUN_060_CAMSPY, kL_GUN_061_DRUGSPY, kL_GUN_062_BOMBSPY,
        kEYESPY_AN_MASK, &spec);
    REQUIRE(ok == 0);
    /* set/clear and langid zero-filled on miss path. */
    REQUIRE(spec.name_langid == 0);
    REQUIRE(spec.set_flags == 0u);
    REQUIRE(spec.clear_flags == 0u);
}

TEST_CASE("catalog-mgr-weapon: eyespy variant spec NULL out -> 0",
          "[catalog-mgr-weapon][s484][f1]") {
    s32 ok = catalogMgrWeaponEyespyVariantSpecPure(
        EYESPY_VARIANT_PURE_CAMSPY,
        kL_GUN_060_CAMSPY, kL_GUN_061_DRUGSPY, kL_GUN_062_BOMBSPY,
        kEYESPY_AN_MASK, nullptr);
    REQUIRE(ok == 0);
}

TEST_CASE("catalog-mgr-weapon: variants are mutually exclusive on AN flags",
          "[catalog-mgr-weapon][s484][f1]") {
    /* CamSpy SETS AN; Drug/Bomb CLEAR AN. The mutator's net effect on
     * weapon->flags is: (flags & ~clear_flags) | set_flags. Verify each
     * variant's clear and set masks are mutually exclusive: the
     * intersection must be zero so the order of (clear, set) doesn't
     * matter. */
    eyespy_variant_spec_pure_t spec;
    for (s32 v = 0; v < 3; ++v) {
        REQUIRE(catalogMgrWeaponEyespyVariantSpecPure(
            (eyespy_variant_pure_e)v,
            kL_GUN_060_CAMSPY, kL_GUN_061_DRUGSPY, kL_GUN_062_BOMBSPY,
            kEYESPY_AN_MASK, &spec) == 1);
        REQUIRE((spec.set_flags & spec.clear_flags) == 0u);
    }
}
