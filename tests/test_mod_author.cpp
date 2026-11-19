/**
 * test_mod_author.cpp -- template archive plus edits to a complete mod folder.
 *
 * modAuthorSave() is what the in-game creators call on Save. These cases make
 * weapon, character, body, head and vehicle mods from the checked-in example
 * archives in a scratch mods folder, then check the result with the readers
 * the game itself uses: release validation, the strict body/head/character
 * descriptor readers, the mod.json gate and the .pdmod folder packer.
 *
 * When PD2_TEST_ARTIFACT_DIR is set, the authored archives are also copied to
 * <dir>/clones so CI can run tools/asset_archive_conformance.py over them.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "body_head_source.h"
#include "character_head_policy.h"
#include "character_tool.h"
#include "constants.h"
#include "mod_author.h"
#include "mod_manifest_validate.h"
#include "modarchive.h"
#include "modpack_pdmod.h"
}
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

const char *const kWeaponTemplate =
	"examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
const char *const kCharacterTemplate =
	"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter";
const char *const kBodyTemplate =
	"examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody";
const char *const kHeadTemplate =
	"examples/modding/typed-pdxxx-basic/heads/tri_head.pdhead";
const char *const kVehicleTemplate =
	"examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle";
const char *const kArenaTemplate =
	"examples/modding/typed-pdxxx-basic/arenas/tri_arena.pdarena";

struct AuthorTempDir {
	fs::path path;

	AuthorTempDir() {
		static unsigned counter = 0;
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path()
			/ ("pd2-mod-author-" + std::to_string(stamp) + "-" + std::to_string(counter++));
		fs::create_directories(path);
	}

	~AuthorTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string authorReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void authorWriteFile(const fs::path &path, const std::string &bytes)
{
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

/* Every file and folder under `root`, relative, folders ending in '/'. */
std::vector<std::string> authorTree(const fs::path &root)
{
	std::vector<std::string> out;
	for (const auto &entry : fs::recursive_directory_iterator(root)) {
		std::string rel = entry.path().lexically_relative(root).generic_string();
		if (entry.is_directory()) rel += "/";
		out.push_back(rel);
	}
	std::sort(out.begin(), out.end());
	return out;
}

std::string authorEntry(const std::string &archive, const std::string &entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry.c_str(), &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

s32 authorCollectNameCb(const char *entryName, u32, void *user)
{
	static_cast<std::vector<std::string> *>(user)->push_back(entryName);
	return 0;
}

std::vector<std::string> authorEntryNames(const std::string &archive)
{
	std::vector<std::string> names;
	REQUIRE(modArchiveMemForEachEntry(archive.data(), static_cast<u32>(archive.size()),
		authorCollectNameCb, &names) == MODARCHIVE_OK);
	std::sort(names.begin(), names.end());
	return names;
}

std::string authorTrim(const std::string &text)
{
	size_t a = 0;
	size_t b = text.size();
	while (a < b && (text[a] == ' ' || text[a] == '\t')) a++;
	while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) b--;
	return text.substr(a, b - a);
}

/* Values assigned to `key` anywhere in a descriptor, in order. */
std::vector<std::string> authorIniValues(const std::string &ini, const std::string &key)
{
	std::vector<std::string> values;
	size_t at = 0;
	while (at < ini.size()) {
		size_t end = ini.find_first_of("\r\n", at);
		if (end == std::string::npos) end = ini.size();
		const std::string line = ini.substr(at, end - at);
		at = end + 1;
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		if (authorTrim(line.substr(0, eq)) == key) values.push_back(authorTrim(line.substr(eq + 1)));
	}
	return values;
}

std::string authorIniValue(const std::string &ini, const std::string &key)
{
	const std::vector<std::string> values = authorIniValues(ini, key);
	REQUIRE(values.size() == 1);
	return values[0];
}

std::string authorManifestField(const std::string &json, const char *key)
{
	char out[512] = {};
	REQUIRE(modManifestReadString(json.data(), json.size(), key, out, sizeof(out)) == 1);
	return out;
}

void authorKeepArtifact(const fs::path &archive)
{
	const char *root = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (!root || !root[0]) return;
	std::error_code ec;
	const fs::path dir = fs::path(root) / "clones";
	fs::create_directories(dir, ec);
	fs::copy_file(archive, dir / archive.filename(), fs::copy_options::overwrite_existing, ec);
}

/* A request whose strings live as long as the object. */
struct AuthorRequest {
	std::string tmpl;
	std::string root;
	mod_author_request_t request{};

	AuthorRequest(const std::string &templatePath, const fs::path &modsRoot, const char *name)
		: tmpl(templatePath), root(modsRoot.string())
	{
		request.template_path = tmpl.c_str();
		request.mods_root = root.c_str();
		request.display_name = name;
		request.tool = "pd-tests";
	}
	AuthorRequest(const AuthorRequest &) = delete;
	AuthorRequest &operator=(const AuthorRequest &) = delete;
};

std::string authorExample(const char *relative)
{
	return (fs::path(PD_SOURCE_DIR) / relative).string();
}

struct AuthorCall {
	s32 code = 0;
	std::string err;
	std::unique_ptr<mod_author_result_t> result;
};

AuthorCall authorSave(const mod_author_request_t &request)
{
	AuthorCall call;
	char err[256] = {};
	call.result = std::make_unique<mod_author_result_t>();
	/* Dirty on entry: a failure must hand back a zeroed result. */
	std::memset(call.result.get(), 0x5a, sizeof(mod_author_result_t));
	call.code = modAuthorSave(&request, call.result.get(), err, sizeof(err));
	call.err = err;
	return call;
}

AuthorCall authorPlan(const mod_author_request_t &request)
{
	AuthorCall call;
	char err[256] = {};
	call.result = std::make_unique<mod_author_result_t>();
	std::memset(call.result.get(), 0x5a, sizeof(mod_author_result_t));
	call.code = modAuthorPlan(&request, call.result.get(), err, sizeof(err));
	call.err = err;
	return call;
}

bool authorResultIsZero(const mod_author_result_t &result)
{
	const unsigned char *bytes = reinterpret_cast<const unsigned char *>(&result);
	for (size_t i = 0; i < sizeof(result); i++) {
		if (bytes[i] != 0) return false;
	}
	return true;
}

void authorRequireReleaseValid(const char *archivePath)
{
	char err[256] = {};
	const s32 fileCode = assetArchiveValidateFile(archivePath,
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err));
	INFO("file policy: " << err);
	REQUIRE(fileCode == 0);

	const std::string bytes = authorReadFile(archivePath);
	const s32 bytesCode = assetArchiveValidateBytes(bytes.data(),
		static_cast<u32>(bytes.size()), archivePath, ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, sizeof(err));
	INFO("bytes policy: " << err);
	REQUIRE(bytesCode == 0);
}

} // namespace

TEST_CASE("mod author creates a complete weapon mod from a template",
          "[modding][author]") {
	AuthorTempDir root;
	AuthorRequest req(authorExample(kWeaponTemplate), root.path, "  Rivet Carbine \t");
	req.request.author = "Mike";

	/* Plan: the names the player is about to get, and nothing written. */
	const AuthorCall plan = authorPlan(req.request);
	INFO("plan error: " << plan.err);
	REQUIRE(plan.code == MOD_AUTHOR_OK);
	CHECK(std::string(plan.result->catalog_id) == "mod:weapon_rivet_carbine");
	CHECK(std::string(plan.result->mod_id) == "mod.weapon_rivet_carbine");
	CHECK(plan.result->updated == 0);
	CHECK(plan.result->ids.id_count == 0);
	CHECK(authorTree(root.path).empty());

	const AuthorCall saved = authorSave(req.request);
	INFO("save error: " << saved.err);
	REQUIRE(saved.code == MOD_AUTHOR_OK);
	const mod_author_result_t &result = *saved.result;
	CHECK(saved.err.empty());
	CHECK(std::string(result.kind) == "weapon");
	CHECK(std::string(result.slug) == "weapon_rivet_carbine");
	CHECK(std::string(result.mod_id) == "mod.weapon_rivet_carbine");
	CHECK(std::string(result.catalog_id) == "mod:weapon_rivet_carbine");
	CHECK(result.updated == 0);
	CHECK(std::string(result.archive_path) == std::string(plan.result->archive_path));
	CHECK(std::string(result.manifest_path) == std::string(plan.result->manifest_path));
	REQUIRE(result.ids.id_count >= 1);
	CHECK(std::string(result.ids.ids[0].old_id) == "example:tri_weapon");
	CHECK(std::string(result.ids.ids[0].new_id) == "mod:weapon_rivet_carbine");

	/* Exactly one mod folder holding exactly the two files. */
	const std::vector<std::string> expected = {
		"Weapons/",
		"Weapons/weapon_rivet_carbine/",
		"Weapons/weapon_rivet_carbine/mod.json",
		"Weapons/weapon_rivet_carbine/weapon_rivet_carbine.pdweapon",
	};
	CHECK(authorTree(root.path) == expected);
	CHECK(fs::equivalent(result.archive_path,
		root.path / "Weapons" / "weapon_rivet_carbine" / "weapon_rivet_carbine.pdweapon"));
	CHECK(fs::equivalent(result.manifest_path,
		root.path / "Weapons" / "weapon_rivet_carbine" / "mod.json"));
	CHECK(fs::equivalent(result.mod_dir, root.path / "Weapons" / "weapon_rivet_carbine"));

	/* The archive: release-valid, mod-owned, named, otherwise the template. */
	authorRequireReleaseValid(result.archive_path);
	const std::string archive = authorReadFile(result.archive_path);
	const std::string templateArchive = authorReadFile(req.tmpl);
	const std::string descriptor = authorEntry(archive, "weapon.ini");
	const std::string templateDescriptor = authorEntry(templateArchive, "weapon.ini");
	CHECK(authorIniValue(descriptor, "catalog_id") == "mod:weapon_rivet_carbine");
	CHECK(authorIniValue(descriptor, "name") == "Rivet Carbine");
	for (const char *key : { "dual_wieldable", "model_file", "primary_graph", "secondary_graph",
			"settings_file", "variables_file", "shared_context_file", "presentation_file",
			"reticle_archive", "primary_projectile_archive", "deployed_entity_archive",
			"manifest" }) {
		INFO("descriptor key " << key);
		CHECK(authorIniValue(descriptor, key) == authorIniValue(templateDescriptor, key));
	}
	CHECK(std::count(descriptor.begin(), descriptor.end(), '\n')
		== std::count(templateDescriptor.begin(), templateDescriptor.end(), '\n'));
	CHECK(descriptor.find("example:") == std::string::npos);
	CHECK(authorEntryNames(archive).size() >= authorEntryNames(templateArchive).size());
	CHECK(authorEntry(archive, "behavior/settings.json")
		== authorEntry(templateArchive, "behavior/settings.json"));

	/* mod.json: accepted by the packers' gate and carrying what was asked. */
	const std::string manifest = authorReadFile(result.manifest_path);
	char verdict[160] = {};
	REQUIRE(modManifestValidateBytes(manifest.data(), manifest.size(), verdict,
		sizeof(verdict)) == MOD_MANIFEST_OK);
	CHECK(authorManifestField(manifest, "id") == "mod.weapon_rivet_carbine");
	CHECK(authorManifestField(manifest, "name") == "Rivet Carbine");
	CHECK(authorManifestField(manifest, "version") == "1.0.0");
	CHECK(authorManifestField(manifest, "author") == "Mike");
	CHECK(authorManifestField(manifest, "description") == "Created with the in-game mod tools.");
	CHECK(manifest.find("\"tags\": [\"weapon\", \"pdweapon\", \"user\"]") != std::string::npos);
	CHECK(manifest.find("\"contents\": [\"weapon\"]") != std::string::npos);

	/* The in-game folder packer takes the mod as it stands and adds nothing
	 * to the mod folder while doing so. */
	AuthorTempDir out;
	const std::string pdmod = (out.path / "rivet_carbine.pdmod").string();
	const s32 packed = modpackPdmodFromFolder(result.mod_dir, pdmod.c_str());
	INFO("pack error: " << modpackPdmodLastError());
	REQUIRE(packed == MODPACK_PDMOD_OK);
	const std::string pack = authorReadFile(pdmod);
	const std::vector<std::string> packed_names = authorEntryNames(pack);
	const std::vector<std::string> expected_names = { "mod.json", "weapon_rivet_carbine.pdweapon" };
	CHECK(packed_names == expected_names);
	CHECK(authorEntry(pack, "mod.json") == manifest);
	CHECK(authorEntry(pack, "weapon_rivet_carbine.pdweapon") == archive);
	CHECK(authorTree(root.path) == expected);

	authorKeepArtifact(result.archive_path);
}

TEST_CASE("mod author names characters, bodies, heads and vehicles the way their loaders read them",
          "[modding][author]") {
	AuthorTempDir root;

	SECTION("character: quotes, a backslash and UTF-8 survive in both files") {
		const char *display = "Wren \"Sparks\" O\\Neil \xc3\xa9";
		AuthorRequest req(authorExample(kCharacterTemplate), root.path, display);
		req.request.author = "D\xc3\xa9v \"Q\"";
		req.request.version = "0.3.1";
		req.request.description = "Dock welder. Path C:\\mods is not special.";
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		const mod_author_result_t &result = *saved.result;
		CHECK(std::string(result.catalog_id) == "mod:character_wren_sparks_o_neil");
		CHECK(fs::equivalent(result.mod_dir,
			root.path / "Characters" / "character_wren_sparks_o_neil"));
		authorRequireReleaseValid(result.archive_path);

		const std::string manifest = authorReadFile(result.manifest_path);
		CHECK(authorManifestField(manifest, "name") == display);
		CHECK(authorManifestField(manifest, "author") == "D\xc3\xa9v \"Q\"");
		CHECK(authorManifestField(manifest, "version") == "0.3.1");
		CHECK(authorManifestField(manifest, "description")
			== "Dock welder. Path C:\\mods is not special.");
		CHECK(manifest.find("\"contents\": [\"character\"]") != std::string::npos);

		/* The game's own character reader accepts the descriptor and sees the
		 * name, with the embedded body and head now in the mod's namespace. */
		const std::string archive = authorReadFile(result.archive_path);
		const std::string descriptor = authorEntry(archive, "character.ini");
		auto ini = std::make_unique<ini_section_t>();
		char err[160] = {};
		REQUIRE(bodyHeadSourceReadIniBytes(descriptor.data(), static_cast<u32>(descriptor.size()),
			"character", ini.get(), err, sizeof(err)) == 1);
		auto entry = std::make_unique<asset_entry_t>();
		std::memset(entry.get(), 0, sizeof(asset_entry_t));
		entry->type = ASSET_CHARACTER;
		std::strcpy(entry->id, result.catalog_id);
		REQUIRE(characterSourceApplyIni(entry.get(), ini.get()) == 1);
		CHECK(std::string(entry->ext.character.display_name) == display);
		CHECK(std::string(entry->ext.character.body_id)
			== "mod:character_wren_sparks_o_neil__body_tri_body");
		CHECK(std::string(entry->ext.character.head_id)
			== "mod:character_wren_sparks_o_neil__head_tri_head");
		CHECK(characterSourcePolicy(entry.get()) == CHARACTER_HEAD_POLICY_FIXED);

		authorKeepArtifact(result.archive_path);
	}

	SECTION("body: an existing display name is replaced in place") {
		AuthorRequest req(authorExample(kBodyTemplate), root.path, "Dock Overalls");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		const mod_author_result_t &result = *saved.result;
		CHECK(std::string(result.catalog_id) == "mod:body_dock_overalls");
		CHECK(fs::equivalent(result.mod_dir, root.path / "Characters" / "body_dock_overalls"));
		authorRequireReleaseValid(result.archive_path);

		const std::string archive = authorReadFile(result.archive_path);
		const std::string descriptor = authorEntry(archive, "body.ini");
		const std::string templateDescriptor =
			authorEntry(authorReadFile(req.tmpl), "body.ini");
		CHECK(std::count(descriptor.begin(), descriptor.end(), '\n')
			== std::count(templateDescriptor.begin(), templateDescriptor.end(), '\n'));

		auto source = std::make_unique<body_head_source_t>();
		char err[160] = {};
		const s32 parsed = bodyHeadSourceParseArchiveBytes(archive.data(),
			static_cast<u32>(archive.size()), result.catalog_id, 0, source.get(),
			err, sizeof(err));
		INFO("body reader: " << err);
		REQUIRE(parsed == 1);
		CHECK(source->has_display_name == 1);
		CHECK(std::string(source->display_name) == "Dock Overalls");
		CHECK(std::string(source->mesh_archive) == "mesh.pdmesh");
		CHECK(std::string(source->hand_archive) == "hand.pdmesh");

		authorKeepArtifact(result.archive_path);
	}

	SECTION("head: a descriptor with no name gains one after its catalog id") {
		AuthorRequest req(authorExample(kHeadTemplate), root.path, "Dock Visor");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		const mod_author_result_t &result = *saved.result;
		CHECK(std::string(result.catalog_id) == "mod:head_dock_visor");
		authorRequireReleaseValid(result.archive_path);

		const std::string archive = authorReadFile(result.archive_path);
		const std::string descriptor = authorEntry(archive, "head.ini");
		const std::string templateDescriptor =
			authorEntry(authorReadFile(req.tmpl), "head.ini");
		CHECK(authorIniValues(templateDescriptor, "display_name").empty());
		CHECK(std::count(descriptor.begin(), descriptor.end(), '\n')
			== std::count(templateDescriptor.begin(), templateDescriptor.end(), '\n') + 1);
		CHECK(descriptor.find("catalog_id = mod:head_dock_visor\ndisplay_name = Dock Visor\n")
			!= std::string::npos);

		auto source = std::make_unique<body_head_source_t>();
		char err[160] = {};
		const s32 parsed = bodyHeadSourceParseArchiveBytes(archive.data(),
			static_cast<u32>(archive.size()), result.catalog_id, 1, source.get(),
			err, sizeof(err));
		INFO("head reader: " << err);
		REQUIRE(parsed == 1);
		CHECK(std::string(source->display_name) == "Dock Visor");

		authorKeepArtifact(result.archive_path);
	}

	SECTION("vehicle") {
		AuthorRequest req(authorExample(kVehicleTemplate), root.path, "Dock Tug");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		const mod_author_result_t &result = *saved.result;
		CHECK(std::string(result.catalog_id) == "mod:vehicle_dock_tug");
		CHECK(fs::equivalent(result.archive_path,
			root.path / "Vehicles" / "vehicle_dock_tug" / "vehicle_dock_tug.pdvehicle"));
		authorRequireReleaseValid(result.archive_path);

		const std::string archive = authorReadFile(result.archive_path);
		const std::string descriptor = authorEntry(archive, "vehicle.ini");
		CHECK(authorIniValue(descriptor, "catalog_id") == "mod:vehicle_dock_tug");
		CHECK(authorIniValue(descriptor, "name") == "Dock Tug");
		CHECK(authorIniValue(descriptor, "physics_file") == "physics.json");
		const std::string manifest = authorReadFile(result.manifest_path);
		CHECK(manifest.find("\"tags\": [\"vehicle\", \"pdvehicle\", \"user\"]")
			!= std::string::npos);

		authorKeepArtifact(result.archive_path);
	}

	SECTION("arena: a level gains a name of its own, which no template has") {
		/* A base arena is named by a language id. A mod level has no string
		 * in the game's language files, so the author writes display_name
		 * and the arena lists read it (Forge F1). */
		AuthorRequest req(authorExample(kArenaTemplate), root.path, "Dock Yard");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		const mod_author_result_t &result = *saved.result;
		CHECK(std::string(result.catalog_id) == "mod:arena_dock_yard");
		CHECK(fs::equivalent(result.archive_path,
			root.path / "MP Maps" / "arena_dock_yard" / "arena_dock_yard.pdarena"));
		authorRequireReleaseValid(result.archive_path);

		const std::string archive = authorReadFile(result.archive_path);
		const std::string descriptor = authorEntry(archive, "arena.ini");
		const std::string templateDescriptor =
			authorEntry(authorReadFile(req.tmpl), "arena.ini");
		CHECK(authorIniValues(templateDescriptor, "display_name").empty());
		CHECK(authorIniValue(descriptor, "catalog_id") == "mod:arena_dock_yard");
		CHECK(authorIniValue(descriptor, "display_name") == "Dock Yard");
		CHECK(std::count(descriptor.begin(), descriptor.end(), '\n')
			== std::count(templateDescriptor.begin(), templateDescriptor.end(), '\n') + 1);
		/* What the level loads is its template's: the load mode by name, and
		 * the scenario inside the archive, now in the mod's namespace. */
		CHECK(authorIniValue(descriptor, "load_mode") == "ARENA_LOADMODE_PLAYABLE");
		CHECK(authorIniValue(descriptor, "scenario_archive")
			== authorIniValue(templateDescriptor, "scenario_archive"));
		CHECK(authorIniValue(descriptor, "scenario").rfind("mod:arena_dock_yard__", 0) == 0);
		const std::string manifest = authorReadFile(result.manifest_path);
		CHECK(authorManifestField(manifest, "name") == "Dock Yard");
		CHECK(manifest.find("\"contents\": [\"arena\"]") != std::string::npos);

		authorKeepArtifact(result.archive_path);
	}
}

TEST_CASE("mod author writes the name into descriptors of every line-ending style",
          "[modding][author]") {
	AuthorTempDir work;
	AuthorTempDir root;
	const std::string source = authorExample(kVehicleTemplate);
	char err[256] = {};

	struct Form {
		const char *label;
		const char *descriptor;   /* replaces the template's vehicle.ini */
		const char *expected;     /* the authored vehicle.ini */
	};
	const Form forms[] = {
		{ "crlf_replace",
		  "[vehicle]\r\ncatalog_id = example:tri_vehicle\r\nname=Old\r\nmodel_file = model.gltf\r\n"
		  "physics_file = physics.json\r\nbehavior_graph = behavior.graph.json\r\n",
		  "[vehicle]\r\ncatalog_id = mod:vehicle_form_crlf_replace\r\nname = Form crlf_replace\r\n"
		  "model_file = model.gltf\r\nphysics_file = physics.json\r\n"
		  "behavior_graph = behavior.graph.json\r\n" },
		{ "crlf_insert",
		  "; no name here\r\n[vehicle]\r\ncatalog_id = example:tri_vehicle\r\nmodel_file = model.gltf\r\n"
		  "physics_file = physics.json\r\nbehavior_graph = behavior.graph.json\r\n",
		  "; no name here\r\n[vehicle]\r\ncatalog_id = mod:vehicle_form_crlf_insert\r\n"
		  "name = Form crlf_insert\r\nmodel_file = model.gltf\r\nphysics_file = physics.json\r\n"
		  "behavior_graph = behavior.graph.json\r\n" },
		{ "no_final_newline",
		  "[vehicle]\nmodel_file = model.gltf\nphysics_file = physics.json\n"
		  "behavior_graph = behavior.graph.json\ncatalog_id = example:tri_vehicle",
		  "[vehicle]\nmodel_file = model.gltf\nphysics_file = physics.json\n"
		  "behavior_graph = behavior.graph.json\ncatalog_id = mod:vehicle_form_no_final_newline\n"
		  "name = Form no_final_newline\n" },
		{ "indented_and_commented",
		  "# name = not this one\n[vehicle]\n\t; name = nor this\n  catalog_id = example:tri_vehicle\n"
		  "  name\t=\tOld\n  name_note = keep me\n  model_file = model.gltf\n"
		  "  physics_file = physics.json\n  behavior_graph = behavior.graph.json\n",
		  "# name = not this one\n[vehicle]\n\t; name = nor this\n"
		  "  catalog_id = mod:vehicle_form_indented_and_commented\n"
		  "name = Form indented_and_commented\n  name_note = keep me\n  model_file = model.gltf\n"
		  "  physics_file = physics.json\n  behavior_graph = behavior.graph.json\n" },
	};

	for (const Form &form : forms) {
		INFO("form " << form.label);
		/* A template carrying this descriptor: the example rebuilt under its
		 * own id with only the descriptor replaced. */
		const fs::path tmpl = work.path / (std::string(form.label) + ".pdvehicle");
		asset_archive_clone_override_t ov{};
		ov.entry = "vehicle.ini";
		ov.data = form.descriptor;
		ov.len = static_cast<u32>(std::strlen(form.descriptor));
		const s32 built = assetArchiveRebuildFile(source.c_str(), tmpl.string().c_str(),
			"example:tri_vehicle", "pd-tests", &ov, 1, nullptr, err, sizeof(err));
		INFO("template error: " << err);
		REQUIRE(built == ASSET_ARCHIVE_CLONE_OK);

		const std::string display = std::string("Form ") + form.label;
		AuthorRequest req(tmpl.string(), root.path, display.c_str());
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);
		CHECK(authorEntry(authorReadFile(saved.result->archive_path), "vehicle.ini")
			== form.expected);
	}

	/* A name key kept outside the first section cannot be set safely. */
	{
		const char *descriptor =
			"[vehicle]\ncatalog_id = example:tri_vehicle\nmodel_file = model.gltf\n"
			"physics_file = physics.json\nbehavior_graph = behavior.graph.json\n"
			"[notes]\nname = Elsewhere\n";
		const fs::path tmpl = work.path / "name_elsewhere.pdvehicle";
		asset_archive_clone_override_t ov{};
		ov.entry = "vehicle.ini";
		ov.data = descriptor;
		ov.len = static_cast<u32>(std::strlen(descriptor));
		REQUIRE(assetArchiveRebuildFile(source.c_str(), tmpl.string().c_str(),
			"example:tri_vehicle", "pd-tests", &ov, 1, nullptr, err, sizeof(err))
			== ASSET_ARCHIVE_CLONE_OK);

		const std::vector<std::string> before = authorTree(root.path);
		AuthorRequest req(tmpl.string(), root.path, "Name Elsewhere");
		const AuthorCall saved = authorSave(req.request);
		CHECK(saved.code == MOD_AUTHOR_ERR_TEMPLATE);
		CHECK(!saved.err.empty());
		CHECK(authorResultIsZero(*saved.result));
		CHECK(authorTree(root.path) == before);
	}
}

TEST_CASE("mod author drops what marks a template as base-game content",
          "[modding][author]") {
	AuthorTempDir work;
	AuthorTempDir root;
	char err[256] = {};

	/* Rebuild an example under its own id with extra descriptor lines, the
	 * way an archive extracted from the base game carries them. */
	const auto baseLike = [&](const char *relative, const char *descriptorName,
			const char *id, const char *extra, const char *leaf) {
		const std::string source = authorExample(relative);
		std::string descriptor = authorEntry(authorReadFile(source), descriptorName);
		const size_t at = descriptor.find(std::string("catalog_id = ") + id);
		REQUIRE(at != std::string::npos);
		descriptor.insert(at, extra);
		asset_archive_clone_override_t ov{};
		ov.entry = descriptorName;
		ov.data = descriptor.data();
		ov.len = static_cast<u32>(descriptor.size());
		const fs::path out = work.path / leaf;
		const s32 built = assetArchiveRebuildFile(source.c_str(), out.string().c_str(),
			id, "pd-tests", &ov, 1, nullptr, err, sizeof(err));
		INFO("template " << leaf << " error: " << err);
		REQUIRE(built == ASSET_ARCHIVE_CLONE_OK);
		return out.string();
	};

	SECTION("weapon: bundled and its owner category go, look-alike keys stay") {
		const std::string tmpl = baseLike(kWeaponTemplate, "weapon.ini",
			"example:tri_weapon",
			"schema = pd.weapon.v1\ncategory = base\nbundled = 1\n"
			"bundled_note = keep me\n\tcategory\t=\tbase\n", "base_like.pdweapon");
		AuthorRequest req(tmpl, root.path, "Owned Gun");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);
		const std::string written =
			authorEntry(authorReadFile(saved.result->archive_path), "weapon.ini");
		CHECK(authorIniValues(written, "bundled").empty());
		CHECK(authorIniValues(written, "category").empty());
		CHECK(authorIniValue(written, "bundled_note") == "keep me");
		CHECK(authorIniValue(written, "schema") == "pd.weapon.v1");
		CHECK(authorIniValue(written, "name") == "Owned Gun");
		CHECK(authorIniValue(written, "catalog_id") == "mod:weapon_owned_gun");
		CHECK(authorIniValue(written, "manifest") == "_meta/manifest.json");

		/* A caller that supplies the descriptor decides for itself. */
		const std::string templateDescriptor = authorEntry(authorReadFile(tmpl), "weapon.ini");
		asset_archive_clone_override_t ov{};
		ov.entry = "weapon.ini";
		ov.data = templateDescriptor.data();
		ov.len = static_cast<u32>(templateDescriptor.size());
		AuthorRequest own(tmpl, root.path, "Callers Gun");
		own.request.overrides = &ov;
		own.request.override_count = 1;
		const AuthorCall kept = authorSave(own.request);
		INFO("save error: " << kept.err);
		REQUIRE(kept.code == MOD_AUTHOR_OK);
		const std::string keptDescriptor =
			authorEntry(authorReadFile(kept.result->archive_path), "weapon.ini");
		CHECK(authorIniValues(keptDescriptor, "bundled").size() == 1);
		CHECK(authorIniValues(keptDescriptor, "category").size() == 2);
	}

	SECTION("character: the unlock requirement goes and the game's reader still accepts it") {
		const std::string tmpl = baseLike(kCharacterTemplate, "character.ini",
			"example:tri_character", "requirefeature = 58\nbundled = 1\n",
			"base_like.pdcharacter");
		AuthorRequest req(tmpl, root.path, "Owned Agent");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);
		const std::string written =
			authorEntry(authorReadFile(saved.result->archive_path), "character.ini");
		CHECK(authorIniValues(written, "requirefeature").empty());
		CHECK(authorIniValues(written, "bundled").empty());
		CHECK(authorIniValue(written, "display_name") == "Owned Agent");

		auto ini = std::make_unique<ini_section_t>();
		REQUIRE(bodyHeadSourceReadIniBytes(written.data(), static_cast<u32>(written.size()),
			"character", ini.get(), err, sizeof(err)) == 1);
		auto entry = std::make_unique<asset_entry_t>();
		std::memset(entry.get(), 0, sizeof(asset_entry_t));
		entry->type = ASSET_CHARACTER;
		std::strcpy(entry->id, saved.result->catalog_id);
		REQUIRE(characterSourceApplyIni(entry.get(), ini.get()) == 1);
		CHECK(std::string(entry->ext.character.display_name) == "Owned Agent");
	}

	SECTION("body: the unlock requirement goes") {
		const std::string tmpl = baseLike(kBodyTemplate, "body.ini",
			"example:tri_body", "requirefeature = 61\nbundled = 1\n",
			"base_like.pdbody");
		AuthorRequest req(tmpl, root.path, "Owned Body");
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		const std::string archive = authorReadFile(saved.result->archive_path);
		auto source = std::make_unique<body_head_source_t>();
		REQUIRE(bodyHeadSourceParseArchiveBytes(archive.data(), static_cast<u32>(archive.size()),
			saved.result->catalog_id, 0, source.get(), err, sizeof(err)) == 1);
		CHECK(source->requirefeature == 0);
		CHECK(source->bundled == 0);
		CHECK(std::string(source->display_name) == "Owned Body");
	}
}

TEST_CASE("mod author leaves the descriptor alone when the caller supplies it",
          "[modding][author]") {
	AuthorTempDir root;
	const std::string templateArchive = authorReadFile(authorExample(kWeaponTemplate));
	std::string descriptor = authorEntry(templateArchive, "weapon.ini");
	const std::string oldName = "name = Triangle Weapon";
	const size_t at = descriptor.find(oldName);
	REQUIRE(at != std::string::npos);
	descriptor.replace(at, oldName.size(), "name = Shop Name\nposx = 30.0");

	asset_archive_clone_override_t ov{};
	ov.entry = "weapon.ini";
	ov.data = descriptor.data();
	ov.len = static_cast<u32>(descriptor.size());
	AuthorRequest req(authorExample(kWeaponTemplate), root.path, "Folder Name");
	req.request.overrides = &ov;
	req.request.override_count = 1;

	const AuthorCall saved = authorSave(req.request);
	INFO("save error: " << saved.err);
	REQUIRE(saved.code == MOD_AUTHOR_OK);
	CHECK(std::string(saved.result->catalog_id) == "mod:weapon_folder_name");
	authorRequireReleaseValid(saved.result->archive_path);
	const std::string written =
		authorEntry(authorReadFile(saved.result->archive_path), "weapon.ini");
	CHECK(authorIniValue(written, "name") == "Shop Name");
	CHECK(authorIniValue(written, "posx") == "30.0");
	CHECK(authorIniValue(written, "catalog_id") == "mod:weapon_folder_name");
	CHECK(authorManifestField(authorReadFile(saved.result->manifest_path), "name")
		== "Folder Name");
}

TEST_CASE("mod author refuses text it cannot store and writes nothing",
          "[modding][author]") {
	AuthorTempDir root;
	const std::string tmpl = authorExample(kCharacterTemplate);
	const std::string sixtyThree(63, 'n');
	const std::string sixtyFour(64, 'n');

	const auto expectRefused = [&](const mod_author_request_t &request, const char *what) {
		const AuthorCall saved = authorSave(request);
		INFO(what << " -> " << saved.err);
		CHECK(saved.code == MOD_AUTHOR_ERR_ARGS);
		CHECK(!saved.err.empty());
		CHECK(authorResultIsZero(*saved.result));
		const AuthorCall plan = authorPlan(request);
		CHECK((plan.code == MOD_AUTHOR_ERR_ARGS || plan.code == MOD_AUTHOR_OK));
		CHECK(authorTree(root.path).empty());
	};

	/* Display names. */
	for (const char *bad : { "", "   ", "\t", "line\nbreak", "carriage\rreturn", "tab\tinside",
			"bell\x07", "del\x7f", "\xff\xfe", "half \xc3", "overlong \xc0\xaf",
			"surrogate \xed\xa0\x80", "!!!", "\xc3\xa9\xc3\xa9", "- _ -" }) {
		AuthorRequest req(tmpl, root.path, bad);
		expectRefused(req.request, "display name");
		/* A name that cannot be stored is refused by Plan as well. */
		CHECK(authorPlan(req.request).code == MOD_AUTHOR_ERR_ARGS);
	}
	{
		AuthorRequest req(tmpl, root.path, sixtyFour.c_str());
		expectRefused(req.request, "64-byte display name");
		AuthorRequest missing(tmpl, root.path, nullptr);
		expectRefused(missing.request, "missing display name");
	}

	/* The other mod.json fields: one line, valid UTF-8, within the size the
	 * mod manager keeps. */
	{
		const std::string longAuthor(64, 'a');
		const std::string longVersion(32, '1');
		const std::string longDescription(256, 'd');
		struct Field { const char *what; const char *author; const char *version; const char *description; };
		const Field fields[] = {
			{ "author with a newline", "Mike\nHays", nullptr, nullptr },
			{ "author too long", longAuthor.c_str(), nullptr, nullptr },
			{ "author not UTF-8", "Mike\xff", nullptr, nullptr },
			{ "version with a tab", nullptr, "1.0\t0", nullptr },
			{ "version too long", nullptr, longVersion.c_str(), nullptr },
			{ "description with a newline", nullptr, nullptr, "two\nlines" },
			{ "description too long", nullptr, nullptr, longDescription.c_str() },
			{ "blank author", "   ", nullptr, nullptr },
		};
		for (const Field &field : fields) {
			AuthorRequest req(tmpl, root.path, "Wren");
			req.request.author = field.author;
			req.request.version = field.version;
			req.request.description = field.description;
			expectRefused(req.request, field.what);
		}
	}

	/* Requests that are malformed in other ways. */
	{
		AuthorRequest req(tmpl, root.path, "Wren");
		req.request.override_count = 1;
		expectRefused(req.request, "override count without a list");

		mod_author_request_t empty{};
		expectRefused(empty, "empty request");

		char err[64] = {};
		CHECK(modAuthorSave(nullptr, nullptr, err, sizeof(err)) == MOD_AUTHOR_ERR_ARGS);
		CHECK(modAuthorPlan(&req.request, nullptr, err, sizeof(err)) == MOD_AUTHOR_ERR_ARGS);
	}

	/* The limits themselves are usable: a 63-byte name and full-size fields. */
	{
		const std::string author(63, 'a');
		const std::string version(31, '1');
		const std::string description(255, 'd');
		AuthorRequest req(tmpl, root.path, sixtyThree.c_str());
		req.request.author = author.c_str();
		req.request.version = version.c_str();
		req.request.description = description.c_str();
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		/* Short enough that the embedded body and head still get ids. */
		CHECK(std::strlen(saved.result->catalog_id) == ASSET_ARCHIVE_CLONE_ROOT_ID_MAX);
		CHECK(std::strlen(saved.result->mod_id) == ASSET_ARCHIVE_CLONE_ROOT_ID_MAX);
		CHECK(saved.result->ids.id_count >= 3);
		for (u32 i = 0; i < saved.result->ids.id_count; i++) {
			CHECK(std::strlen(saved.result->ids.ids[i].new_id) < CATALOG_ID_LEN);
		}
		const std::string manifest = authorReadFile(saved.result->manifest_path);
		CHECK(authorManifestField(manifest, "name") == sixtyThree);
		CHECK(authorManifestField(manifest, "author") == author);
		CHECK(authorManifestField(manifest, "version") == version);
		CHECK(authorManifestField(manifest, "description") == description);
		CHECK(authorIniValue(authorEntry(authorReadFile(saved.result->archive_path),
			"character.ini"), "display_name") == sixtyThree);
		authorRequireReleaseValid(saved.result->archive_path);
	}
}

TEST_CASE("mod author keeps long names apart and stable",
          "[modding][author]") {
	AuthorTempDir root;
	const std::string tmpl = authorExample(kVehicleTemplate);
	const std::string nameA = std::string(60, 'a') + "001";
	const std::string nameB = std::string(60, 'a') + "002";

	AuthorRequest reqA(tmpl, root.path, nameA.c_str());
	AuthorRequest reqB(tmpl, root.path, nameB.c_str());
	const AuthorCall a = authorSave(reqA.request);
	const AuthorCall b = authorSave(reqB.request);
	INFO("errors: " << a.err << " / " << b.err);
	REQUIRE(a.code == MOD_AUTHOR_OK);
	REQUIRE(b.code == MOD_AUTHOR_OK);
	CHECK(std::string(a.result->slug) != std::string(b.result->slug));
	CHECK(std::strlen(a.result->slug) == ASSET_ARCHIVE_CLONE_ROOT_ID_MAX - 4);
	CHECK(std::strlen(b.result->slug) == ASSET_ARCHIVE_CLONE_ROOT_ID_MAX - 4);
	CHECK(std::string(a.result->slug).rfind("vehicle_aaaa", 0) == 0);
	CHECK(a.result->updated == 0);
	CHECK(b.result->updated == 0);
	CHECK(authorIniValue(authorEntry(authorReadFile(a.result->archive_path), "vehicle.ini"),
		"name") == nameA);
	CHECK(authorIniValue(authorEntry(authorReadFile(b.result->archive_path), "vehicle.ini"),
		"name") == nameB);
	authorRequireReleaseValid(a.result->archive_path);

	/* The same long name always lands on the same mod. */
	const AuthorCall again = authorSave(reqA.request);
	REQUIRE(again.code == MOD_AUTHOR_OK);
	CHECK(std::string(again.result->slug) == std::string(a.result->slug));
	CHECK(again.result->updated == 1);

	/* Names that differ only in punctuation or case are the same mod. */
	AuthorRequest plain(tmpl, root.path, "Dock Tug");
	AuthorRequest noisy(tmpl, root.path, "dock--TUG!");
	REQUIRE(authorSave(plain.request).code == MOD_AUTHOR_OK);
	const AuthorCall planNoisy = authorPlan(noisy.request);
	REQUIRE(planNoisy.code == MOD_AUTHOR_OK);
	CHECK(std::string(planNoisy.result->slug) == "vehicle_dock_tug");
	CHECK(planNoisy.result->updated == 1);

	/* The family prefix is not doubled, but a bare family name still gets it. */
	AuthorRequest prefixed(tmpl, root.path, "Vehicle Tug");
	AuthorRequest bare(tmpl, root.path, "Vehicle");
	CHECK(std::string(authorPlan(prefixed.request).result->slug) == "vehicle_tug");
	CHECK(std::string(authorPlan(bare.request).result->slug) == "vehicle_vehicle");
}

TEST_CASE("mod author updates a mod in place and edits it from its own archive",
          "[modding][author]") {
	AuthorTempDir root;
	AuthorRequest first(authorExample(kWeaponTemplate), root.path, "Rivet Carbine");
	const AuthorCall v1 = authorSave(first.request);
	INFO("v1 error: " << v1.err);
	REQUIRE(v1.code == MOD_AUTHOR_OK);
	const std::vector<std::string> tree = authorTree(root.path);
	const std::string archiveV1 = authorReadFile(v1.result->archive_path);

	/* Plan now reports that saving replaces an existing mod. */
	const AuthorCall plan = authorPlan(first.request);
	REQUIRE(plan.code == MOD_AUTHOR_OK);
	CHECK(plan.result->updated == 1);

	/* Save again from the template with an edit and a new version. */
	const std::string settings =
		"{\n  \"schema\": \"pd.weapon_settings.v1\",\n"
		"  \"fire_cadence\": { \"value\": 12, \"unit\": \"centiseconds\" }\n}\n";
	asset_archive_clone_override_t ov{};
	ov.entry = "behavior/settings.json";
	ov.data = settings.data();
	ov.len = static_cast<u32>(settings.size());
	AuthorRequest second(authorExample(kWeaponTemplate), root.path, "Rivet Carbine");
	second.request.version = "1.1.0";
	second.request.overrides = &ov;
	second.request.override_count = 1;
	const AuthorCall v2 = authorSave(second.request);
	INFO("v2 error: " << v2.err);
	REQUIRE(v2.code == MOD_AUTHOR_OK);
	CHECK(v2.result->updated == 1);
	CHECK(std::string(v2.result->archive_path) == std::string(v1.result->archive_path));
	CHECK(authorTree(root.path) == tree);
	authorRequireReleaseValid(v2.result->archive_path);
	const std::string archiveV2 = authorReadFile(v2.result->archive_path);
	CHECK(archiveV2 != archiveV1);
	CHECK(authorEntry(archiveV2, "behavior/settings.json") == settings);
	CHECK(authorManifestField(authorReadFile(v2.result->manifest_path), "version") == "1.1.0");

	/* Edit the saved mod itself: its own archive is the template, so nothing
	 * is renamed and the previous edit is kept. */
	const std::string variables =
		authorEntry(archiveV2, "behavior/variables.json") + "\n";
	asset_archive_clone_override_t edit{};
	edit.entry = "behavior/variables.json";
	edit.data = variables.data();
	edit.len = static_cast<u32>(variables.size());
	AuthorRequest third(v2.result->archive_path, root.path, "Rivet Carbine");
	third.request.version = "1.2.0";
	third.request.overrides = &edit;
	third.request.override_count = 1;
	const AuthorCall v3 = authorSave(third.request);
	INFO("v3 error: " << v3.err);
	REQUIRE(v3.code == MOD_AUTHOR_OK);
	CHECK(v3.result->updated == 1);
	CHECK(authorTree(root.path) == tree);
	authorRequireReleaseValid(v3.result->archive_path);
	const std::string archiveV3 = authorReadFile(v3.result->archive_path);
	CHECK(authorEntry(archiveV3, "behavior/settings.json") == settings);
	CHECK(authorEntry(archiveV3, "behavior/variables.json") == variables);
	CHECK(authorIniValue(authorEntry(archiveV3, "weapon.ini"), "catalog_id")
		== "mod:weapon_rivet_carbine");
	for (u32 i = 0; i < v3.result->ids.id_count; i++) {
		CHECK(std::string(v3.result->ids.ids[i].old_id)
			== std::string(v3.result->ids.ids[i].new_id));
	}
	CHECK(authorManifestField(authorReadFile(v3.result->manifest_path), "version") == "1.2.0");

	/* A failed update leaves the working mod exactly as it was. */
	const std::string manifestV3 = authorReadFile(v3.result->manifest_path);
	asset_archive_clone_override_t stray{};
	stray.entry = "dependencies/assets/models/missing.pdmesh::mesh.ini";
	stray.data = "x";
	stray.len = 1;
	asset_archive_clone_override_t gone{};
	gone.entry = "behavior/settings.json";
	gone.remove = 1;
	for (const asset_archive_clone_override_t *bad : { &stray, &gone }) {
		AuthorRequest fourth(authorExample(kWeaponTemplate), root.path, "Rivet Carbine");
		fourth.request.version = "9.9.9";
		fourth.request.overrides = bad;
		fourth.request.override_count = 1;
		const AuthorCall failed = authorSave(fourth.request);
		INFO("override " << bad->entry << " -> code " << failed.code << " " << failed.err);
		CHECK(failed.code == (bad == &stray ? MOD_AUTHOR_ERR_ARGS : MOD_AUTHOR_ERR_ARCHIVE));
		CHECK(!failed.err.empty());
		CHECK(authorResultIsZero(*failed.result));
		CHECK(authorTree(root.path) == tree);
		CHECK(authorReadFile(v3.result->archive_path) == archiveV3);
		CHECK(authorReadFile(v3.result->manifest_path) == manifestV3);
	}
}

TEST_CASE("mod author can require a new destination folder", "[modding][author]") {
	AuthorTempDir root;
	AuthorRequest req(authorExample(kWeaponTemplate), root.path, "Rivet Carbine");
	req.request.require_new_folder = 1;
	const AuthorCall freePlan = authorPlan(req.request);
	REQUIRE(freePlan.code == MOD_AUTHOR_OK);
	CHECK(freePlan.result->updated == 0);
	CHECK(authorTree(root.path).empty());

	const auto expectOccupied = [&]() {
		const auto before = authorTree(root.path);
		const AuthorCall refusedPlan = authorPlan(req.request);
		CHECK(refusedPlan.code == MOD_AUTHOR_ERR_EXISTS);
		CHECK(refusedPlan.err.find("Choose a different name") != std::string::npos);
		CHECK(authorResultIsZero(*refusedPlan.result));
		const AuthorCall refusedSave = authorSave(req.request);
		CHECK(refusedSave.code == MOD_AUTHOR_ERR_EXISTS);
		CHECK(refusedSave.err.find("Choose a different name") != std::string::npos);
		CHECK(authorResultIsZero(*refusedSave.result));
		CHECK(authorTree(root.path) == before);
	};

	SECTION("same-name saves preserve the archive, manifest and staging files") {
		const AuthorCall saved = authorSave(req.request);
		INFO(saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		CHECK(saved.result->updated == 0);
		CHECK(std::string(saved.result->mod_dir) == freePlan.result->mod_dir);
		const std::string archive = authorReadFile(saved.result->archive_path);
		const std::string manifest = authorReadFile(saved.result->manifest_path);
		const fs::path candidate = std::string(saved.result->archive_path) + ASSET_ARCHIVE_CLONE_STAGING_SUFFIX;
		const fs::path manifestCandidate = std::string(saved.result->manifest_path) + ASSET_ARCHIVE_CLONE_STAGING_SUFFIX;
		const fs::path marker = fs::path(saved.result->mod_dir) / "keep.txt";
		authorWriteFile(candidate, "archive candidate");
		authorWriteFile(manifestCandidate, "manifest candidate");
		authorWriteFile(marker, "folder marker");
		req.request.version = "2.0.0";
		expectOccupied();
		CHECK(authorReadFile(saved.result->archive_path) == archive);
		CHECK(authorReadFile(saved.result->manifest_path) == manifest);
		CHECK(authorReadFile(candidate) == "archive candidate");
		CHECK(authorReadFile(manifestCandidate) == "manifest candidate");
		CHECK(authorReadFile(marker) == "folder marker");
	}

	SECTION("an empty directory created after planning is already occupied") {
		const fs::path modDir = freePlan.result->mod_dir;
		REQUIRE(fs::create_directories(modDir));
		expectOccupied();
		CHECK(fs::is_empty(modDir));
	}
}

TEST_CASE("mod author never touches a folder that belongs to something else",
          "[modding][author]") {
	AuthorTempDir root;
	const std::string tmpl = authorExample(kWeaponTemplate);
	const fs::path modDir = root.path / "Weapons" / "weapon_rivet_carbine";
	const fs::path manifestPath = modDir / "mod.json";
	const fs::path archivePath = modDir / "weapon_rivet_carbine.pdweapon";

	const auto expectExists = [&](const char *what) {
		const std::vector<std::string> before = authorTree(root.path);
		AuthorRequest req(tmpl, root.path, "Rivet Carbine");
		const AuthorCall saved = authorSave(req.request);
		INFO(what << " -> " << saved.err);
		CHECK(saved.code == MOD_AUTHOR_ERR_EXISTS);
		CHECK(!saved.err.empty());
		CHECK(authorResultIsZero(*saved.result));
		const AuthorCall plan = authorPlan(req.request);
		CHECK(plan.code == MOD_AUTHOR_ERR_EXISTS);
		CHECK(authorResultIsZero(*plan.result));
		CHECK(authorTree(root.path) == before);
	};

	/* Somebody else's mod, by id. */
	const std::string foreign =
		"{ \"id\": \"mod.somebody_else\", \"name\": \"Theirs\", \"version\": \"2.0.0\" }\n";
	authorWriteFile(manifestPath, foreign);
	expectExists("foreign id");
	CHECK(authorReadFile(manifestPath) == foreign);

	/* A mod.json that cannot be read strictly is not ours to replace. */
	for (const char *unreadable : { "not json at all", "\xef\xbb\xbf{ \"id\": \"mod.weapon_rivet_carbine\" }",
			"{ \"id\": 7, \"name\": \"x\", \"version\": \"1\" }", "[]",
			"{ \"id\": \"mod.weapon_rivet_carbine\", \"id\": \"mod.weapon_rivet_carbine\" }" }) {
		authorWriteFile(manifestPath, unreadable);
		expectExists("unreadable manifest");
		CHECK(authorReadFile(manifestPath) == unreadable);
	}
	fs::remove(manifestPath);

	/* An archive with no mod.json beside it is somebody's loose file. */
	authorWriteFile(archivePath, "loose archive");
	expectExists("archive without mod.json");
	CHECK(authorReadFile(archivePath) == "loose archive");
	fs::remove(archivePath);

	/* Leftovers of an interrupted save of this same mod are replaced. */
	authorWriteFile(modDir / "weapon_rivet_carbine.pdweapon.candidate", "stale");
	authorWriteFile(modDir / "mod.json.candidate", "stale");
	{
		AuthorRequest req(tmpl, root.path, "Rivet Carbine");
		const AuthorCall saved = authorSave(req.request);
		INFO("save over leftovers: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		CHECK(saved.result->updated == 0);
		const std::vector<std::string> expected = {
			"Weapons/",
			"Weapons/weapon_rivet_carbine/",
			"Weapons/weapon_rivet_carbine/mod.json",
			"Weapons/weapon_rivet_carbine/weapon_rivet_carbine.pdweapon",
		};
		CHECK(authorTree(root.path) == expected);
		authorRequireReleaseValid(saved.result->archive_path);
	}

	/* Our own mod, recognised by its id whatever else its mod.json says. */
	authorWriteFile(manifestPath,
		"{ \"version\": \"0.0.1\", \"name\": \"Renamed by hand\", "
		"\"id\": \"mod.weapon_rivet_carbine\", \"extra\": [1, 2, 3] }\n");
	{
		AuthorRequest req(tmpl, root.path, "Rivet Carbine");
		const AuthorCall saved = authorSave(req.request);
		INFO("update by id: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		CHECK(saved.result->updated == 1);
		CHECK(authorManifestField(authorReadFile(manifestPath), "name") == "Rivet Carbine");
	}
}

TEST_CASE("mod author reports unusable templates and folders without leaving anything behind",
          "[modding][author]") {
	AuthorTempDir root;
	AuthorTempDir work;

	const auto expectCode = [&](const mod_author_request_t &request, s32 expected,
			const char *what) {
		const AuthorCall saved = authorSave(request);
		INFO(what << " -> " << saved.err);
		CHECK(saved.code == expected);
		CHECK(!saved.err.empty());
		CHECK(authorResultIsZero(*saved.result));
		CHECK(authorTree(root.path).empty());
	};

	/* Families with no creator yet, and things that are not typed archives. */
	for (const char *relative : { "examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh",
			"examples/modding/typed-pdxxx-basic/projectiles/tri_projectile.pdprojectile",
			"examples/modding/typed-pdxxx-basic/mod.json" }) {
		AuthorRequest req(authorExample(relative), root.path, "Anything");
		expectCode(req.request, MOD_AUTHOR_ERR_TEMPLATE, relative);
		CHECK(authorPlan(req.request).code == MOD_AUTHOR_ERR_TEMPLATE);
	}

	/* A template that is missing, or is not a release-valid archive. */
	{
		AuthorRequest missing((work.path / "missing.pdweapon").string(), root.path, "Anything");
		expectCode(missing.request, MOD_AUTHOR_ERR_TEMPLATE, "missing template");

		const fs::path junk = work.path / "junk.pdweapon";
		authorWriteFile(junk, "this is not a zip archive");
		AuthorRequest bad(junk.string(), root.path, "Anything");
		expectCode(bad.request, MOD_AUTHOR_ERR_TEMPLATE, "junk template");
	}

	/* An override that belongs to nothing in the template. */
	{
		asset_archive_clone_override_t stray{};
		stray.entry = "dependencies/assets/models/missing.pdmesh::mesh.ini";
		stray.data = "x";
		stray.len = 1;
		AuthorRequest req(authorExample(kWeaponTemplate), root.path, "Anything");
		req.request.overrides = &stray;
		req.request.override_count = 1;
		const AuthorCall saved = authorSave(req.request);
		INFO("stray override -> " << saved.err);
		CHECK(saved.code != MOD_AUTHOR_OK);
		CHECK(saved.code != MOD_AUTHOR_ERR_EXISTS);
		CHECK(!saved.err.empty());
		CHECK(authorResultIsZero(*saved.result));
		/* The category and mod folders this call created are gone again. */
		CHECK(authorTree(root.path).empty());
	}

	/* The mods folder itself must already exist; it is never created. */
	{
		const fs::path absent = work.path / "no_such_mods_folder";
		AuthorRequest req(authorExample(kWeaponTemplate), absent, "Anything");
		const AuthorCall saved = authorSave(req.request);
		CHECK(saved.code == MOD_AUTHOR_ERR_WRITE);
		CHECK(!fs::exists(absent));
		/* Planning against it is fine: there is simply no mod there yet. */
		const AuthorCall plan = authorPlan(req.request);
		CHECK(plan.code == MOD_AUTHOR_OK);
		CHECK(plan.result->updated == 0);
	}

	/* A category folder that already exists is used and kept on failure. */
	{
		fs::create_directories(root.path / "Weapons");
		AuthorRequest missing((work.path / "missing.pdweapon").string(), root.path, "Anything");
		const AuthorCall saved = authorSave(missing.request);
		CHECK(saved.code == MOD_AUTHOR_ERR_TEMPLATE);
		const std::vector<std::string> expected = { "Weapons/" };
		CHECK(authorTree(root.path) == expected);
	}
}

TEST_CASE("mod author sets and removes descriptor keys the creator asks for",
          "[modding][author]") {
	AuthorTempDir work;
	AuthorTempDir root;
	char err[256] = {};

	/* The example weapon with one extra descriptor line to remove. */
	const std::string source = authorExample(kWeaponTemplate);
	std::string seeded = authorEntry(authorReadFile(source), "weapon.ini");
	const std::string idLine = "catalog_id = example:tri_weapon\n";
	REQUIRE(seeded.find(idLine) != std::string::npos);
	seeded.insert(seeded.find(idLine) + idLine.size(), "scratch_note = remove me\n");
	const std::string tmpl = (work.path / "seeded.pdweapon").string();
	{
		asset_archive_clone_override_t ov{};
		ov.entry = "weapon.ini";
		ov.data = seeded.data();
		ov.len = static_cast<u32>(seeded.size());
		REQUIRE(assetArchiveRebuildFile(source.c_str(), tmpl.c_str(), "example:tri_weapon",
			"pd-tests", &ov, 1, nullptr, err, sizeof(err)) == ASSET_ARCHIVE_CLONE_OK);
	}

	/* View position (new keys), dual wield (an existing key), and a key the
	 * creator no longer wants. */
	const mod_author_descriptor_value_t values[] = {
		{ "posx", "30.0" },
		{ "posy", "-18.0" },
		{ "posz", "-45.0" },
		{ "muzzlez", "3.0" },
		{ "dual_wieldable", "true" },
		{ "scratch_note", nullptr },
	};
	AuthorRequest req(tmpl, root.path, "Placed Gun");
	req.request.descriptor_values = values;
	req.request.descriptor_value_count = 6;
	const AuthorCall saved = authorSave(req.request);
	INFO("save error: " << saved.err);
	REQUIRE(saved.code == MOD_AUTHOR_OK);
	authorRequireReleaseValid(saved.result->archive_path);

	const std::string written =
		authorEntry(authorReadFile(saved.result->archive_path), "weapon.ini");
	CHECK(authorIniValue(written, "posx") == "30.0");
	CHECK(authorIniValue(written, "posy") == "-18.0");
	CHECK(authorIniValue(written, "posz") == "-45.0");
	CHECK(authorIniValue(written, "muzzlez") == "3.0");
	CHECK(authorIniValue(written, "dual_wieldable") == "true");
	CHECK(authorIniValues(written, "scratch_note").empty());
	CHECK(authorIniValue(written, "name") == "Placed Gun");
	CHECK(authorIniValue(written, "catalog_id") == "mod:weapon_placed_gun");
	/* New keys sit together at the end of the [weapon] section, in the order
	 * given, and the [meta] section is untouched. */
	CHECK(written.find("posx = 30.0\nposy = -18.0\nposz = -45.0\nmuzzlez = 3.0\n\n[meta]\n")
		!= std::string::npos);
	CHECK(authorIniValue(written, "manifest") == "_meta/manifest.json");
	CHECK(std::count(written.begin(), written.end(), '\n')
		== std::count(seeded.begin(), seeded.end(), '\n') + 3);
	authorKeepArtifact(saved.result->archive_path);

	/* Refusals: nothing is written for any of them. */
	const std::vector<std::string> before = authorTree(root.path);
	const auto expectRefused = [&](const mod_author_descriptor_value_t *list, u32 count,
			const char *what) {
		AuthorRequest bad(tmpl, root.path, "Refused Gun");
		bad.request.descriptor_values = list;
		bad.request.descriptor_value_count = count;
		const AuthorCall call = authorSave(bad.request);
		INFO(what << " -> " << call.err);
		CHECK(call.code == MOD_AUTHOR_ERR_ARGS);
		CHECK(!call.err.empty());
		CHECK(authorTree(root.path) == before);
	};
	{
		const mod_author_descriptor_value_t v[] = { { "catalog_id", "mod:other" } };
		expectRefused(v, 1, "catalog_id");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "name", "Sneaky" } };
		expectRefused(v, 1, "the name key");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "pos x", "1" } };
		expectRefused(v, 1, "a key with a space");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "", "1" } };
		expectRefused(v, 1, "an empty key");
	}
	{
		const mod_author_descriptor_value_t v[] = { { nullptr, "1" } };
		expectRefused(v, 1, "a missing key");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "posx", "1" }, { "posx", "2" } };
		expectRefused(v, 2, "a key given twice");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "posx", "1\nposy = 2" } };
		expectRefused(v, 1, "a value with a line break");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "posx", " 1 " } };
		expectRefused(v, 1, "a padded value");
	}
	{
		const mod_author_descriptor_value_t v[] = { { "posx", "" } };
		expectRefused(v, 1, "an empty value");
	}
	expectRefused(nullptr, 1, "a count with no list");
	/* A key the archive's manifest repeats: the rebuild changes the manifest's
	 * copy with the descriptor's (test_asset_archive_clone.cpp, and the Maian
	 * hover vehicle recipe changes model_file this way), but it cannot drop
	 * it, so removing such a key would leave the two disagreeing. */
	{
		const mod_author_descriptor_value_t v[] = { { "presentation_file", nullptr } };
		expectRefused(v, 1, "a key mirrored in the manifest, removed");
	}
	{
		/* Changed to a member the archive does not have: release validation
		 * refuses the result and nothing is written. */
		const mod_author_descriptor_value_t v[] = { { "model_file", "elsewhere.pdmesh" } };
		AuthorRequest bad(tmpl, root.path, "Refused Gun");
		bad.request.descriptor_values = v;
		bad.request.descriptor_value_count = 1;
		const AuthorCall call = authorSave(bad.request);
		INFO("a key mirrored in the manifest, changed to a missing member -> " << call.err);
		CHECK(call.code == MOD_AUTHOR_ERR_ARCHIVE);
		CHECK(call.err.find("elsewhere.pdmesh") != std::string::npos);
		CHECK(authorTree(root.path) == before);
	}

	/* Descriptor values and a descriptor override cannot both be given. */
	{
		const mod_author_descriptor_value_t v[] = { { "posx", "1" } };
		asset_archive_clone_override_t ov{};
		ov.entry = "weapon.ini";
		ov.data = seeded.data();
		ov.len = static_cast<u32>(seeded.size());
		AuthorRequest both(tmpl, root.path, "Refused Gun");
		both.request.descriptor_values = v;
		both.request.descriptor_value_count = 1;
		both.request.overrides = &ov;
		both.request.override_count = 1;
		const AuthorCall call = authorSave(both.request);
		CHECK(call.code == MOD_AUTHOR_ERR_ARGS);
		CHECK(authorTree(root.path) == before);
	}
}

/* What the Characters tab (C1, maian-vehicle-and-veteran.md) will edit: a
 * body's or head's gender (ismale with type, the rig derived from the type
 * once rig_class is removed) and height as descriptor values, and a
 * character's gender inside its own embedded body through a nested override.
 * Read back with the game's strict body and character readers. */
TEST_CASE("mod author sets the gender, type and height a Characters tab edits",
          "[modding][author][characters-tab]") {
	AuthorTempDir root;
	char err[160] = {};

	SECTION("body: male and Maian, its rig from its type, its height") {
		const mod_author_descriptor_value_t values[] = {
			{ "ismale", "1" },
			{ "type", "HEADBODYTYPE_MAIAN" },
			{ "height", "106" },
			{ "rig_class", nullptr },
		};
		AuthorRequest req(authorExample(kBodyTemplate), root.path, "Veteran Harness");
		req.request.descriptor_values = values;
		req.request.descriptor_value_count = 4;
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);

		const std::string archive = authorReadFile(saved.result->archive_path);
		auto source = std::make_unique<body_head_source_t>();
		const s32 parsed = bodyHeadSourceParseArchiveBytes(archive.data(),
			static_cast<u32>(archive.size()), saved.result->catalog_id, 0, source.get(),
			err, sizeof(err));
		INFO("body reader: " << err);
		REQUIRE(parsed == 1);
		CHECK(source->ismale == 1);
		CHECK(source->type == HEADBODYTYPE_MAIAN);
		CHECK(source->height == 106);
		CHECK(std::string(source->rig_class) == "maian_tall_neck");
		CHECK(std::string(source->display_name) == "Veteran Harness");
		authorKeepArtifact(saved.result->archive_path);
	}

	SECTION("the Characters tool's own path: read the template, set the form, save, read back") {
		/* What the tool does for "Use as Template": the game's body reader on
		 * the template archive gives the template's gender, type and height. */
		const std::string tmplPath = authorExample(kBodyTemplate);
		const std::string tmplBytes = authorReadFile(tmplPath);
		auto tmplSource = std::make_unique<body_head_source_t>();
		REQUIRE(bodyHeadSourceParseArchiveBytes(tmplBytes.data(),
			static_cast<u32>(tmplBytes.size()), "example:tri_body", 0, tmplSource.get(),
			err, sizeof(err)) == 1);
		character_tool_template_t tmpl{};
		tmpl.kind = characterToolKindForAssetType(ASSET_BODY);
		tmpl.ismale = tmplSource->ismale ? 1 : 0;
		tmpl.type = tmplSource->type;
		tmpl.height = tmplSource->height;
		/* The example body names no gender, type or height: the reader's
		 * defaults, and a rig_class of its own. */
		CHECK(tmpl.ismale == 0);
		CHECK(tmpl.type == HEADBODYTYPE_DEFAULT);
		CHECK(tmpl.height == 0);
		CHECK(std::string(tmplSource->rig_class) == "human_male_neck_standard");

		character_tool_form_t form;
		characterToolFormFromTemplate(&tmpl, &form);
		CHECK(form.build == CHARACTER_TOOL_BUILD_TEMPLATE); /* female with DEFAULT: not a build */

		/* Saved as it is, the tool sends nothing and the copy keeps the
		 * template's own neck. */
		character_tool_values_t values;
		REQUIRE(characterToolDescriptorValues(&form, &tmpl, &values) == 1);
		CHECK(values.count == 0);
		{
			AuthorRequest req(tmplPath, root.path, "Plain Copy");
			const AuthorCall saved = authorSave(req.request);
			INFO("save error: " << saved.err);
			REQUIRE(saved.code == MOD_AUTHOR_OK);
			const std::string archive = authorReadFile(saved.result->archive_path);
			auto source = std::make_unique<body_head_source_t>();
			REQUIRE(bodyHeadSourceParseArchiveBytes(archive.data(),
				static_cast<u32>(archive.size()), saved.result->catalog_id, 0, source.get(),
				err, sizeof(err)) == 1);
			CHECK(source->ismale == 0);
			CHECK(source->type == HEADBODYTYPE_DEFAULT);
			CHECK(std::string(source->rig_class) == "human_male_neck_standard");
		}

		/* A male Maian of height 106: the tool's values, straight to the author. */
		form.ismale = 1;
		form.build = CHARACTER_TOOL_BUILD_MAIAN;
		form.height = 106;
		REQUIRE(characterToolDescriptorValues(&form, &tmpl, &values) == 1);
		REQUIRE(values.count == 4);
		AuthorRequest req(tmplPath, root.path, "Tool Veteran");
		req.request.descriptor_values = values.values;
		req.request.descriptor_value_count = values.count;
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);
		CHECK(std::string(saved.result->catalog_id) == "mod:body_tool_veteran");

		const std::string archive = authorReadFile(saved.result->archive_path);
		auto source = std::make_unique<body_head_source_t>();
		const s32 parsed = bodyHeadSourceParseArchiveBytes(archive.data(),
			static_cast<u32>(archive.size()), saved.result->catalog_id, 0, source.get(),
			err, sizeof(err));
		INFO("body reader: " << err);
		REQUIRE(parsed == 1);
		CHECK(source->ismale == 1);
		CHECK(source->type == HEADBODYTYPE_MAIAN);
		CHECK(source->height == 106);
		CHECK(std::string(source->rig_class) == "maian_tall_neck");
		CHECK(std::string(source->display_name) == "Tool Veteran");
		authorKeepArtifact(saved.result->archive_path);

		/* Opened again as a template, the saved mod reads back as the build
		 * the player picked. */
		character_tool_template_t again{};
		again.kind = tmpl.kind;
		again.ismale = source->ismale ? 1 : 0;
		again.type = source->type;
		again.height = source->height;
		characterToolFormFromTemplate(&again, &form);
		CHECK(form.ismale == 1);
		CHECK(form.build == CHARACTER_TOOL_BUILD_MAIAN);
		CHECK(form.height == 106);
	}

	SECTION("head: female, the female rig") {
		const mod_author_descriptor_value_t values[] = {
			{ "ismale", "0" },
			{ "type", "HEADBODYTYPE_FEMALE" },
			{ "rig_class", nullptr },
		};
		AuthorRequest req(authorExample(kHeadTemplate), root.path, "Veteran Face");
		req.request.descriptor_values = values;
		req.request.descriptor_value_count = 3;
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);

		const std::string archive = authorReadFile(saved.result->archive_path);
		auto source = std::make_unique<body_head_source_t>();
		const s32 parsed = bodyHeadSourceParseArchiveBytes(archive.data(),
			static_cast<u32>(archive.size()), saved.result->catalog_id, 1, source.get(),
			err, sizeof(err));
		INFO("head reader: " << err);
		REQUIRE(parsed == 1);
		CHECK(source->ismale == 0);
		CHECK(source->type == HEADBODYTYPE_FEMALE);
		CHECK(std::string(source->rig_class) == "human_female_neck_standard");
		CHECK(std::string(source->display_name) == "Veteran Face");
		authorKeepArtifact(saved.result->archive_path);
	}

	SECTION("character: its gender lives in its own body, changed through a nested override") {
		const std::string tmpl = authorExample(kCharacterTemplate);
		const std::string bodyMember = "dependencies/assets/body/tri_body.pdbody";
		std::string bodyIni = authorEntry(authorEntry(authorReadFile(tmpl), bodyMember), "body.ini");
		const std::string rigLine = "rig_class = human_male_neck_standard\n";
		REQUIRE(bodyIni.find(rigLine) != std::string::npos);
		bodyIni.replace(bodyIni.find(rigLine), rigLine.size(),
			"ismale = 1\ntype = HEADBODYTYPE_MAIAN\n");
		const std::string overrideEntry = bodyMember + "::body.ini";
		asset_archive_clone_override_t ov{};
		ov.entry = overrideEntry.c_str();
		ov.data = bodyIni.data();
		ov.len = static_cast<u32>(bodyIni.size());

		AuthorRequest req(tmpl, root.path, "Maian Veteran");
		req.request.overrides = &ov;
		req.request.override_count = 1;
		const AuthorCall saved = authorSave(req.request);
		INFO("save error: " << saved.err);
		REQUIRE(saved.code == MOD_AUTHOR_OK);
		authorRequireReleaseValid(saved.result->archive_path);
		CHECK(std::string(saved.result->catalog_id) == "mod:character_maian_veteran");

		/* The embedded body carries the mod's id for it (the override's
		 * example id rewritten like the rest) and the new gender and type. */
		const std::string archive = authorReadFile(saved.result->archive_path);
		const std::string body = authorEntry(archive, bodyMember);
		const std::string bodyId = std::string(saved.result->catalog_id) + "__body_tri_body";
		auto source = std::make_unique<body_head_source_t>();
		const s32 parsed = bodyHeadSourceParseArchiveBytes(body.data(),
			static_cast<u32>(body.size()), bodyId.c_str(), 0, source.get(), err, sizeof(err));
		INFO("embedded body reader: " << err);
		REQUIRE(parsed == 1);
		CHECK(source->ismale == 1);
		CHECK(source->type == HEADBODYTYPE_MAIAN);
		CHECK(std::string(source->rig_class) == "maian_tall_neck");

		/* The character still names that body, with its fixed head. */
		const std::string descriptor = authorEntry(archive, "character.ini");
		auto ini = std::make_unique<ini_section_t>();
		REQUIRE(bodyHeadSourceReadIniBytes(descriptor.data(), static_cast<u32>(descriptor.size()),
			"character", ini.get(), err, sizeof(err)) == 1);
		auto entry = std::make_unique<asset_entry_t>();
		std::memset(entry.get(), 0, sizeof(asset_entry_t));
		entry->type = ASSET_CHARACTER;
		std::strcpy(entry->id, saved.result->catalog_id);
		REQUIRE(characterSourceApplyIni(entry.get(), ini.get()) == 1);
		CHECK(std::string(entry->ext.character.body_id) == bodyId);
		CHECK(std::string(entry->ext.character.display_name) == "Maian Veteran");
		CHECK(characterSourcePolicy(entry.get()) == CHARACTER_HEAD_POLICY_FIXED);
		authorKeepArtifact(saved.result->archive_path);
	}
}

TEST_CASE("mod author names a save without looking at the mods folder",
          "[modding][author]") {
	mod_author_request_t request{};
	request.template_path = "anything.pdweapon";
	request.mods_root = "no/such/folder";
	request.display_name = "  \tMaian  Crystal-Pistol!\t  ";
	auto names = std::make_unique<mod_author_result_t>();
	char err[256] = "";
	REQUIRE(modAuthorNames(&request, names.get(), err, sizeof(err)) == MOD_AUTHOR_OK);
	CHECK(std::string(names->display_name) == "Maian  Crystal-Pistol!");
	CHECK(std::string(names->kind) == "weapon");
	CHECK(std::string(names->slug) == "weapon_maian_crystal_pistol");
	CHECK(std::string(names->mod_id) == "mod.weapon_maian_crystal_pistol");
	CHECK(std::string(names->catalog_id) == "mod:weapon_maian_crystal_pistol");
	CHECK(std::string(names->archive_path) ==
		"no/such/folder/Weapons/weapon_maian_crystal_pistol/weapon_maian_crystal_pistol.pdweapon");

	/* Planning the same save for a real folder gives the same names. */
	AuthorTempDir tmp;
	const std::string root = tmp.path.string();
	request.mods_root = root.c_str();
	auto plan = std::make_unique<mod_author_result_t>();
	REQUIRE(modAuthorPlan(&request, plan.get(), err, sizeof(err)) == MOD_AUTHOR_OK);
	CHECK(std::string(plan->display_name) == names->display_name);
	CHECK(std::string(plan->slug) == names->slug);
	CHECK(std::string(plan->mod_id) == names->mod_id);
	CHECK(std::string(plan->catalog_id) == names->catalog_id);

	request.display_name = "!!!";
	CHECK(modAuthorNames(&request, names.get(), err, sizeof(err)) == MOD_AUTHOR_ERR_ARGS);
	CHECK(names->display_name[0] == '\0');
	CHECK(names->slug[0] == '\0');
	CHECK(err[0] != '\0');
	request.display_name = "Fine";
	request.template_path = "thing.pdunknown";
	CHECK(modAuthorNames(&request, names.get(), err, sizeof(err)) == MOD_AUTHOR_ERR_TEMPLATE);
	CHECK(modAuthorNames(&request, nullptr, err, sizeof(err)) == MOD_AUTHOR_ERR_ARGS);
	CHECK(modAuthorNames(nullptr, names.get(), nullptr, 0) == MOD_AUTHOR_ERR_ARGS);
}

/*
 * Local sweep, off unless PD2_BASE_TEMPLATE_DIR names an extracted base data
 * folder (the one holding weapons/, characters/, bodies/, ...). Every base
 * archive of a family the author supports is saved as a mod; the result must
 * be release-valid. The per-archive outcome goes to
 * <PD2_TEST_ARTIFACT_DIR>/base_template_sweep.txt when that is set.
 *
 * The base data is read only. Extracted assets never enter the repository or
 * CI, so this case does nothing there.
 */
TEST_CASE("mod author sweep over base templates (local, needs PD2_BASE_TEMPLATE_DIR)",
          "[modding][author][base-sweep]") {
	const char *base = std::getenv("PD2_BASE_TEMPLATE_DIR");
	if (!base || !base[0] || !fs::is_directory(base)) {
		SUCCEED("PD2_BASE_TEMPLATE_DIR not set; sweep skipped");
		return;
	}

	struct Family { const char *dir; const char *ext; };
	const Family families[] = {
		{ "weapons", ".pdweapon" }, { "vehicles", ".pdvehicle" },
		{ "characters", ".pdcharacter" }, { "bodies", ".pdbody" },
		{ "heads", ".pdhead" }, { "arenas", ".pdarena" },
	};

	std::string report;
	size_t total = 0;
	size_t saved = 0;
	for (const Family &family : families) {
		const fs::path dir = fs::path(base) / family.dir;
		if (!fs::is_directory(dir)) {
			report += std::string(family.dir) + "\t(missing folder)\n";
			continue;
		}
		std::vector<fs::path> archives;
		for (const auto &entry : fs::directory_iterator(dir)) {
			if (entry.is_regular_file() && entry.path().extension() == family.ext) {
				archives.push_back(entry.path());
			}
		}
		std::sort(archives.begin(), archives.end());

		size_t family_saved = 0;
		for (const fs::path &archive : archives) {
			AuthorTempDir root;   /* one scratch mods folder per archive */
			const std::string display = "Sweep " + archive.stem().string();
			AuthorRequest req(archive.string(), root.path, display.c_str());
			const AuthorCall call = authorSave(req.request);
			total++;
			std::string outcome;
			if (call.code == MOD_AUTHOR_OK) {
				char err[256] = {};
				const std::string bytes = authorReadFile(call.result->archive_path);
				const char *descriptorName =
					assetArchiveDescriptorForPath(call.result->archive_path);
				const std::string descriptor =
					descriptorName ? authorEntry(bytes, descriptorName) : std::string();
				const bool ownerKeysLeft = !authorIniValues(descriptor, "bundled").empty() ||
					!authorIniValues(descriptor, "requirefeature").empty() ||
					(std::string(family.ext) == ".pdweapon" &&
						!authorIniValues(descriptor, "category").empty());
				if (assetArchiveValidateBytes(bytes.data(), static_cast<u32>(bytes.size()),
						call.result->archive_path, ASSET_ARCHIVE_VALIDATE_RELEASE,
						err, sizeof(err)) != 0) {
					outcome = std::string("SAVED_BUT_INVALID\t") + err;
				} else if (ownerKeysLeft) {
					outcome = "SAVED_BUT_BASE_KEYS_LEFT";
				} else {
					saved++;
					family_saved++;
					outcome = "OK\tids=" + std::to_string(call.result->ids.id_count);
				}
			} else {
				outcome = "REFUSED(" + std::to_string(call.code) + ")\t" + call.err;
			}
			report += std::string(family.dir) + "\t" + archive.filename().string()
				+ "\t" + outcome + "\n";
		}
		report += std::string("# ") + family.dir + ": " + std::to_string(family_saved)
			+ " of " + std::to_string(archives.size()) + " saved\n";
	}
	report += "# total: " + std::to_string(saved) + " of " + std::to_string(total) + " saved\n";

	const char *artifacts = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (artifacts && artifacts[0]) {
		authorWriteFile(fs::path(artifacts) / "base_template_sweep.txt", report);
	}
	WARN("base template sweep: " << saved << " of " << total << " saved");
	/* A result that was written must always be valid; refusals are findings,
	 * not failures, and are listed in the report. */
	CHECK(report.find("SAVED_BUT_INVALID") == std::string::npos);
	CHECK(report.find("SAVED_BUT_BASE_KEYS_LEFT") == std::string::npos);
	CHECK(total > 0);
}
