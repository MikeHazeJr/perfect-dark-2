/** Exact root-owner ledger for typed catalog activation transactions. */
#ifndef PD_CATALOG_ACTIVATION_LEDGER_H
#define PD_CATALOG_ACTIVATION_LEDGER_H

#include <stddef.h>
#include <PR/ultratypes.h>
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct catalog_activation_root {
	char id[CATALOG_ID_LEN];
	asset_type_e type;
	s32 references;
	s32 stage_owned;
	s32 bundled;
} catalog_activation_root_t;

typedef s32 (*catalog_activation_contains_fn)(
	const catalog_activation_root_t *root, const char *dependency_id,
	s32 *out_contains, void *userdata);
typedef void (*catalog_activation_retire_fn)(
	const catalog_activation_root_t *root, void *userdata);
typedef s32 (*catalog_activation_reload_fn)(
	const catalog_activation_root_t *root, void *userdata);

/* Reserve before mutating runtime truth so recording a successful root
 * activation cannot subsequently fail for allocation reasons. */
s32 catalogActivationLedgerReserve(const char *root_id);
s32 catalogActivationLedgerRecord(const char *root_id, asset_type_e type,
	s32 bundled);
s32 catalogActivationLedgerRelease(const char *root_id);
void catalogActivationLedgerSetStageOwned(const char *root_id, s32 owned);
void catalogActivationLedgerForgetActive(const char *root_id);
void catalogActivationLedgerForget(const char *root_id);

/* Preflight every active root closure before retiring any affected root.
 * Direct ownership of dependency_id is intentionally excluded: this call
 * invalidates dependents, while the ordinary child teardown owns its refs. */
s32 catalogActivationLedgerInvalidateDependents(const char *dependency_id,
	catalog_activation_contains_fn contains_fn,
	catalog_activation_retire_fn retire_fn, void *userdata);
s32 catalogActivationLedgerCanInvalidateDependents(const char *dependency_id,
	catalog_activation_contains_fn contains_fn, void *userdata);

/* Reload every pending root whose complete typed closure is now valid.
 * Failed roots remain pending and unreachable. */
s32 catalogActivationLedgerReloadPending(
	catalog_activation_reload_fn reload_fn, void *userdata);

void catalogActivationLedgerClearMods(void);
void catalogActivationLedgerClear(void);
size_t catalogActivationLedgerActiveCount(void);
size_t catalogActivationLedgerPendingCount(void);
const catalog_activation_root_t *catalogActivationLedgerActiveAt(size_t index);
const catalog_activation_root_t *catalogActivationLedgerPendingAt(size_t index);

#ifdef __cplusplus
}
#endif

#endif
