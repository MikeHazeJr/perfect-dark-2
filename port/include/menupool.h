/**
 * menupool.h -- Pre-allocated menu pool keyed by type.
 *
 * Phase 2 of the input-authority / menu-pool refactor (ADR:
 * context/designs/input-authority-and-menu-pool-2026-04-13.md §6).
 *
 * Background
 * ----------
 * Before this layer, "is menu X open?" was tracked in three independent
 * places:
 *
 *   1. Legacy dialog stack (g_Menus[].layers[].siblings[]) — keyed on
 *      dialogdef pointer. menuPushDialog enforced no-duplicate via a
 *      runtime scan (F-3.1).
 *   2. ImGui renderers — each owned a private bool s_FooPushedCtx for
 *      tracking whether it had pushed g_CtxImGuiMenu. Brittle under
 *      force-close (stage change, netplay transition) — the bool could
 *      desync from the actual stack state, causing "menu closed but
 *      player frozen" soft-locks.
 *   3. Input context stack (inputctx.c) — the truth for mouse/keyboard
 *      routing, but not aware of which menu owns each push.
 *
 * The pool collapses these into one source-of-truth indexed by
 * menu_type_t. Acquire/release is atomic (no partial state). Duplicate
 * acquire of the same type returns 0 — structurally impossible to
 * have two instances live simultaneously (replaces F-3.1 runtime
 * rejection with a hard invariant). Each slot optionally owns an
 * InputContext push/pop pair, so force-close callers can release
 * everything in one call without walking individual renderers.
 *
 * Guarantees
 * ----------
 *   - menupoolIsActive(T) is the authoritative "is menu T open?" query.
 *     All renderers, input logic, and netcode may consult it.
 *   - Calling menupoolAcquire twice for the same type returns 0 on the
 *     second call without mutating pool state.
 *   - menupoolReleaseAll() returns every slot to the free state AND
 *     pops every owned input context. Idempotent; always safe to call.
 *   - Acquire does NOT call inputCtxPush itself if the context is
 *     already active — it just claims ownership. This prevents double-
 *     push when multiple menus share a context (e.g. main menu and
 *     moddinghub both running under g_CtxImGuiMenu).
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#ifndef _IN_PORT_MENUPOOL_H
#define _IN_PORT_MENUPOOL_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

struct InputContext;
struct menudialogdef;

/* Enumerated menu identity.
 *
 * One slot per type — not per dialogdef. Several dialogdefs may map to
 * the same type (e.g. g_CiMenuViaPcMenuDialog and
 * g_CiMenuViaPauseMenuDialog both → MENU_TYPE_MAIN_MENU). Acquiring
 * a type that is already active returns 0.
 *
 * Keep this enum synchronised with menupoolInit() in menupool.c — every
 * legacy dialogdef that can be pushed must be registered to its type.
 * Pure-ImGui menus (no dialogdef backing) acquire by type directly. */
typedef enum {
    MENU_TYPE_NONE = 0,

    /* Legacy dialog-backed — registered via dialogdef→type pairs. */
    MENU_TYPE_MAIN_MENU,          /* CI + Settings variants */
    MENU_TYPE_MAIN_SOLO_VIEW,     /* pure ImGui main-menu Solo Play subview */
    MENU_TYPE_MAIN_SETTINGS_VIEW, /* pure ImGui main-menu Settings subview */
    MENU_TYPE_MAIN_MODDING_VIEW,  /* pure ImGui main-menu Modding subview */
    MENU_TYPE_MAIN_ONLINE_VIEW,   /* pure ImGui main-menu Online Play subview */
    MENU_TYPE_MAIN_STATS_VIEW,    /* pure ImGui main-menu Stats subview */
    MENU_TYPE_CI_OPTIONS,         /* nextsibling auto-open partner of MAIN */
    MENU_TYPE_SOLO_MISSION,       /* select / difficulty / briefing / accept */
    MENU_TYPE_SOLO_MISSION_PAUSE, /* in-mission pause */
    MENU_TYPE_SOLO_INVENTORY,     /* in-mission inventory sibling */
    MENU_TYPE_SOLO_OPTIONS,       /* mission options sub-tree */
    MENU_TYPE_ENDSCREEN_SOLO,
    MENU_TYPE_ENDSCREEN_MP,
    MENU_TYPE_CHEATS,
    MENU_TYPE_MP_SETUP,           /* arena / scenario / weapons / limits */
    MENU_TYPE_MP_SETTINGS,        /* handicap entry point */
    MENU_TYPE_MP_SOUNDTRACK,      /* soundtrack hub (S300 — S-3) */
    MENU_TYPE_MP_TUNES,           /* tunes picker, can stack over soundtrack (S300 — S-3) */
    MENU_TYPE_MP_TEAMNAMES,       /* team-name editor (S300 — S-3) */
    MENU_TYPE_MP_ADVANCED,
    MENU_TYPE_MP_PAUSE,           /* in-match MP pause */
    MENU_TYPE_MP_PLAYER_CONFIG,
    MENU_TYPE_MP_BOT_SETUP,
    MENU_TYPE_MP_TEAM_SETUP,
    MENU_TYPE_CONTROL_DIAGRAM,
    MENU_TYPE_TRAINING,           /* Bio / Hangar entry points (Dt/Ht/Fr split out) */
    /* Firing Range sub-dialogs -- split from MENU_TYPE_TRAINING because the
     * FR flow legitimately stacks three dialogs (Weapon List -> Difficulty
     * -> Pre-Game Info).  Sharing one slot caused pool dedup to reject the
     * second and third push, making the menu appear non-functional. */
    MENU_TYPE_FR_WEAPON_LIST,     /* g_FrWeaponListMenuDialog (root) */
    MENU_TYPE_FR_DIFFICULTY,      /* g_FrDifficultyMenuDialog */
    MENU_TYPE_FR_INFO,            /* PreGame + InGame info (mutually exclusive) */
    MENU_TYPE_FR_RESULT,          /* Completed + Failed (mutually exclusive) */
    /* Device Training (DT) sub-dialogs -- split from MENU_TYPE_TRAINING for
     * the same reason as FR above.  DT flow stacks List -> Details via
     * menuPushDialog; sharing one slot made pool dedup reject the second
     * push and the Details screen would never appear. */
    MENU_TYPE_DT_LIST,            /* g_DtListMenuDialog (root) */
    MENU_TYPE_DT_DETAILS,         /* g_DtDetailsMenuDialog (stacks over list) */
    MENU_TYPE_DT_RESULT,          /* Completed + Failed (mutually exclusive) */
    /* Holo Training (HT) sub-dialogs -- mirrors the DT split.  HT flow
     * stacks List -> Details via menuPushDialog. */
    MENU_TYPE_HT_LIST,            /* g_HtListMenuDialog (root) */
    MENU_TYPE_HT_DETAILS,         /* g_HtDetailsMenuDialog (stacks over list) */
    MENU_TYPE_HT_RESULT,          /* Completed + Failed (mutually exclusive) */
    MENU_TYPE_AGENT_SELECT,
    MENU_TYPE_AGENT_CREATE,
    MENU_TYPE_NETWORK,
    MENU_TYPE_NETWORK_JOINING,
    /* Dedicated-server social lobby (pdgui_menu_lobby) — pure ImGui, no dialogdef. */
    MENU_TYPE_SOCIAL_LOBBY,
    MENU_TYPE_CHALLENGES,
    MENU_TYPE_WARNING_MODAL,      /* danger/success/default type-fallback dialogs */
    MENU_TYPE_ROOM,
    MENU_TYPE_CINEMA,             /* S311: cutscene list (renderCinemaList) */

    /* Pure-ImGui (no dialogdef backing). Acquired by type only. */
    MENU_TYPE_PAUSE_MENU,         /* g_CtxPauseMenu owner */
    MENU_TYPE_MODDING_HUB,
    MENU_TYPE_THEME_EDITOR,
    MENU_TYPE_STATS_PANEL,
    MENU_TYPE_DEBUG_OVERLAY,      /* g_CtxDebugOverlay owner */
    /* AUDIT-24-M5 (2026-04-25): Grid submenu (`s_MenuView == 6` inside
     * the main menu) -- the prior pattern bypassed the pool entirely
     * because the Grid submenu was rendered inline as a tab-state of
     * the parent main menu.  Acquiring a separate pool slot when the
     * Grid sub-view becomes active gives K's input-authority assertion
     * a structural anchor for the Grid screen, lets future entry points
     * call `menupoolAcquire(MENU_TYPE_GRID_SUBMENU, ...)` instead of
     * twiddling `s_MenuView`, and keeps the existing inline-render so
     * the main menu's chrome continues around it. */
    MENU_TYPE_GRID_SUBMENU,
    MENU_TYPE_SOCIAL_SHELL,       /* friends sidebar / Social menu / chat / NAT diagnostics */

    MENU_TYPE_COUNT
} menu_type_t;

/* One-time initialization: registers the built-in dialogdef→type table.
 * Idempotent. Called from inputCtxInit() so the pool is ready before
 * any menu can open. */
void menupoolInit(void);

/* Register a dialogdef → type pair. Called internally by menupoolInit;
 * exposed so late-registered mod dialogs can map themselves. Multiple
 * dialogdefs may share a type. Returns 1 on success, 0 if the registry
 * is full or the type is out of range. Safe to call with NULL def
 * (returns 0). */
s32 menupoolRegisterDialogdef(const struct menudialogdef *def, menu_type_t type);

/* Lookup the registered type for a dialogdef. Returns MENU_TYPE_NONE
 * if unregistered — that is the normal case for dialogs that don't
 * participate in pool dedup (e.g. the PAK-device dialogs, which only
 * appear on N64 and never on PC). */
menu_type_t menupoolTypeForDialogdef(const struct menudialogdef *def);

/* Acquire the pool slot for a type.
 *
 *   type:      which slot to claim.
 *   def:       optional dialogdef that opened it (recorded for diagnostics,
 *              may be NULL for pure-ImGui menus).
 *   ctx:       optional InputContext to push+own. If non-NULL and the
 *              context is not already active on the stack, the pool
 *              calls inputCtxPush(ctx) and remembers ownership. On
 *              release, the pool calls inputCtxPopDeferred(ctx). If the
 *              context is already active (pushed by another pool slot
 *              or externally), the pool does NOT push again — it just
 *              records that it expects the context to survive at least
 *              until this slot releases.
 *
 * Returns:
 *   1  — slot was free and is now active (fresh acquire).
 *   0  — slot was already active (denied — structural dedup). No state
 *        mutated. The caller should treat this as "menu is already open;
 *        no-op or refocus".
 *  -1  — type is out of range (programming error).
 */
s32 menupoolAcquire(menu_type_t type, const struct menudialogdef *def, struct InputContext *ctx);

/* Release the pool slot for a type.
 *
 * Pops the owned input context if any (idempotent — if the ctx has
 * already been popped externally, the inputctx layer warns but nothing
 * else breaks).
 *
 * Returns:
 *   1  — slot was active and is now free.
 *   0  — slot was already free. No state mutated, no warning — release
 *        is always safe. This is intentional so force-close paths can
 *        blanket-release without knowing which slots are live.
 *  -1  — type is out of range (programming error).
 */
s32 menupoolRelease(menu_type_t type);

/* Authoritative query: is this type currently active? */
s32 menupoolIsActive(menu_type_t type);

/* ---- Convenience wrappers keyed on dialogdef (legacy layer) ---- */

/* Resolve dialogdef→type and acquire. Equivalent to
 * menupoolAcquire(menupoolTypeForDialogdef(def), def, ctx), except that
 * an unregistered dialogdef returns 1 (treat as "no dedup enforced")
 * rather than -1 — so legacy dialogs outside the registry still work. */
s32 menupoolAcquireDialog(const struct menudialogdef *def, struct InputContext *ctx);

/* Resolve dialogdef→type and release. Unregistered dialogdef returns 0. */
s32 menupoolReleaseDialog(const struct menudialogdef *def);

/* Query: is this dialogdef's type currently active? Unregistered def → 0. */
s32 menupoolIsDialogActive(const struct menudialogdef *def);

/* Helper for C++ renderer callbacks that receive `struct menudialog *dialog`
 * but can't include types.h (which redefines `bool`). Returns the dialogdef
 * pointer stored at `dialog->definition` (a.k.a. the first struct field),
 * or NULL when dialog is NULL. Implemented in C where types.h is available. */
const struct menudialogdef *menupoolDialogDef(const struct menudialog *dialog);

/* ---- Bulk release (force-close) ---- */

/* Release every active slot. Pops every owned input context. Idempotent.
 *
 * Call sites:
 *   - port/fast3d/pdgui_bridge.c  — endscreen Start/Next/Exit paths
 *   - port/src/net/matchsetup.c   — solo + MP match start (after accept)
 *   - port/src/net/netmsg.c       — co-op / combat stage change handlers
 *   - src/lib/main.c              — stage-transition nuclear reset
 *
 * The canonical invariant after this call: every pool slot is inactive
 * and no InputContext remains owned by the pool. In-flight SDL events
 * may still fire their context's on_event (we use deferred pop), but
 * from the pool's perspective the transition is complete. */
void menupoolReleaseAll(void);

/* ---- Diagnostics ---- */

/* Emit LOG_NOTE lines listing every active slot (name, dialogdef, ctx
 * ownership, generation). Called from inputCtxEndFrame watchdog when it
 * detects a deep stack, and can be invoked manually for debugging. */
void menupoolDumpActive(void);

/* Returns a short human-readable name for a menu_type_t. Never NULL.
 * Used by log lines. */
const char *menupoolTypeName(menu_type_t type);

/* Returns the number of currently active slots. O(N) in MENU_TYPE_COUNT. */
s32 menupoolCountActive(void);

/* Read-only snapshot of active pool slots for diagnostics (no mutations). */
typedef struct MenupoolDebugEntry {
    menu_type_t type;
    const char *type_name;
    u32 generation;
    const struct menudialogdef *def_ptr;
    const char *owned_ctx_name; /* or "shared" / "" */
} MenupoolDebugEntry;

/** Copies active slots in arbitrary order. Returns count written (<= maxEntries). */
s32 menupoolDebugCopyActive(MenupoolDebugEntry *out, s32 maxEntries);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PORT_MENUPOOL_H */
