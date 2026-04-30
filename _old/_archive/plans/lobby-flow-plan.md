# Lobby / Room / Match UX Flow Plan

> Created: 2026-03-27, Session 57
> Status: APPROVED — ready for implementation
>
> Cross-references:
>   [room-architecture-plan.md](room-architecture-plan.md) §10 (R-series phases — server-side/protocol plumbing)
>   [multiplayer-plan.md](multiplayer-plan.md) §2 (room types, lifecycle, access modes)
>   [join-flow-plan.md](join-flow-plan.md) §3.2 (room display gap), §6 J-3 (SVC_ROOM_LIST)
>
> This plan defines the client-facing UX. The server-side/protocol work that supports
> it is in room-architecture-plan.md. The L-series phases here depend on R-series phases
> completing first. See §4 for exact dependencies.

---

## 1. The Problem: Current Lobby Is Wrong

### What's implemented today (the wrong flow)

After connecting to a server, the player sees **one screen** (`pdgui_menu_lobby.cpp`) that shows:
- Left column: player list (name, character, connection state)
- Right column: hub state + room list (read-only display) **plus** arena picker, simulant count slider, difficulty picker, and three mode buttons (Combat Simulator / Co-op Campaign / Counter-Operative)

The leader presses "Combat Simulator" → `CLC_LOBBY_START` → server broadcasts `SVC_STAGE_START` to **all connected clients** → match loads.

### Why this is wrong

1. **Game mode selection is on the social lobby screen.** Players who just joined see "Choose a game mode" immediately, before any social context.
2. **No room concept from the player's perspective.** There's one global lobby, one leader, one active game mode. Multi-room multiplicity is invisible.
3. **No "Create Room" / "Join Room" actions.** Players can't browse rooms, join a specific room, or start their own.
4. **Match start broadcasts to all clients, not room members.** If 4 players are in one room and 2 are in another, pressing "Start" pulls everyone in.
5. **No mode-specific setup screens.** Campaign and Counter-Op both push `g_NetCoopHostMenuDialog` — the same dialog — and there's no distinct per-mode configuration UI.

---

## 2. The Correct Flow (Screen-by-Screen)

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
                                      [Match Start]
                                    (drop-in / drop-out)
```

---

### Screen 1: Social Lobby

**Trigger**: Client reaches `CLSTATE_LOBBY` after auth.
**File (current)**: `pdgui_menu_lobby.cpp` → `pdguiLobbyScreenRender()`
**What to show**:
- Header: server name/connect code (for host), "Connected to [server]" for clients
- **Player list** (left column): all connected players, their status (In Lobby / In Room X / In Match)
- **Room list** (right column): all active rooms with: room name, game mode (if set), state (Lobby/Loading/Match), player count
- **Actions**: "Create Room" button (always), "Join Room" button on each room row (if room is open and not full)
- Optional: server chat (future)
- Disconnect button

**What NOT to show**: Game mode picker, arena picker, simulant sliders. None of that.

**Leader concept**: No global leader on this screen. Every player can create their own room. "Leader" only exists inside a room.

---

### Screen 2: Create Room

**Trigger**: Player presses "Create Room" on Social Lobby.
**Action**: Send `CLC_ROOM_JOIN` with `room_id = 0xFF` (sentinel: "create new") or a dedicated `CLC_ROOM_CREATE` message.
**Server response**: Creates a new room, assigns player as leader, sends `SVC_ROOM_ASSIGN`.
**Client side**: On receiving `SVC_ROOM_ASSIGN`, transition to Room Interior screen.

This is a single-action flow, not a separate screen. The "Create Room" button sends one message and the server response triggers the UI transition.

---

### Screen 3: Join Room

**Trigger**: Player presses "Join" on a room row in the Social Lobby.
**Action**: Send `CLC_ROOM_JOIN` with the target `room_id` (and optional password).
**Server response**: Validates room access, adds player, sends `SVC_ROOM_ASSIGN`, broadcasts `SVC_ROOM_UPDATE`.
**Client side**: On `SVC_ROOM_ASSIGN`, transition to Room Interior screen.

If the room is password-protected, show a password input dialog before sending `CLC_ROOM_JOIN`.

---

### Screen 4: Room Interior

**Trigger**: Client receives `SVC_ROOM_ASSIGN` confirming they're in a room.
**File (new)**: `port/fast3d/pdgui_menu_room.cpp` (new file)
**What to show (leader view)**:
- Room name (auto-generated, editable in future)
- Player list for this room (name, team slot if applicable, ready state)
- **Mode selector** (only for leader): three buttons — "Combat Simulator", "Campaign (Co-op)", "Counter-Operative"
- Room settings: access mode toggle (Open / Password / Invite-only)
- "Leave Room" button (returns to Social Lobby)

**What to show (non-leader view)**:
- Room name
- Player list for this room
- Leader's name highlighted (gold), "waiting for leader to configure mode"
- A read-only mode display: shows what mode the leader has selected (if any)
- "Leave Room" button

**After mode is selected** (leader action):
- Leader presses a mode button → transition to mode-specific setup screen
- Non-leaders: see a loading/wait state "Leader is configuring [Mode]..."

---

### Screen 5a: Combat Sim Setup

**Trigger**: Leader presses "Combat Simulator" in Room Interior.
**File (reuse/adapt)**: `port/fast3d/pdgui_menu_matchsetup.cpp` (already exists — for local play)
  → May need a network-aware variant, or adapt the existing one to accept a "send to server" callback instead of calling `matchStart()` directly.
**What to show**:
- Arena picker (full list of 20 arenas)
- Simulant count (0–32 slider)
- Simulant difficulty (Meat / Easy / Normal / Hard / Perfect / Dark)
- Game options: scenario (Deathmatch / Team / Capture, etc.), time limit, score limit, weapon set
- Player team assignment (if team mode)
- "Start Match" button (leader only), "Back" (returns to Room Interior)

**Non-leader view**: Read-only display of leader's selections as they change (via `SVC_ROOM_UPDATE`).

**On Start**: Leader sends `CLC_ROOM_SETTINGS` (finalize settings) + `CLC_ROOM_START`. Server validates, broadcasts `SVC_STAGE_START` to room members only.

---

### Screen 5b: Campaign Setup

**Trigger**: Leader presses "Campaign (Co-op)" in Room Interior.
**File (new or reuse)**: Adapt `g_NetCoopHostMenuDialog` or write `pdgui_menu_campaign_setup.cpp`.
**What to show**:
- Mission selector (list of available campaign missions)
- Difficulty (Agent / Special Agent / Perfect Agent)
- "Start Mission" button (leader), "Back"

**Drop-in**: During campaign, players can join the room and spawn. This is inherent to how rooms work — the server manages the room state and allows latejoins when `ROOM_STATE_MATCH`.

---

### Screen 5c: Counter-Operative Setup

**Trigger**: Leader presses "Counter-Operative" in Room Interior.
**File (new or adapt)**: Similar to Campaign Setup.
**What to show**:
- Mission selector
- Role assignment: which player plays as Counter-Op (dropdown)
- Difficulty
- "Start Mission" button, "Back"

---

### Screen 6: In-Match

Not a new UI screen — this is the existing gameplay. But some lobby awareness is needed:
- **Drop-in**: New player joins the server → sees Social Lobby → joins the room → the room is in MATCH state → player gets a "Join Match in Progress" prompt → server spawns them.
- **Drop-out**: Player disconnects → room leadership transfers if needed → match continues.
- **Post-match**: Room returns to `ROOM_STATE_POSTGAME` → brief scorecard → returns to Room Interior for next round configuration.

---

## 3. Client State Machine

### States

```
CLIENT_STATE_DISCONNECTED
    │ connect + auth
    ↓
CLIENT_STATE_SOCIAL_LOBBY      ← SVC_ROOM_LIST updates here
    │ create room            │ join room
    ↓                        ↓
CLIENT_STATE_ROOM_LOBBY        ← SVC_ROOM_UPDATE updates here
    │ (leader) select mode
    ↓
CLIENT_STATE_MODE_SETUP        ← SVC_ROOM_UPDATE propagates settings changes
    │ (leader) start match
    ↓
CLIENT_STATE_LOADING           ← SVC_STAGE_START triggers this
    │ load complete
    ↓
CLIENT_STATE_INGAME            ← gameplay, HUD, scoreboard
    │ match ends
    ↓
CLIENT_STATE_POSTGAME          ← scorecard, vote for next
    │ auto / leader chooses
    ↓
CLIENT_STATE_ROOM_LOBBY        ← loop back for next round
```

Note: `CLIENT_STATE_*` is a UI layer on top of the existing `CLSTATE_*` (network state). The existing network states don't change. The menu manager (`menumgr`) tracks which screen is shown.

### Transitions and Messages

| Transition | Client sends | Server sends | Notes |
|-----------|-------------|--------------|-------|
| Reach CLSTATE_LOBBY | (nothing new) | SVC_ROOM_LIST | Server broadcasts full room list on auth complete |
| Create Room | CLC_ROOM_JOIN (room_id=0xFF) | SVC_ROOM_ASSIGN, SVC_ROOM_LIST | Server creates room, assigns leader |
| Join Room | CLC_ROOM_JOIN (room_id=N) | SVC_ROOM_ASSIGN, SVC_ROOM_UPDATE | Server validates + adds client |
| Leader selects mode | CLC_ROOM_SETTINGS (mode only) | SVC_ROOM_UPDATE | Non-leaders see mode update |
| Leader configures settings | CLC_ROOM_SETTINGS (full) | SVC_ROOM_UPDATE | Non-leaders see read-only preview |
| Leader starts match | CLC_ROOM_START | SVC_STAGE_START (room-scoped) | Only room members receive this |
| Player leaves room | CLC_ROOM_LEAVE | SVC_ROOM_ASSIGN (new solo room), SVC_ROOM_LIST | Returns player to Social Lobby |
| Player disconnects | (ENet disconnect) | SVC_ROOM_UPDATE (leadership transfer) | |
| Match ends | (server decides) | SVC_ROOM_UPDATE (state=POSTGAME) | Room returns to lobby after |

---

## 4. What Needs to Change

### UI Files — Rewrite vs Extend

| File | Action | What Changes |
|------|--------|-------------|
| `port/fast3d/pdgui_menu_lobby.cpp` | **Rewrite** — strip game mode selection | Remove: arena picker, sim sliders, mode buttons, "Game Setup" section. Keep: player list, room list (now with Join buttons), connect code display, disconnect button. |
| `port/fast3d/pdgui_menu_room.cpp` | **New file** | Room Interior screen: player list for this room, mode selector (leader), leave room button. |
| `port/fast3d/pdgui_menu_matchsetup.cpp` | **Extend** | Add network-aware path: when `g_NetMode == NETMODE_CLIENT`, "Start" sends CLC_ROOM_SETTINGS + CLC_ROOM_START instead of calling local matchStart(). Non-leaders see read-only preview. |
| `port/fast3d/pdgui_menu_campaign_setup.cpp` | **New file** | Campaign/Counter-Op setup screen. Can reuse dialog structure from g_NetCoopHostMenuDialog. |
| `port/fast3d/pdgui_menu_network.cpp` | **Minor** | Remove raw IP input ("Enter IP:port or connect code" label). Accept ONLY connect codes. |

### Net Messages — What to Add

These are already planned in room-architecture-plan.md §4:

| Message | Direction | When needed |
|---------|-----------|-------------|
| SVC_ROOM_LIST (0x75) | Server→Client | Social Lobby room display (L-1) |
| SVC_ROOM_UPDATE (0x76) | Server→Client | Live room state updates (L-2/L-3) |
| SVC_ROOM_ASSIGN (0x77) | Server→Client | Screen transition trigger (L-2) |
| CLC_ROOM_JOIN (0x0A) | Client→Server | Create or join a room (L-2) |
| CLC_ROOM_LEAVE (0x0B) | Client→Server | Return to Social Lobby (L-2) |
| CLC_ROOM_SETTINGS (0x0C) | Client→Server | Mode + config changes (L-3/L-4) |
| CLC_ROOM_KICK (0x0D) | Client→Server | Room leader kicks (L-3) |
| CLC_ROOM_TRANSFER (0x0E) | Client→Server | Leadership transfer (L-3) |
| CLC_ROOM_START (0x0F) | Client→Server | Match start (L-4) |

These are all defined in room-architecture-plan.md — implement them there (R-3/R-4) before wiring the UI here.

### Server-Side Handler Changes

| Handler | File | Change |
|---------|------|--------|
| `netmsgClcLobbyStartRead()` | netmsg.c | Deprecate in favor of `CLC_ROOM_START`. Keep for backward compat during transition. |
| New: `netmsgClcRoomJoinRead()` | netmsg.c | Create or join room based on room_id. 0xFF = create new. |
| New: `netmsgClcRoomLeaveRead()` | netmsg.c | Remove from current room, create new solo room for client. |
| New: `netmsgClcRoomSettingsRead()` | netmsg.c | Validate sender is leader, update room.scenario/stagenum/etc. Broadcast SVC_ROOM_UPDATE. |
| New: `netmsgClcRoomStartRead()` | netmsg.c | Validate leader + settings complete. Broadcast SVC_STAGE_START to room members only (not all clients). |
| `netServerStageStart()` | net.c | Currently broadcasts to all CLSTATE_LOBBY+ clients. Must become room-scoped: only send to clients whose `room_id` matches. |

### Menu Manager (menumgr)

The menu manager (`port/src/menumgr.c` + `port/include/menumgr.h`) needs new states:

```c
typedef enum {
    // existing...
    MENU_LOBBY,          /* social lobby */
    // new:
    MENU_ROOM_INTERIOR,  /* inside a room, mode selector */
    MENU_MODE_SETUP,     /* combat sim / campaign / counter-op setup */
} menu_state_e;
```

Transitions:
- On `SVC_ROOM_ASSIGN` received → push `MENU_ROOM_INTERIOR`
- On `CLC_ROOM_LEAVE` sent → pop to `MENU_LOBBY`
- On mode button pressed (leader) → push `MENU_MODE_SETUP`
- On `SVC_STAGE_START` received → pop all, enter game
- On post-match → push `MENU_ROOM_INTERIOR` (back to room lobby for next round)

---

## 5. Implementation Phases (L-Series)

The L-series phases define UI work only. Server/protocol plumbing is in the R-series (room-architecture-plan.md). **L-series phases depend on R-series phases being complete first.**

### Dependency Map

```
R-2 (room lifecycle) ──→ R-3 (room sync protocol) ──→ L-1 (social lobby)
                                                    ──→ L-2 (create/join room)
L-1 + L-2 ─────────────────────────────────────────→ L-3 (room interior)
L-3 ────────────────────────────────────────────────→ L-4 (combat sim setup)
L-3 ────────────────────────────────────────────────→ L-5 (campaign/counter-op)
L-4 + L-5 ─────────────────────────────────────────→ L-6 (drop-in/out)
```

---

### Phase L-1: Social Lobby (strip and rebuild)

**Depends on**: R-3 complete (SVC_ROOM_LIST arriving at client)
**Target file**: `port/fast3d/pdgui_menu_lobby.cpp`

What to do:
- [ ] Remove: `s_SelectedArena`, `s_NumSims`, `s_SimType` state vars and all UI for them
- [ ] Remove: "Game Setup" section (arena combo, sim slider, difficulty combo, mode buttons)
- [ ] Remove: "Co-op" and "Counter-Op" button and their `menuPushDialog` calls
- [ ] Keep: player list (left column) — mostly as-is
- [ ] Change: right column — show room list from server-received data (via `roomGetByIndex()` reading server-synced state, not local snapshot)
- [ ] Add: "Create Room" button at top of room list column (sends `CLC_ROOM_JOIN` with `room_id = 0xFF`)
- [ ] Add: "Join" button on each room row (sends `CLC_ROOM_JOIN` with that room's id)
- [ ] Add: Room state color-coding (Lobby=green, Loading=yellow, Match=blue, Full/Closed=gray)
- [ ] On `SVC_ROOM_ASSIGN` received: call `menuPush(MENU_ROOM_INTERIOR)` or equivalent transition
- [ ] Footer: remove "You are the lobby leader" messaging (no global leader in social lobby)

---

### Phase L-2: Room Create / Join

**Depends on**: R-3 complete (CLC_ROOM_JOIN, SVC_ROOM_ASSIGN wired server-side)
**Target files**: `pdgui_menu_lobby.cpp` (L-1 must be done first), `menumgr.c`

What to do:
- [ ] Wire "Create Room" button → send `CLC_ROOM_JOIN(room_id=0xFF)`
- [ ] Wire "Join" button on room row → send `CLC_ROOM_JOIN(room_id=N)`
- [ ] For password-protected rooms: show a simple password input dialog before sending `CLC_ROOM_JOIN`
- [ ] Handle `SVC_ROOM_ASSIGN` response: update `g_NetLocalClient->room_id`, push Room Interior screen
- [ ] Handle `SVC_ROOM_UPDATE`: refresh room list display (player counts, state, mode) in real-time

---

### Phase L-3: Room Interior + Mode Selection

**Depends on**: L-2 complete, R-4 (CLC_ROOM_SETTINGS, CLC_ROOM_TRANSFER wired server-side)
**Target file**: `port/fast3d/pdgui_menu_room.cpp` (new file)

What to do:
- [ ] Create `pdgui_menu_room.cpp` with `pdguiRoomScreenRender(s32 winW, s32 winH)` entry point
- [ ] Leader view: room name + player list + "Change Mode" buttons (Combat Sim / Campaign / Counter-Op) + Leave Room button
- [ ] Non-leader view: same player list, read-only mode display, "Waiting for leader to configure..." text
- [ ] Player list shows: name (gold=leader, green=self, white=others), ready state, ping (future)
- [ ] Mode buttons (leader only): pressing one sends `CLC_ROOM_SETTINGS` with mode field, then transitions to mode-specific setup screen
- [ ] Non-leader mode display: updated from `SVC_ROOM_UPDATE` as leader changes selection
- [ ] Leave Room button: sends `CLC_ROOM_LEAVE`, server responds with new solo room + `SVC_ROOM_ASSIGN`, transition back to Social Lobby
- [ ] Register in menumgr as `MENU_ROOM_INTERIOR`

---

### Phase L-4: Combat Sim Setup Screen

**Depends on**: L-3 complete, R-4 complete (CLC_ROOM_START wired server-side)
**Target file**: `port/fast3d/pdgui_menu_matchsetup.cpp` (extend existing)

What to do:
- [ ] Add `g_NetMode` check at the top of the render function
- [ ] In network mode: make all settings send `CLC_ROOM_SETTINGS` as they change (so non-leaders see live preview)
- [ ] In network mode: "Start Match" button sends `CLC_ROOM_START` instead of calling local `matchStart()`
- [ ] In network mode, non-leader: all controls disabled (BeginDisabled/EndDisabled), show read-only current settings received via `SVC_ROOM_UPDATE`
- [ ] Back button: returns to Room Interior (pop mode setup screen, don't send ROOM_LEAVE)
- [ ] Keep all existing local-play path unchanged (offline/non-network flow untouched)

---

### Phase L-5: Campaign + Counter-Op Setup

**Depends on**: L-3 complete, R-4 complete
**Target file**: `port/fast3d/pdgui_menu_campaign_setup.cpp` (new file)

What to do:
- [ ] Create new file with `pdguiCampaignSetupRender()` and `pdguiCounterOpSetupRender()` — or one unified function with a mode parameter
- [ ] Campaign setup: mission list picker, difficulty selector, "Start Mission" (leader) / read-only (non-leader)
- [ ] Counter-Op setup: mission list picker, role picker (who is the Counter-Op), difficulty, "Start Mission" / read-only
- [ ] Both: send `CLC_ROOM_SETTINGS` on any change, `CLC_ROOM_START` on Start
- [ ] Back button: returns to Room Interior
- [ ] Non-leader view: read-only display of leader's selections

---

### Phase L-6: Drop-In / Drop-Out During Match

**Depends on**: L-4/L-5 complete, R-4 complete (room state ROOM_STATE_MATCH handling)
**Target files**: `netmsg.c`, `hub.c`, `room.c`

What to do:
- [ ] When a new client connects to a server where all rooms are in MATCH state: put them in Social Lobby with the room list showing "In Match [X players]"
- [ ] Add "Join Match" action on match-state rooms (sends `CLC_ROOM_JOIN`; server allows join to ROOM_STATE_MATCH rooms)
- [ ] Server: on join to ROOM_STATE_MATCH room, send `SVC_STAGE_START` to new player only (not re-broadcast to all), spawn new player at safe pad
- [ ] On disconnect mid-match: server removes player, leadership transfers if needed, match continues for remaining players
- [ ] Post-match (`ROOM_STATE_POSTGAME`): brief scorecard, then room returns to `ROOM_STATE_LOBBY`, Room Interior screen appears for all room members (next round)

---

## 6. Cross-References

| Topic | Where to read |
|-------|--------------|
| Room struct, lifecycle, server-side functions | [room-architecture-plan.md](room-architecture-plan.md) |
| R-series implementation phases (server/protocol) | [room-architecture-plan.md](room-architecture-plan.md) §10 |
| Net protocol message IDs and wire formats | [room-architecture-plan.md](room-architecture-plan.md) §4 |
| Social hub vision, room types, player states | [multiplayer-plan.md](multiplayer-plan.md) §1-2 |
| Connect code security, join flow audit | [join-flow-plan.md](join-flow-plan.md) |
| Menu state manager (menumgr) design | [multiplayer-plan.md](multiplayer-plan.md) §6 |
| Campaign architecture (offline + online unified) | [multiplayer-plan.md](multiplayer-plan.md) §9 |

---

*Last updated: 2026-03-27, Session 57*
