/**
 * effect_gameplay_runtime.h -- source-backed v1 .pdeffect gameplay consumer.
 */
#ifndef _IN_EFFECT_GAMEPLAY_RUNTIME_H
#define _IN_EFFECT_GAMEPLAY_RUNTIME_H

#include <stddef.h>

#include "assetcatalog.h"
#include "effect_instance_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

struct prop;
struct coord;

typedef struct effect_gameplay_particle_snapshot {
	u64 instance_id;
	s32 node_index;
	s32 execution_index;
	char texture_id[CATALOG_ID_LEN];
	char material_id[CATALOG_ID_LEN];
	char material_shading_model[32];
	char shader_id[128];
	s32 texture_num;
	effect_instance_vec3_t position;
	f32 size;
	f32 tint[4];
	f32 glow;
	f32 material_roughness;
	f32 material_metallic;
	s32 material_emissive;
	f32 intensity;
	f32 age;
	f32 lifetime;
} effect_gameplay_particle_snapshot_t;

/* Called by the T-ASSETS-036 facade. The builder claims only particle,
 * explosion, spark, and smoke; catalog audio is staged from those nodes. */
s32 effectGameplayRuntimeBuildConsumer(effect_instance_consumer_t *out,
	char *err, size_t err_cap);

/* Read-only committed particle state for the T-ASSETS-035 world renderer.
 * Snapshots contain catalog-owned texture slots and exact hydrated material
 * projections; no native/ROM fallback identity is ever returned. */
size_t effectGameplayRuntimeParticleCount(void);
s32 effectGameplayRuntimeParticleSnapshot(size_t index,
	effect_gameplay_particle_snapshot_t *out);

/* Selected-source production callsite bridges. Empty refs alone may use the
 * explicit native fallback. Nonempty v1 refs execute the complete instance;
 * active v2 profile-row refs stay on their public scalar executor. */
s32 effectGameplayRuntimeTriggerPropExplosion(const char *effect_ref,
	struct prop *prop, s32 fallback_explosion_type);
s32 effectGameplayRuntimeTriggerSpark(const char *effect_ref,
	s32 fallback_spark_type, s32 room, struct prop *prop,
	const struct coord *position, const struct coord *primary_direction,
	const struct coord *secondary_direction);

#ifdef __cplusplus
}
#endif

#endif /* _IN_EFFECT_GAMEPLAY_RUNTIME_H */
