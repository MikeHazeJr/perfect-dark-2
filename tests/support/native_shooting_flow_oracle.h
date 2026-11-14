#ifndef PD_NATIVE_SHOOTING_FLOW_ORACLE_H
#define PD_NATIVE_SHOOTING_FLOW_ORACLE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Independent native-C fixture types cover only the accessed fields, with
 * original scalar storage. This is not a native ABI/layout substitute. */
enum { NATIVE_SHOOTING_SCRIPT_LIMIT=8, NATIVE_SHOOTING_AUDIO_LIMIT=8,
       NATIVE_SHOOTING_TRACE_LIMIT=256 };
typedef enum {
    NS_WRITE_SPOOL, NS_WRITE_STATE_FRAMES, NS_WRITE_SHOTS, NS_WRITE_REMAINDER,
    NS_WRITE_FIRING, NS_WRITE_BURST, NS_WRITE_FLASH, NS_WRITE_LOAD_SLIDE,
    NS_WRITE_STATE_VAR1, NS_WRITE_STATE_FLAGS, NS_WRITE_LOADED_AMMO,
    NS_WRITE_ATTACK_TYPE, NS_WRITE_ALLOW_FRAME, NS_WRITE_LAST_FRAME,
    NS_WRITE_SLIDEINC
} native_shooting_write;
typedef enum {
    NS_RESOLVE_GRAPH, NS_OVERRIDE_ON, NS_OVERRIDE_OFF, NS_START_SLIDE,
    NS_RUMBLE, NS_FIRESLOT_DURATION, NS_SINGLE_SOUND, NS_CURRENT_PRIORITY,
    NS_AUDIO_PRIORITY, NS_SET_PRIORITY, NS_SOUND_STATE, NS_SOUND_STOP,
    NS_SOUND_START, NS_SOUND_EVENT, NS_CLOCK,
    NS_JOY_PADS, NS_CONTROL_MODE, NS_PAK_TYPE, NS_WEAPON_FLAG, NS_PAK_RUMBLE
} native_shooting_service;
enum { NS_EVENT_WRITE=1, NS_EVENT_SERVICE=2 };
/* Snapshot is recorded AFTER an actual source store or service callback.
 * Float values are their copied bit representations, not widened math.
 * subject distinguishes passed-hand writes from current-player hand services.
 * Stable fixture audio tokens are not native addresses or serialized refs. */
typedef struct {
    int32_t kind, operation, subject, a, b, c, result;
    int32_t stateframes, shots, burstbullets, loadedammo, loadedammo1;
    int32_t statevar1, attacktype, allowshootframe, lastshootframe;
    uint32_t stateflags, spool_bits, remainder_bits, loadslide_bits;
    int8_t firing, flashon;
    int32_t current_hand_slideinc[2], current_hand_lastshootframe[2];
    int32_t audio2, audio3, priority;
    int32_t audio_state[NATIVE_SHOOTING_AUDIO_LIMIT];
} native_shooting_event;
typedef struct {
    uint32_t flags, stateflags;
    int32_t ammoindex, functype, automatic, turretaccel, turretdecel;
    int32_t burstbullets, stateframes, shots, loadedammo, ammotype;
    int32_t trigger, aim, shotgun, laser, shotgundelay;
    float spool, remainder, initialrpm, maxrpm, clock;
    int8_t firing;
    int32_t clock_reads, override_calls;
    int32_t graph_present, graph_initial, graph_accel, graph_decel;
    uint32_t legacy_flags;
    int32_t legacy_accel, legacy_decel, loadedammo1, ammotype1;
    float legacy_initialrpm, legacy_maxrpm;
    /* Appended full-body fields leave the old prefix fixture API intact. */
    int32_t graph_ammo_slot, graph_has_type, graph_function_type;
    int32_t legacy_ammoindex, legacy_functype;
    int32_t legacy_function_absent; /* FULL fixtures only; prefix always has func. */
    int32_t handnum, passed_is_current, info_weaponnum, mauler, ge_flashes;
    int8_t flashon;
    float loadslide, matmot;
    int32_t statevar1, attacktype, frame60, allowshootframe, lastshootframe;
    int32_t other_lastshootframe, current_hand_slideinc[2];
    int32_t current_hand_lastshootframe[2];
    int32_t fireslot_script[NATIVE_SHOOTING_SCRIPT_LIMIT], fireslot_count, fireslot_index;
    int32_t sound_script[NATIVE_SHOOTING_SCRIPT_LIMIT], sound_count, sound_index;
    int32_t fireslot_default, sound_default;
    int32_t priority, audio_priority;
    /* Zero means null. Positive audio tokens are 1..AUDIO_LIMIT. */
    int32_t audio2, audio3, audio_state[NATIVE_SHOOTING_AUDIO_LIMIT];
    int32_t start_slot_token, start_return_token, start_state;
    int32_t last_pitch_bits, last_pitch_token, last_pitch_event;
    /* Full wrappers initialize these to 2/4 once per fixture. Native static
     * storage is mechanically lifted only for isolated oracle invocations. */
    uint32_t rontime_state, rofftime_state, override_on_value, override_off_value;
    int32_t overrides_initialized, override_on_enabled, override_off_enabled;
    int32_t trace_count, trace_overflow;
    int32_t program_refused;
    native_shooting_event trace[NATIVE_SHOOTING_TRACE_LIMIT];
    /* V4 direct hand-function fixtures. Prior full/prefix entry points keep
     * their frozen V3 leaf boundaries; these fields do not alter those APIs.
     * Joy playernum and pak type arguments narrow through actual s8 APIs;
     * cached pad outputs and the >=NTSC_1_0 pulse pad remain int32. */
    int32_t current_playernum, current_mpindex, controlmode, contpad1, contpad2;
    int32_t paktype1, paktype2, weapon_dualwield, passed_slideinc;
    int32_t joy_pad_queries, control_mode_queries, pak_type_queries;
    int32_t weapon_flag_queries, rumble_pulses;
    /* Per-call scripted pak results disambiguate aliased/changing devices.
     * Otherwise the fixture uses the first matching narrowed pad identity. */
    int32_t paktype_script[NATIVE_SHOOTING_SCRIPT_LIMIT], paktype_count, paktype_index;
} native_shooting_case;
int32_t nativeShootingControl(native_shooting_case *);
int32_t nativeShootingCredit(native_shooting_case *);
int32_t nativeGraphShootingControl(native_shooting_case *);
int32_t nativeGraphShootingCredit(native_shooting_case *);
int32_t nativeShootingFullControl(native_shooting_case *);
int32_t nativeShootingFullCommit(native_shooting_case *);
int32_t nativeGraphShootingFullControl(native_shooting_case *);
int32_t nativeGraphShootingFullCommit(native_shooting_case *);
/* Complete source-derived native and actual-C-connected hand entries. All
 * use handnum/info_weaponnum; slide targets currentplayer hands, even when
 * the fixture's passed hand is separate. These wrappers return no counter. */
void nativeShootingRumble(native_shooting_case *);
void nativeShootingSlide(native_shooting_case *);
void nativeGraphShootingRumble(native_shooting_case *);
void nativeGraphShootingSlide(native_shooting_case *);
/* Prepared-host fixtures may record their OWN actual stores/services in the
 * same neutral schema, after mutation. These helpers execute no gameplay. */
void nativeShootingRecordEvent(native_shooting_case *, int32_t kind,
    int32_t operation, int32_t subject, int32_t a, int32_t b, int32_t c, int32_t result);
void nativeShootingResetTrace(native_shooting_case *);
/* V5 is an appended independent attack/recovery fixture namespace. The old
 * shooting fixture/source/API epochs retain their previous meaning. */
enum { NATIVE_SHOOTING_ATTACK_TRACE_LIMIT=256 };
typedef enum {
    NTA_WRITE_STATE_FLAGS, NTA_WRITE_STATE_VAR1,
    NTA_WRITE_ROTX_START, NTA_WRITE_ROTX_END, NTA_WRITE_ROTX_OFFSET,
    NTA_WRITE_POS_END_X, NTA_WRITE_POS_END_Y, NTA_WRITE_POS_END_Z,
    NTA_WRITE_POS_START_X, NTA_WRITE_POS_START_Y, NTA_WRITE_POS_START_Z,
    NTA_WRITE_POS_OFFSET_X, NTA_WRITE_POS_OFFSET_Y, NTA_WRITE_POS_OFFSET_Z,
    NTA_WRITE_USE_POS_ROT, NTA_WRITE_SPOOL, NTA_WRITE_ANIMATION_LATCH,
    NTA_WRITE_BURST, NTA_WRITE_STATE_MINOR, NTA_WRITE_MATMOT2, NTA_WRITE_MATMOT1
} native_shooting_attack_write;
typedef enum {
    NTA_RESOLVE_GRAPH, NTA_RESOLVE_FUNCTION, NTA_OVERRIDE_GKEF,
    NTA_SIN, NTA_COS, NTA_TWEEN, NTA_AIM_X,
    NTA_MATRIX_X, NTA_MATRIX_TRANSLATE, NTA_MATRIX_IDENTITY,
    NTA_START_ANIMATION, NTA_ANIMATION_READY, NTA_VIBRATION_MIX,
    NTA_VIBRATION_MULTIPLY, NTA_CONTROL, NTA_COMMIT, NTA_RECOVERY
} native_shooting_attack_service;
enum { NTA_EVENT_WRITE=1, NTA_EVENT_SERVICE=2,
       NTA_EVENT_SHOOTING_WRITE=3, NTA_EVENT_SHOOTING_SERVICE=4 };
typedef struct {
    int32_t stateframes,statevar1,burstbullets,stateminor,statecycles;
    uint32_t stateflags,spool_bits,matmot1_bits,matmot2_bits;
    int32_t triggerreleased,animation_latch,useposrot,animmode;
    uint32_t rotx_bits[3],posstart_bits[3],posend_bits[3],posoffset_bits[3];
    uint32_t posrotmtx_bits[16],upgrademult_bits[12],finalmult_bits[12];
    /* Nested complete shooting calls retain their original scalar snapshot. */
    native_shooting_event shooting;
} native_shooting_attack_snapshot;
typedef struct {
    int32_t kind,operation,subject,scalar[4];
    uint32_t input0_count,input1_count,output_count;
    uint32_t input0_bits[16],input1_bits[16],output_bits[16];
    native_shooting_attack_snapshot state;
} native_shooting_attack_event;
typedef struct {
    native_shooting_case shooting;
    int32_t weaponnum,stateminor,statecycles,triggerreleased,animation_latch,animmode;
    float matmot2,rotxstart,rotxend,rotxoffset;
    float posstart[3],posend[3],posoffset[3],aimpos[3],posrotmtx[16];
    int32_t useposrot;
    int32_t graph_has_unk24,graph_has_unk25,graph_has_unk26,graph_has_unk27;
    int32_t graph_unk24,graph_unk25,graph_unk26,graph_unk27;
    int32_t graph_has_recovery,graph_recovery;
    int32_t graph_has_recoildist,graph_has_recoilangle;
    float graph_recoildist,graph_recoilangle;
    /* Legacy values are narrowed through native signed8 storage on import. */
    int32_t legacy_unk24,legacy_unk25,legacy_unk26,legacy_unk27,legacy_recovery;
    float legacy_recoildist,legacy_recoilangle,definition_posz;
    int32_t function_absent,fire_animation_present,vibration_start_present,vibration_max_present;
    /* Vibration leaves consume only coefficients0,1. Native finalmult[2..11]
     * stays unchanged; the initialized full array remains a tail-canary witness. */
    float vibration_start[12],vibration_max[12],upgrademult[12],finalmult[12];
    /* Explicit narrow engine leaf responses. They are not native engine proof. */
    float sin_result,cos_result,tween_result,aim_x_result;
    /* Translation consumes result[12..14] only and preserves the other13
     * initialized matrix cells. Full before/after16 traces are observations,
     * not a sixteen-store footprint; supplied numeric results remain mocked. */
    float matrix_x_result[16],matrix_translation_result[16],matrix_identity_result[16];
    /* Supplied arithmetic remains mocked: only result[0..1] is consumed.
     * Result tails are unused; no native scratch[2..11] is read or traced. */
    float vibration_mix_result[12],vibration_multiply_result[12];
    int32_t animation_ready_result;
    uint32_t gkef_state,gkef_override_value;
    int32_t gkef_initialized,gkef_override_enabled;
    int32_t trace_count,trace_overflow,program_refused;
    native_shooting_attack_event trace[NATIVE_SHOOTING_ATTACK_TRACE_LIMIT];
} native_shooting_attack_case;
int32_t nativeShootingRecovery(native_shooting_attack_case *);
int32_t nativeShootingAttack(native_shooting_attack_case *);
int32_t nativeGraphShootingRecovery(native_shooting_attack_case *);
int32_t nativeGraphShootingAttack(native_shooting_attack_case *);
/* Pure trace helpers: immediate copies of initialized scalars/float witnesses
 * and post-effect fixture state, no policy, pointer persistence or gameplay. */
void nativeShootingAttackResetTrace(native_shooting_attack_case *);
void nativeShootingAttackRecordEvent(native_shooting_attack_case *,int32_t kind,
    int32_t operation,int32_t subject,const int32_t scalar[4],
    const float *input0,uint32_t input0_count,const float *input1,uint32_t input1_count,
    const float *output,uint32_t output_count);
void nativeShootingAttackForwardShootingTrace(native_shooting_attack_case *,int32_t start_index);
/* Complete source-derived throw entries; separate fixture namespace.
 * Parameters import through original u8/s8/s16/BOOL32/one-bit storage.
 * Engine services are narrow fixtures; no native ABI/callee proof implied. */
enum { NATIVE_THROW_TRACE_LIMIT=128 };
typedef enum {
    NT_WRITE_MINOR, NT_WRITE_LATCH, NT_WRITE_FIRING, NT_WRITE_ATTACK_TYPE,
    NT_WRITE_LOADED_AMMO, NT_WRITE_PRIME, NT_WRITE_CURRENT_THROWING
} native_throw_write;
typedef enum {
    NT_RESOLVE_FUNCTION, NT_RESOLVE_GRAPH, NT_REMOVE_ITEM,
    NT_SWITCH_PREVIOUS, NT_AUTO_SWITCH, NT_START_ANIMATION,
    NT_READY, NT_RESET_ANIMATION, NT_SECONDARY
} native_throw_service;
enum { NT_EVENT_WRITE=1, NT_EVENT_SERVICE=2 };
typedef struct {
    int32_t weaponnum,weaponfunc,minor,cycles,frames,prime;
    int32_t trigger,released,latch,firing,attacktype,loadedammo[2];
    int32_t current_player_index,current_player_throwing[2];
} native_throw_snapshot;
typedef struct {
    int32_t kind,operation,subject,scalar[4];
    native_throw_snapshot state;
} native_throw_event;
typedef struct {
    int32_t handnum,weaponnum,weaponfunc,minor,cycles,frames,prime;
    int32_t trigger,released,latch,firing,attacktype,loadedammo[2];
    int32_t current_player_index,current_player_throwing[2];
    int32_t function_absent,graph_present,fire_animation_present;
    uint32_t graph_flags,legacy_flags;
    int32_t graph_ammo_slot,legacy_ammo_index;
    int32_t graph_has_activation,graph_activation,legacy_activation;
    int32_t graph_has_recovery,graph_recovery,legacy_recovery;
    /* Raw supplied BOOL32 ready response; secondary remains raw and native
     * source compares it exactly with true1. Other service effects are inert,
     * except reset clears the modeled latch; its other fields are unqualified. */
    int32_t ready_result,secondary_result;
    int32_t trace_count,trace_overflow,program_refused;
    native_throw_event trace[NATIVE_THROW_TRACE_LIMIT];
} native_throw_case;
int32_t nativeThrowControl(native_throw_case *);
int32_t nativeGraphThrowControl(native_throw_case *);
/* Pure post-effect observation helpers for independently authored hosts.
 * Subject is selected clip/current-player index, otherwise -1 for passed hand.
 * Scalars are {a,b,c,result}; references never escape as serialized addresses. */
void nativeThrowResetTrace(native_throw_case *);
void nativeThrowRecordEvent(native_throw_case *,int32_t kind,int32_t operation,
    int32_t subject,const int32_t scalar[4]);
/* Complete source-derived Melee and MinClip entries; no native ABI proof. */
enum { NATIVE_MELEE_TRACE_LIMIT=128, NATIVE_MELEE_READY_LIMIT=8 };
typedef enum {
    NM_WRITE_MOTOR, NM_WRITE_BURST, NM_WRITE_FIRING, NM_WRITE_ATTACK_TYPE,
    NM_WRITE_MINOR, NM_WRITE_LATCH, NM_WRITE_LOADED_AMMO
} native_melee_write;
typedef enum {
    NM_RESOLVE_FUNCTION, NM_RESOLVE_GRAPH, NM_START_ANIMATION, NM_READY,
    NM_MIN_CLIP, NM_CLOCK, NM_CHEAT_ACTIVE, NM_ANIM_BUSY
} native_melee_service;
enum { NM_EVENT_WRITE=1, NM_EVENT_SERVICE=2 };
typedef struct {
    int32_t weaponnum,weaponfunc,minor,cycles,frames,burst;
    int32_t trigger,released,latch,firing,attacktype,loadedammo[2],animmode;
    uint32_t motor_bits;
} native_melee_snapshot;
typedef struct {
    int32_t kind,operation,subject,scalar[4];
    native_melee_snapshot state;
} native_melee_event;
typedef struct {
    int32_t handnum,weaponnum,weaponfunc,minor,cycles,frames,burst;
    int32_t trigger,released,latch,firing,attacktype,loadedammo[2],animmode;
    float motor,clock60f,clock60freal;
    int32_t function_absent,graph_present,fire_animation_present;
    int32_t graph_ammo_slot,legacy_ammo_index;
    /* Supplied raw BOOL32 leaves; ready sequence may span successive calls.
     * Neither leaf substitutes a whole native gameplay body or utility. */
    int32_t ready_default,ready_values[NATIVE_MELEE_READY_LIMIT];
    int32_t ready_count,ready_cursor,cheat_result;
    int32_t trace_count,trace_overflow,program_refused;
    native_melee_event trace[NATIVE_MELEE_TRACE_LIMIT];
} native_melee_case;
typedef struct {
    int32_t weaponnum,funcnum,result,program_refused;
} native_minclip_case;
int32_t nativeMeleeControl(native_melee_case *);
int32_t nativeGraphMeleeControl(native_melee_case *);
int32_t nativeMinClipControl(native_minclip_case *);
int32_t nativeGraphMinClipControl(native_minclip_case *);
/* Pure observation helpers: subject is selected clip, otherwise passed hand -1.
 * Scalar arguments are a,b,c,result; float results retain their exact bits. */
void nativeMeleeResetTrace(native_melee_case *);
void nativeMeleeRecordEvent(native_melee_case *,int32_t kind,int32_t operation,
    int32_t subject,const int32_t scalar[4]);
/* Full Special/Empty source bodies with explicitly bounded engine leaves. */
enum { NATIVE_SE_TRACE_LIMIT=128, NATIVE_SE_SOUND_LIMIT=4 };
typedef enum { NSE_WRITE_MINOR,NSE_WRITE_FIRING,NSE_WRITE_ATTACK_TYPE,
    NSE_WRITE_LOADED_AMMO,NSE_WRITE_FRAMES,NSE_WRITE_FLAGS,NSE_WRITE_MODE,
    NSE_WRITE_COUNT60,NSE_WRITE_COUNT } native_se_write;
typedef enum { NSE_RESOLVE_FUNCTION,NSE_RESOLVE_GRAPH,NSE_RESET_ANIMATION,
    NSE_START_ANIMATION,NSE_READY,NSE_CURRENT_PRIORITY,NSE_AUDIO_PRIORITY,
    NSE_SET_PRIORITY,NSE_SOUND_START,NSE_SOUND_EVENT,NSE_SET_STATE } native_se_service;
enum { NSE_EVENT_WRITE=1,NSE_EVENT_SERVICE=2 };
typedef struct {
    int32_t weaponnum,weaponfunc,state,minor,frames,cycles,lastframe;
    int32_t trigger,firing,attacktype,loadedammo[2],animmode,latch,count60,count;
    uint32_t flags,mode;
} native_se_hand_state;
typedef struct {
    native_se_hand_state passed,current_hands[2];
    int32_t info_weapon,priority;
} native_se_snapshot;
typedef struct {
    int32_t kind,operation,subject,scalar[10];
    native_se_snapshot state;
} native_se_event;
typedef struct {
    native_se_hand_state passed,current_hands[2];
    /* -1 detached,0/1 aliases that current hand. Passed state wins on import. */
    int32_t passed_hand_index,handnum,info_weapon,definition_present,lvupdate;
    int32_t function_absent,graph_present,fire_animation_present;
    int32_t graph_ammo_slot,legacy_ammo_index;
    int32_t graph_has_special,graph_special,legacy_special;
    int32_t graph_has_recovery,graph_recovery,legacy_recovery;
    int32_t ready_result,setstate_result;
    int32_t priority,audio_priority;uint32_t audio_bank;
    /* Each sound token0=NULL;1..4 borrowed fixture handle identity. */
    int32_t sound_tokens[NATIVE_SE_SOUND_LIMIT],sound_cursor;
    int32_t trace_count,trace_overflow,program_refused;
    native_se_event trace[NATIVE_SE_TRACE_LIMIT];
} native_special_empty_case;
int32_t nativeSpecialControl(native_special_empty_case *);
int32_t nativeGraphSpecialControl(native_special_empty_case *);
int32_t nativeEmptyControl(native_special_empty_case *);
int32_t nativeGraphEmptyControl(native_special_empty_case *);
void nativeSpecialEmptyResetTrace(native_special_empty_case *);
/* Subject: clip for ammo, target current hand for SetState, otherwise -1.
 * Scalars0..3=a,b,c,result for ordinary services. SoundStart: bank,signed16
 * sound,output-ref-present,volume,pan,pitch-bits,fxbus,fxmix,token,0. */
void nativeSpecialEmptyRecordEvent(native_special_empty_case *,int32_t kind,
    int32_t operation,int32_t subject,const int32_t scalar[10]);
/* Complete Dispatcher/SetState bodies, with typed complete nested oracles.
 * Fixtures are large: qualification callers should allocate them on the heap.
 * This is source correspondence, not native layout/address/lifetime proof. */
enum { NATIVE_DISPATCH_TRACE_LIMIT=512 };
typedef struct {
    native_se_hand_state base;
    int32_t inuse,prime,burst,released,slideinc,lastshootframe;
    float motor;
} native_dispatch_hand_state;
typedef enum { ND_WRITE_FUNCTION,ND_WRITE_STATE,ND_WRITE_FRAMES,ND_WRITE_FLAGS,
    ND_WRITE_CYCLES,ND_WRITE_MINOR,ND_WRITE_LAST_FRAME } native_dispatch_write;
typedef enum { ND_RESOLVE_GRAPH,ND_RESOLVE_FUNCTION,ND_ALTERNATE_FUNCTION,
    ND_LOG,ND_NESTED_BEGIN,ND_NESTED_END } native_dispatch_service;
enum { ND_EVENT_WRITE=1,ND_EVENT_SERVICE=2,ND_CHILD_SHOOT=3,
    ND_CHILD_THROW=4,ND_CHILD_MELEE=5,ND_CHILD_SPECIAL=6 };
typedef struct {
    native_dispatch_hand_state passed,players[2][2];
    int32_t current_player_index,current_playernum,info_weapon,players_throwing[2];
} native_dispatch_snapshot;
typedef struct {
    int32_t kind,operation,subject,scalar[8],has_top_snapshot;
    /* Child records carry their actual immediate typed child snapshot.
     * They never attach a misleading post-call top-level snapshot. */
    native_dispatch_snapshot state;
    union {
        native_shooting_attack_event shoot;
        native_throw_event throwing;
        native_melee_event melee;
        native_se_event special;
    } child;
} native_dispatch_event;
typedef struct {
    native_dispatch_hand_state passed,players[2][2];
    int32_t current_player_index,current_playernum,players_throwing[2];
    /* -1 detached passed hand,otherwise player0/1 with hand0/1.
     * Passed state wins when importing an alias. */
    int32_t passed_player_index,passed_hand_index,handnum;
    int32_t info_weapon,definition_present,lvupdate,state_request;
    int32_t graph_present,graph_has_type,graph_type,function_absent,legacy_type;
    int32_t alternate_present;
    /* -1 leaves globals unchanged;0/1 switches currentplayer during lookup.
     * Selected hand stays cached. Number override flag makes later logging
     * lookup-time mutation explicit, without pretending real provider proof. */
    int32_t alternate_switch_player,alternate_override_playernum,alternate_playernum;
    native_shooting_attack_case shoot;
    native_throw_case throwing;
    native_melee_case melee;
    native_special_empty_case special;
    int32_t trace_count,trace_overflow,program_refused;
    native_dispatch_event trace[NATIVE_DISPATCH_TRACE_LIMIT];
} native_attack_dispatch_case;
int32_t nativeAttackDispatch(native_attack_dispatch_case *);
int32_t nativeGraphAttackDispatch(native_attack_dispatch_case *);
int32_t nativeStateControl(native_attack_dispatch_case *);
int32_t nativeGraphStateControl(native_attack_dispatch_case *);
void nativeAttackDispatchResetTrace(native_attack_dispatch_case *);
void nativeAttackDispatchRecordEvent(native_attack_dispatch_case *,int32_t kind,
    int32_t operation,int32_t subject,const int32_t scalar[8]);
/* Separate full Reset/Busy/Trigger fixture. Earlier partial reset mocks stay
 * historical. Named field widths are modeled; native packed-byte ABI is not. */
enum { NATIVE_AI_TRACE_LIMIT=64,NATIVE_AI_COMMAND_LIMIT=4 };
typedef struct {
    int32_t animload,animmode,latch,incremental,command_token,loopcount,bit02,bit07;
    int32_t neighbor04,neighbor07,neighbor08,ejectstate,ejecttype;
    int32_t triggeron,triggerprev,triggerreleased;
} native_animation_hand_state;
typedef enum { NAI_WRITE_LOAD,NAI_WRITE_MODE,NAI_WRITE_LATCH,NAI_WRITE_INCREMENTAL,
    NAI_WRITE_COMMAND,NAI_WRITE_LOOP,NAI_WRITE_BIT02,NAI_WRITE_BIT07,
    NAI_WRITE_PREVIOUS,NAI_WRITE_ON,NAI_WRITE_RELEASED } native_animation_write;
enum { NAI_EVENT_WRITE=1,NAI_EVENT_INPUT=2 };
typedef struct {
    int32_t kind,operation,subject,scalar[4];
    native_animation_hand_state passed,players[2][2];
    int32_t current_player_index;
} native_animation_event;
typedef struct {
    native_animation_hand_state passed,players[2][2];
    int32_t current_player_index,passed_player_index,passed_hand_index,handnum,raw_on;
    /* Synthetic engine leaf outcomes, distinct from real GraphV2 behavior.
     * Switch-1 leaves globals alone;0/1 switches after input arguments read. */
    int32_t input_override_release,input_release_value,input_switch_player;
    int32_t trace_count,trace_overflow,program_refused;
    native_animation_event trace[NATIVE_AI_TRACE_LIMIT];
} native_animation_input_case;
void nativeAnimationReset(native_animation_input_case *);
void nativeGraphAnimationReset(native_animation_input_case *);
int32_t nativeAnimationBusy(native_animation_input_case *);
int32_t nativeGraphAnimationBusy(native_animation_input_case *);
void nativeTriggerInput(native_animation_input_case *);
void nativeGraphTriggerInput(native_animation_input_case *);
void nativeAnimationInputResetTrace(native_animation_input_case *);
/* Subject-1 detached or player*2+hand; input scalars=player,hand,pressed,released.
 * No command/player numeric token is a serialized native address. */
void nativeAnimationInputRecordEvent(native_animation_input_case *,int32_t kind,
    int32_t operation,int32_t subject,const int32_t scalar[4]);
/* Complete Ammo4 bodies and actual adapters. Source-only independent fixture;
 * named widths are modeled, not native object layout/provider lifetime. */
enum { NATIVE_AMMO_TRACE_LIMIT=128,NATIVE_AMMO_FLAG_LIMIT=8,NATIVE_AMMO_TYPES=33 };
typedef struct { int32_t weaponnum,inuse,loadedammo[2]; } native_ammo_hand_state;
typedef struct {
    int32_t reserve[NATIVE_AMMO_TYPES],equipped_type[2];
    native_ammo_hand_state hands[2];
} native_ammo_player_state;
typedef enum { NAM_RESERVE,NAM_SELECT_PLAYER,NAM_INUSE,NAM_EQUIPPED_TYPE,
    NAM_WEAPON,NAM_LOADED,NAM_FUNCTION_REF,NAM_FUNCTION_TYPE,NAM_SPECIAL,
    NAM_AMMO_INDEX,NAM_AMMO_REF,NAM_DEFINITION_TYPE,NAM_STAGE,
    NAM_HAS_FLAG,NAM_COUNT_BEGIN,NAM_COUNT_END } native_ammo_operation;
enum { NAM_EVENT_READ=1,NAM_EVENT_SELECT=2,NAM_EVENT_SERVICE=3 };
typedef struct {
    int32_t kind,operation,subject,scalar[4],current_player_index;
} native_ammo_event;
typedef struct {
    native_ammo_player_state players[2];
    int32_t current_player_index,request,stage;
    uint32_t unlimited_type;
    int32_t function_absent,function_type,ammoindex,specialfunc,weapon_absent;
    /* Per-slot borrowed definition token0=NULL,1/2=definitions0/1; slots may
     * alias. Definition U32 type must be0..32 when full count is reached. */
    int32_t ammo_ref_tokens[2];uint32_t definition_types[2];
    /* Actual property implementation is NOT executed: these raw BOOL32
     * outcomes model only that leaf. Optional synthetic global-player switch
     * occurs during the property call; cached traversal retains its player. */
    int32_t flag_default,flag_results[NATIVE_AMMO_FLAG_LIMIT],flag_result_count;
    int32_t flag_switch_enabled,flag_switch_players[NATIVE_AMMO_FLAG_LIMIT],flag_cursor;
    int32_t trace_count,trace_overflow,program_refused;
    native_ammo_event trace[NATIVE_AMMO_TRACE_LIMIT];
} native_ammo_case;
int32_t nativeAmmoCount(native_ammo_case *);
int32_t nativeGraphAmmoCount(native_ammo_case *);
int32_t nativeReservedAmmoCount(native_ammo_case *);
int32_t nativeGraphReservedAmmoCount(native_ammo_case *);
/* Exclusion predicate: true means excluded, not eligible. */
int32_t nativeAmmoExclusion(native_ammo_case *);
int32_t nativeGraphAmmoExclusion(native_ammo_case *);
int32_t nativeUnlimitedAmmo(native_ammo_case *);
int32_t nativeGraphUnlimitedAmmo(native_ammo_case *);
void nativeAmmoResetTrace(native_ammo_case *);
/* Pure observations of actual modeled reads are distinct from game effects.
 * Read scalar0=value,scalar1=index where relevant. HAS_FLAG scalars=weapon,
 * slot,U32 flag bits,raw BOOL32 result; subject=-1. Nested count begin/end
 * scalar0=request; end scalar1=complete derived count result. */
void nativeAmmoRecordEvent(native_ammo_case *,int32_t kind,int32_t operation,
    int32_t subject,const int32_t scalar[4]);
/* Full AmmoQuery5 native/current source fixtures. Real metadata provider and
 * native ABI/lifetime are not established by supplied reference leaves. */
enum { NATIVE_QUERY_TRACE_LIMIT=160,NATIVE_QUERY_DEFINITIONS=4 };
typedef struct {int32_t weaponnum,loadedammo[2],clipsizes[2];} native_query_hand_state;
typedef enum { NQ_RESOLVE_FUNCTION,NQ_FIND_WEAPON,NQ_INDEX,NQ_EQUIPPED_TYPE,
    NQ_LOADED,NQ_CLIP,NQ_INFO_WEAPON,NQ_RESERVE,NQ_AMMO_REF,NQ_TYPE,NQ_CAPACITY,
    NQ_MIN_BEGIN,NQ_MIN_END,NQ_RESERVED_BEGIN,NQ_RESERVED_END } native_query_operation;
enum { NQ_EVENT_READ=1,NQ_EVENT_SERVICE=2,NQ_CHILD_RESERVED=3 };
typedef struct {
    int32_t kind,operation,subject,scalar[4],current_player_index;
    /* Child entry is its captured immediate observation, not a fabricated
     * post-call parent snapshot. No native address is serialized. */
    native_ammo_event child;
} native_query_event;
typedef struct {
    /* The full reserved-count child and status share these physical players.
     * All nested source bodies run on the same modeled am_vars/am_players. */
    native_ammo_case ammo;
    native_query_hand_state hand;
    int32_t info_weapon,info_equipped_types[2],info_gunctrl_player;
    int32_t status_func,function_absent,legacy_ammo_index,capacity_request;
    uint32_t weapon_u32,func_u32;int32_t weapon_i32,func_i32;
    /* Provider outcome0=NULL,1/2=live private weapons0/1. Each slot's token
     *0=NULL,1..4=definitions0..3; repeated slots/weapon versions may alias. */
    int32_t lookup_weapon_token,weapon_ammo_tokens[2][2];
    uint32_t definition_types[NATIVE_QUERY_DEFINITIONS];
    /* Optional explicitly synthetic current-player switch in lookup leaves.
     * No mutation occurs inside pure field-read observers. */
    int32_t resolve_switch_enabled,resolve_player,find_switch_enabled,find_player;
    int32_t trace_count,trace_overflow,program_refused;
    native_query_event trace[NATIVE_QUERY_TRACE_LIMIT];
} native_ammo_query_case;
int32_t nativeAmmoStatus(native_ammo_query_case *);
int32_t nativeGraphAmmoStatus(native_ammo_query_case *);
int32_t nativeAmmoCapacity(native_ammo_query_case *);
int32_t nativeGraphAmmoCapacity(native_ammo_query_case *);
uint32_t nativeAmmoTypeQuery(native_ammo_query_case *);
uint32_t nativeGraphAmmoTypeQuery(native_ammo_query_case *);
int32_t nativeAmmoQtyQuery(native_ammo_query_case *);
int32_t nativeGraphAmmoQtyQuery(native_ammo_query_case *);
int32_t nativeAmmoWeaponCapacity(native_ammo_query_case *);
int32_t nativeGraphAmmoWeaponCapacity(native_ammo_query_case *);
void nativeAmmoQueryResetTrace(native_ammo_query_case *);
/* Reads scalar0=value/token,bits for U32;scalar1=index. Lookup args/result
 * occupy scalar0..2; nested begin=arguments,end includes computed result.
 * Native C loaded<clip operand reads have unspecified relative evaluation
 * order; trace consumers must preserve that partial-order distinction. */
void nativeAmmoQueryRecordEvent(native_ammo_query_case *,int32_t kind,
    int32_t operation,int32_t subject,const int32_t scalar[4]);
#ifdef __cplusplus
}
#endif
#endif
