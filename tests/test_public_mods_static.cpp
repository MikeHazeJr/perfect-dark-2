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
	REQUIRE(ft.find("modmgrInstallArchiveFile(inbox_path, err, sizeof(err))") != std::string::npos);
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
	REQUIRE(ft.find("ftPendingModAcceptIndex(pending_index)") != std::string::npos);
	REQUIRE(ft.find("ftQueuePendingModEnable(sender_handle, mod)") != std::string::npos);
	REQUIRE(ft.find("modmgrApplyChanges()") == std::string::npos);
	REQUIRE(ft.find("modmgrPrepareInstalledActivation(mod_index, &p->plan, &p->result)") != std::string::npos);
	REQUIRE(ft.find("modmgrApplyPreparedChanges(p->plan, &p->result)") != std::string::npos);
	REQUIRE(ft.find("installed disabled; enable prompt queue full") != std::string::npos);
	REQUIRE(ft.find("fileTransferDebugInstallReceivedModForSmoke") != std::string::npos);

	REQUIRE(ui.find("renderReceivedModEnableModal") != std::string::npos);
	REQUIRE(ui.find("fileTransferPendingModEnableCount() > 0") != std::string::npos);
	REQUIRE(ui.find("fileTransferPendingModEnableAccept()") != std::string::npos);
	REQUIRE(ui.find("fileTransferPendingModEnableDecline()") != std::string::npos);
	REQUIRE(ui.find("Enable now") != std::string::npos);
	REQUIRE(ui.find("Keep disabled") != std::string::npos);
	REQUIRE(ui.find("Retry activation") != std::string::npos);
	REQUIRE(ui.find("Discard retry") != std::string::npos);
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
	const size_t ft_install = ft.find("modmgrInstallArchiveFile(inbox_path, err, sizeof(err))", ft_received);
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
	const size_t arena_end = scanner.find("case ASSET_MODEL:", arena_case);
	REQUIRE(arena_scenario != std::string::npos);
	REQUIRE(arena_scenario_archive != std::string::npos);
	REQUIRE(arena_archive != std::string::npos);
	REQUIRE(arena_end != std::string::npos);
	REQUIRE(arena_scenario < arena_scenario_archive);
	REQUIRE(arena_scenario_archive < arena_archive);
	REQUIRE(scanner.find("geometry_file", arena_case) > arena_end);
	/* B-1235: arena.ini carries the load mode's name; both readers of
	 * arena.ini (the scanner and the mod distribution's registrar) take it
	 * from one helper that knows the names and still reads a number. Read
	 * as a number, every name was 0, so a canvas arena from a mod loaded as
	 * a playable one. */
	const std::string distribArena = readTextFile("port/src/net/netdistrib.c");
	REQUIRE_FALSE(distribArena.empty());
	const size_t mode_helper = scanner.find("u8 catalogArenaLoadModeFromIni(const ini_section_t *ini)");
	REQUIRE(mode_helper != std::string::npos);
	REQUIRE(scanner.find("strcmp(mode, \"ARENA_LOADMODE_CANVAS\") == 0", mode_helper) != std::string::npos);
	REQUIRE(scanner.find("strcmp(mode, \"ARENA_LOADMODE_PLAYABLE\") == 0", mode_helper) != std::string::npos);
	const size_t arena_mode_read = scanner.find(
		"e->ext.arena.load_mode = catalogArenaLoadModeFromIni(ini);", arena_case);
	REQUIRE(arena_mode_read != std::string::npos);
	REQUIRE(arena_mode_read < arena_end);
	REQUIRE(distribArena.find("e->ext.arena.load_mode = catalogArenaLoadModeFromIni(ini);") !=
		std::string::npos);
	const char *const numberOnly =
		"e->ext.arena.load_mode = iniGetInt(ini, \"load_mode\", ARENA_LOADMODE_PLAYABLE);";
	REQUIRE(scanner.find(numberOnly) == std::string::npos);
	REQUIRE(distribArena.find(numberOnly) == std::string::npos);

	/* Forge F1: a mod level has no string in the game's language files, so
	 * its name is arena.ini's display_name. Both readers of arena.ini take it
	 * from one helper, the catalog entry carries it, and every arena list
	 * shows it in place of the template's language string (a level saved
	 * from Complex used to list as a second "Complex"). */
	const std::string catalogHeader = readTextFile("port/include/assetcatalog.h");
	const size_t arena_ext = catalogHeader.find("u8  load_mode;");
	REQUIRE(arena_ext != std::string::npos);
	const size_t arena_ext_end = catalogHeader.find("} arena;", arena_ext);
	REQUIRE(arena_ext_end != std::string::npos);
	const size_t arena_ext_name = catalogHeader.find("char display_name[64];", arena_ext);
	REQUIRE(arena_ext_name != std::string::npos);
	REQUIRE(arena_ext_name < arena_ext_end);
	const size_t name_helper = scanner.find(
		"void catalogArenaDisplayNameFromIni(const ini_section_t *ini, char *out, size_t out_size)");
	REQUIRE(name_helper != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"display_name\", \"\")", name_helper) != std::string::npos);
	const size_t arena_name_read = scanner.find(
		"catalogArenaDisplayNameFromIni(ini, e->ext.arena.display_name,", arena_case);
	REQUIRE(arena_name_read != std::string::npos);
	REQUIRE(arena_name_read < arena_end);
	REQUIRE(distribArena.find("catalogArenaDisplayNameFromIni(ini, e->ext.arena.display_name,") !=
		std::string::npos);

	const std::string roomMenu = readTextFile("port/fast3d/pdgui_menu_room.cpp");
	const std::string setupMenu = readTextFile("port/fast3d/pdgui_menu_mpsetup.cpp");
	const std::string mainMenu = readTextFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	const size_t entry_name = roomMenu.find("const char *arenaEntryName(const asset_entry_t *e)");
	REQUIRE(entry_name != std::string::npos);
	const size_t entry_name_own = roomMenu.find("if (e->ext.arena.display_name[0]) {", entry_name);
	const size_t entry_name_lang = roomMenu.find(
		"return arenaGetName((u16)e->ext.arena.name_langid);", entry_name);
	REQUIRE(entry_name_own != std::string::npos);
	REQUIRE(entry_name_lang != std::string::npos);
	REQUIRE(entry_name_own < entry_name_lang);
	/* The Room's list, the match setup's list, and the Grid's two lists. */
	REQUIRE(roomMenu.find("const char *name = arenaEntryName(e);") != std::string::npos);
	REQUIRE(setupMenu.find("const char *name = arenaEntryName(e);") != std::string::npos);
	REQUIRE(mainMenu.find("const char *name = arenaEntryName(e);") != std::string::npos);
	const size_t grid_collect = mainMenu.find("static void gridArenaCollect(");
	REQUIRE(grid_collect != std::string::npos);
	REQUIRE(mainMenu.find("const char *langText = e->ext.arena.display_name[0]", grid_collect) !=
		std::string::npos);
	/* No list goes straight to the language id any more. */
	const char *const langOnly = "arenaGetName((u16)e->ext.arena.name_langid)";
	REQUIRE(setupMenu.find(langOnly) == std::string::npos);
	REQUIRE(mainMenu.find(langOnly) == std::string::npos);
	REQUIRE(roomMenu.find(langOnly) == entry_name_lang + std::string("return ").size());
	REQUIRE(roomMenu.find(langOnly, entry_name_lang + 8) == std::string::npos);

	const size_t shared = scanner.find("static s32 registerBodyHeadPublicIni(");
	const size_t head_mesh = scanner.find("bodyHeadSourcePrepareMesh(source.mesh_archive", shared);
	const size_t head_bind = scanner.find("bodyHeadSourceBindMesh(&mesh", shared);
	const size_t head_archive = scanner.find("catalogSetPrimary(entry, primary)", shared);
	REQUIRE(shared != std::string::npos);
	REQUIRE(head_mesh != std::string::npos);
	REQUIRE(head_bind != std::string::npos);
	REQUIRE(head_archive != std::string::npos);
	REQUIRE(head_mesh < head_bind);
	REQUIRE(head_bind < head_archive);
	REQUIRE(scanner.find("bodyHeadSourceReadIniBytes") != std::string::npos);

}

TEST_CASE("missing mods-enabled json is treated as clean no-mods state",
          "[social][public_mods][static][c3844]")
{
	const std::string modmgr = readTextFile("port/src/modmgr.c");
	const size_t loader = modmgr.find("static int modmgrLoadModsEnabledJson(void)");
	REQUIRE(loader != std::string::npos);
	REQUIRE(modmgr.find("modmgrLoadEnabledFile(path, g_ModRegistry,", loader) != std::string::npos);
	REQUIRE(modmgr.find("if (modmgrLoadModsEnabledJson() != 0)") != std::string::npos);
	const std::string io = readTextFile("port/src/modmgr_enabled_state.cpp");
	const size_t adapter = io.find("int modmgrLoadEnabledFile(");
	REQUIRE(adapter != std::string::npos);
	const size_t stat_gate = io.find("if (stat(path, &info) != 0)", adapter);
	const size_t missing = io.find("if (errno == ENOENT) return 0;", adapter);
	const size_t file_load = io.find("std::fopen(path, \"rb\")", adapter);
	REQUIRE(stat_gate != std::string::npos);
	REQUIRE(missing != std::string::npos);
	REQUIRE(file_load != std::string::npos);
	REQUIRE(stat_gate < missing);
	REQUIRE(missing < file_load);
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

TEST_CASE("A mod weapon's own mesh never takes the base file slot its manifest names",
          "[social][public_mods][static][b1152]")
{
	/* B-1152, the loader side. A weapon mod saved from a base gun before the
	 * mod author stopped copying `source_filenum_symbol` still carries that
	 * name in its mesh manifest. Honoured, the mod's mesh claimed the base
	 * gun's file slot and the base gun drew the mod's model. The nested
	 * registration treats the name as provenance for any mesh whose owner is
	 * not bundled: it plans without the row's number, drops the number the
	 * manifest resolved to, and says so once in the log. */
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	REQUIRE_FALSE(scanner.empty());
	const size_t prepare = scanner.find("static s32 weaponNestedPrepareMember(");
	REQUIRE(prepare != std::string::npos);
	const size_t prepare_end = scanner.find("static s32 weaponNestedPrepareRecursiveMeshes(", prepare);
	REQUIRE(prepare_end != std::string::npos);
	const size_t flag = scanner.find("s32 owner_bundled, char *err, size_t err_cap)", prepare);
	const size_t own = scanner.find(
		"const s32 own_slot = !owner_bundled && !(existing && existing->bundled);", prepare);
	const size_t plan = scanner.find(
		"own_slot ? -1 : (existing ? existing->source_filenum : -1),", prepare);
	const size_t drop = scanner.find("p->mesh_source.source_symbol_present = 0;", prepare);
	REQUIRE(flag != std::string::npos);
	REQUIRE(own != std::string::npos);
	REQUIRE(plan != std::string::npos);
	REQUIRE(drop != std::string::npos);
	REQUIRE(flag < own);
	REQUIRE(own < plan);
	REQUIRE(plan < drop);
	REQUIRE(drop < prepare_end);
	REQUIRE(scanner.find("keeps a slot of its own: its manifest names file %d", prepare) < prepare_end);
	/* Both callers hand the owner's flag down: the weapon's own members and
	 * the meshes inside its projectile and entity archives. */
	REQUIRE(scanner.find("entry_name, expected_type, bundled, err, err_cap);") != std::string::npos);
	REQUIRE(scanner.find("&pending_capacity, &mesh_edges, weapon_id, bundled, err, err_cap)") !=
		std::string::npos);
	REQUIRE(scanner.find("owner_bundled, err, err_cap);", prepare_end) != std::string::npos);
}

TEST_CASE("The scenario inside a mod level gets a catalog row of its own",
          "[social][public_mods][static][forge-f1]")
{
	/* Forge F1. A base arena's scenario is also a file of its own; a level
	 * saved by the mod author keeps its scenario only inside its .pdarena.
	 * The scanner registers that member as an ASSET_SCENARIO row under the id
	 * the arena names, for an arena outside the base namespace whose scenario
	 * id is its own, and leaves a scenario another source registered alone.
	 * Both ways a typed archive is registered call it: a file in a mod folder
	 * and a member of a packed mod. */
	const std::string scanner = readTextFile("port/src/assetcatalog_scanner.c");
	REQUIRE_FALSE(scanner.empty());
	const size_t fn = scanner.find("static s32 registerArenaEmbeddedScenario(const char *arena_id, const char *mod_id,");
	REQUIRE(fn != std::string::npos);
	const size_t fn_end = scanner.find("static s32 registerTypedPdDescriptorFile(", fn);
	REQUIRE(fn_end != std::string::npos);
	const size_t own = scanner.find(
		"strncmp(owner, \"base:\", 5) == 0 || !catalogIdSharesNamespace(owner, scenario_id)", fn);
	const size_t load = scanner.find("bytes = fsFileLoad(chain, &size);", fn);
	const size_t type = scanner.find("sectionToType(ini.type) != ASSET_SCENARIO", fn);
	const size_t same = scanner.find("strcmp(found_id, scenario_id) != 0", fn);
	const size_t qualify = scanner.find("!qualifyTypedArchiveSourcePaths(&ini, chain)", fn);
	const size_t reg = scanner.find("!registerComponentWithAnimationBatch(&ini, component_dir, mod_id,", fn);
	for (const size_t at : { own, load, type, same, qualify, reg }) {
		REQUIRE(at != std::string::npos);
		REQUIRE(at < fn_end);
	}
	REQUIRE(own < load);
	REQUIRE(load < type);
	REQUIRE(type < same);
	REQUIRE(same < qualify);
	REQUIRE(qualify < reg);
	const size_t provenance = scanner.find("const s32 grid_offer_only = arena->grid_offer_only;", fn);
	const size_t chain_guard = scanner.find("|| !cut) {", own);
	const size_t existing = scanner.find("if (assetCatalogResolve(scenario_id)) {", chain_guard);
	const size_t existing_row = scanner.find("row = assetCatalogGetMutable(scenario_id);", existing);
	const size_t existing_type = scanner.find("if (row && row->type == ASSET_SCENARIO", existing_row);
	const size_t existing_owner = scanner.find("strncmp(row->descriptor_path, chain, len) == 0", existing_type);
	const size_t existing_boundary = scanner.find(
		"row->descriptor_path[len] == ':' && row->descriptor_path[len + 1] == ':'", existing_owner);
	const size_t existing_provenance = scanner.find("row->grid_offer_only = grid_offer_only;", existing_boundary);
	const size_t existing_skip = scanner.find("return 0;", existing_provenance);
	const size_t new_type = scanner.find("if (!row || row->type != ASSET_SCENARIO) {", reg);
	const size_t new_provenance = scanner.find("row->grid_offer_only = grid_offer_only;", new_type);
	for (const size_t at : { provenance, chain_guard, existing, existing_row, existing_type, existing_owner,
			existing_boundary, existing_provenance, existing_skip, new_type, new_provenance }) {
		REQUIRE(at < fn_end);
	}
	REQUIRE(provenance < own);
	REQUIRE(existing_skip < load);
	REQUIRE(new_type < new_provenance);
	REQUIRE(scanner.find("PDARENA.SCENARIO: owner=%s id=%s stagenum=0x%02x source=%s", fn) < fn_end);

	const size_t file_site = scanner.find("if (ini_type == ASSET_ARENA) {", fn_end);
	const size_t pack_site = scanner.find("if (accepted && typed_archive_entry && ini_type == ASSET_ARENA) {", fn_end);
	REQUIRE(file_site != std::string::npos);
	REQUIRE(pack_site != std::string::npos);
	REQUIRE(scanner.find("registerArenaEmbeddedScenario(arena_id, mod_id, batch,", file_site) < pack_site);
	REQUIRE(scanner.find("registerArenaEmbeddedScenario(arena_id, mod_id, batch,", pack_site) != std::string::npos);
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
