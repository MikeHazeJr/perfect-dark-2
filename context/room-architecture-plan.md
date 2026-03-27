# Room Architecture Plan

> Created: S50 (2026-03-26) — initial draft
> Revised: S51 (2026-03-26) — code audit, struct corrections, message IDs, phase file refs
> Status: APPROVED — ready for implementation
>
> Cross-references:
>   [multiplayer-plan.md](multiplayer-plan.md) §2 (room types, lifecycle, access modes)
>   [join-flow-plan.md](join-flow-plan.md) §3.2 (room display gap), §6 J-3 (SVC_ROOM_LIST)
>
> This plan implements the room architecture described in multiplayer-plan.md §2.
> Do NOT duplicate the design rationale here — reference multiplayer-plan.md.

---

## 1. Design Principles

- **Server is not a player.** A dedicated server manages rooms but must not occupy a player slot.
  See §7 for the `g_NetLocalClient` fix (B-28).
- **No raw IPs anywhere.** Connect codes are the only identifier shown to users — UI, server GUI,
  and all log output. See §8 for specific scrub locations (B-29, B-30).
- **Rooms are demand-driven.** Zero players = zero rooms. Room 0 is no longer permanent.
- **Room leader has authority.** Only the room leader configures settings and starts a match.
- **Server operator has override authority.** Server GUI allows reassign/move/kick/close.

---

## 2. Room Lifecycle

```
ROOM_STATE_LOBBY → ROOM_STATE_LOADING → ROOM_STATE_INGAME → ROOM_STATE_POSTGAME → ROOM_STATE_LOBBY (loop)
                                                           → ROOM_STATE_CLOSED (disband)
```

States already defined in `port/include/room.h` — enum `room_state_t`.
Note: current code uses MATCH, not INGAME. Enum values (verified in room.h):
  - `ROOM_STATE_LOBBY    = 0`
  - `ROOM_STATE_LOADING  = 1`
  - `ROOM_STATE_MATCH    = 2`  (plan calls this INGAME — same thing)
  - `ROOM_STATE_POSTGAME = 3`
  - `ROOM_STATE_CLOSED   = 4`

### On Player Connect (after auth)
1. Create a new room → assign client as leader (`leader_client_id = cl->id`)
2. Set `cl->room_id` on the `netclient` struct
3. Send `SVC_ROOM_ASSIGN` to this client
4. Broadcast `SVC_ROOM_LIST` to all lobby clients

### On CLC_ROOM_JOIN
1. Validate room exists, is accessible, has capacity
2. Remove player from origin room; destroy if now empty
3. Assign player to new room; send `SVC_ROOM_ASSIGN`
4. Broadcast `SVC_ROOM_UPDATE` for both rooms

### On CLC_ROOM_LEAVE
1. Remove player from room; leadership auto-transfer if needed
2. Destroy room if empty; otherwise send `SVC_ROOM_UPDATE`
3. Create new single-player room for this client
4. Send `SVC_ROOM_ASSIGN`; broadcast `SVC_ROOM_LIST`

### On Client Disconnect
1. Remove from room; transfer leadership if needed; destroy if empty
2. Free `netclient` slot; broadcast `SVC_ROOM_UPDATE`

### On CLC_ROOM_START (leader only)
1. Validate sender is leader, game mode and stage set
2. `ROOM_STATE_LOBBY → ROOM_STATE_LOADING`
3. Broadcast `SVC_STAGE_START` to room members only (not whole server)
4. `ROOM_STATE_LOADING → ROOM_STATE_MATCH` when all clients in room reach CLSTATE_GAME

---

## 3. Data Structure Changes

### hub_room_t (in `port/include/room.h`)

**Existing struct** (verified in codebase — do not re-declare, only add/change):
```c
typedef struct hub_room_s {
    u8            id;                       /* unique room index (0 to HUB_MAX_ROOMS-1) */
    room_state_t  state;
    char          name[ROOM_NAME_MAX];      /* ROOM_NAME_MAX = 32 */

    u8            clients[HUB_MAX_CLIENTS]; /* clientId of each member */
    u8            client_count;
    u8            max_players;

    u8            stagenum;                 /* selected stage (arena) */
    u8            scenario;                 /* selected game mode (NETGAMEMODE_*) */
    u32           rng_seed;

    room_access_t access;                   /* ROOM_ACCESS_OPEN/PASSWORD/INVITE */
    char          password[32];
    u8            creator_client_id;        /* client who created this room */

    u32           created_tick;
    u32           state_enter_tick;
} hub_room_t;
```

**Fields to add** (Phase R-2):
```c
    u8            leader_client_id;         /* current leader (may differ from creator) */
```

**Constants to update** (Phase R-2):
```c
/* room.h */
#define HUB_MAX_ROOMS   16   /* was 4 — expand for multi-room hub */
#define HUB_MAX_CLIENTS 32   /* was 8 — must match NET_MAX_CLIENTS in net.h */
```

> **Warning**: Changing HUB_MAX_CLIENTS from 8 to 32 changes the size of `hub_room_t`
> (the `clients[]` array). Any code serializing or casting this struct must be reviewed.
> Current known users: `room.c` (internal), `server_gui.cpp` (reads `r->client_count`).

### netclient (in `port/include/net/net.h`)

**Field to add** (Phase R-2):
```c
    s32  room_id;    /* which hub room this client is in; 0 = no room */
```
Add after the `flags` field. Init to 0 in `netClientReset()`.

> `room_id` is `s32` not `u8` so that 0 can mean "not assigned" without ambiguity
> (hub room IDs are 0-based so 0 is a valid room; use -1 for "unassigned" instead,
> or pick a sentinel like NET_NO_ROOM = -1). Suggest `s32 room_id = -1` (no room).

---

## 4. Net Protocol Messages

### Available message ID ranges

| Direction | Last used | Available from |
|-----------|-----------|----------------|
| SVC (server→client) | `0x74` SVC_LOBBY_KILL_FEED (D3R-9) | `0x75` onwards |
| CLC (client→server) | `0x09` CLC_CATALOG_DIFF (D3R-9)  | `0x0A` onwards |

### New SVC messages (server→client)

```c
/* netmsg.h — add after SVC_LOBBY_KILL_FEED */
#define SVC_ROOM_LIST    0x75  /* full room list snapshot — sent on join + on any change */
#define SVC_ROOM_UPDATE  0x76  /* single room delta (player joined/left, state, leader) */
#define SVC_ROOM_ASSIGN  0x77  /* "you are now in room X" — sent after connect/join/leave */
```

### New CLC messages (client→server)

```c
/* netmsg.h — add after CLC_CATALOG_DIFF */
#define CLC_ROOM_JOIN     0x0A  /* request to join a room {room_id, optional password} */
#define CLC_ROOM_LEAVE    0x0B  /* leave current room (server creates a new one for you) */
#define CLC_ROOM_SETTINGS 0x0C  /* leader sets: game mode, stage, access mode, max_players */
#define CLC_ROOM_KICK     0x0D  /* leader kicks a player from the room {target_client_id} */
#define CLC_ROOM_TRANSFER 0x0E  /* leader assigns leadership {new_leader_client_id} */
#define CLC_ROOM_START    0x0F  /* leader starts match for this room (replaces CLC_LOBBY_START) */
```

> `CLC_LOBBY_START (0x08)` remains for backward compat during transition.
> Phase R-4 wires CLC_ROOM_START; CLC_LOBBY_START is deprecated but not removed until tested.

### Wire format (SVC_ROOM_LIST)

```
u8   room_count
for each room:
  u8   room_id
  u8   state          (room_state_t)
  u8   client_count
  u8   max_players
  u8   leader_client_id
  u8   scenario       (NETGAMEMODE_*)
  u8   stagenum
  u8   access         (room_access_t)
  char name[32]
```

### Wire format (SVC_ROOM_ASSIGN)

```
u8   room_id     (the room the client is now in; 0xFF = no room / kicked)
u8   is_leader   (1 if you are the room leader)
```

### Wire format (CLC_ROOM_JOIN)

```
u8   room_id
char password[32]   (empty if not needed)
```

---

## 5. Hub Slot Pool API (hub.h / hub.c)

These functions are **declared in hub.h but NOT implemented in hub.c** (verified S51):
```c
s32 hubGetMaxSlots(void);
void hubSetMaxSlots(s32 maxSlots);
s32 hubGetUsedSlots(void);
s32 hubGetFreeSlots(void);
```

Phase R-1 must add these implementations before any slot-based logic is wired.
Simple implementations using `g_NetMaxClients` and `g_NetNumClients`.

---

## 6. Server-Side Flow (Code-Level)

### Files involved

| File | Role |
|------|------|
| `port/src/hub.c` | Hub init/tick/shutdown, slot pool, future: client-connect hook |
| `port/src/room.c` | Room create/destroy/transition, `clients[]` management |
| `port/include/hub.h` | Hub API |
| `port/include/room.h` | Room struct + API |
| `port/include/net/net.h` | `struct netclient` — add `room_id` field |
| `port/src/net/net.c` | `netStartServer()` — fix `g_NetLocalClient` for dedicated; `netClientReset()` — init room_id |
| `port/src/net/netmsg.c` | Add read/write for all 9 new messages |
| `port/include/net/netmsg.h` | Declare new message IDs + read/write functions |
| `port/src/net/netlobby.c` | Add room-aware leader assignment; `lobbyGetPlayerRoom()` |
| `port/fast3d/server_gui.cpp` | Phase R-5 redesign |
| `port/fast3d/pdgui_menu_lobby.cpp` | Phase R-3: read room list from server, not local state |

### Functions that already exist (no rewrite needed)

| Function | File | Notes |
|----------|------|-------|
| `roomCreate()` | room.c | Use for on-demand creation |
| `roomCreateConfigured()` | room.c | Use for leader-configured creation |
| `roomDestroy()` | room.c | Currently keeps room 0; change behavior in R-2 |
| `roomTransition()` | room.c | Works as-is |
| `roomGetById()` | room.c | Works as-is |
| `roomGetByIndex()` | room.c | Works as-is |
| `roomGetActiveCount()` | room.c | Works as-is |
| `roomGenerateName()` | room.c | Works as-is |
| `hubInit()/hubTick()/hubShutdown()` | hub.c | Need expansion |
| `hubGetState()` | hub.c | Works as-is |
| `lobbyUpdate()` | netlobby.c | Already skips slot 0 for dedicated server |

### Functions to add / change

| Function | File | Change |
|----------|------|--------|
| `hubGetMaxSlots()` etc. | hub.c | Implement 4 stubs declared in hub.h |
| `hubOnClientConnect()` | hub.c | New: create room, assign client as leader |
| `hubOnClientDisconnect()` | hub.c | New: remove from room, transfer/destroy |
| `roomAddClient()` | room.c | New: add clientId to clients[], inc count |
| `roomRemoveClient()` | room.c | New: remove clientId from clients[], dec count |
| `roomGetLeader()` | room.c | New: return leader_client_id |
| `roomTransferLeadership()` | room.c | New: pick next client if leader leaves |
| `roomDestroy()` | room.c | Change: remove special case for room 0 |
| `roomsInit()` | room.c | Change: don't create room 0 at startup |
| `netClientReset()` | net.c | Change: init `room_id = -1` |
| `netStartServer()` | net.c | Change: `g_NetLocalClient = NULL` when `g_NetDedicated` |
| All 9 new messages | netmsg.c + netmsg.h | New read/write functions |

---

## 7. Dedicated Server: No Player Slot (B-28)

### Root cause (verified in `port/src/net/net.c:519-521`)
```c
// netStartServer() — line 519
g_NetLocalClient = &g_NetClients[0];   // ← server claims slot 0
g_NetLocalClient->state = CLSTATE_LOBBY;
netClientReadConfig(g_NetLocalClient, 0);
```

This means slot 0 is permanently occupied by the server, reducing real player capacity by 1.

### Existing mitigations (already in code)
- `lobbyUpdate()` skips `i == 0` when `g_NetDedicated && g_NetMode == NETMODE_SERVER`
- `server_gui.cpp:707` displays `g_NetNumClients - 1` to compensate

### Fix (Phase R-1)
In `netStartServer()`, add a dedicated server guard:
```c
if (g_NetDedicated) {
    g_NetLocalClient = NULL;  /* no local player on dedicated server */
} else {
    g_NetLocalClient = &g_NetClients[0];
    g_NetLocalClient->state = CLSTATE_LOBBY;
    netClientReadConfig(g_NetLocalClient, 0);
}
```

All sites accessing `g_NetLocalClient` must have NULL guards. Most were added for B-27 (S50).
Run a grep for `g_NetLocalClient->` in net.c and netmsg.c to find any remaining unguarded sites.

---

## 8. IP Scrubbing (B-29, B-30)

### B-29: Raw IP in server_gui.cpp status bar

**Location**: `port/fast3d/server_gui.cpp:695`
```cpp
ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s:%u", ip, g_NetServerPort);
```
The connect code is already shown on line 690-691. Remove or replace the gray IP line with just the port:
```cpp
ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Port %u", g_NetServerPort);
```

### B-30: Raw IPs in server log output

**Root cause**: `netFormatClientAddr()` in `net.c:175-178` returns `"IP:port"` strings.
These are used in `sysLogPrintf` calls throughout `net.c` (connection events, auth, etc.).

**Fix approach (Phase R-1)**:
Replace IP-bearing log calls with anonymized identifiers:
- "connection attempt from X.X.X.X:port" → "connection attempt from client N"
- "client X.X.X.X:port disconnected" → "client N (PlayerName) disconnected"
- UPnP lines that expose IP → keep those (they're internal infrastructure, not player-facing)

The connect code should be shown once (on server startup, already done via `sysLogPrintf`).
Individual connection events log the client index, not the IP.

---

## 9. Server GUI Redesign (Phase R-5)

### Current layout (3 panels + log)
- Status bar: title, connect code, players/mode/status
- "Server" tab: player list (with Kick) + Match Control panel
- "Hub" tab: hub state + room table
- Log panel

### Target layout

```
┌─────────────────────────────────────────────────────────────────┐
│ PD2 Dedicated Server v0.0.X                                     │
│ Connect Code: wicked spider sliding under a savanna  [Copy]     │
│ Players: 3/32 | Rooms: 2 | Status: ONLINE                      │
├────────────────────────┬────────────────────────────────────────┤
│ Players          [tab] │ Rooms                           [tab]  │
│ ┌──────────────────┐   │ ┌──────────────────────────────────┐   │
│ │ Agent Name  Room │   │ │ Room 1: Fat Monkey               │   │
│ │ MikeHazeJr  R1 L│   │ │   Leader: MikeHazeJr  State: Lobby│  │
│ │ Player2     R1   │   │ │   Players: 2/8  Mode: Combat Sim │   │
│ │ Player3     R2 L │   │ ├──────────────────────────────────┤   │
│ └──────────────────┘   │ │ Room 2: Tall Frog                │   │
│                        │ │   Leader: Player3  State: Lobby  │   │
│ Selected: Player2      │ │   Players: 1/8  Mode: Combat Sim │   │
│ [Kick] [Move→Room]     │ └──────────────────────────────────┘   │
│                        │ Selected: Room 1                       │
│                        │ [Set Leader] [Close Room]              │
├────────────────────────┴────────────────────────────────────────┤
│ Server Log                                                      │
│ > MikeHazeJr connected (client 1) / Room "Fat Monkey" created  │
│ > Player2 connected (client 2) / joined room "Fat Monkey"      │
└─────────────────────────────────────────────────────────────────┘
```

### New operator actions
- **Kick**: Disconnects player from server (already in GUI)
- **Move to Room**: Dropdown list of rooms, server-moves player
- **Set Leader**: Select player in room, reassign leadership
- **Close Room**: Destroy room, send all players to new individual rooms
- Player and room counts update live from hub/room API

---

## 10. Implementation Phases

### Phase R-1: Foundation (Critical Path)
**Target files**: `net.c`, `hub.c`, `server_gui.cpp`

- [ ] Implement missing hub slot pool functions (`hubGetMaxSlots` etc.) in `hub.c`
- [ ] Fix `g_NetLocalClient = NULL` for dedicated server in `netStartServer()` (B-28)
- [ ] Audit `g_NetLocalClient->` accesses in `net.c`/`netmsg.c` for NULL guards
- [ ] Remove raw IP from server GUI status bar — `server_gui.cpp:695` (B-29)
- [ ] Replace IP-bearing log calls with client index/name (`net.c` connection events) (B-30)

**Breaks nothing** — all changes are guards or removals, no protocol change.

---

### Phase R-2: Room Lifecycle
**Target files**: `room.h`, `room.c`, `hub.h`, `hub.c`, `net.h`, `net.c`

- [ ] Expand constants: `HUB_MAX_ROOMS = 16`, `HUB_MAX_CLIENTS = 32` in `room.h`
- [ ] Add `leader_client_id` field to `hub_room_t` in `room.h`
- [ ] Add `room_id` field (init `-1`) to `struct netclient` in `net.h`; init in `netClientReset()`
- [ ] Change `roomsInit()`: don't create room 0 at startup (remove permanent room 0)
- [ ] Change `roomDestroy(room0)`: remove special case — room 0 closes like any other room
- [ ] Add `roomAddClient(room, clientId)` / `roomRemoveClient(room, clientId)` in `room.c`
- [ ] Add `roomGetLeader(room)` / `roomTransferLeadership(room)` in `room.c`
- [ ] Add `hubOnClientConnect(cl)` in `hub.c`: create room, assign leader, set `cl->room_id`
- [ ] Add `hubOnClientDisconnect(cl)` in `hub.c`: room cleanup, transfer, destroy if empty
- [ ] Wire `hubOnClientConnect/Disconnect` into `net.c` ENet connect/disconnect handlers

**Protocol**: No protocol change yet. Room state managed server-side only.

---

### Phase R-3: Room Sync (Protocol)
**Target files**: `netmsg.h`, `netmsg.c`, `net.c`, `pdgui_menu_lobby.cpp`

- [ ] Add message defines `SVC_ROOM_LIST 0x75`, `SVC_ROOM_UPDATE 0x76`, `SVC_ROOM_ASSIGN 0x77` to `netmsg.h`
- [ ] Add message defines `CLC_ROOM_JOIN 0x0A` through `CLC_ROOM_LEAVE 0x0B` to `netmsg.h`
- [ ] Implement `netmsgSvcRoomListWrite/Read` in `netmsg.c`
- [ ] Implement `netmsgSvcRoomUpdateWrite/Read` in `netmsg.c`
- [ ] Implement `netmsgSvcRoomAssignWrite/Read` in `netmsg.c`
- [ ] Implement `netmsgClcRoomJoinWrite/Read` in `netmsg.c`
- [ ] Implement `netmsgClcRoomLeaveWrite/Read` in `netmsg.c`
- [ ] Server: send `SVC_ROOM_LIST` when client reaches CLSTATE_LOBBY (in `netmsgClcAuthRead`)
- [ ] Server: broadcast `SVC_ROOM_UPDATE` on any room state change
- [ ] Client: handle `SVC_ROOM_LIST` — update local room pool snapshot via `roomUpdateFromServer()`
- [ ] Client: handle `SVC_ROOM_ASSIGN` — update `g_NetLocalClient->room_id`
- [ ] `pdgui_menu_lobby.cpp`: read room list from received server data, not local room pool
- [ ] Add room access modes: public/password/invite (wire `CLC_ROOM_JOIN` with optional password)

**Protocol bump**: Add as v22 if B-12 Phase 3 hasn't landed yet; otherwise combine bumps.

---

### Phase R-4: Match Start
**Target files**: `netmsg.h`, `netmsg.c`, `netlobby.c`, `net.c`

- [ ] Add `CLC_ROOM_SETTINGS 0x0C`, `CLC_ROOM_KICK 0x0D`, `CLC_ROOM_TRANSFER 0x0E`, `CLC_ROOM_START 0x0F` to `netmsg.h`
- [ ] Implement `netmsgClcRoomSettingsRead` — validate sender is leader, update room settings
- [ ] Implement `netmsgClcRoomKickRead` — leader kicks a player from room
- [ ] Implement `netmsgClcRoomTransferRead` — leader transfers leadership
- [ ] Implement `netmsgClcRoomStartRead` — validate leader + settings, trigger `SVC_STAGE_START` to room members only
- [ ] Room state transitions: `LOBBY → LOADING → MATCH → POSTGAME → LOBBY`
- [ ] Scope stage start/end to room members (not all connected clients)
- [ ] Deprecate `CLC_LOBBY_START (0x08)` — keep as fallback, wire `CLC_ROOM_START` as primary

---

### Phase R-5: Server GUI Redesign
**Target files**: `server_gui.cpp`

- [ ] New 3-panel layout: Players (left), Rooms (right), Log (bottom)
- [ ] Players panel: name, room, leader indicator, state, ping; Move + Kick actions
- [ ] Rooms panel: list of active rooms with leader/players/mode/state; Set Leader + Close Room actions
- [ ] Status header: connect code + Copy, port (no raw IP), player count, room count, ONLINE/OFFLINE
- [ ] Room column in player list shows room name (look up by `room_id`)
- [ ] Remove "Hub" tab (replaced by Rooms panel)
- [ ] Log scrubbing: ensure no IP strings reach `sysLogPrintf` (B-30 complete)

---

## 11. Cross-References

| Topic | Where to read |
|-------|--------------|
| Room types, access modes, player states | [multiplayer-plan.md](multiplayer-plan.md) §2 |
| Sentence connect codes, security model | [multiplayer-plan.md](multiplayer-plan.md) §3 |
| Player profiles, stats | [multiplayer-plan.md](multiplayer-plan.md) §4 |
| Server federation (future) | [multiplayer-plan.md](multiplayer-plan.md) §5 |
| End-to-end join flow (J-series) | [join-flow-plan.md](join-flow-plan.md) |
| Client lobby UI room display gap | [join-flow-plan.md](join-flow-plan.md) §3.2, §5.2 |
| Protocol message table | [networking.md](networking.md) |
| Server architecture | [server-architecture.md](server-architecture.md) |

---

*Last updated: 2026-03-26, Session 51*
