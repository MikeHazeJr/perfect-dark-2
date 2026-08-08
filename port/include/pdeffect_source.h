#ifndef PD_EFFECT_SOURCE_H
#define PD_EFFECT_SOURCE_H

#include <stddef.h>

#include <PR/ultratypes.h>

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
	char effect_file[256];
	char timeline_file[256];
	char effect_key[64];
	char target_key[64];
	char shader_id[128];
	float intensity;
	size_t profile_count;
	size_t has_audio_rows;
	size_t silent_audio_rows;
} pd_effect_source_info_t;

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

#ifdef __cplusplus
}
#endif

#endif
