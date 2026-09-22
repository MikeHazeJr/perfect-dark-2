/* Dependency doubles only. Tests link the production assetcatalog.c registry,
 * locking, mutation and retirement code plus the production reset planner. */
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetprovider_internal.h"
#include "catalog_mutation_support.h"
#include <string.h>

catalog_mutation_controls_t g_CatalogMutationControls;
void catalogMutationTestReset(void)
{
    memset(&g_CatalogMutationControls, 0, sizeof(g_CatalogMutationControls));
    g_CatalogMutationControls.preflight_ok = 1;
    g_CatalogMutationControls.dependent_preflight_ok = 1;
    g_CatalogMutationControls.invalidate_ok = 1;
    g_CatalogMutationControls.teardown_ok = 1;
    g_CatalogMutationControls.reload_ok = 1;
}
s32 catalogCanDeactivateTypedAsset(asset_type_e type, const char *id)
{ (void)type; (void)id; return g_CatalogMutationControls.preflight_ok; }
s32 catalogCanInvalidateTypedAssetDependents(const char *id)
{ (void)id; return g_CatalogMutationControls.dependent_preflight_ok; }
s32 catalogInvalidateTypedAssetDependents(const char *id)
{
    ++g_CatalogMutationControls.invalidate_calls;
    if (g_CatalogMutationControls.conflict_on_invalidate) {
        asset_entry_t *entry = (asset_entry_t *)assetCatalogResolve(id);
        if (entry) entry->enabled = !entry->enabled;
    }
    return g_CatalogMutationControls.invalidate_ok;
}
s32 catalogDeactivateTypedAssetForReset(asset_type_e type, const char *id)
{
    (void)type; (void)id;
    ++g_CatalogMutationControls.teardown_calls;
    return g_CatalogMutationControls.teardown_ok &&
        g_CatalogMutationControls.teardown_calls != g_CatalogMutationControls.fail_teardown_at;
}
s32 catalogDeactivateTypedAsset(asset_type_e type, const char *id)
{ return catalogDeactivateTypedAssetForReset(type, id); }
s32 catalogReloadInvalidatedTypedAssets(void)
{ ++g_CatalogMutationControls.reload_calls; return g_CatalogMutationControls.reload_ok; }
s32 catalogPrepareTypedAssetReplacement(asset_type_e type, const char *id)
{ (void)type; (void)id; return 1; }
s32 catalogDepContains(const char *owner, const char *dependency)
{ (void)owner; (void)dependency; return 0; }
void catalogDepClear(void) {}
void catalogDepClearMods(void) {}
void catalogTypedLifecycleClear(void) {}
void catalogTypedLifecycleClearMods(void) {}
void assetRuntimeReset(void) {}
void weaponGraphRuntimeClearAll(void) {}
void effectGraphRuntimeClearAll(void) {}
void assetCatalogResetCustomAnimSlots(void) {}
void assetCatalogResetCustomBodyHeadSlots(void) {}
void assetCatalogResetCustomModelSlots(void) {}
void assetCatalogResetCustomSoundSlots(void) {}
void assetCatalogResetCustomStageSlots(void) {}
void assetCatalogResetCustomTextureSlots(void) {}
void assetCatalogResetCustomWeaponSlots(void) {}
s32 assetCatalogResolveBodyPrivateSlot(const char *id) { (void)id; return -1; }
s32 assetCatalogResolveHeadPrivateSlot(const char *id) { (void)id; return -1; }
s32 assetCatalogScanComponents(const char *path) { (void)path; return 0; }
s32 assetCatalogScanBotVariants(const char *path) { (void)path; return 0; }
asset_data_handle_t fileProviderHandle(const char *path)
{ asset_data_handle_t handle = {0}; (void)path; return handle; }
asset_data_handle_t romProviderHandle(s32 number)
{ asset_data_handle_t handle = {0}; (void)number; return handle; }
int smokeHarnessIsActive(void) { return 0; }
void sysLogPrintf(s32 level, const char *format, ...) { (void)level; (void)format; }
