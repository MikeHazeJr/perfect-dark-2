#include "catch.hpp"
#include "lang_source.h"

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
std::string source(const std::string &rows)
{
    return "{\"pd_kind\":\"language_strings\",\"pd_schema_version\":1,\"strings\":" + rows + "}";
}

struct Bank {
    lang_source_bank_t value{};
    ~Bank() { std::free(value.data); }
    const char *text(size_t index) const
    {
        return value.data[index] ? reinterpret_cast<const char *>(value.data) + value.data[index] : nullptr;
    }
};

void offset(std::vector<u8> &data, size_t slot, u32 value)
{
    for (int i = 0; i < 4; i++) data[slot * 4 + i] = static_cast<u8>(value >> (24 - 8 * i));
}
}

TEST_CASE("language exporter and runtime retain null empty text and trailing slots",
    "[modding][pdxxx][pdlang][lang-source]")
{
    std::vector<u8> raw(48, 0);
    offset(raw, 1, 24); // A real empty string, unlike offset-zero slots.
    offset(raw, 2, 28);
    offset(raw, 4, 40);
    const char latin[] = "caf\xe9\n";
    std::memcpy(raw.data() + 28, latin, sizeof(latin));
    std::memcpy(raw.data() + 40, "end", 4);
    char *json = nullptr;
    u32 size = 0, count = 0;
    const char *error = nullptr;
    REQUIRE(langSourceExportNative(raw.data(), static_cast<u32>(raw.size()), -1,
        LANG_SOURCE_LATIN1, &json, &size, &count, &error) == 1);
    const std::string exported(json, size);
    std::free(json);
    REQUIRE(count == 6);
    REQUIRE(exported.find("\"text\": null") != std::string::npos);
    Bank bank;
    REQUIRE(langSourceParseJson(exported.data(), exported.size(), count,
        LANG_SOURCE_LATIN1, &bank.value, &error) == 1);
    REQUIRE(bank.value.string_count == 6);
    REQUIRE(bank.text(0) == nullptr);
    REQUIRE(bank.text(1) != nullptr);
    REQUIRE(std::string(bank.text(1)).empty());
    REQUIRE(std::string(bank.text(2)) == latin);
    REQUIRE(bank.text(3) == nullptr);
    REQUIRE(std::string(bank.text(4)) == "end");
    REQUIRE(bank.text(5) == nullptr);
    REQUIRE(bank.text(511) == nullptr);
}

TEST_CASE("language empty native banks and explicit all-null tables retain their extent",
    "[modding][pdxxx][pdlang][lang-source]")
{
    std::vector<u8> raw(16, 0);
    for (s32 declared : {-1, 0, 4}) {
        char *json = nullptr;
        u32 size = 0, count = 99;
        const char *error = nullptr;
        REQUIRE(langSourceExportNative(raw.data(), static_cast<u32>(raw.size()), declared,
            LANG_SOURCE_LATIN1, &json, &size, &count, &error) == 1);
        const std::string exported(json, size);
        std::free(json);
        REQUIRE(count == (declared == 4 ? 4u : 0u));
        Bank bank;
        REQUIRE(langSourceParseJson(exported.data(), exported.size(), count,
            LANG_SOURCE_LATIN1, &bank.value, &error) == 1);
        REQUIRE(bank.value.string_count == count);
        REQUIRE(bank.text(0) == nullptr);
        REQUIRE(bank.text(511) == nullptr);
    }
}

TEST_CASE("language literal UTF8 and escaped Unicode share the native codec",
    "[modding][pdxxx][pdlang][lang-source]")
{
    const auto literal = source("[{\"index\":0,\"text\":\"caf\xc3\xa9 \xc3\xbf\"}]");
    const auto escaped = source("[{\"index\":0,\"text\":\"caf\\u00e9 \\u00ff\"}]");
    Bank a, b;
    const char *error = nullptr;
    REQUIRE(langSourceParseJson(literal.data(), literal.size(), -1, LANG_SOURCE_LATIN1, &a.value, &error) == 1);
    REQUIRE(langSourceParseJson(escaped.data(), escaped.size(), 1, LANG_SOURCE_LATIN1, &b.value, &error) == 1);
    REQUIRE(a.value.string_count == 1);
    REQUIRE(a.value.data_size == b.value.data_size);
    REQUIRE(std::memcmp(a.value.data, b.value.data, a.value.data_size) == 0);
    REQUIRE(std::string(a.text(0)) == "caf\xe9 \xff");

    const auto escapes = source(R"([{"index":0,"text":"\"\\\/\b\f\n\r\t\u001b"}])");
    Bank c;
    REQUIRE(langSourceParseJson(escapes.data(), escapes.size(), 1, LANG_SOURCE_LATIN1, &c.value, &error) == 1);
    REQUIRE(std::string(c.text(0)) == "\"\\/\b\f\n\r\t\x1b");
}

TEST_CASE("language complete indexed schema1 rows can arrive out of order",
    "[modding][pdxxx][pdlang][lang-source]")
{
    const auto json = source(R"([{"index":2,"text":null},{"index":0,"text":"zero"},{"index":1,"text":""}])");
    Bank bank;
    const char *error = nullptr;
    REQUIRE(langSourceParseJson(json.data(), json.size(), -1, LANG_SOURCE_LATIN1, &bank.value, &error) == 1);
    REQUIRE(bank.value.string_count == 3);
    REQUIRE(std::string(bank.text(0)) == "zero");
    REQUIRE(bank.text(1) != nullptr);
    REQUIRE(bank.text(2) == nullptr);

    const std::string legacy = "\xef\xbb\xbf" R"({"pd_kind":"language_strings","strings":[{"index":0.0,"text":"legacy"}]})";
    Bank legacy_bank;
    REQUIRE(langSourceParseJson(legacy.data(), legacy.size(), 1,
        LANG_SOURCE_LATIN1, &legacy_bank.value, &error) == 1);
    REQUIRE(std::string(legacy_bank.text(0)) == "legacy");

    std::string rows = "[";
    for (u32 i = 0; i < LANG_SOURCE_MAX_STRINGS; i++) {
        if (i) rows += ',';
        rows += "{\"index\":" + std::to_string(i) + ",\"text\":";
        rows += i == 511 ? "\"last\"}" : "null}";
    }
    const auto full = source(rows + ']');
    Bank full_bank;
    REQUIRE(langSourceParseJson(full.data(), full.size(), LANG_SOURCE_MAX_STRINGS,
        LANG_SOURCE_LATIN1, &full_bank.value, &error) == 1);
    REQUIRE(full_bank.value.string_count == LANG_SOURCE_MAX_STRINGS);
    REQUIRE(full_bank.text(0) == nullptr);
    REQUIRE(std::string(full_bank.text(511)) == "last");
}

TEST_CASE("language malformed rows indexes and JSON never publish a partial bank",
    "[modding][pdxxx][pdlang][lang-source][negative]")
{
    const std::vector<std::string> bad = {
        source(R"([{"index":0,"text":"a"},{"index":0,"text":"b"}])"),
        source(R"([{"index":0,"text":"a"},{"index":2,"text":"b"}])"),
        source(R"([{"index":-1,"text":"a"}])"),
        source(R"([{"index":512,"text":"a"}])"),
        source(R"([{"index":0.5,"text":"a"}])"),
        source(R"([{"index":true,"text":"a"}])"),
        source(R"([{"index":"0","text":"a"}])"),
        source(R"([{"index":0,"text":7}])"),
        source(R"([{"index":0}])"),
        source(R"([null])"), source(R"(["skip me"] )"),
        source(R"([{"index":0,"text":"a","te\u0078t":"b"}])"),
        source(R"([{"index":0,"text":"\q"}])"),
        source(R"([{"index":0,"text":"\u0000"}])"),
        source(R"([{"index":0,"text":"\ud800"}])"),
        source(R"([{"index":0,"text":"\udc00"}])"),
        source(R"([{"index":0,"text":"\ud800\u0041"}])"),
        source("[{\"index\":0,\"text\":\"\xc0\xaf\"}]"),
        source("[{\"index\":0,\"text\":\"bare\nnewline\"}]"),
        source(R"([{"index":0,"text":"a"},])"),
        source(R"([{"index":NaN,"text":"a"}])"),
        source(R"([{"index":1e999,"text":"a"}])"),
        source(R"([{"index":0,"text":"a"}])") + " trailing",
        R"({"pd_kind":"language_strings","strings":[],"str\u0069ngs":[]})"
    };
    for (const auto &json : bad) {
        INFO(json);
        lang_source_bank_t candidate{};
        const char *error = nullptr;
        REQUIRE(langSourceParseJson(json.data(), json.size(), -1,
            LANG_SOURCE_LATIN1, &candidate, &error) == 0);
        REQUIRE(candidate.data == nullptr);
        REQUIRE(candidate.string_count == 0);
        REQUIRE(candidate.data_size == 0);
        REQUIRE(error != nullptr);
    }
}

TEST_CASE("language valid unsupported Unicode is rejected after scalar decoding",
    "[modding][pdxxx][pdlang][lang-source][negative]")
{
    for (const auto &text : {std::string("\xf0\x9f\x98\x80"), std::string("\\ud83d\\ude00"), std::string("\\u3042")}) {
        const auto json = source("[{\"index\":0,\"text\":\"" + text + "\"}]");
        Bank bank;
        const char *error = nullptr;
        REQUIRE(langSourceParseJson(json.data(), json.size(), 1, LANG_SOURCE_LATIN1, &bank.value, &error) == 0);
        REQUIRE(std::string(error) == "language_codepoint_unsupported_by_latin1");
    }
    REQUIRE(langSourceEncodingForLocale("ntsc-final", "jp") == LANG_SOURCE_LATIN1);
    REQUIRE(langSourceEncodingForLocale("jpn-final", "en") == LANG_SOURCE_LATIN1);
    REQUIRE(langSourceEncodingForLocale("jpn-final", "ja-JP") == LANG_SOURCE_JAPANESE_UNSUPPORTED);
    const auto json = source(R"([{"index":0,"text":"English"}])");
    Bank bank;
    const char *error = nullptr;
    REQUIRE(langSourceParseJson(json.data(), json.size(), 1, LANG_SOURCE_JAPANESE_UNSUPPORTED, &bank.value, &error) == 0);
    REQUIRE(std::string(error) == "language_native_encoding_unsupported");
}

TEST_CASE("language declared counts include zero and never accept numeric prefixes",
    "[modding][pdxxx][pdlang][lang-source]")
{
    u32 count = 99;
    REQUIRE(langSourceParseCount("0", &count) == 1);
    REQUIRE(count == 0);
    REQUIRE(langSourceParseCount(" 512\t", &count) == 1);
    REQUIRE(count == 512);
    for (const char *text : {"", "-1", "+1", "1.0", "0x10", "1e1", "12tail", "513", "4294967296"}) {
        REQUIRE(langSourceParseCount(text, &count) == 0);
        REQUIRE(count == 0);
    }
    const auto json = source(R"([{"index":0,"text":null}])");
    for (s32 expected : {0, 2, 513}) {
        Bank bank;
        const char *error = nullptr;
        REQUIRE(langSourceParseJson(json.data(), json.size(), expected, LANG_SOURCE_LATIN1, &bank.value, &error) == 0);
    }
}

TEST_CASE("language manifest validation preserves count presence before registration",
    "[modding][pdxxx][pdlang][lang-source]")
{
    s32 bank = -1, declared = 0;
    u32 count = 99;
    const std::string missing = R"({"source_bank":38})";
    REQUIRE(langSourceParseManifestFields(missing.data(), missing.size(), &bank, &count, &declared) == 1);
    REQUIRE(bank == 38);
    REQUIRE(count == 0);
    REQUIRE(declared == 0);
    const std::string zero = R"({"source_bank":38,"string_count":0})";
    REQUIRE(langSourceParseManifestFields(zero.data(), zero.size(), &bank, &count, &declared) == 1);
    REQUIRE(declared == 1);
    const std::string integral = "\xef\xbb\xbf" R"({"source_bank":6.8e1,"string_count":512.0})";
    REQUIRE(langSourceParseManifestFields(integral.data(), integral.size(), &bank, &count, &declared) == 1);
    REQUIRE(bank == 68);
    REQUIRE(count == 512);
    REQUIRE(declared == 1);
    for (const std::string bad : {
            R"({"source_bank":0,"string_count":1})", R"({"source_bank":69,"string_count":1})",
            R"({"source_bank":1.5,"string_count":1})", R"({"source_bank":1,"string_count":-1})",
            R"({"source_bank":1,"string_count":513})", R"({"source_bank":1,"string_count":"0"})",
            R"({"source_bank":1,"string_count":0,"string_count":1})"}) {
        REQUIRE(langSourceParseManifestFields(bad.data(), bad.size(), &bank, &count, &declared) == 0);
        REQUIRE(bank == -1);
        REQUIRE(count == 0);
        REQUIRE(declared == 0);
    }
}

TEST_CASE("language malformed native offsets and unterminated strings reject export",
    "[modding][pdxxx][pdlang][lang-source][negative]")
{
    std::vector<std::vector<u8>> invalid;
    std::vector<u8> inside(24, 0); offset(inside, 0, 12); offset(inside, 1, 4); invalid.push_back(inside);
    std::vector<u8> outside(24, 0); offset(outside, 0, 12); offset(outside, 1, 24); invalid.push_back(outside);
    std::vector<u8> unterminated(16, 'x'); offset(unterminated, 0, 8); offset(unterminated, 1, 0); invalid.push_back(unterminated);
    std::vector<u8> unaligned(16, 0); offset(unaligned, 0, 6); invalid.push_back(unaligned);
    invalid.emplace_back(8, 0); // Ambiguous all-null extent without a declaration.
    for (const auto &raw : invalid) {
        char *json = nullptr;
        u32 size = 99, count = 99;
        const char *error = nullptr;
        REQUIRE(langSourceExportNative(raw.data(), static_cast<u32>(raw.size()), -1,
            LANG_SOURCE_LATIN1, &json, &size, &count, &error) == 0);
        REQUIRE(json == nullptr);
        REQUIRE(size == 0);
        REQUIRE(count == 0);
        REQUIRE(error != nullptr);
    }
}
