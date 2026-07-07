/*
 * tests/test_arenapool.cpp -- growable slot pool over memarena (task #39).
 *
 * Source under test: port/src/arenapool.c
 *
 * arenapool is the uniform shape the many fixed-size stage pools (projectiles,
 * embedments, debris, weapon/ammo/hat slots, explosions) convert to. The
 * invariants that matter: the base is stable across grow AND across a fresh-stage
 * re-setup, grow reports the first new index and advances the count, and growth
 * past the reservation fails without corrupting the pool.
 */
#include "catch.hpp"

#include <cstring>

extern "C" {
#include "arenapool.h"
}

namespace {
struct slot {
	int flags;
	int payload;
};
constexpr int FREE = 1;
} // namespace

TEST_CASE("arenapool: setup commits count and returns a base", "[arenapool][pool][c120]") {
	arenapool p;
	std::memset(&p, 0, sizeof(p));

	slot *base = static_cast<slot *>(arenaPoolSetup(&p, "t", sizeof(slot), 1u << 20, 64, 50));
	REQUIRE(base != nullptr);
	REQUIRE(p.count == 50);

	for (int i = 0; i < 50; i++) base[i].flags = FREE;

	arenaPoolReset(&p);
	REQUIRE(p.count == 0);
}

TEST_CASE("arenapool: grow keeps base, reports first-new, advances count", "[arenapool][pool][c120]") {
	arenapool p;
	std::memset(&p, 0, sizeof(p));

	slot *base = static_cast<slot *>(arenaPoolSetup(&p, "grow", sizeof(slot), 1u << 20, 64, 200));
	REQUIRE(base != nullptr);
	for (int i = 0; i < 200; i++) { base[i].flags = FREE; base[i].payload = i; }

	// Hold an interior pointer, then grow and confirm it survives.
	slot *held = &base[123];
	held->payload = 0xABCD;

	s32 first = -999;
	slot *base2 = static_cast<slot *>(arenaPoolGrow(&p, &first));
	REQUIRE(base2 == base);           // base stable
	REQUIRE(first == 200);            // first new slot is the old count
	REQUIRE(p.count == 264);          // grew by chunk 64
	REQUIRE(held->payload == 0xABCD); // interior pointer survived
	REQUIRE(base[123].payload == 0xABCD);

	// New slots [200,264) are usable; init + use them.
	for (int i = first; i < p.count; i++) base2[i].flags = FREE;
	REQUIRE(base2[263].flags == FREE);

	arenaPoolReset(&p);
}

TEST_CASE("arenapool: fresh-stage re-setup reuses the same stable base", "[arenapool][pool][c120]") {
	arenapool p;
	std::memset(&p, 0, sizeof(p));

	slot *a = static_cast<slot *>(arenaPoolSetup(&p, "restage", sizeof(slot), 1u << 20, 64, 200));
	REQUIRE(a != nullptr);
	// Grow this "stage" so committed capacity exceeds the next stage's request.
	s32 first;
	arenaPoolGrow(&p, &first);
	REQUIRE(p.count == 264);

	// Next stage: smaller logical count, same reserved base (idempotent reserve).
	slot *b = static_cast<slot *>(arenaPoolSetup(&p, "restage", sizeof(slot), 1u << 20, 64, 200));
	REQUIRE(b == a);        // base did not move
	REQUIRE(p.count == 200); // logical count reset to the new stage's size
}

TEST_CASE("arenapool: grow past the reservation fails cleanly", "[arenapool][pool][c120]") {
	arenapool p;
	std::memset(&p, 0, sizeof(p));

	// Reserve room for only ~500 slots, chunk 64.
	slot *base = static_cast<slot *>(arenaPoolSetup(&p, "small", sizeof(slot), 500, 64, 480));
	REQUIRE(base != nullptr);
	base[0].payload = 77;
	s32 countbefore = p.count;

	// 480 + 64 = 544 > 500 reserved -> grow must fail without touching state/data.
	s32 first = -999;
	void *r = arenaPoolGrow(&p, &first);
	REQUIRE(r == nullptr);
	REQUIRE(first == -1);
	REQUIRE(p.count == countbefore);
	REQUIRE(base[0].payload == 77);

	arenaPoolReset(&p);
}

TEST_CASE("arenapool: null / zero args are safe", "[arenapool][pool][c120]") {
	s32 first = 5;
	REQUIRE(arenaPoolGrow(nullptr, &first) == nullptr);
	REQUIRE(first == -1);

	arenapool p;
	std::memset(&p, 0, sizeof(p));
	// Grow before setup -> NULL (no reservation yet).
	REQUIRE(arenaPoolGrow(&p, &first) == nullptr);
	arenaPoolReset(&p); // no crash
}
