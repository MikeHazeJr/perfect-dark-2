# Network System Audit — Definitive Reference

> Created: 2026-03-27, Session 61+ (deep audit)
> Supersedes: network-audit.md (S57) and netsend-audit.md (S61)
> Sources read directly: net.h, netmsg.h, netlobby.h, netbuf.c, net.c, netmsg.c,
>   netlobby.c, netdistrib.c, server_main.c, server_bridge.c, net_interface.h,
>   net_server_callbacks.c, all context plan files.
> Back to [index](README.md)

---

## SUMMARY (50 lines)

**Protocol**: v27 (as of S130; was v21 when this audit was written), ENet UDP, 3 channels (DEFAULT unreliable/reliable, CONTROL reliable, TRANSFER reliable). **Note**: net_hash removed from wire in S130; all asset identity uses catalog ID strings.
**Architecture**: Server-authoritative, dedicated-only. 32-client cap. 60 Hz tick.
**Messages**: 39 SVC (server→client) + 10 CLC (client→server) defined. 2 SVC dead (PLAYER_GUNS, partially LOBBY_LEADER/STATE).

**Fixed in S61 (all critical bugs from prior audits):**
- CRIT-0: `CLC_RESYNC_REQ` was silently dropped — desync recovery was non-functional. Fixed with `g_NetPendingResyncReqFlags`.
- CRIT-1: NPC broadcasts on dedicated server were always suppressed (`g_NetLocalClient == NULL` guard). Fixed.
- CRIT-2: `g_Lobby.inGame` always false on dedicated server. Fixed: walks `g_NetClients[]` instead.

**Remaining open items (priority order):**
- HIGH-1: `SVC_LOBBY_LEADER` not re-sent when leader disconnects (leader change is silent on client).
- HIGH-2: Room state not synced to clients — SVC_ROOM_LIST (0x75) unimplemented. Clients see stale local room 0.
- ARCH-1: Multi-room multi-stage is architecturally impossible without massive rework (global `g_StageNum`, `g_MpSetup`, `g_Vars`). Simpler model (one active match at a time) is the correct near-term path.
- ARCH-2: Dead callback infrastructure (`net_interface.h` / `net_server_callbacks.c`) — declared, implemented, zero callers. Decision needed: implement or delete.
- MED-1: `lobbyUpdate()` called 2-3× per frame (safe but wasteful).
- MED-2: `SVC_LOBBY_STATE` implemented but never sent — clients never learn lobby settings between auth and match start.
- MED-3: Score not updated live — per-kill broadcast missing.
- MED-4: `netmsgSvcStageStartRead()` playernum swap logic assumes playernum 0 = local player everywhere. Correct for current 2-player co-op but needs audit before expanding to 32-player MP.
- LOW-1: `numSims/simType` in `CLC_LOBBY_START` are read but not applied.
- LOW-2: Single-pass event drain in `netStartFrame()` — one event per frame, can back up under load.
- LOW-3: `SVC_PLAYER_GUNS (0x21)` dead define — no encode/decode, not in dispatch.
- LOW-4: `networking.md` documents protocol v20, actual is v21.
- LOW-5: Backslash `\ ` anomalies in `net.c:netPlayersAllocate()/netSyncIdsAllocate()`.
- LOW-6: `netSyncIdFind()` is O(n) linear walk.

**Security**: No critical exploitable paths found post-Phase-7-10 fixes. Main concerns: string length validation in `CLC_AUTH` fields, no per-IP rate limiting on connections, connect codes offer obscurity not true security.

**Bandwidth estimate** (32-player MP, 32 bots): ~120-180 KB/s server outbound. Primary cost: SVC_CHR_MOVE + SVC_PLAYER_MOVE per frame. Sustainable on any modern server network.

---

## 1. Full Protocol Audit

### 1.1 Protocol Constants

| Constant | Value | Notes |
|----------|-------|-------|
| `NET_PROTOCOL_VER` | 21 | Bumped from 20 (D3R-9) and 21 (B-12 chrslots u32→u64). `networking.md` still says v20 — stale. |
| `NET_MAX_CLIENTS` | 32 | Hard ceiling (ENet peer allocation). Runtime cap is `g_NetMaxClients`. |
| `NETCHAN_DEFAULT` | 0 | Both unreliable (positions) and reliable (state events) traffic |
| `NETCHAN_CONTROL` | 1 | Reliable only: auth, lobby control, catalog info |
| `NETCHAN_TRANSFER` | 2 | Reliable only: mod component distribution (D3R-9) |
| `NET_BUFSIZE` | 1440 | MTU-safe UDP payload size. Reliable buffer is 4× this. |
| `g_NetServerUpdateRate` | 1 | Multiplier — broadcasts happen every frame |

### 1.2 SVC Messages (Server → Client) — Full Catalog

| ID | Name | Channel | Frequency | Sender | Receiver action | Status |
|----|------|---------|-----------|--------|-----------------|--------|
| 0x00 | SVC_BAD | — | never | — | Logs warning, aborts parse | OK |
| 0x01 | SVC_NOP | any | on demand | server | noop | OK |
| 0x02 | SVC_AUTH | CONTROL reli | once per auth | `netmsgClcAuthRead()` | Client moves from temp slot to `g_NetClients[id]`; state→LOBBY; syncs tick | OK |
| 0x03 | SVC_CHAT | DEFAULT reli | on event | `netmsgClcChatRead()` (relay) or `netChatPrintf()` | Logs to `LOG_CHAT` | OK |
| 0x10 | SVC_STAGE_START | DEFAULT reli | once per match | `netServerStageStart()` / `netServerCoopStageStart()` / reconnect path | Client launches stage, transitions all to CLSTATE_GAME; `netPlayersAllocate()` + `netSyncIdsAllocate()` | OK — see §4 for playernum swap note |
| 0x11 | SVC_STAGE_END | DEFAULT reli | once per match end | `netServerStageEnd()` | MP: end match, state→LOBBY. Co-op: endscreen. | OK |
| 0x20 | SVC_PLAYER_MOVE | DEFAULT unrel/reli | per frame when changed | `netEndFrame()` server | Client updates that player's move buffer + lerp interpolation | OK |
| 0x21 | SVC_PLAYER_GUNS | — | **NEVER** | — | **Dead define**. Not in client dispatch. No encode/decode. Leftover from N64. | DEAD |
| 0x22 | SVC_PLAYER_STATS | DEFAULT reli | on damage/death event | player damage path | Updates local player health/death display | OK |
| 0x23 | SVC_PLAYER_SCORES | DEFAULT reli | on reconnect/resync | `NET_RESYNC_FLAG_SCORES` consume | Client updates score display for all players | OK — not sent live per kill (MED-3) |
| 0x30 | SVC_PROP_MOVE | DEFAULT unrel | per update frame | `netEndFrame()` server | Client updates prop transform | OK |
| 0x31 | SVC_PROP_SPAWN | DEFAULT reli | on spawn event | propobj spawn path | Client creates new prop | OK |
| 0x32 | SVC_PROP_DAMAGE | DEFAULT reli | on prop damage | `objDamage()` server guard | Client applies damage to prop | OK |
| 0x33 | SVC_PROP_PICKUP | DEFAULT reli | on pickup event | item pickup server guard | Client removes/pickups item | OK |
| 0x34 | SVC_PROP_USE | DEFAULT reli | on use event | door/lift use server guard | Client triggers interaction | OK |
| 0x35 | SVC_PROP_DOOR | DEFAULT reli | on door state change | door state machine | Client updates door visual state | OK |
| 0x36 | SVC_PROP_LIFT | DEFAULT reli | on lift state change | lift state machine | Client updates lift state | OK |
| 0x37 | SVC_PROP_SYNC | DEFAULT reli | every 120 frames | `netEndFrame()` schedule | Client compares checksum; desyncs trigger `g_NetPendingResyncReqFlags` | Fixed S61 (resync req now actually sent) |
| 0x38 | SVC_PROP_RESYNC | DEFAULT reli | on `NET_RESYNC_FLAG_PROPS` | `netEndFrame()` consume | Client overwrites local prop state | OK |
| 0x42 | SVC_CHR_DAMAGE | DEFAULT reli | on chr hit | `chrDamage()` server guard | Client applies damage to chr | OK |
| 0x43 | SVC_CHR_DISARM | DEFAULT reli | on disarm | disarm code path | Client drops chr weapon | OK |
| 0x44 | SVC_CHR_MOVE | DEFAULT unrel | every update frame (60 Hz) | `netEndFrame()` server bot loop | Client updates bot transform + animation | OK |
| 0x45 | SVC_CHR_STATE | DEFAULT reli | every 15 frames (~4 Hz) | `netEndFrame()` server (% 15) | Client updates bot full state (health, weapon, respawn) | OK |
| 0x46 | SVC_CHR_SYNC | DEFAULT reli | every 60 frames (~1 Hz) | `netEndFrame()` server (% 60) | Client checksums; desyncs set `g_NetPendingResyncReqFlags` | Fixed S61 |
| 0x47 | SVC_CHR_RESYNC | DEFAULT reli | on `NET_RESYNC_FLAG_CHRS` | `netEndFrame()` consume | Client overwrites all bot state | OK |
| 0x48 | SVC_NPC_MOVE | DEFAULT unrel | every 3 frames (~20 Hz) | `netEndFrame()` co-op (% 3) | Client updates NPC transform | Fixed S61 (dedicated server guard) |
| 0x49 | SVC_NPC_STATE | DEFAULT reli | every 30 frames (~2 Hz) | `netEndFrame()` co-op (% 30) | Client updates NPC state (health, flags, alertness) | Fixed S61 |
| 0x4A | SVC_NPC_SYNC | DEFAULT reli | every 120 frames (~0.5 Hz) | `netEndFrame()` co-op (% 120) | Client checksums; desyncs set `g_NetPendingResyncReqFlags` | Fixed S61 |
| 0x4B | SVC_NPC_RESYNC | DEFAULT reli | on `NET_RESYNC_FLAG_NPCS` | `netEndFrame()` consume | Client overwrites NPC state + stage flags + objectives | OK |
| 0x50 | SVC_STAGE_FLAG | DEFAULT reli | on `g_StageFlags` change | `chraction.c` broadcast | Client updates `g_StageFlags` | OK |
| 0x51 | SVC_OBJ_STATUS | DEFAULT reli | on objective change | `objectivesCheckAll()` server | Client updates `g_ObjectiveStatuses[index]` | OK |
| 0x52 | SVC_ALARM | DEFAULT reli | on alarm state change | alarm server guard | Client updates alarm visual/audio | OK |
| 0x53 | SVC_CUTSCENE | DEFAULT reli | on cutscene start/end | cutscene hook player.c | Client freezes/unfreezes input | OK (camera not synced — MVP) |
| 0x60 | SVC_LOBBY_LEADER | CONTROL reli | on every client join/reconnect | `netmsgClcAuthRead()` (S60+) | Client calls `lobbySetLeader()` | Partial: sent on join, NOT on leader change (HIGH-1) |
| 0x61 | SVC_LOBBY_STATE | CONTROL reli | **NEVER** | — | Client would update `g_NetGameMode` + lobby settings | NOT SENT (MED-2) |
| 0x70 | SVC_CATALOG_INFO | CONTROL reli | once after auth | `netDistribServerSendCatalogInfo()` | Client diffs catalog, sends CLC_CATALOG_DIFF | OK |
| 0x71 | SVC_DISTRIB_BEGIN | TRANSFER reli | once per component | `netDistribServerTick()` | Client prepares receive buffer for component | OK |
| 0x72 | SVC_DISTRIB_CHUNK | TRANSFER reli | per 16KB chunk | `netDistribServerTick()` | Client appends to receive buffer | OK |
| 0x73 | SVC_DISTRIB_END | TRANSFER reli | once per component | `netDistribServerTick()` | Client decompresses, extracts, hot-registers mod | OK |
| 0x74 | SVC_LOBBY_KILL_FEED | DEFAULT reli | on kill event | kill feed broadcast | Client appends to kill feed ring buffer (16 entries) | OK |
| 0x75–0x7F | — | — | — | — | RESERVED: room architecture R-3 (SVC_ROOM_LIST, SVC_ROOM_UPDATE, SVC_ROOM_ASSIGN) | NOT YET IMPLEMENTED |

### 1.3 CLC Messages (Client → Server) — Full Catalog

| ID | Name | Channel | Frequency | Trigger | Payload | Server action | Status |
|----|------|---------|-----------|---------|---------|---------------|--------|
| 0x00 | CLC_BAD | — | never | — | — | Aborts parse | OK |
| 0x01 | CLC_NOP | any | on demand | keepalive | nothing | noop | OK |
| 0x02 | CLC_AUTH | CONTROL reli | once per connect | ENet connect event | name, romName, modDir, localPlayers u8 | Validates files, assigns LOBBY, sends SVC_AUTH + SVC_CATALOG_INFO | OK |
| 0x03 | CLC_CHAT | DEFAULT reli | on user input | user presses Enter | message string | Broadcasts SVC_CHAT to all clients | OK |
| 0x04 | CLC_MOVE | DEFAULT (reli if important) | per frame when changed | `netEndFrame()` client | outmoveack u32 + `netplayermove` struct | Server stores in `srccl->inmove[0]`, applies to player | OK |
| 0x05 | CLC_SETTINGS | CONTROL reli | on change + sent with CLC_AUTH | `netClientSettingsChanged()` | options u16, bodynum, headnum, team, fovy, fovzoommult, name | Server updates `srccl->settings`; applies team change if in-game | OK |
| 0x06 | CLC_RESYNC_REQ | DEFAULT reli | on desync (3 consec, 5-sec cooldown) | `g_NetPendingResyncReqFlags` consume | flags u8 (`NET_RESYNC_FLAG_*`) | Server sets `g_NetPendingResyncFlags`; resync sent next `netEndFrame()` | Fixed S61 |
| 0x07 | CLC_COOP_READY | CONTROL reli | once per co-op mission | client ready for co-op | nothing | Sets CLFLAG_COOPREADY; when all clients ready: start mission | OK |
| 0x08 | CLC_LOBBY_START | CONTROL reli | once per match start | lobby leader clicks start | gamemode u8, stagenum u8, difficulty u8, numSims u8, simType u8 | Validates sender is leader (inline `lobbyUpdate()`); calls `netServerStageStart()` or `netServerCoopStageStart()` | OK — numSims/simType not applied (LOW-1) |
| 0x09 | CLC_CATALOG_DIFF | CONTROL reli | once after SVC_CATALOG_INFO | diff comparison in netDistrib | count u16, net_hash[] u32 array, temporary u8 | Server queues missing components for distribution | OK |
| 0x0A–0x0F | — | — | — | — | RESERVED: room architecture R-3/R-4 (CLC_ROOM_JOIN, LEAVE, SETTINGS, KICK, TRANSFER, START) | NOT YET IMPLEMENTED |

### 1.4 Wire Format Notes

**`netplayermove` struct** (CLC_MOVE / SVC_PLAYER_MOVE payload, ~56 bytes):
```
u32  tick
u32  ucmd         (UCMD_* bitmask)
f32  leanofs
f32  crouchofs
f32  zoomfov
f32  movespeed[2]
f32  angles[2]    (theta, verta)
f32  crosspos[2]  (normalized crosshair position)
s8   weaponnum
coord pos         (3× f32 = 12 bytes)
```
Total: ~56 bytes per player move.

**`netbuf` buffer model**:
- `g_NetMsg`: unreliable broadcast, 1440 bytes, reset each `netStartFrame()` after event dispatch
- `g_NetMsgRel`: reliable broadcast, 5760 bytes (4× MTU), reset each `netStartFrame()` after event dispatch
- Per-client `cl->out`: unicast sends (1440 bytes each)
- **Critical**: any write to `g_NetMsg` or `g_NetMsgRel` inside the event dispatch loop in `netStartFrame()` is silently dropped by the post-loop reset (lines 1219-1220). All writes must happen in `netEndFrame()` or use `netbufStartWrite()` + `netSend()` as an atomic pattern.

---

## 2. Connection Lifecycle — Full Trace

### 2.1 Client Path

```
[User enters 4-word connect code]
  connectCodeDecode() → IP:port string (host byte order, NOT htonl)
  netStartClient(addrStr)
    → g_NetMode = NETMODE_CLIENT
    → g_NetLocalClient = &g_NetClients[NET_MAX_CLIENTS]  ← temp slot [32]
    → CLSTATE_CONNECTING
    → enet_host_connect(ip:port, NETCHAN_COUNT=3, NET_PROTOCOL_VER=21)
    → lobbyInit()

[ENET_EVENT_TYPE_CONNECT in netStartFrame()]
  netClientEvConnect()
    → g_NetLocalClient->state = CLSTATE_AUTH
    → Sends CLC_AUTH + CLC_SETTINGS in one packet on NETCHAN_CONTROL (reliable)

[SVC_AUTH arrives]
  netmsgSvcAuthRead()
    → Receives assigned client ID u8, maxclients u8, current tick u32
    → Moves g_NetLocalClient from temp slot [32] to g_NetClients[id]
    → g_NetLocalClient->state = CLSTATE_LOBBY

[SVC_CATALOG_INFO arrives on NETCHAN_CONTROL]
  netmsgSvcCatalogInfoRead()
    → Diffs against local catalog
    → Sends CLC_CATALOG_DIFF on NETCHAN_CONTROL

[SVC_DISTRIB_BEGIN/CHUNK/END stream on NETCHAN_TRANSFER]
  → Client decompresses, extracts mods/.temp/ or mods/{category}/{id}/
  → Hot-registers in Asset Catalog

[In CLSTATE_LOBBY]
  lobbyUpdate() each frame — rebuilds player list from g_NetClients[]
  pdguiLobbyScreenRender() shows player list + room list (stale local snapshot)

[Leader sends CLC_LOBBY_START → server responds SVC_STAGE_START]
  netmsgSvcStageStartRead()
    → syncs tick, RNG seeds, stagenum, mode settings, player manifest
    → netPlayersAllocate() — playernum swap (local always 0)
    → netSyncIdsAllocate() — assigns syncids to all props
    → g_NetLocalClient->state = CLSTATE_GAME
    → calls mpStartMatch() or mainChangeToStage()

[Async: g_MainChangeToStageNum != -1 triggers stage load next game frame]
  → Stage assets load; game is now running
```

### 2.2 Server Path

```
[server_main.c: main()]
  netInit()        → ENet init, parse --port/--maxclients/--bind/--dedicated
  lobbyInit()      → clears g_Lobby, leaderSlot = 0xFF
  hubInit()        → hub state + room pool init
  netStartServer(port, maxclients)
    → enet_host_create on g_NetLocalAddr
    → if g_NetDedicated: g_NetLocalClient = NULL, g_NetNumClients = 0
    → else (listen server): g_NetLocalClient = &g_NetClients[0], state = CLSTATE_LOBBY
    → g_NetMode = NETMODE_SERVER
    → netUpnpSetup(port)   ← async background thread
    → lobbyInit()
    → netDistribInit()

[Main loop: netStartFrame() → lobbyUpdate() → hubTick() → netEndFrame()]
  Server tick rate: SDL_Delay(16) → ~62.5 fps (NOT exactly 60). Drift accumulates.

[ENET_EVENT_TYPE_CONNECT in netStartFrame()]
  netServerEvConnect(peer, data)
    → Validates data == NET_PROTOCOL_VER (21). Mismatch → DISCONNECT_VERSION.
    → Finds free slot (start=0 for dedicated, 1 for listen server)
    → Rejects if full (DISCONNECT_FULL)
    → Rejects if ingame && no preserved slots (DISCONNECT_LATE)
    → cl->state = CLSTATE_AUTH
    → cl->flags = ingame ? CLFLAG_ABSENT : 0
    → enet_peer_set_data(peer, cl)
    → ++g_NetNumClients

[CLC_AUTH + CLC_SETTINGS arrive (same packet, NETCHAN_CONTROL)]
  netmsgClcAuthRead()
    → Validates state == CLSTATE_AUTH
    → On non-dedicated: validates ROM hash + mod dir match
    → netServerFindPreserved() — checks reconnect
    → cl->state = CLSTATE_LOBBY
    → Sends SVC_AUTH on NETCHAN_CONTROL (per-client cl->out buffer)
    → Sends SVC_CATALOG_INFO on NETCHAN_CONTROL
    → Sends SVC_LOBBY_LEADER to all CLSTATE_LOBBY clients (S60+)
    → If reconnecting: sends SVC_STAGE_START, clears CLFLAG_ABSENT, sets g_NetPendingResyncFlags
  netmsgClcSettingsRead() ← same packet, processed next

[CLC_LOBBY_START received from leader]
  netmsgClcLobbyStartRead()
    → inline lobbyUpdate() ← ensures fresh leader state (S55 race fix)
    → Validates sender == leader (clientId match)
    → Calls netServerStageStart() (MP) or netServerCoopStageStart() (co-op)
      → All CLSTATE_LOBBY clients → CLSTATE_GAME
      → Broadcasts SVC_STAGE_START
      → Sets g_NetPendingResyncFlags

[ENET_EVENT_TYPE_DISCONNECT]
  netServerEvDisconnect(cl)
    → If cl was in-game: netServerPreservePlayer() + playerDie()
    → netClientReset(cl) (clears player link)
    → --g_NetNumClients
```

### 2.3 State Machine

```
CLSTATE_DISCONNECTED = 0   slot free
CLSTATE_CONNECTING   = 1   client only: ENet connect in flight
CLSTATE_AUTH         = 2   server: waiting for CLC_AUTH; client: after connect event
CLSTATE_LOBBY        = 3   authenticated, waiting for match
CLSTATE_GAME         = 4   in active match
```

**Key transitions and their triggers:**

| Transition | Trigger | Side |
|------------|---------|------|
| → AUTH | `netServerEvConnect()` / `netClientEvConnect()` | Both |
| AUTH → LOBBY | `netmsgClcAuthRead()` validates auth | Server; client on `netmsgSvcAuthRead()` |
| LOBBY → GAME | `netServerStageStart()` (batch) | Server; client on `SVC_STAGE_START` |
| GAME → LOBBY | `netServerStageEnd()` | Server; client on `SVC_STAGE_END` |
| Any → DISCONNECTED | `netServerEvDisconnect()` / `netClientEvDisconnect()` | Both |

**Server never sees CLSTATE_CONNECTING.** Server skips straight to AUTH on ENet connect event.

### 2.4 Match Start: Player Number and SyncID Allocation

**`netPlayersAllocate()`** (called on `SVC_STAGE_START` receive):
- Server: assigns playernum sequentially 0..N to all CLSTATE_LOBBY+ clients
- Client: local client always wants to be playernum 0 (game hardcodes "local = 0")
- If client's assigned playernum ≠ 0: swaps `g_PlayerConfigsArray` entries for its slot and slot 0
- After swap: client references itself as playernum 0 everywhere

**`netSyncIdsAllocate()`**:
- Assigns `prop->syncid = prop_array_index + 1` (0 reserved/invalid)
- Deterministic given same stage + prop layout
- Client: if playernum swap happened, also swaps syncids for affected props
- `netSyncIdFind(id)` is O(n) linear walk (LOW-6)

**⚠️ Note**: The playernum swap in `netPlayersAllocate()` assumes only 2 players (server=0, client=1 in co-op). The swap logic needs audit before extending to 32-player MP where clients may have any playernum 0-31.

---

## 3. Tick / Frame Sync Model

### 3.1 Server Main Loop

```
while (running) {
    SDL_PollEvent()           // GUI events
    updaterTick()             // background update check
    netStartFrame()           // ENet service + ++g_NetTick + event dispatch
    lobbyUpdate()             // re-derive leader from g_NetClients[]
    hubTick()                 // hub state machine tick
    netEndFrame()             // broadcast game state, mod distribution
    serverGuiFrame()          // render ImGui server GUI (headless: skipped)
    SDL_Delay(16)             // ~62.5 fps (NOT 60 Hz exactly — drift accumulates)
}
```

**Tick rate drift**: `g_NetTick` increments in `netStartFrame()` once per call. At `SDL_Delay(16)`, actual rate is ~62.5/s, not 60. Over a 1-hour session: ~14,400 extra ticks. This affects the % 15 / % 60 / % 120 / % 600 schedules in `netEndFrame()` — they run at 4.2/4.2/2.1/0.1 Hz instead of 4/4/2/0.1 Hz. Harmless for current sync frequencies, worth noting for any future time-critical sync.

### 3.2 netStartFrame()

1. `++g_NetTick`
2. `enet_host_check_events()` + `enet_host_service()` loop
3. **Single event per frame** — the loop sets `polled = true` after one event and breaks. Multiple simultaneous events back up to next frame (LOW-2).
4. For each event: connect → `netServerEvConnect()`/`netClientEvConnect()`, disconnect → ev handlers, receive → `netServerEvReceive()`/`netClientEvReceive()`
5. **Post-loop**: `netbufStartWrite(&g_NetMsg)` + `netbufStartWrite(&g_NetMsgRel)` — RESETS BOTH BUFFERS.

**The reset at step 5 is the root of the old CRIT-0 bug (now fixed).** Any write to `g_NetMsg`/`g_NetMsgRel` inside the receive handlers fires BEFORE the reset. So they were silently wiped.

### 3.3 netEndFrame()

```
netFlushSendBuffers()           // flush whatever accumulated before this point
if CLSTATE_GAME:
  if CLIENT: record CLC_MOVE + send
             consume g_NetPendingResyncReqFlags → write CLC_RESYNC_REQ (fixed S61)
  if SERVER: record SVC_PLAYER_MOVE per client
             every frame: SVC_CHR_MOVE (bots, unreliable)
             % 15: SVC_CHR_STATE (bots, reliable)
             % 60: SVC_CHR_SYNC (bot checksum, reliable)
             % 120: SVC_PROP_SYNC (prop checksum, reliable)
             if co-op:
               % 3: SVC_NPC_MOVE (NPCs, unreliable)   — fixed S61 guard
               % 30: SVC_NPC_STATE (NPCs, reliable)
               % 120: SVC_NPC_SYNC (NPC checksum, reliable)
             % 600: expire preserved player timeout
             consume g_NetPendingResyncFlags → full resyncs

netDistribServerTick()          // stream one mod component chunk
netFlushSendBuffers()           // final flush
enet_host_flush()
```

### 3.4 Input Replication

**Client → Server (CLC_MOVE)**:
- Unreliable by default; reliable when `UCMD_IMPORTANT_MASK` bits change (fire, reload, select, etc.)
- Contains full `netplayermove` (~56 bytes): tick, ucmd, lean, crouch, angles, crosshair, weapon, position
- Server stores in `srccl->inmove[0]` ring (last 2 moves)

**Server → Client (SVC_PLAYER_MOVE)**:
- Unreliable by default; reliable when forcing a position
- Contains `client_id` u8 + `outmoveack` u32 + full `netplayermove` + optional rooms (force only)
- Client applies to that player's inmove buffer + lerp interpolation
- `g_NetInterpTicks = 3` interpolation buffer

**No explicit rollback.** Client position is authoritative locally; server sends corrections via `UCMD_FL_FORCEPOS` flag. `cl->forcetick` tracks when force was issued; cleared when client acks it.

---

## 4. Lobby / Room / Player Sync

### 4.1 Current Lobby Model (Pre-Room Architecture)

`lobbyUpdate()` runs each frame rebuilding `g_Lobby.players[]` from `g_NetClients[]`:
1. Walk `g_NetClients[0..31]`
2. Skip DISCONNECTED slots
3. Skip `cl == g_NetLocalClient` (listen server host skip; NULL = no skip on dedicated)
4. Skip if `g_NetMode != NETMODE_CLIENT && cl == g_NetLocalClient`
5. Copy name/head/body/team into `g_Lobby.players[count]`
6. Leader: first client with `state >= CLSTATE_LOBBY` — eager assign (fixes same-frame auth+start race)
7. After loop: elect new leader if current leader disconnected

`lobbyUpdate()` call sites per server frame (MED-1):
- `server_main.c:` main loop (once)
- `server_bridge.c:lobbyGetPlayerCount()` (once per GUI query)
- `netmsgClcAuthRead()` (on any new join — intentional, S55 race fix)
- `netmsgClcLobbyStartRead()` (on start — intentional, S55 race fix)

### 4.2 Leader Assignment

**Server side**: First `CLSTATE_LOBBY` client in `g_NetClients[]` array order becomes leader. Pure local computation, no explicit message — until S60+.

**S60+ update**: `SVC_LOBBY_LEADER` IS now sent on every client join/reconnect in `netmsgClcAuthRead()`. Clients call `lobbySetLeader()` on receive.

**Remaining gap (HIGH-1)**: When the leader **disconnects**, `lobbyUpdate()` elects a new leader locally (both server and clients independently compute the same result), but `SVC_LOBBY_LEADER` is NOT re-broadcast to inform clients. Currently benign (both sides compute same), but breaks when multi-room or explicit leader transfer is needed.

**Fix**: `lobbyUpdate()` should detect when `g_Lobby.leaderSlot` changes and broadcast `SVC_LOBBY_LEADER` to all `CLSTATE_LOBBY` clients.

### 4.3 Room State Sync Gap (HIGH-2)

`hubTick()` runs server-side only. Clients read room state from `roomGetByIndex()` reading their local (stale) room pool. `SVC_ROOM_LIST (0x75)` is defined in room-architecture-plan.md Phase R-3 but does not exist in the codebase.

**Clients currently see**: room 0 in LOBBY state, always. No accurate room information from server.

**Fix**: Implement R-3 protocol (SVC_ROOM_LIST, SVC_ROOM_UPDATE, SVC_ROOM_ASSIGN). Tracked in room-architecture-plan.md.

### 4.4 Match Start Propagation

1. Leader sends `CLC_LOBBY_START` (gamemode, stagenum, difficulty, numSims, simType)
2. Server handler: inline `lobbyUpdate()`, validate sender clientId == leaderSlot
3. Server transitions all `CLSTATE_LOBBY` → `CLSTATE_GAME`
4. `netServerStageStart()`: broadcasts `SVC_STAGE_START` (reliable, DEFAULT channel) to all peers
5. On client: `SVC_STAGE_START` triggers `netPlayersAllocate()` + `netSyncIdsAllocate()` + stage load

`netSend(NULL, ...)` broadcasts to all ENet peers. After room architecture, this must become room-scoped (only send to clients in the same room).

---

## 5. Bot Sync

### 5.1 Authority Model

`botTickUnpaused()` is guarded at top — returns early if `NETMODE_CLIENT`. All bot AI runs server-only.

### 5.2 Replication

| Message | Frequency | Content |
|---------|-----------|---------|
| SVC_CHR_MOVE (0x44) | Every frame (60 Hz), unreliable | Position, facing angle, rooms, speed, action state |
| SVC_CHR_STATE (0x45) | Every 15 frames (~4 Hz), reliable | Health, shield, weapon, ammo, team, blur, fade, respawn flags |
| SVC_CHR_SYNC (0x46) | Every 60 frames (~1 Hz), reliable | Rolling XOR checksum of all bots |
| SVC_CHR_RESYNC (0x47) | On `NET_RESYNC_FLAG_CHRS`, reliable | Full state dump of all bots |

### 5.3 Bot Names and Configuration

Bot names (difficulty, character) are part of `g_MpBotChrPtrs[]` which is initialized from the match setup (`g_MpSetup`) on match start. Bot configuration is NOT individually synced — the full match setup is embedded in `SVC_STAGE_START`. Clients reconstruct bot roster deterministically from the same RNG seed and match config.

**Bot name sync path**: `netmsgSvcStageStartWrite()` includes the full `g_MpSetup` (including `numBots`, `botnames[]`, `chrslots u64`, `scenario`, etc.). Clients recreate the exact same bot roster locally.

### 5.4 Weapon Model Sync

Client-side only, no extra message: `botTick()` compares `aibot->weaponnum` against held weapon, creates new model on mismatch. This is a client-side correction, not a separate message.

### 5.5 Desync Detection and Recovery

- Detect: SVC_CHR_SYNC (1 Hz) sends rolling XOR checksum of all bot positions/health
- Escalation: 3 consecutive desyncs with 5-second cooldown → set `g_NetPendingResyncReqFlags |= NET_RESYNC_FLAG_CHRS`
- Send: `netEndFrame()` consumes flags, writes `CLC_RESYNC_REQ` to `g_NetMsgRel` (fixed S61 — was silently dropped)
- Server receives CLC_RESYNC_REQ → sets `g_NetPendingResyncFlags` → next `netEndFrame()` sends SVC_CHR_RESYNC

---

## 6. Multi-Room Architecture Analysis

### 6.1 What Exists

The hub/room system (`hub.c`, `room.c`) manages room structs server-side. Room data:
- `hub_room_t`: id, state, name, clients[], client_count, max_players, stagenum, scenario, rng_seed, access, creator_client_id
- Room states: LOBBY → LOADING → MATCH → POSTGAME → CLOSED
- Constants: `HUB_MAX_ROOMS = 4` (stale, plan says expand to 16), `HUB_MAX_CLIENTS = 8` (stale, must expand to 32)

What doesn't exist:
- `leader_client_id` field in `hub_room_t` (planned R-2)
- `room_id` field on `struct netclient` (planned R-2)
- Any protocol messages for rooms (SVC_ROOM_LIST/UPDATE/ASSIGN, CLC_ROOM_JOIN/LEAVE etc.)
- Any mechanism to scope `SVC_STAGE_START` to a room (currently broadcasts to all peers)
- `hubOnClientConnect()` / `hubOnClientDisconnect()` hooks

### 6.2 Can the Engine Run Two Stages Simultaneously?

**No. The following globals make it impossible:**

| Global | Issue |
|--------|-------|
| `g_StageNum` | Single global stage number. Cannot be "Complex (Room A)" and "dataDyne (Room B)" simultaneously. |
| `g_MpSetup` | Single global match setup (scenario, bot count, weapon set, etc.). |
| `g_Vars` | Global game variables: `activeprops`, `pausedprops`, `props[]`, `players[]`, `mplayerisrunning`, etc. |
| `g_BotCount`, `g_MpBotChrPtrs[]` | Global bot array — one match's worth of bots. |
| `g_ChrSlots[]`, `g_NumChrSlots` | Global NPC slot array (co-op). |
| `g_StageFlags`, `g_ObjectiveStatuses[]` | Global co-op mission state. |
| `g_MissionConfig` | Global co-op mission config (stagenum, difficulty, iscoop, isanti). |
| Memory pools | `mempAlloc(MEMPOOL_STAGE)` is stage-global; loading a second stage would collide. |

Making multi-room multi-stage work would require virtualizing ALL of these into per-room structures — a multi-month architectural overhaul equivalent to rewriting the game engine core.

### 6.3 Recommended Model

**One active match at a time. Other rooms wait in lobby.**

This matches how most multiplayer games work. Implementation:
- Server supports N lobby rooms (players gather, configure settings, chat)
- When a room starts a match: the server loads that stage globally
- Other rooms remain in LOBBY state and cannot start until the current match ends
- After the match (SVC_STAGE_END), server returns to the lobby stage; other rooms can now start

This is consistent with the current architecture and requires no engine changes. It simplifies the room architecture considerably — `SVC_STAGE_START` is still broadcast-to-room but with only one room in MATCH state at a time.

**Future path to true multi-room multi-stage** would require per-room game-state isolation — this is Phase 3+ scope (multiplayer-plan.md §5.4).

### 6.4 Mod Isolation Across Rooms

With one-match-at-a-time model, mod isolation is trivial — the active room's mod set loads, match runs, mods unload on stage end.

With true multi-room multi-stage (not recommended near-term), per-room asset catalogs and isolated mod sets would be needed. `netdistrib.c` would need to track which mods are needed by which room and serve different mod sets to different client subsets.

---

## 7. Mod / Asset Distribution

### 7.1 Architecture

`netdistrib.c` (~1100 lines) implements server-side distribution and client-side reception.

**Server flow**:
- `netDistribInit()` — initializes queue and receive slots
- On auth: `netDistribServerSendCatalogInfo()` → sends SVC_CATALOG_INFO (list of non-bundled enabled entries)
- On `CLC_CATALOG_DIFF`: `netDistribServerHandleDiff()` → queues missing components
- `netDistribServerTick()` (called every `netEndFrame()`) → streams one chunk of next queued component

**Client flow**:
- Receives SVC_CATALOG_INFO → diffs vs local → sends CLC_CATALOG_DIFF
- Receives SVC_DISTRIB_BEGIN → prepares receive slot
- Receives SVC_DISTRIB_CHUNK → accumulates compressed data
- Receives SVC_DISTRIB_END → zlib decompresses → PDCA archive extract → hot-registers in Asset Catalog

**PDCA archive format**:
```
u32  magic       0x41434450 ("PDCA")
u16  file_count
[×file_count]:
  u16 path_len
  char path[]
  u32 data_len
  u8  data[]
```

### 7.2 Per-Room Mod Handling (Current State)

Currently single-server-single-mod-set. The catalog sent via SVC_CATALOG_INFO is the server's global enabled mod list. All clients receive the same catalog. No per-room mod isolation exists.

The `CLC_CATALOG_DIFF` message includes a `temporary` flag — client can request session-only install (to `mods/.temp/`) vs permanent (to `mods/{category}/{id}/`). Crash recovery via `.crash_state` file tracks session mods for cleanup on crash.

---

## 8. Performance Analysis

### 8.1 Bandwidth Profile (32-player MP, 32 bots, ~60 Hz)

**Server outbound (per frame, ~60 Hz)**:

| Traffic | Size estimate | Rate |
|---------|---------------|------|
| SVC_CHR_MOVE × 32 bots (unreliable) | ~32 × 28 bytes ≈ 900 bytes/frame | ~54 KB/s |
| SVC_PLAYER_MOVE × 32 players (unreliable) | ~32 × 60 bytes ≈ 1,920 bytes/frame | ~115 KB/s |
| SVC_CHR_STATE × 32 bots (every 15 frames, reliable) | ~32 × 32 bytes / 15 ≈ 68 bytes/frame avg | ~4 KB/s |
| SVC_CHR_SYNC (every 60 frames) | ~20 bytes / 60 ≈ negligible | < 1 KB/s |
| SVC_PROP_SYNC (every 120 frames) | ~20 bytes / 120 ≈ negligible | < 1 KB/s |
| **Total server outbound** | ~2,900 bytes/frame average | **~175 KB/s** |

**Per-client inbound (server perspective)**:

| Traffic | Size | Rate |
|---------|------|------|
| CLC_MOVE per player per frame | ~60 bytes/frame | ~3.6 KB/s per client |
| × 32 clients | ~1,920 bytes/frame | ~115 KB/s total inbound |

**Total estimated**: ~290 KB/s full duplex at 32 players + 32 bots. Well within any modern server's bandwidth budget (typically 10-100 Mbit/s).

### 8.2 CPU Profile

- `lobbyUpdate()` called 2-3× per frame, walks 32 slots each time: O(32×3) = ~96 operations/frame. Negligible.
- `netSyncIdFind()`: O(n) per lookup where n = number of props in stage. PD stages have ~50-200 props. Likely ~500ns per lookup. Acceptable for current scale.
- Mod distribution: `netDistribServerTick()` sends one chunk (16KB) per frame. At 60 fps, this is ~960 KB/s per client download — fast enough for typical mod sizes (< 10MB).

### 8.3 Most Expensive Per-Frame Operations

1. **SVC_CHR_MOVE per bot** — iterates `g_BotCount` (up to 32) every frame. The bot array walk and write are O(n).
2. **SVC_PLAYER_MOVE per client** — iterates `g_NetMaxClients` (32) every frame, recording and conditionally sending moves.
3. **`netFlushSendBuffers()` called twice** — two ENet broadcast calls per frame (one before, one after per-frame writes in `netEndFrame()`).

---

## 9. Logging Audit

### 9.1 Positive Patterns

- Connection events use `LOG_NOTE | LOGFLAG_NOCON` — don't spam the in-game console, only the log file.
- `netServerEvConnect()` and `netServerEvDisconnect()` log client index, not raw IP.
- Resync events (chr resync, NPC resync, prop resync) log with counts — useful for monitoring.

### 9.2 Remaining Raw IPs in Log Output

- `net.c:netStartServer()` calls `netUpnpSetup(port)` which may log the public IP internally (in `netupnp.c` — not audited directly). The UPnP logs are infrastructure-only, not player-facing, so this is acceptable.
- `server_gui.cpp:695`: **B-29 — raw IP in server GUI status bar**. Remove (planned R-1). Line: `ImGui::TextColored(..., "%s:%u", ip, g_NetServerPort)`.

### 9.3 Log Spam

No per-frame log spam observed. All periodic messages use `LOGFLAG_NOCON` or are gated behind event triggers. The `s_npcBroadcastStarted` flag in `netEndFrame()` correctly fires the NPC startup log only once.

---

## 10. Security Surface

### 10.1 Protocol Version Gate

Version check at ENet connect level — `data == NET_PROTOCOL_VER` before any message exchange. Incompatible clients are rejected before any auth packet is processed. **Correct and sufficient.**

### 10.2 Auth Validation

- On non-dedicated: server validates ROM hash + mod directory match. Prevents clients with different game files from joining.
- On dedicated (`!g_NetDedicated` guard): ROM check is skipped — dedicated servers have no ROM. Correct.

### 10.3 Leader Impersonation

`CLC_LOBBY_START` validates that the sending client's index matches `g_Lobby.leaderSlot`. The inline `lobbyUpdate()` call ensures fresh state. A non-leader client sending `CLC_LOBBY_START` is rejected (the comparison fails). **Cannot be bypassed** without controlling the first-connected-client slot.

### 10.4 Message Forgery

ENet peer → `netclient` mapping is maintained server-side via `enet_peer_get_data(ev.peer)`. A client cannot forge messages from another client because message routing uses the peer's attached client pointer, not a client ID carried in the packet. **Not forgeable in the current model.**

### 10.5 Buffer Safety

From Phase 7-10 audits (all fixed):
- `netbuf.c`: buffer overflow sets `buf->error = 1` + no-ops subsequent reads/writes, no `__builtin_trap()`. Graceful.
- Weapon number bounds checks in 6 message handlers.
- Playernum bounds checks in preserve/restore paths.
- `SVC_STAGE_START` playernum validation.
- `setCurrentPlayerNum()` bounds guards (2 locations).
- `ownerplayernum` bitshift overflow fixed.
- `g_NetNumClients` underflow guard on NULL peer disconnect.

**Current concern**: `CLC_AUTH` fields (`name`, `romName`, `modDir`) — these are read with `netbufReadStr()`. If `netbufReadStr()` has a length cap, this is safe. If not, a malicious client could write arbitrary data before the null terminator. **Recommend auditing `netbufReadStr()` to confirm it caps at a safe length.**

### 10.6 Connect Code Security

4-word sentence codes (256 words × 4 slots = 32-bit IPv4) offer **obscurity, not security**:
- Codes are not secret — any player with the code can join.
- Brute-forcing: 256⁴ = ~4 billion combinations. Brute-force via network is rate-limited by ENet connection overhead (~100ms per attempt). Roughly 10 connection attempts/second → 13 years to brute-force. **Not a practical attack vector.**
- No application-layer rate limiting exists, but ENet's per-peer throttling and the OS firewall provide implicit defense.

### 10.7 Denial of Service

- A malicious client sending CLC_LOBBY_START repeatedly: the handler validates leader status and rejects non-leaders. Harmless.
- A malicious client sending malformed packets: `netbuf` graceful error handling prevents crashes. The server logs a warning and discards.
- Mass connection spam: ENet limits concurrent peers to `maxclients`. Once full, new connections are rejected at `netServerEvConnect()` with `DISCONNECT_FULL` before any auth processing.
- A malicious client occupying a slot forever: no idle timeout exists. A client that authenticates and goes silent holds a slot indefinitely. **Recommend adding an auth timeout** — if a client stays in CLSTATE_AUTH for > 10 seconds, kick them.

---

## 11. Recommendations (Updated — Post-S61)

All CRIT items from network-audit.md §8 are fixed. Open items:

### HIGH-1: SVC_LOBBY_LEADER Not Re-Sent on Leader Change
**Problem**: When the leader disconnects, both sides re-elect locally (same result), but `SVC_LOBBY_LEADER` is not broadcast to inform clients. Currently benign, breaks with room architecture.
**Fix**: In `lobbyUpdate()` server path, detect when `g_Lobby.leaderSlot` changes vs. previous value. Broadcast `SVC_LOBBY_LEADER` to all `CLSTATE_LOBBY` clients on change.
**Impact**: Unblocks R-3 (explicit leader sync for per-room rooms).

### HIGH-2: Room State Not Synced to Clients
**Problem**: Clients read stale local room 0. SVC_ROOM_LIST (0x75) unimplemented.
**Fix**: Implement room-architecture-plan.md Phase R-3. Tracked there.

### ARCH-1: Multi-Room Multi-Stage is Architecturally Infeasible Near-Term
**Finding**: `g_StageNum`, `g_MpSetup`, `g_Vars`, bot arrays, prop array, NPC arrays, memory pools — all global. Cannot run two stages simultaneously.
**Decision needed**: Adopt one-active-match-at-a-time model (recommended) or plan a multi-year engine virtualization effort.
**Recommended action**: Document the one-match-at-a-time constraint in constraints.md and room-architecture-plan.md. Adjust R-4 room start logic accordingly.

### ARCH-2: Dead Callback Interface
**Problem**: `net_interface.h` / `net_server_callbacks.c` declare and implement 10 callback functions with zero callers. Creates false impression of decoupling.
**Fix**: Either (A) wire `netcb_*` functions at actual event sites in netmsg.c — clean architectural boundary; or (B) delete both files. Option A is preferred for long-term server extensibility.

### MED-1: lobbyUpdate() Called 2-3× Per Frame
**Problem**: Wasteful but harmless. Each call is O(32) slot walk.
**Fix**: Add `g_LobbyDirty` flag. Set it in any code that modifies client state. Only call `lobbyUpdate()` when dirty in non-critical GUI paths.

### MED-2: SVC_LOBBY_STATE Never Sent
**Problem**: Clients never learn lobby settings between auth and match start.
**Fix**: Call `netmsgSvcLobbyStateWrite()` from `CLC_LOBBY_START` handler and when lobby settings change. Unblocks room-browsing UI.

### MED-3: Score Not Updated Live Per Kill
**Problem**: Clients watching the scoreboard may lag by minutes.
**Fix**: In `mpstatsRecordDeath()`, set `g_NetPendingResyncFlags |= NET_RESYNC_FLAG_SCORES`. Uses existing resync mechanism.

### MED-4: Auth Timeout Missing
**Problem**: A client in CLSTATE_AUTH forever occupies a slot.
**Fix**: Track `cl->auth_tick` when slot transitions to AUTH. In `netEndFrame()` (or the 600-frame tick), kick clients that have been in AUTH for > 10 seconds.

### LOW-1: numSims/simType Not Applied from CLC_LOBBY_START
Wire `numSims` → `g_MpSetup.numBots` in `netmsgClcLobbyStartRead()`. Currently dead payload.

### LOW-2: Single-Pass Event Drain
Remove `polled = true` break in `netStartFrame()` event loop. Process all available events per frame. Low urgency — bounded by 32-client cap.

### LOW-3: SVC_PLAYER_GUNS (0x21) Dead Define
Remove `#define SVC_PLAYER_GUNS 0x21` from netmsg.h, or repurpose for future gun state sync.

### LOW-4: networking.md Protocol Version Stale
Update `networking.md` header from v20 to v21.

### LOW-5: Backslash Anomalies in net.c
Replace `\ TODO:` / `\ HACK:` lines in `netPlayersAllocate()` and `netSyncIdsAllocate()` with `// TODO:` comments.

### LOW-6: netSyncIdFind() O(n) Lookup
Profile before fixing. If large stages show cost, a 256-entry hash table (indexed by `syncid & 0xFF`) eliminates the hot path.

---

## 12. Message ID Map (Flat Reference)

```
SVC 0x00  BAD             SVC 0x01  NOP
SVC 0x02  AUTH            SVC 0x03  CHAT
SVC 0x10  STAGE_START     SVC 0x11  STAGE_END
SVC 0x20  PLAYER_MOVE     SVC 0x21  PLAYER_GUNS [DEAD — no encode/decode, not in dispatch]
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
SVC 0x60  LOBBY_LEADER    [sent on join; NOT on leader change — HIGH-1]
SVC 0x61  LOBBY_STATE     [BUILT, NEVER SENT — MED-2]
SVC 0x70  CATALOG_INFO    SVC 0x71  DISTRIB_BEGIN
SVC 0x72  DISTRIB_CHUNK   SVC 0x73  DISTRIB_END
SVC 0x74  LOBBY_KILL_FEED
SVC 0x75  ROOM_LIST       [RESERVED — R-3, not implemented]
SVC 0x76  ROOM_UPDATE     [RESERVED — R-3, not implemented]
SVC 0x77  ROOM_ASSIGN     [RESERVED — R-3, not implemented]
SVC 0x78-0x7F: available

CLC 0x00  BAD             CLC 0x01  NOP
CLC 0x02  AUTH            CLC 0x03  CHAT
CLC 0x04  MOVE            CLC 0x05  SETTINGS
CLC 0x06  RESYNC_REQ      CLC 0x07  COOP_READY
CLC 0x08  LOBBY_START     CLC 0x09  CATALOG_DIFF
CLC 0x0A  ROOM_JOIN       [RESERVED — R-3, not implemented]
CLC 0x0B  ROOM_LEAVE      [RESERVED — R-3, not implemented]
CLC 0x0C  ROOM_SETTINGS   [RESERVED — R-4, not implemented]
CLC 0x0D  ROOM_KICK       [RESERVED — R-4, not implemented]
CLC 0x0E  ROOM_TRANSFER   [RESERVED — R-4, not implemented]
CLC 0x0F  ROOM_START      [RESERVED — R-4, not implemented]
CLC 0x10+: available
```

---

## 13. Cross-References

| Topic | File |
|-------|------|
| S61 netSend audit (all write sites verified) | [netsend-audit.md](netsend-audit.md) |
| Room architecture implementation plan (R-1–R-5) | [room-architecture-plan.md](room-architecture-plan.md) |
| Client lobby UX flow (L-series phases) | [lobby-flow-plan.md](lobby-flow-plan.md) |
| Server-as-hub vision, room types, federation | [multiplayer-plan.md](multiplayer-plan.md) |
| Join flow audit, connect code, SVC_ROOM_LIST gap | [join-flow-plan.md](join-flow-plan.md) |
| Mod distribution (D3R-9) | [component-mod-architecture.md](component-mod-architecture.md) |
| Asset catalog as single load gateway | [catalog-loading-plan.md](catalog-loading-plan.md) |
| Phase status (D1–D16, SPF, B-12, R-series) | [infrastructure.md](infrastructure.md) |
| ENet protocol, message list, damage authority | [networking.md](networking.md) |
| Active/removed constraints | [constraints.md](constraints.md) |

---

*Created: 2026-03-27, Session 61+ (post S61 fixes, deep source audit)*
*Supersedes: network-audit.md (S57), netsend-audit.md (S61)*
