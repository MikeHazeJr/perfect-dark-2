/*
 * port/src/spawn_predicate.c -- INV-2 (player-init-architectural-fixes
 * 2026-04-26): single-source-of-truth predicate for the spawn-with-weapon
 * vs INTROCMD_WEAPON mutual-exclusion gate.
 *
 * See port/include/spawn_predicate.h for the contract.
 *
 * Pure: no globals, no I/O, no allocator dependencies. The caller site
 * passes (g_Vars.normmplayerisrunning, g_MpSetup.options,
 * MPOPTION_SPAWNWITHWEAPON) explicitly so this TU can be tested in
 * pd-tests without dragging in g_Vars / g_MpSetup / constants.h.
 */

#include <PR/ultratypes.h>
#include "spawn_predicate.h"

s32 spawnWithWeaponShouldApply(s32 normmplayer_running,
                               u32 mp_options,
                               u32 spawnwithweapon_bit)
{
	if (!normmplayer_running) {
		return 0;
	}
	if ((mp_options & spawnwithweapon_bit) == 0) {
		return 0;
	}
	return 1;
}
