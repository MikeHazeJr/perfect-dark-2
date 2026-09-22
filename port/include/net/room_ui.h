#ifndef PD_NET_ROOM_UI_H
#define PD_NET_ROOM_UI_H
#include <PR/ultratypes.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Operations use the same authority handler for local hosts and remote peers. */
s32 netRequestRoomCreate(const char *name, u8 access, const char *password, u8 max_players);
s32 netRequestRoomJoin(u8 room_id, const char *password);
s32 netRoomRequestPending(void);
const char *netRoomRequestMessage(void);
void netRoomRequestReset(void);
#ifdef __cplusplus
}
#endif
#endif
