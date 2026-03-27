# Network Stack Audit

> Created: 2026-03-27, Session 57 (crazy-brattain)
> Scope: Complete audit of ENet transport, connection lifecycle, message protocol, tick model,
>        lobby sync, in-game state sync, gaps, races, and recommendations.
> Sources: net.h, netmsg.h, netlobby.h, netbuf.c, net.c, netmsg.c, netlobby.c,
>          server_main.c, server_bridge.c, net_interface.h, net_server_callbacks.c — all read directly.
> Note: `src/game/net.c` does not exist. There is no N64 networking heritage to reference; the
>       entire networking stack was written from scratch for the PC port.
> Back to [index](README.md)

---

## 1. Connection Lifecycle

### 1.1 Client-Side Path

```
[Main Menu → Online Play]
  user enters 4-word connect code
  connectCodeDecode() → IP:port string
  netStartClient(addrStr)
    → g_NetMode = NETMODE_CLIENT
    → g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS]  (temporary slot)
    → CLSTATE_CONNECTING
    → enet_host_connect(ip:port, NETCHAN_COUNT, NET_PROTOCOL_VER)
    → lobbyInit()

[ENet ENET_EVENT_TYPE_CONNECT fires in netStartFrame()]
  netClientEvConnect()
    → g_NetLocalClient->state = CLSTATE_AUTH
    → sends CLC_AUTH + CLC_SETTINGS (single reliable packet on NETCHAN_CONTROL)

[Server responds SVC_AUTH on NETCHAN_CONTROL]
  netmsgSvcAuthRead()
    → receives assigned client ID (u8) + maxclients + current tick
    → moves g_NetLocalClient from temp slot [NET_MAX_CLIENTS] to g_NetClients[id]
    → g_NetLocalClient->state = CLSTATE_LOBBY

[Server may send SVC_CATALOG_INFO on NETCHAN_CONTROL after SVC_AUTH]
  → client diffs against local catalog, sends CLC_CATALOG_DIFF
  → server sends SVC_DISTRIB_BEGIN / CHUNK / END on NETCHAN_TRANSFER

[In CLSTATE_LOBBY]
  lobbyUpdate() runs each frame
  pdguiLobbyScreenRender() shows player list + room list (local snapshot only)

[Leader presses game mode button → CLC_LOBBY_START sent]

[Server broadcasts SVC_STAGE_START]
  netmsgSvcStageStartRead()
    → syncs tick, RNG seeds, stage settings, player manifest
    → transitions all known clients to CLSTATE_GAME
    → calls mpStartMatch() or mainChangeToStage() depending on mode
    → g_NetLocalClient->state = CLSTATE_GAME
```

### 1.2 Server-Side Path

```
[server_main.c: main()]
  netInit()         → ENet init, parse --port/--maxclients/--bind
  lobbyInit()       → clears g_Lobby, leaderSlot = 0xFF
  hubInit()         → hub state + room pool init
  netStartServer(port, maxclients)
    → enet_host_create on g_NetLocalAddr
    → if g_NetDedicated: g_NetLocalClient = NULL, g_NetNumClients = 0
    → else: g_NetLocalClient = &g_NetClients[0], state = CLSTATE_LOBBY
    → g_NetMode = NETMODE_SERVER
    → netUpnpSetup(port)   ← async UPnP on background thread
    → lobbyInit()          ← re-initializes lobby
    → netDistribInit()

[Main loop: netStartFrame() → lobbyUpdate() → hubTick() → netEndFrame()]

[ENet ENET_EVENT_TYPE_CONNECT fires in netStartFrame()]
  netServerEvConnect(peer, data)
    → validates protocol version (data == NET_PROTOCOL_VER)
    → allocates first free slot (start at 0 for dedicated, 1 for listen server)
    → rejects if server full (DISCONNECT_FULL)
    → rejects if in-game + no preserved slots (DISCONNECT_LATE)
    → cl->state = CLSTATE_AUTH
    → cl->flags = ingame ? CLFLAG_ABSENT : 0
    → ++g_NetNumClients

[CLC_AUTH + CLC_SETTINGS arrive in same packet]
  netmsgClcAuthRead()
    → validates state == CLSTATE_AUTH
    → on non-dedicated: validates ROM + mod dir match
    → checks mid-game reconnect (netServerFindPreserved)
    → cl->state = CLSTATE_LOBBY
    → sends SVC_AUTH on NETCHAN_CONTROL (per-client buffer)
    → sends SVC_CATALOG_INFO on NETCHAN_CONTROL
    → if reconnecting: sends SVC_STAGE_START, sets CLFLAG_ABSENT clear,
                       schedules full resync via g_NetPendingResyncFlags
  netmsgClcSettingsRead()   ← processed in same packet

[In CLSTATE_LOBBY]
  lobbyUpdate() runs each frame, re-deriving leader from client array
  CLC_LOBBY_START handler validates sender is leader, then calls
    netServerStageStart() (MP) or netServerCoopStageStart() (co-op)
    → transitions all CLSTATE_LOBBY clients to CLSTATE_GAME
    → broadcasts SVC_STAGE_START to all clients
    → sets g_NetPendingResyncFlags for post-start resync

[ENet ENET_EVENT_TYPE_DISCONNECT]
  netServerEvDisconnect(cl)
    → if cl was in-game: netServerPreservePlayer() + playerDie()
    → netClientReset(cl)
    → --g_NetNumClients
```

### 1.3 State Machine (CLSTATE)

```c
CLSTATE_DISCONNECTED = 0   // slot is free
CLSTATE_CONNECTING   = 1   // client only: ENet connect initiated, no packet yet
CLSTATE_AUTH         = 2   // both sides: server waiting for CLC_AUTH; client after connect event
CLSTATE_LOBBY        = 3   // authenticated, waiting for match
CLSTATE_GAME         = 4   // in an active match
```

**Server transitions:**
- → AUTH: `netServerEvConnect()` after ENet connect
- AUTH → LOBBY: `netmsgClcAuthRead()` after receiving CLC_AUTH
- LOBBY → GAME: `netServerStageStart()` / `netServerCoopStageStart()` (batch: all LOBBY clients at once)
- GAME → LOBBY: `netServerStageEnd()` (MP) / game-mode-specific (co-op)
- Any → DISCONNECTED: `netServerEvDisconnect()` + `netClientReset()`

**Client transitions:**
- → CONNECTING: `netStartClient()`
- CONNECTING → AUTH: `netClientEvConnect()` after ENet connect event
- AUTH → LOBBY: `netmsgSvcAuthRead()` after receiving SVC_AUTH
- LOBBY → GAME: `netmsgSvcStageStartRead()` after receiving SVC_STAGE_START
- GAME → LOBBY: `netmsgSvcStageEndRead()` (MP mode)
- Any → DISCONNECTED: `netClientEvDisconnect()` → `netDisconnect()`

**Important**: CLSTATE_CONNECTING is never seen server-side. The server skips straight to CLSTATE_AUTH when the ENet connect event fires.

### 1.4 Match Start: Player Number and SyncID Assignment

When `SVC_STAGE_START` is processed on the client, two allocation passes run back-to-back that establish the shared identity space for the match:

**`netPlayersAllocate()`** — called from `netmsgSvcStageStartRead()`:
1. The server assigns player numbers sequentially starting at 0 in the player manifest embedded in SVC_STAGE_START.
2. The client always wants to be playernum 0 locally (the game assumes local player = slot 0 in most arrays).
3. If the client's assigned playernum ≠ 0, the client **swaps** the `g_PlayerConfigsArray` entries for its actual slot and slot 0.
4. After the swap, the client references itself as playernum 0 everywhere in the game code.
5. The function body contains lines beginning with `\ ` (backslash-space) that look like comments but are actually **C line-continuation characters**. See §7.8.

**`netSyncIdsAllocate()`** — also called from `netmsgSvcStageStartRead()`:
1. On the server, every prop in the world gets a `syncid = prop_array_index + 1` (0 is reserved/invalid). This syncid is the network identity for the prop.
2. Syncids are assigned linearly; the assignment is deterministic given the same stage and same prop table layout.
3. On the client, if the player number swap happened (step 3 above), the client also swaps the syncids for the props owned by the two players. This keeps the syncid-to-prop mapping consistent with the local playernum-0 assumption.
4. O(n) linear lookup: `netSyncIdFind(id)` walks the full prop list. No hash table. At scale (hundreds of props) this could be a frame-budget concern, but the per-prop array is bounded by stage complexity.

**Consequence**: Every client sees itself as playernum 0. All game logic that hardcodes "local player = 0" works correctly. The server's canonical playernum ordering is only used for broadcasting — the server does not use playernum 0 to mean "local player."

---

## 2. Message Protocol Catalog

Protocol version: **21** (code: `NET_PROTOCOL_VER 21` in net.h)
> Note: networking.md documents v20; join-flow-plan.md §5.5 already flags this discrepancy.

### 2.1 SVC Messages (Server → Client)

| ID | Name | Channel | Frequency | Trigger | Payload summary | Receiver action |
|----|------|---------|-----------|---------|-----------------|-----------------|
| 0x00 | SVC_BAD | — | never | — | — | Aborts parse, logs warning |
| 0x01 | SVC_NOP | any | on demand | keepalive | nothing | noop |
| 0x02 | SVC_AUTH | CONTROL reliable | once per auth | After CLC_AUTH validated | client_id u8, maxclients u8, tick u32 | Client moves to real slot; CLSTATE_LOBBY |
| 0x03 | SVC_CHAT | DEFAULT reliable | on event | any chat message | length-prefixed string | Logs to sysLogPrintf(LOG_CHAT) |
| 0x10 | SVC_STAGE_START | DEFAULT reliable | once per match | match start or reconnect | tick, RNG seeds, stagenum, mode-specific settings, full player manifest | Client launches stage, transitions all to CLSTATE_GAME |
| 0x11 | SVC_STAGE_END | DEFAULT reliable | once per match end | `netServerStageEnd()` | gamemode u8 | MP: end match, CLSTATE_LOBBY; Co-op: endscreen |
| 0x20 | SVC_PLAYER_MOVE | DEFAULT (unreliable or reliable for force) | per tick if changed | `netEndFrame()` server | client_id u8, outmoveack u32, netplayermove struct, optional rooms for force | Client applies to that player's move buffer + lerp |
| 0x21 | SVC_PLAYER_GUNS | — | **DEAD CODE** | never sent | — | **Not in client dispatch switch; no read function declared** |
| 0x22 | SVC_PLAYER_STATS | DEFAULT reliable | on damage/death event | player damage path | varies (health, shield, state) | Updates local player health/death display |
| 0x23 | SVC_PLAYER_SCORES | DEFAULT reliable | on reconnect/resync | `NET_RESYNC_FLAG_SCORES` set | all player kill/death/point counts | Client updates score display |
| 0x30 | SVC_PROP_MOVE | DEFAULT unreliable | per update frame | `netEndFrame()` server | syncid u32, position, rotation, rooms | Client updates prop transform |
| 0x31 | SVC_PROP_SPAWN | DEFAULT reliable | on spawn event | propobj spawn path | type, syncid, initial state | Client creates new prop |
| 0x32 | SVC_PROP_DAMAGE | DEFAULT reliable | on prop damage | `objDamage()` server guard | syncid, damage, position, weapon, player | Client applies damage to prop |
| 0x33 | SVC_PROP_PICKUP | DEFAULT reliable | on pickup event | item pickup server guard | syncid, player, op | Client removes/pickups item |
| 0x34 | SVC_PROP_USE | DEFAULT reliable | on use event | door/lift use server guard | syncid, user client, op | Client triggers interaction |
| 0x35 | SVC_PROP_DOOR | DEFAULT reliable | on door state change | door state machine | syncid, mode, flags, hidden | Client updates door visual state |
| 0x36 | SVC_PROP_LIFT | DEFAULT reliable | on lift state change | lift state machine | syncid, level, speed, accel, dist, pos, rooms | Client updates lift |
| 0x37 | SVC_PROP_SYNC | DEFAULT reliable | every 120 frames | `netEndFrame()` schedule | rolling XOR checksum of all prop syncids | Client compares; desyncs trigger resync request |
| 0x38 | SVC_PROP_RESYNC | DEFAULT reliable | on resync | `NET_RESYNC_FLAG_PROPS` | full prop state dump | Client overwrites local prop state |
| 0x42 | SVC_CHR_DAMAGE | DEFAULT reliable | on chr hit | `chrDamage()` server guard | chr prop syncid, damage, vector, gset, hit part, flags | Client applies damage to chr |
| 0x43 | SVC_CHR_DISARM | DEFAULT reliable | on disarm | disarm code path | chr syncid, attacker prop syncid, weapon, damage, pos | Client drops chr weapon |
| 0x44 | SVC_CHR_MOVE | DEFAULT unreliable | every update frame (60 Hz) | `netEndFrame()` server | chr syncid, position, facing angle, rooms, speed, actions | Client updates bot/simulant transform + animation |
| 0x45 | SVC_CHR_STATE | DEFAULT reliable | every 15 frames | `netEndFrame()` server (% 15) | chr syncid, health, shield, weapon, ammo, team, blur, fade, respawn flags | Client updates bot/simulant full state |
| 0x46 | SVC_CHR_SYNC | DEFAULT reliable | every 60 frames | `netEndFrame()` server (% 60) | rolling checksum of all bots | Client compares; desyncs trigger resync request |
| 0x47 | SVC_CHR_RESYNC | DEFAULT reliable | on resync | `NET_RESYNC_FLAG_CHRS` | full state dump of all bots | Client overwrites local bot state |
| 0x48 | SVC_NPC_MOVE | DEFAULT unreliable | every 3 frames (~20 Hz) | `netEndFrame()` server co-op path (% 3) | chr slot index, position, angle, rooms, actions | Client updates NPC transform |
| 0x49 | SVC_NPC_STATE | DEFAULT reliable | every 30 frames | `netEndFrame()` server co-op (% 30) | chr slot index, health, flags, alertness, chrflags, hidden | Client updates NPC state |
| 0x4A | SVC_NPC_SYNC | DEFAULT reliable | every 120 frames | `netEndFrame()` server co-op (% 120) | XOR-rotate checksum of all NPCs | Client compares; desyncs trigger resync request |
| 0x4B | SVC_NPC_RESYNC | DEFAULT reliable | on resync | `NET_RESYNC_FLAG_NPCS` | full NPC state dump + stage flags + objective statuses | Client overwrites NPC state + mission state |
| 0x50 | SVC_STAGE_FLAG | DEFAULT reliable | on g_StageFlags change | stage flag broadcast in chraction.c | full g_StageFlags u32 | Client updates g_StageFlags |
| 0x51 | SVC_OBJ_STATUS | DEFAULT reliable | on objective change | `objectivesCheckAll()` server | index u8, status u8 | Client updates g_ObjectiveStatuses[index] |
| 0x52 | SVC_ALARM | DEFAULT reliable | on alarm state change | alarm activate/deactivate server guard | active u8 | Client updates alarm visual/audio state |
| 0x53 | SVC_CUTSCENE | DEFAULT reliable | on cutscene start/end | cutscene hook in player.c | active u8 | Client freezes/unfreezes input |
| 0x60 | SVC_LOBBY_LEADER | CONTROL reliable | **NEVER SENT** | write function exists, no callers | leaderClientId u8 | Client calls `lobbySetLeader()` by matching clientId |
| 0x61 | SVC_LOBBY_STATE | CONTROL reliable | **NEVER SENT** | write function exists, no callers | gamemode u8, stagenum u8, status u8 | Client updates g_NetGameMode + g_Lobby settings |
| 0x70 | SVC_CATALOG_INFO | CONTROL reliable | once after auth | `netDistribServerSendCatalogInfo()` in CLC_AUTH handler | count + array of (net_hash u32, id string, category string) | Client diffs vs local catalog; sends CLC_CATALOG_DIFF |
| 0x71 | SVC_DISTRIB_BEGIN | TRANSFER reliable | once per component | `netDistribServerTick()` | net_hash, id, category, total_chunks, total_bytes | Client prepares receive buffer |
| 0x72 | SVC_DISTRIB_CHUNK | TRANSFER reliable | per 16KB chunk | `netDistribServerTick()` | net_hash, chunk_idx u16, compression u8, data | Client appends to receive buffer |
| 0x73 | SVC_DISTRIB_END | TRANSFER reliable | once per component | `netDistribServerTick()` | net_hash, success u8 | Client decompresses, extracts, hot-registers mod |
| 0x74 | SVC_LOBBY_KILL_FEED | DEFAULT reliable | on kill event | kill feed broadcast | attacker string, victim string, weapon string, flags u8 | Client appends to kill feed ring buffer |

### 2.2 CLC Messages (Client → Server)

| ID | Name | Channel | Frequency | Trigger | Payload summary | Server action |
|----|------|---------|-----------|---------|-----------------|---------------|
| 0x00 | CLC_BAD | — | never | — | — | Aborts parse |
| 0x01 | CLC_NOP | any | on demand | keepalive | nothing | noop |
| 0x02 | CLC_AUTH | CONTROL reliable | once per connect | ENet connect event | name string, romName string, modDir string, localPlayers u8 | Validates files, assigns state → LOBBY, sends SVC_AUTH + SVC_CATALOG_INFO |
| 0x03 | CLC_CHAT | DEFAULT reliable | on user input | user presses Enter in chat | message string | Broadcasts SVC_CHAT to all clients |
| 0x04 | CLC_MOVE | DEFAULT (reliable if important) | per frame when changed | `netEndFrame()` client | outmoveack u32, netplayermove struct | Server applies to client inmove[] buffer |
| 0x05 | CLC_SETTINGS | CONTROL reliable | on change (also sent with CLC_AUTH) | `netClientSettingsChanged()` | options u16, bodynum, headnum, team, fovy, fovzoommult, name string | Server updates srccl->settings; applies team change if in-game |
| 0x06 | CLC_RESYNC_REQ | DEFAULT reliable | on desync (3 consecutive, 5-sec cooldown) | desync detection in netmsg.c | flags u8 (NET_RESYNC_FLAG_*) | Sets g_NetPendingResyncFlags; resync sent next netEndFrame() |
| 0x07 | CLC_COOP_READY | CONTROL reliable | once per co-op mission | client ready to start co-op | nothing | Sets CLFLAG_COOPREADY; when all clients ready: start mission |
| 0x08 | CLC_LOBBY_START | CONTROL reliable | once per match start | lobby leader clicks start | gamemode u8, stagenum u8, difficulty u8, numSims u8, simType u8 | Validates sender is leader (calls lobbyUpdate()), then netServerStageStart() or netServerCoopStageStart() |
| 0x09 | CLC_CATALOG_DIFF | CONTROL reliable | once after SVC_CATALOG_INFO | diff comparison in netDistrib | count u16, net_hash[] u32 array, temporary u8 | Server queues missing components for distribution |

**Dead define — SVC_PLAYER_GUNS (0x21)**: Defined in netmsg.h, not wired into the client receive dispatch, no read/write functions declared. Never sent. Leftover from original N64 net.c. Safe to remove or repurpose.

**numSims/simType not applied (CLC_LOBBY_START)**: These two fields are written by `netmsgClcLobbyStartWrite()` but the server's `netmsgClcLobbyStartRead()` reads them with `netbufReadU8()` and stores them, but they're not used to configure the match setup. The bot count comes from the lobby settings configured before start, not from this message.

---

## 3. Tick / Frame Sync Model

### 3.1 Server Main Loop (`server_main.c`)

```
while (running && !s_ShutdownRequested) {
    SDL_PollEvent() loop         // process GUI events (headless: skipped)
    updaterTick()                // background update check
    netStartFrame()              // ENet service + tick++, process all incoming events/packets
    lobbyUpdate()                // re-derive lobby leader from g_NetClients[] array
    hubTick()                    // hub state machine tick
    netEndFrame()                // send outbound data (positions, resyncs, mod distribution)
    serverGuiFrame()             // render ImGui server GUI (headless: skipped)
    SDL_Delay(16)                // ~62.5 fps target (not 60 Hz exactly)
}
```

**Server tick rate**: `SDL_Delay(16)` gives ~62.5 fps, not 60. `g_NetTick` increments each `netStartFrame()` call. No fixed-step tick; drift accumulates over long sessions. The server update rate multiplier (`g_NetServerUpdateRate = 1`) means broadcasts happen every frame.

**Game loop tick rate** (client): The client runs at whatever frame rate the game loop produces. `g_NetTick` is synced to the server at auth time (SVC_AUTH sends current tick) and increments every `netStartFrame()` call.

### 3.2 netStartFrame()

1. `++g_NetTick` — advance frame counter
2. `enet_host_check_events()` + `enet_host_service()` loop — drain ENet event queue
3. For each event: dispatch to connect/disconnect/receive handler
4. `netbufStartWrite(&g_NetMsg)` — reset unreliable broadcast buffer
5. `netbufStartWrite(&g_NetMsgRel)` — reset reliable broadcast buffer

Single-pass event drain: processes one event per frame (the loop breaks after `polled = true`). If multiple events arrive in one frame, subsequent ones are deferred to the next `netStartFrame()`. This can delay processing on high-churn frames.

### 3.3 netEndFrame()

Server path (NETMODE_SERVER in CLSTATE_GAME):
1. Flush accumulated send buffers (broadcast)
2. For each connected client: record + send player move if changed
3. Each frame: broadcast SVC_CHR_MOVE (bot positions, unreliable)
4. Every 15 frames: broadcast SVC_CHR_STATE (bot full state, reliable)
5. Every 60 frames: broadcast SVC_CHR_SYNC (bot checksum, reliable)
6. Every 120 frames: broadcast SVC_PROP_SYNC (prop checksum, reliable)
7. Co-op every 3 frames: broadcast SVC_NPC_MOVE (NPC positions, unreliable)
8. Co-op every 30 frames: broadcast SVC_NPC_STATE (NPC state, reliable)
9. Co-op every 120 frames: broadcast SVC_NPC_SYNC (NPC checksum, reliable)
10. Every 600 frames: expire stale preserved player slots
11. Consume g_NetPendingResyncFlags: send full resyncs if flagged
12. `netDistribServerTick()` — stream one mod component chunk
13. Flush send buffers again
14. `enet_host_flush()`

Client path (NETMODE_CLIENT in CLSTATE_GAME):
1. Flush accumulated send buffers
2. If tick > 100: record and conditionally send CLC_MOVE (unreliable, or reliable if important input)

### 3.4 Input Replication

**Client → Server (CLC_MOVE)**:
- Sent unreliably when inputs change, reliably when important (fire, reload, select, etc.)
- Format: `outmoveack u32` (ack of last server tick we applied) + full `netplayermove` struct
- Contains: tick, ucmd bitmask, lean/crouch offsets, move speeds, view angles, crosshair pos, weapon select, player world position
- Server stores in `srccl->inmove[0]` ring (last 2 moves)

**Server → Client (SVC_PLAYER_MOVE)**:
- Sent unreliably when player state changes, reliably when server is forcing a position
- Format: `client_id u8` + `outmoveack u32` (ack of last client input we applied) + `netplayermove` + optional rooms array (force-teleport only)
- Client stores in `g_NetClients[id].inmove[0]` and applies to player

**No explicit client-side prediction rollback.** The client position is authoritative locally; server sends corrections via UCMD_FL_FORCEPOS flag. Force ticks are tracked via `cl->forcetick` and cleared when client acks the forced position.

---

## 4. Lobby State Sync

### 4.1 Who's Connected

`lobbyUpdate()` runs each frame on **both** server and client, rebuilding the `g_Lobby.players[]` array by walking `g_NetClients[]`. It's a full reconstruction from the client array — not incremental. Called in:
- `server_main.c` main loop (between `netStartFrame` and `netEndFrame`)
- `server_bridge.c:lobbyGetPlayerCount()` — also calls `lobbyUpdate()` directly for GUI queries
- `netmsgClcLobbyStartRead()` — inline call to ensure fresh leader state before validation

**Consequence**: `lobbyUpdate()` can fire 2-3 times per server frame if the GUI is open (once in the main loop + once per `lobbyGetPlayerInfo()` call from the GUI). This is safe (idempotent) but wasteful.

### 4.2 Player List Population

`lobbyUpdate()` logic:
1. Walk `g_NetClients[0..NET_MAX_CLIENTS-1]`
2. Skip `CLSTATE_DISCONNECTED` slots
3. Skip slot if `cl == g_NetLocalClient` (skips host on listen server; NULL check handles dedicated)
4. Copy name/head/body/team into `g_Lobby.players[count]`
5. Leader election: first client with `state >= CLSTATE_LOBBY` gets leader slot (eager assign)
6. Mark `isLeader` flag after loop completes

### 4.3 Leader Assignment

**Server side**: Pure local computation in `lobbyUpdate()`. First client with `state >= CLSTATE_LOBBY` becomes leader. If that client disconnects, `firstLobbySlot` in the next `lobbyUpdate()` call becomes the new leader.

**Client side**: Identical logic — each client independently runs the same election, producing the same result because the player order in `g_NetClients[]` is deterministic (the server assigns IDs, and SVC_STAGE_START carries the full player manifest).

**Critical gap**: `SVC_LOBBY_LEADER` (0x60) is never sent. The server never explicitly tells clients who the leader is. Both sides derive it locally. This works only because:
1. Clients receive the player manifest at stage start (SVC_STAGE_START)
2. Clients don't need to know the leader before a match starts
3. The lobby UI doesn't yet need accurate leader state client-side (no per-client settings, no kick UI)

When the room architecture (R-3) is implemented with per-room leaders, this will need to change — explicit leader sync via SVC_LOBBY_LEADER or SVC_ROOM_ASSIGN will be required.

### 4.4 Lobby Settings Changes

There is no protocol for the leader to push lobby setting changes (game mode, stage selection) to clients during the lobby phase. Clients only learn the match settings at SVC_STAGE_START. `SVC_LOBBY_STATE` (0x61) is implemented but never sent — it could fill this role but has no callers.

**Impact**: The lobby UI on clients shows stale default settings until the match starts. For current single-room lobby flow this works (client doesn't need to preview settings before joining), but will be a gap once the room architecture allows browsing rooms.

### 4.5 Match Start Propagation

1. Leader client sends `CLC_LOBBY_START` (gamemode, stagenum, difficulty, numSims, simType)
2. Server handler calls `lobbyUpdate()` (inline refresh to handle same-frame auth+start race)
3. Server validates sender's clientId matches the leader slot
4. Server transitions all `CLSTATE_LOBBY` clients to `CLSTATE_GAME`
5. Server calls `netServerStageStart()` or `netServerCoopStageStart()`
6. Both broadcast `SVC_STAGE_START` (reliable, NETCHAN_DEFAULT) to all peers

**Note on numSims/simType**: Read from CLC_LOBBY_START but not used to configure the match — the bot count comes from match setup configuration elsewhere. This is benign dead data at the protocol level.

---

## 5. In-Game State Sync

### 5.1 Player Position / Rotation

- **Frequency**: Every frame when changed (unreliable), or reliable on important inputs (fire, activate, reload) or forced teleports
- **Format**: Full `netplayermove` struct — tick, ucmd, lean, crouch, move speeds, view angles, crosshair pos, weapon select, world position
- **Authority**: Server applies client input to player struct; server sends authoritative position back to all other clients
- **No delta compression**: Full position every time (12 bytes for coord alone)

### 5.2 Shooting / Damage

Shooting is **server-authoritative**:
- Client sends `CLC_MOVE` with `UCMD_FIRE` flag
- Server reads client's incoming move (including crosshair position), processes the hit in the game's bullet/hitscan path
- Server broadcasts `SVC_CHR_DAMAGE` to all clients with hit result
- `SVC_CHR_DISARM` for weapon drop events

Client-side hit sounds/effects are triggered by receiving `SVC_CHR_DAMAGE`. The client never does its own hit detection against other players.

### 5.3 Health / Death

- **Authoritative side**: Server only. `chrDamage()` guarded on client — returns early if `NETMODE_CLIENT`
- **Health sync**: `SVC_CHR_STATE` (bots every 15 frames) and `SVC_PLAYER_STATS` (players on damage events)
- **Death**: `chrDie()` on server → `SVC_PLAYER_STATS` broadcast with death flag → clients play death animation
- **Respawn**: Server calls `playerDie()` on disconnect → schedules respawn via `dostartnewlife = true`

### 5.4 Score / Kills

- **Storage**: `g_PlayerConfigsArray[playernum].base.killcounts[]`, `.numdeaths`, `.numpoints`
- **Sync**: `SVC_PLAYER_SCORES` sent on reconnect and when `NET_RESYNC_FLAG_SCORES` is set
- **Kill attribution**: `chrDamage()` → `chrDie()` → `mpstatsRecordDeath()` on server; synced via SVC_PLAYER_SCORES
- **Live score updates**: No per-kill score broadcast. Clients learn scores from the stats updates or full resync

### 5.5 Bot State

- **Server-authoritative**: `botTickUnpaused()` guarded at top — returns early if `NETMODE_CLIENT`
- **Replication**: SVC_CHR_MOVE (60 Hz position), SVC_CHR_STATE (4 Hz full state), SVC_CHR_SYNC (1 Hz checksum), SVC_CHR_RESYNC (on demand)
- **Weapon model sync**: Clients update bot weapon model in `botTick()` by comparing `aibot->weaponnum` against held weapon — client-side, no extra message needed

### 5.6 NPC State (Co-op)

- **Server-authoritative**: `chraTick()` (NPC AI loop) guarded on co-op clients
- **Replication**: SVC_NPC_MOVE (~20 Hz position), SVC_NPC_STATE (~2 Hz full state), SVC_NPC_SYNC (~0.5 Hz checksum), SVC_NPC_RESYNC (on demand)
- **NPC identification**: `PROPTYPE_CHR && chr->aibot == NULL && not player-linked`
- **NPC broadcast guard**: Checked `g_NetLocalClient->state >= CLSTATE_GAME` to prevent sending during stage load — but `g_NetLocalClient` is NULL on dedicated server, so this guard is always false and the `if (!s_npcBroadcastStarted)` log message may never fire (see §7)

---

## 6. Functions That Don't Sync (Gaps)

### 6.1 Lobby Leader Not Synced Explicitly

`SVC_LOBBY_LEADER` and `SVC_LOBBY_STATE` are fully implemented (read/write/dispatch) but **have zero callers** on the send side. No code ever calls `netmsgSvcLobbyLeaderWrite()` or `netmsgSvcLobbyStateWrite()`. Both sides independently compute the same result locally, which works now but breaks the moment multi-room or explicit leader transfer is needed.

### 6.2 Room State Not Synced to Clients

As documented in join-flow-plan.md §3.2 and §5.2: clients read room state from their local room pool (`roomGetByIndex()`), but `hubTick()` only runs server-side. Clients always see a stale room 0 in LOBBY state. `SVC_ROOM_LIST` (0x75) is planned in room-architecture-plan.md Phase R-3 but not yet implemented.

### 6.3 Inventory Not Synced on Reconnect

`SVC_INVENTORY_SYNC` is listed as a deferred feature in networking.md §Deferred. A reconnecting co-op player loses their weapons. No current message handles this.

### 6.4 Cutscene Camera Not Synced

`SVC_CUTSCENE` only sends a start/stop flag. Clients freeze input but render from their own camera position. Camera synchronization (animation number) is noted as an MVP limitation.

### 6.5 Score Not Updated Live

No per-kill broadcast. Scores are fully synced at match start, reconnect, and explicit resync. Between resyncs, clients only see incremental updates via `SVC_PLAYER_STATS` (which carries health data, not scores). A client watching the scoreboard mid-match may lag behind by up to 5 minutes.

### 6.6 Dynamic NPC Spawning Not Implemented

`SVC_NPC_SPAWN` is listed as a deferred feature. NPC slots are established at stage load; scripted mid-mission NPC spawns aren't replicated to clients.

### 6.7 CLC_LOBBY_START numSims/simType Payload Dropped

The server reads `numSims` and `simType` from the message but doesn't apply them to configure the match. These fields aren't wired into `g_MpSetup.numBots` or similar. Match setup relies on whatever settings were already configured. Low severity for current flow but a latent inconsistency.

### 6.8 SVC_PLAYER_GUNS (0x21) — Dead Define

Defined in `netmsg.h`. Not in the client dispatch switch. No encode/decode functions declared. No callers. Leftover from original N64 code. Safe to remove (just the `#define` line) or repurpose for a future gun state message.

### 6.9 Dead Callback Infrastructure (`net_interface.h` / `net_server_callbacks.c`)

`port/include/net/net_interface.h` declares a clean protocol boundary — 9 callback functions intended as the **only** interface between the networking layer and game code:

```c
void netcb_OnPlayerJoin(int clientId, const char *name);
void netcb_OnPlayerLeave(int clientId, const char *name, int wasInGame);
void netcb_OnPlayerSettingsChanged(int clientId);
void netcb_OnMatchStart(int stageNum, int gameMode);
void netcb_OnMatchEnd(void);
void netcb_OnPlayerDeath(int victimClientId, int attackerClientId, int weaponId);
void netcb_OnPlayerRespawn(int clientId);
void netcb_OnPlayerPosition(int clientId, float x, float y, float z); // noop
void netcb_OnStageChange(int stageNum);
void netcb_OnStageEnd(void);
void netcb_OnChat(int clientId, const char *message);
```

`port/src/net/net_server_callbacks.c` provides implementations — all are logging-only stubs (`sysLogPrintf` calls).

**Critical finding**: A grep across the entire `port/src/` tree shows **zero call sites** for any `netcb_*` function. The interface is declared, implemented, and completely unused. The actual game↔network coupling happens through direct calls to game functions (`mpStartMatch()`, `mainChangeToStage()`, `playerDie()`, etc.) embedded in `netmsg.c`.

**Architectural implication**: This callback system represents either abandoned refactoring work or aspirational architecture that was never wired in. It's dead infrastructure occupying cognitive space in the codebase. There are two valid paths: (a) implement it properly as the true decoupling boundary, routing all game calls through it; (b) delete it to reduce noise. Leaving it as-is is the worst option — it misleads readers into thinking the system is already decoupled when it isn't.

---

## 7. Ordering and Race Conditions

### 7.1 Auth + Lobby-Start Same-Frame Race

**Problem**: If a client sends `CLC_AUTH` and `CLC_LOBBY_START` in the same ENet batch (e.g., a scripted client that immediately starts after connecting), the server's `netStartFrame()` processes both messages before `lobbyUpdate()` runs in the main loop. At the point `CLC_LOBBY_START` is processed, `g_Lobby.leaderSlot` may still be `0xFF` from the previous frame.

**Fix already in place (S55)**: `netmsgClcLobbyStartRead()` calls `lobbyUpdate()` inline before checking the leader. Additionally, `lobbyUpdate()` has eager leader assignment — as soon as the first `CLSTATE_LOBBY` client is seen in the walk, `g_Lobby.leaderSlot` is set immediately (not deferred to the post-loop election).

**Fallback path**: If after the inline `lobbyUpdate()` call, `g_Lobby.leaderSlot` is still `0xFF` (which can happen if the sender is the only client and reached the handler before `lobbyUpdate()` could assign them), `netmsgClcLobbyStartRead()` falls through to a secondary check: if the sending client is the only connected client with `state >= CLSTATE_LOBBY`, they are treated as leader. This handles the edge case of a solo client starting a match immediately on connect.

**Status**: Fixed. The eager-assign + inline-refresh + fallback-solo pattern handles this race.

### 7.2 CLC_SETTINGS Arrives With CLC_AUTH

On connect, the client sends `CLC_AUTH` + `CLC_SETTINGS` in the same reliable packet (`NETCHAN_CONTROL`). Both are processed sequentially in `netServerEvReceive()` in one call. The server reads auth first, sets `srccl->settings` to defaults in `netmsgClcAuthRead()`, then CLC_SETTINGS overwrites with actual values. This ordering is correct as long as both messages are in the same packet.

**Risk**: If the network splits them into separate packets (unlikely for reliable on same channel), the server processes CLC_AUTH with placeholder settings, then CLC_SETTINGS arrives in the next frame. This is handled gracefully — name is from CLC_AUTH, other settings from CLC_SETTINGS. The first frame may have stale settings but the next frame corrects them.

### 7.3 NPC Broadcast Guard Broken on Dedicated Server

In `netEndFrame()` server co-op path:

```c
if (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME) {
    // NPC broadcast code
}
```

On a dedicated server, `g_NetLocalClient == NULL`. This guard evaluates to `false` always, so **NPC broadcasts never run on dedicated server in co-op mode**. This is a critical co-op-over-dedicated bug — all NPC position updates are silently dropped.

The guard was intended to prevent broadcasting during stage load. A better guard would check `g_NetMode == NETMODE_SERVER` (which is always true) and stage state directly, not `g_NetLocalClient`.

### 7.4 Multiple lobbyUpdate() Calls Per Frame

In the main server loop:
1. `lobbyUpdate()` runs between `netStartFrame()` and `netEndFrame()`
2. `lobbyGetPlayerCount()` in `server_bridge.c` calls `lobbyUpdate()` again each time the GUI queries player count
3. `netmsgClcLobbyStartRead()` calls `lobbyUpdate()` on any CLC_LOBBY_START

This is safe (idempotent, no side effects) but unnecessarily expensive. With 32 clients, each call walks 32 slots and rebuilds the entire player array. Consider caching with a dirty flag.

### 7.5 `g_Lobby.inGame` Always False on Dedicated Server

```c
g_Lobby.inGame = (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME) ? 1 : 0;
```

On dedicated server, `g_NetLocalClient == NULL`, so `g_Lobby.inGame` is always 0. Code that checks `g_Lobby.inGame` to determine if a match is running (including `server_gui.cpp`) will see incorrect state. The server GUI compensates with its own checks, but any new code that relies on `g_Lobby.inGame` will be wrong on dedicated.

**Fix**: Use `(g_NetNumClients > 0 && g_NetClients[0].state >= CLSTATE_GAME)` or check `hubGetState()` instead.

### 7.6 Spurious Disconnect on Peer That Never Completed Auth

`netServerEvDisconnect()` is only called when `enet_peer_get_data(ev.peer)` returns a valid client pointer. If a peer connects and disconnects before CLC_AUTH is processed, `enet_peer_get_data()` may return NULL (the client slot was allocated in `netServerEvConnect()` but the data wasn't set yet — actually it is: `enet_peer_set_data(peer, cl)` is called in `netServerEvConnect()`). Looking at the code: `enet_peer_set_data(peer, cl)` IS called, so the NULL path in `netStartFrame()` should rarely fire unless a spurious ENet event comes from a peer that was never assigned.

**Assessment**: The NULL guard is defensive and correct. Low-risk.

### 7.8 Backslash Line-Continuation Anomalies in `netPlayersAllocate()` / `netSyncIdsAllocate()`

In `net.c`, several lines in these two functions begin with `\ ` (a lone backslash followed by a space or text), e.g.:

```c
\ TODO: handle spectators
\ HACK: assumes playernum 0 is always local
```

These are **not** comments — `//` is a comment; `\` at end of a line is a C line-continuation that joins the line with the next. Here the backslash is not at end-of-line (there is trailing content), so the compiler may issue a "stray `\`" warning or silently concatenate identifiers in unexpected ways depending on position. The text after the backslash would be parsed as a continuation of whatever token or expression precedes the line in the source.

In practice, since these appear on lines by themselves (not in the middle of an expression), the compiler most likely warns and treats them as a no-op. But they're not documentation — they're syntactically ambiguous.

**Action**: Replace with proper `/* TODO: ... */` or `// TODO: ...` comments.

### 7.9 Protocol Version Check at TCP Level, Not Application Level

Protocol version is passed as ENet's `data` field at connect time (`enet_host_connect(host, addr, channels, NET_PROTOCOL_VER)`). The server reads this in `netServerEvConnect()` before any message exchange. A version mismatch sends `DISCONNECT_VERSION` immediately. This is correct — incompatible clients are rejected before wasting bandwidth on auth.

---

## 8. Recommendations

Prioritized by risk and impact:

### CRIT-1: NPC Broadcast Never Fires on Dedicated Server (§7.3)
**Bug**: Co-op NPC positions are silently dropped on dedicated server. The guard `g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME` is always false when `g_NetLocalClient == NULL`.
**Fix**: Replace with a stage-active check that doesn't depend on the local client slot:
```c
if (g_NetMode == NETMODE_SERVER && g_StageNum != STAGE_TITLE && g_StageNum != STAGE_CITRAINING) {
```
Or introduce a dedicated `g_NetStageActive` flag set in `netServerStageStart()` and cleared in `netServerStageEnd()`.
**Priority**: Fix before any co-op playtesting on dedicated server.

### CRIT-2: g_Lobby.inGame Always False on Dedicated Server (§7.5)
**Bug**: Server GUI and any code checking `g_Lobby.inGame` to determine match state will see "not in game" always on dedicated server.
**Fix**: The `lobbyUpdate()` line computing `g_Lobby.inGame` needs a dedicated-server-aware path. Check `g_NetNumClients > 0 && any client is CLSTATE_GAME` instead.
**Priority**: Fix as part of R-2 or sooner if server GUI accuracy matters.

### HIGH-1: SVC_LOBBY_LEADER and SVC_LOBBY_STATE Are Dead (§6.1)
Both messages are fully implemented but never sent. The current shared-computation approach works for single-room, but the moment multi-room or explicit leader transfer arrives (Room Architecture phase R-3+), leader state must be explicitly synced.
**Action**: Add callers in `lobbyUpdate()` (server path) when the leader slot changes. Send `SVC_LOBBY_LEADER` to all CLSTATE_LOBBY clients. This is low-cost (single u8 message) and makes the architecture correct for future work.

### HIGH-2: Room State Not Synced to Clients (§6.2)
Clients read stale local room data. Already tracked as J-3 in join-flow-plan.md and R-3 in room-architecture-plan.md.
**Action**: Implement `SVC_ROOM_LIST` (0x75) as planned. No change needed to this document.

### MED-1: lobbyUpdate() Called Multiple Times Per Frame (§7.4)
Wasteful but harmless. With 32-client cap it's negligible, but worth a flag-based cache.
**Action**: Add `g_LobbyDirty = true` in any code that modifies client state; only call `lobbyUpdate()` when dirty in non-critical paths.

### MED-2: Lobby Settings Not Pushed to Clients (§6.4)
`SVC_LOBBY_STATE` is already implemented. Calling it when the leader changes game mode / stage would give clients accurate lobby display and unblock room browsing UI.
**Action**: Call `netmsgSvcLobbyStateWrite()` from the CLC_LOBBY_START handler (or whenever lobby settings change), broadcasting to all CLSTATE_LOBBY clients.

### MED-3: Score Not Updated Live (§6.5)
No per-kill score broadcast. Players watching the scoreboard may see lagged scores.
**Action**: In the kill attribution path (`mpstatsRecordDeath()`), after updating scores, set `g_NetPendingResyncFlags |= NET_RESYNC_FLAG_SCORES`. This piggybacks on the existing resync mechanism without adding a new message.

### LOW-1: CLC_LOBBY_START numSims/simType Payload Not Applied (§6.7)
Dead fields in the message. Wire `numSims` to `g_MpSetup.numBots` (or the bot count equivalent) in `netmsgClcLobbyStartRead()`.

### LOW-2: Single-Pass Event Drain in netStartFrame() (§3.2)
Only one event is processed per frame call (the loop breaks after `polled = true`). High-churn scenarios (many simultaneous connects) could back up.
**Action**: Consider draining all available events by removing the `polled` flag and always processing until `enet_host_check_events` returns ≤0. Low urgency — the current server cap keeps event volume bounded.

### LOW-3: SVC_PLAYER_GUNS (0x21) Dead Define (§6.8)
Remove `#define SVC_PLAYER_GUNS 0x21` from netmsg.h, or repurpose for a future message. Leaving it creates confusion about what's implemented.

### LOW-4: Stale protocol version in networking.md (§2.0)
networking.md documents protocol v20, actual is v21. Already flagged in join-flow-plan.md §5.5.
**Action**: Update networking.md header to v21 when bumping the protocol next time.

### LOW-5: Backslash Anomalies in net.c (§7.8)
Replace `\ TODO:` and `\ HACK:` lines in `netPlayersAllocate()` and `netSyncIdsAllocate()` with proper `// TODO:` comments. Eliminates compiler warnings and removes syntactic ambiguity.

### LOW-6: Stale syncid Lookup Is O(n) (§1.4)
`netSyncIdFind()` does a linear walk of the prop array. Fine for current stage sizes, but worth noting for profiling if large stages with hundreds of interactable props are added.
**Action**: None urgent. If profiling shows cost, a 256-entry hash table indexed by `syncid & 0xFF` would eliminate the hot path.

### ARCH-1: numSims Configuration Path Needs Audit
Bot count for MP matches flows from lobby settings (the leader configures before pressing start), but the CLC_LOBBY_START message carries `numSims/simType` that aren't applied. There should be one authoritative path. Decide: is the match config set on the server before start, or does CLC_LOBBY_START carry the full config? Currently mixed.

### ARCH-2: Dead Callback Interface Should Be Resolved (§6.9)
`net_interface.h` / `net_server_callbacks.c` declare and implement a clean game↔network boundary but have zero callers. The codebase currently couples net and game code directly (game calls embedded in netmsg.c). Decide:
- **Option A (implement it)**: Wire `netcb_*` calls into the actual event sites in netmsg.c. Move the direct game calls (`mpStartMatch()`, `playerDie()`, etc.) into the callback implementations. This is the architecturally clean choice — it decouples the networking layer from game specifics.
- **Option B (delete it)**: Remove `net_interface.h` and `net_server_callbacks.c`. Reduces codebase noise and prevents the false impression of decoupling.
Option A aligns with the long-term plan (dedicated server, multiple game modes, mod support). Option B is valid if that refactor is out of scope for the current roadmap. Leaving it as-is is not recommended.

---

## 9. Message ID Map (Flat Reference)

```
SVC 0x00  BAD             SVC 0x01  NOP
SVC 0x02  AUTH            SVC 0x03  CHAT
SVC 0x10  STAGE_START     SVC 0x11  STAGE_END
SVC 0x20  PLAYER_MOVE     SVC 0x21  PLAYER_GUNS [DEAD]
SVC 0x22  PLAYER_STATS    SVC 0x23  PLAYER_SCORES
SVC 0x30  PROP_MOVE       SVC 0x31  PROP_SPAWN
SVC 0x32  PROP_DAMAGE     SVC 0x33  PROP_PICKUP
SVC 0x34  PROP_USE        SVC 0x35  PROP_DOOR
SVC 0x36  PROP_LIFT       SVC 0x37  PROP_SYNC
SVC 0x38  PROP_RESYNC
SVC 0x42  CHR_DAMAGE      SVC 0x43  CHR_DISARM
SVC 0x44  CHR_MOVE        SVC 0x45  CHR_STATE
SVC 0x46  CHR_SYNC        SVC 0x47  CHR_RESYNC
SVC 0x48  NPC_MOVE        SVC 0x49  NPC_STATE
SVC 0x4A  NPC_SYNC        SVC 0x4B  NPC_RESYNC
SVC 0x50  STAGE_FLAG      SVC 0x51  OBJ_STATUS
SVC 0x52  ALARM           SVC 0x53  CUTSCENE
SVC 0x60  LOBBY_LEADER [BUILT, NOT SENT]
SVC 0x61  LOBBY_STATE  [BUILT, NOT SENT]
SVC 0x70  CATALOG_INFO    SVC 0x71  DISTRIB_BEGIN
SVC 0x72  DISTRIB_CHUNK   SVC 0x73  DISTRIB_END
SVC 0x74  LOBBY_KILL_FEED
SVC 0x75-0x7F: RESERVED (room architecture R-3)

CLC 0x00  BAD             CLC 0x01  NOP
CLC 0x02  AUTH            CLC 0x03  CHAT
CLC 0x04  MOVE            CLC 0x05  SETTINGS
CLC 0x06  RESYNC_REQ      CLC 0x07  COOP_READY
CLC 0x08  LOBBY_START     CLC 0x09  CATALOG_DIFF
CLC 0x0A-0x0F: RESERVED (room architecture R-3/R-4)
```

---

*Last updated: 2026-03-27, Session 57 (second pass: player/syncid allocation, dead callback system, backslash anomalies, fallback leader path)*
