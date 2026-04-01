#ifndef _IN_PDGUI_MENUS_H
#define _IN_PDGUI_MENUS_H

/**
 * pdgui_menus.h -- Registration functions for all ImGui menu replacements.
 *
 * Each menu replacement lives in its own pdgui_menu_*.cpp file and exposes
 * a single registration function.  pdguiMenusRegisterAll() calls them all
 * during init, after the hot-swap system is ready.
 *
 * To add a new menu replacement:
 *   1. Create port/fast3d/pdgui_menu_yourname.cpp
 *   2. Add void pdguiMenuYourNameRegister(void) declaration here
 *   3. Call it from pdguiMenusRegisterAll() below
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Individual menu registration functions */
void pdguiMenuAgentSelectRegister(void);
void pdguiMenuMainMenuRegister(void);
void pdguiMenuAgentCreateRegister(void);
void pdguiMenuWarningRegister(void);
void pdguiMenuNetworkRegister(void);
void pdguiMenuMatchSetupRegister(void);
void pdguiMenuSoloMissionRegister(void);  /* Group 1: Solo Mission Flow */
void pdguiMenuTeamSetupRegister(void);    /* Group 4: Team assignment screen */
void pdguiMenuMpSettingsRegister(void);   /* Group 4: Player handicaps screen */
void pdguiMenuChallengesRegister(void);   /* Group 4: Combat challenge browser */
void pdguiMpIngameRegister(void);         /* Group 5: MP In-Game overlays + endscreen suppression */
void pdguiMenuEndscreenRegister(void);    /* Group 2: SP/MP end screens */
/* Lobby renders as overlay from pdguiLobbyRender, not via hotswap */
/* void pdguiMenuSettingsRegister(void);   -- TODO: standalone settings if needed */

/**
 * Register all available ImGui menu replacements with the hot-swap system.
 * Called from pdguiInit() after pdguiHotswapInit().
 */
static inline void pdguiMenusRegisterAll(void)
{
    pdguiMenuAgentSelectRegister();
    pdguiMenuMainMenuRegister();
    pdguiMenuAgentCreateRegister();
    pdguiMenuWarningRegister();
    pdguiMenuNetworkRegister();
    pdguiMenuMatchSetupRegister();
    pdguiMenuSoloMissionRegister();
    pdguiMenuTeamSetupRegister();
    pdguiMenuMpSettingsRegister();
    pdguiMenuChallengesRegister();
    pdguiMpIngameRegister();
    pdguiMenuEndscreenRegister();
}

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MENUS_H */
