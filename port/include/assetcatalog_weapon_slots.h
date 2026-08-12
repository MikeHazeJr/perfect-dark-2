#ifndef PD_ASSETCATALOG_WEAPON_SLOTS_H
#define PD_ASSETCATALOG_WEAPON_SLOTS_H

#include <PR/ultratypes.h>

struct weapon;
struct aibotweaponpreference;

#ifdef __cplusplus
extern "C" {
#endif

void assetCatalogResetCustomWeaponSlots(void);
void *assetCatalogSnapshotCustomWeaponSlots(void);
s32 assetCatalogRestoreCustomWeaponSlots(const void *snapshot);
void assetCatalogDestroyCustomWeaponSlotSnapshot(void *snapshot);
s32 assetCatalogResolveWeaponPrivateSlots(const char *catalog_id,
        s32 authored_runtime_weapon_id, s32 has_authored_runtime_weapon_id,
        s32 *runtime_weapon_id_out, s32 *mp_weapon_id_out);
void assetCatalogRefreshWeaponPrivateSlotDefaults(s32 runtime_weapon_id,
        const struct weapon *weapon,
        const struct aibotweaponpreference *bot_pref);

#ifdef __cplusplus
}
#endif

#endif /* PD_ASSETCATALOG_WEAPON_SLOTS_H */
