/**
 * netdistrib.h -- D3R-9: Network mod component distribution.
 *
 * Two-layer system:
 *
 *   Server side: advertises its catalog to connecting clients (SVC_CATALOG_INFO),
 *   receives their diff (CLC_CATALOG_DIFF), and streams missing components as
 *   compressed archives over NETCHAN_TRANSFER.
 *
 *   Client side: receives the catalog info, computes what it's missing, requests
 *   transfer, receives chunks, extracts to mods/.temp/, and hot-registers the
 *   new components in the Asset Catalog (temporary=1).
 *
 *   Crash recovery: a .crash_state file in mods/.temp/ tracks whether the game
 *   exited cleanly. On the next launch, if temp mods exist and crash_count > 0,
 *   the UI shows a recovery prompt (Keep / Keep Disabled / Discard).
 *
 * Archive format (PDCA -- Perfect Dark Component Archive):
 *   u32 magic        "PDCA" (0x41434450)
 *   u16 file_count
 *   for each file:
 *     u16 path_len   length of relative path string (including null terminator)
 *     char path[]    relative path within component dir (null-terminated)
 *     u32 data_len   byte count of file data
 *     u8  data[]     raw file bytes
 *
 * Wire chunks: the PDCA archive is zlib-compressed and split into
 * NET_DISTRIB_CHUNK_SIZE blocks. Each block is one SVC_DISTRIB_CHUNK packet.
 * NETCHAN_TRANSFER (reliable) guarantees ordering and delivery.
 *
 * Kill feed: SVC_LOBBY_KILL_FEED sends pre-resolved display strings so
 * spectating clients don't need catalog entries for the weapon/actor.
 */

#ifndef _IN_NETDISTRIB_H
#define _IN_NETDISTRIB_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Kill feed flags (SVC_LOBBY_KILL_FEED)
 * ======================================================================== */

#define KILLFEED_FLAG_HEADSHOT   (1 << 0)
#define KILLFEED_FLAG_EXPLOSION  (1 << 1)
#define KILLFEED_FLAG_PROXY_MINE (1 << 2)
#define KILLFEED_FLAG_MULTI_KILL (1 << 3)

/* ========================================================================
 * Client-side download state (exposed for UI polling)
 * ======================================================================== */

#define DISTRIB_CSTATE_IDLE      0
#define DISTRIB_CSTATE_DIFFING   1  /* waiting for server to start sending */
#define DISTRIB_CSTATE_RECEIVING 2  /* actively receiving chunks */
#define DISTRIB_CSTATE_DONE      3  /* all components received */
#define DISTRIB_CSTATE_ERROR     4  /* transfer failed */

typedef struct distrib_client_status {
    s32 state;                   /* DISTRIB_CSTATE_* */
    s32 missing_count;           /* total components we need */
    s32 received_count;          /* components fully received */
    char current_id[64];         /* component currently being received */
    u32 current_bytes_received;  /* bytes received for current component */
    u32 current_bytes_total;     /* total bytes expected for current component */
    s32 temporary;               /* 1 = session-only download */
    u32 session_bytes_total;     /* total bytes received this session */
} distrib_client_status_t;

/* ========================================================================
 * Kill feed ring buffer (exposed for UI polling)
 * ======================================================================== */

#define KILLFEED_MAX_ENTRIES 16
#define KILLFEED_NAME_LEN    32
#define KILLFEED_WEAPON_LEN  48

typedef struct killfeed_entry {
    char attacker[KILLFEED_NAME_LEN];
    char victim[KILLFEED_NAME_LEN];
    char weapon[KILLFEED_WEAPON_LEN];
    u8   flags;
    u32  timestamp;   /* g_NetTick when received */
    s32  active;
} killfeed_entry_t;

/* ========================================================================
 * Crash recovery state
 * ======================================================================== */

#define CRASH_RECOVERY_NONE    0  /* no temp mods, nothing to do */
#define CRASH_RECOVERY_PROMPT  1  /* temp mods present, crash detected, show prompt */
#define CRASH_RECOVERY_CLEAN   2  /* temp mods present, clean exit last time */

typedef struct crash_recovery_state {
    s32  status;              /* CRASH_RECOVERY_* */
    s32  crash_count;
    char suspect_id[64];      /* most recently loaded temp component */
    s32  temp_component_count;
} crash_recovery_state_t;

/* ========================================================================
 * Server API
 * ======================================================================== */

/**
 * Initialize the distribution subsystem.
 * Call once after catalog is populated (after assetCatalogScanComponents).
 */
void netDistribInit(void);

/**
 * Send SVC_CATALOG_INFO to a newly lobbied client.
 * Lists all enabled, non-bundled catalog entries with their net_hash + id + category.
 * Call when a client transitions to CLSTATE_LOBBY.
 */
void netDistribServerSendCatalogInfo(struct netclient *cl);

/**
 * Server received CLC_CATALOG_DIFF from a client.
 * Queues transfer of all missing components to that client.
 * Called by netmsgClcCatalogDiffRead().
 */
void netDistribServerHandleDiff(struct netclient *cl,
                                const u32 *missing_hashes,
                                u16 count,
                                u8 temporary);

/**
 * Tick the server distribution system (process pending transfers).
 * Call once per game frame from netStartFrame() or netEndFrame().
 * Sends the next pending component to each client.
 */
void netDistribServerTick(void);

/**
 * Broadcast a kill feed event to all clients currently in CLSTATE_LOBBY.
 * Pre-resolves display strings server-side; spectating clients don't need
 * catalog entries for the weapon/actor.
 *
 * @param attacker  Display name of the killer
 * @param victim    Display name of the victim (may be comma-list for multi-kill)
 * @param weapon    Display name of the weapon used
 * @param flags     KILLFEED_FLAG_* bitmask
 */
void netDistribSendKillFeed(const char *attacker, const char *victim,
                            const char *weapon, u8 flags);

/* ========================================================================
 * Client API
 * ======================================================================== */

/**
 * Client received SVC_CATALOG_INFO from server.
 * Computes diff (which components are missing locally), updates UI state,
 * and sends CLC_CATALOG_DIFF. If nothing is missing, sends empty diff.
 * Called by netmsgSvcCatalogInfoRead().
 */
void netDistribClientHandleCatalogInfo(const u32 *hashes,
                                       const char (*ids)[64],
                                       const char (*categories)[64],
                                       u16 count);

/**
 * Client received SVC_DISTRIB_BEGIN from server.
 * Allocates receive buffer for the component.
 * Called by netmsgSvcDistribBeginRead().
 */
void netDistribClientHandleBegin(u32 net_hash, const char *id,
                                  const char *category,
                                  u32 total_chunks, u32 archive_bytes,
                                  s32 temporary);

/**
 * Client received SVC_DISTRIB_CHUNK.
 * Accumulates compressed data in receive buffer.
 * Called by netmsgSvcDistribChunkRead().
 */
void netDistribClientHandleChunk(u32 net_hash, u16 chunk_idx,
                                  u8 compression,
                                  const u8 *data, u16 data_len);

/**
 * Client received SVC_DISTRIB_END.
 * Decompresses accumulated archive, extracts files, hot-registers in catalog.
 * Called by netmsgSvcDistribEndRead().
 */
void netDistribClientHandleEnd(u32 net_hash, u8 success);

/**
 * Client received SVC_LOBBY_KILL_FEED.
 * Adds entry to the kill feed ring buffer for UI display.
 */
void netDistribClientHandleKillFeed(const char *attacker, const char *victim,
                                    const char *weapon, u8 flags);

/**
 * Get current client download status (for lobby UI).
 * Always safe to call; returns DISTRIB_CSTATE_IDLE when not downloading.
 */
void netDistribClientGetStatus(distrib_client_status_t *out);

/**
 * Get kill feed entries for UI display.
 * Fills out[] with up to maxout entries, newest first.
 * Returns number of active entries (may be 0).
 */
s32 netDistribClientGetKillFeed(killfeed_entry_t *out, s32 maxout);

/**
 * Set whether the current download should be permanent or session-only.
 * Must be called before CLC_CATALOG_DIFF is sent (i.e., before the UI prompt
 * is dismissed). The default is permanent.
 */
void netDistribClientSetTemporary(s32 temporary);

/* ========================================================================
 * Crash Recovery API
 * ======================================================================== */

/**
 * Check for crash recovery state on startup.
 * Reads mods/.temp/.crash_state if it exists.
 * Call during startup, after fsInit() and before catalog scan.
 * Returns CRASH_RECOVERY_NONE, CRASH_RECOVERY_PROMPT, or CRASH_RECOVERY_CLEAN.
 */
s32 netCrashRecoveryCheck(crash_recovery_state_t *out);

/**
 * Apply crash recovery action.
 * action: 0 = keep (load temp mods normally)
 *         1 = keep but disable (files stay, enabled=false in catalog)
 *         2 = discard (delete mods/.temp/ contents)
 * Call after the user selects an option from the recovery prompt.
 */
void netCrashRecoveryApply(s32 action);

/**
 * Record a clean launch (game started successfully).
 * Increments crash counter (dirty until netCrashRecoveryMarkClean is called).
 * Call early in pdmain() after loading is complete.
 */
void netCrashRecoveryMarkLaunching(void);

/**
 * Mark a clean exit (game is about to shut down normally).
 * Resets crash counter and removes suspect state.
 * Call from pdmain() shutdown path or SDL quit handler.
 */
void netCrashRecoveryMarkClean(void);

#ifdef __cplusplus
}
#endif

#endif /* _IN_NETDISTRIB_H */
