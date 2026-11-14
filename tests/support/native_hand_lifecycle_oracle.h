/* native_hand_lifecycle_oracle.h
 *
 * TEST-ONLY native C reference oracle for five bondgun.c hand lifecycle
 * donors. Status: UNCOMPILED, UNRUN, SOURCE_PARTIAL. Registered in pd-tests.
 * C++ differential cases exercise scripted boundaries, not native gameplay.
 *
 * Donors, complete and pinned (bondgun.c ORIGIN_SHA
 * D4F6EC8D9800424368DE88F3BF9EFCAB9282B254229F3699106552FC2E93F12B):
 *   bgunTickIncReload      line 1645, 5568 bytes, SHA256
 *     573559D60024CB688B8FEC8D4CC127FDCC50C707E0024982E52BF47D5AB52FD9
 *   bgunTickIncChangeFunc  line 1848, 736 bytes, SHA256
 *     5D758E708CC00DACA3F1D74EF65FABC08DF6D324E8E696D36576178CF194D428
 *   bgunSetArmPitch        line 1505, 262 bytes, SHA256
 *     CDE4DFE5D8E6D5A0599BE7BDB391DCB03E772CC39355B827A103EEBAEC625B98
 *   bgunIsLoaded           line 3925, 315 bytes, SHA256
 *     66D1B0E7419327DA5E419674DBC18F8491252498380F9654001DC04D33D83684
 *   bgunIsReloading        line 1636, 117 bytes, SHA256
 *     06D01B279CCE686034CCA31D54D04FAE7CF06FD72D43CAE9C4F00ED510E1C4C9
 * Entry points: bgunX -> nhloX with a leading nhlo_oracle *. The oracle never
 * calls GGV; answers come only from the transcribed native bodies. The
 * transformation list and context source identities are in the .c file.
 *
 * Fixtures are narrow accessed-field representations with native scalar
 * types (bool = s32, u8/s8 fields, u8 bitfields, f32 matrix). They are NOT
 * engine ABI, layout or lifetime substitutes.
 *
 * Usage notes
 * - Initialization: call nhloInit on every oracle (zeroes all state and
 *   responses, no hook, version NTSC_FINAL, pal 0). Zero-initialize every
 *   fixture struct before setting fields. The oracle embeds the event log;
 *   prefer static or heap storage.
 * - Copy: fixtures are plain data and may be copied by assignment to build
 *   expected states. A copy has new addresses: pointers held by the oracle
 *   (vars.currentplayer), by info, or logged in events still name originals.
 * - Compare: use nhloHandEqual/nhloPlayerEqual or field checks. Never memcmp
 *   fixtures (padding and bitfield storage are indeterminate). Floats compare
 *   by bit pattern (nhloFloatBits). Arithmetic assumes FLT_EVAL_METHOD 0.
 * - Validity: the oracle adds no validation. Every pointer or index the
 *   native path dereferences (currentplayer, func, info->definition->ammos[i],
 *   hands[1 - handnum]) must be valid fixture storage. Native integer
 *   overflow (e.g. TICKS(23) - count60) stays undefined; do not drive it.
 * - Version: version selects the VERSION >= VERSION_JPN_FINAL block and the
 *   version-dependent WEAPON_BRIEFCASE2 value (nhloWeaponBriefcase2). pal
 *   selects between the two supplied TICKS definitions of `#if PAL`; the PAL
 *   macro itself was not supplied, so it is not derived from version. Runtime
 *   selection in one compiled fixture is not per-version build validation.
 *
 * Scripted boundary protocol
 * Every native callee is a recording stub. Each call appends one event with
 * all arguments in source order (pointer identity kept; f32 also as raw
 * bits), sets event->result = response[kind], then calls hook once if set.
 * The hook may rewrite event->result and mutate any fixture state (switch
 * vars.currentplayer, edit gset.weaponfunc or func->ammoindex, change info,
 * definition, ammo or hand fields) to emulate callee effects at exactly that
 * native boundary. The stub then returns the result converted to the native
 * return type: bool results are raw s32 (never normalized), FileNum2 is
 * truncated to u16. Hooks must not re-enter oracle entry points or clear the
 * log. STORE and SELECT_OTHERHAND events are logged without a hook.
 * nest_arm_pitch / nest_is_loaded additionally run nhloSetArmPitch /
 * nhloIsLoaded after the hook (the nested result replaces the scripted
 * IsLoaded result). The default 0 matches the graph's service boundaries.
 *
 * Not covered: real setState, animation/wait, child transfer, ammo status,
 * catalog, cheat, audio, matrix or libm behaviour; RNG; callers or parent
 * tick; profiles; builds; gameplay. Operand reads with no C sequence point
 * are not an ordering contract and are not logged.
 *
 * Case support (all observable through return, fixture end state and log):
 * WAIT-to-MAIN and LOWER-to-SOUND in one call, death/setState refusal,
 * first versus later MAIN, incremental (a44 before flag test) versus plain
 * (flag test before a44), status -1 and >= 2, trigger, JPN Eyespy with
 * inhibit, every excluded sound weapon and each lazy sound gate, RAISE
 * alternatives, ChangeFunc null/busy with getter before the u8 store and the
 * fresh RIGHT unk0dd4 store after StartAnimation, IsLoaded/IsReloading, and
 * SetArmPitch mtx/cos/sin call and m[3][0..2] store order.
 */
#ifndef NATIVE_HAND_LIFECYCLE_ORACLE_H
#define NATIVE_HAND_LIFECYCLE_ORACLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* versions.h */
enum {
    NHLO_VERSION_NTSC_BETA = 0,
    NHLO_VERSION_NTSC_1_0 = 1,
    NHLO_VERSION_NTSC_FINAL = 2,
    NHLO_VERSION_PAL_BETA = 3,
    NHLO_VERSION_PAL_FINAL = 4,
    NHLO_VERSION_JPN_FINAL = 5
};

/* constants.h, bondgun.c prefix and sfx.h values used by the donors. */
enum {
    NHLO_HANDSTATE_IDLE = 0,
    NHLO_HANDSTATE_RELOAD = 1,
    NHLO_HANDSTATE_CHANGEFUNC = 7,
    NHLO_HANDSTATEMINOR_RELOAD_MAIN = 0,
    NHLO_HANDSTATEMINOR_RELOAD_LOWER = 1,
    NHLO_HANDSTATEMINOR_RELOAD_SOUND = 2,
    NHLO_HANDSTATEMINOR_RELOAD_RAISE = 3,
    NHLO_HANDSTATEMINOR_RELOAD_WAIT = 9,
    NHLO_HANDSTATEFLAG_00000010 = 0x10,
    NHLO_HANDANIMMODE_IDLE = 0,
    NHLO_HANDANIMMODE_BUSY = 2,
    NHLO_HANDMODE_NONE = 0,
    NHLO_HANDMODE_11 = 11,
    NHLO_HANDMODE_12 = 12,
    NHLO_HAND_RIGHT = 0,
    NHLO_HAND_LEFT = 1,
    NHLO_FUNC_PRIMARY = 0,
    NHLO_FUNC_SECONDARY = 1,
    NHLO_AMMOFLAG_INCREMENTALRELOAD = 4,
    NHLO_EJECTSTATE_INACTIVE = 0,
    NHLO_DEVICE_EYESPY = 0x04,
    NHLO_CAMERAMODE_THIRDPERSON = 1,
    NHLO_GUNMEMOWNER_BONDGUN = 0,
    NHLO_MASTERLOADSTATE_LOADED = 4,
    NHLO_WEAPONFLAG_00000040 = 0x40,
    NHLO_WEAPONFLAG_00000080 = 0x80,
    NHLO_SFX_RELOAD_DEFAULT = 0x804f
};

/* enum weaponnum entries before the VERSION_NTSC_1_0 conditional; see
 * nhloWeaponBriefcase2 for WEAPON_BRIEFCASE2. */
enum {
    NHLO_WEAPON_NONE = 0x00,
    NHLO_WEAPON_UNARMED = 0x01,
    NHLO_WEAPON_FALCON2 = 0x02,
    NHLO_WEAPON_SHOTGUN = 0x13,
    NHLO_WEAPON_COMBATKNIFE = 0x1a,
    NHLO_WEAPON_CROSSBOW = 0x1b,
    NHLO_WEAPON_LASER = 0x1d,
    NHLO_WEAPON_GRENADE = 0x1e,
    NHLO_WEAPON_NBOMB = 0x1f,
    NHLO_WEAPON_TIMEDMINE = 0x20,
    NHLO_WEAPON_PROXIMITYMINE = 0x21,
    NHLO_WEAPON_REMOTEMINE = 0x22,
    NHLO_WEAPON_ECMMINE = 0x2d,
    NHLO_WEAPON_COMMSRIDER = 0x35,
    NHLO_WEAPON_TRACERBUG = 0x36,
    NHLO_WEAPON_TARGETAMPLIFIER = 0x37
};

#define NHLO_MAX_PITCH 0.87252569198608f

/* Narrow fixtures. Opaque identities: guncmd and sndstate. */
struct nhlo_guncmd { uint32_t id; };
struct nhlo_sndstate;

struct nhlo_gset {
    uint8_t weaponnum;
    uint8_t unk0639;
    uint8_t unk063a;
    uint8_t weaponfunc;
};

typedef union nhlo_mtxf {
    float m[4][4];
    uint32_t l[4][4];
    int32_t unused;
} nhlo_mtxf;

struct nhlo_hand {
    struct nhlo_gset gset;
    int8_t visible;
    int32_t triggeron;      /* bool */
    int32_t count;
    int32_t count60;
    uint32_t mode;
    int32_t pausetime60;
    nhlo_mtxf posrotmtx;
    int32_t useposrot;      /* bool */
    int32_t state;
    int32_t stateminor;
    uint32_t stateflags;
    int32_t stateframes;
    int32_t statecycles;
    int32_t statelastframe;
    int32_t gs_int1;
    int32_t gs_int2;
    int32_t animload;
    int32_t animmode;
    uint8_t unk0cc8_01 : 1;
    uint8_t unk0cc8_02 : 1;
    uint8_t incrementalreloading : 1;
    uint8_t unk0cc8_04 : 3;
    uint8_t unk0cc8_07 : 1;
    uint8_t unk0cc8_08 : 1;
    uint8_t ejectstate : 4;
    uint8_t ejecttype : 3;
    uint8_t unk0d0e_07 : 1;
    int32_t unk0dd4;
};

struct nhlo_gunctrl {
    int32_t gunmemnew;
    int32_t gunmemtype;
    uint8_t masterloadstate;
    uint8_t gunmemowner;
};

struct nhlo_player {
    int32_t cameramode;
    uint32_t devicesactive;
    int32_t isdead;         /* bool */
    struct nhlo_hand hands[2];
    struct nhlo_gunctrl gunctrl;
    int32_t doautoselect;
    uint32_t devicesinhibit;
};

struct nhlo_vars {
    struct nhlo_player *currentplayer;
    int32_t lvupdate240;
};

struct nhlo_weaponfunc { int8_t ammoindex; };

struct nhlo_inventory_ammo {
    struct nhlo_guncmd *reload_animation;
    uint8_t flags;
};

struct nhlo_weapon { struct nhlo_inventory_ammo *ammos[2]; };

struct nhlo_handweaponinfo {
    int32_t weaponnum;
    struct nhlo_weapon *definition;
};

/* Event kinds. Argument slots (source order):
 * GSET_GET_WEAPON_FUNCTION  p0 gset                          -> p weaponfunc*
 * BGUN_SET_STATE            i0 handnum, i1 state             -> i bool raw
 * BGUN_START_ANIMATION      p0 cmd, i0 handnum, p1 hand
 * BGUN_0F098A44             p0 hand, i0 time                 -> i bool raw
 * BGUN_0F098DF8             i0 weaponfunc, p0 info, p1 hand,
 *                           u0 onebullet (u8), u1 checkunequipped (u8)
 * BGUN_0F098CA0             i0 arg0, p0 info, p1 hand        -> i s32
 * BGUN_SET_ARM_PITCH        p0 hand, f angle
 * BGUN_IS_LOADED            (none)                           -> i bool raw
 * SND_START                 i0 arg0, i1 sound (s16), p0 handle, i2 volume,
 *                           i3 pan, f pitch, i4 fxbus, i5 fxmix
 *                                                            -> p (discarded)
 * WEAPON_GET_FILE_NUM2      i0 weaponnum                     -> i, as u16
 * WEAPON_HAS_FLAG           i0 itemid, u0 flag               -> i bool raw
 * GSET_GET_PRI_TO_SEC_ANIM  p0 gset                          -> p guncmd*
 * GSET_GET_SEC_TO_PRI_ANIM  p0 gset                          -> p guncmd*
 * MTX4_LOAD_X_ROTATION      f radians, p0 mtx
 * COSF / SINF               f x                              -> f
 * SELECT_OTHERHAND (instrumentation, not a call): p0 otherhand,
 *                           p1 currentplayer, i0 handnum
 * STORE (instrumentation): field, object (hand, currentplayer, or
 *   &currentplayer->hands[HAND_RIGHT]), stored_bits = lvalue read back
 *   right after the store, converted to uint32_t (signed values sign
 *   extended; f32 as raw bits). */
typedef enum nhlo_event_kind {
    NHLO_CALL_GSET_GET_WEAPON_FUNCTION,
    NHLO_CALL_BGUN_SET_STATE,
    NHLO_CALL_BGUN_START_ANIMATION,
    NHLO_CALL_BGUN_0F098A44,
    NHLO_CALL_BGUN_0F098DF8,
    NHLO_CALL_BGUN_0F098CA0,
    NHLO_CALL_BGUN_SET_ARM_PITCH,
    NHLO_CALL_BGUN_IS_LOADED,
    NHLO_CALL_SND_START,
    NHLO_CALL_WEAPON_GET_FILE_NUM2,
    NHLO_CALL_WEAPON_HAS_FLAG,
    NHLO_CALL_GSET_GET_PRI_TO_SEC_ANIM,
    NHLO_CALL_GSET_GET_SEC_TO_PRI_ANIM,
    NHLO_CALL_MTX4_LOAD_X_ROTATION,
    NHLO_CALL_COSF,
    NHLO_CALL_SINF,
    NHLO_EVENT_SELECT_OTHERHAND,
    NHLO_EVENT_STORE,
    NHLO_EVENT_KIND_COUNT
} nhlo_event_kind;

typedef enum nhlo_field {
    NHLO_FIELD_NONE,
    NHLO_FIELD_ANIMMODE,
    NHLO_FIELD_ANIMLOAD,
    NHLO_FIELD_GS_INT1,
    NHLO_FIELD_GS_INT2,
    NHLO_FIELD_STATEMINOR,
    NHLO_FIELD_STATEFRAMES,
    NHLO_FIELD_STATECYCLES,
    NHLO_FIELD_STATELASTFRAME,
    NHLO_FIELD_STATEFLAGS,
    NHLO_FIELD_UNK0D0E_07,
    NHLO_FIELD_INCREMENTALRELOADING,
    NHLO_FIELD_EJECTSTATE,
    NHLO_FIELD_MODE,
    NHLO_FIELD_PAUSETIME60,
    NHLO_FIELD_COUNT60,
    NHLO_FIELD_COUNT,
    NHLO_FIELD_UNK0CC8_02,
    NHLO_FIELD_DOAUTOSELECT,
    NHLO_FIELD_WEAPONFUNC,
    NHLO_FIELD_UNK0DD4,
    NHLO_FIELD_USEPOSROT,
    NHLO_FIELD_POSROTMTX_M30,
    NHLO_FIELD_POSROTMTX_M31,
    NHLO_FIELD_POSROTMTX_M32
} nhlo_field;

typedef union nhlo_result {
    int32_t i;
    float f;
    void *p;
} nhlo_result;

typedef struct nhlo_event {
    nhlo_event_kind kind;
    nhlo_field field;
    const void *object;
    uint32_t stored_bits;
    const void *p[2];
    int32_t i[6];
    uint32_t u[2];
    float f;
    uint32_t f_bits;
    nhlo_result result;
} nhlo_event;

typedef struct nhlo_oracle nhlo_oracle;
typedef void (*nhlo_hook)(nhlo_oracle *o, nhlo_event *event, void *user);

enum { NHLO_EVENT_CAPACITY = 256 };

struct nhlo_oracle {
    struct nhlo_vars vars;            /* g_Vars */
    int32_t playerinvincible;         /* g_PlayerInvincible (bool) */
    uint32_t var80095200;
    int32_t version;                  /* NHLO_VERSION_* */
    int32_t pal;                      /* `#if PAL` choice for TICKS */
    nhlo_result response[NHLO_EVENT_KIND_COUNT];
    nhlo_hook hook;
    void *hook_user;
    int32_t nest_arm_pitch;
    int32_t nest_is_loaded;
    uint32_t count;                   /* logged events */
    uint32_t dropped;                 /* events beyond capacity */
    nhlo_event events[NHLO_EVENT_CAPACITY];
    nhlo_event scratch;
};

void nhloInit(nhlo_oracle *o);
void nhloClearLog(nhlo_oracle *o);
uint32_t nhloFloatBits(float value);
float nhloBitsFloat(uint32_t bits);
int32_t nhloWeaponBriefcase2(int32_t version);
const char *nhloEventKindName(nhlo_event_kind kind);
int nhloHandEqual(const struct nhlo_hand *a, const struct nhlo_hand *b);
int nhloPlayerEqual(const struct nhlo_player *a, const struct nhlo_player *b);

int32_t nhloTickIncReload(nhlo_oracle *o, struct nhlo_handweaponinfo *info, int32_t handnum,
        struct nhlo_hand *hand, int32_t lvupdate);
int32_t nhloTickIncChangeFunc(nhlo_oracle *o, struct nhlo_handweaponinfo *info, int32_t handnum,
        struct nhlo_hand *hand, int32_t lvupdate);
void nhloSetArmPitch(nhlo_oracle *o, struct nhlo_hand *hand, float angle);
int32_t nhloIsLoaded(nhlo_oracle *o);
int32_t nhloIsReloading(nhlo_oracle *o, struct nhlo_hand *hand);

/* bgun0f09815c scalar fixture: backend queries counted separately because
 * C does not sequence the two operands of the native subtraction. */
typedef struct nhlo_frame_case {
    int32_t animmode;
    int32_t command_present;
    int64_t command_value;
    int32_t frame_count;
    float current_frame;
    unsigned count_queries;
    unsigned current_queries;
} nhlo_frame_case;
float nhloAnimationFrame(nhlo_frame_case *state);

typedef struct nhlo_wait_command {
    uint8_t type;
    uint16_t key;
    int64_t value;
} nhlo_wait_command;
typedef struct nhlo_wait_case {
    const nhlo_wait_command *commands;
    int32_t mode, load, time, latch, pal;
    int32_t increment;
    float increment_real, frames[2];
    unsigned advances, frame_queries;
} nhlo_wait_case;
int32_t nhloAnimationWait(nhlo_wait_case *state);
int32_t nhloAnimationWaitKey(nhlo_wait_case *state);

typedef struct nhlo_start_command {
    uint8_t type;
    uint16_t key;
    int eligible;
    struct nhlo_start_command *child;
} nhlo_start_command;
enum {
    NHLO_START_NAME, NHLO_START_SOURCE, NHLO_START_LOG_NULL,
    NHLO_START_LOG_SOURCE, NHLO_START_RNG, NHLO_START_ELIGIBILITY,
    NHLO_START_CHILD, NHLO_START_STORE_LOAD, NHLO_START_STORE_MODE,
    NHLO_START_STORE_LATCH, NHLO_START_STORE_INCREMENTAL, NHLO_START_STORE_COMMAND,
    NHLO_START_STORE_LOOP, NHLO_START_STORE_BIT02, NHLO_START_STORE_BIT07,
    NHLO_START_STORE_PREVIOUS
};
typedef struct nhlo_start_event { int kind; const nhlo_start_command *command; } nhlo_start_event;
typedef struct nhlo_start_case {
    int name_present, source_present;
    uint32_t rng_value, rng_calls;
    int32_t load, mode, latch, incremental, loop, bit02, bit07;
    nhlo_start_command *command, *previous;
    unsigned count;
    nhlo_start_event events[256];
} nhlo_start_case;
void nhloAnimationStart(nhlo_start_case *state, nhlo_start_command *command);

typedef struct nhlo_tick_hand {
    int32_t state, frames, cycles, lastframe, count60, count;
    int32_t firing, flash, useposrot;
} nhlo_tick_hand;
typedef struct nhlo_tick_case {
    nhlo_tick_hand hands[2];
    int current, pending, pending_flip, is_v2;
    int32_t update, update60, update240, child_result, child_state;
    unsigned calls;
    int events[16], players[16];
} nhlo_tick_case;
int32_t nhloHandTick(nhlo_tick_case *state);

typedef struct nhlo_tick_frame_case {
    int modern, pal;
    int32_t update60, increment, update_after_info;
    uint32_t increment_legacy;
    float increment_real, speed;
    unsigned info_calls, speed_calls, child_calls;
    int32_t child_returns[21], child_inputs[21];
} nhlo_tick_frame_case;
void nhloTickHand(nhlo_tick_frame_case *state);

typedef struct nhlo_auto_hand {
    struct nhlo_gset gset;
    int8_t inuse, lastdirvalid;
    int32_t state, stateminor, stateframes, animframeinc, count, count60, pausetime60;
    uint32_t animframeincfreal, mode, modenext;
    float shotremainder;
    int32_t burstbullets, triggeron, triggerreleased, activatesecondary;
    uint8_t unk0cc8_01 : 1;
    uint8_t unk0cc8_07 : 1;
    uint8_t unk0cc8_08 : 1;
} nhlo_auto_hand;
typedef struct nhlo_auto_other { int inuse, state, function, ammo[2], excluded; } nhlo_auto_other;
enum { NHLO_AUTO_FUNCTION, NHLO_AUTO_SETSTATE, NHLO_AUTO_READY, NHLO_AUTO_PITCH,
    NHLO_AUTO_DELETE, NHLO_AUTO_FREE, NHLO_AUTO_AMMO, NHLO_AUTO_CAPTURE,
    NHLO_AUTO_OTHER_AMMO, NHLO_AUTO_EXCLUDED, NHLO_AUTO_SWITCH };
typedef struct nhlo_auto_event { int kind, arg, player; float pitch; } nhlo_auto_event;
typedef struct nhlo_auto_case {
    nhlo_auto_hand hand;
    nhlo_auto_other other[2];
    int modern, pal, normal_multi, multi, gunfunc, ready, ammo, autoselect;
    int weaponnum, handnum, lvupdate, current, flip_capture, flip_switch;
    unsigned accept_states, count;
    nhlo_auto_event events[64];
} nhlo_auto_case;
int32_t nhloAutoSwitch(nhlo_auto_case *state);

enum { NHLO_IDLE_FUNCTION, NHLO_IDLE_READY, NHLO_IDLE_SETSTATE, NHLO_IDLE_AMMO,
    NHLO_IDLE_IS_V2, NHLO_IDLE_V2, NHLO_IDLE_FLAG, NHLO_IDLE_STORED,
    NHLO_IDLE_EXCLUDED, NHLO_IDLE_CAPTURE_FUNCTION, NHLO_IDLE_SWITCH };
typedef struct nhlo_idle_case {
    nhlo_auto_hand hand, other[2];
    int modern, gunfunc, ready, is_v2, v2_idle, stored, excluded, wantammo;
    int weaponnum, handnum, lvupdate, ammo[2], current, flip_v2, flip_switch;
    int autoselect[2];
    uint32_t flags, function_type;
    unsigned accept_states, count;
    nhlo_auto_event events[64];
} nhlo_idle_case;
int32_t nhloIdle(nhlo_idle_case *state);

typedef struct nhlo_ready_case {
    int handnum, range_valid, range_weapon, right_weapon, throwing;
    int left_inuse, left_state, left_minor, pending_weapon, right_state, inuse, dualwield;
    int flip_throwing, slot_calls, weapon_calls;
} nhlo_ready_case;
int nhloReadyToSwitch(nhlo_ready_case *state);
int nhloUsingSecondary(int stored, int invert);

#ifdef __cplusplus
}
#endif

#endif
