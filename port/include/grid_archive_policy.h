#ifndef PD_GRID_ARCHIVE_POLICY_H
#define PD_GRID_ARCHIVE_POLICY_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GRID_ARENA_PROVENANCE_KEY "grid_provenance"
#define GRID_ARENA_PROVENANCE_BYOR "byor"
#define GRID_ARENA_PROVENANCE_ORIGINAL "original"
#define GRID_ARENA_PROVENANCE_UNVERIFIED "unverified"

typedef enum grid_archive_origin {
	GRID_ARCHIVE_LEGACY,
	GRID_ARCHIVE_BYOR,
	GRID_ARCHIVE_ORIGINAL,
	GRID_ARCHIVE_UNVERIFIED,
	GRID_ARCHIVE_INVALID
} grid_archive_origin_e;

/* Marked arena and weapon sources require an exact Grid offer and explicit
 * redistribution permission. Read provenance from the native descriptor. */
grid_archive_origin_e gridArchiveOrigin(const char *name, const void *bytes, u32 size);

/* Read the marker from the unique, canonical native descriptor of a typed
 * package. An absent marker returns LEGACY. Only ORIGINAL can enter a new
 * exact permission grant, after independent content and ownership checks. */
grid_archive_origin_e gridMatchPackageOrigin(const char *name, const void *bytes, u32 size);

#ifdef __cplusplus
}
#endif

#endif
