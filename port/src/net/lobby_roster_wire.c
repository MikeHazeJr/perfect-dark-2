#include "net/lobby_roster_wire.h"
#include "net/net.h"
#include "room.h"
#include <string.h>

static s32 boundedString(const char *text, size_t capacity)
{
    return memchr(text, 0, capacity) != NULL;
}

s32 lobbyRosterValid(const lobby_roster_snapshot_t *snapshot)
{
    if (!snapshot || snapshot->count > LOBBY_MAX_PLAYERS) return 0;
    u32 seen = 0;
    for (u8 i = 0; i < snapshot->count; ++i) {
        const struct lobbyplayer *p = &snapshot->players[i];
        if (p->active != 1 || p->clientId >= NET_MAX_CLIENTS
                || p->state < CLSTATE_LOBBY || p->state > CLSTATE_PREPARING
                || (p->roomId != 0xff && p->roomId >= HUB_MAX_ROOMS)
                || (p->team != 0xff && p->team >= MAX_TEAMS)
                || !boundedString(p->name, sizeof(p->name)) || !p->name[0]
                || !boundedString(p->body_id, sizeof(p->body_id))
                || !boundedString(p->head_id, sizeof(p->head_id))
                || (seen & (1u << p->clientId))) return 0;
        seen |= 1u << p->clientId;
    }
    return snapshot->leaderClientId == 0xff
        || (snapshot->leaderClientId < NET_MAX_CLIENTS
            && (seen & (1u << snapshot->leaderClientId)) != 0);
}

static size_t wireStringSize(const char *text)
{
    /* netbufWriteStr includes the terminator even for an empty string. */
    return strlen(text) + 3;
}

s32 lobbyRosterPayloadWrite(struct netbuf *dst, const lobby_roster_snapshot_t *snapshot)
{
    if (!dst || dst->error || !lobbyRosterValid(snapshot)) return 0;
    size_t required = 2;
    for (u8 i = 0; i < snapshot->count; ++i) {
        const struct lobbyplayer *p = &snapshot->players[i];
        required += 4 + wireStringSize(p->name) + wireStringSize(p->body_id)
            + wireStringSize(p->head_id);
    }
    if (netbufWriteLeft(dst) < 0 || required > (size_t)netbufWriteLeft(dst)) {
        dst->error = 1;
        return 0;
    }
    netbufWriteU8(dst, snapshot->count);
    netbufWriteU8(dst, snapshot->leaderClientId);
    for (u8 i = 0; i < snapshot->count; ++i) {
        const struct lobbyplayer *p = &snapshot->players[i];
        netbufWriteU8(dst, p->clientId);
        netbufWriteU8(dst, p->state);
        netbufWriteU8(dst, p->roomId);
        netbufWriteU8(dst, p->team);
        netbufWriteStr(dst, p->name);
        netbufWriteStr(dst, p->body_id);
        netbufWriteStr(dst, p->head_id);
    }
    return !dst->error;
}

static s32 readString(struct netbuf *src, char *out, size_t capacity)
{
    const u32 start = src->rp;
    const char *text = netbufReadStr(src);
    if (src->error || !text) return 0;
    const size_t length = strlen(text);
    if (length >= capacity || (src->rp - start != wireStringSize(text)
            && !(length == 0 && src->rp - start == 2))) return 0;
    memcpy(out, text, length + 1);
    return 1;
}

s32 lobbyRosterPayloadRead(struct netbuf *src, lobby_roster_snapshot_t *snapshot)
{
    if (!src || !snapshot || src->error) return 0;
    lobby_roster_snapshot_t candidate = {0};
    candidate.count = netbufReadU8(src);
    candidate.leaderClientId = netbufReadU8(src);
    if (src->error || candidate.count > LOBBY_MAX_PLAYERS) goto invalid;
    for (u8 i = 0; i < candidate.count; ++i) {
        struct lobbyplayer *p = &candidate.players[i];
        p->active = 1;
        p->clientId = netbufReadU8(src);
        p->state = netbufReadU8(src);
        p->roomId = netbufReadU8(src);
        p->team = netbufReadU8(src);
        if (src->error || !readString(src, p->name, sizeof(p->name))
                || !readString(src, p->body_id, sizeof(p->body_id))
                || !readString(src, p->head_id, sizeof(p->head_id))) goto invalid;
        p->isReady = p->state == CLSTATE_GAME;
        p->isLeader = p->clientId == candidate.leaderClientId;
    }
    if (!lobbyRosterValid(&candidate)) goto invalid;
    *snapshot = candidate;
    return 1;
invalid:
    src->error = 1;
    return 0;
}
