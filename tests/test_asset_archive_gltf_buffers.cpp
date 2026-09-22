#include "catch.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "asset_archive_policy.h"
#include "modarchive.h"
#include "modasset_gltf_document.h"
}

namespace {
using Entries = std::vector<std::pair<std::string, std::string>>;

struct Archive {
    std::filesystem::path path;
    Archive(const Entries &entries, const char *extension = ".pdmesh")
    {
        static unsigned serial = 0;
        path = std::filesystem::temp_directory_path() /
            ("pd2-gltf-buffer-" + std::to_string(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(serial++) + extension);
        mod_archive_writer_t *writer = modArchiveBegin(path.string().c_str());
        REQUIRE(writer != nullptr);
        for (const auto &entry : entries) {
            if (modArchiveAddFileMem(writer, entry.first.c_str(), entry.second.data(),
                    static_cast<u32>(entry.second.size())) != MODARCHIVE_OK) {
                modArchiveAbort(writer);
                FAIL("failed to construct archive member " << entry.first);
            }
        }
        REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
    }
    ~Archive()
    {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
    std::string bytes() const
    {
        std::ifstream input(path, std::ios::binary);
        REQUIRE(input.good());
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }
};

std::string document(const char *uri = "geometry.bin", const char *length = "48")
{
    return std::string("{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"") +
        uri + "\",\"byteLength\":" + length +
        "}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48}]}";
}

Entries meshEntries(const std::string &json, size_t buffer_size = 48,
    const char *buffer_name = "models/geometry.bin")
{
    return {
        { "mesh.ini", "[mesh]\ncatalog_id=mod:gltf_buffer_test\nmodel_file=models/model.gltf\n" },
        { "models/model.gltf", json },
        { buffer_name, std::string(buffer_size, '\0') }
    };
}

void checkOpenedAndMemory(const Archive &archive, bool accepted)
{
    char error[512] = {0};
    mod_archive_t *opened = modArchiveOpen(archive.path.string().c_str());
    REQUIRE(opened != nullptr);
    const s32 result = assetArchiveValidateOpened(opened, archive.path.string().c_str(),
        ASSET_ARCHIVE_VALIDATE_RELEASE, error, sizeof(error));
    modArchiveClose(opened);
    INFO(error);
    CHECK((result == 0) == accepted);
    const std::string bytes = archive.bytes();
    error[0] = '\0';
    const s32 memory = assetArchiveValidateBytes(bytes.data(), static_cast<u32>(bytes.size()),
        archive.path.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE, error, sizeof(error));
    INFO(error);
    CHECK((memory == 0) == accepted);
}

void checkAllIngresses(const Entries &entries, bool accepted)
{
    Archive archive(entries);
    checkOpenedAndMemory(archive, accepted);
    Archive nested({
        { "weapon.ini", "[weapon]\ncatalog_id=mod:gltf_buffer_parent\nmodel_file=child.pdmesh\n" },
        { "child.pdmesh", archive.bytes() }
    }, ".pdweapon");
    checkOpenedAndMemory(nested, accepted);
}

std::string replace(std::string text, const std::string &from, const std::string &to)
{
    const size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}
} // namespace

TEST_CASE("glTF archive buffer admission follows decoded source-relative declarations",
    "[modding][pdxxx][c3842][gltf][archive]")
{
    checkAllIngresses(meshEntries(document()), true);
    checkAllIngresses(meshEntries(document("geometry%20data.bin"), 48,
        "models/geometry data.bin"), true);
    checkAllIngresses(meshEntries(document("geometry\\u0020data.bin"), 48,
        "models/geometry data.bin"), true);
    checkAllIngresses(meshEntries(document("data/geometry.bin"), 48,
        "models/data/geometry.bin"), true);
    // A same-named root file cannot satisfy a relative model-directory URI.
    checkAllIngresses(meshEntries(document(), 48, "geometry.bin"), false);
}

TEST_CASE("glTF archive buffer permission cannot admit raw or unrelated binary payloads",
    "[modding][pdxxx][c3842][gltf][archive]")
{
    checkAllIngresses(meshEntries(document(), 47), false);
    checkAllIngresses(meshEntries(document(), 49), false);
    checkAllIngresses(meshEntries(document("../geometry.bin")), false);
    checkAllIngresses(meshEntries(document("%2e%2e/geometry.bin")), false);
    checkAllIngresses(meshEntries(document("https://example.invalid/geometry.bin")), false);
    checkAllIngresses(meshEntries(document("geometry.bin?version=1")), false);
    checkAllIngresses(meshEntries(document("geometry%2fdata.bin")), false);
    checkAllIngresses(meshEntries(document("geometry.bin", "2147483648")), false);

    Entries entries = meshEntries(document());
    entries.emplace_back("unrelated.bin", std::string(48, '\0'));
    checkAllIngresses(entries, false);
    entries = meshEntries(document());
    entries[0].second += "runtime_file=models/geometry.bin\n";
    checkAllIngresses(entries, false);
    entries = meshEntries(document());
    entries.emplace_back("extra.json", "{\"model_file\":\"models/geometry.bin\"}");
    checkAllIngresses(entries, false);
    const std::string image = replace(document(), "\"buffers\":",
        "\"images\":[{\"uri\":\"geometry.bin\"}],\"buffers\":");
    checkAllIngresses(meshEntries(image), false);
    const std::string disguised = replace(document(), "\"buffers\":",
        "\"extras\":{\"uri\":\"geometry.bin\"},\"buffers\":");
    checkAllIngresses(meshEntries(disguised), false);
    const std::string escapedKey = replace(document(), "\"buffers\":",
        "\"extras\":{\"model\\u005ffile\":\"geometry.bin\"},\"buffers\":");
    checkAllIngresses(meshEntries(escapedKey), false);
}

TEST_CASE("glTF declaration parser rejects ambiguous JSON and invalid view ranges",
    "[modding][pdxxx][c3842][gltf][document]")
{
    const std::string valid = document();
    const std::vector<std::string> invalid = {
        "{}", "{\"asset\":{}}",
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{}]}",
        valid + " trailing", valid + "{}",
        replace(valid, "\"version\":\"2.0\"", "\"version\":\"1.0\""),
        replace(valid, "\"version\":\"2.0\"", "\"version\":\"2.0\",\"version\":\"2.0\""),
        replace(valid, "\"byteLength\":48", "\"byteLength\":48,\"byte\\u004cength\":48"),
        replace(valid, "\"byteLength\":48", "\"byteLength\":048"),
        replace(valid, "\"byteLength\":48", "\"byteLength\":48.5"),
        replace(valid, "\"byteLength\":48", "\"byteLength\":1e9999"),
        replace(valid, "\"buffer\":0", "\"buffer\":1"),
        replace(valid, "\"buffer\":0,", ""),
        replace(valid, "\"byteOffset\":0", "\"byteOffset\":1"),
        replace(valid, "\"byteOffset\":0", "\"byteOffset\":2147483647"),
        replace(valid, "\"byteOffset\":0", "\"byteOffset\":-1"),
        replace(valid, "\"buffer\":0", "\"buffer\":0,\"byteStride\":3"),
        replace(valid, "\"buffer\":0", "\"buffer\":0,\"target\":42"),
        replace(valid, "\"geometry.bin\"", "\"geometry\\q.bin\""),
        replace(valid, "\"geometry.bin\"", "\"geometry\\u0000.bin\""),
        replace(valid, "\"geometry.bin\"", "\"geometry\\ud800.bin\""),
        replace(valid, "geometry.bin", std::string("geometry") + char(0xff) + ".bin")
    };
    for (const std::string &json : invalid) {
        INFO(json);
        char uri[128] = "unchanged";
        u32 declared = 99;
        REQUIRE(modAssetGltfBufferDocument(json.data(), json.size(), 0,
            uri, sizeof(uri), &declared) == 0);
        REQUIRE(uri[0] == '\0');
        REQUIRE(declared == 0);
        checkAllIngresses(meshEntries(json), false);
    }
}

TEST_CASE("glTF buffer parser distinguishes GLB storage and bufferless native animation",
    "[modding][pdxxx][c3842][gltf][document]")
{
    const std::string glb = replace(document(), "\"uri\":\"geometry.bin\",", "");
    char uri[128] = "old";
    u32 declared = 0;
    REQUIRE(modAssetGltfBufferDocument(glb.data(), glb.size(), 1,
        uri, sizeof(uri), &declared) == 1);
    REQUIRE(uri[0] == '\0');
    REQUIRE(declared == 48);
    const std::string external = document();
    REQUIRE(modAssetGltfBufferDocument(external.data(), external.size(), 1,
        uri, sizeof(uri), &declared) == 0);
    REQUIRE(modAssetGltfBufferDocument(glb.data(), glb.size(), 0,
        uri, sizeof(uri), &declared) == 0);

    const std::string native = "{\"asset\":{\"version\":\"2.0\"},\"extras\":{\"pd_native_clip\":{}}}";
    Archive animation({
        { "animation.ini", "[animation]\ncatalog_id=mod:gltf_native\nanimation_file=animation.gltf\n" },
        { "animation.gltf", native }
    }, ".pdanim");
    checkOpenedAndMemory(animation, true);
    REQUIRE(modAssetGltfBufferDocument(native.data(), native.size(), 0,
        uri, sizeof(uri), &declared) == 0);
}

TEST_CASE("glTF optional buffers use decoded root declarations and validate bufferless documents",
    "[modding][pdxxx][c3842][gltf][document]")
{
    const std::string escaped = replace(document(), "\"buffers\"", "\"buff\\u0065rs\"");
    char uri[128] = "old";
    u32 declared = 99;
    REQUIRE(modAssetGltfOptionalBufferDocument(escaped.data(), escaped.size(), 0,
        uri, sizeof(uri), &declared) == 1);
    REQUIRE(std::string(uri) == "geometry.bin");
    REQUIRE(declared == 48);
    checkAllIngresses(meshEntries(escaped), true);

    const std::string escaped_glb = replace(escaped, "\"uri\":\"geometry.bin\",", "");
    REQUIRE(modAssetGltfOptionalBufferDocument(escaped_glb.data(), escaped_glb.size(), 1,
        uri, sizeof(uri), &declared) == 1);
    REQUIRE(uri[0] == '\0');
    REQUIRE(declared == 48);

    const std::string native = "{\"asset\":{\"version\":\"2.0\"},"
        "\"animations\":[{\"channels\":[],\"samplers\":[]}],"
        "\"extras\":{\"buffers\":\"custom metadata\",\"pd_zero_frame_placeholder\":true}}";
    for (s32 glb : { 0, 1 }) {
        for (const std::string &json : { native,
                replace(native, "\"animations\":", "\"bufferViews\":[],\"animations\":") }) {
            INFO(json);
            std::strcpy(uri, "old");
            declared = 99;
            REQUIRE(modAssetGltfOptionalBufferDocument(json.data(), json.size(), glb,
                uri, sizeof(uri), &declared) == 1);
            REQUIRE(uri[0] == '\0');
            REQUIRE(declared == 0);
        }
        const std::vector<std::string> invalid = {
            native + " trailing",
            native.substr(0, native.size() - 1),
            "{}",
            replace(native, "\"2.0\"", "\"1.0\""),
            replace(native, "\"asset\":", "\"asset\":{},\"asset\":"),
            replace(native, "\"animations\":", "\"bufferViews\":{},\"animations\":"),
            replace(native, "\"animations\":", "\"bufferViews\":[{\"buffer\":0,\"byteLength\":4}],\"animations\":"),
            replace(native, "\"animations\":", "\"buffers\":[],\"animations\":"),
            replace(native, "\"animations\":", "\"buff\\u0065rs\":null,\"animations\":")
        };
        for (const std::string &json : invalid) {
            INFO(json);
            std::strcpy(uri, "old");
            declared = 99;
            REQUIRE(modAssetGltfOptionalBufferDocument(json.data(), json.size(), glb,
                uri, sizeof(uri), &declared) == 0);
            REQUIRE(uri[0] == '\0');
            REQUIRE(declared == 0);
        }
    }
}

TEST_CASE("glTF admission validates embedded buffer bytes with the same declaration",
    "[modding][pdxxx][c3842][gltf][archive]")
{
    const std::string base64(64, 'A');
    const std::string valid = "data:application/octet-stream;base64," + base64;
    Entries entries = meshEntries(document(valid.c_str()));
    entries.pop_back();
    checkAllIngresses(entries, true);
    const std::vector<std::string> bad = {
        "data:application/octet-stream;base64," + base64.substr(1),
        "data:application/octet-stream;base64," + base64 + "AAAA",
        "data:application/octet-stream;base64,!!!!",
        "data:text/plain;base64," + base64
    };
    for (const std::string &uri : bad) {
        entries = meshEntries(document(uri.c_str()));
        entries.pop_back();
        checkAllIngresses(entries, false);
    }
}
