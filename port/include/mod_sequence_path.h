#ifndef PORT_MOD_SEQUENCE_PATH_H
#define PORT_MOD_SEQUENCE_PATH_H

#include <stddef.h>
#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Replace the leaf member while preserving every enclosing archive boundary.
 * Supports loose files, one archive, and arbitrarily nested :: chains. */
s32 modSequenceSiblingPath(const char *path, const char *member,
	char *out, size_t out_n);

#ifdef __cplusplus
}
#endif

#endif
