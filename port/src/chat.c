/**
 * chat.c -- Signed private chat over the discovered presence socket.
 *
 * Single writer per persistent file (`<home>/social/chat/<hex>.json`):
 * this module. Chat verifies the signature, recipient, sender handle and
 * cached friend key after presence demultiplexes the datagram. Multi-fragment messages
 * are reassembled before being committed to history.
 */

#include "chat.h"
#include "presence.h"
#include "save_atomic.h"
#include "modasset_json.h"
#include "sha256.h"
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

#define CHAT_MAGIC            "PDCHT"
#define CHAT_MAGIC_LEN        5
#define CHAT_VERSION          1
#define CHAT_BODY_LEN         224 /* bytes that the signature covers */
#define CHAT_PUBKEY_OFFSET    224
#define CHAT_PUBKEY_LEN       32
#define CHAT_SIG_OFFSET       256
#define CHAT_SIG_LEN          64
#define CHAT_FRAME_LEN        CHAT_WIRE_FRAME_LEN
#define CHAT_DOMAIN_LEN       10  /* strlen("pd-chat-v1") */

#define CHAT_KIND_TEXT        0
#define CHAT_KIND_ACK         1

#define CHAT_REASSEMBLY_MAX   8   /* simultaneous in-flight fragments per peer */
#define CHAT_FRIENDS_MAX      128 /* matches SOCIAL_FRIENDS_MAX */
#define CHAT_RATE_WINDOW_MS   1000u
#define CHAT_RATE_MAX_FRAMES  16  /* per second per source handle */
#define CHAT_RECEIPT_MAX      512 /* Covers 30 seconds at the inbound rate cap. */
#define CHAT_RECEIPT_TTL_MS   30000u

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

typedef struct {
	u64 msg_id;
	u32 accepted_ms;
	u8 hash[SHA256_DIGEST_SIZE];
	u8 in_use;
} chat_receipt_t;

typedef struct {
	u32              handle;
	s32              count;
	chat_message_t   ring[CHAT_HISTORY_MAX];
	chat_receipt_t   receipts[CHAT_RECEIPT_MAX];
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
	u32  first_recv_ms;
	u16  chunk_lengths[CHAT_REASSEMBLY_MAX];
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

static s32    s_Initialized;
static u32    s_ActiveHandle;
static const char *s_LastSendError = "";
static u64    s_NextMsgIdCounter;

#define CHAT_PENDING_MAX 16
#define CHAT_PENDING_PER_FRIEND 4
#define CHAT_RETRY_LIMIT 5
#define CHAT_DELIVERY_TIMEOUT_MS 15000u
typedef struct {
	u32 handle, queued_ms, last_send_ms;
	u64 msg_id;
	u8 attempts, in_use;
	char text[CHAT_TEXT_MAX];
} chat_pending_t;
static chat_pending_t s_Pending[CHAT_PENDING_MAX];

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
	jSkipWs(j);
	if (*j->p != '"') return 0;
	const char *start = j->p++;
	while (*j->p) {
		if (*j->p == '\\' && j->p[1]) { j->p += 2; continue; }
		if (*j->p++ == '"') return modAssetJsonDecodeString(start, j->p, dst, dstsize);
	}
	return 0;
}
static s32 jReadInt64(jread_t *j, s64 *out) {
	jSkipWs(j); const char *start = j->p;
	if (*j->p == '-') j->p++;
	while (*j->p >= '0' && *j->p <= '9') j->p++;
	if (j->p == start) return 0;
	*out = strtoll(start, NULL, 10); return 1;
}
static s32 jReadUint64(jread_t *j, u64 *out) {
	jSkipWs(j);
	if (*j->p < '0' || *j->p > '9') return 0;
	u64 value = 0;
	while (*j->p >= '0' && *j->p <= '9') {
		const u64 digit = (u64)(*j->p - '0');
		if (value > (~(u64)0 - digit) / 10) return 0;
		value = value * 10 + digit;
		++j->p;
	}
	*out = value;
	return 1;
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
	save_atomic_file_t transaction;
	if (saveAtomicBegin(&transaction, path) != 0) return -1;
	if (fwrite(bytes, 1, len, saveAtomicStream(&transaction)) != len) {
		saveAtomicAbort(&transaction);
		return -1;
	}
	return saveAtomicCommit(&transaction);
}

/* JSON-escape a string segment into a growing buffer. */
static void appendJsonString(char *dst, size_t dstsize, size_t *plen, const char *src) {
	if (*plen >= dstsize || dstsize - *plen < 2) { *plen = dstsize; return; }
	dst[(*plen)++] = '"';
	while (*src) {
		const unsigned char c = (unsigned char)*src++;
		const size_t needed = c < 0x20 ? 6 : c == '"' || c == '\\' ? 2 : 1;
		if (*plen >= dstsize || needed + 1 >= dstsize - *plen) { *plen = dstsize; return; }
		if (c == '"' || c == '\\') {
			dst[(*plen)++] = '\\'; dst[(*plen)++] = c;
		} else if (c < 0x20) {
			static const char hex[] = "0123456789abcdef";
			memcpy(dst + *plen, "\\u00", 4); *plen += 4;
			dst[(*plen)++] = hex[c >> 4]; dst[(*plen)++] = hex[c & 15];
		} else {
			dst[(*plen)++] = c;
		}
	}
	dst[(*plen)++] = '"';
}
static void appendStr(char *dst, size_t dstsize, size_t *plen, const char *src) {
	while (*src && *plen + 1 < dstsize) dst[(*plen)++] = *src++;
	if (*src) *plen = dstsize;
}
static void appendFmt(char *dst, size_t dstsize, size_t *plen, const char *fmt, ...) {
	if (*plen >= dstsize) return;
	va_list ap; va_start(ap, fmt);
	int n = vsnprintf(dst + *plen, dstsize - *plen, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= dstsize - *plen) { *plen = dstsize; return; }
	*plen += (size_t)n;
}

static void saveHistory(chat_history_t *h)
{
	if (!h || !h->dirty) return;
	/* Worst-case JSON escaping of every bounded text/attachment byte. */
	static char buf[CHAT_HISTORY_MAX * (6 * (CHAT_TEXT_MAX + 64 + 256) + 512) + 128];
	size_t len = 0;
	appendStr(buf, sizeof(buf), &len, "{\n  \"version\": 1,\n  \"messages\": [");
	for (s32 i = 0; i < h->count; i++) {
		const chat_message_t *m = &h->ring[i];
		appendStr(buf, sizeof(buf), &len, i == 0 ? "\n    " : ",\n    ");
		appendStr(buf, sizeof(buf), &len, "{ \"id\": ");
		appendFmt(buf, sizeof(buf), &len, "%llu", (unsigned long long)m->msg_id);
		appendFmt(buf, sizeof(buf), &len, ", \"dir\": %d", (int)m->direction);
		appendFmt(buf, sizeof(buf), &len, ", \"delivery\": %d", (int)m->delivery);
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
	if (len >= sizeof(buf)) {
		sysLogPrintf(LOG_WARNING, "CHAT: history serialization exceeds bounded buffer");
		return;
	}

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
						if (!jReadUint64(&j, &m.msg_id)) { free(data); return; }
					} else if (!strcmp(k2, "dir")) {
						s64 v = 0; jReadInt64(&j, &v); m.direction = (chat_direction_t)v;
					} else if (!strcmp(k2, "ts")) {
						s64 v = 0; jReadInt64(&j, &v); m.timestamp_unix = (u32)v;
					} else if (!strcmp(k2, "delivery")) {
						s64 v = 0; jReadInt64(&j, &v);
						m.delivery = v >= CHAT_DELIVERY_UNKNOWN && v <= CHAT_DELIVERY_FAILED
							? (chat_delivery_t)v : CHAT_DELIVERY_UNKNOWN;
						if (m.delivery == CHAT_DELIVERY_PENDING) m.delivery = CHAT_DELIVERY_FAILED;
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

static s32 sendChunk(u8 kind, u32 target_handle,
                      u64 msg_id, u16 chunk_seq, u16 chunk_total,
                      const char *text, u16 text_len)
{
	if (!s_Initialized || !presenceIsAgentLoaded()) return -1;
	const u8 *mypub = identityGetPubkey();
	if (!mypub) return -1;

	u8 packet[CHAT_FRAME_LEN];
	memset(packet, 0, sizeof(packet));
	u8 *w = packet;
	memcpy(w, CHAT_MAGIC, CHAT_MAGIC_LEN); w += CHAT_MAGIC_LEN;
	wU8(&w, CHAT_VERSION);
	wU8(&w, kind);
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
	if (!signFrame(packet, packet + CHAT_SIG_OFFSET)) return -1;

	return presenceSendChatFrame(target_handle, packet, sizeof(packet));
}

static void setDelivery(u32 handle, u64 msg_id, chat_delivery_t delivery)
{
	chat_history_t *h = findHistory(handle);
	if (!h) return;
	for (s32 i = h->count - 1; i >= 0; --i) {
		chat_message_t *m = &h->ring[i];
		if (m->direction == CHAT_DIR_OUT && m->msg_id == msg_id) {
			m->delivery = delivery;
			h->dirty = 1;
			return;
		}
	}
}

static void sendPending(chat_pending_t *pending, u32 now)
{
	const u32 length = (u32)strlen(pending->text);
	const u16 count = (u16)((length + CHAT_FRAME_PAYLOAD_LEN - 1) / CHAT_FRAME_PAYLOAD_LEN);
	pending->last_send_ms = now;
	++pending->attempts;
	for (u16 seq = 0; seq < count; ++seq) {
		const u32 offset = (u32)seq * CHAT_FRAME_PAYLOAD_LEN;
		const u16 bytes = (u16)(length - offset > CHAT_FRAME_PAYLOAD_LEN
			? CHAT_FRAME_PAYLOAD_LEN : length - offset);
		if (sendChunk(CHAT_KIND_TEXT, pending->handle, pending->msg_id,
			seq, count, pending->text + offset, bytes) != 0) break;
	}
}

static void refreshChatIdentity(void)
{
	const u32 handle = socialMyHandle();
	if (handle == s_ActiveHandle) return;
	for (s32 i = 0; i < CHAT_PENDING_MAX; ++i) {
		if (s_Pending[i].in_use)
			setDelivery(s_Pending[i].handle, s_Pending[i].msg_id, CHAT_DELIVERY_FAILED);
	}
	memset(s_Pending, 0, sizeof(s_Pending));
	memset(s_Reassembly, 0, sizeof(s_Reassembly));
	s_ActiveHandle = handle;
}

static void acknowledgeInbound(u32 handle, u64 msg_id)
{
	(void)sendChunk(CHAT_KIND_ACK, handle, msg_id, 0, 0, NULL, 0);
}

/* -------------------------------------------------------------------------
 * Public lifecycle
 * ------------------------------------------------------------------------- */

void chatInit(void)
{
	memset(s_Histories, 0, sizeof(s_Histories));
	memset(s_Reassembly, 0, sizeof(s_Reassembly));
	memset(s_Rate, 0, sizeof(s_Rate));
	memset(s_Pending, 0, sizeof(s_Pending));
	s_NumHistories = 0;
	s_NextMsgIdCounter = SDL_GetPerformanceCounter();

	(void)chatDir();
	s_Initialized = 1;
	s_ActiveHandle = socialMyHandle();

	const s32 nf = socialFriendCount();
	for (s32 i = 0; i < nf; i++) {
		const social_friend_t *f = socialFriendAt(i);
		if (f) loadHistory(f->handle);
	}
	sysLogPrintf(LOG_NOTE, "CHAT: ready histories=%d", (int)s_NumHistories);
}

void chatShutdown(void)
{
	for (s32 i = 0; i < CHAT_PENDING_MAX; ++i) {
		if (s_Pending[i].in_use)
			setDelivery(s_Pending[i].handle, s_Pending[i].msg_id, CHAT_DELIVERY_FAILED);
	}
	memset(s_Pending, 0, sizeof(s_Pending));
	for (s32 i = 0; i < s_NumHistories; i++) {
		saveHistory(&s_Histories[i]);
	}
	s_Initialized = 0;
	memset(s_Reassembly, 0, sizeof(s_Reassembly));
}

/* -------------------------------------------------------------------------
 * Inbound dispatch
 * ------------------------------------------------------------------------- */

static void commitInboundMessage(u32 src_handle, u64 msg_id,
                                  const char *text, u32 text_len)
{
	const social_friend_t *f = socialFriendByHandle(src_handle);
	if (!f) return; /* DoS gate -- only friends */
	if (socialBlockIsHandle(src_handle)) return;

	chat_history_t *h = touchHistory(src_handle);
	if (!h) return;

	/* Receipt lifetime is independent of outgoing/system history churn. */
	u8 hash[SHA256_DIGEST_SIZE];
	sha256Hash(text, text_len, hash);
	const u32 accepted_ms = SDL_GetTicks();
	chat_receipt_t *available = NULL;
	for (s32 i = 0; i < CHAT_RECEIPT_MAX; ++i) {
		chat_receipt_t *receipt = &h->receipts[i];
		if (receipt->in_use && accepted_ms - receipt->accepted_ms >= CHAT_RECEIPT_TTL_MS)
			receipt->in_use = 0;
		if (!receipt->in_use) { if (!available) available = receipt; continue; }
		if (receipt->msg_id == msg_id) {
			if (!memcmp(receipt->hash, hash, sizeof(hash))) acknowledgeInbound(src_handle, msg_id);
			return;
		}
	}
	if (!available) return; /* Bounded admission; do not evict a live receipt. */

	chat_message_t m; memset(&m, 0, sizeof(m));
	m.msg_id = msg_id;
	m.peer_handle = src_handle;
	m.direction = CHAT_DIR_IN;
	m.timestamp_unix = (u32)time(NULL);
	if (text_len >= sizeof(m.text)) text_len = sizeof(m.text) - 1;
	memcpy(m.text, text, text_len);
	m.text[text_len] = '\0';

	/* Re-ACK a retransmission without duplicating accepted history. */
	for (s32 i = h->count - 1; i >= 0; i--) {
		if (h->ring[i].msg_id == msg_id && h->ring[i].direction == CHAT_DIR_IN) {
			if (strlen(h->ring[i].text) == text_len && !memcmp(h->ring[i].text, text, text_len)) {
				available->in_use = 1; available->msg_id = msg_id; available->accepted_ms = accepted_ms;
				memcpy(available->hash, hash, sizeof(hash));
				acknowledgeInbound(src_handle, msg_id);
			}
			return;
		}
	}

	available->in_use = 1; available->msg_id = msg_id; available->accepted_ms = accepted_ms;
	memcpy(available->hash, hash, sizeof(hash));
	appendMessage(h, &m);
	saveHistory(h);
	acknowledgeInbound(src_handle, msg_id);
	sysLogPrintf(LOG_NOTE, "CHAT: in <- 0x%08x (%u bytes)",
	             (unsigned)src_handle, (unsigned)text_len);
}

void chatReceiveFrame(const u8 *packet, u32 length)
{
	if (!s_Initialized || !presenceIsAgentLoaded() || !packet
		|| length != CHAT_FRAME_LEN || memcmp(packet, CHAT_MAGIC, CHAT_MAGIC_LEN)) return;
	refreshChatIdentity();
	const u8 *p = packet + CHAT_MAGIC_LEN;
	u8 ver  = rU8(&p);
	u8 kind = rU8(&p);
	u8 flags = rU8(&p);
	u32 from_handle = rU32(&p);
	u32 to_handle   = rU32(&p);
	u64 msg_id      = rU64(&p);
	u16 text_len    = rU16(&p);
	u16 chunk_seq   = rU16(&p);
	u16 chunk_total = rU16(&p);
	u16 reserved = rU16(&p);
	if (flags || reserved) return;
	if (!to_handle || to_handle != socialMyHandle()) return;

	if (ver != CHAT_VERSION) return;
	if (kind != CHAT_KIND_TEXT && kind != CHAT_KIND_ACK) return;
	if (!rateLimitAllow(from_handle)) return;
	if (!socialFriendByHandle(from_handle)) return;
	if (socialBlockIsHandle(from_handle)) return;

	const u8 *sender_pub = packet + CHAT_PUBKEY_OFFSET;
	const u8 *sender_sig = packet + CHAT_SIG_OFFSET;

	if (!socialHandleBindsPubkey(from_handle, sender_pub)) return;
	if (!verifyFrame(packet, sender_sig, sender_pub)) return;
	if (socialFriendBindPubkey(from_handle, sender_pub) < 0) return;
	if (kind == CHAT_KIND_ACK) {
		if (text_len || chunk_seq || chunk_total) return;
		for (s32 i = 0; i < CHAT_PENDING_MAX; ++i) {
			chat_pending_t *pending = &s_Pending[i];
			if (pending->in_use && pending->handle == from_handle && pending->msg_id == msg_id) {
				setDelivery(from_handle, msg_id, CHAT_DELIVERY_DELIVERED);
				memset(pending, 0, sizeof(*pending));
				break;
			}
		}
		return;
	}

	if (!text_len || text_len > CHAT_FRAME_PAYLOAD_LEN) return;
	if (memchr(packet + 32, 0, text_len)) return;
	if (chunk_total == 0 || chunk_total > (CHAT_TEXT_MAX - 2) / CHAT_FRAME_PAYLOAD_LEN + 1) return;
	if (chunk_seq >= chunk_total) return;
	if (chunk_seq + 1 < chunk_total && text_len != CHAT_FRAME_PAYLOAD_LEN) return;
	if ((u32)chunk_seq * CHAT_FRAME_PAYLOAD_LEN + text_len >= CHAT_TEXT_MAX) return;

	if (chunk_total == 1) {
		char text[CHAT_TEXT_MAX];
		u32 cap = (text_len < sizeof(text) - 1) ? text_len : (u32)sizeof(text) - 1;
		memcpy(text, packet + 32, cap);
		text[cap] = '\0';
		commitInboundMessage(from_handle, msg_id, text, cap);
		return;
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
		r->first_recv_ms = SDL_GetTicks();
		memset(r->chunk_lengths, 0, sizeof(r->chunk_lengths));
		memset(r->chunk_present, 0, sizeof(r->chunk_present));
		memset(r->text, 0, sizeof(r->text));
	}
	r->last_recv_ms = SDL_GetTicks();

	if (r->expected_chunks != chunk_total || r->last_recv_ms - r->first_recv_ms >= CHAT_DELIVERY_TIMEOUT_MS) {
		memset(r, 0, sizeof(*r));
		return;
	}
	if (chunk_seq >= CHAT_REASSEMBLY_MAX) return;

	const u32 dst_off = (u32)chunk_seq * CHAT_FRAME_PAYLOAD_LEN;
	if (r->chunk_present[chunk_seq]) {
		if (r->chunk_lengths[chunk_seq] != text_len || memcmp(r->text + dst_off, packet + 32, text_len))
			memset(r, 0, sizeof(*r));
		return;
	}
	if (dst_off + text_len > sizeof(r->text)) return;
	memcpy(r->text + dst_off, packet + 32, text_len);
	r->chunk_present[chunk_seq] = 1;
	r->chunk_lengths[chunk_seq] = text_len;
	r->received_chunks++;
	if (chunk_seq == chunk_total - 1) {
		r->total_text_len = dst_off + text_len;
	}

	if (r->received_chunks == r->expected_chunks) {
		commitInboundMessage(from_handle, msg_id, r->text, r->total_text_len);
		memset(r, 0, sizeof(*r));
	}
}

void chatTick(void)
{
	if (!s_Initialized) return;
	refreshChatIdentity();

	/* Prune stale reassemblies after 10s of inactivity. */
	const u32 now = SDL_GetTicks();
	for (s32 i = 0; i < CHAT_PENDING_MAX; ++i) {
		chat_pending_t *pending = &s_Pending[i];
		if (!pending->in_use) continue;
		if (!presenceIsAgentLoaded() || socialBlockIsHandle(pending->handle)
			|| !socialFriendByHandle(pending->handle)
			|| now - pending->queued_ms >= CHAT_DELIVERY_TIMEOUT_MS) {
			setDelivery(pending->handle, pending->msg_id, CHAT_DELIVERY_FAILED);
			memset(pending, 0, sizeof(*pending));
			continue;
		}
		if (pending->attempts < CHAT_RETRY_LIMIT
			&& now - pending->last_send_ms >= (500u << (pending->attempts - 1)))
			sendPending(pending, now);
	}
	for (s32 i = 0; i < CHAT_REASSEMBLY_MAX; i++) {
		if (s_Reassembly[i].in_use && ((now - s_Reassembly[i].last_recv_ms) > 10000u
			|| now - s_Reassembly[i].first_recv_ms >= CHAT_DELIVERY_TIMEOUT_MS)) {
			memset(&s_Reassembly[i], 0, sizeof(s_Reassembly[i]));
		}
	}

	/* Flush dirty histories at most once per second to amortise disk. */
	static u32 s_LastFlushMs;
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
	s_LastSendError = "";
	if (friend_handle == 0 || !text || !*text) { s_LastSendError = "Enter a message and choose a friend."; return -1; }
	if (socialBlockIsHandle(friend_handle)) { s_LastSendError = "Unblock this friend before sending."; return -1; }
	if (!socialFriendByHandle(friend_handle)) { s_LastSendError = "This person is no longer on your Friends list."; return -1; }
	if (!s_Initialized || !presenceIsAgentLoaded()) { s_LastSendError = "Load an agent and connect to Social before sending."; return -1; }
	refreshChatIdentity();

	const u32 text_len = (u32)strnlen(text, CHAT_TEXT_MAX);
	if (text_len == 0 || text_len >= CHAT_TEXT_MAX) { s_LastSendError = "The message is too long. Shorten it and try again."; return -1; }

	chat_pending_t *pending = NULL;
	u32 peer_count = 0;
	for (s32 i = 0; i < CHAT_PENDING_MAX; ++i) {
		if (!s_Pending[i].in_use) { if (!pending) pending = &s_Pending[i]; }
		else if (s_Pending[i].handle == friend_handle) ++peer_count;
	}
	if (!pending || peer_count >= CHAT_PENDING_PER_FRIEND) { s_LastSendError = "Too many messages are pending. Wait for delivery or failure, then try again."; return -1; }
	chat_history_t *h = touchHistory(friend_handle);
	if (!h) { s_LastSendError = "Chat history capacity has been reached for this session."; return -1; }
	const u64 msg_id = ((u64)(u32)time(NULL) << 32) | (s_NextMsgIdCounter++ & 0xffffffffu);
	memset(pending, 0, sizeof(*pending));
	pending->in_use = 1;
	pending->handle = friend_handle;
	pending->msg_id = msg_id;
	pending->queued_ms = SDL_GetTicks();
	memcpy(pending->text, text, text_len + 1);
	chat_message_t m; memset(&m, 0, sizeof(m));
	m.msg_id = msg_id;
	m.peer_handle = friend_handle;
	m.direction = CHAT_DIR_OUT;
	m.delivery = CHAT_DELIVERY_PENDING;
	m.timestamp_unix = (u32)time(NULL);
	memcpy(m.text, text, text_len + 1);
	appendMessage(h, &m);
	saveHistory(h);
	sendPending(pending, pending->queued_ms);
	sysLogPrintf(LOG_NOTE, "CHAT: queued -> 0x%08x (%u bytes)",
	             (unsigned)friend_handle, (unsigned)text_len);
	return 0;
}

const char *chatLastSendError(void) { return s_LastSendError; }

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
