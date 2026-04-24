/**
 * pdgui_menu_grid.h -- The Grid submenu (Priority 1).
 *
 * Map picker + variant (gametype + rules) + Enter flow for The Grid
 * live-bot-test / edit session.  The Main Menu "The Grid" button
 * routes here instead of calling pdguiForgeStartSession directly so
 * the user can pick the base map and configure the variant before
 * entering the session.
 *
 * "The Grid" is the unified facility.  Forge (edit) and Playtest
 * (inhabit) are two modes inside it, toggled in-session via the
 * existing ACTION_FORGE_TOGGLE (priority 2 swaps the binding to the
 * Halo-style Back button with in-place camera/player swap).
 *
 * Blank Map stage:
 *   GRID_BLANK_STAGE is intentionally NOT defined here.  Mike asked
 *   for the engine's "invalid map fallback plane" as the blank
 *   template; an exhaustive search turned up no bare-plane stage in
 *   this codebase, and Mike's direction was firm that CI Training is
 *   NOT the substitute.  The Grid submenu omits the Blank Map entry
 *   until the correct stagenum is identified.  Define this macro in
 *   a single place when we know the target -- the submenu will then
 *   surface the Blank Map row automatically.
 *   See context/audits/evening-decisions-2026-04-23.md.
 */

#ifndef _IN_PDGUI_MENU_GRID_H
#define _IN_PDGUI_MENU_GRID_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* When GRID_BLANK_STAGE is defined (by a build flag or a future edit
 * to this header), the Grid submenu shows a Blank Map entry that
 * loads the given stagenum.  Until then, the entry is omitted. */
/* #define GRID_BLANK_STAGE <stagenum> */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_PDGUI_MENU_GRID_H */
