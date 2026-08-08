#include "catch.hpp"

#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>

using s32 = int32_t;

struct explosiontype {
	float rangeh, rangev, changerateh, changeratev, innersize, blastradius,
		damageradius;
	int16_t duration, propagationrate;
	float flarespeed;
	uint8_t smoketype;
	uint16_t sound;
	float damage;
};
struct sparktype {
	uint16_t unk00; int16_t unk02; uint16_t unk04, unk06, unk08, unk0a;
	float weight; uint16_t maxage, unk12, numsparks; uint32_t unk18, unk1c, unk20;
	float decel;
};
struct smoketype {
	int16_t duration, fadespeed, spreadspeed, size; float bgrotatespeed;
	uint8_t r, g, b; float fgrotatespeed; int16_t numclouds;
	float unk18, unk1c, unk20;
};

static constexpr size_t EXPLOSIONTYPE_BASE_COUNT = 26;
static constexpr size_t SPARKTYPE_BASE_COUNT = 27;
static constexpr size_t SMOKETYPE_BASE_COUNT = 23;

extern "C" {
using pd_effect_sound_id_fn = void (*)(s32, char *, size_t);
s32 pdEffectSourceBuildExplosionGraph(const char *, const explosiontype *, size_t,
	pd_effect_sound_id_fn, char **, size_t *);
s32 pdEffectSourceBuildSparkGraph(const char *, const sparktype *, size_t,
	char **, size_t *);
s32 pdEffectSourceBuildSmokeGraph(const char *, const smoketype *, size_t,
	char **, size_t *);
const char *pdEffectSourceExplosionProfileId(size_t);
const char *pdEffectSourceSparkProfileId(size_t);
const char *pdEffectSourceSmokeProfileId(size_t);
}

static void testSoundId(s32 sound_ref, char *out, size_t out_n)
{
	std::snprintf(out, out_n, "base:sfx_test_%d", (int)sound_ref);
}

static size_t countText(const std::string &text, const std::string &needle)
{
	size_t count = 0;
	for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos;
			pos += needle.size()) count++;
	return count;
}

TEST_CASE("base pdeffect source preserves exact native profile rows and provenance",
	"[modding][pdxxx][pdeffect][t-assets-016]")
{
	struct explosiontype explosions[EXPLOSIONTYPE_BASE_COUNT] = {};
	struct sparktype sparks[SPARKTYPE_BASE_COUNT] = {};
	struct smoketype smoke[SMOKETYPE_BASE_COUNT] = {};

	explosions[2] = { 20.0f, 21.0f, 2.0f, 3.0f, 30.0f, 50.0f, 60.0f,
		40, 2, 3.0f, 2, 0x99, 0.125f };
	sparks[16] = { 50, 28, 100, 1, 2, 3, 1.0f, 60, 30, 10,
		1, 0xffff80ffu, 0xffffffffu, 0.02f };
	smoke[2] = { 220, 60, 50, 20, 0.01f, 0x80, 0x81, 0x82,
		0.3f, 120, 0.15f, 0.3f, 1.0f };

	char *bytes = nullptr;
	size_t len = 0;
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		explosions, EXPLOSIONTYPE_BASE_COUNT, testSoundId, &bytes, &len) == 0);
	std::string graph(bytes, len);
	std::free(bytes);
	REQUIRE(countText(graph, "\"module\": \"effect.explosion\"") ==
		EXPLOSIONTYPE_BASE_COUNT);
	REQUIRE(graph.find("\"id\": \"eyespy\"") != std::string::npos);
	REQUIRE(graph.find("\"range_h\": 20") != std::string::npos);
	REQUIRE(graph.find("\"smoke_profile\": \"mini\"") != std::string::npos);
	REQUIRE(graph.find("\"audio_catalog_id\": \"base:sfx_test_153\"") !=
		std::string::npos);
	REQUIRE(graph.find("\"id\": \"none\"") != std::string::npos);
	REQUIRE(graph.find("\"audio_catalog_id\": null") != std::string::npos);
	REQUIRE(graph.find("\"callsite_owned\": [\"target\", \"attachment\", \"priority\", \"effect_enable\", \"owner\", \"scorch_enable\"]") != std::string::npos);
	REQUIRE(graph.find("\"target\":") == std::string::npos);
	REQUIRE(graph.find("\"absent_independent_tables\": [\"beam\", \"screen\"]") != std::string::npos);
	REQUIRE(graph.find("\"max_parts\": 40") != std::string::npos);
	REQUIRE(graph.find("\"intensity\": 255") != std::string::npos);
	REQUIRE(graph.find("\"duration_ticks\": 12") != std::string::npos);
	REQUIRE(graph.find("\"gas_barrel_tick\": 15") != std::string::npos);
	REQUIRE(graph.find("\"spawn_tick_formula\": \"duration_ticks / 2\"") != std::string::npos);

	bytes = nullptr;
	len = 0;
	REQUIRE(pdEffectSourceBuildSparkGraph("base:effect_spark_profiles", sparks,
		SPARKTYPE_BASE_COUNT, &bytes, &len) == 0);
	graph.assign(bytes, len);
	std::free(bytes);
	REQUIRE(countText(graph, "\"module\": \"effect.spark\"") == SPARKTYPE_BASE_COUNT);
	REQUIRE(graph.find("\"id\": \"projectile\"") != std::string::npos);
	REQUIRE(graph.find("\"color_start_rgba\": \"#ffff80ff\"") != std::string::npos);
	REQUIRE(graph.find("\"streak_width_growth_per_tick\": 2") != std::string::npos);

	bytes = nullptr;
	len = 0;
	REQUIRE(pdEffectSourceBuildSmokeGraph("base:effect_smoke_profiles", smoke,
		SMOKETYPE_BASE_COUNT, &bytes, &len) == 0);
	graph.assign(bytes, len);
	std::free(bytes);
	REQUIRE(countText(graph, "\"module\": \"effect.smoke\"") == SMOKETYPE_BASE_COUNT);
	REQUIRE(graph.find("\"id\": \"mini\"") != std::string::npos);
	REQUIRE(graph.find("\"color_rgb\": \"#808182\"") != std::string::npos);
	REQUIRE(graph.find("\"size_growth_per_tick\": 0.150000006") != std::string::npos);
}

TEST_CASE("pdeffect source profile names are permanent unique nonnumeric identities",
	"[modding][pdxxx][pdeffect][t-assets-016]")
{
	std::unordered_set<std::string> ids;
	for (size_t i = 0; i < EXPLOSIONTYPE_BASE_COUNT; i++) {
		const char *id = pdEffectSourceExplosionProfileId(i);
		REQUIRE(id != nullptr);
		REQUIRE(ids.insert(std::string("explosion:") + id).second);
	}
	for (size_t i = 0; i < SPARKTYPE_BASE_COUNT; i++) {
		const char *id = pdEffectSourceSparkProfileId(i);
		REQUIRE(id != nullptr);
		REQUIRE(ids.insert(std::string("spark:") + id).second);
	}
	for (size_t i = 0; i < SMOKETYPE_BASE_COUNT; i++) {
		const char *id = pdEffectSourceSmokeProfileId(i);
		REQUIRE(id != nullptr);
		REQUIRE(ids.insert(std::string("smoke:") + id).second);
	}
	REQUIRE(pdEffectSourceExplosionProfileId(EXPLOSIONTYPE_BASE_COUNT) == nullptr);
	REQUIRE(pdEffectSourceSparkProfileId(SPARKTYPE_BASE_COUNT) == nullptr);
	REQUIRE(pdEffectSourceSmokeProfileId(SMOKETYPE_BASE_COUNT) == nullptr);
}

TEST_CASE("pdeffect source rejects omitted native rows",
	"[modding][pdxxx][pdeffect][t-assets-016]")
{
	struct explosiontype rows[EXPLOSIONTYPE_BASE_COUNT] = {};
	char *bytes = nullptr;
	size_t len = 0;
	REQUIRE(pdEffectSourceBuildExplosionGraph("base:effect_explosion_profiles",
		rows, EXPLOSIONTYPE_BASE_COUNT - 1, testSoundId, &bytes, &len) != 0);
	REQUIRE(bytes == nullptr);
}
