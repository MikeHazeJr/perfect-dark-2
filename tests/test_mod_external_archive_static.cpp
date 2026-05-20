/*
 * tests/test_mod_external_archive_static.cpp -- external-format .pdmod pins.
 *
 * Static guards for the first c3809 implementation slice. These tests keep
 * the new archive authoring contract anchored until full fixture coverage is
 * wired through the live mod loader.
 */

#include "catch.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "modarchive.h"
#include "modvfs.h"
}

namespace {

std::string readFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

struct TempArchive {
	std::filesystem::path path;

	~TempArchive() {
		if (!path.empty()) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
	}
};

struct OpenArchive {
	mod_archive_t *archive = nullptr;

	~OpenArchive() {
		if (archive) {
			modArchiveClose(archive);
		}
	}
};

struct MountedArchive {
	std::string modId;
	bool mounted = false;

	~MountedArchive() {
		if (mounted) {
			modVfsUnmount(modId.c_str());
		}
	}
};

TempArchive packArchiveFixture() {
	const std::filesystem::path root =
		"tests/fixtures/modpipe/external-archive-entries";
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-modpipe-fixture-" + std::to_string(stamp) + ".pdmod")
	};

	std::vector<std::pair<std::string, std::filesystem::path>> files;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const std::string rel =
			std::filesystem::relative(entry.path(), root).generic_string();
		files.emplace_back(rel, entry.path());
	}
	std::sort(files.begin(), files.end(),
		[](const auto &a, const auto &b) { return a.first < b.first; });
	REQUIRE_FALSE(files.empty());

	mod_archive_writer_t *writer = modArchiveBegin(out.path.string().c_str());
	REQUIRE(writer != nullptr);
	modArchiveSetComment(writer,
		"{\"id\":\"fixture_external_archive\",\"layout\":\"external\"}");

	for (const auto &file : files) {
		INFO("packing " << file.first);
		if (modArchiveAddFileDisk(writer, file.first.c_str(),
				file.second.string().c_str()) != MODARCHIVE_OK) {
			modArchiveAbort(writer);
			FAIL("modArchiveAddFileDisk failed for " << file.first);
		}
	}

	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
	return out;
}

std::string readMountedText(const char *modId, const char *path) {
	u32 size = 0;
	void *bytes = modVfsResolveAllocFromMount(modId, path, &size);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

}  /* anonymous namespace */

TEST_CASE("external pdmod contract forbids authored .bin payloads",
          "[modding][pdmod][static][c3809]") {
	std::string spec = readFile("context/designs/modding/external-format-pdmod-pipeline.md");
	REQUIRE(spec.find("Authored `.pdmod` payloads must not contain `.bin`") != std::string::npos);
	REQUIRE(spec.find("No `.bin` alternate is valid") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	REQUIRE(modmgr.find("modmgrArchiveFindForbiddenBinPayload") != std::string::npos);
	REQUIRE(modmgr.find("External-format archives cannot contain .bin authoring payloads") != std::string::npos);
	REQUIRE(modmgr.find("modmgrArchiveEntryHasForbiddenBinPayload") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("entryHasForbiddenBinPayload(e->entry_name)") != std::string::npos);
	REQUIRE(packer.find("validateNoForbiddenBinsRecurse(srcFolder") != std::string::npos);
	REQUIRE(packer.find("Authored .bin files are not allowed in external .pdmod archives") != std::string::npos);
}

TEST_CASE("folder to pdmod packer validates layout and generates INI templates",
          "[modding][pdmod][static][c3809]") {
	std::string header = readFile("port/include/modpack_pdmod.h");
	REQUIRE(header.find("MODPACK_PDMOD_ERR_LAYOUT") != std::string::npos);
	REQUIRE(header.find("MODPACK_PDMOD_ERR_TEMPLATE") != std::string::npos);
	REQUIRE(header.find("modpackPdmodLastError") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("validateExternalFolderLayout") != std::string::npos);
	REQUIRE(packer.find("r = validateExternalFolderLayout(src_folder, out_path)") != std::string::npos);
	REQUIRE(packer.find("modArchiveBegin(out_path)") != std::string::npos);
	REQUIRE(packer.find("processCanonicalFamily(srcFolder") != std::string::npos);
	REQUIRE(packer.find("writeTemplateIfMissing") != std::string::npos);
	REQUIRE(packer.find("modiniTemplateForKind(kind)") != std::string::npos);
	REQUIRE(packer.find("pads.ini - generated external map pad/spawn template") != std::string::npos);
	REQUIRE(packer.find("setup.ini - generated external map setup template") != std::string::npos);
	REQUIRE(packer.find("props.ini - generated external scenario props template") != std::string::npos);
	REQUIRE(packer.find("objectives.ini - generated external scenario objectives template") != std::string::npos);
	REQUIRE(packer.find("weapons\", \"weapon.ini\", \"weapon\"") != std::string::npos);
	REQUIRE(packer.find("characters/heads\", \"head.ini\", \"head\"") != std::string::npos);
	REQUIRE(packer.find("maps\", \"arena.ini\", \"arena\"") != std::string::npos);
	REQUIRE(packer.find("audio/music\", \"music.ini\", \"music\"") != std::string::npos);
	REQUIRE(packer.find("animations/character\", \"animation.ini\", \"animation\"") != std::string::npos);
	REQUIRE(packer.find("references missing source file") != std::string::npos);
	REQUIRE(packer.find("uses unsafe source path") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("voice.ini - external voice metadata") != std::string::npos);
	REQUIRE(scanner.find("audio_category = voice") != std::string::npos);
	REQUIRE(scanner.find("music.ini - external music metadata") != std::string::npos);
	REQUIRE(scanner.find("audio_category = music") != std::string::npos);
	REQUIRE(scanner.find("file_path = track.ogg") != std::string::npos);

	std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(hub.find("modpackPdmodLastError()") != std::string::npos);
	REQUIRE(hub.find("MODPACK_PDMOD_ERR_LAYOUT") != std::string::npos);
	REQUIRE(hub.find("MODPACK_PDMOD_ERR_TEMPLATE") != std::string::npos);
	REQUIRE(hub.find("standard files + INI/TSV, no .bin") != std::string::npos);
}

TEST_CASE("archive mods scan INI descriptors through the shared catalog scanner",
          "[modding][pdmod][static][c3809]") {
	std::string header = readFile("port/include/assetcatalog_scanner.h");
	REQUIRE(header.find("iniParseBuffer") != std::string::npos);
	REQUIRE(header.find("iniWriteBuffer") != std::string::npos);
	REQUIRE(header.find("iniWriteFile") != std::string::npos);
	REQUIRE(header.find("modiniTemplateForKind") != std::string::npos);
	REQUIRE(header.find("assetCatalogScanExternalLayoutFolder") != std::string::npos);
	REQUIRE(header.find("assetCatalogScanComponentsFromArchive") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("iniParseBuffer") != std::string::npos);
	REQUIRE(scanner.find("iniWriteBuffer") != std::string::npos);
	REQUIRE(scanner.find("if (out->type[0] == '\\0')") != std::string::npos);
	REQUIRE(scanner.find("modArchiveExtractAlloc") != std::string::npos);
	REQUIRE(scanner.find("archiveIniIsDescriptor") != std::string::npos);
	REQUIRE(scanner.find("qualifyArchiveIniPaths") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"catalog_id\", iniGet(ini, \"id\"") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	const auto mount = modmgr.find("modVfsMount(mod->id, mod->archive_handle)");
	const auto scan = modmgr.find("assetCatalogScanComponentsFromArchive(mod->id, mod->archive_handle)");
	REQUIRE(mount != std::string::npos);
	REQUIRE(scan != std::string::npos);
	REQUIRE(mount < scan);
}

TEST_CASE("folder and archive fixtures use the same external descriptor layout",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("assetCatalogScanExternalLayoutFolder") != std::string::npos);
	REQUIRE(scanner.find("scanExternalDescriptorPath(mod_dir, \"maps\"") != std::string::npos);
	REQUIRE(scanner.find("scanExternalDescriptorPath(mod_dir, \"characters/heads\"") != std::string::npos);
	REQUIRE(scanner.find("scanExternalDescriptorPath(mod_dir, \"animations/weapon\"") != std::string::npos);
	REQUIRE(scanner.find("scanExternalDescriptorPath(mod_dir, \"animations/character\"") != std::string::npos);
	REQUIRE(scanner.find("registerComponentIniFile(component_dir, ini_path") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	const auto modjson = modmgr.find("modmgrRegisterModJsonContent(mod)");
	const auto folderScan = modmgr.find("assetCatalogScanExternalLayoutFolder(mod->id, mod->dirpath)");
	REQUIRE(modjson != std::string::npos);
	REQUIRE(folderScan != std::string::npos);
	REQUIRE(modjson < folderScan);

	std::string folderManifest = readFile("tests/fixtures/modpipe/external-folder/mod.json");
	std::string folderArena = readFile("tests/fixtures/modpipe/external-folder/maps/tri_arena/arena.ini");
	std::string folderObj = readFile("tests/fixtures/modpipe/external-folder/maps/tri_arena/geometry.obj");
	std::string folderHead = readFile("tests/fixtures/modpipe/external-folder/characters/heads/tri_head/head.ini");
	std::string folderGltf = readFile("tests/fixtures/modpipe/external-folder/characters/heads/tri_head/model.gltf");
	std::string folderAnim = readFile("tests/fixtures/modpipe/external-folder/animations/weapon/idle/animation.ini");
	std::string folderCharacterAnim = readFile("tests/fixtures/modpipe/external-folder/animations/character/idle/animation.ini");
	std::string folderCharacterAnimGltf = readFile("tests/fixtures/modpipe/external-folder/animations/character/idle/animation.gltf");
	std::string folderSkeletalAnim = readFile("tests/fixtures/modpipe/external-folder/animations/character/skeletal/animation.ini");
	std::string folderSkeletalGltf = readFile("tests/fixtures/modpipe/external-folder/animations/character/skeletal/animation.gltf");

	REQUIRE(folderManifest.find("\"id\": \"fixture_external_folder\"") != std::string::npos);
	REQUIRE(folderArena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE(folderArena.find("pads_file = pads.ini") != std::string::npos);
	REQUIRE(folderObj.find("f 1 2 3") != std::string::npos);
	REQUIRE(folderHead.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(folderGltf.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(folderGltf.find("\"meshes\"") != std::string::npos);
	REQUIRE(folderAnim.find("animation_file = animation.gltf") != std::string::npos);
	REQUIRE(folderAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(folderAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(folderCharacterAnim.find("catalog_id = fixture:character_idle") != std::string::npos);
	REQUIRE(folderCharacterAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(folderCharacterAnim.find("target_body = fixture:tri_body") != std::string::npos);
	REQUIRE(folderCharacterAnimGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(folderSkeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(folderSkeletalGltf.find("\"channels\"") != std::string::npos);
	REQUIRE(folderSkeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	std::string archiveManifest = readFile("tests/fixtures/modpipe/external-archive-entries/mod.json");
	std::string archiveArena = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/arena.ini");
	std::string archiveObj = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/geometry.obj");
	std::string archivePads = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/pads.ini");
	std::string archiveSetup = readFile("tests/fixtures/modpipe/external-archive-entries/maps/tri_arena/setup.ini");
	std::string archiveHead = readFile("tests/fixtures/modpipe/external-archive-entries/characters/heads/tri_head/head.ini");
	std::string archiveGltf = readFile("tests/fixtures/modpipe/external-archive-entries/characters/heads/tri_head/model.gltf");
	std::string archiveWeaponAnim = readFile("tests/fixtures/modpipe/external-archive-entries/animations/weapon/idle/animation.ini");
	std::string archiveCharacterAnim = readFile("tests/fixtures/modpipe/external-archive-entries/animations/character/idle/animation.ini");
	std::string archiveSkeletalAnim = readFile("tests/fixtures/modpipe/external-archive-entries/animations/character/skeletal/animation.ini");
	std::string archiveSkeletalGltf = readFile("tests/fixtures/modpipe/external-archive-entries/animations/character/skeletal/animation.gltf");

	REQUIRE(archiveManifest.find("\"id\": \"fixture_external_archive\"") != std::string::npos);
	REQUIRE(archiveArena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE(archiveObj.find("f 1 2 3") != std::string::npos);
	REQUIRE(archivePads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(archiveSetup.find("props = none") != std::string::npos);
	REQUIRE(archiveHead.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(archiveGltf.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(archiveWeaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(archiveWeaponAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(archiveCharacterAnim.find("catalog_id = fixture:character_idle") != std::string::npos);
	REQUIRE(archiveCharacterAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(archiveSkeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(archiveSkeletalGltf.find("\"channels\"") != std::string::npos);
	REQUIRE(archiveSkeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	REQUIRE(folderManifest.find(".bin") == std::string::npos);
	REQUIRE(folderArena.find(".bin") == std::string::npos);
	REQUIRE(folderObj.find(".bin") == std::string::npos);
	REQUIRE(folderHead.find(".bin") == std::string::npos);
	REQUIRE(folderGltf.find(".bin") == std::string::npos);
	REQUIRE(folderAnim.find(".bin") == std::string::npos);
	REQUIRE(folderCharacterAnim.find(".bin") == std::string::npos);
	REQUIRE(folderCharacterAnimGltf.find(".bin") == std::string::npos);
	REQUIRE(folderSkeletalAnim.find(".bin") == std::string::npos);
	REQUIRE(folderSkeletalGltf.find(".bin") == std::string::npos);
	REQUIRE(archiveManifest.find(".bin") == std::string::npos);
	REQUIRE(archiveArena.find(".bin") == std::string::npos);
	REQUIRE(archiveObj.find(".bin") == std::string::npos);
	REQUIRE(archivePads.find(".bin") == std::string::npos);
	REQUIRE(archiveSetup.find(".bin") == std::string::npos);
	REQUIRE(archiveHead.find(".bin") == std::string::npos);
	REQUIRE(archiveGltf.find(".bin") == std::string::npos);
	REQUIRE(archiveWeaponAnim.find(".bin") == std::string::npos);
	REQUIRE(archiveCharacterAnim.find(".bin") == std::string::npos);
	REQUIRE(archiveSkeletalAnim.find(".bin") == std::string::npos);
	REQUIRE(archiveSkeletalGltf.find(".bin") == std::string::npos);
}

TEST_CASE("external archive scanner recognizes canonical descriptor families",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("\"weapon.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"head.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"body.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"arena.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"scenario.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"sound.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"ui.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"font.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"lang.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"animation.ini\"") != std::string::npos);
}

TEST_CASE("legacy pd asset walkers remain registered during external pdmod migration",
          "[modding][pdmod][static][c3809]") {
	std::string common_h = readFile("port/include/loader_walker_common.h");
	std::string common = readFile("port/src/loader_walker_common.c");
	std::string walker = readFile("port/src/loader_walker.c");

	REQUIRE(common_h.find("auto-detecting plain JSON vs ZIP compound") != std::string::npos);
	REQUIRE(common.find("if (strcmp(name + nlen - ext_len, desc->extension) != 0) continue;") != std::string::npos);
	REQUIRE(common.find("bytes[0] == 'P' && bytes[1] == 'K'") != std::string::npos);
	REQUIRE(common.find("manifest.json") != std::string::npos);

	const char *scanCalls[] = {
		"loaderWalkerScanMeshes(data_root, &kr)",
		"loaderWalkerScanAnimations(data_root, &kr)",
		"loaderWalkerScanWeapons(data_root, &kr)",
		"loaderWalkerScanHeads(data_root, &kr)",
		"loaderWalkerScanBodies(data_root, &kr)",
		"loaderWalkerScanScenarios(data_root, &kr)",
		"loaderWalkerScanArenas(data_root, &kr)",
		"loaderWalkerScanSfx(data_root, &kr)",
		"loaderWalkerScanVoices(data_root, &kr)",
		"loaderWalkerScanSongs(data_root, &kr)",
		"loaderWalkerScanUi(data_root, &kr)",
		"loaderWalkerScanFonts(data_root, &kr)",
		"loaderWalkerScanLangs(data_root, &kr)",
	};
	for (const char *call : scanCalls) {
		INFO(call);
		REQUIRE(walker.find(call) != std::string::npos);
	}

	const std::pair<const char *, const char *> legacyWalkers[] = {
		{ "port/src/loader_walker_weapon.c",   "\"weapon\", \"weapons\", \".pdwpn\"" },
		{ "port/src/loader_walker_head.c",     "\"head\", \"heads\", \".pdhead\"" },
		{ "port/src/loader_walker_body.c",     "\"body\", \"bodies\", \".pdbody\"" },
		{ "port/src/loader_walker_arena.c",    "\"arena\", \"arenas\", \".pdarena\"" },
		{ "port/src/loader_walker_mesh.c",     "\"mesh\", \"meshes\", \".pdmesh\"" },
		{ "port/src/loader_walker_anim.c",     "\"animation\", \"animations\", \".pdanim\"" },
		{ "port/src/loader_walker_sfx.c",      "\"sfx\", \"audio/sfx\", \".pdsfx\"" },
		{ "port/src/loader_walker_voice.c",    "\"voice\", \"audio/voice\", \".pdvoice\"" },
		{ "port/src/loader_walker_song.c",     "\"song\", \"audio/music\", \".pdsong\"" },
		{ "port/src/loader_walker_ui.c",       "\"ui\", \"ui\", \".pdui\"" },
		{ "port/src/loader_walker_font.c",     "\"font\", \"fonts\", \".pdfont\"" },
		{ "port/src/loader_walker_lang.c",     "\"lang\", \"lang\", \".pdlang\"" },
		{ "port/src/loader_walker_scenario.c", "\"scenario\", \"scenarios\", \".pdscenario\"" },
	};
	for (const auto &pin : legacyWalkers) {
		INFO(pin.first);
		std::string src = readFile(pin.first);
		REQUIRE(src.find(pin.second) != std::string::npos);
		REQUIRE(src.find("loaderWalkerScanKind(tier_dir, &desc, s_register, out)") != std::string::npos);
	}
}

TEST_CASE("public mods share folder mods as validated pdmod archives",
          "[modding][pdmod][static][c3809]") {
	std::string share = readFile("port/src/social_share.c");
	std::string public_test = readFile("tests/test_public_mods_static.cpp");
	std::string cmake = readFile("CMakeLists.txt");

	REQUIRE(cmake.find("tests/test_public_mods_static.cpp") != std::string::npos);
	REQUIRE(share.find("#include \"modpack_pdmod.h\"") != std::string::npos);
	REQUIRE(share.find("static s32 packFolderModForPublicShare") != std::string::npos);
	REQUIRE(share.find("modpackPdmodFromFolder(mod->dirpath, out_path)") != std::string::npos);
	REQUIRE(share.find("modpackPdmodLastError()") != std::string::npos);
	REQUIRE(share.find("fileTransferSendFile(from_handle, pdmod_path)") != std::string::npos);
	REQUIRE(share.find("mod manifest offer") == std::string::npos);
	REQUIRE(public_test.find("modpackPdmodFromFolder(mod->dirpath, out_path)") != std::string::npos);
}

TEST_CASE("metadata family INI support covers grouped templates and deps",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("modiniTemplateForKind") != std::string::npos);
	REQUIRE(scanner.find("weapon.ini - external weapon metadata") != std::string::npos);
	REQUIRE(scanner.find("head.ini - external multiplayer head metadata") != std::string::npos);
	REQUIRE(scanner.find("body.ini - external multiplayer body metadata") != std::string::npos);
	REQUIRE(scanner.find("arena.ini - external map/arena metadata") != std::string::npos);
	REQUIRE(scanner.find("animation.ini - external animation metadata") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(owner_id, dep, is_bundled)") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled)") != std::string::npos);
	REQUIRE(scanner.find("\"animation_file\"") != std::string::npos);
}

TEST_CASE("external audio descriptors use standard files through VFS-capable loaders",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("audio_category = sfx") != std::string::npos);
	REQUIRE(scanner.find("Music authoring supports OGG, MP3, or WAV") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"audio_category\"") != std::string::npos);

	std::string audio = readFile("port/src/audio.c");
	REQUIRE(audio.find("fsFileLoad(path, &fileSize)") != std::string::npos);
	REQUIRE(audio.find("SDL_LoadWAV_RW") != std::string::npos);

	std::string modmusic = readFile("port/src/modmusic.c");
	REQUIRE(modmusic.find("modVfsCanResolve(file_path)") != std::string::npos);
	REQUIRE(modmusic.find("fsFileLoad(path, &fileSize)") != std::string::npos);
	REQUIRE(modmusic.find("stb_vorbis_decode_memory") != std::string::npos);

	std::string vorbis = readFile("port/include/external/stb_vorbis.h");
	REQUIRE(vorbis.find("stb_vorbis_decode_memory") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("distribParseAudioCategoryValue") != std::string::npos);
	REQUIRE(distrib.find("\"sound.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"voice.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"music.ini\"") != std::string::npos);
}

TEST_CASE("external UI font and language descriptors use standard files",
          "[modding][pdmod][static][c3809]") {
	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("ui.ini - external UI texture metadata") != std::string::npos);
	REQUIRE(scanner.find("UI texture authoring supports PNG or TGA") != std::string::npos);
	REQUIRE(scanner.find("font.ini - external UI font metadata") != std::string::npos);
	REQUIRE(scanner.find("Font authoring supports TTF or OTF") != std::string::npos);
	REQUIRE(scanner.find("lang.ini - external UTF-8 language-bank metadata") != std::string::npos);
	REQUIRE(scanner.find("strings_file = strings.tsv") != std::string::npos);
	REQUIRE(scanner.find("\"strings_tsv\"") != std::string::npos);

	std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("char strings_file[128]") != std::string::npos);

	std::string theme = readFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(theme.find("stbi_load_from_memory") != std::string::npos);
	REQUIRE(theme.find("GL_MAX_TEXTURE_SIZE") != std::string::npos);
	REQUIRE(theme.find("pdguiFontMgrLoadFont(entry->id, path, 24.0f)") != std::string::npos);
	REQUIRE(theme.find("assetCatalogIterateByType(ASSET_UI, s_applyCatalogUiAsset") != std::string::npos);

	std::string lang = readFile("port/src/langmanifest.c");
	REQUIRE(lang.find("langManifestLoadExternalTsv") != std::string::npos);
	REQUIRE(lang.find("fsFileLoad(path, &raw_size)") != std::string::npos);
	REQUIRE(lang.find("g_LangBanks[bank] = bank_data") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("\"ui.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"font.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"lang.ini\"") != std::string::npos);
	REQUIRE(distrib.find("e->ext.lang.strings_file") != std::string::npos);
}

TEST_CASE("external models maps and animations compile from standard sources",
          "[modding][pdmod][static][c3809]") {
	std::string compiler_h = readFile("port/include/modasset_compiler.h");
	REQUIRE(compiler_h.find("MODASSET_COMPILER_VERSION") != std::string::npos);
	REQUIRE(compiler_h.find("modasset_compiled_result_t") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerIsExternalSource") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerCompileReadable") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerEnsureCache") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildObjColmesh") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildModeldef") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerFreeModeldef") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildAnimationClip") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerFreeAnimationClip") != std::string::npos);
	REQUIRE(compiler_h.find(".bin suffix") != std::string::npos);

	std::string compiler = readFile("port/src/modasset_compiler.c");
	REQUIRE(compiler.find("endsWithNoCase(path, \".gltf\")") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".glb\")") != std::string::npos);
	REQUIRE(compiler.find("endsWithNoCase(path, \".obj\")") != std::string::npos);
	REQUIRE(compiler.find("parseObjSource") != std::string::npos);
	REQUIRE(compiler.find("parseObjFaceLine") != std::string::npos);
	REQUIRE(compiler.find("writeObjMeshJson") != std::string::npos);
	REQUIRE(compiler.find("validateObjSource") != std::string::npos);
	REQUIRE(compiler.find("validateGltfSource") != std::string::npos);
	REQUIRE(compiler.find("validateGlbSource") != std::string::npos);
	REQUIRE(compiler.find("parseGltfLikeMeshSource") != std::string::npos);
	REQUIRE(compiler.find("gltf_external_binary_buffers_are_not_allowed") != std::string::npos);
	REQUIRE(compiler.find("$S/mod-cache") != std::string::npos);
	REQUIRE(compiler.find(".pdmc") != std::string::npos);
	REQUIRE(compiler.find(".pdmesh.json") != std::string::npos);
	REQUIRE(compiler.find(".pdmodel.json") != std::string::npos);
	REQUIRE(compiler.find(".pdanimation.json") != std::string::npos);
	REQUIRE(compiler.find("source_sha256") != std::string::npos);
	REQUIRE(compiler.find("runtime_boundary") != std::string::npos);
	REQUIRE(compiler.find("runtime_payload") != std::string::npos);
	REQUIRE(compiler.find("readable-generated-cache") != std::string::npos);
	REQUIRE(compiler.find("cache_hit") != std::string::npos);
	REQUIRE(compiler.find("parseGltfLikeAnimationClip") != std::string::npos);
	REQUIRE(compiler.find("gltf_skeletal_channel_pack") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_S32_TRANSLATE") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_F32_ROTATE") != std::string::npos);
	REQUIRE(compiler.find("ANIMFIELD_F32_SCALE") != std::string::npos);
	REQUIRE(compiler.find("gltf_animation_weights_not_supported") != std::string::npos);
	REQUIRE(compiler.find("gltf_animation_channels_not_supported_yet") == std::string::npos);
	REQUIRE(compiler.find("runtime_boundary\\\": \\\"animtableentry") != std::string::npos);
	REQUIRE(compiler.find("meshAddTriangle(out_mesh") != std::string::npos);
	REQUIRE(compiler.find("buildGeneratedModeldefFromMesh") != std::string::npos);
	REQUIRE(compiler.find("MODELNODETYPE_POSITION") != std::string::npos);
	REQUIRE(compiler.find("MODELNODETYPE_DL") != std::string::npos);
	REQUIRE(compiler.find("gSPMatrix(gdl++") != std::string::npos);
	REQUIRE(compiler.find("gSPVertex(gdl++") != std::string::npos);
	REQUIRE(compiler.find("gSP1Triangle(gdl++") != std::string::npos);

	std::string mesh_h = readFile("src/include/lib/meshcollision.h");
	REQUIRE(mesh_h.find("void meshInit(struct colmesh *mesh)") != std::string::npos);
	REQUIRE(mesh_h.find("bool meshAddTriangle(struct colmesh *mesh") != std::string::npos);

	std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("#include \"modasset_compiler.h\"") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerIsExternalSource(source_path)") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerCompileReadable(entry") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerBuildModeldef(entry") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerBuildObjColmesh(source_path, mesh)") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerBuildAnimationClip(entry") != std::string::npos);
	REQUIRE(load.find("ASSET_PAYLOAD_COLMESH") != std::string::npos);
	REQUIRE(load.find("ASSET_PAYLOAD_ANIMATION_CLIP") != std::string::npos);
	REQUIRE(load.find("catalogGetLoadedColmesh") != std::string::npos);
	REQUIRE(load.find("catalogGetLoadedAnimationClip") != std::string::npos);
	REQUIRE(load.find("activated external %s modeldef") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerFreeModeldef") != std::string::npos);
	REQUIRE(load.find("modAssetCompilerFreeAnimationClip") != std::string::npos);
	REQUIRE(load.find("external %s source ready via private cache") != std::string::npos);

	std::string mod = readFile("port/src/mod.c");
	REQUIRE(mod.find("modAnimationLoadCatalogClip") != std::string::npos);
	REQUIRE(mod.find("catalogGetLoadedAnimationClip(entry->id") != std::string::npos);
	REQUIRE(mod.find("External animation %04x failed to compile") != std::string::npos);

	std::string catalog_h = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog_h.find("ASSET_PAYLOAD_COLMESH") != std::string::npos);
	REQUIRE(catalog_h.find("ASSET_PAYLOAD_ANIMATION_CLIP") != std::string::npos);

	std::string lv = readFile("src/game/lv.c");
	REQUIRE(lv.find("lvAddLoadedCatalogColmeshes") != std::string::npos);
	REQUIRE(lv.find("meshWorldAddMesh(mesh, NULL)") != std::string::npos);

	std::string resolve = readFile("port/src/assetcatalog_resolve.c");
	REQUIRE(resolve.find("assetCatalogIterateByType(ASSET_ARENA, findMapCb, &ctx)") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scenario.ini - external scenario metadata") != std::string::npos);
	REQUIRE(scanner.find("rooms_file = rooms.obj") != std::string::npos);
	REQUIRE(scanner.find("\"rooms_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"rooms_file\"") != std::string::npos);
	REQUIRE(scanner.find("e->source_animnum = e->ext.anim.anim_id") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("\"head.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"body.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"arena.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"scenario.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"animation.ini\"") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_ANIMATION:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_GAMEMODE:") != std::string::npos);
}

TEST_CASE("packed external pdmod fixture resolves canonical assets through production VFS",
          "[modding][pdmod][static][c3809]") {
	TempArchive temp = packArchiveFixture();
	OpenArchive opened;
	opened.archive = modArchiveOpen(temp.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	u32 manifestSize = 0;
	char *manifest = modArchiveReadManifest(opened.archive, &manifestSize);
	REQUIRE(manifest != nullptr);
	std::string manifestText(manifest, manifestSize);
	free(manifest);
	REQUIRE(manifestText.find("\"id\": \"fixture_external_archive\"") != std::string::npos);

	const char *requiredEntries[] = {
		"mod.json",
		"maps/tri_arena/arena.ini",
		"maps/tri_arena/geometry.obj",
		"maps/tri_arena/pads.ini",
		"maps/tri_arena/setup.ini",
		"characters/heads/tri_head/head.ini",
		"characters/heads/tri_head/model.gltf",
		"animations/weapon/idle/animation.ini",
		"animations/weapon/idle/animation.gltf",
		"animations/character/idle/animation.ini",
		"animations/character/idle/animation.gltf",
		"animations/character/skeletal/animation.ini",
		"animations/character/skeletal/animation.gltf",
	};

	for (const char *entry : requiredEntries) {
		INFO(entry);
		REQUIRE(modArchiveFindEntry(opened.archive, entry) >= 0);
	}

	for (s32 i = 0; i < modArchiveGetEntryCount(opened.archive); i++) {
		const char *entry = modArchiveGetEntryName(opened.archive, i);
		REQUIRE(entry != nullptr);
		REQUIRE(std::string(entry).find(".bin") == std::string::npos);
	}

	modVfsInit();
	MountedArchive mount{ "fixture_external_archive", false };
	REQUIRE(modVfsMount(mount.modId.c_str(), opened.archive) == 1);
	mount.mounted = true;

	for (const char *entry : requiredEntries) {
		INFO(entry);
		REQUIRE(modVfsCanResolve(entry) == 1);
		REQUIRE(modVfsGetSize(entry) > 0);
	}

	const std::string arena =
		readMountedText(mount.modId.c_str(), "maps/tri_arena/arena.ini");
	const std::string geometry =
		readMountedText(mount.modId.c_str(), "maps/tri_arena/geometry.obj");
	const std::string head =
		readMountedText(mount.modId.c_str(), "characters/heads/tri_head/head.ini");
	const std::string model =
		readMountedText(mount.modId.c_str(), "characters/heads/tri_head/model.gltf");
	const std::string weaponAnim =
		readMountedText(mount.modId.c_str(), "animations/weapon/idle/animation.ini");
	const std::string characterAnim =
		readMountedText(mount.modId.c_str(), "animations/character/idle/animation.ini");
	const std::string skeletalAnim =
		readMountedText(mount.modId.c_str(), "animations/character/skeletal/animation.ini");
	const std::string skeletalGltf =
		readMountedText(mount.modId.c_str(), "animations/character/skeletal/animation.gltf");

	REQUIRE(arena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE(geometry.find("f 1 2 3") != std::string::npos);
	REQUIRE(head.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(model.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(weaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(characterAnim.find("catalog_id = fixture:character_idle") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	mod_vfs_stats_t stats;
	modVfsGetStats(&stats);
	REQUIRE(stats.mounts_active == 1);
	REQUIRE(stats.entries_resident >= 6);
}
