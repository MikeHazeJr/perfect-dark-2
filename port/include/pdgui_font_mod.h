#ifndef _IN_PDGUI_FONT_MOD_H
#define _IN_PDGUI_FONT_MOD_H

/**
 * pdgui_font_mod.h -- user-installable font mods (S305 P3)
 *
 * Discovers .ttf / .otf files in mods/Fonts/<slug>/ and exposes them as
 * selectable UI fonts. The active selection is persisted to pd.ini via
 * the Video.FontId key; at startup, pdguiBackendInit consults this
 * module BEFORE building the ImGui font atlas so the user's choice
 * becomes the default font.
 *
 * Install pattern for modders:
 *   mods/Fonts/MyFontName/
 *     font.json         (optional — name/author metadata)
 *     MyFont.ttf        (the actual font file)
 *
 * If no font.json is present, the slug is used as the display name and
 * the first .ttf/.otf file in the directory is picked.
 */

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Register Video.FontId config key and scan mods/Fonts/ for .ttf files.
 *  Idempotent — subsequent calls reset the registry and rescan. Must be
 *  called BEFORE ImGui::AddFontFromFileTTF / AddFontFromMemoryTTF so
 *  the active font can be loaded as the atlas default. */
void pdguiFontModInit(void);

/** Re-run the mods/ scan (after mod install/uninstall). */
void pdguiFontModRescan(void);

/** Number of registered font mods (excludes the built-in Handel Gothic). */
s32 pdguiFontModGetCount(void);

/** Catalog ID for the Nth font mod, e.g. "user.MyFont.font". */
const char *pdguiFontModGetId(s32 index);

/** Display name of the Nth font mod (human-readable). */
const char *pdguiFontModGetName(s32 index);

/** Absolute filesystem path to the Nth font mod's .ttf/.otf file. */
const char *pdguiFontModGetPath(s32 index);

/** The catalog ID of the currently-active font, or "" for the built-in. */
const char *pdguiFontModGetActiveId(void);

/** Select the active font ID. Call pdguiRequestFontAtlasRebuild() after a live
 *  selection change; theme activation does this automatically. */
void pdguiFontModSetActiveId(const char *catalog_id);

/** Resolve the public vector face source for the active font. This may be an
 *  archive::member chain, not only an absolute filesystem path. Returns NULL
 *  for the built-in selection, bitmap-only .pdfont, or missing source. */
const char *pdguiFontModGetActivePath(void);

/* Validate a catalog-backed vector .pdfont source for theme activation.
 * This does not mutate the active selection. */
s32 pdguiFontModValidateCatalogId(const char *catalog_id,
                                  char *error, size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_FONT_MOD_H */
