#ifndef PD_MODMGR_ENABLED_STATE_H
#define PD_MODMGR_ENABLED_STATE_H

#include <stddef.h>
#include "modmgr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Validate a complete ordered JSON string array before changing any registry
 * row. Known persistent entries follow saved order, then unspecified entries
 * retain their relative order. Session-only rows retain their exact slots and
 * state. Unknown IDs are valid and do not create rows or rewrite the document.
 * Returns 1 on success, 0 with an optional error on rejection. Rejection leaves
 * the complete registry byte-for-byte unchanged. */
int modmgrLoadEnabledDocument(const char *json, size_t json_size,
    modinfo_t *registry, int count, char *error, size_t error_cap);

/* 1 = loaded; 0 = absent (caller may use legacy config); -1 = rejected.
 * An existing unreadable or invalid file never authorizes legacy fallback. */
int modmgrLoadEnabledFile(const char *path, modinfo_t *registry,
    int count, char *error, size_t error_cap);

/* Save persistent enabled IDs in registry order. Fully validate and serialize
 * before opening an atomic candidate. Returns 1 on success, 0 on failure;
 * failure preserves the previous destination. Does not modify the registry. */
int modmgrSaveEnabledFile(const char *path, const modinfo_t *registry,
    int count, char *error, size_t error_cap);

/* Build the legacy comma-separated enabled list in registry order, omitting
 * session-only rows. Capacity includes the terminator; an exact fit succeeds.
 * Reject invalid registry/capacity without modifying output or registry.
 * This retains the existing legacy format; ordered JSON remains primary. */
int modmgrBuildEnabledCsv(const modinfo_t *registry, int count,
    char *output, size_t capacity, char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif
#endif
