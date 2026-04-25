/**
 * chat.c -- 1:1 private chat over a signed UDP socket on port 27106.
 *
 * Single writer per persistent file (`<home>/social/chat/<hex>.json`):
 * this module. Inbound text frames are verified (sig + handle bind +
 * TOFU recheck against the cached friend pubkey, done up the stack in
 * presence.c; chat.c only re-checks the signature against the embedded
 * pubkey in case the framing got out of order). Multi-fragment messages
 * are reassembled before being committed to history.
 */

#include "chat.h"
#include "social.h"
#include "identity.h"
#include "ed25519.h"
#include "system.h"
#include "fs.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
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

#define CHAT_PORT             27106
#define CHAT_MAGIC            "PDCHT"
#define CHAT_MAGIC_LEN        5
#define CHAT_VERSION          1
#define CHAT_BODY_LEN         224 /* bytes that the signature covers */
#define CHAT_PUBKEY_OFFSET    224
#define CHAT_PUBKEY_LEN       32
#define CHAT_SIG_OFFSET       256
#define CHAT_SIG_LEN          64
#define CHAT_FRAME_LEN        320
#define CHAT_DOMAIN_LEN       10  /* strlen("pd-chat-v1") */

#define CHAT_KIND_TEXT        0
#define CHAT_KIND_ACK         1

#define CHAT_REASSEMBLY_MAX   8   /* simultaneous in-flight fragments per peer */
#define CHAT_FRIENDS_MAX      128 /* matches SOCIAL_FRIENDS_MAX */
#define CHAT_RATE_WINDOW_MS   1000u
#define CHAT_RATE_MAX_FRAMES  16  /* per second per source handle */

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

typedef struct {
	u32              handle;
	s32              count;
	chat_message_t   ring[CHAT_HISTORY_MAX];
	s32              dirty;
} chat_history_t;

typedef struct {
	u32  src_handle;
	u64  msg_id;
	u16  expected_chunks;
	u16  received_chunks;
	u32  total_text_len;
	char text[CHAT_TEXT_MAX];
	u32  last_recv_ms;
	u8   chunk_present[CHAT_REASSEMBLY_MAX]; /* up to 8 chunks per message */
	u8   in_use;
} chat_reassembly_t;

typedef struct {
	u32 handle;
	u32 last_window_ms;
	u32 frames_in_window;
} chat_rate_t;

static chat_history_t   s_Histories[CHAT_FRIENDS_MAX];
static s32              s_NumHistories;

static chat_reassembly_t s_Reassembly[CHAT_REASSEMBLY_MAX];

static chat_rate_t      s_Rate[64];

static SOCKET s_Sock = INVALID_SOCKET;
static s32    s_SocketReady;
static u64    s_NextMsgIdCounter;

/* -------------------------------------------------------------------------
 * Endian helpers (presence-style direct memcpy, little-endian on wire)
 * ------------------------------------------------------------------------- */

static void wU8(u8 **p, u8 v)   { *(*p)++ = v; }
static void wU16(u8 **p, u16 v) { (*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); *p+=2; }
static void wU32(u8 **p, u32 v) {
	(*p)[0]=(u8)v; (*p)[1]=(u8)(v>>8); (*p)[2]=(u8)(v>>16); (*p)[3]=(u8)(v>>24); *p+=4;
}
static void wU64(u8 **p, u64 v) {
	for (s32 i = 0; i < 8; i++) { (*p)[i] = (u8)(v >> (8*i)); }
	*p += 8;
}
static u8  rU8 (const u8 **p) { return *(*p)++; }
static u16 rU16(const u8 **p) {
	u16 v = (u16)((*p)[0]) | ((u16)((*p)[1])<<8); *p+=2; return v;
}
static u32 rU32(const u8 **p) {
	u32 v = ((u32)((*p)[0])      ) | ((u32)((*p)[1])<< 8) |
	        ((u32)((*p)[2]) << 16) | ((u32)((*p)[3])<<24); *p+=4; return v;
}
static u64 rU64(const u8 **p) {
	u64 v = 0;
	for (s32 i = 0; i < 8; i++) v |= ((u64)((*p)[i])) << (8*i);
	*p += 8;
	return v;
}

/* -------------------------------------------------------------------------
 * Path helpers
 * ------------------------------------------------------------------------- */

static const char *chatDir(void)
{
	static char dir[512];
	if (dir[0]) return dir;
	char home[400];
	sysGetHomePath(home, sizeof(home));
	snprintf(dir, sizeof(dir), "%s/social/chat", home);
	fsCreateDir(dir);
	return dir;
}

static void historyPath(u32 handle, char *out, u32 outsize)
{
	snprintf(out, outsize, "%s/%08x.json", chatDir(), (unsigned)handle);
}

/* -------------------------------------------------------------------------
 * History accessors
 * ------------------------------------------------------------------------- */

static chat_history_t *findHistory(u32 handle)
{
	if (handle == 0) return NULL;
	for (s32 i = 0; i < s_NumHistories; i++) {
		if (s_Histories[i].handle == handle) return &s_Histories[i];
	}
	return NULL;
}

static chat_history_t *touchHistory(u32 handle)
{
	chat_history_t *h = findHistory(handle);
	if (h) return h;
	if (s_NumHistories >= CHAT_FRIENDS_MAX) return NULL;
	h = &s_Histories[s_NumHistories++];
	memset(h, 0, sizeof(*h));
	h->handle = handle;
	return h;
}

static void appendMessage(chat_history_t *h, const chat_message_t *m)
{
	if (!h) return;
	if (h->count < CHAT_HISTORY_MAX) {
		h->ring[h->count] = *m;
		h->count++;
	} else {
		memmove(&h->ring[0], &h->ring[1],
		        (size_t)(CHAT_HISTORY_MAX - 1) * sizeof(chat_message_t));
		h->ring[CHAT_HISTORY_MAX - 1] = *m;
	}
	h->dirty = 1;
}

/* -------------------------------------------------------------------------
 * JSON I/O (tokeniser identical to social_store.c)
 * ------------------------------------------------------------------------- */

typedef struct { const char *src; const char *p; } jread_t;
static void jSkipWs(jread_t *j) {
	while (*j->p && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}
static s32 jExpect(jread_t *j, char c) {
	jSkipWs(j); if (*j->p != c) return 0; j->p++; return 1;
}
static s32 jReadString(jread_t *j, char *dst, u32 dstsize) {
	jSkipWs(j); if (*j->p != '"') return 0; j->p++; u32 i = 0;
	while (*j->p && *j->p != '"') {
		if (*j->p == '\\' && j->p[1]) { j->p++; if (i + 1 < dstsize) dst[i++] = *j->p; j->p++; continue; }
		if (i + 1 < dstsize) dst[i++] = *j->p; j->p++;
	}
	if (*j->p == '"') j->p++;
	if (dstsize > 0) dst[i < dstsize ? i : dstsize - 1] = '\0';
	return 1;
}
static s32 jReadInt64(jread_t *j, s64 *out) {
	jSkipWs(j); const char *start = j->p;
	if (*j->p == '-') j->p++;
	while (*j->p >= '0' && *j->p <= '9') j->p++;
	if (j->p == start) return 0;
	*out = strtoll(start, NULL, 10); return 1;
}
static void jSkipValue(jread_t *j) {
	jSkipWs(j);
	if (*j->p == '"') { char tmp[8]; jReadString(j, tmp, sizeof(tmp)); return; }
	if (*j->p == '{' || *j->p == '[') {
		char open = *j->p++; char close = open == '{' ? '}' : ']';
		s32 depth = 1;
		while (*j->p && depth > 0) {
			if (*j->p == '"') { char tmp[8]; jReadString(j, tmp, sizeof(tmp)); continue; }
			if (*j->p == open) depth++; else if (*j->p == close) depth--;
			j->p++;
		}
		return;
	}
	while (*j->p && *j->p != ',' && *j->p != '}' && *j->p != ']') j->p++;
}

static char *slurpFile(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return NULL;
	fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
	if (sz < 0 || sz > 8 * 1024 * 1024) { fclose(f); return NULL; }
	char *buf = (char *)malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t got = fread(buf, 1, (size_t)sz, f); fclose(f); buf[got] = '\0';
	return buf;
}

static s32 writeAtomic(const char *path, const char *bytes, size_t len) {
	char tmp[600]; snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb"); if (!f) return -1;
	if (fwrite(bytes, 1, len, f) != len) { fclose(f); remove(tmp); return -1; }
	fflush(f); fclose(f);
#ifdef _WIN32
	remove(path);
#endif
	if (rename(tmp, path) != 0) { remove(tmp); return -1; }
	return 0;
}

/* JSON-escape a string segment into a growing buffer. */
static void appendJsonString(char *dst, size_t dstsize, size_t *plen, const char *src) {
	if (*plen + 2 > dstsize) return;
	dst[(*plen)++] = '"';
	while (*src && *plen + 2 < dstsize) {
		char c = *src++;
		if (c == '"' || c == '\\') {
			if (*plen + 2 >= dstsize) break;
			dst[(*plen)++] = '\\'; dst[(*plen)++] = c;
		} else if ((unsigned char)c < 0x20 && c != '\n') {
			continue;
		} else if (c == '\n') {
			if (*plen + 2 >= dstsize) break;
			dst[(*plen)++] = '\\'; dst[(*plen)++] = 'n';
		} else {
			dst[(*plen)++] = c;
		}
	}
	if (*plen + 1 < dstsize) dst[(*plen)++] = '"';
}
static void appendStr(char *dst, size_t dstsize, size_t *plen, const char *src) {
	while (*src && *plen + 1 < dstsize) dst[(*plen)++] = *src++;
}
static void appendFmt(char *dst, size_t dstsize, size_t *plen, const char *fmt, ...) {
	if (*plen >= dstsize) return;
	va_list ap; va_start(ap, fmt);
	int n = vsnprintf(dst + *plen, dstsize - *plen, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	*plen += (size_t)n < (dstsize - *plen) ? (size_t)n : (dstsize - *plen - 1);
}

static void saveHistory(chat_history_t *h)
{
	if (!h || !h->dirty) return;
	static char buf[128 * 1024];
	size_t len = 0;
	appendStr(buf, sizeof(buf), &len, "{\n  \"version\": 1,\n  \"messages\": [");
	for (s32 i = 0; i < h->count; i++) {
		const chat_message_t *m = &h->ring[i];
		appendStr(buf, sizeof(buf), &len, i == 0 ? "\n    " : ",\n    ");
		appendStr(buf, sizeof(buf), &len, "{ \"id\": ");
		appendFmt(buf, sizeof(buf), &len, "%llu", (unsigned long long)m->msg_id);
		appendFmt(buf, sizeof(buf), &len, ", \"dir\": %d", (int)m->direction);
		appendFmt(buf, sizeof(buf), &len, ", \"ts\": %u", (unsigned)m->timestamp_unix);
		appendStr(buf, sizeof(buf), &len, ", \"text\": ");
		appendJsonString(buf, sizeof(buf), &len, m->text);
		if (m->attachment_kind != 0) {
			appendFmt(buf, sizeof(buf), &len,
			          ", \"att\": { \"kind\": %u, \"size\": %llu, \"name\": ",
			          (unsigned)m->attachment_kind,
			          (unsigned long long)m->attachment_size);
			appendJsonString(buf, sizeof(buf), &len, m->attachment_name);
			appendStr(buf, sizeof(buf), &len, ", \"path\": ");
			appendJsonString(buf, sizeof(buf), &len, m->attachment_path);
			appendStr(buf, sizeof(buf), &len, " }");
		}
		appendStr(buf, sizeof(buf), &len, " }");
	}
	appendStr(buf, sizeof(buf), &len, "\n  ]\n}\n");

	char path[600];
	historyPath(h->handle, path, sizeof(path));
	if (writeAtomic(path, buf, len) != 0) {
		sysLogPrintf(LOG_WARNING, "CHAT: failed to write %s", path);
		return;
	}
	h->dirty = 0;
}

static void loadHistory(u32 handle)
{
	char path[600];
	historyPath(handle, path, sizeof(path));
	char *data = slurpFile(path);
	if (!data) return;

	chat_history_t *h = touchHistory(handle);
	if (!h) { free(data); return; }

	jread_t j = { data, data };
	if (!jExpect(&j, '{')) { free(data); return; }

	while (*j.p) {
		jSkipWs(&j);
		if (*j.p == '}') { j.p++; break; }
		if (*j.p == ',') { j.p++; continue; }
		char key[16];
		if (!jReadString(&j, key, sizeof(key))) break;
		if (!jExpect(&j, ':')) break;
		if (!strcmp(key, "messages")) {
			if (!jExpect(&j, '[')) break;
			while (*j.p) {
				jSkipWs(&j);
				if (*j.p == ']') { j.p++; break; }
				if (*j.p == ',') { j.p++; continue; }
				if (*j.p != '{') break; j.p++;

				chat_message_t m; memset(&m, 0, sizeof(m));
				m.peer_handle = handle;

				while (*j.p) {
					jSkipWs(&j);
					if (*j.p == '}') { j.p++; break; }
					if (*j.p == ',') { j.p++; continue; }
					char k2[16];
					if (!jReadString(&j, k2, sizeof(k2))) break;
					if (!jExpect(&j, ':')) break;
					if (!strcmp(k2, "id")) {
						s64 v = 0; jReadInt64(&j, &v); m.msg_id = (u64)v;
					} else if (!strcmp(k2, "dir")) {
						s64 v = 0; jReadInt64(&j, &v); m.direction = (chat_direction_t)v;
					} else if (!strcmp(k2, "ts")) {
						s64 v = 0; jReadInt64(&j, &v); m.timestamp_unix = (u32)v;
					} else if (!strcmp(k2, "text")) {
						jReadString(&j, m.text, sizeof(m.text));
					} else if (!strcmp(k2, "att")) {
						if (!jExpect(&j, '{')) { jSkipValue(&j); continue; }
						while (*j.p) {
							jSkipWs(&j);
							if (*j.p == '}') { j.p++; break; }
							if (*j.p == ',') { j.p++; continue; }
							char k3[12];
							if (!jReadString(&j, k3, sizeof(k3))) break;
							if (!jExpect(&j, ':')) break;
							if (!strcmp(k3, "kind")) {
								s64 v = 0; jReadInt64(&j, &v); m.attachment_kind = (u32)v;
							} else if (!strcmp(k3, "size")) {
								s64 v = 0; jReadInt64(&j, &v); m.attachment_size = (u64)v;
							} else if (!strcmp(k3, "name")) {
								jReadString(&j, m.attachment_name, sizeof(m.attachment_name));
							} else if (!strcmp(k3, "path")) {
								jReadString(&j, m.attachment_path, sizeof(m.attachment_path));
							} else { jSkipValue(&j); }
						}
					} else { jSkipValue(&j); }
				}

				appendMessage(h, &m);
			}
		} else { jSkipValue(&j); }
	}

	h->dirty = 0;
	free(data);
}

/* -------------------------------------------------------------------------
 * Reassembly
 * ------------------------------------------------------------------------- */

static chat_reassembly_t *findReassembly(u32 handle, u64 msg_id)
{
	for (s32 i = 0; i < CHAT_REASSEMBLY_MAX; i++) {
		if (s_Reassembly[i].in_use &&
		    s_Reassembly[i].src_handle == handle &&
		    s_Reassembly[i].msg_id == msg_id) {
			return &s_Reassembly[i];
		}
	}
	return NULL;
}

static chat_reassembly_t *allocReassembly(void)
{
	const u32 now = SDL_GetTicks();
	for (s32 i = 0; i < CHAT_REASSEMBLY_MAX; i++) {
		if (!s_Reassembly[i].in_use) return &s_Reassembly[i];
	}
	s32 oldest = 0;
	for (s32 i = 1; i < CHAT_REASSEMBLY_MAX; i++) {
		if (s_Reassembly[i].last_recv_ms < s_Reassembly[oldest].last_recv_ms) oldest = i;
	}
	memset(&s_Reassembly[oldest], 0, sizeof(s_Reassembly[oldest]));
	(void)now;
	return &s_Reassembly[oldest];
}

/* -------------------------------------------------------------------------
 * Rate limiter
 * ------------------------------------------------------------------------- */

static s32 rateLimitAllow(u32 handle)
{
	const u32 now = SDL_GetTicks();
	chat_rate_t *empty = NULL;
	chat_rate_t *oldest = &s_Rate[0];
	for (s32 i = 0; i < (s32)(sizeof(s_Rate)/sizeof(s_Rate[0])); i++) {
		chat_rate_t *r = &s_Rate[i];
		if (r->handle == handle) {
			if (now - r->last_window_ms >= CHAT_RATE_WINDOW_MS) {
				r->last_window_ms = now;
				r->frames_in_window = 0;
			}
			if (r->frames_in_window >= CHAT_RATE_MAX_FRAMES) return 0;
			r->frames_in_window++;
			return 1;
		}
		if (!r->handle && !empty) empty = r;
		if (r->last_window_ms < oldest->last_window_ms) oldest = r;
	}
	chat_rate_t *slot = empty ? empty : oldest;
	slot->handle = handle;
	slot->last_window_ms = now;
	slot->frames_in_window = 1;
	return 1;
}

/* -------------------------------------------------------------------------
 * Socket lifecycle
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

SOCKET chatGetSocket(void); /* shared with file_transfer.c */

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
	addr.sin_port = htons(CHAT_PORT);
	if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		addr.sin_port = 0;
		if (bind(s_Sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
			closesocket(s_Sock); s_Sock = INVALID_SOCKET; return INVALID_SOCKET;
		}
	}
	s_SocketReady = 1;
	sysLogPrintf(LOG_NOTE, "CHAT: socket bound on UDP %u", (unsigned)CHAT_PORT);
	return s_Sock;
}

SOCKET chatGetSocket(void) { return ensureSocket(); }

/* -------------------------------------------------------------------------
 * Wire frame I/O
 * ------------------------------------------------------------------------- */

static s32 signFrame(const u8 *body, u8 *outSig)
{
	if (!body || !outSig) return 0;
	if (!identityGetPubkey()) return 0;
	u8 buf[CHAT_BODY_LEN + CHAT_DOMAIN_LEN];
	memcpy(buf, body, CHAT_BODY_LEN);
	memcpy(buf + CHAT_BODY_LEN, CHAT_DOMAIN_TAG, CHAT_DOMAIN_LEN);
	return identitySign(buf, sizeof(buf), outSig);
}

static s32 verifyFrame(const u8 *body, const u8 *sig, const u8 *pubkey)
{
	if (!body || !sig || !pubkey) return 0;
	u8 buf[CHAT_BODY_LEN + CHAT_DOMAIN_LEN];
	memcpy(buf, body, CHAT_BODY_LEN);
	memcpy(buf + CHAT_BODY_LEN, CHAT_DOMAIN_TAG, CHAT_DOMAIN_LEN);
	return ed25519Verify(sig, buf, sizeof(buf), pubkey) == 1 ? 1 : 0;
}

static void sendChunk(u32 ipv4, u16 port, u32 target_handle,
                      u64 msg_id, u16 chunk_seq, u16 chunk_total,
                      const char *text, u16 text_len)
{
	if (!s_SocketReady) return;
	const u8 *mypub = identityGetPubkey();
	if (!mypub) return;

	u8 packet[CHAT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, CHAT_MAGIC, CHAT_MAGIC_LEN); w += CHAT_MAGIC_LEN;
	wU8(&w, CHAT_VERSION);
	wU8(&w, CHAT_KIND_TEXT);
	wU8(&w, 0);
	wU32(&w, socialMyHandle());
	wU32(&w, target_handle);
	wU64(&w, msg_id);
	wU16(&w, text_len);
	wU16(&w, chunk_seq);
	wU16(&w, chunk_total);
	wU16(&w, 0);
	if (text_len > 0) memcpy(packet + 32, text, text_len);
	memcpy(packet + CHAT_PUBKEY_OFFSET, mypub, CHAT_PUBKEY_LEN);
	if (!signFrame(packet, packet + CHAT_SIG_OFFSET)) return;

	struct sockaddr_in dst;
	memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_addr.s_addr = htonl(ipv4);
	dst.sin_port = htons(port);
	(void)sendto(s_Sock, (const char *)packet, CHAT_FRAME_LEN, 0,
	             (struct sockaddr *)&dst, sizeof(dst));
}

/* -------------------------------------------------------------------------
 * Public lifecycle
 * ------------------------------------------------------------------------- */

void chatInit(void)
{
	memset(s_Histories, 0, sizeof(s_Histories));
	memset(s_Reassembly, 0, sizeof(s_Reassembly));
	memset(s_Rate, 0, sizeof(s_Rate));
	s_NumHistories = 0;
	s_NextMsgIdCounter = 0;

	(void)chatDir();
	(void)ensureSocket();

	const s32 nf = socialFriendCount();
	for (s32 i = 0; i < nf; i++) {
		const social_friend_t *f = socialFriendAt(i);
		if (f) loadHistory(f->handle);
	}
	sysLogPrintf(LOG_NOTE, "CHAT: ready histories=%d", (int)s_NumHistories);
}

void chatShutdown(void)
{
	for (s32 i = 0; i < s_NumHistories; i++) {
		saveHistory(&s_Histories[i]);
	}
	if (s_SocketReady) {
		closesocket(s_Sock);
		s_Sock = INVALID_SOCKET;
		s_SocketReady = 0;
	}
}

/* -------------------------------------------------------------------------
 * Inbound dispatch
 * ------------------------------------------------------------------------- */

/* Resolve the target endpoint for a friend handle the same way presence
 * does, but without the indirection back into presence.c (which would
 * pull a circular dependency). We rely on the persistent endpoint
 * cache and the friend record. */
static s32 resolveFriendEndpoint(u32 handle, u32 *out_ipv4, u16 *out_port)
{
	if (socialFriendGetEndpoint(handle, out_ipv4, out_port)) {
		if (*out_port == 0) *out_port = CHAT_PORT;
		return 1;
	}
	return 0;
}

static void commitInboundMessage(u32 src_handle, u64 msg_id,
                                  const char *text, u32 text_len)
{
	const social_friend_t *f = socialFriendByHandle(src_handle);
	if (!f) return; /* DoS gate -- only friends */
	if (socialBlockIsHandle(src_handle)) return;

	chat_history_t *h = touchHistory(src_handle);
	if (!h) return;

	chat_message_t m; memset(&m, 0, sizeof(m));
	m.msg_id = msg_id;
	m.peer_handle = src_handle;
	m.direction = CHAT_DIR_IN;
	m.timestamp_unix = (u32)time(NULL);
	if (text_len >= sizeof(m.text)) text_len = sizeof(m.text) - 1;
	memcpy(m.text, text, text_len);
	m.text[text_len] = '\0';

	/* De-dup against the most recent N messages by msg_id. */
	for (s32 i = h->count - 1; i >= 0 && i >= h->count - 8; i--) {
		if (h->ring[i].msg_id == msg_id && h->ring[i].direction == CHAT_DIR_IN) {
			return;
		}
	}

	appendMessage(h, &m);
	saveHistory(h);
	sysLogPrintf(LOG_NOTE, "CHAT: in <- 0x%08x (%u bytes)",
	             (unsigned)src_handle, (unsigned)text_len);
}

static void drainReceive(void)
{
	if (!s_SocketReady) return;
	for (;;) {
		u8 packet[1024];
		struct sockaddr_in src;
		socklen_t srclen = sizeof(src);
		int n = recvfrom(s_Sock, (char *)packet, sizeof(packet), 0,
		                 (struct sockaddr *)&src, &srclen);
		if (n <= 0) break;
		if (n != CHAT_FRAME_LEN) continue;
		if (memcmp(packet, CHAT_MAGIC, CHAT_MAGIC_LEN) != 0) continue;

		const u8 *p = packet + CHAT_MAGIC_LEN;
		u8 ver  = rU8(&p);
		u8 kind = rU8(&p);
		(void)rU8(&p);  /* flags */
		u32 from_handle = rU32(&p);
		u32 to_handle   = rU32(&p);
		u64 msg_id      = rU64(&p);
		u16 text_len    = rU16(&p);
		u16 chunk_seq   = rU16(&p);
		u16 chunk_total = rU16(&p);
		(void)rU16(&p);  /* pad */
		(void)to_handle;

		if (ver != CHAT_VERSION) continue;
		if (kind != CHAT_KIND_TEXT) continue;
		if (!rateLimitAllow(from_handle)) continue;
		if (!socialFriendByHandle(from_handle)) continue;
		if (socialBlockIsHandle(from_handle)) continue;

		const u8 *sender_pub = packet + CHAT_PUBKEY_OFFSET;
		const u8 *sender_sig = packet + CHAT_SIG_OFFSET;

		if (!socialHandleBindsPubkey(from_handle, sender_pub)) continue;
		if (!verifyFrame(packet, sender_sig, sender_pub)) continue;
		if (socialFriendBindPubkey(from_handle, sender_pub) < 0) continue;

		if (text_len > CHAT_FRAME_PAYLOAD_LEN) continue;
		if (chunk_total == 0 || chunk_total > CHAT_REASSEMBLY_MAX) continue;
		if (chunk_seq >= chunk_total) continue;

		if (chunk_total == 1) {
			char text[CHAT_TEXT_MAX];
			u32 cap = (text_len < sizeof(text) - 1) ? text_len : (u32)sizeof(text) - 1;
			memcpy(text, packet + 32, cap);
			text[cap] = '\0';
			commitInboundMessage(from_handle, msg_id, text, cap);
			continue;
		}

		chat_reassembly_t *r = findReassembly(from_handle, msg_id);
		if (!r) {
			r = allocReassembly();
			r->in_use = 1;
			r->src_handle = from_handle;
			r->msg_id = msg_id;
			r->expected_chunks = chunk_total;
			r->received_chunks = 0;
			r->total_text_len = 0;
			memset(r->chunk_present, 0, sizeof(r->chunk_present));
			memset(r->text, 0, sizeof(r->text));
		}
		r->last_recv_ms = SDL_GetTicks();

		if (r->expected_chunks != chunk_total) continue;
		if (chunk_seq >= CHAT_REASSEMBLY_MAX) continue;
		if (r->chunk_present[chunk_seq]) continue;

		const u32 dst_off = (u32)chunk_seq * CHAT_FRAME_PAYLOAD_LEN;
		if (dst_off + text_len > sizeof(r->text)) continue;
		memcpy(r->text + dst_off, packet + 32, text_len);
		r->chunk_present[chunk_seq] = 1;
		r->received_chunks++;
		if (chunk_seq == chunk_total - 1) {
			r->total_text_len = dst_off + text_len;
		}

		if (r->received_chunks == r->expected_chunks) {
			commitInboundMessage(from_handle, msg_id, r->text, r->total_text_len);
			memset(r, 0, sizeof(*r));
		}
	}

	/* Prune stale reassemblies after 10s of inactivity. */
	const u32 now = SDL_GetTicks();
	for (s32 i = 0; i < CHAT_REASSEMBLY_MAX; i++) {
		if (s_Reassembly[i].in_use && (now - s_Reassembly[i].last_recv_ms) > 10000u) {
			memset(&s_Reassembly[i], 0, sizeof(s_Reassembly[i]));
		}
	}
}

void chatTick(void)
{
	if (!s_SocketReady) return;
	drainReceive();

	/* Flush dirty histories at most once per second to amortise disk. */
	static u32 s_LastFlushMs;
	const u32 now = SDL_GetTicks();
	if (now - s_LastFlushMs >= 1000u) {
		s_LastFlushMs = now;
		for (s32 i = 0; i < s_NumHistories; i++) {
			if (s_Histories[i].dirty) saveHistory(&s_Histories[i]);
		}
	}
}

/* -------------------------------------------------------------------------
 * Sending
 * ------------------------------------------------------------------------- */

s32 chatSendText(u32 friend_handle, const char *text)
{
	if (friend_handle == 0 || !text || !*text) return -1;
	if (socialBlockIsHandle(friend_handle)) return -1;
	if (!socialFriendByHandle(friend_handle)) return -1;
	if (!s_SocketReady) (void)ensureSocket();
	if (!s_SocketReady) return -1;

	const u32 text_len = (u32)strnlen(text, CHAT_TEXT_MAX - 1);
	if (text_len == 0) return -1;

	u32 ipv4 = 0; u16 port = 0;
	if (!resolveFriendEndpoint(friend_handle, &ipv4, &port)) return -1;

	const u64 msg_id = ((u64)time(NULL) << 16) | (s_NextMsgIdCounter++ & 0xFFFFu);
	const u16 chunk_total = (u16)((text_len + CHAT_FRAME_PAYLOAD_LEN - 1) / CHAT_FRAME_PAYLOAD_LEN);

	u32 sent = 0;
	for (u16 seq = 0; seq < chunk_total; seq++) {
		const u32 chunk_off = (u32)seq * CHAT_FRAME_PAYLOAD_LEN;
		const u32 chunk_len = (text_len - chunk_off > CHAT_FRAME_PAYLOAD_LEN)
		                       ? CHAT_FRAME_PAYLOAD_LEN
		                       : (text_len - chunk_off);
		sendChunk(ipv4, port, friend_handle,
		          msg_id, seq, chunk_total, text + chunk_off, (u16)chunk_len);
		sent += chunk_len;
	}
	(void)sent;

	chat_history_t *h = touchHistory(friend_handle);
	if (h) {
		chat_message_t m; memset(&m, 0, sizeof(m));
		m.msg_id = msg_id;
		m.peer_handle = friend_handle;
		m.direction = CHAT_DIR_OUT;
		m.timestamp_unix = (u32)time(NULL);
		strncpy(m.text, text, sizeof(m.text) - 1);
		appendMessage(h, &m);
		saveHistory(h);
	}

	sysLogPrintf(LOG_NOTE, "CHAT: out -> 0x%08x (%u bytes, %u frames)",
	             (unsigned)friend_handle, (unsigned)text_len, (unsigned)chunk_total);
	return 0;
}

/* -------------------------------------------------------------------------
 * History accessors (UI side)
 * ------------------------------------------------------------------------- */

s32 chatHistoryCount(u32 friend_handle)
{
	const chat_history_t *h = findHistory(friend_handle);
	return h ? h->count : 0;
}

const chat_message_t *chatHistoryAt(u32 friend_handle, s32 idx)
{
	const chat_history_t *h = findHistory(friend_handle);
	if (!h) return NULL;
	if (idx < 0 || idx >= h->count) return NULL;
	return &h->ring[idx];
}

void chatHistoryClear(u32 friend_handle)
{
	chat_history_t *h = findHistory(friend_handle);
	if (!h) return;
	h->count = 0;
	memset(h->ring, 0, sizeof(h->ring));
	h->dirty = 1;
	char path[600];
	historyPath(friend_handle, path, sizeof(path));
	remove(path);
}

s32 chatHistoryAppendSystem(u32 friend_handle, const char *text)
{
	if (!text) return -1;
	chat_history_t *h = touchHistory(friend_handle);
	if (!h) return -1;
	chat_message_t m; memset(&m, 0, sizeof(m));
	m.peer_handle = friend_handle;
	m.direction = CHAT_DIR_SYS;
	m.timestamp_unix = (u32)time(NULL);
	strncpy(m.text, text, sizeof(m.text) - 1);
	appendMessage(h, &m);
	return 0;
}

s32 chatHistoryAppendAttachment(u32 friend_handle,
                                u32 attachment_kind,
                                const char *attachment_name,
                                const char *attachment_path,
                                u64 size)
{
	chat_history_t *h = touchHistory(friend_handle);
	if (!h) return -1;
	chat_message_t m; memset(&m, 0, sizeof(m));
	m.peer_handle = friend_handle;
	m.direction = CHAT_DIR_IN;
	m.timestamp_unix = (u32)time(NULL);
	m.attachment_kind = attachment_kind;
	m.attachment_size = size;
	if (attachment_name) {
		strncpy(m.attachment_name, attachment_name, sizeof(m.attachment_name) - 1);
	}
	if (attachment_path) {
		strncpy(m.attachment_path, attachment_path, sizeof(m.attachment_path) - 1);
	}
	appendMessage(h, &m);
	saveHistory(h);
	return 0;
}
