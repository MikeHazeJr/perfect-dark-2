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
#define ACTIONMAP_MAX_CONTEXTS     10    /* active IMCs at once */
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

    /* ---- Text input field actions (Cohort 1, 2026-04-27) ----
     * Bound only on g_ImcTextInput. Allows raw IsMouseClicked(Right) sites
     * to migrate to actionPressed(0, ACTION_TEXT_PASTE) without leaking
     * mouse-clicks-as-paste to gameplay/menus. Exempt from gameplay-only
     * classification so menus never gate it via gameplayInputSuppressed. */
    ACTION_TEXT_PASTE,           /* = 71 right-mouse-button paste-from-clipboard */

    /* ---- Cutscene skip (Cohort 4, 2026-04-27, K.2 + K.6) ----
     * Bound on g_ImcCutscene only (the thin per-K.2 IMC). Used by
     * playerTickCutscene's skip detection alongside the legacy
     * actionPressed checks for USE / CANCEL_USE / FIRE_PRIMARY /
     * FIRE_SECONDARY / FIRE_MODE / RELOAD / WEAPON_NEXT / PAUSE.
     * The K.6 belt-and-braces fix for the Mission 1 obj 2 cutscene
     * flash uses actionPressed (edge), not actionHeld (level), so
     * a key held across the menu-accept-then-stage-load transition
     * cannot register as a skip. Exempt from gameplay-only
     * classification. */
    ACTION_SKIP_CUTSCENE,        /* = 72 dedicated cutscene-skip action */

    /* ---- Menu secondary commands (2026-04-28) ----
     * Generic menu-owned secondary/tertiary/delete actions for contextual
     * panel commands such as copy, default, multi-select, and row delete.
     * Bound only on menu / pause IMCs. */
    ACTION_MENU_SECONDARY,       /* = 73 C / X */
    ACTION_MENU_TERTIARY,        /* = 74 D / Y */
    ACTION_MENU_DELETE,          /* = 75 Delete */

    /* ---- Observer / spectator controls (2026-04-28) ----
     * Bound on g_ImcObserver while LAYER_OBSERVER is active. The same
     * observer layer covers spectator and Forge, but these actions are
     * consumed only by the spectator overlay. */
    ACTION_OBSERVER_SUBSET_PREV,  /* = 76 PageUp / LB */
    ACTION_OBSERVER_SUBSET_NEXT,  /* = 77 PageDown / RB */
    ACTION_OBSERVER_MEMBER_PREV,  /* = 78 Left / D-pad left */
    ACTION_OBSERVER_MEMBER_NEXT,  /* = 79 Right / D-pad right */
    ACTION_OBSERVER_CAMERA_TOGGLE,/* = 80 Tab / R3 */
    ACTION_OBSERVER_FREEFLY,      /* = 81 R / Y hold */
    ACTION_OBSERVER_STOP,         /* = 82 Escape / B */
    ACTION_OBSERVER_ASCEND,       /* = 83 E / RT */
    ACTION_OBSERVER_DESCEND,      /* = 84 Q / LT */

    /* ---- Voice chat (2026-04-28) ----
     * Push-to-talk is a shared/system action. It is bound in the gameplay
     * and UI IMCs where the legacy raw V hotkey previously worked, then
     * pdgui_friends.cpp still gates it on ImGui keyboard capture so typing
     * into text fields does not start transmission. */
    ACTION_VOICE_PTT,             /* = 85 V hold */

    /* ---- Forge editor commands (2026-04-28) ---- */
    ACTION_FORGE_PLACE_CANCEL,     /* = 86 Escape while placing */
    ACTION_FORGE_BOT_ADD,          /* = 87 Insert */
    ACTION_FORGE_BOT_REMOVE_ALL,   /* = 88 Delete */
    ACTION_FORGE_BOT_FREEZE_TOGGLE,/* = 89 End */
    ACTION_FORGE_BOT_SPAWN_CYCLE,  /* = 90 Home */

    /* ---- Skin editor commands (2026-04-28) ---- */
    ACTION_SKIN_BRUSH_DECREASE,    /* = 91 [ */
    ACTION_SKIN_BRUSH_INCREASE,    /* = 92 ] */
    ACTION_SKIN_TOOL_DRAW,         /* = 93 1 */
    ACTION_SKIN_TOOL_ERASE,        /* = 94 2 */
    ACTION_SKIN_TOOL_FILL,         /* = 95 3 */
    ACTION_SKIN_TOOL_EYEDROPPER,   /* = 96 4 */
    ACTION_SKIN_TOOL_LINE,         /* = 97 5 */
    ACTION_SKIN_GRID_TOGGLE,       /* = 98 G */
    ACTION_SKIN_UV_TOGGLE,         /* = 99 U */
    ACTION_SKIN_UNDO,              /* = 100 Ctrl+Z */
    ACTION_SKIN_REDO,              /* = 101 Ctrl+Y / Ctrl+Shift+Z */
    ACTION_SKIN_SAVE,              /* = 102 Ctrl+S */

    /* ---- Test scenarios bidirectional cycler + visibility (S594h-A2, 2026-05-01) ----
     * ACTION_TESTSCEN_CYCLE_COUNT (= 70) is the FORWARD cycle (next-higher
     * bot count); kept for backward-compat with the prior single-direction
     * binding on KEY_0 / DPAD_DOWN. CYCLE_PREV is the new BACKWARD cycle
     * (next-lower count) on PgDn / DPAD_UP. VIS_TOGGLE cycles the player's
     * visibility-to-bots state through Normal -> AlwaysSee -> Invisible. */
    ACTION_TESTSCEN_CYCLE_PREV,    /* = 103 Pg Dn / DPAD_UP -- previous bot count */
    ACTION_TESTSCEN_VIS_TOGGLE,    /* = 104 I key -- cycle player visibility mode (V taken by VOICE_PTT) */

    /* ---- Menu skip-up / skip-down (Rule 8, 2026-05-03; renamed per Mike's
     * Q-A 2026-05-03 from SECTION_PREV/NEXT to SKIPUP/SKIPDOWN) ----
     * Bound only on menu / pause IMCs to gamepad LT/RT. Each screen
     * implements a dynamic walker (per Mike's Q-B: "Dynamic walker is
     * the only real choice as we have a fully dynamic system") that,
     * given the current focus, returns the next/previous skip target
     * within the focused panel. Per Mike's Q-C: page-jump within the
     * same panel is the fallback for flat lists with no groups; LT/RT
     * never crosses panels (D-pad does cross-panel; LT/RT stays within
     * the focused panel's scroll). Boundary case: stays on boundary,
     * does not wrap (mirrors Rule 1 D-pad U/D no-wrap). Idle on screens
     * with neither groups NOR scroll. Source:
     * menu-input-interaction-grammar.md Rule 8 + Q-A/Q-B/Q-C inversions. */
    ACTION_MENU_SKIPUP,            /* = 105 LT trigger (gamepad) -- skip up */
    ACTION_MENU_SKIPDOWN,          /* = 106 RT trigger (gamepad) -- skip down */

    /* ---- s036-02 / s036-03 (c036, 2026-05-12) ----
     * Dev hotkeys + tooling chords previously dispatched as raw SDL
     * handlers in pdgui_backend.cpp::pdguiProcessEvent and
     * gfx_sdl2.cpp::gfx_sdl_handle_events. Migrated to the actionmap
     * so the rebind UI sees them, ImGui textbox capture suppresses
     * them, and a single dispatch site (pdsched.c) drives the
     * handlers. PD_DEV_BUILD gating lives at the consumer site; the
     * action and binding exist unconditionally. */
    ACTION_DEBUG_BOT_FREEZE,        /* = 107 F6 -- toggle MP bot AI/movement freeze (DEV) */
    ACTION_DEBUG_INVINCIBILITY,     /* = 108 F7 -- toggle player invincibility (DEV) */
    ACTION_DEBUG_OVERLAY_TOGGLE,    /* = 109 F12 -- push/pop g_CtxDebugOverlay (DEV) */
    ACTION_DEBUG_MESH_TOGGLE,       /* = 110 F10 -- toggle mesh collision overlay */
    ACTION_DEBUG_CULL_MODE_CYCLE,   /* = 111 Shift+F1 -- cycle backface cull (none/back/front) */
    ACTION_DEBUG_TESTFIRE,          /* = 112 F2 (no mod) -- schedule one-shot test-fire pulse */
    ACTION_DEBUG_WIREFRAME_TOGGLE,  /* = 113 Shift+F2 -- toggle wireframe overlay */
    ACTION_HOTSWAP_TOGGLE,          /* = 114 F8 / RS-click -- flip hot-swap rendering mode */
    ACTION_TOGGLE_FULLSCREEN,       /* = 115 Alt+Enter -- toggle fullscreen window */

    /* ---- GPU swarm AI sub-method toggle (B-308 first slice, c3807, 2026-05-15) ----
     * Only meaningful inside a GPU swarm test scenario. Cycles the active
     * method between SWARM_METHOD_GPU_POS_ONLY (legacy seek-only) and
     * SWARM_METHOD_GPU_FULL (compute-side AI decisions). No-op in CPU
     * swarm or non-swarm contexts. Bound to KEY_O on keyboard (SDL scan 18)
     * to sit next to KEY_I (vis toggle). Gameplay-only via the default
     * classifier so it can't leak from menus. */
    ACTION_TESTSCEN_GPU_FULL_TOGGLE, /* = 116 O key -- toggle GPU swarm sub-method */

    /* ---- Vehicle controls expansion (Mike directive 2026-05-17) ----
     * Camera look (yaw/pitch) on RSTICK + mouse-delta so the driver can aim
     * around while mounted. Handbrake on Space / A-button. Vehicle USE is
     * the on-foot interact trigger that boards the nearest hoverbike (so the
     * legacy --debug-mount-bike CLI flag is no longer the only way in). */
    ACTION_VEHICLE_LOOK_X,           /* = 117 mouse-delta-x / RSTICK_X (yaw) */
    ACTION_VEHICLE_LOOK_Y,           /* = 118 mouse-delta-y / RSTICK_Y (pitch) */
    ACTION_VEHICLE_HANDBRAKE,        /* = 119 Space / A-button */
    ACTION_VEHICLE_USE,              /* = 120 E / A-button on-foot mount trigger */

    ACTION_COUNT                /* = 121, sentinel - keep last */
} InputAction;

/* Backward-compat aliases */
#define ACTION_INTERACT     ACTION_USE        /* A_BUTTON was ACTION_INTERACT, now ACTION_USE */
#define ACTION_MENU_ACCEPT  ACTION_USE        /* Consolidated: menu accept = gameplay use */
#define ACTION_MENU_CANCEL  ACTION_CANCEL_USE /* Consolidated: menu cancel = gameplay cancel */

/* Universal grammar v2 aliases (Q3, 2026-05-03). The grammar doc refers to
 * the per-rule semantic names; these aliases let source cite the rule by
 * name (Rule 5 = X = context menu; Rule 6 = Y = social) without a hard
 * rename of the underlying enum (which would touch tests + pure-C mirror).
 * Source: menu-input-interaction-grammar.md Rule 5 + Rule 6. */
#define ACTION_MENU_CONTEXT ACTION_MENU_SECONDARY /* Rule 5: X opens context menu */
#define ACTION_MENU_SOCIAL  ACTION_MENU_TERTIARY  /* Rule 6: Y opens social overlay */

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

/** Flush (zero) all per-player state for a declared action set.
 *
 *  This is for layer / transition boundaries that need to clear shared
 *  actions as well as gameplay-only actions. Example: cutscene entry clears
 *  ACTION_USE / ACTION_MENU_ACCEPT so a held Continue press cannot survive
 *  into cutscene skip handling.
 */
void actionmapFlushActionSet(const InputAction *actions, s32 action_count);

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
 * actionHoldProgress returns 0..1 fill while held; reaches 1 at threshold_ms,
 * then falls back to 0 after actionConsumeHold's short full-ring pin.
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

/** Hold fill in [0..1] toward `threshold_ms`.  0 when not held or already consumed. */
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
extern InputMappingContext g_ImcCutscene;      /* priority  4 — cutscene skip (Cohort 4, K.2)                         */
extern InputMappingContext g_ImcVehicle;       /* priority  5 — vehicle controls     */
extern InputMappingContext g_ImcForgeSession;  /* priority  6 — Forge session toggle (whole session) */
extern InputMappingContext g_ImcForge;         /* priority  7 — Forge editor overlay (FREEFLY only)  */
extern InputMappingContext g_ImcObserver;      /* priority  8 - spectator / observer overlay */
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

/** Activate / deactivate the Cutscene IMC (Cohort 4, K.2). Called from
 *  the LAYER_CUTSCENE on_push / on_pop hooks in inputlayer.c. Bindings
 *  cover ACTION_SKIP_CUTSCENE only (Space + Gamepad A). Pause-during-
 *  cutscene continues to fire ACTION_PAUSE on the gameplay/mission/CS
 *  IMCs as before; this IMC sits at priority 4 below vehicle (5) so a
 *  cutscene playing while a player is mounted still allows skip. */
void imcCutsceneEnter(void);
void imcCutsceneExit(void);

/* ============================================================
 * Smoke verify harness helpers (c115, 2026-05-14)
 *
 * These helpers exist so the smoke harness can drive the actionmap
 * directly, bypassing SDL events and ImGui's focus gate. They are
 * deterministic and focus-independent -- the right tool for steering
 * after the SDL window has lost focus to a firewall prompt or other
 * OS modal.
 *
 * Both calls are inert when the smoke harness is not active (gated
 * via smokeHarnessIsActive() in the implementation), so production
 * paths cannot accidentally mutate state through them.
 * ============================================================ */

/** Resolve a textual action name to an InputAction enum value.
 *  Accepts the full enum identifier ("ACTION_MENU_ACCEPT") as well as
 *  the CamelCase short form used in pd.ini keys ("MenuUp", "Use").
 *  Returns the action index on success, or -1 if the name is unknown.
 *  Backward-compat aliases (ACTION_INTERACT / ACTION_MENU_ACCEPT /
 *  ACTION_MENU_CANCEL / ACTION_MENU_CONTEXT / ACTION_MENU_SOCIAL)
 *  resolve to their canonical targets. */
s32 actionmapResolveByName(const char *name);

/** Inject a press (down=1) or release (down=0) edge on `action` for
 *  `player`, writing directly to the per-player ActionState. The
 *  smoke harness uses this to drive menu navigation deterministically
 *  even when the SDL window has lost focus. Returns 1 if the inject
 *  was applied, 0 if it was rejected (harness not active, out-of-range
 *  player, or out-of-range action). */
s32 actionmapInjectStateForSmoke(s32 player, s32 action, s32 down);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ACTIONMAP_H */
