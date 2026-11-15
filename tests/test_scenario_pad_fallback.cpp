/*
 * tests/test_scenario_pad_fallback.cpp -- B-1125: spaced fallback placement
 * pads for multiplayer scenarios.
 *
 * Source under test:
 *   port/src/scenario_pad_fallback.c::scenarioPadPickSpaced
 *
 * Hacker Central puts its terminal and data uplink on pads from a bank (the
 * arena's Capture the Case pads plus multiplayer ammo crates). On an arena
 * with an empty bank htmInitProps divided by zero; with fewer pads than it
 * needs, its selection loop never ended. The game now fills a short bank
 * from walkable pads through this picker. These cases pin what the bank
 * relies on: no pad number <= 0, no pad twice, the pads already in the bank
 * kept and kept away from, the widest spacing that still reaches the target,
 * capacity respected, and a result that depends only on the inputs and the
 * random sequence.
 */

#include "catch.hpp"

#include <cmath>
#include <vector>

extern "C" {
#include "scenario_pad_fallback.h"
}

namespace {

u32 s_RngState;
s32 s_RngCalls;

u32 testRng(void)
{
	s_RngCalls++;
	s_RngState = s_RngState * 1664525u + 1013904223u;
	return s_RngState >> 8;
}

void seedRng(u32 seed)
{
	s_RngState = seed;
	s_RngCalls = 0;
}

scenariopadpick makePick(s16 padnum, f32 x, f32 z)
{
	scenariopadpick p;
	p.padnum = padnum;
	p.x = x;
	p.z = z;
	return p;
}

f32 distXZ(const scenariopadpick &a, const scenariopadpick &b)
{
	f32 dx = a.x - b.x;
	f32 dz = a.z - b.z;
	return std::sqrt(dx * dx + dz * dz);
}

/* Clusters of `per` pads, `gap` apart inside a cluster, clusters `apart`
 * apart on the x axis. Pad numbers start at 1; cluster c holds pads
 * c*per+1 .. c*per+per. */
std::vector<scenariopadpick> makeClusters(s32 clusters, s32 per, f32 gap, f32 apart)
{
	std::vector<scenariopadpick> out;

	for (s32 c = 0; c < clusters; c++) {
		for (s32 i = 0; i < per; i++) {
			out.push_back(makePick((s16)(c * per + i + 1), c * apart + i * gap, (f32)(i % 2) * gap));
		}
	}

	return out;
}

s32 clusterOf(s16 padnum, s32 per)
{
	return (padnum - 1) / per;
}

} // namespace

TEST_CASE("scenarioPadPickSpaced: a bank already at the target is left alone",
          "[scenario][htm][B-1125]") {
	scenariopadpick chosen[8] = { makePick(3, 0, 0), makePick(9, 500, 0), makePick(12, 0, 900) };
	std::vector<scenariopadpick> cands = makeClusters(2, 3, 50.0f, 5000.0f);

	seedRng(1);
	REQUIRE(scenarioPadPickSpaced(chosen, 3, 8, 3, cands.data(), (s32)cands.size(), testRng) == 3);
	REQUIRE(s_RngCalls == 0);
	REQUIRE(chosen[0].padnum == 3);
	REQUIRE(chosen[1].padnum == 9);
	REQUIRE(chosen[2].padnum == 12);
}

TEST_CASE("scenarioPadPickSpaced: pad numbers <= 0 are never picked",
          "[scenario][htm][B-1125]") {
	scenariopadpick chosen[16];
	std::vector<scenariopadpick> cands = {
		makePick(0, 0, 0), makePick(-1, 3000, 0), makePick(-5, 6000, 0),
		makePick(4, 9000, 0), makePick(8, 12000, 0), makePick(15, 15000, 0),
	};

	seedRng(7);
	s32 count = scenarioPadPickSpaced(chosen, 0, 16, 16, cands.data(), (s32)cands.size(), testRng);

	REQUIRE(count == 3);
	for (s32 i = 0; i < count; i++) {
		REQUIRE(chosen[i].padnum > 0);
	}
}

TEST_CASE("scenarioPadPickSpaced: no pad twice, and none already in the bank",
          "[scenario][htm][B-1125]") {
	scenariopadpick chosen[16] = { makePick(4, 0, 0) };
	std::vector<scenariopadpick> cands = {
		makePick(7, 2000, 0), makePick(7, 2000, 0), makePick(4, 0, 0),
		makePick(4, 0, 0), makePick(11, 4000, 0), makePick(7, 2000, 0),
	};

	seedRng(3);
	s32 count = scenarioPadPickSpaced(chosen, 1, 16, 16, cands.data(), (s32)cands.size(), testRng);

	REQUIRE(count == 3);
	REQUIRE(chosen[0].padnum == 4);
	for (s32 i = 0; i < count; i++) {
		for (s32 j = i + 1; j < count; j++) {
			REQUIRE(chosen[i].padnum != chosen[j].padnum);
		}
	}
}

TEST_CASE("scenarioPadPickSpaced: picks are spread at the widest spacing that reaches the target",
          "[scenario][htm][B-1125]") {
	/* Four tight clusters 3000 apart: at the widest tier only one pad per
	 * cluster fits, and that already reaches the target of 4, so every seed
	 * must give one pad from each cluster. */
	std::vector<scenariopadpick> cands = makeClusters(4, 5, 50.0f, 3000.0f);

	for (u32 seed = 1; seed <= 20; seed++) {
		scenariopadpick chosen[8];
		s32 seen[4] = { 0, 0, 0, 0 };

		seedRng(seed);
		s32 count = scenarioPadPickSpaced(chosen, 0, 8, 4, cands.data(), (s32)cands.size(), testRng);

		REQUIRE(count == 4);
		for (s32 i = 0; i < count; i++) {
			seen[clusterOf(chosen[i].padnum, 5)]++;
			for (s32 j = i + 1; j < count; j++) {
				REQUIRE(distXZ(chosen[i], chosen[j]) >= SCENARIO_PAD_FALLBACK_WIDEST_SPACING);
			}
		}
		REQUIRE(seen[0] == 1);
		REQUIRE(seen[1] == 1);
		REQUIRE(seen[2] == 1);
		REQUIRE(seen[3] == 1);
	}
}

TEST_CASE("scenarioPadPickSpaced: spacing steps down only as far as it must",
          "[scenario][htm][B-1125]") {
	/* Two clusters 600 apart. The widest tier (1000) fits one pad; the next
	 * tier (500) fits one from the other cluster, which reaches the target
	 * of 2. Two pads from the same cluster would mean the picker went
	 * straight to a narrower tier. */
	std::vector<scenariopadpick> cands = makeClusters(2, 3, 10.0f, 600.0f);

	for (u32 seed = 1; seed <= 20; seed++) {
		scenariopadpick chosen[4];

		seedRng(seed);
		s32 count = scenarioPadPickSpaced(chosen, 0, 4, 2, cands.data(), (s32)cands.size(), testRng);

		REQUIRE(count == 2);
		REQUIRE(clusterOf(chosen[0].padnum, 3) != clusterOf(chosen[1].padnum, 3));
	}
}

TEST_CASE("scenarioPadPickSpaced: a tight arena still reaches the target",
          "[scenario][htm][B-1125]") {
	/* Every candidate within 100 units: the last tier (0) takes any
	 * remaining distinct pad, so a small arena still gets its pads. */
	std::vector<scenariopadpick> cands = makeClusters(1, 6, 15.0f, 0.0f);
	scenariopadpick chosen[8];

	seedRng(11);
	REQUIRE(scenarioPadPickSpaced(chosen, 0, 8, 5, cands.data(), (s32)cands.size(), testRng) == 5);
}

TEST_CASE("scenarioPadPickSpaced: new picks keep away from pads already in the bank",
          "[scenario][htm][B-1125]") {
	scenariopadpick chosen[4] = { makePick(1, 0, 0) };
	std::vector<scenariopadpick> cands = { makePick(2, 10, 0), makePick(3, 5000, 0) };

	for (u32 seed = 1; seed <= 10; seed++) {
		seedRng(seed);
		REQUIRE(scenarioPadPickSpaced(chosen, 1, 4, 2, cands.data(), (s32)cands.size(), testRng) == 2);
		REQUIRE(chosen[0].padnum == 1);
		REQUIRE(chosen[1].padnum == 3);
	}
}

TEST_CASE("scenarioPadPickSpaced: same inputs and random sequence give the same pads",
          "[scenario][htm][B-1125]") {
	std::vector<scenariopadpick> cands;
	seedRng(99);
	for (s32 i = 0; i < 144; i++) {
		cands.push_back(makePick((s16)(i + 1), (f32)(testRng() % 4000), (f32)(testRng() % 4000)));
	}

	scenariopadpick a[8];
	scenariopadpick b[8];

	seedRng(1234);
	s32 na = scenarioPadPickSpaced(a, 0, 8, 8, cands.data(), (s32)cands.size(), testRng);
	seedRng(1234);
	s32 nb = scenarioPadPickSpaced(b, 0, 8, 8, cands.data(), (s32)cands.size(), testRng);

	REQUIRE(na == 8);
	REQUIRE(nb == na);
	for (s32 i = 0; i < na; i++) {
		REQUIRE(a[i].padnum == b[i].padnum);
	}
}

TEST_CASE("scenarioPadPickSpaced: an empty bank on an arena-sized map gets the Hacker Central bank",
          "[scenario][htm][B-1125]") {
	/* Complex has 144 navigation waypoints over about 4000 x 4000 units;
	 * Hacker Central fills an empty bank to 8 pads. */
	std::vector<scenariopadpick> cands;
	seedRng(5);
	for (s32 i = 0; i < 144; i++) {
		cands.push_back(makePick((s16)(i + 1), (f32)(testRng() % 4000), (f32)(testRng() % 4000)));
	}

	for (u32 seed = 1; seed <= 20; seed++) {
		scenariopadpick chosen[8];

		seedRng(seed);
		s32 count = scenarioPadPickSpaced(chosen, 0, 8, 8, cands.data(), (s32)cands.size(), testRng);

		REQUIRE(count == 8);
		for (s32 i = 0; i < count; i++) {
			REQUIRE(chosen[i].padnum > 0);
			for (s32 j = i + 1; j < count; j++) {
				REQUIRE(chosen[i].padnum != chosen[j].padnum);
			}
		}
	}
}

TEST_CASE("scenarioPadPickSpaced: capacity bounds the bank",
          "[scenario][htm][B-1125]") {
	std::vector<scenariopadpick> cands = makeClusters(6, 1, 0.0f, 3000.0f);
	scenariopadpick chosen[3] = { makePick(50, -9000, 0) };

	seedRng(2);
	REQUIRE(scenarioPadPickSpaced(chosen, 1, 3, 10, cands.data(), (s32)cands.size(), testRng) == 3);
}

TEST_CASE("scenarioPadPickSpaced: missing inputs leave the bank alone",
          "[scenario][htm][B-1125]") {
	std::vector<scenariopadpick> cands = makeClusters(3, 1, 0.0f, 3000.0f);
	scenariopadpick chosen[4] = { makePick(20, 0, 0) };

	seedRng(4);
	REQUIRE(scenarioPadPickSpaced(chosen, 1, 4, 4, cands.data(), 0, testRng) == 1);
	REQUIRE(scenarioPadPickSpaced(chosen, 1, 4, 4, nullptr, 3, testRng) == 1);
	REQUIRE(scenarioPadPickSpaced(chosen, 1, 4, 4, cands.data(), (s32)cands.size(), nullptr) == 1);
	REQUIRE(scenarioPadPickSpaced(nullptr, 0, 4, 4, cands.data(), (s32)cands.size(), testRng) == 0);
	REQUIRE(scenarioPadPickSpaced(chosen, -1, 4, 4, cands.data(), (s32)cands.size(), testRng) == 0);
	REQUIRE(chosen[0].padnum == 20);
}

TEST_CASE("scenarioPadPickSpaced: candidates past the cap are ignored",
          "[scenario][htm][B-1125]") {
	/* Only the candidates past SCENARIO_PAD_FALLBACK_MAX_CANDIDATES are
	 * usable, so nothing is picked: the cap bounds the work on huge maps. */
	std::vector<scenariopadpick> cands(SCENARIO_PAD_FALLBACK_MAX_CANDIDATES + 50, makePick(0, 0, 0));
	for (s32 i = SCENARIO_PAD_FALLBACK_MAX_CANDIDATES; i < (s32)cands.size(); i++) {
		cands[i] = makePick((s16)(i + 1), (f32)i * 2000.0f, 0);
	}
	scenariopadpick chosen[8];

	seedRng(6);
	REQUIRE(scenarioPadPickSpaced(chosen, 0, 8, 8, cands.data(), (s32)cands.size(), testRng) == 0);
}
