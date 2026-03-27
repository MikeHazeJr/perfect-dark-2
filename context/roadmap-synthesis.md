# Roadmap Synthesis — Network + Menu + Asset Audits

> Synthesized from: network-system-audit.md (S62), menu-asset-audit.md (S62)
> Purpose: Single prioritized action plan derived from both audits
> Created: 2026-03-27

---

## Executive Summary

Two deep audits — networking (766 lines) and menus/assets (652 lines) — reveal a system
that's architecturally sound in its foundations but has significant gaps between what's
built and what's wired up. The asset catalog is complete infrastructure with zero active
intercepts. The networking layer works end-to-end for single-room play but can't do
multi-room or live score updates. The menu system has 8 of 240 menus replaced. Controller
support works where ImGui menus exist but legacy menus have none.

The critical path to a playable networked Combat Sim match is short. The critical path to
a polished, multi-room, mod-aware server is long but well-mapped.

---

## Phase 0: Immediate Fixes (Tonight)

These are bugs that affect the current build and should be fixed before any new feature work.

| # | Issue | Source | Impact |
|---|-------|--------|--------|
| 0-1 | Lobby B/Escape disconnects without confirmation | Menu audit §1 | Users accidentally disconnect |
| 0-2 | Match Start not working (netSend missing in pdgui_bridge.c) | S60 Fix 2 | Can't start Combat Sim |
| 0-3 | SVC_LOBBY_LEADER not re-sent on leader disconnect | Network audit HIGH-1 | Client doesn't know new leader |
| 0-4 | numSims/simType from CLC_LOBBY_START not applied | Network audit LOW-1 | Bot count ignored by server |

---

## Phase 1: Playable Combat Sim (Milestone: Two Players in a Match)

Goal: Two networked players connect, set up a Combat Sim with bots, start the match, play.

| # | Task | Files | Depends On |
|---|------|-------|------------|
| 1-1 | Fix match start (verify netSend in pdgui_bridge.c) | pdgui_bridge.c | 0-2 |
| 1-2 | Apply numSims/simType from CLC_LOBBY_START on server | netmsg.c | 0-4 |
| 1-3 | Send SVC_LOBBY_LEADER on leader change, not just on join | netmsg.c, netlobby.c | 0-3 |
| 1-4 | Add disconnect confirmation dialog in lobby | pdgui_menu_lobby.cpp | 0-1 |
| 1-5 | Verify bot spawning works with networked match start | netmsg.c, setup.c | 1-1, 1-2 |
| 1-6 | Verify player spawning on Combat Sim maps | mplayer.c, setup.c | 1-5 |
| 1-7 | Send SVC_LOBBY_STATE when lobby settings change | netmsg.c, netlobby.c | — |

**Exit criteria**: Two PCs connect, configure Combat Sim, add bots, start match, both
players spawn on the map with bots.

---

## Phase 2: Room Lifecycle (R-2)

Goal: Rooms are demand-driven. No pre-generated rooms. Players create and join rooms properly.

| # | Task | Files |
|---|------|-------|
| 2-1 | Add leader_client_id to hub_room_t | room.h |
| 2-2 | Add room_id to netclient struct | net.h, net.c |
| 2-3 | Expand HUB_MAX_ROOMS to 16, HUB_MAX_CLIENTS to 32 | room.h |
| 2-4 | Remove permanent room 0 from roomsInit() | room.c |
| 2-5 | Remove special-case protection in roomDestroy() | room.c |
| 2-6 | Implement roomAddClient / roomRemoveClient | room.c |
| 2-7 | Implement roomTransferLeadership | room.c |
| 2-8 | Implement hubOnClientConnect (create room, assign leader) | hub.c |
| 2-9 | Implement hubOnClientDisconnect (cleanup, transfer, destroy) | hub.c |
| 2-10 | Wire hub hooks into net.c connect/disconnect handlers | net.c |

**Exit criteria**: Player connects → gets a room. Player disconnects → room destroyed.
No room exists without players.

---

## Phase 3: Room Sync Protocol (R-3)

Goal: Clients see real server room state. Join/leave rooms works.

| # | Task | Files |
|---|------|-------|
| 3-1 | Define SVC_ROOM_LIST (0x75), SVC_ROOM_UPDATE (0x76), SVC_ROOM_ASSIGN (0x77) | netmsg.h |
| 3-2 | Define CLC_ROOM_JOIN (0x0A), CLC_ROOM_LEAVE (0x0B) | netmsg.h |
| 3-3 | Implement read/write for all 5 new messages | netmsg.c |
| 3-4 | Server sends SVC_ROOM_LIST after client auth | netmsg.c |
| 3-5 | Server broadcasts SVC_ROOM_UPDATE on room changes | hub.c, room.c |
| 3-6 | Client handles SVC_ROOM_LIST — updates local room snapshot | netmsg.c, pdgui_menu_lobby.cpp |
| 3-7 | Client handles SVC_ROOM_ASSIGN | netmsg.c |
| 3-8 | Social lobby reads server room data, not local pool | pdgui_menu_lobby.cpp |
| 3-9 | Join Room button wired to CLC_ROOM_JOIN | pdgui_menu_lobby.cpp |

**Exit criteria**: Social lobby shows accurate room list from server. Players can create
and join rooms. Room list updates in real-time.

---

## Phase 4: Room Match Start (R-4)

Goal: Room leader starts match scoped to room members only.

| # | Task | Files |
|---|------|-------|
| 4-1 | Define CLC_ROOM_SETTINGS (0x0C), CLC_ROOM_START (0x0F) | netmsg.h |
| 4-2 | Implement room settings sync (game mode, stage, access mode) | netmsg.c |
| 4-3 | Implement CLC_ROOM_START — server validates leader, triggers stage load for room only | netmsg.c |
| 4-4 | Scope SVC_STAGE_START to room members (not all clients) | net.c |
| 4-5 | Room state transitions: LOBBY → LOADING → MATCH → POSTGAME → LOBBY | room.c |
| 4-6 | Deprecate CLC_LOBBY_START in favor of CLC_ROOM_START | netmsg.c |

**Exit criteria**: Match start only affects players in the room. Other rooms remain in lobby.

---

## Phase 5: Asset Catalog Activation (C-4 through C-7)

Goal: All asset loading goes through the catalog. No bypass paths.

| # | Task | Files |
|---|------|-------|
| 5-1 | C-4: Intercept fileLoadToNew — catalog resolve before ROM load | file.c |
| 5-2 | C-5: Intercept texLoad — catalog resolve for textures | tex.c |
| 5-3 | C-6: Intercept animLoadFrame — catalog resolve for animations | anim.c |
| 5-4 | C-7: Intercept sndStart — catalog resolve for audio | audio.c |
| 5-5 | Verify all CATALOG_CRITICAL log points work | file.c, modeldef.c, body.c |
| 5-6 | Add loading overlay for stage transitions | pdgui_backend.cpp |

**Exit criteria**: Every asset load goes through the catalog. Missing assets are logged
with CATALOG_CRITICAL. Loading has a visible progress indicator.

---

## Phase 6: Multi-Room Architecture Decision

Goal: Decide and document the multi-room model.

**The problem**: `g_StageNum`, `g_MpSetup`, `g_Vars`, `g_PlayerConfigsArray` are all
global singletons. The engine cannot run two stages simultaneously. Two rooms on different
maps is architecturally impossible without one of:

**Option A: Sequential Model** (recommended near-term)
- One match active at a time across the entire server
- Other rooms wait in lobby state until the current match ends
- Simple, works with current globals, no engine rework

**Option B: Process-Per-Room**
- Each room spawns a child process with its own game state
- Server hub is a coordinator, not a game engine
- Complex but enables true multi-room multi-map

**Option C: Instance Isolation**
- Refactor all global state into a per-room context struct
- Game tick takes a context parameter instead of reading globals
- Massive refactor of the decompiled N64 codebase

**Recommendation**: Option A for v0.x, design Option B for v1.0+.

---

## Phase 7: Menu Replacement (Ongoing)

Per menu-replacement-plan.md, 240 legacy menus in 9 groups. 8 replaced so far.

| Priority | Group | Count | Status |
|----------|-------|-------|--------|
| 1 | Solo Mission Flow | 11 | Not started |
| 2 | Combat Simulator Setup | 8 | Partially replaced (room tabs) |
| 3 | Agent File Management | 6 | Agent Select + Create done |
| 4 | Settings | 12 | Settings shell done, tabs partial |
| 5 | Multiplayer Results | 5 | Not started |
| 6 | Co-op / Counter-Op | 8 | Not started |
| 7 | In-Game HUD Overlays | 4 | Console overlay done |
| 8 | Special (credits, cinema) | 3 | Not started |
| 9 | Firing Range | 4 | Not started |

---

## Phase 8: Controller + Accessibility

| # | Task | Source |
|---|------|--------|
| 8-1 | Add D-pad binding to head selector in Agent Create | Menu audit §6 |
| 8-2 | Fix warning dialog rendering of LIST/DROPDOWN items | Menu audit §4 |
| 8-3 | Ensure all ImGui menus have full gamepad nav | Menu audit §4 |
| 8-4 | Add key binding hints in UI (show controller glyphs when gamepad connected) | New |
| 8-5 | Color contrast audit for colorblind accessibility | New |

---

## Phase 9: Performance + Polish

| # | Task | Source |
|---|------|--------|
| 9-1 | Cache lobbyUpdate with dirty flag (MED-1) | Network audit |
| 9-2 | Drain all ENet events per frame, not just one (LOW-2) | Network audit |
| 9-3 | Send live score updates per kill (MED-3) | Network audit |
| 9-4 | Implement auth timeout on server (MED-4) | Network audit |
| 9-5 | Remove dead SVC_PLAYER_GUNS define (LOW-3) | Network audit |
| 9-6 | Decide: implement or delete net_interface.h callbacks (ARCH-2) | Network audit |
| 9-7 | Network benchmark for dynamic player cap | Design constraint |
| 9-8 | Clean up worktree directories | Operational |

---

## Decision Log

Decisions needed from the game director before implementation can proceed:

1. **Multi-room model** (Phase 6): Option A (sequential) for now? Or design Option B immediately?
2. **Dead callback infrastructure** (ARCH-2): Wire it up as the true decoupling boundary, or delete it?
3. **Connect code security**: Sufficient for now, or invest in a proper auth token system?
4. **Menu replacement priority**: Stay with the current group ordering, or reprioritize?

---

*Synthesized: 2026-03-27, Session 62*
