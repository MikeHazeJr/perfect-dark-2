#include "gameplay_graph_values.h"
#include "gameplay_graph_integer.h"
#include "gameplay_graph_effect4.h"
#include "gameplay_graph_divshift.h"
#include <bit>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <cstring>

namespace {
constexpr uint32_t seal = 0x47564731;
constexpr ggv_type types[] = {
    GGV_U32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_U32,
    GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_BOOL,
    GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_I32, GGV_I32, GGV_F32, GGV_BOOL
    , GGV_U32, GGV_U32, GGV_I32, GGV_I32,
    GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL, GGV_BOOL,
    GGV_BOOL, GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL,
    GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_BOOL, GGV_BOOL, GGV_F32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_REF, GGV_REF, GGV_BOOL, GGV_F32,
    GGV_BOOL, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL,
    GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_F32, GGV_F32, GGV_BOOL, GGV_F32, GGV_F32, GGV_BOOL, GGV_F32, GGV_F32, GGV_F32, GGV_BOOL, GGV_I32, GGV_BOOL, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_REF, GGV_REF, GGV_I32, GGV_BOOL, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF,
    GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL,
    GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_REF, GGV_U32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_BOOL, GGV_REF, GGV_I32, GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_REF,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_REF, GGV_REF, GGV_U32, GGV_U32, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_U32, GGV_I32, GGV_REF, GGV_REF, GGV_U32, GGV_REF, GGV_U32, GGV_I32,
    GGV_I32, GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_I32, GGV_U32, GGV_I32, GGV_BOOL, GGV_BOOL, GGV_I32, GGV_I32, GGV_U32,
    GGV_I32, GGV_I32, GGV_U32, GGV_I32, GGV_I32, GGV_U32, GGV_U32, GGV_BOOL, GGV_BOOL,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_U32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    // Reload
    GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_REF, GGV_U32, GGV_BOOL, GGV_BOOL, GGV_I32,
    GGV_U32, GGV_U32, GGV_I32, GGV_I32, GGV_BOOL, GGV_U32, GGV_I32, GGV_BOOL, GGV_I32, GGV_I32, GGV_BOOL,
    GGV_I32, GGV_I32,
    // ChangeFunc, SetArmPitch, IsLoaded
    GGV_I32, GGV_F32, GGV_REF, GGV_F32, GGV_F32, GGV_F32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32,
    GGV_REF, GGV_I64, GGV_F32,
    GGV_I32, GGV_REF, GGV_I32, GGV_I32, GGV_I64, GGV_I32, GGV_F32,
    GGV_REF, GGV_REF, GGV_REF, GGV_U32,
    GGV_BOOL, GGV_U32, GGV_BOOL, GGV_BOOL, GGV_I32, GGV_I32, GGV_I32,
    GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_U32, GGV_BOOL, GGV_BOOL,
    GGV_BOOL, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL,
    GGV_I32, GGV_I32,
    GGV_I32, GGV_REF, GGV_REF, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_REF,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_U32, GGV_U32, GGV_U32, GGV_BOOL, GGV_BOOL, GGV_BOOL,
    GGV_I32, GGV_BOOL, GGV_BOOL, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_REF, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_BOOL, GGV_I32,
    GGV_BOOL, GGV_F32, GGV_F32, GGV_I32, GGV_I32, GGV_U32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL, GGV_F32,
    GGV_REF, GGV_I32, GGV_I32, GGV_REF, GGV_REF, GGV_BOOL, GGV_REF, GGV_I32, GGV_REF, GGV_U32,
    GGV_I32, GGV_I32, GGV_REF, GGV_I32, GGV_I32,
    GGV_I32, GGV_BOOL, GGV_F32, GGV_F32, GGV_F32, GGV_U32, GGV_U32, GGV_I32, GGV_I32, GGV_REF,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL, GGV_F32, GGV_I32, GGV_I32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_I32, GGV_I32, GGV_I32, GGV_REF, GGV_REF, GGV_BOOL, GGV_U32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_U32, GGV_I32, GGV_I32, GGV_F32, GGV_F32, GGV_F32, GGV_F32, GGV_REF, GGV_REF, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_U32, GGV_I32, GGV_U32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32, GGV_BOOL, GGV_BOOL, GGV_I32, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_BOOL,
    GGV_BOOL,
    GGV_I32, GGV_I32, GGV_U32, GGV_REF, GGV_REF, GGV_I32, GGV_I32, GGV_I32, GGV_REF, GGV_BOOL,
    GGV_I32, GGV_I32, GGV_I32, GGV_I32, GGV_REF,
    GGV_REF,
    GGV_REF, GGV_U32,
    GGV_BOOL, GGV_U32,
    GGV_REF, GGV_REF, GGV_REF, GGV_I32, GGV_I32, GGV_U32, GGV_I32,
    GGV_I32, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_REF, GGV_I32, GGV_REF, GGV_U32, GGV_I32,
    GGV_I32, GGV_BOOL,
    GGV_REF, GGV_REF, GGV_REF, GGV_U32, GGV_U32, GGV_U32,
    GGV_I32, GGV_BOOL, GGV_I32, GGV_REF,
    GGV_REF, GGV_I32, GGV_I32, GGV_I64, GGV_I64, GGV_I32,
};
static_assert(sizeof(types)/sizeof(types[0]) == GGV_BINDING_COUNT);
constexpr bool writable(ggv_binding b) {
    return b == GGV_GL_TEXTURE_MODEL || b == GGV_GL_TEXTURE_INDEX || b == GGV_GL_TEXTURE_MARK || b == GGV_GL_LOAD_TYPE || b == GGV_GL_RETAINED_MODEL || b == GGV_GL_TARGET_MODEL || b == GGV_GL_NEXT_TEXTURE || b == GGV_GL_LOADED_SIZE || b == GGV_GL_ALLOC_SIZE || b == GGV_PARENT_LAST_KEY || b == GGV_PARENT_DIAG_HANDS || b == GGV_MASTER_HAS_HANDS_INPUT || b == GGV_FINAL_HAND_INDEX || b == GGV_FINAL_GUN_COMMAND || b == GGV_FINAL_HAND_COMMAND || b == GGV_FINAL_MATRIX_STATE || b == GGV_FINAL_MATRIX_PTR || b == GGV_FINAL_SUM || b == GGV_CART_FUNC || b == GGV_CART_WEAPON || b == GGV_CART_AMMO || b == GGV_MASTER_MEM_PTR || b == GGV_MASTER_MEM_REMAINING || b == GGV_MASTER_CART_MODEL || b == GGV_MASTER_WEAPON || b == GGV_MASTER_HF32 || b == GGV_MASTER_HANDFILE || b == GGV_MASTER_FILE
        || b == GGV_FLUX_FILE || b == GGV_FLUX_CATALOG_HEAD || b == GGV_FLUX_GENERATION || b == GGV_FLUX_HAND_MODEL || b == GGV_FLUX_LOAD_PTR || b == GGV_FLUX_LOAD_REMAINING || b == GGV_FLUX_CASING_INDEX || b == GGV_FLUX_CASING_MODEL || b == GGV_FLUX_CASINGS_ACTIVE
        || b == GGV_LOAD_LOCKSCREEN || b == GGV_MEM_LAST_PAIR || b == GGV_MEM_LAST_WILD || b == GGV_MEM_LAST_OWNER || b == GGV_MEM_WILD_VALUE || b == GGV_MEM_LOAD_ALL || b == GGV_LOADED_GUNMEMOWNER || b == GGV_LOADED_GUNMEMTYPE
        || b == GGV_MEM_CURRENT_OWNER || b == GGV_MEM_GUNLOADSTATE || b == GGV_MEM_LOCK_TIMER || b == GGV_LOADED_GUNMEMNEW || b == GGV_LOADED_MASTERLOADSTATE
        || b == GGV_EQUIP_REQUEST || b == GGV_EQUIP_DIAG_SHORTCUT || b == GGV_EQUIP_WANT_AMMO || b == GGV_EQUIP_CURRENT_DUAL || b == GGV_INIT_HAND || b == GGV_INIT_GUN_ANIM || b == GGV_INIT_HAND_ANIM
        || b == GGV_CLEAN_RELOAD_INDEX || b == GGV_CLEAN_ROUNDS || b == GGV_CLEAN_RESERVE || b == GGV_VIS_CLIP_SIZE || b == GGV_VIS_LOADED || b == GGV_CONFIG_CTRL_AMMO || b == GGV_CONFIG_UPGRADE0 || b == GGV_CONFIG_UPGRADE1 || b == GGV_CONFIG_FINAL0 || b == GGV_CONFIG_FINAL1 || b == GGV_CONFIG_LASTMAG || b == GGV_ABMAG_LOADED || b == GGV_ABMAG_CHANGE || b == GGV_ABMAG_REF || b == GGV_ABMAG_TIMER
        || b == GGV_SW_PENDING || b == GGV_SW_DIAG_RIGHT || b == GGV_SW_DIAG_LEFT || b == GGV_SW_WEAPON || b == GGV_SW_LEFT_INUSE || b == GGV_SW_RIGHT_INUSE || b == GGV_SW_DUAL || b == GGV_SW_PREV_WEAPON || b == GGV_SW_PREV_DUAL || b == GGV_SW_CURRENT_AIM || b == GGV_SW_RESERVE_TARGET || b == GGV_SW_ZOOM_MAX || b == GGV_SW_ZOOM_TIME || b == GGV_SW_FOV_NEW || b == GGV_SW_FOV || b == GGV_SW_CURRENT_INVERT || b == GGV_SW_USE_TIME || b == GGV_SW_FADER || b == GGV_SW_CHR || b == GGV_SW_CURRENT_DISGUISED || b == GGV_SW_CHR_HIDDEN || b == GGV_SW_CURRENT_STRING || b == GGV_SW_STRING_TIMER || b == GGV_SW_THROWING
        || b == GGV_SWITCH_RESET_BIT || b == GGV_SWITCH_MATMOT3 || b == GGV_SWITCH_ANGLE_DAMPER
        || b == GGV_SWITCH_SMOKE_POINT || b == GGV_SWITCH_HAND_UPGRADES || b == GGV_SWITCH_HAND_WEAPON
        || b == GGV_CHANGE_EJECT_TYPE || b == GGV_ANIM_GANGSTA || b == GGV_VIS_CURSOR || b == GGV_VIS_WEAPON || b == GGV_VIS_AMMO_SLOT || b == GGV_SPOOL || b == GGV_STATE_FRAMES || b == GGV_SHOTS
        || b == GGV_REMAINDER || b == GGV_FIRING || b == GGV_FUNCTION_FLAGS
        || b == GGV_AMMO_INDEX || b == GGV_FUNCTION_TYPE || b == GGV_AUTOMATIC
        || b == GGV_LEGACY_AUTOMATIC || b == GGV_ACCELERATION || b == GGV_DECELERATION
        || b == GGV_BURST_BULLETS || b == GGV_FLASH || b == GGV_LOAD_SLIDE
        || b == GGV_STATE_VAR1 || b == GGV_STATE_FLAGS || b == GGV_LOADED_AMMO
        || b == GGV_ATTACK_TYPE || b == GGV_ALLOW_SHOOT_FRAME || b == GGV_LAST_SHOOT_FRAME
        || b == GGV_CURRENT_HAND_SLIDEINC
        || b == GGV_ROTX_START || b == GGV_ROTX_END || b == GGV_ROTX_OFFSET || b == GGV_USE_POSROT || b == GGV_STATE_MINOR || b == GGV_ANIMATION_LATCH || b == GGV_MATMOT2 || b == GGV_MATMOT1 || b == GGV_POS_START_X || b == GGV_POS_START_Y || b == GGV_POS_START_Z || b == GGV_POS_END_X || b == GGV_POS_END_Y || b == GGV_POS_END_Z || b == GGV_POS_OFFSET_X || b == GGV_POS_OFFSET_Y || b == GGV_POS_OFFSET_Z || b == GGV_PRIME_TIMER || b == GGV_CURRENT_PLAYER_THROWING || b == GGV_EMPTY_MODE || b == GGV_EMPTY_COUNT60 || b == GGV_EMPTY_COUNT || b == GGV_WEAPON_FUNCTION || b == GGV_STATE_VALUE || b == GGV_STATE_CYCLES || b == GGV_STATE_LAST_FRAME || b == GGV_STATE_LOG_WEAPON || b == GGV_STATE_LOG_INUSE || b == GGV_ANIM_LOAD || b == GGV_EMPTY_ANIM_MODE || b == GGV_ANIM_INCREMENTAL || b == GGV_ANIM_COMMAND || b == GGV_ANIM_LOOP_COUNT || b == GGV_ANIM_BIT02 || b == GGV_ANIM_BIT07 || b == GGV_TRIGGER || b == GGV_TRIGGER_PREVIOUS || b == GGV_TRIGGER_RELEASED || b == GGV_AMMO_HAND_INDEX || b == GGV_AMMO_SLOT_INDEX || b == GGV_QUERY_CACHED_AMMO || b == GGV_RELOAD_INDEX || b == GGV_RELOAD_RESERVE || b == GGV_RELOAD_RESERVE_CLEAR || b == GGV_COUNTDOWN_HAND_INDEX || b == GGV_COUNTDOWN_SLOT_INDEX || b == GGV_COUNTDOWN_SPENT || b == GGV_QUANTITY_FUNCTION || b == GGV_QUANTITY_LOADED_RIGHT || b == GGV_QUANTITY_RESERVE
        || b == GGV_RELOAD_GS_INT1 || b == GGV_RELOAD_GS_INT2 || b == GGV_RELOAD_UNK0D0E_07
        || b == GGV_RELOAD_INCREMENTAL || b == GGV_RELOAD_EJECT_STATE || b == GGV_RELOAD_COUNT60
        || b == GGV_RELOAD_COUNT || b == GGV_RELOAD_MODE || b == GGV_RELOAD_PAUSETIME60
        || b == GGV_RELOAD_UNK0CC8_02 || b == GGV_RELOAD_DOAUTOSELECT || b == GGV_RIGHT_HAND_UNK0DD4
        || b == GGV_ARM_PITCH_M30 || b == GGV_ARM_PITCH_M31 || b == GGV_ARM_PITCH_M32
        || b == GGV_ANIM_FRAME_RESULT || b == GGV_ANIM_WAIT_CURSOR || b == GGV_ANIM_START_PREVIOUS
        || b == GGV_ANIM_WAIT_INCREMENT || b == GGV_ANIM_WAIT_INCREMENT_REAL || b == GGV_ANIM_TICK_INCREMENT_LEGACY
        || b == GGV_HAND_LASTDIRVALID || b == GGV_HAND_MODE_NEXT
        || b == GGV_HAND_AUTO_BIT07 || b == GGV_HAND_AUTO_BIT08 || b == GGV_HAND_ACTIVATE_SECONDARY
        || b == GGV_AUTO_OTHER_BIT08 || b == GGV_INFO_WEAPON_RESULT
        || b == GGV_INFO_DEFINITION_RESULT || b == GGV_INFO_GUNCTRL
        || b == GGV_FREE_LAST_RIGHT || b == GGV_FREE_LAST_LEFT || b == GGV_HELD_ROCKET || b == GGV_FUNCTION_BYTE_INDEX || b == GGV_FUNCTION_NATIVE_BITS || b == GGV_FUNCTION_CUSTOM_BITS || b == GGV_FUNCTION_CONTROL_INVERT || b == GGV_RELOAD_DEVICES_ACTIVE || b == GGV_SELECT_INDEX || b == GGV_SWITCH_DUALWIELD || b == GGV_GUNCTRL_WANTAMMO || b == GGV_CHANGE_DIAG_LOADED || b == GGV_CHANGE_DIAG_SWITCHTO || b == GGV_CHANGE_DIAG_GUNMEM || b == GGV_CHANGE_DIAG_OTHER || b == GGV_CHANGE_DIAG_RESULT || b == GGV_COMMAND_OUTPUT || b == GGV_PART_COUNT || b == GGV_PART_INDEX || b == GGV_PART_NUMBER || b == GGV_PART_FRAME || b == GGV_PART_VISIBLE || b == GGV_COMMAND_FROM_FLOAT || b == GGV_COMMAND_TO_FLOAT || b == GGV_COMMAND_FROM_INT || b == GGV_COMMAND_TO_INT;
}
struct Signature { int count, result; ggv_type args[3]; };
constexpr Signature signature(ggv_service s) {
    switch(s) {
    case GGV_CHANGE_UNEQUIP_ANIM: case GGV_CHANGE_EQUIP_ANIM: return {1,GGV_REF,{GGV_REF}};
    case GGV_CHANGE_COMPLETE: return {0,GGV_I32,{}};
    case GGV_COMMAND_SOUND_MODERN: return {2,GGV_REF,{GGV_I64,GGV_F32}};
    case GGV_COMMAND_SOUND_LEGACY: return {1,GGV_REF,{GGV_I64}};
    case GGV_PART_RESERVE: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_COMMAND_ACCUMULATE_PART: return {0,GGV_BOOL,{}};
    case GGV_COMMAND_SET_FRAME: return {1,-1,{GGV_F32}};
    case GGV_MASTER_CHOOSE_BODY: case GGV_MASTER_HAND_FILE: case GGV_MASTER_HAND_SOURCE: case GGV_MASTER_SOURCE_CURRENT: return {0,GGV_BOOL,{}};
    case GGV_GL_TEXTURE_BEGIN: case GGV_GL_TEXTURE_LOAD: case GGV_GL_TEXTURE_DONE: return {0,-1,{}};
    case GGV_GL_TEXTURE_AT: return {1,-1,{GGV_I32}};
    case GGV_GL_TEXTURE_UNCOMPRESS: return {2,-1,{GGV_I32,GGV_I32}};
    case GGV_GL_CAN_USE_HANDLE: case GGV_GL_EXTERNAL_SOURCE: return {0,GGV_BOOL,{}};
    case GGV_GL_CATALOG_MODEL: case GGV_GL_FILE_PROVIDER: return {0,GGV_REF,{}};
    case GGV_GL_COPY_HAND_ID: case GGV_GL_COPY_HAND_HANDLE: return {0,-1,{}};
    case GGV_GL_GENERATION: case GGV_GL_GENERATION_ID: case GGV_GL_GENERATION_HASH: return {1,GGV_REF,{GGV_REF}};
    case GGV_GL_COMPARE_ID: return {1,GGV_I32,{GGV_REF}};
    case GGV_GL_FAIL: return {0,-1,{}};
    case GGV_GL_ALLOCATE_RW: return {1,-1,{GGV_REF}};
    case GGV_GL_RETAINED_LOG: return {2,-1,{GGV_REF,GGV_REF}};
    case GGV_GUN_FAILURE_LOG: return {1,-1,{GGV_U32}};
    case GGV_PARENT_READY: case GGV_PARENT_PREPARE: case GGV_PARENT_HANDS: case GGV_PARENT_GUN: case GGV_PARENT_CARTS: return {0,GGV_BOOL,{}};
    case GGV_PARENT_FLUX: case GGV_PARENT_FINALIZE: case GGV_PARENT_ENTRY_LOG: case GGV_PARENT_SHORTCUT_LOG: return {0,-1,{}};
    case GGV_FINAL_MODEL_INIT: return {3,-1,{GGV_REF,GGV_REF,GGV_REF}};
    case GGV_FINAL_COMMANDS: return {3,GGV_I32,{GGV_REF,GGV_REF,GGV_REF}};
    case GGV_FINAL_PTR_OFFSET: return {2,GGV_REF,{GGV_REF,GGV_I32}};
    case GGV_FINAL_LOG: return {0,-1,{}};
    case GGV_CART_FUNCTION: case GGV_CART_FIND_WEAPON: case GGV_CART_AMMO_REF: return {1,GGV_REF,{GGV_I32}};
    case GGV_CART_FILE: return {1,GGV_I32,{GGV_I32}};
    case GGV_CART_QUEUE: return {0,-1,{}};
    case GGV_MASTER_COMPARE_LOAD_ID: return {0,GGV_I32,{}};
    case GGV_MASTER_SAME_LOAD_HANDLE: case GGV_MASTER_QUEUE_HAND: return {0,GGV_BOOL,{}};
    case GGV_MASTER_CATALOG_GENERATION: case GGV_MASTER_CAPACITY: return {0,GGV_U32,{}};
    case GGV_MASTER_MEMORY: return {0,GGV_REF,{}};
    case GGV_MASTER_CLEAR_HAND_HANDLE: case GGV_MASTER_HANDS_LOG: return {0,-1,{}};
    case GGV_MASTER_QUEUE_BODY: return {0,GGV_BOOL,{}};
    case GGV_MASTER_QUEUE_RETAINED: return {1,GGV_BOOL,{GGV_REF}};
    case GGV_MASTER_QUEUE_STATIC: case GGV_MASTER_GUN_TICK: case GGV_MASTER_GUN_LOG: return {0,-1,{}};
    case GGV_MASTER_FLUX_LOG: return {0,-1,{}};
    case GGV_MASTER_SOURCE_ERROR: return {0,-1,{}};
    case GGV_MASTER_WEAPON_FILE: return {1,GGV_I32,{GGV_I32}};
    case GGV_MASTER_RUNTIME_MODEL: return {1,GGV_REF,{GGV_I32}};
    case GGV_MASTER_HAS_HANDS: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_FLUX_LOG: case GGV_FLUX_CLEAR_HANDLE: return {0,-1,{}};
    case GGV_MEM_CHANGE_OWNER: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_MEM_OWNER_LOG_ENTER: case GGV_MEM_OWNER_LOG_WILD: case GGV_MEM_OWNER_LOG_SUCCESS: case GGV_MEM_ENTER_FLUX: case GGV_MEM_REMOVE_BODY: return {0,-1,{}};
    case GGV_MEM_FREE_LOG: case GGV_MEM_SET_LOG_ENTER: case GGV_MEM_SET_LOG_EXIT: case GGV_MEM_TEXTURE_RESET: case GGV_MEM_MASTER_TICK: return {0,-1,{}};
    case GGV_EQUIP_LOG: case GGV_EQUIP2_LOG_ENTER: case GGV_EQUIP2_LOG_EXIT: case GGV_INIT_LOG: return {0,-1,{}};
    case GGV_EQUIP_CHILD: return {1,-1,{GGV_I32}};
    case GGV_INIT_PLAYER_BEGIN: case GGV_INIT_ANIM: return {1,-1,{GGV_REF}};
    case GGV_INIT_HAND_REF: return {1,GGV_REF,{GGV_I32}};
    case GGV_CLEAN_DELETE: case GGV_CLEAN_ROCKET: case GGV_CLEAN_RETIRE: return {1,-1,{GGV_I32}};
    case GGV_CLEAN_LOG_ENTER: case GGV_CLEAN_LOG_EXIT: return {0,-1,{}};
    case GGV_CONFIG_RESET_ABMAG: return {1,-1,{GGV_REF}};
    case GGV_SW_CAN_FREE: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_SW_DEVICE_STATE: return {1,GGV_I32,{GGV_I32}};
    case GGV_SW_HAS_DOUBLE: return {2,GGV_BOOL,{GGV_I32,GGV_I32}};
    case GGV_SW_FREE: case GGV_SW_SET_MEMORY: case GGV_SW_RESET_HAND: case GGV_SW_REMOVE_ITEM: return {1,-1,{GGV_I32}};
    case GGV_SW_CLEANUP: case GGV_SW_INVENTORY_INDEX: case GGV_SW_DISGUISE_SOUND:
    case GGV_SW_CYCLE_BACK: case GGV_SW_LOG: return {0,-1,{}};
    case GGV_SWITCH_CONFIGURE_HAND: return {1,-1,{GGV_I32}};
    case GGV_SWITCH_INIT_ANIM: return {0,-1,{}};
    case GGV_VIS_TEST: return {1,GGV_BOOL,{GGV_REF}};
    case GGV_VIS_NEXT: return {1,GGV_REF,{GGV_REF}};
    case GGV_VIS_MODEL_PART: return {2,GGV_REF,{GGV_REF,GGV_I32}};
    case GGV_VIS_SAVE_HAND: case GGV_VIS_SAVE_GUN: return {2,-1,{GGV_REF,GGV_BOOL}};
    case GGV_VIS_RUN_COMMANDS: return {1,-1,{GGV_REF}};
    case GGV_COMMAND_SET_PART: return {2,-1,{GGV_I32,GGV_BOOL}};
    case GGV_COMMAND_MODEL_ANIMATION: return {2,-1,{GGV_I32,GGV_F32}};
    case GGV_COMMAND_PLAY_SPEED: return {1,-1,{GGV_F32}};
    case GGV_COMMAND_TICK_QUARTER: case GGV_COMMAND_TICK_LEGACY: return {1,-1,{GGV_I32}};
    case GGV_COMMAND_FIRST_PASS: return {0,GGV_BOOL,{}};
    case GGV_COMMAND_SOUND_PASS: return {0,-1,{}};
    case GGV_CHANGE_LOG_LOAD: case GGV_CHANGE_CREATE_WEAPON:
    case GGV_CHANGE_ACTIVATE: case GGV_CHANGE_EQUIP_SOUND: return {0,-1,{}};
    case GGV_RESOLVE_GRAPH: return {0,GGV_BOOL,{}};
    case GGV_OVERRIDE_ON: case GGV_OVERRIDE_OFF: case GGV_START_SLIDE: case GGV_RUMBLE:
        return {0,-1,{}};
    case GGV_FIRESLOT_DURATION: case GGV_SINGLE_SOUND: case GGV_CURRENT_PRIORITY:
    case GGV_AUDIO_PRIORITY: return {0,GGV_I32,{}};
    case GGV_SET_PRIORITY: return {1,-1,{GGV_I32}};
    case GGV_SOUND_STATE: return {1,GGV_I32,{GGV_REF}};
    case GGV_SOUND_STOP: return {1,-1,{GGV_REF}};
    case GGV_SOUND_START: return {2,GGV_REF,{GGV_I32,GGV_I32}};
    case GGV_SOUND_EVENT: return {2,-1,{GGV_REF,GGV_I32}};
    case GGV_GET_PADS: return {0,-1,{}};
    case GGV_CONTROL_MODE: case GGV_PAK_TYPE:
        return s==GGV_CONTROL_MODE?Signature{0,GGV_I32,{}}:Signature{1,GGV_I32,{GGV_I32}};
    case GGV_WEAPON_DUALWIELD: return {0,GGV_BOOL,{}};
    case GGV_RECOIL_TICKS: return {1,GGV_I32,{GGV_I32}};
    case GGV_RECOIL_SIN: case GGV_RECOIL_COS: return {1,GGV_F32,{GGV_F32}};
    case GGV_RECOIL_TWEEN: return {3,GGV_F32,{GGV_F32,GGV_F32,GGV_F32}};
    case GGV_RECOIL_AIM_X: return {0,GGV_F32,{}};
    case GGV_CAPTURE_DEFINITION: case GGV_OVERRIDE_GKEF: case GGV_ATTACK_COMMIT: return {0,-1,{}};
    case GGV_MATRIX_X: return {2,-1,{GGV_F32,GGV_REF}};
    case GGV_MATRIX_TRANSLATION: return {2,-1,{GGV_REF,GGV_REF}};
    case GGV_MATRIX_IDENTITY: return {1,-1,{GGV_REF}};
    case GGV_RESOLVE_FUNCTION: case GGV_ATTACK_RECOVERY: return {0,GGV_BOOL,{}};
    case GGV_FIRE_ANIMATION_START: return {3,-1,{GGV_REF,GGV_I32,GGV_REF}};
    case GGV_ATTACK_READY: return {2,GGV_BOOL,{GGV_REF,GGV_I32}};
    case GGV_ATTACK_CONTROL: return {0,GGV_I32,{}};
    case GGV_VIBRATION_MULTIPLY: return {3,-1,{GGV_REF,GGV_REF,GGV_REF}};
    case GGV_THROW_REMOVE_ITEM: return {1,-1,{GGV_I32}};
    case GGV_THROW_SWITCH_PREVIOUS: case GGV_THROW_AUTO_SWITCH: return {0,-1,{}};
    case GGV_THROW_RESET_ANIMATION: return {1,-1,{GGV_REF}};
    case GGV_THROW_SECONDARY_VALUE: return {0,GGV_I32,{}}; // Native BOOL32 compared exactly with true1.
    case GGV_MELEE_MIN_CLIP: return {2,GGV_I32,{GGV_I32,GGV_I32}};
    case GGV_MELEE_CLOCK: return {0,GGV_F32,{}};
    case GGV_MELEE_CHEAT_ACTIVE: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_MELEE_ANIM_BUSY: return {1,GGV_BOOL,{GGV_REF}};
    case GGV_EMPTY_RESET_ANIM: return {1,-1,{GGV_REF}};
    case GGV_EMPTY_SOUND_START: return {1,GGV_REF,{GGV_I32}};
    case GGV_EMPTY_SET_STATE: return {2,GGV_BOOL,{GGV_I32,GGV_I32}};
    case GGV_STATE_SELECT_HAND: return {0,-1,{}};
    case GGV_STATE_ALTERNATE_FUNCTION: return {1,GGV_REF,{GGV_I32}};
    case GGV_STATE_LOG: return {3,-1,{GGV_I32,GGV_I32,GGV_BOOL}};
    case GGV_DISPATCH_SHOOT: case GGV_DISPATCH_THROW: case GGV_DISPATCH_MELEE: case GGV_DISPATCH_SPECIAL: return {0,GGV_BOOL,{}};
    case GGV_AMMO_SELECT_PLAYER: return {0,-1,{}};
    case GGV_AMMO_HAS_FLAG: return {3,GGV_BOOL,{GGV_I32,GGV_I32,GGV_U32}};
    case GGV_AMMO_TOTAL_COUNT: return {1,GGV_I32,{GGV_I32}};
    case GGV_QUERY_RESOLVE_FUNCTION: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_QUERY_FIND_WEAPON: return {1,GGV_REF,{GGV_I32}};
    case GGV_QUERY_RESERVED_COUNT: return {1,GGV_I32,{GGV_I32}};
    case GGV_RELOAD_INDEX_FOR_WEAPON: return {1,GGV_I32,{GGV_I32}};
    case GGV_SVC_RELOAD_TRANSFER: return {1,-1,{GGV_I32}};
    case GGV_QUANTITY_CURRENT_WEAPON: return {1,GGV_I32,{GGV_I32}};
    case GGV_QUANTITY_AMMO_TYPE: return {2,GGV_U32,{GGV_U32,GGV_U32}};
    case GGV_QUANTITY_APPLY: return {2,-1,{GGV_I32,GGV_I32}};
    case GGV_RELOAD_CAPTURE_FUNCTION: return {0,GGV_REF,{}};
    case GGV_RELOAD_SELECT_OTHER_HAND: return {1,-1,{GGV_I32}};
    case GGV_HAND_START_ANIMATION: return {3,-1,{GGV_REF,GGV_I32,GGV_REF}};
    case GGV_HAND_ANIM_A44: return {2,GGV_BOOL,{GGV_REF,GGV_I32}};
    case GGV_RELOAD_CHILD_TRANSFER: return {3,-1,{GGV_I32,GGV_I32,GGV_I32}};
    case GGV_RELOAD_AMMO_STATUS: return {1,GGV_I32,{GGV_I32}};
    case GGV_RELOAD_SET_ARM_PITCH: return {1,-1,{GGV_F32}};
    case GGV_RELOAD_IS_LOADED: return {0,GGV_BOOL,{}};
    case GGV_RELOAD_SOUND_START: return {0,-1,{}};
    case GGV_RELOAD_FILENUM2: return {1,GGV_I32,{GGV_I32}};
    case GGV_RELOAD_WEAPON_HAS_FLAG: return {2,GGV_BOOL,{GGV_I32,GGV_U32}};
    case GGV_CHANGEFUNC_PRI_TO_SEC_ANIM: case GGV_CHANGEFUNC_SEC_TO_PRI_ANIM: return {0,GGV_REF,{}};
    case GGV_ARM_PITCH_X_ROTATION: return {2,-1,{GGV_F32,GGV_REF}};
    case GGV_ARM_PITCH_COSF: case GGV_ARM_PITCH_SINF: return {1,GGV_F32,{GGV_F32}};
    case GGV_MODEL_ANIM_FRAME_COUNT: return {0,GGV_I32,{}};
    case GGV_MODEL_ANIM_CURRENT_FRAME: case GGV_ANIM_WAIT_FRAME: return {0,GGV_F32,{}};
    case GGV_ANIM_CURSOR_NEXT: return {1,GGV_REF,{GGV_REF}};
    case GGV_ANIM_START_NAME: case GGV_ANIM_START_SOURCE: return {1,GGV_REF,{GGV_REF}};
    case GGV_ANIM_START_LOG_NULL: return {0,-1,{}};
    case GGV_ANIM_START_LOG_SOURCE: return {2,-1,{GGV_REF,GGV_REF}};
    case GGV_RNG_U32: return {0,GGV_U32,{}};
    case GGV_ANIM_START_ELIGIBILITY: return {1,GGV_BOOL,{GGV_REF}};
    case GGV_TICK_DELIVERY_PENDING: case GGV_TICK_IS_V2: return {0,GGV_BOOL,{}};
    case GGV_TICK_ADVANCE_V2: case GGV_TICK_IDLE: case GGV_TICK_RELOAD:
    case GGV_TICK_ATTACK: case GGV_TICK_CHANGE_GUN: case GGV_TICK_EMPTY:
    case GGV_TICK_AUTO_SWITCH: case GGV_TICK_CHANGE_FUNC: return {0,GGV_I32,{}};
    case GGV_HAND_INFO_CAPTURE: return {0,-1,{}};
    case GGV_MODEL_ABS_ANIM_SPEED: return {0,GGV_F32,{}};
    case GGV_HAND_TICK_CHILD: return {1,GGV_I32,{GGV_I32}};
    case GGV_HAND_CURRENT_FUNCTION: return {0,GGV_I32,{}};
    case GGV_HAND_READY_SWITCH: return {0,GGV_BOOL,{}};
    case GGV_HAND_DELETE_WEAPON: case GGV_HAND_FREE_ROCKET:
    case GGV_AUTO_CAPTURE_OTHER: case GGV_AUTO_SWITCH_WEAPON: return {0,-1,{}};
    case GGV_AUTO_OTHER_AMMO_STATUS: return {1,GGV_I32,{GGV_I32}};
    case GGV_AUTO_OTHER_ELIGIBILITY: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_HAND_SET_STATE: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_IDLE_V2: return {0,GGV_BOOL,{}};
    case GGV_IDLE_STORED_FUNCTION: return {0,GGV_I32,{}};
    case GGV_IDLE_ELIGIBILITY: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_IDLE_CAPTURE_FUNCTION: return {1,-1,{GGV_I32}};
    case GGV_IDLE_CAPTURE_OTHER: return {0,-1,{}};
    case GGV_RANGE_SLOT: return {0,GGV_I32,{}};
    case GGV_RANGE_WEAPON: return {1,GGV_I32,{GGV_I32}};
    case GGV_STORED_FUNCTION: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_EXCLUSION_FUNCTION: case GGV_EXCLUSION_WEAPON: return {0,GGV_REF,{}};
    case GGV_EXCLUSION_CHECK: return {2,GGV_BOOL,{GGV_REF,GGV_REF}};
    case GGV_INFO_WEAPON_QUERY: return {0,GGV_I32,{}};
    case GGV_INFO_IS_V2: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_INFO_ENSURE_V2: return {0,-1,{}};
    case GGV_INFO_V2_DEFINITION: return {0,GGV_REF,{}};
    case GGV_FREE_LOG: return {1,-1,{GGV_I32}};
    case GGV_FREE_ROCKET_OBJECT: return {1,-1,{GGV_REF}};
    case GGV_FUNCTION_CAPTURE_NATIVE: case GGV_FUNCTION_CAPTURE_CUSTOM: return {0,-1,{}};
    case GGV_FUNCTION_CAN_STORE: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_FUNCTION_STORE: return {2,-1,{GGV_I32,GGV_BOOL}};
    case GGV_FUNCTION_IS_SECONDARY: return {0,GGV_I32,{}};
    case GGV_SELECT_LOG: case GGV_SELECT_EQUIP: return {1,-1,{GGV_I32}};
    case GGV_SELECT_HAS_SINGLE: return {1,GGV_BOOL,{GGV_I32}};
    case GGV_SELECT_HAS_DOUBLE: return {2,GGV_BOOL,{GGV_I32,GGV_I32}};
    case GGV_SELECT_FIND_FUNCTION: return {2,GGV_REF,{GGV_I32,GGV_I32}};
    case GGV_SELECT_FUNCTION_FLAGS: return {1,GGV_U32,{GGV_REF}};
    default: return {-1,-1,{}};
    }
}
constexpr bool needsServices(const ggv_source &s) {
    for(unsigned p=0;p<s.count;++p) if(s.nodes[p].op==GGV_CALL) return true;
    return false;
}
struct Signature4 { int count, result; ggv_type args[4]; };
constexpr Signature4 signature4(ggv_service4 service) {
    return service==GGV_PAK_RUMBLE?Signature4{4,-1,{GGV_I32,GGV_F32,GGV_I32,GGV_I32}}
        :service==GGV_VIBRATION_INTERPOLATE?Signature4{4,-1,{GGV_REF,GGV_REF,GGV_F32,GGV_REF}}
        :service==GGV_TRIGGER_V2_INPUT?Signature4{4,-1,{GGV_REF,GGV_I32,GGV_BOOL,GGV_BOOL}}
        :Signature4{-1,-1,{}};
}
constexpr bool needsServices4(const ggv_source &s) {
    for(unsigned p=0;p<s.count;++p) if(s.nodes[p].op==GGV_CALL4) return true;
    return false;
}
/* Validation follows control edges and intersects incoming initialized sets.
 * Ordinary edges point forward. Repeat backedges may not weaken the already
 * checked header assumptions. Types are fixed and initialization is monotone;
 * this proves safe reads on repeated iterations, not runtime termination. */
constexpr bool valid(const ggv_source &s) {
    if (!s.count || s.count > GGV_NODE_LIMIT) return false;
    int registerTypes[GGV_REGISTER_LIMIT];
    uint32_t initialized[GGV_NODE_LIMIT + 1] = {};
    bool reached[GGV_NODE_LIMIT + 1] = {};
    for (auto &t : registerTypes) t = -1;
    reached[0] = true;
    bool returned = false;
    for (unsigned pc = 0; pc < s.count; ++pc) {
        const auto &n = s.nodes[pc];
        if (!reached[pc]) return false; // no hidden dead instructions
        uint32_t set = initialized[pc];
        auto has = [&](unsigned r, ggv_type t) constexpr {
            return r < GGV_REGISTER_LIMIT && (set & (uint32_t(1) << r))
                && registerTypes[r] == int(t);
        };
        auto dst = [&](ggv_type t) constexpr {
            if (n.dst >= GGV_REGISTER_LIMIT) return false;
            if (registerTypes[n.dst] != -1 && registerTypes[n.dst] != int(t)) return false;
            registerTypes[n.dst] = int(t); set |= uint32_t(1) << n.dst; return true;
        };
        bool ok = false, terminal = false;
        switch (n.op) {
        case GGV_CONST: ok = n.type >= GGV_I32 && n.type <= GGV_I64
            && (n.type != GGV_REF || n.literal.ref == nullptr) && dst(n.type); break;
        case GGV_READ: ok = n.binding < GGV_BINDING_COUNT && n.binding >= 0 && dst(types[n.binding]); break;
        case GGV_WRITE: ok = n.binding < GGV_BINDING_COUNT && n.binding >= 0 && writable(n.binding) && has(n.a, types[n.binding]); break;
        case GGV_MASK: ok = has(n.a, GGV_U32) && dst(GGV_BOOL); break;
        case GGV_I_LT: case GGV_I_EQ: case GGV_I_GT: case GGV_I_LE:
            ok = has(n.a, GGV_I32) && has(n.b, GGV_I32) && dst(GGV_BOOL); break;
        case GGV_I64_LT: case GGV_I64_EQ:
            ok = has(n.a, GGV_I64) && has(n.b, GGV_I64) && dst(GGV_BOOL); break;
        case GGV_F_LT: case GGV_F_GT: case GGV_F_LE: case GGV_F_GE:
            ok = has(n.a, GGV_F32) && has(n.b, GGV_F32) && dst(GGV_BOOL); break;
        case GGV_NOT: ok = has(n.a, GGV_BOOL) && dst(GGV_BOOL); break;
        case GGV_F_ADD_DIV_I: case GGV_F_SUB_DIV_I:
            ok = has(n.a, GGV_F32) && has(n.b, GGV_F32) && has(n.c, GGV_I32) && dst(GGV_F32); break;
        case GGV_F_INTERPOLATE: case GGV_F_RPM_CREDIT:
            ok = has(n.a, GGV_F32) && has(n.b, GGV_F32) && has(n.c, GGV_F32) && dst(GGV_F32); break;
        case GGV_F_MINUS_I: ok = has(n.a, GGV_F32) && has(n.b, GGV_I32) && dst(GGV_F32); break;
        case GGV_TRUNCATE: ok = has(n.a, GGV_F32) && dst(GGV_I32); break;
        case GGV_I_INCREMENT: case GGV_I_ABS: ok = has(n.a, GGV_I32) && dst(GGV_I32); break;
        case GGV_U_AND_LITERAL: case GGV_U_OR_LITERAL:
            ok=has(n.a,GGV_U32)&&dst(GGV_U32); break;
        case GGV_I_TO_U: ok=has(n.a,GGV_I32)&&dst(GGV_U32); break;
        case GGV_U_TO_I: ok=has(n.a,GGV_U32)&&dst(GGV_I32); break;
        case GGV_I_ADD_WRAP: case GGV_I_SUB_WRAP:
            ok=has(n.a,GGV_I32)&&has(n.b,GGV_I32)&&dst(GGV_I32); break;
        case GGV_I_DIV_TRUNC: ok=has(n.a,GGV_I32)&&has(n.b,GGV_I32)&&dst(GGV_I32); break;
        case GGV_U_SHR: case GGV_U_SHL: case GGV_U_AND: case GGV_U_OR:
            ok=has(n.a,GGV_U32)&&has(n.b,GGV_U32)&&dst(GGV_U32); break;
        case GGV_U_NOT: ok=has(n.a,GGV_U32)&&dst(GGV_U32); break;
        case GGV_U_MOD_LITERAL: ok=has(n.a,GGV_U32)&&n.literal.u!=0&&dst(GGV_U32); break;
        case GGV_U_GT: ok=has(n.a,GGV_U32)&&has(n.b,GGV_U32)&&dst(GGV_BOOL); break;
        case GGV_I_DIV_F: ok=has(n.a,GGV_I32)&&has(n.b,GGV_F32)&&dst(GGV_F32); break;
        case GGV_F_MUL: case GGV_F_SUB: case GGV_F_ADD:
            ok=has(n.a,GGV_F32)&&has(n.b,GGV_F32)&&dst(GGV_F32); break;
        case GGV_F_BITS_I: ok=has(n.a,GGV_F32)&&dst(GGV_I32); break;
        case GGV_I_TO_F: ok=has(n.a,GGV_I32)&&dst(GGV_F32); break;
        case GGV_I64_TO_F: ok=has(n.a,GGV_I64)&&dst(GGV_F32); break;
        case GGV_U_TO_F: ok=has(n.a,GGV_U32)&&dst(GGV_F32); break;
        case GGV_I64_TO_I32: ok=has(n.a,GGV_I64)&&dst(GGV_I32); break;
        case GGV_I64_ADD: case GGV_I64_SUB: case GGV_I64_REM: ok=has(n.a,GGV_I64)&&has(n.b,GGV_I64)&&dst(GGV_I64); break;
        case GGV_F_COPY: ok=has(n.a,GGV_F32)&&dst(GGV_F32); break;
        case GGV_F_EQ: ok=has(n.a,GGV_F32)&&has(n.b,GGV_F32)&&dst(GGV_BOOL); break;
        case GGV_I_TO_I64: ok=has(n.a,GGV_I32)&&dst(GGV_I64); break;
        case GGV_F_DIV: ok=has(n.a,GGV_F32)&&has(n.b,GGV_F32)&&dst(GGV_F32); break;
        case GGV_F_SCALE_BIAS: case GGV_F_BLEND: case GGV_F_ADD_PRODUCT:
            ok=has(n.a,GGV_F32)&&has(n.b,GGV_F32)&&has(n.c,GGV_F32)&&dst(GGV_F32); break;
        case GGV_RETURN_BOOL: ok=has(n.a,GGV_BOOL); terminal=true; returned=true; break;
        case GGV_RETURN_I32: ok=has(n.a,GGV_I32); terminal=true; returned=true; break;
        case GGV_REF_NONNULL: ok=has(n.a,GGV_REF)&&dst(GGV_BOOL); break;
        case GGV_REF_EQ: ok=has(n.a,GGV_REF)&&has(n.b,GGV_REF)&&dst(GGV_BOOL); break;
        case GGV_CALL: {
            const auto sig=signature(n.service);
            const unsigned args[]={n.a,n.b,n.c};
            ok=sig.count>=0;
            for(int a=0;ok&&a<sig.count;++a) ok=has(args[a],sig.args[a]);
            if(ok&&sig.result>=0) ok=dst(ggv_type(sig.result));
            break;
        }
        case GGV_CALL4: {
            const auto sig=signature4(n.service4);
            const unsigned args[]={n.a,n.b,n.c,n.d};
            ok=sig.count==4;
            for(int a=0;ok&&a<4;++a) ok=has(args[a],sig.args[a]);
            if(ok&&sig.result>=0) ok=dst(ggv_type(sig.result));
            break;
        }
        case GGV_BRANCH_FALSE: ok = has(n.a, GGV_BOOL); break;
        case GGV_REPEAT_WHILE:
            ok=has(n.a,GGV_BOOL) && n.target<pc && reached[n.target]
                && (initialized[n.target]&~set)==0;
            break;
        case GGV_JUMP: ok = true; break;
        case GGV_RETURN: ok = true; terminal = true; returned = true; break;
        default: return false;
        }
        if (!ok) return false;
        auto edge = [&](unsigned target) constexpr {
            if (target <= pc || target >= s.count) return false;
            if (reached[target]) initialized[target] &= set;
            else { reached[target] = true; initialized[target] = set; }
            return true;
        };
        if (terminal) continue;
        if ((n.op == GGV_JUMP || n.op == GGV_BRANCH_FALSE) && !edge(n.target)) return false;
        if (n.op != GGV_JUMP && !edge(pc + 1)) return false;
    }
    return returned;
}

struct Builder {
    ggv_source s{};
    constexpr unsigned emit(ggv_op op, unsigned d=0, unsigned a=0, unsigned b=0, unsigned c=0) {
        unsigned p=s.count++; auto &n=s.nodes[p]; n.op=op; n.dst=d; n.a=a; n.b=b; n.c=c; return p;
    }
    constexpr void read(unsigned r, ggv_binding b) { s.nodes[emit(GGV_READ,r)].binding=b; }
    constexpr void write(ggv_binding b, unsigned r) { s.nodes[emit(GGV_WRITE,0,r)].binding=b; }
    constexpr void i(unsigned r, int v) { auto &n=s.nodes[emit(GGV_CONST,r)]; n.type=GGV_I32; n.literal.i=v; }
    constexpr void f(unsigned r, float v) { auto &n=s.nodes[emit(GGV_CONST,r)]; n.type=GGV_F32; n.literal.f=v; }
    constexpr void boolean(unsigned r, bool v) { auto &n=s.nodes[emit(GGV_CONST,r)]; n.type=GGV_BOOL; n.literal.b=v; }
    constexpr void nullref(unsigned r) { auto &n=s.nodes[emit(GGV_CONST,r)]; n.type=GGV_REF; n.literal.ref=nullptr; }
    constexpr void call(ggv_service service, unsigned d=0, unsigned a=0, unsigned c=0) {
        s.nodes[emit(GGV_CALL,d,a,c)].service=service;
    }
    constexpr void call4(ggv_service4 service,unsigned a,unsigned b,unsigned c,unsigned d) {
        auto &node=s.nodes[emit(GGV_CALL4,0,a,b,c)];node.d=d;node.service4=service;
    }
    constexpr void uop(ggv_op op,unsigned d,unsigned a,uint32_t bits) {
        s.nodes[emit(op,d,a)].literal.u=bits;
    }
    constexpr void append(const ggv_source &source) {
        const unsigned offset=s.count;
        for(unsigned p=0;p<source.count;++p) {
            auto n=source.nodes[p];
            if(n.op==GGV_BRANCH_FALSE||n.op==GGV_JUMP||n.op==GGV_REPEAT_WHILE) n.target+=offset;
            s.nodes[s.count++]=n;
        }
    }
    constexpr void mask(ggv_binding binding, uint32_t bits) {
        read(2,binding); s.nodes[emit(GGV_MASK,4,2)].literal.u=bits;
    }
    constexpr void compare(ggv_binding b, ggv_op op, int v) { read(3,b); i(7,v); emit(op,4,3,7); }
    constexpr unsigned branch(unsigned r=4) { return emit(GGV_BRANCH_FALSE,0,r); }
    constexpr unsigned jump() { return emit(GGV_JUMP); }
    constexpr void end(unsigned p) { s.nodes[p].target=s.count; }
    constexpr void ret(int v) { s.nodes[emit(GGV_RETURN)].literal.i=v; }
};
constexpr ggv_source controlSource() {
    Builder b;
    b.boolean(0,false); b.boolean(1,false);
    b.mask(GGV_FUNCTION_FLAGS,2); auto burst3=b.branch();
    b.compare(GGV_BURST_BULLETS,GGV_I_LT,3); auto count3=b.branch();
    b.read(5,GGV_AIM); b.emit(GGV_NOT,4,5); auto aim=b.branch();
    b.boolean(0,true); auto afterAim=b.jump(); b.end(aim);
    b.read(6,GGV_AUTOMATIC); b.emit(GGV_NOT,4,6); auto autoAim=b.branch();
    b.boolean(0,true); b.end(afterAim); b.end(autoAim); b.end(count3); b.end(burst3);
    for (auto pair : {std::pair<uint32_t,int>{0x1000,2}, {0x20000,5}}) {
        b.mask(GGV_FUNCTION_FLAGS,pair.first); auto mask=b.branch();
        b.compare(GGV_BURST_BULLETS,GGV_I_LT,pair.second); auto count=b.branch();
        b.boolean(0,true); b.end(count); b.end(mask);
    }
    b.mask(GGV_FUNCTION_FLAGS,0x20); auto burst50=b.branch();
    b.compare(GGV_BURST_BULLETS,GGV_I_LT,50); auto count50=b.branch();
    b.boolean(1,true); b.end(count50); b.end(burst50);
    auto small=b.branch(0); b.boolean(1,true); b.end(small);
    b.read(5,GGV_TRIGGER); auto notTrigger=b.branch(5); auto triggerAdmission=b.jump(); b.end(notTrigger);
    b.mask(GGV_STATE_FLAGS,0x10); b.emit(GGV_NOT,4,4); auto flagSet=b.branch(); auto flagAdmission=b.jump(); b.end(flagSet);
    auto rejected=b.branch(1); b.end(triggerAdmission); b.end(flagAdmission);
    b.compare(GGV_AMMO_INDEX,GGV_I_GT,-1); auto noIndex=b.branch();
    b.compare(GGV_LOADED_AMMO,GGV_I_EQ,0); auto notEmpty=b.branch();
    b.compare(GGV_AMMO_TYPE,GGV_I_GT,-1); auto noAmmo=b.branch();
    b.ret(-1); b.end(noAmmo); b.end(notEmpty); b.end(noIndex);
    b.read(6,GGV_AUTOMATIC); auto nonAuto=b.branch(6);
    b.read(11,GGV_ACCELERATION); b.i(7,0); b.emit(GGV_I_GT,4,11,7); auto noAccel=b.branch();
    b.read(9,GGV_SPOOL); b.f(12,1); b.emit(GGV_F_LT,4,9,12); auto full=b.branch();
    b.read(10,GGV_CLOCK); b.emit(GGV_F_ADD_DIV_I,12,9,10,11); b.write(GGV_SPOOL,12);
    b.read(9,GGV_SPOOL); b.f(12,1); b.emit(GGV_F_GT,4,9,12); auto noOver=b.branch();
    b.write(GGV_SPOOL,12); b.ret(1); b.end(noOver); b.end(full); b.ret(1);
    b.end(noAccel); b.f(12,1); b.write(GGV_SPOOL,12); b.ret(1);
    b.end(nonAuto); b.f(12,1); b.write(GGV_SPOOL,12);
    auto noSmall=b.branch(0);
    b.compare(GGV_BURST_BULLETS,GGV_I_GT,0); auto first=b.branch();
    b.i(7,3); b.read(5,GGV_SHOTGUN); auto notShotgun=b.branch(5);
    b.read(7,GGV_SHOTGUN_DELAY); b.end(notShotgun);
    b.read(8,GGV_STATE_FRAMES); b.emit(GGV_I_LT,4,8,7); auto delayDone=b.branch();
    b.ret(0); b.end(delayDone); b.end(first); b.i(7,0); b.write(GGV_STATE_FRAMES,7); b.end(noSmall);
    for (auto pair : {std::pair<uint32_t,int>{2,2}, {0x1000,1}, {0x20000,4}}) {
        b.mask(GGV_FUNCTION_FLAGS,pair.first); auto mask=b.branch();
        b.compare(GGV_BURST_BULLETS,GGV_I_EQ,pair.second); auto count=b.branch();
        b.boolean(0,false); b.end(count); b.end(mask);
    }
    auto noRemaining=b.branch(0); b.ret(1); b.end(noRemaining); b.ret(2);
    b.end(rejected); b.read(6,GGV_AUTOMATIC); auto rejectedNonAuto=b.branch(6);
    b.read(11,GGV_DECELERATION); b.i(7,0); b.emit(GGV_I_GT,4,11,7); auto noDecel=b.branch();
    b.read(9,GGV_SPOOL); b.f(12,0); b.emit(GGV_F_GT,4,9,12); auto stopped=b.branch();
    b.read(10,GGV_CLOCK); b.emit(GGV_F_SUB_DIV_I,12,9,10,11); b.write(GGV_SPOOL,12);
    b.read(9,GGV_SPOOL); b.f(12,0); b.emit(GGV_F_LT,4,9,12); auto noUnder=b.branch();
    b.write(GGV_SPOOL,12); b.ret(-1); b.end(noUnder); b.ret(1); b.end(stopped); b.ret(-1);
    b.end(noDecel); b.f(12,0); b.write(GGV_SPOOL,12); b.end(rejectedNonAuto); b.ret(-1);
    return b.s;
}
constexpr ggv_source creditSource() {
    Builder b;
    b.boolean(5,true); b.write(GGV_FIRING,5);
    b.read(6,GGV_AUTOMATIC); auto nonAuto=b.branch(6);
    b.read(13,GGV_INITIAL_RPM); b.read(14,GGV_MAX_RPM); b.read(9,GGV_SPOOL);
    b.emit(GGV_F_INTERPOLATE,12,13,14,9);
    b.read(10,GGV_CLOCK); b.read(17,GGV_REMAINDER);
    b.emit(GGV_F_RPM_CREDIT,15,12,10,17);
    b.emit(GGV_TRUNCATE,16,15); b.write(GGV_SHOTS,16);
    b.read(16,GGV_SHOTS); b.emit(GGV_F_MINUS_I,17,15,16); b.write(GGV_REMAINDER,17);
    b.compare(GGV_SHOTS,GGV_I_LE,0); auto positive=b.branch();
    b.mask(GGV_STATE_FLAGS,0x10); b.emit(GGV_NOT,4,4); auto held=b.branch();
    b.read(16,GGV_SHOTS); b.emit(GGV_I_INCREMENT,16,16); b.write(GGV_SHOTS,16); b.ret(1);
    b.end(held); b.boolean(5,false); b.write(GGV_FIRING,5); b.end(positive); b.ret(1);
    b.end(nonAuto); b.i(16,1); b.write(GGV_SHOTS,16); b.read(5,GGV_LASER);
    auto notLaser=b.branch(5); b.ret(0); b.end(notLaser); b.ret(1);
    return b.s;
}
/* Configuration precedence is graph composition. The resolver only borrows
 * the catalog record; raw graph/legacy scalar reads never choose policy. */
constexpr void configuration(Builder &b,bool accelerations) {
    b.call(GGV_RESOLVE_GRAPH,20);
    auto noFlags=b.branch(20); b.read(2,GGV_GRAPH_FLAGS); auto flagsDone=b.jump();
    b.end(noFlags); b.read(2,GGV_LEGACY_FLAGS); b.end(flagsDone); b.write(GGV_FUNCTION_FLAGS,2);
    auto noSlot=b.branch(20); b.read(3,GGV_GRAPH_AMMO_SLOT); b.i(7,-1); b.emit(GGV_I_GT,4,3,7);
    auto negativeSlot=b.branch(); b.write(GGV_AMMO_INDEX,3); auto slotDone=b.jump();
    b.end(noSlot); b.end(negativeSlot); b.read(3,GGV_LEGACY_AMMO_INDEX); b.write(GGV_AMMO_INDEX,3); b.end(slotDone);
    auto noType=b.branch(20); b.read(5,GGV_GRAPH_HAS_TYPE); auto missingType=b.branch(5);
    b.read(3,GGV_GRAPH_FUNCTION_TYPE); auto typeDone=b.jump();
    b.end(noType); b.end(missingType); b.read(3,GGV_LEGACY_FUNCTION_TYPE); b.end(typeDone); b.write(GGV_FUNCTION_TYPE,3);
    b.emit(GGV_I_TO_U,2,3); b.uop(GGV_U_AND_LITERAL,2,2,0xff00); b.emit(GGV_U_TO_I,28,2);
    b.i(7,0x100); b.emit(GGV_I_EQ,19,28,7); b.write(GGV_LEGACY_AUTOMATIC,19);
    b.read(6,GGV_LEGACY_AUTOMATIC); auto noAutoGraph=b.branch(20);
    b.read(5,GGV_GRAPH_HAS_MAX_RPM); auto noAutoMax=b.branch(5); b.boolean(6,true);
    b.end(noAutoGraph); b.end(noAutoMax); b.write(GGV_AUTOMATIC,6);
    if(accelerations) {
        b.i(11,0); b.write(GGV_ACCELERATION,11); b.write(GGV_DECELERATION,11);
        auto notAuto=b.branch(6);
        for(auto fields : {std::pair<ggv_binding,ggv_binding>{GGV_GRAPH_HAS_ACCEL,GGV_GRAPH_ACCEL},
                           {GGV_GRAPH_HAS_DECEL,GGV_GRAPH_DECEL}}) {
            auto noGraph=b.branch(20); b.read(5,fields.first); auto absent=b.branch(5);
            b.read(11,fields.second); auto done=b.jump();
            b.end(noGraph); b.end(absent); auto noLegacy=b.branch(19);
            b.read(11,fields.first==GGV_GRAPH_HAS_ACCEL?GGV_LEGACY_ACCEL:GGV_LEGACY_DECEL);
            auto legacyDone=b.jump(); b.end(noLegacy); b.i(11,0);
            b.end(done); b.end(legacyDone);
            b.write(fields.first==GGV_GRAPH_HAS_ACCEL?GGV_ACCELERATION:GGV_DECELERATION,11);
        }
        b.end(notAuto);
    }
}
constexpr void rpmSelection(Builder &b) {
    /* Keep the original initial/max graph reads before legacy fallbacks. */
    b.f(13,0); auto noInitialGraph=b.branch(20); b.read(5,GGV_GRAPH_HAS_INITIAL_RPM);
    auto noInitial=b.branch(5); b.read(13,GGV_GRAPH_INITIAL_RPM); b.end(noInitialGraph); b.end(noInitial);
    b.f(14,0); auto noMaxGraph=b.branch(20); b.read(5,GGV_GRAPH_HAS_MAX_RPM);
    auto noMax=b.branch(5); b.read(14,GGV_GRAPH_MAX_RPM); b.end(noMaxGraph); b.end(noMax);
    auto noLegacy=b.branch(19);
    auto legacyInitial=b.branch(20); b.read(5,GGV_GRAPH_HAS_INITIAL_RPM); b.emit(GGV_NOT,4,5);
    auto keepInitial=b.branch(); b.end(legacyInitial); b.read(13,GGV_LEGACY_INITIAL_RPM); b.end(keepInitial);
    auto legacyMax=b.branch(20); b.read(5,GGV_GRAPH_HAS_MAX_RPM); b.emit(GGV_NOT,4,5);
    auto keepMax=b.branch(); b.end(legacyMax); b.read(14,GGV_LEGACY_MAX_RPM); b.end(keepMax);
    b.end(noLegacy);
}
constexpr ggv_source fullControlSource() {
    Builder b; configuration(b,true); b.append(controlSource()); return b.s;
}
constexpr ggv_source fullCommitSource() {
    Builder b; configuration(b,false);
    b.call(GGV_OVERRIDE_ON); b.call(GGV_OVERRIDE_OFF);
    b.boolean(18,true); b.boolean(5,true); b.write(GGV_FIRING,5);
    b.read(6,GGV_AUTOMATIC); auto nonAuto=b.branch(6);
    rpmSelection(b); b.read(9,GGV_SPOOL); b.emit(GGV_F_INTERPOLATE,12,13,14,9);
    b.read(10,GGV_CLOCK); b.read(17,GGV_REMAINDER); b.emit(GGV_F_RPM_CREDIT,15,12,10,17);
    b.emit(GGV_TRUNCATE,16,15); b.write(GGV_SHOTS,16);
    b.read(16,GGV_SHOTS); b.emit(GGV_F_MINUS_I,17,15,16); b.write(GGV_REMAINDER,17);
    b.compare(GGV_SHOTS,GGV_I_LE,0); auto positive=b.branch();
    b.mask(GGV_STATE_FLAGS,0x10); b.emit(GGV_NOT,4,4); auto held=b.branch();
    b.read(16,GGV_SHOTS); b.emit(GGV_I_INCREMENT,16,16); b.write(GGV_SHOTS,16); auto firstDone=b.jump();
    b.end(held); b.boolean(5,false); b.write(GGV_FIRING,5);
    b.end(positive); b.end(firstDone); auto autoDone=b.jump();
    b.end(nonAuto); b.i(16,1); b.write(GGV_SHOTS,16); b.read(5,GGV_LASER);
    auto notLaser=b.branch(5); b.boolean(18,false); b.end(notLaser); b.end(autoDone);

    b.read(3,GGV_BURST_BULLETS); b.read(16,GGV_SHOTS); b.emit(GGV_I_ADD_WRAP,3,3,16); b.write(GGV_BURST_BULLETS,3);
    b.mask(GGV_FUNCTION_FLAGS,0x2000); auto permitFlash=b.branch();
    b.boolean(5,false); b.write(GGV_FLASH,5); auto flashDone=b.jump();
    b.end(permitFlash); b.read(5,GGV_GE_FLASH); auto ordinaryFlash=b.branch(5);
    b.read(6,GGV_AUTOMATIC); b.emit(GGV_NOT,4,6); auto automaticFlash=b.branch();
    b.read(3,GGV_FUNCTION_TYPE); b.emit(GGV_I_TO_U,2,3); b.uop(GGV_U_AND_LITERAL,2,2,0xff); b.emit(GGV_U_TO_I,3,2);
    b.read(7,GGV_FUNCTION_SHOOT_LOW); b.emit(GGV_I_EQ,4,3,7); auto wrongLowType=b.branch();
    b.boolean(5,true); b.write(GGV_FLASH,5); auto singleFlashDone=b.jump();
    b.end(automaticFlash); b.end(wrongLowType);
    b.read(3,GGV_FUNCTION_TYPE); b.read(7,GGV_FUNCTION_SHOOT_SINGLE); b.emit(GGV_I_EQ,4,3,7); auto notSingle=b.branch();
    b.boolean(5,true); b.write(GGV_FLASH,5); auto exactFlashDone=b.jump();
    b.end(notSingle); b.read(16,GGV_SHOTS); b.emit(GGV_I_TO_U,2,16); b.uop(GGV_MASK,4,2,1); auto evenFlash=b.branch();
    b.boolean(5,true); b.write(GGV_FLASH,5); auto oddFlashDone=b.jump();
    b.end(ordinaryFlash); b.boolean(5,true); b.write(GGV_FLASH,5);
    b.end(flashDone); b.end(singleFlashDone); b.end(exactFlashDone); b.end(evenFlash); b.end(oddFlashDone);
    b.call(GGV_START_SLIDE); b.f(12,0); b.write(GGV_LOAD_SLIDE,12);

    b.read(5,GGV_FIRING); auto notFiring=b.branch(5);
    b.read(8,GGV_STATE_FRAMES); b.write(GGV_STATE_VAR1,8);
    b.read(2,GGV_STATE_FLAGS); b.uop(GGV_U_OR_LITERAL,2,2,0x20); b.write(GGV_STATE_FLAGS,2);
    b.read(2,GGV_STATE_FLAGS); b.uop(GGV_U_OR_LITERAL,2,2,0x10); b.write(GGV_STATE_FLAGS,2);
    b.call(GGV_RUMBLE);
    auto noAmmoUse=b.branch(18); b.compare(GGV_AMMO_INDEX,GGV_I_GT,-1); auto noAmmoSlot=b.branch();
    b.read(3,GGV_LOADED_AMMO); b.read(16,GGV_SHOTS); b.emit(GGV_I_SUB_WRAP,3,3,16); b.write(GGV_LOADED_AMMO,3);
    b.compare(GGV_LOADED_AMMO,GGV_I_LT,0); auto enoughAmmo=b.branch();
    b.read(16,GGV_SHOTS); b.read(3,GGV_LOADED_AMMO); b.emit(GGV_I_ADD_WRAP,16,16,3); b.write(GGV_SHOTS,16);
    b.i(3,0); b.write(GGV_LOADED_AMMO,3); b.end(enoughAmmo); b.end(noAmmoUse); b.end(noAmmoSlot);
    b.read(3,GGV_FUNCTION_TYPE); b.emit(GGV_I_TO_U,2,3); b.uop(GGV_U_AND_LITERAL,2,2,0xff00); b.emit(GGV_U_TO_I,28,2);
    b.i(7,0); b.emit(GGV_I_EQ,4,28,7); auto nonZeroType=b.branch();
    b.read(3,GGV_BIND_ATTACK_SHOOT); b.write(GGV_ATTACK_TYPE,3); auto attackDone0=b.jump();
    b.end(nonZeroType); b.i(7,0x100); b.emit(GGV_I_EQ,4,28,7); auto nonAutoType=b.branch();
    b.read(3,GGV_BIND_ATTACK_SHOOT); b.write(GGV_ATTACK_TYPE,3); auto attackDone1=b.jump();
    b.end(nonAutoType); b.i(7,0x200); b.emit(GGV_I_EQ,4,28,7); auto otherType=b.branch();
    b.read(3,GGV_ATTACK_PROJECTILE); b.write(GGV_ATTACK_TYPE,3);
    b.end(attackDone0); b.end(attackDone1); b.end(otherType); b.end(notFiring);

    b.read(5,GGV_FIRING); auto noSoundFiring=b.branch(5);
    b.call(GGV_FIRESLOT_DURATION,25); b.i(7,0); b.emit(GGV_I_GT,4,25,7); auto unlimitedSound=b.branch();
    b.read(26,GGV_CURRENT_FRAME); b.read(8,GGV_OTHER_LAST_FRAME); b.emit(GGV_I_EQ,4,26,8); b.emit(GGV_NOT,4,4); auto otherHandSound=b.branch();
    b.read(26,GGV_CURRENT_FRAME); b.read(8,GGV_ALLOW_SHOOT_FRAME); b.emit(GGV_I_GT,4,26,8); auto tooSoon=b.branch();
    b.read(26,GGV_CURRENT_FRAME); b.call(GGV_FIRESLOT_DURATION,25); b.emit(GGV_I_ADD_WRAP,26,26,25); b.write(GGV_ALLOW_SHOOT_FRAME,26); auto playSound=b.jump();
    b.end(unlimitedSound); b.read(5,GGV_FIRING); auto noUnlimitedFiring=b.branch(5); b.end(playSound);
    b.i(24,0); b.read(5,GGV_PRIORITY_ENABLED); auto noPriority=b.branch(5);
    b.call(GGV_CURRENT_PRIORITY,24); b.call(GGV_AUDIO_PRIORITY,25); b.emit(GGV_I_INCREMENT,25,25); b.call(GGV_SET_PRIORITY,0,25); b.end(noPriority);
    for(auto slot : {GGV_AUDIO_SLOT_2,GGV_AUDIO_SLOT_3}) {
        b.read(21,slot); b.emit(GGV_REF_NONNULL,4,21); auto nullSlot=b.branch();
        b.read(21,slot);
        b.call(GGV_SOUND_STATE,25,21); b.read(7,GGV_SOUND_STOPPED); b.emit(GGV_I_EQ,4,25,7); b.emit(GGV_NOT,4,4); auto stopped=b.branch();
        b.read(21,slot);
        b.call(GGV_SOUND_STOP,0,21); b.end(nullSlot); b.end(stopped);
    }
    b.call(GGV_SINGLE_SOUND,25); b.i(7,0); b.emit(GGV_I_EQ,4,25,7); b.emit(GGV_NOT,4,4); auto noSound=b.branch();
    b.nullref(23); b.read(21,GGV_AUDIO_SLOT_2); b.emit(GGV_REF_NONNULL,4,21); auto useSlot2=b.branch();
    b.read(21,GGV_AUDIO_SLOT_3); b.emit(GGV_REF_NONNULL,4,21); auto useSlot3=b.branch(); auto slotsOccupied=b.jump();
    b.end(useSlot2); b.i(7,2); b.call(GGV_SINGLE_SOUND,25); b.call(GGV_SOUND_START,23,7,25); auto started2=b.jump();
    b.end(useSlot3); b.i(7,3); b.call(GGV_SINGLE_SOUND,25); b.call(GGV_SOUND_START,23,7,25);
    b.end(slotsOccupied); b.end(started2); b.read(26,GGV_CURRENT_FRAME); b.write(GGV_LAST_SHOOT_FRAME,26);
    b.read(5,GGV_MAULER); auto noMauler=b.branch(5); b.emit(GGV_REF_NONNULL,4,23); auto noHandle=b.branch();
    b.read(12,GGV_MATMOT); b.emit(GGV_TRUNCATE,3,12); b.f(14,3); b.emit(GGV_I_DIV_F,12,3,14);
    b.f(14,1); b.emit(GGV_F_GT,4,12,14); auto noClamp=b.branch(); b.f(12,1); b.end(noClamp);
    b.f(14,0.4f); b.emit(GGV_F_MUL,12,12,14); b.f(14,1); b.emit(GGV_F_SUB,12,14,12);
    b.emit(GGV_F_BITS_I,3,12); b.call(GGV_SOUND_EVENT,0,23,3); b.end(noMauler); b.end(noHandle); b.end(noSound);
    b.read(5,GGV_PRIORITY_ENABLED); auto noRestore=b.branch(5); b.call(GGV_SET_PRIORITY,0,24); b.end(noRestore);
    b.end(noSoundFiring); b.end(otherHandSound); b.end(tooSoon); b.end(noUnlimitedFiring);
    auto noAmmoResult=b.branch(18); b.ret(1); b.end(noAmmoResult); b.ret(0);
    return b.s;
}
constexpr ggv_source handRumbleSource() {
    Builder b;
    b.f(9,0.2f); b.i(10,2); b.i(11,4);
    b.call(GGV_GET_PADS); b.call(GGV_CONTROL_MODE,3); b.read(7,GGV_CONTROL_MODE_21);
    b.emit(GGV_I_LT,4,3,7); b.emit(GGV_NOT,4,4); auto ordinaryMode=b.branch();
    b.read(0,GGV_PAD_1); b.i(7,-1); b.emit(GGV_I_GT,4,0,7); auto invalidFirst=b.branch();
    b.read(1,GGV_PAD_2); b.emit(GGV_I_GT,4,1,7); auto invalidSecond=b.branch();
    b.call(GGV_PAK_TYPE,3,0); b.read(7,GGV_PAK_RUMBLE_TYPE); b.emit(GGV_I_EQ,6,3,7);
    b.call(GGV_PAK_TYPE,3,1); b.read(7,GGV_PAK_RUMBLE_TYPE); b.emit(GGV_I_EQ,8,3,7);
    b.call(GGV_WEAPON_DUALWIELD,5); b.emit(GGV_NOT,5,5);
    auto firstNoRumble=b.branch(6); auto secondNoRumble=b.branch(8);
    auto dualWield=b.branch(5);
    b.call4(GGV_PAK_RUMBLE,0,9,10,11); b.call4(GGV_PAK_RUMBLE,1,9,10,11); b.ret(0);
    b.end(dualWield); b.read(2,GGV_PAD_1); b.read(3,GGV_HAND_NUMBER); b.read(7,GGV_HAND_LEFT_NUMBER);
    b.emit(GGV_I_EQ,4,3,7); auto notLeft=b.branch(); b.read(2,GGV_PAD_2); b.end(notLeft);
    b.call4(GGV_PAK_RUMBLE,2,9,10,11); b.ret(0);
    b.end(firstNoRumble); b.end(secondNoRumble); b.read(2,GGV_PAD_1);
    auto noSecondRumble=b.branch(8); b.read(2,GGV_PAD_2); b.end(noSecondRumble);
    b.call4(GGV_PAK_RUMBLE,2,9,10,11); b.ret(0);
    b.end(ordinaryMode); b.end(invalidFirst); b.end(invalidSecond);
    b.read(0,GGV_PAD_1); b.i(7,-1); b.emit(GGV_I_GT,4,0,7); auto noPad=b.branch();
    b.call4(GGV_PAK_RUMBLE,0,9,10,11); b.end(noPad); b.ret(0);
    return b.s;
}
constexpr ggv_source handSlideSource() {
    Builder b;b.boolean(0,true);b.write(GGV_CURRENT_HAND_SLIDEINC,0);b.ret(0);return b.s;
}
/* Recovery/attack programs preserve native version ordering. Raw catalog,
 * hand and reference fields are read lazily; source constants are bindings.
 * Float operations retain native expression grouping, with no numeric repair. */
constexpr ggv_source recoilRecoverySource() {
    Builder b; b.call(GGV_RESOLVE_GRAPH,19);
    auto noGraph24=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_24_HAS); auto noField24=b.branch(18);
    b.read(0,GGV_GRAPH_RECOIL_24); auto selected24=b.jump();
    b.end(noGraph24); b.end(noField24); b.read(0,GGV_LEGACY_RECOIL_24); b.end(selected24);
    auto noGraph25=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_25_HAS); auto noField25=b.branch(18);
    b.read(1,GGV_GRAPH_RECOIL_25); auto selected25=b.jump();
    b.end(noGraph25); b.end(noField25); b.read(1,GGV_LEGACY_RECOIL_25); b.end(selected25);
    b.read(18,GGV_RECOVERY_PAL); auto notPal=b.branch(18);
    auto noGraph26Pal=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_26_HAS); auto noField26Pal=b.branch(18);
    b.read(2,GGV_GRAPH_RECOIL_26); auto selected26Pal=b.jump();
    b.end(noGraph26Pal); b.end(noField26Pal); b.read(2,GGV_LEGACY_RECOIL_26); b.end(selected26Pal);
    auto noGraph27Pal=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_27_HAS); auto noField27Pal=b.branch(18);
    b.read(3,GGV_GRAPH_RECOIL_27); auto selected27Pal=b.jump();
    b.end(noGraph27Pal); b.end(noField27Pal); b.read(3,GGV_LEGACY_RECOIL_27); b.end(selected27Pal);
    auto noGraphTIMEPal=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_TIME_HAS); auto noFieldTIMEPal=b.branch(18);
    b.read(4,GGV_GRAPH_RECOIL_TIME); auto selectedTIMEPal=b.jump();
    b.end(noGraphTIMEPal); b.end(noFieldTIMEPal); b.read(4,GGV_LEGACY_RECOIL_TIME); b.end(selectedTIMEPal);
    b.call(GGV_CAPTURE_DEFINITION);
    b.i(8,4); b.emit(GGV_I_LT,18,0,8); b.emit(GGV_NOT,18,18); auto tick0=b.branch(18); b.call(GGV_RECOIL_TICKS,0,0); b.end(tick0);
    b.i(8,4); b.emit(GGV_I_LT,18,1,8); b.emit(GGV_NOT,18,18); auto tick1=b.branch(18); b.call(GGV_RECOIL_TICKS,1,1); b.end(tick1);
    b.i(8,4); b.emit(GGV_I_LT,18,2,8); b.emit(GGV_NOT,18,18); auto tick2=b.branch(18); b.call(GGV_RECOIL_TICKS,2,2); b.end(tick2);
    b.i(8,4); b.emit(GGV_I_LT,18,3,8); b.emit(GGV_NOT,18,18); auto tick3=b.branch(18); b.call(GGV_RECOIL_TICKS,3,3); b.end(tick3);
    b.i(8,4); b.emit(GGV_I_LT,18,4,8); b.emit(GGV_NOT,18,18); auto tick4=b.branch(18); b.call(GGV_RECOIL_TICKS,4,4); b.end(tick4);
    b.emit(GGV_I_ADD_WRAP,5,0,1); auto selectedPal=b.jump(); b.end(notPal);
    b.read(18,GGV_RECOVERY_JPN); auto notJpn=b.branch(18);
    auto noGraph26Jpn=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_26_HAS); auto noField26Jpn=b.branch(18);
    b.read(2,GGV_GRAPH_RECOIL_26); auto selected26Jpn=b.jump();
    b.end(noGraph26Jpn); b.end(noField26Jpn); b.read(2,GGV_LEGACY_RECOIL_26); b.end(selected26Jpn);
    auto noGraph27Jpn=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_27_HAS); auto noField27Jpn=b.branch(18);
    b.read(3,GGV_GRAPH_RECOIL_27); auto selected27Jpn=b.jump();
    b.end(noGraph27Jpn); b.end(noField27Jpn); b.read(3,GGV_LEGACY_RECOIL_27); b.end(selected27Jpn);
    auto noGraphTIMEJpn=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_TIME_HAS); auto noFieldTIMEJpn=b.branch(18);
    b.read(4,GGV_GRAPH_RECOIL_TIME); auto selectedTIMEJpn=b.jump();
    b.end(noGraphTIMEJpn); b.end(noFieldTIMEJpn); b.read(4,GGV_LEGACY_RECOIL_TIME); b.end(selectedTIMEJpn);
    b.call(GGV_CAPTURE_DEFINITION); b.emit(GGV_I_ADD_WRAP,5,0,1);
    auto selectedJpn=b.jump(); b.end(notJpn); b.emit(GGV_I_ADD_WRAP,5,0,1);
    auto noGraph26=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_26_HAS); auto noField26=b.branch(18);
    b.read(2,GGV_GRAPH_RECOIL_26); auto selected26=b.jump();
    b.end(noGraph26); b.end(noField26); b.read(2,GGV_LEGACY_RECOIL_26); b.end(selected26);
    auto noGraph27=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_27_HAS); auto noField27=b.branch(18);
    b.read(3,GGV_GRAPH_RECOIL_27); auto selected27=b.jump();
    b.end(noGraph27); b.end(noField27); b.read(3,GGV_LEGACY_RECOIL_27); b.end(selected27);
    auto noGraphTIME=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_TIME_HAS); auto noFieldTIME=b.branch(18);
    b.read(4,GGV_GRAPH_RECOIL_TIME); auto selectedTIME=b.jump();
    b.end(noGraphTIME); b.end(noFieldTIME); b.read(4,GGV_LEGACY_RECOIL_TIME); b.end(selectedTIME);
    b.call(GGV_CAPTURE_DEFINITION); b.end(selectedJpn); b.end(selectedPal);
    b.read(8,GGV_STATE_FRAMES); b.read(9,GGV_STATE_VAR1); b.emit(GGV_I_SUB_WRAP,6,8,9);
    b.i(8,1); b.emit(GGV_I_LT,18,5,8); auto positiveSum=b.branch(18);
    b.i(5,0); auto skipMotion=b.jump(); b.end(positiveSum);
    b.read(18,GGV_TRIGGER_RELEASED); auto noRelease=b.branch(18);
    b.read(18,GGV_TRIGGER); auto noTrigger=b.branch(18);
    b.emit(GGV_I_LT,18,6,2); b.emit(GGV_NOT,18,18); auto earlyFrame=b.branch(18);
    b.i(8,0); b.emit(GGV_I_GT,18,2,8); auto noRecoilStart=b.branch(18);
    b.emit(GGV_I_LT,18,3,8); b.emit(GGV_NOT,18,18); auto disabledInterrupt=b.branch(18);
    b.read(26,GGV_STATE_FLAGS); b.s.nodes[b.emit(GGV_MASK,18,26)].literal.u=0x40;
    b.emit(GGV_NOT,18,18); auto alreadyInterrupted=b.branch(18);
    b.emit(GGV_I_ADD_WRAP,8,6,3); b.emit(GGV_I_LT,18,8,5); auto lateInterrupt=b.branch(18);
    b.uop(GGV_U_OR_LITERAL,26,26,0x40); b.write(GGV_STATE_FLAGS,26); b.write(GGV_STATE_VAR1,6);
    b.read(10,GGV_ROTX_OFFSET); b.write(GGV_ROTX_START,10); b.f(10,0.0f); b.write(GGV_ROTX_END,10);
    b.write(GGV_POS_END_X,10); b.write(GGV_POS_END_Y,10); b.write(GGV_POS_END_Z,10);
    b.read(10,GGV_POS_OFFSET_X); b.write(GGV_POS_START_X,10);
    b.read(10,GGV_POS_OFFSET_Y); b.write(GGV_POS_START_Y,10);
    b.read(10,GGV_POS_OFFSET_Z); b.write(GGV_POS_START_Z,10);
    b.end(lateInterrupt); b.end(alreadyInterrupted); b.end(disabledInterrupt);
    b.end(noRecoilStart); b.end(earlyFrame); b.end(noTrigger); b.end(noRelease);
    b.read(26,GGV_STATE_FLAGS); b.s.nodes[b.emit(GGV_MASK,18,26)].literal.u=0x40;
    auto notInterrupted=b.branch(18);
    b.read(9,GGV_STATE_VAR1); b.emit(GGV_I_SUB_WRAP,8,6,9); b.emit(GGV_I_GT,18,3,8);
    auto interruptionDone=b.branch(18);
    b.emit(GGV_I_SUB_WRAP,8,3,6); b.emit(GGV_I_ADD_WRAP,8,8,9); b.emit(GGV_I_TO_F,10,8);
    b.f(11,1.5707963705063f); b.emit(GGV_F_MUL,10,10,11); b.emit(GGV_I_TO_F,11,3);
    b.emit(GGV_F_DIV,10,10,11); b.call(GGV_RECOIL_COS,12,10); b.f(11,0.5f);
    b.emit(GGV_F_SCALE_BIAS,12,12,11,11);
    b.read(10,GGV_ROTX_START); b.read(11,GGV_ROTX_END);
    b.s.nodes[b.emit(GGV_CALL,13,10,11,12)].service=GGV_RECOIL_TWEEN;
    b.write(GGV_ROTX_OFFSET,13); b.boolean(18,true); b.write(GGV_USE_POSROT,18);
    b.read(10,GGV_POS_END_X); b.read(11,GGV_POS_START_X);
    b.emit(GGV_F_BLEND,13,10,11,12); b.write(GGV_POS_OFFSET_X,13);
    b.read(10,GGV_POS_END_Y); b.read(11,GGV_POS_START_Y);
    b.emit(GGV_F_BLEND,13,10,11,12); b.write(GGV_POS_OFFSET_Y,13);
    b.read(10,GGV_POS_END_Z); b.read(11,GGV_POS_START_Z);
    b.emit(GGV_F_BLEND,13,10,11,12); b.write(GGV_POS_OFFSET_Z,13);
    b.read(10,GGV_ROTX_OFFSET); b.read(21,GGV_POS_MATRIX); b.call(GGV_MATRIX_X,0,10,21);
    b.read(22,GGV_POS_COORD); b.call(GGV_MATRIX_TRANSLATION,0,22,21);
    auto interruptionPoseDone=b.jump(); b.end(interruptionDone);
    b.read(21,GGV_POS_MATRIX); b.call(GGV_MATRIX_IDENTITY,0,21);
    b.boolean(18,false); b.write(GGV_USE_POSROT,18); b.ret(1);
    b.end(interruptionPoseDone); b.end(notInterrupted);
    b.emit(GGV_I_LT,18,6,5); auto outsideMotion=b.branch(18);
    b.read(26,GGV_STATE_FLAGS); b.s.nodes[b.emit(GGV_MASK,18,26)].literal.u=0x40;
    b.emit(GGV_NOT,18,18); auto interruptedMotion=b.branch(18);
    auto noGraphDIST=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_DIST_HAS); auto noFieldDIST=b.branch(18);
    b.read(24,GGV_GRAPH_RECOIL_DIST); auto selectedDIST=b.jump();
    b.end(noGraphDIST); b.end(noFieldDIST); b.read(24,GGV_LEGACY_RECOIL_DIST); b.end(selectedDIST);
    auto noGraphANGLE=b.branch(19);
    b.read(18,GGV_GRAPH_RECOIL_ANGLE_HAS); auto noFieldANGLE=b.branch(18);
    b.read(25,GGV_GRAPH_RECOIL_ANGLE); auto selectedANGLE=b.jump();
    b.end(noGraphANGLE); b.end(noFieldANGLE); b.read(25,GGV_LEGACY_RECOIL_ANGLE); b.end(selectedANGLE);
    b.read(26,GGV_STATE_FLAGS); b.s.nodes[b.emit(GGV_MASK,18,26)].literal.u=0x80;
    b.emit(GGV_NOT,18,18); auto savedStart=b.branch(18);
    b.uop(GGV_U_OR_LITERAL,26,26,0x80); b.write(GGV_STATE_FLAGS,26);
    b.read(10,GGV_ROTX_OFFSET); b.write(GGV_ROTX_START,10);
    b.read(10,GGV_POS_OFFSET_X); b.write(GGV_POS_START_X,10);
    b.read(10,GGV_POS_OFFSET_Y); b.write(GGV_POS_START_Y,10);
    b.read(10,GGV_POS_OFFSET_Z); b.write(GGV_POS_START_Z,10);
    b.end(savedStart);
    b.read(10,GGV_RECOIL_BAD_TAU); b.emit(GGV_F_MUL,11,25,10); b.f(13,360.0f);
    b.emit(GGV_F_DIV,11,11,13); b.emit(GGV_F_SUB,11,10,11); b.write(GGV_ROTX_END,11);
    b.call(GGV_RECOIL_AIM_X,10); b.read(11,GGV_AIM_POS_X); b.emit(GGV_F_SUB,10,10,11);
    b.emit(GGV_F_MUL,10,10,24); b.f(11,1000.0f); b.emit(GGV_F_DIV,10,10,11); b.write(GGV_POS_END_X,10);
    b.f(10,0.0f); b.write(GGV_POS_END_Y,10);
    b.read(10,GGV_DEFINITION_POS_Z); b.read(11,GGV_AIM_POS_Z); b.emit(GGV_F_SUB,10,10,11);
    b.emit(GGV_F_MUL,10,10,24); b.f(11,1000.0f); b.emit(GGV_F_DIV,10,10,11); b.write(GGV_POS_END_Z,10);
    b.emit(GGV_I_LT,18,6,0); auto fallingMotion=b.branch(18);
    b.emit(GGV_I_TO_F,10,6); b.f(11,1.5707963705063f); b.emit(GGV_F_MUL,10,10,11);
    b.emit(GGV_I_TO_F,11,0); b.emit(GGV_F_DIV,10,10,11); b.call(GGV_RECOIL_SIN,12,10);
    auto waveDone=b.jump(); b.end(fallingMotion);
    b.emit(GGV_I_SUB_WRAP,8,6,0); b.emit(GGV_I_TO_F,10,8); b.read(11,GGV_RECOIL_PI);
    b.emit(GGV_F_MUL,10,10,11); b.emit(GGV_I_TO_F,11,1); b.emit(GGV_F_DIV,10,10,11);
    b.call(GGV_RECOIL_COS,12,10); b.f(11,0.5f); b.emit(GGV_F_SCALE_BIAS,12,12,11,11);
    b.end(waveDone);
    b.read(10,GGV_ROTX_START); b.read(11,GGV_ROTX_END);
    b.s.nodes[b.emit(GGV_CALL,13,10,11,12)].service=GGV_RECOIL_TWEEN;
    b.write(GGV_ROTX_OFFSET,13); b.boolean(18,true); b.write(GGV_USE_POSROT,18);
    b.read(10,GGV_POS_END_X); b.read(11,GGV_POS_START_X);
    b.emit(GGV_F_BLEND,13,10,11,12); b.write(GGV_POS_OFFSET_X,13);
    b.read(10,GGV_POS_END_Y); b.read(11,GGV_POS_START_Y);
    b.emit(GGV_F_BLEND,13,10,11,12); b.write(GGV_POS_OFFSET_Y,13);
    b.read(10,GGV_POS_END_Z); b.read(11,GGV_POS_START_Z);
    b.emit(GGV_F_BLEND,13,10,11,12); b.write(GGV_POS_OFFSET_Z,13);
    b.read(10,GGV_ROTX_OFFSET); b.read(21,GGV_POS_MATRIX); b.call(GGV_MATRIX_X,0,10,21);
    b.read(22,GGV_POS_COORD); b.call(GGV_MATRIX_TRANSLATION,0,22,21);
    b.end(interruptedMotion); b.end(outsideMotion); b.end(skipMotion);
    b.emit(GGV_I_LE,18,5,6); auto recoveryPending=b.branch(18);
    b.i(8,0); b.emit(GGV_I_LT,18,3,8); b.emit(GGV_NOT,18,18); auto noEarlyRecovery=b.branch(18);
    b.read(18,GGV_TRIGGER_RELEASED); auto notReleased=b.branch(18);
    b.read(18,GGV_TRIGGER); auto notTriggered=b.branch(18); b.ret(1);
    b.end(notTriggered); b.end(notReleased); b.end(noEarlyRecovery);
    b.emit(GGV_I_ADD_WRAP,8,5,4); b.emit(GGV_I_LE,18,8,6); auto waitRecovery=b.branch(18); b.ret(1);
    b.end(waitRecovery); b.end(recoveryPending); b.ret(0); return b.s;
}
constexpr ggv_source attackShootSource() {
    Builder b; b.call(GGV_RESOLVE_FUNCTION,18); auto absent=b.branch(18); auto present=b.jump();
    b.end(absent); b.ret(1); b.end(present);
    b.read(0,GGV_STATE_MINOR); b.i(1,0); b.emit(GGV_I_EQ,18,0,1); auto notInitial=b.branch(18);
    b.call(GGV_OVERRIDE_GKEF); b.read(0,GGV_STATE_CYCLES); b.emit(GGV_I_EQ,18,0,1); auto laterCycle=b.branch(18);
    b.f(10,0.0f); b.write(GGV_SPOOL,10); b.read(21,GGV_FIRE_ANIMATION);
    b.emit(GGV_REF_NONNULL,18,21); auto noAnimation=b.branch(18);
    b.read(0,GGV_HAND_NUMBER); b.read(22,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,21,0,22)].service=GGV_FIRE_ANIMATION_START;
    b.boolean(18,true); b.write(GGV_ANIMATION_LATCH,18); b.end(noAnimation);
    b.i(0,0); b.write(GGV_BURST_BULLETS,0); b.end(laterCycle);
    b.read(22,GGV_PASSED_HAND); b.i(1,2); b.call(GGV_ATTACK_READY,18,22,1); auto notReady=b.branch(18);
    b.i(0,1); b.write(GGV_STATE_MINOR,0); b.end(notReady);
    b.read(10,GGV_SPOOL); b.write(GGV_MATMOT2,10); b.end(notInitial);
    b.read(0,GGV_STATE_MINOR); b.i(1,1); b.emit(GGV_I_EQ,18,0,1); auto notActive=b.branch(18);
    b.call(GGV_ATTACK_CONTROL,0); b.read(2,GGV_LEGACY_FUNCTION_TYPE); b.emit(GGV_I_TO_U,26,2);
    b.uop(GGV_U_AND_LITERAL,26,26,0xff00); b.emit(GGV_U_TO_I,2,26);
    b.i(1,0x100); b.emit(GGV_I_EQ,18,2,1); auto notAutomatic=b.branch(18);
    b.read(21,GGV_VIBRATION_START); b.emit(GGV_REF_NONNULL,18,21); auto noStart=b.branch(18);
    b.read(22,GGV_VIBRATION_MAX); b.emit(GGV_REF_NONNULL,18,22); auto noMax=b.branch(18);
    b.read(10,GGV_SPOOL); b.read(23,GGV_VIBRATION_SCRATCH); b.call4(GGV_VIBRATION_INTERPOLATE,21,22,10,23);
    b.read(24,GGV_VIBRATION_UPGRADE); b.read(25,GGV_VIBRATION_FINAL);
    b.s.nodes[b.emit(GGV_CALL,0,24,23,25)].service=GGV_VIBRATION_MULTIPLY;
    b.end(noMax); b.end(noStart); b.end(notAutomatic);
    b.i(1,0); b.emit(GGV_I_GT,18,0,1); auto noCommit=b.branch(18); b.call(GGV_ATTACK_COMMIT); b.end(noCommit);
    b.emit(GGV_I_LT,18,0,1); auto notNegative=b.branch(18); auto complete=b.jump(); b.end(notNegative);
    b.i(1,2); b.emit(GGV_I_EQ,18,0,1); auto notComplete=b.branch(18);
    b.end(complete); b.i(1,2); b.write(GGV_STATE_MINOR,1); b.end(notComplete);
    b.read(10,GGV_SPOOL); b.write(GGV_MATMOT2,10);
    b.read(18,GGV_TRIGGER); auto noTrigger=b.branch(18); b.read(10,GGV_MATMOT2); b.f(11,0.4f);
    b.emit(GGV_F_LT,18,10,11); auto adequateMotor=b.branch(18); b.write(GGV_MATMOT2,11);
    b.end(adequateMotor); b.end(noTrigger);
    b.read(18,GGV_TRIGGER_RELEASED); auto heldTrigger=b.branch(18);
    b.boolean(18,false); b.write(GGV_ANIMATION_LATCH,18); b.end(heldTrigger); b.ret(0); b.end(notActive);
    b.read(0,GGV_STATE_MINOR); b.i(1,2); b.emit(GGV_I_EQ,18,0,1); auto notRecovery=b.branch(18);
    b.read(26,GGV_STATE_FLAGS); b.s.nodes[b.emit(GGV_MASK,18,26)].literal.u=0x20;
    auto noRecoil=b.branch(18); b.call(GGV_ATTACK_RECOVERY,19); auto recoveryResult=b.jump();
    b.end(noRecoil); b.boolean(19,true); b.end(recoveryResult);
    b.read(18,GGV_SHOTGUN); auto notShotgun=b.branch(18); b.read(18,GGV_BIND_ANIMATION_BUSY); auto animationIdle=b.branch(18);
    b.boolean(19,false); b.end(animationIdle); b.end(notShotgun);
    b.read(10,GGV_SPOOL); b.write(GGV_MATMOT2,10); auto stillRecovering=b.branch(19);
    b.read(18,GGV_TRIGGER); b.emit(GGV_NOT,18,18); auto heldRecovery=b.branch(18);
    b.f(10,0.0f); b.write(GGV_MATMOT2,10); b.end(heldRecovery); b.end(stillRecovering);
    b.read(18,GGV_MAULER); auto notMauler=b.branch(18); b.f(10,0.0f); b.write(GGV_MATMOT1,10); b.end(notMauler);
    b.emit(GGV_RETURN_BOOL,0,19); b.end(notRecovery); b.ret(0); return b.s;
}

/* Complete throw driver: configuration precedence and every state branch are
 * typed control. Engine APIs remain explicit synchronous leaf services. */
constexpr ggv_source throwSource() {
    Builder b;
    b.call(GGV_RESOLVE_FUNCTION,20); b.call(GGV_RESOLVE_GRAPH,19);
    auto absent=b.branch(20); auto present=b.jump(); b.end(absent); b.ret(1); b.end(present);
    auto noFlags=b.branch(19); b.read(26,GGV_GRAPH_FLAGS); auto flagsReady=b.jump();
    b.end(noFlags); b.read(26,GGV_LEGACY_FLAGS); b.end(flagsReady);
    auto noSlot=b.branch(19); b.read(3,GGV_GRAPH_AMMO_SLOT); b.i(8,0); b.emit(GGV_I_LT,18,3,8);
    b.emit(GGV_NOT,18,18); auto negativeSlot=b.branch(18); auto slotReady=b.jump();
    b.end(noSlot); b.end(negativeSlot); b.read(3,GGV_LEGACY_AMMO_INDEX); b.end(slotReady); b.write(GGV_AMMO_INDEX,3);
    auto noActivationGraph=b.branch(19); b.read(18,GGV_THROW_GRAPH_HAS_ACTIVATION); auto noActivation=b.branch(18);
    b.read(0,GGV_THROW_GRAPH_ACTIVATION); auto activationReady=b.jump();
    b.end(noActivationGraph); b.end(noActivation); b.read(0,GGV_THROW_LEGACY_ACTIVATION); b.end(activationReady);
    auto noRecoveryGraph=b.branch(19); b.read(18,GGV_THROW_GRAPH_HAS_RECOVERY); auto noRecovery=b.branch(18);
    b.read(1,GGV_THROW_GRAPH_RECOVERY); auto recoveryReady=b.jump();
    b.end(noRecoveryGraph); b.end(noRecovery); b.read(1,GGV_THROW_LEGACY_RECOVERY); b.end(recoveryReady);
    b.read(4,GGV_STATE_MINOR); b.i(8,0); b.emit(GGV_I_EQ,18,4,8); auto notInitial=b.branch(18);
    b.read(4,GGV_STATE_CYCLES); b.emit(GGV_I_EQ,18,4,8); auto notFirstCycle=b.branch(18);
    b.s.nodes[b.emit(GGV_MASK,18,26)].literal.u=0x40000; auto notDiscard=b.branch(18);
    b.read(4,GGV_WEAPON_NUMBER); b.call(GGV_THROW_REMOVE_ITEM,0,4);
    b.boolean(18,true); b.write(GGV_CURRENT_PLAYER_THROWING,18);
    b.read(18,GGV_VERSION_1_OR_LATER); auto oldVersion=b.branch(18);
    b.call(GGV_THROW_SWITCH_PREVIOUS); auto switchComplete=b.jump();
    b.end(oldVersion); b.call(GGV_THROW_AUTO_SWITCH); b.end(switchComplete);
    b.i(4,0); b.write(GGV_PRIME_TIMER,4); b.ret(1); b.end(notDiscard);
    b.read(21,GGV_FIRE_ANIMATION); b.emit(GGV_REF_NONNULL,18,21); auto noInitialAnimation=b.branch(18);
    b.read(4,GGV_HAND_NUMBER); b.read(22,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,21,4,22)].service=GGV_FIRE_ANIMATION_START;
    b.boolean(18,true); b.write(GGV_ANIMATION_LATCH,18); b.end(noInitialAnimation); b.end(notFirstCycle);
    b.read(21,GGV_FIRE_ANIMATION); b.emit(GGV_REF_NONNULL,18,21); auto noAnimation=b.branch(18);
    b.read(18,GGV_TRIGGER_RELEASED); auto notReleased=b.branch(18);
    b.boolean(18,false); b.write(GGV_ANIMATION_LATCH,18); b.end(notReleased);
    b.read(22,GGV_PASSED_HAND); b.i(4,2); b.call(GGV_ATTACK_READY,18,22,4); auto animationPending=b.branch(18);
    b.i(4,1); b.write(GGV_STATE_MINOR,4); b.boolean(18,false); b.write(GGV_ANIMATION_LATCH,18); b.end(animationPending);
    auto animationComplete=b.jump(); b.end(noAnimation); b.i(4,1); b.write(GGV_STATE_MINOR,4);
    b.end(animationComplete); b.end(notInitial);
    b.read(4,GGV_STATE_MINOR); b.i(8,1); b.emit(GGV_I_EQ,18,4,8); auto notFiring=b.branch(18);
    b.boolean(18,true); b.write(GGV_FIRING,18); b.i(4,3); b.write(GGV_ATTACK_TYPE,4);
    b.i(8,0); b.emit(GGV_I_LT,18,3,8); b.emit(GGV_NOT,18,18); auto noAmmo=b.branch(18);
    b.read(4,GGV_LOADED_AMMO); b.i(8,1); b.emit(GGV_I_SUB_WRAP,4,4,8); b.write(GGV_LOADED_AMMO,4); b.end(noAmmo);
    b.i(4,2); b.write(GGV_STATE_MINOR,4); b.ret(0); b.end(notFiring);
    b.read(4,GGV_STATE_MINOR); b.i(8,2); b.emit(GGV_I_EQ,18,4,8); auto notRecovering=b.branch(18);
    b.read(4,GGV_STATE_FRAMES); b.call(GGV_RECOIL_TICKS,9,1); b.emit(GGV_I_GT,18,4,9); auto recoveryPending=b.branch(18);
    b.ret(1); b.end(recoveryPending);
    b.read(4,GGV_WEAPON_NUMBER); b.i(8,34); b.emit(GGV_I_EQ,18,4,8); auto notRemoteMine=b.branch(18);
    b.call(GGV_THROW_SECONDARY_VALUE,4); b.i(8,1); b.emit(GGV_I_EQ,18,4,8); auto notSecondary=b.branch(18);
    b.read(18,GGV_TRIGGER_RELEASED); auto recoveryNotReleased=b.branch(18);
    b.read(18,GGV_TRIGGER); auto recoveryNotTriggered=b.branch(18); b.ret(1);
    b.end(recoveryNotTriggered); b.end(recoveryNotReleased); b.end(notSecondary); b.end(notRemoteMine); b.ret(0); b.end(notRecovering);
    b.read(4,GGV_STATE_MINOR); b.i(8,55); b.emit(GGV_I_EQ,18,4,8); auto notGrenadeWait=b.branch(18);
    b.read(22,GGV_PASSED_HAND); b.call(GGV_THROW_RESET_ANIMATION,0,22);
    b.read(4,GGV_STATE_FRAMES); b.i(8,240); b.emit(GGV_I_ADD_WRAP,9,0,8); b.call(GGV_RECOIL_TICKS,9,9);
    b.emit(GGV_I_GT,18,4,9); auto stillWaiting=b.branch(18); b.ret(1); b.end(stillWaiting); b.ret(0); b.end(notGrenadeWait);
    b.read(4,GGV_STATE_FRAMES); b.write(GGV_PRIME_TIMER,4);
    b.read(4,GGV_WEAPON_NUMBER); b.i(8,30); b.emit(GGV_I_EQ,18,4,8); auto notGrenade=b.branch(18);
    b.read(4,GGV_WEAPON_FUNCTION); b.i(8,0); b.emit(GGV_I_EQ,18,4,8); auto notPrimary=b.branch(18);
    b.read(4,GGV_PRIME_TIMER); b.call(GGV_RECOIL_TICKS,9,0); b.emit(GGV_I_GT,18,4,9); auto unexpired=b.branch(18);
    b.boolean(18,true); b.write(GGV_FIRING,18); b.i(4,3); b.write(GGV_ATTACK_TYPE,4);
    b.i(8,0); b.emit(GGV_I_LT,18,3,8); b.emit(GGV_NOT,18,18); auto expiryNoAmmo=b.branch(18);
    b.read(4,GGV_LOADED_AMMO); b.i(8,1); b.emit(GGV_I_SUB_WRAP,4,4,8); b.write(GGV_LOADED_AMMO,4); b.end(expiryNoAmmo);
    b.i(4,55); b.write(GGV_STATE_MINOR,4); b.ret(0); b.end(unexpired); b.end(notPrimary); b.end(notGrenade); b.ret(0);
    return b.s;
}

/* Pure complete scalar utility: no metadata resolver or effect service. */
constexpr ggv_source minClipSource() {
    Builder b;
    b.read(0,GGV_WEAPON_NUMBER); b.i(1,28); b.emit(GGV_I_EQ,2,0,1); auto notTranquilizer=b.branch(2);
    b.read(0,GGV_WEAPON_FUNCTION); b.i(1,1); b.emit(GGV_I_EQ,2,0,1); auto notSecondary=b.branch(2);
    b.ret(4); b.end(notTranquilizer); b.end(notSecondary); b.ret(1);
    return b.s;
}
/* Full melee driver; ordered queries/stores remain visible typed operations.
 * Registers are fixed-type and witnesses never escape this synchronous call. */
constexpr ggv_source meleeSource() {
    Builder b;
    b.call(GGV_RESOLVE_FUNCTION,0); b.call(GGV_RESOLVE_GRAPH,1);
    auto absent=b.branch(0); auto present=b.jump(); b.end(absent); b.ret(1); b.end(present);
    auto noGraph=b.branch(1); b.read(6,GGV_GRAPH_AMMO_SLOT); b.i(4,0); b.emit(GGV_I_LT,2,6,4);
    b.emit(GGV_NOT,2,2); auto negativeSlot=b.branch(2); auto slotReady=b.jump();
    b.end(noGraph); b.end(negativeSlot); b.read(6,GGV_LEGACY_AMMO_INDEX); b.end(slotReady); b.write(GGV_AMMO_INDEX,6);
    b.read(5,GGV_WEAPON_NUMBER); b.i(4,20); b.emit(GGV_I_EQ,2,5,4); auto notReaper=b.branch(2);
    b.read(5,GGV_STATE_CYCLES); b.i(4,0); b.emit(GGV_I_EQ,2,5,4); auto notFirst=b.branch(2);
    b.f(11,0.1f); b.write(GGV_MATMOT2,11); b.i(5,0); b.write(GGV_BURST_BULLETS,5); b.end(notFirst);
    b.boolean(2,true); b.write(GGV_FIRING,2); b.i(5,4); b.write(GGV_ATTACK_TYPE,5);
    b.read(5,GGV_BURST_BULLETS); b.emit(GGV_I_INCREMENT,5,5); b.write(GGV_BURST_BULLETS,5);
    b.read(2,GGV_TRIGGER); auto releasedReaper=b.branch(2);
    b.read(11,GGV_MATMOT2); b.f(12,0.01f); b.call(GGV_MELEE_CLOCK,13);
    b.emit(GGV_F_ADD_PRODUCT,11,11,12,13); b.write(GGV_MATMOT2,11);
    b.read(11,GGV_MATMOT2); b.f(14,1.0f); b.emit(GGV_F_GT,2,11,14); auto belowCap=b.branch(2);
    b.write(GGV_MATMOT2,14); b.end(belowCap); b.ret(0);
    b.end(releasedReaper); b.f(11,0.0f); b.write(GGV_MATMOT2,11); b.ret(1); b.end(notReaper);
    b.read(5,GGV_STATE_MINOR); b.i(4,0); b.emit(GGV_I_EQ,2,5,4); auto notInitial=b.branch(2);
    b.read(5,GGV_STATE_CYCLES); b.emit(GGV_I_EQ,2,5,4); auto notInitialCycle=b.branch(2);
    b.boolean(2,true); b.write(GGV_FIRING,2); b.i(5,10); b.write(GGV_ATTACK_TYPE,5);
    b.read(16,GGV_FIRE_ANIMATION); b.emit(GGV_REF_NONNULL,2,16); auto noStartAnimation=b.branch(2);
    b.read(16,GGV_FIRE_ANIMATION); b.read(9,GGV_HAND_NUMBER); b.read(15,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,16,9,15)].service=GGV_FIRE_ANIMATION_START;
    b.boolean(2,true); b.write(GGV_ANIMATION_LATCH,2); b.end(noStartAnimation); b.end(notInitialCycle);
    b.read(16,GGV_FIRE_ANIMATION); b.emit(GGV_REF_NONNULL,2,16); auto noAnimation=b.branch(2);
    b.read(2,GGV_TRIGGER_RELEASED); auto notReleased=b.branch(2);
    b.boolean(2,false); b.write(GGV_ANIMATION_LATCH,2); b.end(notReleased);
    b.read(15,GGV_PASSED_HAND); b.i(4,2); b.call(GGV_ATTACK_READY,2,15,4); auto notReady=b.branch(2);
    b.i(5,1); b.write(GGV_STATE_MINOR,5); b.boolean(2,false); b.write(GGV_ANIMATION_LATCH,2); b.end(notReady);
    auto animationJoin=b.jump(); b.end(noAnimation); b.i(5,1); b.write(GGV_STATE_MINOR,5);
    b.end(animationJoin); b.end(notInitial);
    b.read(5,GGV_STATE_MINOR); b.i(4,3); b.emit(GGV_I_EQ,2,5,4); auto notRetry=b.branch(2);
    b.read(15,GGV_PASSED_HAND); b.call(GGV_ATTACK_READY,2,15,4); auto retryPending=b.branch(2);
    b.i(5,1); b.write(GGV_STATE_MINOR,5); b.boolean(2,false); b.write(GGV_ANIMATION_LATCH,2);
    b.end(retryPending); b.end(notRetry);
    b.read(5,GGV_STATE_MINOR); b.i(4,1); b.emit(GGV_I_EQ,2,5,4); auto notActive=b.branch(2);
    b.boolean(2,true); b.write(GGV_FIRING,2); b.i(5,4); b.write(GGV_ATTACK_TYPE,5);
    b.read(5,GGV_WEAPON_NUMBER); b.i(4,28); b.emit(GGV_I_EQ,2,5,4); auto notTranq=b.branch(2);
    b.i(4,0); b.emit(GGV_I_LT,2,6,4); b.emit(GGV_NOT,2,2); auto noAmmo=b.branch(2);
    b.read(7,GGV_LOADED_AMMO); b.i(4,28); b.i(18,1); b.call(GGV_MELEE_MIN_CLIP,8,4,18);
    b.emit(GGV_I_GT,2,7,8); auto insufficient=b.branch(2);
    b.read(7,GGV_LOADED_AMMO); b.call(GGV_MELEE_MIN_CLIP,8,4,18);
    b.emit(GGV_I_SUB_WRAP,7,7,8); b.write(GGV_LOADED_AMMO,7); auto debitJoin=b.jump();
    b.end(insufficient); b.i(7,0); b.write(GGV_LOADED_AMMO,7);
    b.end(debitJoin); b.end(noAmmo); b.end(notTranq);
    b.read(16,GGV_FIRE_ANIMATION); b.emit(GGV_REF_NONNULL,2,16); auto noActiveAnimation=b.branch(2);
    b.read(16,GGV_FIRE_ANIMATION); b.emit(GGV_REF_NONNULL,2,16); auto disappearedAnimation=b.branch(2);
    b.read(15,GGV_PASSED_HAND); b.i(4,3); b.call(GGV_ATTACK_READY,2,15,4);
    b.emit(GGV_NOT,2,2); auto activeReady=b.branch(2); b.i(5,3); b.write(GGV_STATE_MINOR,5); auto activeJoin=b.jump();
    b.end(disappearedAnimation); b.end(activeReady); b.i(5,2); b.write(GGV_STATE_MINOR,5);
    b.end(activeJoin); b.end(noActiveAnimation);
    b.i(4,0); b.call(GGV_MELEE_CHEAT_ACTIVE,2,4); auto noCheat=b.branch(2);
    b.i(5,2); b.write(GGV_STATE_MINOR,5); b.end(noCheat); b.ret(0); b.end(notActive);
    b.read(5,GGV_STATE_MINOR); b.i(4,2); b.emit(GGV_I_EQ,2,5,4); auto notRecovering=b.branch(2);
    b.read(15,GGV_PASSED_HAND); b.call(GGV_MELEE_ANIM_BUSY,2,15); auto idle=b.branch(2);
    b.i(4,0); b.call(GGV_MELEE_CHEAT_ACTIVE,2,4); auto noRecoveryCheat=b.branch(2);
    b.read(5,GGV_WEAPON_NUMBER); b.i(4,1); b.emit(GGV_I_EQ,2,5,4); auto notUnarmed=b.branch(2); b.ret(1);
    b.end(noRecoveryCheat); b.end(notUnarmed);
    b.read(5,GGV_STATE_FRAMES); b.i(4,60); b.call(GGV_RECOIL_TICKS,8,4); b.emit(GGV_I_GT,2,5,8);
    b.emit(GGV_RETURN_BOOL,0,2); b.end(idle); b.ret(1); b.end(notRecovering); b.ret(0);
    return b.s;
}

/* Entire special driver: lazy raw overrides followed by ordered hand stores. */
constexpr ggv_source specialSource() {
    Builder b;b.call(GGV_RESOLVE_FUNCTION,0);b.call(GGV_RESOLVE_GRAPH,1);
    auto absent=b.branch(0);auto present=b.jump();b.end(absent);b.ret(1);b.end(present);
    auto noSpecialGraph=b.branch(1);b.read(2,GGV_SPECIAL_GRAPH_HAS_VALUE);auto noSpecial=b.branch(2);
    b.read(3,GGV_SPECIAL_GRAPH_VALUE);auto valueReady=b.jump();
    b.end(noSpecialGraph);b.end(noSpecial);b.read(3,GGV_SPECIAL_LEGACY_VALUE);b.end(valueReady);
    auto noAmmoGraph=b.branch(1);b.read(4,GGV_GRAPH_AMMO_SLOT);b.i(6,0);b.emit(GGV_I_LT,2,4,6);
    b.emit(GGV_NOT,2,2);auto negativeSlot=b.branch(2);auto slotReady=b.jump();
    b.end(noAmmoGraph);b.end(negativeSlot);b.read(4,GGV_LEGACY_AMMO_INDEX);b.end(slotReady);b.write(GGV_AMMO_INDEX,4);
    auto noRecoveryGraph=b.branch(1);b.read(2,GGV_SPECIAL_GRAPH_HAS_RECOVERY);auto noRecovery=b.branch(2);
    b.read(5,GGV_SPECIAL_GRAPH_RECOVERY);auto recoveryReady=b.jump();
    b.end(noRecoveryGraph);b.end(noRecovery);b.read(5,GGV_SPECIAL_LEGACY_RECOVERY);b.end(recoveryReady);
    b.read(7,GGV_STATE_MINOR);b.i(6,0);b.emit(GGV_I_EQ,2,7,6);auto notStart=b.branch(2);
    b.i(7,1);b.write(GGV_STATE_MINOR,7);b.end(notStart);
    b.read(7,GGV_STATE_MINOR);b.i(6,1);b.emit(GGV_I_EQ,2,7,6);auto notExecute=b.branch(2);
    b.boolean(2,true);b.write(GGV_FIRING,2);b.write(GGV_ATTACK_TYPE,3);
    b.i(6,0);b.emit(GGV_I_LT,2,4,6);b.emit(GGV_NOT,2,2);auto noAmmo=b.branch(2);
    b.read(7,GGV_LOADED_AMMO);b.i(6,1);b.emit(GGV_I_SUB_WRAP,7,7,6);b.write(GGV_LOADED_AMMO,7);b.end(noAmmo);
    b.i(7,2);b.write(GGV_STATE_MINOR,7);b.ret(0);b.end(notExecute);
    b.read(7,GGV_STATE_MINOR);b.i(6,2);b.emit(GGV_I_EQ,2,7,6);auto notRecovering=b.branch(2);
    b.read(7,GGV_STATE_FRAMES);b.call(GGV_RECOIL_TICKS,8,5);b.emit(GGV_I_GT,2,7,8);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(notRecovering);b.ret(0);return b.s;
}
/* A pitched click is composed from individual priority/start/event leaves.
 * This helper only emits source nodes; it is not a native sound-branch callback. */
constexpr void emptyPitchedClick(Builder &b,float pitch,ggv_binding sound) {
    b.f(12,pitch);b.i(9,0);b.read(3,GGV_PRIORITY_ENABLED);auto noPriority=b.branch(3);
    b.call(GGV_CURRENT_PRIORITY,9);b.call(GGV_AUDIO_PRIORITY,10);b.emit(GGV_I_INCREMENT,10,10);b.call(GGV_SET_PRIORITY,0,10);b.end(noPriority);
    b.read(11,sound);b.call(GGV_EMPTY_SOUND_START,18,11);b.emit(GGV_REF_NONNULL,3,18);auto noHandle=b.branch(3);
    b.emit(GGV_F_BITS_I,13,12);b.call(GGV_SOUND_EVENT,0,18,13);b.end(noHandle);
    b.read(3,GGV_PRIORITY_ENABLED);auto noRestore=b.branch(3);b.call(GGV_SET_PRIORITY,0,9);b.end(noRestore);
}
/* Full empty-attack driver. Both original switches and all fallthroughs are
 * typed control; no entire sound branch or state tail is delegated. */
constexpr ggv_source emptyAttackSource() {
    Builder b;b.boolean(0,false);b.read(4,GGV_WEAPON_NUMBER);
    // Exact native visible-trigger set, evaluated from one info weapon read.
    unsigned visible[15]{};unsigned index=0;
    for(auto weapon : {2,3,4,5,6,7,8,9,10,11,12,13,14,20,28}) {
        b.i(5,weapon);b.emit(GGV_I_EQ,3,4,5);auto next=b.branch(3);visible[index++]=b.jump();b.end(next);
    }
    // Default class: play sound BEFORE frame debit/reset, then common tail.
    b.read(6,GGV_STATE_FRAMES);b.i(5,25);b.call(GGV_RECOIL_TICKS,7,5);b.emit(GGV_I_GT,3,6,7);auto defaultTooSoon=b.branch(3);
    b.boolean(0,true);b.read(6,GGV_STATE_FRAMES);b.call(GGV_RECOIL_TICKS,7,5);b.emit(GGV_I_SUB_WRAP,6,6,7);b.write(GGV_STATE_FRAMES,6);
    b.i(5,0);b.emit(GGV_I_TO_U,21,5);b.write(GGV_STATE_FLAGS,21);b.read(17,GGV_PASSED_HAND);b.call(GGV_EMPTY_RESET_ANIM,0,17);
    b.end(defaultTooSoon);auto defaultDone=b.jump();
    for(auto edge : visible)b.end(edge);
    // Visible-trigger class: initial debit/reset precedes animation-mode query.
    b.read(6,GGV_STATE_FRAMES);b.i(5,25);b.call(GGV_RECOIL_TICKS,7,5);b.emit(GGV_I_GT,3,6,7);auto notExpired=b.branch(3);
    b.read(6,GGV_STATE_FRAMES);b.call(GGV_RECOIL_TICKS,7,5);b.emit(GGV_I_SUB_WRAP,6,6,7);b.write(GGV_STATE_FRAMES,6);
    b.i(5,0);b.emit(GGV_I_TO_U,21,5);b.write(GGV_STATE_FLAGS,21);b.read(17,GGV_PASSED_HAND);b.call(GGV_EMPTY_RESET_ANIM,0,17);b.end(notExpired);
    b.read(6,GGV_EMPTY_ANIM_MODE);b.i(5,2);b.emit(GGV_I_EQ,3,6,5);auto notBusy=b.branch(3);
    b.read(17,GGV_PASSED_HAND);b.i(5,5);b.call(GGV_ATTACK_READY,3,17,5);auto busyPending=b.branch(3);b.boolean(0,true);
    b.end(busyPending);auto busyDone=b.jump();b.end(notBusy);b.boolean(1,false);
    b.read(21,GGV_STATE_FLAGS);b.uop(GGV_MASK,3,21,0x10);b.emit(GGV_NOT,3,3);auto held=b.branch(3);
    b.read(18,GGV_EMPTY_DEFINITION);b.emit(GGV_REF_NONNULL,3,18);auto noDefinition=b.branch(3);
    b.call(GGV_RESOLVE_FUNCTION,2);auto noFunction=b.branch(2);
    b.read(18,GGV_FIRE_ANIMATION);b.emit(GGV_REF_NONNULL,3,18);auto noAnimation=b.branch(3);
    b.read(18,GGV_FIRE_ANIMATION);b.read(8,GGV_HAND_NUMBER);b.read(17,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,18,8,17)].service=GGV_FIRE_ANIMATION_START;b.boolean(1,true);
    b.end(noAnimation);b.end(noFunction);b.end(noDefinition);b.end(held);
    b.emit(GGV_NOT,3,1);auto restarted=b.branch(3);
    b.read(6,GGV_STATE_FRAMES);b.i(5,25);b.call(GGV_RECOIL_TICKS,7,5);b.emit(GGV_I_GT,3,6,7);auto idleTooSoon=b.branch(3);b.boolean(0,true);
    b.end(idleTooSoon);b.end(restarted);b.end(busyDone);b.end(defaultDone);
    b.i(5,13);b.emit(GGV_I_TO_U,21,5);b.write(GGV_EMPTY_MODE,21);
    b.i(5,0);b.write(GGV_EMPTY_COUNT60,5);b.write(GGV_EMPTY_COUNT,5);
    auto silent=b.branch(0);b.read(21,GGV_STATE_FLAGS);b.uop(GGV_MASK,3,21,0x10);b.emit(GGV_NOT,3,3);auto alreadyClicked=b.branch(3);
    b.read(21,GGV_STATE_FLAGS);b.uop(GGV_U_OR_LITERAL,21,21,0x10);b.write(GGV_STATE_FLAGS,21);
    // Re-read info weapon for the original second switch AFTER immediate stores.
    b.read(4,GGV_WEAPON_NUMBER);
    unsigned maian[3]{};index=0;
    for(auto weapon : {7,12,22}) {b.i(5,weapon);b.emit(GGV_I_EQ,3,4,5);auto next=b.branch(3);maian[index++]=b.jump();b.end(next);}
    unsigned faster[2]{};index=0;
    for(auto weapon : {28,36}) {b.i(5,weapon);b.emit(GGV_I_EQ,3,4,5);auto next=b.branch(3);faster[index++]=b.jump();b.end(next);}
    unsigned noSounds[8]{};index=0;
    for(auto weapon : {1,26,30,31,32,33,34,35}) {b.i(5,weapon);b.emit(GGV_I_EQ,3,4,5);auto next=b.branch(3);noSounds[index++]=b.jump();b.end(next);}
    b.read(11,GGV_EMPTY_CLICK_SOUND);b.call(GGV_EMPTY_SOUND_START,18,11);auto defaultSoundDone=b.jump();
    for(auto edge : maian)b.end(edge);
    emptyPitchedClick(b,2.07f,GGV_EMPTY_WATER_SOUND);
    // Intentional preservation of the original unbroken Maian fallthrough.
    for(auto edge : faster)b.end(edge);
    emptyPitchedClick(b,1.5f,GGV_EMPTY_CLICK_SOUND);
    for(auto edge : noSounds)b.end(edge);
    b.end(defaultSoundDone);b.end(alreadyClicked);b.end(silent);
    b.read(3,GGV_TRIGGER);b.emit(GGV_NOT,3,3);auto stillHeld=b.branch(3);
    b.i(5,0);b.emit(GGV_I_TO_U,21,5);b.write(GGV_EMPTY_MODE,21);b.write(GGV_EMPTY_COUNT60,5);b.write(GGV_EMPTY_COUNT,5);
    b.read(8,GGV_HAND_NUMBER);b.i(5,0);b.call(GGV_EMPTY_SET_STATE,3,8,5);auto stateRefused=b.branch(3);
    b.read(6,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,6);b.end(stateRefused);
    b.read(17,GGV_PASSED_HAND);b.call(GGV_EMPTY_RESET_ANIM,0,17);b.end(stillHeld);b.ret(0);
    return b.s;
}

/* Whole state utility: address selection is a narrow native primitive, while
 * validation, resets and logging admission remain visible typed control. */
constexpr ggv_source setStateSource() {
    Builder b;b.boolean(0,true);b.call(GGV_STATE_SELECT_HAND);b.read(4,GGV_STATE_VALUE);
    b.read(5,GGV_STATE_REQUEST);b.i(7,7);b.emit(GGV_I_EQ,1,5,7);auto notChangeFunction=b.branch(1);
    b.read(6,GGV_WEAPON_FUNCTION);b.i(7,1);b.emit(GGV_I_SUB_WRAP,6,7,6);
    b.call(GGV_STATE_ALTERNATE_FUNCTION,10,6);b.emit(GGV_REF_NONNULL,1,10);auto noAlternate=b.branch(1);
    auto alternateReady=b.jump();b.end(noAlternate);b.boolean(0,false);b.end(alternateReady);b.end(notChangeFunction);
    auto invalid=b.branch(0);b.write(GGV_STATE_VALUE,5);b.i(6,0);b.write(GGV_STATE_FRAMES,6);
    b.emit(GGV_I_TO_U,9,6);b.write(GGV_STATE_FLAGS,9);b.write(GGV_STATE_CYCLES,6);b.write(GGV_STATE_MINOR,6);b.write(GGV_STATE_LAST_FRAME,6);b.end(invalid);
    b.read(7,GGV_STATE_PLAYER_NUMBER);b.i(6,0);b.emit(GGV_I_EQ,1,7,6);auto otherPlayer=b.branch(1);
    auto notValid=b.branch(0);auto admittedLog=b.jump();b.end(notValid);
    b.read(5,GGV_STATE_REQUEST);b.i(6,7);b.emit(GGV_I_EQ,1,5,6);auto notRejectedChange=b.branch(1);b.end(admittedLog);
    b.read(8,GGV_HAND_NUMBER);b.read(7,GGV_WEAPON_NUMBER);b.write(GGV_STATE_LOG_WEAPON,7);
    b.read(7,GGV_STATE_INUSE);b.write(GGV_STATE_LOG_INUSE,7);
    b.s.nodes[b.emit(GGV_CALL,0,8,4,0)].service=GGV_STATE_LOG;
    b.end(notRejectedChange);b.end(otherPlayer);b.emit(GGV_RETURN_BOOL,0,0);return b.s;
}
/* Complete outer attack driver, composing the four already converted drivers.
 * No dispatch policy is delegated to a whole native-body callback. */
constexpr ggv_source attackDispatchSource() {
    Builder b;b.boolean(0,true);b.boolean(3,false);b.call(GGV_RESOLVE_GRAPH,1);
    b.read(10,GGV_EMPTY_DEFINITION);b.emit(GGV_REF_NONNULL,2,10);auto noDefinition=b.branch(2);
    b.call(GGV_RESOLVE_FUNCTION,3);b.end(noDefinition);auto noFunction=b.branch(3);
    auto noGraph=b.branch(1);b.read(2,GGV_GRAPH_HAS_TYPE);auto noGraphType=b.branch(2);
    b.read(4,GGV_GRAPH_FUNCTION_TYPE);auto typeReady=b.jump();b.end(noGraph);b.end(noGraphType);
    b.read(4,GGV_LEGACY_FUNCTION_TYPE);b.end(typeReady);
    b.emit(GGV_I_TO_U,9,4);b.uop(GGV_U_AND_LITERAL,9,9,0xff);b.emit(GGV_U_TO_I,4,9);
    b.i(5,1);b.emit(GGV_I_EQ,2,4,5);auto notShoot=b.branch(2);b.call(GGV_DISPATCH_SHOOT,0);auto shootDone=b.jump();b.end(notShoot);
    b.i(5,2);b.emit(GGV_I_EQ,2,4,5);auto notThrow=b.branch(2);b.call(GGV_DISPATCH_THROW,0);auto throwDone=b.jump();b.end(notThrow);
    b.i(5,3);b.emit(GGV_I_EQ,2,4,5);auto notMelee=b.branch(2);b.call(GGV_DISPATCH_MELEE,0);auto meleeDone=b.jump();b.end(notMelee);
    b.i(5,4);b.emit(GGV_I_EQ,2,4,5);auto notSpecial=b.branch(2);b.call(GGV_DISPATCH_SPECIAL,0);
    b.end(notSpecial);b.end(meleeDone);b.end(throwDone);b.end(shootDone);b.end(noFunction);
    auto unfinished=b.branch(0);b.read(4,GGV_WEAPON_NUMBER);b.i(5,20);b.emit(GGV_I_EQ,2,4,5);auto notReaper=b.branch(2);
    b.read(2,GGV_TRIGGER);auto notTriggered=b.branch(2);b.i(4,1);b.write(GGV_WEAPON_FUNCTION,4);b.boolean(0,false);
    b.end(notTriggered);b.end(notReaper);auto noFinalState=b.branch(0);
    b.read(8,GGV_HAND_NUMBER);b.i(5,0);b.call(GGV_EMPTY_SET_STATE,2,8,5);auto stateRefused=b.branch(2);
    b.read(4,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,4);
    b.end(stateRefused);b.end(noFinalState);b.end(unfinished);b.ret(0);return b.s;
}

/* Complete eight-store native reset; NULL retires only the borrowed witness. */
constexpr ggv_source animationResetSource() {
    Builder b;b.i(0,-1);b.write(GGV_ANIM_LOAD,0);b.i(0,0);b.write(GGV_EMPTY_ANIM_MODE,0);
    b.boolean(1,false);b.write(GGV_ANIMATION_LATCH,1);b.write(GGV_ANIM_INCREMENTAL,1);
    b.nullref(2);b.write(GGV_ANIM_COMMAND,2);b.write(GGV_ANIM_LOOP_COUNT,0);
    b.write(GGV_ANIM_BIT02,1);b.write(GGV_ANIM_BIT07,1);b.ret(0);return b.s;
}
constexpr ggv_source animationBusySource() {
    Builder b;b.read(0,GGV_EMPTY_ANIM_MODE);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);
    b.emit(GGV_NOT,2,2);b.emit(GGV_RETURN_BOOL,0,2);return b.s;
}
/* Raw BOOL32 on/prev stores precede the logical short-circuit argument reads.
 * Native C logical results0/1 cross the existing raw BOOL32 effect4 gateway. */
constexpr ggv_source triggerInputSource() {
    Builder b;b.call(GGV_STATE_SELECT_HAND);b.read(0,GGV_TRIGGER);b.write(GGV_TRIGGER_PREVIOUS,0);
    b.read(0,GGV_TRIGGER_REQUEST);b.write(GGV_TRIGGER,0);
    b.boolean(2,false);b.read(0,GGV_TRIGGER_REQUEST);auto notOn=b.branch(0);
    b.read(1,GGV_TRIGGER_PREVIOUS);b.emit(GGV_NOT,2,1);b.end(notOn);
    b.boolean(3,false);b.read(0,GGV_TRIGGER_REQUEST);b.emit(GGV_NOT,4,0);auto on=b.branch(4);
    b.read(1,GGV_TRIGGER_PREVIOUS);b.emit(GGV_NOT,1,1);b.emit(GGV_NOT,3,1);b.end(on);
    b.read(5,GGV_TRIGGER_PLAYER);b.read(6,GGV_HAND_NUMBER);b.call4(GGV_TRIGGER_V2_INPUT,5,6,2,3);
    b.read(0,GGV_TRIGGER_REQUEST);b.emit(GGV_NOT,4,0);auto notReleased=b.branch(4);
    b.boolean(1,true);b.write(GGV_TRIGGER_RELEASED,1);b.end(notReleased);b.ret(0);return b.s;
}

/* Emit the exact two-hand/two-slot native traversal, not a runtime loop VM. */
constexpr void ammoCountFields(Builder &b,bool reserved) {
    b.read(0,GGV_AMMO_RESERVE);b.call(GGV_AMMO_SELECT_PLAYER);b.read(4,GGV_AMMO_REQUEST);
    for(auto hand : {0,1}) {
        b.i(3,hand);b.write(GGV_AMMO_HAND_INDEX,3);b.read(2,GGV_AMMO_HAND_INUSE);auto inactive=b.branch(2);
        for(auto slot : {0,1}) {
            b.i(5,slot);b.write(GGV_AMMO_SLOT_INDEX,5);b.read(6,GGV_AMMO_EQUIPPED_TYPE);b.emit(GGV_I_EQ,2,6,4);auto differentAmmo=b.branch(2);
            if(reserved) {
                b.read(6,GGV_AMMO_HAND_WEAPON);b.i(7,1);b.emit(GGV_I_TO_U,8,7);
                b.s.nodes[b.emit(GGV_CALL,2,6,5,8)].service=GGV_AMMO_HAS_FLAG;
                auto notReserved=b.branch(2);b.read(6,GGV_AMMO_LOADED);b.emit(GGV_I_ADD_WRAP,0,0,6);b.end(notReserved);
            } else {
                b.read(6,GGV_AMMO_LOADED);b.emit(GGV_I_ADD_WRAP,0,0,6);
            }
            b.end(differentAmmo);
        }
        b.end(inactive);
    }
    b.emit(GGV_RETURN_I32,0,0);
}
constexpr ggv_source ammoCountSource(){Builder b;ammoCountFields(b,false);return b.s;}
constexpr ggv_source reservedAmmoSource(){Builder b;ammoCountFields(b,true);return b.s;}
/* Complete original exclusion predicate; no new weapon-null/index policy. */
constexpr ggv_source ammoEligibilitySource() {
    Builder b;b.read(10,GGV_AMMO_FUNCTION_REF);b.emit(GGV_REF_NONNULL,2,10);auto absent=b.branch(2);auto present=b.jump();b.end(absent);b.ret(1);b.end(present);
    b.read(4,GGV_LEGACY_FUNCTION_TYPE);b.i(5,0);b.emit(GGV_I_EQ,2,4,5);auto notNone=b.branch(2);b.ret(1);b.end(notNone);
    b.read(4,GGV_LEGACY_FUNCTION_TYPE);b.emit(GGV_I_TO_U,8,4);b.uop(GGV_U_AND_LITERAL,8,8,0xff);b.emit(GGV_U_TO_I,4,8);
    b.i(5,3);b.emit(GGV_I_EQ,2,4,5);auto notMelee=b.branch(2);b.ret(1);b.end(notMelee);
    b.read(4,GGV_LEGACY_FUNCTION_TYPE);b.emit(GGV_I_TO_U,8,4);b.uop(GGV_U_AND_LITERAL,8,8,0xff);b.emit(GGV_U_TO_I,4,8);
    b.i(5,4);b.emit(GGV_I_EQ,2,4,5);auto notSpecial=b.branch(2);
    b.read(4,GGV_SPECIAL_LEGACY_VALUE);b.i(5,5);b.emit(GGV_I_EQ,2,4,5);auto notDetonate=b.branch(2);auto detonate=b.jump();b.end(notDetonate);
    b.read(4,GGV_SPECIAL_LEGACY_VALUE);b.i(5,6);b.emit(GGV_I_EQ,2,4,5);auto notBoost=b.branch(2);auto boost=b.jump();b.end(notBoost);
    b.read(4,GGV_SPECIAL_LEGACY_VALUE);b.i(5,7);b.emit(GGV_I_EQ,2,4,5);auto notRevert=b.branch(2);auto revert=b.jump();b.end(notRevert);b.ret(1);
    b.end(revert);b.end(boost);b.end(detonate);b.end(notSpecial);
    b.read(4,GGV_LEGACY_FUNCTION_TYPE);b.emit(GGV_I_TO_U,8,4);b.uop(GGV_U_AND_LITERAL,8,8,0xff);b.emit(GGV_U_TO_I,4,8);
    b.i(5,2);b.emit(GGV_I_EQ,2,4,5);auto notThrow=b.branch(2);
    b.read(4,GGV_LEGACY_AMMO_INDEX);b.i(5,-1);b.emit(GGV_I_LE,2,4,5);auto throwHasSlot=b.branch(2);b.ret(1);b.end(throwHasSlot);b.end(notThrow);
    b.read(4,GGV_LEGACY_AMMO_INDEX);b.i(5,0);b.emit(GGV_I_LT,2,4,5);b.emit(GGV_NOT,2,2);auto noAmmo=b.branch(2);
    b.read(10,GGV_AMMO_WEAPON_REF);b.emit(GGV_REF_NONNULL,2,10);auto noDefinition=b.branch(2);
    b.read(8,GGV_AMMO_DEFINITION_TYPE);b.emit(GGV_U_TO_I,4,8);b.call(GGV_AMMO_TOTAL_COUNT,4,4);b.i(5,0);b.emit(GGV_I_LE,2,4,5);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(noDefinition);b.end(noAmmo);b.ret(0);return b.s;
}
constexpr ggv_source unlimitedAmmoSource() {
    Builder b;b.read(8,GGV_AMMO_REQUEST_U32);b.emit(GGV_U_TO_I,4,8);b.i(5,12);b.emit(GGV_I_EQ,2,4,5);auto notRemote=b.branch(2);
    b.read(6,GGV_AMMO_STAGE);b.i(5,29);b.emit(GGV_I_EQ,2,6,5);auto otherRemoteStage=b.branch(2);b.ret(0);b.end(otherRemoteStage);b.ret(1);b.end(notRemote);
    b.i(5,14);b.emit(GGV_I_EQ,2,4,5);auto notTimed=b.branch(2);
    b.read(6,GGV_AMMO_STAGE);b.i(5,49);b.emit(GGV_I_EQ,2,6,5);auto otherTimedStage=b.branch(2);b.ret(0);b.end(otherTimedStage);b.ret(1);b.end(notTimed);
    for(auto forbidden : {22,23,24,25,26,27,28,29,30,31,32}) {
        b.i(5,forbidden);b.emit(GGV_I_EQ,2,4,5);auto permitted=b.branch(2);b.ret(0);b.end(permitted);
    }
    b.ret(1);return b.s;
}

/* Complete original five-way status; no graph override or new index guard. */
constexpr ggv_source ammoStatusSource() {
    Builder b;b.read(8,GGV_QUERY_FUNCTION_I32);b.call(GGV_QUERY_RESOLVE_FUNCTION,2,8);auto absent=b.branch(2);auto present=b.jump();b.end(absent);b.ret(-1);b.end(present);
    b.read(4,GGV_LEGACY_AMMO_INDEX);b.i(5,-1);b.emit(GGV_I_EQ,2,4,5);b.emit(GGV_NOT,2,2);auto noAmmo=b.branch(2);
    b.read(4,GGV_LEGACY_AMMO_INDEX);b.write(GGV_AMMO_INDEX,4);b.read(7,GGV_QUERY_EQUIPPED_TYPE);b.i(5,0);b.emit(GGV_I_LT,2,7,5);b.emit(GGV_NOT,2,2);auto noEquippedAmmo=b.branch(2);
    b.read(7,GGV_LOADED_AMMO);b.read(6,GGV_QUERY_CLIP_CAPACITY);b.emit(GGV_I_LT,2,7,6);auto full=b.branch(2);b.i(0,1);
    b.read(4,GGV_WEAPON_NUMBER);b.i(5,19);b.emit(GGV_I_EQ,2,4,5);auto notShotgun=b.branch(2);
    b.read(8,GGV_QUERY_FUNCTION_I32);b.i(5,1);b.emit(GGV_I_EQ,2,8,5);auto notShotgunSecondary=b.branch(2);b.i(0,2);b.end(notShotgunSecondary);b.end(notShotgun);
    b.read(4,GGV_WEAPON_NUMBER);b.i(5,28);b.emit(GGV_I_EQ,2,4,5);auto notTranquilizer=b.branch(2);
    b.read(8,GGV_QUERY_FUNCTION_I32);b.i(5,1);b.emit(GGV_I_EQ,2,8,5);auto notTranqSecondary=b.branch(2);
    b.i(4,28);b.i(8,1);b.call(GGV_MELEE_MIN_CLIP,0,4,8);b.end(notTranqSecondary);b.end(notTranquilizer);
    b.read(7,GGV_LOADED_AMMO);b.emit(GGV_I_LT,2,7,0);auto enoughClip=b.branch(2);
    b.read(3,GGV_QUERY_RESERVE);b.i(5,0);b.emit(GGV_I_EQ,2,3,5);auto reloadable=b.branch(2);b.ret(-1);b.end(reloadable);b.ret(0);
    b.end(enoughClip);b.read(3,GGV_QUERY_RESERVE);b.i(5,0);b.emit(GGV_I_EQ,2,3,5);auto hasReserve=b.branch(2);b.ret(2);b.end(hasReserve);b.ret(1);
    b.end(full);b.end(noEquippedAmmo);b.end(noAmmo);b.ret(3);return b.s;
}
constexpr ggv_source ammoCapacitySource(){Builder b;b.read(0,GGV_QUERY_CAPACITY);b.emit(GGV_RETURN_I32,0,0);return b.s;}
constexpr ggv_source ammoTypeQuerySource() {
    Builder b;b.read(8,GGV_QUERY_WEAPON_U32);b.emit(GGV_U_TO_I,4,8);b.call(GGV_QUERY_FIND_WEAPON,10,4);
    b.emit(GGV_REF_NONNULL,2,10);auto noWeapon=b.branch(2);b.read(10,GGV_QUERY_AMMO_REF_U32);b.emit(GGV_REF_NONNULL,2,10);auto noAmmo=b.branch(2);
    b.read(8,GGV_QUERY_AMMO_TYPE);b.emit(GGV_U_TO_I,4,8);b.emit(GGV_RETURN_I32,0,4);
    b.end(noAmmo);b.end(noWeapon);b.ret(0);return b.s;
}
constexpr ggv_source ammoQtyQuerySource() {
    Builder b;b.read(8,GGV_QUERY_WEAPON_U32);b.emit(GGV_U_TO_I,4,8);b.call(GGV_QUERY_FIND_WEAPON,10,4);
    b.emit(GGV_REF_NONNULL,2,10);auto noWeapon=b.branch(2);b.read(10,GGV_QUERY_AMMO_REF_U32);b.write(GGV_QUERY_CACHED_AMMO,10);
    b.emit(GGV_REF_NONNULL,2,10);auto noAmmo=b.branch(2);b.read(8,GGV_QUERY_CACHED_TYPE);b.emit(GGV_U_TO_I,4,8);
    b.call(GGV_QUERY_RESERVED_COUNT,4,4);b.emit(GGV_RETURN_I32,0,4);b.end(noAmmo);b.end(noWeapon);b.ret(0);return b.s;
}
constexpr ggv_source ammoWeaponCapacitySource() {
    Builder b;b.read(4,GGV_QUERY_WEAPON_I32);b.call(GGV_QUERY_FIND_WEAPON,10,4);
    // Preserve the native unchecked weapon dereference BEFORE ammo NULL test.
    b.read(10,GGV_QUERY_AMMO_REF_I32);b.write(GGV_QUERY_CACHED_AMMO,10);b.emit(GGV_REF_NONNULL,2,10);auto noAmmo=b.branch(2);
    b.read(4,GGV_QUERY_CACHED_CAPACITY);b.emit(GGV_RETURN_I32,0,4);b.end(noAmmo);b.ret(0);return b.s;
}

/* Full native reload policy; no LoopVM or implicit ammo/index guards. */
constexpr ggv_source unequippedReloadSource() {
    Builder b;b.read(0,GGV_WEAPON_NUMBER);b.i(1,27);b.emit(GGV_I_EQ,2,0,1);auto notCrossbow=b.branch(2);b.ret(0);b.end(notCrossbow);
    b.i(1,19);b.emit(GGV_I_EQ,2,0,1);auto notShotgun=b.branch(2);b.ret(1);b.end(notShotgun);
    b.i(1,8);b.emit(GGV_I_EQ,2,0,1);auto notMagnum=b.branch(2);b.ret(2);b.end(notMagnum);
    b.i(1,9);b.emit(GGV_I_EQ,2,0,1);auto notLX=b.branch(2);b.ret(3);b.end(notLX);b.ret(-1);return b.s;
}
constexpr ggv_source reloadTransferSource() {
    Builder b;b.read(8,GGV_RELOAD_FUNCTION);b.call(GGV_QUERY_RESOLVE_FUNCTION,2,8);auto noFunction=b.branch(2);
    b.read(4,GGV_LEGACY_AMMO_INDEX);b.i(5,-1);b.emit(GGV_I_EQ,2,4,5);b.emit(GGV_NOT,2,2);auto noAmmo=b.branch(2);
    b.read(4,GGV_LEGACY_AMMO_INDEX);b.write(GGV_AMMO_INDEX,4);b.read(7,GGV_QUERY_EQUIPPED_TYPE);b.i(5,0);b.emit(GGV_I_LT,2,7,5);b.emit(GGV_NOT,2,2);auto noEquipped=b.branch(2);
    b.read(6,GGV_QUERY_CLIP_CAPACITY);b.read(7,GGV_LOADED_AMMO);b.emit(GGV_I_SUB_WRAP,0,6,7);
    b.read(4,GGV_WEAPON_NUMBER);b.call(GGV_RELOAD_INDEX_FOR_WEAPON,3,4);b.read(2,GGV_RELOAD_VALID_WEAPON);auto notFiringRange=b.branch(2);b.i(3,-1);b.end(notFiringRange);
    b.read(2,GGV_RELOAD_CHECK);auto noUnequipped=b.branch(2);b.i(5,0);b.emit(GGV_I_LT,2,3,5);b.emit(GGV_NOT,2,2);auto noReloadIndex=b.branch(2);b.write(GGV_RELOAD_INDEX,3);
    b.read(2,GGV_RELOAD_PAL);auto earlyVersion=b.branch(2);
    b.read(9,GGV_RELOAD_ROUNDS);b.emit(GGV_U_TO_I,4,9);b.read(5,GGV_RELOAD_TICKS256);b.emit(GGV_I_DIV_TRUNC,4,4,5);b.emit(GGV_I_SUB_WRAP,0,0,4);auto afterVersion=b.jump();
    b.end(earlyVersion);b.read(9,GGV_RELOAD_ROUNDS);b.i(5,8);b.emit(GGV_I_TO_U,10,5);b.emit(GGV_U_SHR,9,9,10);b.emit(GGV_U_TO_I,4,9);b.emit(GGV_I_SUB_WRAP,0,0,4);b.end(afterVersion);b.end(noReloadIndex);b.end(noUnequipped);
    b.read(2,GGV_RELOAD_ONE);auto notOne=b.branch(2);b.i(0,1);b.end(notOne);
    b.read(6,GGV_RELOAD_RESERVE);b.emit(GGV_I_GT,2,0,6);auto noReserveClamp=b.branch(2);b.read(0,GGV_RELOAD_RESERVE);b.end(noReserveClamp);
    // JPN Final native teleport fix only: capacity difference is re-read.
    b.read(2,GGV_RELOAD_JPN);auto noJpn=b.branch(2);b.read(6,GGV_QUERY_CLIP_CAPACITY);b.read(7,GGV_LOADED_AMMO);b.emit(GGV_I_SUB_WRAP,4,6,7);b.emit(GGV_I_GT,2,0,4);auto noClipClamp=b.branch(2);
    b.read(6,GGV_QUERY_CLIP_CAPACITY);b.read(7,GGV_LOADED_AMMO);b.emit(GGV_I_SUB_WRAP,0,6,7);b.end(noClipClamp);b.end(noJpn);
    b.read(7,GGV_LOADED_AMMO);b.emit(GGV_I_ADD_WRAP,7,7,0);b.write(GGV_LOADED_AMMO,7);
    b.read(6,GGV_RELOAD_RESERVE);b.emit(GGV_I_SUB_WRAP,6,6,0);b.write(GGV_RELOAD_RESERVE,6);
    b.read(9,GGV_RELOAD_AMMO_FLAGS);b.uop(GGV_U_AND_LITERAL,9,9,1);b.emit(GGV_U_TO_I,4,9);b.i(5,0);b.emit(GGV_I_EQ,2,4,5);b.emit(GGV_NOT,2,2);auto reserveAllowed=b.branch(2);b.i(6,0);b.write(GGV_RELOAD_RESERVE_CLEAR,6);b.end(reserveAllowed);
    // Native `if (func);` is a vacuous final check, not a hidden effect.
    b.end(noEquipped);b.end(noAmmo);b.end(noFunction);b.ret(0);return b.s;
}
constexpr ggv_source reloadBothSource() {
    Builder b;b.i(0,0);b.call(GGV_QUERY_RESOLVE_FUNCTION,2,0);auto noPrimary=b.branch(2);b.call(GGV_SVC_RELOAD_TRANSFER,0,0);b.end(noPrimary);
    b.i(0,1);b.call(GGV_QUERY_RESOLVE_FUNCTION,2,0);auto noSecondary=b.branch(2);b.call(GGV_SVC_RELOAD_TRANSFER,0,0);b.end(noSecondary);b.ret(0);return b.s;
}

/* Fixed native2x4 timer sequence. Each READ/WRITE reobserves current player. */
constexpr ggv_source countdownSource() {
    Builder b;
    b.i(4,0);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,0);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero00=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store00=b.jump();
    b.end(zero00);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store00);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,0);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,1);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero01=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store01=b.jump();
    b.end(zero01);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store01);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,0);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,2);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero02=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store02=b.jump();
    b.end(zero02);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store02);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,0);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,3);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero03=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store03=b.jump();
    b.end(zero03);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store03);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,1);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,0);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero10=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store10=b.jump();
    b.end(zero10);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store10);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,1);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,1);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero11=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store11=b.jump();
    b.end(zero11);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store11);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,1);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,2);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero12=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store12=b.jump();
    b.end(zero12);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store12);b.write(GGV_COUNTDOWN_SPENT,0);
    b.i(4,1);b.write(GGV_COUNTDOWN_HAND_INDEX,4);b.i(5,3);b.write(GGV_COUNTDOWN_SLOT_INDEX,5);
    b.read(0,GGV_COUNTDOWN_SPENT);b.emit(GGV_U_TO_I,1,0);b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_GT,2,1,3);auto zero13=b.branch(2);
    b.read(3,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_SUB_WRAP,1,1,3);b.emit(GGV_I_TO_U,0,1);b.uop(GGV_U_AND_LITERAL,0,0,0xffff);auto store13=b.jump();
    b.end(zero13);b.i(7,0);b.emit(GGV_I_TO_U,0,7);b.end(store13);b.write(GGV_COUNTDOWN_SPENT,0);
    b.ret(0);return b.s;
}
constexpr ggv_source gunVisibilitySource() {
    Builder b;b.read(0,GGV_VIS_COMMAND_TYPE);b.i(1,4);b.emit(GGV_I_EQ,2,0,1);auto notUpgrade=b.branch(2);
    b.read(3,GGV_VIS_UPGRADES);b.read(4,GGV_VIS_PARAMETER);b.emit(GGV_U_SHR,3,3,4);b.uop(GGV_U_AND_LITERAL,3,3,1);b.emit(GGV_U_TO_I,0,3);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);b.emit(GGV_NOT,2,2);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(notUpgrade);b.i(1,5);b.emit(GGV_I_EQ,2,0,1);auto notLeft=b.branch(2);b.read(2,GGV_VIS_IS_LEFT_HAND);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(notLeft);b.i(1,6);b.emit(GGV_I_EQ,2,0,1);auto notRight=b.branch(2);b.read(2,GGV_VIS_IS_RIGHT_HAND);b.emit(GGV_RETURN_BOOL,0,2);b.end(notRight);b.ret(1);return b.s;
}

/* Complete getter, reused by both native entry points, no cached player. */
constexpr ggv_source currentWeaponSource() {
    Builder b;b.read(0,GGV_CURRENT_HAND_INUSE);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto inuse=b.branch(2);b.ret(0);b.end(inuse);b.read(0,GGV_CURRENT_WEAPON_NUMBER);b.emit(GGV_RETURN_I32,0,0);return b.s;
}
constexpr ggv_source setAmmoQuantitySource() {
    Builder b;b.call(GGV_AMMO_SELECT_PLAYER);b.i(0,0);b.call(GGV_QUANTITY_CURRENT_WEAPON,2,0);b.read(3,GGV_QUANTITY_AMMO_REQUEST);b.i(1,-1);b.emit(GGV_I_TO_U,8,2);
    b.i(4,0);b.emit(GGV_I_TO_U,9,4);b.call(GGV_QUANTITY_AMMO_TYPE,10,8,9);b.emit(GGV_U_TO_I,4,10);b.emit(GGV_I_EQ,6,4,3);auto noPrimary=b.branch(6);b.i(1,0);b.end(noPrimary);
    b.i(4,1);b.emit(GGV_I_TO_U,9,4);b.call(GGV_QUANTITY_AMMO_TYPE,10,8,9);b.emit(GGV_U_TO_I,4,10);b.emit(GGV_I_EQ,6,4,3);auto noSecondary=b.branch(6);b.i(1,1);b.end(noSecondary);b.write(GGV_QUANTITY_FUNCTION,1);
    b.i(4,-1);b.emit(GGV_I_EQ,6,1,4);b.emit(GGV_NOT,6,6);auto noSelectedFunction=b.branch(6);b.i(4,1);b.emit(GGV_I_TO_U,11,4);b.s.nodes[b.emit(GGV_CALL,6,2,1,11)].service=GGV_AMMO_HAS_FLAG;auto reservePermitted=b.branch(6);
    // For cloak and combat boost, ammo cannot be held outside of the weapon.
    // So just add it to the loaded clip, preserving the native upper clamp.
    b.read(0,GGV_QUANTITY_LOADED_RIGHT);b.read(4,GGV_QUANTITY_REQUEST);b.emit(GGV_I_ADD_WRAP,0,0,4);b.write(GGV_QUANTITY_LOADED_RIGHT,0);
    b.read(0,GGV_QUANTITY_LOADED_RIGHT);b.read(4,GGV_QUANTITY_CLIP_RIGHT);b.emit(GGV_I_GT,6,0,4);auto noClipClamp=b.branch(6);b.read(0,GGV_QUANTITY_CLIP_RIGHT);b.write(GGV_QUANTITY_LOADED_RIGHT,0);b.end(noClipClamp);
    b.i(0,0);b.write(GGV_QUANTITY_RESERVE,0);b.ret(0);b.end(reservePermitted);b.end(noSelectedFunction);
    b.i(0,0);b.i(4,-1);b.emit(GGV_I_EQ,6,1,4);b.emit(GGV_NOT,6,6);auto noReserveFunction=b.branch(6);b.i(4,2);b.emit(GGV_I_TO_U,11,4);b.s.nodes[b.emit(GGV_CALL,6,2,1,11)].service=GGV_AMMO_HAS_FLAG;auto notEquippedReserve=b.branch(6);
    // For throwable items, the capacity applies to reserve + loaded.
    b.read(0,GGV_QUANTITY_LOADED_RIGHT);b.read(4,GGV_QUANTITY_LOADED_LEFT);b.emit(GGV_I_ADD_WRAP,0,0,4);b.end(notEquippedReserve);b.end(noReserveFunction);
    b.read(5,GGV_QUANTITY_REQUEST);b.read(4,GGV_QUANTITY_CAPACITY);b.emit(GGV_I_SUB_WRAP,4,4,0);b.emit(GGV_I_GT,6,5,4);auto requestFits=b.branch(6);
    b.read(4,GGV_QUANTITY_CAPACITY);b.emit(GGV_I_SUB_WRAP,4,4,0);b.write(GGV_QUANTITY_RESERVE,4);auto done=b.jump();b.end(requestFits);b.write(GGV_QUANTITY_RESERVE,5);b.end(done);b.ret(0);return b.s;
}
constexpr ggv_source setWeaponAmmoQuantitySource() {
    Builder b;b.read(8,GGV_QUERY_WEAPON_U32);b.emit(GGV_U_TO_I,0,8);b.call(GGV_QUERY_FIND_WEAPON,10,0);b.emit(GGV_REF_NONNULL,2,10);auto noWeapon=b.branch(2);
    b.read(10,GGV_QUERY_AMMO_REF_U32);b.write(GGV_QUERY_CACHED_AMMO,10);b.emit(GGV_REF_NONNULL,2,10);auto noAmmo=b.branch(2);
    b.read(8,GGV_QUERY_CACHED_TYPE);b.emit(GGV_U_TO_I,0,8);b.read(9,GGV_QUANTITY_REQUEST_U32);b.emit(GGV_U_TO_I,1,9);b.call(GGV_QUANTITY_APPLY,0,0,1);b.end(noAmmo);b.end(noWeapon);b.ret(0);return b.s;
}

constexpr auto control = controlSource();
constexpr auto credit = creditSource();
constexpr auto fullControl = fullControlSource();
constexpr auto fullCommit = fullCommitSource();
constexpr auto handRumble = handRumbleSource();
constexpr auto handSlide = handSlideSource();
static_assert(valid(control), "Stock shooting control program must be total and typed");
static_assert(valid(credit), "Stock shot credit program must be total and typed");
static_assert(valid(fullControl), "Full shooting control must be total and typed");
static_assert(valid(fullCommit), "Full shooting commit must be total and typed");
static_assert(valid(handRumble), "Hand rumble must be total and typed");
static_assert(valid(handSlide), "Hand slide must be total and typed");
constexpr auto recoilRecovery=recoilRecoverySource();
constexpr auto attackShoot=attackShootSource();
static_assert(valid(recoilRecovery),"Recoil recovery must be total and typed");
static_assert(valid(attackShoot),"Attack driver must be total and typed");
constexpr auto throwControl=throwSource();
static_assert(valid(throwControl),"Complete throw control must be total and typed");
constexpr auto minClipQty=minClipSource();
constexpr auto meleeControl=meleeSource();
static_assert(valid(minClipQty),"Pure min-clip utility must be total and typed");
static_assert(valid(meleeControl),"Complete melee control must be total and typed");
constexpr auto specialControl=specialSource();
constexpr auto emptyAttack=emptyAttackSource();
static_assert(valid(specialControl),"Complete special driver must be total and typed");
static_assert(valid(emptyAttack),"Complete empty driver must be total and typed");
constexpr auto setState=setStateSource();
constexpr auto attackDispatch=attackDispatchSource();
static_assert(valid(setState),"Complete state utility must be total and typed");
static_assert(valid(attackDispatch),"Complete attack dispatcher must be total and typed");
constexpr auto animationReset=animationResetSource();
constexpr auto animationBusy=animationBusySource();
constexpr auto triggerInput=triggerInputSource();
static_assert(valid(animationReset),"Complete animation reset must be total and typed");
static_assert(valid(animationBusy),"Complete animation busy predicate must be total and typed");
static_assert(valid(triggerInput),"Complete trigger input must be total and typed");
constexpr auto ammoCount=ammoCountSource();
constexpr auto reservedAmmo=reservedAmmoSource();
constexpr auto ammoEligibility=ammoEligibilitySource();
constexpr auto unlimitedAmmo=unlimitedAmmoSource();
static_assert(valid(ammoCount),"Complete ammo count must be total and typed");
static_assert(valid(reservedAmmo),"Complete reserved count must be total and typed");
static_assert(valid(ammoEligibility),"Complete ammo exclusion predicate must be total and typed");
static_assert(valid(unlimitedAmmo),"Complete unlimited policy must be total and typed");
constexpr auto ammoStatus=ammoStatusSource();
constexpr auto ammoCapacity=ammoCapacitySource();
constexpr auto ammoTypeQuery=ammoTypeQuerySource();
constexpr auto ammoQtyQuery=ammoQtyQuerySource();
constexpr auto ammoWeaponCapacity=ammoWeaponCapacitySource();
static_assert(valid(ammoStatus),"Complete ammo status must be total and typed");
static_assert(valid(ammoCapacity),"Complete capacity must be total and typed");
static_assert(valid(ammoTypeQuery),"Complete ammo type query must be total and typed");
static_assert(valid(ammoQtyQuery),"Complete ammo qty query must be total and typed");
static_assert(valid(ammoWeaponCapacity),"Complete weapon capacity must be total and typed");
constexpr auto unequippedReload=unequippedReloadSource();
constexpr auto reloadTransfer=reloadTransferSource();
constexpr auto reloadBoth=reloadBothSource();
static_assert(valid(unequippedReload),"Reload index must be total and typed");
static_assert(valid(reloadTransfer),"Complete reload transfer must be total and typed");
static_assert(valid(reloadBoth),"Both reload slots must be total and typed");
constexpr auto countdown=countdownSource();
constexpr auto gunVisibility=gunVisibilitySource();
static_assert(valid(countdown),"Full fixed timer sequence must be total and typed");
static_assert(valid(gunVisibility),"Complete visibility predicate must be total and typed");
constexpr auto currentWeapon=currentWeaponSource();
constexpr auto setAmmoQuantity=setAmmoQuantitySource();
constexpr auto setWeaponAmmoQuantity=setWeaponAmmoQuantitySource();
static_assert(valid(currentWeapon),"Full current weapon getter must be total and typed");
static_assert(valid(setAmmoQuantity),"Complete ammo quantity setter must be total and typed");
static_assert(valid(setWeaponAmmoQuantity),"Full weapon quantity wrapper must be total and typed");
/* Hand lifecycle drivers. Captured func/info/passed hand stay host fields;
 * player globals are fresh reads at each node, including after child services.
 * StartAnimation, a44, child setState/transfer/status, sound, catalog and
 * cheat-aware flag queries remain individual typed native services. */
constexpr float maxPitch=0.87252569198608f;
constexpr void idleStateReturn(Builder &b) {
    b.read(3,GGV_HAND_NUMBER);b.i(1,0);b.call(GGV_EMPTY_SET_STATE,2,3,1);auto refused=b.branch(2);
    b.read(0,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,0);b.end(refused);
}
constexpr void reloadSelectOtherHand(Builder &b) {
    // Native otherhand address is taken once from the fresh current player.
    b.i(1,1);b.read(3,GGV_HAND_NUMBER);b.emit(GGV_I_SUB_WRAP,0,1,3);b.call(GGV_RELOAD_SELECT_OTHER_HAND,0,0);
}
constexpr std::pair<unsigned,unsigned> otherHandReloading(Builder &b) {
    b.read(0,GGV_RELOAD_OTHER_STATE);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);auto notReloading=b.branch(2);
    b.read(0,GGV_RELOAD_OTHER_STATEFRAMES);b.i(1,20);b.call(GGV_RECOIL_TICKS,4,1);b.emit(GGV_I_LT,2,0,4);
    return {notReloading,b.branch(2)};
}
constexpr void reloadStartAnimation(Builder &b) {
    b.read(11,GGV_RELOAD_AMMO_ANIMATION);b.read(3,GGV_HAND_NUMBER);b.read(12,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,11,3,12)].service=GGV_HAND_START_ANIMATION;
}
constexpr void reloadChildTransfer(Builder &b,int onebullet) {
    b.read(0,GGV_WEAPON_FUNCTION);b.i(1,onebullet);b.i(4,0);
    b.s.nodes[b.emit(GGV_CALL,0,0,1,4)].service=GGV_RELOAD_CHILD_TRANSFER;
}
// Native f32 (numerator * MAX_PITCH) / TICKS(n), left to right.
constexpr void reloadPitchOver(Builder &b,int ticks) {
    b.f(6,maxPitch);b.emit(GGV_F_MUL,5,5,6);b.i(1,ticks);b.call(GGV_RECOIL_TICKS,4,1);
    b.emit(GGV_I_TO_F,6,4);b.emit(GGV_F_DIV,5,5,6);b.call(GGV_RELOAD_SET_ARM_PITCH,0,5);
}
constexpr ggv_source reloadSource() {
    Builder b;b.call(GGV_RELOAD_CAPTURE_FUNCTION,10);
    // Death: two individual stores, not animationReset; refusal falls through.
    b.read(2,GGV_RELOAD_PLAYER_DEAD);auto alive=b.branch(2);
    b.i(0,0);b.write(GGV_EMPTY_ANIM_MODE,0);b.i(0,-1);b.write(GGV_ANIM_LOAD,0);idleStateReturn(b);b.end(alive);
    b.read(0,GGV_STATE_CYCLES);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto notFirst=b.branch(2);
    reloadSelectOtherHand(b);b.i(0,-1);b.write(GGV_RELOAD_GS_INT1,0);b.i(0,0);b.write(GGV_RELOAD_GS_INT2,0);
    auto first=otherHandReloading(b);b.i(0,9);b.write(GGV_STATE_MINOR,0);b.end(first.first);b.end(first.second);b.end(notFirst);
    // Minor states are independent ifs: WAIT may progress to MAIN this call.
    b.read(0,GGV_STATE_MINOR);b.i(1,9);b.emit(GGV_I_EQ,2,0,1);auto notWait=b.branch(2);
    reloadSelectOtherHand(b);auto waiting=otherHandReloading(b);b.ret(0);b.end(waiting.first);b.end(waiting.second);
    b.i(0,0);b.write(GGV_STATE_FRAMES,0);b.write(GGV_STATE_CYCLES,0);b.write(GGV_STATE_MINOR,0);b.write(GGV_STATE_LAST_FRAME,0);b.end(notWait);
    b.read(0,GGV_STATE_MINOR);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto notMain=b.branch(2);
    b.read(0,GGV_STATE_CYCLES);b.emit(GGV_I_EQ,2,0,1);auto mainLater=b.branch(2);
    b.emit(GGV_REF_NONNULL,2,10);auto noFunc=b.branch(2);
    b.read(0,GGV_RELOAD_FUNC_AMMO_INDEX);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto notSlot0=b.branch(2);auto slotReady=b.jump();b.end(notSlot0);
    b.read(0,GGV_RELOAD_FUNC_AMMO_INDEX);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);auto noSlot=b.branch(2);b.end(slotReady);
    b.read(11,GGV_RELOAD_AMMO_ANIMATION);b.emit(GGV_REF_NONNULL,2,11);auto noAnimation=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,26);b.emit(GGV_I_EQ,2,7,1);b.emit(GGV_NOT,2,2);auto knife=b.branch(2);
    reloadStartAnimation(b);b.boolean(2,true);b.write(GGV_RELOAD_UNK0D0E_07,2);
    b.read(13,GGV_RELOAD_FUNC_AMMO_FLAGS);b.uop(GGV_MASK,2,13,4);auto notIncremental=b.branch(2);
    b.boolean(2,true);b.write(GGV_RELOAD_INCREMENTAL,2);b.end(notIncremental);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,30);b.emit(GGV_I_EQ,2,7,1);auto notGrenade=b.branch(2);auto eject=b.jump();b.end(notGrenade);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,31);b.emit(GGV_I_EQ,2,7,1);auto notNbomb=b.branch(2);b.end(eject);
    b.i(0,0);b.write(GGV_RELOAD_EJECT_STATE,0);b.end(notNbomb);auto started=b.jump();
    b.end(noAnimation);b.end(knife);b.read(0,GGV_STATE_MINOR);b.emit(GGV_I_INCREMENT,0,0);b.write(GGV_STATE_MINOR,0);auto lowered=b.jump();
    b.end(noFunc);b.end(noSlot);idleStateReturn(b);b.end(started);b.end(lowered);auto firstCycleDone=b.jump();
    // Later cycles dereference the captured func unchecked, as native does.
    b.end(mainLater);
    b.read(13,GGV_RELOAD_FUNC_AMMO_FLAGS);b.uop(GGV_MASK,2,13,4);auto notIncrementalLater=b.branch(2);
    b.read(12,GGV_PASSED_HAND);b.i(1,1);b.call(GGV_HAND_ANIM_A44,2,12,1);auto a44Pending=b.branch(2);
    b.read(13,GGV_STATE_FLAGS);b.uop(GGV_MASK,2,13,0x10);b.emit(GGV_NOT,2,2);auto incrementalFlagged=b.branch(2);
    reloadChildTransfer(b,1);b.read(13,GGV_STATE_FLAGS);b.uop(GGV_U_OR_LITERAL,13,13,0x10);b.write(GGV_STATE_FLAGS,13);
    b.read(0,GGV_WEAPON_FUNCTION);b.call(GGV_RELOAD_AMMO_STATUS,4,0);
    b.i(1,2);b.emit(GGV_I_LT,2,4,1);b.emit(GGV_NOT,2,2);auto belowTwo=b.branch(2);b.boolean(14,false);b.write(GGV_RELOAD_INCREMENTAL,14);b.end(belowTwo);
    b.i(1,-1);b.emit(GGV_I_EQ,2,4,1);auto notExhausted=b.branch(2);b.boolean(14,false);b.write(GGV_RELOAD_INCREMENTAL,14);b.end(notExhausted);
    b.end(incrementalFlagged);auto a44Done=b.jump();
    b.end(a44Pending);b.i(0,0);b.emit(GGV_I_TO_U,13,0);b.write(GGV_STATE_FLAGS,13);b.end(a44Done);
    b.read(2,GGV_TRIGGER);auto noTrigger=b.branch(2);b.boolean(14,false);b.write(GGV_RELOAD_INCREMENTAL,14);b.end(noTrigger);
    // JPN Final only: active, uninhibited Eyespy stops incremental reloading.
    b.read(2,GGV_RELOAD_JPN);auto noJpn=b.branch(2);
    b.read(13,GGV_RELOAD_DEVICES_ACTIVE);b.read(16,GGV_RELOAD_DEVICES_INHIBIT);
    b.uop(GGV_MASK,2,13,4);b.uop(GGV_MASK,14,16,4);b.emit(GGV_NOT,14,14);auto noEyespy=b.branch(2);auto inhibited=b.branch(14);
    b.boolean(14,false);b.write(GGV_RELOAD_INCREMENTAL,14);b.end(inhibited);b.end(noEyespy);b.end(noJpn);auto incrementalDone=b.jump();
    b.end(notIncrementalLater);
    b.read(13,GGV_STATE_FLAGS);b.uop(GGV_MASK,2,13,0x10);b.emit(GGV_NOT,2,2);auto plainFlagged=b.branch(2);
    b.read(12,GGV_PASSED_HAND);b.i(1,1);b.call(GGV_HAND_ANIM_A44,2,12,1);auto plainPending=b.branch(2);
    reloadChildTransfer(b,0);b.read(13,GGV_STATE_FLAGS);b.uop(GGV_U_OR_LITERAL,13,13,0x10);b.write(GGV_STATE_FLAGS,13);
    b.end(plainPending);b.end(plainFlagged);b.end(incrementalDone);
    b.read(0,GGV_EMPTY_ANIM_MODE);b.i(1,2);b.emit(GGV_I_EQ,2,0,1);b.emit(GGV_NOT,2,2);auto stillBusy=b.branch(2);
    idleStateReturn(b);b.end(stillBusy);
    // Native `if (1);` is vacuous.
    b.end(firstCycleDone);b.end(notMain);
    b.read(0,GGV_STATE_MINOR);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);auto notLower=b.branch(2);
    b.read(0,GGV_RELOAD_COUNT60);b.i(1,15);b.call(GGV_RECOIL_TICKS,4,1);b.emit(GGV_I_GT,2,0,4);auto notPast=b.branch(2);auto lowerDone=b.jump();b.end(notPast);
    b.read(2,GGV_RELOAD_VISIBLE);auto hidden=b.branch(2);
    b.read(0,GGV_RELOAD_COUNT60);b.emit(GGV_I_TO_F,5,0);reloadPitchOver(b,16);auto lowering=b.jump();
    b.end(lowerDone);b.end(hidden);
    b.i(0,11);b.emit(GGV_I_TO_U,13,0);b.write(GGV_RELOAD_MODE,13);
    b.read(0,GGV_STATE_MINOR);b.emit(GGV_I_INCREMENT,0,0);b.write(GGV_STATE_MINOR,0);
    b.i(1,17);b.call(GGV_RECOIL_TICKS,4,1);b.write(GGV_RELOAD_PAUSETIME60,4);
    b.i(0,0);b.write(GGV_RELOAD_COUNT60,0);b.write(GGV_RELOAD_COUNT,0);b.end(lowering);b.end(notLower);
    b.read(0,GGV_STATE_MINOR);b.i(1,2);b.emit(GGV_I_EQ,2,0,1);auto notSound=b.branch(2);
    b.read(0,GGV_RELOAD_COUNT);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto soundIssued=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,26);b.emit(GGV_I_EQ,2,7,1);auto notKnife=b.branch(2);
    b.read(0,GGV_RELOAD_FUNC_AMMO_INDEX);b.i(1,0);b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto noKnifeAmmo=b.branch(2);
    b.read(11,GGV_RELOAD_AMMO_ANIMATION);b.emit(GGV_REF_NONNULL,2,11);auto noKnifeAnimation=b.branch(2);
    reloadStartAnimation(b);b.boolean(2,true);b.write(GGV_RELOAD_UNK0CC8_02,2);
    b.end(noKnifeAnimation);b.end(noKnifeAmmo);b.end(notKnife);
    b.read(13,GGV_STATE_FLAGS);b.uop(GGV_MASK,2,13,0x10);b.emit(GGV_NOT,2,2);auto soundFlagged=b.branch(2);
    reloadChildTransfer(b,0);b.end(soundFlagged);
    // Lazy && chain, then one switch read of info weapon.
    b.read(0,GGV_RELOAD_LVUPDATE240);b.i(1,0);b.emit(GGV_I_GT,2,0,1);auto paused=b.branch(2);
    b.read(0,GGV_RELOAD_CAMERA_MODE);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);b.emit(GGV_NOT,2,2);auto thirdPerson=b.branch(2);
    b.call(GGV_RELOAD_IS_LOADED,2);auto unloaded=b.branch(2);
    b.read(2,GGV_RELOAD_PLAYER_INVINCIBLE);b.emit(GGV_NOT,2,2);auto invincible=b.branch(2);
    b.read(2,GGV_RELOAD_PLAYER_DEAD);b.emit(GGV_NOT,2,2);auto dead=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);unsigned silent[13]{};unsigned index=0;
    for(auto weapon : {0,1,26,29,30,32,33,34,45,53,54,55}) {
        b.i(1,weapon);b.emit(GGV_I_EQ,2,7,1);auto next=b.branch(2);silent[index++]=b.jump();b.end(next);
    }
    // WEAPON_BRIEFCASE2 follows the version-conditional enum entry.
    b.read(1,GGV_RELOAD_WEAPON_BRIEFCASE2);b.emit(GGV_I_EQ,2,7,1);auto audible=b.branch(2);silent[index++]=b.jump();b.end(audible);
    b.call(GGV_RELOAD_SOUND_START);for(auto edge : silent)b.end(edge);
    b.end(dead);b.end(invincible);b.end(unloaded);b.end(thirdPerson);b.end(paused);b.end(soundIssued);
    b.read(0,GGV_RELOAD_COUNT60);b.read(4,GGV_RELOAD_PAUSETIME60);b.emit(GGV_I_LT,2,0,4);b.emit(GGV_NOT,2,2);auto pausing=b.branch(2);
    b.read(0,GGV_RELOAD_COUNT);b.i(1,2);b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto counting=b.branch(2);
    b.i(0,12);b.emit(GGV_I_TO_U,13,0);b.write(GGV_RELOAD_MODE,13);
    b.read(0,GGV_STATE_MINOR);b.emit(GGV_I_INCREMENT,0,0);b.write(GGV_STATE_MINOR,0);
    b.i(0,0);b.write(GGV_RELOAD_COUNT60,0);b.write(GGV_RELOAD_COUNT,0);auto raised=b.jump();
    b.end(pausing);b.end(counting);b.f(5,maxPitch);b.call(GGV_RELOAD_SET_ARM_PITCH,0,5);b.end(raised);b.end(notSound);
    b.read(0,GGV_STATE_MINOR);b.i(1,3);b.emit(GGV_I_EQ,2,0,1);auto notRaise=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,26);b.emit(GGV_I_EQ,2,7,1);auto raiseNotKnife=b.branch(2);b.i(0,0);b.write(GGV_EMPTY_ANIM_MODE,0);b.end(raiseNotKnife);
    b.read(0,GGV_RELOAD_COUNT);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto raiseCounting=b.branch(2);b.i(0,0);b.write(GGV_RELOAD_DOAUTOSELECT,0);b.end(raiseCounting);
    // Lazy || order: timer, file lookup, then the two cheat-aware flag queries.
    b.read(0,GGV_RELOAD_COUNT60);b.i(1,23);b.call(GGV_RECOIL_TICKS,4,1);b.emit(GGV_I_LT,2,0,4);auto raiseExpired=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.call(GGV_RELOAD_FILENUM2,0,7);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);b.emit(GGV_NOT,2,2);auto noFile=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,0x40);b.emit(GGV_I_TO_U,13,1);b.call(GGV_RELOAD_WEAPON_HAS_FLAG,2,7,13);auto unflagged=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.i(1,0x80);b.emit(GGV_I_TO_U,13,1);b.call(GGV_RELOAD_WEAPON_HAS_FLAG,2,7,13);b.emit(GGV_NOT,2,2);auto nightVision=b.branch(2);
    b.i(1,23);b.call(GGV_RECOIL_TICKS,4,1);b.read(0,GGV_RELOAD_COUNT60);b.emit(GGV_I_SUB_WRAP,0,4,0);b.emit(GGV_I_TO_F,5,0);reloadPitchOver(b,23);auto raising=b.jump();
    b.end(raiseExpired);b.end(noFile);b.end(unflagged);b.end(nightVision);
    b.i(0,0);b.emit(GGV_I_TO_U,13,0);b.write(GGV_RELOAD_MODE,13);b.write(GGV_RELOAD_COUNT60,0);b.write(GGV_RELOAD_COUNT,0);
    idleStateReturn(b);b.end(raising);b.end(notRaise);b.ret(0);return b.s;
}
constexpr ggv_source changeFuncSource() {
    Builder b;b.boolean(4,false);
    b.read(0,GGV_STATE_CYCLES);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto laterCycle=b.branch(2);
    b.read(0,GGV_WEAPON_FUNCTION);b.emit(GGV_I_EQ,2,0,1);auto notPrimary=b.branch(2);
    // The animation lookup precedes the u8 function store in both arms.
    b.call(GGV_CHANGEFUNC_PRI_TO_SEC_ANIM,10);b.i(0,1);b.write(GGV_WEAPON_FUNCTION,0);auto selected=b.jump();
    b.end(notPrimary);b.call(GGV_CHANGEFUNC_SEC_TO_PRI_ANIM,10);b.i(0,0);b.write(GGV_WEAPON_FUNCTION,0);b.end(selected);
    b.boolean(4,false);b.emit(GGV_REF_NONNULL,2,10);auto noCommand=b.branch(2);
    b.read(3,GGV_HAND_NUMBER);b.read(12,GGV_PASSED_HAND);b.s.nodes[b.emit(GGV_CALL,0,10,3,12)].service=GGV_HAND_START_ANIMATION;
    b.boolean(4,true);b.i(0,-1);b.write(GGV_RIGHT_HAND_UNK0DD4,0);b.end(noCommand);auto cycleDone=b.jump();
    // Exact BUSY equality, not the nonidle animation predicate.
    b.end(laterCycle);b.read(0,GGV_EMPTY_ANIM_MODE);b.i(1,2);b.emit(GGV_I_EQ,2,0,1);auto notBusy=b.branch(2);b.boolean(4,true);b.end(notBusy);
    b.end(cycleDone);b.emit(GGV_NOT,2,4);auto more=b.branch(2);
    idleStateReturn(b);b.end(more);b.ret(0);return b.s;
}
constexpr ggv_source armPitchSource() {
    Builder b;b.boolean(0,true);b.write(GGV_USE_POSROT,0);
    b.read(1,GGV_ARM_PITCH_ANGLE);b.read(10,GGV_ARM_PITCH_MATRIX);b.call(GGV_ARM_PITCH_X_ROTATION,0,1,10);
    b.f(2,0.0f);b.write(GGV_ARM_PITCH_M30,2);
    b.call(GGV_ARM_PITCH_COSF,3,1);b.f(2,1.0f);b.emit(GGV_F_SUB,3,2,3);b.f(2,-80.0f);b.emit(GGV_F_MUL,3,3,2);b.write(GGV_ARM_PITCH_M31,3);
    b.call(GGV_ARM_PITCH_SINF,3,1);b.f(2,15.0f);b.emit(GGV_F_MUL,3,3,2);b.write(GGV_ARM_PITCH_M32,3);b.ret(0);return b.s;
}
/* Each gunctrl field is a fresh current-player read, in native order. */
constexpr ggv_source isLoadedSource() {
    Builder b;b.read(0,GGV_LOADED_GUNMEMOWNER);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);auto notOwner=b.branch(2);
    b.read(0,GGV_LOADED_GUNMEMTYPE);b.emit(GGV_I_EQ,2,0,1);auto notNone=b.branch(2);b.ret(1);b.end(notNone);
    b.read(0,GGV_LOADED_GUNMEMNEW);b.emit(GGV_I_LT,2,0,1);auto notPending=b.branch(2);
    b.read(0,GGV_LOADED_MASTERLOADSTATE);b.i(3,4);b.emit(GGV_I_EQ,2,0,3);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(notPending);b.end(notOwner);b.ret(0);return b.s;
}
constexpr ggv_source isReloadingSource() {
    Builder b;b.read(0,GGV_STATE_VALUE);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);auto notReloading=b.branch(2);
    b.ret(1);b.end(notReloading);b.ret(0);return b.s;
}
constexpr auto reloadTick=reloadSource();
constexpr auto changeFuncTick=changeFuncSource();
constexpr auto armPitch=armPitchSource();
constexpr auto isLoaded=isLoadedSource();
constexpr auto isReloading=isReloadingSource();
// Animation command policy is graph control flow, not a hidden host predicate.
// Keep both native if statements and their fresh field reads. Left inuse
// compares to exactly one; signed nonzero values must not become true.
constexpr ggv_source animationEligibilitySource() {
    Builder b;
    b.boolean(3,false);
    b.read(0,GGV_ANIM_ELIGIBILITY_KIND);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);
    auto conditional=b.branch(2);b.ret(1);b.end(conditional);
    b.read(0,GGV_ANIM_ELIGIBILITY_KIND);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);
    auto notDual=b.branch(2);
    b.read(4,GGV_ANIM_ELIGIBILITY_LEFT_INUSE);b.emit(GGV_I_EQ,2,4,1);
    auto leftNotTrue=b.branch(2);b.boolean(3,true);
    b.end(leftNotTrue);b.end(notDual);
    b.read(0,GGV_ANIM_ELIGIBILITY_KIND);b.i(1,2);b.emit(GGV_I_EQ,2,0,1);
    auto notSecondary=b.branch(2);
    b.read(5,GGV_WEAPON_FUNCTION);b.i(1,1);b.emit(GGV_I_EQ,2,5,1);
    auto primary=b.branch(2);b.boolean(3,true);
    b.end(primary);b.end(notSecondary);
    b.emit(GGV_RETURN_BOOL,0,3);return b.s;
}
constexpr auto animationEligibility=animationEligibilitySource();
static_assert(valid(animationEligibility),"Animation eligibility must be total and typed");
static_assert(!needsServices(animationEligibility),"Animation eligibility needs only field reads");
constexpr ggv_source animationFrameSource() {
    Builder b;
    b.read(0,GGV_EMPTY_ANIM_MODE);b.i(1,2);b.emit(GGV_I_EQ,2,0,1);
    auto notBusy=b.branch(2);
    b.read(3,GGV_ANIM_FRAME_COMMAND);b.emit(GGV_REF_NONNULL,2,3);
    auto noCommand=b.branch(2);
    b.read(4,GGV_ANIM_FRAME_COMMAND_VALUE);
    auto zero=b.emit(GGV_CONST,5);b.s.nodes[zero].type=GGV_I64;b.s.nodes[zero].literal.i64=0;
    b.emit(GGV_I64_LT,2,4,5);auto forward=b.branch(2);
    b.call(GGV_MODEL_ANIM_FRAME_COUNT,6);b.emit(GGV_I_TO_F,7,6);
    b.call(GGV_MODEL_ANIM_CURRENT_FRAME,8);b.emit(GGV_F_SUB,9,7,8);
    b.write(GGV_ANIM_FRAME_RESULT,9);b.ret(0);
    b.end(forward);b.call(GGV_MODEL_ANIM_CURRENT_FRAME,8);
    b.write(GGV_ANIM_FRAME_RESULT,8);b.ret(0);
    b.end(notBusy);b.end(noCommand);b.f(9,0.0f);
    b.write(GGV_ANIM_FRAME_RESULT,9);b.ret(0);return b.s;
}
constexpr auto animationFrame=animationFrameSource();
// Native command-list traversal: type is read before the key sentinel test,
// and the cursor advances even on the iteration that finds a matching key.
constexpr ggv_source animationWaitSource(bool readiness) {
    Builder b;
    b.i(0,-1);b.i(1,-1);b.boolean(12,true);
    b.read(2,GGV_EMPTY_ANIM_MODE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);
    auto active=b.branch(4);
    if(readiness) {
        b.read(2,GGV_ANIM_LOAD);b.i(3,-1);b.emit(GGV_I_EQ,4,2,3);
        b.emit(GGV_RETURN_BOOL,0,4);
    } else b.ret(0);
    b.end(active);
    const unsigned loop=b.s.count;
    b.read(2,GGV_ANIM_WAIT_TYPE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);
    b.emit(GGV_NOT,4,4);auto ended=b.branch(4);
    b.i(3,-1);b.emit(GGV_I_EQ,4,0,3);auto found=b.branch(4);
    if(readiness) {
        b.read(2,GGV_ANIM_WAIT_TYPE);b.i(3,3);b.emit(GGV_I_EQ,4,2,3);
        auto noRelease=b.branch(4);b.read(1,GGV_ANIM_WAIT_KEY);b.end(noRelease);
    }
    b.read(2,GGV_ANIM_WAIT_TYPE);b.i(3,4);b.emit(GGV_I_EQ,4,2,3);
    auto noWait=b.branch(4);
    b.read(6,GGV_ANIM_WAIT_TIME);b.emit(GGV_I_TO_I64,7,6);
    b.read(8,GGV_ANIM_WAIT_VALUE);b.emit(GGV_I64_EQ,4,7,8);
    auto noMatch=b.branch(4);b.read(0,GGV_ANIM_WAIT_KEY);
    b.end(noWait);b.end(noMatch);
    b.read(5,GGV_ANIM_WAIT_CURSOR);b.call(GGV_ANIM_CURSOR_NEXT,5,5);
    b.write(GGV_ANIM_WAIT_CURSOR,5);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,12)].target=loop;
    b.end(ended);b.end(found);
    if(!readiness) {
        b.i(3,-1);b.emit(GGV_I_EQ,4,0,3);auto hasKey=b.branch(4);
        b.i(0,0);b.end(hasKey);b.emit(GGV_RETURN_I32,0,0);
        return b.s;
    }
    b.i(3,0);b.emit(GGV_I_LT,4,0,3);auto hasWait=b.branch(4);b.ret(1);
    b.end(hasWait);
    b.read(4,GGV_ANIMATION_LATCH);auto noLatch=b.branch(4);
    b.call(GGV_ANIM_WAIT_FRAME,9);b.read(13,GGV_RECOVERY_PAL);
    auto prePal=b.branch(13);
    b.emit(GGV_I_TO_F,10,1);b.emit(GGV_F_LE,4,9,10);auto releaseReady=b.jump();
    b.end(prePal);b.emit(GGV_TRUNCATE,2,9);b.emit(GGV_I_LE,4,2,1);
    b.end(releaseReady);auto released=b.branch(4);b.ret(0);
    b.end(noLatch);b.end(released);
    b.call(GGV_ANIM_WAIT_FRAME,9);b.read(13,GGV_RECOVERY_PAL);
    auto integerIncrement=b.branch(13);
    b.read(10,GGV_ANIM_WAIT_INCREMENT_REAL);auto incrementReady=b.jump();
    b.end(integerIncrement);b.read(2,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_TO_F,10,2);
    b.end(incrementReady);b.emit(GGV_F_ADD,9,9,10);b.emit(GGV_I_TO_F,11,0);
    b.emit(GGV_F_GE,4,9,11);b.emit(GGV_RETURN_BOOL,0,4);return b.s;
}
constexpr auto animationWait=animationWaitSource(true);
constexpr auto animationWaitKey=animationWaitSource(false);
static_assert(valid(animationWait)&&valid(animationWaitKey),"Animation wait programs must have safe typed control flow");
constexpr ggv_source animationStartSource() {
    Builder b;
    b.read(0,GGV_ANIM_START_COMMAND);b.emit(GGV_REF_NONNULL,1,0);auto nullCommand=b.branch(1);
    // Both catalog lookups happen before either result is checked.
    b.call(GGV_ANIM_START_NAME,2,0);b.call(GGV_ANIM_START_SOURCE,3,0);
    b.emit(GGV_REF_NONNULL,1,2);auto noName=b.branch(1);
    b.emit(GGV_REF_NONNULL,1,3);auto noSource=b.branch(1);
    b.call(GGV_ANIM_START_LOG_SOURCE,0,2,3);b.end(noName);b.end(noSource);
    b.read(4,GGV_ANIM_WAIT_TYPE);b.i(5,10);b.emit(GGV_I_EQ,1,4,5);
    auto list=b.branch(1);
    b.read(4,GGV_ANIM_WAIT_KEY);b.write(GGV_ANIM_LOAD,4);
    b.i(16,0);b.boolean(17,false);
    b.write(GGV_EMPTY_ANIM_MODE,16);b.write(GGV_ANIMATION_LATCH,17);
    b.write(GGV_ANIM_INCREMENTAL,17);b.write(GGV_ANIM_COMMAND,0);
    b.write(GGV_ANIM_LOOP_COUNT,16);b.write(GGV_ANIM_BIT02,17);
    b.write(GGV_ANIM_BIT07,17);b.write(GGV_ANIM_START_PREVIOUS,0);b.ret(0);
    b.end(list);b.boolean(6,false);b.boolean(15,true);
    b.call(GGV_RNG_U32,7);b.uop(GGV_U_MOD_LITERAL,7,7,100);
    const unsigned loop=b.s.count;
    b.read(4,GGV_ANIM_WAIT_TYPE);b.i(5,0);b.emit(GGV_I_EQ,1,4,5);
    b.emit(GGV_NOT,1,1);auto ended=b.branch(1);
    b.read(8,GGV_ANIM_WAIT_CURSOR);b.call(GGV_ANIM_START_ELIGIBILITY,1,8);
    auto ineligible=b.branch(1);b.emit(GGV_NOT,1,6);auto selected=b.branch(1);
    b.read(4,GGV_ANIM_WAIT_TYPE);b.i(5,6);b.emit(GGV_I_EQ,1,4,5);
    auto notInclude=b.branch(1);
    b.boolean(6,true);b.read(9,GGV_ANIM_START_CHILD);
    b.read(13,GGV_HAND_NUMBER);b.read(14,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,9,13,14)].service=GGV_HAND_START_ANIMATION;
    auto included=b.jump();b.end(notInclude);
    b.read(4,GGV_ANIM_WAIT_TYPE);b.i(5,7);b.emit(GGV_I_EQ,1,4,5);
    auto notRandom=b.branch(1);
    b.read(9,GGV_ANIM_START_CHILD);b.read(10,GGV_ANIM_START_PREVIOUS);
    b.emit(GGV_REF_EQ,1,9,10);b.emit(GGV_NOT,1,1);auto same=b.branch(1);
    b.read(11,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_TO_U,12,11);b.emit(GGV_U_GT,1,12,7);
    auto below=b.branch(1);
    b.boolean(6,true);b.read(9,GGV_ANIM_START_CHILD);
    b.read(13,GGV_HAND_NUMBER);b.read(14,GGV_PASSED_HAND);
    b.s.nodes[b.emit(GGV_CALL,0,9,13,14)].service=GGV_HAND_START_ANIMATION;
    b.end(ineligible);b.end(selected);b.end(included);b.end(notRandom);b.end(same);b.end(below);
    b.read(8,GGV_ANIM_WAIT_CURSOR);b.call(GGV_ANIM_CURSOR_NEXT,8,8);b.write(GGV_ANIM_WAIT_CURSOR,8);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,15)].target=loop;
    b.end(ended);b.ret(0);
    b.end(nullCommand);b.call(GGV_ANIM_START_LOG_NULL);b.ret(0);return b.s;
}
constexpr auto animationStart=animationStartSource();
static_assert(valid(animationStart),"Animation start must have safe typed control flow");
// Parent orchestration retains native child boundaries explicitly. Converting
// this dispatcher does not qualify the still-native idle/autoswitch children.
constexpr ggv_source handTickSource() {
    Builder b;b.i(0,0);b.read(1,GGV_STATE_VALUE);
    b.call(GGV_TICK_DELIVERY_PENDING,2);auto available=b.branch(2);b.ret(0);b.end(available);
    b.boolean(3,false);b.write(GGV_FIRING,3);b.write(GGV_FLASH,3);
    b.read(4,GGV_STATE_FRAMES);b.read(5,GGV_EMPTY_LVUPDATE);
    b.emit(GGV_I_ADD_WRAP,4,4,5);b.write(GGV_STATE_FRAMES,4);
    b.read(6,GGV_RELOAD_LVUPDATE240);b.i(7,0);b.emit(GGV_I_GT,2,6,7);auto noTime=b.branch(2);
    b.read(4,GGV_RELOAD_COUNT60);b.read(5,GGV_COUNTDOWN_UPDATE60);
    b.emit(GGV_I_ADD_WRAP,4,4,5);b.write(GGV_RELOAD_COUNT60,4);
    b.read(4,GGV_RELOAD_COUNT);b.emit(GGV_I_INCREMENT,4,4);b.write(GGV_RELOAD_COUNT,4);
    b.end(noTime);b.write(GGV_USE_POSROT,3);
    b.read(6,GGV_STATE_VALUE);
    unsigned dispatchEnds[8]{};unsigned count=0;
    for(const auto [state,service] : {
        std::pair{0,GGV_TICK_IDLE}, {1,GGV_TICK_RELOAD}, {4,GGV_TICK_ATTACK},
        {2,GGV_TICK_IDLE}, {5,GGV_TICK_CHANGE_GUN}, {3,GGV_TICK_EMPTY},
        {8,GGV_TICK_AUTO_SWITCH}, {7,GGV_TICK_CHANGE_FUNC}}) {
        b.i(7,state);b.emit(GGV_I_EQ,2,6,7);auto next=b.branch(2);
        if(state==4) {
            b.call(GGV_TICK_IS_V2,2);auto legacy=b.branch(2);
            b.call(GGV_TICK_ADVANCE_V2,0);auto ready=b.jump();
            b.end(legacy);b.call(GGV_TICK_ATTACK,0);b.end(ready);
        } else if(state!=2) b.call(service,0);
        // State2's donor is the constant-zero beta placeholder.
        dispatchEnds[count++]=b.jump();b.end(next);
    }
    for(unsigned i=0;i<count;++i)b.end(dispatchEnds[i]);
    b.read(4,GGV_STATE_FRAMES);b.write(GGV_STATE_LAST_FRAME,4);
    b.read(6,GGV_STATE_VALUE);b.emit(GGV_I_EQ,2,6,1);auto changed=b.branch(2);
    b.read(4,GGV_STATE_FRAMES);b.emit(GGV_I_SUB_WRAP,4,4,0);b.write(GGV_STATE_FRAMES,4);
    b.read(4,GGV_STATE_CYCLES);b.emit(GGV_I_INCREMENT,4,4);b.write(GGV_STATE_CYCLES,4);
    b.emit(GGV_RETURN_I32,0,0);
    b.end(changed);b.i(7,0);b.emit(GGV_I_SUB_WRAP,4,7,0);b.write(GGV_STATE_LAST_FRAME,4);
    b.emit(GGV_RETURN_I32,0,0);return b.s;
}
constexpr auto handTick=handTickSource();
static_assert(valid(handTick),"Hand tick parent must have safe typed control flow");
constexpr ggv_source tickHandSource() {
    Builder b;b.i(0,20);b.call(GGV_HAND_INFO_CAPTURE);
    b.read(1,GGV_COUNTDOWN_UPDATE60);b.read(2,GGV_COUNTDOWN_UPDATE60);b.write(GGV_ANIM_WAIT_INCREMENT,2);
    b.read(3,GGV_RECOVERY_PAL);auto legacy=b.branch(3);
    b.call(GGV_MODEL_ABS_ANIM_SPEED,4);b.read(2,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_TO_F,5,2);
    b.read(3,GGV_RELOAD_PAL);auto ntsc=b.branch(3);b.f(6,1.2f);b.emit(GGV_F_MUL,5,5,6);b.end(ntsc);
    b.emit(GGV_F_MUL,5,4,5);b.write(GGV_ANIM_WAIT_INCREMENT_REAL,5);auto prepared=b.jump();
    // Pre-PAL animframeincfreal is u32. PALUPF is the identity in these
    // NTSC versions: preserve unsigned wrapping, not float accumulation.
    b.end(legacy);b.read(7,GGV_ANIM_TICK_INCREMENT_LEGACY);b.emit(GGV_U_TO_I,2,7);
    b.read(8,GGV_COUNTDOWN_UPDATE60);b.emit(GGV_I_ADD_WRAP,2,2,8);b.emit(GGV_I_TO_U,7,2);
    b.write(GGV_ANIM_TICK_INCREMENT_LEGACY,7);b.end(prepared);
    const unsigned loop=b.s.count;
    b.i(8,0);b.emit(GGV_I_LT,3,0,8);b.emit(GGV_NOT,3,3);auto exhausted=b.branch(3);
    b.call(GGV_HAND_TICK_CHILD,1,1);b.i(8,1);b.emit(GGV_I_SUB_WRAP,0,0,8);
    b.i(8,0);b.emit(GGV_I_GT,3,1,8);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,3)].target=loop;
    b.end(exhausted);b.ret(0);return b.s;
}
constexpr auto tickHand=tickHandSource();
static_assert(valid(tickHand),"Hand frame parent must have safe typed control flow");
constexpr ggv_source autoSwitchSource() {
    Builder b;b.call(GGV_HAND_CURRENT_FUNCTION,0);
    b.read(2,GGV_STATE_INUSE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);auto inuse=b.branch(4);
    b.call(GGV_HAND_SET_STATE,4,3);auto idleRefused=b.branch(4);
    b.read(1,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,1);b.end(inuse);b.end(idleRefused);
    b.read(2,GGV_STATE_MINOR);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);auto notUnequip=b.branch(4);
    b.i(5,16);b.read(4,GGV_RELOAD_PAL);auto ntsc16=b.branch(4);b.i(5,13);b.end(ntsc16);
    b.read(4,GGV_NORMAL_MULTIPLAYER);auto single=b.branch(4);
    b.i(5,12);b.read(4,GGV_RELOAD_PAL);auto ntsc12=b.branch(4);b.i(5,10);b.end(ntsc12);b.end(single);
    b.read(2,GGV_STATE_FRAMES);b.emit(GGV_I_LT,4,2,5);auto delayDone=b.branch(4);
    // Native multiplication precedes division, and both use float.
    b.emit(GGV_I_TO_F,6,2);b.f(7,0.87252569198608f);b.emit(GGV_F_MUL,6,6,7);
    b.emit(GGV_I_TO_F,8,5);b.emit(GGV_F_DIV,6,6,8);b.call(GGV_RELOAD_SET_ARM_PITCH,0,6);
    auto unequipDone=b.jump();b.end(delayDone);
    b.read(2,GGV_STATE_MINOR);b.emit(GGV_I_INCREMENT,2,2);b.write(GGV_STATE_MINOR,2);
    b.end(unequipDone);b.end(notUnequip);
    b.read(2,GGV_STATE_MINOR);b.i(3,1);b.emit(GGV_I_EQ,4,2,3);auto notDelete=b.branch(4);
    b.boolean(4,false);b.write(GGV_HAND_LASTDIRVALID,4);
    b.read(4,GGV_RECOVERY_PAL);auto legacy=b.branch(4);auto frameReady=b.jump();b.end(legacy);
    b.read(2,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_TO_U,11,2);b.write(GGV_ANIM_TICK_INCREMENT_LEGACY,11);b.end(frameReady);
    b.f(6,0);b.write(GGV_REMAINDER,6);
    b.call(GGV_HAND_READY_SWITCH,4);auto notReady=b.branch(4);
    b.i(3,5);b.call(GGV_HAND_SET_STATE,4,3);auto switchRefused=b.branch(4);
    b.read(4,GGV_MULTIPLAYER);auto noDelete=b.branch(4);b.call(GGV_HAND_DELETE_WEAPON);b.end(noDelete);
    b.call(GGV_HAND_FREE_ROCKET);b.i(2,6);b.emit(GGV_I_TO_U,11,2);b.write(GGV_RELOAD_MODE,11);
    b.i(2,2);b.write(GGV_STATE_MINOR,2);b.i(2,0);b.write(GGV_RELOAD_COUNT,2);b.ret(0);
    b.end(notReady);b.end(switchRefused);
    b.read(2,GGV_STATE_INUSE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto unused=b.branch(4);
    b.call(GGV_RELOAD_AMMO_STATUS,9,0);
    b.read(2,GGV_WEAPON_NUMBER);b.i(3,32);b.emit(GGV_I_EQ,4,2,3);auto notTimed=b.branch(4);
    b.write(GGV_WEAPON_FUNCTION,0);auto mineDone=b.jump();b.end(notTimed);
    b.read(2,GGV_WEAPON_NUMBER);b.i(3,33);b.emit(GGV_I_EQ,4,2,3);auto notProxy=b.branch(4);
    b.write(GGV_WEAPON_FUNCTION,0);b.end(mineDone);b.end(notProxy);
    b.read(2,GGV_WEAPON_NUMBER);b.i(3,34);b.emit(GGV_I_EQ,4,2,3);auto notRemote=b.branch(4);
    b.read(2,GGV_WEAPON_FUNCTION);b.emit(GGV_I_EQ,4,0,2);b.emit(GGV_NOT,4,4);auto sameFunc=b.branch(4);
    b.i(3,7);b.call(GGV_HAND_SET_STATE,4,3);auto functionRefused=b.branch(4);
    b.read(1,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,1);
    b.end(notRemote);b.end(sameFunc);b.end(functionRefused);
    b.read(2,GGV_RELOAD_DOAUTOSELECT);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto noAuto=b.branch(4);
    b.call(GGV_AUTO_CAPTURE_OTHER);b.boolean(10,true);
    b.read(2,GGV_AUTO_OTHER_INUSE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto otherUnused=b.branch(4);
    b.call(GGV_AUTO_OTHER_AMMO_STATUS,2,3);b.emit(GGV_I_LT,4,2,3);auto noPrimary=b.branch(4);auto primaryDone=b.jump();
    b.end(noPrimary);b.boolean(10,false);b.end(primaryDone);
    b.i(3,1);b.call(GGV_AUTO_OTHER_AMMO_STATUS,2,3);b.i(3,0);b.emit(GGV_I_LT,4,2,3);
    auto noSecondary=b.branch(4);auto secondaryDone=b.jump();b.end(noSecondary);b.boolean(10,false);b.end(secondaryDone);
    b.read(2,GGV_AUTO_OTHER_FUNCTION);b.call(GGV_AUTO_OTHER_ELIGIBILITY,4,2);auto eligible=b.branch(4);
    b.boolean(10,true);b.end(eligible);b.end(otherUnused);
    b.read(2,GGV_RELOAD_OTHER_STATE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);auto notIdle=b.branch(4);auto stateReady=b.jump();
    b.end(notIdle);b.read(2,GGV_RELOAD_OTHER_STATE);b.i(3,8);b.emit(GGV_I_EQ,4,2,3);auto notAuto=b.branch(4);
    auto autoReady=b.jump();b.end(notAuto);b.boolean(10,false);b.end(stateReady);b.end(autoReady);
    auto notSwitch=b.branch(10);b.call(GGV_AUTO_SWITCH_WEAPON);b.end(notSwitch);b.end(noAuto);
    b.i(3,1);b.emit(GGV_I_LE,4,9,3);auto tooMuch=b.branch(4);
    b.i(3,0);b.emit(GGV_I_LT,4,9,3);b.emit(GGV_NOT,4,4);auto noAmmo=b.branch(4);
    b.read(2,GGV_AUTO_FRESH_OTHER_STATE);b.i(3,1);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto otherReload=b.branch(4);
    b.i(2,0);b.write(GGV_RELOAD_COUNT60,2);b.write(GGV_RELOAD_COUNT,2);
    b.i(3,1);b.call(GGV_HAND_SET_STATE,4,3);auto reloadRefused=b.branch(4);
    b.read(2,GGV_WEAPON_NUMBER);b.i(3,26);b.emit(GGV_I_EQ,4,2,3);auto notKnife=b.branch(4);
    b.i(2,11);b.emit(GGV_I_TO_U,11,2);b.write(GGV_RELOAD_MODE,11);
    b.i(2,17);b.read(4,GGV_RELOAD_PAL);auto ntsc17=b.branch(4);b.i(2,14);b.end(ntsc17);
    b.write(GGV_RELOAD_PAUSETIME60,2);b.i(2,0);b.write(GGV_RELOAD_COUNT60,2);
    b.i(2,-1);b.write(GGV_RELOAD_COUNT,2);b.i(2,2);b.write(GGV_STATE_MINOR,2);b.end(notKnife);
    b.read(1,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,1);
    b.end(tooMuch);b.end(noAmmo);b.end(otherReload);b.end(reloadRefused);
    b.read(11,GGV_HAND_MODE_NEXT);b.emit(GGV_U_TO_I,2,11);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto noMode=b.branch(4);
    b.read(11,GGV_HAND_MODE_NEXT);b.write(GGV_RELOAD_MODE,11);
    b.i(2,0);b.write(GGV_RELOAD_COUNT60,2);b.write(GGV_RELOAD_COUNT,2);
    b.emit(GGV_I_TO_U,11,2);b.write(GGV_HAND_MODE_NEXT,11);b.end(noMode);b.end(unused);
    b.f(6,0.87252569198608f);b.call(GGV_RELOAD_SET_ARM_PITCH,0,6);
    b.end(notDelete);b.ret(0);return b.s;
}
constexpr auto autoSwitch=autoSwitchSource();
static_assert(valid(autoSwitch),"Autoswitch must have safe typed control flow");
constexpr ggv_source idleSource() {
    Builder b;
    auto retUpdate=[&]() constexpr {b.read(1,GGV_EMPTY_LVUPDATE);b.emit(GGV_RETURN_I32,0,1);};
    auto setState=[&](int state) constexpr {b.i(3,state);b.call(GGV_HAND_SET_STATE,4,3);};
    auto returnIfSet=[&](int state) constexpr {setState(state);auto refused=b.branch(4);retUpdate();b.end(refused);};
    auto compare=[&](ggv_binding binding,ggv_op op,int value) constexpr {b.read(2,binding);b.i(3,value);b.emit(op,4,2,3);};
    auto opposite=[&]() constexpr {b.read(2,GGV_WEAPON_FUNCTION);b.i(3,1);b.emit(GGV_I_SUB_WRAP,2,3,2);};
    auto clearCounts=[&]() constexpr {b.i(2,0);b.write(GGV_RELOAD_COUNT60,2);b.write(GGV_RELOAD_COUNT,2);};
    auto flag=[&](uint32_t mask) constexpr {b.read(2,GGV_WEAPON_NUMBER);auto &n=b.s.nodes[b.emit(GGV_CONST,9)];n.type=GGV_U32;n.literal.u=mask;b.call(GGV_RELOAD_WEAPON_HAS_FLAG,4,2,9);};
    b.call(GGV_HAND_CURRENT_FUNCTION,0);b.boolean(4,false);b.write(GGV_HAND_LASTDIRVALID,4);
    b.i(2,0);b.write(GGV_BURST_BULLETS,2);
    b.read(4,GGV_RECOVERY_PAL);auto oldVersion=b.branch(4);auto frameReady=b.jump();b.end(oldVersion);
    b.read(2,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_TO_U,8,2);b.write(GGV_ANIM_TICK_INCREMENT_LEGACY,8);b.end(frameReady);
    b.f(10,0);b.write(GGV_REMAINDER,10);
    b.call(GGV_HAND_READY_SWITCH,4);auto notReady=b.branch(4);returnIfSet(5);b.end(notReady);
    b.read(2,GGV_WEAPON_FUNCTION);b.emit(GGV_I_EQ,4,0,2);auto different=b.branch(4);
    b.boolean(4,false);b.write(GGV_HAND_AUTO_BIT07,4);b.end(different);
    b.boolean(4,false);b.write(GGV_HAND_AUTO_BIT08,4);
    compare(GGV_STATE_INUSE,GGV_I_EQ,0);b.emit(GGV_NOT,4,4);auto unused=b.branch(4);
    b.read(2,GGV_WEAPON_FUNCTION);b.call(GGV_RELOAD_AMMO_STATUS,5,2);
    b.read(2,GGV_WEAPON_FUNCTION);b.emit(GGV_I_EQ,4,0,2);b.emit(GGV_NOT,4,4);auto sameFunction=b.branch(4);
    b.read(8,GGV_HAND_MODE_NEXT);b.emit(GGV_U_TO_I,2,8);b.i(3,9);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto reloadNext=b.branch(4);
    b.boolean(7,true);b.read(4,GGV_HAND_AUTO_BIT07);auto noAutoFunction=b.branch(4);
    opposite();b.call(GGV_RELOAD_AMMO_STATUS,2,2);b.i(3,0);b.emit(GGV_I_LT,4,2,3);auto hasAlternate=b.branch(4);
    b.boolean(7,false);b.end(hasAlternate);b.end(noAutoFunction);
    auto noChange=b.branch(7);compare(GGV_WEAPON_NUMBER,GGV_I_EQ,26);auto notKnife=b.branch(4);
    b.i(3,0);b.emit(GGV_I_EQ,4,5,3);auto nonempty=b.branch(4);
    clearCounts();b.write(GGV_WEAPON_FUNCTION,0);returnIfSet(1);auto knifeDone=b.jump();
    b.end(nonempty);b.i(3,0);b.emit(GGV_I_LT,4,5,3);auto positive=b.branch(4);b.boolean(7,false);b.end(positive);
    b.end(knifeDone);b.end(notKnife);b.end(noChange);
    auto keepFunction=b.branch(7);b.boolean(4,false);b.write(GGV_HAND_AUTO_BIT07,4);returnIfSet(7);b.end(keepFunction);
    b.end(sameFunction);b.end(reloadNext);
    b.call(GGV_TICK_IS_V2,4);auto legacy=b.branch(4);
    b.call(GGV_IDLE_V2,4);auto v2Idle=b.branch(4);retUpdate();b.end(v2Idle);
    b.read(8,GGV_HAND_MODE_NEXT);b.emit(GGV_U_TO_I,2,8);b.i(3,9);b.emit(GGV_I_EQ,4,2,3);auto noRequest=b.branch(4);
    auto requested=b.jump();b.end(noRequest);b.read(4,GGV_TRIGGER);b.emit(GGV_NOT,4,4);auto held=b.branch(4);
    b.i(3,0);b.emit(GGV_I_EQ,4,5,3);auto hasClip=b.branch(4);b.end(requested);
    b.i(3,0);b.emit(GGV_I_LT,4,5,3);b.emit(GGV_NOT,4,4);auto noAmmo=b.branch(4);
    b.i(3,2);b.emit(GGV_I_LT,4,5,3);auto full=b.branch(4);
    b.i(2,0);b.emit(GGV_I_TO_U,8,2);b.write(GGV_HAND_MODE_NEXT,8);returnIfSet(1);
    b.end(held);b.end(hasClip);b.end(noAmmo);b.end(full);b.ret(0);b.end(legacy);
    b.i(3,0);b.emit(GGV_I_LT,4,5,3);auto nonnegative=b.branch(4);
    flag(1);auto notThrowable=b.branch(4);compare(GGV_WEAPON_NUMBER,GGV_I_EQ,34);auto notRemote=b.branch(4);
    compare(GGV_HAND_NUMBER,GGV_I_EQ,1);b.emit(GGV_NOT,4,4);auto remoteLeft=b.branch(4);b.end(notRemote);
    returnIfSet(8);b.end(notThrowable);b.end(remoteLeft);
    b.call(GGV_IDLE_STORED_FUNCTION,2);b.emit(GGV_I_EQ,4,2,0);auto notStored=b.branch(4);
    opposite();b.call(GGV_RELOAD_AMMO_STATUS,6,2);opposite();b.call(GGV_IDLE_ELIGIBILITY,4,2);auto notExcluded=b.branch(4);
    compare(GGV_WEAPON_NUMBER,GGV_I_EQ,20);b.emit(GGV_NOT,4,4);auto reaper=b.branch(4);
    b.read(4,GGV_GUNCTRL_WANTAMMO);auto notWant=b.branch(4);
    opposite();b.call(GGV_IDLE_CAPTURE_FUNCTION,0,2);b.read(9,GGV_IDLE_FUNCTION_TYPE);b.uop(GGV_U_AND_LITERAL,9,9,0xff);
    b.emit(GGV_U_TO_I,2,9);b.i(3,3);b.emit(GGV_I_EQ,4,2,3);auto notMelee=b.branch(4);auto meleeDone=b.jump();
    b.end(notMelee);b.i(6,-1);auto wantsDone=b.jump();b.end(notWant);b.i(6,-1);
    b.end(wantsDone);b.end(meleeDone);b.end(notExcluded);b.end(reaper);
    b.i(3,0);b.emit(GGV_I_LT,4,6,3);auto alternateAmmo=b.branch(4);
    b.boolean(4,true);b.write(GGV_HAND_AUTO_BIT08,4);auto alternateDone=b.jump();b.end(alternateAmmo);
    flag(0x04000000);auto unrestricted=b.branch(4);compare(GGV_WEAPON_FUNCTION,GGV_I_EQ,1);auto primary=b.branch(4);b.end(unrestricted);
    b.boolean(4,true);b.write(GGV_HAND_AUTO_BIT07,4);returnIfSet(7);b.end(primary);b.end(alternateDone);b.end(notStored);
    auto ammoDone=b.jump();b.end(nonnegative);
    b.i(3,0);b.emit(GGV_I_EQ,4,5,3);auto clipAmmo=b.branch(4);
    b.read(4,GGV_TRIGGER);auto noTrigger=b.branch(4);compare(GGV_WEAPON_NUMBER,GGV_I_EQ,0);b.emit(GGV_NOT,4,4);auto noWeapon=b.branch(4);
    b.boolean(4,false);b.write(GGV_ANIMATION_LATCH,4);returnIfSet(3);auto emptyDone=b.jump();
    b.end(noTrigger);b.end(noWeapon);clearCounts();setState(1);auto emptyRefused=b.branch(4);
    b.i(2,0);b.emit(GGV_I_TO_U,8,2);b.write(GGV_HAND_MODE_NEXT,8);retUpdate();b.end(emptyRefused);b.end(emptyDone);
    auto emptyAmmoDone=b.jump();b.end(clipAmmo);
    b.read(4,GGV_TRIGGER);auto notTriggered=b.branch(4);auto triggered=b.jump();b.end(notTriggered);
    b.read(4,GGV_HAND_ACTIVATE_SECONDARY);auto noSecondary=b.branch(4);compare(GGV_WEAPON_FUNCTION,GGV_I_EQ,1);auto notSecondary=b.branch(4);b.end(triggered);
    compare(GGV_WEAPON_NUMBER,GGV_I_EQ,0);b.emit(GGV_NOT,4,4);auto noAttackWeapon=b.branch(4);
    b.i(2,0);b.write(GGV_RELOAD_DOAUTOSELECT,2);b.i(2,1);b.emit(GGV_I_TO_U,8,2);b.write(GGV_RELOAD_MODE,8);
    b.i(2,0);b.write(GGV_RELOAD_COUNT,2);b.write(GGV_RELOAD_COUNT60,2);
    b.boolean(4,false);b.write(GGV_TRIGGER_RELEASED,4);b.write(GGV_HAND_ACTIVATE_SECONDARY,4);returnIfSet(4);
    b.end(noSecondary);b.end(notSecondary);b.end(noAttackWeapon);
    b.read(8,GGV_HAND_MODE_NEXT);b.emit(GGV_U_TO_I,2,8);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto noNext=b.branch(4);
    b.read(8,GGV_HAND_MODE_NEXT);b.emit(GGV_U_TO_I,11,8);b.read(8,GGV_HAND_MODE_NEXT);b.write(GGV_RELOAD_MODE,8);
    clearCounts();b.i(2,0);b.emit(GGV_I_TO_U,8,2);b.write(GGV_HAND_MODE_NEXT,8);
    b.i(3,9);b.emit(GGV_I_EQ,4,11,3);auto notReload=b.branch(4);
    b.i(3,2);b.emit(GGV_I_LT,4,5,3);auto clipFull=b.branch(4);b.i(3,0);b.emit(GGV_I_LT,4,5,3);b.emit(GGV_NOT,4,4);auto exhausted=b.branch(4);
    returnIfSet(1);b.end(notReload);b.end(clipFull);b.end(exhausted);b.end(noNext);
    b.end(ammoDone);b.end(emptyAmmoDone);b.end(unused);
    compare(GGV_HAND_NUMBER,GGV_I_EQ,0);auto left=b.branch(4);
    b.read(4,GGV_GUNCTRL_WANTAMMO);auto noWant=b.branch(4);b.call(GGV_AUTO_SWITCH_WEAPON);b.ret(0);b.end(noWant);
    b.call(GGV_IDLE_CAPTURE_OTHER);
    b.read(4,GGV_HAND_AUTO_BIT08);auto handHasAmmo=b.branch(4);auto handEmpty=b.jump();b.end(handHasAmmo);
    compare(GGV_STATE_INUSE,GGV_I_EQ,0);auto handUsed=b.branch(4);b.end(handEmpty);
    b.read(4,GGV_AUTO_OTHER_BIT08);auto otherHasAmmo=b.branch(4);auto otherEmpty=b.jump();b.end(otherHasAmmo);
    compare(GGV_AUTO_OTHER_INUSE,GGV_I_EQ,0);auto otherUsed=b.branch(4);b.end(otherEmpty);
    b.read(4,GGV_TRIGGER);auto noHandTrigger=b.branch(4);auto anyTrigger=b.jump();b.end(noHandTrigger);
    b.read(4,GGV_AUTO_OTHER_TRIGGER);auto noOtherTrigger=b.branch(4);b.end(anyTrigger);b.call(GGV_AUTO_SWITCH_WEAPON);
    b.end(handUsed);b.end(otherUsed);b.end(noOtherTrigger);
    b.boolean(4,false);b.write(GGV_AUTO_OTHER_BIT08,4);b.write(GGV_HAND_AUTO_BIT08,4);
    b.end(left);b.ret(0);return b.s;
}
constexpr auto idle=idleSource();
static_assert(valid(idle),"Idle must have safe typed control flow");
constexpr ggv_source readyToSwitchSource() {
    Builder b;
    auto compare=[&](ggv_binding binding,int value) constexpr {b.read(0,binding);b.i(1,value);b.emit(GGV_I_EQ,2,0,1);};
    b.read(2,GGV_SWITCH_RANGE_VALID);auto notRange=b.branch(2);
    b.call(GGV_RANGE_SLOT,0);b.call(GGV_RANGE_WEAPON,0,0);b.read(1,GGV_SWITCH_RIGHT_WEAPON);
    b.emit(GGV_I_EQ,2,0,1);auto otherRangeWeapon=b.branch(2);
    compare(GGV_SWITCH_CURRENT_THROWING,0);auto throwing=b.branch(2);b.ret(0);
    b.end(notRange);b.end(otherRangeWeapon);b.end(throwing);
    compare(GGV_HAND_NUMBER,0);auto notRight=b.branch(2);
    compare(GGV_SWITCH_LEFT_INUSE,0);b.emit(GGV_NOT,2,2);auto leftUnused=b.branch(2);
    compare(GGV_SWITCH_LEFT_STATE,8);auto notAuto=b.branch(2);
    compare(GGV_SWITCH_LEFT_MINOR,0);auto notUnequip=b.branch(2);b.ret(0);
    b.end(notRight);b.end(leftUnused);b.end(notAuto);b.end(notUnequip);
    b.read(0,GGV_SWITCH_PENDING_WEAPON);b.i(1,0);b.emit(GGV_I_LT,2,0,1);auto pending=b.branch(2);
    // Negative switch target: only the left-hand reconciliation can qualify.
    compare(GGV_HAND_NUMBER,1);auto notLeft=b.branch(2);
    // The repeated native hand-number condition is immutable invocation input.
    for(int state:{1,7,4}) {compare(GGV_SWITCH_RIGHT_STATE,state);auto other=b.branch(2);b.ret(0);b.end(other);}
    compare(GGV_STATE_INUSE,0);b.emit(GGV_NOT,2,2);auto unused=b.branch(2);
    b.read(2,GGV_SWITCH_DUALWIELD);auto noDual=b.branch(2);auto firstDone=b.jump();
    b.end(noDual);b.ret(1);b.end(firstDone);b.end(unused);
    compare(GGV_STATE_INUSE,0);auto used=b.branch(2);b.read(2,GGV_SWITCH_DUALWIELD);
    auto noDualSecond=b.branch(2);b.ret(1);b.end(used);b.end(noDualSecond);
    b.end(notLeft);b.ret(0);b.end(pending);b.ret(1);return b.s;
}
constexpr auto readyToSwitch=readyToSwitchSource();
static_assert(valid(readyToSwitch),"Switch readiness must have safe typed control flow");
constexpr ggv_source usingSecondarySource() {
    Builder b;b.read(0,GGV_FUNCTION_CONTROL_WEAPON);b.call(GGV_STORED_FUNCTION,1,0);
    auto primary=b.branch(1);b.read(0,GGV_FUNCTION_CONTROL_INVERT);b.i(2,0);
    b.emit(GGV_I_EQ,1,0,2);b.emit(GGV_RETURN_BOOL,0,1);b.end(primary);
    b.read(0,GGV_FUNCTION_CONTROL_INVERT);b.i(2,1);b.emit(GGV_I_EQ,1,0,2);
    b.emit(GGV_RETURN_BOOL,0,1);return b.s;
}
constexpr ggv_source handExclusionSource() {
    Builder b;b.call(GGV_EXCLUSION_FUNCTION,0);b.call(GGV_EXCLUSION_WEAPON,1);
    b.call(GGV_HAND_CURRENT_FUNCTION,2);b.read(3,GGV_QUERY_FUNCTION_I32);b.emit(GGV_I_EQ,4,2,3);
    auto otherFunction=b.branch(4);b.ret(0);b.end(otherFunction);
    b.call(GGV_EXCLUSION_CHECK,4,0,1);b.emit(GGV_RETURN_BOOL,0,4);return b.s;
}
constexpr ggv_source anyAmmoSource() {
    Builder b;b.call(GGV_HAND_INFO_CAPTURE);b.i(0,0);b.call(GGV_RELOAD_AMMO_STATUS,1,0);
    b.emit(GGV_I_GT,2,1,0);auto noPrimary=b.branch(2);b.ret(1);b.end(noPrimary);
    b.i(3,1);b.call(GGV_RELOAD_AMMO_STATUS,1,3);b.emit(GGV_I_GT,2,1,0);
    b.emit(GGV_RETURN_BOOL,0,2);return b.s;
}
constexpr auto usingSecondary=usingSecondarySource();
constexpr auto handExclusion=handExclusionSource();
constexpr auto anyAmmo=anyAmmoSource();
static_assert(valid(usingSecondary)&&valid(handExclusion)&&valid(anyAmmo),"Function/ammo predicates must have safe typed flow");
constexpr ggv_source weaponInfoSource() {
    Builder b;b.call(GGV_INFO_WEAPON_QUERY,0);b.write(GGV_INFO_WEAPON_RESULT,0);
    b.call(GGV_INFO_IS_V2,1,0);auto native=b.branch(1);b.call(GGV_INFO_ENSURE_V2);b.end(native);
    b.call(GGV_INFO_IS_V2,1,0);auto nativeDefinition=b.branch(1);
    b.call(GGV_INFO_V2_DEFINITION,2);auto definitionReady=b.jump();
    b.end(nativeDefinition);b.call(GGV_QUERY_FIND_WEAPON,2,0);b.end(definitionReady);
    b.write(GGV_INFO_DEFINITION_RESULT,2);b.read(2,GGV_INFO_GUNCTRL);b.write(GGV_INFO_GUNCTRL,2);b.ret(0);return b.s;
}
constexpr ggv_source switchCompleteSource() {
    Builder b;b.boolean(0,true);b.call(GGV_RELOAD_IS_LOADED,1);auto unloaded=b.branch(1);auto loadDone=b.jump();
    b.end(unloaded);b.boolean(0,false);b.end(loadDone);
    b.read(2,GGV_SWITCH_PENDING_WEAPON);b.i(3,-1);b.emit(GGV_I_EQ,1,2,3);auto pending=b.branch(1);auto pendingDone=b.jump();
    b.end(pending);b.boolean(0,false);b.end(pendingDone);
    b.read(2,GGV_HAND_NUMBER);b.i(3,1);b.emit(GGV_I_EQ,1,2,3);auto right=b.branch(1);
    b.read(2,GGV_SWITCH_DUALWIELD_RAW);b.read(3,GGV_STATE_INUSE);b.emit(GGV_I_EQ,1,2,3);
    auto different=b.branch(1);auto dualDone=b.jump();b.end(different);b.boolean(0,false);b.end(dualDone);b.end(right);
    b.read(2,GGV_LOADED_GUNMEMNEW);b.i(3,0);b.emit(GGV_I_LT,1,2,3);auto newMemory=b.branch(1);auto memoryDone=b.jump();
    b.end(newMemory);b.boolean(0,false);b.end(memoryDone);
    b.read(2,GGV_RELOAD_OTHER_STATE);b.i(3,1);b.emit(GGV_I_EQ,1,2,3);auto otherReady=b.branch(1);
    b.boolean(0,false);b.end(otherReady);b.emit(GGV_RETURN_BOOL,0,0);return b.s;
}
constexpr auto weaponInfo=weaponInfoSource();
constexpr auto switchComplete=switchCompleteSource();
static_assert(valid(weaponInfo)&&valid(switchComplete),"Weapon info and switch completion must have typed flow");
constexpr ggv_source canFreeWeaponSource() {
    Builder b;b.i(0,0);
    auto compare=[&](ggv_binding binding,int value) constexpr {b.read(1,binding);b.i(2,value);b.emit(GGV_I_EQ,3,1,2);};
    compare(GGV_STATE_VALUE,5);auto notChanging=b.branch(3);
    compare(GGV_STATE_MINOR,2);auto notLoading=b.branch(3);
    compare(GGV_FREE_THROWING,0);auto throwing=b.branch(3);b.i(0,1);
    b.end(notChanging);b.end(notLoading);b.end(throwing);
    compare(GGV_STATE_PLAYER_NUMBER,0);auto otherPlayer=b.branch(3);
    compare(GGV_HAND_NUMBER,0);auto left=b.branch(3);
    b.read(1,GGV_FREE_LAST_RIGHT);b.emit(GGV_I_EQ,3,0,1);b.emit(GGV_NOT,3,3);auto unchangedRight=b.branch(3);
    b.write(GGV_FREE_LAST_RIGHT,0);b.call(GGV_FREE_LOG,0,0);b.end(unchangedRight);auto logged=b.jump();
    b.end(left);b.read(1,GGV_FREE_LAST_LEFT);b.emit(GGV_I_EQ,3,0,1);b.emit(GGV_NOT,3,3);auto unchangedLeft=b.branch(3);
    b.write(GGV_FREE_LAST_LEFT,0);b.call(GGV_FREE_LOG,0,0);b.end(unchangedLeft);b.end(logged);
    b.end(otherPlayer);b.emit(GGV_RETURN_I32,0,0);return b.s;
}
constexpr auto canFreeWeapon=canFreeWeaponSource();
constexpr ggv_source freeHeldRocketSource() {
    Builder b;b.read(0,GGV_HELD_ROCKET);b.emit(GGV_REF_NONNULL,1,0);
    auto absent=b.branch(1);b.read(0,GGV_HELD_ROCKET);
    b.call(GGV_FREE_ROCKET_OBJECT,0,0);b.nullref(0);b.write(GGV_HELD_ROCKET,0);
    b.end(absent);b.ret(0);return b.s;
}
constexpr ggv_source storedFunctionSecondarySource() {
    Builder b;b.read(0,GGV_QUERY_WEAPON_I32);b.read(1,GGV_FUNCTION_NATIVE_FIRST);
    b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto belowNative=b.branch(2);
    b.read(1,GGV_FUNCTION_NATIVE_LAST);b.emit(GGV_I_LE,2,0,1);auto aboveNative=b.branch(2);
    b.i(1,1);b.emit(GGV_I_SUB_WRAP,0,0,1);b.emit(GGV_I_TO_U,3,0);
    b.i(1,3);b.emit(GGV_I_TO_U,4,1);b.emit(GGV_U_SHR,5,3,4);b.write(GGV_FUNCTION_BYTE_INDEX,5);
    b.read(6,GGV_FUNCTION_NATIVE_BITS);b.uop(GGV_U_AND_LITERAL,4,3,7);
    b.emit(GGV_U_SHR,6,6,4);b.uop(GGV_MASK,2,6,1);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(belowNative);b.end(aboveNative);
    b.read(1,GGV_FUNCTION_CUSTOM_FIRST);b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto belowCustom=b.branch(2);
    b.read(7,GGV_FUNCTION_CUSTOM_END);b.emit(GGV_I_LT,2,0,7);auto aboveCustom=b.branch(2);
    b.emit(GGV_I_SUB_WRAP,0,0,1);b.emit(GGV_I_TO_U,3,0);b.read(6,GGV_FUNCTION_CUSTOM_BITS);
    b.emit(GGV_U_SHR,6,6,3);b.uop(GGV_MASK,2,6,1);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(belowCustom);b.end(aboveCustom);b.ret(0);return b.s;
}
constexpr auto storedFunctionSecondary=storedFunctionSecondarySource();
static_assert(valid(storedFunctionSecondary),"Stored function query must have typed range and bit selection");
constexpr ggv_source storeFunctionSelectionSource() {
    Builder b;b.read(0,GGV_QUERY_WEAPON_I32);b.read(1,GGV_FUNCTION_NATIVE_FIRST);
    b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto belowNative=b.branch(2);
    b.read(1,GGV_FUNCTION_NATIVE_LAST);b.emit(GGV_I_LE,2,0,1);auto aboveNative=b.branch(2);
    b.i(1,1);b.emit(GGV_I_SUB_WRAP,0,0,1);b.emit(GGV_I_TO_U,3,0);
    b.call(GGV_FUNCTION_CAPTURE_NATIVE);
    b.read(2,GGV_FUNCTION_SECONDARY_INPUT);auto primaryNative=b.branch(2);
    auto nativeMask=[&]() constexpr {
        b.uop(GGV_U_AND_LITERAL,4,3,7);b.i(1,1);b.emit(GGV_I_TO_U,5,1);b.emit(GGV_U_SHL,5,5,4);
        b.i(1,3);b.emit(GGV_I_TO_U,4,1);b.emit(GGV_U_SHR,6,3,4);b.write(GGV_FUNCTION_BYTE_INDEX,6);
    };
    nativeMask();b.read(7,GGV_FUNCTION_NATIVE_BITS);b.emit(GGV_U_OR,7,7,5);
    b.write(GGV_FUNCTION_NATIVE_BITS,7);b.ret(0);
    b.end(primaryNative);nativeMask();b.emit(GGV_U_NOT,5,5);b.uop(GGV_U_AND_LITERAL,5,5,255);
    b.read(7,GGV_FUNCTION_NATIVE_BITS);b.emit(GGV_U_AND,7,7,5);b.write(GGV_FUNCTION_NATIVE_BITS,7);b.ret(0);
    b.end(belowNative);b.end(aboveNative);
    b.read(1,GGV_FUNCTION_CUSTOM_FIRST);b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto belowCustom=b.branch(2);
    b.read(8,GGV_FUNCTION_CUSTOM_END);b.emit(GGV_I_LT,2,0,8);auto aboveCustom=b.branch(2);
    b.emit(GGV_I_SUB_WRAP,0,0,1);b.emit(GGV_I_TO_U,3,0);b.i(1,1);b.emit(GGV_I_TO_U,5,1);b.emit(GGV_U_SHL,5,5,3);
    b.read(2,GGV_FUNCTION_SECONDARY_INPUT);auto primaryCustom=b.branch(2);
    b.call(GGV_FUNCTION_CAPTURE_CUSTOM);b.read(7,GGV_FUNCTION_CUSTOM_BITS);b.emit(GGV_U_OR,7,7,5);
    b.write(GGV_FUNCTION_CUSTOM_BITS,7);b.ret(0);
    b.end(primaryCustom);b.emit(GGV_U_NOT,5,5);b.call(GGV_FUNCTION_CAPTURE_CUSTOM);
    b.read(7,GGV_FUNCTION_CUSTOM_BITS);b.emit(GGV_U_AND,7,7,5);b.write(GGV_FUNCTION_CUSTOM_BITS,7);
    b.end(belowCustom);b.end(aboveCustom);b.ret(0);return b.s;
}
constexpr auto storeFunctionSelection=storeFunctionSelectionSource();
static_assert(valid(storeFunctionSelection),"Function selection stores must preserve typed masks and ranges");
constexpr ggv_source canStoreFunctionSource() {
    Builder b;b.read(0,GGV_QUERY_WEAPON_I32);b.read(1,GGV_FUNCTION_NATIVE_FIRST);
    b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto belowNative=b.branch(2);
    b.read(1,GGV_FUNCTION_NATIVE_LAST);b.emit(GGV_I_LE,2,0,1);auto aboveNative=b.branch(2);b.ret(1);
    b.end(belowNative);b.end(aboveNative);b.read(1,GGV_FUNCTION_CUSTOM_FIRST);
    b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto belowCustom=b.branch(2);
    b.read(1,GGV_FUNCTION_CUSTOM_END);b.emit(GGV_I_LT,2,0,1);b.emit(GGV_RETURN_BOOL,0,2);
    b.end(belowCustom);b.ret(0);return b.s;
}
constexpr auto canStoreFunction=canStoreFunctionSource();
static_assert(valid(canStoreFunction),"Function selection eligibility must preserve typed ranges");
constexpr ggv_source resetFunctionInversionSource() {
    Builder b;b.call(GGV_INFO_WEAPON_QUERY,0);unsigned matched[4];unsigned count=0;
    for(int weapon:{13,14,15,34}) {
        b.i(1,weapon);b.emit(GGV_I_EQ,2,0,1);auto other=b.branch(2);
        b.read(2,GGV_FUNCTION_EXT_CONTROLS);auto standard=b.branch(2);b.ret(0);b.end(standard);
        b.read(2,GGV_FUNCTION_REMOTE_PLAYER);auto local=b.branch(2);b.ret(0);b.end(local);
        matched[count++]=b.jump();b.end(other);
    }
    for(unsigned p:matched)b.end(p);
    b.read(2,GGV_HAND_ACTIVATE_SECONDARY);b.emit(GGV_NOT,2,2);auto active=b.branch(2);
    b.i(1,0);b.write(GGV_FUNCTION_CONTROL_INVERT,1);b.end(active);b.ret(0);return b.s;
}
constexpr auto resetFunctionInversion=resetFunctionInversionSource();
static_assert(valid(resetFunctionInversion),"Function inversion reset must preserve lazy control reads");
constexpr ggv_source considerFunctionToggleSource() {
    Builder b;
    // Capture the short-circuit extended-controls value before the weapon query.
    b.read(3,GGV_FUNCTION_EXT_CONTROLS);auto localControls=b.branch(3);auto controlsReady=b.jump();
    b.end(localControls);b.read(3,GGV_FUNCTION_REMOTE_PLAYER);b.end(controlsReady);
    b.call(GGV_INFO_WEAPON_QUERY,0);
    auto weaponIs=[&](int weapon) constexpr {b.i(1,weapon);b.emit(GGV_I_EQ,2,0,1);};
    auto invert=[&](int value) constexpr {b.i(1,value);b.write(GGV_FUNCTION_CONTROL_INVERT,1);};
    auto activate=[&]() constexpr {b.boolean(2,true);b.write(GGV_HAND_ACTIVATE_SECONDARY,2);};
    weaponIs(21);auto notSniper=b.branch(2);
    auto standardSniper=b.branch(3);b.read(4,GGV_FUNCTION_USE_TIME);b.i(1,0);b.emit(GGV_I_LT,2,4,1);
    auto positiveTime=b.branch(2);b.ret(0);b.end(positiveTime);b.end(standardSniper);
    invert(1);b.read(2,GGV_TRIGGER);auto noTrigger=b.branch(2);
    auto standardTrigger=b.branch(3);activate();b.end(standardTrigger);b.ret(1);b.end(noTrigger);
    b.read(2,GGV_FUNCTION_DEDICATED_BUTTON);auto notDedicated=b.branch(2);activate();b.ret(0);b.end(notDedicated);
    auto nativeTiming=b.branch(3);b.read(4,GGV_FUNCTION_USE_TIME);b.emit(GGV_I_ABS,4,4);b.i(1,0);
    b.emit(GGV_I_LT,2,4,1);auto timeReady=b.jump();b.end(nativeTiming);
    b.read(4,GGV_FUNCTION_USE_TIME);b.read(1,GGV_FUNCTION_HOLD_THRESHOLD);b.emit(GGV_I_LT,2,4,1);b.end(timeReady);
    auto elapsed=b.branch(2);b.ret(0);b.end(elapsed);
    b.call(GGV_HAND_CURRENT_FUNCTION,1);b.i(4,1);b.emit(GGV_I_EQ,2,1,4);auto primary=b.branch(2);
    activate();auto repeat=b.branch(3);b.ret(1);b.end(repeat);b.ret(2);b.end(primary);b.ret(0);
    b.end(notSniper);
    weaponIs(13);auto notRcp=b.branch(2);b.read(2,GGV_TRIGGER);b.emit(GGV_NOT,2,2);auto rcpTriggered=b.branch(2);
    auto rcpStandard=b.branch(3);b.read(2,GGV_FUNCTION_DEDICATED_BUTTON);auto rcpNotDedicated=b.branch(2);
    b.read(5,GGV_RELOAD_DEVICES_ACTIVE);b.uop(GGV_MASK,2,5,0x80);auto cloakOff=b.branch(2);
    b.read(5,GGV_RELOAD_DEVICES_ACTIVE);b.uop(GGV_U_AND_LITERAL,5,5,~uint32_t(0x80));auto cloakReady=b.jump();
    b.end(cloakOff);b.read(5,GGV_RELOAD_DEVICES_ACTIVE);b.uop(GGV_U_AND_LITERAL,5,5,~uint32_t(0x80));
    b.uop(GGV_U_OR_LITERAL,5,5,0x80);b.end(cloakReady);b.write(GGV_RELOAD_DEVICES_ACTIVE,5);invert(0);b.ret(1);
    b.end(rcpTriggered);b.end(rcpStandard);b.end(rcpNotDedicated);auto temporaryRcp=b.jump();b.end(notRcp);
    unsigned temporary[3];unsigned ti=0;
    for(int weapon:{14,15,34}) {weaponIs(weapon);auto other=b.branch(2);temporary[ti++]=b.jump();b.end(other);}
    auto notTemporary=b.jump();b.end(temporaryRcp);for(unsigned p:temporary)b.end(p);
    auto standardTemporary=b.branch(3);b.read(1,GGV_FUNCTION_CONTROL_INVERT);b.i(4,0);b.emit(GGV_I_EQ,2,1,4);
    auto wasActive=b.branch(2);invert(1);auto inversionReady=b.jump();b.end(wasActive);invert(0);auto inversionReady2=b.jump();
    b.end(standardTemporary);invert(1);b.end(inversionReady);b.end(inversionReady2);
    b.read(2,GGV_FUNCTION_ACTIVE_MENU);auto noMenu=b.branch(2);b.call(GGV_FUNCTION_IS_SECONDARY,1);
    b.i(4,1);b.emit(GGV_I_EQ,2,1,4);
    auto notSecondary=b.branch(2);activate();b.end(notSecondary);b.end(noMenu);b.ret(1);b.end(notTemporary);
    unsigned disallow[6];unsigned di=0;
    for(int weapon:{6,10,16,17,22,32}) {weaponIs(weapon);auto other=b.branch(2);disallow[di++]=b.jump();b.end(other);}
    // Default B+Z temporarily inverts without touching the stored selection.
    b.read(2,GGV_TRIGGER);auto defaultNotTrigger=b.branch(2);invert(1);b.ret(1);b.end(defaultNotTrigger);
    auto defaultStore=b.jump();for(unsigned p:disallow)b.end(p);
    b.read(2,GGV_TRIGGER);auto allowed=b.branch(2);b.ret(0);b.end(allowed);b.end(defaultStore);
    b.read(1,GGV_FUNCTION_CONTROL_WEAPON);b.call(GGV_FUNCTION_CAN_STORE,2,1);auto unsupported=b.branch(2);
    b.read(1,GGV_FUNCTION_CONTROL_WEAPON);b.call(GGV_STORED_FUNCTION,2,1);b.emit(GGV_NOT,2,2);
    b.read(1,GGV_FUNCTION_CONTROL_WEAPON);b.call(GGV_FUNCTION_STORE,0,1,2);b.end(unsupported);b.ret(1);return b.s;
}
constexpr auto considerFunctionToggle=considerFunctionToggleSource();
static_assert(valid(considerFunctionToggle),"Function toggle must retain typed control and effect ordering");
constexpr ggv_source autoSelectWeaponSource() {
    Builder b;b.i(15,0);b.read(0,GGV_FUNCTION_CONTROL_WEAPON);
    b.read(12,GGV_STATE_PLAYER_NUMBER);b.emit(GGV_I_EQ,11,12,15);auto otherPlayer=b.branch(11);
    b.call(GGV_SELECT_LOG,0,0);b.end(otherPlayer);
    b.read(12,GGV_SELECT_TICKMODE);b.i(7,6);b.emit(GGV_I_EQ,11,12,7);auto notCutscene=b.branch(11);b.ret(0);b.end(notCutscene);
    b.i(1,-1);b.i(2,-1);b.i(3,0);b.boolean(4,false);b.boolean(5,false);b.i(6,0);
    const unsigned primaryLoop=b.s.count;
    b.boolean(10,false);b.write(GGV_SELECT_INDEX,6);b.read(7,GGV_SELECT_PRIMARY_WEAPON);
    b.call(GGV_SELECT_HAS_SINGLE,11,7);auto notOwned=b.branch(11);
    // The array element is reread after inventory, while weapon/function refs
    // remain captured across eligibility callbacks.
    b.read(7,GGV_SELECT_PRIMARY_WEAPON);b.call(GGV_QUERY_FIND_WEAPON,8,7);
    b.i(12,0);b.call(GGV_SELECT_FIND_FUNCTION,9,7,12);
    auto usable=[&]() constexpr {
        b.call(GGV_EXCLUSION_CHECK,11,9,8);b.emit(GGV_NOT,11,11);auto excluded=b.branch(11);
        b.call(GGV_SELECT_FUNCTION_FLAGS,13,9);b.uop(GGV_MASK,11,13,0x00100000);
        b.emit(GGV_NOT,11,11);auto unselectable=b.branch(11);b.boolean(10,true);
        b.end(unselectable);b.end(excluded);
    };
    usable();b.i(12,18);b.emit(GGV_I_EQ,11,7,12);auto notSuper=b.branch(11);
    b.emit(GGV_I_EQ,11,3,15);auto repeatSuper=b.branch(11);
    b.emit(GGV_I_INCREMENT,3,3);auto skipSecondary=b.jump();b.end(notSuper);b.end(repeatSuper);
    b.i(12,1);b.call(GGV_SELECT_FIND_FUNCTION,9,7,12);usable();b.end(skipSecondary);
    b.emit(GGV_I_EQ,11,7,0);auto notCurrent=b.branch(11);b.boolean(4,true);auto entryDone=b.jump();
    b.end(notCurrent);auto unusable=b.branch(10);b.emit(GGV_I_ADD_WRAP,1,7,15);
    b.i(12,-1);b.emit(GGV_I_EQ,11,2,12);auto firstSet=b.branch(11);b.emit(GGV_I_ADD_WRAP,2,7,15);
    b.end(firstSet);b.end(unusable);b.end(entryDone);b.end(notOwned);
    b.emit(GGV_I_INCREMENT,6,6);b.read(12,GGV_SELECT_PRIMARY_COUNT);b.emit(GGV_I_LT,11,6,12);auto primaryEnd=b.branch(11);
    b.i(12,-1);b.emit(GGV_I_EQ,11,1,12);auto hasSelection=b.branch(11);auto repeatReady=b.jump();
    b.end(hasSelection);b.emit(GGV_NOT,11,4);b.end(repeatReady);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,11)].target=primaryLoop;b.end(primaryEnd);
    auto currentAbsent=b.branch(4);auto currentDone=b.jump();b.end(currentAbsent);b.emit(GGV_I_ADD_WRAP,1,2,15);b.end(currentDone);
    b.i(12,-1);b.emit(GGV_I_EQ,11,1,12);auto hasPrimary=b.branch(11);b.i(1,1);b.end(hasPrimary);
    b.i(12,1);b.emit(GGV_I_EQ,11,1,12);auto noFallback=b.branch(11);
    b.boolean(4,false);b.i(2,-1);b.i(6,0);
    const unsigned secondaryLoop=b.s.count;
    b.read(12,GGV_SELECT_SECONDARY_COUNT);b.emit(GGV_I_LT,11,6,12);auto secondaryEnd=b.branch(11);
    b.write(GGV_SELECT_INDEX,6);b.read(7,GGV_SELECT_SECONDARY_WEAPON);b.call(GGV_SELECT_HAS_SINGLE,11,7);auto secondaryNotOwned=b.branch(11);
    b.emit(GGV_I_EQ,11,7,0);auto secondaryNotCurrent=b.branch(11);b.boolean(4,true);b.end(secondaryNotCurrent);
    b.i(12,-1);b.emit(GGV_I_EQ,11,2,12);auto secondaryFirstSet=b.branch(11);b.emit(GGV_I_ADD_WRAP,2,7,15);
    b.end(secondaryFirstSet);b.end(secondaryNotOwned);b.emit(GGV_I_INCREMENT,6,6);b.boolean(11,true);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,11)].target=secondaryLoop;b.end(secondaryEnd);
    b.emit(GGV_I_ADD_WRAP,1,2,15);b.i(12,-1);b.emit(GGV_I_EQ,11,1,12);auto hasSecondary=b.branch(11);b.i(1,1);b.end(hasSecondary);
    auto fallbackCurrentAbsent=b.branch(4);b.i(1,-1);b.end(fallbackCurrentAbsent);b.boolean(5,true);b.end(noFallback);
    b.emit(GGV_I_LT,11,1,15);b.emit(GGV_NOT,11,11);auto noSwitch=b.branch(11);
    b.emit(GGV_I_EQ,11,1,0);b.emit(GGV_NOT,11,11);auto sameWeapon=b.branch(11);
    b.call(GGV_SELECT_HAS_DOUBLE,11,1,1);auto single=b.branch(11);b.boolean(14,true);auto dualReady=b.jump();
    b.end(single);b.boolean(14,false);b.end(dualReady);b.write(GGV_SWITCH_DUALWIELD,14);
    b.call(GGV_SELECT_EQUIP,0,1);auto noWantAmmo=b.branch(5);b.boolean(14,true);b.write(GGV_GUNCTRL_WANTAMMO,14);
    b.end(noWantAmmo);b.end(sameWeapon);b.end(noSwitch);b.ret(0);return b.s;
}
constexpr auto autoSelectWeapon=autoSelectWeaponSource();
static_assert(valid(autoSelectWeapon),"Auto-selection must retain typed loop and effect ordering");
constexpr ggv_source equipSoundSource() {
    Builder b;b.read(0,GGV_RELOAD_LVUPDATE240);b.i(1,0);b.emit(GGV_I_GT,9,0,1);auto noUpdate=b.branch(9);
    b.read(0,GGV_RELOAD_CAMERA_MODE);b.i(1,1);b.emit(GGV_I_EQ,9,0,1);b.emit(GGV_NOT,9,9);auto thirdPerson=b.branch(9);
    b.call(GGV_RELOAD_IS_LOADED,9);auto unloaded=b.branch(9);
    b.read(9,GGV_RELOAD_PLAYER_INVINCIBLE);b.emit(GGV_NOT,9,9);auto invincible=b.branch(9);
    b.read(9,GGV_RELOAD_PLAYER_DEAD);b.emit(GGV_NOT,9,9);auto dead=b.branch(9);
    b.read(0,GGV_WEAPON_NUMBER);
    auto isWeapon=[&](int weapon) constexpr {b.i(1,weapon);b.emit(GGV_I_EQ,9,0,1);};
    auto simple=[&](ggv_binding sound) constexpr {b.read(1,sound);b.call(GGV_EMPTY_SOUND_START,6,1);b.ret(0);};
    auto pitched=[&](ggv_binding sound,float pitch) constexpr {
        b.f(2,pitch);b.i(3,0);b.read(5,GGV_VERSION_1_OR_LATER);auto oldVersion=b.branch(5);
        b.call(GGV_CURRENT_PRIORITY,3);b.call(GGV_AUDIO_PRIORITY,4);b.emit(GGV_I_INCREMENT,4,4);b.call(GGV_SET_PRIORITY,0,4);b.end(oldVersion);
        b.read(1,sound);b.call(GGV_EMPTY_SOUND_START,6,1);b.emit(GGV_REF_NONNULL,8,6);auto nullHandle=b.branch(8);
        b.emit(GGV_F_BITS_I,7,2);b.call(GGV_SOUND_EVENT,0,6,7);b.end(nullHandle);
        auto noRestore=b.branch(5);b.call(GGV_SET_PRIORITY,0,3);b.end(noRestore);b.ret(0);
    };
    isWeapon(42);auto notScanner=b.branch(9);pitched(GGV_EQUIP_SOUND_SCANNER,3.5f);b.end(notScanner);
    isWeapon(28);auto notTranq=b.branch(9);pitched(GGV_EQUIP_SOUND_GUN,1.5f);b.end(notTranq);
    isWeapon(36);auto notPsychosis=b.branch(9);pitched(GGV_EQUIP_SOUND_GUN,1.5f);b.end(notPsychosis);
    isWeapon(20);auto notReaper=b.branch(9);pitched(GGV_EQUIP_SOUND_GUN,0.85f);b.end(notReaper);
    isWeapon(29);auto notLaser=b.branch(9);simple(GGV_EQUIP_SOUND_LASER);b.end(notLaser);
    isWeapon(26);auto notKnife=b.branch(9);simple(GGV_EQUIP_SOUND_KNIFE);b.end(notKnife);
    isWeapon(34);auto notRemote=b.branch(9);b.read(1,GGV_HAND_NUMBER);b.i(4,0);b.emit(GGV_I_EQ,9,1,4);
    auto leftRemote=b.branch(9);simple(GGV_EQUIP_SOUND_MINE);b.end(leftRemote);b.ret(0);b.end(notRemote);
    for(int weapon:{32,33,45,46,47,48,49,50,53,54,55}) {
        isWeapon(weapon);auto other=b.branch(9);simple(GGV_EQUIP_SOUND_MINE);b.end(other);
    }
    for(int weapon:{0,1,14,27,30,31,35,41,51,52,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70}) {
        isWeapon(weapon);auto other=b.branch(9);b.ret(0);b.end(other);
    }
    for(ggv_binding weapon:{GGV_EQUIP_WEAPON_NECKLACE,GGV_EQUIP_WEAPON_BRIEFCASE2}) {
        b.read(1,weapon);b.emit(GGV_I_EQ,9,0,1);auto other=b.branch(9);b.ret(0);b.end(other);
    }
    simple(GGV_EQUIP_SOUND_GUN);
    b.end(noUpdate);b.end(thirdPerson);b.end(unloaded);b.end(invincible);b.end(dead);b.ret(0);return b.s;
}
constexpr ggv_source changeGunSource() {
    Builder b;
    // The entry definition stays captured; later info-definition reads are fresh.
    b.read(10,GGV_CHANGE_DEFINITION);b.read(11,GGV_PASSED_HAND);b.read(12,GGV_HAND_NUMBER);b.read(13,GGV_EMPTY_LVUPDATE);
    auto eq=[&](ggv_binding field,int value) constexpr {b.read(0,field);b.i(1,value);b.emit(GGV_I_EQ,2,0,1);};
    auto store=[&](ggv_binding field,int value) constexpr {b.i(0,value);b.write(field,0);};
    auto increment=[&]() constexpr {b.read(0,GGV_STATE_MINOR);b.emit(GGV_I_INCREMENT,0,0);b.write(GGV_STATE_MINOR,0);};
    auto mode=[&](int value) constexpr {b.i(0,value);b.emit(GGV_I_TO_U,8,0);b.write(GGV_RELOAD_MODE,8);};
    auto modeIs=[&](int value) constexpr {b.read(8,GGV_RELOAD_MODE);b.emit(GGV_U_TO_I,0,8);b.i(1,value);b.emit(GGV_I_EQ,2,0,1);};
    auto flag=[&](ggv_binding weapon,int mask) constexpr {b.read(7,weapon);b.i(1,mask);b.emit(GGV_I_TO_U,8,1);b.call(GGV_RELOAD_WEAPON_HAS_FLAG,2,7,8);};
    auto ticks=[&](int value) constexpr {b.i(0,value);b.call(GGV_RECOIL_TICKS,3,0);};
    auto animation=[&]() constexpr {b.s.nodes[b.emit(GGV_CALL,0,9,12,11)].service=GGV_HAND_START_ANIMATION;};
    auto maxPitch=[&]() constexpr {b.f(5,0.87252569198608f);b.call(GGV_RELOAD_SET_ARM_PITCH,0,5);};
    auto pitchFraction=[&]() constexpr {b.emit(GGV_I_TO_F,5,0);b.f(6,0.87252569198608f);b.emit(GGV_F_MUL,5,5,6);b.emit(GGV_I_TO_F,6,3);b.emit(GGV_F_DIV,5,5,6);b.call(GGV_RELOAD_SET_ARM_PITCH,0,5);};
    auto throwableEmpty=[&]() constexpr {
        b.boolean(4,false);flag(GGV_WEAPON_NUMBER,1);auto notThrowable=b.branch(2);
        eq(GGV_WEAPON_NUMBER,34);auto notRemote=b.branch(2);b.i(1,1);b.emit(GGV_I_EQ,2,12,1);b.emit(GGV_NOT,2,2);auto remoteLeft=b.branch(2);
        b.end(notRemote);b.i(0,0);b.call(GGV_RELOAD_AMMO_STATUS,0,0);b.i(1,0);b.emit(GGV_I_LT,4,0,1);
        b.end(remoteLeft);b.end(notThrowable);
    };
    eq(GGV_STATE_CYCLES,0);auto laterCycle=b.branch(2);store(GGV_RELOAD_PAUSETIME60,0);b.end(laterCycle);

    eq(GGV_STATE_MINOR,0);auto notUnequip=b.branch(2);
    throwableEmpty();store(GGV_RELOAD_COUNT,0);b.emit(GGV_NOT,2,4);auto skipped=b.branch(2);
    b.call(GGV_CHANGE_UNEQUIP_ANIM,9,10);b.emit(GGV_REF_NONNULL,2,9);auto noUnequip=b.branch(2);
    eq(GGV_STATE_INUSE,1);auto notInUse=b.branch(2);
    eq(GGV_RELOAD_EJECT_STATE,0);auto activeEject=b.branch(2);auto inactive=b.jump();b.end(activeEject);
    eq(GGV_CHANGE_EJECT_TYPE,0);b.emit(GGV_NOT,2,2);auto gunEject=b.branch(2);b.end(inactive);
    eq(GGV_STATE_CYCLES,0);auto animStarted=b.branch(2);b.call(GGV_CHANGE_UNEQUIP_ANIM,9,10);animation();auto firstCycle=b.jump();
    b.end(animStarted);eq(GGV_EMPTY_ANIM_MODE,0);auto animBusy=b.branch(2);increment();b.end(animBusy);b.end(firstCycle);auto validAnimation=b.jump();
    b.end(noUnequip);b.end(notInUse);b.end(gunEject);
    b.read(8,GGV_STATE_FLAGS);b.uop(GGV_U_OR_LITERAL,8,8,1);b.write(GGV_STATE_FLAGS,8);
    eq(GGV_RELOAD_EJECT_STATE,1);auto notInitialEject=b.branch(2);b.ret(0);b.end(notInitialEject);increment();auto fallback=b.jump();
    b.end(skipped);increment();b.end(fallback);b.end(validAnimation);
    eq(GGV_STATE_MINOR,1);auto notLowerYet=b.branch(2);store(GGV_STATE_FRAMES,0);b.end(notLowerYet);b.end(notUnequip);

    eq(GGV_STATE_MINOR,1);auto notLower=b.branch(2);ticks(16);store(GGV_RELOAD_COUNT,0);
    b.read(2,GGV_NORMAL_MULTIPLAYER);auto singleLower=b.branch(2);ticks(12);b.end(singleLower);
    b.call(GGV_CHANGE_UNEQUIP_ANIM,9,10);b.emit(GGV_REF_NONNULL,2,9);auto noLowerAnim=b.branch(2);
    b.read(8,GGV_STATE_FLAGS);b.uop(GGV_MASK,2,8,1);b.emit(GGV_NOT,2,2);auto forcedLower=b.branch(2);b.i(3,1);b.end(forcedLower);b.end(noLowerAnim);
    eq(GGV_STATE_INUSE,0);auto usingHand=b.branch(2);b.i(3,1);b.end(usingHand);
    b.boolean(4,false);eq(GGV_CHANGE_EJECT_TYPE,0);auto notGun=b.branch(2);
    eq(GGV_RELOAD_EJECT_STATE,1);auto notInit=b.branch(2);auto init=b.jump();b.end(notInit);eq(GGV_RELOAD_EJECT_STATE,2);auto notAirborne=b.branch(2);
    b.end(init);b.boolean(4,true);b.end(notAirborne);b.end(notGun);
    eq(GGV_SWITCH_CURRENT_THROWING,1);auto notThrowing=b.branch(2);b.boolean(4,true);b.end(notThrowing);
    b.read(0,GGV_STATE_FRAMES);b.emit(GGV_I_LT,2,0,3);b.emit(GGV_NOT,2,2);auto stillLowering=b.branch(2);
    b.emit(GGV_NOT,2,4);auto throwing=b.branch(2);
    b.read(2,GGV_MULTIPLAYER);auto noDelete=b.branch(2);b.call(GGV_HAND_DELETE_WEAPON);b.end(noDelete);
    b.call(GGV_HAND_FREE_ROCKET);mode(6);increment();auto lowered=b.jump();
    b.end(throwing);maxPitch();eq(GGV_SWITCH_CURRENT_THROWING,1);auto noFreshThrow=b.branch(2);eq(GGV_STATE_INUSE,0);b.emit(GGV_NOT,2,2);auto unusedThrow=b.branch(2);
    b.boolean(2,true);b.write(GGV_FIRING,2);store(GGV_ATTACK_TYPE,3);store(GGV_WEAPON_FUNCTION,1);b.end(unusedThrow);b.end(noFreshThrow);b.end(lowered);auto lowerDone=b.jump();
    b.end(stillLowering);b.read(0,GGV_STATE_FRAMES);pitchFraction();b.end(lowerDone);b.end(notLower);

    eq(GGV_STATE_MINOR,2);auto notLoad=b.branch(2);store(GGV_EMPTY_ANIM_MODE,0);
    eq(GGV_STATE_PLAYER_NUMBER,0);auto otherPlayer=b.branch(2);b.i(1,0);b.emit(GGV_I_EQ,2,12,1);auto otherHand=b.branch(2);
    b.read(0,GGV_CURRENT_FRAME);b.i(1,0);b.emit(GGV_I_LT,2,0,1);b.emit(GGV_NOT,2,2);auto negativeFrame=b.branch(2);
    b.emit(GGV_I_TO_U,8,0);b.uop(GGV_U_MOD_LITERAL,8,8,120);b.emit(GGV_U_TO_I,0,8);b.i(1,13);b.emit(GGV_I_EQ,2,0,1);auto quietFrame=b.branch(2);
    b.call(GGV_RELOAD_IS_LOADED,2);b.write(GGV_CHANGE_DIAG_LOADED,2);
    eq(GGV_CHANGE_SWITCHTO,-1);b.write(GGV_CHANGE_DIAG_SWITCHTO,2);
    b.read(0,GGV_LOADED_GUNMEMNEW);b.i(1,0);b.emit(GGV_I_LT,2,0,1);b.write(GGV_CHANGE_DIAG_GUNMEM,2);
    eq(GGV_RELOAD_OTHER_STATE,1);b.emit(GGV_NOT,2,2);b.write(GGV_CHANGE_DIAG_OTHER,2);
    b.call(GGV_CHANGE_COMPLETE,0);b.write(GGV_CHANGE_DIAG_RESULT,0);b.call(GGV_CHANGE_LOG_LOAD);
    b.end(quietFrame);b.end(negativeFrame);b.end(otherHand);b.end(otherPlayer);
    eq(GGV_CHANGE_PAUSECHANGE,0);auto paused=b.branch(2);auto unpaused=b.jump();b.end(paused);
    b.read(0,GGV_RELOAD_PAUSETIME60);b.read(7,GGV_RELOAD_COUNT60);b.emit(GGV_I_LE,2,0,7);auto waiting=b.branch(2);b.end(unpaused);
    modeIs(6);auto otherMode=b.branch(2);b.call(GGV_CHANGE_COMPLETE,0);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);b.emit(GGV_NOT,2,2);auto incomplete=b.branch(2);
    mode(7);eq(GGV_STATE_INUSE,0);auto inUse=b.branch(2);b.i(0,0);b.call(GGV_HAND_SET_STATE,2,0);auto refusedIdle=b.branch(2);b.emit(GGV_RETURN_I32,0,13);
    b.end(refusedIdle);b.end(inUse);b.end(incomplete);auto modeSixDone=b.jump();
    b.end(otherMode);b.call(GGV_RELOAD_IS_LOADED,2);auto notLoaded=b.branch(2);
    b.read(9,GGV_CHANGE_DEFINITION);b.call(GGV_CHANGE_EQUIP_ANIM,9,9);b.emit(GGV_REF_NONNULL,2,9);auto noEquipAnim=b.branch(2);
    b.read(9,GGV_CHANGE_DEFINITION);b.call(GGV_CHANGE_EQUIP_ANIM,9,9);animation();b.boolean(2,true);b.write(GGV_RELOAD_UNK0CC8_02,2);b.end(noEquipAnim);
    mode(8);increment();store(GGV_RELOAD_COUNT60,0);store(GGV_RELOAD_COUNT,0);b.end(notLoaded);b.end(modeSixDone);b.end(waiting);
    modeIs(6);auto notMode6=b.branch(2);auto pitchSix=b.jump();b.end(notMode6);modeIs(7);auto notMode7=b.branch(2);b.end(pitchSix);maxPitch();b.end(notMode7);b.end(notLoad);

    eq(GGV_STATE_MINOR,3);auto notRaise=b.branch(2);ticks(23);b.read(2,GGV_NORMAL_MULTIPLAYER);auto singleRaise=b.branch(2);ticks(12);b.end(singleRaise);
    flag(GGV_AMMO_HAND_WEAPON,0x4000);auto notSpecial=b.branch(2);store(GGV_EMPTY_ANIM_MODE,0);auto special=b.jump();
    b.end(notSpecial);b.call(GGV_CHANGE_EQUIP_ANIM,9,10);b.emit(GGV_REF_NONNULL,2,9);auto noCapturedEquip=b.branch(2);b.i(3,1);b.end(noCapturedEquip);b.end(special);
    eq(GGV_RELOAD_COUNT,0);auto laterRaise=b.branch(2);b.read(2,GGV_MULTIPLAYER);auto noCreate=b.branch(2);b.call(GGV_CHANGE_CREATE_WEAPON);b.end(noCreate);
    b.call(GGV_CHANGE_ACTIVATE);throwableEmpty();auto notEmpty=b.branch(4);b.i(0,8);b.call(GGV_HAND_SET_STATE,2,0);auto refusedAuto=b.branch(2);
    store(GGV_STATE_MINOR,1);b.emit(GGV_RETURN_I32,0,13);b.end(refusedAuto);b.end(notEmpty);
    store(GGV_RELOAD_DOAUTOSELECT,0);b.call(GGV_CHANGE_EQUIP_SOUND);b.end(laterRaise);
    b.read(0,GGV_RELOAD_COUNT60);b.emit(GGV_I_LT,2,0,3);auto raisedByTime=b.branch(2);
    b.read(7,GGV_WEAPON_NUMBER);b.call(GGV_RELOAD_FILENUM2,0,7);b.i(1,0);b.emit(GGV_I_EQ,2,0,1);b.emit(GGV_NOT,2,2);auto noFile=b.branch(2);
    flag(GGV_WEAPON_NUMBER,0x40);auto noFlag40=b.branch(2);flag(GGV_WEAPON_NUMBER,0x80);b.emit(GGV_NOT,2,2);auto flag80=b.branch(2);
    b.read(0,GGV_RELOAD_COUNT60);b.emit(GGV_I_SUB_WRAP,0,3,0);pitchFraction();auto raising=b.jump();
    b.end(raisedByTime);b.end(noFile);b.end(noFlag40);b.end(flag80);mode(0);increment();
    flag(GGV_AMMO_HAND_WEAPON,0x4000);b.emit(GGV_NOT,2,2);auto keepLatch=b.branch(2);b.boolean(2,false);b.write(GGV_RELOAD_UNK0CC8_02,2);b.end(keepLatch);
    store(GGV_RELOAD_COUNT60,0);store(GGV_RELOAD_COUNT,0);b.end(raising);b.end(notRaise);

    eq(GGV_STATE_MINOR,4);auto notEquip=b.branch(2);
    b.read(9,GGV_CHANGE_DEFINITION);b.call(GGV_CHANGE_EQUIP_ANIM,9,9);b.emit(GGV_REF_NONNULL,2,9);auto noFinalAnim=b.branch(2);
    flag(GGV_AMMO_HAND_WEAPON,0x4000);b.emit(GGV_NOT,2,2);auto finalSpecial=b.branch(2);eq(GGV_EMPTY_ANIM_MODE,0);auto finalBusy=b.branch(2);
    b.end(noFinalAnim);b.end(finalSpecial);b.i(0,0);b.call(GGV_HAND_SET_STATE,2,0);auto finalRefused=b.branch(2);b.emit(GGV_RETURN_I32,0,13);
    b.end(finalRefused);b.end(finalBusy);b.end(notEquip);b.ret(0);return b.s;
}
constexpr ggv_source animationSoundCommandsSource() {
    Builder b;b.read(0,GGV_ANIM_FRAME_COMMAND);b.emit(GGV_REF_NONNULL,10,0);auto absent=b.branch(10);
    b.read(0,GGV_ANIM_FRAME_COMMAND);b.write(GGV_ANIM_WAIT_CURSOR,0);b.f(8,1.0f);b.boolean(9,false);
    b.emit(GGV_REF_NONNULL,10,0);auto nullCursor=b.branch(10);
    const unsigned loop=b.s.count;b.boolean(11,false);
    b.read(12,GGV_ANIM_WAIT_TYPE);b.i(13,0);b.emit(GGV_I_EQ,10,12,13);auto command=b.branch(10);
    b.boolean(11,true);auto endCommand=b.jump();b.end(command);
    b.read(14,GGV_COMMAND_FLOAT_FRAMES);auto integerFrames=b.branch(14);
    b.read(3,GGV_COMMAND_TO_FLOAT);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_TO_F,4,1);
    b.emit(GGV_F_GE,10,3,4);auto floatFuture=b.branch(10);
    b.read(2,GGV_COMMAND_FROM_FLOAT);b.emit(GGV_F_LT,10,2,4);auto floatPast=b.branch(10);
    b.emit(GGV_F_LT,10,2,3);auto floatStationary=b.branch(10);auto floatingReady=b.jump();
    b.end(integerFrames);b.read(6,GGV_COMMAND_TO_INT);b.read(1,GGV_ANIM_WAIT_KEY);
    b.emit(GGV_I_LT,10,6,1);b.emit(GGV_NOT,10,10);auto intFuture=b.branch(10);
    b.read(5,GGV_COMMAND_FROM_INT);b.emit(GGV_I_LT,10,5,1);auto intPast=b.branch(10);
    b.emit(GGV_I_LT,10,5,6);auto intStationary=b.branch(10);b.end(floatingReady);
    b.read(12,GGV_ANIM_WAIT_TYPE);b.i(13,5);b.emit(GGV_I_EQ,10,12,13);auto notSound=b.branch(10);
    b.read(14,GGV_VERSION_1_OR_LATER);auto legacyAudio=b.branch(14);
    auto noSpeed=b.branch(9);b.read(15,GGV_ANIM_WAIT_VALUE);b.call(GGV_COMMAND_SOUND_MODERN,16,15,8);
    b.boolean(9,false);auto speedUsed=b.jump();b.end(noSpeed);b.read(15,GGV_ANIM_WAIT_VALUE);b.f(17,1.0f);
    b.call(GGV_COMMAND_SOUND_MODERN,16,15,17);b.end(speedUsed);auto modernDone=b.jump();
    b.end(legacyAudio);b.read(15,GGV_ANIM_WAIT_VALUE);b.call(GGV_COMMAND_SOUND_LEGACY,16,15);
    auto noLegacySpeed=b.branch(9);b.emit(GGV_REF_NONNULL,10,16);auto noHandle=b.branch(10);
    b.boolean(9,false);b.emit(GGV_F_BITS_I,13,8);b.call(GGV_SOUND_EVENT,0,16,13);
    b.end(noHandle);b.end(noLegacySpeed);b.end(modernDone);auto soundDone=b.jump();
    b.end(notSound);b.i(13,11);b.emit(GGV_I_EQ,10,12,13);auto notSpeed=b.branch(10);
    b.read(15,GGV_ANIM_WAIT_VALUE);b.emit(GGV_I64_TO_F,8,15);b.f(17,1000.0f);b.emit(GGV_F_DIV,8,8,17);b.boolean(9,true);auto speedDone=b.jump();
    b.end(notSpeed);b.i(13,9);b.emit(GGV_I_EQ,10,12,13);auto notOutput=b.branch(10);
    b.read(18,GGV_COMMAND_OUTPUT);b.emit(GGV_U_TO_I,13,18);b.emit(GGV_I_INCREMENT,13,13);b.emit(GGV_I_TO_U,18,13);
    b.uop(GGV_U_AND_LITERAL,18,18,7);b.write(GGV_COMMAND_OUTPUT,18);
    b.end(notOutput);b.end(speedDone);b.end(soundDone);b.end(floatFuture);b.end(floatPast);b.end(floatStationary);
    b.end(intFuture);b.end(intPast);b.end(intStationary);b.end(endCommand);
    b.read(0,GGV_ANIM_WAIT_CURSOR);b.call(GGV_ANIM_CURSOR_NEXT,0,0);b.write(GGV_ANIM_WAIT_CURSOR,0);
    b.emit(GGV_NOT,10,11);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,10)].target=loop;
    b.end(nullCursor);b.end(absent);b.ret(0);return b.s;
}
constexpr ggv_source animationPartCommandSource() {
    Builder b;b.i(0,0);b.i(6,-1);b.read(1,GGV_PART_COUNT);b.emit(GGV_I_LT,2,0,1);auto empty=b.branch(2);
    const unsigned loop=b.s.count;b.write(GGV_PART_INDEX,0);b.read(4,GGV_PART_NUMBER);b.emit(GGV_I_TO_I64,5,4);
    b.read(3,GGV_ANIM_WAIT_VALUE);b.emit(GGV_I64_EQ,2,3,5);auto different=b.branch(2);
    b.i(7,0);b.emit(GGV_I_ADD_WRAP,6,0,7);b.end(different);
    b.emit(GGV_I_INCREMENT,0,0);b.read(1,GGV_PART_COUNT);b.emit(GGV_I_LT,2,0,1);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,2)].target=loop;
    b.end(empty);b.write(GGV_PART_INDEX,6);b.i(7,-1);b.emit(GGV_I_EQ,2,6,7);auto existing=b.branch(2);
    b.read(0,GGV_PART_COUNT);b.emit(GGV_I_INCREMENT,7,0);b.call(GGV_PART_RESERVE,2,7);auto failed=b.branch(2);
    b.write(GGV_PART_INDEX,0);b.write(GGV_PART_COUNT,7);b.read(3,GGV_ANIM_WAIT_VALUE);b.emit(GGV_I64_TO_I32,4,3);
    b.write(GGV_PART_NUMBER,4);b.i(4,-1);b.write(GGV_PART_FRAME,4);b.end(existing);
    b.read(0,GGV_ANIM_WAIT_KEY);b.read(1,GGV_PART_FRAME);b.emit(GGV_I_GT,2,0,1);auto older=b.branch(2);
    b.write(GGV_PART_FRAME,0);b.read(0,GGV_ANIM_WAIT_TYPE);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);b.write(GGV_PART_VISIBLE,2);
    b.end(older);b.ret(0);b.end(failed);b.ret(-1);return b.s;
}
constexpr ggv_source animationFirstCommandsSource() {
    Builder b;
    struct Gate {unsigned edges[6];};
    auto cross=[&]() constexpr {
        Gate g{};b.read(4,GGV_COMMAND_FLOAT_FRAMES);auto integers=b.branch(4);
        b.read(7,GGV_COMMAND_TO_FLOAT);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_TO_F,8,1);
        b.emit(GGV_F_GE,3,7,8);g.edges[0]=b.branch(3);b.read(6,GGV_COMMAND_FROM_FLOAT);
        b.emit(GGV_F_LT,3,6,8);g.edges[1]=b.branch(3);b.emit(GGV_F_LT,3,6,7);g.edges[2]=b.branch(3);auto ready=b.jump();
        b.end(integers);b.read(13,GGV_COMMAND_TO_INT);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_LT,3,13,1);b.emit(GGV_NOT,3,3);g.edges[3]=b.branch(3);
        b.read(12,GGV_COMMAND_FROM_INT);b.emit(GGV_I_LT,3,12,1);g.edges[4]=b.branch(3);b.emit(GGV_I_LT,3,12,13);g.edges[5]=b.branch(3);b.end(ready);return g;
    };
    auto endCross=[&](Gate g) constexpr {for(unsigned edge:g.edges)b.end(edge);};
    b.call(GGV_ANIM_WAIT_FRAME,9);b.read(4,GGV_COMMAND_FLOAT_FRAMES);auto initialInteger=b.branch(4);
    b.write(GGV_COMMAND_FROM_FLOAT,9);b.read(10,GGV_ANIM_WAIT_INCREMENT_REAL);b.emit(GGV_F_ADD,7,10,9);b.write(GGV_COMMAND_TO_FLOAT,7);
    b.f(11,0);b.emit(GGV_F_EQ,3,9,11);auto floatNotZero=b.branch(3);b.emit(GGV_F_GT,3,7,11);auto floatNotAdvancing=b.branch(3);
    b.f(11,1);b.emit(GGV_F_SUB,9,9,11);b.write(GGV_COMMAND_FROM_FLOAT,9);b.end(floatNotAdvancing);b.end(floatNotZero);auto floatInitialDone=b.jump();
    b.end(initialInteger);b.emit(GGV_TRUNCATE,12,9);b.write(GGV_COMMAND_FROM_INT,12);b.read(14,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_ADD_WRAP,13,14,12);b.write(GGV_COMMAND_TO_INT,13);
    b.i(2,0);b.emit(GGV_I_EQ,3,12,2);auto intNotZero=b.branch(3);b.emit(GGV_I_GT,3,13,2);auto intNotAdvancing=b.branch(3);
    b.i(2,1);b.emit(GGV_I_SUB_WRAP,12,12,2);b.write(GGV_COMMAND_FROM_INT,12);b.end(intNotAdvancing);b.end(intNotZero);b.end(floatInitialDone);
    b.read(0,GGV_ANIM_FRAME_COMMAND);b.emit(GGV_REF_NONNULL,3,0);auto absent=b.branch(3);
    b.read(0,GGV_ANIM_FRAME_COMMAND);b.write(GGV_ANIM_WAIT_CURSOR,0);b.emit(GGV_REF_NONNULL,3,0);auto nullCursor=b.branch(3);
    b.i(1,0);b.write(GGV_PART_COUNT,1);
    const unsigned loop=b.s.count;b.boolean(5,false);b.read(1,GGV_ANIM_WAIT_TYPE);b.i(2,0);b.emit(GGV_I_EQ,3,1,2);auto notEnd=b.branch(3);
    b.boolean(5,true);auto endCommand=b.jump();b.end(notEnd);
    b.i(2,1);b.emit(GGV_I_EQ,3,1,2);auto notShow=b.branch(3);auto visible=b.jump();b.end(notShow);
    b.read(1,GGV_ANIM_WAIT_TYPE);b.i(2,2);b.emit(GGV_I_EQ,3,1,2);auto notVisibility=b.branch(3);b.end(visible);
    b.read(4,GGV_COMMAND_FLOAT_FRAMES);auto integerDue=b.branch(4);b.read(7,GGV_COMMAND_TO_FLOAT);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_TO_F,8,1);
    b.emit(GGV_F_GE,3,7,8);auto floatNotDue=b.branch(3);auto floatDue=b.jump();b.end(integerDue);
    b.read(13,GGV_COMMAND_TO_INT);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_LT,3,13,1);b.emit(GGV_NOT,3,3);auto intNotDue=b.branch(3);b.end(floatDue);
    b.call(GGV_COMMAND_ACCUMULATE_PART,3);auto allocationFailed=b.branch(3);b.end(floatNotDue);b.end(intNotDue);auto visibilityDone=b.jump();
    b.end(notVisibility);b.read(1,GGV_ANIM_WAIT_TYPE);b.i(2,3);b.emit(GGV_I_EQ,3,1,2);auto notRelease=b.branch(3);
    b.read(3,GGV_ANIMATION_LATCH);auto released=b.branch(3);auto releaseGate=cross();b.read(4,GGV_COMMAND_FLOAT_FRAMES);auto integerRelease=b.branch(4);
    b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_TO_F,8,1);b.call(GGV_ANIM_WAIT_FRAME,10);b.emit(GGV_F_SUB,9,8,10);b.f(11,2);b.emit(GGV_F_DIV,9,9,11);
    b.read(10,GGV_ANIM_WAIT_INCREMENT_REAL);b.emit(GGV_F_GT,3,10,9);auto noFloatClamp=b.branch(3);
    b.read(25,GGV_RELOAD_PAL);auto ntscClamp=b.branch(25);b.f(11,0.83333333f);b.emit(GGV_F_MUL,9,9,11);b.end(ntscClamp);
    b.call(GGV_MODEL_ABS_ANIM_SPEED,10);b.emit(GGV_F_DIV,9,9,10);b.emit(GGV_TRUNCATE,14,9);b.write(GGV_ANIM_WAIT_INCREMENT,14);
    b.call(GGV_MODEL_ABS_ANIM_SPEED,10);b.read(14,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_TO_F,9,14);b.read(25,GGV_RELOAD_PAL);auto ntscReal=b.branch(25);
    b.f(11,1.2f);b.emit(GGV_F_MUL,9,9,11);b.end(ntscReal);b.emit(GGV_F_MUL,9,10,9);b.write(GGV_ANIM_WAIT_INCREMENT_REAL,9);b.end(noFloatClamp);
    b.read(10,GGV_ANIM_WAIT_INCREMENT_REAL);b.read(6,GGV_COMMAND_FROM_FLOAT);b.emit(GGV_F_ADD,7,10,6);b.write(GGV_COMMAND_TO_FLOAT,7);auto floatReleaseDone=b.jump();
    b.end(integerRelease);b.read(1,GGV_ANIM_WAIT_KEY);b.call(GGV_ANIM_WAIT_FRAME,9);b.emit(GGV_TRUNCATE,14,9);b.emit(GGV_I_SUB_WRAP,14,1,14);b.i(2,2);b.emit(GGV_I_DIV_TRUNC,14,14,2);
    b.read(1,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_GT,3,1,14);auto noIntClamp=b.branch(3);b.write(GGV_ANIM_WAIT_INCREMENT,14);b.end(noIntClamp);
    b.read(1,GGV_ANIM_WAIT_INCREMENT);b.read(12,GGV_COMMAND_FROM_INT);b.emit(GGV_I_ADD_WRAP,13,1,12);b.write(GGV_COMMAND_TO_INT,13);
    b.end(floatReleaseDone);endCross(releaseGate);b.end(released);auto releaseDone=b.jump();
    b.end(notRelease);b.i(2,8);b.emit(GGV_I_EQ,3,1,2);auto notRepeat=b.branch(3);b.read(3,GGV_ANIM_INCREMENTAL);auto notIncremental=b.branch(3);
    auto repeatGate=cross();b.read(4,GGV_COMMAND_FLOAT_FRAMES);auto integerRepeat=b.branch(4);
    b.read(15,GGV_ANIM_WAIT_VALUE);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_TO_I64,16,1);b.emit(GGV_I64_SUB,15,15,16);b.emit(GGV_I64_TO_F,10,15);
    b.f(11,0);b.emit(GGV_F_LT,3,10,11);auto notBackward=b.branch(3);b.read(18,GGV_COMMAND_TO_FLOAT);b.emit(GGV_I_TO_F,8,1);
    b.emit(GGV_F_GE,3,18,8);auto alreadyWrapped=b.branch(3);
    const unsigned wrap=b.s.count;b.emit(GGV_F_ADD,19,18,10);b.emit(GGV_F_LT,3,19,18);auto stalled=b.branch(3);
    b.emit(GGV_F_COPY,18,19);b.emit(GGV_F_GE,3,18,8);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,3)].target=wrap;b.end(alreadyWrapped);
    b.write(GGV_COMMAND_FROM_FLOAT,18);b.i(1,0);b.write(GGV_ANIM_WAIT_INCREMENT,1);b.f(9,0);b.write(GGV_ANIM_WAIT_INCREMENT_REAL,9);
    b.call(GGV_COMMAND_SET_FRAME,0,18);b.read(1,GGV_ANIM_LOOP_COUNT);b.emit(GGV_I_INCREMENT,1,1);b.write(GGV_ANIM_LOOP_COUNT,1);b.write(GGV_COMMAND_TO_FLOAT,18);auto floatRepeatDone=b.jump();
    b.end(integerRepeat);b.read(13,GGV_COMMAND_TO_INT);b.read(1,GGV_ANIM_WAIT_KEY);b.emit(GGV_I_SUB_WRAP,14,13,1);b.emit(GGV_I_TO_I64,15,14);
    b.emit(GGV_I_TO_I64,16,1);b.read(17,GGV_ANIM_WAIT_VALUE);b.emit(GGV_I64_SUB,16,16,17);
    auto one=b.emit(GGV_CONST,27);b.s.nodes[one].type=GGV_I64;b.s.nodes[one].literal.i64=1;
    b.emit(GGV_I64_ADD,16,16,27);b.emit(GGV_I64_REM,15,15,16);b.emit(GGV_I64_ADD,15,17,15);b.emit(GGV_I64_TO_I32,14,15);
    b.write(GGV_COMMAND_FROM_INT,14);b.i(1,0);b.write(GGV_ANIM_WAIT_INCREMENT,1);b.emit(GGV_I_TO_F,18,14);b.call(GGV_COMMAND_SET_FRAME,0,18);
    b.read(1,GGV_ANIM_LOOP_COUNT);b.emit(GGV_I_INCREMENT,1,1);b.write(GGV_ANIM_LOOP_COUNT,1);b.write(GGV_COMMAND_TO_INT,14);
    b.end(floatRepeatDone);endCross(repeatGate);b.end(notIncremental);b.end(notRepeat);b.end(releaseDone);b.end(visibilityDone);b.end(endCommand);
    b.read(0,GGV_ANIM_WAIT_CURSOR);b.call(GGV_ANIM_CURSOR_NEXT,0,0);b.write(GGV_ANIM_WAIT_CURSOR,0);b.emit(GGV_NOT,26,5);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,26)].target=loop;
    b.i(22,0);b.read(23,GGV_PART_COUNT);b.emit(GGV_I_LT,3,22,23);auto noParts=b.branch(3);
    const unsigned apply=b.s.count;b.write(GGV_PART_INDEX,22);b.read(20,GGV_PART_NUMBER);b.read(21,GGV_PART_VISIBLE);b.call(GGV_COMMAND_SET_PART,0,20,21);
    b.emit(GGV_I_INCREMENT,22,22);b.read(23,GGV_PART_COUNT);b.emit(GGV_I_LT,3,22,23);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,3)].target=apply;
    b.end(noParts);b.end(nullCursor);b.end(absent);b.ret(0);b.end(allocationFailed);b.end(notBackward);b.end(stalled);b.ret(-1);return b.s;
}
constexpr ggv_source animationDriverSource() {
    Builder b;
    auto clearIncrement=[&]() constexpr {
        b.i(0,0);b.write(GGV_ANIM_WAIT_INCREMENT,0);b.read(13,GGV_COMMAND_FLOAT_FRAMES);auto legacy=b.branch(13);
        b.f(6,0);b.write(GGV_ANIM_WAIT_INCREMENT_REAL,6);b.end(legacy);
    };
    auto setAnimation=[&]() constexpr {
        b.read(0,GGV_ANIM_LOAD);b.emit(GGV_F_MUL,9,5,4);b.call(GGV_COMMAND_MODEL_ANIMATION,0,0,9);
    };
    b.i(0,0);b.emit(GGV_I_TO_U,8,0);b.write(GGV_COMMAND_OUTPUT,8);
    b.read(0,GGV_EMPTY_ANIM_MODE);b.i(1,1);b.emit(GGV_I_EQ,3,0,1);auto notBusy=b.branch(3);
    b.call(GGV_ANIM_WAIT_FRAME,6);b.call(GGV_MODEL_ANIM_FRAME_COUNT,0);b.i(1,1);b.emit(GGV_I_SUB_WRAP,0,0,1);b.emit(GGV_I_TO_F,7,0);
    b.emit(GGV_F_GE,3,6,7);auto notFinished=b.branch(3);b.i(0,0);b.write(GGV_EMPTY_ANIM_MODE,0);b.end(notFinished);b.end(notBusy);
    // Both operands are read before deciding the native non-short-circuit OR.
    b.read(0,GGV_EMPTY_ANIM_MODE);b.i(1,1);b.emit(GGV_I_EQ,2,0,1);
    b.read(0,GGV_ANIM_LOAD);b.i(1,0);b.emit(GGV_I_LT,3,0,1);b.emit(GGV_NOT,3,3);
    auto checkLoad=b.branch(2);auto busy=b.jump();b.end(checkLoad);auto inactive=b.branch(3);b.end(busy);
    b.read(6,GGV_ANIM_GANGSTA);b.f(7,0);b.emit(GGV_F_GT,3,6,7);auto notRotated=b.branch(3);clearIncrement();b.end(notRotated);
    b.read(0,GGV_ANIM_LOAD);b.i(1,0);b.emit(GGV_I_LT,3,0,1);b.emit(GGV_NOT,3,3);auto noLoad=b.branch(3);
    b.f(5,1);b.read(10,GGV_ANIM_FRAME_COMMAND_VALUE);b.emit(GGV_I64_TO_F,4,10);b.f(7,10000);b.emit(GGV_F_DIV,4,4,7);
    b.read(3,GGV_ANIM_BIT07);auto noVariation=b.branch(3);b.read(0,GGV_ANIM_ELIGIBILITY_LEFT_INUSE);b.i(1,0);b.emit(GGV_I_EQ,3,0,1);b.emit(GGV_NOT,3,3);auto leftUnused=b.branch(3);
    b.call(GGV_RNG_U32,8);b.emit(GGV_U_TO_F,5,8);b.f(7,1.0f/UINT32_MAX);b.emit(GGV_F_MUL,5,5,7);
    b.f(7,0.77f);b.emit(GGV_F_MUL,5,5,7);b.f(7,0.7f);b.emit(GGV_F_ADD,5,5,7);b.end(leftUnused);b.end(noVariation);
    b.read(11,GGV_ANIM_FRAME_COMMAND);b.emit(GGV_REF_NONNULL,3,11);auto noCommand=b.branch(3);b.f(7,0);b.emit(GGV_F_LT,3,4,7);auto forward=b.branch(3);
    setAnimation();b.call(GGV_MODEL_ANIM_FRAME_COUNT,0);b.emit(GGV_I_TO_F,6,0);b.call(GGV_COMMAND_SET_FRAME,0,6);auto reverseDone=b.jump();
    b.end(noCommand);b.end(forward);setAnimation();b.end(reverseDone);
    b.i(0,-1);b.write(GGV_ANIM_LOAD,0);b.i(0,1);b.write(GGV_EMPTY_ANIM_MODE,0);
    b.read(13,GGV_COMMAND_FLOAT_FRAMES);auto legacyIncrement=b.branch(13);b.call(GGV_MODEL_ABS_ANIM_SPEED,15);b.read(12,GGV_ANIM_WAIT_INCREMENT);b.emit(GGV_I_TO_F,16,12);
    b.read(14,GGV_RELOAD_PAL);auto ntscIncrement=b.branch(14);b.f(7,1.2f);b.emit(GGV_F_MUL,16,16,7);b.end(ntscIncrement);
    b.emit(GGV_F_MUL,16,15,16);b.write(GGV_ANIM_WAIT_INCREMENT_REAL,16);b.end(legacyIncrement);b.end(noLoad);
    b.read(3,GGV_ANIM_BIT02);auto unlocked=b.branch(3);clearIncrement();b.end(unlocked);
    b.call(GGV_COMMAND_FIRST_PASS,3);auto failed=b.branch(3);b.read(13,GGV_COMMAND_FLOAT_FRAMES);auto legacyTick=b.branch(13);
    b.f(6,4);b.read(14,GGV_RELOAD_PAL);auto ntscPlaySpeed=b.branch(14);b.f(7,1.2f);b.emit(GGV_F_MUL,6,6,7);b.end(ntscPlaySpeed);
    b.call(GGV_COMMAND_PLAY_SPEED,0,6);b.read(12,GGV_ANIM_WAIT_INCREMENT);b.call(GGV_COMMAND_TICK_QUARTER,0,12);auto quarterDone=b.jump();
    b.end(legacyTick);b.read(12,GGV_ANIM_WAIT_INCREMENT);b.call(GGV_COMMAND_TICK_LEGACY,0,12);b.end(quarterDone);
    b.call(GGV_ANIM_WAIT_FRAME,6);b.read(13,GGV_COMMAND_FLOAT_FRAMES);auto integerFrame=b.branch(13);b.write(GGV_COMMAND_TO_FLOAT,6);auto frameReady=b.jump();
    b.end(integerFrame);b.emit(GGV_TRUNCATE,0,6);b.write(GGV_COMMAND_TO_INT,0);b.end(frameReady);b.call(GGV_COMMAND_SOUND_PASS);
    b.end(inactive);b.ret(0);b.end(failed);b.ret(-1);return b.s;
}
constexpr auto animationDriver=animationDriverSource();
static_assert(valid(animationDriver),"Animation driver retains RNG ordering and regional model/frame semantics");
constexpr auto animationFirstCommands=animationFirstCommandsSource();
static_assert(valid(animationFirstCommands),"Animation first pass preserves regional frame domains and initialized loop state");
constexpr auto animationPartCommand=animationPartCommandSource();
static_assert(valid(animationPartCommand),"Animation parts preserve full-width lookup and strict newer-frame replacement");
constexpr auto animationSoundCommands=animationSoundCommandsSource();
static_assert(valid(animationSoundCommands),"Animation sound traversal preserves typed frame domains and speed latch lifetime");
constexpr auto changeGun=changeGunSource();
static_assert(valid(changeGun),"Change-gun phases must preserve captured definition and same-call progression");
constexpr auto equipSound=equipSoundSource();
static_assert(valid(equipSound),"Equip sound policy must retain typed priority and nullable-handle ordering");
constexpr auto freeHeldRocket=freeHeldRocketSource();
static_assert(valid(freeHeldRocket),"Held rocket release must have typed ordered flow");
static_assert(valid(canFreeWeapon),"Can-free predicate and transition logging must have typed flow");
static_assert(valid(animationFrame),"Animation frame program must be total and typed");
static_assert(needsServices(animationFrame)&&!needsServices4(animationFrame),"Animation frame backend services");
static_assert(valid(reloadTick),"Complete reload driver must be total and typed");
static_assert(valid(changeFuncTick),"Complete function change driver must be total and typed");
static_assert(valid(armPitch),"Complete arm pitch must be total and typed");
static_assert(valid(isLoaded),"Complete loaded predicate must be total and typed");
static_assert(valid(isReloading),"Complete reloading predicate must be total and typed");
static_assert(needsServices(reloadTick)&&needsServices(changeFuncTick)&&needsServices(armPitch)
    &&!needsServices(isLoaded)&&!needsServices(isReloading),"Hand lifecycle service flags");
static_assert(!needsServices4(reloadTick)&&!needsServices4(changeFuncTick)&&!needsServices4(armPitch),"Hand lifecycle uses no four-input services");
constexpr ggv_source visibilityCommandsSource() {
    Builder b;b.boolean(12,true);b.read(0,GGV_VIS_CURSOR);b.emit(GGV_REF_NONNULL,1,0);
    auto absent=b.branch(1);const unsigned loop=b.s.count;
    b.read(2,GGV_VIS_COMMAND_TYPE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);
    auto ended=b.branch(4);b.read(0,GGV_VIS_CURSOR);b.call(GGV_VIS_TEST,1,0);auto failed=b.branch(1);
    // Separate fresh reads preserve the native three independent if statements.
    for(auto op:{std::pair<int,bool>{0,true},{1,false},{3,true}}) {
        b.read(5,GGV_VIS_OPERATION);b.i(3,op.first);b.emit(GGV_I_EQ,4,5,3);auto skip=b.branch(4);
        b.read(6,GGV_VIS_COMMAND_PART);b.boolean(7,op.second);b.call(GGV_COMMAND_SET_PART,0,6,7);b.end(skip);
    }
    auto after=b.jump();b.end(failed);b.read(5,GGV_VIS_OPERATION);b.i(3,3);b.emit(GGV_I_EQ,4,5,3);
    auto skip=b.branch(4);b.read(6,GGV_VIS_COMMAND_PART);b.boolean(7,false);b.call(GGV_COMMAND_SET_PART,0,6,7);b.end(skip);b.end(after);
    b.read(0,GGV_VIS_CURSOR);b.call(GGV_VIS_NEXT,0,0);b.write(GGV_VIS_CURSOR,0);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,12)].target=loop;b.end(ended);b.end(absent);b.ret(0);return b.s;
}
constexpr auto visibilityCommands=visibilityCommandsSource();
static_assert(valid(visibilityCommands),"Visibility traversal must be typed");
constexpr ggv_source partVisibilitySource() {
    Builder b;b.read(0,GGV_VIS_PART);b.i(1,0x35);b.emit(GGV_I_EQ,2,0,1);
    auto right=b.branch(2);auto hand=b.jump();b.end(right);b.i(1,0x36);b.emit(GGV_I_EQ,2,0,1);auto gun=b.branch(2);b.end(hand);
    b.read(3,GGV_VIS_HAND_MODEL);b.emit(GGV_REF_NONNULL,6,3);auto absent=b.branch(6);
    b.read(3,GGV_VIS_HAND_MODEL);b.call(GGV_VIS_MODEL_PART,4,3,0);b.emit(GGV_REF_NONNULL,6,4);auto missing=b.branch(6);
    b.read(5,GGV_VIS_VISIBLE);b.call(GGV_VIS_SAVE_HAND,0,4,5);b.end(absent);b.end(missing);b.ret(0);
    b.end(gun);b.read(3,GGV_VIS_MODELDEF);b.call(GGV_VIS_MODEL_PART,4,3,0);b.emit(GGV_REF_NONNULL,6,4);auto missingGun=b.branch(6);
    b.read(5,GGV_VIS_VISIBLE);b.call(GGV_VIS_SAVE_GUN,0,4,5);b.end(missingGun);b.ret(0);return b.s;
}
constexpr auto partVisibility=partVisibilitySource();
static_assert(valid(partVisibility),"Part storage selection must be typed");
constexpr ggv_source ammoVisibilitySource() {
    Builder b;b.read(1,GGV_VIS_WEAPON_ID);b.call(GGV_QUERY_FIND_WEAPON,0,1);b.write(GGV_VIS_WEAPON,0);
    b.read(2,GGV_VIS_COMMANDS);b.call(GGV_VIS_RUN_COMMANDS,0,2);b.i(3,0x42);b.boolean(4,false);b.call(GGV_COMMAND_SET_PART,0,3,4);
    b.i(5,0);b.boolean(12,true);b.i(13,2);b.i(14,100);const unsigned slot=b.s.count;
    b.write(GGV_VIS_AMMO_SLOT,5);b.read(9,GGV_VIS_AMMO);b.emit(GGV_REF_NONNULL,8,9);auto absent=b.branch(8);
    b.read(10,GGV_VIS_AMMO_FLAGS);b.s.nodes[b.emit(GGV_MASK,11,10)].literal.u=8;auto noParts=b.branch(11);
    b.i(6,0);const unsigned part=b.s.count;b.read(7,GGV_VIS_CLIP_SIZE);b.emit(GGV_I_LT,8,6,7);auto endParts=b.branch(8);
    b.read(7,GGV_VIS_LOADED);b.emit(GGV_I_LT,4,6,7);b.emit(GGV_I_ADD_WRAP,3,6,14);b.call(GGV_COMMAND_SET_PART,0,3,4);
    b.emit(GGV_I_INCREMENT,6,6);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,12)].target=part;
    b.end(endParts);b.end(noParts);b.end(absent);b.emit(GGV_I_INCREMENT,5,5);b.emit(GGV_I_LT,8,5,13);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,8)].target=slot;b.ret(0);return b.s;
}
constexpr auto ammoVisibility=ammoVisibilitySource();
static_assert(valid(ammoVisibility),"Ammo visibility loops must be typed");

constexpr ggv_source switchHandResetSource() {
    Builder b;b.i(0,0);b.boolean(1,false);b.f(2,0);
    b.write(GGV_RELOAD_EJECT_STATE,0);b.write(GGV_CHANGE_EJECT_TYPE,0);
    b.write(GGV_SWITCH_RESET_BIT,1);b.write(GGV_HAND_ACTIVATE_SECONDARY,1);
    b.write(GGV_MATMOT1,2);b.write(GGV_MATMOT2,2);b.write(GGV_SWITCH_MATMOT3,2);
    b.write(GGV_SWITCH_ANGLE_DAMPER,2);b.write(GGV_SWITCH_SMOKE_POINT,2);
    b.write(GGV_BURST_BULLETS,0);b.write(GGV_LOAD_SLIDE,2);b.write(GGV_ALLOW_SHOOT_FRAME,0);
    b.write(GGV_LAST_SHOOT_FRAME,0);b.write(GGV_WEAPON_FUNCTION,0);
    b.read(3,GGV_SWITCH_RESET_WEAPON);b.write(GGV_SWITCH_HAND_WEAPON,3);
    b.read(6,GGV_SWITCH_UPGRADES);b.read(4,GGV_SWITCH_HAND_INDEX);
    b.emit(GGV_I_ADD_WRAP,4,4,4);b.emit(GGV_I_ADD_WRAP,4,4,4);b.emit(GGV_I_TO_U,5,4);
    b.emit(GGV_U_SHR,6,6,5);b.uop(GGV_U_AND_LITERAL,6,6,15);b.write(GGV_SWITCH_HAND_UPGRADES,6);
    b.write(GGV_ANIM_GANGSTA,2);b.read(4,GGV_SWITCH_HAND_INDEX);b.call(GGV_SWITCH_CONFIGURE_HAND,0,4);
    b.call(GGV_SWITCH_INIT_ANIM);
    b.read(7,GGV_SWITCH_AUDIO_HANDLE);b.emit(GGV_REF_NONNULL,9,7);auto absent=b.branch(9);
    b.read(7,GGV_SWITCH_AUDIO_HANDLE);b.call(GGV_SOUND_STATE,8,7);b.emit(GGV_I_EQ,9,8,0);b.emit(GGV_NOT,9,9);auto stopped=b.branch(9);
    b.read(7,GGV_SWITCH_AUDIO_HANDLE);b.call(GGV_SOUND_STOP,0,7);b.end(absent);b.end(stopped);b.ret(0);return b.s;
}
constexpr auto switchHandReset=switchHandResetSource();
static_assert(valid(switchHandReset),"Switch hand reset must be typed");

constexpr ggv_source switchDriverSource() {
    Builder b;
    auto integer=[&](ggv_binding field,int value){b.i(4,value);b.write(field,4);};
    auto boolean=[&](ggv_binding field,bool value){b.boolean(5,value);b.write(field,5);};
    auto match=[&](ggv_binding field,ggv_binding constant){b.read(1,field);b.read(2,constant);b.emit(GGV_I_EQ,3,1,2);};
    auto zero=[&](ggv_binding field,bool equal){b.read(1,field);b.i(2,0);b.emit(GGV_I_EQ,3,1,2);if(!equal)b.emit(GGV_NOT,3,3);};
    auto oldMatch=[&](ggv_binding constant){b.read(2,constant);b.emit(GGV_I_EQ,3,10,2);};
    auto normalized=[&](ggv_binding field){b.i(4,0);auto no=b.branch(5);b.i(4,1);b.end(no);b.write(field,4);};
    // Diagnostics call both hands, independently of their boolean results.
    zero(GGV_SW_PLAYER_NUM,true);auto noDiagPlayer=b.branch(3);
    b.read(1,GGV_SW_PENDING);b.i(2,0);b.emit(GGV_I_LT,3,1,2);b.emit(GGV_NOT,3,3);auto noDiagPending=b.branch(3);
    b.read(1,GGV_SW_FRAME60);b.i(2,60);b.emit(GGV_I_LT,3,1,2);auto modulo=b.branch(3);auto diagnostic=b.jump();b.end(modulo);
    b.read(1,GGV_SW_FRAME);b.emit(GGV_I_TO_I64,20,1);b.i(2,120);b.emit(GGV_I_TO_I64,21,2);
    b.emit(GGV_I64_REM,22,20,21);b.emit(GGV_I64_TO_I32,1,22);b.i(2,9);b.emit(GGV_I_EQ,3,1,2);auto noDiagFrame=b.branch(3);b.end(diagnostic);
    b.i(0,0);b.call(GGV_SW_CAN_FREE,5,0);normalized(GGV_SW_DIAG_RIGHT);
    b.i(0,1);b.call(GGV_SW_CAN_FREE,5,0);normalized(GGV_SW_DIAG_LEFT);b.call(GGV_SW_LOG);
    b.end(noDiagPlayer);b.end(noDiagPending);b.end(noDiagFrame);
    b.read(1,GGV_SW_PENDING);b.i(2,0);b.emit(GGV_I_LT,3,1,2);b.emit(GGV_NOT,3,3);auto noPending=b.branch(3);
    b.i(0,0);b.call(GGV_SW_CAN_FREE,5,0);auto rightBusy=b.branch(5);
    b.i(0,1);b.call(GGV_SW_CAN_FREE,5,0);auto leftBusy=b.branch(5);
    b.read(10,GGV_SW_OLD_WEAPON);b.read(11,GGV_SW_LEFT_INUSE);
    b.read(0,GGV_SW_PENDING);b.call(GGV_SW_DEVICE_STATE,1,0);b.i(2,-1);b.emit(GGV_I_EQ,3,1,2);b.emit(GGV_NOT,3,3);auto available=b.branch(3);
    b.read(1,GGV_SW_UNARMED);b.write(GGV_SW_PENDING,1);b.end(available);
    zero(GGV_SW_DUAL,false);auto single=b.branch(3);b.read(0,GGV_SW_PENDING);b.read(1,GGV_SW_PENDING);
    b.call(GGV_SW_HAS_DOUBLE,5,0,1);b.emit(GGV_NOT,5,5);auto hasDouble=b.branch(5);integer(GGV_SW_DUAL,0);b.end(single);b.end(hasDouble);
    b.call(GGV_SW_CLEANUP);b.i(0,1);b.call(GGV_SW_FREE,0,0);b.i(0,0);b.call(GGV_SW_FREE,0,0);
    oldMatch(GGV_SW_SCANNER);auto noAim=b.branch(3);boolean(GGV_SW_CURRENT_AIM,false);b.end(noAim);
    oldMatch(GGV_SW_RCP);auto noReserve=b.branch(3);b.read(6,GGV_SW_MATMOT);b.emit(GGV_TRUNCATE,7,6);
    b.read(8,GGV_SW_RESERVE);b.emit(GGV_I_GT,3,7,8);auto noClamp=b.branch(3);b.read(7,GGV_SW_RESERVE);b.end(noClamp);
    b.read(8,GGV_SW_RESERVE_TARGET);b.emit(GGV_I_SUB_WRAP,8,8,7);b.write(GGV_SW_RESERVE_TARGET,8);b.end(noReserve);
    oldMatch(GGV_SW_SCANNER);auto noZoom=b.branch(3);b.f(12,0);b.write(GGV_SW_ZOOM_MAX,12);b.read(12,GGV_SW_ZOOM_MAX);b.write(GGV_SW_ZOOM_TIME,12);
    b.f(12,60);b.write(GGV_SW_FOV_NEW,12);b.read(12,GGV_SW_FOV_NEW);b.write(GGV_SW_FOV,12);b.end(noZoom);
    match(GGV_SW_PENDING,GGV_SW_NONE);auto equip=b.branch(3);integer(GGV_SW_LEFT_INUSE,0);integer(GGV_SW_RIGHT_INUSE,0);
    b.read(1,GGV_SW_NONE);b.write(GGV_SW_WEAPON,1);auto equipped=b.jump();b.end(equip);
    b.read(0,GGV_SW_PENDING);b.call(GGV_SW_SET_MEMORY,0,0);b.read(1,GGV_SW_PENDING);b.write(GGV_SW_WEAPON,1);
    integer(GGV_SW_LEFT_INUSE,1);integer(GGV_SW_RIGHT_INUSE,1);b.end(equipped);
    match(GGV_SW_WEAPON,GGV_SW_REMOTE);auto notRemote=b.branch(3);integer(GGV_SW_DUAL,1);b.end(notRemote);
    zero(GGV_SW_DUAL,true);auto dual=b.branch(3);integer(GGV_SW_LEFT_INUSE,0);b.end(dual);
    b.read(2,GGV_SW_PSYCHOSIS);b.emit(GGV_I_LE,3,10,2);auto above=b.branch(3);
    b.read(2,GGV_SW_UNARMED);b.emit(GGV_I_LT,3,10,2);b.emit(GGV_NOT,3,3);auto below=b.branch(3);b.write(GGV_SW_PREV_WEAPON,10);b.end(above);b.end(below);
    b.i(2,0);b.emit(GGV_I_EQ,5,11,2);b.emit(GGV_NOT,5,5);normalized(GGV_SW_PREV_DUAL);
    integer(GGV_SW_CURRENT_INVERT,0);integer(GGV_SW_USE_TIME,-1);
    b.i(0,0);b.call(GGV_SW_RESET_HAND,0,0);b.i(0,1);b.call(GGV_SW_RESET_HAND,0,0);
    b.call(GGV_SW_INVENTORY_INDEX);integer(GGV_SW_PENDING,-1);integer(GGV_SW_FADER,0);
    match(GGV_SW_WEAPON,GGV_SW_DISGUISE_A);auto secondDisguise=b.branch(3);auto disguise=b.jump();b.end(secondDisguise);
    match(GGV_SW_WEAPON,GGV_SW_DISGUISE_B);auto noDisguise=b.branch(3);b.end(disguise);
    b.read(14,GGV_SW_PLAYER_CHR);b.write(GGV_SW_CHR,14);b.call(GGV_SW_DISGUISE_SOUND);boolean(GGV_SW_CURRENT_DISGUISED,true);
    b.read(13,GGV_SW_CHR_HIDDEN);b.uop(GGV_U_OR_LITERAL,13,13,0x00080000);b.write(GGV_SW_CHR_HIDDEN,13);
    match(GGV_SW_STAGE,GGV_SW_RESCUE);auto notRescue=b.branch(3);b.read(13,GGV_SW_CHR_HIDDEN);
    b.uop(GGV_U_OR_LITERAL,13,13,0x00001000);b.write(GGV_SW_CHR_HIDDEN,13);b.end(notRescue);
    b.read(0,GGV_SW_WEAPON);b.call(GGV_SW_REMOVE_ITEM,0,0);b.call(GGV_SW_CYCLE_BACK);b.end(noDisguise);
    integer(GGV_SW_CURRENT_STRING,0);integer(GGV_SW_STRING_TIMER,0);integer(GGV_SW_THROWING,0);b.ret(0);
    b.end(rightBusy);b.end(leftBusy);b.ret(0);
    // Preserve the short-circuit expression, including its repeated field reads.
    b.end(noPending);zero(GGV_SW_LEFT_INUSE,false);auto leftOff=b.branch(3);
    zero(GGV_SW_PLAYER_DUAL,true);auto firstFalse=b.branch(3);auto mismatch=b.jump();b.end(leftOff);b.end(firstFalse);
    zero(GGV_SW_LEFT_INUSE,true);auto leftOn=b.branch(3);zero(GGV_SW_PLAYER_DUAL,false);auto desiredOff=b.branch(3);b.end(mismatch);
    b.i(0,1);b.call(GGV_SW_CAN_FREE,5,0);auto busy=b.branch(5);b.i(0,1);b.call(GGV_SW_FREE,0,0);
    b.read(1,GGV_SW_PLAYER_DUAL);b.write(GGV_SW_LEFT_INUSE,1);b.end(leftOn);b.end(desiredOff);b.end(busy);b.ret(0);return b.s;
}
constexpr auto switchDriver=switchDriverSource();
static_assert(valid(switchDriver),"Switch parent must be typed");

constexpr ggv_source handConfigureSource() {
    Builder b;b.read(0,GGV_VIS_WEAPON_ID);b.call(GGV_QUERY_FIND_WEAPON,1,0);b.write(GGV_VIS_WEAPON,1);
    for(int slot:{0,1}) {
        b.i(2,slot);b.write(GGV_VIS_AMMO_SLOT,2);
        b.read(3,GGV_SWITCH_HAND_INDEX);b.i(4,0);b.emit(GGV_I_EQ,5,3,4);auto left=b.branch(5);
        b.i(4,-1);b.write(GGV_CONFIG_CTRL_AMMO,4);b.end(left);
        b.read(1,GGV_VIS_WEAPON);b.emit(GGV_REF_NONNULL,5,1);auto noWeapon=b.branch(5);
        b.read(6,GGV_VIS_AMMO);b.emit(GGV_REF_NONNULL,5,6);auto noAmmo=b.branch(5);
        b.read(3,GGV_SWITCH_HAND_INDEX);b.i(4,0);b.emit(GGV_I_EQ,5,3,4);auto notRight=b.branch(5);
        b.read(8,GGV_CONFIG_AMMO_TYPE);b.emit(GGV_U_TO_I,4,8);b.write(GGV_CONFIG_CTRL_AMMO,4);b.end(notRight);
        b.read(4,GGV_CONFIG_AMMO_CLIP);b.write(GGV_VIS_CLIP_SIZE,4);
        b.read(3,GGV_SWITCH_HAND_INDEX);b.i(4,1);b.emit(GGV_I_EQ,5,3,4);auto notLeft=b.branch(5);
        b.read(3,GGV_VIS_WEAPON_ID);b.read(4,GGV_SW_REMOTE);b.emit(GGV_I_EQ,5,3,4);auto notRemote=b.branch(5);
        b.i(4,0);b.write(GGV_VIS_CLIP_SIZE,4);b.end(notRemote);b.end(notLeft);
        b.i(4,0);b.write(GGV_VIS_LOADED,4);b.end(noAmmo);b.end(noWeapon);
    }
    b.f(7,1);b.write(GGV_CONFIG_UPGRADE0,7);b.write(GGV_CONFIG_UPGRADE1,7);b.write(GGV_CONFIG_FINAL0,7);b.write(GGV_CONFIG_FINAL1,7);
    b.read(4,GGV_CONFIG_FIRST_AMMO);b.i(3,0);b.emit(GGV_I_LT,5,4,3);b.emit(GGV_NOT,5,5);auto unavailable=b.branch(5);
    b.read(1,GGV_CONFIG_HAND_ABMAG);b.call(GGV_CONFIG_RESET_ABMAG,0,1);
    b.read(3,GGV_SWITCH_HAND_INDEX);b.i(4,0);b.emit(GGV_I_EQ,5,3,4);auto secondary=b.branch(5);
    b.read(1,GGV_CONFIG_CTRL_ABMAG);b.call(GGV_CONFIG_RESET_ABMAG,0,1);b.end(secondary);
    b.i(4,0);b.write(GGV_CONFIG_LASTMAG,4);b.end(unavailable);b.ret(0);return b.s;
}
constexpr auto handConfigure=handConfigureSource();
static_assert(valid(handConfigure),"Hand configuration must be typed");
constexpr ggv_source abmagResetSource(){Builder b;b.i(0,0);b.write(GGV_ABMAG_LOADED,0);b.write(GGV_ABMAG_CHANGE,0);
    b.write(GGV_ABMAG_REF,0);b.write(GGV_ABMAG_TIMER,0);b.ret(0);return b.s;}
constexpr auto abmagReset=abmagResetSource();
static_assert(valid(abmagReset),"Ammo display reset must be typed");

constexpr ggv_source freeWeaponSource() {
    Builder b;b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noEnter=b.branch();b.call(GGV_CLEAN_LOG_ENTER);b.end(noEnter);
    b.read(2,GGV_CLEAN_INUSE);b.i(3,0);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto unused=b.branch(4);
    for(int slot:{0,1}) {
        b.i(1,slot);b.write(GGV_VIS_AMMO_SLOT,1);b.read(2,GGV_CLEAN_AMMO_TYPE);b.i(3,0);b.emit(GGV_I_LT,4,2,3);b.emit(GGV_NOT,4,4);auto noAmmo=b.branch(4);
        b.read(5,GGV_VIS_CLIP_SIZE);b.read(6,GGV_VIS_LOADED);b.emit(GGV_I_SUB_WRAP,7,5,6);
        b.read(0,GGV_CLEAN_WEAPON);b.call(GGV_RELOAD_INDEX_FOR_WEAPON,8,0);b.i(3,-1);b.emit(GGV_I_EQ,4,8,3);b.emit(GGV_NOT,4,4);auto noIndex=b.branch(4);
        b.write(GGV_CLEAN_RELOAD_INDEX,8);b.read(2,GGV_CLEAN_REGION);b.i(3,1);b.emit(GGV_I_EQ,4,2,3);auto nonPal=b.branch(4);
        // Exact integer multiplication by213; no float conversion or rounding.
        b.i(9,0);for(int bit=7;bit>=0;--bit){b.emit(GGV_I_ADD_WRAP,9,9,9);if((213>>bit)&1)b.emit(GGV_I_ADD_WRAP,9,9,7);}
        b.i(3,212);b.emit(GGV_I_ADD_WRAP,9,9,3);b.emit(GGV_I_TO_U,10,9);auto counterReady=b.jump();b.end(nonPal);
        b.emit(GGV_I_TO_U,10,7);b.i(3,8);b.emit(GGV_I_TO_U,11,3);b.emit(GGV_U_SHL,10,10,11);
        b.i(3,2);b.emit(GGV_I_EQ,4,2,3);auto old=b.branch(4);b.emit(GGV_U_TO_I,9,10);b.i(3,255);
        b.emit(GGV_I_ADD_WRAP,9,9,3);b.emit(GGV_I_TO_U,10,9);auto shiftedReady=b.jump();b.end(old);b.uop(GGV_U_OR_LITERAL,10,10,255);
        b.end(shiftedReady);b.end(counterReady);b.write(GGV_CLEAN_ROUNDS,10);b.end(noIndex);
        b.read(6,GGV_VIS_LOADED);b.i(3,0);b.emit(GGV_I_GT,4,6,3);auto empty=b.branch(4);
        b.read(5,GGV_CLEAN_RESERVE);b.read(6,GGV_VIS_LOADED);b.emit(GGV_I_ADD_WRAP,5,5,6);b.write(GGV_CLEAN_RESERVE,5);b.end(empty);
        b.i(3,0);b.write(GGV_VIS_LOADED,3);b.end(noAmmo);
    }
    b.end(unused);b.read(10,GGV_CLEAN_MULTIPLAYER);b.s.nodes[b.emit(GGV_MASK,4,10)].literal.u=0xffffffffu;auto single=b.branch(4);
    b.read(0,GGV_SWITCH_HAND_INDEX);b.call(GGV_CLEAN_DELETE,0,0);b.end(single);
    b.read(0,GGV_SWITCH_HAND_INDEX);b.call(GGV_CLEAN_ROCKET,0,0);b.read(0,GGV_SWITCH_HAND_INDEX);b.call(GGV_CLEAN_RETIRE,0,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noExit=b.branch();b.call(GGV_CLEAN_LOG_EXIT);b.end(noExit);b.ret(0);return b.s;
}
constexpr auto freeWeapon=freeWeaponSource();
static_assert(valid(freeWeapon),"Weapon cleanup must be typed");

constexpr ggv_source equipWeaponSource() {
    Builder b;
    auto same=[&](){b.read(1,GGV_SW_WEAPON);b.read(2,GGV_EQUIP_REQUEST);b.emit(GGV_I_EQ,5,1,2);auto different=b.branch(5);
        b.read(1,GGV_SW_PENDING);b.i(2,-1);b.emit(GGV_I_EQ,5,1,2);b.end(different);};
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();same();b.i(6,0);auto notSame=b.branch(5);b.i(6,1);b.end(notSame);
    b.write(GGV_EQUIP_DIAG_SHORTCUT,6);b.call(GGV_EQUIP_LOG);b.end(noLog);
    same();auto change=b.branch(5);b.ret(0);b.end(change);
    b.read(0,GGV_EQUIP_REQUEST);b.write(GGV_SW_PENDING,0);b.boolean(8,false);b.write(GGV_EQUIP_WANT_AMMO,8);b.ret(0);return b.s;
}
constexpr auto equipWeapon=equipWeaponSource();static_assert(valid(equipWeapon),"Equip request must be typed");
constexpr ggv_source equipWeaponHandSource() {
    Builder b;b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noEnter=b.branch();b.call(GGV_EQUIP2_LOG_ENTER);b.end(noEnter);
    b.compare(GGV_SWITCH_HAND_INDEX,GGV_I_EQ,1);auto right=b.branch();b.read(0,GGV_EQUIP_REQUEST);b.read(1,GGV_SW_NONE);
    b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);b.write(GGV_EQUIP_CURRENT_DUAL,5);auto after=b.jump();b.end(right);
    b.read(0,GGV_EQUIP_REQUEST);b.read(1,GGV_EQUIP_CUSTOM_END);b.emit(GGV_I_LT,5,0,1);b.emit(GGV_NOT,5,5);auto below=b.branch(5);
    b.read(0,GGV_SW_UNARMED);b.write(GGV_EQUIP_REQUEST,0);b.end(below);b.read(0,GGV_EQUIP_REQUEST);b.call(GGV_EQUIP_CHILD,0,0);b.end(after);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noExit=b.branch();b.call(GGV_EQUIP2_LOG_EXIT);b.end(noExit);b.ret(0);return b.s;
}
constexpr auto equipWeaponHand=equipWeaponHandSource();static_assert(valid(equipWeaponHand),"Hand equip request must be typed");
constexpr ggv_source initHandAnimsSource() {
    Builder b;b.read(0,GGV_INIT_PLAYER_REF);b.call(GGV_INIT_PLAYER_BEGIN,0,0);
    for(int hand:{1,0}) {
        b.i(1,hand);b.call(GGV_INIT_HAND_REF,0,1);b.write(GGV_INIT_HAND,0);
        b.f(2,0);b.write(GGV_ANIM_GANGSTA,2);b.i(3,0);b.write(GGV_STATE_VALUE,3);
        b.i(3,-1);b.write(GGV_ANIM_LOAD,3);b.i(3,0);b.write(GGV_EMPTY_ANIM_MODE,3);
        b.read(4,GGV_INIT_ANIM_REF);b.call(GGV_INIT_ANIM,0,4);
        b.read(4,GGV_INIT_ANIM_REF);b.write(GGV_INIT_GUN_ANIM,4);b.read(4,GGV_INIT_ANIM_REF);b.write(GGV_INIT_HAND_ANIM,4);
    }
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();b.call(GGV_INIT_LOG);b.end(noLog);b.ret(0);return b.s;
}
constexpr auto initHandAnims=initHandAnimsSource();static_assert(valid(initHandAnims),"Hand animation initialization must be typed");

constexpr ggv_source freeGunMemorySource() {
    Builder b;b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noPlayerLog=b.branch();
    b.read(0,GGV_MEM_CURRENT_OWNER);b.read(1,GGV_MEM_OWNER_FREE);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto alreadyFree=b.branch(5);
    b.call(GGV_MEM_FREE_LOG);b.end(alreadyFree);b.end(noPlayerLog);
    b.read(0,GGV_MEM_OWNER_FREE);b.write(GGV_MEM_CURRENT_OWNER,0);b.call(GGV_MEM_TEXTURE_RESET);b.ret(0);return b.s;
}
constexpr auto freeGunMemory=freeGunMemorySource();static_assert(valid(freeGunMemory),"Free gun memory must be typed");
constexpr ggv_source setGunMemoryWeaponSource() {
    Builder b;b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noEnter=b.branch();b.call(GGV_MEM_SET_LOG_ENTER);b.end(noEnter);
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.read(1,GGV_MEM_OWNER_BOND);b.emit(GGV_I_EQ,5,0,1);auto otherOwner=b.branch(5);
    b.read(0,GGV_MEM_MASTER_FLUX);b.write(GGV_LOADED_MASTERLOADSTATE,0);b.read(0,GGV_MEM_GUN_FLUX);b.write(GGV_MEM_GUNLOADSTATE,0);
    b.read(0,GGV_EQUIP_REQUEST);b.write(GGV_LOADED_GUNMEMNEW,0);b.i(0,-1);b.write(GGV_MEM_LOCK_TIMER,0);auto after=b.jump();b.end(otherOwner);
    b.read(0,GGV_EQUIP_REQUEST);b.write(GGV_LOADED_GUNMEMNEW,0);b.end(after);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noExit=b.branch();b.call(GGV_MEM_SET_LOG_EXIT);b.end(noExit);b.ret(0);return b.s;
}
constexpr auto setGunMemoryWeapon=setGunMemoryWeaponSource();static_assert(valid(setGunMemoryWeapon),"Set gun memory weapon must be typed");
constexpr ggv_source tickLoadSource() {
    Builder b;b.i(0,0);b.i(1,8);b.read(2,GGV_RELOAD_LVUPDATE240);b.emit(GGV_I_LT,5,0,2);auto empty=b.branch(5);
    auto loop=b.s.count;b.call(GGV_MEM_MASTER_TICK);b.emit(GGV_I_ADD_WRAP,0,0,1);
    b.read(2,GGV_RELOAD_LVUPDATE240);b.emit(GGV_I_LT,5,0,2);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,5)].target=loop;
    b.end(empty);b.ret(0);return b.s;
}
constexpr auto tickLoad=tickLoadSource();static_assert(valid(tickLoad),"Load ticks must be typed");
constexpr ggv_source changeMemoryOwnerSource() {
    Builder b;
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noEnterPlayer=b.branch();
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.read(1,GGV_MEM_REQUEST_OWNER);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto sameForLog=b.branch(5);
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.emit(GGV_I_TO_U,2,0);b.i(3,8);b.emit(GGV_I_TO_U,21,3);b.emit(GGV_U_SHL,2,2,21);
    b.read(1,GGV_MEM_REQUEST_OWNER);b.emit(GGV_I_TO_U,22,1);b.emit(GGV_U_OR,2,2,22);b.emit(GGV_U_TO_I,0,2);
    b.read(1,GGV_MEM_LAST_PAIR);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto repeatedPair=b.branch(5);
    b.write(GGV_MEM_LAST_PAIR,0);b.call(GGV_MEM_OWNER_LOG_ENTER);b.end(repeatedPair);b.end(sameForLog);b.end(noEnterPlayer);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noWildPlayer=b.branch();
    b.read(0,GGV_LOADED_GUNMEMNEW);b.write(GGV_MEM_WILD_VALUE,0);b.i(1,-1);b.emit(GGV_I_LT,5,0,1);auto upper=b.branch(5);auto wild=b.jump();
    b.end(upper);b.i(1,100);b.emit(GGV_I_GT,5,0,1);auto validRange=b.branch(5);b.end(wild);
    b.read(1,GGV_MEM_LAST_WILD);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto repeatedWild=b.branch(5);
    b.write(GGV_MEM_LAST_WILD,0);b.call(GGV_MEM_OWNER_LOG_WILD);b.end(repeatedWild);b.end(validRange);b.end(noWildPlayer);
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.read(1,GGV_MEM_REQUEST_OWNER);b.emit(GGV_I_EQ,5,0,1);auto change=b.branch(5);b.ret(1);b.end(change);
    b.compare(GGV_MEM_LOCK_TIMER,GGV_I_LT,0);auto nonnegative=b.branch();
    b.read(0,GGV_MEM_LOCK_TIMER);b.i(1,1);b.emit(GGV_I_SUB_WRAP,0,0,1);b.write(GGV_MEM_LOCK_TIMER,0);
    b.compare(GGV_MEM_LOCK_TIMER,GGV_I_LT,-2);auto pending=b.branch();b.i(0,0);b.write(GGV_MEM_LOCK_TIMER,0);
    b.read(0,GGV_MEM_REQUEST_OWNER);b.write(GGV_LOADED_GUNMEMOWNER,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noSuccessPlayer=b.branch();
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.read(1,GGV_MEM_LAST_OWNER);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto repeatedOwner=b.branch(5);
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.write(GGV_MEM_LAST_OWNER,0);b.call(GGV_MEM_OWNER_LOG_SUCCESS);b.end(repeatedOwner);b.end(noSuccessPlayer);
    b.ret(1);b.end(pending);b.ret(0);b.end(nonnegative);
    b.boolean(10,false);b.read(20,GGV_LOADED_GUNMEMOWNER);b.i(1,0);b.emit(GGV_I_EQ,5,20,1);auto notBond=b.branch(5);
    b.compare(GGV_LOADED_GUNMEMTYPE,GGV_I_EQ,-1);b.emit(GGV_NOT,5,4);auto noType=b.branch(5);
    b.read(0,GGV_LOADED_GUNMEMTYPE);b.write(GGV_LOADED_GUNMEMNEW,0);b.end(noType);
    b.i(0,-1);b.write(GGV_LOADED_GUNMEMTYPE,0);b.call(GGV_MEM_ENTER_FLUX);b.boolean(10,true);b.write(GGV_MEM_LOAD_ALL,10);auto bondDone=b.jump();
    b.end(notBond);b.i(1,2);b.emit(GGV_I_EQ,5,20,1);auto notBody=b.branch(5);
    b.read(23,GGV_CLEAN_MULTIPLAYER);b.i(1,0);b.emit(GGV_I_TO_U,24,1);b.emit(GGV_U_GT,5,23,24);auto noMp=b.branch(5);b.boolean(10,true);b.end(noMp);
    b.compare(GGV_MEM_HAS_BODY,GGV_I_EQ,0);auto hasBody=b.branch();b.boolean(10,true);b.end(hasBody);
    b.compare(GGV_MEM_REQUEST_OWNER,GGV_I_EQ,1);auto notMenu=b.branch();b.compare(GGV_MEM_REMOVE_BODY_FLAG,GGV_I_EQ,0);b.emit(GGV_NOT,5,4);auto noRemove=b.branch(5);
    b.boolean(10,true);b.call(GGV_MEM_REMOVE_BODY);b.end(noRemove);b.end(notMenu);auto bodyDone=b.jump();b.end(notBody);
    for(int owner:{3,10,11}){b.i(1,owner);b.emit(GGV_I_EQ,5,20,1);auto other=b.branch(5);b.boolean(10,true);b.end(other);}
    b.end(bodyDone);b.end(bondDone);auto locked=b.branch(10);b.i(0,-1);b.write(GGV_MEM_LOCK_TIMER,0);b.i(0,10);b.write(GGV_LOADED_GUNMEMOWNER,0);b.end(locked);
    b.ret(0);return b.s;
}
constexpr auto changeMemoryOwner=changeMemoryOwnerSource();static_assert(valid(changeMemoryOwner),"Memory ownership transition must be typed");
constexpr ggv_source loadAllSource() {
    Builder b;b.read(6,GGV_RELOAD_PAL);auto oldVersion=b.branch(6);
    b.read(8,GGV_RELOAD_DEVICES_ACTIVE);b.read(9,GGV_RELOAD_DEVICES_INHIBIT);b.emit(GGV_U_NOT,9,9);b.emit(GGV_U_AND,8,8,9);b.uop(GGV_U_AND_LITERAL,8,8,4);
    b.i(0,0);b.emit(GGV_I_TO_U,10,0);b.emit(GGV_U_GT,5,8,10);auto noEyespy=b.branch(5);
    b.boolean(6,false);b.write(GGV_MEM_LOAD_ALL,6);b.ret(0);b.end(noEyespy);b.end(oldVersion);
    b.call(GGV_MEM_ENTER_FLUX);
    b.read(0,GGV_SW_WEAPON);b.read(1,GGV_SW_NONE);b.emit(GGV_I_EQ,5,0,1);auto weapon=b.branch(5);b.ret(0);b.end(weapon);
    b.read(0,GGV_SW_WEAPON);b.write(GGV_LOADED_GUNMEMNEW,0);
    b.compare(GGV_LOADED_GUNMEMTYPE,GGV_I_EQ,-1);b.emit(GGV_NOT,5,4);auto missingType=b.branch(5);b.ret(0);b.end(missingType);
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.read(1,GGV_MEM_OWNER_BOND);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto owned=b.branch(5);
    b.read(0,GGV_MEM_OWNER_BOND);b.call(GGV_MEM_CHANGE_OWNER,5,0);
    b.read(0,GGV_LOADED_GUNMEMOWNER);b.read(1,GGV_MEM_OWNER_BOND);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto acquired=b.branch(5);
    b.boolean(6,true);b.write(GGV_LOAD_LOCKSCREEN,6);b.ret(1);b.end(acquired);b.end(owned);
    b.call(GGV_MEM_ENTER_FLUX);auto loop=b.s.count;b.call(GGV_MEM_MASTER_TICK);b.call(GGV_RELOAD_IS_LOADED,5);b.emit(GGV_NOT,5,5);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,5)].target=loop;
    b.boolean(6,false);b.write(GGV_MEM_LOAD_ALL,6);b.ret(0);return b.s;
}
constexpr auto loadAll=loadAllSource();static_assert(valid(loadAll),"Load-all parent must be typed");
constexpr ggv_source enterFluxSource() {
    Builder b;b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();b.call(GGV_FLUX_LOG);b.end(noLog);
    b.i(0,65535);b.write(GGV_FLUX_FILE,0);b.i(0,0);b.write(GGV_FLUX_CATALOG_HEAD,0);b.call(GGV_FLUX_CLEAR_HANDLE);
    b.emit(GGV_I_TO_U,8,0);b.write(GGV_FLUX_GENERATION,8);b.nullref(2);b.write(GGV_FLUX_HAND_MODEL,2);b.write(GGV_FLUX_LOAD_PTR,2);
    b.write(GGV_FLUX_LOAD_REMAINING,0);b.read(0,GGV_MEM_MASTER_FLUX);b.write(GGV_LOADED_MASTERLOADSTATE,0);b.read(0,GGV_MEM_GUN_FLUX);b.write(GGV_MEM_GUNLOADSTATE,0);
    b.i(11,0);b.read(12,GGV_FLUX_CASING_COUNT);b.emit(GGV_I_LT,5,11,12);auto empty=b.branch(5);auto loop=b.s.count;
    b.write(GGV_FLUX_CASING_INDEX,11);b.write(GGV_FLUX_CASING_MODEL,2);b.emit(GGV_I_INCREMENT,11,11);b.emit(GGV_I_LT,5,11,12);
    b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,5)].target=loop;b.end(empty);b.boolean(6,false);b.write(GGV_FLUX_CASINGS_ACTIVE,6);b.ret(0);return b.s;
}
constexpr auto enterFlux=enterFluxSource();static_assert(valid(enterFlux),"Flux reset must be typed");
/* Component of the master loader. A true result admits the retained body;
 * false includes the native lock-timer update and skips this load tick. */
constexpr ggv_source masterLoadAdmissionSource() {
    Builder b;b.read(0,GGV_LOADED_GUNMEMOWNER);b.i(1,0);b.emit(GGV_I_EQ,5,0,1);auto request=b.branch(5);auto owned=b.jump();
    b.end(request);b.call(GGV_MEM_CHANGE_OWNER,5,1);auto refused=b.branch(5);b.end(owned);
    b.read(0,GGV_LOADED_GUNMEMNEW);b.i(1,0);b.emit(GGV_I_LT,5,0,1);b.emit(GGV_NOT,5,5);auto noPending=b.branch(5);
    b.compare(GGV_MEM_LOCK_TIMER,GGV_I_EQ,0);auto ticking=b.branch();b.ret(1);b.end(ticking);
    b.read(0,GGV_MEM_LOCK_TIMER);b.i(1,1);b.emit(GGV_I_SUB_WRAP,0,0,1);b.write(GGV_MEM_LOCK_TIMER,0);
    b.compare(GGV_MEM_LOCK_TIMER,GGV_I_LT,-2);auto notExpired=b.branch();b.i(0,0);b.write(GGV_MEM_LOCK_TIMER,0);b.end(notExpired);
    b.end(noPending);b.end(refused);b.ret(0);return b.s;
}
constexpr auto masterLoadAdmission=masterLoadAdmissionSource();static_assert(valid(masterLoadAdmission),"Master-load admission must be typed");
constexpr ggv_source masterSourcePrepareSource() {
    Builder b;b.read(0,GGV_LOADED_GUNMEMNEW);b.write(GGV_MASTER_WEAPON,0);b.i(0,0);b.write(GGV_MASTER_HF32,0);
    b.call(GGV_MASTER_CHOOSE_BODY,5);auto noBody=b.branch(5);b.call(GGV_MASTER_HAND_FILE,5);auto noFile=b.branch(5);
    b.call(GGV_MASTER_HAND_SOURCE,5);auto noSource=b.branch(5);b.read(0,GGV_MASTER_HF32);b.write(GGV_MASTER_HANDFILE,0);
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_MASTER_WEAPON_FILE,1,0);b.write(GGV_MASTER_FILE,1);
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_INFO_IS_V2,5,0);auto notV2=b.branch(5);
    b.compare(GGV_LOADED_MASTERLOADSTATE,GGV_I_EQ,4);auto notLoaded=b.branch();b.read(8,GGV_MASTER_MODEL);
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_MASTER_RUNTIME_MODEL,9,0);b.emit(GGV_REF_EQ,5,8,9);b.emit(GGV_NOT,5,5);auto sameModel=b.branch(5);
    b.call(GGV_MEM_ENTER_FLUX);b.end(sameModel);b.end(notLoaded);b.end(notV2);
    b.read(0,GGV_MASTER_WEAPON);b.i(1,1);b.emit(GGV_I_EQ,5,0,1);auto queryHands=b.branch(5);auto hasHands=b.jump();
    b.end(queryHands);b.call(GGV_MASTER_HAS_HANDS,5,0);auto noHands=b.branch(5);b.end(hasHands);
    b.read(0,GGV_MASTER_WEAPON);b.i(1,1);b.emit(GGV_I_EQ,5,0,1);b.i(12,2);auto armed=b.branch(5);b.i(12,3);b.end(armed);
    b.read(3,GGV_LOADED_MASTERLOADSTATE);b.emit(GGV_I_LT,5,3,12);b.emit(GGV_NOT,5,5);auto tooEarly=b.branch(5);
    b.call(GGV_MASTER_SOURCE_CURRENT,5);b.emit(GGV_NOT,5,5);auto current=b.branch(5);b.call(GGV_MEM_ENTER_FLUX);
    b.end(current);b.end(tooEarly);b.end(noHands);b.ret(1);
    b.end(noSource);b.end(noFile);b.end(noBody);b.call(GGV_MASTER_SOURCE_ERROR);b.ret(0);return b.s;
}
constexpr auto masterSourcePrepare=masterSourcePrepareSource();static_assert(valid(masterSourcePrepare),"Master source preparation must be typed");
constexpr ggv_source masterFluxTransitionSource() {
    Builder b;b.nullref(9);b.i(11,0);b.read(12,GGV_FLUX_CASING_COUNT);b.emit(GGV_I_LT,5,11,12);auto empty=b.branch(5);auto clearLoop=b.s.count;
    b.write(GGV_FLUX_CASING_INDEX,11);b.read(2,GGV_FLUX_CASING_MODEL);b.read(8,GGV_MASTER_CART_MODEL);b.emit(GGV_REF_EQ,5,2,8);auto otherModel=b.branch(5);
    b.write(GGV_FLUX_CASING_MODEL,9);b.end(otherModel);b.emit(GGV_I_INCREMENT,11,11);b.emit(GGV_I_LT,5,11,12);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,5)].target=clearLoop;b.end(empty);
    b.boolean(6,false);b.write(GGV_FLUX_CASINGS_ACTIVE,6);b.i(11,0);b.read(12,GGV_FLUX_CASING_COUNT);b.emit(GGV_I_LT,5,11,12);auto noCasings=b.branch(5);auto scanLoop=b.s.count;
    b.write(GGV_FLUX_CASING_INDEX,11);b.read(2,GGV_FLUX_CASING_MODEL);b.emit(GGV_REF_NONNULL,5,2);auto nullModel=b.branch(5);
    b.boolean(6,true);b.write(GGV_FLUX_CASINGS_ACTIVE,6);b.end(nullModel);b.emit(GGV_I_INCREMENT,11,11);b.emit(GGV_I_LT,5,11,12);b.s.nodes[b.emit(GGV_REPEAT_WHILE,0,5)].target=scanLoop;b.end(noCasings);
    b.write(GGV_MASTER_CART_MODEL,9);b.i(0,1);b.write(GGV_LOADED_MASTERLOADSTATE,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();b.call(GGV_MASTER_FLUX_LOG);b.end(noLog);b.ret(0);return b.s;
}
constexpr auto masterFluxTransition=masterFluxTransitionSource();static_assert(valid(masterFluxTransition),"Master flux transition must be typed");
constexpr ggv_source masterGunStageSource() {
    Builder b;b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,0);auto pending=b.branch();
    b.read(8,GGV_FLUX_LOAD_PTR);b.write(GGV_MASTER_MEM_PTR,8);b.read(0,GGV_FLUX_LOAD_REMAINING);b.emit(GGV_I_TO_U,9,0);b.write(GGV_MASTER_MEM_REMAINING,9);
    b.i(0,1);b.write(GGV_MEM_GUNLOADSTATE,0);b.read(0,GGV_MASTER_WEAPON);b.i(1,1);b.emit(GGV_I_EQ,5,0,1);auto armed=b.branch(5);
    b.call(GGV_MASTER_QUEUE_BODY,5);auto bodyFailed=b.branch(5);auto bodyQueued=b.jump();b.end(armed);
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_INFO_IS_V2,5,0);auto staticWeapon=b.branch(5);
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_MASTER_RUNTIME_MODEL,8,0);b.call(GGV_MASTER_QUEUE_RETAINED,5,8);auto retainedFailed=b.branch(5);auto retainedQueued=b.jump();
    b.end(staticWeapon);b.call(GGV_MASTER_QUEUE_STATIC);b.end(retainedQueued);b.end(bodyQueued);b.end(pending);
    b.call(GGV_MASTER_GUN_TICK);b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,4);auto notLoaded=b.branch();
    b.i(0,3);b.write(GGV_LOADED_MASTERLOADSTATE,0);b.i(0,0);b.write(GGV_MEM_GUNLOADSTATE,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();b.call(GGV_MASTER_GUN_LOG);b.end(noLog);b.end(notLoaded);b.ret(1);
    b.end(retainedFailed);b.end(bodyFailed);b.ret(0);return b.s;
}
constexpr auto masterGunStage=masterGunStageSource();static_assert(valid(masterGunStage),"Master gun stage must be typed");
constexpr ggv_source masterHandsStageSource() {
    Builder b;b.read(6,GGV_MASTER_HAS_HANDS_INPUT);auto noHands=b.branch(6);
    b.read(0,GGV_MASTER_HANDFILE);b.read(1,GGV_FLUX_FILE);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto checkCurrent=b.branch(5);auto refresh=b.jump();
    b.end(checkCurrent);b.call(GGV_MASTER_SOURCE_CURRENT,5);b.emit(GGV_NOT,5,5);auto fresh=b.branch(5);b.end(refresh);
    b.call(GGV_MASTER_HAND_SOURCE,5);auto sourceFailed=b.branch(5);
    b.call(GGV_MASTER_COMPARE_LOAD_ID,0);b.i(1,0);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto checkHandle=b.branch(5);auto invalidateId=b.jump();
    b.end(checkHandle);b.call(GGV_MASTER_SAME_LOAD_HANDLE,5);b.emit(GGV_NOT,5,5);auto checkGeneration=b.branch(5);auto invalidateHandle=b.jump();
    b.end(checkGeneration);b.read(8,GGV_MASTER_LOAD_GENERATION);b.call(GGV_MASTER_CATALOG_GENERATION,9);b.emit(GGV_U_TO_I,0,8);b.emit(GGV_U_TO_I,1,9);b.emit(GGV_I_EQ,5,0,1);b.emit(GGV_NOT,5,5);auto sameGeneration=b.branch(5);
    b.end(invalidateId);b.end(invalidateHandle);b.i(0,0);b.write(GGV_MEM_GUNLOADSTATE,0);b.end(sameGeneration);
    b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,0);auto loading=b.branch();b.call(GGV_MASTER_MEMORY,10);b.write(GGV_FLUX_LOAD_PTR,10);
    b.call(GGV_MASTER_CAPACITY,9);b.emit(GGV_U_TO_I,0,9);b.write(GGV_FLUX_LOAD_REMAINING,0);b.i(0,1);b.write(GGV_MEM_GUNLOADSTATE,0);
    b.call(GGV_MASTER_QUEUE_HAND,5);auto queueFailed=b.branch(5);b.end(loading);
    b.call(GGV_MASTER_GUN_TICK);b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,4);auto notLoaded=b.branch();b.read(0,GGV_MASTER_HANDFILE);b.write(GGV_FLUX_FILE,0);b.end(fresh);auto handsDone=b.jump();
    b.end(noHands);b.i(0,0);b.write(GGV_FLUX_FILE,0);b.write(GGV_FLUX_CATALOG_HEAD,0);b.call(GGV_MASTER_CLEAR_HAND_HANDLE);
    b.call(GGV_MASTER_CATALOG_GENERATION,9);b.write(GGV_FLUX_GENERATION,9);b.nullref(10);b.write(GGV_FLUX_HAND_MODEL,10);
    b.call(GGV_MASTER_MEMORY,10);b.write(GGV_FLUX_LOAD_PTR,10);b.call(GGV_MASTER_CAPACITY,9);b.emit(GGV_U_TO_I,0,9);b.write(GGV_FLUX_LOAD_REMAINING,0);
    b.end(handsDone);b.i(0,2);b.write(GGV_LOADED_MASTERLOADSTATE,0);b.i(0,0);b.write(GGV_MEM_GUNLOADSTATE,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();b.call(GGV_MASTER_HANDS_LOG);b.end(noLog);b.ret(1);
    b.end(notLoaded);b.end(queueFailed);b.end(sourceFailed);b.ret(0);return b.s;
}
constexpr auto masterHandsStage=masterHandsStageSource();static_assert(valid(masterHandsStage),"Master hands stage must be typed");
constexpr ggv_source masterCartsSelectSource() {
    Builder b;b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,4);auto notLoaded=b.branch();b.i(0,0);b.write(GGV_MEM_GUNLOADSTATE,0);b.end(notLoaded);
    b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,0);auto notFlux=b.branch();b.read(14,GGV_MASTER_CART_MODEL);b.emit(GGV_REF_NONNULL,5,14);b.emit(GGV_NOT,5,5);auto hasCart=b.branch(5);
    b.compare(GGV_CART_PLAYER_COUNT,GGV_I_EQ,1);auto manyPlayers=b.branch();unsigned done[2]{};
    for(int hand:{0,1}) {
        b.i(0,hand);b.call(GGV_CART_FUNCTION,8,0);b.write(GGV_CART_FUNC,8);
        b.read(0,GGV_SW_WEAPON);b.call(GGV_CART_FIND_WEAPON,9,0);b.write(GGV_CART_WEAPON,9);
        b.emit(GGV_REF_NONNULL,5,8);auto noFunction=b.branch(5);b.read(0,GGV_CART_TYPE);b.emit(GGV_I_TO_U,12,0);b.uop(GGV_U_AND_LITERAL,12,12,255);b.emit(GGV_U_TO_I,0,12);
        b.i(1,1);b.emit(GGV_I_EQ,5,0,1);auto notShoot=b.branch(5);b.emit(GGV_REF_NONNULL,5,9);auto noWeapon=b.branch(5);
        b.read(0,GGV_CART_AMMO_INDEX);b.i(1,0);b.emit(GGV_I_LT,5,0,1);b.emit(GGV_NOT,5,5);auto negativeAmmo=b.branch(5);
        b.call(GGV_CART_AMMO_REF,10,0);b.write(GGV_CART_AMMO,10);b.emit(GGV_REF_NONNULL,5,10);auto noAmmo=b.branch(5);
        b.read(13,GGV_CART_EJECT);b.emit(GGV_U_TO_I,0,13);b.i(1,0);b.emit(GGV_I_LT,5,0,1);b.emit(GGV_NOT,5,5);auto noCasing=b.branch(5);
        b.read(14,GGV_MASTER_CART_MODEL);b.emit(GGV_REF_NONNULL,5,14);b.emit(GGV_NOT,5,5);auto alreadyCart=b.branch(5);
        b.call(GGV_CART_FILE,1,0);b.write(GGV_MASTER_FILE,1);b.i(0,1);b.write(GGV_MEM_GUNLOADSTATE,0);b.call(GGV_CART_QUEUE);b.end(alreadyCart);done[hand]=b.jump();
        b.end(noCasing);b.end(noAmmo);b.end(negativeAmmo);b.end(noWeapon);b.end(notShoot);b.end(noFunction);
    }
    b.end(done[0]);b.end(done[1]);b.end(manyPlayers);b.end(hasCart);b.end(notFlux);
    b.compare(GGV_MEM_GUNLOADSTATE,GGV_I_EQ,0);auto tick=b.branch();b.ret(1);b.end(tick);b.call(GGV_MASTER_GUN_TICK);b.ret(0);return b.s;
}
constexpr auto masterCartsSelect=masterCartsSelectSource();static_assert(valid(masterCartsSelect),"Master cartridge selection must be typed");
constexpr void masterFinalizeAdvance(Builder &b) {
    b.read(1,GGV_FINAL_SUM);b.emit(GGV_I_ADD_WRAP,1,1,0);b.write(GGV_FINAL_SUM,1);
    b.read(8,GGV_MASTER_MEM_PTR);b.call(GGV_FINAL_PTR_OFFSET,8,8,0);b.write(GGV_MASTER_MEM_PTR,8);
    b.read(11,GGV_MASTER_MEM_REMAINING);b.emit(GGV_U_TO_I,1,11);b.emit(GGV_I_SUB_WRAP,1,1,0);b.emit(GGV_I_TO_U,11,1);b.write(GGV_MASTER_MEM_REMAINING,11);
}
constexpr ggv_source masterFinalizeSource() {
    Builder b;b.i(0,0);b.write(GGV_FINAL_SUM,0);
    for(int hand:{0,1}) {
        b.i(0,hand);b.write(GGV_FINAL_HAND_INDEX,0);
        b.read(8,GGV_FINAL_GUN_MODEL);b.read(9,GGV_FINAL_GUN_DEF);b.read(10,GGV_FINAL_GUN_SAVE);b.s.nodes[b.emit(GGV_CALL,0,8,9,10)].service=GGV_FINAL_MODEL_INIT;
        b.read(9,GGV_FLUX_HAND_MODEL);b.emit(GGV_REF_NONNULL,4,9);auto noHandInit=b.branch();
        b.read(8,GGV_FINAL_HAND_MODEL);b.read(9,GGV_FLUX_HAND_MODEL);b.read(10,GGV_FINAL_HAND_SAVE);b.s.nodes[b.emit(GGV_CALL,0,8,9,10)].service=GGV_FINAL_MODEL_INIT;b.end(noHandInit);
        b.read(10,GGV_MASTER_MEM_PTR);b.write(GGV_FINAL_GUN_COMMAND,10);
        b.read(8,GGV_FINAL_GUN_MODEL);b.read(9,GGV_FINAL_GUN_ROOT);b.read(10,GGV_MASTER_MEM_PTR);b.s.nodes[b.emit(GGV_CALL,0,8,9,10)].service=GGV_FINAL_COMMANDS;masterFinalizeAdvance(b);
        b.read(9,GGV_FLUX_HAND_MODEL);b.emit(GGV_REF_NONNULL,4,9);auto noHandCommands=b.branch();
        b.read(10,GGV_MASTER_MEM_PTR);b.write(GGV_FINAL_HAND_COMMAND,10);
        b.read(8,GGV_FINAL_HAND_MODEL);b.read(9,GGV_FINAL_HAND_ROOT);b.read(10,GGV_MASTER_MEM_PTR);b.s.nodes[b.emit(GGV_CALL,0,8,9,10)].service=GGV_FINAL_COMMANDS;masterFinalizeAdvance(b);b.end(noHandCommands);
    }
    b.i(0,0);b.write(GGV_FINAL_HAND_INDEX,0);b.i(0,-1);b.write(GGV_FINAL_MATRIX_STATE,0);
    b.read(11,GGV_MASTER_MEM_REMAINING);b.read(12,GGV_FINAL_MATRIX_BYTES);b.emit(GGV_U_GT,4,11,12);auto noMatrices=b.branch();
    b.read(8,GGV_MASTER_MEM_PTR);b.write(GGV_FINAL_MATRIX_PTR,8);b.read(8,GGV_MASTER_MEM_PTR);b.emit(GGV_U_TO_I,0,12);b.call(GGV_FINAL_PTR_OFFSET,8,8,0);b.write(GGV_MASTER_MEM_PTR,8);
    b.read(11,GGV_MASTER_MEM_REMAINING);b.emit(GGV_U_TO_I,1,11);b.emit(GGV_I_SUB_WRAP,1,1,0);b.emit(GGV_I_TO_U,11,1);b.write(GGV_MASTER_MEM_REMAINING,11);auto matrixDone=b.jump();
    b.end(noMatrices);b.nullref(8);b.write(GGV_FINAL_MATRIX_PTR,8);b.end(matrixDone);
    b.call(GGV_MASTER_CAPACITY,11);b.i(0,4);b.write(GGV_LOADED_MASTERLOADSTATE,0);b.read(0,GGV_MASTER_WEAPON);b.write(GGV_LOADED_GUNMEMTYPE,0);b.i(0,-1);b.write(GGV_LOADED_GUNMEMNEW,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noLog=b.branch();b.call(GGV_FINAL_LOG);b.end(noLog);b.ret(1);return b.s;
}
constexpr auto masterFinalize=masterFinalizeSource();static_assert(valid(masterFinalize),"Master finalization must be typed");
constexpr ggv_source masterLoadParentSource() {
    Builder b;b.call(GGV_PARENT_READY,4);auto notReady=b.branch();b.call(GGV_PARENT_PREPARE,4);auto notPrepared=b.branch();
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noEntry=b.branch();
    b.read(0,GGV_MASTER_WEAPON);b.emit(GGV_I_TO_U,10,0);b.i(1,8);b.emit(GGV_I_TO_U,11,1);b.emit(GGV_U_SHL,10,10,11);
    b.read(1,GGV_LOADED_MASTERLOADSTATE);b.emit(GGV_I_TO_U,11,1);b.emit(GGV_U_OR,10,10,11);b.emit(GGV_U_TO_I,0,10);
    b.read(1,GGV_PARENT_LAST_KEY);b.emit(GGV_I_EQ,4,0,1);b.emit(GGV_NOT,4,4);auto sameKey=b.branch();b.write(GGV_PARENT_LAST_KEY,0);
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_MASTER_HAS_HANDS,5,0);b.write(GGV_PARENT_DIAG_HANDS,5);b.call(GGV_PARENT_ENTRY_LOG);b.end(sameKey);b.end(noEntry);
    b.compare(GGV_LOADED_MASTERLOADSTATE,GGV_I_EQ,4);auto differentState=b.branch();
    b.read(0,GGV_MASTER_WEAPON);b.read(1,GGV_LOADED_GUNMEMTYPE);b.emit(GGV_I_EQ,4,0,1);b.emit(GGV_NOT,4,4);auto sameWeapon=b.branch();b.end(differentState);
    b.compare(GGV_MASTER_FILE,GGV_I_EQ,0);auto hasFile=b.branch();b.read(0,GGV_MASTER_WEAPON);b.call(GGV_MASTER_RUNTIME_MODEL,8,0);b.emit(GGV_REF_NONNULL,4,8);auto noModel=b.branch();b.end(hasFile);
    b.boolean(5,false);b.write(GGV_MASTER_HAS_HANDS_INPUT,5);b.compare(GGV_MASTER_HANDFILE,GGV_I_EQ,0);b.emit(GGV_NOT,4,4);auto noHandFile=b.branch();
    b.read(0,GGV_MASTER_WEAPON);b.call(GGV_MASTER_HAS_HANDS,5,0);b.write(GGV_MASTER_HAS_HANDS_INPUT,5);b.end(noHandFile);
    b.compare(GGV_MASTER_WEAPON,GGV_I_EQ,1);auto armed=b.branch();b.read(0,GGV_MASTER_HANDFILE);b.write(GGV_MASTER_FILE,0);b.i(0,0);b.write(GGV_MASTER_HANDFILE,0);b.boolean(5,false);b.write(GGV_MASTER_HAS_HANDS_INPUT,5);b.end(armed);
    b.compare(GGV_LOADED_MASTERLOADSTATE,GGV_I_EQ,0);auto notFlux=b.branch();b.call(GGV_PARENT_FLUX);b.ret(1);b.end(notFlux);
    b.compare(GGV_LOADED_MASTERLOADSTATE,GGV_I_EQ,1);auto notHands=b.branch();b.call(GGV_PARENT_HANDS,4);b.ret(1);b.end(notHands);
    b.compare(GGV_LOADED_MASTERLOADSTATE,GGV_I_EQ,2);auto notGun=b.branch();b.call(GGV_PARENT_GUN,4);b.ret(1);b.end(notGun);
    b.compare(GGV_LOADED_MASTERLOADSTATE,GGV_I_EQ,3);auto notCarts=b.branch();b.call(GGV_PARENT_CARTS,4);auto noFinalize=b.branch();b.call(GGV_PARENT_FINALIZE);b.end(noFinalize);b.end(notCarts);b.ret(1);
    b.end(noModel);b.read(4,GGV_VERSION_1_OR_LATER);auto oldVersion=b.branch();b.i(0,4);b.write(GGV_LOADED_MASTERLOADSTATE,0);b.read(0,GGV_MASTER_WEAPON);b.write(GGV_LOADED_GUNMEMTYPE,0);b.i(0,-1);b.write(GGV_LOADED_GUNMEMNEW,0);
    b.compare(GGV_SW_PLAYER_NUM,GGV_I_EQ,0);auto noShortcutLog=b.branch();b.call(GGV_PARENT_SHORTCUT_LOG);b.end(noShortcutLog);b.end(oldVersion);
    b.end(sameWeapon);b.end(notPrepared);b.end(notReady);b.ret(1);return b.s;
}
constexpr auto masterLoadParent=masterLoadParentSource();static_assert(valid(masterLoadParent),"Master parent must be typed");
constexpr ggv_source gunFailureRecoverySource() {
    Builder b;b.call(GGV_MASTER_CATALOG_GENERATION,0);b.read(1,GGV_MASTER_LOAD_GENERATION);
    b.emit(GGV_U_TO_I,2,0);b.emit(GGV_U_TO_I,3,1);b.emit(GGV_I_EQ,4,2,3);b.emit(GGV_NOT,4,4);auto unchanged=b.branch();
    b.call(GGV_GUN_FAILURE_LOG,0,0);b.i(2,0);b.write(GGV_MEM_GUNLOADSTATE,2);b.end(unchanged);b.ret(1);return b.s;
}
constexpr auto gunFailureRecovery=gunFailureRecoverySource();static_assert(valid(gunFailureRecovery),"Gun failure recovery must be typed");
constexpr ggv_source gunRetainedLoadSource() {
    Builder b;b.read(8,GGV_GL_RETAINED_MODEL);b.call(GGV_GL_GENERATION,9,8);b.call(GGV_GL_GENERATION_ID,10,9);
    b.read(11,GGV_GL_TARGET);b.emit(GGV_REF_NONNULL,4,11);auto noTarget=b.branch();b.emit(GGV_REF_NONNULL,4,10);auto noId=b.branch();
    b.call(GGV_GL_COMPARE_ID,0,10);b.i(1,0);b.emit(GGV_I_EQ,4,0,1);auto wrongId=b.branch();
    b.call(GGV_GL_ALLOCATE_RW,0,8);b.write(GGV_GL_TARGET_MODEL,8);b.nullref(11);b.write(GGV_GL_RETAINED_MODEL,11);
    b.i(0,0);b.emit(GGV_I_TO_U,2,0);b.write(GGV_GL_NEXT_TEXTURE,2);b.write(GGV_GL_ALLOC_SIZE,2);b.write(GGV_GL_LOADED_SIZE,2);
    b.i(0,4);b.write(GGV_MEM_GUNLOADSTATE,0);b.call(GGV_GL_GENERATION_HASH,11,9);b.call(GGV_GL_RETAINED_LOG,0,10,11);b.ret(1);
    b.end(noTarget);b.end(noId);b.end(wrongId);b.call(GGV_GL_FAIL);b.ret(1);return b.s;
}
constexpr auto gunRetainedLoad=gunRetainedLoadSource();static_assert(valid(gunRetainedLoad),"Retained gun model load must be typed");
constexpr ggv_source gunSourceRouteSource() {
    Builder b;b.call(GGV_GL_CAN_USE_HANDLE,4);auto invalid=b.branch();b.call(GGV_GL_EXTERNAL_SOURCE,4);auto notExternal=b.branch();
    b.call(GGV_GL_CATALOG_MODEL,8);b.emit(GGV_REF_NONNULL,4,8);auto loadFailed=b.branch();
    b.i(0,4);b.write(GGV_MEM_GUNLOADSTATE,0);b.read(4,GGV_GL_BODY_HAND);auto noHand=b.branch();
    b.read(0,GGV_GL_LOAD_FILE);b.write(GGV_FLUX_FILE,0);b.call(GGV_GL_COPY_HAND_ID);b.call(GGV_GL_COPY_HAND_HANDLE);b.read(1,GGV_MASTER_LOAD_GENERATION);b.write(GGV_FLUX_GENERATION,1);b.end(noHand);b.ret(1);
    b.end(loadFailed);b.i(0,0);b.write(GGV_GL_LOAD_TYPE,0);b.call(GGV_GL_FAIL);b.ret(1);
    // Unsupported providers are source-chain failures, never a legacy byte-loader route.
    b.end(notExternal);b.end(invalid);b.call(GGV_GL_FAIL);b.ret(1);return b.s;
}
constexpr auto gunSourceRoute=gunSourceRouteSource();static_assert(valid(gunSourceRoute),"Gun source routing must be typed");
constexpr ggv_source gunTexturesSource() {
    Builder b;b.call(GGV_GL_TEXTURE_BEGIN);b.read(12,GGV_GL_TARGET_MODEL);b.write(GGV_GL_TEXTURE_MODEL,12);
    b.read(11,GGV_GL_NEXT_TEXTURE);b.emit(GGV_U_TO_I,0,11);b.write(GGV_GL_TEXTURE_INDEX,0);
    b.i(2,0);b.emit(GGV_I_TO_I64,9,2);b.read(10,GGV_GL_TEXTURE_LIMIT);b.i(3,1);unsigned exhausted[3]{};
    for(int tick=0;tick<3;++tick) {
        b.read(0,GGV_GL_TEXTURE_INDEX);b.read(1,GGV_GL_TEXTURE_COUNT);b.emit(GGV_I_LT,4,0,1);exhausted[tick]=b.branch();b.call(GGV_GL_TEXTURE_AT,0,0);
        b.read(8,GGV_GL_TEXTURE_WORD);b.emit(GGV_I64_LT,4,8,9);b.emit(GGV_NOT,4,4);auto highPointer=b.branch();b.emit(GGV_I64_LT,4,8,10);auto resident=b.branch();
        b.read(1,GGV_GL_TEXTURE_COUNT);b.call(GGV_GL_TEXTURE_UNCOMPRESS,0,0,1);b.call(GGV_GL_TEXTURE_LOAD);b.i(2,1);b.write(GGV_GL_TEXTURE_MARK,2);b.end(resident);b.end(highPointer);
        b.read(11,GGV_GL_NEXT_TEXTURE);b.emit(GGV_U_TO_I,2,11);b.emit(GGV_I_ADD_WRAP,2,2,3);b.emit(GGV_I_TO_U,11,2);b.write(GGV_GL_NEXT_TEXTURE,11);
        if(tick==2)b.ret(1);else {b.read(0,GGV_GL_TEXTURE_INDEX);b.emit(GGV_I_ADD_WRAP,0,0,3);b.write(GGV_GL_TEXTURE_INDEX,0);}
    }
    for(auto edge:exhausted)b.end(edge);b.call(GGV_GL_TEXTURE_DONE);b.i(2,3);b.write(GGV_MEM_GUNLOADSTATE,2);b.ret(1);return b.s;
}
constexpr auto gunTextures=gunTexturesSource();static_assert(valid(gunTextures),"Gun texture stage must be typed");
constexpr ggv_program stock[] = {{control,seal,0}, {credit,seal,0},
    {fullControl,seal,1}, {fullCommit,seal,1}, {handRumble,seal,1,1}, {handSlide,seal,0,0},
    {recoilRecovery,seal,1,0}, {attackShoot,seal,1,1}, {throwControl,seal,1,0}, {minClipQty,seal,0,0}, {meleeControl,seal,1,0}, {specialControl,seal,1,0}, {emptyAttack,seal,1,0}, {setState,seal,1,0}, {attackDispatch,seal,1,0}, {animationReset,seal,0,0}, {animationBusy,seal,0,0}, {triggerInput,seal,1,1}, {ammoCount,seal,1,0}, {reservedAmmo,seal,1,0}, {ammoEligibility,seal,1,0}, {unlimitedAmmo,seal,0,0}, {ammoStatus,seal,1,0}, {ammoCapacity,seal,0,0}, {ammoTypeQuery,seal,1,0}, {ammoQtyQuery,seal,1,0}, {ammoWeaponCapacity,seal,1,0}, {unequippedReload,seal,0,0}, {reloadTransfer,seal,1,0}, {reloadBoth,seal,1,0}, {countdown,seal,0,0}, {gunVisibility,seal,0,0}, {currentWeapon,seal,0,0}, {setAmmoQuantity,seal,1,0}, {setWeaponAmmoQuantity,seal,1,0}, {reloadTick,seal,1,0}, {changeFuncTick,seal,1,0}, {armPitch,seal,1,0}, {isLoaded,seal,0,0}, {isReloading,seal,0,0}, {animationEligibility,seal,0,0}, {animationFrame,seal,1,0}, {animationWait,seal,1,0}, {animationWaitKey,seal,1,0}, {animationStart,seal,1,0}, {handTick,seal,1,0}, {tickHand,seal,1,0}, {autoSwitch,seal,1,0}, {idle,seal,1,0}, {readyToSwitch,seal,1,0}, {usingSecondary,seal,1,0}, {handExclusion,seal,1,0}, {anyAmmo,seal,1,0}, {weaponInfo,seal,1,0}, {switchComplete,seal,1,0}, {canFreeWeapon,seal,1,0}, {freeHeldRocket,seal,1,0}, {storedFunctionSecondary,seal,0,0}, {storeFunctionSelection,seal,1,0}, {canStoreFunction,seal,0,0}, {resetFunctionInversion,seal,1,0}, {considerFunctionToggle,seal,1,0}, {autoSelectWeapon,seal,1,0}, {equipSound,seal,1,0}, {changeGun,seal,1,0}, {animationSoundCommands,seal,1,0}, {animationPartCommand,seal,1,0}, {animationFirstCommands,seal,1,0}, {animationDriver,seal,1,0}, {visibilityCommands,seal,1,0}, {partVisibility,seal,1,0}, {ammoVisibility,seal,1,0}, {switchHandReset,seal,1,0}, {switchDriver,seal,1,0}, {handConfigure,seal,1,0}, {abmagReset,seal,0,0}, {freeWeapon,seal,1,0}, {equipWeapon,seal,1,0}, {equipWeaponHand,seal,1,0}, {initHandAnims,seal,1,0}, {freeGunMemory,seal,1,0}, {setGunMemoryWeapon,seal,1,0}, {tickLoad,seal,1,0}, {changeMemoryOwner,seal,1,0}, {loadAll,seal,1,0}, {enterFlux,seal,1,0}, {masterLoadAdmission,seal,1,0}, {masterSourcePrepare,seal,1,0}, {masterFluxTransition,seal,1,0}, {masterGunStage,seal,1,0}, {masterHandsStage,seal,1,0}, {masterCartsSelect,seal,1,0}, {masterFinalize,seal,1,0}, {masterLoadParent,seal,1,0}, {gunFailureRecovery,seal,1,0}, {gunRetainedLoad,seal,1,0}, {gunSourceRoute,seal,1,0}, {gunTextures,seal,1,0}};
static_assert(sizeof(stock)/sizeof(stock[0])==GGV_GUN_TEXTURES+1,"Stock table must cover every stock program");

/* The anonymous effect4 contract pins type codes1..5 and success0. Its macro
 * spelling is not specified; use the exact published numeric schema here.
 * No source-level alternate gateway or engine effect fallback is provided. */
constexpr uint32_t effectType(ggv_type type) { return uint32_t(type)+1u; }
static e4_value effectValue(ggv_type type,ggv_value value) {
    e4_value out{};out.type=effectType(type);
    switch(type) {
    case GGV_I32: out.payload.i32=value.i;break;
    case GGV_U32: out.payload.u32=value.u;break;
    case GGV_F32: std::memcpy(&out.payload.f32,&value.f,sizeof value.f);break;
    case GGV_BOOL: out.payload.b32=value.b;break;
    case GGV_REF: out.payload.ref=value.ref;break;
    // No four-input service accepts I64. Keep unsupported types out of e4.
    case GGV_I64: out.type=0;break;
    }
    return out;
}
static ggv_value graphValue(const e4_value &value) {
    ggv_value out{};
    switch(value.type) {
    case 1: out.i=value.payload.i32;break;
    case 2: out.u=value.payload.u32;break;
    case 3: std::memcpy(&out.f,&value.payload.f32,sizeof out.f);break;
    case 4: out.b=value.payload.b32;break;
    case 5: out.ref=value.payload.ref;break;
    }
    return out;
}
struct Effect4Context { const ggv_host *host; ggv_service4 service; };
static e4_payload invokeEffect4(void *opaque,const e4_value args[4]) noexcept {
    const auto &context=*static_cast<Effect4Context *>(opaque);
    context.host->call4(context.host->context,context.service,graphValue(args[0]),
        graphValue(args[1]),graphValue(args[2]),graphValue(args[3]));
    return e4_payload{}; // The current whitelist is VOID only.
}
static_assert(sizeof(int)==sizeof(int32_t),"BOOL32 requires native32-bit int");
static_assert(sizeof(float)==4,"Effect f32 witnesses require32-bit float");
}

extern "C" const char *ggvBindingName(ggv_binding b) {
    static const char *names[] = {"function_flags","ammo_index","automatic","acceleration",
        "deceleration","trigger","state_flags","burst_bullets","aim","loaded_ammo",
        "ammo_type","shotgun","shotgun_delay","laser","initial_rpm","max_rpm","clock",
        "spool","state_frames","shots","remainder","firing",
        "graph_flags","legacy_flags","graph_ammo_slot","legacy_ammo_index",
        "graph_has_type","graph_function_type","legacy_function_type","function_type",
        "legacy_automatic","graph_has_max_rpm","graph_has_accel","graph_has_decel",
        "graph_accel","graph_decel","legacy_accel","legacy_decel","graph_has_initial_rpm",
        "graph_initial_rpm","graph_max_rpm","legacy_initial_rpm","legacy_max_rpm",
        "ge_flash","flash","load_slide","state_var1","attack_type","attack_shoot",
        "attack_projectile","function_shoot_low","function_shoot_single","current_frame",
        "other_last_frame","allow_shoot_frame","last_shoot_frame","audio_slot_2","audio_slot_3",
        "mauler","matmot","priority_enabled","sound_stopped",
        "hand_number","hand_left_number","pad_1","pad_2","control_mode_21",
        "pak_rumble_type","current_hand_slideinc",
        "recovery_pal", "recovery_jpn", "graph_recoil_24_has", "graph_recoil_24", "legacy_recoil_24", "graph_recoil_25_has", "graph_recoil_25", "legacy_recoil_25", "graph_recoil_26_has", "graph_recoil_26", "legacy_recoil_26", "graph_recoil_27_has", "graph_recoil_27", "legacy_recoil_27", "graph_recoil_time_has", "graph_recoil_time", "legacy_recoil_time", "graph_recoil_dist_has", "graph_recoil_dist", "legacy_recoil_dist", "graph_recoil_angle_has", "graph_recoil_angle", "legacy_recoil_angle", "trigger_released", "rotx_start", "rotx_end", "rotx_offset", "use_posrot", "state_minor", "animation_latch", "matmot2", "matmot1", "pos_start_x", "pos_start_y", "pos_start_z", "pos_end_x", "pos_end_y", "pos_end_z", "pos_offset_x", "pos_offset_y", "pos_offset_z", "aim_pos_x", "aim_pos_z", "definition_pos_z", "recoil_pi", "recoil_bad_tau", "pos_matrix", "pos_coord", "state_cycles", "animation_busy", "fire_animation", "passed_hand", "vibration_start", "vibration_max", "vibration_scratch", "vibration_upgrade", "vibration_final",
        "throw_graph_has_activation", "throw_graph_activation", "throw_legacy_activation", "throw_graph_has_recovery", "throw_graph_recovery", "throw_legacy_recovery", "prime_timer", "current_player_throwing", "weapon_number", "weapon_function", "version_1_or_later",
        "special_graph_has_value", "special_graph_value", "special_legacy_value", "special_graph_has_recovery", "special_graph_recovery", "special_legacy_recovery", "empty_anim_mode", "empty_definition", "empty_mode", "empty_count60", "empty_count", "empty_lvupdate", "empty_water_sound", "empty_click_sound",
        "state_request", "state_value", "state_last_frame", "state_player_number", "state_inuse", "state_log_weapon", "state_log_inuse",
        "anim_load", "anim_incremental", "anim_command", "anim_loop_count", "anim_bit02", "anim_bit07", "trigger_request", "trigger_previous", "trigger_player",
        "ammo_reserve", "ammo_request", "ammo_hand_index", "ammo_slot_index", "ammo_hand_inuse", "ammo_equipped_type", "ammo_hand_weapon", "ammo_loaded", "ammo_function_ref", "ammo_weapon_ref", "ammo_definition_type", "ammo_request_u32", "ammo_stage",
        "query_function_i32", "query_equipped_type", "query_clip_capacity", "query_reserve", "query_capacity", "query_weapon_u32", "query_weapon_i32", "query_ammo_ref_u32", "query_ammo_ref_i32", "query_ammo_type", "query_cached_ammo", "query_cached_type", "query_cached_capacity",
        "reload_function", "reload_one", "reload_check", "reload_valid_weapon", "reload_index", "reload_rounds", "reload_ticks256", "reload_pal", "reload_jpn", "reload_reserve", "reload_reserve_clear", "reload_ammo_flags",
        "countdown_hand_index", "countdown_slot_index", "countdown_spent", "countdown_update60", "vis_command_type", "vis_upgrades", "vis_parameter", "vis_is_left_hand", "vis_is_right_hand",
        "current_hand_inuse", "current_weapon_number", "quantity_ammo_request", "quantity_request", "quantity_request_u32", "quantity_function", "quantity_loaded_right", "quantity_loaded_left", "quantity_clip_right", "quantity_capacity", "quantity_reserve",
        "reload_player_dead", "reload_other_state", "reload_other_stateframes", "reload_gs_int1", "reload_gs_int2", "reload_func_ammo_index", "reload_ammo_animation", "reload_func_ammo_flags", "reload_unk0d0e_07", "reload_incremental", "reload_eject_state",
        "reload_devices_active", "reload_devices_inhibit", "reload_count60", "reload_count", "reload_visible", "reload_mode", "reload_pausetime60", "reload_unk0cc8_02", "reload_lvupdate240", "reload_camera_mode", "reload_player_invincible",
        "reload_weapon_briefcase2", "reload_doautoselect",
        "right_hand_unk0dd4", "arm_pitch_angle", "arm_pitch_matrix", "arm_pitch_m30", "arm_pitch_m31", "arm_pitch_m32", "loaded_gunmemowner", "loaded_gunmemtype", "loaded_gunmemnew", "loaded_masterloadstate", "anim_eligibility_kind", "anim_eligibility_left_inuse", "anim_frame_command", "anim_frame_command_value", "anim_frame_result", "anim_wait_time", "anim_wait_cursor", "anim_wait_type", "anim_wait_key", "anim_wait_value", "anim_wait_increment", "anim_wait_increment_real", "anim_start_command", "anim_start_child", "anim_start_previous", "anim_tick_increment_legacy", "hand_lastdirvalid", "hand_mode_next", "normal_multiplayer", "multiplayer", "auto_other_inuse", "auto_other_function", "auto_fresh_other_state", "hand_auto_bit07", "hand_auto_bit08", "hand_activate_secondary", "gunctrl_wantammo", "idle_function_type", "auto_other_trigger", "auto_other_bit08", "switch_range_valid", "switch_right_weapon", "switch_current_throwing", "switch_left_inuse", "switch_left_state", "switch_left_minor", "switch_pending_weapon", "switch_right_state", "switch_dualwield", "function_control_weapon", "function_control_invert", "info_weapon_result", "info_definition_result", "info_gunctrl", "switch_dualwield_raw", "free_throwing", "free_last_right", "free_last_left", "held_rocket", "function_native_first", "function_native_last", "function_custom_first", "function_custom_end", "function_byte_index", "function_native_bits", "function_custom_bits", "function_secondary_input", "function_ext_controls", "function_remote_player", "function_use_time", "function_active_menu", "function_dedicated_button", "function_hold_threshold", "select_index", "select_primary_weapon", "select_secondary_weapon", "select_primary_count", "select_secondary_count", "select_tickmode", "equip_sound_scanner", "equip_sound_laser", "equip_sound_knife", "equip_sound_mine", "equip_sound_gun", "equip_weapon_necklace", "equip_weapon_briefcase2", "change_definition", "change_eject_type", "change_pausechange", "change_switchto", "change_diag_loaded", "change_diag_switchto", "change_diag_gunmem", "change_diag_other", "change_diag_result", "command_float_frames", "command_from_float", "command_to_float", "command_from_int", "command_to_int", "command_output", "part_count", "part_index", "part_number", "part_frame", "part_visible", "anim_gangsta", "vis_cursor", "vis_operation", "vis_part", "vis_modeldef", "vis_hand_model", "vis_visible", "vis_weapon", "vis_ammo_slot", "vis_ammo", "vis_ammo_flags", "vis_clip_size", "vis_loaded", "vis_commands", "vis_weapon_id", "vis_command_part", "switch_hand_index", "switch_reset_bit", "switch_matmot3", "switch_angle_damper", "switch_smoke_point", "switch_upgrades", "switch_hand_upgrades", "switch_reset_weapon", "switch_hand_weapon", "switch_audio_handle", "sw_player_num", "sw_pending", "sw_frame60", "sw_frame", "sw_diag_right", "sw_diag_left", "sw_weapon", "sw_old_weapon", "sw_left_inuse", "sw_right_inuse", "sw_dual", "sw_player_dual", "sw_prev_weapon", "sw_prev_dual", "sw_current_aim", "sw_matmot", "sw_reserve", "sw_reserve_target", "sw_zoom_max", "sw_zoom_time", "sw_fov_new", "sw_fov", "sw_current_invert", "sw_use_time", "sw_fader", "sw_player_chr", "sw_chr", "sw_current_disguised", "sw_chr_hidden", "sw_stage", "sw_current_string", "sw_string_timer", "sw_throwing", "sw_none", "sw_unarmed", "sw_psychosis", "sw_scanner", "sw_rcp", "sw_remote", "sw_disguise_a", "sw_disguise_b", "sw_rescue", "config_ammo_type", "config_ammo_clip", "config_ctrl_ammo", "config_upgrade0", "config_upgrade1", "config_final0", "config_final1", "config_hand_abmag", "config_ctrl_abmag", "config_lastmag", "config_first_ammo", "abmag_loaded", "abmag_change", "abmag_ref", "abmag_timer", "clean_inuse", "clean_ammo_type", "clean_reload_index", "clean_rounds", "clean_reserve", "clean_multiplayer", "clean_region", "clean_weapon", "equip_request", "equip_diag_shortcut", "equip_want_ammo", "equip_current_dual", "equip_custom_end", "init_hand", "init_anim_ref", "init_gun_anim", "init_hand_anim", "init_player_ref", "mem_current_owner", "mem_gunloadstate", "mem_lock_timer", "mem_owner_free", "mem_owner_bond", "mem_master_flux", "mem_gun_flux", "mem_request_owner", "mem_last_pair", "mem_last_wild", "mem_last_owner", "mem_wild_value", "mem_has_body", "mem_remove_body_flag", "mem_load_all", "load_lockscreen", "flux_file", "flux_catalog_head", "flux_generation", "flux_hand_model", "flux_load_ptr", "flux_load_remaining", "flux_casing_count", "flux_casing_index", "flux_casing_model", "flux_casings_active", "master_weapon", "master_hf32", "master_handfile", "master_file", "master_model", "master_cart_model", "master_mem_ptr", "master_mem_remaining", "master_has_hands_input", "master_load_generation", "cart_func", "cart_weapon", "cart_ammo", "cart_type", "cart_ammo_index", "cart_eject", "cart_player_count", "final_hand_index", "final_gun_model", "final_hand_model", "final_gun_def", "final_gun_save", "final_hand_save", "final_gun_root", "final_hand_root", "final_gun_command", "final_hand_command", "final_matrix_state", "final_matrix_ptr", "final_matrix_bytes", "final_sum", "parent_last_key", "parent_diag_hands", "gl_retained_model", "gl_target", "gl_target_model", "gl_next_texture", "gl_loaded_size", "gl_alloc_size", "gl_load_type", "gl_body_hand", "gl_load_file", "gl_provider", "gl_texture_model", "gl_texture_index", "gl_texture_count", "gl_texture_word", "gl_texture_limit", "gl_texture_mark"};
    static_assert(sizeof(names)/sizeof(names[0]) == GGV_BINDING_COUNT);
    return b >= 0 && b < GGV_BINDING_COUNT ? names[b] : nullptr;
}
extern "C" ggv_type ggvBindingType(ggv_binding b) { return b >= 0 && b < GGV_BINDING_COUNT ? types[b] : GGV_I32; }
extern "C" int ggvPrepare(const ggv_source *source, ggv_program *program, char *error, size_t cap) {
    if (!source || !program || !valid(*source)) {
        if (error && cap) std::snprintf(error,cap,"Invalid typed forward control program");
        return 0;
    }
    *program = {*source,seal,needsServices(*source),needsServices4(*source)}; return 1;
}
extern "C" const ggv_program *ggvStockProgram(ggv_stock_program p) {
    return p >= GGV_SHOOT_CONTROL && p <= GGV_GUN_TEXTURES ? &stock[p] : nullptr;
}
extern "C" int ggvExecute(const ggv_program *program, const ggv_host *host, int32_t *result) {
    if (!program || program->validated != seal || !host || !host->read || !host->write || !result
            || (program->requires_services && !host->call)
            || (program->requires_services4 && !host->call4)) return 0;
    ggv_value r[GGV_REGISTER_LIMIT] = {};
    for (unsigned pc=0; pc<program->source.count;) {
        const auto &n=program->source.nodes[pc++];
        switch(n.op) {
        case GGV_CONST: r[n.dst]=n.literal; break;
        case GGV_READ: r[n.dst]=host->read(host->context,n.binding); break;
        case GGV_WRITE: host->write(host->context,n.binding,r[n.a]); break;
        case GGV_MASK: {
            const ggi_int value{r[n.a].u,32,0};
            const ggi_int mask{n.literal.u,32,0};
            ggi_int masked{};
            if (ggi_binary(GGI_OP_AND,&value,&mask,&masked) != GGI_OK) return 0;
            r[n.dst].b=masked.bits!=0;
            break;
        }
        case GGV_I_LT: r[n.dst].b=r[n.a].i<r[n.b].i; break;
        case GGV_I_EQ: r[n.dst].b=r[n.a].i==r[n.b].i; break;
        case GGV_I_GT: r[n.dst].b=r[n.a].i>r[n.b].i; break;
        case GGV_I_LE: r[n.dst].b=r[n.a].i<=r[n.b].i; break;
        case GGV_I64_LT: r[n.dst].b=r[n.a].i64<r[n.b].i64; break;
        case GGV_I64_EQ: r[n.dst].b=r[n.a].i64==r[n.b].i64; break;
        case GGV_I_TO_I64: r[n.dst].i64=r[n.a].i; break;
        case GGV_F_LT: r[n.dst].b=r[n.a].f<r[n.b].f; break;
        case GGV_F_GT: r[n.dst].b=r[n.a].f>r[n.b].f; break;
        case GGV_F_LE: r[n.dst].b=r[n.a].f<=r[n.b].f; break;
        case GGV_F_GE: r[n.dst].b=r[n.a].f>=r[n.b].f; break;
        case GGV_NOT: r[n.dst].b=!r[n.a].b; break;
        case GGV_F_ADD_DIV_I: r[n.dst].f=r[n.a].f+r[n.b].f/r[n.c].i; break;
        case GGV_F_SUB_DIV_I: r[n.dst].f=r[n.a].f-r[n.b].f/r[n.c].i; break;
        case GGV_F_INTERPOLATE: r[n.dst].f=r[n.a].f+(r[n.b].f-r[n.a].f)*r[n.c].f; break;
        case GGV_F_RPM_CREDIT: r[n.dst].f=r[n.a].f/60.0f*(r[n.b].f/60.0f)+r[n.c].f; break;
        case GGV_F_MINUS_I: r[n.dst].f=r[n.a].f-r[n.b].i; break;
        case GGV_TRUNCATE: r[n.dst].i=r[n.a].f; break;
        case GGV_I_INCREMENT: {
            const ggi_int value{uint32_t(r[n.a].i),32,0};
            const ggi_int one{1,32,0};
            ggi_int incremented{};
            if (ggi_binary(GGI_OP_ADD_U,&value,&one,&incremented) != GGI_OK) return 0;
            r[n.dst].i=std::bit_cast<int32_t>(uint32_t(incremented.bits));
            break;
        }
        case GGV_U_AND_LITERAL: case GGV_U_OR_LITERAL: {
            const ggi_int value{r[n.a].u,32,0}, literal{n.literal.u,32,0};
            ggi_int out{};
            if(ggi_binary(n.op==GGV_U_AND_LITERAL?GGI_OP_AND:GGI_OP_OR,&value,&literal,&out)!=GGI_OK) return 0;
            r[n.dst].u=uint32_t(out.bits); break;
        }
        case GGV_I_ABS:
            if(r[n.a].i==INT32_MIN) return 0; // Outside signed native ABS defined domain.
            r[n.dst].i=r[n.a].i<0?-r[n.a].i:r[n.a].i;break;
        case GGV_I_TO_U: r[n.dst].u=uint32_t(r[n.a].i); break;
        case GGV_U_MOD_LITERAL: r[n.dst].u=r[n.a].u%n.literal.u; break;
        case GGV_U_GT: r[n.dst].b=r[n.a].u>r[n.b].u; break;
        case GGV_U_TO_I: r[n.dst].i=std::bit_cast<int32_t>(r[n.a].u); break;
        case GGV_I_ADD_WRAP: case GGV_I_SUB_WRAP: {
            const ggi_int a{uint32_t(r[n.a].i),32,0}, b{uint32_t(r[n.b].i),32,0};
            ggi_int out{};
            if(ggi_binary(n.op==GGV_I_ADD_WRAP?GGI_OP_ADD_U:GGI_OP_SUB_U,&a,&b,&out)!=GGI_OK) return 0;
            r[n.dst].i=std::bit_cast<int32_t>(uint32_t(out.bits)); break;
        }
        case GGV_I_DIV_TRUNC: {
            const int32_t numerator=r[n.a].i, denominator=r[n.b].i; int32_t out;
            if(gds_i32_div_trunc(&numerator,&denominator,&out)!=GDS_OK) return 0;
            r[n.dst].i=out; break;
        }
        case GGV_U_SHR: {
            const uint32_t value=r[n.a].u, count=r[n.b].u; uint32_t out;
            if(gds_u32_shr(&value,&count,&out)!=GDS_OK) return 0;
            r[n.dst].u=out; break;
        }
        case GGV_U_SHL: {
            const uint32_t value=r[n.a].u,count=r[n.b].u;
            if(count>=32) return 0;
            r[n.dst].u=value<<count;break;
        }
        case GGV_U_AND: case GGV_U_OR: {
            const ggi_int a{r[n.a].u,32,0}, b{r[n.b].u,32,0};ggi_int out{};
            if(ggi_binary(n.op==GGV_U_AND?GGI_OP_AND:GGI_OP_OR,&a,&b,&out)!=GGI_OK) return 0;
            r[n.dst].u=uint32_t(out.bits);break;
        }
        case GGV_U_NOT: {
            const ggi_int a{r[n.a].u,32,0};ggi_int out{};
            if(ggi_unary(GGI_OP_NOT,&a,&out)!=GGI_OK) return 0;
            r[n.dst].u=uint32_t(out.bits);break;
        }
        case GGV_I_DIV_F: r[n.dst].f=r[n.a].i/r[n.b].f; break;
        case GGV_F_MUL: r[n.dst].f=r[n.a].f*r[n.b].f; break;
        case GGV_F_SUB: r[n.dst].f=r[n.a].f-r[n.b].f; break;
        case GGV_F_ADD: r[n.dst].f=r[n.a].f+r[n.b].f; break;
        case GGV_F_BITS_I: r[n.dst].i=std::bit_cast<int32_t>(r[n.a].f); break;
        case GGV_I_TO_F: r[n.dst].f=float(r[n.a].i); break;
        case GGV_I64_TO_F: r[n.dst].f=float(r[n.a].i64); break;
        case GGV_U_TO_F: r[n.dst].f=float(r[n.a].u); break;
        case GGV_I64_TO_I32: r[n.dst].i=int32_t(r[n.a].i64); break;
        case GGV_F_COPY: r[n.dst].f=r[n.a].f; break;
        case GGV_F_EQ: r[n.dst].b=r[n.a].f==r[n.b].f; break;
        case GGV_I64_ADD: case GGV_I64_SUB: case GGV_I64_REM: {
            const int64_t a=r[n.a].i64,b=r[n.b].i64;
            if(n.op==GGV_I64_ADD) {
                if((b>0&&a>INT64_MAX-b)||(b<0&&a<INT64_MIN-b))return 0;
                r[n.dst].i64=a+b;
            } else if(n.op==GGV_I64_SUB) {
                if((b>0&&a<INT64_MIN+b)||(b<0&&a>INT64_MAX+b))return 0;
                r[n.dst].i64=a-b;
            } else {
                if(b==0||(a==INT64_MIN&&b==-1))return 0;
                r[n.dst].i64=a%b;
            }
            break;
        }
        case GGV_F_DIV: r[n.dst].f=r[n.a].f/r[n.b].f; break;
        case GGV_F_SCALE_BIAS: r[n.dst].f=r[n.a].f*r[n.b].f+r[n.c].f; break;
        // Native additive motor update: old + factor * selected-clock, in that order.
        case GGV_F_ADD_PRODUCT: r[n.dst].f=r[n.a].f+r[n.b].f*r[n.c].f; break;
        case GGV_F_BLEND: r[n.dst].f=(r[n.a].f-r[n.b].f)*r[n.c].f+r[n.b].f; break;
        case GGV_RETURN_BOOL: *result=r[n.a].b; return 1;
        case GGV_RETURN_I32: *result=r[n.a].i; return 1;
        case GGV_REF_NONNULL: r[n.dst].b=r[n.a].ref!=nullptr; break;
        case GGV_REF_EQ: r[n.dst].b=r[n.a].ref==r[n.b].ref; break;
        case GGV_CALL: {
            const auto sig=signature(n.service);
            ggv_value args[3]={}; const unsigned regs[]={n.a,n.b,n.c};
            for(int a=0;a<sig.count;++a) args[a]=r[regs[a]];
            const auto output=host->call(host->context,n.service,args[0],args[1],args[2]);
            if(sig.result>=0) r[n.dst]=output;
            break;
        }
        case GGV_CALL4: {
            const auto sig=signature4(n.service4);
            const unsigned regs[]={n.a,n.b,n.c,n.d};
            e4_signature effect{{effectType(sig.args[0]),effectType(sig.args[1]),
                effectType(sig.args[2]),effectType(sig.args[3])},0u};
            e4_value args[4]{};
            for(int a=0;a<4;++a)args[a]=effectValue(sig.args[a],r[regs[a]]);
            Effect4Context context{host,n.service4};
            if(e4_invoke(&effect,args,invokeEffect4,&context,nullptr)!=0u) return 0;
            break;
        }
        case GGV_BRANCH_FALSE: if(!r[n.a].b) pc=n.target; break;
        case GGV_REPEAT_WHILE: if(r[n.a].b) pc=n.target; break;
        case GGV_JUMP: pc=n.target; break;
        case GGV_RETURN: *result=n.literal.i; return 1;
        }
    }
    return 0; // unreachable for an immutable prepared program
}
