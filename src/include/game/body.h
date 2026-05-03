#ifndef IN_GAME_BODY_H
#define IN_GAME_BODY_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void bodiesInit(void);

void bodiesReset(s32 stagenum);

u32 bodyGetRace(s32 bodynum);
/* B-314 (2026-05-03): allocate chr->unk348[] fireslot/beam pair for
 * RACE_ROBOT bodies (currently just BODY_CHICROB). Must be called by every
 * chr-spawn path AFTER setting chr->bodynum so propsRenderBeams (propobj.c
 * around line 11723) can safely dereference chr->unk348[0/1]->beam without
 * NULL deref. Solo bodyAllocateChr already inlined this; botmgr.c (MP bot
 * create) and chraction.c (AI spawn) were missing it pre-B-314, which
 * crashed when a Combat Sim bot rolled a CHICROB body. Helper centralises
 * the init so future spawn paths cannot miss it. No-op for non-robot
 * bodies. */
void bodyInitChrBeams(struct chrdata *chr, s32 bodynum);
bool bodyLoad(s32 bodynum);
struct model *body0f02ce8c(s32 bodynum, s32 headnum, struct modeldef *bodymodeldef, struct modeldef *headmodeldef, bool sunglasses, struct model *model, bool isplayer, u8 varyheight);
struct model *body0f02d338(s32 bodynum, s32 headnum, struct modeldef *bodymodeldef, struct modeldef *headmodeldef, bool sunglasses, u8 varyheight);
struct model *bodyAllocateModel(s32 bodynum, s32 headnum, u32 spawnflags);
s32 body0f02d3f8(void);
s32 bodyChooseHead(s32 bodynum);
void bodyAllocateChr(s32 stagenum, struct packedchr *packed, s32 cmdindex);
struct prop *bodyAllocateEyespy(struct pad *pad, RoomNum room);
void bodyCalculateHeadOffset(struct modeldef *headmodeldef, s32 headnum, s32 bodynum);

#endif
