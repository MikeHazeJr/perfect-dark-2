# Server/Client Join Flow — Audit & Plan

> Audit of the current connection architecture. Maps what's implemented vs. stubbed,
> identifies gaps, and defines the steps to complete the join flow end-to-end.
> Back to [index](README.md)

---

## 1. Architecture Overview

```
CLIENT                              SERVER (dedicated process)
  |                                    |
  | [Main Menu → Online Play]          | [netStartServer()]
  | → user enters 4-word connect code  |   ENet host created on port 27100
  | → connectCodeDecode() → IP:port    |   hubInit() → roomsInit() + identityInit()
  | → netStartClient(addrStr)          |   room 0 open (LOBBY state)
  |   ENet host_connect initiated      |
  |                                    |
  |  --- ENet CONNECT event ----->     |
  | CLSTATE_CONNECTING                 |
  |                                    |
  |  ← ENET_EVENT_TYPE_CONNECT ---     |
  | netClientEvConnect():              |
  |   sends CLC_AUTH                   |
  | CLSTATE_AUTH                       |
  |                                    |
  |  --- CLC_AUTH (name, head, -----> |
  |       body, team) ------------->   | netmsgClcAuthRead()
  |                                    |   assign playernum, validate
  |  <--- SVC_AUTH_OK (playernum) --   |   → CLSTATE_LOBBY
  | CLSTATE_LOBBY                      |
  |   lobbyUpdate() syncs players      | hubTick() runs every frame
  |   pdguiLobbyRender() shows UI      |   room 0 driven by g_Lobby.inGame
  |                                    |
  |  [Lobby leader presses game mode]  |
  |  --- CLC_LOBBY_START ----------->  |
  |                                    | netLobbyHandleStart()
  |  <--- SVC_STAGE_START ----------   |   room 0: LOBBY → LOADING → MATCH
  | CLSTATE_GAME                       |
  | game begins                        | g_Lobby.inGame = 1
  |                                    | hubTick(): room → MATCH state
```

---

## 2. Connect Code Integration

### 2.1 How It Works (IMPLEMENTED)

**Implementation**: `port/src/connectcode.c` + `port/include/connectcode.h`

The IPv4 address is encoded as 4 words:
```
[adjective] [creature/object] [action phrase] [place]
```
Each word maps to one byte (256 entries per category) = 4 bytes = full IPv4. Port is always `CONNECT_DEFAULT_PORT` (27100).

**Encode flow (server side, pdgui_menu_lobby.cpp)**:
1. Call `netGetPublicIP()` → tries UPnP first, then HTTP to `api.ipify.org`
2. Parse IP string → u32 (host byte order)
3. `connectCodeEncode(ip, buf, sizeof(buf))` → "fat vampire running to the park"
4. Display prominently in lobby UI. "Copy" button copies to clipboard.
5. Never displayed in raw IP form.

**Decode flow (client side, pdgui_menu_mainmenu.cpp view 4)**:
1. User enters 4-word sentence in text field
2. `connectCodeDecode(input, &ip)` — validates all 4 words against dictionaries
3. If valid: reconstruct as `"%u.%u.%u.%u:27100"` string
4. `netStartClient(addrStr)` → ENet connect
5. Raw IP is never shown; any invalid input fails before network attempt.

### 2.2 Security

- `connectcode.h` documents the API. **Note**: The IP parameter uses host byte order internally (not network byte order as the header comment states). Round-trip is consistent as long as encode and decode both use the same convention.
- No UI surface accepts raw IP strings. The join screen (`s_MenuView == 4`) only calls `connectCodeDecode()` — any non-sentence input returns -1 and no connection is attempted.
- `g_NetLastJoinAddr` stores the raw addr string internally for reconnect logic. Never displayed.
- When server history is built (future), entries must store and display connect codes, NOT the raw IP in `g_NetRecentServers[i].addr`.

---

## 3. Lobby Discovery and Room Display

### 3.1 Player List

`lobbyUpdate()` runs every frame in `pdguiLobbyScreenRender()`. It walks `g_NetClients[]` and syncs to `g_Lobby.players[]`. The lobby UI displays:
- Player name (Agent name from `cl->settings.name`)
- Character body name
- Connection state (CONNECTING / AUTH / LOBBY / GAME)
- Leader/local indicators

### 3.2 Room Display

`pdguiLobbyScreenRender()` calls `roomGetActiveCount()` and `roomGetByIndex()` to display the room list. Room state colors: Lobby=green, Loading=yellow, Match=blue, Postgame=purple.

**Important**: The lobby UI runs on the CLIENT. `roomGetByIndex()` reads from the **client's own local room pool**. Room data from the server is NOT yet transmitted to clients. The lobby currently shows the hub/room state as viewed by the client-side room objects — which are only updated via `hubTick()` on the server side.

**This is a gap**: clients don't receive server room state. The room list visible to clients is their local snapshot, not the server's authoritative state.

### 3.3 Connect Code on Server Side

The connect code is generated once in `pdguiLobbyScreenRender()` when `netGetMode() == NETMODE_SERVER`. Uses a `static bool s_CodeGenerated` flag — generated once per session. The server's dedicated GUI (`server_gui.cpp`) does NOT currently display the connect code. Server operators must check the log output for the code (`LOBBY: connect code: ...`).

---

## 4. What's Implemented

| Component | Status | File |
|-----------|--------|------|
| Connect code encode/decode | ✅ DONE | connectcode.c |
| Code-only join input (no raw IP) | ✅ DONE | pdgui_menu_mainmenu.cpp view 4 |
| netStartClient → ENet connect | ✅ DONE | net.c |
| CLC_AUTH / SVC_AUTH_OK handshake | ✅ DONE | netmsg.c |
| CLSTATE state machine | ✅ DONE | net.h |
| Lobby player list sync | ✅ DONE | netlobby.c |
| Lobby UI (player list + rooms + modes) | ✅ DONE | pdgui_menu_lobby.cpp |
| Leader game mode buttons | ✅ DONE | pdgui_menu_lobby.cpp |
| netLobbyRequestStart → server | ✅ DONE | netmenu.c (C bridge) |
| Hub + room lifecycle (server) | ✅ DONE (S81 verified) | hub.c, room.c |
| Room auto-naming | ✅ DONE | room.c roomGenerateName() |
| UPnP public IP (async) | ✅ DONE | netupnp.c |
| HTTP public IP fallback (curl/ipify) | ✅ DONE | netupnp.c netHttpGetPublicIP() |
| Connect code display + clipboard copy | ✅ DONE | pdgui_menu_lobby.cpp |
| **J-2** Server GUI connect code display | ✅ **DONE (S84)** | server_gui.cpp:~684, pdgui_bridge.c |
| **J-4** Recent servers + relative timestamps | ✅ **DONE (S80/S84)** | pdgui_menu_mainmenu.cpp, net.c, config |
| **J-5** Lobby handoff polish | ✅ **DONE (S81)** | pdgui_lobby.cpp, pdgui_menu_mainmenu.cpp |

---

## 5. What's Stubbed / Missing

### 5.1 Main Menu → Lobby Transition

After `netStartClient()` returns 0, the join screen shows "Connecting..." status text. The lobby overlay (`pdguiLobbyRender()` → `pdguiLobbyScreenRender()`) renders independently based on `netLocalClientInLobby()`. **Needs verification that this transition happens cleanly** — is the main menu automatically suppressed when the lobby overlay appears? The lobby could be rendering ON TOP of the main menu instead of replacing it.

**Action**: Verify in `pdgui_lobby.cpp` that it gates on client state and that `menuMgr` is updated.

### 5.2 Room State Not Synced to Clients

The room list in the lobby UI reads from client-local room objects. Server room state (which rooms exist, how many players, what state) is not broadcast to clients via the protocol. Clients only see their own local room 0 always in LOBBY state (since `hubTick()` only runs server-side).

**Plan**: Add `SVC_ROOM_LIST` message (server→client, reliable) that broadcasts the room array. Send on connect (CLSTATE_LOBBY) and on any room state change. Client-side `roomUpdateFromServer()` refreshes the local pool.

### ~~5.3 Server GUI Missing Connect Code~~ ✅ DONE (S84)

`server_gui.cpp:~684` now shows connect code via IP waterfall: UPnP → STUN → empty. Shows "discovering..." while either is in flight; falls back to "LAN only" only when both have settled. `pdgui_bridge.c`: `netGetPublicIP()` checks STUN between UPnP and HTTP fallback.

### ~~5.4 Recent Server History Stubbed~~ ✅ DONE (S80/S84)

`serverhistory.json` persists recent servers. Recent Servers panel in join view shows connect code + relative timestamp (S84: `fmtRelTime` lambda: "5s ago", "12m ago" etc.). Subtitle format: "ABC-DEF · 5m ago". `net.c` uses `time(NULL)` for unix timestamps (not tick-based). Config keys: `Net.RecentServer.N.Host` + `Net.RecentServer.N.Time`.

### 5.5 Protocol Version Mismatch in networking.md

`networking.md` still documents protocol v20. Actual version is v21 (bumped in S45 for `chrslots u32→u64`). B-12 Phase 3 will bump to v22.

### 5.6 s_CodeGenerated Static Flag

The connect code in `pdgui_menu_lobby.cpp` uses a `static bool s_CodeGenerated` flag. Once generated, it never refreshes. If the server's public IP changes during a session, the displayed code becomes stale.

**Plan**: Check if `netGetPublicIP()` has changed its cached result each frame (or on each lobby open). Low priority — IPs rarely change mid-session.

---

## 6. Step-by-Step Completion Plan

### Phase J-1: Verify Basic Join (No New Code)

1. Build `SPF-1` (hub/room) — server target
2. Start dedicated server, note connect code in logs
3. Start client, enter code in join screen
4. Verify: client reaches CLSTATE_LOBBY, lobby UI appears
5. Verify: pressing "Combat Simulator" starts a match

**Success criteria**: End-to-end join + match start with no crashes.

### ~~Phase J-2: Server GUI Connect Code Display~~ ✅ DONE (S84)

IP waterfall in `server_gui.cpp:~684`. UPnP→STUN→empty. STUN integrated into `netGetPublicIP()` via `pdgui_bridge.c`.

### Phase J-3: Room State Protocol

1. Add `SVC_ROOM_LIST` message (ID TBD, must not conflict with existing)
2. Server broadcasts on: new client reaching CLSTATE_LOBBY, any room state change
3. Client handler: update local room pool from server data
4. Lobby UI room list now shows server-authoritative state

### ~~Phase J-4: Server History UI~~ ✅ DONE (S80/S84)

`serverhistory.json` persistence (S80). Relative timestamps via `fmtRelTime` lambda (S84). Subtitle "CODE · Xm ago".

### Phase J-5: Main Menu ↔ Lobby Handoff Polish

1. Confirm lobby overlay suppresses main menu input correctly
2. Add transition animation (fade/slide) when connecting
3. Show "Connecting..." progress indicator in lobby overlay during CLSTATE_CONNECTING/AUTH
4. On disconnect: return to main menu join view with last code pre-filled

---

## 7. Security Confirmation

| Check | Status |
|-------|--------|
| No raw IP in join UI | ✅ Code-only input in mainmenu view 4 |
| Invalid code fails before network | ✅ connectCodeDecode() returns -1, no connect attempt |
| No raw IP in lobby display | ✅ Only connect code shown, generated from IP internally |
| g_NetLastJoinAddr raw IP | ✅ Internal only, never displayed in any UI |
| Recent server history | ✅ Connect code + relative timestamp displayed (S80/S84) |
| Server GUI | ✅ Connect code via UPnP→STUN waterfall (S84) |
| netGetPublicIP result | ✅ Used only for code generation, never shown raw |

---

*Created: 2026-03-26, Session 49*
