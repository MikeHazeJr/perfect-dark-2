/**
 * updateversion.h -- Semantic versioning for the update system (D13).
 *
 * Defines the build version for both client and server, release channel
 * enumeration, and version comparison utilities.
 *
 * The actual version numbers are injected at build time via CMake into
 * versioninfo.h. This header provides the types and comparison functions.
 */

#ifndef _IN_UPDATEVERSION_H
#define _IN_UPDATEVERSION_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Version struct
 * ======================================================================== */

typedef struct pdversion {
	s32 major;
	s32 minor;
	s32 patch;    /* "revision" in the UI — third field of Major.Minor.Revision */
} pdversion_t;

/* ========================================================================
 * Release channels
 * ======================================================================== */

typedef enum {
	UPDATE_CHANNEL_STABLE = 0,   /* production releases only */
	UPDATE_CHANNEL_DEV    = 1,   /* includes prerelease/dev builds */
	UPDATE_CHANNEL_COUNT,
} update_channel_t;

/* ========================================================================
 * Compile-time version (populated from versioninfo.h defines)
 * ======================================================================== */

/* These are defined in versioninfo.h.in, injected by CMake.
 * Defaults here for safety if building outside CMake. */
#ifndef VERSION_MAJOR
#define VERSION_MAJOR 0
#endif
#ifndef VERSION_MINOR
#define VERSION_MINOR 0
#endif
#ifndef VERSION_PATCH
#define VERSION_PATCH 0
#endif
/* Build the compile-time version struct */
#define BUILD_VERSION_INIT { VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH }

/* String form: "Major.Minor.Revision" (e.g., "0.0.4", "1.2.3").
 * Channel (stable/dev) is determined by GitHub prerelease flag, not version. */
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

#define VERSION_STRING \
	STRINGIFY(VERSION_MAJOR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)

/* ========================================================================
 * Version comparison
 * ======================================================================== */

/**
 * Compare two versions.
 * Returns: <0 if a < b, 0 if equal, >0 if a > b.
 * Simple Major.Minor.Revision comparison.
 */
static inline s32 versionCompare(const pdversion_t *a, const pdversion_t *b)
{
	if (a->major != b->major) return a->major - b->major;
	if (a->minor != b->minor) return a->minor - b->minor;
	return a->patch - b->patch;
}

/**
 * Parse a version string like "1.2.3" into a pdversion_t.
 * Returns 0 on success, -1 on parse failure.
 */
s32 versionParse(const char *str, pdversion_t *out);

/**
 * Format a version into a string buffer.
 * Returns the number of characters written (excluding null terminator).
 */
s32 versionFormat(const pdversion_t *ver, char *buf, s32 bufsize);

/**
 * Parse a GitHub release tag like "client-v1.2.3" or "server-v1.0.0".
 * Extracts the prefix ("client" or "server") and the version.
 * Returns 0 on success, -1 on parse failure.
 */
s32 versionParseTag(const char *tag, char *prefixbuf, s32 prefixbufsize, pdversion_t *ver);

#ifdef __cplusplus
}
#endif

#endif /* _IN_UPDATEVERSION_H */
