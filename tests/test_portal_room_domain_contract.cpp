#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readPortalDomainFile(const char *path)
{
	std::ifstream input(path, std::ios::binary);
	REQUIRE(input.good());
	std::ostringstream contents;
	contents << input.rdbuf();
	return contents.str();
}

std::string portalDomainFunction(const std::string &source, const char *name)
{
	const std::string signature = std::string(name) + "(";
	size_t start = source.find(signature);
	size_t open = std::string::npos;

	while (start != std::string::npos) {
		open = source.find('{', start);
		const size_t semicolon = source.find(';', start);
		if (open != std::string::npos
				&& (semicolon == std::string::npos || open < semicolon)) {
			break;
		}
		start = source.find(signature, start + signature.size());
	}

	REQUIRE(start != std::string::npos);
	REQUIRE(open != std::string::npos);

	int depth = 0;
	for (size_t i = open; i < source.size(); i++) {
		if (source[i] == '{') {
			depth++;
		} else if (source[i] == '}') {
			depth--;
			if (depth == 0) {
				return source.substr(start, i - start + 1);
			}
		}
	}

	FAIL("unterminated function " << name);
	return std::string();
}

void requireUsesPortalBoundary(const std::string &source, const char *name,
	const char *boundary)
{
	const std::string function = portalDomainFunction(source, name);
	INFO(name);
	REQUIRE(function.find(boundary) != std::string::npos);
	REQUIRE(function.find("g_Rooms[g_BgPortals[") == std::string::npos);
}

} // namespace

TEST_CASE("portal traversal uses one validated room-domain boundary",
	"[scenario][room-domain][portal][t-tests-002][static]")
{
	const std::string header = readPortalDomainFile("src/include/game/bg.h");
	const std::string bg = readPortalDomainFile("src/game/bg.c");
	const std::string dlights = readPortalDomainFile("src/game/dlights.c");
	const std::string explosions = readPortalDomainFile("src/game/explosions.c");
	const std::string portal = readPortalDomainFile("src/lib/lib_17ce0.c");

	REQUIRE(header.find("bool bgRoomIsValid(s32 roomnum)") != std::string::npos);
	REQUIRE(header.find("bool bgPortalGetRooms(s32 portalnum") != std::string::npos);
	REQUIRE(header.find("bool bgPortalGetOtherRoom(s32 portalnum") != std::string::npos);

	const std::string boundary = portalDomainFunction(bg,
		"bool bgPortalGetRooms");
	REQUIRE(boundary.find("portalnum >= g_BgNumPortalCameraCacheItems") !=
		std::string::npos);
	REQUIRE(boundary.find("first <= 0 || second <= 0 || first == second") !=
		std::string::npos);
	REQUIRE(boundary.find("!bgRoomIsValid(first)") != std::string::npos);
	REQUIRE(boundary.find("!bgRoomIsValid(second)") != std::string::npos);

	requireUsesPortalBoundary(bg, "void bgConsumeSnakeItem",
		"bgPortalGetRooms");
	requireUsesPortalBoundary(bg, "void bgChooseRoomsToLoad",
		"bgMarkSecondHopLoadCandidates");
	requireUsesPortalBoundary(bg, "void bgInitPortal", "bgPortalGetRooms");
	requireUsesPortalBoundary(dlights, "void func0f002844",
		"bgPortalGetOtherRoom");
	requireUsesPortalBoundary(dlights, "void func0f00505c",
		"bgPortalGetRooms");
	requireUsesPortalBoundary(explosions,
		"static bool explosionCreateInternal",
		"bgPortalGetOtherRoom");
	requireUsesPortalBoundary(portal, "void portal00018148",
		"bgPortalGetRooms");

	REQUIRE(dlights.find("g_BgPortals[iterportalnum].roomnum") ==
		std::string::npos);
	REQUIRE(explosions.find("g_BgPortals[portalnum2].roomnum") ==
		std::string::npos);
	REQUIRE(portal.find("g_BgPortals[portalnum].roomnum") ==
		std::string::npos);
}

TEST_CASE("scenario extractor rejects portal endpoints outside its room domain",
	"[scenario][room-domain][extractor][t-tests-002][static]")
{
	const std::string extractor =
		readPortalDomainFile("port/src/romextract_pdarena.c");

	REQUIRE(extractor.find("portals[i].roomnum1 <= 0") !=
		std::string::npos);
	REQUIRE(extractor.find("portals[i].roomnum1 == portals[i].roomnum2") !=
		std::string::npos);
	REQUIRE(extractor.find("(u32)portals[i].roomnum2 >= room_count") !=
		std::string::npos);
	REQUIRE(extractor.find(
		"portal %u has invalid room boundary") != std::string::npos);
}
