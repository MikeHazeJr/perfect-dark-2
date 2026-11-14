/**
 * test_mod_asset_validate.cpp -- one in-game Validate for typed archives.
 *
 * Release validation does not read member content. These cases show that the
 * in-game Validate accepts every checked-in example archive and refuses the
 * same archives once a public text member is malformed or a family's own
 * reader would refuse them, naming the member at fault.
 */
#include "catch.hpp"
extern "C" {
#include "asset_archive_clone.h"
#include "asset_archive_policy.h"
#include "mod_asset_validate.h"
#include "mod_author.h"
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

struct ValidateTempDir {
	fs::path path;

	ValidateTempDir() {
		static unsigned counter = 0;
		const auto stamp =
			std::chrono::high_resolution_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path()
			/ ("pd2-mod-validate-" + std::to_string(stamp) + "-" + std::to_string(counter++));
		fs::create_directories(path);
	}

	~ValidateTempDir() {
		std::error_code ec;
		fs::remove_all(path, ec);
	}
};

std::string validateReadFile(const fs::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string validateExample(const char *relative)
{
	return (fs::path(PD_SOURCE_DIR) / relative).string();
}

std::string validateEntry(const std::string &archive, const std::string &entry)
{
	u32 size = 0;
	void *data = modArchiveExtractMemAlloc(archive.data(),
		static_cast<u32>(archive.size()), entry.c_str(), &size);
	REQUIRE(data != nullptr);
	std::string out(static_cast<const char *>(data), size);
	free(data);
	return out;
}

struct Verdict {
	s32 code = 0;
	std::string err;
};

Verdict validateFile(const fs::path &path)
{
	Verdict v;
	char err[320];
	std::memset(err, 'x', sizeof(err));
	err[sizeof(err) - 1] = '\0';
	v.code = modAssetValidateArchiveFile(path.string().c_str(), err, sizeof(err));
	v.err = err;
	return v;
}

/* The example at `relative`, rebuilt under its own id with one member
 * replaced. Release validation still passes: it does not read the content. */
fs::path validateWithMember(const ValidateTempDir &dir, const char *relative, const char *id,
	const char *entry, const std::string &bytes, const char *leaf)
{
	const std::string source = validateExample(relative);
	const fs::path out = dir.path / leaf;
	asset_archive_clone_override_t ov{};
	ov.entry = entry;
	ov.data = bytes.data();
	ov.len = static_cast<u32>(bytes.size());
	char err[256] = {};
	const s32 built = assetArchiveRebuildFile(source.c_str(), out.string().c_str(), id,
		"pd-tests", &ov, 1, nullptr, err, sizeof(err));
	INFO("template " << leaf << " error: " << err);
	REQUIRE(built == ASSET_ARCHIVE_CLONE_OK);
	char policy[256] = {};
	REQUIRE(assetArchiveValidateFile(out.string().c_str(), ASSET_ARCHIVE_VALIDATE_RELEASE,
		policy, sizeof(policy)) == 0);
	return out;
}

const char *const kWeapon = "examples/modding/typed-pdxxx-basic/weapons/tri_weapon.pdweapon";
const char *const kVehicle = "examples/modding/typed-pdxxx-basic/vehicles/tri_vehicle.pdvehicle";
const char *const kBody = "examples/modding/typed-pdxxx-basic/bodies/tri_body.pdbody";
const char *const kCharacter =
	"examples/modding/typed-pdxxx-basic/characters/tri_character.pdcharacter";

} // namespace

TEST_CASE("in-game Validate accepts every checked-in example archive",
          "[modding][validate]") {
	std::vector<fs::path> archives;
	for (const char *root : { "examples/modding/typed-pdxxx-basic", "dev-mods/needler" }) {
		for (const auto &entry : fs::recursive_directory_iterator(fs::path(PD_SOURCE_DIR) / root)) {
			if (entry.is_regular_file() &&
					assetArchivePathIsTyped(entry.path().string().c_str())) {
				archives.push_back(entry.path());
			}
		}
	}
	std::sort(archives.begin(), archives.end());
	REQUIRE(archives.size() >= 28);

	for (const fs::path &archive : archives) {
		const Verdict v = validateFile(archive);
		INFO(archive.filename().string() << " -> " << v.err);
		CHECK(v.code == MOD_ASSET_VALIDATE_OK);
		CHECK(v.err.empty());

		const std::string bytes = validateReadFile(archive);
		char err[160] = {};
		CHECK(modAssetValidateArchiveBytes(bytes.data(), static_cast<u32>(bytes.size()),
			archive.string().c_str(), err, sizeof(err)) == MOD_ASSET_VALIDATE_OK);
	}
}

TEST_CASE("in-game Validate reads the members release validation does not",
          "[modding][validate]") {
	ValidateTempDir dir;

	struct Case {
		const char *what;
		const char *relative;
		const char *id;
		const char *entry;
		std::string bytes;
		s32 expected;
		const char *names;     /* text the reason must contain */
	};

	const std::string vehicleIni =
		validateEntry(validateReadFile(validateExample(kVehicle)), "vehicle.ini");
	const std::string bodyIni =
		validateEntry(validateReadFile(validateExample(kBody)), "body.ini");
	std::string characterIni =
		validateEntry(validateReadFile(validateExample(kCharacter)), "character.ini");
	const std::string nameLine = "display_name = Triangle Character\n";
	REQUIRE(characterIni.find(nameLine) != std::string::npos);
	characterIni.insert(characterIni.find(nameLine), "display_name = Twice\n");
	std::string bodyLocked = bodyIni;
	const std::string bodyId = "catalog_id = example:tri_body\n";
	REQUIRE(bodyLocked.find(bodyId) != std::string::npos);
	bodyLocked.insert(bodyLocked.find(bodyId) + bodyId.size(), "requirefeature = 999\n");

	const Case cases[] = {
		{ "settings that are not JSON", kWeapon, "example:tri_weapon",
		  "behavior/settings.json", "{ not json", MOD_ASSET_VALIDATE_ERR_TEXT,
		  "tri_weapon_0.pdweapon::behavior/settings.json" },
		{ "a graph with a duplicate key", kWeapon, "example:tri_weapon",
		  "behavior/variables.json",
		  "{ \"schema\": \"pd.weapon_variables.v1\", \"schema\": \"again\", \"variables\": [] }",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "behavior/variables.json" },
		{ "JSON with a byte order mark", kWeapon, "example:tri_weapon",
		  "bindings/presentation.json", "\xef\xbb\xbf{ \"schema\": \"pd.weapon.presentation.v1\" }",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "bindings/presentation.json" },
		{ "JSON with trailing text", kWeapon, "example:tri_weapon",
		  "behavior/settings.json", "{ \"schema\": \"pd.weapon_settings.v1\" } trailing",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "behavior/settings.json" },
		{ "a broken graph inside an embedded projectile", kWeapon, "example:tri_weapon",
		  "dependencies/assets/projectiles/primary.pdprojectile::behavior.graph.json",
		  "{ \"schema\": \"pd.projectile_graph.v1\", ", MOD_ASSET_VALIDATE_ERR_TEXT,
		  "dependencies/assets/projectiles/primary.pdprojectile::behavior.graph.json" },
		{ "a shared context cut short", kWeapon, "example:tri_weapon",
		  "behavior/shared-context.json", "{ \"schema\": \"pd.weapon_shared_context.v1\", \"contexts\": [",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "behavior/shared-context.json" },
		{ "a descriptor line with no equals sign", kVehicle, "example:tri_vehicle",
		  "vehicle.ini", vehicleIni + "\nname: Colon Is Not Equals\n",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "vehicle.ini line " },
		{ "a descriptor that is not UTF-8", kVehicle, "example:tri_vehicle",
		  "vehicle.ini", vehicleIni + "note = caf\xe9\n",
		  MOD_ASSET_VALIDATE_ERR_TEXT, "not valid UTF-8" },
		{ "a body field its reader refuses", kBody, "example:tri_body",
		  "body.ini", bodyLocked, MOD_ASSET_VALIDATE_ERR_FAMILY, "body reader" },
		{ "a character key given twice", kCharacter, "example:tri_character",
		  "character.ini", characterIni, MOD_ASSET_VALIDATE_ERR_FAMILY, "character reader" },
	};

	unsigned index = 0;
	for (const Case &c : cases) {
		const std::string ext = fs::path(c.relative).extension().string();
		const std::string leaf = fs::path(c.relative).stem().string() + "_" +
			std::to_string(index++) + ext;
		const fs::path archive =
			validateWithMember(dir, c.relative, c.id, c.entry, c.bytes, leaf.c_str());
		const Verdict v = validateFile(archive);
		INFO(c.what << " -> code " << v.code << ": " << v.err);
		CHECK(v.code == c.expected);
		CHECK(!v.err.empty());
		CHECK(v.err.find(c.names) != std::string::npos);
	}
}

TEST_CASE("in-game Validate refuses what is not a typed archive",
          "[modding][validate]") {
	ValidateTempDir dir;
	char err[160] = {};
	const std::string weapon = validateReadFile(validateExample(kWeapon));

	CHECK(modAssetValidateArchiveBytes(nullptr, 0, "x.pdweapon", err, sizeof(err))
		== MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		"x.zip", err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		nullptr, err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveFile(nullptr, err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);
	CHECK(modAssetValidateArchiveFile((dir.path / "missing.pdweapon").string().c_str(),
		err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_ARGS);

	/* A weapon's bytes under a vehicle's name, and bytes that are no archive. */
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		"x.pdvehicle", err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_POLICY);
	CHECK(std::string(err).size() > 0);
	const std::string junk = "this is not a zip archive";
	CHECK(modAssetValidateArchiveBytes(junk.data(), static_cast<u32>(junk.size()),
		"x.pdweapon", err, sizeof(err)) == MOD_ASSET_VALIDATE_ERR_POLICY);

	/* The reason is optional. */
	CHECK(modAssetValidateArchiveBytes(weapon.data(), static_cast<u32>(weapon.size()),
		"x.pdweapon", nullptr, 0) == MOD_ASSET_VALIDATE_OK);
}

TEST_CASE("mod author refuses to save an archive whose content is malformed",
          "[modding][validate][author]") {
	ValidateTempDir mods;
	const std::string tmpl = validateExample(kWeapon);
	const std::string root = mods.path.string();
	auto result = std::make_unique<mod_author_result_t>();
	char err[320] = {};

	mod_author_request_t request{};
	request.template_path = tmpl.c_str();
	request.mods_root = root.c_str();
	request.display_name = "Checked Gun";
	REQUIRE(modAuthorSave(&request, result.get(), err, sizeof(err)) == MOD_AUTHOR_OK);
	const std::string archivePath = result->archive_path;
	const std::string manifestPath = result->manifest_path;
	const std::string archiveBefore = validateReadFile(archivePath);
	const std::string manifestBefore = validateReadFile(manifestPath);

	asset_archive_clone_override_t broken{};
	broken.entry = "behavior/settings.json";
	broken.data = "{ not json";
	broken.len = 10;
	request.overrides = &broken;
	request.override_count = 1;
	request.version = "9.9.9";
	const s32 code = modAuthorSave(&request, result.get(), err, sizeof(err));
	INFO("save error: " << err);
	CHECK(code == MOD_AUTHOR_ERR_ARCHIVE);
	CHECK(std::string(err).find("behavior/settings.json") != std::string::npos);

	/* The working mod is untouched and nothing staged is left beside it. */
	CHECK(validateReadFile(archivePath) == archiveBefore);
	CHECK(validateReadFile(manifestPath) == manifestBefore);
	size_t files = 0;
	for (const auto &entry : fs::recursive_directory_iterator(mods.path)) {
		if (entry.is_regular_file()) files++;
	}
	CHECK(files == 2);
}

TEST_CASE("pdmod packers refuse a typed archive whose content is malformed",
          "[modding][validate][pdmod]") {
	ValidateTempDir dir;
	const fs::path broken = validateWithMember(dir, kWeapon, "example:tri_weapon",
		"behavior/settings.json", "{ not json", "broken.pdweapon");
	const std::string manifest =
		"{ \"id\": \"mod.packcheck\", \"name\": \"Pack Check\", \"version\": \"1.0.0\" }\n";
	const fs::path folder = dir.path / "mod";
	fs::create_directories(folder);
	{
		std::ofstream out(folder / "mod.json", std::ios::binary);
		out << manifest;
	}
	fs::copy_file(broken, folder / "broken.pdweapon");
	const std::string pack = (dir.path / "out.pdmod").string();

	/* Folder packer. */
	CHECK(modpackPdmodFromFolder(folder.string().c_str(), pack.c_str())
		== MODPACK_PDMOD_ERR_LAYOUT);
	CHECK(std::string(modpackPdmodLastError()).find("behavior/settings.json")
		!= std::string::npos);
	CHECK(!fs::exists(pack));

	/* Single-archive writer. */
	const std::string bytes = validateReadFile(broken);
	modpack_entry_t entry{};
	entry.entry_name = "weapons/broken.pdweapon";
	entry.data = bytes.data();
	entry.len = static_cast<u32>(bytes.size());
	CHECK(modpackPdmodWriteSingle(pack.c_str(), manifest.data(),
		static_cast<u32>(manifest.size()), &entry, 1) == MODPACK_PDMOD_ERR_LAYOUT);
	CHECK(std::string(modpackPdmodLastError()).find("behavior/settings.json")
		!= std::string::npos);
	CHECK(!fs::exists(pack));

	/* Control: the same folder with the unbroken example packs. */
	fs::remove(folder / "broken.pdweapon");
	fs::copy_file(validateExample(kWeapon), folder / "good.pdweapon");
	const s32 packed = modpackPdmodFromFolder(folder.string().c_str(), pack.c_str());
	INFO("pack error: " << modpackPdmodLastError());
	CHECK(packed == MODPACK_PDMOD_OK);
	CHECK(fs::exists(pack));
}
