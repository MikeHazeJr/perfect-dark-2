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
