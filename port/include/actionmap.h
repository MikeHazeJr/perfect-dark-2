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
 * Gameplay thresholds (shared: bondmove hold/tap + UI)
 * ============================================================ */

/** Default hold/tap split for ACTION_USE (interact vs reload), in ms.
 *  - Seeds `actionmap` internal `s_UseHoldThresholdMs` (see `actionmapGetUseHoldThresholdMs()`).
 *  - Last-resort fallback in `prop.c` when `actionmapGetEffectiveHoldMs(ACTION_USE)` is invalid
 *    (< 1 ms), e.g. corrupt save — keeps bondmove/UI from dividing by zero.
 *  Normal gameplay uses `actionmapGetEffectiveHoldMs(ACTION_USE)` / per-action overrides, not this macro. */
#define ACTION_USE_HOLD_THRESHOLD_MS 300

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
 * §2.1  InputAction enum — 45 actions
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
    /* ACTION_MENU_ACCEPT and ACTION_MENU_CANCEL removed:
     * consolidated into ACTION_USE (= 24) and ACTION_CANCEL_USE (= 25) respectively.
     * Use #define aliases below for any remaining references. */
    ACTION_MENU_TAB_PREV,       /* = 49 */
    ACTION_MENU_TAB_NEXT,       /* = 50 */

    /* ---- System ---- */
    ACTION_PAUSE,               /* = 51 START_BUTTON */
    ACTION_SCREENSHOT,          /* = 52 */
    ACTION_CONSOLE_TOGGLE,      /* = 53 */
    ACTION_DEBUG_TOGGLE,        /* = 54 */
    ACTION_CHEAT_ENTER,         /* = 55 */
    ACTION_SCORECARD,           /* = 56 hold-to-show scoreboard (Tab / Back button) */

    /* ---- Forge level editor (F0+) ---- */
    ACTION_FORGE_TOGGLE,        /* = 57 toggle Normal <-> Freefly within a forge session */
    ACTION_FORGE_ASCEND,        /* = 58 freefly +Y (E key / RT trigger) */
    ACTION_FORGE_DESCEND,       /* = 59 freefly -Y (Q key / LT trigger) */
    ACTION_FORGE_BOOST,         /* = 60 hold for 3x freefly speed (LSHIFT) */
    ACTION_FORGE_PRECISION,     /* = 61 hold for 0.25x freefly speed (LCTRL) */

    /* ---- Forge editor sidebar + tab navigation (Issue 8b, 2026-04-24) ----
     * All six actions fire in every context (they live on the gameplay IMC)
     * but the forge editor only consumes them when a forge session is
     * active AND in FREEFLY.  Combat actions sharing the same default
     * buttons (X = USE, LB/RB = WEAPON_PREV/NEXT) are suppressed during
     * FREEFLY via actionIsBlockedInFreefly, so the bindings do not
     * conflict in practice. */
    ACTION_FORGE_SIDEBAR_TOGGLE, /* = 62 show / hide the editor sidebar (X on pad, Tab on key) */
    ACTION_FORGE_SIDEBAR_UP,     /* = 63 sidebar selection -1 (D-pad up) */
    ACTION_FORGE_SIDEBAR_DOWN,   /* = 64 sidebar selection +1 (D-pad down) */
    ACTION_FORGE_SIDEBAR_ACTIVATE,/* = 65 activate focused sidebar row (D-pad right) */
    ACTION_FORGE_TAB_PREV,       /* = 66 previous editor tab (LB on pad, PageUp / Ctrl+Shift+Tab on key) */
    ACTION_FORGE_TAB_NEXT,       /* = 67 next editor tab (RB on pad, PageDown / Ctrl+Tab on key) */

    /* ---- Combat Sim hold-vs-tap (Priority J, 2026-04-25) ----
     * Parallel action to ACTION_SCORECARD: bound only in g_ImcCombatSim
     * to gamepad Back. Reads via actionHeldForMs() with a hold threshold
     * (default 400 ms). Lets CS surface the scorecard on Back-hold while
     * Mission keeps Back inert and keyboard Tab keeps the transient peek
     * on ACTION_SCORECARD across both schemes. */
    ACTION_SCORECARD_HOLD,       /* = 68 hold-Back-for-scorecard, CS scheme only */

    /* ---- Connectivity / friends sidebar (S483b, 2026-04-27) ----
     * Toggles pdguiFriends sidebar (Online connectivity surface). Bound
     * only on the menu / pause-menu IMCs (g_ImcMenu, g_ImcPauseMenu) so
     * that pressing Tab during active gameplay -- when only g_ImcGameplay
     * is on top -- structurally cannot toggle the sidebar. The legacy
     * raw `ImGui::IsKeyPressed(ImGuiKey_Tab)` check at pdgui_friends.cpp
     * was bypassing the IMC stack and opening the sidebar mid-mission;
     * routing through the actionmap restores IMC discipline. */
    ACTION_SOCIAL_TOGGLE,        /* = 69 toggle friends/connectivity sidebar (menu IMCs only) */

    /* ---- Test Scenarios benchmark cycler (S483, PD_DEV_BUILD only) ----
     * Cycles the swarm bot count 4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256 -> 4
     * inside the Swarm CPU / Swarm GPU test scenarios. Default bindings:
     * KEY_0 on keyboard, SDL_CONTROLLER_BUTTON_DPAD_DOWN on gamepad. The
     * binding is gameplay-only and is consumed by swarm_test.c::tick(). */
    ACTION_TESTSCEN_CYCLE_COUNT, /* = 70 cycle bot count in swarm benchmark */

    ACTION_COUNT                /* = 71, sentinel — keep last */
} InputAction;

/* Backward-compat aliases */
#define ACTION_INTERACT     ACTION_USE        /* A_BUTTON was ACTION_INTERACT, now ACTION_USE */
#define ACTION_MENU_ACCEPT  ACTION_USE        /* Consolidated: menu accept = gameplay use */
#define ACTION_MENU_CANCEL  ACTION_CANCEL_USE /* Consolidated: menu cancel = gameplay cancel */

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
 *
 * Hold timing: down_time_ms / up_time_ms are SDL_GetTicks() snapshots at the
 * last rising/falling edge.  hold_consumed lets a consumer mark a hold as
 * already handled so the matching release does not fire a tap action too.
 */
typedef struct {
    s32  held;          /* currently held */
    s32  pressed;       /* rising edge this frame */
    s32  released;      /* falling edge this frame */
    f32  value;         /* magnitude */
    u32  down_time_ms;  /* SDL_GetTicks at last press; 0 = never pressed */
    u32  up_time_ms;    /* SDL_GetTicks at last release */
    s32  hold_consumed; /* 1 = a hold-action has already fired this hold cycle */
    u32  hold_pin_full_until_ms; /* B-221.2: show full ring briefly after consume */
    u32  hold_vis_grace_until_ms; /* B-221.2: after release, keep last progress ~100ms */
    f32  hold_vis_last_down_progress; /* last in-hold progress for UI grace */
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

/** Flush (zero) all per-player state for gameplay-only actions.
 *
 *  Called by inputctx on non-gameplay context push (menu open) and by the
 *  SDL focus-lost handler. Leaves menu/shared actions (ACTION_MENU_*,
 *  ACTION_USE/_CANCEL_USE, ACTION_PAUSE, system) intact so the menu layer
 *  retains its authoritative state. Part of the input-authority predicate
 *  (ADR context/designs/input-authority-and-menu-pool-2026-04-13.md).
 */
void actionmapFlushGameplayState(void);

/** Classify an action as gameplay-only.
 *  Returns 1 for pure gameplay actions (movement, combat, weapon, vehicle,
 *  scorecard, aim, C-buttons, D-pad when mapped to gameplay, etc.).
 *  Returns 0 for menu nav, ACTION_USE/_CANCEL_USE, ACTION_PAUSE, and the
 *  system hotkey group (screenshot/console/debug/cheat).
 */
s32 actionIsGameplayOnly(InputAction a);

/** Priority D (2026-04-24): classify an action as blocked while the
 *  Forge session is in FREEFLY (observer mode). Returns 1 for combat /
 *  weapon / vehicle / interact actions that shouldn't fire on the
 *  frozen player-chr; returns 0 for movement / aim / forge-editor
 *  actions that the freefly camera relies on.
 */
s32 actionIsBlockedInFreefly(InputAction a);

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
 * Hold/tap discrimination helpers
 *
 * A "tap" = released this frame after being held for less than max_hold_ms.
 * A "hold" = currently held for at least threshold_ms continuous time.
 *
 * hold_consumed lets a consumer flag a hold as already handled, so the
 * matching release does NOT also fire as a tap.  Pattern:
 *
 *   if (actionHeldForMs(p, A, 250) && !actionHoldConsumed(p, A)) {
 *       fireInteract();
 *       actionConsumeHold(p, A);
 *   }
 *   if (actionWasTap(p, A, 250)) {
 *       fireReload();
 *   }
 *
 * actionHoldProgress returns 0..1 fill while held; reaches 1 at threshold_ms.
 * Returns 0 when not held.
 * ============================================================ */

/** Currently held for at least `threshold_ms` continuous time. */
s32 actionHeldForMs(s32 player, InputAction action, s32 threshold_ms);

/** Released this frame after being held for less than `max_hold_ms`,
 *  AND the hold was not consumed (so a hold-then-release does not double-fire). */
s32 actionWasTap(s32 player, InputAction action, s32 max_hold_ms);

/** Milliseconds held for the gesture that ended this frame (valid when `actionReleased` is true). */
s32 actionLastGestureHoldMs(s32 player, InputAction action);

/** Mark the current hold as consumed (set during a long-press handler). */
void actionConsumeHold(s32 player, InputAction action);

/** Returns 1 if the current hold has already been consumed. */
s32 actionHoldConsumed(s32 player, InputAction action);

/** Hold fill in [0..1] toward `threshold_ms`.  0 when not held. */
f32 actionHoldProgress(s32 player, InputAction action, s32 threshold_ms);

/** SDL_GetTicks() at the start of the current hold, or 0 if not held. */
u32 actionHoldPressStartMs(s32 player, InputAction action);

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

/** 1 = physical left stick drives movement; 0 = physical right stick drives movement
 *  (look uses the other stick). Same information as swap-sticks, explicit for UI. */
void actionmapSetMoveStickPhysicalLeft(s32 useLeft);
s32  actionmapGetMoveStickPhysicalLeft(void);

f32  actionmapGetStickSensitivityMove(void);
void actionmapSetStickSensitivityMove(f32 v);
f32  actionmapGetStickSensitivityAim(void);
void actionmapSetStickSensitivityAim(f32 v);

/** Settings UI scale 1.0-10.0 (0.5 steps). Maps to internal aim/move mult 0.1-3.0. */
f32  actionmapSensUiToMult(f32 ui);
f32  actionmapMultToSensUi(f32 mult);
f32  actionmapSnapSensUi(f32 ui);
f32  actionmapGetSensMoveUi(void);
void actionmapSetSensMoveUi(f32 ui);
f32  actionmapGetSensAimUi(void);
void actionmapSetSensAimUi(f32 ui);
f32  actionmapGetSensAdsUi(void);
void actionmapSetSensAdsUi(f32 ui);
void actionmapRefreshStickMultFromUi(void);
/** PC ADS: multiply gun zoom FOV (smaller = more zoom). Depends on ADS sensitivity UI. */
f32  actionmapGetPcAdsZoomFovMul(void);
f32  actionmapGetStickDeadzoneMove(void);
void actionmapSetStickDeadzoneMove(f32 v);
f32  actionmapGetStickDeadzoneAim(void);
void actionmapSetStickDeadzoneAim(f32 v);

s32  actionmapGetUseHoldThresholdMs(void);
void actionmapSetUseHoldThresholdMs(s32 ms);

/** Per-action hold length (ms) when gameplay treats an action as hold-to-complete.
 *  -1 = no override (ACTION_USE uses ActionMap.UseHoldThresholdMs from Settings
 *  -> Controls -> Controller; others: see actionmapGetEffectiveHoldMs). Non-negative
 *  values persist in pd.ini as ActionMap.HoldMsOverrides (comma-separated
 *  "action_id:ms"). Values are clamped to at most 2000 ms (2 s). */
s32 actionmapGetActionHoldMsOverride(InputAction action);
void actionmapSetActionHoldMsOverride(InputAction action, s32 ms);

/** Resolved hold window (ms): override if set, else global default for ACTION_USE, else 0. */
s32 actionmapGetEffectiveHoldMs(InputAction action);

/** Extra ms added for hackable-terminal interact prompts (`OBJFLAG3_HTMTERMINAL` in prop.c).
 *  Saved as ActionMap.InteractHoldExtraTerminalMs in pd.ini (default 200). */
s32 actionmapGetInteractHoldExtraTerminalMs(void);
void actionmapSetInteractHoldExtraTerminalMs(s32 ms);

/* ============================================================
 * Default IMC singletons
 * ============================================================ */

extern InputMappingContext g_ImcGameplay;      /* priority  0 — shared baseline: movement, combat, weapons, interact */
extern InputMappingContext g_ImcMission;       /* priority  1 — solo / co-op / anti scheme (Priority J)              */
extern InputMappingContext g_ImcCombatSim;     /* priority  1 — Combat Sim / MP scheme + scorecard-hold (Priority J)  */
extern InputMappingContext g_ImcVehicle;       /* priority  5 — vehicle controls     */
extern InputMappingContext g_ImcForgeSession;  /* priority  6 — Forge session toggle (whole session) */
extern InputMappingContext g_ImcForge;         /* priority  7 — Forge editor overlay (FREEFLY only)  */
extern InputMappingContext g_ImcMenu;          /* priority 10 — ImGui menu nav        */
extern InputMappingContext g_ImcPauseMenu;     /* priority 11 — in-game pause         */
extern InputMappingContext g_ImcDebugOverlay;  /* priority 20 — F12 debug window      */
extern InputMappingContext g_ImcTextInput;     /* priority 30 — text entry / chat     */

/* ============================================================
 * Scene-scope IMC dispatch (Priority J, 2026-04-25)
 *
 * Mission XOR CombatSim. The bottom-of-stack g_ImcGameplay holds the
 * shared baseline bindings and stays active across every gameplay scene;
 * Mission and CombatSim shadow only on actions they explicitly bind
 * (today: only ACTION_SCORECARD_HOLD on CombatSim). Activation is driven
 * by scene load -- callers must dispatch exactly one of these per scene.
 *
 * Mutual exclusion is asserted internally: if the wrong-other IMC is
 * already active when set is called, it is deactivated first and a
 * LOG_WARNING is emitted. The clear path also deactivates Vehicle so a
 * mid-vehicle stage transition does not leak a stale vehicle IMC.
 *
 * See context/designs/contextual-input-schemes.md.
 * ============================================================ */

/** Activate the Mission IMC. Deactivates CombatSim if it was active.
 *  Use for: solo missions, co-op campaign, anti-counter-op. */
void imcSceneSetMission(void);

/** Activate the CombatSim IMC. Deactivates Mission if it was active.
 *  Use for: Combat Simulator, online MP matches. */
void imcSceneSetCombatSim(void);

/** Deactivate both Mission and CombatSim (and Vehicle, defensively).
 *  Use for: SYSTEM stages (title / intro / endscreen), netDisconnect. */
void imcSceneClearGameplay(void);

/** Activate / deactivate the Vehicle IMC. Called from the hoverbike
 *  mount / dismount entry points (Priority J-2). Vehicle bindings shadow
 *  the gameplay baseline (steering, throttle, brake) while mounted; on
 *  dismount the baseline takes over again. */
void imcVehicleMount(void);
void imcVehicleDismount(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ACTIONMAP_H */
