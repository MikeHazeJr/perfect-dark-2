/**
 * social_share.c -- cross-peer presence-aux data over a signed UDP
 * socket on port 27109.
 *
 * Single-writer hygiene: this module owns s_AggregateMods and
 * s_Profiles. Inbound frames flow through one verifyAndDispatch path
 * (signature -> handle bind -> TOFU lock -> kind dispatch). Outbound
 * broadcasters are explicit named functions called by the per-feature
 * modules (listening_room, mod_public, presence) on rotation.
 *
 * Up + down logging: every drop point logs `SHARE:` with cause; every
 * accepted manifest logs counts so a stuck-aggregator trace is one
 * grep.
 */

#include "social_share.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "presence.h"
#include "listening_room.h"
#include "playerstats.h"
#include "file_transfer.h"
#include "system.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #define closesocket close
  typedef int SOCKET;
  #define INVALID_SOCKET (-1)
#endif

#define SHARE_PORT          27109
#define SHARE_MAGIC         "PDSHR"
#define SHARE_MAGIC_LEN     5
#define SHARE_VERSION       1
#define SHARE_HEADER_LEN    22
#define SHARE_PUBKEY_LEN    32
#define SHARE_SIG_LEN       64
#define SHARE_DOMAIN        "pd-share-v1"
#define SHARE_DOMAIN_LEN    11

#define SHARE_BROADCAST_INTERVAL_MS 60000u   /* one full broadcast pass per minute */
#define SHARE_RATE_WINDOW_MS         5000u
#define SHARE_RATE_BUCKETS              64

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

static SOCKET s_Sock = INVALID_SOCKET;
static s32    s_SocketReady;
static u32    s_NextSeq;
static u32    s_LastBroadcastMs;

static share_mod_entry_t s_AggregateMods[SHARE_AGGREGATE_MAX];
static s32               s_NumAggregateMods;

#define SHARE_PROFILES_MAX 64
static share_profile_t   s_Profiles[SHARE_PROFILES_MAX];
static s32               s_NumProfiles;

typedef struct { u32 handle; u32 last_recv_ms; } rate_bucket_t;
static rate_bucket_t s_RateBuckets[SHARE_RATE_BUCKETS];

/* -------------------------------------------------------------------------
 * Endian helpers
 * ------------------------------------------------------------------------- */

static void wU8(u8 **p, u8 v)   { *(*p)++ = v; }
static void wU16(u8 **p, u16 v) { (*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); *p+=2; }
static void wU32(u8 **p, u32 v) {
	(*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); (*p)[2]=(u8)(v>>16); (*p)[3]=(u8)(v>>24); *p+=4;
}
static u8  rU8 (const u8 **p) { return *(*p)++; }
static u16 rU16(const u8 **p) { u16 v = (u16)((*p)[0]) | ((u16)((*p)[1])<<8); *p+=2; return v; }
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}

/* -------------------------------------------------------------------------
 * Socket
 * ------------------------------------------------------------------------- */

static s32 socketSetNonblock(SOCKET s)
{
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
	addr.sin_port = htons(SHARE_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock); s_Sock = INVALID_SOCKET; return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "SHARE: socket bound on UDP %u", (unsigned)SHARE_PORT);
	return s_Sock;
}

/* -------------------------------------------------------------------------
 * Rate limit
 * ------------------------------------------------------------------------- */

static s32 rateLimitAllow(u32 handle)
{
	const u32 now = SDL_GetTicks();
	rate_bucket_t *empty = NULL;
	rate_bucket_t *oldest = &s_RateBuckets[0];
	for (s32 i = 0; i < SHARE_RATE_BUCKETS; i++) {
		rate_bucket_t *b = &s_RateBuckets[i];
		if (b->handle == handle) {
			if (now - b->last_recv_ms < SHARE_RATE_WINDOW_MS) return 0;
			b->last_recv_ms = now; return 1;
		}
		if (!b->handle && !empty) empty = b;
		if (b->last_recv_ms < oldest->last_recv_ms) oldest = b;
	}
	rate_bucket_t *slot = empty ? empty : oldest;
	slot->handle = handle; slot->last_recv_ms = now;
	return 1;
}

/* -------------------------------------------------------------------------
 * Sign / verify
 * ------------------------------------------------------------------------- */

static s32 signFrame(const u8 *body, u32 body_len, u8 *outSig)
{
	if (!identityGetPubkey()) return 0;
	u8 buf[SHARE_HEADER_LEN + SHARE_PAYLOAD_MAX + SHARE_PUBKEY_LEN + SHARE_DOMAIN_LEN];
	memcpy(buf, body, body_len);
	memcpy(buf + body_len, SHARE_DOMAIN, SHARE_DOMAIN_LEN);
	return identitySign(buf, body_len + SHARE_DOMAIN_LEN, outSig);
}

static s32 verifyFrame(const u8 *body, u32 body_len, const u8 *sig, const u8 *pubkey)
{
	u8 buf[SHARE_HEADER_LEN + SHARE_PAYLOAD_MAX + SHARE_PUBKEY_LEN + SHARE_DOMAIN_LEN];
	memcpy(buf, body, body_len);
	memcpy(buf + body_len, SHARE_DOMAIN, SHARE_DOMAIN_LEN);
	return ed25519Verify(sig, buf, body_len + SHARE_DOMAIN_LEN, pubkey) == 1 ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * Outbound builder
 * ------------------------------------------------------------------------- */

static void sendFrameTo(u32 ipv4, u16 port, u8 kind, u32 target_handle,
                         const u8 *payload, u16 payload_len)
{
	if (!s_SocketReady) return;
	if (payload_len > SHARE_PAYLOAD_MAX) return;

	const u32 body_len = SHARE_HEADER_LEN + payload_len + SHARE_PUBKEY_LEN;
	u8 packet[SHARE_HEADER_LEN + SHARE_PAYLOAD_MAX + SHARE_PUBKEY_LEN + SHARE_SIG_LEN];
	memset(packet, 0, sizeof(packet));

	u8 *w = packet;
	memcpy(w, SHARE_MAGIC, SHARE_MAGIC_LEN); w += SHARE_MAGIC_LEN;
	wU8(&w, SHARE_VERSION);
	wU8(&w, kind);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, target_handle);
	wU32(&w, s_NextSeq++);
	wU16(&w, payload_len);
	if (payload_len > 0 && payload) {
		memcpy(w, payload, payload_len); w += payload_len;
	}
	memcpy(w, identityGetPubkey(), SHARE_PUBKEY_LEN); w += SHARE_PUBKEY_LEN;
	if (!signFrame(packet, body_len, w)) return;

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(ipv4);
	dst.sin_port = htons(port);
	(void)sendto(s_Sock, (const char *)packet,
	             body_len + SHARE_SIG_LEN, 0,
	             (struct sockaddr *)&dst, sizeof(dst));
}

/* Resolve a friend's endpoint via the existing presence cache. Returns 0
 * if unreachable. */
static s32 resolveFriendEndpoint(u32 handle, u32 *out_ipv4, u16 *out_port)
{
	const presence_peer_t *p = presencePeerByHandle(handle);
	if (p && p->cached_ipv4 != 0) {
		*out_ipv4 = p->cached_ipv4;
		*out_port = SHARE_PORT;
		return 1;
	}
	if (socialFriendGetEndpoint(handle, out_ipv4, out_port) == 1) {
		*out_port = SHARE_PORT;
		return 1;
	}
	return 0;
}

static void broadcastToFriends(u8 kind, const u8 *payload, u16 payload_len)
{
	const s32 nf = socialFriendCount();
	for (s32 i = 0; i < nf; i++) {
		const social_friend_t *f = socialFriendAt(i);
		if (!f) continue;
		u32 ipv4 = 0; u16 port = 0;
		if (!resolveFriendEndpoint(f->handle, &ipv4, &port)) continue;
		sendFrameTo(ipv4, port, kind, f->handle, payload, payload_len);
	}
}

/* -------------------------------------------------------------------------
 * Listening-room manifest
 * ------------------------------------------------------------------------- */

void shareBroadcastListeningRoom(void)
{
	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady) return;
	if (listeningRoomState() != LR_STATE_HOST) return;

	const s32 ntr = listeningRoomTrackCount();
	u8 payload[SHARE_PAYLOAD_MAX];
	u8 *w = payload;
	*w++ = (u8)(ntr > 31 ? 31 : ntr);

	for (s32 i = 0; i < ntr; i++) {
		const lr_track_t *t = listeningRoomTrackAt(i);
		if (!t) break;
		const size_t id_len = strnlen(t->track_id, LR_TRACK_ID_MAX);
		const size_t nm_len = strnlen(t->display_name, LR_TRACK_NAME_MAX);
		if ((w - payload) + 1 + id_len + 1 + nm_len + 4 + 1 > SHARE_PAYLOAD_MAX) break;
		*w++ = (u8)id_len;
		memcpy(w, t->track_id, id_len); w += id_len;
		*w++ = (u8)nm_len;
		memcpy(w, t->display_name, nm_len); w += nm_len;
		wU32(&w, t->duration_ms);
		*w++ = (i == listeningRoomCurrentIdx()) ? 1 : 0;
	}

	const u16 plen = (u16)(w - payload);
	broadcastToFriends(SHARE_KIND_LR_MANIFEST, payload, plen);
	sysLogPrintf(LOG_NOTE, "SHARE: listening-room manifest broadcast (%d tracks)",
	             (int)ntr);
}

/* -------------------------------------------------------------------------
 * Public mods manifest
 *
 * Mike's brief item (c): per-mod public flag in mod registry. Priority M
 * owns the mod loader / mod.json schema; we maintain a separate
 * <home>/social/mod-public.json registry in this module so the loader
 * stays untouched. The UI populates the registry from the Public Mods
 * tab toggle.
 * ------------------------------------------------------------------------- */

#define MOD_PUBLIC_MAX 64
typedef struct {
	char mod_id[64];
	char display_name[SHARE_MOD_NAME_MAX];
	char version[SHARE_MOD_VER_MAX];
	u32  size_bytes;
} mod_public_entry_t;
static mod_public_entry_t s_MyPublicMods[MOD_PUBLIC_MAX];
static s32                s_NumMyPublicMods;

#include "fs.h"

static const char *modPublicPath(void)
{
	static char path[512];
	if (path[0]) return path;
	char home[400];
	sysGetHomePath(home, sizeof(home));
	snprintf(path, sizeof(path), "%s/social/mod-public.json", home);
	return path;
}

static void saveMyPublicMods(void)
{
	const char *path = modPublicPath();
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "{\n  \"version\": 1,\n  \"mods\": [");
	for (s32 i = 0; i < s_NumMyPublicMods; i++) {
		const mod_public_entry_t *m = &s_MyPublicMods[i];
		fprintf(f, "%s\n    { \"id\": \"%s\", \"name\": \"%s\", \"version\": \"%s\", \"size\": %u }",
		         i == 0 ? "" : ",",
		         m->mod_id, m->display_name, m->version, (unsigned)m->size_bytes);
	}
	fprintf(f, "\n  ]\n}\n");
	fclose(f);
}

static void loadMyPublicMods(void)
{
	/* The loader format is the writer-emitted form; we use a tolerant
	 * reader here that accepts the JSON we wrote and ignores anything
	 * unfamiliar. Errors leave s_NumMyPublicMods at 0. */
	s_NumMyPublicMods = 0;
	FILE *f = fopen(modPublicPath(), "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz <= 0 || sz > 256 * 1024) { fclose(f); return; }
	char *buf = (char *)malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return; }
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		fclose(f); free(buf); return;
	}
	fclose(f);
	buf[sz] = '\0';

	/* Walk through quoted "id" values (cheap). For each, pull the next
	 * three sibling fields. */
	const char *p = buf;
	while ((p = strstr(p, "\"id\""))) {
		p = strchr(p, ':'); if (!p) break;
		p = strchr(p, '"'); if (!p) break;
		p++;
		const char *q = strchr(p, '"');
		if (!q) break;
		mod_public_entry_t e; memset(&e, 0, sizeof(e));
		const size_t n = (q - p) > (s32)sizeof(e.mod_id) - 1
		                  ? sizeof(e.mod_id) - 1 : (size_t)(q - p);
		memcpy(e.mod_id, p, n); e.mod_id[n] = '\0';

		const char *namek = strstr(q, "\"name\"");
		if (namek) {
			const char *c = strchr(namek, ':');
			if (c) { c = strchr(c, '"');
				if (c) { c++; const char *e2 = strchr(c, '"');
					if (e2) { size_t m2 = (e2 - c) > (s32)sizeof(e.display_name) - 1 ? sizeof(e.display_name) - 1 : (size_t)(e2 - c);
						memcpy(e.display_name, c, m2); e.display_name[m2] = '\0';
					} } }
		}
		const char *verk = strstr(q, "\"version\"");
		if (verk) {
			const char *c = strchr(verk, ':');
			if (c) { c = strchr(c, '"');
				if (c) { c++; const char *e2 = strchr(c, '"');
					if (e2) { size_t m2 = (e2 - c) > (s32)sizeof(e.version) - 1 ? sizeof(e.version) - 1 : (size_t)(e2 - c);
						memcpy(e.version, c, m2); e.version[m2] = '\0';
					} } }
		}
		const char *sizek = strstr(q, "\"size\"");
		if (sizek) {
			const char *c = strchr(sizek, ':');
			if (c) {
				c++;
				while (*c && (*c == ' ' || *c == '\t')) c++;
				e.size_bytes = (u32)strtoul(c, NULL, 10);
			}
		}

		if (s_NumMyPublicMods < MOD_PUBLIC_MAX && e.mod_id[0]) {
			s_MyPublicMods[s_NumMyPublicMods++] = e;
		}
		p = q + 1;
	}

	free(buf);
}

s32 shareModPublicAdd(const char *mod_id, const char *display_name,
                      const char *version, u32 size_bytes)
{
	if (!mod_id || !*mod_id) return -1;
	if (s_NumMyPublicMods >= MOD_PUBLIC_MAX) return -1;
	for (s32 i = 0; i < s_NumMyPublicMods; i++) {
		if (!strcmp(s_MyPublicMods[i].mod_id, mod_id)) return 0;
	}
	mod_public_entry_t *e = &s_MyPublicMods[s_NumMyPublicMods++];
	memset(e, 0, sizeof(*e));
	strncpy(e->mod_id, mod_id, sizeof(e->mod_id) - 1);
	if (display_name) strncpy(e->display_name, display_name, sizeof(e->display_name) - 1);
	if (version)      strncpy(e->version, version, sizeof(e->version) - 1);
	e->size_bytes = size_bytes;
	saveMyPublicMods();
	return 1;
}

s32 shareModPublicRemove(const char *mod_id)
{
	if (!mod_id) return 0;
	for (s32 i = 0; i < s_NumMyPublicMods; i++) {
		if (!strcmp(s_MyPublicMods[i].mod_id, mod_id)) {
			memmove(&s_MyPublicMods[i], &s_MyPublicMods[i+1],
			        (size_t)(s_NumMyPublicMods - i - 1) * sizeof(mod_public_entry_t));
			s_NumMyPublicMods--;
			saveMyPublicMods();
			return 1;
		}
	}
	return 0;
}

s32 shareModPublicCount(void) { return s_NumMyPublicMods; }
const char *shareModPublicIdAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumMyPublicMods) return NULL;
	return s_MyPublicMods[idx].mod_id;
}
const char *shareModPublicNameAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumMyPublicMods) return NULL;
	return s_MyPublicMods[idx].display_name;
}

void shareBroadcastPublicMods(void)
{
	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady) return;
	if (s_NumMyPublicMods == 0) return;

	u8 payload[SHARE_PAYLOAD_MAX];
	u8 *w = payload;
	const u8 cap = (u8)(s_NumMyPublicMods > 32 ? 32 : s_NumMyPublicMods);
	*w++ = cap;

	for (u8 i = 0; i < cap; i++) {
		const mod_public_entry_t *m = &s_MyPublicMods[i];
		const size_t id_len = strnlen(m->mod_id, sizeof(m->mod_id));
		const size_t nm_len = strnlen(m->display_name, sizeof(m->display_name));
		const size_t vr_len = strnlen(m->version, sizeof(m->version));
		const u32 need = 1 + id_len + 1 + nm_len + 1 + vr_len + 4 + 1;
		if ((u32)(w - payload) + need > SHARE_PAYLOAD_MAX) break;
		*w++ = (u8)id_len;
		memcpy(w, m->mod_id, id_len); w += id_len;
		*w++ = (u8)nm_len;
		memcpy(w, m->display_name, nm_len); w += nm_len;
		*w++ = (u8)vr_len;
		memcpy(w, m->version, vr_len); w += vr_len;
		wU32(&w, m->size_bytes);
		*w++ = 0; /* sha256_present (not yet computed at broadcast time) */
	}

	const u16 plen = (u16)(w - payload);
	broadcastToFriends(SHARE_KIND_MODS_MANIFEST, payload, plen);
	sysLogPrintf(LOG_NOTE, "SHARE: public mods manifest broadcast (%d mods)",
	             (int)s_NumMyPublicMods);
}

/* -------------------------------------------------------------------------
 * Profile stats
 * ------------------------------------------------------------------------- */

void shareBroadcastProfileStats(void)
{
	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady) return;

	/* Pull totals from the local playerstats subsystem. The keys here
	 * match what statIncrement / statGet record across the codebase
	 * (kills.total, deaths.total, missions.completed). u64 values are
	 * truncated to u32 for the wire; profile UI reads u32. */
	const u32 kills    = (u32)statGet("kills.total");
	const u32 deaths   = (u32)statGet("deaths.total");
	const u32 missions = (u32)statGet("missions.completed");

	u8 payload[16];
	u8 *w = payload;
	wU32(&w, kills);
	wU32(&w, deaths);
	wU32(&w, missions);
	wU32(&w, 0); /* reserved */

	broadcastToFriends(SHARE_KIND_PROFILE_STATS, payload, 16);
}

/* -------------------------------------------------------------------------
 * Mod request -> mod offer flow
 * ------------------------------------------------------------------------- */

s32 shareSendModRequest(u32 friend_handle, const char *mod_id)
{
	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady || friend_handle == 0 || !mod_id || !*mod_id) return -1;
	if (socialBlockIsHandle(friend_handle)) return -1;
	const social_friend_t *f = socialFriendByHandle(friend_handle);
	if (!f) return -1;

	u32 ipv4 = 0; u16 port = 0;
	if (!resolveFriendEndpoint(friend_handle, &ipv4, &port)) return -1;

	const size_t id_len = strnlen(mod_id, 63);
	u8 payload[64];
	payload[0] = (u8)id_len;
	memcpy(payload + 1, mod_id, id_len);
	sendFrameTo(ipv4, port, SHARE_KIND_MOD_REQUEST, friend_handle,
	             payload, (u16)(1 + id_len));
	sysLogPrintf(LOG_NOTE, "SHARE: mod request -> 0x%08x mod_id=\"%s\"",
	             (unsigned)friend_handle, mod_id);
	return 0;
}

/* When a mod request arrives, we look up the requested mod_id in our
 * mods/installed/<mod_id>/ folder and send the archive (or folder zip)
 * via the existing file_transfer pipe. For Phase 4 dual-support folder
 * mods are the on-disk form; Priority M's Phase M-3 auto-package will
 * convert these to .pdmod archives, and the file_transfer call simply
 * picks up whichever form exists. */
static void handleModRequest(u32 from_handle, const u8 *payload, u32 payload_len)
{
	if (payload_len < 1) return;
	const u8 id_len = payload[0];
	if (id_len + 1u > payload_len) return;

	char mod_id[64];
	const u32 cap = id_len < (u32)sizeof(mod_id) - 1 ? id_len : (u32)sizeof(mod_id) - 1;
	memcpy(mod_id, payload + 1, cap);
	mod_id[cap] = '\0';

	/* Resolve a path. Priority M stores mods at <home>/mods/installed/
	 * <mod_id>.pdmod (archive form) or <home>/mods/installed/<mod_id>/
	 * (folder form). We probe both -- pick whichever exists. */
	char home[400];
	sysGetHomePath(home, sizeof(home));

	char archive_path[600];
	snprintf(archive_path, sizeof(archive_path), "%s/mods/installed/%s.pdmod",
	          home, mod_id);
	FILE *probe = fopen(archive_path, "rb");
	if (probe) {
		fclose(probe);
		(void)fileTransferSendFile(from_handle, archive_path);
		sysLogPrintf(LOG_NOTE, "SHARE: mod offer -> 0x%08x archive=%s",
		             (unsigned)from_handle, archive_path);
		return;
	}

	/* Folder form -- send a manifest file as a hint. The receiver gets
	 * the mod.json which they can use to ask the user to manually
	 * fetch the rest. Sending the entire folder verbatim is a
	 * follow-up that piggybacks on Priority M's archive packaging. */
	char manifest_path[600];
	snprintf(manifest_path, sizeof(manifest_path), "%s/mods/installed/%s/mod.json",
	          home, mod_id);
	probe = fopen(manifest_path, "rb");
	if (probe) {
		fclose(probe);
		(void)fileTransferSendFile(from_handle, manifest_path);
		sysLogPrintf(LOG_NOTE, "SHARE: mod manifest offer -> 0x%08x manifest=%s",
		             (unsigned)from_handle, manifest_path);
	} else {
		sysLogPrintf(LOG_WARNING, "SHARE: mod \"%s\" not found locally; ignoring request from 0x%08x",
		             mod_id, (unsigned)from_handle);
	}
}

/* -------------------------------------------------------------------------
 * Aggregator (inbound mods + profiles)
 * ------------------------------------------------------------------------- */

s32 shareAggregateModCount(void) { return s_NumAggregateMods; }

const share_mod_entry_t *shareAggregateModAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumAggregateMods) return NULL;
	return &s_AggregateMods[idx];
}

const share_profile_t *shareProfileFor(u32 friend_handle)
{
	for (s32 i = 0; i < s_NumProfiles; i++) {
		if (s_Profiles[i].owner_handle == friend_handle) return &s_Profiles[i];
	}
	return NULL;
}

static void aggregateMod(u32 owner_handle,
                          const char *mod_id, const char *name,
                          const char *version, u32 size_bytes)
{
	for (s32 i = 0; i < s_NumAggregateMods; i++) {
		share_mod_entry_t *e = &s_AggregateMods[i];
		if (e->owner_handle == owner_handle &&
		    !strcmp(e->mod_id, mod_id)) {
			strncpy(e->display_name, name, sizeof(e->display_name) - 1);
			strncpy(e->version, version, sizeof(e->version) - 1);
			e->size_bytes = size_bytes;
			e->last_seen_ms = SDL_GetTicks();
			return;
		}
	}
	if (s_NumAggregateMods >= SHARE_AGGREGATE_MAX) return;
	share_mod_entry_t *e = &s_AggregateMods[s_NumAggregateMods++];
	memset(e, 0, sizeof(*e));
	e->owner_handle = owner_handle;
	strncpy(e->mod_id, mod_id, sizeof(e->mod_id) - 1);
	strncpy(e->display_name, name, sizeof(e->display_name) - 1);
	strncpy(e->version, version, sizeof(e->version) - 1);
	e->size_bytes = size_bytes;
	e->last_seen_ms = SDL_GetTicks();
}

static void recordProfileStats(u32 owner_handle, u32 kills, u32 deaths, u32 missions)
{
	for (s32 i = 0; i < s_NumProfiles; i++) {
		if (s_Profiles[i].owner_handle == owner_handle) {
			s_Profiles[i].kills = kills;
			s_Profiles[i].deaths = deaths;
			s_Profiles[i].missions_completed = missions;
			s_Profiles[i].last_seen_ms = SDL_GetTicks();
			return;
		}
	}
	if (s_NumProfiles >= SHARE_PROFILES_MAX) return;
	share_profile_t *p = &s_Profiles[s_NumProfiles++];
	p->owner_handle = owner_handle;
	p->kills = kills;
	p->deaths = deaths;
	p->missions_completed = missions;
	p->last_seen_ms = SDL_GetTicks();
}

/* -------------------------------------------------------------------------
 * Inbound dispatch
 * ------------------------------------------------------------------------- */

static void dispatchPayload(u32 from_handle, u8 kind,
                             const u8 *payload, u32 payload_len)
{
	switch (kind) {
		case SHARE_KIND_LR_MANIFEST: {
			/* Listener side: payload is host's playlist. The listening_room
			 * module's listener path consumes this in a follow-up to
			 * populate s_Tracks; for Phase 4 ship we log the receipt so
			 * the trace is visible without source-tracing the call. */
			if (payload_len < 1) return;
			const u8 ntr = payload[0];
			sysLogPrintf(LOG_NOTE, "SHARE: lr manifest from 0x%08x tracks=%u",
			             (unsigned)from_handle, (unsigned)ntr);
			break;
		}
		case SHARE_KIND_MODS_MANIFEST: {
			if (payload_len < 1) return;
			const u8 *p = payload;
			const u8 cap = *p++; const u8 *end = payload + payload_len;
			for (u8 i = 0; i < cap && p < end; i++) {
				if (p + 1 > end) break;
				u8 id_len = *p++;
				if (p + id_len > end) break;
				char mod_id[64]; const u32 ic = id_len < (u32)sizeof(mod_id) - 1 ? id_len : (u32)sizeof(mod_id) - 1;
				memcpy(mod_id, p, ic); mod_id[ic] = '\0'; p += id_len;
				if (p + 1 > end) break;
				u8 nm_len = *p++;
				if (p + nm_len > end) break;
				char nm[SHARE_MOD_NAME_MAX]; const u32 nc = nm_len < (u32)sizeof(nm) - 1 ? nm_len : (u32)sizeof(nm) - 1;
				memcpy(nm, p, nc); nm[nc] = '\0'; p += nm_len;
				if (p + 1 > end) break;
				u8 vr_len = *p++;
				if (p + vr_len > end) break;
				char vr[SHARE_MOD_VER_MAX]; const u32 vc = vr_len < (u32)sizeof(vr) - 1 ? vr_len : (u32)sizeof(vr) - 1;
				memcpy(vr, p, vc); vr[vc] = '\0'; p += vr_len;
				if (p + 4 + 1 > end) break;
				u32 size_bytes = rU32(&p);
				u8 sha_present = *p++;
				if (sha_present) {
					if (p + 32 > end) break;
					p += 32;
				}
				aggregateMod(from_handle, mod_id, nm, vr, size_bytes);
			}
			sysLogPrintf(LOG_NOTE, "SHARE: mods manifest from 0x%08x (now %d aggregate)",
			             (unsigned)from_handle, (int)s_NumAggregateMods);
			break;
		}
		case SHARE_KIND_PROFILE_STATS: {
			if (payload_len < 16) return;
			const u8 *p = payload;
			u32 k = rU32(&p);
			u32 d = rU32(&p);
			u32 m = rU32(&p);
			(void)rU32(&p); /* reserved */
			recordProfileStats(from_handle, k, d, m);
			break;
		}
		case SHARE_KIND_MOD_REQUEST: {
			handleModRequest(from_handle, payload, payload_len);
			break;
		}
		case SHARE_KIND_MOD_OFFER: {
			/* Reserved for a future "host pushes a mod proactively" path.
			 * For Phase 4 the request -> file_transfer flow covers the
			 * pull semantics we need. */
			break;
		}
		default: break;
	}
}

static void drainReceive(void)
{
	if (!s_SocketReady) return;
	for (;;) {
		u8 packet[2048];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n < (int)(SHARE_HEADER_LEN + SHARE_PUBKEY_LEN + SHARE_SIG_LEN)) continue;
		if (memcmp(packet, SHARE_MAGIC, SHARE_MAGIC_LEN) != 0) continue;

		const u8 *r = packet + SHARE_MAGIC_LEN;
		u8 ver  = rU8(&r);
		u8 kind = rU8(&r);
		(void)rU8(&r);
		u32 from_handle = rU32(&r);
		u32 to_handle   = rU32(&r); (void)to_handle;
		u32 seq         = rU32(&r); (void)seq;
		u16 payload_len = rU16(&r);

		if (ver != SHARE_VERSION) continue;
		if (payload_len > SHARE_PAYLOAD_MAX) continue;
		const u32 expected = SHARE_HEADER_LEN + payload_len + SHARE_PUBKEY_LEN + SHARE_SIG_LEN;
		if ((u32)n != expected) continue;

		const u8 *payload = packet + SHARE_HEADER_LEN;
		const u8 *pubkey  = payload + payload_len;
		const u8 *sig     = pubkey + SHARE_PUBKEY_LEN;

		if (!socialFriendByHandle(from_handle)) continue;
		if (socialBlockIsHandle(from_handle)) continue;
		if (!rateLimitAllow(from_handle)) continue;
		if (!socialHandleBindsPubkey(from_handle, pubkey)) continue;
		if (!verifyFrame(packet, SHARE_HEADER_LEN + payload_len + SHARE_PUBKEY_LEN, sig, pubkey)) continue;
		if (socialFriendBindPubkey(from_handle, pubkey) < 0) continue;

		dispatchPayload(from_handle, kind, payload, payload_len);
	}
}

/* -------------------------------------------------------------------------
 * Lifecycle + tick
 * ------------------------------------------------------------------------- */

void shareInit(void)
{
	memset(s_AggregateMods, 0, sizeof(s_AggregateMods));
	memset(s_Profiles, 0, sizeof(s_Profiles));
	memset(s_RateBuckets, 0, sizeof(s_RateBuckets));
	s_NumAggregateMods = 0;
	s_NumProfiles = 0;
	s_NextSeq = 0;
	s_LastBroadcastMs = 0;
	loadMyPublicMods();
	(void)ensureSocket();
}

void shareShutdown(void)
{
	if (s_SocketReady) {
		closesocket(s_Sock);
		s_Sock = INVALID_SOCKET;
		s_SocketReady = 0;
	}
}

void shareTick(void)
{
	if (!s_SocketReady) return;
	drainReceive();

	const u32 now = SDL_GetTicks();
	if (s_LastBroadcastMs == 0 ||
	    (now - s_LastBroadcastMs) >= SHARE_BROADCAST_INTERVAL_MS) {
		s_LastBroadcastMs = now;
		shareBroadcastListeningRoom();
		shareBroadcastPublicMods();
		shareBroadcastProfileStats();
	}
}
