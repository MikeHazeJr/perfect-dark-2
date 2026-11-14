/**
 * test_mod_manifest_validate.cpp -- in-game root mod.json admission.
 *
 * modManifestValidateBytes() is the in-game form of the offline T-MODDING-012
 * rule (validate_manifest() in tools/verify_pdxxx_modder_workflow.py). Both
 * validators are held to one verdict table,
 * tests/fixtures/modmanifest/cases.json: this file checks the "game" column,
 * tools/tests/test_mod_manifest_parity.py checks the "offline" column.
 *
 * The packer cases prove that both production `.pdmod` writers refuse an
 * invalid manifest before they create or change any file.
 */
#include "catch.hpp"
#include "modasset_gltf_document.h"
#include "../port/external/imgui-node-editor/crude_json.h"
extern "C" {
#include "mod_manifest_validate.h"
#include "modpack_pdmod.h"
}
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct ManifestTempPath {
	std::filesystem::path path;

	~ManifestTempPath() {
		if (!path.empty()) {
			std::error_code ec;
			std::filesystem::remove_all(path, ec);
		}
	}
};

std::filesystem::path manifestUniquePath(const char *stem, const char *suffix)
{
	static unsigned counter = 0;
	const auto stamp =
		std::chrono::high_resolution_clock::now().time_since_epoch().count();
	return std::filesystem::temp_directory_path()
		/ (std::string(stem) + "-" + std::to_string(stamp) + "-"
			+ std::to_string(counter++) + suffix);
}

std::string manifestReadFile(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}

void manifestWriteFile(const std::filesystem::path &path, const std::string &bytes)
{
	std::ofstream out(path, std::ios::binary);
	out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool manifestDecodeHex(const std::string &hex, std::string &out)
{
	out.clear();
	if (hex.size() % 2 != 0) return false;
	for (size_t i = 0; i < hex.size(); i += 2) {
		unsigned byte = 0;
		for (size_t k = 0; k < 2; k++) {
			const char c = hex[i + k];
			unsigned nibble;
			if (c >= '0' && c <= '9') nibble = static_cast<unsigned>(c - '0');
			else if (c >= 'a' && c <= 'f') nibble = static_cast<unsigned>(c - 'a') + 10u;
			else return false;
			byte = (byte << 4) | nibble;
		}
		out.push_back(static_cast<char>(byte));
	}
	return true;
}

/* Collapse a validator result into the verdict vocabulary of cases.json. */
std::string manifestVerdict(const std::string &payload)
{
	char error[256];
	const s32 code = modManifestValidateBytes(payload.data(), payload.size(),
		error, sizeof(error));
	const std::string message(error);

	switch (code) {
	case MOD_MANIFEST_OK:
		return message.empty() ? "ok" : "ok-with-error-text";
	case MOD_MANIFEST_ERR_UNREADABLE:
		return message.rfind("unreadable JSON", 0) == 0
			? "unreadable" : "unreadable-with-wrong-text";
	case MOD_MANIFEST_ERR_NOT_OBJECT:
		return message == "expected a JSON object"
			? "not_object" : "not_object-with-wrong-text";
	case MOD_MANIFEST_ERR_FIELD: {
		const std::string prefix = "missing non-empty string ";
		if (message.rfind(prefix, 0) != 0) return "field-with-wrong-text";
		std::string fields = message.substr(prefix.size());
		for (size_t at = fields.find(", "); at != std::string::npos;
				at = fields.find(", ", at)) {
			fields.replace(at, 2, ",");
		}
		return "field:" + fields;
	}
	default:
		return "unknown-code";
	}
}

std::set<std::string> manifestListTree(const std::filesystem::path &root)
{
	std::set<std::string> names;
	for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
		names.insert(std::filesystem::relative(entry.path(), root).generic_string());
	}
	return names;
}

} // namespace

TEST_CASE("mod manifest verdicts follow the shared offline and in-game table",
          "[modding][pdmod][manifest]") {
	const std::string fixture = manifestReadFile(
		std::filesystem::path(PD_SOURCE_DIR) / "tests" / "fixtures"
			/ "modmanifest" / "cases.json");
	REQUIRE(!fixture.empty());

	crude_json::value parsed;
	REQUIRE(modAssetJsonReadValue(fixture.data(), fixture.size(), parsed));
	const crude_json::value &document = parsed;
	REQUIRE(document.is_object());
	REQUIRE(document.contains("schema"));
	REQUIRE(document["schema"].get<crude_json::string>()
		== "pd2.test.mod_manifest_cases.v1");
	REQUIRE(document.contains("cases"));

	const auto &cases = document["cases"].get<crude_json::array>();
	/* A truncated table must not pass quietly. */
	REQUIRE(cases.size() >= 90);

	std::set<std::string> seen;
	size_t accepted = 0;
	size_t stricter = 0;

	for (const auto &row : cases) {
		REQUIRE(row.is_object());
		const std::string name = row["name"].get<crude_json::string>();
		const std::string offline = row["offline"].get<crude_json::string>();
		const std::string game = row["game"].get<crude_json::string>();
		INFO("case " << name);
		CHECK(seen.insert(name).second);

		std::string payload;
		if (row.contains("text")) {
			REQUIRE(!row.contains("hex"));
			payload = row["text"].get<crude_json::string>();
		} else {
			REQUIRE(row.contains("hex"));
			REQUIRE(manifestDecodeHex(row["hex"].get<crude_json::string>(), payload));
		}

		CHECK(manifestVerdict(payload) == game);

		/* The game may refuse what the offline validator accepts, never the
		 * reverse, and every difference carries its reason. */
		if (game == "ok") {
			accepted++;
			CHECK(offline == "ok");
		}
		if (game != offline) {
			stricter++;
			CHECK(row.contains("stricter"));
			CHECK(game != "ok");
		} else {
			CHECK(!row.contains("stricter"));
		}
	}

	CHECK(accepted >= 9);
	CHECK(stricter >= 12);
}

TEST_CASE("mod manifest validation names every failing field and never coerces",
          "[modding][pdmod][manifest]") {
	char error[256];

	std::memset(error, 'x', sizeof(error));
	error[sizeof(error) - 1] = '\0';
	const std::string valid =
		"{\"id\":\"author.sample\",\"name\":\"Sample\",\"version\":\"1.0.0\"}";
	const std::string validCopy = valid;
	REQUIRE(modManifestValidateBytes(valid.data(), valid.size(),
		error, sizeof(error)) == MOD_MANIFEST_OK);
	REQUIRE(std::string(error).empty());
	REQUIRE(valid == validCopy);

	const std::string empty = "{}";
	REQUIRE(modManifestValidateBytes(empty.data(), empty.size(),
		error, sizeof(error)) == MOD_MANIFEST_ERR_FIELD);
	REQUIRE(std::string(error) == "missing non-empty string id, name, version");

	const std::string numericVersion =
		"{\"id\":\"a\",\"name\":\"n\",\"version\":2}";
	REQUIRE(modManifestValidateBytes(numericVersion.data(), numericVersion.size(),
		error, sizeof(error)) == MOD_MANIFEST_ERR_FIELD);
	REQUIRE(std::string(error) == "missing non-empty string version");

	const std::string blankName =
		"{\"id\":\"a\",\"name\":\" \\t\",\"version\":\"1\"}";
	REQUIRE(modManifestValidateBytes(blankName.data(), blankName.size(),
		error, sizeof(error)) == MOD_MANIFEST_ERR_FIELD);
	REQUIRE(std::string(error) == "missing non-empty string name");

	const std::string array = "[]";
	REQUIRE(modManifestValidateBytes(array.data(), array.size(),
		error, sizeof(error)) == MOD_MANIFEST_ERR_NOT_OBJECT);
	REQUIRE(std::string(error) == "expected a JSON object");

	const std::string broken = "{";
	REQUIRE(modManifestValidateBytes(broken.data(), broken.size(),
		error, sizeof(error)) == MOD_MANIFEST_ERR_UNREADABLE);
	REQUIRE(std::string(error).rfind("unreadable JSON", 0) == 0);

	/* Missing input, absent error buffer and a tiny error buffer are all safe. */
	REQUIRE(modManifestValidateBytes(nullptr, 0, error, sizeof(error))
		== MOD_MANIFEST_ERR_UNREADABLE);
	REQUIRE(std::string(error).rfind("unreadable JSON", 0) == 0);
	REQUIRE(modManifestValidateBytes(valid.data(), 0, error, sizeof(error))
		== MOD_MANIFEST_ERR_UNREADABLE);
	REQUIRE(modManifestValidateBytes(empty.data(), empty.size(), nullptr, 0)
		== MOD_MANIFEST_ERR_FIELD);
	char tiny[8];
	std::memset(tiny, 'x', sizeof(tiny));
	REQUIRE(modManifestValidateBytes(empty.data(), empty.size(), tiny, sizeof(tiny))
		== MOD_MANIFEST_ERR_FIELD);
	REQUIRE(tiny[sizeof(tiny) - 1] == '\0');
	REQUIRE(std::string(tiny) == "missing");
}

TEST_CASE("mod manifest validation bounds the input size",
          "[modding][pdmod][manifest]") {
	char error[256];
	const std::string valid =
		"{\"id\":\"author.sample\",\"name\":\"Sample\",\"version\":\"1.0.0\"}";

	/* Exactly at the bound: a valid manifest padded with JSON whitespace. */
	std::string atLimit = valid;
	atLimit.append(MOD_MANIFEST_MAX_BYTES - valid.size(), ' ');
	REQUIRE(atLimit.size() == MOD_MANIFEST_MAX_BYTES);
	REQUIRE(modManifestValidateBytes(atLimit.data(), atLimit.size(),
		error, sizeof(error)) == MOD_MANIFEST_OK);

	std::string overLimit = atLimit;
	overLimit.push_back(' ');
	REQUIRE(modManifestValidateBytes(overLimit.data(), overLimit.size(),
		error, sizeof(error)) == MOD_MANIFEST_ERR_UNREADABLE);
	REQUIRE(std::string(error).find("larger than 8 MiB") != std::string::npos);
}

TEST_CASE("pdmod folder packer refuses an invalid manifest before writing anything",
          "[modding][pdmod][manifest]") {
	const char *const invalidManifests[] = {
		"{ \"id\": \"mod_manifest_reject\", \"name\": \"Manifest Reject\" }\n",
		"{ \"id\": \"mod_manifest_reject\", \"name\": \"\", \"version\": \"1.0.0\" }\n",
		"{ \"id\": 7, \"name\": \"Manifest Reject\", \"version\": \"1.0.0\" }\n",
		"[ { \"id\": \"mod_manifest_reject\", \"name\": \"n\", \"version\": \"1\" } ]\n",
		"{ \"id\": \"mod_manifest_reject\", \"name\": \"n\", \"version\": \"1\", }\n",
	};
	const char *const expectedDetail[] = {
		"mod.json: missing non-empty string version",
		"mod.json: missing non-empty string name",
		"mod.json: missing non-empty string id",
		"mod.json: expected a JSON object",
		"mod.json: unreadable JSON",
	};

	for (size_t i = 0; i < sizeof(invalidManifests) / sizeof(invalidManifests[0]); i++) {
		INFO("manifest " << i);
		ManifestTempPath folder{ manifestUniquePath("pd2-pdmod-manifest-folder", "") };
		REQUIRE(std::filesystem::create_directories(folder.path / "weapons" / "starter"));
		manifestWriteFile(folder.path / "mod.json", invalidManifests[i]);
		manifestWriteFile(folder.path / "weapons" / "starter" / "notes.txt", "starter\n");
		const std::set<std::string> before = manifestListTree(folder.path);

		ManifestTempPath out{ manifestUniquePath("pd2-pdmod-manifest-folder", ".pdmod") };
		REQUIRE(modpackPdmodFromFolder(folder.path.string().c_str(),
			out.path.string().c_str()) == MODPACK_PDMOD_ERR_BAD_MFST);
		CHECK(std::string(modpackPdmodLastError()).find(expectedDetail[i])
			!= std::string::npos);

		/* Nothing was created: no archive, no temp archive, no INI template. */
		CHECK(!std::filesystem::exists(out.path));
		CHECK(!std::filesystem::exists(out.path.string() + ".tmp"));
		CHECK(manifestListTree(folder.path) == before);
		CHECK(manifestReadFile(folder.path / "mod.json") == invalidManifests[i]);
	}
}

TEST_CASE("pdmod single-archive writer refuses an invalid manifest before opening the archive",
          "[modding][pdmod][manifest]") {
	const char payload[] = "theme\n";
	modpack_entry_t entries[] = {
		{ "notes/readme.txt", payload, static_cast<u32>(strlen(payload)) },
	};
	const char *const invalidManifests[] = {
		"{ \"id\": \"mod_manifest_single\", \"version\": \"1.0.0\" }\n",
		"null",
		"{",
	};
	const char *const expectedDetail[] = {
		"mod.json: missing non-empty string name",
		"mod.json: expected a JSON object",
		"mod.json: unreadable JSON",
	};

	for (size_t i = 0; i < sizeof(invalidManifests) / sizeof(invalidManifests[0]); i++) {
		INFO("manifest " << i);
		ManifestTempPath out{ manifestUniquePath("pd2-pdmod-manifest-single", ".pdmod") };
		REQUIRE(modpackPdmodWriteSingle(out.path.string().c_str(),
			invalidManifests[i], static_cast<u32>(strlen(invalidManifests[i])),
			entries, 1) == MODPACK_PDMOD_ERR_BAD_MFST);
		CHECK(std::string(modpackPdmodLastError()).rfind(expectedDetail[i], 0) == 0);
		CHECK(!std::filesystem::exists(out.path));
		CHECK(!std::filesystem::exists(out.path.string() + ".tmp"));
	}

	/* A zero-length manifest is refused even when the pointer is valid. */
	{
		ManifestTempPath out{ manifestUniquePath("pd2-pdmod-manifest-single", ".pdmod") };
		REQUIRE(modpackPdmodWriteSingle(out.path.string().c_str(), "", 0, entries, 1)
			== MODPACK_PDMOD_ERR_BAD_MFST);
		CHECK(!std::filesystem::exists(out.path));
	}

	/* Control: the same call with a valid manifest still writes the archive. */
	{
		const char manifest[] =
			"{ \"id\": \"mod_manifest_single\", \"name\": \"Manifest Single\", \"version\": \"1.0.0\" }\n";
		ManifestTempPath out{ manifestUniquePath("pd2-pdmod-manifest-single", ".pdmod") };
		REQUIRE(modpackPdmodWriteSingle(out.path.string().c_str(),
			manifest, static_cast<u32>(strlen(manifest)), entries, 1)
			== MODPACK_PDMOD_OK);
		CHECK(std::string(modpackPdmodLastError()).empty());
		CHECK(std::filesystem::exists(out.path));
	}
}
