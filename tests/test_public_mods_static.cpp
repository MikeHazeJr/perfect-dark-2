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
	const std::string ft = readTextFile("port/src/file_transfer.c");
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

	REQUIRE(ft.find("#include \"modmgr.h\"") != std::string::npos);
	REQUIRE(ft.find("static void fileTransferInstallReceivedMod") != std::string::npos);
	REQUIRE(ft.find("modmgrValidateArchiveFile(path, err, sizeof(err))") != std::string::npos);
	REQUIRE(ft.find("FT: received mod archive rejected") != std::string::npos);
	REQUIRE(ft.find("modmgrInstallArchiveFile(inbox_path, enable_now, err, sizeof(err))") != std::string::npos);
	REQUIRE(ft.find("through shared .pdmod installer") != std::string::npos);
	REQUIRE(ft.find("if (r->kind == FT_KIND_MOD)") != std::string::npos);
	REQUIRE(ft.find("fileTransferInstallReceivedMod(r->inbox_path, r->name, r->src_handle)") != std::string::npos);

	REQUIRE(ui.find("#include \"modmgr.h\"") != std::string::npos);
	REQUIRE(ui.find("modmgrGetCount()") != std::string::npos);
	REQUIRE(ui.find("ImGui::BeginCombo(\"Installed mod\"") != std::string::npos);
	REQUIRE(ui.find("shareModPublicAdd(id, name, ver, size)") != std::string::npos);
	REQUIRE(ui.find("InputTextWithHint(\"mod id\"") == std::string::npos);
}

TEST_CASE("Request-download installs use friend-aware enable policy",
          "[social][public_mods][static][c3810]")
{
	const std::string ft = readTextFile("port/src/file_transfer.c");
	const std::string fth = readTextFile("port/include/file_transfer.h");
	const std::string ui = readTextFile("port/fast3d/pdgui_friends.cpp");

	REQUIRE(fth.find("fileTransferPendingModEnableCount") != std::string::npos);
	REQUIRE(fth.find("fileTransferPendingModEnablePeek") != std::string::npos);
	REQUIRE(fth.find("fileTransferPendingModEnableAccept") != std::string::npos);
	REQUIRE(fth.find("fileTransferPendingModEnableDecline") != std::string::npos);

	REQUIRE(ft.find("FT_PENDING_MOD_ENABLE_MAX") != std::string::npos);
	REQUIRE(ft.find("socialFriendByHandle(sender_handle)") != std::string::npos);
	REQUIRE(ft.find("const s32 enable_now = socialFriendByHandle(sender_handle) ? 1 : 0") != std::string::npos);
	REQUIRE(ft.find("enabled received mod '%s' (friend request-download)") != std::string::npos);
	REQUIRE(ft.find("ftQueuePendingModEnable(sender_handle, mod)") != std::string::npos);
	REQUIRE(ft.find("modmgrApplyChanges()") != std::string::npos);
	REQUIRE(ft.find("installed disabled; enable prompt queue full") != std::string::npos);
	REQUIRE(ft.find("fileTransferDebugInstallReceivedModForSmoke") != std::string::npos);

	REQUIRE(ui.find("renderReceivedModEnableModal") != std::string::npos);
	REQUIRE(ui.find("fileTransferPendingModEnableCount() > 0") != std::string::npos);
	REQUIRE(ui.find("fileTransferPendingModEnableAccept()") != std::string::npos);
	REQUIRE(ui.find("fileTransferPendingModEnableDecline()") != std::string::npos);
	REQUIRE(ui.find("Enable now") != std::string::npos);
	REQUIRE(ui.find("Keep disabled") != std::string::npos);
	REQUIRE(ui.find("not in your") != std::string::npos);
	REQUIRE(ui.find("friends list") != std::string::npos);
}

TEST_CASE("Public Mods install path refreshes manifest digests and registry",
          "[social][public_mods][static][c3809]")
{
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const std::string distrib = readTextFile("port/src/net/netdistrib.c");

	REQUIRE(modmgr.find("sha256Hash((const u8 *)mfstBuf, mfstSize, manifestSha)") != std::string::npos);
	REQUIRE(modmgr.find("memcpy(mod->sha256, manifestSha, sizeof(mod->sha256))") != std::string::npos);
	REQUIRE(modmgr.find("SHA-256 over the WHOLE archive file") == std::string::npos);
	REQUIRE(modmgr.find("Folder mods use the same boundary") != std::string::npos);

	REQUIRE(distrib.find("#include \"modmgr.h\"") != std::string::npos);
	REQUIRE(distrib.find("modsdir = modmgrGetModsDir()") != std::string::npos);
	REQUIRE(distrib.find("modmgrRescanDirectory()") != std::string::npos);
	REQUIRE(distrib.find("refreshed mod registry after installing") != std::string::npos);
}

TEST_CASE("received Public Mods validate strict archive payloads before install",
          "[social][public_mods][static][c3844][s110]")
{
	const std::string ft = readTextFile("port/src/file_transfer.c");
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");

	const size_t ft_received = ft.find("static void fileTransferInstallReceivedMod");
	REQUIRE(ft_received != std::string::npos);
	const size_t ft_validate = ft.find("if (!ftValidateReceivedModArchive(inbox_path)) return;", ft_received);
	const size_t ft_install = ft.find("modmgrInstallArchiveFile(inbox_path, enable_now, err, sizeof(err))", ft_received);
	REQUIRE(ft_validate != std::string::npos);
	REQUIRE(ft_install != std::string::npos);
	REQUIRE(ft_validate < ft_install);

	const size_t install_fn = modmgr.find("s32 modmgrInstallArchiveFile");
	REQUIRE(install_fn != std::string::npos);
	const size_t install_validate = modmgr.find("modmgrValidateArchiveFile(archive_path, out_error, error_len)", install_fn);
	const size_t install_copy = modmgr.find("modmgrCopyFileAtomic(archive_path, dst)", install_fn);
	REQUIRE(install_validate != std::string::npos);
	REQUIRE(install_copy != std::string::npos);
	REQUIRE(install_validate < install_copy);

	REQUIRE(modmgr.find("modmgrArchiveEntryHasForbiddenBinPayload(name)") != std::string::npos);
	REQUIRE(modmgr.find("strncpy(out_kind, \"authored .bin\"") != std::string::npos);
	REQUIRE(modmgr.find("modmgrArchiveEntryHasForbiddenPublicTsvPayload(name)") != std::string::npos);
	REQUIRE(modmgr.find("strncpy(out_kind, \"public .tsv\"") != std::string::npos);
	REQUIRE(modmgr.find("assetArchiveValidateBytes(nested, nested_size") != std::string::npos);
	REQUIRE(modmgr.find("strncpy(out_kind, \"invalid typed archive\"") != std::string::npos);
	REQUIRE(modmgr.find("External-format archives cannot contain %s payloads") != std::string::npos);

	const size_t arena_case = scanner.find("case ASSET_ARENA:");
	REQUIRE(arena_case != std::string::npos);
	const size_t arena_scenario = scanner.find("iniGet(ini, \"scenario\"", arena_case);
	const size_t arena_scenario_archive = scanner.find("iniGet(ini, \"scenario_archive\"", arena_case);
	const size_t arena_archive = scanner.find("catalogSetPrimaryFile(e, e->ext.arena.scenario_archive)", arena_case);
	const size_t arena_end = scanner.find("case ASSET_BODY:", arena_case);
	REQUIRE(arena_scenario != std::string::npos);
	REQUIRE(arena_scenario_archive != std::string::npos);
	REQUIRE(arena_archive != std::string::npos);
	REQUIRE(arena_end != std::string::npos);
	REQUIRE(arena_scenario < arena_scenario_archive);
	REQUIRE(arena_scenario_archive < arena_archive);
	REQUIRE(scanner.find("geometry_file", arena_case) > arena_end);

	const size_t head_case = scanner.find("case ASSET_HEAD:");
	REQUIRE(head_case != std::string::npos);
	const size_t head_mesh = scanner.find("const char *mesh = iniGet(ini, \"mesh_archive\"", head_case);
	const size_t head_source_path = scanner.find("sourceModelPathFromMeshArchive(e->ext.head.mesh_archive", head_case);
	const size_t head_archive = scanner.find("catalogSetPrimaryFile(e, source_path)", head_case);
	const size_t head_end = scanner.find("case ASSET_MODEL:", head_case);
	REQUIRE(head_mesh != std::string::npos);
	REQUIRE(head_source_path != std::string::npos);
	REQUIRE(head_archive != std::string::npos);
	REQUIRE(head_end != std::string::npos);
	REQUIRE(head_mesh < head_source_path);
	REQUIRE(head_source_path < head_archive);
	REQUIRE(scanner.find("model_file", head_case) > head_end);
}

TEST_CASE("missing mods-enabled json is treated as clean no-mods state",
          "[social][public_mods][static][c3844]")
{
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const size_t loader = modmgr.find("static bool modmgrLoadModsEnabledJson(void)");
	REQUIRE(loader != std::string::npos);

	const size_t stat_gate = modmgr.find("stat(path, &st) != 0 || !S_ISREG(st.st_mode)", loader);
	const size_t file_load = modmgr.find("fsFileLoad(path, &filesize)", loader);
	REQUIRE(stat_gate != std::string::npos);
	REQUIRE(file_load != std::string::npos);
	REQUIRE(stat_gate < file_load);
}

TEST_CASE("mod manifest parse logs do not look like asset fallback failures",
          "[social][public_mods][static][c3844]")
{
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const size_t log = modmgr.find("modmgr: parsed mod.json");
	REQUIRE(log != std::string::npos);
	const size_t log_end = modmgr.find(");", log);
	REQUIRE(log_end != std::string::npos);
	const std::string log_block = modmgr.substr(log, log_end - log);

	REQUIRE(log_block.find("base_mod=%s") != std::string::npos);
	REQUIRE(log_block.find("fallback=%s") == std::string::npos);
	REQUIRE(log_block.find("mod->base_fallback") != std::string::npos);
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
