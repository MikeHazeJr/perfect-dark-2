/**
 * prefs_agent.c -- per-agent preferences sidecar (S309 + S313 batch)
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
 *
 *     [Mods]
 *     Enabled = slug1,slug2,slug3
 *
 * Unknown keys are skipped (forward compatibility).  Reads/writes go
 * directly through subsystem accessors — we deliberately do NOT route
 * through configLoad/configSave because per-agent prefs overlay onto
 * global pd.ini rather than replacing it.
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

/* Forward declarations for C symbols that live in C++ TUs — mirrors
 * the pattern in savefile.c / main.c. */
extern void        pdguiChromeSetEnabled(s32 enabled);
extern void        pdguiSetPanelNineSlice(const char *catalog_id);

#include "modmgr.h"  /* modinfo_t + modmgrGetCount/GetMod/FindMod/SetEnabled */

#define PREFS_MAX_NAME     64
#define PREFS_MAX_PATH     512
#define PREFS_MAX_LINE     1024
#define PREFS_MAX_VAL      256

static char s_ActiveAgent[PREFS_MAX_NAME] = "";
static s32  s_Initialized = 0;

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
    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: initialised");
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
    /* Reset all per-agent visual prefs to built-in defaults. Called when
     * Agent Select opens so the screen always shows the unmodified base
     * appearance before any agent is signed in. Per-agent theme/chrome/font
     * are applied later when the user actually selects an agent. */
    pdguiThemeLoadFromCatalog("base:theme_blue");
    pdguiThemeSetUiChromeEnabled(1);
    pdguiChromeSetEnabled(1);
    pdguiThemeSetUiChromeStyleId("");
    pdguiThemeSetTitleBarStyle(PDGUI_TITLEBAR_CLASSIC);
    pdguiFontModSetActiveId("");
    pdguiThemeSetScanlineEnabled(0);
    sysLogPrintf(LOG_NOTE, "PREFS.AGENT: visuals reset to defaults (Agent Select open)");
}

void prefsAgentSave(void)
{
    if (!s_ActiveAgent[0]) {
        return;  /* No active agent — caller should call prefsAgentSetActive first. */
    }

    /* Build the full content in a scratch buffer first so we can debounce:
     * many callers invoke this every frame while a Settings screen is
     * open, but only on an actual value change should we touch disk. */
    char buf[2048];
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

    /* [Audio] -- per-agent volume layers (S313 batch).  Global pd.ini
     * still holds the same keys as fallback defaults; the per-agent
     * sidecar overlays them on Agent load. */
    off += snprintf(buf + off, sizeof(buf) - off,
                    "[Audio]\n"
                    "MasterVolume   = %.3f\n"
                    "MusicVolume    = %.3f\n"
                    "GameplayVolume = %.3f\n"
                    "UIVolume       = %.3f\n\n",
                    (double)audioGetMasterVolume(),
                    (double)audioGetMusicVolume(),
                    (double)audioGetGameplayVolume(),
                    (double)audioGetUiVolume());

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
    static char s_LastBuf[2048] = "";
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
