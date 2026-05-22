/*
 * tests/test_mod_external_archive_static.cpp -- external-format content pins.
 *
 * Static guards for the c3809 implementation slices. Typed .pdxxx files are
 * the content-unit surface; .pdmod is the transport wrapper used for sharing
 * and online-required delivery.
 */

#include "catch.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "constants.h"
#include "modarchive.h"
#include "modvfs.h"
#include "weapon_graph_archive.h"
#include "weapon_graph_runtime.h"
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

TempArchive writeArchiveEntries(const std::string &stem,
                                const std::vector<std::pair<std::string, std::string>> &entries) {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-" + stem + "-" + std::to_string(stamp) + ".zip")
	};

	mod_archive_writer_t *writer = modArchiveBegin(out.path.string().c_str());
	REQUIRE(writer != nullptr);
	for (const auto &entry : entries) {
		INFO("packing " << entry.first);
		if (modArchiveAddFileMem(writer, entry.first.c_str(),
				entry.second.data(), static_cast<u32>(entry.second.size())) != MODARCHIVE_OK) {
			modArchiveAbort(writer);
			FAIL("modArchiveAddFileMem failed for " << entry.first);
		}
	}
	REQUIRE(modArchiveFinish(writer) == MODARCHIVE_OK);
	return out;
}

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
		"tests/fixtures/modpipe/pdxxx-content";
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
		"{\"id\":\"fixture_pdxxx_content\",\"layout\":\"pdxxx-transport\"}");

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

std::string readArchiveEntryText(const char *archivePath, const char *entryPath) {
	OpenArchive opened;
	opened.archive = modArchiveOpen(archivePath);
	REQUIRE(opened.archive != nullptr);
	s32 idx = modArchiveFindEntry(opened.archive, entryPath);
	REQUIRE(idx >= 0);
	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(opened.archive, idx, &size);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

std::string readArchiveEntryText(mod_archive_t *archive, const char *entryPath) {
	REQUIRE(archive != nullptr);
	s32 idx = modArchiveFindEntry(archive, entryPath);
	REQUIRE(idx >= 0);
	u32 size = 0;
	void *bytes = modArchiveExtractAlloc(archive, idx, &size);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

struct ExpectedPdxxxArchive {
	const char *assetType;
	const char *extension;
	const char *relPath;
	std::vector<const char *> requiredEntries;
};

std::string trimCopy(const std::string &value) {
	const char *ws = " \t\r\n";
	const size_t first = value.find_first_not_of(ws);
	if (first == std::string::npos) {
		return "";
	}
	const size_t last = value.find_last_not_of(ws);
	return value.substr(first, last - first + 1);
}

bool startsWith(const std::string &value, const char *prefix) {
	return value.rfind(prefix, 0) == 0;
}

bool archiveHasEntry(mod_archive_t *archive, const std::string &entryPath) {
	return archive && modArchiveFindEntry(archive, entryPath.c_str()) >= 0;
}

std::string entryRelativePath(const std::string &baseEntry,
                              const std::string &relative) {
	const size_t slash = baseEntry.find_last_of('/');
	if (slash == std::string::npos) {
		return relative;
	}
	return baseEntry.substr(0, slash + 1) + relative;
}

void requireIniPathRefsResolve(mod_archive_t *archive,
                               const char *archiveRel,
                               const char *descriptorEntry,
                               const std::vector<const char *> &keys) {
	const std::string descriptor = readArchiveEntryText(archive, descriptorEntry);
	std::istringstream in(descriptor);
	std::string line;
	while (std::getline(in, line)) {
		std::string text = trimCopy(line);
		if (text.empty() || text[0] == ';' || text[0] == '#') {
			continue;
		}
		const size_t eq = text.find('=');
		if (eq == std::string::npos) {
			continue;
		}
		const std::string key = trimCopy(text.substr(0, eq));
		const std::string value = trimCopy(text.substr(eq + 1));
		if (value.empty()) {
			continue;
		}
		for (const char *expectedKey : keys) {
			if (key == expectedKey) {
				INFO(std::string(archiveRel) + "::" + descriptorEntry
					+ " -> " + key + " = " + value);
				REQUIRE(value.find("..") == std::string::npos);
				REQUIRE(value.find(':') == std::string::npos);
				REQUIRE(archiveHasEntry(archive, value));
			}
		}
	}
}

void requireGltfUrisResolve(mod_archive_t *archive,
                            const char *archiveRel,
                            const char *gltfEntry) {
	const std::string gltf = readArchiveEntryText(archive, gltfEntry);
	const std::regex uriPattern("\"uri\"\\s*:\\s*\"([^\"]+)\"");
	for (auto it = std::sregex_iterator(gltf.begin(), gltf.end(), uriPattern);
			it != std::sregex_iterator(); ++it) {
		const std::string uri = (*it)[1].str();
		INFO(std::string(archiveRel) + "::" + gltfEntry + " uri=" + uri);
		if (startsWith(uri, "data:")) {
			continue;
		}
		REQUIRE(uri.find("..") == std::string::npos);
		REQUIRE(uri.find(':') == std::string::npos);
		REQUIRE((archiveHasEntry(archive, uri) ||
			archiveHasEntry(archive, entryRelativePath(gltfEntry, uri))));
	}
}

void requireObjRefsResolve(mod_archive_t *archive,
                           const char *archiveRel,
                           const char *objEntry) {
	const std::string obj = readArchiveEntryText(archive, objEntry);
	std::istringstream in(obj);
	std::string line;
	while (std::getline(in, line)) {
		std::string text = trimCopy(line);
		if (!startsWith(text, "mtllib ")) {
			continue;
		}
		const std::string mtl = trimCopy(text.substr(strlen("mtllib ")));
		INFO(std::string(archiveRel) + "::" + objEntry + " mtllib=" + mtl);
		REQUIRE(mtl.find("..") == std::string::npos);
		REQUIRE(mtl.find(':') == std::string::npos);
		REQUIRE(archiveHasEntry(archive, mtl));

		const std::string mtlText = readArchiveEntryText(archive, mtl.c_str());
		std::istringstream mtlIn(mtlText);
		std::string mtlLine;
		while (std::getline(mtlIn, mtlLine)) {
			std::string mtlTrimmed = trimCopy(mtlLine);
			if (!startsWith(mtlTrimmed, "map_")) {
				continue;
			}
			const size_t space = mtlTrimmed.find(' ');
			REQUIRE(space != std::string::npos);
			const std::string texture = trimCopy(mtlTrimmed.substr(space + 1));
			INFO(std::string(archiveRel) + "::" + mtl + " texture=" + texture);
			REQUIRE(texture.find("..") == std::string::npos);
			REQUIRE(texture.find(':') == std::string::npos);
			REQUIRE(archiveHasEntry(archive, texture));
		}
	}
}

std::string readVfsText(const char *path) {
	u32 size = 0;
	void *bytes = modVfsResolveAnyAlloc(path, &size, nullptr, 0);
	REQUIRE(bytes != nullptr);
	std::string text(static_cast<const char *>(bytes), size);
	free(bytes);
	return text;
}

}  /* anonymous namespace */

TEST_CASE("external pdmod contract forbids authored .bin payloads",
          "[modding][pdmod][static][c3809]") {
	std::string spec = readFile("context/designs/modding/external-format-pdmod-pipeline.md");
	REQUIRE(spec.find("Authored mod content and `.pdmod` transport payloads must not contain `.bin`") != std::string::npos);
	REQUIRE(spec.find("Typed `*.pdxxx` files are the preferred content-unit format") != std::string::npos);
	REQUIRE(spec.find("No `.bin` alternate is valid") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	REQUIRE(modmgr.find("modmgrArchiveFindForbiddenBinPayload") != std::string::npos);
	REQUIRE(modmgr.find("External-format archives cannot contain .bin authoring payloads") != std::string::npos);
	REQUIRE(modmgr.find("modmgrArchiveEntryHasForbiddenBinPayload") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("entryHasForbiddenBinPayload(e->entry_name)") != std::string::npos);
	REQUIRE(packer.find("validateNoForbiddenBinsRecurse(srcFolder") != std::string::npos);
	REQUIRE(packer.find("Authored .bin files are not allowed in mod content or .pdmod transport archives") != std::string::npos);
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
	REQUIRE(packer.find("validateTypedPdDescriptorsRecurse(srcFolder") != std::string::npos);
	REQUIRE(packer.find("typedPdContentTypeForPath(childRel)") != std::string::npos);
	REQUIRE(packer.find("MODPACK.PDMOD: validated %u typed .pd* content descriptor(s) before packing") != std::string::npos);
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
	REQUIRE(packer.find("\"blender_scene_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_scene_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_material_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_materials_file\"") != std::string::npos);
	REQUIRE(packer.find("\"texture_manifest_file\"") != std::string::npos);
	REQUIRE(packer.find("\"visual_source_file\"") != std::string::npos);
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
	REQUIRE(scanner.find("archiveEntryIsDescriptor") != std::string::npos);
	REQUIRE(scanner.find("typedPdContentTypeForPath(entry_name)") != std::string::npos);
	REQUIRE(scanner.find("typedPdDescriptorComponentDir(entry_name") != std::string::npos);
	REQUIRE(scanner.find("typedPdArchiveDescriptorLeaf(entry_name)") != std::string::npos);
	REQUIRE(scanner.find("modArchiveExtractMemAlloc") != std::string::npos);
	REQUIRE(scanner.find("qualifyTypedArchiveSourcePaths(&ini, entry_name)") != std::string::npos);
	REQUIRE(scanner.find("qualifyArchiveIniPaths") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"catalog_id\", iniGet(ini, \"id\"") != std::string::npos);
	REQUIRE(scanner.find("\"blender_scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_material_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_materials_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"texture_manifest_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"visual_source_file\"") != std::string::npos);

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
	REQUIRE(scanner.find("scanTypedPdDescriptorsRecurse(mod_dir") != std::string::npos);
	REQUIRE(scanner.find("registerTypedPdDescriptorFile") != std::string::npos);
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
	REQUIRE(modmgr.find("modmgrDirHasDirectManifest") != std::string::npos);

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

TEST_CASE("typed pd content fixture keeps pdxxx files as content units",
          "[modding][pdmod][static][c3809]") {
	std::string manifest = readFile("tests/fixtures/modpipe/pdxxx-content/mod.json");
	std::string head = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/heads/tri_head.pdhead",
		"head.ini");
	std::string headGltf = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/heads/tri_head.pdhead",
		"model.gltf");
	std::string arena = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"arena.ini");
	std::string geometry = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"geometry.obj");
	std::string pads = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"pads.ini");
	std::string setup = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena.pdarena",
		"setup.ini");
	std::string weaponAnim = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/weapon_idle.pdanim",
		"animation.ini");
	std::string weaponGltf = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/weapon_idle.pdanim",
		"animation.gltf");
	std::string skeletalAnim = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/character_skeletal.pdanim",
		"animation.ini");
	std::string skeletalGltf = readArchiveEntryText(
		"tests/fixtures/modpipe/pdxxx-content/animations/character_skeletal.pdanim",
		"animation.gltf");

	REQUIRE(manifest.find("\"id\": \"fixture_pdxxx_content\"") != std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(
			"tests/fixtures/modpipe/pdxxx-content/heads/tri_head.pdhead");
		REQUIRE(opened.archive != nullptr);
	}
	REQUIRE(head.find("; tri_head.pdhead") != std::string::npos);
	REQUIRE(head.find("[head]") != std::string::npos);
	REQUIRE(head.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(headGltf.find("\"meshes\"") != std::string::npos);
	REQUIRE(arena.find("; tri_arena.pdarena") != std::string::npos);
	REQUIRE(arena.find("[arena]") != std::string::npos);
	REQUIRE(arena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE((geometry.find("f 1 2 3") != std::string::npos
		|| geometry.find("f 1/1 2/2 3/3") != std::string::npos));
	REQUIRE(pads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(setup.find("props = none") != std::string::npos);
	REQUIRE(weaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(weaponAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(weaponGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(skeletalAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"channels\"") != std::string::npos);

	const std::string *texts[] = {
		&manifest, &head, &headGltf, &arena, &geometry, &pads, &setup,
		&weaponAnim, &weaponGltf, &skeletalAnim, &skeletalGltf
	};
	for (const std::string *text : texts) {
		REQUIRE(text->find(".bin") == std::string::npos);
	}
	REQUIRE_FALSE(std::filesystem::exists(
		"tests/fixtures/modpipe/pdxxx-content/heads/tri_head/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(
		"tests/fixtures/modpipe/pdxxx-content/arenas/tri_arena/geometry.obj"));
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
	REQUIRE(scanner.find("\".pdhead\"") != std::string::npos);
	REQUIRE(scanner.find("\".pdarena\"") != std::string::npos);
	REQUIRE(scanner.find("\".pdanim\"") != std::string::npos);
	REQUIRE(scanner.find("\".pdmesh\"") != std::string::npos);
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
		{ "port/src/loader_walker_weapon.c",   "\"weapon\", \"weapons\", \".pdweapon\"" },
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

TEST_CASE("weapon content pipeline accepts pdweapon only",
          "[modding][pdxxx][weapon][static][c3814]") {
	const std::string removedWeaponExt = std::string(".pd") + "wpn";
	const char *liveFiles[] = {
		"port/src/loader_walker_weapon.c",
		"port/src/assetcatalog_scanner.c",
		"port/src/modpack_pdmod.c",
		"port/src/modmgr.c",
		"port/src/romextract_pdweapon.c",
		"port/include/romextract_pd.h",
	};

	for (const char *path : liveFiles) {
		INFO(path);
		std::string src = readFile(path);
		REQUIRE(src.find(".pdweapon") != std::string::npos);
		REQUIRE(src.find(removedWeaponExt) == std::string::npos);
	}

	const std::string extractor = readFile("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("nested_payloads = nested_payloads.json") != std::string::npos);
	REQUIRE(extractor.find("WEAPON_GRAPH_ARCHIVE_NESTED_PAYLOADS_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("base_weapon_graph_v1") != std::string::npos);
	REQUIRE(extractor.find("\"shared_context\"") != std::string::npos);
	REQUIRE(extractor.find("\"subgraphs\"") != std::string::npos);
	REQUIRE(extractor.find("\"subgraph\"") != std::string::npos);
	REQUIRE(extractor.find("damage_credit_player") != std::string::npos);
	REQUIRE(extractor.find("base_legacy_adapter") == std::string::npos);
	REQUIRE(extractor.find("spawn.fired_projectile") != std::string::npos);
	REQUIRE(extractor.find("spawn.thrown_physical") != std::string::npos);
	REQUIRE(extractor.find("fire.hitscan") != std::string::npos);
	REQUIRE(extractor.find("fire.auto_cadence") != std::string::npos);
	REQUIRE(extractor.find("fire.burst") != std::string::npos);
	REQUIRE(extractor.find("fire.charge_release") != std::string::npos);
	REQUIRE(extractor.find("fire.beam_tick") != std::string::npos);
	REQUIRE(extractor.find("special.remote_detonator") != std::string::npos);
	REQUIRE(extractor.find("device.activate") != std::string::npos);
	REQUIRE(extractor.find("projectile.ini") != std::string::npos);
	REQUIRE(extractor.find("entity.ini") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v9\"") != std::string::npos);
	REQUIRE(extractor.find("dependency_closure = \" PDWEAPON_DEPENDENCY_CLOSURE_MARKER") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v9\"") != std::string::npos);
	REQUIRE(extractor.find("models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("models/held_lo.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("animations_manifest.tsv") != std::string::npos);
	REQUIRE(extractor.find("audio_manifest.tsv") != std::string::npos);
	REQUIRE(extractor.find("model_archive = models/visual.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponMeshDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponAnimationDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponAudioDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addProjectileModelDependency") != std::string::npos);
	REQUIRE(extractor.find("s_existingArchiveEntryContains") != std::string::npos);
	REQUIRE(extractor.find("weaponGraphArchiveCanonicalSha256File") != std::string::npos);
	REQUIRE(extractor.find("modArchiveAddFileDisk(aw, p->archive_entry") != std::string::npos);

	const std::string main = readFile("port/src/main.c");
	const auto meshEmit = main.find("romExtractAllPdmesh(0)");
	const auto animEmit = main.find("romExtractAllPdanim(0)");
	const auto sfxEmit = main.find("romExtractAllPdsfx(0)");
	const auto weaponEmit = main.find("romExtractAllPdweapon(0)");
	REQUIRE(meshEmit != std::string::npos);
	REQUIRE(animEmit != std::string::npos);
	REQUIRE(sfxEmit != std::string::npos);
	REQUIRE(weaponEmit != std::string::npos);
	REQUIRE(meshEmit < weaponEmit);
	REQUIRE(animEmit < weaponEmit);
	REQUIRE(sfxEmit < weaponEmit);

	const std::string mesh = readFile("port/src/romextract_pdmesh.c");
	REQUIRE(mesh.find("s_pdmeshAddWeaponFuncPayloadWork") != std::string::npos);
	REQUIRE(mesh.find("sp->projectilemodelnum") != std::string::npos);
	REQUIRE(mesh.find("tw->projectilemodelnum") != std::string::npos);
}

TEST_CASE("projectile and entity asset kinds are catalog and manifest visible",
          "[modding][pdxxx][projectile_entity][static][c3814]") {
	std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(catalog.find("ASSET_ENTITY") != std::string::npos);
	REQUIRE(catalog.find("} projectile;") != std::string::npos);
	REQUIRE(catalog.find("} entity;") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find(".pdprojectile") != std::string::npos);
	REQUIRE(scanner.find(".pdentity") != std::string::npos);
	REQUIRE(scanner.find("\"projectile.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"entity.ini\"") != std::string::npos);
	REQUIRE(scanner.find("projectile.ini - physical projectile behavior metadata") != std::string::npos);
	REQUIRE(scanner.find("entity.ini - deployed/stuck behavior archetype metadata") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(e->id, e->ext.projectile.entity_ref") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find(".pdprojectile") != std::string::npos);
	REQUIRE(packer.find(".pdentity") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"projectiles\", \"projectile.ini\", \"projectile\"") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"entities\", \"entity.ini\", \"entity\"") != std::string::npos);

	std::string manifest_h = readFile("port/include/net/netmanifest.h");
	REQUIRE(manifest_h.find("MANIFEST_TYPE_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_h.find("MANIFEST_TYPE_ENTITY") != std::string::npos);

	std::string manifest_c = readFile("port/src/net/netmanifest.c");
	REQUIRE(manifest_c.find("case ASSET_PROJECTILE: return MANIFEST_TYPE_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_c.find("case ASSET_ENTITY:") != std::string::npos);
	REQUIRE(manifest_c.find("case MANIFEST_TYPE_PROJECTILE: return ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_c.find("case MANIFEST_TYPE_ENTITY:") != std::string::npos);

	std::string netmsg = readFile("port/src/net/netmsg.c");
	REQUIRE(netmsg.find("ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("projectile.ini\") == 0) return ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(distrib.find("entity.ini\") == 0)    return ASSET_ENTITY") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	REQUIRE(modmgr.find(".pdprojectile") != std::string::npos);
	REQUIRE(modmgr.find(".pdentity") != std::string::npos);
}

TEST_CASE("weapon graph archive helpers derive IDs and validate graph roots",
          "[modding][pdxxx][weapon_graph][archive][c3814]") {
	REQUIRE(std::string(weaponGraphArchiveDescriptorForType(ASSET_WEAPON)) == "weapon.ini");
	REQUIRE(std::string(weaponGraphArchiveDescriptorForType(ASSET_PROJECTILE)) == "projectile.ini");
	REQUIRE(std::string(weaponGraphArchiveDescriptorForType(ASSET_ENTITY)) == "entity.ini");
	REQUIRE(weaponGraphArchiveGraphRequired(ASSET_WEAPON) == 1);
	REQUIRE(weaponGraphArchiveGraphRequired(ASSET_PROJECTILE) == 0);
	REQUIRE(weaponGraphArchiveGraphRequired(ASSET_ENTITY) == 1);

	char id[CATALOG_ID_LEN];
	REQUIRE(weaponGraphArchiveDerivedNestedId("base:slayer", ASSET_PROJECTILE,
		"Fly By Wire Rocket", id, sizeof(id)) == 0);
	REQUIRE(std::string(id) == "base:slayer__projectile_fly_by_wire_rocket");
	REQUIRE(weaponGraphArchiveDerivedNestedId("base:laptopgun", ASSET_ENTITY,
		"deployed-autogun", id, sizeof(id)) == 0);
	REQUIRE(std::string(id) == "base:laptopgun__entity_deployed_autogun");
	REQUIRE(weaponGraphArchiveDerivedNestedId("base:laptopgun", ASSET_PROP,
		"bad", id, sizeof(id)) != 0);

	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"manifest = manifest.json\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n";
	TempArchive weapon = writeArchiveEntries("weapon-graph-root", {
		{ "weapon.ini", weaponIni },
		{ "manifest.json", "{}\n" },
		{ "behavior.graph.json", "{\"schema\":\"pd.weapon_graph.v1\"}\n" },
		{ "nested_payloads.json", "{\"schema\":\"pd.weapon_nested_payloads.v1\",\"payloads\":[]}\n" },
	});

	char err[256] = {};
	REQUIRE(weaponGraphArchiveValidateRootFile(weapon.path.string().c_str(),
		ASSET_WEAPON, err, sizeof(err)) == 0);

	weapon_graph_archive_descriptor_t desc;
	REQUIRE(weaponGraphArchiveReadDescriptorFile(weapon.path.string().c_str(),
		ASSET_WEAPON, &desc, err, sizeof(err)) == 0);
	REQUIRE(std::string(desc.catalog_id) == "base:test_weapon");
	REQUIRE(std::string(desc.behavior_graph) == "behavior.graph.json");
	REQUIRE(std::string(desc.nested_payloads) == "nested_payloads.json");

	const std::string projectileIni =
		"[projectile]\n"
		"catalog_id = base:test_projectile\n"
		"model_file = model.gltf\n";
	TempArchive projectile = writeArchiveEntries("projectile-graph-root", {
		{ "projectile.ini", projectileIni },
	});
	REQUIRE(weaponGraphArchiveValidateRootFile(projectile.path.string().c_str(),
		ASSET_PROJECTILE, err, sizeof(err)) == 0);

	const std::string entityIni =
		"[entity]\n"
		"catalog_id = base:test_entity\n"
		"archetype = autogun\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive brokenEntity = writeArchiveEntries("entity-graph-root", {
		{ "entity.ini", entityIni },
	});
	REQUIRE(weaponGraphArchiveValidateRootFile(brokenEntity.path.string().c_str(),
		ASSET_ENTITY, err, sizeof(err)) != 0);
}

TEST_CASE("weapon graph canonical archive SHA ignores entry order",
          "[modding][pdxxx][weapon_graph][sha256][c3814]") {
	const std::string projectileIni =
		"[projectile]\n"
		"catalog_id = base:rocket\n"
		"behavior_graph = behavior.graph.json\n";
	const std::string graph =
		"{\"schema\":\"pd.projectile_graph.v1\",\"nodes\":[]}\n";

	TempArchive a = writeArchiveEntries("canonical-a", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", graph },
	});
	TempArchive b = writeArchiveEntries("canonical-b", {
		{ "behavior.graph.json", graph },
		{ "projectile.ini", projectileIni },
	});
	TempArchive c = writeArchiveEntries("canonical-c", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.projectile_graph.v1\",\"nodes\":[1]}\n" },
	});

	char ha[SHA256_HEX_SIZE] = {};
	char hb[SHA256_HEX_SIZE] = {};
	char hc[SHA256_HEX_SIZE] = {};
	char hbBytes[SHA256_HEX_SIZE] = {};
	REQUIRE(weaponGraphArchiveCanonicalSha256File(a.path.string().c_str(), ha) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256File(b.path.string().c_str(), hb) == 0);
	REQUIRE(weaponGraphArchiveCanonicalSha256File(c.path.string().c_str(), hc) == 0);
	REQUIRE(std::string(ha).size() == 64);
	REQUIRE(std::string(ha) == std::string(hb));
	REQUIRE(std::string(ha) != std::string(hc));

	const std::string bytes = readFile(b.path.string().c_str());
	REQUIRE(weaponGraphArchiveCanonicalSha256Bytes(bytes.data(),
		static_cast<u32>(bytes.size()), hbBytes) == 0);
	REQUIRE(std::string(hbBytes) == std::string(hb));
}

TEST_CASE("weapon graph nested payload inventory derives IDs and content hashes",
          "[modding][pdxxx][weapon_graph][inventory][c3814]") {
	const std::string projectileIni =
		"[projectile]\n"
		"name = Rocket\n"
		"behavior_graph = behavior.graph.json\n";
	const std::string entityIni =
		"[entity]\n"
		"name = Deployed Autogun\n"
		"archetype = autogun\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive projectile = writeArchiveEntries("nested-projectile", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.projectile_graph.v1\"}\n" },
	});
	TempArchive entity = writeArchiveEntries("nested-entity", {
		{ "entity.ini", entityIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.entity_graph.v1\"}\n" },
	});

	const std::string projectileBytes = readFile(projectile.path.string().c_str());
	const std::string entityBytes = readFile(entity.path.string().c_str());
	const std::string parentIni =
		"[weapon]\n"
		"catalog_id = base:laptopgun\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n";
	TempArchive parent = writeArchiveEntries("nested-parent", {
		{ "weapon.ini", parentIni },
		{ "behavior.graph.json", "{\"schema\":\"pd.weapon_graph.v1\"}\n" },
		{ "nested_payloads.json", "{\"schema\":\"pd.weapon_nested_payloads.v1\",\"payloads\":[]}\n" },
		{ "projectiles/thrown_laptop.pdprojectile", projectileBytes },
		{ "entities/deployed_autogun.pdentity", entityBytes },
	});

	weapon_graph_archive_inventory_t inv;
	char err[256] = {};
	REQUIRE(weaponGraphArchiveScanNestedPayloadsFile(parent.path.string().c_str(),
		"base:laptopgun", &inv, err, sizeof(err)) == 2);
	REQUIRE(inv.count == 2);

	char projectileHash[SHA256_HEX_SIZE] = {};
	REQUIRE(weaponGraphArchiveCanonicalSha256File(projectile.path.string().c_str(),
		projectileHash) == 0);

	bool sawProjectile = false;
	bool sawEntity = false;
	for (s32 i = 0; i < inv.count; i++) {
		const weapon_graph_archive_payload_t &p = inv.payloads[i];
		if (p.type == ASSET_PROJECTILE) {
			sawProjectile = true;
			REQUIRE(std::string(p.local_slug) == "thrown_laptop");
			REQUIRE(std::string(p.catalog_id) == "base:laptopgun__projectile_thrown_laptop");
			REQUIRE(std::string(p.archive_entry) == "projectiles/thrown_laptop.pdprojectile");
			REQUIRE(std::string(p.canonical_sha256) == std::string(projectileHash));
		}
		if (p.type == ASSET_ENTITY) {
			sawEntity = true;
			REQUIRE(std::string(p.local_slug) == "deployed_autogun");
			REQUIRE(std::string(p.catalog_id) == "base:laptopgun__entity_deployed_autogun");
			REQUIRE(std::string(p.archive_entry) == "entities/deployed_autogun.pdentity");
			REQUIRE(std::string(p.canonical_sha256).size() == 64);
		}
	}
	REQUIRE(sawProjectile);
	REQUIRE(sawEntity);

	char json[2048];
	REQUIRE(weaponGraphArchiveFormatNestedPayloadsJson("base:laptopgun",
		&inv, json, sizeof(json)) == 0);
	const std::string inventoryJson(json);
	REQUIRE(inventoryJson.find("\"schema\": \"pd.weapon_nested_payloads.v1\"") != std::string::npos);
	REQUIRE(inventoryJson.find("base:laptopgun__projectile_thrown_laptop") != std::string::npos);
	REQUIRE(inventoryJson.find("base:laptopgun__entity_deployed_autogun") != std::string::npos);
	REQUIRE(inventoryJson.find(projectileHash) != std::string::npos);
}

TEST_CASE("weapon graph runtime compiler validates modules and deterministic IR",
          "[modding][pdxxx][weapon_graph][compiler][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"unit_test_graph_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"recoverytime_ticks60\": 12,\n"
		"        \"projectile_ref\": null,\n"
		"        \"damage\": 1.25,\n"
		"        \"ammo_slot\": 0,\n"
		"        \"mode\": \"primary\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"spawn.fired_projectile\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"projectile_ref\": \"base:test_weapon__projectile_rocket\",\n"
		"        \"timer_ticks60\": 180\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [ { \"from\": \"primary_action\", \"to\": \"secondary_action\" } ],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t a;
	weapon_graph_ir_t b;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &a, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &b, err, sizeof(err)) == 0);
	REQUIRE(a.node_count == 2);
	REQUIRE(a.edge_count == 1);
	REQUIRE(a.export_count == 2);
	REQUIRE(a.nodes[0].opcode == WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(a.nodes[1].opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);
	REQUIRE(std::string(a.source_sha256).size() == 64);
	REQUIRE(std::string(a.ir_sha256).size() == 64);
	REQUIRE(std::string(a.ir_sha256) == std::string(b.ir_sha256));
	REQUIRE(std::string(a.params[a.nodes[0].param_start].key) == "ammo_slot");

	REQUIRE(weaponGraphOpcodeForKind(ASSET_PROJECTILE,
		"projectile.fly_by_wire") == WEAPON_GRAPH_OP_PROJECTILE_FLY_BY_WIRE);
	REQUIRE(weaponGraphOpcodeForKind(ASSET_ENTITY,
		"entity.autogun") == WEAPON_GRAPH_OP_ENTITY_AUTOGUN);
	REQUIRE(weaponGraphOpcodeForKind(ASSET_WEAPON,
		"projectile.fly_by_wire") == WEAPON_GRAPH_OP_INVALID);

	const std::string weaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive weapon = writeArchiveEntries("weapon-graph-compiler", {
		{ "weapon.ini", weaponIni },
		{ "behavior.graph.json", graph },
	});
	weapon_graph_ir_t fromArchive;
	REQUIRE(weaponGraphCompileArchiveFile(weapon.path.string().c_str(),
		ASSET_WEAPON, &fromArchive, err, sizeof(err)) == 0);
	REQUIRE(std::string(fromArchive.ir_sha256) == std::string(a.ir_sha256));
}

TEST_CASE("weapon graph runtime preserves modular subgraphs and shared context",
          "[modding][pdxxx][weapon_graph][compiler][context][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:remotemine\",\n"
		"  \"graph_id\": \"modular_context_test\",\n"
		"  \"shared_context\": [\n"
		"    { \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" },\n"
		"    { \"name\": \"detonator_link_group\", \"scope\": \"weapon\", "
		"\"source\": \"weapon_instance\", \"type\": \"link_group_ref\", "
		"\"lifetime\": \"player_life\" }\n"
		"  ],\n"
		"  \"subgraphs\": [\n"
		"    { \"id\": \"primary\", \"entry\": \"throw_mine\", "
		"\"shared_context\": [\"owner_player\", \"detonator_link_group\"] },\n"
		"    { \"id\": \"secondary\", \"entry\": \"detonate_mines\", "
		"\"shared_context\": [\"owner_player\", \"detonator_link_group\"] }\n"
		"  ],\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"throw_mine\", \"kind\": \"spawn.thrown_physical\", "
		"\"subgraph\": \"primary\", \"params\": { \"mode\": \"primary\", "
		"\"payload_ref\": \"base:remotemine__entity_armed_remote_mine\", "
		"\"activation_time_ticks60\": 240, "
		"\"context_refs\": [\"owner_player\", \"detonator_link_group\"] } },\n"
		"    { \"id\": \"detonate_mines\", \"kind\": \"special.remote_detonator\", "
		"\"subgraph\": \"secondary\", \"params\": { \"mode\": \"secondary\", "
		"\"detonator_ref\": \"base:remotemine__entity_armed_remote_mine\", "
		"\"context_refs\": [\"owner_player\", \"detonator_link_group\"] } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"throw_mine\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"detonate_mines\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
	REQUIRE(ir.context_count == 2);
	REQUIRE(std::string(ir.contexts[0].name) == "owner_player");
	REQUIRE(std::string(ir.contexts[1].type) == "link_group_ref");
	REQUIRE(ir.subgraph_count == 2);
	REQUIRE(std::string(ir.subgraphs[0].id) == "primary");
	REQUIRE(ir.subgraphs[0].entry_node == 0);
	REQUIRE(std::string(ir.nodes[0].subgraph) == "primary");
	REQUIRE(std::string(ir.nodes[1].subgraph) == "secondary");
	REQUIRE(std::string(ir.ir_sha256).size() == 64);
}

TEST_CASE("weapon graph editor layout metadata does not affect runtime IR hash",
          "[modding][pdxxx][weapon_graph][compiler][layout][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:falcon2\",\n"
		"  \"graph_id\": \"layout_hash_test\",\n"
		"  \"shared_context\": [\n"
		"    { \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" }\n"
		"  ],\n"
		"  \"subgraphs\": [\n"
		"    { \"id\": \"primary\", \"entry\": \"single\", "
		"\"shared_context\": [\"owner_player\"] }\n"
		"  ],\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"single\", \"kind\": \"fire.hitscan\", "
		"\"subgraph\": \"primary\", \"params\": { \"mode\": \"primary\", "
		"\"damage\": 8.0, \"context_refs\": [\"owner_player\"] } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"single\" } ]\n"
		"}\n";
	const std::string graphWithLayout =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:falcon2\",\n"
		"  \"graph_id\": \"layout_hash_test\",\n"
		"  \"shared_context\": [\n"
		"    { \"name\": \"owner_player\", \"scope\": \"player\", "
		"\"source\": \"equipped_player\", \"type\": \"player_ref\", "
		"\"lifetime\": \"weapon_instance\" }\n"
		"  ],\n"
		"  \"subgraphs\": [\n"
		"    { \"id\": \"primary\", \"entry\": \"single\", "
		"\"shared_context\": [\"owner_player\"] }\n"
		"  ],\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"single\", \"kind\": \"fire.hitscan\", "
		"\"subgraph\": \"primary\", \"params\": { \"mode\": \"primary\", "
		"\"damage\": 8.0, \"context_refs\": [\"owner_player\"] } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"single\" } ],\n"
		"  \"editor\": {\n"
		"    \"layout\": {\n"
		"      \"nodes\": [ { \"id\": \"single\", \"x\": 64.0, \"y\": 88.0 } ]\n"
		"    }\n"
		"  }\n"
		"}\n";

	weapon_graph_ir_t baseIr;
	weapon_graph_ir_t layoutIr;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &baseIr, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graphWithLayout.data(),
		static_cast<u32>(graphWithLayout.size()), &layoutIr, err, sizeof(err)) == 0);
	REQUIRE(std::string(baseIr.ir_sha256) == std::string(layoutIr.ir_sha256));
	REQUIRE(std::string(baseIr.source_sha256) != std::string(layoutIr.source_sha256));
}

TEST_CASE("weapon graph runtime compiler rejects unsafe or ambiguous graphs",
          "[modding][pdxxx][weapon_graph][compiler][c3814]") {
	char err[256] = {};
	weapon_graph_ir_t ir;

	const std::string unsupported =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad\",\"nodes\":[{\"id\":\"n\",\"kind\":\"script.lua\","
		"\"params\":{}}],\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, unsupported.data(),
		static_cast<u32>(unsupported.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unsupported") != std::string::npos);

	const std::string badUnit =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad_unit\",\"nodes\":[{\"id\":\"n\","
		"\"kind\":\"fire.hitscan\",\"params\":{\"timer\":5}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, badUnit.data(),
		static_cast<u32>(badUnit.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("units") != std::string::npos);

	const std::string badRef =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad_ref\",\"nodes\":[{\"id\":\"n\","
		"\"kind\":\"spawn.fired_projectile\","
		"\"params\":{\"projectile_ref\":\"rocket\"}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, badRef.data(),
		static_cast<u32>(badRef.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("catalog id") != std::string::npos);

	const std::string cycle =
		"{\"schema\":\"pd.projectile_graph.v1\",\"asset_id\":\"base:p\","
		"\"graph_id\":\"cycle\",\"nodes\":["
		"{\"id\":\"a\",\"kind\":\"projectile.spawn_state\",\"params\":{}},"
		"{\"id\":\"b\",\"kind\":\"projectile.motion\",\"params\":{}}],"
		"\"edges\":[{\"from\":\"a\",\"to\":\"b\"},{\"from\":\"b\",\"to\":\"a\"}],"
		"\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_PROJECTILE, cycle.data(),
		static_cast<u32>(cycle.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("cycle") != std::string::npos);

	const std::string badSubgraph =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"bad_subgraph\","
		"\"shared_context\":[{\"name\":\"owner_player\",\"scope\":\"player\","
		"\"source\":\"equipped_player\"}],"
		"\"subgraphs\":[{\"id\":\"primary\",\"entry\":\"missing\"}],"
		"\"nodes\":[{\"id\":\"n\",\"kind\":\"fire.hitscan\",\"params\":{}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, badSubgraph.data(),
		static_cast<u32>(badSubgraph.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("missing entry") != std::string::npos);

	const std::string dupContext =
		"{\"schema\":\"pd.weapon_graph.v1\",\"asset_id\":\"base:test\","
		"\"graph_id\":\"dup_context\","
		"\"shared_context\":["
		"{\"name\":\"owner_player\",\"scope\":\"player\",\"source\":\"a\"},"
		"{\"name\":\"owner_player\",\"scope\":\"player\",\"source\":\"b\"}],"
		"\"nodes\":[{\"id\":\"n\",\"kind\":\"fire.hitscan\",\"params\":{}}],"
		"\"edges\":[],\"exports\":[]}";
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, dupContext.data(),
		static_cast<u32>(dupContext.size()), &ir, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("duplicate shared context") != std::string::npos);
}

TEST_CASE("weapon graph runtime is wired to a Debug Settings toggle",
          "[modding][pdxxx][weapon_graph][debug_toggle][c3814]") {
	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("Debug.WeaponGraphRuntime") != std::string::npos);
	REQUIRE(runtime.find("configRegisterInt(\"Debug.WeaponGraphRuntime\"") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeEnabled") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeSetEnabled") != std::string::npos);

	const std::string settings = readFile("port/fast3d/pdgui_menu_mainmenu.cpp");
	REQUIRE(settings.find("Weapon Graph Runtime") != std::string::npos);
	REQUIRE(settings.find("weaponGraphRuntimeEnabled()") != std::string::npos);
	REQUIRE(settings.find("weaponGraphRuntimeSetEnabled") != std::string::npos);
}

TEST_CASE("weapon graph runtime registers held weapon IR for gameplay gate",
          "[modding][pdxxx][weapon_graph][runtime][held][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"held_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"shoot_single\",\n"
		"        \"ammo_slot\": 0,\n"
		"        \"flags\": 64,\n"
		"        \"damage\": 7.5,\n"
		"        \"spread\": 1.25,\n"
		"        \"recoil_anim_unk24\": 2,\n"
		"        \"recoil_anim_unk25\": 3,\n"
		"        \"recoil_anim_unk26\": 4,\n"
		"        \"recoil_anim_unk27\": 5,\n"
		"        \"recoildist\": 6.5,\n"
		"        \"recoilangle\": 7.5,\n"
		"        \"slidemax\": 8.5,\n"
		"        \"impactforce\": 2.25,\n"
		"        \"duration_ticks60\": 5,\n"
		"        \"shootsound\": \"SFX_804D\",\n"
		"        \"penetration\": 3\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"fire.auto_cadence\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"function_type\": \"shoot_automatic\",\n"
		"        \"initial_rpm\": 300,\n"
		"        \"turret_accel\": 8,\n"
		"        \"turret_decel\": 12,\n"
		"        \"max_rpm\": 900\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(7, &ir, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetHeldFunctionForGameplay(7, 0) == nullptr);

	weaponGraphRuntimeSetEnabled(1);
	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(7, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(7, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->opcode == WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(primary->function_type_id == INVENTORYFUNCTYPE_SHOOT_SINGLE);
	REQUIRE(primary->has_damage == 1);
	REQUIRE(primary->damage == Approx(7.5f));
	REQUIRE(primary->has_spread == 1);
	REQUIRE(primary->spread == Approx(1.25f));
	REQUIRE(primary->has_recoil_anim_unk24 == 1);
	REQUIRE(primary->recoil_anim_unk24 == 2);
	REQUIRE(primary->has_recoil_anim_unk25 == 1);
	REQUIRE(primary->recoil_anim_unk25 == 3);
	REQUIRE(primary->has_recoil_anim_unk26 == 1);
	REQUIRE(primary->recoil_anim_unk26 == 4);
	REQUIRE(primary->has_recoil_anim_unk27 == 1);
	REQUIRE(primary->recoil_anim_unk27 == 5);
	REQUIRE(primary->has_recoildist == 1);
	REQUIRE(primary->recoildist == Approx(6.5f));
	REQUIRE(primary->has_recoilangle == 1);
	REQUIRE(primary->recoilangle == Approx(7.5f));
	REQUIRE(primary->has_slidemax == 1);
	REQUIRE(primary->slidemax == Approx(8.5f));
	REQUIRE(primary->has_impactforce == 1);
	REQUIRE(primary->impactforce == Approx(2.25f));
	REQUIRE(primary->has_duration_ticks60 == 1);
	REQUIRE(primary->duration_ticks60 == 5);
	REQUIRE(primary->has_shootsound == 1);
	REQUIRE(primary->shootsound == SFX_804D);
	REQUIRE(primary->has_penetration == 1);
	REQUIRE(primary->penetration == 3);
	REQUIRE(primary->flags == 64);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_FIRE_AUTO_CADENCE);
	REQUIRE(secondary->has_initial_rpm == 1);
	REQUIRE(secondary->initial_rpm == Approx(300.0f));
	REQUIRE(secondary->has_max_rpm == 1);
	REQUIRE(secondary->max_rpm == Approx(900.0f));
	REQUIRE(secondary->has_turret_accel == 1);
	REQUIRE(secondary->turret_accel == 8);
	REQUIRE(secondary->has_turret_decel == 1);
	REQUIRE(secondary->turret_decel == 12);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon graph runtime captures projectile and entity adapter payloads",
          "[modding][pdxxx][weapon_graph][runtime][projectile][entity][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_projectile_weapon\",\n"
		"  \"graph_id\": \"projectile_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"spawn.fired_projectile\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"shoot_projectile\",\n"
		"        \"ammo_slot\": 0,\n"
		"        \"flags\": 134217728,\n"
		"        \"projectile_ref\": \"base:dyrocket_projectile\",\n"
		"        \"projectile_model_ref\": \"MODEL_dyrocket\",\n"
		"        \"scale\": 1.5,\n"
		"        \"speed\": 300,\n"
		"        \"travel_distance\": 900,\n"
		"        \"timer_ticks60\": 120,\n"
		"        \"reflect_angle\": 0.75,\n"
		"        \"soundnum\": \"SFX_LAUNCH_ROCKET_8053\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"spawn.thrown_physical\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"function_type\": \"throw\",\n"
		"        \"ammo_slot\": 1,\n"
		"        \"payload_ref\": \"base:laptop_autogun\",\n"
		"        \"entity_ref\": \"base:laptop_autogun\",\n"
		"        \"projectile_model_ref\": \"MODEL_autogun\",\n"
		"        \"activation_time_ticks60\": 90,\n"
		"        \"recovery_time_ticks60\": 45,\n"
		"        \"damage\": 12.5\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(8, &ir, err, sizeof(err)) == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(8, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(8, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);
	REQUIRE(primary->function_type_id == INVENTORYFUNCTYPE_SHOOT_PROJECTILE);
	REQUIRE(primary->has_projectile_modelnum == 1);
	REQUIRE(primary->projectile_modelnum == MODEL_CHRDYROCKETMIS);
	REQUIRE(std::string(primary->projectile_ref) == "base:dyrocket_projectile");
	REQUIRE(primary->has_scale == 1);
	REQUIRE(primary->scale == Approx(1.5f));
	REQUIRE(primary->has_speed == 1);
	REQUIRE(primary->speed == Approx(300.0f));
	REQUIRE(primary->has_travel_distance == 1);
	REQUIRE(primary->travel_distance == 900);
	REQUIRE(primary->has_timer_ticks60 == 1);
	REQUIRE(primary->timer_ticks60 == 120);
	REQUIRE(primary->has_reflect_angle == 1);
	REQUIRE(primary->reflect_angle == Approx(0.75f));
	REQUIRE(primary->has_soundnum == 1);
	REQUIRE(primary->soundnum == SFX_LAUNCH_ROCKET_8053);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_SPAWN_THROWN_PHYSICAL);
	REQUIRE(secondary->function_type_id == INVENTORYFUNCTYPE_THROW);
	REQUIRE(secondary->has_projectile_modelnum == 1);
	REQUIRE(secondary->projectile_modelnum == MODEL_CHRAUTOGUN);
	REQUIRE(std::string(secondary->payload_ref) == "base:laptop_autogun");
	REQUIRE(std::string(secondary->entity_ref) == "base:laptop_autogun");
	REQUIRE(secondary->has_activation_time_ticks60 == 1);
	REQUIRE(secondary->activation_time_ticks60 == 90);
	REQUIRE(secondary->has_recovery_time_ticks60 == 1);
	REQUIRE(secondary->recovery_time_ticks60 == 45);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("weapon graph runtime captures special and device adapter payloads",
          "[modding][pdxxx][weapon_graph][runtime][special][device][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_device_weapon\",\n"
		"  \"graph_id\": \"special_device_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_action\",\n"
		"      \"kind\": \"special.remote_detonator\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"special\",\n"
		"        \"ammo_slot\": 0,\n"
		"        \"specialfunc\": 5,\n"
		"        \"recovery_time_ticks60\": 30,\n"
		"        \"soundnum\": \"SFX_LAUNCH_ROCKET_8053\"\n"
		"      }\n"
		"    },\n"
		"    {\n"
		"      \"id\": \"secondary_action\",\n"
		"      \"kind\": \"device.activate\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"secondary\",\n"
		"        \"function_type\": \"device\",\n"
		"        \"device\": 64\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(9, &ir, err, sizeof(err)) == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(9, 0);
	const weapon_graph_held_function_t *secondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(9, 1);
	REQUIRE(primary != nullptr);
	REQUIRE(secondary != nullptr);
	REQUIRE(primary->opcode == WEAPON_GRAPH_OP_SPECIAL_REMOTE_DETONATOR);
	REQUIRE(primary->function_type_id == INVENTORYFUNCTYPE_SPECIAL);
	REQUIRE(primary->has_specialfunc == 1);
	REQUIRE(primary->specialfunc == HANDATTACKTYPE_DETONATE);
	REQUIRE(primary->has_recovery_time_ticks60 == 1);
	REQUIRE(primary->recovery_time_ticks60 == 30);
	REQUIRE(primary->has_soundnum == 1);
	REQUIRE(primary->soundnum == SFX_LAUNCH_ROCKET_8053);
	REQUIRE(secondary->opcode == WEAPON_GRAPH_OP_DEVICE_ACTIVATE);
	REQUIRE(secondary->function_type_id == INVENTORYFUNCTYPE_DEVICE);
	REQUIRE(secondary->has_device == 1);
	REQUIRE(secondary->device == DEVICE_CLOAKDEVICE);

	weaponGraphRuntimeSetEnabled(0);
	weaponGraphRuntimeClearAll();
}

TEST_CASE("held weapon graph adapter is wired into runtime callsites",
          "[modding][pdxxx][weapon_graph][runtime][static][c3814]") {
	const std::string walker = readFile("port/src/loader_walker_weapon.c");
	REQUIRE(walker.find("weaponGraphRuntimeRegisterWeaponArchive") != std::string::npos);
	REQUIRE(walker.find("behavior.graph.json") == std::string::npos);

	const std::string accessors = readFile("src/game/game_0b0fd0.c");
	REQUIRE(accessors.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(accessors.find("weaponGraphRuntimeGetHeldFunctionForGameplay") != std::string::npos);
	REQUIRE(accessors.find("gsetGetDamage") != std::string::npos);
	REQUIRE(accessors.find("weaponGetNumTicksPerShot") != std::string::npos);
	REQUIRE(accessors.find("graph->function_type_id") != std::string::npos);
	REQUIRE(accessors.find("graph->has_device") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetHeldGraph") != std::string::npos);
	REQUIRE(bondgun.find("graph->has_max_rpm") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetAmmoIndexFromGraph") != std::string::npos);
	REQUIRE(bondgun.find("graph->has_recovery_time_ticks60") != std::string::npos);
	REQUIRE(bondgun.find("graph->has_projectile_modelnum") != std::string::npos);

	const std::string bondmove = readFile("src/game/bondmove.c");
	REQUIRE(bondmove.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(bondmove.find("graph->function_type_id") != std::string::npos);

	const std::string botact = readFile("src/game/botact.c");
	REQUIRE(botact.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(botact.find("graph->recoil_anim_unk24 + graph->recoil_anim_unk25") != std::string::npos);

	const std::string chraction = readFile("src/game/chraction.c");
	REQUIRE(chraction.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(chraction.find("graph->has_projectile_modelnum") != std::string::npos);
}

TEST_CASE("PC weapon switching and function HUD consume action-map state",
          "[input][weapon][static][c3814]") {
	const std::string actionmap = readFile("port/src/actionmap.cpp");
	REQUIRE(actionmap.find("ACTION_WEAPON_NEXT,    VKL_Q") != std::string::npos);
	REQUIRE(actionmap.find("st->up_time_ms") != std::string::npos);
	REQUIRE(actionmap.find("hold_vis_grace_until_ms") != std::string::npos);

	const std::string bondmove = readFile("src/game/bondmove.c");
	REQUIRE(bondmove.find("bmoveHandleDirectWeaponSelect") != std::string::npos);
	REQUIRE(bondmove.find("ACTION_WEAPON_6") != std::string::npos);
	REQUIRE(bondmove.find("ACTION_WEAPON_PREV") != std::string::npos);
	REQUIRE(bondmove.find("actionWasTap((s32)contpad1, ACTION_WEAPON_NEXT") != std::string::npos);
	REQUIRE(bondmove.find("bmoveSelectInventoryWeaponIndex") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("ctrl->curfnstr = 0") != std::string::npos);
	REQUIRE(bondgun.find("displayweaponnum = player->gunctrl.weaponnum") != std::string::npos);
	REQUIRE(bondgun.find("weaponGetFunctionById(displayweaponnum, funcnum)") != std::string::npos);
	REQUIRE(bondgun.find("invGetWeaponNumByIndex(currentindex)") != std::string::npos);
	REQUIRE(bondgun.find("weaponGetFunctionById(hand->gset.weaponnum, funcnum)") == std::string::npos);
	REQUIRE(bondgun.find("if (ctrl->curfnstr != func->name)") != std::string::npos);
	REQUIRE(bondgun.find("ctrl->curfnstr != func->name && ctrl->fnfader > 128") == std::string::npos);

	const std::string activemenu = readFile("src/game/activemenu.c");
	REQUIRE(activemenu.find("equippedweaponnum = bgunGetWeaponNum(HAND_RIGHT)") != std::string::npos);
	REQUIRE(activemenu.find("weaponGetFunctionById(equippedweaponnum, FUNC_PRIMARY)") != std::string::npos);
	REQUIRE(activemenu.find("weaponGetFunction(&g_Vars.currentplayer->hands[HAND_RIGHT].gset, FUNC_PRIMARY)") == std::string::npos);

	const std::string accessors = readFile("src/game/game_0b0fd0.c");
	REQUIRE(accessors.find("if (which >= 2)") != std::string::npos);
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

TEST_CASE("packed pdmod transport fixture resolves typed pdxxx content through production VFS",
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
	REQUIRE(manifestText.find("\"id\": \"fixture_pdxxx_content\"") != std::string::npos);

	const char *requiredEntries[] = {
		"mod.json",
		"heads/tri_head.pdhead",
		"arenas/tri_arena.pdarena",
		"animations/weapon_idle.pdanim",
		"animations/character_skeletal.pdanim",
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
	MountedArchive mount{ "fixture_pdxxx_content", false };
	REQUIRE(modVfsMount(mount.modId.c_str(), opened.archive) == 1);
	mount.mounted = true;

	for (const char *entry : requiredEntries) {
		INFO(entry);
		REQUIRE(modVfsCanResolve(entry) == 1);
		REQUIRE(modVfsGetSize(entry) > 0);
	}
	REQUIRE(modVfsCanResolve("heads/tri_head.pdhead::head.ini") == 1);
	REQUIRE(modVfsGetSize("heads/tri_head.pdhead::head.ini") > 0);
	REQUIRE(modVfsCanResolve("arenas/tri_arena.pdarena::geometry.obj") == 1);
	REQUIRE(modVfsGetSize("arenas/tri_arena.pdarena::geometry.obj") > 0);

	const std::string arena =
		readVfsText("arenas/tri_arena.pdarena::arena.ini");
	const std::string geometry =
		readVfsText("arenas/tri_arena.pdarena::geometry.obj");
	const std::string pads =
		readVfsText("arenas/tri_arena.pdarena::pads.ini");
	const std::string head =
		readVfsText("heads/tri_head.pdhead::head.ini");
	const std::string model =
		readVfsText("heads/tri_head.pdhead::model.gltf");
	const std::string weaponAnim =
		readVfsText("animations/weapon_idle.pdanim::animation.ini");
	const std::string weaponGltf =
		readVfsText("animations/weapon_idle.pdanim::animation.gltf");
	const std::string skeletalAnim =
		readVfsText("animations/character_skeletal.pdanim::animation.ini");
	const std::string skeletalGltf =
		readVfsText("animations/character_skeletal.pdanim::animation.gltf");

	REQUIRE(arena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE((geometry.find("f 1 2 3") != std::string::npos
		|| geometry.find("f 1/1 2/2 3/3") != std::string::npos));
	REQUIRE(pads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(head.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(model.find("data:application/octet-stream;base64,") != std::string::npos);
	REQUIRE(weaponAnim.find("catalog_id = fixture:weapon_idle") != std::string::npos);
	REQUIRE(weaponGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = fixture:character_skeletal") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	mod_vfs_stats_t stats;
	modVfsGetStats(&stats);
	REQUIRE(stats.mounts_active == 1);
	REQUIRE(stats.entries_resident >= 4);
}

TEST_CASE("modder examples are zip-openable typed pdxxx asset archives",
          "[modding][pdxxx][examples][static][c3811][c3812]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";
	REQUIRE(std::filesystem::is_directory(root));

	const std::string index = readFile("examples/modding/README.md");
	REQUIRE(index.find("typed `*.pdxxx` asset archives") != std::string::npos);
	REQUIRE(index.find("`.pdmod` is only the transport wrapper") != std::string::npos);

	const std::string readme =
		readFile("examples/modding/typed-pdxxx-basic/README.md");
	REQUIRE(readme.find("The content units are the typed `*.pdxxx` asset archives") != std::string::npos);
	REQUIRE(readme.find("Change any `.pdxxx` extension to `.zip`") != std::string::npos);
	REQUIRE(readme.find("The GLTF, OBJ, and INI files inside each archive are the authored data") != std::string::npos);
	REQUIRE(readme.find("`.pdmod` is not the authoring format") != std::string::npos);

	const char *requiredFiles[] = {
		"mod.json",
		"heads/tri_head.pdhead",
		"bodies/tri_body.pdbody",
		"arenas/tri_arena.pdarena",
		"meshes/tri_mesh.pdmesh",
		"weapons/tri_weapon.pdweapon",
		"animations/weapon_idle.pdanim",
		"animations/character_skeletal.pdanim",
		"audio/sfx/tri_click.pdsfx",
		"audio/voice/tri_voice.pdvoice",
		"audio/music/tri_song.pdsong",
		"ui/tri_reticle.pdui",
		"fonts/tri_font.pdfont",
		"lang/tri_lang.pdlang",
		"scenarios/tri_scenario.pdscenario",
	};

	for (const char *rel : requiredFiles) {
		const std::filesystem::path path = root / rel;
		INFO(path.generic_string());
		REQUIRE(std::filesystem::is_regular_file(path));
		const std::string pathText = path.generic_string();
		REQUIRE_FALSE(readFile(pathText.c_str()).empty());
	}

	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const std::string rel =
			std::filesystem::relative(entry.path(), root).generic_string();
		INFO(rel);
		REQUIRE(rel.find(".pdmod") == std::string::npos);
		REQUIRE(rel.find(".bin") == std::string::npos);
	}

	const std::string manifest =
		readFile("examples/modding/typed-pdxxx-basic/mod.json");
	REQUIRE(manifest.find("\"id\": \"example_typed_pdxxx_basic\"") != std::string::npos);
	REQUIRE(manifest.find("Wrap as .pdmod only") != std::string::npos);

	const std::string headArchivePath =
		"examples/modding/typed-pdxxx-basic/heads/tri_head.pdhead";
	const std::string bodyArchivePath =
		"examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody";
	const std::string arenaArchivePath =
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
	const std::string meshArchivePath =
		"examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh";
	const std::string weaponArchiveFullPath =
		"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
	const std::string weaponArchivePath =
		"examples/modding/typed-pdxxx-basic/animations/weapon_idle.pdanim";
	const std::string skeletalArchivePath =
		"examples/modding/typed-pdxxx-basic/animations/character_skeletal.pdanim";

	const std::string head = readArchiveEntryText(headArchivePath.c_str(), "head.ini");
	const std::string model = readArchiveEntryText(headArchivePath.c_str(), "model.gltf");
	REQUIRE(head.find("catalog_id = example:tri_head") != std::string::npos);
	REQUIRE(head.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(model.find("\"meshes\"") != std::string::npos);
	REQUIRE(model.find("\"TEXCOORD_0\"") != std::string::npos);
	REQUIRE(model.find("\"baseColorTexture\"") != std::string::npos);
	REQUIRE(model.find("\"uri\": \"texture.png\"") != std::string::npos);
	REQUIRE(model.find("data:application/octet-stream;base64,") != std::string::npos);

	const std::string body = readArchiveEntryText(bodyArchivePath.c_str(), "body.ini");
	const std::string bodyModel = readArchiveEntryText(bodyArchivePath.c_str(), "model.gltf");
	REQUIRE(body.find("catalog_id = example:tri_body") != std::string::npos);
	REQUIRE(body.find("rig_class = human_male_neck_standard") != std::string::npos);
	REQUIRE(body.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(body.find("hand_model_file = hand.gltf") != std::string::npos);
	REQUIRE(bodyModel.find("\"skins\"") != std::string::npos);
	REQUIRE(bodyModel.find("\"JOINTS_0\"") != std::string::npos);
	REQUIRE(bodyModel.find("\"WEIGHTS_0\"") != std::string::npos);
	REQUIRE(bodyModel.find("\"TEXCOORD_0\"") != std::string::npos);

	const std::string arena = readArchiveEntryText(arenaArchivePath.c_str(), "arena.ini");
	const std::string geometry = readArchiveEntryText(arenaArchivePath.c_str(), "geometry.obj");
	const std::string pads = readArchiveEntryText(arenaArchivePath.c_str(), "pads.ini");
	const std::string setup = readArchiveEntryText(arenaArchivePath.c_str(), "setup.ini");
	REQUIRE(arena.find("catalog_id = example:tri_arena") != std::string::npos);
	REQUIRE(arena.find("geometry_file = geometry.obj") != std::string::npos);
	REQUIRE(arena.find("pads_file = pads.ini") != std::string::npos);
	REQUIRE(arena.find("setup_file = setup.ini") != std::string::npos);
	REQUIRE(geometry.find("vt 0 0") != std::string::npos);
	REQUIRE(geometry.find("f 1/1 2/2 3/3") != std::string::npos);
	REQUIRE(pads.find("default_spawn = 0,0,0") != std::string::npos);
	REQUIRE(setup.find("props = none") != std::string::npos);

	const std::string mesh = readArchiveEntryText(meshArchivePath.c_str(), "model.ini");
	const std::string meshModel = readArchiveEntryText(meshArchivePath.c_str(), "model.gltf");
	REQUIRE(mesh.find("catalog_id = example:tri_mesh") != std::string::npos);
	REQUIRE(mesh.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(meshModel.find("\"TEXCOORD_0\"") != std::string::npos);
	REQUIRE(meshModel.find("\"baseColorTexture\"") != std::string::npos);

	const std::string weapon = readArchiveEntryText(weaponArchiveFullPath.c_str(), "weapon.ini");
	const std::string weaponGraph = readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior.graph.json");
	REQUIRE(weapon.find("catalog_id = example:tri_weapon") != std::string::npos);
	REQUIRE(weapon.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(weapon.find("behavior_graph = behavior.graph.json") != std::string::npos);
	REQUIRE(weaponGraph.find("\"schema\": \"pd.weapon_graph.v1\"") != std::string::npos);
	REQUIRE(weaponGraph.find("\"asset_id\": \"example:tri_weapon\"") != std::string::npos);
	REQUIRE(weapon.find("hand_model_file = hand.gltf") != std::string::npos);
	REQUIRE(weapon.find("texture_file = weapon_texture.png") != std::string::npos);
	REQUIRE(weapon.find("animation_file = reload.gltf") != std::string::npos);
	REQUIRE(weapon.find("file_path = fire.wav") != std::string::npos);

	const std::string weaponAnim = readArchiveEntryText(weaponArchivePath.c_str(), "animation.ini");
	const std::string weaponGltf = readArchiveEntryText(weaponArchivePath.c_str(), "animation.gltf");
	const std::string skeletalAnim = readArchiveEntryText(skeletalArchivePath.c_str(), "animation.ini");
	const std::string skeletalGltf = readArchiveEntryText(skeletalArchivePath.c_str(), "animation.gltf");
	REQUIRE(weaponAnim.find("catalog_id = example:weapon_idle") != std::string::npos);
	REQUIRE(weaponAnim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(weaponGltf.find("\"animations\"") != std::string::npos);
	REQUIRE(skeletalAnim.find("catalog_id = example:character_skeletal") != std::string::npos);
	REQUIRE(skeletalAnim.find("category = character_animation") != std::string::npos);
	REQUIRE(skeletalGltf.find("\"path\": \"translation\"") != std::string::npos);

	REQUIRE_FALSE(std::filesystem::exists(root / "heads/tri_head/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(root / "bodies/tri_body/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(root / "arenas/tri_arena/geometry.obj"));
	REQUIRE_FALSE(std::filesystem::exists(root / "weapons/tri_weapon/model.gltf"));
	REQUIRE_FALSE(std::filesystem::exists(root / "animations/weapon_idle/animation.gltf"));
}

TEST_CASE("typed pdxxx example archives carry the implemented asset payloads",
          "[modding][pdxxx][examples][static][c3812][archive-inventory]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";
	REQUIRE(std::filesystem::is_directory(root));

	const std::vector<ExpectedPdxxxArchive> implementedArchives = {
		{
			"head",
			".pdhead",
			"heads/tri_head.pdhead",
			{ "head.ini", "model.gltf", "texture.png" },
		},
		{
			"body",
			".pdbody",
			"bodies/tri_body.pdbody",
			{ "body.ini", "model.gltf", "hand.gltf", "texture.png" },
		},
		{
			"arena",
			".pdarena",
			"arenas/tri_arena.pdarena",
			{ "arena.ini", "geometry.obj", "arena.mtl", "arena_texture.png",
			  "pads.ini", "setup.ini" },
		},
		{
			"mesh/model",
			".pdmesh",
			"meshes/tri_mesh.pdmesh",
			{ "model.ini", "model.gltf", "texture.png" },
		},
		{
			"weapon",
			".pdweapon",
			"weapons/tri_weapon.pdweapon",
			{ "weapon.ini", "model.gltf", "hand.gltf", "weapon_texture.png",
			  "texture.png", "reload.gltf", "fire.wav", "behavior.graph.json" },
		},
		{
			"weapon animation",
			".pdanim",
			"animations/weapon_idle.pdanim",
			{ "animation.ini", "animation.gltf" },
		},
		{
			"character animation",
			".pdanim",
			"animations/character_skeletal.pdanim",
			{ "animation.ini", "animation.gltf" },
		},
		{
			"sound effect",
			".pdsfx",
			"audio/sfx/tri_click.pdsfx",
			{ "sound.ini", "sample.wav" },
		},
		{
			"voice",
			".pdvoice",
			"audio/voice/tri_voice.pdvoice",
			{ "voice.ini", "sample.wav" },
		},
		{
			"music",
			".pdsong",
			"audio/music/tri_song.pdsong",
			{ "music.ini", "track.wav" },
		},
		{
			"UI texture",
			".pdui",
			"ui/tri_reticle.pdui",
			{ "ui.ini", "texture.png" },
		},
		{
			"font",
			".pdfont",
			"fonts/tri_font.pdfont",
			{ "font.ini", "font.otf" },
		},
		{
			"language",
			".pdlang",
			"lang/tri_lang.pdlang",
			{ "lang.ini", "strings.tsv" },
		},
		{
			"scenario",
			".pdscenario",
			"scenarios/tri_scenario.pdscenario",
			{ "scenario.ini", "rooms.obj", "scenario.mtl", "room_texture.png",
			  "props.ini", "objectives.ini", "pads.ini", "setup.ini" },
		},
	};

	for (const ExpectedPdxxxArchive &spec : implementedArchives) {
		const std::filesystem::path archivePath = root / spec.relPath;
		INFO(spec.assetType << " " << archivePath.generic_string());
		REQUIRE(std::filesystem::is_regular_file(archivePath));

		OpenArchive opened;
		opened.archive = modArchiveOpen(archivePath.string().c_str());
		REQUIRE(opened.archive != nullptr);

		for (const char *entryPath : spec.requiredEntries) {
			INFO(spec.relPath << "::" << entryPath);
			s32 idx = modArchiveFindEntry(opened.archive, entryPath);
			REQUIRE(idx >= 0);
			REQUIRE(modArchiveGetEntrySize(opened.archive, idx) > 0);
		}

		for (s32 i = 0; i < modArchiveGetEntryCount(opened.archive); i++) {
			const char *entryName = modArchiveGetEntryName(opened.archive, i);
			REQUIRE(entryName != nullptr);
			INFO(spec.relPath << "::" << entryName);
			REQUIRE(std::string(entryName).find(".bin") == std::string::npos);
			REQUIRE(std::string(entryName).find("..") == std::string::npos);
		}
	}
}

TEST_CASE("typed pdxxx example archives keep declared source refs self-contained",
          "[modding][pdxxx][examples][static][c3812][archive-refs]") {
	const std::filesystem::path root =
		"examples/modding/typed-pdxxx-basic";

	struct ArchiveRefSpec {
		const char *relPath;
		const char *descriptorEntry;
		std::vector<const char *> iniPathKeys;
		std::vector<const char *> gltfEntries;
		std::vector<const char *> objEntries;
	};

	const std::vector<ArchiveRefSpec> specs = {
		{
			"heads/tri_head.pdhead",
			"head.ini",
			{ "model_file" },
			{ "model.gltf" },
			{},
		},
		{
			"bodies/tri_body.pdbody",
			"body.ini",
			{ "model_file", "hand_model_file" },
			{ "model.gltf", "hand.gltf" },
			{},
		},
		{
			"arenas/tri_arena.pdarena",
			"arena.ini",
			{ "geometry_file", "pads_file", "setup_file" },
			{},
			{ "geometry.obj" },
		},
		{
			"meshes/tri_mesh.pdmesh",
			"model.ini",
			{ "model_file" },
			{ "model.gltf" },
			{},
		},
		{
			"weapons/tri_weapon.pdweapon",
			"weapon.ini",
			{ "model_file", "hand_model_file", "texture_file",
			  "animation_file", "file_path", "behavior_graph" },
			{ "model.gltf", "hand.gltf", "reload.gltf",
			  "behavior.graph.json" },
			{},
		},
		{
			"animations/weapon_idle.pdanim",
			"animation.ini",
			{ "animation_file" },
			{ "animation.gltf" },
			{},
		},
		{
			"animations/character_skeletal.pdanim",
			"animation.ini",
			{ "animation_file" },
			{ "animation.gltf" },
			{},
		},
		{
			"audio/sfx/tri_click.pdsfx",
			"sound.ini",
			{ "file_path" },
			{},
			{},
		},
		{
			"audio/voice/tri_voice.pdvoice",
			"voice.ini",
			{ "file_path" },
			{},
			{},
		},
		{
			"audio/music/tri_song.pdsong",
			"music.ini",
			{ "file_path" },
			{},
			{},
		},
		{
			"ui/tri_reticle.pdui",
			"ui.ini",
			{ "texture_file" },
			{},
			{},
		},
		{
			"fonts/tri_font.pdfont",
			"font.ini",
			{ "font_file" },
			{},
			{},
		},
		{
			"lang/tri_lang.pdlang",
			"lang.ini",
			{ "strings_file" },
			{},
			{},
		},
		{
			"scenarios/tri_scenario.pdscenario",
			"scenario.ini",
			{ "rooms_file", "props_file", "objectives_file",
			  "pads_file", "setup_file" },
			{},
			{ "rooms.obj" },
		},
	};

	for (const ArchiveRefSpec &spec : specs) {
		const std::filesystem::path archivePath = root / spec.relPath;
		INFO(archivePath.generic_string());
		REQUIRE(std::filesystem::is_regular_file(archivePath));

		OpenArchive opened;
		opened.archive = modArchiveOpen(archivePath.string().c_str());
		REQUIRE(opened.archive != nullptr);

		requireIniPathRefsResolve(opened.archive, spec.relPath,
			spec.descriptorEntry, spec.iniPathKeys);

		for (const char *gltfEntry : spec.gltfEntries) {
			requireGltfUrisResolve(opened.archive, spec.relPath, gltfEntry);
		}
		for (const char *objEntry : spec.objEntries) {
			requireObjRefsResolve(opened.archive, spec.relPath, objEntry);
		}
	}
}

TEST_CASE("Modding Hub exposes typed pdxxx examples as pack input",
          "[modding][pdxxx][examples][static][c3811]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!hub.empty());

	REQUIRE(hub.find("Typed .pdxxx samples: examples/modding/typed-pdxxx-basic/") !=
	        std::string::npos);
	REQUIRE(hub.find("Use Sample Folder") != std::string::npos);
	REQUIRE(hub.find("examples/modding/typed-pdxxx-basic/") != std::string::npos);
	REQUIRE(hub.find("mods/typed-pdxxx-basic.pdmod") != std::string::npos);
	REQUIRE(hub.find("Edit the .pdxxx archives first; pack .pdmod only for transport.") !=
	        std::string::npos);
	REQUIRE(hub.find(".pdmod output is for sharing, Public Mods, or online delivery") !=
	        std::string::npos);
	REQUIRE(hub.find("editable content is inside the typed .pdxxx asset archives") !=
	        std::string::npos);
}

TEST_CASE("Modding Hub exposes weapon graph browser tab",
          "[modding][pdxxx][weapon_graph][ui][c3814]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!hub.empty());

	REQUIRE(hub.find("\"Weapons\"") != std::string::npos);
	REQUIRE(hub.find("renderWeaponTool") != std::string::npos);
	REQUIRE(hub.find("assetCatalogGetByIndex") != std::string::npos);
	REQUIRE(hub.find("ASSET_WEAPON") != std::string::npos);
	REQUIRE(hub.find("weaponGraphArchiveReadTextFile") != std::string::npos);
	REQUIRE(hub.find("behavior.graph.json") != std::string::npos);
	REQUIRE(hub.find("nested_payloads.json") != std::string::npos);
	REQUIRE(hub.find("weapon.ini") != std::string::npos);
	REQUIRE(hub.find("s_WeaponSelectedId") != std::string::npos);
}

TEST_CASE("Modding Hub weapon tool supports template imports and pdweapon save",
          "[modding][pdxxx][weapon_graph][ui][editor][c3814]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!hub.empty());

	REQUIRE(hub.find("Use as Template") != std::string::npos);
	REQUIRE(hub.find("Open Template Editor") != std::string::npos);
	REQUIRE(hub.find("Weapon Template Editor") != std::string::npos);
	REQUIRE(hub.find("Back to Weapon Browser") != std::string::npos);
	REQUIRE(hub.find("weaponToolStartTemplate") != std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateEditor") != std::string::npos);
	REQUIRE(hub.find("s_WeaponTemplateMenuOpen") != std::string::npos);
	REQUIRE(hub.find("weaponToolSaveCustom") != std::string::npos);
	REQUIRE(hub.find("Save Weapon Mod") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Template\")") == std::string::npos);
	REQUIRE(hub.find("modArchiveBegin(archivePath)") != std::string::npos);
	REQUIRE(hub.find("weaponCopyTemplatePayloads") != std::string::npos);
	REQUIRE(hub.find("modArchiveAddFileDisk") != std::string::npos);
	REQUIRE(hub.find("weaponAddCatalogAssetArchive") != std::string::npos);
	REQUIRE(hub.find("weaponAddArchiveRefPayload") != std::string::npos);
	REQUIRE(hub.find("WEAPON_IMPORT_PROJECTILE") != std::string::npos);
	REQUIRE(hub.find("WEAPON_IMPORT_ENTITY") != std::string::npos);
	REQUIRE(hub.find("Import Projectile") != std::string::npos);
	REQUIRE(hub.find("Import Entity") != std::string::npos);
	REQUIRE(hub.find("dependency_closure = embedded.v2") != std::string::npos);
	REQUIRE(hub.find("projectile_archive = %s") != std::string::npos);
	REQUIRE(hub.find("entity_archive = %s") != std::string::npos);
	REQUIRE(hub.find("weaponGraphValidateJson") != std::string::npos);
	REQUIRE(hub.find("mods/Weapons/%s") != std::string::npos);
	REQUIRE(hub.find("pdguiFileBrowserOpen(label, \"mods\", filters)") !=
	        std::string::npos);
	REQUIRE(hub.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(hub.find("ASSET_ENTITY") != std::string::npos);
}

TEST_CASE("Modding Hub weapon tool builds visual graph modules without raw JSON authoring",
          "[modding][pdxxx][weapon_graph][ui][editor][c3814]") {
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	const std::string editor =
		readFile("port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	const std::string header =
		readFile("port/include/pdgui_weapon_graph_node_editor.h");
	REQUIRE(!hub.empty());
	REQUIRE(!editor.empty());
	REQUIRE(!header.empty());

	REQUIRE(hub.find("pdgui_weapon_graph_node_editor.h") != std::string::npos);
	REQUIRE(hub.find("Weapon Behavior Graph") != std::string::npos);
	REQUIRE(hub.find("pdguiWeaponGraphNodeEditorRender") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderLoadEditModelFromJson") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphNodes") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphEdges") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderSyncJson") != std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphContextDefs") != std::string::npos);
	REQUIRE(hub.find("shared_context") != std::string::npos);
	REQUIRE(hub.find("subgraphs") != std::string::npos);
	REQUIRE(hub.find("context_refs") != std::string::npos);
	REQUIRE(hub.find("subgraph") != std::string::npos);
	REQUIRE(hub.find("Seed Single Shot") != std::string::npos);
	REQUIRE(hub.find("Seed Dual Fire Modes") != std::string::npos);
	REQUIRE(hub.find("Seed Automatic") != std::string::npos);
	REQUIRE(hub.find("Seed Projectile") != std::string::npos);
	REQUIRE(hub.find("Seed Mine Link") != std::string::npos);
	REQUIRE(hub.find("Seed Laptop Control") != std::string::npos);
	REQUIRE(editor.find("Shared Context") != std::string::npos);
	REQUIRE(hub.find("Owner Player") != std::string::npos);
	REQUIRE(hub.find("Detonator Link") != std::string::npos);
	REQUIRE(hub.find("Target Policy Override") != std::string::npos);
	REQUIRE(hub.find("hacked_by_player") != std::string::npos);
	REQUIRE(hub.find("Advanced JSON") != std::string::npos);
	REQUIRE(hub.find("\\\"editor\\\"") != std::string::npos);
	REQUIRE(hub.find("\\\"layout\\\"") != std::string::npos);
	REQUIRE(hub.find("\"fire.hitscan\"") != std::string::npos);
	REQUIRE(hub.find("\"spawn.fired_projectile\"") != std::string::npos);
	REQUIRE(hub.find("\"special.remote_detonator\"") != std::string::npos);
	REQUIRE(hub.find("\"gate.target_lock\"") != std::string::npos);
	REQUIRE(hub.find("\\\"exports\\\"") != std::string::npos);
	REQUIRE(editor.find("imgui_node_editor.h") != std::string::npos);
	REQUIRE(editor.find("Weapon Behavior Graph Canvas") != std::string::npos);
	REQUIRE(editor.find("QueryNewLink") != std::string::npos);
	REQUIRE(editor.find("AcceptNewItem") != std::string::npos);
	REQUIRE(editor.find("BeginDelete") != std::string::npos);
	REQUIRE(editor.find("Add Node") != std::string::npos);
	REQUIRE(editor.find("Delete Node") != std::string::npos);
	REQUIRE(editor.find("Duplicate Node") != std::string::npos);
	REQUIRE(editor.find("Set Primary") != std::string::npos);
	REQUIRE(editor.find("Set Secondary") != std::string::npos);
	REQUIRE(editor.find("Break Pin Links") != std::string::npos);
	REQUIRE(editor.find("Alt-click pin") != std::string::npos);
	REQUIRE(editor.find("Node Context Refs") != std::string::npos);
	REQUIRE(editor.find("Choose a compatible node from the add-node menu") !=
	        std::string::npos);
	REQUIRE(editor.find("pdguiWeaponGraphModelLoadJson") != std::string::npos);
	REQUIRE(header.find("PdWeaponGraphEditModel") != std::string::npos);
	REQUIRE(header.find("PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY") !=
	        std::string::npos);
}

TEST_CASE("Modding Hub vendors imgui-node-editor for the in-game graph canvas",
          "[modding][pdxxx][weapon_graph][ui][editor][vendor][c3814]") {
	const std::string cmake = readFile("CMakeLists.txt");
	const std::string license =
		readFile("port/external/imgui-node-editor/LICENSE");
	const std::string api =
		readFile("port/external/imgui-node-editor/imgui_node_editor.h");
	const std::string backend = readFile("port/fast3d/pdgui_backend.cpp");
	REQUIRE(!cmake.empty());
	REQUIRE(!license.empty());
	REQUIRE(!api.empty());
	REQUIRE(!backend.empty());

	REQUIRE(cmake.find("port/external/imgui-node-editor") != std::string::npos);
	REQUIRE(license.find("MIT License") != std::string::npos);
	REQUIRE(api.find("CreateEditor") != std::string::npos);
	REQUIRE(api.find("QueryNewLink") != std::string::npos);
	REQUIRE(api.find("ShowBackgroundContextMenu") != std::string::npos);
	REQUIRE(backend.find("pdguiWeaponGraphNodeEditorInit") != std::string::npos);
	REQUIRE(backend.find("pdguiWeaponGraphNodeEditorShutdown") !=
	        std::string::npos);
}

TEST_CASE("base arena extractor emits zip-openable pdarena archives",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");
	REQUIRE(!arena.empty());

	REQUIRE(arena.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(arena.find("modArchiveBegin(full)") != std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileMem(aw, \"arena.ini\"") != std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileMem(aw, \"manifest.json\"") != std::string::npos);
	REQUIRE(arena.find("s_copyArchiveEntriesWithPrefix") != std::string::npos);
	REQUIRE(arena.find("\"scenario/scenario.ini\"") != std::string::npos);
	REQUIRE(arena.find("\"scenario/rooms.obj\"") != std::string::npos);
	REQUIRE(arena.find("\"scenario/visual/scene.obj\"") != std::string::npos);
	REQUIRE(arena.find("\"scenario/visual/scene.mtl\"") != std::string::npos);
	REQUIRE(arena.find("\"scenario/visual/materials.tsv\"") != std::string::npos);
	REQUIRE(arena.find("\"scenario/visual/export_version.txt\"") !=
	        std::string::npos);
	REQUIRE(arena.find("scenario_root = scenario") != std::string::npos);
	REQUIRE(arena.find("\\\"scenario_root\\\": \\\"scenario\\\"") !=
	        std::string::npos);
	const auto scenarioEmit = arena.find("s_emitOnePdscenario(a, c->scenarios_dir");
	const auto arenaEmit = arena.find("s_emitOnePdarena(a, i, c->arenas_dir");
	REQUIRE(scenarioEmit != std::string::npos);
	REQUIRE(arenaEmit != std::string::npos);
	REQUIRE(scenarioEmit < arenaEmit);
	REQUIRE(arena.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(arena.find("Emit one .pdarena JSON") == std::string::npos);
}

TEST_CASE("base scenario extractor emits standard map and text payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");
	REQUIRE(!arena.empty());

	REQUIRE(arena.find("s_buildTilesExports") != std::string::npos);
	REQUIRE(arena.find("s_buildPadsTsv") != std::string::npos);
	REQUIRE(arena.find("s_buildWordsTsv") != std::string::npos);
	REQUIRE(arena.find("#include \"lib/rzip.h\"") != std::string::npos);
	REQUIRE(arena.find("rzipIs1173") != std::string::npos);
	REQUIRE(arena.find("rzipInflate") != std::string::npos);
	REQUIRE(arena.find("preprocessTilesFile") != std::string::npos);
	REQUIRE(arena.find("preprocessPadsFile") != std::string::npos);
	REQUIRE(arena.find("preprocessSetupFile") != std::string::npos);
	REQUIRE(arena.find("Stage preprocessors share process-global scratch state") !=
	        std::string::npos);
	REQUIRE(arena.find("rooms.obj") != std::string::npos);
	REQUIRE(arena.find("scenario.mtl") != std::string::npos);
	REQUIRE(arena.find("s_buildBgVisualExports") != std::string::npos);
	REQUIRE(arena.find("preprocessBgSection1") != std::string::npos);
	REQUIRE(arena.find("preprocessBgRoom") != std::string::npos);
	REQUIRE(arena.find("G_NOOP") != std::string::npos);
	REQUIRE(arena.find("G_TRI1") != std::string::npos);
	REQUIRE(arena.find("G_TRI4") != std::string::npos);
	REQUIRE(arena.find("texInflateZlib") != std::string::npos);
	REQUIRE(arena.find("texInflateNonZlib") != std::string::npos);
	REQUIRE(arena.find("visual/scene.obj") != std::string::npos);
	REQUIRE(arena.find("visual/scene.mtl") != std::string::npos);
	REQUIRE(arena.find("visual/materials.tsv") != std::string::npos);
	REQUIRE(arena.find("visual/export_version.txt") != std::string::npos);
	REQUIRE(arena.find("visual/textures/tex_%04x.tga") != std::string::npos);
	REQUIRE(arena.find("bg_visual_obj_mtl_tga_v2") != std::string::npos);
	REQUIRE(arena.find("mat_%03u_tex_%04x") != std::string::npos);
	REQUIRE(arena.find("texture_inventory_%04x") != std::string::npos);
	REQUIRE(arena.find("strncmp(mtl_texture_path, \"visual/\", 7)") !=
	        std::string::npos);
	REQUIRE(arena.find("blender_scene_file = visual/scene.obj") !=
	        std::string::npos);
	REQUIRE(arena.find("visual_scene_file = visual/scene.obj") !=
	        std::string::npos);
	REQUIRE(arena.find("visual_material_file = visual/scene.mtl") !=
	        std::string::npos);
	REQUIRE(arena.find("texture_manifest_file = visual/materials.tsv") !=
	        std::string::npos);
	REQUIRE(arena.find("visual_format = OBJ+MTL+TGA") != std::string::npos);
	REQUIRE(arena.find("visual_export_version = bg_visual_obj_mtl_tga_v2") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"visual_format\\\": \\\"OBJ+MTL+TGA\\\"") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"visual_export_version\\\": \\\"bg_visual_obj_mtl_tga_v2\\\"") !=
	        std::string::npos);
	REQUIRE(arena.find("tiles.tsv") != std::string::npos);
	REQUIRE(arena.find("pads.tsv") != std::string::npos);
	REQUIRE(arena.find("setup.tsv") != std::string::npos);
	REQUIRE(arena.find("mpsetup.tsv") != std::string::npos);
	REQUIRE(arena.find("visual_segments.tsv") != std::string::npos);
	REQUIRE(arena.find("geometry_file = rooms.obj") != std::string::npos);
	REQUIRE(arena.find("geometry_format = OBJ") != std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(dst_rel, \"rooms.obj\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(dst_rel, \"visual/scene.obj\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(dst_rel, \"visual/export_version.txt\")") !=
	        std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileMem(aw, \"rooms.obj\"") !=
	        std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileMem(aw, \"visual/export_version.txt\"") !=
	        std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileMem(aw, \"visual/scene.obj\"") !=
	        std::string::npos);
	REQUIRE(arena.find("rooms.obj.sha256") != std::string::npos);
	REQUIRE(arena.find("visual/export_version.txt.sha256") != std::string::npos);
	REQUIRE(arena.find("visual/scene.obj.sha256") != std::string::npos);
	REQUIRE(arena.find("visual/scene.mtl.sha256") != std::string::npos);
	REQUIRE(arena.find("visual/materials.tsv.sha256") != std::string::npos);
	REQUIRE(arena.find("tiles.tsv.sha256") != std::string::npos);
	REQUIRE(arena.find("pads.tsv.sha256") != std::string::npos);
	REQUIRE(arena.find("map_material.png") == std::string::npos);
	REQUIRE(arena.find("data:application/octet-stream;base64,") ==
	        std::string::npos);

	REQUIRE(arena.find("geometry_file = geometry.bin") == std::string::npos);
	REQUIRE(arena.find("\\\"geometry\\\": \\\"geometry.bin\\\"") ==
	        std::string::npos);
	REQUIRE(arena.find("tiles_file = tiles.bin") == std::string::npos);
	REQUIRE(arena.find("pads_file = pads.bin") == std::string::npos);
	REQUIRE(arena.find("setup_file = setup.bin") == std::string::npos);
	REQUIRE(arena.find("mpsetup_file = mpsetup.bin") == std::string::npos);
	REQUIRE(arena.find("s_addStageBin") == std::string::npos);
	REQUIRE(arena.find("modArchiveAddFileDisk") == std::string::npos);
}

TEST_CASE("base character extractors emit zip-openable pdhead and pdbody archives",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string head = readFile("port/src/romextract_pdhead.c");
	const std::string body = readFile("port/src/romextract_pdbody.c");
	REQUIRE(!head.empty());
	REQUIRE(!body.empty());

	REQUIRE(head.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(head.find("modArchiveBegin(full)") != std::string::npos);
	REQUIRE(head.find("modArchiveAddFileMem(aw, \"head.ini\"") != std::string::npos);
	REQUIRE(head.find("modArchiveAddFileMem(aw, \"manifest.json\"") != std::string::npos);
	REQUIRE(head.find("s_addRequiredArchiveFile(aw, \"mesh.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(head.find("s_existingArchiveHasEntry(relpath, \"mesh.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(head.find("mesh_archive = %s") != std::string::npos);
	REQUIRE(head.find("\\\"mesh_archive\\\": \\\"mesh.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(head.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(head.find("emits one .pdhead JSON file") == std::string::npos);

	REQUIRE(body.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(body.find("modArchiveBegin(full)") != std::string::npos);
	REQUIRE(body.find("modArchiveAddFileMem(aw, \"body.ini\"") != std::string::npos);
	REQUIRE(body.find("modArchiveAddFileMem(aw, \"manifest.json\"") != std::string::npos);
	REQUIRE(body.find("s_addRequiredArchiveFile(aw, \"mesh.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(body.find("s_addRequiredArchiveFile(aw, \"hand.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(body.find("s_existingArchiveHasEntry(relpath, \"mesh.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(body.find("s_existingArchiveHasEntry(relpath, \"hand.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(body.find("mesh_archive = %s") != std::string::npos);
	REQUIRE(body.find("hand_archive = hand.pdmesh") != std::string::npos);
	REQUIRE(body.find("\\\"mesh_archive\\\": \\\"mesh.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(body.find("\\\"hand_archive\\\": \\\"hand.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(body.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(body.find("emits one .pdbody JSON file") == std::string::npos);
}

TEST_CASE("base character extractor emits canonical pdcharacter archives",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string character = readFile("port/src/romextract_pdcharacter.c");
	const std::string header = readFile("port/include/romextract_pd.h");
	const std::string main = readFile("port/src/main.c");
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	const std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(!character.empty());

	REQUIRE(character.find(".pdcharacter") != std::string::npos);
	REQUIRE(character.find("modArchiveAddFileMem(aw, \"character.ini\"") !=
	        std::string::npos);
	REQUIRE(character.find("modArchiveAddFileDisk(aw, inner, full)") !=
	        std::string::npos);
	REQUIRE(character.find("\"body.pdbody\"") != std::string::npos);
	REQUIRE(character.find("\"head.pdhead\"") != std::string::npos);
	REQUIRE(character.find("\\\"dependency_closure\\\": \\\"embedded.v2\\\"") !=
	        std::string::npos);
	REQUIRE(character.find("dependency_closure = embedded.v2") !=
	        std::string::npos);
	REQUIRE(character.find("s_existingArchiveEntryContains") !=
	        std::string::npos);
	REQUIRE(character.find("s_existingArchiveHasEntry(dst_rel, \"body.pdbody\")") !=
	        std::string::npos);
	REQUIRE(character.find("s_existingArchiveHasEntry(dst_rel, \"head.pdhead\")") !=
	        std::string::npos);
	REQUIRE(character.find("romextract pdcharacter") != std::string::npos);
	REQUIRE(header.find("romExtractAllPdcharacter") != std::string::npos);
	REQUIRE(main.find("romExtractAllPdcharacter(0)") != std::string::npos);
	REQUIRE(scanner.find("\".pdcharacter\"") != std::string::npos);
	REQUIRE(scanner.find("\"character.ini\"") != std::string::npos);
	REQUIRE(packer.find("\".pdcharacter\"") != std::string::npos);
	REQUIRE(packer.find("\"characters\", \"character.ini\", \"character\"") !=
	        std::string::npos);

	REQUIRE(character.find(".pdweapon") == std::string::npos);
}

TEST_CASE("base non-weapon zip emitters include root editable descriptors",
          "[modding][pdxxx][base][static][c3812]") {
	struct DescriptorEmitter {
		const char *path;
		const char *descriptor;
	};
	const DescriptorEmitter emitters[] = {
		{ "port/src/romextract_pdmesh.c", "model.ini" },
		{ "port/src/romextract_pdanim.c", "animation.ini" },
		{ "port/src/romextract_pdanim_chr.c", "animation.ini" },
		{ "port/src/romextract_pdsfx.c", "sound.ini" },
		{ "port/src/romextract_pdsfx.c", "voice.ini" },
		{ "port/src/romextract_pdsong.c", "music.ini" },
		{ "port/fast3d/pdgui_theme.cpp", "ui.ini" },
		{ "port/src/romextract_pdfont.c", "font.ini" },
		{ "port/src/romextract_pdlang.c", "lang.ini" },
		{ "port/src/romextract_pdarena.c", "scenario.ini" },
	};

	for (const DescriptorEmitter &emitter : emitters) {
		const std::string src = readFile(emitter.path);
		INFO(emitter.path);
		INFO(emitter.descriptor);
		REQUIRE(!src.empty());
		REQUIRE(src.find(emitter.descriptor) != std::string::npos);
		const bool literalAdd =
			src.find(std::string("modArchiveAddFileMem(aw, \"") +
			         emitter.descriptor + "\"") != std::string::npos;
		const bool descriptorVariableAdd =
			src.find("modArchiveAddFileMem(aw, descriptor_name") !=
			std::string::npos;
		REQUIRE((literalAdd || descriptorVariableAdd));
	}
}

TEST_CASE("base audio extractors emit accessible wav payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string audio = readFile("port/src/romextract_pdsfx.c");
	REQUIRE(!audio.empty());

	REQUIRE(audio.find("s_buildWavPayload") != std::string::npos);
	REQUIRE(audio.find("s_decodeAdpcmFrame") != std::string::npos);
	REQUIRE(audio.find("AL_ADPCM_WAVE") != std::string::npos);
	REQUIRE(audio.find("AL_RAW16_WAVE") != std::string::npos);
	REQUIRE(audio.find("modArchiveAddFileMem(aw, \"sample.wav\"") !=
	        std::string::npos);
	REQUIRE(audio.find("modArchiveAddFileMem(aw, \"sample.wav.sha256\"") !=
	        std::string::npos);
	REQUIRE(audio.find("s_existingArchiveHasEntry(dst_rel, \"sample.wav\")") !=
	        std::string::npos);
	REQUIRE(audio.find("file_path = sample.wav") != std::string::npos);
	REQUIRE(audio.find("\\\"data\\\": \\\"sample.wav\\\"") !=
	        std::string::npos);
	REQUIRE(audio.find("source_format = %s") != std::string::npos);

	REQUIRE(audio.find("modArchiveAddFileMem(aw, \"sample.bin\"") ==
	        std::string::npos);
	REQUIRE(audio.find("file_path = sample.bin") == std::string::npos);
	REQUIRE(audio.find("\\\"data\\\": \\\"sample.bin\\\"") ==
	        std::string::npos);
}

TEST_CASE("base language extractor emits editable tsv payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string lang = readFile("port/src/romextract_pdlang.c");
	REQUIRE(!lang.empty());

	REQUIRE(lang.find("s_buildStringsTsv") != std::string::npos);
	REQUIRE(lang.find("s_langStringCount") != std::string::npos);
	REQUIRE(lang.find("s_existingArchiveHasEntry(dst_rel, \"strings.tsv\")") !=
	        std::string::npos);
	REQUIRE(lang.find("strings_file = strings.tsv") != std::string::npos);
	REQUIRE(lang.find("modArchiveAddFileMem(aw, \"strings.tsv\"") !=
	        std::string::npos);
	REQUIRE(lang.find("modArchiveAddFileMem(aw, \"strings.tsv.sha256\"") !=
	        std::string::npos);
	REQUIRE(lang.find("\\\"data\\\": \\\"strings.tsv\\\"") !=
	        std::string::npos);

	REQUIRE(lang.find("modArchiveAddFileDisk(aw, \"data.bin\"") ==
	        std::string::npos);
	REQUIRE(lang.find("strings_file = data.bin") == std::string::npos);
	REQUIRE(lang.find("\\\"data\\\": \\\"data.bin\\\"") == std::string::npos);
}

TEST_CASE("base font extractor emits editable bitmap font payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string font = readFile("port/src/romextract_pdfont.c");
	REQUIRE(!font.empty());

	REQUIRE(font.find("s_buildFontExports") != std::string::npos);
	REQUIRE(font.find("glyphs.pgm") != std::string::npos);
	REQUIRE(font.find("metrics.tsv") != std::string::npos);
	REQUIRE(font.find("kerning.tsv") != std::string::npos);
	REQUIRE(font.find("font_format = bitmap_ci4_atlas") != std::string::npos);
	REQUIRE(font.find("font_file = glyphs.pgm") != std::string::npos);
	REQUIRE(font.find("metrics_file = metrics.tsv") != std::string::npos);
	REQUIRE(font.find("kerning_file = kerning.tsv") != std::string::npos);
	REQUIRE(font.find("s_existingArchiveHasEntry(dst_rel, \"glyphs.pgm\")") !=
	        std::string::npos);
	REQUIRE(font.find("s_existingArchiveHasEntry(dst_rel, \"metrics.tsv\")") !=
	        std::string::npos);
	REQUIRE(font.find("modArchiveAddFileMem(aw, \"glyphs.pgm\"") !=
	        std::string::npos);
	REQUIRE(font.find("modArchiveAddFileMem(aw, \"metrics.tsv\"") !=
	        std::string::npos);
	REQUIRE(font.find("modArchiveAddFileMem(aw, \"kerning.tsv\"") !=
	        std::string::npos);
	REQUIRE(font.find("\"data\": \"data.bin\"") == std::string::npos);
	REQUIRE(font.find("font_file = data.bin") == std::string::npos);
	REQUIRE(font.find("modArchiveAddFileDisk(aw, \"data.bin\"") ==
	        std::string::npos);
}

TEST_CASE("base song extractor emits midi and editable event payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string song = readFile("port/src/romextract_pdsong.c");
	REQUIRE(!song.empty());

	REQUIRE(song.find("s_exportSequence") != std::string::npos);
	REQUIRE(song.find("s_buildMidiFile") != std::string::npos);
	REQUIRE(song.find("rzipInflate") != std::string::npos);
	REQUIRE(song.find("preprocessALCMidiHdr") != std::string::npos);
	REQUIRE(song.find("s_existingArchiveHasSongPayloads") !=
	        std::string::npos);
	REQUIRE(song.find("sequence.mid") != std::string::npos);
	REQUIRE(song.find("sequence.tsv") != std::string::npos);
	REQUIRE(song.find("music_file = sequence.mid") != std::string::npos);
	REQUIRE(song.find("midi_file = sequence.mid") != std::string::npos);
	REQUIRE(song.find("events_file = sequence.tsv") != std::string::npos);
	REQUIRE(song.find("modArchiveAddFileMem(aw, \"sequence.mid\"") !=
	        std::string::npos);
	REQUIRE(song.find("modArchiveAddFileMem(aw, \"sequence.tsv\"") !=
	        std::string::npos);
	REQUIRE(song.find("sequence.mid.sha256") != std::string::npos);
	REQUIRE(song.find("sequence.tsv.sha256") != std::string::npos);

	REQUIRE(song.find("music_file = data.bin") == std::string::npos);
	REQUIRE(song.find("\\\"data\\\": \\\"data.bin\\\"") == std::string::npos);
	REQUIRE(song.find("modArchiveAddFileMem(aw, \"data.bin\"") ==
	        std::string::npos);
}

TEST_CASE("base character animation extractor emits editable tsv payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string anim = readFile("port/src/romextract_pdanim_chr.c");
	REQUIRE(!anim.empty());

	REQUIRE(anim.find("s_buildHeaderTsv") != std::string::npos);
	REQUIRE(anim.find("s_buildFramesTsv") != std::string::npos);
	REQUIRE(anim.find("s_existingArchiveHasAnimPayloads") !=
	        std::string::npos);
	REQUIRE(anim.find("header.tsv") != std::string::npos);
	REQUIRE(anim.find("frames.tsv") != std::string::npos);
	REQUIRE(anim.find("header_file = header.tsv") != std::string::npos);
	REQUIRE(anim.find("frames_file = frames.tsv") != std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"header.tsv\"") !=
	        std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"frames.tsv\"") !=
	        std::string::npos);
	REQUIRE(anim.find("header.tsv.sha256") != std::string::npos);
	REQUIRE(anim.find("frames.tsv.sha256") != std::string::npos);

	REQUIRE(anim.find("frames_file = frames.bin") == std::string::npos);
	REQUIRE(anim.find("\\\"frames\\\": \\\"frames.bin\\\"") ==
	        std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"frames.bin\"") ==
	        std::string::npos);
}

TEST_CASE("base weapon animation extractor emits zip-openable opcode payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string anim = readFile("port/src/romextract_pdanim.c");
	REQUIRE(!anim.empty());

	REQUIRE(anim.find("s_existingArchiveHasAnimPayloads") !=
	        std::string::npos);
	REQUIRE(anim.find("modArchiveBegin(full)") != std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"animation.ini\"") !=
	        std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"manifest.json\"") !=
	        std::string::npos);
	REQUIRE(anim.find("modArchiveAddFileMem(aw, \"opcodes.json\"") !=
	        std::string::npos);
	REQUIRE(anim.find("category = weapon_animation") != std::string::npos);
	REQUIRE(anim.find("\\\"category\\\": \\\"weapon_animation\\\"") !=
	        std::string::npos);
	REQUIRE(anim.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(anim.find("emits one JSON") == std::string::npos);
}

TEST_CASE("base mesh extractor emits standard obj geometry payloads",
          "[modding][pdxxx][base][static][c3812]") {
	const std::string mesh = readFile("port/src/romextract_pdmesh.c");
	REQUIRE(!mesh.empty());

	REQUIRE(mesh.find("s_buildModelObj") != std::string::npos);
	REQUIRE(mesh.find("s_exportGdlToObj") != std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL \"model_obj_mtx_v8\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_FAST_CACHE_KIND \"pdmesh_model_obj_mtx_v8\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("#include \"preprocess.h\"") != std::string::npos);
	REQUIRE(mesh.find("#include \"game/modeldef.h\"") != std::string::npos);
	REQUIRE(mesh.find("#include \"lib/rzip.h\"") != std::string::npos);
	REQUIRE(mesh.find("s_loadSourceModelPreprocessed") != std::string::npos);
	REQUIRE(mesh.find("rzipIs1173") != std::string::npos);
	REQUIRE(mesh.find("rzipInflate") != std::string::npos);
	REQUIRE(mesh.find("preprocessModelFile") != std::string::npos);
	REQUIRE(mesh.find("preprocessGunFile") != std::string::npos);
	REQUIRE(mesh.find("s_modeldefOffsetsLookPromotable") !=
	        std::string::npos);
	REQUIRE(mesh.find("modelPromoteTypeToPointer") != std::string::npos);
	REQUIRE(mesh.find("modelPromoteOffsetsToPointers") != std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_MODEL_VMA") != std::string::npos);
	REQUIRE(mesh.find("rawaddr &= ~(uintptr_t)1") != std::string::npos);
	REQUIRE(mesh.find("uintptr_t src = ((uintptr_t)w1) & ~(uintptr_t)1") !=
	        std::string::npos);
	REQUIRE(mesh.find("G_VTX") != std::string::npos);
	REQUIRE(mesh.find("(w0 & 0xffffu) / sizeof(Vtx)") !=
	        std::string::npos);
	REQUIRE(mesh.find("(w0 >> 16) & 0xf") != std::string::npos);
	REQUIRE(mesh.find("seg == SPSEGMENT_MODEL_VTX") !=
	        std::string::npos);
	REQUIRE(mesh.find("((w0 >> 4) & 0xf) + 1") == std::string::npos);
	REQUIRE(mesh.find("G_MTX") != std::string::npos);
	REQUIRE(mesh.find("G_POPMTX") != std::string::npos);
	REQUIRE(mesh.find("s_objApplyMtxCommand") != std::string::npos);
	REQUIRE(mesh.find("SPSEGMENT_MODEL_MTX") != std::string::npos);
	REQUIRE(mesh.find("s_objBuildDefaultModelMatrices") != std::string::npos);
	REQUIRE(mesh.find("s_objMtxTransformPoint") != std::string::npos);
	REQUIRE(mesh.find("s_objNodeUnderHiddenGunToggle") != std::string::npos);
	REQUIRE(mesh.find("s_objHiddenGunToggleTargetsNode") != std::string::npos);
	REQUIRE(mesh.find("s_objStaticGunVertsLookDetachedEffect") != std::string::npos);
	REQUIRE(mesh.find("s_objMayCullDetachedGunEffects") != std::string::npos);
	REQUIRE(mesh.find("ctx.source_filenum = source_filenum") != std::string::npos);
	REQUIRE(mesh.find("FILE_GFALCON2LOD") != std::string::npos);
	REQUIRE(mesh.find("s_objTextLooksDetachedGunEffect") != std::string::npos);
	REQUIRE(mesh.find("s_exportGunDlToObj") != std::string::npos);
	REQUIRE(mesh.find("ctx.static_gun_model = loadtype == LOADTYPE_GUN") !=
	        std::string::npos);
	REQUIRE(mesh.find("MODELPART_0042") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_FALCON2_002E") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_FALCON2_002F") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_LASERLIQUID") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_MUZZLEFLASH1") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_MUZZLEFLASH2") != std::string::npos);
	REQUIRE(mesh.find("MODELPART_GUN_MUZZLEFLASH3") != std::string::npos);
	REQUIRE(mesh.find("G_TRI1") != std::string::npos);
	REQUIRE(mesh.find("G_TRI4") != std::string::npos);
	REQUIRE(mesh.find("(w1 >> 0)  & 0xf") != std::string::npos);
	REQUIRE(mesh.find("(w0 >> 12) & 0xf") != std::string::npos);
	REQUIRE(mesh.find("gdl[cmdidx].tri4") == std::string::npos);
	REQUIRE(mesh.find("model.obj") != std::string::npos);
	REQUIRE(mesh.find("model.mtl") != std::string::npos);
	REQUIRE(mesh.find("export_version.txt") != std::string::npos);
	REQUIRE(mesh.find("source_format = PD_MODELDEF") != std::string::npos);
	REQUIRE(mesh.find("format = OBJ") != std::string::npos);
	REQUIRE(mesh.find("obj_export_version = %s") != std::string::npos);
	REQUIRE(mesh.find("geometry_file = model.obj") != std::string::npos);
	REQUIRE(mesh.find("material_file = model.mtl") != std::string::npos);
	REQUIRE(mesh.find("model_matrix_reference_count = %u") != std::string::npos);
	REQUIRE(mesh.find("s_existingArchiveHasEntry(dst_rel, \"model.obj\")") !=
	        std::string::npos);
	REQUIRE(mesh.find("s_existingArchiveEntryContains(dst_rel, \"export_version.txt\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileMem(aw, \"export_version.txt\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileMem(aw, \"model.obj\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileMem(aw, \"model.mtl\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("model.obj.sha256") != std::string::npos);
	REQUIRE(mesh.find("model.mtl.sha256") != std::string::npos);
	REQUIRE(mesh.find("export_version.txt.sha256") != std::string::npos);
	REQUIRE(mesh.find("const char *wanted_hint = hint ? hint : \"\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("strcmp(jobs[i].hint, wanted_hint) == 0") !=
	        std::string::npos);
	REQUIRE(mesh.find("unique_mesh_jobs") != std::string::npos);

	REQUIRE(mesh.find("modeldef.tsv") == std::string::npos);
	REQUIRE(mesh.find("geometry_file = geometry.bin") == std::string::npos);
	REQUIRE(mesh.find("\\\"geometry\\\": \\\"geometry.bin\\\"") ==
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileDisk(aw, \"geometry.bin\"") ==
	        std::string::npos);
	REQUIRE(mesh.find("modArchiveAddFileMem(aw, \"geometry.bin\"") ==
	        std::string::npos);
	REQUIRE(mesh.find("s_buildModelObj((const u8 *)src_bytes") ==
	        std::string::npos);
	REQUIRE(mesh.find("OBJ export produced no triangles") !=
	        std::string::npos);
	REQUIRE(mesh.find("return -1;") != std::string::npos);
}
