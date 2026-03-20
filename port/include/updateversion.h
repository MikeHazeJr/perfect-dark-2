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
	s32 patch;
	s32 dev;      /* 0 = stable release, >0 = dev build number (e.g., dev.3) */
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
#ifndef VERSION_DEV
#define VERSION_DEV 0
#endif

/* Build the compile-time version struct */
#define BUILD_VERSION_INIT { VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_DEV }

/* String form: "0.0.3a", "1.2.3", or "1.2.3-dev.4" */
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

#ifndef VERSION_LABEL
#define VERSION_LABEL ""
#endif

#if VERSION_DEV > 0
#define VERSION_STRING \
	STRINGIFY(VERSION_MAJOR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH) \
	"-dev." STRINGIFY(VERSION_DEV)
#else
#define VERSION_STRING \
	STRINGIFY(VERSION_MAJOR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH) \
	VERSION_LABEL
#endif

/* ========================================================================
 * Version comparison
 * ======================================================================== */

/**
 * Compare two versions.
 * Returns: <0 if a < b, 0 if equal, >0 if a > b.
 *
 * Ordering: 1.0.0-dev.1 < 1.0.0-dev.2 < 1.0.0 (stable) < 1.0.1-dev.1
 * A stable release (dev=0) is considered NEWER than any dev build of the
 * same major.minor.patch.
 */
static inline s32 versionCompare(const pdversion_t *a, const pdversion_t *b)
{
	if (a->major != b->major) return a->major - b->major;
	if (a->minor != b->minor) return a->minor - b->minor;
	if (a->patch != b->patch) return a->patch - b->patch;

	/* Both stable (dev=0): equal at this point */
	if (a->dev == 0 && b->dev == 0) return 0;

	/* Stable beats any dev of the same version */
	if (a->dev == 0) return 1;   /* a is stable, b is dev → a > b */
	if (b->dev == 0) return -1;  /* b is stable, a is dev → a < b */

	/* Both dev: compare dev number */
	return a->dev - b->dev;
}

/**
 * Parse a version string like "1.2.3" or "1.2.3-dev.4" into a pdversion_t.
 * Returns 0 on success, -1 on parse failure.
 */
s32 versionParse(const char *str, pdversion_t *out);

/**
 * Format a version into a string buffer.
 * Returns the number of characters written (excluding null terminator).
 */
s32 versionFormat(const pdversion_t *ver, char *buf, s32 bufsize);

/**
 * Parse a GitHub release tag like "client-v1.2.3" or "server-v1.0.0-dev.2".
 * Extracts the prefix ("client" or "server") and the version.
 * Returns 0 on success, -1 on parse failure.
 */
s32 versionParseTag(const char *tag, char *prefixbuf, s32 prefixbufsize, pdversion_t *ver);

#ifdef __cplusplus
}
#endif

#endif /* _IN_UPDATEVERSION_H */
