#ifndef PD_NET_PLAYER_MOVE_WIRE_H
#define PD_NET_PLAYER_MOVE_WIRE_H
#include <PR/ultratypes.h>
/* Project-wide `bool` is `s32` (types.h). Never pull in <stdbool.h> here: it
 * would redefine `bool` as `_Bool` for the rest of any translation unit that
 * includes this header after types.h (net.c defines `extern bool` globals and
 * `bool`-returning functions declared in net.h with the 4-byte type). */
#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif
struct netbuf;
struct netplayermove;
struct netclient;
/* Existing v61 payload layout. Decoder publishes only a complete finite record. */
u32 netbufWritePlayerMove(struct netbuf *, const struct netplayermove *);
u32 netbufReadPlayerMove(struct netbuf *, struct netplayermove *);
/* Commit a decoded, policy-checked record atomically. Stale/duplicate records
 * do not change history, acknowledgement, or interpolation state. */
bool netPlayerMoveCommit(struct netclient *, const struct netplayermove *, u32 ack);
bool netPlayerMoveAcknowledged(const struct netclient *, u32 tick);
#ifdef __cplusplus
}
#endif
#endif
