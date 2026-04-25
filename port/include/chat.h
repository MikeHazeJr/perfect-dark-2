/**
 * chat.h -- Phase 2 1:1 private chat over the connectivity layer.
 *
 * Friends exchange text messages via a dedicated signed UDP socket on
 * port 27106. The same socket is shared with the file-transfer module
 * (different magic / kind), but the chat module is self-contained:
 * `chat.c` only handles text frames and persistent per-friend history.
 *
 * Wire format -- 320 bytes per frame:
 *
 *   off len  field
 *   ----------------------------
 *    0  5    magic "PDCHT"
 *    5  1    version (1)
 *    6  1    kind  (0=text, 1=ack)
 *    7  1    flags
 *    8  4    sender handle
 *   12  4    target handle
 *   16  8    msg_id (u64; sender-local timestamp || counter)
 *   24  2    text_len (u16; bytes of payload actually used)
 *   26  2    chunk_seq (u16; 0-based)
 *   28  2    chunk_total (u16; 1-based)
 *   30  2    _pad
 *   32 192   utf-8 text payload (null-padded)
 *  224  32   sender Ed25519 pubkey
 *  256  64   Ed25519 signature over body[0..256) || domain
 *  320
 *
 * Signature domain: "pd-chat-v1".
 *
 * Persistence:
 *   <home>/social/chat/<handle_hex>.json
 * Per-friend rolling log of recent messages (CHAT_HISTORY_MAX entries).
 * Each entry carries text + direction + timestamp + optional
 * attachment descriptor (filled in by the file-transfer module on
 * receipt).
 */

#ifndef _IN_CHAT_H
#define _IN_CHAT_H

#include <PR/ultratypes.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHAT_TEXT_MAX           512  /* per message; multi-frame transport */
#define CHAT_HISTORY_MAX        128  /* per-friend rolling log size */
#define CHAT_FRAME_PAYLOAD_LEN  192  /* bytes carried per UDP frame */
#define CHAT_DOMAIN_TAG         "pd-chat-v1"

typedef enum {
	CHAT_DIR_OUT = 0,
	CHAT_DIR_IN  = 1,
	CHAT_DIR_SYS = 2,  /* system event: "Chris came online", etc. */
} chat_direction_t;

typedef struct chat_message_s {
	u64              msg_id;
	u32              peer_handle;       /* 0 for system events */
	chat_direction_t direction;
	u32              timestamp_unix;
	u32              attachment_kind;   /* 0 = none; populated by file_transfer */
	u64              attachment_size;
	char             attachment_name[64];
	char             attachment_path[256]; /* local path once a file lands */
	char             text[CHAT_TEXT_MAX];
} chat_message_t;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

void chatInit(void);
void chatShutdown(void);
void chatTick(void);

/* -------------------------------------------------------------------------
 * Sending
 * ------------------------------------------------------------------------- */

/**
 * Send a UTF-8 text message to a friend.  Returns 0 on success, -1 if
 * the friend is unknown / blocked / unreachable / message too long.
 * The message is appended to the local persistent history immediately;
 * the wire send retries silently on dropped packets up to a small
 * timeout window.
 */
s32 chatSendText(u32 friend_handle, const char *text);

/* -------------------------------------------------------------------------
 * History accessors (UI side)
 * ------------------------------------------------------------------------- */

/** Number of stored messages for a friend (0 if unknown). */
s32 chatHistoryCount(u32 friend_handle);

/**
 * Read the message at `idx` (0 = oldest). Returns NULL on OOB.
 * The pointer is valid until the next chatSendText / chat receive.
 */
const chat_message_t *chatHistoryAt(u32 friend_handle, s32 idx);

/** Clear stored history for a friend (also wipes the JSON file). */
void chatHistoryClear(u32 friend_handle);

/**
 * Append a system event into the history (appears as a sys-styled row
 * in the chat panel). Used by toast hooks for "X came online", etc.
 * Returns 0 on append.
 */
s32 chatHistoryAppendSystem(u32 friend_handle, const char *text);

/**
 * Hook used by file_transfer.c on a successful inbound transfer:
 * appends an IN-direction message with the attachment descriptor and
 * an empty text body. Saved automatically.
 */
s32 chatHistoryAppendAttachment(u32 friend_handle,
                                u32 attachment_kind,
                                const char *attachment_name,
                                const char *attachment_path,
                                u64 size);

#ifdef __cplusplus
}
#endif

#endif /* _IN_CHAT_H */
