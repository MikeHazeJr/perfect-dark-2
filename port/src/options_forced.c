/*
 * port/src/options_forced.c -- INV-4 / Cohort D: pure helpers for
 * options + options_engine_forced bit math.
 *
 * See port/include/options_forced.h for the contract.
 *
 * Pure: no globals, no I/O. Trivial bit operations; the value is
 * having one place where the algebra lives so the B-181 caller and
 * the matchStart() restore agree on semantics.
 */

#include <PR/ultratypes.h>
#include "options_forced.h"

u32 matchOptionsUserView(u32 options, u32 engine_forced)
{
	return options & ~engine_forced;
}

u32 matchOptionsEngineView(u32 options, u32 engine_forced)
{
	return options | engine_forced;
}

void matchOptionsRestoreUserOriginal(u32 *options_inout, u32 *engine_forced_inout)
{
	if (options_inout && engine_forced_inout) {
		*options_inout &= ~(*engine_forced_inout);
		*engine_forced_inout = 0;
	}
}

void matchOptionsForceBit(u32 *options_inout, u32 *engine_forced_inout, u32 bit)
{
	if (options_inout && engine_forced_inout) {
		const u32 newly_forced = bit & ~(*options_inout);
		*engine_forced_inout |= newly_forced;
		*options_inout |= bit;
	}
}

void matchOptionsSetUserBit(u32 *options_inout, u32 *engine_forced_inout,
		u32 bit, s32 enabled)
{
	if (options_inout && engine_forced_inout) {
		*engine_forced_inout &= ~bit;

		if (enabled) {
			*options_inout |= bit;
		} else {
			*options_inout &= ~bit;
		}
	}
}

void matchOptionsReplaceUserOriginal(u32 *options_inout,
		u32 *engine_forced_inout, u32 user_options)
{
	if (options_inout && engine_forced_inout) {
		*options_inout = user_options;
		*engine_forced_inout = 0;
	}
}
