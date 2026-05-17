/**
 * social.h -- Always-on social storage for Phase 1 connectivity.
 *
 * Owns the friend list, block list, social-visibility setting, notification
 * toggles, and the local player's stable connect code. Persists to JSON
 * under the user-data root (sysGetHomePath()), specifically:
 *
 *   <home>/social/friends.json    -- friend table
 *   <home>/social/blocks.json     -- block table
 *   <home>/social/presence.json   -- visibility + notification mask
 *
 * Persistence is at the device / account level, NOT per save profile (Q6).
 * A new save profile inherits the device's existing social state.
 *
 * Connect codes are derived deterministically from the device UUID
 * (identity.c device_uuid) so a player's "agentcode" is stable across
 * reinstalls of this build, but private to this device. The four-word
 * phrase reuses the connectcode.c word dictionary (slot 0..3) but is NOT
 * an IP -- it is a 32-bit identity handle.
 */

#ifndef _IN_SOCIAL_H
#define _IN_SOCIAL_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOCIAL_AGENTNAME_MAX  16   /* matches IDENTITY_NAME_MAX */
#define SOCIAL_NICKNAME_MAX   32
#define SOCIAL_CONNECTCODE_MAX 96  /* "fat vampire running to the park" + null */
#define SOCIAL_FRIENDS_MAX    128
#define SOCIAL_BLOCKS_MAX      64
#define SOCIAL_PUBKEY_LEN      32  /* Ed25519 raw public key, TOFU-bound */

/* -------------------------------------------------------------------------
 * Visibility (Q7) -- three-state.
 * Default for fresh installs: SOCIAL_VIS_FRIENDS_ONLY.
 * ------------------------------------------------------------------------- */

typedef enum {
	SOCIAL_VIS_PUBLIC        = 0,
	SOCIAL_VIS_FRIENDS_ONLY  = 1,
	SOCIAL_VIS_APPEAR_OFFLINE = 2,
} social_visibility_t;

/* -------------------------------------------------------------------------
 * Notification categories (Q8) -- bitmask, both default ON.
 * ------------------------------------------------------------------------- */

#define SOCIAL_NOTIF_SOCIAL    (1u << 0)  /* "X came online", achievements */
#define SOCIAL_NOTIF_INVITES   (1u << 1)  /* invitations + chat (Phase 2) */
#define SOCIAL_NOTIF_DEFAULT   (SOCIAL_NOTIF_SOCIAL | SOCIAL_NOTIF_INVITES)

/* -------------------------------------------------------------------------
 * Friend / block records.
 * connect_code is the 4-word string e.g. "fat vampire running to the park".
 * agent_name is the friend's chosen handle (synced via presence).
 * nickname is local-only annotation; if empty, UI shows "[agent]" alone.
 * muted suppresses toasts but not invites (Q9).
 * last_seen_unix records the last successful presence pong (0 = never).
 * ------------------------------------------------------------------------- */

typedef struct social_friend_s {
	char connect_code[SOCIAL_CONNECTCODE_MAX];
	char agent_name[SOCIAL_AGENTNAME_MAX];
	char nickname[SOCIAL_NICKNAME_MAX];
	u32  handle;            /* 32-bit identity hash; matches connect_code */
	u8   muted;             /* per-friend mute (Q9) */
	u8   has_pubkey;        /* 1 once TOFU bind has cached pubkey */
	u8   _pad[2];
	u64  last_seen_unix;    /* unix seconds */
	/* TOFU-bound Ed25519 public key for the friend's identity. Populated
	 * on first verified presence ping; subsequent pings must carry the
	 * matching key or are rejected. The connect-code handle is bound to
	 * this key by SHA256(pubkey || domain)[:4]. */
	u8   pubkey[SOCIAL_PUBKEY_LEN];
	/* Cached endpoint (Section 3 endpoint resolution flow). 0 / 0 / 0
	 * means cold; ttl_unix is the wall-clock second after which the
	 * cache is stale. */
	u32  endpoint_ipv4;     /* host order */
	u16  endpoint_port;     /* host order */
	u16  _pad2;
	u64  endpoint_ttl_unix;
} social_friend_t;

typedef struct social_block_s {
	char connect_code[SOCIAL_CONNECTCODE_MAX];
	char agent_name[SOCIAL_AGENTNAME_MAX]; /* snapshot at block time */
	u32  handle;
	u32  _pad;
} social_block_t;

/* -------------------------------------------------------------------------
 * Lifecycle.
 * ------------------------------------------------------------------------- */

/**
 * Load social state from disk; create defaults if absent.
 * Idempotent. MUST follow identityInit() so the local connect code can be
 * derived from the device UUID. Safe to call before netInit().
 */
void socialInit(void);

/** Save all three JSON files atomically (rename on success). */
void socialSave(void);

/** Has socialInit() completed at least once? */
s32 socialIsReady(void);

/* -------------------------------------------------------------------------
 * Local identity.
 *
 * The 32-bit handle is sha256(device_uuid || "pd-social-connect-v1\n")
 * truncated to the first 4 bytes. The connect-code phrase is the same
 * 4 bytes encoded via connectCodeEncode (reusing the word dictionary).
 * ------------------------------------------------------------------------- */

/** Returns the local handle (stable per-device). */
u32 socialMyHandle(void);

/** Returns the local 4-word connect code. Caller must not modify. */
const char *socialMyConnectCode(void);

/** Returns the local agent name from identityGetActiveProfile(). */
const char *socialMyAgentName(void);

/**
 * Mike directive 2026-05-17: rebind the local handle + connect code to
 * the named agent profile. The handle becomes a hash of (pubkey ||
 * agent_name) so two agents on the same device get distinct connect
 * codes. Idempotent; safe to call on every agent switch. Re-encodes
 * s_MyConnectCode on success.
 *
 * agent_name MUST be the save-slot name (e.g. "MikeHazeJr", "allen")
 * picked from Agent Select, NOT the identity profile name (which today
 * is always "Agent" because there is a single identity keypair per
 * device).  Callers: prefs_agent.c::prefsAgentLoad, main.c::
 * bootLaunchLoadAgentTick.
 */
void socialRebindToActiveAgent(const char *agent_name);

/* -------------------------------------------------------------------------
 * Friend list.
 * ------------------------------------------------------------------------- */

s32 socialFriendCount(void);
const social_friend_t *socialFriendAt(s32 idx);
const social_friend_t *socialFriendByCode(const char *connect_code);
const social_friend_t *socialFriendByHandle(u32 handle);

/**
 * Add a friend by connect code.
 * agent_name may be NULL or empty if not yet known (will be filled in on
 * first presence pong).
 * Returns 1 on add, 0 if already in list, -1 on invalid input or full.
 */
s32 socialFriendAdd(const char *connect_code, const char *agent_name);

/** Remove by connect code. Returns 1 on success, 0 if not found. */
s32 socialFriendRemove(const char *connect_code);

/** Set the local-only nickname for a friend. Empty string clears. */
s32 socialFriendSetNickname(const char *connect_code, const char *nickname);

/** Mute / unmute a friend (Q9). */
s32 socialFriendSetMuted(const char *connect_code, s32 muted);

/** Update agent_name (e.g. after a presence pong reveals a rename). */
s32 socialFriendUpdateAgentName(const char *connect_code, const char *agent_name);

/** Stamp last_seen_unix to now. Returns 1 if friend existed, 0 otherwise. */
s32 socialFriendTouchSeen(const char *connect_code);

/**
 * Bind the friend's Ed25519 pubkey on first verified contact (TOFU).
 *
 * @return  1 on bind, 0 if friend not found, -1 if a pubkey is already
 *          cached and differs from the supplied bytes (caller should
 *          treat that as an identity-changed event).
 */
s32 socialFriendBindPubkey(u32 handle, const u8 pubkey[SOCIAL_PUBKEY_LEN]);

/**
 * Update the cached endpoint for a friend after a successful verified
 * pong. ttl_seconds is added to the wall clock to compute the new
 * expiry. Pass 0 to clear the cache.
 */
s32 socialFriendUpdateEndpoint(u32 handle, u32 ipv4, u16 port, u32 ttl_seconds);

/**
 * Returns 1 + writes endpoint to the out-pointers if the cached endpoint
 * is non-zero AND the TTL has not expired (against current wall clock).
 * Returns 0 otherwise.
 */
s32 socialFriendGetEndpoint(u32 handle, u32 *out_ipv4, u16 *out_port);

/**
 * Verify that SHA256(pubkey || "pd-social-connect-v1")[:4] equals the
 * supplied 32-bit handle. Returns 1 if bound, 0 otherwise. Used by the
 * presence layer to refuse spoofed (handle, pubkey) combinations before
 * even verifying the signature.
 */
s32 socialHandleBindsPubkey(u32 handle, const u8 pubkey[SOCIAL_PUBKEY_LEN]);

/* -------------------------------------------------------------------------
 * Block list.
 *
 * Block effect is symmetric: the blocked party becomes invisible to the
 * blocker AND the blocker becomes invisible to the blocked party. This
 * is enforced at the presence layer (Section 3.3). Storage here is just
 * the membership set.
 * ------------------------------------------------------------------------- */

s32 socialBlockCount(void);
const social_block_t *socialBlockAt(s32 idx);
s32 socialBlockIs(const char *connect_code);
s32 socialBlockIsHandle(u32 handle);
s32 socialBlockAdd(const char *connect_code, const char *agent_name);
s32 socialBlockRemove(const char *connect_code);

/* -------------------------------------------------------------------------
 * Visibility + notifications.
 * ------------------------------------------------------------------------- */

social_visibility_t socialVisibilityGet(void);
void socialVisibilitySet(social_visibility_t v);

u32 socialNotifMaskGet(void);
void socialNotifMaskSet(u32 mask);
s32 socialNotifIsSet(u32 category);

/* -------------------------------------------------------------------------
 * Helpers.
 * ------------------------------------------------------------------------- */

/**
 * Encode a 32-bit handle into a 4-word phrase using the connect-code
 * dictionary. Output buffer must be at least SOCIAL_CONNECTCODE_MAX bytes.
 * Returns 0 on success, -1 on bad arguments.
 */
s32 socialEncodeHandle(u32 handle, char *out, u32 outsize);

/**
 * Decode a 4-word phrase back to a 32-bit handle. Whitespace / hyphen /
 * dot tolerant; case-insensitive. Returns 0 on success, -1 on parse error.
 */
s32 socialDecodeHandle(const char *code, u32 *out_handle);

/**
 * Format a friend's display label as "[nickname]: [agent_name]" or, when
 * nickname is empty, just "[agent_name]". Truncates to outsize.
 */
void socialFormatDisplay(const social_friend_t *f, char *out, u32 outsize);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SOCIAL_H */
