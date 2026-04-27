/* Pause-menu Debug Shortcuts registry (port/src/pdgui_debug_shortcuts.c).
 *
 * Single source of truth for the read-only shortcuts list shown by the
 * pause-menu Debug Shortcuts modal.  Each raw SDL hotkey handler in
 * port/fast3d/pdgui_backend.cpp and port/fast3d/gfx_sdl2.cpp pairs its
 * if-block with a pdguiDebugShortcutRegister call so the modal stays in
 * sync without a hand-maintained array.
 *
 * Storage is a fixed-size static array.  Idempotent on (combo, desc) so
 * repeated init paths (e.g. pdguiInit invoked twice across hot-restart)
 * do not duplicate entries.
 */

#include <stdio.h>
#include <string.h>

#include "PR/ultratypes.h"
#include "pdgui_debug_shortcuts.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void sysLogPrintf(s32 level, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#ifndef LOG_WARNING
#define LOG_WARNING 2
#endif

#define PDGUI_DEBUG_SHORTCUT_MAX 64

static PdguiDebugShortcut s_Entries[PDGUI_DEBUG_SHORTCUT_MAX];
static s32 s_Count = 0;

static const char * const s_CatNames[DBG_SHORTCUT_CAT_COUNT] = {
    "Rendering",
    "Diagnostics",
    "Developer",
    "Tooling",
    "Cheats",
    "Forge / Grid",
};

void pdguiDebugShortcutRegister(const char *combo,
                                const char *description,
                                pdgui_dbg_shortcut_cat_t cat,
                                s32 dev_only)
{
    if (!combo || !description) {
        return;
    }
    if (cat < 0 || cat >= DBG_SHORTCUT_CAT_COUNT) {
        cat = DBG_SHORTCUT_CAT_DEVELOPER;
    }

    /* Idempotent: drop duplicates by (combo, description). */
    for (s32 i = 0; i < s_Count; i++) {
        if (!strcmp(s_Entries[i].combo, combo) &&
            !strcmp(s_Entries[i].description, description)) {
            return;
        }
    }

    if (s_Count >= PDGUI_DEBUG_SHORTCUT_MAX) {
        sysLogPrintf(LOG_WARNING,
            "DBGSHORTCUTS.OVERFLOW: registry full (%d), dropping '%s'",
            (int)PDGUI_DEBUG_SHORTCUT_MAX, combo);
        return;
    }

    PdguiDebugShortcut *e = &s_Entries[s_Count++];
    e->combo       = combo;
    e->description = description;
    e->cat         = cat;
    e->dev_only    = dev_only ? 1 : 0;
}

const PdguiDebugShortcut *pdguiDebugShortcutsGetAll(s32 *outCount)
{
    if (outCount) {
        *outCount = s_Count;
    }
    return s_Entries;
}

const char *pdguiDebugShortcutCatName(pdgui_dbg_shortcut_cat_t cat)
{
    if (cat < 0 || cat >= DBG_SHORTCUT_CAT_COUNT) {
        return "Other";
    }
    return s_CatNames[cat];
}
