#ifndef _IN_MODASSET_JSON_H
#define _IN_MODASSET_JSON_H

#include <PR/ultratypes.h>

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

#ifdef __cplusplus
}
#endif

#endif
