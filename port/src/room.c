/**
 * room.c -- Hub room lifecycle management.
 *
 * Room 0 is created by roomsInit() and represents the server's primary
 * (and currently only) match slot.  Its state is kept in sync with the
 * existing lobby / net layer by hubTick() in hub.c.
 *
 * All rooms share a fixed pool of HUB_MAX_ROOMS slots.  A slot is in use
 * when state != ROOM_STATE_CLOSED.  Room 0 resets to LOBBY on roomDestroy
 * rather than closing, preserving the primary slot permanently.
 *
 * ---------------------------------------------------------------------------
 * ADR note (P6-B, Tier 6): Room 0 and g_Lobby.inGame
 * ---------------------------------------------------------------------------
 * roomsInit() always opens slot 0 as ROOM_STATE_LOBBY ("Lounge"). hubTick()
 * (hub.c) syncs that room's lifecycle with g_Lobby.inGame for backward
 * compatibility with the pre-multi-room stack. Extra rooms use findFreeSlot()
 * and start CLOSED; room 0 is never destroyed, only reset — see roomDestroy.
 * ---------------------------------------------------------------------------
 */

#include "room.h"
#include "system.h"
#include "sha256.h"

/* Bug B forward decls — avoid pulling all of netmsg.h into room.c. */
extern void netReadyGateOnClientLeft(u8 clientId);
extern void netReadyGateAbortForRoom(u8 room_id, const char *reason);

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* SEC-14: Hash a room password into the 32-byte slot.  Mirrors the admin
 * token pattern — domain-separated SHA-256 so the hash can't be confused
 * with an unrelated SHA-256 oracle. */
static void roomHashPassword(const char *plaintext, u8 out[ROOM_PASSWORD_HASH_LEN])
{
    sha256_ctx ctx;
    sha256Init(&ctx);
    static const char kSalt[] = "pd2-room-password-v1\n";
    sha256Update(&ctx, kSalt, sizeof(kSalt) - 1);
    sha256Update(&ctx, plaintext, strlen(plaintext));
    sha256Final(&ctx, out);
}

/* -------------------------------------------------------------------------
 * Module state
 * ------------------------------------------------------------------------- */

static hub_room_t s_Rooms[HUB_MAX_ROOMS];
static int        s_Initialised = 0;

/* External: current network tick (from net.h / net.c) */
extern u32 g_NetTick;

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static hub_room_t *findFreeSlot(void)
{
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        if (s_Rooms[i].state == ROOM_STATE_CLOSED) return &s_Rooms[i];
    }
    return NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void roomsInit(void)
{
    if (s_Initialised) return;

    memset(s_Rooms, 0, sizeof(s_Rooms));

    /* All slots start closed. */
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        s_Rooms[i].id    = (u8)i;
        s_Rooms[i].state = ROOM_STATE_CLOSED;
    }

    /* Room 0 always exists as the primary match room. */
    s_Rooms[0].state            = ROOM_STATE_LOBBY;
    s_Rooms[0].created_tick     = 0;
    s_Rooms[0].state_enter_tick = 0;
    strncpy(s_Rooms[0].name, "Lounge", ROOM_NAME_MAX - 1);

    s_Initialised = 1;
    sysLogPrintf(LOG_NOTE, "HUB ROOM: subsystem initialised, room 0 open");
}

hub_room_t *roomCreate(const char *name)
{
    hub_room_t *r = findFreeSlot();
    if (!r) {
        sysLogPrintf(LOG_WARNING, "HUB ROOM: no free slots (max %d)", HUB_MAX_ROOMS);
        return NULL;
    }

    memset(r->clients, 0, sizeof(r->clients));
    r->client_count      = 0;
    r->stagenum          = 0;
    r->scenario          = 0;
    r->rng_seed          = 0;
    r->state             = ROOM_STATE_LOBBY;
    r->created_tick      = g_NetTick;
    r->state_enter_tick  = g_NetTick;

    strncpy(r->name, name ? name : "Room", ROOM_NAME_MAX - 1);
    r->name[ROOM_NAME_MAX - 1] = '\0';

    sysLogPrintf(LOG_NOTE, "HUB ROOM: created room %u \"%s\"", (unsigned)r->id, r->name);
    return r;
}

hub_room_t *roomCreateConfigured(const char *name, u8 maxPlayers,
                                  room_access_t access, const char *password,
                                  u8 creatorClientId)
{
    hub_room_t *r = roomCreate(name);
    if (!r) return NULL;

    /* Clamp max_players to the hub-wide ceiling.  0 is treated as "use default". */
    if (maxPlayers == 0) maxPlayers = HUB_MAX_CLIENTS;
    if (maxPlayers > HUB_MAX_CLIENTS) maxPlayers = HUB_MAX_CLIENTS;
    r->max_players       = maxPlayers;
    r->access            = access;
    r->creator_client_id = creatorClientId;

    /* SEC-14: hash the plaintext password; never store it. */
    memset(r->password_hash, 0, sizeof(r->password_hash));
    if (password && password[0] && access == ROOM_ACCESS_PASSWORD) {
        roomHashPassword(password, r->password_hash);
    }

    /* Auto-join the creator */
    roomJoin(r, creatorClientId);

    sysLogPrintf(LOG_NOTE, "HUB ROOM: room %u configured by client %u (access=%u max=%u)",
                 (unsigned)r->id, (unsigned)creatorClientId,
                 (unsigned)r->access, (unsigned)r->max_players);
    return r;
}

s32 roomCheckPassword(const hub_room_t *room, const char *plaintext)
{
    if (!room) return 0;
    if (room->access != ROOM_ACCESS_PASSWORD) return 1; /* no password required */

    /* All-zero stored hash means "password room but no password set" —
     * defensive: reject the join so a mis-created room can't be trivially entered. */
    int allZero = 1;
    for (s32 i = 0; i < ROOM_PASSWORD_HASH_LEN; i++) {
        if (room->password_hash[i]) { allZero = 0; break; }
    }
    if (allZero) return 0;

    if (!plaintext || !plaintext[0]) return 0;

    u8 supplied[ROOM_PASSWORD_HASH_LEN];
    roomHashPassword(plaintext, supplied);

    u8 diff = 0;
    for (s32 i = 0; i < ROOM_PASSWORD_HASH_LEN; i++) {
        diff |= (u8)(supplied[i] ^ room->password_hash[i]);
    }
    return (diff == 0) ? 1 : 0;
}

s32 roomJoin(hub_room_t *room, u8 clientId)
{
    if (!room || room->state == ROOM_STATE_CLOSED) return 0;

    /* Check if already in room */
    for (u8 i = 0; i < room->client_count; i++) {
        if (room->clients[i] == clientId) return 0;
    }

    /* SEC-14: room max_players is now authoritative (was previously a display-only
     * field).  Hard cap at HUB_MAX_CLIENTS regardless. */
    const u8 cap = (room->max_players && room->max_players <= HUB_MAX_CLIENTS)
                    ? room->max_players : HUB_MAX_CLIENTS;
    if (room->client_count >= cap) return 0;

    room->clients[room->client_count++] = clientId;
    sysLogPrintf(LOG_NOTE, "HUB ROOM: client %u joined room %u \"%s\" (%u/%u)",
                 (unsigned)clientId, (unsigned)room->id, room->name,
                 (unsigned)room->client_count, (unsigned)room->max_players);
    return 1;
}

void roomLeave(hub_room_t *room, u8 clientId)
{
    if (!room) return;

    s32 found = -1;
    for (u8 i = 0; i < room->client_count; i++) {
        if (room->clients[i] == clientId) {
            found = i;
            break;
        }
    }

    if (found < 0) return;

    /* Shift remaining clients down */
    for (u8 i = found; i < room->client_count - 1; i++) {
        room->clients[i] = room->clients[i + 1];
    }
    room->client_count--;

    sysLogPrintf(LOG_NOTE, "HUB ROOM: client %u left room %u \"%s\" (%u remaining)",
                 (unsigned)clientId, (unsigned)room->id, room->name,
                 (unsigned)room->client_count);

    /* Bug B: if this client was participating in the pre-match ready
     * gate, abort the countdown.  Safe no-op if gate is inactive or the
     * client wasn't preparing.  Runs regardless of whether the room will
     * survive this leave. */
    netReadyGateOnClientLeft(clientId);

    /* Destroy empty rooms (except room 0) */
    if (room->client_count == 0 && room->id != 0) {
        /* Bug B defensive: abort any gate still targeting this room before
         * destroying the slot.  Should already be clear from the
         * netReadyGateOnClientLeft above, but covers edge cases (e.g. the
         * leaver was a late-join spectator not in expected_mask). */
        netReadyGateAbortForRoom(room->id, "Room closed");
        roomDestroy(room);
    }
}

void roomDestroy(hub_room_t *room)
{
    if (!room) return;

    if (room->id == 0) {
        /* Room 0 is permanent — reset to lobby instead of closing. */
        roomTransition(room, ROOM_STATE_LOBBY);
        room->client_count = 0;
        sysLogPrintf(LOG_NOTE, "HUB ROOM: room 0 reset to lobby");
        return;
    }

    sysLogPrintf(LOG_NOTE, "HUB ROOM: closing room %u \"%s\"",
                 (unsigned)room->id, room->name);
    room->state        = ROOM_STATE_CLOSED;
    room->client_count = 0;
}

void roomTransition(hub_room_t *room, room_state_t state)
{
    if (!room) return;
    if (room->state == state) return;

    sysLogPrintf(LOG_NOTE, "HUB ROOM: room %u \"%s\" %s -> %s",
                 (unsigned)room->id, room->name,
                 roomStateName(room->state),
                 roomStateName(state));

    room->state            = state;
    room->state_enter_tick = g_NetTick;
}

hub_room_t *roomGetById(u8 id)
{
    if (id >= HUB_MAX_ROOMS) return NULL;
    if (s_Rooms[id].state == ROOM_STATE_CLOSED) return NULL;
    return &s_Rooms[id];
}

hub_room_t *roomGetByIndex(s32 idx)
{
    if (!s_Initialised) return NULL;
    s32 count = 0;
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        if (s_Rooms[i].state != ROOM_STATE_CLOSED) {
            if (count == idx) return &s_Rooms[i];
            count++;
        }
    }
    return NULL;
}

s32 roomGetActiveCount(void)
{
    if (!s_Initialised) return 0;
    s32 n = 0;
    for (int i = 0; i < HUB_MAX_ROOMS; i++) {
        if (s_Rooms[i].state != ROOM_STATE_CLOSED) n++;
    }
    return n;
}

/* roomStateName() moved to room.h as static inline for client/server shared use */

/* -------------------------------------------------------------------------
 * Room name generator -- adjective + noun combo
 * Short, fun, memorable names for rooms.
 * ------------------------------------------------------------------------- */

static const char *s_RoomAdj[] = {
    "Fat","Tiny","Big","Red","Blue","Green","Purple","Golden",
    "Sneaky","Lazy","Wild","Brave","Jolly","Angry","Happy","Shy",
    "Fast","Slow","Loud","Quiet","Fuzzy","Shiny","Rusty","Dusty",
    "Cosmic","Magic","Frozen","Burning","Ancient","Modern","Secret","Dark",
    "Jumpy","Bouncy","Salty","Spicy","Smooth","Rough","Electric","Mystic",
    "Funky","Crispy","Wobbly","Dizzy","Grumpy","Silly","Fancy","Slippery",
    "Sparkly","Fluffy","Crunchy","Turbo","Mega","Ultra","Hyper","Super",
    "Neon","Retro","Toxic","Atomic","Radical","Extreme","Epic","Legendary",
};

static const char *s_RoomNoun[] = {
    "Monkey","Penguin","Tiger","Dragon","Robot","Wizard","Pirate","Ninja",
    "Cheese","Pizza","Taco","Waffle","Donut","Pickle","Muffin","Cookie",
    "Falcon","Eagle","Cobra","Phoenix","Parrot","Lobster","Walrus","Panda",
    "Castle","Rocket","Hammer","Crystal","Tornado","Volcano","Comet","Glacier",
    "Dinosaur","Viking","Cowboy","Astronaut","Clown","Chef","Ghost","Zombie",
    "Banana","Mushroom","Cactus","Pumpkin","Potato","Onion","Pretzel","Biscuit",
    "Van","Bus","Tank","Submarine","Blimp","Sled","Canoe","Skateboard",
    "Hat","Boot","Sock","Mitten","Crown","Trophy","Medal","Badge",
};

#define ROOM_ADJ_COUNT  (sizeof(s_RoomAdj) / sizeof(s_RoomAdj[0]))
#define ROOM_NOUN_COUNT (sizeof(s_RoomNoun) / sizeof(s_RoomNoun[0]))

void roomGenerateName(char *buf, s32 bufsize)
{
    /* Use stdlib rand -- rngRandom is game code, not available in server build */
    u32 r = (u32)rand();
    s32 ai = r % ROOM_ADJ_COUNT;
    s32 ni = (r >> 8) % ROOM_NOUN_COUNT;
    snprintf(buf, bufsize, "%s %s", s_RoomAdj[ai], s_RoomNoun[ni]);
}
