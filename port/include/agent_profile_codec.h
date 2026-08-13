/**
 * agent_profile_codec.h -- pure versioned Agent Profile document contract.
 *
 * The current document owns campaign state and all per-agent preferences in
 * one JSON file. The codec has no game-runtime dependencies, so the exact
 * production parser and writer can also run under pd-tests.
 */

#ifndef _IN_AGENT_PROFILE_CODEC_H
#define _IN_AGENT_PROFILE_CODEC_H

#include <PR/ultratypes.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AGENT_PROFILE_VERSION 3
#define AGENT_PROFILE_LEGACY_VERSION 2

#define AGENT_PROFILE_NAME_LENGTH_MAX 10
#define AGENT_PROFILE_NAME_MAX (AGENT_PROFILE_NAME_LENGTH_MAX + 1)
#define AGENT_PROFILE_STAGE_COUNT 21
#define AGENT_PROFILE_CHALLENGE_COUNT 30
#define AGENT_PROFILE_PLAYER_COUNTS 4
#define AGENT_PROFILE_CATALOG_ID_MAX 64
#define AGENT_PROFILE_MOD_ID_MAX 64
#define AGENT_PROFILE_PLAYLIST_MAX 16
#define AGENT_PROFILE_ENABLED_MODS_MAX 32

enum agent_profile_source_shape {
	AGENT_PROFILE_SOURCE_CURRENT = 0,
	AGENT_PROFILE_SOURCE_LEGACY_CORE_V2 = 1,
	AGENT_PROFILE_SOURCE_LEGACY_TRANSITIONAL_V2 = 2,
};

struct agent_profile_preferences {
	char theme_id[AGENT_PROFILE_CATALOG_ID_MAX];
	char ui_chrome_style_id[AGENT_PROFILE_CATALOG_ID_MAX];
	u8 ui_chrome_enabled;
	u8 ui_title_bar_style;
	char font_id[AGENT_PROFILE_CATALOG_ID_MAX];
	u8 scanlines;
	f32 scanline_alpha;

	f32 master_volume;
	f32 music_volume;
	f32 gameplay_volume;
	f32 ui_volume;
	u8 mod_playlist_count;
	char mod_playlist[AGENT_PROFILE_PLAYLIST_MAX][AGENT_PROFILE_CATALOG_ID_MAX];
	u8 mod_shuffle;

	u8 center_hud;
	u8 skip_intro;
	u8 disable_mp_death_music;
	u8 ge_muzzle_flashes;
	f32 screen_shake_intensity;
	u8 menu_mouse_control;
	u8 show_dev_releases;

	u8 enabled_mod_count;
	char enabled_mods[AGENT_PROFILE_ENABLED_MODS_MAX][AGENT_PROFILE_MOD_ID_MAX];
};

struct agent_profile_document {
	char name[AGENT_PROFILE_NAME_MAX];
	u32 totaltime;
	u8 autodifficulty;
	u8 autostageindex;
	u8 thumbnail;
	u16 besttimes[AGENT_PROFILE_STAGE_COUNT][3];
	s32 coopcompletions[3];
	u8 firingrangescores[9];
	u8 weaponsfound[6];
	u8 flags[10];
	u16 unk1e;
	u16 sfxvolume;
	u16 musicvolume;
	s32 soundmode;
	u8 controlmode[2];
	u8 challengecompleted[AGENT_PROFILE_CHALLENGE_COUNT][AGENT_PROFILE_PLAYER_COUNTS];
	struct agent_profile_preferences preferences;
};

/** Validate all preference scalar, identifier, count, and uniqueness rules. */
s32 agentProfilePreferencesValidate(
		const struct agent_profile_preferences *preferences,
		char *error, size_t error_size);

/**
 * Parse one complete current profile or one exact known v2 writer shape.
 * legacy_defaults supplies fields that the historical core v2 writer did not
 * persist, including the machine preference baseline. No output is committed
 * unless the complete document passes.
 */
s32 agentProfileParseJson(const char *json, const char *expected_name,
		const struct agent_profile_document *legacy_defaults,
		struct agent_profile_document *out,
		enum agent_profile_source_shape *out_shape,
		char *error, size_t error_size);

/** Write the complete current profile schema to an already-open stream. */
s32 agentProfileWriteJson(FILE *stream,
		const struct agent_profile_document *document);

/**
 * Parse an optional legacy prefs_<agent>.ini overlay into a candidate. This
 * function never mutates runtime state. Missing keys retain defaults; malformed
 * or duplicate recognized keys reject the whole sidecar.
 */
s32 agentProfileParseLegacyPreferencesIni(const char *ini,
		const struct agent_profile_preferences *defaults,
		struct agent_profile_preferences *out,
		char *error, size_t error_size);

#ifdef __cplusplus
}
#endif

#endif /* _IN_AGENT_PROFILE_CODEC_H */
