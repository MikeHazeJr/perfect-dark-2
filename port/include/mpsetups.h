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
 * v3 (2026-07-30): each bot stores its authoritative .pdbotprofile catalog
 *     ID. The PC-only block grows from 80 to 4096 bytes; v0-v2 blocks remain
 *     readable and are upgraded by deriving base profile IDs from traits.
 *
 * Promoted from a file-local #define in port/src/mpsetups.c so the
 * test pin (tests/test_versions_pin.c) can read the live value through
 * the public header rather than hand-mirroring it. */
#define MPSETUP_VERSION 3

s32 mpsetupLoadCurrentFile(void);
s32 mpsetupSaveCurrentFile(void);
void mpsetupLoadSetup(s32 slotindex);
s32 mpsetupSaveSetup(s32 slotindex, u8 savefile);
void mpsetupCopyAllFromPak(void);

#endif
