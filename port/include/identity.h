/**
 * identity.h -- Player identity foundation.
 *
 * Manages a persistent device identity file (pd-identity.dat) containing:
 *   - A device UUID (16 bytes, generated once, never changes)
 *   - Up to 4 agent profiles (name, head, body)
 *
 * The UUID uniquely identifies a machine across sessions, enabling the hub
 * to recognize returning players even if they change their display name.
 * The active profile provides default settings for new connections.
 *
 * File location: <home>/pd-identity.dat
 * File format:   binary, little-endian, version-tagged.
 */

#ifndef _IN_IDENTITY_H
#define _IN_IDENTITY_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define IDENTITY_UUID_LEN       16
#define IDENTITY_MAX_PROFILES    4
#define IDENTITY_NAME_MAX       16  /* null-terminated, 15 usable chars */

/* File magic: ASCII "PDID" */
#define IDENTITY_MAGIC  0x44494450u

/* -------------------------------------------------------------------------
 * Data structures
 * ------------------------------------------------------------------------- */

/** One agent profile stored in the identity file. */
typedef struct identity_profile_s {
    char name[IDENTITY_NAME_MAX];  /**< Agent display name.        */
    u8   headnum;                  /**< Character head index.      */
    u8   bodynum;                  /**< Character body index.      */
    u8   flags;                    /**< Reserved, set to 0.        */
    u8   _pad;
} identity_profile_t;

/** Full in-memory identity state. */
typedef struct pd_identity_s {
    u8                  device_uuid[IDENTITY_UUID_LEN];
    u8                  profile_count;   /**< 1-IDENTITY_MAX_PROFILES   */
    u8                  active_profile;  /**< Index into profiles[]     */
    identity_profile_t  profiles[IDENTITY_MAX_PROFILES];
} pd_identity_t;

/* -------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

/**
 * Load identity from pd-identity.dat, or create a new one if absent.
 * Always succeeds: if the file is missing or corrupt, generates a fresh
 * identity with a new UUID and one default profile.
 */
void identityInit(void);

/** Save the current identity state to pd-identity.dat. */
void identitySave(void);

/** Return a pointer to the in-memory identity (always valid after identityInit). */
pd_identity_t *identityGet(void);

/** Return the active profile (convenience wrapper). */
identity_profile_t *identityGetActiveProfile(void);

/**
 * Format the device UUID as a lowercase hex string.
 * @param buf     Output buffer, must be at least 33 bytes (32 hex + null).
 * @param buflen  Size of buf.
 */
void identityFormatUUID(char *buf, u32 buflen);

#ifdef __cplusplus
}
#endif

#endif /* _IN_IDENTITY_H */
