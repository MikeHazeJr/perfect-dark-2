/**
 * forge_runtime.c -- The Grid runtime instantiation layer.
 *
 * On forgeRuntimeEnterPlay (FREEFLY -> NORMAL):
 *   - SPAWN_POINT objects   -> injected into global spawn pool via
 *                              spawnPoolAppendForgePoints().
 *   - AI objects            -> allocated via botmgrAllocateBot() using
 *                              catalog-resolved body/head mp-indices.
 *   - Bot-tab requests      -> consumed per-tick in forgeRuntimeTick().
 *   - Props / weapons / zones -> logged; model / trigger wire deferred.
 *
 * On forgeRuntimeExitPlay (NORMAL -> FREEFLY):
 *   - Forge bots removed (botmgrRemoveAll).
 *   - Spawn pool rebuilt without forge-injected points.
 *   - Any directly-allocated props freed via propFree.
 */

#include "forge/forge_runtime.h"
#include "forge/forge_core.h"

#include <string.h>

#include "system.h"
#include "assetcatalog.h"

#include "game/botmgr.h"
#include "game/prop.h"
#include "game/spawnpool.h"
#include "game/bg.h"

#include "bss.h"
#include "data.h"
#include "types.h"
#include "constants.h"

/* ================================================================
 * Module state
 * ================================================================ */

static forge_prop_handle_t s_handles[FORGE_RUNTIME_MAX_HANDLES];
static s32                 s_handle_count;
static s32                 s_active;
static s32                 s_spawn_injected; /* number of points added to pool */
static s32                 s_bots_spawned;   /* running forge-bot count */

/* ================================================================
 * Handle registry helpers
 * ================================================================ */

static forge_prop_handle_t *s_alloc(u32 forge_uid)
{
    if (s_handle_count >= FORGE_RUNTIME_MAX_HANDLES) {
        sysLogPrintf(LOG_WARNING, "GRID.RUNTIME: handle pool full (max=%d)",
                FORGE_RUNTIME_MAX_HANDLES);
        return NULL;
    }
    forge_prop_handle_t *h = &s_handles[s_handle_count++];
    memset(h, 0, sizeof(*h));
    h->forge_uid = forge_uid;
    return h;
}

/* ================================================================
 * Bot config helpers
 * ================================================================ */

/*
 * Find an unused g_BotConfigsArray slot.  A slot is "free" when
 * body_id is empty -- the match-setup path fills it before use and
 * leaves it non-empty until the next full stage reset.
 * Walks from high indices so we don't stomp on match-setup-reserved
 * low slots.
 */
static s32 s_findFreeBotSlot(void)
{
    for (s32 i = MAX_BOTS - 1; i >= 0; --i) {
        if (g_BotConfigsArray[i].base.body_id[0] == '\0') {
            return i;
        }
    }
    return -1;
}

/*
 * Fill a g_BotConfigsArray slot with body/head/name derived from the
 * forge AI object.  Uses catalog mp_index (the established pattern from
 * matchsetup.c) for the legacy mpbodynum/mpheadnum fields that
 * botmgrAllocateBot still reads.
 */
static void s_fillBotSlot(s32 slot, const forge_object_t *o)
{
    struct mpbotconfig *bc  = &g_BotConfigsArray[slot];
    struct mpchrconfig *cfg = &bc->base;

    memset(bc, 0, sizeof(*bc));

    /* --- body --- */
    const char *body_id = (o->props.ai.body_id[0] != '\0')
                          ? o->props.ai.body_id
                          : "base:body_dd_guard";
    strncpy(cfg->body_id, body_id, sizeof(cfg->body_id) - 1);
    {
        const asset_entry_t *be = assetCatalogResolve(body_id);
        if (be && be->mp_index >= 0) {
            cfg->mpbodynum = (u8)be->mp_index;
        }
        /* mp_index == -1 falls through with mpbodynum == 0 (Dark Combat default). */
    }

    /* --- head --- */
    const char *head_id = (o->props.ai.head_id[0] != '\0')
                          ? o->props.ai.head_id
                          : "base:head_dd_guard";
    strncpy(cfg->head_id, head_id, sizeof(cfg->head_id) - 1);
    {
        const asset_entry_t *he = assetCatalogResolve(head_id);
        if (he && he->mp_index >= 0) {
            cfg->mpheadnum = (u8)he->mp_index;
        }
    }

    /* --- name (15 chars max including null) --- */
    if (o->label[0] != '\0') {
        strncpy(cfg->name, o->label, 14);
        cfg->name[14] = '\0';
    } else {
        strncpy(cfg->name, "Grid Bot", 14);
    }

    /* --- team --- */
    cfg->team = (u8)(o->team < 4 ? o->team : 0);

    /* --- difficulty: hostile=hard, neutral=normal, friendly=easy --- */
    switch (o->props.ai.faction) {
    case 1:  bc->difficulty = BOTDIFF_HARD;   break;
    case 2:  bc->difficulty = BOTDIFF_NORMAL; break;
    default: bc->difficulty = BOTDIFF_EASY;   break;
    }
}

/*
 * Attempt to allocate one AI forge object as a live bot.
 * Returns 1 on success (bot registered), 0 on failure.
 */
static s32 s_spawnBot(const forge_object_t *o)
{
    s32 slot = s_findFreeBotSlot();
    if (slot < 0) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: no free bot config slot for AI uid=%u '%s'",
                o->uid, o->catalog_id);
        return 0;
    }

    s_fillBotSlot(slot, o);

    /* chrnum: use the current bot count as the sequential ID, matching how
     * setup.c allocates simulants (each gets a unique chrnum 0, 1, 2...). */
    s32 chrnum = (s32)g_BotCount;

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: spawn bot uid=%u slot=%d chrnum=%d body='%s' at (%.0f,%.0f,%.0f)",
            o->uid, slot, chrnum,
            g_BotConfigsArray[slot].base.body_id,
            o->pos[0], o->pos[1], o->pos[2]);

    botmgrAllocateBot(chrnum, slot);

    forge_prop_handle_t *h = s_alloc(o->uid);
    if (h) h->is_bot = 1;

    ++s_bots_spawned;
    return 1;
}

/* ================================================================
 * Public API
 * ================================================================ */

void forgeRuntimeEnterPlay(void)
{
    if (s_active) forgeRuntimeExitPlay();

    s_handle_count  = 0;
    s_spawn_injected = 0;
    s_bots_spawned   = 0;
    s_active         = 1;

    s32 total = forgeObjectCount();
    if (total == 0) {
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: enterPlay -- no objects placed, nothing to instantiate");
        return;
    }

    /* Collect forge spawn points for bulk injection after the walk. */
    struct coord spawn_pos[SPAWNPOOL_MAX];
    f32          spawn_facing[SPAWNPOOL_MAX];
    s32          n_spawns  = 0;
    s32          n_bots    = 0;
    s32          n_deferred = 0;

    for (s32 i = 0; i < FORGE_MAX_OBJECTS; ++i) {
        forge_object_t *o = forgeObjectGet(i);
        if (!o || !o->in_use) continue;
        if (!o->enabled) continue;

        struct coord pos = { o->pos[0], o->pos[1], o->pos[2] };

        switch ((forge_category_t)o->category) {

        case FORGE_CAT_SPAWN_POINT:
            if (n_spawns < SPAWNPOOL_MAX) {
                spawn_pos[n_spawns]    = pos;
                spawn_facing[n_spawns] = o->props.spawn.facing_deg * 0.017453292519943f;
                ++n_spawns;

                forge_prop_handle_t *h = s_alloc(o->uid);
                if (h) h->is_spawn_point = 1;
            } else {
                sysLogPrintf(LOG_WARNING,
                        "GRID.RUNTIME: spawn point uid=%u dropped -- pool full",
                        o->uid);
            }
            break;

        case FORGE_CAT_AI:
            n_bots += s_spawnBot(o);
            break;

        /* Deferred categories: logged so the author knows what will be wired later. */
        case FORGE_CAT_WEAPON_PAD:
            ++n_deferred;
            sysLogPrintf(LOG_NOTE,
                    "GRID.RUNTIME: weapon pad '%s' uid=%u at (%.0f,%.0f,%.0f) -- model wire deferred",
                    o->catalog_id, o->uid, pos.x, pos.y, pos.z);
            break;

        case FORGE_CAT_PROP:
        case FORGE_CAT_GEOMETRY:
            ++n_deferred;
            sysLogPrintf(LOG_NOTE,
                    "GRID.RUNTIME: prop/geo '%s' uid=%u at (%.0f,%.0f,%.0f) -- model wire deferred",
                    o->catalog_id, o->uid, pos.x, pos.y, pos.z);
            break;

        case FORGE_CAT_ZONE:
            ++n_deferred;
            sysLogPrintf(LOG_NOTE,
                    "GRID.RUNTIME: zone '%s' uid=%u at (%.0f,%.0f,%.0f) -- trigger wire deferred",
                    o->catalog_id, o->uid, pos.x, pos.y, pos.z);
            break;

        case FORGE_CAT_INTERACTABLE:
            ++n_deferred;
            sysLogPrintf(LOG_NOTE,
                    "GRID.RUNTIME: interactable '%s' uid=%u at (%.0f,%.0f,%.0f) -- engine wire deferred",
                    o->catalog_id, o->uid, pos.x, pos.y, pos.z);
            break;

        default:
            break;
        }
    }

    /* Inject collected spawn points into the global pool. */
    if (n_spawns > 0) {
        s_spawn_injected = spawnPoolAppendForgePoints(spawn_pos, spawn_facing, n_spawns);
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: injected %d/%d forge spawn points into pool",
                s_spawn_injected, n_spawns);
    }

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: enterPlay -- spawns=%d bots=%d deferred=%d",
            n_spawns, n_bots, n_deferred);
}

void forgeRuntimeExitPlay(void)
{
    if (!s_active) return;
    s_active = 0;

    /* Free any directly-allocated props (not bots, not spawn points). */
    s32 freed = 0;
    for (s32 i = 0; i < s_handle_count; ++i) {
        forge_prop_handle_t *h = &s_handles[i];
        if (h->prop && !h->is_bot && !h->is_spawn_point) {
            propFree(h->prop);
            h->prop = NULL;
            ++freed;
        }
    }

    /* Remove all forge-spawned bots.  In forge mode every bot is editor-
     * managed; they'll be recreated the next time the author enters NORMAL. */
    if (s_bots_spawned > 0) {
        botmgrRemoveAll();
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: removed %d forge bots via botmgrRemoveAll",
                s_bots_spawned);
    }

    /* Rebuild the spawn pool without forge-injected points. */
    if (s_spawn_injected > 0) {
        forge_map_settings_t *ms = forgeMapSettings();
        const char *stage_id = (ms && ms->base_stage_id[0] != '\0')
                               ? ms->base_stage_id
                               : "base:training";
        s32 needed = (g_MpNumChrs > 0) ? g_MpNumChrs : 8;
        spawnPoolBuildGlobal(stage_id, 0, needed);
        sysLogPrintf(LOG_NOTE,
                "GRID.RUNTIME: spawn pool rebuilt (forge points flushed)");
    }

    sysLogPrintf(LOG_NOTE,
            "GRID.RUNTIME: exitPlay -- freed=%d bots_removed=%d spawns_flushed=%d",
            freed, s_bots_spawned, s_spawn_injected);

    s_handle_count   = 0;
    s_spawn_injected = 0;
    s_bots_spawned   = 0;
}

void forgeRuntimeTick(void)
{
    if (!s_active) return;

    forge_bot_settings_t *bs = forgeBotSettings();

    /* Consume pending add-active (fighting) bot requests from the Bots tab. */
    while (bs->pending_add_active > 0) {
        --bs->pending_add_active;

        /* Build a synthetic forge_object_t from the tab's default body setting.
         * The uid is non-zero synthetic (won't collide with the forge pool). */
        forge_object_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.uid = 0xFEED0000u | (u32)(s_bots_spawned & 0xFFFF);
        tmp.category = FORGE_CAT_AI;
        tmp.enabled  = 1;
        tmp.team     = 0;
        strncpy(tmp.props.ai.body_id, bs->default_body_id[0] != '\0'
                    ? bs->default_body_id : "base:body_dd_guard",
                FORGE_ID_LEN - 1);
        strncpy(tmp.props.ai.head_id, "base:head_dd_guard", FORGE_ID_LEN - 1);
        tmp.props.ai.faction = 1; /* hostile */
        s_spawnBot(&tmp);
    }

    /* Consume pending add-frozen bot requests. */
    while (bs->pending_add_frozen > 0) {
        --bs->pending_add_frozen;
        forge_object_t tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.uid = 0xFEED8000u | (u32)(s_bots_spawned & 0xFFFF);
        tmp.category = FORGE_CAT_AI;
        tmp.enabled  = 1;
        strncpy(tmp.props.ai.body_id, bs->default_body_id[0] != '\0'
                    ? bs->default_body_id : "base:body_dd_guard",
                FORGE_ID_LEN - 1);
        strncpy(tmp.props.ai.head_id, "base:head_dd_guard", FORGE_ID_LEN - 1);
        tmp.props.ai.faction = 2; /* neutral */
        s_spawnBot(&tmp);
    }

    /* Consume remove-all request. */
    if (bs->pending_remove_all > 0) {
        bs->pending_remove_all = 0;
        if (s_bots_spawned > 0) {
            botmgrRemoveAll();
            sysLogPrintf(LOG_NOTE,
                    "GRID.RUNTIME: Bots-tab removeAll -- cleared %d forge bots",
                    s_bots_spawned);
            s_bots_spawned = 0;

            /* Compact handle registry -- drop bot entries. */
            for (s32 i = 0; i < s_handle_count; ) {
                if (s_handles[i].is_bot) {
                    s_handles[i] = s_handles[--s_handle_count];
                } else {
                    ++i;
                }
            }
        }
        bs->active_count = 0;
        bs->frozen_count = 0;
    }
}

struct prop *forgeRuntimeFindPropByUid(u32 uid)
{
    if (!uid) return NULL;
    for (s32 i = 0; i < s_handle_count; ++i) {
        if (s_handles[i].forge_uid == uid) return s_handles[i].prop;
    }
    return NULL;
}

void forgeRuntimeSpawnBotAt(u32 forge_uid)
{
    forge_object_t *o = forgeObjectFindByUid(forge_uid);
    if (!o) {
        sysLogPrintf(LOG_WARNING,
                "GRID.RUNTIME: spawnBotAt uid=%u -- object not found", forge_uid);
        return;
    }
    s_spawnBot(o);
}
