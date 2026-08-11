/**
 * effect_instance_runtime.h -- production v1 .pdeffect orchestration.
 *
 * The instance facade owns authored topology selection, context/target
 * resolution, timeline time, lifetime, deterministic ordering, and the global
 * transaction shared by gameplay/audio and presentation consumers. Consumers
 * stage work in begin/node/update, validate it in prepare, and publish it only
 * from the infallible commit callback after every lane prepared successfully.
 */
#ifndef _IN_EFFECT_INSTANCE_RUNTIME_H
#define _IN_EFFECT_INSTANCE_RUNTIME_H

#include <stddef.h>
#include <PR/ultratypes.h>

#include "effect_graph_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors struct prop.rooms[8], including its existing RoomNum list domain;
 * this is not a new public graph/context ceiling. */
#define EFFECT_INSTANCE_ROOM_CAP 8
#define EFFECT_INSTANCE_NAME_LEN WEAPON_GRAPH_IR_ID_LEN

typedef enum effect_instance_action_mode {
	EFFECT_INSTANCE_ACTION_POINT_EMITTER = 0,
	EFFECT_INSTANCE_ACTION_PROP_DETONATION = 1
} effect_instance_action_mode_t;

typedef struct effect_instance_vec3 {
	f32 x;
	f32 y;
	f32 z;
} effect_instance_vec3_t;

typedef struct effect_instance_context_value {
	char name[EFFECT_INSTANCE_NAME_LEN];
	char type[EFFECT_INSTANCE_NAME_LEN];
	effect_instance_vec3_t vec3;
	f32 number;
	void *entity;
} effect_instance_context_value_t;

typedef s32 (*effect_instance_entity_alive_fn)(const void *entity, void *user);

typedef struct effect_instance_request {
	const char *asset_id;
	/* Optional authored roots. At most one may be non-empty. An export starts
	 * at its exported node; a subgraph starts at its entry and remains inside
	 * that subgraph. Empty selects the complete graph. */
	const char *export_name;
	const char *subgraph_id;

	/* Callsite-owned pointers are never dereferenced by the facade. Consumers
	 * may use target_prop synchronously; copied spatial data is authoritative
	 * for updates and retained instances. */
	void *source_prop;
	void *target_prop;
	effect_instance_vec3_t source_position;
	effect_instance_vec3_t target_position;
	effect_instance_vec3_t position;
	effect_instance_vec3_t primary_direction;
	effect_instance_vec3_t secondary_direction;
	s16 rooms[EFFECT_INSTANCE_ROOM_CAP];
	s32 room_count;
	s32 playernum;
	effect_instance_action_mode_t action_mode;

	/* Explicit named call contexts override source-derived values. */
	const effect_instance_context_value_t *contexts;
	size_t context_count;
	effect_instance_entity_alive_fn source_alive;
	effect_instance_entity_alive_fn target_alive;
	void *liveness_user;
} effect_instance_request_t;

typedef struct effect_instance_frame {
	u64 instance_id;
	const effect_graph_runtime_t *runtime;
	const weapon_graph_ir_node_t *node;
	s32 node_index;
	s32 execution_index;
	f32 time;
	f32 delta_time;
	f32 lifetime;
	s32 priority;
	const char *target_name;
	const char *attachment;
	void *source_prop;
	void *target_prop;
	effect_instance_vec3_t position;
	effect_instance_vec3_t source_position;
	effect_instance_vec3_t target_position;
	effect_instance_vec3_t primary_direction;
	effect_instance_vec3_t secondary_direction;
	s16 rooms[EFFECT_INSTANCE_ROOM_CAP];
	s32 room_count;
	s32 playernum;
	effect_instance_action_mode_t action_mode;
	const effect_instance_context_value_t *contexts;
	size_t context_count;
} effect_instance_frame_t;

typedef s32 (*effect_instance_phase_fn)(const effect_instance_frame_t *frame,
	char *err, size_t err_cap);
typedef s32 (*effect_instance_node_fn)(const effect_instance_frame_t *frame,
	char *err, size_t err_cap);
typedef void (*effect_instance_commit_fn)(const effect_instance_frame_t *frame);
typedef void (*effect_instance_rollback_fn)(const effect_instance_frame_t *frame,
	s32 attempted_count);
typedef void (*effect_instance_cleanup_fn)(u64 instance_id);

typedef struct effect_instance_consumer {
	const char *name;
	effect_instance_phase_fn begin;
	effect_instance_phase_fn prepare;
	effect_instance_commit_fn commit;
	effect_instance_rollback_fn rollback;
	effect_instance_phase_fn update;
	effect_instance_cleanup_fn cleanup;
	/* NULL means this lane does not observe the slot. One or more installed
	 * lanes may observe a slot; the facade invokes all observers in stable
	 * lane order inside the same global transaction. */
	effect_instance_node_fn nodes[EFFECT_GRAPH_DISPATCH_SLOT_COUNT];
} effect_instance_consumer_t;

/* One canonical activation schema assigns every accepted public node field to
 * at least one production lane. These flags are intentionally queryable so
 * conformance tests and authoring UI cannot drift from runtime validation. */
typedef enum effect_instance_param_owner {
	EFFECT_INSTANCE_PARAM_UNSUPPORTED = 0,
	EFFECT_INSTANCE_PARAM_ORCHESTRATION = 1,
	EFFECT_INSTANCE_PARAM_GAMEPLAY = 2,
	EFFECT_INSTANCE_PARAM_PRESENTATION = 4
} effect_instance_param_owner_t;

/* Implemented by the sibling consumer modules. Builders must initialize the
 * entire output record and claim only their owned slots. */
s32 effectGameplayRuntimeBuildConsumer(effect_instance_consumer_t *out,
	char *err, size_t err_cap);
s32 effectPresentationRuntimeBuildConsumer(effect_instance_consumer_t *out,
	char *err, size_t err_cap);

/* Spawn is all-or-nothing. A successful one-shot may already be cleaned up;
 * its nonzero ID still uniquely identifies the committed action. */
s32 effectInstanceRuntimeSpawn(const effect_instance_request_t *request,
	u64 *instance_id_out, char *err, size_t err_cap);
/* Activation-time semantic validation prevents catalog-visible programs that
 * could only fail once a production callsite tries to spawn them. */
s32 effectInstanceProgramValidate(const effect_graph_program_t *program,
	const char *descriptor_target, const char *descriptor_shader,
	char *err, size_t err_cap);
effect_instance_param_owner_t effectInstanceParamOwner(
	weapon_graph_opcode_e opcode, const char *key);
size_t effectInstanceParamSchemaCount(void);
s32 effectInstanceParamSchemaEntry(size_t index, weapon_graph_opcode_e *opcode,
	const char **key, effect_instance_param_owner_t *owner);
void effectInstanceRuntimeTick(f32 delta_seconds);
void effectInstanceRuntimeCancelAsset(const char *asset_id);
/* Prop/entity teardown calls this before storage can be recycled. */
void effectInstanceRuntimeCancelEntity(const void *entity);
void effectInstanceRuntimeClearAll(void);
size_t effectInstanceRuntimeCount(void);

/* Focused orchestration tests may replace the two production builders with
 * complete deterministic consumers. Passing NULL restores production. */
void effectInstanceRuntimeSetTestConsumers(
	const effect_instance_consumer_t *consumers, s32 consumer_count);

/* Helpers for consumers. They never silently coerce a wrong field type. */
const weapon_graph_ir_param_t *effectInstanceNodeParam(
	const effect_instance_frame_t *frame, const char *key);
const effect_instance_context_value_t *effectInstanceContext(
	const effect_instance_frame_t *frame, const char *name);
s32 effectInstanceTimelineSample(const effect_instance_frame_t *frame,
	const char *property, f32 *out_value);

#ifdef __cplusplus
}
#endif

#endif /* _IN_EFFECT_INSTANCE_RUNTIME_H */
