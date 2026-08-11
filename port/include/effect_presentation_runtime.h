/**
 * effect_presentation_runtime.h -- renderer-facing public .pdeffect consumer.
 *
 * The consumer compiles presentation fields into immutable screen or world
 * commands. Commands are published only by the shared effect instance
 * transaction; the renderer never falls back to native literals.
 */
#ifndef _IN_EFFECT_PRESENTATION_RUNTIME_H
#define _IN_EFFECT_PRESENTATION_RUNTIME_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "effect_instance_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum effect_presentation_kind {
	EFFECT_PRESENTATION_TINT = 1,
	EFFECT_PRESENTATION_GLOW,
	EFFECT_PRESENTATION_SHIMMER,
	EFFECT_PRESENTATION_DARKEN,
	EFFECT_PRESENTATION_SCREEN
} effect_presentation_kind_t;

typedef enum effect_presentation_channel {
	EFFECT_PRESENTATION_CHANNEL_SCREEN = 1,
	EFFECT_PRESENTATION_CHANNEL_DECAL,
	EFFECT_PRESENTATION_CHANNEL_LIGHT,
	EFFECT_PRESENTATION_CHANNEL_BEAM
} effect_presentation_channel_t;

typedef struct effect_presentation_command {
	u64 instance_id;
	effect_presentation_kind_t kind;
	effect_presentation_channel_t channel;
	s32 execution_index;
	s32 priority;
	f32 time;
	f32 rgba[4];
	f32 secondary_rgba[4];
	/* Durable source provenance retained through prepare. rgba is the exact
	 * prepared product of this tint and the hydrated material base color. */
	f32 authored_tint[4];
	f32 authored_secondary_tint[4];
	char material_ref[CATALOG_ID_LEN];
	char texture_ref[CATALOG_ID_LEN];
	char shader_id[128];
	s32 texture_num;
	char material_shading_model[32];
	f32 material_roughness;
	f32 material_metallic;
	s32 material_emissive;
	char target_name[EFFECT_INSTANCE_NAME_LEN];
	char attachment[EFFECT_INSTANCE_NAME_LEN];
	effect_instance_vec3_t position;
	effect_instance_vec3_t source_position;
	effect_instance_vec3_t target_position;
	effect_instance_vec3_t primary_direction;
	effect_instance_vec3_t secondary_direction;
	s16 rooms[EFFECT_INSTANCE_ROOM_CAP];
	s32 room_count;
	f32 intensity;
	f32 glow;
	/* World size, screen-band fraction, or beam width according to channel. */
	f32 size;
	f32 speed;
	f32 width;
} effect_presentation_command_t;

/* Activation and sibling consumers share this exact fixed-pipeline shader
 * vocabulary. Unknown identifiers are rejected rather than retained inert. */
s32 effectPresentationShaderSupported(const char *node_kind,
	const char *shader_id);
void effectPresentationMaterialColor(const f32 source_rgba[4],
	const char *shading_model, f32 roughness, f32 metallic, s32 emissive,
	f32 out_rgba[4]);

/* The returned snapshot is copied, so catalog reload/instance cleanup cannot
 * leave the renderer holding an invalid pointer. */
size_t effectPresentationRuntimeSnapshotCount(void);
s32 effectPresentationRuntimeSnapshot(size_t index,
	effect_presentation_command_t *out);
s32 effectPresentationRuntimeHasActive(void);
void effectPresentationRuntimeClearAll(void);

/* Called from the real ImGui renderer after NewFrame. Only SCREEN commands
 * are consumed here; world commands are consumed by the GBI bridge. */
void effectPresentationRuntimeRender(s32 width, s32 height);

#ifdef __cplusplus
}
#endif

#endif /* _IN_EFFECT_PRESENTATION_RUNTIME_H */
