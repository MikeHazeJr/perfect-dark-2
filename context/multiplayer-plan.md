# Multiplayer Infrastructure Plan

> Design document for the server-as-hub architecture, room system, player profiles,
> phonetic connection layer, server federation, and related UI. This document is the
> plan — no implementation until the game director approves the design.
> Back to [index](README.md)

---

## 1. Vision

The dedicated server is a **social hub**, not just a match host. Players connect to a
server and exist within it as a persistent presence, regardless of what they're doing.
A server with 8 connected players might have:

- 1 player doing a solo campaign mission (local, but presence visible to others)
- 3 players in an online bot match together
- 2 players in local splitscreen on the level editor
- 1 player spectating the bot match
- 1 player browsing their profile/stats

All connected players can see each other's status, send invites, join joinable sessions,
and view player profiles. The server is the persistent social layer.

---

## 2. Architecture

### 2.1 Server Process

```
Server Process (dedicated or client-hosted)
|
+-- Hub (hub.c) -- singleton, owns everything
|   |
|   +-- Player Registry -- all connected players, their presence state
|   |
|   +-- Room Pool (room.c) -- concurrent activity containers
|   |   +-- Room 0: "MP Match - Skedar Ruins" (3 players + 24 bots)
|   |   +-- Room 1: "Solo Campaign - dataDyne" (1 player, local)
|   |   +-- Room 2: "Level Editor" (2 players, splitscreen)
|   |   +-- Room 3: [empty]
|   |
|   +-- Profile Store -- player stats, achievements, shared content
|   |
|   +-- Federation Link -- connection to other servers in the mesh
|
+-- ENet Transport -- UDP networking
+-- Identity (identity.c) -- device UUID + player profiles
+-- Phonetic Layer (phonetic.c) -- human-friendly server addresses
```

### 2.2 Room Types

| Type | Description | Max Players | Authority |
|------|-------------|-------------|-----------|
| MP_MATCH | Competitive multiplayer (deathmatch, teams, etc.) | 8 + 32 bots | Server |
| CAMPAIGN | Campaign mission (solo or co-op, drop-in/drop-out) | 1-4 | Server or Local |
| SPLITSCREEN | Local splitscreen (any mode) | 4 local | Local (or Server if connected) |
| EDITOR | Level editor session | 1-4 | Server |
| SPECTATE | Watching another room | unlimited | Read-only |
| LOBBY | Waiting room before any session starts | 8 | Server |

**Splitscreen note**: splitscreen does NOT require a server connection. It works fully
offline with local authority. However, when connected to a server, splitscreen players
are treated as a connected group — they stay in the same room together, show up as a
unit in the player list, and can participate in server activities as a group. Think of
it as "local players sharing one network seat."

**Campaign authority modes**:
- **Offline**: local process is authority. Full campaign functionality. No server needed.
- **Online**: dedicated server is authority. Enables drop-in co-op, persistent stats
  tracking, and presence visibility to other connected players. Same game logic, different
  authority routing.

### 2.3 Room Lifecycle

```
EMPTY -> LOBBY -> LOADING -> ACTIVE -> POSTGAME -> LOBBY (loop)
                                    -> EMPTY (if disbanded)
```

- EMPTY: slot available
- LOBBY: players gathering, configuring match settings
- LOADING: stage assets loading on all clients
- ACTIVE: gameplay in progress
- POSTGAME: scores displayed, vote for next map
- Returns to LOBBY for next round, or EMPTY if everyone leaves

### 2.4 Player States (per server connection)

| State | Description |
|-------|-------------|
| CONNECTED | Authenticated, in the hub, not in any room |
| IN_LOBBY | In a room's lobby, configuring |
| IN_GAME | Actively playing in a room |
| SPECTATING | Watching a room |
| AWAY | Connected but idle/AFK |

---

## 3. Phonetic Connection Layer

### 3.1 How It Works (SPF-1, already coded)

IP:port encoded as 8 CV syllables using 16 consonants x 4 vowels = 6 bits per syllable.
48 bits total = full IPv4 (32) + port (16).

Format: `BALE-GIFE-NOME-RIVA`

### 3.2 User Flow

**Hosting:**
1. Player starts a server (dedicated or client-hosted)
2. Server generates phonetic code from its public IP:port
3. Code displayed prominently on the lobby screen
4. Player shares code with friends (text, voice, etc.)

**Joining:**
1. Player opens "Join Server" screen
2. Types the phonetic code (or direct IP)
3. Code decoded to IP:port, ENet connect initiated
4. On success, player enters the server hub

### 3.3 Remaining Work
- Display phonetic code on lobby screen (large, copyable)
- "Join by Code" input field on join screen
- Wire decode -> ENet connect path
- NAT traversal consideration (future: STUN/relay for players behind NAT)

---

## 4. Player Profiles

### 4.1 Local Profile (identity.c, already coded)

Stored in `pd-identity.dat`. Contains:
- Device UUID (unique per machine)
- Up to 4 player profiles (name, preferred head/body, flags)

### 4.2 Stats (to implement)

Tracked per-profile, stored locally and synced to server:

| Stat | Description |
|------|-------------|
| Total playtime | Across all modes |
| Campaign completion % | Per difficulty |
| Total kills / deaths | All modes combined |
| Player kills / deaths | Humans only (vs bot kills) |
| Favorite weapon | Most kills with |
| Favorite map | Most time played on |
| Win/loss record | MP matches |
| Accuracy | Shots fired vs hits |

### 4.3 Achievements (future)

Framework for unlockable achievements. Stored locally, displayed on profile.
Example: "Complete all missions on Perfect Agent", "Win a match with 30 bots", etc.

### 4.4 Shared Content

Players can publicly share:
- Custom maps (from level editor)
- Mod packs (PDPK files)
- Bot presets (from bot customizer)

Visible on their profile. Other connected players can download directly.

---

## 5. Server Federation (Future Phase)

### 5.1 Concept

Multiple servers link together to form a mesh network. Benefits:
- Larger player pool (all connected servers' players visible)
- Load distribution (new sessions routed to lowest-load server)
- Redundancy (if one server goes down, players reconnect to another)

### 5.2 Architecture

```
Server A <---> Server B <---> Server C
   |                              |
   +-----------> Server D <-------+

All servers share:
- Player presence (who's online, what they're doing)
- Server load metrics
- Shared content catalog
- Chat/invite routing
```

### 5.3 Session Routing

When a player starts a new play session:
1. Hub checks load across all federated servers
2. Routes the session to the server with the most capacity
3. Player stays connected to their home server for presence
4. Game authority runs on the routed server
5. Transparent to the player

### 5.4 Implementation Priority

This is Phase 3+ work. The foundation (single server hub) must be solid first.

---

## 6. Menu System Overhaul

### 6.1 Current Problems

- Double/multiple presses registering on Escape and other keys
- Menu state confusion (menus overlapping or appearing incorrectly)
- No clear hierarchy (which menu is on top, which has input focus)
- Mix of legacy PD menus and ImGui menus with no unified management

### 6.2 Proposed: Menu State Manager

Single authority for all menu state:

```c
typedef enum {
    MENU_NONE,           /* gameplay, no menus */
    MENU_MAIN,           /* main menu */
    MENU_PAUSE,          /* in-game pause */
    MENU_LOBBY,          /* server lobby */
    MENU_JOIN,           /* join server screen */
    MENU_SETTINGS,       /* settings/options */
    MENU_BOT_CONFIG,     /* bot customizer */
    MENU_ENDGAME,        /* post-match scorecard */
    MENU_PROFILE,        /* player profile view */
    MENU_MODDING,        /* modding hub */
    MENU_CONSOLE,        /* live console (backtick) */
} menu_state_e;
```

**Rules:**
- Only ONE menu active at a time (plus console as overlay)
- Input consumed by the active menu — game doesn't see it
- One-frame input cooldown on menu transitions (prevents double-press)
- Stack-based navigation: push menu on open, pop on back/escape
- Legacy PD menus wrapped in the same state system

### 6.3 Implementation

New file: `port/src/menumgr.c` + `port/include/menumgr.h`

- `menuPush(menu_state_e)` -- open a menu, push onto stack
- `menuPop()` -- close current menu, return to previous
- `menuGetCurrent()` -- what's on top
- `menuConsumeInput(SDL_Event*)` -- returns true if the menu ate the input
- One-frame cooldown after any transition

---

## 7. Implementation Sequence

| Phase | What | Depends On |
|-------|------|-----------|
| SPF-2a | Menu state manager | Nothing |
| SPF-2b | Verify SPF-1 (hub/room/identity/phonetic) | Build test |
| SPF-3a | Lobby ImGui screen (host side) | SPF-2a, SPF-2b |
| SPF-3b | Join screen with phonetic code input | SPF-3a |
| SPF-3c | Bot customizer in lobby settings | SPF-3a, D3R-8 |
| SPF-4 | Player profiles (stats tracking) | SPF-3a |
| SPF-5 | Server admin dashboard redesign | SPF-3a stable |
| SPF-6 | Server federation / mesh network | SPF-5 stable |

**Start with SPF-2a** (menu manager) because it unblocks everything else and fixes B-21.

---

## 8. Confirmed Decisions (Game Director, S48)

1. **All non-local multiplayer goes through a dedicated server.** No P2P, no client-hosted
   matches. The dedicated server is the sole authority for all connected play.

2. **Campaign is inherently co-op.** Solo campaign = co-op with 1 player. Drop-in/drop-out
   multiplayer for campaign missions. Treat solo and co-op as the same room type internally.

3. **Server federation routing is automatic and invisible to players.** Players connect to
   any server; the mesh routes their sessions to the lowest-load node transparently.

4. **Stats framework first, achievements later.** Build granular event tracking from the
   start (every kill, death, weapon used, mode played, etc. are countable events). The stats
   system is modular — achievements are a query layer on top of the stats, implemented when
   the foundation is solid. Track everything: kills by weapon type, kills against specific
   simulant difficulties, accuracy per weapon, time per map, etc.

5. **Level editor is pre-1.0 but lower priority** than online play, functional campaign,
   collision system, and mod framework. Systems are interconnected regardless — the room
   architecture, asset catalog, and mod pipeline all feed the editor.

## 9. Campaign Architecture Note

Since solo and co-op are the same thing:
- Campaign room type handles 1-4 players
- Mission loads through the same path regardless of player count
- Drop-in: new player joins mid-mission, spawns at nearest safe pad
- Drop-out: player disconnects, their character despawns, mission continues
- Solo player = same system, just no one else has joined yet
- Server tracks mission state (objectives, checkpoints) for the room
- **Offline campaign must work.** A player should never be required to be online to
  play campaign. When offline, campaign runs locally with the same code path — the
  "server" is just the local game process acting as authority. When connected to a
  server, the server tracks state and enables drop-in co-op. The transition between
  offline and online should be seamless — save state is compatible either way.
- This means campaign code has two authority modes: local (offline) and server (online).
  The game logic is identical; only the authority routing differs.

## 10. Priority Order (Confirmed)

1. Online multiplayer (server hub, lobby, rooms, phonetic)
2. Functional campaign (solo/co-op unified, objective loading via catalog)
3. Collision system redesign
4. Mod framework completion (effects, full catalog enumeration)
5. Level editor

---

*Last updated: 2026-03-26, Session 48*
