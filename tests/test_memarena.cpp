/*
 * tests/test_memarena.cpp -- reserve-and-commit growable arena (task #37).
 *
 * Source under test: port/src/memarena.c
 *
 * The arena is the pointer-stable backing store that lets game pools (chr,
 * model, anim, projectile, effect, object) grow at runtime WITHOUT moving --
 * required because the code holds raw interior pointers (prop->chr) and does
 * pointer arithmetic (`chr - g_ChrSlots`) on them. The property that matters
 * most, and that a realloc-based array could NOT provide, is: the base pointer
 * and every previously-handed-out interior pointer stay valid across growth.
 */
#include "catch.hpp"

#include <cstdint>
#include <cstring>

extern "C" {
#include "memarena.h"
}

namespace {
struct rec {
	int id;
	int payload;
};
} // namespace

TEST_CASE("memarena: reserve starts empty, ensure grows capacity", "[memarena][pool][c120]") {
	memarena a;
	REQUIRE(arenaReserve(&a, "test", sizeof(rec), 4096) == 1);
	REQUIRE(arenaCapacity(&a) == 0);

	void *base = arenaEnsure(&a, 10);
	REQUIRE(base != nullptr);
	REQUIRE(arenaCapacity(&a) >= 10);

	arenaRelease(&a);
	REQUIRE(a.base == nullptr);
}

TEST_CASE("memarena: base is STABLE across repeated growth", "[memarena][pool][c120]") {
	memarena a;
	REQUIRE(arenaReserve(&a, "stable", sizeof(rec), 1u << 20) == 1);

	rec *p0 = static_cast<rec *>(arenaEnsure(&a, 8));
	REQUIRE(p0 != nullptr);

	// Write into early slots, then grow many times and confirm the base never
	// moves and the early data survives -- this is the whole point of the arena.
	for (int i = 0; i < 8; i++) {
		p0[i].id = i;
		p0[i].payload = i * 100;
	}

	void *seen = p0;
	for (size_t want = 16; want <= 200000; want *= 2) {
		rec *p = static_cast<rec *>(arenaEnsure(&a, want));
		REQUIRE(p != nullptr);
		REQUIRE(static_cast<void *>(p) == seen); // base never moved
		REQUIRE(arenaCapacity(&a) >= want);
	}

	// Early records are intact after all that growth.
	for (int i = 0; i < 8; i++) {
		REQUIRE(p0[i].id == i);
		REQUIRE(p0[i].payload == i * 100);
	}

	arenaRelease(&a);
}

TEST_CASE("memarena: interior pointers + index arithmetic survive growth", "[memarena][pool][c120]") {
	memarena a;
	REQUIRE(arenaReserve(&a, "interior", sizeof(rec), 1u << 20) == 1);

	rec *base = static_cast<rec *>(arenaEnsure(&a, 64));
	REQUIRE(base != nullptr);

	// Hold a raw interior pointer (the `prop->chr` analogue) and remember the
	// index it should map to via pointer subtraction (the `chr - g_ChrSlots`
	// analogue).
	rec *held = &base[50];
	held->id = 0xBEEF;
	ptrdiff_t heldindex = held - base;
	REQUIRE(heldindex == 50);

	// Grow well past the held pointer.
	rec *base2 = static_cast<rec *>(arenaEnsure(&a, 500000));
	REQUIRE(base2 == base);              // base stable
	REQUIRE(held->id == 0xBEEF);         // interior pointer still valid
	REQUIRE((held - base2) == 50);       // pointer arithmetic still correct
	REQUIRE(&base2[heldindex] == held);  // index round-trips back to the pointer

	arenaRelease(&a);
}

TEST_CASE("memarena: growth beyond the reservation fails gracefully", "[memarena][pool][c120]") {
	memarena a;
	// Reserve room for exactly 1000 records.
	REQUIRE(arenaReserve(&a, "capped", sizeof(rec), 1000) == 1);

	rec *base = static_cast<rec *>(arenaEnsure(&a, 500));
	REQUIRE(base != nullptr);
	size_t capbefore = arenaCapacity(&a);
	REQUIRE(capbefore >= 500);

	base[0].id = 123; // committed data

	// Ask for far more than reserved -> NULL, and the arena keeps its prior
	// capacity + committed data rather than corrupting or shrinking.
	void *fail = arenaEnsure(&a, 100000);
	REQUIRE(fail == nullptr);
	REQUIRE(arenaCapacity(&a) == capbefore);
	REQUIRE(base[0].id == 123);

	arenaRelease(&a);
}

TEST_CASE("memarena: reset decommits but keeps the reserved base", "[memarena][pool][c120]") {
	memarena a;
	REQUIRE(arenaReserve(&a, "reset", sizeof(rec), 4096) == 1);

	rec *base1 = static_cast<rec *>(arenaEnsure(&a, 100));
	REQUIRE(base1 != nullptr);
	REQUIRE(arenaCapacity(&a) >= 100);

	arenaReset(&a);
	REQUIRE(arenaCapacity(&a) == 0);
	REQUIRE(a.base != nullptr); // reservation retained

	// Re-grow: same reserved base address is reused (stable across stages).
	rec *base2 = static_cast<rec *>(arenaEnsure(&a, 50));
	REQUIRE(base2 == base1);
	REQUIRE(arenaCapacity(&a) >= 50);

	arenaRelease(&a);
}

TEST_CASE("memarena: bad arguments are rejected, not crashed", "[memarena][pool][c120]") {
	memarena a;
	REQUIRE(arenaReserve(&a, "bad", 0, 100) == 0);      // zero stride
	REQUIRE(arenaReserve(&a, "bad", sizeof(rec), 0) == 0); // zero elements
	REQUIRE(arenaReserve(nullptr, "bad", 4, 4) == 0);   // null arena

	// Ensure/capacity/reset on an unreserved arena are safe no-ops.
	memarena z;
	std::memset(&z, 0, sizeof(z));
	REQUIRE(arenaEnsure(&z, 10) == nullptr);
	REQUIRE(arenaCapacity(&z) == 0);
	arenaReset(&z);   // no crash
	arenaRelease(&z); // no crash
}
