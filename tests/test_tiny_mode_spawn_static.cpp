/*
 * tests/test_tiny_mode_spawn_static.cpp -- Tiny Mode generic-enemy spawn pins.
 *
 * Source under test:
 *   src/game/body.c
 *   src/game/setup.c
 *
 * Tiny Mode should triple authored non-unique enemy setup chrs without
 * widening into scripted clone spawners, story characters, multiplayer bots,
 * or non-enemies.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

}

TEST_CASE("tiny-mode spawn: generic enemy predicate stays narrow",
          "[cheats][tiny][spawn][static]") {
	const std::string body = readFile("src/game/body.c");

	REQUIRE(body.find("BODY_TINY_MODE_ENEMY_MULTIPLIER 3") != std::string::npos);
	REQUIRE(body.find("CHEAT_SMALLJO) || cheatIsActive(CHEAT_SMALLCHARACTERS)") != std::string::npos);
	REQUIRE(body.find("packed->team != TEAM_ENEMY") != std::string::npos);
	REQUIRE(body.find("packed->spawnflags & SPAWNFLAG_BASICGUARD") != std::string::npos);
	REQUIRE(body.find("packed->spawnflags & SPAWNFLAG_INVINCIBLE") != std::string::npos);
	REQUIRE(body.find("packed->chair != -1 || packed->convtalk != 0") != std::string::npos);
	REQUIRE(body.find("CHRFLAG0_CAN_HEARSPAWN | CHRFLAG0_CHUCKNORRIS") != std::string::npos);
	REQUIRE(body.find("g_Vars.normmplayerisrunning || g_Vars.mplayerisrunning") != std::string::npos);

	REQUIRE(body.find("case BODY_TRENT:") != std::string::npos);
	REQUIRE(body.find("case BODY_CASSANDRA:") != std::string::npos);
	REQUIRE(body.find("case BODY_DRCAROLL:") != std::string::npos);
	REQUIRE(body.find("case BODY_CHICROB:") != std::string::npos);
	REQUIRE(body.find("case BODY_SKEDARKING:") != std::string::npos);

	REQUIRE(body.find("return BODY_TINY_MODE_ENEMY_MULTIPLIER - 1;") != std::string::npos);
}

TEST_CASE("tiny-mode spawn: setup allocates slots and copies safely",
          "[cheats][tiny][spawn][static]") {
	const std::string setup = readFile("src/game/setup.c");
	const std::string header = readFile("src/include/game/body.h");

	REQUIRE(header.find("s32 bodyTinyModeExtraChrCountForPacked(const struct packedchr *packed);") != std::string::npos);
	REQUIRE(header.find("struct chrdata *bodyAllocateChr(") != std::string::npos);

	REQUIRE(setup.find("static s32 setupCountTinyModeExtraChrs(void)") != std::string::npos);
	REQUIRE(setup.find("count += bodyTinyModeExtraChrCountForPacked((struct packedchr *)obj);") != std::string::npos);
	REQUIRE(setup.find("TINYMODE: added %d generic-enemy chr slots for model allocation") != std::string::npos);
	REQUIRE(setup.find("CHRSLOTS: added %d Tiny Mode generic-enemy slots") != std::string::npos);

	REQUIRE(setup.find("static void setupCreateTinyModeExtraChrs(") != std::string::npos);
	REQUIRE(setup.find("clone.chrnum = chrsGetNextUnusedChrnum();") != std::string::npos);
	REQUIRE(setup.find("clone.spawnflags |= SPAWNFLAG_IGNORECOLLISION;") != std::string::npos);
	REQUIRE(setup.find("struct chrdata *chr = bodyAllocateChr(stagenum, packed, index);") != std::string::npos);
	REQUIRE(setup.find("if (chr != NULL)") != std::string::npos);
	REQUIRE(setup.find("setupCreateTinyModeExtraChrs(stagenum, packed, index);") != std::string::npos);
}

TEST_CASE("tiny-mode spawn: generic enemies are made physically small",
          "[cheats][tiny][spawn][static]") {
	const std::string body = readFile("src/game/body.c");

	REQUIRE(body.find("bodyTinyModeScaleGenericEnemy") != std::string::npos);
	REQUIRE(body.find("modelSetScale(chr->model, chr->model->scale * BODY_TINY_MODE_ENEMY_SCALE);") != std::string::npos);
	REQUIRE(body.find("chr->radius = (s32)(chr->radius * BODY_TINY_MODE_ENEMY_SCALE);") != std::string::npos);
	REQUIRE(body.find("chr->height = (s32)(chr->height * BODY_TINY_MODE_ENEMY_SCALE);") != std::string::npos);
	REQUIRE(body.find("bodyTinyModeScaleGenericEnemy(chr, packed, bodynum);") != std::string::npos);
}
