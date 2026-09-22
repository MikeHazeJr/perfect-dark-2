#ifndef _IN_MODASSET_JSON_H
#define _IN_MODASSET_JSON_H

#include <PR/ultratypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical JSON scalar-token parsers used by authored mod-asset ingress.
 * They consume exactly one token, reject non-JSON integer spellings and
 * overflow, and leave container-boundary validation explicit for the caller. */
s32 modAssetJsonParseS32Token(const char *start, const char *end,
		s32 *out, const char **token_end);
s32 modAssetJsonParseU32Token(const char *start, const char *end,
		u32 *out, const char **token_end);
s32 modAssetJsonParseBoolToken(const char *start, const char *end,
		s32 *out, const char **token_end);
s32 modAssetJsonTokenHasContainerBoundary(const char *token_end,
		const char *container_end, char closing_delimiter);

/* Compare one complete quoted JSON string using the shared strict decoder.
 * Returns 1 for an exact decoded match, 0 for a valid string mismatch, and
 * -1 for invalid input. The end pointer is exclusive; no truncation occurs. */
s32 modAssetJsonStringEquals(const char *quoted_start, const char *quoted_end,
		const char *expected);

/* Strict complete JSON object validation without rewriting numeric tokens. */
s32 modAssetJsonValidateObject(const char *json, size_t json_size);
/* Decode a complete quoted string. Returns 1 on success, 0 for malformed,
 * non-string, or oversized input; rejection leaves the output empty. */
s32 modAssetJsonDecodeString(const char *start, const char *end,
		char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
