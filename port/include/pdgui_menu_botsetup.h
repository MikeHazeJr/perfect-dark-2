#ifndef _IN_PDGUI_MENU_BOTSETUP_H
#define _IN_PDGUI_MENU_BOTSETUP_H

/**
 * pdgui_menu_botsetup.h -- Public inline-content helpers for the
 * Batch 6 Bot/Simulant Setup renderers.
 *
 * These functions draw the same content as the modal wrappers in
 * pdgui_menu_botsetup.cpp (renderMpSimulants etc.) but without the
 * surrounding modal window frame.  Intended for use by
 * pdgui_menu_room.cpp so the legacy g_BotConfigsArray Simulant
 * Profile flow can live as an inline expandable section inside the
 * room screen rather than as a pushed modal.
 *
 * IMPORTANT: these helpers must be called from inside an existing
 * ImGui window or child region.  They do NOT call ImGui::Begin /
 * End themselves.
 *
 * The legacy dialog defs (g_MpSimulantsMenuDialog / g_MpAddSimulantMenuDialog
 * / g_MpChangeSimulantMenuDialog / g_MpEditSimulantMenuDialog /
 * g_MpSimulantCharacterMenuDialog) remain hot-swapped to the modal
 * wrappers so any legacy push path (Combat Simulator menu) still
 * renders correctly without breaking linking.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Draw the Simulants roster (8 slots + Add + Clear All) as inline
 * content inside the caller's ImGui window.  Clicking a slot row still
 * pushes the Add or Edit modal via the legacy handler (the handler
 * sets slotindex then calls menuPushDialog).  Does not draw any
 * surrounding frame, title bar, or action bar -- the caller owns the
 * window chrome.
 *
 * @param bodyHeight  Height of the inline body in pixels.  Pass 0 for
 *                    "use remaining content region".
 */
void pdguiBotSetupDrawSimulantsBody(float bodyHeight);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MENU_BOTSETUP_H */
