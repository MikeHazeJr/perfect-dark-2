/**
 * pdgui_menu_mainmenu.cpp -- ImGui replacement for the CI Main Menu.
 *
 * Replaces g_CiMenuViaPcMenuDialog and g_CiMenuViaPauseMenuDialog
 * ("Perfect Menu" — the Carrington Institute hub).
 *
 * Layout: Two tabs — "Play" and "Settings".
 *   Play:     Solo Missions, Combat Simulator, Co-Op, Counter-Op, Network Game
 *   Settings: Unified settings that merge original PD options + port extended options
 *
 * Each Play button invokes the same game functions as the original PD menu handlers
 * (setting g_MissionConfig, calling menuPushDialog, etc.).
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Use extern "C" forward declarations for all game symbols.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* S306: direct.h / unistd.h provide rmdir() for the BATCH 2 theme/mod
 * deletion flow. MinGW uses _rmdir; POSIX uses rmdir. */
#ifdef _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif

#include "imgui/imgui.h"
#include "versioninfo.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_glyphs.h"
#include "pdgui_theme_loader.h"
#include "pdgui_theme.h"
#include "pdgui_font_mod.h"
#include "pdgui_menu_stats.h"
#include "pdgui_menu_theme_editor.h"
#include "pdgui_forge.h"
#include "pdgui_menu_grid.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "pdgui_widgets.h"   /* Priority L: shared label-left widget helpers */
#include "system.h"
#include "inputctx.h"
#include "assetcatalog.h"
#include "net/netmanifest.h"

extern "C" {
#include "pdgui_nav.h"
#include "actionmap.h"
#include "menupool.h"
#include "game/forgemode.h"
/* net/matchsetup.h has no extern "C" wrap of its own; pull it in
 * under the C linkage block so matchConfigInit() etc. resolve to the
 * unmangled C definitions in port/src/net/matchsetup.c. */
#include "net/matchsetup.h"
}

/* ========================================================================
 * Forward declarations for game symbols
 * ======================================================================== */

extern "C" {

/* Dialog definitions we're replacing */
extern struct menudialogdef g_CiMenuViaPcMenuDialog;
extern struct menudialogdef g_CiMenuViaPauseMenuDialog;

/* Options dialog — needed for navigation */
extern struct menudialogdef g_CiOptionsViaPcMenuDialog;
extern struct menudialogdef g_CiOptionsViaPauseMenuDialog;

/* S195 Batch 3 — CI Options sub-dialogs.  Registered with redirect renderer
 * that pops the CI dialog and opens the unified Settings view on the
 * matching sub-tab.  P2 variants are dead (no split-screen).  The
 * "Dialog2" PAL-only variant (mainmenu.c:3316 inside #if VERSION >=
 * VERSION_PAL_FINAL) is not present in NTSC builds; S196 drops the
 * unconditional extern that was causing link failures on NTSC. */
extern struct menudialogdef g_CiControlOptionsMenuDialog;
extern struct menudialogdef g_CiControlStyleMenuDialog;
extern struct menudialogdef g_CiDisplayMenuDialog;
extern struct menudialogdef g_CiControlStylePlayer2MenuDialog;
extern struct menudialogdef g_CiDisplayPlayer2MenuDialog;
extern struct menudialogdef g_CiControlPlayer2MenuDialog;

/* Play target dialogs */
extern struct menudialogdef g_SelectMissionMenuDialog;
extern struct menudialogdef g_CombatSimulatorMenuDialog;
extern struct menudialogdef g_NetMenuDialog;
extern struct menudialogdef g_ChangeAgentMenuDialog;
extern struct menudialogdef g_CheatsMenuDialog;

/* D5 P3 Batch 4 -- Cinema dialog (cutscene viewer). */
extern struct menudialogdef g_CinemaMenuDialog;

/* Match setup init (from matchsetup.c) -- now provided via
 * port/include/net/matchsetup.h (included above); keeping the comment
 * as a breadcrumb. */

/* Extended options (port-added) */
extern struct menudialogdef g_ExtendedMenuDialog;
extern struct menudialogdef g_ExtendedVideoMenuDialog;
extern struct menudialogdef g_ExtendedAudioMenuDialog;
extern struct menudialogdef g_ExtendedMouseMenuDialog;

/* Mission config — set before opening mission select */
struct missionconfig {
    /* Layout matches types.h — only the fields we need */
    u8 _pad[0x00];
    s32 stageindex;     /* ... more fields ... */
};

/* We only need iscoop / isanti — which are bitfield-ish bools.
 * In the real struct they sit at specific offsets. Since we just
 * need to set them to true/false, we'll call the handlers directly
 * or use the real extern. Let's forward-declare the struct opaquely
 * and access the fields we know about. */

/* g_MissionConfig — direct access to the two bool fields we need.
 * In the real types.h, missionconfig has iscoop at offset 0x10 and
 * isanti at 0x11.  But since we can't include types.h, we'll use
 * the handler functions approach instead. */

/* Game state functions */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);
s32 menuIsDialogOpen(struct menudialogdef *dialogdef);
s32 menuDialogIsCurrent(const struct menudialog *dialog);

/* Pause/control restoration — needed when ImGui menu close bypasses
 * the legacy menutick bg-transition that normally calls func0f0fa6ac. */
void playerUnpause(void);
void lvSetPaused(bool paused);
s32 lvIsPaused(void);
extern bool g_PlayersWithControl[];

/* Handlers we invoke to match original behavior */
struct menuitem;
union handlerdata;
typedef s32 MenuItemHandlerResult;

MenuItemHandlerResult menuhandlerMainMenuSoloMissions(s32 operation, struct menuitem *item, union handlerdata *data);
MenuItemHandlerResult menuhandlerMainMenuCombatSimulator(s32 operation, struct menuitem *item, union handlerdata *data);
MenuItemHandlerResult menuhandlerMainMenuCooperative(s32 operation, struct menuitem *item, union handlerdata *data);
MenuItemHandlerResult menuhandlerMainMenuCounterOperative(s32 operation, struct menuitem *item, union handlerdata *data);

/* MENUOP_* opcodes (from src/include/constants.h).
 * Full set needed for the Batch 4 Cinema list delegation. */
#define MENUOP_GETOPTIONCOUNT      1
#define MENUOP_GETOPTGROUPCOUNT    2
#define MENUOP_GETOPTIONTEXT       3
#define MENUOP_GETOPTGROUPTEXT     4
#define MENUOP_GETGROUPSTARTINDEX  5
#define MENUOP_SET                 6
#define MENUOP_GETSELECTEDINDEX    7
#define MENUOP_GET                 8

/* Player count check (for Counter-Op disabled state) */
u32 joyGetConnectedControllers(void);

/* Change agent handler */
MenuItemHandlerResult menuhandlerChangeAgent(s32 operation, struct menuitem *item, union handlerdata *data);

/* S309: per-agent preferences sidecar. Declared here because
 * prefs_agent.h lives in port/include/ and C++ ABI guard (#define bool
 * s32) blocks including it directly. */
void prefsAgentSave(void);
/* B-172: refresh pd.ini baseline after a pre-sign-in visual change so
 * the next Agent Select reset doesn't clobber the user's new theme. */
void prefsAgentRefreshVisualsBaseline(void);

/* Font atlas rebuild — triggers at the start of the next frame so the
 * new font is active immediately without requiring a restart. */
extern "C" void pdguiRequestFontAtlasRebuild(void);

/* Audio API — legacy (still used by original menus) */
s32 optionsGetMusicVolume(void);
void optionsSetMusicVolume(s32 vol);
void sndSetSfxVolume(s32 vol);

/* Look inversion — options.c */
s32 optionsGetForwardPitch(s32 mpchrnum);
void optionsSetForwardPitch(s32 mpchrnum, s32 enable);

/* Volume layer system — from port/include/audio.h */
f32 audioGetMasterVolume(void);
void audioSetMasterVolume(f32 vol);
f32 audioGetMusicVolume(void);
void audioSetMusicVolume(f32 vol);
f32 audioGetGameplayVolume(void);
void audioSetGameplayVolume(f32 vol);
f32 audioGetUiVolume(void);
void audioSetUiVolume(f32 vol);

/* Video API — from port/include/video.h */
s32 videoGetFullscreen(void);
void videoSetFullscreen(s32 fs);
s32 videoGetFullscreenMode(void);
void videoSetFullscreenMode(s32 mode);
s32 videoGetVsync(void);
void videoSetVsync(s32 vsync);
s32 videoGetFramerateLimit(void);
void videoSetFramerateLimit(s32 limit);
s32 videoGetMSAA(void);
void videoSetMSAA(s32 msaa);
u32 videoGetTextureFilter(void);
void videoSetTextureFilter(u32 filter);
s32 videoGetTextureFilter2D(void);
void videoSetTextureFilter2D(s32 filter);
s32 videoGetDetailTextures(void);
void videoSetDetailTextures(s32 detail);
s32 videoGetDisplayFPS(void);
void videoSetDisplayFPS(s32 displayfps);
s32 videoGetCenterWindow(void);
void videoSetCenterWindow(s32 center);
s32 videoGetMaximizeWindow(void);
void videoSetMaximizeWindow(s32 fs);
f32 videoGetUiScaleMult(void);
void videoSetUiScaleMult(f32 mult);

/* Display mode */
typedef struct {
    s32 width;
    s32 height;
} displaymode;
s32 videoGetNumDisplayModes(void);
s32 videoGetDisplayMode(displaymode *out, s32 index);
s32 videoGetDisplayModeIndex(void);
void videoSetDisplayMode(s32 index);

/* Extended vars — from optionsmenu.c / other port files */
extern s32 g_TickRateDiv;
extern s32 g_BgunGeMuzzleFlashes;
extern s32 g_MusicDisableMpDeath;

/* Sound mode (Mono/Stereo/Headphone/Surround) — from src/game/propsnd.c / snd.c.
 * Values match SOUNDMODE_* in src/include/constants.h:
 *   0 = Mono, 1 = Stereo, 2 = Headphone, 3 = Surround */
extern s32 g_SoundMode;
void sndSetSoundMode(s32 mode);

extern s32 g_HudCenter;
extern f32 g_ViShakeIntensityMult;

/* Skip intro — from port/src/main.c, registered as Game.SkipIntro */
extern s32 g_SkipIntro;

/* Mouse API — from port/include/input.h */
s32 inputMouseIsEnabled(void);
void inputMouseEnable(s32 enabled);
void inputMouseGetSpeed(f32 *x, f32 *y);
void inputMouseSetSpeed(f32 x, f32 y);
s32 inputGetMouseLockMode(void);
void inputSetMouseLockMode(s32 mode);

/* Right stick Y invert — from port/include/input.h */
s32 inputControllerGetInvertRStickY(s32 cidx);
void inputControllerSetInvertRStickY(s32 cidx, s32 invert);

/* Stick swap — from port/include/input.h */
s32 inputControllerGetSticksSwapped(s32 cidx);
void inputControllerSetSticksSwapped(s32 cidx, s32 swapped);

/* M0.2 Phase D: Input binding now uses actionmap.h API exclusively.
 * CK_* enum and old bind functions removed. */
#define PD_VK_ESCAPE 41
#define PD_VK_JOY_BEGIN 519
#define PD_VK_TOTAL_COUNT (PD_VK_JOY_BEGIN + 4 * 32)
const char *inputGetKeyName(s32 vk);
void inputClearLastKey(void);
s32 inputGetLastKey(void);

extern s32 g_MenuMouseControl;

/* Player extended config — must match types.h layout exactly.
 * struct extplayerconfig (types.h:6114) */
struct extplayerconfig {
    f32 fovy;              /* 0x00 */
    f32 fovzoommult;       /* 0x04 */
    s32 fovzoom;           /* 0x08 */
    s32 mouseaimmode;      /* 0x0c */
    f32 mouseaimspeedx;    /* 0x10 */
    f32 mouseaimspeedy;    /* 0x14 */
    s32 crouchmode;        /* 0x18 */
    f32 radialmenuspeed;   /* 0x1c */
    f32 crosshairsway;     /* 0x20 */
    s32 extcontrols;       /* 0x24 */
    u32 crosshaircolour;   /* 0x28 */
    u32 crosshairsize;     /* 0x2c */
    s32 crosshairhealth;   /* 0x30 */
    s32 usereloads;        /* 0x34 */
    f32 jumpheight;        /* 0x38 */
};
extern struct extplayerconfig g_PlayerExtCfg[];

/* Window dimensions */
s32 viGetWidth(void);
s32 viGetHeight(void);

/* Language strings */
char *langGet(s32 textid);
char *langSafe(s32 textid);

/* Feature / challenge unlock gate -- used by Grid submenu arena filter. */
s32 challengeIsFeatureUnlocked(u32 feature);

/* Stage table lookup -- used by Grid submenu assertion at commit time. */
s32 stageGetIndex(s32 stagenum);

/* Random-meta resolution. Grid picker surfaces the "Random Multi" / "Random
 * Solo" meta-arenas as selectable rows; commit resolves them to a concrete
 * stagenum via these helpers. Defined in src/game/mplayer/setup.c. */
s16 mpChooseRandomMultiStage(void);
s16 mpChooseRandomSoloStage(void);

/* Meta-arena stagenum sentinels (mirror of constants.h to avoid pulling
 * the full chain into this C++ TU). */
#ifndef GRID_STAGE_MP_RANDOM_MULTI
#define GRID_STAGE_MP_RANDOM_MULTI 0x02
#define GRID_STAGE_MP_RANDOM_SOLO  0x03
#endif

/* STAGE_IS_* mirror (values locked in src/include/constants.h:4091+).
 * Defined locally so the C++ Main Menu TU can reject system / title
 * stages from the Grid arena picker without pulling in the full
 * constants.h chain (which risks the project's bool=s32 typedef).
 * STAGE_TITLE / STAGE_BOOTPAKMENU / STAGE_CREDITS are the three stages
 * STAGE_IS_SYSTEM covers; Grid must never load one. */
#ifndef GRID_STAGE_TITLE
#define GRID_STAGE_TITLE       0x5a
#define GRID_STAGE_BOOTPAKMENU 0x5b
#define GRID_STAGE_CREDITS     0x5c
#endif
#define GRID_STAGE_IS_SYSTEM(s) \
    ((s) == GRID_STAGE_TITLE || (s) == GRID_STAGE_BOOTPAKMENU || (s) == GRID_STAGE_CREDITS)
#define GRID_STAGE_IS_GAMEPLAY(s) (!GRID_STAGE_IS_SYSTEM(s))

/* MPOPTION_* bit mirrors (values locked in src/include/constants.h).
 * Grouped here so the C++ Main Menu TU does not need to include the
 * full constants.h (which pulls the project's bool=s32 typedef). */
#ifndef MPOPTION_ONEHITKILLS
#define MPOPTION_ONEHITKILLS    0x00000001u
#endif
#ifndef MPOPTION_TEAMSENABLED
#define MPOPTION_TEAMSENABLED   0x00000002u
#endif
#ifndef MPOPTION_SLOWMOTION_ON
#define MPOPTION_SLOWMOTION_ON  0x00000040u
#endif

/* g_MpPlayerNum */
extern s32 g_MpPlayerNum;

/* Config save */
s32 configSave(const char *fname);

/* M0.2 Phase D: inputModes API removed — inputmodes.c deleted. */

/* Button edge glow — from pdgui_style */
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

/* Update UI — from pdgui_menu_update.cpp */
void pdguiUpdateRenderSettingsTab(void);

/* Updater — from updater.c */
s32  updaterGetShowDevReleases(void);
void updaterSetShowDevReleases(s32 show);
void updaterCheckAsync(void);

/* Modding Hub UI — declared in pdgui_menu_moddinghub.cpp */
void pdguiModdingHubShow(void);
void pdguiModdingHubShowTool(s32 tool);  /* S306: jump directly to a tool */
void pdguiModdingHubHide(void);
s32  pdguiModdingHubIsVisible(void);

/* Modding Hub tool indices (mirror NUM_TOOLS layout in moddinghub.cpp).
 * Keep in sync with pdgui_menu_moddinghub.cpp s_ActiveTool comment. */
#define MODHUB_TOOL_MOD_MANAGER  0
#define MODHUB_TOOL_INI_EDITOR   1
#define MODHUB_TOOL_MODEL_SCALE  2
#define MODHUB_TOOL_PACK         3
#define MODHUB_TOOL_AUDIO_MODS   4
#define MODHUB_TOOL_SKIN_EDITOR  5
#define MODHUB_TOOL_MAP_IMPORT   6
#define MODHUB_TOOL_MENU_STYLE   7
#define MODHUB_TOOL_FONT_MOD     8

/* Solo Room screen — open the Room screen in offline (NETMODE_NONE) mode */
void pdguiSoloRoomOpen(void);
s32  pdguiSoloRoomIsActive(void);
void pdguiSoloMissionReset(void); /* F-1.2 */

/* Connect codes (connectcode.c) */
s32 connectCodeDecode(const char *code, u32 *outIp);
s32 connectCodeEncode(u32 ip, char *buf, s32 bufsize);
#define CONNECT_DEFAULT_PORT 27100
#define CONNECT_CODE_MAX     128

/* Recent server list — layout must match struct netrecentserver in net.h exactly.
 * NET_MAX_ADDR=256, NET_MAX_NAME=MAX_PLAYERNAME=15. */
#define PD_NET_MAX_RECENT_SERVERS 8
struct netrecentserver {
    char addr[257];       /* NET_MAX_ADDR + 1 */
    u32  protocol;
    u8   flags;
    u8   numclients;
    u8   maxclients;
    u8   stagenum;
    u8   scenario;
    char hostname[15];    /* NET_MAX_NAME */
    u32  lastresponse;
    bool online;
};
extern struct netrecentserver g_NetRecentServers[PD_NET_MAX_RECENT_SERVERS];
extern s32 g_NetNumRecentServers;

/* Network connect + async recent-server ping (net.c / netholepunch.c) */
s32 netStartClient(const char *addr);
s32 netStartClientWithHolePunch(const char *addr);
void netQueryRecentServersAsync(void);
void netPollRecentServers(void);
extern bool g_NetQueryInFlight;

/* Persistent memory diagnostics -- from memp.c */
void *mempPCAlloc(u32 size, const char *tag);
s32 mempPCValidate(const char *context);
u32 mempPCGetTotalAllocated(void);
u32 mempPCGetNumAllocations(void);
extern s32 g_OsMemSizeMb;

/* Screen size / screen split — options.c */
s32  optionsGetScreenSize(void);
void optionsSetScreenSize(s32 size);
u8   optionsGetScreenSplit(void);
void optionsSetScreenSplit(u8 split);

/* Subtitle options — options.c */
u8   optionsGetInGameSubtitles(void);
void optionsSetInGameSubtitles(s32 enable);
u8   optionsGetCutsceneSubtitles(void);
void optionsSetCutsceneSubtitles(s32 enable);

/* HUD display options (per-player) — options.c */
s32  optionsGetSightOnScreen(s32 mpchrnum);
void optionsSetSightOnScreen(s32 mpchrnum, s32 enable);
s32  optionsGetAlwaysShowTarget(s32 mpchrnum);
void optionsSetAlwaysShowTarget(s32 mpchrnum, s32 enable);
s32  optionsGetShowZoomRange(s32 mpchrnum);
void optionsSetShowZoomRange(s32 mpchrnum, s32 enable);
s32  optionsGetAmmoOnScreen(s32 mpchrnum);
void optionsSetAmmoOnScreen(s32 mpchrnum, s32 enable);
s32  optionsGetShowGunFunction(s32 mpchrnum);
void optionsSetShowGunFunction(s32 mpchrnum, s32 enable);
s32  optionsGetShowMissionTime(s32 mpchrnum);
void optionsSetShowMissionTime(s32 mpchrnum, s32 enable);
s32  optionsGetPaintball(s32 mpchrnum);
void optionsSetPaintball(s32 mpchrnum, s32 enable);
s32  optionsGetHeadRoll(s32 mpchrnum);
void optionsSetHeadRoll(s32 mpchrnum, s32 enable);

/* Combat assist options (per-player) — options.c */
s32  optionsGetAutoAim(s32 mpchrnum);
void optionsSetAutoAim(s32 mpchrnum, s32 enable);
s32  optionsGetLookAhead(s32 mpchrnum);
void optionsSetLookAhead(s32 mpchrnum, s32 enable);
s32  optionsGetAimControl(s32 mpchrnum);
void optionsSetAimControl(s32 mpchrnum, s32 index);

/* D5.0a: UI texture bridge — return ImTextureID for a named texture */
void* pdguiGetUiTexture(const char *id);

/* D5 P3 Batch 4 -- Cinema handler delegation
 *
 * menuhandlerCinema in mainmenu.c drives the legacy cutscene viewer.  We
 * call it through a shadow struct (s202 pattern; see pdgui_menu_cheats.cpp)
 * so the cinema group math, g_Vars.autocutgroupcur/left writes, and
 * menuStop trigger all stay inside the legacy handler -- single source of
 * truth for cinema dispatch. */
struct cn_handlerdata_list {
    uintptr_t value;        /* cutscene index (0 = Play All, 1+ = specific) */
    u32       unk04;
    s32       groupstartindex;
    s32       unk0c;
};
union cn_handlerdata {
    struct cn_handlerdata_list list;
    u8 _pad[64];
};
struct cn_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;
    intptr_t  param3;
    uintptr_t (*handler)(s32, struct cn_menuitem *, union cn_handlerdata *);
};
uintptr_t menuhandlerCinema(s32 op, struct cn_menuitem *, union cn_handlerdata *);

/* menuStop: stop the menu runtime so the cutscene takes over the screen. */
void menuStop(void);

} /* extern "C" */

/* Screen size constants (from src/include/constants.h).
 * Prefixed PD_ to avoid any collision with external headers. */
#define PD_SCREENSIZE_FULL    0
#define PD_SCREENSIZE_WIDE    1
#define PD_SCREENSIZE_CINEMA  2
#define PD_SCREENSPLIT_HORIZ  0
#define PD_SCREENSPLIT_VERT   1

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_RegisteredPc = false;
static bool s_RegisteredPause = false;
/* S306: Interface tab inserted between Video and Audio.
 * 0=Video, 1=Interface, 2=Audio, 3=Controls, 4=Game, 5=Updates,
 * 6=Debug (PD_DEV_BUILD only), 6/7=Catalog. */
static s32 s_SettingsSubTab = 0;
static s32 s_PrevView = -1;     /* Previous menu view, for sound on switch */
static s32 s_PrevSubTab = -1;
static bool s_ViewJustChanged = false; /* true on frame after s_MenuView changes */
static bool s_NeedsFocus = false;      /* one-shot: give nav focus to first widget */

/* ========================================================================
 * Sound-playing widget wrappers
 * ======================================================================== */

/* Button that plays SELECT sound on click, with edge glow on hover/active */
static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0,0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);

    /* Draw animated edge glow when hovered (mouse), active (pressed), or focused (gamepad nav) */
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* Priority L (2026-04-25): mainmenu's Pd* helpers now wrap the shared
 * `pdgui*` widgets defined in `port/fast3d/pdgui_widgets.cpp` so the
 * label-left layout pattern is single-source-of-truth across menus.
 * Existing call sites in this file keep the `PdCheckbox` / `PdCombo`
 * / etc. names; new code (and other menus' refactored call sites)
 * call `pdguiCheckbox` / `pdguiCombo` / etc. directly via
 * `pdgui_widgets.h`. */

static bool PdCheckbox(const char *label, bool *v)
{
    return pdguiCheckbox(label, v);
}

static bool PdCombo(const char *label, int *current_item, const char *const items[], int items_count)
{
    return pdguiCombo(label, current_item, items, items_count);
}

static bool PdSliderInt(const char *label, int *v, int v_min, int v_max, const char *format = "%d")
{
    return pdguiSliderInt(label, v, v_min, v_max, format);
}

static bool PdSliderFloat(const char *label, float *v, float v_min, float v_max, const char *format = "%.3f")
{
    return pdguiSliderFloat(label, v, v_min, v_max, format);
}

/* Controller sensitivity UI: 1-10 in 0.5 steps (snapped on edit), label LEFT.
 * Stays in mainmenu because of the snap-to-half-step semantic that the
 * generic helper doesn't carry. */
static bool PdSliderSensUi(const char *label, float *v)
{
    pdguiSettingsBeginRow(label);
    char id[160];
    bool ch = ImGui::SliderFloat(pdguiSettingsHashId(label, id, sizeof(id)),
                                 v, 1.0f, 10.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp);
    if (ch) {
        *v = actionmapSnapSensUi(*v);
    }
    return ch;
}

/* Controller tab: 0 = movement on physical left stick, 1 = movement on physical right stick.
 * Look / aim always uses the other stick; keeps actionmap swap flag and SDL axis map aligned. */
static void pdguiApplyMoveStickLayout(int moveStickUiIdx)
{
    actionmapSetMoveStickPhysicalLeft(moveStickUiIdx == 0 ? 1 : 0);
    inputControllerSetSticksSwapped(0, moveStickUiIdx == 0 ? 0 : 1);
}

/* ========================================================================
 * Settings Sub-Tab Renderers
 * ======================================================================== */

static void renderSettingsVideo(float scale)
{
    /* ---- Display ---- */
    ImGui::TextDisabled("Display");
    ImGui::Separator();

    bool fullscreen = videoGetFullscreen() != 0;
    if (PdCheckbox("Fullscreen", &fullscreen)) {
        videoSetFullscreen(fullscreen ? 1 : 0);
    }

    if (fullscreen) {
        int fsMode = videoGetFullscreenMode();
        const char *fsModes[] = { "Borderless", "Exclusive" };
        if (PdCombo("Fullscreen Mode", &fsMode, fsModes, 2)) {
            videoSetFullscreenMode(fsMode);
        }
    }

    /* Resolution */
    {
        int numModes = videoGetNumDisplayModes();
        int curIdx = videoGetDisplayModeIndex();
        displaymode curMode;
        videoGetDisplayMode(&curMode, curIdx);

        char curLabel[64];
        if (curMode.width == 0 && curMode.height == 0) {
            snprintf(curLabel, sizeof(curLabel), "Custom");
        } else {
            snprintf(curLabel, sizeof(curLabel), "%dx%d", curMode.width, curMode.height);
        }

        bool disabled = fullscreen && videoGetFullscreenMode() == 0;
        if (disabled) ImGui::BeginDisabled();

        if (ImGui::BeginCombo("Resolution", curLabel)) {
            for (int i = 0; i < numModes; i++) {
                displaymode m;
                videoGetDisplayMode(&m, i);
                char label[64];
                if (m.width == 0 && m.height == 0) {
                    snprintf(label, sizeof(label), "Custom");
                } else {
                    snprintf(label, sizeof(label), "%dx%d", m.width, m.height);
                }
                bool selected = (i == curIdx);
                if (ImGui::Selectable(label, selected)) {
                    videoSetDisplayMode(i);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        if (disabled) ImGui::EndDisabled();
    }

    /* UI Scale */
    {
        float uiScale = videoGetUiScaleMult() * 100.0f;
        char uiScaleLabel[16];
        snprintf(uiScaleLabel, sizeof(uiScaleLabel), "%.0f%%", uiScale);
        if (PdSliderFloat("UI Scale", &uiScale, 50.0f, 200.0f, uiScaleLabel)) {
            videoSetUiScaleMult(uiScale / 100.0f);
        }
    }

    bool centerWin = videoGetCenterWindow() != 0;
    if (PdCheckbox("Center Window", &centerWin)) {
        videoSetCenterWindow(centerWin ? 1 : 0);
    }

    if (!fullscreen) {
        bool maximize = videoGetMaximizeWindow() != 0;
        if (PdCheckbox("Maximize Window", &maximize)) {
            videoSetMaximizeWindow(maximize ? 1 : 0);
        }
    }

    ImGui::Spacing();

    /* ---- Performance ---- */
    ImGui::TextDisabled("Performance");
    ImGui::Separator();

    /* VSync */
    {
        int vsync = videoGetVsync() + 1; /* -1=adaptive, 0=off, 1+=on */
        const char *vsyncOpts[] = { "Adaptive", "Off", "On" };
        int vsyncIdx = vsync;
        if (vsyncIdx < 0) vsyncIdx = 0;
        if (vsyncIdx > 2) vsyncIdx = 2;
        if (PdCombo("VSync", &vsyncIdx, vsyncOpts, 3)) {
            videoSetVsync(vsyncIdx - 1);
        }
    }

    /* Framerate Limit */
    {
        int fpsLimit = videoGetFramerateLimit();
        char fpsLabel[32];
        if (fpsLimit == 0) {
            snprintf(fpsLabel, sizeof(fpsLabel), "Off");
        } else {
            snprintf(fpsLabel, sizeof(fpsLabel), "%d FPS", fpsLimit);
        }
        if (PdSliderInt("Framerate Limit", &fpsLimit, 0, 480, fpsLabel)) {
            videoSetFramerateLimit(fpsLimit);
        }
    }

    bool uncapTick = (g_TickRateDiv == 0);
    if (PdCheckbox("Uncap Tickrate", &uncapTick)) {
        g_TickRateDiv = uncapTick ? 0 : 1;
    }

    bool showFps = videoGetDisplayFPS() != 0;
    if (PdCheckbox("Display FPS", &showFps)) {
        videoSetDisplayFPS(showFps ? 1 : 0);
    }

    ImGui::Spacing();

    /* ---- Rendering ---- */
    ImGui::TextDisabled("Rendering");
    ImGui::Separator();

    /* Anti-aliasing */
    {
        int msaa = videoGetMSAA();
        int msaaIdx = 0;
        if (msaa >= 16) msaaIdx = 4;
        else if (msaa >= 8) msaaIdx = 3;
        else if (msaa >= 4) msaaIdx = 2;
        else if (msaa >= 2) msaaIdx = 1;

        const char *msaaOpts[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA" };
        if (PdCombo("Anti-aliasing *", &msaaIdx, msaaOpts, 5)) {
            videoSetMSAA(1 << msaaIdx);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(restart required)");
    }

    /* Texture Filtering */
    {
        int texFilter = (int)videoGetTextureFilter();
        const char *texOpts[] = { "Nearest", "Bilinear", "Three Point" };
        if (PdCombo("Texture Filtering", &texFilter, texOpts, 3)) {
            videoSetTextureFilter((u32)texFilter);
        }
    }

    bool guiTexFilter = videoGetTextureFilter2D() != 0;
    if (PdCheckbox("GUI Texture Filtering", &guiTexFilter)) {
        videoSetTextureFilter2D(guiTexFilter ? 1 : 0);
    }

    bool detailTex = videoGetDetailTextures() != 0;
    if (PdCheckbox("Detail Textures", &detailTex)) {
        videoSetDetailTextures(detailTex ? 1 : 0);
    }

    /* CRT Filter */
    bool crtOn = pdguiThemeGetScanlineEnabled() != 0;
    if (PdCheckbox("CRT Filter", &crtOn)) {
        pdguiThemeSetScanlineEnabled(crtOn ? 1 : 0);
    }

    if (crtOn) {
        float crtAlpha = pdguiThemeGetScanlineAlpha();
        int crtPct = (int)(crtAlpha * 100.0f + 0.5f);
        if (PdSliderInt("CRT Strength", &crtPct, 0, 100, "%d%%")) {
            pdguiThemeSetScanlineAlpha((float)crtPct / 100.0f);
        }
        /* 2026-04-23: vertical spacing slider. 0.50x = denser lines,
         * 2.00x = wider spacing, 1.00x = default. */
        float crtV = pdguiThemeGetScanlineVerticalScale();
        if (PdSliderFloat("CRT Line Spacing", &crtV, 0.25f, 4.0f, "%.2fx")) {
            pdguiThemeSetScanlineVerticalScale(crtV);
        }
    }

    /* S306: Menu Style / Title Bar Style / Font dropdowns moved to the new
     * Settings → Interface tab. A single pointer to that tab keeps the
     * Video section focused on raw display / rendering settings. */
    ImGui::TextDisabled("Menu Style, Title Bar, Color Theme, Font → Settings → Interface");

    ImGui::Spacing();

    /* ---- Gameplay Visuals ---- */
    ImGui::TextDisabled("Gameplay Visuals");
    ImGui::Separator();

    /* HUD Centering */
    {
        int hudCenter = g_HudCenter;
        const char *hudOpts[] = { "None", "4:3", "Wide" };
        if (PdCombo("HUD Centering", &hudCenter, hudOpts, 3)) {
            g_HudCenter = hudCenter;
        }
    }

    bool geMuzzle = g_BgunGeMuzzleFlashes != 0;
    if (PdCheckbox("GE64-style Muzzle Flashes", &geMuzzle)) {
        g_BgunGeMuzzleFlashes = geMuzzle ? 1 : 0;
    }

    {
        float shake = g_ViShakeIntensityMult;
        if (PdSliderFloat("Explosion Shake", &shake, 0.0f, 2.0f, "%.1f")) {
            g_ViShakeIntensityMult = shake;
        }
    }

    ImGui::Spacing();

    /* ---- Gameplay Screen ---- */
    ImGui::TextDisabled("Gameplay Screen");
    ImGui::Separator();

    /* Screen Size — controls the 3D viewport clip region inside the window */
    {
        int sz = optionsGetScreenSize();
        if (sz < 0 || sz > 2) sz = PD_SCREENSIZE_FULL;
        const char *szOpts[] = { "Full", "Wide", "Cinema" };
        if (PdCombo("Screen Size", &sz, szOpts, 3)) {
            optionsSetScreenSize(sz);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Full:   standard viewport fills the frame\n"
                "Wide:   moderate letterbox (top/bottom bars)\n"
                "Cinema: heavy letterbox (cinematic crop)");
        }
    }

    /* 2-Player Screen Split — orientation when two local players are active */
    {
        int sp = (int)optionsGetScreenSplit();
        if (sp < 0 || sp > 1) sp = PD_SCREENSPLIT_HORIZ;
        const char *spOpts[] = { "Horizontal", "Vertical" };
        if (PdCombo("2-Player Screen Split", &sp, spOpts, 2)) {
            optionsSetScreenSplit((u8)sp);
        }
    }
}

/* ========================================================================
 * Interface tab (S306) — consolidates all UI / visual-customization
 * controls in one tab. Pulls Menu Style + Title Bar + Font out of Video
 * and the Color Theme selector out of Debug, then adds launchers for the
 * deeper tools (Theme Editor, Menu Style tool in the Modding Hub).
 * ======================================================================== */

/* Built-in theme count — used by both the Color Theme picker and the
 * delete-theme guard (built-ins cannot be deleted). Defined here so it's
 * visible before both the delete helpers and the renderer. */
#define INTERFACE_NUM_BUILTIN_THEMES 7

/* BATCH 2 — delete-theme / delete-mod support (shared state + helpers).
 * The delete flow is: right-click a user theme → "Delete Theme..." → sets
 * s_InterfaceDeleteTarget to the theme index → renderDeleteConfirm opens
 * a modal next frame → user confirms → fsRemoveAll on the theme's mod
 * dir → pdguiThemeRescanMods. Mirrors the S255 push flow in net rooms so
 * the UI stays in a consistent popup stack while destructive work runs. */
enum InterfaceDeleteKind {
    IFACE_DEL_NONE = 0,
    IFACE_DEL_THEME,
    IFACE_DEL_MOD,
};

static s32 s_InterfaceDeleteTarget = -1;          /* theme index or mod index */
static int s_InterfaceDeleteKind   = IFACE_DEL_NONE;
static char s_InterfaceDeleteName[96] = "";       /* captured for the confirm dialog */
static char s_InterfaceDeletePath[280] = "";      /* mods/<slug>/ path to remove */
static char s_InterfaceDeleteStatus[160] = "";    /* last status string (red/green) */
static bool s_InterfaceDeleteSuccess = false;

/* Forward decl for theme path helper — implementation lives in the theme
 * loader (see pdgui_theme_loader.h). We prefer pdguiThemeGetFilePath
 * because it returns the theme.json path, from which we can derive the
 * parent dir. If the theme is built-in, the path is empty and the delete
 * request is silently rejected. */
extern "C" const char *pdguiThemeGetFilePath(s32 index);
extern "C" const char *pdguiThemeGetName(s32 index);
extern "C" void pdguiThemeRescanMods(void);

/* Best-effort mod directory deletion. The Save-as-Mod path in theme
 * editor / chrome tool / modmgr writes a predictable set of files
 * (theme.json, mod.json, audio.ini, optional component manifests). We
 * `remove()` each candidate, then try `rmdir()` — which succeeds iff the
 * directory is empty after the known files are gone. User-added extras
 * stop the rmdir cleanly so we never nuke unrelated files the player
 * copied in by hand. Returns 1 on rmdir success. */
static s32 interfaceDeleteModDir(const char *dirPath)
{
    if (!dirPath || !dirPath[0]) return 0;

    static const char *k_known[] = {
        "theme.json", "mod.json", "audio.ini",
        "chrome.png", "chrome.tga",
        "character.ini", "skin.ini", "map.ini", "bot.ini", "weapon.ini",
        /* font mods can leave a .ttf sitting in the directory — we do
         * not auto-delete them because the user might want to re-use the
         * font for another theme. rmdir() will surface the failure. */
        NULL
    };

    char p[320];
    for (int i = 0; k_known[i]; i++) {
        snprintf(p, sizeof(p), "%s/%s", dirPath, k_known[i]);
        (void)remove(p); /* silently ignore not-found */
    }

#ifdef _WIN32
    return _rmdir(dirPath) == 0 ? 1 : 0;
#else
    return rmdir(dirPath) == 0 ? 1 : 0;
#endif
}

/* Public entry points (called from Color Theme picker above + Mod Manager
 * in a later commit). Capture the target + name + on-disk path now so the
 * confirm dialog can render even after the popup owner (context menu)
 * closes. */
extern "C" void pdguiInterfaceRequestThemeDelete(s32 themeIndex)
{
    if (themeIndex < 0) return;
    const char *id   = pdguiThemeGetId(themeIndex);
    const char *name = pdguiThemeGetName(themeIndex);
    if (!id || !name) return;
    s32 builtin = pdguiThemeIdToPaletteIndex(id);
    if (builtin >= 0 && builtin < INTERFACE_NUM_BUILTIN_THEMES) {
        /* Refuse to delete built-ins. */
        snprintf(s_InterfaceDeleteStatus, sizeof(s_InterfaceDeleteStatus),
                 "Built-in themes cannot be deleted.");
        s_InterfaceDeleteSuccess = false;
        return;
    }
    const char *fp = pdguiThemeGetFilePath(themeIndex);
    if (!fp || !fp[0]) return;

    /* fp is like ".../mods/<slug>/theme.json" — strip the trailing filename */
    snprintf(s_InterfaceDeletePath, sizeof(s_InterfaceDeletePath), "%s", fp);
    char *slash = strrchr(s_InterfaceDeletePath, '/');
    if (!slash) slash = strrchr(s_InterfaceDeletePath, '\\');
    if (slash) *slash = '\0';

    s_InterfaceDeleteTarget = themeIndex;
    s_InterfaceDeleteKind   = IFACE_DEL_THEME;
    snprintf(s_InterfaceDeleteName, sizeof(s_InterfaceDeleteName), "%s", name);
    s_InterfaceDeleteStatus[0] = '\0';
}

extern "C" void pdguiInterfaceRequestModDelete(s32 modIndex,
                                               const char *displayName,
                                               const char *modDirPath)
{
    if (modIndex < 0) return;
    if (!displayName || !modDirPath) return;
    s_InterfaceDeleteTarget = modIndex;
    s_InterfaceDeleteKind   = IFACE_DEL_MOD;
    snprintf(s_InterfaceDeleteName, sizeof(s_InterfaceDeleteName), "%s", displayName);
    snprintf(s_InterfaceDeletePath, sizeof(s_InterfaceDeletePath), "%s", modDirPath);
    s_InterfaceDeleteStatus[0] = '\0';
}

extern "C" void pdguiInterfaceRenderDeleteConfirm(void)
{
    if (s_InterfaceDeleteKind == IFACE_DEL_NONE && !s_InterfaceDeleteStatus[0]) {
        return;
    }

    /* Open the popup the same frame the request fires. IsPopupOpen is the
     * guard against repeat OpenPopup calls (same-frame stacking). */
    const char *popupId = "Delete?##iface_delete_confirm";
    if (s_InterfaceDeleteKind != IFACE_DEL_NONE &&
            !ImGui::IsPopupOpen(popupId)) {
        ImGui::OpenPopup(popupId);
    }

    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(popupId, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {

        if (s_InterfaceDeleteKind == IFACE_DEL_THEME) {
            ImGui::Text("Delete the theme \"%s\"?", s_InterfaceDeleteName);
        } else if (s_InterfaceDeleteKind == IFACE_DEL_MOD) {
            ImGui::Text("Delete the mod \"%s\"?", s_InterfaceDeleteName);
        } else {
            /* Idle / status-only path: previous delete finished, we're
             * just showing the status string. */
            ImVec4 col = s_InterfaceDeleteSuccess
                ? ImVec4(0.3f, 1.0f, 0.3f, 1.0f)
                : ImVec4(1.0f, 0.55f, 0.2f, 1.0f);
            ImGui::TextColored(col, "%s", s_InterfaceDeleteStatus);
            if (ImGui::Button("OK")) {
                s_InterfaceDeleteStatus[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return;
        }

        ImGui::TextDisabled("This removes theme.json / mod.json / audio.ini in:");
        ImGui::TextWrapped("  %s", s_InterfaceDeletePath);
        ImGui::Spacing();
        ImGui::TextDisabled("The change takes effect immediately and cannot be undone.");
        ImGui::Spacing();

        /* S311: confirm-modal buttons now scale with pdguiScaleFactor so the
         * Cancel/Delete pair reads the same at 1080p and 4K. */
        ImVec2 confirmBtn(pdguiScale(120.0f), 0);
        if (ImGui::Button("Cancel", confirmBtn)) {
            s_InterfaceDeleteKind = IFACE_DEL_NONE;
            s_InterfaceDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        /* Red-tinted Delete button — push the theme's warning colour if
         * available, fall back to a hard-coded red so the affordance reads
         * even on palette-missing setups. */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.10f, 0.10f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.15f, 0.15f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        if (ImGui::Button("Delete", confirmBtn)) {
            s32 ok = interfaceDeleteModDir(s_InterfaceDeletePath);
            if (ok) {
                snprintf(s_InterfaceDeleteStatus, sizeof(s_InterfaceDeleteStatus),
                         "Deleted \"%s\" from mods/.", s_InterfaceDeleteName);
                s_InterfaceDeleteSuccess = true;
                /* Refresh the theme registry so the UI catches up without
                 * a restart. For mod deletes we also re-scan the mods
                 * directory and refresh the Mod Manager snapshot so the
                 * list drops the removed entry on the next frame. */
                pdguiThemeRescanMods();
                if (s_InterfaceDeleteKind == IFACE_DEL_MOD) {
                    extern void modmgrRescanDirectory(void);
                    extern void modmgrSyncCatalogToRegistry(void);
                    extern void pdguiModManagerRefreshSnapshot(void);
                    modmgrRescanDirectory();
                    modmgrSyncCatalogToRegistry();
                    pdguiModManagerRefreshSnapshot();
                }
            } else {
                snprintf(s_InterfaceDeleteStatus, sizeof(s_InterfaceDeleteStatus),
                         "Could not fully delete \"%s\" — extra files may remain.",
                         s_InterfaceDeleteName);
                s_InterfaceDeleteSuccess = false;
            }
            sysLogPrintf(s_InterfaceDeleteSuccess ? LOG_NOTE : LOG_WARNING,
                         "INTERFACE: delete %s '%s' from %s -> %s",
                         s_InterfaceDeleteKind == IFACE_DEL_THEME ? "theme" : "mod",
                         s_InterfaceDeleteName, s_InterfaceDeletePath,
                         s_InterfaceDeleteSuccess ? "OK" : "partial");
            s_InterfaceDeleteKind   = IFACE_DEL_NONE;
            s_InterfaceDeleteTarget = -1;
            ImGui::CloseCurrentPopup();
            /* Re-open the popup next frame in status-only mode so the
             * user sees the result without having to re-navigate. */
            ImGui::OpenPopup(popupId);
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    }
}

/* Theme presentation tables — shared with a fallback disabled-list view
 * used by the Color Theme picker. Live above renderSettingsInterface so
 * it's visible here; the Debug tab no longer references them after the
 * S306 consolidation but keeping them accessible means future screens
 * can reuse the colour mapping. */
static const char *s_InterfaceThemeNames[] = {
    "Grey", "Blue", "Red", "Green", "White", "Silver", "Black & Gold"
};
/* INTERFACE_NUM_BUILTIN_THEMES defined above with delete helpers. */

static const ImVec4 s_InterfaceThemeAccents[] = {
    ImVec4(0.45f, 0.45f, 0.50f, 0.85f),
    ImVec4(0.15f, 0.30f, 0.70f, 0.85f),
    ImVec4(0.70f, 0.12f, 0.12f, 0.85f),
    ImVec4(0.10f, 0.55f, 0.20f, 0.85f),
    ImVec4(0.70f, 0.70f, 0.75f, 0.85f),
    ImVec4(0.55f, 0.55f, 0.60f, 0.85f),
    ImVec4(0.20f, 0.18f, 0.10f, 0.85f),
};
static const ImVec4 s_InterfaceThemeTexts[] = {
    ImVec4(0.90f, 0.90f, 0.90f, 1.0f),
    ImVec4(0.80f, 0.85f, 1.00f, 1.0f),
    ImVec4(1.00f, 0.80f, 0.80f, 1.0f),
    ImVec4(0.80f, 1.00f, 0.80f, 1.0f),
    ImVec4(0.15f, 0.15f, 0.20f, 1.0f),
    ImVec4(0.10f, 0.10f, 0.15f, 1.0f),
    ImVec4(0.90f, 0.78f, 0.35f, 1.0f),
};

static void renderSettingsInterface(float scale)
{
    /* ---- Intro ---- */
    ImGui::TextDisabled("Every visual-customization control lives here: colors,");
    ImGui::TextDisabled("menu chrome, title bar, font. Editors for deeper tweaks");
    ImGui::TextDisabled("launch into the Modding Hub.");
    ImGui::Spacing();

    float btnW = 110.0f * scale;
    float btnH = 24.0f * scale;

    /* ================================================================
     * Section 1 — Color Theme
     * ================================================================ */
    {
        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        ImGui::TextColored(warnCol, "Color Theme");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Accent + background palette. Right-click a custom theme to enable/disable.");
    ImGui::Spacing();

    const char *activeThemeId = pdguiThemeGetActiveId();
    s32 themeCount = pdguiThemeGetCount();

    static const ImVec4 k_ModAccent = ImVec4(0.30f, 0.18f, 0.45f, 0.85f);
    static const ImVec4 k_ModText   = ImVec4(0.95f, 0.85f, 0.55f, 1.00f);

    s32 shownOnCurrentRow = 0;
    bool modHeaderShown = false;
    for (s32 ti = 0; ti < themeCount; ti++) {
        const char *id   = pdguiThemeGetId(ti);
        const char *name = pdguiThemeGetName(ti);
        if (!id || !name) continue;

        s32 builtinIdx = pdguiThemeIdToPaletteIndex(id);
        bool isBuiltin = (builtinIdx >= 0 && builtinIdx < INTERFACE_NUM_BUILTIN_THEMES);
        bool selected  = (activeThemeId && strcmp(id, activeThemeId) == 0);
        bool themeEnabled = pdguiThemeIsEnabled(ti) != 0;

        if (!isBuiltin && !modHeaderShown) {
            if (shownOnCurrentRow > 0) shownOnCurrentRow = 0;
            ImGui::Spacing();
            ImGui::TextDisabled("Custom (from mods/)");
            modHeaderShown = true;
        }

        ImVec4 btnCol  = isBuiltin ? s_InterfaceThemeAccents[builtinIdx] : k_ModAccent;
        ImVec4 txtCol  = isBuiltin ? s_InterfaceThemeTexts[builtinIdx]   : k_ModText;

        if (!isBuiltin && !themeEnabled) {
            btnCol.w *= 0.35f;
            txtCol.w *= 0.45f;
        }

        ImVec4 btnHover = ImVec4(
            btnCol.x + 0.15f, btnCol.y + 0.15f, btnCol.z + 0.15f, 0.95f);
        ImVec4 btnActive = ImVec4(
            btnCol.x + 0.25f, btnCol.y + 0.25f, btnCol.z + 0.25f, 1.0f);

        if (selected) {
            btnCol.x += 0.12f; btnCol.y += 0.12f; btnCol.z += 0.12f; btnCol.w = 1.0f;
            ImGui::PushStyleColor(ImGuiCol_Border, txtCol);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f * scale);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActive);
        ImGui::PushStyleColor(ImGuiCol_Text, txtCol);

        char btnLabel[96];
        snprintf(btnLabel, sizeof(btnLabel), "%s##iface_theme_%d", name, (int)ti);

        if (!isBuiltin && !themeEnabled) {
            ImGui::Button(btnLabel, ImVec2(btnW, btnH));
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Disabled — right-click to re-enable, or X / Delete to remove");
            }
        } else {
            if (ImGui::Button(btnLabel, ImVec2(btnW, btnH))) {
                pdguiThemeLoadFromCatalog(id);
                configSave("pd.ini");
                /* B-172: keep the pd.ini baseline in sync so Agent Select
                 * reset doesn't revert this change on the next visit. */
                prefsAgentRefreshVisualsBaseline();
            }
        }

        if (!isBuiltin) {
            char ctxId[64];
            snprintf(ctxId, sizeof(ctxId), "##iface_themectx_%d", (int)ti);
            if (ImGui::BeginPopupContextItem(ctxId)) {
                if (themeEnabled) {
                    if (ImGui::MenuItem("Disable Theme")) {
                        pdguiThemeSetEnabled(ti, 0);
                    }
                } else {
                    if (ImGui::MenuItem("Enable Theme")) {
                        pdguiThemeSetEnabled(ti, 1);
                    }
                }
                /* S306 BATCH 2: delete for user themes — opens the confirm
                 * dialog handled in renderSettingsInterface below (see
                 * s_InterfaceDeleteTarget). */
                /* extern decl in file-scope extern "C" block at top */
                ImGui::Separator();
                if (ImGui::MenuItem("Delete Theme...")) {
                    pdguiInterfaceRequestThemeDelete(ti);
                }
                ImGui::EndPopup();
            }
        }

        ImGui::PopStyleColor(4);
        if (selected) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        shownOnCurrentRow++;
        if (shownOnCurrentRow < 3 && ti + 1 < themeCount) {
            s32 nextBuiltin = -1;
            const char *nextId = pdguiThemeGetId(ti + 1);
            if (nextId) nextBuiltin = pdguiThemeIdToPaletteIndex(nextId);
            bool nextIsMod = (nextBuiltin < 0);
            if (!(nextIsMod && !modHeaderShown)) {
                ImGui::SameLine();
            } else {
                shownOnCurrentRow = 0;
            }
        } else {
            shownOnCurrentRow = 0;
        }
    }

    ImGui::Spacing();

    /* Customize + delete confirm (BATCH 2 delete dialog is driven below).
     * "Open Color Editor..." is a direct launcher for pdguiThemeEditorShow. */
    if (ImGui::Button("Open Color Editor...", ImVec2(btnW * 2.0f, btnH))) {
        pdguiThemeEditorShow();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Full palette editor with live preview + Save-as-Mod.");
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* ================================================================
     * Section 2 — Menu Style
     * ================================================================ */
    {
        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        ImGui::TextColored(warnCol, "Menu Style");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Nine-slice chrome artwork for window frames. Applies live.");
    ImGui::Spacing();
    {
        const s32 styleCount = pdguiThemeGetChromeStyleCount();
        const s32 maxStyles = 31;
        const s32 usedStyles = styleCount < maxStyles ? styleCount : maxStyles;
        const char *chromeOpts[1 + maxStyles];
        chromeOpts[0] = "Procedural (built-in)";
        for (s32 i = 0; i < usedStyles; i++) {
            chromeOpts[i + 1] = pdguiThemeGetChromeStyleName(i);
        }

        int chromeIdx = 0;
        if (pdguiThemeGetUiChromeEnabled() && usedStyles > 0) {
            const char *savedStyleId = pdguiThemeGetUiChromeStyleId();
            chromeIdx = 1;
            for (s32 i = 0; i < usedStyles; i++) {
                const char *id = pdguiThemeGetChromeStyleId(i);
                if (savedStyleId && id && strcmp(savedStyleId, id) == 0) {
                    chromeIdx = (int)(i + 1);
                    break;
                }
            }
        }

        if (PdCombo("Menu Style", &chromeIdx, chromeOpts, 1 + usedStyles)) {
            if (chromeIdx <= 0) {
                pdguiThemeSetUiChromeEnabled(0);
                pdguiChromeSetEnabled(0);
            } else {
                const char *selectedId = pdguiThemeGetChromeStyleId(chromeIdx - 1);
                pdguiThemeSetUiChromeStyleId(selectedId);
                pdguiThemeSetUiChromeEnabled(1);
                pdguiSetPanelNineSlice(selectedId);
                pdguiChromeSetEnabled(1);
            }
            configSave("pd.ini");
            sysLogPrintf(LOG_NOTE,
                "UI.CHROME: style changed to '%s' (id=%s)",
                chromeOpts[chromeIdx],
                chromeIdx <= 0 ? "procedural"
                               : pdguiThemeGetChromeStyleId(chromeIdx - 1));
        }
    }
    if (ImGui::Button("Open Menu Style Tool...", ImVec2(btnW * 2.0f, btnH))) {
        /* Close the main menu so the Modding Hub modal has the foreground. */
        pdguiModdingHubShowTool(MODHUB_TOOL_MENU_STYLE);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Import a PNG, set corner insets, save as a new Menu Style mod.");
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* ================================================================
     * Section 3 — Title Bar Style
     * ================================================================ */
    {
        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        ImGui::TextColored(warnCol, "Title Bar Style");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Procedural pattern drawn behind window titles.");
    ImGui::Spacing();
    {
        s32 tbIdx = pdguiThemeGetTitleBarStyle();
        const char *tbOpts[PDGUI_TITLEBAR_STYLE_COUNT];
        for (s32 i = 0; i < PDGUI_TITLEBAR_STYLE_COUNT; i++) {
            tbOpts[i] = pdguiThemeGetTitleBarStyleName(i);
        }
        if (PdCombo("Title Bar Style", &tbIdx, tbOpts, PDGUI_TITLEBAR_STYLE_COUNT)) {
            pdguiThemeSetTitleBarStyle(tbIdx);
            configSave("pd.ini");
            sysLogPrintf(LOG_NOTE,
                "UI.TITLEBAR: style changed to '%s' (%d)",
                pdguiThemeGetTitleBarStyleName(tbIdx), tbIdx);
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* ================================================================
     * Section 4 — Font
     * ================================================================ */
    {
        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        ImGui::TextColored(warnCol, "Font");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Font swap takes effect immediately.");
    ImGui::TextDisabled("Drop .ttf / .otf files into mods/Fonts/<slug>/ and they show up here.");
    ImGui::Spacing();
    {
        s32 fontCount = pdguiFontModGetCount();
        const s32 maxFonts = 32;
        const char *fontOpts[1 + maxFonts];
        fontOpts[0] = "Handel Gothic (built-in)";
        s32 fontOptCount = 1;
        for (s32 i = 0; i < fontCount && fontOptCount < (s32)(sizeof(fontOpts) / sizeof(fontOpts[0])); i++) {
            fontOpts[fontOptCount++] = pdguiFontModGetName(i);
        }

        int fontIdx = 0;
        const char *activeFontId = pdguiFontModGetActiveId();
        if (activeFontId && activeFontId[0]) {
            for (s32 i = 0; i < fontCount; i++) {
                const char *id = pdguiFontModGetId(i);
                if (id && strcmp(activeFontId, id) == 0) { fontIdx = (int)(i + 1); break; }
            }
        }

        if (PdCombo("Font", &fontIdx, fontOpts, fontOptCount)) {
            if (fontIdx <= 0) {
                pdguiFontModSetActiveId("");
            } else {
                pdguiFontModSetActiveId(pdguiFontModGetId(fontIdx - 1));
            }
            configSave("pd.ini");
            pdguiRequestFontAtlasRebuild();
            sysLogPrintf(LOG_NOTE, "UI.FONT: selection changed to '%s'",
                fontOpts[fontIdx]);
        }

        ImGui::Spacing();
        if (ImGui::Button("Open Font Mod Tool...", ImVec2(btnW * 2.0f, btnH))) {
            pdguiModdingHubShowTool(MODHUB_TOOL_FONT_MOD);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Import a .ttf/.otf file as a new font mod.");
        }
    }

    /* BATCH 2 delete-theme confirm — rendered as an ImGui popup triggered
     * by pdguiInterfaceRequestThemeDelete above. See implementation near
     * the bottom of the Interface tab. */
    /* pdguiInterfaceRenderDeleteConfirm — declared at file scope */
    pdguiInterfaceRenderDeleteConfirm();

    /* S309: mirror the current Interface selections into the active
     * agent's prefs.ini sidecar.  If an agent is signed in, switching
     * to them later restores the same look.  Debounced writes inside
     * prefs_agent.c mean this is cheap per frame. */
    prefsAgentSave();
}

static void renderSettingsAudio(float scale)
{
    /* Volume layer system — four independent 0.0–1.0 float layers.
     * Displayed as 0–100% sliders. Each setter internally calls
     * audioApplyVolumes() to push composite values to the engine. */

    ImGui::TextDisabled("Volume Controls");
    ImGui::Separator();

    {
        float master = audioGetMasterVolume() * 100.0f;
        if (PdSliderFloat("Master Volume", &master, 0.0f, 100.0f, "%.0f%%")) {
            audioSetMasterVolume(master / 100.0f);
        }
    }

    {
        float music = audioGetMusicVolume() * 100.0f;
        if (PdSliderFloat("Music", &music, 0.0f, 100.0f, "%.0f%%")) {
            audioSetMusicVolume(music / 100.0f);
        }
    }

    {
        float gameplay = audioGetGameplayVolume() * 100.0f;
        if (PdSliderFloat("Gameplay", &gameplay, 0.0f, 100.0f, "%.0f%%")) {
            audioSetGameplayVolume(gameplay / 100.0f);
        }
    }

    {
        float ui = audioGetUiVolume() * 100.0f;
        if (PdSliderFloat("UI", &ui, 0.0f, 100.0f, "%.0f%%")) {
            audioSetUiVolume(ui / 100.0f);
        }
    }

    ImGui::Separator();

    /* ---- Output ---- */
    ImGui::TextDisabled("Output");
    ImGui::Separator();

    /* Sound Mode (absorbed from CI Options — g_SoundMode / sndSetSoundMode) */
    {
        s32 mode = g_SoundMode;
        if (mode < 0 || mode > 3) mode = 1; /* clamp; default = Stereo */
        const char *modeOpts[] = { "Mono", "Stereo", "Headphone", "Surround" };
        if (PdCombo("Sound Mode", &mode, modeOpts, 4)) {
            sndSetSoundMode(mode);
        }
    }

    ImGui::Spacing();

    bool disableMpDeath = g_MusicDisableMpDeath != 0;
    if (PdCheckbox("Disable MP Death Music", &disableMpDeath)) {
        g_MusicDisableMpDeath = disableMpDeath ? 1 : 0;
    }
}

/* ============================================================================
 * Input Mapping (Priority P / B-252 -- per-IMC tab rebuild).
 *
 * S306 surfaced 51 actions on g_ImcGameplay only. After Priority J split
 * Gameplay into a shared baseline plus Mission and CombatSim, and after
 * Vehicle / Forge / Menu / PauseMenu / DebugOverlay / TextInput each grew
 * their own bindings, the flat single-IMC view stopped reflecting what the
 * dispatcher actually consumes. Phase 2 rebuilds the menu around the IMC
 * dispatcher: an outer set of tabs labelled by scene (Mission, Combat
 * Simulator, Vehicle, The Grid, Menu, System) wraps the existing
 * Keyboard & Mouse / Controller inner sub-tabs. Each row carries an
 * explicit owning IMC; rebinding writes into that IMC; reading reads from
 * that IMC; reset writes per-tab.
 *
 * Hold-vs-tap appears as sibling rows: Combat Simulator surfaces both
 * "Scorecard (tap)" (gameplay-baseline) and "Scorecard (hold N ms)"
 * (CombatSim-only ACTION_SCORECARD_HOLD), with the hold threshold read
 * live from actionmapGetEffectiveHoldMs(). The tap row remains visible
 * inside the Mission tab as well so users see both halves of the split.
 *
 * The save / load model already routes correctly: each (player, action,
 * IMC) tuple is the first IMC with has_mapping[action]==1 in
 * s_AllImcs[]; the rebuild's per-row owningImc selection matches that
 * iteration order, so pd.ini round-trips without schema changes.
 *
 * Capture, search filter, conflict highlighting, right-click-to-clear,
 * controller visual mapper, drag-drop, per-action hold overrides, and
 * Esc-cancel are preserved; each helper now takes an explicit
 * InputMappingContext * parameter so a single capture can be scoped to
 * the active tab's owning IMC.
 * ========================================================================= */

/* Capture state. s_CaptureImc is set when capture begins so the deferred
 * key handler writes back into the right IMC. s_CaptureName carries the
 * row's display label so the capture banner reads the correct phrase even
 * when an action is bound on multiple IMCs. */
static s32                  s_CaptureActive   = 0;
static InputAction          s_CaptureAction   = ACTION_MOVE_FORWARD;
static s32                  s_CaptureColumn   = 0;      /* 0 = MKB, 1 = Controller */
static s32                  s_CaptureBind     = 0;      /* trigger slot index */
static s32                  s_CaptureIsSecond = 0;
static InputMappingContext *s_CaptureImc      = NULL;
static const char          *s_CaptureName     = "";

/* Logical groupings. Each row belongs to exactly one group; each tab
 * surfaces a fixed set of groups. BG_COUNT is sentinel-only. The rendering
 * loop iterates groups in declaration order so the visible header order is
 * stable. */
enum BindableGroup {
    BG_MOVEMENT = 0,
    BG_AIM,
    BG_COMBAT,
    BG_WEAPONS,
    BG_HUD,            /* PAUSE, SCORECARD (tap)                                        */
    BG_HUD_CS,         /* SCORECARD_HOLD                                                */
    BG_VEHICLE,
    BG_CBUTTONS,
    BG_DPAD,
    BG_FORGE_SESSION,  /* FORGE_TOGGLE                                                  */
    BG_FORGE_EDITOR,   /* FORGE_ASCEND, _DESCEND, _BOOST, _PRECISION, _SIDEBAR_*, _TAB_* */
    BG_MENU,           /* USE, CANCEL_USE, PAUSE, MENU_*  on g_ImcMenu                  */
    BG_PAUSEMENU,      /* same actions on g_ImcPauseMenu                                */
    BG_DEBUG_OVERLAY,  /* DEBUG_TOGGLE, USE, CANCEL_USE, CONSOLE, SCREENSHOT            */
    BG_TEXT_INPUT,     /* USE, CANCEL_USE, CHEAT_ENTER                                  */
    BG_SYSTEM,         /* SCREENSHOT, CONSOLE_TOGGLE, DEBUG_TOGGLE, CHEAT_ENTER         */
    BG_COUNT
};

struct BindableGroupInfo {
    int         id;
    const char *name;
    const char *blurb;
};

static const BindableGroupInfo s_BindableGroups[BG_COUNT] = {
    { BG_MOVEMENT,       "Movement",        "Walking, strafing, jumping, crouching." },
    { BG_AIM,            "Aim",             "Looking, pitching, zoom in / out." },
    { BG_COMBAT,         "Combat",          "Fire, reload, use, throw weapon." },
    { BG_WEAPONS,        "Weapons",         "Next / Prev, direct weapon-slot hotkeys." },
    { BG_HUD,            "HUD",             "Pause and scoreboard peek (shared baseline)." },
    { BG_HUD_CS,         "Scoreboard hold", "Combat Simulator hold-to-show scoreboard." },
    { BG_VEHICLE,        "Vehicle",         "Drive + exit bindings for pilotable props." },
    { BG_CBUTTONS,       "C-Buttons",       "Legacy C-stick directions (GEX / Dark Noon)." },
    { BG_DPAD,           "D-Pad",           "Directional pad. Doubles as radial." },
    { BG_FORGE_SESSION,  "Forge Session",   "Active for the entire Grid session." },
    { BG_FORGE_EDITOR,   "Forge Editor",    "Active in Freefly only." },
    { BG_MENU,           "Menu",            "Navigation while a menu is on top." },
    { BG_PAUSEMENU,      "Pause Menu",      "Navigation while the pause menu is on top." },
    { BG_DEBUG_OVERLAY,  "Debug Overlay",   "F12 overlay rebinds (DebugOverlay IMC)." },
    { BG_TEXT_INPUT,     "Text Input",      "Chat / cheat entry rebinds (TextInput IMC)." },
    { BG_SYSTEM,         "System Hotkeys",  "Screenshots, debug overlay, cheat entry." },
};

struct BindableAction {
    InputAction          action;
    const char          *name;
    int                  group;
    const char          *tooltip;       /* NULL if no extra hint needed */
    InputMappingContext *owningImc;     /* IMC that holds this row's bindings */
};

/* Master action table. Each row binds an (action, owningImc) pair. The
 * same action may appear on multiple rows when more than one IMC owns
 * bindings for it (USE, CANCEL_USE, PAUSE, MENU_*, etc.). The save / load
 * iteration order in actionmap.cpp (Gameplay first, then Mission /
 * CombatSim / Vehicle / ForgeSession / Forge / Menu / PauseMenu /
 * DebugOverlay / TextInput) determines which IMC's binding round-trips
 * through pd.ini for shared actions; the row order here mirrors that
 * priority so the menu reflects the persisted truth. */
static const BindableAction s_BindableActions[] = {
    /* --- Movement (Gameplay baseline) --- */
    { ACTION_MOVE_FORWARD,        "Move Forward",       BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_MOVE_BACKWARD,       "Move Backward",      BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_MOVE_LEFT,           "Strafe Left",        BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_MOVE_RIGHT,          "Strafe Right",       BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_JUMP,                "Jump",               BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_CROUCH,              "Crouch",             BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_SPRINT,              "Sprint",             BG_MOVEMENT,      NULL,                                                                                                  &g_ImcGameplay },

    /* --- Aim (Gameplay) --- */
    { ACTION_AIM_UP,              "Look Up",            BG_AIM,           "Digital pitch up (controller / keyboard, not mouse).",                                                &g_ImcGameplay },
    { ACTION_AIM_DOWN,            "Look Down",          BG_AIM,           "Digital pitch down.",                                                                                 &g_ImcGameplay },
    { ACTION_AIM_LEFT,            "Look Left",          BG_AIM,           "Digital yaw left.",                                                                                   &g_ImcGameplay },
    { ACTION_AIM_RIGHT,           "Look Right",         BG_AIM,           "Digital yaw right.",                                                                                  &g_ImcGameplay },
    { ACTION_ZOOM_IN,             "Zoom In",            BG_AIM,           NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_ZOOM_OUT,            "Zoom Out",           BG_AIM,           NULL,                                                                                                  &g_ImcGameplay },

    /* --- Combat (Gameplay) --- */
    { ACTION_FIRE_PRIMARY,        "Fire",               BG_COMBAT,        NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_FIRE_SECONDARY,      "Aim Mode / Secondary", BG_COMBAT,      "Aim-mode on hold, secondary fire in aim mode.",                                                       &g_ImcGameplay },
    { ACTION_FIRE_MODE,           "Fire Mode",          BG_COMBAT,        "Cycle through a weapon's firing modes.",                                                              &g_ImcGameplay },
    { ACTION_RELOAD,              "Reload",             BG_COMBAT,        NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_USE,                 "Use / Interact",     BG_COMBAT,        "Doors, terminals, pickups. Tap reloads, hold interacts (threshold tunable in Per-action hold overrides).", &g_ImcGameplay },
    { ACTION_CANCEL_USE,          "Cancel / Drop",      BG_COMBAT,        "Abort an interaction or drop weapon.",                                                                &g_ImcGameplay },
    { ACTION_THROW_WEAPON,        "Throw Weapon",       BG_COMBAT,        NULL,                                                                                                  &g_ImcGameplay },

    /* --- Weapons (Gameplay) --- */
    { ACTION_WEAPON_PREV,         "Previous Weapon",    BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_WEAPON_NEXT,         "Next Weapon",        BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_WEAPON_1,            "Weapon Slot 1",      BG_WEAPONS,       "Direct hotkey for weapon slot 1.",                                                                    &g_ImcGameplay },
    { ACTION_WEAPON_2,            "Weapon Slot 2",      BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_WEAPON_3,            "Weapon Slot 3",      BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_WEAPON_4,            "Weapon Slot 4",      BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_WEAPON_5,            "Weapon Slot 5",      BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_WEAPON_6,            "Weapon Slot 6",      BG_WEAPONS,       NULL,                                                                                                  &g_ImcGameplay },

    /* --- HUD (Gameplay): Mission and Combat Simulator both surface these --- */
    { ACTION_PAUSE,               "Pause / Open Menu",  BG_HUD,           NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_SCORECARD,           "Scorecard (tap)",    BG_HUD,           "Transient scoreboard peek (Tab default). Combat Simulator adds a hold variant in its own tab.",       &g_ImcGameplay },

    /* --- HUD-CS (CombatSim): Combat Simulator only --- */
    { ACTION_SCORECARD_HOLD,      "Scorecard (hold)",   BG_HUD_CS,        "Hold-Back to surface scoreboard in Combat Simulator. Threshold tunable in Per-action hold overrides.", &g_ImcCombatSim },

    /* --- C-Buttons (Gameplay) --- */
    { ACTION_CBUTTON_UP,          "C-Up",               BG_CBUTTONS,      "Legacy C-stick up (GEX / Dark Noon inventory).",                                                      &g_ImcGameplay },
    { ACTION_CBUTTON_DOWN,        "C-Down",             BG_CBUTTONS,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_CBUTTON_LEFT,        "C-Left",             BG_CBUTTONS,      NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_CBUTTON_RIGHT,       "C-Right",            BG_CBUTTONS,      NULL,                                                                                                  &g_ImcGameplay },

    /* --- D-Pad (Gameplay) --- */
    { ACTION_DPAD_UP,             "D-Pad Up",           BG_DPAD,          NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_DPAD_DOWN,           "D-Pad Down",         BG_DPAD,          "Historically Radial Menu trigger.",                                                                   &g_ImcGameplay },
    { ACTION_DPAD_LEFT,           "D-Pad Left",         BG_DPAD,          NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_DPAD_RIGHT,          "D-Pad Right",        BG_DPAD,          NULL,                                                                                                  &g_ImcGameplay },

    /* --- Vehicle (Vehicle IMC, mounted only) --- */
    { ACTION_VEHICLE_ACCELERATE,  "Accelerate",         BG_VEHICLE,       NULL,                                                                                                  &g_ImcVehicle },
    { ACTION_VEHICLE_BRAKE,       "Brake / Reverse",    BG_VEHICLE,       NULL,                                                                                                  &g_ImcVehicle },
    { ACTION_VEHICLE_STEER_LEFT,  "Steer Left",         BG_VEHICLE,       NULL,                                                                                                  &g_ImcVehicle },
    { ACTION_VEHICLE_STEER_RIGHT, "Steer Right",        BG_VEHICLE,       NULL,                                                                                                  &g_ImcVehicle },
    { ACTION_VEHICLE_EXIT,        "Exit Vehicle",       BG_VEHICLE,       NULL,                                                                                                  &g_ImcVehicle },

    /* --- Forge Session (entire Grid session) --- */
    { ACTION_FORGE_TOGGLE,        "Mode Toggle (Forge / Playtest)", BG_FORGE_SESSION, "Enter or leave Freefly. Active for the entire Grid session.",                              &g_ImcForgeSession },

    /* --- Forge Editor (Freefly only) --- */
    { ACTION_FORGE_ASCEND,        "Camera Ascend",      BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_DESCEND,       "Camera Descend",     BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_BOOST,         "Speed Boost",        BG_FORGE_EDITOR,  "Hold for 3x freefly speed.",                                                                          &g_ImcForge },
    { ACTION_FORGE_PRECISION,     "Speed Precision",    BG_FORGE_EDITOR,  "Hold for 0.25x freefly speed.",                                                                       &g_ImcForge },
    { ACTION_FORGE_SIDEBAR_TOGGLE,"Sidebar Toggle",     BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_SIDEBAR_UP,    "Sidebar Up",         BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_SIDEBAR_DOWN,  "Sidebar Down",       BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_SIDEBAR_ACTIVATE,"Sidebar Activate", BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_TAB_PREV,      "Editor Tab Prev",    BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },
    { ACTION_FORGE_TAB_NEXT,      "Editor Tab Next",    BG_FORGE_EDITOR,  NULL,                                                                                                  &g_ImcForge },

    /* --- Menu IMC (top-level menus) --- */
    { ACTION_USE,                 "Accept",             BG_MENU,          "UI accept while a menu is on top. Distinct from gameplay Use.",                                       &g_ImcMenu },
    { ACTION_CANCEL_USE,          "Back",               BG_MENU,          "UI cancel. Distinct from gameplay Cancel.",                                                           &g_ImcMenu },
    { ACTION_PAUSE,               "Toggle Pause",       BG_MENU,          NULL,                                                                                                  &g_ImcMenu },
    { ACTION_MENU_UP,             "Menu Up",            BG_MENU,          NULL,                                                                                                  &g_ImcMenu },
    { ACTION_MENU_DOWN,           "Menu Down",          BG_MENU,          NULL,                                                                                                  &g_ImcMenu },
    { ACTION_MENU_LEFT,           "Menu Left",          BG_MENU,          NULL,                                                                                                  &g_ImcMenu },
    { ACTION_MENU_RIGHT,          "Menu Right",         BG_MENU,          NULL,                                                                                                  &g_ImcMenu },
    { ACTION_MENU_TAB_PREV,       "Prev Tab",           BG_MENU,          "LB / PageUp in settings tabs.",                                                                       &g_ImcMenu },
    { ACTION_MENU_TAB_NEXT,       "Next Tab",           BG_MENU,          "RB / PageDown in settings tabs.",                                                                     &g_ImcMenu },

    /* --- Pause Menu IMC (in-game pause overlay) --- */
    { ACTION_USE,                 "Accept",             BG_PAUSEMENU,     "UI accept while paused.",                                                                             &g_ImcPauseMenu },
    { ACTION_CANCEL_USE,          "Back",               BG_PAUSEMENU,     "UI cancel while paused.",                                                                             &g_ImcPauseMenu },
    { ACTION_PAUSE,               "Toggle Pause",       BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },
    { ACTION_MENU_UP,             "Menu Up",            BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },
    { ACTION_MENU_DOWN,           "Menu Down",          BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },
    { ACTION_MENU_LEFT,           "Menu Left",          BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },
    { ACTION_MENU_RIGHT,          "Menu Right",         BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },
    { ACTION_MENU_TAB_PREV,       "Prev Tab",           BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },
    { ACTION_MENU_TAB_NEXT,       "Next Tab",           BG_PAUSEMENU,     NULL,                                                                                                  &g_ImcPauseMenu },

    /* --- Debug Overlay IMC (F12 overlay) --- */
    { ACTION_DEBUG_TOGGLE,        "Debug Overlay",      BG_DEBUG_OVERLAY, "Toggle the F12 debug overlay.",                                                                       &g_ImcDebugOverlay },
    { ACTION_USE,                 "Accept",             BG_DEBUG_OVERLAY, NULL,                                                                                                  &g_ImcDebugOverlay },
    { ACTION_CANCEL_USE,          "Close",              BG_DEBUG_OVERLAY, NULL,                                                                                                  &g_ImcDebugOverlay },
    { ACTION_CONSOLE_TOGGLE,      "Console Toggle",     BG_DEBUG_OVERLAY, NULL,                                                                                                  &g_ImcDebugOverlay },
    { ACTION_SCREENSHOT,          "Screenshot",         BG_DEBUG_OVERLAY, NULL,                                                                                                  &g_ImcDebugOverlay },

    /* --- Text Input IMC (chat / rebind / cheat field) --- */
    { ACTION_USE,                 "Confirm",            BG_TEXT_INPUT,    NULL,                                                                                                  &g_ImcTextInput },
    { ACTION_CANCEL_USE,          "Cancel",             BG_TEXT_INPUT,    NULL,                                                                                                  &g_ImcTextInput },
    { ACTION_CHEAT_ENTER,         "Submit Cheat",       BG_TEXT_INPUT,    NULL,                                                                                                  &g_ImcTextInput },

    /* --- System Hotkeys (Gameplay): screenshot etc fire from gameplay --- */
    { ACTION_SCREENSHOT,          "Screenshot",         BG_SYSTEM,        "Save a .png of the current frame.",                                                                   &g_ImcGameplay },
    { ACTION_CONSOLE_TOGGLE,      "Console Toggle",     BG_SYSTEM,        NULL,                                                                                                  &g_ImcGameplay },
    { ACTION_DEBUG_TOGGLE,        "Debug Overlay",      BG_SYSTEM,        "F12 by default.",                                                                                     &g_ImcGameplay },
    { ACTION_CHEAT_ENTER,         "Enter Cheat",        BG_SYSTEM,        "Open the cheat-code entry dialog.",                                                                   &g_ImcGameplay },
};
#define NUM_BINDABLE_ACTIONS (sizeof(s_BindableActions) / sizeof(s_BindableActions[0]))

/* Per-tab descriptor. Each tab claims a set of groups (BG_*) to render and
 * a set of IMCs to reset when the user clicks the tab's reset button. The
 * order here drives both the tab bar and the LB/RB cycle order. */
struct ImcTabDesc {
    const char           *label;            /* tab title */
    const char           *blurb;            /* one-line description shown under tab on hover */
    u32                   shownGroups;      /* bitmask of (1u << BG_*) */
    InputMappingContext **resetImcs;        /* IMCs to reset (one per call to actionmapSetDefaults) */
    s32                   numResetImcs;
};

static InputMappingContext *s_ResetImcsMission[]   = { &g_ImcGameplay, &g_ImcMission };
static InputMappingContext *s_ResetImcsCombatSim[] = { &g_ImcGameplay, &g_ImcCombatSim };
static InputMappingContext *s_ResetImcsVehicle[]   = { &g_ImcVehicle };
static InputMappingContext *s_ResetImcsGrid[]      = { &g_ImcForgeSession, &g_ImcForge };
static InputMappingContext *s_ResetImcsMenu[]      = { &g_ImcMenu, &g_ImcPauseMenu };
static InputMappingContext *s_ResetImcsSystem[]    = { &g_ImcDebugOverlay, &g_ImcTextInput, &g_ImcGameplay };

static const ImcTabDesc s_ImcTabs[] = {
    {
        "Mission",
        "Solo, co-op, anti-counter-op gameplay. Shared baseline bindings.",
        (1u << BG_MOVEMENT) | (1u << BG_AIM) | (1u << BG_COMBAT) | (1u << BG_WEAPONS)
            | (1u << BG_HUD) | (1u << BG_CBUTTONS) | (1u << BG_DPAD),
        s_ResetImcsMission, 2,
    },
    {
        "Combat Simulator",
        "MP / CS gameplay. Shared baseline plus hold-Back scoreboard.",
        (1u << BG_MOVEMENT) | (1u << BG_AIM) | (1u << BG_COMBAT) | (1u << BG_WEAPONS)
            | (1u << BG_HUD) | (1u << BG_HUD_CS) | (1u << BG_CBUTTONS) | (1u << BG_DPAD),
        s_ResetImcsCombatSim, 2,
    },
    {
        "Vehicle",
        "Hoverbike controls. Active only while mounted.",
        (1u << BG_VEHICLE),
        s_ResetImcsVehicle, 1,
    },
    {
        "The Grid",
        "Forge level editor. Session toggle plus Freefly-only camera and editor bindings.",
        (1u << BG_FORGE_SESSION) | (1u << BG_FORGE_EDITOR),
        s_ResetImcsGrid, 2,
    },
    {
        "Menu",
        "Top-level menu nav and pause-menu nav. Distinct from gameplay bindings.",
        (1u << BG_MENU) | (1u << BG_PAUSEMENU),
        s_ResetImcsMenu, 2,
    },
    {
        "System",
        "F12 debug overlay, text-input fields, and gameplay-scope system hotkeys.",
        (1u << BG_DEBUG_OVERLAY) | (1u << BG_TEXT_INPUT) | (1u << BG_SYSTEM),
        s_ResetImcsSystem, 3,
    },
};
#define NUM_IMC_TABS (sizeof(s_ImcTabs) / sizeof(s_ImcTabs[0]))

/* Active outer tab (persists across opens for the lifetime of the session). */
static s32 s_ActiveImcTab = 0;

/* Format a row's display name with optional hold-threshold suffix. The
 * SCORECARD_HOLD row reads its threshold live from
 * actionmapGetEffectiveHoldMs(); ACTION_USE shows the effective hold for
 * the shared interact-vs-reload split. The buffer is shared per call site
 * so callers must consume it before the next call. */
static const char *formatRowLabel(const BindableAction *row, char *buf, size_t buflen)
{
    if (!row || !buf || buflen == 0) {
        return row ? row->name : "???";
    }
    s32 effHold = -1;
    if (row->action == ACTION_SCORECARD_HOLD) {
        effHold = actionmapGetEffectiveHoldMs(ACTION_SCORECARD_HOLD);
        if (effHold <= 0) effHold = 400;
    } else if (row->action == ACTION_USE && row->group == BG_COMBAT) {
        effHold = actionmapGetEffectiveHoldMs(ACTION_USE);
        if (effHold <= 0) effHold = ACTION_USE_HOLD_THRESHOLD_MS;
    }
    if (effHold > 0) {
        snprintf(buf, buflen, "%s -- %d ms", row->name, (int)effHold);
        return buf;
    }
    return row->name;
}

static bool isVkMKB(u32 vk)     { return (vk > 0 && vk < PD_VK_JOY_BEGIN); }
static bool isVkController(u32 vk) { return (vk >= PD_VK_JOY_BEGIN && vk < PD_VK_TOTAL_COUNT); }
static const char *getBindName(u32 vk) { return vk ? inputGetKeyName((s32)vk) : "---"; }

/* Get triggers for an action from the supplied IMC, split by MKB vs controller. */
static void getBindsByType(InputMappingContext *imc, InputAction action,
                           u32 mkbVKs[2], s32 mkbSlots[2], s32 *mkbCount,
                           u32 ctrlVKs[2], s32 ctrlSlots[2], s32 *ctrlCount)
{
    *mkbCount = 0; *ctrlCount = 0;
    mkbVKs[0] = mkbVKs[1] = 0; ctrlVKs[0] = ctrlVKs[1] = 0;
    mkbSlots[0] = mkbSlots[1] = -1; ctrlSlots[0] = ctrlSlots[1] = -1;
    if (!imc) return;
    InputMapping *m = &imc->mappings[action];
    for (s32 i = 0; i < m->num_triggers; i++) {
        u32 vk = m->triggers[i].vk;
        if (vk == 0) continue;
        if (isVkMKB(vk) && *mkbCount < 2) {
            mkbSlots[*mkbCount] = i; mkbVKs[(*mkbCount)++] = vk;
        } else if (isVkController(vk) && *ctrlCount < 2) {
            ctrlSlots[*ctrlCount] = i; ctrlVKs[(*ctrlCount)++] = vk;
        }
    }
}

static s32 findFreeTriggerSlot(InputMappingContext *imc, InputAction action)
{
    if (!imc) return 0;
    InputMapping *m = &imc->mappings[action];
    for (s32 i = 0; i < ACTIONMAP_MAX_TRIGGERS; i++) {
        if (m->triggers[i].vk == 0) return i;
    }
    return 0;
}

/* Trigger slot index for controller Bind 1 / Bind 2 columns (matches renderBindTable). */
static s32 pickSlotForControllerBindColumn(InputMappingContext *imc, InputAction action, s32 wantCol)
{
    if (!imc) return 0;
    u32 mkbVKs[2], ctrlVKs[2];
    s32 mkbSlots[2], ctrlSlots[2];
    s32 mkbCount, ctrlCount;
    getBindsByType(imc, action, mkbVKs, mkbSlots, &mkbCount, ctrlVKs, ctrlSlots, &ctrlCount);
    if (wantCol == 0) {
        if (ctrlSlots[0] >= 0) {
            return ctrlSlots[0];
        }
        return findFreeTriggerSlot(imc, action);
    }
    if (ctrlSlots[1] >= 0) {
        return ctrlSlots[1];
    }
    s32 other = (ctrlSlots[0] >= 0) ? ctrlSlots[0] : -1;
    s32 useSlot = findFreeTriggerSlot(imc, action);
    if (other >= 0 && useSlot == other) {
        InputMapping *m = &imc->mappings[action];
        useSlot = -1;
        for (s32 i = 0; i < ACTIONMAP_MAX_TRIGGERS; i++) {
            if (i != other && m->triggers[i].vk == 0) {
                useSlot = i;
                break;
            }
        }
        if (useSlot < 0) {
            useSlot = (other == 0) ? 1 : 0;
        }
    }
    return useSlot;
}

/* Clear controller bind at vk for Bind 1 (0) or Bind 2 (1) column. Walks every
 * row whose group is in the active tab (groupMask), so the visual mapper only
 * affects bindings the user can actually see. */
static void clearControllerVkAtBindColumn(u32 vk, s32 bindCol, u32 groupMask)
{
    for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
        if (!(groupMask & (1u << s_BindableActions[row].group))) {
            continue;
        }
        InputMappingContext *imc = s_BindableActions[row].owningImc;
        if (!imc) continue;
        InputAction act = s_BindableActions[row].action;
        u32 mkbVKs[2], ctrlVKs[2];
        s32 mkbSlots[2], ctrlSlots[2];
        s32 mkbCount, ctrlCount;
        getBindsByType(imc, act, mkbVKs, mkbSlots, &mkbCount, ctrlVKs, ctrlSlots, &ctrlCount);
        if (bindCol == 0 && ctrlVKs[0] == vk && ctrlSlots[0] >= 0) {
            actionmapBind(imc, 0, act, ctrlSlots[0], 0);
        } else if (bindCol == 1 && ctrlVKs[1] == vk && ctrlSlots[1] >= 0) {
            actionmapBind(imc, 0, act, ctrlSlots[1], 0);
        }
    }
    actionmapSaveBinds();
    configSave("pd.ini");
}

/* Conflict map — computed once per frame, per filter column. Keys are the
 * u32 VK values. When a VK shows up in more than one bindable action's
 * triggers we raise a flag so the row's button can render with a red
 * border. Uses a tiny open-addressing hash over 128 slots — plenty for
 * 51×2 possible bindings. */
#define CONFLICT_HASH_SIZE 128

static u32  s_ConflictKey[CONFLICT_HASH_SIZE];   /* vk value (0 = empty) */
static u8   s_ConflictCount[CONFLICT_HASH_SIZE]; /* # of actions binding this vk */

static void conflictMapReset(void)
{
    for (int i = 0; i < CONFLICT_HASH_SIZE; i++) {
        s_ConflictKey[i]   = 0;
        s_ConflictCount[i] = 0;
    }
}
static void conflictMapAdd(u32 vk)
{
    if (!vk) return;
    u32 h = (vk * 2654435761u) & (CONFLICT_HASH_SIZE - 1);
    for (int probe = 0; probe < CONFLICT_HASH_SIZE; probe++) {
        u32 slot = (h + probe) & (CONFLICT_HASH_SIZE - 1);
        if (s_ConflictKey[slot] == 0) {
            s_ConflictKey[slot]   = vk;
            s_ConflictCount[slot] = 1;
            return;
        }
        if (s_ConflictKey[slot] == vk) {
            if (s_ConflictCount[slot] < 255) s_ConflictCount[slot]++;
            return;
        }
    }
}
static u32 conflictMapCount(u32 vk)
{
    if (!vk) return 0;
    u32 h = (vk * 2654435761u) & (CONFLICT_HASH_SIZE - 1);
    for (int probe = 0; probe < CONFLICT_HASH_SIZE; probe++) {
        u32 slot = (h + probe) & (CONFLICT_HASH_SIZE - 1);
        if (s_ConflictKey[slot] == 0) return 0;
        if (s_ConflictKey[slot] == vk) return s_ConflictCount[slot];
    }
    return 0;
}
/* Rebuild the conflict map for a single (groupMask, deviceCol) pair. groupMask
 * scopes detection to the active tab's groups so a key bound to Move Forward
 * in Mission and Steer Right in Vehicle does not falsely flag as conflicting. */
static void conflictMapRebuild(s32 filterCol, u32 groupMask)
{
    conflictMapReset();
    for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
        if (!(groupMask & (1u << s_BindableActions[row].group))) {
            continue;
        }
        InputMappingContext *imc = s_BindableActions[row].owningImc;
        if (!imc) continue;
        InputAction action = s_BindableActions[row].action;
        InputMapping *m = &imc->mappings[action];
        for (s32 i = 0; i < m->num_triggers; i++) {
            u32 vk = m->triggers[i].vk;
            if (!vk) continue;
            bool isMkb  = isVkMKB(vk);
            bool isCtrl = isVkController(vk);
            if (filterCol == 0 && !isMkb)  continue;
            if (filterCol == 1 && !isCtrl) continue;
            conflictMapAdd(vk);
        }
    }
}

/* Render one bind cell. The owningImc determines where capture writes back and
 * where right-click clears. The action enum identifies the row inside that IMC.
 * idSuffix uniquifies the ImGui ID across columns and groups. */
static void renderBindButton(InputMappingContext *imc, InputAction action, const char *rowName,
                              const char *idSuffix, s32 captureCol,
                              s32 isSecond, u32 vk, s32 slot, s32 otherSlot,
                              bool *rowHov, bool *rowNav, bool conflict)
{
    if (!imc) return;
    char btnLabel[64];
    bool isCap = (s_CaptureActive && s_CaptureImc == imc
                  && s_CaptureAction == action
                  && s_CaptureColumn == captureCol
                  && s_CaptureIsSecond == isSecond);
    if (isCap) {
        snprintf(btnLabel, sizeof(btnLabel), "...##%s_%p_%d", idSuffix, (void *)imc, (int)action);
    } else {
        snprintf(btnLabel, sizeof(btnLabel), "%s##%s_%p_%d", getBindName(vk), idSuffix, (void *)imc, (int)action);
    }

    int pushedBorder = 0;
    if (conflict) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        pushedBorder = 1;
    } else if (isCap) {
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.15f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        pushedBorder = 1;
    }

    if (ImGui::SmallButton(btnLabel) && !s_CaptureActive) {
        s32 useSlot = slot;
        if (useSlot < 0) {
            useSlot = findFreeTriggerSlot(imc, action);
            if (useSlot == otherSlot) {
                InputMapping *m = &imc->mappings[action];
                useSlot = -1;
                for (s32 i = 0; i < ACTIONMAP_MAX_TRIGGERS; i++) {
                    if (i != otherSlot && m->triggers[i].vk == 0) { useSlot = i; break; }
                }
                if (useSlot < 0) useSlot = (otherSlot == 0) ? 1 : 0;
            }
        }
        s_CaptureActive   = 1;
        s_CaptureAction   = action;
        s_CaptureColumn   = captureCol;
        s_CaptureIsSecond = isSecond;
        s_CaptureBind     = useSlot;
        s_CaptureImc      = imc;
        s_CaptureName     = rowName ? rowName : "(unnamed)";
        inputClearLastKey();
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }

    if (pushedBorder) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    if (conflict && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Conflict: '%s' is bound to more than one action in this tab.",
                          getBindName(vk));
    }
    if (ImGui::IsItemHovered()) *rowHov = true;
    if (ImGui::IsItemFocused()) *rowNav = true;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && vk != 0 && slot >= 0) {
        actionmapBind(imc, 0, action, slot, 0);
        actionmapSaveBinds();
        configSave("pd.ini");
        pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
    }
}

/* Find the first row whose (action, owningImc) pair matches. Used by the
 * capture banner so we display the right label for shared actions. */
static const BindableAction *findRowByActionAndImc(InputAction action, InputMappingContext *imc)
{
    for (u32 i = 0; i < NUM_BINDABLE_ACTIONS; i++) {
        if (s_BindableActions[i].action == action && s_BindableActions[i].owningImc == imc) {
            return &s_BindableActions[i];
        }
    }
    return NULL;
}

/* Sub-tab index within Controls: 0 = Keyboard & Mouse, 1 = Controller */
static s32 s_ControlsSubTab = 0;

/* Track whether we need to reload binds on Controls tab entry */
static bool s_ControlsNeedsInit = true;

/* S306: optional search filter. Empty means "show all". Case-insensitive
 * substring match on the display name. */
static char s_BindSearch[64] = "";
/* Controls -> Controller -> per-action hold overrides table */
static char s_HoldOvFilter[64] = "";

static bool stringIContains(const char *hay, const char *needle)
{
    if (!needle || !needle[0]) return true;
    if (!hay) return false;
    size_t nlen = strlen(needle);
    for (size_t i = 0; hay[i]; i++) {
        size_t k = 0;
        while (k < nlen && hay[i + k]) {
            char a = hay[i + k]; if (a >= 'A' && a <= 'Z') a += 32;
            char b = needle[k];  if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            k++;
        }
        if (k == nlen) return true;
    }
    return false;
}

/* Shared capture-mode handler. Writes into s_CaptureImc (the IMC that was
 * active when the user clicked the bind cell). If capture started but
 * s_CaptureImc somehow ended up NULL (e.g. tab tear-down race), the handler
 * cancels rather than blindly writing into Gameplay. */
static void handleCaptureInput(void)
{
    if (!s_CaptureActive) return;

    s32 newKey = inputGetLastKey();
    if (newKey == PD_VK_ESCAPE) {
        s_CaptureActive = 0;
        s_CaptureImc    = NULL;
        s_CaptureName   = "";
        inputClearLastKey();
    } else if (newKey > 0) {
        bool valid = (s_CaptureColumn == 0 && isVkMKB((u32)newKey))
                  || (s_CaptureColumn == 1 && isVkController((u32)newKey));
        if (valid && s_CaptureImc != NULL) {
            actionmapBind(s_CaptureImc, 0, s_CaptureAction, s_CaptureBind, (u32)newKey);
            actionmapSaveBinds();
            configSave("pd.ini");
            pdguiPlaySound(PDGUI_SND_SELECT);
        } else {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        s_CaptureActive = 0;
        s_CaptureImc    = NULL;
        s_CaptureName   = "";
        inputClearLastKey();
    }
}

/* Controller diagram: SDL gamepad button indices match port/src/actionmap.cpp (JBTN_*). */
#define PD_JOY0_BTN(off) ((u32)PD_VK_JOY_BEGIN + (u32)(off))

struct CtrlPadZone {
    const char *id;
    const char *shortLabel;
    u32 vk;
    float relX, relY, relW, relH;
};

/* Entries for one physical controller button on the visual map (Bind 1 vs Bind 2
 * matches the table columns; order is B1 then B2, then name). */
struct CtrlZoneEntry {
    InputAction          action;
    InputMappingContext *imc;
    const char          *name;
    u8                   bindIdx; /* 0 = Bind 1, 1 = Bind 2 */
};

/* Drag-drop payload: action plus owning IMC. The drop target writes the
 * dropped VK into the named IMC, not unconditionally into Gameplay. */
struct DragActionPayload {
    InputAction          action;
    InputMappingContext *imc;
};

#define CTRL_ZONE_MAX_ENTRIES 10

static int collectCtrlZoneEntries(u32 vk, u32 groupMask, CtrlZoneEntry *out, int maxOut)
{
    int n = 0;
    for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
        if (!(groupMask & (1u << s_BindableActions[row].group))) {
            continue;
        }
        InputMappingContext *imc = s_BindableActions[row].owningImc;
        if (!imc) continue;
        InputAction act = s_BindableActions[row].action;
        u32 mkbVKs[2], ctrlVKs[2];
        s32 mkbSlots[2], ctrlSlots[2];
        s32 mkbCount, ctrlCount;
        getBindsByType(imc, act, mkbVKs, mkbSlots, &mkbCount, ctrlVKs, ctrlSlots, &ctrlCount);
        for (int bi = 0; bi < 2; bi++) {
            if (ctrlVKs[bi] == vk && n < maxOut) {
                out[n].action  = act;
                out[n].imc     = imc;
                out[n].name    = s_BindableActions[row].name;
                out[n].bindIdx = (u8)bi;
                n++;
            }
        }
    }
    /* Sort: Bind 1 before Bind 2, then label A-Z */
    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            int db = (int)out[i].bindIdx - (int)out[j].bindIdx;
            if (db > 0 || (db == 0 && strcmp(out[i].name, out[j].name) > 0)) {
                CtrlZoneEntry t = out[i];
                out[i] = out[j];
                out[j] = t;
            }
        }
    }
    return n;
}

/* Vector-style Xbox-like silhouette: drawn under drop zones; zones stay interactive on top. */
static void drawControllerSilhouette(ImDrawList *dl, ImVec2 p0, float padW, float padH)
{
    const float px = padW;
    const float py = padH;
    const ImU32 bodyFill = IM_COL32(32, 34, 46, 255);
    const ImU32 bodyEdge = IM_COL32(55, 60, 78, 255);
    const ImU32 deco = IM_COL32(72, 80, 105, 110);
    const ImU32 shoulderFill = IM_COL32(26, 28, 38, 255);
    const float bodyRound = pdguiScale(14.0f);

    ImVec2 bmin(p0.x + 0.016f * px, p0.y + 0.058f * py);
    ImVec2 bmax(p0.x + 0.984f * px, p0.y + 0.978f * py);
    dl->AddRectFilled(bmin, bmax, bodyFill, bodyRound);
    dl->AddRect(bmin, bmax, bodyEdge, bodyRound, 0, 1.25f);

    /* LT / RT caps */
    float shTop = p0.y + 0.038f * py;
    float shBot = p0.y + 0.098f * py;
    dl->AddRectFilled(
        ImVec2(p0.x + 0.065f * px, shTop),
        ImVec2(p0.x + 0.205f * px, shBot),
        shoulderFill, pdguiScale(5.0f));
    dl->AddRectFilled(
        ImVec2(p0.x + 0.795f * px, shTop),
        ImVec2(p0.x + 0.935f * px, shBot),
        shoulderFill, pdguiScale(5.0f));
    dl->AddRect(
        ImVec2(p0.x + 0.065f * px, shTop),
        ImVec2(p0.x + 0.205f * px, shBot),
        IM_COL32(48, 54, 72, 255), pdguiScale(5.0f), 0, 1.0f);
    dl->AddRect(
        ImVec2(p0.x + 0.795f * px, shTop),
        ImVec2(p0.x + 0.935f * px, shBot),
        IM_COL32(48, 54, 72, 255), pdguiScale(5.0f), 0, 1.0f);

    /* D-pad cross (decorative) */
    float dcx = p0.x + 0.19f * px;
    float dcy = p0.y + 0.52f * py;
    float arm = pdguiScale(16.0f);
    float thick = pdguiScale(11.0f);
    dl->AddRectFilled(
        ImVec2(dcx - arm, dcy - thick * 0.5f),
        ImVec2(dcx + arm, dcy + thick * 0.5f),
        deco, pdguiScale(2.5f));
    dl->AddRectFilled(
        ImVec2(dcx - thick * 0.5f, dcy - arm),
        ImVec2(dcx + thick * 0.5f, dcy + arm),
        deco, pdguiScale(2.5f));

    /* Analog sticks (hollow rings, aligned with L3/R3 zones) */
    float srad = pdguiScale(27.0f);
    ImVec2 lStick(p0.x + 0.245f * px, p0.y + 0.8945f * py);
    ImVec2 rStick(p0.x + 0.755f * px, p0.y + 0.8945f * py);
    dl->AddCircle(lStick, srad, deco, 40, 1.35f);
    dl->AddCircle(rStick, srad, deco, 40, 1.35f);

    /* Face button hints (small circles in diamond layout) */
    float br = pdguiScale(7.5f);
    dl->AddCircle(ImVec2(p0.x + 0.8125f * px, p0.y + 0.3785f * py), br, deco, 20, 1.0f);
    dl->AddCircle(ImVec2(p0.x + 0.7125f * px, p0.y + 0.5575f * py), br, deco, 20, 1.0f);
    dl->AddCircle(ImVec2(p0.x + 0.9125f * px, p0.y + 0.5575f * py), br, deco, 20, 1.0f);
    dl->AddCircle(ImVec2(p0.x + 0.8125f * px, p0.y + 0.7365f * py), br, deco, 20, 1.0f);

    /* Center guide line (Start / Back) */
    dl->AddLine(
        ImVec2(p0.x + 0.36f * px, p0.y + 0.30f * py),
        ImVec2(p0.x + 0.64f * px, p0.y + 0.30f * py),
        IM_COL32(55, 62, 82, 80), 1.0f);
}

/* One physical control: left half = Bind 1 column, right half = Bind 2 (matches bind table).
 * groupMask is the active tab's groups so right-click clear and drag-drop only
 * affect bindings the user can see. Drop payload is DragActionPayload (action +
 * owningImc), so a drop on Mission's "Move Forward" lands on g_ImcGameplay,
 * while a drop on Combat Simulator's "Scorecard hold" lands on g_ImcCombatSim. */
static void renderOneCtrlPadZoneSplit(ImDrawList *dl, const ImVec2 &padOrigin, const CtrlPadZone *z,
                                       float padW, float padH, u32 groupMask)
{
    ImGui::SetCursorScreenPos(ImVec2(padOrigin.x + z->relX * padW, padOrigin.y + z->relY * padH));
    ImVec2 zsz(z->relW * padW, z->relH * padH);
    float halfW = zsz.x * 0.5f;

    char id0[56], id1[56];
    snprintf(id0, sizeof(id0), "%s_b0##cz", z->id);
    snprintf(id1, sizeof(id1), "%s_b1##cz", z->id);

    ImGui::PushID(z->id);
    ImGui::InvisibleButton(id0, ImVec2(halfW, zsz.y));
    bool hov0 = ImGui::IsItemHovered();
    bool nav0 = ImGui::IsItemFocused();
    ImVec2 rmin0 = ImGui::GetItemRectMin();
    ImVec2 rmax0 = ImGui::GetItemRectMax();
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("PD_INPUT_ACTION");
        if (pl && pl->DataSize == (int)sizeof(DragActionPayload) && pl->Data != NULL) {
            const DragActionPayload *p = (const DragActionPayload *)pl->Data;
            if (p->imc) {
                s32 sl = pickSlotForControllerBindColumn(p->imc, p->action, 0);
                if (sl >= 0) {
                    actionmapBind(p->imc, 0, p->action, sl, z->vk);
                    actionmapSaveBinds();
                    configSave("pd.ini");
                    pdguiPlaySound(PDGUI_SND_SELECT);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        clearControllerVkAtBindColumn(z->vk, 0, groupMask);
        pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
    }

    ImGui::SameLine(0.0f, 0.0f);
    ImGui::InvisibleButton(id1, ImVec2(halfW, zsz.y));
    bool hov1 = ImGui::IsItemHovered();
    bool nav1 = ImGui::IsItemFocused();
    ImVec2 rmin1 = ImGui::GetItemRectMin();
    ImVec2 rmax1 = ImGui::GetItemRectMax();
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("PD_INPUT_ACTION");
        if (pl && pl->DataSize == (int)sizeof(DragActionPayload) && pl->Data != NULL) {
            const DragActionPayload *p = (const DragActionPayload *)pl->Data;
            if (p->imc) {
                s32 sl = pickSlotForControllerBindColumn(p->imc, p->action, 1);
                if (sl >= 0) {
                    actionmapBind(p->imc, 0, p->action, sl, z->vk);
                    actionmapSaveBinds();
                    configSave("pd.ini");
                    pdguiPlaySound(PDGUI_SND_SELECT);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        clearControllerVkAtBindColumn(z->vk, 1, groupMask);
        pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
    }
    ImGui::PopID();

    ImVec2 rmin(rmin0.x, rmin0.y);
    ImVec2 rmax(rmax1.x, rmax1.y);

    ImU32 fill0 = hov0 ? IM_COL32(65, 85, 115, 230) : IM_COL32(48, 52, 68, 220);
    ImU32 fill1 = hov1 ? IM_COL32(65, 85, 115, 230) : IM_COL32(48, 52, 68, 220);
    if (nav0) {
        fill0 = IM_COL32(75, 95, 130, 240);
    }
    if (nav1) {
        fill1 = IM_COL32(75, 95, 130, 240);
    }
    float zr = pdguiScale(3.0f);
    dl->AddRectFilled(rmin0, rmax0, fill0, zr, ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(rmin0.x + halfW, rmin0.y), rmax1, fill1, zr, ImDrawFlags_RoundCornersRight);
    dl->AddRect(rmin, rmax, IM_COL32(130, 140, 175, 255), zr, 0, 1.25f);
    dl->AddLine(ImVec2(rmin0.x + halfW, rmin.y), ImVec2(rmin0.x + halfW, rmax.y),
                IM_COL32(90, 95, 120, 200), 1.0f);

    dl->AddText(ImVec2(rmin.x + pdguiScale(4.0f), rmin.y + pdguiScale(2.0f)), IM_COL32(170, 190, 220, 255),
                "1");
    dl->AddText(ImVec2(rmin.x + halfW + pdguiScale(4.0f), rmin.y + pdguiScale(2.0f)),
                IM_COL32(170, 190, 220, 255), "2");
    dl->AddText(ImVec2(rmin.x + pdguiScale(22.0f), rmin.y + pdguiScale(2.0f)), IM_COL32(240, 242, 250, 255),
                z->shortLabel);

    CtrlZoneEntry zent[CTRL_ZONE_MAX_ENTRIES];
    int zn = collectCtrlZoneEntries(z->vk, groupMask, zent, CTRL_ZONE_MAX_ENTRIES);
    const ImU32 colB1 = IM_COL32(215, 235, 255, 255);
    const ImU32 colB2 = IM_COL32(165, 200, 240, 245);
    const ImU32 colEmpty = IM_COL32(130, 135, 155, 220);

    float lineY = rmin.y + ImGui::GetTextLineHeight() + pdguiScale(3.0f);
    const float bindFont = ImGui::GetFontSize() * 0.72f;
    ImGui::PushClipRect(rmin, rmax, true);
    if (zn == 0) {
        dl->AddText(ImGui::GetFont(), bindFont, ImVec2(rmin.x + pdguiScale(3.0f), lineY), colEmpty, "(empty)");
    } else {
        for (int zi2 = 0; zi2 < zn; zi2++) {
            char line[96];
            snprintf(line, sizeof(line), "B%u %s", (unsigned)(zent[zi2].bindIdx + 1), zent[zi2].name);
            ImU32 col = (zent[zi2].bindIdx == 0) ? colB1 : colB2;
            dl->AddText(ImGui::GetFont(), bindFont, ImVec2(rmin.x + pdguiScale(3.0f), lineY), col, line);
            lineY += bindFont * 1.05f;
            if (lineY > rmax.y - bindFont * 0.5f) {
                dl->AddText(ImGui::GetFont(), bindFont * 0.9f, ImVec2(rmin.x + pdguiScale(3.0f), lineY),
                            colEmpty, "+more");
                break;
            }
        }
    }
    ImGui::PopClipRect();

    bool hov = hov0 || hov1;
    if (hov && zn > 0) {
        char tip[560];
        size_t tl = 0;
        tip[0] = '\0';
        bool haveUse = false;
        bool haveReload = false;
        for (int zi2 = 0; zi2 < zn && tl < sizeof(tip) - 160; zi2++) {
            int n = snprintf(tip + tl, sizeof(tip) - tl, "Bind %u: %s\n",
                             (unsigned)(zent[zi2].bindIdx + 1), zent[zi2].name);
            if (n > 0) tl += (size_t)n;
            if (zent[zi2].action == ACTION_USE) haveUse = true;
            if (zent[zi2].action == ACTION_RELOAD) haveReload = true;
        }
        strncat(tip, "Drop left: Bind 1. Drop right: Bind 2. Right-click a side to clear that column.\n",
                sizeof(tip) - tl - 1);
        tl = strlen(tip);
        if (haveUse && haveReload) {
            strncat(tip,
                    "\nSame button: hold Use / Interact uses the threshold above; short press / release "
                    "pairs with bondmove for reload when no prompt (see Combat bindings).",
                    sizeof(tip) - tl - 1);
        }
        ImGui::SetTooltip("%s", tip);
    }
}

/* Drag sources + drop targets on a stylized gamepad. groupMask is the active
 * tab's set of groups (bit set = surface that group). Drag payload is a
 * DragActionPayload (action + owning IMC), so a drop on the visual mapper
 * lands in the correct IMC for the dragged row.
 *
 * Zones are ordered so stick click (L3/R3) wins hit-testing over stick
 * cardinals on overlap. */
static void renderControllerVisualMapper(float scale, u32 groupMask)
{
    (void)scale;
    const float padW = pdguiScale(420.0f);
    const float padH = pdguiScale(240.0f);
    const float rowH = padH + pdguiScale(10.0f);

    ImGui::TextWrapped(
        "Drag an action from the list onto the left or right half of a zone: left sets Bind 1, "
        "right sets Bind 2 (same columns as the table below). LS / RS arrows are synthetic stick "
        "directions. Right-click a half to clear that bind column. Drops land on the IMC the row "
        "belongs to, so dragging a Vehicle action only affects the Vehicle scheme.");
    ImGui::Spacing();

    static const CtrlPadZone kZones[] = {
        /* Stick cardinals -- JOFS_* in port/src/actionmap.cpp (must match VK_JOY1_BEGIN + offset). */
        { "lsu", "LS-Up", PD_JOY0_BTN(24), 0.212f, 0.752f, 0.066f, 0.058f },
        { "lsd", "LS-Dn", PD_JOY0_BTN(25), 0.212f, 0.925f, 0.066f, 0.055f },
        { "lsl", "LS-Lt", PD_JOY0_BTN(22), 0.098f, 0.862f, 0.058f, 0.068f },
        { "lsr", "LS-Rt", PD_JOY0_BTN(23), 0.328f, 0.862f, 0.058f, 0.068f },
        { "rsu", "RS-Up", PD_JOY0_BTN(28), 0.722f, 0.752f, 0.066f, 0.058f },
        { "rsd", "RS-Dn", PD_JOY0_BTN(29), 0.722f, 0.925f, 0.066f, 0.055f },
        { "rsl", "RS-Lt", PD_JOY0_BTN(26), 0.608f, 0.862f, 0.058f, 0.068f },
        { "rsr", "RS-Rt", PD_JOY0_BTN(27), 0.838f, 0.862f, 0.058f, 0.068f },
        { "lt", "LT", PD_JOY0_BTN(30), 0.07f, 0.032f, 0.13f, 0.074f },
        { "rt", "RT", PD_JOY0_BTN(31), 0.80f, 0.032f, 0.13f, 0.074f },
        { "lb", "LB", PD_JOY0_BTN(9), 0.12f, 0.137f, 0.14f, 0.116f },
        { "rb", "RB", PD_JOY0_BTN(10), 0.74f, 0.137f, 0.14f, 0.116f },
        { "bk", "Back", PD_JOY0_BTN(4), 0.295f, 0.253f, 0.13f, 0.105f },
        { "st", "Start", PD_JOY0_BTN(6), 0.575f, 0.253f, 0.13f, 0.105f },
        { "dup", "D-Up", PD_JOY0_BTN(11), 0.155f, 0.379f, 0.07f, 0.116f },
        { "ddn", "D-Down", PD_JOY0_BTN(12), 0.155f, 0.653f, 0.07f, 0.116f },
        { "dlt", "D-Left", PD_JOY0_BTN(13), 0.085f, 0.516f, 0.07f, 0.116f },
        { "drt", "D-Right", PD_JOY0_BTN(14), 0.225f, 0.516f, 0.07f, 0.116f },
        { "y", "Y", PD_JOY0_BTN(3), 0.77f, 0.305f, 0.085f, 0.147f },
        { "x", "X", PD_JOY0_BTN(2), 0.67f, 0.484f, 0.085f, 0.147f },
        { "b", "B", PD_JOY0_BTN(1), 0.87f, 0.484f, 0.085f, 0.147f },
        { "a", "A", PD_JOY0_BTN(0), 0.77f, 0.663f, 0.085f, 0.147f },
        { "l3", "L3", PD_JOY0_BTN(7), 0.18f, 0.789f, 0.13f, 0.211f },
        { "r3", "R3", PD_JOY0_BTN(8), 0.69f, 0.789f, 0.13f, 0.211f },
    };

    ImGui::BeginChild("##mapper_row", ImVec2(0.0f, rowH), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar);

    ImGui::BeginChild("##mapper_left", ImVec2(pdguiScale(232.0f), rowH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGui::TextDisabled("Actions (drag)");
    ImGui::Separator();
    ImGui::BeginChild("##mapper_draglist", ImVec2(0.0f, 0.0f), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
        if (!(groupMask & (1u << s_BindableActions[row].group))) {
            continue;
        }
        InputMappingContext *imc = s_BindableActions[row].owningImc;
        if (!imc) continue;
        if (!stringIContains(s_BindableActions[row].name, s_BindSearch)) {
            continue;
        }
        char rowLabelBuf[96];
        const char *rowLabel = formatRowLabel(&s_BindableActions[row], rowLabelBuf, sizeof(rowLabelBuf));
        ImGui::PushID((int)row);
        ImGui::Selectable(rowLabel, false, 0, ImVec2(-1.0f, 0.0f));
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            DragActionPayload payload;
            payload.action = s_BindableActions[row].action;
            payload.imc    = imc;
            ImGui::SetDragDropPayload("PD_INPUT_ACTION", &payload, sizeof(payload));
            ImGui::TextUnformatted(rowLabel);
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("##mapper_right", ImVec2(0.0f, rowH),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoScrollbar);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 win0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(win0, ImVec2(win0.x + padW, win0.y + padH), IM_COL32(24, 26, 34, 255),
                      pdguiScale(6.0f));
    dl->AddRect(win0, ImVec2(win0.x + padW, win0.y + padH), IM_COL32(90, 95, 115, 255),
                pdguiScale(6.0f), 0, 1.5f);
    drawControllerSilhouette(dl, win0, padW, padH);

    ImGui::SetCursorScreenPos(win0);

    for (int zi = 0; zi < (int)(sizeof(kZones) / sizeof(kZones[0])); zi++) {
        renderOneCtrlPadZoneSplit(dl, win0, &kZones[zi], padW, padH, groupMask);
    }
    ImGui::EndChild();

    ImGui::EndChild();
}

/* Render the grouped bind list for one device type (MKB or Controller) scoped
 * to the active tab. groupMask carries the bits-set-means-show convention.
 * filterCol: 0 = MKB, 1 = Controller. Each row reads from / writes to its own
 * owningImc; conflict detection is scoped to the active tab so cross-tab
 * coincidences (Move Forward in Mission and Steer Right in Vehicle on the
 * same VK) do not falsely flag. */
static void renderBindTable(s32 filterCol, const char *tableId, u32 groupMask)
{
    /* Capture banner. Uses s_CaptureName directly because the capture state
     * carries the row's display label, which is unambiguous even when the
     * action enum is shared across IMCs. */
    if (s_CaptureActive && s_CaptureColumn == filterCol) {
        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        const char *typeStr = (filterCol == 0) ? "keyboard/mouse" : "controller";
        const char *imcName = (s_CaptureImc && s_CaptureImc->name) ? s_CaptureImc->name : "?";
        ImGui::TextColored(warnCol,
            "Press a %s key for \"%s\" on the %s scheme (Esc to cancel)",
            typeStr, s_CaptureName ? s_CaptureName : "?", imcName);
        ImGui::Spacing();
    }

    /* Search / filter input. The suffix on the label varies per device so
     * MKB and Controller don't share ImGui state. */
    {
        char label[32];
        snprintf(label, sizeof(label), "Search##bsrch_%d", (int)filterCol);
        ImGui::SetNextItemWidth(pdguiScale(260.0f));
        ImGui::InputTextWithHint(label, "filter by action name", s_BindSearch, sizeof(s_BindSearch));
        if (s_BindSearch[0]) {
            ImGui::SameLine();
            char clearLbl[32];
            snprintf(clearLbl, sizeof(clearLbl), "Clear##bclr_%d", (int)filterCol);
            if (ImGui::SmallButton(clearLbl)) s_BindSearch[0] = '\0';
        }
    }

    /* Conflict detection. Scoped to (active tab's groupMask, current device
     * column) so cross-tab and cross-device coincidences do not flag. */
    conflictMapRebuild(filterCol, groupMask);

    ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV
                                | ImGuiTableFlags_RowBg
                                | ImGuiTableFlags_SizingStretchProp
                                | ImGuiTableFlags_PadOuterX;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,  ImVec2(pdguiScale(4.0f), pdguiScale(2.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(pdguiScale(4.0f), pdguiScale(2.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  ImVec2(pdguiScale(4.0f), pdguiScale(2.0f)));

    /* Per-group render. Each group is an explicit header row + its own table.
     * Groups absent from groupMask never emit. */
    for (int g = 0; g < BG_COUNT; g++) {
        if (!(groupMask & (1u << g))) {
            continue;
        }
        int hits = 0;
        for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
            if (s_BindableActions[row].group != g) continue;
            if (!s_BindableActions[row].owningImc) continue;
            if (!stringIContains(s_BindableActions[row].name, s_BindSearch)) continue;
            hits++;
        }
        if (hits == 0) continue;

        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        ImGui::TextColored(warnCol, "%s", s_BindableGroups[g].name);
        ImGui::SameLine();
        ImGui::TextDisabled("  %s", s_BindableGroups[g].blurb);
        ImGui::Separator();

        char perTableId[64];
        snprintf(perTableId, sizeof(perTableId), "%s_g%d", tableId, g);

        if (ImGui::BeginTable(perTableId, 3, tableFlags)) {
            ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("Bind 1",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Bind 2",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableHeadersRow();

            for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
                if (s_BindableActions[row].group != g) continue;
                InputMappingContext *imc = s_BindableActions[row].owningImc;
                if (!imc) continue;
                if (!stringIContains(s_BindableActions[row].name, s_BindSearch)) continue;

                InputAction action        = s_BindableActions[row].action;
                const char *actionTooltip = s_BindableActions[row].tooltip;

                char rowLabelBuf[96];
                const char *rowLabel = formatRowLabel(&s_BindableActions[row], rowLabelBuf, sizeof(rowLabelBuf));

                u32 mkbVKs[2], ctrlVKs[2];
                s32 mkbSlots[2], ctrlSlots[2];
                s32 mkbCount, ctrlCount;
                getBindsByType(imc, action, mkbVKs, mkbSlots, &mkbCount,
                               ctrlVKs, ctrlSlots, &ctrlCount);

                u32 *vks    = (filterCol == 0) ? mkbVKs    : ctrlVKs;
                s32 *slots  = (filterCol == 0) ? mkbSlots  : ctrlSlots;
                const char *prefix = (filterCol == 0) ? "mkb" : "ctrl";

                ImGui::TableNextRow();
                bool rowHovered = false;
                bool rowNavFocus = false;

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(rowLabel);
                if (actionTooltip && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", actionTooltip);
                }

                char id1[40], id2[40];
                snprintf(id1, sizeof(id1), "%s1g%dr%u", prefix, g, row);
                snprintf(id2, sizeof(id2), "%s2g%dr%u", prefix, g, row);

                ImGui::TableSetColumnIndex(1);
                renderBindButton(imc, action, s_BindableActions[row].name, id1, filterCol, 0, vks[0],
                                 (slots[0] >= 0) ? slots[0] : findFreeTriggerSlot(imc, action),
                                 slots[1], &rowHovered, &rowNavFocus,
                                 conflictMapCount(vks[0]) > 1);

                ImGui::TableSetColumnIndex(2);
                renderBindButton(imc, action, s_BindableActions[row].name, id2, filterCol, 1, vks[1], slots[1], slots[0],
                                 &rowHovered, &rowNavFocus,
                                 conflictMapCount(vks[1]) > 1);

                if (s_CaptureActive && s_CaptureImc == imc && s_CaptureAction == action
                    && s_CaptureColumn == filterCol) {
                    rowNavFocus = true;
                }
                if (rowHovered || rowNavFocus) {
                    ImU32 hlColor = rowNavFocus
                        ? pdguiImU32TintInfo(80)
                        : pdguiImU32TitleGlow(40);
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, hlColor);
                }
            }

            ImGui::EndTable();
        }
        ImGui::Spacing();
    }
    ImGui::PopStyleVar(3);

    ImGui::Spacing();
    ImGui::TextDisabled("Click to rebind. Right-click to clear. Esc to cancel.");
    ImGui::TextDisabled("Red border: same key bound twice in this tab + device. Cross-tab coincidences are fine.");
}

/* ActionMap.HoldMsOverrides — per-action hold ms; serialized by actionmapSaveBinds(). */
static void renderActionHoldOverridesSection(void)
{
    if (!ImGui::CollapsingHeader("Per-action hold overrides (advanced)")) {
        return;
    }
    ImGui::TextWrapped(
        "Optional hold window per action (50-2000 ms), saved with binds as ActionMap.HoldMsOverrides. "
        "For Use / Interact, Default follows the global \"Use hold\" slider above; Custom replaces it for "
        "prompts and bondmove (actionmapGetEffectiveHoldMs). While Use / Interact is Custom, the global "
        "slider is disabled above (effective ms is shown there). Other actions: Default means 0 ms "
        "effective unless you choose Custom.");
    ImGui::Spacing();

    ImGui::SetNextItemWidth(pdguiScale(260.0f));
    ImGui::InputTextWithHint("Filter##holdov", "filter by action name", s_HoldOvFilter,
                             sizeof(s_HoldOvFilter));
    if (s_HoldOvFilter[0]) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##holdovclr")) {
            s_HoldOvFilter[0] = '\0';
        }
    }

    ImGui::BeginChild("##hold_ov_scroll", ImVec2(0.0f, pdguiScale(200.0f)), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImGuiTableFlags tf = ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg
                        | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
    if (ImGui::BeginTable("##hold_ov_tbl", 3, tf)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Override", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Effective", ImGuiTableColumnFlags_WidthStretch, 0.8f);
        ImGui::TableHeadersRow();

        /* Deduplicate by action enum -- the master table now has multiple
         * rows per shared action (USE on Gameplay, Menu, PauseMenu, ...) but
         * the override applies to the action enum globally, so we only
         * surface each action once. */
        bool seenAction[ACTION_COUNT];
        for (s32 i = 0; i < (s32)ACTION_COUNT; i++) seenAction[i] = false;

        for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
            InputAction act = s_BindableActions[row].action;
            if (act < 0 || act >= ACTION_COUNT) continue;
            if (seenAction[act]) continue;
            if (!stringIContains(s_BindableActions[row].name, s_HoldOvFilter)) {
                continue;
            }
            seenAction[act] = true;
            const char *nm = s_BindableActions[row].name;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(nm);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID((int)act);
            s32 ovRaw = actionmapGetActionHoldMsOverride(act);
            int mode = (ovRaw >= 0) ? 1 : 0;
            const char *modes[] = { "Default", "Custom" };
            ImGui::SetNextItemWidth(pdguiScale(118.0f));
            if (ImGui::Combo("##homode", &mode, modes, 2)) {
                if (mode == 0) {
                    actionmapSetActionHoldMsOverride(act, -1);
                } else {
                    s32 seed = (act == ACTION_USE) ? actionmapGetUseHoldThresholdMs() : 500;
                    if (seed < 50) {
                        seed = 50;
                    }
                    if (seed > 2000) {
                        seed = 2000;
                    }
                    actionmapSetActionHoldMsOverride(act, seed);
                }
                actionmapSaveBinds();
                configSave("pd.ini");
            }
            if (mode == 1) {
                s32 ms = actionmapGetActionHoldMsOverride(act);
                if (ms < 0) {
                    ms = 500;
                }
                if (ms < 50) {
                    ms = 50;
                }
                if (ms > 2000) {
                    ms = 2000;
                }
                if (PdSliderInt("##hms", &ms, 50, 2000, "%d ms")) {
                    actionmapSetActionHoldMsOverride(act, ms);
                    actionmapSaveBinds();
                    configSave("pd.ini");
                }
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(2);
            {
                s32 eff = actionmapGetEffectiveHoldMs(act);
                s32 curOv = actionmapGetActionHoldMsOverride(act);
                if (act == ACTION_USE && curOv < 0) {
                    ImGui::TextDisabled("%d ms (global)", (int)eff);
                } else {
                    ImGui::Text("%d ms", (int)eff);
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

/* Per-tab device reset. Walks every IMC in the tab's resetImcs list, strips
 * triggers belonging to the active device (MKB or Controller) for every
 * action that IMC has_mapping[] for, then re-applies that IMC's compiled-in
 * defaults via actionmapSetDefaults. The other device's triggers are
 * preserved so a user who rebound MKB and reset Controller does not lose
 * their MKB layout. */
static void resetTabDeviceToDefaults(const ImcTabDesc *tab, s32 filterCol)
{
    if (!tab) return;
    bool wantMkb = (filterCol == 0);
    for (s32 i = 0; i < tab->numResetImcs; i++) {
        InputMappingContext *imc = tab->resetImcs[i];
        if (!imc) continue;
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (!imc->has_mapping[a]) continue;
            InputMapping *m = &imc->mappings[a];
            s32 write = 0;
            for (s32 ti = 0; ti < m->num_triggers; ti++) {
                u32 vk = m->triggers[ti].vk;
                bool isMkb  = isVkMKB(vk);
                bool isCtrl = isVkController(vk);
                bool isThisDevice = wantMkb ? isMkb : isCtrl;
                if (vk > 0 && !isThisDevice) {
                    m->triggers[write++] = m->triggers[ti];
                } else {
                    m->triggers[ti].vk = 0;
                }
            }
            m->num_triggers = write;
        }
        actionmapSetDefaults(imc, 0);
    }
    actionmapSaveBinds();
    configSave("pd.ini");
}

/* Inner subtab: Keyboard & Mouse. Renders the bind table for the outer tab's
 * groupMask plus a tab-scoped reset button. */
static void renderControlsKbmInner(const ImcTabDesc *tab)
{
    /* Cancel a controller-side capture when the user switches inner tabs. */
    if (s_CaptureActive && s_CaptureColumn == 1) {
        s_CaptureActive = 0;
        s_CaptureImc    = NULL;
        s_CaptureName   = "";
        inputClearLastKey();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Key bindings -- %s", tab->label);
    ImGui::Separator();

    renderBindTable(0, "##mkb_binds", tab->shownGroups);

    ImGui::Spacing();
    char btnLabel[96];
    snprintf(btnLabel, sizeof(btnLabel), "Reset %s Keyboard & Mouse to Defaults", tab->label);
    if (PdButton(btnLabel)) {
        resetTabDeviceToDefaults(tab, 0);
    }
}

/* Inner subtab: Controller. Renders the visual mapper plus the bind table for
 * the outer tab's groupMask plus a tab-scoped reset button. */
static void renderControlsControllerInner(float scale, const ImcTabDesc *tab)
{
    /* Cancel an MKB-side capture when the user switches inner tabs. */
    if (s_CaptureActive && s_CaptureColumn == 0) {
        s_CaptureActive = 0;
        s_CaptureImc    = NULL;
        s_CaptureName   = "";
        inputClearLastKey();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Controller map -- %s", tab->label);
    ImGui::Separator();
    ImGui::TextDisabled(
        "Gamepad: use the binding table below to rebind with the controller. Drag and drop on the visual "
        "map needs a mouse.");
    ImGui::Spacing();

    /* C-Buttons are intentionally absent on the controller pad (modern Xbox /
     * DualShock pads have no C-stick). The bind table still surfaces them so
     * users with custom hardware can map them. */
    u32 mapperGroups = tab->shownGroups & ~(1u << BG_CBUTTONS);
    renderControllerVisualMapper(scale, mapperGroups);

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::TextDisabled("Button bindings -- %s", tab->label);
    ImGui::Separator();

    renderBindTable(1, "##ctrl_binds", tab->shownGroups);

    ImGui::Spacing();
    char btnLabel[96];
    snprintf(btnLabel, sizeof(btnLabel), "Reset %s Controller to Defaults", tab->label);
    if (PdButton(btnLabel)) {
        resetTabDeviceToDefaults(tab, 1);
    }
}

/* Global mouse settings. Shared across all IMCs; live in their own collapsible
 * section above the per-IMC tab bar. */
static void renderControlsGlobalMouse(void)
{
    if (!ImGui::CollapsingHeader("Mouse (global)")) {
        return;
    }
    {
        f32 mx, my;
        inputMouseGetSpeed(&mx, &my);
        if (PdSliderFloat("Mouse Sensitivity X", &mx, 0.0f, 10.0f, "%.2f")) {
            inputMouseSetSpeed(mx, my);
        }
        inputMouseGetSpeed(&mx, &my);
        if (PdSliderFloat("Mouse Sensitivity Y", &my, 0.0f, 10.0f, "%.2f")) {
            inputMouseSetSpeed(mx, my);
        }
    }
    {
        float aimX = g_PlayerExtCfg[0].mouseaimspeedx;
        if (PdSliderFloat("Crosshair Speed X", &aimX, 0.0f, 10.0f, "%.2f")) {
            g_PlayerExtCfg[0].mouseaimspeedx = aimX;
        }
        float aimY = g_PlayerExtCfg[0].mouseaimspeedy;
        if (PdSliderFloat("Crosshair Speed Y", &aimY, 0.0f, 10.0f, "%.2f")) {
            g_PlayerExtCfg[0].mouseaimspeedy = aimY;
        }
    }
    {
        bool invertY = optionsGetForwardPitch(0) == 0;
        if (PdCheckbox("Invert Y (mouse)", &invertY)) {
            optionsSetForwardPitch(0, invertY ? 0 : 1);
        }
    }

    bool mouseAimLock = g_PlayerExtCfg[0].mouseaimmode != 0;
    if (PdCheckbox("Mouse Aim Lock", &mouseAimLock)) {
        g_PlayerExtCfg[0].mouseaimmode = mouseAimLock ? 1 : 0;
    }
    {
        int lockMode = inputGetMouseLockMode();
        const char *lockOpts[] = { "Always Off", "Always On", "Auto" };
        if (PdCombo("Mouse Lock Mode", &lockMode, lockOpts, 3)) {
            inputSetMouseLockMode(lockMode);
        }
    }
    bool mouseEnabled = inputMouseIsEnabled() != 0;
    if (PdCheckbox("Mouse Enabled", &mouseEnabled)) {
        inputMouseEnable(mouseEnabled ? 1 : 0);
    }
    bool menuMouse = g_MenuMouseControl != 0;
    if (PdCheckbox("Mouse Menu Navigation", &menuMouse)) {
        g_MenuMouseControl = menuMouse ? 1 : 0;
    }
}

/* Global stick / analog tuning. Shared across all IMCs (Vehicle steering,
 * Mission look, etc. all read the same sensitivity / deadzone state). */
static void renderControlsGlobalSticks(void)
{
    if (!ImGui::CollapsingHeader("Sticks and analog (global)")) {
        return;
    }
    ImGui::TextWrapped(
        "Deadzone uses a circular gate; analog output outside the deadzone is normalized so the "
        "remaining physical travel still maps to the full 0.0-1.0 range before sensitivity is applied.");
    ImGui::Spacing();

    {
        int moveIdx = actionmapGetMoveStickPhysicalLeft() ? 0 : 1;
        s32 wantSwapped = moveIdx == 0 ? 0 : 1;
        if (inputControllerGetSticksSwapped(0) != wantSwapped) {
            inputControllerSetSticksSwapped(0, wantSwapped);
        }
        int lookIdx = 1 - moveIdx;
        const char *stickOpts[] = { "Left stick", "Right stick" };
        bool sticksChanged = false;
        if (PdCombo("Move stick", &moveIdx, stickOpts, 2)) {
            pdguiApplyMoveStickLayout(moveIdx);
            sticksChanged = true;
        }
        lookIdx = 1 - moveIdx;
        if (PdCombo("Look stick", &lookIdx, stickOpts, 2)) {
            pdguiApplyMoveStickLayout(1 - lookIdx);
            sticksChanged = true;
        }
        if (sticksChanged) {
            configSave("pd.ini");
        }
    }
    {
        f32 sensM = actionmapGetSensMoveUi();
        if (PdSliderSensUi("Move sensitivity (1-10)", &sensM)) {
            actionmapSetSensMoveUi(sensM);
            configSave("pd.ini");
        }
    }
    {
        f32 dzM = actionmapGetStickDeadzoneMove();
        if (PdSliderFloat("Move deadzone", &dzM, 0.0f, 0.5f, "%.2f")) {
            actionmapSetStickDeadzoneMove(dzM);
            configSave("pd.ini");
        }
    }
    {
        f32 sensA = actionmapGetSensAimUi();
        if (PdSliderSensUi("Look sensitivity (1-10)", &sensA)) {
            actionmapSetSensAimUi(sensA);
            configSave("pd.ini");
        }
    }
    {
        f32 sensAds = actionmapGetSensAdsUi();
        if (PdSliderSensUi("Aim-down-sights sensitivity (1-10)", &sensAds)) {
            actionmapSetSensAdsUi(sensAds);
            configSave("pd.ini");
        }
    }
    {
        f32 dzA = actionmapGetStickDeadzoneAim();
        if (PdSliderFloat("Look deadzone", &dzA, 0.0f, 0.5f, "%.2f")) {
            actionmapSetStickDeadzoneAim(dzA);
            configSave("pd.ini");
        }
    }
    {
        bool invertY = actionmapGetStickInvertY() != 0;
        if (PdCheckbox("Invert look (Y axis)", &invertY)) {
            actionmapSetStickInvertY(invertY ? 1 : 0);
            configSave("pd.ini");
        }
    }
}

/* Global hold thresholds + per-action overrides. The hold values feed
 * actionmapGetEffectiveHoldMs which the per-row label formatter reads, so
 * tweaking these immediately updates the row labels in the active tab. */
static void renderControlsGlobalHolds(void)
{
    if (!ImGui::CollapsingHeader("Hold thresholds (global)")) {
        return;
    }

    {
        s32 useHoldOv = actionmapGetActionHoldMsOverride(ACTION_USE);
        s32 effUseMs = actionmapGetEffectiveHoldMs(ACTION_USE);
        if (useHoldOv >= 0) {
            ImGui::Text("Effective Use / Interact hold: %d ms", (int)effUseMs);
            ImGui::BeginDisabled();
            s32 holdMs = actionmapGetUseHoldThresholdMs();
            PdSliderInt("Use hold (interact vs reload)", &holdMs, 50, 2000, "%d ms");
            ImGui::EndDisabled();
            ImGui::TextDisabled(
                "Per-action override is on for Use / Interact (see Per-action hold overrides below). "
                "The slider value above is the stored global default if you clear that override; it "
                "does not apply while Custom is set.");
        } else {
            s32 holdMs = actionmapGetUseHoldThresholdMs();
            if (PdSliderInt("Use hold (interact vs reload)", &holdMs, 50, 2000, "%d ms")) {
                actionmapSetUseHoldThresholdMs(holdMs);
                configSave("pd.ini");
            }
            ImGui::TextDisabled(
                "Global default for Use / Interact when no per-action override is set.");
        }
    }
    {
        s32 termExtra = actionmapGetInteractHoldExtraTerminalMs();
        if (PdSliderInt("Hackable terminal extra hold", &termExtra, 0, 2000, "%d ms")) {
            actionmapSetInteractHoldExtraTerminalMs(termExtra);
            configSave("pd.ini");
        }
        ImGui::TextDisabled(
            "Layered on effective Use hold for hackable-terminal prompts. Saved as "
            "ActionMap.InteractHoldExtraTerminalMs.");
    }

    renderActionHoldOverridesSection();
}

/* Outer per-IMC tab content: header blurb, KB&M / Controller inner subtabs. */
static void renderImcTabBody(float scale, const ImcTabDesc *tab)
{
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tab->blurb);
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTabBar("##controls_device_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem("Keyboard & Mouse")) {
            s_ControlsSubTab = 0;
            renderControlsKbmInner(tab);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controller")) {
            s_ControlsSubTab = 1;
            renderControlsControllerInner(scale, tab);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

static void renderSettingsControls(float scale)
{
    /* Load binds from pd.ini when entering the Controls tab so the UI reflects
     * the current saved state (not stale in-memory mappings). */
    if (s_ControlsNeedsInit) {
        actionmapLoadBinds();
        s_ControlsNeedsInit = false;
    }

    /* Process any active key capture before rendering tabs so a captured key
     * commits into the right IMC even if the user's last action was switching
     * outer tabs. */
    handleCaptureInput();

    if (s_ActiveImcTab < 0 || s_ActiveImcTab >= (s32)NUM_IMC_TABS) {
        s_ActiveImcTab = 0;
    }

    /* Outer tab bar -- one per IMC scene. LB / RB cycle these via the existing
     * pdguiDriveImGuiNav() translation of ACTION_MENU_TAB_PREV / _NEXT. */
    if (ImGui::BeginTabBar("##controls_imc_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (s32 ti = 0; ti < (s32)NUM_IMC_TABS; ti++) {
            const ImcTabDesc *tab = &s_ImcTabs[ti];
            if (ImGui::BeginTabItem(tab->label)) {
                /* Cancel any in-flight capture when the active outer tab
                 * changes -- the captured row no longer belongs to the
                 * visible tab and writing it back would land in the wrong
                 * IMC from the user's point of view. */
                if (s_ActiveImcTab != ti && s_CaptureActive) {
                    s_CaptureActive = 0;
                    s_CaptureImc    = NULL;
                    s_CaptureName   = "";
                    inputClearLastKey();
                }
                s_ActiveImcTab = ti;
                renderImcTabBody(scale, tab);
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    /* Global tuning sections live below the outer tab bar. They affect every
     * IMC, so collapsing-header singletons keep them out of the per-tab flow
     * but always reachable. */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    renderControlsGlobalMouse();
    renderControlsGlobalSticks();
    renderControlsGlobalHolds();
}

static void renderSettingsGame(float scale)
{
    ImGui::TextDisabled("Gameplay");
    ImGui::Separator();

    /* Crouch Mode */
    {
        int crouchMode = g_PlayerExtCfg[0].crouchmode;
        const char *crouchOpts[] = { "Hold", "Analog", "Toggle", "Toggle + Analog" };
        if (PdCombo("Crouch Mode", &crouchMode, crouchOpts, 4)) {
            g_PlayerExtCfg[0].crouchmode = crouchMode;
        }
    }

    /* FOV */
    {
        float fov = g_PlayerExtCfg[0].fovy;
        if (PdSliderFloat("Vertical FOV", &fov, 15.0f, 170.0f, "%.0f")) {
            g_PlayerExtCfg[0].fovy = fov;
        }
    }

    /* Crosshair Sway */
    {
        float sway = g_PlayerExtCfg[0].crosshairsway;
        if (PdSliderFloat("Crosshair Sway", &sway, 0.0f, 2.0f, "%.1f")) {
            g_PlayerExtCfg[0].crosshairsway = sway;
        }
    }

    /* Crosshair Size */
    {
        int chSize = g_PlayerExtCfg[0].crosshairsize;
        if (PdSliderInt("Crosshair Size", &chSize, 0, 4)) {
            g_PlayerExtCfg[0].crosshairsize = chSize;
        }
    }

    /* Crosshair Colour */
    {
        u32 col = g_PlayerExtCfg[0].crosshaircolour;
        float rgba[4] = {
            ((col >> 24) & 0xFF) / 255.0f,
            ((col >> 16) & 0xFF) / 255.0f,
            ((col >> 8) & 0xFF) / 255.0f,
            (col & 0xFF) / 255.0f,
        };
        if (ImGui::ColorEdit4("Crosshair Colour", rgba)) {
            g_PlayerExtCfg[0].crosshaircolour =
                ((u32)(rgba[0] * 255.0f) << 24) |
                ((u32)(rgba[1] * 255.0f) << 16) |
                ((u32)(rgba[2] * 255.0f) << 8) |
                ((u32)(rgba[3] * 255.0f));
        }
    }

    /* Crosshair Colour by Health */
    {
        int chHealth = g_PlayerExtCfg[0].crosshairhealth;
        const char *chHealthOpts[] = { "Off", "On (Green)", "On (White)" };
        if (PdCombo("Crosshair Colour by Health", &chHealth, chHealthOpts, 3)) {
            g_PlayerExtCfg[0].crosshairhealth = chHealth;
        }
    }

    ImGui::Separator();

    bool skipIntro = g_SkipIntro != 0;
    if (PdCheckbox("Skip Intro", &skipIntro)) {
        g_SkipIntro = skipIntro ? 1 : 0;
        configSave("pd.ini");
    }

    bool useKeyReloads = g_PlayerExtCfg[0].usereloads != 0;
    if (PdCheckbox("Use Key Reloads", &useKeyReloads)) {
        g_PlayerExtCfg[0].usereloads = useKeyReloads ? 1 : 0;
    }

    /* Jump Height */
    {
        float jump = g_PlayerExtCfg[0].jumpheight;
        char jumpLabel[32];
        if (jump <= 0.0f) {
            snprintf(jumpLabel, sizeof(jumpLabel), "Match Default");
        } else {
            snprintf(jumpLabel, sizeof(jumpLabel), "%.1f", jump);
        }
        if (PdSliderFloat("Jump Height", &jump, 0.0f, 20.0f, jumpLabel)) {
            g_PlayerExtCfg[0].jumpheight = jump;
        }
    }

    /* Show Dev Releases */
    {
        bool showDev = updaterGetShowDevReleases() != 0;
        if (PdCheckbox("Show Dev Releases", &showDev)) {
            updaterSetShowDevReleases(showDev ? 1 : 0);
            updaterCheckAsync();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Off: only show stable releases in the update list\n"
                "On: also show pre-release / dev builds");
        }
    }

    ImGui::Spacing();

    /* ---- HUD & Display ---- */
    ImGui::TextDisabled("HUD & Display");
    ImGui::Separator();

    {
        bool v = optionsGetSightOnScreen(0) != 0;
        if (PdCheckbox("Sight on Screen", &v)) {
            optionsSetSightOnScreen(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetAmmoOnScreen(0) != 0;
        if (PdCheckbox("Ammo on Screen", &v)) {
            optionsSetAmmoOnScreen(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetShowGunFunction(0) != 0;
        if (PdCheckbox("Show Gun Function", &v)) {
            optionsSetShowGunFunction(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetAlwaysShowTarget(0) != 0;
        if (PdCheckbox("Always Show Target", &v)) {
            optionsSetAlwaysShowTarget(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetShowZoomRange(0) != 0;
        if (PdCheckbox("Show Zoom Range", &v)) {
            optionsSetShowZoomRange(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetShowMissionTime(0) != 0;
        if (PdCheckbox("Show Mission Time", &v)) {
            optionsSetShowMissionTime(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetHeadRoll(0) != 0;
        if (PdCheckbox("Head Roll", &v)) {
            optionsSetHeadRoll(0, v ? 1 : 0);
        }
    }

    ImGui::Spacing();

    /* ---- Subtitles ---- */
    ImGui::TextDisabled("Subtitles");
    ImGui::Separator();

    {
        bool v = optionsGetInGameSubtitles() != 0;
        if (PdCheckbox("In-Game Subtitles", &v)) {
            optionsSetInGameSubtitles(v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetCutsceneSubtitles() != 0;
        if (PdCheckbox("Cutscene Subtitles", &v)) {
            optionsSetCutsceneSubtitles(v ? 1 : 0);
        }
    }

    ImGui::Spacing();

    /* ---- Visual Effects ---- */
    ImGui::TextDisabled("Visual Effects");
    ImGui::Separator();

    {
        bool v = optionsGetPaintball(0) != 0;
        if (PdCheckbox("Paintball Mode", &v)) {
            optionsSetPaintball(0, v ? 1 : 0);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Replace bullet wounds with coloured paint splatters");
        }
    }

    ImGui::Spacing();

    /* ---- Combat Assist ---- */
    ImGui::TextDisabled("Combat Assist");
    ImGui::Separator();

    {
        int aimMode = optionsGetAimControl(0);
        if (aimMode < 0 || aimMode > 1) aimMode = 0;
        const char *aimOpts[] = { "Hold", "Toggle" };
        if (PdCombo("Aim Mode", &aimMode, aimOpts, 2)) {
            optionsSetAimControl(0, aimMode);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Hold:   hold Fire to aim; release to holster\n"
                "Toggle: press Fire to enter aim mode; press again to exit");
        }
    }

    {
        bool v = optionsGetAutoAim(0) != 0;
        if (PdCheckbox("Auto Aim", &v)) {
            optionsSetAutoAim(0, v ? 1 : 0);
        }
    }

    {
        bool v = optionsGetLookAhead(0) != 0;
        if (PdCheckbox("Look Ahead", &v)) {
            optionsSetLookAhead(0, v ? 1 : 0);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Camera tilts forward slightly when moving");
        }
    }
}

/* ========================================================================
 * ImGui Render Callback
 * ======================================================================== */

/* Menu view state: 0 = top-level (Play/Settings/Quit), 1 = Play, 2 = Settings */
static s32 s_MenuView = 0;

/* S295 F7: s_MainMenuPushedCtx removed. The close handler at ~line 2618
 * already pops unconditionally via `inputCtxIsActive` — see comment there.
 * The ownership bool had a documented failure mode (it could be cleared by a
 * second IsWindowAppearing and then skip the pop) and was effectively dead
 * state by the time of removal. If the need for ownership tracking comes back,
 * prefer the `freshEntry = frame-gap` pattern used in pdgui_menu_endscreen.cpp
 * (S295 F5) rather than a one-shot bool. */

/* B-131 (2026-04-11): wall-clock tick at which the main menu last appeared.
 * Used to gate the ESC/B close handler: the first MAIN_MENU_CLOSE_GRACE_MS
 * of a new appearance do NOT run the IsKeyPressed(Escape/GamepadFaceRight)
 * close check.  This prevents the open-then-immediately-close race that
 * happens when:
 *   1. User is in a deferred-pop transition (e.g. CI free-roam after backing
 *      out of the main menu once) and the g_CtxImGuiMenu push_tick grace in
 *      inputctx.c fires for SDL_KEYDOWN only — SDL_CONTROLLERBUTTONDOWN is
 *      not grace-guarded so the button-up edge from the OPEN press can
 *      survive into the first !IsWindowAppearing frame of the new menu.
 *   2. ImGui's gamepad / keyboard IsKeyPressed edge-detection fires true on
 *      that frame because the ImGui event queue processed the press AFTER
 *      the new window was marked appearing.
 * The IsWindowAppearing guard alone catches case 2 only when the edge lands
 * on the appearing frame.  The 150 ms timestamp guard catches the case
 * where the edge slips to the frame-after-appearing. */
static u32 s_MainMenuOpenedTick = 0;
#define MAIN_MENU_CLOSE_GRACE_MS 150

/* Helper: draw PD dialog window frame + title, return content start Y */
static float drawPdWindowFrame(float dialogX, float dialogY, float dialogW,
                                float dialogH, const char *title)
{
    float pdTitleH = dialogH * 0.08f;
    if (pdTitleH < 20.0f) pdTitleH = 20.0f;
    if (pdTitleH > 32.0f) pdTitleH = 32.0f;

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, title, 1);

    ImDrawList *dl = ImGui::GetWindowDrawList();

    /* B-60 fix: clip title text + glow to the title bar rect.
     * Without this, font descenders (e.g., 'g' and 's' in "Settings")
     * bleed below the title bar into the body area, appearing as stray
     * characters behind tab content when the body uses NoBackground. */
    dl->PushClipRect(ImVec2(dialogX, dialogY),
                     ImVec2(dialogX + dialogW, dialogY + pdTitleH));

    pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                      dialogW - 16.0f, pdTitleH - 4.0f);
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(dialogX + 10.0f,
                       dialogY + (pdTitleH - titleSize.y) * 0.5f),
                pdguiPalImU32(PDPAL_TITLEFG, 255), title);

    dl->PopClipRect();

    return pdTitleH;
}

/* ========================================================================
 * Debug tab — log channel filters, theme selector, memory diagnostics
 * ======================================================================== */

static const char *s_ThemeNames[] = {
    "Grey", "Blue", "Red", "Green", "White", "Silver", "Black & Gold"
};
#define PDGUI_NUM_THEMES 7

/* Representative accent colors for each theme — used to tint theme selector buttons */
static const ImVec4 s_ThemeAccentColors[] = {
    ImVec4(0.45f, 0.45f, 0.50f, 0.85f), /* Grey */
    ImVec4(0.15f, 0.30f, 0.70f, 0.85f), /* Blue */
    ImVec4(0.70f, 0.12f, 0.12f, 0.85f), /* Red */
    ImVec4(0.10f, 0.55f, 0.20f, 0.85f), /* Green */
    ImVec4(0.70f, 0.70f, 0.75f, 0.85f), /* White */
    ImVec4(0.55f, 0.55f, 0.60f, 0.85f), /* Silver */
    ImVec4(0.20f, 0.18f, 0.10f, 0.85f), /* Black & Gold */
};
static const ImVec4 s_ThemeTextColors[] = {
    ImVec4(0.90f, 0.90f, 0.90f, 1.0f),  /* Grey */
    ImVec4(0.80f, 0.85f, 1.00f, 1.0f),  /* Blue */
    ImVec4(1.00f, 0.80f, 0.80f, 1.0f),  /* Red */
    ImVec4(0.80f, 1.00f, 0.80f, 1.0f),  /* Green */
    ImVec4(0.15f, 0.15f, 0.20f, 1.0f),  /* White (dark text on light) */
    ImVec4(0.10f, 0.10f, 0.15f, 1.0f),  /* Silver (dark text on light) */
    ImVec4(0.90f, 0.78f, 0.35f, 1.0f),  /* Black & Gold (gold text) */
};

static void renderSettingsDebug(float scale)
{
    /* ------ Log Channel Filters ------ */
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Log Channels");
    ImGui::Separator();
    ImGui::Spacing();

    u32 mask = sysLogGetChannelMask();

    /* Preset buttons: All / None */
    float btnW = 110.0f * scale;  /* wide enough for "All Channels" and "Black & Gold" */
    float btnH = 24.0f * scale;
    bool isAll = (mask == LOG_CH_ALL);
    bool isNone = (mask == LOG_CH_NONE);

    if (isAll) {
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
    }
    if (ImGui::Button("All Channels", ImVec2(btnW, btnH))) {
        sysLogSetChannelMask(LOG_CH_ALL);
        mask = LOG_CH_ALL;
        configSave("pd.ini");
    }
    if (isAll) ImGui::PopStyleColor();

    ImGui::SameLine();

    if (isNone) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 0.85f));
    }
    if (ImGui::Button("None##ch", ImVec2(btnW, btnH))) {
        sysLogSetChannelMask(LOG_CH_NONE);
        mask = LOG_CH_NONE;
        configSave("pd.ini");
    }
    if (isNone) ImGui::PopStyleColor();

    ImGui::Spacing();

    /* Individual channel checkboxes — two columns */
    bool changed = false;
    ImGui::Columns(2, "##logcols", false);
    for (int i = 0; i < LOG_CH_COUNT; i++) {
        bool enabled = (mask & sysLogChannelBits[i]) != 0;
        if (ImGui::Checkbox(sysLogChannelNames[i], &enabled)) {
            if (enabled) {
                mask |= sysLogChannelBits[i];
            } else {
                mask &= ~sysLogChannelBits[i];
            }
            changed = true;
        }
        if (i == (LOG_CH_COUNT / 2) - 1) ImGui::NextColumn();
    }
    ImGui::Columns(1);

    if (changed) {
        sysLogSetChannelMask(mask);
        configSave("pd.ini");
    }

    ImGui::Spacing();

    /* Verbose toggle — persisted to pd.ini via Debug.VerboseLogging */
    bool verbose = sysLogGetVerbose() != 0;
    if (ImGui::Checkbox("Verbose Logging", &verbose)) {
        sysLogSetVerbose(verbose ? 1 : 0);
        configSave("pd.ini");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(0x%04X%s)", mask, verbose ? " +V" : "");

    ImGui::Spacing();

    /* ManifestMaxEntries — hard cap on asset manifest size.
     * Persisted to pd.ini as Debug.ManifestMaxEntries. */
    {
        int maxEnt = (int)manifestGetMaxEntries();
        ImGui::SetNextItemWidth(120.0f * scale);
        if (ImGui::InputInt("Manifest Max Entries", &maxEnt, 64, 256)) {
            manifestSetMaxEntries((s32)maxEnt);
            configSave("pd.ini");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(64 – 4096)");
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* S306: UI Theme selector + Theme Editor launcher moved to the new
     * Settings → Interface tab so all visual-customisation controls live
     * in one place. The Debug tab now focuses on diagnostics only. */
    {
        u32 warnRgba = pdguiGetTextWarning();
        ImVec4 warnCol = ImVec4(
            ((warnRgba >> 24) & 0xFF) / 255.0f,
            ((warnRgba >> 16) & 0xFF) / 255.0f,
            ((warnRgba >>  8) & 0xFF) / 255.0f,
            ((warnRgba >>  0) & 0xFF) / 255.0f);
        ImGui::TextColored(warnCol, "Visual Customization");
    }
    ImGui::Separator();
    ImGui::TextDisabled("Color Theme, Menu Style, Title Bar, Font");
    ImGui::TextDisabled("-> Settings > Interface tab");
    ImGui::Spacing();
    ImGui::Spacing();

    /* ------ Memory Diagnostics ------ */
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Memory");
    ImGui::Separator();
    ImGui::Spacing();

    u32 pcTotal = mempPCGetTotalAllocated();
    u32 pcCount = mempPCGetNumAllocations();

    ImGui::Text("Persistent: %u bytes (%u allocs)", pcTotal, pcCount);
    ImGui::Text("Heap size:  %d MB", g_OsMemSizeMb);

    ImGui::Spacing();
    if (ImGui::Button("Validate Memory", ImVec2(btnW * 1.5f, btnH))) {
        s32 ok = mempPCValidate("settings_debug");
        sysLogPrintf(LOG_NOTE, "SETTINGS_DEBUG: mempPCValidate = %s",
            ok ? "OK" : "CORRUPTED");
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* ------ Keyboard Shortcuts Reminder ------ */
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), "Shortcuts");
    ImGui::Separator();
    ImGui::TextDisabled("F6   Freeze MP bot AI");
    ImGui::TextDisabled("F7   Player invincibility");
    ImGui::TextDisabled("F12  Debug Overlay");

    ImGui::Spacing();
    ImGui::Spacing();

    /* ------ About ------ */
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.6f, 0.8f), "About");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Perfect Dark 2.0, MikeHazeJr");
    ImGui::Text("Version:  v%d.%d.%d", VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH);
    ImGui::TextDisabled("Build:    %s", VERSION_BUILD);
    ImGui::TextDisabled("Commit:   %.12s", VERSION_HASH);
    ImGui::TextDisabled("ROM:      %-10s  Target: %s", VERSION_ROMID, VERSION_TARGET);
    ImGui::TextDisabled("Branch:   %s", VERSION_BRANCH);
}

/* -----------------------------------------------------------------------
 * Catalog tab helpers
 * ----------------------------------------------------------------------- */

static const char *s_AssetTypeNames[ASSET_TYPE_COUNT] = {
    "None",          /* ASSET_NONE */
    "Map",           /* ASSET_MAP */
    "Character",     /* ASSET_CHARACTER */
    "Skin",          /* ASSET_SKIN */
    "Bot Variant",   /* ASSET_BOT_VARIANT */
    "Weapon",        /* ASSET_WEAPON */
    "Texture Pack",  /* ASSET_TEXTURES — collection/pack, distinct from individual Texture */
    "SFX",           /* ASSET_SFX */
    "Music",         /* ASSET_MUSIC */
    "Prop",          /* ASSET_PROP */
    "Vehicle",       /* ASSET_VEHICLE */
    "Mission",       /* ASSET_MISSION */
    "UI",            /* ASSET_UI */
    "Tool",          /* ASSET_TOOL */
    "Arena",         /* ASSET_ARENA */
    "Body",          /* ASSET_BODY */
    "Head",          /* ASSET_HEAD */
    "Animation",     /* ASSET_ANIMATION */
    "Texture",       /* ASSET_TEXTURE — individual texture entry */
    "Game Mode",     /* ASSET_GAMEMODE */
    "Voice",         /* ASSET_AUDIO — generic audio (voice/ambient); SFX and Music have own types */
    "HUD",           /* ASSET_HUD */
    "Effect",        /* ASSET_EFFECT */
    "Model",         /* ASSET_MODEL */
    "Lang",          /* ASSET_LANG — language string bank */
};

static const char *s_LoadStateNames[] = {
    "Registered", "Enabled", "Loaded", "Active"
};

static const char *s_ManifestTypeNames[] = {
    "Body", "Head", "Stage", "Weapon", "Component", "Model", "Anim", "Texture",
    "Lang",  /* MANIFEST_TYPE_LANG = 8 */
};

static void renderSettingsCatalog(float scale)
{
    s32 totalCount   = assetCatalogGetCount();
    s32 poolSize     = assetCatalogGetPoolSize();

    /* ------ D5.0a Spike: ROM texture bridge proof of concept ------ */
    {
        ImGui::TextColored(pdguiVec4TitleGlow(), "D5.0a Texture Bridge Spike");
        ImGui::SameLine();
        ImGui::TextDisabled("catalog id: ui/test_panel");
        void *tex = pdguiGetUiTexture("ui/test_panel");
        if (tex) {
            ImGui::Image((ImTextureID)tex, ImVec2(64.0f * scale, 64.0f * scale));
            ImGui::SameLine();
            ImGui::TextDisabled("PASS: ImGui::Image() rendered via pdguiGetUiTexture()");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                               "FAIL: pdguiGetUiTexture() returned NULL");
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    /* ------ Summary ------ */
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Asset Catalog");
    ImGui::Separator();
    ImGui::Spacing();

    /* Count loaded + mod-overridden */
    s32 loadedCount = 0;
    s32 modCount    = 0;
    for (s32 i = 0; i < poolSize; i++) {
        const asset_entry_t *e = assetCatalogGetByIndex(i);
        if (!e) continue;
        if (e->load_state >= ASSET_STATE_LOADED) loadedCount++;
        if (!e->bundled) modCount++;
    }

    ImGui::Text("Total registered: %d", totalCount);
    ImGui::SameLine();
    ImGui::TextDisabled("  Loaded: %d", loadedCount);
    ImGui::SameLine();
    ImGui::TextDisabled("  Mod-overridden: %d", modCount);
    ImGui::Spacing();

    /* Per-type breakdown — two columns */
    ImGui::Columns(2, "##cat_type_cols", false);
    for (int t = 1; t < ASSET_TYPE_COUNT; t++) {
        s32 cnt = assetCatalogGetCountByType((asset_type_e)t);
        if (cnt > 0) {
            ImGui::TextDisabled("%-12s  %d", s_AssetTypeNames[t], cnt);
            ImGui::NextColumn();
        }
    }
    ImGui::Columns(1);

    ImGui::Spacing();
    ImGui::Spacing();

    /* ------ Browsable List ------ */
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Entries");
    ImGui::Separator();
    ImGui::Spacing();

    /* Type filter */
    static s32 s_FilterType = 0;   /* 0 = All */
    static char s_SearchBuf[64]  = {};

    float filterW = 130.0f * scale;
    ImGui::SetNextItemWidth(filterW);
    if (ImGui::BeginCombo("##cat_type_filter",
            s_FilterType == 0 ? "All Types" : s_AssetTypeNames[s_FilterType])) {
        if (ImGui::Selectable("All Types", s_FilterType == 0)) s_FilterType = 0;
        for (int t = 1; t < ASSET_TYPE_COUNT; t++) {
            bool sel = (s_FilterType == t);
            if (ImGui::Selectable(s_AssetTypeNames[t], sel)) s_FilterType = t;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##cat_search", s_SearchBuf, sizeof(s_SearchBuf));
    ImGui::SameLine();
    ImGui::TextDisabled("Search");
    ImGui::Spacing();

    /* Entry table — dynamically sized to catalog pool, never outgrown by mods */
    static s32 *s_EntSortedIdx   = nullptr;
    static s32  s_EntSortedCap   = 0;
    static s32  s_EntSortedCount = 0;
    static bool s_EntSortDirty   = true; /* force rebuild on first frame */

    /* Grow buffer to current pool size when catalog expands */
    if (poolSize > s_EntSortedCap) {
        free(s_EntSortedIdx);
        s_EntSortedIdx = (s32 *)malloc((size_t)poolSize * sizeof(s32));
        s_EntSortedCap = s_EntSortedIdx ? poolSize : 0;
        s_EntSortDirty = true;
    }

    ImGuiTableFlags tflags = ImGuiTableFlags_RowBg
                           | ImGuiTableFlags_BordersInnerV
                           | ImGuiTableFlags_ScrollY
                           | ImGuiTableFlags_SizingStretchProp
                           | ImGuiTableFlags_Resizable
                           | ImGuiTableFlags_Sortable
                           | ImGuiTableFlags_Reorderable;
    float tableH = ImGui::GetContentRegionAvail().y * 0.55f;
    if (ImGui::BeginTable("##cat_entries", 6, tflags, ImVec2(0, tableH))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 3.0f);
        ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("State",    ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Idx",      ImGuiTableColumnFlags_WidthFixed,   40.0f * scale);
        ImGui::TableSetupColumn("Namespace",ImGuiTableColumnFlags_WidthFixed,   90.0f * scale);
        ImGui::TableSetupColumn("Src",      ImGuiTableColumnFlags_WidthFixed,   36.0f * scale);
        ImGui::TableHeadersRow();

        /* Rebuild sorted index list when filter or sort changes */
        {
            bool hasSearch = s_SearchBuf[0] != '\0';
            ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
            if (sortSpecs && (sortSpecs->SpecsDirty || s_EntSortDirty)) {
                s32 col = (sortSpecs->SpecsCount > 0) ? (s32)sortSpecs->Specs[0].ColumnIndex : 0;
                bool asc = (sortSpecs->SpecsCount > 0)
                           ? (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
                           : true;

                /* Collect matching indices */
                s_EntSortedCount = 0;
                if (s_EntSortedIdx) {
                    for (s32 i = 0; i < poolSize; i++) {
                        const asset_entry_t *e = assetCatalogGetByIndex(i);
                        if (!e) continue;
                        if (s_FilterType != 0 && e->type != (asset_type_e)s_FilterType) continue;
                        if (hasSearch && strstr(e->id, s_SearchBuf) == NULL) continue;
                        s_EntSortedIdx[s_EntSortedCount++] = i;
                    }
                }

                /* Sort the collected indices */
                std::sort(s_EntSortedIdx, s_EntSortedIdx + s_EntSortedCount,
                    [col, asc](s32 ia, s32 ib) -> bool {
                        const asset_entry_t *a = assetCatalogGetByIndex(ia);
                        const asset_entry_t *b = assetCatalogGetByIndex(ib);
                        if (!a || !b) return false;
                        int cmp = 0;
                        switch (col) {
                            case 0: cmp = strcmp(a->id, b->id); break;
                            case 1: cmp = (int)a->type - (int)b->type; break;
                            case 2: cmp = (int)a->load_state - (int)b->load_state; break;
                            case 3: cmp = a->runtime_index - b->runtime_index; break;
                            case 4: {
                                const char *acolon = strchr(a->id, ':');
                                const char *bcolon = strchr(b->id, ':');
                                s32 alen = acolon ? (s32)(acolon - a->id) : 4;
                                s32 blen = bcolon ? (s32)(bcolon - b->id) : 4;
                                cmp = strncmp(a->id, b->id, alen < blen ? alen : blen);
                                if (cmp == 0) cmp = alen - blen;
                                break;
                            }
                            case 5: cmp = (int)a->bundled - (int)b->bundled; break;
                            default: break;
                        }
                        return asc ? (cmp < 0) : (cmp > 0);
                    });

                sortSpecs->SpecsDirty = false;
                s_EntSortDirty = false;
            } else if (!sortSpecs) {
                /* Fallback: rebuild unsorted on filter change */
                if (s_EntSortDirty) {
                    s_EntSortedCount = 0;
                    if (s_EntSortedIdx) {
                        for (s32 i = 0; i < poolSize; i++) {
                            const asset_entry_t *e = assetCatalogGetByIndex(i);
                            if (!e) continue;
                            if (s_FilterType != 0 && e->type != (asset_type_e)s_FilterType) continue;
                            if (hasSearch && strstr(e->id, s_SearchBuf) == NULL) continue;
                            s_EntSortedIdx[s_EntSortedCount++] = i;
                        }
                    }
                    s_EntSortDirty = false;
                }
            }
        }

        /* Mark dirty when filter/search changes next frame */
        {
            static s32  s_PrevFilter = -1;
            static char s_PrevSearch[64] = {};
            if (s_PrevFilter != s_FilterType || strcmp(s_PrevSearch, s_SearchBuf) != 0) {
                s_EntSortDirty = true;
                s_PrevFilter = s_FilterType;
                memcpy(s_PrevSearch, s_SearchBuf, sizeof(s_PrevSearch));
            }
        }

        for (s32 ri = 0; ri < s_EntSortedCount; ri++) {
            const asset_entry_t *e = assetCatalogGetByIndex(s_EntSortedIdx[ri]);
            if (!e) continue;

            ImGui::TableNextRow();

            /* ID */
            ImGui::TableSetColumnIndex(0);
            if (!e->bundled) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "%s", e->id);
            } else {
                ImGui::TextUnformatted(e->id);
            }

            /* Type */
            ImGui::TableSetColumnIndex(1);
            {
                const char *tname = (e->type >= 0 && e->type < ASSET_TYPE_COUNT)
                                    ? s_AssetTypeNames[e->type] : "?";
                ImGui::TextDisabled("%s", tname);
            }

            /* Load state */
            ImGui::TableSetColumnIndex(2);
            {
                const char *sname = (e->load_state >= 0 && e->load_state <= ASSET_STATE_ACTIVE)
                                    ? s_LoadStateNames[e->load_state] : "?";
                if (e->load_state >= ASSET_STATE_LOADED) {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", sname);
                } else {
                    ImGui::TextDisabled("%s", sname);
                }
            }

            /* Runtime index */
            ImGui::TableSetColumnIndex(3);
            if (e->runtime_index >= 0) {
                ImGui::Text("%d", e->runtime_index);
            } else {
                ImGui::TextDisabled("--");
            }

            /* Namespace */
            ImGui::TableSetColumnIndex(4);
            {
                const char *colon = strchr(e->id, ':');
                if (colon && colon != e->id) {
                    ImGui::TextDisabled("%.*s", (int)(colon - e->id), e->id);
                } else {
                    ImGui::TextDisabled("base");
                }
            }

            /* Source: mod indicator */
            ImGui::TableSetColumnIndex(5);
            if (!e->bundled) {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "MOD");
            } else {
                ImGui::TextDisabled("base");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* ------ Current Manifest ------ */
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Current Stage Manifest");
    ImGui::Separator();
    ImGui::Spacing();

    const match_manifest_t *mf = &g_CurrentLoadedManifest;
    if (ImGui::BeginChild("ManifestList", ImVec2(0, 300.0f * scale))) {
    if (mf->num_entries == 0) {
        ImGui::TextDisabled("No manifest loaded (not in a match or SP stage)");
    } else {
        ImGui::Text("Entries: %d", (int)mf->num_entries);
        ImGui::Spacing();

        {
            static s32  s_MfSortedIdx[MANIFEST_MAX_ENTRIES];
            static s32  s_MfSortedCount = 0;
            static bool s_MfSortDirty   = true;
            static u16  s_MfPrevNum     = 0xFFFF;

            ImGuiTableFlags mflags = ImGuiTableFlags_RowBg
                                   | ImGuiTableFlags_BordersInnerV
                                   | ImGuiTableFlags_ScrollY
                                   | ImGuiTableFlags_SizingStretchProp
                                   | ImGuiTableFlags_Resizable
                                   | ImGuiTableFlags_Sortable
                                   | ImGuiTableFlags_Reorderable;
            float mTableH = ImGui::GetContentRegionAvail().y - 4.0f * scale;
            if (mTableH < 60.0f * scale) mTableH = 60.0f * scale;
            if (ImGui::BeginTable("##manifest_entries", 4, mflags, ImVec2(0, mTableH))) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("ID",       ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 3.0f);
                ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Slot",     ImGuiTableColumnFlags_WidthFixed,   36.0f * scale);
                ImGui::TableSetupColumn("Namespace",ImGuiTableColumnFlags_WidthFixed,   90.0f * scale);
                ImGui::TableHeadersRow();

                /* Rebuild sorted index list when manifest or sort changes */
                if (s_MfPrevNum != mf->num_entries)
                    s_MfSortDirty = true;

                {
                    ImGuiTableSortSpecs *sortSpecs = ImGui::TableGetSortSpecs();
                    if (sortSpecs && (sortSpecs->SpecsDirty || s_MfSortDirty)) {
                        s32 col = (sortSpecs->SpecsCount > 0) ? (s32)sortSpecs->Specs[0].ColumnIndex : 0;
                        bool asc = (sortSpecs->SpecsCount > 0)
                                   ? (sortSpecs->Specs[0].SortDirection == ImGuiSortDirection_Ascending)
                                   : true;

                        s_MfSortedCount = (s32)mf->num_entries;
                        for (s32 j = 0; j < s_MfSortedCount; j++)
                            s_MfSortedIdx[j] = j;

                        std::sort(s_MfSortedIdx, s_MfSortedIdx + s_MfSortedCount,
                            [mf, col, asc](s32 ia, s32 ib) -> bool {
                                const match_manifest_entry_t *a = &mf->entries[ia];
                                const match_manifest_entry_t *b = &mf->entries[ib];
                                int cmp = 0;
                                switch (col) {
                                    case 0: cmp = strcmp(a->id, b->id); break;
                                    case 1: cmp = (int)a->type - (int)b->type; break;
                                    case 2: cmp = (int)a->slot_index - (int)b->slot_index; break;
                                    case 3: {
                                        const char *acolon = strchr(a->id, ':');
                                        const char *bcolon = strchr(b->id, ':');
                                        s32 alen = acolon ? (s32)(acolon - a->id) : 4;
                                        s32 blen = bcolon ? (s32)(bcolon - b->id) : 4;
                                        cmp = strncmp(a->id, b->id, alen < blen ? alen : blen);
                                        if (cmp == 0) cmp = alen - blen;
                                        break;
                                    }
                                    default: break;
                                }
                                return asc ? (cmp < 0) : (cmp > 0);
                            });

                        sortSpecs->SpecsDirty = false;
                        s_MfSortDirty = false;
                        s_MfPrevNum = mf->num_entries;
                    }
                }

                for (s32 ri = 0; ri < s_MfSortedCount; ri++) {
                    const match_manifest_entry_t *me = &mf->entries[s_MfSortedIdx[ri]];
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(me->id);

                    ImGui::TableSetColumnIndex(1);
                    {
                        const char *mtype = (me->type < (int)(sizeof(s_ManifestTypeNames)/sizeof(s_ManifestTypeNames[0])))
                                            ? s_ManifestTypeNames[me->type] : "?";
                        ImGui::TextDisabled("%s", mtype);
                    }

                    ImGui::TableSetColumnIndex(2);
                    if (me->slot_index == MANIFEST_SLOT_MATCH) {
                        ImGui::TextDisabled("all");
                    } else {
                        ImGui::Text("%d", (int)me->slot_index);
                    }

                    ImGui::TableSetColumnIndex(3);
                    const char *colon = strchr(me->id, ':');
                    if (colon && colon != me->id) {
                        ImGui::TextDisabled("%.*s", (int)(colon - me->id), me->id);
                    } else {
                        ImGui::TextDisabled("base");
                    }
                }
                ImGui::EndTable();
            }
        }
    }
    } /* BeginChild ManifestList */
    ImGui::EndChild();
}

/* Render the Settings sub-view with LB/RB bumper tab switching */
static void renderSettingsView(float scale, float contentH)
{
    /* LB/RB bumper handling: use a pending flag so SetSelected only fires
     * for ONE frame after a bumper press, not continuously.
     *
     * 2026-04-11 fix: check PageUp/PageDown instead of GamepadL1/R1.
     * NavEnableGamepad is OFF (pdgui_backend.cpp:211) so ImGui ignores all
     * ImGuiKey_Gamepad* inputs. pdguiDriveImGuiNav() translates LB/RB
     * (ACTION_MENU_TAB_PREV/NEXT) to ImGuiKey_PageUp/PageDown — so that is
     * what we must poll here. Before this fix, gamepad bumpers fell through
     * to ImGui's default PgUp/PgDn nav which scrolled within the current
     * tab's list instead of switching tabs. */
    static s32 s_BumperPendingTab = -1; /* -1 = no pending switch */

#if defined(PD_DEV_BUILD)
    const s32 settingsTabLast = 7;
#else
    const s32 settingsTabLast = 6;
    if (s_SettingsSubTab > settingsTabLast) {
        s_SettingsSubTab = settingsTabLast;
    }
#endif

    /* S306: 8 tabs with PD_DEV_BUILD (Debug present); 7 tabs on stable (no Debug).
     * Order: 0=Video 1=Interface 2=Audio 3=Controls 4=Game 5=Updates
     * [6=Debug] 6/7=Catalog. */
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false)) {
        s_SettingsSubTab--;
        if (s_SettingsSubTab < 0) s_SettingsSubTab = settingsTabLast;
        s_BumperPendingTab = s_SettingsSubTab;
        s_NeedsFocus = true;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false)) {
        s_SettingsSubTab++;
        if (s_SettingsSubTab > settingsTabLast) s_SettingsSubTab = 0;
        s_BumperPendingTab = s_SettingsSubTab;
        s_NeedsFocus = true;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }

    /* Tab bar for settings categories */
    ImGuiTabBarFlags tabFlags = ImGuiTabBarFlags_None;
    if (ImGui::BeginTabBar("##settings_tabs", tabFlags)) {

        /* Only apply SetSelected on the frame a bumper press happens.
         * After that frame, clear the pending flag so ImGui's own tab
         * click handling works normally without fighting. */
        ImGuiTabItemFlags selFlag0 = (s_BumperPendingTab == 0) ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags selFlag1 = (s_BumperPendingTab == 1) ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags selFlag2 = (s_BumperPendingTab == 2) ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags selFlag3 = (s_BumperPendingTab == 3) ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags selFlag4 = (s_BumperPendingTab == 4) ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags selFlag5 = (s_BumperPendingTab == 5) ? ImGuiTabItemFlags_SetSelected : 0;
        ImGuiTabItemFlags selFlag6 = (s_BumperPendingTab == 6) ? ImGuiTabItemFlags_SetSelected : 0;
#if defined(PD_DEV_BUILD)
        ImGuiTabItemFlags selFlag7 = (s_BumperPendingTab == 7) ? ImGuiTabItemFlags_SetSelected : 0;
#endif
        s_BumperPendingTab = -1; /* Clear after consuming */

        if (ImGui::BeginTabItem("Video", nullptr, selFlag0)) {
            s_SettingsSubTab = 0;
            ImGui::BeginChild("##settings_scroll_v", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsVideo(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Interface", nullptr, selFlag1)) {
            s_SettingsSubTab = 1;
            ImGui::BeginChild("##settings_scroll_i", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsInterface(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Audio", nullptr, selFlag2)) {
            s_SettingsSubTab = 2;
            ImGui::BeginChild("##settings_scroll_a", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsAudio(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Controls", nullptr, selFlag3)) {
            s_SettingsSubTab = 3;
            ImGui::BeginChild("##settings_scroll_c", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsControls(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Game", nullptr, selFlag4)) {
            s_SettingsSubTab = 4;
            ImGui::BeginChild("##settings_scroll_g", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsGame(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Updates", nullptr, selFlag5)) {
            s_SettingsSubTab = 5;
            ImGui::BeginChild("##settings_scroll_u", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            pdguiUpdateRenderSettingsTab();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

#if defined(PD_DEV_BUILD)
        if (ImGui::BeginTabItem("Debug", nullptr, selFlag6)) {
            s_SettingsSubTab = 6;
            ImGui::BeginChild("##settings_scroll_d", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsDebug(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
#endif

        if (ImGui::BeginTabItem("Catalog", nullptr,
#if defined(PD_DEV_BUILD)
                selFlag7
#else
                selFlag6
#endif
                )) {
#if defined(PD_DEV_BUILD)
            s_SettingsSubTab = 7;
#else
            s_SettingsSubTab = 6;
#endif
            ImGui::BeginChild("##settings_scroll_cat", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsCatalog(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    /* Bumper hint at bottom — S311: glyph-driven key labels track active device. */
    {
        char prevKey[24], nextKey[24];
        pdguiGlyphGetActionLabel(ACTION_MENU_TAB_PREV, prevKey, (s32)sizeof(prevKey));
        pdguiGlyphGetActionLabel(ACTION_MENU_TAB_NEXT, nextKey, (s32)sizeof(nextKey));
        ImGui::TextDisabled("%s / %s to switch tabs", prevKey, nextKey);
    }
}

/* ========================================================================
 * The Grid submenu (Priority 1, 2026-04-23)
 *
 * Map picker + variant config + Enter flow.  "The Grid" button on the
 * Main Menu routes here (s_MenuView=6); Enter writes the variant into
 * g_MatchConfig and calls pdguiForgeStartSessionOn(stagenum) to hand
 * off to the session.
 *
 * Arena list is built on-demand from the catalog (ASSET_ARENA) so the
 * submenu stays in lockstep with whatever mods registered.  A Blank Map
 * entry is conditional on GRID_BLANK_STAGE (undefined by default -- see
 * pdgui_menu_grid.h for rationale).
 * ======================================================================== */

/* Grid arena category tags. The catalog category strings are
 * "Dark" / "Solo Missions" / "Classic" / "Bonus" / "Random"; the picker
 * displays the user-facing abbreviation on each row. Category is a
 * display tag only -- not a validity filter (the catalog is authoritative
 * on which arenas are playable; if it's registered, it ships). */
enum GridArenaCategory {
    GRID_CAT_MP     = 0, /* "Dark" -- native MP arenas */
    GRID_CAT_SP     = 1, /* "Solo Missions" -- SP-class stages usable in MP */
    GRID_CAT_CLASSIC= 2, /* "Classic" -- Temple / Complex / Felicity / etc. */
    GRID_CAT_BONUS  = 3, /* "Bonus" -- extra / test stages */
    GRID_CAT_RANDOM = 4, /* "Random" -- meta arenas (mp_random_*) */
    GRID_CAT_UNKNOWN = 5,
};

struct GridArenaEntry {
    char              name[64];
    char              id[64];
    s32               stagenum;
    GridArenaCategory category;
    bool              is_blank;   /* true for the synthetic Blank Map row */
};

static GridArenaEntry *s_GridArenas = NULL;
static int             s_GridArenaCount = 0;
static int             s_GridArenaCapacity = 0;
static bool            s_GridArenasBuilt = false;
static int             s_GridSelectedArena = 0;

/* AUDIT-24-M7 (2026-04-25): clear the built-flag so the next render
 * rebuilds the Grid arena list from the current catalog state.  Called
 * from `modmgrCatalogChanged()` whenever a mod is enabled / disabled /
 * scanned, so mod-authored arenas appear in the picker without a
 * process restart. */
extern "C" void pdguiGridArenasInvalidate(void)
{
    s_GridArenasBuilt = false;
}

static GridArenaCategory gridClassifyCategory(const char *cat)
{
    if (!cat || !cat[0])                         return GRID_CAT_UNKNOWN;
    if (strcmp(cat, "Dark") == 0)                return GRID_CAT_MP;
    if (strcmp(cat, "Solo Missions") == 0)       return GRID_CAT_SP;
    if (strcmp(cat, "Classic") == 0)             return GRID_CAT_CLASSIC;
    if (strcmp(cat, "Bonus") == 0)               return GRID_CAT_BONUS;
    if (strcmp(cat, "Random") == 0)              return GRID_CAT_RANDOM;
    return GRID_CAT_UNKNOWN;
}

static const char *gridCategoryTag(GridArenaCategory c)
{
    switch (c) {
    case GRID_CAT_MP:      return "MP";
    case GRID_CAT_SP:      return "SP";
    case GRID_CAT_CLASSIC: return "Classic";
    case GRID_CAT_BONUS:   return "Bonus";
    case GRID_CAT_RANDOM:  return "Random";
    default:               return "?";
    }
}

/* Variant state.  Simple local mirror until the user commits via Enter. */
static int   s_GridScenarioIdx = 0;     /* index into s_GridScenarios[] */
static int   s_GridTimeLimit   = 10;    /* minutes, 0 = unlimited */
static int   s_GridScoreLimit  = 10;    /* kills/points, 0 = unlimited */
static bool  s_GridTeamsOn     = false;
static bool  s_GridOneHitKills = false;
static bool  s_GridSlowMotion  = false;

struct GridScenarioEntry {
    const char *id;    /* catalog ID (NULL sentinel) */
    const char *label;
    bool        teams; /* team-based default */
};

/* Base Combat Simulator scenarios.  Matches
 * port/src/assetcatalog_base_extended.c::s_BaseGameModes. */
static const GridScenarioEntry s_GridScenarios[] = {
    { "base:combat",             "Combat",              false },
    { "base:hold_the_briefcase", "Hold the Briefcase",  false },
    { "base:hacker_central",     "Hacker Central",      false },
    { "base:pop_a_cap",          "Pop a Cap",           false },
    { "base:king_of_the_hill",   "King of the Hill",    true  },
    { "base:capture_the_case",   "Capture the Case",    true  },
};
static const int s_GridScenarioCount =
    (int)(sizeof(s_GridScenarios) / sizeof(s_GridScenarios[0]));

static void gridArenaCollect(const asset_entry_t *e, void *userdata)
{
    (void)userdata;
    if (!e) return;

    /* Source-cleanup pass (2026-04-24): the catalog registration is now
     * authoritative on which arenas are valid. assetcatalog_base.c NULLs
     * the slots for test / placeholder stages so they never reach here.
     * The only runtime gate the picker enforces is the feature-unlock
     * gate (legitimate gameplay feature, not a data-validity check). */

    if (!challengeIsFeatureUnlocked((u8)e->ext.arena.requirefeature)) {
        return;
    }

    char *langText = langSafe((s32)e->ext.arena.name_langid);
    if (!langText || !langText[0]) {
        /* Catalog registration contract: every registered arena must have
         * a resolvable langbank name. Hit = data bug, not a soft reject. */
        sysLogPrintf(LOG_ERROR,
            "GRID.MENU: arena id=\"%s\" stagenum=0x%02x langid=0x%04x has no "
            "langbank name -- catalog registration bug, entry skipped",
            e->id, e->ext.arena.stagenum, e->ext.arena.name_langid);
        return;
    }

    if (s_GridArenaCount >= s_GridArenaCapacity) {
        int newCap = (s_GridArenaCapacity == 0) ? 32 : s_GridArenaCapacity * 2;
        GridArenaEntry *newBuf = (GridArenaEntry *)realloc(
                s_GridArenas, (size_t)newCap * sizeof(GridArenaEntry));
        if (!newBuf) {
            sysLogPrintf(LOG_WARNING, "GRID.MENU: arena list realloc failed at %d",
                         s_GridArenaCount);
            return;
        }
        s_GridArenas = newBuf;
        s_GridArenaCapacity = newCap;
    }

    GridArenaEntry *a = &s_GridArenas[s_GridArenaCount];
    strncpy(a->name, langText, sizeof(a->name) - 1);
    a->name[sizeof(a->name) - 1] = '\0';
    strncpy(a->id, e->id, sizeof(a->id) - 1);
    a->id[sizeof(a->id) - 1] = '\0';
    a->stagenum = (s32)e->ext.arena.stagenum;
    a->category = gridClassifyCategory(e->category);
    a->is_blank = false;

    s_GridArenaCount++;
}

static int gridArenaCompare(const void *a, const void *b)
{
    const GridArenaEntry *ea = (const GridArenaEntry *)a;
    const GridArenaEntry *eb = (const GridArenaEntry *)b;
    /* Blank Map always first, then alphabetical. */
    if (ea->is_blank != eb->is_blank) return ea->is_blank ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

static void gridArenaListBuild(void)
{
    free(s_GridArenas);
    s_GridArenas = NULL;
    s_GridArenaCount = 0;
    s_GridArenaCapacity = 0;

#ifdef GRID_BLANK_STAGE
    /* Reserve the Blank Map slot at the top of the list. */
    s_GridArenaCapacity = 32;
    s_GridArenas = (GridArenaEntry *)malloc(
            (size_t)s_GridArenaCapacity * sizeof(GridArenaEntry));
    if (s_GridArenas) {
        GridArenaEntry *a = &s_GridArenas[0];
        strncpy(a->name, "Blank Map", sizeof(a->name) - 1);
        a->name[sizeof(a->name) - 1] = '\0';
        a->id[0] = '\0';
        a->stagenum = (s32)GRID_BLANK_STAGE;
        a->is_blank = true;
        s_GridArenaCount = 1;
    }
#endif

    assetCatalogIterateByType(ASSET_ARENA, gridArenaCollect, NULL);

    if (s_GridArenaCount > 1) {
        qsort(s_GridArenas, (size_t)s_GridArenaCount, sizeof(GridArenaEntry),
              gridArenaCompare);
    }

    sysLogPrintf(LOG_NOTE, "GRID.MENU: arena list built (%d entries)",
                 s_GridArenaCount);
    if (s_GridSelectedArena >= s_GridArenaCount) s_GridSelectedArena = 0;
    s_GridArenasBuilt = true;
}

/* Apply the submenu state to g_MatchConfig and call
 * pdguiForgeStartSessionOn(stagenum).  Returns true on success. */
static bool gridCommitEnter(void)
{
    if (s_GridArenaCount <= 0) {
        sysLogPrintf(LOG_WARNING, "GRID.MENU: commit rejected -- arena list empty");
        return false;
    }
    if (s_GridSelectedArena < 0 || s_GridSelectedArena >= s_GridArenaCount) {
        sysLogPrintf(LOG_WARNING, "GRID.MENU: commit rejected -- selected=%d of %d",
                     s_GridSelectedArena, s_GridArenaCount);
        return false;
    }

    const GridArenaEntry *a = &s_GridArenas[s_GridSelectedArena];

    /* Resolve meta-arenas (Random Multi / Random Solo) to a concrete
     * stagenum at commit time. The user sees "Random Multi" as a row in
     * the picker; Enter rolls the dice and lands on a real stage. */
    s32 commit_stagenum = a->stagenum;
    const char *commit_id = a->id;
    if (!a->is_blank) {
        if (commit_stagenum == GRID_STAGE_MP_RANDOM_MULTI) {
            commit_stagenum = (s32)mpChooseRandomMultiStage();
            commit_id = "";   /* resolved stagenum wins; clear catalog id */
        } else if (commit_stagenum == GRID_STAGE_MP_RANDOM_SOLO) {
            commit_stagenum = (s32)mpChooseRandomSoloStage();
            commit_id = "";
        }
    }

    /* One loud sanity assertion before handing off to the stage-load
     * pipeline. Catches any remaining registration or random-resolver bug
     * -- a hit here means real data corruption, not a soft reject. */
    if (!a->is_blank) {
        if (commit_stagenum <= 0
                || !GRID_STAGE_IS_GAMEPLAY(commit_stagenum)
                || stageGetIndex(commit_stagenum) < 0) {
            sysLogPrintf(LOG_ERROR,
                "GRID.MENU: commit ABORT -- resolved stagenum 0x%02x for arena "
                "\"%s\" is not a loadable gameplay stage (data bug)",
                (u32)commit_stagenum, a->name);
            return false;
        }
    }

    /* Establish a clean match config (resets slots, defaults, options) then
     * overwrite the variant-shaped fields from the submenu. */
    matchConfigInit();

    if (a->is_blank || !commit_id[0]) {
        g_MatchConfig.stage_id[0] = '\0';
    } else {
        strncpy(g_MatchConfig.stage_id, commit_id,
                sizeof(g_MatchConfig.stage_id) - 1);
        g_MatchConfig.stage_id[sizeof(g_MatchConfig.stage_id) - 1] = '\0';
    }
    g_MatchConfig.stagenum = (u8)commit_stagenum;

    if (s_GridScenarioIdx < 0 || s_GridScenarioIdx >= s_GridScenarioCount) {
        s_GridScenarioIdx = 0;
    }
    const GridScenarioEntry *sc = &s_GridScenarios[s_GridScenarioIdx];
    strncpy(g_MatchConfig.scenario_id, sc->id,
            sizeof(g_MatchConfig.scenario_id) - 1);
    g_MatchConfig.scenario_id[sizeof(g_MatchConfig.scenario_id) - 1] = '\0';

    g_MatchConfig.timelimit       = (u8)(s_GridTimeLimit & 0xff);
    g_MatchConfig.scorelimit      = (u8)(s_GridScoreLimit & 0xff);
    g_MatchConfig.teamscorelimit  = (u16)s_GridScoreLimit;

    u32 opts = g_MatchConfig.options;
    if (s_GridTeamsOn)     opts |=  (u32)MPOPTION_TEAMSENABLED;
    else                   opts &= ~(u32)MPOPTION_TEAMSENABLED;
    if (s_GridOneHitKills) opts |=  (u32)MPOPTION_ONEHITKILLS;
    else                   opts &= ~(u32)MPOPTION_ONEHITKILLS;
    if (s_GridSlowMotion)  opts |=  (u32)MPOPTION_SLOWMOTION_ON;
    else                   opts &= ~(u32)MPOPTION_SLOWMOTION_ON;
    g_MatchConfig.options = opts;

    sysLogPrintf(LOG_NOTE,
            "GRID.MENU: enter map='%s' stagenum=0x%02x scenario='%s' "
            "tl=%d sl=%d teams=%d ohk=%d slo=%d",
            a->is_blank ? "(blank)" : a->name, (u32)a->stagenum,
            sc->id, s_GridTimeLimit, s_GridScoreLimit,
            (int)s_GridTeamsOn, (int)s_GridOneHitKills, (int)s_GridSlowMotion);

    /* Hand off.  pdguiForgeStartSessionOn calls mainChangeToStage when the
     * current stage differs; forgemode activates on next tick. */
    return pdguiForgeStartSessionOn(a->stagenum) != 0;
}

static void renderGridSubmenu(float scale, float buttonW, float buttonH,
                              float spacing)
{
    ImGui::Dummy(ImVec2(0, 4.0f * scale));

    if (!s_GridArenasBuilt) {
        gridArenaListBuild();
    }

    ImGui::TextWrapped(
        "The Grid is a live-bot-test and edit facility.  Pick a base map, "
        "configure the variant (gametype + rules), then Enter The Grid to "
        "spin up the session.");
    ImGui::Spacing();

    /* ----------------------------------------------------------------
     * Map picker (category tag per row)
     * ---------------------------------------------------------------- */
    ImGui::SeparatorText("Base Map");

    if (s_GridArenaCount <= 0) {
        ImGui::TextDisabled("No playable arenas registered in the catalog.");
    } else {
        float listH = buttonH * 5.5f;
        if (listH < 120.0f) listH = 120.0f;
        if (ImGui::BeginListBox("##grid_arena_list",
                                ImVec2(buttonW, listH))) {
            for (int i = 0; i < s_GridArenaCount; i++) {
                const GridArenaEntry *a = &s_GridArenas[i];
                char label[112];
                if (a->is_blank) {
                    snprintf(label, sizeof(label),
                             "%-32s  [Blank]", a->name);
                } else {
                    /* Category tag right-justified for scan-ability. */
                    snprintf(label, sizeof(label), "%-32s  [%s]",
                             a->name, gridCategoryTag(a->category));
                }
                const bool sel = (i == s_GridSelectedArena);
                ImGui::PushID(i);
                if (ImGui::Selectable(label, sel)) {
                    s_GridSelectedArena = i;
                }
                if (sel && ImGui::IsWindowAppearing()) {
                    ImGui::SetScrollHereY(0.25f);
                }
                ImGui::PopID();
            }
            ImGui::EndListBox();
        }
    }

    /* ----------------------------------------------------------------
     * Variant: gametype + rules
     * ---------------------------------------------------------------- */
    ImGui::SeparatorText("Variant");

    if (s_GridScenarioIdx < 0 || s_GridScenarioIdx >= s_GridScenarioCount) {
        s_GridScenarioIdx = 0;
    }
    const char *preview = s_GridScenarios[s_GridScenarioIdx].label;
    ImGui::SetNextItemWidth(buttonW);
    if (ImGui::BeginCombo("Gametype", preview)) {
        for (int i = 0; i < s_GridScenarioCount; i++) {
            const bool sel = (i == s_GridScenarioIdx);
            if (ImGui::Selectable(s_GridScenarios[i].label, sel)) {
                s_GridScenarioIdx = i;
                /* Auto-flip teams on when picking a team-based mode. */
                if (s_GridScenarios[i].teams) s_GridTeamsOn = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SetNextItemWidth(buttonW);
    ImGui::SliderInt("Time Limit (min, 0 = unlimited)",
                     &s_GridTimeLimit, 0, 60);
    ImGui::SetNextItemWidth(buttonW);
    ImGui::SliderInt("Score Limit (0 = unlimited)",
                     &s_GridScoreLimit, 0, 100);

    ImGui::Checkbox("Teams enabled", &s_GridTeamsOn);
    ImGui::SameLine();
    ImGui::Checkbox("One-hit kills", &s_GridOneHitKills);
    ImGui::SameLine();
    ImGui::Checkbox("Slow motion", &s_GridSlowMotion);

    /* ----------------------------------------------------------------
     * Enter The Grid
     * ---------------------------------------------------------------- */
    ImGui::Dummy(ImVec2(0, spacing));

    const bool canEnter = (s_GridArenaCount > 0);
    if (!canEnter) ImGui::BeginDisabled();
    if (PdButton("Enter The Grid", ImVec2(buttonW, buttonH * 1.2f))) {
        if (gridCommitEnter()) {
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            s_MenuView = 0;
        } else {
            /* Stay on the submenu; gridCommitEnter already logged why. */
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
    }
    if (!canEnter) ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0, spacing));

    if (PdButton("Back", ImVec2(buttonW, buttonH))) {
        s_MenuView = 0;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
}

/* B-131 ext: duplicate dialog guard — prevents overlapped instances when
 * Start/Pause pushes the dialog while it's already rendering. */
static bool s_MainMenuIsRendering = false;

static s32 renderMainMenu(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
    /* Duplicate guard: if we're already mid-render (second dialog instance
     * on the stack), silently consume but don't draw a second copy. */
    if (s_MainMenuIsRendering) {
        return 1; /* consumed — prevent legacy from rendering */
    }
    /* B-153: main menu (PC) and main menu (Pause) share renderMainMenu;
     * when one is pushed, menuPushDialog auto-opens its `nextsibling` (the
     * CiOptions variant) at the same layer, so menuRenderDialogs queues
     * TWO hotswap entries per frame. The sibling maps to
     * renderCiSettingsRedirect (different renderer), but if a future mod
     * or refactor ever attaches two dialogdefs to this renderer we want
     * the sibling path to early-out cleanly. Also guards against
     * transition-frac edge cases where the "other" dialog is rendered
     * alongside curdialog during a swipe. */
    if (!menuDialogIsCurrent(dialog)) {
        return 1; /* consumed — sibling, not the active dialog */
    }
    /* Bug 1 fix: Combat Simulator solo Room is a pure-ImGui overlay that
     * leaves the Main Menu dialog on the menu stack (CI button calls
     * pdguiSoloRoomOpen() without popping). When a child dialog (Team
     * Setup, Change Agent, ...) is pushed then popped while the Room is
     * up, Main Menu's ImGui window goes hidden→visible, fires
     * IsWindowAppearing, SetWindowFocus()es itself on top of the Room.
     * Suppress the Main Menu render entirely while the Room owns the
     * screen; it comes back as soon as "Back to Menu" clears the flag. */
    if (pdguiSoloRoomIsActive()) {
        return 1;
    }
    /* B-173: The Grid (Forge) session owns the screen while active.  The
     * "The Grid" button calls pdguiForgeStartSession() which kicks off a
     * stage transition to CI Training WITHOUT popping the main-menu
     * dialog.  During the session the HUD + editor overlay own the
     * screen, but the Main Menu dialog is still on the menu stack — its
     * ImGui window keeps going hidden/visible across stage transitions,
     * fires IsWindowAppearing, and SetWindowFocus()es itself back on
     * top, producing the "menus were buggier" symptom once the user
     * exits the forge session.  Mirror the solo-room fix above: while a
     * forge session is active, consume the main-menu render entirely so
     * the Grid HUD/editor owns focus, and let the main menu come back
     * cleanly once forgeExitSession() clears the session flag. */
    if (forgeSessionIsActive()) {
        return 1;
    }
    s_MainMenuIsRendering = true;
    /* E.3: Enforce the user's saved theme — prevents tint bleed from post-mission
     * endscreen (which sets palette 3=green or 2=red and never restores it).
     * P5: Uses catalog-backed theme instead of hardcoded blue.
     * Guard: only reload when theme ID changes (avoids per-frame catalog lookup). */
    {
        static char s_LastThemeId[64] = "";
        const char *activeId = pdguiThemeGetActiveId();
        if (activeId && strcmp(s_LastThemeId, activeId) != 0) {
            pdguiThemeLoadFromCatalog(activeId);
            strncpy(s_LastThemeId, activeId, sizeof(s_LastThemeId) - 1);
            s_LastThemeId[sizeof(s_LastThemeId) - 1] = '\0';
        }
    }

    float scale = pdguiScaleFactor();
    float dialogW = pdguiMenuWidth();
    float dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    float dialogX = menuPos.x;
    float dialogY = menuPos.y;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##main_menu", nullptr, wflags)) {
        ImGui::End();
        s_MainMenuIsRendering = false;
        return 1;
    }

    /* S304 / CI load: attach g_CtxImGuiMenu whenever this window is open, not
     * only on IsWindowAppearing. After a stage transition into CI, ImGui may
     * keep "##main_menu" without a fresh Appearing frame while menupool still
     * has the slot live from menuPushDialog (ctx=NULL). Gameplay stayed the
     * input stack top so pdguiIsActive() was false and the interact prompt
     * drew over the main menu (Hold X facing the hub PC). menupoolAcquireDialog
     * is idempotent; S300 attaches the ctx on active slots when owned_ctx was
     * never bound. */
    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    /* Auto-possess: give this window nav focus ONLY on first appearance.
     * Must be AFTER Begin(). Using SetWindowFocus() instead of
     * SetNextWindowFocus() to avoid stealing focus from child popups. */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_MenuView = 0; /* Always open to main menu */
        s_NeedsFocus = true;
        /* B-131: stamp the open time so the close handler can grace-guard
         * the first MAIN_MENU_CLOSE_GRACE_MS of this appearance. */
        s_MainMenuOpenedTick = SDL_GetTicks();
        /* B-131: clear the stale Escape / GamepadFaceRight edges that the
         * opening press queued into ImGui's input queue before this window
         * existed.  Without this, a gamepad B-button or keyboard Escape
         * press that opened the menu is still reported as "just pressed"
         * on the frame after IsWindowAppearing — surviving past the
         * IsWindowAppearing guard and slamming the close handler.
         * AddKeyEvent(..., false) forces the down-state off this frame so
         * the next frame sees prev=false cur=false (released, then re-press
         * if the user actually wants to close). */
        ImGuiIO &nio = ImGui::GetIO();
        nio.AddKeyEvent(ImGuiKey_Escape, false);
        nio.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
        /* B-131 extension: also clear Start/A/Enter edges to prevent the
         * opening press from being read as a menu selection on the first
         * frame.  Without this, opening with Start can auto-select the
         * first focused button. */
        nio.AddKeyEvent(ImGuiKey_GamepadStart, false);
        nio.AddKeyEvent(ImGuiKey_GamepadFaceDown, false);
        nio.AddKeyEvent(ImGuiKey_Enter, false);
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu OPEN");
    }

    /* Determine title based on current view */
    const char *windowTitle = "Perfect Dark";
    if (s_MenuView == 1) windowTitle = "Solo Play";
    else if (s_MenuView == 2) windowTitle = "Settings";
    else if (s_MenuView == 3) windowTitle = "Modding";
    else if (s_MenuView == 4) windowTitle = "Online Play";
    else if (s_MenuView == 5) windowTitle = "Player Statistics";
    else if (s_MenuView == 6) windowTitle = "The Grid";

    float pdTitleH = drawPdWindowFrame(dialogX, dialogY, dialogW, dialogH, windowTitle);

    /* S305: content inset — keep menu items clear of the nineslice chrome
     * border plus a small breathing-room buffer so the content doesn't feel
     * cramped against the inner edge. Endscreen + pause menu use the same
     * pattern (resolveEndscreenPadding / pdguiThemeGetContentInset). */
    float insL = 0, insR = 0, insT = 0, insB = 0;
    pdguiThemeGetContentInset(&insL, &insR, &insT, &insB);
    float breathe = pdguiScale(8.0f);
    float padX = pdguiScale(16.0f);
    if (insL + breathe > padX) padX = insL + breathe;
    float padR = pdguiScale(16.0f);
    if (insR + breathe > padR) padR = insR + breathe;
    float padB = pdguiScale(14.0f);
    if (insB + breathe > padB) padB = insB + breathe;
    float padT = pdTitleH + ImGui::GetStyle().WindowPadding.y + breathe;
    if (insT + breathe + pdTitleH > padT) padT = insT + breathe + pdTitleH;

    /* Offset content below the PD title bar and inside the border */
    ImGui::SetCursorPos(ImVec2(padX, padT));

    float buttonH = 40.0f * scale;
    float contentW = dialogW - padX - padR;
    if (contentW < pdguiScale(32.0f)) contentW = pdguiScale(32.0f); /* sanity */
    float buttonW = contentW;
    float spacing = 6.0f * scale;

    /* B button / Escape navigation:
     * Sub-views (Play, Settings) -> back to top-level
     * Top-level -> close menu entirely (return to CI free-roam)
     *
     * Guard: skip on the frame the window first appears. When the user
     * presses B/Escape to close the menu and then reopens it, ImGui's
     * key state can still report IsKeyPressed=true on the first frame,
     * which would immediately close the menu again.
     *
     * B-131 extra guard (2026-04-11): also suppress the close check for
     * MAIN_MENU_CLOSE_GRACE_MS after the appearance timestamp.  The
     * IsWindowAppearing guard alone only covers frame N (appearing frame);
     * if ImGui processes the opening keypress on frame N+1 instead of
     * frame N (backend / hotswap timing, deferred input pump), the
     * IsKeyPressed edge fires on N+1 where !IsWindowAppearing is true.
     * The timestamp guard catches that second-frame case. */
    u32 nowTick = SDL_GetTicks();
    u32 sinceOpen = nowTick - s_MainMenuOpenedTick;
    bool closeGracePending = (sinceOpen < MAIN_MENU_CLOSE_GRACE_MS);

    /* S306 input-bug fix: include the direct title-close channel
     * alongside the Escape / B-button edges. Fixes the reported bug
     * where clicking the X on Settings needed two attempts - ImGui's
     * own nav was eating the first Escape edge before the renderer's
     * IsKeyPressed saw it. pdguiConsumeTitleClose returns 1 at most
     * once per X click and resets the flag internally.
     *
     * 2026-04-23 B-230 fix: controller B needs two presses to close.
     * Same root cause as the S306 X-button fix: ImGui's internal nav
     * was eating the first Escape edge that pdguiDriveImGuiNav injected
     * from actionPressed(ACTION_CANCEL_USE). Add a third close channel
     * reading actionPressed directly, which is the hardware-level edge
     * (untouchable by ImGui's nav consumption). The IsKeyPressed path
     * stays in as a fallback for the pure-keyboard Escape case (where
     * no ACTION_CANCEL_USE binding exists or differs from the key).
     * The closeGracePending guard only applies to the IsKeyPressed
     * path - the actionPressed edge is precise at the hardware layer
     * and doesn't need a timestamp grace (IsWindowAppearing alone is
     * enough to skip the appearing frame itself). */
    bool titleClose = pdguiConsumeTitleClose() != 0;
    bool actionCancelEdge = actionPressed(0, ACTION_CANCEL_USE) != 0;
    bool keyboardEscape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    /* titleClose and actionCancelEdge bypass the closeGracePending grace: they
     * are precise signals (X-button click flag / hardware actionmap edge) that
     * cannot be spoofed by a queued opening press. keyboardEscape keeps the
     * grace because ImGui's key queue can carry an opening Escape edge into
     * the first few frames of the new window (B-131 rationale). */
    if (!ImGui::IsWindowAppearing()
        && (titleClose || actionCancelEdge
            || (!closeGracePending && keyboardEscape))) {
        if (s_MenuView != 0) {
            if (s_MenuView == 2) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — settings CLOSE (view 2->0)%s",
                             titleClose ? " [via X]" : "");
            } else if (s_MenuView == 3) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — modding hub CLOSE (view 3->0)");
                pdguiModdingHubHide();
            } else if (s_MenuView == 4) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — online play CLOSE (view 4->0)");
            } else if (s_MenuView == 5) {
                pdguiMenuStatsHide();
            } else if (s_MenuView == 6) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC -- Grid submenu CLOSE (view 6->0)");
            } else {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — sub-view %d -> 0", s_MenuView);
            }
            s_MenuView = 0;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        } else {
            /* At top-level: close the menu, return to Carrington Institute */
            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu CLOSE via ESC/B (top-level -> CI free-roam)");
            pdguiPlaySound(PDGUI_SND_KBCANCEL);

            /* Restore game control BEFORE menuPopDialog. The legacy
             * menutick bg-transition (func0f0fa6ac) never completes
             * under ImGui hotswap. Must restore ALL game state:
             *  - lvSetPaused(false): unfreeze game world (lvupdate240=0 while paused)
             *  - playerUnpause(): reset pausemode + music (only if pausemode==PAUSED)
             *  - g_PlayersWithControl: allow movement/buttons in bmoveTick
             * All three must run before menuPopDialog in case it has side effects. */
            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: restoring game state — lvIsPaused=%d g_PlayersWithControl[0]=%d",
                         lvIsPaused(), (int)g_PlayersWithControl[0]);
            lvSetPaused(false);
            playerUnpause();
            g_PlayersWithControl[0] = true;
            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: game state restored — lvIsPaused=%d", lvIsPaused());
            /* S304: menuPopDialog → menuCloseDialog → menupoolReleaseDialog
             * releases the pool slot AND pops the owned ctx. If the stack
             * is somehow already at depth=0 (force-close race), the
             * underflow handler in menuPopDialog catches it with
             * menupoolReleaseAll. Either way, no manual ctx pop needed. */
            menuPopDialog();

            /* S306: defensive ctx flush. If g_CtxImGuiMenu is still on
             * the stack after menuPopDialog (e.g. because a sibling
             * renderer like renderCiSettingsRedirect pushed it at boot
             * and the pool-release chain only tears down the current
             * main menu slot), input would continue to route to
             * g_ImcMenu instead of gameplay — that's the "movement
             * locked after menu closed" symptom in Mike's playtest
             * report. Popping it here closes the leak without requiring
             * the full renderCiSettingsRedirect S300 migration. The pop
             * is idempotent (no-op when not active) so it's safe even
             * when menuCloseDialog already cleaned up. */
            if (inputCtxIsActive(&g_CtxImGuiMenu)) {
                sysLogPrintf(LOG_NOTE,
                    "MENU_IMGUI: defensive inputCtxPopDeferred(g_CtxImGuiMenu) — leak class caught on top-level close");
                inputCtxPopDeferred(&g_CtxImGuiMenu);
            }
        }
    }

    /* LB/RB: cycle through top-level sub-views (1=Solo, 2=Settings, 3=Modding, 4=Online).
     * From view 0 (hub), LB/RB enter the first/last sub-view.
     * Wraps around: view 1 ← LB → view 4, view 4 → RB → view 1.
     *
     * 2026-04-11 fix: poll PageUp/PageDown instead of GamepadL1/R1.  See
     * renderSettingsView() comment for the full rationale.  pdguiDriveImGuiNav
     * already injects these keys from ACTION_MENU_TAB_PREV/NEXT every frame. */
    /* Issue 12: bumpers (LB/RB → PageUp/PageDown) only switch tabs inside
     * the Settings sub-view (view 2).  At the root level they do nothing.
     * The Settings view handles its own bumper tab switching internally
     * in renderSettingsView(). Consume PageUp/PageDown at all other levels
     * to prevent ImGui's nav from scrolling or jumping focus. */
    if (!ImGui::IsWindowAppearing() && s_MenuView != 2) {
        /* Silently consume bumper presses — no action at root or non-Settings views */
        (void)ImGui::IsKeyPressed(ImGuiKey_PageUp, false);
        (void)ImGui::IsKeyPressed(ImGuiKey_PageDown, false);
    }

    if (s_MenuView == 0) {
        /* ================================================================
         * TOP LEVEL: Solo Play / Online Play / Change Agent / Settings
         * Quit Game docked to bottom-right, opens canonical S385 confirm
         * modal (M-Q-A 2026-04-19 — replaces the old inline
         * button-toggle-between-Quit-Game-and-Confirm-Quit pattern).
         * ================================================================ */
        static s32 s_QuitOpenFrame = -1;

        ImGui::Dummy(ImVec2(0, 8.0f * scale));

        /* Solo Play -- opens local lobby (no server connection) */
        if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
        if (PdButton("Solo Play", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 1; /* Solo play sub-menu for now; will become local lobby */
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Online Play */
        if (PdButton("Online Play", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 4;
            pdguiPlaySound(PDGUI_SND_SELECT);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Change Agent */
        if (PdButton("Change Agent", ImVec2(buttonW, buttonH * 1.2f))) {
            menuPushDialog(&g_ChangeAgentMenuDialog);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Settings */
        if (PdButton("Settings", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 2;
            sysLogPrintf(LOG_NOTE, "MENU_STACK: settings OPEN (s_MenuView=2)");
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Mods -- opens the Modding Hub (view 3) */
        if (PdButton("Mods", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 3;
            pdguiModdingHubShow();
            sysLogPrintf(LOG_NOTE, "MENU_STACK: modding hub OPEN (s_MenuView=3)");
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Cheats -- opens cheats hub dialog */
        if (PdButton("Cheats", ImVec2(buttonW, buttonH * 1.2f))) {
            menuPushDialog(&g_CheatsMenuDialog);
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Stats -- opens the Stats Viewer (view 5) */
        if (PdButton("Stats", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 5;
            pdguiMenuStatsShow();
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* The Grid -- live-bot-test + edit facility.  Opens a submenu
         * (s_MenuView=6) where the user picks the base map and
         * configures the variant (gametype + rules) before entering
         * the session.  The previous "launch Forge on CI Training"
         * shortcut is preserved as pdguiForgeStartSession() for other
         * callers, but the Main Menu button no longer uses it.
         *
         * Internal module names stay forge* for code continuity;
         * "The Grid" is the unified user-facing facility (Forge and
         * Playtest are modes inside it).  Priority 2 will wire the
         * in-session Halo-Back-style mode toggle. */
        if (PdButton("The Grid", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 6;
            sysLogPrintf(LOG_NOTE, "MENU_STACK: Grid submenu OPEN (s_MenuView=6)");
        }

        /* Quit Game -- docked to bottom-right; M-Q-A opens a canonical
         * S385 confirm modal instead of the previous inline toggle. */
        {
            float quitBtnW = ImGui::CalcTextSize("Quit Game").x
                           + ImGui::GetStyle().FramePadding.x * 2.0f
                           + pdguiScale(16.0f);
            float quitBtnH = 28.0f * scale;
            /* S305: honour content inset (padR/padB) so the Quit button clears
             * the nineslice chrome border. */
            float cursorX = dialogW - padR - quitBtnW;
            float cursorY = dialogH - padB - quitBtnH;

            ImGui::SetCursorPos(ImVec2(cursorX, cursorY));

            const char *quitPopupId = "Quit Game?##mainmenu_quit";
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.1f, 0.1f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.15f, 0.15f, 0.9f));
            if (ImGui::Button("Quit Game", ImVec2(quitBtnW, quitBtnH))
                    && !ImGui::IsPopupOpen(quitPopupId)) {
                ImGui::OpenPopup(quitPopupId);
                s_QuitOpenFrame = (s32)ImGui::GetFrameCount();
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            }
            ImGui::PopStyleColor(2);

            s32 quitRes = pdguiRenderConfirmModal(
                quitPopupId,
                "Quit Game?",
                "Quit Perfect Dark? Any unsaved settings will be lost.",
                "Quit",
                &s_QuitOpenFrame);
            if (quitRes == PDGUI_CONFIRM_OK) {
                SDL_Event quitEvent;
                quitEvent.type = SDL_QUIT;
                SDL_PushEvent(&quitEvent);
            }
        }

    } else if (s_MenuView == 1) {
        /* ================================================================
         * SOLO PLAY SUB-MENU
         * Campaign missions, local combat sim, co-op, counter-op.
         * Online play is accessed from the top-level "Online Play" button.
         * ================================================================ */
        ImGui::Dummy(ImVec2(0, 4.0f * scale));

        /* Solo Missions -- campaign */
        if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
        if (PdButton("Solo Missions", ImVec2(buttonW, buttonH))) {
            pdguiSoloMissionReset(); /* F-1.2: clear stale cursor/difficulty state */
            menuhandlerMainMenuSoloMissions(MENUOP_SET, nullptr, nullptr);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Combat Simulator -- opens Room screen in solo (offline) mode */
        if (PdButton("Combat Simulator", ImVec2(buttonW, buttonH))) {
            pdguiSoloRoomOpen();
        }

        /* Co-Operative and Counter-Operative removed — no local multiplayer
         * (single local player only, constraint in CLAUDE.md). Keep code but
         * don't render the buttons. */

    } else if (s_MenuView == 2) {
        /* ================================================================
         * SETTINGS SUB-MENU
         * Navigation: B/Escape returns to top-level (handled above).
         *
         * Wrap in a constraining BeginChild so the tab content renders
         * within the body region and doesn't fall behind the PD dialog
         * chrome drawn by drawPdWindowFrame(). Without this wrapper the
         * tab content uses the parent window's full region and the
         * procedural body background (AddRectFilled in pdguiDrawPdDialog)
         * can overdraw on top of the settings widgets.
         * ================================================================ */
        /* S305: honour content inset (padX/padT/padR/padB from above) so
         * tab buttons and body don't bleed into the chrome border. */
        float contentH = dialogH - padT - padB;
        if (contentH < pdguiScale(80.0f)) contentH = pdguiScale(80.0f);
        if (ImGui::BeginChild("##main_settings_body", ImVec2(contentW, contentH),
                              ImGuiChildFlags_NavFlattened,
                              ImGuiWindowFlags_NoBackground)) {
            renderSettingsView(scale, contentH);
        }
        ImGui::EndChild();

    } else if (s_MenuView == 3) {
        /* ================================================================
         * MODDING — Modding Hub renders in pdguiModdingHubRender (overlay).
         * B-213: when the hub closes, stay on this view with a clear re-entry
         * point instead of forcing s_MenuView=0 (which felt like "Back to
         * Main Menu" after leaving Skin Editor or closing the hub).
         * ================================================================ */
        if (!pdguiModdingHubIsVisible()) {
            ImGui::Dummy(ImVec2(0, 8.0f * scale));
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.9f, 1.0f), "Modding");
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 6.0f * scale));
            ImGui::TextDisabled("Modding tools are closed.");
            ImGui::Dummy(ImVec2(0, 8.0f * scale));
            if (PdButton("Open Modding Hub", ImVec2(buttonW, buttonH * 1.15f))) {
                pdguiModdingHubShow();
                pdguiPlaySound(PDGUI_SND_SELECT);
            }
            ImGui::Dummy(ImVec2(0, 6.0f * scale));
            if (PdButton("Back", ImVec2(buttonW * 0.65f, buttonH))) {
                s_MenuView = 0;
                pdguiPlaySound(PDGUI_SND_SWIPE);
            }
        }

    } else if (s_MenuView == 4) {
        /* ================================================================
         * ONLINE PLAY
         * Join a server by connect code or direct IP.
         * After connecting, transitions to the server lobby.
         * ================================================================ */
        /* Format a unix timestamp as a compact relative-time string.
         * buf must be at least 32 bytes. Returns buf. */
        auto fmtRelTime = [](char *buf, size_t bufsz, u32 ts) -> const char * {
            if (ts == 0) { snprintf(buf, bufsz, "never"); return buf; }
            time_t now = time(NULL);
            if ((time_t)ts > now) { snprintf(buf, bufsz, "just now"); return buf; }
            long diff = (long)(now - (time_t)ts);
            if (diff < 60)             snprintf(buf, bufsz, "%lds ago", diff);
            else if (diff < 3600)      snprintf(buf, bufsz, "%ldm ago", diff / 60);
            else if (diff < 86400)     snprintf(buf, bufsz, "%ldh ago", diff / 3600);
            else                       snprintf(buf, bufsz, "%ldd ago", diff / 86400);
            return buf;
        };
        static char s_JoinCodeInput[64] = "";
        static char s_JoinStatus[128] = "";
        static ImVec4 s_JoinStatusColor = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
        static Uint32 s_LastQueryMs = 0;
#define ONLINE_PLAY_REQUERY_MS 12000

        /* Fire async ping queries when the view opens and every 12 s. */
        {
            Uint32 nowMs = SDL_GetTicks();
            if (s_ViewJustChanged || (nowMs - s_LastQueryMs) >= ONLINE_PLAY_REQUERY_MS) {
                netQueryRecentServersAsync();
                s_LastQueryMs = nowMs;
            }
        }

        /* Drain any pending ping responses each frame. */
        netPollRecentServers();

        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.13f, 1.0f), "Join Server");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 4.0f * scale));

        ImGui::Text("Enter the connect code shared by the server host:");
        ImGui::Dummy(ImVec2(0, 4.0f * scale));

        ImGui::SetNextItemWidth(buttonW);
        if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
        ImGui::InputText("##joincode", s_JoinCodeInput, sizeof(s_JoinCodeInput));

        ImGui::Dummy(ImVec2(0, 4.0f * scale));

        if (PdButton("Connect", ImVec2(buttonW, 32.0f * scale))) {
            if (s_JoinCodeInput[0]) {
                u32 ip = 0;

                /* Connect code is the ONLY accepted input.
                 * Must be exactly 4 valid words from the dictionaries.
                 * No direct IP addresses allowed -- the code is a security layer
                 * that prevents sharing raw public IPs. */
                if (connectCodeDecode(s_JoinCodeInput, &ip) == 0 && ip) {
                    /* Code validated -- resolve internally and connect.
                     * Bytes are packed little-endian (a=LSB, d=MSB) by the encoder. */
                    char addrStr[64];
                    snprintf(addrStr, sizeof(addrStr), "%u.%u.%u.%u:%u",
                        ip & 0xff, (ip >> 8) & 0xff,
                        (ip >> 16) & 0xff, (ip >> 24) & 0xff, CONNECT_DEFAULT_PORT);
                    sysLogPrintf(LOG_NOTE, "JOIN: code validated, connecting...");

                    if (netStartClientWithHolePunch(addrStr) == 0) {
                        snprintf(s_JoinStatus, sizeof(s_JoinStatus), "Connecting...");
                        s_JoinStatusColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                    } else {
                        snprintf(s_JoinStatus, sizeof(s_JoinStatus), "Server unreachable");
                        s_JoinStatusColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    }
                } else {
                    snprintf(s_JoinStatus, sizeof(s_JoinStatus), "Invalid connect code");
                    s_JoinStatusColor = ImVec4(1.0f, 0.5f, 0.2f, 1.0f);
                }
            }
        }

        if (s_JoinStatus[0]) {
            ImGui::Dummy(ImVec2(0, 4.0f * scale));
            ImGui::TextColored(s_JoinStatusColor, "%s", s_JoinStatus);
        }

        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::TextDisabled("Enter a 4-word connect code from the server host");
        ImGui::TextDisabled("Example: fat vampire running to the park");

        /* Server History */
        ImGui::Dummy(ImVec2(0, 8.0f * scale));
        ImGui::Separator();

        /* Header: title + in-flight indicator or Refresh button */
        ImGui::TextColored(pdguiVec4TitleGlow(), "Recent Servers");
        ImGui::SameLine();
        if (g_NetQueryInFlight) {
            /* Pulse the dot between yellow and white while queries are in flight. */
            float pulse = (float)(0.5 + 0.5 * ImGui::GetTime() * 4.0);
            float p = (float)(0.55 + 0.45 * sin(pulse));
            ImGui::TextColored(ImVec4(1.0f, p, 0.1f, 1.0f), " ●");
        } else {
            ImGui::SameLine(buttonW - pdguiScale(96.0f));
            if (ImGui::SmallButton("Refresh")) {
                netQueryRecentServersAsync();
                s_LastQueryMs = SDL_GetTicks();
            }
        }

        ImGui::Dummy(ImVec2(0, 4.0f * scale));
        if (g_NetNumRecentServers == 0) {
            ImGui::TextDisabled("No recent servers");
        } else {
            /* Entries are stored oldest-first; display newest first. */
            for (s32 i = g_NetNumRecentServers - 1; i >= 0; --i) {
                struct netrecentserver *srv = &g_NetRecentServers[i];

                /* Build connect code from stored addr "a.b.c.d[:port]". */
                char code[CONNECT_CODE_MAX] = "";
                {
                    u32 a = 0, b = 0, c = 0, d = 0;
                    if (sscanf(srv->addr, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
                        u32 ip = a | (b << 8) | (c << 16) | (d << 24);
                        connectCodeEncode(ip, code, sizeof(code));
                    }
                }

                /* Online/offline dot — pulsing amber while query in flight. */
                if (g_NetQueryInFlight) {
                    float pulse = (float)(0.5 + 0.5 * ImGui::GetTime() * 4.0);
                    float p = (float)(0.55 + 0.45 * sin(pulse));
                    ImGui::TextColored(ImVec4(1.0f, p, 0.1f, 1.0f), "◌");
                } else {
                    ImGui::TextColored(
                        srv->online ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f)
                                    : ImVec4(0.45f, 0.45f, 0.45f, 1.0f),
                        srv->online ? "●" : "○");
                }
                ImGui::SameLine();

                /* Clickable row — hostname (or code fallback) + player count. */
                const char *name = (srv->hostname[0] != '\0') ? srv->hostname : code;
                char rowText[256];
                if (srv->online && srv->maxclients > 0) {
                    snprintf(rowText, sizeof(rowText), "%s  [%u/%u]",
                        name, (u32)srv->numclients, (u32)srv->maxclients);
                } else {
                    snprintf(rowText, sizeof(rowText), "%s", name);
                }

                ImGui::PushID(i);
                if (ImGui::Selectable(rowText, false, ImGuiSelectableFlags_None,
                        ImVec2(buttonW - pdguiScale(36.0f), 0.0f))) {
                    if (netStartClientWithHolePunch(srv->addr) == 0) {
                        snprintf(s_JoinStatus, sizeof(s_JoinStatus), "Connecting...");
                        s_JoinStatusColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                    } else {
                        snprintf(s_JoinStatus, sizeof(s_JoinStatus), "Server unreachable");
                        s_JoinStatusColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
                    }
                }
                ImGui::PopID();

                /* Show connect code and last-seen time beneath the hostname. */
                {
                    char timebuf[32];
                    fmtRelTime(timebuf, sizeof(timebuf), srv->lastresponse);
                    if (srv->hostname[0] != '\0' && code[0] != '\0') {
                        ImGui::TextDisabled("    %s  ·  %s", code, timebuf);
                    } else {
                        ImGui::TextDisabled("    %s", timebuf);
                    }
                }

                ImGui::Dummy(ImVec2(0, pdguiScale(3.0f)));
            }
        }

    } else if (s_MenuView == 5) {
        /* ================================================================
         * PLAYER STATISTICS (M2.3)
         * ================================================================ */
        pdguiMenuStatsRender(winW, winH);
        if (!pdguiMenuStatsIsVisible()) {
            s_MenuView = 0;
        }

    } else if (s_MenuView == 6) {
        /* ================================================================
         * THE GRID SUBMENU (Priority 1, 2026-04-23)
         *
         * Map picker + variant config + Enter button.  Committing Enter
         * applies the variant into g_MatchConfig (scenario, stage, rules)
         * and hands off to pdguiForgeStartSessionOn(stagenum).  The
         * session launches and the user enters The Grid session -- Forge
         * (edit) mode by default; Playtest mode toggle lands in P2.
         *
         * Blank Map: surfaced only when GRID_BLANK_STAGE is defined.
         * See pdgui_menu_grid.h and the evening-decisions-2026-04-23
         * audit entry for why the entry is currently suppressed.
         * ================================================================ */
        renderGridSubmenu(scale, buttonW, buttonH, spacing);
    }

    /* Sound on view switches + auto-focus flag */
    s_ViewJustChanged = (s_PrevView >= 0 && s_PrevView != s_MenuView);
    if (s_ViewJustChanged) {
        pdguiPlaySound(PDGUI_SND_SWIPE);
        s_NeedsFocus = true;  /* focus first widget on next frame */
        /* S197a: only set s_ControlsNeedsInit when LEAVING the Settings view,
         * not on every view switch.  Previously this fired on every change
         * (including the 0 -> 2 transition that ENTERS Settings), causing a
         * second actionmapLoadBinds() reload on the frame after Settings
         * first rendered the Controls tab.  The initial load is already
         * triggered by the default s_ControlsNeedsInit=true at static init
         * or by the leave-side flag below. */
        if (s_PrevView == 2 && s_MenuView != 2) {
            s_ControlsNeedsInit = true;
        }
    }
    s_PrevView = s_MenuView;

    if (s_PrevSubTab >= 0 && s_PrevSubTab != s_SettingsSubTab) {
        pdguiPlaySound(PDGUI_SND_FOCUS);
        s_NeedsFocus = true;  /* focus first widget when tab changes */
        /* S197a: re-init Controls binds only when LEAVING the Controls tab.
         * (S306 shifted Controls from 2 → 3 when the Interface tab was
         * inserted, so the guard now compares against the new index.)
         * Previously fired on both leave and enter, causing a redundant
         * reload on the frame after the user clicked the Controls tab
         * (the enter-frame already reloaded via the default/view-change
         * flag). */
        if (s_PrevSubTab == 3 && s_SettingsSubTab != 3) {
            s_ControlsNeedsInit = true;
        }
    }
    s_PrevSubTab = s_SettingsSubTab;

    /* D5 Phase 2: D-pad wrapping — must be after all widgets, before End() */
    pdguiNavTickWrap();

    ImGui::End();
    s_MainMenuIsRendering = false;
    return 1;  /* Handled */
}

/* ========================================================================
 * S195 Batch 3 -- CI Options redirect
 *
 * Legacy CI Options dialogs (Controls/Display/Style/etc.) are deprecated
 * per context/designs/menu-replacement-plan.md -- settings have been
 * unified into the single Settings view built into the main menu.
 *
 * Rather than re-implementing each CI sub-screen with its own ImGui
 * renderer, we register a thin redirect renderer that:
 *
 *   1. Opens an ImGui window with the same PD title frame as the main
 *      menu, titled "Settings".
 *   2. Pre-selects the matching sub-tab (Controls/Video/etc.) by setting
 *      s_SettingsSubTab BEFORE calling renderSettingsView().
 *   3. Calls renderSettingsView() -- the user sees the exact same
 *      unified settings UI they'd see if they navigated through the main
 *      menu's Settings tab.
 *   4. Docks a Back action-bar button (using the S192 pdguiLayout
 *      primitives) that pops the CI dialog.
 *
 * This guarantees visual parity with the main menu Settings, while
 * keeping the legacy push path (any fringe caller that still pushes
 * g_CiControlOptionsMenuDialog etc.) functional.
 *
 * P2 variants (Player 2 controls/display) are DEAD -- no split-screen.
 * Their renderer shows a brief deprecated notice and auto-pops.
 *
 * Compliance:
 * - 1080p baseline via pdguiScale() everywhere.
 * - Popup scrim: pdguiPopupDarkenBehind(0.55f).
 * - Docked CTA: pdguiBeginActionBar + pdguiActionBarButton.
 * - Real Selectable-with-label, no ##hidden + AddText antipattern.
 * - Audio cues: SND_OPENDIALOG on appear, SND_KBCANCEL on Back/Esc/B.
 * ======================================================================== */

static s32 ciRedirectTargetTabForDialog(struct menudialogdef *dlg)
{
    /* Map each CI Options sub-dialog to its matching unified Settings tab.
     * S306 tab order: 0=Video 1=Interface 2=Audio 3=Controls 4=Game
     * 5=Updates 6=Debug 7=Catalog. */
    if (dlg == &g_CiControlOptionsMenuDialog)    return 3; /* Controls */
    /* g_CiControlOptionsMenuDialog2 is PAL-only — not linked in NTSC builds. */
    if (dlg == &g_CiControlStyleMenuDialog)      return 3; /* Controls */
    if (dlg == &g_CiDisplayMenuDialog)           return 0; /* Video */
    if (dlg == &g_CiOptionsViaPcMenuDialog)      return 0; /* start on Video */
    if (dlg == &g_CiOptionsViaPauseMenuDialog)   return 0; /* start on Video */
    return 0; /* safe default */
}

static s32 renderCiSettingsRedirect(struct menudialog *dialog,
                                     struct menu *menu,
                                     s32 winW, s32 winH)
{
    (void)menu;
    (void)winW;
    (void)winH;

    /* B-153: five CI Options dialogdefs route through this renderer
     * (CiOptionsViaPc, CiOptionsViaPause, CiControlOptions, CiControlStyle,
     * CiDisplay). When any of them is pushed as a nextsibling of the main
     * menu (g_CiMenuViaPauseMenuDialog's nextsibling IS g_CiOptions...), the
     * sibling gets queued for render alongside the main menu. Without this
     * guard the 0.55 darken scrim below compounds with the main menu's own
     * frame, producing the "two menus overlaid / darker background" visual
     * Mike reported after rapid close → reopen. Skip unless we're the
     * user's actual current dialog. */
    if (!menuDialogIsCurrent(dialog)) {
        return 1; /* consumed — sibling preload, not the active dialog */
    }

    /* Extract the dialog definition pointer.  struct menudialog's first
     * field is a pointer to its menudialogdef -- same pattern used by
     * pdgui_menu_warning.cpp because types.h cannot be included from C++. */
    struct menudialogdef *def = *(struct menudialogdef **)((u8 *)dialog);

    /* Scrim the whole viewport behind the modal so the user focuses
     * on the unified settings panel. */
    pdguiPopupDarkenBehind(0.55f);

    f32 scale   = pdguiScaleFactor();
    f32 dialogW = pdguiMenuWidth();
    f32 dialogH = pdguiMenuHeight();
    ImVec2 menuPos = pdguiMenuPos();
    f32 dialogX = menuPos.x;
    f32 dialogY = menuPos.y;

    ImGui::SetNextWindowPos(ImVec2(dialogX, dialogY));
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##ci_settings_redirect", nullptr, wflags)) {
        ImGui::End();
        /* S311: pool slot leak guard — if Begin fails mid-frame the
         * menuCloseDialog cascade hasn't fired yet, so drop the slot
         * defensively (idempotent). */
        menupoolReleaseDialog(menupoolDialogDef(dialog));
        return 1;
    }

    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    /* On first appearance or dialog switch: play open cue, select sub-tab. */
    static struct menudialogdef *s_LastDialog = nullptr;
    if (ImGui::IsWindowAppearing() || s_LastDialog != def) {
        ImGui::SetWindowFocus();
        s_NeedsFocus = true;
        s_SettingsSubTab = ciRedirectTargetTabForDialog(def);
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: CI Options redirect OPEN (dialog=%p subtab=%d)",
            (void *)def, (int)s_SettingsSubTab);
        s_LastDialog = def;
    }

    /* PD title frame -- same look as the main menu. */
    f32 pdTitleH = drawPdWindowFrame(dialogX, dialogY, dialogW, dialogH,
                                      "Settings");

    /* S305: content inset — clear the nineslice chrome border + breathing room. */
    float insLr = 0, insRr = 0, insTr = 0, insBr = 0;
    pdguiThemeGetContentInset(&insLr, &insRr, &insTr, &insBr);
    float breatheR = pdguiScale(8.0f);
    float padXr = pdguiScale(16.0f);
    if (insLr + breatheR > padXr) padXr = insLr + breatheR;
    float padRr = pdguiScale(16.0f);
    if (insRr + breatheR > padRr) padRr = insRr + breatheR;
    float padTr = pdTitleH + ImGui::GetStyle().WindowPadding.y + breatheR;
    if (insTr + breatheR + pdTitleH > padTr) padTr = insTr + breatheR + pdTitleH;

    ImGui::SetCursorPos(ImVec2(padXr, padTr));

    /* Compute the scrollable body region, reserving the docked action bar
     * height per the S192 primitive.  contentH must match what renderMainMenu
     * passes to renderSettingsView so the settings body looks identical. */
    f32 avail   = ImGui::GetContentRegionAvail().y;
    f32 bodyH   = pdguiBodyHeightForActionBar(avail);
    f32 contentH = bodyH;
    float bodyW = dialogW - padXr - padRr;
    if (bodyW < pdguiScale(80.0f)) bodyW = pdguiScale(80.0f);

    if (ImGui::BeginChild("##ci_settings_body",
                           ImVec2(bodyW, bodyH),
                           ImGuiChildFlags_NavFlattened,
                           ImGuiWindowFlags_NoBackground)) {
        renderSettingsView(scale, contentH);
    }
    ImGui::EndChild();

    /* Docked action bar -- Back button, always visible. */
    bool wantBack = false;
    if (pdguiBeginActionBar("##ci_settings_ab")) {
        f32 barW = ImGui::GetContentRegionAvail().x;
        if (pdguiActionBarButton("Back", 1, barW)) {
            wantBack = true;
        }
    }
    pdguiEndActionBar();

    /* S311: title X / Escape / B all back out.  pdguiConsumeTitleClose
     * fires on the X click before ImGui's own nav swallows the Escape
     * edge, so the X button closes on the first attempt. */
    bool titleCloseCi = pdguiConsumeTitleClose() != 0;
    if (!ImGui::IsWindowAppearing() &&
        (titleCloseCi || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        wantBack = true;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    if (wantBack) {
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: CI Options redirect CLOSE (dialog=%p)%s",
            (void *)def, titleCloseCi ? " [via X]" : "");
        /* S311: menuCloseDialog (invoked by menuPopDialog) releases the
         * pool slot and pops the owned ctx; no explicit ctx pop here. */
        s_LastDialog = nullptr;
        menuPopDialog();
    }

    /* D-pad wrapping must be called before End(). */
    pdguiNavTickWrap();

    ImGui::End();
    return 1;
}

static s32 renderCiDeadPlayer2(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    (void)menu;
    (void)winW;
    (void)winH;
    (void)dialog;

    /* P2 CI dialogs are DEAD -- no split-screen.  This renderer exists to
     * catch any fringe path that still pushes them, show a brief notice,
     * and auto-pop so the menu stack stays healthy.
     *
     * Batch 3 policy: rather than silently noop (which would leave a stuck
     * invisible dialog on the stack) we render a small informative modal
     * with a docked OK action bar button.  On first appearance we log and
     * start a one-frame countdown so the user can read the message. */
    pdguiPopupDarkenBehind(0.55f);

    f32 scale = pdguiScaleFactor();
    f32 dW = pdguiScale(520.0f);
    f32 dH = pdguiScale(220.0f);
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    f32 dX = (disp.x - dW) * 0.5f;
    f32 dY = (disp.y - dH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(dX, dY));
    ImGui::SetNextWindowSize(ImVec2(dW, dH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##ci_dead_p2", nullptr, wflags)) {
        ImGui::End();
        /* S311: pool slot leak guard. */
        menupoolReleaseDialog(menupoolDialogDef(dialog));
        return 1;
    }

    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: CI Player-2 dialog (deprecated; no split-screen)");
    }

    f32 pdTitleH = drawPdWindowFrame(dX, dY, dW, dH, "Not Available");
    pdguiSetCursorBelowTitle(pdTitleH);

    ImGui::TextWrapped(
        "Split-screen is not supported in this PC port.  Player-2 "
        "settings are no longer applicable.");
    ImGui::Dummy(ImVec2(0, pdguiScale(8.0f)));
    ImGui::TextDisabled("This dialog was reached via a legacy menu path.");

    /* Docked action bar with OK button. */
    f32 avail = ImGui::GetContentRegionAvail().y;
    f32 bodyH = pdguiBodyHeightForActionBar(avail);
    ImGui::Dummy(ImVec2(0, bodyH - ImGui::GetStyle().ItemSpacing.y));

    bool wantClose = false;
    if (pdguiBeginActionBar("##ci_dead_p2_ab")) {
        f32 barW = ImGui::GetContentRegionAvail().x;
        if (pdguiActionBarButton("OK", 1, barW)) {
            wantClose = true;
        }
    }
    pdguiEndActionBar();

    /* S311: title X / Escape / Enter all close the dead-P2 notice. */
    if (!ImGui::IsWindowAppearing() &&
        (pdguiConsumeTitleClose() ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
        wantClose = true;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    if (wantClose) {
        menuPopDialog();
    }

    pdguiNavTickWrap();
    ImGui::End();
    return 1;
}

/* ========================================================================
 * D5 P3 Batch 4 -- Cinema renderer (g_CinemaMenuDialog)
 *
 * Replaces the legacy MENUITEMTYPE_LIST dialog with a grouped, scrollable
 * list of unlocked cutscenes.  All group enumeration, completion gating,
 * and play-trigger side effects delegate to the existing menuhandlerCinema
 * in mainmenu.c via the cn_* shadow-struct call-through pattern.  We do
 * not duplicate getNumCompletedMissions() / g_CutsceneCountsByMission --
 * the handler owns them.
 * ======================================================================== */

static s32 s_CinemaSelectIdx = 0;
static bool s_CinemaRegistered = false;

static uintptr_t cn_handlerQuery(s32 op, uintptr_t value)
{
    struct cn_menuitem  it{};
    union  cn_handlerdata hd{};
    hd.list.value = value;
    return menuhandlerCinema(op, &it, &hd);
}

static s32 renderCinemaList(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    (void)dialog; (void)menu; (void)winW; (void)winH;

    pdguiPopupDarkenBehind(0.55f);

    f32 mw = pdguiMenuWidth() * 0.55f;
    f32 mh = pdguiMenuHeight() * 0.80f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##cinema_list", nullptr, wf)) {
        ImGui::End();
        /* S311: pool slot leak guard. */
        menupoolReleaseDialog(menupoolDialogDef(dialog));
        return 1;
    }

    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_CinemaSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    f32 titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Cinema", 1);

    /* S305: content inset — clear the nineslice chrome border + breathing room. */
    float insLc = 0, insRc = 0, insTc = 0, insBc = 0;
    pdguiThemeGetContentInset(&insLc, &insRc, &insTc, &insBc);
    float breatheC = pdguiScale(8.0f);
    float padXc = pdguiScale(16.0f);
    if (insLc + breatheC > padXc) padXc = insLc + breatheC;
    float padRc = pdguiScale(16.0f);
    if (insRc + breatheC > padRc) padRc = insRc + breatheC;
    float padTc = titleH + ImGui::GetStyle().WindowPadding.y + breatheC;
    if (insTc + breatheC + titleH > padTc) padTc = insTc + breatheC + titleH;
    ImGui::SetCursorPos(ImVec2(padXc, padTc));

    /* S311: title X / Escape / B close. */
    bool wantClose = false;
    if (!ImGui::IsWindowAppearing() &&
        (pdguiConsumeTitleClose() ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        wantClose = true;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    /* Query the legacy handler for option count + group count. */
    uintptr_t optionCount = cn_handlerQuery(MENUOP_GETOPTIONCOUNT, 0);
    uintptr_t groupCount  = cn_handlerQuery(MENUOP_GETOPTGROUPCOUNT, 0);

    /* Bound selection to valid range. */
    if (optionCount == 0) optionCount = 1; /* at least Play All */
    if (s_CinemaSelectIdx < 0) s_CinemaSelectIdx = 0;
    if ((uintptr_t)s_CinemaSelectIdx >= optionCount) s_CinemaSelectIdx = (s32)optionCount - 1;

    /* D-pad nav with wrap -- include +1 action-bar row for Back */
    const s32 totalFocusable = (s32)optionCount + 1;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_CinemaSelectIdx = (s_CinemaSelectIdx + 1) % totalFocusable;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_CinemaSelectIdx = (s_CinemaSelectIdx - 1 + totalFocusable) % totalFocusable;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }

    /* ---- Body: scrollable cutscene list ---- */
    f32 avail = ImGui::GetContentRegionAvail().y;
    f32 bodyH = pdguiBodyHeightForActionBar(avail);
    /* S305: body width respects the content inset */
    float bodyWc = mw - padXc - padRc;
    if (bodyWc < pdguiScale(80.0f)) bodyWc = pdguiScale(80.0f);

    if (ImGui::BeginChild("##cinema_body", ImVec2(bodyWc, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground
                          | ImGuiWindowFlags_AlwaysVerticalScrollbar)) {

        /* Track which group we're inside so we can print headers. */
        s32 nextGroupIndex = 0;
        uintptr_t nextGroupStart = 0;

        /* Resolve first group start */
        if ((s32)groupCount > 0) {
            nextGroupStart = cn_handlerQuery(MENUOP_GETGROUPSTARTINDEX, 0);
        }

        for (uintptr_t i = 0; i < optionCount; i++) {
            /* Emit group header when this option is the start of the next group.
             * Group 0 ("Special") starts at option 1 (since option 0 is always
             * "Play All" per the legacy handler). */
            while ((s32)nextGroupIndex < (s32)groupCount &&
                   (i == 0 || (s32)(i - 1) == (s32)nextGroupStart)) {
                if (i > 0) {
                    /* Print header for the group that starts at option (i-1) */
                    uintptr_t gname = cn_handlerQuery(MENUOP_GETOPTGROUPTEXT,
                                                      nextGroupIndex);
                    if (nextGroupIndex > 0) ImGui::Spacing();
                    ImGui::TextDisabled("%s", gname ? (const char *)gname : "");
                    ImGui::Separator();
                }
                nextGroupIndex++;
                if ((s32)nextGroupIndex < (s32)groupCount) {
                    nextGroupStart = cn_handlerQuery(MENUOP_GETGROUPSTARTINDEX,
                                                      nextGroupIndex);
                } else {
                    nextGroupStart = (uintptr_t)-1;
                }
            }

            /* Fetch option text.  Value 0 = "Play All"; 1..N = specific. */
            uintptr_t textPtr = cn_handlerQuery(MENUOP_GETOPTIONTEXT, i);
            const char *label = (textPtr ? (const char *)textPtr : "");

            bool isActive = (s_CinemaSelectIdx == (s32)i);

            ImGui::PushID((int)i);
            bool clicked = ImGui::Selectable(label, isActive,
                                              ImGuiSelectableFlags_None,
                                              ImVec2(0, pdguiScale(36.0f)));
            if (ImGui::IsItemHovered()) s_CinemaSelectIdx = (s32)i;

            bool kbConfirm = isActive &&
                ImGui::IsKeyPressed(ImGuiKey_Enter, false);

            if (clicked || kbConfirm) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* Delegate to legacy handler -- it sets g_Vars.autocutgroupcur
                 * and autocutgroupleft, then calls menuPopDialog + menuStop.
                 * S311: the embedded menuPopDialog cascades through
                 * menuCloseDialog → menupoolReleaseDialog so the pool slot +
                 * owned ctx are released without an explicit pop here. */
                cn_handlerQuery(MENUOP_SET, i);
                ImGui::PopID();
                ImGui::EndChild();
                ImGui::End();
                return 1;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    /* Docked action bar: Back */
    if (pdguiBeginActionBar("##cinema_ab")) {
        f32 barW = ImGui::GetContentRegionAvail().x;
        bool backActive = (s_CinemaSelectIdx == (s32)optionCount);
        if (pdguiActionBarButton("Back", backActive ? 1 : 0, barW)) {
            wantClose = true;
        }
        if (backActive &&
            ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            wantClose = true;
        }
    }
    pdguiEndActionBar();

    if (wantClose) {
        /* S311: menuCloseDialog cascade releases pool slot + ctx. */
        menuPopDialog();
    }

    pdguiNavTickWrap();
    ImGui::End();
    return 1;
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

void pdguiMainMenuReset(void)
{
    s_MenuView = 0;
}

void pdguiMenuMainMenuRegister(void)
{
    if (!s_RegisteredPc) {
        pdguiHotswapRegister(
            &g_CiMenuViaPcMenuDialog,
            renderMainMenu,
            "Main Menu (PC)"
        );
        s_RegisteredPc = true;
    }

    if (!s_RegisteredPause) {
        pdguiHotswapRegister(
            &g_CiMenuViaPauseMenuDialog,
            renderMainMenu,
            "Main Menu (Pause)"
        );
        s_RegisteredPause = true;
    }

    /* S195 Batch 3 -- CI Options sub-dialogs all redirect to the unified
     * Settings view.  Register once (idempotent: pdguiHotswapRegister skips
     * duplicates silently if called twice, per its contract).  These
     * registrations replace the legacy DANGER/DEFAULT type-fallback that
     * previously caught CI Options from warning.cpp. */
    static bool s_RegisteredCi = false;
    if (!s_RegisteredCi) {
        pdguiHotswapRegister(
            &g_CiOptionsViaPcMenuDialog,
            renderCiSettingsRedirect,
            "CI Options (PC -> unified Settings)"
        );
        pdguiHotswapRegister(
            &g_CiOptionsViaPauseMenuDialog,
            renderCiSettingsRedirect,
            "CI Options (Pause -> unified Settings)"
        );
        pdguiHotswapRegister(
            &g_CiControlOptionsMenuDialog,
            renderCiSettingsRedirect,
            "CI Control Options -> Settings.Controls"
        );
        /* g_CiControlOptionsMenuDialog2 is PAL-only (mainmenu.c #if
         * VERSION >= VERSION_PAL_FINAL).  Not present in NTSC builds. */
        pdguiHotswapRegister(
            &g_CiControlStyleMenuDialog,
            renderCiSettingsRedirect,
            "CI Control Style -> Settings.Controls"
        );
        pdguiHotswapRegister(
            &g_CiDisplayMenuDialog,
            renderCiSettingsRedirect,
            "CI Display -> Settings.Video"
        );

        /* P2 variants: DEAD (no split-screen).  Dedicated dead-dialog
         * renderer with auto-pop so the menu stack stays healthy. */
        pdguiHotswapRegister(
            &g_CiControlStylePlayer2MenuDialog,
            renderCiDeadPlayer2,
            "CI Control Style P2 (DEAD -- no split-screen)"
        );
        pdguiHotswapRegister(
            &g_CiDisplayPlayer2MenuDialog,
            renderCiDeadPlayer2,
            "CI Display P2 (DEAD -- no split-screen)"
        );
        pdguiHotswapRegister(
            &g_CiControlPlayer2MenuDialog,
            renderCiDeadPlayer2,
            "CI Control P2 (DEAD -- no split-screen)"
        );
        s_RegisteredCi = true;
    }

    /* S202 Batch 4 -- Cinema dialog.  Separate static guard so this is
     * independent of CI Options plumbing. */
    if (!s_CinemaRegistered) {
        pdguiHotswapRegister(
            &g_CinemaMenuDialog,
            renderCinemaList,
            "Cinema (cutscene viewer)"
        );
        s_CinemaRegistered = true;
    }

    sysLogPrintf(LOG_NOTE, "pdgui_menu_mainmenu: Registered (PC + Pause + 9 CI redirects + Cinema)");
}

} /* extern "C" */
