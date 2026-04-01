/**
 * netmanifest.c -- Match manifest construction (Phase B).
 *
 * Builds a match_manifest_t from the current match state on the server.
 * Called server-side after CLC_LOBBY_START has populated g_MpSetup,
 * g_NetClients[] (player body/head from settings), and g_BotConfigsArray[]
 * (bot body/head).
 *
 * Phase B: build + log only.  SVC_MATCH_MANIFEST is not sent until Phase C/E.
 *
 * Asset ID convention (Phase 0: canonical catalog IDs, no numeric aliases):
 *   body:      e.g. "base:dark_combat"      (ASSET_BODY canonical id)
 *   head:      e.g. "base:head_dark_combat" (ASSET_HEAD canonical id)
 *   stage:     e.g. "base:mp_felicity"      (ASSET_MAP canonical id)
 *   weapon:    e.g. "base:falcon2"          (ASSET_WEAPON canonical id)
 *   component: mod->id (from mod.json)
 * Synthetic fallback IDs (stage_0x%02x / weapon_%d) are only used when an
 * asset is not yet registered in the catalog (e.g., unrecognised mod stage).
 *
 * For catalog-registered entries, asset_entry_t.net_hash (CRC32) is used.
 * For synthetic fallback entries, s_fnv1a(id) is used as the net_hash.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "platform.h"
#include "types.h"
#include "bss.h"
#include "system.h"
#include "config.h"
#include "constants.h"
#include "game/setuputils.h"
#include "net/netmanifest.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "assetcatalog.h"
#include "assetcatalog_load.h"
#include "assetcatalog_deps.h"
#include "modmgr.h"

/* =========================================================================
 * Config — hard cap on manifest entries, overridable via pd.ini
 * ========================================================================= */

static s32 s_ManifestMaxEntries = MANIFEST_MAX_ENTRIES;

PD_CONSTRUCTOR static void manifestConfigInit(void)
{
    configRegisterInt("Debug.ManifestMaxEntries", &s_ManifestMaxEntries,
                      64, MANIFEST_MAX_ENTRIES);
}

s32 manifestGetMaxEntries(void)
{
    return s_ManifestMaxEntries;
}

void manifestSetMaxEntries(s32 n)
{
    if (n < 64) {
        n = 64;
    }
    if (n > MANIFEST_MAX_ENTRIES) {
        n = MANIFEST_MAX_ENTRIES;
    }
    s_ManifestMaxEntries = n;
}

/* =========================================================================
 * Manifests: server-side (rebuilt per match start) and client-side (received)
 * ========================================================================= */

match_manifest_t g_ServerManifest;
match_manifest_t g_ClientManifest;

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* FNV-1a hash of a C string — mirrors the static fnv1a() in assetcatalog.c */
static u32 s_fnv1a(const char *str)
{
    u32 h = 0x811c9dc5u;
    while (*str) {
        h ^= (u8)*str++;
        h *= 0x01000193u;
    }
    return h;
}

/**
 * Ensure m->entries has room for at least one more entry.
 * Allocates from MANIFEST_INITIAL_CAPACITY on first call; doubles on each
 * subsequent growth; refuses to exceed s_ManifestMaxEntries.
 * Returns 1 if there is now room, 0 if the cap would be exceeded.
 */
static s32 s_manifestGrow(match_manifest_t *m)
{
    s32 new_cap;
    match_manifest_entry_t *new_buf;

    if (!m->entries) {
        /* First allocation */
        new_cap = MANIFEST_INITIAL_CAPACITY;
        if (new_cap > s_ManifestMaxEntries) {
            new_cap = s_ManifestMaxEntries;
        }
        new_buf = (match_manifest_entry_t *)malloc(
            (size_t)new_cap * sizeof(match_manifest_entry_t));
        if (!new_buf) {
            sysLogPrintf(LOG_ERROR,
                         "MANIFEST: malloc failed (cap=%d)", new_cap);
            return 0;
        }
        m->entries  = new_buf;
        m->capacity = (u16)new_cap;
        return 1;
    }

    if ((s32)m->capacity >= s_ManifestMaxEntries) {
        /* Already at hard cap */
        return 0;
    }

    /* Double the capacity, capped at the configured max */
    new_cap = (s32)m->capacity * 2;
    if (new_cap > s_ManifestMaxEntries) {
        new_cap = s_ManifestMaxEntries;
    }

    new_buf = (match_manifest_entry_t *)realloc(
        m->entries, (size_t)new_cap * sizeof(match_manifest_entry_t));
    if (!new_buf) {
        sysLogPrintf(LOG_ERROR,
                     "MANIFEST: realloc failed (old_cap=%d new_cap=%d)",
                     (int)m->capacity, new_cap);
        return 0;
    }
    m->entries  = new_buf;
    m->capacity = (u16)new_cap;
    return 1;
}

/**
 * Deep-copy src into dst.
 * Allocates/grows dst->entries as needed.  After the call dst is an
 * independent copy of src and owns its own entries buffer.
 */
static void s_manifestCopyInto(match_manifest_t *dst,
                                const match_manifest_t *src)
{
    s32 n;
    match_manifest_entry_t *new_buf;
    s32 new_cap;

    if (!src || src->num_entries == 0) {
        if (dst->entries) {
            dst->num_entries   = 0;
            dst->manifest_hash = 0;
        }
        return;
    }

    n = (s32)src->num_entries;

    /* Grow dst until it has room */
    if (!dst->entries || (s32)dst->capacity < n) {
        new_cap = n;
        if (new_cap < MANIFEST_INITIAL_CAPACITY) {
            new_cap = MANIFEST_INITIAL_CAPACITY;
        }
        free(dst->entries);
        new_buf = (match_manifest_entry_t *)malloc(
            (size_t)new_cap * sizeof(match_manifest_entry_t));
        if (!new_buf) {
            sysLogPrintf(LOG_ERROR,
                         "MANIFEST: copy malloc failed (n=%d)", n);
            dst->entries     = NULL;
            dst->capacity    = 0;
            dst->num_entries = 0;
            return;
        }
        dst->entries  = new_buf;
        dst->capacity = (u16)new_cap;
    }

    memcpy(dst->entries, src->entries,
           (size_t)n * sizeof(match_manifest_entry_t));
    dst->num_entries   = src->num_entries;
    dst->manifest_hash = src->manifest_hash;
}

/**
 * Ensure a manifest_diff_t sub-array has room for one more entry.
 * Lazily allocates from MANIFEST_INITIAL_CAPACITY and doubles each time.
 * Returns a pointer to the next free slot, or NULL if the cap is exceeded.
 */
static manifest_diff_entry_t *s_diffGrow(manifest_diff_entry_t **arr,
                                          s32 *count, s32 *cap)
{
    s32 new_cap;
    manifest_diff_entry_t *new_buf;

    if (*count < *cap) {
        return &(*arr)[(*count)++];
    }

    if (*cap <= 0) {
        new_cap = MANIFEST_INITIAL_CAPACITY;
    } else if (*cap >= s_ManifestMaxEntries) {
        return NULL;
    } else {
        new_cap = *cap * 2;
        if (new_cap > s_ManifestMaxEntries) {
            new_cap = s_ManifestMaxEntries;
        }
    }

    new_buf = (manifest_diff_entry_t *)realloc(
        *arr, (size_t)new_cap * sizeof(manifest_diff_entry_t));
    if (!new_buf) {
        sysLogPrintf(LOG_ERROR,
                     "MANIFEST: diff realloc failed (cap=%d->%d)",
                     *cap, new_cap);
        return NULL;
    }
    *arr = new_buf;
    *cap = new_cap;
    return &(*arr)[(*count)++];
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void manifestClear(match_manifest_t *m)
{
    /* Reset entry count but keep the allocated buffer for reuse. */
    m->num_entries   = 0;
    m->manifest_hash = 0;
    /* entries and capacity are left unchanged */
}

void manifestFree(match_manifest_t *m)
{
    free(m->entries);
    m->entries       = NULL;
    m->capacity      = 0;
    m->num_entries   = 0;
    m->manifest_hash = 0;
}

void manifestAddEntry(match_manifest_t *m, u32 net_hash, const char *id,
                      u8 type, u8 slot_index)
{
    s32 i;
    match_manifest_entry_t *e;

    /* Dedup by net_hash — skip if already present */
    for (i = 0; i < (s32)m->num_entries; i++) {
        if (m->entries[i].net_hash == net_hash) {
            return;
        }
    }

    /* Grow if at capacity (also handles first-time allocation) */
    if ((s32)m->num_entries >= (s32)m->capacity) {
        if (!s_manifestGrow(m)) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST: manifest full (%d entries max), dropping '%s'",
                         s_ManifestMaxEntries, id ? id : "?");
            return;
        }
    }

    e = &m->entries[m->num_entries++];
    e->net_hash   = net_hash;
    e->type       = type;
    e->slot_index = slot_index;
    if (id) {
        strncpy(e->id, id, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
    } else {
        e->id[0] = '\0';
    }
}

u32 manifestComputeHash(match_manifest_t *m)
{
    /* FNV-1a over (net_hash bytes, type, slot_index) for each entry in order */
    u32 h = 0x811c9dc5u;
    s32 i;
    for (i = 0; i < (s32)m->num_entries; i++) {
        const match_manifest_entry_t *e = &m->entries[i];
        /* Feed all 4 bytes of net_hash */
        h ^= (u8)(e->net_hash);        h *= 0x01000193u;
        h ^= (u8)(e->net_hash >> 8);   h *= 0x01000193u;
        h ^= (u8)(e->net_hash >> 16);  h *= 0x01000193u;
        h ^= (u8)(e->net_hash >> 24);  h *= 0x01000193u;
        h ^= e->type;                  h *= 0x01000193u;
        h ^= e->slot_index;            h *= 0x01000193u;
    }
    m->manifest_hash = h;
    return h;
}

/* =========================================================================
 * Phase 2: Dependency-graph expansion helpers
 *
 * When a BODY or HEAD catalog entry is added to a manifest, all deps
 * registered under that entry's catalog ID are automatically included
 * as MANIFEST_TYPE_ANIM or MANIFEST_TYPE_TEXTURE entries.
 *
 * Base-game (bundled) entries have no registered deps; catalogDepForEach()
 * is a no-op for them.  manifestAddEntry() deduplicates by net_hash so a
 * dep shared between two characters appears exactly once in the manifest.
 * ========================================================================= */

typedef struct {
    match_manifest_t *manifest;
    u8                slot_index;
} s_DepExpandCtx;

static u8 s_assetTypeToManifestType(asset_type_e atype)
{
    switch (atype) {
    case ASSET_ANIMATION: return MANIFEST_TYPE_ANIM;
    case ASSET_TEXTURE:   return MANIFEST_TYPE_TEXTURE;
    default:              return MANIFEST_TYPE_COMPONENT;
    }
}

static void s_manifestDepAddEntry(const char *dep_id, void *userdata)
{
    s_DepExpandCtx *ctx = (s_DepExpandCtx *)userdata;
    const asset_entry_t *de = assetCatalogResolve(dep_id);
    if (de) {
        u8 mtype = s_assetTypeToManifestType(de->type);
        manifestAddEntry(ctx->manifest, de->net_hash, de->id,
                         mtype, ctx->slot_index);
    }
    /* Unresolved dep_id is silently skipped — mod may be partially loaded */
}

static void s_manifestExpandDeps(match_manifest_t *m,
                                 const char *owner_id, u8 slot_index)
{
    s_DepExpandCtx ctx;
    ctx.manifest   = m;
    ctx.slot_index = slot_index;
    catalogDepForEach(owner_id, s_manifestDepAddEntry, &ctx);
}

/**
 * manifestBuild -- populate *out with all assets required for the upcoming match.
 *
 * room: hub_room_t pointer; currently unused (Phase B, no room scoping yet).
 *       Will be used in Phase E to scope manifest to a specific room's clients.
 * cfg:  matchconfig pointer; currently unused (Phase B reads g_MpSetup directly).
 *       Will be used in Phase E for client-side manifest building.
 *
 * Reads from global server state:
 *   g_MpSetup         -- stage, weapons, chrslots
 *   g_NetClients[]    -- player body/head (settings.body_id / settings.head_id)
 *   g_BotConfigsArray[] -- bot body/head (mpbodynum / mpheadnum)
 *   g_Lobby.settings.numSimulants -- bot count
 *   modmgrGetCount()  -- enabled mods (returns 0 on dedicated server stub)
 */
void manifestBuild(match_manifest_t *out, struct hub_room_s *room,
                   const struct matchconfig *cfg)
{
    s32 i;
    u8 slot_index;

    (void)room;  /* Phase E: will scope to room->clients[] */
    (void)cfg;   /* Phase E: will use cfg->stagenum, cfg->slots[], etc. */

    manifestClear(out);

    /* ---- Stage ---- */
    {
        const char *canon_id = catalogResolveStageByStagenum((s32)g_MpSetup.stagenum);
        const asset_entry_t *e = canon_id ? assetCatalogResolve(canon_id) : NULL;
        if (e) {
            manifestAddEntry(out, e->net_hash, e->id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            /* Fallback: stage not in catalog (unregistered mod stage) */
            char id[64];
            snprintf(id, sizeof(id), "stage_0x%02x", (unsigned)g_MpSetup.stagenum);
            manifestAddEntry(out, s_fnv1a(id), id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- Weapons ---- */
    for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
        const u8 wnum = g_MpSetup.weapons[i];
        const char *canon_id;
        const asset_entry_t *e;
        if (wnum == 0) {
            continue;
        }
        canon_id = catalogResolveWeaponByGameId((s32)wnum);
        e = canon_id ? assetCatalogResolve(canon_id) : NULL;
        if (e) {
            manifestAddEntry(out, e->net_hash, e->id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        } else {
            /* Fallback: weapon not in catalog (unknown weapon id) */
            char id[64];
            snprintf(id, sizeof(id), "weapon_%d", (int)wnum);
            manifestAddEntry(out, s_fnv1a(id), id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- Players: iterate connected clients ---- */
    slot_index = 0;
    for (i = 0; i < NET_MAX_CLIENTS; i++) {
        const struct netclient *ncl = &g_NetClients[i];
        if (ncl->state != CLSTATE_LOBBY && ncl->state != CLSTATE_GAME) {
            continue;
        }

        /* SA-3: use catalog string IDs directly from settings */
        {
            const asset_entry_t *be = assetCatalogResolve(ncl->settings.body_id);
            if (be) {
                manifestAddEntry(out, be->net_hash, be->id,
                                 MANIFEST_TYPE_BODY, slot_index);
                s_manifestExpandDeps(out, be->id, slot_index);
            } else {
                manifestAddEntry(out, s_fnv1a(ncl->settings.body_id), ncl->settings.body_id,
                                 MANIFEST_TYPE_BODY, slot_index);
            }
        }
        {
            const asset_entry_t *he = assetCatalogResolve(ncl->settings.head_id);
            if (he) {
                manifestAddEntry(out, he->net_hash, he->id,
                                 MANIFEST_TYPE_HEAD, slot_index);
                s_manifestExpandDeps(out, he->id, slot_index);
            } else {
                manifestAddEntry(out, s_fnv1a(ncl->settings.head_id), ncl->settings.head_id,
                                 MANIFEST_TYPE_HEAD, slot_index);
            }
        }

        slot_index++;
    }

    /* ---- Bots ---- */
    {
        const s32 num_bots = (s32)g_Lobby.settings.numSimulants;
        for (i = 0; i < num_bots && i < MAX_BOTS; i++) {
            const u8 bodynum = g_BotConfigsArray[i].base.mpbodynum;
            const u8 headnum = g_BotConfigsArray[i].base.mpheadnum;
            const char *body_canon = catalogResolveByRuntimeIndex(ASSET_BODY, (s32)bodynum);
            const char *head_canon = catalogResolveByRuntimeIndex(ASSET_HEAD, (s32)headnum);
            const asset_entry_t *be = body_canon ? assetCatalogResolve(body_canon) : NULL;
            const asset_entry_t *he = head_canon ? assetCatalogResolve(head_canon) : NULL;

            if (be) {
                manifestAddEntry(out, be->net_hash, be->id,
                                 MANIFEST_TYPE_BODY, slot_index);
                s_manifestExpandDeps(out, be->id, slot_index);
            } else {
                char body_id[64];
                snprintf(body_id, sizeof(body_id), "body_%d", (int)bodynum);
                manifestAddEntry(out, s_fnv1a(body_id), body_id,
                                 MANIFEST_TYPE_BODY, slot_index);
            }

            if (he) {
                manifestAddEntry(out, he->net_hash, he->id,
                                 MANIFEST_TYPE_HEAD, slot_index);
                s_manifestExpandDeps(out, he->id, slot_index);
            } else {
                char head_id[64];
                snprintf(head_id, sizeof(head_id), "head_%d", (int)headnum);
                manifestAddEntry(out, s_fnv1a(head_id), head_id,
                                 MANIFEST_TYPE_HEAD, slot_index);
            }

            slot_index++;
        }
    }

    /* ---- Mod components (returns 0 on dedicated server stub) ---- */
    {
        const s32 num_mods = modmgrGetCount();
        for (i = 0; i < num_mods; i++) {
            modinfo_t *mod = modmgrGetMod(i);
            if (!mod || !mod->enabled || mod->contenthash == 0) {
                continue;
            }
            manifestAddEntry(out, mod->contenthash, mod->id,
                             MANIFEST_TYPE_COMPONENT, MANIFEST_SLOT_MATCH);
        }
    }

    /* Seal: compute manifest-level hash over all entries */
    manifestComputeHash(out);
}

/**
 * manifestLog -- dump manifest contents to the system log (Phase B debug).
 *
 * Call after manifestBuild() to verify the manifest is correct before
 * the send path is wired in Phase C/E.
 */
void manifestLog(const match_manifest_t *m)
{
    s32 i;
    static const char *s_type_names[] = {
        "BODY", "HEAD", "STAGE", "WEAPON", "COMPONENT", "MODEL", "ANIM", "TEXTURE"
    };

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST: built — %d entries, hash=0x%08x",
                 (int)m->num_entries, (unsigned)m->manifest_hash);

    for (i = 0; i < (s32)m->num_entries; i++) {
        const match_manifest_entry_t *e = &m->entries[i];
        const char *type_name = (e->type < ARRAYCOUNT(s_type_names))
                                ? s_type_names[e->type] : "?";
        const char *slot_str = (e->slot_index == MANIFEST_SLOT_MATCH)
                               ? "MATCH" : NULL;
        if (slot_str) {
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST:   [%2d] %-9s hash=0x%08x slot=MATCH  id='%s'",
                         i, type_name, (unsigned)e->net_hash, e->id);
        } else {
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST:   [%2d] %-9s hash=0x%08x slot=%-3d   id='%s'",
                         i, type_name, (unsigned)e->net_hash,
                         (int)e->slot_index, e->id);
        }
    }
}

/* =========================================================================
 * SA-6: SP diff-based asset lifecycle
 * ========================================================================= */

/** Tracks which assets are currently marked LOADED for the active SP mission. */
match_manifest_t g_CurrentLoadedManifest;

/**
 * Static buffers used by manifestSPTransition().
 * Kept module-local to avoid large stack allocations in the caller.
 * Both start zero-initialised (entries == NULL).
 */
static match_manifest_t s_SpNeededManifest;
static manifest_diff_t  s_SpLastDiff;

void manifestBuildMission(s32 stagenum, match_manifest_t *out)
{
    char id[64];
    catalog_stage_result_t stage_result;
    const asset_entry_t   *be;
    const asset_entry_t   *he;

    manifestClear(out);

    /* ---- Stage ---- */
    {
        const char *stage_canon = catalogResolveStageByStagenum(stagenum);
        if (stage_canon && catalogResolveStage(stage_canon, &stage_result)
                && stage_result.entry) {
            manifestAddEntry(out, stage_result.net_hash, stage_result.entry->id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            /* Fallback: stage not in catalog (pre-scan or unregistered mod stage) */
            snprintf(id, sizeof(id), "stage_0x%02x", (unsigned)stagenum);
            manifestAddEntry(out, s_fnv1a(id), id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- SP player character: Joanna Dark (runtime body 0 / head 0) ---- */
    {
        const char *body0_id = catalogResolveByRuntimeIndex(ASSET_BODY, 0);
        const char *head0_id = catalogResolveByRuntimeIndex(ASSET_HEAD, 0);
        be = body0_id ? assetCatalogResolve(body0_id) : NULL;
        he = head0_id ? assetCatalogResolve(head0_id) : NULL;
    }
    if (be) {
        manifestAddEntry(out, be->net_hash, be->id, MANIFEST_TYPE_BODY, 0);
        s_manifestExpandDeps(out, be->id, 0);
    } else {
        manifestAddEntry(out, s_fnv1a("body_0"), "body_0", MANIFEST_TYPE_BODY, 0);
    }

    if (he) {
        manifestAddEntry(out, he->net_hash, he->id, MANIFEST_TYPE_HEAD, 0);
        s_manifestExpandDeps(out, he->id, 0);
    } else {
        manifestAddEntry(out, s_fnv1a("head_0"), "head_0", MANIFEST_TYPE_HEAD, 0);
    }

    /* ---- Stage characters and prop models from setup spawn list ----
     * g_StageSetup.props is NULL when called pre-load; the scan silently
     * skips in that case.  When invoked post-load (after filesetup converts
     * the setup file to host byte order) this enumerates all CHR entries for
     * body/head and all prop-object entries for their MODEL catalog IDs.
     * manifestSPTransition() should be called after setupLoadFiles() returns
     * so that both passes are meaningful. */
    if (g_StageSetup.props) {
        struct defaultobj *sobj = (struct defaultobj *)g_StageSetup.props;

        while (sobj->type != OBJTYPE_END) {
            if (sobj->type == OBJTYPE_CHR) {
                /* ---- character body / head ---- */
                const struct packedchr *chr = (const struct packedchr *)sobj;

                /* bodynum 255 = random; no fixed catalog entry to require */
                if (chr->bodynum != 255) {
                    const char *bcan = catalogResolveByRuntimeIndex(ASSET_BODY,
                                                                     (s32)chr->bodynum);
                    const asset_entry_t *cbe = bcan ? assetCatalogResolve(bcan) : NULL;
                    if (cbe) {
                        manifestAddEntry(out, cbe->net_hash, cbe->id,
                                         MANIFEST_TYPE_BODY, 0);
                        s_manifestExpandDeps(out, cbe->id, 0);
                    } else {
                        char bstr[64];
                        snprintf(bstr, sizeof(bstr), "body_%d", (int)chr->bodynum);
                        manifestAddEntry(out, s_fnv1a(bstr), bstr,
                                         MANIFEST_TYPE_BODY, 0);
                    }
                }

                /* headnum < 0 = holograph / special; no fixed catalog entry */
                if (chr->headnum >= 0) {
                    const char *hcan = catalogResolveByRuntimeIndex(ASSET_HEAD,
                                                                     (s32)chr->headnum);
                    const asset_entry_t *che = hcan ? assetCatalogResolve(hcan) : NULL;
                    if (che) {
                        manifestAddEntry(out, che->net_hash, che->id,
                                         MANIFEST_TYPE_HEAD, 0);
                        s_manifestExpandDeps(out, che->id, 0);
                    } else {
                        char hstr[64];
                        snprintf(hstr, sizeof(hstr), "head_%d", (int)chr->headnum);
                        manifestAddEntry(out, s_fnv1a(hstr), hstr,
                                         MANIFEST_TYPE_HEAD, 0);
                    }
                }
            } else {
                /* ---- prop model (types that embed struct defaultobj) ---- */
                const char *model_id;
                const asset_entry_t *me;

                switch (sobj->type) {
                case OBJTYPE_DOOR:
                case OBJTYPE_BASIC:
                case OBJTYPE_KEY:
                case OBJTYPE_ALARM:
                case OBJTYPE_CCTV:
                case OBJTYPE_AMMOCRATE:
                case OBJTYPE_WEAPON:
                case OBJTYPE_SINGLEMONITOR:
                case OBJTYPE_MULTIMONITOR:
                case OBJTYPE_HANGINGMONITORS:
                case OBJTYPE_AUTOGUN:
                case OBJTYPE_DEBRIS:
                case OBJTYPE_HAT:
                case OBJTYPE_MULTIAMMOCRATE:
                case OBJTYPE_SHIELD:
                case OBJTYPE_GASBOTTLE:
                case OBJTYPE_29:
                case OBJTYPE_TRUCK:
                case OBJTYPE_HELI:
                case OBJTYPE_GLASS:
                case OBJTYPE_SAFE:
                case OBJTYPE_TINTEDGLASS:
                case OBJTYPE_LIFT:
                case OBJTYPE_HOVERBIKE:
                case OBJTYPE_HOVERPROP:
                case OBJTYPE_FAN:
                case OBJTYPE_HOVERCAR:
                case OBJTYPE_CHOPPER:
                case OBJTYPE_MINE:
                case OBJTYPE_ESCASTEP:
                    model_id = catalogResolveByRuntimeIndex(ASSET_MODEL,
                                                            (s32)sobj->modelnum);
                    if (model_id) {
                        me = assetCatalogResolve(model_id);
                        if (me) {
                            manifestAddEntry(out, me->net_hash, me->id,
                                             MANIFEST_TYPE_MODEL,
                                             MANIFEST_SLOT_MATCH);
                        } else {
                            manifestAddEntry(out, s_fnv1a(model_id), model_id,
                                             MANIFEST_TYPE_MODEL,
                                             MANIFEST_SLOT_MATCH);
                        }
                    }
                    break;
                default:
                    break;
                }
            }

            sobj = (struct defaultobj *)((u32 *)sobj +
                                         setupGetCmdLength((u32 *)sobj));
        }
    }

    /* ---- Counter-op player body/head ---- */
    /* When a counter-operative player is active (antiplayernum >= 0), their
     * appearance is determined by g_Vars.antibodynum / g_Vars.antiheadnum —
     * these do not appear in the props spawn list, so they must be added here. */
    if (g_Vars.antiplayernum >= 0) {
        if (g_Vars.antibodynum >= 0) {
            const char *bcan = catalogResolveByRuntimeIndex(ASSET_BODY,
                                                             (s32)g_Vars.antibodynum);
            const asset_entry_t *cbe = bcan ? assetCatalogResolve(bcan) : NULL;
            if (cbe) {
                manifestAddEntry(out, cbe->net_hash, cbe->id,
                                 MANIFEST_TYPE_BODY, 1);
                s_manifestExpandDeps(out, cbe->id, 1);
            } else {
                char bstr[64];
                snprintf(bstr, sizeof(bstr), "body_%d", (int)g_Vars.antibodynum);
                manifestAddEntry(out, s_fnv1a(bstr), bstr,
                                 MANIFEST_TYPE_BODY, 1);
            }
        }

        if (g_Vars.antiheadnum >= 0) {
            const char *hcan = catalogResolveByRuntimeIndex(ASSET_HEAD,
                                                             (s32)g_Vars.antiheadnum);
            const asset_entry_t *che = hcan ? assetCatalogResolve(hcan) : NULL;
            if (che) {
                manifestAddEntry(out, che->net_hash, che->id,
                                 MANIFEST_TYPE_HEAD, 1);
                s_manifestExpandDeps(out, che->id, 1);
            } else {
                char hstr[64];
                snprintf(hstr, sizeof(hstr), "head_%d", (int)g_Vars.antiheadnum);
                manifestAddEntry(out, s_fnv1a(hstr), hstr,
                                 MANIFEST_TYPE_HEAD, 1);
            }
        }
    }

    manifestComputeHash(out);
}

void manifestDiff(const match_manifest_t *current,
                  const match_manifest_t *needed,
                  manifest_diff_t *out)
{
    s32 i;
    s32 j;
    s32 found;
    manifest_diff_entry_t *de;

    /* Zero counts; leave existing allocations in place for reuse */
    out->num_to_load   = 0;
    out->num_to_unload = 0;
    out->num_to_keep   = 0;

    /* Pass 1: walk needed — classify as to_load or to_keep */
    for (i = 0; i < (s32)needed->num_entries; i++) {
        const match_manifest_entry_t *ne = &needed->entries[i];
        found = 0;
        for (j = 0; j < (s32)current->num_entries; j++) {
            if (ne->net_hash == current->entries[j].net_hash) {
                found = 1;
                break;
            }
        }
        if (found) {
            de = s_diffGrow(&out->to_keep, &out->num_to_keep, &out->cap_to_keep);
            if (de) {
                de->net_hash = ne->net_hash;
                de->type     = ne->type;
                strncpy(de->id, ne->id, sizeof(de->id) - 1);
                de->id[sizeof(de->id) - 1] = '\0';
            }
        } else {
            de = s_diffGrow(&out->to_load, &out->num_to_load, &out->cap_to_load);
            if (de) {
                de->net_hash = ne->net_hash;
                de->type     = ne->type;
                strncpy(de->id, ne->id, sizeof(de->id) - 1);
                de->id[sizeof(de->id) - 1] = '\0';
            }
        }
    }

    /* Pass 2: walk current — anything not in needed goes to to_unload */
    for (i = 0; i < (s32)current->num_entries; i++) {
        const match_manifest_entry_t *ce = &current->entries[i];
        found = 0;
        for (j = 0; j < (s32)needed->num_entries; j++) {
            if (ce->net_hash == needed->entries[j].net_hash) {
                found = 1;
                break;
            }
        }
        if (!found) {
            de = s_diffGrow(&out->to_unload, &out->num_to_unload, &out->cap_to_unload);
            if (de) {
                de->net_hash = ce->net_hash;
                de->type     = ce->type;
                strncpy(de->id, ce->id, sizeof(de->id) - 1);
                de->id[sizeof(de->id) - 1] = '\0';
            }
        }
    }
}

void manifestDiffFree(manifest_diff_t *diff)
{
    free(diff->to_load);
    free(diff->to_unload);
    free(diff->to_keep);
    memset(diff, 0, sizeof(*diff));
}

void manifestApplyDiff(const match_manifest_t *needed,
                       manifest_diff_t *diff)
{
    s32 i;
    s32 load_ok;

    /* Unload: decrement ref_count for entries leaving the active manifest.
     * For bundled (base-game) assets catalogUnloadAsset is a no-op.
     * For mod assets, ref_count decrements and data is freed when it hits 0. */
    for (i = 0; i < diff->num_to_unload; i++) {
        if (diff->to_unload[i].id[0]) {
            catalogUnloadAsset(diff->to_unload[i].id);
            sysLogPrintf(LOG_NOTE, "MANIFEST-SP: unload '%s'",
                         diff->to_unload[i].id);
        }
    }

    /* Load: resolve and load entries entering the active manifest.
     * For bundled assets this is a no-op retain (already ROM-resident).
     * For mod assets, the file is read from disk and ref_count is incremented.
     * A missing asset logs a warning and is skipped — the game must not crash
     * if a mod file is absent; the intercept layer falls back to ROM. */
    for (i = 0; i < diff->num_to_load; i++) {
        if (diff->to_load[i].id[0]) {
            load_ok = catalogLoadAsset(diff->to_load[i].id);
            if (!load_ok) {
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST-SP: load failed '%s' — asset missing, skipping",
                             diff->to_load[i].id);
            } else {
                sysLogPrintf(LOG_NOTE, "MANIFEST-SP: load '%s'",
                             diff->to_load[i].id);
            }
        }
    }

    /* Deep-copy needed into g_CurrentLoadedManifest so it becomes the new
     * baseline for the next diff.  Cannot use a plain struct assignment since
     * both would alias the same entries pointer. */
    s_manifestCopyInto(&g_CurrentLoadedManifest, needed);

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-SP: applied diff — load=%d unload=%d keep=%d",
                 diff->num_to_load, diff->num_to_unload, diff->num_to_keep);
}

/* =========================================================================
 * Phase 4: Pre-validation pass
 * ========================================================================= */

/**
 * Callback context for dep-chain validation inside manifestValidate().
 * owner_id is logged in warnings so the user knows which entry has the
 * broken dep.  warn_count accumulates missing/disabled deps.
 */
typedef struct {
    const char *owner_id;
    s32         warn_count;
} s_ValidateDepCtx;

static void s_validateDepCallback(const char *dep_id, void *userdata)
{
    s_ValidateDepCtx *ctx = (s_ValidateDepCtx *)userdata;
    const asset_entry_t *de;

    de = assetCatalogResolve(dep_id);
    if (!de) {
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST-VALIDATE: WARN: dep '%s' of '%s'"
                     " not found in catalog",
                     dep_id, ctx->owner_id);
        ctx->warn_count++;
    } else if (!de->enabled) {
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST-VALIDATE: WARN: dep '%s' of '%s' is disabled",
                     dep_id, ctx->owner_id);
        ctx->warn_count++;
    }
}

s32 manifestValidate(manifest_diff_t *diff)
{
    s32 i;
    s32 invalid_count;
    manifest_diff_entry_t *entry;
    const asset_entry_t *e;
    s_ValidateDepCtx dep_ctx;

    if (!diff || diff->num_to_load == 0) {
        return 0;
    }

    invalid_count = 0;

    for (i = 0; i < diff->num_to_load; i++) {
        entry = &diff->to_load[i];

        if (!entry->id[0]) {
            continue; /* already cleared by a prior pass */
        }

        /* Try catalog lookup by string ID first, then net_hash as fallback
         * (net_hash lookup handles the rare case where ID was truncated or
         * the entry was re-registered under a different string form). */
        e = assetCatalogResolve(entry->id);
        if (!e) {
            e = assetCatalogResolveByNetHash(entry->net_hash);
        }

        if (!e) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-VALIDATE: WARN: entry '%s' not found"
                         " in catalog, skipping",
                         entry->id);
            entry->id[0] = '\0';
            invalid_count++;
            continue;
        }

        /* Disabled entries must not be loaded — the user toggled them off. */
        if (!e->enabled) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-VALIDATE: WARN: entry '%s' is disabled,"
                         " skipping",
                         entry->id);
            entry->id[0] = '\0';
            invalid_count++;
            continue;
        }

        /* Lang bank: must map to ASSET_LANG with a positive bank_id.
         * An ASSET_LANG entry with bank_id <= 0 has no backing LANGBANK_*
         * constant and langLoad() would silently do nothing. */
        if (entry->type == MANIFEST_TYPE_LANG) {
            if (e->type != ASSET_LANG) {
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST-VALIDATE: WARN: entry '%s' expected"
                             " ASSET_LANG but got type %d, skipping",
                             entry->id, (int)e->type);
                entry->id[0] = '\0';
                invalid_count++;
                continue;
            }
            if (e->ext.lang.bank_id <= 0) {
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST-VALIDATE: WARN: entry '%s' has"
                             " invalid bank_id %d, skipping",
                             entry->id, e->ext.lang.bank_id);
                entry->id[0] = '\0';
                invalid_count++;
                continue;
            }
        }

        /* Dependency chain: warn if any declared dep is unresolvable but
         * keep the parent entry — it can still load, just without the dep.
         * Base-game bundled entries have no deps registered (they are always
         * ROM-resident) so catalogDepForEach is a no-op for them. */
        dep_ctx.owner_id   = entry->id;
        dep_ctx.warn_count = 0;
        catalogDepForEach(entry->id, s_validateDepCallback, &dep_ctx);
        if (dep_ctx.warn_count > 0) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-VALIDATE: WARN: '%s' has %d unresolvable"
                         " dep(s) — loading parent, skipping missing deps",
                         entry->id, dep_ctx.warn_count);
        }
    }

    if (invalid_count > 0) {
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST-VALIDATE: %d of %d to-load entries invalid"
                     " and skipped",
                     invalid_count, diff->num_to_load);
    } else {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-VALIDATE: all %d to-load entries valid",
                     diff->num_to_load);
    }

    return invalid_count;
}

void manifestSPTransition(s32 stagenum)
{
    manifestBuildMission(stagenum, &s_SpNeededManifest);

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-SP: transition to stage 0x%02x — %d entries",
                 (unsigned)stagenum, (int)s_SpNeededManifest.num_entries);

    manifestDiff(&g_CurrentLoadedManifest, &s_SpNeededManifest, &s_SpLastDiff);
    manifestValidate(&s_SpLastDiff);
    manifestApplyDiff(&s_SpNeededManifest, &s_SpLastDiff);
    manifestDiffFree(&s_SpLastDiff);
}

/**
 * manifestMPTransition -- apply a diff-based transition for an MP match.
 *
 * Uses g_ClientManifest (populated when SVC_MATCH_MANIFEST was received) as
 * the "needed" manifest and diffs it against g_CurrentLoadedManifest.
 *
 * For each diff entry:
 *   to_load   -- catalogLoadAsset() (no-op for bundled; loads mod file from disk)
 *   to_unload -- catalogUnloadAsset() (no-op for bundled; decrements ref + frees at 0)
 *   to_keep   -- no action (already loaded, ref_count unchanged)
 *
 * After applying, g_CurrentLoadedManifest becomes the MP manifest, which
 * serves as the baseline for the next transition (e.g., match → SP mission).
 *
 * Call from mainChangeToStage() when g_ClientManifest is populated (MP mode).
 * manifestClear(&g_ClientManifest) should be called on returning to lobby so
 * that subsequent SP missions take the SP path, not a stale MP manifest.
 */
void manifestMPTransition(void)
{
    if (g_ClientManifest.num_entries == 0) {
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST-MP: no client manifest available, skipping transition");
        return;
    }

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-MP: transition — %d entries in client manifest",
                 (int)g_ClientManifest.num_entries);

    manifestDiff(&g_CurrentLoadedManifest, &g_ClientManifest, &s_SpLastDiff);
    manifestValidate(&s_SpLastDiff);
    manifestApplyDiff(&g_ClientManifest, &s_SpLastDiff);
    manifestDiffFree(&s_SpLastDiff);
}

/* =========================================================================
 * SA-6 cont.: Runtime manifest safety net
 * ========================================================================= */

/**
 * manifestEnsureLoaded -- ensure a single asset is tracked in the active SP manifest.
 *
 * Checks whether catalog_id is already recorded in g_CurrentLoadedManifest by
 * FNV-1a hash.  If not, resolves it via assetCatalogResolve(), adds it to the
 * manifest, and advances its catalog state to ASSET_STATE_LOADED.
 *
 * asset_type: MANIFEST_TYPE_BODY, MANIFEST_TYPE_HEAD, or MANIFEST_TYPE_MODEL.
 *
 * Returns 1 if the asset is now tracked; 0 if catalog_id is NULL/empty, the
 * active manifest has no entries (MP mode or pre-load), or the asset could not
 * be resolved (synthetic hash is still added to suppress future log spam).
 *
 * Safe to call on every spawn: the dedup check is O(n) over the entry list.
 */
s32 manifestEnsureLoaded(const char *catalog_id, s32 asset_type)
{
    u32 hash;
    s32 i;
    const asset_entry_t *e;

    if (!catalog_id || catalog_id[0] == '\0') {
        return 0;
    }

    /* Only active when an SP manifest has been built (num_entries > 0).
     * In MP mode g_CurrentLoadedManifest is never populated, so this
     * returns immediately without touching the server-managed manifest. */
    if (g_CurrentLoadedManifest.num_entries == 0) {
        return 0;
    }

    hash = s_fnv1a(catalog_id);

    /* Dedup: already tracked — no action needed. */
    for (i = 0; i < (s32)g_CurrentLoadedManifest.num_entries; i++) {
        if (g_CurrentLoadedManifest.entries[i].net_hash == hash) {
            return 1;
        }
    }

    /* Not yet tracked — late-register and load. */
    e = assetCatalogResolve(catalog_id);
    if (e) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: late-add '%s' type=%d (missed by pre-scan)",
                     catalog_id, asset_type);
        manifestAddEntry(&g_CurrentLoadedManifest, e->net_hash, e->id,
                         (u8)asset_type, MANIFEST_SLOT_MATCH);
        catalogLoadAsset(e->id);
        return 1;
    }

    /* Not in catalog — add with synthetic hash to avoid repeat log spam on
     * subsequent spawn attempts for the same asset. */
    sysLogPrintf(LOG_WARNING,
                 "MANIFEST-SP: late-add '%s' not in catalog, using synthetic hash",
                 catalog_id);
    manifestAddEntry(&g_CurrentLoadedManifest, hash, catalog_id,
                     (u8)asset_type, MANIFEST_SLOT_MATCH);
    return 0;
}

/* =========================================================================
 * Phase C: MP manifest check
 * ========================================================================= */

/**
 * manifestCheck -- check local asset catalog against received manifest.
 *
 * Called client-side after SVC_MATCH_MANIFEST is parsed into *manifest.
 * Iterates all entries.  For each one:
 *   1. Try assetCatalogResolveByNetHash(net_hash).
 *   2. If not found, try assetCatalogResolve(id) by string.
 *   3. If still not found:
 *      - MANIFEST_TYPE_COMPONENT → add to missing list (must be downloaded).
 *      - Other types → assume present (base game asset, always local).
 *
 * Sends CLC_MANIFEST_STATUS to the server:
 *   MANIFEST_STATUS_READY       — all entries accounted for
 *   MANIFEST_STATUS_NEED_ASSETS — one or more components missing
 */
void manifestCheck(const match_manifest_t *manifest)
{
    static const char *s_type_names[] = {
        "BODY", "HEAD", "STAGE", "WEAPON", "COMPONENT", "MODEL", "ANIM", "TEXTURE"
    };

    /* missing_hashes bounded by wire protocol: num_missing sent as u8 (0-255) */
    u32 missing_hashes[256];
    s32 num_missing = 0;
    u8  status;
    s32 i;

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST: checking %u entries against local catalog (hash=0x%08x)",
                 (unsigned)manifest->num_entries, (unsigned)manifest->manifest_hash);

    for (i = 0; i < (s32)manifest->num_entries; i++) {
        const match_manifest_entry_t *e = &manifest->entries[i];
        const char *type_name;
        const asset_entry_t *local;

        type_name = (e->type < ARRAYCOUNT(s_type_names))
                    ? s_type_names[e->type] : "?";

        /* Try hash lookup first — most reliable since net_hash is the canonical
         * wire identity.  Then fall back to string ID for synthetic entries. */
        local = assetCatalogResolveByNetHash(e->net_hash);
        if (!local && e->id[0]) {
            local = assetCatalogResolve(e->id);
        }

        if (local) {
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST: [%2d] %-9s hash=0x%08x id='%s' — OK",
                         i, type_name, (unsigned)e->net_hash, e->id);
            continue;
        }

        /* Not found.  Non-component types are base game assets — always present
         * locally even if the catalog doesn't have a named entry for them. */
        if (e->type != MANIFEST_TYPE_COMPONENT) {
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST: [%2d] %-9s hash=0x%08x id='%s' — not in catalog, assumed base game",
                         i, type_name, (unsigned)e->net_hash, e->id);
            continue;
        }

        /* Mod component — must be present.  Report as missing. */
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST: [%2d] COMPONENT  hash=0x%08x id='%s' — MISSING",
                     i, (unsigned)e->net_hash, e->id);
        if (num_missing < 255) {
            missing_hashes[num_missing++] = e->net_hash;
        }
    }

    status = (num_missing == 0) ? MANIFEST_STATUS_READY
                                 : MANIFEST_STATUS_NEED_ASSETS;

    if (status == MANIFEST_STATUS_READY) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST: check passed — %d/%d entries OK, sending READY",
                     (int)manifest->num_entries, (int)manifest->num_entries);
    } else {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST: check found %d missing component(s) of %d entries, sending NEED_ASSETS",
                     num_missing, (int)manifest->num_entries);
    }

    netbufStartWrite(&g_NetMsgRel);
    netmsgClcManifestStatusWrite(&g_NetMsgRel, manifest->manifest_hash,
                                 status, missing_hashes, (u8)num_missing);
    netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}
