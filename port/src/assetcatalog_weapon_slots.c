#include <string.h>
#include <stdlib.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_weapon_slots.h"
#include "constants.h"
#include "types.h"
#include "data.h"
#include "system.h"

static char s_CustomWeaponCatalogIds[MPWEAPON_CUSTOM_COUNT][CATALOG_ID_LEN];

typedef struct weapon_slot_snapshot {
    char ids[MPWEAPON_CUSTOM_COUNT][CATALOG_ID_LEN];
    struct mpweapon rows[MPWEAPON_CUSTOM_COUNT];
} weapon_slot_snapshot_t;

_Static_assert(MPWEAPON_CUSTOM_END == 64,
    "MP weapon identities must fit the persisted 64-bit random-filter field");
_Static_assert(MPWEAPON_CUSTOM_COUNT == WEAPON_CUSTOM_COUNT,
    "private MP/runtime weapon slots must remain paired");
_Static_assert(WEAPON_CUSTOM_END <= 0x80,
    "runtime weapon identities must remain representable by signed s8 fields");

static s32 s_runtimeWeaponIdIsBase(s32 runtime_weapon_id)
{
    return runtime_weapon_id >= 0 && runtime_weapon_id < WEAPON_CUSTOM_START;
}

static s32 s_mpWeaponIdForRuntimeWeapon(s32 runtime_weapon_id)
{
    for (s32 mp_weapon_id = 0; mp_weapon_id < NUM_MPWEAPONS; mp_weapon_id++) {
        if (catalogGetMpWeaponNum(mp_weapon_id) == runtime_weapon_id) {
            return mp_weapon_id;
        }
    }
    return -1;
}

static s32 s_customMpWeaponIdForRuntimeWeapon(s32 runtime_weapon_id)
{
    if (runtime_weapon_id < WEAPON_CUSTOM_START
            || runtime_weapon_id >= WEAPON_CUSTOM_END) {
        return -1;
    }
    return MPWEAPON_CUSTOM_START + (runtime_weapon_id - WEAPON_CUSTOM_START);
}

void assetCatalogResetCustomWeaponSlots(void)
{
    memset(s_CustomWeaponCatalogIds, 0, sizeof(s_CustomWeaponCatalogIds));

    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; i++) {
        s32 mp_weapon_id = MPWEAPON_CUSTOM_START + i;
        memset(&g_MpWeapons[mp_weapon_id], 0, sizeof(g_MpWeapons[mp_weapon_id]));
        g_MpWeapons[mp_weapon_id].weaponnum = WEAPON_DISABLED;
    }
}

void *assetCatalogSnapshotCustomWeaponSlots(void)
{
    weapon_slot_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return NULL;
    memcpy(snapshot->ids, s_CustomWeaponCatalogIds, sizeof(snapshot->ids));
    memcpy(snapshot->rows, &g_MpWeapons[MPWEAPON_CUSTOM_START],
        sizeof(snapshot->rows));
    return snapshot;
}

s32 assetCatalogRestoreCustomWeaponSlots(const void *opaque)
{
    const weapon_slot_snapshot_t *snapshot = opaque;
    if (!snapshot) return 0;
    memcpy(s_CustomWeaponCatalogIds, snapshot->ids,
        sizeof(s_CustomWeaponCatalogIds));
    memcpy(&g_MpWeapons[MPWEAPON_CUSTOM_START], snapshot->rows,
        sizeof(snapshot->rows));
    return 1;
}

void assetCatalogDestroyCustomWeaponSlotSnapshot(void *snapshot)
{
    free(snapshot);
}

static s32 s_allocateCustomWeaponSlot(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0]) {
        return -1;
    }

    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; i++) {
        if (s_CustomWeaponCatalogIds[i][0] &&
                strncmp(s_CustomWeaponCatalogIds[i], catalog_id,
                    CATALOG_ID_LEN) == 0) {
            return i;
        }
    }

    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; i++) {
        if (!s_CustomWeaponCatalogIds[i][0]) {
            s32 runtime_weapon_id = WEAPON_CUSTOM_START + i;
            s32 mp_weapon_id = MPWEAPON_CUSTOM_START + i;
            strncpy(s_CustomWeaponCatalogIds[i], catalog_id,
                sizeof(s_CustomWeaponCatalogIds[i]) - 1);
            s_CustomWeaponCatalogIds[i][sizeof(s_CustomWeaponCatalogIds[i]) - 1] = '\0';
            g_MpWeapons[mp_weapon_id].weaponnum = (u8)runtime_weapon_id;
            g_MpWeapons[mp_weapon_id].hasweapon = 1;
            g_MpWeapons[mp_weapon_id].extrascale = 256;
            return i;
        }
    }

    sysLogPrintf(LOG_WARNING,
        "CATALOG.WEAPON.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom weapon slots available",
        catalog_id);
    return -1;
}

s32 assetCatalogResolveWeaponPrivateSlots(const char *catalog_id,
        s32 authored_runtime_weapon_id, s32 has_authored_runtime_weapon_id,
        s32 *runtime_weapon_id_out, s32 *mp_weapon_id_out)
{
    s32 runtime_weapon_id = has_authored_runtime_weapon_id
        ? authored_runtime_weapon_id : -1;
    s32 mp_weapon_id = -1;

    if (has_authored_runtime_weapon_id
            && s_runtimeWeaponIdIsBase(runtime_weapon_id)) {
        mp_weapon_id = s_mpWeaponIdForRuntimeWeapon(runtime_weapon_id);

        /* Many legitimate base catalog rows are inventory items or devices,
         * not multiplayer weapon-set choices. Preserve their authored runtime
         * identity without consuming a private custom pair. */
        if (mp_weapon_id < 0) {
            if (runtime_weapon_id_out) {
                *runtime_weapon_id_out = runtime_weapon_id;
            }
            if (mp_weapon_id_out) {
                *mp_weapon_id_out = -1;
            }
            return 1;
        }
    }

    if (mp_weapon_id < 0) {
        s32 custom_index = s_allocateCustomWeaponSlot(catalog_id);
        if (custom_index >= 0) {
            runtime_weapon_id = WEAPON_CUSTOM_START + custom_index;
            mp_weapon_id = MPWEAPON_CUSTOM_START + custom_index;
        }
    }

    if (runtime_weapon_id_out) {
        *runtime_weapon_id_out = runtime_weapon_id;
    }
    if (mp_weapon_id_out) {
        *mp_weapon_id_out = mp_weapon_id;
    }
    return runtime_weapon_id >= 0 && mp_weapon_id >= 0;
}

static s32 s_clampAmmoQty(s32 qty)
{
    if (qty <= 0) {
        return 0;
    }
    if (qty > 255) {
        return 255;
    }
    return qty;
}

static s32 s_defaultAmmoQtyFromClip(const struct inventory_ammo *ammo)
{
    if (!ammo || ammo->type == 0) {
        return 0;
    }
    if (ammo->clipsize > 0) {
        return s_clampAmmoQty((s32)ammo->clipsize * 10);
    }
    return 1;
}

static void s_refreshFunctionAmmoDefault(const struct weapon *weapon,
        s32 func_index, s32 authored_qty, s8 *type_out, u8 *qty_out)
{
    const struct weaponfunc *func = NULL;
    const struct inventory_ammo *ammo = NULL;
    s32 ammo_index;
    s32 qty;

    if (!type_out || !qty_out) {
        return;
    }
    *type_out = 0;
    *qty_out = 0;

    if (!weapon || func_index < 0 || func_index >= 2) {
        return;
    }
    func = (const struct weaponfunc *)weapon->functions[func_index];
    if (!func) {
        return;
    }

    ammo_index = (s32)func->ammoindex;
    if (ammo_index < 0 || ammo_index >= 2) {
        return;
    }
    ammo = weapon->ammos[ammo_index];
    if (!ammo || ammo->type == 0 || ammo->type > 127) {
        return;
    }

    qty = authored_qty > 0 ? authored_qty : s_defaultAmmoQtyFromClip(ammo);
    *type_out = (s8)ammo->type;
    *qty_out = (u8)s_clampAmmoQty(qty);
}

void assetCatalogRefreshWeaponPrivateSlotDefaults(s32 runtime_weapon_id,
        const struct weapon *weapon,
        const struct aibotweaponpreference *bot_pref)
{
    s32 mp_weapon_id = s_customMpWeaponIdForRuntimeWeapon(runtime_weapon_id);
    struct mpweapon *mpw;
    s32 pri_authored_qty = bot_pref ? (s32)bot_pref->targetammopri : 0;
    s32 sec_authored_qty = bot_pref ? (s32)bot_pref->targetammosec : 0;

    if (mp_weapon_id < 0 || !weapon) {
        return;
    }

    mpw = &g_MpWeapons[mp_weapon_id];
    mpw->weaponnum = (u8)runtime_weapon_id;
    mpw->hasweapon = 1;
    mpw->unlockfeature = 0;
    if (mpw->extrascale == 0) {
        mpw->extrascale = 256;
    }
    mpw->model = (s16)weapon->hi_model;

    s_refreshFunctionAmmoDefault(weapon, 0, pri_authored_qty,
        &mpw->priammotype, &mpw->priammoqty);
    s_refreshFunctionAmmoDefault(weapon, 1, sec_authored_qty,
        &mpw->secammotype, &mpw->secammoqty);

    sysLogPrintf(LOG_NOTE,
        "CATALOG.WEAPON.CUSTOM_SLOT_DEFAULTS: runtime=%d mp=%d "
        "model=%d pri_ammo=%d qty=%u sec_ammo=%d qty=%u",
        runtime_weapon_id, mp_weapon_id, (s32)mpw->model,
        (s32)mpw->priammotype, (u32)mpw->priammoqty,
        (s32)mpw->secammotype, (u32)mpw->secammoqty);
}
