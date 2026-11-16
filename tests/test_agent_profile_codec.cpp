#include "catch.hpp"

extern "C" {
#include "agent_profile_codec.h"
}

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static agent_profile_document makeProfile(const char *name = "Agent")
{
	agent_profile_document document{};
	std::snprintf(document.name, sizeof(document.name), "%s", name);
	document.totaltime = 123456;
	document.autodifficulty = 2;
	document.autostageindex = 7;
	document.thumbnail = 4;
	for (int stage = 0; stage < AGENT_PROFILE_STAGE_COUNT; stage++) {
		for (int difficulty = 0; difficulty < 3; difficulty++) {
			document.besttimes[stage][difficulty] =
				static_cast<u16>(stage * 10 + difficulty + 1);
		}
	}
	document.coopcompletions[0] = 3;
	document.coopcompletions[1] = 2;
	document.coopcompletions[2] = 1;
	for (int i = 0; i < 9; i++) document.firingrangescores[i] = static_cast<u8>(i);
	for (int i = 0; i < 6; i++) document.weaponsfound[i] = static_cast<u8>(i + 1);
	for (int i = 0; i < 10; i++) document.flags[i] = static_cast<u8>(10 - i);
	document.unk1e = 55;
	document.sfxvolume = 0x4000;
	document.musicvolume = 0x3000;
	document.soundmode = 2;
	document.controlmode[0] = 3;
	document.controlmode[1] = 4;
	for (int challenge = 0; challenge < AGENT_PROFILE_CHALLENGE_COUNT;
			challenge++) {
		for (int players = 0; players < AGENT_PROFILE_PLAYER_COUNTS; players++) {
			document.challengecompleted[challenge][players] =
				static_cast<u8>((challenge + players) & 1);
		}
	}

	auto &prefs = document.preferences;
	std::snprintf(prefs.theme_id, sizeof(prefs.theme_id), "base:theme_blue");
	std::snprintf(prefs.ui_chrome_style_id,
		sizeof(prefs.ui_chrome_style_id), "base:chrome_grid");
	prefs.ui_chrome_enabled = 1;
	prefs.ui_title_bar_style = 2;
	std::snprintf(prefs.font_id, sizeof(prefs.font_id), "base:font_handel");
	prefs.scanlines = 1;
	prefs.scanline_alpha = 0.25f;
	prefs.master_volume = 0.75f;
	prefs.music_volume = 0.5f;
	prefs.gameplay_volume = 0.625f;
	prefs.ui_volume = 0.875f;
	prefs.mod_playlist_count = 2;
	std::snprintf(prefs.mod_playlist[0], sizeof(prefs.mod_playlist[0]),
		"base:music_defection");
	std::snprintf(prefs.mod_playlist[1], sizeof(prefs.mod_playlist[1]),
		"mod_sample:music_alt");
	prefs.mod_shuffle = 1;
	prefs.center_hud = 2;
	prefs.skip_intro = 1;
	prefs.disable_mp_death_music = 1;
	prefs.ge_muzzle_flashes = 1;
	prefs.screen_shake_intensity = 0.8f;
	prefs.menu_mouse_control = 1;
	prefs.show_dev_releases = 1;
	prefs.enabled_mod_count = 2;
	std::snprintf(prefs.enabled_mods[0], sizeof(prefs.enabled_mods[0]),
		"mod.sample.weapon");
	std::snprintf(prefs.enabled_mods[1], sizeof(prefs.enabled_mods[1]),
		"mod.sample.theme");
	return document;
}

static std::string writeProfile(const agent_profile_document &document,
	int *result = nullptr)
{
	FILE *stream = std::tmpfile();
	REQUIRE(stream != nullptr);
	const int write_result = agentProfileWriteJson(stream, &document);
	if (result) *result = write_result;
	std::fflush(stream);
	REQUIRE(std::fseek(stream, 0, SEEK_END) == 0);
	const long length = std::ftell(stream);
	REQUIRE(length >= 0);
	REQUIRE(std::fseek(stream, 0, SEEK_SET) == 0);
	std::vector<char> bytes(static_cast<size_t>(length));
	if (!bytes.empty()) {
		REQUIRE(std::fread(bytes.data(), 1, bytes.size(), stream) == bytes.size());
	}
	std::fclose(stream);
	return std::string(bytes.begin(), bytes.end());
}

static std::string legacyArray(int count, int offset = 0)
{
	std::ostringstream out;
	out << '[';
	for (int i = 0; i < count; i++) {
		if (i) out << ',';
		out << i + offset;
	}
	out << ']';
	return out.str();
}

static std::string legacyBestTimes()
{
	std::ostringstream out;
	out << '[';
	for (int stage = 0; stage < AGENT_PROFILE_STAGE_COUNT; stage++) {
		if (stage) out << ',';
		out << '[' << stage + 1 << ',' << stage + 2 << ',' << stage + 3 << ']';
	}
	out << ']';
	return out.str();
}

static std::string legacyChallenges()
{
	std::ostringstream out;
	out << '[';
	for (int challenge = 0; challenge < AGENT_PROFILE_CHALLENGE_COUNT;
			challenge++) {
		if (challenge) out << ',';
		out << "[0,1,0,1]";
	}
	out << ']';
	return out.str();
}

static std::string legacyProfile(bool transitional, bool include_thumbnail = true,
	bool add_hybrid_field = false)
{
	std::ostringstream out;
	out << "{\"version\":2,\"name\":\"Legacy\",\"totaltime\":99,"
		"\"autodifficulty\":1,\"autostageindex\":3,";
	if (include_thumbnail) out << "\"thumbnail\":2,";
	out << "\"besttimes\":" << legacyBestTimes()
		<< ",\"coopcompletions\":[3,2,1]"
		<< ",\"firingrangescores\":" << legacyArray(9)
		<< ",\"weaponsfound\":" << legacyArray(6, 1);
	if (transitional || add_hybrid_field) {
		out << ",\"flags\":" << legacyArray(10);
	}
	if (transitional) {
		out << ",\"unk1e\":17,\"sfxvolume\":16384,"
			"\"musicvolume\":12288,\"soundmode\":2,"
			"\"controlmode\":[3,4],\"challengecompleted\":"
			<< legacyChallenges();
	}
	out << '}';
	return out.str();
}

static int parseProfile(const std::string &json, const char *expected_name,
	const agent_profile_document &defaults, agent_profile_document &out,
	agent_profile_source_shape &shape, std::string *error_text = nullptr)
{
	char error[256]{};
	const int result = agentProfileParseJson(json.c_str(), expected_name,
		&defaults, &out, &shape, error, sizeof(error));
	if (error_text) *error_text = error;
	return result;
}

TEST_CASE("current Agent Profile round-trips campaign state and every preference",
	"[agent-profile][d-005][save][t-tests-002]")
{
	const agent_profile_document source = makeProfile();
	const std::string json = writeProfile(source);
	agent_profile_document parsed{};
	agent_profile_source_shape shape = AGENT_PROFILE_SOURCE_LEGACY_CORE_V2;

	REQUIRE(parseProfile(json, "Agent", source, parsed, shape) == 0);
	REQUIRE(shape == AGENT_PROFILE_SOURCE_CURRENT);
	REQUIRE(std::strcmp(parsed.name, "Agent") == 0);
	REQUIRE(parsed.totaltime == source.totaltime);
	REQUIRE(parsed.besttimes[20][2] == source.besttimes[20][2]);
	REQUIRE(parsed.challengecompleted[29][3]
		== source.challengecompleted[29][3]);
	REQUIRE(std::strcmp(parsed.preferences.theme_id,
		source.preferences.theme_id) == 0);
	REQUIRE(std::strcmp(parsed.preferences.font_id,
		source.preferences.font_id) == 0);
	REQUIRE(parsed.preferences.master_volume == source.preferences.master_volume);
	REQUIRE(parsed.preferences.mod_playlist_count == 2);
	REQUIRE(std::strcmp(parsed.preferences.mod_playlist[1],
		"mod_sample:music_alt") == 0);
	REQUIRE(parsed.preferences.center_hud == 2);
	REQUIRE(parsed.preferences.show_dev_releases == 1);
	REQUIRE(parsed.preferences.enabled_mod_count == 2);
	REQUIRE(std::strcmp(parsed.preferences.enabled_mods[1],
		"mod.sample.theme") == 0);
}

TEST_CASE("only the two exact historical v2 Agent writer shapes migrate",
	"[agent-profile][d-005][migration][t-tests-002]")
{
	agent_profile_document defaults = makeProfile("Default");
	agent_profile_document parsed{};
	agent_profile_source_shape shape = AGENT_PROFILE_SOURCE_CURRENT;

	REQUIRE(parseProfile(legacyProfile(false), "Legacy", defaults,
		parsed, shape) == 0);
	REQUIRE(shape == AGENT_PROFILE_SOURCE_LEGACY_CORE_V2);
	REQUIRE(parsed.flags[0] == defaults.flags[0]);
	REQUIRE(std::strcmp(parsed.preferences.theme_id,
		defaults.preferences.theme_id) == 0);

	REQUIRE(parseProfile(legacyProfile(true), "Legacy", defaults,
		parsed, shape) == 0);
	REQUIRE(shape == AGENT_PROFILE_SOURCE_LEGACY_TRANSITIONAL_V2);
	REQUIRE(parsed.flags[9] == 9);
	REQUIRE(parsed.sfxvolume == 16384);
	REQUIRE(parsed.controlmode[1] == 4);
	REQUIRE(parsed.challengecompleted[29][3] == 1);

	REQUIRE(parseProfile(legacyProfile(false, false), "Legacy", defaults,
		parsed, shape) == -1);
	REQUIRE(parseProfile(legacyProfile(false, true, true), "Legacy", defaults,
		parsed, shape) == -1);
}

TEST_CASE("Agent Profile parser rejects incomplete unknown duplicate and mistyped data",
	"[agent-profile][d-005][negative][t-tests-002]")
{
	const agent_profile_document defaults = makeProfile();
	const std::string valid = writeProfile(defaults);
	agent_profile_document parsed{};
	agent_profile_source_shape shape = AGENT_PROFILE_SOURCE_CURRENT;

	auto unknown = valid;
	unknown.replace(unknown.find("\"thumbnail\""), 11, "\"mysteryyy\"");
	REQUIRE(parseProfile(unknown, "Agent", defaults, parsed, shape) == -1);

	auto duplicate = valid;
	const size_t name_end = duplicate.find('\n', duplicate.find("\"name\""));
	duplicate.insert(name_end + 1, "  \"name\": \"Agent\",\n");
	REQUIRE(parseProfile(duplicate, "Agent", defaults, parsed, shape) == -1);

	auto missing = valid;
	const size_t font_start = missing.find("    \"font_id\":");
	const size_t font_end = missing.find('\n', font_start);
	missing.erase(font_start, font_end - font_start + 1);
	REQUIRE(parseProfile(missing, "Agent", defaults, parsed, shape) == -1);

	auto wrong_type = valid;
	const size_t scanline = wrong_type.find("\"scanlines\": true");
	REQUIRE(scanline != std::string::npos);
	wrong_type.replace(scanline, std::strlen("\"scanlines\": true"),
		"\"scanlines\": 1");
	REQUIRE(parseProfile(wrong_type, "Agent", defaults, parsed, shape) == -1);

	auto future = valid;
	future.replace(future.find("\"version\": 3"),
		std::strlen("\"version\": 3"), "\"version\": 4");
	REQUIRE(parseProfile(future, "Agent", defaults, parsed, shape) == -1);

	auto truncated = valid;
	truncated.resize(truncated.size() - 8);
	REQUIRE(parseProfile(truncated, "Agent", defaults, parsed, shape) == -1);
	REQUIRE(parseProfile(valid, "Other", defaults, parsed, shape) == -1);
}

TEST_CASE("Agent Profile writer refuses invalid runtime state",
	"[agent-profile][d-005][negative][t-tests-002]")
{
	agent_profile_document invalid = makeProfile();
	int result = 0;
	invalid.challengecompleted[0][0] = 2;
	writeProfile(invalid, &result);
	REQUIRE(result == -1);

	invalid = makeProfile();
	invalid.preferences.enabled_mod_count = 2;
	std::snprintf(invalid.preferences.enabled_mods[1],
		sizeof(invalid.preferences.enabled_mods[1]), "%s",
		invalid.preferences.enabled_mods[0]);
	writeProfile(invalid, &result);
	REQUIRE(result == -1);
}

TEST_CASE("legacy preference sidecar parses completely without mutating defaults",
	"[agent-profile][d-005][migration][ini][t-tests-002]")
{
	const agent_profile_preferences defaults = makeProfile().preferences;
	agent_profile_preferences parsed{};
	char error[256]{};
	const char *ini =
		"[Theme]\nActiveId=mod_theme:ui_blue\n"
		"[Video]\nUiChromeStyleId=mod_theme:chrome_blue\n"
		"UiChromeEnabled=0\nUiTitleBarStyle=3\n"
		"FontId=mod_theme:font_blue\nScanlines=0\nScanlineAlpha=0.4\n"
		"[Audio]\nMasterVolume=0.9\nMusicVolume=0.8\n"
		"GameplayVolume=0.7\nUIVolume=0.6\n"
		"ModPlaylist=base:music_one;mod_music:track_two\nModShuffle=0\n"
		"[Game]\nCenterHUD=1\nSkipIntro=0\nDisableMpDeathMusic=0\n"
		"GEMuzzleFlashes=0\nScreenShakeIntensity=1.25\nMenuMouseControl=0\n"
		"[Updates]\nShowDevReleases=0\n"
		"[Mods]\nEnabled=mod.one,mod.two\n"
		"[Ignored]\nFutureKey=future-value\n";

	REQUIRE(agentProfileParseLegacyPreferencesIni(ini, &defaults, &parsed,
		error, sizeof(error)) == 0);
	REQUIRE(std::strcmp(parsed.theme_id, "mod_theme:ui_blue") == 0);
	REQUIRE(parsed.ui_chrome_enabled == 0);
	REQUIRE(parsed.mod_playlist_count == 2);
	REQUIRE(std::strcmp(parsed.mod_playlist[1], "mod_music:track_two") == 0);
	REQUIRE(parsed.screen_shake_intensity == 1.25f);
	REQUIRE(parsed.enabled_mod_count == 2);
	REQUIRE(std::strcmp(defaults.theme_id, "base:theme_blue") == 0);

	const char *legacy_track = "[Audio]\nModTrackId=base:music_legacy\n";
	REQUIRE(agentProfileParseLegacyPreferencesIni(legacy_track, &defaults,
		&parsed, error, sizeof(error)) == 0);
	REQUIRE(parsed.mod_playlist_count == 1);
	REQUIRE(std::strcmp(parsed.mod_playlist[0], "base:music_legacy") == 0);
}

TEST_CASE("legacy preference sidecar rejects malformed duplicate and invalid values atomically",
	"[agent-profile][d-005][migration][ini][negative][t-tests-002]")
{
	const agent_profile_preferences defaults = makeProfile().preferences;
	agent_profile_preferences output = defaults;
	std::snprintf(output.theme_id, sizeof(output.theme_id), "sentinel:theme");
	char error[256]{};

	REQUIRE(agentProfileParseLegacyPreferencesIni(
		"[Audio]\nMasterVolume=0.5\nMasterVolume=0.6\n",
		&defaults, &output, error, sizeof(error)) == -1);
	REQUIRE(std::strcmp(output.theme_id, "sentinel:theme") == 0);
	REQUIRE(agentProfileParseLegacyPreferencesIni(
		"[Audio]\nMasterVolume=1.5\n",
		&defaults, &output, error, sizeof(error)) == -1);
	REQUIRE(agentProfileParseLegacyPreferencesIni(
		"MasterVolume=0.5\n",
		&defaults, &output, error, sizeof(error)) == -1);
	REQUIRE(agentProfileParseLegacyPreferencesIni(
		"[Mods]\nEnabled=mod.one,MOD.ONE\n",
		&defaults, &output, error, sizeof(error)) == -1);
}
