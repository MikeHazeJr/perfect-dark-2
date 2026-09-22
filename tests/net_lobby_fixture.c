#include "net/net.h"
#include "net/netlobby.h"
#include "room.h"
#include <string.h>
#include <stdio.h>

s32 g_NetMode;
struct netclient g_NetClients[NET_MAX_CLIENTS + 1];
struct netclient *g_NetLocalClient;

void lobbyFixtureReset(int server, int local_id)
{
    memset(g_NetClients, 0, sizeof(g_NetClients));
    for (int i = 0; i <= NET_MAX_CLIENTS; ++i) {
        g_NetClients[i].id = (u8)i;
        g_NetClients[i].room_id = 0xFF;
        g_NetClients[i].reconnect_preserved_index = NET_NULL_CLIENT;
    }
    g_NetMode = server ? NETMODE_SERVER : NETMODE_CLIENT;
    g_NetLocalClient = local_id >= 0 && local_id < NET_MAX_CLIENTS
        ? &g_NetClients[local_id] : NULL;
    memset(g_RoomCache, 0, sizeof(g_RoomCache));
    g_RoomCacheCount = 0;
    g_LocalRoomId = 0xFF;
    roomsInit();
    for (u8 i = 0; i < HUB_MAX_ROOMS; ++i) {
        hub_room_t *room = roomGetById(i);
        if (room) roomDestroy(room);
    }
    lobbyInit();
}
void lobbyFixtureClient(int index, const char *name, int state)
{
    if (index < 0 || index >= NET_MAX_CLIENTS) return;
    g_NetClients[index].state = state;
    snprintf(g_NetClients[index].settings.name,
        sizeof(g_NetClients[index].settings.name), "%s", name);
}
void lobbyFixtureLocalRoom(unsigned char room_id)
{
    if (g_NetLocalClient) g_NetLocalClient->room_id = room_id;
    g_LocalRoomId = room_id;
}
int lobbyFixtureLobbyState(void) { return CLSTATE_LOBBY; }
int lobbyFixturePreparingState(void) { return CLSTATE_PREPARING; }
int lobbyFixtureGameState(void) { return CLSTATE_GAME; }

void lobbyFixtureClientRoom(int index, unsigned char room)
{
    if (index >= 0 && index < NET_MAX_CLIENTS) g_NetClients[index].room_id = room;
}
int lobbyFixtureClientState(int index)
{
    return index >= 0 && index < NET_MAX_CLIENTS ? g_NetClients[index].state : -1;
}

void lobbyFixturePendingReconnect(int index)
{
    if (index >= 0 && index < NET_MAX_CLIENTS)
        g_NetClients[index].reconnect_preserved_index = 0;
}
