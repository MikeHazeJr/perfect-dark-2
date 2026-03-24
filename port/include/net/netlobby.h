/**
 * netlobby.h -- Network lobby state management.
 *
 * Tracks lobby state: who's connected, who's the leader, game settings,
 * and player ready states. Used by both the dedicated server overlay
 * and the client-side lobby UI.
 */

#ifndef _IN_NETLOBBY_H
#define _IN_NETLOBBY_H

#include <PR/ultratypes.h>

#define LOBBY_MAX_PLAYERS 8
#define LOBBY_NAME_LEN    32  /* PC port: longer names, no N64 constraint */

/* Lobby player slot */
struct lobbyplayer {
    u8 active;              /* 1 if slot in use */
    u8 clientId;            /* netclient index */
    u8 isLeader;            /* 1 if this player is the lobby leader */
    u8 isReady;             /* 1 if player has readied up */
    u8 headnum;             /* character head */
    u8 bodynum;             /* character body */
    u8 team;                /* team assignment */
    char name[LOBBY_NAME_LEN];
};

/* Lobby game settings (controlled by leader) */
struct lobbysettings {
    u8 scenario;            /* game mode: combat, ctc, htb, etc. */
    u8 stagenum;            /* arena/map index */
    u8 numSimulants;        /* number of AI bots */
    u8 teamEnabled;         /* teams on/off */
    f32 jumpHeight;         /* custom jump height (0 = default) */
    /* Expand as needed */
};

/* Lobby state */
struct lobbystate {
    struct lobbyplayer players[LOBBY_MAX_PLAYERS];
    struct lobbysettings settings;
    u8 leaderSlot;          /* which player slot is the leader */
    u8 numPlayers;          /* active player count */
    u8 inGame;              /* 1 if match is running */
};

extern struct lobbystate g_Lobby;

/* Initialize lobby state (call on server start or session join) */
void lobbyInit(void);

/* Update lobby from network client data (call each frame) */
void lobbyUpdate(void);

/* Set a player as the lobby leader */
void lobbySetLeader(u8 slot);

/* Get the leader's slot index */
u8 lobbyGetLeader(void);

/* Check if the local player is the leader */
s32 lobbyIsLocalLeader(void);

#endif /* _IN_NETLOBBY_H */
