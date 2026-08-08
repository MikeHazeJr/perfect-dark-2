#ifndef _IN_PROP_GRAPH_RUNTIME_H
#define _IN_PROP_GRAPH_RUNTIME_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct prop_graph_runtime_target {
	void *context;
	s32 (*is_enabled)(void *context);
	void (*set_enabled)(void *context, s32 enabled);
	void (*set_health)(void *context, f32 health);
	void (*set_collision)(void *context, s32 enabled);
	void (*set_channel)(void *context, const char *channel, s32 enabled);
} prop_graph_runtime_target_t;

/* Compiles and retains the public behavior.graph.json for one .pdprop.
 * Registration is fail-closed: unsupported modules, invalid topology,
 * catalog identity mismatches, or graphs without a production event root
 * are rejected. */
s32 propGraphRuntimeRegisterJson(const char *asset_id, const char *json,
		u32 json_size, char *err, size_t err_cap);
void propGraphRuntimeClearAsset(const char *asset_id);
void propGraphRuntimeReset(void);

/* Forge production lifecycle integration. */
s32 propGraphRuntimeBind(const char *asset_id, u32 forge_uid,
		const prop_graph_runtime_target_t *target);
void propGraphRuntimeUnbindAll(void);
void propGraphRuntimeTick(void);
s32 propGraphRuntimeHasAsset(const char *asset_id);
s32 propGraphRuntimeInstanceCount(void);

#ifdef __cplusplus
}
#endif

#endif
