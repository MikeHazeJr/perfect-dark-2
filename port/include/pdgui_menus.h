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
void pdguiMenuSoloMissionRegister(void);  /* Group 1: Solo Mission Flow */
void pdguiMenuTeamSetupRegister(void);    /* Group 4: Team assignment screen */
void pdguiMenuMpSettingsRegister(void);   /* Group 4: Player handicaps + D5 P3 Batch 12 Music/Soundtrack/Team Names */
void pdguiMenuChallengesRegister(void);   /* Group 4: Combat challenge browser (+ D5 P3 Batch 12 root variant) */
void pdguiMpIngameRegister(void);         /* Group 5: MP In-Game overlays + endscreen suppression */
void pdguiMenuEndscreenRegister(void);    /* Group 2: SP/MP end screens */
void pdguiMenuTrainingRegister(void);     /* Group 6: Firing Range, DT, HT, Bio dialogs */
void pdguiMenuCheatsRegister(void);       /* D5 P3 Batch 4: Cheats hub (9 dialogs) */
void pdguiMenuMpSetupRegister(void);      /* D5 P3 Batch 5: MP Setup Core (14 dialogs) */
void pdguiMenuBotSetupRegister(void);     /* D5 P3 Batch 6: Bot/Simulant Setup (5 dialogs) */
void pdguiMenuMpAdvancedRegister(void);   /* D5 P3 Batch 7: MP Advanced/Quick paths (11 dialogs) */
void pdguiMenuMpPauseRegister(void);      /* D5 P3 Batch 8: MP Pause & In-Game (6 dialogs) */
void pdguiMenuPlayerConfigRegister(void); /* D5 P3 Batch 11: MP Player Config & Stats (5 dialogs) */
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
    pdguiMenuSoloMissionRegister();
    pdguiMenuTeamSetupRegister();
    pdguiMenuMpSettingsRegister();
    pdguiMenuChallengesRegister();
    pdguiMpIngameRegister();
    pdguiMenuEndscreenRegister();
    pdguiMenuTrainingRegister();
    pdguiMenuCheatsRegister();
    pdguiMenuMpSetupRegister();
    pdguiMenuBotSetupRegister();
    pdguiMenuMpAdvancedRegister();
    pdguiMenuMpPauseRegister();
    pdguiMenuPlayerConfigRegister();
}

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDGUI_MENUS_H */
