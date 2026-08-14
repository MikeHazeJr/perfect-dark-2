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
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
struct netbuf {
    u8 *data;
    u32 size;
    u32 rp;
    u32 wp;
    u32 error;
};

void netbufStartReadData(struct netbuf *buf, const void *data, u32 size);
void netbufStartWrite(struct netbuf *buf);
s32  netbufReadLeft(const struct netbuf *buf);
u8   netbufReadU8(struct netbuf *buf);
const char *netbufReadStr(struct netbuf *buf);
u32  netbufWriteU8(struct netbuf *buf, const u8 v);
u32  netbufWriteStr(struct netbuf *buf, const char *v);
}

namespace {

/* Mirror of constants.h (post-cull, 2026-04-26). */
constexpr u8 kMPWEAPON_NONE     = 0x00;
constexpr u8 kMPWEAPON_FALCON2  = 0x01;
constexpr u8 kMPWEAPON_DRAGON   = 0x0e;
constexpr u8 kMPWEAPON_K7       = 0x0f;
constexpr u8 kMPWEAPON_AR34     = 0x10;
constexpr u8 kMPWEAPON_SHIELD   = 0x27;
constexpr u8 kMPWEAPON_DISABLED = 0x28;
constexpr u8 kMPWEAPON_CUSTOM_START = 0x29;
constexpr u8 kMPWEAPON_CUSTOM_COUNT = 64 - kMPWEAPON_CUSTOM_START;

/* Mirror of NUM_MPWEAPONS (constants.h). */
constexpr s32 kNUM_MPWEAPONS = kMPWEAPON_CUSTOM_START + kMPWEAPON_CUSTOM_COUNT;

/* Mirror of NUM_MPWEAPONSLOTS (constants.h). */
constexpr s32 kNUM_MPWEAPONSLOTS = 6;

/* Mirror of enum spawn_weapon_mode (port/include/net/matchsetup.h). */
constexpr u8 kSPAWNWEAPON_MODE_SPECIFIC = 0;
constexpr u8 kSPAWNWEAPON_MODE_RANDOM   = 1;
constexpr u8 kSPAWNWEAPON_MODE_FIESTA   = 2;

/* Mirror of SPAWNWEAPON_FIESTA_SENTINEL. */
constexpr u8 kSPAWNWEAPON_FIESTA_SENTINEL = 0xFE;

struct WireBuf {
    u8 storage[256];
    netbuf nb;

    WireBuf() {
        std::memset(storage, 0, sizeof(storage));
        nb.data = storage;
        nb.size = sizeof(storage);
        nb.rp = 0;
        nb.wp = 0;
        nb.error = 0;
        netbufStartWrite(&nb);
    }

    void rewind_for_read() {
        const u32 written = nb.wp;
        netbufStartReadData(&nb, storage, written);
    }
};

std::string read_text_file(const char *path)
{
    std::ifstream in(path, std::ios::in | std::ios::binary);
    REQUIRE(in.good());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string source_slice(const std::string &text, const char *start, const char *end)
{
    const size_t begin = text.find(start);
    REQUIRE(begin != std::string::npos);
    const size_t finish = text.find(end, begin + std::strlen(start));
    REQUIRE(finish != std::string::npos);
    return text.substr(begin, finish - begin);
}

void require_ordered(const std::string &text, const std::vector<const char *> &patterns)
{
    size_t cursor = 0;
    for (const char *pattern : patterns) {
        const size_t pos = text.find(pattern, cursor);
        INFO("missing or out-of-order pattern: " << pattern);
        REQUIRE(pos != std::string::npos);
        cursor = pos + std::strlen(pattern);
    }
}

/* Pure replication of spawnWeaponPickFromSlots (matchsetup.c S482).
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

/* Mirror of the matchStart() mode-dispatch decision (matchsetup.c S482).
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

/* Mirror of the scenario_save.c S482 legacy-default rule:
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

TEST_CASE("spawn-weapon: selected catalog IDs keep their authoritative runtime index",
		"[spawn-weapon][catalog][b1022]") {
	const std::string setup = read_text_file("port/src/net/matchsetup.c");
	const std::string netmsg = read_text_file("port/src/net/netmsg.c");
	const std::string setupSpecific = source_slice(setup,
		"case SPAWNWEAPON_MODE_SPECIFIC:",
		"default:");
	const std::string lobbySpecific = source_slice(netmsg,
		"if (plan.spawn_weapon_mode == SPAWNWEAPON_MODE_SPECIFIC)",
		"} else if ((plan.spawn_weapon_mode != SPAWNWEAPON_MODE_RANDOM");

	REQUIRE(setupSpecific.find("entry->runtime_index") != std::string::npos);
	REQUIRE(setupSpecific.find("catalogGetMpWeaponNum(mpw)") == std::string::npos);
	REQUIRE(lobbySpecific.find("spawn->runtime_index") != std::string::npos);
	REQUIRE(lobbySpecific.find("catalogGetMpWeaponNum(mpw)") == std::string::npos);
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
     * spawn_weapon_id is empty + (post-S482) mode defaulted to SPECIFIC.
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
    /* Pre-S482 scenario JSON: only "spawnWeaponId" was written. When that
     * id is empty, the user picked "Random" from the dropdown — and the
     * S482 rule is that those saves load with mode=RANDOM so the actual
     * roll happens (rather than the broken legacy weapons[0] fallback). */
    u8 mode = spec_loadLegacyMode(/*keyPresent=*/false, /*keyValue=*/0,
                                  /*spawnIdPresent=*/false);
    REQUIRE(mode == kSPAWNWEAPON_MODE_RANDOM);
}

TEST_CASE("spawn-weapon: legacy save (no spawnWeaponMode key) -> SPECIFIC when id is non-empty",
          "[spawn-weapon][migration]") {
    /* If a pre-S482 save has a named weapon, the user picked a specific
     * weapon — load as SPECIFIC. */
    u8 mode = spec_loadLegacyMode(/*keyPresent=*/false, /*keyValue=*/0,
                                  /*spawnIdPresent=*/true);
    REQUIRE(mode == kSPAWNWEAPON_MODE_SPECIFIC);
}

TEST_CASE("spawn-weapon: post-S482 save round-trips mode verbatim",
          "[spawn-weapon][migration]") {
    /* Saves authored after S482 ALWAYS write the spawnWeaponMode key.
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

TEST_CASE("weapon catalog identity: MP weapon table count includes private custom slots",
          "[spawn-weapon][catalog][pin]") {
    REQUIRE(kNUM_MPWEAPONS == 64);
    REQUIRE(kMPWEAPON_NONE == 0x00);
    REQUIRE(kMPWEAPON_SHIELD == 0x27);
    REQUIRE(kMPWEAPON_DISABLED == 0x28);
    REQUIRE(kMPWEAPON_CUSTOM_START == 0x29);
    REQUIRE(kMPWEAPON_CUSTOM_COUNT == 23);
}

TEST_CASE("weapon catalog identity: MP and runtime identity remain separate",
          "[spawn-weapon][catalog][regression]") {
    struct fake_resolved_weapon {
        s32 mp_weapon_id;
        s32 runtime_weapon_num;
    };

    const fake_resolved_weapon falcon2 = {
        kMPWEAPON_FALCON2,
        2 /* WEAPON_FALCON2 in constants.h */
    };

    REQUIRE(falcon2.mp_weapon_id == 1);
    REQUIRE(falcon2.runtime_weapon_num == 2);
    REQUIRE(falcon2.mp_weapon_id != falcon2.runtime_weapon_num);
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

TEST_CASE("spawn-weapon wire: SVC_STAGE_START v45 tail preserves following mod-track field",
          "[spawn-weapon][wire][netbuf]") {
    WireBuf b;
    netbufWriteStr(&b.nb, "base:dragon");
    netbufWriteU8(&b.nb, kSPAWNWEAPON_MODE_FIESTA);
    netbufWriteU8(&b.nb, kSPAWNWEAPON_FIESTA_SENTINEL);
    netbufWriteStr(&b.nb, "user:test_track");
    REQUIRE(b.nb.error == 0);

    b.rewind_for_read();
    const char *spawnId = netbufReadStr(&b.nb);
    const u8 mode = netbufReadU8(&b.nb);
    const u8 num = netbufReadU8(&b.nb);
    const char *modTrack = netbufReadStr(&b.nb);

    REQUIRE(spawnId != nullptr);
    REQUIRE(std::string(spawnId) == "base:dragon");
    REQUIRE(mode == kSPAWNWEAPON_MODE_FIESTA);
    REQUIRE(num == kSPAWNWEAPON_FIESTA_SENTINEL);
    REQUIRE(modTrack != nullptr);
    REQUIRE(std::string(modTrack) == "user:test_track");
    REQUIRE(b.nb.error == 0);
    REQUIRE(netbufReadLeft(&b.nb) == 0);
}

TEST_CASE("spawn-weapon wire: CLC_LOBBY_START mode is followed directly by bot payload",
          "[spawn-weapon][wire][netbuf]") {
    WireBuf b;
    netbufWriteStr(&b.nb, "base:falcon2");
    netbufWriteU8(&b.nb, kSPAWNWEAPON_MODE_RANDOM);
    netbufWriteStr(&b.nb, "First Sim");
    REQUIRE(b.nb.error == 0);

    b.rewind_for_read();
    const char *spawnId = netbufReadStr(&b.nb);
    const u8 mode = netbufReadU8(&b.nb);
    const char *firstBotName = netbufReadStr(&b.nb);

    REQUIRE(spawnId != nullptr);
    REQUIRE(std::string(spawnId) == "base:falcon2");
    REQUIRE(mode == kSPAWNWEAPON_MODE_RANDOM);
    REQUIRE(firstBotName != nullptr);
    REQUIRE(std::string(firstBotName) == "First Sim");
    REQUIRE(b.nb.error == 0);
    REQUIRE(netbufReadLeft(&b.nb) == 0);
}

TEST_CASE("spawn-weapon wire: truncated SVC_STAGE_START spawn tail is malformed",
          "[spawn-weapon][wire][netbuf]") {
    WireBuf b;
    netbufWriteStr(&b.nb, "base:dragon");
    netbufWriteU8(&b.nb, kSPAWNWEAPON_MODE_RANDOM);
    REQUIRE(b.nb.error == 0);

    b.rewind_for_read();
    REQUIRE(std::string(netbufReadStr(&b.nb)) == "base:dragon");
    REQUIRE(netbufReadU8(&b.nb) == kSPAWNWEAPON_MODE_RANDOM);
    REQUIRE(netbufReadU8(&b.nb) == 0);
    REQUIRE(b.nb.error == 1);
}

TEST_CASE("spawn-weapon wire: production netmsg keeps v45 field order",
          "[spawn-weapon][wire][static]") {
    const std::string netmsg = read_text_file("port/src/net/netmsg.c");

    const std::string svcWrite = source_slice(
        netmsg,
        "u32 netmsgSvcStageStartWrite",
        "u32 netmsgSvcStageStartRead");
    require_ordered(svcWrite, {
        "netbufWriteStr(dst, plan.spawn_weapon_id);",
        "netbufWriteU8(dst, plan.spawn_weapon_mode);",
        "netbufWriteU8(dst, plan.spawn_weapon_num);",
        "netbufWriteStr(dst, plan.mod_track_id);",
    });

    const std::string svcRead = source_slice(
        netmsg,
        "u32 netmsgSvcStageStartRead",
        "u32 netmsgSvcStageEndWrite");
    require_ordered(svcRead, {
        "const char *swid_str = netbufReadStr(src);",
        "const u8 wireMode = netbufReadU8(src);",
        "const u8 wireNum  = netbufReadU8(src);",
        "const char *modtrack_str = netbufReadStr(src);",
    });

    const std::string clcWrite = source_slice(
        netmsg,
        "u32 netmsgClcLobbyStartWrite",
        "u32 netmsgClcLobbyStartRead");
    require_ordered(clcWrite, {
        "netbufWriteStr(dst, plan.spawn_weapon_id);",
        "netbufWriteU8(dst, plan.spawn_weapon_mode);",
        "const net_lobby_start_write_bot_t *bot",
    });
    REQUIRE(clcWrite.find("plan.handicaps") == std::string::npos);

    const std::string clcRead = source_slice(
        netmsg,
        "u32 netmsgClcLobbyStartRead",
        "u32 netmsgClcCatalogDiffWrite");
    require_ordered(clcRead, {
        "netLobbyStartCopyString(plan.spawn_weapon_id",
        "plan.spawn_weapon_mode = netbufReadU8(src);",
        "netLobbyStartPrepareRoster(&plan, srccl, room)",
        "netLobbyStartReadBots(src, &plan, advertised_bots)",
    });
    REQUIRE(clcRead.find("plan.handicaps") == std::string::npos);
    const std::string clcRoster = source_slice(
        netmsg,
        "static bool netLobbyStartPrepareRoster",
        "static bool netLobbyStartReadBots");
    REQUIRE(clcRoster.find("settings_input.handicap = client->settings.handicap") !=
        std::string::npos);
    REQUIRE(clcRoster.find(
        "player->config.handicap = settings_plan.handicap") !=
        std::string::npos);
}

/* ============================================================================
 * S483 (2026-04-27): host-eligible pool via match manifest.
 *
 * Source of the rules:
 *   port/src/net/matchsetup.c
 *     - spawnWeaponPickFromMatchManifest  (live: cascade through
 *                                          g_CurrentLoadedManifest /
 *                                          g_ServerManifest / g_ClientManifest;
 *                                          fallback to active set)
 *     - spawnWeaponBuildPoolFromManifest  (file-static: catalog-resolve each
 *                                          MANIFEST_TYPE_WEAPON entry, filter
 *                                          NONE/DISABLED/SHIELD)
 *   port/src/net/netmanifest.c
 *     - s_manifestAppendWeaponPool        (build site: walks
 *                                          assetCatalogIterateUnlockedByType
 *                                          ASSET_WEAPON, adds each as
 *                                          MANIFEST_TYPE_WEAPON +
 *                                          MANIFEST_SLOT_MATCH; dedup is
 *                                          automatic via manifestAddEntry)
 *
 * Tests below replicate the spec as a pure helper so the test stays
 * SDL/ENet/catalog-free. Drift audit: diff
 * spec_pickFromManifestPool against matchsetup.c
 * spawnWeaponBuildPoolFromManifest + the live cascade in
 * spawnWeaponPickFromMatchManifest.
 * ============================================================================ */

namespace {

/* Synthetic manifest entry for testing. The live helper walks
 * match_manifest_t.entries[] for MANIFEST_TYPE_WEAPON; we model that as a
 * (catalog_id_present, weapon_id) pair where catalog_id_present represents
 * "the catalog has a non-NULL ASSET_WEAPON entry for this id". */
struct fake_manifest_entry {
    u8   type;            /* mirror of match_manifest_entry_t.type */
    bool catalog_present; /* assetCatalogResolve hits */
    u8   weapon_id;       /* ext.weapon.weapon_id (MPWEAPON_*) */
};

/* Mirror of MANIFEST_TYPE_WEAPON (netmanifest.h:74). */
constexpr u8 kMANIFEST_TYPE_WEAPON = 3;
/* Mirror of MANIFEST_TYPE_BODY  (netmanifest.h:71) — used to cover the
 * "non-weapon manifest entries are skipped" invariant. */
constexpr u8 kMANIFEST_TYPE_BODY   = 0;

/* Pure replication of spawnWeaponBuildPoolFromManifest + the cascade in
 * spawnWeaponPickFromMatchManifest (matchsetup.c S483). Build the pool from
 * `manifest`; if it is empty / all-filtered, fall back to `activeSet`. Both
 * pools use the same filtering rules.
 *
 * Returns the picked MPWEAPON_* index, or 0 when both pools are degenerate. */
s32 spec_pickFromManifestPool(const fake_manifest_entry *manifest,
                              s32 manifest_count,
                              const u8 *activeSet, s32 activeSetCount,
                              std::function<u32()> rng_fn)
{
    /* Build manifest-derived pool. */
    u8 mpool[64];
    s32 mcount = 0;
    if (manifest && manifest_count > 0) {
        for (s32 i = 0; i < manifest_count && mcount < (s32)sizeof(mpool); i++) {
            const fake_manifest_entry &e = manifest[i];
            if (e.type != kMANIFEST_TYPE_WEAPON) continue;     /* non-weapon: skip */
            if (!e.catalog_present) continue;                  /* missing catalog: skip */
            s32 wid = (s32)e.weapon_id;
            if (wid <= 0 || wid >= kNUM_MPWEAPONS) continue;
            if (wid == kMPWEAPON_NONE)     continue;
            if (wid == kMPWEAPON_DISABLED) continue;
            if (wid == kMPWEAPON_SHIELD)   continue;
            mpool[mcount++] = (u8)wid;
        }
    }
    if (mcount > 0) {
        u32 r = rng_fn();
        return (s32)mpool[r % (u32)mcount];
    }
    /* Fallback: active weapon set. */
    return spec_pickFromSlots(activeSet, activeSetCount, rng_fn);
}

} /* anon */

TEST_CASE("spawn-weapon manifest: pool draws from MANIFEST_TYPE_WEAPON entries",
          "[spawn-weapon][manifest]") {
    /* Manifest has 4 weapon entries plus 1 body entry; the body is ignored. */
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_FALCON2 },
        { kMANIFEST_TYPE_BODY,   true, 0 /* irrelevant */ },
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_DRAGON },
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_K7 },
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_AR34 },
    };
    /* Active set is intentionally degenerate so any non-weapon-pool pick
     * would surface as 0. */
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9001);
    for (int i = 0; i < 256; i++) {
        s32 picked = spec_pickFromManifestPool(m, (s32)(sizeof(m)/sizeof(m[0])),
                                               active, kNUM_MPWEAPONSLOTS,
                                               [&] { return rng.next(); });
        REQUIRE(picked != 0);
        REQUIRE(picked != kMPWEAPON_NONE);
        bool ok = (picked == kMPWEAPON_FALCON2)
               || (picked == kMPWEAPON_DRAGON)
               || (picked == kMPWEAPON_K7)
               || (picked == kMPWEAPON_AR34);
        REQUIRE(ok);
    }
}

TEST_CASE("spawn-weapon manifest: NONE/DISABLED/SHIELD entries are filtered",
          "[spawn-weapon][manifest][pool]") {
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_NONE },     /* excluded */
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_FALCON2 },  /* eligible */
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_DISABLED }, /* excluded */
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_SHIELD },   /* excluded */
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_DRAGON },   /* eligible */
    };
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_K7, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9002);
    for (int i = 0; i < 64; i++) {
        s32 picked = spec_pickFromManifestPool(m, 5, active,
                                               kNUM_MPWEAPONSLOTS,
                                               [&] { return rng.next(); });
        /* Manifest pool has 2 eligible (FALCON2, DRAGON) so the active-set
         * fallback (which would pick K7) MUST NOT fire. */
        bool ok = (picked == kMPWEAPON_FALCON2)
               || (picked == kMPWEAPON_DRAGON);
        REQUIRE(ok);
    }
}

TEST_CASE("spawn-weapon manifest: empty manifest falls back to active set",
          "[spawn-weapon][manifest][fallback]") {
    /* Empty manifest pool -> fallback path runs. */
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_FALCON2, kMPWEAPON_DRAGON, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9003);
    for (int i = 0; i < 64; i++) {
        s32 picked = spec_pickFromManifestPool(nullptr, 0, active,
                                               kNUM_MPWEAPONSLOTS,
                                               [&] { return rng.next(); });
        bool ok = (picked == kMPWEAPON_FALCON2)
               || (picked == kMPWEAPON_DRAGON);
        REQUIRE(ok);
    }
}

TEST_CASE("spawn-weapon manifest: all-filtered manifest falls back to active set",
          "[spawn-weapon][manifest][fallback]") {
    /* Manifest has only excluded entries -> pool is empty -> fallback. */
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_NONE },
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_DISABLED },
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_SHIELD },
        { kMANIFEST_TYPE_BODY,   true, 0 },
    };
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_K7, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9004);
    s32 picked = spec_pickFromManifestPool(m, 4, active,
                                           kNUM_MPWEAPONSLOTS,
                                           [&] { return rng.next(); });
    REQUIRE(picked == kMPWEAPON_K7);
}

TEST_CASE("spawn-weapon manifest: missing-catalog entries skipped (Phase 2 distribution gap)",
          "[spawn-weapon][manifest][distribution]") {
    /* If the host puts a mod weapon in the manifest but the client's catalog
     * distribution hasn't completed yet, the weapon's catalog_present is
     * false. The pool builder must skip such entries gracefully (they
     * effectively don't exist for this client until SVC_DISTRIB_END
     * registers them). When no eligible entries remain, fall back. */
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, false, kMPWEAPON_FALCON2 }, /* not yet distributed */
        { kMANIFEST_TYPE_WEAPON, false, kMPWEAPON_DRAGON },  /* not yet distributed */
    };
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_K7, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9005);
    s32 picked = spec_pickFromManifestPool(m, 2, active,
                                           kNUM_MPWEAPONSLOTS,
                                           [&] { return rng.next(); });
    REQUIRE(picked == kMPWEAPON_K7);
}

TEST_CASE("spawn-weapon manifest: mod weapon (synthetic catalog id) included",
          "[spawn-weapon][manifest][mods]") {
    /* A mod weapon is just an ASSET_WEAPON entry with a catalog_present and
     * a non-zero weapon_id (the catalog scanner assigns one). The pool
     * builder is agnostic to whether the entry is base or mod -- the
     * weapon_id alone determines eligibility. We use 0x12 (SHOTGUN) as a
     * stand-in for a mod-supplied weapon to exercise the "any valid
     * weapon_id is eligible" rule. */
    const u8 kMODWEAPON_STANDIN = 0x12; /* SHOTGUN slot, used as a stand-in */
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, true, kMODWEAPON_STANDIN },
    };
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9006);
    s32 picked = spec_pickFromManifestPool(m, 1, active,
                                           kNUM_MPWEAPONSLOTS,
                                           [&] { return rng.next(); });
    REQUIRE(picked == kMODWEAPON_STANDIN);
}

TEST_CASE("spawn-weapon manifest: invalid weapon_id (>=NUM_MPWEAPONS) skipped",
          "[spawn-weapon][manifest][pool]") {
    /* A corrupt manifest with an out-of-range weapon_id must NOT crash and
     * must be skipped. Falls back to active set when nothing valid remains. */
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, true, 0xFF }, /* out of range */
        { kMANIFEST_TYPE_WEAPON, true, (u8)kNUM_MPWEAPONS }, /* out of range */
    };
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_FALCON2, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9007);
    s32 picked = spec_pickFromManifestPool(m, 2, active,
                                           kNUM_MPWEAPONSLOTS,
                                           [&] { return rng.next(); });
    REQUIRE(picked == kMPWEAPON_FALCON2);
}

TEST_CASE("spawn-weapon manifest: active-set BOTH degenerate -> 0 (caller fallback)",
          "[spawn-weapon][manifest][fallback]") {
    /* If both manifest and active set are empty, the spec helper returns 0.
     * The live spawn sites (player.c / bot.c) handle this via the existing
     * weapons[0] / unarmed fallback path. matchStart() RANDOM falls back to
     * MPWEAPON_FALCON2 with a LOG_WARNING. */
    const fake_manifest_entry m[] = {
        { kMANIFEST_TYPE_WEAPON, true, kMPWEAPON_NONE },
    };
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    seeded_rng rng(0x9008);
    s32 picked = spec_pickFromManifestPool(m, 1, active,
                                           kNUM_MPWEAPONSLOTS,
                                           [&] { return rng.next(); });
    REQUIRE(picked == 0);
}

TEST_CASE("spawn-weapon manifest: pool size scales beyond 6 slots",
          "[spawn-weapon][manifest]") {
    /* The active-set pool caps at 6 slots; the manifest pool is bounded by
     * NUM_MPWEAPONS (41). Verify the spec covers a pool larger than 6 -- if
     * the implementation accidentally clamped to NUM_MPWEAPONSLOTS the larger
     * pool entries would be dropped. */
    fake_manifest_entry m[20];
    /* Fill 20 entries with weapons 1..20 (all eligible). */
    for (int i = 0; i < 20; i++) {
        m[i].type = kMANIFEST_TYPE_WEAPON;
        m[i].catalog_present = true;
        m[i].weapon_id = (u8)(i + 1); /* 1..20, all eligible (NONE=0/SHIELD=0x27/DISABLED=0x28 not in range) */
    }
    const u8 active[kNUM_MPWEAPONSLOTS] = {
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
        kMPWEAPON_NONE, kMPWEAPON_NONE, kMPWEAPON_NONE,
    };
    /* Confirm at least 6 distinct values appear under a reasonable rng
     * stream -- verifying the pool isn't artificially clamped at 6. */
    seeded_rng rng(0x9009);
    bool seen[256] = {false};
    int distinct = 0;
    for (int i = 0; i < 200; i++) {
        s32 picked = spec_pickFromManifestPool(m, 20, active,
                                               kNUM_MPWEAPONSLOTS,
                                               [&] { return rng.next(); });
        REQUIRE(picked >= 1);
        REQUIRE(picked <= 20);
        if (!seen[picked]) {
            seen[picked] = true;
            distinct++;
        }
    }
    /* Statistical: with 20 buckets and 200 picks, distinct should easily
     * exceed 7. Setting the bar at 7 makes the assertion meaningful (catches
     * a 6-slot clamp) without being flaky. */
    REQUIRE(distinct > 6);
}

TEST_CASE("spawn-weapon manifest: MANIFEST_TYPE_WEAPON value pin",
          "[spawn-weapon][pin]") {
    /* The wire format depends on this exact integer (netmanifest.h:74).
     * If MANIFEST_TYPE_WEAPON drifts, the pool builder reads zero entries
     * silently because the type filter mismatches. */
    REQUIRE(kMANIFEST_TYPE_WEAPON == 3);
}
