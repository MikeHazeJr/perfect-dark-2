#ifndef IN_GAME_PLAYERRESET_H
#define IN_GAME_PLAYERRESET_H
#include <ultra64.h>
#include "data.h"
#include "types.h"

void modelmgrReset(void);
void modelmgrSetLvResetting(bool value);
void modelmgrAllocateSlots(s32 numobjs, s32 numchrs);
bool modelmgrLoadProjectileModeldefs(s32 weaponnum);
bool playerInitEyespy(void);
/*
 * Canonical defaults for transient input/weapon state at a stage boundary.
 * This deliberately leaves transport ownership (client/isremote/ucmd) and
 * persistent player preferences untouched.
 */
void playerInitStageTransientDefaults(struct player *player);

enum player_reset_result {
	PLAYER_RESET_OK = 0,
	PLAYER_RESET_INVALID_IDENTITY = -1,
	PLAYER_RESET_PROP_ALLOCATION_FAILED = -2,
	PLAYER_RESET_CHR_ALLOCATION_FAILED = -3,
	PLAYER_RESET_EYESPY_ALLOCATION_FAILED = -4,
	PLAYER_RESET_CHRBODY_PREFLIGHT_FAILED = -5,
	PLAYER_RESET_TARGET_BIND_FAILED = -6,
};

enum player_reset_result playerReset(void);
const char *playerResetResultString(enum player_reset_result result);

enum player_stage_rollback_flags {
	PLAYER_STAGE_ROLLBACK_NONE = 0,
	PLAYER_STAGE_ROLLBACK_EYESPY = 1 << 0,
	PLAYER_STAGE_ROLLBACK_CHRBODY = 1 << 1,
	PLAYER_STAGE_ROLLBACK_PLAYER_PROP = 1 << 2,
	PLAYER_STAGE_ROLLBACK_GUNMEM = 1 << 3,
	PLAYER_STAGE_ROLLBACK_MP_BINDINGS = 1 << 4,
};

/* Reverse one successfully committed playerReset/playerSpawn stage unit. The
 * caller owns reverse roster order and must select the player first. */
u32 playerRollbackStageInitialization(void);

#endif
