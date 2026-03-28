/**
 * pdgui_storyboard.cpp -- Menu Storyboard & Migration Preview System.
 *
 * F11 opens a fullscreen ImGui mode that replaces the game scene.
 * Left panel: categorized catalog of all 113 PD menu dialogs.
 * Right panel: decoupled preview of the selected menu.
 *
 *   X  = toggle OLD (PD native) / NEW (ImGui rebuild) rendering
 *   Y  = cycle quality rating: Good → Fine → Incomplete → Redo → Unrated
 *   LB/RB = cycle theme palette (0-6)
 *   D-pad = navigate catalog
 *   A  = select / confirm
 *   B  = back / deselect
 *   START = exit storyboard
 *
 * Ratings persist to storyboard_ratings.json in the working directory.
 *
 * IMPORTANT: This file is C++ and must NOT include types.h (which #defines
 * bool as s32, breaking C++ bool).  See pdgui_debugmenu.cpp for the pattern.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 *
 * Part of Phase D4 -- see context/menu-storyboard.md (ADR-001).
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include "pdgui_storyboard.h"
#include "pdgui_menubuilder.h"
#include "pdgui_style.h"
#include "pdgui_scaling.h"
#include "system.h"

/* ========================================================================
 * Menu Catalog -- every dialog in the game
 * ======================================================================== */

/* Category IDs */
enum StoryboardCategory {
    CAT_CS_ENDSCREEN = 0,
    CAT_CS_PAUSE,
    CAT_CS_SETUP,
    CAT_SOLO,
    CAT_COOP,
    CAT_TRAINING,
    CAT_FILEMGR,
    CAT_PAK_ERROR,
    CAT_EXT_OPTIONS,
    CAT_NETWORK,
    CAT_CHALLENGE,
    CAT_COUNT
};

static const char *s_CategoryNames[CAT_COUNT] = {
    "CS Endscreen",
    "CS Pause",
    "CS Setup",
    "Solo Mission",
    "Co-op / Anti",
    "Training",
    "File Mgmt",
    "Pak Errors",
    "Ext Options",
    "Network",
    "Challenge"
};

/* Dialog type constants (mirrors constants.h without including it) */
#define DLGTYPE_DEFAULT  1
#define DLGTYPE_DANGER   2
#define DLGTYPE_SUCCESS  3
#define DLGTYPE_WHITE    5

struct MenuCatalogEntry {
    const char *variableName;   /* C symbol for reference */
    const char *displayTitle;   /* human-readable title */
    s32         dialogType;     /* MENUDIALOGTYPE (1-5) */
    s32         category;       /* StoryboardCategory enum */
    bool        needsMockData;  /* true if mock data needed for preview */
};

/* Master catalog -- all 113 menus.
 * Order within each category matches the ADR inventory. */
static const MenuCatalogEntry s_Catalog[] = {
    /* ---- CAT_CS_ENDSCREEN (7) ---- */
    { "g_MpEndscreenIndGameOverMenuDialog",           "Game Over (Individual)",     DLGTYPE_DEFAULT, CAT_CS_ENDSCREEN, true  },
    { "g_MpEndscreenTeamGameOverMenuDialog",          "Game Over (Team)",           DLGTYPE_DEFAULT, CAT_CS_ENDSCREEN, true  },
    { "g_MpEndscreenPlayerRankingMenuDialog",         "Player Ranking",             DLGTYPE_DEFAULT, CAT_CS_ENDSCREEN, true  },
    { "g_MpEndscreenTeamRankingMenuDialog",           "Team Ranking",               DLGTYPE_DEFAULT, CAT_CS_ENDSCREEN, true  },
    { "g_MpEndscreenPlayerStatsMenuDialog",           "Player Stats",               DLGTYPE_DEFAULT, CAT_CS_ENDSCREEN, true  },
    { "g_MpEndscreenChallengeCompletedMenuDialog",    "Challenge Completed!",       DLGTYPE_SUCCESS, CAT_CS_ENDSCREEN, true  },
    { "g_MpEndscreenChallengeFailedMenuDialog",       "Challenge Failed!",          DLGTYPE_DANGER,  CAT_CS_ENDSCREEN, true  },

    /* ---- CAT_CS_PAUSE (6) ---- */
    { "g_MpPauseControlMenuDialog",                   "Pause - Control",            DLGTYPE_DEFAULT, CAT_CS_PAUSE,     true  },
    { "g_MpPauseInventoryMenuDialog",                 "Pause - Inventory",          DLGTYPE_DEFAULT, CAT_CS_PAUSE,     true  },
    { "g_MpPausePlayerStatsMenuDialog",               "Pause - Player Stats",       DLGTYPE_DEFAULT, CAT_CS_PAUSE,     true  },
    { "g_MpPausePlayerRankingMenuDialog",             "Pause - Player Ranking",     DLGTYPE_DEFAULT, CAT_CS_PAUSE,     true  },
    { "g_MpPauseTeamRankingsMenuDialog",              "Pause - Team Ranking",       DLGTYPE_DEFAULT, CAT_CS_PAUSE,     true  },
    { "g_MpEndGameMenuDialog",                        "End Game",                   DLGTYPE_DANGER,  CAT_CS_PAUSE,     false },

    /* ---- CAT_CS_SETUP (12) ---- */
    { "g_MpArenaMenuDialog",                          "Arena Select",               DLGTYPE_DEFAULT, CAT_CS_SETUP,     true  },
    { "g_MpWeaponsMenuDialog",                        "Weapons",                    DLGTYPE_DEFAULT, CAT_CS_SETUP,     true  },
    { "g_MpGameSetupMenuDialog",                      "Game Setup",                 DLGTYPE_DEFAULT, CAT_CS_SETUP,     false },
    { "g_MpPlayerSetupMenuDialog",                    "Player Setup",               DLGTYPE_DEFAULT, CAT_CS_SETUP,     true  },
    { "g_MpSimulantListMenuDialog",                   "Simulants",                  DLGTYPE_DEFAULT, CAT_CS_SETUP,     true  },
    { "g_MpAddSimulantMenuDialog",                    "Add Simulant",               DLGTYPE_DEFAULT, CAT_CS_SETUP,     false },
    { "g_MpChangeSimulantMenuDialog",                 "Change Simulant",            DLGTYPE_DEFAULT, CAT_CS_SETUP,     false },
    { "g_MpEditSimulantMenuDialog",                   "Edit Simulant",              DLGTYPE_DEFAULT, CAT_CS_SETUP,     false },
    { "g_MpLimitsMenuDialog",                         "Limits",                     DLGTYPE_DEFAULT, CAT_CS_SETUP,     false },
    { "g_MpHandicapsMenuDialog",                      "Handicaps",                  DLGTYPE_DEFAULT, CAT_CS_SETUP,     true  },
    { "g_MpDropOutMenuDialog",                        "Drop Out",                   DLGTYPE_DANGER,  CAT_CS_SETUP,     false },
    { "g_MpSaveSetupNameMenuDialog",                  "Save Setup",                 DLGTYPE_DEFAULT, CAT_CS_SETUP,     false },

    /* ---- CAT_SOLO (8) ---- */
    { "g_PreAndPostMissionBriefingMenuDialog",        "Briefing",                   DLGTYPE_DEFAULT, CAT_SOLO,         true  },
    { "g_SoloMissionEndscreenCompletedMenuDialog",    "Mission Complete",           DLGTYPE_SUCCESS, CAT_SOLO,         true  },
    { "g_SoloMissionEndscreenFailedMenuDialog",       "Mission Failed",             DLGTYPE_DANGER,  CAT_SOLO,         true  },
    { "g_SoloEndscreenObjectivesCompletedMenuDialog", "Objectives (Complete)",      DLGTYPE_SUCCESS, CAT_SOLO,         true  },
    { "g_SoloEndscreenObjectivesFailedMenuDialog",    "Objectives (Failed)",        DLGTYPE_DANGER,  CAT_SOLO,         true  },
    { "g_RetryMissionMenuDialog",                     "Retry Mission",              DLGTYPE_DEFAULT, CAT_SOLO,         false },
    { "g_NextMissionMenuDialog",                      "Next Mission",               DLGTYPE_DEFAULT, CAT_SOLO,         false },
    { "g_MissionContinueOrReplyMenuDialog",           "Continue / Replay",          DLGTYPE_DEFAULT, CAT_SOLO,         false },

    /* ---- CAT_COOP (8) ---- */
    { "g_2PMissionEndscreenCompletedHMenuDialog",     "2P Complete (Horiz)",        DLGTYPE_SUCCESS, CAT_COOP,         false },
    { "g_2PMissionEndscreenFailedHMenuDialog",        "2P Failed (Horiz)",          DLGTYPE_DANGER,  CAT_COOP,         false },
    { "g_2PMissionEndscreenCompletedVMenuDialog",     "2P Complete (Vert)",         DLGTYPE_SUCCESS, CAT_COOP,         false },
    { "g_2PMissionEndscreenFailedVMenuDialog",        "2P Failed (Vert)",           DLGTYPE_DANGER,  CAT_COOP,         false },
    { "g_2PMissionEndscreenObjectivesFailedVMenuDialog",  "2P Objectives Failed",   DLGTYPE_DANGER,  CAT_COOP,         true  },
    { "g_2PMissionEndscreenObjectivesCompletedVMenuDialog","2P Objectives Complete", DLGTYPE_SUCCESS, CAT_COOP,         true  },
    { "g_2PMissionInventoryHMenuDialog",              "2P Inventory (Horiz)",       DLGTYPE_DEFAULT, CAT_COOP,         true  },
    { "g_2PMissionInventoryVMenuDialog",              "2P Inventory (Vert)",        DLGTYPE_DEFAULT, CAT_COOP,         true  },

    /* ---- CAT_TRAINING (22) ---- */
    { "g_FrWeaponListMenuDialog",                     "FR: Weapon List",            DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_FrDifficultyMenuDialog",                     "FR: Difficulty",             DLGTYPE_DEFAULT, CAT_TRAINING,     false },
    { "g_FrTrainingInfoPreGameMenuDialog",            "FR: Training Info (Pre)",    DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_FrTrainingInfoInGameMenuDialog",             "FR: Training Info (In)",     DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_FrCompletedMenuDialog",                      "FR: Completed",              DLGTYPE_SUCCESS, CAT_TRAINING,     true  },
    { "g_FrFailedMenuDialog",                         "FR: Failed",                 DLGTYPE_DANGER,  CAT_TRAINING,     true  },
    { "g_DtListMenuDialog",                           "Device Training List",       DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_DtDetailsMenuDialog",                        "Device Details",             DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_DtCompletedMenuDialog",                      "DT: Completed",              DLGTYPE_SUCCESS, CAT_TRAINING,     true  },
    { "g_DtFailedMenuDialog",                         "DT: Failed",                 DLGTYPE_DANGER,  CAT_TRAINING,     true  },
    { "g_HtListMenuDialog",                           "Holotraining List",          DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_HtDetailsMenuDialog",                        "Holotraining Details",       DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_HtCompletedMenuDialog",                      "HT: Completed",              DLGTYPE_SUCCESS, CAT_TRAINING,     true  },
    { "g_HtFailedMenuDialog",                         "HT: Failed",                 DLGTYPE_DANGER,  CAT_TRAINING,     true  },
    { "g_BioListMenuDialog",                          "Information",                DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_BioProfileMenuDialog",                       "Character Profile",          DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_BioTextMenuDialog",                          "Bio Text",                   DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_HangarListMenuDialog",                       "Hangar Information",         DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_HangarVehicleDetailsMenuDialog",             "Vehicle Details",            DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_HangarLocationDetailsMenuDialog",            "Location Details",           DLGTYPE_DEFAULT, CAT_TRAINING,     true  },
    { "g_HangarVehicleHolographMenuDialog",           "Holograph",                  DLGTYPE_DEFAULT, CAT_TRAINING,     false },
    { "g_NowSafeMenuDialog",                          "Cheats",                     DLGTYPE_DEFAULT, CAT_TRAINING,     true  },

    /* ---- CAT_FILEMGR (20) ---- */
    { "g_FilemgrFileSelectMenuDialog",                "File Select",                DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_FilemgrEnterNameMenuDialog",                 "Enter Agent Name",           DLGTYPE_DEFAULT, CAT_FILEMGR,      false },
    { "g_FilemgrOperationsMenuDialog",                "Game Files",                 DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_FilemgrRenameMenuDialog",                    "Change File Name",           DLGTYPE_DEFAULT, CAT_FILEMGR,      false },
    { "g_FilemgrDuplicateNameMenuDialog",             "Duplicate File Name",        DLGTYPE_DEFAULT, CAT_FILEMGR,      false },
    { "g_FilemgrSelectLocationMenuDialog",            "Select Location",            DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_FilemgrCopyMenuDialog",                      "Copy File",                  DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_FilemgrDeleteMenuDialog",                    "Delete File",                DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_FilemgrConfirmDeleteMenuDialog",             "Warning (Delete)",           DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_FilemgrFileInUseMenuDialog",                 "Error (In Use)",             DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_FilemgrErrorMenuDialog",                     "Error",                      DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_FilemgrFileSavedMenuDialog",                 "Cool!",                      DLGTYPE_SUCCESS, CAT_FILEMGR,      false },
    { "g_FilemgrSaveErrorMenuDialog",                 "Save Error",                 DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_FilemgrFileLostMenuDialog",                  "File Lost",                  DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_FilemgrSaveElsewhereMenuDialog",             "Save Elsewhere",             DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_PakNotOriginalMenuDialog",                   "Not Original Pak",           DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_PakChoosePakMenuDialog",                     "Controller Pak Menu",        DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_PakGameNotesMenuDialog",                     "Game Notes",                 DLGTYPE_DEFAULT, CAT_FILEMGR,      true  },
    { "g_PakDeleteNoteMenuDialog",                    "Delete Game Note",           DLGTYPE_DANGER,  CAT_FILEMGR,      false },
    { "g_ChooseLanguageMenuDialog",                   "Choose Language",            DLGTYPE_DEFAULT, CAT_FILEMGR,      false },

    /* ---- CAT_PAK_ERROR (8) ---- */
    { "g_PakDamagedMenuDialog",                       "Damaged Controller Pak",     DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },
    { "g_PakFullMenuDialog",                          "Full Controller Pak",        DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },
    { "g_PakRemovedMenuDialog",                       "Pak Removed",               DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },
    { "g_PakCannotReadGameBoyMenuDialog",             "Cannot Read GameBoy",        DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },
    { "g_PakDataLostMenuDialog",                      "Data Lost",                  DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },
    { "g_PakRepairSuccessMenuDialog",                 "Repair Successful",          DLGTYPE_SUCCESS, CAT_PAK_ERROR,    false },
    { "g_PakRepairFailedMenuDialog",                  "Repair Failed",              DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },
    { "g_PakAttemptRepairMenuDialog",                 "Attempt Repair",             DLGTYPE_DANGER,  CAT_PAK_ERROR,    false },

    /* ---- CAT_EXT_OPTIONS (11) ---- */
    { "g_ExtendedMenuDialog",                         "Extended Options",           DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedMouseMenuDialog",                    "Mouse Options",              DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedStickMenuDialog",                    "Analog Stick Settings",      DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedControllerMenuDialog",               "Controller Options",         DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedVideoMenuDialog",                    "Video Options",              DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedAudioMenuDialog",                    "Audio Options",              DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtPlayerGameOptionsMenuDialog",             "Game Options",               DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedGameCrosshairColourMenuDialog",      "Crosshair Colour",           DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedBindsMenuDialog",                    "Key Bindings",               DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedBindKeyMenuDialog",                  "Bind Key",                   DLGTYPE_SUCCESS, CAT_EXT_OPTIONS,  false },
    { "g_ExtendedSelectPlayerMenuDialog",             "Select Player",              DLGTYPE_DEFAULT, CAT_EXT_OPTIONS,  false },

    /* ---- CAT_NETWORK (6) ---- */
    { "g_NetMenuDialog",                              "Multiplayer",                DLGTYPE_DEFAULT, CAT_NETWORK,      false },
    { "g_NetCoopHostMenuDialog",                      "Co-op Mission Setup",        DLGTYPE_DEFAULT, CAT_NETWORK,      false },
    { "g_NetJoinAddressDialog",                       "Enter Address",              DLGTYPE_SUCCESS, CAT_NETWORK,      false },
    { "g_NetJoiningDialog",                           "Connecting...",              DLGTYPE_SUCCESS, CAT_NETWORK,      false },
    { "g_NetPauseControlsMenuDialog",                 "Net Controls",               DLGTYPE_DEFAULT, CAT_NETWORK,      false },
    { "g_NetRecentServersMenuDialog",                 "Recent Servers",             DLGTYPE_DEFAULT, CAT_NETWORK,      true  },

    /* ---- CAT_CHALLENGE (3) ---- */
    { "g_MpEndscreenChallengeCheatedMenuDialog",      "Challenge Cheated!",         DLGTYPE_DANGER,  CAT_CHALLENGE,    false },
    { "g_MpEndscreenSavePlayerMenuDialog",            "Save Player",                DLGTYPE_DEFAULT, CAT_CHALLENGE,    false },
    { "g_MpEndscreenConfirmNameMenuDialog",           "Confirm Player Name",        DLGTYPE_DEFAULT, CAT_CHALLENGE,    false },
};

static const s32 CATALOG_SIZE = (s32)(sizeof(s_Catalog) / sizeof(s_Catalog[0]));

/* ========================================================================
 * Storyboard State
 * ======================================================================== */

static bool s_StoryboardActive  = false;
static bool s_StoryboardInited  = false;

/* Navigation */
static s32 s_SelectedCategory   = 0;
static s32 s_SelectedIndex      = 0;   /* index within current category */
static s32 s_PreviewCatalogIdx  = -1;  /* flat catalog index of preview, or -1 */
static bool s_ShowNewMode       = false;

/* Quality ratings -- one per catalog entry */
static s32 s_Ratings[256];  /* sized larger than catalog for safety */

/* Saved mouse state (same pattern as debug overlay) */
static SDL_bool s_SavedRelativeMode = SDL_FALSE;
static int      s_SavedShowCursor   = 0;

/* Gamepad repeat timing */
static Uint32 s_LastNavTime     = 0;
static const Uint32 NAV_REPEAT_MS = 200;

/* ========================================================================
 * Ratings persistence -- simple JSON read/write
 * ======================================================================== */

static const char *RATINGS_FILE = "storyboard_ratings.json";

static void loadRatings(void)
{
    memset(s_Ratings, 0, sizeof(s_Ratings));

    FILE *f = fopen(RATINGS_FILE, "r");
    if (!f) return;

    /* Minimal JSON parser: expects {"0":1,"1":3,...} */
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    const char *p = buf;
    while (*p) {
        /* Find next "index":value pair */
        const char *q = strchr(p, '"');
        if (!q) break;
        q++;
        int idx = atoi(q);
        const char *colon = strchr(q, ':');
        if (!colon) break;
        colon++;
        int val = atoi(colon);
        if (idx >= 0 && idx < CATALOG_SIZE && val >= 0 && val <= 4) {
            s_Ratings[idx] = val;
        }
        p = colon + 1;
    }

    sysLogPrintf(LOG_NOTE, "storyboard: Loaded ratings from %s", RATINGS_FILE);
}

static void saveRatings(void)
{
    FILE *f = fopen(RATINGS_FILE, "w");
    if (!f) {
        sysLogPrintf(LOG_NOTE, "storyboard: Failed to save ratings to %s", RATINGS_FILE);
        return;
    }

    fprintf(f, "{");
    bool first = true;
    for (s32 i = 0; i < CATALOG_SIZE; i++) {
        if (s_Ratings[i] != PD_RATING_UNRATED) {
            fprintf(f, "%s\"%d\":%d", first ? "" : ",", i, s_Ratings[i]);
            first = false;
        }
    }
    fprintf(f, "}");
    fclose(f);
}

/* ========================================================================
 * Helper: get catalog entries for a given category
 * ======================================================================== */

static s32 getCategoryEntries(s32 cat, s32 *outIndices, s32 maxOut)
{
    s32 count = 0;
    for (s32 i = 0; i < CATALOG_SIZE && count < maxOut; i++) {
        if (s_Catalog[i].category == cat) {
            outIndices[count++] = i;
        }
    }
    return count;
}

/* ========================================================================
 * Helper: dialog type to color for badges / indicators
 * ======================================================================== */

static ImU32 dialogTypeColor(s32 type)
{
    switch (type) {
        case DLGTYPE_DANGER:  return IM_COL32(255, 60, 60, 255);
        case DLGTYPE_SUCCESS: return IM_COL32(60, 255, 60, 255);
        case DLGTYPE_WHITE:   return IM_COL32(255, 255, 255, 255);
        default:              return IM_COL32(60, 160, 255, 255);
    }
}

static const char *dialogTypeName(s32 type)
{
    switch (type) {
        case DLGTYPE_DANGER:  return "DANGER";
        case DLGTYPE_SUCCESS: return "SUCCESS";
        case DLGTYPE_WHITE:   return "WHITE";
        default:              return "DEFAULT";
    }
}

static ImU32 ratingColor(s32 rating)
{
    switch (rating) {
        case PD_RATING_GOOD:       return IM_COL32(40, 200, 40, 255);
        case PD_RATING_FINE:       return IM_COL32(60, 140, 220, 255);
        case PD_RATING_INCOMPLETE: return IM_COL32(220, 200, 40, 255);
        case PD_RATING_REDO:       return IM_COL32(220, 40, 40, 255);
        default:                   return IM_COL32(120, 120, 120, 255);
    }
}

static const char *ratingName(s32 rating)
{
    switch (rating) {
        case PD_RATING_GOOD:       return "Good";
        case PD_RATING_FINE:       return "Fine";
        case PD_RATING_INCOMPLETE: return "Incomplete";
        case PD_RATING_REDO:       return "Redo";
        default:                   return "Unrated";
    }
}

/* ========================================================================
 * Aggregate stats
 * ======================================================================== */

struct RatingStats {
    s32 good, fine, incomplete, redo, unrated;
};

static RatingStats computeStats(void)
{
    RatingStats st = { 0, 0, 0, 0, 0 };
    for (s32 i = 0; i < CATALOG_SIZE; i++) {
        switch (s_Ratings[i]) {
            case PD_RATING_GOOD:       st.good++; break;
            case PD_RATING_FINE:       st.fine++; break;
            case PD_RATING_INCOMPLETE: st.incomplete++; break;
            case PD_RATING_REDO:       st.redo++; break;
            default:                   st.unrated++; break;
        }
    }
    return st;
}

/* ========================================================================
 * Mouse grab management (same pattern as pdgui_backend.cpp)
 * ======================================================================== */

static SDL_Window *s_Window = nullptr;

/* Get SDL window handle from the video system */
extern "C" void *videoGetWindowHandle(void);

static void storyboardUpdateMouseGrab(bool active)
{
    if (!s_Window) {
        s_Window = (SDL_Window *)videoGetWindowHandle();
    }

    if (active) {
        s_SavedRelativeMode = SDL_GetRelativeMouseMode();
        s_SavedShowCursor   = SDL_ShowCursor(SDL_QUERY);
        SDL_SetRelativeMouseMode(SDL_FALSE);
        SDL_ShowCursor(SDL_ENABLE);
        if (s_Window) {
            int w, h;
            SDL_GetWindowSize(s_Window, &w, &h);
            SDL_WarpMouseInWindow(s_Window, w / 2, h / 2);
        }
    } else {
        SDL_SetRelativeMouseMode(s_SavedRelativeMode);
        SDL_ShowCursor(s_SavedShowCursor ? SDL_ENABLE : SDL_DISABLE);
    }
}

/* ========================================================================
 * C-callable API
 * ======================================================================== */

extern "C" {

void pdguiStoryboardInit(void)
{
    if (s_StoryboardInited) return;
    loadRatings();
    pdguiMockDataInit();
    s_StoryboardInited = true;
    sysLogPrintf(LOG_NOTE, "storyboard: Initialized (%d menus in catalog)", CATALOG_SIZE);
}

void pdguiStoryboardShutdown(void)
{
    if (!s_StoryboardInited) return;
    saveRatings();
    s_StoryboardInited = false;
}

void pdguiStoryboardToggle(void)
{
    s_StoryboardActive = !s_StoryboardActive;
    storyboardUpdateMouseGrab(s_StoryboardActive);

    if (s_StoryboardActive) {
        sysLogPrintf(LOG_NOTE, "storyboard: Opened (F11)");
    } else {
        saveRatings();
        sysLogPrintf(LOG_NOTE, "storyboard: Closed (F11)");
    }
}

s32 pdguiStoryboardIsActive(void)
{
    return s_StoryboardActive ? 1 : 0;
}

/* ========================================================================
 * Input handling
 * ======================================================================== */

s32 pdguiStoryboardProcessEvent(void *sdlEvent)
{
    if (!s_StoryboardInited) return 0;

    const SDL_Event *ev = (const SDL_Event *)sdlEvent;

    /* F11 toggle (always listen, even when storyboard is closed) */
    if (ev->type == SDL_KEYDOWN && ev->key.keysym.sym == SDLK_F11) {
        pdguiStoryboardToggle();
        return 1;
    }

    if (!s_StoryboardActive) return 0;

    /* Count entries in current category */
    s32 catIndices[64];
    s32 catCount = getCategoryEntries(s_SelectedCategory, catIndices, 64);

    Uint32 now = SDL_GetTicks();

    /* Keyboard navigation */
    if (ev->type == SDL_KEYDOWN) {
        switch (ev->key.keysym.sym) {
            case SDLK_UP:
                if (catCount > 0) {
                    s_SelectedIndex = (s_SelectedIndex - 1 + catCount) % catCount;
                }
                return 1;
            case SDLK_DOWN:
                if (catCount > 0) {
                    s_SelectedIndex = (s_SelectedIndex + 1) % catCount;
                }
                return 1;
            case SDLK_LEFT:
                s_SelectedCategory = (s_SelectedCategory - 1 + CAT_COUNT) % CAT_COUNT;
                s_SelectedIndex = 0;
                return 1;
            case SDLK_RIGHT:
                s_SelectedCategory = (s_SelectedCategory + 1) % CAT_COUNT;
                s_SelectedIndex = 0;
                return 1;
            case SDLK_RETURN:
            case SDLK_SPACE:
                /* A / Select: set preview */
                if (catCount > 0 && s_SelectedIndex < catCount) {
                    s_PreviewCatalogIdx = catIndices[s_SelectedIndex];
                }
                return 1;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                /* B / Back: clear preview or exit */
                if (s_PreviewCatalogIdx >= 0) {
                    s_PreviewCatalogIdx = -1;
                } else {
                    pdguiStoryboardToggle();
                }
                return 1;
            case SDLK_x:
                /* Toggle OLD/NEW */
                s_ShowNewMode = !s_ShowNewMode;
                return 1;
            case SDLK_y:
                /* Cycle rating */
                if (s_PreviewCatalogIdx >= 0) {
                    s32 r = s_Ratings[s_PreviewCatalogIdx];
                    r = (r % 4) + 1;  /* 0→1, 1→2, 2→3, 3→4, 4→1 */
                    s_Ratings[s_PreviewCatalogIdx] = r;
                }
                return 1;
            default:
                break;
        }
    }

    /* Controller / Gamepad support */
    if (ev->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (ev->cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (catCount > 0) s_SelectedIndex = (s_SelectedIndex - 1 + catCount) % catCount;
                return 1;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (catCount > 0) s_SelectedIndex = (s_SelectedIndex + 1) % catCount;
                return 1;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                s_SelectedCategory = (s_SelectedCategory - 1 + CAT_COUNT) % CAT_COUNT;
                s_SelectedIndex = 0;
                return 1;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                s_SelectedCategory = (s_SelectedCategory + 1) % CAT_COUNT;
                s_SelectedIndex = 0;
                return 1;
            case SDL_CONTROLLER_BUTTON_A:
                if (catCount > 0 && s_SelectedIndex < catCount)
                    s_PreviewCatalogIdx = catIndices[s_SelectedIndex];
                return 1;
            case SDL_CONTROLLER_BUTTON_B:
                if (s_PreviewCatalogIdx >= 0) {
                    s_PreviewCatalogIdx = -1;
                } else {
                    pdguiStoryboardToggle();
                }
                return 1;
            case SDL_CONTROLLER_BUTTON_X:
                s_ShowNewMode = !s_ShowNewMode;
                return 1;
            case SDL_CONTROLLER_BUTTON_Y:
                if (s_PreviewCatalogIdx >= 0) {
                    s32 r = s_Ratings[s_PreviewCatalogIdx];
                    r = (r % 4) + 1;
                    s_Ratings[s_PreviewCatalogIdx] = r;
                }
                return 1;
            case SDL_CONTROLLER_BUTTON_START:
                pdguiStoryboardToggle();
                return 1;
            default:
                break;
        }
    }

    /* LB/RB for theme cycling is handled above.
     * For keyboard, use [ and ] as alternates: */
    if (ev->type == SDL_KEYDOWN) {
        if (ev->key.keysym.sym == SDLK_LEFTBRACKET) {
            s32 pal = pdguiGetPalette();
            /* Palette indices: 0,1,2,3,4,5,6 -- but 4 wraps to skip 4 (White doesn't look great) */
            pal = (pal - 1 + 7) % 7;
            pdguiSetPalette(pal);
            return 1;
        }
        if (ev->key.keysym.sym == SDLK_RIGHTBRACKET) {
            s32 pal = pdguiGetPalette();
            pal = (pal + 1) % 7;
            pdguiSetPalette(pal);
            return 1;
        }
    }

    /* When storyboard is active, consume all keyboard/mouse so game doesn't act */
    switch (ev->type) {
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
        case SDL_KEYDOWN:
        case SDL_KEYUP:
        case SDL_TEXTINPUT:
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
            return 1;
        default:
            return 0;
    }
}

/* ========================================================================
 * Rendering
 * ======================================================================== */

void pdguiStoryboardRender(s32 winW, s32 winH)
{
    if (!s_StoryboardActive || !s_StoryboardInited) return;

    /* Scale factor for game-relative sizing (base: 640x480) */
    float scale = (float)winH / 480.0f;
    if (scale < 0.5f) scale = 0.5f;

    ImGuiIO &io = ImGui::GetIO();
    io.FontGlobalScale = scale * 0.65f;

    /* Fullscreen background -- dark, slightly transparent */
    ImDrawList *bgDl = ImGui::GetBackgroundDrawList();
    bgDl->AddRectFilled(ImVec2(0, 0), ImVec2((float)winW, (float)winH),
                         IM_COL32(8, 6, 12, 240));

    /* Layout constants */
    float catalogW = 260.0f * scale;
    float padding  = 8.0f * scale;
    float headerH  = 40.0f * scale;
    float previewX = catalogW + padding * 2;
    float previewY = headerH + padding;
    float previewW = (float)winW - previewX - padding;
    float previewH = (float)winH - previewY - padding;

    /* ------- Header bar ------- */
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)winW, headerH));
        ImGui::Begin("##StoryboardHeader", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::Text("MENU STORYBOARD");
        ImGui::SameLine(0, 20.0f * scale);

        /* Aggregate stats */
        RatingStats st = computeStats();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%d Good", st.good);
        ImGui::SameLine(0, 10.0f * scale);
        ImGui::TextColored(ImVec4(0.3f, 0.6f, 0.9f, 1.0f), "%d Fine", st.fine);
        ImGui::SameLine(0, 10.0f * scale);
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.2f, 1.0f), "%d Inc", st.incomplete);
        ImGui::SameLine(0, 10.0f * scale);
        ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%d Redo", st.redo);
        ImGui::SameLine(0, 10.0f * scale);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%d Unrated", st.unrated);

        ImGui::SameLine((float)winW - 200.0f * scale);
        ImGui::Text("[%s]", s_ShowNewMode ? "NEW" : "OLD");
        ImGui::SameLine(0, 10.0f * scale);

        /* Current palette name */
        static const char *palNames[] = { "Grey", "Blue", "Red", "Green", "White", "Silver", "Black&Gold" };
        s32 pal = pdguiGetPalette();
        if (pal >= 0 && pal < 7) {
            ImGui::Text("Theme: %s", palNames[pal]);
        }

        ImGui::End();
    }

    /* ------- Category tabs ------- */
    {
        ImGui::SetNextWindowPos(ImVec2(0, headerH));
        ImGui::SetNextWindowSize(ImVec2(catalogW, 28.0f * scale));
        ImGui::Begin("##CatTabs", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

        /* Show abbreviated category name + left/right hints */
        ImGui::Text("<  %s  >", s_CategoryNames[s_SelectedCategory]);

        ImGui::End();
    }

    /* ------- Catalog list ------- */
    {
        float listY = headerH + 28.0f * scale;
        float listH = (float)winH - listY;

        ImGui::SetNextWindowPos(ImVec2(0, listY));
        ImGui::SetNextWindowSize(ImVec2(catalogW, listH));
        ImGui::Begin("##CatalogList", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        s32 catIndices[64];
        s32 catCount = getCategoryEntries(s_SelectedCategory, catIndices, 64);

        for (s32 i = 0; i < catCount; i++) {
            s32 flatIdx = catIndices[i];
            const MenuCatalogEntry &entry = s_Catalog[flatIdx];
            bool isSelected = (i == s_SelectedIndex);
            bool isPreviewing = (flatIdx == s_PreviewCatalogIdx);

            /* Rating color dot */
            ImU32 rCol = ratingColor(s_Ratings[flatIdx]);
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            float dotSize = 6.0f * scale;
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(cursorPos.x + dotSize, cursorPos.y + ImGui::GetTextLineHeight() * 0.5f),
                dotSize * 0.5f, rCol);

            /* Item text with selection highlight */
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotSize + 4.0f * scale);

            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Text, dialogTypeColor(entry.dialogType));
            }

            char label[128];
            snprintf(label, sizeof(label), "%s%s", isPreviewing ? "> " : "  ", entry.displayTitle);

            if (ImGui::Selectable(label, isSelected || isPreviewing)) {
                s_SelectedIndex = i;
                s_PreviewCatalogIdx = flatIdx;
            }

            if (isSelected) {
                ImGui::PopStyleColor();
                /* Auto-scroll to keep selection visible */
                if (ImGui::IsItemVisible() == false) {
                    ImGui::SetScrollHereY(0.5f);
                }
            }
        }

        ImGui::End();
    }

    /* ------- Preview area ------- */
    {
        ImGui::SetNextWindowPos(ImVec2(previewX, previewY));
        ImGui::SetNextWindowSize(ImVec2(previewW, previewH));
        ImGui::Begin("##PreviewArea", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        if (s_PreviewCatalogIdx < 0) {
            /* No menu selected */
            float centerY = previewH * 0.4f;
            ImGui::SetCursorPosY(centerY);
            ImGui::TextWrapped("Select a menu from the catalog to preview it.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "Controls:");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  D-pad / Arrows  Navigate");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  A / Enter       Select menu");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  X               Toggle OLD/NEW");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  Y               Cycle rating");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  LB/RB or [/]    Cycle theme");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  B / Esc         Back");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "  START           Exit storyboard");
        } else {
            const MenuCatalogEntry &entry = s_Catalog[s_PreviewCatalogIdx];
            s32 rating = s_Ratings[s_PreviewCatalogIdx];

            /* Preview header: title + metadata */
            ImGui::TextColored(ImColor(dialogTypeColor(entry.dialogType)).Value,
                "%s", entry.displayTitle);
            ImGui::SameLine(0, 12.0f * scale);
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                "[%s]", dialogTypeName(entry.dialogType));
            ImGui::SameLine(0, 12.0f * scale);

            /* Mode badge */
            if (s_ShowNewMode) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[NEW]");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[OLD]");
            }

            /* Rating bar */
            ImGui::SameLine(0, 20.0f * scale);
            ImGui::TextColored(ImColor(ratingColor(rating)).Value,
                "Rating: %s", ratingName(rating));
            ImGui::SameLine(0, 8.0f * scale);
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "(Y to change)");

            ImGui::Separator();

            /* Variable name for reference */
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f),
                "%s", entry.variableName);
            if (entry.needsMockData) {
                ImGui::SameLine(0, 8.0f * scale);
                ImGui::TextColored(ImVec4(0.6f, 0.5f, 0.3f, 1.0f), "[mock data]");
            }

            ImGui::Spacing();

            /* ----- Actual menu preview area ----- */
            ImVec2 menuAreaPos = ImGui::GetCursorScreenPos();
            float menuAreaW = previewW - padding * 2;
            float menuAreaH = previewH - (menuAreaPos.y - previewY) - padding;

            if (s_ShowNewMode) {
                /* NEW: try the ImGui menu builder */
                s32 built = pdguiMenuBuilderRender(s_PreviewCatalogIdx,
                    menuAreaPos.x, menuAreaPos.y, menuAreaW, menuAreaH);

                if (!built) {
                    /* No builder yet — show placeholder */
                    ImVec2 center(menuAreaPos.x + menuAreaW * 0.5f,
                                  menuAreaPos.y + menuAreaH * 0.4f);

                    ImDrawList *dl = ImGui::GetWindowDrawList();

                    /* Draw a dotted outline where the menu would go */
                    float mockW = 300.0f * scale;
                    float mockH = 200.0f * scale;
                    ImVec2 p1(center.x - mockW * 0.5f, center.y - mockH * 0.3f);
                    ImVec2 p2(center.x + mockW * 0.5f, center.y + mockH * 0.7f);

                    dl->AddRect(p1, p2, IM_COL32(80, 80, 80, 180), 0.0f, 0, 1.0f);

                    /* Title bar mock */
                    ImVec2 titleP2(p2.x, p1.y + 24.0f * scale);
                    dl->AddRectFilled(p1, titleP2, IM_COL32(40, 30, 60, 200));
                    dl->AddText(ImVec2(p1.x + 8.0f * scale, p1.y + 4.0f * scale),
                        dialogTypeColor(entry.dialogType),
                        entry.displayTitle);

                    /* "Not implemented" message */
                    const char *msg = "NEW mode not yet built for this menu.";
                    ImVec2 textSize = ImGui::CalcTextSize(msg);
                    dl->AddText(
                        ImVec2(center.x - textSize.x * 0.5f, center.y + mockH * 0.2f),
                        IM_COL32(160, 160, 160, 200), msg);

                    const char *msg2 = "See pdgui_menubuilder.cpp to implement.";
                    ImVec2 textSize2 = ImGui::CalcTextSize(msg2);
                    dl->AddText(
                        ImVec2(center.x - textSize2.x * 0.5f, center.y + mockH * 0.2f + 20.0f * scale),
                        IM_COL32(100, 100, 100, 160), msg2);
                }
            } else {
                /* OLD: placeholder for FBO capture of original PD dialog.
                 * Phase D4b will implement the actual framebuffer capture.
                 * For now, show a descriptive placeholder. */
                ImVec2 center(menuAreaPos.x + menuAreaW * 0.5f,
                              menuAreaPos.y + menuAreaH * 0.4f);

                ImDrawList *dl = ImGui::GetWindowDrawList();

                float mockW = 300.0f * scale;
                float mockH = 200.0f * scale;
                ImVec2 p1(center.x - mockW * 0.5f, center.y - mockH * 0.3f);
                ImVec2 p2(center.x + mockW * 0.5f, center.y + mockH * 0.7f);

                /* Simulate the OG PD menu appearance with colored fills */
                ImU32 typeCol = dialogTypeColor(entry.dialogType);
                ImU32 bodyCol = IM_COL32(0, 0, 30, 200);

                /* Body fill */
                dl->AddRectFilled(p1, p2, bodyCol);

                /* Title bar */
                ImVec2 titleP2(p2.x, p1.y + 24.0f * scale);
                dl->AddRectFilled(p1, titleP2, (typeCol & 0x00FFFFFF) | 0x80000000);
                dl->AddText(ImVec2(p1.x + 8.0f * scale, p1.y + 4.0f * scale),
                    IM_COL32(255, 255, 255, 255),
                    entry.displayTitle);

                /* Borders (colored by type) */
                dl->AddRect(p1, p2, typeCol, 0.0f, 0, 2.0f);

                /* Status text */
                const char *msg = "OLD mode: FBO capture pending (D4b).";
                ImVec2 textSize = ImGui::CalcTextSize(msg);
                dl->AddText(
                    ImVec2(center.x - textSize.x * 0.5f, p2.y + 12.0f * scale),
                    IM_COL32(120, 120, 120, 180), msg);
            }
        }

        ImGui::End();
    }

    /* Restore frame-level font scale (set by pdguiNewFrame) so subsequent
     * renderers (lobby, pause, update) use the correct resolution-scaled value. */
    io.FontGlobalScale = pdguiScaleFactor();
}

} /* extern "C" */
