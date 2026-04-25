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
 *   Priority F (2026-04-24) -- GRID_BLANK_STAGE = STAGE_TEST_DEST (0x1a).
 *   Selected from the test-arena set after Mike re-enabled them in the
 *   picker. STAGE_TEST_DEST ships under the langbank name "Training
 *   Day" with the smallest geometry budget of the candidates
 *   (-mgfx120 -mvtx98, see src/lib/main.c) and a clean sky + ground
 *   environment (clouds_enabled=1, water_enabled=1, see env.c). It is
 *   the closest the shipped data gets to a featureless arena -- a
 *   plain test plane Mike can drop Forge objects onto without competing
 *   geometry. The Grid submenu surfaces a "Blank Map" row that loads
 *   this stagenum.
 *   See context/audits/evening-decisions-2026-04-23.md.
 */

#ifndef _IN_PDGUI_MENU_GRID_H
#define _IN_PDGUI_MENU_GRID_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Priority F (2026-04-24): Blank Map = STAGE_TEST_DEST = 0x1a. The
 * Grid submenu surfaces a "Blank Map" row that loads this stagenum.
 * Mirrored as a literal here rather than including constants.h because
 * port/fast3d/pdgui_menu_mainmenu.cpp pulls this header from a C++ TU
 * that cannot drag in the project's bool=s32 typedef. */
#define GRID_BLANK_STAGE 0x1a

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _IN_PDGUI_MENU_GRID_H */
