#include "catch.hpp"
#include <cstring>
#include <memory>
#include <string>

extern "C" {
#include "catalog_entry_snapshot.h"
#include "catalog_activation_ledger.h"
}

namespace {
void active(asset_entry_t &entry)
{
    std::memset(&entry, 0, sizeof(entry));
    std::strcpy(entry.id, "mod:snapshot_owner");
    entry.type = ASSET_CHARACTER;
    entry.occupied = entry.enabled = 1;
    entry.runtime_index = 73;
    entry.load_state = ASSET_STATE_ACTIVE;
    entry.payload_kind = ASSET_PAYLOAD_RUNTIME_ACTIVE;
    entry.loaded_data = &entry;
    entry.ref_count = 2;
    entry.stage_ref_count = 1;
}
}

TEST_CASE("catalog snapshot equality survives pool and snapshot relocation",
    "[catalog][snapshot][relocation]")
{
    auto original = std::make_unique<asset_entry_t>();
    auto moved = std::make_unique<asset_entry_t>();
    active(*original);
    catalog_entry_snapshot_t snapshot{};
    catalogEntrySnapshotCapture(&snapshot, original.get());
    REQUIRE(snapshot.runtime_self_marker == 1);
    REQUIRE(snapshot.row.loaded_data == nullptr);
    *moved = *original;
    moved->loaded_data = moved.get();
    original.reset();
    auto moved_snapshot = std::make_unique<catalog_entry_snapshot_t>(snapshot);
    std::memset(&snapshot, 0, sizeof(snapshot));
    REQUIRE(catalogEntrySnapshotMatches(moved_snapshot.get(), moved.get()));
    moved->ref_count++;
    REQUIRE_FALSE(catalogEntrySnapshotMatches(moved_snapshot.get(), moved.get()));
}

TEST_CASE("preserved snapshot restore rebases only its live self marker",
    "[catalog][snapshot][relocation]")
{
    asset_entry_t original{}, moved{};
    active(original);
    std::strcpy(original.descriptor_path, "body.pdbody::body.ini");
    catalog_entry_snapshot_t snapshot{};
    catalogEntrySnapshotCapture(&snapshot, &original);
    moved = original;
    moved.loaded_data = &moved;
    std::strcpy(moved.descriptor_path, "changed.pdbody::body.ini");
    REQUIRE_FALSE(catalogEntrySnapshotMatches(&snapshot, &moved));
    REQUIRE(catalogEntrySnapshotRestorePreserved(&moved, &snapshot));
    REQUIRE(moved.loaded_data == &moved);
    REQUIRE(moved.ref_count == 2);
    REQUIRE(moved.stage_ref_count == 1);
    REQUIRE(std::string(moved.descriptor_path) == "body.pdbody::body.ini");
}

TEST_CASE("retired snapshot rollback never resurrects old runtime ownership",
    "[catalog][snapshot][relocation]")
{
    asset_entry_t original{}, retired{};
    active(original);
    catalog_entry_snapshot_t snapshot{};
    catalogEntrySnapshotCapture(&snapshot, &original);
    retired = original;
    retired.loaded_data = nullptr;
    retired.payload_kind = ASSET_PAYLOAD_NONE;
    retired.load_state = ASSET_STATE_ENABLED;
    retired.ref_count = retired.stage_ref_count = 0;
    REQUIRE_FALSE(catalogEntrySnapshotMatches(&snapshot, &retired));
    REQUIRE_FALSE(catalogEntrySnapshotRestorePreserved(&retired, &snapshot));
    catalogActivationLedgerRestoreRetiredSnapshot(&retired, &snapshot.row);
    REQUIRE(retired.loaded_data == nullptr);
    REQUIRE(retired.payload_kind == ASSET_PAYLOAD_NONE);
    REQUIRE(retired.load_state == ASSET_STATE_ENABLED);
    REQUIRE(retired.ref_count == 0);
    REQUIRE(retired.stage_ref_count == 0);
}

TEST_CASE("snapshot keeps ordinary payload allocation addresses exact",
    "[catalog][snapshot][relocation]")
{
    asset_entry_t original{}, moved{};
    int payload = 123;
    active(original);
    original.loaded_data = &payload;
    original.data_size_bytes = sizeof(payload);
    catalog_entry_snapshot_t snapshot{};
    catalogEntrySnapshotCapture(&snapshot, &original);
    REQUIRE(snapshot.runtime_self_marker == 0);
    REQUIRE(snapshot.row.loaded_data == &payload);
    moved = original;
    REQUIRE(catalogEntrySnapshotMatches(&snapshot, &moved));
    REQUIRE(catalogEntrySnapshotRestorePreserved(&moved, &snapshot));
    REQUIRE(moved.loaded_data == &payload);
    moved.loaded_data = nullptr;
    REQUIRE_FALSE(catalogEntrySnapshotRestorePreserved(&moved, &snapshot));
}

TEST_CASE("missing runtime marker is not normalized into an owned marker",
    "[catalog][snapshot][relocation]")
{
    asset_entry_t original{}, moved{};
    active(original);
    original.loaded_data = nullptr;
    catalog_entry_snapshot_t snapshot{};
    catalogEntrySnapshotCapture(&snapshot, &original);
    REQUIRE(snapshot.runtime_self_marker == 0);
    moved = original;
    moved.loaded_data = &moved;
    REQUIRE_FALSE(catalogEntrySnapshotMatches(&snapshot, &moved));
    REQUIRE_FALSE(catalogEntrySnapshotRestorePreserved(&moved, &snapshot));
}

TEST_CASE("rejected replacement preserves never-loaded catalog admission state",
    "[catalog][snapshot][rollback]")
{
    for (s32 enabled : {0, 1}) {
        for (s32 state : {ASSET_STATE_REGISTERED, ASSET_STATE_ENABLED}) {
            asset_entry_t original{}, replacement{};
            std::strcpy(original.id, "mod:commands");
            original.type = ASSET_ANIMATION;
            original.occupied = 1;
            original.enabled = enabled;
            original.load_state = static_cast<asset_load_state_t>(state);
            std::strcpy(original.descriptor_path, "old.pdanim::animation.ini");
            replacement = original;
            std::strcpy(replacement.descriptor_path, "rejected.pdanim::animation.ini");
            catalogActivationLedgerRestoreRetiredSnapshot(&replacement, &original);
            REQUIRE(std::memcmp(&original, &replacement, sizeof(original)) == 0);
        }
    }
}
