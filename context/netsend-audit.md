# netSend Audit — Message Write Sites

> Created: 2026-03-27, Session 61 (affectionate-hopper)
> Scope: Every netmsgXxxWrite / netbufStartWrite / ->out / g_NetMsg / g_NetMsgRel call site.
> Goal: Verify every message write reaches a netSend. Find polling patterns that should be event-driven.
> Sources: net.c, netmsg.c, netlobby.c, netmenu.c, netdistrib.c, pdgui_bridge.c — all read directly.
> Back to [index](README.md)

---

## 1. Write-site Catalog

Every place a message is written, grouped by buffer type.

### 1.1 Per-client `->out` buffer (immediate send pattern)

These use `netbufStartWrite(&cl->out)` + write + `netSend(cl, NULL, ...)`.
Per-client buffers are only used by the server when it needs to send unicast to one client.

| Call site | Message | Has netSend? | Channel | Notes |
|-----------|---------|--------------|---------|-------|
| `netmsg.c:284-286` | SVC_AUTH | ✅ yes | CONTROL reliable | In `netmsgClcAuthRead()` — server sends auth response after CLC_AUTH |
| `netmsg.c:298-300` | SVC_STAGE_START | ✅ yes | DEFAULT reliable | In `netmsgClcAuthRead()` reconnect path — server resends start to reconnecting client |
| `netmsg.c:325-327` | SVC_LOBBY_LEADER | ✅ yes | CONTROL reliable | In `netmsgClcAuthRead()` — broadcast to all CLSTATE_LOBBY clients on every join |

### 1.2 `g_NetMsgRel` broadcast buffer (accumulate-then-flush pattern)

`netStartFrame()` resets this buffer at the END (after event dispatch). `netEndFrame()` calls
`netFlushSendBuffers()` twice — once before per-frame writes and once after — to flush whatever
has accumulated.

**⚠️ CRITICAL**: Any write to `g_NetMsgRel` that occurs DURING `netStartFrame()`'s event dispatch
loop will be **silently dropped** because `netStartFrame()` resets the buffer after the loop completes
(net.c lines 1219-1220). This is the root cause of Bug 1 below.

| Call site | Message | Written during netStartFrame? | Has netSend? | Notes |
|-----------|---------|-------------------------------|--------------|-------|
| `net.c:597-599` | SVC_STAGE_START | no — in `netServerStageStart()` | ✅ yes (immediate) | Broadcast to all clients at match start |
| `net.c:647-649` | SVC_STAGE_START | no — in `netServerCoopStageStart()` | ✅ yes (immediate) | Co-op variant |
| `net.c:694-696` | SVC_STAGE_END | no — in `netServerStageEnd()` | ✅ yes (immediate) | Broadcast end of match |
| `net.c:1060-1064` | CLC_AUTH + CLC_SETTINGS | no — in `netClientEvConnect()` which is an event callback but uses netbufStartWrite + immediate send | ✅ yes | Uses `netbufStartWrite(&g_NetMsgRel)` then sends immediately via netSend; safe because netSend resets the buffer |
| `net.c:1148-1150` | CLC_SETTINGS | no — in `netClientSettingsChanged()`, called from game code | ✅ yes (immediate) | `netbufStartWrite` + write + `netSend`; safe |
| `net.c:1241` | CLC_MOVE | no — in `netEndFrame()` | ✅ yes (flushed by netFlushSendBuffers at end of netEndFrame) | Written to g_NetMsg (unreliable) or g_NetMsgRel (reliable) |
| `net.c:1254` | SVC_PLAYER_MOVE | no — in `netEndFrame()` | ✅ yes | Accumulated per-frame; flushed |
| `net.c:1264` | SVC_CHR_MOVE | no — in `netEndFrame()` | ✅ yes | Every frame, unreliable |
| `net.c:1273` | SVC_CHR_STATE | no — in `netEndFrame()` | ✅ yes | Every 15 frames |
| `net.c:1280` | SVC_CHR_SYNC | no — in `netEndFrame()` | ✅ yes | Every 60 frames |
| `net.c:1285` | SVC_PROP_SYNC | no — in `netEndFrame()` | ✅ yes | Every 120 frames |
| `net.c:1308` | SVC_NPC_MOVE | no — in `netEndFrame()` | ✅ yes | Co-op, every 3 frames |
| `net.c:1320` | SVC_NPC_STATE | no — in `netEndFrame()` | ✅ yes | Co-op, every 30 frames |
| `net.c:1328` | SVC_NPC_SYNC | no — in `netEndFrame()` | ✅ yes | Co-op, every 120 frames |
| `net.c:1349` | SVC_CHR_RESYNC | no — in `netEndFrame()` | ✅ yes | On demand, server pending-flags |
| `net.c:1353` | SVC_PROP_RESYNC | no — in `netEndFrame()` | ✅ yes | On demand |
| `net.c:1357` | SVC_PLAYER_SCORES | no — in `netEndFrame()` | ✅ yes | On demand |
| `net.c:1362` | SVC_NPC_RESYNC | no — in `netEndFrame()` | ✅ yes | On demand |
| `net.c:1364` | SVC_STAGE_FLAG | no — in `netEndFrame()` | ✅ yes | On demand |
| `net.c:1366` | SVC_OBJ_STATUS | no — in `netEndFrame()` | ✅ yes | On demand, loop |
| `netmsg.c:349-351` | SVC_CHAT | **yes — in `netmsgClcChatRead()`** which fires in netStartFrame | ✅ yes (immediate) | Uses `netbufStartWrite` explicitly + `netSend`; safe because it resets, writes, sends immediately and netSend resets buffer again. No accumulated data lost. |
| `netmsg.c:2160` | CLC_RESYNC_REQ | **yes — in `netmsgSvcChrSyncRead()`** which fires in netStartFrame | ❌ **NO** | **BUG 1** — see §2.1 |
| `netmsg.c:2251` | CLC_RESYNC_REQ | **yes — in `netmsgSvcPropSyncRead()`** which fires in netStartFrame | ❌ **NO** | **BUG 1** — see §2.1 |
| `netmsg.c:2916` | CLC_RESYNC_REQ | **yes — in `netmsgSvcNpcSyncRead()`** which fires in netStartFrame | ❌ **NO** | **BUG 1** — see §2.1 |

### 1.3 Immediate sends via `g_NetMsgRel` (standalone packet, not accumulated)

These call `netbufStartWrite(&g_NetMsgRel)` + write + `netSend()` as an atomic pattern.
`netSend()` internally resets the buffer after transmitting (net.c:1416), so these leave
the buffer clean and don't interfere with accumulated data in netEndFrame.

| Call site | Message | Notes |
|-----------|---------|-------|
| `netdistrib.c:309-311` | SVC_DISTRIB_END (failure) | Immediate send to one client |
| `netdistrib.c:318-320` | SVC_DISTRIB_END (failure) | Immediate send |
| `netdistrib.c:340-342` | SVC_DISTRIB_END (failure) | Immediate send |
| `netdistrib.c:358-362` | SVC_DISTRIB_BEGIN | Immediate send |
| `netdistrib.c:395-397` | SVC_DISTRIB_END (success) | Immediate send |
| `netdistrib.c:421-423` | SVC_CATALOG_INFO | Immediate send per client after auth |
| `netdistrib.c:501-503` | SVC_LOBBY_KILL_FEED | Immediate send per lobby client |
| `netdistrib.c:615-617` | CLC_CATALOG_DIFF (no-op) | Immediate send, client has everything |
| `netdistrib.c:625-627` | CLC_CATALOG_DIFF | Immediate send with missing hashes |

### 1.4 Local buffer (stack-allocated, immediate send)

These use a stack-local `struct netbuf` with zero-initialized `wp=0`, equivalent to `netbufStartWrite`.

| Call site | Message | Notes |
|-----------|---------|-------|
| `net.c:1521-1547` | SVC_CHAT or CLC_CHAT | `netChatPrintf()` — uses local `bufdata[600]` with zero-init, immediate `netSend` |
| `net.c:1536-1546` | SVC_CHAT (server) | Same function, server path |
| `net.c:364-380` | Query response | Server info query, raw socket send, not ENet |

### 1.5 Client UI → network bridge

| Call site | Message | Has netSend? | Notes |
|-----------|---------|--------------|-------|
| `pdgui_bridge.c:420-422` | CLC_LOBBY_START | ✅ yes | `netbufStartWrite` + write + `netSend` — fixed in S60 |
| `netmenu.c:208-211` | CLC_LOBBY_START | ✅ yes | Legacy menu path, same pattern |

---

## 2. Bugs Found

### BUG-1 (CRITICAL): CLC_RESYNC_REQ silently dropped — all desync recovery broken

**Root cause**: Three sync read handlers (`netmsgSvcChrSyncRead`, `netmsgSvcPropSyncRead`,
`netmsgSvcNpcSyncRead`) detect desyncs and write `CLC_RESYNC_REQ` to `g_NetMsgRel` directly.
These handlers are called from `netClientEvReceive()` which is dispatched inside `netStartFrame()`'s
event loop. After the event loop completes, `netStartFrame()` resets `g_NetMsgRel` at lines 1219-1220.
The write is silently discarded. The server never receives the resync request.

**Impact**: Desync recovery is completely non-functional on the client. When chr, prop, or NPC state
drifts (detectable via SVC_CHR_SYNC, SVC_PROP_SYNC, SVC_NPC_SYNC checksums), the client logs the
desync but never actually requests a correction from the server.

**Fix**: Mirror the server-side `g_NetPendingResyncFlags` pattern. Add a client-side
`g_NetPendingResyncReqFlags` global. Set it in the handlers. Consume it in `netEndFrame()`
before the final flush — same as how the server consumes `g_NetPendingResyncFlags`.

**Files**: `net.h`, `netmsg.c`, `net.c`
**Status**: Fixed in this session (S61).

### BUG-2 (CRIT-2 from network-audit.md §7.5): g_Lobby.inGame always false on dedicated server

`netlobby.c:141`: `g_Lobby.inGame = (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME) ? 1 : 0;`

On dedicated server, `g_NetLocalClient == NULL`, so this is always 0.
`hub.c` reads `g_Lobby.inGame` to transition room 0 between LOBBY/GAME/POSTGAME states.
The server GUI also reads it. Both are always wrong on dedicated.

**Fix**: Walk `g_NetClients[]` to check if any client is `>= CLSTATE_GAME`.
**Status**: Fixed in this session (S61).

### BUG-3 (CRIT-1 from network-audit.md §7.3): NPC broadcast never fires on dedicated server

`net.c:1295`: `if (g_NetLocalClient && g_NetLocalClient->state >= CLSTATE_GAME)` — always false when
`g_NetLocalClient == NULL` on dedicated server. Co-op NPC positions are silently dropped.

**Fix**: Replace guard with `g_NetMode == NETMODE_SERVER && g_NetNumClients > 0`.
The intent is "broadcast once the stage is active and clients are present," not "broadcast only on
listen-server." The stage-active condition is captured by the outer `CLSTATE_GAME` client check.
**Status**: Fixed in this session (S61).

---

## 3. Already-Correct Patterns (no action needed)

- **`netmsgSvcLobbyLeaderWrite`**: The network-audit.md (written at S57) listed `SVC_LOBBY_LEADER`
  as "NEVER SENT." This was accurate at that time. As of the current codebase (post-S60), it IS
  now sent on every client join/reconnect in `netmsgClcAuthRead()`. network-audit.md §6.1 and §8
  HIGH-1 are now stale — see §4 below.

- **Desync detection logic itself**: The desync counters, thresholds, and cooldown values
  in netmsg.c are correct. Only the send path was broken (fixed above).

- **Accumulate-then-flush model**: `g_NetMsg` (unreliable) and `g_NetMsgRel` (reliable) are reset
  once per frame by `netStartFrame()` and flushed twice by `netEndFrame()`. All per-frame game-state
  writes (moves, bot positions, resync payloads) correctly use this model.

- **Immediate-send pattern**: The standalone send pattern (reset + write + netSend) in
  `netChatPrintf`, `netClientEvConnect`, `netClientSettingsChanged`, and the distrib functions is
  safe. `netSend()` resets the buffer after transmitting, so no stale data bleeds across.

---

## 4. Polling Patterns (Goal 2 audit)

### 4.1 lobbyUpdate() called 2-3x per frame — already documented in §7.4

- `server_main.c:353` — main loop (once per frame, correct)
- `server_bridge.c:27` — inside `lobbyGetPlayerCount()`, called per GUI query
- `pdgui_bridge.c:253` — same via `lobbyGetPlayerInfo()`
- `netmsg.c:319` — inline in `netmsgClcAuthRead()` (deliberate race fix from S55)
- `netmsg.c:3270` — inline in `netmsgClcLobbyStartRead()` (deliberate race fix from S55)

**Assessment**: With a 32-client cap, each `lobbyUpdate()` walks 32 slots — negligible CPU even
at 3x per frame. The inline calls in message handlers are intentional (S55 race fix). The GUI
bridge calls fire only when the lobby screen is open. No fix needed for correctness; recommended
optimization (dirty flag) is documented in network-audit.md MED-1 and remains low priority.

### 4.2 g_Lobby.inGame computed every frame from g_NetLocalClient state — already documented in §7.5

This is BUG-2 above (fixed in S61). The per-frame recomputation is correct in principle;
the bug was the wrong data source on dedicated server.

### 4.3 Single-pass event drain in netStartFrame() — already documented in §3.2

Only one ENet event is processed per `netStartFrame()` call. On high-churn frames (many simultaneous
connects), queued events back up. Already documented in network-audit.md LOW-2. No new findings.

---

## 5. Updates to network-audit.md

The following entries in network-audit.md are now stale or newly resolved:

| Section | Status | Detail |
|---------|--------|--------|
| §6.1 / HIGH-1 | **RESOLVED** | `SVC_LOBBY_LEADER` IS now sent on every client join in `netmsgClcAuthRead()`. "NEVER SENT" no longer applies. The per-disconnect leader re-send is not yet implemented (if leader disconnects, no SVC_LOBBY_LEADER is sent to update remaining clients). |
| §7.5 / CRIT-2 | **FIXED S61** | `g_Lobby.inGame` fixed to check `g_NetClients[]` array instead of `g_NetLocalClient`. |
| §7.3 / CRIT-1 | **FIXED S61** | NPC broadcast guard fixed for dedicated server. |
| NEW — BUG-1 | **FIXED S61** | CLC_RESYNC_REQ was silently dropped. Fixed with `g_NetPendingResyncReqFlags` pattern. |
