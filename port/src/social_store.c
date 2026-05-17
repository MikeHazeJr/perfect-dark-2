/**
 * social_store.c -- Friend list, block list, visibility, notification
 * settings persisted under <home>/social/.
 *
 * Phase 1 of the connectivity rollout (context/designs/connectivity-and-modern-main-menu.md).
 *
 * JSON layout:
 *
 *   friends.json:
 *     {
 *       "version": 1,
 *       "friends": [
 *         { "code": "fat vampire running to the park",
 *           "agent": "smarch", "nick": "Chris",
 *           "handle": 305419896, "muted": false,
 *           "lastSeen": 1714000000 },
 *         ...
 *       ]
 *     }
 *
 *   blocks.json:
 *     {
 *       "version": 1,
 *       "blocks": [
 *         { "code": "...", "agent": "...", "handle": 0 },
 *         ...
 *       ]
 *     }
 *
 *   presence.json:
 *     {
 *       "version": 1,
 *       "visibility": "friends_only",
 *       "notify": { "social": true, "invites": true }
 *     }
 *
 * The parser is hand-rolled (modmgr.c precedent) so we avoid pulling in a
 * heavyweight JSON dep. It accepts the formats we write; it does not aim
 * to be a general-purpose JSON parser.
 */

#include "social.h"
#include "identity.h"
#include "connectcode.h"
#include "sha256.h"
#include "system.h"
#include "fs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static social_friend_t s_Friends[SOCIAL_FRIENDS_MAX];
static s32             s_NumFriends;

static social_block_t  s_Blocks[SOCIAL_BLOCKS_MAX];
static s32             s_NumBlocks;

static social_visibility_t s_Visibility = SOCIAL_VIS_FRIENDS_ONLY;
static u32             s_NotifMask  = SOCIAL_NOTIF_DEFAULT;

static u32             s_MyHandle;
static char            s_MyConnectCode[SOCIAL_CONNECTCODE_MAX];
static s32             s_Ready;

#define SOCIAL_DOMAIN "pd-social-connect-v1\n"

/* -------------------------------------------------------------------------
 * Path resolution
 * ------------------------------------------------------------------------- */

static const char *socialDir(void)
{
	static char s_Dir[512];
	if (s_Dir[0]) return s_Dir;

	char home[400];
	sysGetHomePath(home, sizeof(home));
	snprintf(s_Dir, sizeof(s_Dir), "%s/social", home);
	fsCreateDir(s_Dir);
	return s_Dir;
}

static void socialPath(const char *file, char *out, u32 outlen)
{
	snprintf(out, outlen, "%s/%s", socialDir(), file);
}

/* -------------------------------------------------------------------------
 * Connect-code derivation
 *
 * Handle = first 4 bytes of sha256(device_uuid || domain).
 * The domain string keeps the handle separate from any future hash that
 * might be derived from the same UUID. Endian-stable so two builds of
 * this exe produce the same handle for the same UUID.
 * ------------------------------------------------------------------------- */

/**
 * Per Mike's 2026-04-25 clarification: the connect-code handle is bound to
 * the local Ed25519 *public key*, not the device UUID. This makes the
 * handle stable across reinstalls (the keypair is persisted in
 * pd-identity.dat) and gives every signed presence ping a verifiable
 * identity check that does not depend on any IP-based identifier.
 *
 * The legacy UUID path remains as a fallback when a keypair is not yet
 * available (very early init or OpenSSL keygen failure) so the pill / UI
 * is never broken; once ensureKeypair() runs the handle re-derives off
 * the pubkey and the social store is re-saved.
 */
static u32 deriveHandleFromBytes(const u8 *bytes, u32 len)
{
	sha256_ctx ctx;
	u8         digest[SHA256_DIGEST_SIZE];
	sha256Init(&ctx);
	sha256Update(&ctx, bytes, len);
	sha256Update(&ctx, SOCIAL_DOMAIN, sizeof(SOCIAL_DOMAIN) - 1);
	sha256Final(&ctx, digest);
	return ((u32)digest[0])
	     | ((u32)digest[1] << 8)
	     | ((u32)digest[2] << 16)
	     | ((u32)digest[3] << 24);
}

s32 socialHandleBindsPubkey(u32 handle, const u8 pubkey[SOCIAL_PUBKEY_LEN])
{
	if (!pubkey) return 0;
	return deriveHandleFromBytes(pubkey, SOCIAL_PUBKEY_LEN) == handle ? 1 : 0;
}

s32 socialEncodeHandle(u32 handle, char *out, u32 outsize)
{
	if (!out || outsize < 32) return -1;
	/* Reuse the connect-code dictionary by treating the handle's 4 bytes as
	 * the four slot indices. connectCodeEncode takes a u32 in the same byte
	 * order our handle is built in (LSB first), so this is a direct call. */
	return connectCodeEncode(handle, out, (s32)outsize) >= 0 ? 0 : -1;
}

s32 socialDecodeHandle(const char *code, u32 *out_handle)
{
	if (!code || !out_handle) return -1;
	u32 ip = 0;
	if (connectCodeDecode(code, &ip) != 0) return -1;
	*out_handle = ip;
	return 0;
}

/* -------------------------------------------------------------------------
 * Tiny tokeniser-driven JSON reader (matches modmgr.c style).
 *
 * Only what we need to round-trip the schemas above: object / array /
 * key-value / string / number / boolean. Whitespace tolerant. No escapes
 * besides \" inside strings (we only emit ASCII-safe values). On error,
 * the parser stops at the first malformed token and the caller proceeds
 * with whatever was loaded so far.
 * ------------------------------------------------------------------------- */

typedef struct {
	const char *src;
	const char *p;
} jread_t;

static void jSkipWs(jread_t *j)
{
	while (*j->p && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static s32 jExpect(jread_t *j, char c)
{
	jSkipWs(j);
	if (*j->p != c) return 0;
	j->p++;
	return 1;
}

static s32 jReadString(jread_t *j, char *dst, u32 dstsize)
{
	jSkipWs(j);
	if (*j->p != '"') return 0;
	j->p++;
	u32 i = 0;
	while (*j->p && *j->p != '"') {
		if (*j->p == '\\' && j->p[1]) {
			j->p++;
			if (i + 1 < dstsize) dst[i++] = *j->p;
			j->p++;
			continue;
		}
		if (i + 1 < dstsize) dst[i++] = *j->p;
		j->p++;
	}
	if (*j->p == '"') j->p++;
	if (dstsize > 0) dst[i < dstsize ? i : dstsize - 1] = '\0';
	return 1;
}

static s32 jReadInt64(jread_t *j, s64 *out)
{
	jSkipWs(j);
	const char *start = j->p;
	if (*j->p == '-') j->p++;
	while (*j->p >= '0' && *j->p <= '9') j->p++;
	if (j->p == start) return 0;
	*out = strtoll(start, NULL, 10);
	return 1;
}

static s32 jReadBool(jread_t *j, s32 *out)
{
	jSkipWs(j);
	if (strncmp(j->p, "true", 4) == 0)  { j->p += 4; *out = 1; return 1; }
	if (strncmp(j->p, "false", 5) == 0) { j->p += 5; *out = 0; return 1; }
	return 0;
}

/* Skip whatever value comes next (string / number / object / array / bool / null) */
static void jSkipValue(jread_t *j)
{
	jSkipWs(j);
	if (*j->p == '"') {
		char tmp[8];
		jReadString(j, tmp, sizeof(tmp));
		return;
	}
	if (*j->p == '{' || *j->p == '[') {
		char open = *j->p++;
		char close = open == '{' ? '}' : ']';
		s32 depth = 1;
		while (*j->p && depth > 0) {
			if (*j->p == '"') {
				char tmp[8];
				jReadString(j, tmp, sizeof(tmp));
				continue;
			}
			if (*j->p == open) depth++;
			else if (*j->p == close) depth--;
			j->p++;
		}
		return;
	}
	while (*j->p && *j->p != ',' && *j->p != '}' && *j->p != ']') j->p++;
}

/* -------------------------------------------------------------------------
 * File I/O helpers
 * ------------------------------------------------------------------------- */

static char *slurpFile(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0 || sz > 4 * 1024 * 1024) { fclose(f); return NULL; }
	char *buf = (char *)malloc((size_t)sz + 1);
	if (!buf) { fclose(f); return NULL; }
	size_t got = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	buf[got] = '\0';
	return buf;
}

static s32 writeAtomic(const char *path, const char *bytes, size_t len)
{
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return -1;
	if (fwrite(bytes, 1, len, f) != len) { fclose(f); remove(tmp); return -1; }
	fflush(f);
	fclose(f);
#ifdef _WIN32
	/* Windows rename does not overwrite -- delete first, then rename. */
	remove(path);
#endif
	if (rename(tmp, path) != 0) { remove(tmp); return -1; }
	return 0;
}

/* Append a JSON-escaped string segment. Handles backslash, quote, control. */
static void appendJsonString(char *dst, size_t dstsize, size_t *plen, const char *src)
{
	if (*plen + 2 > dstsize) return;
	dst[(*plen)++] = '"';
	while (*src && *plen + 2 < dstsize) {
		char c = *src++;
		if (c == '"' || c == '\\') {
			if (*plen + 2 >= dstsize) break;
			dst[(*plen)++] = '\\';
			dst[(*plen)++] = c;
		} else if ((unsigned char)c < 0x20) {
			/* skip control characters silently */
			continue;
		} else {
			dst[(*plen)++] = c;
		}
	}
	if (*plen + 1 < dstsize) dst[(*plen)++] = '"';
}

static void appendStr(char *dst, size_t dstsize, size_t *plen, const char *src)
{
	while (*src && *plen + 1 < dstsize) dst[(*plen)++] = *src++;
}

static void appendFmt(char *dst, size_t dstsize, size_t *plen, const char *fmt, ...)
{
	if (*plen >= dstsize) return;
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(dst + *plen, dstsize - *plen, fmt, ap);
	va_end(ap);
	if (n < 0) return;
	*plen += (size_t)n < (dstsize - *plen) ? (size_t)n : (dstsize - *plen - 1);
}

/* Encode binary bytes as a lowercase hex string. dst must hold 2*len + 1. */
static void hexEncode(const u8 *src, u32 len, char *dst)
{
	static const char *hex = "0123456789abcdef";
	for (u32 i = 0; i < len; i++) {
		dst[i * 2 + 0] = hex[(src[i] >> 4) & 0xF];
		dst[i * 2 + 1] = hex[src[i] & 0xF];
	}
	dst[len * 2] = '\0';
}

static s32 hexNibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Decode a hex string into dst. Returns 1 on success, 0 on parse failure
 * or if the hex is shorter than 2*expected_len. */
static s32 hexDecode(const char *src, u8 *dst, u32 expected_len)
{
	if (!src || !dst) return 0;
	for (u32 i = 0; i < expected_len; i++) {
		s32 hi = hexNibble(src[i * 2 + 0]);
		s32 lo = hexNibble(src[i * 2 + 1]);
		if (hi < 0 || lo < 0) return 0;
		dst[i] = (u8)((hi << 4) | lo);
	}
	return 1;
}

/* -------------------------------------------------------------------------
 * Save
 * ------------------------------------------------------------------------- */

static void saveFriends(void)
{
	static char buf[64 * 1024];
	size_t len = 0;
	appendStr(buf, sizeof(buf), &len, "{\n  \"version\": 1,\n  \"friends\": [");

	for (s32 i = 0; i < s_NumFriends; i++) {
		const social_friend_t *f = &s_Friends[i];
		appendStr(buf, sizeof(buf), &len, i == 0 ? "\n    " : ",\n    ");
		appendStr(buf, sizeof(buf), &len, "{ \"code\": ");
		appendJsonString(buf, sizeof(buf), &len, f->connect_code);
		appendStr(buf, sizeof(buf), &len, ", \"agent\": ");
		appendJsonString(buf, sizeof(buf), &len, f->agent_name);
		appendStr(buf, sizeof(buf), &len, ", \"nick\": ");
		appendJsonString(buf, sizeof(buf), &len, f->nickname);
		appendFmt(buf, sizeof(buf), &len,
		          ", \"handle\": %u, \"muted\": %s, \"lastSeen\": %llu",
		          (unsigned)f->handle,
		          f->muted ? "true" : "false",
		          (unsigned long long)f->last_seen_unix);
		if (f->has_pubkey) {
			char hex[SOCIAL_PUBKEY_LEN * 2 + 1];
			hexEncode(f->pubkey, SOCIAL_PUBKEY_LEN, hex);
			appendStr(buf, sizeof(buf), &len, ", \"pubkey\": ");
			appendJsonString(buf, sizeof(buf), &len, hex);
		}
		if (f->endpoint_ipv4 && f->endpoint_port) {
			appendFmt(buf, sizeof(buf), &len,
			          ", \"endpoint\": { \"ipv4\": %u, \"port\": %u, \"ttl\": %llu }",
			          (unsigned)f->endpoint_ipv4,
			          (unsigned)f->endpoint_port,
			          (unsigned long long)f->endpoint_ttl_unix);
		}
		appendStr(buf, sizeof(buf), &len, " }");
	}

	appendStr(buf, sizeof(buf), &len, "\n  ]\n}\n");

	char path[600];
	socialPath("friends.json", path, sizeof(path));
	if (writeAtomic(path, buf, len) != 0) {
		sysLogPrintf(LOG_WARNING, "SOCIAL: failed to write %s", path);
	}
}

static void saveBlocks(void)
{
	static char buf[16 * 1024];
	size_t len = 0;
	appendStr(buf, sizeof(buf), &len, "{\n  \"version\": 1,\n  \"blocks\": [");

	for (s32 i = 0; i < s_NumBlocks; i++) {
		const social_block_t *b = &s_Blocks[i];
		appendStr(buf, sizeof(buf), &len, i == 0 ? "\n    " : ",\n    ");
		appendStr(buf, sizeof(buf), &len, "{ \"code\": ");
		appendJsonString(buf, sizeof(buf), &len, b->connect_code);
		appendStr(buf, sizeof(buf), &len, ", \"agent\": ");
		appendJsonString(buf, sizeof(buf), &len, b->agent_name);
		appendFmt(buf, sizeof(buf), &len, ", \"handle\": %u }", (unsigned)b->handle);
	}

	appendStr(buf, sizeof(buf), &len, "\n  ]\n}\n");

	char path[600];
	socialPath("blocks.json", path, sizeof(path));
	if (writeAtomic(path, buf, len) != 0) {
		sysLogPrintf(LOG_WARNING, "SOCIAL: failed to write %s", path);
	}
}

static const char *visibilityToString(social_visibility_t v)
{
	switch (v) {
		case SOCIAL_VIS_PUBLIC:         return "public";
		case SOCIAL_VIS_FRIENDS_ONLY:   return "friends_only";
		case SOCIAL_VIS_APPEAR_OFFLINE: return "appear_offline";
		default:                        return "friends_only";
	}
}

static social_visibility_t visibilityFromString(const char *s)
{
	if (!s) return SOCIAL_VIS_FRIENDS_ONLY;
	if (!strcmp(s, "public")) return SOCIAL_VIS_PUBLIC;
	if (!strcmp(s, "appear_offline")) return SOCIAL_VIS_APPEAR_OFFLINE;
	return SOCIAL_VIS_FRIENDS_ONLY;
}

static void savePresence(void)
{
	char buf[1024];
	int n = snprintf(buf, sizeof(buf),
	                 "{\n"
	                 "  \"version\": 1,\n"
	                 "  \"visibility\": \"%s\",\n"
	                 "  \"notify\": { \"social\": %s, \"invites\": %s }\n"
	                 "}\n",
	                 visibilityToString(s_Visibility),
	                 (s_NotifMask & SOCIAL_NOTIF_SOCIAL)  ? "true" : "false",
	                 (s_NotifMask & SOCIAL_NOTIF_INVITES) ? "true" : "false");
	if (n < 0) return;
	char path[600];
	socialPath("presence.json", path, sizeof(path));
	if (writeAtomic(path, buf, (size_t)n) != 0) {
		sysLogPrintf(LOG_WARNING, "SOCIAL: failed to write %s", path);
	}
}

void socialSave(void)
{
	if (!s_Ready) return;
	saveFriends();
	saveBlocks();
	savePresence();
}

/* -------------------------------------------------------------------------
 * Load
 * ------------------------------------------------------------------------- */

static void loadFriends(void)
{
	char path[600];
	socialPath("friends.json", path, sizeof(path));
	char *data = slurpFile(path);
	if (!data) return;

	jread_t j = { data, data };
	if (!jExpect(&j, '{')) { free(data); return; }

	s_NumFriends = 0;
	while (*j.p) {
		jSkipWs(&j);
		if (*j.p == '}') { j.p++; break; }
		if (*j.p == ',') { j.p++; continue; }

		char key[32];
		if (!jReadString(&j, key, sizeof(key))) break;
		if (!jExpect(&j, ':')) break;

		if (!strcmp(key, "friends")) {
			if (!jExpect(&j, '[')) break;
			while (*j.p) {
				jSkipWs(&j);
				if (*j.p == ']') { j.p++; break; }
				if (*j.p == ',') { j.p++; continue; }
				if (*j.p != '{') break;
				j.p++;

				social_friend_t f;
				memset(&f, 0, sizeof(f));

				while (*j.p) {
					jSkipWs(&j);
					if (*j.p == '}') { j.p++; break; }
					if (*j.p == ',') { j.p++; continue; }
					char k2[16];
					if (!jReadString(&j, k2, sizeof(k2))) break;
					if (!jExpect(&j, ':')) break;
					if (!strcmp(k2, "code")) {
						jReadString(&j, f.connect_code, sizeof(f.connect_code));
					} else if (!strcmp(k2, "agent")) {
						jReadString(&j, f.agent_name, sizeof(f.agent_name));
					} else if (!strcmp(k2, "nick")) {
						jReadString(&j, f.nickname, sizeof(f.nickname));
					} else if (!strcmp(k2, "handle")) {
						s64 v = 0; jReadInt64(&j, &v); f.handle = (u32)v;
					} else if (!strcmp(k2, "muted")) {
						s32 v = 0; jReadBool(&j, &v); f.muted = (u8)v;
					} else if (!strcmp(k2, "lastSeen")) {
						s64 v = 0; jReadInt64(&j, &v); f.last_seen_unix = (u64)v;
					} else if (!strcmp(k2, "pubkey")) {
						char hex[SOCIAL_PUBKEY_LEN * 2 + 4];
						jReadString(&j, hex, sizeof(hex));
						if (hexDecode(hex, f.pubkey, SOCIAL_PUBKEY_LEN)) {
							f.has_pubkey = 1;
						}
					} else if (!strcmp(k2, "endpoint")) {
						/* { "ipv4": N, "port": N, "ttl": N } */
						if (jExpect(&j, '{')) {
							while (*j.p) {
								jSkipWs(&j);
								if (*j.p == '}') { j.p++; break; }
								if (*j.p == ',') { j.p++; continue; }
								char k3[12];
								if (!jReadString(&j, k3, sizeof(k3))) break;
								if (!jExpect(&j, ':')) break;
								s64 v = 0;
								if (!strcmp(k3, "ipv4")) {
									jReadInt64(&j, &v); f.endpoint_ipv4 = (u32)v;
								} else if (!strcmp(k3, "port")) {
									jReadInt64(&j, &v); f.endpoint_port = (u16)v;
								} else if (!strcmp(k3, "ttl")) {
									jReadInt64(&j, &v); f.endpoint_ttl_unix = (u64)v;
								} else {
									jSkipValue(&j);
								}
							}
						}
					} else {
						jSkipValue(&j);
					}
				}

				if (s_NumFriends < SOCIAL_FRIENDS_MAX && f.connect_code[0]) {
					/* Backfill handle when an old file omitted it. */
					if (f.handle == 0) {
						u32 h = 0;
						if (socialDecodeHandle(f.connect_code, &h) == 0) {
							f.handle = h;
						}
					}
					s_Friends[s_NumFriends++] = f;
				}
			}
		} else {
			jSkipValue(&j);
		}
	}

	free(data);
}

static void loadBlocks(void)
{
	char path[600];
	socialPath("blocks.json", path, sizeof(path));
	char *data = slurpFile(path);
	if (!data) return;

	jread_t j = { data, data };
	if (!jExpect(&j, '{')) { free(data); return; }

	s_NumBlocks = 0;
	while (*j.p) {
		jSkipWs(&j);
		if (*j.p == '}') { j.p++; break; }
		if (*j.p == ',') { j.p++; continue; }

		char key[32];
		if (!jReadString(&j, key, sizeof(key))) break;
		if (!jExpect(&j, ':')) break;

		if (!strcmp(key, "blocks")) {
			if (!jExpect(&j, '[')) break;
			while (*j.p) {
				jSkipWs(&j);
				if (*j.p == ']') { j.p++; break; }
				if (*j.p == ',') { j.p++; continue; }
				if (*j.p != '{') break;
				j.p++;

				social_block_t b;
				memset(&b, 0, sizeof(b));

				while (*j.p) {
					jSkipWs(&j);
					if (*j.p == '}') { j.p++; break; }
					if (*j.p == ',') { j.p++; continue; }
					char k2[16];
					if (!jReadString(&j, k2, sizeof(k2))) break;
					if (!jExpect(&j, ':')) break;
					if (!strcmp(k2, "code")) {
						jReadString(&j, b.connect_code, sizeof(b.connect_code));
					} else if (!strcmp(k2, "agent")) {
						jReadString(&j, b.agent_name, sizeof(b.agent_name));
					} else if (!strcmp(k2, "handle")) {
						s64 v = 0; jReadInt64(&j, &v); b.handle = (u32)v;
					} else {
						jSkipValue(&j);
					}
				}

				if (s_NumBlocks < SOCIAL_BLOCKS_MAX && b.connect_code[0]) {
					if (b.handle == 0) {
						u32 h = 0;
						if (socialDecodeHandle(b.connect_code, &h) == 0) {
							b.handle = h;
						}
					}
					s_Blocks[s_NumBlocks++] = b;
				}
			}
		} else {
			jSkipValue(&j);
		}
	}

	free(data);
}

static void loadPresence(void)
{
	char path[600];
	socialPath("presence.json", path, sizeof(path));
	char *data = slurpFile(path);
	if (!data) return;

	jread_t j = { data, data };
	if (!jExpect(&j, '{')) { free(data); return; }

	while (*j.p) {
		jSkipWs(&j);
		if (*j.p == '}') { j.p++; break; }
		if (*j.p == ',') { j.p++; continue; }
		char key[24];
		if (!jReadString(&j, key, sizeof(key))) break;
		if (!jExpect(&j, ':')) break;
		if (!strcmp(key, "visibility")) {
			char val[24];
			jReadString(&j, val, sizeof(val));
			s_Visibility = visibilityFromString(val);
		} else if (!strcmp(key, "notify")) {
			if (!jExpect(&j, '{')) { jSkipValue(&j); continue; }
			s_NotifMask = 0;
			while (*j.p) {
				jSkipWs(&j);
				if (*j.p == '}') { j.p++; break; }
				if (*j.p == ',') { j.p++; continue; }
				char k2[16];
				if (!jReadString(&j, k2, sizeof(k2))) break;
				if (!jExpect(&j, ':')) break;
				s32 v = 0;
				jReadBool(&j, &v);
				if (!strcmp(k2, "social")  && v) s_NotifMask |= SOCIAL_NOTIF_SOCIAL;
				if (!strcmp(k2, "invites") && v) s_NotifMask |= SOCIAL_NOTIF_INVITES;
			}
		} else {
			jSkipValue(&j);
		}
	}

	free(data);
}

/* -------------------------------------------------------------------------
 * Init
 * ------------------------------------------------------------------------- */

void socialInit(void)
{
	if (s_Ready) return;

	memset(s_Friends, 0, sizeof(s_Friends));
	memset(s_Blocks, 0, sizeof(s_Blocks));
	s_NumFriends = 0;
	s_NumBlocks = 0;
	s_Visibility = SOCIAL_VIS_FRIENDS_ONLY;
	s_NotifMask = SOCIAL_NOTIF_DEFAULT;

	/* Prefer the Ed25519 pubkey (Mike clarification 2026-04-25): the
	 * handle is now bound to the persistent identity key, not the device
	 * UUID. Falls back to UUID-derived if the keypair has not been
	 * generated yet (very early init / OpenSSL keygen failure). */
	pd_identity_t *ident = identityGet();
	const u8 *pub = identityGetPubkey();
	if (pub) {
		s_MyHandle = deriveHandleFromBytes(pub, SOCIAL_PUBKEY_LEN);
	} else if (ident) {
		s_MyHandle = deriveHandleFromBytes(ident->device_uuid, IDENTITY_UUID_LEN);
	} else {
		s_MyHandle = 0;
	}
	if (socialEncodeHandle(s_MyHandle, s_MyConnectCode, sizeof(s_MyConnectCode)) != 0) {
		strncpy(s_MyConnectCode, "unknown",  sizeof(s_MyConnectCode) - 1);
	}

	(void)socialDir(); /* ensure directory exists */
	loadFriends();
	loadBlocks();
	loadPresence();

	s_Ready = 1;

	sysLogPrintf(LOG_NOTE,
	             "SOCIAL: ready handle=0x%08x code=\"%s\" friends=%d blocks=%d vis=%s notify=0x%x",
	             (unsigned)s_MyHandle, s_MyConnectCode,
	             (int)s_NumFriends, (int)s_NumBlocks,
	             visibilityToString(s_Visibility), (unsigned)s_NotifMask);
}

s32 socialIsReady(void) { return s_Ready; }

/* -------------------------------------------------------------------------
 * Local identity accessors
 * ------------------------------------------------------------------------- */

u32 socialMyHandle(void) { return s_MyHandle; }
const char *socialMyConnectCode(void) { return s_MyConnectCode; }

const char *socialMyAgentName(void)
{
	identity_profile_t *p = identityGetActiveProfile();
	if (p && p->name[0]) return p->name;
	return "Agent";
}

/* Mike directive 2026-05-17: per-agent connect code. Hash (pubkey ||
 * agent_name) so each agent on the same device produces a distinct
 * handle + connect code. Two players sharing a build can load their own
 * agent profiles and broadcast independent join codes. */
void socialRebindToActiveAgent(void)
{
	const u8 *pub = identityGetPubkey();
	const char *agent = socialMyAgentName();
	const u32 prev_handle = s_MyHandle;
	char prev_code[32];
	strncpy(prev_code, s_MyConnectCode, sizeof(prev_code) - 1);
	prev_code[sizeof(prev_code) - 1] = '\0';

	if (pub) {
		/* Compose (pubkey || agent_name) into a single derivation buffer.
		 * SOCIAL_PUBKEY_LEN is 32; agent names are short, so a fixed
		 * 128-byte combined buffer covers any reasonable length. */
		u8 combined[SOCIAL_PUBKEY_LEN + 96];
		memcpy(combined, pub, SOCIAL_PUBKEY_LEN);
		const u32 alen = agent ? (u32)strnlen(agent, sizeof(combined) - SOCIAL_PUBKEY_LEN) : 0;
		if (alen > 0 && agent) {
			memcpy(combined + SOCIAL_PUBKEY_LEN, agent, alen);
		}
		s_MyHandle = deriveHandleFromBytes(combined, SOCIAL_PUBKEY_LEN + alen);
	} else {
		/* Pre-keypair fallback: device-uuid hash unchanged. */
		pd_identity_t *ident = identityGet();
		if (ident) {
			s_MyHandle = deriveHandleFromBytes(ident->device_uuid, IDENTITY_UUID_LEN);
		}
	}

	if (socialEncodeHandle(s_MyHandle, s_MyConnectCode, sizeof(s_MyConnectCode)) != 0) {
		strncpy(s_MyConnectCode, "unknown", sizeof(s_MyConnectCode) - 1);
		s_MyConnectCode[sizeof(s_MyConnectCode) - 1] = '\0';
	}

	if (s_MyHandle != prev_handle) {
		sysLogPrintf(LOG_NOTE,
		             "SOCIAL: rebind agent='%s' handle=0x%08x->0x%08x code=\"%s\"->\"%s\"",
		             agent ? agent : "(null)",
		             (unsigned)prev_handle, (unsigned)s_MyHandle,
		             prev_code, s_MyConnectCode);
	}
}

/* -------------------------------------------------------------------------
 * Friend list
 * ------------------------------------------------------------------------- */

static s32 friendIndexByCode(const char *cc)
{
	if (!cc || !cc[0]) return -1;
	for (s32 i = 0; i < s_NumFriends; i++) {
		if (!strcmp(s_Friends[i].connect_code, cc)) return i;
	}
	return -1;
}

static s32 friendIndexByHandle(u32 h)
{
	for (s32 i = 0; i < s_NumFriends; i++) {
		if (s_Friends[i].handle == h) return i;
	}
	return -1;
}

s32 socialFriendCount(void) { return s_NumFriends; }

const social_friend_t *socialFriendAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumFriends) return NULL;
	return &s_Friends[idx];
}

const social_friend_t *socialFriendByCode(const char *cc)
{
	s32 i = friendIndexByCode(cc);
	return i >= 0 ? &s_Friends[i] : NULL;
}

const social_friend_t *socialFriendByHandle(u32 h)
{
	s32 i = friendIndexByHandle(h);
	return i >= 0 ? &s_Friends[i] : NULL;
}

s32 socialFriendAdd(const char *connect_code, const char *agent_name)
{
	if (!s_Ready || !connect_code || !connect_code[0]) return -1;
	if (s_NumFriends >= SOCIAL_FRIENDS_MAX) return -1;

	u32 handle = 0;
	if (socialDecodeHandle(connect_code, &handle) != 0) return -1;

	if (handle == s_MyHandle) {
		sysLogPrintf(LOG_NOTE, "SOCIAL: refused self-friend (handle 0x%08x)", (unsigned)handle);
		return -1;
	}

	s32 existing = friendIndexByHandle(handle);
	if (existing >= 0) {
		/* Already in list -- update agent name if newly known. */
		if (agent_name && agent_name[0]) {
			strncpy(s_Friends[existing].agent_name, agent_name,
			        SOCIAL_AGENTNAME_MAX - 1);
			s_Friends[existing].agent_name[SOCIAL_AGENTNAME_MAX - 1] = '\0';
		}
		socialSave();
		return 0;
	}

	social_friend_t *f = &s_Friends[s_NumFriends++];
	memset(f, 0, sizeof(*f));
	strncpy(f->connect_code, connect_code, SOCIAL_CONNECTCODE_MAX - 1);
	if (agent_name) {
		strncpy(f->agent_name, agent_name, SOCIAL_AGENTNAME_MAX - 1);
	}
	f->handle = handle;
	f->muted = 0;
	f->last_seen_unix = 0;

	socialSave();
	sysLogPrintf(LOG_NOTE, "SOCIAL: friend added handle=0x%08x agent=\"%s\"",
	             (unsigned)handle, f->agent_name);
	return 1;
}

s32 socialFriendRemove(const char *connect_code)
{
	if (!s_Ready) return 0;
	s32 i = friendIndexByCode(connect_code);
	if (i < 0) return 0;
	s_NumFriends--;
	if (i < s_NumFriends) {
		memmove(&s_Friends[i], &s_Friends[i + 1],
		        (size_t)(s_NumFriends - i) * sizeof(social_friend_t));
	}
	memset(&s_Friends[s_NumFriends], 0, sizeof(social_friend_t));
	socialSave();
	return 1;
}

s32 socialFriendSetNickname(const char *connect_code, const char *nickname)
{
	if (!s_Ready) return 0;
	s32 i = friendIndexByCode(connect_code);
	if (i < 0) return 0;
	if (nickname) {
		strncpy(s_Friends[i].nickname, nickname, SOCIAL_NICKNAME_MAX - 1);
		s_Friends[i].nickname[SOCIAL_NICKNAME_MAX - 1] = '\0';
	} else {
		s_Friends[i].nickname[0] = '\0';
	}
	socialSave();
	return 1;
}

s32 socialFriendSetMuted(const char *connect_code, s32 muted)
{
	if (!s_Ready) return 0;
	s32 i = friendIndexByCode(connect_code);
	if (i < 0) return 0;
	s_Friends[i].muted = muted ? 1 : 0;
	socialSave();
	return 1;
}

s32 socialFriendUpdateAgentName(const char *connect_code, const char *agent_name)
{
	if (!s_Ready || !agent_name) return 0;
	s32 i = friendIndexByCode(connect_code);
	if (i < 0) return 0;
	strncpy(s_Friends[i].agent_name, agent_name, SOCIAL_AGENTNAME_MAX - 1);
	s_Friends[i].agent_name[SOCIAL_AGENTNAME_MAX - 1] = '\0';
	socialSave();
	return 1;
}

s32 socialFriendTouchSeen(const char *connect_code)
{
	if (!s_Ready) return 0;
	s32 i = friendIndexByCode(connect_code);
	if (i < 0) return 0;
	s_Friends[i].last_seen_unix = (u64)time(NULL);
	/* Don't save on every pong -- caller decides cadence. */
	return 1;
}

s32 socialFriendBindPubkey(u32 handle, const u8 pubkey[SOCIAL_PUBKEY_LEN])
{
	if (!s_Ready || !pubkey) return 0;
	s32 i = friendIndexByHandle(handle);
	if (i < 0) return 0;

	if (s_Friends[i].has_pubkey) {
		if (memcmp(s_Friends[i].pubkey, pubkey, SOCIAL_PUBKEY_LEN) == 0) {
			return 1; /* identical, no-op */
		}
		/* Identity changed -- caller must surface to UI. The handle stays
		 * valid (the connect code is the user-visible address book entry),
		 * but the new pubkey must be re-confirmed before we accept pings. */
		return -1;
	}

	memcpy(s_Friends[i].pubkey, pubkey, SOCIAL_PUBKEY_LEN);
	s_Friends[i].has_pubkey = 1;
	socialSave();
	sysLogPrintf(LOG_NOTE,
	             "SOCIAL: TOFU bind pubkey for handle=0x%08x agent=\"%s\"",
	             (unsigned)handle, s_Friends[i].agent_name);
	return 1;
}

s32 socialFriendUpdateEndpoint(u32 handle, u32 ipv4, u16 port, u32 ttl_seconds)
{
	if (!s_Ready) return 0;
	s32 i = friendIndexByHandle(handle);
	if (i < 0) return 0;
	s_Friends[i].endpoint_ipv4 = ipv4;
	s_Friends[i].endpoint_port = port;
	if (ipv4 == 0 || port == 0 || ttl_seconds == 0) {
		s_Friends[i].endpoint_ttl_unix = 0;
	} else {
		s_Friends[i].endpoint_ttl_unix = (u64)time(NULL) + (u64)ttl_seconds;
	}
	/* Don't save on every endpoint update -- caller decides cadence. */
	return 1;
}

s32 socialFriendGetEndpoint(u32 handle, u32 *out_ipv4, u16 *out_port)
{
	if (!s_Ready) return 0;
	s32 i = friendIndexByHandle(handle);
	if (i < 0) return 0;
	if (s_Friends[i].endpoint_ipv4 == 0 || s_Friends[i].endpoint_port == 0) return 0;
	if ((u64)time(NULL) >= s_Friends[i].endpoint_ttl_unix) return 0;
	if (out_ipv4) *out_ipv4 = s_Friends[i].endpoint_ipv4;
	if (out_port) *out_port = s_Friends[i].endpoint_port;
	return 1;
}

/* -------------------------------------------------------------------------
 * Block list
 * ------------------------------------------------------------------------- */

static s32 blockIndexByCode(const char *cc)
{
	if (!cc || !cc[0]) return -1;
	for (s32 i = 0; i < s_NumBlocks; i++) {
		if (!strcmp(s_Blocks[i].connect_code, cc)) return i;
	}
	return -1;
}

static s32 blockIndexByHandle(u32 h)
{
	for (s32 i = 0; i < s_NumBlocks; i++) {
		if (s_Blocks[i].handle == h) return i;
	}
	return -1;
}

s32 socialBlockCount(void) { return s_NumBlocks; }

const social_block_t *socialBlockAt(s32 idx)
{
	if (idx < 0 || idx >= s_NumBlocks) return NULL;
	return &s_Blocks[idx];
}

s32 socialBlockIs(const char *connect_code)
{
	return blockIndexByCode(connect_code) >= 0;
}

s32 socialBlockIsHandle(u32 handle)
{
	return blockIndexByHandle(handle) >= 0;
}

s32 socialBlockAdd(const char *connect_code, const char *agent_name)
{
	if (!s_Ready || !connect_code || !connect_code[0]) return -1;
	if (s_NumBlocks >= SOCIAL_BLOCKS_MAX) return -1;

	u32 handle = 0;
	if (socialDecodeHandle(connect_code, &handle) != 0) return -1;

	if (blockIndexByHandle(handle) >= 0) return 0;

	/* Block also removes from friend list (symmetric break). */
	socialFriendRemove(connect_code);

	social_block_t *b = &s_Blocks[s_NumBlocks++];
	memset(b, 0, sizeof(*b));
	strncpy(b->connect_code, connect_code, SOCIAL_CONNECTCODE_MAX - 1);
	if (agent_name) {
		strncpy(b->agent_name, agent_name, SOCIAL_AGENTNAME_MAX - 1);
	}
	b->handle = handle;

	socialSave();
	sysLogPrintf(LOG_NOTE, "SOCIAL: blocked handle=0x%08x agent=\"%s\"",
	             (unsigned)handle, b->agent_name);
	return 1;
}

s32 socialBlockRemove(const char *connect_code)
{
	if (!s_Ready) return 0;
	s32 i = blockIndexByCode(connect_code);
	if (i < 0) return 0;
	s_NumBlocks--;
	if (i < s_NumBlocks) {
		memmove(&s_Blocks[i], &s_Blocks[i + 1],
		        (size_t)(s_NumBlocks - i) * sizeof(social_block_t));
	}
	memset(&s_Blocks[s_NumBlocks], 0, sizeof(social_block_t));
	socialSave();
	return 1;
}

/* -------------------------------------------------------------------------
 * Visibility + notifications
 * ------------------------------------------------------------------------- */

social_visibility_t socialVisibilityGet(void) { return s_Visibility; }

void socialVisibilitySet(social_visibility_t v)
{
	if (v < SOCIAL_VIS_PUBLIC || v > SOCIAL_VIS_APPEAR_OFFLINE) return;
	if (s_Visibility == v) return;
	s_Visibility = v;
	if (s_Ready) savePresence();
}

u32 socialNotifMaskGet(void) { return s_NotifMask; }

void socialNotifMaskSet(u32 mask)
{
	if (s_NotifMask == mask) return;
	s_NotifMask = mask;
	if (s_Ready) savePresence();
}

s32 socialNotifIsSet(u32 category) { return (s_NotifMask & category) != 0; }

/* -------------------------------------------------------------------------
 * Display formatting
 * ------------------------------------------------------------------------- */

void socialFormatDisplay(const social_friend_t *f, char *out, u32 outsize)
{
	if (!out || outsize == 0) return;
	if (!f) { out[0] = '\0'; return; }
	if (f->nickname[0]) {
		snprintf(out, outsize, "%s: %s", f->nickname, f->agent_name);
	} else if (f->agent_name[0]) {
		snprintf(out, outsize, "%s", f->agent_name);
	} else {
		snprintf(out, outsize, "%s", f->connect_code);
	}
}
