#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include "input_profile_metadata.h"

struct PdguiInputDeviceRule {
    char key[INPUT_DEVICE_KEY_CAPACITY];
    char alias[INPUT_DEVICE_ALIAS_CAPACITY];
    int profile;
};

inline void pdguiInputProfileSanitizeField(char *field)
{
    if (!field) return;
    for (char *p = field; *p; ++p) {
        const unsigned char ch = static_cast<unsigned char>(*p);
        if (ch < 0x20 || ch >= 0x7f || ch == '|' || ch == ';' || ch == '~' || ch == '=')
            *p = ' ';
    }
}

// Publish both complete fields together. A rejected edit leaves the caller's
// accepted configuration intact, including the final device's complete key.
inline bool pdguiInputProfileStateSerialize(const char (*profileNames)[INPUT_PROFILE_NAME_CAPACITY],
    size_t profileCount, const PdguiInputDeviceRule *deviceRules, size_t ruleCount,
    char *names, size_t namesCapacity, char *rules, size_t rulesCapacity)
{
    if (!profileNames || !names || !rules || !namesCapacity || !rulesCapacity ||
        profileCount > INPUT_PROFILE_COUNT || ruleCount > INPUT_DEVICE_RULE_COUNT ||
        (ruleCount && !deviceRules)) return false;

    std::string completeNames, completeRules;
    for (size_t i = 0; i < profileCount; ++i) {
        if (!std::memchr(profileNames[i], '\0', INPUT_PROFILE_NAME_CAPACITY)) return false;
        if (i) completeNames += '|';
        completeNames += profileNames[i];
    }
    for (size_t i = 0; i < ruleCount; ++i) {
        const PdguiInputDeviceRule &rule = deviceRules[i];
        if (!rule.key[0]) continue;
        if (!std::memchr(rule.key, '\0', sizeof(rule.key)) ||
            !std::memchr(rule.alias, '\0', sizeof(rule.alias))) return false;
        if (rule.profile < 0 || static_cast<size_t>(rule.profile) >= profileCount)
            return false;
        char profile[32];
        std::snprintf(profile, sizeof(profile), "%d", rule.profile);
        if (!completeRules.empty()) completeRules += ';';
        completeRules += profile;
        completeRules += '~';
        completeRules += rule.alias;
        completeRules += '~';
        completeRules += rule.key;
    }
    if (completeNames.size() >= namesCapacity || completeRules.size() >= rulesCapacity)
        return false;
    std::memcpy(names, completeNames.c_str(), completeNames.size() + 1);
    std::memcpy(rules, completeRules.c_str(), completeRules.size() + 1);
    return true;
}
