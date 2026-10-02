#include "catch.hpp"
#include "pdgui_input_profile_state.h"

#include <cstring>
#include <string>

namespace {
struct ProfileState {
    char profiles[INPUT_PROFILE_COUNT][INPUT_PROFILE_NAME_CAPACITY] = {};
    PdguiInputDeviceRule devices[INPUT_DEVICE_RULE_COUNT] = {};
    ProfileState() {
        for (int i = 0; i < INPUT_PROFILE_COUNT; ++i) {
            std::memset(profiles[i], 'A' + i, sizeof(profiles[i]) - 1);
        }
        for (int i = 0; i < INPUT_DEVICE_RULE_COUNT; ++i) {
            std::memset(devices[i].key, 'a' + i, sizeof(devices[i].key) - 1);
            std::memset(devices[i].alias, 'A' + i, sizeof(devices[i].alias) - 1);
            devices[i].profile = i % INPUT_PROFILE_COUNT;
        }
    }
};
}

TEST_CASE("Input metadata serializes every maximum-length supported profile and device",
          "[input][settings][profile-state]")
{
    ProfileState state;
    char names[INPUT_PROFILE_NAMES_STR_MAX] = {};
    char rules[INPUT_DEVICE_PROFILES_STR_MAX] = {};
    REQUIRE(pdguiInputProfileStateSerialize(state.profiles, INPUT_PROFILE_COUNT,
        state.devices, INPUT_DEVICE_RULE_COUNT, names, sizeof(names), rules, sizeof(rules)));
    REQUIRE(std::strlen(names) == sizeof(names) - 1);
    REQUIRE(std::strlen(rules) == sizeof(rules) - 1);
    REQUIRE(std::strlen(rules) == 3103);
    std::string expected;
    for (int i = 0; i < INPUT_DEVICE_RULE_COUNT; ++i) {
        if (i) expected += ';';
        expected += std::to_string(i % INPUT_PROFILE_COUNT) + '~' + state.devices[i].alias + '~' + state.devices[i].key;
    }
    REQUIRE(std::string(rules) == expected);
}

TEST_CASE("Input metadata capacity rejection leaves both accepted fields unchanged",
          "[input][settings][profile-state]")
{
    ProfileState state;
    char names[INPUT_PROFILE_NAMES_STR_MAX] = "accepted names";
    char rules[INPUT_DEVICE_PROFILES_STR_MAX] = "accepted rules";
    for (bool shortNames : {false, true}) {
        CAPTURE(shortNames);
        REQUIRE_FALSE(pdguiInputProfileStateSerialize(state.profiles, INPUT_PROFILE_COUNT,
            state.devices, INPUT_DEVICE_RULE_COUNT, names,
            sizeof(names) - (shortNames ? 1 : 0), rules,
            sizeof(rules) - (shortNames ? 0 : 1)));
        REQUIRE(std::string(names) == "accepted names");
        REQUIRE(std::string(rules) == "accepted rules");
    }
}

TEST_CASE("Input metadata rejects corrupt row bounds without publishing a prefix",
          "[input][settings][profile-state]")
{
    ProfileState state;
    char names[INPUT_PROFILE_NAMES_STR_MAX] = "accepted names";
    char rules[INPUT_DEVICE_PROFILES_STR_MAX] = "accepted rules";
    SECTION("invalid profile") { state.devices[15].profile = INPUT_PROFILE_COUNT; }
    SECTION("unterminated key") { std::memset(state.devices[15].key, 'x', sizeof(state.devices[15].key)); }
    SECTION("unterminated alias") { std::memset(state.devices[15].alias, 'x', sizeof(state.devices[15].alias)); }
    SECTION("unterminated profile name") { std::memset(state.profiles[5], 'x', sizeof(state.profiles[5])); }
    REQUIRE_FALSE(pdguiInputProfileStateSerialize(state.profiles, INPUT_PROFILE_COUNT,
        state.devices, INPUT_DEVICE_RULE_COUNT, names, sizeof(names), rules, sizeof(rules)));
    REQUIRE(std::string(names) == "accepted names");
    REQUIRE(std::string(rules) == "accepted rules");
}

TEST_CASE("Input metadata keeps reserved delimiters inside their edited fields",
          "[input][settings][profile-state]")
{
    ProfileState state;
    std::strcpy(state.profiles[0], "Profile|;~=\n");
    std::strcpy(state.devices[0].alias, "Pad|;~=\t");
    std::strcpy(state.devices[0].key, "guid|;~=\r");
    pdguiInputProfileSanitizeField(state.profiles[0]);
    pdguiInputProfileSanitizeField(state.devices[0].alias);
    pdguiInputProfileSanitizeField(state.devices[0].key);
    char names[INPUT_PROFILE_NAMES_STR_MAX] = {};
    char rules[INPUT_DEVICE_PROFILES_STR_MAX] = {};
    REQUIRE(pdguiInputProfileStateSerialize(state.profiles, INPUT_PROFILE_COUNT,
        state.devices, 1, names, sizeof(names), rules, sizeof(rules)));
    REQUIRE(std::string(names).find("Profile     |") == 0);
    REQUIRE(std::string(rules) == "0~Pad     ~guid     ");
    state.devices[0].key[0] = '\0';
    REQUIRE(pdguiInputProfileStateSerialize(state.profiles, INPUT_PROFILE_COUNT,
        state.devices, 1, names, sizeof(names), rules, sizeof(rules)));
    REQUIRE(std::string(rules).empty());
}
