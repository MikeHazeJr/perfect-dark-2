#include "catch.hpp"

#include <fstream>
#include <cstring>
#include <sstream>
#include <string>

extern "C" {
#include "voice_locale_source.h"
}

static std::string readVoiceSourceFile(const char *path)
{
	std::ifstream stream(path, std::ios::binary);
	std::ostringstream text;
	text << stream.rdbuf();
	return text.str();
}

TEST_CASE("pdvoice subtitle source selects active, fallback, then default locale",
	"[modding][pdxxx][pdvoice][localization][c3842]")
{
	const char *json =
		"{\"schema\":\"pd.voice_subtitle.v1\","
		"\"default\":\"Default line\",\"en\":\"English line\","
		"\"fr\":\"Ligne francaise\",\"metadata\":{\"note\":\"ok\"}}";
	char text[128];

	REQUIRE(voiceSubtitleJsonSelect(json, strlen(json), "fr-FR", "en",
		text, sizeof(text)) == 1);
	REQUIRE(std::string(text) == "Ligne francaise");
	REQUIRE(voiceSubtitleJsonSelect(json, strlen(json), "de", "en-US",
		text, sizeof(text)) == 1);
	REQUIRE(std::string(text) == "English line");
	REQUIRE(voiceSubtitleJsonSelect(json, strlen(json), "de", "",
		text, sizeof(text)) == 1);
	REQUIRE(std::string(text) == "Default line");
}

TEST_CASE("pdvoice subtitle source decodes JSON text and rejects malformed authority",
	"[modding][pdxxx][pdvoice][localization][c3842][negative]")
{
	char text[128];
	const char *escaped =
		"{\"schema\":\"pd.voice_subtitle.v1\",\"en\":\"Line\\nTwo \\u00e9\"}";
	const char *wrong =
		"{\"schema\":\"pd.voice_subtitle.v2\",\"en\":\"wrong\"}";
	const char *wrong_type =
		"{\"schema\":\"pd.voice_subtitle.v1\",\"en\":42}";

	REQUIRE(voiceSubtitleJsonSelect(escaped, strlen(escaped), "en", "",
		text, sizeof(text)) == 1);
	REQUIRE(std::string(text) == std::string("Line\nTwo \xC3\xA9"));
	REQUIRE(voiceSubtitleJsonSelect(wrong, strlen(wrong), "en", "",
		text, sizeof(text)) == -1);
	REQUIRE(voiceSubtitleJsonSelect(wrong_type, strlen(wrong_type), "en", "",
		text, sizeof(text)) == -1);
}

TEST_CASE("pdvoice localized source is wired through every production registration path",
	"[modding][pdxxx][pdvoice][localization][static][c3842]")
{
	const std::string walker = readVoiceSourceFile("port/src/loader_walker_voice.c");
	const std::string scanner = readVoiceSourceFile("port/src/assetcatalog_scanner.c");
	const std::string distrib = readVoiceSourceFile("port/src/net/netdistrib.c");
	const std::string resolver = readVoiceSourceFile("port/src/assetcatalog_load.c");
	const std::string dialogue = readVoiceSourceFile("port/src/scenario_source_runtime.c");

	for (const std::string *source : { &walker, &scanner, &distrib }) {
		REQUIRE(source->find("subtitle_file") != std::string::npos);
		REQUIRE(source->find("locale_en_file") != std::string::npos);
		REQUIRE(source->find("fallback_locale") != std::string::npos);
	}
	REQUIRE(resolver.find("voiceLocaleSelectAudioPath") != std::string::npos);
	REQUIRE(dialogue.find("voiceLocaleActiveTag") != std::string::npos);
	REQUIRE(scanner.find("audioCategoryForSection(ini)") != std::string::npos);
	REQUIRE(distrib.find("distribAudioCategoryForSection(ini)") !=
		std::string::npos);
}
