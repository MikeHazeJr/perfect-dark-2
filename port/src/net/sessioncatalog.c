/**
 * sessioncatalog.c -- SA-1: Per-match session catalog implementation
 *
 * Builds a compact u16 wire ID table from a match manifest and broadcasts it
 * to all room clients via SVC_SESSION_CATALOG (0x67).  Clients receive and
 * store it for the duration of the match, then tear it down on SVC_STAGE_END.
 *
 * Wire ID assignment: entries are numbered 1..num_entries in manifest order.
 * Wire ID 0 is reserved as "not found / no entry".
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

/* -------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

session_catalog_t g_SessionCatalog;

/* -------------------------------------------------------------------------
 * Server-side: build
 * ------------------------------------------------------------------------- */

void sessionCatalogBuild(const match_manifest_t *manifest)
{
    memset(&g_SessionCatalog, 0, sizeof(g_SessionCatalog));

    if (!manifest || manifest->num_entries == 0) {
        return;
    }

    u16 count = manifest->num_entries;
    if (count > SESSION_CATALOG_MAX_ENTRIES) {
        count = SESSION_CATALOG_MAX_ENTRIES;
    }

    for (u16 i = 0; i < count; i++) {
        session_catalog_entry_t *e = &g_SessionCatalog.entries[i];
        e->wire_id    = (u16)(i + 1);  /* 1-based; 0 reserved as "not found" */
        e->asset_type = manifest->entries[i].type;
        strncpy(e->catalog_id, manifest->entries[i].id, SESSION_CATALOG_ID_LEN - 1);
        e->catalog_id[SESSION_CATALOG_ID_LEN - 1] = '\0';
    }

    g_SessionCatalog.num_entries = count;
}

/* -------------------------------------------------------------------------
 * Server-side: broadcast
 * ------------------------------------------------------------------------- */

void sessionCatalogBroadcast(void)
{
    if (g_SessionCatalog.num_entries == 0) {
        return;
    }

    netbufStartWrite(&g_NetMsgRel);
    netbufWriteU8(&g_NetMsgRel, SVC_SESSION_CATALOG);
    netbufWriteU16(&g_NetMsgRel, g_SessionCatalog.num_entries);

    for (u16 i = 0; i < g_SessionCatalog.num_entries; i++) {
        const session_catalog_entry_t *e = &g_SessionCatalog.entries[i];
        netbufWriteU16(&g_NetMsgRel, e->wire_id);
        netbufWriteU8(&g_NetMsgRel,  e->asset_type);
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
    memset(&g_SessionCatalog, 0, sizeof(g_SessionCatalog));

    u16 count = netbufReadU16(src);
    if (src->error) {
        sysLogPrintf(LOG_WARNING, "NET: SVC_SESSION_CATALOG: malformed header");
        return;
    }

    if (count > SESSION_CATALOG_MAX_ENTRIES) {
        sysLogPrintf(LOG_WARNING, "NET: SVC_SESSION_CATALOG: entry count %u exceeds max %u, clamping",
                     (unsigned)count, (unsigned)SESSION_CATALOG_MAX_ENTRIES);
        count = SESSION_CATALOG_MAX_ENTRIES;
    }

    for (u16 i = 0; i < count; i++) {
        session_catalog_entry_t *e = &g_SessionCatalog.entries[i];
        e->wire_id    = netbufReadU16(src);
        e->asset_type = netbufReadU8(src);
        char *id      = netbufReadStr(src);
        if (src->error) {
            sysLogPrintf(LOG_WARNING, "NET: SVC_SESSION_CATALOG: parse error at entry %u", (unsigned)i);
            break;
        }
        if (id) {
            strncpy(e->catalog_id, id, SESSION_CATALOG_ID_LEN - 1);
            e->catalog_id[SESSION_CATALOG_ID_LEN - 1] = '\0';
        }
    }

    if (!src->error) {
        g_SessionCatalog.num_entries = count;
        sysLogPrintf(LOG_NOTE, "NET: SVC_SESSION_CATALOG received (%u entries)", (unsigned)count);
    }
}

/* -------------------------------------------------------------------------
 * Shared: teardown
 * ------------------------------------------------------------------------- */

void sessionCatalogTeardown(void)
{
    memset(&g_SessionCatalog, 0, sizeof(g_SessionCatalog));
    sysLogPrintf(LOG_NOTE, "NET: session catalog torn down");
}

/* -------------------------------------------------------------------------
 * Shared: logging
 * ------------------------------------------------------------------------- */

void sessionCatalogLogMapping(void)
{
    sysLogPrintf(LOG_NOTE, "NET: session catalog (%u entries):",
                 (unsigned)g_SessionCatalog.num_entries);
    for (u16 i = 0; i < g_SessionCatalog.num_entries; i++) {
        const session_catalog_entry_t *e = &g_SessionCatalog.entries[i];
        sysLogPrintf(LOG_NOTE, "  [%u] wire=%u type=%u id=%s",
                     (unsigned)i, (unsigned)e->wire_id,
                     (unsigned)e->asset_type, e->catalog_id);
    }
}

/* -------------------------------------------------------------------------
 * Shared: lookup
 * ------------------------------------------------------------------------- */

u16 sessionCatalogLookupWireId(const char *catalog_id)
{
    for (u16 i = 0; i < g_SessionCatalog.num_entries; i++) {
        if (strncmp(g_SessionCatalog.entries[i].catalog_id, catalog_id, SESSION_CATALOG_ID_LEN) == 0) {
            return g_SessionCatalog.entries[i].wire_id;
        }
    }
    return 0;
}

const session_catalog_entry_t *sessionCatalogLookupEntry(u16 wire_id)
{
    for (u16 i = 0; i < g_SessionCatalog.num_entries; i++) {
        if (g_SessionCatalog.entries[i].wire_id == wire_id) {
            return &g_SessionCatalog.entries[i];
        }
    }
    return NULL;
}
