#ifndef _IN_PDGUI_STYLE_H
#define _IN_PDGUI_STYLE_H

/**
 * pdgui_style.h — PD-authentic ImGui style API.
 *
 * Exposes the PD-style rendering functions to pdgui_backend.cpp and
 * future menu code (modmenu.c, etc.).
 */

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Apply PD's blue palette and sharp-cornered metrics to ImGui's style. */
void pdguiApplyPdStyle(void);

/* Draw a complete PD-style dialog frame (title gradient, body, borders, shimmer).
 * Call this with custom window rendering for windows that need the full PD look. */
void pdguiDrawPdDialog(float x, float y, float w, float h,
                        const char *title, s32 focused);

/* Draw PD-style focus highlight behind a menu item. */
void pdguiDrawItemHighlight(float x, float y, float w, float h);

/* Add shimmer overlay to the current ImGui window's borders.
 * Call after ImGui::Begin() to add animated shimmer to any window. */
void pdguiRenderWindowBg(void);

/* Iterate all active ImGui windows and add PD-style shimmer to their borders.
 * Call once per frame after all windows have been submitted, before Render(). */
void pdguiRenderAllWindowShimmers(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_STYLE_H */
