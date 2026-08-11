/**
 * effect_graph_runtime.c -- .pdeffect compiler/runtime + OG-executor bridges.
 *
 * c3849 Unit 8 (full runtime behind the Unit 1b bridge seams). Mirrors the
 * weapon_graph_runtime slot-array/clear/register/accessor layout, scaled
 * down: effect records key by asset_id string only (no runtime_index slots),
 * with growable activation-owner sets. Gameplay reads go through the bridges; in
 * product builds the Wave 7 cutover keeps the shared runtime gate enabled,
 * while tests can still disable it for parity coverage.
 *
 * Closure rule: the executor IS the OG explosion/spark/smoke machinery. The
 * compiler maps class words to EXISTING table indices (and, for tinted
 * effect.spark nodes, appends a clone of the OG SPARKTYPE_PROJECTILE row to
 * the growable registry in src/game/sparks_custom.c). T-ASSETS-018 also owns
 * complete executable graph/timeline/profile retention. T-ASSETS-019 connects
 * v2 native-profile libraries; T-ASSETS-032 provides typed v1 dispatch, while
 * T-ASSETS-034/035/036 own the concrete consumer implementations.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* game/sparks.h must precede constants.h: it pulls <ultra64.h>, whose
 * os_libc.h prototypes collide with constants.h's empty osSyncPrintf macro
 * when the macro lands first (same order every src/game file uses). */
#include "game/sparks.h"

#include "constants.h"
#include "effect_executor.h"
#include "effect_graph_runtime.h"
#include "effect_instance_runtime.h"
#include "modarchive.h"
#include "pdeffect_source.h"
#include "system.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"

static effect_graph_runtime_t **s_effect_runtimes;
static size_t s_effect_runtime_count;
static size_t s_effect_runtime_cap;

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

/* ---------------------------------------------------------------------------
 * Record table (mirror of the weapon-graph slot arrays, string-keyed).
 * ------------------------------------------------------------------------- */

static effect_graph_runtime_t *effectRuntimeFindMutable(const char *asset_id)
{
	if (!asset_id || !asset_id[0]) return NULL;
	for (size_t i = 0; i < s_effect_runtime_count; i++) {
		if (s_effect_runtimes[i]->valid &&
				strcmp(s_effect_runtimes[i]->asset_id, asset_id) == 0) {
			return s_effect_runtimes[i];
		}
	}
	return NULL;
}

static void effectProgramFree(effect_graph_program_t *program)
{
	if (!program) return;
	free(program->contexts);
	free(program->nodes);
	free(program->edges);
	free(program->exports);
	free(program->subgraphs);
	free(program->params);
	free(program->execution_order);
	pdEffectTimelineFree(&program->timeline);
	pdEffectSourceFreeProfileLibrary(&program->profiles);
	memset(program, 0, sizeof(*program));
}

static void effectRuntimeFree(effect_graph_runtime_t *record)
{
	if (!record) return;
	effectProgramFree(&record->program);
	free(record->owners);
	free(record);
}

static s32 effectRuntimeOwnerIndex(const effect_graph_runtime_t *record,
	const char *owner_id)
{
	if (!record || !owner_id || !owner_id[0]) return -1;
	for (size_t i = 0; i < record->owner_count; i++) {
		if (strcmp(record->owners[i], owner_id) == 0) return (s32)i;
	}
	return -1;
}

static s32 effectRuntimeAddOwner(effect_graph_runtime_t *record,
	const char *owner_id, char *err, size_t err_cap)
{
	if (!record || !owner_id || !owner_id[0]) {
		setErr(err, err_cap, "effect runtime owner is missing");
		return -1;
	}
	if (effectRuntimeOwnerIndex(record, owner_id) >= 0) return 0;
	if (record->owner_count == record->owner_capacity) {
		size_t next = record->owner_capacity ? record->owner_capacity * 2 : 2;
		if (next < record->owner_count + 1 ||
				next > SIZE_MAX / sizeof(*record->owners)) {
			setErr(err, err_cap, "effect runtime owner table size overflow");
			return -1;
		}
		char (*grown)[CATALOG_ID_LEN] = (char (*)[CATALOG_ID_LEN])realloc(
			record->owners, next * sizeof(*record->owners));
		if (!grown) {
			setErr(err, err_cap, "out of memory growing effect owner table");
			return -1;
		}
		record->owners = grown;
		record->owner_capacity = next;
	}
	copyStr(record->owners[record->owner_count], CATALOG_ID_LEN, owner_id);
	record->owner_count++;
	return 0;
}

static s32 effectRuntimeCommit(effect_graph_runtime_t *record,
	const char *owner_id, char *err, size_t err_cap)
{
	for (size_t i = 0; i < s_effect_runtime_count; i++) {
		if (strcmp(s_effect_runtimes[i]->asset_id, record->asset_id) == 0) {
			effect_graph_runtime_t *existing = s_effect_runtimes[i];
			if (strcmp(existing->source_sha256, record->source_sha256) == 0) {
				if (effectRuntimeAddOwner(existing, owner_id, err, err_cap) != 0) {
					return -1;
				}
				effectRuntimeFree(record);
				return 0;
			}
			if (existing->owner_count != 1 ||
					effectRuntimeOwnerIndex(existing, owner_id) < 0) {
				setErr(err, err_cap,
					"effect %s source conflicts with another active owner",
					record->asset_id);
				return -1;
			}
			if (effectRuntimeAddOwner(record, owner_id, err, err_cap) != 0) {
				return -1;
			}
			if (!effectExecutorInstallProgram(record, err, err_cap)) {
				return -1;
			}
			if (existing->program.kind == EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY
					&& record->program.kind != EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY) {
				effectExecutorRemoveProgram(existing->asset_id);
			}
			/* Retained instances may hold this exact program pointer. Retire all
			 * committed consumer state before replacing public source. */
			effectInstanceRuntimeCancelAsset(existing->asset_id);
			effectRuntimeFree(s_effect_runtimes[i]);
			s_effect_runtimes[i] = record;
			return 0;
		}
	}
	if (effectRuntimeAddOwner(record, owner_id, err, err_cap) != 0) return -1;
	if (s_effect_runtime_count == s_effect_runtime_cap) {
		size_t next = s_effect_runtime_cap ? s_effect_runtime_cap * 2 : 32;
		if (next < s_effect_runtime_count + 1 ||
				next > SIZE_MAX / sizeof(*s_effect_runtimes)) {
			setErr(err, err_cap, "effect runtime registry size overflow");
			return -1;
		}
		effect_graph_runtime_t **grown = (effect_graph_runtime_t **)realloc(
			s_effect_runtimes, next * sizeof(*s_effect_runtimes));
		if (!grown) {
			setErr(err, err_cap, "out of memory growing effect runtime registry");
			return -1;
		}
		s_effect_runtimes = grown;
			s_effect_runtime_cap = next;
	}
	if (!effectExecutorInstallProgram(record, err, err_cap)) {
		return -1;
	}
	s_effect_runtimes[s_effect_runtime_count++] = record;
	return 0;
}

void effectGraphRuntimeReleaseOwner(const char *owner_id)
{
	if (!owner_id || !owner_id[0]) return;
	for (size_t i = s_effect_runtime_count; i-- > 0; ) {
		effect_graph_runtime_t *record = s_effect_runtimes[i];
		s32 owner_index = effectRuntimeOwnerIndex(record, owner_id);
		if (owner_index < 0) continue;
		if ((size_t)owner_index + 1 < record->owner_count) {
			memmove(&record->owners[owner_index], &record->owners[owner_index + 1],
				(record->owner_count - (size_t)owner_index - 1) *
					sizeof(*record->owners));
		}
		record->owner_count--;
		if (record->owner_count == 0) {
			effectInstanceRuntimeCancelAsset(record->asset_id);
			effectExecutorRemoveProgram(record->asset_id);
			effectRuntimeFree(record);
			if (i + 1 < s_effect_runtime_count) {
				memmove(&s_effect_runtimes[i], &s_effect_runtimes[i + 1],
					(s_effect_runtime_count - i - 1) * sizeof(*s_effect_runtimes));
			}
			s_effect_runtime_count--;
		}
	}
}

void effectGraphRuntimeClearAll(void)
{
	effectInstanceRuntimeClearAll();
	effectExecutorReset();
	for (size_t i = 0; i < s_effect_runtime_count; i++) {
		effectRuntimeFree(s_effect_runtimes[i]);
	}
	free(s_effect_runtimes);
	s_effect_runtimes = NULL;
	s_effect_runtime_count = 0;
	s_effect_runtime_cap = 0;
	/* Same reset path as the effect records (header contract): mod reloads
	 * that clear every record also release the custom spark rows. */
	sparksResetCustomTypes();
}

size_t effectGraphRuntimeCount(void)
{
	return s_effect_runtime_count;
}

s32 effectGraphProgramSample(const effect_graph_program_t *program,
	const char *property, f32 time, f32 *out_value)
{
	return program ? pdEffectTimelineSample(&program->timeline, property, time,
		out_value) : 0;
}

s32 effectGraphDispatchSlotForOpcode(weapon_graph_opcode_e opcode)
{
	switch (opcode) {
	case WEAPON_GRAPH_OP_EFFECT_TINT:      return EFFECT_GRAPH_DISPATCH_TINT;
	case WEAPON_GRAPH_OP_EFFECT_GLOW:      return EFFECT_GRAPH_DISPATCH_GLOW;
	case WEAPON_GRAPH_OP_EFFECT_SHIMMER:   return EFFECT_GRAPH_DISPATCH_SHIMMER;
	case WEAPON_GRAPH_OP_EFFECT_DARKEN:    return EFFECT_GRAPH_DISPATCH_DARKEN;
	case WEAPON_GRAPH_OP_EFFECT_SCREEN:    return EFFECT_GRAPH_DISPATCH_SCREEN;
	case WEAPON_GRAPH_OP_EFFECT_PARTICLE:  return EFFECT_GRAPH_DISPATCH_PARTICLE;
	case WEAPON_GRAPH_OP_EFFECT_EXPLOSION: return EFFECT_GRAPH_DISPATCH_EXPLOSION;
	case WEAPON_GRAPH_OP_EFFECT_SPARK:     return EFFECT_GRAPH_DISPATCH_SPARK;
	case WEAPON_GRAPH_OP_EFFECT_SMOKE:     return EFFECT_GRAPH_DISPATCH_SMOKE;
	default:                               return -1;
	}
}

static s32 effectProgramValidateSchedule(const effect_graph_program_t *program,
	char *err, size_t err_cap)
{
	s32 *position;

	if (!program || program->node_count <= 0 || !program->nodes ||
			program->execution_count != program->node_count ||
			!program->execution_order) {
		setErr(err, err_cap, "effect graph has no complete execution schedule");
		return -1;
	}
	if (program->edge_count < 0 ||
			(program->edge_count > 0 && !program->edges)) {
		setErr(err, err_cap, "effect graph has an invalid edge table");
		return -1;
	}
	position = (s32 *)malloc((size_t)program->node_count * sizeof(*position));
	if (!position) {
		setErr(err, err_cap, "out of memory validating effect graph schedule");
		return -1;
	}
	for (s32 i = 0; i < program->node_count; i++) position[i] = -1;
	for (s32 i = 0; i < program->execution_count; i++) {
		s32 node = program->execution_order[i];
		if (node < 0 || node >= program->node_count || position[node] >= 0) {
			free(position);
			setErr(err, err_cap,
				"effect graph execution schedule has an invalid or duplicate node");
			return -1;
		}
		if (effectGraphDispatchSlotForOpcode(program->nodes[node].opcode) < 0) {
			free(position);
			setErr(err, err_cap, "effect node %s has no production dispatch slot",
				program->nodes[node].kind);
			return -1;
		}
		position[node] = i;
	}
	for (s32 i = 0; i < program->edge_count; i++) {
		s32 from = program->edges[i].from;
		s32 to = program->edges[i].to;
		if (from < 0 || from >= program->node_count ||
				to < 0 || to >= program->node_count ||
				position[from] >= position[to]) {
			free(position);
			setErr(err, err_cap,
				"effect graph execution schedule violates dependency order");
			return -1;
		}
	}
	free(position);
	return 0;
}

s32 effectGraphRuntimeDispatch(const char *asset_id, f32 time,
	const effect_graph_dispatch_table_t *dispatch, void *user,
	char *err, size_t err_cap)
{
	const effect_graph_runtime_t *runtime;
	const effect_graph_program_t *program;
	effect_graph_dispatch_context_t context;

	if (err && err_cap) err[0] = '\0';
	if (!asset_id || !asset_id[0]) {
		setErr(err, err_cap, "effect dispatch requires a catalog ID");
		return -1;
	}
	runtime = effectGraphRuntimeGetForGameplay(asset_id);
	if (!runtime) {
		setErr(err, err_cap, "effect %s has no active public runtime", asset_id);
		return -1;
	}
	program = &runtime->program;
	if (program->kind != EFFECT_GRAPH_PROGRAM_GRAPH &&
			program->kind != EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE) {
		setErr(err, err_cap, "effect %s is not a v1 graph program", asset_id);
		return -1;
	}
	if (effectProgramValidateSchedule(program, err, err_cap) != 0) return -1;
	if (!dispatch || !dispatch->begin || !dispatch->commit ||
			!dispatch->rollback) {
		setErr(err, err_cap, "effect dispatch transaction phases are incomplete");
		return -1;
	}
	/* Preflight the complete handler set before begin. A mixed graph cannot
	 * partially execute merely because its first few kinds have consumers. */
	for (s32 i = 0; i < program->execution_count; i++) {
		s32 node_index = program->execution_order[i];
		s32 slot = effectGraphDispatchSlotForOpcode(
			program->nodes[node_index].opcode);
		if (slot < 0 || slot >= EFFECT_GRAPH_DISPATCH_SLOT_COUNT ||
				!dispatch->nodes[slot]) {
			setErr(err, err_cap, "effect node kind %s has no installed handler",
				program->nodes[node_index].kind);
			return -1;
		}
	}

	context.runtime = runtime;
	context.time = time;
	context.user = user;
	if (dispatch->begin(&context) != 0) {
		dispatch->rollback(&context, 0);
		setErr(err, err_cap, "effect %s dispatch begin failed", asset_id);
		return -1;
	}
	for (s32 i = 0; i < program->execution_count; i++) {
		s32 node_index = program->execution_order[i];
		s32 slot = effectGraphDispatchSlotForOpcode(
			program->nodes[node_index].opcode);
		if (dispatch->nodes[slot](&context, &program->nodes[node_index], i) != 0) {
			dispatch->rollback(&context, i + 1);
			setErr(err, err_cap, "effect node %s dispatch failed",
				program->nodes[node_index].id);
			return -1;
		}
	}
	if (dispatch->commit(&context) != 0) {
		dispatch->rollback(&context, program->execution_count);
		setErr(err, err_cap, "effect %s dispatch commit failed", asset_id);
		return -1;
	}
	return program->execution_count;
}

const effect_graph_runtime_t *effectGraphRuntimeGet(const char *asset_id)
{
	return effectRuntimeFindMutable(asset_id);
}

const effect_graph_runtime_t *effectGraphRuntimeGetForGameplay(
	const char *asset_id)
{
	if (!weaponGraphRuntimeEnabled()) return NULL;
	return effectGraphRuntimeGet(asset_id);
}

/* ---------------------------------------------------------------------------
 * IR param access + vocabulary maps.
 * ------------------------------------------------------------------------- */

static const weapon_graph_ir_param_t *effectParam(
	const weapon_graph_ir_t *ir,
	const weapon_graph_ir_node_t *node,
	const char *key)
{
	if (!ir || !node || !key) return NULL;
	for (s32 i = 0; i < node->param_count; i++) {
		const weapon_graph_ir_param_t *p = &ir->params[node->param_start + i];
		if (strcmp(p->key, key) == 0) return p;
	}
	return NULL;
}

static s32 effectParamString(const weapon_graph_ir_t *ir,
                             const weapon_graph_ir_node_t *node,
                             const char *key, char *out, size_t out_cap)
{
	const weapon_graph_ir_param_t *p = effectParam(ir, node, key);
	if (!p || p->type != WEAPON_GRAPH_PARAM_STRING || !out || out_cap == 0) {
		return 0;
	}
	copyStr(out, out_cap, p->value);
	return 1;
}

static s32 effectParamFloat(const weapon_graph_ir_t *ir,
                            const weapon_graph_ir_node_t *node,
                            const char *key, f32 *out)
{
	const weapon_graph_ir_param_t *p = effectParam(ir, node, key);
	if (!p || !out) return 0;
	if (p->type == WEAPON_GRAPH_PARAM_FLOAT) {
		*out = p->f_value;
		return 1;
	}
	if (p->type == WEAPON_GRAPH_PARAM_INT) {
		*out = (f32)p->i_value;
		return 1;
	}
	return 0;
}

static s32 effectParamInt(const weapon_graph_ir_t *ir,
                          const weapon_graph_ir_node_t *node,
                          const char *key, s32 *out)
{
	const weapon_graph_ir_param_t *p = effectParam(ir, node, key);
	if (!p || !out) return 0;
	if (p->type == WEAPON_GRAPH_PARAM_INT) {
		*out = p->i_value;
		return 1;
	}
	if (p->type == WEAPON_GRAPH_PARAM_FLOAT) {
		*out = (s32)p->f_value;
		return 1;
	}
	return 0;
}

/* Parse a tint ARRAY param ("[r, g, b, a]", components authored 0..1) into
 * up to 4 floats. Returns the component count (0 when not an array / not
 * numeric). The IR stores the raw bracketed span, possibly multi-line. */
static s32 effectParamFloats(const weapon_graph_ir_t *ir,
                             const weapon_graph_ir_node_t *node,
                             const char *key, f32 *out, s32 max_out)
{
	const weapon_graph_ir_param_t *p = effectParam(ir, node, key);
	const char *s;
	s32 count = 0;
	if (!p || p->type != WEAPON_GRAPH_PARAM_ARRAY || !out || max_out <= 0) {
		return 0;
	}
	s = p->value;
	while (*s && count < max_out) {
		char *end = NULL;
		double d;
		while (*s && (isspace((unsigned char)*s) || *s == '[' ||
				*s == ']' || *s == ',')) {
			s++;
		}
		if (!*s) break;
		d = strtod(s, &end);
		if (end == s) break;
		out[count++] = (f32)d;
		s = end;
	}
	return count;
}

static u8 effectColorByte(f32 component)
{
	if (component < 0.0f) component = 0.0f;
	if (component > 1.0f) component = 1.0f;
	return (u8)(component * 255.0f + 0.5f);
}

/* Tint floats -> the sparktype unk1c/unk20 colour word layout. sparks.c
 * authors those words as 0xRRGGBBAA (e.g. SPARKTYPE_PROJECTILE 0xffff80ff
 * = warm white, full alpha) and the renderer byte-swaps with PD_BE32 at the
 * vertex-colour write (sparks.c sparksRender), so the compile-time format
 * here must stay RR GG BB AA from high byte to low. */
static u32 effectColorWord(const f32 *rgba, s32 count)
{
	f32 a = count >= 4 ? rgba[3] : 1.0f;
	return ((u32)effectColorByte(rgba[0]) << 24) |
		((u32)effectColorByte(rgba[1]) << 16) |
		((u32)effectColorByte(rgba[2]) << 8) |
		(u32)effectColorByte(a);
}

/* B2 vocabulary: the class words are legal ONLY inside .pdeffect graph
 * bodies (never in *_explosion_ref fields, which speak the base: token
 * vocabulary of weaponGraphResolveExplosionRef). Values mirror the
 * EXPLOSIONTYPE_* constants (constants.h:919-940). */
static s32 effectExplosionClassToType(const char *cls)
{
	if (!cls || !cls[0]) return -1;
	if (strcmp(cls, "tiny") == 0)    return EXPLOSIONTYPE_6;       /* 6 */
	if (strcmp(cls, "small") == 0)   return EXPLOSIONTYPE_EYESPY;  /* 2 */
	if (strcmp(cls, "medium") == 0)  return EXPLOSIONTYPE_11;      /* 11 */
	if (strcmp(cls, "large") == 0)   return EXPLOSIONTYPE_ROCKET;  /* 13 */
	if (strcmp(cls, "huge") == 0)    return EXPLOSIONTYPE_HUGE17;  /* 17 */
	if (strcmp(cls, "massive") == 0) return EXPLOSIONTYPE_HUGE25;  /* 25 */
	return -1;
}

/* effect.smoke class -> NEAREST existing SMOKETYPE_* row (slice 1; custom
 * smoke rows are deferred -- 28 direct index sites). Documented mapping:
 *   none           -> SMOKETYPE_NONE (0)
 *   electrical     -> SMOKETYPE_ELECTRICAL (1)
 *   tiny | mini    -> SMOKETYPE_MINI (2)        (smallest OG row)
 *   small          -> SMOKETYPE_SMALL (4)
 *   medium         -> SMOKETYPE_MEDIUM (5)
 *   large | huge | massive -> SMOKETYPE_LARGE (6) (largest OG row; nearest
 *                                                  for every bigger class)
 *   bullet_impact  -> SMOKETYPE_BULLETIMPACT (7)
 *   rocket_tail    -> SMOKETYPE_ROCKETTAIL (8)
 *   grenade_tail   -> SMOKETYPE_GRENADETAIL (9)
 *   homing_tail    -> SMOKETYPE_HOMINGTAIL (11)
 *   pinball        -> SMOKETYPE_PINBALL (19)
 *   water          -> SMOKETYPE_WATER (20)
 *   debris         -> SMOKETYPE_DEBRIS (21)
 * Unknown words return -1 (bridge falls back to the OG smoke type). */
static s32 effectSmokeClassToType(const char *cls)
{
	if (!cls || !cls[0]) return -1;
	if (strcmp(cls, "none") == 0)          return SMOKETYPE_NONE;
	if (strcmp(cls, "electrical") == 0)    return SMOKETYPE_ELECTRICAL;
	if (strcmp(cls, "tiny") == 0)          return SMOKETYPE_MINI;
	if (strcmp(cls, "mini") == 0)          return SMOKETYPE_MINI;
	if (strcmp(cls, "small") == 0)         return SMOKETYPE_SMALL;
	if (strcmp(cls, "medium") == 0)        return SMOKETYPE_MEDIUM;
	if (strcmp(cls, "large") == 0)         return SMOKETYPE_LARGE;
	if (strcmp(cls, "huge") == 0)          return SMOKETYPE_LARGE;
	if (strcmp(cls, "massive") == 0)       return SMOKETYPE_LARGE;
	if (strcmp(cls, "bullet_impact") == 0) return SMOKETYPE_BULLETIMPACT;
	if (strcmp(cls, "rocket_tail") == 0)   return SMOKETYPE_ROCKETTAIL;
	if (strcmp(cls, "grenade_tail") == 0)  return SMOKETYPE_GRENADETAIL;
	if (strcmp(cls, "homing_tail") == 0)   return SMOKETYPE_HOMINGTAIL;
	if (strcmp(cls, "pinball") == 0)       return SMOKETYPE_PINBALL;
	if (strcmp(cls, "water") == 0)         return SMOKETYPE_WATER;
	if (strcmp(cls, "debris") == 0)        return SMOKETYPE_DEBRIS;
	return -1;
}

/* ---------------------------------------------------------------------------
 * IR -> record.
 * ------------------------------------------------------------------------- */

static void effectRuntimeFromNode(const weapon_graph_ir_t *ir,
                                  const weapon_graph_ir_node_t *node,
                                  effect_graph_runtime_t *record)
{
	f32 tint[4];
	s32 tint_count;
	f32 fvalue;
	s32 ivalue;

	/* Optional intensity/sound params are honored on ANY effect node (the
	 * base emitter authors intensity on its single apply node). */
	if (effectParamFloat(ir, node, "intensity", &fvalue)) {
		record->has_intensity = 1;
		record->intensity = fvalue;
	}
	if (effectParamInt(ir, node, "sound", &ivalue) ||
			effectParamInt(ir, node, "soundnum", &ivalue)) {
		record->has_sound = 1;
		record->soundnum = ivalue;
	}

	switch (node->opcode) {
	case WEAPON_GRAPH_OP_EFFECT_EXPLOSION:
		record->has_explosion = 1;
		effectParamString(ir, node, "explosion_class",
			record->explosion_class, sizeof(record->explosion_class));
		record->explosion_type =
			(s16)effectExplosionClassToType(record->explosion_class);
		if (record->explosion_class[0] && record->explosion_type < 0) {
			sysLogPrintf(LOG_WARNING,
				"EFFECTGRAPH.PARSE: '%s' unknown explosion_class '%s'; selected channel is unavailable",
				ir->asset_id, record->explosion_class);
		}
		tint_count = effectParamFloats(ir, node, "tint", tint, 4);
		if (tint_count >= 3) {
			record->explosion_tint[0] = tint[0];
			record->explosion_tint[1] = tint[1];
			record->explosion_tint[2] = tint[2];
			record->explosion_tint[3] = tint_count >= 4 ? tint[3] : 1.0f;
		}
		break;

	case WEAPON_GRAPH_OP_EFFECT_SPARK:
		record->has_spark = 1;
		record->spark_type = -1;
		tint_count = effectParamFloats(ir, node, "tint", tint, 4);
		if (tint_count >= 3) {
			f32 tint2[4];
			s32 tint2_count;
			record->spark_color1 = effectColorWord(tint, tint_count);
			/* Optional secondary colour; the OG PROJECTILE row fades to
			 * plain white, so that stays the default. */
			tint2_count = effectParamFloats(ir, node, "tint2", tint2, 4);
			if (tint2_count < 3) {
				tint2_count = effectParamFloats(ir, node, "tint_secondary",
					tint2, 4);
			}
			record->spark_color2 = tint2_count >= 3
				? effectColorWord(tint2, tint2_count) : 0xffffffffu;
			/* Allocate the custom row NOW (registration time), store the
			 * returned index. -1 on exhaustion is loud
			 * (SPARK.CUSTOM_ROW_FAIL) and leaves the bridge on its OG
			 * fallback. */
			record->spark_type = sparksRegisterCustomTintedType(
				record->spark_color1, record->spark_color2);
		}
		break;

	case WEAPON_GRAPH_OP_EFFECT_SMOKE: {
		char cls[32];
		record->has_smoke = 1;
		record->smoke_type = -1;
		cls[0] = '\0';
		if (!effectParamString(ir, node, "smoke_class", cls, sizeof(cls))) {
			effectParamString(ir, node, "class", cls, sizeof(cls));
		}
		if (cls[0]) {
			record->smoke_type = effectSmokeClassToType(cls);
			if (record->smoke_type < 0) {
				sysLogPrintf(LOG_WARNING,
					"EFFECTGRAPH.PARSE: '%s' unknown smoke class '%s'; selected channel is unavailable",
					ir->asset_id, cls);
			}
		} else if (effectParamInt(ir, node, "smoke_type", &ivalue)) {
			/* Direct SMOKETYPE_* index passthrough, bounds-checked against
			 * the OG table (SMOKETYPE_NONE..SMOKETYPE_UFO). */
			if (ivalue >= SMOKETYPE_NONE && ivalue <= SMOKETYPE_UFO) {
				record->smoke_type = ivalue;
			} else {
				sysLogPrintf(LOG_WARNING,
					"EFFECTGRAPH.PARSE: '%s' smoke_type %d outside the OG table; selected channel is unavailable",
					ir->asset_id, ivalue);
			}
		}
		break;
	}

	default:
		/* effect.tint/glow/shimmer/darken/screen/particle retain their full
		 * typed parameters here. The T-ASSETS-032 scheduler routes them through
		 * mandatory dispatch slots; T-ASSETS-035 owns presentation behavior. */
		break;
	}
}

static void *effectArrayCopy(const void *source, size_t count, size_t stride)
{
	if (count == 0) return NULL;
	if (!source || stride == 0 || count > (size_t)-1 / stride) return NULL;
	void *copy = malloc(count * stride);
	if (copy) memcpy(copy, source, count * stride);
	return copy;
}

static s32 effectProgramCopyIr(effect_graph_program_t *program,
	const weapon_graph_ir_t *ir, char *err, size_t err_cap)
{
	program->context_count = ir->context_count;
	program->node_count = ir->node_count;
	program->edge_count = ir->edge_count;
	program->export_count = ir->export_count;
	program->subgraph_count = ir->subgraph_count;
	program->param_count = ir->param_count;
	program->execution_count = ir->node_count;
	program->contexts = (weapon_graph_ir_context_t *)effectArrayCopy(ir->contexts,
		(size_t)ir->context_count, sizeof(ir->contexts[0]));
	program->nodes = (weapon_graph_ir_node_t *)effectArrayCopy(ir->nodes,
		(size_t)ir->node_count, sizeof(ir->nodes[0]));
	program->edges = (weapon_graph_ir_edge_t *)effectArrayCopy(ir->edges,
		(size_t)ir->edge_count, sizeof(ir->edges[0]));
	program->exports = (weapon_graph_ir_export_t *)effectArrayCopy(ir->exports,
		(size_t)ir->export_count, sizeof(ir->exports[0]));
	program->subgraphs = (weapon_graph_ir_subgraph_t *)effectArrayCopy(ir->subgraphs,
		(size_t)ir->subgraph_count, sizeof(ir->subgraphs[0]));
	program->params = (weapon_graph_ir_param_t *)effectArrayCopy(ir->params,
		(size_t)ir->param_count, sizeof(ir->params[0]));
	program->execution_order = (s32 *)calloc((size_t)ir->node_count, sizeof(s32));
	if ((ir->context_count && !program->contexts) ||
			(ir->node_count && (!program->nodes || !program->execution_order)) ||
			(ir->edge_count && !program->edges) ||
			(ir->export_count && !program->exports) ||
			(ir->subgraph_count && !program->subgraphs) ||
			(ir->param_count && !program->params)) {
		setErr(err, err_cap, "out of memory retaining effect graph program");
		return -1;
	}

	/* Stable Kahn order: each pass selects the first authored node whose
	 * predecessors are already scheduled. weaponGraphCompileJson has already
	 * rejected cycles, so a stalled pass is corruption. */
	u8 *scheduled = (u8 *)calloc((size_t)ir->node_count, 1);
	if (ir->node_count && !scheduled) {
		setErr(err, err_cap, "out of memory ordering effect graph program");
		return -1;
	}
	for (s32 out = 0; out < ir->node_count; out++) {
		s32 chosen = -1;
		for (s32 node = 0; node < ir->node_count && chosen < 0; node++) {
			if (scheduled[node]) continue;
			s32 ready = 1;
			for (s32 edge = 0; edge < ir->edge_count; edge++) {
				if (ir->edges[edge].to == node && !scheduled[ir->edges[edge].from]) {
					ready = 0;
					break;
				}
			}
			if (ready) chosen = node;
		}
		if (chosen < 0) {
			free(scheduled);
			setErr(err, err_cap, "effect graph execution order is cyclic");
			return -1;
		}
		scheduled[chosen] = 1;
		program->execution_order[out] = chosen;
	}
	free(scheduled);
	/* Activation is allowed only when every accepted opcode maps to the
	 * permanent production dispatch contract and the retained schedule is a
	 * complete dependency-respecting permutation. */
	return effectProgramValidateSchedule(program, err, err_cap);
}

static effect_graph_runtime_t *effectRuntimeCreateIr(const weapon_graph_ir_t *ir,
	const char *timeline, u32 timeline_size, const char *descriptor_target,
	const char *descriptor_shader, char *err, size_t err_cap)
{
	if (!ir || ir->asset_type != ASSET_EFFECT || !ir->asset_id[0]) {
		setErr(err, err_cap,
			"effect IR registration requires an effect graph with asset_id");
		return NULL;
	}

	effect_graph_runtime_t *record = (effect_graph_runtime_t *)calloc(1,
		sizeof(*record));
	if (!record) { setErr(err, err_cap, "out of memory creating effect program"); return NULL; }

	record->valid = 1;
	copyStr(record->asset_id, sizeof(record->asset_id), ir->asset_id);
	copyStr(record->source_sha256, sizeof(record->source_sha256),
		ir->source_sha256);
	copyStr(record->ir_sha256, sizeof(record->ir_sha256), ir->ir_sha256);
	record->explosion_type = -1;
	record->spark_type = -1;
	record->smoke_type = -1;
	if (effectProgramCopyIr(&record->program, ir, err, err_cap) != 0) {
		effectRuntimeFree(record);
		return NULL;
	}
	if (effectInstanceProgramValidate(&record->program, descriptor_target,
			descriptor_shader,
			err, err_cap) != 0) {
		effectRuntimeFree(record);
		return NULL;
	}
	if (timeline && timeline_size) {
		if (!pdEffectTimelineParse(timeline, timeline_size,
				&record->program.timeline, err, err_cap)) {
			effectRuntimeFree(record);
			return NULL;
		}
		record->program.kind = EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE;
	} else {
		record->program.kind = EFFECT_GRAPH_PROGRAM_GRAPH;
	}

	for (s32 i = 0; i < ir->node_count; i++) {
		effectRuntimeFromNode(ir, &ir->nodes[i], record);
	}

	return record;
}

/* ---------------------------------------------------------------------------
 * Registration entry points.
 * ------------------------------------------------------------------------- */

static effect_graph_runtime_t *effectRuntimeCreateProfile(
	const pd_effect_source_info_t *source, const char *graph, u32 graph_size,
	char *err, size_t err_cap)
{
	effect_graph_runtime_t *record = (effect_graph_runtime_t *)calloc(1,
		sizeof(*record));
	if (!record) { setErr(err, err_cap, "out of memory creating profile program"); return NULL; }
	record->valid = 1;
	record->explosion_type = record->spark_type = record->smoke_type = -1;
	copyStr(record->asset_id, sizeof(record->asset_id), source->catalog_id);
	if (!pdEffectSourceDecodeProfileLibrary(graph, graph_size, source,
			&record->program.profiles, err, err_cap)) {
		effectRuntimeFree(record);
		return NULL;
	}
	record->program.kind = EFFECT_GRAPH_PROGRAM_PROFILE_LIBRARY;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Hash(graph, graph_size, digest);
	sha256ToHex(digest, record->source_sha256);
	copyStr(record->ir_sha256, sizeof(record->ir_sha256), record->source_sha256);
	return record;
}

static s32 effectRuntimeSetActivationDigest(effect_graph_runtime_t *record,
	const pd_effect_source_info_t *source, const char *graph, u32 graph_size,
	const char *timeline, u32 timeline_size, char *err, size_t err_cap)
{
	if (!record || !source) return -1;
	u8 graph_digest[SHA256_DIGEST_SIZE] = {0};
	u8 timeline_digest[SHA256_DIGEST_SIZE];
	char descriptor[1400];
	s32 descriptor_len;
	u8 combined_digest[SHA256_DIGEST_SIZE];
	u8 *identity;
	size_t identity_size;

	memset(timeline_digest, 0, sizeof(timeline_digest));
	if (graph && graph_size) sha256Hash(graph, graph_size, graph_digest);
	if (timeline && timeline_size) {
		sha256Hash(timeline, timeline_size, timeline_digest);
	}
	descriptor_len = snprintf(descriptor, sizeof(descriptor),
		"format=%d\nprofile_kind=%d\ncatalog_id=%s\nname=%s\n"
		"effect_file=%s\ntimeline_file=%s\neffect_key=%s\ntarget_key=%s\n"
		"shader_id=%s\nintensity=%.9g\nprofile_count=%zu\n"
		"audio_rows=%zu\nsilent_audio_rows=%zu\n",
		(s32)source->format, (s32)source->profile_kind, source->catalog_id,
		source->name, source->effect_file, source->timeline_file,
		source->effect_key, source->target_key, source->shader_id,
		(double)source->intensity, source->profile_count,
		source->has_audio_rows, source->silent_audio_rows);
	if (descriptor_len < 0 || (size_t)descriptor_len >= sizeof(descriptor)) {
		setErr(err, err_cap, "effect source identity exceeds canonical envelope");
		return -1;
	}
	identity_size = (size_t)descriptor_len + sizeof(graph_digest) +
		sizeof(timeline_digest);
	identity = (u8 *)malloc(identity_size);
	if (!identity) {
		setErr(err, err_cap, "out of memory hashing effect source identity");
		return -1;
	}
	memcpy(identity, descriptor, (size_t)descriptor_len);
	memcpy(identity + descriptor_len, graph_digest, sizeof(graph_digest));
	memcpy(identity + descriptor_len + sizeof(graph_digest), timeline_digest,
		sizeof(timeline_digest));
	sha256Hash(identity, identity_size, combined_digest);
	free(identity);
	sha256ToHex(combined_digest, record->source_sha256);
	return 0;
}

static s32 effectRuntimeRegisterPublicSource(
	const pd_effect_source_info_t *source, const char *graph, u32 graph_size,
	const char *timeline, u32 timeline_size, const char *owner_id,
	char *err, size_t err_cap)
{
	effect_graph_runtime_t *record = NULL;
	if (source->format == PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY) {
		record = effectRuntimeCreateProfile(source, graph, graph_size, err, err_cap);
	} else if (graph && graph_size) {
		weapon_graph_ir_t ir;
		if (weaponGraphCompileJson(ASSET_EFFECT, graph, graph_size, &ir,
				err, err_cap) != 0) return -1;
		if (ir.asset_id[0] && strcmp(ir.asset_id, source->catalog_id) != 0) {
			setErr(err, err_cap, "effect graph asset_id %s does not match catalog %s",
				ir.asset_id, source->catalog_id);
			weaponGraphIrFree(&ir);
			return -1;
		}
		if (!ir.asset_id[0]) copyStr(ir.asset_id, sizeof(ir.asset_id),
			source->catalog_id);
		record = effectRuntimeCreateIr(&ir, timeline, timeline_size,
			source->target_key, source->shader_id, err, err_cap);
		weaponGraphIrFree(&ir);
	} else if (timeline && timeline_size) {
		/* A v1 timeline has values but no executable target without a graph.
		 * Accepting it created a catalog-visible program that production could
		 * never dispatch. Public source must fail closed instead. */
		setErr(err, err_cap,
			"v1 effect timeline requires an effect graph target");
		return -1;
	} else {
		setErr(err, err_cap, "effect source has neither graph nor timeline");
		return -1;
	}
	if (!record) return -1;
	copyStr(record->effect_key, sizeof(record->effect_key), source->effect_key);
	copyStr(record->target_key, sizeof(record->target_key), source->target_key);
	copyStr(record->shader_id, sizeof(record->shader_id), source->shader_id);
	record->descriptor_intensity = source->intensity;
	if (record->program.kind == EFFECT_GRAPH_PROGRAM_GRAPH ||
			record->program.kind == EFFECT_GRAPH_PROGRAM_GRAPH_TIMELINE) {
		if (effectInstanceProgramValidate(&record->program, record->target_key,
				record->shader_id, err, err_cap) != 0) {
			effectRuntimeFree(record);
			return -1;
		}
	}
	/* Owner collision/replacement uses the complete public-source identity,
	 * including normalized descriptor semantics and both authored members. */
	if (effectRuntimeSetActivationDigest(record, source, graph, graph_size,
			timeline, timeline_size, err, err_cap) != 0) {
		effectRuntimeFree(record);
		return -1;
	}
	if (effectRuntimeCommit(record, owner_id, err, err_cap) != 0) {
		effectRuntimeFree(record);
		return -1;
	}
	return 0;
}

s32 effectGraphRuntimeRegisterGraphJson(const char *asset_id,
                                        const char *json,
                                        u32 json_size,
                                        char *err, size_t err_cap)
{
	weapon_graph_ir_t ir;
	if (weaponGraphCompileJson(ASSET_EFFECT, json, json_size, &ir,
			err, err_cap) != 0) {
		return -1;
	}
	if (asset_id && asset_id[0]) {
		if (ir.asset_id[0] && strcmp(ir.asset_id, asset_id) != 0) {
			setErr(err, err_cap,
				"effect graph asset_id %s does not match catalog %s",
				ir.asset_id, asset_id);
			weaponGraphIrFree(&ir);
			return -1;
		}
		if (!ir.asset_id[0]) {
			copyStr(ir.asset_id, sizeof(ir.asset_id), asset_id);
		}
	}
	effect_graph_runtime_t *record = effectRuntimeCreateIr(&ir, NULL, 0,
		NULL, NULL, err, err_cap);
	weaponGraphIrFree(&ir);
	if (!record) return -1;
	if (effectRuntimeCommit(record, record->asset_id, err, err_cap) != 0) {
		effectRuntimeFree(record);
		return -1;
	}
	return 0;
}

static s32 effectRuntimeRegisterArchiveBytesOwned(const void *archive_bytes,
	u32 archive_size, const char *owner_id, char *err, size_t err_cap)
{
	pd_effect_source_info_t public_source;
	void *graph = NULL;
	void *timeline = NULL;
	u32 graph_size = 0;
	u32 timeline_size = 0;
	s32 result;

	if (!archive_bytes || archive_size == 0) {
		setErr(err, err_cap, "effect archive bytes register called with null input");
		return -1;
	}
	if (!pdEffectSourceParseArchiveBytes(archive_bytes, archive_size, NULL,
			&public_source, err, err_cap)) {
		return -1;
	}
	if (public_source.effect_file[0]) graph = modArchiveExtractMemAlloc(
		archive_bytes, archive_size, public_source.effect_file, &graph_size);
	if (public_source.timeline_file[0]) timeline = modArchiveExtractMemAlloc(
		archive_bytes, archive_size, public_source.timeline_file, &timeline_size);
	if ((public_source.effect_file[0] && (!graph || !graph_size)) ||
			(public_source.timeline_file[0] && (!timeline || !timeline_size))) {
		setErr(err, err_cap, "effect archive is missing a declared program member");
		free(graph); free(timeline); return -1;
	}
	result = effectRuntimeRegisterPublicSource(&public_source,
		(const char *)graph, graph_size, (const char *)timeline, timeline_size,
		owner_id && owner_id[0] ? owner_id : public_source.catalog_id,
		err, err_cap);
	free(graph);
	free(timeline);
	return result;
}

s32 effectGraphRuntimeRegisterArchiveBytesOwned(const void *archive_bytes,
	u32 archive_size, const char *owner_id, char *err, size_t err_cap)
{
	return effectRuntimeRegisterArchiveBytesOwned(archive_bytes, archive_size,
		owner_id, err, err_cap);
}

s32 effectGraphRuntimeRegisterArchiveBytes(const void *archive_bytes,
	u32 archive_size, char *err, size_t err_cap)
{
	return effectRuntimeRegisterArchiveBytesOwned(archive_bytes, archive_size,
		NULL, err, err_cap);
}

static s32 effectRuntimeRegisterArchiveOwned(const char *archive_path,
	const char *owner_id, char *err, size_t err_cap)
{
	pd_effect_source_info_t public_source;
	char *graph = NULL;
	char *timeline = NULL;
	u32 graph_size = 0;
	u32 timeline_size = 0;
	s32 result;

	if (!archive_path || !archive_path[0]) {
		setErr(err, err_cap, "effect archive register called with null input");
		return -1;
	}
	if (!pdEffectSourceParseArchiveFile(archive_path, NULL, &public_source,
			err, err_cap)) {
		return -1;
	}
	if (public_source.effect_file[0] && weaponGraphArchiveReadTextFile(archive_path,
			public_source.effect_file, &graph, &graph_size) != 0) {
		setErr(err, err_cap, "%s missing graph entry %s", archive_path,
			public_source.effect_file); return -1;
	}
	if (public_source.timeline_file[0] && weaponGraphArchiveReadTextFile(archive_path,
			public_source.timeline_file, &timeline, &timeline_size) != 0) {
		free(graph); setErr(err, err_cap, "%s missing timeline entry %s",
			archive_path, public_source.timeline_file); return -1;
	}
	result = effectRuntimeRegisterPublicSource(&public_source, graph, graph_size,
		timeline, timeline_size,
		owner_id && owner_id[0] ? owner_id : public_source.catalog_id,
		err, err_cap);
	free(graph);
	free(timeline);
	return result;
}

s32 effectGraphRuntimeRegisterArchiveOwned(const char *archive_path,
	const char *owner_id, char *err, size_t err_cap)
{
	return effectRuntimeRegisterArchiveOwned(archive_path, owner_id, err, err_cap);
}

s32 effectGraphRuntimeRegisterArchive(const char *archive_path,
	char *err, size_t err_cap)
{
	return effectRuntimeRegisterArchiveOwned(archive_path, NULL, err, err_cap);
}

/* ---------------------------------------------------------------------------
 * OG-executor bridges (Unit 1b signatures, frozen).
 * ------------------------------------------------------------------------- */

s32 effectGraphResolveExplosionType(const char *effect_ref, s32 fallback_exptype)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_exptype;
	}
	/* A disabled runtime cannot satisfy an authored selection. Product keeps
	 * this gate enabled; tests may disable it, but selected refs still fail
	 * closed rather than reviving OG behavior. */
	if (!weaponGraphRuntimeEnabled()) {
		return EFFECT_GRAPH_RESOLVE_FAILED;
	}

	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_explosion && record->explosion_type >= 0) {
		return record->explosion_type;
	}
	{
		s32 profile = effectExecutorResolveExplosionProfile(effect_ref);
		if (profile >= 0) return profile;
	}

	return EFFECT_GRAPH_RESOLVE_FAILED;
}

s32 effectGraphResolveSparkType(const char *effect_ref, s32 fallback_sparktype)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_sparktype;
	}
	if (!weaponGraphRuntimeEnabled()) {
		return EFFECT_GRAPH_RESOLVE_FAILED;
	}

	/* spark_type is the custom registry row index for tinted sparks. An
	 * unallocated selected channel remains unavailable. */
	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_spark && record->spark_type >= 0) {
		return record->spark_type;
	}
	{
		s32 profile = effectExecutorResolveSparkProfile(effect_ref);
		if (profile >= 0) return profile;
	}

	return EFFECT_GRAPH_RESOLVE_FAILED;
}

s32 effectGraphResolveSmokeType(const char *effect_ref, s32 fallback_smoketype)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_smoketype;
	}
	if (!weaponGraphRuntimeEnabled()) {
		return EFFECT_GRAPH_RESOLVE_FAILED;
	}

	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_smoke && record->smoke_type >= 0) {
		return record->smoke_type;
	}
	{
		s32 profile = effectExecutorResolveSmokeProfile(effect_ref);
		if (profile >= 0) return profile;
	}

	return EFFECT_GRAPH_RESOLVE_FAILED;
}

s32 effectGraphResolveSound(const char *effect_ref, s32 fallback_soundnum)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_soundnum;
	}
	if (!weaponGraphRuntimeEnabled()) {
		return EFFECT_GRAPH_RESOLVE_FAILED;
	}

	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_sound && record->soundnum > 0) {
		return record->soundnum;
	}

	return EFFECT_GRAPH_RESOLVE_FAILED;
}
