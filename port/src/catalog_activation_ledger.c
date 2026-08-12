#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "catalog_activation_ledger.h"

typedef struct catalog_activation_rows {
	catalog_activation_root_t *items;
	size_t count;
	size_t capacity;
} catalog_activation_rows_t;

static catalog_activation_rows_t s_active;
static catalog_activation_rows_t s_pending;

static s32 reserveRows(catalog_activation_rows_t *rows, size_t additional)
{
	if (!rows || additional > SIZE_MAX - rows->count) return 0;
	size_t required = rows->count + additional;
	if (required <= rows->capacity) return 1;
	size_t next = rows->capacity ? rows->capacity * 2 : 16;
	if (next < required) next = required;
	if (next > SIZE_MAX / sizeof(*rows->items)) return 0;
	catalog_activation_root_t *grown =
		(catalog_activation_root_t *)realloc(rows->items,
			next * sizeof(*rows->items));
	if (!grown) return 0;
	rows->items = grown;
	rows->capacity = next;
	return 1;
}

static size_t findRow(const catalog_activation_rows_t *rows, const char *id)
{
	if (!rows || !id) return SIZE_MAX;
	for (size_t i = 0; i < rows->count; i++) {
		if (strcmp(rows->items[i].id, id) == 0) return i;
	}
	return SIZE_MAX;
}

static void eraseRow(catalog_activation_rows_t *rows, size_t index)
{
	if (!rows || index >= rows->count) return;
	if (index + 1 < rows->count) {
		memmove(&rows->items[index], &rows->items[index + 1],
			(rows->count - index - 1) * sizeof(*rows->items));
	}
	rows->count--;
}

static s32 appendOrMerge(catalog_activation_rows_t *rows,
	const catalog_activation_root_t *root)
{
	size_t index = findRow(rows, root->id);
	if (index != SIZE_MAX) {
		if (rows->items[index].type != root->type
				|| rows->items[index].references > INT32_MAX - root->references) {
			return 0;
		}
		rows->items[index].references += root->references;
		rows->items[index].stage_owned |= root->stage_owned;
		rows->items[index].bundled |= root->bundled;
		return 1;
	}
	if (!reserveRows(rows, 1)) return 0;
	rows->items[rows->count++] = *root;
	return 1;
}

s32 catalogActivationLedgerReserve(const char *root_id)
{
	if (!root_id || !root_id[0] || strlen(root_id) >= CATALOG_ID_LEN) return 0;
	return findRow(&s_active, root_id) != SIZE_MAX || reserveRows(&s_active, 1);
}

s32 catalogActivationLedgerRecord(const char *root_id, asset_type_e type,
	s32 bundled)
{
	if (!catalogActivationLedgerReserve(root_id)) return 0;
	size_t index = findRow(&s_active, root_id);
	if (index != SIZE_MAX) {
		if (s_active.items[index].type != type
				|| s_active.items[index].references == INT32_MAX) return 0;
		s_active.items[index].references++;
		return 1;
	}
	catalog_activation_root_t root;
	memset(&root, 0, sizeof(root));
	strncpy(root.id, root_id, sizeof(root.id) - 1);
	root.type = type;
	root.references = 1;
	root.bundled = bundled ? 1 : 0;
	s_active.items[s_active.count++] = root;
	return 1;
}

s32 catalogActivationLedgerRelease(const char *root_id)
{
	size_t index = findRow(&s_active, root_id);
	if (index == SIZE_MAX) return 0;
	if (--s_active.items[index].references <= 0) eraseRow(&s_active, index);
	return 1;
}

void catalogActivationLedgerSetStageOwned(const char *root_id, s32 owned)
{
	size_t index = findRow(&s_active, root_id);
	if (index != SIZE_MAX) s_active.items[index].stage_owned = owned ? 1 : 0;
}

void catalogActivationLedgerForgetActive(const char *root_id)
{
	size_t index = findRow(&s_active, root_id);
	if (index != SIZE_MAX) eraseRow(&s_active, index);

}

void catalogActivationLedgerForget(const char *root_id)
{
	catalogActivationLedgerForgetActive(root_id);
	size_t index;
	index = findRow(&s_pending, root_id);
	if (index != SIZE_MAX) eraseRow(&s_pending, index);
}

static s32 preflightInvalidation(const char *dependency_id,
	s32 include_root, catalog_activation_contains_fn contains_fn, void *userdata,
	u8 *affected, size_t *out_affected_count)
{
	size_t affected_count = 0;
	if (!dependency_id || !dependency_id[0] || !contains_fn || !affected) return 0;
	for (size_t i = 0; i < s_active.count; i++) {
		s32 contains = 0;
		if (!include_root
				&& strcmp(s_active.items[i].id, dependency_id) == 0) continue;
		if (!contains_fn(&s_active.items[i], dependency_id, &contains, userdata)) {
			return 0;
		}
		if (contains) {
			affected[i] = 1;
			affected_count++;
		}
	}
	if (out_affected_count) *out_affected_count = affected_count;
	return reserveRows(&s_pending, affected_count);
}

s32 catalogActivationLedgerCanInvalidateDependents(const char *dependency_id,
	catalog_activation_contains_fn contains_fn, void *userdata)
{
	u8 *affected = (u8 *)calloc(s_active.count ? s_active.count : 1, 1);
	if (!affected) return 0;
	s32 result = preflightInvalidation(dependency_id, 0, contains_fn, userdata,
		affected, NULL);
	free(affected);
	return result;
}

static s32 invalidateRoots(const char *dependency_id, s32 include_root,
	catalog_activation_contains_fn contains_fn,
	catalog_activation_retire_fn retire_fn, void *userdata)
{
	u8 *affected;
	if (!dependency_id || !dependency_id[0] || !contains_fn || !retire_fn) return 0;
	affected = (u8 *)calloc(s_active.count ? s_active.count : 1, 1);
	if (!affected) return 0;

	/* Complete preflight. A stale type or cycle aborts before one owner moves. */
	if (!preflightInvalidation(dependency_id, include_root, contains_fn, userdata,
			affected, NULL)) {
		free(affected);
		return 0;
	}

	for (size_t i = s_active.count; i-- > 0;) {
		if (!affected[i]) continue;
		catalog_activation_root_t root = s_active.items[i];
		retire_fn(&root, userdata);
		/* Capacity was reserved for the worst case. Merge cannot overflow
		 * because each root has only one active row. */
		if (!appendOrMerge(&s_pending, &root)) {
			free(affected);
			return 0;
		}
		eraseRow(&s_active, i);
	}
	free(affected);
	return 1;
}

s32 catalogActivationLedgerInvalidateDependents(const char *dependency_id,
	catalog_activation_contains_fn contains_fn,
	catalog_activation_retire_fn retire_fn, void *userdata)
{
	return invalidateRoots(dependency_id, 0, contains_fn, retire_fn, userdata);
}

s32 catalogActivationLedgerInvalidateReplacementRoots(const char *dependency_id,
	catalog_activation_contains_fn contains_fn,
	catalog_activation_retire_fn retire_fn, void *userdata)
{
	return invalidateRoots(dependency_id, 1, contains_fn, retire_fn, userdata);
}

void catalogActivationLedgerRestoreRetiredSnapshot(asset_entry_t *entry,
	const asset_entry_t *snapshot)
{
	if (!entry || !snapshot) return;
	*entry = *snapshot;
	entry->load_state = entry->enabled
		? ASSET_STATE_ENABLED : ASSET_STATE_REGISTERED;
	entry->loaded_data = NULL;
	entry->data_size_bytes = 0;
	entry->payload_kind = ASSET_PAYLOAD_NONE;
	entry->ref_count = 0;
	entry->stage_ref_count = 0;
}

s32 catalogActivationLedgerReloadPending(
	catalog_activation_reload_fn reload_fn, void *userdata)
{
	s32 all_reloaded = 1;
	if (!reload_fn) return 0;
	for (size_t i = s_pending.count; i-- > 0;) {
		catalog_activation_root_t root = s_pending.items[i];
		if (!reserveRows(&s_active, 1) || !reload_fn(&root, userdata)) {
			all_reloaded = 0;
			continue;
		}
		/* reload_fn records exact successful roots in s_active. */
		eraseRow(&s_pending, i);
	}
	return all_reloaded;
}

void catalogActivationLedgerClearMods(void)
{
	for (size_t i = s_active.count; i-- > 0;) {
		if (!s_active.items[i].bundled) eraseRow(&s_active, i);
	}
	for (size_t i = s_pending.count; i-- > 0;) {
		if (!s_pending.items[i].bundled) eraseRow(&s_pending, i);
	}
}

void catalogActivationLedgerClear(void)
{
	free(s_active.items);
	free(s_pending.items);
	memset(&s_active, 0, sizeof(s_active));
	memset(&s_pending, 0, sizeof(s_pending));
}

size_t catalogActivationLedgerActiveCount(void) { return s_active.count; }
size_t catalogActivationLedgerPendingCount(void) { return s_pending.count; }
const catalog_activation_root_t *catalogActivationLedgerActiveAt(size_t index)
{
	return index < s_active.count ? &s_active.items[index] : NULL;
}
const catalog_activation_root_t *catalogActivationLedgerPendingAt(size_t index)
{
	return index < s_pending.count ? &s_pending.items[index] : NULL;
}
