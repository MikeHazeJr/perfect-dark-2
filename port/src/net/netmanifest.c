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
#include "audio.h"
#include "sha256.h"
#include "net/netbuf.h"
#include "net/matchsetup.h"
#include "game/chrai.h"     /* FIX-B.1: chraiGetCommandLength for ailist walk */

/* =========================================================================
 * Config — hard cap on manifest entries, overridable via pd.ini
 * ========================================================================= */

static s32 s_ManifestMaxEntries = MANIFEST_MAX_ENTRIES;

PD_CONSTRUCTOR static void manifestConfigInit(void)
{
    configRegisterInt("Debug.ManifestMaxEntries", &s_ManifestMaxEntries,
                      64, MANIFEST_MAX_ENTRIES);
}

/* S483 (2026-04-27): host-eligible spawn-weapon pool enumeration.
 *
 * The Random and Fiesta spawn-weapon modes (matchsetup.c
 * spawnWeaponPickFromMatchManifest) draw their pool from the host's full
 * unlocked-weapon catalog, not just the 6 active-set slots. Build sites
 * (manifestBuild server-side, manifestBuildForHost client-side) call this
 * helper after the existing 6-slot enumeration to add every host-unlocked
 * ASSET_WEAPON entry (minus NONE/DISABLED/SHIELD) so clients receive the
 * same pool the host rolls from. manifestAddEntry deduplicates by catalog
 * id, so weapons already added via the active-set pass are not double-
 * counted.
 *
 * Distribution: ASSET_WEAPON is already in the SVC_CATALOG_INFO type list
 * (netmsg.c netmsgSvcCatalogInfoWrite), so mod-only weapons will already
 * stream to clients via the existing CLC_CATALOG_DIFF / SVC_DISTRIB_*
 * pipeline at lobby join time. */
struct manifestWeaponPoolCtx {
    match_manifest_t *m;
    s32               added;
    s32               skipped_filtered;
    s32               skipped_invalid;
};

static void s_manifestWeaponPoolCb(const asset_entry_t *e, void *ud)
{
    struct manifestWeaponPoolCtx *ctx = (struct manifestWeaponPoolCtx *)ud;
    if (!e || !e->id[0]) {
        ctx->skipped_invalid++;
        return;
    }
    s32 wid = (s32)e->ext.weapon.weapon_id;
    if (wid <= 0 || wid >= NUM_MPWEAPONS
            || wid == MPWEAPON_NONE
            || wid == MPWEAPON_DISABLED
            || wid == MPWEAPON_SHIELD) {
        ctx->skipped_filtered++;
        return;
    }
    manifestAddEntry(ctx->m, e->id,
                     MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
    ctx->added++;
}

static void s_manifestAppendWeaponPool(match_manifest_t *out)
{
    struct manifestWeaponPoolCtx ctx;
    ctx.m = out;
    ctx.added = 0;
    ctx.skipped_filtered = 0;
    ctx.skipped_invalid = 0;
    assetCatalogIterateUnlockedByType(ASSET_WEAPON,
                                      s_manifestWeaponPoolCb, &ctx);
    sysLogPrintf(LOG_NOTE,
        "manifest: weapon pool enumeration -- added=%d filtered=%d invalid=%d",
        ctx.added, ctx.skipped_filtered, ctx.skipped_invalid);
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

void manifestAddEntry(match_manifest_t *m, const char *id,
                      u8 type, u8 slot_index)
{
    s32 i;
    match_manifest_entry_t *e;

    /* v27: dedup by catalog ID string comparison. */
    for (i = 0; i < (s32)m->num_entries; i++) {
        if (id && strncmp(m->entries[i].id, id, sizeof(m->entries[i].id)) == 0) {
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
    /* Derive net_hash locally for internal use (not sent on wire). */
    {
        const asset_entry_t *ce = id ? assetCatalogResolve(id) : NULL;
        e->net_hash = ce ? ce->net_hash : (id ? s_fnv1a(id) : 0);
    }
    e->type       = type;
    e->slot_index = slot_index;
    memset(e->sha256, 0, sizeof(e->sha256));
    if (id) {
        strncpy(e->id, id, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
    } else {
        e->id[0] = '\0';
    }
}

void manifestAddModEntry(match_manifest_t *m, const char *id,
                         u8 slot_index, const u8 *sha256)
{
    s32 i;
    match_manifest_entry_t *e;

    /* v27: dedup by catalog ID string comparison. */
    for (i = 0; i < (s32)m->num_entries; i++) {
        if (id && strncmp(m->entries[i].id, id, sizeof(m->entries[i].id)) == 0) {
            return;
        }
    }

    if ((s32)m->num_entries >= (s32)m->capacity) {
        if (!s_manifestGrow(m)) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST: manifest full (%d entries max), dropping mod '%s'",
                         s_ManifestMaxEntries, id ? id : "?");
            return;
        }
    }

    e = &m->entries[m->num_entries++];
    /* Derive net_hash locally for internal use (not sent on wire). */
    {
        const asset_entry_t *ce = id ? assetCatalogResolve(id) : NULL;
        e->net_hash = ce ? ce->net_hash : (id ? s_fnv1a(id) : 0);
    }
    e->type       = MANIFEST_TYPE_COMPONENT;
    e->slot_index = slot_index;
    if (sha256) {
        memcpy(e->sha256, sha256, sizeof(e->sha256));
    } else {
        memset(e->sha256, 0, sizeof(e->sha256));
    }
    if (id) {
        strncpy(e->id, id, sizeof(e->id) - 1);
        e->id[sizeof(e->id) - 1] = '\0';
    } else {
        e->id[0] = '\0';
    }
}

u32 manifestComputeHash(match_manifest_t *m)
{
    /* v27: FNV-1a over (id string bytes, type, slot_index) for each entry in order.
     * net_hash removed from the computation so both sides can agree without the
     * hash being on the wire. */
    u32 h = 0x811c9dc5u;
    s32 i;
    for (i = 0; i < (s32)m->num_entries; i++) {
        const match_manifest_entry_t *e = &m->entries[i];
        /* Feed id string bytes */
        const char *s = e->id;
        while (*s) { h ^= (u8)*s++; h *= 0x01000193u; }
        h ^= e->type;                  h *= 0x01000193u;
        h ^= e->slot_index;            h *= 0x01000193u;
    }
    m->manifest_hash = h;
    return h;
}

/* =========================================================================
 * Phase 2: Dependency-graph expansion helpers
 *
 * When a manifest builder expands a catalog entry, deps registered under
 * that entry's catalog ID are included with the dep asset's manifest type.
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
    case ASSET_PROJECTILE: return MANIFEST_TYPE_PROJECTILE;
    case ASSET_ENTITY:    return MANIFEST_TYPE_ENTITY;
    default:              return MANIFEST_TYPE_COMPONENT;
    }
}

static asset_type_e s_manifestCatalogAssetType(u8 manifest_type)
{
    switch (manifest_type) {
    case MANIFEST_TYPE_BODY:      return ASSET_BODY;
    case MANIFEST_TYPE_HEAD:      return ASSET_HEAD;
    case MANIFEST_TYPE_STAGE:     return ASSET_MAP;
    case MANIFEST_TYPE_WEAPON:    return ASSET_WEAPON;
    case MANIFEST_TYPE_MODEL:     return ASSET_MODEL;
    case MANIFEST_TYPE_ANIM:      return ASSET_ANIMATION;
    case MANIFEST_TYPE_TEXTURE:   return ASSET_TEXTURE;
    case MANIFEST_TYPE_LANG:      return ASSET_LANG;
    case MANIFEST_TYPE_AUDIO:     return ASSET_AUDIO;
    case MANIFEST_TYPE_PROJECTILE: return ASSET_PROJECTILE;
    case MANIFEST_TYPE_ENTITY:    return ASSET_ENTITY;
    case MANIFEST_TYPE_COMPONENT: return ASSET_NONE;
    default:                      return ASSET_NONE;
    }
}

static void s_manifestDepAddEntry(const char *dep_id, void *userdata)
{
    s_DepExpandCtx *ctx = (s_DepExpandCtx *)userdata;
    const asset_entry_t *de = assetCatalogResolve(dep_id);
    if (de) {
        u8 mtype = s_assetTypeToManifestType(de->type);
        manifestAddEntry(ctx->manifest, de->id,
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
 *   g_MpSetup         -- stage, weapons (B-12: chrslots removed in v37)
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
        if (g_MpSetup.stage_id[0]) {
            manifestAddEntry(out, g_MpSetup.stage_id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            sysLogPrintf(LOG_WARNING, "manifest: no stage_id set, skipping stage entry");
        }
    }

    /* ---- Weapons (active set: 6 slots) ---- */
    for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
        const u8 wnum = g_MpSetup.weapons[i];
        const char *canon_id;
        const asset_entry_t *e;
        if (wnum == 0) {
            continue;
        }
        canon_id = catalogWeaponIdByMpWeaponId((s32)wnum);
        e = canon_id ? assetCatalogResolve(canon_id) : NULL;
        if (e) {
            manifestAddEntry(out, e->id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        } else {
            /* Weapon not in catalog — skip rather than emit a dead synthetic ID */
            sysLogPrintf(LOG_WARNING, "manifest: weapon %d not in catalog, skipping",
                         (int)wnum);
        }
    }

    /* ---- Weapons (host-eligible Random/Fiesta pool, S483 2026-04-27) ----
     * Adds every host-unlocked ASSET_WEAPON entry (minus NONE/DISABLED/SHIELD)
     * so clients receive the full pool the host rolls Random/Fiesta from.
     * Dedup is automatic; the active-set 6 above already covers their slots. */
    s_manifestAppendWeaponPool(out);

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
                manifestAddEntry(out, be->id,
                                 MANIFEST_TYPE_BODY, slot_index);
                s_manifestExpandDeps(out, be->id, slot_index);
            } else {
                manifestAddEntry(out, ncl->settings.body_id,
                                 MANIFEST_TYPE_BODY, slot_index);
            }
        }
        {
            const asset_entry_t *he = assetCatalogResolve(ncl->settings.head_id);
            if (he) {
                manifestAddEntry(out, he->id,
                                 MANIFEST_TYPE_HEAD, slot_index);
                s_manifestExpandDeps(out, he->id, slot_index);
            } else {
                manifestAddEntry(out, ncl->settings.head_id,
                                 MANIFEST_TYPE_HEAD, slot_index);
            }
        }

        slot_index++;
    }

    /* ---- Bots from g_MatchConfig (mirrors manifestBuildForHost pattern) ---- */
    /* body_id/head_id are the PRIMARY identity — use catalog IDs directly;
     * no integer-domain conversion needed. */
    for (i = 0; i < (s32)g_MatchConfig.numSlots && slot_index < 0xFF; i++) {
        const struct matchslot *sl = &g_MatchConfig.slots[i];
        if (sl->type != SLOT_BOT) {
            continue;
        }
        {
            const asset_entry_t *be = sl->body_id[0] ? assetCatalogResolve(sl->body_id) : NULL;
            const asset_entry_t *he = sl->head_id[0] ? assetCatalogResolve(sl->head_id) : NULL;
            if (be) {
                manifestAddEntry(out, be->id,
                                 MANIFEST_TYPE_BODY, slot_index);
                s_manifestExpandDeps(out, be->id, slot_index);
            }
            if (he) {
                manifestAddEntry(out, he->id,
                                 MANIFEST_TYPE_HEAD, slot_index);
                s_manifestExpandDeps(out, he->id, slot_index);
            }
        }
        slot_index++;
    }

    /* ---- Mod components (returns 0 on dedicated server stub) ---- */
    /* B-172: only include mods that are fully valid and carry distributable
     * game content (mod.json present and parsed cleanly). Audio-only mods
     * (has_audioini, no mod.json) are represented via MANIFEST_TYPE_AUDIO
     * playlist entries below -- including them as COMPONENT entries causes
     * `DISTRIB: unknown catalog_id` warnings because their id is not
     * registered in the server-side asset catalog. Invalid mods (failed
     * mod.json parse) likewise can't be distributed and must not stall the
     * ready gate. */
    {
        const s32 num_mods = modmgrGetCount();
        for (i = 0; i < num_mods; i++) {
            modinfo_t *mod = modmgrGetMod(i);
            if (!mod || !mod->enabled || mod->contenthash == 0) {
                continue;
            }
            if (!mod->valid || !mod->has_modjson) {
                sysLogPrintf(LOG_NOTE,
                    "manifest: skipping mod '%s' (valid=%d has_modjson=%d has_audioini=%d)",
                    mod->id, (int)mod->valid,
                    (int)mod->has_modjson, (int)mod->has_audioini);
                continue;
            }
            manifestAddModEntry(out, mod->id,
                                MANIFEST_SLOT_MATCH, mod->sha256);
        }
    }

    /* ---- Audio playlist tracks (v34) ---- */
    /* All tracks in the host's playlist must be present on every client
     * before match start. The ready gate will transfer missing ones. */
    {
        const s32 plcount = audioGetModPlaylistCount();
        for (i = 0; i < plcount; i++) {
            const char *tid = audioGetModPlaylistEntry(i);
            if (tid && tid[0]) {
                const asset_entry_t *ae = assetCatalogResolve(tid);
                if (ae && !ae->bundled) {
                    manifestAddEntry(out, ae->id,
                                     MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH);
                }
            }
        }
        /* Single-track fallback: if no playlist but a track is set */
        if (plcount == 0) {
            const char *single = audioGetModTrackId();
            if (single && single[0]) {
                const asset_entry_t *ae = assetCatalogResolve(single);
                if (ae && !ae->bundled) {
                    manifestAddEntry(out, ae->id,
                                     MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH);
                }
            }
        }
    }

    /* Seal: compute manifest-level hash over all entries */
    manifestComputeHash(out);
}

/* =========================================================================
 * Menu-stage manifest: pre-populate ALL character models so the Skin Editor,
 * Agent Select, Bot Setup, and other menu screens can preview any character
 * from the catalog without needing a match manifest.
 *
 * The title screen is a stage too — it just wasn't treated as one by the
 * manifest system until now.
 * ========================================================================= */

typedef struct {
    match_manifest_t *manifest;
    /* S300: per-category counters so we can audit the menu manifest without
     * manifestLog()'ing the entire flat list. Mod counts split out so Skin
     * Editor / Mod Pack user-visible categories are easy to verify. */
    s32  bodies_added;
    s32  bodies_skipped_disabled;
    s32  bodies_mod;
    s32  heads_added;
    s32  heads_skipped_disabled;
    s32  heads_mod;
} s_MenuIterCtx;

/* Returns 1 if the catalog entry belongs to a mod namespace (not "base:").
 * Used purely for diagnostics — tells us whether mod bodies/heads are
 * reaching the menu manifest pipeline. */
static s32 s_entryIsMod(const asset_entry_t *entry)
{
    if (!entry || !entry->id[0]) {
        return 0;
    }
    /* Catalog IDs are "namespace:readable_name". Anything not "base:" is
     * a mod-registered asset (user.<slug>, custom.<slug>, etc.). */
    return (strncmp(entry->id, "base:", 5) != 0);
}

static void s_menuBodyCallback(const asset_entry_t *entry, void *userdata)
{
    s_MenuIterCtx *ctx = (s_MenuIterCtx *)userdata;
    if (!entry || !entry->id[0]) {
        return;
    }
    if (!entry->enabled) {
        ctx->bodies_skipped_disabled++;
        return;
    }
    manifestAddEntry(ctx->manifest, entry->id,
                     MANIFEST_TYPE_BODY, MANIFEST_SLOT_MATCH);
    s_manifestExpandDeps(ctx->manifest, entry->id, MANIFEST_SLOT_MATCH);
    ctx->bodies_added++;
    if (s_entryIsMod(entry)) {
        ctx->bodies_mod++;
    }
}

static void s_menuHeadCallback(const asset_entry_t *entry, void *userdata)
{
    s_MenuIterCtx *ctx = (s_MenuIterCtx *)userdata;
    if (!entry || !entry->id[0]) {
        return;
    }
    if (!entry->enabled) {
        ctx->heads_skipped_disabled++;
        return;
    }
    manifestAddEntry(ctx->manifest, entry->id,
                     MANIFEST_TYPE_HEAD, MANIFEST_SLOT_MATCH);
    s_manifestExpandDeps(ctx->manifest, entry->id, MANIFEST_SLOT_MATCH);
    ctx->heads_added++;
    if (s_entryIsMod(entry)) {
        ctx->heads_mod++;
    }
}

void manifestBuildForMenu(match_manifest_t *out)
{
    s_MenuIterCtx ctx;
    ctx.manifest               = out;
    ctx.bodies_added           = 0;
    ctx.bodies_skipped_disabled = 0;
    ctx.bodies_mod             = 0;
    ctx.heads_added            = 0;
    ctx.heads_skipped_disabled = 0;
    ctx.heads_mod              = 0;

    manifestClear(out);

    /* Register every body and head in the catalog.  The menu stage is the
     * hub — the Skin Editor, Agent Select, Bot Setup, and Modding Hub all
     * need to preview arbitrary characters. Bundled assets are ROM-resident;
     * mod assets are activated through the typed lifecycle diff pipeline. */
    assetCatalogIterateByType(ASSET_BODY, s_menuBodyCallback, &ctx);
    assetCatalogIterateByType(ASSET_HEAD, s_menuHeadCallback, &ctx);

    manifestComputeHash(out);

    /* S300: structured diagnostic — include mod counts so Mike can verify
     * Skin Editor / Mod Pack assets are making it into the menu manifest.
     * A zero mod count despite enabled user.<slug>.* catalog entries is the
     * classic "Skin Editor mod characters silently missing" symptom. */
    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-MENU: built %d entries — bodies=%d(mod=%d,disabled=%d) heads=%d(mod=%d,disabled=%d)",
                 (int)out->num_entries,
                 ctx.bodies_added, ctx.bodies_mod, ctx.bodies_skipped_disabled,
                 ctx.heads_added, ctx.heads_mod, ctx.heads_skipped_disabled);
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
        "BODY", "HEAD", "STAGE", "WEAPON", "COMPONENT", "MODEL", "ANIM", "TEXTURE", "LANG", "AUDIO"
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
 * Phase D: Host-built manifest + wire serialisation helpers
 * ========================================================================= */

/**
 * manifestBuildForHost -- client-side manifest build (D.2).
 *
 * Called by the lobby-leader client before sending CLC_LOBBY_START.
 * Builds stage, weapons, host player body/head, bot body/head, and
 * enabled mod components.  The server supplements the received manifest
 * with other connected players' body/head before broadcasting
 * SVC_MATCH_MANIFEST.
 */
void manifestBuildForHost(match_manifest_t *out)
{
    s32 i;
    u8 slot_index;

    manifestClear(out);

    /* ---- Stage ---- */
    {
        if (g_MpSetup.stage_id[0]) {
            manifestAddEntry(out, g_MpSetup.stage_id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-HOST: no stage_id set, skipping stage entry");
        }
    }

    /* ---- Weapons (active set: 6 slots) ---- */
    for (i = 0; i < NUM_MPWEAPONSLOTS; i++) {
        const u8 wnum = g_MpSetup.weapons[i];
        const char *canon_id;
        const asset_entry_t *e;
        if (wnum == 0) {
            continue;
        }
        canon_id = catalogWeaponIdByMpWeaponId((s32)wnum);
        e = canon_id ? assetCatalogResolve(canon_id) : NULL;
        if (e) {
            manifestAddEntry(out, e->id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- Weapons (host-eligible Random/Fiesta pool, S483 2026-04-27) ----
     * Mirror of manifestBuild's enumeration so the host's CLC_LOBBY_START
     * carries the full Random/Fiesta pool. Dedup against the active-set 6
     * is automatic via manifestAddEntry. */
    s_manifestAppendWeaponPool(out);

    /* ---- Host/local players ---- */
    slot_index = 0;
    if (g_NetLocalClient) {
        const asset_entry_t *be = assetCatalogResolve(g_NetLocalClient->settings.body_id);
        if (be) {
            manifestAddEntry(out, be->id, MANIFEST_TYPE_BODY, slot_index);
            s_manifestExpandDeps(out, be->id, slot_index);
        }
        {
            const asset_entry_t *he = assetCatalogResolve(g_NetLocalClient->settings.head_id);
            if (he) {
                manifestAddEntry(out, he->id, MANIFEST_TYPE_HEAD, slot_index);
                s_manifestExpandDeps(out, he->id, slot_index);
            }
        }
        slot_index = 1;
    } else {
        /* Offline Combat Simulator has no net-local client, but the
         * match config is still the source of truth for local players.
         * Include those player body/head assets so local matches use the
         * same predeclared MP manifest path as hosted matches. */
        for (i = 0; i < (s32)g_MatchConfig.numSlots && slot_index < 0xFF; i++) {
            const struct matchslot *sl = &g_MatchConfig.slots[i];
            if (sl->type != SLOT_PLAYER) {
                continue;
            }
            {
                const asset_entry_t *be = sl->body_id[0] ? assetCatalogResolve(sl->body_id) : NULL;
                const asset_entry_t *he = sl->head_id[0] ? assetCatalogResolve(sl->head_id) : NULL;
                if (be) {
                    manifestAddEntry(out, be->id, MANIFEST_TYPE_BODY, slot_index);
                    s_manifestExpandDeps(out, be->id, slot_index);
                }
                if (he) {
                    manifestAddEntry(out, he->id, MANIFEST_TYPE_HEAD, slot_index);
                    s_manifestExpandDeps(out, he->id, slot_index);
                }
            }
            slot_index++;
        }
    }

    /* ---- Bots from g_MatchConfig ---- */
    /* body_id/head_id are the PRIMARY identity — use them directly; no
     * integer-domain conversion on the manifest build path. */
    for (i = 0; i < (s32)g_MatchConfig.numSlots && slot_index < 0xFF; i++) {
        const struct matchslot *sl = &g_MatchConfig.slots[i];
        if (sl->type != SLOT_BOT) {
            continue;
        }
        {
            const asset_entry_t *be = sl->body_id[0] ? assetCatalogResolve(sl->body_id) : NULL;
            const asset_entry_t *he = sl->head_id[0] ? assetCatalogResolve(sl->head_id) : NULL;
            if (be) {
                manifestAddEntry(out, be->id, MANIFEST_TYPE_BODY, slot_index);
                s_manifestExpandDeps(out, be->id, slot_index);
            }
            if (he) {
                manifestAddEntry(out, he->id, MANIFEST_TYPE_HEAD, slot_index);
                s_manifestExpandDeps(out, he->id, slot_index);
            }
        }
        slot_index++;
    }

    /* ---- Mod components with SHA-256 ---- */
    /* B-172: mirror manifestBuild -- only distributable mods. Audio-only
     * mods are carried via MANIFEST_TYPE_AUDIO below; invalid mods fail to
     * stream and stall the ready gate. */
    {
        const s32 num_mods = modmgrGetCount();
        for (i = 0; i < num_mods; i++) {
            modinfo_t *mod = modmgrGetMod(i);
            if (!mod || !mod->enabled || mod->contenthash == 0) {
                continue;
            }
            if (!mod->valid || !mod->has_modjson) {
                sysLogPrintf(LOG_NOTE,
                    "MANIFEST-HOST: skipping mod '%s' (valid=%d has_modjson=%d has_audioini=%d)",
                    mod->id, (int)mod->valid,
                    (int)mod->has_modjson, (int)mod->has_audioini);
                continue;
            }
            manifestAddModEntry(out, mod->id,
                                MANIFEST_SLOT_MATCH, mod->sha256);
        }
    }

    /* ---- Audio playlist tracks (v34) ---- */
    {
        const s32 plcount = audioGetModPlaylistCount();
        for (i = 0; i < plcount; i++) {
            const char *tid = audioGetModPlaylistEntry(i);
            if (tid && tid[0]) {
                const asset_entry_t *ae = assetCatalogResolve(tid);
                if (ae && !ae->bundled) {
                    manifestAddEntry(out, ae->id,
                                     MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH);
                }
            }
        }
        if (plcount == 0) {
            const char *single = audioGetModTrackId();
            if (single && single[0]) {
                const asset_entry_t *ae = assetCatalogResolve(single);
                if (ae && !ae->bundled) {
                    manifestAddEntry(out, ae->id,
                                     MANIFEST_TYPE_AUDIO, MANIFEST_SLOT_MATCH);
                }
            }
        }
    }

    manifestComputeHash(out);
    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-HOST: built %d entries, hash=0x%08x",
                 (int)out->num_entries, (unsigned)out->manifest_hash);
}

/**
 * manifestSerialize -- write manifest entries into netbuf (D.3 wire helper).
 *
 * Format: u16 num_entries, then per entry:
 *   u8 type, u8 slot_index, str id,
 *   [u8[32] sha256] only when type == MANIFEST_TYPE_COMPONENT.
 *
 * v27: net_hash removed from wire format. Both sides derive it locally from
 * the asset catalog (or s_fnv1a) after deserialization.
 *
 * Used for embedding the host manifest in CLC_LOBBY_START and is the
 * same per-entry format used by SVC_MATCH_MANIFEST (protocol 27).
 */
u32 manifestSerialize(struct netbuf *dst, const match_manifest_t *m)
{
    s32 i;
    netbufWriteU16(dst, (u16)m->num_entries);
    for (i = 0; i < (s32)m->num_entries; i++) {
        const match_manifest_entry_t *e = &m->entries[i];
        /* v27: no net_hash on wire — id string is the sole identity. */
        netbufWriteU8(dst, e->type);
        netbufWriteU8(dst, e->slot_index);
        netbufWriteStr(dst, e->id);
        if (e->type == MANIFEST_TYPE_COMPONENT) {
            netbufWriteData(dst, e->sha256, sizeof(e->sha256));
        }
    }
    return dst->error;
}

/**
 * manifestDeserialize -- read manifest entries from netbuf (D.3 wire helper).
 *
 * Appends to *out (does NOT clear first — caller should call manifestClear
 * if starting fresh). On parse failure, rolls back entries appended by this
 * call so malformed packets cannot leave a partial manifest behind.
 * Counterpart to manifestSerialize().
 * Returns 0 on success, 1 on parse error.
 */
s32 manifestDeserialize(struct netbuf *src, match_manifest_t *out)
{
    s32 i;
    const u16 start_entries = out->num_entries;
    const u32 start_hash = out->manifest_hash;
    const u16 num_entries = netbufReadU16(src);
    if (src->error || num_entries > (u16)manifestGetMaxEntries()) {
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST: deserialize: bad entry count %u", (unsigned)num_entries);
        out->num_entries = start_entries;
        out->manifest_hash = start_hash;
        return 1;
    }
    for (i = 0; i < (s32)num_entries; i++) {
        /* v27: no net_hash on wire — derive locally from catalog or s_fnv1a. */
        const u8   type       = netbufReadU8(src);
        const u8   slot_index = netbufReadU8(src);
        const char *id        = netbufReadStr(src);
        if (src->error) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST: deserialize: truncated at entry %d", i);
            out->num_entries = start_entries;
            out->manifest_hash = start_hash;
            return 1;
        }
        /* Derive net_hash from local catalog (CRC32) or s_fnv1a as fallback. */
        u32 net_hash;
        {
            const asset_entry_t *ce = id ? assetCatalogResolve(id) : NULL;
            net_hash = ce ? ce->net_hash : (id ? s_fnv1a(id) : 0);
        }
        if (type == MANIFEST_TYPE_COMPONENT) {
            u8 sha256[32];
            netbufReadData(src, sha256, sizeof(sha256));
            if (src->error) {
                out->num_entries = start_entries;
                out->manifest_hash = start_hash;
                return 1;
            }
            /* SEC-5 / MASTER-H2: reject COMPONENT entries with all-zero SHA-256.
             * A non-zero hash is the integrity root for mod distribution; an
             * adversary server that supplies a zero hash is asking the client
             * to install an archive without verification. Drop the entry so
             * the install path will not find a manifest match and refuse the
             * transfer. */
            {
                static const u8 s_zero32[32] = {0};
                if (memcmp(sha256, s_zero32, sizeof(sha256)) == 0) {
                    sysLogPrintf(LOG_ERROR,
                                 "MANIFEST: dropping COMPONENT entry '%s' — zero SHA-256 (no integrity)",
                                 (id && id[0]) ? id : "?");
                    continue;
                }
            }
            manifestAddModEntry(out, id, slot_index, sha256);
        } else {
            manifestAddEntry(out, id, type, slot_index);
        }
    }
    return 0;
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

/* =========================================================================
 * FIX-B.1 — deep manifest scanner for cinematics + AI scripts
 *
 * The base `manifestBuildMission` only reads g_StageSetup.props.  That
 * misses two whole classes of runtime spawns:
 *
 *   1. Intro commands (INTROCMD_WEAPON / INTROCMD_OUTFIT) set the player's
 *      starting weapon and outfit — those model/weapon refs never appear
 *      in the props list.
 *
 *   2. AI scripts (g_StageSetup.ailists) run from AICMD_SPAWNCHRATPAD /
 *      AICMD_SPAWNCHRATCHR / AICMD_DROPITEM / AICMD_EQUIPWEAPON /
 *      AICMD_EQUIPHAT spawn chrs and props mid-mission.  Cinematic
 *      cutscenes piggy-back on these (cutscene chrs are BG chrs running
 *      cinematic ai scripts).
 *
 * Mirrors the logic in `stageLoadAllAilistModels` (game_00b820.c) and the
 * INTROCMD_WEAPON branch of `playerReset` (playerreset.c:200), using
 * catalog IDs instead of raw bodynum/modelnum.
 * ========================================================================= */

/* FIX-B.1: return 1 if `id` is already present in the manifest.  Used by
 * the scan helpers below to emit a one-shot discovery log only when an
 * entry is newly added -- repeat references in intro/ailist commands
 * dedup via manifestAddEntry() but would otherwise spam the log. */
static s32 s_manifestHasEntry(const match_manifest_t *m, const char *id)
{
    s32 i;
    if (!id || !m) {
        return 0;
    }
    for (i = 0; i < (s32)m->num_entries; i++) {
        if (strncmp(m->entries[i].id, id, sizeof(m->entries[i].id)) == 0) {
            return 1;
        }
    }
    return 0;
}

static void s_manifestAddBody(match_manifest_t *out, s32 bodynum, s32 slot_tag,
                              const char *scan_source)
{
    const char *bcan;
    const asset_entry_t *be;
    s32 was_present;

    if (bodynum < 0 || bodynum >= 256) {
        return; /* 255 = random, negative = reserved */
    }
    bcan = catalogBodyIdByBodynum(bodynum);
    be   = bcan ? assetCatalogResolve(bcan) : NULL;
    if (!be) {
        if (scan_source) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-SP: %s-scan body bodynum=%d not in catalog",
                         scan_source, (int)bodynum);
        }
        return;
    }
    was_present = s_manifestHasEntry(out, be->id);
    manifestAddEntry(out, be->id, MANIFEST_TYPE_BODY, slot_tag);
    s_manifestExpandDeps(out, be->id, slot_tag);
    if (!was_present && scan_source) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: %s-scan discovered body '%s' (bodynum=%d)",
                     scan_source, be->id, (int)bodynum);
    }
}

static void s_manifestAddHead(match_manifest_t *out, s32 headnum, s32 slot_tag,
                              const char *scan_source)
{
    const char *hcan;
    const asset_entry_t *he;
    s32 was_present;

    if (headnum < 0 || headnum >= 256) {
        return; /* negative = hologram / special */
    }
    hcan = catalogHeadIdByHeadnum(headnum);
    he   = hcan ? assetCatalogResolve(hcan) : NULL;
    if (!he) {
        if (scan_source) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-SP: %s-scan head headnum=%d not in catalog",
                         scan_source, (int)headnum);
        }
        return;
    }
    was_present = s_manifestHasEntry(out, he->id);
    manifestAddEntry(out, he->id, MANIFEST_TYPE_HEAD, slot_tag);
    s_manifestExpandDeps(out, he->id, slot_tag);
    if (!was_present && scan_source) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: %s-scan discovered head '%s' (headnum=%d)",
                     scan_source, he->id, (int)headnum);
    }
}

static void s_manifestAddModel(match_manifest_t *out, s32 modelnum,
                               const char *scan_source)
{
    const char *mcan;
    const asset_entry_t *me;
    const char *add_id;
    s32 was_present;

    if (modelnum <= 0 || modelnum >= 0xFFFF) {
        return;
    }
    mcan = catalogModelIdByModelnum(modelnum);
    if (!mcan) {
        return;
    }
    me = assetCatalogResolve(mcan);
    add_id = me ? me->id : mcan;
    was_present = s_manifestHasEntry(out, add_id);
    manifestAddEntry(out, add_id, MANIFEST_TYPE_MODEL, MANIFEST_SLOT_MATCH);
    if (!was_present && scan_source) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: %s-scan discovered model '%s' (modelnum=%d)",
                     scan_source, add_id, (int)modelnum);
    }
}

static void s_manifestAddWeapon(match_manifest_t *out, s32 weaponnum,
                                const char *scan_source)
{
    const char *wcan;
    const asset_entry_t *we;
    s32 was_present;

    if (weaponnum <= 0) {
        return;
    }
    wcan = catalogWeaponIdByRuntimeWeaponNum(weaponnum);
    we   = wcan ? assetCatalogResolve(wcan) : NULL;
    if (!we) {
        if (wcan && scan_source) {
            sysLogPrintf(LOG_WARNING,
                         "MANIFEST-SP: %s-scan weapon weaponnum=%d not in catalog",
                         scan_source, (int)weaponnum);
        }
        return;
    }
    was_present = s_manifestHasEntry(out, we->id);
    manifestAddEntry(out, we->id, MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
    s_manifestExpandDeps(out, we->id, MANIFEST_SLOT_MATCH);
    if (!was_present && scan_source) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: %s-scan discovered weapon '%s' (weaponnum=%d)",
                     scan_source, we->id, (int)weaponnum);
    }
}

/* Walk g_StageSetup.intro for INTROCMD_WEAPON / INTROCMD_OUTFIT hints and
 * add any referenced models/weapons.  Command widths are mirrored from
 * playerReset's dispatcher (playerreset.c). */
static void s_manifestScanIntro(match_manifest_t *out)
{
    struct cmd32 {
        s32 type;
        s32 param1;
        s32 param2;
        s32 param3;
    };
    const struct cmd32 *cmd = (const struct cmd32 *)g_StageSetup.intro;
    s32 safety = 0;

    if (!cmd) {
        return;
    }

    while (cmd->type != INTROCMD_END) {
        if (++safety > 10000) {
            sysLogPrintf(LOG_WARNING,
                         "manifestBuildMission: intro scan exceeded 10000 cmds, aborting");
            break;
        }

        switch (cmd->type) {
        case INTROCMD_WEAPON:
            /* param1 = primary weapon, param2 = secondary (>=0 if set).
             * In PD the "weapon" for the player also pulls in its model
             * via modelmgrLoadProjectileModeldefs; we only register the
             * catalog weapon ID — projectile deps flow through
             * s_manifestExpandDeps. */
            s_manifestAddWeapon(out, cmd->param1, "intro");
            if (cmd->param2 >= 0) {
                s_manifestAddWeapon(out, cmd->param2, "intro");
            }
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 16);
            break;
        case INTROCMD_AMMO:
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 16);
            break;
        case INTROCMD_SPAWN:
        case INTROCMD_CASE:
        case INTROCMD_CASERESPAWN:
        case INTROCMD_WATCHTIME:
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 12);
            break;
        case INTROCMD_3:
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 32);
            break;
        case INTROCMD_6:
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 40);
            break;
        case INTROCMD_HILL:
        case INTROCMD_4:
        case INTROCMD_OUTFIT:
        case INTROCMD_CREDITOFFSET:
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 8);
            break;
        default:
            /* Unknown intro cmd: advance by one word (matches playerReset's
             * default branch). */
            cmd = (const struct cmd32 *)((uintptr_t)cmd + 4);
            break;
        }
    }
}

/* Walk every AI script in g_StageSetup.ailists looking for spawn commands,
 * mirrors game_00b820.c::stageLoadAllAilistModels. */
static void s_manifestScanAilists(match_manifest_t *out)
{
    s32 listidx = 0;
    u8 *cmd;

    if (!g_StageSetup.ailists) {
        return;
    }

    cmd = g_StageSetup.ailists[listidx].list;
    while (cmd) {
        s32 safety = 0;
        while (cmd[0] != AICMD_END) {
            if (++safety > 50000) {
                sysLogPrintf(LOG_WARNING,
                             "manifestBuildMission: ailist[%d] exceeded 50000 cmds, aborting",
                             listidx);
                break;
            }

            switch (cmd[0]) {
            case AICMD_DROPITEM: {
                u16 modelid = (u16)((cmd[2] << 8) | cmd[3]);
                s_manifestAddModel(out, (s32)modelid, "ailist");
                break;
            }
            case AICMD_SPAWNCHRATPAD:
            case AICMD_SPAWNCHRATCHR:
                /* cmd[2] = bodynum (u8), cmd[3] = headnum (s8) */
                s_manifestAddBody(out, (s32)cmd[2], 0, "ailist");
                if ((s8)cmd[3] >= 0) {
                    s_manifestAddHead(out, (s32)(s8)cmd[3], 0, "ailist");
                }
                break;
            case AICMD_EQUIPWEAPON: {
                u16 modelid = (u16)((cmd[2] << 8) | cmd[3]);
                s_manifestAddModel(out, (s32)modelid, "ailist");
                /* cmd[4] = weapon num */
                s_manifestAddWeapon(out, (s32)cmd[4], "ailist");
                break;
            }
            case AICMD_EQUIPHAT: {
                u16 modelid = (u16)((cmd[2] << 8) | cmd[3]);
                s_manifestAddModel(out, (s32)modelid, "ailist");
                break;
            }
            default:
                break;
            }

            cmd += chraiGetCommandLength(cmd, 0);
        }

        listidx++;
        cmd = g_StageSetup.ailists[listidx].list;
    }
}

void manifestBuildMission(s32 stagenum, match_manifest_t *out)
{
    catalog_stage_result_t stage_result;
    const asset_entry_t   *be;
    const asset_entry_t   *he;
    /* FIX-B.1: per-phase entry counters so the scan can be audited in logs. */
    s32 count_after_joanna;
    s32 count_after_props;
    s32 count_after_intro;
    s32 count_after_ailist;

    manifestClear(out);

    /* ---- Stage ---- */
    {
        const char *stage_canon = catalogStageIdByStagenum(stagenum);
        if (stage_canon && catalogResolveStage(stage_canon, &stage_result)
                && stage_result.entry) {
            manifestAddEntry(out, stage_result.entry->id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            /* Stage not in catalog — skip rather than emit a dead synthetic ID */
            sysLogPrintf(LOG_WARNING, "manifestBuildMission: stage 0x%02x not in catalog, skipping",
                         (unsigned)stagenum);
        }
    }

    /* FIX-17: SP player character — Joanna Dark by named catalog ID.
     * Previously used catalogIdByRuntime(ASSET_BODY/HEAD, 0) which
     * relies on registration order and would pick the wrong character if order
     * ever changed.  Use the canonical IDs directly. */
    be = assetCatalogResolve("base:dark_combat");
    he = assetCatalogResolve("base:head_dark_combat");
    if (be) {
        manifestAddEntry(out, be->id, MANIFEST_TYPE_BODY, 0);
        s_manifestExpandDeps(out, be->id, 0);
    } else {
        sysLogPrintf(LOG_WARNING, "manifestBuildMission: Joanna body (base:dark_combat) not in catalog");
    }
    if (he) {
        manifestAddEntry(out, he->id, MANIFEST_TYPE_HEAD, 0);
        s_manifestExpandDeps(out, he->id, 0);
    } else {
        sysLogPrintf(LOG_WARNING, "manifestBuildMission: Joanna head (base:head_dark_combat) not in catalog");
    }
    count_after_joanna = (s32)out->num_entries;

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
                /* ---- character body / head ----
                 * bodynum 255 = random; no fixed catalog entry to require.
                 * headnum < 0 = holograph / special; no fixed catalog entry. */
                const struct packedchr *chr = (const struct packedchr *)sobj;
                if (chr->bodynum != 255) {
                    s_manifestAddBody(out, (s32)chr->bodynum, 0, "props");
                }
                if (chr->headnum >= 0) {
                    s_manifestAddHead(out, (s32)chr->headnum, 0, "props");
                }
            } else {
                /* ---- prop model (types that embed struct defaultobj) ---- */
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
                    s_manifestAddModel(out, (s32)sobj->modelnum, "props");
                    break;
                default:
                    break;
                }
            }

            sobj = (struct defaultobj *)((u32 *)sobj +
                                         setupGetCmdLength((u32 *)sobj));
        }
    }
    count_after_props = (s32)out->num_entries;

    /* FIX-B.1: deep scan intro commands + AI scripts for spawned assets.
     * Both pointers may be NULL pre-load; the helpers are guarded.
     * Per-phase counts let us confirm in playtest logs that each source
     * (props / intro / ailists) is actually contributing entries --
     * silent regressions in any phase would otherwise only surface as
     * runtime late-adds via manifestEnsureLoaded(). */
    s_manifestScanIntro(out);
    count_after_intro = (s32)out->num_entries;
    s_manifestScanAilists(out);
    count_after_ailist = (s32)out->num_entries;

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-SP: scan stage=0x%02x joanna=%d props+=%d intro+=%d ailist+=%d (total=%d)",
                 (unsigned)stagenum,
                 (int)count_after_joanna,
                 (int)(count_after_props - count_after_joanna),
                 (int)(count_after_intro - count_after_props),
                 (int)(count_after_ailist - count_after_intro),
                 (int)count_after_ailist);

    /* ---- Counter-op player body/head ---- */
    /* When a counter-operative player is active (antiplayernum >= 0), their
     * appearance is determined by g_Vars.antibodynum / g_Vars.antiheadnum —
     * these do not appear in the props spawn list, so they must be added here. */
    if (g_Vars.antiplayernum >= 0) {
        if (g_Vars.antibodynum >= 0) {
            const char *bcan = catalogBodyIdByBodynum((s32)g_Vars.antibodynum);
            const asset_entry_t *cbe = bcan ? assetCatalogResolve(bcan) : NULL;
            if (cbe) {
                manifestAddEntry(out, cbe->id,
                                 MANIFEST_TYPE_BODY, 1);
                s_manifestExpandDeps(out, cbe->id, 1);
            }
            /* else: antibodynum not in catalog — skip */
        }

        if (g_Vars.antiheadnum >= 0) {
            const char *hcan = catalogHeadIdByHeadnum((s32)g_Vars.antiheadnum);
            const asset_entry_t *che = hcan ? assetCatalogResolve(hcan) : NULL;
            if (che) {
                manifestAddEntry(out, che->id,
                                 MANIFEST_TYPE_HEAD, 1);
                s_manifestExpandDeps(out, che->id, 1);
            }
            /* else: antiheadnum not in catalog — skip */
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

    /* Load first: bring in entries entering the active manifest BEFORE releasing
     * any outgoing assets.  This ensures new assets are resident before old data
     * is freed, preventing any window during the transition where neither the old
     * nor the new asset data exists in memory.
     * For bundled (base-game) assets this is a no-op retain (already ROM-resident).
     * For mod assets, the typed catalog lifecycle activates the provider-backed
     * payload or metadata and increments ref_count. A missing asset logs a
     * warning and is skipped. */
    for (i = 0; i < diff->num_to_load; i++) {
        if (diff->to_load[i].id[0]) {
            load_ok = catalogLoadTypedAsset(
                    s_manifestCatalogAssetType(diff->to_load[i].type),
                    diff->to_load[i].id);
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

    /* Unload second: decrement ref_count for entries leaving the active manifest.
     * Executed after loads so new assets are already resident before old ones are
     * released — this is the primary guard against the 56-asset transition crash
     * (use-after-free when assets were freed before the new stage was ready).
     * For bundled (base-game) assets typed catalog release is a no-op.
     * For mod assets, ref_count decrements; when it hits 0, data is freed and
     * registered deps are cascade-decremented. Detailed "freed / retained"
     * logging is emitted inside the catalog lifecycle implementation. */
    for (i = 0; i < diff->num_to_unload; i++) {
        if (diff->to_unload[i].id[0]) {
            catalogReleaseTypedAsset(
                    s_manifestCatalogAssetType(diff->to_unload[i].type),
                    diff->to_unload[i].id);
            sysLogPrintf(LOG_NOTE, "MANIFEST-SP: unload '%s'",
                         diff->to_unload[i].id);
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

        /* Resolve strictly by catalog ID string.  Manifest boundaries are
         * string-authoritative; avoid hash fallback that can mask ID drift. */
        e = assetCatalogResolve(entry->id);

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

void manifestSPRescanSetup(s32 stagenum)
{
    s32 pre_count;
    s32 post_count;

    /* After setupLoadFiles() populates g_StageSetup.props, re-run
     * manifestBuildMission() so the CHR/prop scan actually finds entries.
     * Then diff against the current loaded manifest (which was set by the
     * pre-load manifestSPTransition) and apply only the newly discovered
     * to_load entries.  Entries from the pre-load phase are already in
     * g_CurrentLoadedManifest and will appear in to_keep — no double-load. */

    if (g_CurrentLoadedManifest.num_entries == 0) {
        /* No SP manifest active (MP mode or system stage). */
        return;
    }

    if (g_NetMode != NETMODE_NONE) {
        /* MP: client manifest was populated by the network path —
         * SP rescan would build, diff, and discard.  Skip it. */
        return;
    }

    pre_count = (s32)g_CurrentLoadedManifest.num_entries;

    manifestBuildMission(stagenum, &s_SpNeededManifest);

    post_count = (s32)s_SpNeededManifest.num_entries;

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-SP: post-setup rescan for 0x%02x — diff/apply"
                 " (pre=%d, post=%d)",
                 (unsigned)stagenum,
                 pre_count, post_count);

    manifestDiff(&g_CurrentLoadedManifest, &s_SpNeededManifest, &s_SpLastDiff);
    /* S300: structured diagnostic — how many entries did the post-setup
     * rescan actually DISCOVER (to_load) vs. keep (to_keep) vs. unload
     * (to_unload, shouldn't happen during rescan — would indicate pre-scan
     * overshot). A healthy rescan shows a non-zero to_load count when a
     * stage uses cinematic intro / ailist spawns (B-118 symptom was that
     * to_load was always 0 because g_StageSetup.props was NULL pre-load). */
    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-SP: rescan diff — newly-discovered=%d kept=%d unload=%d",
                 s_SpLastDiff.num_to_load,
                 s_SpLastDiff.num_to_keep,
                 s_SpLastDiff.num_to_unload);
    manifestValidate(&s_SpLastDiff);
    manifestApplyDiff(&s_SpNeededManifest, &s_SpLastDiff);
    manifestDiffFree(&s_SpLastDiff);
}

void manifestMenuTransition(void)
{
    static match_manifest_t s_MenuManifest;

    manifestBuildForMenu(&s_MenuManifest);

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST-MENU: transition — %d entries",
                 (int)s_MenuManifest.num_entries);

    manifestDiff(&g_CurrentLoadedManifest, &s_MenuManifest, &s_SpLastDiff);
    manifestValidate(&s_SpLastDiff);
    manifestApplyDiff(&s_MenuManifest, &s_SpLastDiff);
    manifestDiffFree(&s_SpLastDiff);
}

/**
 * manifestMPTransition -- apply a diff-based transition for an MP match.
 *
 * Uses g_ClientManifest (populated when SVC_MATCH_MANIFEST was received) as
 * the "needed" manifest and diffs it against g_CurrentLoadedManifest.
 *
 * For each diff entry:
 *   to_load   -- typed catalog load (no-op for bundled; activates provider-backed payload/metadata)
 *   to_unload -- typed catalog release (no-op for bundled; decrements ref + frees at 0)
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
 * active manifest has no entries (pre-load), MP mode is active, or the asset could not
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

    if (g_NetMode != NETMODE_NONE) {
        /* MP/lobby: never mutate the server-driven manifest from local late-load paths. */
        return 0;
    }

    /* Only active when an SP manifest has been built (num_entries > 0). */
    if (g_CurrentLoadedManifest.num_entries == 0) {
        return 0;
    }

    /* Resolve first so we can use the canonical CRC32 net_hash for the dedup
     * check.  The manifest stores e->net_hash (CRC32); using a different hash
     * algorithm here would cause the dedup check to always miss. */
    e = assetCatalogResolve(catalog_id);
    hash = e ? e->net_hash : s_fnv1a(catalog_id);

    /* Dedup: already tracked — no action needed. */
    for (i = 0; i < (s32)g_CurrentLoadedManifest.num_entries; i++) {
        if (g_CurrentLoadedManifest.entries[i].net_hash == hash) {
            return 1;
        }
    }

    /* Not yet tracked — late-register and load. */
    if (e) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST-SP: late-add '%s' type=%d (missed by pre-scan)",
                     catalog_id, asset_type);
        manifestAddEntry(&g_CurrentLoadedManifest, e->id,
                         (u8)asset_type, MANIFEST_SLOT_MATCH);
        catalogLoadTypedAsset(s_manifestCatalogAssetType((u8)asset_type), e->id);

        /* S312: late-add diagnostic for body/head modeldefs — the
         * sp_body_108 parts=0 class (S308) was observed after a late-add
         * of body/head assets during stage transitions.  Log the
         * post-load modeldef state so we can correlate "truly invalid
         * bodymodeldef" / "parts=0" with the specific catalog id that
         * arrived late.  MODEL (prop) assets aren't checked here — their
         * modeldef lives in a different accessor path.
         * Client-only: catalogGetBodyModeldef is #if !defined(PD_SERVER). */
#if !defined(PD_SERVER)
        if (asset_type == MANIFEST_TYPE_BODY
                || asset_type == MANIFEST_TYPE_HEAD) {
            s32 runtime_idx = e->runtime_index;
            const struct modeldef *md = NULL;

            if (asset_type == MANIFEST_TYPE_BODY) {
                md = catalogGetBodyModeldef(runtime_idx);
            } else {
                md = catalogGetHeadModeldef(runtime_idx);
            }

            if (md == NULL) {
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST-SP: late-add '%s' type=%d runtime=%d -- "
                             "post-load modeldef is NULL (catalog miss after late-add)",
                             catalog_id, asset_type, runtime_idx);
            } else if (md->numparts > 500
                       || md->rootnode == NULL
                       || md->scale <= 0.0f) {
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST-SP: late-add '%s' type=%d runtime=%d -- "
                             "post-load modeldef torn: parts=%d root=%p scale=%.3f "
                             "(invalid structure / scale)",
                             catalog_id, asset_type, runtime_idx,
                             md->numparts, (void *)md->rootnode,
                             md->scale);
            } else if (asset_type == MANIFEST_TYPE_BODY && md->numparts <= 0) {
                /* Bodies require skeletal parts[] — same class as sp_body_108. */
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST-SP: late-add '%s' type=%d runtime=%d -- "
                             "post-load body modeldef torn: parts=%d root=%p scale=%.3f "
                             "(root cause class: sp_body_108 parts=0 / S308)",
                             catalog_id, asset_type, runtime_idx,
                             md->numparts, (void *)md->rootnode,
                             md->scale);
            } else if (asset_type == MANIFEST_TYPE_HEAD && md->numparts <= 0) {
                /* B-207: heads may legitimately have numparts==0 with valid
                 * rootnode+skel (modelcatalog validateModeldef / B-179). */
                sysLogPrintf(LOG_NOTE,
                             "MANIFEST-SP: late-add '%s' type=%d runtime=%d -- "
                             "post-load head modeldef OK (parts=0 valid for heads) scale=%.3f",
                             catalog_id, asset_type, runtime_idx,
                             md->scale);
            } else {
                sysLogPrintf(LOG_NOTE,
                             "MANIFEST-SP: late-add '%s' type=%d runtime=%d -- "
                             "post-load modeldef OK parts=%d scale=%.3f",
                             catalog_id, asset_type, runtime_idx,
                             md->numparts, md->scale);
            }
        }
#endif /* !PD_SERVER */
        return 1;
    }

    /* Not in catalog — add with synthetic hash to avoid repeat log spam on
     * subsequent spawn attempts for the same asset. */
    sysLogPrintf(LOG_WARNING,
                 "MANIFEST-SP: late-add '%s' not in catalog, using synthetic hash",
                 catalog_id);
    manifestAddEntry(&g_CurrentLoadedManifest, catalog_id,
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
 *   1. Try assetCatalogResolve(id) by catalog ID string (v27: sole resolution method).
 *   2. If not found:
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
        "BODY", "HEAD", "STAGE", "WEAPON", "COMPONENT", "MODEL", "ANIM", "TEXTURE", "LANG", "AUDIO"
    };

    /* v27: catalog ID strings only — no u32 net_hash on wire.
     * num_missing bounded at 255 by CLC_MANIFEST_STATUS wire field (u8). */
    char missing_ids[256][CATALOG_ID_LEN];
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

        /* v27: resolve by catalog ID string only. */
        local = e->id[0] ? assetCatalogResolve(e->id) : NULL;

        if (local) {
            /* D.6: for mod COMPONENT entries, validate SHA-256 against local mod. */
            if (e->type == MANIFEST_TYPE_COMPONENT) {
                /* Check whether local mod has matching SHA-256.
                 * A zero sha256 in the manifest means the host didn't compute one — skip. */
                static const u8 s_zero32[32] = {0};
                if (memcmp(e->sha256, s_zero32, sizeof(e->sha256)) != 0) {
                    modinfo_t *localMod = modmgrFindMod(e->id);
                    if (localMod) {
                        if (memcmp(localMod->sha256, e->sha256, sizeof(e->sha256)) != 0) {
                            /* SHA-256 mismatch: we have the mod but it's the wrong version.
                             * Report as missing so the server can redistribute the correct one. */
                            char expected_hex[SHA256_HEX_SIZE];
                            char local_hex[SHA256_HEX_SIZE];
                            sha256ToHex(e->sha256, expected_hex);
                            sha256ToHex(localMod->sha256, local_hex);
                            sysLogPrintf(LOG_WARNING,
                                         "MANIFEST: [%2d] COMPONENT id='%s' SHA-256 MISMATCH: want %s got %s",
                                         i, e->id, expected_hex, local_hex);
                            if (num_missing < 255) {
                                strncpy(missing_ids[num_missing], e->id, CATALOG_ID_LEN - 1);
                                missing_ids[num_missing][CATALOG_ID_LEN - 1] = '\0';
                                num_missing++;
                            }
                            continue;
                        }
                    }
                }
            }
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST: [%2d] %-9s id='%s' — OK",
                         i, type_name, e->id);
            continue;
        }

        /* Not found.  Non-component ids in non-base namespaces are treated as
         * missing (mod content not present/registered locally). */
        if (e->type != MANIFEST_TYPE_COMPONENT) {
            const char *colon = strchr(e->id, ':');
            const s32 non_base_namespace = (colon && strncmp(e->id, "base:", 5) != 0);

            if (non_base_namespace) {
                sysLogPrintf(LOG_WARNING,
                             "MANIFEST: [%2d] %-9s id='%s' — unresolved non-base namespace, marking MISSING",
                             i, type_name, e->id);
                if (num_missing < 255) {
                    strncpy(missing_ids[num_missing], e->id, CATALOG_ID_LEN - 1);
                    missing_ids[num_missing][CATALOG_ID_LEN - 1] = '\0';
                    num_missing++;
                }
                continue;
            }

            sysLogPrintf(LOG_NOTE,
                         "MANIFEST: [%2d] %-9s id='%s' — not in catalog, assumed base game",
                         i, type_name, e->id);
            continue;
        }

        /* Mod component — must be present.  Report as missing. */
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST: [%2d] COMPONENT  id='%s' — MISSING",
                     i, e->id);
        if (num_missing < 255) {
            strncpy(missing_ids[num_missing], e->id, CATALOG_ID_LEN - 1);
            missing_ids[num_missing][CATALOG_ID_LEN - 1] = '\0';
            num_missing++;
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
                                 status,
                                 (const char (*)[CATALOG_ID_LEN])missing_ids,
                                 (u8)num_missing);
    netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}
