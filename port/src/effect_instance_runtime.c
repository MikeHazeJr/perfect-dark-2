/**
 * effect_instance_runtime.c -- production v1 .pdeffect instance facade.
 */

#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "effect_instance_runtime.h"
#include "effect_presentation_runtime.h"
#include "system.h"

typedef struct effect_instance_record {
	effect_instance_frame_t frame;
	effect_instance_vec3_t origin_position;
	effect_instance_context_value_t *contexts;
	char asset_id[CATALOG_ID_LEN];
	char source_sha256[SHA256_HEX_SIZE];
	u8 *active_nodes;
	effect_instance_consumer_t consumers[2];
	s32 consumer_count;
	s32 sort_priority;
	s32 committed;
	effect_instance_entity_alive_fn source_alive;
	effect_instance_entity_alive_fn target_alive;
	void *liveness_user;
} effect_instance_record_t;

typedef struct effect_instance_dispatch_state {
	effect_instance_record_t *instance;
	s32 begun_count;
	s32 attempted_count;
} effect_instance_dispatch_state_t;

static effect_instance_record_t **s_instances;
static size_t s_instance_count;
static size_t s_instance_capacity;
static u64 s_next_instance_id = 1;
static effect_instance_consumer_t s_test_consumers[2];
static s32 s_test_consumer_count;
static s32 s_audit_enabled;

void effectInstanceRuntimeSetAuditEnabled(s32 enabled)
{
	s_audit_enabled = enabled ? 1 : 0;
}

s32 effectInstanceRuntimeAuditEnabled(void)
{
	return s_audit_enabled;
}

static void setErr(char *err, size_t err_cap, const char *fmt, ...)
{
	va_list ap;
	if (!err || err_cap == 0) return;
	va_start(ap, fmt);
	vsnprintf(err, err_cap, fmt, ap);
	va_end(ap);
	err[err_cap - 1] = '\0';
}

static void copyStr(char *dst, size_t cap, const char *src)
{
	if (!dst || cap == 0) return;
	if (!src) src = "";
	strncpy(dst, src, cap - 1);
	dst[cap - 1] = '\0';
}

static s32 finiteFloat(f32 value)
{
	return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

const weapon_graph_ir_param_t *effectInstanceNodeParam(
	const effect_instance_frame_t *frame, const char *key)
{
	if (!frame || !frame->runtime || !frame->node || !key) return NULL;
	const effect_graph_program_t *program = &frame->runtime->program;
	if (frame->node->param_start < 0 || frame->node->param_count < 0 ||
			frame->node->param_start > program->param_count - frame->node->param_count) {
		return NULL;
	}
	for (s32 i = 0; i < frame->node->param_count; i++) {
		const weapon_graph_ir_param_t *param =
			&program->params[frame->node->param_start + i];
		if (strcmp(param->key, key) == 0) return param;
	}
	return NULL;
}

const effect_instance_context_value_t *effectInstanceContext(
	const effect_instance_frame_t *frame, const char *name)
{
	if (!frame || !name || !name[0]) return NULL;
	for (size_t i = 0; i < frame->context_count; i++) {
		if (strcmp(frame->contexts[i].name, name) == 0) return &frame->contexts[i];
	}
	return NULL;
}

s32 effectInstanceTimelineSample(const effect_instance_frame_t *frame,
	const char *property, f32 *out_value)
{
	return frame && frame->runtime
		? effectGraphProgramSample(&frame->runtime->program, property,
			frame->time, out_value) : 0;
}

static const weapon_graph_ir_param_t *nodeParam(
	const effect_graph_program_t *program, const weapon_graph_ir_node_t *node,
	const char *key)
{
	if (!program || !node || !key || node->param_start < 0 ||
			node->param_count < 0 ||
			node->param_start > program->param_count - node->param_count) return NULL;
	for (s32 i = 0; i < node->param_count; i++) {
		const weapon_graph_ir_param_t *param = &program->params[node->param_start + i];
		if (strcmp(param->key, key) == 0) return param;
	}
	return NULL;
}

static s32 numericParam(const weapon_graph_ir_param_t *param, f32 *value)
{
	if (!param || !value) return 0;
	if (param->type == WEAPON_GRAPH_PARAM_FLOAT) *value = param->f_value;
	else if (param->type == WEAPON_GRAPH_PARAM_INT) *value = (f32)param->i_value;
	else return -1;
	return finiteFloat(*value) ? 1 : -1;
}

static s32 knownName(const char *value, const char *const *names, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (strcmp(value, names[i]) == 0) return 1;
	}
	return 0;
}

static s32 effectTargetIsNamedVec3(const effect_graph_program_t *program,
	const char *target)
{
	if (!program || !target) return 0;
	for (s32 i = 0; i < program->context_count; i++) {
		if (strcmp(program->contexts[i].name, target) == 0) {
			return strcmp(program->contexts[i].type, "vec3") == 0;
		}
	}
	return 0;
}

typedef enum effect_param_rule {
	EFFECT_PARAM_STRING = 1,
	EFFECT_PARAM_CATALOG_ID,
	EFFECT_PARAM_COLOR,
	EFFECT_PARAM_NUMBER_FINITE,
	EFFECT_PARAM_NUMBER_NONNEGATIVE,
	EFFECT_PARAM_NUMBER_POSITIVE,
	EFFECT_PARAM_INTEGER,
	EFFECT_PARAM_SMOKE_TYPE,
	EFFECT_PARAM_EXPLOSION_CLASS,
	EFFECT_PARAM_SMOKE_CLASS,
	EFFECT_PARAM_SHADER
} effect_param_rule_t;

typedef struct effect_param_schema {
	weapon_graph_opcode_e opcode;
	const char *key;
	effect_param_rule_t rule;
	effect_instance_param_owner_t owner;
} effect_param_schema_t;

#define EFFECT_COMMON_OPCODE ((weapon_graph_opcode_e)0)
#define ORCH EFFECT_INSTANCE_PARAM_ORCHESTRATION
#define GAME EFFECT_INSTANCE_PARAM_GAMEPLAY
#define PRES EFFECT_INSTANCE_PARAM_PRESENTATION

/* This is the single accepted public v1 node vocabulary. A field is absent
 * from this table until a production lane consumes it without coercion. */
static const effect_param_schema_t kEffectParamSchema[] = {
	{ EFFECT_COMMON_OPCODE, "target", EFFECT_PARAM_STRING, ORCH },
	{ EFFECT_COMMON_OPCODE, "attachment", EFFECT_PARAM_STRING, ORCH },
	{ EFFECT_COMMON_OPCODE, "priority", EFFECT_PARAM_INTEGER, ORCH },
	{ EFFECT_COMMON_OPCODE, "lifetime", EFFECT_PARAM_NUMBER_NONNEGATIVE, ORCH },

	{ WEAPON_GRAPH_OP_EFFECT_TINT, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_TINT, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_TINT, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_TINT, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_TINT, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_GLOW, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_GLOW, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_GLOW, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_GLOW, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_GLOW, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "speed", EFFECT_PARAM_NUMBER_FINITE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "width", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SHIMMER, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_DARKEN, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_DARKEN, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_DARKEN, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_DARKEN, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_DARKEN, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SCREEN, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SCREEN, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SCREEN, "texture_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SCREEN, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SCREEN, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SCREEN, "shader", EFFECT_PARAM_SHADER, PRES },

	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "texture_ref", EFFECT_PARAM_CATALOG_ID, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "material_ref", EFFECT_PARAM_CATALOG_ID, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "tint", EFFECT_PARAM_COLOR, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "size", EFFECT_PARAM_NUMBER_POSITIVE, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "glow", EFFECT_PARAM_NUMBER_NONNEGATIVE, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "audio_catalog_id", EFFECT_PARAM_CATALOG_ID, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_PARTICLE, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "explosion_class", EFFECT_PARAM_EXPLOSION_CLASS, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "audio_catalog_id", EFFECT_PARAM_CATALOG_ID, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "glow", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "tint", EFFECT_PARAM_COLOR, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "tint2", EFFECT_PARAM_COLOR, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "tint_secondary", EFFECT_PARAM_COLOR, GAME | PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "audio_catalog_id", EFFECT_PARAM_CATALOG_ID, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "glow", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "shader", EFFECT_PARAM_SHADER, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "smoke_class", EFFECT_PARAM_SMOKE_CLASS, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "class", EFFECT_PARAM_SMOKE_CLASS, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "smoke_type", EFFECT_PARAM_SMOKE_TYPE, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "audio_catalog_id", EFFECT_PARAM_CATALOG_ID, GAME },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "tint", EFFECT_PARAM_COLOR, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "material_ref", EFFECT_PARAM_CATALOG_ID, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "intensity", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "size", EFFECT_PARAM_NUMBER_POSITIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "glow", EFFECT_PARAM_NUMBER_NONNEGATIVE, PRES },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "shader", EFFECT_PARAM_SHADER, PRES },
};

#undef ORCH
#undef GAME
#undef PRES

static const effect_param_schema_t *effectParamSchemaFind(
	weapon_graph_opcode_e opcode, const char *key)
{
	const effect_param_schema_t *common = NULL;
	if (opcode < WEAPON_GRAPH_OP_EFFECT_TINT ||
			opcode > WEAPON_GRAPH_OP_EFFECT_SMOKE) return NULL;
	for (size_t i = 0; i < sizeof(kEffectParamSchema) / sizeof(kEffectParamSchema[0]); i++) {
		const effect_param_schema_t *entry = &kEffectParamSchema[i];
		if (strcmp(entry->key, key) != 0) continue;
		if (entry->opcode == opcode) return entry;
		if (entry->opcode == EFFECT_COMMON_OPCODE) common = entry;
	}
	return common;
}

effect_instance_param_owner_t effectInstanceParamOwner(
	weapon_graph_opcode_e opcode, const char *key)
{
	const effect_param_schema_t *entry = key ? effectParamSchemaFind(opcode, key) : NULL;
	return entry ? entry->owner : EFFECT_INSTANCE_PARAM_UNSUPPORTED;
}

size_t effectInstanceParamSchemaCount(void)
{
	return sizeof(kEffectParamSchema) / sizeof(kEffectParamSchema[0]);
}

s32 effectInstanceParamSchemaEntry(size_t index, weapon_graph_opcode_e *opcode,
	const char **key, effect_instance_param_owner_t *owner)
{
	if (index >= effectInstanceParamSchemaCount() || !opcode || !key || !owner)
		return 0;
	*opcode = kEffectParamSchema[index].opcode;
	*key = kEffectParamSchema[index].key;
	*owner = kEffectParamSchema[index].owner;
	return 1;
}

static s32 effectColorParamValid(const weapon_graph_ir_param_t *param)
{
	const char *cursor;
	s32 count = 0;
	if (param->type != WEAPON_GRAPH_PARAM_ARRAY) return 0;
	cursor = param->value;
	while (*cursor) {
		char *end = NULL;
		double value;
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
				*cursor == '\n' || *cursor == '[' || *cursor == ']' ||
				*cursor == ',') cursor++;
		if (!*cursor) break;
		if (count >= 4) return 0;
		value = strtod(cursor, &end);
		if (end == cursor || value != value || value < 0.0 || value > 1.0)
			return 0;
		count++;
		cursor = end;
	}
	return count == 3 || count == 4;
}

static s32 effectStringIn(const char *value, const char *const *values,
	size_t count)
{
	return value && knownName(value, values, count);
}

static s32 effectShaderMatchesNode(weapon_graph_opcode_e opcode,
	const char *shader)
{
	if (!shader || !shader[0]) return 1;
	switch (opcode) {
	case WEAPON_GRAPH_OP_EFFECT_TINT: return strcmp(shader, "classic_tint") == 0;
	case WEAPON_GRAPH_OP_EFFECT_GLOW: return strcmp(shader, "classic_glow") == 0;
	case WEAPON_GRAPH_OP_EFFECT_SHIMMER: return strcmp(shader, "classic_shimmer") == 0;
	case WEAPON_GRAPH_OP_EFFECT_DARKEN: return strcmp(shader, "classic_darken") == 0;
	case WEAPON_GRAPH_OP_EFFECT_SCREEN: return strcmp(shader, "classic_screen") == 0;
	case WEAPON_GRAPH_OP_EFFECT_PARTICLE: return strcmp(shader, "classic_particle") == 0;
	case WEAPON_GRAPH_OP_EFFECT_EXPLOSION:
	case WEAPON_GRAPH_OP_EFFECT_SPARK:
	case WEAPON_GRAPH_OP_EFFECT_SMOKE:
		return strcmp(shader, "classic_tint") == 0 ||
			strcmp(shader, "needler_pink_burst") == 0;
	default: return 0;
	}
}

static s32 effectParamValueValid(const effect_param_schema_t *schema,
	const weapon_graph_ir_param_t *param)
{
	f32 number;
	static const char *const explosion_classes[] = {
		"tiny", "small", "medium", "large", "huge", "massive"
	};
	static const char *const smoke_classes[] = {
		"none", "electrical", "tiny", "mini", "small", "medium", "large",
		"bullet_impact", "rocket_tail", "grenade_tail", "homing_tail",
		"pinball", "water", "debris"
	};
	switch (schema->rule) {
	case EFFECT_PARAM_STRING:
		return param->type == WEAPON_GRAPH_PARAM_STRING && param->value[0];
	case EFFECT_PARAM_CATALOG_ID:
		return param->type == WEAPON_GRAPH_PARAM_STRING &&
			pdEffectCatalogIdValid(param->value);
	case EFFECT_PARAM_COLOR: return effectColorParamValid(param);
	case EFFECT_PARAM_NUMBER_FINITE:
		return numericParam(param, &number) > 0;
	case EFFECT_PARAM_NUMBER_NONNEGATIVE:
		return numericParam(param, &number) > 0 && number >= 0.0f;
	case EFFECT_PARAM_NUMBER_POSITIVE:
		return numericParam(param, &number) > 0 && number > 0.0f;
	case EFFECT_PARAM_INTEGER: return param->type == WEAPON_GRAPH_PARAM_INT;
	case EFFECT_PARAM_SMOKE_TYPE:
		return param->type == WEAPON_GRAPH_PARAM_INT &&
			param->i_value >= 0 && param->i_value <= 22;
	case EFFECT_PARAM_EXPLOSION_CLASS:
		return param->type == WEAPON_GRAPH_PARAM_STRING && effectStringIn(
			param->value, explosion_classes, 6);
	case EFFECT_PARAM_SMOKE_CLASS:
		return param->type == WEAPON_GRAPH_PARAM_STRING && effectStringIn(
			param->value, smoke_classes, 14);
	case EFFECT_PARAM_SHADER:
		return param->type == WEAPON_GRAPH_PARAM_STRING && param->value[0] &&
			effectShaderMatchesNode(schema->opcode, param->value);
	default: return 0;
	}
}

static s32 nodeHasVisualPayload(const effect_graph_program_t *program,
	const weapon_graph_ir_node_t *node, const char *descriptor_shader)
{
	const weapon_graph_ir_param_t *shader = nodeParam(program, node, "shader");
	switch (node->opcode) {
	case WEAPON_GRAPH_OP_EFFECT_TINT:
	case WEAPON_GRAPH_OP_EFFECT_GLOW:
	case WEAPON_GRAPH_OP_EFFECT_SHIMMER:
	case WEAPON_GRAPH_OP_EFFECT_DARKEN:
	case WEAPON_GRAPH_OP_EFFECT_SCREEN:
	case WEAPON_GRAPH_OP_EFFECT_PARTICLE:
		return 1;
	case WEAPON_GRAPH_OP_EFFECT_EXPLOSION:
	case WEAPON_GRAPH_OP_EFFECT_SPARK:
	case WEAPON_GRAPH_OP_EFFECT_SMOKE:
		return nodeParam(program, node, "tint") ||
			(node->opcode == WEAPON_GRAPH_OP_EFFECT_SPARK &&
				(nodeParam(program, node, "tint2") ||
				nodeParam(program, node, "tint_secondary"))) ||
			nodeParam(program, node, "material_ref") ||
			nodeParam(program, node, "texture_ref") ||
			nodeParam(program, node, "intensity") ||
			nodeParam(program, node, "glow") || shader ||
			(descriptor_shader && descriptor_shader[0]);
	default:
		return 0;
	}
}

static s32 validateVisualTarget(const effect_graph_program_t *program,
	const weapon_graph_ir_node_t *node, const char *descriptor_target,
	char *err, size_t err_cap)
{
	static const char *const screen_targets[] = { "scene", "screen", "player" };
	static const char *const world_targets[] = {
		"world", "position", "source", "target", "weapon", "prop"
	};
	const weapon_graph_ir_param_t *target_param = nodeParam(program, node, "target");
	const weapon_graph_ir_param_t *attachment_param =
		nodeParam(program, node, "attachment");
	const char *kind_default =
		(node->opcode == WEAPON_GRAPH_OP_EFFECT_SCREEN ||
		 node->opcode == WEAPON_GRAPH_OP_EFFECT_DARKEN) ? "screen" : "position";
	const char *target = target_param ? target_param->value :
		(descriptor_target && descriptor_target[0] ? descriptor_target : kind_default);
	const char *attachment = attachment_param ? attachment_param->value : "";
	s32 screen = knownName(target, screen_targets, 3);
	s32 world = knownName(target, world_targets, 6) ||
		effectTargetIsNamedVec3(program, target);

	if (!screen && !world) {
		setErr(err, err_cap, "effect node %s has unsupported visual target %s",
			node->id, target);
		return -1;
	}
	if (screen && attachment[0]) {
		setErr(err, err_cap,
			"effect node %s screen target cannot use attachment %s",
			node->id, attachment);
		return -1;
	}
	if (node->opcode == WEAPON_GRAPH_OP_EFFECT_SCREEN ||
			node->opcode == WEAPON_GRAPH_OP_EFFECT_DARKEN) {
		if (!screen) {
			setErr(err, err_cap, "effect node %s kind %s requires a screen target",
				node->id, node->kind);
			return -1;
		}
		return 0;
	}
	if (node->opcode == WEAPON_GRAPH_OP_EFFECT_PARTICLE && !world) {
		setErr(err, err_cap, "effect node %s particle requires a world target",
			node->id);
		return -1;
	}
	if (!world) return 0;
	if (node->opcode == WEAPON_GRAPH_OP_EFFECT_GLOW) {
		if (attachment[0] && strcmp(attachment, "point") != 0) {
			setErr(err, err_cap, "effect node %s glow attachment %s is unsupported",
				node->id, attachment);
			return -1;
		}
	} else if (node->opcode == WEAPON_GRAPH_OP_EFFECT_SHIMMER) {
		if (attachment[0] && strcmp(attachment, "beam") != 0) {
			setErr(err, err_cap,
				"effect node %s shimmer attachment %s is unsupported",
				node->id, attachment);
			return -1;
		}
	} else if (attachment[0] && strcmp(attachment, "point") != 0 &&
			strcmp(attachment, "surface") != 0) {
		setErr(err, err_cap, "effect node %s attachment %s is unsupported",
			node->id, attachment);
		return -1;
	}
	return 0;
}

s32 effectInstanceProgramValidate(const effect_graph_program_t *program,
	const char *descriptor_target, const char *descriptor_shader,
	char *err, size_t err_cap)
{
	static const char *const scopes[] = { "call", "effect", "node" };
	static const char *const lifetimes[] = { "call", "effect", "node" };
	static const char *const types[] = { "vec3", "float", "number", "entity" };
	static const char *const direct_targets[] = {
		"target", "source", "position", "world", "scene", "screen",
		"player", "weapon", "prop"
	};
	s32 descriptor_shader_consumed = !descriptor_shader || !descriptor_shader[0];
	if (!program || program->node_count <= 0 || !program->nodes) {
		setErr(err, err_cap, "effect instance program has no executable graph");
		return -1;
	}
	if (program->context_count < 0 ||
			(program->context_count && !program->contexts)) {
		setErr(err, err_cap, "effect instance context table is invalid");
		return -1;
	}
	for (s32 i = 0; i < program->context_count; i++) {
		const weapon_graph_ir_context_t *context = &program->contexts[i];
		if (!context->name[0] || !context->source[0] ||
				!knownName(context->scope, scopes, 3) ||
				!knownName(context->lifetime, lifetimes, 3) ||
				!knownName(context->type, types, 4)) {
			setErr(err, err_cap,
				"effect context %s has unsupported scope/source/type/lifetime semantics",
				context->name[0] ? context->name : "<unnamed>");
			return -1;
		}
	}
	if (program->timeline.count && !program->timeline.keys) {
		setErr(err, err_cap, "effect timeline key table is invalid");
		return -1;
	}
	for (size_t i = 0; i < program->timeline.count; i++) {
		if (strcmp(program->timeline.keys[i].property, "intensity") != 0) {
			setErr(err, err_cap,
				"effect timeline property %s has no production consumer",
				program->timeline.keys[i].property);
			return -1;
		}
	}
	for (s32 i = 0; i < program->node_count; i++) {
		const weapon_graph_ir_node_t *node = &program->nodes[i];
		const weapon_graph_ir_param_t *param = nodeParam(program, node, "target");
		const weapon_graph_ir_param_t *node_shader = nodeParam(program, node, "shader");
		for (s32 p = 0; p < node->param_count; p++) {
			const weapon_graph_ir_param_t *authored =
				&program->params[node->param_start + p];
			const effect_param_schema_t *schema =
				effectParamSchemaFind(node->opcode, authored->key);
			if (!schema) {
				if (strcmp(authored->key, "sound") == 0 ||
						strcmp(authored->key, "soundnum") == 0) {
					setErr(err, err_cap,
						"effect node %s uses forbidden numeric audio field %s; use audio_catalog_id",
						node->id, authored->key);
				} else {
					setErr(err, err_cap,
						"effect node %s kind %s has unsupported field %s",
						node->id, node->kind, authored->key);
				}
				return -1;
			}
			if (!effectParamValueValid(schema, authored)) {
				setErr(err, err_cap,
					"effect node %s field %s has invalid type, range, or value",
					node->id, authored->key);
				return -1;
			}
		}
		if (param) {
			s32 resolved = 0;
			if (param->type != WEAPON_GRAPH_PARAM_STRING || !param->value[0]) {
				setErr(err, err_cap, "effect node %s has invalid target", node->id);
				return -1;
			}
			resolved = knownName(param->value, direct_targets, 9);
			for (s32 c = 0; c < program->context_count && !resolved; c++) {
				resolved = strcmp(param->value, program->contexts[c].name) == 0;
			}
			if (!resolved) {
				setErr(err, err_cap, "effect node %s target %s is unresolved",
					node->id, param->value);
				return -1;
			}
		}
		param = nodeParam(program, node, "attachment");
		if (param && (param->type != WEAPON_GRAPH_PARAM_STRING || !param->value[0])) {
			setErr(err, err_cap, "effect node %s has invalid attachment", node->id);
			return -1;
		}
		if (param && node->opcode == WEAPON_GRAPH_OP_EFFECT_PARTICLE) {
			setErr(err, err_cap,
				"effect node %s particle attachment has no implemented transform contract",
				node->id);
			return -1;
		}
		if (param && (node->opcode == WEAPON_GRAPH_OP_EFFECT_EXPLOSION ||
				node->opcode == WEAPON_GRAPH_OP_EFFECT_SPARK ||
				node->opcode == WEAPON_GRAPH_OP_EFFECT_SMOKE) &&
				!nodeHasVisualPayload(program, node, descriptor_shader)) {
			setErr(err, err_cap,
				"effect node %s attachment has no gameplay or presentation consumer",
				node->id);
			return -1;
		}
		param = nodeParam(program, node, "priority");
		if (param && param->type != WEAPON_GRAPH_PARAM_INT) {
			setErr(err, err_cap, "effect node %s priority must be an integer", node->id);
			return -1;
		}
		param = nodeParam(program, node, "lifetime");
		if (param) {
			f32 value;
			if (numericParam(param, &value) <= 0 || value < 0.0f) {
				setErr(err, err_cap,
					"effect node %s lifetime must be finite and nonnegative", node->id);
				return -1;
			}
		}
		if (node->opcode == WEAPON_GRAPH_OP_EFFECT_SMOKE) {
			s32 smoke_selectors =
				(nodeParam(program, node, "smoke_class") != NULL) +
				(nodeParam(program, node, "class") != NULL) +
				(nodeParam(program, node, "smoke_type") != NULL);
			if (smoke_selectors > 1) {
				setErr(err, err_cap,
					"effect node %s has multiple mutually exclusive smoke selectors",
					node->id);
				return -1;
			}
			param = nodeParam(program, node, "smoke_class");
			if (!param) param = nodeParam(program, node, "class");
			if (param && param->type == WEAPON_GRAPH_PARAM_STRING &&
					(strcmp(param->value, "huge") == 0 ||
					strcmp(param->value, "massive") == 0)) {
				setErr(err, err_cap,
					"effect node %s smoke class %s has no distinct v1 consumer",
					node->id, param->value);
				return -1;
			}
		}
		if (node->opcode == WEAPON_GRAPH_OP_EFFECT_SPARK &&
				nodeParam(program, node, "tint2") &&
				nodeParam(program, node, "tint_secondary")) {
			setErr(err, err_cap,
				"effect node %s has both tint2 and tint_secondary", node->id);
			return -1;
		}
		if (node->opcode == WEAPON_GRAPH_OP_EFFECT_PARTICLE &&
				!nodeParam(program, node, "texture_ref")) {
			setErr(err, err_cap,
				"effect node %s particle requires texture_ref", node->id);
			return -1;
		}
		if (node->opcode == WEAPON_GRAPH_OP_EFFECT_EXPLOSION &&
				!nodeParam(program, node, "explosion_class")) {
			setErr(err, err_cap,
				"effect node %s explosion requires explosion_class", node->id);
			return -1;
		}
		if (node->opcode == WEAPON_GRAPH_OP_EFFECT_SMOKE &&
				!nodeParam(program, node, "smoke_class") &&
				!nodeParam(program, node, "class") &&
				!nodeParam(program, node, "smoke_type")) {
			setErr(err, err_cap,
				"effect node %s smoke requires smoke_class, class, or smoke_type",
				node->id);
			return -1;
		}
		if (nodeHasVisualPayload(program, node, descriptor_shader)) {
			const char *shader = node_shader ? node_shader->value : descriptor_shader;
			if (shader && shader[0] &&
					!effectPresentationShaderSupported(node->kind, shader)) {
				setErr(err, err_cap,
					"effect node %s shader %s has no matching production pipeline",
					node->id, shader);
				return -1;
			}
			if (!node_shader && shader && shader[0]) descriptor_shader_consumed = 1;
			if (validateVisualTarget(program, node, descriptor_target,
					err, err_cap) != 0) return -1;
		}
	}
	if (!descriptor_shader_consumed) {
		setErr(err, err_cap,
			"effect descriptor shader %s is overridden or has no visual consumer",
			descriptor_shader);
		return -1;
	}
	return 0;
}

static const effect_instance_context_value_t *requestContext(
	const effect_instance_request_t *request, const char *name)
{
	for (size_t i = 0; i < request->context_count; i++) {
		if (strcmp(request->contexts[i].name, name) == 0) return &request->contexts[i];
	}
	return NULL;
}

static s32 resolveContexts(effect_instance_record_t *instance,
	const effect_instance_request_t *request, char *err, size_t err_cap)
{
	static const char *const scopes[] = { "call", "effect", "node" };
	static const char *const lifetimes[] = { "call", "effect", "node" };
	static const char *const types[] = { "vec3", "float", "number", "entity" };
	const effect_graph_program_t *program = &instance->frame.runtime->program;

	if (request->context_count && !request->contexts) {
		setErr(err, err_cap, "effect request context table is invalid");
		return -1;
	}
	for (size_t i = 0; i < request->context_count; i++) {
		if (!request->contexts[i].name[0]) {
			setErr(err, err_cap, "effect request context has no name");
			return -1;
		}
		for (size_t j = 0; j < i; j++) {
			if (strcmp(request->contexts[i].name, request->contexts[j].name) == 0) {
				setErr(err, err_cap, "duplicate effect request context %s",
					request->contexts[i].name);
				return -1;
			}
		}
	}
	if ((size_t)program->context_count > SIZE_MAX / sizeof(*instance->contexts)) {
		setErr(err, err_cap, "effect runtime context table size overflow");
		return -1;
	}
	if (program->context_count > 0) {
		instance->contexts = (effect_instance_context_value_t *)calloc(
			(size_t)program->context_count, sizeof(*instance->contexts));
		if (!instance->contexts) {
			setErr(err, err_cap, "out of memory resolving effect contexts");
			return -1;
		}
		instance->frame.contexts = instance->contexts;
	}
	for (s32 i = 0; i < program->context_count; i++) {
		const weapon_graph_ir_context_t *authored = &program->contexts[i];
		effect_instance_context_value_t *resolved =
			&instance->contexts[instance->frame.context_count];
		const effect_instance_context_value_t *explicit_value =
			requestContext(request, authored->name);
		if (!knownName(authored->scope, scopes, 3) ||
				!knownName(authored->lifetime, lifetimes, 3) ||
				!knownName(authored->type, types, 4)) {
			setErr(err, err_cap,
				"effect context %s has unsupported scope/type/lifetime semantics",
				authored->name);
			return -1;
		}
		memset(resolved, 0, sizeof(*resolved));
		copyStr(resolved->name, sizeof(resolved->name), authored->name);
		copyStr(resolved->type, sizeof(resolved->type), authored->type);
		if (explicit_value) {
			if (explicit_value->type[0] &&
					strcmp(explicit_value->type, authored->type) != 0 &&
					!((strcmp(authored->type, "float") == 0 ||
						strcmp(authored->type, "number") == 0) &&
					(strcmp(explicit_value->type, "float") == 0 ||
						strcmp(explicit_value->type, "number") == 0))) {
				setErr(err, err_cap, "effect context %s type mismatch", authored->name);
				return -1;
			}
			resolved->vec3 = explicit_value->vec3;
			resolved->number = explicit_value->number;
			resolved->entity = explicit_value->entity;
		} else if (strcmp(authored->source, "target") == 0) {
			resolved->vec3 = request->target_position;
			resolved->entity = request->target_prop;
		} else if (strcmp(authored->source, "source") == 0) {
			resolved->vec3 = request->source_position;
			resolved->entity = request->source_prop;
		} else if (strcmp(authored->source, "position") == 0 ||
				strcmp(authored->source, "world") == 0) {
			resolved->vec3 = request->position;
		} else if (strcmp(authored->source, "primary_direction") == 0) {
			resolved->vec3 = request->primary_direction;
		} else if (strcmp(authored->source, "secondary_direction") == 0) {
			resolved->vec3 = request->secondary_direction;
		} else {
			setErr(err, err_cap, "effect context %s has unresolved source %s",
				authored->name, authored->source);
			return -1;
		}
		instance->frame.context_count++;
	}
	return 0;
}

static s32 findRoot(const effect_graph_program_t *program,
	const effect_instance_request_t *request, s32 *root, const char **subgraph,
	char *err, size_t err_cap)
{
	*root = -1;
	*subgraph = NULL;
	if (request->export_name && request->export_name[0] &&
			request->subgraph_id && request->subgraph_id[0]) {
		setErr(err, err_cap, "effect request cannot select both export and subgraph");
		return -1;
	}
	if (request->export_name && request->export_name[0]) {
		for (s32 i = 0; i < program->export_count; i++) {
			if (strcmp(program->exports[i].name, request->export_name) == 0) {
				*root = program->exports[i].node;
				return 0;
			}
		}
		setErr(err, err_cap, "effect export %s does not exist", request->export_name);
		return -1;
	}
	if (request->subgraph_id && request->subgraph_id[0]) {
		for (s32 i = 0; i < program->subgraph_count; i++) {
			if (strcmp(program->subgraphs[i].id, request->subgraph_id) == 0) {
				*root = program->subgraphs[i].entry_node;
				*subgraph = program->subgraphs[i].id;
				return 0;
			}
		}
		setErr(err, err_cap, "effect subgraph %s does not exist", request->subgraph_id);
		return -1;
	}
	return 0;
}

static s32 selectTopology(effect_instance_record_t *instance,
	const effect_instance_request_t *request, char *err, size_t err_cap)
{
	const effect_graph_program_t *program = &instance->frame.runtime->program;
	s32 root;
	const char *subgraph;
	if (findRoot(program, request, &root, &subgraph, err, err_cap) != 0) return -1;
	instance->active_nodes = (u8 *)calloc((size_t)program->node_count, 1);
	if (!instance->active_nodes) {
		setErr(err, err_cap, "out of memory selecting effect topology");
		return -1;
	}
	if (root < 0) {
		memset(instance->active_nodes, 1, (size_t)program->node_count);
		return 0;
	}
	if (root >= program->node_count) {
		setErr(err, err_cap, "effect root index is corrupt");
		return -1;
	}
	instance->active_nodes[root] = 1;
	for (s32 pass = 0; pass < program->node_count; pass++) {
		s32 changed = 0;
		for (s32 i = 0; i < program->edge_count; i++) {
			const weapon_graph_ir_edge_t *edge = &program->edges[i];
			if (edge->from < 0 || edge->from >= program->node_count ||
					edge->to < 0 || edge->to >= program->node_count) {
				setErr(err, err_cap, "effect topology contains a corrupt edge");
				return -1;
			}
			if (instance->active_nodes[edge->from] && !instance->active_nodes[edge->to] &&
					(!subgraph || strcmp(program->nodes[edge->to].subgraph, subgraph) == 0)) {
				instance->active_nodes[edge->to] = 1;
				changed = 1;
			}
		}
		if (!changed) break;
	}
	return 0;
}

static s32 resolveNodeFields(effect_instance_record_t *instance,
	const weapon_graph_ir_node_t *node, char *err, size_t err_cap)
{
	const effect_graph_program_t *program = &instance->frame.runtime->program;
	const weapon_graph_ir_param_t *param;
	f32 value;
	instance->frame.target_name = "";
	instance->frame.attachment = "";
	instance->frame.position = instance->origin_position;
	instance->frame.priority = 0;

	param = nodeParam(program, node, "target");
	if (param) {
		if (param->type != WEAPON_GRAPH_PARAM_STRING || !param->value[0]) {
			setErr(err, err_cap, "effect node %s has invalid target", node->id);
			return -1;
		}
		instance->frame.target_name = param->value;
		const effect_instance_context_value_t *context =
			effectInstanceContext(&instance->frame, param->value);
		if (context) instance->frame.position = context->vec3;
		else if (strcmp(param->value, "target") == 0 ||
				strcmp(param->value, "weapon") == 0 ||
				strcmp(param->value, "prop") == 0) {
			instance->frame.position = instance->frame.target_position;
		} else if (strcmp(param->value, "source") == 0) {
			instance->frame.position = instance->frame.source_position;
		} else if (strcmp(param->value, "position") == 0 ||
				strcmp(param->value, "world") == 0 ||
				strcmp(param->value, "scene") == 0 ||
				strcmp(param->value, "screen") == 0 ||
				strcmp(param->value, "player") == 0) {
			/* request position already copied into the frame before dispatch */
		} else {
			setErr(err, err_cap, "effect node %s target %s is unresolved",
				node->id, param->value);
			return -1;
		}
	} else if (instance->frame.runtime->target_key[0]) {
		const char *target = instance->frame.runtime->target_key;
		instance->frame.target_name = target;
		if (strcmp(target, "target") == 0 || strcmp(target, "weapon") == 0 ||
				strcmp(target, "prop") == 0) {
			instance->frame.position = instance->frame.target_position;
		} else if (strcmp(target, "source") == 0) {
			instance->frame.position = instance->frame.source_position;
		} else if (strcmp(target, "position") != 0 &&
				strcmp(target, "world") != 0 && strcmp(target, "scene") != 0 &&
				strcmp(target, "screen") != 0 && strcmp(target, "player") != 0) {
			const effect_instance_context_value_t *context =
				effectInstanceContext(&instance->frame, target);
			if (!context) {
				setErr(err, err_cap, "effect descriptor target %s is unresolved", target);
				return -1;
			}
			instance->frame.position = context->vec3;
		}
	} else if (node->opcode == WEAPON_GRAPH_OP_EFFECT_SCREEN ||
			node->opcode == WEAPON_GRAPH_OP_EFFECT_DARKEN) {
		instance->frame.target_name = "screen";
	} else {
		instance->frame.target_name = "position";
	}
	param = nodeParam(program, node, "attachment");
	if (param) {
		if (param->type != WEAPON_GRAPH_PARAM_STRING || !param->value[0]) {
			setErr(err, err_cap, "effect node %s has invalid attachment", node->id);
			return -1;
		}
		instance->frame.attachment = param->value;
	}
	param = nodeParam(program, node, "priority");
	if (param) {
		if (param->type != WEAPON_GRAPH_PARAM_INT) {
			setErr(err, err_cap, "effect node %s priority must be an integer", node->id);
			return -1;
		}
		instance->frame.priority = param->i_value;
		if (instance->frame.time == 0.0f &&
				param->i_value > instance->sort_priority) {
			instance->sort_priority = param->i_value;
		}
	}
	param = nodeParam(program, node, "lifetime");
	if (param) {
		if (numericParam(param, &value) <= 0 || value < 0.0f) {
			setErr(err, err_cap, "effect node %s lifetime must be finite and nonnegative",
				node->id);
			return -1;
		}
		if (value > instance->frame.lifetime) instance->frame.lifetime = value;
	}
	return 0;
}

static s32 buildConsumers(effect_instance_record_t *instance,
	char *err, size_t err_cap)
{
	if (s_test_consumer_count > 0) {
		memcpy(instance->consumers, s_test_consumers,
			(size_t)s_test_consumer_count * sizeof(instance->consumers[0]));
		instance->consumer_count = s_test_consumer_count;
	} else if (effectGameplayRuntimeBuildConsumer(&instance->consumers[0], err, err_cap) != 0 ||
			effectPresentationRuntimeBuildConsumer(&instance->consumers[1], err, err_cap) != 0) {
		if (err && err_cap && !err[0]) setErr(err, err_cap,
			"effect consumer installation failed");
		return -1;
	} else {
		instance->consumer_count = 2;
	}
	for (s32 lane = 0; lane < instance->consumer_count; lane++) {
		const effect_instance_consumer_t *consumer = &instance->consumers[lane];
		if (!consumer->name || !consumer->name[0] || !consumer->begin ||
				!consumer->prepare || !consumer->commit || !consumer->rollback ||
				!consumer->update || !consumer->cleanup) {
			setErr(err, err_cap, "effect consumer lane %d is incomplete", lane);
			return -1;
		}
	}
	for (s32 slot = 0; slot < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; slot++) {
		s32 owners = 0;
		for (s32 lane = 0; lane < instance->consumer_count; lane++) {
			if (instance->consumers[lane].nodes[slot]) owners++;
		}
		if (owners < 1) {
			setErr(err, err_cap, "effect dispatch slot %d has no consumer", slot);
			return -1;
		}
	}
	return 0;
}

static effect_instance_dispatch_state_t *dispatchState(
	const effect_graph_dispatch_context_t *context)
{
	return context ? (effect_instance_dispatch_state_t *)context->user : NULL;
}

static s32 compositeBegin(const effect_graph_dispatch_context_t *context)
{
	effect_instance_dispatch_state_t *state = dispatchState(context);
	char err[256];
	if (!state || !state->instance) return -1;
	state->begun_count = 0;
	state->attempted_count = 0;
	for (s32 i = 0; i < state->instance->consumer_count; i++) {
		/* A failing begin may already have staged lane-local state. Include it
		 * in the reverse rollback set. */
		state->begun_count = i + 1;
		if (state->instance->consumers[i].begin(&state->instance->frame,
				err, sizeof(err)) != 0) return -1;
	}
	/* Updates stage instance-wide time-dependent state before the selected
	 * node callbacks rebuild every owned command at the new sample time. */
	if (state->instance->frame.time > 0.0f ||
			state->instance->frame.delta_time > 0.0f) {
		for (s32 i = 0; i < state->instance->consumer_count; i++) {
			if (state->instance->consumers[i].update(&state->instance->frame,
					err, sizeof(err)) != 0) return -1;
		}
	}
	return 0;
}

static s32 compositeNode(const effect_graph_dispatch_context_t *context,
	const weapon_graph_ir_node_t *node, s32 execution_index)
{
	effect_instance_dispatch_state_t *state = dispatchState(context);
	effect_instance_record_t *instance;
	s32 node_index;
	s32 slot;
	char err[256];
	if (!state || !(instance = state->instance) || !node) return -1;
	node_index = (s32)(node - instance->frame.runtime->program.nodes);
	if (node_index < 0 || node_index >= instance->frame.runtime->program.node_count) return -1;
	if (!instance->active_nodes[node_index]) return 0;
	instance->frame.node = node;
	instance->frame.node_index = node_index;
	instance->frame.execution_index = execution_index;
	if (resolveNodeFields(instance, node, err, sizeof(err)) != 0) return -1;
	slot = effectGraphDispatchSlotForOpcode(node->opcode);
	s32 invoked = 0;
	for (s32 lane = 0; lane < instance->consumer_count; lane++) {
		if (instance->consumers[lane].nodes[slot]) {
			state->attempted_count++;
			invoked++;
			if (instance->consumers[lane].nodes[slot](&instance->frame,
					err, sizeof(err)) != 0) return -1;
		}
	}
	return invoked > 0 ? 0 : -1;
}

static s32 compositeCommit(const effect_graph_dispatch_context_t *context)
{
	effect_instance_dispatch_state_t *state = dispatchState(context);
	char err[256];
	if (!state || !state->instance) return -1;
	for (s32 i = 0; i < state->instance->consumer_count; i++) {
		if (state->instance->consumers[i].prepare(&state->instance->frame,
				err, sizeof(err)) != 0) return -1;
	}
	for (s32 i = 0; i < state->instance->consumer_count; i++) {
		state->instance->consumers[i].commit(&state->instance->frame);
	}
	state->instance->committed = 1;
	if (s_audit_enabled) {
		sysLogPrintf(LOG_NOTE,
			"EFFECT.INSTANCE.AUDIT: committed asset=%s instance=%llu consumers=%d lifetime=%.3f",
			state->instance->asset_id,
			(unsigned long long)state->instance->frame.instance_id,
			state->instance->consumer_count,
			(double)state->instance->frame.lifetime);
	}
	return 0;
}

static void compositeRollback(const effect_graph_dispatch_context_t *context,
	s32 attempted_count)
{
	effect_instance_dispatch_state_t *state = dispatchState(context);
	if (!state || !state->instance) return;
	(void)attempted_count;
	for (s32 i = state->begun_count; i-- > 0; ) {
		state->instance->consumers[i].rollback(&state->instance->frame,
			state->attempted_count);
	}
}

static void cleanupInstance(effect_instance_record_t *instance)
{
	if (!instance) return;
	if (instance->committed) {
		for (s32 i = instance->consumer_count; i-- > 0; ) {
			instance->consumers[i].cleanup(instance->frame.instance_id);
		}
	}
	free(instance->active_nodes);
	free(instance->contexts);
	free(instance);
}

static s32 insertInstance(effect_instance_record_t *instance,
	char *err, size_t err_cap)
{
	if (s_instance_count == s_instance_capacity) {
		size_t next = s_instance_capacity ? s_instance_capacity * 2 : 16;
		if (next < s_instance_count + 1 ||
				next > SIZE_MAX / sizeof(*s_instances)) {
			setErr(err, err_cap, "effect instance table size overflow");
			return -1;
		}
		effect_instance_record_t **grown = (effect_instance_record_t **)realloc(
			s_instances, next * sizeof(*s_instances));
		if (!grown) {
			setErr(err, err_cap, "out of memory growing effect instance table");
			return -1;
		}
		s_instances = grown;
		s_instance_capacity = next;
	}
	size_t at = s_instance_count;
	while (at > 0) {
		effect_instance_record_t *prior = s_instances[at - 1];
		if (prior->sort_priority > instance->sort_priority ||
				(prior->sort_priority == instance->sort_priority &&
				prior->frame.instance_id < instance->frame.instance_id)) break;
		s_instances[at] = prior;
		at--;
	}
	s_instances[at] = instance;
	s_instance_count++;
	return 0;
}

s32 effectInstanceRuntimeSpawn(const effect_instance_request_t *request,
	u64 *instance_id_out, char *err, size_t err_cap)
{
	effect_instance_record_t *instance;
	effect_instance_dispatch_state_t state;
	effect_graph_dispatch_table_t dispatch;
	const effect_graph_runtime_t *runtime;
	f32 timeline_end = 0.0f;

	if (err && err_cap) err[0] = '\0';
	if (instance_id_out) *instance_id_out = 0;
	if (!request || !request->asset_id || !request->asset_id[0]) {
		setErr(err, err_cap, "effect spawn requires a catalog ID");
		return -1;
	}
	runtime = effectGraphRuntimeGetForGameplay(request->asset_id);
	if (!runtime || (runtime->program.kind != EFFECT_GRAPH_PROGRAM_GRAPH &&
			runtime->program.kind != EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE)) {
		setErr(err, err_cap, "effect %s has no active v1 graph", request->asset_id);
		return -1;
	}
	if (request->room_count < 0 || request->room_count > EFFECT_INSTANCE_ROOM_CAP) {
		setErr(err, err_cap, "effect request room list is invalid or too large");
		return -1;
	}
	instance = (effect_instance_record_t *)calloc(1, sizeof(*instance));
	if (!instance) {
		setErr(err, err_cap, "out of memory creating effect instance");
		return -1;
	}
	instance->frame.runtime = runtime;
	instance->frame.instance_id = s_next_instance_id++;
	if (instance->frame.instance_id == 0 || s_next_instance_id == 0) {
		free(instance);
		setErr(err, err_cap, "effect instance ID space exhausted");
		return -1;
	}
	copyStr(instance->asset_id, sizeof(instance->asset_id), request->asset_id);
	copyStr(instance->source_sha256, sizeof(instance->source_sha256),
		runtime->source_sha256);
	instance->frame.source_prop = request->source_prop;
	instance->frame.target_prop = request->target_prop;
	instance->frame.source_position = request->source_position;
	instance->frame.target_position = request->target_position;
	instance->frame.position = request->position;
	instance->origin_position = request->position;
	instance->frame.primary_direction = request->primary_direction;
	instance->frame.secondary_direction = request->secondary_direction;
	instance->frame.room_count = request->room_count;
	if (request->room_count) memcpy(instance->frame.rooms, request->rooms,
		(size_t)request->room_count * sizeof(request->rooms[0]));
	instance->frame.playernum = request->playernum;
	instance->frame.action_mode = request->action_mode;
	instance->source_alive = request->source_alive;
	instance->target_alive = request->target_alive;
	instance->liveness_user = request->liveness_user;
	if (resolveContexts(instance, request, err, err_cap) != 0 ||
			selectTopology(instance, request, err, err_cap) != 0 ||
			buildConsumers(instance, err, err_cap) != 0) {
		cleanupInstance(instance);
		return -1;
	}
	for (size_t i = 0; i < runtime->program.timeline.count; i++) {
		f32 time = runtime->program.timeline.keys[i].time;
		if (!finiteFloat(time) || time < 0.0f) {
			setErr(err, err_cap, "effect timeline contains invalid time");
			cleanupInstance(instance);
			return -1;
		}
		if (time > timeline_end) timeline_end = time;
	}
	instance->frame.lifetime = timeline_end;
	memset(&dispatch, 0, sizeof(dispatch));
	dispatch.begin = compositeBegin;
	dispatch.commit = compositeCommit;
	dispatch.rollback = compositeRollback;
	for (s32 i = 0; i < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; i++) {
		dispatch.nodes[i] = compositeNode;
	}
	memset(&state, 0, sizeof(state));
	state.instance = instance;
	if (effectGraphRuntimeDispatch(request->asset_id, 0.0f, &dispatch, &state,
			err, err_cap) < 0) {
		cleanupInstance(instance);
		return -1;
	}
	if (instance_id_out) *instance_id_out = instance->frame.instance_id;
	if (instance->frame.lifetime <= 0.0f) {
		cleanupInstance(instance);
		return 0;
	}
	if (insertInstance(instance, err, err_cap) != 0) {
		cleanupInstance(instance);
		return -1;
	}
	return 0;
}

static void removeInstance(size_t index)
{
	cleanupInstance(s_instances[index]);
	if (index + 1 < s_instance_count) {
		memmove(&s_instances[index], &s_instances[index + 1],
			(s_instance_count - index - 1) * sizeof(*s_instances));
	}
	s_instance_count--;
}

void effectInstanceRuntimeTick(f32 delta_seconds)
{
	if (!finiteFloat(delta_seconds) || delta_seconds < 0.0f) {
		sysLogPrintf(LOG_WARNING,
			"EFFECT.INSTANCE: rejected invalid tick delta %.9g", (double)delta_seconds);
		return;
	}
	for (size_t i = 0; i < s_instance_count; ) {
		effect_instance_record_t *instance = s_instances[i];
		const effect_graph_runtime_t *runtime =
			effectGraphRuntimeGetForGameplay(instance->asset_id);
		if (!runtime || strcmp(runtime->source_sha256, instance->source_sha256) != 0) {
			removeInstance(i);
			continue;
		}
		if ((instance->frame.source_prop && instance->source_alive &&
				!instance->source_alive(instance->frame.source_prop,
					instance->liveness_user)) ||
				(instance->frame.target_prop && instance->target_alive &&
				!instance->target_alive(instance->frame.target_prop,
					instance->liveness_user))) {
			removeInstance(i);
			continue;
		}
		instance->frame.runtime = runtime;
		instance->frame.delta_time = delta_seconds;
		instance->frame.time += delta_seconds;
		if (instance->frame.time >= instance->frame.lifetime) {
			instance->frame.time = instance->frame.lifetime;
		}
		char err[256] = {0};
		effect_instance_dispatch_state_t state;
		effect_graph_dispatch_table_t dispatch;
		memset(&state, 0, sizeof(state));
		memset(&dispatch, 0, sizeof(dispatch));
		state.instance = instance;
		dispatch.begin = compositeBegin;
		dispatch.commit = compositeCommit;
		dispatch.rollback = compositeRollback;
		for (s32 slot = 0; slot < EFFECT_GRAPH_DISPATCH_SLOT_COUNT; slot++) {
			dispatch.nodes[slot] = compositeNode;
		}
		if (effectGraphRuntimeDispatch(instance->asset_id, instance->frame.time,
				&dispatch, &state, err, sizeof(err)) < 0) {
			sysLogPrintf(LOG_WARNING, "EFFECT.INSTANCE: update failed for %s: %s",
				instance->asset_id, err[0] ? err : "consumer transaction failure");
			removeInstance(i);
			continue;
		}
		if (instance->frame.time >= instance->frame.lifetime) {
			removeInstance(i);
			continue;
		}
		i++;
	}
}

void effectInstanceRuntimeCancelAsset(const char *asset_id)
{
	if (!asset_id || !asset_id[0]) return;
	for (size_t i = s_instance_count; i-- > 0; ) {
		if (strcmp(s_instances[i]->asset_id, asset_id) == 0) removeInstance(i);
	}
}

void effectInstanceRuntimeCancelEntity(const void *entity)
{
	if (!entity) return;
	for (size_t i = s_instance_count; i-- > 0; ) {
		if (s_instances[i]->frame.source_prop == entity ||
				s_instances[i]->frame.target_prop == entity) {
			removeInstance(i);
		}
	}
}

void effectInstanceRuntimeClearAll(void)
{
	while (s_instance_count) removeInstance(s_instance_count - 1);
	free(s_instances);
	s_instances = NULL;
	s_instance_capacity = 0;
}

size_t effectInstanceRuntimeCount(void)
{
	return s_instance_count;
}

void effectInstanceRuntimeSetTestConsumers(
	const effect_instance_consumer_t *consumers, s32 consumer_count)
{
	if (!consumers || consumer_count <= 0) {
		memset(s_test_consumers, 0, sizeof(s_test_consumers));
		s_test_consumer_count = 0;
		return;
	}
	if (consumer_count > 2) consumer_count = 2;
	memcpy(s_test_consumers, consumers,
		(size_t)consumer_count * sizeof(s_test_consumers[0]));
	s_test_consumer_count = consumer_count;
}
