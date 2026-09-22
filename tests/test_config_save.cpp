#include "catch.hpp"
#include "config.h"
#include "save_atomic.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {
namespace fs = std::filesystem;
struct Directory {
    fs::path path;
    Directory() {
        static unsigned serial = 0;
        path = fs::temp_directory_path() / ("pd2-config-save-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(++serial));
        fs::create_directory(path);
    }
    ~Directory() { std::error_code ignored; fs::remove_all(path, ignored); }
};
std::string bytes(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}
size_t candidates(const fs::path &dir) {
    size_t count = 0;
    for (const auto &entry : fs::directory_iterator(dir))
        if (entry.path().filename().string().find(".pd2tmp-") != std::string::npos) ++count;
    return count;
}
// The real registry retains every pointer for the process lifetime.
s32 signedValue = 0;
u32 unsignedValue = 0;
f32 floatValue = 0;
char stringValue[128] = "registered-value";
void registerValues() {
    configRegisterInt("SettingsSave.Signed", &signedValue, -10, 10);
    configRegisterUInt("SettingsSave.Unsigned", &unsignedValue, 1, 20);
    configRegisterFloat("SettingsSave.Float", &floatValue, 0.0f, 1.0f);
    configRegisterString("SettingsSave.String", stringValue, sizeof(stringValue));
}
}

TEST_CASE("Real config save preserves bytes and runtime on failed commit then retries completely",
          "[settings-save][config-save]")
{
    Directory dir;
    const auto dest = dir.path / "pd.ini";
    std::ofstream(dest, std::ios::binary) << "[NotRegisteredYet]\nFutureOption=keep this value\n";
    REQUIRE(configLoad(dest.string().c_str()) == 1);
    registerValues();
    signedValue = 500;
    unsignedValue = 500;
    floatValue = 5.0f;
    const std::string baseline = bytes(dest);

    saveAtomicDebugFailNextCommit();
    REQUIRE(configSave(dest.string().c_str()) == 0);
    REQUIRE(bytes(dest) == baseline);
    REQUIRE(candidates(dir.path) == 0);
    REQUIRE(signedValue == 500);
    REQUIRE(unsignedValue == 500);
    REQUIRE(floatValue == 5.0f);

    REQUIRE(configSave(dest.string().c_str()) == 1);
    const std::string committed = bytes(dest);
    REQUIRE(committed.find("[NotRegisteredYet]\nFutureOption=keep this value\n") != std::string::npos);
    REQUIRE(committed.find("Signed=10\n") != std::string::npos);
    REQUIRE(committed.find("Unsigned=20\n") != std::string::npos);
    REQUIRE(committed.find("Float=1.000000\n") != std::string::npos);
    REQUIRE(committed.find("String=registered-value\n") != std::string::npos);
    REQUIRE(signedValue == 10);
    REQUIRE(unsignedValue == 20);
    REQUIRE(floatValue == 1.0f);
    REQUIRE(candidates(dir.path) == 0);
    signedValue = 0;
    unsignedValue = 1;
    floatValue = 0.0f;
    REQUIRE(configLoad(dest.string().c_str()) == 1);
    REQUIRE(signedValue == 10);
    REQUIRE(unsignedValue == 20);
    REQUIRE(floatValue == 1.0f);
}

TEST_CASE("Real config candidate open failure preserves unrelated bytes and unclamped runtime",
          "[settings-save][config-save]")
{
    Directory dir;
    registerValues();
    signedValue = -500;
    const auto existing = dir.path / "pd.ini";
    std::ofstream(existing, std::ios::binary) << "existing config";
    const auto missingParent = dir.path / "absent" / "pd.ini";
    REQUIRE(configSave(missingParent.string().c_str()) == 0);
    REQUIRE(signedValue == -500);
    REQUIRE(bytes(existing) == "existing config");
    REQUIRE_FALSE(fs::exists(missingParent));
    REQUIRE(candidates(dir.path) == 0);
    REQUIRE(configSave(nullptr) == 0);
    REQUIRE(configSave("") == 0);
}

TEST_CASE("Real config serializer detects a stream write failure without publishing normalization",
          "[settings-save][config-save]")
{
    Directory dir;
    registerValues();
    signedValue = 500;
    const auto dest = dir.path / "readonly.ini";
    std::ofstream(dest, std::ios::binary) << "immutable baseline";
    FILE *stream = std::fopen(dest.string().c_str(), "rb");
    REQUIRE(stream != nullptr);
    const s32 result = configWriteSnapshot(stream);
    const bool hadError = std::ferror(stream) != 0;
    std::fclose(stream);
    REQUIRE(result == 0);
    REQUIRE(hadError);
    REQUIRE(signedValue == 500);
    REQUIRE(bytes(dest) == "immutable baseline");
    REQUIRE(configWriteSnapshot(nullptr) == 0);
}

TEST_CASE("Real config replacement denial keeps a directory destination and removes its candidate",
          "[settings-save][config-save]")
{
    Directory dir;
    registerValues();
    signedValue = 500;
    const auto destination = dir.path / "pd.ini";
    fs::create_directory(destination);
    std::ofstream(destination / "sentinel", std::ios::binary) << "preserved";
    REQUIRE(configSave(destination.string().c_str()) == 0);
    REQUIRE(fs::is_directory(destination));
    REQUIRE(bytes(destination / "sentinel") == "preserved");
    REQUIRE(signedValue == 500);
    REQUIRE(candidates(dir.path) == 0);
}
