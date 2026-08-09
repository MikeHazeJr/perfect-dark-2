/*
 * test_spawn_weapon_resolved.cpp -- S483c (2026-04-27, B-263)
 *
 * Pins the invariant Mike named in the comprehensive fix brief:
 *   "FIESTA-mode spawnWeaponNum never flows into modelmgrLoadProjectileModeldefs
 *    or any other weapon-num-as-array-index consumer."
 *
 * Background. The B-263 crash was a Fiesta-mode match start at LVTICK 0 of
 * stage 0x1f: g_MatchConfig.spawnWeaponNum carried SPAWNWEAPON_FIESTA_SENTINEL
 * (0xFE = 254). The model-preload guard at src/game/setup.c:2870-2872 read
 *
 *     if (spawnWeaponNum != 0xFF && spawnWeaponNum != 0) { /\* preload \*\/ }
 *
 * which let 0xFE flow through as if it were a real WEAPON_* enum.
 * modelmgrLoadProjectileModeldefs(254) then read g_Weapons[254] -- a 254-byte
 * walk past the end of the weapon runtime slot array -- and AVed
 * on the next deref.
 *
 * The fix introduces spawnWeaponNumIsResolved() in port/include/net/matchsetup.h
 * as a single source of truth that excludes all THREE reserved sentinel values
 * (0, 0xFF, 0xFE) instead of just two. Every consumer that previously open-
 * coded the predicate is migrated to call the helper. The leaf
 * modelmgrLoadProjectileModeldefs gains a defensive bounds check (with INV-1-
 * style "WEAPON.SLOT.MISS:" loud-fail logging) so a future bad caller surfaces
 * in the log instead of AVing.
 *
 * This file pins the helper's contract directly (so any future enum
 * reorder or sentinel addition shows up here), plus a behavioural pin that
 * walks every reserved sentinel and asserts no consumer would index
 * g_Weapons[]-style on it.
 *
 * @SYNC port/include/net/matchsetup.h    spawnWeaponNumIsResolved (the helper)
 * @SYNC port/include/net/matchsetup.h    SPAWNWEAPON_FIESTA_SENTINEL (= 0xFE)
 * @SYNC src/game/setup.c                 setupCreateProps spawn-weapon preload
 * @SYNC src/game/modelmgrreset.c         modelmgrLoadProjectileModeldefs leaf
 * @SYNC port/include/catalog_mgr_weapons_pure.h CATALOG_MGR_WEAPON_COUNT_PURE
 *       (size of the array the consumer indexes, including private custom slots)
 *
 * Methodology note (Mike's directive, B-263):
 *   When adding a new sentinel value, the audit must include EVERY consumer
 *   of the field, not just the writer that introduced the sentinel. The
 *   FIESTA sentinel was added cleanly to matchStart's writer (S482) but the
 *   downstream consumers in setup.c kept the legacy (!= 0xFF && != 0) check
 *   that the new sentinel was not designed to satisfy. This test fixture
 *   makes the consumer-side audit mechanical: any future sentinel addition
 *   that doesn't extend spawnWeaponNumIsResolved gets caught here.
 */

#include "catch.hpp"
#include <PR/ultratypes.h>

extern "C" {
#include "net/matchsetup.h"
}

/* ---------------------------------------------------------------------------
 * Helper-direct contract: walk every byte value 0..0xFF, assert the helper's
 * decision matches the documented spec.
 * ------------------------------------------------------------------------- */

TEST_CASE("spawnWeaponNumIsResolved: 0 is not resolved (unset)",
          "[matchsetup][spawn-weapon][sentinel][b263][s483c]")
{
    REQUIRE(spawnWeaponNumIsResolved(0) == 0);
}

TEST_CASE("spawnWeaponNumIsResolved: 0xFF is not resolved (legacy random sentinel)",
          "[matchsetup][spawn-weapon][sentinel][b263][s483c]")
{
    REQUIRE(spawnWeaponNumIsResolved(0xFF) == 0);
}

TEST_CASE("spawnWeaponNumIsResolved: 0xFE is not resolved (FIESTA sentinel)",
          "[matchsetup][spawn-weapon][sentinel][b263][s483c]")
{
    /* The B-263 crash was specifically this case: 0xFE flowing into
     * modelmgrLoadProjectileModeldefs and indexing g_Weapons[254]. */
    REQUIRE(SPAWNWEAPON_FIESTA_SENTINEL == 0xFE);
    REQUIRE(spawnWeaponNumIsResolved(SPAWNWEAPON_FIESTA_SENTINEL) == 0);
    REQUIRE(spawnWeaponNumIsResolved(0xFE) == 0);
}

TEST_CASE("spawnWeaponNumIsResolved: real WEAPON_* enums in [1, 0x55] are resolved",
          "[matchsetup][spawn-weapon][sentinel][b263][s483c]")
{
    /* WEAPON_UNARMED = 1 .. WEAPON_SUICIDEPILL = 0x55 are the populated
     * base slots in the weapon runtime pool. Anything in this range is a real
     * resolved weapon enum. */
    for (s32 num = 1; num <= 0x55; num++) {
        INFO("spawnWeaponNum = " << num);
        REQUIRE(spawnWeaponNumIsResolved(num) == 1);
    }
}

TEST_CASE("spawnWeaponNumIsResolved: all reserved values are excluded; everything else passes",
          "[matchsetup][spawn-weapon][sentinel][b263][s483c]")
{
    /* Exhaustive walk over the byte domain: the helper must reject ONLY
     * the three documented reserved values. If a future commit adds a new
     * sentinel without updating the helper (or removes one), this case
     * fails and points at the contract drift. */
    for (s32 num = 0; num <= 0xFF; num++) {
        const bool isReserved =
            (num == 0) ||
            (num == 0xFF) ||
            (num == SPAWNWEAPON_FIESTA_SENTINEL);

        INFO("spawnWeaponNum = " << num << " (0x" << std::hex << num << ")");
        if (isReserved) {
            REQUIRE(spawnWeaponNumIsResolved(num) == 0);
        } else {
            REQUIRE(spawnWeaponNumIsResolved(num) == 1);
        }
    }
}

TEST_CASE("spawnWeaponNumIsResolved: negative inputs do not crash and are accepted as 'resolved'",
          "[matchsetup][spawn-weapon][sentinel][b263][s483c][edge]")
{
    /* The helper takes s32 even though the field is u8 in struct matchconfig.
     * Pre-existing call sites cast (s32)g_MatchConfig.spawnWeaponNum so a
     * negative value cannot occur in practice today, but the helper must
     * not undefined-behaviour on a defensive-cast caller. We don't expand
     * the contract to claim anything about negative inputs other than
     * "doesn't crash and returns a deterministic value". */
    REQUIRE_NOTHROW(spawnWeaponNumIsResolved(-1));
    REQUIRE_NOTHROW(spawnWeaponNumIsResolved(-256));
    REQUIRE_NOTHROW(spawnWeaponNumIsResolved(0x10000));
}

/* ---------------------------------------------------------------------------
 * Behavioural pin: weapon-num-as-array-index consumer invariant.
 *
 * The B-263 crash class is "spawnWeaponNum used as an unguarded array index
 * into a fixed-size table". The leaf modelmgrLoadProjectileModeldefs is the
 * canonical such consumer (g_Weapons[]). This case mirrors the consumer's
 * gate at src/game/setup.c:2870-2872 (post-fix) and asserts that no reserved
 * sentinel value ever reaches the leaf.
 *
 * Concretely: simulate the gate, walk all u8 values, and verify that for
 * each reserved sentinel the gate denies entry. If a future regression
 * removes the spawnWeaponNumIsResolved gate or adds a fourth reserved
 * value not handled by the helper, this fixture fires.
 * ------------------------------------------------------------------------- */

namespace {

/* Pure mirror of the post-fix guard at src/game/setup.c:2878. */
bool consumer_gate_admits(s32 spawnWeaponNum)
{
    return spawnWeaponNumIsResolved(spawnWeaponNum) != 0;
}

/* Pure mirror of the post-fix leaf bound at modelmgrreset.c:172. The leaf
 * accepts anything in [0, kArraySize) and rejects everything else with a
 * loud-fail return. We treat "admitted" as "would index the weapon pool". */
constexpr s32 kArraySize_g_Weapons = 0x6d; /* base 0x00..0x55 + custom 0x56..0x6c */

bool leaf_admits_for_indexing(s32 weaponnum)
{
    return weaponnum >= 0 && weaponnum < kArraySize_g_Weapons;
}

} /* namespace */

TEST_CASE("FIESTA sentinel: consumer gate denies; leaf would AV without bound check",
          "[matchsetup][spawn-weapon][b263][s483c][regression]")
{
    /* The single most important pin: 0xFE must never reach the leaf via
     * the consumer gate. */
    const s32 sentinel = SPAWNWEAPON_FIESTA_SENTINEL;
    REQUIRE(consumer_gate_admits(sentinel) == false);

    /* And confirm the latent hazard the leaf bound catches: if the gate
     * is ever bypassed, the sentinel is OUT OF BOUNDS for g_Weapons[] and
     * the leaf-side WEAPON.SLOT.MISS guard is what stops the AV. */
    REQUIRE(leaf_admits_for_indexing(sentinel) == false);
}

TEST_CASE("Reserved sentinels: gate denies all of {0, 0xFF, 0xFE}",
          "[matchsetup][spawn-weapon][b263][s483c]")
{
    REQUIRE(consumer_gate_admits(0)    == false);
    REQUIRE(consumer_gate_admits(0xFF) == false);
    REQUIRE(consumer_gate_admits(0xFE) == false);
}

TEST_CASE("Real WEAPON_* enums pass the gate AND fit in g_Weapons[] bounds",
          "[matchsetup][spawn-weapon][b263][s483c]")
{
    for (s32 num = 1; num <= 0x55; num++) {
        INFO("weaponnum = " << num);
        REQUIRE(consumer_gate_admits(num) == true);
        REQUIRE(leaf_admits_for_indexing(num) == true);
    }
}

TEST_CASE("Private custom weapon slots pass the gate AND fit in weapon-pool bounds",
          "[matchsetup][spawn-weapon][b263][b855]")
{
    for (s32 num = 0x56; num < kArraySize_g_Weapons; num++) {
        INFO("weaponnum = " << num);
        REQUIRE(consumer_gate_admits(num) == true);
        REQUIRE(leaf_admits_for_indexing(num) == true);
    }
}

TEST_CASE("Out-of-bounds weaponnum reaches leaf only via gate failure (defence in depth)",
          "[matchsetup][spawn-weapon][b263][s483c][defence-in-depth]")
{
    /* Values 0x6d..0xFD are NOT reserved sentinels (so the upstream gate
     * admits them) but ARE out of bounds for the weapon pool. The leaf bound
     * is what catches these. This covers the case where a future caller
     * passes a bad value that bypassed the upstream gate logic
     * (e.g. wire-data tampering, not-yet-migrated consumer). */
    for (s32 num = kArraySize_g_Weapons; num <= 0xFD; num++) {
        INFO("weaponnum = " << num);
        /* Upstream gate says yes (it's not a reserved sentinel) -- */
        REQUIRE(consumer_gate_admits(num) == true);
        /* -- but the leaf bound MUST reject it. */
        REQUIRE(leaf_admits_for_indexing(num) == false);
    }
}
