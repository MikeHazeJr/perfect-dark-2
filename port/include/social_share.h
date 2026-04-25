/**
 * social_share.h -- Phase 4 follow-up wire layer for cross-peer
 * presence-aux data: listening-room playlists, public mods manifests,
 * profile stats, and on-demand mod requests.
 *
 * Runs on its own signed UDP socket (port 27109) to keep the existing
 * presence frame fixed-size and to allow larger payloads (manifests
 * can carry up to SHARE_PAYLOAD_MAX bytes per frame).
 *
 * Frame format (variable length, max 1500 bytes for MTU safety):
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDSHR"
 *    5  1    version (1)
 *    6  1    kind (1=lr_manifest, 2=mods_manifest, 3=profile_stats,
 *                  4=mod_request, 5=mod_offer)
 *    7  1    flags
 *    8  4    sender handle
 *   12  4    target handle (0 = broadcast to all friends)
 *   16  4    seq
 *   20  2    payload_len
 *   22  N    payload (sender-defined per kind)
 *   22+N 32  sender pubkey
 *   54+N 64  signature over body[0..22+N+32) || domain
 *
 * Signature domain: "pd-share-v1".
 *
 * Trust pipeline mirrors chat / file_transfer: friend allowlist,
 * rate-limit, signature verify, TOFU pubkey lock.
 */

#ifndef _IN_SOCIAL_SHARE_H
#define _IN_SOCIAL_SHARE_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHARE_KIND_LR_MANIFEST    1
#define SHARE_KIND_MODS_MANIFEST  2
#define SHARE_KIND_PROFILE_STATS  3
#define SHARE_KIND_MOD_REQUEST    4
#define SHARE_KIND_MOD_OFFER      5

#define SHARE_PAYLOAD_MAX  1024

void shareInit(void);
void shareShutdown(void);
void shareTick(void);

/* -------------------------------------------------------------------------
 * Listening-room manifest (host -> friend listeners on subscribe)
 *
 * Payload layout: u8 track_count, then for each track:
 *   u8 id_len, id_len bytes (track id),
 *   u8 name_len, name_len bytes (display_name),
 *   u32 duration_ms, u8 is_current.
 * ------------------------------------------------------------------------- */

void shareBroadcastListeningRoom(void);

/* -------------------------------------------------------------------------
 * Public mods manifest (peer -> all friends on rotation)
 *
 * Payload layout: u8 mod_count, then for each mod:
 *   u8 id_len, id_len bytes (mod_id),
 *   u8 name_len, name_len bytes (display name),
 *   u8 version_len, version_len bytes (e.g. "1.2.0"),
 *   u32 size_bytes, u8 sha256_present, [32 bytes sha256 if present].
 * ------------------------------------------------------------------------- */

void shareBroadcastPublicMods(void);

/* Local public-mod registry. Persisted at <home>/social/mod-public.json
 * so Priority M's mod loader / mod.json schema does NOT need a
 * `public` field; the toggle lives entirely in the connectivity layer.
 * UI calls shareModPublicAdd / Remove via the Public Mods tab. */
s32 shareModPublicAdd(const char *mod_id, const char *display_name,
                      const char *version, u32 size_bytes);
s32 shareModPublicRemove(const char *mod_id);
s32 shareModPublicCount(void);
const char *shareModPublicIdAt(s32 idx);
const char *shareModPublicNameAt(s32 idx);

/* -------------------------------------------------------------------------
 * Profile stats (peer -> all friends, on rotation or on REQUEST)
 *
 * Payload layout: u32 kills, u32 deaths, u32 missions_completed,
 *                 u32 _reserved.
 * ------------------------------------------------------------------------- */

void shareBroadcastProfileStats(void);

/* -------------------------------------------------------------------------
 * Mod request (listener / browser -> mod owner)
 *
 * Payload layout: u8 id_len, id_len bytes (mod_id).
 *
 * On receipt the mod owner enumerates the local mod, finds the local
 * file path, and calls fileTransferSendFile(target_handle, path).
 * ------------------------------------------------------------------------- */

s32 shareSendModRequest(u32 friend_handle, const char *mod_id);

/* -------------------------------------------------------------------------
 * Aggregator accessors (UI side)
 * ------------------------------------------------------------------------- */

#define SHARE_AGGREGATE_MAX 64
#define SHARE_MOD_NAME_MAX  64
#define SHARE_MOD_VER_MAX   16

typedef struct share_mod_entry_s {
	u32  owner_handle;
	char mod_id[64];
	char display_name[SHARE_MOD_NAME_MAX];
	char version[SHARE_MOD_VER_MAX];
	u32  size_bytes;
	u8   has_sha256;
	u8   _pad[3];
	u8   sha256[32];
	u32  last_seen_ms;
} share_mod_entry_t;

s32 shareAggregateModCount(void);
const share_mod_entry_t *shareAggregateModAt(s32 idx);

typedef struct share_profile_s {
	u32  owner_handle;
	u32  kills;
	u32  deaths;
	u32  missions_completed;
	u32  last_seen_ms;
} share_profile_t;

const share_profile_t *shareProfileFor(u32 friend_handle);

#ifdef __cplusplus
}
#endif

#endif /* _IN_SOCIAL_SHARE_H */
