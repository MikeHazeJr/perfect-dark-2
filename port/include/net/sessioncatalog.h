/**
 * sessioncatalog.h -- SA-1: Per-match session catalog (wire ID translation layer)
 *
 * Maps catalog string IDs to compact u16 wire IDs for the duration of one match.
 * Built server-side from the match manifest, broadcast via SVC_SESSION_CATALOG (0x67),
 * resolved client-side to local catalog entries.
 *
 * Wire format (SVC_SESSION_CATALOG):
 *   u8   opcode = 0x67
 *   u16  num_entries
 *   [for each entry]:
 *     u16  wire_id       compact match-scoped ID
 *     u8   asset_type    MANIFEST_TYPE_* from netmanifest.h
 *     str  catalog_id    null-terminated catalog string ID
 *
 * Lifecycle:
 *   Server: sessionCatalogBuild()     -- after manifestBuild()
 *           sessionCatalogBroadcast() -- after SVC_MATCH_MANIFEST broadcast
 *           sessionCatalogTeardown()  -- on match end / room reset
 *   Client: sessionCatalogReceive()   -- in SVC_SESSION_CATALOG handler
 *           sessionCatalogTeardown()  -- in SVC_STAGE_END handler
 */

#ifndef _IN_SESSIONCATALOG_H
#define _IN_SESSIONCATALOG_H

#include <PR/ultratypes.h>
#include "net/netbuf.h"
#include "net/netmanifest.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum entries in the session catalog (matches manifest limit) */
#define SESSION_CATALOG_MAX_ENTRIES MANIFEST_MAX_ENTRIES

/* Max length of a catalog ID string in this subsystem (mirrors manifest entry id size) */
#define SESSION_CATALOG_ID_LEN 64

/**
 * One entry in the session catalog.
 */
typedef struct {
    u16  wire_id;                               /**< compact match-scoped wire ID */
    u8   asset_type;                            /**< MANIFEST_TYPE_* */
    char catalog_id[SESSION_CATALOG_ID_LEN];    /**< catalog string ID */
} session_catalog_entry_t;

/**
 * Full session catalog for one match.
 * Populated server-side from manifest, received client-side via SVC_SESSION_CATALOG.
 */
typedef struct {
    u16                     num_entries;
    session_catalog_entry_t entries[SESSION_CATALOG_MAX_ENTRIES];
} session_catalog_t;

/** Global session catalog (server and client share the same struct, built/received differently). */
extern session_catalog_t g_SessionCatalog;

/**
 * Build the session catalog from a manifest.
 * Assigns sequential wire_ids starting at 1 (0 is reserved as "no entry").
 * Called server-side after manifestBuild().
 */
void sessionCatalogBuild(const match_manifest_t *manifest);

/**
 * Broadcast the session catalog to all room clients (SVC_SESSION_CATALOG).
 * Called server-side after SVC_MATCH_MANIFEST broadcast.
 */
void sessionCatalogBroadcast(void);

/**
 * Receive and parse a session catalog from a network buffer.
 * Called client-side in the SVC_SESSION_CATALOG handler.
 */
void sessionCatalogReceive(struct netbuf *src);

/**
 * Tear down the session catalog (zero it out).
 * Called on match end (client: SVC_STAGE_END, server: room reset).
 */
void sessionCatalogTeardown(void);

/**
 * Log the current catalog mapping to the system log.
 * Called server-side after sessionCatalogBuild() for debugging.
 */
void sessionCatalogLogMapping(void);

/**
 * Look up a wire_id by catalog string ID.
 * Returns 0 if not found.
 */
u16 sessionCatalogLookupWireId(const char *catalog_id);

/**
 * Look up a catalog entry by wire_id.
 * Returns NULL if not found.
 */
const session_catalog_entry_t *sessionCatalogLookupEntry(u16 wire_id);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SESSIONCATALOG_H */
