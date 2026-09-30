#include "catch.hpp"
#include <memory>
#include <string>
#include "types.h"
extern "C" {
#include "constants.h"
extern struct mpweapon g_MpWeapons[NUM_MPWEAPONS];
#include "assetcatalog_weapon_slots.h"
}
#undef bool

namespace {
using Snapshot = std::unique_ptr<void, decltype(&assetCatalogDestroyCustomWeaponSlotSnapshot)>;
struct Isolation {
    Snapshot saved{assetCatalogSnapshotCustomWeaponSlots(), assetCatalogDestroyCustomWeaponSlotSnapshot};
    Isolation() { REQUIRE(saved); assetCatalogResetCustomWeaponSlots(); }
    ~Isolation() { assetCatalogResetCustomWeaponSlots(); assetCatalogRestoreCustomWeaponSlots(saved.get()); }
};
struct Pin {
    const char *id; int slot; bool live;
    Pin(const char *id, int slot) : id(id), slot(slot), live(assetCatalogPinWeaponPrivateSlot(id, slot) != 0) {}
    ~Pin() { release(); }
    void release() { if (live) { assetCatalogReleaseWeaponPrivateSlot(id, slot); live = false; } }
};
int allocate(const char *id) {
    s32 runtime = -1, mp = -1;
    if (!assetCatalogResolveWeaponPrivateSlots(id, -1, 0, &runtime, &mp)) return -1;
    CHECK(mp == MPWEAPON_CUSTOM_START + runtime - WEAPON_CUSTOM_START);
    return runtime;
}
}

TEST_CASE("weapon private leases prevent identity reuse while retiring MP reservations",
        "[catalog][weapon][slots][modding][pdxxx][c3842]") {
    Isolation isolation;
    const int old = allocate("pin:old"); REQUIRE(old >= WEAPON_CUSTOM_START);
    Pin first("pin:old", old), second("pin:old", old); REQUIRE(first.live); REQUIRE(second.live);
    const auto mp = MPWEAPON_CUSTOM_START + old - WEAPON_CUSTOM_START;
    assetCatalogResetCustomWeaponSlots();
    CHECK(g_MpWeapons[mp].weaponnum == WEAPON_DISABLED); CHECK_FALSE(g_MpWeapons[mp].hasweapon);
    CHECK(allocate("pin:other") != old);
    first.release(); CHECK(allocate("pin:third") != old);
    second.release(); CHECK(allocate("pin:reuse") == old);
}

TEST_CASE("weapon empty rollback never restores live reference counts or retired MP rows",
        "[catalog][weapon][slots][modding][pdxxx][c3842]") {
    Isolation isolation;
    Snapshot empty(assetCatalogSnapshotCustomWeaponSlots(), assetCatalogDestroyCustomWeaponSlotSnapshot); REQUIRE(empty);
    const int old = allocate("pin:child"); Pin pin("pin:child", old); REQUIRE(pin.live);
    REQUIRE(assetCatalogRestoreCustomWeaponSlots(empty.get()));
    REQUIRE(assetCatalogRestoreCustomWeaponSlots(empty.get()));
    CHECK_FALSE(g_MpWeapons[MPWEAPON_CUSTOM_START + old - WEAPON_CUSTOM_START].hasweapon);
    CHECK(allocate("pin:other") != old);
    pin.release(); CHECK(allocate("pin:reuse") == old);
}

TEST_CASE("weapon rollback rejects identity conflict atomically including MP metadata",
        "[catalog][weapon][slots][modding][pdxxx][c3842]") {
    Isolation isolation;
    const int before = allocate("pin:before");
    Snapshot saved(assetCatalogSnapshotCustomWeaponSlots(), assetCatalogDestroyCustomWeaponSlotSnapshot); REQUIRE(saved);
    assetCatalogResetCustomWeaponSlots();
    const int after = allocate("pin:after"); REQUIRE(after == before);
    Pin pin("pin:after", after); REQUIRE(pin.live);
    const int other = allocate("pin:other");
    auto &row = g_MpWeapons[MPWEAPON_CUSTOM_START + other - WEAPON_CUSTOM_START]; row.priammoqty = 79;
    CHECK_FALSE(assetCatalogRestoreCustomWeaponSlots(saved.get()));
    CHECK(row.priammoqty == 79); CHECK(allocate("pin:after") == after); CHECK(allocate("pin:other") == other);
    pin.release(); REQUIRE(assetCatalogRestoreCustomWeaponSlots(saved.get())); CHECK(allocate("pin:before") == before);
}

TEST_CASE("weapon rollback cannot move a retained identity and resurrection keeps its reservation",
        "[catalog][weapon][slots][modding][pdxxx][c3842]") {
    Isolation isolation;
    allocate("pin:blocker"); const int old = allocate("pin:move");
    Snapshot saved(assetCatalogSnapshotCustomWeaponSlots(), assetCatalogDestroyCustomWeaponSlotSnapshot); REQUIRE(saved);
    assetCatalogResetCustomWeaponSlots(); const int moved = allocate("pin:move"); REQUIRE(moved != old);
    Pin pin("pin:move", moved); REQUIRE(pin.live);
    CHECK_FALSE(assetCatalogRestoreCustomWeaponSlots(saved.get()));
    assetCatalogResetCustomWeaponSlots(); REQUIRE(allocate("pin:move") == moved);
    CHECK(g_MpWeapons[MPWEAPON_CUSTOM_START + moved - WEAPON_CUSTOM_START].hasweapon == 1);
    pin.release(); CHECK(allocate("pin:other") != moved);
}

TEST_CASE("weapon private pins require allocated exact identity and balanced release",
        "[catalog][weapon][slots][modding][pdxxx][c3842]") {
    Isolation isolation; const int slot = allocate("pin:exact");
    CHECK_FALSE(assetCatalogPinWeaponPrivateSlot(nullptr, slot));
    CHECK_FALSE(assetCatalogPinWeaponPrivateSlot("pin:foreign", slot));
    CHECK_FALSE(assetCatalogPinWeaponPrivateSlot("pin:exact", WEAPON_CUSTOM_END));
    CHECK_FALSE(assetCatalogReleaseWeaponPrivateSlot("pin:exact", slot));
    Pin pin("pin:exact", slot); REQUIRE(pin.live);
    CHECK_FALSE(assetCatalogReleaseWeaponPrivateSlot("pin:foreign", slot));
    assetCatalogResetCustomWeaponSlots(); pin.release();
    CHECK_FALSE(assetCatalogReleaseWeaponPrivateSlot("pin:exact", slot));
    CHECK(allocate("pin:reuse") == slot);
    CHECK(allocate(std::string(1024, 'x').c_str()) == -1);
}
