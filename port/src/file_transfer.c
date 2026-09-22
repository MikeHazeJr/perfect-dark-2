/**
 * file_transfer.c -- chunked + sha256-verified file pipe over signed UDP.
 *
 * Wire format (1320 bytes per frame):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDFTX"
 *    5  1    version (2)
 *    6  1    kind  (0=init, 1=chunk, 2=end, 3=ack, 4=reject)
 *    7  1    reserved
 *    8  4    sender handle
 *   12  4    target handle
 *   16  8    transfer_id (u64)
 *   24  4    chunk_seq    (init/end use 0)
 *   28  4    chunk_total  (init carries it; chunks echo it)
 *   32  8    file_size    (init only)
 *   40 32    file_sha256  (init only)
 *   72 32    file_kind_string (utf-8: "mod" / "music" / "image" / ...
 *                              null-padded; init only)
 *  104 96    original_name (utf-8, null-padded; init only)
 *  200 1024  payload (chunk: file bytes; init/end/ack/reject: zero)
 * 1224 32    sender Ed25519 pubkey
 * 1256 64    Ed25519 signature over bytes[0..1256) || domain
 * 1320
 *
 * Signature covers bytes0..1256 plus domain "pd-ft-v2".
 *
 * Reliability: per-chunk ack. Sender retransmits unacked chunks at
 * 250 ms intervals up to 12 attempts. Receiver buffers chunks in a
 * single in-memory buffer (file_size bytes; capped per-kind) until
 * complete + sha256 verifies, then writes atomically to the inbox.
 *
 * Single-writer hygiene: this module owns every byte of in-flight
 * transfer state. Inbound progress writes to s_Receivers[]; outbound
 * progress to s_Senders[]. No callback into chat.c except on terminal
 * success (chatHistoryAppendAttachment).
 */

#include "file_transfer.h"
#include "file_transfer_storage.h"
#include "save_atomic.h"
#include "presence.h"
#include "net/net_candidate.h"
#include "chat.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "sha256.h"
#include "system.h"
#include "fs.h"
#include "modmgr.h"
#include "modmgr_apply.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#define FT_MAGIC FT_WIRE_MAGIC
#define FT_MAGIC_LEN 5
#define FT_VERSION FT_WIRE_VERSION
#define FT_PUBKEY_OFFSET FT_WIRE_PUBKEY_OFFSET
#define FT_PUBKEY_LEN FT_WIRE_PUBKEY_LEN

#define FT_KIND_INIT     0
#define FT_KIND_CHUNK    1
#define FT_KIND_END      2
#define FT_KIND_ACK      3
#define FT_KIND_REJECT   4

#define FT_MAX_INFLIGHT_SEND 8
#define FT_MAX_INFLIGHT_RECV 8
#define FT_RETRANSMIT_MS     250u
#define FT_MAX_RETRIES       12

/* -------------------------------------------------------------------------
 * Per-kind size limits (Q18 amendment)
 * ------------------------------------------------------------------------- */

static u64 sizeLimitForKind(s32 kind)
{
	switch (kind) {
		case FT_KIND_MOD:    return 250ull * 1024ull * 1024ull;
		case FT_KIND_MUSIC:  return  50ull * 1024ull * 1024ull;
		case FT_KIND_IMAGE:  return  25ull * 1024ull * 1024ull;
		case FT_KIND_SAVE:   return   5ull * 1024ull * 1024ull;
		case FT_KIND_REPLAY: return 100ull * 1024ull * 1024ull;
		default:             return  50ull * 1024ull * 1024ull; /* OTHER */
	}
}

u64 fileTransferKindSizeLimit(s32 kind) { return sizeLimitForKind(kind); }

const char *fileTransferKindName(s32 kind)
{
	switch (kind) {
		case FT_KIND_MOD:    return "mod";
		case FT_KIND_MUSIC:  return "music";
		case FT_KIND_IMAGE:  return "image";
		case FT_KIND_SAVE:   return "save";
		case FT_KIND_REPLAY: return "replay";
		default:             return "file";
	}
}

static const char *kindFolder(s32 kind)
{
	switch (kind) {
		case FT_KIND_MOD:    return "mods";
		case FT_KIND_MUSIC:  return "music";
		case FT_KIND_IMAGE:  return "images";
		case FT_KIND_SAVE:   return "saves";
		case FT_KIND_REPLAY: return "replays";
		default:             return "files";
	}
}

/* Lowercased extension probe: returns FT_KIND_*. */
s32 fileTransferClassifyByExt(const char *filename)
{
	if (!filename) return FT_KIND_OTHER;
	const char *dot = strrchr(filename, '.');
	if (!dot) return FT_KIND_OTHER;
	char ext[16];
	u32 i = 0;
	dot++;
	while (dot[i] && i + 1 < sizeof(ext)) { ext[i] = (char)tolower((unsigned char)dot[i]); i++; }
	ext[i] = '\0';
	if (!strcmp(ext, "pdmod") || !strcmp(ext, "zip")) return FT_KIND_MOD;
	if (!strcmp(ext, "mp3") || !strcmp(ext, "ogg") ||
	    !strcmp(ext, "wav") || !strcmp(ext, "flac")) return FT_KIND_MUSIC;
	if (!strcmp(ext, "png") || !strcmp(ext, "jpg") ||
	    !strcmp(ext, "jpeg") || !strcmp(ext, "bmp")) return FT_KIND_IMAGE;
	if (!strcmp(ext, "sav") || !strcmp(ext, "save")) return FT_KIND_SAVE;
	if (!strcmp(ext, "pdrep") || !strcmp(ext, "replay")) return FT_KIND_REPLAY;
	return FT_KIND_OTHER;
}

/* -------------------------------------------------------------------------
 * In-flight state
 * ------------------------------------------------------------------------- */

typedef struct {
	u8  in_use;
	u32 friend_handle;
	u64 transfer_id;
	u32 chunk_total;
	u32 next_seq;
	u32 last_send_ms;
	u32 attempts;
	u64 file_size;
	s32 kind;
	u8  sha256[SHA256_DIGEST_SIZE];
	char path[400];
	FILE *fp;
	char name[96];
} ft_sender_t;

typedef struct {
	u8  in_use;
	u32 src_handle;
	u64 transfer_id;
	u32 chunk_total;
	u32 received_chunks;
	u32 last_recv_ms;
	u64 file_size;
	s32 kind;
	u8  expected_sha256[SHA256_DIGEST_SIZE];
	u8 request_digest[32];
	u8 *buffer;
	u8 *chunk_present;
	char name[96];
	char inbox_path[600];
} ft_receiver_t;

static ft_sender_t   s_Senders[FT_MAX_INFLIGHT_SEND];
static ft_receiver_t s_Receivers[FT_MAX_INFLIGHT_RECV];
static ft_receipt_store_t s_SavedReceipts;

static u32    s_LocalOwner;
static s32 ftOwnerCurrent(void);

#define FT_PENDING_MOD_ENABLE_MAX 4

typedef struct {
	u8  in_use;
	u32 sender_handle;
	u32 local_handle;
	modmgr_apply_plan_t *plan;
	modmgr_apply_result_t result;
	char mod_id[MODMGR_ID_LEN];
	char mod_name[MODMGR_NAME_LEN];
} ft_pending_mod_enable_t;

static ft_pending_mod_enable_t s_PendingModEnable[FT_PENDING_MOD_ENABLE_MAX];

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static void wU8(u8 **p, u8 v)   { *(*p)++ = v; }
static void wU32(u8 **p, u32 v) {
	(*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); (*p)[2]=(u8)(v>>16); (*p)[3]=(u8)(v>>24); *p+=4;
}
static void wU64(u8 **p, u64 v) {
	for (s32 i = 0; i < 8; i++) (*p)[i] = (u8)(v >> (8*i));
	*p += 8;
}
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}
static u64 rU64(const u8 **p) {
	u64 v = 0; for (s32 i = 0; i < 8; i++) v |= ((u64)((*p)[i])) << (8*i);
	*p += 8; return v;
}

static void sendFrame(const u8 *packet, u32 length)
{
    const u8 *recipient = packet + 12;
    (void)presenceSendFileFrame(rU32(&recipient), packet, length);
}

static const char *inboxRoot(void)
{
	static char path[512];
	if (path[0]) return path;
	char home[400];
	sysGetHomePath(home, sizeof(home));
	snprintf(path, sizeof(path), "%s/social/inbox", home);
	fsCreateDir(path);
	return path;
}

/* The local music-to-mod converter still needs a readable audio leaf. Remote
 * inbox paths use fileTransferStorageNames instead of this helper. */
static void sanitiseFilename(const char *src, char *out, u32 outsize)
{
	if (!out || outsize == 0) return;
	const char *base = src ? src : "received";
	const char *slash = strrchr(base, '/');
	if (slash) base = slash + 1;
	slash = strrchr(base, '\\');
	if (slash) base = slash + 1;

	u32 i = 0;
	while (base[i] && i + 1 < outsize) {
		char c = base[i];
		if (c == '/' || c == '\\' || c == ':' || c == '?' ||
		    c == '*' || c == '<' || c == '>' || c == '|' || c == '"') {
			out[i] = '_';
		} else {
			out[i] = c;
		}
		i++;
	}
	out[i] = '\0';
	if (i == 0) snprintf(out, outsize, "received");
}

static s32 ftEndsWithNoCase(const char *s, const char *suffix)
{
	if (!s || !suffix) return 0;
	const size_t slen = strlen(s);
	const size_t tlen = strlen(suffix);
	if (tlen > slen) return 0;
	const char *tail = s + slen - tlen;
	for (size_t i = 0; i < tlen; i++) {
		if (tolower((unsigned char)tail[i]) !=
		    tolower((unsigned char)suffix[i])) {
			return 0;
		}
	}
	return 1;
}

static s32 ftCharEqPathNoCase(char a, char b)
{
	if (a == '\\') a = '/';
	if (b == '\\') b = '/';
	return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static const char *ftPathLeaf(const char *path)
{
	if (!path) return "";
	const char *leaf = path;
	const char *slash = strrchr(path, '/');
	if (slash && slash + 1 > leaf) leaf = slash + 1;
	slash = strrchr(path, '\\');
	if (slash && slash + 1 > leaf) leaf = slash + 1;
	return leaf;
}

static s32 ftValidateReceivedModArchive(const char *path)
{
	char err[MODMGR_ERROR_LEN];
	if (modmgrValidateArchiveFile(path, err, sizeof(err))) {
		return 1;
	}
	sysLogPrintf(LOG_WARNING,
	             "FT: received mod archive rejected: %s (%s)",
	             err[0] ? err : "invalid .pdmod archive",
	             path ? path : "");
	return 0;
}

static s32 ftFindModIndexById(const char *mod_id)
{
	if (!mod_id || !mod_id[0]) return -1;
	const s32 count = modmgrGetCount();
	for (s32 i = 0; i < count; i++) {
		modinfo_t *mod = modmgrGetMod(i);
		if (mod && strcmp(mod->id, mod_id) == 0) {
			return i;
		}
	}
	return -1;
}

static s32 ftPendingModFirstIndex(void)
{
	if (!ftOwnerCurrent()) return -1;
	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		if (s_PendingModEnable[i].in_use) return i;
	}
	return -1;
}

static void ftPendingModClearIndex(s32 idx)
{
	if (idx < 0 || idx >= FT_PENDING_MOD_ENABLE_MAX) return;
	modmgrFreeApplyPlan(s_PendingModEnable[idx].plan);
	memset(&s_PendingModEnable[idx], 0, sizeof(s_PendingModEnable[idx]));
}

static s32 ftQueuePendingModEnable(u32 sender_handle, const modinfo_t *mod)
{
	if (!ftOwnerCurrent() || !mod || !mod->id[0]) return -1;

	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		if (s_PendingModEnable[i].in_use &&
		    strcmp(s_PendingModEnable[i].mod_id, mod->id) == 0) {
			s_PendingModEnable[i].sender_handle = sender_handle;
			strncpy(s_PendingModEnable[i].mod_name, mod->name,
			        sizeof(s_PendingModEnable[i].mod_name) - 1);
			s_PendingModEnable[i].mod_name[
				sizeof(s_PendingModEnable[i].mod_name) - 1] = '\0';
			return i;
		}
	}

	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		ft_pending_mod_enable_t *p = &s_PendingModEnable[i];
		if (!p->in_use) {
			p->in_use = 1;
			p->local_handle = s_LocalOwner;
			p->sender_handle = sender_handle;
			strncpy(p->mod_id, mod->id, sizeof(p->mod_id) - 1);
			strncpy(p->mod_name, mod->name, sizeof(p->mod_name) - 1);
			p->mod_id[sizeof(p->mod_id) - 1] = '\0';
			p->mod_name[sizeof(p->mod_name) - 1] = '\0';
			sysLogPrintf(LOG_NOTE,
			             "FT: queued enable prompt for received mod '%s' from 0x%08x",
			             p->mod_id, (unsigned)sender_handle);
			return i;
		}
	}

	sysLogPrintf(LOG_WARNING,
	             "FT: received mod '%s' installed disabled; enable prompt queue full",
	             mod->id);
	return -1;
}

s32 fileTransferPendingModEnableCount(void)
{
	if (!ftOwnerCurrent()) return 0;
	s32 count = 0;
	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		if (s_PendingModEnable[i].in_use) count++;
	}
	return count;
}

s32 fileTransferPendingModEnablePeek(char *out_mod_id,
                                     u32 out_mod_id_size,
                                     char *out_mod_name,
                                     u32 out_mod_name_size,
                                     u32 *out_sender_handle)
{
	const s32 idx = ftPendingModFirstIndex();
	if (idx < 0) return 0;
	const ft_pending_mod_enable_t *p = &s_PendingModEnable[idx];
	if (out_mod_id && out_mod_id_size > 0) {
		strncpy(out_mod_id, p->mod_id, out_mod_id_size - 1);
		out_mod_id[out_mod_id_size - 1] = '\0';
	}
	if (out_mod_name && out_mod_name_size > 0) {
		strncpy(out_mod_name, p->mod_name, out_mod_name_size - 1);
		out_mod_name[out_mod_name_size - 1] = '\0';
	}
	if (out_sender_handle) {
		*out_sender_handle = p->sender_handle;
	}
	return 1;
}

static s32 ftPendingModAcceptIndex(s32 idx)
{
	if (!ftOwnerCurrent() || idx < 0 || idx >= FT_PENDING_MOD_ENABLE_MAX) return -1;
	ft_pending_mod_enable_t *p = &s_PendingModEnable[idx];
	if (!p->in_use || p->local_handle != s_LocalOwner) return -1;
	if (!p->plan) {
		const s32 mod_index = ftFindModIndexById(p->mod_id);
		if (mod_index < 0) {
			snprintf(p->result.error, sizeof(p->result.error), "This mod is no longer installed.");
			return -1;
		}
		if (!modmgrPrepareInstalledActivation(mod_index, &p->plan, &p->result)) return -1;
	}
	/* Retry the original persistent plan, including its pre-publication state. */
	if (!modmgrApplyPreparedChanges(p->plan, &p->result)) return -1;
	sysLogPrintf(LOG_NOTE, "FT: received mod '%s' activation completed", p->mod_id);
	ftPendingModClearIndex(idx);
	return 0;
}

s32 fileTransferPendingModEnableAccept(void)
{
	return ftPendingModAcceptIndex(ftPendingModFirstIndex());
}

const char *fileTransferPendingModEnableError(void)
{
	const s32 idx = ftPendingModFirstIndex();
	return idx < 0 ? "" : s_PendingModEnable[idx].result.error;
}

s32 fileTransferPendingModEnableHasEffects(void)
{
	const s32 idx = ftPendingModFirstIndex();
	if (idx < 0) return 0;
	const ft_pending_mod_enable_t *p = &s_PendingModEnable[idx];
	const s32 mod_index = ftFindModIndexById(p->mod_id);
	const modinfo_t *mod = mod_index < 0 ? NULL : modmgrGetMod(mod_index);
	return p->plan != NULL || p->result.activation_published ||
	       p->result.runtime_started || (mod && mod->enabled);
}

void fileTransferPendingModEnableDecline(void)
{
	const s32 idx = ftPendingModFirstIndex();
	if (idx < 0) return;
	sysLogPrintf(LOG_NOTE, "FT: received mod '%s' activation prompt dismissed; no rollback performed",
	             s_PendingModEnable[idx].mod_id);
	ftPendingModClearIndex(idx);
}

static void fileTransferInstallReceivedMod(const char *inbox_path,
                                           const char *original_name,
                                           u32 sender_handle)
{
	if (!ftOwnerCurrent() || !inbox_path || !inbox_path[0]) return;
	if (!ftValidateReceivedModArchive(inbox_path)) return;

	const s32 enable_now = socialFriendByHandle(sender_handle) ? 1 : 0;
	char err[MODMGR_ERROR_LEN];
	const s32 mod_index =
		modmgrInstallArchiveFile(inbox_path, err, sizeof(err));
	if (mod_index < 0) {
		sysLogPrintf(LOG_WARNING,
		             "FT: received mod archive install failed: %s (%s)",
		             err[0] ? err : "install failed",
		             inbox_path);
		return;
	}

	modinfo_t *mod = modmgrGetMod(mod_index);
	if (!mod || !mod->valid) {
		sysLogPrintf(LOG_WARNING,
		             "FT: installed received mod archive is invalid: %s",
		             mod && mod->validation_error[0]
		                 ? mod->validation_error
		                 : inbox_path);
		return;
	}

	sysLogPrintf(LOG_NOTE,
	             "FT: installed received mod archive '%s' through shared .pdmod installer",
	             mod->id);

	const s32 pending_index = ftQueuePendingModEnable(sender_handle, mod);
	if (enable_now && pending_index >= 0) {
		/* Preserve the existing friend-download policy, but retain failed activation. */
		(void)ftPendingModAcceptIndex(pending_index);
	}
	(void)original_name;
}

s32 fileTransferDebugInstallReceivedModForSmoke(const char *inbox_path,
                                                s32 accept_enable_prompt)
{
	if (!inbox_path || !inbox_path[0]) return -1;
	fileTransferInstallReceivedMod(inbox_path, ftPathLeaf(inbox_path), 0);
	if (accept_enable_prompt) {
		return fileTransferPendingModEnableAccept();
	}
	return 0;
}

s32 fileTransferDebugQueueInstalledModForSmoke(const char *mod_id)
{
	if (!ftOwnerCurrent()) return -1;
	const s32 index = ftFindModIndexById(mod_id);
	const modinfo_t *mod = index < 0 ? NULL : modmgrGetMod(index);
	return ftQueuePendingModEnable(0, mod) >= 0 ? 0 : -1;
}

/* Build the destination path: <home>/social/inbox/<kind>/<friend>/<file>.
 * Creates intermediate folders. */
static s32 buildInboxPath(s32 kind, u32 peer, const char *name, const u8 digest[32],
                            char *out, u32 outsize)
{
    char folder[700], peer_folder[16], filename[96];
    if (!fileTransferStorageNames(peer, name, digest, peer_folder, sizeof(peer_folder), filename, sizeof(filename))) return 0;
    int n = snprintf(folder, sizeof(folder), "%s/%s", inboxRoot(), kindFolder(kind));
    if (n < 0 || (size_t)n >= sizeof(folder) || !fsCreateDir(folder)) return 0;
    n = snprintf(folder, sizeof(folder), "%s/%s/%s", inboxRoot(), kindFolder(kind), peer_folder);
    if (n < 0 || (size_t)n >= sizeof(folder) || !fsCreateDir(folder)) return 0;
    n = snprintf(out, outsize, "%s/%s", folder, filename);
    return n >= 0 && (u32)n < outsize;
}

/* -------------------------------------------------------------------------
 * Sender bookkeeping
 * ------------------------------------------------------------------------- */

static ft_sender_t *findSender(u64 transfer_id)
{
	for (s32 i = 0; i < FT_MAX_INFLIGHT_SEND; i++) {
		if (s_Senders[i].in_use && s_Senders[i].transfer_id == transfer_id) {
			return &s_Senders[i];
		}
	}
	return NULL;
}

static ft_sender_t *allocSender(void)
{
	for (s32 i = 0; i < FT_MAX_INFLIGHT_SEND; i++) {
		if (!s_Senders[i].in_use) return &s_Senders[i];
	}
	return NULL;
}

static void freeSender(ft_sender_t *s)
{
	if (!s) return;
	if (s->fp) { fclose(s->fp); s->fp = NULL; }
	memset(s, 0, sizeof(*s));
}

/* Build and send the INIT frame. */
static void sendInit(ft_sender_t *s)
{
	u8 packet[FT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, FT_MAGIC, FT_MAGIC_LEN); w += FT_MAGIC_LEN;
	wU8(&w, FT_VERSION);
	wU8(&w, FT_KIND_INIT);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, s->friend_handle);
	wU64(&w, s->transfer_id);
	wU32(&w, 0);
	wU32(&w, s->chunk_total);
	wU64(&w, s->file_size);
	memcpy(packet + 40, s->sha256, SHA256_DIGEST_SIZE);
	const char *kname = fileTransferKindName(s->kind);
	strncpy((char *)(packet + 72), kname, 31);
	strncpy((char *)(packet + 104), s->name, 95);
	if (!fileTransferWireSign(packet, sizeof(packet), identityGetPubkey(), identitySign)) return;
	sendFrame(packet, sizeof(packet));
}

/* Send the chunk at next_seq. Reads from disk on demand. */
static void sendChunk(ft_sender_t *s)
{
	if (!s->fp) return;
	const u32 seq = s->next_seq;
	if (seq >= s->chunk_total) return;
	if (fseek(s->fp, (long)seq * FT_CHUNK_PAYLOAD, SEEK_SET) != 0) return;

	u8 chunk[FT_CHUNK_PAYLOAD];
	const size_t want = (seq + 1 == s->chunk_total)
	                     ? (size_t)(s->file_size - (u64)seq * FT_CHUNK_PAYLOAD)
	                     : (size_t)FT_CHUNK_PAYLOAD;
	const size_t got = fread(chunk, 1, want, s->fp);
	if (got != want) return;

	u8 packet[FT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, FT_MAGIC, FT_MAGIC_LEN); w += FT_MAGIC_LEN;
	wU8(&w, FT_VERSION);
	wU8(&w, FT_KIND_CHUNK);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, s->friend_handle);
	wU64(&w, s->transfer_id);
	wU32(&w, seq);
	wU32(&w, s->chunk_total);
	memcpy(packet + 200, chunk, got);
	if (!fileTransferWireSign(packet, sizeof(packet), identityGetPubkey(), identitySign)) return;
	sendFrame(packet, sizeof(packet));
}

static void sendEnd(ft_sender_t *s)
{
	u8 packet[FT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, FT_MAGIC, FT_MAGIC_LEN); w += FT_MAGIC_LEN;
	wU8(&w, FT_VERSION);
	wU8(&w, FT_KIND_END);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, s->friend_handle);
	wU64(&w, s->transfer_id);
	wU32(&w, 0);
	wU32(&w, s->chunk_total);
	if (!fileTransferWireSign(packet, sizeof(packet), identityGetPubkey(), identitySign)) return;
	sendFrame(packet, sizeof(packet));
}

/* -------------------------------------------------------------------------
 * Receiver bookkeeping
 * ------------------------------------------------------------------------- */

static ft_receiver_t *findReceiver(u64 transfer_id, u32 src_handle)
{
	for (s32 i = 0; i < FT_MAX_INFLIGHT_RECV; i++) {
		if (s_Receivers[i].in_use &&
		    s_Receivers[i].transfer_id == transfer_id &&
		    s_Receivers[i].src_handle == src_handle) {
			return &s_Receivers[i];
		}
	}
	return NULL;
}

static ft_receiver_t *allocReceiver(void)
{
	for (s32 i = 0; i < FT_MAX_INFLIGHT_RECV; i++) {
		if (!s_Receivers[i].in_use) return &s_Receivers[i];
	}
	return NULL;
}

static void freeReceiver(ft_receiver_t *r)
{
	if (!r) return;
	if (r->buffer) { free(r->buffer); r->buffer = NULL; }
	if (r->chunk_present) { free(r->chunk_present); r->chunk_present = NULL; }
	memset(r, 0, sizeof(*r));
}

/* Send an ACK back to the sender for a specific chunk_seq. */
static void sendControl(u8 control_kind, u32 dst_handle, u64 transfer_id, u32 chunk_seq)
{
	u8 packet[FT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, FT_MAGIC, FT_MAGIC_LEN); w += FT_MAGIC_LEN;
	wU8(&w, FT_VERSION);
	wU8(&w, control_kind);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, dst_handle);
	wU64(&w, transfer_id);
	wU32(&w, chunk_seq);
	wU32(&w, 0);
	if (!fileTransferWireSign(packet, sizeof(packet), identityGetPubkey(), identitySign)) return;
	sendFrame(packet, sizeof(packet));
}

static void sendAck(u32 peer, u64 id, u32 sequence) { sendControl(FT_KIND_ACK, peer, id, sequence); }
static void sendReject(u32 peer, u64 id) { sendControl(FT_KIND_REJECT, peer, id, 0); }

static void writeJsonString(FILE *stream, const char *value)
{
    fputc('"', stream);
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', stream); fputc(*p, stream); }
        else if (*p < 32) fprintf(stream, "\\u%04x", (unsigned)*p);
        else fputc(*p, stream);
    }
    fputc('"', stream);
}

/* -------------------------------------------------------------------------
 * Sidecar metadata write
 * ------------------------------------------------------------------------- */

static void writeSidecar(const ft_receiver_t *r)
{
    char sidecar[700];
    int n = snprintf(sidecar, sizeof(sidecar), "%s.meta.json", r->inbox_path);
    if (n < 0 || (size_t)n >= sizeof(sidecar)) return;
    save_atomic_file_t transaction;
    if (saveAtomicBegin(&transaction, sidecar) != 0) return;
    FILE *f = saveAtomicStream(&transaction);
    const social_friend_t *friend = socialFriendByHandle(r->src_handle);
    char hex[SHA256_HEX_SIZE]; sha256ToHex(r->expected_sha256, hex);
    fprintf(f, "{\n  \"sender_handle\": %u,\n  \"sender_agent\": ", (unsigned)r->src_handle);
    writeJsonString(f, friend ? friend->agent_name : "anonymous");
    fprintf(f, ",\n  \"received_at\": %lld,\n  \"original_name\": ", (long long)time(NULL));
    writeJsonString(f, r->name);
    fprintf(f, ",\n  \"sha256\": \"%s\",\n  \"size_bytes\": %llu,\n  \"kind\": ", hex, (unsigned long long)r->file_size);
    writeJsonString(f, fileTransferKindName(r->kind));
    fputs("\n}\n", f);
    if (saveAtomicCommit(&transaction) != 0)
        sysLogPrintf(LOG_WARNING, "FT: verified data saved but metadata save failed");
}

/* -------------------------------------------------------------------------
 * Drain receive
 * ------------------------------------------------------------------------- */

static void completeReceiver(ft_receiver_t *r)
{
    if (r->received_chunks != r->chunk_total) return;
    ft_receipt_key_t key = {0};
    key.local = socialMyHandle(); key.peer = r->src_handle; key.id = r->transfer_id;
    key.bytes = r->file_size; key.chunks = r->chunk_total;
    memcpy(key.digest, r->expected_sha256, 32); memcpy(key.request_digest, r->request_digest, 32);
    char error[192];
    if (!fileTransferStoreCommit(&s_SavedReceipts, &key, r->inbox_path, r->buffer,
            (size_t)r->file_size, SDL_GetTicks(), error, sizeof(error))) {
        sysLogPrintf(LOG_WARNING, "FT: receiver rejected transfer %llu: %s", (unsigned long long)r->transfer_id, error);
        sendReject(r->src_handle, r->transfer_id); freeReceiver(r); return;
    }
	writeSidecar(r);
    sendAck(r->src_handle, r->transfer_id, r->chunk_total - 1);

	if (r->kind == FT_KIND_MOD) {
		fileTransferInstallReceivedMod(r->inbox_path, r->name, r->src_handle);
	}

	(void)chatHistoryAppendAttachment(r->src_handle, (u32)r->kind, r->name,
	                                   r->inbox_path, r->file_size);

	sysLogPrintf(LOG_NOTE,
	             "FT: complete <- 0x%08x kind=%s size=%llu path=%s",
	             (unsigned)r->src_handle, fileTransferKindName(r->kind),
	             (unsigned long long)r->file_size, r->inbox_path);

	freeReceiver(r);
}

static void handleInit(const u8 *body)
{
	const u8 *p = body + FT_MAGIC_LEN + 1 + 1 + 1; /* magic+ver+kind+rsv */
	u32 src_handle = rU32(&p);
	u32 dst_handle = rU32(&p);
	u64 tid        = rU64(&p);
	(void)rU32(&p); /* chunk_seq */
	u32 chunk_total = rU32(&p);
	u64 file_size   = rU64(&p);

	if (dst_handle != socialMyHandle()) return;
	if (chunk_total == 0 || file_size == 0) return;

	const u8 *sha = body + 40;
	const char *kindstr = (const char *)(body + 72);
	const char *origname = (const char *)(body + 104);

	s32 kind = FT_KIND_OTHER;
	if (!strncmp(kindstr, "mod", 31))    kind = FT_KIND_MOD;
	else if (!strncmp(kindstr, "music", 31)) kind = FT_KIND_MUSIC;
	else if (!strncmp(kindstr, "image", 31)) kind = FT_KIND_IMAGE;
	else if (!strncmp(kindstr, "save", 31))  kind = FT_KIND_SAVE;
	else if (!strncmp(kindstr, "replay", 31)) kind = FT_KIND_REPLAY;

	if (!fileTransferWireGeometry(file_size, chunk_total, sizeLimitForKind(kind))) {
		sysLogPrintf(LOG_WARNING,
		             "FT: refusing invalid size/chunk geometry for %s (%llu bytes; limit %llu) from 0x%08x",
		             fileTransferKindName(kind),
		             (unsigned long long)file_size,
		             (unsigned long long)sizeLimitForKind(kind),
		             (unsigned)src_handle);
		return;
	}

    ft_receipt_key_t key = {0};
    key.local = socialMyHandle(); key.peer = src_handle; key.id = tid;
    key.bytes = file_size; key.chunks = chunk_total; memcpy(key.digest, sha, 32);
    sha256Hash(body, FT_WIRE_PAYLOAD_OFFSET, key.request_digest);
    const ft_saved_receipt_t *saved = fileTransferReceiptFind(&s_SavedReceipts, key.local, src_handle, tid, SDL_GetTicks());
    if (saved) {
        if (fileTransferReceiptMatchesInit(saved, &key)) sendAck(src_handle, tid, chunk_total - 1);
        else sendReject(src_handle, tid);
        return;
    }
	ft_receiver_t *r = findReceiver(tid, src_handle);
	if (r) {
        if (r->file_size != file_size || r->chunk_total != chunk_total ||
                r->kind != kind || memcmp(r->expected_sha256, sha, SHA256_DIGEST_SIZE) ||
                memcmp(r->request_digest, key.request_digest, 32)) return;
		r->last_recv_ms = SDL_GetTicks();
        sendAck(src_handle, tid, 0xFFFFFFFFu);
		return;
	}
	r = allocReceiver();
	if (!r) {
		sysLogPrintf(LOG_WARNING, "FT: receiver table full");
		return;
	}
	memset(r, 0, sizeof(*r));
	r->in_use = 1;
	r->src_handle = src_handle;
	r->transfer_id = tid;
	r->chunk_total = chunk_total;
	r->received_chunks = 0;
	r->last_recv_ms = SDL_GetTicks();
	r->file_size = file_size;
	r->kind = kind;
	memcpy(r->expected_sha256, sha, SHA256_DIGEST_SIZE);
    memcpy(r->request_digest, key.request_digest, 32);
	strncpy(r->name, origname, sizeof(r->name) - 1);

	r->buffer = (u8 *)malloc((size_t)file_size);
	r->chunk_present = (u8 *)calloc(chunk_total, 1);
	if (!r->buffer || !r->chunk_present) {
		freeReceiver(r);
		return;
	}

    if (!buildInboxPath(kind, src_handle, r->name, r->expected_sha256, r->inbox_path, sizeof(r->inbox_path))) {
        sendReject(src_handle, tid); freeReceiver(r); return;
    }

	sysLogPrintf(LOG_NOTE,
	             "FT: init <- 0x%08x kind=%s size=%llu chunks=%u name=\"%s\"",
	             (unsigned)src_handle, fileTransferKindName(kind),
	             (unsigned long long)file_size, (unsigned)chunk_total, r->name);

	/* Ack the init so the sender knows we are listening. */
	sendAck(src_handle, tid, 0xFFFFFFFFu);
}

static void handleChunk(const u8 *body)
{
	const u8 *p = body + FT_MAGIC_LEN + 1 + 1 + 1;
	u32 src_handle = rU32(&p);
	u32 dst_handle = rU32(&p);
	u64 tid        = rU64(&p);
	u32 chunk_seq  = rU32(&p);
	u32 chunk_total = rU32(&p);
	if (dst_handle != socialMyHandle()) return;

	ft_receiver_t *r = findReceiver(tid, src_handle);
    if (!r) {
        const ft_saved_receipt_t *saved = fileTransferReceiptFind(&s_SavedReceipts, socialMyHandle(), src_handle, tid, SDL_GetTicks());
        if (fileTransferReceiptMatchesChunk(saved, chunk_total, chunk_seq, body + FT_WIRE_PAYLOAD_OFFSET, FT_CHUNK_PAYLOAD))
            sendAck(src_handle, tid, chunk_seq);
        return;
    }
    u64 off; u32 cap;
    if (!fileTransferWireChunkRange(r->file_size, r->chunk_total, chunk_seq, chunk_total, &off, &cap)) return;
	r->last_recv_ms = SDL_GetTicks();
	if (r->chunk_present[chunk_seq]) {
		if (chunk_seq + 1 < r->chunk_total) sendAck(src_handle, tid, chunk_seq);
		return;
	}
	memcpy(r->buffer + off, body + 200, cap);
	r->chunk_present[chunk_seq] = 1;
	r->received_chunks++;

    if (r->received_chunks == r->chunk_total) completeReceiver(r);
    else if (chunk_seq + 1 < r->chunk_total) sendAck(src_handle, tid, chunk_seq);
}

static void handleAck(const u8 *body)
{
	const u8 *p = body + FT_MAGIC_LEN + 1 + 1 + 1;
	(void)rU32(&p);
	const u32 dst_handle = rU32(&p);
	const u64 tid = rU64(&p);
	const u32 chunk_seq = rU32(&p);
	if (dst_handle != socialMyHandle()) return;
	ft_sender_t *s = findSender(tid);
	if (!s || !fileTransferWirePeerMatches(body, FT_FRAME_LEN, socialMyHandle(), s->friend_handle, s->transfer_id)) return;
    u32 advanced;
    if (!fileTransferWireAckProgress(s->next_seq, s->chunk_total, chunk_seq, &advanced)) return;
    s->next_seq = advanced;
    if (s->next_seq == s->chunk_total) {
        sendEnd(s);
        sysLogPrintf(LOG_NOTE, "FT: receiver verified and saved -> 0x%08x tid=%llu",
            (unsigned)s->friend_handle, (unsigned long long)tid);
        freeSender(s); return;
    }
    /* ACK arrival advances immediately; 250ms is retransmission delay only. */
    sendChunk(s);
    s->last_send_ms = SDL_GetTicks();
    s->attempts = 1;
}

void fileTransferReceiveFrame(const u8 *packet, u32 length)
{
    if (!ftOwnerCurrent() || !fileTransferWireHeaderValid(packet, length, socialMyHandle())) return;
    const u8 *p = packet + 8;
    const u32 src_handle = rU32(&p);
    const u8 *sender_pub = packet + FT_PUBKEY_OFFSET;
    if (!socialFriendByHandle(src_handle) || socialBlockIsHandle(src_handle) ||
            !socialHandleBindsPubkey(src_handle, sender_pub) ||
            !fileTransferWireVerify(packet, length, socialMyHandle()) ||
            socialFriendBindPubkey(src_handle, sender_pub) < 0) return;
    switch (packet[6]) {
        case FT_KIND_INIT: handleInit(packet); break;
        case FT_KIND_CHUNK: handleChunk(packet); break;
        case FT_KIND_END: break; /* Current receiver completes after all chunks. */
        case FT_KIND_ACK: handleAck(packet); break;
        case FT_KIND_REJECT: {
            const u8 *q = packet + 16;
            ft_sender_t *sender = findSender(rU64(&q));
            if (sender && fileTransferWirePeerMatches(packet, length, socialMyHandle(),
                    sender->friend_handle, sender->transfer_id)) {
                sysLogPrintf(LOG_WARNING, "FT: peer rejected transfer %llu", (unsigned long long)sender->transfer_id);
                freeSender(sender);
            }
            break;
        }
    }
}

/* -------------------------------------------------------------------------
 * Sender progression
 * ------------------------------------------------------------------------- */

static void senderTick(void)
{
	const u32 now = SDL_GetTicks();
	for (s32 i = 0; i < FT_MAX_INFLIGHT_SEND; i++) {
		ft_sender_t *s = &s_Senders[i];
		if (!s->in_use) continue;
		if (now - s->last_send_ms < FT_RETRANSMIT_MS) continue;

		if (s->next_seq == 0xFFFFFFFFu) {
			/* still waiting for INIT ack */
			s->attempts++;
			if (s->attempts > FT_MAX_RETRIES) {
				sysLogPrintf(LOG_WARNING, "FT: send abandoned (no init ack) -> 0x%08x",
				             (unsigned)s->friend_handle);
				freeSender(s);
				continue;
			}
			sendInit(s);
			s->last_send_ms = now;
			continue;
		}

		if (s->next_seq < s->chunk_total) {
			s->attempts++;
			if (s->attempts > FT_MAX_RETRIES) {
				sysLogPrintf(LOG_WARNING, "FT: send abandoned (no chunk ack) -> 0x%08x seq=%u",
				             (unsigned)s->friend_handle, (unsigned)s->next_seq);
				freeSender(s);
				continue;
			}
			sendChunk(s);
			s->last_send_ms = now;
		}
	}
}

/* -------------------------------------------------------------------------
 * Public lifecycle
 * ------------------------------------------------------------------------- */

void fileTransferInit(void)
{
	fileTransferShutdown();
	s_LocalOwner = socialMyHandle();
	memset(s_Senders, 0, sizeof(s_Senders));
	memset(s_Receivers, 0, sizeof(s_Receivers));
	(void)inboxRoot();
}

void fileTransferShutdown(void)
{
    memset(&s_SavedReceipts, 0, sizeof(s_SavedReceipts));
	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) ftPendingModClearIndex(i);
	s_LocalOwner = 0;
	for (s32 i = 0; i < FT_MAX_INFLIGHT_SEND; i++) freeSender(&s_Senders[i]);
	for (s32 i = 0; i < FT_MAX_INFLIGHT_RECV; i++) freeReceiver(&s_Receivers[i]);
}

static s32 ftOwnerCurrent(void)
{
	const u32 current = socialMyHandle();
	if (current != s_LocalOwner) {
		/* No pending plan or signed transfer may cross an Agent boundary. */
		fileTransferShutdown();
		s_LocalOwner = current;
	}
	return current != 0;
}

void fileTransferTick(void)
{
	if (!ftOwnerCurrent()) return;
	senderTick();

	/* Prune stale receivers after 30s of silence. */
	const u32 now = SDL_GetTicks();
	for (s32 i = 0; i < FT_MAX_INFLIGHT_RECV; i++) {
		if (s_Receivers[i].in_use && (now - s_Receivers[i].last_recv_ms) > 30000u) {
			sysLogPrintf(LOG_WARNING, "FT: receiver timed out tid=%llu",
			             (unsigned long long)s_Receivers[i].transfer_id);
			freeReceiver(&s_Receivers[i]);
		}
	}
}

s32 fileTransferSendFile(u32 friend_handle, const char *local_path)
{
	if (!ftOwnerCurrent()) return -1;
	if (friend_handle == 0 || !local_path) return -1;
	if (socialBlockIsHandle(friend_handle)) return -1;
	if (!socialFriendByHandle(friend_handle)) return -1;

    if (!presenceFileTransportReady()) return -1;

	FILE *fp = fopen(local_path, "rb");
	if (!fp) {
		sysLogPrintf(LOG_WARNING, "FT: cannot open %s", local_path);
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz <= 0) { fclose(fp); return -1; }

	const s32 kind = fileTransferClassifyByExt(local_path);
	if ((u64)sz > sizeLimitForKind(kind)) {
		fclose(fp);
		sysLogPrintf(LOG_WARNING, "FT: refusing oversize local file %s (%ld bytes)",
		             local_path, sz);
		return -1;
	}

	u64 transfer_id = 0;
	if (!netCandidateRandomBytes((u8 *)&transfer_id, sizeof(transfer_id)) ||
	        !transfer_id || findSender(transfer_id)) { fclose(fp); return -1; }
	ft_sender_t *s = allocSender();
	if (!s) { fclose(fp); return -1; }
	memset(s, 0, sizeof(*s));
	s->in_use = 1;
	s->friend_handle = friend_handle;
	s->transfer_id = transfer_id;
	s->file_size = (u64)sz;
	s->chunk_total = (u32)((s->file_size + FT_CHUNK_PAYLOAD - 1) / FT_CHUNK_PAYLOAD);
	s->next_seq = 0xFFFFFFFFu; /* awaiting INIT ack */
	s->last_send_ms = 0;
	s->attempts = 0;
	s->kind = kind;
	s->fp = fp;

	/* Compute sha256 of the whole file. The file is small enough by
	 * cap that a single pass is reasonable; we re-rewind for chunk
	 * reads. */
	if (sha256HashFile(local_path, s->sha256) != 0) {
		freeSender(s);
		return -1;
	}

	const char *base = strrchr(local_path, '/');
	if (!base) base = strrchr(local_path, '\\');
	base = base ? base + 1 : local_path;
	strncpy(s->name, base, sizeof(s->name) - 1);
	strncpy(s->path, local_path, sizeof(s->path) - 1);

	sendInit(s);
	s->last_send_ms = SDL_GetTicks();
	s->attempts = 1;

	sysLogPrintf(LOG_NOTE,
	             "FT: send -> 0x%08x kind=%s size=%llu chunks=%u file=\"%s\"",
	             (unsigned)friend_handle, fileTransferKindName(kind),
	             (unsigned long long)s->file_size,
	             (unsigned)s->chunk_total, s->name);
	return 0;
}

/* -------------------------------------------------------------------------
 * Convert-to-mod (Phase 2.D piggybacks here)
 *
 * Phase 1 of the .pdmod migration (see pdmod-unified-mod-format.md M-1
 * dual-support) accepts loose folder layout. Until Priority M lands the
 * archive form, we emit a folder mod under
 * <home>/mods/installed/<id>/ with a mod.json + audio/<safe>.ext.
 * The mod manager re-scans on disk change; the caller should kick a
 * scan after this returns. The returned path is the directory.
 * ------------------------------------------------------------------------- */

static void appendCopy(const char *src, const char *dst)
{
	FILE *f1 = fopen(src, "rb"); if (!f1) return;
	FILE *f2 = fopen(dst, "wb"); if (!f2) { fclose(f1); return; }
	char buf[8192]; size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f1)) > 0) fwrite(buf, 1, n, f2);
	fclose(f1); fclose(f2);
}

static void slugify(const char *src, char *out, u32 outsize)
{
	u32 i = 0;
	while (src[i] && i + 1 < outsize) {
		char c = src[i];
		if ((c >= 'A' && c <= 'Z')) c = (char)(c + ('a' - 'A'));
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
			out[i] = c;
		} else {
			out[i] = '_';
		}
		i++;
	}
	out[i] = '\0';
	if (i == 0) snprintf(out, outsize, "untitled");
}

s32 fileTransferConvertMusicToMod(const char *source_path,
                                  const char *display_name,
                                  const char *description,
                                  const char *creator,
                                  const char *tags,
                                  const char *version,
                                  char *out_pdmod_path,
                                  u32 out_pdmod_path_size)
{
	if (!source_path || !display_name || !creator) return -1;
	if (!*display_name || !*creator) return -1;

	const s32 kind = fileTransferClassifyByExt(source_path);
	if (kind != FT_KIND_MUSIC) return -1;

	char home[400];
	sysGetHomePath(home, sizeof(home));
	char installed_root[600];
	snprintf(installed_root, sizeof(installed_root), "%s/mods/installed", home);
	fsCreateDir(installed_root);

	char creator_slug[64];
	slugify(creator, creator_slug, sizeof(creator_slug));
	char name_slug[64];
	slugify(display_name, name_slug, sizeof(name_slug));

	char modid[160];
	snprintf(modid, sizeof(modid), "%s.%s", creator_slug, name_slug);

	char moddir[600];
	snprintf(moddir, sizeof(moddir), "%s/%s", installed_root, modid);
	fsCreateDir(moddir);

	char audiodir[700];
	snprintf(audiodir, sizeof(audiodir), "%s/audio", moddir);
	fsCreateDir(audiodir);

	const char *base = strrchr(source_path, '/');
	if (!base) base = strrchr(source_path, '\\');
	base = base ? base + 1 : source_path;
	char safe_name[128];
	sanitiseFilename(base, safe_name, sizeof(safe_name));

	char dest_audio[800];
	snprintf(dest_audio, sizeof(dest_audio), "%s/%s", audiodir, safe_name);
	appendCopy(source_path, dest_audio);

	char manifest[700];
	snprintf(manifest, sizeof(manifest), "%s/mod.json", moddir);
	FILE *f = fopen(manifest, "wb");
	if (!f) return -1;
	fprintf(f,
	         "{\n"
	         "  \"id\": \"%s\",\n"
	         "  \"name\": \"%s\",\n"
	         "  \"version\": \"%s\",\n"
	         "  \"creator\": \"%s\",\n"
	         "  \"description\": \"%s\",\n"
	         "  \"tags\": \"%s\",\n"
	         "  \"_converted_from\": { \"source\": \"%s\", \"type\": \"music\" }\n"
	         "}\n",
	         modid,
	         display_name,
	         version && *version ? version : "1.0.0",
	         creator,
	         description ? description : "",
	         tags ? tags : "music,user-converted",
	         safe_name);
	fclose(f);

	if (out_pdmod_path && out_pdmod_path_size > 0) {
		strncpy(out_pdmod_path, moddir, out_pdmod_path_size - 1);
		out_pdmod_path[out_pdmod_path_size - 1] = '\0';
	}

	sysLogPrintf(LOG_NOTE,
	             "FT: converted %s -> mod \"%s\" at %s",
	             source_path, display_name, moddir);
	return 0;
}
