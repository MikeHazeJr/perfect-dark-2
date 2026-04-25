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
#include "assetcatalog.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define IDENTITY_UUID_LEN       16
#define IDENTITY_MAX_PROFILES    4
#define IDENTITY_NAME_MAX       16  /* null-terminated, 15 usable chars */
#define IDENTITY_PRIVKEY_LEN    32  /* Ed25519 raw private (seed) bytes */
#define IDENTITY_PUBKEY_LEN     32  /* Ed25519 raw public bytes */

/* File magic: ASCII "PDID" */
#define IDENTITY_MAGIC  0x44494450u

/* -------------------------------------------------------------------------
 * Data structures
 * ------------------------------------------------------------------------- */

/** One agent profile stored in the identity file. */
typedef struct identity_profile_s {
    char name[IDENTITY_NAME_MAX];          /**< Agent display name.              */
    char head_id[CATALOG_ID_LEN];          /**< SA-4: head catalog string ID.    */
    char body_id[CATALOG_ID_LEN];          /**< SA-4: body catalog string ID.    */
    u8   flags;                            /**< Reserved, set to 0.              */
    u8   _pad[3];
} identity_profile_t;

/** Full in-memory identity state. */
typedef struct pd_identity_s {
    u8                  device_uuid[IDENTITY_UUID_LEN];
    u8                  profile_count;   /**< 1-IDENTITY_MAX_PROFILES   */
    u8                  active_profile;  /**< Index into profiles[]     */
    identity_profile_t  profiles[IDENTITY_MAX_PROFILES];
    /** Ed25519 keypair (Phase 1 connectivity, 2026-04-25).  The private
     *  key is the raw 32-byte seed.  Generated once on first launch and
     *  persisted in pd-identity.dat v3.  NEVER leaves the device. */
    u8                  ed25519_priv[IDENTITY_PRIVKEY_LEN];
    u8                  ed25519_pub [IDENTITY_PUBKEY_LEN];
    u8                  has_ed25519;     /**< 1 once a keypair has been generated */
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

/**
 * Read-only accessors for the Ed25519 identity. Pointers are stable for
 * the lifetime of the process. Returns NULL if no keypair has been
 * generated (pre-init or generation failure).
 */
const u8 *identityGetPubkey(void);
const u8 *identityGetPrivkey(void);

/**
 * Sign a message with the local identity's private key.
 *
 * @param msg       message bytes.
 * @param msgLen    length of msg.
 * @param outSig    receives the 64-byte signature.
 *
 * @return 1 on success, 0 on failure (no keypair, OpenSSL error).
 */
s32 identitySign(const void *msg, u32 msgLen, u8 *outSig /* 64 bytes */);

#ifdef __cplusplus
}
#endif

#endif /* _IN_IDENTITY_H */
