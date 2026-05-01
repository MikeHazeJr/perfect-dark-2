/**
 * actionmap.cpp — M0.2 Phase A: Core Action Map System (implementation)
 *
 * All public symbols are C-linkage (see actionmap.h extern "C" block).
 * Compiled as C++ so we can use std::sort, std::clamp, etc. where convenient.
 *
 * Architecture:
 *   - Up to ACTIONMAP_MAX_CONTEXTS InputMappingContext pointers are kept
 *     sorted by priority (descending) in s_Active[].
 *   - actionmapDispatch() walks s_Active top-down; first IMC that maps a VK
 *     to an action wins for that player's ActionState.
 *   - actionmapPollFrame() reads analog sticks (SDL_GameControllerGetAxis)
 *     and mouse delta (inputMouseGetRawDelta), applies deadzone, and updates
 *     ACTION_AXIS_* states.  Also handles stick→digital threshold crossing
 *     so MENU_UP etc. work from a thumbstick.
 *   - actionmapEndFrame() clears pressed/released edge signals.
 *
 * pd.ini keys: "ActionMap.P%d.%s" per player×action, value = comma-separated
 * VK names as returned by inputGetKeyName().
 *
 * Auto-discovered by CMakeLists.txt GLOB_RECURSE port/*.cpp.
 */

#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string.h> /* strtok */
#include <PR/ultratypes.h>

#include "actionmap.h"
#include "config.h"     /* configRegisterString */
#include "system.h"     /* sysLogPrintf, LOG_NOTE, LOG_WARNING */
#include "inputctx.h"   /* inputCtxGetTop, g_CtxGameplay — for menu axis suppression */
#include "inputlayer.h" /* layer-declared action aperture for transition layers */

/* Priority D (2026-04-24): FREEFLY observer suppression. Single extern
 * decl keeps forgemode.h (and its types.h dependency) out of this TU. */
extern "C" s32 forgeIsFreefly(void);

/* os_thread.h must precede input.h / os_cont.h.
 * os_message.h (included by os_cont.h) uses OSThread without including
 * os_thread.h itself, so we must pre-include it here. */
#include <PR/os_thread.h>
#include "input.h"      /* virtkey enum, inputGetKeyName, inputGetKeyByName,
                           inputMouseGetRawDelta, INPUT_MAX_CONTROLLER_BUTTONS */

void actionmapRefreshStickMultFromUi(void);

/* ============================================================
 * SDL scancode aliases for letters/keys not in the virtkey enum
 * ============================================================ */

/* Letters follow SDL scancode order: A=4 .. Z=29 */
#define VKL_W     26
#define VKL_S     22
#define VKL_D     7
#define VKL_E     8
#define VKL_F     9
#define VKL_Q     20
#define VKL_R     21
#define VKL_V     25
#define VKL_U     24
#define VKL_Y     28
#define VKL_Z     29
#define VKL_G     10
#define VKL_C     6
#define VKL_1     30
#define VKL_4     33
#define VKL_5     34
#define VKL_LEFTBRACKET 47
#define VKL_RIGHTBRACKET 48

/* Arrow keys */
#define VKL_UP    82
#define VKL_DOWN  81
#define VKL_LEFT  80
#define VKL_RIGHT 79
#define VKL_KP_ENTER 88
#define VKL_PAGEUP 75
#define VKL_PAGEDOWN 78
#define VKL_INSERT 73
#define VKL_HOME 74
#define VKL_END 77

/* F-keys: VK_F1=58 -> F5=62, F7=64, F10=67, F11=68 */
#define VKL_F5    62
#define VKL_F7    64
#define VKL_F11   68

/* Number row: VK_1=30 .. VK_9=38, VK_0=39 */
#define VKL_2 31
#define VKL_3 32
#define VKL_4 33
#define VKL_5 34
#define VKL_6 35

/* Gamepad button VKs relative to VK_JOY1_BEGIN */
#define JOY_BTN(player, btn)  ((u32)(VK_JOY1_BEGIN) + (u32)(player) * INPUT_MAX_CONTROLLER_BUTTONS + (u32)(btn))

/* SDL controller button indices */
#define JBTN_A       0
#define JBTN_B       1
#define JBTN_X       2
#define JBTN_Y       3
#define JBTN_BACK    4
#define JBTN_START   6
#define JBTN_LSTICK  7   /* SDL_CONTROLLER_BUTTON_LEFTSTICK */
#define JBTN_RSTICK  8   /* SDL_CONTROLLER_BUTTON_RIGHTSTICK */
#define JBTN_LB      9
#define JBTN_RB      10
#define JBTN_DPAD_UP    11
#define JBTN_DPAD_DOWN  12
#define JBTN_DPAD_LEFT  13
#define JBTN_DPAD_RIGHT 14

/* Synthetic stick-direction offsets (relative to JOY_BEGIN + player*32) */
#define JOFS_LSTICK_LEFT   22
#define JOFS_LSTICK_RIGHT  23
#define JOFS_LSTICK_UP     24
#define JOFS_LSTICK_DOWN   25
#define JOFS_RSTICK_LEFT   26
#define JOFS_RSTICK_RIGHT  27
#define JOFS_RSTICK_UP     28
#define JOFS_RSTICK_DOWN   29
#define JOFS_LTRIG         30
#define JOFS_RTRIG         31

/* Axis thresholds */
#define STICK_PRESS_THRESHOLD  12000   /* ~37% of 32767 */
#define STICK_RELEASE_THRESHOLD 8000   /* hysteresis */
#define TRIGGER_THRESHOLD       7680   /* (30*256) matches input.c */

/* Mouse scaling for ACTION_AXIS_AIM: raw pixels → rough [-1..1] per frame.
 * Tuned for 1080p at 60Hz; the real sensitivity lives in the input system. */
#define MOUSE_AIM_SCALE 0.003f

/* Swap sticks: 0 = normal (L=move, R=aim), 1 = swapped — kept for pd.ini + input.c */
static s32 s_SwapSticks      = 0;

/* Per-stick tuning (radial deadzone + sensitivity on analog output).
 * s_Sens*Ui: 1-10 (0.5 steps) in settings UI; maps to 0.1-3.0 internal mult. */
#define ACTIONMAP_SENS_UI_DEFAULT 4.0f /* ~1.0x legacy stick mult */
static f32 s_SensMoveUi      = ACTIONMAP_SENS_UI_DEFAULT;
static f32 s_SensAimUi       = ACTIONMAP_SENS_UI_DEFAULT;
static f32 s_SensAdsUi       = ACTIONMAP_SENS_UI_DEFAULT;
static f32 s_StickSensMove   = (0.1f + 3.f * (2.9f / 9.f));
static f32 s_StickSensAim    = (0.1f + 3.f * (2.9f / 9.f));
static f32 s_StickDzMove     = 0.15f;
static f32 s_StickDzAim      = 0.15f;

/* Hold/tap threshold for ACTION_USE (interact vs reload on same bind). */
static s32 s_UseHoldThresholdMs = ACTION_USE_HOLD_THRESHOLD_MS;

/* Extra hold (ms) for hackable-terminal prompts — layered in propInteractPromptHoldThresholdMs. */
static s32 s_InteractHoldExtraTerminalMs = 200;

/* Optional per-action hold duration (ms). -1 = unset (USE falls back to s_UseHoldThresholdMs). */
#define ACTIONMAP_HOLD_MS_MAX 2000
#define HOLD_OVERRIDES_STR_MAX 512
static char s_HoldOverridesIniStr[HOLD_OVERRIDES_STR_MAX];
static s32 s_ActionHoldMsOverride[ACTION_COUNT];

/* ============================================================
 * Self-contained VK ↔ name table for pd.ini serialisation.
 *
 * P10 fix: actionmap must not depend on input.c vkNames[] (which
 * may not be populated at config-load time).  This table uses the
 * EXACT names that appear in pd.ini bind strings.
 * ============================================================ */

struct VkNameEntry { u32 vk; const char *name; };

static const VkNameEntry s_VkNameTable[] = {
    /* --- Keyboard: letters (SDL scancodes 4-29) --- */
    { 4,"A"},{ 5,"B"},{ 6,"C"},{ 7,"D"},{ 8,"E"},{ 9,"F"},{10,"G"},{11,"H"},
    {12,"I"},{13,"J"},{14,"K"},{15,"L"},{16,"M"},{17,"N"},{18,"O"},{19,"P"},
    {20,"Q"},{21,"R"},{22,"S"},{23,"T"},{24,"U"},{25,"V"},{26,"W"},{27,"X"},
    {28,"Y"},{29,"Z"},
    /* --- Keyboard: number row (SDL scancodes 30-39) --- */
    {30,"1"},{31,"2"},{32,"3"},{33,"4"},{34,"5"},{35,"6"},{36,"7"},{37,"8"},
    {38,"9"},{39,"0"},
    /* --- Keyboard: editing/whitespace --- */
    {40,"RETURN"},{41,"ESCAPE"},{42,"BACKSPACE"},{43,"TAB"},{44,"SPACE"},
    /* --- Keyboard: punctuation (scancodes 45-56) --- */
    {45,"MINUS"},{46,"EQUALS"},{47,"LEFTBRACKET"},{48,"RIGHTBRACKET"},
    {49,"BACKSLASH"},{50,"HASH"},{51,"SEMICOLON"},{52,"APOSTROPHE"},
    {53,"GRAVE"},{54,"COMMA"},{55,"PERIOD"},{56,"SLASH"},
    /* --- Keyboard: function/toggle (57-69) --- */
    {57,"CAPSLOCK"},
    {58,"F1"},{59,"F2"},{60,"F3"},{61,"F4"},{62,"F5"},{63,"F6"},
    {64,"F7"},{65,"F8"},{66,"F9"},{67,"F10"},{68,"F11"},{69,"F12"},
    /* --- Keyboard: navigation --- */
    {70,"PRINTSCREEN"},{71,"SCROLLLOCK"},{72,"PAUSE"},
    {73,"INSERT"},{74,"HOME"},{75,"PAGEUP"},{76,"DELETE"},
    {77,"END"},{78,"PAGEDOWN"},
    {79,"RIGHT"},{80,"LEFT"},{81,"DOWN"},{82,"UP"},
    {83,"NUMLOCKCLEAR"},
    /* --- Keyboard: keypad --- */
    {84,"KP_DIVIDE"},{85,"KP_MULTIPLY"},{86,"KP_MINUS"},{87,"KP_PLUS"},
    {88,"KP_ENTER"},{89,"KP_1"},{90,"KP_2"},{91,"KP_3"},{92,"KP_4"},
    {93,"KP_5"},{94,"KP_6"},{95,"KP_7"},{96,"KP_8"},{97,"KP_9"},
    {98,"KP_0"},{99,"KP_PERIOD"},{103,"KP_EQUALS"},
    /* --- Keyboard: modifiers (224-231) --- */
    {224,"LEFT_CTRL"},{225,"LEFT_SHIFT"},{226,"LEFT_ALT"},{227,"LEFT_GUI"},
    {228,"RIGHT_CTRL"},{229,"RIGHT_SHIFT"},{230,"RIGHT_ALT"},{231,"RIGHT_GUI"},
    /* --- Synthetic keyboard chords --- */
    {VK_CHORD_CTRL_TAB,       "CTRL+TAB"},
    {VK_CHORD_CTRL_SHIFT_TAB, "CTRL+SHIFT+TAB"},
    {VK_CHORD_CTRL_Z,         "CTRL+Z"},
    {VK_CHORD_CTRL_SHIFT_Z,   "CTRL+SHIFT+Z"},
    {VK_CHORD_CTRL_Y,         "CTRL+Y"},
    {VK_CHORD_CTRL_S,         "CTRL+S"},
    /* --- Mouse (VK_MOUSE_BEGIN = 512) --- */
    {VK_MOUSE_LEFT,     "MOUSE_LEFT"},
    {VK_MOUSE_MIDDLE,   "MOUSE_MIDDLE"},
    {VK_MOUSE_RIGHT,    "MOUSE_RIGHT"},
    {VK_MOUSE_X1,       "MOUSE_X1"},
    {VK_MOUSE_X2,       "MOUSE_X2"},
    {VK_MOUSE_WHEEL_UP, "MOUSE_WHEEL_UP"},
    {VK_MOUSE_WHEEL_DN, "MOUSE_WHEEL_DN"},
};
static const s32 s_VkNameTableSize = (s32)(sizeof(s_VkNameTable) / sizeof(s_VkNameTable[0]));

/* Joystick button names (offset within a controller's 32-slot range).
 * JOY<n>_<name> is assembled dynamically. */
static const char * const s_JoyBtnNames[INPUT_MAX_CONTROLLER_BUTTONS] = {
    "A","B","X","Y","BACK","GUIDE","START","LSTICK","RSTICK",
    "LSHOULDER","RSHOULDER","DPAD_UP","DPAD_DOWN","DPAD_LEFT","DPAD_RIGHT",
    "BUTTON_15","BUTTON_16","BUTTON_17","BUTTON_18","BUTTON_19",
    "TOUCHPAD","BUTTON_21",
    "LSTICK_LEFT","LSTICK_RIGHT","LSTICK_UP","LSTICK_DOWN",
    "RSTICK_LEFT","RSTICK_RIGHT","RSTICK_UP","RSTICK_DOWN",
    "LTRIGGER","RTRIGGER",
};

/** Look up VK name for pd.ini serialisation (self-contained, no input.c dependency).
 * M-9: WARNING — returns pointer to static buffer for joystick VKs.
 * The returned string is only valid until the next call with a joystick VK. */
static const char *actionmapGetVkName(u32 vk)
{
    /* Joystick buttons: JOY<n>_<btn> */
    if (vk >= (u32)VK_JOY1_BEGIN && vk < (u32)VK_TOTAL_COUNT) {
        u32 off = vk - (u32)VK_JOY1_BEGIN;
        u32 jidx = off / INPUT_MAX_CONTROLLER_BUTTONS;
        u32 jbtn = off % INPUT_MAX_CONTROLLER_BUTTONS;
        static char joyBuf[32];
        snprintf(joyBuf, sizeof(joyBuf), "JOY%u_%s", jidx + 1, s_JoyBtnNames[jbtn]);
        return joyBuf;
    }

    /* Static table lookup */
    for (s32 i = 0; i < s_VkNameTableSize; i++) {
        if (s_VkNameTable[i].vk == vk) {
            return s_VkNameTable[i].name;
        }
    }

    /* Unknown: encode as UNKNOWNnnn */
    static char unkBuf[16];
    snprintf(unkBuf, sizeof(unkBuf), "UNKNOWN%u", vk);
    return unkBuf;
}

/** Resolve a pd.ini key name to a VK code (self-contained). */
static s32 actionmapGetVkByName(const char *name)
{
    if (!name || !name[0]) return -1;

    /* Joystick: JOY<n>_<btn> */
    if (!strncmp(name, "JOY", 3) && name[3] >= '1' && name[3] <= '4' && name[4] == '_') {
        u32 jidx = (u32)(name[3] - '1');
        const char *btn = name + 5;
        for (u32 b = 0; b < INPUT_MAX_CONTROLLER_BUTTONS; b++) {
            if (!strcmp(btn, s_JoyBtnNames[b])) {
                return (s32)(VK_JOY1_BEGIN + jidx * INPUT_MAX_CONTROLLER_BUTTONS + b);
            }
        }
        return -1;
    }

    /* UNKNOWN<n> raw code */
    if (!strncmp(name, "UNKNOWN", 7) && name[7] >= '0' && name[7] <= '9') {
        s32 v = atoi(name + 7);
        return (v >= 0 && v < VK_TOTAL_COUNT) ? v : -1;
    }

    /* Static table */
    for (s32 i = 0; i < s_VkNameTableSize; i++) {
        if (!strcmp(s_VkNameTable[i].name, name)) {
            return (s32)s_VkNameTable[i].vk;
        }
    }

    sysLogPrintf(LOG_WARNING, "actionmap: unknown key name: `%s`", name);
    return -1;
}

/* ============================================================
 * Action name table (must match InputAction enum order)
 * ============================================================ */

static const char * const s_ActionNames[ACTION_COUNT] = {
    /* 0-5: movement */
    "MoveForward",
    "MoveBackward",
    "MoveLeft",
    "MoveRight",
    "AxisMoveX",
    "AxisMoveY",
    /* 6-11: analog aim (right stick / mouse) */
    "AimUp",
    "AimDown",
    "AimLeft",
    "AimRight",
    "AxisAimX",
    "AxisAimY",
    /* 12-15: N64 C-buttons */
    "CButtonUp",
    "CButtonDown",
    "CButtonLeft",
    "CButtonRight",
    /* 16-19: gameplay D-pad */
    "DpadUp",
    "DpadDown",
    "DpadLeft",
    "DpadRight",
    /* 20-31: combat */
    "FirePrimary",
    "FireSecondary",
    "FireMode",
    "Reload",
    "Use",
    "CancelUse",
    "ThrowWeapon",
    "Crouch",
    "Jump",
    "Sprint",
    "ZoomIn",
    "ZoomOut",
    /* 32-39: weapon selection */
    "WeaponPrev",
    "WeaponNext",
    "Weapon1",
    "Weapon2",
    "Weapon3",
    "Weapon4",
    "Weapon5",
    "Weapon6",
    /* 40-44: vehicle */
    "VehicleAccelerate",
    "VehicleBrake",
    "VehicleSteerLeft",
    "VehicleSteerRight",
    "VehicleExit",
    /* 45-50: menu nav (MenuAccept/Cancel removed — consolidated into Use/CancelUse) */
    "MenuUp",
    "MenuDown",
    "MenuLeft",
    "MenuRight",
    "MenuTabPrev",
    "MenuTabNext",
    /* 51-56: system */
    "Pause",
    "Screenshot",
    "ConsoleToggle",
    "DebugToggle",
    "CheatEnter",
    "Scorecard",
    /* 57-61: forge level editor (F0+) */
    "ForgeToggle",
    "ForgeAscend",
    "ForgeDescend",
    "ForgeBoost",
    "ForgePrecision",
    /* 62-67: forge sidebar + tab navigation (Issue 8b) */
    "ForgeSidebarToggle",
    "ForgeSidebarUp",
    "ForgeSidebarDown",
    "ForgeSidebarActivate",
    "ForgeTabPrev",
    "ForgeTabNext",
    /* 68: combat sim hold-vs-tap (Priority J) */
    "ScorecardHold",
    /* 69: connectivity sidebar toggle (S483b) -- menu IMCs only */
    "SocialToggle",
    /* 70: test-scenarios swarm benchmark cycler (S483c) -- PD_DEV_BUILD only.
     * Renamed semantically to "next-higher count" by S594h-A2; the PREV
     * counterpart is at index 103. */
    "TestScenCycleNext",
    /* 71: text-input paste-from-clipboard (Cohort 1) -- text-input IMC only */
    "TextPaste",
    /* 72: cutscene-skip (Cohort 4, K.2) -- g_ImcCutscene IMC only */
    "SkipCutscene",
    /* 73-75: menu secondary commands */
    "MenuSecondary",
    "MenuTertiary",
    "MenuDelete",
    /* 76-84: observer / spectator controls */
    "ObserverSubsetPrev",
    "ObserverSubsetNext",
    "ObserverMemberPrev",
    "ObserverMemberNext",
    "ObserverCameraToggle",
    "ObserverFreefly",
    "ObserverStop",
    "ObserverAscend",
    "ObserverDescend",
    /* 85: voice chat */
    "VoicePtt",
    /* 86-90: Forge editor commands */
    "ForgePlaceCancel",
    "ForgeBotAdd",
    "ForgeBotRemoveAll",
    "ForgeBotFreezeToggle",
    "ForgeBotSpawnCycle",
    /* 91-102: Skin editor commands */
    "SkinBrushDecrease",
    "SkinBrushIncrease",
    "SkinToolDraw",
    "SkinToolErase",
    "SkinToolFill",
    "SkinToolEyedropper",
    "SkinToolLine",
    "SkinGridToggle",
    "SkinUvToggle",
    "SkinUndo",
    "SkinRedo",
    "SkinSave",
    /* 103-104: test-scenarios cycler reverse + visibility toggle (S594h-A2) */
    "TestScenCyclePrev",
    "TestScenVisToggle",
};

/* ============================================================
 * Module state
 * ============================================================ */

/* Active IMC registry, kept sorted by priority descending */
static InputMappingContext *s_Active[ACTIONMAP_MAX_CONTEXTS];
static s32 s_NumActive = 0;

/* Per-player, per-action state */
static ActionState s_State[ACTIONMAP_MAX_PLAYERS][ACTION_COUNT];

/* M-2: Pre-computed list of actions bound to mouse wheel VKs, for fast end-of-frame release. */
static InputAction s_WheelActions[ACTION_COUNT];
static s32 s_NumWheelActions = 0;

/* Stick digital state: tracks whether each synthetic stick-dir VK is held.
 * Indexed [player][stick_slot] where stick_slot:
 *   0=LX-, 1=LX+, 2=LY-, 3=LY+, 4=RX-, 5=RX+, 6=RY-, 7=RY+, 8=LTrig, 9=RTrig */
static s32 s_StickHeld[ACTIONMAP_MAX_PLAYERS][10];

/* Device detection */
static s32 s_RawDevice  = ACTIONMAP_DEVICE_KBM;
static s32 s_LastDevice = ACTIONMAP_DEVICE_KBM;
static u32 s_DeviceChangeTime = 0;

/* Cheat code rolling buffer */
static InputAction s_CheatBuf[ACTIONMAP_CHEAT_BUF_LEN];
static s32 s_CheatHead  = 0;  /* next write index (ring buffer) */
static s32 s_CheatCount = 0;  /* how many entries are valid */

/* pd.ini bind string storage: [player][action] */
#define BIND_STR_MAX 128
static char s_BindStr[ACTIONMAP_MAX_PLAYERS][ACTION_COUNT][BIND_STR_MAX];

static s32 s_StickInvertY     = 0;      /* 1 = negate AIM_Y for controller sticks */

/* ============================================================
 * Helpers: player-from-VK, deadzone
 * ============================================================ */

/** Infer which player 0-3 owns a VK.
 *  Keyboard/mouse → player 0; JOY{n} → player n-1. */
static inline s32 playerForVk(u32 vk)
{
    if (vk < (u32)VK_JOY_BEGIN) {
        return 0;
    }
    u32 off = vk - (u32)VK_JOY_BEGIN;
    s32 p = (s32)(off / INPUT_MAX_CONTROLLER_BUTTONS);
    return (p < ACTIONMAP_MAX_PLAYERS) ? p : 0;
}

/** Radial deadzone: returns 0 inside dead band, linearly rescaled outside. */
static inline f32 applyDeadzone(f32 raw, f32 dz)
{
    if (raw < 0.0f) {
        if (raw > -dz) return 0.0f;
        return (raw + dz) / (1.0f - dz);
    } else {
        if (raw < dz) return 0.0f;
        return (raw - dz) / (1.0f - dz);
    }
}

static inline f32 clampf(f32 v, f32 lo, f32 hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/** 2D stick: circular deadzone in normalized space; outside dz, magnitude is
 *  remapped so the rim of the deadzone is 0 and full deflection is |v|<=1
 *  (before sensitivity). nx, ny in [-1,1]. */
static void applyRadialStick2D(f32 nx, f32 ny, f32 dz, f32 sens, f32 *ox, f32 *oy)
{
    *ox = 0.0f;
    *oy = 0.0f;
    f32 m = sqrtf(nx * nx + ny * ny);
    if (m < 1e-5f) {
        return;
    }
    if (m <= dz) {
        return;
    }
    f32 newMag = (m - dz) / (1.0f - dz);
    if (newMag < 0.0f) {
        return;
    }
    f32 scale = (newMag / m) * sens;
    *ox = clampf(nx * scale, -1.0f, 1.0f);
    *oy = clampf(ny * scale, -1.0f, 1.0f);
}

/* Diagnostic: frame counter for throttled logging */
static u32 s_DiagFrameCount = 0;

/* Swap sticks API */
void actionmapSetSwapSticks(s32 swapped) { s_SwapSticks = swapped ? 1 : 0; }
s32  actionmapGetSwapSticks(void)        { return s_SwapSticks; }

/* ============================================================
 * Helpers: cheat buffer
 * ============================================================ */

static void cheatRecord(InputAction action)
{
    s_CheatBuf[s_CheatHead] = action;
    s_CheatHead = (s_CheatHead + 1) % ACTIONMAP_CHEAT_BUF_LEN;
    if (s_CheatCount < ACTIONMAP_CHEAT_BUF_LEN) {
        s_CheatCount++;
    }
}

static s32 actionLayerAllows(InputAction a);

/* ============================================================
 * Helpers: fire a digital VK event into action states
 * ============================================================ */

/** Given a VK and direction (pressed=1, released=0), find the action it maps
 *  to in the highest-priority active IMC and update the player's state. */
static void fireVk(u32 vk, s32 is_down)
{
    if (vk == 0) {
        return;
    }

    s32 player = playerForVk(vk);

    /* Legacy dispatch-site gate (ADR 2026-04-13): while the inputctx
     * predicate suppresses gameplay, skip gameplay-scope IMCs entirely.
     * Layer-declared action apertures are applied per winning action below. */
    s32 suppressGameplay = gameplayInputSuppressed();

    /* Walk contexts from highest priority to lowest */
    for (s32 ci = 0; ci < s_NumActive; ci++) {
        InputMappingContext *ctx = s_Active[ci];

        if (suppressGameplay &&
            (ctx == &g_ImcGameplay || ctx == &g_ImcVehicle || ctx == &g_ImcObserver)) {
            continue;
        }

        /* B-221.5: one VK can map to several actions in the same IMC (e.g. two reload paths).
         * Pick a single winner per context: lowest trigger slot first (Bind 1 / primary edge),
         * then lower InputAction id as a deterministic tie-break. */
        s32 best_a = ACTION_COUNT;
        s32 best_ti = ACTIONMAP_MAX_TRIGGERS + 1;

        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (!ctx->has_mapping[a]) {
                continue;
            }
            InputMapping *m = &ctx->mappings[a];
            for (s32 ti = 0; ti < m->num_triggers; ti++) {
                if (m->triggers[ti].vk != vk) {
                    continue;
                }
                if (ti < best_ti || (ti == best_ti && a < best_a)) {
                    best_ti = ti;
                    best_a = a;
                }
            }
        }

        if (best_a < ACTION_COUNT) {
            if (!actionLayerAllows((InputAction)best_a)) {
                goto next_player;
            }

            ActionState *st = &s_State[player][best_a];

            /* DIAG: Log which IMC wins the VK→action mapping for gamepad VKs.
             * S197a: gated behind sysLogGetVerbose() — was spamming the log
             * at ~60 Hz × N buttons per player. Enable with --verbose. */
            if (sysLogGetVerbose() && vk >= (u32)VK_JOY1_BEGIN && is_down) {
                sysLogPrintf(LOG_NOTE, "DIAG fireVk: vk=%u player=%d -> IMC '%s' action=%d(%s) DOWN",
                             vk, player,
                             ctx->name ? ctx->name : "?",
                             best_a, (best_a < ACTION_COUNT) ? s_ActionNames[best_a] : "?");
            }

            if (is_down) {
                if (!st->held) {
                    st->held          = 1;
                    st->pressed       = 1;
                    st->value         = 1.0f;
                    st->down_time_ms  = SDL_GetTicks();
                    st->hold_consumed = 0;
                    st->hold_vis_grace_until_ms = 0;
                    st->hold_pin_full_until_ms = 0;
                    /* Record non-axis actions in cheat buffer */
                    if (best_a != ACTION_AXIS_MOVE_X && best_a != ACTION_AXIS_MOVE_Y &&
                        best_a != ACTION_AXIS_AIM_X  && best_a != ACTION_AXIS_AIM_Y) {
                        cheatRecord((InputAction)best_a);
                    }
                }
            } else {
                if (st->held) {
                    st->held        = 0;
                    st->released    = 1;
                    st->value       = 0.0f;
                    st->up_time_ms  = SDL_GetTicks();
                    st->hold_vis_grace_until_ms = SDL_GetTicks() + 100;
                }
            }
            /* Matched this IMC — do not fall through to lower-priority contexts */
            goto next_player;
        }
    }
    /* DIAG: Log when a gamepad VK has no binding in any active IMC.
     * S197a: gated behind sysLogGetVerbose(). Also filter out synthetic
     * stick-as-button VKs (LSTICK_*, RSTICK_*, LTRIG, RTRIG) which are
     * INTENTIONALLY unbound in most IMCs (analog movement is handled by
     * the SDL_GameControllerGetAxis path, not the button table). These
     * were producing hundreds of false "NO BINDING FOUND" warnings per
     * stick flick and made the log unreadable. */
    if (sysLogGetVerbose() && vk >= (u32)VK_JOY1_BEGIN && is_down) {
        u32 joyOffset = (vk - (u32)VK_JOY1_BEGIN) % (u32)INPUT_MAX_CONTROLLER_BUTTONS;
        s32 isSyntheticAxis = (joyOffset >= 22 && joyOffset <= 31);
        if (!isSyntheticAxis) {
            sysLogPrintf(LOG_WARNING, "DIAG fireVk: vk=%u player=%d NO BINDING FOUND in %d active IMCs",
                         vk, player, s_NumActive);
        }
    }
    return;
next_player:;
}

static u32 chordVkForKeysym(const SDL_Keysym *keysym)
{
    if (!keysym) {
        return 0;
    }

    const s32 ctrl = (keysym->mod & KMOD_CTRL) != 0;
    const s32 shift = (keysym->mod & KMOD_SHIFT) != 0;

    if (!ctrl) {
        return 0;
    }

    switch (keysym->scancode) {
    case SDL_SCANCODE_TAB:
        return shift ? VK_CHORD_CTRL_SHIFT_TAB : VK_CHORD_CTRL_TAB;
    case SDL_SCANCODE_Z:
        return shift ? VK_CHORD_CTRL_SHIFT_Z : VK_CHORD_CTRL_Z;
    case SDL_SCANCODE_Y:
        return VK_CHORD_CTRL_Y;
    case SDL_SCANCODE_S:
        return VK_CHORD_CTRL_S;
    default:
        return 0;
    }
}

static u32 s_KeyDownChordVk[SDL_NUM_SCANCODES];

/* ============================================================
 * Helpers: device detection update
 * ============================================================ */

static void updateDevice(s32 new_raw)
{
    if (new_raw == s_RawDevice) {
        return;
    }
    s_RawDevice = new_raw;
    s_DeviceChangeTime = SDL_GetTicks();
}

/* ============================================================
 * Helpers: axis → digital transitions (stick threshold crossing)
 * ============================================================ */

/** Handle one axis value, firing synthetic VKs when threshold is crossed.
 *  slot_neg/slot_pos are indices into s_StickHeld[player][].
 *  vk_neg/vk_pos are the synthetic VKs to fire. */
static void handleAxisDigital(s32 player, s16 val,
                               s32 slot_neg, s32 slot_pos,
                               u32 vk_neg, u32 vk_pos)
{
    /* Negative direction */
    if (val < -STICK_PRESS_THRESHOLD) {
        if (!s_StickHeld[player][slot_neg]) {
            s_StickHeld[player][slot_neg] = 1;
            fireVk(vk_neg, 1);
        }
    } else if (val > -STICK_RELEASE_THRESHOLD) {
        if (s_StickHeld[player][slot_neg]) {
            s_StickHeld[player][slot_neg] = 0;
            fireVk(vk_neg, 0);
        }
    }

    /* Positive direction */
    if (val > STICK_PRESS_THRESHOLD) {
        if (!s_StickHeld[player][slot_pos]) {
            s_StickHeld[player][slot_pos] = 1;
            fireVk(vk_pos, 1);
        }
    } else if (val < STICK_RELEASE_THRESHOLD) {
        if (s_StickHeld[player][slot_pos]) {
            s_StickHeld[player][slot_pos] = 0;
            fireVk(vk_pos, 0);
        }
    }
}

/** Handle one trigger axis (unipolar 0..32767). */
static void handleTriggerDigital(s32 player, s16 val, s32 slot, u32 vk)
{
    if (val > TRIGGER_THRESHOLD) {
        if (!s_StickHeld[player][slot]) {
            s_StickHeld[player][slot] = 1;
            fireVk(vk, 1);
        }
    } else if (val < TRIGGER_THRESHOLD / 2) {
        if (s_StickHeld[player][slot]) {
            s_StickHeld[player][slot] = 0;
            fireVk(vk, 0);
        }
    }
}

/* ============================================================
 * Context sort (priority descending)
 * ============================================================ */

static void sortContexts(void)
{
    /* Insertion sort — small array */
    for (s32 i = 1; i < s_NumActive; i++) {
        InputMappingContext *key = s_Active[i];
        s32 j = i - 1;
        while (j >= 0 && s_Active[j]->priority < key->priority) {
            s_Active[j + 1] = s_Active[j];
            j--;
        }
        s_Active[j + 1] = key;
    }
}

/* ============================================================
 * Public: lifecycle
 * ============================================================ */

void imcActivate(InputMappingContext *imc)
{
    if (!imc || imc->active) {
        return;
    }
    if (s_NumActive >= ACTIONMAP_MAX_CONTEXTS) {
        sysLogPrintf(LOG_WARNING, "ACTIONMAP: context overflow, cannot activate '%s'",
                     imc->name ? imc->name : "?");
        return;
    }
    imc->active = 1;
    s_Active[s_NumActive++] = imc;
    sortContexts();
    sysLogPrintf(LOG_NOTE, "ACTIONMAP: activated '%s' (priority %d, depth %d)",
                 imc->name, imc->priority, s_NumActive);
}

void imcDeactivate(InputMappingContext *imc)
{
    if (!imc || !imc->active) {
        return;
    }
    s32 write = 0;
    for (s32 i = 0; i < s_NumActive; i++) {
        if (s_Active[i] != imc) {
            s_Active[write++] = s_Active[i];
        }
    }
    s_NumActive = write;
    imc->active = 0;
    sysLogPrintf(LOG_NOTE, "ACTIONMAP: deactivated '%s' (depth now %d)",
                 imc->name, s_NumActive);
}

/* ============================================================
 * Public: scene-scope IMC dispatch (Priority J, 2026-04-25)
 *
 * Mission XOR CombatSim invariant. The setters always deactivate the
 * other gameplay-scope IMC first, then activate the requested one. If a
 * mutual-exclusion violation is detected (both already active when set is
 * called -- shouldn't happen, but defends against future drift) a
 * LOG_WARNING fires and we converge to the requested state regardless.
 *
 * imcSceneClearGameplay also drops Vehicle so a stage transition that
 * happens mid-vehicle (e.g. unexpected disconnect while mounted) cannot
 * leave the vehicle IMC active across an unrelated next scene.
 * ============================================================ */

static void imcSceneAssertMutex(const char *caller)
{
    if (g_ImcMission.active && g_ImcCombatSim.active) {
        sysLogPrintf(LOG_WARNING,
            "ACTIONMAP: scene-scope mutex violated in %s -- both Mission and CombatSim active. "
            "Recovering to requested state.",
            caller ? caller : "?");
    }
}

void imcSceneSetMission(void)
{
    imcSceneAssertMutex("imcSceneSetMission");
    if (g_ImcCombatSim.active) {
        imcDeactivate(&g_ImcCombatSim);
    }
    if (!g_ImcMission.active) {
        imcActivate(&g_ImcMission);
        sysLogPrintf(LOG_NOTE, "ACTIONMAP: scene -> Mission");
    }
}

void imcSceneSetCombatSim(void)
{
    imcSceneAssertMutex("imcSceneSetCombatSim");
    if (g_ImcMission.active) {
        imcDeactivate(&g_ImcMission);
    }
    if (!g_ImcCombatSim.active) {
        imcActivate(&g_ImcCombatSim);
        sysLogPrintf(LOG_NOTE, "ACTIONMAP: scene -> CombatSim");
    }
}

void imcSceneClearGameplay(void)
{
    if (g_ImcMission.active) {
        imcDeactivate(&g_ImcMission);
    }
    if (g_ImcCombatSim.active) {
        imcDeactivate(&g_ImcCombatSim);
    }
    if (g_ImcVehicle.active) {
        /* Stage transition while mounted should never happen, but if it
         * does, the vehicle IMC must not survive into the next scene. */
        imcDeactivate(&g_ImcVehicle);
    }
    sysLogPrintf(LOG_NOTE, "ACTIONMAP: scene -> cleared");
}

void imcVehicleMount(void)
{
    if (!g_ImcVehicle.active) {
        imcActivate(&g_ImcVehicle);
        sysLogPrintf(LOG_NOTE, "ACTIONMAP: vehicle IMC mounted");
    }
}

void imcVehicleDismount(void)
{
    if (g_ImcVehicle.active) {
        imcDeactivate(&g_ImcVehicle);
        sysLogPrintf(LOG_NOTE, "ACTIONMAP: vehicle IMC dismounted");
    }
}

/* Cohort 4 (2026-04-27, K.2): Cutscene IMC entry / exit. Called from
 * inputlayer.c's LAYER_CUTSCENE on_push / on_pop hooks. Idempotent. */
void imcCutsceneEnter(void)
{
    if (!g_ImcCutscene.active) {
        imcActivate(&g_ImcCutscene);
        sysLogPrintf(LOG_NOTE, "ACTIONMAP: cutscene IMC entered");
    }
}

void imcCutsceneExit(void)
{
    if (g_ImcCutscene.active) {
        imcDeactivate(&g_ImcCutscene);
        sysLogPrintf(LOG_NOTE, "ACTIONMAP: cutscene IMC exited");
    }
}

/* ============================================================
 * Public: actionmapDispatch
 * ============================================================ */

void actionmapDispatch(const SDL_Event *ev)
{
    if (!ev) {
        return;
    }

    switch (ev->type) {

    /* ---- Keyboard ---- */
    case SDL_KEYDOWN:
        if (ev->key.repeat) break;
        updateDevice(ACTIONMAP_DEVICE_KBM);
        if (ev->key.keysym.scancode >= 0 && ev->key.keysym.scancode < SDL_NUM_SCANCODES) {
            u32 chord_vk = chordVkForKeysym(&ev->key.keysym);
            s_KeyDownChordVk[ev->key.keysym.scancode] = chord_vk;
            fireVk(chord_vk ? chord_vk : (u32)ev->key.keysym.scancode, 1);
        }
        break;

    case SDL_KEYUP:
        if (ev->key.repeat) break;
        updateDevice(ACTIONMAP_DEVICE_KBM);
        if (ev->key.keysym.scancode >= 0 && ev->key.keysym.scancode < SDL_NUM_SCANCODES) {
            u32 chord_vk = s_KeyDownChordVk[ev->key.keysym.scancode];
            s_KeyDownChordVk[ev->key.keysym.scancode] = 0;
            fireVk(chord_vk ? chord_vk : (u32)ev->key.keysym.scancode, 0);
        }
        break;

    /* ---- Mouse buttons ---- */
    case SDL_MOUSEBUTTONDOWN:
        updateDevice(ACTIONMAP_DEVICE_KBM);
        if (ev->button.button >= 1 && ev->button.button <= 5) {
            fireVk(VK_MOUSE_BEGIN + (ev->button.button - 1), 1);
        }
        break;

    case SDL_MOUSEBUTTONUP:
        updateDevice(ACTIONMAP_DEVICE_KBM);
        if (ev->button.button >= 1 && ev->button.button <= 5) {
            fireVk(VK_MOUSE_BEGIN + (ev->button.button - 1), 0);
        }
        break;

    /* ---- Mouse wheel (momentary press, released next frame via EndFrame) ---- */
    case SDL_MOUSEWHEEL:
        updateDevice(ACTIONMAP_DEVICE_KBM);
        if (ev->wheel.y > 0) {
            fireVk(VK_MOUSE_WHEEL_UP, 1);
        } else if (ev->wheel.y < 0) {
            fireVk(VK_MOUSE_WHEEL_DN, 1);
        }
        break;

    /* ---- Mouse motion ---- */
    case SDL_MOUSEMOTION:
        updateDevice(ACTIONMAP_DEVICE_KBM);
        break;

    /* ---- Gamepad buttons ---- */
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(ev->cbutton.which);
        s32 player = ctrl ? SDL_GameControllerGetPlayerIndex(ctrl) : 0;
        if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) player = 0;
        updateDevice(ACTIONMAP_DEVICE_GAMEPAD);
        u32 vk = JOY_BTN(player, (u32)ev->cbutton.button);
        if (sysLogGetVerbose()) {
            sysLogPrintf(LOG_NOTE, "DIAG btn: SDL btn=%d player=%d vk=%u %s ctrl=%p",
                         (int)ev->cbutton.button, player, vk,
                         (ev->type == SDL_CONTROLLERBUTTONDOWN) ? "DOWN" : "UP",
                         (void*)ctrl);
        }
        fireVk(vk, (ev->type == SDL_CONTROLLERBUTTONDOWN) ? 1 : 0);
        break;
    }

    /* ---- Gamepad axes: digital threshold crossing ---- */
    case SDL_CONTROLLERAXISMOTION: {
        SDL_GameController *ctrl = SDL_GameControllerFromInstanceID(ev->caxis.which);
        s32 player = ctrl ? SDL_GameControllerGetPlayerIndex(ctrl) : 0;
        if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) player = 0;

        s16 val  = ev->caxis.value;
        s32 axis = ev->caxis.axis;

        /* Only count as gamepad input if axis moves meaningfully */
        if (val > 4000 || val < -4000) {
            updateDevice(ACTIONMAP_DEVICE_GAMEPAD);
        }

        switch (axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            handleAxisDigital(player, val, 0, 1,
                              JOY_BTN(player, JOFS_LSTICK_LEFT),
                              JOY_BTN(player, JOFS_LSTICK_RIGHT));
            break;
        case SDL_CONTROLLER_AXIS_LEFTY:
            handleAxisDigital(player, val, 2, 3,
                              JOY_BTN(player, JOFS_LSTICK_UP),
                              JOY_BTN(player, JOFS_LSTICK_DOWN));
            break;
        case SDL_CONTROLLER_AXIS_RIGHTX:
            handleAxisDigital(player, val, 4, 5,
                              JOY_BTN(player, JOFS_RSTICK_LEFT),
                              JOY_BTN(player, JOFS_RSTICK_RIGHT));
            break;
        case SDL_CONTROLLER_AXIS_RIGHTY:
            handleAxisDigital(player, val, 6, 7,
                              JOY_BTN(player, JOFS_RSTICK_UP),
                              JOY_BTN(player, JOFS_RSTICK_DOWN));
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            handleTriggerDigital(player, val, 8,
                                 JOY_BTN(player, JOFS_LTRIG));
            break;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            handleTriggerDigital(player, val, 9,
                                 JOY_BTN(player, JOFS_RTRIG));
            break;
        default:
            break;
        }
        break;
    }

    /* ---- Joystick (non-SDL_GameController devices) ---- */
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        updateDevice(ACTIONMAP_DEVICE_GAMEPAD);
        break;

    case SDL_JOYAXISMOTION:
        if (ev->jaxis.value > 4000 || ev->jaxis.value < -4000) {
            updateDevice(ACTIONMAP_DEVICE_GAMEPAD);
        }
        break;

    default:
        break;
    }
}

static void actionmapZeroGameplayAxes(s32 player, s32 zero_move, s32 zero_aim)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) {
        return;
    }

    if (zero_move) {
        s_State[player][ACTION_AXIS_MOVE_X].value = 0.0f;
        s_State[player][ACTION_AXIS_MOVE_Y].value = 0.0f;
        s_State[player][ACTION_AXIS_MOVE_X].held = 0;
        s_State[player][ACTION_AXIS_MOVE_Y].held = 0;
    }

    if (zero_aim) {
        s_State[player][ACTION_AXIS_AIM_X].value = 0.0f;
        s_State[player][ACTION_AXIS_AIM_Y].value = 0.0f;
        s_State[player][ACTION_AXIS_AIM_X].held = 0;
        s_State[player][ACTION_AXIS_AIM_Y].held = 0;
    }
}

/* ============================================================
 * Public: actionmapPollFrame — analog axis sampling
 * ============================================================ */

void actionmapPollFrame(void)
{
    s_DiagFrameCount++;

    /* B-124 fix (Bug D): When a menu context is active, zero all gameplay
     * axes and skip SDL stick polling entirely. The input context stack
     * blocks SDL EVENTS, but analog stick polling bypasses the event system
     * — without this check the right stick moves the camera behind the menu.
     *
     * ADR 2026-04-13: broadened the trigger from "menu active" to the
     * authority predicate gameplayInputSuppressed(), which also covers
     * window-focus-lost and the focus-regain settle window. */
    InputContext *topCtx = inputCtxGetTop();
    s32 menuActive = gameplayInputSuppressed();
    s32 moveAxesAllowed =
        !menuActive &&
        actionLayerAllows(ACTION_AXIS_MOVE_X) &&
        actionLayerAllows(ACTION_AXIS_MOVE_Y);
    s32 aimAxesAllowed =
        !menuActive &&
        actionLayerAllows(ACTION_AXIS_AIM_X) &&
        actionLayerAllows(ACTION_AXIS_AIM_Y);

    /* DIAG: Log context and active IMCs every ~120 frames (verbose-only). */
    if (sysLogGetVerbose() && (s_DiagFrameCount % 120) == 1) {
        sysLogPrintf(LOG_NOTE, "DIAG poll: frame=%u topCtx='%s' menuActive=%d numActiveIMCs=%d pad0=%p",
                     s_DiagFrameCount,
                     topCtx ? (topCtx->name ? topCtx->name : "unnamed") : "NULL",
                     menuActive, s_NumActive,
                     inputGetPad(0));
        for (s32 ci = 0; ci < s_NumActive; ci++) {
            sysLogPrintf(LOG_NOTE, "DIAG poll: IMC[%d]='%s' prio=%d",
                         ci, s_Active[ci]->name ? s_Active[ci]->name : "?",
                         s_Active[ci]->priority);
        }
    }

    /* Track whether P0 had a controller actively writing to AXIS_MOVE this
     * frame. When no controller is enumerated for P0 (keyboard-only setup),
     * nothing else resets AXIS_MOVE.value — the WASD synthesis block below
     * would leave .value at its last non-zero write forever, because its
     * analogStickActive guard reads the stale .value and short-circuits.
     * The flag lets the synthesis path distinguish "real analog input" from
     * "stale axis value" and always drive the axis from digital keys.
     * (B-152: stuck WASD after menu close, 2026-04-16.) */
    s32 p0CtrlDroveAxis = 0;

    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        SDL_GameController *ctrl = SDL_GameControllerFromPlayerIndex(p);

        if (ctrl && (moveAxesAllowed || aimAxesAllowed)) {
            /* Read raw axes — honour swap sticks setting */
            SDL_GameControllerAxis lxAxis = s_SwapSticks ? SDL_CONTROLLER_AXIS_RIGHTX : SDL_CONTROLLER_AXIS_LEFTX;
            SDL_GameControllerAxis lyAxis = s_SwapSticks ? SDL_CONTROLLER_AXIS_RIGHTY : SDL_CONTROLLER_AXIS_LEFTY;
            SDL_GameControllerAxis rxAxis = s_SwapSticks ? SDL_CONTROLLER_AXIS_LEFTX  : SDL_CONTROLLER_AXIS_RIGHTX;
            SDL_GameControllerAxis ryAxis = s_SwapSticks ? SDL_CONTROLLER_AXIS_LEFTY  : SDL_CONTROLLER_AXIS_RIGHTY;

            s16 lx = SDL_GameControllerGetAxis(ctrl, lxAxis);
            s16 ly = SDL_GameControllerGetAxis(ctrl, lyAxis);
            s16 rx = SDL_GameControllerGetAxis(ctrl, rxAxis);
            s16 ry = SDL_GameControllerGetAxis(ctrl, ryAxis);

            f32 nlx = lx / 32767.0f;
            f32 nly = ly / 32767.0f;
            f32 nrx = rx / 32767.0f;
            f32 nry = ry / 32767.0f;

            /* Negate Y before radial processing: SDL Y+ = down, game Y+ = forward/up */
            nly = -nly;
            nry = -nry;

            f32 mvx, mvy, avx, avy;
            applyRadialStick2D(nlx, nly, s_StickDzMove, s_StickSensMove, &mvx, &mvy);
            applyRadialStick2D(nrx, nry, s_StickDzAim, s_StickSensAim, &avx, &avy);

            /* Optional controller Y-invert for aim axis only */
            if (s_StickInvertY) {
                avy = -avy;
            }

            if (moveAxesAllowed) {
                s_State[p][ACTION_AXIS_MOVE_X].value = clampf(mvx, -1.0f, 1.0f);
                s_State[p][ACTION_AXIS_MOVE_Y].value = clampf(mvy, -1.0f, 1.0f);
                s_State[p][ACTION_AXIS_MOVE_X].held = (mvx != 0.0f) ? 1 : 0;
                s_State[p][ACTION_AXIS_MOVE_Y].held = (mvy != 0.0f) ? 1 : 0;
            } else {
                actionmapZeroGameplayAxes(p, 1, 0);
            }

            if (aimAxesAllowed) {
                s_State[p][ACTION_AXIS_AIM_X].value = clampf(avx, -1.0f, 1.0f);
                s_State[p][ACTION_AXIS_AIM_Y].value = clampf(avy, -1.0f, 1.0f);
                s_State[p][ACTION_AXIS_AIM_X].held  = (avx != 0.0f) ? 1 : 0;
                s_State[p][ACTION_AXIS_AIM_Y].held  = (avy != 0.0f) ? 1 : 0;
            } else {
                actionmapZeroGameplayAxes(p, 0, 1);
            }

            if (p == 0 && moveAxesAllowed) {
                p0CtrlDroveAxis = 1;
            }

            /* DIAG: Log raw and processed axis values every ~120 frames (verbose-only). */
            if (sysLogGetVerbose() && (s_DiagFrameCount % 120) == 1 && p == 0 &&
                (lx != 0 || ly != 0 || rx != 0 || ry != 0)) {
                sysLogPrintf(LOG_NOTE, "DIAG axes p%d: raw lx=%d ly=%d rx=%d ry=%d -> mv=%.3f,%.3f av=%.3f,%.3f",
                             p, (int)lx, (int)ly, (int)rx, (int)ry, mvx, mvy, avx, avy);
            }
        } else if (ctrl) {
            /* Layer or inputctx authority blocks generic gameplay axes. */
            actionmapZeroGameplayAxes(p, 1, 1);
        }
    }

    if (!moveAxesAllowed || !aimAxesAllowed) {
        actionmapZeroGameplayAxes(0, !moveAxesAllowed, !aimAxesAllowed);
    }

    if (!moveAxesAllowed) {
        /* Move axes blocked: skip player 0 keyboard synthesis too. */
        return;
    }

    /* C-1 fix: Mouse aim is NOT routed through ACTION_AXIS_AIM.
     * Mouse aiming goes exclusively through inputMouseGetScaledDelta() →
     * movedata.freelookdx/dy in bondmove.c, exactly as the original port worked.
     * ACTION_AXIS_AIM_X/Y are for controller right stick only. */

    /* B-124 fix (Bug C): WASD→AXIS_MOVE synthesis always runs for player 0,
     * regardless of s_LastDevice. Previous code gated this on KBM device,
     * so any gamepad event (stick noise) would switch s_LastDevice to GAMEPAD
     * and WASD synthesis would stop — causing "moves for 1 frame" behavior.
     * Now: gamepad sticks write first (above), then WASD overrides IF any
     * WASD key is held. Both inputs can coexist.
     *
     * 2026-04-12: Skip synthesis when the analog stick path already set
     * non-zero axis values. LSTICK_UP/DOWN/LEFT/RIGHT are now bound to
     * the movement actions (for rebind UI display), so their synthetic
     * digital VKs fire when the stick crosses the threshold. Without this
     * guard, the synthesis would snap smooth analog values to digital ±1.0
     * whenever the stick passes the threshold. */
    {
        f32 axmx = s_State[0][ACTION_AXIS_MOVE_X].value;
        f32 axmy = s_State[0][ACTION_AXIS_MOVE_Y].value;
        /* B-152: only treat the axis as "driven by analog stick" when a
         * controller actually wrote it this frame. If p0CtrlDroveAxis is 0,
         * any non-zero axmx/axmy we see is stale data from a previous WASD
         * synthesis write, not live stick input. Treating it as analog-active
         * would skip the synthesis block below and leave AXIS_MOVE pegged at
         * its previous value even after WASD keys were released. */
        bool analogStickActive = p0CtrlDroveAxis && (axmx != 0.0f || axmy != 0.0f);

        f32 mx = 0.0f, my = 0.0f;
        if (s_State[0][ACTION_MOVE_RIGHT].held)    mx += 1.0f;
        if (s_State[0][ACTION_MOVE_LEFT].held)     mx -= 1.0f;
        if (s_State[0][ACTION_MOVE_FORWARD].held)  my += 1.0f;  /* Y+ = forward (N64 convention) */
        if (s_State[0][ACTION_MOVE_BACKWARD].held) my -= 1.0f;

        if (!analogStickActive) {
            if (mx != 0.0f || my != 0.0f) {
                /* Normalize diagonal */
                f32 len = sqrtf(mx * mx + my * my);
                if (len > 1.0f) { mx /= len; my /= len; }
            }
            /* Always assign — when no digital keys are held, this writes 0,0
             * and clears any stuck axis value from a previous synthesis frame.
             * (B-152.) */
            s_State[0][ACTION_AXIS_MOVE_X].value = mx;
            s_State[0][ACTION_AXIS_MOVE_Y].value = my;
            s_State[0][ACTION_AXIS_MOVE_X].held  = (mx != 0.0f) ? 1 : 0;
            s_State[0][ACTION_AXIS_MOVE_Y].held  = (my != 0.0f) ? 1 : 0;
        }
    }

    /* DIAG: Log final axis values every ~120 frames for player 0 (verbose-only). */
    if (sysLogGetVerbose() && (s_DiagFrameCount % 120) == 1) {
        f32 mvx = s_State[0][ACTION_AXIS_MOVE_X].value;
        f32 mvy = s_State[0][ACTION_AXIS_MOVE_Y].value;
        f32 amx = s_State[0][ACTION_AXIS_AIM_X].value;
        f32 amy = s_State[0][ACTION_AXIS_AIM_Y].value;
        if (mvx != 0.0f || mvy != 0.0f || amx != 0.0f || amy != 0.0f) {
            sysLogPrintf(LOG_NOTE, "DIAG axis: move=%.3f,%.3f aim=%.3f,%.3f fwd=%d back=%d left=%d right=%d",
                         mvx, mvy, amx, amy,
                         s_State[0][ACTION_MOVE_FORWARD].held,
                         s_State[0][ACTION_MOVE_BACKWARD].held,
                         s_State[0][ACTION_MOVE_LEFT].held,
                         s_State[0][ACTION_MOVE_RIGHT].held);
        }
    }
}

/* ============================================================
 * Public: actionmapEndFrame
 * ============================================================ */

void actionmapEndFrame(void)
{
    /* M-2: Rebuild wheel-bound action cache (only when contexts change, but cheap enough per-frame). */
    s_NumWheelActions = 0;
    for (s32 ci = 0; ci < s_NumActive; ci++) {
        InputMappingContext *ctx = s_Active[ci];
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (!ctx->has_mapping[a]) continue;
            InputMapping *m = &ctx->mappings[a];
            for (s32 ti = 0; ti < m->num_triggers; ti++) {
                u32 vk = m->triggers[ti].vk;
                if (vk == VK_MOUSE_WHEEL_UP || vk == VK_MOUSE_WHEEL_DN) {
                    /* Avoid duplicates */
                    s32 dup = 0;
                    for (s32 k = 0; k < s_NumWheelActions; k++) {
                        if (s_WheelActions[k] == (InputAction)a) { dup = 1; break; }
                    }
                    if (!dup && s_NumWheelActions < ACTION_COUNT) {
                        s_WheelActions[s_NumWheelActions++] = (InputAction)a;
                    }
                }
            }
        }
    }

    /* Debounce: promote raw device to stable last device after timeout */
    if (s_RawDevice != s_LastDevice) {
        u32 now     = SDL_GetTicks();
        u32 elapsed = now - s_DeviceChangeTime;
        if (elapsed >= ACTIONMAP_DEVICE_DEBOUNCE_MS) {
            s_LastDevice = s_RawDevice;
        }
    }

    /* Clear edge signals and auto-release mouse wheel (momentary) */
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            ActionState *st = &s_State[p][a];
            st->pressed  = 0;
            st->released = 0;

            /* Wheel auto-release is now handled by the M-2 pre-computed array below. */
        }

        /* M-2: Auto-release wheel-bound actions using pre-computed list (avoids O(contexts*actions*triggers) scan). */
        for (s32 wi = 0; wi < s_NumWheelActions; wi++) {
            ActionState *st = &s_State[p][s_WheelActions[wi]];
            if (st->held) {
                st->held     = 0;
                st->released = 1;
                st->value    = 0.0f;
            }
        }
    }
}

/* ============================================================
 * Input authority: classification + flush helpers
 *
 * See context/designs/input-authority-and-menu-pool-2026-04-13.md.
 * ============================================================ */

s32 actionIsGameplayOnly(InputAction a)
{
    if (a < 0 || a >= ACTION_COUNT) {
        return 0;
    }

    /* Menu navigation — explicitly owned by the menu layer. */
    if (a >= ACTION_MENU_UP && a <= ACTION_MENU_TAB_NEXT) {
        return 0;
    }

    /* Shared / system actions: menus legitimately consume these too.
     * (ACTION_USE and ACTION_CANCEL_USE alias ACTION_MENU_ACCEPT/CANCEL.) */
    switch (a) {
    case ACTION_USE:
    case ACTION_CANCEL_USE:
    case ACTION_PAUSE:
    case ACTION_SCORECARD:
    case ACTION_SCORECARD_HOLD:
    case ACTION_SOCIAL_TOGGLE:
    case ACTION_SCREENSHOT:
    case ACTION_CONSOLE_TOGGLE:
    case ACTION_DEBUG_TOGGLE:
    case ACTION_CHEAT_ENTER:
    case ACTION_TEXT_PASTE:
    case ACTION_SKIP_CUTSCENE:
    case ACTION_MENU_SECONDARY:
    case ACTION_MENU_TERTIARY:
    case ACTION_MENU_DELETE:
    case ACTION_VOICE_PTT:
    case ACTION_FORGE_PLACE_CANCEL:
    case ACTION_FORGE_BOT_ADD:
    case ACTION_FORGE_BOT_REMOVE_ALL:
    case ACTION_FORGE_BOT_FREEZE_TOGGLE:
    case ACTION_FORGE_BOT_SPAWN_CYCLE:
    case ACTION_SKIN_BRUSH_DECREASE:
    case ACTION_SKIN_BRUSH_INCREASE:
    case ACTION_SKIN_TOOL_DRAW:
    case ACTION_SKIN_TOOL_ERASE:
    case ACTION_SKIN_TOOL_FILL:
    case ACTION_SKIN_TOOL_EYEDROPPER:
    case ACTION_SKIN_TOOL_LINE:
    case ACTION_SKIN_GRID_TOGGLE:
    case ACTION_SKIN_UV_TOGGLE:
    case ACTION_SKIN_UNDO:
    case ACTION_SKIN_REDO:
    case ACTION_SKIN_SAVE:
        return 0;
    default:
        return 1;
    }
}

static s32 actionLayerAllows(InputAction a)
{
    const LayerHandle *top = inputLayerTop();
    const LayerDef *def = top ? inputLayerHandleDef(top) : NULL;

    if (def && def->action_set && def->action_set_count > 0 && actionIsGameplayOnly(a)) {
        for (s32 i = 0; i < def->action_set_count; i++) {
            if (def->action_set[i] == a) {
                return 1;
            }
        }
        return 0;
    }

    if (gameplayInputSuppressed() && actionIsGameplayOnly(a)) {
        return 0;
    }

    return 1;
}

/* Priority D (2026-04-24): FREEFLY observer suppression.
 *
 * When the Forge session is in FREEFLY, the player-chr is frozen and
 * re-skinned as Dr. Carroll. Combat actions (fire, reload, weapon
 * swap, radial menu, interact-as-gameplay) must NOT fire -- they'd
 * shoot an invisible weapon from the observer camera or pick up
 * objects Dr. Carroll isn't really touching.
 *
 * Freefly-specific actions (the camera axes + forge toggle + ascend /
 * descend / boost / precision) DO stay live so the observer can fly
 * and exit. Movement axes also stay live so the freefly camera can
 * reuse them for translation. */
s32 actionIsBlockedInFreefly(InputAction a)
{
    if (a < 0 || a >= ACTION_COUNT) {
        return 0;
    }

    switch (a) {
    /* Combat: block */
    case ACTION_FIRE_PRIMARY:
    case ACTION_FIRE_SECONDARY:
    case ACTION_FIRE_MODE:
    case ACTION_RELOAD:
    case ACTION_THROW_WEAPON:
    case ACTION_ZOOM_IN:
    case ACTION_ZOOM_OUT:
    /* Weapon selection / radial: block */
    case ACTION_WEAPON_PREV:
    case ACTION_WEAPON_NEXT:
    case ACTION_WEAPON_1:
    case ACTION_WEAPON_2:
    case ACTION_WEAPON_3:
    case ACTION_WEAPON_4:
    case ACTION_WEAPON_5:
    case ACTION_WEAPON_6:
    /* Vehicle interaction: block (observer is not in a vehicle) */
    case ACTION_VEHICLE_ACCELERATE:
    case ACTION_VEHICLE_BRAKE:
    case ACTION_VEHICLE_STEER_LEFT:
    case ACTION_VEHICLE_STEER_RIGHT:
    case ACTION_VEHICLE_EXIT:
    /* Crouch / jump / sprint on the frozen player-chr: block */
    case ACTION_CROUCH:
    case ACTION_JUMP:
    case ACTION_SPRINT:
    /* Interact-as-gameplay: block. FREEFLY has its own observer
     * interaction model (placement reticle) that bypasses the chr. */
    case ACTION_USE:
    case ACTION_CANCEL_USE:
        return 1;
    default:
        /* Everything else (movement, aim axes, forge editor actions,
         * menu nav, system) passes through. */
        return 0;
    }
}

static void actionmapFlushStateSlot(ActionState *st, u32 now)
{
    s32 wasHeld = st->held;
    st->held          = 0;
    st->pressed       = 0;
    st->released      = wasHeld ? 1 : st->released;
    st->value         = 0.0f;
    if (wasHeld) {
        st->up_time_ms = now;
    }
    st->hold_consumed = 0;
    st->hold_pin_full_until_ms = 0;
    st->hold_vis_grace_until_ms = 0;
    st->hold_vis_last_down_progress = 0.0f;
}

void actionmapFlushGameplayState(void)
{
    /* Zero every gameplay-only action's state across all players. Issues a
     * synthetic "released" edge so any consumer that latched on press sees a
     * corresponding release. */
    u32 now = SDL_GetTicks();
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (!actionIsGameplayOnly((InputAction)a)) {
                continue;
            }
            actionmapFlushStateSlot(&s_State[p][a], now);
        }
        /* Stick threshold bookkeeping: clear latched digital-from-axis state so
         * that when gameplay resumes, a subsequent axis below threshold does
         * NOT fire a spurious release (which could be consumed by a menu
         * handler as e.g. MOVE_LEFT toggling off). */
        for (s32 i = 0; i < 10; i++) {
            s_StickHeld[p][i] = 0;
        }
    }
}

void actionmapFlushActionSet(const InputAction *actions, s32 action_count)
{
    if (!actions || action_count <= 0) {
        return;
    }

    u32 now = SDL_GetTicks();
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 i = 0; i < action_count; i++) {
            InputAction a = actions[i];
            if (a < 0 || a >= ACTION_COUNT) {
                continue;
            }
            actionmapFlushStateSlot(&s_State[p][a], now);
        }
    }
}

/* ============================================================
 * Public: query API (5 functions)
 * ============================================================ */

s32 actionPressed(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    /* Input-authority gate: when a top layer declares a gameplay action
     * aperture, gameplay-only reads must be in that set. Otherwise, fall
     * back to the transitional inputctx suppression predicate. */
    if (!actionLayerAllows(action)) return 0;
    /* Priority D (2026-04-24): FREEFLY observer suppression. */
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    return s_State[player][action].pressed;
}

s32 actionHeld(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (!actionLayerAllows(action)) return 0;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    return s_State[player][action].held;
}

s32 actionReleased(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (!actionLayerAllows(action)) return 0;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    return s_State[player][action].released;
}

f32 actionValue(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0.0f;
    if (action < 0 || action >= ACTION_COUNT) return 0.0f;
    if (!actionLayerAllows(action)) return 0.0f;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0.0f;
    return s_State[player][action].value;
}

void actionAxis(s32 player, InputAction action, f32 *out_x, f32 *out_y)
{
    if (!out_x || !out_y) return;
    *out_x = 0.0f;
    *out_y = 0.0f;

    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return;
    if (action < 0 || action >= ACTION_COUNT) return;

    /* Gate axis reads through the layer-declared gameplay aperture. */
    if (!actionLayerAllows(action)) return;

    /* Axis-pair actions: return both components */
    if (action == ACTION_AXIS_MOVE_X || action == ACTION_AXIS_MOVE_Y) {
        *out_x = s_State[player][ACTION_AXIS_MOVE_X].value;
        *out_y = s_State[player][ACTION_AXIS_MOVE_Y].value;
    } else if (action == ACTION_AXIS_AIM_X || action == ACTION_AXIS_AIM_Y) {
        *out_x = s_State[player][ACTION_AXIS_AIM_X].value;
        *out_y = s_State[player][ACTION_AXIS_AIM_Y].value;
    } else {
        *out_x = s_State[player][action].value;
        *out_y = 0.0f;
    }
}

/* ============================================================
 * Public: hold/tap discrimination helpers
 * ============================================================ */

s32 actionHeldForMs(s32 player, InputAction action, s32 threshold_ms)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (!actionLayerAllows(action)) return 0;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    const ActionState *st = &s_State[player][action];
    if (!st->held || st->down_time_ms == 0) return 0;
    if (threshold_ms <= 0) return 1;
    u32 now = SDL_GetTicks();
    return (s32)(now - st->down_time_ms) >= threshold_ms;
}

s32 actionWasTap(s32 player, InputAction action, s32 max_hold_ms)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (!actionLayerAllows(action)) return 0;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    const ActionState *st = &s_State[player][action];
    if (!st->released) return 0;
    if (st->hold_consumed) return 0;
    if (st->down_time_ms == 0) return 0;
    s32 elapsed = (s32)(st->up_time_ms - st->down_time_ms);
    return (elapsed >= 0 && elapsed < max_hold_ms);
}

s32 actionLastGestureHoldMs(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (!actionLayerAllows(action)) return 0;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    const ActionState *st = &s_State[player][action];
    if (!st->released) return 0;
    if (st->down_time_ms == 0) return 0;
    s32 elapsed = (s32)(st->up_time_ms - st->down_time_ms);
    return (elapsed >= 0) ? elapsed : 0;
}

void actionConsumeHold(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return;
    if (action < 0 || action >= ACTION_COUNT) return;
    if (!actionLayerAllows(action)) return;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return;
    {
        ActionState *st = &s_State[player][action];
        st->hold_consumed = 1;
        /* B-221.2: hold ring at 1.0 briefly after long-hold fires (matches UI expectation). */
        st->hold_pin_full_until_ms = SDL_GetTicks() + 200;
    }
}

s32 actionHoldConsumed(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (!actionLayerAllows(action)) return 0;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0;
    return s_State[player][action].hold_consumed;
}

f32 actionHoldProgress(s32 player, InputAction action, s32 threshold_ms)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0.0f;
    if (action < 0 || action >= ACTION_COUNT) return 0.0f;
    if (!actionLayerAllows(action)) return 0.0f;
    if (forgeIsFreefly() && actionIsBlockedInFreefly(action)) return 0.0f;
    if (threshold_ms <= 0) return 0.0f;
    ActionState *st = &s_State[player][action];
    u32 now = SDL_GetTicks();

    if (st->hold_pin_full_until_ms && now < st->hold_pin_full_until_ms) {
        return 1.0f;
    }
    if (st->hold_pin_full_until_ms && now >= st->hold_pin_full_until_ms) {
        st->hold_pin_full_until_ms = 0;
    }
    if (!st->held || st->down_time_ms == 0) {
        if (st->hold_vis_grace_until_ms && now < st->hold_vis_grace_until_ms) {
            return st->hold_vis_last_down_progress;
        }
        return 0.0f;
    }
    {
        s32 elapsed = (s32)(now - st->down_time_ms);
        f32 raw;
        /* Only reject clock skew; allow elapsed==0 to show a sliver of fill on
         * the first frame so the hold ring does not stay empty until a full ms. */
        if (elapsed < 0) {
            return 0.0f;
        }
		if (elapsed >= threshold_ms) {
			raw = 1.0f;
		} else {
			raw = (f32)elapsed / (f32)threshold_ms;
		}
        st->hold_vis_last_down_progress = raw;
        return raw;
    }
}

u32 actionHoldPressStartMs(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) {
        return 0;
    }
    if (action < 0 || action >= ACTION_COUNT) {
        return 0;
    }
    if (!actionLayerAllows(action)) {
        return 0;
    }
    const ActionState *st = &s_State[player][action];
    if (!st->held || st->down_time_ms == 0) {
        return 0;
    }
    return st->down_time_ms;
}

/* ============================================================
 * Public: last-device detection
 * ============================================================ */

s32 actionmapGetLastDevice(void)
{
    return s_LastDevice;
}

/* ============================================================
 * Bind management helpers
 * ============================================================ */

/* All known IMCs — used for save/load to iterate regardless of active state.
 *
 * Order matters: buildBindStr / actionmapLoadBinds use first-match-wins
 * semantics. Gameplay must come first so the shared baseline (movement,
 * combat, weapons, interact) is the canonical owner of those bindings in
 * pd.ini. Mission / CombatSim sit above gameplay in the iteration but only
 * own actions they explicitly bind (today: ACTION_SCORECARD_HOLD on
 * CombatSim), so they never shadow the baseline at save/load time. */
static InputMappingContext * const s_AllImcs[] = {
    &g_ImcGameplay,
    &g_ImcCutscene,
    &g_ImcMission,
    &g_ImcCombatSim,
    &g_ImcVehicle,
    &g_ImcForgeSession,
    &g_ImcForge,
    &g_ImcObserver,
    &g_ImcMenu,
    &g_ImcPauseMenu,
    &g_ImcDebugOverlay,
    &g_ImcTextInput,
};
static const s32 s_NumAllImcs = (s32)(sizeof(s_AllImcs) / sizeof(s_AllImcs[0]));

/** Build a comma-separated bind string from the triggers of action a for player p.
 *  Iterates ALL known IMCs (not just active ones) so inactive IMC binds are persisted. */
static void buildBindStr(s32 player, InputAction action,
                         char *out, s32 outlen)
{
    out[0] = '\0';

    for (s32 ci = 0; ci < s_NumAllImcs; ci++) {
        InputMappingContext *ctx = s_AllImcs[ci];
        if (!ctx->has_mapping[action]) continue;

        InputMapping *m = &ctx->mappings[action];
        s32 written = 0;
        for (s32 ti = 0; ti < m->num_triggers; ti++) {
            u32 vk = m->triggers[ti].vk;
            if (vk == 0) continue;
            /* Only include triggers belonging to this player */
            if (playerForVk(vk) != player) continue;

            const char *name = actionmapGetVkName(vk);
            if (!name || name[0] == '\0') continue;

            s32 curlen = (s32)strlen(out);
            if (written > 0 && curlen + 1 < outlen) {
                out[curlen] = ',';
                out[curlen + 1] = '\0';
                curlen++;
            }
            s32 remaining = outlen - curlen - 1;
            if (remaining > 0) {
                strncat(out, name, (size_t)remaining);
                written++;
            }
        }
        break; /* first IMC with the action wins */
    }
}

/** Parse a comma-separated bind string and populate triggers in imc for player p. */
static void parseBindStr(InputMappingContext *imc, s32 player,
                         InputAction action, const char *str)
{
    if (!imc || !str || str[0] == '\0') return;

    char buf[BIND_STR_MAX];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Remove old player-owned triggers for this action */
    if (imc->has_mapping[action]) {
        InputMapping *m = &imc->mappings[action];
        s32 write = 0;
        for (s32 ti = 0; ti < m->num_triggers; ti++) {
            u32 vk = m->triggers[ti].vk;
            if (vk == 0 || playerForVk(vk) == player) continue;
            m->triggers[write++] = m->triggers[ti];
        }
        m->num_triggers = write;
    }

    /* Parse and add new triggers */
    char *token = strtok(buf, ",");
    while (token) {
        /* Trim whitespace */
        while (*token == ' ') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && *end == ' ') *end-- = '\0';

        if (*token) {
            s32 vk = actionmapGetVkByName(token);
            if (vk > 0) {
                actionmapBind(imc, player, action, -1, (u32)vk);
            }
        }
        token = strtok(NULL, ",");
    }
}

/* ============================================================
 * Public: bind management
 * ============================================================ */

void actionmapBind(InputMappingContext *imc, s32 player,
                   InputAction action, s32 slot, u32 vk)
{
    if (!imc) return;
    if (action < 0 || action >= ACTION_COUNT) return;
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return;

    InputMapping *m = &imc->mappings[action];
    m->action = action;
    imc->has_mapping[action] = 1;

    if (slot < 0) {
        /* Auto-pick: find a free slot or append */
        for (s32 ti = 0; ti < ACTIONMAP_MAX_TRIGGERS; ti++) {
            if (m->triggers[ti].vk == 0 || m->triggers[ti].vk == vk) {
                m->triggers[ti].vk = vk;
                if (ti >= m->num_triggers) m->num_triggers = ti + 1;
                return;
            }
        }
        /* All slots full: overwrite last */
        slot = ACTIONMAP_MAX_TRIGGERS - 1;
    }

    if (slot < 0 || slot >= ACTIONMAP_MAX_TRIGGERS) return;
    m->triggers[slot].vk = vk;
    if (slot >= m->num_triggers) {
        m->num_triggers = slot + 1;
    }
}

static void actionmapSerializeHoldOverridesToIniStr(void)
{
    char *p = s_HoldOverridesIniStr;
    char *end = s_HoldOverridesIniStr + HOLD_OVERRIDES_STR_MAX - 1;
    *p = '\0';
    for (s32 a = 0; a < ACTION_COUNT; a++) {
        if (s_ActionHoldMsOverride[a] < 0) {
            continue;
        }
        size_t room = (size_t)(end - p + 1);
        if (room < 8) {
            break;
        }
        int w = snprintf(p, room, "%s%d:%d",
                         p > s_HoldOverridesIniStr ? "," : "", (int)a,
                         (int)s_ActionHoldMsOverride[a]);
        if (w < 0 || (size_t)w >= room) {
            break;
        }
        p += w;
    }
    if (p <= end) {
        *p = '\0';
    } else {
        s_HoldOverridesIniStr[HOLD_OVERRIDES_STR_MAX - 1] = '\0';
    }
}

static void actionmapParseHoldOverridesFromIniStr(void)
{
    for (s32 a = 0; a < ACTION_COUNT; a++) {
        s_ActionHoldMsOverride[a] = -1;
    }
    if (s_HoldOverridesIniStr[0] == '\0') {
        return;
    }
    char work[HOLD_OVERRIDES_STR_MAX];
    strncpy(work, s_HoldOverridesIniStr, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';
    char *tok = strtok(work, ",");
    while (tok) {
        int aid = -1;
        int ms = -1;
        if (sscanf(tok, "%d : %d", &aid, &ms) >= 2 || sscanf(tok, "%d:%d", &aid, &ms) >= 2) {
            if (aid >= 0 && aid < (int)ACTION_COUNT && ms >= 0 && ms <= ACTIONMAP_HOLD_MS_MAX) {
                s_ActionHoldMsOverride[aid] = ms;
            }
        }
        tok = strtok(NULL, ",");
    }
}

void actionmapSaveBinds(void)
{
    actionmapSerializeHoldOverridesToIniStr();
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            buildBindStr(p, (InputAction)a,
                         s_BindStr[p][a], BIND_STR_MAX);
        }
    }
}

void actionmapLoadBinds(void)
{
    actionmapRefreshStickMultFromUi();

    /* Parse the (possibly file-overridden) bind strings into the FIRST IMC
     * whose has_mapping[] marks the action as its own.
     *
     * 2026-04-11 fix: apply the loaded bind to ONLY the first matching IMC,
     * not every IMC that happens to have_mapping[a] set.  The previous
     * behaviour cross-contaminated menu / pause_menu / text_input with
     * gameplay-saved bindings for shared actions (USE, CANCEL_USE, PAUSE):
     * - buildBindStr() is already first-match-wins (breaks after the first
     *   IMC with has_mapping[a] is serialised), so s_BindStr holds the
     *   gameplay trigger list for shared actions;
     * - actionmapLoadBinds() used to write that same string into every IMC,
     *   so the menu IMC would end up with gameplay bindings (F, JOY_Y for
     *   USE) instead of its own defaults (Return, A).  Net effect: the
     *   rebind UI correctly wrote gameplay bindings to pd.ini and showed
     *   them on subsequent opens, but the LIVE menu IMC overwrote its own
     *   action triggers with gameplay ones on every load, so pressing the
     *   rebound key in gameplay worked, but held defaults in menus broke.
     *
     * The break; below matches buildBindStr's first-match-wins semantics so
     * the load path is the exact inverse of the save path.  Menu-only
     * actions (MENU_UP / MENU_TAB_PREV / etc.) still land in g_ImcMenu
     * because menu is the first (and only) IMC with has_mapping[a] for
     * those. */
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (s_BindStr[p][a][0] == '\0') continue;
            for (s32 ci = 0; ci < s_NumAllImcs; ci++) {
                if (s_AllImcs[ci]->has_mapping[a]) {
                    parseBindStr(s_AllImcs[ci], p, (InputAction)a, s_BindStr[p][a]);
                    break; /* first IMC wins — matches buildBindStr */
                }
            }
        }
    }
    sysLogPrintf(LOG_NOTE, "ACTIONMAP: binds loaded from pd.ini into %d IMCs", s_NumAllImcs);

    /* Migration: strip stale JOFS_LSTICK_* binds from movement actions.
     * Old pd.ini files bound MOVE_FORWARD/BACKWARD/LEFT/RIGHT to left-stick
     * VKs. These clobber analog movement (the digital path overwrites smooth
     * stick_x/y with snapped ±1 values). Strip them on load and rewrite
     * s_BindStr so the next configSave() persists the fix. */
    static const InputAction s_MoveActions[] = {
        ACTION_MOVE_FORWARD,
        ACTION_MOVE_BACKWARD,
        ACTION_MOVE_LEFT,
        ACTION_MOVE_RIGHT,
    };
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 ai = 0; ai < 4; ai++) {
            InputAction a = s_MoveActions[ai];
            if (!g_ImcGameplay.has_mapping[a]) continue;
            InputMapping *m = &g_ImcGameplay.mappings[a];
            s32 stripped = 0;
            s32 write    = 0;
            for (s32 ti = 0; ti < m->num_triggers; ti++) {
                u32 vk = m->triggers[ti].vk;
                if (vk != 0 && vk >= (u32)VK_JOY1_BEGIN) {
                    u32 btn = (vk - (u32)VK_JOY1_BEGIN) % (u32)INPUT_MAX_CONTROLLER_BUTTONS;
                    if (btn >= JOFS_LSTICK_LEFT && btn <= JOFS_LSTICK_DOWN) {
                        stripped++;
                        continue; /* drop this trigger */
                    }
                }
                m->triggers[write++] = m->triggers[ti];
            }
            if (stripped > 0) {
                m->num_triggers = write;
                buildBindStr(p, a, s_BindStr[p][a], BIND_STR_MAX);
                sysLogPrintf(LOG_NOTE,
                    "ACTIONMAP: stripped stale LSTICK movement bind from pd.ini"
                    " (player %d, action %d, %d bind(s) removed)",
                    p, (s32)a, stripped);
            }
        }
    }

    actionmapParseHoldOverridesFromIniStr();
}

/* ============================================================
 * Public: cheat code buffer
 * ============================================================ */

s32 actionmapCheckCheat(const InputAction *seq, s32 len)
{
    if (!seq || len <= 0 || len > ACTIONMAP_CHEAT_BUF_LEN) return 0;
    if (s_CheatCount < len) return 0;

    /* Walk backwards from the most-recent entry */
    for (s32 i = 0; i < len; i++) {
        s32 bufIdx = (s_CheatHead - 1 - i + ACTIONMAP_CHEAT_BUF_LEN)
                     % ACTIONMAP_CHEAT_BUF_LEN;
        if (s_CheatBuf[bufIdx] != seq[len - 1 - i]) return 0;
    }
    return 1;
}

void actionmapClearCheat(void)
{
    memset(s_CheatBuf, 0, sizeof(s_CheatBuf));
    s_CheatHead  = 0;
    s_CheatCount = 0;
}

/* ============================================================
 * Stick tuning getter/setter API
 * ============================================================ */

f32  actionmapGetStickSensitivity(void) { return s_StickSensMove; }
void actionmapSetStickSensitivity(f32 v)
{
    f32 c = clampf(v, 0.1f, 3.0f);
    s_StickSensMove = c;
    s_StickSensAim  = c;
    s_SensMoveUi = actionmapSnapSensUi(actionmapMultToSensUi(c));
    s_SensAimUi  = s_SensMoveUi;
}

f32  actionmapGetStickDeadzone(void) { return s_StickDzMove; }
void actionmapSetStickDeadzone(f32 v)
{
    f32 c = clampf(v, 0.0f, 0.5f);
    s_StickDzMove = c;
    s_StickDzAim  = c;
}

f32  actionmapGetStickSensitivityMove(void) { return s_StickSensMove; }
void actionmapSetStickSensitivityMove(f32 v)
{
    s_StickSensMove = clampf(v, 0.1f, 3.0f);
    s_SensMoveUi = actionmapSnapSensUi(actionmapMultToSensUi(s_StickSensMove));
}
f32  actionmapGetStickSensitivityAim(void) { return s_StickSensAim; }
void actionmapSetStickSensitivityAim(f32 v)
{
    s_StickSensAim = clampf(v, 0.1f, 3.0f);
    s_SensAimUi = actionmapSnapSensUi(actionmapMultToSensUi(s_StickSensAim));
}

f32 actionmapSnapSensUi(f32 ui)
{
    if (ui < 1.f) {
        ui = 1.f;
    }
    if (ui > 10.f) {
        ui = 10.f;
    }
    return roundf(ui * 2.f) * 0.5f;
}

f32 actionmapSensUiToMult(f32 ui)
{
    ui = actionmapSnapSensUi(ui);
    return 0.1f + (ui - 1.f) * (2.9f / 9.f);
}

f32 actionmapMultToSensUi(f32 mult)
{
    mult = clampf(mult, 0.1f, 3.0f);
    f32 ui = 1.f + (mult - 0.1f) * (9.f / 2.9f);
    return actionmapSnapSensUi(ui);
}

void actionmapRefreshStickMultFromUi(void)
{
    s_SensMoveUi = actionmapSnapSensUi(s_SensMoveUi);
    s_SensAimUi = actionmapSnapSensUi(s_SensAimUi);
    s_SensAdsUi = actionmapSnapSensUi(s_SensAdsUi);
    s_StickSensMove = actionmapSensUiToMult(s_SensMoveUi);
    s_StickSensAim = actionmapSensUiToMult(s_SensAimUi);
}

f32 actionmapGetSensMoveUi(void) { return s_SensMoveUi; }
void actionmapSetSensMoveUi(f32 ui)
{
    s_SensMoveUi = actionmapSnapSensUi(ui);
    s_StickSensMove = actionmapSensUiToMult(s_SensMoveUi);
}

f32 actionmapGetSensAimUi(void) { return s_SensAimUi; }
void actionmapSetSensAimUi(f32 ui)
{
    s_SensAimUi = actionmapSnapSensUi(ui);
    s_StickSensAim = actionmapSensUiToMult(s_SensAimUi);
}

f32 actionmapGetSensAdsUi(void) { return s_SensAdsUi; }
void actionmapSetSensAdsUi(f32 ui)
{
    s_SensAdsUi = actionmapSnapSensUi(ui);
}

f32 actionmapGetPcAdsZoomFovMul(void)
{
    f32 u = actionmapSnapSensUi(s_SensAdsUi);
    f32 t = (u - 1.f) / 9.f;
    return 0.93f - t * 0.06f;
}
f32  actionmapGetStickDeadzoneMove(void) { return s_StickDzMove; }
void actionmapSetStickDeadzoneMove(f32 v) { s_StickDzMove = clampf(v, 0.0f, 0.5f); }
f32  actionmapGetStickDeadzoneAim(void) { return s_StickDzAim; }
void actionmapSetStickDeadzoneAim(f32 v) { s_StickDzAim = clampf(v, 0.0f, 0.5f); }

s32  actionmapGetUseHoldThresholdMs(void) { return s_UseHoldThresholdMs; }
void actionmapSetUseHoldThresholdMs(s32 ms)
{
    if (ms < 50) ms = 50;
    if (ms > ACTIONMAP_HOLD_MS_MAX) ms = ACTIONMAP_HOLD_MS_MAX;
    s_UseHoldThresholdMs = ms;
}

s32 actionmapGetActionHoldMsOverride(InputAction action)
{
    if (action < 0 || action >= ACTION_COUNT) return -1;
    return s_ActionHoldMsOverride[action];
}

void actionmapSetActionHoldMsOverride(InputAction action, s32 ms)
{
    if (action < 0 || action >= ACTION_COUNT) return;
    if (ms < 0) {
        s_ActionHoldMsOverride[action] = -1;
        return;
    }
    if (ms < 50) {
        ms = 50;
    }
    if (ms > ACTIONMAP_HOLD_MS_MAX) {
        ms = ACTIONMAP_HOLD_MS_MAX;
    }
    s_ActionHoldMsOverride[action] = ms;
}

s32 actionmapGetEffectiveHoldMs(InputAction action)
{
    if (action < 0 || action >= ACTION_COUNT) return 0;
    if (s_ActionHoldMsOverride[action] >= 0) {
        return s_ActionHoldMsOverride[action];
    }
    if (action == ACTION_USE) {
        return s_UseHoldThresholdMs;
    }
    return 0;
}

s32 actionmapGetInteractHoldExtraTerminalMs(void)
{
    return s_InteractHoldExtraTerminalMs;
}

void actionmapSetInteractHoldExtraTerminalMs(s32 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    if (ms > ACTIONMAP_HOLD_MS_MAX) {
        ms = ACTIONMAP_HOLD_MS_MAX;
    }
    s_InteractHoldExtraTerminalMs = ms;
}

void actionmapSetMoveStickPhysicalLeft(s32 useLeft)
{
    s_SwapSticks = useLeft ? 0 : 1;
}

s32 actionmapGetMoveStickPhysicalLeft(void)
{
    return s_SwapSticks ? 0 : 1;
}

s32  actionmapGetStickInvertY(void) { return s_StickInvertY; }
void actionmapSetStickInvertY(s32 v) { s_StickInvertY = v ? 1 : 0; }

/* ============================================================
 * Default IMC definitions
 * ============================================================ */

InputMappingContext g_ImcGameplay = {
    .name     = "gameplay",
    .priority = 0,
    .active   = 0,
};

/* Priority J (2026-04-25): Mission scheme IMC. Priority 1 sits above the
 * always-active gameplay baseline. Today carries no unique bindings --
 * exists for the scene-scope identity, the mutual-exclusion invariant
 * with CombatSim, and so future Mission-only bindings have a home. */
InputMappingContext g_ImcMission = {
    .name     = "mission",
    .priority = 1,
    .active   = 0,
};

/* Priority J (2026-04-25): Combat Sim scheme IMC. Priority 1 (mutually
 * exclusive with Mission). Hosts ACTION_SCORECARD_HOLD on gamepad Back so
 * CS scoreboard requires a hold, while Mission keeps Back inert. Tab on
 * keyboard remains in g_ImcGameplay -> ACTION_SCORECARD (transient peek)
 * across both schemes. */
InputMappingContext g_ImcCombatSim = {
    .name     = "combat_sim",
    .priority = 1,
    .active   = 0,
};

InputMappingContext g_ImcVehicle = {
    .name     = "vehicle",
    .priority = 5,
    .active   = 0,
};

/* Cohort 4 (2026-04-27, K.2): Cutscene-scoped IMC. Priority 4 sits below
 * vehicle (5) so a cutscene fired while the player is mounted still lets
 * the vehicle layer keep its bindings, while still routing dedicated
 * ACTION_SKIP_CUTSCENE above the gameplay (0) / mission (1) / combat-
 * sim (1) baseline. Activated by inputlayer.c's onCutscenePush hook
 * when LAYER_CUTSCENE is pushed; deactivated on pop. */
InputMappingContext g_ImcCutscene = {
    .name     = "cutscene",
    .priority = 4,
    .active   = 0,
};

/* Issue 8b (2026-04-24): Forge session IMC. Priority 6 -- active for
 * the entire forge session (NORMAL + FREEFLY). Hosts the FORGE_TOGGLE
 * binding (gamepad Back) so the toggle wins over ACTION_SCORECARD in
 * the gameplay IMC, both when entering FREEFLY from NORMAL playtest
 * and when leaving FREEFLY back to NORMAL. AUDIT-24-H2 closure.
 * Activated by forgeTick on the first tick of a live session;
 * deactivated by forgeTransitionToInactive. */
InputMappingContext g_ImcForgeSession = {
    .name     = "forge_session",
    .priority = 6,
    .active   = 0,
};

/* Issue 8b (2026-04-24): Forge editor IMC. Priority 7 sits above
 * gameplay + vehicle + forge_session so sidebar / tab / camera-axis
 * bindings shadow their gameplay counterparts (X vs USE, LB/RB vs
 * WEAPON_PREV/NEXT, DPad vs DPad cardinal, LT/RT vs FIRE_*, LSHIFT/
 * LCTRL vs SPRINT/CROUCH) while FREEFLY is active. Priority stays
 * below menu (10) so the pause menu still wins if it opens over a
 * forge session. Activated in forgeTransitionToFreefly, deactivated
 * on exit. */
InputMappingContext g_ImcForge = {
    .name     = "forge",
    .priority = 7,
    .active   = 0,
};

InputMappingContext g_ImcObserver = {
    .name     = "observer",
    .priority = 8,
    .active   = 0,
};

InputMappingContext g_ImcMenu = {
    .name     = "menu",
    .priority = 10,
    .active   = 0,
};

InputMappingContext g_ImcPauseMenu = {
    .name     = "pause_menu",
    .priority = 11,
    .active   = 0,
};

InputMappingContext g_ImcDebugOverlay = {
    .name     = "debug_overlay",
    .priority = 20,
    .active   = 0,
};

InputMappingContext g_ImcTextInput = {
    .name     = "text_input",
    .priority = 30,
    .active   = 0,
};

/* ============================================================
 * Default bindings setup
 * ============================================================ */

/** Add a single binding to an IMC. One VK per call — call twice for dual binds. */
static void addBind(InputMappingContext *imc, InputAction action, u32 vk)
{
    if (vk) actionmapBind(imc, playerForVk(vk), action, -1, vk);
}

static void setupGameplayDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcGameplay;
    s32 p = player;

    /* Keyboard/mouse defaults for player 0 */
    if (p == 0) {
        /* Left stick directions are bound to movement for display in the
         * rebind UI (so Forward shows "LSTICK_UP" etc.). Smooth analog
         * movement from SDL_GameControllerGetAxis still takes priority —
         * the WASD synthesis block skips when the analog path already
         * provided axis values (see actionmapPollFrame). */
        addBind(imc, ACTION_MOVE_FORWARD,   VKL_W);
        addBind(imc, ACTION_MOVE_FORWARD,   JOY_BTN(0, JOFS_LSTICK_UP));
        addBind(imc, ACTION_MOVE_BACKWARD,  VKL_S);
        addBind(imc, ACTION_MOVE_BACKWARD,  JOY_BTN(0, JOFS_LSTICK_DOWN));
        addBind(imc, ACTION_MOVE_LEFT,      VK_A);
        addBind(imc, ACTION_MOVE_LEFT,      JOY_BTN(0, JOFS_LSTICK_LEFT));
        addBind(imc, ACTION_MOVE_RIGHT,     VKL_D);
        addBind(imc, ACTION_MOVE_RIGHT,     JOY_BTN(0, JOFS_LSTICK_RIGHT));
        addBind(imc, ACTION_FIRE_PRIMARY,   VK_MOUSE_LEFT);
        addBind(imc, ACTION_FIRE_PRIMARY,   JOY_BTN(0, JOFS_RTRIG));
        addBind(imc, ACTION_FIRE_SECONDARY, VK_MOUSE_RIGHT);
        addBind(imc, ACTION_FIRE_SECONDARY, JOY_BTN(0, JOFS_LTRIG));
        addBind(imc, ACTION_FIRE_MODE,      VKL_C);            /* alt secondary fire mode — kbd */
        addBind(imc, ACTION_FIRE_MODE,      JOY_BTN(0, JBTN_DPAD_RIGHT)); /* D-pad right — Xbox default */
        addBind(imc, ACTION_RELOAD,         VKL_R);
        /* X (gamepad) and F (kbd) -> ACTION_USE; bondmove.c hold/tap (300 ms) + interact fallback. */
        addBind(imc, ACTION_USE,            VKL_F);
        addBind(imc, ACTION_USE,            JOY_BTN(0, JBTN_X));
        addBind(imc, ACTION_WEAPON_NEXT,    JOY_BTN(0, JBTN_Y)); /* next weapon */
        addBind(imc, ACTION_CANCEL_USE,     VK_MOUSE_MIDDLE);
        addBind(imc, ACTION_CANCEL_USE,     JOY_BTN(0, JBTN_RSTICK)); /* R3 — cancel/drop/FarSight (B is crouch) */
        addBind(imc, ACTION_CROUCH,         VK_LCTRL);
        addBind(imc, ACTION_CROUCH,         JOY_BTN(0, JBTN_B)); /* crouch — Xbox B */
        addBind(imc, ACTION_JUMP,           VK_SPACE);
        addBind(imc, ACTION_JUMP,           JOY_BTN(0, JBTN_A)); /* A_BUTTON / jump */
        addBind(imc, ACTION_SPRINT,         VK_LSHIFT);
        addBind(imc, ACTION_SPRINT,         JOY_BTN(0, JBTN_LSTICK)); /* Left stick click */
        /* ACTION_ZOOM_IN / ZOOM_OUT: no default kbd bind — user rebinds if needed */
        addBind(imc, ACTION_WEAPON_PREV,    VK_MOUSE_WHEEL_UP);
        addBind(imc, ACTION_WEAPON_PREV,    JOY_BTN(0, JBTN_LB));
        addBind(imc, ACTION_WEAPON_NEXT,    VK_MOUSE_WHEEL_DN);
        addBind(imc, ACTION_WEAPON_NEXT,    JOY_BTN(0, JBTN_RB));
        addBind(imc, ACTION_WEAPON_1,       (u32)VK_1);
        addBind(imc, ACTION_WEAPON_2,       VKL_2);
        addBind(imc, ACTION_WEAPON_3,       VKL_3);
        addBind(imc, ACTION_WEAPON_4,       VKL_4);
        addBind(imc, ACTION_WEAPON_5,       VKL_5);
        addBind(imc, ACTION_WEAPON_6,       VKL_6);
        /* Look: right stick (+ swap sticks / invert Y in Controls). */
        addBind(imc, ACTION_AIM_UP,         VKL_UP);
        addBind(imc, ACTION_AIM_UP,         JOY_BTN(0, JOFS_RSTICK_UP));
        addBind(imc, ACTION_AIM_DOWN,       VKL_DOWN);
        addBind(imc, ACTION_AIM_DOWN,       JOY_BTN(0, JOFS_RSTICK_DOWN));
        addBind(imc, ACTION_AIM_LEFT,       VKL_LEFT);
        addBind(imc, ACTION_AIM_LEFT,       JOY_BTN(0, JOFS_RSTICK_LEFT));
        addBind(imc, ACTION_AIM_RIGHT,      VKL_RIGHT);
        addBind(imc, ACTION_AIM_RIGHT,      JOY_BTN(0, JOFS_RSTICK_RIGHT));
        /* N64 C-button bits are optional; default Xbox layout uses LS/RS axes only. */
        /* D-pad: physical LEFT -> ACTION_DPAD_DOWN (D_JPAD) for radial hold-open. */
        addBind(imc, ACTION_DPAD_DOWN,      JOY_BTN(0, JBTN_DPAD_LEFT));
        addBind(imc, ACTION_DPAD_UP,        JOY_BTN(0, JBTN_DPAD_UP));
        addBind(imc, ACTION_PAUSE,          VK_ESCAPE);
        addBind(imc, ACTION_PAUSE,          JOY_BTN(0, JBTN_START));
        addBind(imc, ACTION_SCREENSHOT,     VKL_F5);
        addBind(imc, ACTION_CONSOLE_TOGGLE, VK_GRAVE);
        addBind(imc, ACTION_DEBUG_TOGGLE,   (u32)VK_F9);
        addBind(imc, ACTION_SCORECARD,      43); /* 43 = SDL_SCANCODE_TAB */
        addBind(imc, ACTION_SCORECARD,      JOY_BTN(0, JBTN_BACK));
        addBind(imc, ACTION_VOICE_PTT,      VKL_V);

        /* The Grid mode toggle (Forge <-> Playtest), keyboard binding.
         * Moved from F7 to F11 (2026-04-26) to split the dual-bind with
         * the raw F7 -> playerToggleDevInvincibility() handler in
         * pdgui_backend.cpp. F7 was previously bound here AND to the
         * cheat handler; the in-handler forgeIsFreefly() check in the
         * cheat path was the only thing keeping the two from racing.
         * F11 is otherwise unbound in the codebase (Alt+Enter handles
         * fullscreen, not F11). Keyboard E / Q (FORGE_ASCEND / DESCEND)
         * are also unique-bound on this IMC and remain here -- they
         * only have an effect when forgeReadFreeflyInput reads them in
         * FREEFLY.
         *
         * AUDIT-24-H2 / H3 fix (2026-04-24):
         *   - Gamepad Back -> FORGE_TOGGLE moved to g_ImcForgeSession.
         *     Old dual-bind here lost the single-winner-per-IMC race
         *     to ACTION_SCORECARD (lower enum) and never fired.
         *   - Gamepad LT / RT -> FORGE_DESCEND / ASCEND moved to
         *     g_ImcForge. Old binds here lost to FIRE_PRIMARY /
         *     FIRE_SECONDARY (lower enum) and never fired.
         *   - LSHIFT / LCTRL -> FORGE_BOOST / PRECISION moved to
         *     g_ImcForge. Old binds here lost to SPRINT / CROUCH
         *     (lower enum) and never fired even on keyboard. */
        addBind(imc, ACTION_FORGE_TOGGLE,    (u32)VKL_F11);
        addBind(imc, ACTION_FORGE_ASCEND,    VKL_E);
        addBind(imc, ACTION_FORGE_DESCEND,   VKL_Q);

        /* S483: Test Scenarios swarm benchmark cycler (PD_DEV_BUILD only).
         * KEY_0 (SDL scan 39) and gamepad D-pad-down. Action only does
         * anything when a swarm test scenario is active; the gameplay-side
         * read is gated on g_TestScenario state in swarm_test.c so the
         * binding is harmless during normal play. D-pad-down also drives
         * ACTION_FORGE_SIDEBAR_DOWN; that action is consumed only inside
         * forge freefly + sidebar visible, so the two coexist. */
        addBind(imc, ACTION_TESTSCEN_CYCLE_COUNT, 39);                       /* KEY_0 (SDL scan) -- legacy, equiv to PgUp */
        addBind(imc, ACTION_TESTSCEN_CYCLE_COUNT, JOY_BTN(0, JBTN_DPAD_DOWN));
        /* S594h-A2 (2026-05-01): bidirectional cycler. PgUp / PgDn for
         * keyboard, DPAD_UP for the reverse direction on gamepad (the
         * existing DPAD_DOWN keeps the forward-direction binding). The
         * SDL scan codes for PgUp/PgDn are 75 / 78 respectively. */
        addBind(imc, ACTION_TESTSCEN_CYCLE_COUNT, 75);                       /* KEY_PAGEUP */
        addBind(imc, ACTION_TESTSCEN_CYCLE_PREV,  78);                       /* KEY_PAGEDOWN */
        addBind(imc, ACTION_TESTSCEN_CYCLE_PREV,  JOY_BTN(0, JBTN_DPAD_UP));
        /* Visibility-mode toggle. Cycles Normal -> AlwaysSee -> Invisible
         * each press. Bound to V (SDL scan 25) on keyboard. */
        addBind(imc, ACTION_TESTSCEN_VIS_TOGGLE, 25);                        /* KEY_V */
    }
    /* Players 1-3: no default gamepad binds. MP slots start unbound.
     * The rebind UI is functional for all players — user configures manually. */
}

/* Priority J (2026-04-25): Mission scheme defaults. No unique bindings
 * today -- the Mission IMC exists as a scene-scope identity above the
 * always-active gameplay baseline. Reserved for future Mission-only
 * actions (e.g. an in-mission objective re-prompt key). */
static void setupMissionDefaults(s32 player)
{
    (void)player;
    /* Intentionally empty. */
}

/* Priority J (2026-04-25): Combat Sim scheme defaults.
 *
 * Hosts ACTION_SCORECARD_HOLD on gamepad Back so CS scoreboard surfaces
 * via a held Back press (read with actionHeldForMs threshold, default
 * 400 ms). Tab on keyboard stays in g_ImcGameplay -> ACTION_SCORECARD
 * for the transient peek across both schemes. Mission scheme leaves Back
 * unbound entirely -- single-winner-per-IMC dispatch + priority 1 means
 * CS's Back binding wins over the legacy gameplay Back -> SCORECARD only
 * while CS is active. */
static void setupCombatSimDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcCombatSim;
    if (player != 0) return; /* Player 0 only -- single-seat port. */

    addBind(imc, ACTION_SCORECARD_HOLD, JOY_BTN(0, JBTN_BACK));
}

/* Cohort 4 (2026-04-27, K.2): Cutscene IMC default bindings. Bound
 * only on g_ImcCutscene; the IMC is activated/deactivated on
 * LAYER_CUTSCENE push/pop. SKIP fires via actionPressed (K.6 edge),
 * so a held-since-menu state cannot register as a skip. */
static void setupCutsceneDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcCutscene;
    if (player != 0) return; /* Player 0 only -- no local MP */
    addBind(imc, ACTION_SKIP_CUTSCENE, VK_SPACE);            /* keyboard */
    addBind(imc, ACTION_SKIP_CUTSCENE, JOY_BTN(0, JBTN_A));  /* gamepad  */
    addBind(imc, ACTION_PAUSE,         VK_ESCAPE);           /* allow Escape during cutscene */
    addBind(imc, ACTION_PAUSE,         JOY_BTN(0, JBTN_START));
    addBind(imc, ACTION_VOICE_PTT,     VKL_V);
}

static void setupVehicleDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcVehicle;
    s32 p = player;

    if (p != 0) return; /* Player 0 only — no local MP */
    addBind(imc, ACTION_VEHICLE_ACCELERATE,  VKL_W);
    addBind(imc, ACTION_VEHICLE_ACCELERATE,  JOY_BTN(0, JOFS_RTRIG));
    addBind(imc, ACTION_VEHICLE_BRAKE,       VKL_S);
    addBind(imc, ACTION_VEHICLE_BRAKE,       JOY_BTN(0, JOFS_LTRIG));
    addBind(imc, ACTION_VEHICLE_STEER_LEFT,  VK_A);
    addBind(imc, ACTION_VEHICLE_STEER_LEFT,  JOY_BTN(0, JOFS_LSTICK_LEFT));
    addBind(imc, ACTION_VEHICLE_STEER_RIGHT, VKL_D);
    addBind(imc, ACTION_VEHICLE_STEER_RIGHT, JOY_BTN(0, JOFS_LSTICK_RIGHT));
    addBind(imc, ACTION_VEHICLE_EXIT,        VKL_F);
    addBind(imc, ACTION_VEHICLE_EXIT,        JOY_BTN(0, JBTN_X)); /* align with on-foot USE / exit */
    addBind(imc, ACTION_PAUSE,               VK_ESCAPE);
    addBind(imc, ACTION_PAUSE,               JOY_BTN(0, JBTN_START));
    addBind(imc, ACTION_VOICE_PTT,           VKL_V);
}

/* Issue 8b (2026-04-24): Forge session-scoped IMC default bindings.
 *
 * Lives on g_ImcForgeSession (priority 6). Active for the entire forge
 * session -- both NORMAL (Playtest) and FREEFLY -- so the toggle works
 * in both directions: Back during Playtest enters FREEFLY, Back during
 * FREEFLY returns to Playtest. Whole-session scope is required because
 * NORMAL is a regular gameplay state (player has weapons, can interact)
 * but Back must still reach FORGE_TOGGLE rather than ACTION_SCORECARD.
 *
 * Hosts ONLY the toggle binding -- anything that affects the freefly
 * camera (axes / triggers / boost / precision) or the editor surface
 * (sidebar / tabs) lives on the FREEFLY-only g_ImcForge instead, so
 * gameplay actions on those same buttons keep working in NORMAL state.
 *
 * AUDIT-24-H2 closure. */
static void setupForgeSessionDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcForgeSession;
    if (player != 0) return;

    addBind(imc, ACTION_FORGE_TOGGLE, JOY_BTN(0, JBTN_BACK));
    addBind(imc, ACTION_VOICE_PTT,    VKL_V);
    addBind(imc, ACTION_FORGE_BOT_ADD,           VKL_INSERT);
    addBind(imc, ACTION_FORGE_BOT_REMOVE_ALL,    VK_DELETE);
    addBind(imc, ACTION_FORGE_BOT_FREEZE_TOGGLE, VKL_END);
    addBind(imc, ACTION_FORGE_BOT_SPAWN_CYCLE,   VKL_HOME);
}

/* Issue 8b (2026-04-24): Forge editor (FREEFLY) IMC default bindings.
 *
 * Lives on g_ImcForge (priority 7). Activated by forgemode when a forge
 * session enters FREEFLY, deactivated on exit. Binding here means each
 * VK shadows its gameplay counterpart only while FREEFLY is on:
 *
 *   - X      shadows ACTION_USE             (sidebar toggle, Issue 8b v1)
 *   - LB/RB  shadow  ACTION_WEAPON_PREV/NEXT (tab cycle, Issue 8b v1)
 *   - D-pad  shadow  ACTION_FIRE_MODE etc.  (sidebar nav, Issue 8b v1)
 *   - LT/RT  shadow  ACTION_FIRE_SECONDARY/PRIMARY  (descend/ascend,
 *                                                    AUDIT-24-H3)
 *   - LSHIFT shadows ACTION_SPRINT          (boost, AUDIT-24-H3 follow-up)
 *   - LCTRL  shadows ACTION_CROUCH          (precision, same)
 *
 * Outside FREEFLY (NORMAL playtest, gameplay) g_ImcForge is inactive
 * and every one of these VKs falls through to its gameplay binding.
 *
 * Keyboard defaults for sidebar / tabs land here too; Tab toggles
 * sidebar, PageUp / PageDown cycle tabs. Ctrl+Tab / Ctrl+Shift+Tab
 * chord lives in the editor render loop because the actionmap stores
 * single-VK bindings only. */
static void setupForgeDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcForge;
    if (player != 0) return; /* Player 0 only -- single-seat forge authoring. */

    /* ---- Camera axis / freefly modifiers (gamepad) ---- */
    addBind(imc, ACTION_FORGE_ASCEND,           JOY_BTN(0, JOFS_RTRIG));
    addBind(imc, ACTION_FORGE_DESCEND,          JOY_BTN(0, JOFS_LTRIG));

    /* ---- Camera modifiers (keyboard, also FREEFLY-only) ---- */
    addBind(imc, ACTION_FORGE_BOOST,            VK_LSHIFT);
    addBind(imc, ACTION_FORGE_PRECISION,        VK_LCTRL);

    /* ---- Editor sidebar + tabs (gamepad) ---- */
    addBind(imc, ACTION_FORGE_SIDEBAR_TOGGLE,   JOY_BTN(0, JBTN_X));
    addBind(imc, ACTION_FORGE_TAB_PREV,         JOY_BTN(0, JBTN_LB));
    addBind(imc, ACTION_FORGE_TAB_NEXT,         JOY_BTN(0, JBTN_RB));
    addBind(imc, ACTION_FORGE_SIDEBAR_UP,       JOY_BTN(0, JBTN_DPAD_UP));
    addBind(imc, ACTION_FORGE_SIDEBAR_DOWN,     JOY_BTN(0, JBTN_DPAD_DOWN));
    addBind(imc, ACTION_FORGE_SIDEBAR_ACTIVATE, JOY_BTN(0, JBTN_DPAD_RIGHT));
    /* Fix 8 (2026-05-01, Mike playtest): A on the gamepad shadows
     * gameplay's JUMP while g_ImcForge (priority 7) is active in FREEFLY,
     * so authors can release a held object with the same A button used
     * to confirm everywhere else in the editor. The select-spawn-attach
     * path lives on the catalog list's mouse-click handler; the release
     * half of the lifecycle is what this binding drives. D-pad RIGHT
     * stays bound for keyboard-arrow + dpad parity. */
    addBind(imc, ACTION_FORGE_SIDEBAR_ACTIVATE, JOY_BTN(0, JBTN_A));

    /* ---- Editor sidebar + tabs (keyboard) ---- */
    /* Scancodes follow the SDL_SCANCODE_* enum mirrored in s_VkNames
     * above: TAB=43, PAGEUP=75, PAGEDOWN=78, RIGHT=79, DOWN=81, UP=82. */
    addBind(imc, ACTION_FORGE_SIDEBAR_TOGGLE,   43);  /* TAB */
    addBind(imc, ACTION_FORGE_TAB_PREV,         75);  /* PAGEUP */
    addBind(imc, ACTION_FORGE_TAB_PREV,         VK_CHORD_CTRL_SHIFT_TAB);
    addBind(imc, ACTION_FORGE_TAB_NEXT,         78);  /* PAGEDOWN */
    addBind(imc, ACTION_FORGE_TAB_NEXT,         VK_CHORD_CTRL_TAB);
    addBind(imc, ACTION_FORGE_SIDEBAR_UP,       82);  /* UP */
    addBind(imc, ACTION_FORGE_SIDEBAR_DOWN,     81);  /* DOWN */
    addBind(imc, ACTION_FORGE_SIDEBAR_ACTIVATE, 79);  /* RIGHT */
    addBind(imc, ACTION_FORGE_PLACE_CANCEL,     VK_ESCAPE);
}

static void setupObserverDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcObserver;
    if (player != 0) return;

    addBind(imc, ACTION_OBSERVER_SUBSET_PREV,   VKL_PAGEUP);
    addBind(imc, ACTION_OBSERVER_SUBSET_PREV,   JOY_BTN(0, JBTN_LB));
    addBind(imc, ACTION_OBSERVER_SUBSET_NEXT,   VKL_PAGEDOWN);
    addBind(imc, ACTION_OBSERVER_SUBSET_NEXT,   JOY_BTN(0, JBTN_RB));
    addBind(imc, ACTION_OBSERVER_MEMBER_PREV,   VKL_LEFT);
    addBind(imc, ACTION_OBSERVER_MEMBER_PREV,   JOY_BTN(0, JBTN_DPAD_LEFT));
    addBind(imc, ACTION_OBSERVER_MEMBER_NEXT,   VKL_RIGHT);
    addBind(imc, ACTION_OBSERVER_MEMBER_NEXT,   JOY_BTN(0, JBTN_DPAD_RIGHT));
    addBind(imc, ACTION_OBSERVER_CAMERA_TOGGLE, 43); /* TAB */
    addBind(imc, ACTION_OBSERVER_CAMERA_TOGGLE, JOY_BTN(0, JBTN_RSTICK));
    addBind(imc, ACTION_OBSERVER_FREEFLY,       VKL_R);
    addBind(imc, ACTION_OBSERVER_FREEFLY,       JOY_BTN(0, JBTN_Y));
    addBind(imc, ACTION_OBSERVER_STOP,          VK_ESCAPE);
    addBind(imc, ACTION_OBSERVER_STOP,          JOY_BTN(0, JBTN_B));
    addBind(imc, ACTION_OBSERVER_ASCEND,        VKL_E);
    addBind(imc, ACTION_OBSERVER_ASCEND,        JOY_BTN(0, JOFS_RTRIG));
    addBind(imc, ACTION_OBSERVER_DESCEND,       VKL_Q);
    addBind(imc, ACTION_OBSERVER_DESCEND,       JOY_BTN(0, JOFS_LTRIG));
    addBind(imc, ACTION_VOICE_PTT,              VKL_V);
}

static void setupMenuDefaults(void)
{
    /* Menu IMC: Player 0 only.
     *
     * S197a NAV FIX: MENU_UP/DOWN/LEFT/RIGHT + TAB_PREV/NEXT MUST be bound here.
     * pdguiDriveImGuiNav() in pdgui_backend.cpp reads actionHeld(ACTION_MENU_*)
     * every frame and translates to io.AddKeyEvent(ImGuiKey_UpArrow/...) for
     * ImGui's keyboard nav. ImGui NavEnableGamepad is OFF, so this is the only
     * path for d-pad menu navigation. A prior session removed these as "dead
     * weight" on the mistaken assumption that ImGui handled nav itself — it
     * doesn't, and the result was that arrows/d-pad stopped working in menus.
     */
    InputMappingContext *imc = &g_ImcMenu;
    addBind(imc, ACTION_USE,          VK_RETURN);                 /* UI Select/Accept - kbd */
    addBind(imc, ACTION_USE,          VK_SPACE);                  /* UI Select/Accept - kbd */
    addBind(imc, ACTION_USE,          VKL_KP_ENTER);              /* UI Select/Accept - keypad */
    addBind(imc, ACTION_USE,          JOY_BTN(0, JBTN_A));        /* UI Select/Accept - gamepad */
    addBind(imc, ACTION_CANCEL_USE,   VK_ESCAPE);                 /* Back/Cancel - kbd */
    addBind(imc, ACTION_CANCEL_USE,   JOY_BTN(0, JBTN_B));        /* Back/Cancel - gamepad */
    addBind(imc, ACTION_PAUSE,        JOY_BTN(0, JBTN_START));    /* Pause toggle - gamepad only (kbd Escape covered by CANCEL_USE) */
    addBind(imc, ACTION_MENU_UP,      VKL_UP);                    /* Nav up - kbd arrow */
    addBind(imc, ACTION_MENU_UP,      JOY_BTN(0, JBTN_DPAD_UP));  /* Nav up - d-pad */
    addBind(imc, ACTION_MENU_DOWN,    VKL_DOWN);                  /* Nav down - kbd arrow */
    addBind(imc, ACTION_MENU_DOWN,    JOY_BTN(0, JBTN_DPAD_DOWN));/* Nav down - d-pad */
    addBind(imc, ACTION_MENU_LEFT,    VKL_LEFT);                  /* Nav left - kbd arrow */
    addBind(imc, ACTION_MENU_LEFT,    JOY_BTN(0, JBTN_DPAD_LEFT));/* Nav left - d-pad */
    addBind(imc, ACTION_MENU_RIGHT,   VKL_RIGHT);                 /* Nav right - kbd arrow */
    addBind(imc, ACTION_MENU_RIGHT,   JOY_BTN(0, JBTN_DPAD_RIGHT));/* Nav right - d-pad */
    addBind(imc, ACTION_MENU_TAB_PREV,VKL_PAGEUP);                /* Previous tab - kbd */
    addBind(imc, ACTION_MENU_TAB_PREV,VKL_Q);                     /* Previous tab - kbd */
    addBind(imc, ACTION_MENU_TAB_PREV,JOY_BTN(0, JBTN_LB));       /* Previous tab - LB */
    addBind(imc, ACTION_MENU_TAB_NEXT,VKL_PAGEDOWN);              /* Next tab - kbd */
    addBind(imc, ACTION_MENU_TAB_NEXT,VKL_E);                     /* Next tab - kbd */
    addBind(imc, ACTION_MENU_TAB_NEXT,JOY_BTN(0, JBTN_RB));       /* Next tab - RB */
    addBind(imc, ACTION_MENU_SECONDARY,VKL_C);                    /* Secondary command - kbd */
    addBind(imc, ACTION_MENU_SECONDARY,JOY_BTN(0, JBTN_X));       /* Secondary command - X */
    addBind(imc, ACTION_MENU_TERTIARY,VKL_D);                     /* Tertiary command - kbd */
    addBind(imc, ACTION_MENU_TERTIARY,JOY_BTN(0, JBTN_Y));        /* Tertiary command - Y */
    addBind(imc, ACTION_MENU_DELETE,VK_DELETE);                   /* Delete command - kbd */
    /* S483b (2026-04-27): Tab toggles the Online connectivity sidebar.
     * Bound on menu IMCs only (here + setupPauseMenuDefaults) so pressing
     * Tab during pure gameplay (only g_ImcGameplay active) cannot fire
     * ACTION_SOCIAL_TOGGLE -- fireVk's first-match-wins walk simply has
     * no binding for SDL_SCANCODE_TAB at gameplay priority. The legacy
     * ImGui::IsKeyPressed(Tab) hotkey at pdgui_friends.cpp:813 was the
     * IMC-bypass that let Tab open the sidebar mid-mission; routed
     * through actionPressed(0, ACTION_SOCIAL_TOGGLE) instead. */
    addBind(imc, ACTION_SOCIAL_TOGGLE, 43);                       /* TAB scancode */
    addBind(imc, ACTION_VOICE_PTT,     VKL_V);                    /* Voice push-to-talk */
    addBind(imc, ACTION_SKIN_BRUSH_DECREASE,  VKL_LEFTBRACKET);
    addBind(imc, ACTION_SKIN_BRUSH_INCREASE,  VKL_RIGHTBRACKET);
    addBind(imc, ACTION_SKIN_TOOL_DRAW,       VKL_1);
    addBind(imc, ACTION_SKIN_TOOL_ERASE,      VKL_2);
    addBind(imc, ACTION_SKIN_TOOL_FILL,       VKL_3);
    addBind(imc, ACTION_SKIN_TOOL_EYEDROPPER, VKL_4);
    addBind(imc, ACTION_SKIN_TOOL_LINE,       VKL_5);
    addBind(imc, ACTION_SKIN_GRID_TOGGLE,     VKL_G);
    addBind(imc, ACTION_SKIN_UV_TOGGLE,       VKL_U);
    addBind(imc, ACTION_SKIN_UNDO,            VK_CHORD_CTRL_Z);
    addBind(imc, ACTION_SKIN_REDO,            VK_CHORD_CTRL_Y);
    addBind(imc, ACTION_SKIN_REDO,            VK_CHORD_CTRL_SHIFT_Z);
    addBind(imc, ACTION_SKIN_SAVE,            VK_CHORD_CTRL_S);
}

static void setupPauseMenuDefaults(void)
{
    /* PauseMenu IMC: same actions as Menu plus the S197a nav fix. Player 0 only.
     * ACTION_PAUSE is NOT bound to VK_ESCAPE here — Escape means "go back"
     * (ACTION_CANCEL_USE) in a pause menu, not a second pause-toggle. */
    InputMappingContext *imc = &g_ImcPauseMenu;
    addBind(imc, ACTION_USE,          VK_RETURN);                 /* UI Select/Accept - kbd */
    addBind(imc, ACTION_USE,          VK_SPACE);                  /* UI Select/Accept - kbd */
    addBind(imc, ACTION_USE,          VKL_KP_ENTER);              /* UI Select/Accept - keypad */
    addBind(imc, ACTION_USE,          JOY_BTN(0, JBTN_A));        /* UI Select/Accept - gamepad */
    addBind(imc, ACTION_CANCEL_USE,   VK_ESCAPE);                 /* Back/Cancel - kbd */
    addBind(imc, ACTION_CANCEL_USE,   JOY_BTN(0, JBTN_B));        /* Back/Cancel - gamepad */
    addBind(imc, ACTION_PAUSE,        JOY_BTN(0, JBTN_START));    /* Pause toggle - gamepad only */
    addBind(imc, ACTION_MENU_UP,      VKL_UP);                    /* Nav up - kbd arrow */
    addBind(imc, ACTION_MENU_UP,      JOY_BTN(0, JBTN_DPAD_UP));  /* Nav up - d-pad */
    addBind(imc, ACTION_MENU_DOWN,    VKL_DOWN);                  /* Nav down - kbd arrow */
    addBind(imc, ACTION_MENU_DOWN,    JOY_BTN(0, JBTN_DPAD_DOWN));/* Nav down - d-pad */
    addBind(imc, ACTION_MENU_LEFT,    VKL_LEFT);                  /* Nav left - kbd arrow */
    addBind(imc, ACTION_MENU_LEFT,    JOY_BTN(0, JBTN_DPAD_LEFT));/* Nav left - d-pad */
    addBind(imc, ACTION_MENU_RIGHT,   VKL_RIGHT);                 /* Nav right - kbd arrow */
    addBind(imc, ACTION_MENU_RIGHT,   JOY_BTN(0, JBTN_DPAD_RIGHT));/* Nav right - d-pad */
    addBind(imc, ACTION_MENU_TAB_PREV,VKL_PAGEUP);                /* Previous tab - kbd */
    addBind(imc, ACTION_MENU_TAB_PREV,VKL_Q);                     /* Previous tab - kbd */
    addBind(imc, ACTION_MENU_TAB_PREV,JOY_BTN(0, JBTN_LB));       /* Previous tab - LB */
    addBind(imc, ACTION_MENU_TAB_NEXT,VKL_PAGEDOWN);              /* Next tab - kbd */
    addBind(imc, ACTION_MENU_TAB_NEXT,VKL_E);                     /* Next tab - kbd */
    addBind(imc, ACTION_MENU_TAB_NEXT,JOY_BTN(0, JBTN_RB));       /* Next tab - RB */
    addBind(imc, ACTION_MENU_SECONDARY,VKL_C);                    /* Secondary command - kbd */
    addBind(imc, ACTION_MENU_SECONDARY,JOY_BTN(0, JBTN_X));       /* Secondary command - X */
    addBind(imc, ACTION_MENU_TERTIARY,VKL_D);                     /* Tertiary command - kbd */
    addBind(imc, ACTION_MENU_TERTIARY,JOY_BTN(0, JBTN_Y));        /* Tertiary command - Y */
    addBind(imc, ACTION_MENU_DELETE,VK_DELETE);                   /* Delete command - kbd */
    /* S483b (2026-04-27): Tab toggles the Online connectivity sidebar
     * while paused. See setupMenuDefaults for the design rationale. */
    addBind(imc, ACTION_SOCIAL_TOGGLE, 43);                       /* TAB scancode */
    addBind(imc, ACTION_VOICE_PTT,     VKL_V);                    /* Voice push-to-talk */
}

static void setupDebugOverlayDefaults(void)
{
    /* Player 0 only. MENU_UP/DOWN/LEFT/RIGHT removed — ImGui handles nav. */
    InputMappingContext *imc = &g_ImcDebugOverlay;
    addBind(imc, ACTION_DEBUG_TOGGLE,   (u32)VK_F9);
    addBind(imc, ACTION_USE,            VK_RETURN); /* accept in debug panels */
    addBind(imc, ACTION_USE,            VK_SPACE);  /* accept in debug panels */
    addBind(imc, ACTION_USE,            VKL_KP_ENTER);
    addBind(imc, ACTION_CANCEL_USE,     VK_ESCAPE); /* close/cancel */
    addBind(imc, ACTION_CONSOLE_TOGGLE, VK_GRAVE);
    addBind(imc, ACTION_SCREENSHOT,     VKL_F5);
    addBind(imc, ACTION_VOICE_PTT,      VKL_V);
}

static void setupTextInputDefaults(void)
{
    InputMappingContext *imc = &g_ImcTextInput;

    /* Text input context: Player 0 only. Most key events go directly to SDL
     * text input mode, not through actionmap. One default per action. */
    addBind(imc, ACTION_USE,         VK_RETURN);           /* confirm/accept — kbd */
    addBind(imc, ACTION_USE,         JOY_BTN(0, JBTN_A));  /* confirm/accept — gamepad */
    addBind(imc, ACTION_CANCEL_USE,  VK_ESCAPE);           /* cancel — kbd */
    addBind(imc, ACTION_CANCEL_USE,  JOY_BTN(0, JBTN_B));  /* cancel — gamepad */
    addBind(imc, ACTION_CHEAT_ENTER, VK_RETURN);
    addBind(imc, ACTION_TEXT_PASTE,  VK_MOUSE_RIGHT);      /* RMB paste-from-clipboard (Cohort 1) */
}

/** Build default bind strings from the freshly populated IMC mappings. */
static void initDefaultBindStrings(void)
{
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            buildBindStr(p, (InputAction)a,
                         s_BindStr[p][a], BIND_STR_MAX);
        }
    }
}

/* ============================================================
 * Public: actionmapSetDefaults
 * ============================================================ */

void actionmapSetDefaults(InputMappingContext *imc, s32 player)
{
    if (!imc) return;

    /* Clear all triggers owned by this player in the IMC */
    for (s32 a = 0; a < ACTION_COUNT; a++) {
        if (!imc->has_mapping[a]) continue;
        InputMapping *m = &imc->mappings[a];
        s32 write = 0;
        for (s32 ti = 0; ti < m->num_triggers; ti++) {
            if (playerForVk(m->triggers[ti].vk) != player) {
                m->triggers[write++] = m->triggers[ti];
            }
        }
        m->num_triggers = write;
    }

    /* Re-apply defaults for this player */
    if (imc == &g_ImcGameplay) {
        setupGameplayDefaults(player);
    } else if (imc == &g_ImcMission) {
        setupMissionDefaults(player);
    } else if (imc == &g_ImcCombatSim) {
        setupCombatSimDefaults(player);
    } else if (imc == &g_ImcVehicle) {
        setupVehicleDefaults(player);
    } else if (imc == &g_ImcCutscene) {
        setupCutsceneDefaults(player);
    } else if (imc == &g_ImcForgeSession) {
        setupForgeSessionDefaults(player);
    } else if (imc == &g_ImcForge) {
        setupForgeDefaults(player);
    } else if (imc == &g_ImcObserver) {
        setupObserverDefaults(player);
    } else if (imc == &g_ImcMenu) {
        setupMenuDefaults();
    } else if (imc == &g_ImcPauseMenu) {
        setupPauseMenuDefaults();
    } else if (imc == &g_ImcDebugOverlay) {
        setupDebugOverlayDefaults();
    } else if (imc == &g_ImcTextInput) {
        setupTextInputDefaults();
    }
}

/* ============================================================
 * Public: actionmapInit
 * ============================================================ */

void actionmapInit(void)
{
    /* Zero all state */
    memset(s_Active,     0, sizeof(s_Active));
    memset(s_State,      0, sizeof(s_State));
    memset(s_StickHeld,  0, sizeof(s_StickHeld));
    memset(s_CheatBuf,   0, sizeof(s_CheatBuf));
    memset(s_BindStr,    0, sizeof(s_BindStr));

    s_NumActive         = 0;
    s_CheatHead         = 0;
    s_CheatCount        = 0;
    s_RawDevice         = ACTIONMAP_DEVICE_KBM;
    s_LastDevice        = ACTIONMAP_DEVICE_KBM;
    s_DeviceChangeTime  = 0;

    /* Zero all IMC mapping slots */
    memset(&g_ImcGameplay,     0, sizeof(g_ImcGameplay));
    memset(&g_ImcMission,      0, sizeof(g_ImcMission));
    memset(&g_ImcCombatSim,    0, sizeof(g_ImcCombatSim));
    memset(&g_ImcCutscene,     0, sizeof(g_ImcCutscene));
    memset(&g_ImcVehicle,      0, sizeof(g_ImcVehicle));
    memset(&g_ImcForgeSession, 0, sizeof(g_ImcForgeSession));
    memset(&g_ImcForge,        0, sizeof(g_ImcForge));
    memset(&g_ImcObserver,     0, sizeof(g_ImcObserver));
    memset(&g_ImcMenu,         0, sizeof(g_ImcMenu));
    memset(&g_ImcPauseMenu,    0, sizeof(g_ImcPauseMenu));
    memset(&g_ImcDebugOverlay, 0, sizeof(g_ImcDebugOverlay));
    memset(&g_ImcTextInput,    0, sizeof(g_ImcTextInput));

    /* Restore names and priorities (memset wiped them) */
    g_ImcGameplay.name     = "gameplay";      g_ImcGameplay.priority     = 0;
    g_ImcMission.name      = "mission";       g_ImcMission.priority      = 1;
    g_ImcCombatSim.name    = "combat_sim";    g_ImcCombatSim.priority    = 1;
    g_ImcCutscene.name     = "cutscene";      g_ImcCutscene.priority     = 4;
    g_ImcVehicle.name      = "vehicle";       g_ImcVehicle.priority      = 5;
    g_ImcForgeSession.name = "forge_session"; g_ImcForgeSession.priority = 6;
    g_ImcForge.name        = "forge";         g_ImcForge.priority        = 7;
    g_ImcObserver.name     = "observer";      g_ImcObserver.priority     = 8;
    g_ImcMenu.name         = "menu";          g_ImcMenu.priority         = 10;
    g_ImcPauseMenu.name    = "pause_menu";    g_ImcPauseMenu.priority    = 11;
    g_ImcDebugOverlay.name = "debug_overlay"; g_ImcDebugOverlay.priority = 20;
    g_ImcTextInput.name    = "text_input";    g_ImcTextInput.priority    = 30;

    /* Populate default bindings — Player 0 only. No local MP in this port. */
    setupGameplayDefaults(0);
    setupMissionDefaults(0);
    setupCombatSimDefaults(0);
    setupCutsceneDefaults(0);
    setupVehicleDefaults(0);
    setupForgeSessionDefaults(0);
    setupForgeDefaults(0);
    setupObserverDefaults(0);
    setupMenuDefaults();
    setupPauseMenuDefaults();
    setupDebugOverlayDefaults();
    setupTextInputDefaults();

    /* Build default bind strings for pd.ini registration */
    initDefaultBindStrings();

    /* Register bind strings with config system.
     * configLoad() will overwrite defaults if the key exists in pd.ini. */
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            char key[80];
            snprintf(key, sizeof(key), "ActionMap.P%d.%s", p, s_ActionNames[a]);
            configRegisterString(key, s_BindStr[p][a], BIND_STR_MAX);
        }
    }

    /* Register stick tuning variables with config system.
     * Sens*Ui: 1-10 (0.5 steps) in UI; internal mult 0.1-3.0 derived via actionmapRefreshStickMultFromUi. */
    configRegisterFloat("ActionMap.SensMoveUi", &s_SensMoveUi, 1.0f, 10.0f);
    configRegisterFloat("ActionMap.SensAimUi", &s_SensAimUi, 1.0f, 10.0f);
    configRegisterFloat("ActionMap.SensAdsUi", &s_SensAdsUi, 1.0f, 10.0f);
    configRegisterFloat("ActionMap.StickDeadzone", &s_StickDzMove, 0.0f, 0.5f);
    configRegisterFloat("ActionMap.StickDeadzoneAim", &s_StickDzAim, 0.0f, 0.5f);
    configRegisterInt("ActionMap.StickInvertY",       &s_StickInvertY,     0, 1);
    configRegisterInt("ActionMap.SwapSticks",          &s_SwapSticks,      0, 1);
    configRegisterInt("ActionMap.UseHoldThresholdMs",  &s_UseHoldThresholdMs, 50,
                        ACTIONMAP_HOLD_MS_MAX);
    configRegisterInt("ActionMap.InteractHoldExtraTerminalMs", &s_InteractHoldExtraTerminalMs, 0,
                      ACTIONMAP_HOLD_MS_MAX);

    for (s32 a = 0; a < ACTION_COUNT; a++) {
        s_ActionHoldMsOverride[a] = -1;
    }
    s_HoldOverridesIniStr[0] = '\0';
    configRegisterString("ActionMap.HoldMsOverrides", s_HoldOverridesIniStr,
                         HOLD_OVERRIDES_STR_MAX);

    /* Activate only the gameplay context by default.
     * Menu IMC is activated/deactivated by the input context push/pop system
     * (g_CtxImGuiMenu, g_CtxPauseMenu, g_CtxDebugOverlay).  Activating it
     * here caused it to shadow gameplay gamepad bindings (same VKs at higher
     * priority) — A/B/LB/RB/D-pad/stick all fired MENU actions instead of
     * gameplay actions during gameplay. */
    imcActivate(&g_ImcGameplay);

    actionmapRefreshStickMultFromUi();

    sysLogPrintf(LOG_NOTE, "ACTIONMAP: initialized — %d actions, %d players, %d IMCs",
                 (s32)ACTION_COUNT, ACTIONMAP_MAX_PLAYERS, s_NumActive);
}
