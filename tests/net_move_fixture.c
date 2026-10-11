#include "net/net.h"
#include "net/netbuf.h"
#include "net/net_player_move_wire.h"
#include "net_move_fixture.h"
#include <string.h>
#include <stdlib.h>

struct move_admission_fixture {
    struct netclient client, before;
};

void *moveFixtureCreate(void)
{
    return calloc(1, sizeof(struct move_admission_fixture));
}

void moveFixtureDestroy(void *fixture)
{
    free(fixture);
}

void moveFixtureCommit(void *fixture, uint32_t tick, uint32_t ack,
        float position, uint32_t command, uint32_t checkAckTick,
        move_admission_result *result)
{
    struct move_admission_fixture *f = fixture;
    struct netplayermove move = {0};
    move.tick = tick;
    move.pos.x = position;
    move.ucmd = command;
    /* Model interpolation elapsed since the preceding packet. */
    f->client.lerpticks = 17;
    memcpy(&f->before, &f->client, sizeof(f->client));
    memset(result, 0, sizeof(*result));
    result->accepted = netPlayerMoveCommit(&f->client, &move, ack);
    result->unchanged = memcmp(&f->before, &f->client, sizeof(f->client)) == 0;
    result->tick = f->client.inmove[0].tick;
    result->previousTick = f->client.inmove[1].tick;
    result->position = f->client.inmove[0].pos.x;
    result->command = f->client.inmove[0].ucmd;
    result->ack = f->client.outmoveack;
    result->ackValid = f->client.outmoveack_valid;
    result->lerp = f->client.lerpticks;
    result->acknowledged = netPlayerMoveAcknowledged(&f->client, checkAckTick);
}

int moveFixtureRead(const void *wire, uint32_t size, move_fixture_result *result)
{
    struct netplayermove value, before;
    struct netbuf buf;
    memset(&value, 0x5a, sizeof(value));
    memcpy(&before, &value, sizeof(value));
    netbufStartReadData(&buf, wire, size);
    const u32 error = netbufReadPlayerMove(&buf, &value);
    memset(result, 0, sizeof(*result));
    result->unchanged = memcmp(&before, &value, sizeof(value)) == 0;
    if (error) return (int)error;
    result->tick = value.tick;
    result->ucmd = value.ucmd;
    const float fields[] = {value.leanofs, value.crouchofs,
        value.movespeed[0], value.movespeed[1], value.angles[0], value.angles[1],
        value.crosspos[0], value.crosspos[1], value.pos.x, value.pos.y, value.pos.z,
        value.zoomfov};
    memcpy(result->fields, fields, sizeof(fields));
    result->weaponnum = value.weaponnum;
    result->emptyIdentity = 1;
    for (size_t i = 0; i < sizeof(value.weapon_id); ++i)
        if (value.weapon_id[i] != 0) result->emptyIdentity = 0;
    return 0;
}

uint32_t moveFixtureWrite(void *wire, uint32_t size, uint32_t tick,
        uint32_t ucmd, const float *f, int weapon)
{
    struct netplayermove value = {0};
    struct netbuf buf = {0};
    buf.data = wire;
    buf.size = size;
    value.tick = tick; value.ucmd = ucmd;
    value.leanofs = f[0]; value.crouchofs = f[1];
    value.movespeed[0] = f[2]; value.movespeed[1] = f[3];
    value.angles[0] = f[4]; value.angles[1] = f[5];
    value.crosspos[0] = f[6]; value.crosspos[1] = f[7];
    value.pos.x = f[8]; value.pos.y = f[9]; value.pos.z = f[10];
    value.zoomfov = f[11]; value.weaponnum = (s8)weapon;
    return netbufWritePlayerMove(&buf, &value) ? 0 : buf.wp;
}
