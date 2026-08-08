/**
 * weapon_graph_runtime.c -- graph validator and deterministic IR compiler.
 *
 * The editor-facing asset is JSON. Runtime code consumes this compact IR so
 * gameplay adapters do not parse editor affordances on the hot path.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assetcatalog_model_slots.h"  /* B-911: custom-model private slots for embedded meshes */
#include "system.h"  /* B-911/B-912: sysLogPrintf for the embedded-mesh ingest (pd-tests has no PCH) */
#include "constants.h"
#include "effect_graph_runtime.h"  /* c3849 Unit 8: nested .pdeffect ingestion */
#include "loader_enum_reverse.h"
#include "modarchive.h"
#include "platform.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"

typedef struct json_span {
	const char *start;
	const char *end;
} json_span_t;

typedef struct module_info {
	const char *kind;
	asset_type_e type;
	weapon_graph_opcode_e opcode;
} module_info_t;

#define WEAPON_GRAPH_RUNTIME_MAX_WEAPONS 96
#define WEAPON_GRAPH_RUNTIME_MAX_FUNCS   2
#define WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES 128
#define WEAPON_GRAPH_RUNTIME_MAX_ENTITIES    128
#define WEAPON_GRAPH_RUNTIME_OWNER_WORDS \
	((WEAPON_GRAPH_RUNTIME_MAX_WEAPONS + 31) / 32)

static s32 s_runtime_enabled = 1;
static weapon_graph_held_function_t
	s_held_functions[WEAPON_GRAPH_RUNTIME_MAX_WEAPONS][WEAPON_GRAPH_RUNTIME_MAX_FUNCS];
/* c3849 Wave 5f Unit 2: per-weapon tunables beside the held records. */
static weapon_graph_weapon_settings_t
	s_weapon_settings[WEAPON_GRAPH_RUNTIME_MAX_WEAPONS];
/* c3849 Wave 5f Unit 2 (B6.3): variables table active for the CURRENT held
 * weapon graph compile. compileParam resolves "$name" string params from it;
 * NULL means no substitution scope (behavior graphs, bare compile calls).
 * Single-threaded by contract: the walker serializes register_fn under
 * register_mutex (loader_walker_common.c) and the loose/catalog path runs on
 * the activation thread. */
static const weapon_graph_weapon_settings_t *s_compile_variables = NULL;
static weapon_graph_projectile_runtime_t
	s_projectile_runtimes[WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES];
static weapon_graph_entity_runtime_t
	s_entity_runtimes[WEAPON_GRAPH_RUNTIME_MAX_ENTITIES];
static u32
	s_projectile_owner_bits[WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES][WEAPON_GRAPH_RUNTIME_OWNER_WORDS];
static u32
	s_entity_owner_bits[WEAPON_GRAPH_RUNTIME_MAX_ENTITIES][WEAPON_GRAPH_RUNTIME_OWNER_WORDS];
static s32 s_projectile_external_refs[WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES];
static s32 s_entity_external_refs[WEAPON_GRAPH_RUNTIME_MAX_ENTITIES];

static s32 weaponGraphRuntimeRegisterBehaviorIr(const weapon_graph_ir_t *ir,
                                                char *err, size_t err_cap);
static s32 weaponGraphRuntimeRegisterBehaviorIrOwned(const weapon_graph_ir_t *ir,
                                                     s32 owner_weapon,
                                                     s32 external_ref,
                                                     char *err, size_t err_cap);

static const module_info_t s_modules[] = {
	{ "function.empty", ASSET_WEAPON, WEAPON_GRAPH_OP_FUNCTION_EMPTY },
	{ "event.trigger_pressed", ASSET_WEAPON, WEAPON_GRAPH_OP_EVENT_TRIGGER_PRESSED },
	{ "event.trigger_held", ASSET_WEAPON, WEAPON_GRAPH_OP_EVENT_TRIGGER_HELD },
	{ "event.trigger_released", ASSET_WEAPON, WEAPON_GRAPH_OP_EVENT_TRIGGER_RELEASED },
	{ "gate.ammo_available", ASSET_WEAPON, WEAPON_GRAPH_OP_GATE_AMMO_AVAILABLE },
	{ "gate.cooldown_ready", ASSET_WEAPON, WEAPON_GRAPH_OP_GATE_COOLDOWN_READY },
	{ "gate.target_lock", ASSET_WEAPON, WEAPON_GRAPH_OP_GATE_TARGET_LOCK },
	{ "ammo.consume", ASSET_WEAPON, WEAPON_GRAPH_OP_AMMO_CONSUME },
	{ "ammo.reserve_transfer", ASSET_WEAPON, WEAPON_GRAPH_OP_AMMO_RESERVE_TRANSFER },
	{ "fire.hitscan", ASSET_WEAPON, WEAPON_GRAPH_OP_FIRE_HITSCAN },
	{ "fire.auto_cadence", ASSET_WEAPON, WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE },
	{ "fire.burst", ASSET_WEAPON, WEAPON_GRAPH_OP_FIRE_BURST },
	{ "fire.charge_release", ASSET_WEAPON, WEAPON_GRAPH_OP_FIRE_CHARGE_RELEASE },
	{ "fire.beam_tick", ASSET_WEAPON, WEAPON_GRAPH_OP_FIRE_BEAM_TICK },
	{ "spawn.fired_projectile", ASSET_WEAPON, WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE },
	{ "spawn.thrown_physical", ASSET_WEAPON, WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL },
	{ "melee.strike", ASSET_WEAPON, WEAPON_GRAPH_OP_MELEE_STRIKE },
	{ "special.remote_detonator", ASSET_WEAPON, WEAPON_GRAPH_OP_SPECIAL_REMOTE_DETONATOR },
	{ "special.combat_boost", ASSET_WEAPON, WEAPON_GRAPH_OP_SPECIAL_COMBAT_BOOST },
	{ "special.weapon_state", ASSET_WEAPON, WEAPON_GRAPH_OP_SPECIAL_WEAPON_STATE },
	{ "device.activate", ASSET_WEAPON, WEAPON_GRAPH_OP_DEVICE_ACTIVATE },
	{ "presentation.weapon_visibility", ASSET_WEAPON, WEAPON_GRAPH_OP_PRESENTATION_WEAPON_VISIBILITY },
	{ "presentation.reticle_overlay_camera", ASSET_WEAPON, WEAPON_GRAPH_OP_PRESENTATION_RETICLE_OVERLAY_CAMERA },

	{ "projectile.spawn_state", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_SPAWN_STATE },
	{ "projectile.motion", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_MOTION },
	{ "projectile.trajectory_correction", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_TRAJECTORY_CORRECTION },
	{ "projectile.homing", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_HOMING },
	{ "projectile.fly_by_wire", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_FLY_BY_WIRE },
	{ "projectile.wall_hugger", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_WALL_HUGGER },
	{ "projectile.sticky_attach", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_STICKY_ATTACH },
	{ "projectile.bounce_slide", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_BOUNCE_SLIDE },
	{ "projectile.timer", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_TIMER },
	{ "projectile.impact", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_IMPACT },
	{ "projectile.trail", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_TRAIL },
	{ "projectile.transition_to_entity", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_TRANSITION_TO_ENTITY },
	{ "projectile.pickup_recover", ASSET_PROJECTILE, WEAPON_GRAPH_OP_PROJECTILE_PICKUP_RECOVER },

	{ "entity.armed_explosive", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_ARMED_EXPLOSIVE },
	{ "entity.proxy_trigger", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_PROXY_TRIGGER },
	{ "entity.remote_detonatable", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_REMOTE_DETONATABLE },
	{ "entity.timed_detonatable", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_TIMED_DETONATABLE },
	{ "entity.nbomb_storm", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_NBOMB_STORM },
	{ "entity.autogun", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_AUTOGUN },
	{ "entity.sticky_device", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_STICKY_DEVICE },
	{ "entity.owner_cleanup", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_OWNER_CLEANUP },
	{ "entity.interaction", ASSET_ENTITY, WEAPON_GRAPH_OP_ENTITY_INTERACTION },

	/* c3849 Unit 8: .pdeffect module family. The six presentation kinds match
	 * romextract_pdmeta.c s_effectTypeKey (base archives author exactly one
	 * effect.<type_key> node) and are gameplay-inert; the three gameplay kinds
	 * resolve into existing OG table indices at registration time
	 * (effect_graph_runtime.c). */
	{ "effect.tint", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_TINT },
	{ "effect.glow", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_GLOW },
	{ "effect.shimmer", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_SHIMMER },
	{ "effect.darken", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_DARKEN },
	{ "effect.screen", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_SCREEN },
	{ "effect.particle", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_PARTICLE },
	{ "effect.explosion", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_EXPLOSION },
	{ "effect.spark", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_SPARK },
	{ "effect.smoke", ASSET_EFFECT, WEAPON_GRAPH_OP_EFFECT_SMOKE },
};

static const weapon_graph_parity_module_t s_parity_modules[] = {
	{ WEAPON_GRAPH_OP_FIRE_HITSCAN, "og.fire.hitscan", "held_hitscan", "bondgun.c held fire path", "damage, recoil, cadence, muzzle, audio" },
	{ WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE, "og.fire.auto_cadence", "held_cadence", "bondgun.c automatic cadence path", "spin, rpm, ammo, recovery" },
	{ WEAPON_GRAPH_OP_FIRE_BURST, "og.fire.burst", "held_burst", "bondgun.c burst fire path", "burst count, timing, ammo" },
	{ WEAPON_GRAPH_OP_FIRE_CHARGE_RELEASE, "og.fire.charge_release", "held_charge", "bondgun.c charge/release path", "charge timing, release damage" },
	{ WEAPON_GRAPH_OP_FIRE_BEAM_TICK, "og.fire.beam_tick", "held_beam", "bondgun.c beam/laser tick path", "beam timing, sight, projection" },
	{ WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE, "og.spawn.fired_projectile", "projectile_spawn", "bondgun.c projectile creation path", "spawn state, owner, speed, model" },
	{ WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL, "og.spawn.thrown_physical", "thrown_physical", "bondgun.c thrown object path", "throw timing, owner, carrier" },
	{ WEAPON_GRAPH_OP_MELEE_STRIKE, "og.melee.strike", "melee", "bondgun.c melee path", "range, damage, recovery" },
	{ WEAPON_GRAPH_OP_SPECIAL_REMOTE_DETONATOR, "og.special.remote_detonator", "remote_detonator", "propobj.c mine detonation path", "owner links, remote signal" },
	{ WEAPON_GRAPH_OP_SPECIAL_COMBAT_BOOST, "og.special.combat_boost", "combat_boost", "bondgun.c combat boost path", "activation, recovery, device state" },
	{ WEAPON_GRAPH_OP_DEVICE_ACTIVATE, "og.device.activate", "device", "bondgun.c device activation path", "device ids, toggles, cleanup" },
	{ WEAPON_GRAPH_OP_PROJECTILE_MOTION, "og.projectile.motion", "projectile_motion", "propobj.c projectileTick/projectileLaunch", "motion, gravity, sliding, bounce" },
	{ WEAPON_GRAPH_OP_PROJECTILE_TRAJECTORY_CORRECTION, "og.projectile.trajectory", "projectile_trajectory", "bondgun.c projectile trajectory solve", "aim correction, launch velocity" },
	{ WEAPON_GRAPH_OP_PROJECTILE_HOMING, "og.projectile.homing", "projectile_guidance", "propobj.c homing rocket path", "targeting, steering, loss" },
	{ WEAPON_GRAPH_OP_PROJECTILE_FLY_BY_WIRE, "og.projectile.fly_by_wire", "projectile_guidance", "propobj.c Slayer control path", "manual steering, smoke, owner death" },
	{ WEAPON_GRAPH_OP_PROJECTILE_WALL_HUGGER, "og.projectile.wall_hugger", "projectile_wallhugger", "propobj.c Devastator path", "wall stick, fall, post-fall timer" },
	{ WEAPON_GRAPH_OP_PROJECTILE_STICKY_ATTACH, "og.projectile.sticky_attach", "projectile_sticky", "propobj.c embed/stick path", "surface filters, embed policy" },
	{ WEAPON_GRAPH_OP_PROJECTILE_BOUNCE_SLIDE, "og.projectile.bounce_slide", "projectile_bounce", "propobj.c bounce/slide path", "bounce count, slide friction" },
	{ WEAPON_GRAPH_OP_PROJECTILE_TIMER, "og.projectile.timer", "projectile_timer", "propobj.c projectile timer path", "timer start, expiry, detonation" },
	{ WEAPON_GRAPH_OP_PROJECTILE_IMPACT, "og.projectile.impact", "projectile_impact", "propobj.c impact/explosion path", "impact filter, effects, consumption" },
	{ WEAPON_GRAPH_OP_PROJECTILE_TRAIL, "og.projectile.trail", "projectile_trail", "propobj.c smoke/trail path", "trail cadence and visual parity" },
	{ WEAPON_GRAPH_OP_PROJECTILE_TRANSITION_TO_ENTITY, "og.projectile.transition_to_entity", "projectile_entity_transition", "bondgun.c/propobj.c deploy transition", "carrier to deployed entity" },
	{ WEAPON_GRAPH_OP_ENTITY_ARMED_EXPLOSIVE, "og.entity.armed_explosive", "entity_explosive", "propobj.c weaponTick mines", "arm timing, owner, detonation" },
	{ WEAPON_GRAPH_OP_ENTITY_PROXY_TRIGGER, "og.entity.proxy_trigger", "entity_proxy", "propobj.c proximity mine trigger", "radius, filters, self-attach policy" },
	{ WEAPON_GRAPH_OP_ENTITY_REMOTE_DETONATABLE, "og.entity.remote_detonatable", "entity_remote", "propobj.c remote mine path", "detonator ownership, coop policy" },
	{ WEAPON_GRAPH_OP_ENTITY_TIMED_DETONATABLE, "og.entity.timed_detonatable", "entity_timed", "propobj.c timed mine path", "timer and expiry" },
	{ WEAPON_GRAPH_OP_ENTITY_NBOMB_STORM, "og.entity.nbomb_storm", "entity_nbomb", "propobj.c nbombCreateStorm path", "storm creation, ownership" },
	{ WEAPON_GRAPH_OP_ENTITY_AUTOGUN, "og.entity.autogun", "entity_autogun", "propobj.c Laptop autogun path", "targeting, ammo, beam, pickup" },
	{ WEAPON_GRAPH_OP_ENTITY_STICKY_DEVICE, "og.entity.sticky_device", "entity_sticky_device", "propobj.c sticky device path", "attachment, visible state" },
	{ WEAPON_GRAPH_OP_ENTITY_OWNER_CLEANUP, "og.entity.owner_cleanup", "entity_owner_cleanup", "propobj.c owner cleanup path", "death/lost owner behavior" },
	{ WEAPON_GRAPH_OP_ENTITY_INTERACTION, "og.entity.interaction", "entity_interaction", "propobj.c pickup/recover path", "prompts, transfer, sound" },
	/* c3849 Unit 8: effect gameplay kinds. The executor IS the OG machinery
	 * (closure rule); the runtime only selects existing table rows (and, for
	 * sparks, appends tinted clones of an existing row). */
	{ WEAPON_GRAPH_OP_EFFECT_EXPLOSION, "og.effect.explosion", "effect_explosion", "explosions.c g_ExplosionTypes path", "class to EXPLOSIONTYPE_* selection" },
	{ WEAPON_GRAPH_OP_EFFECT_SPARK, "og.effect.spark", "effect_spark", "sparks.c g_SparkTypes path", "tinted custom spark rows" },
	{ WEAPON_GRAPH_OP_EFFECT_SMOKE, "og.effect.smoke", "effect_smoke", "smoke.c g_SmokeTypes path", "class to nearest SMOKETYPE_*" },
};

static void setErr(char *err, size_t err_cap, const char *fmt, ...)
{
	if (!err || err_cap == 0) return;
	va_list ap;
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

static const char *jsonSkipWs(const char *p, const char *end)
{
	while (p < end && isspace((unsigned char)*p)) p++;
	return p;
}

static const char *jsonSkipString(const char *p, const char *end)
{
	if (p >= end || *p != '"') return p;
	p++;
	while (p < end) {
		if (*p == '\\') {
			p += (p + 1 < end) ? 2 : 1;
			continue;
		}
		if (*p == '"') return p + 1;
		p++;
	}
	return end;
}

static const char *jsonFindMatching(const char *p, const char *end,
                                    char open_ch, char close_ch)
{
	s32 depth = 0;
	while (p < end) {
		if (*p == '"') {
			p = jsonSkipString(p, end);
			continue;
		}
		if (*p == open_ch) {
			depth++;
		} else if (*p == close_ch) {
			depth--;
			if (depth == 0) return p;
		}
		p++;
	}
	return NULL;
}

static const char *jsonValueEnd(const char *p, const char *end)
{
	p = jsonSkipWs(p, end);
	if (p >= end) return end;
	if (*p == '"') return jsonSkipString(p, end);
	if (*p == '{') {
		const char *m = jsonFindMatching(p, end, '{', '}');
		return m ? m + 1 : end;
	}
	if (*p == '[') {
		const char *m = jsonFindMatching(p, end, '[', ']');
		return m ? m + 1 : end;
	}
	while (p < end && *p != ',' && *p != '}' && *p != ']') p++;
	return p;
}

static s32 jsonReadObject(const char *value, const char *end, json_span_t *out)
{
	const char *p;
	const char *m;
	if (!value || !out) return 0;
	p = jsonSkipWs(value, end);
	if (p >= end || *p != '{') return 0;
	m = jsonFindMatching(p, end, '{', '}');
	if (!m) return 0;
	out->start = p + 1;
	out->end = m;
	return 1;
}

static s32 jsonReadArray(const char *value, const char *end, json_span_t *out)
{
	const char *p;
	const char *m;
	if (!value || !out) return 0;
	p = jsonSkipWs(value, end);
	if (p >= end || *p != '[') return 0;
	m = jsonFindMatching(p, end, '[', ']');
	if (!m) return 0;
	out->start = p + 1;
	out->end = m;
	return 1;
}

static const char *jsonFindKeyInObject(json_span_t object, const char *key)
{
	const char *p = object.start;
	const size_t key_len = key ? strlen(key) : 0;
	if (!key || key_len == 0) return NULL;

	while (p < object.end) {
		p = jsonSkipWs(p, object.end);
		if (p < object.end && *p == ',') {
			p++;
			continue;
		}
		if (p >= object.end) break;
		if (*p != '"') {
			p = jsonValueEnd(p, object.end);
			continue;
		}
		const char *s = p + 1;
		const char *after = jsonSkipString(p, object.end);
		const char *close = after > s ? after - 1 : after;
		const char *colon = jsonSkipWs(after, object.end);
		if (colon < object.end && *colon == ':' &&
				(size_t)(close - s) == key_len &&
				memcmp(s, key, key_len) == 0) {
			return colon + 1;
		}
		p = jsonValueEnd(colon < object.end ? colon + 1 : after, object.end);
	}
	return NULL;
}

static s32 jsonObjectString(json_span_t object, const char *key,
                            char *out, size_t out_len)
{
	const char *value = jsonFindKeyInObject(object, key);
	const char *p;
	size_t written = 0;
	if (!value || !out || out_len == 0) return 0;

	p = jsonSkipWs(value, object.end);
	if (p >= object.end || *p != '"') return 0;
	p++;
	while (p < object.end && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < object.end) {
			c = *p++;
		}
		if (written + 1 < out_len) out[written++] = c;
	}
	out[written] = '\0';
	return p < object.end && *p == '"';
}

static s32 jsonObjectObject(json_span_t object, const char *key, json_span_t *out)
{
	const char *value = jsonFindKeyInObject(object, key);
	return jsonReadObject(value, object.end, out);
}

static s32 jsonObjectArray(json_span_t object, const char *key, json_span_t *out)
{
	const char *value = jsonFindKeyInObject(object, key);
	return jsonReadArray(value, object.end, out);
}

static s32 jsonArrayNextObject(json_span_t array, const char **cursor,
                               json_span_t *out)
{
	const char *p;
	if (!cursor || !out) return 0;
	p = *cursor ? *cursor : array.start;
	while (p < array.end) {
		p = jsonSkipWs(p, array.end);
		if (p < array.end && *p == ',') {
			p++;
			continue;
		}
		if (p >= array.end) break;
		if (*p == '{') {
			const char *m = jsonFindMatching(p, array.end, '{', '}');
			if (!m) return 0;
			out->start = p + 1;
			out->end = m;
			*cursor = m + 1;
			return 1;
		}
		p = jsonValueEnd(p, array.end);
	}
	*cursor = array.end;
	return 0;
}

static s32 jsonObjectNextMember(json_span_t object, const char **cursor,
                                char *key, size_t key_cap,
                                json_span_t *value, char *type)
{
	const char *p;
	size_t written = 0;
	if (!cursor || !key || key_cap == 0 || !value || !type) return 0;
	p = *cursor ? *cursor : object.start;
	while (p < object.end) {
		p = jsonSkipWs(p, object.end);
		if (p < object.end && *p == ',') {
			p++;
			continue;
		}
		if (p >= object.end) break;
		if (*p != '"') return 0;
		p++;
		while (p < object.end && *p != '"') {
			char c = *p++;
			if (c == '\\' && p < object.end) c = *p++;
			if (written + 1 < key_cap) key[written++] = c;
		}
		if (p >= object.end || *p != '"') return 0;
		key[written] = '\0';
		p++;
		p = jsonSkipWs(p, object.end);
		if (p >= object.end || *p != ':') return 0;
		p++;
		p = jsonSkipWs(p, object.end);
		value->start = p;
		value->end = jsonValueEnd(p, object.end);
		if (p < object.end) *type = *p;
		else *type = '\0';
		*cursor = value->end;
		return 1;
	}
	*cursor = object.end;
	return 0;
}

static s32 startsWith(const char *value, const char *prefix)
{
	return value && prefix && strncmp(value, prefix, strlen(prefix)) == 0;
}

static s32 endsWith(const char *value, const char *suffix)
{
	size_t vlen;
	size_t slen;
	if (!value || !suffix) return 0;
	vlen = strlen(value);
	slen = strlen(suffix);
	return vlen >= slen && strcmp(value + vlen - slen, suffix) == 0;
}

const char *weaponGraphSchemaForType(asset_type_e type)
{
	switch (type) {
	case ASSET_WEAPON:     return "pd.weapon_graph.v1";
	case ASSET_PROJECTILE: return "pd.projectile_graph.v1";
	case ASSET_ENTITY:     return "pd.entity_graph.v1";
	/* c3849 Unit 8 (B6.4): canonical effect schema. The legacy
	 * "pd2.effect.graph.v1" spelling (stale base archives predating the
	 * emitter update) is accepted in weaponGraphCompileJson with a one-time
	 * LOG_WARNING. */
	case ASSET_EFFECT:     return "pd.effect_graph.v1";
	default:               return NULL;
	}
}

const char *weaponGraphOpcodeName(weapon_graph_opcode_e opcode)
{
	for (size_t i = 0; i < sizeof(s_modules) / sizeof(s_modules[0]); i++) {
		if (s_modules[i].opcode == opcode) return s_modules[i].kind;
	}
	if (opcode == WEAPON_GRAPH_OP_INVALID) return "invalid";
	return "unknown";
}

weapon_graph_opcode_e weaponGraphOpcodeForKind(asset_type_e graph_type,
                                               const char *kind)
{
	if (!kind || !kind[0]) return WEAPON_GRAPH_OP_INVALID;
	for (size_t i = 0; i < sizeof(s_modules) / sizeof(s_modules[0]); i++) {
		if (s_modules[i].type == graph_type && strcmp(s_modules[i].kind, kind) == 0) {
			return s_modules[i].opcode;
		}
	}
	return WEAPON_GRAPH_OP_INVALID;
}

size_t weaponGraphParityModuleCount(void)
{
	return sizeof(s_parity_modules) / sizeof(s_parity_modules[0]);
}

const weapon_graph_parity_module_t *weaponGraphParityModuleAt(size_t index)
{
	if (index >= weaponGraphParityModuleCount()) return NULL;
	return &s_parity_modules[index];
}

const weapon_graph_parity_module_t *weaponGraphParityModuleForOpcode(
	weapon_graph_opcode_e opcode)
{
	for (size_t i = 0; i < weaponGraphParityModuleCount(); i++) {
		if (s_parity_modules[i].opcode == opcode) return &s_parity_modules[i];
	}
	return NULL;
}

const char *weaponGraphParityModuleNameForOpcode(weapon_graph_opcode_e opcode)
{
	const weapon_graph_parity_module_t *module =
		weaponGraphParityModuleForOpcode(opcode);
	return module ? module->module_name : "";
}

s32 weaponGraphRuntimeEnabled(void)
{
#ifndef PD_TESTS
	return 1;
#else
	return s_runtime_enabled ? 1 : 0;
#endif
}

void weaponGraphRuntimeSetEnabled(s32 enabled)
{
#ifdef PD_TESTS
	s_runtime_enabled = enabled ? 1 : 0;
#else
	(void)enabled;
#endif
}

/* c3849 Unit 1b (binding spec B2): single explosion-ref table. Values mirror
 * the EXPLOSIONTYPE_* constants (constants.h:919-940, included above). The
 * pdeffect class words (tiny/small/medium/...) are legal ONLY inside
 * .pdeffect graph bodies, never here. Spark refs intentionally have NO base
 * vocabulary: the OG spark table has no stable authored names worth pinning,
 * so spark refs stay runtime-resolved through effectGraphResolveSparkType
 * (fallback verbatim until the Unit 8 effect runtime lands). */
s32 weaponGraphResolveExplosionRef(const char *ref)
{
	static s32 s_warned_alias_small = 0;
	static s32 s_warned_alias_laptop = 0;

	if (!ref || !ref[0]) {
		return -1;
	}

	if (strcmp(ref, "base:explosion_rocket") == 0) {
		return EXPLOSIONTYPE_ROCKET;
	}
	if (strcmp(ref, "base:explosion_huge") == 0) {
		return EXPLOSIONTYPE_HUGE17;
	}
	if (strcmp(ref, "base:explosion_sdgrenade") == 0) {
		return EXPLOSIONTYPE_SDGRENADE;
	}
	if (strcmp(ref, "base:explosion_phoenix") == 0) {
		return EXPLOSIONTYPE_PHOENIX;
	}
	if (strcmp(ref, "base:explosion_dragonbombspy") == 0) {
		return EXPLOSIONTYPE_DRAGONBOMBSPY;
	}

	/* Deprecated impact-cluster spellings: accepted, warned once. */
	if (strcmp(ref, "base:explosion_small") == 0) {
		if (!s_warned_alias_small) {
			s_warned_alias_small = 1;
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.PARSE: explosion ref 'base:explosion_small' is deprecated; use 'base:explosion_phoenix'");
		}
		return EXPLOSIONTYPE_PHOENIX;
	}
	if (strcmp(ref, "base:explosion_laptop") == 0) {
		if (!s_warned_alias_laptop) {
			s_warned_alias_laptop = 1;
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.PARSE: explosion ref 'base:explosion_laptop' is deprecated; use the canonical EXPLOSIONTYPE token vocabulary");
		}
		return EXPLOSIONTYPE_LAPTOP;
	}

	return -1;
}

/* c3849 Unit 1c: rpm -> autogunTickShoot firecount interval. The OG laptop
 * autogun fires every other firecount tick (1800 rpm baseline = interval 1);
 * halving the rpm doubles the interval. Clamped to >= 1 so a bad authored
 * cadence can never divide by zero or stall the gun. */
s32 weaponGraphAutogunFireInterval(f32 rpm)
{
	s32 interval;

	if (rpm <= 0.0f) {
		return 1;
	}

	interval = (s32)((1800.0f / rpm) + 0.5f);

	if (interval < 1) {
		interval = 1;
	}

	return interval;
}

/* c3849 Wave 5 Unit 6: detonator provenance predicate for the custom remote
 * sub-branch (propobj.c weaponTick CUSTOM ENTITY ARM). Resolution is lazy
 * (signal time): a remote detonation is a one-shot event, so no per-tick
 * catalog work ever happens. Match semantics (pinned by the unit tests):
 * - empty/unauthored detonator_ref: ANY signal matches, including the -1
 *   wildcard weaponnum (OG parity - the OG mask carries no identity);
 * - authored detonator_ref: demands an exact runtime weaponnum match, so
 *   the -1 wildcard does NOT satisfy it;
 * - authored but unresolved ref: never matches (one-time LOG_WARNING). */
s32 weaponGraphEntityRemoteSignalMatches(
		const weapon_graph_entity_runtime_t *entity, s32 weaponnum)
{
	static s32 s_warned_unresolved = 0;
	catalog_weapon_result_t weapon;

	if (!entity || entity->detonator_ref[0] == '\0') {
		return 1;
	}

	if (weaponnum < 0) {
		return 0;
	}

	if (!catalogResolveWeapon(entity->detonator_ref, &weapon)
			|| weapon.weapon_num <= 0) {
		if (!s_warned_unresolved) {
			s_warned_unresolved = 1;
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.RUNTIME: detonator_ref '%s' unresolved at signal time; remote signal rejected",
				entity->detonator_ref);
		}
		return 0;
	}

	return weapon.weapon_num == weaponnum ? 1 : 0;
}

static s32 heldIndexValid(s32 weaponnum, s32 funcindex)
{
	return weaponnum >= 0 &&
		weaponnum < WEAPON_GRAPH_RUNTIME_MAX_WEAPONS &&
		funcindex >= 0 &&
		funcindex < WEAPON_GRAPH_RUNTIME_MAX_FUNCS;
}

static void ownerBitsSet(u32 bits[WEAPON_GRAPH_RUNTIME_OWNER_WORDS],
                         s32 weaponnum)
{
	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) return;
	bits[weaponnum / 32] |= (u32)1 << (weaponnum % 32);
}

static void ownerBitsClear(u32 bits[WEAPON_GRAPH_RUNTIME_OWNER_WORDS],
                           s32 weaponnum)
{
	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) return;
	bits[weaponnum / 32] &= ~((u32)1 << (weaponnum % 32));
}

static s32 ownerBitsAny(const u32 bits[WEAPON_GRAPH_RUNTIME_OWNER_WORDS])
{
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_OWNER_WORDS; i++) {
		if (bits[i]) return 1;
	}
	return 0;
}

static void projectileRuntimeClearSlot(s32 index)
{
	if (index < 0 || index >= WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES) return;
	memset(&s_projectile_runtimes[index], 0,
		sizeof(s_projectile_runtimes[index]));
	memset(s_projectile_owner_bits[index], 0,
		sizeof(s_projectile_owner_bits[index]));
	s_projectile_external_refs[index] = 0;
}

static void entityRuntimeClearSlot(s32 index)
{
	if (index < 0 || index >= WEAPON_GRAPH_RUNTIME_MAX_ENTITIES) return;
	memset(&s_entity_runtimes[index], 0, sizeof(s_entity_runtimes[index]));
	memset(s_entity_owner_bits[index], 0, sizeof(s_entity_owner_bits[index]));
	s_entity_external_refs[index] = 0;
}

static void weaponGraphRuntimeClearWeaponDependencies(s32 weaponnum)
{
	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) return;

	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES; i++) {
		ownerBitsClear(s_projectile_owner_bits[i], weaponnum);
		if (s_projectile_runtimes[i].valid &&
				!s_projectile_external_refs[i] &&
				!ownerBitsAny(s_projectile_owner_bits[i])) {
			projectileRuntimeClearSlot(i);
		}
	}

	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_ENTITIES; i++) {
		ownerBitsClear(s_entity_owner_bits[i], weaponnum);
		if (s_entity_runtimes[i].valid &&
				!s_entity_external_refs[i] &&
				!ownerBitsAny(s_entity_owner_bits[i])) {
			entityRuntimeClearSlot(i);
		}
	}
}

void weaponGraphRuntimeClearWeapon(s32 weaponnum)
{
	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) return;
	memset(s_held_functions[weaponnum], 0, sizeof(s_held_functions[weaponnum]));
	/* c3849 Wave 5f Unit 2: tunables share the held-record lifecycle. */
	memset(&s_weapon_settings[weaponnum], 0, sizeof(s_weapon_settings[weaponnum]));
	weaponGraphRuntimeClearWeaponDependencies(weaponnum);
}

static weapon_graph_projectile_runtime_t *projectileRuntimeFindMutable(
	const char *asset_id)
{
	if (!asset_id || !asset_id[0]) return NULL;
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES; i++) {
		if (s_projectile_runtimes[i].valid &&
				strcmp(s_projectile_runtimes[i].asset_id, asset_id) == 0) {
			return &s_projectile_runtimes[i];
		}
	}
	return NULL;
}

static s32 projectileRuntimeFindIndex(const char *asset_id)
{
	if (!asset_id || !asset_id[0]) return -1;
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES; i++) {
		if (s_projectile_runtimes[i].valid &&
				strcmp(s_projectile_runtimes[i].asset_id, asset_id) == 0) {
			return i;
		}
	}
	return -1;
}

static weapon_graph_entity_runtime_t *entityRuntimeFindMutable(
	const char *asset_id)
{
	if (!asset_id || !asset_id[0]) return NULL;
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_ENTITIES; i++) {
		if (s_entity_runtimes[i].valid &&
				strcmp(s_entity_runtimes[i].asset_id, asset_id) == 0) {
			return &s_entity_runtimes[i];
		}
	}
	return NULL;
}

static s32 entityRuntimeFindIndex(const char *asset_id)
{
	if (!asset_id || !asset_id[0]) return -1;
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_ENTITIES; i++) {
		if (s_entity_runtimes[i].valid &&
				strcmp(s_entity_runtimes[i].asset_id, asset_id) == 0) {
			return i;
		}
	}
	return -1;
}

static weapon_graph_projectile_runtime_t *projectileRuntimeAlloc(
	const char *asset_id)
{
	s32 existing = projectileRuntimeFindIndex(asset_id);
	if (existing >= 0) {
		memset(&s_projectile_runtimes[existing], 0,
			sizeof(s_projectile_runtimes[existing]));
		return &s_projectile_runtimes[existing];
	}
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_PROJECTILES; i++) {
		if (!s_projectile_runtimes[i].valid) {
			projectileRuntimeClearSlot(i);
			return &s_projectile_runtimes[i];
		}
	}
	return NULL;
}

static weapon_graph_entity_runtime_t *entityRuntimeAlloc(const char *asset_id)
{
	s32 existing = entityRuntimeFindIndex(asset_id);
	if (existing >= 0) {
		memset(&s_entity_runtimes[existing], 0,
			sizeof(s_entity_runtimes[existing]));
		return &s_entity_runtimes[existing];
	}
	for (s32 i = 0; i < WEAPON_GRAPH_RUNTIME_MAX_ENTITIES; i++) {
		if (!s_entity_runtimes[i].valid) {
			entityRuntimeClearSlot(i);
			return &s_entity_runtimes[i];
		}
	}
	return NULL;
}

void weaponGraphRuntimeClearAsset(const char *asset_id)
{
	s32 projectile = projectileRuntimeFindIndex(asset_id);
	if (projectile >= 0) {
		s_projectile_external_refs[projectile] = 0;
		if (!ownerBitsAny(s_projectile_owner_bits[projectile])) {
			projectileRuntimeClearSlot(projectile);
		}
	}

	s32 entity = entityRuntimeFindIndex(asset_id);
	if (entity >= 0) {
		s_entity_external_refs[entity] = 0;
		if (!ownerBitsAny(s_entity_owner_bits[entity])) {
			entityRuntimeClearSlot(entity);
		}
	}
}

void weaponGraphRuntimeClearAll(void)
{
	memset(s_held_functions, 0, sizeof(s_held_functions));
	memset(s_weapon_settings, 0, sizeof(s_weapon_settings));
	memset(s_projectile_runtimes, 0, sizeof(s_projectile_runtimes));
	memset(s_entity_runtimes, 0, sizeof(s_entity_runtimes));
	memset(s_projectile_owner_bits, 0, sizeof(s_projectile_owner_bits));
	memset(s_entity_owner_bits, 0, sizeof(s_entity_owner_bits));
	memset(s_projectile_external_refs, 0, sizeof(s_projectile_external_refs));
	memset(s_entity_external_refs, 0, sizeof(s_entity_external_refs));
}

const weapon_graph_held_function_t *weaponGraphRuntimeGetHeldFunction(
	s32 weaponnum, s32 funcindex)
{
	if (!heldIndexValid(weaponnum, funcindex)) return NULL;
	if (!s_held_functions[weaponnum][funcindex].valid) return NULL;
	return &s_held_functions[weaponnum][funcindex];
}

const weapon_graph_held_function_t *weaponGraphRuntimeGetHeldFunctionForGameplay(
	s32 weaponnum, s32 funcindex)
{
	if (!weaponGraphRuntimeEnabled()) return NULL;
	return weaponGraphRuntimeGetHeldFunction(weaponnum, funcindex);
}

const weapon_graph_weapon_settings_t *weaponGraphRuntimeGetWeaponSettings(
	s32 weaponnum)
{
	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) {
		return NULL;
	}
	if (!s_weapon_settings[weaponnum].valid) {
		return NULL;
	}
	return &s_weapon_settings[weaponnum];
}

const weapon_graph_weapon_settings_t *weaponGraphRuntimeGetWeaponSettingsForGameplay(
	s32 weaponnum)
{
	if (!weaponGraphRuntimeEnabled()) return NULL;
	return weaponGraphRuntimeGetWeaponSettings(weaponnum);
}

const weapon_graph_projectile_runtime_t *weaponGraphRuntimeGetProjectile(
	const char *asset_id)
{
	return projectileRuntimeFindMutable(asset_id);
}

const weapon_graph_projectile_runtime_t *weaponGraphRuntimeGetProjectileForGameplay(
	const char *asset_id)
{
	if (!weaponGraphRuntimeEnabled()) return NULL;
	return weaponGraphRuntimeGetProjectile(asset_id);
}

const weapon_graph_projectile_runtime_t *weaponGraphRuntimeGetProjectileForHeldFunction(
	const weapon_graph_held_function_t *held)
{
	if (!weaponGraphRuntimeEnabled() || !held || !held->valid ||
			!held->projectile_ref[0]) {
		return NULL;
	}
	return weaponGraphRuntimeGetProjectile(held->projectile_ref);
}

const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntity(
	const char *asset_id)
{
	return entityRuntimeFindMutable(asset_id);
}

const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntityForGameplay(
	const char *asset_id)
{
	if (!weaponGraphRuntimeEnabled()) return NULL;
	return weaponGraphRuntimeGetEntity(asset_id);
}

const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntityForHeldFunction(
	const weapon_graph_held_function_t *held)
{
	if (!weaponGraphRuntimeEnabled() || !held || !held->valid) {
		return NULL;
	}
	if (held->entity_ref[0]) {
		return weaponGraphRuntimeGetEntity(held->entity_ref);
	}
	if (held->payload_ref[0]) {
		return weaponGraphRuntimeGetEntity(held->payload_ref);
	}
	return NULL;
}

const weapon_graph_entity_runtime_t *weaponGraphRuntimeGetEntityForProjectile(
	const weapon_graph_projectile_runtime_t *projectile)
{
	if (!weaponGraphRuntimeEnabled() || !projectile || !projectile->valid ||
			!projectile->entity_ref[0]) {
		return NULL;
	}
	return weaponGraphRuntimeGetEntity(projectile->entity_ref);
}

static s32 graphNodeIndex(const weapon_graph_ir_t *ir, const char *id)
{
	if (!ir || !id || !id[0]) return -1;
	for (s32 i = 0; i < ir->node_count; i++) {
		if (strcmp(ir->nodes[i].id, id) == 0) return i;
	}
	return -1;
}

static s32 graphContextIndex(const weapon_graph_ir_t *ir, const char *name)
{
	if (!ir || !name || !name[0]) return -1;
	for (s32 i = 0; i < ir->context_count; i++) {
		if (strcmp(ir->contexts[i].name, name) == 0) return i;
	}
	return -1;
}

static s32 graphSubgraphIndex(const weapon_graph_ir_t *ir, const char *id)
{
	if (!ir || !id || !id[0]) return -1;
	for (s32 i = 0; i < ir->subgraph_count; i++) {
		if (strcmp(ir->subgraphs[i].id, id) == 0) return i;
	}
	return -1;
}

static s32 keyNeedsUnit(const char *key)
{
	if (!key || !key[0] || startsWith(key, "runtime_")) return 0;
	/* c3849 Unit 1c: "timer_starts" is a policy string (on_spawn/on_impact/
	 * on_attach), not a time value; without this exemption the substring
	 * heuristic below made the documented projectile.timer key
	 * uncompilable. */
	if (strcmp(key, "timer_starts") == 0) return 0;
	if (strstr(key, "timer") || strstr(key, "duration") ||
			strstr(key, "cooldown") || strstr(key, "interval") ||
			strstr(key, "_time") || strstr(key, "recoverytime")) {
		return 1;
	}
	return 0;
}

static s32 keyHasUnit(const char *key)
{
	return strstr(key, "ticks60") || strstr(key, "ticks240") ||
		strstr(key, "centiseconds") || strstr(key, "timer60") ||
		strstr(key, "time60") || endsWith(key, "_ms") ||
		endsWith(key, "_rpm") || strcmp(key, "initial_rpm") == 0 ||
		strcmp(key, "max_rpm") == 0;
}

static s32 paramIsCatalogRefKey(const char *key)
{
	return strcmp(key, "projectile_ref") == 0 ||
		strcmp(key, "entity_ref") == 0 ||
		strcmp(key, "payload_ref") == 0 ||
		strcmp(key, "recover_weapon_ref") == 0 ||
		strcmp(key, "detonator_ref") == 0;
}

static s32 catalogRefLooksValid(const char *value)
{
	const char *colon = strchr(value, ':');
	if (!value || !value[0]) return 0;
	return colon && colon != value && colon[1] != '\0';
}

static void trimRawValue(json_span_t span, char *out, size_t out_cap)
{
	const char *a = span.start;
	const char *b = span.end;
	size_t n;
	while (a < b && isspace((unsigned char)*a)) a++;
	while (b > a && isspace((unsigned char)*(b - 1))) b--;
	n = (size_t)(b - a);
	if (n >= out_cap) n = out_cap - 1;
	if (out_cap > 0) {
		memcpy(out, a, n);
		out[n] = '\0';
	}
}

static s32 readJsonStringSpan(json_span_t span, char *out, size_t out_cap)
{
	const char *p;
	size_t written = 0;
	if (!out || out_cap == 0) return 0;
	out[0] = '\0';
	p = jsonSkipWs(span.start, span.end);
	if (p >= span.end || *p != '"') return 0;
	p++;
	while (p < span.end && *p != '"') {
		char c = *p++;
		if (c == '\\' && p < span.end) c = *p++;
		if (written + 1 < out_cap) out[written++] = c;
	}
	out[written] = '\0';
	return p < span.end && *p == '"';
}

static int cmpParam(const void *a, const void *b)
{
	const weapon_graph_ir_param_t *pa = (const weapon_graph_ir_param_t *)a;
	const weapon_graph_ir_param_t *pb = (const weapon_graph_ir_param_t *)b;
	int k = strcmp(pa->key, pb->key);
	if (k != 0) return k;
	return strcmp(pa->value, pb->value);
}

static const weapon_graph_setting_entry_t *weaponGraphFindVariable(
	const weapon_graph_weapon_settings_t *table, const char *name)
{
	if (!table || !name || !name[0]) return NULL;
	for (s32 i = 0; i < table->variable_count; i++) {
		if (strcmp(table->variables[i].key, name) == 0) {
			return &table->variables[i];
		}
	}
	return NULL;
}

/* c3849 Wave 5f Unit 2 (B6.3): "$name" string params resolve from the active
 * weapon variables table BEFORE the IR is emitted, so the IR digest hashes the
 * resolved typed value and stays deterministic for a given archive. Scope is
 * held weapon graphs only: when no variables table is active
 * (s_compile_variables == NULL -- behavior graphs, bare compile entry points)
 * the "$" string stays literal. An ACTIVE table that lacks the name is a loud
 * compile error like any other validation failure. */
static s32 compileParamSubstituteVariable(weapon_graph_ir_param_t *p,
                                          char *err, size_t err_cap)
{
	const weapon_graph_setting_entry_t *var;

	if (!s_compile_variables) {
		return 0;
	}

	var = weaponGraphFindVariable(s_compile_variables, p->value + 1);
	if (!var) {
		setErr(err, err_cap, "unresolved weapon variable '%s' for param %s",
			p->value, p->key);
		return -1;
	}

	switch (var->type) {
	case WEAPON_GRAPH_PARAM_INT:
		p->type = WEAPON_GRAPH_PARAM_INT;
		p->i_value = var->i_value;
		snprintf(p->value, sizeof(p->value), "%d", var->i_value);
		break;
	case WEAPON_GRAPH_PARAM_FLOAT:
		p->type = WEAPON_GRAPH_PARAM_FLOAT;
		p->f_value = var->f_value;
		snprintf(p->value, sizeof(p->value), "%.9g", (double)var->f_value);
		break;
	case WEAPON_GRAPH_PARAM_BOOL:
		p->type = WEAPON_GRAPH_PARAM_BOOL;
		p->b_value = var->b_value;
		copyStr(p->value, sizeof(p->value), var->b_value ? "true" : "false");
		break;
	case WEAPON_GRAPH_PARAM_STRING:
		p->type = WEAPON_GRAPH_PARAM_STRING;
		copyStr(p->value, sizeof(p->value), var->value);
		break;
	default:
		setErr(err, err_cap, "weapon variable '%s' has unsupported type for param %s",
			p->value, p->key);
		return -1;
	}
	return 0;
}

static s32 compileParam(json_span_t value, char value_type,
                        weapon_graph_ir_param_t *p,
                        char *err, size_t err_cap)
{
	char *endptr = NULL;
	const char *v = jsonSkipWs(value.start, value.end);
	if (!p) return -1;

	if (value_type == '"') {
		p->type = WEAPON_GRAPH_PARAM_STRING;
		if (!readJsonStringSpan(value, p->value, sizeof(p->value))) {
			setErr(err, err_cap, "bad string param %s", p->key);
			return -1;
		}
		/* B6.3: $name resolves from the active variables table before the
		 * catalog-ref shape check, so a substituted ref validates like a
		 * directly-authored one. */
		if (p->value[0] == '$' && p->value[1] &&
				compileParamSubstituteVariable(p, err, err_cap) != 0) {
			return -1;
		}
		if (p->type == WEAPON_GRAPH_PARAM_STRING &&
				paramIsCatalogRefKey(p->key) && p->value[0] &&
				!catalogRefLooksValid(p->value)) {
			setErr(err, err_cap, "%s must be a catalog id, got %s",
				p->key, p->value);
			return -1;
		}
	} else if (value_type == '{') {
		p->type = WEAPON_GRAPH_PARAM_OBJECT;
		trimRawValue(value, p->value, sizeof(p->value));
	} else if (value_type == '[') {
		p->type = WEAPON_GRAPH_PARAM_ARRAY;
		trimRawValue(value, p->value, sizeof(p->value));
	} else if (startsWith(v, "true") || startsWith(v, "false")) {
		p->type = WEAPON_GRAPH_PARAM_BOOL;
		p->b_value = startsWith(v, "true") ? 1 : 0;
		copyStr(p->value, sizeof(p->value), p->b_value ? "true" : "false");
	} else if (startsWith(v, "null")) {
		p->type = WEAPON_GRAPH_PARAM_NULL;
		copyStr(p->value, sizeof(p->value), "null");
	} else {
		double d = strtod(v, &endptr);
		if (endptr == v) {
			setErr(err, err_cap, "unsupported param value for %s", p->key);
			return -1;
		}
		if (memchr(v, '.', (size_t)(endptr - v)) ||
				memchr(v, 'e', (size_t)(endptr - v)) ||
				memchr(v, 'E', (size_t)(endptr - v))) {
			p->type = WEAPON_GRAPH_PARAM_FLOAT;
			p->f_value = (f32)d;
			snprintf(p->value, sizeof(p->value), "%.9g", d);
		} else {
			long i = strtol(v, NULL, 10);
			p->type = WEAPON_GRAPH_PARAM_INT;
			p->i_value = (s32)i;
			snprintf(p->value, sizeof(p->value), "%ld", i);
		}
	}
	return 0;
}

static s32 compileParams(json_span_t node_obj, weapon_graph_ir_t *ir,
                         weapon_graph_ir_node_t *node,
                         char *err, size_t err_cap)
{
	json_span_t params;
	const char *cursor = NULL;
	char key[WEAPON_GRAPH_IR_KEY_LEN];
	json_span_t value;
	char value_type;
	s32 start = ir->param_count;

	node->param_start = start;
	node->param_count = 0;

	if (!jsonObjectObject(node_obj, "params", &params)) return 0;

	while (jsonObjectNextMember(params, &cursor, key, sizeof(key), &value, &value_type)) {
		if (ir->param_count >= WEAPON_GRAPH_IR_MAX_PARAMS) {
			setErr(err, err_cap, "too many graph params");
			return -1;
		}
		if (keyNeedsUnit(key) && !keyHasUnit(key)) {
			setErr(err, err_cap, "time-like param %s needs explicit units", key);
			return -1;
		}
		weapon_graph_ir_param_t *p = &ir->params[ir->param_count];
		memset(p, 0, sizeof(*p));
		copyStr(p->key, sizeof(p->key), key);
		if (compileParam(value, value_type, p, err, err_cap) != 0) {
			return -1;
		}
		ir->param_count++;
		node->param_count++;
	}

	if (node->param_count > 1) {
		qsort(&ir->params[start], (size_t)node->param_count,
			sizeof(ir->params[0]), cmpParam);
	}
	return 0;
}

static s32 compileSharedContexts(json_span_t root, weapon_graph_ir_t *ir,
                                 char *err, size_t err_cap)
{
	json_span_t contexts;
	const char *cursor = NULL;
	json_span_t obj;
	if (!jsonObjectArray(root, "shared_context", &contexts)) return 0;

	while (jsonArrayNextObject(contexts, &cursor, &obj)) {
		if (ir->context_count >= WEAPON_GRAPH_IR_MAX_CONTEXTS) {
			setErr(err, err_cap, "too many graph shared contexts");
			return -1;
		}
		weapon_graph_ir_context_t *ctx = &ir->contexts[ir->context_count];
		memset(ctx, 0, sizeof(*ctx));
		if (!jsonObjectString(obj, "name", ctx->name, sizeof(ctx->name)) ||
				!ctx->name[0] ||
				!jsonObjectString(obj, "scope", ctx->scope, sizeof(ctx->scope)) ||
				!ctx->scope[0] ||
				!jsonObjectString(obj, "source", ctx->source, sizeof(ctx->source)) ||
				!ctx->source[0]) {
			setErr(err, err_cap, "shared context missing name/scope/source");
			return -1;
		}
		if (graphContextIndex(ir, ctx->name) >= 0) {
			setErr(err, err_cap, "duplicate shared context %s", ctx->name);
			return -1;
		}
		jsonObjectString(obj, "type", ctx->type, sizeof(ctx->type));
		jsonObjectString(obj, "lifetime", ctx->lifetime, sizeof(ctx->lifetime));
		ir->context_count++;
	}
	return 0;
}

static s32 compileNodes(asset_type_e graph_type, json_span_t root,
                        weapon_graph_ir_t *ir, char *err, size_t err_cap)
{
	json_span_t nodes;
	const char *cursor = NULL;
	json_span_t obj;
	if (!jsonObjectArray(root, "nodes", &nodes)) {
		setErr(err, err_cap, "graph missing nodes array");
		return -1;
	}

	while (jsonArrayNextObject(nodes, &cursor, &obj)) {
		if (ir->node_count >= WEAPON_GRAPH_IR_MAX_NODES) {
			setErr(err, err_cap, "too many graph nodes");
			return -1;
		}
		weapon_graph_ir_node_t *node = &ir->nodes[ir->node_count];
		memset(node, 0, sizeof(*node));
		if (!jsonObjectString(obj, "id", node->id, sizeof(node->id)) ||
				!node->id[0]) {
			setErr(err, err_cap, "graph node missing id");
			return -1;
		}
		if (graphNodeIndex(ir, node->id) >= 0) {
			setErr(err, err_cap, "duplicate graph node id %s", node->id);
			return -1;
		}
		if (!jsonObjectString(obj, "kind", node->kind, sizeof(node->kind)) ||
				!node->kind[0]) {
			setErr(err, err_cap, "graph node %s missing kind", node->id);
			return -1;
		}
		jsonObjectString(obj, "subgraph", node->subgraph, sizeof(node->subgraph));
		node->opcode = weaponGraphOpcodeForKind(graph_type, node->kind);
		if (node->opcode == WEAPON_GRAPH_OP_INVALID) {
			setErr(err, err_cap, "unsupported %s graph module %s",
				weaponGraphArchiveTypeName(graph_type), node->kind);
			return -1;
		}
		if (compileParams(obj, ir, node, err, err_cap) != 0) return -1;
		ir->node_count++;
	}

	if (ir->node_count == 0) {
		setErr(err, err_cap, "graph has no nodes");
		return -1;
	}
	return 0;
}

static s32 compileSubgraphs(json_span_t root, weapon_graph_ir_t *ir,
                            char *err, size_t err_cap)
{
	json_span_t subgraphs;
	const char *cursor = NULL;
	json_span_t obj;
	if (!jsonObjectArray(root, "subgraphs", &subgraphs)) return 0;

	while (jsonArrayNextObject(subgraphs, &cursor, &obj)) {
		if (ir->subgraph_count >= WEAPON_GRAPH_IR_MAX_SUBGRAPHS) {
			setErr(err, err_cap, "too many graph subgraphs");
			return -1;
		}
		weapon_graph_ir_subgraph_t *subgraph =
			&ir->subgraphs[ir->subgraph_count];
		memset(subgraph, 0, sizeof(*subgraph));
		subgraph->entry_node = -1;
		if (!jsonObjectString(obj, "id", subgraph->id, sizeof(subgraph->id)) ||
				!subgraph->id[0]) {
			setErr(err, err_cap, "subgraph missing id");
			return -1;
		}
		if (graphSubgraphIndex(ir, subgraph->id) >= 0) {
			setErr(err, err_cap, "duplicate subgraph %s", subgraph->id);
			return -1;
		}
		jsonObjectString(obj, "entry", subgraph->entry, sizeof(subgraph->entry));
		if (subgraph->entry[0]) {
			subgraph->entry_node = graphNodeIndex(ir, subgraph->entry);
			if (subgraph->entry_node < 0) {
				setErr(err, err_cap, "subgraph %s references missing entry %s",
					subgraph->id, subgraph->entry);
				return -1;
			}
		}
		ir->subgraph_count++;
	}
	return 0;
}

static s32 compileEdges(json_span_t root, weapon_graph_ir_t *ir,
                        char *err, size_t err_cap)
{
	json_span_t edges;
	const char *cursor = NULL;
	json_span_t obj;
	if (!jsonObjectArray(root, "edges", &edges)) return 0;

	while (jsonArrayNextObject(edges, &cursor, &obj)) {
		char from_id[WEAPON_GRAPH_IR_ID_LEN];
		char to_id[WEAPON_GRAPH_IR_ID_LEN];
		s32 from;
		s32 to;
		if (ir->edge_count >= WEAPON_GRAPH_IR_MAX_EDGES) {
			setErr(err, err_cap, "too many graph edges");
			return -1;
		}
		if (!jsonObjectString(obj, "from", from_id, sizeof(from_id)) ||
				!jsonObjectString(obj, "to", to_id, sizeof(to_id))) {
			setErr(err, err_cap, "edge missing from/to");
			return -1;
		}
		from = graphNodeIndex(ir, from_id);
		to = graphNodeIndex(ir, to_id);
		if (from < 0 || to < 0) {
			setErr(err, err_cap, "edge references missing node %s -> %s",
				from_id, to_id);
			return -1;
		}
		ir->edges[ir->edge_count].from = from;
		ir->edges[ir->edge_count].to = to;
		ir->edge_count++;
	}
	return 0;
}

static s32 visitCycle(const weapon_graph_ir_t *ir, s32 node,
                      u8 *visiting, u8 *visited)
{
	if (visiting[node]) return 1;
	if (visited[node]) return 0;
	visiting[node] = 1;
	for (s32 i = 0; i < ir->edge_count; i++) {
		if (ir->edges[i].from == node &&
				visitCycle(ir, ir->edges[i].to, visiting, visited)) {
			return 1;
		}
	}
	visiting[node] = 0;
	visited[node] = 1;
	return 0;
}

static s32 graphHasCycle(const weapon_graph_ir_t *ir)
{
	u8 visiting[WEAPON_GRAPH_IR_MAX_NODES];
	u8 visited[WEAPON_GRAPH_IR_MAX_NODES];
	memset(visiting, 0, sizeof(visiting));
	memset(visited, 0, sizeof(visited));
	for (s32 i = 0; i < ir->node_count; i++) {
		if (visitCycle(ir, i, visiting, visited)) return 1;
	}
	return 0;
}

static s32 compileExports(json_span_t root, weapon_graph_ir_t *ir,
                          char *err, size_t err_cap)
{
	json_span_t exports;
	const char *cursor = NULL;
	json_span_t obj;
	if (!jsonObjectArray(root, "exports", &exports)) return 0;

	while (jsonArrayNextObject(exports, &cursor, &obj)) {
		char node_id[WEAPON_GRAPH_IR_ID_LEN];
		if (ir->export_count >= WEAPON_GRAPH_IR_MAX_EXPORTS) {
			setErr(err, err_cap, "too many graph exports");
			return -1;
		}
		weapon_graph_ir_export_t *ex = &ir->exports[ir->export_count];
		memset(ex, 0, sizeof(*ex));
		if (!jsonObjectString(obj, "name", ex->name, sizeof(ex->name)) ||
				!jsonObjectString(obj, "node", node_id, sizeof(node_id))) {
			setErr(err, err_cap, "export missing name/node");
			return -1;
		}
		ex->node = graphNodeIndex(ir, node_id);
		if (ex->node < 0) {
			setErr(err, err_cap, "export %s references missing node %s",
				ex->name, node_id);
			return -1;
		}
		ir->export_count++;
	}
	return 0;
}

static void hashS32(sha256_ctx *ctx, s32 value)
{
	u8 bytes[4];
	bytes[0] = (u8)(value & 0xff);
	bytes[1] = (u8)((value >> 8) & 0xff);
	bytes[2] = (u8)((value >> 16) & 0xff);
	bytes[3] = (u8)((value >> 24) & 0xff);
	sha256Update(ctx, bytes, sizeof(bytes));
}

static void hashString(sha256_ctx *ctx, const char *value)
{
	static const u8 zero = 0;
	if (!value) value = "";
	sha256Update(ctx, value, strlen(value));
	sha256Update(ctx, &zero, 1);
}

static void finalizeIrDigest(weapon_graph_ir_t *ir)
{
	sha256_ctx ctx;
	u8 digest[SHA256_DIGEST_SIZE];
	sha256Init(&ctx);
	hashString(&ctx, "pd.weapon_graph.ir.v1");
	hashS32(&ctx, (s32)ir->asset_type);
	hashString(&ctx, ir->schema);
	hashString(&ctx, ir->asset_id);
	hashString(&ctx, ir->graph_id);
	hashS32(&ctx, ir->context_count);
	for (s32 i = 0; i < ir->context_count; i++) {
		const weapon_graph_ir_context_t *c = &ir->contexts[i];
		hashString(&ctx, c->name);
		hashString(&ctx, c->scope);
		hashString(&ctx, c->source);
		hashString(&ctx, c->type);
		hashString(&ctx, c->lifetime);
	}
	hashS32(&ctx, ir->node_count);
	for (s32 i = 0; i < ir->node_count; i++) {
		const weapon_graph_ir_node_t *n = &ir->nodes[i];
		hashString(&ctx, n->id);
		hashString(&ctx, n->kind);
		hashString(&ctx, n->subgraph);
		hashS32(&ctx, (s32)n->opcode);
		hashS32(&ctx, n->param_count);
		for (s32 j = 0; j < n->param_count; j++) {
			const weapon_graph_ir_param_t *p =
				&ir->params[n->param_start + j];
			hashString(&ctx, p->key);
			hashS32(&ctx, (s32)p->type);
			hashString(&ctx, p->value);
		}
	}
	hashS32(&ctx, ir->edge_count);
	for (s32 i = 0; i < ir->edge_count; i++) {
		hashS32(&ctx, ir->edges[i].from);
		hashS32(&ctx, ir->edges[i].to);
	}
	hashS32(&ctx, ir->export_count);
	for (s32 i = 0; i < ir->export_count; i++) {
		hashString(&ctx, ir->exports[i].name);
		hashS32(&ctx, ir->exports[i].node);
	}
	hashS32(&ctx, ir->subgraph_count);
	for (s32 i = 0; i < ir->subgraph_count; i++) {
		hashString(&ctx, ir->subgraphs[i].id);
		hashString(&ctx, ir->subgraphs[i].entry);
		hashS32(&ctx, ir->subgraphs[i].entry_node);
	}
	sha256Final(&ctx, digest);
	sha256ToHex(digest, ir->ir_sha256);
}

s32 weaponGraphCompileJson(asset_type_e graph_type, const char *json,
                           u32 json_size, weapon_graph_ir_t *out,
                           char *err, size_t err_cap)
{
	const char *schema_expected = weaponGraphSchemaForType(graph_type);
	json_span_t root;
	u8 digest[SHA256_DIGEST_SIZE];
	if (err && err_cap) err[0] = '\0';
	if (!schema_expected) {
		setErr(err, err_cap, "unsupported graph asset type %d", (s32)graph_type);
		return -1;
	}
	if (!json || json_size == 0 || !out) {
		setErr(err, err_cap, "compile called with empty graph input");
		return -1;
	}
	if (!jsonReadObject(json, json + json_size, &root)) {
		setErr(err, err_cap, "graph root must be a JSON object");
		return -1;
	}

	memset(out, 0, sizeof(*out));
	out->asset_type = graph_type;
	if (!jsonObjectString(root, "schema", out->schema, sizeof(out->schema)) ||
			strcmp(out->schema, schema_expected) != 0) {
		/* c3849 Unit 8 (B6.4): stale base .pdeffect archives persist the
		 * legacy schema spelling (the s_emitEffect early-out checks entry
		 * presence only); accept it with a one-time deprecation warning. */
		if (graph_type == ASSET_EFFECT &&
				strcmp(out->schema, "pd2.effect.graph.v1") == 0) {
			static s32 s_warned_legacy_effect_schema = 0;
			if (!s_warned_legacy_effect_schema) {
				s_warned_legacy_effect_schema = 1;
				sysLogPrintf(LOG_WARNING,
					"EFFECTGRAPH.PARSE: schema 'pd2.effect.graph.v1' is deprecated; use 'pd.effect_graph.v1'");
			}
		} else {
			setErr(err, err_cap, "graph schema must be %s", schema_expected);
			return -1;
		}
	}
	jsonObjectString(root, "asset_id", out->asset_id, sizeof(out->asset_id));
	if (!out->asset_id[0] && graph_type == ASSET_EFFECT) {
		/* Base s_emitEffect writes the catalog_id spelling. */
		jsonObjectString(root, "catalog_id", out->asset_id, sizeof(out->asset_id));
	}
	jsonObjectString(root, "graph_id", out->graph_id, sizeof(out->graph_id));
	if (!out->graph_id[0]) {
		if (graph_type == ASSET_EFFECT) {
			/* Neither the base emitter nor the needler proving asset authors
			 * a graph_id for effect graphs; default deterministically. */
			copyStr(out->graph_id, sizeof(out->graph_id), "effect_graph");
		} else {
			setErr(err, err_cap, "graph missing graph_id");
			return -1;
		}
	}
	if (out->asset_id[0] && !catalogRefLooksValid(out->asset_id)) {
		setErr(err, err_cap, "asset_id must be a catalog id, got %s",
			out->asset_id);
		return -1;
	}

	sha256Hash(json, json_size, digest);
	sha256ToHex(digest, out->source_sha256);

	if (compileSharedContexts(root, out, err, err_cap) != 0) return -1;
	if (compileNodes(graph_type, root, out, err, err_cap) != 0) return -1;
	if (compileSubgraphs(root, out, err, err_cap) != 0) return -1;
	if (compileEdges(root, out, err, err_cap) != 0) return -1;
	if (graphHasCycle(out)) {
		setErr(err, err_cap, "graph contains a cycle");
		return -1;
	}
	if (compileExports(root, out, err, err_cap) != 0) return -1;
	finalizeIrDigest(out);
	return 0;
}

s32 weaponGraphValidateJson(asset_type_e graph_type, const char *json,
                            u32 json_size, char *err, size_t err_cap)
{
	weapon_graph_ir_t ir;
	return weaponGraphCompileJson(graph_type, json, json_size, &ir,
		err, err_cap);
}

typedef struct graph_source_builder {
	char *buf;
	size_t len;
	size_t cap;
} graph_source_builder_t;

static s32 builderAppend(graph_source_builder_t *b, const char *text)
{
	size_t n;
	if (!b || !text) return -1;
	n = strlen(text);
	if (b->len + n + 1 > b->cap) return -1;
	memcpy(b->buf + b->len, text, n);
	b->len += n;
	b->buf[b->len] = '\0';
	return 0;
}

static s32 builderAppendFmt(graph_source_builder_t *b, const char *fmt, ...)
{
	va_list ap;
	int n;
	if (!b || !fmt || b->len >= b->cap) return -1;
	va_start(ap, fmt);
	n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= b->cap - b->len) return -1;
	b->len += (size_t)n;
	return 0;
}

static s32 spanHasJsonContent(json_span_t span)
{
	const char *p = jsonSkipWs(span.start, span.end);
	return p < span.end;
}

static s32 builderAppendSpan(graph_source_builder_t *b, json_span_t span)
{
	size_t n;
	if (!b || !span.start || !span.end || span.end < span.start) return -1;
	n = (size_t)(span.end - span.start);
	if (b->len + n + 1 > b->cap) return -1;
	memcpy(b->buf + b->len, span.start, n);
	b->len += n;
	b->buf[b->len] = '\0';
	return 0;
}

static s32 builderAppendArrayMembers(graph_source_builder_t *b,
                                     const char *json, u32 json_size,
                                     const char *key, s32 required,
                                     s32 *wrote_any,
                                     char *err, size_t err_cap)
{
	json_span_t root;
	json_span_t array;
	if (!json || json_size == 0 ||
			!jsonReadObject(json, json + json_size, &root)) {
		setErr(err, err_cap, "source graph is not a JSON object");
		return -1;
	}
	if (!jsonObjectArray(root, key, &array) || !spanHasJsonContent(array)) {
		if (required) {
			setErr(err, err_cap, "source graph missing non-empty %s array", key);
			return -1;
		}
		return 0;
	}
	if (wrote_any && *wrote_any) {
		if (builderAppend(b, ",\n") != 0) return -1;
	}
	if (builderAppendSpan(b, array) != 0) return -1;
	if (wrote_any) *wrote_any = 1;
	return 0;
}

static s32 builderAppendSharedContexts(graph_source_builder_t *b,
                                       const char *json, u32 json_size,
                                       s32 *wrote_any,
                                       char *err, size_t err_cap)
{
	json_span_t root;
	json_span_t contexts;
	if (!json || json_size == 0) return 0;
	if (!jsonReadObject(json, json + json_size, &root)) {
		setErr(err, err_cap, "shared context source is not a JSON object");
		return -1;
	}
	if (!jsonObjectArray(root, "contexts", &contexts) ||
			!spanHasJsonContent(contexts)) {
		return 0;
	}
	if (wrote_any && *wrote_any) {
		if (builderAppend(b, ",\n") != 0) return -1;
	}
	if (builderAppendSpan(b, contexts) != 0) return -1;
	if (wrote_any) *wrote_any = 1;
	return 0;
}

static s32 composeWeaponGraphFromAuthoringFiles(
	const char *archive_path,
	const weapon_graph_archive_descriptor_t *desc,
	char **out_graph,
	u32 *out_graph_size,
	char *err,
	size_t err_cap)
{
	char *primary = NULL;
	char *secondary = NULL;
	char *shared = NULL;
	u32 primary_size = 0;
	u32 secondary_size = 0;
	u32 shared_size = 0;
	size_t cap;
	graph_source_builder_t b;
	s32 wrote;

	if (!archive_path || !desc || !out_graph || !desc->primary_graph[0] ||
			!desc->secondary_graph[0]) {
		setErr(err, err_cap, "weapon archive missing primary/secondary graph sources");
		return -1;
	}
	*out_graph = NULL;
	if (out_graph_size) *out_graph_size = 0;

	if (weaponGraphArchiveReadTextFile(archive_path, desc->primary_graph,
			&primary, &primary_size) != 0 ||
			weaponGraphArchiveReadTextFile(archive_path, desc->secondary_graph,
			&secondary, &secondary_size) != 0) {
		setErr(err, err_cap, "%s missing primary/secondary graph source",
			archive_path);
		free(primary);
		free(secondary);
		return -1;
	}
	if (desc->shared_context[0]) {
		(void)weaponGraphArchiveReadTextFile(archive_path, desc->shared_context,
			&shared, &shared_size);
	}

	cap = (size_t)primary_size + (size_t)secondary_size +
		(size_t)shared_size + 2048u;
	b.buf = (char *)malloc(cap);
	b.len = 0;
	b.cap = cap;
	if (!b.buf) {
		free(primary);
		free(secondary);
		free(shared);
		setErr(err, err_cap, "out of memory composing weapon graph source");
		return -1;
	}
	b.buf[0] = '\0';

	if (builderAppendFmt(&b,
			"{\n"
			"  \"schema\": \"pd.weapon_graph.v1\",\n"
			"  \"asset_id\": \"%s\",\n"
			"  \"graph_id\": \"source_composed\",\n",
			desc->catalog_id) != 0) goto overflow;

	if (builderAppend(&b, "  \"shared_context\": [\n") != 0) goto overflow;
	wrote = 0;
	if (shared && builderAppendSharedContexts(&b, shared, shared_size,
			&wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ],\n") != 0) goto overflow;

	if (builderAppend(&b, "  \"nodes\": [\n") != 0) goto overflow;
	wrote = 0;
	if (builderAppendArrayMembers(&b, primary, primary_size, "nodes", 1,
			&wrote, err, err_cap) != 0 ||
			builderAppendArrayMembers(&b, secondary, secondary_size, "nodes", 1,
			&wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ],\n") != 0) goto overflow;

	if (builderAppend(&b, "  \"edges\": [\n") != 0) goto overflow;
	wrote = 0;
	if (builderAppendArrayMembers(&b, primary, primary_size, "edges", 0,
			&wrote, err, err_cap) != 0 ||
			builderAppendArrayMembers(&b, secondary, secondary_size, "edges", 0,
			&wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ],\n") != 0) goto overflow;

	if (builderAppend(&b, "  \"exports\": [\n") != 0) goto overflow;
	wrote = 0;
	if (builderAppendArrayMembers(&b, primary, primary_size, "exports", 1,
			&wrote, err, err_cap) != 0 ||
			builderAppendArrayMembers(&b, secondary, secondary_size, "exports", 1,
			&wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ]\n}\n") != 0) goto overflow;

	free(primary);
	free(secondary);
	free(shared);
	*out_graph = b.buf;
	if (out_graph_size) *out_graph_size = (u32)b.len;
	return 0;

overflow:
	setErr(err, err_cap, "weapon graph source composition overflow");
fail:
	free(primary);
	free(secondary);
	free(shared);
	free(b.buf);
	return -1;
}

s32 weaponGraphCompileWeaponSourceJson(const char *asset_id,
                                       const char *primary_json,
                                       u32 primary_size,
                                       const char *secondary_json,
                                       u32 secondary_size,
                                       const char *shared_json,
                                       u32 shared_size,
                                       weapon_graph_ir_t *out,
                                       char *err,
                                       size_t err_cap)
{
	size_t cap;
	graph_source_builder_t b;
	s32 wrote;
	s32 result;

	if (!out || !primary_json || primary_size == 0 ||
			!secondary_json || secondary_size == 0) {
		setErr(err, err_cap, "weapon source graph compile requires primary and secondary graph JSON");
		return -1;
	}

	cap = (size_t)primary_size + (size_t)secondary_size +
		(size_t)shared_size + 2048u;
	b.buf = (char *)malloc(cap);
	b.len = 0;
	b.cap = cap;
	if (!b.buf) {
		setErr(err, err_cap, "out of memory composing weapon graph source");
		return -1;
	}
	b.buf[0] = '\0';

	if (builderAppendFmt(&b,
			"{\n"
			"  \"schema\": \"pd.weapon_graph.v1\",\n"
			"  \"asset_id\": \"%s\",\n"
			"  \"graph_id\": \"source_composed\",\n",
			asset_id ? asset_id : "") != 0) goto overflow;

	if (builderAppend(&b, "  \"shared_context\": [\n") != 0) goto overflow;
	wrote = 0;
	if (shared_json && shared_size > 0 &&
			builderAppendSharedContexts(&b, shared_json, shared_size,
			&wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ],\n") != 0) goto overflow;

	if (builderAppend(&b, "  \"nodes\": [\n") != 0) goto overflow;
	wrote = 0;
	if (builderAppendArrayMembers(&b, primary_json, primary_size, "nodes", 1,
			&wrote, err, err_cap) != 0 ||
			builderAppendArrayMembers(&b, secondary_json, secondary_size,
			"nodes", 1, &wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ],\n") != 0) goto overflow;

	if (builderAppend(&b, "  \"edges\": [\n") != 0) goto overflow;
	wrote = 0;
	if (builderAppendArrayMembers(&b, primary_json, primary_size, "edges", 0,
			&wrote, err, err_cap) != 0 ||
			builderAppendArrayMembers(&b, secondary_json, secondary_size,
			"edges", 0, &wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ],\n") != 0) goto overflow;

	if (builderAppend(&b, "  \"exports\": [\n") != 0) goto overflow;
	wrote = 0;
	if (builderAppendArrayMembers(&b, primary_json, primary_size, "exports", 1,
			&wrote, err, err_cap) != 0 ||
			builderAppendArrayMembers(&b, secondary_json, secondary_size,
			"exports", 1, &wrote, err, err_cap) != 0) goto fail;
	if (builderAppend(&b, "\n  ]\n}\n") != 0) goto overflow;

	result = weaponGraphCompileJson(ASSET_WEAPON, b.buf, (u32)b.len, out,
		err, err_cap);
	free(b.buf);
	return result;

overflow:
	setErr(err, err_cap, "weapon graph source composition overflow");
fail:
	free(b.buf);
	return -1;
}

/* ---------------------------------------------------------------------------
 * c3849 Wave 5f Unit 2: settings.json / variables.json / presentation.json
 * parsers. Both schema spellings are accepted (canonical pd.weapon_settings.v1
 * / pd.weapon_variables.v1 per B6.4; the needler pd.weapon.settings.v1 /
 * pd.weapon.variables.v1 spellings warn once) and both shapes (the base
 * "variables": [] array of {name,value,...} rows and the needler flat object
 * keys, where a member value may be a scalar or a {value, unit} object).
 * ------------------------------------------------------------------------- */

static s32 weaponGraphSettingScalar(json_span_t value, char value_type,
                                    weapon_graph_setting_entry_t *e,
                                    char *err, size_t err_cap)
{
	char *endptr = NULL;
	const char *v = jsonSkipWs(value.start, value.end);

	if (value_type == '"') {
		e->type = WEAPON_GRAPH_PARAM_STRING;
		if (!readJsonStringSpan(value, e->value, sizeof(e->value))) {
			setErr(err, err_cap, "bad string value for tunable %s", e->key);
			return -1;
		}
	} else if (startsWith(v, "true") || startsWith(v, "false")) {
		e->type = WEAPON_GRAPH_PARAM_BOOL;
		e->b_value = startsWith(v, "true") ? 1 : 0;
		copyStr(e->value, sizeof(e->value), e->b_value ? "true" : "false");
	} else if (startsWith(v, "null")) {
		e->type = WEAPON_GRAPH_PARAM_NULL;
		copyStr(e->value, sizeof(e->value), "null");
	} else {
		double d = strtod(v, &endptr);
		if (endptr == v) {
			setErr(err, err_cap, "unsupported value for tunable %s", e->key);
			return -1;
		}
		if (memchr(v, '.', (size_t)(endptr - v)) ||
				memchr(v, 'e', (size_t)(endptr - v)) ||
				memchr(v, 'E', (size_t)(endptr - v))) {
			e->type = WEAPON_GRAPH_PARAM_FLOAT;
			e->f_value = (f32)d;
			e->i_value = (s32)d;
			snprintf(e->value, sizeof(e->value), "%.9g", d);
		} else {
			long i = strtol(v, NULL, 10);
			e->type = WEAPON_GRAPH_PARAM_INT;
			e->i_value = (s32)i;
			e->f_value = (f32)i;
			snprintf(e->value, sizeof(e->value), "%ld", i);
		}
	}
	return 0;
}

static s32 weaponGraphTunableKeyReserved(const char *key)
{
	return strcmp(key, "schema") == 0 ||
		strcmp(key, "asset_id") == 0 ||
		strcmp(key, "catalog_id") == 0 ||
		strcmp(key, "graph_id") == 0 ||
		strcmp(key, "dependency_closure") == 0;
}

/* The fixed defaults-layer key map (Unit 9). Settings keys outside it are
 * stored but noted: nothing consumes them yet. */
static s32 weaponGraphSettingKeyKnown(const char *key)
{
	return strcmp(key, "fire_cadence") == 0 ||
		strcmp(key, "spread") == 0 ||
		strcmp(key, "zoom_fov") == 0 ||
		strcmp(key, "sight") == 0;
}

static s32 weaponGraphTunableAppend(const char *label, s32 is_variables,
                                    const char *key,
                                    json_span_t value, char value_type,
                                    weapon_graph_setting_entry_t *out,
                                    s32 cap, s32 *count,
                                    char *err, size_t err_cap)
{
	weapon_graph_setting_entry_t *e;

	if (*count >= cap) {
		setErr(err, err_cap, "too many weapon %s entries (max %d)", label, cap);
		return -1;
	}
	e = &out[*count];
	memset(e, 0, sizeof(*e));
	copyStr(e->key, sizeof(e->key), key);

	if (value_type == '{') {
		/* {value, unit} object form (needler fire_cadence shape). */
		json_span_t obj;
		json_span_t member;
		char member_key[WEAPON_GRAPH_IR_KEY_LEN];
		char member_type;
		const char *cursor = NULL;
		s32 saw_value = 0;
		if (!jsonReadObject(value.start, value.end, &obj)) {
			setErr(err, err_cap, "bad object value for tunable %s", key);
			return -1;
		}
		while (jsonObjectNextMember(obj, &cursor, member_key,
				sizeof(member_key), &member, &member_type)) {
			if (strcmp(member_key, "value") == 0) {
				if (weaponGraphSettingScalar(member, member_type, e,
						err, err_cap) != 0) {
					return -1;
				}
				saw_value = 1;
			} else if (strcmp(member_key, "unit") == 0) {
				readJsonStringSpan(member, e->unit, sizeof(e->unit));
			}
		}
		if (!saw_value) {
			setErr(err, err_cap, "tunable %s object form requires a value member",
				key);
			return -1;
		}
	} else if (value_type == '[') {
		setErr(err, err_cap, "tunable %s must be a scalar or {value, unit} object",
			key);
		return -1;
	} else if (weaponGraphSettingScalar(value, value_type, e,
			err, err_cap) != 0) {
		return -1;
	}

	/* Explicit-unit enforcement on time-like keys, mirroring the graph
	 * param validator (keyNeedsUnit/keyHasUnit). The unit row satisfies it. */
	if (keyNeedsUnit(e->key) && !keyHasUnit(e->key) && !e->unit[0]) {
		setErr(err, err_cap, "time-like %s key %s needs explicit units",
			label, e->key);
		return -1;
	}

	if (!is_variables && !weaponGraphSettingKeyKnown(e->key)) {
		setErr(err, err_cap,
			"weapon %s key %s has no production consumer",
			label, e->key);
		return -1;
	}

	(*count)++;
	return 0;
}

static s32 weaponGraphTunablesSchemaOk(json_span_t root, s32 is_variables,
                                       char *err, size_t err_cap)
{
	static s32 s_warned_legacy_settings_schema = 0;
	static s32 s_warned_legacy_variables_schema = 0;
	char schema[64];
	const char *canonical = is_variables ?
		"pd.weapon_variables.v1" : "pd.weapon_settings.v1";
	const char *legacy = is_variables ?
		"pd.weapon.variables.v1" : "pd.weapon.settings.v1";

	schema[0] = '\0';
	jsonObjectString(root, "schema", schema, sizeof(schema));
	if (strcmp(schema, canonical) == 0) {
		return 0;
	}
	if (strcmp(schema, legacy) == 0) {
		s32 *warned = is_variables ?
			&s_warned_legacy_variables_schema : &s_warned_legacy_settings_schema;
		if (!*warned) {
			*warned = 1;
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.SETTINGS: schema '%s' is deprecated; use '%s' (B6.4)",
				legacy, canonical);
		}
		return 0;
	}
	setErr(err, err_cap, "weapon %s schema must be %s",
		is_variables ? "variables" : "settings", canonical);
	return -1;
}

static s32 weaponGraphParseTunablesJson(const char *json, u32 json_size,
                                        s32 is_variables,
                                        weapon_graph_setting_entry_t *out,
                                        s32 cap, s32 *count,
                                        char *err, size_t err_cap)
{
	json_span_t root;
	json_span_t rows;
	const char *label = is_variables ? "variables" : "settings";
	const char *cursor = NULL;
	char key[WEAPON_GRAPH_IR_KEY_LEN];
	json_span_t value;
	char value_type;

	*count = 0;
	if (!json || json_size == 0) {
		return 0;
	}
	if (!jsonReadObject(json, json + json_size, &root)) {
		setErr(err, err_cap, "weapon %s source is not a JSON object", label);
		return -1;
	}
	if (weaponGraphTunablesSchemaOk(root, is_variables, err, err_cap) != 0) {
		return -1;
	}

	/* Shape 1: "settings"/"variables" array of {name|key, value, unit} rows
	 * (the base emitter's variables shape). */
	if (jsonObjectArray(root, label, &rows)) {
		json_span_t row;
		while (jsonArrayNextObject(rows, &cursor, &row)) {
			char name[WEAPON_GRAPH_IR_KEY_LEN];
			const char *value_start;
			name[0] = '\0';
			if (!jsonObjectString(row, "name", name, sizeof(name)) &&
					!jsonObjectString(row, "key", name, sizeof(name))) {
				setErr(err, err_cap, "weapon %s row missing name", label);
				return -1;
			}
			value_start = jsonFindKeyInObject(row, "value");
			if (!value_start) {
				setErr(err, err_cap, "weapon %s row %s missing value",
					label, name);
				return -1;
			}
			value.start = jsonSkipWs(value_start, row.end);
			value.end = jsonValueEnd(value.start, row.end);
			value_type = value.start < row.end ? *value.start : '\0';
			if (weaponGraphTunableAppend(label, is_variables, name,
					value, value_type, out, cap, count, err, err_cap) != 0) {
				return -1;
			}
			/* Row-level unit overrides nothing if absent: the append above
			 * captured a {value,unit} object when authored that way. */
			if (*count > 0 && !out[*count - 1].unit[0]) {
				jsonObjectString(row, "unit", out[*count - 1].unit,
					sizeof(out[*count - 1].unit));
			}
		}
	}

	/* Shape 2: flat object keys (the needler shape). Reserved envelope keys
	 * and the array member itself are skipped. */
	cursor = NULL;
	while (jsonObjectNextMember(root, &cursor, key, sizeof(key),
			&value, &value_type)) {
		if (weaponGraphTunableKeyReserved(key) || strcmp(key, label) == 0) {
			continue;
		}
		if (weaponGraphTunableAppend(label, is_variables, key,
				value, value_type, out, cap, count, err, err_cap) != 0) {
			return -1;
		}
	}

	return 0;
}

/* presentation.json fold-in: sight / zoom_fov land in the SAME defaults layer
 * as settings.json rows (settings.json wins on key collision). The v1 schema
 * intentionally exposes only production-consumed fields. "crosshair" may be
 * "default" as an explicit declaration of the native renderer; custom
 * reticles and unknown keys fail registration instead of being stored or
 * ignored. */
static s32 weaponGraphParsePresentationJson(const char *json, u32 json_size,
                                            weapon_graph_weapon_settings_t *table,
                                            char *err, size_t err_cap)
{
	json_span_t root;
	const char *cursor = NULL;
	char key[WEAPON_GRAPH_IR_KEY_LEN];
	json_span_t value;
	char value_type;

	if (!json || json_size == 0) {
		return 0;
	}
	if (!jsonReadObject(json, json + json_size, &root)) {
		setErr(err, err_cap, "weapon presentation source is not a JSON object");
		return -1;
	}
	{
		char schema[64];
		schema[0] = '\0';
		jsonObjectString(root, "schema", schema, sizeof(schema));
		if (strcmp(schema, "pd.weapon.presentation.v1") != 0) {
			setErr(err, err_cap,
				"weapon presentation schema must be pd.weapon.presentation.v1");
			return -1;
		}
	}

	while (jsonObjectNextMember(root, &cursor, key, sizeof(key),
			&value, &value_type)) {
		if (weaponGraphTunableKeyReserved(key)) {
			continue;
		}
		if (strcmp(key, "crosshair") == 0) {
			char crosshair[64];
			crosshair[0] = '\0';
			if (!readJsonStringSpan(value, crosshair, sizeof(crosshair)) ||
					strcmp(crosshair, "default") != 0) {
				setErr(err, err_cap,
					"weapon presentation crosshair must be default until catalog reticle rendering is implemented");
				return -1;
			}
			continue;
		}
		if (strcmp(key, "sight") == 0 || strcmp(key, "zoom_fov") == 0) {
			s32 already = 0;
			for (s32 i = 0; i < table->setting_count; i++) {
				if (strcmp(table->settings[i].key, key) == 0) {
					already = 1;
					break;
				}
			}
			if (already) {
				continue;
			}
			if (weaponGraphTunableAppend("presentation", 0, key, value,
					value_type, table->settings, WEAPON_GRAPH_SETTINGS_MAX,
					&table->setting_count, err, err_cap) != 0) {
				return -1;
			}
			continue;
		}
		setErr(err, err_cap,
			"weapon presentation key %s has no production consumer", key);
		return -1;
	}

	return 0;
}

static s32 weaponGraphParseWeaponTunables(const char *settings_json,
                                          u32 settings_size,
                                          const char *variables_json,
                                          u32 variables_size,
                                          const char *presentation_json,
                                          u32 presentation_size,
                                          weapon_graph_weapon_settings_t *out,
                                          char *err, size_t err_cap)
{
	memset(out, 0, sizeof(*out));
	if (weaponGraphParseTunablesJson(settings_json, settings_size, 0,
			out->settings, WEAPON_GRAPH_SETTINGS_MAX, &out->setting_count,
			err, err_cap) != 0) {
		return -1;
	}
	if (weaponGraphParseTunablesJson(variables_json, variables_size, 1,
			out->variables, WEAPON_GRAPH_SETTINGS_MAX, &out->variable_count,
			err, err_cap) != 0) {
		return -1;
	}
	if (weaponGraphParsePresentationJson(presentation_json, presentation_size,
			out, err, err_cap) != 0) {
		return -1;
	}
	if ((settings_json && settings_size) || (variables_json && variables_size) ||
			(presentation_json && presentation_size)) {
		out->valid = 1;
	}
	return 0;
}

/* Archive-side tunables read. A descriptor entry that names a member which
 * cannot be read is a loud failure (the descriptor promised it); an absent
 * descriptor entry is a silent skip (legacy/test archives). */
static s32 weaponGraphArchiveReadTunables(
	const char *archive_path,
	const weapon_graph_archive_descriptor_t *desc,
	weapon_graph_weapon_settings_t *out,
	char *err, size_t err_cap)
{
	char *settings = NULL;
	char *variables = NULL;
	char *presentation = NULL;
	u32 settings_size = 0;
	u32 variables_size = 0;
	u32 presentation_size = 0;
	s32 result;

	memset(out, 0, sizeof(*out));
	if (desc->settings[0] &&
			weaponGraphArchiveReadTextFile(archive_path, desc->settings,
			&settings, &settings_size) != 0) {
		setErr(err, err_cap, "%s missing settings entry %s",
			archive_path, desc->settings);
		return -1;
	}
	if (desc->variables[0] &&
			weaponGraphArchiveReadTextFile(archive_path, desc->variables,
			&variables, &variables_size) != 0) {
		free(settings);
		setErr(err, err_cap, "%s missing variables entry %s",
			archive_path, desc->variables);
		return -1;
	}
	if (desc->presentation[0] &&
			weaponGraphArchiveReadTextFile(archive_path, desc->presentation,
			&presentation, &presentation_size) != 0) {
		free(settings);
		free(variables);
		setErr(err, err_cap, "%s missing presentation entry %s",
			archive_path, desc->presentation);
		return -1;
	}

	result = weaponGraphParseWeaponTunables(settings, settings_size,
		variables, variables_size, presentation, presentation_size,
		out, err, err_cap);
	free(settings);
	free(variables);
	free(presentation);
	return result;
}

s32 weaponGraphCompileArchiveFile(const char *archive_path,
                                  asset_type_e graph_type,
                                  weapon_graph_ir_t *out,
                                  char *err, size_t err_cap)
{
	weapon_graph_archive_descriptor_t desc;
	weapon_graph_weapon_settings_t vartable;
	char *graph = NULL;
	u32 graph_size = 0;
	s32 result;
	if (!archive_path || !out) {
		setErr(err, err_cap, "compile archive called with null input");
		return -1;
	}
	if (weaponGraphArchiveReadDescriptorFile(archive_path, graph_type,
			&desc, err, err_cap) != 0) {
		return -1;
	}

	/* c3849 Wave 5f Unit 2 (B6.3): a held weapon archive that ships a
	 * variables file gets $name substitution at this compile, so digests are
	 * identical between bare compiles and the register path. An empty
	 * variables table is still an ACTIVE substitution scope: unresolved
	 * $refs in such graphs fail loudly. */
	if (graph_type == ASSET_WEAPON && desc.variables[0]) {
		char *vtext = NULL;
		u32 vsize = 0;
		if (weaponGraphArchiveReadTextFile(archive_path, desc.variables,
				&vtext, &vsize) != 0) {
			setErr(err, err_cap, "%s missing variables entry %s",
				archive_path, desc.variables);
			return -1;
		}
		memset(&vartable, 0, sizeof(vartable));
		result = weaponGraphParseTunablesJson(vtext, vsize, 1,
			vartable.variables, WEAPON_GRAPH_SETTINGS_MAX,
			&vartable.variable_count, err, err_cap);
		free(vtext);
		if (result != 0) {
			return -1;
		}
		vartable.valid = 1;
		s_compile_variables = &vartable;
	}
	if (desc.behavior_graph[0]) {
		if (weaponGraphArchiveReadTextFile(archive_path, desc.behavior_graph,
				&graph, &graph_size) != 0) {
			setErr(err, err_cap, "%s missing graph entry %s",
				archive_path, desc.behavior_graph);
			s_compile_variables = NULL;
			return -1;
		}
	} else if (graph_type == ASSET_WEAPON) {
		if (composeWeaponGraphFromAuthoringFiles(archive_path, &desc,
				&graph, &graph_size, err, err_cap) != 0) {
			s_compile_variables = NULL;
			return -1;
		}
	} else {
		setErr(err, err_cap, "%s has no behavior_graph", archive_path);
		s_compile_variables = NULL;
		return -1;
	}
	result = weaponGraphCompileJson(graph_type, graph, graph_size, out,
		err, err_cap);
	s_compile_variables = NULL;
	free(graph);
	if (result == 0 && desc.catalog_id[0] && out->asset_id[0] &&
			strcmp(desc.catalog_id, out->asset_id) != 0) {
		setErr(err, err_cap, "graph asset_id %s does not match descriptor %s",
			out->asset_id, desc.catalog_id);
		return -1;
	}
	if (result == 0 && desc.catalog_id[0] && !out->asset_id[0]) {
		copyStr(out->asset_id, sizeof(out->asset_id), desc.catalog_id);
		finalizeIrDigest(out);
	}
	return result;
}

static s32 weaponGraphCompileArchiveBytes(const void *archive_bytes,
                                          u32 archive_size,
                                          asset_type_e graph_type,
                                          weapon_graph_ir_t *out,
                                          char *err, size_t err_cap)
{
	weapon_graph_archive_descriptor_t desc;
	void *graph = NULL;
	u32 graph_size = 0;
	s32 result;
	if (!archive_bytes || archive_size == 0 || !out) {
		setErr(err, err_cap, "compile archive bytes called with null input");
		return -1;
	}
	if (weaponGraphArchiveReadDescriptorBytes(archive_bytes, archive_size,
			graph_type, &desc, err, err_cap) != 0) {
		return -1;
	}
	if (!desc.behavior_graph[0]) {
		setErr(err, err_cap, "embedded %s archive has no behavior_graph",
			weaponGraphArchiveTypeName(graph_type));
		return -1;
	}
	graph = modArchiveExtractMemAlloc(archive_bytes, archive_size,
		desc.behavior_graph, &graph_size);
	if (!graph || graph_size == 0) {
		free(graph);
		setErr(err, err_cap, "embedded %s archive missing graph entry %s",
			weaponGraphArchiveTypeName(graph_type), desc.behavior_graph);
		return -1;
	}
	result = weaponGraphCompileJson(graph_type, (const char *)graph,
		graph_size, out, err, err_cap);
	free(graph);
	if (result == 0 && desc.catalog_id[0] && out->asset_id[0] &&
			strcmp(desc.catalog_id, out->asset_id) != 0) {
		setErr(err, err_cap, "graph asset_id %s does not match descriptor %s",
			out->asset_id, desc.catalog_id);
		return -1;
	}
	if (result == 0 && desc.catalog_id[0] && !out->asset_id[0]) {
		copyStr(out->asset_id, sizeof(out->asset_id), desc.catalog_id);
		finalizeIrDigest(out);
	}
	return result;
}

static const weapon_graph_ir_param_t *heldParam(
	const weapon_graph_ir_t *ir,
	const weapon_graph_ir_node_t *node,
	const char *key)
{
	if (!ir || !node || !key) return NULL;
	for (s32 i = 0; i < node->param_count; i++) {
		const weapon_graph_ir_param_t *p =
			&ir->params[node->param_start + i];
		if (strcmp(p->key, key) == 0) return p;
	}
	return NULL;
}

static s32 heldParamInt(const weapon_graph_ir_t *ir,
                        const weapon_graph_ir_node_t *node,
                        const char *key, s32 *out)
{
	const weapon_graph_ir_param_t *p = heldParam(ir, node, key);
	if (!p || !out) return 0;
	if (p->type == WEAPON_GRAPH_PARAM_INT) {
		*out = p->i_value;
		return 1;
	}
	if (p->type == WEAPON_GRAPH_PARAM_FLOAT) {
		*out = (s32)p->f_value;
		return 1;
	}
	if (p->type == WEAPON_GRAPH_PARAM_STRING && p->value[0]) {
		char *end = NULL;
		long v = strtol(p->value, &end, 10);
		if (end && *end == '\0') {
			*out = (s32)v;
			return 1;
		}
	}
	return 0;
}

static s32 heldParamU32(const weapon_graph_ir_t *ir,
                        const weapon_graph_ir_node_t *node,
                        const char *key, u32 *out)
{
	const weapon_graph_ir_param_t *p = heldParam(ir, node, key);
	if (!p || !out) return 0;
	if (p->type == WEAPON_GRAPH_PARAM_INT ||
			p->type == WEAPON_GRAPH_PARAM_FLOAT ||
			p->type == WEAPON_GRAPH_PARAM_STRING) {
		char *end = NULL;
		unsigned long v = strtoul(p->value, &end, 10);
		if (end && *end == '\0') {
			*out = (u32)v;
			return 1;
		}
	}
	return 0;
}

static s32 heldParamFloat(const weapon_graph_ir_t *ir,
                          const weapon_graph_ir_node_t *node,
                          const char *key, f32 *out)
{
	const weapon_graph_ir_param_t *p = heldParam(ir, node, key);
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

static s32 heldParamString(const weapon_graph_ir_t *ir,
                           const weapon_graph_ir_node_t *node,
                           const char *key, char *out, size_t out_cap)
{
	const weapon_graph_ir_param_t *p = heldParam(ir, node, key);
	if (!p || !out || out_cap == 0 ||
			p->type != WEAPON_GRAPH_PARAM_STRING) {
		return 0;
	}
	copyStr(out, out_cap, p->value);
	return 1;
}

static s32 heldParamBool(const weapon_graph_ir_t *ir,
                         const weapon_graph_ir_node_t *node,
                         const char *key, s32 *out)
{
	const weapon_graph_ir_param_t *p = heldParam(ir, node, key);
	if (!p || !out) return 0;
	if (p->type == WEAPON_GRAPH_PARAM_BOOL) {
		*out = p->b_value ? 1 : 0;
		return 1;
	}
	if (p->type == WEAPON_GRAPH_PARAM_INT) {
		*out = p->i_value ? 1 : 0;
		return 1;
	}
	if (p->type == WEAPON_GRAPH_PARAM_STRING) {
		if (strcmp(p->value, "true") == 0 || strcmp(p->value, "yes") == 0 ||
				strcmp(p->value, "1") == 0) {
			*out = 1;
			return 1;
		}
		if (strcmp(p->value, "false") == 0 || strcmp(p->value, "no") == 0 ||
				strcmp(p->value, "0") == 0) {
			*out = 0;
			return 1;
		}
	}
	return 0;
}

static s32 heldParamIntAlias(const weapon_graph_ir_t *ir,
                             const weapon_graph_ir_node_t *node,
                             const char *key0, const char *key1, s32 *out)
{
	if (heldParamInt(ir, node, key0, out)) return 1;
	return key1 ? heldParamInt(ir, node, key1, out) : 0;
}

static s32 heldParamFloatAlias(const weapon_graph_ir_t *ir,
                               const weapon_graph_ir_node_t *node,
                               const char *key0, const char *key1, f32 *out)
{
	if (heldParamFloat(ir, node, key0, out)) return 1;
	return key1 ? heldParamFloat(ir, node, key1, out) : 0;
}

static s32 heldFunctionTypeId(const char *function_type)
{
	if (!function_type || !function_type[0]) return INVENTORYFUNCTYPE_NONE;
	if (strcmp(function_type, "none") == 0) return INVENTORYFUNCTYPE_NONE;
	if (strcmp(function_type, "shoot_single") == 0) return INVENTORYFUNCTYPE_SHOOT_SINGLE;
	if (strcmp(function_type, "shoot_automatic") == 0) return INVENTORYFUNCTYPE_SHOOT_AUTOMATIC;
	if (strcmp(function_type, "shoot_projectile") == 0) return INVENTORYFUNCTYPE_SHOOT_PROJECTILE;
	if (strcmp(function_type, "throw") == 0) return INVENTORYFUNCTYPE_THROW;
	if (strcmp(function_type, "melee") == 0) return INVENTORYFUNCTYPE_MELEE;
	if (strcmp(function_type, "special") == 0) return INVENTORYFUNCTYPE_SPECIAL;
	if (strcmp(function_type, "device") == 0) return INVENTORYFUNCTYPE_DEVICE;
	return INVENTORYFUNCTYPE_NONE;
}

static s32 heldSightId(const char *sight)
{
	if (!sight || !sight[0]) return -1;
	if (strcmp(sight, "default") == 0) return SIGHT_DEFAULT;
	if (strcmp(sight, "classic") == 0) return SIGHT_CLASSIC;
	if (strcmp(sight, "skedar") == 0) return SIGHT_SKEDAR;
	if (strcmp(sight, "zoom") == 0) return SIGHT_ZOOM;
	if (strcmp(sight, "maian") == 0) return SIGHT_MAIAN;
	if (strcmp(sight, "none") == 0) return SIGHT_NONE;
	return -1;
}

static s32 heldParseHexSuffix(const char *value, s32 *out)
{
	const char *end;
	const char *start;
	char buf[16];
	size_t len;
	char *parse_end = NULL;
	long parsed;

	if (!value || !value[0] || !out) return 0;
	end = value + strlen(value);
	start = end;
	while (start > value && isxdigit((unsigned char)*(start - 1))) {
		start--;
	}
	if (start == end || (start > value && *(start - 1) != '_')) {
		return 0;
	}
	len = (size_t)(end - start);
	if (len == 0 || len >= sizeof(buf)) return 0;
	memcpy(buf, start, len);
	buf[len] = '\0';
	parsed = strtol(buf, &parse_end, 16);
	if (!parse_end || *parse_end != '\0') return 0;
	*out = (s32)parsed;
	return 1;
}

static s32 heldResolveSfxParam(const weapon_graph_ir_t *ir,
                               const weapon_graph_ir_node_t *node,
                               const char *key, s32 *out)
{
	const weapon_graph_ir_param_t *p = heldParam(ir, node, key);
	s32 resolved;
	char *end = NULL;
	long parsed;

	if (!p || !out) return 0;
	if (heldParamInt(ir, node, key, out)) return 1;
	if (p->type != WEAPON_GRAPH_PARAM_STRING || !p->value[0]) return 0;

	if (strchr(p->value, ':')) {
		catalog_audio_result_t audio;
		/* c3849 Wave 2: only accept a RESOLVED soundnum. An unallocated
		 * custom row carries sound_id = -1; downstream consumers clamp
		 * negatives to 0, which would silently play SFX_0000. Fall through
		 * so the miss stays loud (no shootsound) instead. */
		if (catalogResolveAudio(p->value, &audio) && audio.sound_id >= 0) {
			*out = audio.sound_id;
			return 1;
		}
		if (strcmp(p->value, "base:sfx_launch_rocket") == 0) {
			*out = SFX_LAUNCH_ROCKET_8053;
			return 1;
		}
	}

	resolved = loaderEnumResolveSfxEnum(p->value, -1);
	if (resolved >= 0) {
		*out = resolved;
		return 1;
	}

	parsed = strtol(p->value, &end, 0);
	if (end && *end == '\0') {
		*out = (s32)parsed;
		return 1;
	}

	return heldParseHexSuffix(p->value, out);
}

static s32 heldResolveProjectileModelRef(const char *ref, s32 *out)
{
	const char *hex;
	char *end = NULL;
	long parsed;

	if (!ref || !out) return 0;
	if (strchr(ref, ':')) {
		catalog_model_result_t model;
		if (catalogResolveModel(ref, &model)) {
			*out = model.modelnum;
			return 1;
		}
		if (strcmp(ref, "base:model_chrdyrocketmis") == 0 ||
				strcmp(ref, "base:model_dyrocket") == 0) {
			*out = MODEL_CHRDYROCKETMIS;
			return 1;
		}
		if (strcmp(ref, "base:model_chrskrocketmis") == 0 ||
				strcmp(ref, "base:model_skrocket") == 0) {
			*out = MODEL_CHRSKROCKETMIS;
			return 1;
		}
		if (strcmp(ref, "base:model_chrcrossbolt") == 0 ||
				strcmp(ref, "base:model_crossbow_bolt") == 0) {
			*out = MODEL_CHRCROSSBOLT;
			return 1;
		}
		if (strcmp(ref, "base:model_chrdevgrenade") == 0 ||
				strcmp(ref, "base:model_devastator_grenade") == 0) {
			*out = MODEL_CHRDEVGRENADE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrdraggrenade") == 0 ||
				strcmp(ref, "base:model_dragon_grenade") == 0) {
			*out = MODEL_CHRDRAGGRENADE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrknife") == 0 ||
				strcmp(ref, "base:model_knife") == 0) {
			*out = MODEL_CHRKNIFE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrbug") == 0 ||
				strcmp(ref, "base:model_bug") == 0) {
			*out = MODEL_CHRBUG;
			return 1;
		}
		if (strcmp(ref, "base:model_targetamp") == 0 ||
				strcmp(ref, "base:model_target_amplifier") == 0) {
			*out = MODEL_TARGETAMP;
			return 1;
		}
		if (strcmp(ref, "base:model_chrautogun") == 0 ||
				strcmp(ref, "base:model_autogun") == 0) {
			*out = MODEL_CHRAUTOGUN;
			return 1;
		}
		if (strcmp(ref, "base:model_chrdragon") == 0 ||
				strcmp(ref, "base:model_dragon") == 0) {
			*out = MODEL_CHRDRAGON;
			return 1;
		}
		if (strcmp(ref, "base:model_chrgrenade") == 0 ||
				strcmp(ref, "base:model_grenade") == 0) {
			*out = MODEL_CHRGRENADE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrnbomb") == 0 ||
				strcmp(ref, "base:model_nbomb") == 0) {
			*out = MODEL_CHRNBOMB;
			return 1;
		}
		if (strcmp(ref, "base:model_chrtimedmine") == 0 ||
				strcmp(ref, "base:model_timed_mine") == 0) {
			*out = MODEL_CHRTIMEDMINE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrproximitymine") == 0 ||
				strcmp(ref, "base:model_proximity_mine") == 0) {
			*out = MODEL_CHRPROXIMITYMINE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrremotemine") == 0 ||
				strcmp(ref, "base:model_remote_mine") == 0) {
			*out = MODEL_CHRREMOTEMINE;
			return 1;
		}
		if (strcmp(ref, "base:model_chrecmmine") == 0 ||
				strcmp(ref, "base:model_ecm_mine") == 0) {
			*out = MODEL_CHRECMMINE;
			return 1;
		}
	}
	if (strcmp(ref, "MODEL_dyrocket") == 0) { *out = MODEL_CHRDYROCKETMIS; return 1; }
	if (strcmp(ref, "MODEL_skrocket") == 0) { *out = MODEL_CHRSKROCKETMIS; return 1; }
	if (strcmp(ref, "MODEL_crossbow_bolt") == 0) { *out = MODEL_CHRCROSSBOLT; return 1; }
	if (strcmp(ref, "MODEL_devastator_grenade") == 0) { *out = MODEL_CHRDEVGRENADE; return 1; }
	if (strcmp(ref, "MODEL_dragon_grenade") == 0) { *out = MODEL_CHRDRAGGRENADE; return 1; }
	if (strcmp(ref, "MODEL_knife") == 0) { *out = MODEL_CHRKNIFE; return 1; }
	if (strcmp(ref, "MODEL_bug") == 0) { *out = MODEL_CHRBUG; return 1; }
	if (strcmp(ref, "MODEL_target_amplifier") == 0) { *out = MODEL_TARGETAMP; return 1; }
	if (strcmp(ref, "MODEL_autogun") == 0) { *out = MODEL_CHRAUTOGUN; return 1; }
	if (strcmp(ref, "MODEL_dragon") == 0) { *out = MODEL_CHRDRAGON; return 1; }
	if (strcmp(ref, "MODEL_grenade") == 0) { *out = MODEL_CHRGRENADE; return 1; }
	if (strcmp(ref, "MODEL_nbomb") == 0) { *out = MODEL_CHRNBOMB; return 1; }
	if (strcmp(ref, "MODEL_timed_mine") == 0) { *out = MODEL_CHRTIMEDMINE; return 1; }
	if (strcmp(ref, "MODEL_proximity_mine") == 0) { *out = MODEL_CHRPROXIMITYMINE; return 1; }
	if (strcmp(ref, "MODEL_remote_mine") == 0) { *out = MODEL_CHRREMOTEMINE; return 1; }
	if (strcmp(ref, "MODEL_ecm_mine") == 0) { *out = MODEL_CHRECMMINE; return 1; }

	if (!startsWith(ref, "MODEL_")) return 0;
	hex = ref + 6;
	if (!isxdigit((unsigned char)hex[0])) return 0;
	parsed = strtol(hex, &end, 16);
	if (end && *end == '\0') {
		*out = (s32)parsed;
		return 1;
	}
	return 0;
}

static void runtimeCopyIrIdentity(const weapon_graph_ir_t *ir,
                                  char *asset_id, size_t asset_id_cap,
                                  char *graph_id, size_t graph_id_cap,
                                  char *source_sha256, size_t source_cap,
                                  char *ir_sha256, size_t ir_cap)
{
	copyStr(asset_id, asset_id_cap, ir ? ir->asset_id : "");
	copyStr(graph_id, graph_id_cap, ir ? ir->graph_id : "");
	copyStr(source_sha256, source_cap, ir ? ir->source_sha256 : "");
	copyStr(ir_sha256, ir_cap, ir ? ir->ir_sha256 : "");
}

/* c3849 Unit 1c: string policies latch to s32 enums at registration so no
 * weaponTick/projectileTick consumer ever strcmps per event (binding spec
 * B3). Unknown values latch the OG default LOUDLY so authoring mistakes
 * surface at registration time. */
static s32 policyLatchUnknown(const char *field, const char *value,
                              s32 og_default)
{
	sysLogPrintf(LOG_WARNING,
		"WEAPONGRAPH.PARSE: unknown %s '%s'; latching OG default",
		field, value);
	return og_default;
}

static s32 weaponGraphParseTimerStartPolicy(const char *value)
{
	if (!value || !value[0] || strcmp(value, "on_spawn") == 0) return 0;
	if (strcmp(value, "on_impact") == 0) return 1;
	if (strcmp(value, "on_attach") == 0) return 2;
	return policyLatchUnknown("timer_starts", value, 0);
}

static s32 weaponGraphParseTimerExpirePolicy(const char *value)
{
	if (!value || !value[0] || strcmp(value, "explode") == 0) return 0;
	if (strcmp(value, "delete") == 0) return 1;
	return policyLatchUnknown("timer_on_expire", value, 0);
}

/* Accepts "down" (the OG Devastator fall) or an explicit "x,y,z" triple;
 * anything else keeps the pre-set {0,-10,0} default loudly. */
static void weaponGraphParseFallVector(const char *value, f32 out[3])
{
	f32 x;
	f32 y;
	f32 z;

	if (!value || !value[0] || strcmp(value, "down") == 0) {
		return;
	}
	if (sscanf(value, " %f , %f , %f", &x, &y, &z) == 3) {
		out[0] = x;
		out[1] = y;
		out[2] = z;
		return;
	}
	policyLatchUnknown("fall_vector", value, 0);
}

static s32 weaponGraphParseImpactFilterMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "any") == 0) return 0;
	if (strcmp(value, "background") == 0) return 1;
	if (strcmp(value, "props") == 0) return 2;
	if (strcmp(value, "chr") == 0) return 3;
	return policyLatchUnknown("impact_filter", value, 0);
}

static s32 weaponGraphParseTrailSmokeType(const char *value)
{
	if (!value || !value[0] || strcmp(value, "none") == 0) return -1;
	if (strcmp(value, "rocket") == 0) return SMOKETYPE_ROCKETTAIL;
	if (strcmp(value, "homing") == 0) return SMOKETYPE_HOMINGTAIL;
	if (strcmp(value, "grenade") == 0) return SMOKETYPE_GRENADETAIL;
	return policyLatchUnknown("trail_type", value, SMOKETYPE_ROCKETTAIL);
}

/* Mirrors heldResolveSfxParam: catalog refs resolve at registration, and
 * only an actually-resolved runtime weaponnum is accepted. If the ref names
 * a weapon that registers later, this stays -1 and the consume site keeps
 * its OG fallback; the miss is logged so it stays visible. */
static s32 weaponGraphResolveRecoverWeapon(const char *ref)
{
	catalog_weapon_result_t weapon;

	if (!ref || !ref[0]) {
		return -1;
	}
	if (catalogResolveWeapon(ref, &weapon) && weapon.weapon_num > 0) {
		return weapon.weapon_num;
	}
	sysLogPrintf(LOG_NOTE,
		"WEAPONGRAPH.PARSE: recover_weapon_ref '%s' unresolved at registration; consume site keeps the OG fallback",
		ref);
	return -1;
}

static s32 weaponGraphParseDamageResponseMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "detonate") == 0) return 0;
	if (strcmp(value, "ignore") == 0) return 1;
	return policyLatchUnknown("damage_response", value, 0);
}

static s32 weaponGraphParseProxyTriggerMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "detonate") == 0) return 0;
	if (strcmp(value, "storm") == 0 || strcmp(value, "create_storm") == 0) {
		return 1;
	}
	return policyLatchUnknown("proxy on_trigger", value, 0);
}

static s32 weaponGraphParseProxyTargetFilterMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "all") == 0 ||
			strcmp(value, "any") == 0) {
		return 0;
	}
	if (strcmp(value, "hostile_chr") == 0) return 1;
	return policyLatchUnknown("proxy target_filter", value, 0);
}

static s32 weaponGraphParseProxyTeamFilterMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "all") == 0 ||
			strcmp(value, "any") == 0) {
		return 0;
	}
	if (strcmp(value, "enemy_only") == 0) return 1;
	return policyLatchUnknown("proxy team_filter", value, 0);
}

static s32 weaponGraphParseProxyOwnerFilterMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "all") == 0 ||
			strcmp(value, "any") == 0) {
		return 0;
	}
	if (strcmp(value, "exclude_owner") == 0) return 1;
	if (strcmp(value, "owner_only") == 0) return 2;
	return policyLatchUnknown("proxy owner_filter", value, 0);
}

static s32 weaponGraphParseRemoteSignalMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "detonate") == 0) return 0;
	/* c3849 Wave 5 Unit 6: remote-storm route (the custom entity arm
	 * consumes mode 1); same vocabulary as the proxy/timed parsers. */
	if (strcmp(value, "storm") == 0 || strcmp(value, "create_storm") == 0) {
		return 1;
	}
	return policyLatchUnknown("on_remote_signal", value, 0);
}

static s32 weaponGraphParseTimedExpireMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "detonate") == 0) return 0;
	if (strcmp(value, "storm") == 0 || strcmp(value, "create_storm") == 0) {
		return 1;
	}
	if (strcmp(value, "delete") == 0) return 2;
	return policyLatchUnknown("timed on_expire", value, 0);
}

static s32 weaponGraphParseTimedStartsMode(const char *value)
{
	if (!value || !value[0] || strcmp(value, "thrown") == 0 ||
			strcmp(value, "armed") == 0) {
		return 0;
	}
	return policyLatchUnknown("timed starts_when", value, 0);
}

static void projectileRuntimeFromNode(const weapon_graph_ir_t *ir,
                                      const weapon_graph_ir_node_t *node,
                                      weapon_graph_projectile_runtime_t *out)
{
	s32 v;
	u32 uv;
	f32 f;
	const char *module = weaponGraphParityModuleNameForOpcode(node->opcode);
	if (module[0] && !out->parity_module[0]) {
		copyStr(out->parity_module, sizeof(out->parity_module), module);
	}
	switch (node->opcode) {
	case WEAPON_GRAPH_OP_PROJECTILE_SPAWN_STATE:
		if (!heldParamString(ir, node, "model_catalog_id", out->model_ref,
				sizeof(out->model_ref))) {
			heldParamString(ir, node, "model_ref", out->model_ref,
				sizeof(out->model_ref));
		}
		if (out->model_ref[0] &&
				heldResolveProjectileModelRef(out->model_ref, &v)) {
			out->has_projectile_modelnum = 1;
			out->projectile_modelnum = v;
		}
		heldParamString(ir, node, "model_archive", out->model_archive,
			sizeof(out->model_archive));
		heldParamString(ir, node, "source_mode", out->source_mode,
			sizeof(out->source_mode));
		heldParamString(ir, node, "source_function_type",
			out->source_function_type, sizeof(out->source_function_type));
		out->source_function_type_id =
			heldFunctionTypeId(out->source_function_type);
		out->has_source_function_type_id =
			out->source_function_type[0] ? 1 : 0;
		if (heldParamU32(ir, node, "flags", &uv)) out->flags = uv;
		if (heldParamFloat(ir, node, "scale", &f)) {
			out->has_scale = 1;
			out->scale = f;
		}
		if (heldParamFloat(ir, node, "damage", &f)) {
			out->has_damage = 1;
			out->damage = f;
		}
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_MOTION:
		heldParamString(ir, node, "motion_kind", out->motion_kind,
			sizeof(out->motion_kind));
		if (heldParamFloatAlias(ir, node, "speed", "initial_speed", &f)) {
			out->has_speed = 1;
			out->speed = f;
		}
		if (heldParamInt(ir, node, "travel_distance", &v)) {
			out->has_travel_distance = 1;
			out->travel_distance = v;
		}
		if (heldParamIntAlias(ir, node, "timer60", "timer_ticks60", &v)) {
			out->has_timer60 = 1;
			out->timer60 = v;
		}
		if (heldParamIntAlias(ir, node, "activation_time60",
				"activation_time_ticks60", &v)) {
			out->has_activation_time60 = 1;
			out->activation_time60 = v;
		}
		if (heldParamIntAlias(ir, node, "recovery_time60",
				"recovery_time_ticks60", &v)) {
			out->has_recovery_time60 = 1;
			out->recovery_time60 = v;
		}
		if (heldParamFloat(ir, node, "reflect_angle", &f)) {
			out->has_reflect_angle = 1;
			out->reflect_angle = f;
		}
		if (heldParamBool(ir, node, "powered", &v)) out->powered = v;
		if (heldParamBool(ir, node, "calculate_trajectory", &v)) {
			out->calculate_trajectory = v;
		}
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_TRAJECTORY_CORRECTION:
		out->has_trajectory_correction = 1;
		heldParamString(ir, node, "aim_source",
			out->trajectory_aim_source,
			sizeof(out->trajectory_aim_source));
		if (heldParamBool(ir, node, "solve_velocity", &v)) {
			out->trajectory_solve_velocity = v;
		}
		heldParamFloat(ir, node, "max_angle", &out->trajectory_max_angle);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_HOMING:
		out->has_homing = 1;
		heldParamString(ir, node, "target_source",
			out->homing_target_source, sizeof(out->homing_target_source));
		heldParamString(ir, node, "target_filter",
			out->homing_target_filter, sizeof(out->homing_target_filter));
		heldParamString(ir, node, "lost_target_behavior",
			out->homing_lost_target_behavior,
			sizeof(out->homing_lost_target_behavior));
		heldParamString(ir, node, "retarget_policy",
			out->homing_retarget_policy,
			sizeof(out->homing_retarget_policy));
		heldParamString(ir, node, "runtime_constants",
			out->homing_runtime_constants,
			sizeof(out->homing_runtime_constants));
		heldParamFloat(ir, node, "steering_gain",
			&out->homing_steering_gain);
		heldParamFloat(ir, node, "steering_damping",
			&out->homing_steering_damping);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_FLY_BY_WIRE:
		out->has_fly_by_wire = 1;
		heldParamString(ir, node, "control_source", out->fly_control_source,
			sizeof(out->fly_control_source));
		heldParamString(ir, node, "bot_route_policy",
			out->fly_bot_route_policy, sizeof(out->fly_bot_route_policy));
		heldParamString(ir, node, "owner_death_behavior",
			out->fly_owner_death_behavior,
			sizeof(out->fly_owner_death_behavior));
		heldParamFloat(ir, node, "turn_rate", &out->fly_turn_rate);
		heldParamFloat(ir, node, "acceleration", &out->fly_acceleration);
		heldParamFloat(ir, node, "enemy_proximity_radius",
			&out->fly_enemy_proximity_radius);
		heldParamFloat(ir, node, "max_altitude", &out->fly_max_altitude);
		heldParamInt(ir, node, "lost_target_timeout_ticks60",
			&out->fly_lost_target_timeout_ticks60);
		heldParamInt(ir, node, "smoke_interval_ticks60",
			&out->fly_smoke_interval_ticks60);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_WALL_HUGGER:
		out->has_wall_hugger = 1;
		heldParamString(ir, node, "stick_surface_filter",
			out->wall_stick_surface_filter,
			sizeof(out->wall_stick_surface_filter));
		heldParamString(ir, node, "fall_vector", out->wall_fall_vector,
			sizeof(out->wall_fall_vector));
		heldParamString(ir, node, "explosion_ref", out->wall_explosion_ref,
			sizeof(out->wall_explosion_ref));
		heldParamInt(ir, node, "stick_timer_ticks60",
			&out->wall_stick_timer_ticks60);
		heldParamFloat(ir, node, "fall_threshold",
			&out->wall_fall_threshold);
		heldParamIntAlias(ir, node, "post_fall_timer60",
			"post_fall_timer_ticks60", &out->wall_post_fall_timer60);
		/* c3849 Unit 1c: derived latches (B3). */
		out->wall_stick_bg_only =
			strcmp(out->wall_stick_surface_filter, "background") == 0 ? 1 : 0;
		weaponGraphParseFallVector(out->wall_fall_vector, out->wall_fall_vec);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_STICKY_ATTACH:
		out->has_sticky_attach = 1;
		heldParamString(ir, node, "surface_filter",
			out->sticky_surface_filter, sizeof(out->sticky_surface_filter));
		heldParamString(ir, node, "prop_filter", out->sticky_prop_filter,
			sizeof(out->sticky_prop_filter));
		heldParamString(ir, node, "embed_policy", out->sticky_embed_policy,
			sizeof(out->sticky_embed_policy));
		heldParamString(ir, node, "on_attach", out->sticky_on_attach,
			sizeof(out->sticky_on_attach));
		heldParamBool(ir, node, "allow_background",
			&out->sticky_allow_background);
		heldParamBool(ir, node, "allow_char", &out->sticky_allow_char);
		heldParamBool(ir, node, "allow_obj", &out->sticky_allow_obj);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_BOUNCE_SLIDE:
		out->has_bounce_slide = 1;
		/* c3849 Wave 5 (surface): randomize_rotation is absent-vs-authored-0
		 * ambiguous on a zeroed struct, and authored false carries meaning
		 * (it sets PROJECTILEFLAG_00000100 to disable the OG random bounce
		 * rotation). Sentinel pre-set mirrors the entity-record discipline:
		 * absent -1, authored false 0, authored true 1. */
		out->bounce_randomize_rotation = -1;
		heldParamInt(ir, node, "bounce_limit", &out->bounce_limit);
		heldParamFloat(ir, node, "first_bounce_boost",
			&out->bounce_first_boost);
		heldParamFloat(ir, node, "rest_speed", &out->bounce_rest_speed);
		heldParamFloat(ir, node, "slide_friction",
			&out->bounce_slide_friction);
		heldParamBool(ir, node, "randomize_rotation",
			&out->bounce_randomize_rotation);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_TIMER:
		out->has_timer = 1;
		heldParamIntAlias(ir, node, "timer", "timer_ticks60",
			&out->timer_ticks60);
		heldParamString(ir, node, "timer_starts", out->timer_starts,
			sizeof(out->timer_starts));
		heldParamString(ir, node, "on_expire", out->timer_on_expire,
			sizeof(out->timer_on_expire));
		/* c3849 Unit 1c: derived latches (B3). */
		out->timer_start_policy =
			weaponGraphParseTimerStartPolicy(out->timer_starts);
		out->timer_expire_policy =
			weaponGraphParseTimerExpirePolicy(out->timer_on_expire);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_IMPACT:
		out->has_impact = 1;
		heldParamString(ir, node, "impact_filter", out->impact_filter,
			sizeof(out->impact_filter));
		heldParamString(ir, node, "explosion_ref",
			out->impact_explosion_ref, sizeof(out->impact_explosion_ref));
		heldParamString(ir, node, "spark_ref", out->impact_spark_ref,
			sizeof(out->impact_spark_ref));
		heldResolveSfxParam(ir, node, "hit_sound", &out->impact_hit_sound);
		heldParamBool(ir, node, "consume_on_hit",
			&out->impact_consume_on_hit);
		heldParamBool(ir, node, "stick_on_hit", &out->impact_stick_on_hit);
		/* c3849 Unit 1c: derived latches (B3). Non-base explosion refs stay
		 * -1 here and resolve at detonation via the effect bridge (B2). */
		out->impact_filter_mode =
			weaponGraphParseImpactFilterMode(out->impact_filter);
		out->impact_exptype =
			weaponGraphResolveExplosionRef(out->impact_explosion_ref);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_TRAIL:
		out->has_trail = 1;
		heldParamString(ir, node, "trail_type", out->trail_type,
			sizeof(out->trail_type));
		heldParamIntAlias(ir, node, "interval", "interval_ticks60",
			&out->trail_interval_ticks60);
		/* c3849 Unit 1c: derived latch (B3). */
		out->trail_smoketype =
			weaponGraphParseTrailSmokeType(out->trail_type);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_TRANSITION_TO_ENTITY:
		out->has_transition_to_entity = 1;
		heldParamString(ir, node, "entity_ref", out->entity_ref,
			sizeof(out->entity_ref));
		heldParamString(ir, node, "when", out->transition_when,
			sizeof(out->transition_when));
		heldParamBool(ir, node, "transfer_owner", &out->transfer_owner);
		heldParamBool(ir, node, "transfer_ammo", &out->transfer_ammo);
		heldParamBool(ir, node, "transfer_position",
			&out->transfer_position);
		heldParamBool(ir, node, "delete_carrier", &out->delete_carrier);
		break;
	case WEAPON_GRAPH_OP_PROJECTILE_PICKUP_RECOVER:
		out->has_pickup_recover = 1;
		heldParamIntAlias(ir, node, "pickup_timer", "pickup_timer_ticks60",
			&out->pickup_timer_ticks60);
		heldParamString(ir, node, "allowed_owner",
			out->pickup_allowed_owner, sizeof(out->pickup_allowed_owner));
		heldParamString(ir, node, "recover_weapon_ref",
			out->recover_weapon_ref, sizeof(out->recover_weapon_ref));
		heldParamString(ir, node, "recover_ammo_policy",
			out->recover_ammo_policy, sizeof(out->recover_ammo_policy));
		heldResolveSfxParam(ir, node, "sound", &out->pickup_sound);
		/* c3849 Unit 1c: derived latches (B3). */
		out->pickup_owner_only =
			(strcmp(out->pickup_allowed_owner, "owner") == 0 ||
			 strcmp(out->pickup_allowed_owner, "owner_only") == 0) ? 1 : 0;
		out->recover_ammo_none =
			strcmp(out->recover_ammo_policy, "none") == 0 ? 1 : 0;
		out->recover_weaponnum =
			weaponGraphResolveRecoverWeapon(out->recover_weapon_ref);
		break;
	default:
		break;
	}
}

static void entityRuntimeFromNode(const weapon_graph_ir_t *ir,
                                  const weapon_graph_ir_node_t *node,
                                  weapon_graph_entity_runtime_t *out)
{
	s32 v;
	u32 uv;
	f32 f;
	heldParamString(ir, node, "archetype", out->archetype,
		sizeof(out->archetype));
	if ((heldParamString(ir, node, "model_catalog_id", out->model_ref,
			sizeof(out->model_ref)) ||
			heldParamString(ir, node, "model_ref", out->model_ref,
			sizeof(out->model_ref))) &&
			heldResolveProjectileModelRef(out->model_ref, &v)) {
		out->has_projectile_modelnum = 1;
		out->projectile_modelnum = v;
	}
	heldParamString(ir, node, "model_archive", out->model_archive,
		sizeof(out->model_archive));
	heldParamString(ir, node, "source_mode", out->source_mode,
		sizeof(out->source_mode));
	if (heldParamU32(ir, node, "flags", &uv)) out->flags = uv;
	if (heldParamIntAlias(ir, node, "activation_time60",
			"activation_time_ticks60", &v)) {
		out->has_activation_time60 = 1;
		out->activation_time60 = v;
	}
	if (heldParamIntAlias(ir, node, "recovery_time60",
			"recovery_time_ticks60", &v)) {
		out->has_recovery_time60 = 1;
		out->recovery_time60 = v;
	}
	heldParamString(ir, node, "runtime_detail", out->runtime_detail,
		sizeof(out->runtime_detail));
	const char *module = weaponGraphParityModuleNameForOpcode(node->opcode);
	if (module[0] && !out->parity_module[0]) {
		copyStr(out->parity_module, sizeof(out->parity_module), module);
	}

	switch (node->opcode) {
	case WEAPON_GRAPH_OP_ENTITY_ARMED_EXPLOSIVE:
		out->has_armed_explosive = 1;
		heldParamIntAlias(ir, node, "arm_delay", "arm_delay_ticks60",
			&out->arm_delay_ticks60);
		heldParamString(ir, node, "detonation_policy",
			out->detonation_policy, sizeof(out->detonation_policy));
		heldParamString(ir, node, "explosion_ref", out->explosion_ref,
			sizeof(out->explosion_ref));
		heldParamString(ir, node, "owner_filter", out->armed_owner_filter,
			sizeof(out->armed_owner_filter));
		heldParamString(ir, node, "damage_response", out->damage_response,
			sizeof(out->damage_response));
		heldParamBool(ir, node, "delete_on_detonate",
			&out->delete_on_detonate);
		/* c3849 Unit 1c: derived latches (B3). Non-base refs stay -1 and
		 * resolve at detonation via the effect bridge (B2). */
		out->armed_damage_response_mode =
			weaponGraphParseDamageResponseMode(out->damage_response);
		out->armed_exptype =
			weaponGraphResolveExplosionRef(out->explosion_ref);
		break;
	case WEAPON_GRAPH_OP_ENTITY_PROXY_TRIGGER:
		out->has_proxy_trigger = 1;
		heldParamFloat(ir, node, "radius", &out->proxy_radius);
		heldParamString(ir, node, "target_filter",
			out->proxy_target_filter, sizeof(out->proxy_target_filter));
		heldParamString(ir, node, "team_filter", out->proxy_team_filter,
			sizeof(out->proxy_team_filter));
		heldParamString(ir, node, "owner_filter",
			out->proxy_owner_filter, sizeof(out->proxy_owner_filter));
		heldParamBool(ir, node, "line_of_sight", &out->proxy_line_of_sight);
		heldParamString(ir, node, "on_trigger", out->proxy_on_trigger,
			sizeof(out->proxy_on_trigger));
		/* c3849 Unit 1c: derived latches (B3); 0 = OG (all/detonate). */
		out->proxy_on_trigger_mode =
			weaponGraphParseProxyTriggerMode(out->proxy_on_trigger);
		out->proxy_target_filter_mode =
			weaponGraphParseProxyTargetFilterMode(out->proxy_target_filter);
		out->proxy_team_filter_mode =
			weaponGraphParseProxyTeamFilterMode(out->proxy_team_filter);
		out->proxy_owner_filter_mode =
			weaponGraphParseProxyOwnerFilterMode(out->proxy_owner_filter);
		break;
	case WEAPON_GRAPH_OP_ENTITY_REMOTE_DETONATABLE:
		out->has_remote_detonatable = 1;
		heldParamString(ir, node, "detonator_ref", out->detonator_ref,
			sizeof(out->detonator_ref));
		heldParamString(ir, node, "owner_slot_source",
			out->owner_slot_source, sizeof(out->owner_slot_source));
		heldParamString(ir, node, "coop_policy", out->coop_policy,
			sizeof(out->coop_policy));
		heldParamString(ir, node, "anti_policy", out->anti_policy,
			sizeof(out->anti_policy));
		heldParamString(ir, node, "self_attached_policy",
			out->self_attached_policy, sizeof(out->self_attached_policy));
		heldParamString(ir, node, "on_remote_signal",
			out->on_remote_signal, sizeof(out->on_remote_signal));
		/* c3849 Unit 1c: derived latch (B3). */
		out->remote_signal_mode =
			weaponGraphParseRemoteSignalMode(out->on_remote_signal);
		break;
	case WEAPON_GRAPH_OP_ENTITY_TIMED_DETONATABLE:
		out->has_timed_detonatable = 1;
		heldParamIntAlias(ir, node, "timer", "timer_ticks60",
			&out->timed_timer_ticks60);
		heldParamString(ir, node, "starts_when", out->timed_starts_when,
			sizeof(out->timed_starts_when));
		heldParamString(ir, node, "on_expire", out->timed_on_expire,
			sizeof(out->timed_on_expire));
		heldParamString(ir, node, "pause_policy", out->timed_pause_policy,
			sizeof(out->timed_pause_policy));
		/* c3849 Unit 1c: derived latches (B3). */
		out->timed_on_expire_mode =
			weaponGraphParseTimedExpireMode(out->timed_on_expire);
		out->timed_starts_mode =
			weaponGraphParseTimedStartsMode(out->timed_starts_when);
		break;
	case WEAPON_GRAPH_OP_ENTITY_NBOMB_STORM:
		out->has_nbomb_storm = 1;
		heldParamString(ir, node, "storm_ref", out->storm_ref,
			sizeof(out->storm_ref));
		heldParamString(ir, node, "owner_transfer",
			out->storm_owner_transfer, sizeof(out->storm_owner_transfer));
		heldParamString(ir, node, "activation_policy",
			out->storm_activation_policy,
			sizeof(out->storm_activation_policy));
		heldParamBool(ir, node, "delete_carrier",
			&out->storm_delete_carrier);
		break;
	case WEAPON_GRAPH_OP_ENTITY_AUTOGUN:
		out->has_autogun = 1;
		heldParamString(ir, node, "target_filter",
			out->autogun_target_filter, sizeof(out->autogun_target_filter));
		heldParamString(ir, node, "team_policy", out->autogun_team_policy,
			sizeof(out->autogun_team_policy));
		heldParamString(ir, node, "net_authority",
			out->autogun_net_authority, sizeof(out->autogun_net_authority));
		heldParamBool(ir, node, "friendly_fire_suppression",
			&out->autogun_friendly_fire_suppression);
		heldParamBool(ir, node, "pickup_recover",
			&out->autogun_pickup_recover);
		heldParamFloat(ir, node, "aim_distance", &out->autogun_aim_distance);
		heldParamFloat(ir, node, "turn_speed", &out->autogun_turn_speed);
		if (heldParamFloat(ir, node, "fire_cadence", &f)) {
			out->autogun_fire_cadence = f;
		}
		heldParamInt(ir, node, "alternate_muzzles",
			&out->autogun_alternate_muzzles);
		heldParamIntAlias(ir, node, "beam_interval",
			"beam_interval_ticks60", &out->autogun_beam_interval_ticks60);
		heldParamInt(ir, node, "ammo_reserve", &out->autogun_ammo_reserve);
		break;
	case WEAPON_GRAPH_OP_ENTITY_STICKY_DEVICE:
		out->has_sticky_device = 1;
		heldParamString(ir, node, "attachment_filter",
			out->sticky_attachment_filter,
			sizeof(out->sticky_attachment_filter));
		heldParamString(ir, node, "mission_behavior_ref",
			out->mission_behavior_ref, sizeof(out->mission_behavior_ref));
		heldParamString(ir, node, "pickup_policy",
			out->sticky_pickup_policy, sizeof(out->sticky_pickup_policy));
		heldParamString(ir, node, "disable_policy",
			out->sticky_disable_policy, sizeof(out->sticky_disable_policy));
		heldParamString(ir, node, "visible_state",
			out->sticky_visible_state, sizeof(out->sticky_visible_state));
		/* c3849 Wave 5 Unit 7: derived latches (B3); 0 = OG. The propobj.c
		 * stick gate / pickup gate / landing arm consume only these. */
		out->sticky_attachment_bg_only =
			strcmp(out->sticky_attachment_filter, "background_only") == 0 ? 1 : 0;
		out->sticky_pickup_none =
			strcmp(out->sticky_pickup_policy, "none") == 0 ? 1 : 0;
		out->sticky_disable_shootable =
			strcmp(out->sticky_disable_policy, "shootable") == 0 ? 1 : 0;
		out->sticky_visible_hidden =
			strcmp(out->sticky_visible_state, "hidden") == 0 ? 1 : 0;
		/* mission_behavior_ref: scenario-owned per the module spec;
		 * registration-time existence validation only (Unit 1c did not
		 * cover it). */
		if (out->mission_behavior_ref[0] &&
				!assetCatalogResolve(out->mission_behavior_ref)) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.PARSE: sticky_device mission_behavior_ref '%s' does not resolve in the catalog (validation only; behavior unchanged)",
				out->mission_behavior_ref);
		}
		break;
	case WEAPON_GRAPH_OP_ENTITY_OWNER_CLEANUP:
		out->has_owner_cleanup = 1;
		heldParamString(ir, node, "owner_lost_behavior",
			out->owner_lost_behavior, sizeof(out->owner_lost_behavior));
		heldParamString(ir, node, "owner_death_behavior",
			out->owner_death_behavior, sizeof(out->owner_death_behavior));
		heldParamString(ir, node, "replace_existing_policy",
			out->replace_existing_policy, sizeof(out->replace_existing_policy));
		heldParamInt(ir, node, "max_active_per_owner",
			&out->max_active_per_owner);
		break;
	case WEAPON_GRAPH_OP_ENTITY_INTERACTION:
		out->has_interaction = 1;
		heldParamString(ir, node, "interact_filter", out->interact_filter,
			sizeof(out->interact_filter));
		heldParamString(ir, node, "action", out->interaction_action,
			sizeof(out->interaction_action));
		heldParamString(ir, node, "prompt_ref", out->prompt_ref,
			sizeof(out->prompt_ref));
		heldResolveSfxParam(ir, node, "sound", &out->interaction_sound);
		heldParamString(ir, node, "transfer_payload", out->transfer_payload,
			sizeof(out->transfer_payload));
		break;
	default:
		break;
	}
}

static s32 heldModeToFuncIndex(const char *mode)
{
	if (!mode) return -1;
	if (strcmp(mode, "primary") == 0) return 0;
	if (strcmp(mode, "secondary") == 0) return 1;
	return -1;
}

static s32 heldOpcodeIsSupported(weapon_graph_opcode_e opcode)
{
	switch (opcode) {
	case WEAPON_GRAPH_OP_FUNCTION_EMPTY:
	case WEAPON_GRAPH_OP_FIRE_HITSCAN:
	case WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE:
	case WEAPON_GRAPH_OP_FIRE_BURST:
	case WEAPON_GRAPH_OP_FIRE_CHARGE_RELEASE:
	case WEAPON_GRAPH_OP_FIRE_BEAM_TICK:
	case WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE:
	case WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL:
	case WEAPON_GRAPH_OP_MELEE_STRIKE:
	case WEAPON_GRAPH_OP_SPECIAL_REMOTE_DETONATOR:
	case WEAPON_GRAPH_OP_SPECIAL_COMBAT_BOOST:
	case WEAPON_GRAPH_OP_SPECIAL_WEAPON_STATE:
	case WEAPON_GRAPH_OP_DEVICE_ACTIVATE:
	case WEAPON_GRAPH_OP_PRESENTATION_WEAPON_VISIBILITY:
	case WEAPON_GRAPH_OP_PRESENTATION_RETICLE_OVERLAY_CAMERA:
		return 1;
	default:
		return 0;
	}
}

static void heldFunctionFromNode(const weapon_graph_ir_t *ir,
                                 const weapon_graph_ir_node_t *node,
                                 const char *mode_hint,
                                 weapon_graph_held_function_t *out)
{
	s32 v;
	u32 uv;
	f32 f;
	memset(out, 0, sizeof(*out));
	out->valid = 1;
	out->opcode = node->opcode;
	out->ammo_slot = -1;
	copyStr(out->parity_module, sizeof(out->parity_module),
		weaponGraphParityModuleNameForOpcode(node->opcode));
	copyStr(out->node_id, sizeof(out->node_id), node->id);
	if (!heldParamString(ir, node, "mode", out->mode, sizeof(out->mode)) &&
			mode_hint) {
		copyStr(out->mode, sizeof(out->mode), mode_hint);
	}
	heldParamString(ir, node, "function_type", out->function_type,
		sizeof(out->function_type));
	out->function_type_id = heldFunctionTypeId(out->function_type);
	out->has_function_type_id = out->function_type[0] ? 1 : 0;
	heldParamString(ir, node, "trigger_policy", out->trigger_policy,
		sizeof(out->trigger_policy));

	if (heldParamInt(ir, node, "ammo_slot", &v)) out->ammo_slot = v;
	if (heldParamU32(ir, node, "flags", &uv)) out->flags = uv;
	if (heldParamInt(ir, node, "burst_count", &v)) {
		out->has_burst_count = 1;
		out->burst_count = v;
	}
	if (heldParamFloat(ir, node, "damage", &f)) {
		out->has_damage = 1;
		out->damage = f;
	}
	if (heldParamFloat(ir, node, "spread", &f)) {
		out->has_spread = 1;
		out->spread = f;
	}
	if (heldParamInt(ir, node, "recoil_anim_unk24", &v)) {
		out->has_recoil_anim_unk24 = 1;
		out->recoil_anim_unk24 = v;
	}
	if (heldParamInt(ir, node, "recoil_anim_unk25", &v)) {
		out->has_recoil_anim_unk25 = 1;
		out->recoil_anim_unk25 = v;
	}
	if (heldParamInt(ir, node, "recoil_anim_unk26", &v)) {
		out->has_recoil_anim_unk26 = 1;
		out->recoil_anim_unk26 = v;
	}
	if (heldParamInt(ir, node, "recoil_anim_unk27", &v)) {
		out->has_recoil_anim_unk27 = 1;
		out->recoil_anim_unk27 = v;
	}
	if (heldParamFloat(ir, node, "recoildist", &f)) {
		out->has_recoildist = 1;
		out->recoildist = f;
	}
	if (heldParamFloat(ir, node, "recoilangle", &f)) {
		out->has_recoilangle = 1;
		out->recoilangle = f;
	}
	if (heldParamFloat(ir, node, "slidemax", &f)) {
		out->has_slidemax = 1;
		out->slidemax = f;
	}
	if (heldParamFloat(ir, node, "impactforce", &f)) {
		out->has_impactforce = 1;
		out->impactforce = f;
	}
	if (heldParamInt(ir, node, "duration_ticks60", &v)) {
		out->has_duration_ticks60 = 1;
		if (v < 0) v = 0;
		if (v > 255) v = 255;
		out->duration_ticks60 = (u8)v;
	}
	if (heldResolveSfxParam(ir, node, "shoot_sound_catalog_id", &v) ||
			heldResolveSfxParam(ir, node, "shoot_sound_ref", &v) ||
			heldResolveSfxParam(ir, node, "shootsound", &v)) {
		out->has_shootsound = 1;
		if (v < 0) v = 0;
		if (v > 65535) v = 65535;
		out->shootsound = (u16)v;
	}
	if (heldParamInt(ir, node, "penetration", &v)) {
		out->has_penetration = 1;
		if (v < 0) v = 0;
		if (v > 255) v = 255;
		out->penetration = (u8)v;
	}
	if (heldParamFloat(ir, node, "initial_rpm", &f)) {
		out->has_initial_rpm = 1;
		out->initial_rpm = f;
	}
	if (heldParamFloat(ir, node, "max_rpm", &f)) {
		out->has_max_rpm = 1;
		out->max_rpm = f;
	}
	if (heldParamInt(ir, node, "turret_accel", &v)) {
		out->has_turret_accel = 1;
		out->turret_accel = v;
	}
	if (heldParamInt(ir, node, "turret_decel", &v)) {
		out->has_turret_decel = 1;
		out->turret_decel = v;
	}
	if (heldParamInt(ir, node, "recoverytime_ticks60", &v)) {
		out->has_recoverytime_ticks60 = 1;
		out->recoverytime_ticks60 = v;
	}
	heldParamString(ir, node, "projectile_ref",
		out->projectile_ref, sizeof(out->projectile_ref));
	heldParamString(ir, node, "entity_ref",
		out->entity_ref, sizeof(out->entity_ref));
	heldParamString(ir, node, "payload_ref",
		out->payload_ref, sizeof(out->payload_ref));
	if ((heldParamString(ir, node, "projectile_model_catalog_id",
			out->projectile_model_ref,
			sizeof(out->projectile_model_ref)) ||
			heldParamString(ir, node, "projectile_model_ref",
			out->projectile_model_ref,
			sizeof(out->projectile_model_ref))) &&
			heldResolveProjectileModelRef(out->projectile_model_ref, &v)) {
		out->has_projectile_modelnum = 1;
		out->projectile_modelnum = v;
	}
	if (heldParamFloat(ir, node, "scale", &f)) {
		out->has_scale = 1;
		out->scale = f;
	}
	if (heldParamFloat(ir, node, "speed", &f)) {
		out->has_speed = 1;
		out->speed = f;
	}
	if (heldParamInt(ir, node, "travel_distance", &v)) {
		out->has_travel_distance = 1;
		out->travel_distance = v;
	}
	if (heldParamInt(ir, node, "timer_ticks60", &v)) {
		out->has_timer_ticks60 = 1;
		out->timer_ticks60 = v;
	}
	if (heldParamFloat(ir, node, "reflect_angle", &f)) {
		out->has_reflect_angle = 1;
		out->reflect_angle = f;
	}
	if (heldResolveSfxParam(ir, node, "projectile_sound_catalog_id", &v) ||
			heldResolveSfxParam(ir, node, "special_sound_catalog_id", &v) ||
			heldResolveSfxParam(ir, node, "sound_catalog_id", &v) ||
			heldResolveSfxParam(ir, node, "soundnum", &v)) {
		out->has_soundnum = 1;
		out->soundnum = v;
	}
	if (heldParamInt(ir, node, "activation_time_ticks60", &v)) {
		out->has_activation_time_ticks60 = 1;
		out->activation_time_ticks60 = v;
	}
	if (heldParamInt(ir, node, "recovery_time_ticks60", &v)) {
		out->has_recovery_time_ticks60 = 1;
		out->recovery_time_ticks60 = v;
	}
	if (heldParamFloat(ir, node, "range", &f)) {
		out->has_range = 1;
		out->range = f;
	}
	if (heldParamInt(ir, node, "specialfunc", &v)) {
		out->has_specialfunc = 1;
		out->specialfunc = v;
	}
	if (heldParamU32(ir, node, "device", &uv)) {
		out->has_device = 1;
		out->device = uv;
	}
	{
		char sight[32];
		if (heldParamInt(ir, node, "sight", &v)) {
			out->has_sight = 1;
			out->sight = (u32)v;
		} else if ((heldParamString(ir, node, "sight", sight, sizeof(sight)) ||
				heldParamString(ir, node, "sight_type", sight, sizeof(sight))) &&
				(v = heldSightId(sight)) >= 0) {
			out->has_sight = 1;
			out->sight = (u32)v;
		}
	}
	if (heldParamFloatAlias(ir, node, "zoom_fov", "zoom_fovy", &f)) {
		out->has_zoom_fov = 1;
		out->zoom_fov = f;
	}
	heldParamString(ir, node, "reticle_ref", out->reticle_ref,
		sizeof(out->reticle_ref));
	heldParamString(ir, node, "overlay_ref", out->overlay_ref,
		sizeof(out->overlay_ref));
	heldParamString(ir, node, "camera_effect", out->camera_effect,
		sizeof(out->camera_effect));
	/* c3849 Wave 5f Unit 9: latch camera_effect to an s32 enum at parse so
	 * the bgunTick vision arm never strcmps per tick (B3). Unknown values
	 * get a one-time LOG_NOTE and stay NONE. */
	out->camera_effect_mode = WEAPON_GRAPH_CAMERA_EFFECT_NONE;
	if (out->camera_effect[0]) {
		if (strcmp(out->camera_effect, "xray") == 0) {
			out->camera_effect_mode = WEAPON_GRAPH_CAMERA_EFFECT_XRAY;
		} else if (strcmp(out->camera_effect, "none") != 0) {
			static s32 s_noted_camera_effect = 0;
			if (!s_noted_camera_effect) {
				s_noted_camera_effect = 1;
				sysLogPrintf(LOG_NOTE,
					"WEAPONGRAPH.PARSE: camera_effect '%s' has no backend yet; latching none",
					out->camera_effect);
			}
		}
	}
}

static f32 weaponGraphSettingNumber(const weapon_graph_setting_entry_t *e)
{
	if (e->type == WEAPON_GRAPH_PARAM_FLOAT) return e->f_value;
	if (e->type == WEAPON_GRAPH_PARAM_INT) return (f32)e->i_value;
	return 0.0f;
}

/* fire_cadence -> rpm (canonical unit per B6.6; absent unit = rpm). Explicit
 * period units convert: centiseconds (6000/v, the needler authoring) and
 * ticks60 (3600/v). Unknown units latch 0 loudly (no mapping). */
static f32 weaponGraphSettingCadenceRpm(const weapon_graph_setting_entry_t *e)
{
	f32 v = weaponGraphSettingNumber(e);
	if (v <= 0.0f) {
		return 0.0f;
	}
	if (!e->unit[0] || strcmp(e->unit, "rpm") == 0) {
		return v;
	}
	if (strcmp(e->unit, "centiseconds") == 0) {
		return 6000.0f / v;
	}
	if (strcmp(e->unit, "ticks60") == 0) {
		return 3600.0f / v;
	}
	policyLatchUnknown("fire_cadence unit", e->unit, 0);
	return 0.0f;
}

/* c3849 Wave 5f Unit 9: defaults layering. After held node param capture
 * completes for a weapon's functions, settings rows fill ONLY fields whose
 * has_* bit is still 0 -- node params always win. Fixed key map, conservative:
 * - spread     -> has_spread/spread
 * - zoom_fov   -> has_zoom_fov/zoom_fov
 * - sight      -> has_sight/sight (int or heldSightId string)
 * - fire_cadence (rpm) -> max_rpm for fire.auto_cadence functions ONLY
 *   (weaponGetNumTicksPerShot + the bondgun auto path consume it); for every
 *   other function it converts to recoverytime_ticks60 (3600/rpm), the
 *   cadence field the bondgun recovery sites consume. It must never set
 *   has_max_rpm on a non-auto function: bondgun.c classifies a function as
 *   automatic by exactly that bit. initial_rpm is deliberately NOT filled --
 *   one rpm number cannot express a spin-up curve and the consumer falls back
 *   gracefully.
 * Base archives carry zero tunables, so this layer is empty for base weapons
 * (asserted in tests). */
static void weaponGraphApplySettingsDefaults(s32 weaponnum)
{
	const weapon_graph_weapon_settings_t *table;

	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) {
		return;
	}
	table = &s_weapon_settings[weaponnum];
	if (!table->valid || table->setting_count <= 0) {
		return;
	}

	for (s32 func = 0; func < WEAPON_GRAPH_RUNTIME_MAX_FUNCS; func++) {
		weapon_graph_held_function_t *held = &s_held_functions[weaponnum][func];
		if (!held->valid) {
			continue;
		}
		for (s32 i = 0; i < table->setting_count; i++) {
			const weapon_graph_setting_entry_t *e = &table->settings[i];
			s32 numeric = e->type == WEAPON_GRAPH_PARAM_FLOAT ||
				e->type == WEAPON_GRAPH_PARAM_INT;
			if (strcmp(e->key, "spread") == 0) {
				if (!held->has_spread && numeric) {
					held->has_spread = 1;
					held->spread = weaponGraphSettingNumber(e);
				}
			} else if (strcmp(e->key, "zoom_fov") == 0) {
				if (!held->has_zoom_fov && numeric) {
					held->has_zoom_fov = 1;
					held->zoom_fov = weaponGraphSettingNumber(e);
				}
			} else if (strcmp(e->key, "sight") == 0) {
				if (!held->has_sight) {
					if (e->type == WEAPON_GRAPH_PARAM_INT) {
						held->has_sight = 1;
						held->sight = (u32)e->i_value;
					} else if (e->type == WEAPON_GRAPH_PARAM_STRING) {
						s32 id = heldSightId(e->value);
						if (id >= 0) {
							held->has_sight = 1;
							held->sight = (u32)id;
						}
					}
				}
			} else if (strcmp(e->key, "fire_cadence") == 0) {
				f32 rpm = weaponGraphSettingCadenceRpm(e);
				if (rpm > 0.0f) {
					if (held->opcode == WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE) {
						if (!held->has_max_rpm) {
							held->has_max_rpm = 1;
							held->max_rpm = rpm;
						}
					} else if (!held->has_recoverytime_ticks60) {
						s32 ticks = (s32)((3600.0f / rpm) + 0.5f);
						if (ticks < 1) {
							ticks = 1;
						}
						held->has_recoverytime_ticks60 = 1;
						held->recoverytime_ticks60 = ticks;
					}
				}
			}
		}
	}
}

s32 weaponGraphRuntimeRegisterHeldIr(s32 weaponnum, const weapon_graph_ir_t *ir,
                                     char *err, size_t err_cap)
{
	s32 registered = 0;
	if (!heldIndexValid(weaponnum, 0) || !ir) {
		setErr(err, err_cap, "held IR registration called with invalid input");
		return -1;
	}
	if (ir->asset_type != ASSET_WEAPON) {
		setErr(err, err_cap, "held IR registration requires a weapon graph");
		return -1;
	}

	weaponGraphRuntimeClearWeapon(weaponnum);

	for (s32 i = 0; i < ir->export_count; i++) {
		const char *name = ir->exports[i].name;
		s32 funcindex = heldModeToFuncIndex(name);
		s32 node_index = ir->exports[i].node;
		if (funcindex < 0 || node_index < 0 || node_index >= ir->node_count) {
			continue;
		}
		const weapon_graph_ir_node_t *node = &ir->nodes[node_index];
		if (!heldOpcodeIsSupported(node->opcode)) continue;
		heldFunctionFromNode(ir, node, name,
			&s_held_functions[weaponnum][funcindex]);
		registered++;
	}

	for (s32 i = 0; i < ir->node_count; i++) {
		char mode[16];
		s32 funcindex;
		if (!heldOpcodeIsSupported(ir->nodes[i].opcode)) continue;
		if (!heldParamString(ir, &ir->nodes[i], "mode", mode, sizeof(mode))) {
			continue;
		}
		funcindex = heldModeToFuncIndex(mode);
		if (funcindex < 0 ||
				s_held_functions[weaponnum][funcindex].valid) {
			continue;
		}
		heldFunctionFromNode(ir, &ir->nodes[i], mode,
			&s_held_functions[weaponnum][funcindex]);
		registered++;
	}

	if (registered == 0) {
		setErr(err, err_cap, "weapon graph contains no held function exports");
		return -1;
	}
	return 0;
}

/* B-911 (c3848): register a nested payload's embedded .pdmesh dependencies as
 * mod-category ASSET_MODEL rows on catalog-owned private custom slots, BEFORE
 * the payload's IR is registered, so the graph's catalog-ID model_ref resolves
 * to a usable g_ModelStates index (heldResolveProjectileModelRef ->
 * catalogResolveModel -> entry->runtime_index). The bound source handle is a
 * multi-level "::" member chain into the loose on-disk weapon archive, which
 * fsLoadNestedArchiveEntry resolves recursively (fs.c) -- the same mechanism
 * the .pdbody mesh chain ships with, one level deeper. Failures are loud but
 * non-fatal: the weapon still registers minus its custom model, which is the
 * pre-B-911 behavior. The private slot never crosses wire/save/manifest/UI. */
static void s_registerEmbeddedMeshDeps(const char *archive_path,
		const char *container_entry, const void *container_bytes,
		u32 container_size, const char *parent_id,
		s32 bind_weapon_model_source)
{
	weapon_graph_embedded_mesh_t meshes[WEAPON_GRAPH_EMBEDDED_MESH_MAX];
	s32 count = weaponGraphArchiveScanEmbeddedMeshesBytes(container_bytes,
		container_size, parent_id, meshes, WEAPON_GRAPH_EMBEDDED_MESH_MAX);

	for (s32 i = 0; i < count; i++) {
		const weapon_graph_embedded_mesh_t *mesh = &meshes[i];

		if (strcmp(mesh->catalog_id, parent_id) == 0) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.MESH.INGEST: embedded mesh %s reuses parent id %s; skipped",
				mesh->archive_entry, parent_id);
			continue;
		}

		asset_entry_t *e = assetCatalogGetMutable(mesh->catalog_id);
		if (e && e->type != ASSET_MODEL) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.MESH.INGEST: catalog id %s is type=%d, not ASSET_MODEL; skipped",
				mesh->catalog_id, (s32)e->type);
			continue;
		}

		if (!e) {
			e = assetCatalogRegister(mesh->catalog_id, ASSET_MODEL);
		}
		if (!e) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.MESH.INGEST: catalog register failed for %s",
				mesh->catalog_id);
			continue;
		}

		s32 slot = assetCatalogResolveModelPrivateSlot(mesh->catalog_id);
		if (slot < 0) {
			/* Allocator already logged CATALOG.MODEL.CUSTOM_SLOT_FAIL. */
			continue;
		}
		e->runtime_index = slot;

		/* Mod-category, never base/bundled: the row must be evicted by
		 * assetCatalogClearMods like every other mod asset. A fresh register
		 * already defaults bundled=0; inherit the owning weapon's category. */
		const asset_entry_t *parent = assetCatalogResolve(parent_id);
		if (parent && parent->category[0]) {
			copyStr(e->category, sizeof(e->category), parent->category);
		}

		char source_path[FS_MAXPATH + 1];
		int written = container_entry && container_entry[0]
			? snprintf(source_path, sizeof(source_path),
				"%s::%s::%s::%s", archive_path, container_entry,
				mesh->archive_entry, mesh->geometry)
			: snprintf(source_path, sizeof(source_path),
				"%s::%s::%s", archive_path, mesh->archive_entry,
				mesh->geometry);
		if (written < 0 || (size_t)written >= sizeof(source_path)) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.MESH.INGEST: source path for %s exceeds FS_MAXPATH; mesh not bound",
				mesh->catalog_id);
			continue;
		}

		if (bind_weapon_model_source || e->source.primary.provider == NULL) {
			catalogSetPrimaryFile(e, source_path);
		}

		if (bind_weapon_model_source) {
			s32 source_filenum = assetCatalogModelPrivateSourceFilenum(slot);
			if (source_filenum > 0) {
				asset_entry_t *parent = assetCatalogGetMutable(parent_id);
				e->source_filenum = source_filenum;
				if (parent && parent->type == ASSET_WEAPON
						&& parent->source_filenum <= 0) {
					parent->source_filenum = source_filenum;
				}
			}
		}

		sysLogPrintf(LOG_NOTE,
			"WEAPONGRAPH.MESH.INGEST: id=%s slot=%d filenum=%d source=%s",
			mesh->catalog_id, slot, e->source_filenum, source_path);
	}
}

/* c3849 Unit 8: embedded .pdeffect discovery inside nested payload bytes.
 * The needler proving asset nests its pink effect one level down
 * (secondary.pdprojectile -> dependencies/assets/effects/pink_burst.pdeffect),
 * the same place B-911 finds embedded meshes. Effects key by asset_id string
 * in the effect runtime (no slot allocator), so ingestion is registration
 * only. Failures are loud but non-fatal at this depth (mesh-ingest
 * precedent): the weapon still registers and the effect bridges keep their
 * OG fallbacks. */

#define WEAPON_GRAPH_EMBEDDED_EFFECT_MAX 8

typedef struct {
	char names[WEAPON_GRAPH_EMBEDDED_EFFECT_MAX][FS_MAXPATH];
	s32 count;
	s32 overflow;
} embedded_effect_name_scan_t;

static s32 effectSuffixMatches(const char *name)
{
	static const char suffix[] = ".pdeffect";
	size_t nlen;
	size_t slen = sizeof(suffix) - 1;
	if (!name) return 0;
	nlen = strlen(name);
	if (nlen < slen) return 0;
	name += nlen - slen;
	for (size_t i = 0; i < slen; i++) {
		if (tolower((unsigned char)name[i]) != suffix[i]) return 0;
	}
	return 1;
}

static s32 s_collectEmbeddedEffectNames(const char *entryName,
		u32 uncompressedSize, void *user)
{
	embedded_effect_name_scan_t *scan = (embedded_effect_name_scan_t *)user;
	(void)uncompressedSize;
	if (!effectSuffixMatches(entryName)) {
		return 0;
	}
	if (scan->count >= WEAPON_GRAPH_EMBEDDED_EFFECT_MAX) {
		scan->overflow = 1;
		return 0;
	}
	copyStr(scan->names[scan->count], sizeof(scan->names[0]), entryName);
	scan->count++;
	return 0;
}

static s32 effectSameNamespace(const char *a, const char *b)
{
	const char *ac = a ? strchr(a, ':') : NULL;
	const char *bc = b ? strchr(b, ':') : NULL;
	size_t alen;
	size_t blen;
	if (!ac || !bc || ac == a || bc == b) return 0;
	alen = (size_t)(ac - a);
	blen = (size_t)(bc - b);
	return alen == blen && strncmp(a, b, alen) == 0;
}

static void s_registerEmbeddedEffectDeps(const void *container_bytes,
		u32 container_size, const char *parent_id)
{
	embedded_effect_name_scan_t scan;
	memset(&scan, 0, sizeof(scan));
	if (modArchiveMemForEachEntry(container_bytes, container_size,
			s_collectEmbeddedEffectNames, &scan) != MODARCHIVE_OK) {
		return;
	}
	if (scan.overflow) {
		sysLogPrintf(LOG_WARNING,
			"WEAPONGRAPH.EFFECT.SCAN: more than %d embedded .pdeffect members; extras ignored",
			WEAPON_GRAPH_EMBEDDED_EFFECT_MAX);
	}

	for (s32 i = 0; i < scan.count; i++) {
		u32 effect_size = 0;
		void *effect_bytes = modArchiveExtractMemAlloc(container_bytes,
			container_size, scan.names[i], &effect_size);
		if (!effect_bytes || effect_size == 0) {
			free(effect_bytes);
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.EFFECT.SCAN: could not read embedded effect %s",
				scan.names[i]);
			continue;
		}

		weapon_graph_archive_descriptor_t desc;
		char err[256];
		err[0] = '\0';
		if (weaponGraphArchiveReadDescriptorBytes(effect_bytes, effect_size,
				ASSET_EFFECT, &desc, err, sizeof(err)) != 0) {
			free(effect_bytes);
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.EFFECT.SCAN: embedded effect %s has a bad effect.ini; skipped: %s",
				scan.names[i], err[0] ? err : "unknown error");
			continue;
		}
		if (desc.catalog_id[0] &&
				!effectSameNamespace(parent_id, desc.catalog_id)) {
			free(effect_bytes);
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.EFFECT.SCAN: embedded effect %s id %s is outside parent namespace %s; skipped",
				scan.names[i], desc.catalog_id, parent_id);
			continue;
		}

		err[0] = '\0';
		if (effectGraphRuntimeRegisterArchiveBytes(effect_bytes, effect_size,
				err, sizeof(err)) != 0) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.EFFECT.SCAN: embedded effect %s failed to register: %s",
				scan.names[i], err[0] ? err : "unknown error");
		} else {
			sysLogPrintf(LOG_NOTE,
				"WEAPONGRAPH.EFFECT.INGEST: id=%s source=%s",
				desc.catalog_id[0] ? desc.catalog_id : "(derived)",
				scan.names[i]);
		}
		free(effect_bytes);
	}
}

static s32 weaponGraphRuntimeRegisterWeaponArchiveDependencies(
	const char *archive_path,
	const char *parent_id,
	s32 owner_weapon,
	char *err, size_t err_cap)
{
	weapon_graph_archive_inventory_t inventory;
	char registered[WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS][CATALOG_ID_LEN];
	s32 registered_count = 0;
	mod_archive_t *arc;
	void *archive_bytes = NULL;
	u32 archive_size = 0;
	s32 count;

	if (!archive_path || !archive_path[0] || !parent_id || !parent_id[0]) {
		return 0;
	}

	count = weaponGraphArchiveScanNestedPayloadsFile(archive_path, parent_id,
		&inventory, err, err_cap);
	if (count < 0) {
		return -1;
	}

	archive_bytes = weaponGraphArchiveReadBytesFile(archive_path, &archive_size);
	arc = archive_bytes ? NULL : modArchiveOpen(archive_path);
	if (!archive_bytes && !arc) {
		setErr(err, err_cap, "could not reopen weapon archive %s",
			archive_path);
		return -1;
	}

	if (archive_bytes && archive_size > 0) {
		s_registerEmbeddedMeshDeps(archive_path, "", archive_bytes,
			archive_size, parent_id, 1);
	}

	if (count == 0) {
		free(archive_bytes);
		if (arc) {
			modArchiveClose(arc);
		}
		return 0;
	}

	for (s32 i = 0; i < inventory.count; i++) {
		const weapon_graph_archive_payload_t *payload = &inventory.payloads[i];
		if (payload->type != ASSET_PROJECTILE &&
				payload->type != ASSET_ENTITY &&
				payload->type != ASSET_EFFECT) {
			continue;
		}

		u32 nested_size = 0;
		void *nested = archive_bytes
			? modArchiveExtractMemAlloc(archive_bytes, archive_size,
				payload->archive_entry, &nested_size)
			: NULL;
		if (!archive_bytes) {
			s32 idx = modArchiveFindEntry(arc, payload->archive_entry);
			if (idx < 0) {
				setErr(err, err_cap, "nested payload %s disappeared from %s",
					payload->archive_entry, archive_path);
				goto fail;
			}
			nested = modArchiveExtractAlloc(arc, idx, &nested_size);
		}
		if (!nested || nested_size == 0) {
			free(nested);
			setErr(err, err_cap, "could not read nested payload %s",
				payload->archive_entry);
			goto fail;
		}

		/* c3849 Unit 8: a top-level nested .pdeffect is a typed payload like
		 * projectile/entity and registers (loud-fail) into the effect runtime,
		 * keyed by asset_id string -- no slot allocator, no owner bits. */
		if (payload->type == ASSET_EFFECT) {
			if (effectGraphRuntimeRegisterArchiveBytes(nested, nested_size,
					err, err_cap) != 0) {
				free(nested);
				goto fail;
			}
			free(nested);
			if (registered_count < WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS &&
					payload->catalog_id[0]) {
				copyStr(registered[registered_count++],
					sizeof(registered[0]), payload->catalog_id);
			}
			continue;
		}

		/* B-911: register the payload's embedded mesh dependencies before the
		 * payload IR is compiled/registered, so its model_ref resolves to the
		 * custom slot during projectileRuntimeFromNode. */
		s_registerEmbeddedMeshDeps(archive_path, payload->archive_entry,
			nested, nested_size, parent_id, 0);

		/* c3849 Unit 8: register effects embedded one level deeper (inside
		 * the projectile/entity bytes), so the payload's impact spark and
		 * explosion refs resolve once the toggle is on. Loud, non-fatal. */
		s_registerEmbeddedEffectDeps(nested, nested_size, parent_id);

		weapon_graph_ir_t ir;
		if (weaponGraphCompileArchiveBytes(nested, nested_size, payload->type,
				&ir, err, err_cap) != 0) {
			free(nested);
			goto fail;
		}
		free(nested);

		if (payload->catalog_id[0] && ir.asset_id[0] &&
				strcmp(payload->catalog_id, ir.asset_id) != 0) {
			setErr(err, err_cap,
				"nested payload %s compiled as %s, expected %s",
				payload->archive_entry, ir.asset_id, payload->catalog_id);
			goto fail;
		}

		if (payload->type == ASSET_PROJECTILE &&
				projectileRuntimeFindIndex(ir.asset_id) >= 0) {
			ownerBitsSet(s_projectile_owner_bits[
				projectileRuntimeFindIndex(ir.asset_id)], owner_weapon);
		} else if (payload->type == ASSET_ENTITY &&
				entityRuntimeFindIndex(ir.asset_id) >= 0) {
			ownerBitsSet(s_entity_owner_bits[
				entityRuntimeFindIndex(ir.asset_id)], owner_weapon);
		} else if (weaponGraphRuntimeRegisterBehaviorIrOwned(&ir,
				owner_weapon, 0, err, err_cap) != 0) {
			goto fail;
		}
		if (registered_count < WEAPON_GRAPH_ARCHIVE_MAX_NESTED_PAYLOADS &&
				ir.asset_id[0]) {
			copyStr(registered[registered_count++],
				sizeof(registered[0]), ir.asset_id);
		}
	}

	free(archive_bytes);
	if (arc) {
		modArchiveClose(arc);
	}
	return 0;

fail:
	for (s32 i = 0; i < registered_count; i++) {
		/* c3849 Unit 8: effect records have no owner bits; clear by id
		 * (no-op for projectile/entity ids). */
		effectGraphRuntimeClearAsset(registered[i]);
		if (owner_weapon >= 0 &&
				owner_weapon < WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) {
			s32 projectile = projectileRuntimeFindIndex(registered[i]);
			if (projectile >= 0) {
				ownerBitsClear(s_projectile_owner_bits[projectile],
					owner_weapon);
				if (!s_projectile_external_refs[projectile] &&
						!ownerBitsAny(s_projectile_owner_bits[projectile])) {
					projectileRuntimeClearSlot(projectile);
				}
			}
			s32 entity = entityRuntimeFindIndex(registered[i]);
			if (entity >= 0) {
				ownerBitsClear(s_entity_owner_bits[entity], owner_weapon);
				if (!s_entity_external_refs[entity] &&
						!ownerBitsAny(s_entity_owner_bits[entity])) {
					entityRuntimeClearSlot(entity);
				}
			}
		} else {
			weaponGraphRuntimeClearAsset(registered[i]);
		}
	}
	free(archive_bytes);
	if (arc) {
		modArchiveClose(arc);
	}
	return -1;
}

/* c3849 Unit 1c (binding spec B1 rule 7): a weapon authoring BOTH a
 * projectile fuse timer and an entity timed_detonatable record would qualify
 * for two weaponTick arms; the projectile arm wins by chain position. The
 * check lives at the archive registration seam because it is the only parse
 * path where the held function and both behavior records are reliably
 * co-visible (the loose JSON path registers behavior graphs independently,
 * with no ordering guarantee). Non-gated accessors on purpose: registration
 * runs with the gameplay toggle in either state. */
static void weaponGraphWarnTimerOverlap(s32 weaponnum)
{
	for (s32 func = 0; func < WEAPON_GRAPH_RUNTIME_MAX_FUNCS; func++) {
		const weapon_graph_held_function_t *held =
			weaponGraphRuntimeGetHeldFunction(weaponnum, func);
		const weapon_graph_projectile_runtime_t *projectile;
		const weapon_graph_entity_runtime_t *entity;

		if (!held) {
			continue;
		}

		projectile = held->projectile_ref[0] ?
			weaponGraphRuntimeGetProjectile(held->projectile_ref) : NULL;
		entity = held->entity_ref[0] ?
			weaponGraphRuntimeGetEntity(held->entity_ref) :
			(held->payload_ref[0] ?
				weaponGraphRuntimeGetEntity(held->payload_ref) : NULL);

		if (projectile && projectile->has_timer &&
				entity && entity->has_timed_detonatable) {
			sysLogPrintf(LOG_WARNING,
				"WEAPONGRAPH.PARSE: weapon %d func %d authors both projectile.timer and entity.timed_detonatable; the projectile arm wins (B1 rule 7)",
				weaponnum, func);
		}
	}
}

s32 weaponGraphRuntimeRegisterWeaponArchive(s32 weaponnum,
                                            const char *archive_path,
                                            char *err, size_t err_cap)
{
	weapon_graph_ir_t ir;
	weapon_graph_archive_descriptor_t desc;
	weapon_graph_weapon_settings_t tunables;

	/* c3849 Wave 5f Unit 2: parse the tunables trio (settings/variables/
	 * presentation, via the B-917-fixed *_file descriptor aliases) BEFORE the
	 * compile so a malformed tunables file fails registration as loudly as a
	 * malformed graph. The compile activates the variables file itself for
	 * $name substitution (weaponGraphCompileArchiveFile). */
	if (weaponGraphArchiveReadDescriptorFile(archive_path, ASSET_WEAPON,
			&desc, err, err_cap) != 0) {
		return -1;
	}
	if (weaponGraphArchiveReadTunables(archive_path, &desc, &tunables,
			err, err_cap) != 0) {
		return -1;
	}
	if (weaponGraphCompileArchiveFile(archive_path, ASSET_WEAPON, &ir,
			err, err_cap) != 0) {
		return -1;
	}
	if (weaponGraphRuntimeRegisterHeldIr(weaponnum, &ir, err, err_cap) != 0) {
		return -1;
	}
	/* Store AFTER RegisterHeldIr: the ClearWeapon inside it wipes the slot.
	 * Then layer settings defaults onto held fields whose has_* is still 0. */
	if (heldIndexValid(weaponnum, 0)) {
		s_weapon_settings[weaponnum] = tunables;
		weaponGraphApplySettingsDefaults(weaponnum);
	}
	if (weaponGraphRuntimeRegisterWeaponArchiveDependencies(archive_path,
			ir.asset_id, weaponnum, err, err_cap) != 0) {
		weaponGraphRuntimeClearWeapon(weaponnum);
		return -1;
	}
	weaponGraphWarnTimerOverlap(weaponnum);
	return 0;
}

s32 weaponGraphRuntimeRegisterWeaponGraphJson(s32 weaponnum,
                                              const char *asset_id,
                                              const char *json,
                                              u32 json_size,
                                              char *err,
                                              size_t err_cap)
{
	weapon_graph_ir_t ir;
	if (weaponGraphCompileJson(ASSET_WEAPON, json, json_size, &ir,
			err, err_cap) != 0) {
		return -1;
	}
	if (asset_id && asset_id[0]) {
		if (ir.asset_id[0] && strcmp(ir.asset_id, asset_id) != 0) {
			setErr(err, err_cap, "graph asset_id %s does not match catalog %s",
				ir.asset_id, asset_id);
			return -1;
		}
		if (!ir.asset_id[0]) {
			copyStr(ir.asset_id, sizeof(ir.asset_id), asset_id);
			finalizeIrDigest(&ir);
		}
	}
	return weaponGraphRuntimeRegisterHeldIr(weaponnum, &ir, err, err_cap);
}

s32 weaponGraphRuntimeRegisterWeaponSourceJson(s32 weaponnum,
                                               const char *asset_id,
                                               const char *primary_json,
                                               u32 primary_size,
                                               const char *secondary_json,
                                               u32 secondary_size,
                                               const char *shared_json,
                                               u32 shared_size,
                                               const char *settings_json,
                                               u32 settings_size,
                                               const char *variables_json,
                                               u32 variables_size,
                                               const char *presentation_json,
                                               u32 presentation_size,
                                               char *err,
                                               size_t err_cap)
{
	weapon_graph_ir_t ir;
	weapon_graph_weapon_settings_t tunables;
	s32 result;

	/* c3849 Wave 5f Unit 2: loose-path mirror of the archive register. */
	if (weaponGraphParseWeaponTunables(settings_json, settings_size,
			variables_json, variables_size, presentation_json,
			presentation_size, &tunables, err, err_cap) != 0) {
		return -1;
	}
	/* B6.3: a PRESENT variables file (even an empty table) is an active
	 * substitution scope for this held compile. */
	if (variables_json && variables_size > 0) {
		s_compile_variables = &tunables;
	}
	result = weaponGraphCompileWeaponSourceJson(asset_id, primary_json,
		primary_size, secondary_json, secondary_size, shared_json,
		shared_size, &ir, err, err_cap);
	s_compile_variables = NULL;
	if (result != 0) {
		return -1;
	}
	if (weaponGraphRuntimeRegisterHeldIr(weaponnum, &ir, err, err_cap) != 0) {
		return -1;
	}
	if (heldIndexValid(weaponnum, 0)) {
		s_weapon_settings[weaponnum] = tunables;
		weaponGraphApplySettingsDefaults(weaponnum);
	}
	return 0;
}

static s32 weaponGraphRuntimeRegisterProjectileIrOwned(
	const weapon_graph_ir_t *ir,
	s32 owner_weapon,
	s32 external_ref,
	char *err,
	size_t err_cap)
{
	if (!ir || ir->asset_type != ASSET_PROJECTILE || !ir->asset_id[0]) {
		setErr(err, err_cap,
			"projectile IR registration requires a projectile graph with asset_id");
		return -1;
	}

	weapon_graph_projectile_runtime_t *runtime =
		projectileRuntimeAlloc(ir->asset_id);
	if (!runtime) {
		setErr(err, err_cap, "projectile runtime table is full");
		return -1;
	}

	runtime->valid = 1;
	runtimeCopyIrIdentity(ir,
		runtime->asset_id, sizeof(runtime->asset_id),
		runtime->graph_id, sizeof(runtime->graph_id),
		runtime->source_sha256, sizeof(runtime->source_sha256),
		runtime->ir_sha256, sizeof(runtime->ir_sha256));

	/* c3849 Unit 1c: derived-field defaults must hold even when the source
	 * node is absent (base-emitted graphs carry records with empty params,
	 * binding spec B3). Pre-set BEFORE the node loop; parse overrides. */
	runtime->impact_exptype = -1;
	runtime->trail_smoketype = -1;
	runtime->recover_weaponnum = -1;
	runtime->wall_fall_vec[0] = 0.0f;
	runtime->wall_fall_vec[1] = -10.0f;
	runtime->wall_fall_vec[2] = 0.0f;

	for (s32 i = 0; i < ir->node_count; i++) {
		projectileRuntimeFromNode(ir, &ir->nodes[i], runtime);
	}

	if (!runtime->model_ref[0] && !runtime->motion_kind[0] &&
			!runtime->has_homing && !runtime->has_fly_by_wire &&
			!runtime->has_wall_hugger && !runtime->has_transition_to_entity) {
		memset(runtime, 0, sizeof(*runtime));
		setErr(err, err_cap,
			"projectile graph contains no runtime projectile modules");
		return -1;
	}

	s32 index = projectileRuntimeFindIndex(ir->asset_id);
	if (index >= 0) {
		if (external_ref) {
			s_projectile_external_refs[index] = 1;
		}
		ownerBitsSet(s_projectile_owner_bits[index], owner_weapon);
	}

	return 0;
}

s32 weaponGraphRuntimeRegisterProjectileIr(const weapon_graph_ir_t *ir,
                                           char *err, size_t err_cap)
{
	return weaponGraphRuntimeRegisterProjectileIrOwned(ir, -1, 1,
		err, err_cap);
}

static s32 weaponGraphRuntimeRegisterEntityIrOwned(
	const weapon_graph_ir_t *ir,
	s32 owner_weapon,
	s32 external_ref,
	char *err,
	size_t err_cap)
{
	if (!ir || ir->asset_type != ASSET_ENTITY || !ir->asset_id[0]) {
		setErr(err, err_cap,
			"entity IR registration requires an entity graph with asset_id");
		return -1;
	}

	weapon_graph_entity_runtime_t *runtime = entityRuntimeAlloc(ir->asset_id);
	if (!runtime) {
		setErr(err, err_cap, "entity runtime table is full");
		return -1;
	}

	runtime->valid = 1;
	runtimeCopyIrIdentity(ir,
		runtime->asset_id, sizeof(runtime->asset_id),
		runtime->graph_id, sizeof(runtime->graph_id),
		runtime->source_sha256, sizeof(runtime->source_sha256),
		runtime->ir_sha256, sizeof(runtime->ir_sha256));

	/* c3849 Unit 1c: -1 sentinels BEFORE the heldParam reads. heldParam*
	 * assigns only when the key is present, so an absent bool param stays
	 * -1 (OG default) while an authored 0/1 is preserved -- the
	 * absent-vs-authored-0 disambiguation for the entity-deployed cluster.
	 * armed_exptype mirrors the projectile-side unresolved convention. */
	runtime->armed_exptype = -1;
	runtime->autogun_alternate_muzzles = -1;
	runtime->autogun_friendly_fire_suppression = -1;
	runtime->autogun_pickup_recover = -1;
	runtime->storm_delete_carrier = -1;
	/* c3849 Wave 5 Unit 7: absent max_active_per_owner must stay distinct
	 * from an authored 0 (0 denies the deployment at laptopDeploy). */
	runtime->max_active_per_owner = -1;

	for (s32 i = 0; i < ir->node_count; i++) {
		entityRuntimeFromNode(ir, &ir->nodes[i], runtime);
	}

	if (!runtime->has_armed_explosive && !runtime->has_proxy_trigger &&
			!runtime->has_remote_detonatable &&
			!runtime->has_timed_detonatable &&
			!runtime->has_nbomb_storm && !runtime->has_autogun &&
			!runtime->has_sticky_device && !runtime->has_owner_cleanup &&
			!runtime->has_interaction) {
		memset(runtime, 0, sizeof(*runtime));
		setErr(err, err_cap, "entity graph contains no runtime entity modules");
		return -1;
	}

	s32 index = entityRuntimeFindIndex(ir->asset_id);
	if (index >= 0) {
		if (external_ref) {
			s_entity_external_refs[index] = 1;
		}
		ownerBitsSet(s_entity_owner_bits[index], owner_weapon);
	}

	return 0;
}

s32 weaponGraphRuntimeRegisterEntityIr(const weapon_graph_ir_t *ir,
                                       char *err, size_t err_cap)
{
	return weaponGraphRuntimeRegisterEntityIrOwned(ir, -1, 1,
		err, err_cap);
}

static s32 weaponGraphRuntimeRegisterBehaviorIrOwned(const weapon_graph_ir_t *ir,
                                                     s32 owner_weapon,
                                                     s32 external_ref,
                                                     char *err, size_t err_cap)
{
	if (!ir) {
		setErr(err, err_cap, "behavior IR registration called with null input");
		return -1;
	}
	switch (ir->asset_type) {
	case ASSET_PROJECTILE:
		return weaponGraphRuntimeRegisterProjectileIrOwned(ir, owner_weapon,
			external_ref, err, err_cap);
	case ASSET_ENTITY:
		return weaponGraphRuntimeRegisterEntityIrOwned(ir, owner_weapon,
			external_ref, err, err_cap);
	default:
		setErr(err, err_cap, "unsupported behavior graph runtime type %d",
			(s32)ir->asset_type);
		return -1;
	}
}

static s32 weaponGraphRuntimeRegisterBehaviorIr(const weapon_graph_ir_t *ir,
                                                char *err, size_t err_cap)
{
	return weaponGraphRuntimeRegisterBehaviorIrOwned(ir, -1, 1,
		err, err_cap);
}

s32 weaponGraphRuntimeRegisterBehaviorGraphJson(asset_type_e graph_type,
                                                const char *asset_id,
                                                const char *json,
                                                u32 json_size,
                                                char *err,
                                                size_t err_cap)
{
	weapon_graph_ir_t ir;
	if (weaponGraphCompileJson(graph_type, json, json_size, &ir,
			err, err_cap) != 0) {
		return -1;
	}
	if (asset_id && asset_id[0]) {
		if (ir.asset_id[0] && strcmp(ir.asset_id, asset_id) != 0) {
			setErr(err, err_cap, "graph asset_id %s does not match catalog %s",
				ir.asset_id, asset_id);
			return -1;
		}
		if (!ir.asset_id[0]) {
			copyStr(ir.asset_id, sizeof(ir.asset_id), asset_id);
			finalizeIrDigest(&ir);
		}
	}
	return weaponGraphRuntimeRegisterBehaviorIr(&ir, err, err_cap);
}

s32 weaponGraphRuntimeRegisterBehaviorArchive(asset_type_e graph_type,
                                              const char *archive_path,
                                              char *err, size_t err_cap)
{
	weapon_graph_ir_t ir;
	if (graph_type != ASSET_PROJECTILE && graph_type != ASSET_ENTITY) {
		setErr(err, err_cap, "behavior archive type must be projectile or entity");
		return -1;
	}
	if (weaponGraphCompileArchiveFile(archive_path, graph_type, &ir,
			err, err_cap) != 0) {
		return -1;
	}
	return weaponGraphRuntimeRegisterBehaviorIr(&ir, err, err_cap);
}
