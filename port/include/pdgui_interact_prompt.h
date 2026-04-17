/**
 * pdgui_interact_prompt.h -- S311 interact prompt entry point.
 *
 * Render-loop hook for the "[E] Pick up" style HUD overlay driven by the
 * game's `g_InteractProp` tracker + S312 glyph system.  Called from
 * pdguiRender after the gameplay HUD reticle.
 *
 * Auto-discovered by CMakeLists.txt file(GLOB_RECURSE port/*.cpp).
 */
#ifndef PDGUI_INTERACT_PROMPT_H
#define PDGUI_INTERACT_PROMPT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Draw the contextual interact prompt (if any) during the gameplay HUD
 * pass.  No-op when no prop is currently tracked.  Safe to call every
 * frame.
 */
void pdguiInteractPromptRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_INTERACT_PROMPT_H */
