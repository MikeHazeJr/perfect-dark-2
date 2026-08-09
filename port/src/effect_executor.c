/**
 * effect_executor.c -- translate validated public .pdeffect programs into the
 * native engine tables consumed by gameplay, rendering, smoke and audio.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "assetcatalog.h"
#include "effect_executor.h"
#include "effect_graph_runtime.h"
#include "game/explosions.h"
#include "game/smoke.h"
#include "game/sparks.h"
#include "pdeffect_source.h"

#define EXPLOSION_LIBRARY_ID "base:effect_explosion_profiles"
#define SPARK_LIBRARY_ID "base:effect_spark_profiles"
#define SMOKE_LIBRARY_ID "base:effect_smoke_profiles"

static struct explosiontype s_explosion_rows[EXPLOSIONTYPE_BASE_COUNT];
static struct sparktype s_spark_rows[SPARKTYPE_BASE_COUNT];
static struct smoketype s_smoke_rows[SMOKETYPE_BASE_COUNT];
static s32 s_explosion_active;
static s32 s_spark_active;
static s32 s_smoke_active;

static void setError(char *error, size_t cap, const char *message)
{
	if (error && cap) {
		snprintf(error, cap, "%s", message ? message : "effect executor failure");
	}
}

static s32 profileIndex(const char *row_id, size_t count,
	const char *(*id_for_index)(size_t))
{
	size_t i;

	if (!row_id || !row_id[0]) {
		return -1;
	}

	for (i = 0; i < count; i++) {
		const char *candidate = id_for_index(i);
		if (candidate && strcmp(candidate, row_id) == 0) {
			return (s32)i;
		}
	}

	return -1;
}

s32 effectExecutorResolveExplosionProfile(const char *row_id)
{
	return s_explosion_active
		? profileIndex(row_id, EXPLOSIONTYPE_BASE_COUNT,
			pdEffectSourceExplosionProfileId) : -1;
}

s32 effectExecutorResolveSparkProfile(const char *row_id)
{
	return s_spark_active
		? profileIndex(row_id, SPARKTYPE_BASE_COUNT,
			pdEffectSourceSparkProfileId) : -1;
}

s32 effectExecutorResolveSmokeProfile(const char *row_id)
{
	return s_smoke_active
		? profileIndex(row_id, SMOKETYPE_BASE_COUNT,
			pdEffectSourceSmokeProfileId) : -1;
}

static s32 resolveSound(const pd_effect_explosion_profile_t *source,
	u16 *sound, char *error, size_t error_cap)
{
	catalog_audio_result_t audio = {0};
	union soundnumhack native_ref;
	s32 playable_category;
	s32 resolved;

	if (!source->has_audio) {
		*sound = 0;
		return 1;
	}

	resolved = catalogResolveAudio(source->audio_catalog_id, &audio);
	native_ref.packed = (s16)audio.sound_id;
	/* A configured native sound may deliberately select an MP3/voice-backed
	 * leaf while remaining a fully playable SFX token (the stock huge
	 * explosion does this).  Keep ordinary voice lines and music out of the
	 * effect path; only the packed hasconfig token earns this exception. */
	playable_category = resolved && (audio.category == AUDIO_CAT_SFX
		|| (audio.category == AUDIO_CAT_VOICE && native_ref.hasconfig));
	if (!resolved || !playable_category
			|| audio.sound_id <= 0 || audio.sound_id > USHRT_MAX) {
		if (error && error_cap) {
			snprintf(error, error_cap,
				"explosion profile '%s' has unresolved/non-playable audio '%s'",
				source->id, source->audio_catalog_id);
		}
		return 0;
	}

	*sound = (u16)audio.sound_id;
	return 1;
}

static s32 installExplosions(const effect_graph_runtime_t *record,
	char *error, size_t error_cap)
{
	const pd_effect_profile_library_t *library = &record->program.profiles;
	size_t i;

	if (library->kind != PD_EFFECT_PROFILE_EXPLOSION
			|| library->count != EXPLOSIONTYPE_BASE_COUNT) {
		setError(error, error_cap, "explosion library must contain every base profile");
		return 0;
	}
	if (!s_smoke_active) {
		setError(error, error_cap,
			"explosion library requires the active public smoke profile library");
		return 0;
	}

	for (i = 0; i < library->count; i++) {
		const pd_effect_explosion_profile_t *source = &library->explosions[i];
		struct explosiontype *target = &s_explosion_rows[i];
		s32 smoke_type = profileIndex(source->smoke_profile,
			SMOKETYPE_BASE_COUNT, pdEffectSourceSmokeProfileId);

		if (smoke_type < 0 || !resolveSound(source, &target->sound,
				error, error_cap)) {
			if (smoke_type < 0 && error && error_cap) {
				snprintf(error, error_cap,
					"explosion profile '%s' has unknown smoke profile '%s'",
					source->id, source->smoke_profile);
			}
			return 0;
		}

		target->rangeh = source->range_h;
		target->rangev = source->range_v;
		target->changerateh = source->change_rate_h;
		target->changeratev = source->change_rate_v;
		target->innersize = source->inner_size;
		target->blastradius = source->blast_radius;
		target->damageradius = source->damage_radius;
		target->duration = (s16)source->duration_ticks;
		target->propagationrate = (s16)source->propagation_rate;
		target->flarespeed = source->flare_speed;
		target->smoketype = (u8)smoke_type;
		target->damage = source->damage;
	}

	explosionsSetProfileOverride(s_explosion_rows, EXPLOSIONTYPE_BASE_COUNT);
	s_explosion_active = 1;
	return 1;
}

static s32 installSparks(const effect_graph_runtime_t *record,
	char *error, size_t error_cap)
{
	const pd_effect_profile_library_t *library = &record->program.profiles;
	size_t i;

	if (library->kind != PD_EFFECT_PROFILE_SPARK
			|| library->count != SPARKTYPE_BASE_COUNT) {
		setError(error, error_cap, "spark library must contain every base profile");
		return 0;
	}

	for (i = 0; i < library->count; i++) {
		const pd_effect_spark_profile_t *source = &library->sparks[i];
		struct sparktype *target = &s_spark_rows[i];

		target->unk00 = (u16)source->speed_random_range;
		target->unk02 = (s16)source->origin_offset_scale;
		target->unk04 = (u16)source->streak_width_base;
		target->unk06 = (u16)source->streak_length_base;
		target->unk08 = (u16)source->streak_width_growth_per_tick;
		target->unk0a = (u16)source->streak_length_growth_per_tick;
		target->weight = source->gravity_per_tick;
		target->maxage = (u16)source->max_age_ticks;
		target->unk12 = (u16)source->fade_start_tick;
		target->numsparks = (u16)source->spark_count;
		target->unk18 = source->flags;
		target->unk1c = source->color_start_rgba;
		target->unk20 = source->color_end_rgba;
		target->decel = source->deceleration;
	}

	sparksSetBaseProfileOverride(s_spark_rows, SPARKTYPE_BASE_COUNT);
	s_spark_active = 1;
	return 1;
}

static s32 installSmoke(const effect_graph_runtime_t *record,
	char *error, size_t error_cap)
{
	const pd_effect_profile_library_t *library = &record->program.profiles;
	size_t i;

	if (library->kind != PD_EFFECT_PROFILE_SMOKE
			|| library->count != SMOKETYPE_BASE_COUNT) {
		setError(error, error_cap, "smoke library must contain every base profile");
		return 0;
	}

	for (i = 0; i < library->count; i++) {
		const pd_effect_smoke_profile_t *source = &library->smokes[i];
		struct smoketype *target = &s_smoke_rows[i];

		target->duration = (s16)source->duration_ticks;
		target->fadespeed = (s16)source->fade_speed;
		target->spreadspeed = (s16)source->spread_interval_ticks;
		target->size = (s16)source->initial_size;
		target->bgrotatespeed = source->background_rotation_speed;
		target->r = source->color_r;
		target->g = source->color_g;
		target->b = source->color_b;
		target->fgrotatespeed = source->foreground_rotation_speed;
		target->numclouds = (s16)source->cloud_count;
		target->unk18 = source->size_growth_per_tick;
		target->unk1c = source->rise_per_tick;
		target->unk20 = source->drift_radius;
	}

	smokesSetProfileOverride(s_smoke_rows, SMOKETYPE_BASE_COUNT);
	s_smoke_active = 1;
	return 1;
}

s32 effectExecutorInstallProgram(const struct effect_graph_runtime *record,
	char *error, size_t error_cap)
{
	if (error && error_cap) {
		error[0] = '\0';
	}
	if (!record || !record->valid) {
		setError(error, error_cap, "effect program is not valid");
		return 0;
	}
	if (record->program.kind != EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY) {
		return 1;
	}

	if (strcmp(record->asset_id, EXPLOSION_LIBRARY_ID) == 0) {
		return installExplosions(record, error, error_cap);
	}
	if (strcmp(record->asset_id, SPARK_LIBRARY_ID) == 0) {
		return installSparks(record, error, error_cap);
	}
	if (strcmp(record->asset_id, SMOKE_LIBRARY_ID) == 0) {
		return installSmoke(record, error, error_cap);
	}

	setError(error, error_cap, "profile library has no production consumer");
	return 0;
}

void effectExecutorRemoveProgram(const char *asset_id)
{
	if (!asset_id) {
		return;
	}
	if (strcmp(asset_id, EXPLOSION_LIBRARY_ID) == 0) {
		explosionsClearProfileOverride();
		s_explosion_active = 0;
	} else if (strcmp(asset_id, SPARK_LIBRARY_ID) == 0) {
		sparksClearBaseProfileOverride();
		s_spark_active = 0;
	} else if (strcmp(asset_id, SMOKE_LIBRARY_ID) == 0) {
		smokesClearProfileOverride();
		s_smoke_active = 0;
		/* Explosion rows declare smoke row IDs. Removing their owner invalidates
		 * the dependent production table immediately; native smoke is not a
		 * legal fallback for authored public-source dependencies. */
		explosionsClearProfileOverride();
		s_explosion_active = 0;
	}
}

void effectExecutorReset(void)
{
	explosionsClearProfileOverride();
	sparksClearBaseProfileOverride();
	smokesClearProfileOverride();
	s_explosion_active = 0;
	s_spark_active = 0;
	s_smoke_active = 0;
}
