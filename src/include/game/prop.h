#ifndef IN_GAME_PROP_H
#define IN_GAME_PROP_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

extern f32 g_AutoAimScale;
/* S311: pointer to whichever interactable prop the player is currently
 * looking at, or NULL. Set by propFindForInteract per frame and read by
 * the bondmove interact-prompt path + the pdgui prompt overlay. Cleared
 * by propsReset on stage transition (S311-followup, 2026-05-01) so it
 * cannot dangle into a freed prop slot from the prior stage. */
extern struct prop *g_InteractProp;

void propsTick(void);

void propsStop(void);

void propsSort(void);
void propEnable(struct prop *prop);
void propDisable(struct prop *prop);
struct prop *propAllocate(void);
/* Return whether the free-prop list can satisfy a synchronous transaction's
 * complete aggregate demand without any mutation. */
s32 propsReserveCreateCount(s32 count);
void propFree(struct prop *prop);
void propActivate(struct prop *prop);
void propActivateThisFrame(struct prop *prop);
void propDelist(struct prop *prop);
void propReparent(struct prop *mover, struct prop *adopter);
void propDetach(struct prop *prop);
Gfx *propRender(Gfx *gdl, struct prop *prop, bool xlupass);
Gfx *propsRender(Gfx *gdl, RoomNum renderroomnum, s32 renderpass, RoomNum *roomnumsbyprop);
void weaponPlayWhooshSound(s32 weaponnum, struct prop *prop);
void func0f060bac(s32 weaponnum, struct prop *prop);
struct prop *shotCalculateHits(s32 handnum, bool isshooting, struct coord *gunpos2d, struct coord *gundir2d, struct coord *gunpos3d, struct coord *gundir3d, u32 arg6, f32 distance, bool arg8);
struct prop *propFindAimingAt(s32 handnum, bool isshooting, u32 context);
void shotCreate(s32 handnum, bool arg1, bool dorandom, s32 numshots, bool arg4);
void hitCreate(struct shotdata *shotdata, struct prop *prop, f32 hitdistance, s32 hitpart, struct modelnode *bboxnode, struct hitthing *hitthing, s32 arg6, struct modelnode *dlnode, struct model *model, bool slowsbullet, bool bulletproof, struct coord *arg11, struct coord *arg12);
void handInflictMeleeDamage(s32 handnum, struct gset *gset, bool arg2);
void handTickAttack(s32 handnum);
void handsTickAttack(void);
void propExecuteTickOperation(struct prop *prop, s32 op);
struct prop *propFindForInteract(bool eyespy);
/* S311: short English label ("Pick up", "Open", "Access", "Use") for the
 * currently-targeted interact prop, or NULL if none.  Read-only. */
const char *propInteractPromptLabel(void);
/**
 * Effective hold duration (ms) for the current g_InteractProp interact prompt:
 * actionmapGetEffectiveHoldMs(ACTION_USE) (global and/or per-action override in
 * ActionMap.HoldMsOverrides) plus optional per-category extra time — keeps HUD
 * ring and bondmove hold/tap in sync.
 */
s32 propInteractPromptHoldThresholdMs(void);
/** 1 when UI should read "Press [key] ..." with no hold ring (tap-to-enter). */
s32 propInteractPromptPreferPressStyle(void);
/**
 * ACTION_USE hold/tap threshold for bondmove: full prompt path when targeted,
 * otherwise actionmapGetEffectiveHoldMs(ACTION_USE).
 */
s32 propGetActionUseHoldThresholdMs(void);

/**
 * Phase 2 fix #5 (input-menu pillar, 2026-05-01): tunable interaction cast
 * half-angle (radians). Default 0.2617993878f = 15 degrees (30-degree full
 * cone), down from prior 22.5 degrees / 45-degree full cone per Mike's
 * directive. Fix #6 will expose this via Settings -> Debug for empirical
 * dial-in alongside the visualization toggle.
 */
f32 propGetInteractCastHalfAngleRad(void);
void propSetInteractCastHalfAngleRad(f32 rad);
void propFindForUplink(void);
bool currentPlayerInteract(bool eyespy);
void propPause(struct prop *prop);
void propUnpause(struct prop *prop);
void propsTickPlayer(bool islastplayer);
void propsTickPadEffects(void);
void propSetPerimEnabled(struct prop *prop, bool enable);
void propsTestForPickup(void);
f32 func0f06438c(struct prop *prop, struct coord *arg1, f32 *arg2, f32 *arg3, f32 *arg4, bool throughobjects, bool cangangsta, s32 arg7);
void farsightChooseTarget(void);
void autoaimTick(void);
u32 propDoorGetCdTypes(struct prop *prop);
bool propIsOfCdType(struct prop *prop, u32 types);
void roomsCopy(RoomNum *srcrooms, RoomNum *dstrooms);
void roomsCopySafe(RoomNum *srcrooms, RoomNum *dstrooms, s32 maxdst);
void roomsAppend(RoomNum *newrooms, RoomNum *dstrooms, s32 maxlen);
bool arrayIntersects(RoomNum *a, RoomNum *b);
bool propTryAddToChunk(s16 propnum, s32 chunkindex);
s32 roomAllocatePropListChunk(s32 room, s32 arg1);
void propRegisterRoom(struct prop *prop, RoomNum room);
void propDeregisterRoom(struct prop *prop, RoomNum room);
void propDeregisterRooms(struct prop *prop);
void propRegisterRooms(struct prop *prop);
void func0f065d1c(struct coord *pos, RoomNum *rooms, struct coord *newpos, RoomNum *newrooms, RoomNum *morerooms, u32 arg5);
void func0f065dd8(struct coord *pos, RoomNum *rooms, struct coord *newpos, RoomNum *newrooms);
void func0f065dfc(struct coord *pos, RoomNum *rooms, struct coord *newpos, RoomNum *newrooms, RoomNum *morerooms, u32 arg5);
void func0f065e74(struct coord *pos, RoomNum *rooms, struct coord *newpos, RoomNum *newrooms);
void func0f065e98(struct coord *pos, RoomNum *rooms, struct coord *pos2, RoomNum *rooms2);
void roomGetProps(RoomNum *room, s16 *propnums, s32 len);
void propsDefragRoomProps(void);
void propGetBbox(struct prop *prop, f32 *radius, f32 *ymax, f32 *ymin);
bool propUpdateGeometry(struct prop *prop, u8 **start, u8 **end);

bool shotTestLos(struct coord *gunpos2d, struct coord *gundir2d, struct coord *gunpos3d, struct coord *gundir3d, struct coord *endpos3d);

#endif
