#ifndef PD_NET_ROOM_WIRE_H
#define PD_NET_ROOM_WIRE_H
#include "room.h"
#ifdef __cplusplus
extern "C" {
#endif
struct netbuf;
typedef struct net_room_create_request {
    char name[ROOM_NAME_MAX];
    u8 access;
    char password[32];
    u8 max_players;
    room_settings_t settings;
    char playlist[ROOM_PLAYLIST_TEXT_MAX];
} net_room_create_request_t;
/* Payload only. The dispatcher owns the CLC_ROOM_CREATE opcode. */
s32 netRoomCreatePayloadWrite(struct netbuf *dst, const net_room_create_request_t *request);
s32 netRoomCreatePayloadRead(struct netbuf *src, net_room_create_request_t *request);
#ifdef __cplusplus
}
#endif
#endif
