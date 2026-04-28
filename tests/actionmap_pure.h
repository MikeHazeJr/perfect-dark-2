/*
 * actionmap_pure.h -- pure-C subset of port/src/actionmap.cpp for pd-tests.
 *
 * Mirrors the InputAction enum, ActionState struct, s_State backing array,
 * actionIsGameplayOnly classifier, and actionmap flush mutators.
 * No SDL, no IMC stack, no dispatch logic. The aim is to lock down the
 * flush invariants for gameplay-only state and declared transition action
 * sets.
 *
 * @SYNC port/include/actionmap.h (InputAction enum + ACTION_COUNT)
 * @SYNC port/src/actionmap.cpp (actionIsGameplayOnly,
 *       actionmapFlushGameplayState, actionmapFlushActionSet)
 *
 * Cohort 1 of context/designs/input-universality-and-transitions-2026-04-27.md.
 */

#ifndef _IN_ACTIONMAP_PURE_H
#define _IN_ACTIONMAP_PURE_H

#ifdef __cplusplus
extern "C" {
#endif

#define AMP_MAX_PLAYERS 4

typedef enum AmpInputAction {
    AMP_ACTION_MOVE_FORWARD = 0,
    AMP_ACTION_MOVE_BACKWARD,
    AMP_ACTION_MOVE_LEFT,
    AMP_ACTION_MOVE_RIGHT,
    AMP_ACTION_AXIS_MOVE_X,
    AMP_ACTION_AXIS_MOVE_Y,
    AMP_ACTION_AIM_UP,
    AMP_ACTION_AIM_DOWN,
    AMP_ACTION_AIM_LEFT,
    AMP_ACTION_AIM_RIGHT,
    AMP_ACTION_AXIS_AIM_X,
    AMP_ACTION_AXIS_AIM_Y,
    AMP_ACTION_CBUTTON_UP,
    AMP_ACTION_CBUTTON_DOWN,
    AMP_ACTION_CBUTTON_LEFT,
    AMP_ACTION_CBUTTON_RIGHT,
    AMP_ACTION_DPAD_UP,
    AMP_ACTION_DPAD_DOWN,
    AMP_ACTION_DPAD_LEFT,
    AMP_ACTION_DPAD_RIGHT,
    AMP_ACTION_FIRE_PRIMARY,
    AMP_ACTION_FIRE_SECONDARY,
    AMP_ACTION_FIRE_MODE,
    AMP_ACTION_RELOAD,
    AMP_ACTION_USE,
    AMP_ACTION_CANCEL_USE,
    AMP_ACTION_THROW_WEAPON,
    AMP_ACTION_CROUCH,
    AMP_ACTION_JUMP,
    AMP_ACTION_SPRINT,
    AMP_ACTION_ZOOM_IN,
    AMP_ACTION_ZOOM_OUT,
    AMP_ACTION_WEAPON_PREV,
    AMP_ACTION_WEAPON_NEXT,
    AMP_ACTION_WEAPON_1,
    AMP_ACTION_WEAPON_2,
    AMP_ACTION_WEAPON_3,
    AMP_ACTION_WEAPON_4,
    AMP_ACTION_WEAPON_5,
    AMP_ACTION_WEAPON_6,
    AMP_ACTION_VEHICLE_ACCELERATE,
    AMP_ACTION_VEHICLE_BRAKE,
    AMP_ACTION_VEHICLE_STEER_LEFT,
    AMP_ACTION_VEHICLE_STEER_RIGHT,
    AMP_ACTION_VEHICLE_EXIT,
    AMP_ACTION_MENU_UP,
    AMP_ACTION_MENU_DOWN,
    AMP_ACTION_MENU_LEFT,
    AMP_ACTION_MENU_RIGHT,
    AMP_ACTION_MENU_TAB_PREV,
    AMP_ACTION_MENU_TAB_NEXT,
    AMP_ACTION_PAUSE,
    AMP_ACTION_SCREENSHOT,
    AMP_ACTION_CONSOLE_TOGGLE,
    AMP_ACTION_DEBUG_TOGGLE,
    AMP_ACTION_CHEAT_ENTER,
    AMP_ACTION_SCORECARD,
    AMP_ACTION_FORGE_TOGGLE,
    AMP_ACTION_FORGE_ASCEND,
    AMP_ACTION_FORGE_DESCEND,
    AMP_ACTION_FORGE_BOOST,
    AMP_ACTION_FORGE_PRECISION,
    AMP_ACTION_FORGE_SIDEBAR_TOGGLE,
    AMP_ACTION_FORGE_SIDEBAR_UP,
    AMP_ACTION_FORGE_SIDEBAR_DOWN,
    AMP_ACTION_FORGE_SIDEBAR_ACTIVATE,
    AMP_ACTION_FORGE_TAB_PREV,
    AMP_ACTION_FORGE_TAB_NEXT,
    AMP_ACTION_SCORECARD_HOLD,
    AMP_ACTION_SOCIAL_TOGGLE,
    AMP_ACTION_TESTSCEN_CYCLE_COUNT,
    AMP_ACTION_TEXT_PASTE,
    AMP_ACTION_SKIP_CUTSCENE,
    AMP_ACTION_MENU_SECONDARY,
    AMP_ACTION_MENU_TERTIARY,
    AMP_ACTION_MENU_DELETE,
    AMP_ACTION_OBSERVER_SUBSET_PREV,
    AMP_ACTION_OBSERVER_SUBSET_NEXT,
    AMP_ACTION_OBSERVER_MEMBER_PREV,
    AMP_ACTION_OBSERVER_MEMBER_NEXT,
    AMP_ACTION_OBSERVER_CAMERA_TOGGLE,
    AMP_ACTION_OBSERVER_FREEFLY,
    AMP_ACTION_OBSERVER_STOP,
    AMP_ACTION_OBSERVER_ASCEND,
    AMP_ACTION_OBSERVER_DESCEND,
    AMP_ACTION_VOICE_PTT,
    AMP_ACTION_FORGE_PLACE_CANCEL,
    AMP_ACTION_FORGE_BOT_ADD,
    AMP_ACTION_FORGE_BOT_REMOVE_ALL,
    AMP_ACTION_FORGE_BOT_FREEZE_TOGGLE,
    AMP_ACTION_FORGE_BOT_SPAWN_CYCLE,
    AMP_ACTION_SKIN_BRUSH_DECREASE,
    AMP_ACTION_SKIN_BRUSH_INCREASE,
    AMP_ACTION_SKIN_TOOL_DRAW,
    AMP_ACTION_SKIN_TOOL_ERASE,
    AMP_ACTION_SKIN_TOOL_FILL,
    AMP_ACTION_SKIN_TOOL_EYEDROPPER,
    AMP_ACTION_SKIN_TOOL_LINE,
    AMP_ACTION_SKIN_GRID_TOGGLE,
    AMP_ACTION_SKIN_UV_TOGGLE,
    AMP_ACTION_SKIN_UNDO,
    AMP_ACTION_SKIN_REDO,
    AMP_ACTION_SKIN_SAVE,
    AMP_ACTION_COUNT
} AmpInputAction;

typedef struct {
    int   held;
    int   pressed;
    int   released;
    float value;
    unsigned down_time_ms;
    unsigned up_time_ms;
    int   hold_consumed;
} AmpActionState;

/* Reset every player's state to zero. */
void ampReset(void);

/* Mutators (test scaffolding). */
void ampSetHeld(int player, AmpInputAction a);
void ampSetReleased(int player, AmpInputAction a);
void ampSetPressed(int player, AmpInputAction a);

/* Read accessors (test inspection). */
const AmpActionState *ampGetState(int player, AmpInputAction a);

/* Mirrors port/src/actionmap.cpp:actionIsGameplayOnly. */
int ampIsGameplayOnly(AmpInputAction a);

/* Mirrors port/src/actionmap.cpp:actionmapFlushGameplayState. */
void ampFlushGameplayState(void);

/* Mirrors port/src/actionmap.cpp:actionmapFlushActionSet. */
void ampFlushActionSet(const AmpInputAction *actions, int action_count);

/* Mirrors port/src/inputlayer.c:g_LayerCutscene.action_set. */
const AmpInputAction *ampCutsceneActionSet(int *out_count);

#ifdef __cplusplus
}
#endif

#endif /* _IN_ACTIONMAP_PURE_H */
