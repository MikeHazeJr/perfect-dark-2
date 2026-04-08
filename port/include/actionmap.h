/**
 * actionmap.h — M0.2 Phase A: Core Action Map System
 *
 * Abstracts raw SDL input (keys, mouse, gamepad) into named game actions.
 * Provides:
 *   - InputAction enum (47 actions covering gameplay, menu, system)
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
    ACTION_MOVE_FORWARD = 0,
    ACTION_MOVE_BACKWARD,
    ACTION_MOVE_LEFT,
    ACTION_MOVE_RIGHT,
    ACTION_AXIS_MOVE_X,         /* signed analog: left stick X  (-1..1) */
    ACTION_AXIS_MOVE_Y,         /* signed analog: left stick Y  (-1..1) */

    /* ---- Gameplay: aiming ---- */
    ACTION_AIM_UP,
    ACTION_AIM_DOWN,
    ACTION_AIM_LEFT,
    ACTION_AIM_RIGHT,
    ACTION_AXIS_AIM_X,          /* signed analog: right stick X / mouse X */
    ACTION_AXIS_AIM_Y,          /* signed analog: right stick Y / mouse Y */

    /* ---- Gameplay: combat ---- */
    ACTION_FIRE_PRIMARY,
    ACTION_FIRE_SECONDARY,
    ACTION_RELOAD,
    ACTION_INTERACT,
    ACTION_CROUCH,
    ACTION_JUMP,
    ACTION_SPRINT,
    ACTION_ZOOM_IN,
    ACTION_ZOOM_OUT,

    /* ---- Gameplay: weapon selection ---- */
    ACTION_WEAPON_PREV,
    ACTION_WEAPON_NEXT,
    ACTION_WEAPON_1,
    ACTION_WEAPON_2,
    ACTION_WEAPON_3,
    ACTION_WEAPON_4,
    ACTION_WEAPON_5,
    ACTION_WEAPON_6,

    /* ---- Vehicle ---- */
    ACTION_VEHICLE_ACCELERATE,
    ACTION_VEHICLE_BRAKE,
    ACTION_VEHICLE_STEER_LEFT,
    ACTION_VEHICLE_STEER_RIGHT,
    ACTION_VEHICLE_EXIT,

    /* ---- Menu navigation ---- */
    ACTION_MENU_UP,
    ACTION_MENU_DOWN,
    ACTION_MENU_LEFT,
    ACTION_MENU_RIGHT,
    ACTION_MENU_ACCEPT,
    ACTION_MENU_CANCEL,
    ACTION_MENU_TAB_PREV,
    ACTION_MENU_TAB_NEXT,

    /* ---- System ---- */
    ACTION_PAUSE,
    ACTION_SCREENSHOT,
    ACTION_CONSOLE_TOGGLE,
    ACTION_DEBUG_TOGGLE,
    ACTION_CHEAT_ENTER,

    ACTION_COUNT   /* sentinel — keep last */
} InputAction;

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
