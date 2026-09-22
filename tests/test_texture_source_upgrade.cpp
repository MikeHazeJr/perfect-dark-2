#include "catch.hpp"
#include "texture_source_upgrade.h"
#include "texture_stage_source.h"
#include "modasset_gltf_document.h"
#include "../port/external/imgui-node-editor/crude_json.h"
extern "C" {
#include "asset_archive_writer.h"
#include "modarchive.h"
#include "sha256.h"
}
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace {
struct Temporary {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("pd-texture-upgrade-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".zip");
    ~Temporary() { std::error_code error; std::filesystem::remove(path, error); }
};
std::string disk(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void writeDisk(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), bytes.size()); file.close(); REQUIRE(file.good());
}
std::map<std::string, std::string> members(const std::filesystem::path &path, std::string *comment = nullptr) {
    std::unique_ptr<mod_archive_t, decltype(&modArchiveClose)> archive(modArchiveOpen(path.string().c_str()), &modArchiveClose);
    REQUIRE(archive != nullptr);
    if (comment) *comment = modArchiveGetComment(archive.get());
    std::map<std::string, std::string> result;
    for (int i = 0; i < modArchiveGetEntryCount(archive.get()); ++i) {
        u32 size = 0;
        std::unique_ptr<void, decltype(&std::free)> data(modArchiveExtractAlloc(archive.get(), i, &size), &std::free);
        REQUIRE(data != nullptr);
        result.emplace(modArchiveGetEntryName(archive.get(), i), std::string(static_cast<const char *>(data.get()), size));
    }
    return result;
}
void makeArchive(const std::filesystem::path &path, const char *family, const char *id,
    const std::string &descriptor, const std::map<std::string, std::string> &sources) {
    auto *archive = modArchiveBegin(path.string().c_str()); REQUIRE(archive != nullptr);
    auto writer = std::make_unique<asset_archive_writer_t>();
    REQUIRE(assetArchiveWriterInit(writer.get(), archive, family, id) == MODARCHIVE_OK);
    const std::string ini = std::string(family) + ".ini";
    REQUIRE(assetArchiveWriterAddDescriptor(writer.get(), ini.c_str(), descriptor.data(), descriptor.size()) == MODARCHIVE_OK);
    for (const auto &entry : sources)
        REQUIRE(assetArchiveWriterAddPublicMem(writer.get(), entry.first.c_str(), entry.second.data(), entry.second.size(), "source") == MODARCHIVE_OK);
    const std::string custom = "{\"keep\":9007199254740993,\"spelling\":1.2300e+10}";
    REQUIRE(modArchiveAddFileMem(archive, "_meta/user-extension.json", custom.data(), custom.size()) == MODARCHIVE_OK);
    modArchiveSetComment(archive, "Keep my authored archive comment");
    REQUIRE(assetArchiveWriterFinishMetadata(writer.get()) == MODARCHIVE_OK);
    REQUIRE(modArchiveFinish(archive) == MODARCHIVE_OK);
}
texture_source_properties_t defaults() {
    texture_source_properties_t value{};
    value.surface_type = 2; value.sound_surface_type = 3;
    value.tile_column_offset = 1; value.tile_row_offset = 2;
    value.mask_s_reduction = 3; value.mask_t_reduction = 4;
    return value;
}
std::string hash(const std::string &text) {
    u8 digest[SHA256_DIGEST_SIZE]; char hex[SHA256_HEX_SIZE];
    sha256Hash(text.data(), text.size(), digest); sha256ToHex(digest, hex); return hex;
}
void checkRecord(const std::map<std::string, std::string> &files, const std::string &member) {
    for (const auto *name : {"_meta/inventory.json", "_meta/hashes.json"}) {
        crude_json::value document;
        REQUIRE(modAssetJsonReadValue(files.at(name).data(), files.at(name).size(), document));
        unsigned matches = 0;
        for (const auto &row : document["entries"].get<crude_json::array>())
            if (row["path"].get<crude_json::string>() == member) {
                ++matches;
                REQUIRE(row["sha256"].get<crude_json::string>() == hash(files.at(member)));
                REQUIRE(row["size"].get<crude_json::number>() == files.at(member).size());
            }
        REQUIRE(matches == 1);
    }
    REQUIRE(files.at("_meta/" + member + ".sha256") == hash(files.at(member)) + "\n");
}
const char *legacy = "; my texture\r\n[texture]\r\ncatalog_id = mod:paint\r\ntexture_file = edited.png\r\nsurface_type = glass\r\ncustom = keep\r\n[extension]\r\nvalue = unchanged";
}

TEST_CASE("Texture upgrade inserts only missing properties and preserves authored descriptor bytes", "[texture-source]") {
    const auto original = defaults(); char error[192], *output = nullptr; size_t size = 0;
    REQUIRE(textureSourceUpgradeDescriptor(legacy, std::strlen(legacy), "mod:paint", &original,
        &output, &size, error, sizeof(error)) == 1);
    const std::string changed(output, size); std::free(output);
    const auto insertion = std::string(legacy).find("catalog_id");
    REQUIRE(changed.substr(0, insertion) == std::string(legacy).substr(0, insertion));
    REQUIRE(changed.substr(changed.find("catalog_id")) == std::string(legacy).substr(insertion));
    REQUIRE(changed.find("surface_type = wood") == std::string::npos);
    texture_source_properties_t parsed{};
    REQUIRE(textureSourceReadProperties(changed.data(), changed.size(), "mod:paint", &parsed, error, sizeof(error)));
    REQUIRE(parsed.surface_type == 4);
    REQUIRE(parsed.sound_surface_type == 3);
    REQUIRE(parsed.present_mask == TEXTURE_SOURCE_ALL_PROPERTIES);
    REQUIRE(parsed.properties_version == 1);
    REQUIRE(textureSourceUpgradeDescriptor(changed.data(), changed.size(), "mod:paint", &original,
        &output, &size, error, sizeof(error)) == 0);
    REQUIRE(output == nullptr); REQUIRE(size == 0);
}
TEST_CASE("Versioned texture omissions remain defaults and malformed versions reject", "[texture-source]") {
    const auto original = defaults(); char error[192], *output = nullptr; size_t size = 0;
    const std::string source = "[texture]\ncatalog_id = mod:paint\nproperties_version = 1";
    REQUIRE(textureSourceUpgradeDescriptor(source.data(), source.size(), "mod:paint", &original,
        &output, &size, error, sizeof(error)) == 0);
    texture_source_properties_t parsed{};
    REQUIRE(textureSourceReadProperties(source.data(), source.size(), "mod:paint", &parsed, error, sizeof(error)));
    REQUIRE(parsed.present_mask == 0); REQUIRE(parsed.surface_type == 0);
    for (const auto *bad : {"properties_version = 2", "properties_version = 0", "surface_type = invalid",
            "surface_type = wood\nsurface_type = glass", "mask_s_reduction = -1", "tile_row_offset = 16"}) {
        const std::string text = std::string("[texture]\ncatalog_id = mod:paint\n") + bad;
        REQUIRE(textureSourceUpgradeDescriptor(text.data(), text.size(), "mod:paint", &original,
            &output, &size, error, sizeof(error)) == -1);
        REQUIRE(output == nullptr); REQUIRE(size == 0);
    }
}
TEST_CASE("Texture archive upgrade preserves edited images extras and comments with coherent hash records", "[texture-source]") {
    Temporary file;
    makeArchive(file.path, "texture", "mod:paint", legacy, {{"edited.png", "edited image bytes"}, {"notes.txt", "my notes"}});
    std::string before_comment, after_comment;
    const auto before = members(file.path, &before_comment);
    const auto original = defaults(); char error[192];
    REQUIRE(textureSourceUpgradeArchive(file.path.string().c_str(), "mod:paint", &original, error, sizeof(error)) == 1);
    const auto after = members(file.path, &after_comment);
    REQUIRE(before.size() == after.size()); REQUIRE(before_comment == after_comment);
    for (const auto &entry : before) if (entry.first != "texture.ini" && entry.first != "_meta/texture.ini.sha256"
            && entry.first != "_meta/inventory.json" && entry.first != "_meta/hashes.json") REQUIRE(after.at(entry.first) == entry.second);
    checkRecord(after, "texture.ini");
    const auto unchanged = disk(file.path);
    REQUIRE(textureSourceUpgradeArchive(file.path.string().c_str(), "mod:paint", &original, error, sizeof(error)) == 0);
    REQUIRE(disk(file.path) == unchanged);
}
TEST_CASE("Texture upgrade rejects incomplete archive views without replacing the original", "[texture-source]") {
    Temporary file;
    makeArchive(file.path, "texture", "mod:paint", legacy, {{"edited.png", "image"}, {"unsupported.txt", "keep me"}});
    std::string damaged = disk(file.path);
    bool changed = false;
    for (size_t at = 0; at + 46 < damaged.size(); ++at) if (!std::memcmp(damaged.data() + at, "PK\1\2", 4)
            && damaged.compare(at + 46, std::strlen("unsupported.txt"), "unsupported.txt") == 0) {
        damaged[at + 10] = 99; damaged[at + 11] = 0; changed = true;
    }
    REQUIRE(changed); writeDisk(file.path, damaged);
    const auto original = defaults(); char error[192];
    REQUIRE(textureSourceUpgradeArchive(file.path.string().c_str(), "mod:paint", &original, error, sizeof(error)) == -1);
    REQUIRE(disk(file.path) == damaged);
    REQUIRE(modArchiveReplaceFileMem(file.path.string().c_str(), "texture.ini", legacy, std::strlen(legacy)) == MODARCHIVE_ERR_FORMAT);
    REQUIRE(disk(file.path) == damaged);
}
TEST_CASE("Stage texture migration preserves standalone and embedded authored graph topology", "[texture-source]") {
    Temporary scenario, arena;
    const std::string graph = "{\n\"schema\":\"pd2.level.graph.v1\",\"nodes\":[{\"id\":\"my edit\"}],\"large\":9007199254740993,\"precise\":1.2300e+10}";
    makeArchive(scenario.path, "scenario", "mod:stage", "[scenario]\ncatalog_id = mod:stage\n", {{"level.graph.json", graph}, {"scene.glb", "edited geometry"}});
    const std::string member = "dependencies/assets/scenarios/my.pdscenario";
    makeArchive(arena.path, "arena", "mod:arena", "[arena]\ncatalog_id = mod:arena\n", {{member, disk(scenario.path)}});
    const char *properties = "{\"mode\":\"multiplayer\",\"entries\":[{\"texture\":\"mod:paint\",\"surface_type\":\"wood\"}]}";
    char error[192];
    REQUIRE(textureSourceUpgradeStageArchive(arena.path.string().c_str(), member.c_str(), properties, error, sizeof(error)) == 1);
    const auto outer = members(arena.path); checkRecord(outer, member);
    writeDisk(scenario.path, outer.at(member));
    const auto inner = members(scenario.path); checkRecord(inner, "level.graph.json");
    REQUIRE(inner.at("level.graph.json").find(graph.substr(1)) != std::string::npos);
    REQUIRE(inner.at("scene.glb") == "edited geometry");
    const auto accepted = disk(arena.path);
    REQUIRE(textureSourceUpgradeStageArchive(arena.path.string().c_str(), member.c_str(), "{}", error, sizeof(error)) == 0);
    REQUIRE(disk(arena.path) == accepted);
}
TEST_CASE("Rejected descriptor and metadata upgrades preserve the entire archive", "[texture-source]") {
    Temporary file; const auto original = defaults(); char error[192];
    makeArchive(file.path, "texture", "mod:paint", legacy, {{"edited.png", "authored bytes"}});
    const auto valid = disk(file.path);
    REQUIRE(textureSourceUpgradeArchive(file.path.string().c_str(), "mod:wrong", &original, error, sizeof(error)) == -1);
    REQUIRE(disk(file.path) == valid);
    const char *invalid = "{\"schema\":\"pd2.asset.hashes.v1\",\"entries\":[";
    REQUIRE(modArchiveReplaceFileMem(file.path.string().c_str(), "_meta/hashes.json", invalid, std::strlen(invalid)) == MODARCHIVE_OK);
    const auto malformed = disk(file.path);
    REQUIRE(textureSourceUpgradeArchive(file.path.string().c_str(), "mod:paint", &original, error, sizeof(error)) == -1);
    REQUIRE(disk(file.path) == malformed);
}
TEST_CASE("Stage property source validates all rows before mode-scoped native visits", "[texture-source]") {
    using Rows = std::vector<std::pair<std::string, texture_source_properties_t>>;
    Rows rows; char error[192];
    const auto visitor = [](const char *id, const texture_source_properties_t *p, void *ctx) {
        static_cast<Rows *>(ctx)->emplace_back(id, *p); return 0;
    };
    const std::string prefix = "{\"texture_properties_version\":1,\"texture_properties\":{\"mode\":\"multiplayer\",\"entries\":[";
    const std::string good = prefix + "{\"texture\":\"mod:paint\",\"surface_type\":\"wood\",\"sound_surface_type\":\"metal\"}]}}";
    REQUIRE(textureStageSourceRead(good.data(), good.size(), 0, 1, visitor, &rows, error, sizeof(error)));
    REQUIRE(rows.empty());
    REQUIRE(textureStageSourceRead(good.data(), good.size(), 1, 1, visitor, &rows, error, sizeof(error)));
    REQUIRE(rows.size() == 1); REQUIRE(rows[0].first == "mod:paint");
    REQUIRE(rows[0].second.surface_type == 2); REQUIRE(rows[0].second.sound_surface_type == 3);
    rows.clear();
    const std::string bad = prefix + "{\"texture\":\"mod:paint\",\"surface_type\":\"wood\"},{\"texture\":\"mod:bad\",\"surface_type\":\"unknown\"}]}}";
    REQUIRE_FALSE(textureStageSourceRead(bad.data(), bad.size(), 1, 1, visitor, &rows, error, sizeof(error)));
    REQUIRE(rows.empty());
    const std::string omission = "{\"texture_properties_version\":1}";
    REQUIRE(textureStageSourceRead(omission.data(), omission.size(), 1, 1, visitor, &rows, error, sizeof(error)));
    REQUIRE_FALSE(textureStageSourceRead("{}", 2, 1, 1, visitor, &rows, error, sizeof(error)));
    REQUIRE(textureStageSourceRead("{}", 2, 1, 0, visitor, &rows, error, sizeof(error)));
}
