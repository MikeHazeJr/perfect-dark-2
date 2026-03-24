/**
 * netdistrib.c -- D3R-9: Network mod component distribution.
 *
 * Implements server-side distribution and client-side reception of mod
 * components. See netdistrib.h for the architecture overview.
 *
 * Wire format (PDCA archive, before compression):
 *   u32  magic       0x41434450 ("PDCA")
 *   u16  file_count
 *   [file_count entries]:
 *     u16 path_len   (including null terminator)
 *     char path[]    (relative to component dir, null-terminated)
 *     u32 data_len
 *     u8  data[data_len]
 *
 * Transfer sequence:
 *   1. Server sends SVC_CATALOG_INFO (list of non-bundled enabled entries).
 *   2. Client diffs, sends CLC_CATALOG_DIFF with missing net_hashes.
 *   3. Server queues each missing component for transfer.
 *   4. Per frame: server calls netDistribServerTick() which sends the next
 *      pending component (SVC_DISTRIB_BEGIN then all SVC_DISTRIB_CHUNK).
 *   5. After last chunk: server sends SVC_DISTRIB_END.
 *   6. Client decompresses, extracts to mods/.temp/ (or mods/ if permanent).
 *   7. Client hot-registers the component in the Asset Catalog.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

#include "types.h"
#include "constants.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "net/netdistrib.h"
#include "net/netenet.h"
#include "assetcatalog.h"
#include "assetcatalog_scanner.h"
#include "system.h"
#include "fs.h"

/* ========================================================================
 * Constants
 * ======================================================================== */

#define PDCA_MAGIC       0x41434450u   /* "PDCA" little-endian */
#define CRASH_STATE_FILE ".crash_state"
#define TEMP_SUBDIR      ".temp"

/* Max pending transfers per server (one per client × max concurrent) */
#define DISTRIB_MAX_QUEUE 64

/* ========================================================================
 * Server Transfer Queue
 * ======================================================================== */

typedef struct distrib_queue_entry {
    struct netclient *cl;        /* destination client */
    u32  net_hash;               /* component to send */
    s32  active;
    s32  temporary;              /* client requested session-only */
} distrib_queue_entry_t;

static distrib_queue_entry_t s_Queue[DISTRIB_MAX_QUEUE];
static s32 s_QueueHead = 0;
static s32 s_QueueTail = 0;
static s32 s_Initialized = 0;

/* ========================================================================
 * Client Receive State
 * ======================================================================== */

#define RECV_SLOTS 4  /* max simultaneous incoming components (usually 1) */

typedef struct distrib_recv_slot {
    s32  active;
    u32  net_hash;
    char id[64];
    char category[64];
    u16  total_chunks;
    u16  chunks_received;
    u32  archive_bytes;       /* expected uncompressed archive size */
    u8  *compressed_buf;      /* accumulates compressed chunks */
    u32  compressed_cap;
    u32  compressed_len;
    s32  temporary;
} distrib_recv_slot_t;

static distrib_recv_slot_t s_RecvSlots[RECV_SLOTS];

/* Client-visible status */
static distrib_client_status_t s_ClientStatus;

/* Kill feed ring buffer */
static killfeed_entry_t s_KillFeed[KILLFEED_MAX_ENTRIES];
static s32 s_KillFeedNext = 0;  /* circular write head */

/* Pending diff decision (before user confirms) */
static s32 s_PendingTemporary = 0;

/* ========================================================================
 * PDCA Archive Builder (server side)
 * ======================================================================== */

/**
 * Recursively enumerate files in a directory, appending entries to a
 * growing heap buffer. Returns the new buffer and updated size.
 * The prefix is the relative path from the component root.
 */
static u8 *buildArchiveDir(u8 *buf, u32 *buf_len, u32 *buf_cap,
                            const char *abspath, const char *relprefix)
{
    DIR *d = opendir(abspath);
    if (!d) {
        return buf;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;  /* skip . .. and hidden files */
        }

        char childabs[FS_MAXPATH];
        char childrel[FS_MAXPATH];

        snprintf(childabs, sizeof(childabs), "%s/%s", abspath, ent->d_name);
        if (relprefix[0]) {
            snprintf(childrel, sizeof(childrel), "%s/%s", relprefix, ent->d_name);
        } else {
            snprintf(childrel, sizeof(childrel), "%s", ent->d_name);
        }

        struct stat st;
        if (stat(childabs, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            buf = buildArchiveDir(buf, buf_len, buf_cap, childabs, childrel);
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            continue;
        }

        /* Read file data */
        FILE *fp = fopen(childabs, "rb");
        if (!fp) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: can't open %s", childabs);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        u32 fsize = (u32)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsize > NET_DISTRIB_MAX_COMP) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: skipping oversized file %s (%u bytes)", childabs, fsize);
            fclose(fp);
            continue;
        }

        /* Grow buffer: path_len(2) + path(n+1) + data_len(4) + data(fsize) */
        u16 path_len = (u16)(strlen(childrel) + 1);
        u32 entry_size = 2 + path_len + 4 + fsize;

        while (*buf_len + entry_size > *buf_cap) {
            *buf_cap = (*buf_cap < 65536) ? 65536 : *buf_cap * 2;
            u8 *newbuf = (u8 *)realloc(buf, *buf_cap);
            if (!newbuf) {
                sysLogPrintf(LOG_ERROR, "DISTRIB: OOM building archive (cap=%u)", *buf_cap);
                fclose(fp);
                closedir(d);
                return buf;
            }
            buf = newbuf;
        }

        /* Write entry */
        u8 *p = buf + *buf_len;
        memcpy(p, &path_len, 2);           p += 2;
        memcpy(p, childrel, path_len);     p += path_len;
        memcpy(p, &fsize, 4);              p += 4;
        if (fread(p, 1, fsize, fp) != fsize) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: short read %s", childabs);
            fclose(fp);
            continue;
        }
        *buf_len += entry_size;

        fclose(fp);
    }

    closedir(d);
    return buf;
}

/**
 * Build a PDCA archive from a component directory.
 * Returns heap-allocated buffer (caller must free), or NULL on error.
 * Sets *out_len to the archive size.
 */
static u8 *buildComponentArchive(const asset_entry_t *entry, u32 *out_len)
{
    /* Header: magic(4) + file_count(2) = 6 bytes */
    u32 buf_cap = 65536;
    u32 buf_len = 6;
    u8 *buf = (u8 *)malloc(buf_cap);
    if (!buf) {
        return NULL;
    }

    /* Reserve space for header — fill in file_count after enumerating */
    memset(buf, 0, 6);
    u32 magic = PDCA_MAGIC;
    memcpy(buf, &magic, 4);

    /* Enumerate files */
    u32 data_start = 6;
    u8 *data_buf = buf + data_start;
    u32 data_len = 0;
    u32 data_cap = buf_cap - data_start;

    /* We need a separate growing buffer for data, then prepend header */
    /* Simpler: build from offset 6, track file count */
    u16 file_count = 0;

    /* Re-use buf for the whole thing, starting data at offset 6 */
    /* This is a bit tricky: buildArchiveDir grows from *buf_len = 6 */
    buf = buildArchiveDir(buf, &buf_len, &buf_cap, entry->dirpath, "");

    /* Count files by scanning the data section */
    {
        u8 *p = buf + 6;
        u8 *end = buf + buf_len;
        while (p < end) {
            u16 path_len;
            if (p + 2 > end) break;
            memcpy(&path_len, p, 2);
            p += 2;
            if (p + path_len > end) break;
            p += path_len;
            if (p + 4 > end) break;
            u32 dlen;
            memcpy(&dlen, p, 4);
            p += 4 + dlen;
            file_count++;
        }
    }

    /* Patch file_count into header */
    memcpy(buf + 4, &file_count, 2);

    *out_len = buf_len;
    sysLogPrintf(LOG_NOTE, "DISTRIB: archive for '%s': %u files, %u bytes raw",
                 entry->id, file_count, buf_len);
    return buf;
}

/* ========================================================================
 * Server: Send Packets Directly (bypasses netbuf for large payloads)
 * ======================================================================== */

static void distribSendPacketToPeer(ENetPeer *peer, const u8 *data, u32 len, s32 chan)
{
    if (!peer || !data || !len) return;
    ENetPacket *p = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    if (!p) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: enet_packet_create failed (%u bytes)", len);
        return;
    }
    enet_peer_send(peer, (u8)chan, p);
}

/* ========================================================================
 * Server: Build and Stream Component to Client
 * ======================================================================== */

static void streamComponentToClient(struct netclient *cl, u32 net_hash, s32 temporary)
{
    const asset_entry_t *entry = assetCatalogResolveByNetHash(net_hash);
    if (!entry) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: unknown net_hash 0x%08x requested", net_hash);
        return;
    }

    if (entry->bundled) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: client requested bundled asset '%s' -- skipped", entry->id);
        return;
    }

    sysLogPrintf(LOG_NOTE, "DISTRIB: sending '%s' to %s (temporary=%d)",
                 entry->id, cl->settings.name, temporary);

    /* Build raw PDCA archive */
    u32 raw_len = 0;
    u8 *raw = buildComponentArchive(entry, &raw_len);
    if (!raw || !raw_len) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: failed to build archive for '%s'", entry->id);
        if (raw) free(raw);
        /* Send END with failure */
        netbufStartWrite(&g_NetMsgRel);
        netmsgSvcDistribEndWrite(&g_NetMsgRel, net_hash, 0);
        netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        return;
    }

    if (raw_len > NET_DISTRIB_MAX_COMP) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: '%s' exceeds 50MB limit (%u bytes) -- skipped", entry->id, raw_len);
        free(raw);
        netbufStartWrite(&g_NetMsgRel);
        netmsgSvcDistribEndWrite(&g_NetMsgRel, net_hash, 0);
        netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        return;
    }

    /* Compress with zlib */
    uLongf compressed_cap = compressBound((uLong)raw_len);
    u8 *compressed = (u8 *)malloc(compressed_cap);
    if (!compressed) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for compressed buffer (%lu bytes)", compressed_cap);
        free(raw);
        return;
    }

    uLongf compressed_len = compressed_cap;
    int zret = compress2(compressed, &compressed_len, raw, (uLong)raw_len, Z_DEFAULT_COMPRESSION);
    free(raw);

    if (zret != Z_OK) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: zlib compress failed (%d) for '%s'", zret, entry->id);
        free(compressed);
        netbufStartWrite(&g_NetMsgRel);
        netmsgSvcDistribEndWrite(&g_NetMsgRel, net_hash, 0);
        netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        return;
    }

    /* Split into chunks */
    u32 chunk_size = NET_DISTRIB_CHUNK_SIZE;
    u32 total_chunks = (compressed_len + chunk_size - 1) / chunk_size;
    if (total_chunks > 65535) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: too many chunks (%u) for '%s'", total_chunks, entry->id);
        free(compressed);
        return;
    }

    sysLogPrintf(LOG_NOTE, "DISTRIB: '%s' compressed %lu→%lu bytes, %u chunks",
                 entry->id, (unsigned long)raw_len, (unsigned long)compressed_len, total_chunks);

    /* SVC_DISTRIB_BEGIN */
    netbufStartWrite(&g_NetMsgRel);
    netmsgSvcDistribBeginWrite(&g_NetMsgRel, net_hash, entry->id, entry->category,
                               total_chunks, raw_len);
    netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);

    /* SVC_DISTRIB_CHUNK × total_chunks on NETCHAN_TRANSFER */
    for (u32 i = 0; i < total_chunks; i++) {
        u32 offset = i * chunk_size;
        u16 this_len = (u16)((offset + chunk_size <= compressed_len)
                             ? chunk_size
                             : compressed_len - offset);

        /* Build chunk packet: msgid(1) + net_hash(4) + chunk_idx(2) + compression(1)
         *                   + data_len(2) + data(this_len) */
        u32 pkt_len = 1 + 4 + 2 + 1 + 2 + this_len;
        u8 *pkt = (u8 *)malloc(pkt_len);
        if (!pkt) {
            sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for chunk packet");
            break;
        }
        u8 *p = pkt;
        *p++ = SVC_DISTRIB_CHUNK;
        memcpy(p, &net_hash, 4);    p += 4;
        u16 cidx = (u16)i;
        memcpy(p, &cidx, 2);        p += 2;
        *p++ = NET_DISTRIB_COMP_DEFLATE;
        memcpy(p, &this_len, 2);    p += 2;
        memcpy(p, compressed + offset, this_len);

        distribSendPacketToPeer(cl->peer, pkt, pkt_len, NETCHAN_TRANSFER);
        free(pkt);
    }

    free(compressed);

    /* SVC_DISTRIB_END */
    netbufStartWrite(&g_NetMsgRel);
    netmsgSvcDistribEndWrite(&g_NetMsgRel, net_hash, 1);
    netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
}

/* ========================================================================
 * Server Public API
 * ======================================================================== */

void netDistribInit(void)
{
    memset(s_Queue, 0, sizeof(s_Queue));
    memset(s_RecvSlots, 0, sizeof(s_RecvSlots));
    memset(&s_ClientStatus, 0, sizeof(s_ClientStatus));
    memset(s_KillFeed, 0, sizeof(s_KillFeed));
    s_QueueHead = 0;
    s_QueueTail = 0;
    s_KillFeedNext = 0;
    s_PendingTemporary = 0;
    s_Initialized = 1;
}

void netDistribServerSendCatalogInfo(struct netclient *cl)
{
    if (!s_Initialized || !cl) return;

    netbufStartWrite(&g_NetMsgRel);
    netmsgSvcCatalogInfoWrite(&g_NetMsgRel);
    netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);

    sysLogPrintf(LOG_NOTE, "DISTRIB: sent SVC_CATALOG_INFO to %s", cl->settings.name);
}

void netDistribServerHandleDiff(struct netclient *cl,
                                const u32 *missing_hashes,
                                u16 count,
                                u8 temporary)
{
    if (!s_Initialized || !cl || !count) {
        sysLogPrintf(LOG_NOTE, "DISTRIB: client %s has all required components", cl->settings.name);
        return;
    }

    sysLogPrintf(LOG_NOTE, "DISTRIB: client %s missing %u components (temporary=%d)",
                 cl->settings.name, count, (s32)temporary);

    for (u16 i = 0; i < count; i++) {
        /* Find a free queue slot */
        s32 found = 0;
        for (s32 j = 0; j < DISTRIB_MAX_QUEUE; j++) {
            if (!s_Queue[j].active) {
                s_Queue[j].cl = cl;
                s_Queue[j].net_hash = missing_hashes[i];
                s_Queue[j].temporary = (s32)temporary;
                s_Queue[j].active = 1;
                found = 1;
                break;
            }
        }
        if (!found) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: transfer queue full, dropping hash 0x%08x", missing_hashes[i]);
        }
    }
}

void netDistribServerTick(void)
{
    if (!s_Initialized || g_NetMode != NETMODE_SERVER) return;

    /* Process one pending entry per tick to avoid stalling the frame */
    for (s32 i = 0; i < DISTRIB_MAX_QUEUE; i++) {
        if (!s_Queue[i].active) continue;

        struct netclient *cl = s_Queue[i].cl;
        u32 net_hash = s_Queue[i].net_hash;
        s32 temporary = s_Queue[i].temporary;

        /* Check client is still connected */
        s32 valid = 0;
        for (s32 j = 0; j <= NET_MAX_CLIENTS; j++) {
            if (&g_NetClients[j] == cl && cl->state >= CLSTATE_LOBBY) {
                valid = 1;
                break;
            }
        }

        s_Queue[i].active = 0;  /* consume immediately */

        if (valid) {
            streamComponentToClient(cl, net_hash, temporary);
        }

        break;  /* one transfer per tick */
    }
}

void netDistribSendKillFeed(const char *attacker, const char *victim,
                            const char *weapon, u8 flags)
{
    if (!s_Initialized || g_NetMode != NETMODE_SERVER) return;

    /* Broadcast to all clients in CLSTATE_LOBBY (spectating) */
    for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
        struct netclient *cl = &g_NetClients[i];
        if (cl->state != CLSTATE_LOBBY) continue;

        netbufStartWrite(&g_NetMsgRel);
        netmsgSvcLobbyKillFeedWrite(&g_NetMsgRel, attacker, victim, weapon, flags);
        netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
    }
}

/* ========================================================================
 * Client: PDCA Archive Extraction
 * ======================================================================== */

/**
 * Parse and extract a PDCA archive to a target directory.
 * Creates directories as needed.
 * Returns 1 on success, 0 on failure.
 */
static s32 extractArchive(const u8 *data, u32 data_len, const char *destdir)
{
    if (data_len < 6) return 0;

    u32 magic;
    memcpy(&magic, data, 4);
    if (magic != PDCA_MAGIC) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: bad archive magic 0x%08x", magic);
        return 0;
    }

    u16 file_count;
    memcpy(&file_count, data + 4, 2);

    const u8 *p = data + 6;
    const u8 *end = data + data_len;
    s32 extracted = 0;

    for (u16 i = 0; i < file_count; i++) {
        if (p + 2 > end) break;
        u16 path_len;
        memcpy(&path_len, p, 2);
        p += 2;

        if (p + path_len > end) break;
        const char *relpath = (const char *)p;
        p += path_len;

        if (p + 4 > end) break;
        u32 dlen;
        memcpy(&dlen, p, 4);
        p += 4;

        if (p + dlen > end) break;
        const u8 *fdata = p;
        p += dlen;

        /* Build full output path */
        char outpath[FS_MAXPATH];
        snprintf(outpath, sizeof(outpath), "%s/%s", destdir, relpath);

        /* Create parent directory */
        char dirpath[FS_MAXPATH];
        snprintf(dirpath, sizeof(dirpath), "%s", outpath);
        char *slash = strrchr(dirpath, '/');
        if (slash) {
            *slash = '\0';
            fsCreateDir(dirpath);
        }

        /* Write file */
        FILE *fp = fopen(outpath, "wb");
        if (!fp) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: can't write %s", outpath);
            continue;
        }
        fwrite(fdata, 1, dlen, fp);
        fclose(fp);
        extracted++;
    }

    sysLogPrintf(LOG_NOTE, "DISTRIB: extracted %d/%d files to %s", extracted, file_count, destdir);
    return (extracted > 0) ? 1 : 0;
}

/* ========================================================================
 * Client Public API
 * ======================================================================== */

void netDistribClientHandleCatalogInfo(const u32 *hashes,
                                       const char (*ids)[64],
                                       const char (*categories)[64],
                                       u16 count)
{
    if (!s_Initialized) return;

    /* Compute diff: which hashes we don't have */
    u32 missing[256];
    u16 missing_count = 0;

    for (u16 i = 0; i < count && missing_count < 256; i++) {
        const asset_entry_t *e = assetCatalogResolveByNetHash(hashes[i]);
        if (!e) {
            missing[missing_count++] = hashes[i];
            sysLogPrintf(LOG_NOTE, "DISTRIB: missing component '%s' (0x%08x)",
                         ids[i], hashes[i]);
        }
    }

    /* Update UI state */
    s_ClientStatus.missing_count = missing_count;
    s_ClientStatus.received_count = 0;
    s_ClientStatus.session_bytes_total = 0;

    if (missing_count == 0) {
        sysLogPrintf(LOG_NOTE, "DISTRIB: local catalog satisfies server requirements");
        s_ClientStatus.state = DISTRIB_CSTATE_IDLE;

        /* Still send an empty diff so server knows we're ready */
        netbufStartWrite(&g_NetMsgRel);
        netmsgClcCatalogDiffWrite(&g_NetMsgRel, NULL, 0, 0);
        netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        return;
    }

    s_ClientStatus.state = DISTRIB_CSTATE_DIFFING;
    sysLogPrintf(LOG_NOTE, "DISTRIB: requesting %u missing components", missing_count);

    /* Send CLC_CATALOG_DIFF */
    netbufStartWrite(&g_NetMsgRel);
    netmsgClcCatalogDiffWrite(&g_NetMsgRel, missing, missing_count, (u8)s_PendingTemporary);
    netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
}

void netDistribClientHandleBegin(u32 net_hash, const char *id,
                                  const char *category,
                                  u32 total_chunks, u32 archive_bytes,
                                  s32 temporary)
{
    if (!s_Initialized) return;

    /* Find a free receive slot */
    distrib_recv_slot_t *slot = NULL;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (!s_RecvSlots[i].active) {
            slot = &s_RecvSlots[i];
            break;
        }
    }

    if (!slot) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: no free recv slot for '%s'", id);
        return;
    }

    /* Allocate compressed buffer — size unknown yet, start at 64KB */
    u32 initial_cap = 65536;
    u8 *buf = (u8 *)malloc(initial_cap);
    if (!buf) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for recv buffer");
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->active = 1;
    slot->net_hash = net_hash;
    slot->temporary = temporary;
    slot->total_chunks = (u16)total_chunks;
    slot->chunks_received = 0;
    slot->archive_bytes = archive_bytes;
    slot->compressed_buf = buf;
    slot->compressed_cap = initial_cap;
    slot->compressed_len = 0;
    strncpy(slot->id, id, sizeof(slot->id) - 1);
    strncpy(slot->category, category, sizeof(slot->category) - 1);

    /* Update UI */
    s_ClientStatus.state = DISTRIB_CSTATE_RECEIVING;
    strncpy(s_ClientStatus.current_id, id, sizeof(s_ClientStatus.current_id) - 1);
    s_ClientStatus.current_bytes_total = archive_bytes;
    s_ClientStatus.current_bytes_received = 0;
    s_ClientStatus.temporary = temporary;

    sysLogPrintf(LOG_NOTE, "DISTRIB: recv begin '%s' (%u chunks, %u bytes)", id, total_chunks, archive_bytes);
}

void netDistribClientHandleChunk(u32 net_hash, u16 chunk_idx,
                                  u8 compression,
                                  const u8 *data, u16 data_len)
{
    if (!s_Initialized) return;

    /* Find slot */
    distrib_recv_slot_t *slot = NULL;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active && s_RecvSlots[i].net_hash == net_hash) {
            slot = &s_RecvSlots[i];
            break;
        }
    }

    if (!slot) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: chunk for unknown hash 0x%08x (idx %u)", net_hash, chunk_idx);
        return;
    }

    /* Grow buffer if needed */
    while (slot->compressed_len + data_len > slot->compressed_cap) {
        slot->compressed_cap *= 2;
        u8 *newbuf = (u8 *)realloc(slot->compressed_buf, slot->compressed_cap);
        if (!newbuf) {
            sysLogPrintf(LOG_ERROR, "DISTRIB: OOM growing recv buffer for '%s'", slot->id);
            return;
        }
        slot->compressed_buf = newbuf;
    }

    memcpy(slot->compressed_buf + slot->compressed_len, data, data_len);
    slot->compressed_len += data_len;
    slot->chunks_received++;

    /* Update UI */
    s_ClientStatus.current_bytes_received = slot->compressed_len;
    s_ClientStatus.session_bytes_total += data_len;

    (void)compression;  /* stored in slot for future use; we detect below from END */
    (void)chunk_idx;
}

void netDistribClientHandleEnd(u32 net_hash, u8 success)
{
    if (!s_Initialized) return;

    /* Find slot */
    distrib_recv_slot_t *slot = NULL;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active && s_RecvSlots[i].net_hash == net_hash) {
            slot = &s_RecvSlots[i];
            break;
        }
    }

    if (!slot) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: END for unknown hash 0x%08x", net_hash);
        return;
    }

    if (!success) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: server signalled failure for '%s'", slot->id);
        goto done;
    }

    if (!slot->compressed_buf || !slot->compressed_len) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: no data received for '%s'", slot->id);
        goto done;
    }

    /* Decompress */
    uLongf raw_len = (uLongf)(slot->archive_bytes + 1024); /* a bit of headroom */
    if (raw_len < 65536) raw_len = 65536;
    u8 *raw = (u8 *)malloc(raw_len);
    if (!raw) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for decompressed buffer (%lu bytes)", raw_len);
        goto done;
    }

    int zret = uncompress(raw, &raw_len,
                          slot->compressed_buf, (uLong)slot->compressed_len);
    if (zret != Z_OK) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: decompress failed (%d) for '%s'", zret, slot->id);
        free(raw);
        goto done;
    }

    /* Build destination directory */
    const char *modsdir = fsGetModDir();
    char destdir[FS_MAXPATH];
    if (slot->temporary) {
        snprintf(destdir, sizeof(destdir), "%s/%s/%s/%s",
                 modsdir, TEMP_SUBDIR, slot->category, slot->id);
    } else {
        snprintf(destdir, sizeof(destdir), "%s/%s/%s",
                 modsdir, slot->category, slot->id);
    }

    fsCreateDir(destdir);

    /* Extract */
    if (extractArchive(raw, (u32)raw_len, destdir)) {
        /* Hot-register in catalog */
        char inipath[FS_MAXPATH];
        const char *ini_names[] = { "map.ini", "character.ini", "bot.ini", "textures.ini",
                                    "skin.ini", "weapon.ini", NULL };
        ini_section_t ini;
        s32 registered = 0;

        for (s32 k = 0; ini_names[k] && !registered; k++) {
            snprintf(inipath, sizeof(inipath), "%s/%s", destdir, ini_names[k]);
            if (iniParse(inipath, &ini)) {
                asset_entry_t *e = assetCatalogRegister(slot->id, ASSET_NONE);
                if (e) {
                    strncpy(e->id, slot->id, sizeof(e->id) - 1);
                    strncpy(e->category, slot->category, sizeof(e->category) - 1);
                    strncpy(e->dirpath, destdir, sizeof(e->dirpath) - 1);
                    e->enabled = 1;
                    e->temporary = slot->temporary;
                    e->bundled = 0;
                    e->model_scale = iniGetFloat(&ini, "model_scale", 1.0f);
                    sysLogPrintf(LOG_NOTE, "DISTRIB: hot-registered '%s' from %s", slot->id, destdir);
                    registered = 1;
                }
            }
        }

        if (!registered) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: no recognized INI for '%s' -- catalog entry skipped", slot->id);
        }

        s_ClientStatus.received_count++;
    }

    free(raw);

done:
    free(slot->compressed_buf);
    memset(slot, 0, sizeof(*slot));

    /* Check if all transfers done */
    s32 any_active = 0;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active) { any_active = 1; break; }
    }

    if (!any_active) {
        s_ClientStatus.state = (s_ClientStatus.received_count > 0)
                               ? DISTRIB_CSTATE_DONE : DISTRIB_CSTATE_ERROR;
        sysLogPrintf(LOG_NOTE, "DISTRIB: all transfers complete (%d received)",
                     s_ClientStatus.received_count);
    }
}

void netDistribClientHandleKillFeed(const char *attacker, const char *victim,
                                    const char *weapon, u8 flags)
{
    if (!s_Initialized) return;

    killfeed_entry_t *kf = &s_KillFeed[s_KillFeedNext % KILLFEED_MAX_ENTRIES];
    s_KillFeedNext = (s_KillFeedNext + 1) % KILLFEED_MAX_ENTRIES;

    strncpy(kf->attacker, attacker, sizeof(kf->attacker) - 1);
    kf->attacker[sizeof(kf->attacker) - 1] = '\0';
    strncpy(kf->victim, victim, sizeof(kf->victim) - 1);
    kf->victim[sizeof(kf->victim) - 1] = '\0';
    strncpy(kf->weapon, weapon, sizeof(kf->weapon) - 1);
    kf->weapon[sizeof(kf->weapon) - 1] = '\0';
    kf->flags = flags;
    kf->timestamp = g_NetTick;
    kf->active = 1;
}

void netDistribClientGetStatus(distrib_client_status_t *out)
{
    if (!out) return;
    memcpy(out, &s_ClientStatus, sizeof(*out));
}

s32 netDistribClientGetKillFeed(killfeed_entry_t *out, s32 maxout)
{
    s32 count = 0;
    /* Walk ring buffer newest-first */
    for (s32 i = 0; i < KILLFEED_MAX_ENTRIES && count < maxout; i++) {
        s32 idx = ((s_KillFeedNext - 1 - i) + KILLFEED_MAX_ENTRIES) % KILLFEED_MAX_ENTRIES;
        if (s_KillFeed[idx].active) {
            out[count++] = s_KillFeed[idx];
        }
    }
    return count;
}

void netDistribClientSetTemporary(s32 temporary)
{
    s_PendingTemporary = temporary;
}

/* ========================================================================
 * Crash Recovery
 * ======================================================================== */

static void getCrashStatePath(char *out, s32 maxlen)
{
    const char *modsdir = fsGetModDir();
    snprintf(out, maxlen, "%s/%s/%s", modsdir, TEMP_SUBDIR, CRASH_STATE_FILE);
}

s32 netCrashRecoveryCheck(crash_recovery_state_t *out)
{
    if (!out) return CRASH_RECOVERY_NONE;
    memset(out, 0, sizeof(*out));

    /* Check if there are any temp components */
    const char *modsdir = fsGetModDir();
    char tempdir[FS_MAXPATH];
    snprintf(tempdir, sizeof(tempdir), "%s/%s", modsdir, TEMP_SUBDIR);

    DIR *d = opendir(tempdir);
    if (!d) {
        /* No .temp directory at all — nothing to recover */
        out->status = CRASH_RECOVERY_NONE;
        return CRASH_RECOVERY_NONE;
    }

    /* Count non-hidden entries (subdirectories = category dirs) */
    struct dirent *ent;
    s32 component_count = 0;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] != '.') {
            component_count++;
        }
    }
    closedir(d);

    if (!component_count) {
        out->status = CRASH_RECOVERY_NONE;
        return CRASH_RECOVERY_NONE;
    }

    out->temp_component_count = component_count;

    /* Read crash state file */
    char statepath[FS_MAXPATH];
    getCrashStatePath(statepath, sizeof(statepath));

    FILE *fp = fopen(statepath, "r");
    if (!fp) {
        /* No state file — temp mods exist but no crash data. Clean exit? */
        out->status = CRASH_RECOVERY_CLEAN;
        return CRASH_RECOVERY_CLEAN;
    }

    char line[256];
    s32 crash_count = 0;
    char suspect[64] = "";

    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "crash_count=", 12) == 0) {
            crash_count = atoi(line + 12);
        } else if (strncmp(line, "suspect=", 8) == 0) {
            strncpy(suspect, line + 8, sizeof(suspect) - 1);
            /* strip trailing newline */
            s32 slen = (s32)strlen(suspect);
            while (slen > 0 && (suspect[slen-1] == '\n' || suspect[slen-1] == '\r')) {
                suspect[--slen] = '\0';
            }
        }
    }
    fclose(fp);

    out->crash_count = crash_count;
    strncpy(out->suspect_id, suspect, sizeof(out->suspect_id) - 1);

    if (crash_count > 0) {
        out->status = CRASH_RECOVERY_PROMPT;
        sysLogPrintf(LOG_WARNING, "DISTRIB: crash recovery needed (count=%d, suspect='%s')",
                     crash_count, suspect);
        return CRASH_RECOVERY_PROMPT;
    }

    out->status = CRASH_RECOVERY_CLEAN;
    return CRASH_RECOVERY_CLEAN;
}

void netCrashRecoveryApply(s32 action)
{
    const char *modsdir = fsGetModDir();
    char tempdir[FS_MAXPATH];
    snprintf(tempdir, sizeof(tempdir), "%s/%s", modsdir, TEMP_SUBDIR);

    switch (action) {
        case 0: /* Keep — load normally, leave crash state as-is */
            sysLogPrintf(LOG_NOTE, "DISTRIB: crash recovery: keeping temp mods");
            break;

        case 1: /* Keep but Disable — mark all temp catalog entries disabled */
            sysLogPrintf(LOG_NOTE, "DISTRIB: crash recovery: disabling temp mods");
            /* Iterate catalog and disable temporary entries */
            for (s32 i = 0; i < assetCatalogGetCount(); i++) {
                /* We can't iterate by index directly — use the global resolve path.
                 * For now, log the action; full disable is done during catalog scan
                 * by checking for .disabled marker file. */
            }
            /* Write a .disabled marker in .temp/ */
            {
                char markerpath[FS_MAXPATH];
                snprintf(markerpath, sizeof(markerpath), "%s/.disabled", tempdir);
                FILE *fp = fopen(markerpath, "w");
                if (fp) {
                    fprintf(fp, "disabled_by_crash_recovery\n");
                    fclose(fp);
                }
            }
            break;

        case 2: /* Discard — delete .temp/ contents */
            sysLogPrintf(LOG_NOTE, "DISTRIB: crash recovery: discarding temp mods");
            {
                /* Remove all category subdirectories */
                DIR *d = opendir(tempdir);
                if (d) {
                    struct dirent *ent;
                    while ((ent = readdir(d)) != NULL) {
                        if (ent->d_name[0] == '.') continue;
                        char catdir[FS_MAXPATH];
                        snprintf(catdir, sizeof(catdir), "%s/%s", tempdir, ent->d_name);
                        /* Remove component subdirs */
                        DIR *catd = opendir(catdir);
                        if (catd) {
                            struct dirent *comp;
                            while ((comp = readdir(catd)) != NULL) {
                                if (comp->d_name[0] == '.') continue;
                                char compdir[FS_MAXPATH];
                                snprintf(compdir, sizeof(compdir), "%s/%s", catdir, comp->d_name);
                                /* Remove files within component dir */
                                DIR *compd = opendir(compdir);
                                if (compd) {
                                    struct dirent *f;
                                    while ((f = readdir(compd)) != NULL) {
                                        if (f->d_name[0] == '.') continue;
                                        char filepath[FS_MAXPATH];
                                        snprintf(filepath, sizeof(filepath), "%s/%s", compdir, f->d_name);
                                        remove(filepath);
                                    }
                                    closedir(compd);
                                }
                                rmdir(compdir);
                            }
                            closedir(catd);
                        }
                        rmdir(catdir);
                    }
                    closedir(d);
                }
            }
            break;

        default:
            break;
    }
}

void netCrashRecoveryMarkLaunching(void)
{
    /* Check if temp mods exist */
    const char *modsdir = fsGetModDir();
    char tempdir[FS_MAXPATH];
    snprintf(tempdir, sizeof(tempdir), "%s/%s", modsdir, TEMP_SUBDIR);

    DIR *d = opendir(tempdir);
    if (!d) return;
    s32 has_temp = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] != '.') { has_temp = 1; break; }
    }
    closedir(d);
    if (!has_temp) return;

    /* Write/increment crash state */
    char statepath[FS_MAXPATH];
    getCrashStatePath(statepath, sizeof(statepath));

    /* Read existing count */
    s32 crash_count = 0;
    char suspect[64] = "";
    FILE *fp = fopen(statepath, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "crash_count=", 12) == 0)
                crash_count = atoi(line + 12);
            else if (strncmp(line, "suspect=", 8) == 0) {
                strncpy(suspect, line + 8, sizeof(suspect) - 1);
                s32 slen = (s32)strlen(suspect);
                while (slen > 0 && (suspect[slen-1] == '\n' || suspect[slen-1] == '\r'))
                    suspect[--slen] = '\0';
            }
        }
        fclose(fp);
    }

    crash_count++;

    fp = fopen(statepath, "w");
    if (fp) {
        fprintf(fp, "[crash_recovery]\n");
        fprintf(fp, "crash_count=%d\n", crash_count);
        fprintf(fp, "suspect=%s\n", suspect);
        fclose(fp);
    }
}

void netCrashRecoveryMarkClean(void)
{
    char statepath[FS_MAXPATH];
    getCrashStatePath(statepath, sizeof(statepath));
    /* Delete crash state on clean exit */
    remove(statepath);
}
