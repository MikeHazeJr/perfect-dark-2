#include "net/net.h"
#include "net/netbuf.h"
#include "net/net_player_move_wire.h"
#include "net_move_fixture.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include "bss.h"
#include "game/chraction.h"
#include "game/inv.h"
#include "game/playermgr.h"
#include "net/netmsg.h"
#include "system.h"

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

void moveFixtureReset(void *fixture)
{
    struct move_admission_fixture *f = fixture;
    netPlayerMoveReset(&f->client);
}

void moveFixtureConsume(void *fixture, int maxTicks, move_consumption_result *result)
{
    struct move_admission_fixture *f = fixture;
    struct netplayermove before[2];
    memcpy(before, f->client.inmove, sizeof(before));
    memset(result, 0, sizeof(*result));
    result->pendingBefore = netPlayerMovePending(&f->client);
    result->receiptPendingBefore = f->client.inmove_pending;
    result->commandBefore = netPlayerMoveForwardCommands(&f->client);
    result->interval = netPlayerMoveInterval(&f->client, maxTicks);
    result->consumed = netPlayerMoveConsume(&f->client);
    result->pendingAfter = netPlayerMovePending(&f->client);
    result->receiptPendingAfter = f->client.inmove_pending;
    result->commandAfter = netPlayerMoveForwardCommands(&f->client);
    result->appliedTick = f->client.inmovetick;
    result->appliedValid = f->client.inmovetick_valid;
    result->previousValid = f->client.inmoveprev_valid;
    result->receivedValid = f->client.inmove_valid;
    result->recordUnchanged = memcmp(before, f->client.inmove, sizeof(before)) == 0;
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

/* Only this fixture owns these game dependencies in pd-tests. Network globals
 * are defined by the lobby fixture, so save/restore them across each lifetime. */
struct g_vars g_Vars;
struct move_ingress_fixture {
    struct netclient savedClients[NET_MAX_CLIENTS + 1], sender, before;
    struct netclient *savedLocal;
    struct g_vars savedVars;
    struct player player, beforePlayer;
    struct prop prop, beforeProp;
    struct chrdata chr, beforeChr;
    int single, dual;
    move_ingress_result observed;
};
static struct move_ingress_fixture *s_Ingress;

void *moveIngressCreate(int local)
{
    if (s_Ingress) return NULL;
    struct move_ingress_fixture *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    memcpy(f->savedClients, g_NetClients, sizeof(g_NetClients));
    f->savedLocal = g_NetLocalClient;
    f->savedVars = g_Vars;
    memset(g_NetClients, 0, sizeof(g_NetClients));
    struct netclient *cl = &g_NetClients[1];
    cl->id = 1;
    cl->state = f->sender.state = CLSTATE_GAME;
    cl->playernum = 1;
    cl->player = &f->player;
    f->player.prop = &f->prop;
    f->prop.chr = &f->chr;
    f->prop.pos.x = 700.f;
    f->player.vv_theta = 90.f;
    f->player.vv_verta = 45.f;
    g_NetLocalClient = local ? cl : &g_NetClients[0];
    g_Vars.currentplayernum = 3;
    f->single = f->dual = 1;
    s_Ingress = f;
    return f;
}

void moveIngressDestroy(void *fixture)
{
    struct move_ingress_fixture *f = fixture;
    if (!f) return;
    memcpy(g_NetClients, f->savedClients, sizeof(g_NetClients));
    g_NetLocalClient = f->savedLocal;
    g_Vars = f->savedVars;
    s_Ingress = NULL;
    free(f);
}

void moveIngressForce(void *fixture, uint32_t tick, uint32_t command)
{
    struct move_ingress_fixture *f = fixture;
    g_NetClients[1].forcetick = tick;
    f->player.ucmd = command;
}

void moveIngressInventory(void *fixture, int single, int dual)
{
    struct move_ingress_fixture *f = fixture;
    f->single = single;
    f->dual = dual;
}

void moveIngressReady(void *fixture, int sender, int target)
{
    struct move_ingress_fixture *f = fixture;
    f->sender.state = sender ? CLSTATE_GAME : CLSTATE_GAME - 1;
    g_NetClients[1].state = target ? CLSTATE_GAME : CLSTATE_GAME - 1;
}

void moveIngressWitness(void *fixture)
{
    (void)fixture;
    g_NetClients[1].reconnect_gameplay_witness_pending = true;
}

void moveFixtureLog(s32 level, const char *fmt, ...)
{
    if (!s_Ingress) return;
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    ++s_Ingress->observed.logs;
    s_Ingress->observed.warnings += level == LOG_WARNING;
    s_Ingress->observed.forceLogs += strstr(message, "successfully forcemoved") != NULL;
    s_Ingress->observed.reconnectLogs += strstr(message, "NET.RECONNECT.GAMEPLAY") != NULL;
}

void setCurrentPlayerNum(s32 playernum)
{
    g_Vars.currentplayernum = playernum;
    ++s_Ingress->observed.playerChanges;
}

bool invHasSingleWeaponIncAllGuns(s32 weaponnum)
{
    (void)weaponnum;
    ++s_Ingress->observed.singleChecks;
    s_Ingress->observed.inventoryPlayer = g_Vars.currentplayernum;
    return s_Ingress->single;
}

bool invHasDoubleWeaponIncAllGuns(s32 weapon1, s32 weapon2)
{
    (void)weapon1; (void)weapon2;
    ++s_Ingress->observed.doubleChecks;
    s_Ingress->observed.inventoryPlayer = g_Vars.currentplayernum;
    return s_Ingress->dual;
}

bool chrSetPos(struct chrdata *chr, struct coord *pos, RoomNum *rooms,
        f32 angle, bool findground)
{
    (void)chr;
    ++s_Ingress->observed.teleports;
    s_Ingress->observed.teleportPosition = pos->x;
    s_Ingress->observed.teleportAngle = angle;
    s_Ingress->observed.ground = findground;
    s_Ingress->observed.room = rooms[0];
    return true;
}

int moveIngressRead(void *fixture, int snapshot, const void *wire, uint32_t size,
        move_ingress_result *result)
{
    struct move_ingress_fixture *f = fixture;
    struct netclient *cl = &g_NetClients[1];
    struct netbuf buf;
    cl->lerpticks = 17;
    memcpy(&f->before, cl, sizeof(*cl));
    memcpy(&f->beforePlayer, &f->player, sizeof(f->player));
    memcpy(&f->beforeProp, &f->prop, sizeof(f->prop));
    memcpy(&f->beforeChr, &f->chr, sizeof(f->chr));
    memset(&f->observed, 0, sizeof(f->observed));
    netbufStartReadData(&buf, wire, size);
    const u32 error = snapshot ? netmsgSvcPlayerMoveRead(&buf, &f->sender)
        : netmsgClcMoveRead(&buf, cl);
    *result = f->observed;
    result->unchanged = memcmp(cl, &f->before, sizeof(*cl)) == 0
        && memcmp(&f->player, &f->beforePlayer, sizeof(f->player)) == 0
        && memcmp(&f->prop, &f->beforeProp, sizeof(f->prop)) == 0
        && memcmp(&f->chr, &f->beforeChr, sizeof(f->chr)) == 0;
    result->tick = cl->inmove[0].tick;
    result->previousTick = cl->inmove[1].tick;
    result->ack = cl->outmoveack;
    result->ackValid = cl->outmoveack_valid;
    result->lerp = cl->lerpticks;
    result->position = cl->inmove[0].pos.x;
    result->angle = cl->inmove[0].angles[0];
    result->command = cl->inmove[0].ucmd;
    result->weapon = cl->inmove[0].weaponnum;
    result->forceTick = cl->forcetick;
    result->playerCommand = f->player.ucmd;
    result->witnessPending = cl->reconnect_gameplay_witness_pending;
    result->currentPlayer = g_Vars.currentplayernum;
    return (int)error;
}
