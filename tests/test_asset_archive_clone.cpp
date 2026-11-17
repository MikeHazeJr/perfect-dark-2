/**
 * test_asset_archive_clone.cpp -- typed archive clone to a new catalog id.
 *
 * assetArchiveCloneFile() is the first step of every in-game creator: a
 * template archive becomes a mod-owned archive whose embedded typed archives
 * all live in the new asset's namespace. These cases clone the checked-in
 * example and dev-mod archives, then check the result with the same C
 * validators the game uses on load and on package.
 *
 * When PD2_TEST_ARTIFACT_DIR is set, the successful clones are also copied to
 * <dir>/clones so CI can run tools/asset_archive_conformance.py over them.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "modarchive.h"
#include "weapon_graph_archive.h"
}
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct CloneTempDir {
	std::filesystem::path path;

	CloneTempDir() {
		static unsigned counter = 0;
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = std::filesystem::temp_directory_path()
			/ ("pd2-archive-clone-" + std::to_string(stamp) + "-"
				+ std::to_string(counter++));
		std::filesystem::create_directories(path);
	}

	~CloneTempDir() {
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

std::string cloneSourcePath(const char *relative)
{
	return (std::filesystem::path(PD_SOURCE_DIR) / relative).string();
}

std::string cloneReadFileBytes(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}

s32 cloneCollectNameCb(const char *entryName, u32, void *user)
{
	static_cast<std::vector<std::string> *>(user)->push_back(entryName);
	return 0;
}

std::vector<std::string> cloneEntryNames(const std::string &archive)
{
	std::vector<std::string> names;
	REQUIRE(modArchiveMemForEachEntry(archive.data(), static_cast<u32>(archive.size()),
		cloneCollectNameCb, &names) == MODARCHIVE_OK);
	return names;
}

std::string cloneEntryBytes(const std::string &archive, const std::string &entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry.c_str(), &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

bool cloneHasSuffix(const std::string &text, const char *suffix)
{
	const size_t n = std::strlen(suffix);
	return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
}

bool cloneIsMetaEntry(const std::string &entry)
{
	return entry.rfind("_meta/", 0) == 0;
}

/* Flattened view of an archive tree: "outer.pdx::inner.pdy::member" -> bytes. */
void cloneFlatten(const std::string &archive, const std::string &label,
	std::map<std::string, std::string> &members)
{
	for (const std::string &entry : cloneEntryNames(archive)) {
		const std::string bytes = cloneEntryBytes(archive, entry);
		const std::string path = label.empty() ? entry : label + "::" + entry;
		members[path] = bytes;
		if (!cloneIsMetaEntry(entry) && assetArchivePathIsTyped(entry.c_str())) {
			cloneFlatten(bytes, path, members);
		}
	}
}

std::string cloneDescriptorId(const std::string &descriptor)
{
	const std::string key = "catalog_id";
	size_t at = 0;
	while (at < descriptor.size()) {
		size_t end = descriptor.find('\n', at);
		if (end == std::string::npos) end = descriptor.size();
		std::string line = descriptor.substr(at, end - at);
		at = end + 1;
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
		if (line.rfind(key, 0) != 0) continue;
		const size_t eq = line.find('=', key.size());
		if (eq == std::string::npos) continue;
		size_t value = eq + 1;
		while (value < line.size() && line[value] == ' ') value++;
		return line.substr(value);
	}
	return "";
}

/* Every catalog_id declared by a typed archive anywhere in the tree. */
std::vector<std::string> cloneDeclaredIds(const std::string &archive,
	const std::string &archiveName)
{
	std::vector<std::string> ids;
	const char *descriptor = assetArchiveDescriptorForPath(archiveName.c_str());
	REQUIRE(descriptor != nullptr);
	ids.push_back(cloneDescriptorId(cloneEntryBytes(archive, descriptor)));
	for (const std::string &entry : cloneEntryNames(archive)) {
		if (cloneIsMetaEntry(entry) || !assetArchivePathIsTyped(entry.c_str())) continue;
		const std::vector<std::string> nested =
			cloneDeclaredIds(cloneEntryBytes(archive, entry), entry);
		ids.insert(ids.end(), nested.begin(), nested.end());
	}
	return ids;
}

std::string cloneNewIdOf(const asset_archive_clone_report_t &report, const char *oldId)
{
	for (u32 i = 0; i < report.id_count; i++) {
		if (std::strcmp(report.ids[i].old_id, oldId) == 0) return report.ids[i].new_id;
	}
	return "";
}

void cloneKeepArtifact(const std::filesystem::path &archive)
{
	const char *root = std::getenv("PD2_TEST_ARTIFACT_DIR");
	if (!root || !root[0]) return;
	std::error_code ec;
	const std::filesystem::path dir = std::filesystem::path(root) / "clones";
	std::filesystem::create_directories(dir, ec);
	std::filesystem::copy_file(archive, dir / archive.filename(),
		std::filesystem::copy_options::overwrite_existing, ec);
}

/* Checks every clone must satisfy, whatever the family. */
void cloneCheckCommon(const std::string &sourcePath, const std::filesystem::path &outPath,
	const asset_archive_clone_report_t &report, const std::string &newNamespace)
{
	char err[256] = {};
	INFO("policy: " << err);
	REQUIRE(assetArchiveValidateFile(outPath.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	const std::string source = cloneReadFileBytes(sourcePath);
	const std::string clone = cloneReadFileBytes(outPath);
	REQUIRE(!source.empty());
	REQUIRE(!clone.empty());

	/* Every archive in the tree now declares an id in the new namespace, and
	 * the report maps each old declared id to exactly the id found. */
	const std::vector<std::string> oldIds = cloneDeclaredIds(source, sourcePath);
	const std::vector<std::string> newIds =
		cloneDeclaredIds(clone, outPath.string());
	REQUIRE(oldIds.size() == newIds.size());
	REQUIRE(report.archive_count == newIds.size());
	std::set<std::string> distinctOld(oldIds.begin(), oldIds.end());
	REQUIRE(report.id_count == distinctOld.size());
	for (size_t i = 0; i < oldIds.size(); i++) {
		INFO("declared id " << oldIds[i] << " -> " << newIds[i]);
		CHECK(newIds[i].rfind(newNamespace + ":", 0) == 0);
		CHECK(newIds[i].size() < CATALOG_ID_LEN);
		CHECK(cloneNewIdOf(report, oldIds[i].c_str()) == newIds[i]);
	}

	std::map<std::string, std::string> before;
	std::map<std::string, std::string> after;
	cloneFlatten(source, "", before);
	cloneFlatten(clone, "", after);

	for (const auto &member : before) {
		const std::string &path = member.first;
		const size_t leafAt = path.rfind("::");
		const std::string leaf =
			leafAt == std::string::npos ? path : path.substr(leafAt + 2);
		if (cloneIsMetaEntry(leaf)) continue;
		INFO("member " << path);

		/* Same public members, and anything that is not id-bearing text or an
		 * embedded archive is byte-identical. */
		REQUIRE(after.count(path) == 1);
		const bool text = cloneHasSuffix(leaf, ".ini") || cloneHasSuffix(leaf, ".json")
			|| cloneHasSuffix(leaf, ".gltf");
		if (!text && !assetArchivePathIsTyped(leaf.c_str())) {
			CHECK(after[path] == member.second);
		}
	}

	/* No old declared id survives in any member of the clone, except in the
	 * provenance records that name the template on purpose. */
	for (const auto &member : after) {
		if (cloneHasSuffix(member.first, "_meta/provenance.json")
				|| cloneHasSuffix(member.first, "_meta/source-handles.json")) continue;
		const size_t leafAt = member.first.rfind("::");
		const std::string leaf = leafAt == std::string::npos
			? member.first : member.first.substr(leafAt + 2);
		if (assetArchivePathIsTyped(leaf.c_str()) && !cloneIsMetaEntry(leaf)) continue;
		for (const std::string &oldId : distinctOld) {
			INFO("member " << member.first << " old id " << oldId);
			size_t at = member.second.find(oldId);
			while (at != std::string::npos) {
				const size_t end = at + oldId.size();
				const char next = end < member.second.size() ? member.second[end] : '"';
				const bool longerId = std::isalnum(static_cast<unsigned char>(next))
					|| next == '_' || next == '.' || next == '-';
				CHECK(longerId);
				at = member.second.find(oldId, at + 1);
			}
		}
	}

	cloneKeepArtifact(outPath);
}

} // namespace

TEST_CASE("typed archive clone re-identifies a weapon and its embedded archives",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon");
	const std::filesystem::path out = dir.path / "clone_tri_weapon.pdweapon";
	const char *newId = "mod_clonetest:rivet_carbine";

	asset_archive_clone_report_t report;
	char err[256] = {};
	const s32 code = assetArchiveCloneFile(source.c_str(), out.string().c_str(),
		newId, "pd-tests", &report, err, sizeof(err));
	INFO("clone error: " << err);
	REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
	REQUIRE(std::string(err).empty());

	/* Root, held mesh, reticle, projectile and deployed entity. */
	REQUIRE(report.id_count == 5);
	REQUIRE(report.archive_count == 5);
	REQUIRE(std::string(report.ids[0].old_id) == "example:tri_weapon");
	REQUIRE(std::string(report.ids[0].new_id) == newId);
	const std::string projectile = cloneNewIdOf(report, "example:tri_projectile");
	const std::string entity = cloneNewIdOf(report, "example:tri_entity");
	const std::string reticle = cloneNewIdOf(report, "example:tri_reticle");
	const std::string mesh = cloneNewIdOf(report, "example:tri_mesh");
	CHECK(projectile == "mod_clonetest:rivet_carbine__projectile_tri_projectile");
	CHECK(entity == "mod_clonetest:rivet_carbine__entity_tri_entity");
	CHECK(reticle == "mod_clonetest:rivet_carbine__ui_tri_reticle");
	CHECK(mesh == "mod_clonetest:rivet_carbine__mesh_tri_mesh");

	cloneCheckCommon(source, out, report, "mod_clonetest");

	/* The weapon-specific loader checks accept the clone. */
	REQUIRE(weaponGraphArchiveValidateRootFile(out.string().c_str(), ASSET_WEAPON,
		err, sizeof(err)) == 0);
	weapon_graph_archive_descriptor_t descriptor;
	REQUIRE(weaponGraphArchiveReadDescriptorFile(out.string().c_str(), ASSET_WEAPON,
		&descriptor, err, sizeof(err)) == 0);
	CHECK(std::string(descriptor.catalog_id) == newId);
	CHECK(std::string(descriptor.primary_graph) == "behavior/primary.graph.json");
	CHECK(std::string(descriptor.secondary_graph) == "behavior/secondary.graph.json");
	CHECK(std::string(descriptor.model_file) == "dependencies/assets/models/weapon.pdmesh");

	/* References follow the embedded archives; references to assets that
	 * are not embedded are left alone. */
	const std::string clone = cloneReadFileBytes(out);
	const std::string primary = cloneEntryBytes(clone, "behavior/primary.graph.json");
	CHECK(primary.find("\"asset_id\": \"" + std::string(newId) + "\"") != std::string::npos);
	CHECK(primary.find(projectile) != std::string::npos);
	const std::string secondary = cloneEntryBytes(clone, "behavior/secondary.graph.json");
	CHECK(secondary.find(entity) != std::string::npos);
	const std::string presentation = cloneEntryBytes(clone, "bindings/presentation.json");
	CHECK(presentation.find(reticle) != std::string::npos);
	const std::string manifest = cloneEntryBytes(clone, "_meta/manifest.json");
	CHECK(manifest.find("\"catalog_id\": \"" + std::string(newId) + "\"") != std::string::npos);
	CHECK(manifest.find("\"primary_graph\": \"behavior/primary.graph.json\"")
		!= std::string::npos);

	const std::string entityArchive =
		cloneEntryBytes(clone, "dependencies/assets/entities/deployed.pdentity");
	const std::string bindings = cloneEntryBytes(entityArchive, "bindings.json");
	CHECK(bindings.find("example:tri_body") != std::string::npos);
	CHECK(bindings.find("example:tri_head") != std::string::npos);
	const std::string projectileArchive =
		cloneEntryBytes(clone, "dependencies/assets/projectiles/primary.pdprojectile");
	const std::string projectileGraph =
		cloneEntryBytes(projectileArchive, "behavior.graph.json");
	CHECK(projectileGraph.find(projectile) != std::string::npos);
	CHECK(projectileGraph.find(entity) != std::string::npos);

	/* Nothing but the clone is left beside it. */
	size_t files = 0;
	for (const auto &entry : std::filesystem::directory_iterator(dir.path)) {
		(void)entry;
		files++;
	}
	CHECK(files == 1);
}

TEST_CASE("typed archive clone keeps one new id per old id across a nested tree",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;

	SECTION("character with body, hand and head meshes") {
		const std::string source = cloneSourcePath(
			"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter");
		const std::filesystem::path out = dir.path / "clone_tri_character.pdcharacter";
		asset_archive_clone_report_t report;
		char err[256] = {};
		const s32 code = assetArchiveCloneFile(source.c_str(), out.string().c_str(),
			"mod_clonetest:dock_welder", "pd-tests", &report, err, sizeof(err));
		INFO("clone error: " << err);
		REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);

		/* Character, body, head, and one mesh id shared by three archives. */
		REQUIRE(report.id_count == 4);
		REQUIRE(report.archive_count == 6);
		const std::string body = cloneNewIdOf(report, "example:tri_body");
		const std::string head = cloneNewIdOf(report, "example:tri_head");
		CHECK(body == "mod_clonetest:dock_welder__body_tri_body");
		CHECK(head == "mod_clonetest:dock_welder__head_tri_head");
		CHECK(cloneNewIdOf(report, "example:tri_mesh")
			== "mod_clonetest:dock_welder__mesh_tri_mesh");

		cloneCheckCommon(source, out, report, "mod_clonetest");

		const std::string clone = cloneReadFileBytes(out);
		const std::string descriptor = cloneEntryBytes(clone, "character.ini");
		CHECK(descriptor.find("body_asset = " + body) != std::string::npos);
		CHECK(descriptor.find("head_asset = " + head) != std::string::npos);
		CHECK(descriptor.find("body_archive = dependencies/assets/body/tri_body.pdbody")
			!= std::string::npos);
	}

	SECTION("vehicle with no embedded archives") {
		const std::string source = cloneSourcePath(
			"examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle");
		const std::filesystem::path out = dir.path / "clone_tri_vehicle.pdvehicle";
		asset_archive_clone_report_t report;
		char err[256] = {};
		const s32 code = assetArchiveCloneFile(source.c_str(), out.string().c_str(),
			"mod_clonetest:tug", "pd-tests", &report, err, sizeof(err));
		INFO("clone error: " << err);
		REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
		REQUIRE(report.id_count == 1);
		REQUIRE(report.archive_count == 1);

		cloneCheckCommon(source, out, report, "mod_clonetest");

		const std::string clone = cloneReadFileBytes(out);
		CHECK(cloneEntryBytes(clone, "physics.json").find("\"catalog_id\": \"mod_clonetest:tug\"")
			!= std::string::npos);
		CHECK(cloneEntryBytes(clone, "physics.json").find("\"archetype\": \"hoverbike\"")
			!= std::string::npos);
	}

	SECTION("mod weapon whose embedded ids are already derived from its root") {
		const std::string source = cloneSourcePath("dev-mods/needler/needler.pdweapon");
		const std::filesystem::path out = dir.path / "clone_needler.pdweapon";
		asset_archive_clone_report_t report;
		char err[256] = {};
		const s32 code = assetArchiveCloneFile(source.c_str(), out.string().c_str(),
			"mod_clonetest:needle_gun", "pd-tests", &report, err, sizeof(err));
		INFO("clone error: " << err);
		REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);

		/* Three levels deep: weapon, projectile, effect, sound. */
		REQUIRE(report.id_count == 8);
		REQUIRE(report.archive_count == 9);
		CHECK(cloneNewIdOf(report, "mod_needler:needler__projectile_homing")
			== "mod_clonetest:needle_gun__projectile_homing");
		CHECK(cloneNewIdOf(report, "mod_needler:needler__projectile_burst")
			== "mod_clonetest:needle_gun__projectile_burst");
		CHECK(cloneNewIdOf(report, "mod_needler:needler_model")
			== "mod_clonetest:needle_gun__mesh_needler_model");
		CHECK(cloneNewIdOf(report, "mod_needler:needle")
			== "mod_clonetest:needle_gun__mesh_needle");
		CHECK(cloneNewIdOf(report, "mod_needler:pink_burst_sfx")
			== "mod_clonetest:needle_gun__sfx_pink_burst_sfx");

		cloneCheckCommon(source, out, report, "mod_clonetest");

		REQUIRE(weaponGraphArchiveValidateRootFile(out.string().c_str(), ASSET_WEAPON,
			err, sizeof(err)) == 0);
	}
}

TEST_CASE("typed archive clone refuses bad requests and leaves nothing behind",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon");
	const std::filesystem::path out = dir.path / "refused.pdweapon";
	asset_archive_clone_report_t report;
	char err[256];

	const auto expectRefused = [&](const char *src, const std::filesystem::path &dst,
			const char *newId, s32 expected) {
		std::memset(&report, 0xff, sizeof(report));
		err[0] = '\0';
		const s32 code = assetArchiveCloneFile(src, dst.string().c_str(), newId,
			"pd-tests", &report, err, sizeof(err));
		INFO("request id=" << (newId ? newId : "(null)") << " error=" << err);
		CHECK(code == expected);
		CHECK(std::string(err).size() > 0);
		CHECK(report.id_count == 0);
		CHECK(report.archive_count == 0);
	};

	/* Catalog ids that are not namespace:name. */
	for (const char *bad : { "", "rivet", ":rivet", "mod:", "1mod:rivet", "mod:_rivet",
			"mod rivet:x", "mod:rivet carbine",
			"mod:this_name_is_far_too_long_to_be_a_catalog_id_because_it_exceeds_the_limit" }) {
		expectRefused(source.c_str(), out, bad, ASSET_ARCHIVE_CLONE_ERR_ARGS);
		CHECK(!std::filesystem::exists(out));
	}
	expectRefused(source.c_str(), out, nullptr, ASSET_ARCHIVE_CLONE_ERR_ARGS);

	/* The new id must differ from the template's. */
	expectRefused(source.c_str(), out, "example:tri_weapon", ASSET_ARCHIVE_CLONE_ERR_ARGS);
	CHECK(!std::filesystem::exists(out));

	/* Source and output must be the same family, and different files. */
	expectRefused(source.c_str(), dir.path / "refused.pdvehicle", "mod_clonetest:x",
		ASSET_ARCHIVE_CLONE_ERR_ARGS);
	expectRefused(source.c_str(), dir.path / "refused.zip", "mod_clonetest:x",
		ASSET_ARCHIVE_CLONE_ERR_ARGS);
	expectRefused(source.c_str(), std::filesystem::path(source), "mod_clonetest:x",
		ASSET_ARCHIVE_CLONE_ERR_ARGS);

	/* An existing output is never overwritten. */
	{
		const std::filesystem::path existing = dir.path / "existing.pdweapon";
		{
			std::ofstream keep(existing, std::ios::binary);
			keep << "keep me";
		}
		expectRefused(source.c_str(), existing, "mod_clonetest:x",
			ASSET_ARCHIVE_CLONE_ERR_ARGS);
		CHECK(cloneReadFileBytes(existing) == "keep me");
		std::filesystem::remove(existing);
	}

	/* A source that is missing, or is not a release-valid typed archive. */
	expectRefused((dir.path / "missing.pdweapon").string().c_str(), out,
		"mod_clonetest:x", ASSET_ARCHIVE_CLONE_ERR_SOURCE);
	{
		const std::filesystem::path junk = dir.path / "junk.pdweapon";
		{
			std::ofstream file(junk, std::ios::binary);
			file << "this is not a zip archive";
		}
		expectRefused(junk.string().c_str(), out, "mod_clonetest:x",
			ASSET_ARCHIVE_CLONE_ERR_SOURCE);
		std::filesystem::remove(junk);
	}

	/* No output and no scratch file survives any refusal. */
	CHECK(!std::filesystem::exists(out));
	size_t files = 0;
	for (const auto &entry : std::filesystem::directory_iterator(dir.path)) {
		(void)entry;
		files++;
	}
	CHECK(files == 0);

	/* The same request with a good id succeeds, so the refusals above were
	 * about the arguments and not the environment. */
	std::memset(&report, 0, sizeof(report));
	REQUIRE(assetArchiveCloneFile(source.c_str(), out.string().c_str(),
		"mod_clonetest:control", "pd-tests", &report, err, sizeof(err))
		== ASSET_ARCHIVE_CLONE_OK);
	CHECK(std::filesystem::exists(out));

	/* The report is optional. */
	const std::filesystem::path second = dir.path / "no_report.pdweapon";
	REQUIRE(assetArchiveCloneFile(source.c_str(), second.string().c_str(),
		"mod_clonetest:no_report", nullptr, nullptr, nullptr, 0)
		== ASSET_ARCHIVE_CLONE_OK);
	CHECK(std::filesystem::exists(second));
}

namespace {

asset_archive_clone_override_t cloneReplace(const char *entry, const std::string &bytes)
{
	asset_archive_clone_override_t ov{};
	ov.entry = entry;
	ov.data = bytes.data();
	ov.len = static_cast<u32>(bytes.size());
	return ov;
}

asset_archive_clone_override_t cloneRemove(const char *entry)
{
	asset_archive_clone_override_t ov{};
	ov.entry = entry;
	ov.remove = 1;
	return ov;
}

size_t cloneCountFiles(const std::filesystem::path &dir)
{
	size_t files = 0;
	for (const auto &entry : std::filesystem::directory_iterator(dir)) {
		(void)entry;
		files++;
	}
	return files;
}

} // namespace

TEST_CASE("typed archive rebuild applies overrides while it re-identifies",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon");
	const std::string sourceBytes = cloneReadFileBytes(source);
	const std::filesystem::path out = dir.path / "clone_override_gun.pdweapon";
	const char *newId = "mod_clonetest:override_gun";
	const char *projectilePath = "dependencies/assets/projectiles/primary.pdprojectile";
	const char *meshPath = "dependencies/assets/models/weapon.pdmesh";

	/* Root text member replaced; written in terms of the TEMPLATE's ids to
	 * prove override text goes through the same rewrite as source text. */
	const std::string settings =
		"{\n  \"schema\": \"pd.weapon_settings.v1\",\n"
		"  \"fire_cadence\": { \"value\": 12, \"unit\": \"centiseconds\" }\n}\n";
	const std::string primary =
		"{\n  \"schema\": \"pd.weapon_graph.v1\",\n"
		"  \"asset_id\": \"example:tri_weapon\",\n"
		"  \"graph_id\": \"primary\",\n"
		"  \"nodes\": [\n"
		"    { \"id\": \"trigger_primary\", \"kind\": \"event.trigger_pressed\", \"params\": { \"mode\": \"primary\" } },\n"
		"    { \"id\": \"spawn_projectile\", \"kind\": \"spawn.fired_projectile\", \"params\": { \"mode\": \"primary\", \"function_type\": \"shoot_projectile\", \"projectile_ref\": \"example:tri_projectile\" } }\n"
		"  ],\n"
		"  \"edges\": [ { \"from\": \"trigger_primary\", \"to\": \"spawn_projectile\" } ],\n"
		"  \"exports\": [ { \"name\": \"primary\", \"node\": \"spawn_projectile\" } ]\n}\n";

	/* A member of an embedded archive, edited. */
	const std::string projectileArchive = cloneEntryBytes(sourceBytes, projectilePath);
	std::string projectileGraph = cloneEntryBytes(projectileArchive, "behavior.graph.json");
	const size_t speedAt = projectileGraph.find("\"speed\": 18.0");
	REQUIRE(speedAt != std::string::npos);
	projectileGraph.replace(speedAt, std::strlen("\"speed\": 18.0"), "\"speed\": 31.0");
	const std::string projectileGraphPath =
		std::string(projectilePath) + "::behavior.graph.json";

	/* A whole embedded archive replaced by one from another mod. */
	const std::string needler =
		cloneReadFileBytes(cloneSourcePath("dev-mods/needler/needler.pdweapon"));
	const std::string needlerMesh = cloneEntryBytes(needler, meshPath);

	const asset_archive_clone_override_t overrides[] = {
		cloneReplace("behavior/settings.json", settings),
		cloneReplace("behavior/primary.graph.json", primary),
		cloneReplace(projectileGraphPath.c_str(), projectileGraph),
		cloneReplace(meshPath, needlerMesh),
	};

	asset_archive_clone_report_t report;
	char err[256] = {};
	const s32 code = assetArchiveRebuildFile(source.c_str(), out.string().c_str(), newId,
		"pd-tests", overrides, 4, &report, err, sizeof(err));
	INFO("rebuild error: " << err);
	REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
	REQUIRE(assetArchiveValidateFile(out.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, sizeof(err)) == 0);
	REQUIRE(weaponGraphArchiveValidateRootFile(out.string().c_str(), ASSET_WEAPON,
		err, sizeof(err)) == 0);

	const std::string projectile = cloneNewIdOf(report, "example:tri_projectile");
	const std::string mesh = cloneNewIdOf(report, "mod_needler:needler_model");
	CHECK(projectile == "mod_clonetest:override_gun__projectile_tri_projectile");
	CHECK(mesh == "mod_clonetest:override_gun__mesh_needler_model");
	/* The replaced mesh's old id is no longer part of the tree. */
	CHECK(cloneNewIdOf(report, "example:tri_mesh").empty());
	CHECK(report.id_count == 5);
	CHECK(report.archive_count == 5);

	const std::string result = cloneReadFileBytes(out);
	CHECK(cloneEntryBytes(result, "behavior/settings.json") == settings);
	const std::string newPrimary = cloneEntryBytes(result, "behavior/primary.graph.json");
	CHECK(newPrimary.find("\"asset_id\": \"" + std::string(newId) + "\"") != std::string::npos);
	CHECK(newPrimary.find("\"projectile_ref\": \"" + projectile + "\"") != std::string::npos);
	CHECK(newPrimary.find("example:") == std::string::npos);

	const std::string newProjectile = cloneEntryBytes(result, projectilePath);
	const std::string newProjectileGraph = cloneEntryBytes(newProjectile, "behavior.graph.json");
	CHECK(newProjectileGraph.find("\"speed\": 31.0") != std::string::npos);
	CHECK(newProjectileGraph.find("\"speed\": 18.0") == std::string::npos);
	CHECK(newProjectileGraph.find(projectile) != std::string::npos);

	const std::string newMesh = cloneEntryBytes(result, meshPath);
	CHECK(cloneDescriptorId(cloneEntryBytes(newMesh, "mesh.ini")) == mesh);

	/* Untouched members still match the template. */
	CHECK(cloneEntryBytes(result, "behavior/variables.json")
		== cloneEntryBytes(sourceBytes, "behavior/variables.json"));

	CHECK(cloneCountFiles(dir.path) == 1);
	cloneKeepArtifact(out);
}

TEST_CASE("typed archive rebuild keeps the manifest's copy of a changed descriptor value",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle");
	const std::string sourceBytes = cloneReadFileBytes(source);
	const std::string mesh =
		cloneReadFileBytes(cloneSourcePath("examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh"));
	const std::filesystem::path out = dir.path / "mirror_vehicle.pdvehicle";
	const char *newId = "mod_clonetest:mirror_vehicle";
	const char *hullPath = "dependencies/assets/models/hull.pdmesh";

	/* The vehicle's model becomes an embedded mesh; its name changes too. The
	 * template's manifest repeats model_file but not name. */
	std::string descriptor = cloneEntryBytes(sourceBytes, "vehicle.ini");
	const std::string oldModel = "model_file = model.gltf";
	const std::string oldName = "name = Triangle Hoverbike";
	REQUIRE(descriptor.find(oldModel) != std::string::npos);
	REQUIRE(descriptor.find(oldName) != std::string::npos);
	descriptor.replace(descriptor.find(oldModel), oldModel.size(),
		"model_file = " + std::string(hullPath));
	descriptor.replace(descriptor.find(oldName), oldName.size(), "name = Mirror Hover");
	const std::string sourceManifest = cloneEntryBytes(sourceBytes, "_meta/manifest.json");
	REQUIRE(sourceManifest.find("\"model_file\": \"model.gltf\"") != std::string::npos);

	const asset_archive_clone_override_t overrides[] = {
		cloneReplace("vehicle.ini", descriptor),
		cloneReplace(hullPath, mesh),
		cloneRemove("model.gltf"),
	};
	asset_archive_clone_report_t report;
	char err[256] = {};
	const s32 code = assetArchiveRebuildFile(source.c_str(), out.string().c_str(), newId,
		"pd-tests", overrides, 3, &report, err, sizeof(err));
	INFO("rebuild error: " << err);
	REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
	REQUIRE(assetArchiveValidateFile(out.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, sizeof(err)) == 0);

	const std::string result = cloneReadFileBytes(out);
	const std::string manifest = cloneEntryBytes(result, "_meta/manifest.json");
	CHECK(manifest.find("\"model_file\": \"" + std::string(hullPath) + "\"") != std::string::npos);
	CHECK(manifest.find("model.gltf") == std::string::npos);
	/* Values the descriptor kept stay as they were, ids are re-identified,
	 * and a key the manifest does not repeat does not appear in it. */
	CHECK(manifest.find("\"physics_file\": \"physics.json\"") != std::string::npos);
	CHECK(manifest.find("\"behavior_graph\": \"behavior.graph.json\"") != std::string::npos);
	CHECK(manifest.find("\"catalog_id\": \"" + std::string(newId) + "\"") != std::string::npos);
	CHECK(manifest.find("Mirror Hover") == std::string::npos);
	const std::string newDescriptor = cloneEntryBytes(result, "vehicle.ini");
	CHECK(newDescriptor.find("model_file = " + std::string(hullPath)) != std::string::npos);
	CHECK(newDescriptor.find("name = Mirror Hover") != std::string::npos);
	CHECK(cloneCountFiles(dir.path) == 1);
}

TEST_CASE("typed archive rebuild moves a mesh manifest's geometry with its descriptor",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle");
	const std::string sourceBytes = cloneReadFileBytes(source);
	const std::string mesh =
		cloneReadFileBytes(cloneSourcePath("examples/modding/typed-pdxxx-basic/meshes/tri_mesh.pdmesh"));
	const std::filesystem::path out = dir.path / "obj_vehicle.pdvehicle";
	const char *newId = "mod_clonetest:obj_vehicle";
	const std::string hull = "dependencies/assets/models/hull.pdmesh";

	/* The embedded mesh swaps its glTF for an OBJ (the Maian vehicle recipe does
	 * this): a descriptor naming model.obj, the OBJ, the glTF removed. The
	 * template mesh's manifest names model.gltf twice: "model_file" (the
	 * descriptor's own key) and "geometry" (the echo of whichever geometry key
	 * the descriptor uses), and a recipe cannot replace a manifest. */
	std::string meshIni = cloneEntryBytes(mesh, "mesh.ini");
	const std::string oldGeometry = "model_file = model.gltf";
	REQUIRE(meshIni.find(oldGeometry) != std::string::npos);
	meshIni.replace(meshIni.find(oldGeometry), oldGeometry.size(), "model_file = model.obj");
	const std::string sourceMeshManifest = cloneEntryBytes(mesh, "_meta/manifest.json");
	REQUIRE(sourceMeshManifest.find("\"geometry\": \"model.gltf\"") != std::string::npos);
	REQUIRE(sourceMeshManifest.find("\"model_file\": \"model.gltf\"") != std::string::npos);
	std::string descriptor = cloneEntryBytes(sourceBytes, "vehicle.ini");
	const std::string oldModel = "model_file = model.gltf";
	REQUIRE(descriptor.find(oldModel) != std::string::npos);
	descriptor.replace(descriptor.find(oldModel), oldModel.size(), "model_file = " + hull);
	const std::string obj = "v 0 0 0 255 255 255 255\nv 100 0 0 255 255 255 255\n"
		"v 0 100 0 255 255 255 255\nf 1 2 3\n";
	const std::string iniEntry = hull + "::mesh.ini";
	const std::string objEntry = hull + "::model.obj";
	const std::string gltfEntry = hull + "::model.gltf";

	const asset_archive_clone_override_t overrides[] = {
		cloneReplace("vehicle.ini", descriptor),
		cloneReplace(hull.c_str(), mesh),
		cloneReplace(iniEntry.c_str(), meshIni),
		cloneReplace(objEntry.c_str(), obj),
		cloneRemove(gltfEntry.c_str()),
		cloneRemove("model.gltf"),
	};
	asset_archive_clone_report_t report;
	char err[256] = {};
	const s32 code = assetArchiveRebuildFile(source.c_str(), out.string().c_str(), newId,
		"pd-tests", overrides, 6, &report, err, sizeof(err));
	INFO("rebuild error: " << err);
	REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
	REQUIRE(assetArchiveValidateFile(out.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE,
		err, sizeof(err)) == 0);

	const std::string result = cloneReadFileBytes(out);
	const std::string newMesh = cloneEntryBytes(result, hull.c_str());
	const std::string manifest = cloneEntryBytes(newMesh, "_meta/manifest.json");
	CHECK(manifest.find("\"model_file\": \"model.obj\"") != std::string::npos);
	CHECK(manifest.find("\"geometry\": \"model.obj\"") != std::string::npos);
	CHECK(manifest.find("model.gltf") == std::string::npos);
	CHECK(manifest.find("\"material_file\": \"model.mtl\"") != std::string::npos);
	CHECK(cloneEntryBytes(newMesh, "model.obj") == obj);
	CHECK(cloneCountFiles(dir.path) == 1);
}

TEST_CASE("typed archive rebuild edits an archive under its own id",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle");
	const std::string sourceBytes = cloneReadFileBytes(source);
	const std::filesystem::path first = dir.path / "tug_v1.pdvehicle";
	const std::filesystem::path second = dir.path / "tug_v2.pdvehicle";
	asset_archive_clone_report_t report;
	char err[256] = {};

	REQUIRE(assetArchiveCloneFile(source.c_str(), first.string().c_str(),
		"mod_clonetest:tug_edit", "pd-tests", &report, err, sizeof(err))
		== ASSET_ARCHIVE_CLONE_OK);

	/* Edit the clone under the id it already has: tune one physics value. */
	const std::string firstBytes = cloneReadFileBytes(first);
	std::string physics = cloneEntryBytes(firstBytes, "physics.json");
	/* physics v3 holds the handling numbers; the boost is the vehicle
	 * graph's (B-1230). */
	const size_t thrustAt = physics.find("\"forward_thrust\": 1.2");
	REQUIRE(thrustAt != std::string::npos);
	physics.replace(thrustAt, std::strlen("\"forward_thrust\": 1.2"), "\"forward_thrust\": 1.5");
	const asset_archive_clone_override_t overrides[] = {
		cloneReplace("physics.json", physics),
	};
	const s32 code = assetArchiveRebuildFile(first.string().c_str(),
		second.string().c_str(), "mod_clonetest:tug_edit", "pd-tests",
		overrides, 1, &report, err, sizeof(err));
	INFO("rebuild error: " << err);
	REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
	REQUIRE(report.id_count == 1);
	CHECK(std::string(report.ids[0].old_id) == "mod_clonetest:tug_edit");
	CHECK(std::string(report.ids[0].new_id) == "mod_clonetest:tug_edit");
	REQUIRE(assetArchiveValidateFile(second.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) == 0);

	const std::string secondBytes = cloneReadFileBytes(second);
	const std::string newPhysics = cloneEntryBytes(secondBytes, "physics.json");
	CHECK(newPhysics.find("\"forward_thrust\": 1.5") != std::string::npos);
	CHECK(newPhysics.find("\"catalog_id\": \"mod_clonetest:tug_edit\"") != std::string::npos);
	CHECK(cloneEntryBytes(secondBytes, "vehicle.ini") == cloneEntryBytes(firstBytes, "vehicle.ini"));
	CHECK(cloneEntryBytes(secondBytes, "model.gltf") == cloneEntryBytes(sourceBytes, "model.gltf"));

	/* Same id with nothing to change is refused. */
	const std::filesystem::path third = dir.path / "tug_v3.pdvehicle";
	CHECK(assetArchiveRebuildFile(first.string().c_str(), third.string().c_str(),
		"mod_clonetest:tug_edit", "pd-tests", nullptr, 0, &report, err, sizeof(err))
		== ASSET_ARCHIVE_CLONE_ERR_ARGS);
	CHECK(!std::filesystem::exists(third));

	CHECK(cloneCountFiles(dir.path) == 2);
	cloneKeepArtifact(second);
}

TEST_CASE("typed archive rebuild refuses overrides it cannot honour",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon");
	const std::string sourceBytes = cloneReadFileBytes(source);
	const std::filesystem::path out = dir.path / "refused_override.pdweapon";
	const std::string text = "{}\n";
	asset_archive_clone_report_t report;
	char err[256];

	const auto expectRefused = [&](const asset_archive_clone_override_t *overrides,
			u32 count, s32 expected) {
		std::memset(&report, 0xff, sizeof(report));
		err[0] = '\0';
		const s32 code = assetArchiveRebuildFile(source.c_str(), out.string().c_str(),
			"mod_clonetest:refused", "pd-tests", overrides, count, &report,
			err, sizeof(err));
		INFO("error=" << err);
		CHECK(code == expected);
		CHECK(std::string(err).size() > 0);
		CHECK(report.id_count == 0);
		CHECK(!std::filesystem::exists(out));
		CHECK(cloneCountFiles(dir.path) == 0);
	};

	/* `_meta` is derived, never supplied. */
	{
		const asset_archive_clone_override_t ov[] = { cloneReplace("_meta/manifest.json", text) };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	{
		const asset_archive_clone_override_t ov[] = { cloneReplace(
			"dependencies/assets/models/weapon.pdmesh::_meta/manifest.json", text) };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* Malformed paths. */
	for (const char *bad : { "", "::behavior/settings.json", "behavior/settings.json::",
			"/behavior/settings.json", "behavior\\settings.json" }) {
		const asset_archive_clone_override_t ov[] = { cloneReplace(bad, text) };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* The same member twice. */
	{
		const asset_archive_clone_override_t ov[] = {
			cloneReplace("behavior/settings.json", text),
			cloneReplace("behavior/settings.json", text),
		};
		expectRefused(ov, 2, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* A length with no bytes, and a count with no list. */
	{
		asset_archive_clone_override_t ov[] = { cloneReplace("behavior/settings.json", text) };
		ov[0].data = nullptr;
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
		expectRefused(nullptr, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* A member of an archive that is not in the tree. */
	{
		const asset_archive_clone_override_t ov[] = { cloneReplace(
			"dependencies/assets/models/missing.pdmesh::mesh.ini", text) };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* Removing something that is not there, and removing the descriptor. */
	{
		const asset_archive_clone_override_t ov[] = { cloneRemove("bindings/nothing.json") };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	{
		const asset_archive_clone_override_t ov[] = { cloneRemove("weapon.ini") };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* A descriptor that declares a different catalog id. */
	{
		std::string descriptor = cloneEntryBytes(sourceBytes, "weapon.ini");
		const size_t idAt = descriptor.find("example:tri_weapon");
		REQUIRE(idAt != std::string::npos);
		descriptor.replace(idAt, std::strlen("example:tri_weapon"), "mod_other:gun");
		const asset_archive_clone_override_t ov[] = { cloneReplace("weapon.ini", descriptor) };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_ARGS);
	}
	/* Removing a file the descriptor still declares fails release validation
	 * of the result, and the rejected result is not left behind. */
	{
		const asset_archive_clone_override_t ov[] = { cloneRemove("behavior/settings.json") };
		expectRefused(ov, 1, ASSET_ARCHIVE_CLONE_ERR_OUTPUT);
	}
}

TEST_CASE("typed archive rebuild can stage its result where the scanner will not see it",
          "[modding][pdxxx][clone]") {
	CloneTempDir dir;
	const std::string source =
		cloneSourcePath("examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon");
	const std::filesystem::path final_path = dir.path / "staged_gun.pdweapon";
	const std::filesystem::path staged =
		dir.path / ("staged_gun.pdweapon" ASSET_ARCHIVE_CLONE_STAGING_SUFFIX);
	asset_archive_clone_report_t report;
	char err[256] = {};

	/* The staged name is not a typed archive to the policy or the scanner. */
	CHECK(assetArchivePathIsTyped(staged.string().c_str()) == 0);

	const s32 code = assetArchiveCloneFile(source.c_str(), staged.string().c_str(),
		"mod_clonetest:staged_gun", "pd-tests", &report, err, sizeof(err));
	INFO("clone error: " << err);
	REQUIRE(code == ASSET_ARCHIVE_CLONE_OK);
	CHECK(std::filesystem::exists(staged));
	CHECK(!std::filesystem::exists(final_path));
	CHECK(cloneCountFiles(dir.path) == 1);
	CHECK(assetArchiveValidateFile(staged.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, err, sizeof(err)) != 0);

	/* Renamed into place it is exactly the archive a direct clone writes. */
	std::filesystem::rename(staged, final_path);
	cloneCheckCommon(source, final_path, report, "mod_clonetest");
	const std::filesystem::path direct = dir.path / "direct_gun.pdweapon";
	REQUIRE(assetArchiveCloneFile(source.c_str(), direct.string().c_str(),
		"mod_clonetest:staged_gun", "pd-tests", nullptr, err, sizeof(err))
		== ASSET_ARCHIVE_CLONE_OK);
	std::map<std::string, std::string> staged_members;
	std::map<std::string, std::string> direct_members;
	cloneFlatten(cloneReadFileBytes(final_path), "", staged_members);
	cloneFlatten(cloneReadFileBytes(direct), "", direct_members);
	CHECK(staged_members == direct_members);

	/* The suffix does not relax anything else: the family must still match,
	 * one suffix only, and an existing staged file is not overwritten. */
	const auto expectRefused = [&](const std::filesystem::path &out) {
		err[0] = '\0';
		const std::string before = std::filesystem::exists(out) ? cloneReadFileBytes(out) : "";
		const bool existed = std::filesystem::exists(out);
		const s32 refused = assetArchiveCloneFile(source.c_str(), out.string().c_str(),
			"mod_clonetest:staged_other", "pd-tests", &report, err, sizeof(err));
		INFO("out=" << out.filename().string() << " error=" << err);
		CHECK(refused == ASSET_ARCHIVE_CLONE_ERR_ARGS);
		CHECK(std::string(err).size() > 0);
		CHECK(std::filesystem::exists(out) == existed);
		if (existed) CHECK(cloneReadFileBytes(out) == before);
	};
	expectRefused(dir.path / ("other.pdvehicle" ASSET_ARCHIVE_CLONE_STAGING_SUFFIX));
	expectRefused(dir.path / ("other" ASSET_ARCHIVE_CLONE_STAGING_SUFFIX));
	expectRefused(dir.path / ("other.pdweapon" ASSET_ARCHIVE_CLONE_STAGING_SUFFIX
		ASSET_ARCHIVE_CLONE_STAGING_SUFFIX));
	{
		const std::filesystem::path existing =
			dir.path / ("existing.pdweapon" ASSET_ARCHIVE_CLONE_STAGING_SUFFIX);
		{
			std::ofstream keep(existing, std::ios::binary);
			keep << "keep me";
		}
		expectRefused(existing);
	}
	CHECK(cloneCountFiles(dir.path) == 3);
}
