#ifndef PD_EFFECT_SOURCE_H
#define PD_EFFECT_SOURCE_H

#include <stddef.h>

#include <PR/ultratypes.h>
#include "fs.h"

struct explosiontype;
struct sparktype;
struct smoketype;

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pd_effect_sound_id_fn)(s32 sound_ref, char *out, size_t out_n);

#define PD_EFFECT_SOURCE_SCHEMA_V1 "pd.effect_graph.v1"
#define PD_EFFECT_SOURCE_SCHEMA_V2 "pd.effect_graph.v2"

typedef enum pd_effect_source_format {
	PD_EFFECT_SOURCE_FORMAT_LEGACY_GRAPH = 1,
	PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY = 2
} pd_effect_source_format_t;

typedef enum pd_effect_profile_kind {
	PD_EFFECT_PROFILE_NONE = 0,
	PD_EFFECT_PROFILE_EXPLOSION,
	PD_EFFECT_PROFILE_SPARK,
	PD_EFFECT_PROFILE_SMOKE
} pd_effect_profile_kind_t;

/* Normalized public-source identity. This deliberately contains no runtime
 * executor state: T-ASSETS-018 owns conversion of v2 profile rows into the
 * native effect tables. has_audio_rows + silent_audio_rows preserve explicit
 * catalog-ID versus null audio semantics for that later conversion. */
typedef struct pd_effect_source_info {
	pd_effect_source_format_t format;
	pd_effect_profile_kind_t profile_kind;
	char catalog_id[128];
	char name[128];
	/* Public archive-member paths follow the same capacity contract as every
	 * catalog/provider path. A 256-byte mirror silently truncated otherwise
	 * valid nested .pdeffect sources before archive lookup (B-1047). */
	char effect_file[FS_MAXPATH];
	char timeline_file[FS_MAXPATH];
	char effect_key[64];
	char target_key[64];
	char shader_id[128];
	float intensity;
	size_t profile_count;
	size_t has_audio_rows;
	size_t silent_audio_rows;
} pd_effect_source_info_t;

/* T-ASSETS-018: decoded, validated profile-library rows. These are public
 * source values, not renderer-owned products. Catalog audio identity stays a
 * string until the production consumer resolves it. */
typedef struct pd_effect_explosion_profile {
	char id[64];
	float range_h, range_v, change_rate_h, change_rate_v;
	float inner_size, blast_radius, damage_radius;
	s32 duration_ticks, propagation_rate;
	float flare_speed, damage;
	char smoke_profile[64];
	s32 has_audio;
	char audio_catalog_id[128];
} pd_effect_explosion_profile_t;

typedef struct pd_effect_spark_profile {
	char id[64];
	u32 speed_random_range;
	s32 origin_offset_scale;
	u32 streak_width_base, streak_length_base;
	u32 streak_width_growth_per_tick, streak_length_growth_per_tick;
	float gravity_per_tick;
	u32 max_age_ticks, fade_start_tick, spark_count, flags;
	u32 color_start_rgba, color_end_rgba;
	float deceleration;
} pd_effect_spark_profile_t;

typedef struct pd_effect_smoke_profile {
	char id[64];
	s32 duration_ticks, fade_speed, spread_interval_ticks, initial_size;
	float background_rotation_speed;
	u8 color_r, color_g, color_b;
	float foreground_rotation_speed;
	s32 cloud_count;
	float size_growth_per_tick, rise_per_tick, drift_radius;
} pd_effect_smoke_profile_t;

typedef struct pd_effect_profile_library {
	pd_effect_profile_kind_t kind;
	size_t count;
	union {
		pd_effect_explosion_profile_t *explosions;
		pd_effect_spark_profile_t *sparks;
		pd_effect_smoke_profile_t *smokes;
		void *rows;
	};
} pd_effect_profile_library_t;

typedef struct pd_effect_timeline_key {
	float time;
	char property[64];
	float value;
	size_t authored_order;
} pd_effect_timeline_key_t;

typedef struct pd_effect_timeline {
	pd_effect_timeline_key_t *keys;
	size_t count;
} pd_effect_timeline_t;

/* Authoritative public .pdeffect parser. Descriptor and graph are validated
 * together so catalog identity and the declared archive member cannot drift.
 * v2 rejects every unknown/duplicate/omitted field and profile row. v1 keeps
 * the existing graph compiler as executor but validates descriptor identity,
 * schema and member references before it is reached. */
s32 pdEffectSourceParse(const char *descriptor, size_t descriptor_len,
	const char *graph, size_t graph_len, const char *expected_catalog_id,
	pd_effect_source_info_t *out, char *error, size_t error_cap);

s32 pdEffectSourceParseArchiveFile(const char *archive_path,
	const char *expected_catalog_id, pd_effect_source_info_t *out,
	char *error, size_t error_cap);
s32 pdEffectSourceParseArchiveBytes(const void *archive_bytes,
	u32 archive_size, const char *expected_catalog_id,
	pd_effect_source_info_t *out, char *error, size_t error_cap);

s32 pdEffectSourceDecodeProfileLibrary(const char *graph, size_t graph_len,
	const pd_effect_source_info_t *source,
	pd_effect_profile_library_t *out, char *error, size_t error_cap);
void pdEffectSourceFreeProfileLibrary(pd_effect_profile_library_t *library);

s32 pdEffectTimelineParse(const char *json, size_t json_len,
	pd_effect_timeline_t *out, char *error, size_t error_cap);
void pdEffectTimelineFree(pd_effect_timeline_t *timeline);
s32 pdEffectTimelineSample(const pd_effect_timeline_t *timeline,
	const char *property, float time, float *out_value);

/* Build the canonical public profile-graph source for the three native base
 * effect tables. The returned UTF-8 buffer is malloc-owned by the caller. */
s32 pdEffectSourceBuildExplosionGraph(const char *catalog_id,
	const struct explosiontype *rows, size_t row_count,
	pd_effect_sound_id_fn sound_id_fn, char **out, size_t *out_len);
s32 pdEffectSourceBuildSparkGraph(const char *catalog_id,
	const struct sparktype *rows, size_t row_count, char **out, size_t *out_len);
s32 pdEffectSourceBuildSmokeGraph(const char *catalog_id,
	const struct smoketype *rows, size_t row_count, char **out, size_t *out_len);

const char *pdEffectSourceExplosionProfileId(size_t index);
const char *pdEffectSourceSparkProfileId(size_t index);
const char *pdEffectSourceSmokeProfileId(size_t index);
s32 pdEffectCatalogIdValid(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif
