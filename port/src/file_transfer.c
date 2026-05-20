/**
 * file_transfer.c -- chunked + sha256-verified file pipe over signed UDP.
 *
 * Wire format (1280 bytes per frame):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDFTX"
 *    5  1    version (1)
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
 * 1256 64    Ed25519 signature over body[0..1224) || domain
 * 1280
 *
 * Signature domain: "pd-ft-v1".
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
#include "chat.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "sha256.h"
#include "system.h"
#include "fs.h"
#include "modarchive.h"
#include "modmgr.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <direct.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/stat.h>
  #define closesocket close
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
#endif

#define FT_PORT          27107
#define FT_MAGIC         "PDFTX"
#define FT_MAGIC_LEN     5
#define FT_VERSION       1
#define FT_BODY_LEN      1224
#define FT_PUBKEY_OFFSET 1224
#define FT_PUBKEY_LEN    32
#define FT_SIG_OFFSET    1256
#define FT_SIG_LEN       64
#define FT_DOMAIN        "pd-ft-v1"
#define FT_DOMAIN_LEN    8

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
	u32 ipv4;
	u16 port;
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
	u8 *buffer;
	u8 *chunk_present;
	char name[96];
	char inbox_path[600];
	u32 ipv4;
	u16 port;
} ft_receiver_t;

static ft_sender_t   s_Senders[FT_MAX_INFLIGHT_SEND];
static ft_receiver_t s_Receivers[FT_MAX_INFLIGHT_RECV];

static SOCKET s_Sock = INVALID_SOCKET;
static s32    s_SocketReady;
static u64    s_NextTransferId;

#define FT_PENDING_MOD_ENABLE_MAX 4

typedef struct {
	u8  in_use;
	u32 sender_handle;
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
static u8  rU8 (const u8 **p) { return *(*p)++; }
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}
static u64 rU64(const u8 **p) {
	u64 v = 0; for (s32 i = 0; i < 8; i++) v |= ((u64)((*p)[i])) << (8*i);
	*p += 8; return v;
}

static s32 socketSetNonblock(SOCKET s) {
#ifdef _WIN32
	u_long mode = 1; return ioctlsocket(s, FIONBIO, &mode) == 0 ? 0 : -1;
#else
	int fl = fcntl(s, F_GETFL, 0); if (fl < 0) return -1;
	return fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static SOCKET ensureSocket(void)
{
	if (s_SocketReady) return s_Sock;
	s_Sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_Sock == INVALID_SOCKET) return INVALID_SOCKET;
	int yes = 1;
	setsockopt(s_Sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
	socketSetNonblock(s_Sock);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(FT_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock); s_Sock = INVALID_SOCKET; return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "FT: socket bound on UDP %u", (unsigned)FT_PORT);
	return s_Sock;
}

static s32 signFrame(const u8 *body, u8 *outSig)
{
	if (!body || !outSig) return 0;
	if (!identityGetPubkey()) return 0;
	u8 buf[FT_BODY_LEN + FT_DOMAIN_LEN];
	memcpy(buf, body, FT_BODY_LEN);
	memcpy(buf + FT_BODY_LEN, FT_DOMAIN, FT_DOMAIN_LEN);
	return identitySign(buf, sizeof(buf), outSig);
}

static s32 verifyFrame(const u8 *body, const u8 *sig, const u8 *pubkey)
{
	if (!body || !sig || !pubkey) return 0;
	u8 buf[FT_BODY_LEN + FT_DOMAIN_LEN];
	memcpy(buf, body, FT_BODY_LEN);
	memcpy(buf + FT_BODY_LEN, FT_DOMAIN, FT_DOMAIN_LEN);
	return ed25519Verify(sig, buf, sizeof(buf), pubkey) == 1 ? 1 : 0;
}

static void sendFrameTo(u32 ipv4, u16 port, const u8 *packet, u32 len)
{
	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(ipv4);
	dst.sin_port = htons(port);
	(void)sendto(s_Sock, (const char *)packet, len, 0,
	             (struct sockaddr *)&dst, sizeof(dst));
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

/* Sanitise a filename for inbox storage: strip path components, replace
 * unsafe characters with '_', cap to 95 bytes. */
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

static s32 ftPathEqualsNoCase(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *b) {
		if (!ftCharEqPathNoCase(*a, *b)) return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
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

static s32 ftStringEqualsNoCase(const char *a, const char *b)
{
	if (!a || !b) return 0;
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
			return 0;
		}
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static s32 ftArchiveEntryHasForbiddenBinPayload(const char *name)
{
	if (!name || !name[0]) return 0;
	const char *leaf = strrchr(name, '/');
	leaf = leaf ? leaf + 1 : name;
	if (!leaf[0] || leaf[0] == '.') return 0;
	return ftEndsWithNoCase(leaf, ".bin");
}

static s32 ftValidateReceivedModArchive(const char *path)
{
	mod_archive_t *arc = modArchiveOpen(path);
	if (!arc) {
		sysLogPrintf(LOG_WARNING,
		             "FT: received mod archive did not open: %s err=%d",
		             path ? path : "", modArchiveLastError());
		return 0;
	}

	u32 mfst_size = 0;
	char *mfst = modArchiveReadManifest(arc, &mfst_size);
	if (!mfst) {
		sysLogPrintf(LOG_WARNING,
		             "FT: received mod archive has no root mod.json: %s",
		             path ? path : "");
		modArchiveClose(arc);
		return 0;
	}
	free(mfst);
	(void)mfst_size;

	const s32 count = modArchiveGetEntryCount(arc);
	for (s32 i = 0; i < count; i++) {
		const char *entry = modArchiveGetEntryName(arc, i);
		if (ftArchiveEntryHasForbiddenBinPayload(entry)) {
			sysLogPrintf(LOG_WARNING,
			             "FT: received mod archive rejected for authored .bin payload: %s",
			             entry ? entry : "");
			modArchiveClose(arc);
			return 0;
		}
	}

	modArchiveClose(arc);
	return 1;
}

static s32 ftCopyFileAtomic(const char *src, const char *dst)
{
	char tmp[FS_MAXPATH + 1];
	snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
	remove(tmp);

	FILE *in = fopen(src, "rb");
	if (!in) return 0;
	FILE *out = fopen(tmp, "wb");
	if (!out) {
		fclose(in);
		return 0;
	}

	u8 buf[8192];
	s32 ok = 1;
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf), in);
		if (n > 0 && fwrite(buf, 1, n, out) != n) {
			ok = 0;
			break;
		}
		if (n < sizeof(buf)) {
			if (ferror(in)) ok = 0;
			break;
		}
	}

	if (fclose(out) != 0) ok = 0;
	fclose(in);

	if (!ok) {
		remove(tmp);
		return 0;
	}

	remove(dst);
	if (rename(tmp, dst) != 0) {
		remove(tmp);
		return 0;
	}
	return 1;
}

static s32 ftFindInstalledArchiveModIndex(const char *archive_path,
                                          const char *archive_leaf)
{
	s32 leaf_match = -1;
	const s32 count = modmgrGetCount();
	for (s32 i = 0; i < count; i++) {
		modinfo_t *mod = modmgrGetMod(i);
		if (!mod || !mod->is_archive || !mod->archive_path[0]) {
			continue;
		}
		if (ftPathEqualsNoCase(mod->archive_path, archive_path)) {
			return i;
		}
		if (archive_leaf && archive_leaf[0] &&
		    ftStringEqualsNoCase(ftPathLeaf(mod->archive_path), archive_leaf)) {
			leaf_match = i;
		}
	}
	return leaf_match;
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

static s32 ftEnableModIndexNow(s32 mod_index, const char *reason)
{
	modinfo_t *mod = modmgrGetMod(mod_index);
	if (!mod || !mod->valid) {
		sysLogPrintf(LOG_WARNING,
		             "FT: cannot enable received mod at index %d (%s)",
		             (int)mod_index, reason ? reason : "invalid");
		return -1;
	}

	char missing[256];
	if (modmgrCheckDependencies(mod_index, missing, sizeof(missing)) > 0) {
		sysLogPrintf(LOG_WARNING,
		             "FT: received mod '%s' left disabled; missing dependencies: %s",
		             mod->id, missing);
		return -1;
	}

	if (!mod->enabled) {
		modmgrSetEnabled(mod_index, 1);
		modmgrApplyChanges();
	} else {
		modmgrSyncCatalogToRegistry();
	}

	if (reason && reason[0]) {
		sysLogPrintf(LOG_NOTE,
		             "FT: enabled received mod '%s' (%s)",
		             mod->id, reason);
	} else {
		sysLogPrintf(LOG_NOTE,
		             "FT: enabled received mod '%s'",
		             mod->id);
	}
	return 0;
}

static s32 ftPendingModFirstIndex(void)
{
	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		if (s_PendingModEnable[i].in_use) return i;
	}
	return -1;
}

static void ftPendingModClearIndex(s32 idx)
{
	if (idx < 0 || idx >= FT_PENDING_MOD_ENABLE_MAX) return;
	memset(&s_PendingModEnable[idx], 0, sizeof(s_PendingModEnable[idx]));
}

static void ftQueuePendingModEnable(u32 sender_handle, const modinfo_t *mod)
{
	if (!mod || !mod->id[0]) return;

	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		if (s_PendingModEnable[i].in_use &&
		    strcmp(s_PendingModEnable[i].mod_id, mod->id) == 0) {
			s_PendingModEnable[i].sender_handle = sender_handle;
			strncpy(s_PendingModEnable[i].mod_name, mod->name,
			        sizeof(s_PendingModEnable[i].mod_name) - 1);
			s_PendingModEnable[i].mod_name[
				sizeof(s_PendingModEnable[i].mod_name) - 1] = '\0';
			return;
		}
	}

	for (s32 i = 0; i < FT_PENDING_MOD_ENABLE_MAX; i++) {
		ft_pending_mod_enable_t *p = &s_PendingModEnable[i];
		if (!p->in_use) {
			p->in_use = 1;
			p->sender_handle = sender_handle;
			strncpy(p->mod_id, mod->id, sizeof(p->mod_id) - 1);
			strncpy(p->mod_name, mod->name, sizeof(p->mod_name) - 1);
			p->mod_id[sizeof(p->mod_id) - 1] = '\0';
			p->mod_name[sizeof(p->mod_name) - 1] = '\0';
			sysLogPrintf(LOG_NOTE,
			             "FT: queued enable prompt for received mod '%s' from 0x%08x",
			             p->mod_id, (unsigned)sender_handle);
			return;
		}
	}

	sysLogPrintf(LOG_WARNING,
	             "FT: received mod '%s' installed disabled; enable prompt queue full",
	             mod->id);
}

s32 fileTransferPendingModEnableCount(void)
{
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

s32 fileTransferPendingModEnableAccept(void)
{
	const s32 idx = ftPendingModFirstIndex();
	if (idx < 0) return -1;

	char mod_id[MODMGR_ID_LEN];
	strncpy(mod_id, s_PendingModEnable[idx].mod_id, sizeof(mod_id) - 1);
	mod_id[sizeof(mod_id) - 1] = '\0';

	const s32 mod_index = ftFindModIndexById(mod_id);
	s32 rc = -1;
	if (mod_index >= 0) {
		rc = ftEnableModIndexNow(mod_index, "accepted received-mod prompt");
	} else {
		sysLogPrintf(LOG_WARNING,
		             "FT: pending received mod '%s' is no longer installed",
		             mod_id);
	}
	ftPendingModClearIndex(idx);
	return rc;
}

void fileTransferPendingModEnableDecline(void)
{
	const s32 idx = ftPendingModFirstIndex();
	if (idx < 0) return;
	sysLogPrintf(LOG_NOTE,
	             "FT: received mod '%s' kept disabled by user",
	             s_PendingModEnable[idx].mod_id);
	ftPendingModClearIndex(idx);
}

static void fileTransferInstallReceivedMod(const char *inbox_path,
                                           const char *original_name,
                                           u32 sender_handle)
{
	if (!inbox_path || !inbox_path[0]) return;
	if (!ftValidateReceivedModArchive(inbox_path)) return;

	const char *modsdir = modmgrGetModsDir();
	char fallback_mods[FS_MAXPATH + 1];
	if (!modsdir || !modsdir[0]) {
		fsFullPath("./mods", fallback_mods, sizeof(fallback_mods));
		fsCreateDir(fallback_mods);
		modsdir = fallback_mods;
	}

	char install_dir[FS_MAXPATH + 1];
	snprintf(install_dir, sizeof(install_dir), "%s/installed", modsdir);
	fsCreateDir(install_dir);

	char safe[96];
	sanitiseFilename(original_name, safe, sizeof(safe));
	if (!ftEndsWithNoCase(safe, ".pdmod") && !ftEndsWithNoCase(safe, ".zip")) {
		strncat(safe, ".pdmod", sizeof(safe) - strlen(safe) - 1);
	}

	char dst[FS_MAXPATH + 1];
	snprintf(dst, sizeof(dst), "%s/%s", install_dir, safe);
	if (!ftCopyFileAtomic(inbox_path, dst)) {
		sysLogPrintf(LOG_WARNING,
		             "FT: received mod archive could not install to %s",
		             dst);
		return;
	}

	modmgrRescanDirectory();
	sysLogPrintf(LOG_NOTE,
	             "FT: installed received mod archive -> %s and refreshed mod registry",
	             dst);

	const s32 mod_index = ftFindInstalledArchiveModIndex(dst, safe);
	if (mod_index < 0) {
		sysLogPrintf(LOG_WARNING,
		             "FT: installed received mod archive was not found in registry: %s",
		             dst);
		return;
	}

	modinfo_t *mod = modmgrGetMod(mod_index);
	if (!mod || !mod->valid) {
		sysLogPrintf(LOG_WARNING,
		             "FT: installed received mod archive is invalid: %s",
		             mod && mod->validation_error[0]
		                 ? mod->validation_error
		                 : dst);
		return;
	}

	if (socialFriendByHandle(sender_handle)) {
		(void)ftEnableModIndexNow(mod_index, "friend request-download");
	} else {
		ftQueuePendingModEnable(sender_handle, mod);
	}
}

/* Build the destination path: <home>/social/inbox/<kind>/<friend>/<file>.
 * Creates intermediate folders. */
static void buildInboxPath(s32 kind, const char *friend_agent, const char *name,
                            char *out, u32 outsize)
{
	char folder[700];
	const char *agent = (friend_agent && friend_agent[0]) ? friend_agent : "anonymous";
	snprintf(folder, sizeof(folder), "%s/%s", inboxRoot(), kindFolder(kind));
	fsCreateDir(folder);
	snprintf(folder, sizeof(folder), "%s/%s/%s",
	          inboxRoot(), kindFolder(kind), agent);
	fsCreateDir(folder);

	char safe[96];
	sanitiseFilename(name, safe, sizeof(safe));
	snprintf(out, outsize, "%s/%s", folder, safe);
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
	memcpy(packet + FT_PUBKEY_OFFSET, identityGetPubkey(), FT_PUBKEY_LEN);
	if (!signFrame(packet, packet + FT_SIG_OFFSET)) return;
	sendFrameTo(s->ipv4, s->port, packet, FT_FRAME_LEN);
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
	if (got == 0) return;

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
	memcpy(packet + FT_PUBKEY_OFFSET, identityGetPubkey(), FT_PUBKEY_LEN);
	if (!signFrame(packet, packet + FT_SIG_OFFSET)) return;
	sendFrameTo(s->ipv4, s->port, packet, FT_FRAME_LEN);
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
	memcpy(packet + FT_PUBKEY_OFFSET, identityGetPubkey(), FT_PUBKEY_LEN);
	if (!signFrame(packet, packet + FT_SIG_OFFSET)) return;
	sendFrameTo(s->ipv4, s->port, packet, FT_FRAME_LEN);
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
static void sendAck(u32 ipv4, u16 port, u32 dst_handle, u64 transfer_id, u32 chunk_seq)
{
	u8 packet[FT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, FT_MAGIC, FT_MAGIC_LEN); w += FT_MAGIC_LEN;
	wU8(&w, FT_VERSION);
	wU8(&w, FT_KIND_ACK);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, dst_handle);
	wU64(&w, transfer_id);
	wU32(&w, chunk_seq);
	wU32(&w, 0);
	memcpy(packet + FT_PUBKEY_OFFSET, identityGetPubkey(), FT_PUBKEY_LEN);
	if (!signFrame(packet, packet + FT_SIG_OFFSET)) return;
	sendFrameTo(ipv4, port, packet, FT_FRAME_LEN);
}

/* -------------------------------------------------------------------------
 * Sidecar metadata write
 * ------------------------------------------------------------------------- */

static void writeSidecar(const ft_receiver_t *r)
{
	char sidecar[700];
	snprintf(sidecar, sizeof(sidecar), "%s.meta.json", r->inbox_path);
	FILE *f = fopen(sidecar, "wb");
	if (!f) return;
	const social_friend_t *fr = socialFriendByHandle(r->src_handle);
	const char *agent = fr && fr->agent_name[0] ? fr->agent_name : "anonymous";
	char hex[SHA256_HEX_SIZE];
	sha256ToHex(r->expected_sha256, hex);
	fprintf(f,
	         "{\n"
	         "  \"sender_handle\": %u,\n"
	         "  \"sender_agent\": \"%s\",\n"
	         "  \"received_at\": %lld,\n"
	         "  \"original_name\": \"%s\",\n"
	         "  \"sha256\": \"%s\",\n"
	         "  \"size_bytes\": %llu,\n"
	         "  \"kind\": \"%s\"\n"
	         "}\n",
	         (unsigned)r->src_handle,
	         agent,
	         (long long)time(NULL),
	         r->name,
	         hex,
	         (unsigned long long)r->file_size,
	         fileTransferKindName(r->kind));
	fclose(f);
}

/* -------------------------------------------------------------------------
 * Drain receive
 * ------------------------------------------------------------------------- */

static void completeReceiver(ft_receiver_t *r, u32 src_ipv4, u16 src_port)
{
	if (r->received_chunks != r->chunk_total) return;

	u8 actual[SHA256_DIGEST_SIZE];
	sha256Hash(r->buffer, (size_t)r->file_size, actual);
	if (memcmp(actual, r->expected_sha256, SHA256_DIGEST_SIZE) != 0) {
		sysLogPrintf(LOG_WARNING, "FT: hash mismatch on transfer %llu from 0x%08x -- rejecting",
		             (unsigned long long)r->transfer_id, (unsigned)r->src_handle);
		freeReceiver(r);
		return;
	}

	FILE *f = fopen(r->inbox_path, "wb");
	if (!f) {
		sysLogPrintf(LOG_WARNING, "FT: failed to open %s for write", r->inbox_path);
		freeReceiver(r);
		return;
	}
	if (fwrite(r->buffer, 1, (size_t)r->file_size, f) != (size_t)r->file_size) {
		fclose(f);
		remove(r->inbox_path);
		sysLogPrintf(LOG_WARNING, "FT: short write to %s", r->inbox_path);
		freeReceiver(r);
		return;
	}
	fclose(f);

	writeSidecar(r);

	if (r->kind == FT_KIND_MOD) {
		fileTransferInstallReceivedMod(r->inbox_path, r->name, r->src_handle);
	}

	(void)chatHistoryAppendAttachment(r->src_handle, (u32)r->kind, r->name,
	                                   r->inbox_path, r->file_size);

	sysLogPrintf(LOG_NOTE,
	             "FT: complete <- 0x%08x kind=%s size=%llu path=%s",
	             (unsigned)r->src_handle, fileTransferKindName(r->kind),
	             (unsigned long long)r->file_size, r->inbox_path);

	(void)src_ipv4; (void)src_port;
	freeReceiver(r);
}

static void handleInit(const u8 *body, u32 src_ipv4, u16 src_port)
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

	if (file_size > sizeLimitForKind(kind)) {
		sysLogPrintf(LOG_WARNING,
		             "FT: refusing oversize %s (%llu > %llu) from 0x%08x",
		             fileTransferKindName(kind),
		             (unsigned long long)file_size,
		             (unsigned long long)sizeLimitForKind(kind),
		             (unsigned)src_handle);
		return;
	}

	ft_receiver_t *r = findReceiver(tid, src_handle);
	if (r) {
		r->last_recv_ms = SDL_GetTicks();
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
	r->ipv4 = src_ipv4;
	r->port = src_port;
	memcpy(r->expected_sha256, sha, SHA256_DIGEST_SIZE);
	strncpy(r->name, origname, sizeof(r->name) - 1);

	r->buffer = (u8 *)malloc((size_t)file_size);
	r->chunk_present = (u8 *)calloc(chunk_total, 1);
	if (!r->buffer || !r->chunk_present) {
		freeReceiver(r);
		return;
	}

	const social_friend_t *fr = socialFriendByHandle(src_handle);
	const char *agent = fr && fr->agent_name[0] ? fr->agent_name : "anonymous";
	buildInboxPath(kind, agent, r->name, r->inbox_path, sizeof(r->inbox_path));

	sysLogPrintf(LOG_NOTE,
	             "FT: init <- 0x%08x kind=%s size=%llu chunks=%u name=\"%s\"",
	             (unsigned)src_handle, fileTransferKindName(kind),
	             (unsigned long long)file_size, (unsigned)chunk_total, r->name);

	/* Ack the init so the sender knows we are listening. */
	sendAck(src_ipv4, src_port, src_handle, tid, 0xFFFFFFFFu);
}

static void handleChunk(const u8 *body, u32 src_ipv4, u16 src_port)
{
	const u8 *p = body + FT_MAGIC_LEN + 1 + 1 + 1;
	u32 src_handle = rU32(&p);
	u32 dst_handle = rU32(&p);
	u64 tid        = rU64(&p);
	u32 chunk_seq  = rU32(&p);
	u32 chunk_total = rU32(&p);
	(void)dst_handle;
	(void)chunk_total;

	ft_receiver_t *r = findReceiver(tid, src_handle);
	if (!r) return;
	if (chunk_seq >= r->chunk_total) return;
	r->last_recv_ms = SDL_GetTicks();
	if (r->chunk_present[chunk_seq]) {
		sendAck(src_ipv4, src_port, src_handle, tid, chunk_seq);
		return;
	}

	const u64 off = (u64)chunk_seq * FT_CHUNK_PAYLOAD;
	const u32 cap = (chunk_seq + 1 == r->chunk_total)
	                ? (u32)(r->file_size - off)
	                : (u32)FT_CHUNK_PAYLOAD;
	if (off + cap > r->file_size) return;
	memcpy(r->buffer + off, body + 200, cap);
	r->chunk_present[chunk_seq] = 1;
	r->received_chunks++;

	sendAck(src_ipv4, src_port, src_handle, tid, chunk_seq);

	if (r->received_chunks == r->chunk_total) {
		completeReceiver(r, src_ipv4, src_port);
	}
}

static void handleAck(const u8 *body)
{
	const u8 *p = body + FT_MAGIC_LEN + 1 + 1 + 1;
	(void)rU32(&p);
	u32 dst_handle = rU32(&p);
	u64 tid        = rU64(&p);
	u32 chunk_seq  = rU32(&p);
	(void)dst_handle;

	ft_sender_t *s = findSender(tid);
	if (!s) return;
	if (chunk_seq == 0xFFFFFFFFu) {
		/* INIT ack: start sending data chunks. */
		s->next_seq = 0;
		s->last_send_ms = SDL_GetTicks();
		s->attempts = 0;
		return;
	}
	if (chunk_seq >= s->chunk_total) return;
	/* Advance: receiver got chunk_seq; we are sending sequentially so
	 * this is just the ack we expected. */
	if (chunk_seq == s->next_seq) {
		s->next_seq++;
		s->last_send_ms = SDL_GetTicks();
		s->attempts = 0;
		if (s->next_seq == s->chunk_total) {
			sendEnd(s);
			sysLogPrintf(LOG_NOTE,
			             "FT: send complete -> 0x%08x tid=%llu",
			             (unsigned)s->friend_handle, (unsigned long long)tid);
			freeSender(s);
		}
	}
}

static void drainReceive(void)
{
	if (!s_SocketReady) return;
	for (;;) {
		u8 packet[FT_FRAME_LEN + 64];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n != FT_FRAME_LEN) continue;
		if (memcmp(packet, FT_MAGIC, FT_MAGIC_LEN) != 0) continue;

		const u8 ver  = packet[5];
		const u8 kind = packet[6];
		if (ver != FT_VERSION) continue;

		/* We need src_handle to perform the friend / pubkey check. */
		const u8 *p = packet + 8;
		u32 src_handle = rU32(&p);
		(void)src_handle;

		const u8 *sender_pub = packet + FT_PUBKEY_OFFSET;
		const u8 *sender_sig = packet + FT_SIG_OFFSET;

		if (!socialFriendByHandle(src_handle) &&
		    findSender(0) == NULL) {
			/* Allow ACKs from peers we have an active send to even before
			 * social adds them; otherwise gate on friendship. */
			if (!socialFriendByHandle(src_handle)) continue;
		}
		if (socialBlockIsHandle(src_handle)) continue;
		if (!socialHandleBindsPubkey(src_handle, sender_pub)) continue;
		if (!verifyFrame(packet, sender_sig, sender_pub)) continue;
		if (socialFriendBindPubkey(src_handle, sender_pub) < 0) continue;

		const u32 src_ipv4 = ntohl(src.sin_addr.s_addr);
		const u16 src_port = ntohs(src.sin_port);

		switch (kind) {
			case FT_KIND_INIT:  handleInit (packet, src_ipv4, src_port); break;
			case FT_KIND_CHUNK: handleChunk(packet, src_ipv4, src_port); break;
			case FT_KIND_END:
				/* No-op for the receiver: chunk-complete handles teardown.
				 * END exists so the sender can signal "I'm done sending"
				 * for diagnostics; arrival-after-completion is harmless. */
				break;
			case FT_KIND_ACK:   handleAck(packet); break;
			case FT_KIND_REJECT: {
				const u8 *q = packet + 16;
				u64 tid = rU64(&q);
				ft_sender_t *s = findSender(tid);
				if (s) {
					sysLogPrintf(LOG_WARNING, "FT: peer 0x%08x rejected transfer %llu",
					             (unsigned)s->friend_handle, (unsigned long long)tid);
					freeSender(s);
				}
				break;
			}
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
	memset(s_Senders, 0, sizeof(s_Senders));
	memset(s_Receivers, 0, sizeof(s_Receivers));
	s_NextTransferId = ((u64)time(NULL) << 16);
	(void)inboxRoot();
	(void)ensureSocket();
}

void fileTransferShutdown(void)
{
	for (s32 i = 0; i < FT_MAX_INFLIGHT_SEND; i++) freeSender(&s_Senders[i]);
	for (s32 i = 0; i < FT_MAX_INFLIGHT_RECV; i++) freeReceiver(&s_Receivers[i]);
	if (s_SocketReady) {
		closesocket(s_Sock); s_Sock = INVALID_SOCKET; s_SocketReady = 0;
	}
}

void fileTransferTick(void)
{
	if (!s_SocketReady) return;
	drainReceive();
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
	if (friend_handle == 0 || !local_path) return -1;
	if (socialBlockIsHandle(friend_handle)) return -1;
	if (!socialFriendByHandle(friend_handle)) return -1;

	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady) return -1;

	u32 ipv4 = 0; u16 port = 0;
	if (!socialFriendGetEndpoint(friend_handle, &ipv4, &port)) return -1;
	if (port == 0) port = FT_PORT;

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

	ft_sender_t *s = allocSender();
	if (!s) { fclose(fp); return -1; }
	memset(s, 0, sizeof(*s));
	s->in_use = 1;
	s->friend_handle = friend_handle;
	s->transfer_id = s_NextTransferId++;
	s->file_size = (u64)sz;
	s->chunk_total = (u32)((s->file_size + FT_CHUNK_PAYLOAD - 1) / FT_CHUNK_PAYLOAD);
	s->next_seq = 0xFFFFFFFFu; /* awaiting INIT ack */
	s->last_send_ms = 0;
	s->attempts = 0;
	s->kind = kind;
	s->fp = fp;
	s->ipv4 = ipv4;
	s->port = port;

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
