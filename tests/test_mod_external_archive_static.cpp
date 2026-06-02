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

#include "pdgui_gameplay_graph_editor.h"

extern "C" {
#include "asset_archive_policy.h"
#include "asset_archive_writer.h"
#include "asset_mod_utility_contract.h"
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

TempArchive writeTypedArchiveEntries(const std::string &stem,
                                     const std::string &extension,
                                     const std::vector<std::pair<std::string, std::string>> &entries) {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-" + stem + "-" + std::to_string(stamp) + extension)
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

TEST_CASE("typed asset archive policy enforces the two-zone migration boundary",
          "[modding][pdxxx][policy][c3824]") {
	char err[256];

	auto clean = writeTypedArchiveEntries("policy-clean", ".pdweapon", {
		{ "weapon.ini",
		  "[weapon]\n"
		  "catalog_id = mod:weapon_policy_clean\n"
		  "manifest = _meta/manifest.json\n"
		  "model_file = model.obj\n" },
		{ "model.obj", "o model\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"weapon\" }\n" },
		{ "_meta/provenance.json", "{ \"source_path\": \"C:/rom/source/model.obj\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(clean.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	OpenArchive opened;
	opened.archive = modArchiveOpen(clean.path.string().c_str());
	REQUIRE(opened.archive != nullptr);
	const s32 metaIdx = assetArchiveFindMetadataEntry(opened.archive,
		ASSET_ARCHIVE_META_MANIFEST);
	REQUIRE(metaIdx >= 0);
	REQUIRE(std::string(modArchiveGetEntryName(opened.archive, metaIdx))
		== "_meta/manifest.json");

	auto legacyRoot = writeTypedArchiveEntries("policy-legacy-root", ".pdweapon", {
		{ "weapon.ini",
		  "[weapon]\n"
		  "catalog_id = mod:weapon_policy_legacy\n"
		  "manifest = manifest.json\n" },
		{ "manifest.json", "{ \"pd_kind\": \"weapon\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(legacyRoot.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_MIGRATION, err, sizeof(err)) == 0);
	REQUIRE(assetArchiveValidateFile(legacyRoot.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("archive root") != std::string::npos);

	auto legacyMesh = writeTypedArchiveEntries("policy-legacy-mesh", ".pdmesh", {
		{ "model.ini",
		  "[model]\n"
		  "catalog_id = mod:model_policy_legacy\n"
		  "geometry_file = model.obj\n" },
		{ "model.obj", "o mesh\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"mesh\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(legacyMesh.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_MIGRATION, err, sizeof(err)) == 0);
	REQUIRE(assetArchiveValidateFile(legacyMesh.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("mesh.ini") != std::string::npos);

	auto missingGltfUri = writeTypedArchiveEntries("policy-missing-gltf-uri", ".pdhead", {
		{ "head.ini",
		  "[head]\n"
		  "catalog_id = mod:head_policy_missing_gltf_uri\n"
		  "model_file = model.gltf\n" },
		{ "model.gltf",
		  "{ \"asset\": { \"version\": \"2.0\" }, "
		  "\"images\": [ { \"uri\": \"texture.png\" } ] }\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"head\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(missingGltfUri.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("texture.png") != std::string::npos);
	const std::string missingGltfBytes =
		readFile(missingGltfUri.path.string().c_str());
	REQUIRE(assetArchiveValidateBytes(missingGltfBytes.data(),
		static_cast<u32>(missingGltfBytes.size()),
		missingGltfUri.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("texture.png") != std::string::npos);

	auto objMtlChain = writeTypedArchiveEntries("policy-obj-mtl-chain", ".pdmesh", {
		{ "mesh.ini",
		  "[mesh]\n"
		  "catalog_id = mod:mesh_policy_obj_mtl_chain\n"
		  "geometry_file = geometry/model.obj\n" },
		{ "geometry/model.obj",
		  "mtllib materials/model.mtl\n"
		  "v 0 0 0\n"
		  "v 1 0 0\n"
		  "v 0 1 0\n"
		  "usemtl mat\n"
		  "f 1 2 3\n" },
		{ "geometry/materials/model.mtl",
		  "newmtl mat\n"
		  "map_Kd textures/diffuse.png\n" },
		{ "geometry/materials/textures/diffuse.png", "png" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"mesh\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(objMtlChain.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	auto missingObjTexture = writeTypedArchiveEntries("policy-missing-obj-texture", ".pdmesh", {
		{ "mesh.ini",
		  "[mesh]\n"
		  "catalog_id = mod:mesh_policy_missing_obj_texture\n"
		  "geometry_file = geometry/model.obj\n" },
		{ "geometry/model.obj", "mtllib materials/model.mtl\n" },
		{ "geometry/materials/model.mtl",
		  "newmtl mat\n"
		  "map_Kd textures/diffuse.png\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"mesh\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(missingObjTexture.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("textures/diffuse.png") != std::string::npos);

	auto missingNestedArchive = writeTypedArchiveEntries("policy-missing-nested", ".pdcharacter", {
		{ "character.ini",
		  "[character]\n"
		  "catalog_id = mod:character_policy_missing_nested\n"
		  "body_archive = dependencies/assets/body/tri_body.pdbody\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"character\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(missingNestedArchive.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("tri_body.pdbody") != std::string::npos);

	const char manifestMissingDependency[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"dependencies/assets/font/base_font_body.pdfont\",\n"
		"      \"required\": true,\n"
		"      \"version\": \"\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": { \"id\": \"\", \"reason\": \"\" }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	auto missingManifestDependency = writeTypedArchiveEntries(
		"policy-missing-manifest-dependency", ".pdlang", {
		{ "lang.ini",
		  "[lang]\n"
		  "catalog_id = mod:lang_policy_missing_manifest_dependency\n"
		  "manifest = _meta/manifest.json\n"
		  "strings_file = strings.tsv\n" },
		{ "strings.tsv", "0\tMissing dependency\n" },
		{ "_meta/manifest.json", manifestMissingDependency },
	});
	REQUIRE(assetArchiveValidateFile(missingManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("no explicit fallback") != std::string::npos);
	const std::string missingManifestBytes =
		readFile(missingManifestDependency.path.string().c_str());
	REQUIRE(assetArchiveValidateBytes(missingManifestBytes.data(),
		static_cast<u32>(missingManifestBytes.size()),
		missingManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("no explicit fallback") != std::string::npos);

	const char manifestBaseFallback[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"dependencies/assets/font/base_font_body.pdfont\",\n"
		"      \"required\": false,\n"
		"      \"version\": \"\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": { \"id\": \"base:font_body\", \"reason\": \"base asset supplied by runtime\" }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	auto fallbackManifestDependency = writeTypedArchiveEntries(
		"policy-fallback-manifest-dependency", ".pdlang", {
		{ "lang.ini",
		  "[lang]\n"
		  "catalog_id = mod:lang_policy_fallback_manifest_dependency\n"
		  "manifest = _meta/manifest.json\n"
		  "strings_file = strings.tsv\n" },
		{ "strings.tsv", "0\tFallback dependency\n" },
		{ "_meta/manifest.json", manifestBaseFallback },
	});
	REQUIRE(assetArchiveValidateFile(fallbackManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	const char manifestUnsafeDependency[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"../base_font_body.pdfont\",\n"
		"      \"required\": false,\n"
		"      \"version\": \"\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": { \"id\": \"base:font_body\", \"reason\": \"base asset supplied by runtime\" }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	auto unsafeManifestDependency = writeTypedArchiveEntries(
		"policy-unsafe-manifest-dependency", ".pdlang", {
		{ "lang.ini",
		  "[lang]\n"
		  "catalog_id = mod:lang_policy_unsafe_manifest_dependency\n"
		  "manifest = _meta/manifest.json\n"
		  "strings_file = strings.tsv\n" },
		{ "strings.tsv", "0\tUnsafe dependency\n" },
		{ "_meta/manifest.json", manifestUnsafeDependency },
	});
	REQUIRE(assetArchiveValidateFile(unsafeManifestDependency.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("unsafe dependency archive") !=
		std::string::npos);

	auto badNestedBody = writeTypedArchiveEntries("policy-bad-nested-body", ".pdbody", {
		{ "body.ini",
		  "[body]\n"
		  "catalog_id = mod:body_policy_bad_nested\n"
		  "model_file = model.gltf\n" },
		{ "model.gltf",
		  "{ \"asset\": { \"version\": \"2.0\" }, "
		  "\"images\": [ { \"uri\": \"missing_nested_texture.png\" } ] }\n" },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"body\" }\n" },
	});
	const std::string badNestedBytes =
		readFile(badNestedBody.path.string().c_str());
	auto recursiveNested = writeTypedArchiveEntries("policy-recursive-nested", ".pdcharacter", {
		{ "character.ini",
		  "[character]\n"
		  "catalog_id = mod:character_policy_recursive_nested\n"
		  "body_archive = dependencies/assets/body/bad_body.pdbody\n" },
		{ "dependencies/assets/body/bad_body.pdbody", badNestedBytes },
		{ "_meta/manifest.json", "{ \"pd_kind\": \"character\" }\n" },
	});
	REQUIRE(assetArchiveValidateFile(recursiveNested.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("missing_nested_texture.png") != std::string::npos);

	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive plain{
		std::filesystem::temp_directory_path()
			/ ("pd2-policy-plain-" + std::to_string(stamp) + ".pdhead")
	};
	{
		std::ofstream out(plain.path, std::ios::binary);
		out << "[head]\nmodel_file = model.gltf\n";
	}
	REQUIRE(assetArchiveValidateFile(plain.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);
	REQUIRE(std::string(err).find("zip-openable") != std::string::npos);
	REQUIRE(assetArchivePathIsDeprecated("weapons/old.pdwpn") == 1);
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

TEST_CASE("bundled pdmod manifests declare explicit stable ids",
          "[modding][pdmod][static][c3844]") {
	const std::string baseUi =
		readArchiveEntryText("mods/base-ui.pdmod", "mod.json");
	const std::string modernUi =
		readArchiveEntryText("mods/pd-modern-ui.pdmod", "mod.json");
	const std::string modernSource =
		readFile("mods/pd-modern-ui.legacy_backup/mod.json");

	REQUIRE(baseUi.find("\"id\": \"base-ui\"") != std::string::npos);
	REQUIRE(modernUi.find("\"id\": \"pd-modern-ui\"") != std::string::npos);
	REQUIRE(modernSource.find("\"id\": \"pd-modern-ui\"") !=
	        std::string::npos);
}

TEST_CASE("shared typed archive writer emits c3838 metadata contract",
          "[modding][pdxxx][writer][c3838]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-writer-contract-" + std::to_string(stamp) + ".pdlang")
	};

	mod_archive_writer_t *raw = modArchiveBegin(out.path.string().c_str());
	REQUIRE(raw != nullptr);

	asset_archive_writer_t writer;
	REQUIRE(assetArchiveWriterInit(&writer, raw, "lang",
		"base:lang_writer_contract_en") == MODARCHIVE_OK);
	assetArchiveWriterSetProvenance(&writer, "pd-tests",
		"data/test/lang.raw", 7, "FILE_LTESTE");

	const char descriptor[] =
		"[lang]\n"
		"catalog_id = base:lang_writer_contract_en\n"
		"manifest = _meta/manifest.json\n"
		"strings_file = strings.tsv\n";
	const char manifest[] =
		"{\n"
		"  \"pd_kind\": \"lang\",\n"
		"  \"pd_schema_version\": 1,\n"
		"  \"id\": \"base:lang_writer_contract_en\",\n"
		"  \"data\": \"strings.tsv\",\n"
		"  \"dependencies\": [\n"
		"    {\n"
		"      \"role\": \"font\",\n"
		"      \"type\": \"font\",\n"
		"      \"id\": \"base:font_body\",\n"
		"      \"archive\": \"dependencies/assets/font/base_font_body.pdfont\",\n"
		"      \"required\": false,\n"
		"      \"version\": \"1\",\n"
		"      \"sha256\": \"\",\n"
		"      \"fallback\": {\n"
		"        \"id\": \"base:font_body\",\n"
		"        \"reason\": \"base asset supplied by runtime\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"dependency_schema\": {\n"
		"    \"root\": \"dependencies/assets\",\n"
		"    \"required_fields\": [\"role\", \"type\", \"id\", \"archive\", \"required\", \"version\", \"sha256\", \"fallback.id\", \"fallback.reason\"]\n"
		"  }\n"
		"}\n";
	const char strings[] = "0\tHello\n1\tArchive\n";

	REQUIRE(assetArchiveWriterAddDescriptor(&writer, "lang.ini",
		descriptor, (u32)strlen(descriptor)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddManifestJson(&writer, manifest,
		(u32)strlen(manifest)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "strings.tsv",
		strings, (u32)strlen(strings), "strings") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterFinishMetadata(&writer) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(raw) == MODARCHIVE_OK);

	char err[256];
	REQUIRE(assetArchiveValidateFile(out.path.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	OpenArchive opened;
	opened.archive = modArchiveOpen(out.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	REQUIRE(modArchiveFindEntry(opened.archive, "lang.ini") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "strings.tsv") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/manifest.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/inventory.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/hashes.tsv") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/provenance.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/validation.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive,
		"_meta/source-handles.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive, "_meta/lang.ini.sha256") >= 0);
	REQUIRE(modArchiveFindEntry(opened.archive,
		"_meta/strings.tsv.sha256") >= 0);

	const std::string inventory =
		readArchiveEntryText(opened.archive, "_meta/inventory.json");
	REQUIRE(inventory.find("\"path\": \"lang.ini\"") != std::string::npos);
	REQUIRE(inventory.find("\"role\": \"descriptor\"") != std::string::npos);
	REQUIRE(inventory.find("\"path\": \"strings.tsv\"") != std::string::npos);
	REQUIRE(inventory.find("\"role\": \"strings\"") != std::string::npos);
	REQUIRE(inventory.find("\"dependency_archive_root\": \"dependencies/assets\"") !=
		std::string::npos);

	const std::string hashes =
		readArchiveEntryText(opened.archive, "_meta/hashes.tsv");
	REQUIRE(hashes.find("path\tsha256\tsize\trole") != std::string::npos);
	REQUIRE(hashes.find("lang.ini\t") != std::string::npos);
	REQUIRE(hashes.find("strings.tsv\t") != std::string::npos);

	const std::string validation =
		readArchiveEntryText(opened.archive, "_meta/validation.json");
	REQUIRE(validation.find("\"status\": \"writer_checked\"") !=
		std::string::npos);
	REQUIRE(validation.find("\"fallback.id\"") != std::string::npos);
	REQUIRE(validation.find("\"fallback.reason\"") != std::string::npos);

	const std::string sourceHandles =
		readArchiveEntryText(opened.archive, "_meta/source-handles.json");
	REQUIRE(sourceHandles.find("\"path\": \"data/test/lang.raw\"") !=
		std::string::npos);
	REQUIRE(sourceHandles.find("\"symbol\": \"FILE_LTESTE\"") !=
		std::string::npos);
}

TEST_CASE("shared typed archive writer infers c3838 dependency records",
          "[modding][pdxxx][writer][c3838]") {
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	TempArchive out{
		std::filesystem::temp_directory_path()
			/ ("pd2-writer-deps-" + std::to_string(stamp) + ".pdlang")
	};

	mod_archive_writer_t *raw = modArchiveBegin(out.path.string().c_str());
	REQUIRE(raw != nullptr);

	asset_archive_writer_t writer;
	REQUIRE(assetArchiveWriterInit(&writer, raw, "lang",
		"base:lang_writer_dependency_en") == MODARCHIVE_OK);

	const char descriptor[] =
		"[lang]\n"
		"catalog_id = base:lang_writer_dependency_en\n"
		"manifest = _meta/manifest.json\n"
		"strings_file = strings.tsv\n"
		"font_archive = dependencies/assets/font/base_font_body.pdfont\n";
	const char strings[] = "0\tDependency\n";
	const char nested[] = "placeholder nested archive bytes";

	REQUIRE(assetArchiveWriterAddDescriptor(&writer, "lang.ini",
		descriptor, (u32)strlen(descriptor)) == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer, "strings.tsv",
		strings, (u32)strlen(strings), "strings") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterAddPublicMem(&writer,
		"dependencies/assets/font/base_font_body.pdfont",
		nested, (u32)strlen(nested), "font") == MODARCHIVE_OK);
	REQUIRE(assetArchiveWriterFinishMetadata(&writer) == MODARCHIVE_OK);
	REQUIRE(modArchiveFinish(raw) == MODARCHIVE_OK);

	OpenArchive opened;
	opened.archive = modArchiveOpen(out.path.string().c_str());
	REQUIRE(opened.archive != nullptr);

	const std::string manifest =
		readArchiveEntryText(opened.archive, "_meta/manifest.json");
	REQUIRE(manifest.find("\"dependencies\": [") != std::string::npos);
	REQUIRE(manifest.find("\"role\": \"font\"") != std::string::npos);
	REQUIRE(manifest.find("\"type\": \"font\"") != std::string::npos);
	REQUIRE(manifest.find("\"id\": \"base:font_body\"") != std::string::npos);
	REQUIRE(manifest.find("\"archive\": \"dependencies/assets/font/base_font_body.pdfont\"") !=
		std::string::npos);
	REQUIRE(manifest.find("\"required\": true") != std::string::npos);
	REQUIRE(manifest.find("\"sha256\": \"") != std::string::npos);
	REQUIRE(manifest.find("\"fallback\": {") != std::string::npos);
	REQUIRE(manifest.find("\"id\": \"\",\n        \"reason\": \"\"") !=
		std::string::npos);

	const std::string validation =
		readArchiveEntryText(opened.archive, "_meta/validation.json");
	REQUIRE(validation.find("\"dependency_archive_count\": 1") !=
		std::string::npos);
	REQUIRE(validation.find("\"fallback.reason\"") != std::string::npos);
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

bool archiveHasRootOrRelativeEntry(mod_archive_t *archive,
                                   const std::string &baseEntry,
                                   const std::string &value) {
	return archiveHasEntry(archive, value) ||
		archiveHasEntry(archive, entryRelativePath(baseEntry, value));
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
				REQUIRE(archiveHasRootOrRelativeEntry(archive,
					descriptorEntry, value));
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
		REQUIRE(archiveHasRootOrRelativeEntry(archive, gltfEntry, uri));
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
		const std::string mtlEntry = archiveHasEntry(archive, mtl)
			? mtl
			: entryRelativePath(objEntry, mtl);
		REQUIRE(archiveHasEntry(archive, mtlEntry));

		const std::string mtlText = readArchiveEntryText(archive, mtlEntry.c_str());
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
			REQUIRE(archiveHasRootOrRelativeEntry(archive, mtlEntry, texture));
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
	REQUIRE(packer.find("assetArchivePathIsTyped(childRel)") != std::string::npos);
	REQUIRE(packer.find("assetArchiveValidateFile(descriptorAbs, ASSET_ARCHIVE_VALIDATE_RELEASE") != std::string::npos);
	REQUIRE(packer.find("MODPACK.PDMOD: validated %u typed .pd* content descriptor(s) before packing") != std::string::npos);
	REQUIRE(packer.find("processCanonicalFamily(srcFolder") != std::string::npos);
	REQUIRE(packer.find("writeTemplateIfMissing") != std::string::npos);
	REQUIRE(packer.find("modiniTemplateForKind(kind)") != std::string::npos);
	REQUIRE(packer.find("pads.ini - generated external map pad/spawn template") != std::string::npos);
	REQUIRE(packer.find("setup.ini - generated external map setup template") != std::string::npos);
	REQUIRE(packer.find("\"pads.tsv\"") != std::string::npos);
	REQUIRE(packer.find("\"spawns.tsv\"") != std::string::npos);
	REQUIRE(packer.find("\"volumes.tsv\"") != std::string::npos);
	REQUIRE(packer.find("\"objects.tsv\"") != std::string::npos);
	REQUIRE(packer.find("\"objectives.tsv\"") != std::string::npos);
	REQUIRE(packer.find("\"level.graph.json\"") != std::string::npos);
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
	REQUIRE(scanner.find("assetArchiveExtractDescriptorMemAlloc") != std::string::npos);
	REQUIRE(scanner.find("qualifyTypedArchiveSourcePaths(&ini, entry_name)") != std::string::npos);
	REQUIRE(scanner.find("\"theme_file\"") != std::string::npos);
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
	std::string policy = readFile("port/src/asset_archive_policy.c");
	REQUIRE(scanner.find("assetArchiveDescriptorForPath(path)") != std::string::npos);
	REQUIRE(scanner.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(policy.find("\"weapon.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"head.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"body.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"arena.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"scenario.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"sound.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"ui.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"font.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"lang.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"animation.ini\"") != std::string::npos);
	REQUIRE(policy.find("\"mesh.ini\"") != std::string::npos);
	REQUIRE(policy.find("\".pdhead\"") != std::string::npos);
	REQUIRE(policy.find("\".pdarena\"") != std::string::npos);
	REQUIRE(policy.find("\".pdanim\"") != std::string::npos);
	REQUIRE(policy.find("\".pdmesh\"") != std::string::npos);

	struct TypedFamily {
		const char *path;
		const char *descriptor;
		asset_type_e type;
	};
	const TypedFamily families[] = {
		{ "weapons/x.pdweapon", "weapon.ini", ASSET_WEAPON },
		{ "projectiles/x.pdprojectile", "projectile.ini", ASSET_PROJECTILE },
		{ "entities/x.pdentity", "entity.ini", ASSET_ENTITY },
		{ "materials/x.pdmaterial", "material.ini", ASSET_MATERIAL },
		{ "textures/x.pdtexture", "texture.ini", ASSET_TEXTURE },
		{ "characters/x.pdcharacter", "character.ini", ASSET_CHARACTER },
		{ "heads/x.pdhead", "head.ini", ASSET_HEAD },
		{ "bodies/x.pdbody", "body.ini", ASSET_BODY },
		{ "arenas/x.pdarena", "arena.ini", ASSET_ARENA },
		{ "scenarios/x.pdscenario", "scenario.ini", ASSET_SCENARIO },
		{ "meshes/x.pdmesh", "mesh.ini", ASSET_MODEL },
		{ "animations/x.pdanim", "animation.ini", ASSET_ANIMATION },
		{ "audio/x.pdsfx", "sound.ini", ASSET_AUDIO },
		{ "audio/x.pdvoice", "voice.ini", ASSET_AUDIO },
		{ "audio/x.pdsong", "music.ini", ASSET_AUDIO },
		{ "ui/x.pdui", "ui.ini", ASSET_UI },
		{ "fonts/x.pdfont", "font.ini", ASSET_FONT },
		{ "lang/x.pdlang", "lang.ini", ASSET_LANG },
		{ "skins/x.pdskin", "skin.ini", ASSET_SKIN },
		{ "effects/x.pdeffect", "effect.ini", ASSET_EFFECT },
		{ "props/x.pdprop", "prop.ini", ASSET_PROP },
		{ "vehicles/x.pdvehicle", "vehicle.ini", ASSET_VEHICLE },
		{ "missions/x.pdmission", "mission.ini", ASSET_MISSION },
		{ "gamemodes/x.pdgamemode", "gamemode.ini", ASSET_GAMEMODE },
		{ "botprofiles/x.pdbotprofile", "botprofile.ini", ASSET_BOT_PROFILE },
		{ "hud/x.pdhud", "hud.ini", ASSET_HUD },
		{ "themes/x.pdtheme", "theme.ini", ASSET_THEME },
	};
	for (const TypedFamily &family : families) {
		INFO(family.path);
		REQUIRE(assetArchivePathIsTyped(family.path) == 1);
		REQUIRE(std::string(assetArchiveDescriptorForPath(family.path)) == family.descriptor);
		REQUIRE(assetArchiveTypeForPath(family.path) == family.type);
	}
	REQUIRE(assetArchivePathIsTyped("tools/x.pdtool") == 0);
}

TEST_CASE("legacy pd asset walkers remain registered during external pdmod migration",
          "[modding][pdmod][static][c3809]") {
	std::string common_h = readFile("port/include/loader_walker_common.h");
	std::string common = readFile("port/src/loader_walker_common.c");
	std::string walker = readFile("port/src/loader_walker.c");

	REQUIRE(common_h.find("auto-detecting plain JSON vs ZIP compound") != std::string::npos);
	REQUIRE(common.find("if (strcmp(name + nlen - ext_len, desc->extension) != 0) continue;") != std::string::npos);
	REQUIRE(common.find("bytes[0] == 'P' && bytes[1] == 'K'") != std::string::npos);
	REQUIRE(common.find("assetArchiveFindMetadataEntry") != std::string::npos);

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
		"loaderWalkerScanMetadataFamilies(data_root, &mr)",
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
		{ "port/src/loader_walker_sfx.c",      ".kind_str = \"sfx\"" },
		{ "port/src/loader_walker_voice.c",    ".kind_str = \"voice\"" },
		{ "port/src/loader_walker_song.c",     ".kind_str = \"song\"" },
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

	const std::string metaWalker = readFile("port/src/loader_walker_meta.c");
	REQUIRE(metaWalker.find(".pdcharacter") != std::string::npos);
	REQUIRE(metaWalker.find(".pdskin") != std::string::npos);
	REQUIRE(metaWalker.find(".pdprop") != std::string::npos);
	REQUIRE(metaWalker.find(".pdvehicle") != std::string::npos);
	REQUIRE(metaWalker.find(".pdmission") != std::string::npos);
	REQUIRE(metaWalker.find(".pdgamemode") != std::string::npos);
	REQUIRE(metaWalker.find(".pdbotprofile") != std::string::npos);
	REQUIRE(metaWalker.find(".pdhud") != std::string::npos);
	REQUIRE(metaWalker.find(".pdeffect") != std::string::npos);
	REQUIRE(metaWalker.find(".pdmaterial") != std::string::npos);
	REQUIRE(metaWalker.find(".pdtheme") != std::string::npos);
	REQUIRE(metaWalker.find("desc.always_invoke = 1") != std::string::npos);
	REQUIRE(metaWalker.find("assetCatalogGetMutable(id)") != std::string::npos);
	REQUIRE(metaWalker.find("entry->type != type") != std::string::npos);
	REQUIRE(metaWalker.find("loaderWalkerArchiveMemberPath") != std::string::npos);
	REQUIRE(metaWalker.find("catalogSetPrimaryFile") != std::string::npos);
	REQUIRE(metaWalker.find("loaderWalkerMarkBaseArchiveEntry") != std::string::npos);
}

TEST_CASE("weapon content pipeline accepts pdweapon only",
          "[modding][pdxxx][weapon][static][c3814]") {
	const std::string removedWeaponExt = std::string(".pd") + "wpn";
	const char *liveFiles[] = {
		"port/src/loader_walker_weapon.c",
		"port/src/assetcatalog_scanner.c",
		"port/src/modmgr.c",
		"port/src/romextract_pdweapon.c",
		"port/include/romextract_pd.h",
	};

	for (const char *path : liveFiles) {
		INFO(path);
		std::string src = readFile(path);
		if (std::string(path) == "port/src/assetcatalog_scanner.c") {
			REQUIRE(src.find("assetArchiveTypeForPath(path)") != std::string::npos);
			REQUIRE(src.find("ASSET_WEAPON") != std::string::npos);
		} else {
			REQUIRE(src.find(".pdweapon") != std::string::npos);
		}
		REQUIRE(src.find(removedWeaponExt) == std::string::npos);
	}

	const std::string policy = readFile("port/src/asset_archive_policy.c");
	const std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(policy.find(removedWeaponExt) != std::string::npos);
	REQUIRE(packer.find("Deprecated .pdwpn files are not allowed") != std::string::npos);

	const std::string extractor = readFile("port/src/romextract_pdweapon.c");
	REQUIRE(extractor.find("nested_payloads = \" PDWEAPON_NESTED_PAYLOADS_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_NESTED_PAYLOADS_ENTRY \"_meta/nested-payloads.json\"") != std::string::npos);
	REQUIRE(extractor.find("runtime.graph.json") == std::string::npos);
	REQUIRE(extractor.find("behavior_graph = \" PDWEAPON_RUNTIME_GRAPH_ENTRY") == std::string::npos);
	REQUIRE(extractor.find("primary_graph = \" PDWEAPON_PRIMARY_GRAPH_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("secondary_graph = \" PDWEAPON_SECONDARY_GRAPH_ENTRY") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_PRIMARY_GRAPH_ENTRY \"behavior/primary.graph.json\"") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_SECONDARY_GRAPH_ENTRY \"behavior/secondary.graph.json\"") != std::string::npos);
	REQUIRE(extractor.find("\"shared_context\"") != std::string::npos);
	REQUIRE(extractor.find("\"subgraph\"") != std::string::npos);
	REQUIRE(extractor.find("primary_trigger") != std::string::npos);
	REQUIRE(extractor.find("secondary_trigger") != std::string::npos);
	REQUIRE(extractor.find("event.trigger_pressed") != std::string::npos);
	REQUIRE(extractor.find("event.trigger_held") != std::string::npos);
	REQUIRE(extractor.find("event.trigger_released") != std::string::npos);
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
	REQUIRE(extractor.find("bindings.json") != std::string::npos);
	REQUIRE(extractor.find("model_catalog_id = %s") != std::string::npos);
	REQUIRE(extractor.find("model_ref = %s") == std::string::npos);
	REQUIRE(extractor.find("projectile_model_catalog_id") != std::string::npos);
	REQUIRE(extractor.find("projectile_model_ref") == std::string::npos);
	REQUIRE(extractor.find("shoot_sound_catalog_id") != std::string::npos);
	REQUIRE(extractor.find("special_sound_catalog_id") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEPENDENCY_CLOSURE_MARKER \"embedded.v13\"") != std::string::npos);
	REQUIRE(extractor.find("dependency_closure = \" PDWEAPON_DEPENDENCY_CLOSURE_MARKER") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_FAST_CACHE_KIND \"pdweapon_embedded_v13_clean_public\"") != std::string::npos);
	REQUIRE(extractor.find("PDWEAPON_DEP_MODELS \"dependencies/assets/models\"") != std::string::npos);
	REQUIRE(extractor.find("/held_hi.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("/held_lo.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("bindings/animations.tsv") != std::string::npos);
	REQUIRE(extractor.find("bindings/audio.tsv") != std::string::npos);
	REQUIRE(extractor.find("model_archive = \" PDWEAPON_DEP_MODELS \"/visual.pdmesh") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponMeshDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponAnimationDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addWeaponAudioDependency") != std::string::npos);
	REQUIRE(extractor.find("s_addProjectileModelDependency") != std::string::npos);
	REQUIRE(extractor.find("s_emitWeaponManifestDependencies") != std::string::npos);
	REQUIRE(extractor.find("s_emitWeaponManifestDependencySchema") != std::string::npos);
	REQUIRE(extractor.find("\"dependency_schema\"") != std::string::npos);
	REQUIRE(extractor.find("required_fields") != std::string::npos);
	REQUIRE(extractor.find("\"fallback.reason\"") != std::string::npos);
	REQUIRE(extractor.find("s_existingArchiveEntryContains") != std::string::npos);
	REQUIRE(extractor.find("weaponGraphArchiveCanonicalSha256File") != std::string::npos);
	REQUIRE(extractor.find("assetArchiveWriterAddPublicDisk(&asset_writer") != std::string::npos);
	REQUIRE(extractor.find("modArchiveAddFileDisk(aw, p->archive_entry") == std::string::npos);

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
	REQUIRE(scanner.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(scanner.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(scanner.find("ASSET_ENTITY") != std::string::npos);
	REQUIRE(scanner.find("\"projectile.ini\"") != std::string::npos);
	REQUIRE(scanner.find("\"entity.ini\"") != std::string::npos);
	REQUIRE(scanner.find("projectile.ini - physical projectile behavior metadata") != std::string::npos);
	REQUIRE(scanner.find("entity.ini - deployed/stuck behavior archetype metadata") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(e->id, e->ext.projectile.entity_ref") != std::string::npos);

	std::string packer = readFile("port/src/modpack_pdmod.c");
	REQUIRE(packer.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(packer.find("ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(packer.find("ASSET_ENTITY") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"projectiles\", \"projectile.ini\", \"projectile\"") != std::string::npos);
	REQUIRE(packer.find("RUN_FAMILY(\"entities\", \"entity.ini\", \"entity\"") != std::string::npos);

	std::string manifest_h = readFile("port/include/net/netmanifest.h");
	REQUIRE(manifest_h.find("MANIFEST_TYPE_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_h.find("MANIFEST_TYPE_ENTITY") != std::string::npos);
	REQUIRE(manifest_h.find("MANIFEST_TYPE_ASSET") != std::string::npos);
	REQUIRE(manifest_h.find("slot_index stores asset_type_e") != std::string::npos);

	std::string manifest_c = readFile("port/src/net/netmanifest.c");
	REQUIRE(manifest_c.find("case ASSET_PROJECTILE: return MANIFEST_TYPE_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_c.find("case ASSET_ENTITY:") != std::string::npos);
	REQUIRE(manifest_c.find("case MANIFEST_TYPE_PROJECTILE: return ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(manifest_c.find("case MANIFEST_TYPE_ENTITY:") != std::string::npos);
	REQUIRE(manifest_c.find("default:              return MANIFEST_TYPE_ASSET") != std::string::npos);
	REQUIRE(manifest_c.find("s_manifestEntryCatalogAssetType") != std::string::npos);
	REQUIRE(manifest_c.find("mtype == MANIFEST_TYPE_ASSET") != std::string::npos);
	REQUIRE(manifest_c.find("entry->type == MANIFEST_TYPE_ASSET") != std::string::npos);

	std::string netmsg = readFile("port/src/net/netmsg.c");
	REQUIRE(netmsg.find("ASSET_WEAPON, ASSET_PROJECTILE, ASSET_ENTITY") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("projectile.ini\") == 0) return ASSET_PROJECTILE") != std::string::npos);
	REQUIRE(distrib.find("entity.ini\") == 0)    return ASSET_ENTITY") != std::string::npos);

	std::string modmgr = readFile("port/src/modmgr.c");
	REQUIRE(modmgr.find(".pdprojectile") != std::string::npos);
	REQUIRE(modmgr.find(".pdentity") != std::string::npos);
}

TEST_CASE("base ROM extraction skips empty file slots before size lookup",
          "[modding][pdxxx][c3844][static]") {
	const std::string extractor = readFile("port/src/romextract.c");
	const std::string firstLaunchNeedle =
		"u8 *data = romdataFileGetData(fileNum);\n"
		"        if (data == NULL) {\n"
		"            skippedEmpty++;\n"
		"            continue;\n"
		"        }\n"
		"\n"
		"        s32 size = romdataFileGetSize(fileNum);";
	const std::string verifyNeedle =
		"u8 *romData = romdataFileGetData(fileNum);\n"
		"    if (romData == NULL) {\n"
		"        th->skippedEmpty++;\n"
		"        return;\n"
		"    }\n"
		"\n"
		"    s32 romSize = romdataFileGetSize(fileNum);";
	REQUIRE(extractor.find(firstLaunchNeedle) != std::string::npos);
	REQUIRE(extractor.find(verifyNeedle) != std::string::npos);
}

TEST_CASE("scenario path extraction uses runtime setup pointer resolution",
          "[modding][pdxxx][c3844][static]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");

	REQUIRE(arena.find("static s32 s_resolveSetupPointer") !=
	        std::string::npos);
	REQUIRE(arena.find("raw >= base && raw < end") != std::string::npos);
	REQUIRE(arena.find("raw < (uintptr_t)size") != std::string::npos);
	REQUIRE(arena.find("s_resolveSetupPointer(data, size, setup->paths") !=
	        std::string::npos);
	REQUIRE(arena.find("setup->paths, sizeof(void *)") !=
	        std::string::npos);
	REQUIRE(arena.find("sizeof(paths[i].pads)") != std::string::npos);
	REQUIRE(arena.find("s_resolveSetupPointer(data, size, paths[i].pads") !=
	        std::string::npos);
	REQUIRE(arena.find("if (!paths_ofs) {\n\t\treturn 0;\n\t}") !=
	        std::string::npos);
}

TEST_CASE("random meta arenas do not resolve through the stage table",
          "[modding][pdxxx][c3844][static]") {
	const std::string arena = readFile("port/src/romextract_pdarena.c");

	REQUIRE(arena.find("static s32 s_arenaStageIndexForScenario") !=
	        std::string::npos);
	REQUIRE(arena.find("stagenum == STAGE_MP_RANDOM_MULTI") !=
	        std::string::npos);
	REQUIRE(arena.find("stagenum == STAGE_MP_RANDOM_SOLO") !=
	        std::string::npos);
	REQUIRE(arena.find("return stageGetIndex(stagenum);") !=
	        std::string::npos);
	REQUIRE(arena.find("stageGetIndex(a->stagenum)") == std::string::npos);
	REQUIRE(arena.find("s_arenaStageIndexForScenario(a->stagenum) >= 0") !=
	        std::string::npos);
	REQUIRE(arena.find("s32 stage_idx = s_arenaStageIndexForScenario(a->stagenum);") !=
	        std::string::npos);
}

TEST_CASE("base UI chrome is emitted and loaded as pdui source",
          "[modding][pdxxx][c3844][static]") {
	const std::string theme = readFile("port/fast3d/pdgui_theme.cpp");

	REQUIRE(theme.find("\"base:ui_chrome_frame\"") != std::string::npos);
	REQUIRE(theme.find("\"ui_chrome_frame\"") != std::string::npos);
	REQUIRE(theme.find("s_generateChromeFrameBgra(bgra)") !=
	        std::string::npos);
	REQUIRE(theme.find("assetArchiveWriterAddPublicMem(&asset_writer, \"texture.tga\"") !=
	        std::string::npos);
	REQUIRE(theme.find("s_loadPduiTexture(chrome, &w, &h)") !=
	        std::string::npos);
	REQUIRE(theme.find("UI.CHROME: loaded base chrome from ui_chrome_frame.pdui") !=
	        std::string::npos);
	REQUIRE(theme.find("mods/base-game/ui-chrome") == std::string::npos);
	REQUIRE(theme.find("base-game chrome missing") == std::string::npos);
	REQUIRE(theme.find("ui_chrome_frame.tga") == std::string::npos);
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
		"manifest = _meta/manifest.json\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n";
	TempArchive weapon = writeArchiveEntries("weapon-graph-root", {
		{ "weapon.ini", weaponIni },
		{ "_meta/manifest.json", "{}\n" },
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

	const std::string cleanWeaponIni =
		"[weapon]\n"
		"catalog_id = base:test_weapon\n"
		"primary_graph = behavior/primary.graph.json\n"
		"secondary_graph = behavior/secondary.graph.json\n"
		"shared_context = behavior/shared-context.json\n";
	const std::string primaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"primary\",\n"
		"  \"nodes\": [ { \"id\": \"primary_action\", \"kind\": \"fire.hitscan\","
		" \"params\": { \"mode\": \"primary\", \"damage\": 1.25 } } ],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"primary_action\" } ]\n"
		"}\n";
	const std::string secondaryGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_weapon\",\n"
		"  \"graph_id\": \"secondary\",\n"
		"  \"nodes\": [ { \"id\": \"secondary_action\", \"kind\": \"spawn.fired_projectile\","
		" \"params\": { \"mode\": \"secondary\", \"projectile_ref\": \"base:test_weapon__projectile_rocket\" } } ],\n"
		"  \"exports\": [ { \"name\": \"secondary\", \"node\": \"secondary_action\" } ]\n"
		"}\n";
	const std::string sharedContext =
		"{ \"schema\": \"pd.weapon_shared_context.v1\", \"asset_id\": \"base:test_weapon\","
		" \"contexts\": [ { \"name\": \"owner_player\", \"scope\": \"player\","
		" \"source\": \"equipped_player\", \"type\": \"player_ref\" } ] }\n";
	TempArchive cleanWeapon = writeArchiveEntries("weapon-clean-graph-compiler", {
		{ "weapon.ini", cleanWeaponIni },
		{ "behavior/primary.graph.json", primaryGraph },
		{ "behavior/secondary.graph.json", secondaryGraph },
		{ "behavior/shared-context.json", sharedContext },
	});
	weapon_graph_ir_t fromCleanArchive;
	REQUIRE(weaponGraphCompileArchiveFile(cleanWeapon.path.string().c_str(),
		ASSET_WEAPON, &fromCleanArchive, err, sizeof(err)) == 0);
	REQUIRE(fromCleanArchive.context_count == 1);
	REQUIRE(fromCleanArchive.node_count == 2);
	REQUIRE(fromCleanArchive.export_count == 2);
	REQUIRE(fromCleanArchive.nodes[0].opcode == WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(fromCleanArchive.nodes[1].opcode == WEAPON_GRAPH_OP_SPAWN_FIRED_PROJECTILE);
}

TEST_CASE("weapon mod save contract supports shotgun template dual wield save",
          "[modding][pdxxx][weapon_graph][compiler][ui][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"mod:weapon_needler\",\n"
		"  \"graph_id\": \"shotgun_template_save\",\n"
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
		"      \"kind\": \"fire.hitscan\",\n"
		"      \"params\": {\n"
		"        \"recoverytime_ticks60\": 18,\n"
		"        \"projectile_ref\": null,\n"
		"        \"damage\": 1.75,\n"
		"        \"ammo_slot\": 0,\n"
		"        \"mode\": \"secondary\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_action\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary_action\" }\n"
		"  ]\n"
		"}\n";

	char err[256] = {};
	weapon_graph_ir_t ir;
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);
	REQUIRE(ir.export_count == 2);

	const std::string weaponIni =
		"[weapon]\n"
		"schema = pd.weapon.v1\n"
		"dependency_closure = embedded.v2\n"
		"catalog_id = mod:weapon_needler\n"
		"name = Needler\n"
		"manifest = _meta/manifest.json\n"
		"behavior_graph = behavior.graph.json\n"
		"nested_payloads = nested_payloads.json\n"
		"model_file = models/held_hi.pdmesh\n"
		"dual_wieldable = 1\n"
		"\n"
		"[references]\n"
		"template = base:shotgun\n"
		"model_ref = base:model_shotgun_hi\n"
		"animation_ref = base:invanim_shotgun_singleshot\n"
		"animation_archive = animations/invanim_shotgun_singleshot.pdanim\n";
	const std::string manifest =
		"{\n"
		"  \"schema\": \"pd.weapon.manifest.v1\",\n"
		"  \"catalog_id\": \"mod:weapon_needler\",\n"
		"  \"name\": \"Needler\",\n"
		"  \"creator\": \"Agent\",\n"
		"  \"template\": \"base:shotgun\",\n"
		"  \"dependency_closure\": \"embedded.v2\"\n"
		"}\n";
	const std::string nested =
		"{\"schema\":\"pd.weapon_nested_payloads.v1\",\"asset_id\":\"mod:weapon_needler\",\"payloads\":[]}\n";
	TempArchive saved = writeArchiveEntries("shotgun-template-save", {
		{ "weapon.ini", weaponIni },
		{ "_meta/manifest.json", manifest },
		{ "behavior.graph.json", graph },
		{ "nested_payloads.json", nested },
		{ "animations/invanim_shotgun_singleshot.pdanim", "pdanim" },
	});

	REQUIRE(weaponGraphArchiveValidateRootFile(saved.path.string().c_str(),
		ASSET_WEAPON, err, sizeof(err)) == 0);
	weapon_graph_archive_descriptor_t desc;
	REQUIRE(weaponGraphArchiveReadDescriptorFile(saved.path.string().c_str(),
		ASSET_WEAPON, &desc, err, sizeof(err)) == 0);
	REQUIRE(std::string(desc.catalog_id) == "mod:weapon_needler");
	REQUIRE(weaponGraphCompileArchiveFile(saved.path.string().c_str(),
		ASSET_WEAPON, &ir, err, sizeof(err)) == 0);

	const std::string savedIni =
		readArchiveEntryText(saved.path.string().c_str(), "weapon.ini");
	REQUIRE(savedIni.find("catalog_id = mod:weapon_needler") != std::string::npos);
	REQUIRE(savedIni.find("name = Needler") != std::string::npos);
	REQUIRE(savedIni.find("model_file = models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(savedIni.find("template = base:shotgun") != std::string::npos);
	REQUIRE(savedIni.find("animation_archive = animations/invanim_shotgun_singleshot.pdanim") !=
	        std::string::npos);
	REQUIRE(savedIni.find("dual_wieldable = 1") != std::string::npos);
	REQUIRE(savedIni.find("weapon_id") == std::string::npos);
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
		"        \"projectile_model_catalog_id\": \"base:model_chrdyrocketmis\",\n"
		"        \"scale\": 1.5,\n"
		"        \"speed\": 300,\n"
		"        \"travel_distance\": 900,\n"
		"        \"timer_ticks60\": 120,\n"
		"        \"reflect_angle\": 0.75,\n"
		"        \"projectile_sound_catalog_id\": \"base:sfx_launch_rocket\"\n"
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
		"        \"projectile_model_catalog_id\": \"base:model_chrautogun\",\n"
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

TEST_CASE("weapon graph runtime registers projectile and entity behavior assets",
          "[modding][pdxxx][weapon_graph][runtime][projectile][entity][c3814][c3838]") {
	const std::string projectileGraph =
		"{\n"
		"  \"schema\": \"pd.projectile_graph.v1\",\n"
		"  \"asset_id\": \"base:test_projectile_payload\",\n"
		"  \"graph_id\": \"projectile_asset_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"spawn\", \"kind\": \"projectile.spawn_state\", \"params\": {\n"
		"      \"model_catalog_id\": \"base:model_chrskrocketmis\",\n"
		"      \"model_archive\": \"dependencies/assets/model/visual.pdmesh\",\n"
		"      \"source_mode\": \"secondary\",\n"
		"      \"source_function_type\": \"shoot_projectile\",\n"
		"      \"scale\": 2.1,\n"
		"      \"damage\": 7,\n"
		"      \"flags\": 72\n"
		"    } },\n"
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\", \"params\": {\n"
		"      \"motion_kind\": \"powered\",\n"
		"      \"speed\": 60,\n"
		"      \"travel_distance\": 5,\n"
		"      \"timer60\": -1,\n"
		"      \"activation_time60\": 0,\n"
		"      \"recovery_time60\": 0,\n"
		"      \"reflect_angle\": 0.05,\n"
		"      \"powered\": true,\n"
		"      \"calculate_trajectory\": false\n"
		"    } },\n"
		"    { \"id\": \"homing\", \"kind\": \"projectile.homing\", \"params\": {\n"
		"      \"target_source\": \"current_lock\",\n"
		"      \"runtime_constants\": \"extract_in_runtime_adapter\"\n"
		"    } },\n"
		"    { \"id\": \"fly\", \"kind\": \"projectile.fly_by_wire\", \"params\": {\n"
		"      \"control_source\": \"owner\",\n"
		"      \"bot_route_policy\": \"runtime_existing\",\n"
		"      \"turn_rate\": 1.5\n"
		"    } },\n"
		"    { \"id\": \"wall\", \"kind\": \"projectile.wall_hugger\", \"params\": {\n"
		"      \"stick_surface_filter\": \"background\",\n"
		"      \"post_fall_timer60\": 360\n"
		"    } },\n"
		"    { \"id\": \"impact\", \"kind\": \"projectile.impact\", \"params\": {\n"
		"      \"impact_filter\": \"solid_or_chr\",\n"
		"      \"hit_sound\": \"base:sfx_launch_rocket\",\n"
		"      \"consume_on_hit\": true,\n"
		"      \"stick_on_hit\": false\n"
		"    } },\n"
		"    { \"id\": \"transition\", \"kind\": \"projectile.transition_to_entity\", \"params\": {\n"
		"      \"entity_ref\": \"base:test_entity_payload\",\n"
		"      \"when\": \"at_rest\",\n"
		"      \"transfer_owner\": true,\n"
		"      \"transfer_ammo\": true,\n"
		"      \"transfer_position\": true,\n"
		"      \"delete_carrier\": true\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"transition\" } ]\n"
		"}\n";
	const std::string entityGraph =
		"{\n"
		"  \"schema\": \"pd.entity_graph.v1\",\n"
		"  \"asset_id\": \"base:test_entity_payload\",\n"
		"  \"graph_id\": \"entity_asset_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"armed\", \"kind\": \"entity.armed_explosive\", \"params\": {\n"
		"      \"archetype\": \"armed_proxy_explosive\",\n"
		"      \"model_catalog_id\": \"base:model_chrproximitymine\",\n"
		"      \"model_archive\": \"dependencies/assets/model/visual.pdmesh\",\n"
		"      \"source_mode\": \"secondary\",\n"
		"      \"activation_time60\": 240,\n"
		"      \"recovery_time60\": 60,\n"
		"      \"flags\": 262144,\n"
		"      \"arm_delay_ticks60\": 240,\n"
		"      \"detonation_policy\": \"explode_once\",\n"
		"      \"delete_on_detonate\": true\n"
		"    } },\n"
		"    { \"id\": \"proxy\", \"kind\": \"entity.proxy_trigger\", \"params\": {\n"
		"      \"radius\": 300,\n"
		"      \"target_filter\": \"hostile_chr\",\n"
		"      \"team_filter\": \"enemy_only\",\n"
		"      \"line_of_sight\": true,\n"
		"      \"on_trigger\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"remote\", \"kind\": \"entity.remote_detonatable\", \"params\": {\n"
		"      \"detonator_ref\": \"base:remote_mine_detonator\",\n"
		"      \"owner_slot_source\": \"owner_inventory\",\n"
		"      \"on_remote_signal\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"timed\", \"kind\": \"entity.timed_detonatable\", \"params\": {\n"
		"      \"timer_ticks60\": 180,\n"
		"      \"starts_when\": \"armed\",\n"
		"      \"on_expire\": \"detonate\"\n"
		"    } },\n"
		"    { \"id\": \"storm\", \"kind\": \"entity.nbomb_storm\", \"params\": {\n"
		"      \"storm_ref\": \"base:nbomb_storm\",\n"
		"      \"activation_policy\": \"on_timer_or_proxy\",\n"
		"      \"delete_carrier\": true\n"
		"    } },\n"
		"    { \"id\": \"autogun\", \"kind\": \"entity.autogun\", \"params\": {\n"
		"      \"target_filter\": \"hostile_chr\",\n"
		"      \"team_policy\": \"owner_team\",\n"
		"      \"aim_distance\": 4000,\n"
		"      \"turn_speed\": 6,\n"
		"      \"fire_cadence\": 1000,\n"
		"      \"ammo_reserve\": 400,\n"
		"      \"net_authority\": \"server\",\n"
		"      \"friendly_fire_suppression\": true,\n"
		"      \"pickup_recover\": true\n"
		"    } },\n"
		"    { \"id\": \"cleanup\", \"kind\": \"entity.owner_cleanup\", \"params\": {\n"
		"      \"replace_existing_policy\": \"explode_previous\",\n"
		"      \"max_active_per_owner\": 1\n"
		"    } },\n"
		"    { \"id\": \"interaction\", \"kind\": \"entity.interaction\", \"params\": {\n"
		"      \"interact_filter\": \"owner\",\n"
		"      \"action\": \"recover_weapon\",\n"
		"      \"transfer_payload\": \"base:laptopgun\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [ { \"name\": \"main\", \"node\": \"armed\" } ]\n"
		"}\n";

	char err[256] = {};
	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(0);
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"base:test_projectile_payload", projectileGraph.data(),
		static_cast<u32>(projectileGraph.size()), err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetProjectileForGameplay(
		"base:test_projectile_payload") == nullptr);
	weaponGraphRuntimeSetEnabled(1);
	const weapon_graph_projectile_runtime_t *projectile =
		weaponGraphRuntimeGetProjectileForGameplay("base:test_projectile_payload");
	REQUIRE(projectile != nullptr);
	REQUIRE(projectile->has_projectile_modelnum == 1);
	REQUIRE(projectile->projectile_modelnum == MODEL_CHRSKROCKETMIS);
	REQUIRE(std::string(projectile->source_mode) == "secondary");
	REQUIRE(projectile->source_function_type_id == INVENTORYFUNCTYPE_SHOOT_PROJECTILE);
	REQUIRE(projectile->has_scale == 1);
	REQUIRE(projectile->scale == Approx(2.1f));
	REQUIRE(projectile->has_damage == 1);
	REQUIRE(projectile->damage == Approx(7.0f));
	REQUIRE(projectile->flags == 72);
	REQUIRE(std::string(projectile->motion_kind) == "powered");
	REQUIRE(projectile->has_speed == 1);
	REQUIRE(projectile->speed == Approx(60.0f));
	REQUIRE(projectile->has_timer60 == 1);
	REQUIRE(projectile->timer60 == -1);
	REQUIRE(projectile->powered == 1);
	REQUIRE(projectile->calculate_trajectory == 0);
	REQUIRE(projectile->has_homing == 1);
	REQUIRE(std::string(projectile->homing_target_source) == "current_lock");
	REQUIRE(projectile->has_fly_by_wire == 1);
	REQUIRE(std::string(projectile->fly_bot_route_policy) == "runtime_existing");
	REQUIRE(projectile->has_wall_hugger == 1);
	REQUIRE(projectile->wall_post_fall_timer60 == 360);
	REQUIRE(projectile->has_impact == 1);
	REQUIRE(projectile->impact_hit_sound == SFX_LAUNCH_ROCKET_8053);
	REQUIRE(projectile->has_transition_to_entity == 1);
	REQUIRE(std::string(projectile->entity_ref) == "base:test_entity_payload");
	REQUIRE(projectile->transfer_owner == 1);
	REQUIRE(projectile->delete_carrier == 1);

	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_ENTITY,
		"base:test_entity_payload", entityGraph.data(),
		static_cast<u32>(entityGraph.size()), err, sizeof(err)) == 0);
	const weapon_graph_entity_runtime_t *entity =
		weaponGraphRuntimeGetEntityForGameplay("base:test_entity_payload");
	REQUIRE(entity != nullptr);
	REQUIRE(entity->has_armed_explosive == 1);
	REQUIRE(std::string(entity->archetype) == "armed_proxy_explosive");
	REQUIRE(entity->projectile_modelnum == MODEL_CHRPROXIMITYMINE);
	REQUIRE(entity->activation_time60 == 240);
	REQUIRE(entity->recovery_time60 == 60);
	REQUIRE(entity->arm_delay_ticks60 == 240);
	REQUIRE(entity->delete_on_detonate == 1);
	REQUIRE(entity->has_proxy_trigger == 1);
	REQUIRE(entity->proxy_radius == Approx(300.0f));
	REQUIRE(entity->proxy_line_of_sight == 1);
	REQUIRE(entity->has_remote_detonatable == 1);
	REQUIRE(std::string(entity->detonator_ref) == "base:remote_mine_detonator");
	REQUIRE(entity->has_timed_detonatable == 1);
	REQUIRE(entity->timed_timer_ticks60 == 180);
	REQUIRE(entity->has_nbomb_storm == 1);
	REQUIRE(entity->storm_delete_carrier == 1);
	REQUIRE(entity->has_autogun == 1);
	REQUIRE(entity->autogun_ammo_reserve == 400);
	REQUIRE(entity->autogun_friendly_fire_suppression == 1);
	REQUIRE(entity->has_owner_cleanup == 1);
	REQUIRE(entity->max_active_per_owner == 1);
	REQUIRE(entity->has_interaction == 1);
	REQUIRE(std::string(entity->transfer_payload) == "base:laptopgun");

	const std::string heldGraph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_runtime_bridge_weapon\",\n"
		"  \"graph_id\": \"runtime_bridge_refs_v1\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"primary\", \"kind\": \"spawn.fired_projectile\", \"params\": {\n"
		"      \"mode\": \"primary\",\n"
		"      \"function_type\": \"shoot_projectile\",\n"
		"      \"projectile_ref\": \"base:test_projectile_payload\"\n"
		"    } },\n"
		"    { \"id\": \"secondary\", \"kind\": \"spawn.thrown_physical\", \"params\": {\n"
		"      \"mode\": \"secondary\",\n"
		"      \"function_type\": \"throw\",\n"
		"      \"payload_ref\": \"base:test_entity_payload\",\n"
		"      \"entity_ref\": \"base:test_entity_payload\"\n"
		"    } }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary\" },\n"
		"    { \"name\": \"secondary\", \"node\": \"secondary\" }\n"
		"  ]\n"
		"}\n";
	weapon_graph_ir_t heldIr;
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, heldGraph.data(),
		static_cast<u32>(heldGraph.size()), &heldIr, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(10, &heldIr, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *heldPrimary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(10, 0);
	const weapon_graph_held_function_t *heldSecondary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(10, 1);
	REQUIRE(weaponGraphRuntimeGetProjectileForHeldFunction(heldPrimary) == projectile);
	REQUIRE(weaponGraphRuntimeGetEntityForHeldFunction(heldSecondary) == entity);
	REQUIRE(weaponGraphRuntimeGetEntityForProjectile(projectile) == entity);

	const std::string projectileIni =
		"[projectile]\n"
		"catalog_id = base:test_projectile_payload\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive projectileArchive = writeArchiveEntries("projectile-runtime", {
		{ "projectile.ini", projectileIni },
		{ "behavior.graph.json", projectileGraph },
	});
	weaponGraphRuntimeClearAsset("base:test_projectile_payload");
	REQUIRE(weaponGraphRuntimeRegisterBehaviorArchive(ASSET_PROJECTILE,
		projectileArchive.path.string().c_str(), err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetProjectile(
		"base:test_projectile_payload") != nullptr);

	const std::string entityIni =
		"[entity]\n"
		"catalog_id = base:test_entity_payload\n"
		"behavior_graph = behavior.graph.json\n";
	TempArchive entityArchive = writeArchiveEntries("entity-runtime", {
		{ "entity.ini", entityIni },
		{ "behavior.graph.json", entityGraph },
	});
	weaponGraphRuntimeClearAsset("base:test_entity_payload");
	REQUIRE(weaponGraphRuntimeRegisterBehaviorArchive(ASSET_ENTITY,
		entityArchive.path.string().c_str(), err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeGetEntity("base:test_entity_payload") != nullptr);

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
		"        \"special_sound_catalog_id\": \"base:sfx_launch_rocket\"\n"
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

TEST_CASE("weapon graph runtime captures presentation adapter payloads",
          "[modding][pdxxx][weapon_graph][runtime][presentation][c3814]") {
	const std::string graph =
		"{\n"
		"  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"base:test_presentation_weapon\",\n"
		"  \"graph_id\": \"presentation_runtime_test_v1\",\n"
		"  \"nodes\": [\n"
		"    {\n"
		"      \"id\": \"primary_presentation\",\n"
		"      \"kind\": \"presentation.reticle_overlay_camera\",\n"
		"      \"params\": {\n"
		"        \"mode\": \"primary\",\n"
		"        \"function_type\": \"none\",\n"
		"        \"sight\": \"zoom\",\n"
		"        \"zoom_fov\": 12.5,\n"
		"        \"reticle_ref\": \"base:hud_zoom_reticle\",\n"
		"        \"overlay_ref\": \"base:hud_zoom_overlay\",\n"
		"        \"camera_effect\": \"runtime_existing_zoom\"\n"
		"      }\n"
		"    }\n"
		"  ],\n"
		"  \"edges\": [],\n"
		"  \"exports\": [\n"
		"    { \"name\": \"primary\", \"node\": \"primary_presentation\" }\n"
		"  ]\n"
		"}\n";

	weapon_graph_ir_t ir;
	char err[256] = {};
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, graph.data(),
		static_cast<u32>(graph.size()), &ir, err, sizeof(err)) == 0);

	weaponGraphRuntimeClearAll();
	weaponGraphRuntimeSetEnabled(1);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(11, &ir, err, sizeof(err)) == 0);

	const weapon_graph_held_function_t *primary =
		weaponGraphRuntimeGetHeldFunctionForGameplay(11, 0);
	REQUIRE(primary != nullptr);
	REQUIRE(primary->opcode ==
		WEAPON_GRAPH_OP_PRESENTATION_RETICLE_OVERLAY_CAMERA);
	REQUIRE(primary->has_sight == 1);
	REQUIRE(primary->sight == SIGHT_ZOOM);
	REQUIRE(primary->has_zoom_fov == 1);
	REQUIRE(primary->zoom_fov == Approx(12.5f));
	REQUIRE(std::string(primary->reticle_ref) == "base:hud_zoom_reticle");
	REQUIRE(std::string(primary->overlay_ref) == "base:hud_zoom_overlay");
	REQUIRE(std::string(primary->camera_effect) == "runtime_existing_zoom");

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
	REQUIRE(accessors.find("graph->has_zoom_fov") != std::string::npos);
	REQUIRE(accessors.find("graph->has_sight") != std::string::npos);

	const std::string bondgun = readFile("src/game/bondgun.c");
	REQUIRE(bondgun.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetHeldGraph") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetProjectileGraph") != std::string::npos);
	REQUIRE(bondgun.find("bgunGetEntityGraph") != std::string::npos);
	REQUIRE(bondgun.find("projectileApplyGraphRuntime") != std::string::npos);
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
	REQUIRE(chraction.find("weaponGraphRuntimeGetProjectileForHeldFunction") != std::string::npos);
	REQUIRE(chraction.find("graph->has_projectile_modelnum") != std::string::npos);
	REQUIRE(chraction.find("projectileApplyGraphRuntime") != std::string::npos);

	const std::string propobj = readFile("src/game/propobj.c");
	REQUIRE(propobj.find("weapon_graph_runtime.h") != std::string::npos);
	REQUIRE(propobj.find("projectileApplyGraphRuntime") != std::string::npos);
	REQUIRE(propobj.find("weaponGraphRuntimeGetEntityForHeldFunction") != std::string::npos);
	REQUIRE(propobj.find("entitygraph->proxy_radius") != std::string::npos);
	REQUIRE(propobj.find("entitygraph->autogun_aim_distance") != std::string::npos);

	const std::string runtime = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(runtime.find("weaponGraphRuntimeGetProjectileForHeldFunction") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeGetEntityForHeldFunction") != std::string::npos);
	REQUIRE(runtime.find("weaponGraphRuntimeGetEntityForProjectile") != std::string::npos);

	const std::string lifecycle = readFile("port/src/assetcatalog_load.c");
	REQUIRE(lifecycle.find("s_catalogTypeUsesWeaponGraphRuntime") != std::string::npos);
	REQUIRE(lifecycle.find("fsFileLoad(source_path") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeRegisterBehaviorGraphJson") != std::string::npos);
	REQUIRE(lifecycle.find("weaponGraphRuntimeClearAsset") != std::string::npos);
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
	REQUIRE(scanner.find("material.ini - reusable render/surface material metadata") != std::string::npos);
	REQUIRE(scanner.find("effect.ini - reusable visual/feedback effect metadata") != std::string::npos);
	REQUIRE(scanner.find("prop.ini - reusable spawnable prop metadata") != std::string::npos);
	REQUIRE(scanner.find("gamemode.ini - Combat Simulator/custom rules metadata") != std::string::npos);
	REQUIRE(scanner.find("rules_file = rules.json") != std::string::npos);
	REQUIRE(scanner.find("botprofile.ini - reusable bot skill/personality metadata") != std::string::npos);
	REQUIRE(scanner.find("profile_file = profile.json") != std::string::npos);
	REQUIRE(scanner.find("; target_body = base:body_id") != std::string::npos);
	REQUIRE(scanner.find("theme.ini - menu/UI theme bundle metadata") != std::string::npos);
	REQUIRE(scanner.find("animation.ini - external animation metadata") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(owner_id, dep, is_bundled)") != std::string::npos);
	REQUIRE(scanner.find("catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled)") != std::string::npos);
	REQUIRE(scanner.find("\"animation_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"rules_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"profile_file\"") != std::string::npos);
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
	REQUIRE(catalog.find("ASSET_FONT") != std::string::npos);
	REQUIRE(catalog.find("char strings_file[128]") != std::string::npos);

	std::string theme = readFile("port/fast3d/pdgui_theme.cpp");
	REQUIRE(theme.find("stbi_load_from_memory") != std::string::npos);
	REQUIRE(theme.find("GL_MAX_TEXTURE_SIZE") != std::string::npos);
	REQUIRE(theme.find("pdguiFontMgrLoadFont(entry->id, path, 24.0f)") != std::string::npos);
	REQUIRE(theme.find("assetCatalogIterateByType(ASSET_UI, s_applyCatalogUiAsset") != std::string::npos);
	REQUIRE(theme.find("assetCatalogIterateByType(ASSET_FONT, s_applyCatalogUiAsset") != std::string::npos);

	std::string theme_loader = readFile("port/fast3d/pdgui_theme_loader.cpp");
	REQUIRE(theme_loader.find("assetCatalogRegister(k_BuiltinIds[i], ASSET_THEME)") != std::string::npos);
	REQUIRE(theme_loader.find("assetCatalogIterateByType(ASSET_THEME, register_catalog_theme_entry") != std::string::npos);
	REQUIRE(theme_loader.find("fileProviderPath(entry->source.primary)") != std::string::npos);

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

TEST_CASE("catalog game mode and bot profile assets drive live runtime selectors",
          "[modding][pdxxx][runtime][c3838]") {
	std::string scenarios = readFile("src/game/mplayer/scenarios.c");
	REQUIRE(scenarios.find("assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioCountCb, &ctx)") != std::string::npos);
	REQUIRE(scenarios.find("assetCatalogIterateUnlockedByType(ASSET_GAMEMODE, scenarioPickByIndexCb, &ctx)") != std::string::npos);
	REQUIRE(scenarios.find("g_MpSetup.scenario = ctx.scenario_match") != std::string::npos);
	REQUIRE(scenarios.find("scenarioInit()") != std::string::npos);
	REQUIRE(scenarios.find("g_MpScenarioOverviews[e->ext.gamemode.mode_id].name") != std::string::npos);

	std::string setup = readFile("src/game/mplayer/setup.c");
	REQUIRE(setup.find("assetCatalogIterateUnlockedByType(ASSET_BOT_PROFILE, botprofileCountCb, &ctx)") != std::string::npos);
	REQUIRE(setup.find("assetCatalogIterateUnlockedByType(ASSET_BOT_PROFILE, botprofilePickByIndexCb, &ctx)") != std::string::npos);
	REQUIRE(setup.find("mpCreateBotFromProfile(botnum, profnum)") != std::string::npos);
	REQUIRE(setup.find("g_BotConfigsArray[botnum].type = g_BotProfiles[profnum].type") != std::string::npos);
	REQUIRE(setup.find("assetCatalogIterateUnlockedByType(ASSET_BOT_PROFILE, botdiffCountCb, &ctx)") != std::string::npos);
	REQUIRE(setup.find("return (uintptr_t)langGet(L_MISC_082 + ctx.result_diffidx)") != std::string::npos);

	std::string base = readFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(base.find("assetCatalogRegisterGameMode(") != std::string::npos);
	REQUIRE(base.find("assetCatalogRegisterBotProfile(") != std::string::npos);
	REQUIRE(base.find("e->mp_index = (s16)s_BaseGameModes[i].mode_id") != std::string::npos);
	REQUIRE(base.find("e->mp_index = (s16)i") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("case ASSET_GAMEMODE:") != std::string::npos);
	REQUIRE(scanner.find("e->ext.gamemode.mode_id = parseModeKeyValue(") != std::string::npos);
	REQUIRE(scanner.find("e->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_BOT_PROFILE:") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.type = parseBotTypeKeyValue(") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.difficulty = parseBotDifficultyKeyValue(") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.target_body") != std::string::npos);
	REQUIRE(scanner.find("e->ext.bot_profile.profile_file") != std::string::npos);

	std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("char rules_file[128]") != std::string::npos);
	REQUIRE(catalog.find("char target_body[CATALOG_ID_LEN]") != std::string::npos);
	REQUIRE(catalog.find("char profile_file[128]") != std::string::npos);

	std::string runtime = readFile("port/src/asset_runtime.c");
	REQUIRE(runtime.find("entry->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.profile_file") != std::string::npos);
	REQUIRE(runtime.find("entry->ext.bot_profile.target_body") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("distribParseModeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseBotTypeKeyValue") != std::string::npos);
	REQUIRE(distrib.find("distribParseBotDifficultyKeyValue") != std::string::npos);
	REQUIRE(distrib.find("e->ext.gamemode.rules_file") != std::string::npos);
	REQUIRE(distrib.find("e->ext.bot_profile.target_body") != std::string::npos);
}

TEST_CASE("skin assets expose saved texture payloads through catalog provider paths",
          "[modding][pdxxx][runtime][c3838][skin]") {
	const std::string catalog = readFile("port/include/assetcatalog.h");
	REQUIRE(catalog.find("char texture_file[128];          /* appearance payload for this skin */") != std::string::npos);

	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("case ASSET_SKIN:") != std::string::npos);
	REQUIRE(scanner.find("texture_file = texture.tga") != std::string::npos);
	REQUIRE(scanner.find("e->ext.skin.texture_file") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.skin.texture_file)") != std::string::npos);

	const std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.skin.texture_file)") != std::string::npos);

	const std::string editor = readFile("port/fast3d/pdgui_skin_editor.cpp");
	REQUIRE(editor.find("texture_file = texture.tga") != std::string::npos);
	REQUIRE(editor.find("catalogSetPrimaryFile(entry, entry->ext.skin.texture_file)") != std::string::npos);

	const std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("type == ASSET_SKIN") != std::string::npos);
}

TEST_CASE("metadata asset families expose primary authored files through catalog provider paths",
          "[modding][pdxxx][runtime][c3838][files]") {
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("material_file = material.json") != std::string::npos);
	REQUIRE(scanner.find("texture_file = texture.png") != std::string::npos);
	REQUIRE(scanner.find("effect_file = effect.graph.json") != std::string::npos);
	REQUIRE(scanner.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(scanner.find("scenario_archive = dependencies/assets/scenario/mission.pdscenario") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_MATERIAL:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_EFFECT:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_HUD:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_VEHICLE:") != std::string::npos);
	REQUIRE(scanner.find("case ASSET_MISSION:") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"material_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"effect_file\"") != std::string::npos);
	REQUIRE(scanner.find("catalogSetPrimaryFile(e, e->ext.hud.texture_file)") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scenario_archive\"") != std::string::npos);

	const std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("case ASSET_MATERIAL:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_EFFECT:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_HUD:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_VEHICLE:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_MISSION:") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, pf)") != std::string::npos);
	REQUIRE(distrib.find("distribSetPrimaryFromFile(e, dirpath, e->ext.hud.texture_file)") != std::string::npos);

	const std::string load = readFile("port/src/assetcatalog_load.c");
	REQUIRE(load.find("type == ASSET_VEHICLE") != std::string::npos);
	REQUIRE(load.find("type == ASSET_MISSION") != std::string::npos);
	REQUIRE(load.find("type == ASSET_HUD") != std::string::npos);
	REQUIRE(load.find("type == ASSET_EFFECT") != std::string::npos);
	REQUIRE(load.find("type == ASSET_MATERIAL") != std::string::npos);
}

TEST_CASE("external models maps and animations compile from standard sources",
          "[modding][pdmod][static][c3809]") {
	std::string compiler_h = readFile("port/include/modasset_compiler.h");
	REQUIRE(compiler_h.find("MODASSET_COMPILER_VERSION") != std::string::npos);
	REQUIRE(compiler_h.find("modasset_compiled_result_t") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerIsExternalSource") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerCompileReadable") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerEnsureCache") != std::string::npos);
	REQUIRE(compiler_h.find("modAssetCompilerBuildColmesh") != std::string::npos);
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
	REQUIRE(compiler.find("snprintf(cache_root, sizeof(cache_root), \"$S/mod-cache\")") !=
	        std::string::npos);
	REQUIRE(compiler.find("snprintf(cache_root, sizeof(cache_root), \"$B/mod-cache\")") !=
	        std::string::npos);
	REQUIRE(compiler.find("$S/mod-cache") != std::string::npos);
	REQUIRE(compiler.find("$B/mod-cache") != std::string::npos);
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
	REQUIRE(load.find("modAssetCompilerBuildColmesh(colmesh_source_path, mesh)") != std::string::npos);
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
	REQUIRE(resolve.find("entry->type == ASSET_SCENARIO && entry->ext.scenario.stagenum") != std::string::npos);
	REQUIRE(resolve.find("assetCatalogIterateByType(ASSET_SCENARIO, findMapCb, &ctx)") != std::string::npos);
	REQUIRE(resolve.find("assetCatalogIterateByType(ASSET_ARENA, findMapCb, &ctx)") != std::string::npos);

	std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(scanner.find("scenario.ini - external scenario source metadata") != std::string::npos);
	REQUIRE(scanner.find("scene_file = scene.glb") != std::string::npos);
	REQUIRE(scanner.find("\"rooms_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("\"runtime_source_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"rooms_file\"") != std::string::npos);
	REQUIRE(scanner.find("iniGet(ini, \"scene_file\"") != std::string::npos);
	REQUIRE(scanner.find("e->source_animnum = e->ext.anim.anim_id") != std::string::npos);

	std::string distrib = readFile("port/src/net/netdistrib.c");
	REQUIRE(distrib.find("\"head.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"body.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"arena.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"scenario.ini\"") != std::string::npos);
	REQUIRE(distrib.find("\"animation.ini\"") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_ANIMATION:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_SCENARIO:") != std::string::npos);
	REQUIRE(distrib.find("case ASSET_GAMEMODE:") != std::string::npos);
}

TEST_CASE("packed pdmod transport fixture resolves typed pdxxx content through production VFS",
          "[modding][pdmod][pdxxx][runtime][static][c3809][c3838][files]") {
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

TEST_CASE("typed pdxxx archive entries stay reachable through fs and FileProvider loaders",
          "[modding][pdxxx][runtime][static][c3838][files]") {
	const std::string fs = readFile("port/src/fs.c");
	REQUIRE(fs.find("fsLoadNestedArchiveEntry(name, outSize)") != std::string::npos);
	REQUIRE(fs.find("modVfsResolveAnyAlloc(archiveName, &archiveSize, NULL, 0)") != std::string::npos);
	REQUIRE(fs.find("modVfsResolveAnyAlloc(name, &vfsSize, NULL, 0)") != std::string::npos);
	REQUIRE(fs.find("modVfsGetSize(name)") != std::string::npos);

	const std::string provider = readFile("port/src/assetprovider_file.c");
	REQUIRE(provider.find("fsFileSize(path)") != std::string::npos);
	REQUIRE(provider.find("fsFileLoad(path, &size)") != std::string::npos);

	const std::string dispatcher = readFile("port/src/assetload.c");
	REQUIRE(dispatcher.find("return handle.provider->load(handle.provider, handle, buf, buf_size)") != std::string::npos);
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
	REQUIRE(readme.find("The GLTF, OBJ, INI, JSON, TSV, and source media files inside each archive are the authored data") != std::string::npos);
	REQUIRE(readme.find("Machine-owned manifest and provenance data lives under `_meta/`") != std::string::npos);
	REQUIRE(readme.find("`.pdmod` is not the authoring format") != std::string::npos);

	const char *requiredFiles[] = {
		"mod.json",
		"characters/tri_character.pdcharacter",
		"heads/tri_head.pdhead",
		"bodies/tri_body.pdbody",
		"arenas/tri_arena.pdarena",
		"meshes/tri_mesh.pdmesh",
		"materials/tri_material.pdmaterial",
		"textures/tri_texture.pdtexture",
		"skins/tri_skin.pdskin",
		"effects/tri_effect.pdeffect",
		"props/tri_prop.pdprop",
		"vehicles/tri_vehicle.pdvehicle",
		"missions/tri_mission.pdmission",
		"gamemodes/tri_gamemode.pdgamemode",
		"botprofiles/tri_botprofile.pdbotprofile",
		"hud/tri_hud.pdhud",
		"themes/tri_theme.pdtheme",
		"entities/tri_entity.pdentity",
		"projectiles/tri_projectile.pdprojectile",
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

	const std::string characterArchivePath =
		"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter";
	const std::string headArchivePath =
		"examples/modding/typed-pdxxx-basic/heads/tri_head.pdhead";
	const std::string bodyArchivePath =
		"examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody";
	const std::string arenaArchivePath =
		"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";
	const std::string meshArchivePath =
		"examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh";
	const std::string materialArchivePath =
		"examples/modding/typed-pdxxx-basic/materials/tri_material.pdmaterial";
	const std::string textureArchivePath =
		"examples/modding/typed-pdxxx-basic/textures/tri_texture.pdtexture";
	const std::string skinArchivePath =
		"examples/modding/typed-pdxxx-basic/skins/tri_skin.pdskin";
	const std::string effectArchivePath =
		"examples/modding/typed-pdxxx-basic/effects/tri_effect.pdeffect";
	const std::string propArchivePath =
		"examples/modding/typed-pdxxx-basic/props/tri_prop.pdprop";
	const std::string vehicleArchivePath =
		"examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle";
	const std::string missionArchivePath =
		"examples/modding/typed-pdxxx-basic/missions/tri_mission.pdmission";
	const std::string gamemodeArchivePath =
		"examples/modding/typed-pdxxx-basic/gamemodes/tri_gamemode.pdgamemode";
	const std::string botprofileArchivePath =
		"examples/modding/typed-pdxxx-basic/botprofiles/tri_botprofile.pdbotprofile";
	const std::string hudArchivePath =
		"examples/modding/typed-pdxxx-basic/hud/tri_hud.pdhud";
	const std::string themeArchivePath =
		"examples/modding/typed-pdxxx-basic/themes/tri_theme.pdtheme";
	const std::string entityArchivePath =
		"examples/modding/typed-pdxxx-basic/entities/tri_entity.pdentity";
	const std::string projectileArchivePath =
		"examples/modding/typed-pdxxx-basic/projectiles/tri_projectile.pdprojectile";
	const std::string weaponArchiveFullPath =
		"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
	const std::string weaponArchivePath =
		"examples/modding/typed-pdxxx-basic/animations/weapon_idle.pdanim";
	const std::string skeletalArchivePath =
		"examples/modding/typed-pdxxx-basic/animations/character_skeletal.pdanim";

	const std::string character = readArchiveEntryText(characterArchivePath.c_str(), "character.ini");
	REQUIRE(character.find("catalog_id = example:tri_character") != std::string::npos);
	REQUIRE(character.find("body_archive = dependencies/assets/body/tri_body.pdbody") != std::string::npos);
	REQUIRE(character.find("head_archive = dependencies/assets/head/tri_head.pdhead") != std::string::npos);

	const std::string head = readArchiveEntryText(headArchivePath.c_str(), "head.ini");
	REQUIRE(head.find("catalog_id = example:tri_head") != std::string::npos);
	REQUIRE(head.find("mesh_archive = mesh.pdmesh") != std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(headArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(archiveHasEntry(opened.archive, "mesh.pdmesh"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "model.gltf"));
	}

	const std::string body = readArchiveEntryText(bodyArchivePath.c_str(), "body.ini");
	REQUIRE(body.find("catalog_id = example:tri_body") != std::string::npos);
	REQUIRE(body.find("rig_class = human_male_neck_standard") != std::string::npos);
	REQUIRE(body.find("mesh_archive = mesh.pdmesh") != std::string::npos);
	REQUIRE(body.find("hand_archive = hand.pdmesh") != std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(bodyArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(archiveHasEntry(opened.archive, "mesh.pdmesh"));
		REQUIRE(archiveHasEntry(opened.archive, "hand.pdmesh"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "model.gltf"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "hand.gltf"));
	}

	const std::string arena = readArchiveEntryText(arenaArchivePath.c_str(), "arena.ini");
	REQUIRE(arena.find("catalog_id = example:tri_arena") != std::string::npos);
	REQUIRE(arena.find("scenario = example:tri_scenario") != std::string::npos);
	REQUIRE(arena.find("scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario") != std::string::npos);
	REQUIRE(arena.find("geometry_file") == std::string::npos);
	{
		OpenArchive opened;
		opened.archive = modArchiveOpen(arenaArchivePath.c_str());
		REQUIRE(opened.archive != nullptr);
		REQUIRE(archiveHasEntry(opened.archive,
			"dependencies/assets/scenarios/tri_scenario.pdscenario"));
		REQUIRE_FALSE(archiveHasEntry(opened.archive, "geometry.obj"));
	}

	const std::string mesh = readArchiveEntryText(meshArchivePath.c_str(), "mesh.ini");
	const std::string meshModel = readArchiveEntryText(meshArchivePath.c_str(), "model.gltf");
	REQUIRE(mesh.find("catalog_id = example:tri_mesh") != std::string::npos);
	REQUIRE(mesh.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(meshModel.find("\"TEXCOORD_0\"") != std::string::npos);
	REQUIRE(meshModel.find("\"baseColorFactor\"") != std::string::npos);

	const std::string material = readArchiveEntryText(materialArchivePath.c_str(), "material.ini");
	REQUIRE(material.find("catalog_id = example:tri_material") != std::string::npos);
	REQUIRE(material.find("texture_archive = dependencies/assets/texture/tri_texture.pdtexture") != std::string::npos);

	const std::string texture = readArchiveEntryText(textureArchivePath.c_str(), "texture.ini");
	REQUIRE(texture.find("catalog_id = example:tri_texture") != std::string::npos);
	REQUIRE(texture.find("texture_file = texture.png") != std::string::npos);

	const std::string skin = readArchiveEntryText(skinArchivePath.c_str(), "skin.ini");
	REQUIRE(skin.find("catalog_id = example:tri_skin") != std::string::npos);
	REQUIRE(skin.find("texture_file = texture.tga") != std::string::npos);
	REQUIRE(skin.find("material_archive = dependencies/assets/material/tri_material.pdmaterial") != std::string::npos);

	const std::string effect = readArchiveEntryText(effectArchivePath.c_str(), "effect.ini");
	const std::string effectGraph = readArchiveEntryText(effectArchivePath.c_str(), "effect.graph.json");
	REQUIRE(effect.find("catalog_id = example:tri_effect") != std::string::npos);
	REQUIRE(effect.find("effect_key = glow") != std::string::npos);
	REQUIRE(effect.find("target_key = weapon") != std::string::npos);
	REQUIRE(effect.find("effect_type") == std::string::npos);
	REQUIRE(effect.find("effect_file = effect.graph.json") != std::string::npos);
	REQUIRE(effectGraph.find("\"schema\": \"pd.effect_graph.v1\"") != std::string::npos);

	const std::string prop = readArchiveEntryText(propArchivePath.c_str(), "prop.ini");
	REQUIRE(prop.find("catalog_id = example:tri_prop") != std::string::npos);
	REQUIRE(prop.find("prop_key = object") != std::string::npos);
	REQUIRE(prop.find("prop_type") == std::string::npos);
	REQUIRE(prop.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(prop.find("behavior_graph = behavior.graph.json") != std::string::npos);

	const std::string vehicle = readArchiveEntryText(vehicleArchivePath.c_str(), "vehicle.ini");
	REQUIRE(vehicle.find("catalog_id = example:tri_vehicle") != std::string::npos);
	REQUIRE(vehicle.find("model_file = model.gltf") != std::string::npos);
	REQUIRE(vehicle.find("physics_file = physics.json") != std::string::npos);

	const std::string mission = readArchiveEntryText(missionArchivePath.c_str(), "mission.ini");
	REQUIRE(mission.find("catalog_id = example:tri_mission") != std::string::npos);
	REQUIRE(mission.find("mission_graph_file = mission.graph.json") != std::string::npos);
	REQUIRE(mission.find("scenario_archive = dependencies/assets/scenarios/tri_scenario.pdscenario") != std::string::npos);
	REQUIRE(mission.find("scenario_graph_cache = pdscenario_scene_glb_clean_public_v77_standalone_backfill_collision_obj_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound_quip_shuffle_graph_portals") != std::string::npos);
	REQUIRE(mission.find("objectives_file = objectives.tsv") != std::string::npos);

	const std::string gamemode = readArchiveEntryText(gamemodeArchivePath.c_str(), "gamemode.ini");
	REQUIRE(gamemode.find("catalog_id = example:tri_gamemode") != std::string::npos);
	REQUIRE(gamemode.find("mode_key = custom") != std::string::npos);
	REQUIRE(gamemode.find("mode_id") == std::string::npos);
	REQUIRE(gamemode.find("rules_file = rules.json") != std::string::npos);

	const std::string botprofile = readArchiveEntryText(botprofileArchivePath.c_str(), "botprofile.ini");
	REQUIRE(botprofile.find("catalog_id = example:tri_botprofile") != std::string::npos);
	REQUIRE(botprofile.find("type_key = general") != std::string::npos);
	REQUIRE(botprofile.find("difficulty_key = normal") != std::string::npos);
	REQUIRE(botprofile.find("profile_file = profile.json") != std::string::npos);

	const std::string hud = readArchiveEntryText(hudArchivePath.c_str(), "hud.ini");
	REQUIRE(hud.find("catalog_id = example:tri_hud") != std::string::npos);
	REQUIRE(hud.find("hud_key = ammo") != std::string::npos);
	REQUIRE(hud.find("hud_id") == std::string::npos);
	REQUIRE(hud.find("element_type") == std::string::npos);
	REQUIRE(hud.find("texture_file = texture.png") != std::string::npos);
	REQUIRE(hud.find("layout_file = layout.json") != std::string::npos);

	const std::string theme = readArchiveEntryText(themeArchivePath.c_str(), "theme.ini");
	REQUIRE(theme.find("catalog_id = example:tri_theme") != std::string::npos);
	REQUIRE(theme.find("theme_file = theme.json") != std::string::npos);
	REQUIRE(theme.find("ui_archive = dependencies/assets/ui/tri_reticle.pdui") != std::string::npos);

	const std::string entity = readArchiveEntryText(entityArchivePath.c_str(), "entity.ini");
	const std::string bindings = readArchiveEntryText(entityArchivePath.c_str(), "bindings.json");
	REQUIRE(entity.find("catalog_id = example:tri_entity") != std::string::npos);
	REQUIRE(entity.find("bindings_file = bindings.json") != std::string::npos);
	REQUIRE(bindings.find("\"schema\": \"pd.entity.bindings.v1\"") != std::string::npos);

	const std::string projectile = readArchiveEntryText(projectileArchivePath.c_str(), "projectile.ini");
	REQUIRE(projectile.find("catalog_id = example:tri_projectile") != std::string::npos);
	REQUIRE(projectile.find("behavior_graph = behavior.graph.json") != std::string::npos);

	const std::string weapon = readArchiveEntryText(weaponArchiveFullPath.c_str(), "weapon.ini");
	const std::string weaponPrimaryGraph =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior/primary.graph.json");
	const std::string weaponSecondaryGraph =
		readArchiveEntryText(weaponArchiveFullPath.c_str(), "behavior/secondary.graph.json");
	REQUIRE(weapon.find("catalog_id = example:tri_weapon") != std::string::npos);
	REQUIRE(weapon.find("model_file = dependencies/assets/models/weapon.pdmesh") != std::string::npos);
	REQUIRE(weapon.find("primary_graph = behavior/primary.graph.json") != std::string::npos);
	REQUIRE(weapon.find("secondary_graph = behavior/secondary.graph.json") != std::string::npos);
	REQUIRE(weapon.find("behavior_graph") == std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"schema\": \"pd.weapon_graph.v1\"") != std::string::npos);
	REQUIRE(weaponPrimaryGraph.find("\"asset_id\": \"example:tri_weapon\"") != std::string::npos);
	REQUIRE(weaponSecondaryGraph.find("\"graph_id\": \"secondary\"") != std::string::npos);
	REQUIRE(weapon.find("hand_model_file") == std::string::npos);
	REQUIRE(weapon.find("texture_file = weapon_texture.png") == std::string::npos);
	REQUIRE(weapon.find("animation_file = reload.gltf") == std::string::npos);
	REQUIRE(weapon.find("file_path = fire.wav") == std::string::npos);

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
	REQUIRE_FALSE(std::filesystem::exists(root / "characters/tri_character/character.ini"));
	REQUIRE_FALSE(std::filesystem::exists(root / "projectiles/tri_projectile/projectile.ini"));
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
			"character",
			".pdcharacter",
			"characters/tri_character.pdcharacter",
			{ "character.ini", "portrait.png",
			  "dependencies/assets/body/tri_body.pdbody",
			  "dependencies/assets/head/tri_head.pdhead",
			  "_meta/manifest.json" },
		},
		{
			"head",
			".pdhead",
			"heads/tri_head.pdhead",
			{ "head.ini", "mesh.pdmesh", "_meta/manifest.json" },
		},
		{
			"body",
			".pdbody",
			"bodies/tri_body.pdbody",
			{ "body.ini", "mesh.pdmesh", "hand.pdmesh",
			  "_meta/manifest.json" },
		},
		{
			"arena",
			".pdarena",
			"arenas/tri_arena.pdarena",
			{ "arena.ini",
			  "dependencies/assets/scenarios/tri_scenario.pdscenario",
			  "_meta/manifest.json" },
		},
		{
			"mesh/model",
			".pdmesh",
			"meshes/tri_mesh.pdmesh",
			{ "mesh.ini", "model.gltf",
			  "_meta/manifest.json" },
		},
		{
			"material",
			".pdmaterial",
			"materials/tri_material.pdmaterial",
			{ "material.ini",
			  "dependencies/assets/texture/tri_texture.pdtexture",
			  "_meta/manifest.json" },
		},
		{
			"texture",
			".pdtexture",
			"textures/tri_texture.pdtexture",
			{ "texture.ini", "texture.png", "_meta/manifest.json" },
		},
		{
			"skin",
			".pdskin",
			"skins/tri_skin.pdskin",
			{ "skin.ini", "texture.tga",
			  "dependencies/assets/material/tri_material.pdmaterial",
			  "_meta/manifest.json" },
		},
		{
			"effect",
			".pdeffect",
			"effects/tri_effect.pdeffect",
			{ "effect.ini", "effect.graph.json", "_meta/manifest.json" },
		},
		{
			"prop",
			".pdprop",
			"props/tri_prop.pdprop",
			{ "prop.ini", "model.gltf", "behavior.graph.json",
			  "_meta/manifest.json" },
		},
		{
			"vehicle",
			".pdvehicle",
			"vehicles/tri_vehicle.pdvehicle",
			{ "vehicle.ini", "model.gltf", "physics.json",
			  "behavior.graph.json", "_meta/manifest.json" },
		},
		{
			"mission",
			".pdmission",
			"missions/tri_mission.pdmission",
			{ "mission.ini", "mission.graph.json", "objectives.tsv", "briefing.tsv",
			  "dependencies/assets/scenarios/tri_scenario.pdscenario",
			  "_meta/manifest.json" },
		},
		{
			"gamemode",
			".pdgamemode",
			"gamemodes/tri_gamemode.pdgamemode",
			{ "gamemode.ini", "rules.json", "_meta/manifest.json" },
		},
		{
			"bot profile",
			".pdbotprofile",
			"botprofiles/tri_botprofile.pdbotprofile",
			{ "botprofile.ini", "profile.json", "_meta/manifest.json" },
		},
		{
			"HUD",
			".pdhud",
			"hud/tri_hud.pdhud",
			{ "hud.ini", "texture.png", "layout.json",
			  "_meta/manifest.json" },
		},
		{
			"theme",
			".pdtheme",
			"themes/tri_theme.pdtheme",
			{ "theme.ini", "theme.json",
			  "dependencies/assets/ui/tri_reticle.pdui",
			  "dependencies/assets/font/tri_font.pdfont",
			  "_meta/manifest.json" },
		},
		{
			"entity",
			".pdentity",
			"entities/tri_entity.pdentity",
			{ "entity.ini", "bindings.json", "behavior.graph.json",
			  "composition.json", "_meta/manifest.json" },
		},
		{
			"projectile",
			".pdprojectile",
			"projectiles/tri_projectile.pdprojectile",
			{ "projectile.ini", "behavior.graph.json",
			  "_meta/manifest.json" },
		},
		{
			"weapon",
			".pdweapon",
			"weapons/tri_weapon.pdweapon",
			{ "weapon.ini", "behavior/primary.graph.json",
			  "behavior/secondary.graph.json", "behavior/settings.json",
			  "behavior/variables.json", "behavior/shared-context.json",
			  "bindings/material-slots.json", "bindings/grip-sockets.json",
			  "bindings/presentation.json",
			  "dependencies/assets/models/weapon.pdmesh",
			  "dependencies/assets/projectiles/primary.pdprojectile",
			  "_meta/manifest.json" },
		},
		{
			"weapon animation",
			".pdanim",
			"animations/weapon_idle.pdanim",
			{ "animation.ini", "animation.gltf", "_meta/manifest.json" },
		},
		{
			"character animation",
			".pdanim",
			"animations/character_skeletal.pdanim",
			{ "animation.ini", "animation.gltf", "_meta/manifest.json" },
		},
		{
			"sound effect",
			".pdsfx",
			"audio/sfx/tri_click.pdsfx",
			{ "sound.ini", "sample.wav", "_meta/manifest.json" },
		},
		{
			"voice",
			".pdvoice",
			"audio/voice/tri_voice.pdvoice",
			{ "voice.ini", "sample.wav", "_meta/manifest.json" },
		},
		{
			"music",
			".pdsong",
			"audio/music/tri_song.pdsong",
			{ "music.ini", "track.wav", "_meta/manifest.json" },
		},
		{
			"UI texture",
			".pdui",
			"ui/tri_reticle.pdui",
			{ "ui.ini", "texture.png", "_meta/manifest.json" },
		},
		{
			"font",
			".pdfont",
			"fonts/tri_font.pdfont",
			{ "font.ini", "font.otf", "_meta/manifest.json" },
		},
		{
			"language",
			".pdlang",
			"lang/tri_lang.pdlang",
			{ "lang.ini", "strings.tsv", "_meta/manifest.json" },
		},
		{
			"scenario",
			".pdscenario",
			"scenarios/tri_scenario.pdscenario",
			{ "scenario.ini", "scene.glb",
			  "pads.tsv", "spawns.tsv", "volumes.tsv", "objects.tsv",
			  "setup.fields.tsv", "ai/ailists.tsv", "objectives.tsv",
			  "navigation/waypoints.tsv", "navigation/waygroups.tsv",
			  "navigation/covers.tsv", "navigation/paths.tsv",
			  "navigation.ini", "level.graph.json",
			  "_meta/generated-collision.json", "_meta/generated-navmesh.json",
			  "_meta/manifest.json" },
		},
	};

	for (const ExpectedPdxxxArchive &spec : implementedArchives) {
		const std::filesystem::path archivePath = root / spec.relPath;
		INFO(spec.assetType << " " << archivePath.generic_string());
		REQUIRE(std::filesystem::is_regular_file(archivePath));
		char err[256];
		REQUIRE(assetArchiveValidateFile(archivePath.string().c_str(),
			ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

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
			"characters/tri_character.pdcharacter",
			"character.ini",
			{ "body_archive", "head_archive", "portrait_file" },
			{},
			{},
		},
		{
			"heads/tri_head.pdhead",
			"head.ini",
			{ "mesh_archive" },
			{},
			{},
		},
		{
			"bodies/tri_body.pdbody",
			"body.ini",
			{ "mesh_archive", "hand_archive" },
			{},
			{},
		},
		{
			"arenas/tri_arena.pdarena",
			"arena.ini",
			{ "scenario_archive" },
			{},
			{},
		},
		{
			"meshes/tri_mesh.pdmesh",
			"mesh.ini",
			{ "model_file" },
			{ "model.gltf" },
			{},
		},
		{
			"materials/tri_material.pdmaterial",
			"material.ini",
			{ "texture_archive" },
			{},
			{},
		},
		{
			"textures/tri_texture.pdtexture",
			"texture.ini",
			{ "texture_file" },
			{},
			{},
		},
		{
			"skins/tri_skin.pdskin",
			"skin.ini",
			{ "texture_file", "material_archive" },
			{},
			{},
		},
		{
			"effects/tri_effect.pdeffect",
			"effect.ini",
			{ "effect_file" },
			{ "effect.graph.json" },
			{},
		},
		{
			"props/tri_prop.pdprop",
			"prop.ini",
			{ "model_file", "behavior_graph" },
			{ "model.gltf", "behavior.graph.json" },
			{},
		},
		{
			"vehicles/tri_vehicle.pdvehicle",
			"vehicle.ini",
			{ "model_file", "physics_file", "behavior_graph" },
			{ "model.gltf", "physics.json", "behavior.graph.json" },
			{},
		},
		{
			"missions/tri_mission.pdmission",
			"mission.ini",
			{ "mission_graph_file", "scenario_archive", "objectives_file", "briefing_file" },
			{},
			{},
		},
		{
			"gamemodes/tri_gamemode.pdgamemode",
			"gamemode.ini",
			{ "rules_file" },
			{ "rules.json" },
			{},
		},
		{
			"botprofiles/tri_botprofile.pdbotprofile",
			"botprofile.ini",
			{ "profile_file" },
			{ "profile.json" },
			{},
		},
		{
			"hud/tri_hud.pdhud",
			"hud.ini",
			{ "texture_file", "layout_file" },
			{ "layout.json" },
			{},
		},
		{
			"themes/tri_theme.pdtheme",
			"theme.ini",
			{ "theme_file", "ui_archive", "font_archive" },
			{ "theme.json" },
			{},
		},
		{
			"entities/tri_entity.pdentity",
			"entity.ini",
			{ "bindings_file", "behavior_graph", "composition_file" },
			{ "behavior.graph.json", "bindings.json", "composition.json" },
			{},
		},
		{
			"projectiles/tri_projectile.pdprojectile",
			"projectile.ini",
			{ "behavior_graph" },
			{ "behavior.graph.json" },
			{},
		},
		{
			"weapons/tri_weapon.pdweapon",
			"weapon.ini",
			{ "model_file", "primary_graph", "secondary_graph",
			  "settings_file", "variables_file", "shared_context_file",
			  "material_slots_file", "grip_sockets_file",
			  "presentation_file", "primary_projectile_archive",
			  "deployed_entity_archive", "fire_sound_archive",
			  "idle_animation_archive", "reticle_archive" },
			{},
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
			{ "scene_file", "runtime_source_file", "pads_file",
			  "spawns_file", "volumes_file", "objects_file",
			  "setup_fields_file", "objectives_file", "navigation_file", "level_graph_file" },
			{},
			{},
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
	const std::string scanner = readFile("port/src/assetcatalog_scanner.c");
	REQUIRE(!hub.empty());
	REQUIRE(!scanner.empty());

	REQUIRE(hub.find("Use as Template") != std::string::npos);
	REQUIRE(hub.find("Open Creator") != std::string::npos);
	REQUIRE(hub.find("Create Weapon Mod") != std::string::npos);
	REQUIRE(hub.find("Weapon Mod Creation") != std::string::npos);
	REQUIRE(hub.find("Back to Weapons") != std::string::npos);
	REQUIRE(hub.find("weaponToolStartTemplate") != std::string::npos);
	REQUIRE(hub.find("weaponToolPopulateTemplateRefs") != std::string::npos);
	REQUIRE(hub.find("weaponIniGetValue") != std::string::npos);
	REQUIRE(hub.find("weaponCopyCatalogOrArchiveRef") != std::string::npos);
	REQUIRE(hub.find("s_WeaponEditTemplateModelFile") != std::string::npos);
	REQUIRE(hub.find("s_WeaponEditTemplateAnimationFile") != std::string::npos);
	REQUIRE(hub.find("models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(hub.find("weaponArchiveReadNestedCatalogId") != std::string::npos);
	REQUIRE(hub.find("weaponArchiveHasEntry") != std::string::npos);
	REQUIRE(hub.find("weaponTsvFirstField") != std::string::npos);
	REQUIRE(hub.find("weaponTsvFindFieldForRow") != std::string::npos);
	REQUIRE(hub.find("weaponJsonFindFirstStringField") != std::string::npos);
	REQUIRE(hub.find("models/held_hi.pdmesh") != std::string::npos);
	REQUIRE(hub.find("animations_manifest.tsv") != std::string::npos);
	REQUIRE(hub.find("audio_manifest.tsv") != std::string::npos);
	REQUIRE(hub.find("\"projectile_ref\"") != std::string::npos);
	REQUIRE(hub.find("\"entity_ref\"") != std::string::npos);
	REQUIRE(hub.find("\"payload_ref\"") != std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateWindow") == std::string::npos);
	REQUIRE(hub.find("ImGui::Begin(\"Create Weapon Mod\"") == std::string::npos);
	REQUIRE(hub.find("##modhub_weapon_creator") != std::string::npos);
	REQUIRE(hub.find("weaponCreatorView") != std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateEditor") != std::string::npos);
	REQUIRE(hub.find("s_WeaponTemplateMenuOpen") != std::string::npos);
	REQUIRE(hub.find("weaponToolSaveCustom") != std::string::npos);
	REQUIRE(hub.find("Save Weapon Mod") != std::string::npos);
	REQUIRE(hub.find("Save Weapon Mod##weapon_save_options") != std::string::npos);
	REQUIRE(hub.find("Creator") != std::string::npos);
	REQUIRE(hub.find("InputText(\"Display Name\", s_WeaponSaveDisplayName") !=
	        std::string::npos);
	REQUIRE(hub.find("InputText(\"Mod Name\", s_WeaponSave") == std::string::npos);
	REQUIRE(hub.find("weaponCatalogSlugForDisplayName") != std::string::npos);
	REQUIRE(hub.find("weaponCatalogIdForDisplayName") != std::string::npos);
	REQUIRE(hub.find("Catalog Name: mod:%s") != std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.BEGIN") != std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.PAYLOAD_FAIL slot=model_ref") !=
	        std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.PAYLOAD_FAIL slot=animation_ref") !=
	        std::string::npos);
	REQUIRE(hub.find("weaponSaveFail(\"GRAPH_FAIL\"") != std::string::npos);
	REQUIRE(hub.find("weaponSaveFail(\"MODINVALID\"") != std::string::npos);
	REQUIRE(hub.find("weaponSaveFail(\"MODDISCOVER_FAIL\"") != std::string::npos);
	REQUIRE(hub.find("WEAPONMOD.SAVE.OK") != std::string::npos);
	REQUIRE(hub.find("Create + Enable") != std::string::npos);
	REQUIRE(hub.find("identityGetActiveProfile") != std::string::npos);
	REQUIRE(hub.find("modmgrGetModValid") != std::string::npos);
	REQUIRE(hub.find("modmgrApplyChanges();") != std::string::npos);
	REQUIRE(hub.find("Saved, enabled, and catalog updated") != std::string::npos);
	REQUIRE(hub.find("s_WeaponEditWeaponId") == std::string::npos);
	REQUIRE(hub.find("InputInt(\"Weapon ID\"") == std::string::npos);
	REQUIRE(hub.find("\"weapon_id = %d") == std::string::npos);
	REQUIRE(hub.find("Runtime Index:") == std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Details\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Assets\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Primary Graph\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Secondary Graph\")") != std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Payloads\")") != std::string::npos);
	REQUIRE(hub.find("weaponRenderGraphBuilder(\"primary\", \"Primary Graph\"") !=
	        std::string::npos);
	REQUIRE(hub.find("weaponRenderGraphBuilder(\"secondary\", \"Secondary Graph\"") !=
	        std::string::npos);
	REQUIRE(hub.find("BeginTabItem(\"Template\")") == std::string::npos);
	REQUIRE(hub.find("weaponRenderTemplateEditor(scale);\n        ImGui::EndChild();") ==
	        std::string::npos);
	REQUIRE(hub.find("Show non-weapon meshes") != std::string::npos);
	REQUIRE(hub.find("Select Weapon Mesh") != std::string::npos);
	REQUIRE(hub.find("pdguiPopupDarkenBehind") != std::string::npos);
	REQUIRE(hub.find("ImGuiCol_PopupBg") != std::string::npos);
	REQUIRE(hub.find("weaponMeshEntryIsWeaponMesh") != std::string::npos);
	REQUIRE(hub.find("pdguiModelPreviewDrawEx") != std::string::npos);
	REQUIRE(hub.find("PDGUI_MP_WEAPON") != std::string::npos);
	REQUIRE(hub.find("ASSET_MODEL") != std::string::npos);
	REQUIRE(hub.find("modArchiveBegin(archivePath)") != std::string::npos);
	REQUIRE(hub.find("weaponCopyTemplatePayloads") != std::string::npos);
	REQUIRE(hub.find("modelTemplateEntry") != std::string::npos);
	REQUIRE(hub.find("animTemplateEntry") != std::string::npos);
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
	REQUIRE(hub.find("snprintf(newModId, sizeof(newModId), \"mod.%s\"") !=
	        std::string::npos);
	REQUIRE(hub.find("\"user.%s.weapon\"") == std::string::npos);
	REQUIRE(hub.find("\"user:%s\"") == std::string::npos);
	REQUIRE(hub.find("mods/Weapons/%s") != std::string::npos);
	REQUIRE(scanner.find("catalog_id = mod:weapon_catalog_name") !=
	        std::string::npos);
	REQUIRE(scanner.find("weapon_id = -1") == std::string::npos);
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
	REQUIRE(hub.find("\"event.trigger_pressed\"") != std::string::npos);
	REQUIRE(hub.find("\"event.trigger_held\"") != std::string::npos);
	REQUIRE(hub.find("\"event.trigger_released\"") != std::string::npos);
	REQUIRE(hub.find("weaponGraphBuilderAddTriggeredModuleInScope") !=
	        std::string::npos);
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
	REQUIRE(editor.find("renderPinSocket") != std::string::npos);
	REQUIRE(editor.find("InvisibleButton") != std::string::npos);
	REQUIRE(editor.find("##exec_out_pin") != std::string::npos);
	REQUIRE(editor.find("kWeaponGraphNodeWidth") != std::string::npos);
	REQUIRE(editor.find("BeginTable(\"##exec_pin_row\"") != std::string::npos);
	REQUIRE(editor.find("TableSetupColumn(\"##out_pin\"") != std::string::npos);
	REQUIRE(editor.find("PinPivotAlignment(ImVec2(0.0f, 0.5f))") !=
	        std::string::npos);
	REQUIRE(editor.find("PinPivotAlignment(ImVec2(1.0f, 0.5f))") !=
	        std::string::npos);
	REQUIRE(editor.find("linkColorForPinKind(2)") != std::string::npos);
	REQUIRE(editor.find("Connect Selected") != std::string::npos);
	REQUIRE(editor.find("Connect To") != std::string::npos);
	REQUIRE(editor.find("Visible links: %d/%d") != std::string::npos);
	REQUIRE(editor.find("Current Links") != std::string::npos);
	REQUIRE(editor.find("Remove Link") != std::string::npos);
	REQUIRE(editor.find("BeginDelete") != std::string::npos);
	REQUIRE(editor.find("Add Node") != std::string::npos);
	REQUIRE(editor.find("Delete Node") != std::string::npos);
	REQUIRE(editor.find("Duplicate Node") != std::string::npos);
	REQUIRE(editor.find("Set Primary") != std::string::npos);
	REQUIRE(editor.find("Set Secondary") != std::string::npos);
	REQUIRE(editor.find("Break Pin Links") != std::string::npos);
	REQUIRE(editor.find("Alt-click pin") != std::string::npos);
	REQUIRE(editor.find("Node Parameters") != std::string::npos);
	REQUIRE(editor.find("Advanced JSON") != std::string::npos);
	REQUIRE(editor.find("InputFloat") != std::string::npos);
	REQUIRE(editor.find("Checkbox") != std::string::npos);
	REQUIRE(editor.find("Node parameter updated") != std::string::npos);
	REQUIRE(editor.find("Node Context Refs") != std::string::npos);
	REQUIRE(editor.find("Choose a compatible node from the add-node menu") !=
	        std::string::npos);
	REQUIRE(editor.find("pdguiWeaponGraphModelLoadJson") != std::string::npos);
	REQUIRE(editor.find("scope_filter") != std::string::npos);
	REQUIRE(editor.find("nodeVisibleForScope") != std::string::npos);
	REQUIRE(editor.find("seedMissingNodeLayout") != std::string::npos);
	REQUIRE(editor.find("Links must stay inside the visible graph tab") !=
	        std::string::npos);
	REQUIRE(header.find("scope_filter") != std::string::npos);
	REQUIRE(header.find("scope_label") != std::string::npos);
	REQUIRE(header.find("PdWeaponGraphEditModel") != std::string::npos);
	REQUIRE(header.find("PD_WEAPON_GRAPH_EDITOR_ACTION_SET_PRIMARY") !=
	        std::string::npos);
}

TEST_CASE("shared gameplay graph editor foundation owns typed pins and adapter boundaries",
          "[modding][pdxxx][graph_editor][c3835]") {
	const std::string sharedHeader =
		readFile("port/include/pdgui_gameplay_graph_editor.h");
	const std::string sharedImpl =
		readFile("port/fast3d/pdgui_gameplay_graph_editor.cpp");
	const std::string weaponHeader =
		readFile("port/include/pdgui_weapon_graph_node_editor.h");
	const std::string weaponEditor =
		readFile("port/fast3d/pdgui_weapon_graph_node_editor.cpp");
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!sharedHeader.empty());
	REQUIRE(!sharedImpl.empty());
	REQUIRE(!weaponHeader.empty());
	REQUIRE(!weaponEditor.empty());
	REQUIRE(!hub.empty());

	REQUIRE(sharedHeader.find("PdGameplayGraphPinType") != std::string::npos);
	REQUIRE(sharedHeader.find("PdGameplayGraphEditorAdapter") != std::string::npos);
	REQUIRE(sharedHeader.find("pdguiGameplayGraphPinsCompatible") != std::string::npos);
	REQUIRE(sharedImpl.find("pdguiGameplayGraphLinkColor") != std::string::npos);
	REQUIRE(sharedImpl.find("PDGAMEPLAY_GRAPH_PIN_CONTEXT_REF") != std::string::npos);
	REQUIRE(weaponHeader.find("const PdGameplayGraphEditorAdapter *adapter") !=
	        std::string::npos);
	REQUIRE(weaponEditor.find("pdguiGameplayGraphCategoryColor(kind)") !=
	        std::string::npos);
	REQUIRE(weaponEditor.find("pinTypeForWeaponPinKind") != std::string::npos);
	REQUIRE(weaponEditor.find("pdguiGameplayGraphPinsCompatible") !=
	        std::string::npos);
	REQUIRE(hub.find("s_WeaponGraphEditorAdapter") != std::string::npos);
	REQUIRE(hub.find("\"behavior/primary.graph.json\"") != std::string::npos);
	REQUIRE(hub.find("\"behavior/secondary.graph.json\"") != std::string::npos);

	REQUIRE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_OUTPUT, PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_INPUT));
	REQUIRE_FALSE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_INPUT, PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_INPUT));
	REQUIRE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_CATALOG_ID,
		PDGAMEPLAY_GRAPH_PIN_OUTPUT, PDGAMEPLAY_GRAPH_PIN_STRING,
		PDGAMEPLAY_GRAPH_PIN_INPUT));
	REQUIRE_FALSE(pdguiGameplayGraphPinsCompatible(PDGAMEPLAY_GRAPH_PIN_NUMBER,
		PDGAMEPLAY_GRAPH_PIN_OUTPUT, PDGAMEPLAY_GRAPH_PIN_ENTITY,
		PDGAMEPLAY_GRAPH_PIN_INPUT));

	ImVec4 execPin = pdguiGameplayGraphPinColor(PDGAMEPLAY_GRAPH_PIN_EXEC);
	ImVec4 execWire = pdguiGameplayGraphLinkColor(PDGAMEPLAY_GRAPH_PIN_EXEC,
		PDGAMEPLAY_GRAPH_PIN_EXEC);
	REQUIRE(execWire.x == execPin.x);
	REQUIRE(execWire.y == execPin.y);
	REQUIRE(execWire.z == execPin.z);
}

TEST_CASE("asset utility contracts cover clean typed archive families",
          "[modding][pdxxx][utilities][c3834]") {
	const std::string header = readFile("port/include/asset_mod_utility_contract.h");
	const std::string impl = readFile("port/src/asset_mod_utility_contract.c");
	const std::string hub = readFile("port/fast3d/pdgui_menu_moddinghub.cpp");
	REQUIRE(!header.empty());
	REQUIRE(!impl.empty());
	REQUIRE(!hub.empty());

	REQUIRE(header.find("ASSET_MOD_UTIL_CREATE") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_IMPORT") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_CLONE") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_EDIT") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_VALIDATE") != std::string::npos);
	REQUIRE(header.find("ASSET_MOD_UTIL_PACKAGE") != std::string::npos);
	REQUIRE(impl.find(".pdweapon") != std::string::npos);
	REQUIRE(impl.find(".pdscenario") != std::string::npos);
	REQUIRE(impl.find(".pdbotprofile") != std::string::npos);
	REQUIRE(impl.find(".pdtool") != std::string::npos);
	REQUIRE(hub.find("Archive Utility Contracts") != std::string::npos);
	REQUIRE(hub.find("assetModUtilityContractCount()") != std::string::npos);

	const asset_mod_utility_contract_t *weapon =
		assetModUtilityContractForExtension(".pdweapon");
	REQUIRE(weapon != nullptr);
	REQUIRE(std::string(weapon->descriptor) == "weapon.ini");
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_CREATE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_IMPORT));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_CLONE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_EDIT));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_VALIDATE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_PACKAGE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_HOT_ENABLE));
	REQUIRE(assetModUtilitySupports(weapon, ASSET_MOD_UTIL_EMBED_DEPS));

	const asset_mod_utility_contract_t *bot =
		assetModUtilityContractForExtension(".pdbotprofile");
	REQUIRE(bot != nullptr);
	REQUIRE(assetModUtilitySupports(bot, ASSET_MOD_UTIL_TEMPLATE));
	REQUIRE(std::string(bot->cli_noun) == "bot-profile");

	const asset_mod_utility_contract_t *tool =
		assetModUtilityContractForExtension(".pdtool");
	REQUIRE(tool != nullptr);
	REQUIRE(assetModUtilitySupports(tool, ASSET_MOD_UTIL_SECURE_TOOL));
	REQUIRE_FALSE(assetModUtilitySupports(tool, ASSET_MOD_UTIL_CREATE));

	for (size_t i = 0; i < assetModUtilityContractCount(); i++) {
		const asset_mod_utility_contract_t *c = assetModUtilityContractAt(i);
		REQUIRE(c != nullptr);
		REQUIRE(c->extension[0] == '.');
		REQUIRE(std::string(c->descriptor).find(".ini") != std::string::npos);
		REQUIRE(assetModUtilitySupports(c, ASSET_MOD_UTIL_VALIDATE));
		REQUIRE(assetModUtilitySupports(c, ASSET_MOD_UTIL_PACKAGE));
	}
}

TEST_CASE("weapon graph parity modules wrap OG behavior families before retirement",
          "[modding][pdxxx][weapon_graph][parity][c3840]") {
	const std::string runtimeHeader = readFile("port/include/weapon_graph_runtime.h");
	const std::string runtimeImpl = readFile("port/src/weapon_graph_runtime.c");
	REQUIRE(!runtimeHeader.empty());
	REQUIRE(!runtimeImpl.empty());

	REQUIRE(runtimeHeader.find("weapon_graph_parity_module") != std::string::npos);
	REQUIRE(runtimeHeader.find("parity_module") != std::string::npos);
	REQUIRE(runtimeImpl.find("s_parity_modules") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.projectile.motion") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.entity.autogun") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.entity.proxy_trigger") != std::string::npos);
	REQUIRE(runtimeImpl.find("og.fire.hitscan") != std::string::npos);

	const weapon_graph_parity_module_t *hitscan =
		weaponGraphParityModuleForOpcode(WEAPON_GRAPH_OP_FIRE_HITSCAN);
	REQUIRE(hitscan != nullptr);
	REQUIRE(std::string(hitscan->module_name) == "og.fire.hitscan");
	REQUIRE(std::string(hitscan->behavior_family) == "held_hitscan");
	REQUIRE(std::string(hitscan->legacy_backend).find("bondgun.c") !=
	        std::string::npos);

	const weapon_graph_parity_module_t *motion =
		weaponGraphParityModuleForOpcode(WEAPON_GRAPH_OP_PROJECTILE_MOTION);
	REQUIRE(motion != nullptr);
	REQUIRE(std::string(motion->module_name) == "og.projectile.motion");
	REQUIRE(std::string(motion->legacy_backend).find("propobj.c") !=
	        std::string::npos);

	const weapon_graph_parity_module_t *autogun =
		weaponGraphParityModuleForOpcode(WEAPON_GRAPH_OP_ENTITY_AUTOGUN);
	REQUIRE(autogun != nullptr);
	REQUIRE(std::string(autogun->module_name) == "og.entity.autogun");

	REQUIRE(weaponGraphParityModuleForOpcode(
		WEAPON_GRAPH_OP_EVENT_TRIGGER_PRESSED) == nullptr);
	REQUIRE(std::string(weaponGraphParityModuleNameForOpcode(
		WEAPON_GRAPH_OP_EVENT_TRIGGER_PRESSED)).empty());

	weaponGraphRuntimeClearAll();
	char err[256] = "";
	const char *weaponJson =
		"{ \"schema\": \"pd.weapon_graph.v1\","
		"  \"asset_id\": \"user:weapon_parity\","
		"  \"graph_id\": \"primary\","
		"  \"nodes\": ["
		"    { \"id\": \"fire\", \"kind\": \"fire.hitscan\","
		"      \"params\": { \"mode\": \"primary\", \"damage\": 8.0 } }"
		"  ],"
		"  \"edges\": [],"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"fire\" } ] }";
	weapon_graph_ir_t ir;
	REQUIRE(weaponGraphCompileJson(ASSET_WEAPON, weaponJson,
		(u32)std::strlen(weaponJson), &ir, err, sizeof(err)) == 0);
	REQUIRE(weaponGraphRuntimeRegisterHeldIr(7, &ir, err, sizeof(err)) == 0);
	const weapon_graph_held_function_t *held =
		weaponGraphRuntimeGetHeldFunction(7, 0);
	REQUIRE(held != nullptr);
	REQUIRE(std::string(held->parity_module) == "og.fire.hitscan");

	const char *projectileJson =
		"{ \"schema\": \"pd.projectile_graph.v1\","
		"  \"asset_id\": \"user:projectile_parity\","
		"  \"graph_id\": \"projectile\","
		"  \"nodes\": ["
		"    { \"id\": \"motion\", \"kind\": \"projectile.motion\","
		"      \"params\": { \"motion_kind\": \"powered\", \"speed\": 20.0 } }"
		"  ],"
		"  \"edges\": [],"
		"  \"exports\": [] }";
	REQUIRE(weaponGraphRuntimeRegisterBehaviorGraphJson(ASSET_PROJECTILE,
		"user:projectile_parity", projectileJson,
		(u32)std::strlen(projectileJson), err, sizeof(err)) == 0);
	const weapon_graph_projectile_runtime_t *projectile =
		weaponGraphRuntimeGetProjectile("user:projectile_parity");
	REQUIRE(projectile != nullptr);
	REQUIRE(std::string(projectile->parity_module) == "og.projectile.motion");
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
	REQUIRE(arena.find("assetArchiveWriterAddDescriptor(&asset_writer, \"arena.ini\"") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddManifestJson(&asset_writer") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterFinishMetadata(&asset_writer)") != std::string::npos);
	REQUIRE(arena.find("s_addScenarioArchiveDependency") != std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicDisk(writer, dst_name") !=
	        std::string::npos);
	REQUIRE(arena.find("\"dependencies/assets/scenarios/\"") != std::string::npos);
	REQUIRE(arena.find("scenario_archive = dependencies/assets/scenarios/%s.pdscenario") !=
	        std::string::npos);
	REQUIRE(arena.find("s_copyArchiveEntriesWithPrefix") == std::string::npos);
	REQUIRE(arena.find("\"scenario/scenario.ini\"") == std::string::npos);
	REQUIRE(arena.find("\"scenario/rooms.obj\"") == std::string::npos);
	REQUIRE(arena.find("\"scenario/visual/scene.obj\"") == std::string::npos);
	REQUIRE(arena.find("scenario_root = scenario") == std::string::npos);
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
	const std::string conformance = readFile("tools/asset_archive_conformance.py");
	REQUIRE(!arena.empty());
	REQUIRE(!conformance.empty());

	REQUIRE(arena.find("s_buildTilesExports") != std::string::npos);
	REQUIRE(arena.find("s_buildPadsTsv") != std::string::npos);
	REQUIRE(arena.find("s_buildWordsTsv") == std::string::npos);
	REQUIRE(arena.find("#include \"lib/rzip.h\"") != std::string::npos);
	REQUIRE(arena.find("rzipIs1173") != std::string::npos);
	REQUIRE(arena.find("rzipInflate") != std::string::npos);
	REQUIRE(arena.find("preprocessTilesFile") != std::string::npos);
	REQUIRE(arena.find("preprocessPadsFile") != std::string::npos);
	REQUIRE(arena.find("Stage preprocessors share process-global scratch state") !=
	        std::string::npos);
	REQUIRE(arena.find("s_buildBgVisualExports") != std::string::npos);
	REQUIRE(arena.find("preprocessBgSection1") != std::string::npos);
	REQUIRE(arena.find("preprocessBgRoom") != std::string::npos);
	REQUIRE(arena.find("G_NOOP") != std::string::npos);
	REQUIRE(arena.find("G_TRI1") != std::string::npos);
	REQUIRE(arena.find("G_TRI4") != std::string::npos);
	REQUIRE(arena.find("texInflateZlib") != std::string::npos);
	REQUIRE(arena.find("texInflateNonZlib") != std::string::npos);
	REQUIRE(arena.find("scene.glb") != std::string::npos);
	REQUIRE(arena.find("stbi_write_png_to_mem") != std::string::npos);
	REQUIRE(arena.find("bg_visual_scene_glb_v7_dccuv_rsptexscale_texshift_samplerwrap_untextured_uvbound") != std::string::npos);
	REQUIRE(arena.find("s_existingArchiveEntryContains(relpath, \"scene.glb\",") !=
	        std::string::npos);
	REQUIRE(arena.find("PDSCENARIO_BG_VISUAL_EXPORT_VERSION)") !=
	        std::string::npos);
	REQUIRE(arena.find("\\\"texCoord\\\":0") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialUvScaleForGlb") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbAuthorUv") != std::string::npos);
	REQUIRE(arena.find("s_bgMaterialGlbUv") != std::string::npos);
	REQUIRE(arena.find("\\\"TEXCOORD_1\\\":%u") != std::string::npos);
	REQUIRE(arena.find("\\\"baseColorTexture\\\":{\\\"index\\\":%d,\\\"texCoord\\\":0}") !=
	        std::string::npos);
	REQUIRE(arena.find("s_bgGltfWrapMode") != std::string::npos);
	REQUIRE(arena.find("op == (u8)G_TEXTURE") != std::string::npos);
	REQUIRE(arena.find("m->shifts <= 10") != std::string::npos);
	REQUIRE(arena.find("m->shiftt <= 10") != std::string::npos);
	REQUIRE(arena.find("\\\"sampler\\\":%u,\\\"source\\\":%d") != std::string::npos);
	const std::string filebg = readFile("port/src/preprocess/filebg.c");
	const auto empty_room_guard =
		filebg.find("!dst_header->vertices && !dst_header->colours");
	const auto ptr_vertices_warn = filebg.find("ptr_vertices yields");
	REQUIRE(empty_room_guard != std::string::npos);
	REQUIRE(filebg.find("!dst_header->opablocks && !dst_header->xlublocks",
			empty_room_guard) != std::string::npos);
	REQUIRE(filebg.find("dst_header->numvertices == 0 && dst_header->numcolours == 0",
			empty_room_guard) != std::string::npos);
	REQUIRE(filebg.find("sizeof(struct roomgfxdata) - sizeof(struct roomblock)",
			empty_room_guard) != std::string::npos);
	REQUIRE(ptr_vertices_warn != std::string::npos);
	REQUIRE(empty_room_guard < ptr_vertices_warn);
	REQUIRE(conformance.find("validate_scene_glb_texture_contract") !=
	        std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_0 range") != std::string::npos);
	REQUIRE(conformance.find("DCC-authoring UV range") != std::string::npos);
	REQUIRE(conformance.find("TEXCOORD_1 runtime UVs") != std::string::npos);
	REQUIRE(conformance.find("visible textures to TEXCOORD_0") !=
	        std::string::npos);
	REQUIRE(arena.find("mat_%03u_tex_%04x") != std::string::npos);
	REQUIRE(arena.find("texture_inventory_%04x") != std::string::npos);
	REQUIRE(arena.find("strncmp(mtl_texture_path, \"visual/\", 7)") !=
	        std::string::npos);
	REQUIRE(arena.find("scene_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(arena.find("runtime_source_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(arena.find("collision_source = collision.obj") !=
	        std::string::npos);
	REQUIRE(arena.find("collision_fallback = override") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"collision.obj\"") !=
	        std::string::npos);
	REQUIRE(arena.find("blender_scene_file = scene.glb") !=
	        std::string::npos);
	REQUIRE(arena.find("pads.tsv") != std::string::npos);
	REQUIRE(arena.find("spawns.tsv") != std::string::npos);
	REQUIRE(arena.find("volumes.tsv") != std::string::npos);
	REQUIRE(arena.find("objects.tsv") != std::string::npos);
	REQUIRE(arena.find("ai/ailists.tsv") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.lists.source") != std::string::npos);
	REQUIRE(arena.find("scenario.pads.source") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_return_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_shot_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.return_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.face_entity") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.apply_gset_damage") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_damage_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.consider_grenade_throw") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.drop_item") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_run_from_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_jog_to_target_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_walk_to_target_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_run_to_target_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_go_to_cover_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_jog_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_walk_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_run_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_punch_dodge_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_shooting_at_me_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_dark_room_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_player_dead_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.jog_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.go_to_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.walk_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.run_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_path") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.start_patrol") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_start_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.activate_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.deactivate_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.add_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_add_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.subtract_morale") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.add_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_add_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.subtract_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.increase_squadron_alertness") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_hear_distance") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_view_distance") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_grenade_probability") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_num") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_max_damage") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.add_health") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_shield") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_reaction_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_recovery_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_accuracy") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_dodge_rating") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_unarmed_dodge_rating") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unset_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_has_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_unset_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_chr_has_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_stage_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unset_stage_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_stage_flag_eq") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.open_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.close_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_door_state") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_object_is_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.lock_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unlock_door") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_door_locked") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_lift_stationary") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.lift_go_to_stop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_lift_at_stop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.activate_lift") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_using_lift") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.configure_rain") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.configure_snow") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.switch_to_alt_sky") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_wind_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_lights") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_room_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_cutscene_chrs") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.configure_environment") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_distance_to_target2_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_distance_to_target2_greater_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.play_sound_from_prop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.play_temporary_primary_track") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.play_x_track") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.stop_ambient_track") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_draw_weapon") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_draw_weapon_in_cutscene") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_player_force_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_invincible") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_player_is_invincible") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_chr_has_no_gun") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_delete_weapon") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_trigger_shot_list") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.end_level") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.end_cutscene") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.warp_jo_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.warp_jo_to_tag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.revoke_control") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.grant_control") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.player_fade_in") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.players_fade_out") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_colour_fade_complete") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.prepare_warp_orbit") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.begin_warp_latch") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_warp_latch_complete") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_camera_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_in_cutscene") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_cutscene_button_pressed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.reorient_for_cutscene_stop") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.spawn_chr_at_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.spawn_chr_at_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_equip_weapon") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_equip_hat") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_obj_image") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.object_do_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_door_open") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.duplicate_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.enable_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.disable_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.enable_obj") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.disable_obj") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_move_to_pad") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_team") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.damage_chr_by_amount") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.do_preset_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_player_chr_portal_distance_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_chr_reposition_valid") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.do_gun_command") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_distance_to_gun_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.recover_gun") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_copy_properties") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.player_auto_walk") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_player_auto_walk_finished") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_obj_in_room") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_kill") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.remove_weapon_from_inventory") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_music_event_queue_is_empty") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_coop_mode") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_chr_same_floor_distance_to_pad_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.remove_references_to_chr") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_toggle_model_part") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.obj_set_model_part_visible") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_obj_health_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_obj_health") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_special_death_animation") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_room_to_search") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_savefile_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.unset_savefile_flag") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_savefile_flag_set") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.if_savefile_flag_unset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_hudmsg") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_hudmsg_middle") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.show_hudmsg_top_middle") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.hovercar_begin_path") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_vehicle_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_rotor_speed") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_explosions") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_tinted_glass_enabled") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.hovercopter_fire_rocket") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_adjust_motion_blur") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.punch_or_kick") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_target_to_eyespy_if_in_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.mini_skedar_try_pounce") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_object_distance_to_pad_less_than") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.avoid") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.title_init_mode") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_exit_title") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_emit_sparks") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_dr_caroll_images") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.say_quip") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.say_ci_staff_quip") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.shuffle_ruins_pillars") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.shuffle_pelagic_switches") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_action") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_team_orders") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.retreat") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.find_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.find_cover_within_dist") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.find_cover_outside_dist") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.go_to_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.check_cover_out_of_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.orbit_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_preset_to_unalerted_teammate") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_squadron") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.face_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.danger_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.release_cover") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.rebuild_teams") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.rebuild_squadrons") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_listening") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.try_attack_amount") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_chr_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_set_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.chr_copy_pad_preset") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_can_hear_alarm") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_patrolling") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_alarm_active") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_gas_active") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_hears_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_saw_injury") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_saw_death") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_los_to_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_los_to_attack_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_target_nearly_in_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_nearly_in_targets_sight") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.action.set_pad_preset_to_pad_on_route_to_target") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_saw_target_recently") != std::string::npos);
	REQUIRE(arena.find("scenario.ai.condition.if_heard_target_recently") != std::string::npos);
	for (const char *kind : {
		     "scenario.ai.condition.if_los_to_chr",
		     "scenario.ai.condition.if_never_been_on_screen",
		     "scenario.ai.condition.if_on_screen",
		     "scenario.ai.condition.if_chr_in_on_screen_room",
		     "scenario.ai.condition.if_room_is_on_screen",
		     "scenario.ai.condition.if_target_aiming_at_me",
		     "scenario.ai.condition.if_near_miss",
		     "scenario.ai.condition.if_sees_suspicious_item",
		     "scenario.ai.condition.if_target_in_fov_left",
		     "scenario.ai.condition.if_check_fov_with_target",
		     "scenario.ai.condition.if_target_out_of_fov_left",
		     "scenario.ai.condition.if_target_in_fov",
		     "scenario.ai.condition.if_target_out_of_fov",
		     "scenario.ai.condition.if_distance_to_target_less_than",
		     "scenario.ai.condition.if_distance_to_target_greater_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_less_than",
		     "scenario.ai.condition.if_chr_distance_to_pad_greater_than",
		     "scenario.ai.condition.if_distance_to_chr_less_than",
		     "scenario.ai.condition.if_distance_to_chr_greater_than",
		     "scenario.ai.condition.if_any_chr_near_self",
		     "scenario.ai.condition.if_distance_from_target_to_pad_less_than",
		     "scenario.ai.condition.if_distance_from_target_to_pad_greater_than",
		     "scenario.ai.condition.if_chr_in_room",
		     "scenario.ai.condition.if_target_in_room",
		     "scenario.ai.condition.if_chr_has_object",
		     "scenario.ai.condition.if_weapon_thrown",
		     "scenario.ai.condition.if_weapon_thrown_on_object",
		     "scenario.ai.condition.if_chr_has_weapon_equipped",
		     "scenario.ai.condition.if_gun_unclaimed",
		     "scenario.ai.condition.if_object_healthy",
		     "scenario.ai.condition.if_chr_activated_object",
		     "scenario.ai.action.obj_interact",
		     "scenario.ai.action.destroy_object",
		     "scenario.ai.action.drop_object_from_chr",
		     "scenario.ai.action.chr_drop_items",
		     "scenario.ai.action.chr_drop_weapon",
		     "scenario.ai.action.give_object_to_chr",
		     "scenario.ai.action.object_move_to_pad",
		     "scenario.ai.condition.if_chr_not_talking",
		     "scenario.ai.condition.if_orders",
		     "scenario.ai.condition.if_has_orders",
		     "scenario.ai.condition.if_chr_in_squadron_doing_action",
		     "scenario.ai.condition.if_chr_listening",
		     "scenario.ai.condition.if_not_listening",
		     "scenario.ai.condition.if_chr_injured_target",
		     "scenario.ai.condition.if_action",
		     "scenario.ai.condition.if_chr_ammo_quantity_less_than",
		     "scenario.ai.condition.if_chr_target",
		     "scenario.ai.condition.if_compare_chr_presets_team",
		     "scenario.ai.condition.if_human",
		     "scenario.ai.condition.if_skedar",
		     "scenario.ai.condition.if_prop_preset_blocking_sight_to_target",
		     "scenario.ai.action.remove_object_at_prop_preset",
		     "scenario.ai.condition.if_prop_preset_height_less_than",
		     "scenario.ai.action.set_target",
		     "scenario.ai.condition.if_presets_target_is_not_my_target",
		     "scenario.ai.action.set_chr_preset_to_chr_near_self",
		     "scenario.ai.action.set_chr_preset_to_chr_near_pad",
		     "scenario.ai.condition.if_dangerous_object_nearby",
		     "scenario.ai.condition.if_heli_weapons_armed",
		     "scenario.ai.condition.if_hoverbot_next_step",
		     "scenario.ai.action.shuffle_investigation_terminals",
		     "scenario.ai.action.set_pad_preset_to_investigation_terminal",
		     "scenario.ai.action.heli_arm_weapons",
		     "scenario.ai.action.heli_unarm_weapons",
		     "scenario.ai.condition.if_safety2_less_than",
		     "scenario.ai.condition.if_player_using_cmp_or_ar34",
		     "scenario.ai.condition.detect_enemy_on_same_floor",
		     "scenario.ai.condition.detect_enemy",
		     "scenario.ai.condition.if_safety_less_than",
		     "scenario.ai.condition.if_target_moving_slowly",
		     "scenario.ai.condition.if_target_moving_closer",
		     "scenario.ai.condition.if_target_moving_away",
		     "scenario.ai.condition.if_squadron_is_dead",
		     "scenario.ai.condition.if_true",
		     "scenario.ai.condition.if_num_chrs_in_squadron_greater_than",
		     "scenario.ai.condition.if_natural_anim",
		     "scenario.ai.condition.if_y",
		     "scenario.ai.condition.if_sound_timer",
		     "scenario.ai.condition.if_target_y_difference_less_than",
		     "scenario.ai.condition.if_waypoint_within_quadrant",
		     "scenario.ai.action.set_pad_preset_to_target_quadrant",
	     }) {
		REQUIRE(arena.find(kind) != std::string::npos);
	}
	REQUIRE(arena.find("ai_lists_file = ai/ailists.tsv") != std::string::npos);
	REQUIRE(arena.find("navigation/paths.tsv") != std::string::npos);
	REQUIRE(arena.find("scenario.navigation.paths.source") != std::string::npos);
	REQUIRE(arena.find("paths_file = navigation/paths.tsv") != std::string::npos);
	REQUIRE(arena.find("objectives.tsv") != std::string::npos);
	REQUIRE(arena.find("navigation.ini") != std::string::npos);
	REQUIRE(arena.find("level.graph.json") != std::string::npos);
	REQUIRE(arena.find("_meta/generated-collision.json") != std::string::npos);
	REQUIRE(arena.find("_meta/generated-navmesh.json") != std::string::npos);
	REQUIRE(arena.find("setup.tsv") == std::string::npos);
	REQUIRE(arena.find("mpsetup.tsv") == std::string::npos);
	REQUIRE(arena.find("visual_segments.tsv") == std::string::npos);
	REQUIRE(arena.find("geometry_file = rooms.obj") == std::string::npos);
	REQUIRE(arena.find("geometry_format = OBJ") == std::string::npos);
	REQUIRE(arena.find("visual_scene_file = visual/scene.obj") ==
	        std::string::npos);
	REQUIRE(arena.find("visual_material_file = visual/scene.mtl") ==
	        std::string::npos);
	REQUIRE(arena.find("texture_manifest_file = visual/materials.tsv") ==
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(relpath, \"scene.glb\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(relpath, \"level.graph.json\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(relpath, \"rooms.obj\")") !=
	        std::string::npos);
	REQUIRE(arena.find("s_existingArchiveHasEntry(relpath, \"tiles.tsv\")") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"scene.glb\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"level.graph.json\"") !=
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"rooms.obj\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterAddPublicMem(&asset_writer, \"visual/scene.obj\"") ==
	        std::string::npos);
	REQUIRE(arena.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
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
	REQUIRE(head.find("assetArchiveWriterAddDescriptor(&asset_writer, \"head.ini\"") != std::string::npos);
	REQUIRE(head.find("assetArchiveWriterAddManifestJson(&asset_writer") != std::string::npos);
	REQUIRE(head.find("s_addRequiredArchiveFile(&asset_writer, \"mesh.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(head.find("assetArchiveWriterFinishMetadata(&asset_writer)") != std::string::npos);
	REQUIRE(head.find("s_existingArchiveHasEntry(relpath, \"mesh.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(head.find("PDHEAD_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(head.find("s_existingArchiveEntryContains(relpath, \"head.ini\", \"headnum\")") !=
	        std::string::npos);
	REQUIRE(head.find("headnum = %d") == std::string::npos);
	REQUIRE(head.find("mesh = %s") == std::string::npos);
	REQUIRE(head.find("mesh_catalog_id = %s") != std::string::npos);
	REQUIRE(head.find("mesh_archive = %s") != std::string::npos);
	REQUIRE(head.find("\\\"mesh_archive\\\": \\\"mesh.pdmesh\\\"") !=
	        std::string::npos);
	REQUIRE(head.find("fsFileOpenWrite(relpath)") == std::string::npos);
	REQUIRE(head.find("emits one .pdhead JSON file") == std::string::npos);

	REQUIRE(body.find("s_existingZipArchive") != std::string::npos);
	REQUIRE(body.find("modArchiveBegin(full)") != std::string::npos);
	REQUIRE(body.find("assetArchiveWriterAddDescriptor(&asset_writer, \"body.ini\"") != std::string::npos);
	REQUIRE(body.find("assetArchiveWriterAddManifestJson(&asset_writer") != std::string::npos);
	REQUIRE(body.find("s_addRequiredArchiveFile(&asset_writer, \"mesh.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(body.find("s_addRequiredArchiveFile(&asset_writer, \"hand.pdmesh\"") !=
	        std::string::npos);
	REQUIRE(body.find("assetArchiveWriterFinishMetadata(&asset_writer)") != std::string::npos);
	REQUIRE(body.find("s_existingArchiveHasEntry(relpath, \"mesh.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(body.find("s_existingArchiveHasEntry(relpath, \"hand.pdmesh\")") !=
	        std::string::npos);
	REQUIRE(body.find("PDBODY_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(body.find("s_existingArchiveEntryContains(relpath, \"body.ini\", \"bodynum\")") !=
	        std::string::npos);
	REQUIRE(body.find("bodynum = %d") == std::string::npos);
	REQUIRE(body.find("mesh = %s") == std::string::npos);
	REQUIRE(body.find("hand = %s") == std::string::npos);
	REQUIRE(body.find("mesh_catalog_id = %s") != std::string::npos);
	REQUIRE(body.find("hand_catalog_id = %s") != std::string::npos);
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
	const std::string policy = readFile("port/src/asset_archive_policy.c");
	REQUIRE(!character.empty());

	REQUIRE(character.find(".pdcharacter") != std::string::npos);
	REQUIRE(character.find("assetArchiveWriterAddDescriptor(&asset_writer, \"character.ini\"") !=
	        std::string::npos);
	REQUIRE(character.find("assetArchiveWriterAddPublicDisk(writer, inner, full") !=
	        std::string::npos);
	REQUIRE(character.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
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
	REQUIRE(scanner.find("assetArchiveTypeForPath(path)") != std::string::npos);
	REQUIRE(policy.find("\".pdcharacter\"") != std::string::npos);
	REQUIRE(policy.find("\"character.ini\"") != std::string::npos);
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
		{ "port/src/romextract_pdmesh.c", "mesh.ini" },
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
		const bool sharedDescriptorAdd =
			src.find("assetArchiveWriterAddDescriptor") !=
			std::string::npos;
		REQUIRE((literalAdd || descriptorVariableAdd || sharedDescriptorAdd));
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
	REQUIRE(audio.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sample.wav\"") !=
	        std::string::npos);
	REQUIRE(audio.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(audio.find("assetArchiveWriterAddDescriptor(&asset_writer, descriptor_name") !=
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
	REQUIRE(lang.find("rzipIs1173") != std::string::npos);
	REQUIRE(lang.find("rzipInflate") != std::string::npos);
	REQUIRE(lang.find("PDLANG_EXTRACT_VERSION") != std::string::npos);
	REQUIRE(lang.find("PDLANG_FAST_CACHE_KIND") != std::string::npos);
	REQUIRE(lang.find("s_existingArchiveHasEntry(dst_rel, \"strings.tsv\")") !=
	        std::string::npos);
	REQUIRE(lang.find("s_existingArchiveEntryContains(dst_rel, \"lang.ini\"") !=
	        std::string::npos);
	REQUIRE(lang.find("strings_file = strings.tsv") != std::string::npos);
	REQUIRE(lang.find("extract_version = %s\\n") != std::string::npos);
	REQUIRE(lang.find("assetArchiveWriterAddPublicMem(&asset_writer, \"strings.tsv\"") !=
	        std::string::npos);
	REQUIRE(lang.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(lang.find("assetArchiveWriterAddDescriptor(&asset_writer, \"lang.ini\"") !=
	        std::string::npos);
	REQUIRE(lang.find("\\\"data\\\": \\\"strings.tsv\\\"") !=
	        std::string::npos);

	REQUIRE(lang.find("sha256Hash((const u8 *)tsv_text") == std::string::npos);
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
	REQUIRE(font.find("assetArchiveWriterAddPublicMem(&asset_writer, \"glyphs.pgm\"") !=
	        std::string::npos);
	REQUIRE(font.find("assetArchiveWriterAddPublicMem(&asset_writer, \"metrics.tsv\"") !=
	        std::string::npos);
	REQUIRE(font.find("assetArchiveWriterAddPublicMem(&asset_writer, \"kerning.tsv\"") !=
	        std::string::npos);
	REQUIRE(font.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
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
	REQUIRE(song.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sequence.mid\"") !=
	        std::string::npos);
	REQUIRE(song.find("assetArchiveWriterAddPublicMem(&asset_writer, \"sequence.tsv\"") !=
	        std::string::npos);
	REQUIRE(song.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);

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
	REQUIRE(anim.find("assetArchiveWriterAddPublicMem(&asset_writer, \"header.tsv\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddPublicMem(&asset_writer, \"frames.tsv\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);

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
	REQUIRE(anim.find("assetArchiveWriterAddDescriptor(&asset_writer, \"animation.ini\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddManifestJson(&asset_writer") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterAddPublicMem(&asset_writer, \"opcodes.json\"") !=
	        std::string::npos);
	REQUIRE(anim.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
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
	const std::string gbi = readFile("port/src/preprocess/gbi.c");
	const std::string model_core = readFile("src/lib/model.c");
	REQUIRE(!mesh.empty());
	REQUIRE(!gbi.empty());
	REQUIRE(!model_core.empty());

	REQUIRE(mesh.find("s_buildModelObj") != std::string::npos);
	REQUIRE(mesh.find("s_exportGdlToObj") != std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_OBJ_EXPORT_VERSION_LABEL \"model_obj_mtx_v9_skeleton\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_FAST_CACHE_KIND \"pdmesh_model_obj_mtx_v11_skeleton_allmodels_menuhud\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("catalogReadableModelIdForFile((s32)FILE_GHUDPIECE, \"menu\", \"menu\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("(u16)FILE_GHUDPIECE, \"menu\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("#include \"preprocess.h\"") != std::string::npos);
	REQUIRE(mesh.find("#include \"game/modeldef.h\"") != std::string::npos);
	REQUIRE(mesh.find("#include \"lib/rzip.h\"") != std::string::npos);
	REQUIRE(mesh.find("s_loadSourceModelPreprocessed") != std::string::npos);
	REQUIRE(mesh.find("romextract pdmesh: preprocessed filenum") !=
	        std::string::npos);
	REQUIRE(mesh.find("trace 0x019c") == std::string::npos);
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
	REQUIRE(mesh.find("ROMEXTRACT_PDMESH_NODE_DEPTH_CAP") != std::string::npos);
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
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"export_version.txt\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.obj\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterAddPublicMem(&asset_writer, \"model.mtl\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("assetArchiveWriterFinishMetadata(&asset_writer)") !=
	        std::string::npos);
	REQUIRE(mesh.find("const char *wanted_hint = hint ? hint : \"\"") !=
	        std::string::npos);
	REQUIRE(mesh.find("strcmp(existing_id, wanted_id) == 0") !=
	        std::string::npos);
	REQUIRE(mesh.find("unique_mesh_jobs") != std::string::npos);
	REQUIRE(gbi.find("GBI_GDL_REWRITE_MAX_CMDS") != std::string::npos);
	REQUIRE(gbi.find("no ENDDL within") != std::string::npos);
	REQUIRE(model_core.find("MODEL_PROMOTE_NODE_VISIT_CAP") !=
	        std::string::npos);
	REQUIRE(model_core.find("promotion stopped at repeated/deep node") !=
	        std::string::npos);

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
