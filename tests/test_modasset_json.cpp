#include "catch.hpp"

#include <string>

extern "C" {
#include "modasset_json.h"
}

namespace {

bool parseS32ObjectValue(const std::string &value, s32 *out)
{
	const char *end = value.data() + value.size();
	const char *token_end = nullptr;
	s32 parsed;
	if (!modAssetJsonParseS32Token(value.data(), end, &parsed, &token_end)
			|| !modAssetJsonTokenHasContainerBoundary(token_end, end, '}')) {
		return false;
	}
	*out = parsed;
	return true;
}

bool parseU32ArrayValue(const std::string &value, u32 *out)
{
	const char *end = value.data() + value.size();
	const char *token_end = nullptr;
	u32 parsed;
	if (!modAssetJsonParseU32Token(value.data(), end, &parsed, &token_end)
			|| !modAssetJsonTokenHasContainerBoundary(token_end, end, ']')) {
		return false;
	}
	*out = parsed;
	return true;
}

bool parseBoolObjectValue(const std::string &value, s32 *out)
{
	const char *end = value.data() + value.size();
	const char *token_end = nullptr;
	s32 parsed;
	if (!modAssetJsonParseBoolToken(value.data(), end, &parsed, &token_end)
			|| !modAssetJsonTokenHasContainerBoundary(token_end, end, '}')) {
		return false;
	}
	*out = parsed;
	return true;
}

} // namespace

TEST_CASE("mod-asset JSON integer tokens preserve exact signed and unsigned domains",
		"[modding][json][animation][B-1102]")
{
	s32 signed_value = 0;
	u32 unsigned_value = 0;

	REQUIRE(parseS32ObjectValue("0}", &signed_value));
	REQUIRE(signed_value == 0);
	REQUIRE(parseS32ObjectValue("-0 ,", &signed_value));
	REQUIRE(signed_value == 0);
	REQUIRE(parseS32ObjectValue("2147483647}", &signed_value));
	REQUIRE(signed_value == 2147483647);
	REQUIRE(parseS32ObjectValue("-2147483648}", &signed_value));
	REQUIRE(signed_value == (-2147483647 - 1));

	REQUIRE(parseU32ArrayValue("4294967295]", &unsigned_value));
	REQUIRE(unsigned_value == 0xffffffffu);
	REQUIRE(parseU32ArrayValue("2147483648 ,", &unsigned_value));
	REQUIRE(unsigned_value == 0x80000000u);
}

TEST_CASE("mod-asset JSON integer tokens reject aliases overflow and prefixes",
		"[modding][json][animation][B-1102]")
{
	s32 signed_value = 17;
	u32 unsigned_value = 23;

	for (const char *value : {"00}", "01}", "-01}", "+1}", "1.0}",
			"1e0}", "2147483648}", "-2147483649}", "9junk}"}) {
		INFO(value);
		REQUIRE_FALSE(parseS32ObjectValue(value, &signed_value));
	}
	for (const char *value : {"00]", "01]", "-1]", "+1]", "1.0]",
			"4294967296]", "9junk]"}) {
		INFO(value);
		REQUIRE_FALSE(parseU32ArrayValue(value, &unsigned_value));
	}
}

TEST_CASE("mod-asset JSON booleans require exact container-bounded tokens",
		"[modding][json][animation][B-1102]")
{
	s32 value = -1;

	REQUIRE(parseBoolObjectValue("true}", &value));
	REQUIRE(value == 1);
	REQUIRE(parseBoolObjectValue("false ,", &value));
	REQUIRE(value == 0);
	REQUIRE_FALSE(parseBoolObjectValue("falseX}", &value));
	REQUIRE_FALSE(parseBoolObjectValue("true0}", &value));
	REQUIRE_FALSE(parseBoolObjectValue("False}", &value));
}

TEST_CASE("mod-asset JSON key comparisons decode standard escapes without truncation",
		"[modding][json][gltf-scene][T-ASSETS-043]")
{
	struct KeyCase { const char *token; const char *expected; };
	const KeyCase cases[] = {
		{ "\"meshes\"", "meshes" },
		{ "\"me\\u0073hes\"", "meshes" },
		{ "\"PO\\u0053ITION\"", "POSITION" },
		{ "\"\\u00e9\"", "\xc3\xa9" },
		{ "\"\\ud83d\\ude80\"", "\xf0\x9f\x9a\x80" },
		{ "\"line\\nkey\\t\\\"\\\\\\/\"", "line\nkey\t\"\\/" },
		{ "\"\"", "" },
	};
	for (const KeyCase &test : cases) {
		const std::string token(test.token);
		INFO(token);
		REQUIRE(modAssetJsonStringEquals(token.data(), token.data() + token.size(),
			test.expected) == 1);
		REQUIRE(modAssetJsonStringEquals(token.data(), token.data() + token.size(),
			"different") == 0);
	}
	const std::string long_key(4096, 'x');
	const std::string token = '"' + long_key + '"';
	REQUIRE(modAssetJsonStringEquals(token.data(), token.data() + token.size(),
		long_key.c_str()) == 1);
	REQUIRE(modAssetJsonStringEquals(token.data(), token.data() + token.size(),
		long_key.substr(0, 64).c_str()) == 0);
}

TEST_CASE("mod-asset JSON key comparisons reject malformed and non-string tokens",
		"[modding][json][gltf-scene][T-ASSETS-043]")
{
	for (const char *source : { "", "123", "null", "{}", "[]", "\"unterminated",
			"\"bad\\q\"", "\"bad\nkey\"", "\"\\u0000\"", "\"\\ud800\"",
			"\"\\udc00\"", "\"\\ud800\\u0041\"", "\"meshes\" trailing",
			"\"meshes\",\"nodes\"", "\"\xc0\xaf\"" }) {
		const std::string token(source);
		INFO(token);
		REQUIRE(modAssetJsonStringEquals(token.data(), token.data() + token.size(),
			"meshes") == -1);
	}
	const std::string bounded = "\"meshes\" trailing outside token";
	REQUIRE(modAssetJsonStringEquals(bounded.data(), bounded.data() + 8, "meshes") == 1);
	REQUIRE(modAssetJsonStringEquals(nullptr, nullptr, "meshes") == -1);
	REQUIRE(modAssetJsonStringEquals(bounded.data(), bounded.data() + 8, nullptr) == -1);
}

TEST_CASE("mod-asset JSON document validation enforces decoded key uniqueness and complete input",
		"[modding][json][weapon_graph][T-ASSETS-045]")
{
	for (const char *source : { "{}", " \n{\"integer\":1,\"float\":1.0,\"exponent\":1e0}\t",
			"{\"name\":\"\\ud83d\\ude80\",\"nested\":{\"name\":true}}" }) {
		const std::string json(source);
		INFO(json);
		REQUIRE(modAssetJsonValidateObject(json.data(), json.size()) == 1);
	}
	for (const char *source : { "", "[]", "null", "{} trailing", "{}{}",
			"{\"x\":1,}", "{\"x\":[1,]}", "{\"x\":1 \"y\":2}",
			"{\"x\":1,\"\\u0078\":2}", "{\"nested\":{\"x\":1,\"x\":2}}",
			"{\"x\":\"\\q\"}", "{\"x\":\"\\ud800\"}", "{\"x\":\"\\u0000\"}",
			"{\"x\":01}", "{\"x\":1e1000}" }) {
		const std::string json(source);
		INFO(json);
		REQUIRE(modAssetJsonValidateObject(json.data(), json.size()) == 0);
	}
	const std::string bounded = "{}trailing outside supplied span";
	REQUIRE(modAssetJsonValidateObject(bounded.data(), 2) == 1);
	REQUIRE(modAssetJsonValidateObject(nullptr, 0) == 0);
}

TEST_CASE("mod-asset JSON string decoding preserves UTF8 and rejects capacity loss",
		"[modding][json][weapon_graph][T-ASSETS-045]")
{
	const std::string token = "\"\\u00e9\\n\\\"\\\\\\/\\ud83d\\ude80\"";
	const std::string expected = "\xc3\xa9\n\"\\/\xf0\x9f\x9a\x80";
	char output[64] = {};
	REQUIRE(modAssetJsonDecodeString(token.data(), token.data() + token.size(),
		output, expected.size() + 1) == 1);
	REQUIRE(std::string(output) == expected);
	REQUIRE(modAssetJsonDecodeString(token.data(), token.data() + token.size(),
		output, expected.size()) == 0);
	REQUIRE(output[0] == '\0');
	const std::string utf8 = "\"\\u00e9\"";
	REQUIRE(modAssetJsonDecodeString(utf8.data(), utf8.data() + utf8.size(), output, 2) == 0);
	REQUIRE(output[0] == '\0');
	for (const char *source : { "17", "null", "\"\\q\"", "\"\\udc00\"", "\"ok\" false" }) {
		const std::string invalid(source);
		output[0] = 'x';
		REQUIRE(modAssetJsonDecodeString(invalid.data(), invalid.data() + invalid.size(),
			output, sizeof(output)) == 0);
		REQUIRE(output[0] == '\0');
	}
}
