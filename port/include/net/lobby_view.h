#ifndef PD_NET_LOBBY_VIEW_H
#define PD_NET_LOBBY_VIEW_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Shared C/C++ presentation contract. Catalog identities stay in lobbyplayer;
 * no legacy head/body indices or manually duplicated byte offsets. */
struct lobbyplayer_view {
    uint8_t active, isLeader, isReady, team;
    char name[32];
    int32_t isLocal, state;
    uint8_t clientId;
};
struct lobbyplayer;
int32_t lobbyProjectPlayerView(const struct lobbyplayer *player,
    int32_t is_local, int32_t state, struct lobbyplayer_view *out);
int32_t lobbyGetPlayerInfo(int32_t index, struct lobbyplayer_view *out);
int32_t lobbyProjectViewIndex(int32_t index, int32_t room_only, struct lobbyplayer_view *out);
int32_t lobbyRoomGetPlayerCount(void);
int32_t lobbyRoomGetPlayerInfo(int32_t index, struct lobbyplayer_view *out);
const char *lobbyRoomGetPlayerBodyId(int32_t index);
const char *lobbyRoomGetPlayerHeadId(int32_t index);
#ifdef __cplusplus
}
#endif
#endif
