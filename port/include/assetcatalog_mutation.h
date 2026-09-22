#ifndef PD_ASSETCATALOG_MUTATION_H
#define PD_ASSETCATALOG_MUTATION_H
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Checked mutation status is positive only when the requested catalog change
 * completed or was already satisfied. Negative outcomes can follow partial
 * dependent retirement/reload; they do not promise runtime rollback. An
 * UNCHANGED enabled bit does not prove every dependent is currently loaded. */
typedef enum asset_catalog_change {
    CATALOG_CHANGE_APPLIED = 1,
    CATALOG_CHANGE_UNCHANGED = 2,
    CATALOG_CHANGE_NOT_FOUND = -1,
    CATALOG_CHANGE_PREFLIGHT_FAILED = -2,
    CATALOG_CHANGE_DEPENDENTS_FAILED = -3,
    CATALOG_CHANGE_CONFLICT = -4,
    CATALOG_CHANGE_TEARDOWN_FAILED = -5,
    CATALOG_CHANGE_RELOAD_FAILED = -6,
    CATALOG_CHANGE_RETIRE_FAILED = -7
} asset_catalog_change_e;
asset_catalog_change_e assetCatalogClearModsChecked(void);
asset_catalog_change_e assetCatalogSetEnabledChecked(const char *id, s32 enabled);
#ifdef __cplusplus
}
#endif
#endif
