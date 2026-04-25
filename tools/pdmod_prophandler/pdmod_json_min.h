/**
 * pdmod_json_min.h -- Priority M / B-238 / M-2.1
 *
 * Find a top-level "key": "string-value" pair in a JSON document. Single
 * function -- the property handler only needs to extract headline string
 * fields from mod.json. Sister of port/src/modpack_pdmod.c's
 * findJsonStringValue, kept independent so the DLL has zero dependency on
 * the engine source tree.
 */
#ifndef _IN_PDMOD_JSON_MIN_H
#define _IN_PDMOD_JSON_MIN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns a pointer into `json` at the first byte of `key`'s string value,
 * with `*outLen` set to the value's length (excluding the surrounding
 * quotes). Returns NULL if the key is absent or its value is not a string.
 * The pointer is valid for the lifetime of `json`. */
const char *pdmodJsonFindString(const char *json, size_t json_len,
                                  const char *key, size_t *outLen);

#ifdef __cplusplus
}
#endif

#endif /* _IN_PDMOD_JSON_MIN_H */
