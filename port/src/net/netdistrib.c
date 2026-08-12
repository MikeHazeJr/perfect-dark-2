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
 *   2. Client diffs, sends CLC_CATALOG_DIFF with missing catalog ID strings (v27: no net_hash).
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
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>

#include "types.h"
#include "constants.h"
#include "platform.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "net/netdistrib.h"
#include "net/netenet.h"
#include "net/netmanifest.h"
#include "assetcatalog.h"
#include "asset_path_contract.h"
#include "pdca_extract_transaction.h"
#include "asset_archive_policy.h"
#include "assetcatalog_body_head_slots.h"
#include "assetcatalog_weapon_slots.h"
#include "catalog_mgr_bodies.h"
#include "catalog_mgr_heads.h"
#include "assetcatalog_sound_slots.h" /* c3849 Wave 2 */
#include "assetcatalog_texture_slots.h" /* c3849 Wave 2 */
#include "assetcatalog_anim_slots.h" /* c3849 Wave 2 */
#include "assetcatalog_stage_slots.h" /* c3849 Wave 2 */
#include "game/stagetable.h" /* c3849 Wave 2 */
#include "assetcatalog_deps.h"
#include "assetcatalog_load.h"
#include "assetcatalog_scanner.h"
#include "loader_pool.h"
#include "modmgr.h"
#include "modarchive.h"
#include "pdgui_theme_loader.h"
#include "system.h"
#include "fs.h"
#include "config.h"
#include "sha256.h"

/* ========================================================================
 * Constants
 * ======================================================================== */

#define PDCA_MAGIC       0x41434450u   /* "PDCA" little-endian */
#define CRASH_STATE_FILE ".crash_state"
#define TEMP_SUBDIR      ".temp"

/* Initial pending transfer slots. The queue grows for large mod packs. */
#define DISTRIB_INITIAL_QUEUE 128

/* Default trust threshold in MB — transfers above this require user approval.
 * No hard size ceiling exists; the only protection is this user approval prompt.
 * Configurable via Net.DistribTrustThresholdMB in pd.ini (range 16–4096). */
#define DISTRIB_TRUST_THRESHOLD_DEFAULT_MB  256

/* Absolute hard cap on archive_bytes in a DISTRIB_BEGIN message.
 * Prevents a malicious server from causing an unbounded malloc at decompress time. */
#define MAX_DISTRIB_ARCHIVE_BYTES  (512u * 1024u * 1024u)  /* 512 MB */

/* ========================================================================
 * Server Transfer Queue
 * ======================================================================== */

typedef struct distrib_queue_entry {
    struct netclient *cl;        /* destination client */
    char catalog_id[64];         /* v27: component to send — catalog ID string */
    u8   kind;                   /* DISTRIB_QUEUE_* identity domain */
    s32  active;
    s32  temporary;              /* client requested session-only */
} distrib_queue_entry_t;

#define DISTRIB_QUEUE_ASSET    0
#define DISTRIB_QUEUE_PACKAGE  1
#define DISTRIB_PACKAGE_CATEGORY "pdmod"

static distrib_queue_entry_t *s_Queue = NULL;
static s32 s_QueueCap = 0;
static s32 s_Initialized = 0;

/* ========================================================================
 * Client Receive State
 * ======================================================================== */

#define RECV_SLOTS 4  /* max simultaneous incoming components (usually 1) */

typedef struct distrib_recv_slot {
    s32  active;
    /* v27: identified by id[] string — net_hash removed from wire. */
    char id[64];
    char category[64];
    u16  total_chunks;
    u16  chunks_received;
    u16  expected_chunk;      /* next chunk index we expect (for ordering validation) */
    u32  archive_bytes;       /* expected uncompressed archive size */
    u8  *compressed_buf;      /* accumulates compressed chunks */
    u32  compressed_cap;
    u32  compressed_len;
    u8   expected_sha256[SHA256_DIGEST_SIZE];  /* v46: digest of compressed archive */
    s32  temporary;
    /* Trust threshold approval (set when archive_bytes > s_TrustThresholdMb*1MB) */
    s32  needs_approval;
    char mod_name[64];        /* display name for the approval prompt */
    u32  archive_bytes_pending; /* size shown in the prompt */
} distrib_recv_slot_t;

static distrib_recv_slot_t s_RecvSlots[RECV_SLOTS];

/* Client-visible status */
static distrib_client_status_t s_ClientStatus;
/* Smoke ingress drives the real receive/extract/register transaction without
 * a remote peer. Only the final manifest protocol acknowledgement is skipped;
 * every filesystem/catalog/runtime mutation remains production code. */
static s32 s_SmokeReceiveActive;

/* Kill feed ring buffer */
static killfeed_entry_t s_KillFeed[KILLFEED_MAX_ENTRIES];
static s32 s_KillFeedNext = 0;  /* circular write head */

/* Pending diff decision (before user confirms) */
static s32 s_PendingTemporary = 1;

/* Configurable trust threshold (MB) — transfers above this need user approval.
 * Bound to Net.DistribTrustThresholdMB in pd.ini. */
static s32 s_TrustThresholdMb = DISTRIB_TRUST_THRESHOLD_DEFAULT_MB;

static s32 distribEnsureQueueCapacity(s32 min_cap)
{
    if (min_cap <= s_QueueCap) {
        return 1;
    }

    s32 old_cap = s_QueueCap;
    s32 new_cap = s_QueueCap ? s_QueueCap : DISTRIB_INITIAL_QUEUE;
    while (new_cap < min_cap) {
        new_cap *= 2;
    }

    distrib_queue_entry_t *new_queue =
        (distrib_queue_entry_t *)realloc(s_Queue,
            (size_t)new_cap * sizeof(*new_queue));
    if (!new_queue) {
        sysLogPrintf(LOG_ERROR,
                     "DISTRIB: failed to grow transfer queue to %d entries",
                     new_cap);
        return 0;
    }

    s_Queue = new_queue;
    memset(s_Queue + old_cap, 0,
           (size_t)(new_cap - old_cap) * sizeof(*s_Queue));
    s_QueueCap = new_cap;
    return 1;
}

/* Catalog IDs and categories are protocol identities, not filesystem names.
 * Encode every byte so distinct public identities remain distinct without
 * leaking Windows-reserved punctuation (notably the namespace ':') into a
 * receive destination. The original strings remain authoritative everywhere
 * game-facing; this segment is storage-only. */
static s32 distribStorageSegment(const char *identity, char *out, size_t out_n)
{
    static const char hex[] = "0123456789abcdef";
    if (!identity || !identity[0] || !out || out_n == 0) return 0;
    size_t len = strlen(identity);
    if (len > (out_n - 1) / 2) return 0;
    for (size_t i = 0; i < len; i++) {
        u8 value = (u8)identity[i];
        out[i * 2] = hex[value >> 4];
        out[i * 2 + 1] = hex[value & 0x0f];
    }
    out[len * 2] = '\0';
    return 1;
}

static distrib_queue_entry_t *distribAllocQueueSlot(void)
{
    if (!distribEnsureQueueCapacity(DISTRIB_INITIAL_QUEUE)) {
        return NULL;
    }

    for (s32 i = 0; i < s_QueueCap; i++) {
        if (!s_Queue[i].active) {
            return &s_Queue[i];
        }
    }

    s32 old_cap = s_QueueCap;
    if (!distribEnsureQueueCapacity(s_QueueCap + 1)) {
        return NULL;
    }
    return &s_Queue[old_cap];
}

/* ========================================================================
 * PDCA Archive Builder (server side)
 * ======================================================================== */

static s32 distribTypedArchiveRef(const asset_entry_t *entry,
		char *out, size_t out_cap)
{
	const char *separator;
	if (!entry || !out || out_cap == 0 || !entry->descriptor_path[0]) {
		return 0;
	}
	out[0] = '\0';
	separator = strrchr(entry->descriptor_path, ':');
	if (!separator || separator == entry->descriptor_path
			|| separator[-1] != ':') {
		return 0;
	}
	separator--;
	size_t len = (size_t)(separator - entry->descriptor_path);
	if (len == 0 || len >= out_cap) {
		return 0;
	}
	memcpy(out, entry->descriptor_path, len);
	out[len] = '\0';
	return assetArchivePathIsTyped(out);
}

static u8 *buildTypedArchiveComponent(const asset_entry_t *entry,
		const char *archive_ref, u32 *out_len)
{
	u32 archive_size = 0;
	u8 *archive_bytes = (u8 *)fsFileLoad(archive_ref, &archive_size);
	const char *leaf;
	const char *chain;
	u16 path_len;
	u32 total;
	u8 *buf;
	u8 *p;

	if (!archive_bytes || archive_size == 0) {
		if (archive_bytes) sysMemFree(archive_bytes);
		return NULL;
	}
	chain = strrchr(archive_ref, ':');
	leaf = (chain && chain > archive_ref && chain[-1] == ':') ? chain + 1
		: archive_ref;
	{
		const char *slash = strrchr(leaf, '/');
		const char *backslash = strrchr(leaf, '\\');
		if (backslash && (!slash || backslash > slash)) slash = backslash;
		if (slash) leaf = slash + 1;
	}
	path_len = (u16)(strlen(leaf) + 1);
	total = 6u + 2u + (u32)path_len + 4u + archive_size;
	buf = (u8 *)malloc(total);
	if (!buf) {
		sysMemFree(archive_bytes);
		return NULL;
	}
	p = buf;
	{
		u32 magic = PDCA_MAGIC;
		u16 count = 1;
		memcpy(p, &magic, 4); p += 4;
		memcpy(p, &count, 2); p += 2;
	}
	memcpy(p, &path_len, 2); p += 2;
	memcpy(p, leaf, path_len); p += path_len;
	memcpy(p, &archive_size, 4); p += 4;
	memcpy(p, archive_bytes, archive_size);
	sysMemFree(archive_bytes);
	*out_len = total;
	sysLogPrintf(LOG_NOTE,
		"DISTRIB: archive for '%s': exact typed source %s (%u bytes raw)",
		entry->id, archive_ref, total);
	return buf;
}

/**
 * Recursively enumerate files in a directory, appending entries to a
 * growing heap buffer. Returns the new buffer and updated size.
 * The prefix is the relative path from the component root.
 */
static u8 *buildArchiveDir(u8 *buf, u32 *buf_len, u32 *buf_cap,
                            const char *abspath, const char *relprefix,
                            s32 *ok)
{
    if (!ok || !*ok) return buf;
    DIR *d = opendir(abspath);
    if (!d) {
        return buf;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char childabs[FS_MAXPATH];
        char childrel[FS_MAXPATH];

        if (!assetPathJoinChecked(childabs, sizeof(childabs), abspath, "/",
                ent->d_name)) {
            *ok = 0;
            break;
        }
        if (relprefix[0]) {
            if (!assetPathJoinChecked(childrel, sizeof(childrel), relprefix,
                    "/", ent->d_name)) {
                *ok = 0;
                break;
            }
        } else {
            if (!assetPathCopyChecked(childrel, sizeof(childrel), ent->d_name)) {
                *ok = 0;
                break;
            }
        }

        struct stat st;
        if (stat(childabs, &st) != 0) {
            *ok = 0;
            break;
        }

        if (S_ISDIR(st.st_mode)) {
            buf = buildArchiveDir(buf, buf_len, buf_cap, childabs, childrel, ok);
            if (!*ok) break;
            continue;
        }

        if (!S_ISREG(st.st_mode)) {
            *ok = 0;
            break;
        }

        /* Read file data */
        FILE *fp = fopen(childabs, "rb");
        if (!fp) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: can't open %s", childabs);
            *ok = 0;
            break;
        }

        fseek(fp, 0, SEEK_END);
        u32 fsize = (u32)ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsize > NET_DISTRIB_MAX_COMP) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: skipping oversized file %s (%u bytes)", childabs, fsize);
            fclose(fp);
            *ok = 0;
            break;
        }

        /* Grow buffer: path_len(2) + path(n+1) + data_len(4) + data(fsize) */
        u16 path_len = (u16)(strlen(childrel) + 1);
        u32 entry_size = 2 + path_len + 4 + fsize;

        while (*buf_len + entry_size > *buf_cap) {
            u32 new_cap = (*buf_cap < 65536) ? 65536 : *buf_cap * 2;
            u8 *newbuf = (u8 *)realloc(buf, new_cap);
            if (!newbuf) {
                sysLogPrintf(LOG_ERROR, "DISTRIB: OOM building archive (cap=%u)", new_cap);
                fclose(fp);
                closedir(d);
                *ok = 0;
                return buf;
            }
            *buf_cap = new_cap;
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
            *ok = 0;
            break;
        }
        *buf_len += entry_size;

        fclose(fp);
    }

    closedir(d);
    return buf;
}

static u8 *distribAppendArchiveFile(u8 *buf, u32 *buf_len, u32 *buf_cap,
		const char *relative_path, const u8 *data, u32 data_len, s32 *ok)
{
	if (!ok || !*ok || !buf || !buf_len || !buf_cap || !relative_path
			|| !relative_path[0] || !data) {
		if (ok) *ok = 0;
		return buf;
	}
	size_t path_bytes = strlen(relative_path) + 1;
	if (path_bytes > 0xFFFFu) {
		*ok = 0;
		return buf;
	}
	u64 required = (u64)*buf_len + 2u + (u64)path_bytes + 4u + data_len;
	if (required > NET_DISTRIB_MAX_COMP || required > 0xFFFFFFFFull) {
		*ok = 0;
		return buf;
	}
	while ((u64)*buf_cap < required) {
		u32 new_cap = *buf_cap < 65536u ? 65536u : *buf_cap * 2u;
		if (new_cap <= *buf_cap || (u64)new_cap > NET_DISTRIB_MAX_COMP) {
			new_cap = (u32)required;
		}
		u8 *grown = (u8 *)realloc(buf, new_cap);
		if (!grown) {
			*ok = 0;
			return buf;
		}
		buf = grown;
		*buf_cap = new_cap;
	}

	u16 path_len = (u16)path_bytes;
	u8 *p = buf + *buf_len;
	memcpy(p, &path_len, 2); p += 2;
	memcpy(p, relative_path, path_len); p += path_len;
	memcpy(p, &data_len, 4); p += 4;
	memcpy(p, data, data_len);
	*buf_len = (u32)required;
	return buf;
}

static u8 *buildModPackageArchive(const modinfo_t *mod, u32 *out_len)
{
	if (!mod || !out_len || !mod->enabled || !mod->valid
			|| !mod->has_modjson) {
		return NULL;
	}
	u32 cap = 65536;
	u32 len = 6;
	u16 file_count = 0;
	s32 ok = 1;
	u8 *buf = (u8 *)calloc(1, cap);
	if (!buf) return NULL;
	{
		u32 magic = PDCA_MAGIC;
		memcpy(buf, &magic, 4);
	}

	if (mod->is_archive) {
		mod_archive_t *archive = modArchiveOpen(mod->archive_path);
		if (!archive) {
			free(buf);
			return NULL;
		}
		s32 count = modArchiveGetEntryCount(archive);
		for (s32 i = 0; i < count && ok; i++) {
			const char *name = modArchiveGetEntryName(archive, i);
			if (!name || !name[0]) {
				ok = 0;
				break;
			}
			size_t name_len = strlen(name);
			if (name[name_len - 1] == '/') continue;
			u32 size = 0;
			u8 *data = (u8 *)modArchiveExtractAlloc(archive, i, &size);
			if (!data) {
				ok = 0;
				break;
			}
			buf = distribAppendArchiveFile(buf, &len, &cap, name,
				data, size, &ok);
			free(data);
			if (ok) file_count++;
		}
		modArchiveClose(archive);
	} else {
		buf = buildArchiveDir(buf, &len, &cap, mod->dirpath, "", &ok);
		if (ok) {
			u8 *p = buf + 6;
			u8 *end = buf + len;
			while (p < end && ok) {
				u16 path_len;
				u32 data_len;
				if (p + 2 > end) { ok = 0; break; }
				memcpy(&path_len, p, 2); p += 2;
				if (!path_len || p + path_len > end) { ok = 0; break; }
				p += path_len;
				if (p + 4 > end) { ok = 0; break; }
				memcpy(&data_len, p, 4); p += 4;
				if (p + data_len > end) { ok = 0; break; }
				p += data_len;
				file_count++;
			}
		}
	}

	if (!ok || file_count == 0) {
		free(buf);
		return NULL;
	}
	memcpy(buf + 4, &file_count, 2);
	*out_len = len;
	sysLogPrintf(LOG_NOTE,
		"DISTRIB: package archive for '%s': %u files, %u bytes raw",
		mod->id, file_count, len);
	return buf;
}

/**
 * Build a PDCA archive from a component directory.
 * Returns heap-allocated buffer (caller must free), or NULL on error.
 * Sets *out_len to the archive size.
 */
static u8 *buildComponentArchive(const asset_entry_t *entry, u32 *out_len)
{
	char typed_archive[FS_MAXPATH * 2 + 4];
	if (distribTypedArchiveRef(entry, typed_archive,
			sizeof(typed_archive))) {
		return buildTypedArchiveComponent(entry, typed_archive, out_len);
	}

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
    s32 archive_ok = 1;
    buf = buildArchiveDir(buf, &buf_len, &buf_cap, entry->dirpath, "",
        &archive_ok);
    if (!archive_ok) {
        sysLogPrintf(LOG_WARNING,
            "DISTRIB.ASSET.PATH.REJECT: component traversal exceeds path capacity for '%s'",
            entry->id);
        free(buf);
        return NULL;
    }

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

static s32 distribSendPacketToPeer(ENetPeer *peer, const u8 *data, u32 len, s32 chan)
{
    if (!peer || !data || !len) return 0;
    ENetPacket *p = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    if (!p) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: enet_packet_create failed (%u bytes)", len);
        return 0;
    }
    if (enet_peer_send(peer, (u8)chan, p) < 0) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: enet_peer_send failed (%u bytes, chan %d)", len, chan);
        enet_packet_destroy(p);
		return 0;
    }
	return 1;
}

static void distribSendEnd(struct netclient *cl, const char *id, u8 success)
{
	if (!cl || !id || !id[0]) return;
	netbufStartWrite(&g_NetMsgRel);
	netmsgSvcDistribEndWrite(&g_NetMsgRel, id, success);
	netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
}

/* ========================================================================
 * Server: Build and Stream Component to Client
 * ======================================================================== */

static void streamComponentToClient(struct netclient *cl, const char *catalog_id,
		u8 kind, s32 temporary)
{
	const char *id = catalog_id;
	const char *category = NULL;
	u32 raw_len = 0;
	u8 *raw = NULL;
	if (kind == DISTRIB_QUEUE_PACKAGE) {
		modinfo_t *mod = modmgrFindMod(catalog_id);
		if (!mod || !mod->enabled || !mod->valid || !mod->has_modjson) {
			sysLogPrintf(LOG_WARNING,
				"DISTRIB: unknown or unavailable package_id '%s' requested",
				catalog_id);
			distribSendEnd(cl, catalog_id, 0);
			return;
		}
		category = DISTRIB_PACKAGE_CATEGORY;
		raw = buildModPackageArchive(mod, &raw_len);
		sysLogPrintf(LOG_NOTE,
			"DISTRIB: sending package '%s' to %s (temporary=%d)",
			id, cl->settings.name, temporary);
	} else {
		/* v27: typed assets resolve by catalog ID string — no net_hash. */
		const asset_entry_t *entry = assetCatalogResolve(catalog_id);
		if (!entry) {
			sysLogPrintf(LOG_WARNING,
				"DISTRIB: unknown catalog_id '%s' requested", catalog_id);
			distribSendEnd(cl, catalog_id, 0);
			return;
		}
		if (entry->bundled) {
			sysLogPrintf(LOG_WARNING,
				"DISTRIB: client requested bundled asset '%s' -- skipped",
				entry->id);
			distribSendEnd(cl, catalog_id, 0);
			return;
		}
		id = entry->id;
		category = entry->category;
		raw = buildComponentArchive(entry, &raw_len);
		sysLogPrintf(LOG_NOTE,
			"DISTRIB: sending '%s' to %s (temporary=%d)",
			id, cl->settings.name, temporary);
	}

    if (!raw || !raw_len) {
		sysLogPrintf(LOG_ERROR, "DISTRIB: failed to build archive for '%s'", id);
        if (raw) free(raw);
		distribSendEnd(cl, id, 0);
        return;
    }

    if (raw_len > NET_DISTRIB_MAX_COMP) {
		sysLogPrintf(LOG_WARNING,
			"DISTRIB: '%s' exceeds transfer limit (%u bytes) -- skipped",
			id, raw_len);
        free(raw);
		distribSendEnd(cl, id, 0);
        return;
    }

    /* Compress with zlib */
    uLongf compressed_cap = compressBound((uLong)raw_len);
    u8 *compressed = (u8 *)malloc(compressed_cap);
    if (!compressed) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for compressed buffer (%lu bytes)", compressed_cap);
        free(raw);
		distribSendEnd(cl, id, 0);
        return;
    }

    uLongf compressed_len = compressed_cap;
    int zret = compress2(compressed, &compressed_len, raw, (uLong)raw_len, Z_DEFAULT_COMPRESSION);
    free(raw);

    if (zret != Z_OK) {
		sysLogPrintf(LOG_ERROR, "DISTRIB: zlib compress failed (%d) for '%s'", zret, id);
        free(compressed);
		distribSendEnd(cl, id, 0);
        return;
    }

    /* Split into chunks */
    u32 chunk_size = NET_DISTRIB_CHUNK_SIZE;
    u32 total_chunks = (compressed_len + chunk_size - 1) / chunk_size;
    if (total_chunks > 65535) {
		sysLogPrintf(LOG_ERROR, "DISTRIB: too many chunks (%u) for '%s'", total_chunks, id);
        free(compressed);
		distribSendEnd(cl, id, 0);
        return;
    }

    sysLogPrintf(LOG_NOTE, "DISTRIB: '%s' compressed %lu→%lu bytes, %u chunks",
			 id, (unsigned long)raw_len, (unsigned long)compressed_len, total_chunks);

    /* v46 / SEC-5: digest the exact compressed archive bytes that cross the
     * wire. The client rejects BEGIN packets with a zero digest and verifies
     * this before decompression/extraction at END. */
    u8 compressed_sha256[SHA256_DIGEST_SIZE];
    sha256Hash(compressed, (size_t)compressed_len, compressed_sha256);

    /* SVC_DISTRIB_BEGIN */
    netbufStartWrite(&g_NetMsgRel);
	netmsgSvcDistribBeginWrite(&g_NetMsgRel, id, category,
                               total_chunks, raw_len, compressed_sha256);
	if (g_NetMsgRel.error) {
		free(compressed);
		distribSendEnd(cl, id, 0);
		return;
	}
    netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);

    /* SVC_DISTRIB_CHUNK × total_chunks on NETCHAN_TRANSFER.
     * v27: packet format: msgid(1) + id_str(2+idlen) + chunk_idx(2) + compression(1)
     *                   + data_len(2) + data(this_len).
     * String format mirrors netbufWriteStr: u16 length (incl. null) + bytes. */
	u16 id_wire_len = (u16)(strlen(id) + 1);   /* include null terminator */
	s32 sent_ok = 1;
    for (u32 i = 0; i < total_chunks; i++) {
        u32 offset = i * chunk_size;
        u16 this_len = (u16)((offset + chunk_size <= compressed_len)
                             ? chunk_size
                             : compressed_len - offset);

        u32 pkt_len = 1 + sizeof(u16) + id_wire_len + 2 + 1 + 2 + this_len;
        u8 *pkt = (u8 *)malloc(pkt_len);
        if (!pkt) {
            sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for chunk packet");
			sent_ok = 0;
            break;
        }
        u8 *p = pkt;
        *p++ = SVC_DISTRIB_CHUNK;
        memcpy(p, &id_wire_len, 2);             p += 2;
		memcpy(p, id, id_wire_len);             p += id_wire_len;
        u16 cidx = (u16)i;
        memcpy(p, &cidx, 2);                    p += 2;
        *p++ = NET_DISTRIB_COMP_DEFLATE;
        memcpy(p, &this_len, 2);                p += 2;
        memcpy(p, compressed + offset, this_len);

		if (!distribSendPacketToPeer(cl->peer, pkt, pkt_len,
				NETCHAN_TRANSFER)) {
			sent_ok = 0;
		}
        free(pkt);
		if (!sent_ok) break;
    }

    free(compressed);

	distribSendEnd(cl, id, sent_ok ? 1 : 0);
}

/* ========================================================================
 * Server Public API
 * ======================================================================== */

void netDistribInit(void)
{
    if (distribEnsureQueueCapacity(DISTRIB_INITIAL_QUEUE)) {
        memset(s_Queue, 0, (size_t)s_QueueCap * sizeof(*s_Queue));
    }
    memset(s_RecvSlots, 0, sizeof(s_RecvSlots));
    memset(&s_ClientStatus, 0, sizeof(s_ClientStatus));
    memset(s_KillFeed, 0, sizeof(s_KillFeed));
    s_KillFeedNext = 0;
    /* D3R-9's default is session-only receive under mods/.temp. The user can
     * still opt into a permanent catalog download before requesting it. */
    s_PendingTemporary = 1;
    s_TrustThresholdMb = DISTRIB_TRUST_THRESHOLD_DEFAULT_MB;
    if (!s_Initialized) {
        configRegisterInt("Net.DistribTrustThresholdMB", &s_TrustThresholdMb, 16, 4096);
    }
    s_Initialized = 1;
}

void netDistribServerSendCatalogInfo(struct netclient *cl)
{
    if (!s_Initialized || !cl) return;

    u16 offset = 0;
    u16 total = 0;
    s32 batches = 0;
    do {
        u16 next = offset;
        netbufStartWrite(&g_NetMsgRel);
        netmsgSvcCatalogInfoWriteChunk(&g_NetMsgRel, offset, &next, &total);
        if (g_NetMsgRel.error || (total > offset && next == offset)) {
            sysLogPrintf(LOG_WARNING,
                         "DISTRIB: failed to write SVC_CATALOG_INFO batch "
                         "offset=%u total=%u for %s",
                         (unsigned)offset, (unsigned)total, cl->settings.name);
            break;
        }
        netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        batches++;
        offset = next;
    } while (offset < total);

    sysLogPrintf(LOG_NOTE,
                 "DISTRIB: sent SVC_CATALOG_INFO to %s (%u entries, %d batches)",
                 cl->settings.name, (unsigned)total, batches);
}

void netDistribServerHandleDiff(struct netclient *cl,
                                const char (*missing_ids)[64],
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
        distrib_queue_entry_t *slot = distribAllocQueueSlot();
        if (!slot) {
            sysLogPrintf(LOG_WARNING,
                         "DISTRIB: transfer queue allocation failed for '%s'",
                         missing_ids[i]);
			distribSendEnd(cl, missing_ids[i], 0);
            continue;
        }
        slot->cl = cl;
        strncpy(slot->catalog_id, missing_ids[i], sizeof(slot->catalog_id) - 1);
        slot->catalog_id[sizeof(slot->catalog_id) - 1] = '\0';
        slot->temporary = (s32)temporary;
		slot->kind = DISTRIB_QUEUE_ASSET;
        slot->active = 1;
    }
}

void netDistribServerHandleManifestDiff(struct netclient *cl,
		const char (*missing_ids)[64], u16 count, u8 temporary)
{
	if (!s_Initialized || !cl || !count) {
		return;
	}
	sysLogPrintf(LOG_NOTE,
		"DISTRIB: client %s missing %u manifest entries (temporary=%d)",
		cl->settings.name, count, (s32)temporary);

	for (u16 i = 0; i < count; i++) {
		u8 kind = DISTRIB_QUEUE_ASSET;
		s32 found = 0;
		for (u16 j = 0; j < g_ServerManifest.num_entries; j++) {
			const match_manifest_entry_t *entry = &g_ServerManifest.entries[j];
			if (strcmp(entry->id, missing_ids[i]) == 0) {
				kind = entry->type == MANIFEST_TYPE_COMPONENT
					? DISTRIB_QUEUE_PACKAGE : DISTRIB_QUEUE_ASSET;
				found = 1;
				break;
			}
		}
		if (!found) {
			sysLogPrintf(LOG_WARNING,
				"DISTRIB: manifest missing id '%s' is not in the active server manifest",
				missing_ids[i]);
			distribSendEnd(cl, missing_ids[i], 0);
			continue;
		}

		distrib_queue_entry_t *slot = distribAllocQueueSlot();
		if (!slot) {
			sysLogPrintf(LOG_WARNING,
				"DISTRIB: transfer queue allocation failed for manifest id '%s'",
				missing_ids[i]);
			distribSendEnd(cl, missing_ids[i], 0);
			continue;
		}
		slot->cl = cl;
		strncpy(slot->catalog_id, missing_ids[i],
			sizeof(slot->catalog_id) - 1);
		slot->catalog_id[sizeof(slot->catalog_id) - 1] = '\0';
		slot->temporary = (s32)temporary;
		slot->kind = kind;
		slot->active = 1;
		sysLogPrintf(LOG_NOTE,
			"DISTRIB: queued manifest id '%s' kind=%s",
			slot->catalog_id,
			kind == DISTRIB_QUEUE_PACKAGE ? "package" : "asset");
	}
}

void netDistribServerTick(void)
{
    if (!s_Initialized || g_NetMode != NETMODE_SERVER) return;

    /* Process one pending entry per tick to avoid stalling the frame */
    for (s32 i = 0; i < s_QueueCap; i++) {
        if (!s_Queue[i].active) continue;

        struct netclient *cl = s_Queue[i].cl;
        char catalog_id[64];
        strncpy(catalog_id, s_Queue[i].catalog_id, sizeof(catalog_id) - 1);
        catalog_id[sizeof(catalog_id) - 1] = '\0';
        s32 temporary = s_Queue[i].temporary;
		u8 kind = s_Queue[i].kind;

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
			streamComponentToClient(cl, catalog_id, kind, temporary);
        }

        break;  /* one transfer per tick */
    }
}

void netDistribServerGetClientStatus(s32 client_index,
                                     distrib_server_client_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (!s_Initialized || client_index < 0 || client_index > NET_MAX_CLIENTS) return;

    const struct netclient *target = &g_NetClients[client_index];
    for (s32 i = 0; i < s_QueueCap; i++) {
        if (s_Queue[i].active && s_Queue[i].cl == target) {
            out->queue_remaining++;
            if (!out->current_id[0]) {
                strncpy(out->current_id, s_Queue[i].catalog_id,
                        sizeof(out->current_id) - 1);
            }
        }
    }
    out->queue_total = out->queue_remaining;
}

void netDistribServerRebroadcastCatalog(void)
{
    if (!s_Initialized || g_NetMode != NETMODE_SERVER) return;

    for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
        struct netclient *cl = &g_NetClients[i];
        if (cl->state == CLSTATE_LOBBY) {
            netDistribServerSendCatalogInfo(cl);
        }
    }
    sysLogPrintf(LOG_NOTE, "DISTRIB: re-broadcast SVC_CATALOG_INFO to all lobby clients");
}

void netDistribSendKillFeed(const char *attacker, const char *victim,
                            const char *weapon, u8 flags)
{
    if (!s_Initialized || g_NetMode != NETMODE_SERVER) return;

    /* Broadcast to all active clients: spectators (CLSTATE_LOBBY) and
     * in-game players (CLSTATE_GAME) so the HUD killfeed shows on all clients. */
    for (s32 i = 0; i < NET_MAX_CLIENTS; i++) {
        struct netclient *cl = &g_NetClients[i];
        if (cl->state != CLSTATE_LOBBY && cl->state != CLSTATE_GAME) continue;

        netbufStartWrite(&g_NetMsgRel);
        netmsgSvcLobbyKillFeedWrite(&g_NetMsgRel, attacker, victim, weapon, flags);
        netSend(cl, &g_NetMsgRel, 1, NETCHAN_CONTROL);
    }
}

/* ========================================================================
 * Client: PDCA Archive Extraction
 * ======================================================================== */

/* ========================================================================
 * Client Public API
 * ======================================================================== */

void netDistribClientHandleCatalogInfo(const char (*ids)[64],
                                       const char (*categories)[64],
                                       u16 count,
                                       u16 batch_offset,
                                       u16 total_count)
{
    if (!s_Initialized) return;
    (void)categories;

    if (batch_offset == 0) {
        s_ClientStatus.missing_count = 0;
        s_ClientStatus.received_count = 0;
        s_ClientStatus.session_bytes_total = 0;
        s_ClientStatus.current_id[0] = '\0';
        s_ClientStatus.current_bytes_received = 0;
        s_ClientStatus.current_bytes_total = 0;
    }

    /* v27: diff by catalog ID string — no net_hash lookup. */
    char (*missing_ids)[64] = NULL;
    u16 missing_count = 0;
    if (count > 0) {
        missing_ids = (char (*)[64])calloc(count, sizeof(*missing_ids));
        if (!missing_ids) {
            s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
            sysLogPrintf(LOG_WARNING,
                         "DISTRIB: OOM while diffing %u catalog entries",
                         count);
            return;
        }
    }

    for (u16 i = 0; i < count; i++) {
        const asset_entry_t *e = assetCatalogResolve(ids[i]);
        if (!e) {
            strncpy(missing_ids[missing_count], ids[i], 63);
            missing_ids[missing_count][63] = '\0';
            missing_count++;
            sysLogPrintf(LOG_NOTE, "DISTRIB: missing component '%s'", ids[i]);
        }
    }

    s_ClientStatus.missing_count += missing_count;

    const s32 final_batch = ((u32)batch_offset + (u32)count >= (u32)total_count);
    if (final_batch && s_ClientStatus.missing_count == 0) {
        sysLogPrintf(LOG_NOTE, "DISTRIB: local catalog satisfies server requirements");
        s_ClientStatus.state = DISTRIB_CSTATE_IDLE;

        /* Still send an empty diff so server knows we're ready */
        netbufStartWrite(&g_NetMsgRel);
        netmsgClcCatalogDiffWrite(&g_NetMsgRel, NULL, 0, 0);
        netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        free(missing_ids);
        return;
    }

    if (missing_count > 0) {
        s_ClientStatus.state = DISTRIB_CSTATE_DIFFING;
        sysLogPrintf(LOG_NOTE,
                     "DISTRIB: requesting %u missing components from catalog batch "
                     "%u..%u of %u (total missing so far %d)",
                     missing_count, (unsigned)batch_offset,
                     (unsigned)(batch_offset + count),
                     (unsigned)total_count, s_ClientStatus.missing_count);

        /* Send CLC_CATALOG_DIFF for this batch. */
        netbufStartWrite(&g_NetMsgRel);
        netmsgClcCatalogDiffWrite(&g_NetMsgRel, (const char (*)[64])missing_ids,
                                  missing_count, (u8)s_PendingTemporary);
        netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
    } else if (final_batch) {
        sysLogPrintf(LOG_NOTE,
                     "DISTRIB: catalog diff complete, total missing=%d",
                     s_ClientStatus.missing_count);
    }

    free(missing_ids);
}

void netDistribClientBeginManifestTransferSet(u16 missing_count)
{
	if (!s_Initialized || missing_count == 0) return;
	s_PendingTemporary = 1;
	s_ClientStatus.missing_count = (s32)missing_count;
	s_ClientStatus.received_count = 0;
	s_ClientStatus.current_id[0] = '\0';
	s_ClientStatus.current_bytes_received = 0;
	s_ClientStatus.current_bytes_total = 0;
	s_ClientStatus.temporary = 1;
	s_ClientStatus.state = DISTRIB_CSTATE_DIFFING;
	sysLogPrintf(LOG_NOTE,
		"DISTRIB: manifest transfer set armed (%u entries)",
		(unsigned)missing_count);
}

s32 netDistribClientGetTransferTemporary(void)
{
    return s_PendingTemporary ? 1 : 0;
}

void netDistribClientHandleBegin(const char *catalog_id, const char *category,
                                  u32 total_chunks, u32 archive_bytes,
                                  const u8 expected_sha256[SHA256_DIGEST_SIZE],
                                  s32 temporary)
{
    if (!s_Initialized) return;

    if (!catalog_id || !catalog_id[0] || !category) {
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: rejecting BEGIN -- missing catalog/category identity");
        return;
    }

    /* M-4: Validate archive_bytes before storing — reject oversized transfers. */
    if (archive_bytes == 0 || archive_bytes > MAX_DISTRIB_ARCHIVE_BYTES
            || total_chunks == 0 || total_chunks > 65535u) {
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: rejecting BEGIN '%s' — invalid archive_bytes=%u "
                     "(max=%u) total_chunks=%u",
                     catalog_id, archive_bytes, MAX_DISTRIB_ARCHIVE_BYTES,
                     total_chunks);
        return;
    }

    {
        static const u8 s_zero32[SHA256_DIGEST_SIZE] = {0};
        if (!expected_sha256
                || memcmp(expected_sha256, s_zero32, SHA256_DIGEST_SIZE) == 0) {
            sysLogPrintf(LOG_ERROR,
                         "DISTRIB: rejecting BEGIN '%s' — missing SHA-256 digest",
                         catalog_id);
            return;
        }
    }

    /* SEC-25: enforce the aggregate per-session cap across all components.
     * session_bytes_total is only incremented on chunk receipt, so comparing
     * against the *incoming* archive_bytes here is the right moment: a hostile
     * server that stacks many medium-size components can no longer exceed the
     * declared NET_DISTRIB_MAX_SESSION budget. */
    {
        u64 projected = (u64)s_ClientStatus.session_bytes_total + (u64)archive_bytes;
        if (projected > (u64)NET_DISTRIB_MAX_SESSION) {
            sysLogPrintf(LOG_ERROR,
                         "DISTRIB: rejecting BEGIN '%s' — session cap exceeded "
                         "(have=%u, incoming=%u, max=%u)",
                         catalog_id, s_ClientStatus.session_bytes_total,
                         archive_bytes, (u32)NET_DISTRIB_MAX_SESSION);
            return;
        }
    }

    /* Find a free receive slot */
    distrib_recv_slot_t *slot = NULL;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (!s_RecvSlots[i].active) {
            slot = &s_RecvSlots[i];
            break;
        }
    }

    if (!slot) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: no free recv slot for '%s'", catalog_id);
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
    /* v27: identified by id[] string — no net_hash. */
    slot->temporary = temporary;
    slot->total_chunks = (u16)total_chunks;
    slot->chunks_received = 0;
    slot->expected_chunk = 0;
    slot->archive_bytes = archive_bytes;
    slot->compressed_buf = buf;
    slot->compressed_cap = initial_cap;
    slot->compressed_len = 0;
    memcpy(slot->expected_sha256, expected_sha256, sizeof(slot->expected_sha256));
    strncpy(slot->id, catalog_id, sizeof(slot->id) - 1);
    strncpy(slot->category, category, sizeof(slot->category) - 1);

    /* Trust threshold check: if size exceeds threshold, set approval flag instead
     * of proceeding silently. The transfer is still staged so chunks can be buffered
     * after the user approves — the UI should call netDistribApproveTransfer(). */
    u32 threshold_bytes = (u32)s_TrustThresholdMb * 1024u * 1024u;
    if (archive_bytes > threshold_bytes) {
        slot->needs_approval = 1;
        strncpy(slot->mod_name, catalog_id, sizeof(slot->mod_name) - 1);
        slot->archive_bytes_pending = archive_bytes;
        sysLogPrintf(LOG_WARNING, "DISTRIB: '%s' (%u bytes) exceeds trust threshold (%u MB) — awaiting user approval",
                     catalog_id, archive_bytes, (u32)s_TrustThresholdMb);
        /* Don't update the "receiving" UI state yet — wait for approval. */
        return;
    }

    /* Update UI */
    s_ClientStatus.state = DISTRIB_CSTATE_RECEIVING;
    strncpy(s_ClientStatus.current_id, catalog_id, sizeof(s_ClientStatus.current_id) - 1);
    s_ClientStatus.current_bytes_total = archive_bytes;
    s_ClientStatus.current_bytes_received = 0;
    s_ClientStatus.temporary = temporary;

	sysLogPrintf(LOG_NOTE,
		"DISTRIB: recv begin '%s' category=%s (%u chunks, %u bytes)",
		catalog_id, category, total_chunks, archive_bytes);
}

void netDistribClientHandleChunk(const char *catalog_id, u16 chunk_idx,
                                  u8 compression,
                                  const u8 *data, u16 data_len)
{
    if (!s_Initialized) return;

    /* v27: find slot by catalog ID string. */
    distrib_recv_slot_t *slot = NULL;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active && strncmp(s_RecvSlots[i].id, catalog_id, sizeof(s_RecvSlots[i].id)) == 0) {
            slot = &s_RecvSlots[i];
            break;
        }
    }

    if (!slot) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: chunk for unknown catalog_id '%s' (idx %u)", catalog_id, chunk_idx);
        return;
    }

    /* M-3: Validate chunk ordering — ENet reliable channels deliver in order,
     * so out-of-sequence chunks indicate protocol tampering or a serious bug. */
    if (chunk_idx != slot->expected_chunk) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: out-of-order chunk for '%s': expected %u got %u — dropping",
                     catalog_id, slot->expected_chunk, chunk_idx);
        return;
    }
    slot->expected_chunk++;

    /* Grow buffer if needed */
    while (slot->compressed_len + data_len > slot->compressed_cap) {
        /* C-5: Prevent integer overflow when doubling compressed_cap. */
        if (slot->compressed_cap > 256u * 1024u * 1024u / 2) {
            sysLogPrintf(LOG_ERROR, "DISTRIB: compressed buffer exceeds 128MB cap for '%s'", slot->id);
            free(slot->compressed_buf);
            memset(slot, 0, sizeof(*slot));
            return;
        }
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
}

static void netDistribClientDeclineActiveManifest(const char *reason)
{
    if (g_NetMode != NETMODE_CLIENT || !g_NetLocalClient) {
        return;
    }

    if (g_NetLocalClient->state != CLSTATE_PREPARING) {
        return;
    }

    if (g_ClientManifest.num_entries == 0) {
        return;
    }

    sysLogPrintf(LOG_WARNING,
                 "DISTRIB: declining active match manifest after transfer failure (%s)",
                 reason ? reason : "unknown");

    netbufStartWrite(&g_NetMsgRel);
    netmsgClcManifestStatusWrite(&g_NetMsgRel, g_ClientManifest.manifest_hash,
                                 MANIFEST_STATUS_DECLINE, NULL, 0);
    if (!g_NetMsgRel.error) {
        netSend(NULL, &g_NetMsgRel, true, NETCHAN_CONTROL);
        g_NetLocalClient->state = CLSTATE_LOBBY;
    } else {
        netbufStartWrite(&g_NetMsgRel);
    }
}

/* H-2: Map an INI filename ("map.ini", "character.ini", ...) to the asset_type_e
 * that the scanner would register locally. Keeps wire-delivered mods type-resolvable
 * on arrival so the typed catalog resolvers work before the next refresh tick. */
static asset_type_e iniFilenameToAssetType(const char *ini_name)
{
    if (!ini_name) return ASSET_NONE;
    if (strcmp(ini_name, "map.ini") == 0)       return ASSET_MAP;
    if (strcmp(ini_name, "character.ini") == 0) return ASSET_CHARACTER;
    if (strcmp(ini_name, "bot.ini") == 0)       return ASSET_BOT_VARIANT;
    if (strcmp(ini_name, "prop.ini") == 0)      return ASSET_PROP;
    if (strcmp(ini_name, "textures.ini") == 0)  return ASSET_TEXTURES;
    if (strcmp(ini_name, "texture.ini") == 0)   return ASSET_TEXTURE;
    if (strcmp(ini_name, "material.ini") == 0)  return ASSET_MATERIAL;
    if (strcmp(ini_name, "mesh.ini") == 0)      return ASSET_MODEL;
    if (strcmp(ini_name, "model.ini") == 0)     return ASSET_MODEL;
    if (strcmp(ini_name, "skin.ini") == 0)      return ASSET_SKIN;
    if (strcmp(ini_name, "weapon.ini") == 0)    return ASSET_WEAPON;
    if (strcmp(ini_name, "projectile.ini") == 0) return ASSET_PROJECTILE;
    if (strcmp(ini_name, "entity.ini") == 0)    return ASSET_ENTITY;
    if (strcmp(ini_name, "head.ini") == 0)      return ASSET_HEAD;
    if (strcmp(ini_name, "body.ini") == 0)      return ASSET_BODY;
    if (strcmp(ini_name, "arena.ini") == 0)     return ASSET_ARENA;
    if (strcmp(ini_name, "scenario.ini") == 0)  return ASSET_SCENARIO;
    if (strcmp(ini_name, "animation.ini") == 0) return ASSET_ANIMATION;
    if (strcmp(ini_name, "effect.ini") == 0)    return ASSET_EFFECT;
    if (strcmp(ini_name, "vehicle.ini") == 0)   return ASSET_VEHICLE;
    if (strcmp(ini_name, "mission.ini") == 0)   return ASSET_MISSION;
    if (strcmp(ini_name, "gamemode.ini") == 0)  return ASSET_GAMEMODE;
    if (strcmp(ini_name, "botprofile.ini") == 0) return ASSET_BOT_PROFILE;
    if (strcmp(ini_name, "audio.ini") == 0)     return ASSET_AUDIO;
    if (strcmp(ini_name, "sound.ini") == 0)     return ASSET_AUDIO;
    if (strcmp(ini_name, "sfx.ini") == 0)       return ASSET_AUDIO;
    if (strcmp(ini_name, "voice.ini") == 0)     return ASSET_AUDIO;
    if (strcmp(ini_name, "music.ini") == 0)     return ASSET_AUDIO;
    if (strcmp(ini_name, "hud.ini") == 0)       return ASSET_HUD;
    if (strcmp(ini_name, "ui.ini") == 0)        return ASSET_UI;
    if (strcmp(ini_name, "font.ini") == 0)      return ASSET_FONT;
    if (strcmp(ini_name, "lang.ini") == 0)      return ASSET_LANG;
    if (strcmp(ini_name, "theme.ini") == 0)     return ASSET_THEME;
    return ASSET_NONE;
}

static s32 distribParseAudioCategoryValue(const char *value, s32 default_category)
{
    if (!value || !value[0]) {
        return default_category;
    }

    if (((u8)value[0] >= '0' && (u8)value[0] <= '9') || value[0] == '-' || value[0] == '+') {
        s32 n = (s32)strtol(value, NULL, 10);
        if (n >= AUDIO_CAT_SFX && n <= AUDIO_CAT_VOICE) {
            return n;
        }
        return default_category;
    }

    char lower[32];
    s32 i = 0;
    while (value[i] && i < (s32)sizeof(lower) - 1) {
        lower[i] = (char)tolower((u8)value[i]);
        i++;
    }
    lower[i] = '\0';

    if (strcmp(lower, "music") == 0 || strcmp(lower, "track") == 0) {
        return AUDIO_CAT_MUSIC;
    }
    if (strcmp(lower, "voice") == 0 || strcmp(lower, "dialog") == 0 ||
            strcmp(lower, "dialogue") == 0) {
        return AUDIO_CAT_VOICE;
    }
    if (strcmp(lower, "sfx") == 0 || strcmp(lower, "sound") == 0 ||
            strcmp(lower, "soundfx") == 0) {
        return AUDIO_CAT_SFX;
    }

    return default_category;
}

static s32 distribAudioCategoryForSection(const ini_section_t *ini)
{
    if (ini && strcmp(ini->type, "voice") == 0) return AUDIO_CAT_VOICE;
    if (ini && strcmp(ini->type, "music") == 0) return AUDIO_CAT_MUSIC;
    return AUDIO_CAT_SFX;
}

static void distribLowerKey(const char *value, char *out, size_t out_n)
{
    if (!out || out_n == 0) {
        return;
    }
    out[0] = '\0';
    if (!value) {
        return;
    }

    size_t i = 0;
    while (value[i] && i + 1 < out_n) {
        char c = (char)tolower((u8)value[i]);
        if (c == '-' || c == ' ') {
            c = '_';
        }
        out[i] = c;
        i++;
    }
    out[i] = '\0';
}

static s32 distribParseNamedIntValue(const char *value, s32 default_value,
                                     const char *const *keys,
                                     const s32 *values, s32 count)
{
    if (!value || !value[0]) {
        return default_value;
    }
    if (((u8)value[0] >= '0' && (u8)value[0] <= '9')
            || value[0] == '-' || value[0] == '+') {
        return (s32)strtol(value, NULL, 10);
    }

    char lower[64];
    distribLowerKey(value, lower, sizeof(lower));
    for (s32 i = 0; i < count; i++) {
        if (strcmp(lower, keys[i]) == 0) {
            return values[i];
        }
    }
    return default_value;
}

static s32 distribParseModeKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "combat",
        "hold_the_briefcase",
        "hacker_central",
        "pop_a_cap",
        "king_of_the_hill",
        "capture_the_case",
    };
    static const s32 values[] = { 0, 1, 2, 3, 4, 5 };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 distribParseBotTypeKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "general", "peace", "shield", "rocket", "kaze", "fist",
        "prey", "coward", "judge", "feud", "speed", "turtle", "venge",
    };
    static const s32 values[] = {
        BOTTYPE_GENERAL, BOTTYPE_PEACE, BOTTYPE_SHIELD, BOTTYPE_ROCKET,
        BOTTYPE_KAZE, BOTTYPE_FIST, BOTTYPE_PREY, BOTTYPE_COWARD,
        BOTTYPE_JUDGE, BOTTYPE_FEUD, BOTTYPE_SPEED, BOTTYPE_TURTLE,
        BOTTYPE_VENGE,
    };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 distribParseBotDifficultyKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "meat", "easy", "normal", "hard", "perfect", "dark",
    };
    static const s32 values[] = {
        BOTDIFF_MEAT, BOTDIFF_EASY, BOTDIFF_NORMAL, BOTDIFF_HARD,
        BOTDIFF_PERFECT, BOTDIFF_DARK,
    };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 distribParseHudElementKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "crosshair", "ammo", "radar", "health", "timer", "score",
    };
    static const s32 values[] = {
        HUD_ELEM_CROSSHAIR, HUD_ELEM_AMMO, HUD_ELEM_RADAR,
        HUD_ELEM_HEALTH, HUD_ELEM_TIMER, HUD_ELEM_SCORE,
    };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 distribParsePropKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "object", "door", "character", "weapon_pickup",
        "eyespy", "player", "explosion", "smoke",
    };
    static const s32 values[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 distribParseEffectTypeKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "tint", "glow", "shimmer", "darken", "screen", "particle",
    };
    static const s32 values[] = {
        EFFECT_TYPE_TINT, EFFECT_TYPE_GLOW, EFFECT_TYPE_SHIMMER,
        EFFECT_TYPE_DARKEN, EFFECT_TYPE_SCREEN, EFFECT_TYPE_PARTICLE,
    };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static s32 distribParseEffectTargetKeyValue(const char *value, s32 default_value)
{
    static const char *const keys[] = {
        "scene", "player", "character", "prop", "weapon", "level",
    };
    static const s32 values[] = {
        EFFECT_TARGET_SCENE, EFFECT_TARGET_PLAYER, EFFECT_TARGET_CHR,
        EFFECT_TARGET_PROP, EFFECT_TARGET_WEAPON, EFFECT_TARGET_LEVEL,
    };
    return distribParseNamedIntValue(value, default_value, keys, values,
        (s32)(sizeof(values) / sizeof(values[0])));
}

static char *distribTrimWhitespace(char *str)
{
    while (*str && isspace((u8)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((u8)*end)) {
        *end = '\0';
        end--;
    }

    return str;
}

static void distribRegisterDependencyList(const char *owner_id, const char *deps,
                                          s32 is_bundled)
{
    if (!owner_id || !owner_id[0] || !deps || !deps[0]) {
        return;
    }

    char buf[512];
    strncpy(buf, deps, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = buf;
    while (tok) {
        char *next = strpbrk(tok, ",|");
        if (next) {
            *next++ = '\0';
        }

        char *dep = distribTrimWhitespace(tok);
        if (dep[0]) {
            catalogDepRegister(owner_id, dep, is_bundled);
        }

        tok = next;
    }
}

static s32 distribParseModeString(const char *val)
{
    if (!val || !val[0]) {
        return 0;
    }

    s32 mode = 0;
    s32 saw_named_token = 0;
    char buf[64];
    strncpy(buf, val, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = buf;
    while (tok) {
        char *pipe = strchr(tok, '|');
        if (pipe) {
            *pipe = '\0';
        }

        char *t = distribTrimWhitespace(tok);
        if (strcmp(t, "mp") == 0) {
            mode |= MAP_MODE_MP;
            saw_named_token = 1;
        } else if (strcmp(t, "solo") == 0) {
            mode |= MAP_MODE_SOLO;
            saw_named_token = 1;
        } else if (strcmp(t, "coop") == 0) {
            mode |= MAP_MODE_COOP;
            saw_named_token = 1;
        }

        tok = pipe ? pipe + 1 : NULL;
    }

    if (!saw_named_token && (((u8)val[0] >= '0' && (u8)val[0] <= '9')
            || val[0] == '-' || val[0] == '+')) {
        return (s32)strtol(val, NULL, 10);
    }

    return mode;
}

static s32 distribSetPrimaryFromFileChecked(asset_entry_t *e,
                                            const char *dirpath,
                                            const char *relpath)
{
    char fullpath[FS_MAXPATH];
    const char *path = relpath;

    if (!relpath || !relpath[0]) {
        return 0;
    }

    if (relpath[0] != '/'
            && relpath[0] != '\\'
            && !(relpath[0] && relpath[1] == ':')
            && dirpath && dirpath[0]) {
        if (!assetPathJoinChecked(fullpath, sizeof(fullpath), dirpath, "/",
                relpath)) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB.ASSET.PATH.REJECT: over-capacity source path");
            return 0;
        }
        path = fullpath;
    }

    catalogSetPrimaryFile(e, path);
    return e && e->source.primary.provider == fileProvider();
}

static s32 distribQualifyFilePath(const char *dirpath,
                                  const char *relpath,
                                  char *out,
                                  size_t outsz)
{
    if (!out || outsz == 0) {
        return 0;
    }

    out[0] = '\0';

    if (!relpath || !relpath[0]) {
        return 0;
    }

    if (relpath[0] != '/'
            && relpath[0] != '\\'
            && !(relpath[0] && relpath[1] == ':')
            && dirpath && dirpath[0]) {
        if (!assetPathJoinChecked(out, outsz, dirpath, "/", relpath)) return 0;
    } else {
        if (!assetPathCopyChecked(out, outsz, relpath)) return 0;
    }
    return out[0] != '\0';
}

static const char *distribFindLastArchiveSeparator(const char *path)
{
    const char *last = NULL;
    const char *p = path;

    while (p && *p) {
        const char *sep = strstr(p, "::");
        if (!sep) {
            break;
        }
        last = sep;
        p = sep + 2;
    }

    return last;
}

static s32 distribSourceModelPathFromMeshArchiveChecked(const char *mesh_archive,
                                                  char *out,
                                                  size_t outsz)
{
    static const char *candidates[] = {
        "model.obj",
        "model.gltf",
        "model.glb",
    };
    size_t i;

    if (!out || outsz == 0) {
        return 0;
    }

    out[0] = '\0';

    if (!mesh_archive || !mesh_archive[0]) {
        return 0;
    }

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!assetPathJoinChecked(out, outsz, mesh_archive, "::",
                candidates[i])) return 0;
        if (fsFileSize(out) > 0) {
            return 1;
        }
    }

    return assetPathJoinChecked(out, outsz, mesh_archive, "::", "model.obj");
}

static s32 distribManifestPathFromMeshArchive(const char *mesh_archive,
                                              char *out,
                                              size_t outsz)
{
    if (!out || outsz == 0) {
        return 0;
    }

    out[0] = '\0';

    if (!mesh_archive || !mesh_archive[0]) {
        return 0;
    }

    const char *sep = distribFindLastArchiveSeparator(mesh_archive);
    if (sep) {
        size_t prefix_len = (size_t)(sep - mesh_archive);
        if (prefix_len == 0 || prefix_len + strlen("::_meta/manifest.json") >= outsz) {
            return 0;
        }
        snprintf(out, outsz, "%.*s::_meta/manifest.json",
                 (int)prefix_len, mesh_archive);
        out[outsz - 1] = '\0';
        return 1;
    }

    const char *slash = strrchr(mesh_archive, '/');
    const char *backslash = strrchr(mesh_archive, '\\');
    if (!slash || (backslash && backslash > slash)) {
        slash = backslash;
    }
    if (!slash) {
        return 0;
    }

    size_t dir_len = (size_t)(slash - mesh_archive);
    if (dir_len == 0 || dir_len + strlen("/_meta/manifest.json") >= outsz) {
        return 0;
    }
    snprintf(out, outsz, "%.*s/_meta/manifest.json",
             (int)dir_len, mesh_archive);
    out[outsz - 1] = '\0';
    return 1;
}

static void distribParseBodyManifestForPrivateSlot(const char *id,
                                                   const char *mesh_archive,
                                                   s32 bodynum)
{
    char manifest_path[FS_MAXPATH + 64];
    char *json;
    u32 json_size = 0;

    if (bodynum < CATALOG_MGR_BODY_CUSTOM_START) {
        return;
    }

    if (!distribManifestPathFromMeshArchive(mesh_archive, manifest_path,
            sizeof(manifest_path))) {
        sysLogPrintf(LOG_WARNING,
                     "NETDISTRIB.BODY.MANIFEST_PATH_MISSING: id=%s mesh=%s",
                     id ? id : "(null)", mesh_archive ? mesh_archive : "(null)");
        return;
    }

    json = (char *)fsFileLoad(manifest_path, &json_size);
    if (!json || json_size == 0) {
        if (json) {
            free(json);
        }
        sysLogPrintf(LOG_WARNING,
                     "NETDISTRIB.BODY.MANIFEST_MISSING: id=%s path=%s",
                     id ? id : "(null)", manifest_path);
        return;
    }

    if (loaderPoolParseBodyJsonForSlot(json, json_size, bodynum)) {
        loaderPoolFinalize();
        {
            const body_data_t *body = loaderPoolGetBody(bodynum);
            if (body) {
                catalogManagerRegisterBody(id, body);
            }
        }
        sysLogPrintf(LOG_NOTE,
                     "NETDISTRIB.BODY.LOADER_SLOT: id=%s slot=%d manifest=%s",
                     id ? id : "(null)", bodynum, manifest_path);
    }

    free(json);
}

static void distribParseHeadManifestForPrivateSlot(const char *id,
                                                   const char *mesh_archive,
                                                   s32 headnum)
{
    char manifest_path[FS_MAXPATH + 64];
    char *json;
    u32 json_size = 0;

    if (headnum < CATALOG_MGR_HEAD_CUSTOM_START) {
        return;
    }

    if (!distribManifestPathFromMeshArchive(mesh_archive, manifest_path,
            sizeof(manifest_path))) {
        sysLogPrintf(LOG_WARNING,
                     "NETDISTRIB.HEAD.MANIFEST_PATH_MISSING: id=%s mesh=%s",
                     id ? id : "(null)", mesh_archive ? mesh_archive : "(null)");
        return;
    }

    json = (char *)fsFileLoad(manifest_path, &json_size);
    if (!json || json_size == 0) {
        if (json) {
            free(json);
        }
        sysLogPrintf(LOG_WARNING,
                     "NETDISTRIB.HEAD.MANIFEST_MISSING: id=%s path=%s",
                     id ? id : "(null)", manifest_path);
        return;
    }

    if (loaderPoolParseHeadJsonForSlot(json, json_size, headnum)) {
        loaderPoolFinalize();
        {
            const head_data_t *head = loaderPoolGetHead(headnum);
            if (head) {
                catalogManagerRegisterHead(id, head);
            }
        }
        sysLogPrintf(LOG_NOTE,
                     "NETDISTRIB.HEAD.LOADER_SLOT: id=%s slot=%d manifest=%s",
                     id ? id : "(null)", headnum, manifest_path);
    }

    free(json);
}

static s32 distribResolveBodyRuntimeSlot(const char *id,
                                         s32 declared_bodynum,
                                         const char *dirpath)
{
    if (declared_bodynum >= 0 && declared_bodynum < CATALOG_MGR_BODY_COUNT) {
        return declared_bodynum;
    }

    s32 custom = assetCatalogResolveBodyPrivateSlot(id);
    if (custom >= 0) {
        sysLogPrintf(LOG_NOTE,
                     "NETDISTRIB.BODY.CUSTOM_SLOT: id=%s slot=%d path=%s",
                     id ? id : "(null)", custom, dirpath ? dirpath : "(null)");
    } else {
        sysLogPrintf(LOG_WARNING,
                     "NETDISTRIB.BODY.RUNTIME_SLOT_MISSING: id=%s bodynum=%d path=%s",
                     id ? id : "(null)", declared_bodynum,
                     dirpath ? dirpath : "(null)");
    }

    return custom;
}

static s32 distribResolveHeadRuntimeSlot(const char *id,
                                         s32 declared_headnum,
                                         const char *dirpath)
{
    if (declared_headnum >= 0 && declared_headnum < CATALOG_MGR_HEAD_COUNT) {
        return declared_headnum;
    }

    s32 custom = assetCatalogResolveHeadPrivateSlot(id);
    if (custom >= 0) {
        sysLogPrintf(LOG_NOTE,
                     "NETDISTRIB.HEAD.CUSTOM_SLOT: id=%s slot=%d path=%s",
                     id ? id : "(null)", custom, dirpath ? dirpath : "(null)");
    } else {
        sysLogPrintf(LOG_WARNING,
                     "NETDISTRIB.HEAD.RUNTIME_SLOT_MISSING: id=%s headnum=%d path=%s",
                     id ? id : "(null)", declared_headnum,
                     dirpath ? dirpath : "(null)");
    }

    return custom;
}

static s32 distribRegisterAnimationCommandSource(const char *id,
                                                  const char *dirpath,
                                                  const char *relpath)
{
    char fullpath[FS_MAXPATH];
    const char *path = relpath;
    char *json;
    u32 json_size = 0;

    if (!relpath || !relpath[0]) {
        return 1;
    }

    if (relpath[0] != '/'
            && relpath[0] != '\\'
            && !(relpath[0] && relpath[1] == ':')
            && dirpath && dirpath[0]) {
        if (!assetPathJoinChecked(fullpath, sizeof(fullpath), dirpath, "/",
                relpath)) return 0;
        path = fullpath;
    }

    json = (char *)fsFileLoad(path, &json_size);
    if (!json || json_size == 0) {
        if (json) {
            free(json);
        }
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: animation command source missing for '%s': %s",
                     id ? id : "", path);
        return 0;
    }

    if (!loaderPoolParseAnimationSourceJson(json, json_size, path)) {
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: animation command source rejected for '%s': %s",
                     id ? id : "", path);
        free(json);
        return 0;
    }

    loaderPoolFinalize();
    sysLogPrintf(LOG_NOTE,
                 "DISTRIB: animation command source registered for '%s': %s",
                 id ? id : "", path);
    free(json);
    return 1;
}

/* Populate the asset_entry_t ext union from the parsed INI, mirroring the
 * field-for-field behavior of assetcatalog_scanner.c's registerComponent().
 * Keeps registration parity between local-scan and wire-delivery paths. */

/* c3849 Wave 2 (parity mirror of the scanner s_mintCustomStagenum): mint a private stagenum for a custom stage component that
 * authored no INI stagenum, and (client only) ensure an idempotent g_Stages
 * row exists so stageGetIndex/s_fillStageResult resolve it. File IDs are 0 (the fileids are u16; -1 would wrap to 65535 and pass the
 * fileid > 0 handle guard) so loading routes through the scenario-source path, never base ROM handles.
 * Returns the minted stagenum or -1 (already loudly logged). */
static s32 distribMintCustomStagenum(asset_entry_t *e, const char *mint_key)
{
    s32 stagenum = assetCatalogResolveStagenumPrivateSlot(mint_key);

    if (stagenum < 0) {
        return -1;
    }

    if (g_Stages != NULL) { /* dedicated server has no stage table */
        s32 idx = stageGetIndex(stagenum);

        if (idx < 0) {
            s32 template_idx = stageGetIndex(STAGE_MP_SKEDAR);

            if (template_idx >= 0) {
                struct stagetableentry tmpl = *stageGetEntry(template_idx);
                tmpl.id = (s16)stagenum;
                tmpl.bgfileid = 0;
                tmpl.tilefileid = 0;
                tmpl.padsfileid = 0;
                tmpl.setupfileid = 0;
                tmpl.mpsetupfileid = 0;
                idx = stageTableAppend(&tmpl);
            }
        }

        if (idx >= 0) {
            e->runtime_index = idx;
        }
    }

    return stagenum;
}

#define distribSetPrimaryFromFile(e, dirpath, relpath) do { \
    if (!distribSetPrimaryFromFileChecked((e), (dirpath), (relpath))) return 0; \
} while (0)
#define distribSourceModelPathFromMeshArchive(mesh, out, outsz) do { \
    if (!distribSourceModelPathFromMeshArchiveChecked((mesh), (out), (outsz))) return 0; \
} while (0)

static s32 distribIniKeyIsSourcePath(const char *key)
{
    return assetPathKeyIsSource(key);
}

static s32 distribQualifyIniSourcePaths(ini_section_t *ini,
                                        const char *dirpath)
{
    char checked[FS_MAXPATH];
    if (!ini) return 0;
    for (s32 i = 0; i < ini->count; i++) {
        const char *value = ini->pairs[i].value;
        if (!distribIniKeyIsSourcePath(ini->pairs[i].key) || !value[0]) continue;
        if (!assetPathQualifyFilesystemChecked(checked, sizeof(checked),
                dirpath, value)
                || !assetPathCopyChecked(ini->pairs[i].value,
                    sizeof(ini->pairs[i].value), checked)) return 0;
        if (strcmp(ini->pairs[i].key, "mesh_archive") == 0) {
            char source_model[FS_MAXPATH];
            if (!assetPathJoinChecked(source_model, sizeof(source_model),
                    checked, "::", "model.obj")) return 0;
        }
    }
    return 1;
}

static s32 populateExtFromIni(asset_entry_t *e, asset_type_e type, const char *dirpath,
                               ini_section_t *ini,
                               const asset_entry_t *preserved_entry)
{
    if (!e || !distribQualifyIniSourcePaths(ini, dirpath)) return 0;
    switch (type) {
    case ASSET_MAP:
        e->ext.map.stagenum = distribMintCustomStagenum(e, e->id); /* c3849 */
        e->ext.map.mode = (u8)distribParseModeString(iniGet(ini, "mode", ""));
        {
            const char *mf = iniGet(ini, "music_file", "");
            if (mf[0]) {
                strncpy(e->ext.map.music_file, mf, FS_MAXPATH - 1);
            }
        }
        break;
    case ASSET_CHARACTER:
        strncpy(e->ext.character.bodyfile,
                iniGet(ini, "body_archive", iniGet(ini, "bodyfile", "")),
                FS_MAXPATH - 1);
        strncpy(e->ext.character.headfile,
                iniGet(ini, "head_archive", iniGet(ini, "headfile", "")),
                FS_MAXPATH - 1);
        strncpy(e->ext.character.portrait_file,
                iniGet(ini, "portrait_file", ""),
                FS_MAXPATH - 1);
        if (e->ext.character.bodyfile[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.character.bodyfile);
        }
        break;
    case ASSET_SKIN:
        strncpy(e->ext.skin.target_id, iniGet(ini, "target", ""), CATALOG_ID_LEN - 1);
        strncpy(e->ext.skin.skin_file, iniGet(ini, "skin_file",
                iniGet(ini, "file_path", "")), sizeof(e->ext.skin.skin_file) - 1);
        e->ext.skin.skin_file[sizeof(e->ext.skin.skin_file) - 1] = '\0';
        strncpy(e->ext.skin.texture_file, iniGet(ini, "texture_file",
                iniGet(ini, "texture", "")), sizeof(e->ext.skin.texture_file) - 1);
        e->ext.skin.texture_file[sizeof(e->ext.skin.texture_file) - 1] = '\0';
        strncpy(e->ext.skin.swatches_file, iniGet(ini, "swatches_file", ""),
                sizeof(e->ext.skin.swatches_file) - 1);
        e->ext.skin.swatches_file[sizeof(e->ext.skin.swatches_file) - 1] = '\0';
        strncpy(e->ext.skin.material_archive, iniGet(ini, "material_archive", ""),
                sizeof(e->ext.skin.material_archive) - 1);
        e->ext.skin.material_archive[sizeof(e->ext.skin.material_archive) - 1] = '\0';
        strncpy(e->ext.skin.texture_archive, iniGet(ini, "texture_archive", ""),
                sizeof(e->ext.skin.texture_archive) - 1);
        e->ext.skin.texture_archive[sizeof(e->ext.skin.texture_archive) - 1] = '\0';
        if (e->ext.skin.texture_file[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.skin.texture_file);
        } else if (e->ext.skin.skin_file[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.skin.skin_file);
        }
        break;
    case ASSET_BOT_VARIANT:
        strncpy(e->ext.bot_variant.base_type, iniGet(ini, "base_type", "NormalSim"), 31);
        e->ext.bot_variant.accuracy      = iniGetFloat(ini, "accuracy", 0.5f);
        e->ext.bot_variant.reaction_time = iniGetFloat(ini, "reaction_time", 0.5f);
        e->ext.bot_variant.aggression    = iniGetFloat(ini, "aggression", 0.5f);
        break;
    case ASSET_ARENA:
        e->ext.arena.stagenum = -1;
        strncpy(e->ext.arena.scenario_id, iniGet(ini, "scenario", ""),
                sizeof(e->ext.arena.scenario_id) - 1);
        e->ext.arena.stagenum = distribMintCustomStagenum(e,
            e->ext.arena.scenario_id[0] ? e->ext.arena.scenario_id : e->id); /* c3849 */
        strncpy(e->ext.arena.scenario_archive,
                iniGet(ini, "scenario_archive", ""),
                sizeof(e->ext.arena.scenario_archive) - 1);
        e->ext.arena.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
        e->ext.arena.name_langid = iniGetInt(ini, "name_langid", 0);
        e->ext.arena.load_mode = iniGetInt(ini, "load_mode", ARENA_LOADMODE_PLAYABLE);
        if (e->ext.arena.scenario_archive[0]) {
            distribSetPrimaryFromFile(e, dirpath,
                                      e->ext.arena.scenario_archive);
        }
        break;
    case ASSET_BODY:
        e->ext.body.bodynum = (s16)distribResolveBodyRuntimeSlot(
            e->id, -1, dirpath);
        if (e->ext.body.bodynum >= 0) {
            e->runtime_index = e->ext.body.bodynum;
        }
        e->ext.body.name_langid = (s16)iniGetInt(ini, "name_langid", 0);
        e->ext.body.headnum = -1;
        e->ext.body.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
        catalogSetBodyDisplayName(e, iniGet(ini, "display_name",
            iniGet(ini, "name", "")));
        catalogSetBodyRigClass(e, iniGet(ini, "rig_class", ""));
        {
            const char *mesh = iniGet(ini, "mesh_archive", "");
            const char *hand = iniGet(ini, "hand_archive", "");
            if (mesh[0]) {
                char mesh_full[FS_MAXPATH + 1];
                char source_path[FS_MAXPATH + 32];
                strncpy(e->ext.body.mesh_archive, mesh,
                    sizeof(e->ext.body.mesh_archive) - 1);
                e->ext.body.mesh_archive[
                    sizeof(e->ext.body.mesh_archive) - 1] = '\0';
                if (distribQualifyFilePath(dirpath, e->ext.body.mesh_archive,
                        mesh_full, sizeof(mesh_full))) {
                    distribSourceModelPathFromMeshArchive(mesh_full,
                        source_path, sizeof(source_path));
                    catalogSetPrimaryFile(e, source_path);
                    distribParseBodyManifestForPrivateSlot(e->id, mesh_full,
                        e->ext.body.bodynum);
                }
            }
            if (hand[0]) {
                strncpy(e->ext.body.hand_archive, hand,
                    sizeof(e->ext.body.hand_archive) - 1);
                e->ext.body.hand_archive[
                    sizeof(e->ext.body.hand_archive) - 1] = '\0';
            }
        }
        break;
    case ASSET_HEAD:
        e->ext.head.headnum = (s16)distribResolveHeadRuntimeSlot(
            e->id, -1, dirpath);
        if (e->ext.head.headnum >= 0) {
            e->runtime_index = e->ext.head.headnum;
        }
        e->ext.head.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
        catalogSetHeadRigClass(e, iniGet(ini, "rig_class", ""));
        {
            const char *mesh = iniGet(ini, "mesh_archive", "");
            if (mesh[0]) {
                char mesh_full[FS_MAXPATH + 1];
                char source_path[FS_MAXPATH + 32];
                strncpy(e->ext.head.mesh_archive, mesh,
                    sizeof(e->ext.head.mesh_archive) - 1);
                e->ext.head.mesh_archive[
                    sizeof(e->ext.head.mesh_archive) - 1] = '\0';
                if (distribQualifyFilePath(dirpath, e->ext.head.mesh_archive,
                        mesh_full, sizeof(mesh_full))) {
                    distribSourceModelPathFromMeshArchive(mesh_full,
                        source_path, sizeof(source_path));
                    catalogSetPrimaryFile(e, source_path);
                    distribParseHeadManifestForPrivateSlot(e->id, mesh_full,
                        e->ext.head.headnum);
                }
            }
        }
        break;
    case ASSET_MODEL:
        {
            const char *pf = iniGet(ini, "model_file",
                iniGet(ini, "model",
                iniGet(ini, "geometry_file",
                iniGet(ini, "geometry",
                iniGet(ini, "file_path", "")))));
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    case ASSET_WEAPON:
        {
            s32 preserved_weapon_id = -1;
            s32 preserved_runtime_index = -1;
            s16 preserved_mp_index = -1;
            u8 preserved_weapon_requirefeature = 0;
            s32 preserved_dual_wieldable = 0;
            char preserved_weapon_name[64];
            preserved_weapon_name[0] = '\0';
            if (preserved_entry && preserved_entry->type == ASSET_WEAPON) {
                preserved_weapon_id = preserved_entry->ext.weapon.weapon_id;
                preserved_runtime_index = preserved_entry->runtime_index;
                preserved_mp_index = preserved_entry->mp_index;
                preserved_weapon_requirefeature = preserved_entry->ext.weapon.requirefeature;
                preserved_dual_wieldable = preserved_entry->ext.weapon.dual_wieldable;
                strncpy(preserved_weapon_name, preserved_entry->ext.weapon.name,
                        sizeof(preserved_weapon_name) - 1);
                preserved_weapon_name[sizeof(preserved_weapon_name) - 1] = '\0';
            }
            e->ext.weapon.weapon_id = preserved_weapon_id;
            if (e->ext.weapon.weapon_id >= 0
                    && e->ext.weapon.weapon_id < NUM_MPWEAPONS) {
                e->mp_index = (s16)e->ext.weapon.weapon_id;
                e->runtime_index = catalogGetMpWeaponNum(e->ext.weapon.weapon_id);
            } else {
                s32 runtime_weapon_id = -1;
                s32 mp_weapon_id = -1;
                if (assetCatalogResolveWeaponPrivateSlots(e->id, -1, 0,
                        &runtime_weapon_id, &mp_weapon_id)) {
                    e->ext.weapon.weapon_id = mp_weapon_id;
                    e->mp_index = (s16)mp_weapon_id;
                    e->runtime_index = runtime_weapon_id;
                } else {
                    e->mp_index = preserved_mp_index;
                    e->runtime_index = preserved_runtime_index;
                }
            }
            {
                const char *weapon_name = iniGet(ini, "name", "");
                strncpy(e->ext.weapon.name,
                        weapon_name[0] ? weapon_name : preserved_weapon_name,
                        sizeof(e->ext.weapon.name) - 1);
                e->ext.weapon.name[sizeof(e->ext.weapon.name) - 1] = '\0';
            }
            strncpy(e->ext.weapon.model_file, iniGet(ini, "model_file", ""),
                    sizeof(e->ext.weapon.model_file) - 1);
            strncpy(e->ext.weapon.behavior_graph,
                    iniGet(ini, "behavior_graph", iniGet(ini, "graph", "")),
                    sizeof(e->ext.weapon.behavior_graph) - 1);
            strncpy(e->ext.weapon.primary_graph,
                    iniGet(ini, "primary_graph", ""),
                    sizeof(e->ext.weapon.primary_graph) - 1);
            strncpy(e->ext.weapon.secondary_graph,
                    iniGet(ini, "secondary_graph", ""),
                    sizeof(e->ext.weapon.secondary_graph) - 1);
            strncpy(e->ext.weapon.shared_context,
                    iniGet(ini, "shared_context_file",
                        iniGet(ini, "shared_context", "")),
                    sizeof(e->ext.weapon.shared_context) - 1);
            strncpy(e->ext.weapon.settings_file,
                    iniGet(ini, "settings_file", iniGet(ini, "settings", "")),
                    sizeof(e->ext.weapon.settings_file) - 1);
            strncpy(e->ext.weapon.variables_file,
                    iniGet(ini, "variables_file", iniGet(ini, "variables", "")),
                    sizeof(e->ext.weapon.variables_file) - 1);
            /* c3849 Wave 5f: presentation fold-in. Field-for-field parity
             * with assetcatalog_scanner.c and loader_walker_weapon.c --
             * keep all three in sync. */
            strncpy(e->ext.weapon.presentation_file,
                    iniGet(ini, "presentation_file",
                        iniGet(ini, "presentation", "")),
                    sizeof(e->ext.weapon.presentation_file) - 1);
            if (e->ext.weapon.primary_graph[0]) {
                distribSetPrimaryFromFile(e, dirpath, e->ext.weapon.primary_graph);
            }
            /* S484 F9 / Mike I.2 (2026-04-27): damage/fire_rate/ammo_type
             * shadow fields dropped. Wire-delivered mod INIs that include
             * those keys are now parsed-and-ignored; the manager is the
             * single source of truth. */
            e->ext.weapon.dual_wieldable = iniGetInt(ini, "dual_wieldable",
                    preserved_dual_wieldable);
            e->ext.weapon.requirefeature = preserved_weapon_requirefeature;
        }
        break;
    case ASSET_PROJECTILE:
        strncpy(e->ext.projectile.name, iniGet(ini, "name", ""),
                sizeof(e->ext.projectile.name) - 1);
        strncpy(e->ext.projectile.model_file, iniGet(ini, "model_file",
                iniGet(ini, "model", "")), sizeof(e->ext.projectile.model_file) - 1);
        strncpy(e->ext.projectile.behavior_graph, iniGet(ini, "behavior_graph",
                iniGet(ini, "graph", "")), sizeof(e->ext.projectile.behavior_graph) - 1);
        strncpy(e->ext.projectile.entity_ref, iniGet(ini, "entity_ref",
                iniGet(ini, "transition_entity", "")), sizeof(e->ext.projectile.entity_ref) - 1);
        if (e->ext.projectile.behavior_graph[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.projectile.behavior_graph);
        }
        break;
    case ASSET_ENTITY:
        strncpy(e->ext.entity.name, iniGet(ini, "name", ""),
                sizeof(e->ext.entity.name) - 1);
        strncpy(e->ext.entity.archetype, iniGet(ini, "archetype", ""),
                sizeof(e->ext.entity.archetype) - 1);
        strncpy(e->ext.entity.model_file, iniGet(ini, "model_file",
                iniGet(ini, "model", "")), sizeof(e->ext.entity.model_file) - 1);
        strncpy(e->ext.entity.behavior_graph, iniGet(ini, "behavior_graph",
                iniGet(ini, "graph", "")), sizeof(e->ext.entity.behavior_graph) - 1);
        if (e->ext.entity.behavior_graph[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.entity.behavior_graph);
        }
        break;
    case ASSET_ANIMATION:
        e->ext.anim.anim_id = -1;
        e->source_animnum = -1;
        e->runtime_index = -1;
        if (assetCatalogAnimationCategoryUsesCharacterClip(e->category)) {
            /* c3849 Wave 2: mirror the scanner. */
            s32 slot = assetCatalogResolveAnimPrivateSlot(e->id);
            if (slot >= 0) {
                e->ext.anim.anim_id = slot;
                e->source_animnum = slot;
                e->runtime_index = slot;
            }
        }
        strncpy(e->ext.anim.name, iniGet(ini, "name", ""),
                sizeof(e->ext.anim.name) - 1);
        e->ext.anim.frame_count = iniGetInt(ini, "frame_count", 0);
        e->ext.anim.bytes_per_frame = iniGetInt(ini, "bytes_per_frame", 0);
        e->ext.anim.header_len = iniGetInt(ini, "header_len", 0);
        e->ext.anim.framelen = iniGetInt(ini, "framelen", 0);
        e->ext.anim.flags = iniGetInt(ini, "flags", 0);
        strncpy(e->ext.anim.target_body, iniGet(ini, "target_body", ""),
                sizeof(e->ext.anim.target_body) - 1);
        {
            const char *af = iniGet(ini, "animation_file",
                iniGet(ini, "commands_file", iniGet(ini, "file_path", "")));
            if (af[0]) {
                distribSetPrimaryFromFile(e, dirpath, af);
            }
            {
                const char *cf = iniGet(ini, "commands_file", "");
                if (cf[0]) {
                    if (!distribRegisterAnimationCommandSource(e->id, dirpath,
                            cf)) return 0;
                }
            }
        }
        break;
    case ASSET_PROP:
        e->ext.prop.prop_type = distribParsePropKeyValue(
            iniGet(ini, "prop_key", iniGet(ini, "prop_type", "")), 0);
        strncpy(e->ext.prop.name, iniGet(ini, "name", ""), sizeof(e->ext.prop.name) - 1);
        strncpy(e->ext.prop.prop_file, iniGet(ini, "prop_file",
                iniGet(ini, "file_path", "")), sizeof(e->ext.prop.prop_file) - 1);
        strncpy(e->ext.prop.model_file, iniGet(ini, "model_file", ""), sizeof(e->ext.prop.model_file) - 1);
        strncpy(e->ext.prop.behavior_graph,
                iniGet(ini, "behavior_graph", iniGet(ini, "graph", "")),
                sizeof(e->ext.prop.behavior_graph) - 1);
        if (e->ext.prop.model_file[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.prop.model_file);
        } else if (e->ext.prop.prop_file[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.prop.prop_file);
        }
        e->ext.prop.flags = (u32)iniGetInt(ini, "flags", 0);
        e->ext.prop.health = iniGetFloat(ini, "health", 100.0f);
        break;
    case ASSET_TEXTURE:
        e->ext.texture.texture_id = -1;
        if (e->ext.texture.texture_id >= 0) {
            e->source_texnum = e->ext.texture.texture_id;
        } else {
            /* c3849 Wave 2: mirror the scanner -- net-distributed custom
             * texture with no base texnum gets a private slot. */
            s32 slot = assetCatalogResolveTexturePrivateSlot(e->id);
            if (slot >= 0) {
                e->source_texnum = slot;
                e->runtime_index = slot;
            }
        }
        e->ext.texture.width = iniGetInt(ini, "width", 0);
        e->ext.texture.height = iniGetInt(ini, "height", 0);
        e->ext.texture.format = iniGetInt(ini, "format", 0);
        strncpy(e->ext.texture.file_path, iniGet(ini, "file_path",
                iniGet(ini, "texture_file", "")), sizeof(e->ext.texture.file_path) - 1);
        if (e->ext.texture.file_path[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.texture.file_path);
        }
        break;
    case ASSET_MATERIAL:
        {
            const char *pf = iniGet(ini, "material_file",
                iniGet(ini, "file_path", ""));
            strncpy(e->ext.material.material_file,
                    iniGet(ini, "material_file", iniGet(ini, "file_path", "")),
                    sizeof(e->ext.material.material_file) - 1);
            strncpy(e->ext.material.texture_archive,
                    iniGet(ini, "texture_archive", ""),
                    sizeof(e->ext.material.texture_archive) - 1);
            strncpy(e->ext.material.effect_archive,
                    iniGet(ini, "effect_archive", ""),
                    sizeof(e->ext.material.effect_archive) - 1);
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    case ASSET_AUDIO:
        e->ext.audio.sound_id = -1;
        strncpy(e->ext.audio.name, iniGet(ini, "name", ""), sizeof(e->ext.audio.name) - 1);
        e->ext.audio.category = distribParseAudioCategoryValue(
            iniGet(ini, "audio_category",
                iniGet(ini, "kind",
                iniGet(ini, "category", ""))),
            distribAudioCategoryForSection(ini));
        e->ext.audio.duration_ms = iniGetInt(ini, "duration_ms", 0);
        strncpy(e->ext.audio.voice_actor, iniGet(ini, "actor", ""),
                sizeof(e->ext.audio.voice_actor) - 1);
        strncpy(e->ext.audio.voice_transcript, iniGet(ini, "transcript", ""),
                sizeof(e->ext.audio.voice_transcript) - 1);
        strncpy(e->ext.audio.voice_language, iniGet(ini, "language", ""),
                sizeof(e->ext.audio.voice_language) - 1);
        strncpy(e->ext.audio.voice_context, iniGet(ini, "context", ""),
                sizeof(e->ext.audio.voice_context) - 1);
        distribQualifyFilePath(dirpath, iniGet(ini, "subtitle_file", ""),
                e->ext.audio.subtitle_file,
                sizeof(e->ext.audio.subtitle_file));
        strncpy(e->ext.audio.fallback_locale,
                iniGet(ini, "fallback_locale", ""),
                sizeof(e->ext.audio.fallback_locale) - 1);
        {
            static const char *locale_keys[6] = {
                "locale_en_file", "locale_fr_file", "locale_de_file",
                "locale_it_file", "locale_es_file", "locale_ja_file"
            };
            for (s32 i = 0; i < 6; i++) {
                distribQualifyFilePath(dirpath, iniGet(ini, locale_keys[i], ""),
                    e->ext.audio.locale_audio_files[i],
                    sizeof(e->ext.audio.locale_audio_files[i]));
            }
        }
        e->ext.audio.has_keymap = iniGetInt(ini, "has_keymap",
            iniGet(ini, "key_base", NULL) != NULL ? 1 : 0);
        e->ext.audio.key_min = iniGetInt(ini, "key_min", 0);
        e->ext.audio.key_max = iniGetInt(ini, "key_max", 127);
        e->ext.audio.key_base = iniGetInt(ini, "key_base", 60);
        e->ext.audio.key_detune = iniGetInt(ini, "key_detune", 0);
        e->ext.audio.velocity_min = iniGetInt(ini, "velocity_min", 0);
        e->ext.audio.velocity_max = iniGetInt(ini, "velocity_max", 0);
        e->ext.audio.sample_pan = iniGetInt(ini, "sample_pan", 64);
        e->ext.audio.sample_volume = iniGetInt(ini, "sample_volume", 127);
        e->ext.audio.loop_start_samples = (u32)iniGetInt(ini, "loop_start_samples", 0);
        e->ext.audio.loop_end_samples = (u32)iniGetInt(ini, "loop_end_samples", 0);
        e->ext.audio.loop_count = (u32)iniGetInt(ini, "loop_count", 0);
        {
            const char *has_loop = iniGet(ini, "has_loop", "0");
            e->ext.audio.has_loop = iniGetInt(ini, "has_loop", 0)
                || strcmp(has_loop, "true") == 0
                || strcmp(has_loop, "yes") == 0
                || e->ext.audio.loop_end_samples > e->ext.audio.loop_start_samples
                || e->ext.audio.loop_count != 0;
        }
        e->ext.audio.attack_time_us = (u32)iniGetInt(ini, "attack_time_us", 0);
        e->ext.audio.decay_time_us = (u32)iniGetInt(ini, "decay_time_us", 0);
        e->ext.audio.release_time_us = (u32)iniGetInt(ini, "release_time_us", 0);
        e->ext.audio.attack_volume = iniGetInt(ini, "attack_volume", 127);
        e->ext.audio.decay_volume = iniGetInt(ini, "decay_volume", 127);
        {
            const char *has_envelope = iniGet(ini, "has_envelope", "0");
            e->ext.audio.has_envelope = iniGetInt(ini, "has_envelope", 0)
                || strcmp(has_envelope, "true") == 0
                || strcmp(has_envelope, "yes") == 0
                || e->ext.audio.attack_time_us != 0
                || e->ext.audio.decay_time_us != 0
                || e->ext.audio.release_time_us != 0
                || e->ext.audio.attack_volume != 127
                || e->ext.audio.decay_volume != 127;
        }
        {
            const char *audio_file = iniGet(ini, "file_path", "");
            const char *primary_file = audio_file[0] ? audio_file :
                iniGet(ini, "music_file", iniGet(ini, "midi_file", ""));
            strncpy(e->ext.audio.file_path, audio_file,
                    sizeof(e->ext.audio.file_path) - 1);
            if (primary_file[0]) {
                distribSetPrimaryFromFile(e, dirpath, primary_file);
            }
            /* c3849 Wave 2: mirror the scanner -- net-distributed custom
             * SFX/VOICE with no authored sound_id gets a private soundnum
             * (never MUSIC: sound_id doubles as a tracknum there). */
            if (e->ext.audio.sound_id < 0 &&
                    e->ext.audio.category != AUDIO_CAT_MUSIC &&
                    primary_file[0]) {
                s32 slot = assetCatalogResolveSoundPrivateSlot(e->id);
                if (slot >= 0) {
                    e->ext.audio.sound_id = slot;
                    e->source_soundnum = slot;
                }
            }
        }
        break;
    case ASSET_GAMEMODE:
        e->ext.gamemode.mode_id = distribParseModeKeyValue(
            iniGet(ini, "mode_key", iniGet(ini, "mode_id", "")), -1);
        strncpy(e->ext.gamemode.name, iniGet(ini, "name", ""),
                sizeof(e->ext.gamemode.name) - 1);
        strncpy(e->ext.gamemode.description, iniGet(ini, "description", ""),
                sizeof(e->ext.gamemode.description) - 1);
        e->ext.gamemode.min_players = iniGetInt(ini, "min_players", 2);
        e->ext.gamemode.max_players = iniGetInt(ini, "max_players", 8);
        e->ext.gamemode.team_based = iniGetInt(ini, "team_based", 0);
        e->ext.gamemode.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
        {
            const char *rf = iniGet(ini, "rules_file",
                iniGet(ini, "file_path", ""));
            strncpy(e->ext.gamemode.rules_file, rf,
                    sizeof(e->ext.gamemode.rules_file) - 1);
            if (rf[0]) {
                distribSetPrimaryFromFile(e, dirpath, rf);
            }
        }
        break;
    case ASSET_SCENARIO:
        e->ext.scenario.stagenum = distribMintCustomStagenum(e, e->id); /* c3849 */
        e->ext.scenario.mode = (u8)distribParseModeString(iniGet(ini, "mode", ""));
        {
            const char *sf = iniGet(ini, "scene_file",
                iniGet(ini, "scene",
                iniGet(ini, "runtime_source_file",
                iniGet(ini, "blender_scene_file",
                iniGet(ini, "visual_scene_file", "")))));
            const char *cf = iniGet(ini, "collision_file",
                iniGet(ini, "collision_source_file",
                iniGet(ini, "collision_source", "")));
            const char *rf = iniGet(ini, "rooms_file",
                iniGet(ini, "rooms",
                iniGet(ini, "geometry_file",
                iniGet(ini, "geometry", ""))));
            strncpy(e->ext.scenario.scene_file, sf,
                    sizeof(e->ext.scenario.scene_file) - 1);
            strncpy(e->ext.scenario.collision_file, cf,
                    sizeof(e->ext.scenario.collision_file) - 1);
            strncpy(e->ext.scenario.rooms_file, rf,
                    sizeof(e->ext.scenario.rooms_file) - 1);
            strncpy(e->ext.scenario.portals_file,
                    iniGet(ini, "portals_file", ""),
                    sizeof(e->ext.scenario.portals_file) - 1);
            strncpy(e->ext.scenario.pads_file, iniGet(ini, "pads_file", ""),
                    sizeof(e->ext.scenario.pads_file) - 1);
            strncpy(e->ext.scenario.spawns_file, iniGet(ini, "spawns_file", ""),
                    sizeof(e->ext.scenario.spawns_file) - 1);
            strncpy(e->ext.scenario.volumes_file, iniGet(ini, "volumes_file", ""),
                    sizeof(e->ext.scenario.volumes_file) - 1);
            strncpy(e->ext.scenario.objects_file,
                    iniGet(ini, "objects_file", iniGet(ini, "props_file", "")),
                    sizeof(e->ext.scenario.objects_file) - 1);
            strncpy(e->ext.scenario.setup_fields_file,
                    iniGet(ini, "setup_fields_file", ""),
                    sizeof(e->ext.scenario.setup_fields_file) - 1);
            strncpy(e->ext.scenario.ai_lists_file,
                    iniGet(ini, "ai_lists_file", ""),
                    sizeof(e->ext.scenario.ai_lists_file) - 1);
            strncpy(e->ext.scenario.objectives_file,
                    iniGet(ini, "objectives_file", ""),
                    sizeof(e->ext.scenario.objectives_file) - 1);
            strncpy(e->ext.scenario.navigation_file,
                    iniGet(ini, "navigation_file", ""),
                    sizeof(e->ext.scenario.navigation_file) - 1);
            strncpy(e->ext.scenario.navigation_waypoints_file,
                    iniGet(ini, "waypoints_file", ""),
                    sizeof(e->ext.scenario.navigation_waypoints_file) - 1);
            strncpy(e->ext.scenario.navigation_waygroups_file,
                    iniGet(ini, "waygroups_file", ""),
                    sizeof(e->ext.scenario.navigation_waygroups_file) - 1);
            strncpy(e->ext.scenario.navigation_covers_file,
                    iniGet(ini, "covers_file", ""),
                    sizeof(e->ext.scenario.navigation_covers_file) - 1);
            strncpy(e->ext.scenario.navigation_paths_file,
                    iniGet(ini, "paths_file", ""),
                    sizeof(e->ext.scenario.navigation_paths_file) - 1);
            strncpy(e->ext.scenario.level_graph_file,
                    iniGet(ini, "level_graph_file", iniGet(ini, "level_graph", "")),
                    sizeof(e->ext.scenario.level_graph_file) - 1);
            if (sf[0]) {
                distribSetPrimaryFromFile(e, dirpath, sf);
            } else if (rf[0]) {
                distribSetPrimaryFromFile(e, dirpath, rf);
            }
        }
        break;
    case ASSET_HUD:
        e->ext.hud.element_type = distribParseHudElementKeyValue(
            iniGet(ini, "hud_key",
                iniGet(ini, "element",
                iniGet(ini, "element_type", ""))),
            HUD_ELEM_CROSSHAIR);
        e->ext.hud.hud_id = iniGetInt(ini, "hud_id",
            e->ext.hud.element_type);
        strncpy(e->ext.hud.name, iniGet(ini, "name", ""), sizeof(e->ext.hud.name) - 1);
        strncpy(e->ext.hud.texture_file, iniGet(ini, "texture_file", ""),
                sizeof(e->ext.hud.texture_file) - 1);
        strncpy(e->ext.hud.layout_file,
                iniGet(ini, "layout_file", iniGet(ini, "file_path", "")),
                sizeof(e->ext.hud.layout_file) - 1);
        if (e->ext.hud.layout_file[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.hud.layout_file);
        }
        break;
    case ASSET_UI:
        {
            const char *pf = iniGet(ini, "file_path",
                iniGet(ini, "texture_file",
                iniGet(ini, "texture",
                iniGet(ini, "ui_file", ""))));
            const char *lf = iniGet(ini, "layout_file", "");
            const char *nf = iniGet(ini, "nineslice_file", "");
            const char *tn = iniGet(ini, "texture_name", "");
            strncpy(e->ext.ui.texture_file, pf,
                    sizeof(e->ext.ui.texture_file) - 1);
            e->ext.ui.texture_file[sizeof(e->ext.ui.texture_file) - 1] = '\0';
            strncpy(e->ext.ui.layout_file, lf,
                    sizeof(e->ext.ui.layout_file) - 1);
            e->ext.ui.layout_file[sizeof(e->ext.ui.layout_file) - 1] = '\0';
            strncpy(e->ext.ui.nineslice_file, nf,
                    sizeof(e->ext.ui.nineslice_file) - 1);
            e->ext.ui.nineslice_file[sizeof(e->ext.ui.nineslice_file) - 1] = '\0';
            strncpy(e->ext.ui.texture_name, tn,
                    sizeof(e->ext.ui.texture_name) - 1);
            e->ext.ui.texture_name[sizeof(e->ext.ui.texture_name) - 1] = '\0';
            e->ext.ui.width = iniGetInt(ini, "width", 0);
            e->ext.ui.height = iniGetInt(ini, "height", 0);
            e->ext.ui.data_size = iniGetInt(ini, "data_size", 0);
            e->ext.ui.nineslice_left = iniGetInt(ini, "nineslice_left", 0);
            e->ext.ui.nineslice_right = iniGetInt(ini, "nineslice_right", 0);
            e->ext.ui.nineslice_top = iniGetInt(ini, "nineslice_top", 0);
            e->ext.ui.nineslice_bottom = iniGetInt(ini, "nineslice_bottom", 0);
            strncpy(e->ext.ui.nineslice_edge_mode,
                    iniGet(ini, "nineslice_edge_mode", ""),
                    sizeof(e->ext.ui.nineslice_edge_mode) - 1);
            e->ext.ui.nineslice_edge_mode[
                    sizeof(e->ext.ui.nineslice_edge_mode) - 1] = '\0';
            strncpy(e->ext.ui.nineslice_center_mode,
                    iniGet(ini, "nineslice_center_mode", ""),
                    sizeof(e->ext.ui.nineslice_center_mode) - 1);
            e->ext.ui.nineslice_center_mode[
                    sizeof(e->ext.ui.nineslice_center_mode) - 1] = '\0';
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    case ASSET_FONT:
        {
            const char *pf = iniGet(ini, "font_file",
                iniGet(ini, "glyphs_file",
                iniGet(ini, "file_path",
                iniGet(ini, "font", ""))));
            const char *mf = iniGet(ini, "metrics_file", "");
            strncpy(e->ext.font.font_file, pf,
                    sizeof(e->ext.font.font_file) - 1);
            strncpy(e->ext.font.metrics_file, mf,
                    sizeof(e->ext.font.metrics_file) - 1);
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    case ASSET_LANG:
        e->ext.lang.bank_id = iniGetInt(ini, "bank_id",
            iniGetInt(ini, "source_bank", -1));
        {
            const char *locale = iniGet(ini, "locale", "");
            const char *category = iniGet(ini, "category", "");
            const char *sf = iniGet(ini, "strings_file",
                iniGet(ini, "strings",
                iniGet(ini, "file_path", "")));
            strncpy(e->ext.lang.locale, locale, sizeof(e->ext.lang.locale) - 1);
            e->ext.lang.locale[sizeof(e->ext.lang.locale) - 1] = '\0';
            strncpy(e->ext.lang.lang_category, category,
                    sizeof(e->ext.lang.lang_category) - 1);
            e->ext.lang.lang_category[sizeof(e->ext.lang.lang_category) - 1] = '\0';
            e->ext.lang.string_count = (u32)iniGetInt(ini, "string_count", 0);
            strncpy(e->ext.lang.strings_file, sf,
                    sizeof(e->ext.lang.strings_file) - 1);
            e->ext.lang.strings_file[sizeof(e->ext.lang.strings_file) - 1] = '\0';
            if (e->ext.lang.strings_file[0]) {
                distribSetPrimaryFromFile(e, dirpath, e->ext.lang.strings_file);
            }
        }
        break;
    case ASSET_EFFECT:
        strncpy(e->ext.effect.name, iniGet(ini, "name", ""),
                sizeof(e->ext.effect.name) - 1);
        e->ext.effect.effect_type = distribParseEffectTypeKeyValue(
            iniGet(ini, "effect_key", iniGet(ini, "effect_type", "")),
            EFFECT_TYPE_PARTICLE);
        e->ext.effect.target = distribParseEffectTargetKeyValue(
            iniGet(ini, "target_key", iniGet(ini, "target", "")),
            EFFECT_TARGET_SCENE);
        strncpy(e->ext.effect.effect_file, iniGet(ini, "effect_file",
                iniGet(ini, "behavior_graph",
                iniGet(ini, "file_path", ""))),
                sizeof(e->ext.effect.effect_file) - 1);
        strncpy(e->ext.effect.timeline_file, iniGet(ini, "timeline_file",
                iniGet(ini, "timeline", "")),
                sizeof(e->ext.effect.timeline_file) - 1);
        strncpy(e->ext.effect.shader_id, iniGet(ini, "shader_id", ""),
                sizeof(e->ext.effect.shader_id) - 1);
        e->ext.effect.intensity = iniGetFloat(ini, "intensity", 1.0f);
        {
            const char *pf = e->ext.effect.effect_file;
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            } else if (e->ext.effect.timeline_file[0]) {
                distribSetPrimaryFromFile(e, dirpath,
                        e->ext.effect.timeline_file);
            }
        }
        break;
    case ASSET_BOT_PROFILE:
        e->ext.bot_profile.type = distribParseBotTypeKeyValue(
            iniGet(ini, "type_key", iniGet(ini, "type", "")),
            BOTTYPE_GENERAL);
        e->ext.bot_profile.difficulty = distribParseBotDifficultyKeyValue(
            iniGet(ini, "difficulty_key", iniGet(ini, "difficulty", "")),
            BOTDIFF_NORMAL);
        e->ext.bot_profile.body = (s16)iniGetInt(ini, "body", -1);
        e->ext.bot_profile.name_langid = (s16)iniGetInt(ini, "name_langid", 0);
        e->ext.bot_profile.requirefeature = (u8)iniGetInt(ini, "requirefeature", 0);
        strncpy(e->ext.bot_profile.target_body,
                iniGet(ini, "target_body",
                iniGet(ini, "body_id",
                iniGet(ini, "body_ref", ""))),
                sizeof(e->ext.bot_profile.target_body) - 1);
        strncpy(e->ext.bot_profile.profile_file, iniGet(ini, "profile_file",
                iniGet(ini, "file_path", "")), sizeof(e->ext.bot_profile.profile_file) - 1);
        if (e->ext.bot_profile.profile_file[0]) {
            distribSetPrimaryFromFile(e, dirpath, e->ext.bot_profile.profile_file);
        }
        break;
    case ASSET_VEHICLE:
        {
            strncpy(e->ext.vehicle.model_file,
                    iniGet(ini, "model_file", iniGet(ini, "file_path", "")),
                    sizeof(e->ext.vehicle.model_file) - 1);
            strncpy(e->ext.vehicle.physics_file,
                    iniGet(ini, "physics_file", ""),
                    sizeof(e->ext.vehicle.physics_file) - 1);
            strncpy(e->ext.vehicle.behavior_graph,
                    iniGet(ini, "behavior_graph", ""),
                    sizeof(e->ext.vehicle.behavior_graph) - 1);
            const char *pf = e->ext.vehicle.model_file[0] ?
                e->ext.vehicle.model_file :
                e->ext.vehicle.behavior_graph;
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    case ASSET_MISSION:
        {
            strncpy(e->ext.mission.scenario_archive,
                    iniGet(ini, "scenario_archive", iniGet(ini, "file_path", "")),
                    sizeof(e->ext.mission.scenario_archive) - 1);
            strncpy(e->ext.mission.objectives_file,
                    iniGet(ini, "objectives_file", ""),
                    sizeof(e->ext.mission.objectives_file) - 1);
            strncpy(e->ext.mission.mission_graph_file,
                    iniGet(ini, "mission_graph_file", iniGet(ini, "graph", "")),
                    sizeof(e->ext.mission.mission_graph_file) - 1);
            const char *pf = e->ext.mission.mission_graph_file;
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    case ASSET_THEME:
        {
            strncpy(e->ext.theme.theme_file,
                    iniGet(ini, "theme_file", iniGet(ini, "file_path", "")),
                    sizeof(e->ext.theme.theme_file) - 1);
            strncpy(e->ext.theme.ui_archive,
                    iniGet(ini, "ui_archive", ""),
                    sizeof(e->ext.theme.ui_archive) - 1);
            strncpy(e->ext.theme.font_archive,
                    iniGet(ini, "font_archive", ""),
                    sizeof(e->ext.theme.font_archive) - 1);
            strncpy(e->ext.theme.audio_archive,
                    iniGet(ini, "audio_archive", ""),
                    sizeof(e->ext.theme.audio_archive) - 1);
            strncpy(e->ext.theme.music_archive,
                    iniGet(ini, "music_archive", ""),
                    sizeof(e->ext.theme.music_archive) - 1);
            strncpy(e->ext.theme.effect_archive,
                    iniGet(ini, "effect_archive", ""),
                    sizeof(e->ext.theme.effect_archive) - 1);
            const char *pf = e->ext.theme.theme_file;
            if (pf[0]) {
                distribSetPrimaryFromFile(e, dirpath, pf);
            }
        }
        break;
    default:
        /* ASSET_TEXTURES, ASSET_SFX, ASSET_MUSIC: no extra ext fields. */
        break;
    }
    return 1;
}

#undef distribSourceModelPathFromMeshArchive
#undef distribSetPrimaryFromFile

static void distribMarkRegisteredTree(const char *root_dir, s32 temporary)
{
	if (!root_dir || !root_dir[0]) return;
	size_t root_len = strlen(root_dir);
	for (s32 i = 0; i < assetCatalogGetPoolSize(); i++) {
		const asset_entry_t *entry = assetCatalogGetByIndex(i);
		if (!entry || !entry->id[0]) continue;
		const char *source = entry->descriptor_path[0]
			? entry->descriptor_path : entry->dirpath;
		if (strncmp(source, root_dir, root_len) != 0
				|| (source[root_len] != '\0' && source[root_len] != '/'
					&& source[root_len] != '\\')) {
			continue;
		}
		asset_entry_t *mutable_entry = assetCatalogGetMutable(entry->id);
		if (mutable_entry) {
			mutable_entry->temporary = temporary ? 1 : 0;
			mutable_entry->bundled = 0;
		}
	}
}

static const u8 *distribManifestComponentSha(const char *id)
{
	if (!id || !id[0]) return NULL;
	for (u16 i = 0; i < g_ClientManifest.num_entries; i++) {
		const match_manifest_entry_t *entry = &g_ClientManifest.entries[i];
		if (entry->type == MANIFEST_TYPE_COMPONENT
				&& strcmp(entry->id, id) == 0) {
			return entry->sha256;
		}
	}
	return NULL;
}

void netDistribClientHandleEnd(const char *catalog_id, u8 success)
{
    if (!s_Initialized) return;
    s32 slot_completed = 0;

    /* v27: find slot by catalog ID string. */
    distrib_recv_slot_t *slot = NULL;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active && strncmp(s_RecvSlots[i].id, catalog_id, sizeof(s_RecvSlots[i].id)) == 0) {
            slot = &s_RecvSlots[i];
            break;
        }
    }

    if (!slot) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: END for unknown catalog_id '%s'", catalog_id);
		if (!success && (s_ClientStatus.state == DISTRIB_CSTATE_DIFFING
				|| s_ClientStatus.state == DISTRIB_CSTATE_RECEIVING)) {
			s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
			netDistribClientDeclineActiveManifest("server transfer failure");
		}
        return;
    }

    /* C-3: Block processing if transfer requires approval that hasn't been granted. */
    if (slot->needs_approval) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: END for '%s' blocked — awaiting user approval", catalog_id);
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

    /* SEC-5 / v46: mandatory archive integrity before decompression. */
    {
        u8 actual[SHA256_DIGEST_SIZE];
        sha256Hash(slot->compressed_buf, slot->compressed_len, actual);
        if (memcmp(actual, slot->expected_sha256, sizeof(actual)) != 0) {
            char expect_hex[SHA256_HEX_SIZE], actual_hex[SHA256_HEX_SIZE];
            sha256ToHex(slot->expected_sha256, expect_hex);
            sha256ToHex(actual, actual_hex);
            sysLogPrintf(LOG_ERROR,
                         "DISTRIB: SHA-256 mismatch for '%s': expected %s, got %s",
                         slot->id, expect_hex, actual_hex);
            goto done;
        }
        sysLogPrintf(LOG_NOTE, "DISTRIB: SHA-256 verified for '%s'", slot->id);
    }

    /* Decompress */
    if (slot->archive_bytes == 0) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: archive_bytes is zero — rejecting '%s'", slot->id);
        goto done;
    }
    /* C-4: Reject absurdly large decompression to prevent integer overflow in alloc. */
    if (slot->archive_bytes > 256u * 1024u * 1024u) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: archive_bytes %u exceeds 256MB safety cap — rejecting '%s'",
                     slot->archive_bytes, slot->id);
        goto done;
    }
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
    const char *modsdir = modmgrGetModsDir();
    if (!modsdir || !modsdir[0]) {
        modsdir = fsGetModDir();
    }
    if (!modsdir || !modsdir[0]) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: no mods directory available for '%s'", slot->id);
        goto done;
    }
    char destdir[FS_MAXPATH];
    char category_segment[129];
    char id_segment[129];
    if (!distribStorageSegment(slot->category, category_segment,
            sizeof(category_segment))
            || !distribStorageSegment(slot->id, id_segment,
                sizeof(id_segment))) {
        sysLogPrintf(LOG_WARNING,
            "DISTRIB.ASSET.PATH.REJECT: invalid received storage identity id='%s' category='%s'",
            slot->id, slot->category);
        goto done;
    }
    if (slot->temporary) {
        char temp_root[FS_MAXPATH];
        char category_root[FS_MAXPATH];
        if (!assetPathJoinChecked(temp_root, sizeof(temp_root), modsdir, "/",
                TEMP_SUBDIR)
                || !assetPathJoinChecked(category_root, sizeof(category_root),
                    temp_root, "/", category_segment)
                || !assetPathJoinChecked(destdir, sizeof(destdir), category_root,
                    "/", id_segment)) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB.ASSET.PATH.REJECT: received destination exceeds capacity for '%s'",
                slot->id);
            goto done;
        }
    } else {
        char category_root[FS_MAXPATH];
        if (!assetPathJoinChecked(category_root, sizeof(category_root), modsdir,
                "/", category_segment)
                || !assetPathJoinChecked(destdir, sizeof(destdir), category_root,
                    "/", id_segment)) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB.ASSET.PATH.REJECT: received destination exceeds capacity for '%s'",
                slot->id);
            goto done;
        }
    }

    /* Extract into a unique sibling and publish only after every member is
     * durable. A pre-existing component is restored on any publish failure. */
    pdca_extract_transaction_t install_transaction;
    pdca_extract_result_t extract_result = pdcaExtractArchiveBegin(raw,
        (u32)raw_len, destdir, NULL, &install_transaction);
    if (extract_result > 0) {
        sysLogPrintf(LOG_NOTE,
            "DISTRIB: transactionally staged received archive at %s pending catalog admission",
            destdir);
        /* Hot-register in catalog */
        char inipath[FS_MAXPATH];
        const char *ini_names[] = { "map.ini", "character.ini", "bot.ini", "prop.ini",
                                    "textures.ini", "texture.ini", "skin.ini",
                                    "material.ini", "mesh.ini", "model.ini", "effect.ini",
                                    "weapon.ini", "projectile.ini", "entity.ini",
                                    "head.ini", "body.ini",
                                    "arena.ini", "scenario.ini", "animation.ini",
                                    "vehicle.ini", "mission.ini", "gamemode.ini",
                                    "botprofile.ini", "audio.ini", "hud.ini",
                                    "sound.ini", "sfx.ini", "voice.ini", "music.ini",
                                    "ui.ini", "font.ini", "lang.ini", "theme.ini", NULL };
        ini_section_t ini;
        s32 package_transfer =
			strcmp(slot->category, DISTRIB_PACKAGE_CATEGORY) == 0;
		s32 registered = 0;
		if (package_transfer) {
			const u8 *expected_sha = distribManifestComponentSha(slot->id);
			registered = expected_sha
				? modmgrRegisterSessionFolder(destdir, slot->id, expected_sha)
				: 0;
			if (!expected_sha) {
				sysLogPrintf(LOG_WARNING,
					"DISTRIB: package '%s' has no component identity in the active manifest",
					slot->id);
			}
		} else {
			/* Typed archives are transferred intact, including their nested
			 * dependency closure. Scan them before the loose-INI compatibility
			 * path so temporary lobby installs hot-register nested media too. */
			registered = assetCatalogScanExternalLayoutFolderDeferred(
				slot->category, destdir);
		}
		if (registered > 0) {
			distribMarkRegisteredTree(destdir, slot->temporary);
		}

		for (s32 k = 0; !package_transfer && ini_names[k] && !registered; k++) {
            if (!assetPathJoinChecked(inipath, sizeof(inipath), destdir, "/",
                    ini_names[k])) continue;
            if (iniParse(inipath, &ini)) {
                /* audio.ini: preserve legacy registration while mirroring the shared audio parser. */
                if (strcmp(ini_names[k], "audio.ini") == 0) {
                    const char *aname = iniGet(&ini, "name", slot->id);
                    s32 cat = distribParseAudioCategoryValue(
                        iniGet(&ini, "audio_category",
                            iniGet(&ini, "kind",
                            iniGet(&ini, "category", ""))), AUDIO_CAT_SFX);
                    s32 dur = iniGetInt(&ini, "duration_ms", 0);
                    const char *fpath = iniGet(&ini, "file_path", "");
                    char fullfile[FS_MAXPATH];
                    if (fpath[0] && !assetPathJoinChecked(fullfile,
                            sizeof(fullfile), destdir, "/", fpath)) {
                        sysLogPrintf(LOG_WARNING,
                            "DISTRIB.ASSET.PATH.REJECT: audio candidate %s",
                            slot->id);
                        continue;
                    }
                    asset_entry_t prior_entry;
                    const asset_entry_t *prior = assetCatalogResolve(slot->id);
                    if (prior) prior_entry = *prior;
                    asset_entry_t *e = assetCatalogRegisterAudio(
                        slot->id, 0, aname, cat, dur, fpath[0] ? fullfile : "");
                    if (e) {
                        strncpy(e->dirpath, destdir, sizeof(e->dirpath) - 1);
                        e->enabled = 1;
                        e->temporary = slot->temporary;
                        e->bundled = 0;
                        if (!populateExtFromIni(e, ASSET_AUDIO, destdir, &ini,
                                prior ? &prior_entry : NULL)) {
                            if (prior) *e = prior_entry;
                            else assetCatalogUnregister(slot->id);
                            sysLogPrintf(LOG_WARNING,
                                "DISTRIB.ASSET.PATH.REJECT: audio candidate %s",
                                slot->id);
                            continue;
                        }
                        distribRegisterDependencyList(e->id, iniGet(&ini, "deps", ""), e->bundled);
                        sysLogPrintf(LOG_NOTE, "DISTRIB: hot-registered audio '%s' from %s", slot->id, destdir);
                        registered = 1;
                    }
                } else {
                    /* H-2: Resolve asset type from INI filename so the entry is
                     * type-queryable immediately. ASSET_NONE hides the entry from
                     * typed resolvers until the next catalog refresh. */
                    asset_type_e type = iniFilenameToAssetType(ini_names[k]);
                    if (type == ASSET_NONE) {
                        sysLogPrintf(LOG_WARNING,
                            "DISTRIB: no typed registrar for '%s' (id='%s') — "
                            "falling back to ASSET_NONE; entry will not be "
                            "type-resolvable until next catalog refresh",
                            ini_names[k], slot->id);
                    }
                    asset_entry_t preserved_entry;
                    const asset_entry_t *existing = assetCatalogResolve(slot->id);
                    const asset_entry_t *preserved_entry_ptr = NULL;
                    if (existing) {
                        preserved_entry = *existing;
                        preserved_entry_ptr = &preserved_entry;
                    }
                    asset_entry_t *e = assetCatalogRegister(slot->id, type);
                    if (e) {
                        strncpy(e->id, slot->id, sizeof(e->id) - 1);
                        strncpy(e->category, slot->category, sizeof(e->category) - 1);
                        strncpy(e->dirpath, destdir, sizeof(e->dirpath) - 1);
                        e->enabled = 1;
                        e->temporary = slot->temporary;
                        e->bundled = 0;
                        e->model_scale = iniGetFloat(&ini, "model_scale", 1.0f);
                        if (!populateExtFromIni(e, type, destdir, &ini,
                                preserved_entry_ptr)) {
                            if (existing) *e = preserved_entry;
                            else assetCatalogUnregister(slot->id);
                            sysLogPrintf(LOG_WARNING,
                                "DISTRIB.ASSET.PATH.REJECT: candidate %s type=%d",
                                slot->id, (int)type);
                            continue;
                        }
                        distribRegisterDependencyList(e->id, iniGet(&ini, "deps", ""), e->bundled);
                        if (type == ASSET_PROJECTILE && e->ext.projectile.entity_ref[0]) {
                            catalogDepRegister(e->id, e->ext.projectile.entity_ref, e->bundled);
                        }
                        if (type == ASSET_ANIMATION && e->ext.anim.target_body[0]) {
                            catalogDepRegister(e->ext.anim.target_body, e->id, e->bundled);
                        }
                        sysLogPrintf(LOG_NOTE, "DISTRIB: hot-registered '%s' (type=%d) from %s",
                                     slot->id, (int)type, destdir);
                        registered = 1;
                    }
                }
            }
        }

        /* Check for theme.json — hot-register as mod theme (default-enabled
         * policy: first-sight themes auto-apply via pdguiThemeRegisterModDir).
         * This runs in addition to INI registration, not instead of it — a mod
         * can have both an asset INI and a theme.json. */
        {
            char theme_check[FS_MAXPATH];
            struct stat tst;
			if (!package_transfer
					&& assetPathJoinChecked(theme_check, sizeof(theme_check), destdir,
                    "/", "theme.json")
                    && stat(theme_check, &tst) == 0 && S_ISREG(tst.st_mode)) {
                pdguiThemeRegisterModDir(slot->id, theme_check);
                sysLogPrintf(LOG_NOTE, "DISTRIB: hot-registered theme '%s' from %s",
                             slot->id, destdir);
                if (!registered) registered = 1;
            }
        }

        sysLogPrintf(LOG_NOTE,
            "DISTRIB.CATALOG.ADMISSION: id=%s scanner_result=%d",
            slot->id, registered);

        /* A negative scanner result means at least one recognized typed source
         * rejected after another candidate may already have been inspected.
         * Only a strictly positive result admits the published filesystem tree;
         * zero (nothing recognized) and negative (recognized rejection) both
         * roll the PDCA transaction back before pending roots are retried. */
        if (registered <= 0) {
            sysLogPrintf(LOG_WARNING, "DISTRIB: no recognized INI for '%s' -- catalog entry skipped", slot->id);
            s32 rollback_result =
                pdcaExtractTransactionRollback(&install_transaction);
            sysLogPrintf(LOG_WARNING,
                "DISTRIB.CATALOG.ROLLBACK: id=%s result=%d active=%d",
                slot->id, rollback_result, install_transaction.active);
            if (!rollback_result) {
                sysLogPrintf(LOG_ERROR,
                    "DISTRIB: catalog rejection rollback failed for '%s'; recovery required at %s",
                    slot->id, destdir);
            } else {
                sysLogPrintf(LOG_NOTE,
                    "DISTRIB: catalog rejected '%s'; received install rolled back with prior destination preserved",
                    slot->id);
                /* B-1027/B-1043: the scanner restores the complete prior
                 * catalog transaction before this filesystem transaction can
                 * restore its source bytes. Retry roots and rebuild reverse
                 * indexes only after the prior public tree is authoritative. */
                s32 reload_result = catalogReloadInvalidatedTypedAssets();
				catalogLoadInit();
                sysLogPrintf(LOG_WARNING,
                    "DISTRIB.CATALOG.RELOAD: id=%s result=%d",
                    slot->id, reload_result);
                if (reload_result) {
                    sysLogPrintf(LOG_NOTE,
                        "DISTRIB: restored pending catalog roots after rollback for '%s'",
                        slot->id);
                } else {
                    sysLogPrintf(LOG_ERROR,
                        "DISTRIB: prior destination restored but catalog roots remain pending for '%s'",
                        slot->id);
                }
            }
        } else {
            /* Hot registration happens after the boot-time reverse indexes.
             * Rebuild them now so custom animation and sound private slots
             * resolve on the first real weapon use, including temporary
             * ready-gate transfers that do not trigger a mod-manager rebuild. */
            catalogLoadInit();

            pdca_extract_result_t commit_result =
                pdcaExtractTransactionCommit(&install_transaction);
            if (commit_result == PDCA_EXTRACT_OK_BACKUP_RETAINED) {
                sysLogPrintf(LOG_WARNING,
                    "DISTRIB: admitted '%s' but retained recovery backup; future replacement requires review",
                    slot->id);
            }
            s_ClientStatus.received_count++;
            slot_completed = 1;
            if (!slot->temporary) {
                modmgrRescanDirectory();
                sysLogPrintf(LOG_NOTE,
                             "DISTRIB: refreshed mod registry after installing '%s'",
                             slot->id);
            }
        }
    } else {
        sysLogPrintf(LOG_WARNING,
            "DISTRIB: transactional extract failed result=%d destination='%s' preserved",
            (int)extract_result, destdir);
    }

    free(raw);

done:
    if (!slot_completed) {
        s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
    }

    free(slot->compressed_buf);
    memset(slot, 0, sizeof(*slot));

    /* Check if all transfers done */
    s32 any_active = 0;
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active) { any_active = 1; break; }
    }

    if (!any_active) {
		if (s_ClientStatus.state == DISTRIB_CSTATE_ERROR) {
            s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
            sysLogPrintf(LOG_WARNING,
                         "DISTRIB: transfer set failed (%d/%d received)",
                         s_ClientStatus.received_count,
                         s_ClientStatus.missing_count);
            netDistribClientDeclineActiveManifest("distribution failed");
		} else if (s_ClientStatus.received_count <
				s_ClientStatus.missing_count) {
			s_ClientStatus.state = DISTRIB_CSTATE_DIFFING;
			sysLogPrintf(LOG_NOTE,
				"DISTRIB: transfer set awaiting next item (%d/%d received)",
				s_ClientStatus.received_count,
				s_ClientStatus.missing_count);
		} else {
            s_ClientStatus.state = DISTRIB_CSTATE_DONE;
            sysLogPrintf(LOG_NOTE, "DISTRIB: all transfers complete (%d received)",
                         s_ClientStatus.received_count);
            /* Phase D→E bridge: re-check manifest now that transfers are done.
             * This sends CLC_MANIFEST_STATUS(READY) to the server so the
             * ready gate can count this client. */
            if (s_SmokeReceiveActive) {
                sysLogPrintf(LOG_NOTE,
                    "DISTRIB.SMOKE: transfer complete; peer manifest acknowledgement suppressed");
            } else {
                manifestCheck(&g_ClientManifest);
            }
        }
    }
}

s32 netDistribDebugReceivePdcaListForSmoke(const char *list_path)
{
    u32 list_size = 0;
    char *list = (char *)fsFileLoad(list_path, &list_size);
    s32 delivered = 0;
    if (!list || list_size == 0) {
        if (list) sysMemFree(list);
        sysLogPrintf(LOG_WARNING,
            "DISTRIB.SMOKE: could not read receive list '%s'",
            list_path ? list_path : "");
        return 0;
    }

    char *text = (char *)malloc((size_t)list_size + 1);
    if (!text) {
        sysMemFree(list);
        return 0;
    }
    memcpy(text, list, list_size);
    text[list_size] = '\0';
    sysMemFree(list);
    netDistribInit();
    s_SmokeReceiveActive = 1;

    char *line = text;
    while (line && *line) {
        char *next = strpbrk(line, "\r\n");
        if (next) {
            *next++ = '\0';
            while (*next == '\r' || *next == '\n') next++;
        }
        if (line[0] && line[0] != '#') {
            char *path = line;
            char *id = strchr(path, '|');
            char *category = id ? strchr(id + 1, '|') : NULL;
            char *temporary_text = category ? strchr(category + 1, '|') : NULL;
            if (id && category && temporary_text) {
                *id++ = '\0';
                *category++ = '\0';
                *temporary_text++ = '\0';
                u32 raw_size = 0;
                u8 *raw = (u8 *)fsFileLoad(path, &raw_size);
                if (raw && raw_size > 0) {
                    uLongf compressed_cap = compressBound((uLong)raw_size);
                    u8 *compressed = (u8 *)malloc((size_t)compressed_cap);
                    if (compressed && compress2(compressed, &compressed_cap,
                            raw, (uLong)raw_size, Z_BEST_SPEED) == Z_OK) {
                        u8 digest[SHA256_DIGEST_SIZE];
                        sha256Hash(compressed, (u32)compressed_cap, digest);
                        u32 chunks = ((u32)compressed_cap
                            + NET_DISTRIB_CHUNK_SIZE - 1)
                            / NET_DISTRIB_CHUNK_SIZE;
                        netDistribClientHandleBegin(id, category, chunks,
                            raw_size, digest, atoi(temporary_text) ? 1 : 0);
                        for (u32 i = 0; i < chunks; i++) {
                            u32 offset = i * NET_DISTRIB_CHUNK_SIZE;
                            u32 remaining = (u32)compressed_cap - offset;
                            u16 chunk_size = (u16)(remaining
                                < NET_DISTRIB_CHUNK_SIZE ? remaining
                                : NET_DISTRIB_CHUNK_SIZE);
                            netDistribClientHandleChunk(id, (u16)i, 1,
                                compressed + offset, chunk_size);
                        }
                        netDistribClientHandleEnd(id, 1);
                        sysLogPrintf(LOG_NOTE,
                            "DISTRIB.SMOKE: delivered '%s' category='%s' raw=%u compressed=%u",
                            id, category, raw_size, (u32)compressed_cap);
                        delivered++;
                    }
                    free(compressed);
                    sysMemFree(raw);
                }
            }
        }
        line = next;
    }
    s_SmokeReceiveActive = 0;
    free(text);
    return delivered;
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
 * Transfer Approval API
 * ======================================================================== */

s32 netDistribGetPendingApproval(char *name_out, u32 *size_out)
{
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        distrib_recv_slot_t *slot = &s_RecvSlots[i];
        if (slot->active && slot->needs_approval) {
            if (name_out) strncpy(name_out, slot->mod_name, 64);
            if (size_out) *size_out = slot->archive_bytes_pending;
            return i;  /* slot index, acts as approval handle */
        }
    }
    return -1;
}

void netDistribApproveTransfer(s32 slot_idx)
{
    if (slot_idx < 0 || slot_idx >= RECV_SLOTS) return;
    distrib_recv_slot_t *slot = &s_RecvSlots[slot_idx];
    if (!slot->active || !slot->needs_approval) return;

    slot->needs_approval = 0;

    /* Now activate the UI receiving state that was deferred. */
    s_ClientStatus.state = DISTRIB_CSTATE_RECEIVING;
    strncpy(s_ClientStatus.current_id, slot->id, sizeof(s_ClientStatus.current_id) - 1);
    s_ClientStatus.current_bytes_total = slot->archive_bytes;
    s_ClientStatus.current_bytes_received = slot->compressed_len;
    s_ClientStatus.temporary = slot->temporary;

    sysLogPrintf(LOG_NOTE, "DISTRIB: user approved transfer of '%s' (%u bytes)",
                 slot->id, slot->archive_bytes);
}

void netDistribDeclineTransfer(s32 slot_idx)
{
    if (slot_idx < 0 || slot_idx >= RECV_SLOTS) return;
    distrib_recv_slot_t *slot = &s_RecvSlots[slot_idx];
    if (!slot->active || !slot->needs_approval) return;

    sysLogPrintf(LOG_NOTE, "DISTRIB: user declined transfer of '%s' (%u bytes)",
                 slot->id, slot->archive_bytes);

    free(slot->compressed_buf);
    memset(slot, 0, sizeof(*slot));
}

/* ========================================================================
 * Crash Recovery
 * ======================================================================== */

static s32 getCrashStatePath(char *out, s32 maxlen)
{
    const char *modsdir = fsGetModDir();
    char tempdir[FS_MAXPATH];
    return assetPathJoinChecked(tempdir, sizeof(tempdir), modsdir, "/",
        TEMP_SUBDIR) && assetPathJoinChecked(out, (size_t)maxlen, tempdir, "/",
        CRASH_STATE_FILE);
}

s32 netCrashRecoveryCheck(crash_recovery_state_t *out)
{
    if (!out) return CRASH_RECOVERY_NONE;
    memset(out, 0, sizeof(*out));

    /* Check if there are any temp components */
    const char *modsdir = fsGetModDir();
    char tempdir[FS_MAXPATH];
    if (!assetPathJoinChecked(tempdir, sizeof(tempdir), modsdir, "/",
            TEMP_SUBDIR)) return CRASH_RECOVERY_NONE;

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
    if (!getCrashStatePath(statepath, sizeof(statepath))) {
        out->status = CRASH_RECOVERY_NONE;
        return CRASH_RECOVERY_NONE;
    }

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
    if (!assetPathJoinChecked(tempdir, sizeof(tempdir), modsdir, "/",
            TEMP_SUBDIR)) return;

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
                if (!assetPathJoinChecked(markerpath, sizeof(markerpath), tempdir,
                        "/", ".disabled")) return;
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
                        if (!assetPathJoinChecked(catdir, sizeof(catdir), tempdir,
                                "/", ent->d_name)) continue;
                        /* Remove component subdirs */
                        DIR *catd = opendir(catdir);
                        if (catd) {
                            struct dirent *comp;
                            while ((comp = readdir(catd)) != NULL) {
                                if (comp->d_name[0] == '.') continue;
                                char compdir[FS_MAXPATH];
                                if (!assetPathJoinChecked(compdir,
                                        sizeof(compdir), catdir, "/",
                                        comp->d_name)) continue;
                                /* Remove files within component dir */
                                DIR *compd = opendir(compdir);
                                if (compd) {
                                    struct dirent *f;
                                    while ((f = readdir(compd)) != NULL) {
                                        if (f->d_name[0] == '.') continue;
                                        char filepath[FS_MAXPATH];
                                        if (!assetPathJoinChecked(filepath,
                                                sizeof(filepath), compdir, "/",
                                                f->d_name)) continue;
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
    if (!assetPathJoinChecked(tempdir, sizeof(tempdir), modsdir, "/",
            TEMP_SUBDIR)) return;

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
    if (!getCrashStatePath(statepath, sizeof(statepath))) return;

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
    if (!getCrashStatePath(statepath, sizeof(statepath))) return;
    /* Delete crash state on clean exit */
    remove(statepath);
}
