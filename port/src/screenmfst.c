/**
 * screenmfst.c -- Phase 6: Menu/UI screen asset mini-manifests.
 *
 * Registry that maps menudialogdef* → list of catalog IDs.  On every frame,
 * pdguiHotswapRenderQueued() calls screenManifestTick() with the set of
 * dialogs rendered this frame.  Enter/leave events are detected by comparing
 * against last-frame's set, then catalogLoadAsset / catalogUnloadAsset are
 * driven accordingly.
 *
 * Phase 5 ref counting ensures that an asset shared by two simultaneously
 * active screens is only freed after both screens leave.
 *
 * C89 style: variables at top of function, s32 i for loops.
 * Does NOT include types.h — uses PR/ultratypes.h directly.
 * Auto-discovered by CMake GLOB_RECURSE for port/src/*.c.
 */

#include <string.h>
#include <stdio.h>
#include <PR/ultratypes.h>
#include "screenmfst.h"
#include "assetcatalog_load.h"
#include "system.h"

/* ========================================================================
 * Internal types
 * ======================================================================== */

typedef struct {
    void *dialogdef;                            /* menudialogdef* key (opaque) */
    char  ids[SMFST_MAX_IDS_PER_SCREEN][64];   /* catalog string IDs */
    u8    types[SMFST_MAX_IDS_PER_SCREEN];     /* MANIFEST_TYPE_* per ID */
    s32   count;                                /* valid entries */
} ScreenEntry;

/* ========================================================================
 * Module state
 * ======================================================================== */

static ScreenEntry s_Screens[SMFST_MAX_SCREENS];
static s32         s_ScreenCount = 0;

/* Active dialog set from the previous frame — for enter/leave detection. */
static void *s_LastActive[SMFST_MAX_ACTIVE];
static s32   s_LastActiveCount = 0;

/* ========================================================================
 * Helpers (internal)
 * ======================================================================== */

static ScreenEntry *findEntry(void *dialogdef)
{
    s32 i;
    for (i = 0; i < s_ScreenCount; i++) {
        if (s_Screens[i].dialogdef == dialogdef) {
            return &s_Screens[i];
        }
    }
    return NULL;
}

static s32 isInList(void **list, s32 count, void *ptr)
{
    s32 i;
    for (i = 0; i < count; i++) {
        if (list[i] == ptr) {
            return 1;
        }
    }
    return 0;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void screenManifestRegister(void *dialogdef,
                             const char **catalog_ids,
                             const u8 *types,
                             s32 count)
{
    ScreenEntry *e;
    s32 i;

    if (!dialogdef || !catalog_ids || !types || count <= 0) {
        return;
    }

    e = findEntry(dialogdef);
    if (!e) {
        if (s_ScreenCount >= SMFST_MAX_SCREENS) {
            sysLogPrintf(LOG_WARNING, "SMFST: registry full (%d screens)",
                         SMFST_MAX_SCREENS);
            return;
        }
        e = &s_Screens[s_ScreenCount++];
        e->dialogdef = dialogdef;
        e->count = 0;
    }

    if (count > SMFST_MAX_IDS_PER_SCREEN) {
        count = SMFST_MAX_IDS_PER_SCREEN;
    }

    for (i = 0; i < count; i++) {
        if (catalog_ids[i]) {
            strncpy(e->ids[i], catalog_ids[i], 63);
            e->ids[i][63] = '\0';
        } else {
            e->ids[i][0] = '\0';
        }
        e->types[i] = types[i];
    }
    e->count = count;

    sysLogPrintf(LOG_NOTE, "SMFST: registered %p with %d asset(s)",
                 dialogdef, count);
}

void screenManifestTick(void **active_defs, s32 count)
{
    ScreenEntry *e;
    s32 i, j;

    if (count > SMFST_MAX_ACTIVE) {
        count = SMFST_MAX_ACTIVE;
    }

    /* Enter pass: dialogs in active_defs but absent from s_LastActive. */
    for (i = 0; i < count; i++) {
        if (!active_defs[i]) {
            continue;
        }
        if (isInList(s_LastActive, s_LastActiveCount, active_defs[i])) {
            continue;
        }

        /* Screen just became active — load its declared assets. */
        e = findEntry(active_defs[i]);
        if (!e) {
            continue;
        }

        sysLogPrintf(LOG_NOTE, "SMFST: enter %p — loading %d asset(s)",
                     active_defs[i], e->count);
        for (j = 0; j < e->count; j++) {
            if (e->ids[j][0]) {
                catalogLoadAsset(e->ids[j]);
            }
        }
    }

    /* Leave pass: dialogs in s_LastActive but absent from active_defs. */
    for (i = 0; i < s_LastActiveCount; i++) {
        if (!s_LastActive[i]) {
            continue;
        }
        if (isInList(active_defs, count, s_LastActive[i])) {
            continue;
        }

        /* Screen just left — unload its declared assets.
         * Phase 5 ref counting: only actually freed when ref hits 0,
         * so assets shared with the match manifest or another active
         * screen are safely retained. */
        e = findEntry(s_LastActive[i]);
        if (!e) {
            continue;
        }

        sysLogPrintf(LOG_NOTE, "SMFST: leave %p — unloading %d asset(s)",
                     s_LastActive[i], e->count);
        for (j = 0; j < e->count; j++) {
            if (e->ids[j][0]) {
                catalogUnloadAsset(e->ids[j]);
            }
        }
    }

    /* Update last-active snapshot for next frame. */
    for (i = 0; i < count; i++) {
        s_LastActive[i] = active_defs[i];
    }
    s_LastActiveCount = count;
}

void screenManifestShutdown(void)
{
    s32 i, j;
    ScreenEntry *e;

    /* Unload assets for any screens still considered active. */
    for (i = 0; i < s_LastActiveCount; i++) {
        if (!s_LastActive[i]) {
            continue;
        }
        e = findEntry(s_LastActive[i]);
        if (!e) {
            continue;
        }
        for (j = 0; j < e->count; j++) {
            if (e->ids[j][0]) {
                catalogUnloadAsset(e->ids[j]);
            }
        }
    }

    memset(s_Screens, 0, sizeof(s_Screens));
    s_ScreenCount = 0;
    memset(s_LastActive, 0, sizeof(s_LastActive));
    s_LastActiveCount = 0;
}
