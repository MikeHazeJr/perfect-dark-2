/**
 * prop_graph_runtime.c -- executable public .pdprop behavior graphs.
 *
 * The editor-facing JSON compiles through the shared deterministic graph IR.
 * This module retains only compiled source and per-instance engine pointers;
 * it never creates a second authored representation. Unknown modules and bad
 * required parameters reject the parent asset during catalog hydration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "prop_graph_runtime.h"
#include "weapon_graph_runtime.h"

typedef struct prop_graph_asset {
	char asset_id[CATALOG_ID_LEN];
	weapon_graph_ir_t ir;
} prop_graph_asset_t;

typedef struct prop_graph_instance {
	char asset_id[CATALOG_ID_LEN];
	u32 forge_uid;
	prop_graph_runtime_target_t target;
} prop_graph_instance_t;

static prop_graph_asset_t *s_assets;
static s32 s_asset_count;
static s32 s_asset_capacity;
static prop_graph_instance_t *s_instances;
static s32 s_instance_count;
static s32 s_instance_capacity;

static void setErr(char *err, size_t cap, const char *message)
{
	if (!err || cap == 0) return;
	snprintf(err, cap, "%s", message ? message : "prop graph error");
	err[cap - 1] = '\0';
}

static s32 ensureCapacity(void **items, s32 *capacity, s32 needed,
		size_t item_size)
{
	if (needed <= *capacity) return 1;
	s32 next = *capacity > 0 ? *capacity * 2 : 16;
	while (next < needed) next *= 2;
	void *grown = realloc(*items, (size_t)next * item_size);
	if (!grown) return 0;
	*items = grown;
	*capacity = next;
	return 1;
}

static prop_graph_asset_t *findAsset(const char *asset_id)
{
	if (!asset_id) return NULL;
	for (s32 i = s_asset_count - 1; i >= 0; i--) {
		if (strcmp(s_assets[i].asset_id, asset_id) == 0) return &s_assets[i];
	}
	return NULL;
}

static const weapon_graph_ir_param_t *findParam(
		const weapon_graph_ir_t *ir, const weapon_graph_ir_node_t *node,
		const char *key)
{
	if (!ir || !node || !key) return NULL;
	for (s32 i = 0; i < node->param_count; i++) {
		const weapon_graph_ir_param_t *p = &ir->params[node->param_start + i];
		if (strcmp(p->key, key) == 0) return p;
	}
	return NULL;
}

static s32 validateNode(const weapon_graph_ir_t *ir,
		const weapon_graph_ir_node_t *node, char *err, size_t err_cap)
{
	const weapon_graph_ir_param_t *p;
	switch (node->opcode) {
	case WEAPON_GRAPH_OP_PROP_EVENT_SPAWN:
	case WEAPON_GRAPH_OP_PROP_EVENT_TICK:
	case WEAPON_GRAPH_OP_PROP_CONDITION_ENABLED:
		return 1;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_ENABLED:
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_COLLISION:
		p = findParam(ir, node, "value");
		if (p && p->type == WEAPON_GRAPH_PARAM_BOOL) return 1;
		setErr(err, err_cap, "prop boolean action requires boolean params.value");
		return 0;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_HEALTH:
		p = findParam(ir, node, "value");
		if (p && (p->type == WEAPON_GRAPH_PARAM_FLOAT ||
				p->type == WEAPON_GRAPH_PARAM_INT) && p->f_value >= 0.0f) return 1;
		setErr(err, err_cap, "action.set_health requires non-negative numeric params.value");
		return 0;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_CHANNEL:
		p = findParam(ir, node, "channel");
		if (!p || p->type != WEAPON_GRAPH_PARAM_STRING || !p->value[0]) {
			setErr(err, err_cap, "action.set_channel requires string params.channel");
			return 0;
		}
		p = findParam(ir, node, "value");
		if (p && p->type == WEAPON_GRAPH_PARAM_BOOL) return 1;
		setErr(err, err_cap, "action.set_channel requires boolean params.value");
		return 0;
	default:
		setErr(err, err_cap, "prop graph contains an unsupported opcode");
		return 0;
	}
}

s32 propGraphRuntimeRegisterJson(const char *asset_id, const char *json,
		u32 json_size, char *err, size_t err_cap)
{
	weapon_graph_ir_t ir;
	prop_graph_asset_t *asset;
	s32 has_event = 0;
	u8 reachable[WEAPON_GRAPH_IR_MAX_NODES] = { 0 };

	if (err && err_cap) err[0] = '\0';
	if (!asset_id || !asset_id[0]) {
		setErr(err, err_cap, "prop graph registration requires a catalog id");
		return -1;
	}
	if (weaponGraphCompileJson(ASSET_PROP, json, json_size, &ir,
			err, err_cap) != 0) return -1;
	if (!ir.asset_id[0] || strcmp(ir.asset_id, asset_id) != 0) {
		setErr(err, err_cap, "prop graph asset_id does not match its catalog entry");
		weaponGraphIrFree(&ir);
		return -1;
	}
	if (ir.node_count == 0) {
		setErr(err, err_cap, "prop graph must contain at least one executable node");
		weaponGraphIrFree(&ir);
		return -1;
	}
	for (s32 i = 0; i < ir.node_count; i++) {
		if (!validateNode(&ir, &ir.nodes[i], err, err_cap)) {
			weaponGraphIrFree(&ir);
			return -1;
		}
		if (ir.nodes[i].opcode == WEAPON_GRAPH_OP_PROP_EVENT_SPAWN ||
				ir.nodes[i].opcode == WEAPON_GRAPH_OP_PROP_EVENT_TICK) has_event = 1;
	}
	if (!has_event) {
		setErr(err, err_cap, "prop graph needs event.spawn or event.tick");
		weaponGraphIrFree(&ir);
		return -1;
	}
	for (s32 root = 0; root < ir.node_count; root++) {
		if (ir.nodes[root].opcode != WEAPON_GRAPH_OP_PROP_EVENT_SPAWN &&
				ir.nodes[root].opcode != WEAPON_GRAPH_OP_PROP_EVENT_TICK) continue;
		s32 stack[WEAPON_GRAPH_IR_MAX_NODES];
		s32 count = 0;
		if (!reachable[root]) {
			reachable[root] = 1;
			stack[count++] = root;
		}
		while (count > 0) {
			s32 current = stack[--count];
			for (s32 edge = 0; edge < ir.edge_count; edge++) {
				if (ir.edges[edge].from == current && !reachable[ir.edges[edge].to]) {
					reachable[ir.edges[edge].to] = 1;
					stack[count++] = ir.edges[edge].to;
				}
			}
		}
	}
	for (s32 i = 0; i < ir.node_count; i++) {
		if (!reachable[i]) {
			setErr(err, err_cap,
				"prop graph contains a node unreachable from a production event");
			weaponGraphIrFree(&ir);
			return -1;
		}
	}

	asset = findAsset(asset_id);
	if (!asset) {
		if (!ensureCapacity((void **)&s_assets, &s_asset_capacity,
				s_asset_count + 1, sizeof(*s_assets))) {
			setErr(err, err_cap, "out of memory retaining prop graph");
			weaponGraphIrFree(&ir);
			return -1;
		}
		asset = &s_assets[s_asset_count++];
		memset(asset, 0, sizeof(*asset));
	} else {
		weaponGraphIrFree(&asset->ir);
	}
	snprintf(asset->asset_id, sizeof(asset->asset_id), "%s", asset_id);
	asset->ir = ir;
	return 0;
}

static s32 executeFrom(prop_graph_instance_t *instance, s32 node_index,
		u8 *visited)
{
	prop_graph_asset_t *asset = findAsset(instance->asset_id);
	weapon_graph_ir_t *ir;
	weapon_graph_ir_node_t *node;
	const weapon_graph_ir_param_t *p;
	s32 pass = 1;

	if (!asset) return 0;
	ir = &asset->ir;
	if (node_index < 0 || node_index >= ir->node_count || visited[node_index]) return 0;
	visited[node_index] = 1;
	node = &ir->nodes[node_index];
	switch (node->opcode) {
	case WEAPON_GRAPH_OP_PROP_CONDITION_ENABLED:
		pass = instance->target.is_enabled &&
			instance->target.is_enabled(instance->target.context);
		break;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_ENABLED:
		p = findParam(ir, node, "value");
		instance->target.set_enabled(instance->target.context, p->b_value);
		break;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_HEALTH:
		p = findParam(ir, node, "value");
		instance->target.set_health(instance->target.context,
			p->type == WEAPON_GRAPH_PARAM_INT ? (f32)p->i_value : p->f_value);
		break;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_COLLISION:
		p = findParam(ir, node, "value");
		instance->target.set_collision(instance->target.context, p->b_value);
		break;
	case WEAPON_GRAPH_OP_PROP_ACTION_SET_CHANNEL:
		p = findParam(ir, node, "channel");
		{
			const weapon_graph_ir_param_t *value = findParam(ir, node, "value");
			instance->target.set_channel(instance->target.context, p->value,
				value->b_value);
		}
		break;
	default:
		break;
	}
	if (!pass) return 1;
	for (s32 i = 0; i < ir->edge_count; i++) {
		if (ir->edges[i].from == node_index) {
			executeFrom(instance, ir->edges[i].to, visited);
		}
	}
	return 1;
}

static void fireEvent(prop_graph_instance_t *instance,
		weapon_graph_opcode_e event)
{
	u8 visited[WEAPON_GRAPH_IR_MAX_NODES];
	prop_graph_asset_t *asset = findAsset(instance->asset_id);
	weapon_graph_ir_t *ir;
	if (!asset) return;
	ir = &asset->ir;
	for (s32 i = 0; i < ir->node_count; i++) {
		if (ir->nodes[i].opcode != event) continue;
		memset(visited, 0, sizeof(visited));
		executeFrom(instance, i, visited);
	}
}

s32 propGraphRuntimeBind(const char *asset_id, u32 forge_uid,
		const prop_graph_runtime_target_t *target)
{
	prop_graph_asset_t *asset = findAsset(asset_id);
	if (!asset || !target || !target->is_enabled || !target->set_enabled ||
			!target->set_health || !target->set_collision ||
			!target->set_channel) return 0;
	if (!ensureCapacity((void **)&s_instances, &s_instance_capacity,
			s_instance_count + 1, sizeof(*s_instances))) return 0;
	prop_graph_instance_t *instance = &s_instances[s_instance_count++];
	snprintf(instance->asset_id, sizeof(instance->asset_id), "%s", asset_id);
	instance->forge_uid = forge_uid;
	instance->target = *target;
	fireEvent(instance, WEAPON_GRAPH_OP_PROP_EVENT_SPAWN);
	return 1;
}

void propGraphRuntimeTick(void)
{
	for (s32 i = 0; i < s_instance_count; i++) {
		fireEvent(&s_instances[i], WEAPON_GRAPH_OP_PROP_EVENT_TICK);
	}
}

void propGraphRuntimeUnbindAll(void)
{
	s_instance_count = 0;
}

void propGraphRuntimeClearAsset(const char *asset_id)
{
	if (!asset_id) return;
	for (s32 i = 0; i < s_asset_count; i++) {
		if (strcmp(s_assets[i].asset_id, asset_id) != 0) continue;
		weaponGraphIrFree(&s_assets[i].ir);
		for (s32 j = i + 1; j < s_asset_count; j++) s_assets[j - 1] = s_assets[j];
		s_asset_count--;
		break;
	}
}

void propGraphRuntimeReset(void)
{
	for (s32 i = 0; i < s_asset_count; i++) weaponGraphIrFree(&s_assets[i].ir);
	free(s_instances);
	free(s_assets);
	s_instances = NULL;
	s_assets = NULL;
	s_instance_count = s_instance_capacity = 0;
	s_asset_count = s_asset_capacity = 0;
}

s32 propGraphRuntimeHasAsset(const char *asset_id)
{
	return findAsset(asset_id) != NULL;
}

s32 propGraphRuntimeInstanceCount(void)
{
	return s_instance_count;
}
