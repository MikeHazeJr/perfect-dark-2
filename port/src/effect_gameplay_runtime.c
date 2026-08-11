/**
 * effect_gameplay_runtime.c -- transactional v1 gameplay/audio execution.
 */
#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "asset_runtime.h"
#include "assetcatalog.h"
#include "effect_gameplay_runtime.h"
#include "effect_presentation_runtime.h"
#include "game/explosions.h"
#include "game/prop.h"
#include "game/propsnd.h"
#include "game/smoke.h"
#include "game/sparks.h"
#include "game/propobj.h"
#include "system.h"
#include "types.h"

typedef enum gameplay_command_kind {
	GAMEPLAY_COMMAND_EXPLOSION = 1,
	GAMEPLAY_COMMAND_SPARK,
	GAMEPLAY_COMMAND_SMOKE,
	GAMEPLAY_COMMAND_AUDIO,
	GAMEPLAY_COMMAND_PARTICLE,
} gameplay_command_kind_t;

typedef struct gameplay_command {
	gameplay_command_kind_t kind;
	u64 instance_id;
	struct prop *target_prop;
	struct coord position;
	struct coord primary_direction;
	struct coord secondary_direction;
	RoomNum rooms[EFFECT_INSTANCE_ROOM_CAP + 1];
	s32 room_count;
	s32 playernum;
	effect_instance_action_mode_t action_mode;
	s32 native_type;
	s32 custom_spark;
	u32 spark_color1;
	u32 spark_color2;
	s16 soundnum;
	effect_gameplay_particle_snapshot_t particle;
	f32 particle_authored_tint[4];
	f32 particle_glow_scale;
} gameplay_command_t;

typedef struct gameplay_transaction {
	u64 instance_id;
	gameplay_command_t *commands;
	size_t count;
	size_t capacity;
	s32 prepared;
	s32 spark_checkpoint;
	s32 spark_checkpoint_active;
} gameplay_transaction_t;

static gameplay_transaction_t s_Transaction;
static effect_gameplay_particle_snapshot_t *s_Particles;
static size_t s_ParticleCount;
static size_t s_ParticleCapacity;

static s32 finiteNumber(double value)
{
	return value == value && value <= DBL_MAX && value >= -DBL_MAX;
}

static void setError(char *err, size_t err_cap, const char *message)
{
	if (err && err_cap) snprintf(err, err_cap, "%s", message ? message : "effect gameplay failure");
}

static void setNodeError(const effect_instance_frame_t *frame,
	char *err, size_t err_cap, const char *message)
{
	if (err && err_cap) {
		snprintf(err, err_cap, "effect node %s %s",
			frame && frame->node ? frame->node->id : "<unknown>", message);
	}
}

static s32 checkedGrow(void **items, size_t *capacity, size_t needed,
	size_t stride)
{
	size_t next;
	void *grown;

	if (needed <= *capacity) return 1;
	next = *capacity ? *capacity : 16;
	while (next < needed) {
		if (next > SIZE_MAX / 2) return 0;
		next *= 2;
	}
	if (!stride || next > SIZE_MAX / stride) return 0;
	grown = realloc(*items, next * stride);
	if (!grown) return 0;
	*items = grown;
	*capacity = next;
	return 1;
}

static const weapon_graph_ir_param_t *nodeParam(
	const effect_instance_frame_t *frame, const char *key)
{
	return effectInstanceNodeParam(frame, key);
}

static s32 paramString(const effect_instance_frame_t *frame, const char *key,
	const char **out)
{
	const weapon_graph_ir_param_t *param = nodeParam(frame, key);
	if (!param) return 0;
	if (param->type != WEAPON_GRAPH_PARAM_STRING || !param->value[0]) return -1;
	*out = param->value;
	return 1;
}

static s32 paramNumber(const effect_instance_frame_t *frame, const char *key,
	f32 *out)
{
	const weapon_graph_ir_param_t *param = nodeParam(frame, key);
	if (!param) return 0;
	if (param->type == WEAPON_GRAPH_PARAM_FLOAT) *out = param->f_value;
	else if (param->type == WEAPON_GRAPH_PARAM_INT) *out = (f32)param->i_value;
	else return -1;
	return finiteNumber(*out) ? 1 : -1;
}

static s32 paramColor(const effect_instance_frame_t *frame, const char *key,
	f32 out[4])
{
	const weapon_graph_ir_param_t *param = nodeParam(frame, key);
	const char *cursor;
	s32 count = 0;

	if (!param) return 0;
	if (param->type != WEAPON_GRAPH_PARAM_ARRAY) return -1;
	cursor = param->value;
	while (*cursor && count < 4) {
		char *after;
		double value;
		while (*cursor && (isspace((unsigned char)*cursor) || *cursor == '[' ||
				*cursor == ']' || *cursor == ',')) cursor++;
		if (!*cursor) break;
		value = strtod(cursor, &after);
		if (after == cursor || !finiteNumber(value) || value < 0.0 || value > 1.0) return -1;
		out[count++] = (f32)value;
		cursor = after;
	}
	while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ']' ||
			*cursor == ',')) cursor++;
	if (*cursor) return -1;
	if (count != 3 && count != 4) return -1;
	if (count == 3) out[3] = 1.0f;
	return 1;
}

static u8 colorByte(f32 value)
{
	if (value < 0) value = 0;
	if (value > 1) value = 1;
	return (u8)(value * 255.0f + 0.5f);
}

static u32 colorWord(const f32 color[4])
{
	return ((u32)colorByte(color[0]) << 24) |
		((u32)colorByte(color[1]) << 16) |
		((u32)colorByte(color[2]) << 8) | colorByte(color[3]);
}

static s32 explosionClassType(const char *value)
{
	if (!value) return -1;
	if (!strcmp(value, "tiny")) return EXPLOSIONTYPE_6;
	if (!strcmp(value, "small")) return EXPLOSIONTYPE_EYESPY;
	if (!strcmp(value, "medium")) return EXPLOSIONTYPE_11;
	if (!strcmp(value, "large")) return EXPLOSIONTYPE_ROCKET;
	if (!strcmp(value, "huge")) return EXPLOSIONTYPE_HUGE17;
	if (!strcmp(value, "massive")) return EXPLOSIONTYPE_HUGE25;
	return -1;
}

static s32 smokeClassType(const char *value)
{
	if (!value) return -1;
	if (!strcmp(value, "none")) return SMOKETYPE_NONE;
	if (!strcmp(value, "electrical")) return SMOKETYPE_ELECTRICAL;
	if (!strcmp(value, "tiny") || !strcmp(value, "mini")) return SMOKETYPE_MINI;
	if (!strcmp(value, "small")) return SMOKETYPE_SMALL;
	if (!strcmp(value, "medium")) return SMOKETYPE_MEDIUM;
	if (!strcmp(value, "large") || !strcmp(value, "huge") || !strcmp(value, "massive")) return SMOKETYPE_LARGE;
	if (!strcmp(value, "bullet_impact")) return SMOKETYPE_BULLETIMPACT;
	if (!strcmp(value, "rocket_tail")) return SMOKETYPE_ROCKETTAIL;
	if (!strcmp(value, "grenade_tail")) return SMOKETYPE_GRENADETAIL;
	if (!strcmp(value, "homing_tail")) return SMOKETYPE_HOMINGTAIL;
	if (!strcmp(value, "pinball")) return SMOKETYPE_PINBALL;
	if (!strcmp(value, "water")) return SMOKETYPE_WATER;
	if (!strcmp(value, "debris")) return SMOKETYPE_DEBRIS;
	return -1;
}

static s32 resolveCatalogAudio(const char *catalog_id, s16 *soundnum)
{
	catalog_audio_result_t audio = {0};
	union soundnumhack native_ref;

	if (!catalog_id || !catalog_id[0] || !catalogResolveAudio(catalog_id, &audio) ||
			audio.sound_id <= 0 || audio.sound_id > SHRT_MAX) return 0;
	native_ref.packed = (s16)audio.sound_id;
	if (audio.category != AUDIO_CAT_SFX &&
			!(audio.category == AUDIO_CAT_VOICE && native_ref.hasconfig)) return 0;
	*soundnum = (s16)audio.sound_id;
	return 1;
}

static s32 copyFrameSpatial(const effect_instance_frame_t *frame,
	gameplay_command_t *command, char *err, size_t err_cap)
{
	s32 i;
	if (!frame || !command || frame->room_count <= 0 ||
			frame->room_count > EFFECT_INSTANCE_ROOM_CAP ||
			!finiteNumber(frame->position.x) || !finiteNumber(frame->position.y) ||
			!finiteNumber(frame->position.z)) {
		setNodeError(frame, err, err_cap, "has invalid spatial target");
		return 0;
	}
	command->instance_id = frame->instance_id;
	command->target_prop = (struct prop *)frame->target_prop;
	command->position.x = frame->position.x;
	command->position.y = frame->position.y;
	command->position.z = frame->position.z;
	command->primary_direction.x = frame->primary_direction.x;
	command->primary_direction.y = frame->primary_direction.y;
	command->primary_direction.z = frame->primary_direction.z;
	command->secondary_direction.x = frame->secondary_direction.x;
	command->secondary_direction.y = frame->secondary_direction.y;
	command->secondary_direction.z = frame->secondary_direction.z;
	command->room_count = frame->room_count;
	for (i = 0; i < frame->room_count; i++) {
		if (frame->rooms[i] < 0) {
			setNodeError(frame, err, err_cap, "has an invalid room");
			return 0;
		}
		command->rooms[i] = frame->rooms[i];
	}
	command->rooms[frame->room_count] = -1;
	command->playernum = frame->playernum;
	command->action_mode = frame->action_mode;
	return 1;
}

static gameplay_command_t *appendCommand(const effect_instance_frame_t *frame,
	gameplay_command_kind_t kind, char *err, size_t err_cap)
{
	gameplay_command_t *command;
	if (!checkedGrow((void **)&s_Transaction.commands, &s_Transaction.capacity,
			s_Transaction.count + 1, sizeof(*s_Transaction.commands))) {
		setNodeError(frame, err, err_cap, "could not grow its transaction");
		return NULL;
	}
	command = &s_Transaction.commands[s_Transaction.count++];
	memset(command, 0, sizeof(*command));
	command->kind = kind;
	return command;
}

static s32 stageAudio(const effect_instance_frame_t *frame, s16 soundnum,
	char *err, size_t err_cap)
{
	gameplay_command_t *command;
	if (soundnum <= 0) return 1;
	command = appendCommand(frame, GAMEPLAY_COMMAND_AUDIO, err, err_cap);
	if (!command || !copyFrameSpatial(frame, command, err, err_cap)) return 0;
	command->soundnum = soundnum;
	return 1;
}

static s32 resolveOptionalNodeAudio(const effect_instance_frame_t *frame,
	s16 *soundnum, s32 *present, char *err, size_t err_cap)
{
	const char *catalog_id = NULL;
	s32 result = paramString(frame, "audio_catalog_id", &catalog_id);
	if (nodeParam(frame, "sound") || nodeParam(frame, "soundnum")) {
		setNodeError(frame, err, err_cap, "uses forbidden numeric audio; use audio_catalog_id");
		return 0;
	}
	if (result < 0) {
		setNodeError(frame, err, err_cap, "has invalid audio_catalog_id");
		return 0;
	}
	*present = result > 0;
	if (*present && !resolveCatalogAudio(catalog_id, soundnum)) {
		setNodeError(frame, err, err_cap, "has unresolved or non-SFX audio_catalog_id");
		return 0;
	}
	return 1;
}

static s32 stageExplosion(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	const char *class_name = NULL;
	gameplay_command_t *command;
	s16 soundnum = 0;
	s32 has_audio = 0;
	s32 type;

	if (frame->time > 0.0f) return 0;
	if (paramString(frame, "explosion_class", &class_name) <= 0 ||
			(type = explosionClassType(class_name)) < 0) {
		setNodeError(frame, err, err_cap, "has invalid explosion_class");
		return -1;
	}
	if (!resolveOptionalNodeAudio(frame, &soundnum, &has_audio, err, err_cap)) return -1;
	if (!has_audio) soundnum = (s16)explosionTypeFor(type)->sound;
	command = appendCommand(frame, GAMEPLAY_COMMAND_EXPLOSION, err, err_cap);
	if (!command || !copyFrameSpatial(frame, command, err, err_cap)) return -1;
	command->native_type = type;
	/* Always suppress delayed profile audio; the separately staged sound is
	 * capacity-reserved and committed in the same all-or-nothing transaction. */
	command->soundnum = 0;
	return stageAudio(frame, soundnum, err, err_cap) ? 0 : -1;
}

static s32 stageSpark(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	gameplay_command_t *command;
	f32 tint[4] = {1, 1, 0.5f, 1};
	f32 tint2[4] = {1, 1, 1, 1};
	s16 soundnum = 0;
	s32 has_audio = 0;
	s32 color_status;
	s32 color2_status;
	s32 type = SPARKTYPE_PROJECTILE;

	if (frame->time > 0.0f) return 0;
	color_status = paramColor(frame, "tint", tint);
	color2_status = paramColor(frame, "tint2", tint2);
	if (color2_status == 0) {
		color2_status = paramColor(frame, "tint_secondary", tint2);
	}
	if (color_status < 0 || color2_status < 0) {
		setNodeError(frame, err, err_cap, "has invalid spark tint");
		return -1;
	}
	if (!resolveOptionalNodeAudio(frame, &soundnum, &has_audio, err, err_cap)) return -1;
	command = appendCommand(frame, GAMEPLAY_COMMAND_SPARK, err, err_cap);
	if (!command || !copyFrameSpatial(frame, command, err, err_cap)) return -1;
	command->native_type = type;
	if (color_status > 0) {
		command->custom_spark = 1;
		command->spark_color1 = colorWord(tint);
		command->spark_color2 = colorWord(tint2);
	}
	if (has_audio && !stageAudio(frame, soundnum, err, err_cap)) return -1;
	return 0;
}

static s32 stageSmoke(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	const char *class_name = NULL;
	const weapon_graph_ir_param_t *direct;
	gameplay_command_t *command;
	s16 soundnum = 0;
	s32 has_audio = 0;
	s32 type = -1;

	if (frame->time > 0.0f) return 0;
	if (paramString(frame, "smoke_class", &class_name) > 0 ||
			paramString(frame, "class", &class_name) > 0) type = smokeClassType(class_name);
	direct = nodeParam(frame, "smoke_type");
	if (type < 0 && direct && direct->type == WEAPON_GRAPH_PARAM_INT &&
			direct->i_value >= SMOKETYPE_NONE && direct->i_value <= SMOKETYPE_UFO) {
		type = direct->i_value;
	}
	if (type < 0) {
		setNodeError(frame, err, err_cap, "has invalid smoke class/type");
		return -1;
	}
	if (!resolveOptionalNodeAudio(frame, &soundnum, &has_audio, err, err_cap)) return -1;
	command = appendCommand(frame, GAMEPLAY_COMMAND_SMOKE, err, err_cap);
	if (!command || !copyFrameSpatial(frame, command, err, err_cap)) return -1;
	command->native_type = type;
	if (has_audio && !stageAudio(frame, soundnum, err, err_cap)) return -1;
	return 0;
}

static s32 stageParticle(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	const char *texture_id = NULL;
	const char *material_id = NULL;
	const char *shader_id = NULL;
	const asset_entry_t *texture;
	const asset_runtime_binding_t *material = NULL;
	gameplay_command_t *command;
	effect_gameplay_particle_snapshot_t *particle;
	f32 value;
	f32 authored_tint[4] = {1, 1, 1, 1};
	s16 soundnum = 0;
	s32 has_audio = 0;
	s32 number_status;
	s32 i;

	if (paramString(frame, "texture_ref", &texture_id) <= 0) {
		setNodeError(frame, err, err_cap, "requires source-backed texture_ref");
		return -1;
	}
	texture = assetCatalogResolve(texture_id);
	if (!texture || !texture->enabled || texture->type != ASSET_TEXTURE ||
			texture->source_texnum < 0 || !texture->source.primary.provider) {
		setNodeError(frame, err, err_cap, "has unresolved texture_ref");
		return -1;
	}
	if (paramString(frame, "material_ref", &material_id) < 0) {
		setNodeError(frame, err, err_cap, "has invalid material_ref");
		return -1;
	}
	if (material_id) {
		material = assetRuntimeFindByTypeAndId(ASSET_MATERIAL, material_id);
		if (!material || !material->source_hydrated) {
			setNodeError(frame, err, err_cap, "has unresolved material_ref");
			return -1;
		}
	}
	if (paramString(frame, "shader", &shader_id) < 0) {
		setNodeError(frame, err, err_cap, "has invalid shader");
		return -1;
	}
	if (!effectPresentationShaderSupported(frame->node->kind,
			shader_id ? shader_id : frame->runtime->shader_id)) {
		setNodeError(frame, err, err_cap,
			"has a shader with no production particle pipeline");
		return -1;
	}
	if (paramColor(frame, "tint", authored_tint) < 0) {
		setNodeError(frame, err, err_cap, "has invalid particle tint");
		return -1;
	}
	if (!resolveOptionalNodeAudio(frame, &soundnum, &has_audio, err, err_cap)) return -1;

	command = appendCommand(frame, GAMEPLAY_COMMAND_PARTICLE, err, err_cap);
	if (!command || !copyFrameSpatial(frame, command, err, err_cap)) return -1;
	particle = &command->particle;
	particle->instance_id = frame->instance_id;
	particle->node_index = frame->node_index;
	particle->execution_index = frame->execution_index;
	snprintf(particle->texture_id, sizeof(particle->texture_id), "%s", texture_id);
	if (material_id) snprintf(particle->material_id, sizeof(particle->material_id), "%s", material_id);
	if (material) snprintf(particle->material_shading_model,
		sizeof(particle->material_shading_model), "%s",
		material->material_shading_model);
	if (shader_id) snprintf(particle->shader_id, sizeof(particle->shader_id),
		"%s", shader_id);
	else if (frame->runtime->shader_id[0]) snprintf(particle->shader_id,
		sizeof(particle->shader_id), "%s", frame->runtime->shader_id);
	particle->texture_num = texture->source_texnum;
	particle->position = frame->position;
	particle->size = 16.0f;
	particle->intensity = frame->runtime->descriptor_intensity;
	particle->glow = material ? (material->material_emissive ? 1.0f : 0.0f) : 1.0f;
	particle->material_roughness = material ? material->material_roughness : 0.0f;
	particle->material_metallic = material ? material->material_metallic : 0.0f;
	particle->material_emissive = material ? material->material_emissive : 0;
	particle->age = frame->time;
	particle->lifetime = frame->lifetime;
	for (i = 0; i < 4; i++) {
		command->particle_authored_tint[i] = authored_tint[i];
		particle->tint[i] = authored_tint[i] *
			(material ? material->material_base_color[i] : 1.0f);
	}
	command->particle_glow_scale = 1.0f;
	number_status = paramNumber(frame, "size", &value);
	if (number_status < 0 || (number_status > 0 && value <= 0)) {
		if (number_status != 0) {
			setNodeError(frame, err, err_cap, "has invalid particle size");
			return -1;
		}
	} else if (number_status > 0) particle->size = value;
	number_status = paramNumber(frame, "intensity", &value);
	if (number_status < 0 || (number_status > 0 && value < 0)) {
		if (number_status != 0) {
			setNodeError(frame, err, err_cap, "has invalid particle intensity");
			return -1;
		}
	} else if (number_status > 0) particle->intensity = value;
	if (effectInstanceTimelineSample(frame, "intensity", &value)) {
		if (!finiteNumber(value) || value < 0.0f) {
			setNodeError(frame, err, err_cap,
				"has invalid sampled particle intensity");
			return -1;
		}
		particle->intensity = value;
	}
	number_status = paramNumber(frame, "glow", &value);
	if (number_status < 0 || (number_status > 0 && value < 0)) {
		if (number_status != 0) {
			setNodeError(frame, err, err_cap, "has invalid particle glow");
			return -1;
		}
	} else if (number_status > 0) {
		command->particle_glow_scale = value;
		particle->glow *= value;
	}

	if (frame->time <= 0.0f && has_audio && !stageAudio(frame, soundnum, err, err_cap)) return -1;
	return 0;
}

static s32 gameplayBegin(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	if (!frame || !frame->instance_id) {
		setError(err, err_cap, "effect gameplay transaction has no instance");
		return -1;
	}
	if (s_Transaction.spark_checkpoint_active) {
		sparksRollbackCustomTypes(s_Transaction.spark_checkpoint);
	}
	s_Transaction.instance_id = frame->instance_id;
	s_Transaction.count = 0;
	s_Transaction.prepared = 0;
	s_Transaction.spark_checkpoint_active = 0;
	return 0;
}

static s32 gameplayUpdate(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	(void)frame; (void)err; (void)err_cap;
	return 0;
}

static s32 gameplayPrepare(const effect_instance_frame_t *frame,
	char *err, size_t err_cap)
{
	s32 explosions = 0;
	s32 smokes = 0;
	s32 sounds = 0;
	s32 particles = 0;
	s32 props;
	size_t i;

	(void)frame;
	for (i = 0; i < s_Transaction.count; i++) {
		gameplay_command_t *command = &s_Transaction.commands[i];
		switch (s_Transaction.commands[i].kind) {
		case GAMEPLAY_COMMAND_EXPLOSION: explosions++; break;
		case GAMEPLAY_COMMAND_SMOKE: smokes++; break;
		case GAMEPLAY_COMMAND_AUDIO: sounds++; break;
		case GAMEPLAY_COMMAND_PARTICLE: {
			const asset_entry_t *texture = assetCatalogResolve(
				command->particle.texture_id);
			const asset_runtime_binding_t *material = NULL;
			s32 channel;
			if (!texture || !texture->enabled || texture->type != ASSET_TEXTURE ||
					texture->source_texnum < 0 || !texture->source.primary.provider ||
					texture->source_texnum != command->particle.texture_num) {
				setError(err, err_cap, "effect particle texture changed before prepare");
				return -1;
			}
			if (command->particle.material_id[0]) {
				material = assetRuntimeFindByTypeAndId(ASSET_MATERIAL,
					command->particle.material_id);
				if (!material || !material->source_hydrated) {
					setError(err, err_cap, "effect particle material changed before prepare");
					return -1;
				}
			}
			if (material) {
				snprintf(command->particle.material_shading_model,
					sizeof(command->particle.material_shading_model), "%s",
					material->material_shading_model);
				command->particle.material_roughness = material->material_roughness;
				command->particle.material_metallic = material->material_metallic;
				command->particle.material_emissive = material->material_emissive;
			}
			for (channel = 0; channel < 4; channel++) {
				command->particle.tint[channel] =
					command->particle_authored_tint[channel] *
					(material ? material->material_base_color[channel] : 1.0f);
			}
			command->particle.glow = material
				? (material->material_emissive ? command->particle_glow_scale : 0.0f)
				: command->particle_glow_scale;
			particles++;
			break;
		}
		default: break;
		}
	}
	if (explosions > INT_MAX - smokes) {
		setError(err, err_cap, "effect gameplay resource count overflow");
		return -1;
	}
	props = explosions + smokes;
	if (!explosionsReserveCreateCount(explosions) ||
			!smokesReserveCreateCount(smokes) ||
			!propsReserveCreateCount(props) || !psReserveCreateCount(sounds)) {
		setError(err, err_cap, "effect gameplay native resources are exhausted");
		return -1;
	}
	if (particles && !checkedGrow((void **)&s_Particles, &s_ParticleCapacity,
			s_ParticleCount + (size_t)particles, sizeof(*s_Particles))) {
		setError(err, err_cap, "effect particle registry allocation failed");
		return -1;
	}
	s_Transaction.spark_checkpoint = sparksCustomTypeCheckpoint();
	s_Transaction.spark_checkpoint_active = 1;
	for (i = 0; i < s_Transaction.count; i++) {
		gameplay_command_t *command = &s_Transaction.commands[i];
		if (command->kind == GAMEPLAY_COMMAND_SPARK && command->custom_spark) {
			command->native_type = sparksRegisterCustomTintedType(
				command->spark_color1, command->spark_color2);
			if (command->native_type < SPARKTYPE_BASE_COUNT) {
				sparksRollbackCustomTypes(s_Transaction.spark_checkpoint);
				s_Transaction.spark_checkpoint_active = 0;
				setError(err, err_cap, "effect custom spark reservation failed");
				return -1;
			}
		}
	}
	s_Transaction.prepared = 1;
	return 0;
}

static void commitParticle(const effect_gameplay_particle_snapshot_t *particle)
{
	size_t i;
	for (i = 0; i < s_ParticleCount; i++) {
		if (s_Particles[i].instance_id == particle->instance_id &&
				s_Particles[i].node_index == particle->node_index &&
				s_Particles[i].execution_index == particle->execution_index) {
			s_Particles[i] = *particle;
			return;
		}
	}
	s_Particles[s_ParticleCount++] = *particle;
}

static void gameplayCommit(const effect_instance_frame_t *frame)
{
	size_t i;
	(void)frame;
	if (!s_Transaction.prepared) return;
	for (i = 0; i < s_Transaction.count; i++) {
		gameplay_command_t *command = &s_Transaction.commands[i];
		switch (command->kind) {
		case GAMEPLAY_COMMAND_EXPLOSION:
			if (command->action_mode == EFFECT_INSTANCE_ACTION_PROP_DETONATION &&
					command->target_prop) {
				propExplodeWithSound(command->target_prop,
					command->native_type, command->soundnum);
			} else {
				explosionCreateComplexWithSound(NULL, &command->position,
					command->rooms, (s16)command->native_type,
					command->playernum, command->soundnum);
			}
			break;
		case GAMEPLAY_COMMAND_SPARK:
			sparksCreate(command->rooms[0], command->target_prop,
				&command->position, &command->primary_direction,
				&command->secondary_direction, command->native_type);
			break;
		case GAMEPLAY_COMMAND_SMOKE:
			smokeCreate(&command->position, command->rooms,
				(s16)command->native_type);
			break;
		case GAMEPLAY_COMMAND_AUDIO:
			psCreate(NULL, command->target_prop, command->soundnum, -1, -1,
				0, 0, PSTYPE_NONE, &command->position, -1.0f,
				command->rooms, -1, -1.0f, -1.0f, -1.0f);
			break;
		case GAMEPLAY_COMMAND_PARTICLE:
			commitParticle(&command->particle);
			break;
		}
	}
	s_Transaction.count = 0;
	s_Transaction.prepared = 0;
	s_Transaction.spark_checkpoint_active = 0;
}

static void gameplayRollback(const effect_instance_frame_t *frame,
	s32 attempted_count)
{
	(void)frame; (void)attempted_count;
	if (s_Transaction.spark_checkpoint_active) {
		sparksRollbackCustomTypes(s_Transaction.spark_checkpoint);
	}
	s_Transaction.count = 0;
	s_Transaction.prepared = 0;
	s_Transaction.spark_checkpoint_active = 0;
}

static void gameplayCleanup(u64 instance_id)
{
	size_t i;
	for (i = s_ParticleCount; i-- > 0;) {
		if (s_Particles[i].instance_id == instance_id) {
			if (i + 1 < s_ParticleCount) {
				memmove(&s_Particles[i], &s_Particles[i + 1],
					(s_ParticleCount - i - 1) * sizeof(*s_Particles));
			}
			s_ParticleCount--;
		}
	}
	if (s_Transaction.instance_id == instance_id) gameplayRollback(NULL, 0);
}

s32 effectGameplayRuntimeBuildConsumer(effect_instance_consumer_t *out,
	char *err, size_t err_cap)
{
	if (!out) {
		setError(err, err_cap, "effect gameplay consumer output is missing");
		return -1;
	}
	memset(out, 0, sizeof(*out));
	out->name = "gameplay_audio";
	out->begin = gameplayBegin;
	out->prepare = gameplayPrepare;
	out->commit = gameplayCommit;
	out->rollback = gameplayRollback;
	out->update = gameplayUpdate;
	out->cleanup = gameplayCleanup;
	out->nodes[EFFECT_GRAPH_DISPATCH_PARTICLE] = stageParticle;
	out->nodes[EFFECT_GRAPH_DISPATCH_EXPLOSION] = stageExplosion;
	out->nodes[EFFECT_GRAPH_DISPATCH_SPARK] = stageSpark;
	out->nodes[EFFECT_GRAPH_DISPATCH_SMOKE] = stageSmoke;
	if (err && err_cap) err[0] = '\0';
	return 0;
}

size_t effectGameplayRuntimeParticleCount(void)
{
	return s_ParticleCount;
}

s32 effectGameplayRuntimeParticleSnapshot(size_t index,
	effect_gameplay_particle_snapshot_t *out)
{
	if (!out || index >= s_ParticleCount) return 0;
	*out = s_Particles[index];
	return 1;
}

static s32 runtimeIsV1Graph(const char *effect_ref)
{
	const effect_graph_runtime_t *runtime =
		effectGraphRuntimeGetForGameplay(effect_ref);
	return runtime && (runtime->program.kind == EFFECT_GRAPH_PROGRAM_GRAPH ||
		runtime->program.kind == EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE);
}

static s32 requestCopyRooms(effect_instance_request_t *request,
	const RoomNum *rooms)
{
	s32 i;
	if (!request || !rooms) return 0;
	for (i = 0; i < EFFECT_INSTANCE_ROOM_CAP && rooms[i] >= 0; i++) {
		request->rooms[i] = rooms[i];
	}
	request->room_count = i;
	return i > 0;
}

s32 effectGameplayRuntimeTriggerPropExplosion(const char *effect_ref,
	struct prop *prop, s32 fallback_explosion_type)
{
	effect_instance_request_t request;
	u64 instance_id = 0;
	char err[256] = {0};
	s32 explosion_type;

	if (!prop || !prop->obj) return 0;
	if (!effect_ref || !effect_ref[0]) {
		return propExplode(prop, fallback_explosion_type) ? 1 : 0;
	}
	if (!runtimeIsV1Graph(effect_ref)) {
		explosion_type = effectGraphResolveExplosionType(effect_ref,
			fallback_explosion_type);
		return explosion_type != EFFECT_GRAPH_RESOLVE_FAILED &&
			propExplode(prop, explosion_type);
	}

	memset(&request, 0, sizeof(request));
	request.asset_id = effect_ref;
	{
		struct coord position;
		RoomNum rooms[EFFECT_INSTANCE_ROOM_CAP];
		if (!propResolveExplosionSpatial(prop, &position, rooms)) return 0;
		request.position.x = position.x;
		request.position.y = position.y;
		request.position.z = position.z;
		if (!requestCopyRooms(&request, rooms)) return 0;
	}
	request.source_position = request.position;
	request.target_position = request.position;
	request.source_prop = prop;
	request.target_prop = prop;
	request.playernum = (prop->obj->hidden & 0xf0000000) >> 28;
	request.action_mode = EFFECT_INSTANCE_ACTION_PROP_DETONATION;
	if (effectInstanceRuntimeSpawn(&request, &instance_id,
				err, sizeof(err)) != 0) {
		sysLogPrintf(LOG_WARNING,
			"EFFECT.GAMEPLAY: selected explosion '%s' suppressed: %s",
			effect_ref, err[0] ? err : "invalid spatial target");
		return 0;
	}
	return instance_id != 0;
}

s32 effectGameplayRuntimeTriggerSpark(const char *effect_ref,
	s32 fallback_spark_type, s32 room, struct prop *prop,
	const struct coord *position, const struct coord *primary_direction,
	const struct coord *secondary_direction)
{
	effect_instance_request_t request;
	u64 instance_id = 0;
	char err[256] = {0};
	s32 spark_type;

	if (!position || room < 0) return 0;
	if (!effect_ref || !effect_ref[0]) {
		sparksCreate(room, prop, (struct coord *)position,
			(struct coord *)primary_direction, (struct coord *)secondary_direction,
			fallback_spark_type);
		return 1;
	}
	if (!runtimeIsV1Graph(effect_ref)) {
		spark_type = effectGraphResolveSparkType(effect_ref, fallback_spark_type);
		if (spark_type == EFFECT_GRAPH_RESOLVE_FAILED) return 0;
		sparksCreate(room, prop, (struct coord *)position,
			(struct coord *)primary_direction, (struct coord *)secondary_direction,
			spark_type);
		return 1;
	}

	memset(&request, 0, sizeof(request));
	request.asset_id = effect_ref;
	request.position.x = position->x;
	request.position.y = position->y;
	request.position.z = position->z;
	request.source_position = request.position;
	request.target_position = request.position;
	request.source_prop = prop;
	request.target_prop = prop;
	if (primary_direction) {
		request.primary_direction.x = primary_direction->x;
		request.primary_direction.y = primary_direction->y;
		request.primary_direction.z = primary_direction->z;
	}
	if (secondary_direction) {
		request.secondary_direction.x = secondary_direction->x;
		request.secondary_direction.y = secondary_direction->y;
		request.secondary_direction.z = secondary_direction->z;
	}
	request.rooms[0] = (s16)room;
	request.room_count = 1;
	request.action_mode = EFFECT_INSTANCE_ACTION_POINT_EMITTER;
	if (effectInstanceRuntimeSpawn(&request, &instance_id, err, sizeof(err)) != 0) {
		sysLogPrintf(LOG_WARNING,
			"EFFECT.GAMEPLAY: selected spark '%s' suppressed: %s",
			effect_ref, err[0] ? err : "dispatch failed");
		return 0;
	}
	return instance_id != 0;
}
