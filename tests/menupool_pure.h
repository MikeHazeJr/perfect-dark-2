/*
 * menupool_pure.h -- Pure-C subset of the menu pool used by pd-tests.
 *
 * The real menupool.c (port/src/menupool.c) is the source of truth for
 * structural single-instance dedup keyed by menu_type_t. It pulls in the
 * full data.h dialogdef extern table (~70 names, defined across src/game/
 * .c files) and the input-context layer for ctx push/pop.
 *
 * This file replicates the slot machinery (acquire / release / IsActive /
 * ReleaseAll) without the dialogdef registry or the InputContext coupling.
 * Tests in tests/test_menu_stack.cpp drive this module to verify:
 *
 *   1. Acquire on a free slot returns 1; pool slot becomes active.
 *   2. Acquire on an already-active slot returns 0; no state change.
 *   3. Release on an active slot returns 1; slot becomes free.
 *   4. Release on an already-free slot returns 0 (no-op, no warning).
 *   5. IsActive reflects the slot's current state.
 *   6. ReleaseAll frees every active slot atomically.
 *   7. Out-of-range type returns -1 from acquire / release.
 *   8. Generation counter monotonically increments per acquire (diag aid).
 *
 * @SYNC port/src/menupool.c (menupoolAcquire / Release / IsActive / ReleaseAll)
 * @SYNC port/include/menupool.h (menu_type_t enum is mirrored)
 *
 * If menu_type_t enum changes in the real header, re-sync this header
 * (the enum below) so test type indices match.
 */

#ifndef _IN_TESTS_MENUPOOL_PURE_H
#define _IN_TESTS_MENUPOOL_PURE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mirror the canonical menu_type_t enum from port/include/menupool.h.
 * If the real enum gains or removes entries, update this table and the
 * tests will catch any mismatched assumptions. */
typedef enum {
    MP_TYPE_NONE = 0,
    MP_TYPE_MAIN_MENU,
    MP_TYPE_SOLO_MISSION,
    MP_TYPE_AGENT_SELECT,
    MP_TYPE_TRAINING,
    MP_TYPE_NETWORK,
    MP_TYPE_ROOM,
    MP_TYPE_MP_SETUP,
    MP_TYPE_MP_PLAYER_CONFIG,
    MP_TYPE_MP_BOT_SETUP,
    MP_TYPE_MP_TEAM_SETUP,
    MP_TYPE_MP_PAUSE,
    MP_TYPE_PAUSE_MENU,
    MP_TYPE_CHEATS,
    MP_TYPE_MODDING_HUB,
    MP_TYPE_THEME_EDITOR,
    MP_TYPE_WARNING_MODAL,
    MP_TYPE_CINEMA,
    MP_TYPE_DEBUG_OVERLAY,
    MP_TYPE_COUNT
} mp_type_t;

void menupoolPureReset(void);

/* Returns 1 on fresh acquire, 0 on duplicate (already active),
 * -1 on out-of-range type. */
s32 menupoolPureAcquire(mp_type_t type);

/* Returns 1 on release of an active slot, 0 if slot was already free,
 * -1 on out-of-range. */
s32 menupoolPureRelease(mp_type_t type);

s32 menupoolPureIsActive(mp_type_t type);
s32 menupoolPureCountActive(void);
void menupoolPureReleaseAll(void);

/* Generation counter for a type - monotonically increments on every fresh
 * acquire. Useful for tests verifying the slot was reused vs. retained. */
u32 menupoolPureGeneration(mp_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* _IN_TESTS_MENUPOOL_PURE_H */
