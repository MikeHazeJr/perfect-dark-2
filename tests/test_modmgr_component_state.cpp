#include "catch.hpp"
#include "modmgr_component_state.h"
#include "save_atomic.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {
using State = std::unique_ptr<modmgr_component_state_t, decltype(&modmgrFreeComponentState)>;
struct File {
    std::filesystem::path dir;
    std::filesystem::path path;
    File() {
        static std::atomic<unsigned> serial{0};
        dir = std::filesystem::temp_directory_path() / ("pd2-component-state-" +
            std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(serial++));
        std::filesystem::create_directory(dir);
        path = dir / ".modstate";
    }
    ~File() { std::error_code ignored; std::filesystem::remove_all(dir, ignored); }
    void write(const std::string &value) {
        std::ofstream output(path, std::ios::binary);
        output.write(value.data(), value.size());
        REQUIRE(output.good());
    }
    std::string read() {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    std::vector<std::string> ids() {
        modmgr_component_state_t *raw = nullptr;
        char error[256]{};
        REQUIRE(modmgrReadComponentState(path.string().c_str(), &raw, error, sizeof(error)) == 1);
        State state(raw, modmgrFreeComponentState);
        std::vector<std::string> result;
        for (size_t i = 0; i < modmgrComponentStateCount(raw); ++i)
            result.emplace_back(modmgrComponentStateId(raw, i));
        REQUIRE(modmgrComponentStateId(raw, result.size()) == nullptr);
        return result;
    }
};
modmgr_component_choice_t choice(const char *id, int enabled, int persistent = 1)
{
    modmgr_component_choice_t row{};
    std::snprintf(row.id, sizeof(row.id), "%s", id);
    row.enabled = enabled;
    row.persistent = persistent;
    return row;
}
}

TEST_CASE("Component state reads complete commented documents with CRLF BOM and duplicate IDs", "[modmgr][component-state]")
{
    File file;
    const std::string longest(CATALOG_ID_LEN - 1, 'x');
    file.write("\xEF\xBB\xBF# comment\r\n\r\nbase:one\r\nbase:one\n" + longest);
    REQUIRE(file.ids() == std::vector<std::string>{"base:one", longest});
}

TEST_CASE("Component state rejects a malformed later line before exposing any ID", "[modmgr][component-state]")
{
    File file;
    const std::vector<std::string> invalid = {
        "first\n" + std::string(CATALOG_ID_LEN, 'x'),
        std::string("first\nsecond\0tail", 17),
        "first\nsecond\rinside",
        std::string("# comment\0bad\nfirst", 19)
    };
    for (const auto &document : invalid) {
        file.write(document);
        modmgr_component_state_t *state = nullptr;
        char error[256]{};
        REQUIRE(modmgrReadComponentState(file.path.string().c_str(), &state, error, sizeof(error)) == -1);
        REQUIRE(state == nullptr);
        REQUIRE(error[0] != '\0');
        REQUIRE(file.read() == document);
    }
}

TEST_CASE("Component state distinguishes absent files from invalid sources", "[modmgr][component-state]")
{
    File file;
    modmgr_component_state_t *state = nullptr;
    char error[256] = "stale";
    REQUIRE(modmgrReadComponentState(file.path.string().c_str(), &state, error, sizeof(error)) == 0);
    REQUIRE(state == nullptr);
    REQUIRE(error[0] == '\0');
    REQUIRE(modmgrReadComponentState(file.dir.string().c_str(), &state, error, sizeof(error)) == -1);
    REQUIRE(modmgrReadComponentState(nullptr, &state, error, sizeof(error)) == -1);
    REQUIRE(modmgrReadComponentState(file.path.string().c_str(), nullptr, error, sizeof(error)) == -1);
    REQUIRE(modmgrComponentStateCount(nullptr) == 0);
    REQUIRE(modmgrComponentStateId(nullptr, 0) == nullptr);
    modmgrFreeComponentState(nullptr);
}

TEST_CASE("Component state merges disabled enabled absent and excluded choices", "[modmgr][component-state]")
{
    File file;
    file.write("absent\nnow-enabled\nsession-existing\n");
    std::array<modmgr_component_choice_t, 4> rows = {
        choice("disabled", 0), choice("now-enabled", 1),
        choice("session-existing", 1, 0), choice("session-new", 0, 0)
    };
    const auto before = rows;
    char error[256]{};
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), rows.size(),
        error, sizeof(error)) == 1);
    REQUIRE(file.ids() == std::vector<std::string>{"absent", "disabled", "session-existing"});
    REQUIRE(std::memcmp(rows.data(), before.data(), sizeof(rows)) == 0);
}

TEST_CASE("Component state refuses to replace invalid previous preferences", "[modmgr][component-state]")
{
    File file;
    const std::string previous = "saved\n" + std::string(CATALOG_ID_LEN, 'x');
    file.write(previous);
    auto row = choice("new", 0);
    char error[256]{};
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), &row, 1, error, sizeof(error)) == 0);
    REQUIRE(file.read() == previous);
    REQUIRE(error[0] != '\0');
}

TEST_CASE("Component state atomic failure preserves bytes and retry commits the merged set", "[modmgr][component-state]")
{
    File file;
    const std::string previous = "# original\nabsent\n";
    file.write(previous);
    auto row = choice("new", 0);
    char error[256]{};
    saveAtomicDebugFailNextCommit();
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), &row, 1, error, sizeof(error)) == 0);
    REQUIRE(file.read() == previous);
    REQUIRE(std::distance(std::filesystem::directory_iterator(file.dir),
        std::filesystem::directory_iterator()) == 1);
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), &row, 1, error, sizeof(error)) == 1);
    REQUIRE(file.ids() == std::vector<std::string>{"absent", "new"});
}

TEST_CASE("Component state stores more choices than fixed package registry limits", "[modmgr][component-state]")
{
    File file;
    std::vector<modmgr_component_choice_t> rows;
    for (int i = 0; i < 5000; ++i) rows.push_back(choice(("component:" + std::to_string(i)).c_str(), 0));
    char error[256]{};
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), rows.size(),
        error, sizeof(error)) == 1);
    REQUIRE(file.ids().size() == rows.size());
    for (auto &row : rows) row.enabled = 1;
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), rows.size(),
        error, sizeof(error)) == 1);
    REQUIRE(file.ids().empty());
}

TEST_CASE("Component state rejects invalid choices without touching saved bytes", "[modmgr][component-state]")
{
    File file;
    file.write("retained\n");
    std::array<modmgr_component_choice_t, 2> rows = {choice("same", 0), choice("same", 1)};
    char error[256]{};
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == 0);
    rows[1] = choice("#comment-id", 0);
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == 0);
    rows[1] = choice("embedded\nnewline", 0);
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == 0);
    std::memset(rows[1].id, 'x', sizeof(rows[1].id));
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), rows.data(), 2, error, sizeof(error)) == 0);
    REQUIRE(modmgrSaveComponentStateFile(file.path.string().c_str(), nullptr, 1, error, sizeof(error)) == 0);
    REQUIRE(file.read() == "retained\n");
}
