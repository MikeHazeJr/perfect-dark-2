# Network Architecture

> Authoritative network design for Perfect Dark 2. Consolidates what were five
> separate plan documents (`multiplayer-plan.md`, `master-server-plan.md`,
> `join-flow-plan.md`, `lobby-flow-plan.md`, `room-architecture-plan.md`) into
> a single coherent architecture under one roof.
>
> Companion docs (kept separate): [networking.md](networking.md) — protocol
> reference and message-type cheatsheet; [network-system-audit.md](network-system-audit.md)
> — the 2026-04-02 definitive audit (39 SVC + 10 CLC + lifecycle + tick model);
> [nat-traversal-architecture.md](designs/nat-traversal-architecture.md) — STUN / hole-punch design (D8, DONE S83).
>
> **Last consolidated**: 2026-04-14. Status markers reflect state as of dev `ec8384ed`.
> Back to [index](README.md).

---

## 1. Overview

### 1.1 Vision

The dedicated server is a **social hub**, not just a match host. Players connect
to a server and exist within it as a persistent presence regardless of what
they're doing. A server with 8 connected players might have one player doing a
solo campaign mission (local, but presence visible to others), three players in
an online bot match together, two players in local splitscreen on the level
editor, one player spectating the bot match, and one player browsing their
profile.

All connected players can see each other's status, send invites, join joinable
sessions, and view player profiles. The server is the persistent social layer.

### 1.2 Confirmed architectural decisions (Game Director, S48)

1. **All non-local multiplayer goes through a dedicated server.** No P2P, no
   client-hosted matches. The dedicated server is the sole authority for all
   connected play.
2. **Campaign is inherently co-op.** Solo campaign = co-op with 1 player.
   Drop-in / drop-out multiplayer for campaign missions. Treat solo and co-op
   as the same room type internally.
3. **Server federation routing is automatic and invisible to players.** Players
   connect to any server; the mesh routes their sessions to the lowest-load
   node transparently. (Phase 3+; not built yet.)
4. **Stats framework first, achievements later.** Build granular event tracking
   from the start (every kill, death, weapon used, mode played is a countable
   event). Achievements are a query layer on top.
5. **Campaign must work offline.** Online mode merely routes authority to the
   server; logic and save format are identical to offline.

### 1.3 Process topology

```
Server Process (dedicated or client-hosted)
|
+-- Hub (hub.c) — singleton, owns everything
|   |
|   +-- Player Registry (g_NetClients[]) — connected players, presence state
|   +-- Room Pool (room.c) — concurrent activity containers
|   +-- Profile Store (future) — player stats, achievements, shared content
|   +-- Federation Link (future) — connection to other servers in the mesh
|
+-- ENet Transport — UDP networking (60 Hz tick, protocol v35)
+-- Identity (identity.c) — device UUID + player profiles
+-- Connect-code layer (connectcode.c) — human-friendly server addresses
```

### 1.4 Room types

| Type | Description | Max Players | Authority |
|------|-------------|-------------|-----------|
| MP_MATCH | Competitive multiplayer (deathmatch, teams, etc.) | 8 + 32 bots | Server |
| CAMPAIGN | Campaign mission (solo or co-op, drop-in/drop-out) | 1-4 | Server or Local |
| SPLITSCREEN | Local splitscreen (any mode) | 4 local | Local (or Server if connected) |
| EDITOR | Level editor session | 1-4 | Server |
| SPECTATE | Watching another room | unlimited | Read-only |
| LOBBY | Waiting room before any session starts | 8 | Server |

Splitscreen does not require a server connection — works fully offline with
local authority. When connected, splitscreen players are treated as a connected
group sharing one network seat.

### 1.5 Player states (per server connection)

| State | Description |
|-------|-------------|
| CONNECTED | Authenticated, in the hub, not in any room |
| IN_LOBBY | In a room's lobby, configuring |
| IN_GAME | Actively playing in a room |
| SPECTATING | Watching a room |
| AWAY | Connected but idle / AFK |

### 1.6 Room access & slots

**Server slot pool**: the server has a configurable total player slot count
(`HUB_MAX_CLIENTS = 32`). Rooms draw slots from the pool. Zero over-allocation.

**Room access modes**:
- **Open** — anyone connected to the server can join
- **Password** — requires a password set by the room creator
- **Invite-only** — room creator selects from connected players. Friends
  prioritized (distinct color, sorted top).

**Friends system** (planned): mutual, persisted locally. Friends appear in a
distinct color in all player lists. Distinguishes same-name players.

---

## 2. Connection Layer

### 2.1 Sentence connect codes (DONE — S48)

IPv4 address encoded as a 4-word memorable sentence:
`[adjective] [creature/object] [action phrase] [place]`

Examples: "fat vampire running to the park", "tiny cheese skipping around
space", "heroic flea hiding in a mall".

256 words per slot × 4 slots = 32 bits = full IPv4. Port assumed as default
(27100). Case-insensitive decode. Replaces the earlier phonetic syllable system
entirely.

**Byte-order note**: `connectCodeEncode()`/`connectCodeDecode()` use host byte
order (little-endian on Windows), not network byte order despite the header
comment. Both sides use the same convention, so round-trips are consistent. Do
not apply `htonl()` before passing IPs. See [constraints.md](constraints.md).

### 2.2 Security: code-only joining (DONE — enforced)

Connect codes serve convenience AND security:

- **No raw IP addresses displayed** in any UI surface (active constraint —
  see [constraints.md](constraints.md)).
- **No direct IP input accepted** in any join flow.
- The connect code is the SOLE mechanism for sharing connection info.
- Validation: exactly 4 words, each must exist in its dictionary slot.
- Invalid input → no connection attempt (fail before network).
- `g_NetLastJoinAddr` and `g_NetRecentServers` store raw IPs internally — never
  exposed in UI. Future server-history UI must encode back to codes for display.

### 2.3 Join flow (DONE — J-1, J-2, J-4, J-5)

```
CLIENT                              SERVER (dedicated process)
  |                                    |
  | [Main Menu → Online Play]          | [netStartServer()]
  | → 4-word connect code              |   ENet host on :27100
  | → connectCodeDecode() → IP:port    |   hubInit() → roomsInit() + identityInit()
  | → netStartClient(addrStr)          |
  |  --- ENet CONNECT ----------->     |
  | CLSTATE_CONNECTING                 |
  |  ← ENET_EVENT_TYPE_CONNECT ---     |
  | CLSTATE_AUTH                       |
  |  --- CLC_AUTH (name/head/body) -->  | netmsgClcAuthRead(): validate
  |  <--- SVC_AUTH_OK ----------       | → CLSTATE_LOBBY
  | CLSTATE_LOBBY                      | hubTick() runs every frame
  |  <--- SVC_ROOM_LIST -------         | Server broadcasts full room state
  |  [leader presses game mode]        |
  |  --- CLC_ROOM_START -------->      | netmsgClcRoomStartRead()
  |  <--- SVC_STAGE_START ---          | room LOBBY → LOADING → MATCH
  | CLSTATE_GAME                       | (room-scoped)
```

### 2.4 NAT traversal (DONE — D8, S83)

All 4 phases shipped: STUN client (RFC 5389, `port/src/net/netstun.c`); server
advertises STUN-discovered public IP to lobby clients via `SVC_ADDR_QUERY` /
`CLC_ADDR_REPORT`; symmetric hole-punch (`port/src/net/netholepunch.c`)
with 5 probe packets / 3 s timeout / relay fallback; NAT diagnostics section in
debug menu. Full design in
[designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md).

### 2.5 Server GUI connect-code display (DONE — J-2, S84)

`server_gui.cpp` shows the connect code via an IP waterfall: UPnP → STUN →
empty (LAN-only). Shows "discovering..." while in flight. Never shows the raw
IP. `pdgui_bridge.c::netGetPublicIP()` checks STUN between UPnP and HTTP
fallback.

### 2.6 Recent-server history (DONE — J-4, S80 / S84)

`serverhistory.json` persists recent connections. Recent Servers panel in the
join view shows connect code + relative timestamp ("5s ago", "12m ago" via
`fmtRelTime` lambda, S84). Subtitle format: "ABC-DEF · 5m ago". Config keys
`Net.RecentServer.N.Host` + `Net.RecentServer.N.Time`; `time(NULL)` unix
timestamps, not tick-based.

### 2.7 Lobby handoff (DONE — J-5, S81)

Client transition from main-menu join view → CLSTATE_LOBBY → lobby overlay is
clean. Verified `pdgui_lobby.cpp` gates on client state.

---

## 3. Room Architecture (server-side / protocol)

### 3.1 Design principles

- **Server is not a player.** Dedicated server sets `g_NetLocalClient = NULL`
  and `g_NetNumClients = 0` at startup; slot 0 is free for real players. All
  code paths dereferencing `g_NetLocalClient` must NULL-guard. Active
  constraint — see [constraints.md](constraints.md).
- **No raw IPs anywhere.** Connect codes only.
- **Rooms are demand-driven.** Zero players = zero rooms. Room 0 is no longer
  permanent (removed the transitional artifact).
- **Room leader has authority.** Only the room leader configures settings and
  starts a match. Server validates leader on all settings/start messages.
- **Server operator has override authority.** Server GUI can reassign / move /
  kick / close.
- **Room-settings dirty-flag broadcast pattern** (active constraint — see
  [constraints.md](constraints.md)). Leader-side mutations set a file-static
  dirty flag and flush at end-of-frame, not inline during drag/scroll.

### 3.2 Room lifecycle

```
ROOM_STATE_LOBBY → LOADING → MATCH → POSTGAME → LOBBY (loop)
                                            → CLOSED (disband)
```

Enum in `port/include/room.h`:

- `ROOM_STATE_LOBBY    = 0`
- `ROOM_STATE_LOADING  = 1`
- `ROOM_STATE_MATCH    = 2`
- `ROOM_STATE_POSTGAME = 3`
- `ROOM_STATE_CLOSED   = 4`

### 3.3 On-event behaviour

- **Client connect (post-auth)**: create a new room → assign client as leader
  (`leader_client_id = cl->id`), set `cl->room_id`, send `SVC_ROOM_ASSIGN` to
  the client, broadcast `SVC_ROOM_LIST` to all lobby clients.
- **CLC_ROOM_JOIN**: validate room exists / accessible / has capacity; remove
  player from origin room (destroy if now empty); assign to new room; send
  `SVC_ROOM_ASSIGN`; broadcast `SVC_ROOM_UPDATE` for both rooms.
- **CLC_ROOM_LEAVE**: remove from room, auto-transfer leadership if needed;
  destroy if empty; create new single-player room for this client; send
  `SVC_ROOM_ASSIGN`; broadcast `SVC_ROOM_LIST`.
- **Client disconnect**: remove from room; transfer leadership if needed;
  destroy room if empty; free `netclient` slot; broadcast `SVC_ROOM_UPDATE`.
- **CLC_ROOM_START** (leader only): validate sender is leader + game mode/stage
  set; LOBBY → LOADING; broadcast `SVC_STAGE_START` to room members only (not
  the whole server); LOADING → MATCH once all room clients reach CLSTATE_GAME.

### 3.4 Data structures

**`hub_room_t`** (`port/include/room.h`):

```c
typedef struct hub_room_s {
    u8            id;
    room_state_t  state;
    char          name[ROOM_NAME_MAX];      /* 32 */
    u8            clients[HUB_MAX_CLIENTS]; /* 32 */
    u8            client_count;
    u8            max_players;
    u8            stagenum;
    u8            scenario;                 /* NETGAMEMODE_* */
    u32           rng_seed;
    room_access_t access;                   /* OPEN / PASSWORD / INVITE */
    char          password[32];
    u8            creator_client_id;
    u8            leader_client_id;         /* added R-2 */
    u32           created_tick;
    u32           state_enter_tick;
} hub_room_t;
```

Constants: `HUB_MAX_ROOMS = 16`, `HUB_MAX_CLIENTS = 32`. Must match
`NET_MAX_CLIENTS` in `net.h`.

**`struct netclient`** (`port/include/net/net.h`):

```c
    s32  room_id;    /* -1 = not assigned; otherwise hub room ID */
```

Initialised to `-1` in `netClientReset()`.

### 3.5 Net protocol messages

Current protocol: **v35**. Message ID allocations:

| ID | Name | Direction | Added |
|----|------|-----------|-------|
| `0x0A` | CLC_ROOM_JOIN | Client→Server | R-3 (S143) |
| `0x0B` | CLC_ROOM_LEAVE | Client→Server | R-3 (S143) |
| `0x0C` | CLC_ROOM_SETTINGS | Client→Server | R-4 |
| `0x0D` | CLC_ROOM_KICK | Client→Server | R-4 (planned) |
| `0x0E` | CLC_ROOM_TRANSFER | Client→Server | R-4 (planned) |
| `0x0F` | CLC_ROOM_START | Client→Server | R-4 |
| `0x13` | CLC_ROOM_SETTINGS_UPDATE | Client→Server | S253 (v35) |
| `0x14` | CLC_ROOM_PLAYLIST_UPDATE | Client→Server | S253 (v35) |
| `0x75` | SVC_ROOM_LIST | Server→Client | R-3 (S143) |
| `0x76` | SVC_ROOM_UPDATE | Server→Client | R-3 (S143) |
| `0x77` | SVC_ROOM_ASSIGN | Server→Client | R-3 (S143) |
| `0x78` | SVC_ROOM_SETTINGS | Server→Client | S253 (v35) |
| `0x79` | SVC_ROOM_PLAYLIST | Server→Client | S253 (v35) |

`CLC_LOBBY_START (0x08)` remains for backward compat. `CLC_ROOM_START (0x0F)`
is the canonical new flow.

### 3.6 Wire formats

**SVC_ROOM_LIST**:

```
u8   room_count
for each room:
  u8   room_id
  u8   state              (room_state_t)
  u8   client_count
  u8   max_players
  u8   leader_client_id
  u8   scenario           (NETGAMEMODE_*)
  u8   stagenum
  u8   access             (room_access_t)
  char name[32]
```

**SVC_ROOM_ASSIGN**:

```
u8   room_id     (0xFF = kicked / no room)
u8   is_leader   (1 if you are the leader)
```

**CLC_ROOM_JOIN**:

```
u8   room_id      (0xFF = create new)
char password[32] (empty if not required)
```

**SVC_ROOM_SETTINGS / CLC_ROOM_SETTINGS_UPDATE**: Serialises the `g_MatchConfig`
subset leader can mutate: bot count, player count, scenario, arena, timelimit,
scorelimit, weapon set. Clients apply on receipt. Leader flushes at end-of-frame
via `netSendRoomSettingsUpdate()` (see [constraints.md](constraints.md)).

**SVC_ROOM_PLAYLIST / CLC_ROOM_PLAYLIST_UPDATE**: Music playlist (mod track list)
synced by the same dirty-flag pattern via `netSendRoomPlaylistUpdate()`.

### 3.7 Hub slot-pool API (`port/include/hub.h`)

```c
s32 hubGetMaxSlots(void);
void hubSetMaxSlots(s32 maxSlots);
s32 hubGetUsedSlots(void);
s32 hubGetFreeSlots(void);
```

Implemented in `hub.c` backed by `g_NetMaxClients` / `g_NetNumClients`. Added R-1.

---

## 4. Client UX Flow

### 4.1 Screen flow

```
[Online Play entry] → [Social Lobby] → [Create Room] ─┐
                                     → [Join Room]    ─┤
                                                       ↓
                                         [Room Interior]
                                              │
                      ┌───────────────────────┼──────────────────────────┐
                      ↓                       ↓                          ↓
             [Combat Sim Setup]      [Campaign Setup]          [Counter-Op Setup]
                      │                       │                          │
                      └───────────────────────┴──────────────────────────┘
                                              ↓
                                      [Match Start] (drop-in / drop-out)
```

### 4.2 Screens

1. **Social Lobby** — `pdgui_menu_lobby.cpp`. Player list, room list with Join
   per row, Create Room button, connect code, disconnect. No game-mode picker
   here.
2. **Room Interior** — `pdgui_menu_room.cpp`. Room name, player list, mode
   selector (leader), access-mode toggle, Leave Room. Non-leaders see
   read-only mode display.
3. **Combat Sim Setup** — `pdgui_menu_matchsetup.cpp` (network-aware variant):
   arena / sims / difficulty / scenario / team assign / weapon set. Leader
   clicks Start → `CLC_ROOM_START`.
4. **Campaign Setup** — mission + difficulty picker.
5. **Counter-Op Setup** — mission + role-assign (who is Counter-Op) + difficulty.
6. **In-Match** — existing gameplay. Drop-in allows latejoin to
   `ROOM_STATE_MATCH`. Post-match returns to Room Interior.

### 4.3 Client state machine (UI layer on top of CLSTATE_*)

```
DISCONNECTED → SOCIAL_LOBBY ⇄ ROOM_LOBBY ⇄ MODE_SETUP → LOADING → INGAME → POSTGAME → ROOM_LOBBY
```

Managed via `menumgr` (`port/src/menumgr.c`). Menu states:
`MENU_LOBBY`, `MENU_ROOM_INTERIOR`, `MENU_MODE_SETUP` (set of one).

### 4.4 Key transitions

| Transition | Client sends | Server sends |
|-----------|-------------|--------------|
| Reach CLSTATE_LOBBY | — | SVC_ROOM_LIST |
| Create Room | CLC_ROOM_JOIN (room_id=0xFF) | SVC_ROOM_ASSIGN + SVC_ROOM_LIST |
| Join Room | CLC_ROOM_JOIN (room_id=N) | SVC_ROOM_ASSIGN + SVC_ROOM_UPDATE |
| Leader changes settings | CLC_ROOM_SETTINGS_UPDATE (0x13) | SVC_ROOM_SETTINGS (0x78) to room |
| Leader changes playlist | CLC_ROOM_PLAYLIST_UPDATE (0x14) | SVC_ROOM_PLAYLIST (0x79) to room |
| Leader starts match | CLC_ROOM_START | SVC_STAGE_START (room-scoped) |
| Player leaves | CLC_ROOM_LEAVE | SVC_ROOM_ASSIGN (new solo room) + SVC_ROOM_LIST |
| Player disconnects | (ENet disconnect) | SVC_ROOM_UPDATE (leadership transfer) |
| Match ends | — | SVC_ROOM_UPDATE (state=POSTGAME) |

---

## 5. Dedicated-Server Safety

### 5.1 B-28 — server is not a player

In `netStartServer()` (`port/src/net/net.c`):

```c
if (g_NetDedicated) {
    g_NetLocalClient = NULL;     /* no local player on dedicated */
} else {
    g_NetLocalClient = &g_NetClients[0];
    g_NetLocalClient->state = CLSTATE_LOBBY;
    netClientReadConfig(g_NetLocalClient, 0);
}
```

All sites dereferencing `g_NetLocalClient` NULL-guard (enforced since S50).

### 5.2 B-29 / B-30 — IP scrubbing

- `server_gui.cpp` status bar shows "Port N" only (no raw IP).
- `netFormatClientAddr()` output is NOT used in player-facing log lines —
  connection events log client index + name: `"client 3 (Agent Dark) disconnected"`.
- UPnP internal-infrastructure logs may still surface IPs (acceptable — not
  player-facing).
- Server-side `sysLogPrintf("LOBBY: connect code: ...")` fires once on startup.

---

## 6. Server GUI (R-5 — planned)

### 6.1 Target layout

```
┌─────────────────────────────────────────────────────────────────┐
│ PD2 Dedicated Server v0.0.X                                     │
│ Connect Code: wicked spider sliding under a savanna  [Copy]     │
│ Players: 3/32 | Rooms: 2 | Status: ONLINE                       │
├────────────────────────┬────────────────────────────────────────┤
│ Players          [tab] │ Rooms                           [tab]  │
│ ┌──────────────────┐   │ ┌──────────────────────────────────┐   │
│ │ Agent Name  Room │   │ │ Room 1: Fat Monkey               │   │
│ │ MikeHazeJr  R1 L │   │ │   Leader: MikeHazeJr  State: Lobby│  │
│ │ Player2     R1   │   │ │   Players: 2/8  Mode: Combat Sim │   │
│ │ Player3     R2 L │   │ ├──────────────────────────────────┤   │
│ └──────────────────┘   │ │ Room 2: Tall Frog                │   │
│ Selected: Player2      │ │   Leader: Player3  State: Lobby  │   │
│ [Kick] [Move→Room]     │ │   Players: 1/8  Mode: Combat Sim │   │
│                        │ └──────────────────────────────────┘   │
│                        │ Selected: Room 1                       │
│                        │ [Set Leader] [Close Room]              │
├────────────────────────┴────────────────────────────────────────┤
│ Server Log                                                      │
└─────────────────────────────────────────────────────────────────┘
```

### 6.2 Operator actions

- **Kick** — disconnect player from server (DONE).
- **Move to Room** — dropdown of rooms, server-moves player (R-5 pending).
- **Set Leader** — reassign leadership in a room (R-5 pending).
- **Close Room** — destroy room, all players to new individual rooms (R-5 pending).

### 6.3 Current state (S84)

Tabbed layout "Server" + "Hub" shipped. Player list with Kick present. Hub tab
shows room table. R-5 redesign consolidates Hub into Rooms panel with operator
actions listed above.

---

## 7. Master Server (D16 — planned)

### 7.1 Purpose

Lightweight master server maintaining a live registry of dedicated game
servers. Enables players to browse and join matches without knowing server IPs.
Game servers register on startup + heartbeat periodically. Clients query the
master for a server list, then connect directly to the chosen game server via
existing ENet protocol. The master is **never** in the gameplay data path.

### 7.2 Why it can wait

The master is **purely additive**:

- Server side already has `g_NetServerPort`, `g_NetMaxClients`,
  `g_NetNumClients`, `g_NetGameMode`, UPnP external-IP discovery. Registration
  is just packaging these into a UDP packet.
- Client side `netRecentServer*` infrastructure already models a list of
  `{addr, flags, numclients, maxclients, online}`. Master query becomes a new
  source for the same data.
- No changes to CLC / SVC messages, ENet channels, or game-state sync.

### 7.3 Topology

```
              ┌──────────────┐
              │ Master Server │  (standalone process, ~500 LOC, UDP :27099)
              └──────┬───────┘
                     │
        ┌────────────┼────────────┐
        │            │            │
  ┌─────┴─────┐ ┌───┴─────┐ ┌───┴─────┐
  │ Game Srv A │ │ Game Srv B│ │ Game Srv C│  (existing pd-server)
  └─────┬─────┘ └────┬────┘ └────┬────┘
        │             │           │
        └──────┬──────┘           │
               │                  │
          ┌────┴────┐         ┌───┴────┐
          │ Client1 │         │ Client2 │  (existing pd client)
          └────────┘          └────────┘

  1. Game server → Master: REGISTER (startup), HEARTBEAT (15 s)
  2. Client → Master: QUERY_SERVERS
  3. Master → Client: SERVER_LIST
  4. Client → Game Server: direct ENet connect (protocol unchanged)
```

### 7.4 Master protocol (new UDP, no ENet)

| Message | Direction | Payload |
|---------|-----------|---------|
| MS_REGISTER | Server→Master | protocol_ver, port, maxclients, gamemode, name[32], auth_token[16] |
| MS_HEARTBEAT | Server→Master | port, numclients, gamemode, lobby_state |
| MS_UNREGISTER | Server→Master | port, auth_token[16] |
| MS_QUERY | Client→Master | protocol_ver, filter_flags |
| MS_SERVER_LIST | Master→Client | count, array of {ip[4], port, numclients, maxclients, gamemode, name[32], ping_hint} |

### 7.5 Phases (all planned)

| Phase | Scope | Est. LOC |
|-------|-------|----------|
| D16a | Master server process (new `master_server.c`) | ~300 |
| D16b | Game-server registration in `server_main.c` + new `port/src/net/netmaster.c` | ~100 |
| D16c | Client server-browser tab in `pdgui_menu_network.cpp` | ~200 |
| D16d | Security + polish (rate limit, HMAC tokens, ping probes) | ~150 |

### 7.6 Implementation note

Servers age out after 3 consecutive missed heartbeats (45 s timeout). Simple
auth via shared token in config. If master is unreachable, the server still
works standalone — registration is best-effort.

---

## 8. Player Profiles & Stats

### 8.1 Local identity (DONE — S47d)

Stored in `pd-identity.dat`: device UUID (unique per machine) + up to 4 player
profiles (name, preferred head/body, flags). `identity.c`.

**Constraint**: On PC, `identityGetActiveProfile()->name` is the canonical
player name. Legacy N64 config field is only consulted as fallback (see
[constraints.md](constraints.md)).

### 8.2 Persistent stats (CODED — S49, needs wire-in)

`port/src/playerstats.c` — string-keyed hash table of counters, JSON
persistence to `$S/playerstats.json`. Stats increment per gameplay event
(kill, death, shot, mode played, weapon used). Accessor `statIncrement()`.

**Open**: wire into gameplay sites (`mpstats.c`, `mplayer.c`). Until then,
stats are recorded only by audio telemetry + the few sites that already call
it.

### 8.3 Achievements (future)

Query layer on top of persistent stats. Example: "Complete all missions on
Perfect Agent", "Win a match with 30 bots". Stored locally, displayed on
profile.

### 8.4 Shared content (future)

Custom maps (level editor), mod packs (PDPK), bot presets (bot customizer).
Visible on a player's profile. Other connected players can download directly.

---

## 9. Server Federation (future)

Multiple servers link in a mesh. Benefits: larger player pool (all connected
servers' players visible), load distribution (new sessions routed to
lowest-load server), redundancy.

```
Server A ⇄ Server B ⇄ Server C
   ↕                       ↕
   └─────→ Server D ←──────┘
```

Shared across the mesh: player presence, server load metrics, shared-content
catalog, chat/invite routing.

Phase 3+ work. Foundation (single-server hub) must be solid first. Routing is
automatic and invisible to the player (Game-Director decision §1.2.3).

---

## 10. Implementation Status

### 10.1 J-series — join flow (mostly DONE)

| ID | Scope | Status |
|----|-------|--------|
| J-1 | End-to-end verify basic join | DONE (S81) |
| J-2 | Server GUI connect-code display (UPnP→STUN waterfall) | DONE (S84) |
| J-3 | Room state protocol (SVC_ROOM_LIST) | DONE (R-3, S143) |
| J-4 | Recent-servers history UI with relative timestamps | DONE (S80 / S84) |
| J-5 | Main-menu ↔ lobby handoff polish | DONE (S81) |

### 10.2 R-series — server / protocol (largely DONE)

| ID | Scope | Status |
|----|-------|--------|
| R-1 | Foundation: hub slot-pool API, B-28 / B-29 / B-30 | DONE |
| R-2 | Room lifecycle: `leader_client_id`, demand-driven rooms, `HUB_MAX_ROOMS=16` / `HUB_MAX_CLIENTS=32` | DONE |
| R-3 | Room sync: SVC_ROOM_LIST / UPDATE / ASSIGN, CLC_ROOM_JOIN / LEAVE | DONE (S143) |
| R-4 | Match start: CLC_ROOM_SETTINGS / START + S253 additive SVC_ROOM_SETTINGS / PLAYLIST + CLC_ROOM_SETTINGS_UPDATE / PLAYLIST_UPDATE | DONE (S143 + S253) |
| R-4 ancillary | CLC_ROOM_KICK / CLC_ROOM_TRANSFER | PLANNED (operator ops) |
| R-5 | Server GUI redesign | PLANNED |

### 10.3 L-series — client UX (mostly DONE)

| ID | Scope | Status |
|----|-------|--------|
| L-1 | Social Lobby: strip game-mode from lobby, add Create/Join Room | DONE |
| L-2 | Room Create / Join | DONE |
| L-3 | Room Interior + mode selection | DONE (`pdgui_menu_room.cpp`) |
| L-4 | Combat Sim Setup (network-aware) | DONE (integrated in `pdgui_menu_matchsetup.cpp` + S253 room residual) |
| L-5 | Campaign + Counter-Op Setup | PARTIAL (co-op host dialog exists; dedicated `pdgui_menu_campaign_setup.cpp` not written; S241/S253 wired protocol side) |
| L-6 | Drop-in / drop-out | PARTIAL (server handles latejoin; client "Join Match in Progress" prompt not yet wired) |

### 10.4 D16 — master server (PLANNED)

- D16a, D16b, D16c, D16d — all planned (§7 above). ~750 LOC total. Nothing in
  current code needs changes to prepare.

---

## 11. Open Items

Active work fronts (see `tasks-current.md` for priority):

- **Bug B — countdown-cancel-on-room-close** (fix in `readyGateTickCountdown()` / `netmsg.c`)
- **R-4 ancillary** — CLC_ROOM_KICK / CLC_ROOM_TRANSFER operator ops
- **R-5** — server GUI redesign (Players + Rooms panels, operator actions)
- **L-5** — dedicated Campaign / Counter-Op setup screen
- **L-6** — drop-in join prompt on client side
- **D16** — master server (all 4 phases)
- **Player stats wire-in** — call `statIncrement()` at gameplay sites
- **Friends system** — persistence + distinct color in player lists

---

## 12. References

| Topic | Where |
|-------|-------|
| Active invariants (protocol v35, connect-code, server-is-not-a-player, room-settings dirty flag, etc.) | [constraints.md](constraints.md) |
| Protocol / message type cheatsheet | [networking.md](networking.md) |
| Definitive protocol audit (39 SVC + 10 CLC, lifecycle, tick model) | [network-system-audit.md](network-system-audit.md) |
| NAT traversal (STUN / hole-punch / relay) | [designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md) |
| Server process architecture | [server-architecture.md](server-architecture.md) |
| Manifest pipeline during match start | [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md) |
| Catalog ID as on-wire identity (scenarios / bodies / heads / weapons) | [constraints.md](constraints.md) + `designs/session-catalog-and-modular-api.md` |

---

*Consolidated 2026-04-14 from multiplayer-plan.md (S48) + master-server-plan.md (2026-03-19) + join-flow-plan.md (S49) + lobby-flow-plan.md (S57) + room-architecture-plan.md (S51 / S57). Originals moved to `_archive/plans/` with `git mv` for history traceability.*
