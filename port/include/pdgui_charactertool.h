/**
 * pdgui_charactertool.h -- the Modding Hub's Characters tool.
 *
 * A browser of the catalog's characters, bodies and heads, and a creator that
 * saves one of them as a new mod through the mod author: a name for all
 * three, and gender, build and height for a body or a head. The window is
 * port/fast3d/pdgui_menu_charactertool.cpp; its rules are character_tool.h.
 *
 * The hub (pdgui_menu_moddinghub.cpp) owns the frame around it: it calls
 * Refresh when the tool becomes active, Render for the browser, and, while
 * CreatorOpen says so, RenderCreator instead of its own tool bar and body.
 */
#ifndef _IN_PDGUI_CHARACTERTOOL_H
#define _IN_PDGUI_CHARACTERTOOL_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The tool became the hub's active tool: read the selection again. */
void pdguiCharacterToolRefresh(void);
/* The browser: the list on the left, the selected entry on the right. */
void pdguiCharacterToolRender(float contentW, float contentH, float scale);

/* The creator view, which takes the hub's whole body while it is open. */
s32 pdguiCharacterToolCreatorOpen(void);
void pdguiCharacterToolRenderCreator(float scale);
void pdguiCharacterToolCloseCreator(void);
/* The hub's header and breadcrumb for the creator, e.g. "CREATE BODY MOD"
 * and "Characters > Create Body Mod". */
const char *pdguiCharacterToolCreatorTitle(void);
const char *pdguiCharacterToolCreatorPath(void);

/*
 * --debug-character-tool-create: the creator without the mouse. It makes the
 * calls the Use as Template and Create + Enable buttons make, on the same
 * state, so a scripted launch proves what a player gets. gender ("male",
 * "female"), build ("human", "guard", "maian", "template") and height (0 =
 * keep) are optional and apply to a body or a head. Returns 1 when the mod
 * was saved and is active.
 */
s32 pdguiCharacterToolDebugCreate(const char *templateId, const char *displayName,
	const char *gender, const char *build, s32 height);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_CHARACTERTOOL_H */
