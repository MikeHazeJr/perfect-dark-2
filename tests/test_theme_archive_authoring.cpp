#include "catch.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include "modarchive.h"
#include "theme_archive_authoring.h"

void testStubFsFileLoadWith(const char *path, const void *bytes, u32 size);
void testStubFsFileLoadAdd(const char *path, const void *bytes, u32 size);
}

namespace {

struct TempDir {
	std::filesystem::path path;
	TempDir()
	{
		path = std::filesystem::temp_directory_path() /
			("pd2-theme-author-" + std::to_string(
				std::chrono::high_resolution_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(path);
	}
	~TempDir()
	{
		testStubFsFileLoadWith(nullptr, nullptr, 0);
		std::error_code ec;
		std::filesystem::remove_all(path, ec);
	}
};

std::vector<unsigned char> readFile(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<unsigned char>(std::istreambuf_iterator<char>(in), {});
}

std::vector<unsigned char> extractNested(mod_archive_t *archive,
	const char *entry)
{
	const s32 index = modArchiveFindEntry(archive, entry);
	REQUIRE(index >= 0);
	u32 size = 0;
	void *raw = modArchiveExtractAlloc(archive, index, &size);
	REQUIRE(raw != nullptr);
	std::vector<unsigned char> bytes((unsigned char *)raw,
		(unsigned char *)raw + size);
	free(raw);
	return bytes;
}

std::string archiveText(mod_archive_t *archive, const char *entry)
{
	const s32 index = modArchiveFindEntry(archive, entry);
	REQUIRE(index >= 0);
	u32 size = 0;
	char *raw = (char *)modArchiveExtractAlloc(archive, index, &size);
	REQUIRE(raw != nullptr);
	std::string text(raw, raw + size);
	free(raw);
	return text;
}

} // namespace

TEST_CASE("Theme creator emits a strict self-contained pdtheme with every typed role",
	"[modding][pdxxx][pdtheme][creator][T-ASSETS-029]")
{
	TempDir temp;
	mod_archive_t *fixture = modArchiveOpen(
		"examples/modding/typed-pdxxx-basic/themes/tri_theme.pdtheme");
	REQUIRE(fixture != nullptr);

	const char *entries[] = {
		"dependencies/assets/ui/tri_reticle.pdui",
		"dependencies/assets/font/tri_theme_font.pdfont",
		"dependencies/assets/audio/tri_click.pdsfx",
		"dependencies/assets/music/tri_song.pdsong",
	};
	const char *ids[] = {
		"example:tri_reticle", "example:tri_theme_font", "example:tri_click",
		"example:tri_song",
	};
	const char *leaves[] = { "ui.pdui", "font.pdfont", "audio.pdsfx",
		"music.pdsong" };
	std::vector<std::vector<unsigned char>> dependencyBytes;
	std::vector<std::string> dependencyPaths;
	dependencyBytes.reserve(THEME_ARCHIVE_DEP_COUNT);
	dependencyPaths.reserve(THEME_ARCHIVE_DEP_COUNT);
	theme_archive_dependency_t dependencies[THEME_ARCHIVE_DEP_COUNT] = {};
	for (s32 i = 0; i < THEME_ARCHIVE_DEP_COUNT; i++) {
		dependencyBytes.push_back(extractNested(fixture, entries[i]));
		dependencyPaths.push_back((temp.path / leaves[i]).string());
		dependencies[i] = {
			(theme_archive_dependency_role_e)i,
			ids[i], dependencyPaths.back().c_str()
		};
		testStubFsFileLoadAdd(dependencyPaths.back().c_str(),
			dependencyBytes.back().data(), (u32)dependencyBytes.back().size());
	}
	modArchiveClose(fixture);

	const std::string output = (temp.path / "creator_theme.pdtheme").string();
	const std::string json = R"JSON({
  "schema":"pd2.theme.v1",
  "catalog_id":"creator:theme_demo",
  "name":"Creator Theme",
  "author":"Test",
  "version":"1",
  "palette":{"dialog_border1":"66ccffff"},
  "menuStyle":"example:tri_reticle",
  "font":"example:tri_theme_font",
  "sounds":{"swipe":"example:tri_click","open":"example:tri_click","focus":"example:tri_click","select":"example:tri_click","error":"example:tri_click","toggle_on":"example:tri_click","toggle_off":"example:tri_click","subfocus":"example:tri_click","keyboard_focus":"example:tri_click","cancel":"example:tri_click","success":"example:tri_click"},
  "menuMusic":"example:tri_song"
})JSON";
	theme_archive_author_request_t request = {
		output.c_str(), "creator:theme_demo", "Creator Theme",
		json.data(), json.size(), dependencies, THEME_ARCHIVE_DEP_COUNT
	};
	char error[512] = {};
	REQUIRE(themeArchiveAuthor(&request, error, sizeof(error)) == 1);

	mod_archive_t *created = modArchiveOpen(output.c_str());
	REQUIRE(created != nullptr);
	REQUIRE(modArchiveFindEntry(created, "theme.ini") >= 0);
	REQUIRE(modArchiveFindEntry(created, "theme.json") >= 0);
	REQUIRE(modArchiveFindEntry(created,
		"dependencies/assets/ui/example_tri_reticle.pdui") >= 0);
	REQUIRE(modArchiveFindEntry(created,
		"dependencies/assets/font/example_tri_theme_font.pdfont") >= 0);
	REQUIRE(modArchiveFindEntry(created,
		"dependencies/assets/audio/example_tri_click.pdsfx") >= 0);
	REQUIRE(modArchiveFindEntry(created,
		"dependencies/assets/music/example_tri_song.pdsong") >= 0);
	const std::string descriptor = archiveText(created, "theme.ini");
	REQUIRE(descriptor.find("catalog_id = creator:theme_demo") != std::string::npos);
	REQUIRE(descriptor.find("ui_archive = dependencies/assets/ui/example_tri_reticle.pdui") != std::string::npos);
	const std::string manifest = archiveText(created, "_meta/manifest.json");
	/* Compatibility metadata may name ui_archive as an inventory role, but
	 * must never duplicate the public descriptor as an authority field. */
	REQUIRE(manifest.find("\"ui_archive\":") == std::string::npos);
	REQUIRE(manifest.find("\"theme_file\":") == std::string::npos);
	modArchiveClose(created);

	const std::vector<unsigned char> before = readFile(output);
	const std::string invalid = R"({"schema":"pd2.theme.v1","catalog_id":"creator:wrong","name":"X","version":"1"})";
	request.theme_json = invalid.data();
	request.theme_json_size = invalid.size();
	REQUIRE(themeArchiveAuthor(&request, error, sizeof(error)) == 0);
	REQUIRE(readFile(output) == before);
}

TEST_CASE("Theme creator rejects duplicate roles and dependency identity drift",
	"[modding][pdxxx][pdtheme][creator][negative][T-ASSETS-029]")
{
	TempDir temp;
	const std::string output = (temp.path / "invalid.pdtheme").string();
	const std::string json = R"({"schema":"pd2.theme.v1","catalog_id":"creator:theme","name":"Theme","version":"1"})";
	theme_archive_dependency_t duplicate[2] = {
		{ THEME_ARCHIVE_DEP_UI, "creator:ui_a", "a.pdui" },
		{ THEME_ARCHIVE_DEP_UI, "creator:ui_b", "b.pdui" },
	};
	theme_archive_author_request_t request = {
		output.c_str(), "creator:theme", "Theme", json.data(), json.size(),
		duplicate, 2
	};
	char error[256] = {};
	REQUIRE(themeArchiveAuthor(&request, error, sizeof(error)) == 0);
	REQUIRE_FALSE(std::filesystem::exists(output));
}
