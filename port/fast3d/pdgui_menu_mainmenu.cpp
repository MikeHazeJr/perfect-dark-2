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

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_theme_loader.h"
#include "pdgui_theme.h"
#include "pdgui_menu_stats.h"
#include "pdgui_menu_theme_editor.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_layout.h"
#include "system.h"
#include "inputctx.h"
#include "assetcatalog.h"
#include "net/netmanifest.h"

extern "C" {
#include "pdgui_nav.h"
#include "actionmap.h"
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

/* D5 P3 Batch 4 -- Cinema dialog (cutscene viewer). */
extern struct menudialogdef g_CinemaMenuDialog;

/* Match setup init (from matchsetup.c) */
void matchConfigInit(void);

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

/* MENUOP_SET = 6 (from types.h MENUOP enum) */
#define MENUOP_SET 6

/* Player count check (for Counter-Op disabled state) */
u32 joyGetConnectedControllers(void);

/* Change agent handler */
MenuItemHandlerResult menuhandlerChangeAgent(s32 operation, struct menuitem *item, union handlerdata *data);

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

/* g_MpPlayerNum */
extern s32 g_MpPlayerNum;

/* Config save */
s32 configSave(const char *fname);

/* M0.2 Phase D: inputModes API removed — inputmodes.c deleted. */

/* Button edge glow — from pdgui_style */
void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

/* Update UI — from pdgui_menu_update.cpp */
void pdguiUpdateRenderSettingsTab(void);

/* Update channel — from updater.c */
typedef enum { UPDATE_CHANNEL_STABLE = 0, UPDATE_CHANNEL_DEV, UPDATE_CHANNEL_COUNT } update_channel_t;
update_channel_t updaterGetChannel(void);
void updaterSetChannel(update_channel_t channel);
void updaterCheckAsync(void);

/* Modding Hub UI — declared in pdgui_menu_moddinghub.cpp */
void pdguiModdingHubShow(void);
void pdguiModdingHubHide(void);
s32  pdguiModdingHubIsVisible(void);

/* Solo Room screen — open the Room screen in offline (NETMODE_NONE) mode */
void pdguiSoloRoomOpen(void);

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
static s32 s_SettingsSubTab = 0; /* 0=Video, 1=Audio, 2=Controls, 3=Game, 4=Updates, 5=Debug, 6=Catalog */
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

/* Checkbox that plays TOGGLE sound on change */
static bool PdCheckbox(const char *label, bool *v)
{
    bool changed = ImGui::Checkbox(label, v);
    if (changed) pdguiPlaySound(*v ? PDGUI_SND_TOGGLEON : PDGUI_SND_TOGGLEOFF);
    return changed;
}

/* Combo that plays TOGGLEOFF (dropdown open) sound on change */
static bool PdCombo(const char *label, int *current_item, const char *const items[], int items_count)
{
    bool changed = ImGui::Combo(label, current_item, items, items_count);
    if (changed) pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    return changed;
}

/* SliderInt that plays SUBFOCUS on change */
static bool PdSliderInt(const char *label, int *v, int v_min, int v_max, const char *format = "%d")
{
    bool changed = ImGui::SliderInt(label, v, v_min, v_max, format);
    return changed; /* sliders are continuous — don't spam sounds */
}

/* SliderFloat that doesn't spam sounds */
static bool PdSliderFloat(const char *label, float *v, float v_min, float v_max, const char *format = "%.3f")
{
    return ImGui::SliderFloat(label, v, v_min, v_max, format);
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
    }

    /* UI Chrome Style (S196) — swap the procedural dialog chrome for a
     * nineslice-based chrome mod.  The dropdown is hot-applied so users
     * see the change immediately; the selection persists to pd.ini via
     * Video.UiChromeEnabled. */
    {
        int chromeIdx = pdguiThemeGetUiChromeEnabled() ? 1 : 0;
        const char *chromeOpts[] = {
            "Procedural",
            "Classic (base-game test)",
        };
        if (PdCombo("UI Chrome Style", &chromeIdx, chromeOpts, 2)) {
            pdguiThemeSetUiChromeEnabled(chromeIdx != 0 ? 1 : 0);
            if (chromeIdx != 0) {
                pdguiSetPanelNineSlice("base:ui_chrome_frame");
                pdguiChromeSetEnabled(1);
            } else {
                pdguiChromeSetEnabled(0);
            }
            sysLogPrintf(LOG_NOTE,
                "UI.CHROME: style changed to '%s'", chromeOpts[chromeIdx]);
        }
    }

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

/* ---- Key rebinding state (M0.2 Phase D: uses actionmap instead of CK_*) ---- */

static s32 s_CaptureActive = 0;
static InputAction s_CaptureAction = ACTION_MOVE_FORWARD;
static s32 s_CaptureColumn = 0;      /* 0 = MKB, 1 = Controller */
static s32 s_CaptureBind = 0;        /* trigger slot index */
static s32 s_CaptureIsSecond = 0;

/* Bindable actions table — maps InputAction to display name. */
struct BindableAction {
    InputAction action;
    const char *name;
};

static const BindableAction s_BindableActions[] = {
    { ACTION_MOVE_FORWARD,     "Forward"       },
    { ACTION_MOVE_BACKWARD,    "Backward"      },
    { ACTION_MOVE_LEFT,        "Strafe Left"   },
    { ACTION_MOVE_RIGHT,       "Strafe Right"  },
    { ACTION_FIRE_PRIMARY,     "Fire"          },
    { ACTION_FIRE_SECONDARY,   "Aim Mode"      },
    { ACTION_FIRE_MODE,        "Fire Mode"     },
    { ACTION_RELOAD,           "Reload"        },
    { ACTION_WEAPON_NEXT,      "Next Weapon"   },
    { ACTION_WEAPON_PREV,      "Prev Weapon"   },
    { ACTION_CANCEL_USE,       "Use / Cancel"  },
    { ACTION_USE,              "Use / Accept"  },
    { ACTION_DPAD_DOWN,        "Radial Menu"   },
    { ACTION_PAUSE,            "Pause Menu"    },
    { ACTION_JUMP,             "Jump"          },
    { ACTION_CROUCH,           "Crouch"        },
    { ACTION_SPRINT,           "Sprint"        },
    { ACTION_CBUTTON_LEFT,     "C-Left"        },
    { ACTION_CBUTTON_RIGHT,    "C-Right"       },
    { ACTION_CBUTTON_UP,       "C-Up"          },
    { ACTION_CBUTTON_DOWN,     "C-Down"        },
    { ACTION_DPAD_UP,          "D-Pad Up"      },
    { ACTION_DPAD_RIGHT,       "D-Pad Right"   },
    /* ACTION_MENU_ACCEPT and ACTION_MENU_CANCEL removed — consolidated into
     * ACTION_USE and ACTION_CANCEL_USE respectively. */
};
#define NUM_BINDABLE_ACTIONS (sizeof(s_BindableActions) / sizeof(s_BindableActions[0]))

static bool isVkMKB(u32 vk)     { return (vk > 0 && vk < PD_VK_JOY_BEGIN); }
static bool isVkController(u32 vk) { return (vk >= PD_VK_JOY_BEGIN && vk < PD_VK_TOTAL_COUNT); }
static const char *getBindName(u32 vk) { return vk ? inputGetKeyName((s32)vk) : "---"; }

/* Get triggers for an action from the gameplay IMC, split by MKB vs controller */
static void getBindsByType(InputAction action, u32 mkbVKs[2], s32 mkbSlots[2], s32 *mkbCount,
                           u32 ctrlVKs[2], s32 ctrlSlots[2], s32 *ctrlCount)
{
    InputMapping *m = &g_ImcGameplay.mappings[action];
    *mkbCount = 0; *ctrlCount = 0;
    mkbVKs[0] = mkbVKs[1] = 0; ctrlVKs[0] = ctrlVKs[1] = 0;
    mkbSlots[0] = mkbSlots[1] = -1; ctrlSlots[0] = ctrlSlots[1] = -1;
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

static s32 findFreeTriggerSlot(InputAction action)
{
    InputMapping *m = &g_ImcGameplay.mappings[action];
    for (s32 i = 0; i < ACTIONMAP_MAX_TRIGGERS; i++) {
        if (m->triggers[i].vk == 0) return i;
    }
    return 0;
}

static void renderBindButton(InputAction action, const char *idSuffix, s32 captureCol,
                              s32 isSecond, u32 vk, s32 slot, s32 otherSlot,
                              bool *rowHov, bool *rowNav)
{
    char btnLabel[64];
    bool isCap = (s_CaptureActive && s_CaptureAction == action
                  && s_CaptureColumn == captureCol
                  && s_CaptureIsSecond == isSecond);
    if (isCap) {
        snprintf(btnLabel, sizeof(btnLabel), "...##%s_%d", idSuffix, (int)action);
    } else {
        snprintf(btnLabel, sizeof(btnLabel), "%s##%s_%d", getBindName(vk), idSuffix, (int)action);
    }
    if (ImGui::SmallButton(btnLabel) && !s_CaptureActive) {
        s32 useSlot = slot;
        if (useSlot < 0) {
            useSlot = findFreeTriggerSlot(action);
            if (useSlot == otherSlot) {
                InputMapping *m = &g_ImcGameplay.mappings[action];
                useSlot = -1;
                for (s32 i = 0; i < ACTIONMAP_MAX_TRIGGERS; i++) {
                    if (i != otherSlot && m->triggers[i].vk == 0) { useSlot = i; break; }
                }
                if (useSlot < 0) useSlot = (otherSlot == 0) ? 1 : 0;
            }
        }
        s_CaptureActive = 1;
        s_CaptureAction = action;
        s_CaptureColumn = captureCol;
        s_CaptureIsSecond = isSecond;
        s_CaptureBind = useSlot;
        inputClearLastKey();
        pdguiPlaySound(PDGUI_SND_SUBFOCUS);
    }
    if (ImGui::IsItemHovered()) *rowHov = true;
    if (ImGui::IsItemFocused()) *rowNav = true;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && vk != 0 && slot >= 0) {
        actionmapBind(&g_ImcGameplay, 0, action, slot, 0);
        actionmapSaveBinds();
        configSave("pd.ini");
        pdguiPlaySound(PDGUI_SND_TOGGLEOFF);
    }
}

static const char *getActionName(InputAction action)
{
    for (u32 i = 0; i < NUM_BINDABLE_ACTIONS; i++) {
        if (s_BindableActions[i].action == action) return s_BindableActions[i].name;
    }
    return "???";
}

/* Sub-tab index within Controls: 0 = Keyboard & Mouse, 1 = Controller */
static s32 s_ControlsSubTab = 0;

/* Track whether we need to reload binds on Controls tab entry */
static bool s_ControlsNeedsInit = true;

/* Shared capture-mode handler — called at the top of each sub-tab that uses it. */
static void handleCaptureInput(void)
{
    if (!s_CaptureActive) return;

    s32 newKey = inputGetLastKey();
    if (newKey == PD_VK_ESCAPE) {
        s_CaptureActive = 0;
        inputClearLastKey();
    } else if (newKey > 0) {
        bool valid = (s_CaptureColumn == 0 && isVkMKB((u32)newKey))
                  || (s_CaptureColumn == 1 && isVkController((u32)newKey));
        if (valid) {
            actionmapBind(&g_ImcGameplay, 0, s_CaptureAction, s_CaptureBind, (u32)newKey);
            actionmapSaveBinds();
            configSave("pd.ini");
            pdguiPlaySound(PDGUI_SND_SELECT);
        } else {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        s_CaptureActive = 0;
        inputClearLastKey();
    }
}

/* Render a 3-column bind table for one device type (MKB or Controller).
 * filterCol: 0 = MKB, 1 = Controller. */
static void renderBindTable(s32 filterCol, const char *tableId)
{
    /* Capture banner */
    if (s_CaptureActive && s_CaptureColumn == filterCol) {
        const char *typeStr = (filterCol == 0) ? "keyboard/mouse" : "controller";
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "Press a %s key for \"%s\" (Esc to cancel)",
            typeStr, getActionName(s_CaptureAction));
        ImGui::Spacing();
    }

    ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerV
                                | ImGuiTableFlags_RowBg
                                | ImGuiTableFlags_SizingStretchProp
                                | ImGuiTableFlags_PadOuterX;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 1.0f));

    if (ImGui::BeginTable(tableId, 3, tableFlags)) {
        ImGui::TableSetupColumn("Action",  ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Bind 1",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Bind 2",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        for (u32 row = 0; row < NUM_BINDABLE_ACTIONS; row++) {
            InputAction action = s_BindableActions[row].action;
            const char *actionName = s_BindableActions[row].name;

            u32 mkbVKs[2], ctrlVKs[2];
            s32 mkbSlots[2], ctrlSlots[2];
            s32 mkbCount, ctrlCount;
            getBindsByType(action, mkbVKs, mkbSlots, &mkbCount,
                           ctrlVKs, ctrlSlots, &ctrlCount);

            u32 *vks    = (filterCol == 0) ? mkbVKs    : ctrlVKs;
            s32 *slots  = (filterCol == 0) ? mkbSlots  : ctrlSlots;
            const char *prefix = (filterCol == 0) ? "mkb" : "ctrl";

            ImGui::TableNextRow();
            bool rowHovered = false;
            bool rowNavFocus = false;

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(actionName);

            char id1[16], id2[16];
            snprintf(id1, sizeof(id1), "%s1", prefix);
            snprintf(id2, sizeof(id2), "%s2", prefix);

            ImGui::TableSetColumnIndex(1);
            renderBindButton(action, id1, filterCol, 0, vks[0],
                             (slots[0] >= 0) ? slots[0] : findFreeTriggerSlot(action),
                             slots[1], &rowHovered, &rowNavFocus);

            ImGui::TableSetColumnIndex(2);
            renderBindButton(action, id2, filterCol, 1, vks[1], slots[1], slots[0],
                             &rowHovered, &rowNavFocus);

            if (s_CaptureActive && s_CaptureAction == action && s_CaptureColumn == filterCol) {
                rowNavFocus = true;
            }
            if (rowHovered || rowNavFocus) {
                ImU32 hlColor = rowNavFocus
                    ? IM_COL32(80, 120, 200, 80)
                    : IM_COL32(200, 200, 255, 40);
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, hlColor);
            }
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar(3);

    ImGui::Spacing();
    ImGui::TextDisabled("Click to rebind. Right-click to clear. Esc to cancel.");
}

static void renderSettingsControls(float scale)
{
    /* Load binds from pd.ini when entering the Controls tab so the UI
     * reflects the current saved state (not stale in-memory mappings). */
    if (s_ControlsNeedsInit) {
        actionmapLoadBinds();
        s_ControlsNeedsInit = false;
    }

    /* Process any active key capture regardless of which sub-tab is showing */
    handleCaptureInput();

    if (ImGui::BeginTabBar("##controls_tabs")) {

        /* ======== Tab 1: Keyboard & Mouse ======== */
        if (ImGui::BeginTabItem("Keyboard & Mouse")) {
            s_ControlsSubTab = 0;
            /* Cancel controller capture if we switched tabs */
            if (s_CaptureActive && s_CaptureColumn == 1) {
                s_CaptureActive = 0;
                inputClearLastKey();
            }

            ImGui::Spacing();

            /* ---- Mouse Settings ---- */
            ImGui::TextDisabled("Mouse");
            ImGui::Separator();

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
                if (PdCheckbox("Invert Y", &invertY)) {
                    optionsSetForwardPitch(0, invertY ? 0 : 1);
                }
            }

            ImGui::Separator();

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

            ImGui::Spacing();
            ImGui::Spacing();

            /* ---- MKB Bindings ---- */
            ImGui::TextDisabled("Key Bindings");
            ImGui::Separator();

            renderBindTable(0, "##mkb_binds");

            ImGui::Spacing();
            if (PdButton("Reset Keyboard & Mouse to Defaults")) {
                /* Clear only MKB triggers from gameplay IMC, re-apply defaults */
                for (s32 a = 0; a < ACTION_COUNT; a++) {
                    if (!g_ImcGameplay.has_mapping[a]) continue;
                    InputMapping *m = &g_ImcGameplay.mappings[a];
                    s32 write = 0;
                    for (s32 ti = 0; ti < m->num_triggers; ti++) {
                        u32 vk = m->triggers[ti].vk;
                        if (vk > 0 && isVkController(vk)) {
                            m->triggers[write++] = m->triggers[ti];
                        } else {
                            m->triggers[ti].vk = 0;
                        }
                    }
                    m->num_triggers = write;
                }
                actionmapSetDefaults(&g_ImcGameplay, 0);
                actionmapSaveBinds();
                configSave("pd.ini");
            }

            ImGui::EndTabItem();
        }

        /* ======== Tab 2: Controller ======== */
        if (ImGui::BeginTabItem("Controller")) {
            s_ControlsSubTab = 1;
            /* Cancel MKB capture if we switched tabs */
            if (s_CaptureActive && s_CaptureColumn == 0) {
                s_CaptureActive = 0;
                inputClearLastKey();
            }

            ImGui::Spacing();

            /* ---- Controller Settings ---- */
            ImGui::TextDisabled("Controller");
            ImGui::Separator();

            {
                f32 sens = actionmapGetStickSensitivity();
                if (PdSliderFloat("Stick Sensitivity", &sens, 0.1f, 3.0f, "%.2f")) {
                    actionmapSetStickSensitivity(sens);
                    configSave("pd.ini");
                }
            }

            {
                f32 dz = actionmapGetStickDeadzone();
                if (PdSliderFloat("Stick Deadzone", &dz, 0.0f, 0.5f, "%.2f")) {
                    actionmapSetStickDeadzone(dz);
                    configSave("pd.ini");
                }
            }

            {
                bool invertY = actionmapGetStickInvertY() != 0;
                if (PdCheckbox("Invert Y (Stick)", &invertY)) {
                    actionmapSetStickInvertY(invertY ? 1 : 0);
                    configSave("pd.ini");
                }
            }

            {
                s32 swapped = actionmapGetSwapSticks();
                bool swap = (swapped != 0);
                if (PdCheckbox("Swap Sticks", &swap)) {
                    actionmapSetSwapSticks(swap ? 1 : 0);
                    /* Keep legacy input.c in sync for stickCButtons */
                    inputControllerSetSticksSwapped(0, swap ? 1 : 0);
                    configSave("pd.ini");
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            /* ---- Controller Bindings ---- */
            ImGui::TextDisabled("Button Bindings");
            ImGui::Separator();

            renderBindTable(1, "##ctrl_binds");

            ImGui::Spacing();
            if (PdButton("Reset Controller to Defaults")) {
                /* Clear only controller triggers from gameplay IMC, re-apply defaults */
                for (s32 a = 0; a < ACTION_COUNT; a++) {
                    if (!g_ImcGameplay.has_mapping[a]) continue;
                    InputMapping *m = &g_ImcGameplay.mappings[a];
                    s32 write = 0;
                    for (s32 ti = 0; ti < m->num_triggers; ti++) {
                        u32 vk = m->triggers[ti].vk;
                        if (vk > 0 && isVkMKB(vk)) {
                            m->triggers[write++] = m->triggers[ti];
                        } else {
                            m->triggers[ti].vk = 0;
                        }
                    }
                    m->num_triggers = write;
                }
                actionmapSetDefaults(&g_ImcGameplay, 0);
                actionmapSaveBinds();
                configSave("pd.ini");
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    /* ---- Save Controls button ---- */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (PdButton("Save Controls")) {
        actionmapSaveBinds();
        configSave("pd.ini");
        pdguiPlaySound(PDGUI_SND_SELECT);
    }
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

    /* Update Channel */
    {
        int ch = (int)updaterGetChannel();
        const char *chOpts[] = { "Stable", "Dev / Test" };
        if (PdCombo("Update Channel", &ch, chOpts, 2)) {
            updaterSetChannel((update_channel_t)ch);
            updaterCheckAsync();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Stable: only show stable releases in the update notification\n"
                "Dev / Test: also show pre-release and dev builds");
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

/* B-124 pattern: true if renderMainMenu pushed g_CtxImGuiMenu itself.
 * Only pop it on close if we pushed it — prevents unintended double-pop
 * when another system already had the context active. */
static bool s_MainMenuPushedCtx = false;

/* Helper: draw PD dialog window frame + title, return content start Y */
static float drawPdWindowFrame(float dialogX, float dialogY, float dialogW,
                                float dialogH, const char *title)
{
    float pdTitleH = dialogH * 0.08f;
    if (pdTitleH < 20.0f) pdTitleH = 20.0f;
    if (pdTitleH > 32.0f) pdTitleH = 32.0f;

    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, title, 1);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                      dialogW - 16.0f, pdTitleH - 4.0f);
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    dl->AddText(ImVec2(dialogX + 10.0f,
                       dialogY + (pdTitleH - titleSize.y) * 0.5f),
                pdguiPalImU32(PDPAL_TITLEFG, 255), title);

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

    /* ------ Theme Selector ------ */
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "UI Theme");
    ImGui::Separator();
    ImGui::Spacing();

    s32 currentPal = pdguiGetPalette();

    for (int i = 0; i < PDGUI_NUM_THEMES; i++) {
        bool selected = (i == currentPal);

        /* Tint each button to preview the theme it represents */
        ImVec4 btnCol = s_ThemeAccentColors[i];
        ImVec4 btnHover = ImVec4(
            btnCol.x + 0.15f, btnCol.y + 0.15f, btnCol.z + 0.15f, 0.95f);
        ImVec4 btnActive = ImVec4(
            btnCol.x + 0.25f, btnCol.y + 0.25f, btnCol.z + 0.25f, 1.0f);

        if (selected) {
            /* Brighten selected button and add a visible border */
            btnCol.x += 0.12f; btnCol.y += 0.12f; btnCol.z += 0.12f;
            btnCol.w = 1.0f;
            ImGui::PushStyleColor(ImGuiCol_Border, s_ThemeTextColors[i]);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f * scale);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, btnActive);
        ImGui::PushStyleColor(ImGuiCol_Text, s_ThemeTextColors[i]);

        if (ImGui::Button(s_ThemeNames[i], ImVec2(btnW, btnH))) {
            /* P5: Use catalog-backed theme system instead of raw palette index.
             * This persists the selection via Theme.ActiveTheme in pd.ini. */
            const char *themeId = pdguiThemePaletteIndexToId(i);
            if (themeId) {
                pdguiThemeLoadFromCatalog(themeId);
            }
            configSave("pd.ini");
        }

        ImGui::PopStyleColor(4); /* Text, Active, Hovered, Button */

        if (selected) {
            ImGui::PopStyleVar();   /* FrameBorderSize */
            ImGui::PopStyleColor(); /* Border */
        }

        /* 3 buttons per row */
        if (i % 3 != 2 && i + 1 < PDGUI_NUM_THEMES) {
            ImGui::SameLine();
        }
    }

    ImGui::Spacing();

    /* P5: Theme Editor button */
    if (ImGui::Button("Theme Editor...", ImVec2(btnW * 2.0f, btnH))) {
        pdguiThemeEditorShow();
    }

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
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "D5.0a Texture Bridge Spike");
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
        ImGui::TableSetupColumn("NetHash",  ImGuiTableColumnFlags_WidthFixed,   80.0f * scale);
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
                            case 4: cmp = (a->net_hash < b->net_hash) ? -1 : (a->net_hash > b->net_hash) ? 1 : 0; break;
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

            /* Net hash */
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("%08X", e->net_hash);

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
                ImGui::TableSetupColumn("NetHash",  ImGuiTableColumnFlags_WidthFixed,   80.0f * scale);
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
                                    case 3: cmp = (a->net_hash < b->net_hash) ? -1 : (a->net_hash > b->net_hash) ? 1 : 0; break;
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
                    ImGui::TextDisabled("%08X", me->net_hash);
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
     * for ONE frame after a bumper press, not continuously. */
    static s32 s_BumperPendingTab = -1; /* -1 = no pending switch */

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
        s_SettingsSubTab--;
        if (s_SettingsSubTab < 0) s_SettingsSubTab = 6;
        s_BumperPendingTab = s_SettingsSubTab;
        s_NeedsFocus = true;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
        s_SettingsSubTab++;
        if (s_SettingsSubTab > 6) s_SettingsSubTab = 0;
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

        if (ImGui::BeginTabItem("Audio", nullptr, selFlag1)) {
            s_SettingsSubTab = 1;
            ImGui::BeginChild("##settings_scroll_a", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsAudio(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Controls", nullptr, selFlag2)) {
            s_SettingsSubTab = 2;
            ImGui::BeginChild("##settings_scroll_c", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsControls(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Game", nullptr, selFlag3)) {
            s_SettingsSubTab = 3;
            ImGui::BeginChild("##settings_scroll_g", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsGame(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Updates", nullptr, selFlag4)) {
            s_SettingsSubTab = 4;
            ImGui::BeginChild("##settings_scroll_u", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            pdguiUpdateRenderSettingsTab();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Debug", nullptr, selFlag5)) {
            s_SettingsSubTab = 5;
            ImGui::BeginChild("##settings_scroll_d", ImVec2(0, 0),
                              ImGuiChildFlags_NavFlattened);
            if (ImGui::IsWindowAppearing()) ImGui::SetScrollY(0);
            if (s_NeedsFocus) { ImGui::SetKeyboardFocusHere(0); s_NeedsFocus = false; }
            renderSettingsDebug(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Catalog", nullptr, selFlag6)) {
            s_SettingsSubTab = 6;
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

    /* Bumper hint at bottom */
    ImGui::TextDisabled("LB / RB to switch tabs");
}

static s32 renderMainMenu(struct menudialog *dialog,
                           struct menu *menu,
                           s32 winW, s32 winH)
{
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
        return 1;
    }

    /* Auto-possess: give this window nav focus ONLY on first appearance.
     * Must be AFTER Begin(). Using SetWindowFocus() instead of
     * SetNextWindowFocus() to avoid stealing focus from child popups. */
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_MenuView = 0; /* Always open to main menu */
        s_NeedsFocus = true;
        /* B-124 pattern: push g_CtxImGuiMenu so that:
         *   (a) Mouse mode is set to absolute/visible (Bug 3)
         *   (b) pdguiIsActive() returns 1, blocking gameplay input (Bug 4)
         *   (c) push_tick is set, enabling inputCtxShouldSuppressKey 100ms
         *       grace period to prevent the opening key from immediately
         *       closing the menu (Bugs 1 & 2)
         * Track ownership — only pop it on close if we pushed it here. */
        if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPush(&g_CtxImGuiMenu);
            s_MainMenuPushedCtx = true;
        } else {
            s_MainMenuPushedCtx = false;
        }
        sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu OPEN");
    }

    /* Determine title based on current view */
    const char *windowTitle = "Perfect Dark";
    if (s_MenuView == 1) windowTitle = "Solo Play";
    else if (s_MenuView == 2) windowTitle = "Settings";
    else if (s_MenuView == 3) windowTitle = "Modding";
    else if (s_MenuView == 4) windowTitle = "Online Play";
    else if (s_MenuView == 5) windowTitle = "Player Statistics";

    float pdTitleH = drawPdWindowFrame(dialogX, dialogY, dialogW, dialogH, windowTitle);

    /* Offset content below the PD title bar */
    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float buttonH = 40.0f * scale;
    float buttonW = -1.0f; /* full width */
    float spacing = 6.0f * scale;

    /* B button / Escape navigation:
     * Sub-views (Play, Settings) -> back to top-level
     * Top-level -> close menu entirely (return to CI free-roam)
     *
     * Guard: skip on the frame the window first appears. When the user
     * presses B/Escape to close the menu and then reopens it, ImGui's
     * key state can still report IsKeyPressed=true on the first frame,
     * which would immediately close the menu again. */
    if (!ImGui::IsWindowAppearing() &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        if (s_MenuView != 0) {
            if (s_MenuView == 2) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — settings CLOSE (view 2->0)");
            } else if (s_MenuView == 3) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — modding hub CLOSE (view 3->0)");
                pdguiModdingHubHide();
            } else if (s_MenuView == 4) {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — online play CLOSE (view 4->0)");
            } else if (s_MenuView == 5) {
                pdguiMenuStatsHide();
            } else {
                sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu ESC — sub-view %d -> 0", s_MenuView);
            }
            s_MenuView = 0;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        } else {
            /* At top-level: close the menu, return to Carrington Institute */
            sysLogPrintf(LOG_NOTE, "MENU_IMGUI: main menu CLOSE via ESC/B (top-level -> CI free-roam)");
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            /* Pop the input context unconditionally if active. The previous
             * s_MainMenuPushedCtx ownership guard caused a leak: if
             * IsWindowAppearing fired twice (legacy dialog re-push, ImGui
             * visibility cycle), the second appearing saw the context already
             * active and set s_MainMenuPushedCtx=false, so the close handler
             * skipped the pop. All other menus (solomission, endscreen,
             * bridge) use the unconditional pattern. */
            if (inputCtxIsActive(&g_CtxImGuiMenu)) {
                inputCtxPopDeferred(&g_CtxImGuiMenu);
            }
            s_MainMenuPushedCtx = false;

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
            menuPopDialog();
        }
    }

    /* LB/RB: cycle through top-level sub-views (1=Solo, 2=Settings, 3=Modding, 4=Online).
     * From view 0 (hub), LB/RB enter the first/last sub-view.
     * Wraps around: view 1 ← LB → view 4, view 4 → RB → view 1. */
    if (!ImGui::IsWindowAppearing()) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
            if (s_MenuView <= 1) {
                s_MenuView = 4;
            } else {
                s_MenuView--;
            }
            s_NeedsFocus = true;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
            if (s_MenuView >= 4 || s_MenuView == 0) {
                s_MenuView = 1;
            } else {
                s_MenuView++;
            }
            s_NeedsFocus = true;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
    }

    if (s_MenuView == 0) {
        /* ================================================================
         * TOP LEVEL: Solo Play / Online Play / Change Agent / Settings
         * Quit Game docked to bottom-right with confirmation.
         * ================================================================ */
        static bool s_QuitConfirm = false;

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

        /* Stats -- opens the Stats Viewer (view 5) */
        if (PdButton("Stats", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 5;
            pdguiMenuStatsShow();
        }

        /* Quit Game -- docked to bottom-right with confirmation */
        {
            /* Width sized to fit the widest label ("Confirm Quit") so both states match */
            float quitBtnW = ImGui::CalcTextSize("Confirm Quit").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            float quitBtnH = 28.0f * scale;
            float margin = 4.0f * scale;
            /* Cursor pos is relative to window origin; subtract padding + margin so
               the button right edge sits margin pixels inside the content clip rect */
            float cursorX = dialogW - ImGui::GetStyle().WindowPadding.x - quitBtnW - margin;
            float cursorY = dialogH - ImGui::GetStyle().WindowPadding.y - quitBtnH - margin;

            ImGui::SetCursorPos(ImVec2(cursorX, cursorY));

            if (!s_QuitConfirm) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.1f, 0.1f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5f, 0.15f, 0.15f, 0.9f));
                if (ImGui::Button("Quit Game", ImVec2(quitBtnW, quitBtnH))) {
                    s_QuitConfirm = true;
                }
                ImGui::PopStyleColor(2);
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
                if (ImGui::Button("Confirm Quit", ImVec2(quitBtnW, quitBtnH))) {
                    SDL_Event quitEvent;
                    quitEvent.type = SDL_QUIT;
                    SDL_PushEvent(&quitEvent);
                }
                ImGui::PopStyleColor(2);

                ImGui::SetCursorPos(ImVec2(cursorX - quitBtnW * 0.7f - 8.0f * scale, cursorY));
                if (ImGui::Button("Cancel", ImVec2(quitBtnW * 0.7f, quitBtnH))) {
                    s_QuitConfirm = false;
                }
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
            menuhandlerMainMenuSoloMissions(MENUOP_SET, nullptr, nullptr);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Combat Simulator -- opens Room screen in solo (offline) mode */
        if (PdButton("Combat Simulator", ImVec2(buttonW, buttonH))) {
            pdguiSoloRoomOpen();
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Co-Operative -- local co-op campaign */
        if (PdButton("Co-Operative", ImVec2(buttonW, buttonH))) {
            menuhandlerMainMenuCooperative(MENUOP_SET, nullptr, nullptr);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Counter-Operative -- requires 2 controllers */
        {
            bool disabled = ((joyGetConnectedControllers() & ~0x1) == 0);
            if (disabled) ImGui::BeginDisabled();

            if (PdButton("Counter-Operative", ImVec2(buttonW, buttonH))) {
                menuhandlerMainMenuCounterOperative(MENUOP_SET, nullptr, nullptr);
            }

            if (disabled) ImGui::EndDisabled();
        }

    } else if (s_MenuView == 2) {
        /* ================================================================
         * SETTINGS SUB-MENU
         * Navigation: B/Escape returns to top-level (handled above).
         * ================================================================ */
        float contentH = dialogH - pdTitleH - 40.0f * scale;
        renderSettingsView(scale, contentH);

    } else if (s_MenuView == 3) {
        /* ================================================================
         * MODDING HUB
         * ================================================================ */
        if (!pdguiModdingHubIsVisible()) {
            s_MenuView = 0;
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
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Recent Servers");
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
        /* S197a: re-init Controls binds only when LEAVING the Controls tab
         * (prev==2, new!=2).  Previously fired on both leave and enter,
         * causing a redundant reload on the frame after the user clicked
         * the Controls tab (the enter-frame already reloaded via the
         * default/view-change flag). */
        if (s_PrevSubTab == 2 && s_SettingsSubTab != 2) {
            s_ControlsNeedsInit = true;
        }
    }
    s_PrevSubTab = s_SettingsSubTab;

    /* D5 Phase 2: D-pad wrapping — must be after all widgets, before End() */
    pdguiNavTickWrap();

    ImGui::End();
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
     * Sub-tab indices match the numbering in s_SettingsSubTab:
     *   0 = Video, 1 = Audio, 2 = Controls, 3 = Game,
     *   4 = Updates, 5 = Debug, 6 = Catalog. */
    if (dlg == &g_CiControlOptionsMenuDialog)    return 2; /* Controls */
    /* g_CiControlOptionsMenuDialog2 is PAL-only — not linked in NTSC builds. */
    if (dlg == &g_CiControlStyleMenuDialog)      return 2; /* Controls */
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
        return 1;
    }

    /* On first appearance: play open cue, force input context, select the
     * pre-determined sub-tab for this CI dialog. */
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

        /* Safety: ensure the ImGui menu input context is active so mouse
         * mode flips to visible and gameplay input is blocked. */
        if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPush(&g_CtxImGuiMenu);
        }
    }

    /* PD title frame -- same look as the main menu. */
    f32 pdTitleH = drawPdWindowFrame(dialogX, dialogY, dialogW, dialogH,
                                      "Settings");
    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    /* Compute the scrollable body region, reserving the docked action bar
     * height per the S192 primitive.  contentH must match what renderMainMenu
     * passes to renderSettingsView so the settings body looks identical. */
    f32 avail   = ImGui::GetContentRegionAvail().y;
    f32 bodyH   = pdguiBodyHeightForActionBar(avail);
    f32 contentH = bodyH;

    if (ImGui::BeginChild("##ci_settings_body",
                           ImVec2(0.0f, bodyH), false,
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

    /* B / Escape also backs out. */
    if (!ImGui::IsWindowAppearing() &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        wantBack = true;
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
    }

    if (wantBack) {
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: CI Options redirect CLOSE (dialog=%p)",
            (void *)def);
        /* Pop the input context on close so gameplay mouse mode restores. */
        if (inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
        }
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
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        sysLogPrintf(LOG_NOTE,
            "MENU_IMGUI: CI Player-2 dialog (deprecated; no split-screen)");
    }

    f32 pdTitleH = drawPdWindowFrame(dX, dY, dW, dH, "Not Available");
    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

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

    if (!ImGui::IsWindowAppearing() &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
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
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_CinemaSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
        if (!inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPush(&g_CtxImGuiMenu);
        }
    }

    f32 titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Cinema", 1);
    ImGui::SetCursorPosY(titleH + ImGui::GetStyle().WindowPadding.y);

    /* B / Escape closes. */
    bool wantClose = false;
    if (!ImGui::IsWindowAppearing() &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
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
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
        ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_CinemaSelectIdx = (s_CinemaSelectIdx + 1) % totalFocusable;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
        ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_CinemaSelectIdx = (s_CinemaSelectIdx - 1 + totalFocusable) % totalFocusable;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }

    /* ---- Body: scrollable cutscene list ---- */
    f32 avail = ImGui::GetContentRegionAvail().y;
    f32 bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##cinema_body", ImVec2(0, bodyH), false,
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
                (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Enter, false));

            if (clicked || kbConfirm) {
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* Delegate to legacy handler -- it sets g_Vars.autocutgroupcur
                 * and autocutgroupleft, then calls menuPopDialog + menuStop. */
                cn_handlerQuery(MENUOP_SET, i);
                /* menuhandlerCinema already popped + stopped the menu for us.
                 * Clean up our input context on the way out. */
                if (inputCtxIsActive(&g_CtxImGuiMenu)) {
                    inputCtxPopDeferred(&g_CtxImGuiMenu);
                }
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
            (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
             ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
            wantClose = true;
        }
    }
    pdguiEndActionBar();

    if (wantClose) {
        if (inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
        }
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
