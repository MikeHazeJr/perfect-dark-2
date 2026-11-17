/*
 * tests/test_scenario_pad_fallback.cpp -- B-1125, B-1126: fallback placement
 * pads for multiplayer scenarios.
 *
 * Source under test:
 *   port/src/scenario_pad_fallback.c::scenarioPadPickSpaced
 *   port/src/scenario_pad_fallback.c::scenarioPadPickFarthest
 *   port/src/scenario_pad_fallback.c::scenarioPadPickNearest
 *   port/src/scenario_pad_fallback.c::scenarioPadReduceToRooms
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
 *
 * B-1126: the same arenas left Capture the Case without bases (every case was
 * created off-world), King of the Hill with its hill stuck in the room of pad
 * 0, and Hold the Briefcase always starting on pad 0. Bases now come from the
 * farthest-point pick, a base's respawn pads from the nearest-pad pick, and
 * hills from one pad per room followed by the spaced pick; the cases below
 * pin those contracts the same way.
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
	scenariopadpick p = {};
	p.padnum = padnum;
	p.x = x;
	p.z = z;
	return p;
}

scenariopadpick makePick3(s16 padnum, f32 x, f32 y, f32 z)
{
	scenariopadpick p = makePick(padnum, x, z);
	p.y = y;
	return p;
}

scenariopadpick makeRoomPick(s16 padnum, f32 x, f32 z, s16 room)
{
	scenariopadpick p = makePick(padnum, x, z);
	p.room = room;
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

f32 minPairwiseXZ(const scenariopadpick *picks, s32 count)
{
	f32 best = -1.0f;

	for (s32 i = 0; i < count; i++) {
		for (s32 j = i + 1; j < count; j++) {
			f32 d = distXZ(picks[i], picks[j]);
			if (best < 0.0f || d < best) {
				best = d;
			}
		}
	}

	return best;
}

/* A 5 x 5 grid of pads 1000 apart over 0..4000 on both axes, numbered 1..25
 * row by row. */
std::vector<scenariopadpick> makeGrid(void)
{
	std::vector<scenariopadpick> out;

	for (s32 row = 0; row < 5; row++) {
		for (s32 col = 0; col < 5; col++) {
			out.push_back(makePick((s16)(row * 5 + col + 1), col * 1000.0f, row * 1000.0f));
		}
	}

	return out;
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

/* ---- B-1126: Capture the Case bases (farthest-point pick) ---- */

TEST_CASE("scenarioPadPickFarthest: with no base yet, the first is one random usable pad",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> cands = {
		makePick(0, 0, 0), makePick(-1, 100, 0), makePick(6, 2000, 0), makePick(9, 4000, 0),
	};
	bool seen6 = false;
	bool seen9 = false;

	for (u32 seed = 1; seed <= 40; seed++) {
		scenariopadpick chosen[4];

		seedRng(seed);
		REQUIRE(scenarioPadPickFarthest(chosen, 0, 4, 1, cands.data(), (s32)cands.size(), testRng) == 1);
		REQUIRE(s_RngCalls == 1);
		REQUIRE((chosen[0].padnum == 6 || chosen[0].padnum == 9));
		seen6 = seen6 || chosen[0].padnum == 6;
		seen9 = seen9 || chosen[0].padnum == 9;
	}

	REQUIRE(seen6);
	REQUIRE(seen9);
}

TEST_CASE("scenarioPadPickFarthest: four bases on a square arena sit far apart",
          "[scenario][ctc][B-1126]") {
	/* The best four pads on this grid are the corners, 4000 apart. Taking the
	 * farthest pad each time is guaranteed at least half of the best possible
	 * smallest gap, whatever the random first base. */
	std::vector<scenariopadpick> cands = makeGrid();

	for (u32 seed = 1; seed <= 25; seed++) {
		scenariopadpick chosen[4];

		seedRng(seed);
		s32 count = scenarioPadPickFarthest(chosen, 0, 4, 4, cands.data(), (s32)cands.size(), testRng);

		REQUIRE(count == 4);
		REQUIRE(s_RngCalls == 1);
		REQUIRE(minPairwiseXZ(chosen, count) >= 2000.0f);
	}
}

TEST_CASE("scenarioPadPickFarthest: bases the arena has stay, and new ones go farthest from them",
          "[scenario][ctc][B-1126]") {
	scenariopadpick chosen[4] = { makePick(30, 0, 0) };
	std::vector<scenariopadpick> cands = {
		makePick(1, 1000, 0), makePick(2, 5000, 0), makePick(3, 2500, 0), makePick(30, 0, 0),
	};

	seedRng(3);
	REQUIRE(scenarioPadPickFarthest(chosen, 1, 4, 3, cands.data(), (s32)cands.size(), testRng) == 3);
	/* With a base already chosen nothing is random, so an arena's sequence
	 * is not shifted. */
	REQUIRE(s_RngCalls == 0);
	REQUIRE(chosen[0].padnum == 30);
	REQUIRE(chosen[1].padnum == 2);
	REQUIRE(chosen[2].padnum == 3);
}

TEST_CASE("scenarioPadPickFarthest: a tie goes to the earlier candidate",
          "[scenario][ctc][B-1126]") {
	scenariopadpick chosen[2] = { makePick(1, 0, 0) };
	std::vector<scenariopadpick> forward = { makePick(7, 3000, 0), makePick(8, -3000, 0) };
	std::vector<scenariopadpick> backward = { makePick(8, -3000, 0), makePick(7, 3000, 0) };

	REQUIRE(scenarioPadPickFarthest(chosen, 1, 2, 2, forward.data(), 2, testRng) == 2);
	REQUIRE(chosen[1].padnum == 7);
	REQUIRE(scenarioPadPickFarthest(chosen, 1, 2, 2, backward.data(), 2, testRng) == 2);
	REQUIRE(chosen[1].padnum == 8);
}

TEST_CASE("scenarioPadPickFarthest: no pad twice and no pad number <= 0",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> cands = {
		makePick(0, 9000, 0), makePick(-3, -9000, 0), makePick(5, 100, 0),
		makePick(5, 100, 0), makePick(6, 3000, 0),
	};

	for (u32 seed = 1; seed <= 10; seed++) {
		scenariopadpick chosen[8];

		seedRng(seed);
		s32 count = scenarioPadPickFarthest(chosen, 0, 8, 8, cands.data(), (s32)cands.size(), testRng);

		REQUIRE(count == 2);
		REQUIRE(chosen[0].padnum != chosen[1].padnum);
		REQUIRE((chosen[0].padnum == 5 || chosen[0].padnum == 6));
		REQUIRE((chosen[1].padnum == 5 || chosen[1].padnum == 6));
	}
}

TEST_CASE("scenarioPadPickFarthest: missing inputs and capacity",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> cands = makeClusters(4, 1, 0.0f, 3000.0f);
	scenariopadpick chosen[4] = { makePick(40, -3000, 0) };
	scenariopadpick empty[4];
	scenariopadpick two[2] = { makePick(40, -3000, 0) };

	/* The first base needs the random source; later ones do not. */
	REQUIRE(scenarioPadPickFarthest(empty, 0, 4, 4, cands.data(), (s32)cands.size(), nullptr) == 0);
	REQUIRE(scenarioPadPickFarthest(chosen, 1, 4, 2, cands.data(), (s32)cands.size(), nullptr) == 2);
	REQUIRE(chosen[1].padnum == 4);
	REQUIRE(scenarioPadPickFarthest(nullptr, 0, 4, 4, cands.data(), (s32)cands.size(), testRng) == 0);
	REQUIRE(scenarioPadPickFarthest(chosen, -1, 4, 4, cands.data(), (s32)cands.size(), testRng) == 0);
	REQUIRE(scenarioPadPickFarthest(chosen, 1, 4, 4, nullptr, 3, testRng) == 1);
	REQUIRE(scenarioPadPickFarthest(chosen, 1, 4, 4, cands.data(), 0, testRng) == 1);
	REQUIRE(scenarioPadPickFarthest(two, 1, 2, 4, cands.data(), (s32)cands.size(), testRng) == 2);
}

/* ---- B-1126: Capture the Case respawn pads (nearest-pad pick) ---- */

TEST_CASE("scenarioPadPickNearest: respawn pads are the nearest ones on the base's own side",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> anchors = { makePick3(10, 0, 0, 0), makePick3(20, 4000, 0, 0) };
	std::vector<scenariopadpick> cands = {
		makePick3(1, 1900, 0, 0), makePick3(2, 100, 0, 0), makePick3(3, 2100, 0, 0),
		makePick3(4, 200, 0, 0), makePick3(5, 3900, 0, 0), makePick3(6, 3800, 0, 0),
	};
	s16 out[6];

	REQUIRE(scenarioPadPickNearest(out, 6, 0, anchors.data(), 2, cands.data(), (s32)cands.size()) == 3);
	REQUIRE(out[0] == 2);
	REQUIRE(out[1] == 4);
	REQUIRE(out[2] == 1);

	REQUIRE(scenarioPadPickNearest(out, 6, 1, anchors.data(), 2, cands.data(), (s32)cands.size()) == 3);
	REQUIRE(out[0] == 5);
	REQUIRE(out[1] == 6);
	REQUIRE(out[2] == 3);
}

TEST_CASE("scenarioPadPickNearest: a pad on another floor counts as farther",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> anchors = { makePick3(10, 0, 0, 0) };
	std::vector<scenariopadpick> cands = { makePick3(1, 100, 1000, 0), makePick3(2, 300, 0, 0) };
	s16 out[2];

	REQUIRE(scenarioPadPickNearest(out, 2, 0, anchors.data(), 1, cands.data(), 2) == 2);
	REQUIRE(out[0] == 2);
	REQUIRE(out[1] == 1);
}

TEST_CASE("scenarioPadPickNearest: base pads, repeats and pad numbers <= 0 are skipped",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> anchors = { makePick3(10, 0, 0, 0), makePick3(20, 9000, 0, 0) };
	std::vector<scenariopadpick> cands = {
		makePick3(10, 0, 0, 0), makePick3(20, 9000, 0, 0), makePick3(3, 50, 0, 0),
		makePick3(3, 50, 0, 0), makePick3(0, 10, 0, 0), makePick3(-2, 20, 0, 0),
		makePick3(4, 400, 0, 0), makePick3(5, 500, 0, 0),
	};
	s16 two[2];
	s16 six[6];

	seedRng(1);
	REQUIRE(scenarioPadPickNearest(two, 2, 0, anchors.data(), 2, cands.data(), (s32)cands.size()) == 2);
	REQUIRE(two[0] == 3);
	REQUIRE(two[1] == 4);

	REQUIRE(scenarioPadPickNearest(six, 6, 0, anchors.data(), 2, cands.data(), (s32)cands.size()) == 3);
	REQUIRE(six[0] == 3);
	REQUIRE(six[1] == 4);
	REQUIRE(six[2] == 5);
	REQUIRE(s_RngCalls == 0);
}

TEST_CASE("scenarioPadPickNearest: a pad halfway between two bases belongs to the lower one",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> anchors = { makePick3(10, -1000, 0, 0), makePick3(20, 1000, 0, 0) };
	std::vector<scenariopadpick> cands = { makePick3(1, 0, 0, 0) };
	s16 out[2];

	REQUIRE(scenarioPadPickNearest(out, 2, 0, anchors.data(), 2, cands.data(), 1) == 1);
	REQUIRE(out[0] == 1);
	REQUIRE(scenarioPadPickNearest(out, 2, 1, anchors.data(), 2, cands.data(), 1) == 0);
}

TEST_CASE("scenarioPadPickNearest: missing inputs give no pads",
          "[scenario][ctc][B-1126]") {
	std::vector<scenariopadpick> anchors = { makePick3(10, 0, 0, 0), makePick3(20, 4000, 0, 0) };
	std::vector<scenariopadpick> cands = { makePick3(1, 100, 0, 0), makePick3(2, 3900, 0, 0) };
	s16 out[4];

	REQUIRE(scenarioPadPickNearest(nullptr, 4, 0, anchors.data(), 2, cands.data(), 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 0, 0, anchors.data(), 2, cands.data(), 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 4, 2, anchors.data(), 2, cands.data(), 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 4, -1, anchors.data(), 2, cands.data(), 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 4, 0, nullptr, 2, cands.data(), 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 4, 0, anchors.data(), 0, cands.data(), 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 4, 0, anchors.data(), 2, nullptr, 2) == 0);
	REQUIRE(scenarioPadPickNearest(out, 4, 0, anchors.data(), 2, cands.data(), 0) == 0);
}

/* ---- B-1126: King of the Hill hills (one pad per room, then spaced) ---- */

TEST_CASE("scenarioPadReduceToRooms: one pad per room, the one nearest the room's middle",
          "[scenario][koh][B-1126]") {
	std::vector<scenariopadpick> cands = {
		makeRoomPick(1, 0, 0, 5), makeRoomPick(2, 1000, 0, 7), makeRoomPick(3, 100, 0, 5),
		makeRoomPick(4, 1200, 0, 7), makeRoomPick(5, 200, 0, 5),
	};

	REQUIRE(scenarioPadReduceToRooms(cands.data(), (s32)cands.size(), 1) == 2);
	/* Rooms keep the order of their first pad. Room 5's middle is x=100; room
	 * 7's two pads are equally near its middle, so the earlier one stays. */
	REQUIRE(cands[0].room == 5);
	REQUIRE(cands[0].padnum == 3);
	REQUIRE(cands[1].room == 7);
	REQUIRE(cands[1].padnum == 2);
}

TEST_CASE("scenarioPadReduceToRooms: rooms with too few pads are dropped",
          "[scenario][koh][B-1126]") {
	std::vector<scenariopadpick> base = {
		makeRoomPick(1, 0, 0, 5), makeRoomPick(2, 50, 0, 5), makeRoomPick(3, 3000, 0, 9),
	};
	std::vector<scenariopadpick> two = base;
	std::vector<scenariopadpick> one = base;

	REQUIRE(scenarioPadReduceToRooms(two.data(), (s32)two.size(), 2) == 1);
	REQUIRE(two[0].room == 5);
	REQUIRE(scenarioPadReduceToRooms(one.data(), (s32)one.size(), 1) == 2);
	REQUIRE(one[1].room == 9);
}

TEST_CASE("scenarioPadReduceToRooms: pad numbers <= 0 and rooms <= 0 are dropped",
          "[scenario][koh][B-1126]") {
	std::vector<scenariopadpick> cands = {
		makeRoomPick(0, 0, 0, 5), makeRoomPick(4, 10, 0, 0), makeRoomPick(6, 20, 0, -1),
		makeRoomPick(7, 30, 0, 3),
	};

	REQUIRE(scenarioPadReduceToRooms(cands.data(), (s32)cands.size(), 1) == 1);
	REQUIRE(cands[0].padnum == 7);
	REQUIRE(scenarioPadReduceToRooms(nullptr, 3, 1) == 0);
	REQUIRE(scenarioPadReduceToRooms(cands.data(), 0, 1) == 0);
}

TEST_CASE("hill fallback: one pad per room then the spaced pick gives hills in distinct rooms",
          "[scenario][koh][B-1126]") {
	/* Twelve rooms in a 4 x 3 layout, 1500 apart, each crossed by three
	 * waypoints 40 apart; King of the Hill asks for six hills. */
	std::vector<scenariopadpick> cands;
	s16 padnum = 1;

	for (s32 room = 1; room <= 12; room++) {
		f32 rx = ((room - 1) % 4) * 1500.0f;
		f32 rz = ((room - 1) / 4) * 1500.0f;

		for (s32 k = 0; k < 3; k++) {
			cands.push_back(makeRoomPick(padnum++, rx + k * 40.0f, rz, (s16)room));
		}
	}

	s32 numrooms = scenarioPadReduceToRooms(cands.data(), (s32)cands.size(), 2);
	REQUIRE(numrooms == 12);

	for (u32 seed = 1; seed <= 20; seed++) {
		scenariopadpick chosen[6];

		seedRng(seed);
		s32 count = scenarioPadPickSpaced(chosen, 0, 6, 6, cands.data(), numrooms, testRng);

		REQUIRE(count == 6);
		for (s32 i = 0; i < count; i++) {
			for (s32 j = i + 1; j < count; j++) {
				REQUIRE(chosen[i].room != chosen[j].room);
			}
		}
		REQUIRE(minPairwiseXZ(chosen, count) >= SCENARIO_PAD_FALLBACK_WIDEST_SPACING);
	}
}
