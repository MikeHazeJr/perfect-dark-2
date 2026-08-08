#ifndef _IN_PDGUI_THEME_LOADER_H
#define _IN_PDGUI_THEME_LOADER_H

/**
 * pdgui_theme_loader.h -- JSON-based theme definition loading (P3 gap)
 *
 * Provides:
 *   - JSON theme file parsing (palette, texture overrides, scanline, glow)
 *   - Theme registration as catalog assets (base:theme_*)
 *   - Hot-reload via catalog ID: pdguiThemeLoadFromCatalog()
 *   - pd.ini persistence for active theme selection
 *
 * Schema (theme.json):
 *   {
 *     "name": "PD Blue",
 *     "author": "Rare / PD2 Team",
 *     "version": "1.0",
 *     "palette": {
 *       "dialog_border1": "0060bf7f",
 *       "dialog_titlebg": "0000507f",
 *       ...all 15 pdgui_palette fields as 8-char hex (RRGGBBAA)
 *     },
 *     "textures": {              // optional
 *       "ui_bg_haze": "textures/custom_haze.tga",
 *       ...slot name → relative file path
 *     },
 *     "scanline": {              // optional
 *       "enabled": true,
 *       "alpha": 0.8,
 *       "interval": 2
 *     },
 *     "textGlow": {              // optional
 *       "enabled": true,
 *       "intensity": 0.6,
 *       "color": "0080ffff"
 *     },
 *     "soundPack": "default"     // optional
 *   }
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 * Part of Phase P3: Visual Theme Layer completion.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Lifecycle
 * --------------------------------------------------------------------- */

/** Initialize theme loader: register pd.ini config var for active theme,
 *  register built-in themes as catalog assets.
 *  Call after pdguiThemeInit() and catalogInit(). */
void pdguiThemeLoaderInit(void);

/** Shutdown: free any loaded theme data. */
void pdguiThemeLoaderShutdown(void);

/** Rescan mods/ for new/updated theme.json files.
 *  Call after modmgrApplyChanges() so newly-installed mod themes appear
 *  without requiring a restart.  Safe to call at any time after init. */
void pdguiThemeRescanMods(void);

/* -----------------------------------------------------------------------
 * Theme loading
 * --------------------------------------------------------------------- */

/** Load and apply a theme by catalog ID (e.g., "base:theme_blue").
 *  Resolves the catalog entry, reads the JSON file, applies palette +
 *  texture overrides + scanline/glow settings.
 *  Returns 1 on success, 0 on failure (theme remains unchanged). */
s32 pdguiThemeLoadFromCatalog(const char *catalog_id);

/** Load a theme from a JSON file path (for mod themes).
 *  Does NOT register in catalog — caller does that.
 *  Returns 1 on success, 0 on failure. */
s32 pdguiThemeLoadFromFile(const char *filepath);

/** Active theme-owned menu music runtime track, or -1 for normal menu music. */
s32 pdguiThemeGetMenuMusicTrack(void);

/** Register a theme.json file at `filepath` under catalog ID `mod:<slug>`.
 *  The display name is read from the JSON's "name" field, falling back to
 *  the slug if the JSON is malformed.  Safe to call from the theme editor
 *  immediately after `saveThemeAsMod()` succeeds so the newly written
 *  theme appears in the Load Theme dropdown and Settings UI Theme
 *  selector without requiring a restart.  Idempotent — calling again
 *  with the same slug is a no-op.
 *  Returns 1 on success, 0 on failure. */
s32 pdguiThemeRegisterModDir(const char *slug, const char *filepath);

/* -----------------------------------------------------------------------
 * Theme enumeration
 * --------------------------------------------------------------------- */

/** Get number of registered theme catalog entries. */
s32 pdguiThemeGetCount(void);

/** Get catalog ID of the Nth registered theme (0-indexed).
 *  Returns NULL if index out of range. */
const char *pdguiThemeGetId(s32 index);

/** Get display name of the Nth registered theme.
 *  Returns NULL if index out of range. */
const char *pdguiThemeGetName(s32 index);

/** S306: Get the on-disk theme.json filepath for a registered theme.
 *  Returns an empty string for built-in themes (no on-disk asset),
 *  NULL if the index is out of range. Used by the Settings → Interface
 *  tab's "Delete Theme" flow to derive the parent mod directory. */
const char *pdguiThemeGetFilePath(s32 index);

/** Get the currently active theme catalog ID.
 *  Returns "base:theme_blue" as default. */
const char *pdguiThemeGetActiveId(void);

/** Check if the Nth registered theme is enabled.
 *  Built-in themes are always enabled.  Mod themes default to enabled
 *  on first detection; user can toggle off via pdguiThemeSetEnabled().
 *  Returns 1 if enabled, 0 if disabled or out of range. */
s32 pdguiThemeIsEnabled(s32 index);

/** Set the enabled flag for a mod theme (no-op for built-in themes).
 *  If disabling the currently active theme, reverts to default.
 *  Persists to pd.ini immediately. */
void pdguiThemeSetEnabled(s32 index, s32 enabled);

/* -----------------------------------------------------------------------
 * Built-in palette index ↔ catalog ID mapping
 * --------------------------------------------------------------------- */

/** Convert a legacy palette index (0-6) to a theme catalog ID.
 *  Returns NULL if index out of range. */
const char *pdguiThemePaletteIndexToId(s32 index);

/** Convert a theme catalog ID to a legacy palette index.
 *  Returns -1 if not a built-in theme. */
s32 pdguiThemeIdToPaletteIndex(const char *catalog_id);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_THEME_LOADER_H */
