/**
 * sessioncatalog.c -- SA-1: Per-match session catalog implementation
 *
 * Builds a compact u16 wire ID table from a match manifest and broadcasts it
 * to all room clients via SVC_SESSION_CATALOG (0x67).  Clients receive and
 * store it for the duration of the match, then tear it down on SVC_STAGE_END.
 *
 * Wire ID assignment: entries are numbered 1..num_entries in manifest order.
 * Wire ID 0 is reserved as "not found / no entry".
 *
 * Client-side resolution: each received entry is resolved to a local catalog
 * pointer via assetCatalogResolveByNetHash() (CRC32 primary), with string ID
 * fallback.  Results are stored in s_LocalTranslation[] for O(1) gameplay lookup.
 */

#include <string.h>
#include <stdio.h>
#include "types.h"
#include "system.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netmanifest.h"
#include "net/sessioncatalog.h"
#include "assetcatalog.h"

/* -------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

session_catalog_t g_SessionCatalog;

/* -------------------------------------------------------------------------
 * Module-private state
 * ------------------------------------------------------------------------- */

/* Client-side translation table indexed by wire_id (1-based; index 0 unused). */
static session_translation_t s_LocalTranslation[SESSION_CATALOG_MAX_ENTRIES + 1];

/* -------------------------------------------------------------------------
 * Server-side: build
 * ------------------------------------------------------------------------- */

void sessionCatalogBuild(const match_manifest_t *manifest)
{
    s32 i;
    u16 count;
    session_catalog_entry_t *e;
    const asset_entry_t *ae;

    memset(&g_SessionCatalog, 0, sizeof(g_SessionCatalog));

    if (!manifest || manifest->num_entries == 0) {
        return;
    }

    count = manifest->num_entries;
    if (count > SESSION_CATALOG_MAX_ENTRIES) {
        count = SESSION_CATALOG_MAX_ENTRIES;
    }

    for (i = 0; i < (s32)count; i++) {
        e = &g_SessionCatalog.entries[i];
        e->wire_id    = (u16)(i + 1);  /* 1-based; 0 reserved as "not found" */
        e->asset_type = manifest->entries[i].type;
        strncpy(e->catalog_id, manifest->entries[i].id, SESSION_CATALOG_ID_LEN - 1);
        e->catalog_id[SESSION_CATALOG_ID_LEN - 1] = '\0';

        /* Populate CRC32 net_hash from asset catalog entry for client-side verification. */
        /* Note: manifest->entries[i].net_hash is FNV-1a; we need CRC32 from asset_entry_t */
        /* for compatibility with assetCatalogResolveByNetHash() on the client side.        */
        ae = assetCatalogResolve(manifest->entries[i].id);
        if (ae) {
            e->net_hash = ae->net_hash;  /* CRC32 from catalog entry */
        } else {
            e->net_hash = 0;  /* fallback -- should not happen if manifest is valid */
        }
    }

    g_SessionCatalog.num_entries = count;
}

/* -------------------------------------------------------------------------
 * Server-side: broadcast
 * ------------------------------------------------------------------------- */

void sessionCatalogBroadcast(void)
{
    s32 i;
    const session_catalog_entry_t *e;

    if (g_SessionCatalog.num_entries == 0) {
        return;
    }

    netbufStartWrite(&g_NetMsgRel);
    netbufWriteU8(&g_NetMsgRel, SVC_SESSION_CATALOG);
    netbufWriteU16(&g_NetMsgRel, g_SessionCatalog.num_entries);

    for (i = 0; i < (s32)g_SessionCatalog.num_entries; i++) {
        e = &g_SessionCatalog.entries[i];
        netbufWriteU16(&g_NetMsgRel, e->wire_id);
        netbufWriteU8(&g_NetMsgRel,  e->asset_type);
        netbufWriteU32(&g_NetMsgRel, e->net_hash);
        netbufWriteStr(&g_NetMsgRel, e->catalog_id);
    }

    netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);

    sysLogPrintf(LOG_NOTE, "NET: SVC_SESSION_CATALOG broadcast (%u entries)",
                 (unsigned)g_SessionCatalog.num_entries);
}

/* -------------------------------------------------------------------------
 * Client-side: receive
 * ------------------------------------------------------------------------- */

void sessionCatalogReceive(struct netbuf *src)
{
    s32 i;
    u16 count;
    u16 wire_id;
    u8  asset_type;
    u32 net_hash;
    char *id;
    session_catalog_entry_t *e;
    session_translation_t *t;
    const asset_entry_t *local;

    memset(&g_SessionCatalog, 0, sizeof(g_SessionCatalog));
    memset(s_LocalTranslation, 0, sizeof(s_LocalTranslation));

    count = netbufReadU16(src);
    if (src->error) {
        sysLogPrintf(LOG_WARNING, "NET: SVC_SESSION_CATALOG: malformed header");
        return;
    }

    if (count > SESSION_CATALOG_MAX_ENTRIES) {
        sysLogPrintf(LOG_WARNING, "NET: SVC_SESSION_CATALOG: entry count %u exceeds max %u, clamping",
                     (unsigned)count, (unsigned)SESSION_CATALOG_MAX_ENTRIES);
        count = SESSION_CATALOG_MAX_ENTRIES;
    }

    for (i = 0; i < (s32)count; i++) {
        wire_id    = netbufReadU16(src);
        asset_type = netbufReadU8(src);
        net_hash   = netbufReadU32(src);
        id         = netbufReadStr(src);

        if (src->error) {
            sysLogPrintf(LOG_WARNING, "NET: SVC_SESSION_CATALOG: parse error at entry %d", i);
            break;
        }

        e             = &g_SessionCatalog.entries[i];
        e->wire_id    = wire_id;
        e->asset_type = asset_type;
        e->net_hash   = net_hash;
        if (id) {
            strncpy(e->catalog_id, id, SESSION_CATALOG_ID_LEN - 1);
            e->catalog_id[SESSION_CATALOG_ID_LEN - 1] = '\0';
        }

        /* Resolve local catalog entry: CRC32 hash first, string ID fallback. */
        /* Design doc §5.3 and §5.8: missing at this point is a pipeline bug. */
        local = assetCatalogResolveByNetHash(net_hash);
        if (!local && e->catalog_id[0]) {
            local = assetCatalogResolve(e->catalog_id);
        }

        /* Store result in translation table (wire_id is 1-based; 0 unused). */
        if (wire_id > 0 && wire_id <= SESSION_CATALOG_MAX_ENTRIES) {
            t             = &s_LocalTranslation[wire_id];
            t->session_id = wire_id;
            t->local_entry = local;
            t->resolved   = (local != NULL) ? 1 : 0;
        }

        if (!local) {
            sysLogPrintf(LOG_WARNING,
                "[SESSION-CATALOG-ASSERT] entry '%s' (hash 0x%08x) not resolved "
                "in local catalog -- pipeline bug",
                e->catalog_id, (unsigned)net_hash);
        }
    }

    if (!src->error) {
        g_SessionCatalog.num_entries = count;
        sysLogPrintf(LOG_NOTE, "NET: SVC_SESSION_CATALOG received (%u entries)", (unsigned)count);
    }
}

/* -------------------------------------------------------------------------
 * Client-side: O(1) translation lookups
 * ------------------------------------------------------------------------- */

const asset_entry_t *sessionCatalogLocalResolve(u16 session_id)
{
    if (session_id == 0 || session_id > SESSION_CATALOG_MAX_ENTRIES) {
        return NULL;
    }
    return s_LocalTranslation[session_id].local_entry;
}

s32 sessionCatalogLocalIsResolved(u16 session_id)
{
    if (session_id == 0 || session_id > SESSION_CATALOG_MAX_ENTRIES) {
        return 0;
    }
    return s_LocalTranslation[session_id].resolved;
}

/* -------------------------------------------------------------------------
 * Shared: teardown
 * ------------------------------------------------------------------------- */

void sessionCatalogTeardown(void)
{
    memset(&g_SessionCatalog, 0, sizeof(g_SessionCatalog));
    memset(s_LocalTranslation, 0, sizeof(s_LocalTranslation));
    sysLogPrintf(LOG_NOTE, "NET: session catalog torn down");
}

/* -------------------------------------------------------------------------
 * Shared: active check
 * ------------------------------------------------------------------------- */

s32 sessionCatalogIsActive(void)
{
    return (g_SessionCatalog.num_entries > 0) ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Shared: logging
 * ------------------------------------------------------------------------- */

void sessionCatalogLogMapping(void)
{
    s32 i;
    const session_catalog_entry_t *e;

    sysLogPrintf(LOG_NOTE, "NET: session catalog (%u entries):",
                 (unsigned)g_SessionCatalog.num_entries);

    for (i = 0; i < (s32)g_SessionCatalog.num_entries; i++) {
        e = &g_SessionCatalog.entries[i];
        sysLogPrintf(LOG_NOTE, "  [%d] wire=%u type=%u hash=0x%08x id=%s",
                     i, (unsigned)e->wire_id,
                     (unsigned)e->asset_type, (unsigned)e->net_hash, e->catalog_id);
    }
}

/* -------------------------------------------------------------------------
 * Server-side: lookups
 * ------------------------------------------------------------------------- */

u16 sessionCatalogGetId(const char *asset_id)
{
    s32 i;

    for (i = 0; i < (s32)g_SessionCatalog.num_entries; i++) {
        if (strncmp(g_SessionCatalog.entries[i].catalog_id, asset_id, SESSION_CATALOG_ID_LEN) == 0) {
            return g_SessionCatalog.entries[i].wire_id;
        }
    }
    return 0;
}

u16 sessionCatalogGetIdByHash(u32 net_hash)
{
    s32 i;

    for (i = 0; i < (s32)g_SessionCatalog.num_entries; i++) {
        if (g_SessionCatalog.entries[i].net_hash == net_hash) {
            return g_SessionCatalog.entries[i].wire_id;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Shared: lookup by wire_id
 * ------------------------------------------------------------------------- */

const session_catalog_entry_t *sessionCatalogLookupEntry(u16 wire_id)
{
    s32 i;

    for (i = 0; i < (s32)g_SessionCatalog.num_entries; i++) {
        if (g_SessionCatalog.entries[i].wire_id == wire_id) {
            return &g_SessionCatalog.entries[i];
        }
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Compatibility alias: retained for existing call sites
 * ------------------------------------------------------------------------- */

u16 sessionCatalogLookupWireId(const char *catalog_id)
{
    return sessionCatalogGetId(catalog_id);
}
