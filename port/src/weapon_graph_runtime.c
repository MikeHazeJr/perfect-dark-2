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

#include "config.h"
#include "constants.h"
#include "loader_enum_reverse.h"
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

static s32 s_runtime_enabled = 0;
static weapon_graph_held_function_t
	s_held_functions[WEAPON_GRAPH_RUNTIME_MAX_WEAPONS][WEAPON_GRAPH_RUNTIME_MAX_FUNCS];

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
};

#ifndef PD_TESTS
PD_CONSTRUCTOR static void weaponGraphRuntimeConfigInit(void)
{
	configRegisterInt("Debug.WeaponGraphRuntime", &s_runtime_enabled, 0, 1);
}
#endif

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

s32 weaponGraphRuntimeEnabled(void)
{
	return s_runtime_enabled ? 1 : 0;
}

void weaponGraphRuntimeSetEnabled(s32 enabled)
{
	s_runtime_enabled = enabled ? 1 : 0;
}

static s32 heldIndexValid(s32 weaponnum, s32 funcindex)
{
	return weaponnum >= 0 &&
		weaponnum < WEAPON_GRAPH_RUNTIME_MAX_WEAPONS &&
		funcindex >= 0 &&
		funcindex < WEAPON_GRAPH_RUNTIME_MAX_FUNCS;
}

void weaponGraphRuntimeClearWeapon(s32 weaponnum)
{
	if (weaponnum < 0 || weaponnum >= WEAPON_GRAPH_RUNTIME_MAX_WEAPONS) return;
	memset(s_held_functions[weaponnum], 0, sizeof(s_held_functions[weaponnum]));
}

void weaponGraphRuntimeClearAll(void)
{
	memset(s_held_functions, 0, sizeof(s_held_functions));
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

static s32 graphNodeIndex(const weapon_graph_ir_t *ir, const char *id)
{
	if (!ir || !id || !id[0]) return -1;
	for (s32 i = 0; i < ir->node_count; i++) {
		if (strcmp(ir->nodes[i].id, id) == 0) return i;
	}
	return -1;
}

static s32 keyNeedsUnit(const char *key)
{
	if (!key || !key[0] || startsWith(key, "runtime_")) return 0;
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
		if (paramIsCatalogRefKey(p->key) && p->value[0] &&
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
	hashS32(&ctx, ir->node_count);
	for (s32 i = 0; i < ir->node_count; i++) {
		const weapon_graph_ir_node_t *n = &ir->nodes[i];
		hashString(&ctx, n->id);
		hashString(&ctx, n->kind);
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
		setErr(err, err_cap, "graph schema must be %s", schema_expected);
		return -1;
	}
	jsonObjectString(root, "asset_id", out->asset_id, sizeof(out->asset_id));
	jsonObjectString(root, "graph_id", out->graph_id, sizeof(out->graph_id));
	if (!out->graph_id[0]) {
		setErr(err, err_cap, "graph missing graph_id");
		return -1;
	}
	if (out->asset_id[0] && !catalogRefLooksValid(out->asset_id)) {
		setErr(err, err_cap, "asset_id must be a catalog id, got %s",
			out->asset_id);
		return -1;
	}

	sha256Hash(json, json_size, digest);
	sha256ToHex(digest, out->source_sha256);

	if (compileNodes(graph_type, root, out, err, err_cap) != 0) return -1;
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

s32 weaponGraphCompileArchiveFile(const char *archive_path,
                                  asset_type_e graph_type,
                                  weapon_graph_ir_t *out,
                                  char *err, size_t err_cap)
{
	weapon_graph_archive_descriptor_t desc;
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
	if (!desc.behavior_graph[0]) {
		setErr(err, err_cap, "%s has no behavior_graph", archive_path);
		return -1;
	}
	if (weaponGraphArchiveReadTextFile(archive_path, desc.behavior_graph,
			&graph, &graph_size) != 0) {
		setErr(err, err_cap, "%s missing graph entry %s",
			archive_path, desc.behavior_graph);
		return -1;
	}
	result = weaponGraphCompileJson(graph_type, graph, graph_size, out,
		err, err_cap);
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
	if (heldResolveSfxParam(ir, node, "shootsound", &v)) {
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
	if (heldParamString(ir, node, "projectile_model_ref",
			out->projectile_model_ref, sizeof(out->projectile_model_ref)) &&
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
	if (heldResolveSfxParam(ir, node, "soundnum", &v)) {
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

s32 weaponGraphRuntimeRegisterWeaponArchive(s32 weaponnum,
                                            const char *archive_path,
                                            char *err, size_t err_cap)
{
	weapon_graph_ir_t ir;
	if (weaponGraphCompileArchiveFile(archive_path, ASSET_WEAPON, &ir,
			err, err_cap) != 0) {
		return -1;
	}
	return weaponGraphRuntimeRegisterHeldIr(weaponnum, &ir, err, err_cap);
}
