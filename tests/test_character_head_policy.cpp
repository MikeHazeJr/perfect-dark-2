#include "catch.hpp"
#include <cstring>
#include <string>

extern "C" {
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "character_head_policy.h"
}

namespace {
void pair(ini_section_t &ini, const char *key, const char *value)
{
    REQUIRE(ini.count < INI_MAX_PAIRS);
    REQUIRE(std::strlen(key) < sizeof(ini.pairs[0].key));
    REQUIRE(std::strlen(value) < sizeof(ini.pairs[0].value));
    std::strcpy(ini.pairs[ini.count].key, key);
    std::strcpy(ini.pairs[ini.count++].value, value);
}

ini_section_t descriptor(const char *policy, bool head)
{
    ini_section_t ini = {};
    std::strcpy(ini.type, "character");
    pair(ini, "catalog_id", "mod:character");
    pair(ini, "body_asset", "mod:body");
    pair(ini, "bodyfile", "mods/test/character.pdcharacter::body.pdbody");
    if (policy) pair(ini, "head_policy", policy);
    if (head) {
        pair(ini, "head_asset", "mod:head");
        pair(ini, "headfile", "mods/test/character.pdcharacter::head.pdhead");
    }
    return ini;
}

asset_entry_t entry()
{
    asset_entry_t out = {};
    out.type = ASSET_CHARACTER;
    std::strcpy(out.id, "mod:character");
    return out;
}
}

TEST_CASE("character policy admits explicit fixed random and integrated source",
          "[modding][character][head-policy][source]")
{
    for (const auto policy : {CHARACTER_HEAD_POLICY_FIXED,
            CHARACTER_HEAD_POLICY_RANDOM_GENDER, CHARACTER_HEAD_POLICY_INTEGRATED}) {
        CAPTURE(policy);
        const char *name = characterHeadPolicyName(policy);
        auto ini = descriptor(name, policy == CHARACTER_HEAD_POLICY_FIXED);
        pair(ini, "display_name", "Edited character");
        pair(ini, "portrait_file", "mods/test/character.pdcharacter::portrait.png");
        auto out = entry();
        REQUIRE(characterSourceApplyIni(&out, &ini) == 1);
        REQUIRE(out.ext.character.head_policy == policy);
        REQUIRE(characterSourcePolicy(&out) == policy);
        REQUIRE(std::string(out.ext.character.body_id) == "mod:body");
        REQUIRE(std::string(out.ext.character.bodyfile) ==
            "mods/test/character.pdcharacter::body.pdbody");
        REQUIRE(std::string(out.ext.character.display_name) == "Edited character");
        REQUIRE(std::string(out.ext.character.portrait_file) ==
            "mods/test/character.pdcharacter::portrait.png");
        REQUIRE(std::string(out.ext.character.head_id) ==
            (policy == CHARACTER_HEAD_POLICY_FIXED ? "mod:head" : ""));
        REQUIRE(std::string(out.ext.character.headfile) ==
            (policy == CHARACTER_HEAD_POLICY_FIXED
                ? "mods/test/character.pdcharacter::head.pdhead" : ""));
    }
}

TEST_CASE("character policy preserves only complete legacy fixed declarations",
          "[modding][character][head-policy][source]")
{
    auto out = entry();
    auto legacy = descriptor(nullptr, true);
    REQUIRE(characterSourceApplyIni(&out, &legacy) == 1);
    REQUIRE(out.ext.character.head_policy == CHARACTER_HEAD_POLICY_FIXED);
    auto nullTemplate = descriptor(nullptr, false);
    REQUIRE(characterSourceApplyIni(&out, &nullTemplate) == 0);
    for (const char *bad : {"", "random", "Fixed", "unknown"}) {
        auto ini = descriptor(bad, true);
        CAPTURE(bad);
        REQUIRE(characterSourceApplyIni(&out, &ini) == 0);
    }
}

TEST_CASE("character policy refuses contradictory or incomplete head dependencies",
          "[modding][character][head-policy][source]")
{
    REQUIRE(characterHeadPolicyResolve("fixed", "m:body", "", "body.pdbody", "") ==
        CHARACTER_HEAD_POLICY_INVALID);
    REQUIRE(characterHeadPolicyResolve("fixed", "m:body", "m:head", "body.pdbody", "") ==
        CHARACTER_HEAD_POLICY_INVALID);
    REQUIRE(characterHeadPolicyResolve("fixed", "m:body", "", "body.pdbody", "head.pdhead") ==
        CHARACTER_HEAD_POLICY_INVALID);
    for (const char *name : {"random_gender", "integrated"}) {
        CAPTURE(name);
        REQUIRE(characterHeadPolicyResolve(name, "m:body", "m:head", "body.pdbody", "") ==
            CHARACTER_HEAD_POLICY_INVALID);
        REQUIRE(characterHeadPolicyResolve(name, "m:body", "", "body.pdbody", "head.pdhead") ==
            CHARACTER_HEAD_POLICY_INVALID);
        REQUIRE(characterHeadPolicyResolve(name, "", "", "body.pdbody", "") ==
            CHARACTER_HEAD_POLICY_INVALID);
        REQUIRE(characterHeadPolicyResolve(name, "m:body", "", "", "") ==
            CHARACTER_HEAD_POLICY_INVALID);
    }
}

TEST_CASE("character descriptor admission is transactional and rejects alias ambiguity",
          "[modding][character][head-policy][source]")
{
    auto ini = descriptor("fixed", true);
    auto out = entry();
    REQUIRE(characterSourceApplyIni(&out, &ini) == 1);
    const auto before = out;
    SECTION("conflicting ID alias") { pair(ini, "body_id", "mod:other"); }
    SECTION("conflicting file alias") { pair(ini, "body_archive", "other.pdbody"); }
    SECTION("duplicate policy") { pair(ini, "head_policy", "fixed"); }
    SECTION("duplicate body") { pair(ini, "body_asset", "mod:body"); }
    SECTION("descriptor identity mismatch") { std::strcpy(ini.pairs[0].value, "mod:wrong"); }
    SECTION("oversized ID") {
        std::string longId(CATALOG_ID_LEN, 'x');
        std::strcpy(ini.pairs[1].value, longId.c_str());
    }
    SECTION("oversized display name") {
        std::string longName(sizeof(out.ext.character.display_name), 'x');
        pair(ini, "display_name", longName.c_str());
    }
    SECTION("unterminated pair") { std::memset(ini.pairs[0].value, 'x', sizeof(ini.pairs[0].value)); }
    REQUIRE(characterSourceApplyIni(&out, &ini) == 0);
    REQUIRE(std::memcmp(&before, &out, sizeof(out)) == 0);
}

TEST_CASE("character descriptor accepts matching aliases without losing fields",
          "[modding][character][head-policy][source]")
{
    auto ini = descriptor("fixed", true);
    pair(ini, "body_id", "mod:body");
    pair(ini, "head_id", "mod:head");
    pair(ini, "body_archive", "mods/test/character.pdcharacter::body.pdbody");
    pair(ini, "head_archive", "mods/test/character.pdcharacter::head.pdhead");
    auto out = entry();
    REQUIRE(characterSourceApplyIni(&out, &ini) == 1);
    REQUIRE(characterSourcePolicy(&out) == CHARACTER_HEAD_POLICY_FIXED);
    out.ext.character.head_policy = 1234;
    REQUIRE(characterSourcePolicy(&out) == CHARACTER_HEAD_POLICY_INVALID);
    out.ext.character.head_policy = CHARACTER_HEAD_POLICY_UNSPECIFIED;
    REQUIRE(characterSourcePolicy(&out) == CHARACTER_HEAD_POLICY_FIXED);
    std::memset(out.ext.character.body_id, 'x', sizeof(out.ext.character.body_id));
    REQUIRE(characterSourcePolicy(&out) == CHARACTER_HEAD_POLICY_INVALID);
}

TEST_CASE("character admission enforces catalog grammar and UTF8 byte capacity",
          "[modding][character][head-policy][source]")
{
    auto out = entry();
    for (const char *bad : {"17", "body", "1mod:body", "mod:", "mod:bad/path", "mod:bad head"}) {
        auto ini = descriptor("integrated", false);
        std::strcpy(ini.pairs[1].value, bad);
        CAPTURE(bad);
        REQUIRE(characterSourceApplyIni(&out, &ini) == 0);
    }
    auto ini = descriptor("integrated", false);
    std::string validName;
    for (int i = 0; i < 31; ++i) validName += "\xc3\xa9";
    pair(ini, "display_name", validName.c_str());
    REQUIRE(characterSourceApplyIni(&out, &ini) == 1);
    REQUIRE(std::strlen(out.ext.character.display_name) == 62);
    std::strcat(ini.pairs[ini.count - 1].value, "\xc3\xa9");
    REQUIRE(characterSourceApplyIni(&out, &ini) == 0);
    std::strcpy(ini.pairs[ini.count - 1].value, "\xc0\xaf");
    REQUIRE(characterSourceApplyIni(&out, &ini) == 0);
    std::strcpy(ini.pairs[ini.count - 1].value, "\xed\xa0\x80");
    REQUIRE(characterSourceApplyIni(&out, &ini) == 0);
}

TEST_CASE("character closure checks public child identity type and complete-body declaration",
          "[modding][character][head-policy][source][dependencies]")
{
    ini_section_t body = {};
    std::strcpy(body.type, "body");
    pair(body, "catalog_id", "mod:body");
    pair(body, "unk00_01", "1");
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 0, 1) == 1);
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 1, 0) == 0);
    REQUIRE(characterDependencySourceMatches(&body, "mod:other", 0, 1) == 0);
    std::strcpy(body.pairs[1].value, "0");
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 0, 1) == 0);
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 0, 0) == 1);
    std::strcpy(body.pairs[1].value, "1junk");
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 0, 1) == 0);
    std::strcpy(body.pairs[1].value, "1");
    pair(body, "unk00_01", "0");
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 0, 1) == 0);
    --body.count;
    pair(body, "id", "mod:other");
    REQUIRE(characterDependencySourceMatches(&body, "mod:body", 0, 1) == 0);
}
