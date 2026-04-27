#ifndef _IN_MPSETUPS_H
#define _IN_MPSETUPS_H

#include "types.h"

/* MPSETUP_VERSION = binary WAD format for MP setup blocks.
 * v0: legacy N64 ROM format (read-only support).
 * v1: PC port format (32-bit options, 64-bit random-filter mask, etc.).
 * v2 (2026-04-26): coordinated bump for Goldfinger 64 weapon cull.
 *     8 mod-imported weapons (MPWEAPON_PP9I..MPWEAPON_RCP45, old slots
 *     0x27..0x2e) removed; SHIELD/DISABLED shifted from 0x2f/0x30 to
 *     0x27/0x28. Loader migrates v < 2 saves: clamps weapons[] values
 *     in the now-removed range to MPWEAPON_DISABLED and clears the
 *     random-filter mask. See mpsetupfileLoadWad in mplayer.c.
 *
 * Promoted from a file-local #define in port/src/mpsetups.c so the
 * test pin (tests/test_versions_pin.c) can read the live value through
 * the public header rather than hand-mirroring it. */
#define MPSETUP_VERSION 2

s32 mpsetupLoadCurrentFile(void);
s32 mpsetupSaveCurrentFile(void);
void mpsetupLoadSetup(s32 slotindex);
s32 mpsetupSaveSetup(s32 slotindex, u8 savefile);
void mpsetupCopyAllFromPak(void);

#endif
