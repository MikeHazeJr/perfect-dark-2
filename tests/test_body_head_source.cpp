#include "catch.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>
#include "constants.h"

extern "C" {
#include "body_head_source.h"
#include "loader_enum_reverse.h"
#include "assetcatalog_model_slots.h"
#include "modarchive.h"
}

namespace {
// This exercises the archive reader and native-record builder. It deliberately
// does not stub a manager and call that production manager consumption proof.
std::vector<char> archiveBytes(const std::string &kind, const std::string &publicText,
        const std::string &privateText)
{
    struct Temp {
        std::filesystem::path path;
        ~Temp() { std::error_code error; std::filesystem::remove(path, error); }
    } temp {std::filesystem::temp_directory_path() / ("pd2-public-body-head-" +
        std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".zip")};
    auto *writer = modArchiveBegin(temp.path.string().c_str());
    REQUIRE(writer != nullptr);
    int result = modArchiveAddFileMem(writer, (kind + ".ini").c_str(),
        publicText.data(), static_cast<u32>(publicText.size()));
    if (result == MODARCHIVE_OK) result = modArchiveAddFileMem(writer,
        "_meta/manifest.json", privateText.data(), static_cast<u32>(privateText.size()));
    if (result != MODARCHIVE_OK) {
        modArchiveAbort(writer);
        FAIL("could not pack public/private divergence fixture");
    }
    REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
    std::ifstream file(temp.path, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::unique_ptr<ini_section_t> ini(const char *kind, const std::string &field = {},
        const std::string &value = {})
{
    auto out = std::make_unique<ini_section_t>();
    std::strcpy(out->type, kind);
    std::strcpy(out->pairs[0].key, "catalog_id");
    std::strcpy(out->pairs[0].value, "mod:edited");
    out->count = 1;
    if (!field.empty()) {
        REQUIRE(field.size() < sizeof(out->pairs[1].key));
        REQUIRE(value.size() < sizeof(out->pairs[1].value));
        std::strcpy(out->pairs[1].key, field.c_str());
        std::strcpy(out->pairs[1].value, value.c_str());
        out->count = 2;
    }
    return out;
}
}

TEST_CASE("public body archive scalars override stale generated metadata",
        "[modding][body-head][source][archive]")
{
    auto bytes = archiveBytes("body", "[body]\ncatalog_id=mod:edited\nismale=0\n"
        "unk00_01=1\ncanvaryheight=1\ntype=HEADBODYTYPE_MAIAN\nheight=199\n"
        "scale=0.75\nanimscale=1.25\nmesh_catalog_id=mod:mesh\nmesh_archive=mesh.pdmesh\n"
        "hand_catalog_id=mod:hand\nhand_archive=hand.pdmesh\n[meta]\n"
        "manifest=_meta/manifest.json\n", R"({"kind":"body","catalog_id":"mod:edited",
        "bodynum":3,"ismale":1,"unk00_01":0,"canvaryheight":0,"type":0,"height":100,
        "scale":9,"animscale":8,"mesh":17,"hand":18,"mesh_archive":"stale.pdmesh"})");
    body_head_source_t source = {};
    char error[128] = {};
    REQUIRE(bodyHeadSourceParseArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()),
        "mod:edited", 0, &source, error, sizeof(error)) == 1);
    body_data_t native = {};
    REQUIRE(bodyHeadSourceBuildBody(&source, CATALOG_MGR_BODY_CUSTOM_START, 2047, 2046, &native) == 1);
    CHECK(native.bodynum == CATALOG_MGR_BODY_CUSTOM_START);
    CHECK(std::string(native.catalog_id) == "mod:edited");
    CHECK(native.ismale == 0);
    CHECK(native.unk00_01 == 1);
    CHECK(native.canvaryheight == 1);
    CHECK(native.type == HEADBODYTYPE_MAIAN);
    CHECK(native.height == 199);
    CHECK(native.scale == Approx(0.75f));
    CHECK(native.animscale == Approx(1.25f));
    CHECK(native.filenum == 2047);
    CHECK(native.handfilenum == 2046);
    CHECK(std::string(native.hand_catalog_id) == "mod:hand");
    CHECK(native.modeldef == nullptr);
    CHECK(std::string(source.mesh_id) == "mod:mesh");
    CHECK(std::string(source.hand_id) == "mod:hand");
    CHECK(std::string(source.mesh_archive) == "mesh.pdmesh");
    CHECK(std::string(source.hand_archive) == "hand.pdmesh");
}

TEST_CASE("public head archive edits feed the native scalar candidate",
        "[modding][body-head][source][archive]")
{
    auto bytes = archiveBytes("head", "[head]\ncatalog_id=mod:edited\nismale=1\n"
        "unk00_01=1\ntype=HEADBODYTYPE_MRBLONDE\nheight=175\nscale=1.5\nanimscale=0.5\n"
        "mesh_archive=edited.pdmesh\n", R"({"kind":"head","catalog_id":"mod:edited",
        "headnum":2,"ismale":0,"unk00_01":0,"type":0,"height":1,"scale":8,"animscale":9,"mesh":17})");
    body_head_source_t source = {};
    char error[128] = {};
    REQUIRE(bodyHeadSourceParseArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()),
        "mod:edited", 1, &source, error, sizeof(error)) == 1);
    head_data_t native = {};
    REQUIRE(bodyHeadSourceBuildHead(&source, CATALOG_MGR_HEAD_CUSTOM_START, 0, &native) == 1);
    CHECK(native.headnum == CATALOG_MGR_HEAD_CUSTOM_START);
    CHECK(native.ismale == 1);
    CHECK(native.unk00_01 == 1);
    CHECK(native.type == HEADBODYTYPE_MRBLONDE);
    CHECK(native.height == 175);
    CHECK(native.scale == Approx(1.5f));
    CHECK(native.animscale == Approx(0.5f));
    CHECK(native.filenum == 0); // Direct catalog provider; no fabricated legacy file number.
}

TEST_CASE("omitted public scalar defaults never fall back to private echoes",
        "[modding][body-head][source][archive]")
{
    for (const std::string kind : {"body", "head"}) {
        auto bytes = archiveBytes(kind, "[" + kind + "]\ncatalog_id=mod:edited\nmesh_archive=mesh.pdmesh\n",
            R"({"ismale":1,"unk00_01":1,"type":5,"height":123,"scale":9,"animscale":8})");
        body_head_source_t source = {};
        REQUIRE(bodyHeadSourceParseArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()),
            "mod:edited", kind == "head", &source, nullptr, 0) == 1);
        CHECK(source.ismale == 0);
        CHECK(source.complete == 0);
        CHECK(source.type == 0);
        CHECK(source.height == 0);
        CHECK(source.scale == 1);
        CHECK(source.animscale == 1);
        CHECK(source.mesh_id[0] == 0); // Resolve from selected nested public mesh.ini at binding.
    }
}

TEST_CASE("invalid authored scalar and duplicate fields reject transactionally",
        "[modding][body-head][source]")
{
    const std::vector<std::pair<std::string, std::string>> invalid = {
        {"enabled", "2"}, {"requirefeature", "256"}, {"model_scale", "nan"},
        {"ismale", "2"}, {"unk00_01", "-1"}, {"height", "65536"}, {"height", "1.5"},
        {"height", "999999999999999999999999"}, {"canvaryheight", "2"},
        {"type", "HEADBODYTYPE_UNKNOWN"}, {"type", "6"}, {"scale", "nan"},
        {"scale", "0"}, {"scale", "-1"}, {"scale", "1e100"}, {"animscale", "1.2extra"},
        {"mesh_catalog_id", "123"}, {"hand_catalog_id", "mod:hand-without-archive"}};
    for (const auto &entry : invalid) {
        CAPTURE(entry.first, entry.second);
        auto document = ini("body", entry.first, entry.second);
        body_head_source_t out = {};
        out.height = 777;
        CHECK(bodyHeadSourceParseIni(document.get(), "mod:edited", 0, &out, nullptr, 0) == 0);
        CHECK(out.height == 777);
    }
    auto duplicate = ini("body", "catalog_id", "mod:edited");
    body_head_source_t out = {};
    CHECK(bodyHeadSourceParseIni(duplicate.get(), "mod:edited", 0, &out, nullptr, 0) == 0);
}

TEST_CASE("body head archive parser rejects duplicate sections and invalid UTF8",
        "[modding][body-head][source][archive]")
{
    const std::vector<std::string> invalid = {
        "[body]\ncatalog_id=mod:edited\n[body]\nheight=100\n",
        "[body]\ncatalog_id=mod:edited\nheight=1\nheight=2\n",
        std::string("[body]\ncatalog_id=mod:edited\nmesh_archive=") + char(0xc0) + char(0xaf),
        std::string("[body]\ncatalog_id=mod:edited\nmesh_archive=x") + char(0) + "hidden.pdmesh"};
    for (const auto &text : invalid) {
        auto bytes = archiveBytes("body", text, "{}");
        body_head_source_t out = {};
        out.height = 777;
        char error[128];
        std::memset(error, 'x', sizeof(error));
        CHECK(bodyHeadSourceParseArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()),
            "mod:edited", 0, &out, error, sizeof(error)) == 0);
        CHECK(out.height == 777);
        CHECK(std::memchr(error, 0, sizeof(error)) != nullptr);
    }
}

TEST_CASE("native scalar builders preserve checked caller owned slots and file bridges",
        "[modding][body-head][source]")
{
    auto document = ini("body", "hand_archive", "hand.pdmesh");
    body_head_source_t source = {};
    REQUIRE(bodyHeadSourceParseIni(document.get(), "mod:edited", 0, &source, nullptr, 0) == 1);
    body_data_t out = {};
    out.height = 777;
    CHECK(bodyHeadSourceBuildBody(&source, CATALOG_MGR_BODY_CUSTOM_START, 0, 0, &out) == 0);
    CHECK(bodyHeadSourceBuildBody(&source, CATALOG_MGR_BODY_TOTAL, 0, 1, &out) == 0);
    CHECK(bodyHeadSourceBuildBody(&source, 0, 65536, 1, &out) == 0);
    CHECK(bodyHeadSourceBuildBody(&source, 0, 1, 65536, &out) == 0);
    CHECK(out.height == 777);
    CHECK(bodyHeadSourceBuildBody(&source, 0, 65535, 65535, &out) == 0);
    std::strcpy(source.hand_id, "mod:resolved_hand");
    CHECK(bodyHeadSourceBuildBody(&source, 0, 65535, 65535, &out) == 1);
    CHECK(out.filenum == 65535);
    CHECK(out.handfilenum == 65535);
    CHECK(std::string(out.hand_catalog_id) == "mod:resolved_hand");
    CHECK(out.bodynum == 0);
}

TEST_CASE("public type selects the shared rig default and explicit rig stays authoritative",
        "[modding][body-head][source]")
{
    const struct { const char *name; unsigned native_type; const char *rig; } types[] = {
        {"HEADBODYTYPE_DEFAULT", HEADBODYTYPE_DEFAULT, "human_male_neck_standard"},
        {"HEADBODYTYPE_FEMALE", HEADBODYTYPE_FEMALE, "human_female_neck_standard"},
        {"HEADBODYTYPE_FEMALEGUARD", HEADBODYTYPE_FEMALEGUARD, "human_female_neck_standard"},
        {"HEADBODYTYPE_MAIAN", HEADBODYTYPE_MAIAN, "maian_tall_neck"},
        {"HEADBODYTYPE_CASS", HEADBODYTYPE_CASS, "cass_neck"},
        {"HEADBODYTYPE_MRBLONDE", HEADBODYTYPE_MRBLONDE, "mrblonde_neck"}
    };
    for (const auto &type : types) {
        const char *emitted_type = loaderEnumNameForHeadbodyType(type.native_type);
        REQUIRE(emitted_type != nullptr);
        CHECK(std::string(emitted_type) == type.name);
        for (const char *kind : {"body", "head"}) {
            INFO(type.name << " " << kind);
            auto named = ini(kind, "type", emitted_type);
            body_head_source_t parsed = {};
            REQUIRE(bodyHeadSourceParseIni(named.get(), "mod:edited", !std::strcmp(kind, "head"),
                &parsed, nullptr, 0) == 1);
            CHECK(parsed.type == type.native_type);
            CHECK(std::string(parsed.rig_class) == type.rig);
            auto numeric = ini(kind, "type", std::to_string(type.native_type).c_str());
            REQUIRE(bodyHeadSourceParseIni(numeric.get(), "mod:edited", !std::strcmp(kind, "head"),
                &parsed, nullptr, 0) == 1);
            CHECK(parsed.type == type.native_type);
            CHECK(std::string(parsed.rig_class) == type.rig);
        }
    }
    auto document = ini("body", "type", "HEADBODYTYPE_MAIAN");
    body_head_source_t source = {};
    REQUIRE(bodyHeadSourceParseIni(document.get(), "mod:edited", 0, &source, nullptr, 0) == 1);
    CHECK(std::string(source.rig_class) == "maian_tall_neck");
    std::strcpy(document->pairs[2].key, "rig_class");
    std::strcpy(document->pairs[2].value, "mod_neck");
    document->count = 3;
    REQUIRE(bodyHeadSourceParseIni(document.get(), "mod:edited", 0, &source, nullptr, 0) == 1);
    CHECK(std::string(source.rig_class) == "mod_neck");
    document->pairs[2].value[0] = 0;
    REQUIRE(bodyHeadSourceParseIni(document.get(), "mod:edited", 0, &source, nullptr, 0) == 1);
    CHECK(source.rig_class[0] == 0);
}

TEST_CASE("custom model slot snapshot rolls back nested reservations without changing prior ownership",
        "[modding][body-head][source][transaction]")
{
    using Snapshot = std::unique_ptr<void, decltype(&assetCatalogDestroyCustomModelSlotSnapshot)>;
    struct Restore {
        Snapshot value {assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot};
        ~Restore() { if (value) assetCatalogRestoreCustomModelSlots(value.get()); }
    } original;
    REQUIRE(original.value != nullptr);
    assetCatalogResetCustomModelSlots();
    const s32 retained = assetCatalogResolveModelPrivateSlot("test:retained_mesh");
    REQUIRE(retained >= 0);
    Snapshot outer(assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot);
    REQUIRE(outer != nullptr);
    const s32 child = assetCatalogResolveModelPrivateSlot("test:child_mesh");
    Snapshot inner(assetCatalogSnapshotCustomModelSlots(), assetCatalogDestroyCustomModelSlotSnapshot);
    REQUIRE(inner != nullptr);
    const s32 grandchild = assetCatalogResolveModelPrivateSlot("test:grandchild_mesh");
    CHECK(grandchild != child);
    REQUIRE(assetCatalogRestoreCustomModelSlots(inner.get()) == 1);
    CHECK(assetCatalogResolveModelPrivateSlot("test:child_mesh") == child);
    CHECK(assetCatalogResolveModelPrivateSlot("test:replacement_grandchild") == grandchild);
    REQUIRE(assetCatalogRestoreCustomModelSlots(outer.get()) == 1);
    CHECK(assetCatalogResolveModelPrivateSlot("test:retained_mesh") == retained);
    CHECK(assetCatalogResolveModelPrivateSlot("test:replacement_child") == child);
    CHECK(assetCatalogRestoreCustomModelSlots(nullptr) == 0);
}
