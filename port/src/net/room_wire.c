#include "net/room_wire.h"
#include "net/netbuf.h"
#include <string.h>

static s32 terminated(const char *text, size_t capacity)
{
    return memchr(text, 0, capacity) != NULL;
}

static s32 readString(struct netbuf *src, char *dst, size_t capacity)
{
    const u32 start = src->rp;
    const char *text = netbufReadStr(src);
    if (src->error || !text) return 0;
    const size_t length = strlen(text);
    /* Reject hidden bytes behind embedded NULs, without publishing a prefix. */
    if (length >= capacity || (src->rp - start != length + 3
            && !(length == 0 && src->rp - start == 2))) {
        src->error = 1;
        return 0;
    }
    memcpy(dst, text, length + 1);
    return 1;
}

s32 netRoomCreatePayloadWrite(struct netbuf *dst, const net_room_create_request_t *request)
{
    if (!dst || !request || dst->error
            || !terminated(request->name, sizeof(request->name))
            || !terminated(request->password, sizeof(request->password))
            || !terminated(request->settings.stage_id, sizeof(request->settings.stage_id))
            || !terminated(request->playlist, sizeof(request->playlist))) return 0;
    const size_t required = 25 + strlen(request->name) + strlen(request->password)
        + strlen(request->settings.stage_id) + strlen(request->playlist);
    if (netbufWriteLeft(dst) < 0 || required > (size_t)netbufWriteLeft(dst)) {
        dst->error = 1;
        return 0;
    }
    netbufWriteStr(dst, request->name);
    netbufWriteU8(dst, request->access);
    netbufWriteStr(dst, request->password);
    netbufWriteU8(dst, request->max_players);
    netbufWriteU8(dst, request->settings.num_bots);
    netbufWriteU8(dst, request->settings.timelimit);
    netbufWriteU8(dst, request->settings.scorelimit);
    netbufWriteU16(dst, request->settings.teamscorelimit);
    netbufWriteU32(dst, request->settings.options);
    netbufWriteU8(dst, request->settings.scenario);
    netbufWriteU8(dst, request->settings.weapon_set);
    netbufWriteStr(dst, request->settings.stage_id);
    netbufWriteStr(dst, request->playlist);
    return !dst->error;
}

s32 netRoomCreatePayloadRead(struct netbuf *src, net_room_create_request_t *request)
{
    if (!src || !request || src->error) return 0;
    net_room_create_request_t candidate = {0};
    if (!readString(src, candidate.name, sizeof(candidate.name))) return 0;
    candidate.access = netbufReadU8(src);
    if (!readString(src, candidate.password, sizeof(candidate.password))) return 0;
    candidate.max_players = netbufReadU8(src);
    candidate.settings.num_bots = netbufReadU8(src);
    candidate.settings.timelimit = netbufReadU8(src);
    candidate.settings.scorelimit = netbufReadU8(src);
    candidate.settings.teamscorelimit = netbufReadU16(src);
    candidate.settings.options = netbufReadU32(src);
    candidate.settings.scenario = netbufReadU8(src);
    candidate.settings.weapon_set = netbufReadU8(src);
    if (!readString(src, candidate.settings.stage_id, sizeof(candidate.settings.stage_id))
            || !readString(src, candidate.playlist, sizeof(candidate.playlist)) || src->error) return 0;
    *request = candidate;
    return 1;
}
