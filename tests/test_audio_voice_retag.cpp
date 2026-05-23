/*
 * tests/test_audio_voice_retag.cpp -- Catalog Phase 3 Pass B Slice 10
 * voice retag static-contract pins.
 *
 * Source under test:
 *   port/src/assetcatalog_base_extended.c (registration loop +
 *     s_audioConfigIsVoice helper)
 *   src/lib/snd.c (g_NumAudioRussMappings symbol)
 *   src/include/data.h (extern decl for the count)
 *
 * The runtime retag walks g_AudioRussMappings[] and re-categorises
 * SFX entries whose audioconfig falls into one of seven voice slots.
 * Static-text grep pins the criteria + the integration so a future
 * edit cannot silently lose the retag or change the slot set.
 *
 * @SYNC: any change to the voice-config slot set or the registration
 *        integration shape must update this file.
 */

#include "catch.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readFile(const char *path) {
	std::ifstream in(path, std::ios::in | std::ios::binary);
	REQUIRE(in.good());
	std::ostringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

}  /* anonymous namespace */

TEST_CASE("voice-retag: helper s_audioConfigIsVoice exists",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(src.find("s_audioConfigIsVoice") != std::string::npos);
	REQUIRE(src.find("static s32 s_audioConfigIsVoice(") != std::string::npos);
}

TEST_CASE("voice-retag: all seven voice config slots covered",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	/* Each enumerator must appear in the helper's case list. The seven
	 * voice configs per the audit (Section B.2):
	 *   AUDIOCONFIG_01 mission briefings
	 *   AUDIOCONFIG_02 NPC combat barks (OFFENSIVE)
	 *   AUDIOCONFIG_03 Carrington urgent
	 *   AUDIOCONFIG_47 scripted scene dialogue
	 *   AUDIOCONFIG_48 programmer cluster
	 *   AUDIOCONFIG_60 NPC greetings (RESPONDHELLO)
	 *   AUDIOCONFIG_62 death scream (NTSC-1.0+ only)
	 */
	REQUIRE(src.find("case AUDIOCONFIG_01:") != std::string::npos);
	REQUIRE(src.find("case AUDIOCONFIG_02:") != std::string::npos);
	REQUIRE(src.find("case AUDIOCONFIG_03:") != std::string::npos);
	REQUIRE(src.find("case AUDIOCONFIG_47:") != std::string::npos);
	REQUIRE(src.find("case AUDIOCONFIG_48:") != std::string::npos);
	REQUIRE(src.find("case AUDIOCONFIG_60:") != std::string::npos);
	REQUIRE(src.find("case AUDIOCONFIG_62:") != std::string::npos);
}

TEST_CASE("voice-retag: AUDIOCONFIG_62 is VERSION-guarded",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	/* CONFIG_62 only exists on NTSC-1.0+ builds; the russ entries that
	 * reference it are gated on the same VERSION macro. The retag's
	 * predicate must be #if-guarded the same way for version
	 * determinism. */
	const std::string needle = "#if VERSION >= VERSION_NTSC_1_0\n\tcase AUDIOCONFIG_62:";
	REQUIRE(src.find(needle) != std::string::npos);
}

TEST_CASE("voice-retag: registration loop passes category to RegisterAudio",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	/* Pre-Slice-10 the SFX loop hardcoded category=0 in the call. The
	 * retag adds a `s32 category = AUDIO_CAT_SFX;` local + conditional
	 * upgrade to AUDIO_CAT_VOICE. Pin the upgrade path. */
	REQUIRE(src.find("s32 category = AUDIO_CAT_SFX;") != std::string::npos);
	REQUIRE(src.find("category = AUDIO_CAT_VOICE;") != std::string::npos);
	/* The RegisterAudio call site now passes `category` (variable),
	 * not a hardcoded 0. Pin the new shape. */
	REQUIRE(src.find("idbuf, i, \"\", category, 0, \"\"") != std::string::npos);
}

TEST_CASE("voice-retag: registration loop consults g_AudioRussMappings",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(src.find("g_AudioRussMappings[i]") != std::string::npos);
	REQUIRE(src.find("audioconfig_index") != std::string::npos);
	REQUIRE(src.find("g_NumAudioRussMappings") != std::string::npos);
}

TEST_CASE("voice-retag: PD_SERVER guard suppresses retag on server build",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	/* Server build doesn't link snd.c, so the russ-table reference is
	 * gated on !PD_SERVER. Server registers everything as SFX which is
	 * correct (server has no audio runtime). */
	REQUIRE(src.find("#if !defined(PD_SERVER)") != std::string::npos);
}

TEST_CASE("voice-retag: log line splits SFX vs VOICE counts",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string src = readFile("port/src/assetcatalog_base_extended.c");
	REQUIRE(src.find("base audio entries") != std::string::npos);
	REQUIRE(src.find("SFX +") != std::string::npos);
	REQUIRE(src.find("VOICE)") != std::string::npos);
}

TEST_CASE("voice-retag: g_NumAudioRussMappings symbol shipped from snd.c",
          "[catalog-audio-voice][gate3][slice10]") {
	std::string snd = readFile("src/lib/snd.c");
	REQUIRE(snd.find("const s32 g_NumAudioRussMappings") != std::string::npos);
	REQUIRE(snd.find("sizeof(g_AudioRussMappings) / sizeof(g_AudioRussMappings[0])") != std::string::npos);

	std::string data = readFile("src/include/data.h");
	REQUIRE(data.find("extern const s32 g_NumAudioRussMappings") != std::string::npos);
}

TEST_CASE("tiny voice pitch: Tiny Mode cheat scales voice-config starts",
          "[audio][voice][tiny][static]") {
	std::string snd = readFile("src/lib/snd.c");

	REQUIRE(snd.find("#include \"game/cheats.h\"") != std::string::npos);
	REQUIRE(snd.find("SND_TINY_VOICE_PITCH_SCALE 1.12f") != std::string::npos);
	REQUIRE(snd.find("sndSoundRefHasVoiceConfig") != std::string::npos);
	REQUIRE(snd.find("cheatIsActive(CHEAT_SMALLJO)") != std::string::npos);
	REQUIRE(snd.find("sndApplyTinyVoicePitch(sound, PSTYPE_NONE, pitch)") != std::string::npos);
}

TEST_CASE("tiny voice pitch: character talk channels get the same start pitch",
          "[audio][voice][tiny][static]") {
	std::string propsnd = readFile("src/game/propsnd.c");
	std::string header = readFile("src/include/lib/snd.h");

	REQUIRE(header.find("sndApplyTinyVoicePitch") != std::string::npos);
	REQUIRE(propsnd.find("sndApplyTinyVoicePitch(channel->soundnum26, channel->type, newpitch)") != std::string::npos);
	REQUIRE(propsnd.find("channel->soundnum26, startpitch, channel->fxbus") != std::string::npos);
}
