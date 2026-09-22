#include "catch.hpp"
#include "modmgr_enabled_state.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <chrono>
#include "save_atomic.h"

namespace {
modinfo_t row(const char *id, int enabled, int session = 0)
{
    modinfo_t result{};
    std::snprintf(result.id, sizeof(result.id), "%s", id);
    std::snprintf(result.name, sizeof(result.name), "Metadata for %s", id);
    std::snprintf(result.dirpath, sizeof(result.dirpath), "mods/%s", id);
    result.enabled = enabled;
    result.loaded = 1;
    result.session_only = session;
    result.contenthash = static_cast<unsigned char>(id[0]);
    return result;
}
template<size_t N>
int load(const std::string &json, std::array<modinfo_t, N> &rows, char *error)
{
    return modmgrLoadEnabledDocument(json.data(), json.size(), rows.data(),
        static_cast<int>(rows.size()), error, 256);
}
struct SavedFile {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("pd2-modmgr-state-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    std::filesystem::path path = dir / "mods-enabled.json";
    SavedFile() { std::filesystem::create_directory(dir); }
    ~SavedFile() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
    std::string read() const {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    size_t files() const { return static_cast<size_t>(std::distance(
        std::filesystem::directory_iterator(dir), std::filesystem::directory_iterator())); }
};
}

TEST_CASE("Mod Manager legacy list retains every maximum-length enabled ID", "[modmgr][persistence][legacy-list]")
{
    std::array<modinfo_t, MODMGR_MAX_MODS> rows{};
    std::string expected;
    for (size_t i = 0; i < rows.size(); ++i) {
        std::string id(MODMGR_ID_LEN - 3, 'm');
        id += static_cast<char>('A' + i / 26);
        id += static_cast<char>('A' + i % 26);
        rows[i] = row(id.c_str(), 1);
        if (i) expected += ',';
        expected += id;
    }
    const auto before = rows;
    std::array<char, 2049> output{};
    output.fill('!');
    char error[256] = "stale";
    REQUIRE(expected.size() == 2047);
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), static_cast<int>(rows.size()),
        output.data(), 2048, error, sizeof(error)) == 1);
    REQUIRE(std::string(output.data()) == expected);
    REQUIRE(output[2047] == '\0');
    REQUIRE(output[2048] == '!');
    REQUIRE(error[0] == '\0');
    REQUIRE(std::memcmp(rows.data(), before.data(), sizeof(rows)) == 0);
}

TEST_CASE("Mod Manager legacy list preserves output on insufficient capacity", "[modmgr][persistence][legacy-list]")
{
    std::array<modinfo_t, 2> rows = {row("alpha", 1), row("beta", 1)};
    std::array<char, 11> output{};
    output.fill('!');
    const auto before = output;
    char error[256]{};
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), 2, output.data(), 10, error, sizeof(error)) == 0);
    REQUIRE(output == before);
    REQUIRE(error[0] != '\0');
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), 2, output.data(), 11, error, sizeof(error)) == 1);
    REQUIRE(std::string(output.data()) == "alpha,beta");
}

TEST_CASE("Mod Manager legacy list retains persistent order and excludes session rows", "[modmgr][persistence][legacy-list]")
{
    std::array<modinfo_t, 4> rows = {row("second", 1), row("session", 1, 1),
        row("disabled", 0), row("first", 1)};
    char output[64]{};
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), 4, output, sizeof(output), nullptr, 0) == 1);
    REQUIRE(std::string(output) == "second,first");
    REQUIRE(modmgrBuildEnabledCsv(nullptr, 0, output, 1, nullptr, 0) == 1);
    REQUIRE(output[0] == '\0');
}

TEST_CASE("Mod Manager legacy list rejects invalid registries without output changes", "[modmgr][persistence][legacy-list]")
{
    std::array<modinfo_t, 2> rows = {row("a", 1), row("a", 1)};
    char output[32] = "retained";
    char error[256]{};
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), 2, output, sizeof(output), error, sizeof(error)) == 0);
    REQUIRE(std::string(output) == "retained");
    std::memset(rows[1].id, 'x', sizeof(rows[1].id));
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), 2, output, sizeof(output), error, sizeof(error)) == 0);
    REQUIRE(std::string(output) == "retained");
    REQUIRE(modmgrBuildEnabledCsv(nullptr, 1, output, sizeof(output), error, sizeof(error)) == 0);
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), -1, output, sizeof(output), error, sizeof(error)) == 0);
    REQUIRE(modmgrBuildEnabledCsv(rows.data(), MODMGR_MAX_MODS + 1, output, sizeof(output), error, sizeof(error)) == 0);
    REQUIRE(modmgrBuildEnabledCsv(nullptr, 0, output, 0, error, sizeof(error)) == 0);
    REQUIRE(modmgrBuildEnabledCsv(nullptr, 0, nullptr, 1, error, sizeof(error)) == 0);
    REQUIRE(std::string(output) == "retained");
}


TEST_CASE("Mod Manager loads enabled order while preserving complete row identity", "[modmgr][persistence]")
{
    std::array<modinfo_t, 4> rows = {row("a", 1), row("temporary", 7, 1), row("b", 0), row("c", 1)};
    const auto before = rows;
    char error[256] = "old error";
    REQUIRE(load("[\"c\",\"unknown\",\"a\",\"temporary\"]", rows, error) == 1);
    REQUIRE(error[0] == '\0');
    REQUIRE(std::string(rows[0].id) == "c");
    REQUIRE(std::string(rows[2].id) == "a");
    REQUIRE(std::string(rows[3].id) == "b");
    REQUIRE(rows[0].enabled == 1);
    REQUIRE(rows[2].enabled == 1);
    REQUIRE(rows[3].enabled == 0);
    REQUIRE(std::memcmp(&rows[1], &before[1], sizeof(modinfo_t)) == 0);
    REQUIRE(std::string(rows[0].name) == before[3].name);
    REQUIRE(std::string(rows[0].dirpath) == before[3].dirpath);
    REQUIRE(rows[0].loaded == before[3].loaded);
    REQUIRE(rows[0].contenthash == before[3].contenthash);
}

TEST_CASE("Mod Manager rejects malformed saved lists without publishing any row", "[modmgr][persistence]")
{
    const std::vector<std::string> invalid = {
        "", "[", "[\"a\"", "[\"a\" \"b\"]", "[\"a\",]", "[null]", "[1]",
        "{\"a\":true}", "[\"a\"]junk", "[\"a\"] []", "\"a\"", "[\"a\",\"a\"]",
        "[\"a\",\"\\u0061\"]", "[\"\"]", "[\"a\\u0000b\"]", "[\"unterminated]",
        "[\"\\q\"]", "[\"" + std::string(MODMGR_ID_LEN, 'x') + "\"]"
    };
    for (const auto &json : invalid) {
        CAPTURE(json);
        std::array<modinfo_t, 3> rows = {row("a", 0), row("temporary", 1, 1), row("b", 1)};
        std::array<unsigned char, sizeof(rows)> before{};
        std::memcpy(before.data(), rows.data(), before.size());
        char error[256]{};
        REQUIRE(load(json, rows, error) == 0);
        REQUIRE(error[0] != '\0');
        REQUIRE(std::memcmp(rows.data(), before.data(), before.size()) == 0);
    }
}

TEST_CASE("Mod Manager empty selection disables persistent rows and retains session state", "[modmgr][persistence]")
{
    std::array<modinfo_t, 3> rows = {row("a", 1), row("temporary", 1, 1), row("b", 1)};
    char error[256]{};
    REQUIRE(load("[]", rows, error) == 1);
    REQUIRE(rows[0].enabled == 0);
    REQUIRE(rows[1].enabled == 1);
    REQUIRE(rows[2].enabled == 0);
    REQUIRE(std::string(rows[0].id) == "a");
    REQUIRE(std::string(rows[2].id) == "b");
}

TEST_CASE("Mod Manager strict decoder accepts escaped IDs at the canonical length boundary", "[modmgr][persistence]")
{
    const std::string maximum(MODMGR_ID_LEN - 1, 'x');
    std::array<modinfo_t, 2> rows = {row(maximum.c_str(), 0), row("a", 0)};
    char error[256]{};
    REQUIRE(load("[\"\\u0061\",\"" + maximum + "\"]", rows, error) == 1);
    REQUIRE(std::string(rows[0].id) == "a");
    REQUIRE(std::string(rows[1].id) == maximum);
    REQUIRE(rows[0].enabled == 1);
    REQUIRE(rows[1].enabled == 1);
}

TEST_CASE("Mod Manager saved order preserves session slots for every persistent permutation", "[modmgr][persistence]")
{
    std::array<std::string, 3> ids = {"a", "b", "c"};
    do {
        std::array<modinfo_t, 5> rows = {row("t0", 1, 1), row("a", 0), row("b", 0), row("t1", 0, 1), row("c", 0)};
        const std::string json = "[\"" + ids[0] + "\",\"" + ids[1] + "\",\"" + ids[2] + "\"]";
        char error[256]{};
        CAPTURE(json);
        REQUIRE(load(json, rows, error) == 1);
        REQUIRE(std::string(rows[0].id) == "t0");
        REQUIRE(std::string(rows[3].id) == "t1");
        REQUIRE(rows[0].enabled == 1);
        REQUIRE(rows[3].enabled == 0);
        REQUIRE(std::string(rows[1].id) == ids[0]);
        REQUIRE(std::string(rows[2].id) == ids[1]);
        REQUIRE(std::string(rows[4].id) == ids[2]);
    } while (std::next_permutation(ids.begin(), ids.end()));
}

TEST_CASE("Mod Manager rejects ambiguous or unterminated registry identities before mutation", "[modmgr][persistence]")
{
    std::array<modinfo_t, 2> rows = {row("a", 0), row("a", 1)};
    SECTION("duplicate") {}
    SECTION("unterminated") { std::memset(rows[1].id, 'x', sizeof(rows[1].id)); }
    SECTION("empty") { rows[1].id[0] = '\0'; }
    std::array<unsigned char, sizeof(rows)> before{};
    std::memcpy(before.data(), rows.data(), before.size());
    char error[256]{};
    REQUIRE(load("[\"a\"]", rows, error) == 0);
    REQUIRE(std::memcmp(rows.data(), before.data(), before.size()) == 0);
}

TEST_CASE("Mod Manager validates a complete saved list with no installed mods", "[modmgr][persistence]")
{
    const char json[] = "[\"not-installed\"]";
    REQUIRE(modmgrLoadEnabledDocument(json, sizeof(json) - 1, nullptr, 0, nullptr, 0) == 1);
    REQUIRE(modmgrLoadEnabledDocument("[", 1, nullptr, 0, nullptr, 0) == 0);
}

TEST_CASE("Mod Manager rejects invalid registry bounds without dereferencing rows", "[modmgr][persistence]")
{
    char error[1] = {'x'};
    REQUIRE(modmgrLoadEnabledDocument("[]", 2, nullptr, 1, error, sizeof(error)) == 0);
    REQUIRE(error[0] == '\0');
    REQUIRE(modmgrLoadEnabledDocument("[]", 2, nullptr, -1, nullptr, 0) == 0);
    REQUIRE(modmgrLoadEnabledDocument("[]", 2, nullptr, MODMGR_MAX_MODS + 1, nullptr, 0) == 0);
}

TEST_CASE("Mod Manager atomic saved selection roundtrips escaped IDs and ordered rows", "[modmgr][persistence]")
{
    SavedFile file;
    std::string special = "quote\"slash\\tab\t";
    special += '\x01';
    special += "\xc3\xa9";
    const std::string maximum(MODMGR_ID_LEN - 1, 'x');
    std::array<modinfo_t, 4> rows = {row(special.c_str(), 1), row("session", 1, 1), row(maximum.c_str(), 1), row("disabled", 0)};
    const auto before = rows;
    char error[256]{};
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), rows.data(), 4, error, sizeof(error)) == 1);
    REQUIRE(error[0] == '\0');
    REQUIRE(std::memcmp(before.data(), rows.data(), sizeof(rows)) == 0);
    const auto json = file.read();
    REQUIRE(json.find("session") == std::string::npos);
    REQUIRE(json.find("disabled") == std::string::npos);
    std::swap(rows[0], rows[2]);
    rows[0].enabled = rows[2].enabled = 0;
    rows[3].enabled = 1;
    REQUIRE(modmgrLoadEnabledFile(file.path.string().c_str(), rows.data(), 4, error, sizeof(error)) == 1);
    REQUIRE(std::string(rows[0].id) == special);
    REQUIRE(std::string(rows[2].id) == maximum);
    REQUIRE(rows[0].enabled == 1);
    REQUIRE(rows[2].enabled == 1);
    REQUIRE(rows[3].enabled == 0);
    REQUIRE(file.files() == 1);
}

TEST_CASE("Mod Manager failed atomic save preserves prior selection and permits retry", "[modmgr][persistence]")
{
    SavedFile file;
    std::ofstream(file.path, std::ios::binary) << "[\"prior\"]\n";
    std::array<modinfo_t, 1> rows = {row("replacement", 1)};
    char error[256]{};
    saveAtomicDebugFailNextCommit();
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), rows.data(), 1, error, sizeof(error)) == 0);
    REQUIRE(error[0] != '\0');
    REQUIRE(file.read() == "[\"prior\"]\n");
    REQUIRE(file.files() == 1);
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), rows.data(), 1, error, sizeof(error)) == 1);
    REQUIRE(file.read().find("replacement") != std::string::npos);
    REQUIRE(file.files() == 1);
}

TEST_CASE("Mod Manager invalid save inputs never replace an existing selection", "[modmgr][persistence]")
{
    SavedFile file;
    std::ofstream(file.path, std::ios::binary) << "prior bytes";
    std::array<modinfo_t, 2> rows = {row("a", 1), row("b", 1)};
    SECTION("duplicate ID") { rows[1] = row("a", 1); }
    SECTION("unterminated ID") { std::memset(rows[1].id, 'x', sizeof(rows[1].id)); }
    SECTION("invalid UTF8") { rows[1].id[0] = static_cast<char>(0xff); }
    char error[256]{};
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == 0);
    REQUIRE(error[0] != '\0');
    REQUIRE(file.read() == "prior bytes");
    REQUIRE(file.files() == 1);
}

TEST_CASE("Mod Manager empty save and refused destination return checked results", "[modmgr][persistence]")
{
    SavedFile file;
    char error[256]{};
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), nullptr, 0, error, sizeof(error)) == 1);
    REQUIRE(file.read() == "[]\n");
    const auto absent = file.dir / "missing" / "mods-enabled.json";
    REQUIRE(modmgrSaveEnabledFile(absent.string().c_str(), nullptr, 0, error, sizeof(error)) == 0);
    REQUIRE(error[0] != '\0');
    REQUIRE(file.files() == 1);
    REQUIRE(modmgrSaveEnabledFile(nullptr, nullptr, 0, nullptr, 0) == 0);
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), nullptr, 1, nullptr, 0) == 0);
}

TEST_CASE("Mod Manager file reader allows fallback only for an absent document", "[modmgr][persistence]")
{
    SavedFile file;
    std::array<modinfo_t, 2> rows = {row("a", 1), row("b", 0)};
    std::array<unsigned char, sizeof(rows)> before{};
    std::memcpy(before.data(), rows.data(), before.size());
    char error[256]{};
    REQUIRE(modmgrLoadEnabledFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == 0);
    REQUIRE(error[0] == '\0');
    for (const std::string json : {std::string(), std::string("[\"b\"")}) {
        std::ofstream(file.path, std::ios::binary | std::ios::trunc) << json;
        REQUIRE(modmgrLoadEnabledFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == -1);
        REQUIRE(error[0] != '\0');
        REQUIRE(std::memcmp(rows.data(), before.data(), before.size()) == 0);
        REQUIRE(file.read() == json);
    }
    REQUIRE(modmgrLoadEnabledFile(file.dir.string().c_str(), rows.data(), 2, error, sizeof(error)) == -1);
    REQUIRE(modmgrLoadEnabledFile(nullptr, rows.data(), 2, error, sizeof(error)) == -1);
    REQUIRE(std::memcmp(rows.data(), before.data(), before.size()) == 0);
}

TEST_CASE("Mod Manager roundtrips every maximum-length installed ID across read buffers", "[modmgr][persistence]")
{
    SavedFile file;
    std::array<modinfo_t, MODMGR_MAX_MODS> rows{};
    for (size_t i = 0; i < rows.size(); ++i) {
        std::string id(MODMGR_ID_LEN - 3, '\x01');
        id += static_cast<char>('A' + i / 26);
        id += static_cast<char>('A' + i % 26);
        rows[i] = row(id.c_str(), 1);
    }
    char error[256]{};
    REQUIRE(modmgrSaveEnabledFile(file.path.string().c_str(), rows.data(), static_cast<int>(rows.size()), error, sizeof(error)) == 1);
    REQUIRE(file.read().size() > 4096);
    std::vector<std::string> expected;
    for (const auto &entry : rows) expected.emplace_back(entry.id);
    std::reverse(rows.begin(), rows.end());
    for (auto &entry : rows) entry.enabled = 0;
    REQUIRE(modmgrLoadEnabledFile(file.path.string().c_str(), rows.data(), static_cast<int>(rows.size()), error, sizeof(error)) == 1);
    for (size_t i = 0; i < rows.size(); ++i) {
        REQUIRE(std::string(rows[i].id) == expected[i]);
        REQUIRE(rows[i].enabled == 1);
    }
}
