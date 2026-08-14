#ifndef PD_OPTIONS_FORCED_H
#define PD_OPTIONS_FORCED_H

#include <PR/ultratypes.h>

/*
 * options_forced.h -- INV-4 / Cohort D (player-init-architectural-fixes
 * 2026-04-26): pure helpers for the options + options_engine_forced
 * pair that lets engine-forced MP options bits be unwound on match
 * boundaries without losing the user's original choice.
 *
 * Background. The B-181 fallback at src/game/setup.c historically
 * OR'd MPOPTION_SPAWNWITHWEAPON onto g_MatchConfig.options + g_MpSetup.options
 * with no record that the engine had forced the bit. If the user had
 * explicitly disabled spawn-with-weapon via the menu, the engine
 * silently re-enabled it, and the override persisted across matches
 * because g_MatchConfig.options keeps state until matchConfigReset()
 * runs again (which is rare).
 *
 * INV-4 introduces a parallel `options_engine_forced` u32 field. The
 * B-181 fallback (and any future engine-forced bit setter) records only
 * bits that it actually added. A bit that was already enabled belongs to
 * the user and must never enter the forced mask. At a match boundary the
 * engine-forced bits are cleared from the active options and the mask is
 * reset, restoring the user's original state. B-181 can re-evaluate and
 * re-fire if the new map's pickup count is still low.
 *
 * The pure helpers here do the bit math. The B-181 caller and the
 * matchStart() restore site call these so the algebra has a single
 * source of truth.
 *
 * Decision: D-4 (parent session, 2026-04-26) selected the separate
 * field over a parallel overlay struct -- less invasive.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Returns the user-original options mask: options with all
 * engine-forced bits cleared. This is what the user set via the menu
 * (modulo any user toggles since the last engine-force, which the
 * codebase treats as user-original by default).
 *
 * Pure: bit operation, no side effects.
 */
u32 matchOptionsUserView(u32 options, u32 engine_forced);

/*
 * Returns the active engine options mask: options with all
 * engine-forced bits applied. With the current invariant (B-181 sets
 * the bit in `options` AT THE SAME TIME it sets it in `engine_forced`)
 * this is byte-equivalent to `options`. Provided for symmetry and so
 * any future code path that records a forced bit WITHOUT also setting
 * it in `options` (deferred apply) is still consistent.
 *
 * Pure.
 */
u32 matchOptionsEngineView(u32 options, u32 engine_forced);

/*
 * Apply the forced-bit restoration: clear engine-forced bits from
 * `*options_inout` and zero `*engine_forced_inout`. This is the
 * matchStart() restore. After calling, `*options_inout` reflects only
 * the user-original choices and `*engine_forced_inout` is 0.
 *
 * Pure modulo the in-out pointers.
 */
void matchOptionsRestoreUserOriginal(u32 *options_inout, u32 *engine_forced_inout);

/*
 * Apply one or more engine-required bits atomically. Only bits absent
 * from the current options value are added to the ownership mask. Bits
 * the user had already enabled remain user-owned, so a later restore
 * cannot erase them.
 *
 * Pure modulo the in-out pointers.
 */
void matchOptionsForceBit(u32 *options_inout, u32 *engine_forced_inout, u32 bit);

/*
 * Apply an explicit user edit. The edited bits cease to be engine-owned,
 * then are set or cleared according to `enabled`. This lets UI writers use
 * the same ownership contract as UI readers.
 */
void matchOptionsSetUserBit(u32 *options_inout, u32 *engine_forced_inout,
		u32 bit, s32 enabled);

/*
 * Replace the complete user-authored option set (scenario load, room sync,
 * or other authoritative settings replacement). Any prior engine overlay
 * is superseded and its ownership mask is cleared in the same operation.
 */
void matchOptionsReplaceUserOriginal(u32 *options_inout,
		u32 *engine_forced_inout, u32 user_options);

#ifdef __cplusplus
}
#endif

#endif /* PD_OPTIONS_FORCED_H */
