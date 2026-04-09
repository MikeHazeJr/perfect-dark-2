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
#include <PR/ultratypes.h>

#include "actionmap.h"
#include "config.h"     /* configRegisterString */
#include "system.h"     /* sysLogPrintf, LOG_NOTE, LOG_WARNING */

/* os_thread.h must precede input.h / os_cont.h.
 * os_message.h (included by os_cont.h) uses OSThread without including
 * os_thread.h itself, so we must pre-include it here. */
#include <PR/os_thread.h>
#include "input.h"      /* virtkey enum, inputGetKeyName, inputGetKeyByName,
                           inputMouseGetRawDelta, INPUT_MAX_CONTROLLER_BUTTONS */

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
#define VKL_C     6

/* Arrow keys */
#define VKL_UP    82
#define VKL_DOWN  81
#define VKL_LEFT  80
#define VKL_RIGHT 79

/* F-keys: VK_F1=58 → F5=62, F7=64, F10=67 */
#define VKL_F5    62
#define VKL_F7    64

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

/** Look up VK name for pd.ini serialisation (self-contained, no input.c dependency). */
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
    /* 45-52: menu nav */
    "MenuUp",
    "MenuDown",
    "MenuLeft",
    "MenuRight",
    "MenuAccept",
    "MenuCancel",
    "MenuTabPrev",
    "MenuTabNext",
    /* 53-57: system */
    "Pause",
    "Screenshot",
    "ConsoleToggle",
    "DebugToggle",
    "CheatEnter",
};

/* ============================================================
 * Module state
 * ============================================================ */

/* Active IMC registry, kept sorted by priority descending */
static InputMappingContext *s_Active[ACTIONMAP_MAX_CONTEXTS];
static s32 s_NumActive = 0;

/* Per-player, per-action state */
static ActionState s_State[ACTIONMAP_MAX_PLAYERS][ACTION_COUNT];

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

/* Controller stick tuning — exposed to Controls menu via getter/setter API */
static f32 s_StickSensitivity = 1.0f;   /* multiplier on stick axes (0.1 .. 3.0) */
static f32 s_StickDeadzone    = 0.15f;  /* radial deadzone (0.0 .. 0.5) */
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

    /* Walk contexts from highest priority to lowest */
    for (s32 ci = 0; ci < s_NumActive; ci++) {
        InputMappingContext *ctx = s_Active[ci];

        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (!ctx->has_mapping[a]) {
                continue;
            }
            InputMapping *m = &ctx->mappings[a];
            for (s32 ti = 0; ti < m->num_triggers; ti++) {
                if (m->triggers[ti].vk != vk) {
                    continue;
                }

                ActionState *st = &s_State[player][a];
                if (is_down) {
                    if (!st->held) {
                        st->held    = 1;
                        st->pressed = 1;
                        st->value   = 1.0f;
                        /* Record non-axis actions in cheat buffer */
                        if (a != ACTION_AXIS_MOVE_X && a != ACTION_AXIS_MOVE_Y &&
                            a != ACTION_AXIS_AIM_X  && a != ACTION_AXIS_AIM_Y) {
                            cheatRecord((InputAction)a);
                        }
                    }
                } else {
                    if (st->held) {
                        st->held     = 0;
                        st->released = 1;
                        st->value    = 0.0f;
                    }
                }
                /* First IMC+action match wins — stop searching */
                goto next_player;
            }
        }
    }
next_player:;
}

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
        fireVk((u32)ev->key.keysym.scancode, 1);
        break;

    case SDL_KEYUP:
        if (ev->key.repeat) break;
        updateDevice(ACTIONMAP_DEVICE_KBM);
        fireVk((u32)ev->key.keysym.scancode, 0);
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

/* ============================================================
 * Public: actionmapPollFrame — analog axis sampling
 * ============================================================ */

void actionmapPollFrame(void)
{
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        SDL_GameController *ctrl = SDL_GameControllerFromPlayerIndex(p);

        if (ctrl) {
            s16 lx = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
            s16 ly = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
            s16 rx = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_RIGHTX);
            s16 ry = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_RIGHTY);

            f32 dz = s_StickDeadzone;
            f32 flx = applyDeadzone(lx / 32767.0f, dz) * s_StickSensitivity;
            f32 fly = applyDeadzone(ly / 32767.0f, dz) * s_StickSensitivity;
            f32 frx = applyDeadzone(rx / 32767.0f, dz) * s_StickSensitivity;
            f32 fry = applyDeadzone(ry / 32767.0f, dz) * s_StickSensitivity;

            /* Negate Y: SDL Y+ = down, game expects Y+ = forward/up (N64 convention) */
            fly = -fly;
            fry = -fry;

            /* Optional controller Y-invert for aim axis */
            if (s_StickInvertY) {
                fry = -fry;
            }

            s_State[p][ACTION_AXIS_MOVE_X].value = clampf(flx, -1.0f, 1.0f);
            s_State[p][ACTION_AXIS_MOVE_Y].value = clampf(fly, -1.0f, 1.0f);
            s_State[p][ACTION_AXIS_AIM_X].value  = clampf(frx, -1.0f, 1.0f);
            s_State[p][ACTION_AXIS_AIM_Y].value  = clampf(fry, -1.0f, 1.0f);

            /* Mark as held if axis is significantly deflected (sign-agnostic) */
            s_State[p][ACTION_AXIS_MOVE_X].held = (flx != 0.0f) ? 1 : 0;
            s_State[p][ACTION_AXIS_MOVE_Y].held = (fly != 0.0f) ? 1 : 0;
            s_State[p][ACTION_AXIS_AIM_X].held  = (frx != 0.0f) ? 1 : 0;
            s_State[p][ACTION_AXIS_AIM_Y].held  = (fry != 0.0f) ? 1 : 0;
        }
    }

    /* Player 0: KBM aim axis from mouse delta (when no gamepad or KBM active) */
    if (s_LastDevice == ACTIONMAP_DEVICE_KBM ||
        SDL_GameControllerFromPlayerIndex(0) == NULL)
    {
        s32 mdx = 0, mdy = 0;
        inputMouseGetRawDelta(&mdx, &mdy);

        f32 ax = clampf((f32)mdx * MOUSE_AIM_SCALE, -1.0f, 1.0f);
        f32 ay = clampf(-(f32)mdy * MOUSE_AIM_SCALE, -1.0f, 1.0f);  /* Negate: mouse Y+ = down, game Y+ = up */

        s_State[0][ACTION_AXIS_AIM_X].value = ax;
        s_State[0][ACTION_AXIS_AIM_Y].value = ay;
        s_State[0][ACTION_AXIS_AIM_X].held  = (ax != 0.0f) ? 1 : 0;
        s_State[0][ACTION_AXIS_AIM_Y].held  = (ay != 0.0f) ? 1 : 0;

        /* KBM move axes from WASD digital states */
        f32 mx = 0.0f, my = 0.0f;
        if (s_State[0][ACTION_MOVE_RIGHT].held)    mx += 1.0f;
        if (s_State[0][ACTION_MOVE_LEFT].held)     mx -= 1.0f;
        if (s_State[0][ACTION_MOVE_FORWARD].held)  my += 1.0f;  /* Y+ = forward (N64 convention) */
        if (s_State[0][ACTION_MOVE_BACKWARD].held) my -= 1.0f;
        /* Normalize diagonal */
        f32 len = sqrtf(mx * mx + my * my);
        if (len > 1.0f) { mx /= len; my /= len; }
        s_State[0][ACTION_AXIS_MOVE_X].value = mx;
        s_State[0][ACTION_AXIS_MOVE_Y].value = my;
    }

    /* Mouse wheel actions auto-release after one frame (no SDL_KEYUP equivalent) */
    if (s_State[0][ACTION_WEAPON_NEXT].pressed &&
        s_State[0][ACTION_WEAPON_NEXT].held) {
        /* Will be naturally cleared by endFrame if bound to wheel */
    }
}

/* ============================================================
 * Public: actionmapEndFrame
 * ============================================================ */

void actionmapEndFrame(void)
{
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

            /* Wheel VKs have no corresponding SDL_KEYUP, so auto-release */
            if (a == ACTION_WEAPON_PREV || a == ACTION_WEAPON_NEXT) {
                /* Only auto-release if the binding is a wheel VK */
                /* Check conservatively: if it was held but no digital stick tracks it */
                /* This is handled by the VK: the next dispatch will not re-fire */
                /* For safety, clear held for these momentary actions */
                /* (they re-fire every MOUSEWHEEL event, which is per-scroll-notch) */
            }
        }

        /* Auto-release synthetic wheel VKs: fire a release for wheel up/dn */
        /* They don't have SDL_KEYUP events, so we release them end-of-frame */
        /* Check if the action bound to WHEEL_UP/DN was pressed this frame */
        /* and clear it. We do this by searching for wheel VKs in mappings. */
        for (s32 ci = 0; ci < s_NumActive; ci++) {
            InputMappingContext *ctx = s_Active[ci];
            for (s32 a = 0; a < ACTION_COUNT; a++) {
                if (!ctx->has_mapping[a]) continue;
                InputMapping *m = &ctx->mappings[a];
                for (s32 ti = 0; ti < m->num_triggers; ti++) {
                    u32 vk = m->triggers[ti].vk;
                    if (vk == VK_MOUSE_WHEEL_UP || vk == VK_MOUSE_WHEEL_DN) {
                        /* Auto-release wheel-bound actions */
                        ActionState *st = &s_State[p][a];
                        if (st->held) {
                            st->held     = 0;
                            st->released = 1;
                            st->value    = 0.0f;
                        }
                    }
                }
            }
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
    return s_State[player][action].pressed;
}

s32 actionHeld(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    return s_State[player][action].held;
}

s32 actionReleased(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0;
    if (action < 0 || action >= ACTION_COUNT) return 0;
    return s_State[player][action].released;
}

f32 actionValue(s32 player, InputAction action)
{
    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return 0.0f;
    if (action < 0 || action >= ACTION_COUNT) return 0.0f;
    return s_State[player][action].value;
}

void actionAxis(s32 player, InputAction action, f32 *out_x, f32 *out_y)
{
    if (!out_x || !out_y) return;
    *out_x = 0.0f;
    *out_y = 0.0f;

    if (player < 0 || player >= ACTIONMAP_MAX_PLAYERS) return;
    if (action < 0 || action >= ACTION_COUNT) return;

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
 * Public: last-device detection
 * ============================================================ */

s32 actionmapGetLastDevice(void)
{
    return s_LastDevice;
}

/* ============================================================
 * Bind management helpers
 * ============================================================ */

/** Build a comma-separated bind string from the triggers of action a for player p. */
static void buildBindStr(s32 player, InputAction action,
                         char *out, s32 outlen)
{
    out[0] = '\0';

    /* Collect triggers from all IMCs (defaults from g_ImcGameplay typically) */
    /* We use the first active IMC that has this action, then combine all triggers */
    for (s32 ci = 0; ci < s_NumActive; ci++) {
        InputMappingContext *ctx = s_Active[ci];
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

void actionmapSaveBinds(void)
{
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            buildBindStr(p, (InputAction)a,
                         s_BindStr[p][a], BIND_STR_MAX);
        }
    }
}

void actionmapLoadBinds(void)
{
    /* Parse the (possibly file-overridden) bind strings into g_ImcGameplay
     * and g_ImcVehicle triggers.  Other IMCs have fixed bindings. */
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        for (s32 a = 0; a < ACTION_COUNT; a++) {
            if (s_BindStr[p][a][0] != '\0') {
                parseBindStr(&g_ImcGameplay, p, (InputAction)a, s_BindStr[p][a]);
            }
        }
    }
    sysLogPrintf(LOG_NOTE, "ACTIONMAP: binds loaded from pd.ini");
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

f32  actionmapGetStickSensitivity(void) { return s_StickSensitivity; }
void actionmapSetStickSensitivity(f32 v) { s_StickSensitivity = clampf(v, 0.1f, 3.0f); }

f32  actionmapGetStickDeadzone(void) { return s_StickDeadzone; }
void actionmapSetStickDeadzone(f32 v) { s_StickDeadzone = clampf(v, 0.0f, 0.5f); }

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

InputMappingContext g_ImcVehicle = {
    .name     = "vehicle",
    .priority = 5,
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

/** Add bindings to an IMC for a given player, supporting up to 2 VKs. */
static void addBind(InputMappingContext *imc, InputAction action,
                    u32 vk1, u32 vk2)
{
    if (vk1) actionmapBind(imc, playerForVk(vk1), action, -1, vk1);
    if (vk2) actionmapBind(imc, playerForVk(vk2), action, -1, vk2);
}

static void setupGameplayDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcGameplay;
    s32 p = player;

    /* Keyboard/mouse defaults for player 0 */
    if (p == 0) {
        addBind(imc, ACTION_MOVE_FORWARD,   VKL_W,             JOY_BTN(0, JOFS_LSTICK_UP));
        addBind(imc, ACTION_MOVE_BACKWARD,  VKL_S,             JOY_BTN(0, JOFS_LSTICK_DOWN));
        addBind(imc, ACTION_MOVE_LEFT,      VK_A,              JOY_BTN(0, JOFS_LSTICK_LEFT));
        addBind(imc, ACTION_MOVE_RIGHT,     VKL_D,             JOY_BTN(0, JOFS_LSTICK_RIGHT));
        addBind(imc, ACTION_FIRE_PRIMARY,   VK_MOUSE_LEFT,     JOY_BTN(0, JOFS_RTRIG));
        addBind(imc, ACTION_FIRE_SECONDARY, VK_MOUSE_RIGHT,    JOY_BTN(0, JOFS_LTRIG));
        addBind(imc, ACTION_FIRE_MODE,      VKL_C,             0);            /* L_TRIG: fire mode cycle */
        addBind(imc, ACTION_RELOAD,         VKL_R,             JOY_BTN(0, JBTN_X)); /* X_BUTTON */
        addBind(imc, ACTION_USE,            VKL_F,             JOY_BTN(0, JBTN_A)); /* A_BUTTON */
        addBind(imc, ACTION_CANCEL_USE,     VK_MOUSE_MIDDLE,   JOY_BTN(0, JBTN_B)); /* B_BUTTON gameplay */
        addBind(imc, ACTION_CROUCH,         VK_LCTRL,          0);
        addBind(imc, ACTION_JUMP,           VK_SPACE,          JOY_BTN(0, JBTN_Y));
        addBind(imc, ACTION_SPRINT,         VK_LSHIFT,         0);
        addBind(imc, ACTION_ZOOM_IN,        0,                 0);            /* scope zoom: no default kbd */
        addBind(imc, ACTION_ZOOM_OUT,       0,                 0);
        addBind(imc, ACTION_WEAPON_PREV,    VK_MOUSE_WHEEL_UP, JOY_BTN(0, JBTN_LB));
        addBind(imc, ACTION_WEAPON_NEXT,    VK_MOUSE_WHEEL_DN, JOY_BTN(0, JBTN_RB));
        addBind(imc, ACTION_WEAPON_1,       (u32)VK_1,         0);
        addBind(imc, ACTION_WEAPON_2,       VKL_2,             0);
        addBind(imc, ACTION_WEAPON_3,       VKL_3,             0);
        addBind(imc, ACTION_WEAPON_4,       VKL_4,             0);
        addBind(imc, ACTION_WEAPON_5,       VKL_5,             0);
        addBind(imc, ACTION_WEAPON_6,       VKL_6,             0);
        /* Analog aim: right stick directions */
        addBind(imc, ACTION_AIM_UP,         VKL_UP,            JOY_BTN(0, JOFS_RSTICK_UP));
        addBind(imc, ACTION_AIM_DOWN,       VKL_DOWN,          JOY_BTN(0, JOFS_RSTICK_DOWN));
        addBind(imc, ACTION_AIM_LEFT,       VKL_LEFT,          JOY_BTN(0, JOFS_RSTICK_LEFT));
        addBind(imc, ACTION_AIM_RIGHT,      VKL_RIGHT,         JOY_BTN(0, JOFS_RSTICK_RIGHT));
        /* C-buttons: D-pad on gamepad (no kbd default; mouse handles aiming) */
        addBind(imc, ACTION_CBUTTON_UP,     0,                 JOY_BTN(0, JBTN_DPAD_UP));
        addBind(imc, ACTION_CBUTTON_DOWN,   0,                 JOY_BTN(0, JBTN_DPAD_DOWN));
        addBind(imc, ACTION_CBUTTON_LEFT,   0,                 JOY_BTN(0, JBTN_DPAD_LEFT));
        addBind(imc, ACTION_CBUTTON_RIGHT,  0,                 JOY_BTN(0, JBTN_DPAD_RIGHT));
        /* D-pad gameplay: Back/Select on gamepad (no conflict with C-button above) */
        addBind(imc, ACTION_DPAD_UP,        0,                 0);
        addBind(imc, ACTION_DPAD_DOWN,      0,                 0);
        addBind(imc, ACTION_DPAD_LEFT,      0,                 0);
        addBind(imc, ACTION_DPAD_RIGHT,     0,                 0);
        addBind(imc, ACTION_PAUSE,          VK_ESCAPE,         JOY_BTN(0, JBTN_START));
        addBind(imc, ACTION_SCREENSHOT,     VKL_F5,            0);
        addBind(imc, ACTION_CONSOLE_TOGGLE, VK_GRAVE,          0);
        addBind(imc, ACTION_DEBUG_TOGGLE,   (u32)VK_F9,        0);
    } else {
        /* Players 1-3: gamepad-only defaults */
        addBind(imc, ACTION_MOVE_FORWARD,   JOY_BTN(p, JOFS_LSTICK_UP),    0);
        addBind(imc, ACTION_MOVE_BACKWARD,  JOY_BTN(p, JOFS_LSTICK_DOWN),  0);
        addBind(imc, ACTION_MOVE_LEFT,      JOY_BTN(p, JOFS_LSTICK_LEFT),  0);
        addBind(imc, ACTION_MOVE_RIGHT,     JOY_BTN(p, JOFS_LSTICK_RIGHT), 0);
        addBind(imc, ACTION_FIRE_PRIMARY,   JOY_BTN(p, JOFS_RTRIG),        0);
        addBind(imc, ACTION_FIRE_SECONDARY, JOY_BTN(p, JOFS_LTRIG),        0);
        addBind(imc, ACTION_FIRE_MODE,      0,                              0);
        addBind(imc, ACTION_RELOAD,         JOY_BTN(p, JBTN_X),            0); /* X_BUTTON */
        addBind(imc, ACTION_USE,            JOY_BTN(p, JBTN_A),            0); /* A_BUTTON */
        addBind(imc, ACTION_CANCEL_USE,     JOY_BTN(p, JBTN_B),            0); /* B_BUTTON gameplay */
        addBind(imc, ACTION_CROUCH,         0,                              0);
        addBind(imc, ACTION_JUMP,           JOY_BTN(p, JBTN_Y),            0);
        addBind(imc, ACTION_WEAPON_PREV,    JOY_BTN(p, JBTN_LB),           0);
        addBind(imc, ACTION_WEAPON_NEXT,    JOY_BTN(p, JBTN_RB),           0);
        addBind(imc, ACTION_AIM_UP,         JOY_BTN(p, JOFS_RSTICK_UP),    0);
        addBind(imc, ACTION_AIM_DOWN,       JOY_BTN(p, JOFS_RSTICK_DOWN),  0);
        addBind(imc, ACTION_AIM_LEFT,       JOY_BTN(p, JOFS_RSTICK_LEFT),  0);
        addBind(imc, ACTION_AIM_RIGHT,      JOY_BTN(p, JOFS_RSTICK_RIGHT), 0);
        addBind(imc, ACTION_CBUTTON_UP,     JOY_BTN(p, JBTN_DPAD_UP),      0);
        addBind(imc, ACTION_CBUTTON_DOWN,   JOY_BTN(p, JBTN_DPAD_DOWN),    0);
        addBind(imc, ACTION_CBUTTON_LEFT,   JOY_BTN(p, JBTN_DPAD_LEFT),    0);
        addBind(imc, ACTION_CBUTTON_RIGHT,  JOY_BTN(p, JBTN_DPAD_RIGHT),   0);
        addBind(imc, ACTION_PAUSE,          JOY_BTN(p, JBTN_START),        0);
    }
}

static void setupVehicleDefaults(s32 player)
{
    InputMappingContext *imc = &g_ImcVehicle;
    s32 p = player;

    if (p == 0) {
        addBind(imc, ACTION_VEHICLE_ACCELERATE,  VKL_W,    JOY_BTN(0, JOFS_RTRIG));
        addBind(imc, ACTION_VEHICLE_BRAKE,       VKL_S,    JOY_BTN(0, JOFS_LTRIG));
        addBind(imc, ACTION_VEHICLE_STEER_LEFT,  VK_A,     JOY_BTN(0, JOFS_LSTICK_LEFT));
        addBind(imc, ACTION_VEHICLE_STEER_RIGHT, VKL_D,    JOY_BTN(0, JOFS_LSTICK_RIGHT));
        addBind(imc, ACTION_VEHICLE_EXIT,        VKL_F,    JOY_BTN(0, JBTN_A));
        addBind(imc, ACTION_PAUSE,               VK_ESCAPE, JOY_BTN(0, JBTN_START));
    } else {
        addBind(imc, ACTION_VEHICLE_ACCELERATE,  JOY_BTN(p, JOFS_RTRIG),       0);
        addBind(imc, ACTION_VEHICLE_BRAKE,       JOY_BTN(p, JOFS_LTRIG),       0);
        addBind(imc, ACTION_VEHICLE_STEER_LEFT,  JOY_BTN(p, JOFS_LSTICK_LEFT), 0);
        addBind(imc, ACTION_VEHICLE_STEER_RIGHT, JOY_BTN(p, JOFS_LSTICK_RIGHT),0);
        addBind(imc, ACTION_VEHICLE_EXIT,        JOY_BTN(p, JBTN_A),           0);
        addBind(imc, ACTION_PAUSE,               JOY_BTN(p, JBTN_START),       0);
    }
}

static void setupMenuDefaults(void)
{
    /* Menu IMC: keyboard + gamepad nav for all players */
    InputMappingContext *imc = &g_ImcMenu;

    /* Player 0 - keyboard */
    addBind(imc, ACTION_MENU_UP,       VKL_UP,       0);
    addBind(imc, ACTION_MENU_DOWN,     VKL_DOWN,     0);
    addBind(imc, ACTION_MENU_LEFT,     VKL_LEFT,     0);
    addBind(imc, ACTION_MENU_RIGHT,    VKL_RIGHT,    0);
    addBind(imc, ACTION_MENU_ACCEPT,   VK_RETURN,    0);
    addBind(imc, ACTION_MENU_CANCEL,   VK_ESCAPE,    0);
    addBind(imc, ACTION_MENU_TAB_PREV, VKL_Q,        0);
    addBind(imc, ACTION_MENU_TAB_NEXT, VKL_E,        0);

    /* All players - gamepad D-pad + face buttons */
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        addBind(imc, ACTION_MENU_UP,       JOY_BTN(p, JBTN_DPAD_UP),   JOY_BTN(p, JOFS_LSTICK_UP));
        addBind(imc, ACTION_MENU_DOWN,     JOY_BTN(p, JBTN_DPAD_DOWN), JOY_BTN(p, JOFS_LSTICK_DOWN));
        addBind(imc, ACTION_MENU_LEFT,     JOY_BTN(p, JBTN_DPAD_LEFT), JOY_BTN(p, JOFS_LSTICK_LEFT));
        addBind(imc, ACTION_MENU_RIGHT,    JOY_BTN(p, JBTN_DPAD_RIGHT),JOY_BTN(p, JOFS_LSTICK_RIGHT));
        addBind(imc, ACTION_MENU_ACCEPT,   JOY_BTN(p, JBTN_A),         0);
        addBind(imc, ACTION_MENU_CANCEL,   JOY_BTN(p, JBTN_B),         0);
        addBind(imc, ACTION_MENU_TAB_PREV, JOY_BTN(p, JBTN_LB),        0);
        addBind(imc, ACTION_MENU_TAB_NEXT, JOY_BTN(p, JBTN_RB),        0);
        addBind(imc, ACTION_PAUSE,         JOY_BTN(p, JBTN_START),     0);
    }
    addBind(imc, ACTION_PAUSE, VK_ESCAPE, 0);
}

static void setupPauseMenuDefaults(void)
{
    /* PauseMenu IMC: same as Menu but separate context so game knows it's a pause */
    InputMappingContext *imc = &g_ImcPauseMenu;

    addBind(imc, ACTION_MENU_UP,     VKL_UP,    0);
    addBind(imc, ACTION_MENU_DOWN,   VKL_DOWN,  0);
    addBind(imc, ACTION_MENU_LEFT,   VKL_LEFT,  0);
    addBind(imc, ACTION_MENU_RIGHT,  VKL_RIGHT, 0);
    addBind(imc, ACTION_MENU_ACCEPT, VK_RETURN, 0);
    addBind(imc, ACTION_MENU_CANCEL, VK_ESCAPE, 0);
    addBind(imc, ACTION_PAUSE,       VK_ESCAPE, 0);

    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        addBind(imc, ACTION_MENU_UP,     JOY_BTN(p, JBTN_DPAD_UP),   JOY_BTN(p, JOFS_LSTICK_UP));
        addBind(imc, ACTION_MENU_DOWN,   JOY_BTN(p, JBTN_DPAD_DOWN), JOY_BTN(p, JOFS_LSTICK_DOWN));
        addBind(imc, ACTION_MENU_ACCEPT, JOY_BTN(p, JBTN_A),         0);
        addBind(imc, ACTION_MENU_CANCEL, JOY_BTN(p, JBTN_B),         0);
        addBind(imc, ACTION_PAUSE,       JOY_BTN(p, JBTN_START),     0);
    }
}

static void setupDebugOverlayDefaults(void)
{
    InputMappingContext *imc = &g_ImcDebugOverlay;

    addBind(imc, ACTION_DEBUG_TOGGLE,   (u32)VK_F9,  0);
    addBind(imc, ACTION_MENU_UP,        VKL_UP,      0);
    addBind(imc, ACTION_MENU_DOWN,      VKL_DOWN,    0);
    addBind(imc, ACTION_MENU_LEFT,      VKL_LEFT,    0);
    addBind(imc, ACTION_MENU_RIGHT,     VKL_RIGHT,   0);
    addBind(imc, ACTION_MENU_ACCEPT,    VK_RETURN,   0);
    addBind(imc, ACTION_MENU_CANCEL,    VK_ESCAPE,   0);
    addBind(imc, ACTION_CONSOLE_TOGGLE, VK_GRAVE,    0);
    addBind(imc, ACTION_SCREENSHOT,     VKL_F5,      0);
}

static void setupTextInputDefaults(void)
{
    InputMappingContext *imc = &g_ImcTextInput;

    /* Text input context binds very few actions — most key events
     * go directly to SDL text input mode, not through actionmap. */
    addBind(imc, ACTION_MENU_ACCEPT, VK_RETURN,    JOY_BTN(0, JBTN_A));
    addBind(imc, ACTION_MENU_CANCEL, VK_ESCAPE,    JOY_BTN(0, JBTN_B));
    addBind(imc, ACTION_CHEAT_ENTER, VK_RETURN,    0);
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
    } else if (imc == &g_ImcVehicle) {
        setupVehicleDefaults(player);
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
    memset(&g_ImcVehicle,      0, sizeof(g_ImcVehicle));
    memset(&g_ImcMenu,         0, sizeof(g_ImcMenu));
    memset(&g_ImcPauseMenu,    0, sizeof(g_ImcPauseMenu));
    memset(&g_ImcDebugOverlay, 0, sizeof(g_ImcDebugOverlay));
    memset(&g_ImcTextInput,    0, sizeof(g_ImcTextInput));

    /* Restore names and priorities (memset wiped them) */
    g_ImcGameplay.name     = "gameplay";      g_ImcGameplay.priority     = 0;
    g_ImcVehicle.name      = "vehicle";       g_ImcVehicle.priority      = 5;
    g_ImcMenu.name         = "menu";          g_ImcMenu.priority         = 10;
    g_ImcPauseMenu.name    = "pause_menu";    g_ImcPauseMenu.priority    = 11;
    g_ImcDebugOverlay.name = "debug_overlay"; g_ImcDebugOverlay.priority = 20;
    g_ImcTextInput.name    = "text_input";    g_ImcTextInput.priority    = 30;

    /* Populate default bindings (into IMC structs, not yet active) */
    for (s32 p = 0; p < ACTIONMAP_MAX_PLAYERS; p++) {
        setupGameplayDefaults(p);
        setupVehicleDefaults(p);
    }
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

    /* Register stick tuning variables with config system */
    configRegisterFloat("ActionMap.StickSensitivity", &s_StickSensitivity, 0.1f, 3.0f);
    configRegisterFloat("ActionMap.StickDeadzone",    &s_StickDeadzone,    0.0f, 0.5f);
    configRegisterInt("ActionMap.StickInvertY",       &s_StickInvertY,     0, 1);

    /* Activate the gameplay and menu contexts by default.
     * Callers activate Vehicle/Pause/Debug/TextInput as needed. */
    imcActivate(&g_ImcGameplay);
    imcActivate(&g_ImcMenu);

    sysLogPrintf(LOG_NOTE, "ACTIONMAP: initialized — %d actions, %d players, %d IMCs",
                 (s32)ACTION_COUNT, ACTIONMAP_MAX_PLAYERS, s_NumActive);
}
