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
