#include "catch.hpp"

#include <cstring>
#include <vector>

extern "C" {
#include "effect_executor.h"
#include "effect_graph_runtime.h"
#include "pdeffect_source.h"

float testStubEffectExplosionRangeH(s32 index);
s32 testStubEffectExplosionSmokeType(s32 index);
s32 testStubEffectSmokeDuration(s32 index);
void testStubEffectAudio(const char *id, s32 category, s32 sound_id);
s32 testStubEffectExplosionRow(s32 index, void *out, size_t size);
s32 testStubEffectSparkRow(s32 index, void *out, size_t size);
s32 testStubEffectSmokeRow(s32 index, void *out, size_t size);
}

struct ExplosionRow {
	float rangeh, rangev, changerateh, changeratev, innersize, blastradius,
		damageradius; s16 duration, propagationrate; float flarespeed;
	u8 smoketype, pad; u16 sound; float damage;
};
struct SparkRow {
	u16 unk00; s16 unk02; u16 unk04, unk06, unk08, unk0a; float weight;
	u16 maxage, unk12, numsparks; u32 unk18, unk1c, unk20; float decel;
};
struct SmokeRow {
	s16 duration, fadespeed, spreadspeed, size; float bgrotatespeed;
	u8 r, g, b, pad; float fgrotatespeed; s16 numclouds, pad2;
	float unk18, unk1c, unk20;
};
static_assert(sizeof(ExplosionRow) == 44);
static_assert(sizeof(SparkRow) == 40);
static_assert(sizeof(SmokeRow) == 36);

TEST_CASE("explosion profile audio is a typed SFX dependency",
	"[modding][pdxxx][effect_executor][t-assets-019]")
{
	effectExecutorReset();
	char error[256] = {};
	std::vector<pd_effect_smoke_profile_t> smokes(23);
	for (size_t i = 0; i < smokes.size(); i++) {
		std::strncpy(smokes[i].id, pdEffectSourceSmokeProfileId(i),
			sizeof(smokes[i].id) - 1);
	}
	effect_graph_runtime_t smokeRecord = {};
	smokeRecord.valid = 1;
	std::strcpy(smokeRecord.asset_id, "base:effect_smoke_profiles");
	smokeRecord.program.kind = EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY;
	smokeRecord.program.profiles.kind = PD_EFFECT_PROFILE_SMOKE;
	smokeRecord.program.profiles.count = smokes.size();
	smokeRecord.program.profiles.smokes = smokes.data();
	REQUIRE(effectExecutorInstallProgram(&smokeRecord, error, sizeof(error)) == 1);

	std::vector<pd_effect_explosion_profile_t> explosions(26);
	for (size_t i = 0; i < explosions.size(); i++) {
		std::strncpy(explosions[i].id, pdEffectSourceExplosionProfileId(i),
			sizeof(explosions[i].id) - 1);
		std::strncpy(explosions[i].smoke_profile,
			pdEffectSourceSmokeProfileId(i % smokes.size()),
			sizeof(explosions[i].smoke_profile) - 1);
	}
	explosions[0].has_audio = 1;
	std::strcpy(explosions[0].audio_catalog_id, "modx:blast_sound");
	effect_graph_runtime_t record = {};
	record.valid = 1;
	std::strcpy(record.asset_id, "base:effect_explosion_profiles");
	record.program.kind = EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY;
	record.program.profiles.kind = PD_EFFECT_PROFILE_EXPLOSION;
	record.program.profiles.count = explosions.size();
	record.program.profiles.explosions = explosions.data();

	for (s32 wrongCategory : { AUDIO_CAT_MUSIC, AUDIO_CAT_VOICE }) {
		testStubEffectAudio("modx:blast_sound", wrongCategory, 123);
		std::memset(error, 0, sizeof(error));
		REQUIRE(effectExecutorInstallProgram(&record, error, sizeof(error)) == 0);
		REQUIRE(std::strstr(error, "non-playable audio") != nullptr);
	}
	testStubEffectAudio("modx:blast_sound", AUDIO_CAT_SFX, 123);
	REQUIRE(effectExecutorInstallProgram(&record, error, sizeof(error)) == 1);
	ExplosionRow audioRow = {};
	REQUIRE(testStubEffectExplosionRow(0, &audioRow, sizeof(audioRow)) == 1);
	REQUIRE(audioRow.sound == 123);
	effectExecutorReset();
}

TEST_CASE("effect profile libraries gate source IDs and feed native consumers",
	"[modding][pdxxx][effect_executor][t-assets-019]")
{
	effectExecutorReset();
	char error[256] = {};

	std::vector<pd_effect_smoke_profile_t> smokes(23);
	for (size_t i = 0; i < smokes.size(); i++) {
		std::strncpy(smokes[i].id, pdEffectSourceSmokeProfileId(i),
			sizeof(smokes[i].id) - 1);
		smokes[i].duration_ticks = static_cast<s32>(100 + i);
	}
	smokes[7].fade_speed = -11;
	smokes[7].spread_interval_ticks = 13;
	smokes[7].initial_size = 17;
	smokes[7].background_rotation_speed = 1.25f;
	smokes[7].color_r = 21; smokes[7].color_g = 22; smokes[7].color_b = 23;
	smokes[7].foreground_rotation_speed = -2.5f;
	smokes[7].cloud_count = 27;
	smokes[7].size_growth_per_tick = 3.5f;
	smokes[7].rise_per_tick = -4.5f;
	smokes[7].drift_radius = 5.5f;
	effect_graph_runtime_t smokeRecord = {};
	smokeRecord.valid = 1;
	std::strcpy(smokeRecord.asset_id, "base:effect_smoke_profiles");
	smokeRecord.program.kind = EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY;
	smokeRecord.program.profiles.kind = PD_EFFECT_PROFILE_SMOKE;
	smokeRecord.program.profiles.count = smokes.size();
	smokeRecord.program.profiles.smokes = smokes.data();
	REQUIRE(effectExecutorInstallProgram(&smokeRecord, error, sizeof(error)) == 1);
	REQUIRE(effectExecutorResolveSmokeProfile(pdEffectSourceSmokeProfileId(7)) == 7);
	REQUIRE(testStubEffectSmokeDuration(7) == 107);
	std::vector<pd_effect_spark_profile_t> sparks(27);
	for (size_t i = 0; i < sparks.size(); i++) {
		std::strncpy(sparks[i].id, pdEffectSourceSparkProfileId(i),
			sizeof(sparks[i].id) - 1);
	}
	sparks[3].speed_random_range = 31;
	sparks[3].origin_offset_scale = -32;
	sparks[3].streak_width_base = 33;
	sparks[3].streak_length_base = 34;
	sparks[3].streak_width_growth_per_tick = 35;
	sparks[3].streak_length_growth_per_tick = 36;
	sparks[3].gravity_per_tick = -3.75f;
	sparks[3].max_age_ticks = 37;
	sparks[3].fade_start_tick = 38;
	sparks[3].spark_count = 39;
	sparks[3].flags = 0x12345678u;
	sparks[3].color_start_rgba = 0x11223344u;
	sparks[3].color_end_rgba = 0x55667788u;
	sparks[3].deceleration = 0.125f;
	effect_graph_runtime_t sparkRecord = {};
	sparkRecord.valid = 1;
	std::strcpy(sparkRecord.asset_id, "base:effect_spark_profiles");
	sparkRecord.program.kind = EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY;
	sparkRecord.program.profiles.kind = PD_EFFECT_PROFILE_SPARK;
	sparkRecord.program.profiles.count = sparks.size();
	sparkRecord.program.profiles.sparks = sparks.data();
	REQUIRE(effectExecutorInstallProgram(&sparkRecord, error, sizeof(error)) == 1);

	std::vector<pd_effect_explosion_profile_t> explosions(26);
	for (size_t i = 0; i < explosions.size(); i++) {
		std::strncpy(explosions[i].id, pdEffectSourceExplosionProfileId(i),
			sizeof(explosions[i].id) - 1);
		std::strncpy(explosions[i].smoke_profile,
			pdEffectSourceSmokeProfileId(i % smokes.size()),
			sizeof(explosions[i].smoke_profile) - 1);
		explosions[i].range_h = 200.0f + static_cast<float>(i);
	}
	explosions[9].range_v = 210.0f;
	explosions[9].change_rate_h = -11.5f;
	explosions[9].change_rate_v = 12.5f;
	explosions[9].inner_size = 213.0f;
	explosions[9].blast_radius = 214.0f;
	explosions[9].damage_radius = 215.0f;
	explosions[9].duration_ticks = 216;
	explosions[9].propagation_rate = -217;
	explosions[9].flare_speed = 2.25f;
	explosions[9].damage = 218.5f;
	effect_graph_runtime_t explosionRecord = {};
	explosionRecord.valid = 1;
	std::strcpy(explosionRecord.asset_id, "base:effect_explosion_profiles");
	explosionRecord.program.kind = EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY;
	explosionRecord.program.profiles.kind = PD_EFFECT_PROFILE_EXPLOSION;
	explosionRecord.program.profiles.count = explosions.size();
	explosionRecord.program.profiles.explosions = explosions.data();
	REQUIRE(effectExecutorInstallProgram(&explosionRecord, error, sizeof(error)) == 1);
	REQUIRE(effectExecutorResolveExplosionProfile(
		pdEffectSourceExplosionProfileId(9)) == 9);
	REQUIRE(testStubEffectExplosionRangeH(9) == 209.0f);
	REQUIRE(testStubEffectExplosionSmokeType(9) == 9);
	ExplosionRow explosionRow = {};
	SparkRow sparkRow = {};
	SmokeRow smokeRow = {};
	REQUIRE(testStubEffectExplosionRow(9, &explosionRow, sizeof(explosionRow)) == 1);
	REQUIRE(explosionRow.rangeh == 209.0f);
	REQUIRE(explosionRow.rangev == 210.0f);
	REQUIRE(explosionRow.changerateh == -11.5f);
	REQUIRE(explosionRow.changeratev == 12.5f);
	REQUIRE(explosionRow.innersize == 213.0f);
	REQUIRE(explosionRow.blastradius == 214.0f);
	REQUIRE(explosionRow.damageradius == 215.0f);
	REQUIRE(explosionRow.duration == 216);
	REQUIRE(explosionRow.propagationrate == -217);
	REQUIRE(explosionRow.flarespeed == 2.25f);
	REQUIRE(explosionRow.smoketype == 9);
	REQUIRE(explosionRow.sound == 0);
	REQUIRE(explosionRow.damage == 218.5f);
	REQUIRE(testStubEffectSparkRow(3, &sparkRow, sizeof(sparkRow)) == 1);
	REQUIRE(sparkRow.unk00 == 31); REQUIRE(sparkRow.unk02 == -32);
	REQUIRE(sparkRow.unk04 == 33); REQUIRE(sparkRow.unk06 == 34);
	REQUIRE(sparkRow.unk08 == 35); REQUIRE(sparkRow.unk0a == 36);
	REQUIRE(sparkRow.weight == -3.75f); REQUIRE(sparkRow.maxage == 37);
	REQUIRE(sparkRow.unk12 == 38); REQUIRE(sparkRow.numsparks == 39);
	REQUIRE(sparkRow.unk18 == 0x12345678u);
	REQUIRE(sparkRow.unk1c == 0x11223344u);
	REQUIRE(sparkRow.unk20 == 0x55667788u);
	REQUIRE(sparkRow.decel == 0.125f);
	REQUIRE(testStubEffectSmokeRow(7, &smokeRow, sizeof(smokeRow)) == 1);
	REQUIRE(smokeRow.duration == 107); REQUIRE(smokeRow.fadespeed == -11);
	REQUIRE(smokeRow.spreadspeed == 13); REQUIRE(smokeRow.size == 17);
	REQUIRE(smokeRow.bgrotatespeed == 1.25f);
	REQUIRE(smokeRow.r == 21); REQUIRE(smokeRow.g == 22); REQUIRE(smokeRow.b == 23);
	REQUIRE(smokeRow.fgrotatespeed == -2.5f);
	REQUIRE(smokeRow.numclouds == 27); REQUIRE(smokeRow.unk18 == 3.5f);
	REQUIRE(smokeRow.unk1c == -4.5f); REQUIRE(smokeRow.unk20 == 5.5f);

	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(effectGraphResolveExplosionType(pdEffectSourceExplosionProfileId(9), 4) == 9);
	REQUIRE(effectGraphResolveSparkType(pdEffectSourceSparkProfileId(3), 4) == 3);
	REQUIRE(effectGraphResolveSmokeType(pdEffectSourceSmokeProfileId(7), 4) == 7);
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(effectGraphResolveExplosionType(pdEffectSourceExplosionProfileId(9), 4)
		== EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSparkType(pdEffectSourceSparkProfileId(3), 4)
		== EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveSmokeType(pdEffectSourceSmokeProfileId(7), 4)
		== EFFECT_GRAPH_RESOLVE_FAILED);
	REQUIRE(effectGraphResolveExplosionType("", 4) == 4);
	REQUIRE(effectGraphResolveSparkType(NULL, 5) == 5);
	weaponGraphRuntimeSetEnabled(1);

	/* Removing the declared smoke owner invalidates dependent explosions;
	 * neither row family may silently fall back to native tables. */
	effectExecutorRemoveProgram("base:effect_smoke_profiles");
	REQUIRE(effectExecutorResolveSmokeProfile(pdEffectSourceSmokeProfileId(7)) == -1);
	REQUIRE(effectExecutorResolveExplosionProfile(
		pdEffectSourceExplosionProfileId(9)) == -1);
	REQUIRE(testStubEffectExplosionRangeH(9) == 0.0f);
	SmokeRow clearedSmoke = {};
	REQUIRE(testStubEffectSmokeRow(7, &clearedSmoke, sizeof(clearedSmoke)) == 1);
	REQUIRE(clearedSmoke.duration == 0);

	std::memset(error, 0, sizeof(error));
	REQUIRE(effectExecutorInstallProgram(&explosionRecord, error, sizeof(error)) == 0);
	REQUIRE(std::strstr(error, "requires the active public smoke") != nullptr);
	effectExecutorReset();
	SparkRow clearedSpark = {};
	REQUIRE(testStubEffectSparkRow(3, &clearedSpark, sizeof(clearedSpark)) == 1);
	REQUIRE(clearedSpark.unk00 == 0);
	REQUIRE(effectExecutorResolveSparkProfile(pdEffectSourceSparkProfileId(3)) == -1);
}
