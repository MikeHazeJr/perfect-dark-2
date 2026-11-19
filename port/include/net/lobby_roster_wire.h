#ifndef PD_LOBBY_ROSTER_WIRE_H
#define PD_LOBBY_ROSTER_WIRE_H
#include "net/netlobby.h"
#ifdef __cplusplus
extern "C" {
#endif
struct netbuf;
/* Per player: client, state, room, team, three strings, then a flags byte
 * (bit 0: reviewing match stats, B-1190). */
#define LOBBY_ROSTER_WIRE_MAX (2 + LOBBY_MAX_PLAYERS * (5 + 35 + 67 + 67))
s32 lobbyRosterValid(const lobby_roster_snapshot_t *snapshot);
s32 lobbyRosterPayloadWrite(struct netbuf *dst, const lobby_roster_snapshot_t *snapshot);
s32 lobbyRosterPayloadRead(struct netbuf *src, lobby_roster_snapshot_t *snapshot);
#ifdef __cplusplus
}
#endif
#endif
