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
	u8   _pad[3];
	u64  last_seen_unix;    /* unix seconds */
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
