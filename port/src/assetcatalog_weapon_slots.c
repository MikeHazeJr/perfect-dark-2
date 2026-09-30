#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <PR/ultratypes.h>

#include "assetcatalog.h"
#include "assetcatalog_weapon_slots.h"
#include "types.h"
#include "constants.h"
#include "data.h"
#include "system.h"

static char s_CustomWeaponCatalogIds[MPWEAPON_CUSTOM_COUNT][CATALOG_ID_LEN];
static unsigned s_CustomWeaponPins[MPWEAPON_CUSTOM_COUNT];
static unsigned char s_CustomWeaponReserved[MPWEAPON_CUSTOM_COUNT];

typedef struct weapon_slot_snapshot {
    char ids[MPWEAPON_CUSTOM_COUNT][CATALOG_ID_LEN];
    struct mpweapon rows[MPWEAPON_CUSTOM_COUNT];
    unsigned char reserved[MPWEAPON_CUSTOM_COUNT];
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
    memset(s_CustomWeaponReserved, 0, sizeof(s_CustomWeaponReserved));

    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; i++) {
        s32 mp_weapon_id = MPWEAPON_CUSTOM_START + i;
        if (!s_CustomWeaponPins[i]) s_CustomWeaponCatalogIds[i][0] = 0;
        memset(&g_MpWeapons[mp_weapon_id], 0, sizeof(g_MpWeapons[mp_weapon_id]));
        g_MpWeapons[mp_weapon_id].weaponnum = WEAPON_DISABLED;
    }
}

void *assetCatalogSnapshotCustomWeaponSlots(void)
{
    weapon_slot_snapshot_t *snapshot = malloc(sizeof(*snapshot));
    if (!snapshot) return NULL;
    memcpy(snapshot->ids, s_CustomWeaponCatalogIds, sizeof(snapshot->ids));
    memcpy(snapshot->reserved, s_CustomWeaponReserved, sizeof(snapshot->reserved));
    memcpy(snapshot->rows, &g_MpWeapons[MPWEAPON_CUSTOM_START],
        sizeof(snapshot->rows));
    return snapshot;
}

s32 assetCatalogRestoreCustomWeaponSlots(const void *opaque)
{
    const weapon_slot_snapshot_t *snapshot = opaque;
    if (!snapshot) return 0;
    /* Reference counts are live consumers, never rollback state. Preflight
     * the entire map before changing reservations or multiplayer rows. */
    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; ++i) {
        if (!s_CustomWeaponPins[i]) continue;
        if (snapshot->reserved[i] && strcmp(snapshot->ids[i], s_CustomWeaponCatalogIds[i])) {
            sysLogPrintf(LOG_ERROR, "CATALOG.WEAPON.SLOT_RESTORE_FAIL: live identity slot=%d", WEAPON_CUSTOM_START + i);
            return 0;
        }
        for (s32 j = 0; j < MPWEAPON_CUSTOM_COUNT; ++j) {
            if (j != i && snapshot->reserved[j] && !strcmp(snapshot->ids[j], s_CustomWeaponCatalogIds[i])) {
                sysLogPrintf(LOG_ERROR, "CATALOG.WEAPON.SLOT_RESTORE_FAIL: retained identity would move slot=%d", WEAPON_CUSTOM_START + i);
                return 0;
            }
        }
    }
    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; ++i) {
        s_CustomWeaponReserved[i] = snapshot->reserved[i];
        if (!s_CustomWeaponPins[i]) {
            if (snapshot->reserved[i]) memcpy(s_CustomWeaponCatalogIds[i], snapshot->ids[i], CATALOG_ID_LEN);
            else s_CustomWeaponCatalogIds[i][0] = 0;
        }
        if (snapshot->reserved[i]) g_MpWeapons[MPWEAPON_CUSTOM_START + i] = snapshot->rows[i];
        else {
            memset(&g_MpWeapons[MPWEAPON_CUSTOM_START + i], 0, sizeof(struct mpweapon));
            g_MpWeapons[MPWEAPON_CUSTOM_START + i].weaponnum = WEAPON_DISABLED;
        }
    }
    return 1;
}

void assetCatalogDestroyCustomWeaponSlotSnapshot(void *snapshot)
{
    free(snapshot);
}

static s32 s_allocateCustomWeaponSlot(const char *catalog_id)
{
    if (!catalog_id || !catalog_id[0] || strlen(catalog_id) >= CATALOG_ID_LEN) {
        return -1;
    }

    for (s32 i = 0; i < MPWEAPON_CUSTOM_COUNT; i++) {
        if (s_CustomWeaponCatalogIds[i][0] &&
                strncmp(s_CustomWeaponCatalogIds[i], catalog_id,
                    CATALOG_ID_LEN) == 0) {
            if (!s_CustomWeaponReserved[i]) {
                struct mpweapon *row = &g_MpWeapons[MPWEAPON_CUSTOM_START + i];
                memset(row, 0, sizeof(*row));
                row->weaponnum = (u8)(WEAPON_CUSTOM_START + i);
                row->hasweapon = 1; row->extrascale = 256;
            }
            s_CustomWeaponReserved[i] = 1;
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
            s_CustomWeaponReserved[i] = 1;
            return i;
        }
    }

    sysLogPrintf(LOG_WARNING,
        "CATALOG.WEAPON.CUSTOM_SLOT_FAIL: id=\"%s\" no private custom weapon slots available",
        catalog_id);
    return -1;
}

static s32 s_exactWeaponSlot(const char *id, s32 slot)
{
    if (!id || !id[0] || strlen(id) >= CATALOG_ID_LEN
            || slot < WEAPON_CUSTOM_START || slot >= WEAPON_CUSTOM_END) return -1;
    s32 local = slot - WEAPON_CUSTOM_START;
    return strcmp(s_CustomWeaponCatalogIds[local], id) == 0 ? local : -1;
}

s32 assetCatalogPinWeaponPrivateSlot(const char *id, s32 slot)
{
    s32 local = s_exactWeaponSlot(id, slot);
    if (local < 0 || s_CustomWeaponPins[local] == UINT_MAX) {
        sysLogPrintf(LOG_ERROR, "CATALOG.WEAPON.SLOT_PIN_FAIL: exact identity required slot=%d", slot);
        return 0;
    }
    ++s_CustomWeaponPins[local];
    return 1;
}

s32 assetCatalogReleaseWeaponPrivateSlot(const char *id, s32 slot)
{
    s32 local = s_exactWeaponSlot(id, slot);
    if (local < 0 || !s_CustomWeaponPins[local]) {
        sysLogPrintf(LOG_ERROR, "CATALOG.WEAPON.SLOT_RELEASE_FAIL: matching live pin required slot=%d", slot);
        return 0;
    }
    if (--s_CustomWeaponPins[local] == 0 && !s_CustomWeaponReserved[local])
        s_CustomWeaponCatalogIds[local][0] = 0;
    return 1;
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
