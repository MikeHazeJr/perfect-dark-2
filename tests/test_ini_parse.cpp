/**
 * test_ini_parse.cpp -- the descriptor INI parser the game uses.
 *
 * The rest of pd-tests links a stand-in for iniParseBuffer, which never had
 * the defect recorded as B-1140. These cases run the real loop
 * (port/src/ini_parse.c).
 */
#include "catch.hpp"
extern "C" {
#include "assetcatalog_scanner.h"
}
#include <cstring>
#include <memory>
#include <string>

namespace {

struct IniResult {
	std::unique_ptr<ini_section_t> ini = std::make_unique<ini_section_t>();
	char err[160];
	s32 ok = 0;

	explicit IniResult(const std::string &text) {
		std::memset(err, 'x', sizeof(err));
		err[sizeof(err) - 1] = '\0';
		ok = iniParseText(text.data(), static_cast<u32>(text.size()), ini.get(), err, sizeof(err));
	}

	std::string value(const char *key) const {
		for (s32 i = 0; i < ini->count; i++) {
			if (std::strcmp(ini->pairs[i].key, key) == 0) return ini->pairs[i].value;
		}
		return "<missing>";
	}
};

} // namespace

TEST_CASE("descriptor INI parser reads sections, pairs and comments",
          "[ini][modding]") {
	const IniResult r(
		"title line before any section\n"
		"ignored = because no section yet\n"
		"; comment\n"
		"# comment\n"
		"[weapon]\r\n"
		"  catalog_id   =   mod:weapon_x  \r\n"
		"name = Two Words\r"
		"formula = a = b = c\n"
		"quoted = \"kept as written\"\n"
		"empty =\n"
		"\n"
		"[meta]\n"
		"manifest = _meta/manifest.json\n"
		"last = no newline at the end");
	INFO(r.err);
	REQUIRE(r.ok == 1);
	CHECK(r.err[0] == '\0');
	CHECK(std::string(r.ini->type) == "weapon");
	CHECK(r.ini->count == 7);
	CHECK(r.value("catalog_id") == "mod:weapon_x");
	CHECK(r.value("name") == "Two Words");
	CHECK(r.value("formula") == "a = b = c");
	CHECK(r.value("quoted") == "\"kept as written\"");
	CHECK(r.value("empty") == "");
	CHECK(r.value("manifest") == "_meta/manifest.json");
	CHECK(r.value("last") == "no newline at the end");
	CHECK(r.value("ignored") == "<missing>");
}

TEST_CASE("descriptor INI parser refuses a line it cannot read instead of hanging (B-1140)",
          "[ini][modding]") {
	/* Before the fix this call never returned: the loop "skipped" the line
	 * without moving past it. */
	const IniResult colon(
		"[vehicle]\n"
		"catalog_id = mod:vehicle_x\n"
		"name: Colon Is Not Equals\n"
		"speed = 4\n");
	CHECK(colon.ok == 0);
	CHECK(std::string(colon.err) == "line 3 is not a comment, a section or key = value");

	const IniResult lastLine("[vehicle]\ncatalog_id = mod:vehicle_x\nstray words");
	CHECK(lastLine.ok == 0);
	CHECK(std::string(lastLine.err).find("line 3 ") == 0);

	const IniResult crlf("[vehicle]\r\ncatalog_id = mod:vehicle_x\r\n\r\nstray\r\n");
	CHECK(crlf.ok == 0);
	CHECK(std::string(crlf.err).find("line 4 ") == 0);

	/* The same words before the first section were always ignored. */
	const IniResult before("stray words\n[vehicle]\ncatalog_id = mod:vehicle_x\n");
	CHECK(before.ok == 1);
	CHECK(before.value("catalog_id") == "mod:vehicle_x");

	/* A section header that never closes is not a section and not a pair. */
	const IniResult open("[vehicle]\n[broken\n");
	CHECK(open.ok == 1);
	CHECK(open.ini->count == 0);
}

TEST_CASE("descriptor INI parser keeps its limits",
          "[ini][modding]") {
	SECTION("a text with no section is not a descriptor and gives no reason") {
		const IniResult r("key = value\n");
		CHECK(r.ok == 0);
		CHECK(r.err[0] == '\0');
		const IniResult empty("");
		CHECK(empty.ok == 0);
		CHECK(empty.err[0] == '\0');
	}

	SECTION("the pair limit") {
		std::string text = "[x]\n";
		for (int i = 0; i < INI_MAX_PAIRS; i++) {
			text += "k" + std::to_string(i) + " = v\n";
		}
		const IniResult full(text);
		CHECK(full.ok == 1);
		CHECK(full.ini->count == INI_MAX_PAIRS);
		const IniResult over(text + "one_more = v\n");
		CHECK(over.ok == 0);
		CHECK(std::string(over.err).find("pair limit") != std::string::npos);
	}

	SECTION("a key or a value that does not fit") {
		const std::string longKey(sizeof(((ini_pair_t *)nullptr)->key), 'k');
		const std::string longValue(sizeof(((ini_pair_t *)nullptr)->value), 'v');
		const IniResult key("[x]\n" + longKey + " = v\n");
		CHECK(key.ok == 0);
		CHECK(std::string(key.err).find("key or value too long") != std::string::npos);
		const IniResult value("[x]\nk = " + longValue + "\n");
		CHECK(value.ok == 0);
		CHECK(std::string(value.err).find("line 2: key or value too long (key 'k')") == 0);
		const IniResult fits("[x]\nk = " + longValue.substr(1) + "\n");
		CHECK(fits.ok == 1);
	}

	SECTION("a section name that does not fit") {
		const std::string longName(sizeof(((ini_section_t *)nullptr)->type), 's');
		const IniResult r("[" + longName + "]\nk = v\n");
		CHECK(r.ok == 0);
		CHECK(std::string(r.err).find("section name too long") != std::string::npos);
	}

	SECTION("missing arguments") {
		ini_section_t out;
		char err[64] = "";
		CHECK(iniParseText(nullptr, 0, &out, err, sizeof(err)) == 0);
		CHECK(iniParseText("[x]\n", 4, nullptr, err, sizeof(err)) == 0);
		CHECK(iniParseText("[x]\nk = v\n", 10, &out, nullptr, 0) == 1);
		CHECK(out.count == 1);
	}
}
