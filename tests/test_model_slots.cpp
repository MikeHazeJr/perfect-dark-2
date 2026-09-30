/*
 * tests/test_model_slots.cpp -- B-911 (c3848): catalog-owned private custom-model
 * runtime-slot allocator.
 *
 * Source under test: port/src/assetcatalog_model_slots.c
 *   assetCatalogResolveModelPrivateSlot / assetCatalogResetCustomModelSlots
 *
 * The model analog of the proven weapon and body/head custom-slot allocators. A
 * custom .pdweapon's embedded .pdmesh (referenced by a catalog-ID model_ref in
 * its projectile graph) has no base g_ModelStates index; this allocator maps such
 * a catalog ID to a private runtime slot in [MODEL_CUSTOM_START, MODEL_CUSTOM_END)
 * so the existing integer render path assembles it from public source. Pinned
 * behaviors: in-range, dedup-by-id, distinct ids get distinct slots, empty id
 * rejected, exhaustion loud-fails (-1), reset clears.
 */

#include "catch.hpp"

#include <cstdio>
#include <memory>
#include <string>

extern "C" {
#include "constants.h" /* MODEL_CUSTOM_START / MODEL_CUSTOM_COUNT / MODEL_CUSTOM_END */
#include "assetcatalog_model_slots.h"
}

namespace {
using SlotSnapshot = std::unique_ptr<void, decltype(&assetCatalogDestroyCustomModelSlotSnapshot)>;
struct ModelSlotPin {
    const char *id;
    s32 slot;
    bool held;
    ModelSlotPin(const char *id, s32 slot) : id(id), slot(slot),
        held(assetCatalogPinModelPrivateSlot(id, slot) != 0) {}
    ~ModelSlotPin() { release(); }
    void release() {
        if (held) { assetCatalogReleaseModelPrivateSlot(id, slot); held = false; }
    }
};
}

TEST_CASE("model slot generations prevent reuse through reset until the final lease",
          "[catalog][model][slots][modding][pdxxx][c3842]") {
    assetCatalogResetCustomModelSlots();
    const auto slot = assetCatalogResolveModelPrivateSlot("mod_pin:model_old");
    ModelSlotPin first("mod_pin:model_old", slot), second("mod_pin:model_old", slot);
    REQUIRE(first.held); REQUIRE(second.held);
    assetCatalogResetCustomModelSlots();
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_new") != slot);
    first.release();
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_third") != slot);
    second.release();
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_reuse") == slot);
}

TEST_CASE("model slot empty rollback preserves live pins without restoring reference counts",
          "[catalog][model][slots][modding][pdxxx][c3842]") {
    assetCatalogResetCustomModelSlots();
    SlotSnapshot empty(assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot);
    REQUIRE(empty);
    const auto slot = assetCatalogResolveModelPrivateSlot("mod_pin:model_child");
    ModelSlotPin pin("mod_pin:model_child", slot);
    REQUIRE(pin.held);
    REQUIRE(assetCatalogRestoreCustomModelSlots(empty.get()));
    REQUIRE(assetCatalogRestoreCustomModelSlots(empty.get()));
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_other") != slot);
    pin.release();
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_reuse") == slot);
}

TEST_CASE("model slot restored catalog reservation survives its last generation release",
          "[catalog][model][slots][modding][pdxxx][c3842]") {
    assetCatalogResetCustomModelSlots();
    const auto slot = assetCatalogResolveModelPrivateSlot("mod_pin:model_reserved");
    SlotSnapshot saved(assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot);
    REQUIRE(saved);
    ModelSlotPin pin("mod_pin:model_reserved", slot);
    REQUIRE(pin.held);
    assetCatalogResetCustomModelSlots();
    REQUIRE(assetCatalogRestoreCustomModelSlots(saved.get()));
    pin.release();
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_other") != slot);
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_reserved") == slot);
}

TEST_CASE("model slot conflicting rollback rejects atomically until live source retirement",
          "[catalog][model][slots][modding][pdxxx][c3842]") {
    assetCatalogResetCustomModelSlots();
    const auto old = assetCatalogResolveModelPrivateSlot("mod_pin:model_before");
    SlotSnapshot saved(assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot);
    REQUIRE(saved);
    assetCatalogResetCustomModelSlots();
    const auto newer = assetCatalogResolveModelPrivateSlot("mod_pin:model_after");
    REQUIRE(newer == old);
    ModelSlotPin pin("mod_pin:model_after", newer);
    REQUIRE(pin.held);
    const auto other = assetCatalogResolveModelPrivateSlot("mod_pin:model_other");
    CHECK(assetCatalogRestoreCustomModelSlots(saved.get()) == 0);
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_after") == newer);
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_other") == other);
    pin.release();
    REQUIRE(assetCatalogRestoreCustomModelSlots(saved.get()));
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_before") == old);
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_reuse_other") == other);
}

TEST_CASE("model slot rollback rejects moving a retained identity to a different slot",
          "[catalog][model][slots][modding][pdxxx][c3842]") {
    assetCatalogResetCustomModelSlots();
    assetCatalogResolveModelPrivateSlot("mod_pin:model_blocker");
    const auto old = assetCatalogResolveModelPrivateSlot("mod_pin:model_move");
    ModelSlotPin original("mod_pin:model_move", old);
    REQUIRE(original.held);
    assetCatalogResetCustomModelSlots();
    REQUIRE(assetCatalogResolveModelPrivateSlot("mod_pin:model_move") == old);
    SlotSnapshot saved(assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot);
    REQUIRE(saved);
    original.release();
    assetCatalogResetCustomModelSlots();
    const auto moved = assetCatalogResolveModelPrivateSlot("mod_pin:model_move");
    REQUIRE(moved != old);
    ModelSlotPin pin("mod_pin:model_move", moved);
    REQUIRE(pin.held);
    CHECK(assetCatalogRestoreCustomModelSlots(saved.get()) == 0);
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_move") == moved);
}

TEST_CASE("model slot leases require exact allocated identity and balanced releases",
          "[catalog][model][slots][modding][pdxxx][c3842]") {
    assetCatalogResetCustomModelSlots();
    const auto slot = assetCatalogResolveModelPrivateSlot("mod_pin:model_exact");
    CHECK(assetCatalogPinModelPrivateSlot("mod_pin:model_foreign", slot) == 0);
    CHECK(assetCatalogPinModelPrivateSlot(nullptr, slot) == 0);
    CHECK(assetCatalogPinModelPrivateSlot("mod_pin:model_exact", MODEL_CUSTOM_END) == 0);
    CHECK(assetCatalogReleaseModelPrivateSlot("mod_pin:model_exact", slot) == 0);
    ModelSlotPin pin("mod_pin:model_exact", slot);
    REQUIRE(pin.held);
    CHECK(assetCatalogReleaseModelPrivateSlot("mod_pin:model_foreign", slot) == 0);
    assetCatalogResetCustomModelSlots();
    pin.release();
    CHECK(assetCatalogReleaseModelPrivateSlot("mod_pin:model_exact", slot) == 0);
    CHECK(assetCatalogResolveModelPrivateSlot("mod_pin:model_reuse") == slot);
    CHECK(assetCatalogResolveModelPrivateSlot(std::string(1024, 'x').c_str()) == -1);
}

TEST_CASE("model slots: allocation is in the private custom range",
          "[catalog][model][slots][c3848]") {
	assetCatalogResetCustomModelSlots();
	s32 slot = assetCatalogResolveModelPrivateSlot("mod_needler:needle");
	REQUIRE(slot >= MODEL_CUSTOM_START);
	REQUIRE(slot < MODEL_CUSTOM_END);
}

TEST_CASE("model slots: same id dedups to the same slot",
          "[catalog][model][slots][c3848]") {
	assetCatalogResetCustomModelSlots();
	s32 a = assetCatalogResolveModelPrivateSlot("mod_needler:needle");
	s32 b = assetCatalogResolveModelPrivateSlot("mod_needler:needle");
	REQUIRE(a == b);
}

TEST_CASE("model slots: distinct ids get distinct slots",
          "[catalog][model][slots][c3848]") {
	assetCatalogResetCustomModelSlots();
	s32 a = assetCatalogResolveModelPrivateSlot("mod_a:mesh_a");
	s32 b = assetCatalogResolveModelPrivateSlot("mod_a:mesh_b");
	REQUIRE(a != b);
	REQUIRE(a >= MODEL_CUSTOM_START);
	REQUIRE(b >= MODEL_CUSTOM_START);
}

TEST_CASE("model slots: empty / null id is rejected",
          "[catalog][model][slots][c3848]") {
	assetCatalogResetCustomModelSlots();
	REQUIRE(assetCatalogResolveModelPrivateSlot("") == -1);
	REQUIRE(assetCatalogResolveModelPrivateSlot(nullptr) == -1);
}

TEST_CASE("model slots: exhaustion loud-fails with -1",
          "[catalog][model][slots][c3848]") {
	assetCatalogResetCustomModelSlots();
	/* Fill every private slot with a distinct id. */
	for (s32 i = 0; i < MODEL_CUSTOM_COUNT; i++) {
		char id[32];
		snprintf(id, sizeof(id), "mod_a:mesh_%d", i);
		s32 slot = assetCatalogResolveModelPrivateSlot(id);
		REQUIRE(slot >= MODEL_CUSTOM_START);
		REQUIRE(slot < MODEL_CUSTOM_END);
	}
	/* One more distinct id has nowhere to go. */
	REQUIRE(assetCatalogResolveModelPrivateSlot("mod_a:mesh_overflow") == -1);
	/* A previously-seen id still dedups even when full. */
	REQUIRE(assetCatalogResolveModelPrivateSlot("mod_a:mesh_0")
	        == MODEL_CUSTOM_START);
}

TEST_CASE("model slots: reset clears reservations",
          "[catalog][model][slots][c3848]") {
	assetCatalogResetCustomModelSlots();
	s32 first = assetCatalogResolveModelPrivateSlot("mod_a:mesh_a");
	assetCatalogResetCustomModelSlots();
	/* After reset a different id can take the first slot again. */
	s32 reused = assetCatalogResolveModelPrivateSlot("mod_a:mesh_b");
	REQUIRE(reused == first);
}

TEST_CASE("model slots: custom range sits directly above the base model count",
          "[catalog][model][slots][c3848]") {
	/* The custom range must follow NUM_MODELS so a slot value is a valid index
	 * into the grown g_ModelStates[MODEL_CUSTOM_END]. */
	REQUIRE(MODEL_CUSTOM_START == NUM_MODELS);
	REQUIRE(MODEL_CUSTOM_END == NUM_MODELS + MODEL_CUSTOM_COUNT);
	assetCatalogResetCustomModelSlots();
	s32 slot = assetCatalogResolveModelPrivateSlot("mod_a:first");
	REQUIRE(slot == NUM_MODELS);
}

TEST_CASE("model slots: private runtime slots map to private source filenums",
          "[catalog][model][slots][c3848][c3849]") {
	REQUIRE(assetCatalogModelPrivateSourceFilenum(MODEL_CUSTOM_START) == 0x7e0);
	REQUIRE(assetCatalogModelPrivateSourceFilenum(MODEL_CUSTOM_END - 1) == 0x7ff);
	REQUIRE(assetCatalogModelPrivateSourceFilenum(MODEL_CUSTOM_START - 1) == -1);
	REQUIRE(assetCatalogModelPrivateSourceFilenum(MODEL_CUSTOM_END) == -1);
}
