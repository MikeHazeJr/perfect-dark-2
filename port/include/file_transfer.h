/**
 * file_transfer.h -- Phase 2 chunked + sha256-verified file pipe.
 *
 * Friends send arbitrary files (mods, music, images, saves, replays,
 * other) through a dedicated signed UDP frame on port 27107. Each
 * transfer carries a sha256 digest computed by the sender; the
 * receiver verifies before accepting the file and refuses on mismatch
 * (rejects the file with a sidecar log).
 *
 * Per-type inbox layout (Q18 amendment in connectivity-and-modern-main-menu.md
 * Section 8.5.1):
 *
 *   <home>/social/inbox/mods/<friend_agent>/<original_name>
 *   <home>/social/inbox/music/<friend_agent>/<original_name>
 *   <home>/social/inbox/images/<friend_agent>/<original_name>
 *   <home>/social/inbox/saves/<friend_agent>/<original_name>
 *   <home>/social/inbox/replays/<friend_agent>/<original_name>
 *   <home>/social/inbox/files/<friend_agent>/<original_name>
 *
 * Each landing path also gets a `<original>.meta.json` sidecar with
 * sender handle / agent / received_at / sha256 / size / kind.
 *
 * Per-type size cap (default; a future settings tab may override):
 *
 *   mods    250 MB
 *   music    50 MB
 *   images   25 MB
 *   saves     5 MB
 *   replays 100 MB
 *   files    50 MB
 *
 * Wire frames are dispatched by kind (init, chunk, end, ack). Reliable
 * in user-space: receiver acks each chunk, sender retransmits absent
 * acks. Chunk size = 1024 bytes payload (frame total 1280 bytes
 * including header + signature).
 */

#ifndef _IN_FILE_TRANSFER_H
#define _IN_FILE_TRANSFER_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FT_KIND_OTHER    0
#define FT_KIND_MOD      1
#define FT_KIND_MUSIC    2
#define FT_KIND_IMAGE    3
#define FT_KIND_SAVE     4
#define FT_KIND_REPLAY   5

#define FT_CHUNK_PAYLOAD 1024
#define FT_FRAME_LEN     1280

void fileTransferInit(void);
void fileTransferShutdown(void);
void fileTransferTick(void);

/**
 * Initiate a send to a friend. Returns 0 on success, -1 on bad input or
 * if the file does not exist. The actual send progresses asynchronously
 * via fileTransferTick. Progress can be polled via the inbound side's
 * progress accessor on the receiver; the sender currently fires
 * fire-and-forget with retransmit.
 */
s32 fileTransferSendFile(u32 friend_handle, const char *local_path);

/**
 * Classify a file by extension into the FT_KIND_* taxonomy. Returns
 * FT_KIND_OTHER for unknown extensions. Used by sender + UI.
 */
s32 fileTransferClassifyByExt(const char *filename);

/** Returns the documented per-type cap in bytes. */
u64 fileTransferKindSizeLimit(s32 kind);

/** Returns a printable kind name ("mod", "music", etc.). */
const char *fileTransferKindName(s32 kind);

/* -------------------------------------------------------------------------
 * Convert-to-mod modal back-end.
 *
 * Builds a `.pdmod` archive at <home>/mods/installed/<id>.pdmod with a
 * mod.json at the archive root and the source file copied at
 * audio/<safe>.ext. Currently supports music sources (mp3/ogg/wav/flac).
 * Returns 0 on success.
 *
 * The UI gathers (name, description, creator, tags, version) and calls
 * this once the form is valid. The function does NOT touch the mod
 * registry; the caller (the mod manager) re-scans after creation.
 * ------------------------------------------------------------------------- */

s32 fileTransferConvertMusicToMod(const char *source_path,
                                  const char *display_name,
                                  const char *description,
                                  const char *creator,
                                  const char *tags,
                                  const char *version,
                                  char *out_pdmod_path,
                                  u32 out_pdmod_path_size);

#ifdef __cplusplus
}
#endif

#endif /* _IN_FILE_TRANSFER_H */
