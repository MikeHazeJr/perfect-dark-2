#ifndef _IN_PDGUI_MENU_THEME_EDITOR_H
#define _IN_PDGUI_MENU_THEME_EDITOR_H

/**
 * pdgui_menu_theme_editor.h -- Theme palette editor UI (P5)
 *
 * Full-screen theme editor with:
 *   - Per-element color pickers for all 15 palette fields
 *   - Live preview (changes apply immediately)
 *   - Reset to base palette
 *   - Load from existing theme (registered catalog assets)
 *   - Save as mod (creates mods/<name>/ with mod.json + theme.json)
 *
 * Show/Hide/Render pattern matches pdgui_menu_moddinghub.
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Open the theme editor. */
void pdguiThemeEditorShow(void);

/** Close the theme editor, restoring original palette if not saved. */
void pdguiThemeEditorHide(void);

/** Returns 1 if the theme editor is currently visible. */
s32 pdguiThemeEditorIsVisible(void);

/** Render the theme editor. Called every frame from pdgui_backend.cpp. */
void pdguiThemeEditorRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MENU_THEME_EDITOR_H */
