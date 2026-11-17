/**
 * test_body_blood.cpp -- the blood a mod body bleeds (body_blood.h).
 *
 * C3 of context/designs/modding/maian-vehicle-and-veteran.md: a Maian-type mod
 * body bleeds the Maian green; base bodies keep the original's per-body list.
 */
#include "catch.hpp"

#include "constants.h"
#include "body_blood.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string bloodReadSource(const char *relative)
{
	const std::filesystem::path path = std::filesystem::path(PD_SOURCE_DIR) / relative;
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/* The whole definition of name (from "name(" at a line start to the next line
 * that is a lone closing brace). */
std::string bloodDefinition(const std::string &text, const std::string &signature)
{
	const size_t start = text.find("\n" + signature);
	if (start == std::string::npos) return std::string();
	const size_t end = text.find("\n}\n", start + 1);
	if (end == std::string::npos) return std::string();
	return text.substr(start, end - start + 3);
}

} // namespace

TEST_CASE("A Maian-type mod body bleeds the Maian green; base bodies keep the original's", "[body-blood]")
{
	CHECK(bodyBloodForCatalogBody("mod:body_maian_veteran", HEADBODYTYPE_MAIAN) == BODY_BLOOD_MAIAN);
	CHECK(bodyBloodForCatalogBody("example:tri_body", HEADBODYTYPE_MAIAN) == BODY_BLOOD_MAIAN);

	// Base bodies keep the original's list, even when their type is Maian
	// (the base Maian soldier bleeds red in the original).
	CHECK(bodyBloodForCatalogBody("base:maian_soldier", HEADBODYTYPE_MAIAN) == BODY_BLOOD_ORIGINAL);
	CHECK(bodyBloodForCatalogBody("base:elvis1", HEADBODYTYPE_MAIAN) == BODY_BLOOD_ORIGINAL);

	// Other mod body types and unknown bodies bleed what the original gives.
	CHECK(bodyBloodForCatalogBody("mod:body_guard", HEADBODYTYPE_DEFAULT) == BODY_BLOOD_ORIGINAL);
	CHECK(bodyBloodForCatalogBody("mod:body_agent", HEADBODYTYPE_FEMALE) == BODY_BLOOD_ORIGINAL);
	CHECK(bodyBloodForCatalogBody("mod:body_blonde", HEADBODYTYPE_MRBLONDE) == BODY_BLOOD_ORIGINAL);
	CHECK(bodyBloodForCatalogBody(nullptr, HEADBODYTYPE_MAIAN) == BODY_BLOOD_ORIGINAL);
	CHECK(bodyBloodForCatalogBody("", HEADBODYTYPE_MAIAN) == BODY_BLOOD_ORIGINAL);
}

TEST_CASE("chrGetBloodColour asks about mod bodies only after the original's list", "[body-blood]")
{
	const std::string chr = bloodReadSource("src/game/chr.c");
	const std::string fn = bloodDefinition(chr, "void chrGetBloodColour(");
	REQUIRE_FALSE(fn.empty());

	// The original's cases come first, unchanged in what they cover.
	const size_t maian = fn.find("case BODY_ELVISWAISTCOAT:");
	const size_t robots = fn.find("case BODY_CHICROB:");
	const size_t skedar = fn.find("case BODY_SKEDARKING:");
	REQUIRE(maian != std::string::npos);
	REQUIRE(robots != std::string::npos);
	REQUIRE(skedar != std::string::npos);

	// Then the mod body check, by catalog id and declared type (the type read
	// only for a body the catalog knows), with the same Maian colours, before
	// the red default.
	const size_t id = fn.find("bodyid = catalogBodyIdByBodynum(bodynum);");
	const size_t ask = fn.find("bodyid && bodyBloodForCatalogBody(bodyid, catalogGetBodyType(bodynum)) == BODY_BLOOD_MAIAN");
	const size_t red = fn.find("colour1[0] = 0x40;\n\t\tcolour1[1] = 10;");
	REQUIRE(id != std::string::npos);
	REQUIRE(ask != std::string::npos);
	REQUIRE(red != std::string::npos);
	CHECK(maian < id);
	CHECK(robots < id);
	CHECK(skedar < id);
	CHECK(id < ask);
	CHECK(ask < red);

	// Both Maian paths fill the same colours.
	const std::string helper = bloodDefinition(chr, "static void chrGetMaianBloodColour(");
	REQUIRE_FALSE(helper.empty());
	CHECK(helper.find("colour2[0] = 0x103010ff;") != std::string::npos);
	size_t calls = 0;
	for (size_t at = fn.find("chrGetMaianBloodColour(colour1, colour2);"); at != std::string::npos;
			at = fn.find("chrGetMaianBloodColour(colour1, colour2);", at + 1)) {
		calls++;
	}
	CHECK(calls == 2);
}
