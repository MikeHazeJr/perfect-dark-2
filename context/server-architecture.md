# Server Architecture — Decoupled from N64 Game Code

## Vision

The dedicated server and game client communicate through a **protocol interface** — a clean boundary of callbacks and data structures. Neither side directly accesses the other's internals.

```
┌──────────────────┐     Protocol      ┌──────────────────┐
│                  │    Interface       │                  │
│  Dedicated       │◄────────────────► │  Game Client     │
│  Server          │  (net_interface.h) │  (N64 port)      │
│                  │                    │                  │
│  - ENet          │                    │  - Full game     │
│  - Lobby mgmt    │                    │  - Rendering     │
│  - Match state   │                    │  - Audio         │
│  - Server GUI    │                    │  - Menus         │
│  - Logging       │                    │  - Input         │
│                  │                    │                  │
└──────────────────┘                    └──────────────────┘
```

## Protocol Interface (net_interface.h)

The interface defines callbacks that the networking layer invokes. The server and client each provide their own implementations.

### Callbacks (implemented by server OR client):

```c
/* Called when a player connects and is authenticated */
void netcb_OnPlayerJoin(u8 clientId, const char *name, u8 headnum, u8 bodynum);

/* Called when a player disconnects */
void netcb_OnPlayerLeave(u8 clientId, const char *name, u32 reason);

/* Called when a player's settings change (character, team, name) */
void netcb_OnPlayerSettingsChanged(u8 clientId);

/* Called when the host requests match start */
void netcb_OnMatchStart(u8 stagenum, u8 scenario, u32 rngSeed);

/* Called when the match ends */
void netcb_OnMatchEnd(void);

/* Called when a player dies (server-authoritative) */
void netcb_OnPlayerDeath(u8 victimId, u8 killerId, u8 weaponId);

/* Called when a player respawns */
void netcb_OnPlayerRespawn(u8 clientId);

/* Called to get match state for a joining client (resync) */
void netcb_GetMatchState(struct netbuf *buf);

/* Called to apply match state from server (client-side resync) */
void netcb_ApplyMatchState(struct netbuf *buf);
```

### Data Structures (shared):

```c
/* Player identity — what the protocol needs to know about a player */
struct net_player {
    u8 id;
    char name[16];
    u8 headnum;
    u8 bodynum;
    u8 team;
    u8 state;       /* LOBBY, GAME, ABSENT */
};

/* Match configuration — what the host sends to start a game */
struct net_match_config {
    u8 stagenum;
    u8 scenario;
    u8 num_simulants;
    u8 team_enabled;
    u32 rng_seed;
    u8 weapon_set;
};
```

## Server Implementation

The server implements `netcb_*` callbacks with **no game logic** — just state tracking and logging:

- `netcb_OnPlayerJoin` → add to player list, log, update GUI
- `netcb_OnPlayerLeave` → remove from list, log, handle leader transfer
- `netcb_OnMatchStart` → broadcast to all clients, update state
- `netcb_OnMatchEnd` → return all to lobby state
- `netcb_OnPlayerDeath` → update scoreboard, broadcast

The server does NOT:
- Run game physics
- Simulate AI
- Render anything (except its own GUI)
- Process player movement
- Load models or textures

## Client Implementation

The client implements `netcb_*` callbacks by calling into the N64 game code:

- `netcb_OnMatchStart` → `mainChangeToStage()`, `mpStartMatch()`
- `netcb_OnPlayerDeath` → `playerDie()`, HUD notification
- `netcb_OnPlayerRespawn` → `playerStartNewLife()`
- `netcb_OnMatchEnd` → endscreen, return to lobby

## Migration Path

1. **Phase 1 (now):** Create the interface. Server uses stubs. Client uses existing net.c code.
2. **Phase 2:** Gradually move net.c's game function calls behind the callback interface.
3. **Phase 3:** Server and client each have their own callback implementations.
4. **Phase 4:** Remove all direct game state access from networking code.

## SPF-1: Server Platform Foundation (S47d — DONE)

Thin platform layer added on top of the existing ENet server.  Protocol v21 unchanged.

### Hub + Room system

```
Server process
└── Hub (hub.h/c)           — singleton, LOUNGE / ACTIVE state
    ├── Room 0 "Lounge"     — wraps existing single-match lifecycle
    │   state: LOBBY → LOADING → MATCH → POSTGAME → LOBBY (loop)
    ├── Room 1..3            — future rooms (pool pre-allocated, all CLOSED)
    └── Identity (identity.h/c) — pd-identity.dat, device UUID + profiles
```

`hubTick()` (called each frame after `lobbyUpdate()`) syncs room 0 state from
`g_Lobby.inGame`.  No changes to net.c or netlobby.c — backward compatible.

### Phonetic encoding (phonetic.h/c)

8 CV syllables (16 consonants × 4 vowels = 6 bits each) encode IP:port as
"BALE-GIFE-NOME-RIVA" — shorter than the word-based connect codes.
Both encodings remain available.

### Server GUI changes (server_gui.cpp)

Middle panel converted to tab bar:
- **Server** tab: existing player list + match controls (unchanged)
- **Hub** tab: hub state (Lounge/Active) + room table with color-coded states
- Log panel: HUB: prefix highlighted purple

### New files

| File | Purpose |
|------|---------|
| `port/include/hub.h` / `port/src/hub.c` | Hub lifecycle |
| `port/include/room.h` / `port/src/room.c` | Room struct + 5-state lifecycle |
| `port/include/identity.h` / `port/src/identity.c` | pd-identity.dat |
| `port/include/phonetic.h` / `port/src/phonetic.c` | CV syllable IP encoding |

---

## Files

- `port/include/net/net_interface.h` — Protocol interface definition
- `port/src/net/net_core.c` — Core networking (ENet, message encode/decode) — shared
- `port/src/net/net_server.c` — Server-side callback implementations
- `port/src/net/net_client.c` — Client-side callback implementations (wraps N64 code)
- `port/src/server_main.c` — Server entry point
- `port/fast3d/server_gui.cpp` — Server GUI (ImGui)
