/**
 * prefs_agent.c -- per-agent preferences sidecar (S309 + S313 + S341 batch)
 *
 * See prefs_agent.h for the overall shape.  Format is plain INI:
 *
 *     [Theme]
 *     ActiveId = user.pokemon.theme
 *
 *     [Video]
 *     UiChromeStyleId  = user.pokemon-chrome.ui-chrome
 *     UiChromeEnabled  = 1
 *     UiTitleBarStyle  = 2
 *     FontId           = user.PokemonFont.font
 *     Scanlines        = 1
 *     ScanlineAlpha    = 0.8
 *
 *     [Audio]
 *     MasterVolume     = 0.85
 *     MusicVolume      = 0.70
 *     GameplayVolume   = 0.90
 *     UIVolume         = 0.75
 *     ModPlaylist      = user.track1.audio;user.track2.audio
 *     ModShuffle       = 1
 *
 *     [Game]
 *     CenterHUD            = 0
 *     SkipIntro            = 0
 *     DisableMpDeathMusic  = 0
 *     GEMuzzleFlashes      = 0
 *     ScreenShakeIntensity = 1.000
 *     MenuMouseControl     = 1
 *
 *     [Updates]
 *     ShowDevReleases      = 0
 *
 *     [Mods]
 *     Enabled = slug1,slug2,slug3
 *
 * Unknown keys are skipped (forward compatibility).  Reads/writes go
 * directly through subsystem accessors — we deliberately do NOT route
 * through configLoad/configSave because per-agent prefs overlay onto
 * global pd.ini rather than replacing it.
 *
 * On Agent Select open, prefsAgentResetVisuals() restores all settings to
 * the pd.ini baselines captured at prefsAgentInit() time so an agent
 * without a sidecar block inherits the machine default rather than the
 * previous agent's value.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <PR/ultratypes.h>

#include "prefs_agent.h"
#include "savefile.h"
#include "fs.h"
#include "system.h"
#include "pdgui_theme_loader.h"
#include "pdgui_theme.h"
#include "pdgui_font_mod.h"
#include "audio.h"
#include "updater.h"

/* Forward declarations for C symbols that live in C++ TUs — mirrors
 * the pattern in savefile.c / main.c. */
extern void        pdguiChromeSetEnabled(s32 enabled);
extern void        pdguiSetPanelNineSlice(const char *catalog_id);

#include "modmgr.h"  /* modinfo_t + modmgrGetCount/GetMod/FindMod/SetEnabled */

/* -----------------------------------------------------------------------
 * Game-layer globals accessed from port code.  Declared extern rather than
 * pulling in heavy game headers.  Types match the definitions in data.h /
 * the respective .c files.
 * --------------------------------------------------------------------- */
extern s32  g_HudCenter;          /* game_1531a0.c */
extern u32  g_HudAlignModeL;      /* game_1531a0.c */
extern u32  g_HudAlignModeR;      /* game_1531a0.c */
extern f32  g_ViShakeIntensityMult; /* pdsched.c */
extern s32  g_MusicDisableMpDeath; /* music.c */
extern s32  g_BgunGeMuzzleFlashes; /* bondgun.c */
extern s32  g_SkipIntro;           /* main.c */
extern s32  g_MenuMouseControl;    /* menu.c */

/* G_ASPECT_* bit-flags (from gbiex.h) used to update g_HudAlignModeL/R
 * when CenterHUD changes.  Hardcoded values — kept in sync by code, not
 * by the compiler, so comment them carefully. */
#define PREFS_ASPECT_LEFT   0x00000010u  /* G_ASPECT_LEFT_EXT  */
#define PREFS_ASPECT_RIGHT  0x00000020u  /* G_ASPECT_RIGHT_EXT */
#define PREFS_ASPECT_WIDE   0x00000040u  /* G_ASPECT_WIDE_EXT  */
#define PREFS_ASPECT_CENTER (PREFS_ASPECT_LEFT | PREFS_ASPECT_RIGHT)

/* HUDCENTER_* constants (from constants.h) */
#define PREFS_HUDCENTER_NONE   0
#define PREFS_HUDCENTER_NORMAL 1
#define PREFS_HUDCENTER_WIDE   2

#define PREFS_MAX_NAME     64
#define PREFS_MAX_PATH     512
#define PREFS_MAX_LINE     1024
#define PREFS_MAX_VAL      256

/* Serialization buffer sizes */
#define PREFS_BUF_SIZE       4096
#define PREFS_PLAYLIST_SIZE  (AUDIO_MAX_PLAYLIST * 65)

static char s_ActiveAgent[PREFS_MAX_NAME] = "";
static s32  s_Initialized = 0;

/* -----------------------------------------------------------------------
 * pd.ini baselines -- gameplay prefs captured at prefsAgentInit() before
 * any per-agent sidecar can overlay them.  prefsAgentResetVisuals() uses
 * these to revert to the machine default when no sidecar block is present.
 * --------------------------------------------------------------------- */
static s32  s_BaseHudCenter            = 0;
static s32  s_BaseSkipIntro            = 0;
static s32  s_BaseDisableMpDeathMusic  = 0;
static s32  s_BaseGEMuzzleFlashes      = 0;
static f32  s_BaseScreenShakeIntensity = 1.0f;
static s32  s_BaseMenuMouseControl     = 1;
static char s_BaseModPlaylist[PREFS_PLAYLIST_SIZE] = "";
static s32  s_BaseModShuffle           = 1;
static s32  s_BaseShowDevReleases      = 0;

/* -----------------------------------------------------------------------
 * HUD centering — setting g_HudCenter also requires updating the render
 * alignment mode flags.  This helper mirrors the logic in optionsmenu.c
 * and main.c so all paths stay consistent.
 * --------------------------------------------------------------------- */
static void applyHudCenter(s32 val)
{
    g_HudCenter = val;
    if (val == PREFS_HUDCENTER_NORMAL) {
        g_HudAlignModeL = PREFS_ASPECT_CENTER;
        g_HudAlignModeR = PREFS_ASPECT_CENTER;
    } else if (val == PREFS_HUDCENTER_WIDE) {
        g_HudAlignModeL = PREFS_ASPECT_LEFT  | PREFS_ASPECT_WIDE;
        g_HudAlignModeR = PREFS_ASPECT_RIGHT | PREFS_ASPECT_WIDE;
    } else { /* PREFS_HUDCENTER_NONE */
        g_HudAlignModeL = PREFS_ASPECT_LEFT;
        g_HudAlignModeR = PREFS_ASPECT_RIGHT;
    }
}

/* -----------------------------------------------------------------------
 * Mod playlist serialization helpers.
 * serializeModPlaylist  -- iterate audioGetModPlaylist* → semicolon string
 * deserializeModPlaylist -- semicolon string → audioClear + audioAdd calls
 * --------------------------------------------------------------------- */
static void serializeModPlaylist(char *out, size_t outmax)
{
    s32 cnt = audioGetModPlaylistCount();
    s32 off = 0;
    out[0] = '\0';
    for (s32 i = 0; i < cnt; i++) {
        const char *e = audioGetModPlaylistEntry(i);
        if (e && e[0]) {
            s32 rem = (s32)outmax - off;
            if (rem <= 1) break;
            off += snprintf(out + off, (size_t)rem,
                            "%s%s", off > 0 ? ";" : "", e);
        }
    }
}

static void deserializeModPlaylist(const char *src)
{
    audioClearModPlaylist();
    if (!src || !src[0]) return;
    char buf[PREFS_PLAYLIST_SIZE];
    snprintf(buf, sizeof(buf), "%s", src);
    char *p = buf;
    while (*p) {
        while (*p == ';' || *p == ' ') p++;
        if (!*p) break;
        char *start = p;
        while (*p && *p != ';') p++;
        char save = *p;
        *p = '\0';
        if (start[0]) audioAddModPlaylistEntry(start);
        *p = save;
        if (save) p++;
    }
}

static void sanitize(const char *src, char *dst, size_t dstmax)
{
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j < dstmax - 1; i++) {
        char c = src[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '_';
        }
    }
    dst[j] = '\0';
}

static void prefsBuildPath(const char *agent_name, char *out, size_t outmax)
{
    char safe[PREFS_MAX_NAME];
    sanitize(agent_name ? agent_name : "", safe, sizeof(safe));
    if (!safe[0]) {
        snprintf(safe, sizeof(safe), "default");
    }
    const char *dir = saveGetDir();
    snprintf(out, outmax, "%s/prefs_%s.ini",
             (dir && dir[0]) ? dir : ".", safe);
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (!*s) return s;
    char *e = s + strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) {
        *e-- = '\0';
    }
    return s;
}

/* -----------------------------------------------------------------------
 * Apply a single key/value pair.
 * Called by prefsAgentLoad per-line.
 * --------------------------------------------------------------------- */

static void applyKV(const char *section, const char *key, const char *val)
{
    if (strcasecmp(section, "Theme") == 0) {
        if (strcasecmp(key, "ActiveId") == 0) {
            if (val[0]) {
                pdguiThemeLoadFromCatalog(val);
            }
            return;
        }
    }
    if (strcasecmp(section, "Video") == 0) {
        if (strcasecmp(key, "UiChromeStyleId") == 0) {
            pdguiThemeSetUiChromeStyleId(val);
            if (val[0]) {
                pdguiSetPanelNineSlice(val);
            }
            return;
        }
        if (strcasecmp(key, "UiChromeEnabled") == 0) {
            s32 v = (s32)atoi(val);
            pdguiThemeSetUiChromeEnabled(v);
            pdguiChromeSetEnabled(v);
            return;
        }
        if (strcasecmp(key, "UiTitleBarStyle") == 0) {
            pdguiThemeSetTitleBarStyle((s32)atoi(val));
            return;
        }
        if (strcasecmp(key, "FontId") == 0) {
            /* Font change takes effect on next restart; setter just
             * updates the stored id so pdguiFontModGetActiveId reflects
             * the loaded profile's choice. */
            pdguiFontModSetActiveId(val);
            return;
        }
        if (strcasecmp(key, "Scanlines") == 0) {
            pdguiThemeSetScanlineEnabled((s32)atoi(val));
            return;
        }
        if (strcasecmp(key, "ScanlineAlpha") == 0) {
            pdguiThemeSetScanlineAlpha((f32)atof(val));
            return;
        }
    }
    if (strcasecmp(section, "Audio") == 0) {
        if (strcasecmp(key, "MasterVolume") == 0) {
            audioSetMasterVolume((f32)atof(val));
            return;
        }
        if (strcasecmp(key, "MusicVolume") == 0) {
            audioSetMusicVolume((f32)atof(val));
            return;
        }
        if (strcasecmp(key, "GameplayVolume") == 0) {
            audioSetGameplayVolume((f32)atof(val));
            return;
        }
        if (strcasecmp(key, "UIVolume") == 0) {
            audioSetUiVolume((f32)atof(val));
            return;
        }
        if (strcasecmp(key, "ModPlaylist") == 0) {
            deserializeModPlaylist(val);
            return;
        }
        if (strcasecmp(key, "ModShuffle") == 0) {
            audioSetModShuffle((s32)atoi(val));
            return;
        }
        if (strcasecmp(key, "ModTrackId") == 0) {
            /* Legacy single-track compat key: only apply if playlist
             * isn't set (the ModPlaylist key takes precedence). */
            if (audioGetModPlaylistCount() == 0 && val[0]) {
                audioSetModTrackId(val);
            }
            return;
        }
    }
    if (strcasecmp(section, "Game") == 0) {
        if (strcasecmp(key, "CenterHUD") == 0) {
            applyHudCenter((s32)atoi(val));
            return;
        }
        if (strcasecmp(key, "SkipIntro") == 0) {
            g_SkipIntro = (s32)atoi(val);
            return;
        }
        if (strcasecmp(key, "DisableMpDeathMusic") == 0) {
            g_MusicDisableMpDeath = (s32)atoi(val);
            return;
        }
        if (strcasecmp(key, "GEMuzzleFlashes") == 0) {
            g_BgunGeMuzzleFlashes = (s32)atoi(val);
            return;
        }
        if (strcasecmp(key, "ScreenShakeIntensity") == 0) {
            g_ViShakeIntensityMult = (f32)atof(val);
            return;
        }
        if (strcasecmp(key, "MenuMouseControl") == 0) {
            g_MenuMouseControl = (s32)atoi(val);
            return;
        }
    }
    if (strcasecmp(section, "Updates") == 0) {
        if (strcasecmp(key, "ShowDevReleases") == 0) {
            updaterSetShowDevReleases((s32)atoi(val));
            return;
        }
    }
    if (strcasecmp(section, "Mods") == 0) {
        if (strcasecmp(key, "Enabled") == 0) {
            /* First pass: disable everything.  Second pass: enable each
             * slug that appears in the comma-separated list.  This is
             * the simplest semantics — "the enabled-mods set is what
             * the agent's prefs declare, full stop". */
            s32 n = modmgrGetCount();
            for (s32 i = 0; i < n; i++) {
                modmgrSetEnabled(i, 0);
            }
            char buf[PREFS_MAX_VAL];
            snprintf(buf, sizeof(buf), "%s", val);
            char *p = buf;
            while (*p) {
                /* Skip spaces */
                while (*p == ' ' || *p == ',') p++;
                if (!*p) break;
                char *start = p;
                while (*p && *p != ',') p++;
                char save = *p;
                *p = '\0';
                for (s32 i = 0; i < n; i++) {
                    const char *id = modmgrGetModId(i);
                    if (id && strcasecmp(id, start) == 0) {
                        modmgrSetEnabled(i, 1);
                        break;
                    }
                }
                *p = save;
                if (save) p++;
            }
            modmgrApplyChanges();
            return;
        }
    }
    /* Unknown section/key — silently skipped so older clients reading
     * newer prefs don't break. */
    (void)key;
    (void)val;
}

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

void prefsAgentInit(void)
{
    if (s_Initialized) return;
    s_Initialized = 1;
    s_ActiveAgent[0] = '\0';

    /* Capture pd.ini baselines before any per-agent sidecar can overlay
     * them.  prefsAgentResetVisuals() restores these values so agents
     * without a given block revert to the machine default. */
    s_BaseHudCenter            = g_HudCenter;
    s_BaseSkipIntro            = g_SkipIntro;
    s_BaseDisableMpDeathMusic  = g_MusicDisableMpDeath;
    s_BaseGEMuzzleFlashes      = g_BgunGeMuzzleFlashes;
    s_BaseScreenShakeIntensity = g_ViShakeIntensityMult;
    s_BaseMenuMouseControl     = g_MenuMouseControl;
    serializeModPlaylist(s_BaseModPlaylist, sizeof(s_BaseModPlaylist));
    s_BaseModShuffle           = audioGetModShuffle();
    s_BaseShowDevReleases      = updaterGetShowDevReleases();

    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: initialised (baselines captured)");
}

void prefsAgentSetActive(const char *agent_name)
{
    if (!agent_name) { s_ActiveAgent[0] = '\0'; return; }
    snprintf(s_ActiveAgent, sizeof(s_ActiveAgent), "%s", agent_name);
    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: active = '%s'", s_ActiveAgent);
}

const char *prefsAgentGetActive(void)
{
    return s_ActiveAgent;
}

void prefsAgentLoad(const char *agent_name)
{
    if (!agent_name || !agent_name[0]) {
        sysLogPrintf(LOG_WARNING, "PREFS.AGENT: prefsAgentLoad called with empty name");
        return;
    }

    char path[PREFS_MAX_PATH];
    prefsBuildPath(agent_name, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) {
        sysLogPrintf(LOG_NOTE, "PREFS.AGENT: no sidecar at %s — global pd.ini defaults apply",
                     path);
        /* Still mark active so subsequent saves route here. */
        prefsAgentSetActive(agent_name);
        return;
    }

    char line[PREFS_MAX_LINE];
    char section[64];
    section[0] = '\0';
    s32 applied = 0;

    while (fgets(line, sizeof(line), f)) {
        char *t = trim(line);
        if (!t[0] || t[0] == '#' || t[0] == ';') continue;

        if (t[0] == '[') {
            char *end = strchr(t, ']');
            if (!end) continue;
            *end = '\0';
            snprintf(section, sizeof(section), "%s", t + 1);
            continue;
        }
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = trim(t);
        char *v = trim(eq + 1);
        applyKV(section, k, v);
        applied++;
    }
    fclose(f);

    prefsAgentSetActive(agent_name);
    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: loaded %d key(s) from %s", applied, path);
}

void prefsAgentMigrateLegacySidecar(const char *raw_name, const char *display_name)
{
    if (!raw_name || !display_name || !display_name[0]) return;

    /* Build new (display-name) path */
    char new_path[PREFS_MAX_PATH];
    prefsBuildPath(display_name, new_path, sizeof(new_path));

    /* Build old path: sanitize raw bytes the same way prefsBuildPath does,
     * which is what pre-S313 code did when it called prefsAgentLoad(file->name). */
    char old_safe[PREFS_MAX_NAME];
    sanitize(raw_name, old_safe, sizeof(old_safe));
    if (!old_safe[0]) {
        snprintf(old_safe, sizeof(old_safe), "default");
    }
    const char *dir = saveGetDir();
    char old_path[PREFS_MAX_PATH];
    snprintf(old_path, sizeof(old_path), "%s/prefs_%s.ini",
             (dir && dir[0]) ? dir : ".", old_safe);

    if (strcmp(old_path, new_path) == 0) return;

    /* Skip if new path already exists — agent already has a current sidecar. */
    FILE *f = fopen(new_path, "r");
    if (f) { fclose(f); return; }

    /* Skip if old path doesn't exist — nothing to migrate. */
    f = fopen(old_path, "r");
    if (!f) return;
    fclose(f);

    if (rename(old_path, new_path) == 0) {
        sysLogPrintf(LOG_NOTE, "PREFS: migrated legacy sidecar '%s' -> '%s'",
                     old_path, new_path);
    } else {
        sysLogPrintf(LOG_WARNING,
                     "PREFS: failed to migrate legacy sidecar '%s' -> '%s' (errno %d)",
                     old_path, new_path, errno);
    }
}

void prefsAgentResetVisuals(void)
{
    /* Reset all per-agent prefs to built-in defaults. Called when Agent Select
     * opens so the screen always shows the unmodified base appearance before
     * any agent is signed in. Per-agent overrides are applied later when the
     * user actually selects an agent. */
    pdguiThemeLoadFromCatalog("base:theme_blue");
    pdguiThemeSetUiChromeEnabled(1);
    pdguiChromeSetEnabled(1);
    pdguiThemeSetUiChromeStyleId("");
    pdguiThemeSetTitleBarStyle(PDGUI_TITLEBAR_CLASSIC);
    pdguiFontModSetActiveId("");
    pdguiThemeSetScanlineEnabled(0);

    /* Restore audio volume layers to pd.ini baseline so agents without an
     * [Audio] block don't inherit the previous agent's volume settings. */
    audioResetToDefaults();

    /* Restore audio mod playlist to pd.ini baseline. */
    deserializeModPlaylist(s_BaseModPlaylist);
    audioSetModShuffle(s_BaseModShuffle);

    /* Restore gameplay prefs to pd.ini baseline (captured at init). */
    applyHudCenter(s_BaseHudCenter);
    g_SkipIntro            = s_BaseSkipIntro;
    g_MusicDisableMpDeath  = s_BaseDisableMpDeathMusic;
    g_BgunGeMuzzleFlashes  = s_BaseGEMuzzleFlashes;
    g_ViShakeIntensityMult = s_BaseScreenShakeIntensity;
    g_MenuMouseControl     = s_BaseMenuMouseControl;

    /* Restore updater prefs to pd.ini baseline. */
    updaterSetShowDevReleases(s_BaseShowDevReleases);

    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: prefs reset to defaults (Agent Select open)");
}

void prefsAgentSave(void)
{
    if (!s_ActiveAgent[0]) {
        return;  /* No active agent — caller should call prefsAgentSetActive first. */
    }

    /* Build the full content in a scratch buffer first so we can debounce:
     * many callers invoke this every frame while a Settings screen is
     * open, but only on an actual value change should we touch disk. */
    char buf[PREFS_BUF_SIZE];
    s32 off = 0;

    /* [Theme] */
    const char *themeId = pdguiThemeGetActiveId();
    off += snprintf(buf + off, sizeof(buf) - off,
                    "[Theme]\nActiveId = %s\n\n",
                    themeId ? themeId : "");

    /* [Video] */
    const char *chromeId = pdguiThemeGetUiChromeStyleId();
    const char *fontId = pdguiFontModGetActiveId();
    off += snprintf(buf + off, sizeof(buf) - off,
                    "[Video]\n"
                    "UiChromeStyleId = %s\n"
                    "UiChromeEnabled = %d\n"
                    "UiTitleBarStyle = %d\n"
                    "FontId = %s\n"
                    "Scanlines = %d\n"
                    "ScanlineAlpha = %.3f\n\n",
                    chromeId ? chromeId : "",
                    (int)pdguiThemeGetUiChromeEnabled(),
                    (int)pdguiThemeGetTitleBarStyle(),
                    fontId ? fontId : "",
                    (int)pdguiThemeGetScanlineEnabled(),
                    (double)pdguiThemeGetScanlineAlpha());

    /* [Audio] -- per-agent volume layers (S313) + mod playlist (S341). */
    char playlist[PREFS_PLAYLIST_SIZE];
    serializeModPlaylist(playlist, sizeof(playlist));
    off += snprintf(buf + off, sizeof(buf) - off,
                    "[Audio]\n"
                    "MasterVolume   = %.3f\n"
                    "MusicVolume    = %.3f\n"
                    "GameplayVolume = %.3f\n"
                    "UIVolume       = %.3f\n"
                    "ModPlaylist    = %s\n"
                    "ModShuffle     = %d\n\n",
                    (double)audioGetMasterVolume(),
                    (double)audioGetMusicVolume(),
                    (double)audioGetGameplayVolume(),
                    (double)audioGetUiVolume(),
                    playlist,
                    (int)audioGetModShuffle());

    /* [Game] -- per-agent gameplay preferences (S341). */
    off += snprintf(buf + off, sizeof(buf) - off,
                    "[Game]\n"
                    "CenterHUD           = %d\n"
                    "SkipIntro           = %d\n"
                    "DisableMpDeathMusic = %d\n"
                    "GEMuzzleFlashes     = %d\n"
                    "ScreenShakeIntensity = %.3f\n"
                    "MenuMouseControl    = %d\n\n",
                    (int)g_HudCenter,
                    (int)g_SkipIntro,
                    (int)g_MusicDisableMpDeath,
                    (int)g_BgunGeMuzzleFlashes,
                    (double)g_ViShakeIntensityMult,
                    (int)g_MenuMouseControl);

    /* [Updates] -- per-agent updater preferences. */
    off += snprintf(buf + off, sizeof(buf) - off,
                    "[Updates]\n"
                    "ShowDevReleases = %d\n\n",
                    (int)updaterGetShowDevReleases());

    /* [Mods] */
    off += snprintf(buf + off, sizeof(buf) - off, "[Mods]\nEnabled = ");
    s32 n = modmgrGetCount();
    s32 first = 1;
    for (s32 i = 0; i < n; i++) {
        modinfo_t *m = modmgrGetMod(i);
        if (!m || !m->enabled || !m->id[0]) continue;
        off += snprintf(buf + off, sizeof(buf) - off, "%s%s",
                        first ? "" : ",", m->id);
        first = 0;
    }
    off += snprintf(buf + off, sizeof(buf) - off, "\n");

    /* Debounce by content hash — skip disk write when nothing changed. */
    static char s_LastBuf[PREFS_BUF_SIZE] = "";
    static char s_LastAgent[PREFS_MAX_NAME] = "";
    if (strcmp(s_LastAgent, s_ActiveAgent) == 0 &&
        strcmp(s_LastBuf, buf) == 0) {
        return;
    }

    char path[PREFS_MAX_PATH];
    prefsBuildPath(s_ActiveAgent, path, sizeof(path));

    FILE *f = fopen(path, "w");
    if (!f) {
        sysLogPrintf(LOG_WARNING, "PREFS.AGENT: cannot write %s (errno %d)", path, errno);
        return;
    }
    fwrite(buf, 1, (size_t)off, f);
    fclose(f);

    snprintf(s_LastAgent, sizeof(s_LastAgent), "%s", s_ActiveAgent);
    snprintf(s_LastBuf,   sizeof(s_LastBuf),   "%s", buf);
    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: saved '%s' to %s (%d bytes)",
                 s_ActiveAgent, path, (int)off);
}
