#include "net/lobby_view.h"
#include "net/netlobby.h"
#include "net/net.h"
#include <string.h>

int32_t lobbyProjectPlayerView(const struct lobbyplayer *player,
    int32_t is_local, int32_t state, struct lobbyplayer_view *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!player || !player->active || player->clientId >= LOBBY_MAX_PLAYERS)
        return 0;
    out->active = player->active;
    out->isLeader = player->isLeader;
    out->isReady = player->isReady;
    out->team = player->team;
    memcpy(out->name, player->name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = 0;
    out->isLocal = is_local != 0;
    out->state = state;
    out->clientId = player->clientId;
    return 1;
}

int32_t lobbyProjectViewIndex(int32_t index, int32_t room_only, struct lobbyplayer_view *out)
{
    const struct lobbyplayer *player = lobbyPlayerForView(index, room_only);
    if (!player) return lobbyProjectPlayerView(NULL, 0, 0, out);
    struct lobbyplayer projected = *player;
    if (room_only) projected.isLeader = projected.clientId == lobbyRoomLeaderClientId();
    return lobbyProjectPlayerView(&projected,
        g_NetLocalClient && projected.clientId == g_NetLocalClient->id,
        projected.state, out);
}

int32_t lobbyRoomGetPlayerCount(void) { return lobbyPlayerCountForView(1); }
int32_t lobbyRoomGetPlayerInfo(int32_t index, struct lobbyplayer_view *out)
{
    return lobbyProjectViewIndex(index, 1, out);
}
const char *lobbyRoomGetPlayerBodyId(int32_t index)
{
    const struct lobbyplayer *player = lobbyPlayerForView(index, 1);
    return player ? player->body_id : "";
}
const char *lobbyRoomGetPlayerHeadId(int32_t index)
{
    const struct lobbyplayer *player = lobbyPlayerForView(index, 1);
    return player ? player->head_id : "";
}
