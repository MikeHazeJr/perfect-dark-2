#ifndef _IN_GAME_PLAYERMGR_H
#define _IN_GAME_PLAYERMGR_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void playermgrInit(void);
void playermgrReset(void);
enum playermgr_allocate_result {
	PLAYMGR_ALLOC_OK = 0,
	PLAYMGR_ALLOC_INVALID_COUNT = -1,
	PLAYMGR_ALLOC_OUT_OF_MEMORY = -2,
	PLAYMGR_ALLOC_NETWORK_REJECTED = -3,
};

enum playermgr_allocate_result playermgrAllocatePlayers(s32 count);
const char *playermgrAllocateResultString(enum playermgr_allocate_result result);
void playermgrCalculateAiBuddyNums(void);
void setCurrentPlayerNum(s32 playernum);
s32 playermgrGetPlayerNumByProp(struct prop *prop);
s32 playermgrGetLocalPlayerNum(void);
s32 playermgrGetPresentationPlayerNum(void);
bool playermgrRestoreLocalPlayerContext(void);
void playermgrSetViewSize(s32 viewx, s32 viewy);
void playermgrSetViewPosition(s32 viewleft, s32 viewtop);
void playermgrSetFovY(f32 fovy);
void playermgrSetAspectRatio(f32 aspect);
s32 playermgrGetModelOfWeapon(s32 weapon);
void playermgrDeleteWeapon(s32 hand);
void playermgrCreateWeapon(s32 hand);
void playermgrShuffle(void);
s32 playermgrGetOrderOfPlayer(s32 playernum);
s32 playermgrGetPlayerAtOrder(s32 ordernum);

#endif
