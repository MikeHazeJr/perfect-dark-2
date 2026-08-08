#include "catch.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "asset_archive_policy.h"
#include "modarchive.h"
#include "voice_archive_authoring.h"
}

namespace {

struct VoiceAuthorTempDir {
	std::filesystem::path path;
	explicit VoiceAuthorTempDir(std::filesystem::path value) : path(std::move(value))
	{
		std::filesystem::create_directories(path);
	}
	VoiceAuthorTempDir(const VoiceAuthorTempDir &) = delete;
	VoiceAuthorTempDir &operator=(const VoiceAuthorTempDir &) = delete;
	VoiceAuthorTempDir(VoiceAuthorTempDir &&other) noexcept
		: path(std::move(other.path)) {}
	~VoiceAuthorTempDir() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};

static VoiceAuthorTempDir makeVoiceAuthorTempDir()
{
	auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
	return VoiceAuthorTempDir{
		std::filesystem::temp_directory_path()
			/ ("pd2-voice-author-" + std::to_string(stamp))
	};
}

static void writeTinyWav(const std::filesystem::path &path)
{
	const unsigned char wav[] = {
		'R','I','F','F', 40,0,0,0, 'W','A','V','E',
		'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
		0x44,0xac,0,0, 0x88,0x58,0x01,0, 2,0, 16,0,
		'd','a','t','a', 4,0,0,0, 0,0, 0,0
	};
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(wav), sizeof(wav));
}

static std::string archiveText(const std::filesystem::path &archive_path,
	const char *entry_name)
{
	mod_archive_t *archive = modArchiveOpen(archive_path.string().c_str());
	REQUIRE(archive != nullptr);
	s32 index = modArchiveFindEntry(archive, entry_name);
	REQUIRE(index >= 0);
	u32 size = 0;
	char *bytes = static_cast<char *>(modArchiveExtractAlloc(archive, index, &size));
	REQUIRE(bytes != nullptr);
	std::string text(bytes, bytes + size);
	free(bytes);
	modArchiveClose(archive);
	return text;
}

static std::vector<char> readBytes(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::vector<char>(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}

static std::string readSource(const char *path)
{
	std::ifstream in(path, std::ios::binary);
	REQUIRE(in.good());
	return std::string(std::istreambuf_iterator<char>(in),
		std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("Voice creator emits a self-contained localized pdvoice through the shared writer",
	"[modding][pdxxx][pdvoice][creator][c3842]")
{
	auto temp = makeVoiceAuthorTempDir();
	auto source = temp.path / "line.wav";
	auto archive = temp.path / "voice_line.pdvoice";
	writeTinyWav(source);
	char error[256];
	std::string archive_path = archive.string();
	std::string source_path = source.string();
	voice_archive_author_request_t request = {
		archive_path.c_str(),
		"mod_voice_test:voice_line",
		"Creator Voice",
		source_path.c_str(),
		"Joanna",
		"Test chamber",
		"Ready \xF0\x9F\x8E\xA4",
		1234,
		"fr",
		"en"
	};

	REQUIRE(voiceArchiveAuthor(&request, error, sizeof(error)) == 1);
	REQUIRE(std::filesystem::exists(archive));
	REQUIRE_FALSE(std::filesystem::exists(
		std::filesystem::path(archive.string() + ".candidate.pdvoice")));
	REQUIRE(assetArchiveValidateFile(archive.string().c_str(),
		ASSET_ARCHIVE_VALIDATE_RELEASE, error, sizeof(error)) == 0);

	const std::string descriptor = archiveText(archive, "voice.ini");
	const std::string subtitle = archiveText(archive, "subtitle.json");
	REQUIRE(descriptor.find("catalog_id = mod_voice_test:voice_line") != std::string::npos);
	REQUIRE(descriptor.find("audio_category = voice") != std::string::npos);
	REQUIRE(descriptor.find("duration_ms = 1234") != std::string::npos);
	REQUIRE(descriptor.find("locale_fr_file = locales/fr.wav") != std::string::npos);
	REQUIRE(descriptor.find("subtitle_file = subtitle.json") != std::string::npos);
	REQUIRE(subtitle.find("\"fr\": \"Ready \xF0\x9F\x8E\xA4\"") != std::string::npos);

	mod_archive_t *opened = modArchiveOpen(archive.string().c_str());
	REQUIRE(opened != nullptr);
	REQUIRE(modArchiveFindEntry(opened, "sample.wav") >= 0);
	REQUIRE(modArchiveFindEntry(opened, "locales/fr.wav") >= 0);
	REQUIRE(modArchiveFindEntry(opened, "_meta/inventory.json") >= 0);
	REQUIRE(modArchiveFindEntry(opened, "_meta/hashes.json") >= 0);
	modArchiveClose(opened);
}

TEST_CASE("Voice creator rejects invalid edits without replacing the last good archive",
	"[modding][pdxxx][pdvoice][creator][negative][atomic]")
{
	auto temp = makeVoiceAuthorTempDir();
	auto source = temp.path / "line.wav";
	auto archive = temp.path / "voice_line.pdvoice";
	writeTinyWav(source);
	char error[256];
	std::string archive_path = archive.string();
	std::string source_path = source.string();
	voice_archive_author_request_t valid = {
		archive_path.c_str(), "mod_voice_test:voice_line", "Creator Voice",
		source_path.c_str(), "Joanna", "Test chamber", "Ready", 1, "en", "en"
	};
	REQUIRE(voiceArchiveAuthor(&valid, error, sizeof(error)) == 1);
	const std::vector<char> before = readBytes(archive);

	voice_archive_author_request_t invalid = valid;
	invalid.actor = "Bad\nActor";
	REQUIRE(voiceArchiveAuthor(&invalid, error, sizeof(error)) == 0);
	REQUIRE(std::string(error).find("required") != std::string::npos);
	REQUIRE(readBytes(archive) == before);

	invalid = valid;
	std::string missing_path = (temp.path / "missing.wav").string();
	invalid.source_audio_path = missing_path.c_str();
	REQUIRE(voiceArchiveAuthor(&invalid, error, sizeof(error)) == 0);
	REQUIRE(readBytes(archive) == before);
}

TEST_CASE("Audio Mods Voice UI uses archive authority and dynamic menu glyphs",
	"[modding][pdxxx][pdvoice][creator][menu][input][glyphs][static]")
{
	const std::string menu = readSource("port/fast3d/pdgui_menu_audiomod.cpp");
	const size_t voice_begin = menu.find("static bool importVoiceArchive");
	const size_t generic_begin = menu.find("/**\n * Import an audio file as a new mod component.");
	REQUIRE(voice_begin != std::string::npos);
	REQUIRE(generic_begin != std::string::npos);
	REQUIRE(voice_begin < generic_begin);
	const std::string voice_path = menu.substr(voice_begin,
		generic_begin - voice_begin);
	REQUIRE(voice_path.find("voiceArchiveAuthor(&request") != std::string::npos);
	REQUIRE(voice_path.find("assetCatalogScanExternalLayoutFolder(modId, modDir)") !=
		std::string::npos);
	REQUIRE(voice_path.find("assetCatalogRegisterAudio") == std::string::npos);
	REQUIRE(voice_path.find("audio.ini") == std::string::npos);
	REQUIRE(voice_path.find("netDistribServerRebroadcastCatalog") != std::string::npos);
	REQUIRE(menu.find("pdguiGlyphGetActionLabel(ACTION_MENU_ACCEPT") !=
		std::string::npos);
	REQUIRE(menu.find("pdguiGlyphGetActionLabel(ACTION_MENU_CANCEL") !=
		std::string::npos);
	REQUIRE(menu.find("InputTextMultiline(\"##aud_voice_subtitle\"") !=
		std::string::npos);
	REQUIRE(menu.find("Combo(\"Audio locale##aud_voice\"") != std::string::npos);
	REQUIRE(menu.find("Combo(\"Fallback##aud_voice\"") != std::string::npos);
}
