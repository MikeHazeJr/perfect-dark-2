#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assetcatalog_deps.h"
#include "catalog_dep_activation_plan.h"

typedef struct activation_frame {
	catalog_dep_activation_node_t node;
	catalog_dep_activation_plan_t children;
	size_t next_child;
} activation_frame_t;

static s32 appendNode(catalog_dep_activation_plan_t *plan, const char *id,
	asset_type_e expected_type)
{
	if (!plan || plan->failed || !id || !id[0]) return 0;
	if (strlen(id) >= CATALOG_ID_LEN) {
		plan->failed = 1;
		return 0;
	}
	if (plan->count == plan->capacity) {
		size_t next = plan->capacity ? plan->capacity * 2 : 16;
		if (next < plan->count + 1 || next > SIZE_MAX / sizeof(*plan->nodes)) {
			plan->failed = 1;
			return 0;
		}
		catalog_dep_activation_node_t *grown =
			(catalog_dep_activation_node_t *)realloc(plan->nodes,
				next * sizeof(*plan->nodes));
		if (!grown) {
			plan->failed = 1;
			return 0;
		}
		plan->nodes = grown;
		plan->capacity = next;
	}
	strncpy(plan->nodes[plan->count].id, id, CATALOG_ID_LEN - 1);
	plan->nodes[plan->count].id[CATALOG_ID_LEN - 1] = '\0';
	plan->nodes[plan->count].expected_type = expected_type;
	plan->count++;
	return 1;
}

void catalogDepActivationPlanCollect(const char *dep_id, void *userdata)
{
	(void)appendNode((catalog_dep_activation_plan_t *)userdata, dep_id,
		ASSET_NONE);
}

static void collectTyped(const char *dep_id, asset_type_e expected_type,
	void *userdata)
{
	(void)appendNode((catalog_dep_activation_plan_t *)userdata, dep_id,
		expected_type);
}

static s32 planContains(const catalog_dep_activation_plan_t *plan,
	const char *id)
{
	for (size_t i = 0; plan && i < plan->count; i++) {
		if (strcmp(plan->nodes[i].id, id) == 0) return 1;
	}
	return 0;
}

static void freeFrames(activation_frame_t *frames, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		catalogDepActivationPlanFree(&frames[i].children);
	}
	free(frames);
}

s32 catalogDepActivationPlanBuild(catalog_dep_activation_plan_t *plan,
	const char *root_id, asset_type_e root_type,
	catalog_dep_activation_resolve_fn resolve_fn, void *resolve_userdata,
	char *error, size_t error_cap)
{
	activation_frame_t *frames = NULL;
	size_t depth = 0;
	size_t capacity = 0;
	catalog_dep_activation_node_t pending;

	if (error && error_cap) error[0] = '\0';
	if (!plan || !root_id || !root_id[0] || !resolve_fn) return 0;
	catalogDepActivationPlanFree(plan);
	if (strlen(root_id) >= CATALOG_ID_LEN) {
		if (error && error_cap) snprintf(error, error_cap,
			"root catalog ID exceeds %d bytes", CATALOG_ID_LEN - 1);
		return 0;
	}
	memset(&pending, 0, sizeof(pending));
	strncpy(pending.id, root_id, sizeof(pending.id) - 1);
	pending.expected_type = root_type;

	for (;;) {
		if (pending.id[0]) {
			asset_type_e actual = ASSET_NONE;
			if (!resolve_fn(pending.id, &actual, resolve_userdata) ||
					(pending.expected_type != ASSET_NONE &&
					 actual != pending.expected_type)) {
				if (error && error_cap) snprintf(error, error_cap,
					"dependency %s missing or wrong type (expected=%d actual=%d)",
					pending.id, pending.expected_type, actual);
				freeFrames(frames, depth);
				catalogDepActivationPlanFree(plan);
				return 0;
			}
			for (size_t i = 0; i < depth; i++) {
				if (strcmp(frames[i].node.id, pending.id) == 0) {
					if (error && error_cap) snprintf(error, error_cap,
						"catalog dependency cycle reaches %s", pending.id);
					freeFrames(frames, depth);
					catalogDepActivationPlanFree(plan);
					return 0;
				}
			}
			if (planContains(plan, pending.id)) {
				pending.id[0] = '\0';
			} else {
				if (depth == capacity) {
					size_t next = capacity ? capacity * 2 : 16;
					if (next < depth + 1 ||
							next > SIZE_MAX / sizeof(*frames)) {
						if (error && error_cap) snprintf(error, error_cap,
							"dependency activation stack size overflow");
						freeFrames(frames, depth);
						catalogDepActivationPlanFree(plan);
						return 0;
					}
					activation_frame_t *grown = (activation_frame_t *)realloc(
						frames, next * sizeof(*frames));
					if (!grown) {
						if (error && error_cap) snprintf(error, error_cap,
							"out of memory building dependency activation stack");
						freeFrames(frames, depth);
						catalogDepActivationPlanFree(plan);
						return 0;
					}
					frames = grown;
					capacity = next;
				}
				memset(&frames[depth], 0, sizeof(frames[depth]));
				frames[depth].node = pending;
				frames[depth].node.expected_type = actual;
				catalogDepForEachTyped(pending.id, collectTyped,
					&frames[depth].children);
				if (frames[depth].children.failed) {
					if (error && error_cap) snprintf(error, error_cap,
						"out of memory enumerating dependencies for %s", pending.id);
					freeFrames(frames, depth + 1);
					catalogDepActivationPlanFree(plan);
					return 0;
				}
				depth++;
				pending.id[0] = '\0';
			}
		}

		if (depth == 0) break;
		activation_frame_t *top = &frames[depth - 1];
		if (top->next_child < top->children.count) {
			pending = top->children.nodes[top->next_child++];
			continue;
		}
		if (!appendNode(plan, top->node.id, top->node.expected_type)) {
			if (error && error_cap) snprintf(error, error_cap,
				"out of memory finalizing dependency activation plan");
			freeFrames(frames, depth);
			catalogDepActivationPlanFree(plan);
			return 0;
		}
		catalogDepActivationPlanFree(&top->children);
		depth--;
	}
	free(frames);
	return 1;
}

void catalogDepActivationPlanRollback(const catalog_dep_activation_plan_t *plan,
	size_t loaded_count, catalog_dep_activation_release_fn release_fn,
	void *userdata)
{
	if (!plan || !release_fn) return;
	if (loaded_count > plan->count) loaded_count = plan->count;
	while (loaded_count > 0) {
		loaded_count--;
		release_fn(plan->nodes[loaded_count].id,
			plan->nodes[loaded_count].expected_type, userdata);
	}
}

void catalogDepActivationPlanFree(catalog_dep_activation_plan_t *plan)
{
	if (!plan) return;
	free(plan->nodes);
	memset(plan, 0, sizeof(*plan));
}
