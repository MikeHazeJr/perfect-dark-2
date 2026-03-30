#ifndef _IN_PDGUI_MENUBUILDER_H
#define _IN_PDGUI_MENUBUILDER_H

/**
 * pdgui_menubuilder.h -- ImGui component library & menu builder registry.
 *
 * Provides PD-authentic UI primitives (PdDialog, PdMenuItem, PdSlider, etc.)
 * that draw using the active palette from pdgui_style.cpp.  Also exposes a
 * registry of per-menu builder functions so the storyboard can render the
 * "NEW" version of any game menu by index.
 *
 * Part of Phase D4 -- Menu Storyboard & ImGui Migration.
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------ Menu Preview Registry ------ */

/* How many menus are registered. */
s32 pdguiMenuBuilderCount(void);

/* Render the NEW (ImGui) version of menu at catalogIndex.
 * Returns 1 if a builder exists, 0 if not yet implemented.
 * previewX/Y/W/H define the bounding box within the storyboard preview area. */
s32 pdguiMenuBuilderRender(s32 catalogIndex, float previewX, float previewY,
                            float previewW, float previewH);

/* ------ Mock Data ------ */

/* Populate the shared mock data struct used by menu builders. */
void pdguiMockDataInit(void);

/* ------ Dialog tint ------ */

/* Blend the current custom theme palette with a dialog-type color overlay.
 * dialogType: 1=DEFAULT(blue), 2=DANGER(red), 3=SUCCESS(green), 5=WHITE.
 * tintStrength: 0.0 = pure theme, 1.0 = pure dialog color.  Default 0.3. */
void pdguiApplyDialogTint(s32 dialogType, float tintStrength);

/* Remove tint, revert to pure theme palette. */
void pdguiClearDialogTint(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MENUBUILDER_H */
