// B-1112: the archive validator skipped the reference checks of every text
// member over 4 MiB. All 45 base arenas with a nested scenario once carried
// the same dangling `mtllib scenario.mtl` line in collision.obj; the four whose
// collision source is over 4 MiB passed validation and the other 41 failed.
// A large member is exactly where a missing reference is most likely to go
// unseen. Text members are now checked whole up to 128 MiB, and a larger one
// refuses the archive instead of passing unchecked.
#include "catch.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "asset_archive_policy.h"
#include "modarchive.h"
}

namespace {
using Entries = std::vector<std::pair<std::string, std::string>>;

struct Archive {
    std::filesystem::path path;
    Archive(const Entries &entries, const char *extension)
    {
        static unsigned serial = 0;
        path = std::filesystem::temp_directory_path() /
            ("pd2-large-text-" + std::to_string(
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

// A small valid mesh archive (as in test_asset_archive_gltf_buffers.cpp) with
// one extra collision.obj member.
Entries meshWithCollision(const std::string &obj, bool with_mtl)
{
    Entries entries = {
        { "mesh.ini", "[mesh]\ncatalog_id=mod:large_text_test\nmodel_file=models/model.gltf\n" },
        { "models/model.gltf", "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"geometry.bin\","
            "\"byteLength\":48}],\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48}]}" },
        { "models/geometry.bin", std::string(48, '\0') },
        { "collision.obj", obj },
    };
    if (with_mtl) {
        entries.push_back({ "collision.mtl", "newmtl solid\nKd 1 1 1\n" });
    }
    return entries;
}

// An OBJ of at least `bytes` bytes whose first line names collision.mtl.
std::string objOfSize(size_t bytes, bool names_mtl)
{
    std::string obj = names_mtl ? "mtllib collision.mtl\n" : "o collision\n";
    obj.reserve(bytes + 16);
    while (obj.size() < bytes) {
        obj += "v 0 0 0\n";
    }
    return obj;
}

s32 validateOpened(const Archive &archive, std::string &error)
{
    char buffer[512] = {0};
    mod_archive_t *opened = modArchiveOpen(archive.path.string().c_str());
    REQUIRE(opened != nullptr);
    const s32 result = assetArchiveValidateOpened(opened, archive.path.string().c_str(),
        ASSET_ARCHIVE_VALIDATE_RELEASE, buffer, sizeof(buffer));
    modArchiveClose(opened);
    error = buffer;
    return result;
}

s32 validateBytes(const Archive &archive, std::string &error)
{
    char buffer[512] = {0};
    const std::string bytes = archive.bytes();
    const s32 result = assetArchiveValidateBytes(bytes.data(), static_cast<u32>(bytes.size()),
        archive.path.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE, buffer, sizeof(buffer));
    error = buffer;
    return result;
}

// The same archive through all three ingresses: opened file, memory, and
// nested inside a parent archive.
void checkAll(const Entries &entries, bool accepted, const char *expected_error = nullptr)
{
    Archive archive(entries, ".pdmesh");
    Archive parent({
        { "weapon.ini", "[weapon]\ncatalog_id=mod:large_text_parent\nmodel_file=child.pdmesh\n" },
        { "child.pdmesh", archive.bytes() },
    }, ".pdweapon");
    for (int ingress = 0; ingress < 3; ingress++) {
        std::string error;
        const s32 result = ingress == 0 ? validateOpened(archive, error)
            : ingress == 1 ? validateBytes(archive, error)
            : validateOpened(parent, error);
        INFO("ingress " << ingress << ": " << error);
        CHECK((result == 0) == accepted);
        if (expected_error) {
            CHECK(error.find(expected_error) != std::string::npos);
        }
    }
}
}

TEST_CASE("a text member over the old 4 MiB limit is still checked for references",
    "[modding][pdxxx][archive][b1112]")
{
    const size_t over_old_limit = 4u * 1024u * 1024u + 4096u;

    // Below the old limit a dangling mtllib was always refused, and still is.
    checkAll(meshWithCollision(objOfSize(4096, true), false), false, "collision.mtl");
    // Above it the member used to pass unchecked; now it is refused the same way.
    checkAll(meshWithCollision(objOfSize(over_old_limit, true), false), false, "collision.mtl");
    // A large member whose reference resolves, or that names none, passes.
    checkAll(meshWithCollision(objOfSize(over_old_limit, true), true), true);
    checkAll(meshWithCollision(objOfSize(over_old_limit, false), false), true);
}

// Opt-in sweep (hidden from the default run): every typed archive under
// PD2_VALIDATE_ARCHIVE_ROOT through the validator in the mode the asset
// catalog uses at boot, so a change to the validator can be checked against a
// whole extracted base before it ships. Read-only.
TEST_CASE("every typed archive under PD2_VALIDATE_ARCHIVE_ROOT passes release validation",
    "[.][b1112-sweep]")
{
    const char *root = std::getenv("PD2_VALIDATE_ARCHIVE_ROOT");
    if (!root || !root[0]) {
        WARN("PD2_VALIDATE_ARCHIVE_ROOT is not set; nothing to sweep");
        return;
    }
    size_t checked = 0;
    size_t refused = 0;
    std::error_code walk_error;
    for (std::filesystem::recursive_directory_iterator it(root, walk_error), end;
            !walk_error && it != end; it.increment(walk_error)) {
        if (!it->is_regular_file()) {
            continue;
        }
        const std::string path = it->path().string();
        if (!assetArchivePathIsTyped(path.c_str())) {
            continue;
        }
        char error[512] = {0};
        checked++;
        if (assetArchiveValidateFile(path.c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE,
                error, sizeof(error)) != 0) {
            if (refused++ < 25) {
                WARN("refused: " << path << ": " << error);
            }
        }
    }
    REQUIRE_FALSE(walk_error);
    WARN("validated " << checked << " typed archives under " << root << ", refused " << refused);
    CHECK(checked > 0);
    CHECK(refused == 0);
}

TEST_CASE("a text member too large to check refuses the archive instead of passing",
    "[modding][pdxxx][archive][b1112]")
{
    std::ifstream input("port/src/asset_archive_policy.c", std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream out;
    out << input.rdbuf();
    const std::string policy = out.str();

    REQUIRE(policy.find("#define ASSET_ARCHIVE_TEXT_SCAN_LIMIT (128u * 1024u * 1024u)") != std::string::npos);
    REQUIRE(policy.find("if (!text && size > ASSET_ARCHIVE_TEXT_SCAN_LIMIT) return 0;") == std::string::npos);
    size_t refusals = 0;
    for (size_t at = policy.find("return refuseOversizedText("); at != std::string::npos;
            at = policy.find("return refuseOversizedText(", at + 1)) {
        refusals++;
    }
    // The opened and memory manifest checks and the opened and memory member checks.
    REQUIRE(refusals == 4);
    // An oversized first manifest is not skipped by reading the other spelling.
    REQUIRE(policy.find("if (!text && size == 0) {\n\t\tmanifest = ASSET_ARCHIVE_META_MANIFEST;") !=
        std::string::npos);
}
