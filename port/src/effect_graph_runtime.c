/**
 * effect_graph_runtime.c -- .pdeffect compiler/runtime + OG-executor bridges.
 *
 * c3849 Unit 8 (full runtime behind the Unit 1b bridge seams). Mirrors the
 * weapon_graph_runtime slot-array/clear/register/accessor layout, scaled
 * down: effect records key by asset_id string only (no runtime_index slots,
 * no owner bits). Gameplay reads go through GetForGameplay/the bridges; in
 * product builds the Wave 7 cutover keeps the shared runtime gate enabled,
 * while tests can still disable it for parity coverage.
 *
 * Closure rule: the executor IS the OG explosion/spark/smoke machinery. The
 * compiler maps class words to EXISTING table indices (and, for tinted
 * effect.spark nodes, appends a clone of the OG SPARKTYPE_PROJECTILE row to
 * the bounded registry in src/game/sparks_custom.c). No new particle or
 * render systems.
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
#include "effect_graph_runtime.h"
#include "modarchive.h"
#include "pdeffect_source.h"
#include "system.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"

static effect_graph_runtime_t s_effect_runtimes[EFFECT_GRAPH_RUNTIME_MAX_EFFECTS];

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
	for (s32 i = 0; i < EFFECT_GRAPH_RUNTIME_MAX_EFFECTS; i++) {
		if (s_effect_runtimes[i].valid &&
				strcmp(s_effect_runtimes[i].asset_id, asset_id) == 0) {
			return &s_effect_runtimes[i];
		}
	}
	return NULL;
}

static effect_graph_runtime_t *effectRuntimeAlloc(const char *asset_id)
{
	effect_graph_runtime_t *existing = effectRuntimeFindMutable(asset_id);
	if (existing) {
		memset(existing, 0, sizeof(*existing));
		return existing;
	}
	for (s32 i = 0; i < EFFECT_GRAPH_RUNTIME_MAX_EFFECTS; i++) {
		if (!s_effect_runtimes[i].valid) {
			memset(&s_effect_runtimes[i], 0, sizeof(s_effect_runtimes[i]));
			return &s_effect_runtimes[i];
		}
	}
	return NULL;
}

void effectGraphRuntimeClearAsset(const char *asset_id)
{
	effect_graph_runtime_t *record = effectRuntimeFindMutable(asset_id);
	if (record) {
		/* The record's custom spark row (if any) stays in the registry until
		 * the next full clear: the registry is append-only by design and
		 * re-registration of the same tint deduplicates back onto the same
		 * row, so reload cycles do not leak slots. */
		memset(record, 0, sizeof(*record));
	}
}

void effectGraphRuntimeClearAll(void)
{
	memset(s_effect_runtimes, 0, sizeof(s_effect_runtimes));
	/* Same reset path as the effect records (header contract): mod reloads
	 * that clear every record also release the custom spark rows. */
	sparksResetCustomTypes();
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
				"EFFECTGRAPH.PARSE: '%s' unknown explosion_class '%s'; detonation keeps the OG fallback",
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
					"EFFECTGRAPH.PARSE: '%s' unknown smoke class '%s'; smoke keeps the OG fallback",
					ir->asset_id, cls);
			}
		} else if (effectParamInt(ir, node, "smoke_type", &ivalue)) {
			/* Direct SMOKETYPE_* index passthrough, bounds-checked against
			 * the OG table (SMOKETYPE_NONE..SMOKETYPE_UFO). */
			if (ivalue >= SMOKETYPE_NONE && ivalue <= SMOKETYPE_UFO) {
				record->smoke_type = ivalue;
			} else {
				sysLogPrintf(LOG_WARNING,
					"EFFECTGRAPH.PARSE: '%s' smoke_type %d outside the OG table; smoke keeps the OG fallback",
					ir->asset_id, ivalue);
			}
		}
		break;
	}

	default:
		/* effect.tint/glow/shimmer/darken/screen/particle: presentation
		 * kinds, gameplay-inert (intensity/sound captured above). */
		break;
	}
}

static s32 effectRuntimeRegisterIr(const weapon_graph_ir_t *ir,
                                   char *err, size_t err_cap)
{
	effect_graph_runtime_t *record;

	if (!ir || ir->asset_type != ASSET_EFFECT || !ir->asset_id[0]) {
		setErr(err, err_cap,
			"effect IR registration requires an effect graph with asset_id");
		return -1;
	}

	record = effectRuntimeAlloc(ir->asset_id);
	if (!record) {
		setErr(err, err_cap, "effect runtime table is full");
		return -1;
	}

	record->valid = 1;
	copyStr(record->asset_id, sizeof(record->asset_id), ir->asset_id);
	copyStr(record->source_sha256, sizeof(record->source_sha256),
		ir->source_sha256);
	copyStr(record->ir_sha256, sizeof(record->ir_sha256), ir->ir_sha256);
	record->explosion_type = -1;
	record->spark_type = -1;
	record->smoke_type = -1;

	for (s32 i = 0; i < ir->node_count; i++) {
		effectRuntimeFromNode(ir, &ir->nodes[i], record);
	}

	return 0;
}

/* ---------------------------------------------------------------------------
 * Registration entry points.
 * ------------------------------------------------------------------------- */

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
			return -1;
		}
		if (!ir.asset_id[0]) {
			copyStr(ir.asset_id, sizeof(ir.asset_id), asset_id);
		}
	}
	return effectRuntimeRegisterIr(&ir, err, err_cap);
}

s32 effectGraphRuntimeRegisterArchiveBytes(const void *archive_bytes,
                                           u32 archive_size,
                                           char *err, size_t err_cap)
{
	pd_effect_source_info_t public_source;
	weapon_graph_archive_descriptor_t desc;
	void *graph = NULL;
	u32 graph_size = 0;
	s32 result;

	if (!archive_bytes || archive_size == 0) {
		setErr(err, err_cap, "effect archive bytes register called with null input");
		return -1;
	}
	if (!pdEffectSourceParseArchiveBytes(archive_bytes, archive_size, NULL,
			&public_source, err, err_cap)) {
		return -1;
	}
	/* T-ASSETS-017 validates v2 at the real activation boundary. T-ASSETS-018
	 * owns applying these rows to native executor tables; do not silently feed
	 * the profile library to the legacy node compiler. */
	if (public_source.format == PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY) {
		return 0;
	}
	if (weaponGraphArchiveReadDescriptorBytes(archive_bytes, archive_size,
			ASSET_EFFECT, &desc, err, err_cap) != 0) {
		return -1;
	}
	if (!desc.behavior_graph[0]) {
		setErr(err, err_cap, "effect archive declares no effect_file graph member");
		return -1;
	}
	graph = modArchiveExtractMemAlloc(archive_bytes, archive_size,
		desc.behavior_graph, &graph_size);
	if (!graph || graph_size == 0) {
		free(graph);
		setErr(err, err_cap, "effect archive missing graph entry %s",
			desc.behavior_graph);
		return -1;
	}
	result = effectGraphRuntimeRegisterGraphJson(desc.catalog_id,
		(const char *)graph, graph_size, err, err_cap);
	free(graph);
	return result;
}

s32 effectGraphRuntimeRegisterArchive(const char *archive_path,
                                      char *err, size_t err_cap)
{
	pd_effect_source_info_t public_source;
	weapon_graph_archive_descriptor_t desc;
	char *graph = NULL;
	u32 graph_size = 0;
	s32 result;

	if (!archive_path || !archive_path[0]) {
		setErr(err, err_cap, "effect archive register called with null input");
		return -1;
	}
	if (!pdEffectSourceParseArchiveFile(archive_path, NULL, &public_source,
			err, err_cap)) {
		return -1;
	}
	if (public_source.format == PD_EFFECT_SOURCE_FORMAT_PROFILE_LIBRARY) {
		return 0;
	}
	if (weaponGraphArchiveReadDescriptorFile(archive_path, ASSET_EFFECT,
			&desc, err, err_cap) != 0) {
		return -1;
	}
	if (!desc.behavior_graph[0]) {
		setErr(err, err_cap, "%s declares no effect_file graph member",
			archive_path);
		return -1;
	}
	if (weaponGraphArchiveReadTextFile(archive_path, desc.behavior_graph,
			&graph, &graph_size) != 0) {
		setErr(err, err_cap, "%s missing graph entry %s",
			archive_path, desc.behavior_graph);
		return -1;
	}
	result = effectGraphRuntimeRegisterGraphJson(desc.catalog_id,
		graph, graph_size, err, err_cap);
	free(graph);
	return result;
}

/* ---------------------------------------------------------------------------
 * OG-executor bridges (Unit 1b signatures, frozen).
 * ------------------------------------------------------------------------- */

s32 effectGraphResolveExplosionType(const char *effect_ref, s32 fallback_exptype)
{
	const effect_graph_runtime_t *record;
	s32 resolved;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_exptype;
	}
	/* Test gate: with the shared runtime gate off every consumer must be
	 * bit-identical to OG, so the fallback wins unconditionally. */
	if (!weaponGraphRuntimeEnabled()) {
		return fallback_exptype;
	}

	resolved = weaponGraphResolveExplosionRef(effect_ref);
	if (resolved >= 0) {
		return resolved;
	}

	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_explosion && record->explosion_type >= 0) {
		return record->explosion_type;
	}

	return fallback_exptype;
}

s32 effectGraphResolveSparkType(const char *effect_ref, s32 fallback_sparktype)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_sparktype;
	}

	/* GetForGameplay carries the toggle gate: NULL while off. spark_type is
	 * the custom registry row index for tinted sparks (>= base count); an
	 * untinted/unallocated spark record stays on the OG fallback. */
	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_spark && record->spark_type >= 0) {
		return record->spark_type;
	}

	return fallback_sparktype;
}

s32 effectGraphResolveSmokeType(const char *effect_ref, s32 fallback_smoketype)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_smoketype;
	}

	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_smoke && record->smoke_type >= 0) {
		return record->smoke_type;
	}

	return fallback_smoketype;
}

s32 effectGraphResolveSound(const char *effect_ref, s32 fallback_soundnum)
{
	const effect_graph_runtime_t *record;

	if (!effect_ref || !effect_ref[0]) {
		return fallback_soundnum;
	}

	record = effectGraphRuntimeGetForGameplay(effect_ref);
	if (record && record->has_sound && record->soundnum > 0) {
		return record->soundnum;
	}

	return fallback_soundnum;
}
