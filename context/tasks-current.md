# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Awaiting Build Test / Playtest

| Item | Status |
|------|--------|
| **Room interior UX + Match Start** (S57–S60) | **BUILD VERIFIED (S60)** — Five playtest fixes applied: Leave Room now calls pdguiSetInRoom(0) not netDisconnect; Start Match now sends via netSend (CLC_LOBBY_START was silently dropped); bot modal labels left-aligned; score slider 1-based; lobby shows Players: X/Y. Needs full playtest: Leave Room → social lobby, Start Match → match loads. |
| **netSend audit + CRIT fixes** (S61) | **BUILD VERIFIED (S61)** — 3 critical bugs fixed: (1) CLC_RESYNC_REQ was silently dropped (netStartFrame resets g_NetMsgRel after dispatch — fixed via g_NetPendingResyncReqFlags); (2) g_Lobby.inGame always 0 on dedicated server (fixed: walk g_NetClients[]); (3) NPC broadcast guard always false on dedicated server (fixed: g_NetNumClients > 0). Desync recovery now functional. |
| **2-player Combat Sim match** (S54) | Build client + server. Connect → lobby → Combat Simulator button → verify match loads + both players spawn. Key fixes: lobbyUpdate B-28 regression, g_MpSetup chrslots, playernum assignment. |
| **Collision Rewrite** (S48) | DISABLED -- original collision restored. Mesh code preserved for Phase 2 redesign. |
| **Data copy fix** (S48) | Rewritten with Split-Path parent traversal (no Resolve-Path/.. issues). Error popup on failure. Needs verify. |
| **SPF-1/3**: Hub, rooms, identity, lobby, join-by-code (S47d–S49) | Run `.\devtools\build-headless.ps1 -Target server`, then end-to-end join test (J-1) |
| **Update tab — cross-session staged version** (S50) | Build client. Download a version, close without restarting. Reopen Update tab → Switch button should appear without re-downloading. |
| **Player Stats** (playerstats.c): String-keyed counters, JSON persistence (S49) | Needs client build test. `port/src/playerstats.c` |
| **D3R-7**: Modding Hub -- 6 files (S40) | Needs client build test |
| **MEM-1**: Asset load state fields in asset_entry_t (S47a) | Needs full cmake pass |
| **B-13**: Prop scale fix -- modelGetEffectiveScale (S26) | Needs build test |
| **B-12 Phase 1**: Dynamic participant system (S26) | Needs build test |

---

## Bugs Still Open

| Bug | Severity | Status |
|-----|----------|--------|
| [B-17](bugs.md) Mod stages load wrong maps | HIGH | Structurally fixed (S32). Needs broader testing across all mod maps. |
| B-18 Pink sky on Skedar Ruins | MEDIUM | Reported S48. Possible missing texture or clear color issue. Needs investigation. |
| B-19 Bot spawn stacking on Skedar Ruins | MEDIUM | **PARTIAL FIX (S54)** — playerreset.c now scans pads file and populates g_SpawnPoints when none set and in net mode. Needs test to confirm dispersal works. |
| B-20 Mission 1 objective crash | HIGH | **FIXED (S48)** -- NULL modeldef guard added in modelmgrInstantiateModel. Root cause: objective completion spawns chr whose body filenum fails to load. |
| B-21 Menu double-press / hierarchy issues | MEDIUM | Escape and other inputs registering multiple times, menu state confusion. |
| B-24 Connect code byte-order reversal | CRITICAL | **FIXED (S49)** -- `pdgui_menu_mainmenu.cpp` byte extraction corrected (MSB→LSB order). |
| B-25 Server max clients = 8 | MEDIUM | **FIXED (S49)** -- `NET_MAX_CLIENTS` decoupled from `MAX_PLAYERS`, now 32. |
| B-26 Player name shows "Player1" | HIGH | **FIXED (S49)** -- `netClientReadConfig()` falls back to identity profile name. |

---

## Active Work Tracks

### Memory Modernization (D-MEM)

| Task | Status |
|------|--------|
| MEM-1: Load state fields | CODED (S47a) -- needs build test |
| MEM-2: assetCatalogLoad/Unload | PENDING |
| MEM-3: ref_count + eviction | PENDING |

### B-12: Participant System

| Phase | Status |
|-------|--------|
| Phase 1: Parallel pool | CODED -- needs build test |
| Phase 2: Callsite migration | DONE (S47b) -- build pass |
| Phase 3: Remove chrslots + protocol v22 | READY -- depends on Phase 2 QC |

### Join Flow (J-series) — Next Steps

| Phase | Task | Details |
|-------|------|---------|
| J-1 | **Verify end-to-end join** | Build server target, start server, enter code in client, verify CLSTATE_LOBBY + match start. |
| J-2 | **Server GUI connect code** | Add connect code display + Copy button to server_gui.cpp Server tab. |
| J-3 | **SVC_ROOM_LIST protocol** | Broadcast room state from server to clients so lobby UI shows real room data. |
| J-4 | **Server history UI** | Populate Recent Servers section. Display connect codes (re-encode), not raw IPs. |
| J-5 | **Lobby handoff polish** | Verify main menu → lobby overlay transition. Progress indicator during CONNECTING/AUTH. |

See [join-flow-plan.md](join-flow-plan.md) for full audit.

---

### Room Architecture (R-series) — See [room-architecture-plan.md](room-architecture-plan.md)

| Phase | Task | Details |
|-------|------|---------|
| R-1 | **Foundation (no protocol change)** | ~~Implement hub slot pool stubs (`hub.c`). Fix `g_NetLocalClient = NULL` for dedicated server (`net.c` B-28). Remove raw IP from server GUI status bar (`server_gui.cpp:695` B-29). Replace IP-bearing log lines with client index/name (B-30).~~ **DONE (S52)** — needs server build test. |
| R-2 | **Room lifecycle** | Expand `HUB_MAX_ROOMS=16`, `HUB_MAX_CLIENTS=32`. Add `leader_client_id` to `hub_room_t`. Add `room_id` to `struct netclient`. On-demand room creation per connect (`hubOnClientConnect/Disconnect`). Remove permanent room 0. |
| R-3 | **Room sync (protocol)** | `SVC_ROOM_LIST 0x75`, `SVC_ROOM_UPDATE 0x76`, `SVC_ROOM_ASSIGN 0x77`. `CLC_ROOM_JOIN 0x0A`, `CLC_ROOM_LEAVE 0x0B`. Client lobby UI reads server-authoritative room list (closes join-flow gap J-3). |
| R-4 | **Match start (room-scoped)** | `CLC_ROOM_SETTINGS 0x0C`, `CLC_ROOM_KICK 0x0D`, `CLC_ROOM_TRANSFER 0x0E`, `CLC_ROOM_START 0x0F`. Stage start scoped to room members. Room state transitions. |
| R-5 | **Server GUI redesign** | New Players + Rooms panel layout. Move/Kick/Set Leader/Close Room actions. No raw IP anywhere. Replace Hub tab with Rooms panel. |

---

### Lobby / Room / Match UX Flow (L-series) — See [lobby-flow-plan.md](lobby-flow-plan.md)

> L-series = client-facing UI only. Depends on R-2 + R-3 complete before L-1/L-2 can be wired.
> Full audit and design in lobby-flow-plan.md (S57).

| Phase | Task | Details |
|-------|------|---------|
| L-1 | **Social Lobby** | Rewrite `pdgui_menu_lobby.cpp`: strip game mode selection, add room list with Join buttons and Create Room. No game mode picker on this screen. Depends on R-3. |
| L-2 | **Room Create/Join** | Wire Create Room + Join Room buttons to `CLC_ROOM_JOIN`. Handle `SVC_ROOM_ASSIGN` → transition to Room Interior screen. Password dialog for protected rooms. Depends on R-3. |
| L-3 | **Room Interior + Mode Selection** | New `pdgui_menu_room.cpp`. Leader: mode buttons + room player list. Non-leader: read-only mode display. Leave Room → `CLC_ROOM_LEAVE`. Depends on R-4. |
| L-4 | **Combat Sim Setup** | Extend `pdgui_menu_matchsetup.cpp` with network path: settings send `CLC_ROOM_SETTINGS`, Start sends `CLC_ROOM_START`. Non-leader: read-only preview. Depends on R-4. |
| L-5 | **Campaign + Counter-Op Setup** | New `pdgui_menu_campaign_setup.cpp`. Mission picker, difficulty, role (Counter-Op), Start. Depends on R-4. |
| L-6 | **Drop-In / Drop-Out** | Allow joining ROOM_STATE_MATCH rooms. Server spawns new player at safe pad. Post-match returns to Room Interior for next round. Depends on L-4/L-5. |

---

### Asset Catalog Expansion

| Task | Status |
|------|--------|
| S46b: Full enumeration (anims, SFX, textures) | PENDING |

---

## Prioritized Next Up

| # | Task | Details |
|---|------|---------|
| 1 | **J-1: Verify end-to-end join** | Build server target + end-to-end test. See join-flow-plan.md. |
| 2 | **R-1: Room foundation** | Hub slot pool stubs, g_NetLocalClient=NULL for dedicated, IP scrub (B-28/29/30). No protocol change. See room-architecture-plan.md. |
| 3 | **J-2: Server GUI connect code** | Add code display to server_gui.cpp (superseded by R-5, but R-1 partially covers). |
| 3 | **Menu Replacement Group 1** | Solo mission flow (11 menus). See [menu-replacement-plan.md](context/menu-replacement-plan.md). |
| 2 | **Catalog Phase C-1/C-2** | ROM hash + base game catalog population. See [catalog-loading-plan.md](context/catalog-loading-plan.md). |
| 3 | **Catalog Phase C-4** | Intercept fileLoadToNew — catalog resolve before ROM load. Critical gateway. |
| 4 | **Menu Replacement Group 2** | End screens (13 menus). |
| 5 | **Menu Replacement Group 4** | Multiplayer setup (68 menus, largest group). |
| 6 | **Catalog Phase C-8** | Mod diff-based re-cataloging. |
| 7 | **Collision Rewrite Design** | Proper design. HIGH PRIORITY but design-first. |
| 8 | **B-19: Bot spawn stacking** | Populate g_SpawnPoints from arena pad data. |
| 6 | **B-20: Mission 1 crash** | Objective completion triggers loading that may bypass catalog. |
| 7 | **B-18: Pink sky** | Investigate Skedar Ruins sky rendering. |
| 8 | **B-12 Phase 3** | Remove chrslots field, legacy shims, BOT_SLOT_OFFSET. Protocol bump to v22. |
| 9 | **Bot Customizer Integration** | Wire into lobby flow (D3R-8 already coded). |
| 10 | **D5: Settings/Graphics/QoL** | FOV slider, resolution, audio volumes. |

---

## Pause Menu UX (S26 feedback)

| Issue | What Mike Wants |
|-------|----------------|
| End Game confirm/cancel too small | Separate overlay dialog. B cancels to pause menu. |
| Settings B-button exits to main menu | Should back out one level only |
| OG Paused text behind ImGui (B-15) | Suppress legacy pause rendering. Low priority. |
| Scroll-hidden buttons | Prefer docked/always-visible, minimize scrolling |

---

## Backlog

- Systemic bug audit: SP-1 remaining files (activemenu.c, player.c, endscreen.c, menu.c)
- S46b: Full asset catalog enumeration
- ~~Update tab UX: button sizing~~ **DONE S49** -- CalcTextSize-based buttons, per-row Download/Rollback, staged release "Switch" support. Version policy design still pending.
- ~~Update tab — cross-session staged version~~ **DONE S50** -- `.update.ver` sidecar, `updaterGetStagedVersion()`, Switch button persists across restarts.
- ~~Version baking~~ **DONE S50** -- always-clean builds (Clean Build toggle removed), version from UI boxes, `-DVERSION_SEM_*` flags injected every build via `Get-BuildSteps`
- **Network benchmark → dynamic player cap**: Measure bandwidth/latency at server start, call `hubSetMaxSlots()` to lower `g_NetMaxClients` below 32. Do not hardcode player counts.
- TODO-1: SDL2/zlib still DLL (low priority)
