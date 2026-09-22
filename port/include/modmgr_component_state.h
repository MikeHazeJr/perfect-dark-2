#ifndef PD_MODMGR_COMPONENT_STATE_H
#define PD_MODMGR_COMPONENT_STATE_H
#include <stddef.h>
#include "assetcatalog.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct modmgr_component_state modmgr_component_state_t;
typedef struct modmgr_component_choice {
    char id[CATALOG_ID_LEN];
    int enabled;
    /* Supplied from authoritative catalog/package ownership by the caller.
     * Excluded rows neither add nor erase saved machine preferences. */
    int persistent;
} modmgr_component_choice_t;

/* Read and validate the complete line-based document before returning IDs.
 * 1=loaded, 0=absent, -1=invalid/unreadable. *state is NULL on absent/failure.
 * IDs remain owned by state until free. Unknown IDs are valid preferences. */
int modmgrReadComponentState(const char *path, modmgr_component_state_t **state,
    char *error, size_t error_cap);
size_t modmgrComponentStateCount(const modmgr_component_state_t *state);
const char *modmgrComponentStateId(const modmgr_component_state_t *state, size_t index);
void modmgrFreeComponentState(modmgr_component_state_t *state);

/* Merge current persistent choices into the previous valid saved set. Disabled
 * adds, enabled removes, absent/excluded retains. No fixed number of IDs.
 * Serialize before opening an atomic candidate; 1=committed, 0=failure.
 * Failure preserves the prior destination and leaves the supplied rows alone. */
int modmgrSaveComponentStateFile(const char *path,
    const modmgr_component_choice_t *choices, size_t count,
    char *error, size_t error_cap);
#ifdef __cplusplus
}
#endif
#endif
