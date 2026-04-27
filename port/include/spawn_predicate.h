#ifndef PD_SPAWN_PREDICATE_H
#define PD_SPAWN_PREDICATE_H

#include <PR/ultratypes.h>
/* No <stdbool.h>: this header is consumed from src/game/ TUs where
 * `bool` is the s32 typedef (per CLAUDE.md). Use s32 for the public
 * signature and the test TU casts as needed. */

/*
 * spawn_predicate.h -- INV-2 (player-init-architectural-fixes 2026-04-26):
 * single-source-of-truth predicate for the spawn-with-weapon vs
 * INTROCMD_WEAPON mutual-exclusion gate.
 *
 * Background. The PD2 spawn-with-weapon logic involves two sites that
 * share a precondition:
 *
 *   - src/game/playerreset.c:225 -- INTROCMD_WEAPON skip-gate (skips
 *     intro inventory in normal MP when SPAWNWITHWEAPON is set, so
 *     spawn-with-weapon owns the loadout).
 *   - src/game/player.c:1790    -- spawn-with-weapon application.
 *   - src/game/bot.c:506        -- bot-side spawn-with-weapon application.
 *   - src/game/setup.c:2745     -- B-181 fallback that may force the bit
 *     ON when world pickups are below threshold.
 *
 * Pre-INV-2, the PD2 application sites at player.c:1790 + bot.c:506
 * dropped the inner `g_Vars.normmplayerisrunning &&` outer guard while
 * the playerreset.c:225 gate kept it. Net: in Co-Op or Counter-Op Bond,
 * MPOPTION_SPAWNWITHWEAPON propagates from g_MatchConfig.options
 * (default ON, see matchsetup.c:101) into g_MpSetup.options, the
 * dropped-guard sites fire spawn-with-weapon, AND the gate at
 * playerreset.c:225 (keyed on `normmplayerisrunning`) fails to skip
 * INTROCMD_WEAPON. Both apply: the player gets intro inventory PLUS
 * the spawn weapon.
 *
 * INV-2 restores symmetry. All four sites consult `spawnWithWeaponShouldApply`,
 * which keys on (normmplayer && SPAWNWITHWEAPON-bit). The B-181 fallback
 * also gates its OR-set on `normmplayer` so the bit cannot leak into
 * Co-Op / Counter-Op match config from the engine fallback path.
 *
 * The predicate is intentionally trivial. Its value is being the SINGLE
 * point where the decision lives so the test suite can pin it and any
 * future change applies to every consumer simultaneously.
 *
 * Decision: D-1 (parent session, 2026-04-26) selected Option A (re-add
 * the normmplayer guard symmetrically) over Option B (force-clear the
 * bit on Co-Op entry) and Option C (drop the gate the other way).
 * Smallest behavioral footprint; restores the upstream invariant.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns 1 iff spawn-with-weapon should apply for this spawn, else 0.
 *
 * Inputs:
 *   normmplayer_running  -- value of g_Vars.normmplayerisrunning at the
 *                           caller site (1 iff active match is normal
 *                           MP, NOT Co-Op or Counter-Op). Pass as s32
 *                           because src/game/ TUs use s32-typedef'd bool.
 *   mp_options           -- value of g_MpSetup.options. The
 *                           MPOPTION_SPAWNWITHWEAPON bit (defined in
 *                           constants.h) is the user / engine choice.
 *   spawnwithweapon_bit  -- the MPOPTION_SPAWNWITHWEAPON value itself,
 *                           passed in to keep the predicate independent
 *                           of constants.h. Callers pass MPOPTION_SPAWNWITHWEAPON
 *                           directly.
 *
 * Returns:
 *   1 if spawn-with-weapon should fire AND INTROCMD_WEAPON should
 *   skip. 0 if INTROCMD_WEAPON should run normally and
 *   spawn-with-weapon should not fire.
 *
 * Mutual exclusion: callers MUST treat the same return value as the
 * gate decision for both paths in a given spawn. Treating
 * spawn-with-weapon and INTROCMD_WEAPON as independent decisions
 * reintroduces the F-1 / F-5 / F-8 dual-add bug.
 */
s32 spawnWithWeaponShouldApply(s32 normmplayer_running,
                               u32 mp_options,
                               u32 spawnwithweapon_bit);

#ifdef __cplusplus
}
#endif

#endif /* PD_SPAWN_PREDICATE_H */
