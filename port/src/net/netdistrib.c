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
 *   4. Per frame: server calls netDistribServerTick() which prepares one
 *      component, then queues bounded CHUNK batches on the transfer channel.
 *   5. After last chunk: server sends SVC_DISTRIB_END.
 *   6. Client decompresses, extracts to mods/.temp/ (or mods/ if permanent).
 *   7. Client hot-registers the component in the Asset Catalog.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <zlib.h>
#include <io.h>
#include <windows.h>

#include "types.h"
#include "constants.h"
#include "platform.h"
#include "net/net.h"
#include "net/netbuf.h"
#include "net/netmsg.h"
#include "net/netlobby.h"
#include "net/netdistrib.h"
#include "net/distrib_queue.h"
#include "net/transfer_buffer.h"
#include "net/netenet.h"
#include "net/netmanifest.h"
#include "assetcatalog.h"
#include "lang_source.h"
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
#include "body_head_source_bind.h"
#include "character_head_policy.h"
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
#define RECOVERY_RECEIPT_FILE ".pd2-recovery"
#define RECOVERY_RECEIPT_VERSION 1

/* Default trust threshold in MB — transfers above this require user approval.
 * Configurable via Net.DistribTrustThresholdMB in pd.ini (range 16–4096). */
#define DISTRIB_TRUST_THRESHOLD_DEFAULT_MB  256

/* ========================================================================
 * Server Transfer Queue
 * ======================================================================== */

#define DISTRIB_QUEUE_ASSET    0
#define DISTRIB_QUEUE_PACKAGE  1
#define DISTRIB_PACKAGE_CATEGORY "pdmod"

static distrib_queue s_Queue;
static s32 s_Initialized = 0;

/* ENet retains reliable packet copies until its service/ACK cycle advances.
 * Keep only one compressed component in preparation and cap the number of
 * transfer packets submitted in a frame as well as the peer's unsent command
 * backlog. The 50 MiB component limit also bounds this retained buffer. */
#define DISTRIB_CHUNKS_PER_TICK 4
#define DISTRIB_MAX_OUTGOING_COMMANDS 128
typedef struct distrib_send_stream {
    struct netclient *cl;
    ENetPeer *peer;
    u32 peer_connect_id;
    u8 kind;
    char id[64];
    u8 *compressed;
    u32 compressed_len;
    u32 next_chunk;
    u32 total_chunks;
    s32 active;
} distrib_send_stream_t;
static distrib_send_stream_t s_SendStream;

static void distribClearSendStream(void)
{
    free(s_SendStream.compressed);
    memset(&s_SendStream, 0, sizeof(s_SendStream));
}

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
    u32  compressed_limit;     /* compressBound(declared expanded archive) */
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
/* Expanded archive bytes admitted during this network session. Keep spent
 * reservations after a failed transfer so retries cannot reset the cap. */
static u32 s_SessionArchiveBytesReserved;
/* Smoke ingress drives the real receive/extract/register transaction without
 * a remote peer. Only the final manifest protocol acknowledgement is skipped;
 * every filesystem/catalog/runtime mutation remains production code. */
static s32 s_SmokeReceiveActive;

/* Kill feed ring buffer */
static killfeed_entry_t s_KillFeed[KILLFEED_MAX_ENTRIES];
static s32 s_KillFeedNext = 0;  /* circular write head */

/* Pending diff decision (before user confirms) */
static s32 s_PendingTemporary = 1;

/* Missing content is an admission transaction. No request may reach the host
 * until the player has chosen its persistence policy. */
enum distrib_consent_kind { DISTRIB_CONSENT_NONE, DISTRIB_CONSENT_CATALOG,
                            DISTRIB_CONSENT_MANIFEST };
static enum distrib_consent_kind s_ConsentKind;
static char (*s_ConsentIds)[64];
static u16 s_ConsentCount;
static u16 s_CatalogExpected;
static u16 s_CatalogSeen;
static u32 s_ConsentManifestHash;
static char (*s_ApprovedIds)[64];
static u16 s_ApprovedCount;

static void distribClearConsent(void)
{
    free(s_ConsentIds);
    s_ConsentIds = NULL;
    s_ConsentKind = DISTRIB_CONSENT_NONE;
    s_ConsentCount = s_CatalogExpected = s_CatalogSeen = 0;
    s_ConsentManifestHash = 0;
}

static void distribClearApproved(void)
{
    free(s_ApprovedIds);
    s_ApprovedIds = NULL;
    s_ApprovedCount = 0;
}

static s32 distribIdWasApproved(const char *id)
{
    for (u16 i = 0; i < s_ApprovedCount; i++) {
        if (strcmp(s_ApprovedIds[i], id) == 0) return 1;
    }
    return 0;
}

/* B-1044: mods/.temp is quarantined across process startup. A dirty tree is
 * admitted only after the user chooses Keep and every receive receipt still
 * matches the editable files on disk. */
static crash_recovery_state_t s_RecoveryPendingState;
static s32 s_RecoveryPending;
static s32 s_RecoveryLaunchMarked;
static s32 s_RecoveryPreserveDisabled;

typedef struct distrib_recovery_candidate {
    char dirpath[FS_MAXPATH];
    char category[64];
    char id[64];
    u8 package_sha256[SHA256_DIGEST_SIZE];
    s32 package;
} distrib_recovery_candidate_t;

typedef struct distrib_recovery_member {
    char path[FS_MAXPATH];
    char key[FS_MAXPATH];
    s32 seen;
} distrib_recovery_member_t;

static s32 distribQuarantineAndRemoveTemp(const char *tempdir);
static s32 distribPathIsDirectory(const char *path);
static s32 distribVerifyRecoveryTree(const char *dirpath,
    distrib_recovery_member_t *members, s32 member_count);
static s32 distribMarkRecoveryLaunchingAt(const char *tempdir,
    const char *suspect_id);

/* Configurable trust threshold (MB) — transfers above this need user approval.
 * Bound to Net.DistribTrustThresholdMB in pd.ini. */
static s32 s_TrustThresholdMb = DISTRIB_TRUST_THRESHOLD_DEFAULT_MB;

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

static u16 distribReadLe16(const u8 *p)
{
    return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u32 distribReadLe32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16)
        | ((u32)p[3] << 24);
}

static s32 distribHexToDigest(const char *hex,
        u8 out[SHA256_DIGEST_SIZE])
{
    if (!hex || strlen(hex) != SHA256_HEX_SIZE - 1 || !out) return 0;
    for (s32 i = 0; i < SHA256_DIGEST_SIZE; i++) {
        s32 hi = hex[i * 2];
        s32 lo = hex[i * 2 + 1];
        hi = hi >= '0' && hi <= '9' ? hi - '0'
            : hi >= 'a' && hi <= 'f' ? hi - 'a' + 10 : -1;
        lo = lo >= '0' && lo <= '9' ? lo - '0'
            : lo >= 'a' && lo <= 'f' ? lo - 'a' + 10 : -1;
        if (hi < 0 || lo < 0) return 0;
        out[i] = (u8)((hi << 4) | lo);
    }
    return 1;
}

static s32 distribSyncFile(FILE *fp)
{
    return fp && fflush(fp) == 0 && _commit(_fileno(fp)) == 0;
}

/* Persist the exact public member hashes while the PDCA candidate is still a
 * rollback-capable filesystem transaction. The receipt is hidden metadata,
 * never a parallel authored runtime source. */
static s32 distribWriteRecoveryReceipt(const char *destdir,
        const distrib_recv_slot_t *slot, const u8 *archive, u32 archive_len,
        const u8 *package_sha256)
{
    char receipt_path[FS_MAXPATH];
    char receipt_stage[FS_MAXPATH];
    FILE *fp;
    const u8 *p;
    const u8 *end;
    u16 file_count;
    char package_hex[SHA256_HEX_SIZE] = {0};

    if (!destdir || !slot || !archive || archive_len < 6
            || distribReadLe32(archive) != PDCA_MAGIC
            || !assetPathJoinChecked(receipt_path, sizeof(receipt_path),
                destdir, "/", RECOVERY_RECEIPT_FILE)
            || !assetPathJoinChecked(receipt_stage, sizeof(receipt_stage),
                destdir, "/", ".pd2-recovery.tmp")) return 0;
    file_count = distribReadLe16(archive + 4);
    if (file_count == 0) return 0;
    if (package_sha256) sha256ToHex(package_sha256, package_hex);

    remove(receipt_stage);
    fp = fopen(receipt_stage, "wb");
    if (!fp) return 0;
    if (fprintf(fp,
            "version=%d\nkind=%s\ncategory=%s\nid=%s\npackage_sha256=%s\nmember_count=%u\n",
            RECOVERY_RECEIPT_VERSION,
            strcmp(slot->category, DISTRIB_PACKAGE_CATEGORY) == 0
                ? "package" : "asset",
            slot->category, slot->id, package_hex,
            (unsigned)file_count) < 0) {
        fclose(fp);
        remove(receipt_stage);
        return 0;
    }

    p = archive + 6;
    end = archive + archive_len;
    for (u16 i = 0; i < file_count; i++) {
        u16 path_len;
        u32 bytes_len;
        u8 digest[SHA256_DIGEST_SIZE];
        char digest_hex[SHA256_HEX_SIZE];
        const char *relpath;
        if (p + 2 > end) goto fail;
        path_len = distribReadLe16(p);
        p += 2;
        if (path_len < 2 || p + path_len + 4 > end
                || p[path_len - 1] != '\0') goto fail;
        relpath = (const char *)p;
        p += path_len;
        bytes_len = distribReadLe32(p);
        p += 4;
        if (p + bytes_len > end) goto fail;
        sha256Hash(p, bytes_len, digest);
        sha256ToHex(digest, digest_hex);
        if (fprintf(fp, "member=%s %s\n", digest_hex, relpath) < 0) goto fail;
        p += bytes_len;
    }
    s32 durable = p == end && distribSyncFile(fp);
    if (fclose(fp) != 0) durable = 0;
    if (!durable) {
        remove(receipt_stage);
        return 0;
    }
    if (!MoveFileExA(receipt_stage, receipt_path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(receipt_stage);
        return 0;
    }
    return 1;

fail:
    fclose(fp);
    remove(receipt_stage);
    return 0;
}

static s32 distribValidateRecoveryReceipt(const char *dirpath,
        distrib_recovery_candidate_t *out)
{
    char receipt_path[FS_MAXPATH];
    char line[FS_MAXPATH + SHA256_HEX_SIZE + 32];
    char kind[16] = {0};
    char package_hex[SHA256_HEX_SIZE] = {0};
    s32 version = 0;
    s32 expected_members = -1;
    s32 verified_members = 0;
    s32 saw_version = 0;
    s32 saw_kind = 0;
    s32 saw_category = 0;
    s32 saw_id = 0;
    s32 saw_package_sha = 0;
    s32 saw_member_count = 0;
    distrib_recovery_member_t *members = NULL;
    FILE *fp;

    if (!dirpath || !out || !assetPathJoinChecked(receipt_path,
            sizeof(receipt_path), dirpath, "/", RECOVERY_RECEIPT_FILE)) return 0;
    memset(out, 0, sizeof(*out));
    if (!assetPathCopyChecked(out->dirpath, sizeof(out->dirpath), dirpath)) return 0;
    fp = fopen(receipt_path, "rb");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len == sizeof(line) - 1 && line[len - 1] != '\n' && !feof(fp))
            goto fail;
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
            line[--len] = '\0';
        if (!strncmp(line, "version=", 8)) {
            if (saw_version++) goto fail;
            version = atoi(line + 8);
        }
        else if (!strncmp(line, "kind=", 5)) {
            if (saw_kind++) goto fail;
            if (!assetPathCopyChecked(kind, sizeof(kind), line + 5)) goto fail;
        } else if (!strncmp(line, "category=", 9)) {
            if (saw_category++) goto fail;
            if (!assetPathCopyChecked(out->category, sizeof(out->category),
                    line + 9)) goto fail;
        } else if (!strncmp(line, "id=", 3)) {
            if (saw_id++) goto fail;
            if (!assetPathCopyChecked(out->id, sizeof(out->id), line + 3)) goto fail;
        } else if (!strncmp(line, "package_sha256=", 15)) {
            if (saw_package_sha++) goto fail;
            if (!assetPathCopyChecked(package_hex, sizeof(package_hex),
                    line + 15)) goto fail;
        } else if (!strncmp(line, "member_count=", 13)) {
            if (saw_member_count++) goto fail;
            expected_members = atoi(line + 13);
            if (expected_members <= 0 || expected_members > 65535) goto fail;
            members = (distrib_recovery_member_t *)calloc(
                (size_t)expected_members, sizeof(*members));
            if (!members) goto fail;
        } else if (!strncmp(line, "member=", 7)) {
            const char *value = line + 7;
            const char *relpath;
            char digest_hex[SHA256_HEX_SIZE];
            char fullpath[FS_MAXPATH];
            if (!members || verified_members >= expected_members
                    || strlen(value) <= SHA256_HEX_SIZE - 1
                    || value[SHA256_HEX_SIZE - 1] != ' ') goto fail;
            memcpy(digest_hex, value, SHA256_HEX_SIZE - 1);
            digest_hex[SHA256_HEX_SIZE - 1] = '\0';
            relpath = value + SHA256_HEX_SIZE;
            if (!pdcaNormalizeMemberPath(members[verified_members].path,
                    sizeof(members[verified_members].path),
                    members[verified_members].key,
                    sizeof(members[verified_members].key), relpath)
                    || !assetPathJoinChecked(fullpath, sizeof(fullpath), dirpath,
                        "/", relpath)) goto fail;
            for (s32 i = 0; i < verified_members; i++) {
                if (!strcmp(members[i].key, members[verified_members].key))
                    goto fail;
            }
            if (sha256VerifyFile(fullpath, digest_hex) != 1) goto fail;
            verified_members++;
        } else goto fail;
    }
    fclose(fp);
    fp = NULL;
    if (version != RECOVERY_RECEIPT_VERSION || !saw_version || !saw_kind
            || !saw_category || !saw_id || !saw_package_sha
            || !saw_member_count || !out->category[0] || !out->id[0]
            || expected_members <= 0 || verified_members != expected_members
            || !distribVerifyRecoveryTree(dirpath, members, expected_members)) {
        free(members);
        return 0;
    }
    if (!strcmp(kind, "package")) {
        out->package = 1;
        if (strcmp(out->category, DISTRIB_PACKAGE_CATEGORY) != 0
                || !distribHexToDigest(package_hex, out->package_sha256)) {
            free(members);
            return 0;
        }
    } else if (strcmp(kind, "asset") != 0) {
        free(members);
        return 0;
    }
    free(members);
    return 1;

fail:
    if (fp) fclose(fp);
    free(members);
    return 0;
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
	netSend(cl, &g_NetMsgRel, 1, NETCHAN_TRANSFER);
}

/* ========================================================================
 * Server: Build and Stream Component to Client
 * ======================================================================== */

static void streamComponentToClient(struct netclient *cl, const char *catalog_id,
		u8 kind, s32 temporary)
{
	if (!cl || !cl->peer || !catalog_id || !catalog_id[0]) return;
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
    /* BEGIN, every CHUNK and END share ENet's ordered transfer stream.
     * Control traffic remains independent of bulk retransmission. */
    if (!netSend(cl, &g_NetMsgRel, 1, NETCHAN_TRANSFER)) {
        free(compressed);
        distribSendEnd(cl, id, 0);
        return;
    }

    /* BEGIN has entered the ordered transfer channel. Retain the compressed
     * bytes until later ticks have submitted every CHUNK and END. A peer
     * replacement cannot inherit a half-sent component. */
    s_SendStream.cl = cl;
    s_SendStream.peer = cl->peer;
    s_SendStream.peer_connect_id = cl->peer->connectID;
    s_SendStream.kind = kind;
    strncpy(s_SendStream.id, id, sizeof(s_SendStream.id) - 1);
    s_SendStream.id[sizeof(s_SendStream.id) - 1] = '\0';
    s_SendStream.compressed = compressed;
    s_SendStream.compressed_len = (u32)compressed_len;
    s_SendStream.next_chunk = 0;
    s_SendStream.total_chunks = total_chunks;
    s_SendStream.active = 1;
}

static void distribTickSendStream(void)
{
    if (!s_SendStream.active) return;
    struct netclient *cl = s_SendStream.cl;
    ENetPeer *peer = s_SendStream.peer;
    if (!cl || cl->state < CLSTATE_LOBBY || cl->peer != peer
            || !peer || peer->connectID != s_SendStream.peer_connect_id
            || enet_peer_get_state(peer) != ENET_PEER_STATE_CONNECTED) {
        distribClearSendStream();
        return;
    }

    /* A 16 KiB reliable packet becomes multiple ENet commands. Count the
     * unsent commands, including gameplay/control traffic, before adding
     * more bulk work. ENet's own window limits the in-flight commands. */
    for (u32 sent = 0; sent < DISTRIB_CHUNKS_PER_TICK
            && s_SendStream.next_chunk < s_SendStream.total_chunks; sent++) {
        if (enet_list_size(&peer->outgoingCommands)
                >= DISTRIB_MAX_OUTGOING_COMMANDS) break;
        const u32 i = s_SendStream.next_chunk;
        const u32 offset = i * NET_DISTRIB_CHUNK_SIZE;
        const u32 remaining = s_SendStream.compressed_len - offset;
        const u16 this_len = (u16)(remaining < NET_DISTRIB_CHUNK_SIZE
            ? remaining : NET_DISTRIB_CHUNK_SIZE);
        const u16 id_wire_len = (u16)(strlen(s_SendStream.id) + 1);
        const u32 pkt_len = 1 + sizeof(u16) + id_wire_len + 2 + 1 + 2 + this_len;
        u8 *pkt = (u8 *)malloc(pkt_len);
        if (!pkt) {
            distribSendEnd(cl, s_SendStream.id, 0);
            distribClearSendStream();
            return;
        }
        u8 *p = pkt;
        *p++ = SVC_DISTRIB_CHUNK;
        memcpy(p, &id_wire_len, 2); p += 2;
        memcpy(p, s_SendStream.id, id_wire_len); p += id_wire_len;
        const u16 chunk_index = (u16)i;
        memcpy(p, &chunk_index, 2); p += 2;
        *p++ = NET_DISTRIB_COMP_DEFLATE;
        memcpy(p, &this_len, 2); p += 2;
        memcpy(p, s_SendStream.compressed + offset, this_len);
        const s32 ok = distribSendPacketToPeer(peer, pkt, pkt_len,
            NETCHAN_TRANSFER);
        free(pkt);
        if (!ok) {
            distribSendEnd(cl, s_SendStream.id, 0);
            distribClearSendStream();
            return;
        }
        s_SendStream.next_chunk++;
    }
    if (s_SendStream.next_chunk == s_SendStream.total_chunks) {
        distribSendEnd(cl, s_SendStream.id, 1);
        distribClearSendStream();
    }
}

/* ========================================================================
 * Server Public API
 * ======================================================================== */

void netDistribInit(void)
{
    distribClearSendStream();
    distribClearConsent();
    distribClearApproved();
    distribQueueClear(&s_Queue);
    memset(s_RecvSlots, 0, sizeof(s_RecvSlots));
    memset(&s_ClientStatus, 0, sizeof(s_ClientStatus));
    s_SessionArchiveBytesReserved = 0;
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

static void distribQueueRequest(struct netclient *cl, const char *id,
        u8 kind, u8 temporary)
{
    if (!cl || !cl->peer || cl->state < CLSTATE_LOBBY || !id
            || enet_peer_get_state(cl->peer) != ENET_PEER_STATE_CONNECTED) return;
    /* Retransmission of an admitted request must not restart its stream. */
    if (s_SendStream.active && s_SendStream.cl == cl
            && s_SendStream.peer == cl->peer
            && s_SendStream.peer_connect_id == cl->peer->connectID
            && s_SendStream.kind == kind && strcmp(s_SendStream.id, id) == 0) return;
    distrib_request request = {0};
    request.client = cl;
    request.peer = cl->peer;
    request.connection = cl->peer->connectID;
    request.kind = kind;
    request.temporary = temporary;
    strncpy(request.id, id, sizeof(request.id) - 1);
    if (distribQueuePush(&s_Queue, &request) == DISTRIB_QUEUE_REJECTED) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: transfer queue rejected '%s'", id);
        distribSendEnd(cl, id, 0);
    }
}

void netDistribServerHandleDiff(struct netclient *cl,
                                const char (*missing_ids)[64],
                                u16 count, u8 temporary)
{
    if (!s_Initialized || !cl || !missing_ids || !count) return;
    for (u16 i = 0; i < count; i++)
        distribQueueRequest(cl, missing_ids[i], DISTRIB_QUEUE_ASSET, temporary);
}

void netDistribServerHandleManifestDiff(struct netclient *cl,
		const char (*missing_ids)[64], u16 count, u8 temporary)
{
	if (!s_Initialized || !cl || !missing_ids || !count) {
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

		distribQueueRequest(cl, missing_ids[i], kind, temporary);
	}
}

void netDistribServerTick(void)
{
    if (!s_Initialized || g_NetMode != NETMODE_SERVER) return;

    if (s_SendStream.active) {
        distribTickSendStream();
        return;
    }

    /* FIFO admission prevents a client refilling a free slot from jumping
     * ahead of already accepted requests. Retire stale generations silently. */
    distrib_request request;
    if (distribQueuePop(&s_Queue, &request)) {
        struct netclient *cl = request.client;
        if (cl && cl->state >= CLSTATE_LOBBY && cl->peer
                && distribRequestSameConnection(&request, cl, cl->peer,
                    cl->peer->connectID)
                && enet_peer_get_state(cl->peer) == ENET_PEER_STATE_CONNECTED) {
            streamComponentToClient(cl, request.id, request.kind, request.temporary);
            if (s_SendStream.active) distribTickSendStream();
        }
    }
}

void netDistribServerCancelClient(struct netclient *cl)
{
    if (!cl) return;
    distribQueueRemoveClient(&s_Queue, cl);
    if (s_SendStream.active && s_SendStream.cl == cl) {
        if (cl->peer == s_SendStream.peer && cl->peer
                && cl->peer->connectID == s_SendStream.peer_connect_id
                && enet_peer_get_state(cl->peer) == ENET_PEER_STATE_CONNECTED)
            distribSendEnd(cl, s_SendStream.id, 0);
        distribClearSendStream();
    }
}

void netDistribServerGetClientStatus(s32 client_index,
                                     distrib_server_client_status_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    if (!s_Initialized || client_index < 0 || client_index > NET_MAX_CLIENTS) return;

    const struct netclient *target = &g_NetClients[client_index];
    if (s_SendStream.active && s_SendStream.cl == target) {
        out->queue_remaining = 1;
        strncpy(out->current_id, s_SendStream.id,
            sizeof(out->current_id) - 1);
    }
    for (size_t i = 0; i < s_Queue.count; i++) {
        const distrib_request *request = &s_Queue.entries[i];
        if (target->peer && distribRequestSameConnection(request, target,
                target->peer, target->peer->connectID)) {
            out->queue_remaining++;
            if (!out->current_id[0])
                strncpy(out->current_id, request->id, sizeof(out->current_id) - 1);
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
        distribClearConsent();
        distribClearApproved();
        if (total_count > 4096) {
            s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
            sysLogPrintf(LOG_WARNING, "DISTRIB: catalog offer exceeds 4096 entries");
            return;
        }
        s_CatalogExpected = total_count;
        if (total_count) {
            s_ConsentIds = (char (*)[64])calloc(total_count, sizeof(*s_ConsentIds));
            if (!s_ConsentIds) {
                s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
                return;
            }
        }
        s_ClientStatus.missing_count = 0;
        s_ClientStatus.received_count = 0;
        s_ClientStatus.current_id[0] = '\0';
        s_ClientStatus.current_bytes_received = 0;
        s_ClientStatus.current_bytes_total = 0;
        s_ClientStatus.current_chunks_received = 0;
        s_ClientStatus.current_chunks_total = 0;
    }

    if (batch_offset != s_CatalogSeen || total_count != s_CatalogExpected
            || (u32)batch_offset + count > total_count
            || (total_count && !s_ConsentIds)) {
        s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
        distribClearConsent();
        sysLogPrintf(LOG_WARNING, "DISTRIB: malformed catalog offer batch");
        return;
    }

    for (u16 i = 0; i < count; i++) {
        const asset_entry_t *e = assetCatalogResolve(ids[i]);
        if (!e) {
            strncpy(s_ConsentIds[s_ConsentCount], ids[i], 63);
            s_ConsentIds[s_ConsentCount][63] = '\0';
            s_ConsentCount++;
            sysLogPrintf(LOG_NOTE, "DISTRIB: missing component '%s'", ids[i]);
        }
    }
    s_CatalogSeen = (u16)(batch_offset + count);
    s_ClientStatus.missing_count = s_ConsentCount;

    const s32 final_batch = ((u32)batch_offset + (u32)count >= (u32)total_count);
    if (final_batch && s_ConsentCount == 0) {
        sysLogPrintf(LOG_NOTE, "DISTRIB: local catalog satisfies server requirements");
        s_ClientStatus.state = DISTRIB_CSTATE_IDLE;

        /* Still send an empty diff so server knows we're ready */
        netbufStartWrite(&g_NetMsgRel);
        netmsgClcCatalogDiffWrite(&g_NetMsgRel, NULL, 0, 0);
        netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        distribClearConsent();
        return;
    }

    if (final_batch) {
        s_ConsentKind = DISTRIB_CONSENT_CATALOG;
        s_ClientStatus.state = DISTRIB_CSTATE_DIFFING;
        sysLogPrintf(LOG_NOTE,
                     "DISTRIB: %u missing catalog components await consent",
                     (unsigned)s_ConsentCount);
        if (sysArgCheck("--debug-approve-downloads")) {
            netDistribClientResolveConsent(1, 1);
        }
    }
}

s32 netDistribClientBeginManifestTransferSet(const char (*missing_ids)[64],
                                             u16 missing_count, u32 manifest_hash)
{
	if (!s_Initialized || !missing_ids || missing_count == 0
			|| missing_count > 4096 || s_ConsentKind != DISTRIB_CONSENT_NONE) return 0;
	distribClearConsent();
	distribClearApproved();
	s_ConsentIds = (char (*)[64])calloc(missing_count, sizeof(*s_ConsentIds));
	if (!s_ConsentIds) return 0;
	memcpy(s_ConsentIds, missing_ids, (size_t)missing_count * sizeof(*s_ConsentIds));
	s_ConsentCount = missing_count;
	s_ConsentManifestHash = manifest_hash;
	s_ConsentKind = DISTRIB_CONSENT_MANIFEST;
	s_PendingTemporary = 1;
	s_ClientStatus.missing_count = (s32)missing_count;
	s_ClientStatus.received_count = 0;
	s_ClientStatus.current_id[0] = '\0';
	s_ClientStatus.current_bytes_received = 0;
	s_ClientStatus.current_bytes_total = 0;
	s_ClientStatus.current_chunks_received = 0;
	s_ClientStatus.current_chunks_total = 0;
	s_ClientStatus.temporary = 1;
	s_ClientStatus.state = DISTRIB_CSTATE_DIFFING;
	sysLogPrintf(LOG_NOTE,
		"DISTRIB: manifest transfer set awaiting consent (%u entries)",
		(unsigned)missing_count);
	if (sysArgCheck("--debug-approve-downloads")) {
		netDistribClientResolveConsent(1, 1);
	}
	return 1;
}

s32 netDistribClientConsentPending(void)
{
    return s_ConsentKind != DISTRIB_CONSENT_NONE;
}

s32 netDistribClientResolveConsent(s32 accept, s32 temporary)
{
    enum distrib_consent_kind kind = s_ConsentKind;
    u32 manifest_hash = s_ConsentManifestHash;
    s32 sent_ok = 1;
    if (kind == DISTRIB_CONSENT_NONE || !s_ConsentCount) return 0;
    if (accept) {
        s_PendingTemporary = temporary ? 1 : 0;
        if (kind == DISTRIB_CONSENT_CATALOG) {
            for (u16 offset = 0; offset < s_ConsentCount; offset += 32) {
                u16 batch = (u16)(s_ConsentCount - offset);
                if (batch > 32) batch = 32;
                netbufStartWrite(&g_NetMsgRel);
                netmsgClcCatalogDiffWrite(&g_NetMsgRel,
                    (const char (*)[64])&s_ConsentIds[offset], batch,
                    (u8)s_PendingTemporary);
                if (g_NetMsgRel.error
                        || !netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL)) {
                    sent_ok = 0;
                    break;
                }
            }
        } else {
            netbufStartWrite(&g_NetMsgRel);
            netmsgClcManifestStatusWrite(&g_NetMsgRel, manifest_hash,
                MANIFEST_STATUS_NEED_ASSETS,
                (const char (*)[64])s_ConsentIds, s_ConsentCount);
            if (g_NetMsgRel.error
                    || !netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL)) {
                sent_ok = 0;
            }
        }
        if (!sent_ok) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB: consented request could not be sent; rolling back");
            s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
            distribClearConsent();
            if (kind == DISTRIB_CONSENT_CATALOG) {
                netDisconnect();
            } else {
                netbufStartWrite(&g_NetMsgRel);
                netmsgClcManifestStatusWrite(&g_NetMsgRel, manifest_hash,
                    MANIFEST_STATUS_DECLINE, NULL, 0);
                if (!g_NetMsgRel.error)
                    netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
                if (g_NetLocalClient) g_NetLocalClient->state = CLSTATE_LOBBY;
            }
            return 0;
        }
        sysLogPrintf(LOG_NOTE, "DISTRIB: player approved %u %s entries temporary=%d",
            (unsigned)s_ConsentCount,
            kind == DISTRIB_CONSENT_CATALOG ? "catalog" : "manifest",
            s_PendingTemporary);
        distribClearApproved();
        s_ApprovedIds = s_ConsentIds;
        s_ApprovedCount = s_ConsentCount;
        s_ConsentIds = NULL;
    } else if (kind == DISTRIB_CONSENT_MANIFEST) {
        netbufStartWrite(&g_NetMsgRel);
        netmsgClcManifestStatusWrite(&g_NetMsgRel, manifest_hash,
            MANIFEST_STATUS_DECLINE, NULL, 0);
        if (!g_NetMsgRel.error) netSend(NULL, &g_NetMsgRel, 1, NETCHAN_CONTROL);
        if (g_NetLocalClient) g_NetLocalClient->state = CLSTATE_LOBBY;
        s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
    } else {
        s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
    }
    distribClearConsent();
    if (!accept && kind == DISTRIB_CONSENT_CATALOG) netDisconnect();
    return 1;
}

s32 netDistribClientGetTransferTemporary(void)
{
    return s_PendingTemporary ? 1 : 0;
}

static void netDistribClientDeclineActiveManifest(const char *reason);

static void netDistribClientRejectBegin(const char *reason)
{
    s_ClientStatus.state = DISTRIB_CSTATE_ERROR;
    netDistribClientDeclineActiveManifest(reason);
}

void netDistribClientHandleBegin(const char *catalog_id, const char *category,
                                  u32 total_chunks, u32 archive_bytes,
                                  const u8 expected_sha256[SHA256_DIGEST_SIZE],
                                  s32 temporary)
{
    if (!s_Initialized) return;

    if (!catalog_id || !catalog_id[0] || !category || !category[0]
            || strlen(catalog_id) >= sizeof(s_RecvSlots[0].id)
            || strlen(category) >= sizeof(s_RecvSlots[0].category)) {
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: rejecting BEGIN -- invalid catalog/category identity");
        netDistribClientRejectBegin("invalid transfer identity");
        return;
    }

    if (g_NetMode == NETMODE_CLIENT && !s_SmokeReceiveActive
            && !distribIdWasApproved(catalog_id)) {
        sysLogPrintf(LOG_WARNING,
            "DISTRIB: rejecting unapproved BEGIN '%s'", catalog_id);
        netDistribClientRejectBegin("transfer was not admitted by player");
        return;
    }

    /* The sender limits expanded PDCA archives to this same per-component
     * size. compressBound() is the corresponding upper bound for its zlib
     * compress2 wire stream, including incompressible input. */
    if (archive_bytes == 0 || archive_bytes > NET_DISTRIB_MAX_COMP) {
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: rejecting BEGIN '%s' — expanded archive %u exceeds %u",
                     catalog_id, archive_bytes, (u32)NET_DISTRIB_MAX_COMP);
        netDistribClientRejectBegin("archive exceeds component limit");
        return;
    }
    uLong compressed_bound = compressBound((uLong)archive_bytes);
    u32 max_chunks = (u32)((compressed_bound + NET_DISTRIB_CHUNK_SIZE - 1)
        / NET_DISTRIB_CHUNK_SIZE);
    if (compressed_bound == 0 || compressed_bound > UINT32_MAX
            || total_chunks == 0 || total_chunks > 65535u
            || total_chunks > max_chunks) {
        sysLogPrintf(LOG_WARNING,
            "DISTRIB: rejecting BEGIN '%s' — invalid chunk count %u (max %u)",
            catalog_id, total_chunks, max_chunks);
        netDistribClientRejectBegin("invalid transfer geometry");
        return;
    }

    {
        static const u8 s_zero32[SHA256_DIGEST_SIZE] = {0};
        if (!expected_sha256
                || memcmp(expected_sha256, s_zero32, SHA256_DIGEST_SIZE) == 0) {
            sysLogPrintf(LOG_ERROR,
                         "DISTRIB: rejecting BEGIN '%s' — missing SHA-256 digest",
                         catalog_id);
            netDistribClientRejectBegin("missing transfer digest");
            return;
        }
    }

    /* Reserve expanded bytes across active and completed transfers. The wire
     * telemetry below is compressed bytes and cannot enforce this budget. */
    u32 proposed_reservation = s_SessionArchiveBytesReserved;
    if (!netTransferBudgetReserve(&proposed_reservation, archive_bytes,
            NET_DISTRIB_MAX_COMP, NET_DISTRIB_MAX_SESSION)) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB: rejecting BEGIN '%s' — expanded session cap exceeded "
            "(reserved=%u, incoming=%u, max=%u)",
            catalog_id, s_SessionArchiveBytesReserved, archive_bytes,
            (u32)NET_DISTRIB_MAX_SESSION);
        netDistribClientRejectBegin("archive session budget exhausted");
        return;
    }
    for (s32 i = 0; i < RECV_SLOTS; i++) {
        if (s_RecvSlots[i].active && strcmp(s_RecvSlots[i].id, catalog_id) == 0) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB: rejecting duplicate BEGIN for '%s'", catalog_id);
            netDistribClientRejectBegin("duplicate transfer identity");
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
        netDistribClientRejectBegin("receive slots exhausted");
        return;
    }

    /* Allocate compressed buffer — size unknown yet, start at 64KB */
    u32 initial_cap = compressed_bound < 65536u ? (u32)compressed_bound : 65536u;
    u8 *buf = (u8 *)malloc(initial_cap);
    if (!buf) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: OOM for recv buffer");
        netDistribClientRejectBegin("receive buffer allocation failed");
        return;
    }

    s_SessionArchiveBytesReserved = proposed_reservation;
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
    slot->compressed_limit = (u32)compressed_bound;
    memcpy(slot->expected_sha256, expected_sha256, sizeof(slot->expected_sha256));
    strncpy(slot->id, catalog_id, sizeof(slot->id) - 1);
    strncpy(slot->category, category, sizeof(slot->category) - 1);

    /* Trust threshold check: if size exceeds threshold, set approval flag instead
     * of proceeding silently. The transfer is still staged so chunks can be buffered
     * after the user approves — the UI should call netDistribApproveTransfer(). */
    u64 threshold_bytes = (u64)s_TrustThresholdMb * 1024u * 1024u;
    if ((u64)archive_bytes > threshold_bytes
            && !distribIdWasApproved(catalog_id)) {
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
    s_ClientStatus.current_chunks_total = total_chunks;
    s_ClientStatus.current_chunks_received = 0;
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

    /* Reliable transfer framing must remain ordered. A failed append ends
     * this transfer; never advance a sequence or advertise unallocated space. */
    if (chunk_idx != slot->expected_chunk || chunk_idx >= slot->total_chunks
            || compression != NET_DISTRIB_COMP_DEFLATE
            || data_len == 0 || data_len > NET_DISTRIB_CHUNK_SIZE || !data
            || (chunk_idx + 1u < slot->total_chunks
                && data_len != NET_DISTRIB_CHUNK_SIZE)) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: invalid chunk for '%s' (expected %u, got %u)",
            slot->id, slot->expected_chunk, chunk_idx);
        netDistribClientHandleEnd(catalog_id, 0);
        return;
    }
    if (!netTransferBufferAppend(&slot->compressed_buf, &slot->compressed_cap,
            &slot->compressed_len, data, data_len, slot->compressed_limit, NULL)) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: receive allocation or capacity failure for '%s'", slot->id);
        netDistribClientHandleEnd(catalog_id, 0);
        return;
    }
    slot->expected_chunk++;
    slot->chunks_received++;

    /* Update UI */
    s_ClientStatus.current_bytes_received = slot->compressed_len;
    s_ClientStatus.current_chunks_received = slot->chunks_received;
    s_ClientStatus.session_bytes_total += data_len;

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

    loader_animation_source_batch_t *batch = loaderAnimationSourceBatchCreate();
    s32 accepted = batch && loaderAnimationSourceBatchStage(batch, json, json_size, path, id)
        && loaderAnimationSourceBatchCommit(batch);
    loaderAnimationSourceBatchDestroy(batch);
    if (!accepted) {
        sysLogPrintf(LOG_WARNING,
                     "DISTRIB: animation command source rejected for '%s': %s",
                     id ? id : "", path);
        free(json);
        return 0;
    }

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
        if (!characterSourceApplyIni(e, ini)) {
            e->enabled = 0;
            return 0;
        }
        {
            char dependency_error[256];
            char owner_id[CATALOG_ID_LEN];
            strcpy(owner_id, e->id);
            s32 dependency_result = assetCatalogRegisterCharacterDependencies(e, e->bundled,
                dependency_error, sizeof(dependency_error));
            e = assetCatalogGetMutable(owner_id);
            if (dependency_result < 0 || !e) {
                if (e) e->enabled = 0;
                sysLogPrintf(LOG_WARNING, "DISTRIB: character dependency failure %s: %s",
                    owner_id, dependency_error);
                return 0;
            }
        }
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
    case ASSET_HEAD:
        /* These sources prepare a complete transaction before this generic
         * registrar can clear the owner row. No private metadata hydration. */
        return 0;
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
        /* All received audio uses transactional scanner admission. Never
         * recreate an audio identity through this reset-then-fill fallback. */
        return 0;
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
            const char *count_text = iniGet(ini, "string_count", NULL);
            e->ext.lang.string_count_declared = count_text != NULL;
            e->ext.lang.string_count = 0;
            if (count_text && !langSourceParseCount(count_text, &e->ext.lang.string_count)) {
                /* Preserve invalid presence for the shared activation boundary. */
                e->ext.lang.string_count = UINT32_MAX;
            }
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

#undef distribSetPrimaryFromFile

/* 0 absent, 1 prepared, -1 invalid. A loose root descriptor must be handled
 * before scanning its child .pdmesh files can masquerade as root admission. */
static s32 distribPreparePublicBodyHead(const char *dirpath, const char *id,
    const char *category, s32 temporary, void **admission)
{
    char body_path[FS_MAXPATH], head_path[FS_MAXPATH], error[256];
    if (!admission || !assetPathJoinChecked(body_path, sizeof(body_path), dirpath, "/", "body.ini") ||
            !assetPathJoinChecked(head_path, sizeof(head_path), dirpath, "/", "head.ini")) return -1;
    *admission = NULL;
    s32 body_present = fsFileSize(body_path) >= 0, head_present = fsFileSize(head_path) >= 0;
    if (!body_present && !head_present) return 0;
    if (body_present && head_present) return -1;
    *admission = assetCatalogPrepareBodyHeadDescriptor(head_present ? head_path : body_path,
        id, dirpath, category, head_present, temporary, error, sizeof(error));
    if (!*admission) sysLogPrintf(LOG_WARNING,
        "DISTRIB.BODYHEAD.SOURCE.REJECT: id=%s error=%s", id, error);
    return *admission ? 1 : -1;
}

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

    /* Failure must resolve and release a transfer even while consent is pending. */
    if (!success) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: transfer failed for '%s'", slot->id);
        goto done;
    }
    if (slot->needs_approval) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: END for '%s' blocked — awaiting user approval", catalog_id);
        return;
    }
    if (slot->chunks_received != slot->total_chunks) {
        sysLogPrintf(LOG_WARNING, "DISTRIB: incomplete ordered transfer for '%s'", slot->id);
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
    /* BEGIN already admitted the exact expanded size against both budgets. */
    if (slot->archive_bytes > NET_DISTRIB_MAX_COMP) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: expanded size %u exceeds component cap — rejecting '%s'",
                     slot->archive_bytes, slot->id);
        goto done;
    }
    uLongf raw_len = (uLongf)slot->archive_bytes;
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
    if (raw_len != (uLongf)slot->archive_bytes) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB: expanded size mismatch for '%s' (declared=%u, actual=%lu)",
            slot->id, slot->archive_bytes, (unsigned long)raw_len);
        free(raw);
        goto done;
    }

    /* Build destination directory */
    const char *modsdir = modmgrGetModsDir();
    if (!modsdir || !modsdir[0]) {
        sysLogPrintf(LOG_ERROR, "DISTRIB: no mods directory available for '%s'", slot->id);
        goto done;
    }
    if (slot->temporary && s_RecoveryPreserveDisabled) {
        char disabled_temp[FS_MAXPATH];
        if (!assetPathJoinChecked(disabled_temp, sizeof(disabled_temp), modsdir,
                "/", TEMP_SUBDIR)
                || !distribQuarantineAndRemoveTemp(disabled_temp)) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB.RECOVERY.DISABLED.REJECT: could not retire the prior disabled quarantine");
            goto done;
        }
        s_RecoveryPreserveDisabled = 0;
        sysLogPrintf(LOG_NOTE,
            "DISTRIB.RECOVERY.DISABLED.RETIRED: new temporary session starts from an empty quarantine");
    }
    char destdir[FS_MAXPATH];
    char temp_root[FS_MAXPATH] = {0};
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
    void *body_head_admission = NULL;
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
		const u8 *package_sha = package_transfer
			? distribManifestComponentSha(slot->id) : NULL;
		s32 receipt_ok = !slot->temporary || distribWriteRecoveryReceipt(
			destdir, slot, raw, (u32)raw_len, package_sha);
		s32 registered = 0;
		if (!receipt_ok) {
			sysLogPrintf(LOG_WARNING,
				"DISTRIB.RECOVERY.RECEIPT.REJECT: could not persist verified receipt for '%s'",
				slot->id);
		} else if (package_transfer) {
			registered = package_sha
				? modmgrRegisterSessionFolder(destdir, slot->id, package_sha)
				: 0;
			if (!package_sha) {
				sysLogPrintf(LOG_WARNING,
					"DISTRIB: package '%s' has no component identity in the active manifest",
					slot->id);
			}
		} else {
			/* Typed archives are transferred intact, including their nested
			 * dependency closure. Scan them before the loose-INI compatibility
			 * path so temporary lobby installs hot-register nested media too. */
			registered = distribPreparePublicBodyHead(destdir, slot->id,
				slot->category, slot->temporary, &body_head_admission);
			if (registered == 0) registered = assetCatalogScanExternalLayoutFolderDeferred(
				slot->category, destdir);
		}
		if (registered > 0) {
			distribMarkRegisteredTree(destdir, slot->temporary);
		}

		for (s32 k = 0; receipt_ok && !package_transfer && ini_names[k]
				&& !registered; k++) {
            if (!assetPathJoinChecked(inipath, sizeof(inipath), destdir, "/",
                    ini_names[k])) continue;
            if (iniParse(inipath, &ini)) {
                /* Legacy audio.ini shares typed-source identity/rollback. */
                if (strcmp(ini_names[k], "audio.ini") == 0) {
                    s32 audio_result = assetCatalogRegisterAudioIni(slot->id,
                        slot->category, destdir, &ini, AUDIO_CAT_SFX, 1);
                    if (audio_result > 0) {
                        asset_entry_t *e = assetCatalogGetMutable(slot->id);
                        if (e) {
                            e->enabled = 1;
                            e->temporary = slot->temporary;
                            e->bundled = 0;
                        }
                    }
                    registered = audio_result;
                } else {
                    /* H-2: Resolve asset type from INI filename so the entry is
                     * type-queryable immediately. ASSET_NONE hides the entry from
                     * typed resolvers until the next catalog refresh. */
                    asset_type_e type = iniFilenameToAssetType(ini_names[k]);
                    if (type == ASSET_AUDIO) {
                        /* sound/sfx/voice/music descriptors were already
                         * admitted or rejected by the scanner transaction. */
                        registered = -1;
                        break;
                    }
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
                        s32 populated = populateExtFromIni(e, type, destdir, &ini,
                            preserved_entry_ptr);
                        if (type == ASSET_CHARACTER) {
                            /* The callee's refreshed pointer cannot update this
                             * caller after embedded child registration/rollback. */
                            e = assetCatalogGetMutable(slot->id);
                        }
                        if (!e) {
                            sysLogPrintf(LOG_WARNING,
                                "DISTRIB: catalog parent disappeared during admission: %s", slot->id);
                            continue;
                        }
                        if (!populated) {
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

        sysLogPrintf(LOG_NOTE,
            "DISTRIB.CATALOG.ADMISSION: id=%s scanner_result=%d",
            slot->id, registered);

        if (registered > 0 && slot->temporary
                && !distribMarkRecoveryLaunchingAt(temp_root, slot->id)) {
            sysLogPrintf(LOG_ERROR,
                "DISTRIB.RECOVERY.STATE.REJECT: could not durably mark temporary content '%s' dirty",
                slot->id);
            if (!assetCatalogFinishBodyHeadAdmission(body_head_admission, 0))
                sysLoudFailf("DISTRIB.BODYHEAD.ROLLBACK", "source admission rollback failed: %s", slot->id);
            body_head_admission = NULL;
            (void)modmgrRetireSessionContent();
            registered = 0;
        }

        /* A negative scanner result means at least one recognized typed source
         * rejected after another candidate may already have been inspected.
         * Only a strictly positive result admits the published filesystem tree;
         * zero (nothing recognized) and negative (recognized rejection) both
         * roll the PDCA transaction back before pending roots are retried. */
        if (registered <= 0) {
            if (!assetCatalogFinishBodyHeadAdmission(body_head_admission, 0))
                sysLoudFailf("DISTRIB.BODYHEAD.ROLLBACK", "source admission rollback failed: %s", slot->id);
            body_head_admission = NULL;
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
            if (!assetCatalogFinishBodyHeadAdmission(body_head_admission, commit_result > 0))
                sysLoudFailf("DISTRIB.BODYHEAD.ROLLBACK", "source admission finalization failed: %s", slot->id);
            body_head_admission = NULL;
            if (commit_result == PDCA_EXTRACT_OK_BACKUP_RETAINED) {
                sysLogPrintf(LOG_WARNING,
                    "DISTRIB: admitted '%s' but retained recovery backup; future replacement requires review",
                    slot->id);
            }
            if (package_transfer) {
                /* B-1050: the catalog-ready theme pass is intentionally
                 * one-shot and may have preserved a saved custom ID that did
                 * not exist before this peer-delivered package committed.
                 * Rescan only after catalog initialization and filesystem
                 * publication are authoritative, then retry that saved ID. */
                pdguiThemeRescanMods();
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
    s_ClientStatus.current_chunks_total = slot->total_chunks;
    s_ClientStatus.current_chunks_received = slot->chunks_received;
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

static const char *distribGetModsRoot(void)
{
    const char *modsdir = modmgrGetModsDir();
    return modsdir && modsdir[0] ? modsdir : NULL;
}

static s32 distribGetTempRoot(char *out, size_t out_cap)
{
    const char *modsdir = distribGetModsRoot();
    return modsdir && assetPathJoinChecked(out, out_cap, modsdir, "/",
        TEMP_SUBDIR);
}

static s32 getCrashStatePathAt(const char *tempdir, char *out, size_t out_cap)
{
    return tempdir && tempdir[0] && assetPathJoinChecked(out, out_cap,
        tempdir, "/", CRASH_STATE_FILE);
}

static s32 getCrashStatePath(char *out, size_t out_cap)
{
    char tempdir[FS_MAXPATH];
    return distribGetTempRoot(tempdir, sizeof(tempdir))
        && getCrashStatePathAt(tempdir, out, out_cap);
}

static s32 distribWriteCrashStateAt(const char *tempdir, s32 crash_count,
        const char *suspect, s32 disabled)
{
    char statepath[FS_MAXPATH];
    char stagepath[FS_MAXPATH];
    FILE *fp;
    if (!getCrashStatePathAt(tempdir, statepath, sizeof(statepath))
            || !assetPathJoinChecked(stagepath, sizeof(stagepath),
                statepath, "", ".tmp")) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.WRITE.REJECT: path construction failed");
        return 0;
    }
    remove(stagepath);
    fp = fopen(stagepath, "wb");
    if (!fp) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.WRITE.REJECT: open path=%s errno=%d",
            stagepath, errno);
        return 0;
    }
    s32 wrote = fprintf(fp,
            "[crash_recovery]\nversion=1\ncrash_count=%d\ndisabled=%d\nsuspect=%s\n",
            crash_count, disabled ? 1 : 0, suspect ? suspect : "") >= 0;
    s32 durable = wrote && distribSyncFile(fp);
    if (fclose(fp) != 0) durable = 0;
    if (!durable) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.WRITE.REJECT: sync path=%s errno=%d",
            stagepath, errno);
        remove(stagepath);
        return 0;
    }
    if (!MoveFileExA(stagepath, statepath,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.WRITE.REJECT: publish source=%s destination=%s winerr=%lu",
            stagepath, statepath, (unsigned long)GetLastError());
        remove(stagepath);
        return 0;
    }
    return 1;
}

/* 1 = parsed, 0 = absent, -1 = present but invalid. */
static s32 distribReadCrashStateAt(const char *tempdir, s32 *crash_count,
        char *suspect, size_t suspect_cap, s32 *disabled)
{
    char statepath[FS_MAXPATH];
    char line[256];
    s32 saw_version = 0;
    s32 saw_count = 0;
    s32 saw_disabled = 0;
    s32 saw_suspect = 0;
    s32 version = 0;
    FILE *fp;
    if (!crash_count || !suspect || suspect_cap == 0 || !disabled
            || !getCrashStatePathAt(tempdir, statepath, sizeof(statepath))) return -1;
    *crash_count = 0;
    *disabled = 0;
    suspect[0] = '\0';
    fp = fopen(statepath, "rb");
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
            line[--len] = '\0';
        if (!strcmp(line, "[crash_recovery]")) continue;
        if (!strncmp(line, "version=", 8)) {
            if (saw_version++) goto invalid;
            version = atoi(line + 8);
        } else if (!strncmp(line, "crash_count=", 12)) {
            if (saw_count++) goto invalid;
            *crash_count = atoi(line + 12);
        } else if (!strncmp(line, "disabled=", 9)) {
            if (saw_disabled++) goto invalid;
            *disabled = atoi(line + 9) ? 1 : 0;
        } else if (!strncmp(line, "suspect=", 8)) {
            if (saw_suspect++ || !assetPathCopyChecked(suspect, suspect_cap,
                    line + 8)) goto invalid;
        } else goto invalid;
    }
    fclose(fp);
    return version == 1 && saw_version && saw_count && saw_disabled
        && saw_suspect && *crash_count >= 0 ? 1 : -1;

invalid:
    fclose(fp);
    return -1;
}

static s32 distribCountRecoveryComponents(const char *tempdir)
{
    DIR *categories = opendir(tempdir);
    struct dirent *category_ent;
    s32 count = 0;
    if (!categories) return 0;
    while ((category_ent = readdir(categories)) != NULL) {
        char category_dir[FS_MAXPATH];
        DIR *components;
        struct dirent *component_ent;
        if (category_ent->d_name[0] == '.') continue;
        if (!assetPathJoinChecked(category_dir, sizeof(category_dir), tempdir,
                "/", category_ent->d_name)
                || !distribPathIsDirectory(category_dir)) {
            count++;
            continue;
        }
        components = opendir(category_dir);
        if (!components) {
            count++;
            continue;
        }
        while ((component_ent = readdir(components)) != NULL) {
            if (component_ent->d_name[0] != '.') count++;
        }
        closedir(components);
    }
    closedir(categories);
    return count;
}

static s32 distribPathIsDirectory(const char *path)
{
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES
        && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0
        && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
}

static s32 distribRemoveTree(const char *path)
{
    DWORD attrs;
    if (!path || !path[0]) return 0;
    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND
            || GetLastError() == ERROR_PATH_NOT_FOUND;
    /* Never follow a received junction or symlink while deleting. The exact
     * .temp root is quarantined first, so a rejection leaves only an inert
     * sibling for explicit inspection. */
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) return 0;
    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) return remove(path) == 0;
    DIR *dir = opendir(path);
    if (!dir) return 0;
    s32 ok = 1;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        char child[FS_MAXPATH];
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (!assetPathJoinChecked(child, sizeof(child), path, "/", ent->d_name)
                || !distribRemoveTree(child)) ok = 0;
    }
    closedir(dir);
    return ok && rmdir(path) == 0;
}

static s32 distribRecoveryDirHasMemberPrefix(const char *directory_key,
        const distrib_recovery_member_t *members, s32 member_count)
{
    size_t prefix_len = strlen(directory_key);
    for (s32 i = 0; i < member_count; i++) {
        if (!strncmp(members[i].key, directory_key, prefix_len)
                && members[i].key[prefix_len] == '/') return 1;
    }
    return 0;
}

static s32 distribVerifyRecoveryTreeRecursive(const char *root,
        const char *current, const char *prefix,
        distrib_recovery_member_t *members, s32 member_count)
{
    DIR *dir = opendir(current);
    struct dirent *ent;
    if (!dir) return 0;
    while ((ent = readdir(dir)) != NULL) {
        char child[FS_MAXPATH];
        char relative[FS_MAXPATH];
        char normalized[FS_MAXPATH];
        char key[FS_MAXPATH];
        DWORD attrs;
        s32 match = -1;
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;
        if (!assetPathJoinChecked(child, sizeof(child), current, "/",
                ent->d_name)
                || !(prefix && prefix[0]
                    ? assetPathJoinChecked(relative, sizeof(relative), prefix,
                        "/", ent->d_name)
                    : assetPathCopyChecked(relative, sizeof(relative),
                        ent->d_name))) {
            closedir(dir);
            return 0;
        }
        attrs = GetFileAttributesA(child);
        if (attrs == INVALID_FILE_ATTRIBUTES
                || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
                || !pdcaNormalizeMemberPath(normalized, sizeof(normalized), key,
                    sizeof(key), relative)) {
            closedir(dir);
            return 0;
        }
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            if (!distribRecoveryDirHasMemberPrefix(key, members, member_count)
                    || !distribVerifyRecoveryTreeRecursive(root, child,
                        normalized, members, member_count)) {
                closedir(dir);
                return 0;
            }
            continue;
        }
        if (!strcmp(key, RECOVERY_RECEIPT_FILE)) continue;
        for (s32 i = 0; i < member_count; i++) {
            if (!strcmp(members[i].key, key)) {
                match = i;
                break;
            }
        }
        if (match < 0 || members[match].seen) {
            closedir(dir);
            return 0;
        }
        members[match].seen = 1;
    }
    closedir(dir);
    (void)root;
    return 1;
}

static s32 distribVerifyRecoveryTree(const char *dirpath,
        distrib_recovery_member_t *members, s32 member_count)
{
    if (!dirpath || !members || member_count <= 0
            || !distribPathIsDirectory(dirpath)
            || !distribVerifyRecoveryTreeRecursive(dirpath, dirpath, "",
                members, member_count)) return 0;
    for (s32 i = 0; i < member_count; i++) {
        if (!members[i].seen) return 0;
    }
    return 1;
}

static s32 distribCollectRecoveryCandidates(const char *tempdir,
        distrib_recovery_candidate_t **out_candidates, s32 *out_count)
{
    DIR *categories;
    distrib_recovery_candidate_t *rows = NULL;
    s32 count = 0;
    s32 capacity = 0;
    if (!tempdir || !out_candidates || !out_count) return 0;
    *out_candidates = NULL;
    *out_count = 0;
    categories = opendir(tempdir);
    if (!categories) return 0;

    struct dirent *category_ent;
    while ((category_ent = readdir(categories)) != NULL) {
        char category_dir[FS_MAXPATH];
        DIR *components;
        if (category_ent->d_name[0] == '.') continue;
        if (!assetPathJoinChecked(category_dir, sizeof(category_dir), tempdir,
                "/", category_ent->d_name) || !distribPathIsDirectory(category_dir))
            goto fail;
        components = opendir(category_dir);
        if (!components) goto fail;
        struct dirent *component_ent;
        while ((component_ent = readdir(components)) != NULL) {
            char component_dir[FS_MAXPATH];
            char expected_category[129];
            char expected_id[129];
            distrib_recovery_candidate_t candidate;
            if (component_ent->d_name[0] == '.') continue;
            if (!assetPathJoinChecked(component_dir, sizeof(component_dir),
                    category_dir, "/", component_ent->d_name)
                    || !distribPathIsDirectory(component_dir)
                    || !distribValidateRecoveryReceipt(component_dir, &candidate)
                    || !distribStorageSegment(candidate.category,
                        expected_category, sizeof(expected_category))
                    || !distribStorageSegment(candidate.id, expected_id,
                        sizeof(expected_id))
                    || strcmp(expected_category, category_ent->d_name) != 0
                    || strcmp(expected_id, component_ent->d_name) != 0) {
                closedir(components);
                goto fail;
            }
            if (count == capacity) {
                s32 next_capacity = capacity ? capacity * 2 : 8;
                distrib_recovery_candidate_t *grown =
                    (distrib_recovery_candidate_t *)realloc(rows,
                        (size_t)next_capacity * sizeof(*rows));
                if (!grown) {
                    closedir(components);
                    goto fail;
                }
                rows = grown;
                capacity = next_capacity;
            }
            rows[count++] = candidate;
        }
        closedir(components);
    }
    closedir(categories);
    if (count == 0) {
        free(rows);
        return 0;
    }
    *out_candidates = rows;
    *out_count = count;
    return 1;

fail:
    closedir(categories);
    free(rows);
    return 0;
}

static s32 distribQuarantineAndRemoveTemp(const char *tempdir)
{
    const char *modsdir = distribGetModsRoot();
    char quarantine[FS_MAXPATH];
    if (!tempdir || !modsdir || !modsdir[0] || !distribPathIsDirectory(tempdir))
        return 1;
    for (s32 attempt = 0; attempt < 100; attempt++) {
        char leaf[48];
        snprintf(leaf, sizeof(leaf), ".temp-discard-%02d", attempt);
        if (!assetPathJoinChecked(quarantine, sizeof(quarantine), modsdir, "/",
                leaf)) return 0;
        struct stat st;
        if (stat(quarantine, &st) == 0) continue;
        if (rename(tempdir, quarantine) != 0) return 0;
        if (!distribRemoveTree(quarantine)) {
            sysLogPrintf(LOG_WARNING,
                "DISTRIB.RECOVERY.DISCARD: catalog retired but quarantined cleanup remains at %s",
                quarantine);
        }
        return 1;
    }
    return 0;
}

s32 netCrashRecoveryCheck(crash_recovery_state_t *out)
{
    if (!out) return CRASH_RECOVERY_NONE;
    memset(out, 0, sizeof(*out));

    /* Check if there are any temp components */
    char tempdir[FS_MAXPATH];
    if (!distribGetTempRoot(tempdir, sizeof(tempdir)))
        return CRASH_RECOVERY_NONE;

    if (!distribPathIsDirectory(tempdir)) {
        /* No .temp directory at all — nothing to recover */
        out->status = CRASH_RECOVERY_NONE;
        return CRASH_RECOVERY_NONE;
    }

    s32 component_count = distribCountRecoveryComponents(tempdir);

    if (!component_count) {
        out->status = CRASH_RECOVERY_NONE;
        return CRASH_RECOVERY_NONE;
    }

    out->temp_component_count = component_count;

    s32 crash_count = 0;
    s32 disabled = 0;
    char suspect[64] = "";
    s32 state_result = distribReadCrashStateAt(tempdir, &crash_count, suspect,
        sizeof(suspect), &disabled);
    if (state_result == 0) {
        /* No state file — temp mods exist but no crash data. Clean exit? */
        out->status = CRASH_RECOVERY_CLEAN;
        return CRASH_RECOVERY_CLEAN;
    }
    if (state_result < 0) {
        /* Corrupt recovery metadata never downgrades a dirty tree to clean. */
        crash_count = 1;
        assetPathCopyChecked(suspect, sizeof(suspect), "<invalid-recovery-state>");
    }

    out->crash_count = crash_count;
    strncpy(out->suspect_id, suspect, sizeof(out->suspect_id) - 1);

    if (disabled) {
        out->status = CRASH_RECOVERY_DISABLED;
        return CRASH_RECOVERY_DISABLED;
    }
    if (crash_count > 0) {
        out->status = CRASH_RECOVERY_PROMPT;
        sysLogPrintf(LOG_WARNING, "DISTRIB: crash recovery needed (count=%d, suspect='%s')",
                     crash_count, suspect);
        return CRASH_RECOVERY_PROMPT;
    }

    out->status = CRASH_RECOVERY_CLEAN;
    return CRASH_RECOVERY_CLEAN;
}

s32 netCrashRecoveryApply(s32 action)
{
    char tempdir[FS_MAXPATH];
    if (!distribGetTempRoot(tempdir, sizeof(tempdir))) return 0;

    switch (action) {
        case 0: {
            distrib_recovery_candidate_t *candidates = NULL;
            s32 count = 0;
            s32 admitted = 0;
            char suspect[64] = "";
            sysLogPrintf(LOG_NOTE,
                "DISTRIB.RECOVERY.KEEP: verifying and re-admitting temporary public sources");
            if (!distribCollectRecoveryCandidates(tempdir, &candidates, &count)) {
                sysLogPrintf(LOG_WARNING,
                    "DISTRIB.RECOVERY.KEEP.REJECT: missing, corrupt, or identity-mismatched receipt");
                return 0;
            }
            /* Packages mount first so typed components can resolve any VFS-
             * owned siblings. Both passes use the same verified candidate set. */
            for (s32 pass = 0; pass < 2; pass++) {
                for (s32 i = 0; i < count; i++) {
                    s32 result;
                    if (candidates[i].package != (pass == 0)) continue;
                    if (candidates[i].package) {
                        result = modmgrRegisterSessionFolder(candidates[i].dirpath,
                            candidates[i].id, candidates[i].package_sha256);
                    } else {
                        void *body_head_admission = NULL;
                        result = distribPreparePublicBodyHead(candidates[i].dirpath, candidates[i].id,
                            candidates[i].category, 1, &body_head_admission);
                        if (result == 0) result = assetCatalogScanExternalLayoutFolderDeferred(
                            candidates[i].category, candidates[i].dirpath);
                        if (!assetCatalogFinishBodyHeadAdmission(body_head_admission, result > 0)) result = -1;
                    }
                    if (result <= 0) {
                        sysLogPrintf(LOG_WARNING,
                            "DISTRIB.RECOVERY.KEEP.REJECT: id=%s result=%d",
                            candidates[i].id, result);
                        free(candidates);
                        (void)modmgrRetireSessionContent();
                        return 0;
                    }
                    distribMarkRegisteredTree(candidates[i].dirpath, 1);
                    assetPathCopyChecked(suspect, sizeof(suspect),
                        candidates[i].id);
                    admitted++;
                }
            }
            if (!distribMarkRecoveryLaunchingAt(tempdir, suspect)) {
                sysLogPrintf(LOG_ERROR,
                    "DISTRIB.RECOVERY.KEEP.REJECT: could not persist dirty launch state");
                free(candidates);
                (void)modmgrRetireSessionContent();
                return 0;
            }
            free(candidates);
            catalogLoadInit();
            catalogBuildRuntimeCaches();
            modmgrCatalogChanged();
            s_RecoveryPending = 0;
            s_RecoveryPreserveDisabled = 0;
            sysLogPrintf(LOG_NOTE,
                "DISTRIB.RECOVERY.KEEP.PASS: admitted=%d", admitted);
            return 1;
        }

        case 1:
            sysLogPrintf(LOG_NOTE,
                "DISTRIB.RECOVERY.DISABLE: retiring live temporary closure and preserving quarantined files");
            if (!modmgrRetireSessionContent()) return 0;
            if (!distribWriteCrashStateAt(tempdir, 0,
                    s_RecoveryPendingState.suspect_id, 1)) return 0;
            s_RecoveryPending = 0;
            s_RecoveryLaunchMarked = 0;
            s_RecoveryPreserveDisabled = 1;
            sysLogPrintf(LOG_NOTE,
                "DISTRIB.RECOVERY.DISABLE.PASS: catalog retired files=preserved restart_state=disabled");
            return 1;

        case 2:
            sysLogPrintf(LOG_NOTE,
                "DISTRIB.RECOVERY.DISCARD: retiring temporary closure before filesystem quarantine");
            if (!modmgrRetireSessionContent()
                    || !distribQuarantineAndRemoveTemp(tempdir)) return 0;
            s_RecoveryPending = 0;
            s_RecoveryLaunchMarked = 0;
            s_RecoveryPreserveDisabled = 0;
            sysLogPrintf(LOG_NOTE,
                "DISTRIB.RECOVERY.DISCARD.PASS: catalog retired temp_root=retired");
            return 1;

        default:
            return 0;
    }

    return 0;
}

void netCrashRecoveryStartup(void)
{
    crash_recovery_state_t state;
    s32 status = netCrashRecoveryCheck(&state);
    memset(&s_RecoveryPendingState, 0, sizeof(s_RecoveryPendingState));
    s_RecoveryPending = 0;
    s_RecoveryPreserveDisabled = 0;
    if (status == CRASH_RECOVERY_PROMPT) {
        s_RecoveryPendingState = state;
        s_RecoveryPending = 1;
        sysLogPrintf(LOG_WARNING,
            "DISTRIB.RECOVERY.PROMPT: components=%d crashes=%d suspect=%s",
            state.temp_component_count, state.crash_count,
            state.suspect_id[0] ? state.suspect_id : "<unknown>");
    } else if (status == CRASH_RECOVERY_CLEAN) {
        /* Session-only content surviving a clean process is stale by
         * definition. It was never part of persistent mod selection. */
        (void)netCrashRecoveryApply(2);
    } else if (status == CRASH_RECOVERY_DISABLED) {
        s_RecoveryPendingState = state;
        s_RecoveryPreserveDisabled = 1;
        sysLogPrintf(LOG_NOTE,
            "DISTRIB.RECOVERY.DISABLED: temporary sources remain quarantined and unregistered");
    } else {
        sysLogPrintf(LOG_NOTE,
            "DISTRIB.RECOVERY.NONE: no temporary session content is pending");
    }
}

s32 netCrashRecoveryPending(void)
{
    return s_RecoveryPending;
}

s32 netCrashRecoveryGetPending(crash_recovery_state_t *out)
{
    if (!out || !s_RecoveryPending) return 0;
    *out = s_RecoveryPendingState;
    return 1;
}

static s32 distribMarkRecoveryLaunchingAt(const char *tempdir,
        const char *suspect_id)
{
    if (!tempdir || !tempdir[0]) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.PREFLIGHT.REJECT: authoritative temp root is unavailable");
        return 0;
    }

    DIR *d = opendir(tempdir);
    if (!d) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.PREFLIGHT.REJECT: temp root unavailable path=%s errno=%d",
            tempdir, errno);
        return 0;
    }
    s32 has_temp = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] != '.') { has_temp = 1; break; }
    }
    closedir(d);
    if (!has_temp) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.PREFLIGHT.REJECT: temp root has no component path=%s",
            tempdir);
        return 0;
    }

    s32 crash_count = 0;
    s32 disabled = 0;
    char suspect[64] = "";
    s32 read_result = distribReadCrashStateAt(tempdir, &crash_count, suspect,
        sizeof(suspect), &disabled);
    if (read_result < 0) crash_count = 0;
    if (suspect_id && suspect_id[0]
            && !assetPathCopyChecked(suspect, sizeof(suspect), suspect_id)) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.PREFLIGHT.REJECT: suspect identity exceeds capacity");
        return 0;
    }
    if (!s_RecoveryLaunchMarked) crash_count++;
    if (!distribWriteCrashStateAt(tempdir, crash_count, suspect, 0)) return 0;
    s_RecoveryLaunchMarked = 1;
    s_RecoveryPreserveDisabled = 0;
    sysLogPrintf(LOG_NOTE,
        "DISTRIB.RECOVERY.STATE.DIRTY: crashes=%d suspect=%s",
        crash_count, suspect[0] ? suspect : "<unknown>");
    return 1;
}

s32 netCrashRecoveryMarkLaunching(const char *suspect_id)
{
    char tempdir[FS_MAXPATH];
    if (!distribGetTempRoot(tempdir, sizeof(tempdir))) {
        sysLogPrintf(LOG_ERROR,
            "DISTRIB.RECOVERY.STATE.PREFLIGHT.REJECT: temp path construction failed");
        return 0;
    }
    return distribMarkRecoveryLaunchingAt(tempdir, suspect_id);
}

void netCrashRecoveryMarkClean(void)
{
    if (s_RecoveryPending) {
        /* The user did not choose. Preserve the dirty marker and quarantined
         * files so the same decision is offered next launch. */
        return;
    }
    if (s_RecoveryLaunchMarked && !s_RecoveryPreserveDisabled) {
        if (!netCrashRecoveryApply(2)) return;
    }
    if (s_RecoveryPreserveDisabled) return;
    char statepath[FS_MAXPATH];
    if (!getCrashStatePath(statepath, sizeof(statepath))) return;
    remove(statepath);
    s_RecoveryLaunchMarked = 0;
}
