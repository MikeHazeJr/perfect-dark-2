/**
 * pdgui_subtitles.h -- ImGui subtitle overlay.
 *
 * Replaces the legacy N64 hudmsg-path subtitle renderer for
 * HUDMSGTYPE_INGAMESUBTITLE and HUDMSGTYPE_CUTSCENESUBTITLE with
 * a bottom-center ImGui panel with a semi-transparent backdrop.
 *
 * Legacy rendering of the two subtitle types is suppressed in
 * hudmsgsRender so only this renderer draws them.
 */

#ifndef PDGUI_SUBTITLES_H
#define PDGUI_SUBTITLES_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Render active subtitles as a bottom-center ImGui panel.
 * Safe to call every frame; no-op when no active subtitles.
 * Called from pdgui_backend.cpp::pdguiRender after pdguiHudRender.
 */
void pdguiSubtitlesRender(s32 winW, s32 winH);

#ifdef __cplusplus
}
#endif

#endif /* PDGUI_SUBTITLES_H */
