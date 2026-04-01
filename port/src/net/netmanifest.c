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
 * Asset ID naming convention (must stay consistent with catalog scanner):
 *   body: "body_%d" where %d is the mp-body index (bodynum)
 *   head: "head_%d" where %d is the mp-head index (headnum)
 *   stage: "stage_0x%02x" where %02x is the logical stagenum
 *   weapon: "weapon_%d" where %d is the MPWEAPON_* value
 *   component: mod->id (from mod.json)
 *
 * net_hash is FNV-1a of the ID string, matching assetcatalog.c's id_hash
 * computation.  (The catalog uses CRC32 for net_hash in asset_entry_t, but
 * for synthetic entries we use FNV-1a of the generated ID — Phase C will
 * resolve the correct catalog net_hash on the client side.)
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
        char id[64];
        const asset_entry_t *e;
        snprintf(id, sizeof(id), "stage_0x%02x", (unsigned)g_MpSetup.stagenum);

        /* Try catalog first (resolves on client / host where base catalog loaded) */
        e = assetCatalogResolve(id);
        if (e) {
            manifestAddEntry(out, e->net_hash, e->id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            manifestAddEntry(out, s_fnv1a(id), id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- Weapons ---- */
    for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
        const u8 wnum = g_MpSetup.weapons[i];
        const asset_entry_t *e;
        char id[64];
        if (wnum == 0) {
            continue;
        }
        snprintf(id, sizeof(id), "weapon_%d", (int)wnum);

        e = assetCatalogResolve(id);
        if (e) {
            manifestAddEntry(out, e->net_hash, e->id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        } else {
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
            const asset_entry_t *be;
            const asset_entry_t *he;
            char body_id[64];
            char head_id[64];

            snprintf(body_id, sizeof(body_id), "body_%d", (int)bodynum);
            snprintf(head_id, sizeof(head_id), "head_%d", (int)headnum);

            be = assetCatalogResolve(body_id);
            if (be) {
                manifestAddEntry(out, be->net_hash, be->id,
                                 MANIFEST_TYPE_BODY, slot_index);
            } else {
                manifestAddEntry(out, s_fnv1a(body_id), body_id,
                                 MANIFEST_TYPE_BODY, slot_index);
            }

            he = assetCatalogResolve(head_id);
            if (he) {
                manifestAddEntry(out, he->net_hash, he->id,
                                 MANIFEST_TYPE_HEAD, slot_index);
            } else {
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
    snprintf(id, sizeof(id), "stage_0x%02x", (unsigned)stagenum);
    if (catalogResolveStage(id, &stage_result) && stage_result.entry) {
        manifestAddEntry(out, stage_result.net_hash, stage_result.entry->id,
                         MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
    } else {
        /* Stage not yet in catalog (pre-scan or modded stage) — use synthetic hash */
        manifestAddEntry(out, s_fnv1a(id), id,
                         MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
    }

    /* ---- SP player character: Joanna Dark (body_0 / head_0) ---- */
    be = assetCatalogResolve("body_0");
    if (be) {
        manifestAddEntry(out, be->net_hash, be->id, MANIFEST_TYPE_BODY, 0);
    } else {
        manifestAddEntry(out, s_fnv1a("body_0"), "body_0", MANIFEST_TYPE_BODY, 0);
    }

    he = assetCatalogResolve("head_0");
    if (he) {
        manifestAddEntry(out, he->net_hash, he->id, MANIFEST_TYPE_HEAD, 0);
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
                    char bstr[64];
                    const asset_entry_t *cbe;
                    snprintf(bstr, sizeof(bstr), "body_%d", (int)chr->bodynum);
                    cbe = assetCatalogResolve(bstr);
                    if (cbe) {
                        manifestAddEntry(out, cbe->net_hash, cbe->id,
                                         MANIFEST_TYPE_BODY, 0);
                    } else {
                        manifestAddEntry(out, s_fnv1a(bstr), bstr,
                                         MANIFEST_TYPE_BODY, 0);
                    }
                }

                /* headnum < 0 = holograph / special; no fixed catalog entry */
                if (chr->headnum >= 0) {
                    char hstr[64];
                    const asset_entry_t *che;
                    snprintf(hstr, sizeof(hstr), "head_%d", (int)chr->headnum);
                    che = assetCatalogResolve(hstr);
                    if (che) {
                        manifestAddEntry(out, che->net_hash, che->id,
                                         MANIFEST_TYPE_HEAD, 0);
                    } else {
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
        char bstr[64];
        char hstr[64];
        const asset_entry_t *cbe;
        const asset_entry_t *che;

        if (g_Vars.antibodynum >= 0) {
            snprintf(bstr, sizeof(bstr), "body_%d", (int)g_Vars.antibodynum);
            cbe = assetCatalogResolve(bstr);
            if (cbe) {
                manifestAddEntry(out, cbe->net_hash, cbe->id,
                                 MANIFEST_TYPE_BODY, 1);
            } else {
                manifestAddEntry(out, s_fnv1a(bstr), bstr,
                                 MANIFEST_TYPE_BODY, 1);
            }
        }

        if (g_Vars.antiheadnum >= 0) {
            snprintf(hstr, sizeof(hstr), "head_%d", (int)g_Vars.antiheadnum);
            che = assetCatalogResolve(hstr);
            if (che) {
                manifestAddEntry(out, che->net_hash, che->id,
                                 MANIFEST_TYPE_HEAD, 1);
            } else {
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

    /* Unload: transition entries no longer needed back to ENABLED */
    for (i = 0; i < diff->num_to_unload; i++) {
        if (diff->to_unload[i].id[0]) {
            assetCatalogSetLoadState(diff->to_unload[i].id, ASSET_STATE_ENABLED);
            sysLogPrintf(LOG_NOTE, "MANIFEST-SP: unload '%s'",
                         diff->to_unload[i].id);
        }
    }

    /* Load: transition entries needed for the new mission to LOADED */
    for (i = 0; i < diff->num_to_load; i++) {
        if (diff->to_load[i].id[0]) {
            assetCatalogSetLoadState(diff->to_load[i].id, ASSET_STATE_LOADED);
            sysLogPrintf(LOG_NOTE, "MANIFEST-SP: load '%s'",
                         diff->to_load[i].id);
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

void manifestSPTransition(s32 stagenum)
{
    manifestBuildMission(stagenum, &s_SpNeededManifest);

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-SP: transition to stage 0x%02x — %d entries",
                 (unsigned)stagenum, (int)s_SpNeededManifest.num_entries);

    manifestDiff(&g_CurrentLoadedManifest, &s_SpNeededManifest, &s_SpLastDiff);
    manifestApplyDiff(&s_SpNeededManifest, &s_SpLastDiff);
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

    /* Not yet tracked — late-register and mark as loaded. */
    e = assetCatalogResolve(catalog_id);
    if (e) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: late-add '%s' type=%d (missed by pre-scan)",
                     catalog_id, asset_type);
        manifestAddEntry(&g_CurrentLoadedManifest, e->net_hash, e->id,
                         (u8)asset_type, MANIFEST_SLOT_MATCH);
        assetCatalogSetLoadState(e->id, ASSET_STATE_LOADED);
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
