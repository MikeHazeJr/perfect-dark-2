#ifndef PD_NET_PLAYER_MOVE_WIRE_H
#define PD_NET_PLAYER_MOVE_WIRE_H
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif
struct netbuf;
struct netplayermove;
/* Existing v61 payload layout. Decoder publishes only a complete finite record. */
u32 netbufWritePlayerMove(struct netbuf *, const struct netplayermove *);
u32 netbufReadPlayerMove(struct netbuf *, struct netplayermove *);
#ifdef __cplusplus
}
#endif
#endif
