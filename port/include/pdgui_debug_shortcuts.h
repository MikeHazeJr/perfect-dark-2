#ifndef PDGUI_DEBUG_SHORTCUTS_H
#define PDGUI_DEBUG_SHORTCUTS_H

#include "PR/ultratypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Categories for grouping in the pause-menu Debug Shortcuts modal.
 * Order here = display order in the modal. */
typedef enum {
    DBG_SHORTCUT_CAT_RENDERING = 0,
    DBG_SHORTCUT_CAT_DIAGNOSTICS,
    DBG_SHORTCUT_CAT_DEVELOPER,
    DBG_SHORTCUT_CAT_TOOLING,
    DBG_SHORTCUT_CAT_CHEATS,
    DBG_SHORTCUT_CAT_FORGE,
    DBG_SHORTCUT_CAT_COUNT
} pdgui_dbg_shortcut_cat_t;

/* One registry entry. Strings are not copied -- callers must pass string
 * literals or static-lifetime pointers. */
typedef struct PdguiDebugShortcut {
    const char *combo;            /* e.g. "F6", "Shift+F1", "Right-stick click" */
    const char *description;      /* one human-readable line */
    pdgui_dbg_shortcut_cat_t cat;
    s32 dev_only;                 /* 1 if entry only meaningful in PD_DEV_BUILD; 0 = always-on */
} PdguiDebugShortcut;

/* Register a shortcut. Idempotent on (combo, description) -- repeated calls
 * with the same pair are dropped. Combo strings are compared case-sensitively.
 * Internal storage is fixed-size (cap PDGUI_DEBUG_SHORTCUT_MAX); overflow logs
 * a WARNING and drops the entry. */
void pdguiDebugShortcutRegister(const char *combo,
                                const char *description,
                                pdgui_dbg_shortcut_cat_t cat,
                                s32 dev_only);

/* Iterate the registry. Returns the count via outCount and a pointer to the
 * internal array. Pointer remains valid for the program lifetime. */
const PdguiDebugShortcut *pdguiDebugShortcutsGetAll(s32 *outCount);

/* Human-readable category label for the modal. */
const char *pdguiDebugShortcutCatName(pdgui_dbg_shortcut_cat_t cat);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PDGUI_DEBUG_SHORTCUTS_H */
