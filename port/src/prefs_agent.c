/**
 * prefs_agent.c -- runtime adapter for unified Agent Profile preferences.
 *
 * The current Agent JSON is the only per-agent persistence target. This file
 * owns machine-baseline capture, pure candidate preparation, deterministic
 * runtime application, active identity publication, and legacy INI ingestion.
 */

#include <PR/ultratypes.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "assetcatalog.h"
#include "audio.h"
#include "fs.h"
#include "modmgr.h"
#include "pdgui_font_mod.h"
#include "pdgui_theme.h"
#include "pdgui_theme_loader.h"
#include "prefs_agent.h"
#include "presence.h"
#include "savefile.h"
#include "social.h"
#include "social_hub.h"
#include "system.h"
#include "updater.h"

extern void pdguiChromeSetEnabled(s32 enabled);
extern void pdguiSetPanelNineSlice(const char *catalog_id);
extern void pdguiRequestFontAtlasRebuild(void);

extern s32 g_HudCenter;
extern u32 g_HudAlignModeL;
extern u32 g_HudAlignModeR;
extern f32 g_ViShakeIntensityMult;
extern s32 g_MusicDisableMpDeath;
extern s32 g_BgunGeMuzzleFlashes;
extern s32 g_SkipIntro;
extern s32 g_MenuMouseControl;

#define PREFS_ASPECT_LEFT   0x00000010u
#define PREFS_ASPECT_RIGHT  0x00000020u
#define PREFS_ASPECT_WIDE   0x00000040u
#define PREFS_ASPECT_CENTER (PREFS_ASPECT_LEFT | PREFS_ASPECT_RIGHT)

#define PREFS_HUDCENTER_NONE   0
#define PREFS_HUDCENTER_NORMAL 1
#define PREFS_HUDCENTER_WIDE   2
#define PREFS_LEGACY_MAX_BYTES (256 * 1024)

static char s_ActiveAgent[AGENT_PROFILE_NAME_MAX];
static s32 s_Initialized;
static s32 s_CurrentValid;
static s32 s_LastSavedValid;
static struct agent_profile_preferences s_Baseline;
static struct agent_profile_preferences s_Current;
static struct agent_profile_preferences s_LastSaved;
static char s_LastSavedAgent[AGENT_PROFILE_NAME_MAX];

static void prefsApplyHudCenter(s32 value)
{
	g_HudCenter = value;
	if (value == PREFS_HUDCENTER_NORMAL) {
		g_HudAlignModeL = PREFS_ASPECT_CENTER;
		g_HudAlignModeR = PREFS_ASPECT_CENTER;
	} else if (value == PREFS_HUDCENTER_WIDE) {
		g_HudAlignModeL = PREFS_ASPECT_LEFT | PREFS_ASPECT_WIDE;
		g_HudAlignModeR = PREFS_ASPECT_RIGHT | PREFS_ASPECT_WIDE;
	} else {
		g_HudAlignModeL = PREFS_ASPECT_LEFT;
		g_HudAlignModeR = PREFS_ASPECT_RIGHT;
	}
}

static s32 prefsStringListContains(const char *values, size_t stride,
		s32 count, const char *value)
{
	for (s32 i = 0; i < count; i++) {
		if (strcasecmp(values + (size_t)i * stride, value) == 0) return 1;
	}
	return 0;
}

static s32 prefsFindMod(const char *id)
{
	if (!id || !id[0]) return -1;
	for (s32 i = 0; i < modmgrGetCount(); i++) {
		const char *candidate = modmgrGetModId(i);
		if (candidate && strcasecmp(candidate, id) == 0) return i;
	}
	return -1;
}

static s32 prefsThemeAvailable(const char *id)
{
	if (!id || !id[0]) return 0;
	for (s32 i = 0; i < pdguiThemeGetCount(); i++) {
		const char *candidate = pdguiThemeGetId(i);
		if (candidate && strcmp(candidate, id) == 0) return 1;
	}
	return 0;
}

static s32 prefsChromeAvailable(const char *id)
{
	if (!id || !id[0]) return 0;
	for (s32 i = 0; i < pdguiThemeGetChromeStyleCount(); i++) {
		const char *candidate = pdguiThemeGetChromeStyleId(i);
		if (candidate && strcmp(candidate, id) == 0) return 1;
	}
	return 0;
}

static s32 prefsFontAvailable(const char *id)
{
	char error[160];
	return !id || !id[0]
		|| pdguiFontModValidateCatalogId(id, error, sizeof(error));
}

static s32 prefsCatalogEntryAvailable(const char *id)
{
	const asset_entry_t *entry;
	if (!id || !id[0]) return 0;
	entry = assetCatalogGetMutable(id);
	return entry && entry->enabled;
}

static void prefsCaptureRuntime(struct agent_profile_preferences *out)
{
	const char *value;

	memset(out, 0, sizeof(*out));
	value = pdguiThemeGetActiveId();
	snprintf(out->theme_id, sizeof(out->theme_id), "%s", value ? value : "");
	value = pdguiThemeGetUiChromeStyleId();
	snprintf(out->ui_chrome_style_id, sizeof(out->ui_chrome_style_id),
		"%s", value ? value : "");
	out->ui_chrome_enabled = pdguiThemeGetUiChromeEnabled() ? 1 : 0;
	out->ui_title_bar_style = (u8)pdguiThemeGetTitleBarStyle();
	value = pdguiFontModGetActiveId();
	snprintf(out->font_id, sizeof(out->font_id), "%s", value ? value : "");
	out->scanlines = pdguiThemeGetScanlineEnabled() ? 1 : 0;
	out->scanline_alpha = pdguiThemeGetScanlineAlpha();

	out->master_volume = audioGetMasterVolume();
	out->music_volume = audioGetMusicVolume();
	out->gameplay_volume = audioGetGameplayVolume();
	out->ui_volume = audioGetUiVolume();
	for (s32 i = 0; i < audioGetModPlaylistCount()
			&& out->mod_playlist_count < AGENT_PROFILE_PLAYLIST_MAX; i++) {
		value = audioGetModPlaylistEntry(i);
		if (!value || !value[0]) continue;
		snprintf(out->mod_playlist[out->mod_playlist_count],
			sizeof(out->mod_playlist[0]), "%s", value);
		out->mod_playlist_count++;
	}
	out->mod_shuffle = audioGetModShuffle() ? 1 : 0;

	out->center_hud = (u8)g_HudCenter;
	out->skip_intro = g_SkipIntro ? 1 : 0;
	out->disable_mp_death_music = g_MusicDisableMpDeath ? 1 : 0;
	out->ge_muzzle_flashes = g_BgunGeMuzzleFlashes ? 1 : 0;
	out->screen_shake_intensity = g_ViShakeIntensityMult;
	out->menu_mouse_control = g_MenuMouseControl ? 1 : 0;
	out->show_dev_releases = updaterGetShowDevReleases() ? 1 : 0;

	for (s32 i = 0; i < modmgrGetCount()
			&& out->enabled_mod_count < AGENT_PROFILE_ENABLED_MODS_MAX; i++) {
		modinfo_t *mod = modmgrGetMod(i);
		if (!mod || !mod->enabled || !mod->id[0]) continue;
		snprintf(out->enabled_mods[out->enabled_mod_count],
			sizeof(out->enabled_mods[0]), "%s", mod->id);
		out->enabled_mod_count++;
	}
}

static void prefsPreserveUnavailableRequests(
		struct agent_profile_preferences *captured)
{
	if (!s_CurrentValid) return;
	if (s_Current.theme_id[0] && !prefsThemeAvailable(s_Current.theme_id)) {
		snprintf(captured->theme_id, sizeof(captured->theme_id), "%s",
			s_Current.theme_id);
	}
	if (s_Current.ui_chrome_style_id[0]
			&& !prefsChromeAvailable(s_Current.ui_chrome_style_id)) {
		snprintf(captured->ui_chrome_style_id,
			sizeof(captured->ui_chrome_style_id), "%s",
			s_Current.ui_chrome_style_id);
	}
	if (s_Current.font_id[0] && !prefsFontAvailable(s_Current.font_id)) {
		snprintf(captured->font_id, sizeof(captured->font_id), "%s",
			s_Current.font_id);
	}
	for (s32 i = 0; i < s_Current.mod_playlist_count
			&& captured->mod_playlist_count < AGENT_PROFILE_PLAYLIST_MAX; i++) {
		const char *id = s_Current.mod_playlist[i];
		if (prefsCatalogEntryAvailable(id)
				|| prefsStringListContains((const char *)captured->mod_playlist,
					AGENT_PROFILE_CATALOG_ID_MAX,
					captured->mod_playlist_count, id)) continue;
		snprintf(captured->mod_playlist[captured->mod_playlist_count],
			sizeof(captured->mod_playlist[0]), "%s", id);
		captured->mod_playlist_count++;
	}
	for (s32 i = 0; i < s_Current.enabled_mod_count
			&& captured->enabled_mod_count < AGENT_PROFILE_ENABLED_MODS_MAX; i++) {
		const char *id = s_Current.enabled_mods[i];
		if (prefsFindMod(id) >= 0
				|| prefsStringListContains((const char *)captured->enabled_mods,
					AGENT_PROFILE_MOD_ID_MAX,
					captured->enabled_mod_count, id)) continue;
		snprintf(captured->enabled_mods[captured->enabled_mod_count],
			sizeof(captured->enabled_mods[0]), "%s", id);
		captured->enabled_mod_count++;
	}
}

static void prefsApplyMods(const struct agent_profile_preferences *candidate)
{
	s32 changed = 0;
	for (s32 i = 0; i < modmgrGetCount(); i++) {
		modinfo_t *mod = modmgrGetMod(i);
		s32 enabled;
		if (!mod || !mod->id[0]) continue;
		enabled = prefsStringListContains((const char *)candidate->enabled_mods,
			AGENT_PROFILE_MOD_ID_MAX, candidate->enabled_mod_count, mod->id);
		if (!!mod->enabled != !!enabled) {
			modmgrSetEnabled(i, enabled);
			changed = 1;
		}
	}
	if (changed) {
		modmgrApplyChangesTransient();
	}
}

static const char *prefsThemeFallback(void)
{
	return s_Baseline.theme_id[0] ? s_Baseline.theme_id : "base:theme_blue";
}

static void prefsApplyRuntime(const struct agent_profile_preferences *candidate)
{
	const char *theme = candidate->theme_id;
	const char *chrome = candidate->ui_chrome_style_id;
	const char *font = candidate->font_id;

	prefsApplyMods(candidate);
	if (!theme[0] || !prefsThemeAvailable(theme)
			|| !pdguiThemeLoadFromCatalog(theme)) {
		theme = prefsThemeFallback();
		if (!pdguiThemeLoadFromCatalog(theme)) {
			sysLogPrintf(LOG_ERROR,
				"PREFS.AGENT: machine fallback theme '%s' failed to apply", theme);
		}
		if (candidate->theme_id[0] && strcmp(candidate->theme_id, theme) != 0) {
			sysLogPrintf(LOG_WARNING,
				"PREFS.AGENT: deferred unavailable theme '%s'; using '%s'",
				candidate->theme_id, theme);
		}
	}

	if (!chrome[0] || !prefsChromeAvailable(chrome)) {
		chrome = s_Baseline.ui_chrome_style_id;
		if (candidate->ui_chrome_style_id[0]) {
			sysLogPrintf(LOG_WARNING,
				"PREFS.AGENT: deferred unavailable chrome '%s'",
				candidate->ui_chrome_style_id);
		}
	}
	pdguiThemeSetUiChromeStyleId(chrome);
	if (chrome && chrome[0]) pdguiSetPanelNineSlice(chrome);
	pdguiThemeSetUiChromeEnabled(candidate->ui_chrome_enabled);
	pdguiChromeSetEnabled(candidate->ui_chrome_enabled);
	pdguiThemeSetTitleBarStyle(candidate->ui_title_bar_style);

	if (!prefsFontAvailable(font)) {
		font = s_Baseline.font_id;
		sysLogPrintf(LOG_WARNING,
			"PREFS.AGENT: deferred unavailable font '%s'",
			candidate->font_id);
	}
	pdguiFontModSetActiveId(font);
	pdguiRequestFontAtlasRebuild();
	pdguiThemeSetScanlineEnabled(candidate->scanlines);
	pdguiThemeSetScanlineAlpha(candidate->scanline_alpha);

	audioSetMasterVolume(candidate->master_volume);
	audioSetMusicVolume(candidate->music_volume);
	audioSetGameplayVolume(candidate->gameplay_volume);
	audioSetUiVolume(candidate->ui_volume);
	audioClearModPlaylist();
	for (s32 i = 0; i < candidate->mod_playlist_count; i++) {
		if (audioAddModPlaylistEntry(candidate->mod_playlist[i]) != 0) {
			sysLogPrintf(LOG_WARNING,
				"PREFS.AGENT: deferred unavailable playlist entry '%s'",
				candidate->mod_playlist[i]);
		}
	}
	audioSetModShuffle(candidate->mod_shuffle);

	prefsApplyHudCenter(candidate->center_hud);
	g_SkipIntro = candidate->skip_intro;
	g_MusicDisableMpDeath = candidate->disable_mp_death_music;
	g_BgunGeMuzzleFlashes = candidate->ge_muzzle_flashes;
	g_ViShakeIntensityMult = candidate->screen_shake_intensity;
	g_MenuMouseControl = candidate->menu_mouse_control;
	updaterApplyShowDevReleases(candidate->show_dev_releases);
}

void prefsAgentInit(void)
{
	char error[160];
	if (s_Initialized) return;
	memset(s_ActiveAgent, 0, sizeof(s_ActiveAgent));
	memset(&s_Current, 0, sizeof(s_Current));
	memset(&s_LastSaved, 0, sizeof(s_LastSaved));
	memset(s_LastSavedAgent, 0, sizeof(s_LastSavedAgent));
	prefsCaptureRuntime(&s_Baseline);
	if (agentProfilePreferencesValidate(&s_Baseline, error, sizeof(error)) != 0) {
		sysLogPrintf(LOG_ERROR,
			"PREFS.AGENT: machine baseline rejected: %s", error);
		memset(&s_Baseline, 0, sizeof(s_Baseline));
		snprintf(s_Baseline.theme_id, sizeof(s_Baseline.theme_id),
			"base:theme_blue");
		s_Baseline.ui_chrome_enabled = 1;
		s_Baseline.master_volume = 1.0f;
		s_Baseline.music_volume = 1.0f;
		s_Baseline.gameplay_volume = 1.0f;
		s_Baseline.ui_volume = 1.0f;
		s_Baseline.mod_shuffle = 1;
		s_Baseline.screen_shake_intensity = 1.0f;
		s_Baseline.menu_mouse_control = 1;
	}
	s_Initialized = 1;
	sysLogPrintf(LOG_NOTE,
		"PREFS.AGENT: unified profile runtime initialized theme='%s' mods=%u",
		s_Baseline.theme_id, s_Baseline.enabled_mod_count);
}

void prefsAgentGetBaselineSnapshot(struct agent_profile_preferences *out)
{
	if (!out) return;
	if (!s_Initialized) prefsAgentInit();
	*out = s_Baseline;
}

void prefsAgentCaptureSnapshot(struct agent_profile_preferences *out)
{
	if (!out) return;
	if (!s_Initialized) prefsAgentInit();
	prefsCaptureRuntime(out);
	prefsPreserveUnavailableRequests(out);
}

s32 prefsAgentPrepareSnapshot(const struct agent_profile_preferences *candidate,
		char *error, size_t error_size)
{
	return agentProfilePreferencesValidate(candidate, error, error_size);
}

void prefsAgentCommitSnapshot(const struct agent_profile_preferences *candidate)
{
	if (!candidate) return;
	if (!s_Initialized) prefsAgentInit();
	prefsApplyRuntime(candidate);
	s_Current = *candidate;
	s_CurrentValid = 1;
}

void prefsAgentPublishActive(const char *agent_name)
{
	if (!agent_name || !agent_name[0] || !s_CurrentValid) return;
	snprintf(s_ActiveAgent, sizeof(s_ActiveAgent), "%s", agent_name);
	s_LastSaved = s_Current;
	s_LastSavedValid = 1;
	snprintf(s_LastSavedAgent, sizeof(s_LastSavedAgent), "%s", agent_name);
	socialRebindToActiveAgent(agent_name);
	socialHubBringOnline();
	presenceMarkAgentLoaded();
	sysLogPrintf(LOG_NOTE,
		"PREFS.AGENT: active identity published name='%s' source=profile-json",
		s_ActiveAgent);
}

const char *prefsAgentGetActive(void)
{
	return s_ActiveAgent;
}

void prefsAgentApplyMachineBaseline(void)
{
	if (!s_Initialized) prefsAgentInit();
	prefsApplyRuntime(&s_Baseline);
	sysLogPrintf(LOG_NOTE,
		"PREFS.AGENT: machine baseline applied for Agent Select");
}

void prefsAgentRefreshVisualsBaseline(void)
{
	const char *value;
	if (!s_Initialized) prefsAgentInit();
	if (s_ActiveAgent[0]) return;
	value = pdguiThemeGetActiveId();
	snprintf(s_Baseline.theme_id, sizeof(s_Baseline.theme_id),
		"%s", value ? value : "base:theme_blue");
	value = pdguiThemeGetUiChromeStyleId();
	snprintf(s_Baseline.ui_chrome_style_id,
		sizeof(s_Baseline.ui_chrome_style_id), "%s", value ? value : "");
	s_Baseline.ui_chrome_enabled = pdguiThemeGetUiChromeEnabled() ? 1 : 0;
	s_Baseline.ui_title_bar_style = (u8)pdguiThemeGetTitleBarStyle();
	value = pdguiFontModGetActiveId();
	snprintf(s_Baseline.font_id, sizeof(s_Baseline.font_id),
		"%s", value ? value : "");
	s_Baseline.scanlines = pdguiThemeGetScanlineEnabled() ? 1 : 0;
	s_Baseline.scanline_alpha = pdguiThemeGetScanlineAlpha();
}

s32 prefsAgentSave(void)
{
	struct agent_profile_preferences captured;
	if (!s_ActiveAgent[0]) return -1;
	prefsAgentCaptureSnapshot(&captured);
	if (s_LastSavedValid && strcmp(s_LastSavedAgent, s_ActiveAgent) == 0
			&& memcmp(&captured, &s_LastSaved, sizeof(captured)) == 0) return 0;
	if (saveSaveAgent(s_ActiveAgent) != 0) {
		sysLogPrintf(LOG_WARNING,
			"PREFS.AGENT: unified save failed name='%s'", s_ActiveAgent);
		return -1;
	}
	s_Current = captured;
	s_CurrentValid = 1;
	s_LastSaved = captured;
	s_LastSavedValid = 1;
	snprintf(s_LastSavedAgent, sizeof(s_LastSavedAgent), "%s", s_ActiveAgent);
	return 0;
}

static void prefsBuildLegacyPath(const char *agent_name, char *out,
		size_t out_size)
{
	char safe[AGENT_PROFILE_NAME_MAX];
	size_t count = 0;
	const char *dir = saveGetDir();

	for (size_t i = 0; agent_name && agent_name[i]
			&& count + 1 < sizeof(safe); i++) {
		char c = agent_name[i];
		if (isalnum((unsigned char)c) || c == '_' || c == '-') safe[count++] = c;
		else if (c == ' ') safe[count++] = '_';
	}
	safe[count] = '\0';
	snprintf(out, out_size, "%s/prefs_%s.ini",
		dir && dir[0] ? dir : ".", safe);
}

static char *prefsReadLegacyFile(const char *path)
{
	FILE *stream = fopen(path, "rb");
	long length;
	char *data;
	if (!stream) return NULL;
	if (fseek(stream, 0, SEEK_END) != 0
			|| (length = ftell(stream)) < 0
			|| length > PREFS_LEGACY_MAX_BYTES
			|| fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return NULL;
	}
	data = (char *)malloc((size_t)length + 1);
	if (!data) {
		fclose(stream);
		return NULL;
	}
	if (fread(data, 1, (size_t)length, stream) != (size_t)length) {
		free(data);
		fclose(stream);
		return NULL;
	}
	data[length] = '\0';
	fclose(stream);
	return data;
}

s32 prefsAgentReadLegacySidecar(const char *agent_name,
		const struct agent_profile_preferences *defaults,
		struct agent_profile_preferences *out, s32 *found,
		char *error, size_t error_size)
{
	char path[FS_MAXPATH + 1];
	char *data;
	FILE *probe;

	if (error && error_size) error[0] = '\0';
	if (!agent_name || !defaults || !out || !found) return -1;
	*out = *defaults;
	*found = 0;
	prefsBuildLegacyPath(agent_name, path, sizeof(path));
	probe = fopen(path, "rb");
	if (!probe) {
		if (errno == ENOENT) return 0;
		if (error && error_size) snprintf(error, error_size,
			"cannot open legacy preference sidecar (errno %d)", errno);
		return -1;
	}
	fclose(probe);
	data = prefsReadLegacyFile(path);
	if (!data) {
		if (error && error_size) snprintf(error, error_size,
			"cannot read bounded legacy preference sidecar");
		return -1;
	}
	if (agentProfileParseLegacyPreferencesIni(data, defaults, out,
			error, error_size) != 0) {
		free(data);
		return -1;
	}
	free(data);
	*found = 1;
	return 0;
}

void prefsAgentRetireLegacySidecar(const char *agent_name)
{
	char path[FS_MAXPATH + 1];
	if (!agent_name || !agent_name[0]) return;
	prefsBuildLegacyPath(agent_name, path, sizeof(path));
	if (remove(path) == 0) {
		sysLogPrintf(LOG_NOTE,
			"PREFS.AGENT: retired migrated legacy sidecar '%s'", path);
	} else if (errno != ENOENT) {
		sysLogPrintf(LOG_WARNING,
			"PREFS.AGENT: stale legacy sidecar retirement deferred path='%s' errno=%d",
			path, errno);
	}
}
