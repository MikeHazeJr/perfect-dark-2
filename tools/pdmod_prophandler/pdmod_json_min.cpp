/**
 * pdmod_json_min.cpp -- see header.
 *
 * Walks the JSON looking for a balanced "key": "value" pair where `key`
 * matches the requested name. Skips nested objects/arrays so a deeper-
 * nested key with the same name does not accidentally match.
 *
 * The implementation is intentionally tolerant of well-formed JSON:
 * - Whitespace anywhere.
 * - String escapes are passed through unmodified (caller decides whether
 *   to unescape; for the property handler's needs, raw UTF-8 bytes work
 *   fine because OS shell consumers re-validate themselves).
 * - Comments are NOT supported (mod.json is strict JSON).
 */

#include "pdmod_json_min.h"
#include <string.h>
#include <stdint.h>

static const char *skipWS(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
	return p;
}

/* Skip a JSON string literal starting at *p (which must point AT the opening
 * quote). Returns the position immediately after the closing quote. */
static const char *skipString(const char *p, const char *end)
{
	if (p >= end || *p != '"') return p;
	p++;
	while (p < end && *p != '"') {
		if (*p == '\\' && p + 1 < end) p += 2;
		else                            p++;
	}
	if (p < end) p++;  /* skip closing quote */
	return p;
}

/* Skip a JSON value of any type starting at p. Returns the position after. */
static const char *skipValue(const char *p, const char *end);

/* Skip a JSON object starting at the opening brace. */
static const char *skipObject(const char *p, const char *end)
{
	if (p >= end || *p != '{') return p;
	p++;
	while (p < end) {
		p = skipWS(p, end);
		if (p < end && *p == '}') return p + 1;
		if (p < end && *p == '"') {
			p = skipString(p, end);
			p = skipWS(p, end);
			if (p < end && *p == ':') p++;
			p = skipWS(p, end);
			p = skipValue(p, end);
			p = skipWS(p, end);
			if (p < end && *p == ',') p++;
		} else {
			break;
		}
	}
	return p;
}

/* Skip a JSON array starting at the opening bracket. */
static const char *skipArray(const char *p, const char *end)
{
	if (p >= end || *p != '[') return p;
	p++;
	while (p < end) {
		p = skipWS(p, end);
		if (p < end && *p == ']') return p + 1;
		p = skipValue(p, end);
		p = skipWS(p, end);
		if (p < end && *p == ',') p++;
	}
	return p;
}

static const char *skipValue(const char *p, const char *end)
{
	p = skipWS(p, end);
	if (p >= end) return p;
	if (*p == '"') return skipString(p, end);
	if (*p == '{') return skipObject(p, end);
	if (*p == '[') return skipArray(p, end);
	/* Number, true, false, null -- skip until the next structural char. */
	while (p < end && *p != ',' && *p != '}' && *p != ']' &&
	       *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
		p++;
	}
	return p;
}

const char *pdmodJsonFindString(const char *json, size_t json_len,
                                  const char *key, size_t *outLen)
{
	if (!json || !key) return NULL;
	if (outLen) *outLen = 0;
	const char *end = json + json_len;
	const char *p = skipWS(json, end);
	if (p >= end || *p != '{') return NULL;
	p++;

	size_t klen = strlen(key);

	while (p < end) {
		p = skipWS(p, end);
		if (p < end && *p == '}') return NULL;
		if (p >= end || *p != '"') return NULL;

		const char *kStart = p + 1;
		const char *kEnd = skipString(p, end);
		if (kEnd <= kStart + 1) return NULL;
		const char *kClose = kEnd - 1;  /* points at the closing quote */
		size_t cKeyLen = (size_t)(kClose - kStart);

		p = kEnd;
		p = skipWS(p, end);
		if (p < end && *p == ':') p++;
		p = skipWS(p, end);

		int matchedKey = (cKeyLen == klen && memcmp(kStart, key, klen) == 0);

		if (matchedKey) {
			if (p < end && *p == '"') {
				const char *vStart = p + 1;
				const char *vEnd = skipString(p, end);
				if (vEnd <= vStart + 1) return NULL;
				const char *vClose = vEnd - 1;
				if (outLen) *outLen = (size_t)(vClose - vStart);
				return vStart;
			}
			/* Key matched but value is not a string. */
			return NULL;
		}

		p = skipValue(p, end);
		p = skipWS(p, end);
		if (p < end && *p == ',') p++;
	}
	return NULL;
}
