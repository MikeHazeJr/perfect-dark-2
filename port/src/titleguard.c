/**
 * titleguard.c -- Real-time minimum display guard for intro sequences.
 *
 * The original N64 title/intro code uses g_TitleTimer (accumulated from
 * g_Vars.lvupdate60) for both animation timing and skip detection.
 * On the PC port, lvupdate60 can spike enormously on the first few frames
 * (delta time from initialization), causing the intro to blast through
 * in one frame.
 *
 * This module provides a wall-clock timer that the title tick functions
 * can check before honoring button-skip requests. Each title mode resets
 * the timer when it starts, and the tick function queries whether enough
 * real time has elapsed to allow skipping.
 *
 * Auto-discovered by GLOB_RECURSE for port/*.c in CMakeLists.txt.
 */

#include <PR/ultratypes.h>
#include "system.h"

/* Timestamp (microseconds) when the current title mode started */
static u64 g_TitleModeStartUs = 0;

/**
 * Reset the skip guard timer. Call when a title mode transition occurs.
 */
void titleGuardReset(void)
{
	g_TitleModeStartUs = sysGetMicroseconds();
}

/**
 * Returns non-zero if at least minMs milliseconds of real wall-clock time
 * have elapsed since the last titleGuardReset() call.
 *
 * Use this in title tick functions before honoring button-skip requests:
 *   if (titleGuardCanSkip(2000) && joyGetButtonsPressedThisFrame(...)) { skip; }
 */
s32 titleGuardCanSkip(u32 minMs)
{
	u64 now = sysGetMicroseconds();
	u64 elapsedUs = now - g_TitleModeStartUs;
	u64 minUs = (u64)minMs * 1000;

	return (elapsedUs >= minUs) ? 1 : 0;
}
