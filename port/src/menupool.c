/**
 * menupool.c -- Pre-allocated menu pool keyed by type (Phase 2).
 *
 * See menupool.h for the rationale and invariants. This file implements
 * the flat slot array, the dialogdef→type registry, and the built-in
 * registration table.
 *
 * Auto-discovered by GLOB_RECURSE in CMakeLists.txt.
 */

#include <string.h>
#include <PR/ultratypes.h>
#include "data.h"
#include "types.h"
#include "menupool.h"
#include "inputctx.h"
#include "system.h"

/* S311: CI Options family + Cinema dialogdefs defined in mainmenu.c but
 * NOT exported via data.h — duplicate the extern decls here so the
 * registry table can address them by symbol. */
extern struct menudialogdef g_CiOptionsViaPcMenuDialog;
extern struct menudialogdef g_CiOptionsViaPauseMenuDialog;
extern struct menudialogdef g_CiControlOptionsMenuDialog;
extern struct menudialogdef g_CiDisplayMenuDialog;
extern struct menudialogdef g_CiDisplayPlayer2MenuDialog;
extern struct menudialogdef g_CiControlPlayer2MenuDialog;
extern struct menudialogdef g_CinemaMenuDialog;

/* B-194: g_FilemgrFileSelectMenuDialog is defined in src/game/filemgr.c
 * but only the 4MB twin is externed in data.h. Declare it locally so the
 * registry can register it. See B-194 comment at the REG(...) site below
 * for rationale. */
extern struct menudialogdef g_FilemgrFileSelectMenuDialog;

/* B-End-Game-Input (2026-04-19): MP endscreen + End Game dialogs defined
 * in src/game/mplayer/ingame.c but not exported via data.h. Register them
 * in the pool so menuPushRootDialog's menupoolReleaseAll + deferred-pop
 * path doesn't leave g_CtxImGuiMenu in a mid-transition state when the
 * endscreen opens — the renderer's fresh-entry ctx-push check raced with
 * the deferred pop, stranding input after CS matches ended. Also registers
 * g_MpEndGameMenuDialog as WARNING_MODAL so the confirm popup participates
 * in pool lifecycle + gets proper ctx ownership instead of leaking through
 * the unregistered fallback. */
extern struct menudialogdef g_MpEndscreenIndGameOverMenuDialog;
extern struct menudialogdef g_MpEndscreenTeamGameOverMenuDialog;
extern struct menudialogdef g_MpEndscreenChallengeCompletedMenuDialog;
extern struct menudialogdef g_MpEndGameMenuDialog;

/* M-2 / M-3 (2026-04-19): destructive-action confirm popups registered as
 * WARNING_MODAL so their BeginPopupModal-based renderers participate in
 * structural dedup + ctx ownership. Mirrors the B-End-Game-Input change
 * that registered g_MpEndGameMenuDialog above.
 *   g_MissionAbortMenuDialog        — Abort Mission confirm (solo pause)
 *   g_CheatsConfirmUnlockMenuDialog — Unlock Everything confirm (cheats hub)
 * Both dialogdefs are defined in src/game/ but not exported via data.h. */
extern struct menudialogdef g_MissionAbortMenuDialog;
extern struct menudialogdef g_CheatsConfirmUnlockMenuDialog;

/* M-22 (2026-04-19): Solo mission endscreens defined in src/game/endscreen.c
 * and the Network menu dialog defined in port/src/net/netmenu.c are not
 * exported via data.h. Register them in the pool so the renderer can route
 * its ctx acquire through menupoolAcquireDialog(def, &g_CtxImGuiMenu) — the
 * pool then owns the push/pop pair and menupoolReleaseAll() cleans them up
 * on stage transition. */
extern struct menudialogdef g_SoloMissionEndscreenCompletedMenuDialog;
extern struct menudialogdef g_SoloMissionEndscreenFailedMenuDialog;
extern struct menudialogdef g_NetMenuDialog;

/* Firing Range difficulty picker -- defined in src/game/trainingmenus.c
 * but not exported via data.h.  Registered locally so the FR flow
 * (Weapon List -> Difficulty -> Pre-Game Info) gets proper pool dedup
 * on its own slot instead of colliding with MENU_TYPE_TRAINING. */
extern struct menudialogdef g_FrDifficultyMenuDialog;

/* Dialogdef→type registry. One entry per (def,type) pair. Capacity is
 * chosen to cover all ~70 data.h externs plus headroom for late-registered
 * mod dialogs. Linear scan is fine — registry is read-heavy but short. */
#define MENUPOOL_REGISTRY_CAP 96

typedef struct {
    const struct menudialogdef *def;
    menu_type_t type;
} menupool_reg_entry_t;

static menupool_reg_entry_t s_Registry[MENUPOOL_REGISTRY_CAP];
static s32 s_RegistryCount = 0;
static s32 s_Initialised = 0;

/* Per-type state. Indexed by menu_type_t. */
typedef struct {
    s32 active;
    const struct menudialogdef *def;  /* what opened this (may be NULL) */
    InputContext *owned_ctx;          /* ctx pushed+owned by this slot, or NULL */
    u32 generation;                   /* bumped each acquire, for diagnostics */
} menupool_slot_t;

static menupool_slot_t s_Pool[MENU_TYPE_COUNT];

/* Name table — must stay index-aligned with menu_type_t. */
static const char *const s_TypeNames[MENU_TYPE_COUNT] = {
    [MENU_TYPE_NONE]                = "none",
    [MENU_TYPE_MAIN_MENU]           = "main_menu",
    [MENU_TYPE_CI_OPTIONS]          = "ci_options",
    [MENU_TYPE_SOLO_MISSION]        = "solo_mission",
    [MENU_TYPE_SOLO_MISSION_PAUSE]  = "solo_mission_pause",
    [MENU_TYPE_SOLO_OPTIONS]        = "solo_options",
    [MENU_TYPE_ENDSCREEN_SOLO]      = "endscreen_solo",
    [MENU_TYPE_ENDSCREEN_MP]        = "endscreen_mp",
    [MENU_TYPE_CHEATS]              = "cheats",
    [MENU_TYPE_MP_SETUP]            = "mp_setup",
    [MENU_TYPE_MP_SETTINGS]         = "mp_settings",
    [MENU_TYPE_MP_SOUNDTRACK]       = "mp_soundtrack",
    [MENU_TYPE_MP_TUNES]            = "mp_tunes",
    [MENU_TYPE_MP_TEAMNAMES]        = "mp_teamnames",
    [MENU_TYPE_MP_ADVANCED]         = "mp_advanced",
    [MENU_TYPE_MP_PAUSE]            = "mp_pause",
    [MENU_TYPE_MP_PLAYER_CONFIG]    = "mp_player_config",
    [MENU_TYPE_MP_BOT_SETUP]        = "mp_bot_setup",
    [MENU_TYPE_MP_TEAM_SETUP]       = "mp_team_setup",
    [MENU_TYPE_CONTROL_DIAGRAM]     = "control_diagram",
    [MENU_TYPE_TRAINING]            = "training",
    [MENU_TYPE_FR_WEAPON_LIST]      = "fr_weapon_list",
    [MENU_TYPE_FR_DIFFICULTY]       = "fr_difficulty",
    [MENU_TYPE_FR_INFO]             = "fr_info",
    [MENU_TYPE_FR_RESULT]           = "fr_result",
    [MENU_TYPE_DT_LIST]             = "dt_list",
    [MENU_TYPE_DT_DETAILS]          = "dt_details",
    [MENU_TYPE_DT_RESULT]           = "dt_result",
    [MENU_TYPE_HT_LIST]             = "ht_list",
    [MENU_TYPE_HT_DETAILS]          = "ht_details",
    [MENU_TYPE_HT_RESULT]           = "ht_result",
    [MENU_TYPE_AGENT_SELECT]        = "agent_select",
    [MENU_TYPE_AGENT_CREATE]        = "agent_create",
    [MENU_TYPE_NETWORK]             = "network",
    [MENU_TYPE_CHALLENGES]          = "challenges",
    [MENU_TYPE_WARNING_MODAL]       = "warning_modal",
    [MENU_TYPE_ROOM]                = "room",
    [MENU_TYPE_CINEMA]              = "cinema",
    [MENU_TYPE_PAUSE_MENU]          = "pause_menu",
    [MENU_TYPE_MODDING_HUB]         = "modding_hub",
    [MENU_TYPE_THEME_EDITOR]        = "theme_editor",
    [MENU_TYPE_STATS_PANEL]         = "stats_panel",
    [MENU_TYPE_DEBUG_OVERLAY]       = "debug_overlay",
};

/* ---- Private helpers ---- */

static s32 typeInRange(menu_type_t t)
{
    return (t > MENU_TYPE_NONE && t < MENU_TYPE_COUNT);
}

/* Linear scan of registry. Returns MENU_TYPE_NONE if not found. */
static menu_type_t lookupType(const struct menudialogdef *def)
{
    if (!def) {
        return MENU_TYPE_NONE;
    }
    for (s32 i = 0; i < s_RegistryCount; i++) {
        if (s_Registry[i].def == def) {
            return s_Registry[i].type;
        }
    }
    return MENU_TYPE_NONE;
}

/* ---- Public API ---- */

const char *menupoolTypeName(menu_type_t type)
{
    if (type >= 0 && type < MENU_TYPE_COUNT && s_TypeNames[type]) {
        return s_TypeNames[type];
    }
    return "?";
}

s32 menupoolRegisterDialogdef(const struct menudialogdef *def, menu_type_t type)
{
    if (!def || !typeInRange(type)) {
        return 0;
    }

    /* Duplicate registration → update to new type and log. Callers
     * shouldn't rely on this, but it's safer than silently retaining
     * a stale mapping. */
    for (s32 i = 0; i < s_RegistryCount; i++) {
        if (s_Registry[i].def == def) {
            if (s_Registry[i].type != type) {
                sysLogPrintf(LOG_WARNING,
                    "MENUPOOL: dialogdef %p re-registered, %s → %s",
                    (const void *)def,
                    menupoolTypeName(s_Registry[i].type),
                    menupoolTypeName(type));
                s_Registry[i].type = type;
            }
            return 1;
        }
    }

    if (s_RegistryCount >= MENUPOOL_REGISTRY_CAP) {
        sysLogPrintf(LOG_ERROR,
            "MENUPOOL: registry full (cap=%d), cannot register %s",
            MENUPOOL_REGISTRY_CAP, menupoolTypeName(type));
        return 0;
    }

    s_Registry[s_RegistryCount].def = def;
    s_Registry[s_RegistryCount].type = type;
    s_RegistryCount++;
    return 1;
}

menu_type_t menupoolTypeForDialogdef(const struct menudialogdef *def)
{
    return lookupType(def);
}

s32 menupoolAcquire(menu_type_t type, const struct menudialogdef *def, InputContext *ctx)
{
    if (!typeInRange(type)) {
        return -1;
    }

    menupool_slot_t *slot = &s_Pool[type];

    /* S300: when the slot is already active (typical flow — menuPushDialog
     * pre-acquired with ctx=NULL, renderer's IsWindowAppearing re-acquires
     * with a real ctx), we MUST still honour the ctx argument. Previously
     * this returned 0 without touching ctx, forcing each ImGui renderer
     * to own its own `s_FooPushedCtx` bool. Now the pool itself attaches
     * the ctx to the live slot if:
     *   (a) the caller provided a ctx,
     *   (b) the slot hasn't already attached one (owned_ctx == NULL),
     *   (c) the ctx isn't already live on the stack (shared mode).
     * Return 0 still signals "slot was already active" so menuPushDialog's
     * dedup rejection still works. */
    if (slot->active) {
        if (ctx && !slot->owned_ctx && !inputCtxIsActive(ctx)) {
            inputCtxPush(ctx);
            slot->owned_ctx = ctx;
            sysLogPrintf(LOG_NOTE,
                "MENUPOOL: attached ctx to active %s gen=%u ctx=%s",
                menupoolTypeName(type),
                (unsigned)slot->generation,
                ctx->name ? ctx->name : "?");
        }
        return 0;
    }

    slot->active = 1;
    slot->def = def;
    slot->owned_ctx = NULL;
    slot->generation++;

    if (ctx) {
        if (!inputCtxIsActive(ctx)) {
            /* Only take ownership of the ctx push when WE perform it.
             * If the context is already live (pushed externally or by
             * another pool slot), we just attach to it passively —
             * release will not pop it. This is important because
             * several menus legitimately share g_CtxImGuiMenu: the main
             * menu pushes it once, then modding-hub / theme-editor /
             * etc. open under the same context. Only the first opener
             * owns the pop. */
            inputCtxPush(ctx);
            slot->owned_ctx = ctx;
        }
    }

    sysLogPrintf(LOG_NOTE,
        "MENUPOOL: acquired %s gen=%u def=%p ctx=%s(%s)",
        menupoolTypeName(type),
        (unsigned)slot->generation,
        (const void *)def,
        ctx ? (ctx->name ? ctx->name : "?") : "none",
        slot->owned_ctx ? "owned" : "shared");

    return 1;
}

s32 menupoolRelease(menu_type_t type)
{
    if (!typeInRange(type)) {
        return -1;
    }

    menupool_slot_t *slot = &s_Pool[type];
    if (!slot->active) {
        /* Already free — idempotent release, no warning. */
        return 0;
    }

    InputContext *ctx = slot->owned_ctx;
    u32 gen = slot->generation;

    /* Clear slot FIRST so that any callback triggered by the context
     * pop (on_pop handlers, deferred flush) reads the pool as already
     * released. This makes re-entry during teardown safe. */
    slot->active = 0;
    slot->def = NULL;
    slot->owned_ctx = NULL;

    if (ctx) {
        if (inputCtxIsActive(ctx)) {
            inputCtxPopDeferred(ctx);
        }
        /* If ctx has already been popped externally (force-close race),
         * inputCtxPopDeferred would warn; skip the call to keep logs clean. */
    }

    sysLogPrintf(LOG_NOTE,
        "MENUPOOL: released %s gen=%u ctx=%s",
        menupoolTypeName(type), (unsigned)gen,
        ctx ? (ctx->name ? ctx->name : "?") : "none");

    return 1;
}

s32 menupoolIsActive(menu_type_t type)
{
    if (!typeInRange(type)) {
        return 0;
    }
    return s_Pool[type].active;
}

/* B-192 leak-class guard: track the last unregistered dialogdef that took
 * ownership of a context push through the fallback path, so the mirror
 * release can actually pop it instead of silently leaking. Single slot is
 * sufficient — unregistered dialogs are diagnostic footguns, not a stacking
 * use case. A WARNING fires on every unregistered acquire that pushes a
 * ctx to surface the missing registration. */
static const struct menudialogdef *s_UnregisteredOwnedDef = NULL;
static InputContext *s_UnregisteredOwnedCtx = NULL;

s32 menupoolAcquireDialog(const struct menudialogdef *def, InputContext *ctx)
{
    menu_type_t type = lookupType(def);
    if (type == MENU_TYPE_NONE) {
        /* Unregistered dialogdef — fall through with no pool dedup
         * enforcement. Treat as success so legacy flows are unaffected.
         * Optionally push the context so callers get the same behavior
         * as registered paths. */
        if (ctx && !inputCtxIsActive(ctx)) {
            inputCtxPush(ctx);
            /* B-192: remember the (def, ctx) pair so the release mirror can
             * pop it. Without this, the push silently leaks. */
            s_UnregisteredOwnedDef = def;
            s_UnregisteredOwnedCtx = ctx;
            sysLogPrintf(LOG_WARNING,
                "MENUPOOL: unregistered dialogdef %p pushed ctx '%s' without pool dedup — add to menupoolInit to avoid ctx leak",
                (const void *)def,
                ctx->name ? ctx->name : "?");
        }
        return 1;
    }
    return menupoolAcquire(type, def, ctx);
}

s32 menupoolReleaseDialog(const struct menudialogdef *def)
{
    menu_type_t type = lookupType(def);
    if (type == MENU_TYPE_NONE) {
        /* B-192: if the matching acquire pushed a ctx through the
         * unregistered fallback, pop it here so input returns to gameplay.
         * Match on both def and the recorded ctx so a late release doesn't
         * double-pop after another dialog has taken ownership. */
        if (def != NULL && s_UnregisteredOwnedDef == def && s_UnregisteredOwnedCtx) {
            InputContext *ctx = s_UnregisteredOwnedCtx;
            s_UnregisteredOwnedDef = NULL;
            s_UnregisteredOwnedCtx = NULL;
            if (inputCtxIsActive(ctx)) {
                inputCtxPopDeferred(ctx);
                sysLogPrintf(LOG_NOTE,
                    "MENUPOOL: popped unregistered ctx '%s' on release (def=%p)",
                    ctx->name ? ctx->name : "?",
                    (const void *)def);
            }
        }
        return 0;
    }
    return menupoolRelease(type);
}

s32 menupoolIsDialogActive(const struct menudialogdef *def)
{
    menu_type_t type = lookupType(def);
    if (type == MENU_TYPE_NONE) {
        return 0;
    }
    return menupoolIsActive(type);
}

const struct menudialogdef *menupoolDialogDef(const struct menudialog *dialog)
{
    /* This file is C and includes types.h, so struct menudialog is complete
     * here. C++ renderers can't include types.h without breaking `bool`, so
     * we expose this tiny accessor instead. */
    if (!dialog) {
        return NULL;
    }
    return dialog->definition;
}

s32 menupoolCountActive(void)
{
    s32 n = 0;
    for (s32 i = MENU_TYPE_NONE + 1; i < MENU_TYPE_COUNT; i++) {
        if (s_Pool[i].active) {
            n++;
        }
    }
    return n;
}

s32 menupoolDebugCopyActive(MenupoolDebugEntry *out, s32 maxEntries)
{
    if (!out || maxEntries <= 0) {
        return 0;
    }
    s32 w = 0;
    for (s32 i = MENU_TYPE_NONE + 1; i < MENU_TYPE_COUNT && w < maxEntries; i++) {
        const menupool_slot_t *slot = &s_Pool[i];
        if (!slot->active) {
            continue;
        }
        out[w].type = (menu_type_t)i;
        out[w].type_name = menupoolTypeName((menu_type_t)i);
        out[w].generation = slot->generation;
        out[w].def_ptr = slot->def;
        if (slot->owned_ctx) {
            out[w].owned_ctx_name = slot->owned_ctx->name ? slot->owned_ctx->name : "?";
        } else {
            out[w].owned_ctx_name = "shared/none";
        }
        w++;
    }
    return w;
}

void menupoolReleaseAll(void)
{
    s32 released = 0;
    for (s32 i = MENU_TYPE_NONE + 1; i < MENU_TYPE_COUNT; i++) {
        if (s_Pool[i].active) {
            if (menupoolRelease((menu_type_t)i) == 1) {
                released++;
            }
        }
    }
    if (released > 0) {
        sysLogPrintf(LOG_NOTE, "MENUPOOL: released %d slot(s) (bulk)", released);
    }
}

void menupoolDumpActive(void)
{
    s32 n = 0;
    for (s32 i = MENU_TYPE_NONE + 1; i < MENU_TYPE_COUNT; i++) {
        const menupool_slot_t *slot = &s_Pool[i];
        if (slot->active) {
            sysLogPrintf(LOG_NOTE,
                "MENUPOOL dump: [%s] gen=%u def=%p ctx=%s",
                menupoolTypeName((menu_type_t)i),
                (unsigned)slot->generation,
                (const void *)slot->def,
                slot->owned_ctx
                    ? (slot->owned_ctx->name ? slot->owned_ctx->name : "?")
                    : "shared/none");
            n++;
        }
    }
    if (n == 0) {
        sysLogPrintf(LOG_NOTE, "MENUPOOL dump: (no active slots)");
    }
}

/* ---- Built-in registration table ----
 *
 * Maps the canonical data.h dialogdefs to their pool types. Sticks to
 * the dialogdefs that can actually be pushed at runtime (excludes 2P
 * splitscreen and PAK-device variants — those are dead on PC per
 * 2026-04-10 no-local-multiplayer constraint).
 *
 * Unregistered dialogs pass through acquire with no dedup enforcement,
 * so this table doesn't need to be exhaustive to be correct — only
 * exhaustive to be MAXIMALLY protective. Dialogs whose push-path is
 * known to collide structurally belong here.
 *
 * The macro form is just to keep the list readable. If you rename a
 * dialogdef, update data.h AND this table together. */

#define REG(defptr, type) menupoolRegisterDialogdef(defptr, type)

void menupoolInit(void)
{
    if (s_Initialised) {
        return;
    }

    memset(s_Pool, 0, sizeof(s_Pool));
    memset(s_Registry, 0, sizeof(s_Registry));
    s_RegistryCount = 0;

    /* ---- Main menu family (CI free-roam + pause variants) ---- */
    REG(&g_CiMenuViaPcMenuDialog,        MENU_TYPE_MAIN_MENU);
    REG(&g_CiMenuViaPauseMenuDialog,     MENU_TYPE_MAIN_MENU);
    REG(&g_MainMenu4MbMenuDialog,        MENU_TYPE_MAIN_MENU);

    /* ---- CI Options subtree (nextsibling of main menu) ---- */
    /* S311: full CI Options family registered so renderCiSettingsRedirect
     * can claim the pool slot + own ctx on push, matching cheats/mpsetup.
     * P2 variants are dead but still registered so renderCiDeadPlayer2's
     * menuPopDialog() cascade releases the slot cleanly. */
    REG(&g_CiControlStyleMenuDialog,         MENU_TYPE_CI_OPTIONS);
    REG(&g_CiOptionsViaPcMenuDialog,         MENU_TYPE_CI_OPTIONS);
    REG(&g_CiOptionsViaPauseMenuDialog,      MENU_TYPE_CI_OPTIONS);
    REG(&g_CiControlOptionsMenuDialog,       MENU_TYPE_CI_OPTIONS);
    REG(&g_CiDisplayMenuDialog,              MENU_TYPE_CI_OPTIONS);
    REG(&g_CiControlStylePlayer2MenuDialog,  MENU_TYPE_CI_OPTIONS);
    REG(&g_CiDisplayPlayer2MenuDialog,       MENU_TYPE_CI_OPTIONS);
    REG(&g_CiControlPlayer2MenuDialog,       MENU_TYPE_CI_OPTIONS);

    /* ---- Cinema list (cutscene viewer) ---- */
    REG(&g_CinemaMenuDialog,             MENU_TYPE_CINEMA);

    /* ---- Solo mission ---- */
    REG(&g_PreAndPostMissionBriefingMenuDialog, MENU_TYPE_SOLO_MISSION);
    REG(&g_SoloMissionPauseMenuDialog,   MENU_TYPE_SOLO_MISSION_PAUSE);
    REG(&g_SoloMissionControlStyleMenuDialog, MENU_TYPE_SOLO_OPTIONS);

    /* ---- Endscreen (MP variants — Solo endscreens use different defs
     * registered via hotswap type fallbacks).
     *
     * B-End-Game-Input (2026-04-19): the Ind / Team / ChallengeCompleted
     * defs were previously unregistered, which routed their push through
     * the unregistered fallback in menupoolAcquireDialog. Since
     * menuPushRootDialog calls menupoolReleaseAll() before pushing, any
     * ctx popped by that call was deferred to end-of-frame; on the
     * renderer's first frame the ctx still appeared active so the fresh-
     * entry ctx-push was skipped, and on the NEXT frame freshEntry was
     * false — so the push never happened, and input to the endscreen
     * died. Registering here puts the endscreen on the same ctx-owned
     * acquire/release lifecycle as the Cheated/Failed variants, and the
     * renderer can drop its timing-fragile fresh-entry probe. */
    REG(&g_MpEndscreenIndGameOverMenuDialog,      MENU_TYPE_ENDSCREEN_MP);
    REG(&g_MpEndscreenTeamGameOverMenuDialog,     MENU_TYPE_ENDSCREEN_MP);
    REG(&g_MpEndscreenChallengeCompletedMenuDialog, MENU_TYPE_ENDSCREEN_MP);
    REG(&g_MpEndscreenChallengeCheatedMenuDialog, MENU_TYPE_ENDSCREEN_MP);
    REG(&g_MpEndscreenChallengeFailedMenuDialog,  MENU_TYPE_ENDSCREEN_MP);

    /* M-22: Solo endscreen roots share MENU_TYPE_ENDSCREEN_SOLO so the
     * pool owns the ctx push/pop alongside the MP variants. Before this,
     * renderSoloEndscreen took the unregistered fallback in
     * menupoolAcquireDialog, leaving the ctx leak-prone on force-close. */
    REG(&g_SoloMissionEndscreenCompletedMenuDialog, MENU_TYPE_ENDSCREEN_SOLO);
    REG(&g_SoloMissionEndscreenFailedMenuDialog,    MENU_TYPE_ENDSCREEN_SOLO);

    /* ---- End Game confirm (DANGER popup) ----
     * B-End-Game-Input: routes through the shared WARNING_MODAL slot so
     * the confirm popup participates in structural dedup + ctx ownership.
     * renderMpEndGameDialog uses BeginPopupModal under this acquire. */
    REG(&g_MpEndGameMenuDialog,                   MENU_TYPE_WARNING_MODAL);

    /* ---- M-2 / M-3 destructive-action confirm popups ----
     * Registered alongside End Game so their BeginPopupModal renderers
     * behave consistently: one instance per type, pool owns ctx push/pop. */
    REG(&g_MissionAbortMenuDialog,                MENU_TYPE_WARNING_MODAL);
    REG(&g_CheatsConfirmUnlockMenuDialog,         MENU_TYPE_WARNING_MODAL);

    /* ---- Cheats ---- */
    REG(&g_CheatsMenuDialog,             MENU_TYPE_CHEATS);

    /* ---- MP setup (arena / scenario / weapons / limits) ---- */
    REG(&g_MpArenaMenuDialog,            MENU_TYPE_MP_SETUP);
    REG(&g_MpScenarioMenuDialog,         MENU_TYPE_MP_SETUP);
    REG(&g_MpQuickTeamScenarioMenuDialog, MENU_TYPE_MP_SETUP);
    REG(&g_MpWeaponsMenuDialog,          MENU_TYPE_MP_SETUP);
    REG(&g_MpLimitsMenuDialog,           MENU_TYPE_MP_SETUP);
    REG(&g_MpCombatOptionsMenuDialog,    MENU_TYPE_MP_SETUP);
    REG(&g_HtbOptionsMenuDialog,         MENU_TYPE_MP_SETUP);
    REG(&g_CtcOptionsMenuDialog,         MENU_TYPE_MP_SETUP);
    REG(&g_KohOptionsMenuDialog,         MENU_TYPE_MP_SETUP);
    REG(&g_HtmOptionsMenuDialog,         MENU_TYPE_MP_SETUP);
    REG(&g_PacOptionsMenuDialog,         MENU_TYPE_MP_SETUP);

    /* ---- MP settings family (handicap / soundtrack / tunes / teamnames)
     * each sub-dialog gets its own pool slot because soundtrack → tunes
     * (and handicap → tunes) can legitimately stack. See S-3 audit. */
    REG(&g_MpHandicapsMenuDialog,        MENU_TYPE_MP_SETTINGS);
    REG(&g_MpSoundtrackMenuDialog,       MENU_TYPE_MP_SOUNDTRACK);
    REG(&g_MpSelectTunesMenuDialog,      MENU_TYPE_MP_TUNES);
    REG(&g_MpTeamNamesMenuDialog,        MENU_TYPE_MP_TEAMNAMES);

    /* ---- MP advanced (quick-go / quick-team / advanced hub) ---- */
    REG(&g_MpAdvancedSetupMenuDialog,    MENU_TYPE_MP_ADVANCED);
    REG(&g_MpQuickGoMenuDialog,          MENU_TYPE_MP_ADVANCED);
    REG(&g_MpQuickTeamMenuDialog,        MENU_TYPE_MP_ADVANCED);
    REG(&g_MpQuickTeamGameSetupMenuDialog, MENU_TYPE_MP_ADVANCED);
    REG(&g_MpQuickGo4MbMenuDialog,       MENU_TYPE_MP_ADVANCED);
    REG(&g_MpConfirmChallenge4MbMenuDialog, MENU_TYPE_MP_ADVANCED);
    REG(&g_AdvancedSetup4MbMenuDialog,   MENU_TYPE_MP_ADVANCED);
    REG(&g_MpChallengeListOrDetailsMenuDialog, MENU_TYPE_MP_ADVANCED);
    REG(&g_MpChallengeListOrDetailsViaAdvChallengeMenuDialog, MENU_TYPE_MP_ADVANCED);

    /* ---- MP in-match pause ---- */
    REG(&g_MpControlMenuDialog,          MENU_TYPE_MP_PAUSE);
    REG(&g_MpDropOutMenuDialog,          MENU_TYPE_MP_PAUSE);

    /* ---- MP player config ---- */
    REG(&g_MpPlayerOptionsMenuDialog,    MENU_TYPE_MP_PLAYER_CONFIG);
    REG(&g_MpPlayerStatsMenuDialog,      MENU_TYPE_MP_PLAYER_CONFIG);
    REG(&g_MpPlayerNameMenuDialog,       MENU_TYPE_MP_PLAYER_CONFIG);
    REG(&g_MpLoadSettingsMenuDialog,     MENU_TYPE_MP_PLAYER_CONFIG);
    REG(&g_MpLoadPresetMenuDialog,       MENU_TYPE_MP_PLAYER_CONFIG);
    REG(&g_MpLoadPlayerMenuDialog,       MENU_TYPE_MP_PLAYER_CONFIG);

    /* ---- MP bot setup ---- */
    REG(&g_MpSimulantsMenuDialog,        MENU_TYPE_MP_BOT_SETUP);
    REG(&g_MpEditSimulant4MbMenuDialog,  MENU_TYPE_MP_BOT_SETUP);

    /* ---- MP team setup ---- */
    REG(&g_MpTeamsMenuDialog,            MENU_TYPE_MP_TEAM_SETUP);

    /* ---- Training (FR / DT / HT / Bio / Hangar) ----
     *
     * FR / DT / HT sub-dialogs use dedicated pool types because each flow
     * legitimately stacks (List -> Details, or Weapon List -> Difficulty
     * -> Pre-Game Info for FR).  Sharing MENU_TYPE_TRAINING caused the
     * second push to be rejected by pool dedup ("pool slot [training]
     * already active"), silently breaking the drill-in.  See B-200 for
     * the FR case and the DT/HT follow-up note in tasks-current.md.
     *
     * g_FrWeaponsAvailableMenuDialog is a separate in-mission dialog
     * (tag 0x1b) and stays under MENU_TYPE_TRAINING.  Bio / Hangar have
     * no stacked Details dialog at this time. */
    REG(&g_FrWeaponListMenuDialog,          MENU_TYPE_FR_WEAPON_LIST);
    REG(&g_FrDifficultyMenuDialog,          MENU_TYPE_FR_DIFFICULTY);
    REG(&g_FrTrainingInfoPreGameMenuDialog, MENU_TYPE_FR_INFO);
    REG(&g_FrTrainingInfoInGameMenuDialog,  MENU_TYPE_FR_INFO);
    REG(&g_FrCompletedMenuDialog,           MENU_TYPE_FR_RESULT);
    REG(&g_FrFailedMenuDialog,              MENU_TYPE_FR_RESULT);
    REG(&g_FrWeaponsAvailableMenuDialog,    MENU_TYPE_TRAINING);
    REG(&g_DtListMenuDialog,                MENU_TYPE_DT_LIST);
    REG(&g_DtDetailsMenuDialog,             MENU_TYPE_DT_DETAILS);
    REG(&g_DtCompletedMenuDialog,           MENU_TYPE_DT_RESULT);
    REG(&g_DtFailedMenuDialog,              MENU_TYPE_DT_RESULT);
    REG(&g_HtListMenuDialog,                MENU_TYPE_HT_LIST);
    REG(&g_HtDetailsMenuDialog,             MENU_TYPE_HT_DETAILS);
    REG(&g_HtCompletedMenuDialog,           MENU_TYPE_HT_RESULT);
    REG(&g_HtFailedMenuDialog,              MENU_TYPE_HT_RESULT);
    REG(&g_BioListMenuDialog,               MENU_TYPE_TRAINING);
    REG(&g_HangarListMenuDialog,            MENU_TYPE_TRAINING);

    /* ---- Combat simulator top-level (acts as the MP lobby entry point) ---- */
    REG(&g_CombatSimulatorMenuDialog,    MENU_TYPE_MP_ADVANCED);

    /* ---- Filemgr / Pak (solo save flows — MP uses different paths) ----
     * B-192 (2026-04-19): `g_FilemgrFileSelectMenuDialog` (the non-4MB
     * Agent Select dialog pushed by `filemgrConsiderPushingFileSelectDialog`
     * on modern PC builds) was missing — only its 4MB twin was registered.
     * That routed every Agent Select open/close through the unregistered-
     * fallback path in menupoolAcquireDialog, which silently pushed
     * g_CtxImGuiMenu and the mirror release was a no-op — stranding input
     * on the menu IMC after Agent Select closed. */
    REG(&g_FilemgrFileSelectMenuDialog,  MENU_TYPE_AGENT_SELECT);
    REG(&g_FilemgrFileSelect4MbMenuDialog, MENU_TYPE_AGENT_SELECT);
    REG(&g_PakChoosePakMenuDialog,       MENU_TYPE_AGENT_SELECT);
    REG(&g_ChangeAgentMenuDialog,        MENU_TYPE_AGENT_SELECT);

    /* Dedicated "ready" dialog used by MP match-start — share slot with setup. */
    REG(&g_MpReadyMenuDialog,            MENU_TYPE_MP_SETUP);

    /* M-22: Multiplayer network menu (server browser + direct connect).
     * Rendered by pdgui_menu_network.cpp; pushed from the main menu's
     * "Network Game" item. Registering gives it structural dedup so the
     * main menu item cannot open a second copy, and ties it to cascade
     * close via menupoolReleaseAll() on stage transitions. */
    REG(&g_NetMenuDialog,                MENU_TYPE_NETWORK);

    /* All remaining dialogs (PAK device errors, 2P splitscreen variants,
     * and whatever else lives in src/game/ .c files) pass through
     * unregistered — acquire returns 1 with no dedup, release is no-op.
     * That preserves legacy behavior for any dialog we haven't explicitly
     * wired in. */

    s_Initialised = 1;
    sysLogPrintf(LOG_NOTE,
        "MENUPOOL: initialised (%d dialogdef registrations, %d types)",
        s_RegistryCount, (int)MENU_TYPE_COUNT);
}

#undef REG
