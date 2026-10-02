#include "catch.hpp"
#include "config.h"
#include "save_atomic.h"
#include "pdgui_input_profile_state.h"

#include <chrono>
#include <cstdio>
#include <cstring>
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

TEST_CASE("Real config retains all sixteen maximum device rules across delayed registration and reload",
          "[settings-save][config-save][input][profile-state]")
{
    // Registry pointers must survive all later tests and save operations.
    static char restoredRules[4][INPUT_DEVICE_PROFILES_STR_MAX] = {};
    static char restoredNames[4][INPUT_PROFILE_NAMES_STR_MAX] = {};
    static s32 following[4] = {};
    char profiles[INPUT_PROFILE_COUNT][INPUT_PROFILE_NAME_CAPACITY] = {};
    PdguiInputDeviceRule devices[INPUT_DEVICE_RULE_COUNT] = {};
    for (int i = 0; i < INPUT_PROFILE_COUNT; ++i)
        std::memset(profiles[i], 'A' + i, sizeof(profiles[i]) - 1);
    for (int i = 0; i < INPUT_DEVICE_RULE_COUNT; ++i) {
        devices[i].profile = i % INPUT_PROFILE_COUNT;
        std::memset(devices[i].alias, 'A' + i, sizeof(devices[i].alias) - 1);
        std::memset(devices[i].key, 'a' + i, sizeof(devices[i].key) - 1);
    }
    // Literal quotes/backslashes inside fields remain content in the existing
    // delimiter format; they must not split or unquote the complete rule list.
    devices[15].alias[12] = '"';
    devices[15].alias[13] = '\\';
    char names[INPUT_PROFILE_NAMES_STR_MAX], rules[INPUT_DEVICE_PROFILES_STR_MAX];
    REQUIRE(pdguiInputProfileStateSerialize(profiles, INPUT_PROFILE_COUNT,
        devices, INPUT_DEVICE_RULE_COUNT, names, sizeof(names), rules, sizeof(rules)));
    REQUIRE(std::strlen(rules) == 3103);
    for (int variant = 0; variant < 4; ++variant) {
        CAPTURE(variant);
        Directory dir;
        const auto source = dir.path / "source.ini";
        const auto saved = dir.path / "saved.ini";
        const std::string section = "InputMax" + std::to_string(variant);
        const std::string newline = variant & 1 ? "\r\n" : "\n";
        std::ofstream(source, std::ios::binary) << '[' << section << ']' << newline
            << "DeviceProfiles=" << rules << newline
            << "ProfileNames=" << names << newline << "Following=7"
            << (variant & 2 ? "" : newline);
        // Production startup reads pd.ini before inputInit registers strings.
        REQUIRE(configLoad(source.string().c_str()) == 1);
        // Saving while still pending must also preserve the complete payload.
        REQUIRE(configSave(saved.string().c_str()) == 1);
        REQUIRE(bytes(saved).find(std::string("DeviceProfiles=") + rules + '\n') != std::string::npos);
        configRegisterString((section + ".DeviceProfiles").c_str(), restoredRules[variant], sizeof(restoredRules[variant]));
        configRegisterString((section + ".ProfileNames").c_str(), restoredNames[variant], sizeof(restoredNames[variant]));
        configRegisterInt((section + ".Following").c_str(), &following[variant], 0, 10);
        REQUIRE(std::string(restoredRules[variant]) == rules);
        REQUIRE(std::string(restoredNames[variant]) == names);
        REQUIRE(following[variant] == 7);
        REQUIRE(configSave(saved.string().c_str()) == 1);
        restoredRules[variant][0] = '\0';
        restoredNames[variant][0] = '\0';
        following[variant] = 0;
        REQUIRE(configLoad(saved.string().c_str()) == 1);
        REQUIRE(std::string(restoredRules[variant]) == rules);
        REQUIRE(std::string(restoredNames[variant]) == names);
        REQUIRE(following[variant] == 7);
        const std::string accepted = bytes(saved);
        restoredRules[variant][0] = 'Z';
        saveAtomicDebugFailNextCommit();
        REQUIRE(configSave(saved.string().c_str()) == 0);
        REQUIRE(bytes(saved) == accepted);
        REQUIRE(restoredRules[variant][0] == 'Z');
        REQUIRE(candidates(dir.path) == 0);
        REQUIRE(configSave(saved.string().c_str()) == 1);
        restoredRules[variant][0] = '\0';
        REQUIRE(configLoad(saved.string().c_str()) == 1);
        REQUIRE(restoredRules[variant][0] == 'Z');
        REQUIRE(std::string(restoredRules[variant] + 1) == std::string(rules + 1));
    }
}

TEST_CASE("Real config replaces complete pending values before registration without disturbing following keys",
          "[settings-save][config-save][input][profile-state]")
{
    Directory dir;
    const auto source = dir.path / "pending.ini";
    static char replacement[9000] = {};
    static s32 following = 0;
    const std::string first(3000, 'a'), last(8500, 'b');
    std::ofstream(source, std::ios::binary) << "[PendingLong]\nValue=" << first
        << "\nValue=" << last << "\nFollowing=9";
    REQUIRE(configLoad(source.string().c_str()) == 1);
    configRegisterString("PendingLong.Value", replacement, sizeof(replacement));
    configRegisterInt("PendingLong.Following", &following, 0, 10);
    REQUIRE(std::string(replacement) == last);
    REQUIRE(following == 9);
}

TEST_CASE("Real config preserves quoted and boundary whitespace field content through save and replay",
          "[settings-save][config-save][input][profile-state]")
{
    Directory dir;
    const auto source = dir.path / "quoted.ini";
    const auto saved = dir.path / "saved.ini";
    static char registered[4][128] = {};
    const std::string values[] = {
        "\"Agent\"|Second|Third", " Agent|Second|Third ",
        "Agent\\Path|\"Second\"|Third", "Agent|Second|Third\""
    };
    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        const std::string section = "InputLiteral" + std::to_string(i);
        // Seed an explicitly quoted value just as the established parser
        // already supports, then require pending and registered save parity.
        std::ofstream(source, std::ios::binary) << '[' << section << "]\r\nNames=\""
            << values[i] << "\"\r\n";
        REQUIRE(configLoad(source.string().c_str()) == 1);
        REQUIRE(configSave(saved.string().c_str()) == 1);
        configRegisterString((section + ".Names").c_str(), registered[i], sizeof(registered[i]));
        REQUIRE(std::string(registered[i]) == values[i]);
        registered[i][0] = '\0';
        REQUIRE(configLoad(saved.string().c_str()) == 1);
        REQUIRE(std::string(registered[i]) == values[i]);
        REQUIRE(configSave(saved.string().c_str()) == 1);
        registered[i][0] = '\0';
        REQUIRE(configLoad(saved.string().c_str()) == 1);
        REQUIRE(std::string(registered[i]) == values[i]);
    }
}
