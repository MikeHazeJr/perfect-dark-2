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
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_audio.h"
#include "system.h"

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

/* Play target dialogs */
extern struct menudialogdef g_SelectMissionMenuDialog;
extern struct menudialogdef g_CombatSimulatorMenuDialog;
extern struct menudialogdef g_NetMenuDialog;
extern struct menudialogdef g_ChangeAgentMenuDialog;

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

/* Audio API */
s32 optionsGetMusicVolume(void);
void optionsSetMusicVolume(s32 vol);
void sndSetSfxVolume(s32 vol);

/* g_SfxVolume is an extern s16 but the VOLUME macro scales it.
 * We'll call the handlers via MENUOP_GET pattern instead. */

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
extern s32 g_HudCenter;
extern f32 g_ViShakeIntensityMult;

/* Mouse API — from port/include/input.h */
s32 inputMouseIsEnabled(void);
void inputMouseEnable(s32 enabled);
void inputMouseGetSpeed(f32 *x, f32 *y);
void inputMouseSetSpeed(f32 x, f32 y);
s32 inputGetMouseLockMode(void);
void inputSetMouseLockMode(s32 mode);

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

} /* extern "C" */

/* ========================================================================
 * State
 * ======================================================================== */

static bool s_RegisteredPc = false;
static bool s_RegisteredPause = false;
static s32 s_SettingsSubTab = 0; /* 0=Video, 1=Audio, 2=Controls, 3=Game */
static s32 s_PrevView = -1;     /* Previous menu view, for sound on switch */
static s32 s_PrevSubTab = -1;

/* ========================================================================
 * Sound-playing widget wrappers
 * ======================================================================== */

/* Button that plays SELECT sound on click */
static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0,0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
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

    bool centerWin = videoGetCenterWindow() != 0;
    if (PdCheckbox("Center Window", &centerWin)) {
        videoSetCenterWindow(centerWin ? 1 : 0);
    }

    /* Anti-aliasing */
    {
        int msaa = videoGetMSAA();
        int msaaIdx = 0;
        if (msaa >= 16) msaaIdx = 4;
        else if (msaa >= 8) msaaIdx = 3;
        else if (msaa >= 4) msaaIdx = 2;
        else if (msaa >= 2) msaaIdx = 1;

        const char *msaaOpts[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA" };
        if (PdCombo("Anti-aliasing", &msaaIdx, msaaOpts, 5)) {
            videoSetMSAA(1 << msaaIdx);
        }
    }

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

    ImGui::Separator();

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
}

static void renderSettingsAudio(float scale)
{
    /* Sound and Music volumes — we call the handlers indirectly.
     * The original uses slider values 0-65535 (u16 range) for sfx
     * and 0-65535 for music. We'll use a 0-100 scale and map. */

    ImGui::TextDisabled("Volume Controls");
    ImGui::Separator();

    {
        int musicVol = optionsGetMusicVolume();
        /* Music volume is 0 to 0x7FFF range in PD */
        float musicPct = (float)musicVol / 327.67f; /* 0x7FFF/100 */
        if (PdSliderFloat("Music", &musicPct, 0.0f, 100.0f, "%.0f%%")) {
            optionsSetMusicVolume((s32)(musicPct * 327.67f));
        }
    }

    ImGui::Separator();

    bool disableMpDeath = g_MusicDisableMpDeath != 0;
    if (PdCheckbox("Disable MP Death Music", &disableMpDeath)) {
        g_MusicDisableMpDeath = disableMpDeath ? 1 : 0;
    }
}

static void renderSettingsControls(float scale)
{
    ImGui::TextDisabled("Mouse");
    ImGui::Separator();

    bool mouseEnabled = inputMouseIsEnabled() != 0;
    if (PdCheckbox("Mouse Enabled", &mouseEnabled)) {
        inputMouseEnable(mouseEnabled ? 1 : 0);
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

    bool menuMouse = g_MenuMouseControl != 0;
    if (PdCheckbox("Mouse Menu Navigation", &menuMouse)) {
        g_MenuMouseControl = menuMouse ? 1 : 0;
    }

    ImGui::Separator();

    {
        f32 mx, my;
        inputMouseGetSpeed(&mx, &my);
        if (PdSliderFloat("Mouse Speed X", &mx, 0.0f, 10.0f, "%.2f")) {
            inputMouseSetSpeed(mx, my);
        }
        inputMouseGetSpeed(&mx, &my);
        if (PdSliderFloat("Mouse Speed Y", &my, 0.0f, 10.0f, "%.2f")) {
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
}

/* ========================================================================
 * ImGui Render Callback
 * ======================================================================== */

/* Menu view state: 0 = top-level (Play/Settings/Quit), 1 = Play, 2 = Settings */
static s32 s_MenuView = 0;

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
                IM_COL32(255, 255, 255, 255), title);

    return pdTitleH;
}

/* Render the Settings sub-view with LB/RB bumper tab switching */
static void renderSettingsView(float scale, float contentH)
{
    /* LB/RB bumper handling: use a pending flag so SetSelected only fires
     * for ONE frame after a bumper press, not continuously. */
    static s32 s_BumperPendingTab = -1; /* -1 = no pending switch */

    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) {
        s_SettingsSubTab--;
        if (s_SettingsSubTab < 0) s_SettingsSubTab = 3;
        s_BumperPendingTab = s_SettingsSubTab;
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) {
        s_SettingsSubTab++;
        if (s_SettingsSubTab > 3) s_SettingsSubTab = 0;
        s_BumperPendingTab = s_SettingsSubTab;
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
        s_BumperPendingTab = -1; /* Clear after consuming */

        if (ImGui::BeginTabItem("Video", nullptr, selFlag0)) {
            s_SettingsSubTab = 0;
            ImGui::BeginChild("##settings_scroll", ImVec2(0, 0), false);
            renderSettingsVideo(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Audio", nullptr, selFlag1)) {
            s_SettingsSubTab = 1;
            ImGui::BeginChild("##settings_scroll", ImVec2(0, 0), false);
            renderSettingsAudio(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Controls", nullptr, selFlag2)) {
            s_SettingsSubTab = 2;
            ImGui::BeginChild("##settings_scroll", ImVec2(0, 0), false);
            renderSettingsControls(scale);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Game", nullptr, selFlag3)) {
            s_SettingsSubTab = 3;
            ImGui::BeginChild("##settings_scroll", ImVec2(0, 0), false);
            renderSettingsGame(scale);
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
    float scale = (float)winH / 480.0f;
    float dialogW = 520.0f * scale;
    float dialogH = 400.0f * scale;
    float dialogX = ((float)winW - dialogW) * 0.5f;
    float dialogY = ((float)winH - dialogH) * 0.5f;

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
    }

    /* Determine title based on current view */
    const char *windowTitle = "Perfect Dark";
    if (s_MenuView == 1) windowTitle = "Play";
    else if (s_MenuView == 2) windowTitle = "Settings";

    float pdTitleH = drawPdWindowFrame(dialogX, dialogY, dialogW, dialogH, windowTitle);

    /* Offset content below the PD title bar */
    ImGui::SetCursorPosY(pdTitleH + ImGui::GetStyle().WindowPadding.y);

    float buttonH = 40.0f * scale;
    float buttonW = -1.0f; /* full width */
    float spacing = 6.0f * scale;

    /* B button / Escape navigation:
     * Sub-views (Play, Settings) -> back to top-level
     * Top-level -> close menu entirely (return to CI free-roam) */
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (s_MenuView != 0) {
            s_MenuView = 0;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        } else {
            /* At top-level: close the menu, return to Carrington Institute */
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }

    if (s_MenuView == 0) {
        /* ================================================================
         * TOP LEVEL: Play / Settings / Quit Game
         * ================================================================ */
        ImGui::Dummy(ImVec2(0, 8.0f * scale));

        ImGui::TextDisabled("Carrington Institute");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, spacing * 2));

        /* Play */
        if (PdButton("Play", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 1;
        }
        /* Give default nav focus to the first button */
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Settings */
        if (PdButton("Settings", ImVec2(buttonW, buttonH * 1.2f))) {
            s_MenuView = 2;
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Quit Game */
        if (PdButton("Quit Game", ImVec2(buttonW, buttonH * 1.2f))) {
            /* SDL_Quit event to cleanly shut down */
            SDL_Event quitEvent;
            quitEvent.type = SDL_QUIT;
            SDL_PushEvent(&quitEvent);
        }

        ImGui::Dummy(ImVec2(0, spacing * 3));
        ImGui::Separator();

        /* Change Agent link at bottom */
        if (PdButton("Change Agent...", ImVec2(buttonW, 28.0f * scale))) {
            menuPushDialog(&g_ChangeAgentMenuDialog);
        }

    } else if (s_MenuView == 1) {
        /* ================================================================
         * PLAY SUB-MENU
         * ================================================================ */
        ImGui::Dummy(ImVec2(0, 4.0f * scale));

        /* Solo Missions */
        if (PdButton("Solo Missions", ImVec2(buttonW, buttonH))) {
            menuhandlerMainMenuSoloMissions(MENUOP_SET, nullptr, nullptr);
        }
        /* Give default nav focus to first item */
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetItemDefaultFocus();
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Combat Simulator */
        if (PdButton("Combat Simulator", ImVec2(buttonW, buttonH))) {
            menuhandlerMainMenuCombatSimulator(MENUOP_SET, nullptr, nullptr);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Co-Operative */
        if (PdButton("Co-Operative", ImVec2(buttonW, buttonH))) {
            menuhandlerMainMenuCooperative(MENUOP_SET, nullptr, nullptr);
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Counter-Operative */
        {
            bool disabled = ((joyGetConnectedControllers() & ~0x1) == 0);
            if (disabled) ImGui::BeginDisabled();

            if (PdButton("Counter-Operative", ImVec2(buttonW, buttonH))) {
                menuhandlerMainMenuCounterOperative(MENUOP_SET, nullptr, nullptr);
            }

            if (disabled) ImGui::EndDisabled();
        }

        ImGui::Dummy(ImVec2(0, spacing));

        /* Network Game / Host */
        if (PdButton("Network Game", ImVec2(buttonW, buttonH))) {
            menuPushDialog(&g_NetMenuDialog);
        }

        ImGui::Dummy(ImVec2(0, spacing * 2));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, spacing));

        /* Back button */
        if (PdButton("Back", ImVec2(buttonW, 28.0f * scale))) {
            s_MenuView = 0;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }

    } else if (s_MenuView == 2) {
        /* ================================================================
         * SETTINGS SUB-MENU
         * ================================================================ */
        float contentH = dialogH - pdTitleH - 40.0f * scale;
        renderSettingsView(scale, contentH);

        ImGui::Separator();

        /* Back button */
        if (PdButton("Back", ImVec2(buttonW, 28.0f * scale))) {
            s_MenuView = 0;
            pdguiPlaySound(PDGUI_SND_SWIPE);
        }
    }

    /* Sound on view switches */
    if (s_PrevView >= 0 && s_PrevView != s_MenuView) {
        pdguiPlaySound(PDGUI_SND_SWIPE);
    }
    s_PrevView = s_MenuView;

    if (s_PrevSubTab >= 0 && s_PrevSubTab != s_SettingsSubTab) {
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    s_PrevSubTab = s_SettingsSubTab;

    /* ---- Footer ---- */
    ImGui::SetCursorPosY(dialogH - 20.0f * scale);
    ImGui::TextDisabled("F8: toggle OLD/NEW");

    ImGui::End();
    return 1;  /* Handled */
}

/* ========================================================================
 * Registration
 * ======================================================================== */

extern "C" {

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

    sysLogPrintf(LOG_NOTE, "pdgui_menu_mainmenu: Registered (PC + Pause variants)");
}

} /* extern "C" */
