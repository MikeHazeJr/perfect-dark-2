/**
 * pdgui_menu_solomission.cpp -- ImGui replacements for the Solo Mission flow.
 *
 * Group 1 (11 dialogs):
 *   g_SelectMissionMenuDialog         -- mission list with progressive unlock
 *   g_SoloMissionDifficultyMenuDialog -- Agent / Special Agent / Perfect Agent / PD Mode
 *   g_SoloMissionBriefingMenuDialog   -- scrollable briefing text
 *   g_PreAndPostMissionBriefingMenuDialog -- pre/post briefing (same renderer)
 *   g_AcceptMissionMenuDialog         -- objectives overview + Accept / Decline
 *   g_SoloMissionPauseMenuDialog      -- in-game pause: objectives + Abort
 *   g_SoloMissionOptionsMenuDialog    -- options: tabbed panel (Audio/Video/Controls)
 *   g_MissionAbortMenuDialog          -- danger confirmation: Cancel / Abort
 *
 * Registered with NULL renderFn (keep legacy rendering for model/controller previews):
 *   g_SoloMissionInventoryMenuDialog  -- ImGui weapon list (M1.2)
 *   g_FrWeaponsAvailableMenuDialog    -- handled by DEFAULT type fallback
 *   g_SoloMissionControlStyleMenuDialog -- handled by DEFAULT type fallback
 *
 * Design notes:
 *   - All sizing via pdguiScale() — zero hardcoded pixels.
 *   - Abort dialog uses the Red palette (danger) and restores Blue on exit.
 *   - Legacy dialog handlers (e.g. menudialog00103608, soloMenuDialogPauseStatus)
 *     still fire on OPEN/CLOSE/TICK regardless of which renderer is active, so
 *     g_Briefing is always correctly populated before these renderers run.
 *   - Complex dialogs with 3D previews are registered as NULL (force PD native)
 *     so their weapon-model and controller-diagram UX is fully preserved.
 *
 * IMPORTANT: C++ file — must NOT include types.h (#define bool s32 breaks C++).
 * Auto-discovered by GLOB_RECURSE for port/*.cpp in CMakeLists.txt.
 */

#include <SDL.h>
#include <PR/ultratypes.h>
#include <stdio.h>
#include <string.h>

#include "imgui/imgui.h"
#include "pdgui_hotswap.h"
#include "pdgui_style.h"
#include "pdgui_glyphs.h"
#include "pdgui_scaling.h"
#include "pdgui_audio.h"
#include "pdgui_nav.h"
#include "pdgui_layout.h"
#include "pdgui_widgets.h"      /* Priority L: shared label-left widget helpers */
#include "actionmap.h"
#include "system.h"
#include "inputctx.h"
#include "menupool.h"
#include "assetcatalog.h"

/* =========================================================================
 * Forward declarations — game symbols (extern "C" to avoid types.h)
 * ========================================================================= */

extern "C" {

/* ---- Dialog definitions ---- */
extern struct menudialogdef g_SelectMissionMenuDialog;
extern struct menudialogdef g_SoloMissionDifficultyMenuDialog;
extern struct menudialogdef g_SoloMissionBriefingMenuDialog;
extern struct menudialogdef g_PreAndPostMissionBriefingMenuDialog;
extern struct menudialogdef g_AcceptMissionMenuDialog;
extern struct menudialogdef g_SoloMissionPauseMenuDialog;
extern struct menudialogdef g_SoloMissionOptionsMenuDialog;
extern struct menudialogdef g_MissionAbortMenuDialog;
extern struct menudialogdef g_SoloMissionInventoryMenuDialog;
extern struct menudialogdef g_FrWeaponsAvailableMenuDialog;
extern struct menudialogdef g_SoloMissionControlStyleMenuDialog;
/* Sub-dialogs opened from the Options hub */
extern struct menudialogdef g_AudioOptionsMenuDialog;
extern struct menudialogdef g_VideoOptionsMenuDialog;
extern struct menudialogdef g_MissionControlOptionsMenuDialog;
extern struct menudialogdef g_MissionDisplayOptionsMenuDialog;
extern struct menudialogdef g_ExtendedMenuDialog;
/* PD Mode settings — opened from Difficulty, keep legacy */
extern struct menudialogdef g_PdModeSettingsMenuDialog;
/* S194 Batch 2: Co-op / Counter-Op flow */
extern struct menudialogdef g_CoopMissionDifficultyMenuDialog;
extern struct menudialogdef g_CoopOptionsMenuDialog;
extern struct menudialogdef g_AntiMissionDifficultyMenuDialog;
extern struct menudialogdef g_AntiOptionsMenuDialog;

/* ---- Menu navigation ---- */
void menuPushDialog(struct menudialogdef *dialogdef);
void menuPopDialog(void);
void menuStop(void);

/* ---- Language ---- */
const char *langSafe(s32 textid);

/* ---- Inventory query API ---- */
s32 invGetCount(void);
char *invGetNameByIndex(s32 index);
s32 invGetWeaponNumByIndex(s32 index);

/* ---- Audio API (port/include/audio.h) ---- */
f32  audioGetMasterVolume(void);
void audioSetMasterVolume(f32 vol);
f32  audioGetMusicVolume(void);
void audioSetMusicVolume(f32 vol);
f32  audioGetGameplayVolume(void);
void audioSetGameplayVolume(f32 vol);
f32  audioGetUiVolume(void);
void audioSetUiVolume(f32 vol);

/* ---- Video API (port/include/video.h) ---- */
s32  videoGetFullscreen(void);
void videoSetFullscreen(s32 fs);
s32  videoGetFullscreenMode(void);
void videoSetFullscreenMode(s32 mode);
s32  videoGetVsync(void);
void videoSetVsync(s32 vsync);
s32  videoGetFramerateLimit(void);
void videoSetFramerateLimit(s32 limit);
s32  videoGetMSAA(void);
void videoSetMSAA(s32 msaa);
u32  videoGetTextureFilter(void);
void videoSetTextureFilter(u32 filter);
s32  videoGetTextureFilter2D(void);
void videoSetTextureFilter2D(s32 filter);
s32  videoGetDetailTextures(void);
void videoSetDetailTextures(s32 detail);
s32  videoGetDisplayFPS(void);
void videoSetDisplayFPS(s32 displayfps);
f32  videoGetUiScaleMult(void);
void videoSetUiScaleMult(f32 mult);

/* Display mode */
typedef struct { s32 width; s32 height; } displaymode;
s32  videoGetNumDisplayModes(void);
s32  videoGetDisplayMode(displaymode *out, s32 index);
s32  videoGetDisplayModeIndex(void);
void videoSetDisplayMode(s32 index);

/* ---- Screen size / split (options.c) ---- */
s32  optionsGetScreenSize(void);
void optionsSetScreenSize(s32 size);
u8   optionsGetScreenSplit(void);
void optionsSetScreenSplit(u8 split);
s32  optionsGetForwardPitch(s32 mpchrnum);
void optionsSetForwardPitch(s32 mpchrnum, s32 enable);

/* ---- Input API (port/include/input.h) ---- */
s32  inputMouseIsEnabled(void);
void inputMouseEnable(s32 enabled);
void inputMouseGetSpeed(f32 *x, f32 *y);
void inputMouseSetSpeed(f32 x, f32 y);
s32  inputGetMouseLockMode(void);
void inputSetMouseLockMode(s32 mode);
s32  inputControllerGetInvertRStickY(s32 cidx);
void inputControllerSetInvertRStickY(s32 cidx, s32 invert);

/* ---- Extended player config (must match types.h layout) ---- */
struct sm_extplayerconfig {
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
extern struct sm_extplayerconfig g_PlayerExtCfg[];

/* Extended vars */
extern s32 g_TickRateDiv;
extern s32 g_BgunGeMuzzleFlashes;
extern s32 g_MusicDisableMpDeath;
extern s32 g_HudCenter;
extern f32 g_ViShakeIntensityMult;
extern s32 g_MenuMouseControl;

/* Config save */
s32 configSave(const char *fname);

/* ---- Solo stage table (21 entries, indices 0–20) ----
 * Mirrors struct solostage from types.h.  The compiler inserts 1 byte of
 * natural alignment padding between unk04 (u8) and name1 (u16). */
struct sm_solostage {
    u32 stagenum;
    u8  unk04;
    /* 1 byte compiler pad */
    u16 name1;  /* e.g. "dataDyne Central" */
    u16 name2;  /* e.g. " - Defection"     */
    u16 name3;  /* e.g. "dataDyne Defection" (short form) */
    const char *catalog_id; /* PRIMARY: catalog ID string, e.g. "base:defection" */
};
extern struct sm_solostage g_SoloStages[];

/* ---- Mission configuration ----
 * Layout mirrors struct missionconfig from types.h exactly.
 * IMPORTANT: stage_id[64] was added to missionconfig (PC port catalog field)
 * between diff_pdmode and stagenum. The shadow struct MUST include it or all
 * subsequent field accesses are at wrong offsets (stagenum=0x00 crash).
 *
 * Real layout:
 *   offset  0: diff_pdmode (u8)         — difficulty:7, pdmode:1
 *   offset  1: stage_id[64] (char[64])  — PRIMARY catalog ID e.g. "base:defection"
 *   offset 65: stagenum (u8)            — DEPRECATED integer stagenum
 *   offset 66: stageindex (u8)
 *   offset 67: coop_anti (u8)           — iscoop:1, isanti:1
 */
struct sm_missionconfig {
    u8   diff_pdmode;   /* difficulty:7, pdmode:1 */
    char stage_id[64];  /* PRIMARY: catalog ID string — e.g. "base:defection" */
    u8   stagenum;      /* DEPRECATED: integer stage ID — used by menuhandlerAcceptMission */
    u8   stageindex;
    u8   coop_anti;     /* iscoop:1, isanti:1 */
    /* remaining fields not needed here */
};
extern struct sm_missionconfig g_MissionConfig;

#define SM_DIFFICULTY(mc)   ((mc)->diff_pdmode & 0x7F)
#define SM_PDMODE(mc)       (((mc)->diff_pdmode >> 7) & 0x1)
#define SM_ISCOOP(mc)       ((mc)->coop_anti & 0x01)
#define SM_ISANTI(mc)       (((mc)->coop_anti >> 1) & 0x01)
#define SM_SET_DIFFICULTY(mc, d) \
    ((mc)->diff_pdmode = (u8)(((mc)->diff_pdmode & 0x80) | ((d) & 0x7F)))
#define SM_CLEAR_PDMODE(mc) \
    ((mc)->diff_pdmode &= 0x7F)

/* ---- Game file — only the besttimes slice we need ----
 * struct gamefile layout: name[11] + flags(2) + pad(3) + totaltime(4) +
 *   flags[10](10) + unk1e(2) + besttimes[21][3] at offset 0x20. */
struct sm_gamefile {
    char name[11];                    /* 0x00 */
    u8   thumbnail_autodifficulty;    /* 0x0b: thumbnail:5, autodifficulty:3 */
    u8   autostageindex;              /* 0x0c */
    u8   _pad0d[3];                   /* 0x0d–0x0f: alignment padding */
    u32  totaltime;                   /* 0x10 */
    u8   flags[10];                   /* 0x14 */
    u16  unk1e;                       /* 0x1e */
    u16  besttimes[21][3];            /* 0x20  (NUM_SOLOSTAGES=21, 3 difficulties) */
};
extern struct sm_gamefile g_GameFile;

#define SM_AUTODIFFICULTY(gf) (((gf)->thumbnail_autodifficulty >> 5) & 0x07)
#define SOLOSTAGEINDEX_SKEDARRUINS 16
#define NUM_SOLOSTAGES             21
#define DIFF_A  0
#define DIFF_SA 1
#define DIFF_PA 2
#define DIFF_PD 3

/* ---- Briefing (set by dialog handler before ImGui renderer runs) ---- */
struct sm_briefing {
    u16 briefingtextnum;
    u16 objectivenames[6];
    u16 objectivedifficulties[6];
    u16 langbank;
};
extern struct sm_briefing g_Briefing;

/* ---- Functions ---- */
bool         isStageDifficultyUnlocked(s32 stageindex, s32 difficulty);
s32          getNumUnlockedSpecialStages(void);
s32          func0f104720(s32 slot);   /* special-stage slot → g_SoloStages index */
void         lvSetDifficulty(s32 difficulty);
s32          lvGetDifficulty(void);
s32          objectiveGetCount(void);
s32          objectiveCheck(s32 index);
void         mainChangeToStage(s32 stagenum);
extern s32   g_MpPlayerNum;

/* Stage table index lookup — converts logical stagenum to g_Stages[] index */
s32 bgGetStageIndex(s32 stagenum);

/* Accept / abort mission — only MENUOP_SET branch used; item/data may be NULL */
#define MENUOP_SET 6
uintptr_t menuhandlerAcceptMission(s32 op, void *item, void *data);
uintptr_t menuhandlerAbortMission(s32 op, void *item, void *data);

/* Load briefing data for a stage by catalog ID (populates g_Briefing) */
void soloLoadBriefingForStageId(const char *stage_id);

/* Language text IDs for mission group headings.
 * Encoding: (LANGBANK << 9) | string_offset.
 * LANGBANK_OPTIONS = 0x2b -> prefix 0x5600.
 * LANGBANK_MPWEAPONS = 0x2a -> prefix 0x5400.
 * F-0.1: shadow defines were missing bank prefix (used raw offset in bank 0
 * which is NULL), causing ~15 blank strings and ~15 English-only fallbacks. */
#define L_OPTIONS_122  0x567a  /* "Mission Select"     */
#define L_OPTIONS_123  0x567b  /* "Mission 1"          */
#define L_OPTIONS_124  0x567c  /* "Mission 2"          */
#define L_OPTIONS_125  0x567d  /* "Mission 3"          */
#define L_OPTIONS_126  0x567e  /* "Mission 4"          */
#define L_OPTIONS_127  0x567f  /* "Mission 5"          */
#define L_OPTIONS_128  0x5680  /* "Mission 6"          */
#define L_OPTIONS_129  0x5681  /* "Mission 7"          */
#define L_OPTIONS_130  0x5682  /* "Mission 8"          */
#define L_OPTIONS_131  0x5683  /* "Mission 9"          */
#define L_OPTIONS_132  0x5684  /* "Special Assignments"*/
#define L_OPTIONS_172  0x56ac  /* "Status"             */
#define L_OPTIONS_173  0x56ad  /* "Abort!"             */
#define L_OPTIONS_174  0x56ae  /* "Warning"            */
#define L_OPTIONS_175  0x56af  /* "Do you want to abort the mission?" */
#define L_OPTIONS_176  0x56b0  /* "Cancel"             */
#define L_OPTIONS_177  0x56b1  /* "Abort"              */
#define L_OPTIONS_178  0x56b2  /* "Inventory"          */
#define L_OPTIONS_181  0x56b5  /* "Audio"              */
#define L_OPTIONS_182  0x56b6  /* "Video"              */
#define L_OPTIONS_183  0x56b7  /* "Control"            */
#define L_OPTIONS_184  0x56b8  /* "Display"            */
#define L_OPTIONS_247  0x56f7  /* "Briefing"           */
#define L_OPTIONS_248  0x56f8  /* "Select Difficulty"  */
#define L_OPTIONS_249  0x56f9  /* "Difficulty"         */
#define L_OPTIONS_251  0x56fb  /* "Agent"              */
#define L_OPTIONS_252  0x56fc  /* "Special Agent"      */
#define L_OPTIONS_253  0x56fd  /* "Perfect Agent"      */
#define L_OPTIONS_254  0x56fe  /* "Cancel"             */
#define L_OPTIONS_273  0x5711  /* "Overview"           */
#define L_OPTIONS_274  0x5712  /* "Accept"             */
#define L_OPTIONS_275  0x5713  /* "Decline"            */
#define L_MPWEAPONS_221 0x54dd /* "Perfect Dark" (mode)*/

/* S194 Batch 2: Co-op / Counter-Op language IDs.
 * Same bank prefix fix as above (F-0.1). */
#define L_OPTIONS_255  0x56ff  /* "Co-Operative Options"      */
#define L_OPTIONS_256  0x5700  /* "Radar On" (coop)            */
#define L_OPTIONS_257  0x5701  /* "Friendly Fire"              */
#define L_OPTIONS_258  0x5702  /* "Perfect Buddy"              */
#define L_OPTIONS_259  0x5703  /* "Continue" (coop)            */
#define L_OPTIONS_260  0x5704  /* "Cancel"  (coop)             */
#define L_OPTIONS_261  0x5705  /* "Human"                      */
#define L_OPTIONS_262  0x5706  /* "1 Simulant"                 */
#define L_OPTIONS_263  0x5707  /* "2 Simulants"                */
#define L_OPTIONS_264  0x5708  /* "3 Simulants"                */
#define L_OPTIONS_265  0x5709  /* "4 Simulants"                */
#define L_OPTIONS_266  0x570a  /* "Counter-Operative Options"  */
#define L_OPTIONS_267  0x570b  /* "Radar On" (anti)            */
#define L_OPTIONS_269  0x570d  /* "Continue" (anti)            */
#define L_OPTIONS_270  0x570e  /* "Cancel"  (anti)             */

/* ---- S194 Batch 2: menuitem + handlerdata ABI ----
 * Mirrors types.h layout.  C++ file cannot include types.h because its
 * "#define bool s32" breaks C++, so the subset we need is shadowed here.
 * Layout verified to match struct menuitem / struct handlerdata_{checkbox,
 * dropdown} in src/include/types.h. */
#define MENUOP_GETOPTIONCOUNT    1
#define MENUOP_GETOPTIONTEXT     3
#define MENUOP_GETSELECTEDINDEX  7
#define MENUOP_GET               8
#define MENUOP_CHECKDISABLED     12

struct s194_handlerdata_checkbox { u32 value; };
struct s194_handlerdata_dropdown { uintptr_t value; uintptr_t unk04; };

union s194_handlerdata {
    struct s194_handlerdata_checkbox checkbox;
    struct s194_handlerdata_dropdown dropdown;
    u8 _pad[256];
};

struct s194_menuitem {
    u8        type;
    u8        param;
    u32       flags;
    intptr_t  param2;    /* lang ID or literal text pointer */
    intptr_t  param3;
    uintptr_t (*handler)(s32 op, struct s194_menuitem *, union s194_handlerdata *);
};

/* ---- Batch 2: game-side menu handler delegates ----
 * These are normal legacy menu handlers in mainmenu.c.  The renderer
 * delegates ALL state manipulation to these so backing-store logic
 * (modifiedfiles dirty flag, getMaxAiBuddies clamps, connected-controller
 * math) stays in ONE place.  We call them with our shadow types; the
 * layout is ABI-compatible with the real struct menuitem / handlerdata. */
uintptr_t menuhandlerCoopRadar           (s32, struct s194_menuitem *, union s194_handlerdata *);
uintptr_t menuhandlerCoopFriendlyFire    (s32, struct s194_menuitem *, union s194_handlerdata *);
uintptr_t menuhandlerCoopBuddy           (s32, struct s194_menuitem *, union s194_handlerdata *);
uintptr_t menuhandlerAntiRadar           (s32, struct s194_menuitem *, union s194_handlerdata *);
uintptr_t menuhandlerAntiMainPlayer      (s32, struct s194_menuitem *, union s194_handlerdata *);
uintptr_t menuhandlerBuddyOptionsContinue(s32, struct s194_menuitem *, union s194_handlerdata *);

} /* extern "C" */

/* =========================================================================
 * Module state
 * ========================================================================= */

static bool s_Registered = false;

/* Mission Select — two-panel state */
static s32 s_MissionSelectIdx = 0;     /* Currently selected stage index (0–20) */
static s32 s_DetailDiffIdx    = 0;     /* Difficulty selection in detail panel (0=A, 1=SA, 2=PA) */
static s32 s_DetailFocusIdx   = 0;     /* Focus within detail panel: 0..2=diff, 3=Briefing, 4=Start */

/* M-18 (menu-stack §6 progressive focus): the mission-select flow uses a
 * three-tier focus model. The leaf menu is always MENU_TYPE_SOLO_MISSION;
 * within the leaf, focus narrows A-press-by-A-press from the mission list
 * to the difficulty rows to the Start button. B backs up one tier and
 * restores focus to the invoker (the selected mission row / last diff
 * row). Mouse click on any control jumps focus to that control directly
 * without breaking the group semantics. */
typedef enum {
    FOCUS_MISSION_LIST = 0,   /* Left panel: mission list has focus */
    FOCUS_DIFFICULTY   = 1,   /* Right panel: difficulty rows have focus */
    FOCUS_START        = 2,   /* Right panel: Start Mission button has focus */
} MissionFocusGroup;

static MissionFocusGroup s_FocusGroup = FOCUS_MISSION_LIST;
/* s_DetailPanelFocus is derived from s_FocusGroup; kept as a helper macro
 * below so the dozens of existing "is right panel active" checks continue
 * to work without a mass-rename. */
#define s_DetailPanelFocus (s_FocusGroup != FOCUS_MISSION_LIST)
static s32 s_PrevBriefingStage = -1;   /* Last stage we loaded briefing for (avoid reload) */
static bool s_ShowLockedMissions = false; /* Debug: show all missions regardless of unlock */

/* Difficulty (legacy path, kept for fallback) */
static s32 s_DiffSelectIdx = 0;

/* Briefing scroll */
static float s_BriefingScroll = 0.0f;

/* Accept Mission */
static s32 s_AcceptSelectIdx = 0;  /* 0 = Accept, 1 = Decline */

/* Pause */
static s32 s_PauseSelectIdx = 0;   /* 0 = close, 1 = Inventory, 2 = Options, 3 = Abort */

/* P8: Restart confirmation state.
 * M-6-A (2026-04-19): Replaced s_RestartConfirm + s_RestartSelectIdx with
 * canonical S385 popup modal via pdguiRenderConfirmModal().  openFrame is
 * the caller-owned tracker that the helper reads/clears. */
static s32 s_RestartOpenFrame = -1;

/* Abort confirmation — M-2 (2026-04-19): BeginPopupModal pattern with
 * 5-frame SetKeyboardFocusHere(0) focus latch + 3-frame input debounce so
 * the Enter press that opened the popup can't bleed through. Mirrors the
 * M-1 renderMpEndGameDialog pattern in pdgui_menu_warning.cpp. */
static void *s_AbortOpenedForDialog = nullptr;
static s32   s_AbortOpenFrame = -1;
#define ABORT_FRAME_DEBOUNCE       3
#define ABORT_FORCE_FOCUS_FRAMES   5

/* Options hub — now a tabbed panel */
static s32 s_OptionsSelectIdx = 0;
static s32 s_OptionsTabIdx    = 0;  /* 0=Audio, 1=Video, 2=Controls */

/* =========================================================================
 * PD-styled widget wrappers (match pdgui_menu_mainmenu.cpp style)
 * ========================================================================= */

extern "C" void pdguiDrawButtonEdgeGlow(f32 x, f32 y, f32 w, f32 h, s32 isActive);

static bool PdButton(const char *label, const ImVec2 &size = ImVec2(0, 0))
{
    bool clicked = ImGui::Button(label, size);
    if (clicked) pdguiPlaySound(PDGUI_SND_SELECT);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive() || ImGui::IsItemFocused()) {
        ImVec2 rmin = ImGui::GetItemRectMin();
        ImVec2 rmax = ImGui::GetItemRectMax();
        pdguiDrawButtonEdgeGlow(rmin.x, rmin.y,
                                rmax.x - rmin.x, rmax.y - rmin.y,
                                ImGui::IsItemActive() ? 1 : 0);
    }
    return clicked;
}

/* Priority L (2026-04-25): solomission's Pd* helpers now wrap the shared
 * `pdgui*` widgets defined in `port/fast3d/pdgui_widgets.cpp` so the
 * label-left layout pattern is single-source-of-truth across menus. */

static bool PdCheckbox(const char *label, bool *v)
{
    return pdguiCheckbox(label, v);
}

static bool PdCombo(const char *label, int *current_item, const char *const items[], int items_count)
{
    return pdguiCombo(label, current_item, items, items_count);
}

static bool PdSliderFloat(const char *label, float *v, float v_min, float v_max, const char *format = "%.3f")
{
    return pdguiSliderFloat(label, v, v_min, v_max, format);
}

static bool PdSliderInt(const char *label, int *v, int v_min, int v_max, const char *format = "%d")
{
    return pdguiSliderInt(label, v, v_min, v_max, format);
}

/* =========================================================================
 * Helpers
 * ========================================================================= */

/**
 * Format a best-time value (seconds) into "Mm:SSs" or "--:--" if zero.
 * Mirrors soloMenuTextBestTime() from mainmenu.c.
 */
static void formatBestTime(char *buf, size_t bufsz, u16 t)
{
    if (t == 0) {
        snprintf(buf, bufsz, "--:--");
        return;
    }
    if (t >= 0xFFF) {
        snprintf(buf, bufsz, "==:==");
        return;
    }
    s32 h = t / 3600;
    s32 m = (t % 3600) / 60;
    s32 s = t % 60;
    if (h > 0) {
        snprintf(buf, bufsz, "%dh:%02dm:%02ds", h, m, s);
    } else {
        snprintf(buf, bufsz, "%dm:%02ds", m, s);
    }
}



/* Difficulty badge fill colors: Agent=green, SA=blue, PA=gold, PD=purple */
static const ImU32 k_DiffBadgeColor[] = {
    IM_COL32( 60, 200,  80, 255),   /* Agent */
    IM_COL32( 80, 160, 255, 255),   /* Special Agent */
    IM_COL32(220, 190,  50, 255),   /* Perfect Agent */
    IM_COL32(180,  80, 220, 255),   /* Dark Agent / PD Mode */
};
static const char *k_DiffShort[] = { "A", "S", "P" };

/* =========================================================================
 * Mission Select
 * ========================================================================= */

/* =========================================================================
 * FIX-G.1: Stage category classification
 * Stages 0..SOLOSTAGEINDEX_SKEDARRUINS  = main campaign missions
 * Stages SOLOSTAGEINDEX_SKEDARRUINS+1.. = Special Assignments
 * STAGE_CAT_BONUS reserved for future unlockable content
 * ========================================================================= */

typedef enum {
    STAGE_CAT_MISSION = 0,   /* Missions 1–9, main campaign */
    STAGE_CAT_SPECIAL,       /* Special Assignments SA-1–4  */
    STAGE_CAT_BONUS,         /* Reserved — future bonus stages */
} stagecat_t;

static stagecat_t stageCategoryFor(s32 stageIdx)
{
    if (stageIdx <= SOLOSTAGEINDEX_SKEDARRUINS) return STAGE_CAT_MISSION;
    return STAGE_CAT_SPECIAL;
}
/* countMissionGroupCompletions / countSpecialCompletions defined below
 * k_MissionGroups / k_NumRegularGroups (same TU, resolved at compile time). */

/* Mission group data: {firstStageIndex, langId} — mirrors menuhandlerMissionList */
struct MissionGroup { s32 firstIdx; s32 langId; };
static const MissionGroup k_MissionGroups[] = {
    {  0, L_OPTIONS_123 },
    {  3, L_OPTIONS_124 },
    {  4, L_OPTIONS_125 },
    {  6, L_OPTIONS_126 },
    {  9, L_OPTIONS_127 },
    { 12, L_OPTIONS_128 },
    { 14, L_OPTIONS_129 },
    { 15, L_OPTIONS_130 },
    { 16, L_OPTIONS_131 },
};
static const s32 k_NumRegularGroups = (s32)(sizeof(k_MissionGroups) / sizeof(k_MissionGroups[0]));

/** Return the mission group index (0–8) for a given stage index. */
static s32 stageToGroupIdx(s32 stageIdx)
{
    s32 grp = 0;
    for (s32 g = 1; g < k_NumRegularGroups; g++) {
        if (stageIdx >= k_MissionGroups[g].firstIdx) grp = g;
        else break;
    }
    return grp;
}

/* FIX-G.1: Completion counters for section headers (G.2).
 * A mission group is "completed" when every stage in it has been beaten on Agent. */
static void countMissionGroupCompletions(s32 *doneOut, s32 *totalOut)
{
    *totalOut = k_NumRegularGroups;
    *doneOut  = 0;
    for (s32 g = 0; g < k_NumRegularGroups; g++) {
        s32 first = k_MissionGroups[g].firstIdx;
        s32 last  = (g + 1 < k_NumRegularGroups)
                  ? k_MissionGroups[g + 1].firstIdx - 1
                  : SOLOSTAGEINDEX_SKEDARRUINS;
        bool allDone = true;
        for (s32 i = first; i <= last; i++) {
            if (g_GameFile.besttimes[i][DIFF_A] == 0) { allDone = false; break; }
        }
        if (allDone) (*doneOut)++;
    }
}

/* Count special stages with at least one cleared time on Agent. */
static void countSpecialCompletions(s32 *doneOut, s32 *totalOut)
{
    *totalOut = NUM_SOLOSTAGES - (SOLOSTAGEINDEX_SKEDARRUINS + 1);
    *doneOut  = 0;
    for (s32 j = SOLOSTAGEINDEX_SKEDARRUINS + 1; j < NUM_SOLOSTAGES; j++) {
        if (g_GameFile.besttimes[j][DIFF_A] != 0) (*doneOut)++;
    }
}

/*
 * renderMissionSelect — Two-panel mission select (M1.1 redesign).
 *
 * Left panel:  Mission list with chapter headings, blip completion dots,
 *              unlock filter (B-90). Locked missions grayed out.
 * Right panel: Mission detail — stage name, difficulty picker (B-96),
 *              objectives for selected difficulty (B-91), briefing text,
 *              best time, Start button.
 *
 * Flow: pick mission (left) → pick difficulty (right) → see objectives → Start.
 * All in one screen — no dialog chain.
 */

/** Helper: check if a stage index is accessible (unlocked on Agent, or debug override). */
static bool missionIsAccessible(s32 stageIdx)
{
    if (s_ShowLockedMissions) return true;
    return (bool)isStageDifficultyUnlocked(stageIdx, DIFF_A);
}

/** Helper: set the selected mission and load its briefing data. */
static void missionSelectStage(s32 stageIdx)
{
    if (stageIdx < 0 || stageIdx >= NUM_SOLOSTAGES) return;
    s_MissionSelectIdx = stageIdx;
    s_DetailDiffIdx    = 0;  /* default to Agent */
    s_DetailFocusIdx   = 0;

    g_MissionConfig.stageindex = (u8)stageIdx;

    /* CATALOG-FIRST: stage_id from g_SoloStages[] is the sole identity */
    const char *cid = g_SoloStages[stageIdx].catalog_id;
    if (cid && cid[0]) {
        strncpy(g_MissionConfig.stage_id,
                cid, sizeof(g_MissionConfig.stage_id) - 1);
        g_MissionConfig.stage_id[sizeof(g_MissionConfig.stage_id) - 1] = '\0';
    } else {
        g_MissionConfig.stage_id[0] = '\0';
    }

    /* Load briefing only if stage changed */
    if (s_PrevBriefingStage != stageIdx) {
        soloLoadBriefingForStageId(g_MissionConfig.stage_id);
        s_PrevBriefingStage = stageIdx;
    }
}

static s32 renderMissionSelect(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    float mw      = pdguiMenuWidth();
    float mh      = pdguiMenuHeight();
    ImVec2 mpos   = pdguiMenuPos();
    float titleH  = pdguiScale(39.0f);
    float rowH    = pdguiScale(42.0f);
    float blipR   = pdguiScale(7.5f);
    float blipGap = pdguiScale(19.5f);

    /* Panel split: 38% left, 62% right */
    float panelGap = pdguiScale(12.0f);
    float leftW    = mw * 0.38f;
    float rightW   = mw - leftW - panelGap;

    static const char *k_DiffFullNames[] = {
        "Agent", "Special Agent", "Perfect Agent", "Dark Agent"
    };

    ImGui::SetNextWindowPos(mpos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_mission_select", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        /* M-18: reset to MISSION_LIST group on open. Focus returns to the
         * invoker (mission list) via the parent menu pop in any case, but
         * fresh opens always start at the list level regardless of what
         * group last had focus before a previous close. */
        s_FocusGroup = FOCUS_MISSION_LIST;
        s_PrevBriefingStage = -1;  /* force reload on reopen */
        /* Select first accessible mission */
        for (s32 i = 0; i < NUM_SOLOSTAGES; i++) {
            if (missionIsAccessible(i)) {
                missionSelectStage(i);
                break;
            }
        }
    }

    pdguiDrawPdDialog(mpos.x, mpos.y, mw, mh, "Mission Select", 1);
    pdguiSetCursorBelowTitle(titleH);

    /* Escape at the list level pops the dialog (menu-stack root exit).
     * Escape inside DIFFICULTY/START is consumed by the progressive-back
     * handler below, so we gate this pop on MISSION_LIST only — otherwise
     * a single Esc press would skip DIFFICULTY -> LIST -> dialog-pop in
     * one keystroke, violating the "one tier per press" invariant. */
    if (s_FocusGroup == FOCUS_MISSION_LIST &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* M-18 progressive focus (menu-stack §6): Right/A narrows to the next
     * group (LIST -> DIFFICULTY -> START), Left/B steps back one group.
     * Left-arrow jumps directly back to the list (visual parity with the
     * two-column layout), while Esc/B steps ONE tier so keyboard users
     * can unwind START -> DIFFICULTY -> LIST one press at a time. */
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        if (s_FocusGroup == FOCUS_MISSION_LIST) {
            s_FocusGroup = FOCUS_DIFFICULTY;
            s_DetailFocusIdx = 0;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        if (s_FocusGroup != FOCUS_MISSION_LIST) {
            s_FocusGroup = FOCUS_MISSION_LIST;
            pdguiPlaySound(PDGUI_SND_FOCUS);
        }
    }
    /* B button / Esc: step back one focus tier. From START -> DIFFICULTY
     * (restoring focus to the currently-selected diff row); from DIFFICULTY
     * -> MISSION_LIST (restoring focus to the selected mission row). */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (s_FocusGroup == FOCUS_START) {
            s_FocusGroup = FOCUS_DIFFICULTY;
            s_DetailFocusIdx = s_DetailDiffIdx; /* return focus to invoker diff */
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        } else if (s_FocusGroup == FOCUS_DIFFICULTY) {
            s_FocusGroup = FOCUS_MISSION_LIST;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
        }
        /* MISSION_LIST Esc is handled above (pops the dialog). */
    }

    float bodyH = mh - titleH - pdguiScale(18.0f);

    /* ===================================================================
     * LEFT PANEL — Mission List
     * =================================================================== */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    if (ImGui::BeginChild("##ms_left", ImVec2(leftW, bodyH),
                           ImGuiChildFlags_NavFlattened,
                           ImGuiWindowFlags_None)) {

        /* D-pad up/down navigation in left panel */
        if (!s_DetailPanelFocus) {
            bool navDown = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
            bool navUp   = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);

            if (navDown) {
                s32 next = s_MissionSelectIdx + 1;
                while (next < NUM_SOLOSTAGES && !missionIsAccessible(next)) next++;
                if (next < NUM_SOLOSTAGES) {
                    missionSelectStage(next);
                    pdguiPlaySound(PDGUI_SND_FOCUS);
                }
            }
            if (navUp) {
                s32 prev = s_MissionSelectIdx - 1;
                while (prev >= 0 && !missionIsAccessible(prev)) prev--;
                if (prev >= 0) {
                    missionSelectStage(prev);
                    pdguiPlaySound(PDGUI_SND_FOCUS);
                }
            }

            /* A button / Enter in left panel = narrow to DIFFICULTY group
             * (M-18: MISSION_LIST -> DIFFICULTY). Focus lands on the first
             * (Agent) difficulty row; the user then presses A again on the
             * chosen diff to narrow to START. */
            if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
                s_FocusGroup = FOCUS_DIFFICULTY;
                s_DetailFocusIdx = 0;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
        }

        if (ImGui::BeginChild("##ms_list_scroll", ImVec2(0, 0), false,
                               ImGuiWindowFlags_AlwaysVerticalScrollbar)) {

            /* FIX-G.2: "CAMPAIGN" section header with completion indicator.
             * Shown at the very top of the list so missions are clearly labelled
             * as a distinct category from Special Assignments below. */
            {
                s32 mDone = 0, mTotal = 0;
                countMissionGroupCompletions(&mDone, &mTotal);
                char campaignHdr[80];
                snprintf(campaignHdr, sizeof(campaignHdr),
                         "  CAMPAIGN  (%d/%d)", mDone, mTotal);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
                ImGui::SeparatorText(campaignHdr);
                ImGui::PopStyleColor();
                ImGui::Spacing();
            }

            /* Regular missions 0..SOLOSTAGEINDEX_SKEDARRUINS */
            s32 prevGroup = -1;
            for (s32 i = 0; i <= SOLOSTAGEINDEX_SKEDARRUINS; i++) {
                s32 grp     = stageToGroupIdx(i);
                s32 chap    = grp + 1;
                s32 chapPos = i - k_MissionGroups[grp].firstIdx + 1;

                bool accessible = missionIsAccessible(i);
                bool isSelected = (s_MissionSelectIdx == i);

                /* Chapter heading when the group changes.
                 * FIX-G (B-97): show completion count per chapter. */
                if (grp != prevGroup) {
                    if (prevGroup >= 0) ImGui::Spacing();
                    const char *chapLang = langSafe(k_MissionGroups[grp].langId);

                    /* Count completed and total missions in this chapter */
                    s32 chapFirst = k_MissionGroups[grp].firstIdx;
                    s32 chapLast  = (grp + 1 < k_NumRegularGroups)
                                  ? k_MissionGroups[grp + 1].firstIdx - 1
                                  : SOLOSTAGEINDEX_SKEDARRUINS;
                    s32 chapTotal = chapLast - chapFirst + 1;
                    s32 chapDone  = 0;
                    for (s32 ci = chapFirst; ci <= chapLast; ci++) {
                        if (isStageDifficultyUnlocked(ci, DIFF_A)) chapDone++;
                    }

                    char chapHdr[96];
                    if (chapLang[0]) {
                        snprintf(chapHdr, sizeof(chapHdr), "-- %s (%d/%d) --",
                                 chapLang, chapDone, chapTotal);
                    } else {
                        snprintf(chapHdr, sizeof(chapHdr), "-- Mission %d (%d/%d) --",
                                 chap, chapDone, chapTotal);
                    }
                    ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo());
                    ImGui::TextUnformatted(chapHdr);
                    ImGui::PopStyleColor();
                    prevGroup = grp;
                }

                /* B-90: Skip inaccessible missions entirely (unless debug) */
                if (!accessible && !s_ShowLockedMissions) {
                    /* Show grayed-out name but not selectable */
                    ImGui::PushID(i);
                    ImVec2 rowPos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, rowH));
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    float rcy = rowPos.y + rowH * 0.5f;
                    char lockedLabel[192];
                    const char *ln1 = langSafe(g_SoloStages[i].name1);
                    const char *ln2 = langSafe(g_SoloStages[i].name2);
                    snprintf(lockedLabel, sizeof(lockedLabel), "%d.%d  %s%s",
                             chap, chapPos, ln1, ln2);
                    dl->AddText(
                        ImVec2(rowPos.x + pdguiScale(6.0f),
                               rcy - ImGui::GetTextLineHeight() * 0.5f),
                        pdguiPalImU32(PDPAL_ITEM_DISABLED, 120), lockedLabel);
                    ImGui::PopID();
                    continue;
                }

                const char *ln1 = langSafe(g_SoloStages[i].name1);
                const char *ln2 = langSafe(g_SoloStages[i].name2);
                char nodeLabel[192];
                snprintf(nodeLabel, sizeof(nodeLabel), "%d.%d  %s%s",
                         chap, chapPos, ln1, ln2);

                ImGui::PushID(i);

                ImVec2 rowPos   = ImGui::GetCursorScreenPos();
                float  contentW = ImGui::GetContentRegionAvail().x;

                /* Highlight selected row */
                if (isSelected && !s_DetailPanelFocus) {
                    pdguiDrawItemHighlight(rowPos.x, rowPos.y, contentW, rowH);
                } else if (isSelected) {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(rowPos,
                        ImVec2(rowPos.x + contentW, rowPos.y + rowH),
                        pdguiPalImU32(PDPAL_FOCUS_BG, 80));
                }

                bool doSelect = ImGui::Selectable("##ms_row", isSelected,
                                                   ImGuiSelectableFlags_None,
                                                   ImVec2(contentW, rowH));
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
                    doSelect = true;
                }

                /* Blip dots + mission name drawn over the selectable area */
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    float rcy = rowPos.y + rowH * 0.5f;
                    float bx  = rowPos.x + blipR + pdguiScale(6.0f);
                    for (s32 d = 0; d < 3; d++) {
                        bool beaten = (g_GameFile.besttimes[i][d] != 0);
                        float bcx   = bx + d * blipGap;
                        ImU32 fill  = beaten ? k_DiffBadgeColor[d]
                                             : IM_COL32(40, 40, 50, 160);
                        ImU32 ring  = beaten ? IM_COL32(200, 200, 200, 100)
                                             : IM_COL32(80, 80, 100, 120);
                        dl->AddCircleFilled(ImVec2(bcx, rcy), blipR, fill);
                        dl->AddCircle(ImVec2(bcx, rcy), blipR, ring);
                    }
                    float nameX  = bx + 3.0f * blipGap + pdguiScale(6.0f);
                    ImU32 nameCol = pdguiPalImU32(PDPAL_ITEM_UNFOCUSED, 255);
                    dl->AddText(
                        ImVec2(nameX, rcy - ImGui::GetTextLineHeight() * 0.5f),
                        nameCol, nodeLabel);
                }

                if (doSelect && accessible) {
                    missionSelectStage(i);
                    /* M-18: clicking / A-pressing a mission row narrows
                     * focus to DIFFICULTY (the mission is the "invoker"
                     * remembered for B-back). */
                    s_FocusGroup = FOCUS_DIFFICULTY;
                    s_DetailFocusIdx = 0;
                    pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                }

                /* Auto-scroll to keep selected item visible */
                if (isSelected && ImGui::IsWindowAppearing()) {
                    ImGui::SetScrollHereY(0.3f);
                }

                ImGui::PopID();
            } /* regular stage loop */

            /* FIX-G.2: Special Assignments section header — same SeparatorText style
             * as CAMPAIGN above, gold tint to distinguish the category visually.
             * Completion count uses besttimes (beaten on Agent) not unlock status. */
            s32 specialStart = SOLOSTAGEINDEX_SKEDARRUINS + 1;
            ImGui::Spacing();
            {
                s32 sDone = 0, sTotal = 0;
                countSpecialCompletions(&sDone, &sTotal);
                const char *saLang = langSafe(L_OPTIONS_132);
                char saBuf[96];
                if (saLang[0]) {
                    snprintf(saBuf, sizeof(saBuf), "  %s  (%d/%d)",
                             saLang, sDone, sTotal);
                } else {
                    snprintf(saBuf, sizeof(saBuf),
                             "  SPECIAL ASSIGNMENTS  (%d/%d)", sDone, sTotal);
                }
                ImGui::PushStyleColor(ImGuiCol_Text,        ImVec4(1.0f, 0.85f, 0.4f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_Separator,   ImVec4(0.7f, 0.55f, 0.1f, 0.6f));
                ImGui::SeparatorText(saBuf);
                ImGui::PopStyleColor(2);
                ImGui::Spacing();
            }

            for (s32 j = specialStart; j < NUM_SOLOSTAGES; j++) {
                s32 saNum       = j - specialStart + 1;
                bool accessible = missionIsAccessible(j);
                bool isSelected = (s_MissionSelectIdx == j);

                if (!accessible && !s_ShowLockedMissions) {
                    ImGui::PushID(0x200 + j);
                    ImVec2 rowPos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, rowH));
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    float rcy = rowPos.y + rowH * 0.5f;
                    char lockedLabel[192];
                    snprintf(lockedLabel, sizeof(lockedLabel), "SA-%d  %s",
                             saNum, langSafe(g_SoloStages[j].name1));
                    dl->AddText(
                        ImVec2(rowPos.x + pdguiScale(6.0f),
                               rcy - ImGui::GetTextLineHeight() * 0.5f),
                        pdguiPalImU32(PDPAL_ITEM_DISABLED, 120), lockedLabel);
                    ImGui::PopID();
                    continue;
                }

                const char *ln1 = langSafe(g_SoloStages[j].name1);
                char nodeLabel[192];
                snprintf(nodeLabel, sizeof(nodeLabel), "SA-%d  %s", saNum, ln1);

                ImGui::PushID(0x200 + j);

                ImVec2 rowPos   = ImGui::GetCursorScreenPos();
                float  contentW = ImGui::GetContentRegionAvail().x;

                if (isSelected && !s_DetailPanelFocus) {
                    pdguiDrawItemHighlight(rowPos.x, rowPos.y, contentW, rowH);
                } else if (isSelected) {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(rowPos,
                        ImVec2(rowPos.x + contentW, rowPos.y + rowH),
                        pdguiPalImU32(PDPAL_FOCUS_BG, 80));
                }

                bool doSelect = ImGui::Selectable("##ms_sp_row", isSelected,
                                                   ImGuiSelectableFlags_None,
                                                   ImVec2(contentW, rowH));

                /* Blip dots + name (gold tint for specials) */
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    float rcy = rowPos.y + rowH * 0.5f;
                    float bx  = rowPos.x + blipR + pdguiScale(6.0f);
                    for (s32 d = 0; d < 3; d++) {
                        bool beaten = (g_GameFile.besttimes[j][d] != 0);
                        float bcx   = bx + d * blipGap;
                        ImU32 fill  = beaten ? k_DiffBadgeColor[d]
                                             : IM_COL32(40, 40, 50, 160);
                        ImU32 ring  = beaten ? IM_COL32(200, 200, 200, 100)
                                             : IM_COL32(80, 80, 100, 120);
                        dl->AddCircleFilled(ImVec2(bcx, rcy), blipR, fill);
                        dl->AddCircle(ImVec2(bcx, rcy), blipR, ring);
                    }
                    float nameX  = bx + 3.0f * blipGap + pdguiScale(6.0f);
                    dl->AddText(
                        ImVec2(nameX, rcy - ImGui::GetTextLineHeight() * 0.5f),
                        IM_COL32(255, 230, 140, 255), nodeLabel);
                }

                if (doSelect && accessible) {
                    missionSelectStage(j);
                    /* M-18: Special Assignment row click/A narrows focus
                     * to DIFFICULTY, same as the main chapter rows above. */
                    s_FocusGroup = FOCUS_DIFFICULTY;
                    s_DetailFocusIdx = 0;
                    pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                }

                ImGui::PopID();
            } /* special stage loop */

        }
        ImGui::EndChild(); /* ms_list_scroll */
    }
    ImGui::EndChild(); /* ms_left */

    ImGui::SameLine(0.0f, panelGap);

    /* ===================================================================
     * RIGHT PANEL — Mission Detail
     * =================================================================== */
    /* Priority L (2026-04-25): NavFlattened layout panel. */
    if (ImGui::BeginChild("##ms_right", ImVec2(rightW, bodyH),
                           ImGuiChildFlags_NavFlattened,
                           ImGuiWindowFlags_None)) {

        s32 si = s_MissionSelectIdx;
        if (si < 0 || si >= NUM_SOLOSTAGES) si = 0;

        /* Ensure briefing is loaded for current selection */
        if (s_PrevBriefingStage != si) {
            missionSelectStage(si);
        }

        /* ---- Stage name header ---- */
        {
            char stageName[192];
            const char *ln1 = langSafe(g_SoloStages[si].name1);
            const char *ln2 = langSafe(g_SoloStages[si].name2);
            snprintf(stageName, sizeof(stageName), "%s%s", ln1, ln2);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::TextUnformatted(stageName);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();

        /* ---- Difficulty Picker (B-96: inline in detail panel) ---- */
        /* Detail panel navigation: 0–2 = diff, [3 = PD Mode], last = Start */
        bool pdModeVisible = (g_GameFile.besttimes[SOLOSTAGEINDEX_SKEDARRUINS][DIFF_PA] != 0);
        if (!pdModeVisible && s_DetailDiffIdx >= 3) s_DetailDiffIdx = 2;
        s32 k_NumDetailItems = 3 + (pdModeVisible ? 1 : 0) + 1; /* diffs + [PD] + Start */
        s32 startFocusIdx    = k_NumDetailItems - 1;

        if (s_DetailPanelFocus) {
            bool navDown = ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);
            bool navUp   = ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);

            if (navDown) {
                s_DetailFocusIdx++;
                if (s_DetailFocusIdx >= k_NumDetailItems)
                    s_DetailFocusIdx = 0;
                /* M-18: keep s_FocusGroup in sync with the detail index so
                 * B-back lands in the correct tier. When the cursor wraps
                 * past the last diff onto Start, we're in FOCUS_START; any
                 * other index within the right panel is FOCUS_DIFFICULTY. */
                s_FocusGroup = (s_DetailFocusIdx == startFocusIdx)
                             ? FOCUS_START : FOCUS_DIFFICULTY;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
            if (navUp) {
                s_DetailFocusIdx--;
                if (s_DetailFocusIdx < 0)
                    s_DetailFocusIdx = k_NumDetailItems - 1;
                /* M-18 (see navDown): sync focus group with the detail index. */
                s_FocusGroup = (s_DetailFocusIdx == startFocusIdx)
                             ? FOCUS_START : FOCUS_DIFFICULTY;
                pdguiPlaySound(PDGUI_SND_FOCUS);
            }
        }

        float diffRowH = pdguiScale(45.0f);
        static const s32  k_DiffIds[]          = { L_OPTIONS_251, L_OPTIONS_252, L_OPTIONS_253 };
        static const char *k_DiffFallbackNames[] = { "Agent", "Special Agent", "Perfect Agent" };

        ImGui::TextDisabled("Difficulty:");
        ImGui::Spacing();

        for (s32 d = 0; d < 3; d++) {
            bool locked  = !isStageDifficultyUnlocked(si, d);
            bool isSel   = (s_DetailDiffIdx == d);
            bool isFocus = s_DetailPanelFocus && (s_DetailFocusIdx == d);

            ImGui::PushID(0x300 + d);

            ImVec2 cp = ImGui::GetCursorScreenPos();
            float rowW = rightW - pdguiScale(12.0f);

            /* Selection highlight */
            if (isSel) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(cp,
                    ImVec2(cp.x + rowW, cp.y + diffRowH),
                    IM_COL32(40, 60, 100, 120), pdguiScale(6.0f));
                dl->AddRect(cp,
                    ImVec2(cp.x + rowW, cp.y + diffRowH),
                    k_DiffBadgeColor[d], pdguiScale(6.0f), 0, 1.5f);
            }
            /* Focus highlight */
            if (isFocus) {
                pdguiDrawItemHighlight(cp.x, cp.y, rowW, diffRowH);
            }

            bool clicked = ImGui::Selectable("##diff_pick", isSel,
                                              ImGuiSelectableFlags_None,
                                              ImVec2(rowW, diffRowH));
            if (ImGui::IsItemHovered()) {
                s_DetailFocusIdx = d;
                /* M-18: hovering a diff row promotes focus into DIFFICULTY
                 * group (mouse stays additive with controller). */
                s_FocusGroup = FOCUS_DIFFICULTY;
            }

            /* Confirm from keyboard/gamepad */
            bool doConfirm = isFocus &&
                ImGui::IsKeyPressed(ImGuiKey_Enter, false);

            if ((clicked || doConfirm) && !locked) {
                s_DetailDiffIdx = d;
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* M-18: A on an unlocked difficulty narrows focus to the
                 * START group so the next A press launches. Mouse clicks
                 * also advance to START — the visual "Start Mission" CTA
                 * is now highlighted waiting for confirm. We also move
                 * s_DetailFocusIdx to the action-bar slot so the focus
                 * ring + nav cursor land on the button, not the diff row. */
                s_FocusGroup = FOCUS_START;
                s_DetailFocusIdx = startFocusIdx;
            } else if ((clicked || doConfirm) && locked) {
                pdguiPlaySound(PDGUI_SND_ERROR);
            }

            /* Overlay: color badge + difficulty name + best time */
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                float cy = cp.y + (diffRowH - ImGui::GetTextLineHeight()) * 0.5f;
                float bx = cp.x + pdguiScale(9.0f);

                /* Color badge dot */
                float dotR = pdguiScale(6.0f);
                dl->AddCircleFilled(
                    ImVec2(bx + dotR, cp.y + diffRowH * 0.5f),
                    dotR, locked ? IM_COL32(60, 60, 70, 160) : k_DiffBadgeColor[d]);

                float tx = bx + dotR * 2.0f + pdguiScale(12.0f);
                ImU32 nameCol = locked ? IM_COL32(100, 100, 120, 180)
                                       : IM_COL32(255, 255, 255, 255);
                const char *diffName = langSafe(k_DiffIds[d]);
                if (!diffName || !diffName[0]) diffName = k_DiffFallbackNames[d];
                dl->AddText(ImVec2(tx, cy), nameCol, diffName);

                if (locked) {
                    dl->AddText(ImVec2(tx + pdguiScale(165.0f), cy),
                                pdguiImU32TintDanger(200), "[Locked]");
                } else {
                    char timeStr[32];
                    formatBestTime(timeStr, sizeof(timeStr),
                                   g_GameFile.besttimes[si][d]);
                    ImVec2 tSz = ImGui::CalcTextSize(timeStr);
                    dl->AddText(ImVec2(cp.x + rowW - tSz.x - pdguiScale(12.0f), cy),
                                pdguiImU32TintSuccess(210), timeStr);
                }
            }

            ImGui::PopID();
        }

        /* PD Mode row — only when Skedar Ruins beaten on Perfect Agent */
        if (pdModeVisible) {
            s32 pdFocusIdx = 3;
            bool isPdSel   = (s_DetailDiffIdx == 3);
            bool isPdFocus = s_DetailPanelFocus && (s_DetailFocusIdx == pdFocusIdx);

            ImGui::PushID(0x303);
            ImVec2 pdcp = ImGui::GetCursorScreenPos();
            float  rowW = rightW - pdguiScale(12.0f);

            if (isPdSel) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(pdcp,
                    ImVec2(pdcp.x + rowW, pdcp.y + diffRowH),
                    IM_COL32(60, 20, 80, 120), pdguiScale(6.0f));
                dl->AddRect(pdcp,
                    ImVec2(pdcp.x + rowW, pdcp.y + diffRowH),
                    k_DiffBadgeColor[3], pdguiScale(6.0f), 0, 1.5f);
            }
            if (isPdFocus) {
                pdguiDrawItemHighlight(pdcp.x, pdcp.y, rowW, diffRowH);
            }

            bool pdClicked = ImGui::Selectable("##diff_pd", isPdSel,
                                               ImGuiSelectableFlags_None,
                                               ImVec2(rowW, diffRowH));
            if (ImGui::IsItemHovered()) {
                s_DetailFocusIdx = pdFocusIdx;
                /* M-18: PD Mode row hover promotes focus into DIFFICULTY. */
                s_FocusGroup = FOCUS_DIFFICULTY;
            }

            bool pdConfirm = isPdFocus && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
            if (pdClicked || pdConfirm) {
                s_DetailDiffIdx = 3;
                pdguiPlaySound(PDGUI_SND_SELECT);
                /* M-18: A on PD Mode narrows focus to START, matching the
                 * behaviour of the other difficulty rows. Move the focus
                 * index too so the Start button gets the visual cursor. */
                s_FocusGroup = FOCUS_START;
                s_DetailFocusIdx = startFocusIdx;
            }

            /* Overlay: purple badge + label + best time */
            {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                float cy = pdcp.y + (diffRowH - ImGui::GetTextLineHeight()) * 0.5f;
                float bx = pdcp.x + pdguiScale(9.0f);
                float dotR = pdguiScale(6.0f);
                dl->AddCircleFilled(ImVec2(bx + dotR, pdcp.y + diffRowH * 0.5f),
                                    dotR, k_DiffBadgeColor[3]);
                float tx = bx + dotR * 2.0f + pdguiScale(12.0f);
                const char *pdLabel = langSafe(L_MPWEAPONS_221);
                if (!pdLabel || !pdLabel[0]) pdLabel = "Dark Agent";
                dl->AddText(ImVec2(tx, cy), IM_COL32(255, 255, 255, 255), pdLabel);
                char timeStr[32];
                formatBestTime(timeStr, sizeof(timeStr), g_GameFile.besttimes[si][DIFF_PD]);
                ImVec2 tSz = ImGui::CalcTextSize(timeStr);
                dl->AddText(ImVec2(pdcp.x + rowW - tSz.x - pdguiScale(12.0f), cy),
                            pdguiImU32TintSuccess(210), timeStr);
            }

            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();

        /* Pinned footer hint -- lives above the scrollable body so it is
         * always visible even as objectives/briefing scroll.  Preserves
         * the UX hint from the pre-Batch-0 bottom-of-panel placement. */
        if (s_DetailPanelFocus) {
            ImGui::TextDisabled("A: Select   B: Back   D-Pad: Navigate");
        } else {
            ImGui::TextDisabled("Select a mission from the list");
        }

        /* ---- Objectives + Briefing (scrollable body) ----
         * Batch 0 docked-action-bar rule: the Start Mission CTA must never
         * scroll off-screen.  We compute the scrollable body height as
         * (remaining space) - (action bar + gap) so the bar is always
         * visible and clickable at every resolution.  Objectives and
         * briefing share this scroll region; fixed content above (header,
         * difficulty picker, footer hint) stays pinned. */
        s32 selDiff = s_DetailDiffIdx;

        float availH  = ImGui::GetContentRegionAvail().y;
        float scrollH = pdguiBodyHeightForActionBar(availH);

        if (ImGui::BeginChild("##ms_detail_body", ImVec2(0, scrollH),
                               ImGuiChildFlags_NavFlattened,
                               ImGuiWindowFlags_AlwaysVerticalScrollbar)) {

            ImGui::TextDisabled("Objectives (%s):", k_DiffFullNames[selDiff]);
            ImGui::Spacing();

            bool anyObj = false;
            /* g_Briefing.objectivenames[0] = briefing text; [1]-[5] = objectives */
            for (s32 oi = 1; oi < 6; oi++) {
                if (g_Briefing.objectivenames[oi] == 0) continue;
                /* Filter by selected difficulty */
                u16 bits = g_Briefing.objectivedifficulties[oi];
                if (bits != 0 && !((bits >> (unsigned)selDiff) & 1)) continue;
                anyObj = true;

                const char *objText = langSafe(g_Briefing.objectivenames[oi]);

                /* Bullet dot */
                float dotSz = pdguiScale(12.0f);
                ImVec2 ocp  = ImGui::GetCursorScreenPos();
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddCircleFilled(
                    ImVec2(ocp.x + dotSz * 0.5f + pdguiScale(6.0f),
                           ocp.y + ImGui::GetTextLineHeight() * 0.5f + pdguiScale(3.0f)),
                    dotSz * 0.5f,
                    k_DiffBadgeColor[selDiff]);

                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotSz + pdguiScale(15.0f));
                ImGui::PushTextWrapPos(rightW - pdguiScale(24.0f));
                ImGui::TextUnformatted(objText);
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }

            if (!anyObj) {
                ImGui::TextDisabled("(No objectives for this difficulty)");
            }

            /* ---- Briefing text ---- */
            if (g_Briefing.briefingtextnum != 0) {
                const char *btxt = langSafe(g_Briefing.briefingtextnum);
                if (btxt && btxt[0]) {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::TextDisabled("Briefing:");
                    ImGui::Spacing();
                    ImGui::PushTextWrapPos(rightW - pdguiScale(24.0f));
                    ImGui::TextUnformatted(btxt);
                    ImGui::PopTextWrapPos();
                }
            }
        }
        ImGui::EndChild(); /* ms_detail_body */

        /* ---- Docked Action Bar: Start Mission (never scrolls) ---- */
        bool startFocus = s_DetailPanelFocus && (s_DetailFocusIdx == startFocusIdx);
        /* PD Mode is gated by pdModeVisible row visibility, not isStageDifficultyUnlocked */
        bool diffLocked = (selDiff == DIFF_PD) ? false
                                               : !isStageDifficultyUnlocked(si, selDiff);

        if (pdguiBeginActionBar("##ms_action_bar")) {
            float barW = ImGui::GetContentRegionAvail().x;

            if (diffLocked) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.8f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.4f, 1.0f));
            }

            bool activated = pdguiActionBarButton("Start Mission##ms_start",
                                                   startFocus ? 1 : 0,
                                                   barW);
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                s_DetailFocusIdx = startFocusIdx;
                /* M-18: Start button hover promotes focus into the
                 * START group (mouse users jump straight to the leaf). */
                s_FocusGroup = FOCUS_START;
            }

            if (activated && !diffLocked) {
                if (selDiff == DIFF_PD) {
                    /* PD Mode: set PA as base difficulty and open PD Mode settings */
                    SM_CLEAR_PDMODE(&g_MissionConfig);
                    SM_SET_DIFFICULTY(&g_MissionConfig, DIFF_PA);
                    lvSetDifficulty(DIFF_PA);
                    pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                    menuPushDialog(&g_PdModeSettingsMenuDialog);
                } else {
                    SM_CLEAR_PDMODE(&g_MissionConfig);
                    SM_SET_DIFFICULTY(&g_MissionConfig, selDiff);
                    lvSetDifficulty(selDiff);
                    menuhandlerAcceptMission(MENUOP_SET, nullptr, nullptr);
                    if (inputCtxIsActive(&g_CtxImGuiMenu)) {
                        inputCtxPopDeferred(&g_CtxImGuiMenu);
                    }
                }
            } else if (activated && diffLocked) {
                /* Action-bar button already played SND_SELECT; override
                 * with an error cue so the user knows the diff is locked. */
                pdguiPlaySound(PDGUI_SND_ERROR);
            }
        }
        pdguiEndActionBar();
    }
    ImGui::EndChild(); /* ms_right */

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Difficulty Selection
 * ========================================================================= */

static s32 renderDifficulty(struct menudialog *dialog,
                             struct menu *menu,
                             s32 winW, s32 winH)
{
    float sf  = pdguiScaleFactor();
    float mw  = pdguiMenuWidth() * 0.60f;   /* narrower dialog for difficulty */
    float mh  = pdguiMenuHeight() * 0.55f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_difficulty", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        /* Pre-select the last auto-difficulty */
        s32 autoD = SM_AUTODIFFICULTY(&g_GameFile);
        s_DiffSelectIdx = (autoD <= DIFF_PA) ? autoD : 0;
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_248), 1);

    pdguiSetCursorBelowTitle(titleH);

    /* Current stage name */
    s32 si = g_MissionConfig.stageindex;
    if (si >= 0 && si < NUM_SOLOSTAGES) {
        char stageName[128];
        snprintf(stageName, sizeof(stageName), "%s%s",
                 langSafe(g_SoloStages[si].name1),
                 langSafe(g_SoloStages[si].name2));
        ImGui::TextDisabled("%s", stageName);
    }
    ImGui::Separator();

    /* Show PD Mode option? Only when Skedar Ruins is beaten on PA */
    bool pdModeVisible = (g_GameFile.besttimes[SOLOSTAGEINDEX_SKEDARRUINS][DIFF_PA] != 0);

    /* Difficulty rows: Agent, Special Agent, Perfect Agent, [PD Mode], Cancel */
    static const s32 k_Diffs[]  = { DIFF_A, DIFF_SA, DIFF_PA };
    static const s32 k_DiffIds[] = { L_OPTIONS_251, L_OPTIONS_252, L_OPTIONS_253 };
    s32 numOptions = 3 + (pdModeVisible ? 1 : 0) + 1; /* +1 for Cancel */

    if (s_DiffSelectIdx < 0)           s_DiffSelectIdx = 0;
    if (s_DiffSelectIdx >= numOptions) s_DiffSelectIdx = numOptions - 1;

    /* Navigation */
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_DiffSelectIdx++;
        if (s_DiffSelectIdx >= numOptions) s_DiffSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_DiffSelectIdx--;
        if (s_DiffSelectIdx < 0) s_DiffSelectIdx = numOptions - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    float rowH  = pdguiScale(54.0f);

    /* ---- Difficulty rows ----
     * Batch 0 fix: use the hardcoded difficulty name ("Agent" / "Special
     * Agent" / "Perfect Agent") as a fallback when langSafe() returns an
     * empty string.  The previous renderer drew the diff name via
     * dl->AddText() as an overlay; when langSafe returned "" (lang bank
     * not yet resident at dialog open), the row appeared blank.  Now we
     * use an ImGui::Selectable with the label text directly so the text
     * is a real widget that ImGui clips, lays out, and renders through
     * its own path, and we use a non-empty hardcoded fallback when the
     * localized string is missing. */
    static const char *k_DiffFallbackNames[3] = {
        "Agent", "Special Agent", "Perfect Agent"
    };

    for (s32 i = 0; i < 3; i++) {
        s32 diff = k_Diffs[i];
        bool locked   = !isStageDifficultyUnlocked(si, diff);
        bool isActive = (s_DiffSelectIdx == i);

        ImGui::PushID(i);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH);
        }

        /* Build the row label: "  <DiffName>                    <BestTime>".
         * The leading spaces reserve visual space for a left gutter; the
         * trailing time is padded out with spaces so the right-hand text
         * aligns.  This is a single ImGui::Selectable so the text renders
         * via the normal ImGui path (no AddText overlay race). */
        const char *locName = langSafe(k_DiffIds[i]);
        if (!locName || !locName[0]) {
            locName = k_DiffFallbackNames[i];
        }

        char rowLabel[128];
        if (locked) {
            snprintf(rowLabel, sizeof(rowLabel), "  %s    [Locked]",
                     locName);
        } else {
            char timeStr[32];
            formatBestTime(timeStr, sizeof(timeStr),
                           g_GameFile.besttimes[si < NUM_SOLOSTAGES ? si : 0][diff]);
            snprintf(rowLabel, sizeof(rowLabel), "  %s    %s",
                     locName, timeStr);
        }

        /* Text color per state */
        ImVec4 textCol = locked ? ImVec4(0.55f, 0.55f, 0.65f, 1.0f)
                                : ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, textCol);

        bool clicked = ImGui::Selectable(rowLabel, isActive,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(0, rowH));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) { s_DiffSelectIdx = i; }

        /* Confirm from keyboard/gamepad */
        bool kbConfirm = isActive &&
            ImGui::IsKeyPressed(ImGuiKey_Enter, false);

        if ((clicked || kbConfirm) && !locked) {
            s_DiffSelectIdx = i;
            SM_CLEAR_PDMODE(&g_MissionConfig);
            SM_SET_DIFFICULTY(&g_MissionConfig, diff);
            lvSetDifficulty(diff);
            pdguiPlaySound(PDGUI_SND_SELECT);
            menuPopDialog();
            menuPushDialog(&g_AcceptMissionMenuDialog);
        } else if ((clicked || kbConfirm) && locked) {
            pdguiPlaySound(PDGUI_SND_ERROR);
        }

        /* Hover tooltip: objectives for this difficulty from g_Briefing.
         * g_Briefing is populated by the dialog handler before this runs. */
        if (!locked && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("%s", locName);
            ImGui::Separator();
            bool anyObj = false;
            for (s32 oi = 0; oi < 6; oi++) {
                if (g_Briefing.objectivenames[oi] == 0) continue;
                u16 bits = g_Briefing.objectivedifficulties[oi];
                if (bits & (1u << (unsigned)diff)) {
                    ImGui::TextUnformatted(langSafe(g_Briefing.objectivenames[oi]));
                    anyObj = true;
                }
            }
            if (!anyObj) {
                ImGui::TextDisabled("(No objectives)");
            }
            ImGui::EndTooltip();
        }

        /* Diff-color badge dot on the left gutter (overlays the row). */
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float dotR = pdguiScale(6.0f);
            float dotX = rmin.x + pdguiScale(12.0f);
            float dotY = rmin.y + rowH * 0.5f;
            dl->AddCircleFilled(ImVec2(dotX, dotY), dotR,
                locked ? IM_COL32(60, 60, 70, 160)
                       : k_DiffBadgeColor[diff]);
        }

        ImGui::PopID();
    }

    /* ---- PD Mode row (optional) ---- */
    if (pdModeVisible) {
        s32 pdIdx    = 3;
        bool isActive = (s_DiffSelectIdx == pdIdx);

        ImGui::PushID(0x10);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH);
        }

        /* Same Selectable-with-label pattern as Agent/SA/PA rows: the
         * visible text is a real ImGui widget (not AddText overlay), so
         * a langSafe() returning "" can't produce a blank row. */
        const char *pdLabel = langSafe(L_MPWEAPONS_221);
        if (!pdLabel || !pdLabel[0]) pdLabel = "PD Mode";

        char pdRowLabel[80];
        snprintf(pdRowLabel, sizeof(pdRowLabel), "  %s", pdLabel);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.70f, 1.0f, 1.0f));
        bool doSelect = ImGui::Selectable(pdRowLabel, isActive,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(0, rowH));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_DiffSelectIdx = pdIdx;
        if (isActive && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            doSelect = true;

        if (doSelect) {
            pdguiPlaySound(PDGUI_SND_OPENDIALOG);
            menuPushDialog(&g_PdModeSettingsMenuDialog);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    /* ---- Cancel row ----
     * Selectable-with-label pattern: label text is a real ImGui widget,
     * fallback string used if langSafe() returns empty. */
    {
        s32 cancelIdx = 3 + (pdModeVisible ? 1 : 0);
        bool isActive  = (s_DiffSelectIdx == cancelIdx);

        ImGui::PushID(0x20);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH * 0.8f);
        }

        const char *cancelLoc = langSafe(L_OPTIONS_254);
        if (!cancelLoc || !cancelLoc[0]) cancelLoc = "Cancel";

        char cancelRowLabel[64];
        snprintf(cancelRowLabel, sizeof(cancelRowLabel), "  %s", cancelLoc);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        bool doCancel = ImGui::Selectable(cancelRowLabel, isActive,
                                           ImGuiSelectableFlags_None,
                                           ImVec2(0, rowH * 0.8f));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_DiffSelectIdx = cancelIdx;
        if (isActive && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            doCancel = true;

        if (doCancel) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }

        ImGui::PopID();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * S194 Batch 2: Co-op / Counter-Op Mission Difficulty
 *
 * These two dialogs share the look of renderDifficulty (Agent / SA / PA
 * rows) but differ from the solo picker in three ways:
 *   1. No PD Mode row -- PD Mode is a solo-only modifier.
 *   2. Co-op checks isStageDifficultyUnlocked() per difficulty; Counter-Op
 *      does NOT gate on unlock (legacy CoopDifficulty checks, Anti does not).
 *   3. On confirm, the flow pushes g_CoopOptionsMenuDialog /
 *      g_AntiOptionsMenuDialog instead of g_AcceptMissionMenuDialog --
 *      the options pass comes first, THEN accept mission.
 *
 * Every pixel value uses pdguiScale() against the 1080p baseline.  The
 * popup-scrim primitive darkens the viewport behind the modal per the d5
 * "popups always darken" rule.
 * ========================================================================= */

static s32 s_CoopAntiDiffSelectIdx = 0;

static s32 renderCoopAntiDifficultyImpl(struct menudialog *dialog,
                                         struct menu *menu,
                                         s32 winW, s32 winH,
                                         bool isCoop,
                                         const char *windowId)
{
    (void)dialog; (void)menu; (void)winW; (void)winH;

    /* d5: popups always darken behind the modal. */
    pdguiPopupDarkenBehind(0.55f);

    float mw  = pdguiMenuWidth() * 0.60f;
    float mh  = pdguiMenuHeight() * 0.48f;  /* shorter: no PD Mode row */
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(windowId, nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_CoopAntiDiffSelectIdx = 0;  /* default to Agent */
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_248), 1);
    pdguiSetCursorBelowTitle(titleH);

    /* Stage name + mode subheader */
    s32 si = g_MissionConfig.stageindex;
    if (si >= 0 && si < NUM_SOLOSTAGES) {
        char stageName[128];
        snprintf(stageName, sizeof(stageName), "%s%s",
                 langSafe(g_SoloStages[si].name1),
                 langSafe(g_SoloStages[si].name2));
        ImGui::TextDisabled("%s", stageName);
    }
    ImGui::TextDisabled("%s", isCoop ? "Cooperative" : "Counter-Operative");
    ImGui::Separator();

    /* Difficulty rows: Agent, SA, PA, Cancel */
    static const s32 k_Diffs[]    = { DIFF_A, DIFF_SA, DIFF_PA };
    static const s32 k_DiffIds[]  = { L_OPTIONS_251, L_OPTIONS_252, L_OPTIONS_253 };
    static const char *k_DiffFallbackNames[3] = {
        "Agent", "Special Agent", "Perfect Agent"
    };

    const s32 numOptions = 3 + 1;  /* diffs + Cancel */

    if (s_CoopAntiDiffSelectIdx < 0)           s_CoopAntiDiffSelectIdx = 0;
    if (s_CoopAntiDiffSelectIdx >= numOptions) s_CoopAntiDiffSelectIdx = numOptions - 1;

    /* D-pad nav with wrap (d5 rule: circular wrapping always enabled) */
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_CoopAntiDiffSelectIdx = (s_CoopAntiDiffSelectIdx + 1) % numOptions;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_CoopAntiDiffSelectIdx = (s_CoopAntiDiffSelectIdx - 1 + numOptions) % numOptions;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    float rowH = pdguiScale(54.0f);

    for (s32 i = 0; i < 3; i++) {
        s32 diff = k_Diffs[i];
        /* Only Co-op checks unlock status (matches legacy CHECKDISABLED). */
        bool locked = isCoop ? !isStageDifficultyUnlocked(si, diff) : false;
        bool isActive = (s_CoopAntiDiffSelectIdx == i);

        ImGui::PushID(i);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH);
        }

        /* Selectable-with-label pattern (NOT dl->AddText overlay) so the
         * text is clipped and laid out by ImGui's own path -- rules out
         * the S192 "missing text" class of regressions. */
        const char *locName = langSafe(k_DiffIds[i]);
        if (!locName || !locName[0]) locName = k_DiffFallbackNames[i];

        char rowLabel[128];
        if (locked) {
            snprintf(rowLabel, sizeof(rowLabel), "  %s    [Locked]", locName);
        } else {
            snprintf(rowLabel, sizeof(rowLabel), "  %s", locName);
        }

        ImVec4 textCol = locked ? ImVec4(0.55f, 0.55f, 0.65f, 1.0f)
                                : ImVec4(1.00f, 1.00f, 1.00f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, textCol);
        bool clicked = ImGui::Selectable(rowLabel, isActive,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(0, rowH));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_CoopAntiDiffSelectIdx = i;

        bool kbConfirm = isActive &&
            ImGui::IsKeyPressed(ImGuiKey_Enter, false);

        if ((clicked || kbConfirm) && !locked) {
            s_CoopAntiDiffSelectIdx = i;
            SM_CLEAR_PDMODE(&g_MissionConfig);
            SM_SET_DIFFICULTY(&g_MissionConfig, diff);
            lvSetDifficulty(diff);
            pdguiPlaySound(PDGUI_SND_SELECT);
            menuPopDialog();
            menuPushDialog(isCoop ? &g_CoopOptionsMenuDialog
                                  : &g_AntiOptionsMenuDialog);
        } else if ((clicked || kbConfirm) && locked) {
            pdguiPlaySound(PDGUI_SND_ERROR);
        }

        /* Diff badge dot on the left gutter */
        {
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 rmin = ImGui::GetItemRectMin();
            float dotR = pdguiScale(6.0f);
            float dotX = rmin.x + pdguiScale(12.0f);
            float dotY = rmin.y + rowH * 0.5f;
            dl->AddCircleFilled(ImVec2(dotX, dotY), dotR,
                locked ? IM_COL32(60, 60, 70, 160)
                       : k_DiffBadgeColor[diff]);
        }

        ImGui::PopID();
    }

    ImGui::Separator();

    /* Cancel row */
    {
        s32 cancelIdx = 3;
        bool isActive = (s_CoopAntiDiffSelectIdx == cancelIdx);
        ImGui::PushID(0x20);

        if (isActive) {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH * 0.8f);
        }

        const char *cancelLoc = langSafe(L_OPTIONS_254);
        if (!cancelLoc || !cancelLoc[0]) cancelLoc = "Cancel";
        char cancelRowLabel[64];
        snprintf(cancelRowLabel, sizeof(cancelRowLabel), "  %s", cancelLoc);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
        bool doCancel = ImGui::Selectable(cancelRowLabel, isActive,
                                           ImGuiSelectableFlags_None,
                                           ImVec2(0, rowH * 0.8f));
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_CoopAntiDiffSelectIdx = cancelIdx;
        if (isActive && ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            doCancel = true;

        if (doCancel) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }

        ImGui::PopID();
    }

    ImGui::End();
    return 1;
}

static s32 renderCoopMissionDifficulty(struct menudialog *dialog,
                                        struct menu *menu,
                                        s32 winW, s32 winH)
{
    return renderCoopAntiDifficultyImpl(dialog, menu, winW, winH,
                                         /*isCoop=*/true, "##coop_difficulty");
}

static s32 renderAntiMissionDifficulty(struct menudialog *dialog,
                                        struct menu *menu,
                                        s32 winW, s32 winH)
{
    return renderCoopAntiDifficultyImpl(dialog, menu, winW, winH,
                                         /*isCoop=*/false, "##anti_difficulty");
}

/* =========================================================================
 * S194 Batch 2: Co-op / Counter-Op Options
 *
 * Two dialogs with similar structure.  Each has a small settings list
 * (checkboxes + one dropdown) followed by a Continue / Cancel choice.
 *
 * Co-op list:
 *   - Radar On        (checkbox, g_Vars.coopradaron)
 *   - Friendly Fire   (checkbox, g_Vars.coopfriendlyfire)
 *   - Perfect Buddy   (dropdown, g_Vars.numaibuddies / getMaxAiBuddies)
 *
 * Anti list:
 *   - Radar On        (checkbox, g_Vars.antiradaron)
 *   - Main Player     (dropdown, g_Vars.pendingantiplayernum^1)
 *
 * d5 compliance:
 *   - popup scrim behind the modal (pdguiPopupDarkenBehind 0.55)
 *   - Continue / Cancel docked in the action bar -- NEVER scroll.
 *     Uses pdguiBeginActionBar / pdguiActionBarButton primitives.
 *   - Every state read/write delegates to the legacy menu handler so the
 *     backing-store logic (modifiedfiles dirty flag, getMaxAiBuddies
 *     clamps, connected-controller math) stays in one place.
 *   - Every sizing via pdguiScale() against the 1080p baseline.
 * ========================================================================= */

static s32  s_CoopAntiOptSelectIdx = 0;  /* 0..N = items, N+1 = Continue, N+2 = Cancel */

/* Call a legacy menu handler.  The handler pointer is fetched directly
 * from the named function -- no items-array walking, no dialog pointer
 * indirection. */
static inline u32 s194_GetCheckbox(uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *))
{
    /* MENUOP_GET returns the boolean value as the return code. */
    return (u32)h(MENUOP_GET, nullptr, nullptr);
}

static inline void s194_SetCheckbox(uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *),
                                    u32 newVal)
{
    union s194_handlerdata hd;
    memset(&hd, 0, sizeof(hd));
    hd.checkbox.value = newVal;
    h(MENUOP_SET, nullptr, &hd);
}

static inline uintptr_t s194_GetDropdownCount(uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *))
{
    union s194_handlerdata hd;
    memset(&hd, 0, sizeof(hd));
    h(MENUOP_GETOPTIONCOUNT, nullptr, &hd);
    return hd.dropdown.value;
}

static inline uintptr_t s194_GetDropdownSelected(uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *))
{
    union s194_handlerdata hd;
    memset(&hd, 0, sizeof(hd));
    h(MENUOP_GETSELECTEDINDEX, nullptr, &hd);
    return hd.dropdown.value;
}

static inline const char *s194_GetDropdownOptionText(uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *),
                                                      uintptr_t idx)
{
    union s194_handlerdata hd;
    memset(&hd, 0, sizeof(hd));
    hd.dropdown.value = idx;
    /* Return value is a uintptr_t encoding either a lang id OR a const char*.
     * Legacy handlers typically return langGet(langId) which IS a const char*. */
    uintptr_t rv = h(MENUOP_GETOPTIONTEXT, nullptr, &hd);
    return reinterpret_cast<const char *>(rv);
}

static inline void s194_SetDropdownIndex(uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *),
                                         uintptr_t idx)
{
    union s194_handlerdata hd;
    memset(&hd, 0, sizeof(hd));
    hd.dropdown.value = idx;
    h(MENUOP_SET, nullptr, &hd);
}

/* Shared options renderer.  numItems = 0..3 row items above the
 * Continue/Cancel docked action bar. */
static s32 renderCoopAntiOptionsImpl(struct menudialog *dialog,
                                      struct menu *menu,
                                      s32 winW, s32 winH,
                                      bool isCoop,
                                      const char *windowId,
                                      const char *fallbackTitle)
{
    (void)dialog; (void)menu; (void)winW; (void)winH;

    pdguiPopupDarkenBehind(0.55f);

    float mw  = pdguiMenuWidth() * 0.60f;
    float mh  = pdguiMenuHeight() * 0.55f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(windowId, nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_CoopAntiOptSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    /* Title: lang id (L_OPTIONS_255 coop / L_OPTIONS_266 anti). */
    float titleH = pdguiScale(39.0f);
    s32 titleLang = isCoop ? L_OPTIONS_255 : L_OPTIONS_266;
    const char *title = langSafe(titleLang);
    if (!title || !title[0]) title = fallbackTitle;
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, title, 1);
    pdguiSetCursorBelowTitle(titleH);

    /* Item count: coop has 3 rows (radar, ff, buddy), anti has 2. */
    const s32 numRows      = isCoop ? 3 : 2;
    const s32 numContinue  = numRows;       /* Continue index */
    const s32 numCancel    = numRows + 1;   /* Cancel   index */
    const s32 numFocusable = numRows + 2;   /* total */

    if (s_CoopAntiOptSelectIdx < 0)            s_CoopAntiOptSelectIdx = 0;
    if (s_CoopAntiOptSelectIdx >= numFocusable)
        s_CoopAntiOptSelectIdx = numFocusable - 1;

    /* D-pad nav with wrap (d5 circular wrapping rule). */
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_CoopAntiOptSelectIdx = (s_CoopAntiOptSelectIdx + 1) % numFocusable;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_CoopAntiOptSelectIdx =
            (s_CoopAntiOptSelectIdx - 1 + numFocusable) % numFocusable;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    /* B / Escape closes the dialog (d5 back rule). */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }
    /* ---- Body (scrollable if it ever grows beyond the window) ---- */
    float avail = ImGui::GetContentRegionAvail().y;
    float bodyH = pdguiBodyHeightForActionBar(avail);

    if (ImGui::BeginChild("##coopanti_body", ImVec2(0, bodyH),
                          ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoBackground)) {
        float rowH = pdguiScale(48.0f);  /* gamepad-friendly min target */

        s32 rowIdx = 0;

        /* ---- Radar On ---- */
        {
            bool isActive = (s_CoopAntiOptSelectIdx == rowIdx);
            ImGui::PushID(rowIdx);

            if (isActive) {
                ImVec2 cp = ImGui::GetCursorScreenPos();
                pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH);
            }

            uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *) =
                isCoop ? menuhandlerCoopRadar : menuhandlerAntiRadar;
            u32 radarOn = s194_GetCheckbox(h);
            s32 radarLang = isCoop ? L_OPTIONS_256 : L_OPTIONS_267;
            const char *label = langSafe(radarLang);
            if (!label || !label[0]) label = "Radar On";

            char rowLabel[128];
            snprintf(rowLabel, sizeof(rowLabel), "  %s    [%s]",
                     label, radarOn ? "ON" : "OFF");

            bool clicked = ImGui::Selectable(rowLabel, isActive,
                                              ImGuiSelectableFlags_None,
                                              ImVec2(0, rowH));
            if (ImGui::IsItemHovered()) s_CoopAntiOptSelectIdx = rowIdx;

            bool kbToggle = isActive &&
                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_RightArrow, false));

            if (clicked || kbToggle) {
                s194_SetCheckbox(h, radarOn ? 0u : 1u);
                pdguiPlaySound(radarOn ? PDGUI_SND_TOGGLEOFF : PDGUI_SND_TOGGLEON);
            }

            ImGui::PopID();
            rowIdx++;
        }

        /* ---- Coop-only: Friendly Fire ---- */
        if (isCoop) {
            bool isActive = (s_CoopAntiOptSelectIdx == rowIdx);
            ImGui::PushID(rowIdx);

            if (isActive) {
                ImVec2 cp = ImGui::GetCursorScreenPos();
                pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH);
            }

            u32 ffOn = s194_GetCheckbox(menuhandlerCoopFriendlyFire);
            const char *label = langSafe(L_OPTIONS_257);
            if (!label || !label[0]) label = "Friendly Fire";

            char rowLabel[128];
            snprintf(rowLabel, sizeof(rowLabel), "  %s    [%s]",
                     label, ffOn ? "ON" : "OFF");

            bool clicked = ImGui::Selectable(rowLabel, isActive,
                                              ImGuiSelectableFlags_None,
                                              ImVec2(0, rowH));
            if (ImGui::IsItemHovered()) s_CoopAntiOptSelectIdx = rowIdx;

            bool kbToggle = isActive &&
                (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_Space, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
                 ImGui::IsKeyPressed(ImGuiKey_RightArrow, false));

            if (clicked || kbToggle) {
                s194_SetCheckbox(menuhandlerCoopFriendlyFire, ffOn ? 0u : 1u);
                pdguiPlaySound(ffOn ? PDGUI_SND_TOGGLEOFF : PDGUI_SND_TOGGLEON);
            }

            ImGui::PopID();
            rowIdx++;
        }

        /* ---- Dropdown row (Perfect Buddy coop / Main Player anti) ---- */
        {
            bool isActive = (s_CoopAntiOptSelectIdx == rowIdx);
            ImGui::PushID(rowIdx);

            if (isActive) {
                ImVec2 cp = ImGui::GetCursorScreenPos();
                pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), rowH);
            }

            uintptr_t (*h)(s32, struct s194_menuitem *, union s194_handlerdata *) =
                isCoop ? menuhandlerCoopBuddy : menuhandlerAntiMainPlayer;

            /* Fetch count & current index from the handler. */
            uintptr_t ddCount = s194_GetDropdownCount(h);
            uintptr_t ddCur   = s194_GetDropdownSelected(h);
            if ((s32)ddCur < 0)          ddCur = 0;
            if (ddCount > 0 && ddCur >= ddCount) ddCur = ddCount - 1;

            const char *curText = s194_GetDropdownOptionText(h, ddCur);
            if (!curText || !curText[0]) curText = "(unknown)";

            const char *label = isCoop ? langSafe(L_OPTIONS_258)
                                       : "Main Player"; /* anti uses literal */
            if (isCoop && (!label || !label[0])) label = "Perfect Buddy";

            char rowLabel[192];
            snprintf(rowLabel, sizeof(rowLabel), "  %s    < %s >",
                     label, curText);

            bool clicked = ImGui::Selectable(rowLabel, isActive,
                                              ImGuiSelectableFlags_None,
                                              ImVec2(0, rowH));
            if (ImGui::IsItemHovered()) s_CoopAntiOptSelectIdx = rowIdx;

            /* Left/Right decrements/increments the dropdown value.
             * A / Enter cycles forward (advance one step). */
            bool decrement = isActive && ddCount > 1 &&
                ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);
            bool increment = isActive && ddCount > 1 &&
                (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) ||
                 ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                 clicked);

            if (decrement) {
                uintptr_t next = (ddCur == 0) ? (ddCount - 1) : (ddCur - 1);
                s194_SetDropdownIndex(h, next);
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            } else if (increment) {
                uintptr_t next = (ddCur + 1 >= ddCount) ? 0 : (ddCur + 1);
                s194_SetDropdownIndex(h, next);
                pdguiPlaySound(PDGUI_SND_SUBFOCUS);
            }

            ImGui::PopID();
            rowIdx++;
        }
    }
    ImGui::EndChild();

    /* ---- Docked action bar: Continue / Cancel ----
     * d5 rule: primary action NEVER scrolls.  pdguiBeginActionBar pins
     * this row to the bottom of the window at pdguiActionBarHeight(). */
    if (pdguiBeginActionBar("##coopanti_ab")) {
        float avail = ImGui::GetContentRegionAvail().x;
        float half  = avail * 0.5f;

        bool continueActive = (s_CoopAntiOptSelectIdx == numContinue);
        bool cancelActive   = (s_CoopAntiOptSelectIdx == numCancel);

        const char *contLang = isCoop ? langSafe(L_OPTIONS_259) : langSafe(L_OPTIONS_269);
        if (!contLang || !contLang[0]) contLang = "Continue";
        const char *cancLang = isCoop ? langSafe(L_OPTIONS_260) : langSafe(L_OPTIONS_270);
        if (!cancLang || !cancLang[0]) cancLang = "Cancel";

        bool doContinue = false;
        bool doCancel   = false;

        if (pdguiActionBarButton(contLang, continueActive, half)) {
            doContinue = true;
        }
        ImGui::SameLine();
        if (pdguiActionBarButton(cancLang, cancelActive, ImGui::GetContentRegionAvail().x)) {
            doCancel = true;
        }

        /* Keyboard / gamepad confirm on the focused action button */
        if (continueActive && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            doContinue = true;
        }
        if (cancelActive && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            doCancel = true;
        }

        if (doContinue) {
            /* Delegate to the legacy handler: pops dialog, pushes
             * g_AcceptMissionMenuDialog.  PDGUI_SND_SELECT already fired
             * inside pdguiActionBarButton on click, but fire again on the
             * kb/Start paths so audio feedback is consistent. */
            menuhandlerBuddyOptionsContinue(MENUOP_SET, nullptr, nullptr);
        } else if (doCancel) {
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
    }
    pdguiEndActionBar();

    ImGui::End();
    return 1;
}

static s32 renderCoopOptions(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    return renderCoopAntiOptionsImpl(dialog, menu, winW, winH,
                                      /*isCoop=*/true,
                                      "##coop_options",
                                      "Co-Operative Options");
}

static s32 renderAntiOptions(struct menudialog *dialog,
                              struct menu *menu,
                              s32 winW, s32 winH)
{
    return renderCoopAntiOptionsImpl(dialog, menu, winW, winH,
                                      /*isCoop=*/false,
                                      "##anti_options",
                                      "Counter-Operative Options");
}

/* =========================================================================
 * Briefing (shared by g_SoloMissionBriefingMenuDialog and
 *            g_PreAndPostMissionBriefingMenuDialog)
 * g_Briefing is populated before this renders by the dialog's C handler.
 * ========================================================================= */

static s32 renderBriefingImpl(struct menudialog *dialog,
                               struct menu *menu,
                               s32 winW, s32 winH,
                               const char *windowId)
{
    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin(windowId, nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_BriefingScroll = 0.0f;
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_247), 1);

    pdguiSetCursorBelowTitle(titleH);
    ImGui::Separator();

    /* Close with B / Escape */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    /* Scrollable briefing text */
    float footerH = pdguiScale(48.0f);
    float bodyH   = mh - titleH - pdguiScale(36.0f) - footerH;

    if (ImGui::BeginChild("##briefing_scroll", ImVec2(0, bodyH), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        const char *txt = (g_Briefing.briefingtextnum != 0)
                          ? langSafe(g_Briefing.briefingtextnum)
                          : "(No briefing text available)";

        ImGui::PushTextWrapPos(mw - pdguiScale(60.0f));
        ImGui::TextUnformatted(txt);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled("D-Pad/Scroll: Read   B/Esc: Close");

    ImGui::End();
    return 1;
}

static s32 renderSoloBriefing(struct menudialog *dialog,
                               struct menu *menu, s32 winW, s32 winH)
{
    return renderBriefingImpl(dialog, menu, winW, winH, "##solo_briefing");
}

static s32 renderPrePostBriefing(struct menudialog *dialog,
                                  struct menu *menu, s32 winW, s32 winH)
{
    return renderBriefingImpl(dialog, menu, winW, winH, "##prepost_briefing");
}

/* =========================================================================
 * Inventory Screen (M1.2)
 *
 * Replaces the legacy 3D weapon-model inventory dialog with an ImGui
 * scrollable list.  Shows all weapons/items in the player's inventory
 * during a solo mission (pause → Inventory).
 * ========================================================================= */

static s32 renderInventory(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_inventory", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, langSafe(L_OPTIONS_178), 1);

    pdguiSetCursorBelowTitle(titleH);
    ImGui::Separator();

    /* Scrollable weapon/item list */
    float footerH = pdguiScale(48.0f);
    float bodyH   = mh - titleH - pdguiScale(36.0f) - footerH;

    if (ImGui::BeginChild("##inv_scroll", ImVec2(0, bodyH), false, 0)) {
        s32 count = invGetCount();
        if (count <= 0) {
            ImGui::TextDisabled("(No items)");
        } else {
            float scale = pdguiScaleFactor();
            float rowH  = 28.0f * scale;
            float availW = mw - ImGui::GetStyle().WindowPadding.x * 2.0f;

            for (s32 i = 0; i < count; i++) {
                char *name = invGetNameByIndex(i);
                if (!name || !name[0]) continue;

                ImGui::PushID(i);

                /* Row background — alternating subtle shade */
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImVec2 rowMax = ImVec2(rowMin.x + availW, rowMin.y + rowH);
                if (i & 1) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        rowMin, rowMax, IM_COL32(255, 255, 255, 8));
                }

                /* Weapon/item name */
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                                     (rowH - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::TextUnformatted(name);

                ImGui::SetCursorPosY(rowMin.y - pos.y + rowH);
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextDisabled("B/Esc: Close");

    /* B / Escape = dismiss */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Accept Mission (Overview)
 * Shows mission objectives + Accept / Decline.
 * g_Briefing populated by menudialog00103608 before this renders.
 * ========================================================================= */

static s32 renderAcceptMission(struct menudialog *dialog,
                                struct menu *menu,
                                s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##accept_mission", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_AcceptSelectIdx = 0;  /* default to Accept */
    }

    /* Title = "StageName: Overview" */
    char title[128] = "Overview";
    {
        s32 si = g_MissionConfig.stageindex;
        if (si >= 0 && si < NUM_SOLOSTAGES)
            snprintf(title, sizeof(title), "%s: %s",
                     langSafe(g_SoloStages[si].name3),
                     langSafe(L_OPTIONS_273));
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, title, 1);
    pdguiSetCursorBelowTitle(titleH);

    /* Difficulty badge */
    {
        s32 d = SM_DIFFICULTY(&g_MissionConfig);
        const char *diffNames[] = { "Agent", "Special Agent", "Perfect Agent" };
        const char *dn = (d <= DIFF_PA) ? diffNames[d] : "Agent";
        ImGui::TextDisabled("Difficulty: %s", dn);
    }
    ImGui::Separator();

    /* Navigation */
    bool doAccept  = false;
    bool doDecline = false;

    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_AcceptSelectIdx = 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_AcceptSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (s_AcceptSelectIdx == 0) doAccept  = true;
        else                        doDecline = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        doDecline = true;
    }

    if (doAccept) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        menuhandlerAcceptMission(MENUOP_SET, nullptr, nullptr);
        if (inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
        }
        ImGui::End();
        return 1;
    }
    if (doDecline) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* ---- Objectives list ---- */
    float btnH    = pdguiScale(57.0f);
    float bodyH   = mh - titleH - pdguiScale(75.0f) - btnH * 2.0f - pdguiScale(24.0f);

    if (ImGui::BeginChild("##objectives_scroll", ImVec2(0, bodyH), false,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        bool anyObj = false;
        for (s32 i = 0; i < 6; i++) {
            if (g_Briefing.objectivenames[i] == 0) continue;
            anyObj = true;

            const char *objText = langSafe(g_Briefing.objectivenames[i]);

            /* Difficulty indicators */
            u16 bits = g_Briefing.objectivedifficulties[i];
            char diffStr[16] = "";
            if (bits & 1) strcat(diffStr, "A ");
            if (bits & 2) strcat(diffStr, "SA ");
            if (bits & 4) strcat(diffStr, "PA");

            float dotSz = pdguiScale(12.0f);
            ImVec2 cp = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            dl->AddCircleFilled(
                ImVec2(cp.x + dotSz * 0.5f + pdguiScale(6.0f),
                       cp.y + ImGui::GetTextLineHeight() * 0.5f + pdguiScale(3.0f)),
                dotSz * 0.5f,
                pdguiImU32TintInfo(220));

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dotSz + pdguiScale(15.0f));
            ImGui::PushTextWrapPos(mw - pdguiScale(90.0f));
            ImGui::TextUnformatted(objText);
            ImGui::PopTextWrapPos();

            if (diffStr[0]) {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", diffStr);
            }
            ImGui::Spacing();
        }

        if (!anyObj) {
            ImGui::TextDisabled("(No objectives data)");
        }
    }
    ImGui::EndChild();

    ImGui::Separator();

    /* ---- Accept / Decline buttons ---- */
    float btnW  = (mw - ImGui::GetStyle().WindowPadding.x * 2.0f - pdguiScale(15.0f)) * 0.5f;

    auto drawBtn = [&](s32 idx, const char *lbl, ImU32 hlCol) {
        bool sel = (s_AcceptSelectIdx == idx);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (sel) pdguiDrawItemHighlight(cp.x, cp.y, btnW, btnH);

        bool clicked = ImGui::Button(lbl, ImVec2(btnW, btnH));
        if (ImGui::IsItemHovered()) s_AcceptSelectIdx = idx;
        return clicked;
    };

    if (drawBtn(0, langSafe(L_OPTIONS_274), pdguiImU32TintSuccess(255))) {
        pdguiPlaySound(PDGUI_SND_SELECT);
        menuhandlerAcceptMission(MENUOP_SET, nullptr, nullptr);
        if (inputCtxIsActive(&g_CtxImGuiMenu)) {
            inputCtxPopDeferred(&g_CtxImGuiMenu);
        }
        ImGui::End();
        return 1;
    }
    ImGui::SameLine(0.0f, pdguiScale(15.0f));
    if (drawBtn(1, langSafe(L_OPTIONS_275), pdguiImU32TintDanger(255))) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Pause Menu
 * Shows stage name, objectives (with completion status), and navigation
 * to Inventory / Options, plus the Abort button.
 * g_Briefing populated by soloMenuDialogPauseStatus before this renders.
 *
 * Gap 8 (investigation 2026-04-16) — input context asymmetry:
 *   Solo pause enters via menuPushRootDialog(&g_SoloMissionPauseMenuDialog,
 *   MENUROOT_MAINMENU), which walks the legacy push path and pushes
 *   &g_CtxImGuiMenu (priority 10). It does NOT push &g_CtxPauseMenu
 *   (priority 11) the way MP pause does via pdguiPauseMenuOpen().
 *   Consequence: &g_ImcPauseMenu never activates for solo and
 *   s_GamePaused stays 0. This is INTENTIONAL for now:
 *     - Solo pause is tied to MENUROOT_MAINMENU (player presses Start ->
 *       mainmenu root, not a dedicated pause modal). Its lifecycle is
 *       owned by the legacy menu stack, not by pdguiPauseMenuOpen/Close.
 *     - g_ImcPauseMenu was added for MP's dedicated pause window, which
 *       does NOT freeze the net tick. Solo pause does freeze the game
 *       loop, so the extra IMC buys nothing.
 *   Planned unification: Phase 2 menu-pool ADR
 *   (designs/input-authority-and-menu-pool-2026-04-13.md) will migrate
 *   both paths onto a single pause context. Until then, any code that
 *   assumes "pause => g_CtxPauseMenu active" must also consult the
 *   solo-pause menu-stack state (PAUSEMODE_PAUSING / MENUROOT_MAINMENU).
 * ========================================================================= */

static s32 renderPauseMenu(struct menudialog *dialog,
                            struct menu *menu,
                            s32 winW, s32 winH)
{
    /* P8: Semi-transparent backdrop overlay — dims the frozen game frame
     * so the pause menu is clearly readable over gameplay. */
    {
        ImDrawList *bgDl = ImGui::GetBackgroundDrawList();
        bgDl->AddRectFilled(
            ImVec2(0.0f, 0.0f),
            ImVec2((float)winW, (float)winH),
            pdguiPalImU32(PDPAL_BODYBG, 140));
    }

    float mw  = pdguiMenuWidth();
    float mh  = pdguiMenuHeight();
    ImVec2 pos = pdguiMenuPos();

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_pause", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    /* S369 + CI-load class: ctx every frame the pause window is open, not only
     * IsWindowAppearing (same menupool S300 attach semantics as main menu). */
    menupoolAcquireDialog(menupoolDialogDef(dialog), &g_CtxImGuiMenu);

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        /* S308: reset ALL pause-state statics on every fresh open so the
         * Restart-Confirm overlay and selection cursor can't leak between
         * mission opens (prior impl only reset s_PauseSelectIdx).
         * M-6-A: replaced s_RestartConfirm/s_RestartSelectIdx with
         * s_RestartOpenFrame tracker (popup modal path). */
        s_PauseSelectIdx    = 0;
        s_RestartOpenFrame  = -1;
        pdguiPlaySound(PDGUI_SND_OPENDIALOG);
    }

    /* Title: "StageName: Status" */
    char title[128] = "Status";
    {
        s32 si = g_MissionConfig.stageindex;
        if (si >= 0 && si < NUM_SOLOSTAGES) {
            const char *sname = langSafe(g_SoloStages[si].name3);
            const char *slabel = langSafe(L_OPTIONS_172);
            /* Guard: if the lang bank hasn't resolved the stage name (empty
             * string) fall back to "Mission %d" so the title never reads
             * ": Status" with leading colon. */
            if (sname && sname[0]) {
                snprintf(title, sizeof(title), "%s: %s",
                         sname, (slabel && slabel[0]) ? slabel : "Status");
            } else {
                snprintf(title, sizeof(title), "Mission %d: %s",
                         (int)(si + 1),
                         (slabel && slabel[0]) ? slabel : "Status");
            }
        }
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, title, 1);
    pdguiSetCursorBelowTitle(titleH);

    /* Difficulty badge */
    {
        s32 d = SM_DIFFICULTY(&g_MissionConfig);
        const char *dnames[] = { "Agent", "Special Agent", "Perfect Agent" };
        ImGui::TextDisabled("Difficulty: %s", (d <= DIFF_PA) ? dnames[d] : "?");
    }
    ImGui::Separator();

    /* Nav items: Resume + Restart + Inventory + Options + Abort */
    static const s32 k_NumPauseItems = 5;
    if (s_PauseSelectIdx < 0)                s_PauseSelectIdx = 0;
    if (s_PauseSelectIdx >= k_NumPauseItems) s_PauseSelectIdx = k_NumPauseItems - 1;

    /* Navigation */
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
        s_PauseSelectIdx++;
        if (s_PauseSelectIdx >= k_NumPauseItems) s_PauseSelectIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
        s_PauseSelectIdx--;
        if (s_PauseSelectIdx < 0) s_PauseSelectIdx = k_NumPauseItems - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    bool doConfirm = ImGui::IsKeyPressed(ImGuiKey_Enter, false);

    /* ---- Objectives with completion status ---- */
    /* Height = total - title - separators/padding - 5 action buttons */
    float objH = mh - titleH - pdguiScale(90.0f) - pdguiScale(57.0f * 5.0f);
    if (objH < pdguiScale(90.0f)) objH = pdguiScale(90.0f);

    s32 curDiff = lvGetDifficulty();
    s32 objCount = objectiveGetCount();

    /* Priority L (2026-04-25): NavFlattened layout panel. */
    if (ImGui::BeginChild("##pause_obj", ImVec2(0, objH),
                           ImGuiChildFlags_NavFlattened,
                           ImGuiWindowFlags_None)) {
        bool anyObj = false;
        /* objectivenames[0] is the briefing text; objectives are indices 1-5.
         * Each maps to runtime objective index (i-1) for objectiveCheck(). */
        for (s32 i = 1; i < 6; i++) {
            if (g_Briefing.objectivenames[i] == 0) continue;
            /* Skip objectives not active at current difficulty */
            if (g_Briefing.objectivedifficulties[i] != 0 &&
                    !((g_Briefing.objectivedifficulties[i] >> curDiff) & 1)) {
                continue;
            }
            anyObj = true;

            /* Completion status for runtime objective index (0-based) */
            s32 objIdx = i - 1;
            s32 status = (objIdx < objCount) ? objectiveCheck(objIdx) : 0;
            /* status: 0=INCOMPLETE, 1=COMPLETE, 2=FAILED */

            float iconSz = pdguiScale(15.0f);
            ImVec2 cp    = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            float iconCx = cp.x + iconSz * 0.5f + pdguiScale(6.0f);
            float iconCy = cp.y + ImGui::GetTextLineHeight() * 0.5f + pdguiScale(1.5f);

            if (status == 1) {
                /* COMPLETE — green circle with checkmark */
                dl->AddCircleFilled(ImVec2(iconCx, iconCy), iconSz * 0.5f,
                                    pdguiImU32TintSuccess(230));
                float r = iconSz * 0.26f;
                dl->AddLine(ImVec2(iconCx - r, iconCy),
                            ImVec2(iconCx - r * 0.2f, iconCy + r),
                            IM_COL32(255, 255, 255, 240), 1.5f);
                dl->AddLine(ImVec2(iconCx - r * 0.2f, iconCy + r),
                            ImVec2(iconCx + r, iconCy - r),
                            IM_COL32(255, 255, 255, 240), 1.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintSuccess());
            } else if (status == 2) {
                /* FAILED — red circle with X */
                dl->AddCircleFilled(ImVec2(iconCx, iconCy), iconSz * 0.5f,
                                    pdguiImU32TintDanger(220));
                float r = iconSz * 0.24f;
                dl->AddLine(ImVec2(iconCx - r, iconCy - r),
                            ImVec2(iconCx + r, iconCy + r),
                            IM_COL32(255, 255, 255, 230), 1.5f);
                dl->AddLine(ImVec2(iconCx + r, iconCy - r),
                            ImVec2(iconCx - r, iconCy + r),
                            IM_COL32(255, 255, 255, 230), 1.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintDanger());
            } else {
                /* INCOMPLETE — blue pending dot */
                dl->AddCircleFilled(ImVec2(iconCx, iconCy), iconSz * 0.5f,
                                    pdguiImU32TintInfo(180));
                ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintInfo(230));
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconSz + pdguiScale(15.0f));
            ImGui::PushTextWrapPos(mw - pdguiScale(24.0f));
            ImGui::TextUnformatted(langSafe(g_Briefing.objectivenames[i]));
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        if (!anyObj) {
            ImGui::TextDisabled("(No objectives)");
        }
    }
    ImGui::EndChild();
    ImGui::Separator();

    /* ---- Action buttons ---- */
    float btnH = pdguiScale(54.0f);

    /* S308: resolve lang labels once with hard-coded fallbacks, so a missing
     * lang bank can't paint blank buttons. Visible fallbacks match the
     * English equivalents of the L_OPTIONS_* ids. */
    const char *invLabel   = langSafe(L_OPTIONS_178);
    const char *abortLabel = langSafe(L_OPTIONS_173);
    if (!invLabel   || !invLabel[0])   invLabel   = "Inventory";
    if (!abortLabel || !abortLabel[0]) abortLabel = "Abort Mission";

    struct PauseBtn { const char *label; s32 idx; };
    const PauseBtn k_Btns[] = {
        { "Resume",          0 },
        { "Restart Mission", 1 },
        { invLabel,          2 },
        { "Settings",        3 },
        { abortLabel,        4 },
    };

    for (s32 b = 0; b < 5; b++) {
        bool isSel = (s_PauseSelectIdx == k_Btns[b].idx);
        ImGui::PushID(b);

        ImVec2 cp = ImGui::GetCursorScreenPos();
        if (isSel) pdguiDrawItemHighlight(cp.x, cp.y, mw - pdguiScale(24.0f), btnH);

        /* Abort gets a red tint */
        if (b == 4) ImGui::PushStyleColor(ImGuiCol_Text, pdguiVec4TintDanger());

        bool clicked = ImGui::Button(k_Btns[b].label,
                                     ImVec2(mw - pdguiScale(24.0f), btnH));
        if (b == 4) ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) s_PauseSelectIdx = k_Btns[b].idx;

        bool doThis = clicked || (isSel && doConfirm);
        if (doThis) {
            switch (b) {
            case 0: /* Resume */
                pdguiPlaySound(PDGUI_SND_KBCANCEL);
                menuPopDialog();
                break;
            case 1: /* Restart Mission — M-6-A: open canonical S385 confirm modal */
                ImGui::OpenPopup("Restart Mission?##restart");
                s_RestartOpenFrame = (s32)ImGui::GetFrameCount();
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                break;
            case 2: /* Inventory (ImGui weapon list — M1.2) */
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                menuPushDialog(&g_SoloMissionInventoryMenuDialog);
                break;
            case 3: /* Options */
                pdguiPlaySound(PDGUI_SND_OPENDIALOG);
                menuPushDialog(&g_SoloMissionOptionsMenuDialog);
                break;
            case 4: /* Abort! */
                pdguiPlaySound(PDGUI_SND_ERROR);
                menuPushDialog(&g_MissionAbortMenuDialog);
                break;
            }
            ImGui::PopID();
            ImGui::End();
            return 1;
        }

        ImGui::PopID();
    }

    /* B button / Escape = Resume (unless restart confirm is showing).
     * M-6-A: restart confirm is now a popup modal — suppress the pause-level
     * cancel while the popup is open so Esc routes to the modal only. */
    if (!ImGui::IsPopupOpen("Restart Mission?##restart") &&
        (actionPressed(0, ACTION_CANCEL_USE) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        menuPopDialog();
        ImGui::End();
        return 1;
    }
    pdguiNavTickWrap();

    /* M-6-A: Restart-Mission confirmation modal — canonical S385 pattern
     * via pdguiRenderConfirmModal().  Rendered before End() so the popup
     * is parented to the pause window. */
    s32 restartRes = pdguiRenderConfirmModal(
        "Restart Mission?##restart",
        "Restart Mission?",
        "Restart the current mission from the beginning? All progress will be lost.",
        "Restart",
        &s_RestartOpenFrame);

    ImGui::End();

    if (restartRes == PDGUI_CONFIRM_OK) {
        s32 rsn = (s32)(u8)g_MissionConfig.stagenum;
        if (g_MissionConfig.stage_id[0] != '\0') {
            catalog_stage_result_t sr;
            if (catalogResolveStage(g_MissionConfig.stage_id, &sr)) rsn = sr.stagenum;
        }
        mainChangeToStage(rsn);
    }

    return 1;
}

/* =========================================================================
 * Abort Mission (M-2: BeginPopupModal over pause menu)
 *
 * Mirrors the M-1 renderMpEndGameDialog pattern in pdgui_menu_warning.cpp:
 *   - ImGui::OpenPopup + BeginPopupModal for popup-over-parent semantics so
 *     the Solo pause is visually underneath the scrim.
 *   - pdguiPopupDarkenBehind(0.65f) scrim darkens the viewport.
 *   - Red/danger palette inside the modal body.
 *   - 5-frame SetKeyboardFocusHere(0) on Cancel so controller focus lands
 *     reliably even if ImGui popup NavInit hasn't settled.
 *   - 3-frame input debounce so the Enter/A press that opened the popup
 *     can't bleed through into Confirm.
 * ========================================================================= */

static s32 renderAbortMission(struct menudialog *dialog,
                               struct menu * /*menu*/,
                               s32 /*winW*/, s32 /*winH*/)
{
    struct menudialogdef *def = (dialog != nullptr)
        ? *(struct menudialogdef **)((u8 *)dialog)
        : nullptr;
    (void)def;

    /* Full-viewport scrim — dim the pause menu / game scene behind the modal. */
    pdguiPopupDarkenBehind(0.65f);

    /* Red / warning palette for the frame and title. */
    s32 prevPalette = pdguiGetPalette();
    pdguiSetPalette(2);

    float scale = pdguiScaleFactor();
    float dialogW = pdguiScale(540.0f);
    float dialogH = pdguiScale(260.0f);
    ImVec2 dlgPos = pdguiCenterPos(dialogW, dialogH);

    float pdTitleH = pdguiScale(36.0f);
    if (pdTitleH < 18.0f) pdTitleH = 18.0f;

    const char *popupId = "##mission_abort_modal";
    s32 curFrame = (s32)ImGui::GetFrameCount();

    /* Kick the popup open on first frame the dialog is seen. */
    if (s_AbortOpenedForDialog != (void *)dialog) {
        ImGui::OpenPopup(popupId);
        s_AbortOpenedForDialog = (void *)dialog;
        s_AbortOpenFrame = curFrame;
        pdguiPlaySound(PDGUI_SND_ERROR);
    }

    ImGui::SetNextWindowPos(dlgPos);
    ImGui::SetNextWindowSize(ImVec2(dialogW, dialogH));

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoSavedSettings
                            | ImGuiWindowFlags_NoTitleBar
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_NoScrollbar;

    bool open = ImGui::BeginPopupModal(popupId, nullptr, wflags);
    if (!open) {
        /* Popup dismissed externally (hotswap teardown etc.) — drop the
         * dialog from the legacy stack so the pause menu returns cleanly. */
        if (s_AbortOpenedForDialog == (void *)dialog) {
            s_AbortOpenedForDialog = nullptr;
            s_AbortOpenFrame = -1;
            pdguiPlaySound(PDGUI_SND_KBCANCEL);
            menuPopDialog();
        }
        pdguiSetPalette(prevPalette);
        return 1;
    }

    float dialogX = ImGui::GetWindowPos().x;
    float dialogY = ImGui::GetWindowPos().y;

    /* Opaque backdrop behind the PD-authentic frame. */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(dialogX, dialogY),
                          ImVec2(dialogX + dialogW, dialogY + dialogH),
                          pdguiPalImU32(PDPAL_BODYBG, 255), 0.0f);
    }

    /* PD-authentic frame + title. */
    const char *title = langSafe(L_OPTIONS_174);
    if (!title || !title[0]) title = "Warning";
    pdguiDrawPdDialog(dialogX, dialogY, dialogW, dialogH, title, 1);
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        pdguiDrawTextGlow(dialogX + 8.0f, dialogY + 2.0f,
                          dialogW - 16.0f, pdTitleH - 4.0f);
        ImVec2 titleSize = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(dialogX + (dialogW - titleSize.x) * 0.5f,
                           dialogY + (pdTitleH - titleSize.y) * 0.5f),
                    IM_COL32(255, 255, 0, 255), title);
    }

    /* Body */
    pdguiSetCursorBelowTitle(pdTitleH);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f * scale);

    const char *bodyMsg = langSafe(L_OPTIONS_175);
    if (!bodyMsg || !bodyMsg[0]) bodyMsg = "Do you want to abort the mission?";
    {
        float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
        ImVec2 ts = ImGui::CalcTextSize(bodyMsg, nullptr, false, availW);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - ts.x) * 0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availW);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.85f, 1.0f));
        ImGui::TextWrapped("%s", bodyMsg);
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
    }

    ImGui::Spacing();
    ImGui::Spacing();

    /* Buttons */
    float btnW = pdguiScale(160.0f);
    float btnH = pdguiScale(32.0f);
    float gap  = pdguiScale(16.0f);
    float availW = dialogW - ImGui::GetStyle().WindowPadding.x * 2.0f;
    float totalW = btnW * 2.0f + gap;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availW - totalW) * 0.5f);

    bool doConfirm = false;
    bool doCancel  = false;

    s32 framesOpen = (s_AbortOpenFrame >= 0)
                     ? (curFrame - s_AbortOpenFrame)
                     : ABORT_FORCE_FOCUS_FRAMES + 1;
    bool forceFocus = (framesOpen >= 0 && framesOpen < ABORT_FORCE_FOCUS_FRAMES);
    bool inputDebounced = (framesOpen >= 0 && framesOpen < ABORT_FRAME_DEBOUNCE);

    if (forceFocus) {
        ImGui::SetKeyboardFocusHere(0);
    }

    /* Cancel first — safer default focus for destructive confirm. */
    const char *cancelLabel = langSafe(L_OPTIONS_176);
    if (!cancelLabel || !cancelLabel[0]) cancelLabel = "Cancel";
    char cancelBtnId[96];
    snprintf(cancelBtnId, sizeof(cancelBtnId), "%s##mission_abort_cancel", cancelLabel);
    if (ImGui::Button(cancelBtnId, ImVec2(btnW, btnH))) {
        if (!inputDebounced) doCancel = true;
    }
    ImGui::SetItemDefaultFocus();

    ImGui::SameLine(0.0f, gap);

    /* Red "Abort" confirm. */
    const char *abortLabel = langSafe(L_OPTIONS_177);
    if (!abortLabel || !abortLabel[0]) abortLabel = "Abort";
    char abortBtnId[96];
    snprintf(abortBtnId, sizeof(abortBtnId), "%s##mission_abort_confirm", abortLabel);
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.00f, 0.20f, 0.20f, 1.0f));
    if (ImGui::Button(abortBtnId, ImVec2(btnW, btnH))) {
        if (!inputDebounced) doConfirm = true;
    }
    ImGui::PopStyleColor(3);

    /* Keybinding hints */
    {
        const char *hintL = "[Enter/Space/(A)] Confirm";
        const char *hintR = "[Esc/(B)] Cancel";

        float hintY = dialogH - pdguiScale(22.0f);
        if (hintY < ImGui::GetCursorPosY() + 4.0f * scale) {
            hintY = ImGui::GetCursorPosY() + 4.0f * scale;
        }
        ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, hintY));
        ImGui::TextDisabled("%s", hintL);

        ImVec2 rSize = ImGui::CalcTextSize(hintR);
        ImGui::SetCursorPos(ImVec2(dialogW - ImGui::GetStyle().WindowPadding.x - rSize.x,
                                   hintY));
        ImGui::TextDisabled("%s", hintR);
    }

    /* Keyboard + gamepad shortcuts. Debounced for ABORT_FRAME_DEBOUNCE frames
     * after open so the Enter press that activated the Abort row can't bleed
     * through. */
    if (!inputDebounced) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            doConfirm = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            doCancel = true;
        }
    }

    if (doConfirm) {
        pdguiPlaySound(PDGUI_SND_EXPLOSION);
        /* Abort handler kicks off mission-end transition which unwinds the
         * menu stack via menupoolReleaseAll(). Pop the dialog explicitly as
         * belt-and-braces so the legacy stack is clean even if the handler
         * path changes. */
        menuhandlerAbortMission(MENUOP_SET, nullptr, nullptr);
        ImGui::CloseCurrentPopup();
        s_AbortOpenedForDialog = nullptr;
        s_AbortOpenFrame = -1;
        menuPopDialog();
    } else if (doCancel) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        ImGui::CloseCurrentPopup();
        s_AbortOpenedForDialog = nullptr;
        s_AbortOpenFrame = -1;
        menuPopDialog();
    }

    ImGui::EndPopup();
    pdguiSetPalette(prevPalette);
    return 1;
}

/* =========================================================================
 * Options — Tabbed panel: Audio / Video / Controls
 * Replaces the old 5-button hub that pushed legacy sub-dialogs.
 * ========================================================================= */

#define PD_SCREENSIZE_FULL    0
#define PD_SCREENSPLIT_HORIZ  0

/* ---- Audio tab content ---- */
static void renderOptionsAudio(void)
{
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

    bool disableMpDeath = g_MusicDisableMpDeath != 0;
    if (PdCheckbox("Disable MP Death Music", &disableMpDeath)) {
        g_MusicDisableMpDeath = disableMpDeath ? 1 : 0;
    }
}

/* ---- Video tab content ---- */
static void renderOptionsVideo(void)
{
    /* Display */
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
                if (ImGui::Selectable(label, selected)) videoSetDisplayMode(i);
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

    ImGui::Spacing();

    /* Performance */
    ImGui::TextDisabled("Performance");
    ImGui::Separator();

    {
        int vsync = videoGetVsync() + 1;
        const char *vsyncOpts[] = { "Adaptive", "Off", "On" };
        int vsyncIdx = vsync;
        if (vsyncIdx < 0) vsyncIdx = 0;
        if (vsyncIdx > 2) vsyncIdx = 2;
        if (PdCombo("VSync", &vsyncIdx, vsyncOpts, 3)) {
            videoSetVsync(vsyncIdx - 1);
        }
    }

    {
        int fpsLimit = videoGetFramerateLimit();
        char fpsLabel[32];
        if (fpsLimit == 0) snprintf(fpsLabel, sizeof(fpsLabel), "Off");
        else snprintf(fpsLabel, sizeof(fpsLabel), "%d FPS", fpsLimit);
        if (PdSliderInt("Framerate Limit", &fpsLimit, 0, 480, fpsLabel)) {
            videoSetFramerateLimit(fpsLimit);
        }
    }

    bool showFps = videoGetDisplayFPS() != 0;
    if (PdCheckbox("Display FPS", &showFps)) {
        videoSetDisplayFPS(showFps ? 1 : 0);
    }

    ImGui::Spacing();

    /* Rendering */
    ImGui::TextDisabled("Rendering");
    ImGui::Separator();

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

    ImGui::Spacing();

    /* Gameplay Visuals */
    ImGui::TextDisabled("Gameplay Visuals");
    ImGui::Separator();

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

    {
        int sz = optionsGetScreenSize();
        if (sz < 0 || sz > 2) sz = PD_SCREENSIZE_FULL;
        const char *szOpts[] = { "Full", "Wide", "Cinema" };
        if (PdCombo("Screen Size", &sz, szOpts, 3)) {
            optionsSetScreenSize(sz);
        }
    }
}

/* ---- Controls tab content ---- */
static void renderOptionsControls(void)
{
    /* Mouse */
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

    ImGui::Spacing();

    /* Look */
    ImGui::TextDisabled("Look");
    ImGui::Separator();

    {
        bool invertY = optionsGetForwardPitch(0) == 0;
        if (PdCheckbox("Invert Look (Y-Axis)", &invertY)) {
            optionsSetForwardPitch(0, invertY ? 0 : 1);
        }
    }

    {
        bool invertRStick = inputControllerGetInvertRStickY(0) != 0;
        if (PdCheckbox("Invert Y-Axis (Right Stick)", &invertRStick)) {
            inputControllerSetInvertRStickY(0, invertRStick ? 1 : 0);
            configSave("pd.ini");
        }
    }

    ImGui::Spacing();

    /* Gameplay */
    ImGui::TextDisabled("Gameplay");
    ImGui::Separator();

    {
        float fov = g_PlayerExtCfg[0].fovy;
        if (PdSliderFloat("Field of View", &fov, 60.0f, 120.0f, "%.0f")) {
            g_PlayerExtCfg[0].fovy = fov;
            g_PlayerExtCfg[0].fovzoommult = g_PlayerExtCfg[0].fovzoom ? fov / 60.0f : 1.0f;
        }
    }

    {
        float sway = g_PlayerExtCfg[0].crosshairsway;
        if (PdSliderFloat("Crosshair Sway", &sway, 0.0f, 10.0f, "%.1f")) {
            g_PlayerExtCfg[0].crosshairsway = sway;
        }
    }
}

/* ---- Options panel renderer ---- */
static s32 renderOptions(struct menudialog *dialog,
                          struct menu *menu,
                          s32 winW, s32 winH)
{
    float mw  = pdguiMenuWidth() * 0.7f;
    float mh  = pdguiMenuHeight() * 0.75f;
    ImVec2 pos = pdguiCenterPos(mw, mh);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(mw, mh));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoResize
                        | ImGuiWindowFlags_NoMove
                        | ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoSavedSettings
                        | ImGuiWindowFlags_NoTitleBar
                        | ImGuiWindowFlags_NoBackground;

    if (!ImGui::Begin("##solo_options", nullptr, wf)) {
        ImGui::End();
        return 1;
    }

    if (ImGui::IsWindowAppearing()) {
        ImGui::SetWindowFocus();
        s_OptionsTabIdx = 0;
    }

    float titleH = pdguiScale(39.0f);
    pdguiDrawPdDialog(pos.x, pos.y, mw, mh, "Options", 1);
    pdguiSetCursorBelowTitle(titleH);

    /* ---- Tab bar ---- */
    static const char *k_TabNames[] = { "Audio", "Video", "Controls" };
    static const s32 k_NumTabs = 3;

    /* Tab switching uses action-driven PageUp/PageDown (LB/RB mapping comes
     * from pdguiDriveImGuiNav) plus Q/E keyboard shortcuts. */
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
        s_OptionsTabIdx--;
        if (s_OptionsTabIdx < 0) s_OptionsTabIdx = k_NumTabs - 1;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, false) ||
        ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        s_OptionsTabIdx++;
        if (s_OptionsTabIdx >= k_NumTabs) s_OptionsTabIdx = 0;
        pdguiPlaySound(PDGUI_SND_FOCUS);
    }

    /* B / Escape = back + save config */
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        pdguiPlaySound(PDGUI_SND_KBCANCEL);
        configSave("pd.ini");
        menuPopDialog();
        ImGui::End();
        return 1;
    }

    /* Draw tab buttons */
    float tabW = (mw - ImGui::GetStyle().WindowPadding.x * 2.0f
                     - pdguiScale(6.0f) * (float)(k_NumTabs - 1)) / (float)k_NumTabs;
    float tabH = pdguiScale(45.0f);

    for (s32 t = 0; t < k_NumTabs; t++) {
        if (t > 0) ImGui::SameLine(0.0f, pdguiScale(6.0f));

        bool active = (s_OptionsTabIdx == t);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
        }
        ImGui::PushID(t);
        if (ImGui::Button(k_TabNames[t], ImVec2(tabW, tabH))) {
            if (!active) pdguiPlaySound(PDGUI_SND_FOCUS);
            s_OptionsTabIdx = t;
        }
        ImGui::PopID();
        if (active) ImGui::PopStyleColor();
    }

    /* S311: glyph-driven bumper hint. */
    {
        char prevKey[24], nextKey[24];
        pdguiGlyphGetActionLabel(ACTION_MENU_TAB_PREV, prevKey, (s32)sizeof(prevKey));
        pdguiGlyphGetActionLabel(ACTION_MENU_TAB_NEXT, nextKey, (s32)sizeof(nextKey));
        ImGui::TextDisabled("%s / %s to switch tabs", prevKey, nextKey);
    }
    ImGui::Separator();

    /* ---- Scrollable tab content ---- */
    /* Priority L (2026-04-25): NavFlattened layout panel for options tabs. */
    if (ImGui::BeginChild("##opts_content", ImVec2(0, 0),
                          ImGuiChildFlags_NavFlattened)) {
        switch (s_OptionsTabIdx) {
        case 0: renderOptionsAudio();    break;
        case 1: renderOptionsVideo();    break;
        case 2: renderOptionsControls(); break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return 1;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

/* F-1.2: Reset all persistent static state so returning to Solo Missions
 * always starts clean.  Called from pdguiEndscreenExitToMainMenu() and on
 * Solo Missions entry from main menu. */
extern "C" void pdguiSoloMissionReset(void)
{
    s_MissionSelectIdx    = 0;
    s_DetailDiffIdx       = 0;
    s_DetailFocusIdx      = 0;
    s_FocusGroup          = FOCUS_MISSION_LIST;   /* M-18 reset */
    s_PrevBriefingStage   = -1;
    s_ShowLockedMissions  = false;
    s_DiffSelectIdx       = 0;
    s_BriefingScroll      = 0.0f;
    s_AcceptSelectIdx     = 0;
    s_PauseSelectIdx      = 0;
    s_RestartOpenFrame    = -1;
    s_AbortOpenedForDialog = nullptr;
    s_AbortOpenFrame      = -1;
    s_OptionsSelectIdx    = 0;
    s_OptionsTabIdx       = 0;
    s_CoopAntiDiffSelectIdx = 0;
    s_CoopAntiOptSelectIdx  = 0;
}

extern "C" {

void pdguiMenuSoloMissionRegister(void)
{
    if (s_Registered) return;
    s_Registered = true;

    /* ---- Full ImGui replacements ---- */
    pdguiHotswapRegister(&g_SelectMissionMenuDialog,
                          renderMissionSelect,         "Mission Select");

    pdguiHotswapRegister(&g_SoloMissionDifficultyMenuDialog,
                          renderDifficulty,            "Solo Difficulty");

    pdguiHotswapRegister(&g_SoloMissionBriefingMenuDialog,
                          renderSoloBriefing,          "Solo Briefing");

    pdguiHotswapRegister(&g_PreAndPostMissionBriefingMenuDialog,
                          renderPrePostBriefing,       "Pre/Post Briefing");

    pdguiHotswapRegister(&g_AcceptMissionMenuDialog,
                          renderAcceptMission,         "Accept Mission");

    pdguiHotswapRegister(&g_SoloMissionPauseMenuDialog,
                          renderPauseMenu,             "Solo Pause");

    pdguiHotswapRegister(&g_MissionAbortMenuDialog,
                          renderAbortMission,          "Abort Mission");

    pdguiHotswapRegister(&g_SoloMissionOptionsMenuDialog,
                          renderOptions,               "Solo Options");

    /* ---- Options sub-dialogs: no longer pushed (inline tabbed panel),
     * but registered as NULL to block legacy renderers if reached. ---- */
    pdguiHotswapRegister(&g_AudioOptionsMenuDialog,
                          nullptr,  "Audio Opts (inline)");
    pdguiHotswapRegister(&g_VideoOptionsMenuDialog,
                          nullptr,  "Video Opts (inline)");
    pdguiHotswapRegister(&g_MissionControlOptionsMenuDialog,
                          nullptr,  "Control Opts (inline)");
    pdguiHotswapRegister(&g_MissionDisplayOptionsMenuDialog,
                          nullptr,  "Display Opts (inline)");

    /* M1.2: Inventory now has a proper ImGui renderer.
     * FrWeapons and ControlStyle use the DEFAULT type fallback (P10). */
    pdguiHotswapRegister(&g_SoloMissionInventoryMenuDialog,
                          renderInventory,  "Inventory");

    /* These two previously used NULL (force-native) but P10 removed native
     * rendering.  Unregister them so the DEFAULT type fallback handles them
     * generically until dedicated ImGui renderers are built. */

    /* ---- S194 Batch 2: Co-op / Counter-Op Flow (4 dialogs) ---- */
    pdguiHotswapRegister(&g_CoopMissionDifficultyMenuDialog,
                          renderCoopMissionDifficulty, "Co-op Difficulty");
    pdguiHotswapRegister(&g_CoopOptionsMenuDialog,
                          renderCoopOptions,           "Co-op Options");
    pdguiHotswapRegister(&g_AntiMissionDifficultyMenuDialog,
                          renderAntiMissionDifficulty, "Counter-Op Difficulty");
    pdguiHotswapRegister(&g_AntiOptionsMenuDialog,
                          renderAntiOptions,           "Counter-Op Options");

    sysLogPrintf(LOG_NOTE,
        "pdgui_menu_solomission: Registered Group 1 + Batch 2 — 12 ImGui + 7 legacy/inline");
}

} /* extern "C" */
