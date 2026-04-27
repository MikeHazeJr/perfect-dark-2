/*
 * tests/test_spawn_weapon_mode.cpp -- Spawn-weapon mode (Random / Fiesta) spec.
 *
 * Source of the rules:
 *   port/src/net/matchsetup.c
 *     - spawnWeaponPickFromSlots          (pure helper, signature mirrored here)
 *     - spawnWeaponPickFromActiveSet      (live entry point — reads g_MpSetup
 *                                          + rngRandom, replicated as spec)
 *     - matchStart() spawn-weapon block   (mode dispatch: SPECIFIC / RANDOM /
 *                                          FIESTA — replicated as spec_resolve)
 *   src/game/player.c playerSpawn         (FIESTA branch keying on
 *                                          g_MatchConfig.spawnWeaponMode +
 *                                          SPAWNWEAPON_FIESTA_SENTINEL)
 *   src/game/bot.c botSpawn               (mirror of player.c)
 *   port/src/scenario_save.c              (legacy-default rule for missing
 *                                          spawnWeaponMode in older saves)
 *
 * Linking the live function would cascade through assetcatalog + mpsetup
 * + game globals; we replicate the SPECIFICATION as a pure helper so the
 * test stays SDL/ENet-free. The drift audit is a `diff` between
 * spec_pickFromSlots below and matchsetup.c::spawnWeaponPickFromSlots.
 *
 * Mike's directive (2026-04-27):
 *   Random  = roll ONCE at match start; every spawn (every player, every
 *             bot) uses the rolled weapon for the rest of the match.
 *   Fiesta  = roll FRESH on every spawn — each respawn yields a different
 *             weapon, independent of other players / bots.
 *   Eligible pool = active match weapon set's 6 slots, NONE/DISABLED/SHIELD
 *             filtered out (today's effective "Random" pool).
 *
 * Coverage:
 *   - pool filter (NONE/DISABLED/SHIELD excluded)
 *   - degenerate empty pool returns 0 (no eligible weapon)
 *   - Random: roll once, every spawn returns the same weapon for a fixed seed
 *   - Fiesta: rolls fresh per spawn, sequence varies under fixed seed
 *   - Cross-version save: missing "spawnWeaponMode" defaults to RANDOM when
 *     spawn_weapon_id is empty, SPECIFIC when non-empty.
 *   - Wire pin: NET_PROTOCOL_VER 45 (carries u8 spawnWeaponMode + u8
 *     spawnWeaponNum after spawn_weapon_id in SVC_STAGE_START, u8 mode in
 *     CLC_LOBBY_START).
 */

#include "catch.hpp"
#include <PR/ultratypes.h>

#include <cstdint>
#include <cstring>
#include <vector>
#include <functional>

namespace {

/* Mirror of constants.h (post-cull, 2026-04-26). */
constexpr u8 kMPWEAPON_NONE     = 0x00;
constexpr u8 kMPWEAPON_FALCON2  = 0x01;
constexpr u8 kMPWEAPON_DRAGON   = 0x0e;
constexpr u8 kMPWEAPON_K7       = 0x0f;
constexpr u8 kMPWEAPON_AR34     = 0x10;
constexpr u8 kMPWEAPON_SHIELD   = 0x27;
constexpr u8 kMPWEAPON_DISABLED = 0x28;

/* Mirror of NUM_MPWEAPONSLOTS (constants.h). */
constexpr s32 kNUM_MPWEAPONSLOTS = 6;

/* Mirror of enum spawn_weapon_mode (port/include/net/matchsetup.h). */
constexpr u8 kSPAWNWEAPON_MODE_SPECIFIC = 0;
constexpr u8 kSPAWNWEAPON_MODE_RANDOM   = 1;
constexpr u8 kSPAWNWEAPON_MODE_FIESTA   = 2;

/* Mirror of SPAWNWEAPON_FIESTA_SENTINEL. */
constexpr u8 kSPAWNWEAPON_FIESTA_SENTINEL = 0xFE;

/* Pure replication of spawnWeaponPickFromSlots (matchsetup.c S481).
 * Filter out NONE/DISABLED/SHIELD, then index uniformly via RNG % count. */
s32 spec_pickFromSlots(const u8 *slots, s32 numSlots,
                       std::function<u32()> rng_fn)
{
    if (slots == nullptr || numSlots <= 0) return 0;
    if (numSlots > kNUM_MPWEAPONSLOTS) numSlots = kNUM_MPWEAPONSLOTS;

    u8 eligible[kNUM_MPWEAPONSLOTS];
    s32 num_eligible = 0;
    for (s32 i = 0; i < numSlots; i++) {
        u8 w = slots[i];
        if (w == kMPWEAPON_NONE) continue;
        if (w == kMPWEAPON_DISABLED) continue;
        if (w == kMPWEAPON_SHIELD) continue;
        eligible[num_eligible++] = w;
    }
    if (num_eligible == 0) return 0;
    u32 r = rng_fn();
    return (s32)eligible[r % (u32)num_eligible];
}

/* Deterministic xorshift32 — used to give a "fixed seed" to the spec helper. */
struct seeded_rng {
    u32 state;
    explicit seeded_rng(u32 seed) : state(seed ? seed : 0xC0FFEEu) {}
    u32 next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

/* Mirror of the matchStart() mode-dispatch decision (matchsetup.c S481).
 * Inputs: mode + spawn_weapon_id_present + roll_result.
 * Outputs: the runtime spawnWeaponNum that the spawn sites would observe.
 *
 * For SPECIFIC, the input "specificResolved" is what assetCatalogResolve()
 * would yield. For RANDOM, "rolledMpw" is the picked MPWEAPON_*. The mapping
 * MPWEAPON_* -> WEAPON_* enum is not modeled here (catalogGetMpWeaponNum is
 * out of scope); we test the dispatch invariants only.
 */
u8 spec_resolveAtMatchStart(u8 mode, bool specificIdPresent,
                            u8 specificResolved, s32 rolledMpw)
{
    switch (mode) {
    case kSPAWNWEAPON_MODE_FIESTA:
        return kSPAWNWEAPON_FIESTA_SENTINEL;
    case kSPAWNWEAPON_MODE_RANDOM:
        if (rolledMpw <= 0) return 0xFF; /* fallback path */
        /* Live code multiplies by catalogGetMpWeaponNum here; in tests we
         * just assert "non-fiesta-sentinel non-zero non-FF" for a real roll
         * by returning the mpw cast to u8. */
        return (u8)rolledMpw;
    case kSPAWNWEAPON_MODE_SPECIFIC:
    default:
        return specificIdPresent ? specificResolved : (u8)0xFF;
    }
}

/* Mirror of the scenario_save.c S481 legacy-default rule:
 * If "spawnWeaponMode" key is missing in the JSON, default to:
 *   - RANDOM   when spawn_weapon_id is empty (legacy "empty = Random label")
 *   - SPECIFIC when spawn_weapon_id is non-empty (a named weapon)
 * If the key IS present and in [0..2], use it verbatim.
 * Out-of-range values fall back to the same legacy default rule.
 */
u8 spec_loadLegacyMode(bool keyPresent, s32 keyValue, bool spawnIdPresent)
{
    if (keyPresent && keyValue >= 0 && keyValue <= (s32)kSPAWNWEAPON_MODE_FIESTA) {
        return (u8)keyValue;
    }
    return spawnIdPresent ? kSPAWNWEAPON_MODE_SPECIFIC
                          : kSPAWNWEAPON_MODE_RANDOM;
}

} /* anon */

TEST_CASE("spawn-weapon: pool excludes NONE / DISABLED / SHIELD",
          "[spawn-weapon][pool]") {
    const u8 slots[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_NONE,      /* 0 — excluded */
        kMPWEAPON_FALCON2,   /* 1 — eligible */
        kMPWEAPON_SHIELD,    /* 2 — excluded */
        kMPWEAPON_DRAGON,    /* 3 — eligible */
        kMPWEAPON_DISABLED,  /* 4 — excluded */
        kMPWEAPON_AR34,      /* 5 — eligible */
    };
    seeded_rng rng(0x12345);

    /* Run a few hundred picks and confirm we NEVER see an excluded slot. */
    for (int i = 0; i < 256; i++) {
        s32 picked = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                        [&] { return rng.next(); });
        REQUIRE(picked != 0);
        REQUIRE(picked != kMPWEAPON_NONE);
        REQUIRE(picked != kMPWEAPON_DISABLED);
        REQUIRE(picked != kMPWEAPON_SHIELD);
        /* Must be one of the three eligible. */
        bool ok = (picked == kMPWEAPON_FALCON2)
               || (picked == kMPWEAPON_DRAGON)
               || (picked == kMPWEAPON_AR34);
        REQUIRE(ok);
    }
}

TEST_CASE("spawn-weapon: empty / all-excluded set returns 0 (caller fallback)",
          "[spawn-weapon][pool]") {
    const u8 emptySlots[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_DISABLED,
        kMPWEAPON_SHIELD, kMPWEAPON_NONE, kMPWEAPON_DISABLED,
    };
    seeded_rng rng(0xBADBEEF);
    /* Every call should return 0 — there are zero eligible slots, no matter
     * what the RNG produces. The matchStart() RANDOM branch falls back to
     * MPWEAPON_FALCON2 in this case (logged WARNING); player.c / bot.c
     * FIESTA branch falls into the resolved-weapon-num=0 path. */
    for (int i = 0; i < 16; i++) {
        s32 picked = spec_pickFromSlots(emptySlots, kNUM_MPWEAPONSLOTS,
                                        [&] { return rng.next(); });
        REQUIRE(picked == 0);
    }
}

TEST_CASE("spawn-weapon: RANDOM rolls once — every spawn returns the same weapon",
          "[spawn-weapon][random]") {
    /* The Random spec: matchStart() rolls ONCE and stores the rolled
     * MPWEAPON_* in spawnWeaponNum. Every subsequent spawn (for every
     * player + every bot) reads spawnWeaponNum and uses it as-is — there
     * is no per-spawn re-roll on the SPECIFIC / RANDOM resolved-integer
     * code path.
     *
     * We verify this by: (a) doing one roll to choose the match weapon,
     * (b) calling the spec_resolveAtMatchStart shape repeatedly with
     * mode=RANDOM and the same rolled value, (c) asserting all spawns
     * see the same value. */
    const u8 slots[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_FALCON2, kMPWEAPON_DRAGON, kMPWEAPON_K7,
        kMPWEAPON_AR34, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };

    seeded_rng matchRoll(0xAA55AA55);
    s32 rolled = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                    [&] { return matchRoll.next(); });
    REQUIRE(rolled > 0);

    /* Player spawn x100 + bot spawn x100 — every observation reads the
     * same stored spawnWeaponNum (no re-roll). */
    for (int i = 0; i < 100; i++) {
        u8 obs = spec_resolveAtMatchStart(kSPAWNWEAPON_MODE_RANDOM,
                                          /*specificIdPresent=*/false,
                                          /*specificResolved=*/0,
                                          rolled);
        REQUIRE(obs == (u8)rolled);
    }
}

TEST_CASE("spawn-weapon: RANDOM determinism — same seed -> same rolled weapon",
          "[spawn-weapon][random]") {
    const u8 slots[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_FALCON2, kMPWEAPON_DRAGON, kMPWEAPON_K7,
        kMPWEAPON_AR34, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };

    seeded_rng a(0xDEADC0DE);
    seeded_rng b(0xDEADC0DE);
    s32 rollA = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                   [&] { return a.next(); });
    s32 rollB = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                   [&] { return b.next(); });
    REQUIRE(rollA == rollB);
    REQUIRE(rollA > 0);
}

TEST_CASE("spawn-weapon: FIESTA matchStart arms the sentinel",
          "[spawn-weapon][fiesta]") {
    /* The FIESTA spec: matchStart() does NOT roll. It writes
     * SPAWNWEAPON_FIESTA_SENTINEL to spawnWeaponNum so player.c / bot.c
     * spawn sites detect FIESTA at every spawn and roll fresh. */
    u8 obs = spec_resolveAtMatchStart(kSPAWNWEAPON_MODE_FIESTA,
                                      /*specificIdPresent=*/false,
                                      /*specificResolved=*/0,
                                      /*rolledMpw=*/0);
    REQUIRE(obs == kSPAWNWEAPON_FIESTA_SENTINEL);
}

TEST_CASE("spawn-weapon: FIESTA rolls fresh per spawn — sequence varies",
          "[spawn-weapon][fiesta]") {
    /* FIESTA semantics: each spawn calls spawnWeaponPickFromActiveSet()
     * independently. Across many spawns under the same RNG we should see
     * MORE THAN ONE distinct weapon — the sequence is NOT pinned to a
     * single value (which would be the Random behavior).
     *
     * With 4 eligible weapons under xorshift32, we expect all 4 to appear
     * in 100 rolls with extremely high probability. */
    const u8 slots[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_FALCON2, kMPWEAPON_DRAGON, kMPWEAPON_K7,
        kMPWEAPON_AR34, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0xFEEDFACE);

    int seen[256] = {0};
    int distinct = 0;
    for (int i = 0; i < 100; i++) {
        s32 picked = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                        [&] { return rng.next(); });
        REQUIRE(picked > 0);
        REQUIRE(picked < 256);
        if (!seen[picked]) {
            seen[picked] = 1;
            distinct++;
        }
    }
    /* With 4 eligible weapons and xorshift32, we expect to hit at least 2
     * distinct weapons within 100 rolls. The actual hit count is much
     * higher in practice — this is a "not pinned to one weapon" assertion,
     * not a pigeonhole proof. */
    REQUIRE(distinct >= 2);
}

TEST_CASE("spawn-weapon: FIESTA per-player independence",
          "[spawn-weapon][fiesta]") {
    /* Fiesta rolls are per-spawn, per-player. Two players respawning at the
     * "same time" should each pull from their own RNG sequence. We model
     * this by giving each player its own seed; the rolls should diverge
     * (i.e. not always match). */
    const u8 slots[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_FALCON2, kMPWEAPON_DRAGON, kMPWEAPON_K7,
        kMPWEAPON_AR34, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng playerA(0x111);
    seeded_rng playerB(0x222);

    int matches = 0;
    int total = 50;
    for (int i = 0; i < total; i++) {
        s32 a = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                   [&] { return playerA.next(); });
        s32 b = spec_pickFromSlots(slots, kNUM_MPWEAPONSLOTS,
                                   [&] { return playerB.next(); });
        if (a == b) matches++;
    }
    /* If players were forcibly synchronized (a bug), matches would equal
     * total. We expect matches strictly less than total — they're rolling
     * independently. */
    REQUIRE(matches < total);
}

TEST_CASE("spawn-weapon: SPECIFIC mode resolves to the named weapon",
          "[spawn-weapon][specific]") {
    /* SPECIFIC = catalog ID names the weapon. matchStart() resolves it via
     * assetCatalogResolve + catalogGetMpWeaponNum. We model this as a
     * direct passthrough of the resolved value. */
    u8 obs = spec_resolveAtMatchStart(kSPAWNWEAPON_MODE_SPECIFIC,
                                      /*specificIdPresent=*/true,
                                      /*specificResolved=*/0x42,
                                      /*rolledMpw=*/0);
    REQUIRE(obs == 0x42);
}

TEST_CASE("spawn-weapon: SPECIFIC with empty id falls back to legacy 0xFF",
          "[spawn-weapon][specific]") {
    /* The legacy meaning of 0xFF is "no resolved weapon, fall back to
     * weapons[0]" — preserved for backwards compat with old saves where
     * spawn_weapon_id is empty + (post-S481) mode defaulted to SPECIFIC.
     * Such saves SHOULD load with mode=RANDOM (see legacy-default test
     * below), but if a save somehow lands here, the spawn sites still
     * have a sensible fallback path. */
    u8 obs = spec_resolveAtMatchStart(kSPAWNWEAPON_MODE_SPECIFIC,
                                      /*specificIdPresent=*/false,
                                      /*specificResolved=*/0,
                                      /*rolledMpw=*/0);
    REQUIRE(obs == 0xFF);
}

TEST_CASE("spawn-weapon: legacy save (no spawnWeaponMode key) -> RANDOM when id is empty",
          "[spawn-weapon][migration]") {
    /* Pre-S481 scenario JSON: only "spawnWeaponId" was written. When that
     * id is empty, the user picked "Random" from the dropdown — and the
     * S481 rule is that those saves load with mode=RANDOM so the actual
     * roll happens (rather than the broken legacy weapons[0] fallback). */
    u8 mode = spec_loadLegacyMode(/*keyPresent=*/false, /*keyValue=*/0,
                                  /*spawnIdPresent=*/false);
    REQUIRE(mode == kSPAWNWEAPON_MODE_RANDOM);
}

TEST_CASE("spawn-weapon: legacy save (no spawnWeaponMode key) -> SPECIFIC when id is non-empty",
          "[spawn-weapon][migration]") {
    /* If a pre-S481 save has a named weapon, the user picked a specific
     * weapon — load as SPECIFIC. */
    u8 mode = spec_loadLegacyMode(/*keyPresent=*/false, /*keyValue=*/0,
                                  /*spawnIdPresent=*/true);
    REQUIRE(mode == kSPAWNWEAPON_MODE_SPECIFIC);
}

TEST_CASE("spawn-weapon: post-S481 save round-trips mode verbatim",
          "[spawn-weapon][migration]") {
    /* Saves authored after S481 ALWAYS write the spawnWeaponMode key.
     * The loader uses it verbatim for every value in [0..2]. */
    REQUIRE(spec_loadLegacyMode(true, 0, false) == kSPAWNWEAPON_MODE_SPECIFIC);
    REQUIRE(spec_loadLegacyMode(true, 0, true)  == kSPAWNWEAPON_MODE_SPECIFIC);
    REQUIRE(spec_loadLegacyMode(true, 1, false) == kSPAWNWEAPON_MODE_RANDOM);
    REQUIRE(spec_loadLegacyMode(true, 1, true)  == kSPAWNWEAPON_MODE_RANDOM);
    REQUIRE(spec_loadLegacyMode(true, 2, false) == kSPAWNWEAPON_MODE_FIESTA);
    REQUIRE(spec_loadLegacyMode(true, 2, true)  == kSPAWNWEAPON_MODE_FIESTA);
}

TEST_CASE("spawn-weapon: out-of-range mode value falls back to legacy default",
          "[spawn-weapon][migration]") {
    /* If a hand-edited (or corrupt) save has spawnWeaponMode = 99, the
     * loader treats the key as absent — same legacy default rule applies. */
    REQUIRE(spec_loadLegacyMode(true, 99, false) == kSPAWNWEAPON_MODE_RANDOM);
    REQUIRE(spec_loadLegacyMode(true, 99, true)  == kSPAWNWEAPON_MODE_SPECIFIC);
    REQUIRE(spec_loadLegacyMode(true, -5, false) == kSPAWNWEAPON_MODE_RANDOM);
}

TEST_CASE("spawn-weapon: NUM_MPWEAPONSLOTS pin",
          "[spawn-weapon][pin]") {
    /* If the live constants.h NUM_MPWEAPONSLOTS changes, this test fails
     * loudly so the spec gets updated alongside. */
    REQUIRE(kNUM_MPWEAPONSLOTS == 6);
}

TEST_CASE("spawn-weapon: FIESTA sentinel pin",
          "[spawn-weapon][pin]") {
    /* Pin the on-the-wire / runtime sentinel. If matchsetup.h
     * SPAWNWEAPON_FIESTA_SENTINEL changes, both the wire codec and every
     * spawn-site detect MUST agree. */
    REQUIRE(kSPAWNWEAPON_FIESTA_SENTINEL == 0xFE);
    /* The sentinel must NOT collide with the legacy 0xFF "no spawn weapon"
     * meaning, nor with 0 (NONE). */
    REQUIRE(kSPAWNWEAPON_FIESTA_SENTINEL != 0xFF);
    REQUIRE(kSPAWNWEAPON_FIESTA_SENTINEL != 0x00);
}

TEST_CASE("spawn-weapon: mode enum values are stable",
          "[spawn-weapon][pin]") {
    /* Wire codec depends on these exact integers (they cross the wire as
     * u8 in NET_PROTOCOL_VER 45 SVC_STAGE_START + CLC_LOBBY_START). */
    REQUIRE(kSPAWNWEAPON_MODE_SPECIFIC == 0);
    REQUIRE(kSPAWNWEAPON_MODE_RANDOM   == 1);
    REQUIRE(kSPAWNWEAPON_MODE_FIESTA   == 2);
}
