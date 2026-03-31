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

#include <string.h>
#include <stdio.h>
#include "types.h"
#include "bss.h"
#include "system.h"
#include "constants.h"
#include "net/netmanifest.h"
#include "net/net.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "assetcatalog.h"
#include "modmgr.h"

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

/* =========================================================================
 * Public API
 * ========================================================================= */

void manifestClear(match_manifest_t *m)
{
    memset(m, 0, sizeof(*m));
}

void manifestAddEntry(match_manifest_t *m, u32 net_hash, const char *id,
                      u8 type, u8 slot_index)
{
    if (m->num_entries >= MANIFEST_MAX_ENTRIES) {
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST: manifest full (%d entries max), dropping '%s'",
                     MANIFEST_MAX_ENTRIES, id ? id : "?");
        return;
    }

    /* Dedup by net_hash — skip if already present */
    for (s32 i = 0; i < m->num_entries; i++) {
        if (m->entries[i].net_hash == net_hash) {
            return;
        }
    }

    match_manifest_entry_t *e = &m->entries[m->num_entries++];
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
    for (s32 i = 0; i < m->num_entries; i++) {
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
    (void)room;  /* Phase E: will scope to room->clients[] */
    (void)cfg;   /* Phase E: will use cfg->stagenum, cfg->slots[], etc. */

    manifestClear(out);

    /* ---- Stage ---- */
    {
        char id[64];
        snprintf(id, sizeof(id), "stage_0x%02x", (unsigned)g_MpSetup.stagenum);

        /* Try catalog first (resolves on client / host where base catalog loaded) */
        const asset_entry_t *e = assetCatalogResolve(id);
        if (e) {
            manifestAddEntry(out, e->net_hash, e->id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        } else {
            manifestAddEntry(out, s_fnv1a(id), id,
                             MANIFEST_TYPE_STAGE, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- Weapons ---- */
    for (s32 i = 0; i < NUM_MPWEAPONSLOTS; i++) {
        const u8 wnum = g_MpSetup.weapons[i];
        if (wnum == 0) {
            continue;
        }
        char id[64];
        snprintf(id, sizeof(id), "weapon_%d", (int)wnum);

        const asset_entry_t *e = assetCatalogResolve(id);
        if (e) {
            manifestAddEntry(out, e->net_hash, e->id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        } else {
            manifestAddEntry(out, s_fnv1a(id), id,
                             MANIFEST_TYPE_WEAPON, MANIFEST_SLOT_MATCH);
        }
    }

    /* ---- Players: iterate connected clients ---- */
    u8 slot_index = 0;
    for (s32 ci = 0; ci < NET_MAX_CLIENTS; ci++) {
        const struct netclient *ncl = &g_NetClients[ci];
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
        for (s32 bi = 0; bi < num_bots && bi < MAX_BOTS; bi++) {
            const u8 bodynum = g_BotConfigsArray[bi].base.mpbodynum;
            const u8 headnum = g_BotConfigsArray[bi].base.mpheadnum;

            char body_id[64], head_id[64];
            snprintf(body_id, sizeof(body_id), "body_%d", (int)bodynum);
            snprintf(head_id, sizeof(head_id), "head_%d", (int)headnum);

            const asset_entry_t *be = assetCatalogResolve(body_id);
            if (be) {
                manifestAddEntry(out, be->net_hash, be->id,
                                 MANIFEST_TYPE_BODY, slot_index);
            } else {
                manifestAddEntry(out, s_fnv1a(body_id), body_id,
                                 MANIFEST_TYPE_BODY, slot_index);
            }

            const asset_entry_t *he = assetCatalogResolve(head_id);
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
        for (s32 i = 0; i < num_mods; i++) {
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
    static const char *s_type_names[] = {
        "BODY", "HEAD", "STAGE", "WEAPON", "COMPONENT"
    };

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST: built — %d entries, hash=0x%08x",
                 (int)m->num_entries, (unsigned)m->manifest_hash);

    for (s32 i = 0; i < m->num_entries; i++) {
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
        "BODY", "HEAD", "STAGE", "WEAPON", "COMPONENT"
    };

    u32 missing_hashes[MANIFEST_MAX_ENTRIES];
    u8  num_missing = 0;

    sysLogPrintf(LOG_NOTE,
                 "MANIFEST: checking %u entries against local catalog (hash=0x%08x)",
                 (unsigned)manifest->num_entries, (unsigned)manifest->manifest_hash);

    for (u16 i = 0; i < manifest->num_entries; i++) {
        const match_manifest_entry_t *e = &manifest->entries[i];
        const char *type_name = (e->type < ARRAYCOUNT(s_type_names))
                                ? s_type_names[e->type] : "?";

        /* Try hash lookup first — most reliable since net_hash is the canonical
         * wire identity.  Then fall back to string ID for synthetic entries. */
        const asset_entry_t *local = assetCatalogResolveByNetHash(e->net_hash);
        if (!local && e->id[0]) {
            local = assetCatalogResolve(e->id);
        }

        if (local) {
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST: [%2u] %-9s hash=0x%08x id='%s' — OK",
                         (unsigned)i, type_name, (unsigned)e->net_hash, e->id);
            continue;
        }

        /* Not found.  Non-component types are base game assets — always present
         * locally even if the catalog doesn't have a named entry for them. */
        if (e->type != MANIFEST_TYPE_COMPONENT) {
            sysLogPrintf(LOG_NOTE,
                         "MANIFEST: [%2u] %-9s hash=0x%08x id='%s' — not in catalog, assumed base game",
                         (unsigned)i, type_name, (unsigned)e->net_hash, e->id);
            continue;
        }

        /* Mod component — must be present.  Report as missing. */
        sysLogPrintf(LOG_WARNING,
                     "MANIFEST: [%2u] COMPONENT  hash=0x%08x id='%s' — MISSING",
                     (unsigned)i, (unsigned)e->net_hash, e->id);
        if (num_missing < MANIFEST_MAX_ENTRIES) {
            missing_hashes[num_missing++] = e->net_hash;
        }
    }

    u8 status = (num_missing == 0) ? MANIFEST_STATUS_READY
                                   : MANIFEST_STATUS_NEED_ASSETS;

    if (status == MANIFEST_STATUS_READY) {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST: check passed — %u/%u entries OK, sending READY",
                     (unsigned)manifest->num_entries, (unsigned)manifest->num_entries);
    } else {
        sysLogPrintf(LOG_NOTE,
                     "MANIFEST: check found %u missing component(s) of %u entries, sending NEED_ASSETS",
                     (unsigned)num_missing, (unsigned)manifest->num_entries);
    }

    netbufStartWrite(&g_NetMsgRel);
    netmsgClcManifestStatusWrite(&g_NetMsgRel, manifest->manifest_hash,
                                 status, missing_hashes, num_missing);
    netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
}
