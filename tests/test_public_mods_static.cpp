/*
 * tests/test_public_mods_static.cpp -- Public Mods publishing guards.
 *
 * These tests pin the first public creator-content sharing slice: the Social
 * shell must publish installed mods from the mod registry, and peer mod
 * requests must not turn network-provided IDs into filesystem paths.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

static std::string readTextFile(const char *path)
{
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

TEST_CASE("Public Mods publishing is registry-backed and path-safe", "[social][public_mods][static]")
{
	const std::string share = readTextFile("port/src/social_share.c");
	const std::string ui = readTextFile("port/fast3d/pdgui_friends.cpp");
	const std::string cmake = readTextFile("CMakeLists.txt");

	REQUIRE(cmake.find("tests/test_public_mods_static.cpp") != std::string::npos);

	REQUIRE(share.find("#include \"modmgr.h\"") != std::string::npos);
	REQUIRE(share.find("#include \"modpack_pdmod.h\"") != std::string::npos);
	REQUIRE(share.find("static s32 shareModIdIsSafe") != std::string::npos);
	REQUIRE(share.find("static s32 packFolderModForPublicShare") != std::string::npos);
	REQUIRE(share.find("isalnum(ch) || ch == '_' || ch == '-' || ch == '.'") != std::string::npos);
	REQUIRE(share.find("mod_id[0] == '.' || mod_id[len - 1] == '.'") != std::string::npos);
	REQUIRE(share.find("ch == '.' && mod_id[i + 1] == '.'") != std::string::npos);

	REQUIRE(share.find("if (!shareModIdIsSafe(mod_id)) return -1;") != std::string::npos);
	REQUIRE(share.find("modinfo_t *mod = modmgrFindMod(mod_id);") != std::string::npos);
	REQUIRE(share.find("if (!mod || !mod->valid) return -1;") != std::string::npos);
	REQUIRE(share.find("!shareModIdIsSafe(m->mod_id) || !mod || !mod->valid") != std::string::npos);

	REQUIRE(share.find("friend_handle == 0 || !shareModIdIsSafe(mod_id)") != std::string::npos);
	REQUIRE(share.find("unsafe mod id request") != std::string::npos);
	REQUIRE(share.find("non-public mod") != std::string::npos);
	REQUIRE(share.find("modpackPdmodFromFolder(mod->dirpath, out_path)") != std::string::npos);
	REQUIRE(share.find("modpackPdmodLastError()") != std::string::npos);
	REQUIRE(share.find("fileTransferSendFile(from_handle, pdmod_path)") != std::string::npos);
	REQUIRE(share.find("public folder mod") != std::string::npos);
	REQUIRE(share.find("%s/mods/installed/%s") == std::string::npos);
	REQUIRE(share.find("mod manifest offer") == std::string::npos);

	REQUIRE(ui.find("#include \"modmgr.h\"") != std::string::npos);
	REQUIRE(ui.find("modmgrGetCount()") != std::string::npos);
	REQUIRE(ui.find("ImGui::BeginCombo(\"Installed mod\"") != std::string::npos);
	REQUIRE(ui.find("shareModPublicAdd(id, name, ver, size)") != std::string::npos);
	REQUIRE(ui.find("InputTextWithHint(\"mod id\"") == std::string::npos);
}

TEST_CASE("Public Mods registry writes JSON strings through an escaping helper", "[social][public_mods][static]")
{
	const std::string share = readTextFile("port/src/social_share.c");

	REQUIRE(share.find("static void writeJsonString") != std::string::npos);
	REQUIRE(share.find("writeJsonString(f, m->mod_id)") != std::string::npos);
	REQUIRE(share.find("writeJsonString(f, m->display_name)") != std::string::npos);
	REQUIRE(share.find("writeJsonString(f, m->version)") != std::string::npos);
	REQUIRE(share.find("fprintf(f, \", \\\"size\\\": %u }\"") != std::string::npos);
}
