/**
 * pdgui_leveltool.h -- the Modding Hub's Levels tool.
 *
 * A browser of the catalog's levels (the game's own, and level mods: the ones
 * made here, the ones installed, and templates such as the Blank Room). For
 * the selected level it says what the level holds, opens it in Forge, and
 * makes a new level from it under a name of its own, which Forge then edits
 * and saves in place. This is where level authoring joins the other in-game
 * mod tools (context/designs/modding/forge-mode-scope.md, step F4). The
 * window is port/fast3d/pdgui_menu_leveltool.cpp; its rules are level_tool.h.
 *
 * The hub (pdgui_menu_moddinghub.cpp) owns the frame around it: it calls
 * Refresh when the tool becomes active and Render for its body.
 */
#ifndef _IN_PDGUI_LEVELTOOL_H
#define _IN_PDGUI_LEVELTOOL_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tool became the hub's active tool: read the selection again. */
void pdguiLevelToolRefresh(void);
/* The browser: the list on the left, the selected level on the right. */
void pdguiLevelToolRender(float contentW, float contentH, float scale);

/*
 * --debug-level-tool-new <arena catalog id> with --debug-level-tool-name
 * <name>: the tool's New Level without the mouse. It selects the level as the
 * template and makes the call the Create button makes, on the same state, so
 * a scripted launch proves what a player gets. Returns 1 when the new level
 * was saved and is in the catalog. Opening it in Forge is the Open in Forge
 * button's one call (forgeLevelStartForge), which --launch-forge-level makes
 * once a stage is up.
 */
s32 pdguiLevelToolDebugNew(const char *templateId, const char *displayName);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_LEVELTOOL_H */
