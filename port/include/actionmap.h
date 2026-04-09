/**
 * actionmap.h — M0.2 Phase B2: Core Action Map System (updated enum)
 *
 * Abstracts raw SDL input (keys, mouse, gamepad) into named game actions.
 * Provides:
 *   - InputAction enum (58 actions covering gameplay, menu, system)
 *   - InputMappingContext (named, prioritized binding sets)
 *   - Per-player ActionState (pressed/held/released/value)
 *   - 6 default IMC singletons (Gameplay/Vehicle/Menu/PauseMenu/Debug/TextInput)
 *   - pd.ini persistence via configRegisterString
 *   - 20-input rolling cheat code buffer
 *   - 500ms debounce last-device tracking
 *
 * C/C++ compatible: all declarations wrapped in extern "C".
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#ifndef _IN_ACTIONMAP_H
#define _IN_ACTIONMAP_H

#include <PR/ultratypes.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * Capacities
 * ============================================================ */

#define ACTIONMAP_MAX_TRIGGERS      4    /* bindings per action per IMC */
#define ACTIONMAP_MAX_CONTEXTS      8    /* active IMCs at once */
#define ACTIONMAP_MAX_PLAYERS       4    /* splitscreen players */
#define ACTIONMAP_CHEAT_BUF_LEN    20    /* rolling cheat input window */
#define ACTIONMAP_DEFAULT_DEADZONE  0.15f
#define ACTIONMAP_DEVICE_DEBOUNCE_MS 500

/* ============================================================
 * §2.1  InputAction enum — 47 actions
 * ============================================================ */

typedef enum InputAction {
    /* ---- Gameplay: movement ---- */
    ACTION_MOVE_FORWARD = 0,    /* = 0  */
    ACTION_MOVE_BACKWARD,       /* = 1  */
    ACTION_MOVE_LEFT,           /* = 2  */
    ACTION_MOVE_RIGHT,          /* = 3  */
    ACTION_AXIS_MOVE_X,         /* = 4  signed analog: left stick X  (-1..1) */
    ACTION_AXIS_MOVE_Y,         /* = 5  signed analog: left stick Y  (-1..1) */

    /* ---- Gameplay: aiming (analog right stick / mouse) ---- */
    ACTION_AIM_UP,              /* = 6  */
    ACTION_AIM_DOWN,            /* = 7  */
    ACTION_AIM_LEFT,            /* = 8  */
    ACTION_AIM_RIGHT,           /* = 9  */
    ACTION_AXIS_AIM_X,          /* = 10 signed analog: right stick X / mouse X */
    ACTION_AXIS_AIM_Y,          /* = 11 signed analog: right stick Y / mouse Y */

    /* ---- N64 C-buttons (digital: strafe L/R and look up/down in gameplay) ---- */
    ACTION_CBUTTON_UP,          /* = 12 U_CBUTTONS */
    ACTION_CBUTTON_DOWN,        /* = 13 D_CBUTTONS */
    ACTION_CBUTTON_LEFT,        /* = 14 L_CBUTTONS */
    ACTION_CBUTTON_RIGHT,       /* = 15 R_CBUTTONS */

    /* ---- Gameplay: D-pad (weapon/function select in gameplay context) ---- */
    ACTION_DPAD_UP,             /* = 16 U_JPAD */
    ACTION_DPAD_DOWN,           /* = 17 D_JPAD */
    ACTION_DPAD_LEFT,           /* = 18 L_JPAD */
    ACTION_DPAD_RIGHT,          /* = 19 R_JPAD */

    /* ---- Gameplay: combat ---- */
    ACTION_FIRE_PRIMARY,        /* = 20 Z_TRIG */
    ACTION_FIRE_SECONDARY,      /* = 21 R_TRIG */
    ACTION_FIRE_MODE,           /* = 22 L_TRIG — fire mode cycle */
    ACTION_RELOAD,              /* = 23 X_BUTTON */
    ACTION_USE,                 /* = 24 A_BUTTON — interact, pick up, open doors */
    ACTION_CANCEL_USE,          /* = 25 B_BUTTON gameplay — cancel action / drop weapon */
    ACTION_THROW_WEAPON,        /* = 26 B_BUTTON gesture in gameplay */
    ACTION_CROUCH,              /* = 27 */
    ACTION_JUMP,                /* = 28 */
    ACTION_SPRINT,              /* = 29 */
    ACTION_ZOOM_IN,             /* = 30 scope zoom (distinct from fire mode) */
    ACTION_ZOOM_OUT,            /* = 31 */

    /* ---- Gameplay: weapon selection ---- */
    ACTION_WEAPON_PREV,         /* = 32 */
    ACTION_WEAPON_NEXT,         /* = 33 Y_BUTTON */
    ACTION_WEAPON_1,            /* = 34 */
    ACTION_WEAPON_2,            /* = 35 */
    ACTION_WEAPON_3,            /* = 36 */
    ACTION_WEAPON_4,            /* = 37 */
    ACTION_WEAPON_5,            /* = 38 */
    ACTION_WEAPON_6,            /* = 39 */

    /* ---- Vehicle ---- */
    ACTION_VEHICLE_ACCELERATE,  /* = 40 */
    ACTION_VEHICLE_BRAKE,       /* = 41 */
    ACTION_VEHICLE_STEER_LEFT,  /* = 42 */
    ACTION_VEHICLE_STEER_RIGHT, /* = 43 */
    ACTION_VEHICLE_EXIT,        /* = 44 */

    /* ---- Menu navigation ---- */
    ACTION_MENU_UP,             /* = 45 */
    ACTION_MENU_DOWN,           /* = 46 */
    ACTION_MENU_LEFT,           /* = 47 */
    ACTION_MENU_RIGHT,          /* = 48 */
    ACTION_MENU_ACCEPT,         /* = 49 */
    ACTION_MENU_CANCEL,         /* = 50 */
    ACTION_MENU_TAB_PREV,       /* = 51 */
    ACTION_MENU_TAB_NEXT,       /* = 52 */

    /* ---- System ---- */
    ACTION_PAUSE,               /* = 53 START_BUTTON */
    ACTION_SCREENSHOT,          /* = 54 */
    ACTION_CONSOLE_TOGGLE,      /* = 55 */
    ACTION_DEBUG_TOGGLE,        /* = 56 */
    ACTION_CHEAT_ENTER,         /* = 57 */
    ACTION_SCORECARD,           /* = 58 hold-to-show scoreboard (Tab / Back button) */

    ACTION_COUNT                /* = 59, sentinel — keep last */
} InputAction;

/* Backward-compat alias: A_BUTTON was ACTION_INTERACT, now ACTION_USE */
#define ACTION_INTERACT ACTION_USE

/* ============================================================
 * Core structs
 * ============================================================ */

/**
 * InputTrigger — a single VK binding for an action.
 * vk == 0 means the slot is empty.
 */
typedef struct {
    u32 vk;         /* virtkey value (see input.h enum virtkey) */
} InputTrigger;

/**
 * InputMapping — one action mapped to up to ACTIONMAP_MAX_TRIGGERS triggers.
 */
typedef struct {
    InputAction  action;
    InputTrigger triggers[ACTIONMAP_MAX_TRIGGERS];
    s32          num_triggers;
} InputMapping;

/**
 * InputMappingContext — a named, prioritized binding set.
 *
 * Higher priority contexts are consulted first during dispatch.
 * An action is resolved by the highest-priority active context that maps it.
 * Contexts are activated/deactivated at runtime via imcActivate/imcDeactivate.
 */
typedef struct InputMappingContext {
    const char  *name;
    s32          priority;              /* higher = first consulted */
    s32          active;               /* managed by imcActivate/Deactivate */
    InputMapping mappings[ACTION_COUNT]; /* indexed directly by InputAction */
    s32          has_mapping[ACTION_COUNT]; /* 1 = slot is populated */
} InputMappingContext;

/**
 * ActionState — per-player, per-action state for one frame.
 *
 * pressed/released are one-frame edge signals cleared by actionmapEndFrame().
 * value is in [0..1] for digital actions, [-1..1] for signed axis actions.
 */
typedef struct {
    s32  held;      /* currently held */
    s32  pressed;   /* rising edge this frame */
    s32  released;  /* falling edge this frame */
    f32  value;     /* magnitude */
} ActionState;

/* ============================================================
 * Lifecycle
 * ============================================================ */

/** Initialize system, register pd.ini keys, activate default IMCs.
 *  Call once at startup before configLoad(). */
void actionmapInit(void);

/** Activate an IMC (add to priority-sorted active list). Safe to call mid-frame. */
void imcActivate(InputMappingContext *imc);

/** Deactivate an IMC (remove from active list). Safe to call mid-frame. */
void imcDeactivate(InputMappingContext *imc);

/* ============================================================
 * Per-frame update
 * ============================================================ */

/** Process one SDL event — updates digital action states for all players.
 *  Call from the SDL event loop for every event. */
void actionmapDispatch(const SDL_Event *ev);

/** Sample analog axes (gamepad sticks, mouse delta) with deadzone.
 *  Call once per frame before game logic reads actions. */
void actionmapPollFrame(void);

/** Flip edge state: clear pressed/released for all players/actions.
 *  Call at the end of each frame (after all consumers have queried). */
void actionmapEndFrame(void);

/* ============================================================
 * Query API (5 functions)
 * ============================================================ */

/** Was this action just pressed (rising edge) this frame? */
s32 actionPressed(s32 player, InputAction action);

/** Is this action currently held? */
s32 actionHeld(s32 player, InputAction action);

/** Was this action just released (falling edge) this frame? */
s32 actionReleased(s32 player, InputAction action);

/** Analog magnitude [0..1] for digital; signed [-1..1] for axis actions. */
f32 actionValue(s32 player, InputAction action);

/** 2D axis values for axis-pair actions.
 *  - ACTION_AXIS_MOVE_X/Y  → left stick X and Y
 *  - ACTION_AXIS_AIM_X/Y   → right stick X and Y (or mouse)
 *  - Any other action      → (actionValue(p,a), 0.0f) */
void actionAxis(s32 player, InputAction action, f32 *out_x, f32 *out_y);

/* ============================================================
 * Last-device detection
 * ============================================================ */

#define ACTIONMAP_DEVICE_KBM     0
#define ACTIONMAP_DEVICE_GAMEPAD 1

/** Returns ACTIONMAP_DEVICE_KBM or ACTIONMAP_DEVICE_GAMEPAD.
 *  500ms debounce prevents flicker on transitions. */
s32 actionmapGetLastDevice(void);

/* ============================================================
 * Bind management
 * ============================================================ */

/** Set trigger slot [0..ACTIONMAP_MAX_TRIGGERS-1] for player's action in imc.
 *  vk == 0 clears the slot. */
void actionmapBind(InputMappingContext *imc, s32 player,
                   InputAction action, s32 slot, u32 vk);

/** Reset all bindings in imc for player to built-in PC defaults. */
void actionmapSetDefaults(InputMappingContext *imc, s32 player);

/** Parse pd.ini bind strings into trigger arrays.  Call after configLoad(). */
void actionmapLoadBinds(void);

/** Serialize current trigger arrays into pd.ini bind strings.
 *  Call before configSave(). */
void actionmapSaveBinds(void);

/* ============================================================
 * Cheat code buffer
 * ============================================================ */

/** Returns 1 if the last `len` distinct action presses match seq[].
 *  seq must have len ≤ ACTIONMAP_CHEAT_BUF_LEN elements. */
s32 actionmapCheckCheat(const InputAction *seq, s32 len);

/** Clear the rolling cheat input buffer (call on mission start, etc.). */
void actionmapClearCheat(void);

/* ============================================================
 * Stick tuning (controller sensitivity, deadzone, invert, swap)
 * ============================================================ */

f32  actionmapGetStickSensitivity(void);
void actionmapSetStickSensitivity(f32 v);

f32  actionmapGetStickDeadzone(void);
void actionmapSetStickDeadzone(f32 v);

s32  actionmapGetStickInvertY(void);
void actionmapSetStickInvertY(s32 v);

void actionmapSetSwapSticks(s32 swapped);
s32  actionmapGetSwapSticks(void);

/* ============================================================
 * Default IMC singletons
 * ============================================================ */

extern InputMappingContext g_ImcGameplay;      /* priority  0 — WASD + mouse + JOY1 */
extern InputMappingContext g_ImcVehicle;       /* priority  5 — vehicle controls     */
extern InputMappingContext g_ImcMenu;          /* priority 10 — ImGui menu nav        */
extern InputMappingContext g_ImcPauseMenu;     /* priority 11 — in-game pause         */
extern InputMappingContext g_ImcDebugOverlay;  /* priority 20 — F12 debug window      */
extern InputMappingContext g_ImcTextInput;     /* priority 30 — text entry / chat     */

#ifdef __cplusplus
}
#endif

#endif /* _IN_ACTIONMAP_H */
