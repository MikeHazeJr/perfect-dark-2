/**
 * effect_presentation_runtime.c -- transactional renderer-facing .pdeffect
 * presentation state.
 */
#include <ctype.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asset_runtime.h"
#include "effect_presentation_runtime.h"
#include "system.h"

typedef struct presentation_instance {
	u64 id;
	effect_presentation_command_t *committed;
	size_t committed_count;
	effect_presentation_command_t *staged;
	size_t staged_count;
	size_t staged_capacity;
	s32 transaction_active;
	s32 created_by_transaction;
} presentation_instance_t;

static presentation_instance_t *s_Instances;
static size_t s_InstanceCount;
static size_t s_InstanceCapacity;
static effect_presentation_command_t *s_RenderCommands;
static size_t s_RenderCommandCount;
static effect_presentation_command_t *s_PreparedCommands;
static size_t s_PreparedCommandCount;
static u64 s_PreparedInstanceId;

static void presentationError(char *err, size_t cap, const char *text);

s32 effectPresentationShaderSupported(const char *node_kind,
	const char *shader_id)
{
	if (!shader_id || !shader_id[0]) return 1;
	if (!node_kind || !node_kind[0]) return 0;
	if (strcmp(node_kind, "effect.tint") == 0)
		return strcmp(shader_id, "classic_tint") == 0;
	if (strcmp(node_kind, "effect.glow") == 0)
		return strcmp(shader_id, "classic_glow") == 0;
	if (strcmp(node_kind, "effect.shimmer") == 0)
		return strcmp(shader_id, "classic_shimmer") == 0;
	if (strcmp(node_kind, "effect.darken") == 0)
		return strcmp(shader_id, "classic_darken") == 0;
	if (strcmp(node_kind, "effect.screen") == 0)
		return strcmp(shader_id, "classic_screen") == 0;
	if (strcmp(node_kind, "effect.particle") == 0)
		return strcmp(shader_id, "classic_particle") == 0;
	if (strcmp(node_kind, "effect.explosion") == 0 ||
			strcmp(node_kind, "effect.spark") == 0 ||
			strcmp(node_kind, "effect.smoke") == 0) {
		return strcmp(shader_id, "needler_pink_burst") == 0 ||
			strcmp(shader_id, "classic_tint") == 0;
	}
	return 0;
}

void effectPresentationMaterialColor(const f32 source_rgba[4],
	const char *shading_model, f32 roughness, f32 metallic, s32 emissive,
	f32 out_rgba[4])
{
	f32 average;
	f32 peak;
	if (!source_rgba || !out_rgba) return;
	if (roughness < 0.0f) roughness = 0.0f;
	if (roughness > 1.0f) roughness = 1.0f;
	if (metallic < 0.0f) metallic = 0.0f;
	if (metallic > 1.0f) metallic = 1.0f;
	average = (source_rgba[0] + source_rgba[1] + source_rgba[2]) / 3.0f;
	peak = source_rgba[0];
	if (source_rgba[1] > peak) peak = source_rgba[1];
	if (source_rgba[2] > peak) peak = source_rgba[2];
	for (s32 channel = 0; channel < 3; channel++) {
		f32 value = source_rgba[channel] * (1.0f - roughness * 0.2f) +
			average * roughness * 0.2f;
		value = value * (1.0f - metallic * 0.25f) +
			peak * metallic * 0.25f;
		if (emissive || (shading_model &&
				strcmp(shading_model, "classic_emissive") == 0)) {
			value *= 1.0f + (1.0f - roughness) * 0.35f;
		}
		if (value < 0.0f) value = 0.0f;
		if (value > 1.0f) value = 1.0f;
		out_rgba[channel] = value;
	}
	out_rgba[3] = source_rgba[3];
}

static s32 presentationStringIs(const char *value, const char *expected)
{
	return value && expected && strcmp(value, expected) == 0;
}

static s32 presentationScreenTarget(const char *target)
{
	return !target || !target[0] || presentationStringIs(target, "screen") ||
		presentationStringIs(target, "scene") ||
		presentationStringIs(target, "player");
}

static s32 presentationCopyName(char out[EFFECT_INSTANCE_NAME_LEN],
	const char *value, const char *label, char *err, size_t err_cap)
{
	if (!value) value = "";
	if (strlen(value) >= EFFECT_INSTANCE_NAME_LEN) {
		char message[160];
		snprintf(message, sizeof(message), "%s exceeds the presentation name limit",
			label);
		presentationError(err, err_cap, message);
		return 0;
	}
	memcpy(out, value, strlen(value) + 1);
	return 1;
}

static void presentationError(char *err, size_t cap, const char *text)
{
	if (err && cap) snprintf(err, cap, "%s", text ? text : "presentation error");
}

static s32 presentationFinite(f32 value)
{
	return value == value && value <= FLT_MAX && value >= -FLT_MAX;
}

static presentation_instance_t *presentationFind(u64 id)
{
	for (size_t i = 0; i < s_InstanceCount; i++) {
		if (s_Instances[i].id == id) return &s_Instances[i];
	}
	return NULL;
}

static presentation_instance_t *presentationCreate(u64 id)
{
	presentation_instance_t *grown;
	if (!id) return NULL;
	if (s_InstanceCount == s_InstanceCapacity) {
		size_t next = s_InstanceCapacity ? s_InstanceCapacity * 2 : 16;
		if (next < s_InstanceCount + 1 ||
				next > SIZE_MAX / sizeof(*s_Instances)) return NULL;
		grown = (presentation_instance_t *)realloc(s_Instances,
			next * sizeof(*s_Instances));
		if (!grown) return NULL;
		s_Instances = grown;
		s_InstanceCapacity = next;
	}
	presentation_instance_t *entry = &s_Instances[s_InstanceCount++];
	memset(entry, 0, sizeof(*entry));
	entry->id = id;
	entry->created_by_transaction = 1;
	return entry;
}

static s32 presentationBegin(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	presentation_instance_t *entry;
	if (!frame || !frame->instance_id) {
		presentationError(err, err_cap,
			"presentation begin requires a stable effect instance ID");
		return -1;
	}
	entry = presentationFind(frame->instance_id);
	if (!entry) entry = presentationCreate(frame->instance_id);
	if (!entry) {
		presentationError(err, err_cap,
			"out of memory reserving presentation instance");
		return -1;
	}
	if (entry->transaction_active || s_PreparedInstanceId) {
		presentationError(err, err_cap,
			"presentation transaction overlaps an active transaction");
		return -1;
	}
	free(entry->staged);
	entry->staged = NULL;
	entry->staged_count = 0;
	entry->staged_capacity = 0;
	entry->transaction_active = 1;
	return 0;
}

static const weapon_graph_ir_param_t *presentationParam(
	const effect_instance_frame_t *frame, const char *key)
{
	return effectInstanceNodeParam(frame, key);
}

static s32 presentationNumber(const effect_instance_frame_t *frame,
	const char *key, f32 fallback, f32 *out, char *err, size_t err_cap)
{
	const weapon_graph_ir_param_t *p = presentationParam(frame, key);
	if (!p) {
		*out = fallback;
		return 0;
	}
	if (p->type == WEAPON_GRAPH_PARAM_FLOAT) *out = p->f_value;
	else if (p->type == WEAPON_GRAPH_PARAM_INT) *out = (f32)p->i_value;
	else {
		presentationError(err, err_cap,
			"presentation numeric parameter has the wrong type");
		return -1;
	}
	if (!presentationFinite(*out)) {
		presentationError(err, err_cap,
			"presentation numeric parameter must be finite");
		return -1;
	}
	return 1;
}

static s32 presentationColorArray(const weapon_graph_ir_param_t *p,
	f32 out[4])
{
	const char *cursor;
	s32 count = 0;
	if (!p || p->type != WEAPON_GRAPH_PARAM_ARRAY) return 0;
	cursor = p->value;
	while (*cursor) {
		char *end = NULL;
		double value;
		while (*cursor && (isspace((unsigned char)*cursor) ||
				*cursor == '[' || *cursor == ']' || *cursor == ',')) cursor++;
		if (!*cursor) break;
		if (count >= 4) return -1;
		value = strtod(cursor, &end);
		if (end == cursor || value != value || value < 0.0 || value > 1.0) {
			return -1;
		}
		out[count++] = (f32)value;
		cursor = end;
	}
	if (count != 3 && count != 4) return -1;
	if (count == 3) out[3] = 1.0f;
	return 1;
}

static s32 presentationStageColor(const effect_instance_frame_t *frame,
	effect_presentation_kind_t kind, effect_presentation_command_t *command,
	char *err, size_t err_cap)
{
	const weapon_graph_ir_param_t *tint = presentationParam(frame, "tint");
	const weapon_graph_ir_param_t *secondary = presentationParam(frame, "tint2");
	const weapon_graph_ir_param_t *material = presentationParam(frame,
		"material_ref");
	const weapon_graph_ir_param_t *texture = presentationParam(frame,
		"texture_ref");
	const weapon_graph_ir_param_t *shader = presentationParam(frame, "shader");
	f32 authored[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	f32 authored_secondary[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	s32 has_tint = 0;

	if (kind == EFFECT_PRESENTATION_GLOW && !material) {
		authored[0] = 1.0f; authored[1] = 0.8f; authored[2] = 0.35f;
	} else if (kind == EFFECT_PRESENTATION_DARKEN && !material) {
		authored[0] = authored[1] = authored[2] = 0.0f;
	}
	if (tint) {
		has_tint = presentationColorArray(tint, authored);
		if (has_tint < 0) {
			presentationError(err, err_cap,
				"presentation tint must be an array of three or four 0..1 numbers");
			return -1;
		}
	}
	if (!secondary) secondary = presentationParam(frame, "tint_secondary");
	memcpy(authored_secondary, authored, sizeof(authored_secondary));
	if (secondary && presentationColorArray(secondary, authored_secondary) < 0) {
		presentationError(err, err_cap,
			"presentation secondary tint must be an array of three or four 0..1 numbers");
		return -1;
	}
	if (material) {
		if (material->type != WEAPON_GRAPH_PARAM_STRING || !material->value[0]) {
			presentationError(err, err_cap,
				"presentation material_ref must be an exact catalog-ID string");
			return -1;
		}
		if (strlen(material->value) >= sizeof(command->material_ref)) {
			presentationError(err, err_cap,
				"presentation material_ref exceeds the catalog-ID limit");
			return -1;
		}
		memcpy(command->material_ref, material->value,
			strlen(material->value) + 1);
	}
	if (texture) {
		const asset_entry_t *entry;
		if (texture->type != WEAPON_GRAPH_PARAM_STRING || !texture->value[0] ||
				strlen(texture->value) >= sizeof(command->texture_ref)) {
			presentationError(err, err_cap,
				"presentation texture_ref must be an exact catalog-ID string");
			return -1;
		}
		memcpy(command->texture_ref, texture->value, strlen(texture->value) + 1);
		entry = assetCatalogResolve(command->texture_ref);
		if (!entry || !entry->enabled || entry->type != ASSET_TEXTURE ||
				entry->source_texnum < 0 || !entry->source.primary.provider) {
			presentationError(err, err_cap,
				"presentation texture_ref has no active public texture source");
			return -1;
		}
		command->texture_num = entry->source_texnum;
	}
	if (shader) {
		if (shader->type != WEAPON_GRAPH_PARAM_STRING || !shader->value[0] ||
				strlen(shader->value) >= sizeof(command->shader_id)) {
			presentationError(err, err_cap,
				"presentation shader must be a nonempty supported identifier");
			return -1;
		}
		memcpy(command->shader_id, shader->value, strlen(shader->value) + 1);
	} else if (frame->runtime->shader_id[0]) {
		if (strlen(frame->runtime->shader_id) >= sizeof(command->shader_id)) {
			presentationError(err, err_cap,
				"presentation descriptor shader_id exceeds the renderer limit");
			return -1;
		}
		memcpy(command->shader_id, frame->runtime->shader_id,
			strlen(frame->runtime->shader_id) + 1);
	}
	if (!effectPresentationShaderSupported(frame->node->kind,
			command->shader_id)) {
		presentationError(err, err_cap,
			"presentation shader identifier has no production renderer pipeline");
		return -1;
	}
	memcpy(command->authored_tint, authored, sizeof(authored));
	memcpy(command->rgba, authored, sizeof(authored));
	memcpy(command->authored_secondary_tint, authored_secondary,
		sizeof(authored_secondary));
	memcpy(command->secondary_rgba, authored_secondary,
		sizeof(authored_secondary));
	(void)has_tint;
	return 0;
}

static s32 presentationResolveChannel(const effect_instance_frame_t *frame,
	effect_presentation_kind_t kind, effect_presentation_command_t *command,
	char *err, size_t err_cap)
{
	const char *target = frame->target_name ? frame->target_name : "";
	const char *attachment = frame->attachment ? frame->attachment : "";
	const s32 screen = presentationScreenTarget(target);

	if (!presentationCopyName(command->target_name, target, "presentation target",
			err, err_cap) ||
			!presentationCopyName(command->attachment, attachment,
				"presentation attachment", err, err_cap)) return -1;
	command->position = frame->position;
	command->source_position = frame->source_position;
	command->target_position = frame->target_position;
	command->primary_direction = frame->primary_direction;
	command->secondary_direction = frame->secondary_direction;
	command->room_count = frame->room_count;
	if (command->room_count < 0 || command->room_count > EFFECT_INSTANCE_ROOM_CAP) {
		presentationError(err, err_cap,
			"presentation frame has an invalid copied room list");
		return -1;
	}
	if (command->room_count) memcpy(command->rooms, frame->rooms,
		(size_t)command->room_count * sizeof(command->rooms[0]));

	if (kind == EFFECT_PRESENTATION_SCREEN || kind == EFFECT_PRESENTATION_DARKEN) {
		if (!screen || attachment[0]) {
			presentationError(err, err_cap,
				"screen and darken effects require a screen/scene/player target and no attachment");
			return -1;
		}
		command->channel = EFFECT_PRESENTATION_CHANNEL_SCREEN;
		return 0;
	}
	if (screen) {
		if (attachment[0]) {
			presentationError(err, err_cap,
				"screen-space presentation does not accept an attachment");
			return -1;
		}
		command->channel = EFFECT_PRESENTATION_CHANNEL_SCREEN;
		return 0;
	}
	if (kind == EFFECT_PRESENTATION_TINT) {
		if (attachment[0] && strcmp(attachment, "point") != 0 &&
				strcmp(attachment, "surface") != 0) {
			presentationError(err, err_cap,
				"world tint attachment must be point or surface");
			return -1;
		}
		command->channel = EFFECT_PRESENTATION_CHANNEL_DECAL;
	} else if (kind == EFFECT_PRESENTATION_GLOW) {
		if (attachment[0] && strcmp(attachment, "point") != 0) {
			presentationError(err, err_cap,
				"world glow attachment must be point");
			return -1;
		}
		command->channel = EFFECT_PRESENTATION_CHANNEL_LIGHT;
	} else if (kind == EFFECT_PRESENTATION_SHIMMER) {
		if (attachment[0] && strcmp(attachment, "beam") != 0) {
			presentationError(err, err_cap,
				"world shimmer attachment must be beam");
			return -1;
		}
		command->channel = EFFECT_PRESENTATION_CHANNEL_BEAM;
	} else {
		presentationError(err, err_cap, "presentation kind has no renderer channel");
		return -1;
	}
	if (!presentationFinite(command->position.x) ||
			!presentationFinite(command->position.y) ||
			!presentationFinite(command->position.z)) {
		presentationError(err, err_cap,
			"world presentation target position must be finite");
		return -1;
	}
	if (command->channel == EFFECT_PRESENTATION_CHANNEL_DECAL &&
			strcmp(command->attachment, "surface") == 0) {
		f32 primary_len2 = command->primary_direction.x * command->primary_direction.x +
			command->primary_direction.y * command->primary_direction.y +
			command->primary_direction.z * command->primary_direction.z;
		f32 secondary_len2 =
			command->secondary_direction.x * command->secondary_direction.x +
			command->secondary_direction.y * command->secondary_direction.y +
			command->secondary_direction.z * command->secondary_direction.z;
		if (!presentationFinite(primary_len2) || !presentationFinite(secondary_len2) ||
				primary_len2 <= 0.000001f || secondary_len2 <= 0.000001f) {
			presentationError(err, err_cap,
				"surface presentation requires two finite nonzero copied axes");
			return -1;
		}
	}
	if (command->channel == EFFECT_PRESENTATION_CHANNEL_BEAM) {
		f32 x = command->target_position.x - command->source_position.x;
		f32 y = command->target_position.y - command->source_position.y;
		f32 z = command->target_position.z - command->source_position.z;
		f32 length2 = x * x + y * y + z * z;
		if (!presentationFinite(length2) || length2 <= 0.000001f) {
			presentationError(err, err_cap,
				"beam presentation requires distinct finite copied endpoints");
			return -1;
		}
	}
	return 0;
}

static s32 presentationAppend(const effect_instance_frame_t *frame,
	effect_presentation_kind_t kind, char *err, size_t err_cap)
{
	presentation_instance_t *entry = frame ? presentationFind(frame->instance_id) : NULL;
	effect_presentation_command_t command;
	f32 sampled;
	f32 descriptor_intensity;

	if (!entry || !entry->transaction_active || !frame->node) {
		presentationError(err, err_cap,
			"presentation node ran outside its instance transaction");
		return -1;
	}
	memset(&command, 0, sizeof(command));
	command.instance_id = frame->instance_id;
	command.kind = kind;
	command.execution_index = frame->execution_index;
	command.priority = frame->priority;
	command.time = frame->time;
	command.size = 16.0f;
	command.width = 0.18f;
	command.glow = 1.0f;
	descriptor_intensity = frame->runtime->descriptor_intensity;
	if (presentationResolveChannel(frame, kind, &command, err, err_cap) != 0)
		return -1;
	if (command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN) command.size = 1.0f;
	if (presentationStageColor(frame, kind, &command, err, err_cap) != 0)
		return -1;
	if (presentationNumber(frame, "intensity", descriptor_intensity,
			&command.intensity,
			err, err_cap) < 0 || command.intensity < 0.0f) {
		presentationError(err, err_cap,
			"presentation intensity must be a finite non-negative number");
		return -1;
	}
	/* The timeline is authored public source too. A sampled value replaces the
	 * node default; failure means the track is simply absent, not a fallback. */
	if (effectInstanceTimelineSample(frame, "intensity", &sampled)) {
		if (!presentationFinite(sampled) || sampled < 0.0f) {
			presentationError(err, err_cap,
				"sampled presentation intensity is invalid");
			return -1;
		}
		command.intensity = sampled;
	}
	if (presentationNumber(frame, "glow", 1.0f, &command.glow,
			err, err_cap) < 0 || command.glow < 0.0f) {
		presentationError(err, err_cap,
			"presentation glow must be a finite non-negative number");
		return -1;
	}
	if (presentationNumber(frame, "size", command.size, &command.size,
			err, err_cap) < 0 || command.size <= 0.0f ||
			(command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN &&
				command.size > 1.0f)) {
		presentationError(err, err_cap,
			"presentation size is invalid for its renderer channel");
		return -1;
	}
	if (kind == EFFECT_PRESENTATION_SHIMMER) {
		command.width = command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN
			? 0.18f : 4.0f;
		if (presentationNumber(frame, "speed", 1.0f, &command.speed,
				err, err_cap) < 0 ||
				presentationNumber(frame, "width", command.width, &command.width,
					err, err_cap) < 0 || command.width <= 0.0f ||
				(command.channel == EFFECT_PRESENTATION_CHANNEL_SCREEN &&
					command.width > 1.0f)) {
			presentationError(err, err_cap,
				"presentation shimmer width is invalid for its renderer channel");
			return -1;
		}
	}
	if (entry->staged_count == entry->staged_capacity) {
		size_t next = entry->staged_capacity ? entry->staged_capacity * 2 : 8;
		effect_presentation_command_t *grown;
		if (next < entry->staged_count + 1 || next > SIZE_MAX / sizeof(*grown)) {
			presentationError(err, err_cap,
				"presentation command capacity overflow");
			return -1;
		}
		grown = (effect_presentation_command_t *)realloc(entry->staged,
			next * sizeof(*grown));
		if (!grown) {
			presentationError(err, err_cap,
				"out of memory staging presentation command");
			return -1;
		}
		entry->staged = grown;
		entry->staged_capacity = next;
	}
	entry->staged[entry->staged_count++] = command;
	return 0;
}

#define DEFINE_PRESENTATION_NODE(name, kind) \
	static s32 name(const effect_instance_frame_t *frame, char *err, size_t cap) \
	{ return presentationAppend(frame, kind, err, cap); }

DEFINE_PRESENTATION_NODE(presentationTint, EFFECT_PRESENTATION_TINT)
DEFINE_PRESENTATION_NODE(presentationGlow, EFFECT_PRESENTATION_GLOW)
DEFINE_PRESENTATION_NODE(presentationShimmer, EFFECT_PRESENTATION_SHIMMER)
DEFINE_PRESENTATION_NODE(presentationDarken, EFFECT_PRESENTATION_DARKEN)
DEFINE_PRESENTATION_NODE(presentationScreen, EFFECT_PRESENTATION_SCREEN)

static s32 presentationGameplayVisual(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	const weapon_graph_ir_param_t *tint = presentationParam(frame, "tint");
	const weapon_graph_ir_param_t *material = presentationParam(frame,
		"material_ref");
	const weapon_graph_ir_param_t *glow = presentationParam(frame, "glow");
	const weapon_graph_ir_param_t *shader = presentationParam(frame, "shader");
	/* Particle's exact texture/material/tint/glow snapshot is committed by
	 * T034 and rendered by the T035 GBI bridge. Observing the slot here keeps
	 * it inside the same global transaction without duplicating its quad. */
	if (frame->node->opcode == WEAPON_GRAPH_OP_EFFECT_PARTICLE) return 0;
	if (glow) return presentationAppend(frame, EFFECT_PRESENTATION_GLOW,
		err, err_cap);
	if (tint || material || shader || frame->runtime->shader_id[0])
		return presentationAppend(frame,
		EFFECT_PRESENTATION_TINT, err, err_cap);
	return 0;
}

static int presentationCommandCompare(const void *lhs, const void *rhs)
{
	const effect_presentation_command_t *a = (const effect_presentation_command_t *)lhs;
	const effect_presentation_command_t *b = (const effect_presentation_command_t *)rhs;
	if (a->priority != b->priority) return a->priority < b->priority ? -1 : 1;
	if (a->instance_id != b->instance_id) return a->instance_id < b->instance_id ? -1 : 1;
	if (a->execution_index != b->execution_index)
		return a->execution_index < b->execution_index ? -1 : 1;
	return 0;
}

static s32 presentationPrepare(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	presentation_instance_t *entry = frame ? presentationFind(frame->instance_id) : NULL;
	size_t retained = 0;
	size_t total;
	size_t out = 0;
	if (!entry || !entry->transaction_active || s_PreparedInstanceId) {
		presentationError(err, err_cap,
			"presentation prepare has no unique active transaction");
		return -1;
	}
	for (size_t i = 0; i < s_RenderCommandCount; i++) {
		if (s_RenderCommands[i].instance_id != frame->instance_id) retained++;
	}
	if (entry->staged_count > SIZE_MAX - retained) {
		presentationError(err, err_cap, "presentation output capacity overflow");
		return -1;
	}
	total = retained + entry->staged_count;
	if (total > SIZE_MAX / sizeof(*s_PreparedCommands)) {
		presentationError(err, err_cap, "presentation output size overflow");
		return -1;
	}
	if (total) {
		s_PreparedCommands = (effect_presentation_command_t *)malloc(
			total * sizeof(*s_PreparedCommands));
		if (!s_PreparedCommands) {
			presentationError(err, err_cap,
				"out of memory preparing renderer output");
			return -1;
		}
	}
	for (size_t i = 0; i < s_RenderCommandCount; i++) {
		if (s_RenderCommands[i].instance_id != frame->instance_id)
			s_PreparedCommands[out++] = s_RenderCommands[i];
	}
	for (size_t i = 0; i < entry->staged_count; i++)
		s_PreparedCommands[out++] = entry->staged[i];
	/* Resolve and retain every source-backed material only after all node
	 * callbacks succeeded. A disable/replacement between stage and prepare
	 * therefore fails without mutating the previously committed frame. */
	for (size_t i = retained; i < total; i++) {
		effect_presentation_command_t *command = &s_PreparedCommands[i];
		if (command->material_ref[0]) {
			const asset_runtime_binding_t *binding = assetRuntimeFindByTypeAndId(
				ASSET_MATERIAL, command->material_ref);
			if (!binding || !binding->source_hydrated) {
				free(s_PreparedCommands);
				s_PreparedCommands = NULL;
				presentationError(err, err_cap,
					"presentation material_ref has no active hydrated public source");
				return -1;
			}
			for (s32 channel = 0; channel < 4; channel++) {
				command->rgba[channel] = binding->material_base_color[channel] *
					command->authored_tint[channel];
				command->secondary_rgba[channel] =
					binding->material_base_color[channel] *
					command->authored_secondary_tint[channel];
			}
			memcpy(command->material_shading_model,
				binding->material_shading_model,
				sizeof(command->material_shading_model));
			command->material_roughness = binding->material_roughness;
			command->material_metallic = binding->material_metallic;
			command->material_emissive = binding->material_emissive;
		}
		if (command->texture_ref[0]) {
			const asset_entry_t *texture = assetCatalogResolve(command->texture_ref);
			if (!texture || !texture->enabled || texture->type != ASSET_TEXTURE ||
					texture->source_texnum < 0 || !texture->source.primary.provider) {
				free(s_PreparedCommands);
				s_PreparedCommands = NULL;
				presentationError(err, err_cap,
					"presentation texture_ref has no active public texture source");
				return -1;
			}
			if (command->texture_num != texture->source_texnum) {
				free(s_PreparedCommands);
				s_PreparedCommands = NULL;
				presentationError(err, err_cap,
					"presentation texture source changed before prepare");
				return -1;
			}
		}
	}
	if (total > 1) qsort(s_PreparedCommands, total,
		sizeof(*s_PreparedCommands), presentationCommandCompare);
	s_PreparedCommandCount = total;
	s_PreparedInstanceId = frame->instance_id;
	return 0;
}

static void presentationCommit(const effect_instance_frame_t *frame)
{
	presentation_instance_t *entry = frame ? presentationFind(frame->instance_id) : NULL;
	if (!entry || !entry->transaction_active ||
			s_PreparedInstanceId != frame->instance_id) return;
	free(s_RenderCommands);
	s_RenderCommands = s_PreparedCommands;
	s_RenderCommandCount = s_PreparedCommandCount;
	s_PreparedCommands = NULL;
	s_PreparedCommandCount = 0;
	s_PreparedInstanceId = 0;
	free(entry->committed);
	entry->committed = entry->staged;
	entry->committed_count = entry->staged_count;
	entry->staged = NULL;
	entry->staged_count = 0;
	entry->staged_capacity = 0;
	entry->transaction_active = 0;
	entry->created_by_transaction = 0;
	if (effectInstanceRuntimeAuditEnabled() && frame && frame->runtime) {
		sysLogPrintf(LOG_NOTE,
			"EFFECT.PRESENTATION.AUDIT: committed asset=%s instance=%llu snapshots=%zu",
			frame->runtime->asset_id, (unsigned long long)frame->instance_id,
			s_RenderCommandCount);
	}
}

static void presentationRemoveAt(size_t index)
{
	free(s_Instances[index].committed);
	free(s_Instances[index].staged);
	if (index + 1 < s_InstanceCount) {
		memmove(&s_Instances[index], &s_Instances[index + 1],
			(s_InstanceCount - index - 1) * sizeof(*s_Instances));
	}
	s_InstanceCount--;
}

static void presentationRollback(const effect_instance_frame_t *frame,
	s32 attempted_count)
{
	presentation_instance_t *entry = frame ? presentationFind(frame->instance_id) : NULL;
	(void)attempted_count;
	free(s_PreparedCommands);
	s_PreparedCommands = NULL;
	s_PreparedCommandCount = 0;
	s_PreparedInstanceId = 0;
	if (!entry) return;
	free(entry->staged);
	entry->staged = NULL;
	entry->staged_count = 0;
	entry->staged_capacity = 0;
	entry->transaction_active = 0;
	if (entry->created_by_transaction && entry->committed_count == 0) {
		size_t index = (size_t)(entry - s_Instances);
		presentationRemoveAt(index);
	}
}

static s32 presentationUpdate(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	/* T036 invokes this once for per-instance orchestration, then routes each
	 * selected node through its permanent slot below. Presentation sampling is
	 * consequently staged exactly once by those node callbacks. */
	(void)frame;
	(void)err;
	(void)err_cap;
	return 0;
}

static void presentationCleanup(u64 instance_id)
{
	size_t out = 0;
	for (size_t i = 0; i < s_RenderCommandCount; i++) {
		if (s_RenderCommands[i].instance_id != instance_id)
			s_RenderCommands[out++] = s_RenderCommands[i];
	}
	s_RenderCommandCount = out;
	for (size_t i = 0; i < s_InstanceCount; i++) {
		if (s_Instances[i].id == instance_id) {
			presentationRemoveAt(i);
			break;
		}
	}
}

s32 effectPresentationRuntimeBuildConsumer(effect_instance_consumer_t *out,
	char *err, size_t err_cap)
{
	if (err && err_cap) err[0] = '\0';
	if (!out) {
		presentationError(err, err_cap,
			"presentation consumer output is required");
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->name = "public-pdeffect-presentation";
	out->begin = presentationBegin;
	out->prepare = presentationPrepare;
	out->commit = presentationCommit;
	out->rollback = presentationRollback;
	out->update = presentationUpdate;
	out->cleanup = presentationCleanup;
	out->nodes[EFFECT_GRAPH_DISPATCH_TINT] = presentationTint;
	out->nodes[EFFECT_GRAPH_DISPATCH_GLOW] = presentationGlow;
	out->nodes[EFFECT_GRAPH_DISPATCH_SHIMMER] = presentationShimmer;
	out->nodes[EFFECT_GRAPH_DISPATCH_DARKEN] = presentationDarken;
	out->nodes[EFFECT_GRAPH_DISPATCH_SCREEN] = presentationScreen;
	out->nodes[EFFECT_GRAPH_DISPATCH_PARTICLE] = presentationGameplayVisual;
	out->nodes[EFFECT_GRAPH_DISPATCH_EXPLOSION] = presentationGameplayVisual;
	out->nodes[EFFECT_GRAPH_DISPATCH_SPARK] = presentationGameplayVisual;
	out->nodes[EFFECT_GRAPH_DISPATCH_SMOKE] = presentationGameplayVisual;
	return 0;
}

size_t effectPresentationRuntimeSnapshotCount(void)
{
	return s_RenderCommandCount;
}

s32 effectPresentationRuntimeSnapshot(size_t index,
	effect_presentation_command_t *out)
{
	if (!out || index >= s_RenderCommandCount) return 0;
	*out = s_RenderCommands[index];
	return 1;
}

s32 effectPresentationRuntimeHasActive(void)
{
	return s_RenderCommandCount > 0 ? 1 : 0;
}

void effectPresentationRuntimeClearAll(void)
{
	for (size_t i = 0; i < s_InstanceCount; i++) {
		free(s_Instances[i].committed);
		free(s_Instances[i].staged);
	}
	free(s_Instances);
	free(s_RenderCommands);
	free(s_PreparedCommands);
	s_Instances = NULL;
	s_InstanceCount = 0;
	s_InstanceCapacity = 0;
	s_RenderCommands = NULL;
	s_RenderCommandCount = 0;
	s_PreparedCommands = NULL;
	s_PreparedCommandCount = 0;
	s_PreparedInstanceId = 0;
}
