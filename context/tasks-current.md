# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Critical Blockers (v0.1.0 "Foundation")

| # | Task | Details |
|---|------|---------|
| 1 | **UI Scaling** | Not addressed yet. Required for v0.1.0. |

> B-49 (toilet freeze): **VERIFIED FIXED S81**. B-38 (setupCreateProps crash): **CLOSED S80 — FALSE ALARM** (all hypotheses verified safe). Both removed from blockers.

---

## Catalog Activation

| Step | Status |
|------|--------|
| **C-0** Wire assetCatalogInit + RegisterBaseGame + ScanComponents | **DONE** |
| **C-2-ext** source_filenum/texnum/animnum/soundnum in asset_entry_t | **DONE (S74)** |
| **catalogLoadInit** Reverse-index arrays + query functions | **DONE (S74)** |
| **C-4** `catalogGetFileOverride` intercept in `romdataFileLoad()` | **DONE (S74)** — needs playtest with a mod that declares bodyfile |
| **C-5** `catalogGetTextureOverride` intercept in `texLoad()` | **DONE (S92)** — confirmed: texLoad→modTextureLoad→catalogResolveTexture chain fully wired in texdecompress.c:2255 + mod.c |
| **C-6** `catalogGetAnimOverride` intercept in `animLoadFrame/Header()` | **DONE (S92)** — `modAnimationTryCatalogOverride()` added to mod.c; wired in animLoadFrame + animLoadHeader ROM paths in anim.c. Build clean. Needs playtest with anim override mod. |
| **C-7** `catalogGetSoundOverride` in `sndStart()` | **DONE (S80)** — `audioPlayFileSound()` via SDL_LoadWAV in audio.c, intercept wired in snd.c |
| **C-8** Re-wire `catalogLoadInit()` on mod enable/disable | **DONE (S80)** — `catalogLoadInit()` re-wired on toggle |
| **C-9** Stage diff | **DONE (S80)** — `catalogComputeStageDiff` implemented |

---

## Mod System (T-series) — ALL DONE

| Task | Status |
|------|--------|
| T-1 through T-10 | **ALL DONE (S80)** — base table expansion (anim 1207, tex 3503, audio 1545), size_bytes walker, thumbnail queue, sound intercept, mod.json content, stage reset, texture flush |

---

## Memory Modernization (D-MEM) — ALL DONE

| Task | Status |
|------|--------|
| MEM-1: Load state fields | **DONE** |
| MEM-2: assetCatalogLoad/Unload | **DONE** |
| MEM-3: ref_count + eviction | **DONE** |

---

## Awaiting Build Test / Playtest

| Item | Status |
|------|--------|
| **T-7 mod.json body/head/arena catalog registration** (S77) | **CODED (S77)** — needs playtest: enable a mod with `content.bodies/heads/arenas` in mod.json; verify entries appear in character/arena pickers in-game. |
| **T-8/T-9 Stage table restore + texture cache flush on reload** (S78) | **BUILD VERIFIED (S78)** — needs playtest: toggle a mod on/off, confirm no stale stages or textures after reload. |
| **B-46 Void spawn on MP stages** (S74) | **FIXED (S74)** — needs Felicity playtest confirmation. |
| **B-47 Exit freeze on window close** (S74) | **FIXED (S74)** — needs test: close window during match, should exit within 1s. |
| **Combat Sim scenario save/load** (S71) | **CODED (S71)** — needs build + smoke test. |

> The following were confirmed working in S81 networked playtest and removed from this list: **B-44/B-26** (bot names + player name), **B-43** (first-tick crash), **B-39** (jump crash), **B-40/41** (timelimit+options), **B-42** (bot cap).

---

## Awaiting Build Test / Playtest (Pre-S68)

| Item | Status |
|------|--------|
| **Room interior UX + Match Start** (S57–S60) | **MILESTONE: MATCH RUNS** (confirmed S68: 7 bots, Jungle, 25s gameplay). Full playtest: Leave Room → social lobby, Start Match → match loads. |
| **netSend audit + CRIT fixes** (S61) | **BUILD VERIFIED (S61)** — 3 critical bugs fixed. Desync recovery now functional. |
| **SP-6 Null Guard Audit** (S64) | **CODED (S64)** — 14 HIGH/CRITICAL PLAYERCOUNT() loops guarded across 8 files. Needs full client build + playtest. |
| **SP-8 Prop/Obj Null Guard Audit 2 of 4** (S65) | **CODED (S65)** — 7 CRITICAL/HIGH fixes. Needs build + playtest. |
| **Bot/AI/Simulant null-guard audit 4 of 4** (S66) | **CODED (S66)** — 28 CRITICAL/HIGH bugs fixed across 6 files. Needs build + dedicated server playtest. |
| **B-36 Client crash after skyReset** (S63) | **CODED (S63+S64)** — needs full client build + playtest on stage with ambient music. |
| **2-player Combat Sim match** (S54) | Build client + server. Connect → lobby → Combat Simulator → verify match loads + both players spawn. |
| **SPF-1/3**: Hub, rooms, identity, lobby, join-by-code (S47d–S49) | Run `.\devtools\build-headless.ps1 -Target server`, then end-to-end join test (J-1) |
| **Update tab — cross-session staged version** (S50) | Build client. Download a version, close without restarting. Reopen Update tab → Switch button should appear. |
| **Player Stats** (playerstats.c) (S49) | Needs client build test. |
| **D3R-7**: Modding Hub -- 6 files (S40) | Needs client build test |
| **B-13**: Prop scale fix (S26) | Needs build test |
| **B-12 Phase 1**: Dynamic participant system (S26) | Needs build test |

---

## Bugs Still Open

| Bug | Severity | Status |
|-----|----------|--------|
| [B-56](bugs.md) ImGui duplicate ID in arena dropdown (Room screen) | LOW | OPEN (S84) — PushID()/PopID() needed around arena selector. Not yet implemented. |
| [B-50](bugs.md) Dedicated server match-end freeze | HIGH | FIXED (S81) hub.c SDL timer. Needs playtest: start timed match on dedicated server. |
| [B-17](bugs.md) Mod stages load wrong maps | HIGH | Structurally fixed (S32). Needs broader testing. |
| B-18 Pink sky on Skedar Ruins | MEDIUM | Reported S48. Needs investigation. |
| B-19 Bot spawn stacking on Skedar Ruins | MEDIUM | **PARTIAL FIX (S54)** — needs test to confirm dispersal works. |
| B-21 Menu double-press / hierarchy issues | MEDIUM | Escape registering multiple times, menu state confusion. |

---

## Active Work Tracks

---

### ⚡ SESSION CATALOG + MODULAR API — HIGHEST INFRASTRUCTURE PRIORITY

> **Design doc**: [`context/designs/session-catalog-and-modular-api.md`](designs/session-catalog-and-modular-api.md)
> **Principle**: The catalog replaces the **entire** legacy loading pipeline, not just networking. All asset references at interface boundaries (wire protocol, save files, public APIs) must use catalog IDs — never raw N64 indices.

| Phase | Task | Details |
|-------|------|---------|
| SA-1 | **Session catalog infrastructure** | `sessioncatalog.h/c`: build from manifest, broadcast SVC_SESSION_CATALOG (0x67), receive + resolve, teardown. **DONE (S91)** — both targets build clean. |
| SA-2 | **Modular catalog API** | Per-system query functions (bodies, heads, stages, weapons, sounds) replacing ad-hoc `catalogResolve()` calls. Typed entry structs. |
| SA-3 | **Network session catalog wire migration** | Replace raw indices in SVC_*/CLC_* with u16 session IDs from session catalog. ~180 call sites, ~20 message types. Requires SA-1 + SA-2. |
| SA-4 | **Load manifest system** | Unified asset list for both MP and SP: what stages, bodies, heads, mods are needed. Feeds manifest pipeline (Phase B/C) and mod transfer (Phase D). |
| SA-5 | **Load path migration + deprecation pass** | SA-5a through SA-5f all complete. All `g_HeadsAndBodies[].filenum/.scale` and `g_Stages[].bgfileid` etc. load-path accesses migrated. `catalogGetBodyScaleByIndex` legacy fallback fixed. Deprecated-attribute audit confirmed zero external callers on 3 old modelcatalog functions. **DONE (S92-S93)**. |
| SA-6 | **SP load manifest (diff-based lifecycle)** | `manifest_diff_t`, `manifestBuildMission`, `manifestDiff`, `manifestDiffFree`, `manifestApplyDiff`, `manifestSPTransition` added to netmanifest.c/h. `mainChangeToStage()` calls `manifestSPTransition()` for gameplay stages. `g_CurrentLoadedManifest` global tracks current state. TODOs logged for spawn-list character enumeration and prop model enumeration. **DONE (S94) — build clean, needs SP playtest (two consecutive missions, verify Joanna stays in to_keep).**

> **Status**: SA-1 DONE (S91). **SA-5 DONE (S93)**. **SA-6 DONE (S94)**. SA-2 is next (modular catalog API layer — new typed query functions). SA-3 depends on SA-1 + SA-2. SA-4 is parallel infrastructure.
> **Dependencies**: Match Startup Pipeline (Phases B–F) consumes SA-2. R-series room sync consumes SA-1.

---

### Join Flow (J-series) — Next Steps

| Phase | Task | Details |
|-------|------|---------|
| J-1 | **Verify end-to-end join** | **DONE (S81)** — full join cycle verified: connect code → CLSTATE_LOBBY → match loads → match runs → match ends. |
| J-2 | **Server GUI connect code** | **DONE (S84)** — IP waterfall UPnP→STUN→empty in `server_gui.cpp:~684`. Shows "discovering..." while UPnP/STUN working; "LAN only" only when both settled. `pdgui_bridge.c`: `netGetPublicIP()` checks STUN between UPnP and HTTP fallback. |
| J-3 | **SVC_ROOM_LIST protocol** | Broadcast room state from server to clients so lobby UI shows real room data. |
| J-4 | **Server history UI** | **DONE (S80/S84)** — serverhistory.json + Recent Servers panel. S84: `fmtRelTime` lambda ("5s ago", "12m ago") in subtitle "ABC-DEF · 5m ago"; `net.c` changed `lastresponse = g_NetTick` → `lastresponse = (u32)time(NULL)` for unix timestamps; expanded config `Net.RecentServer.N.Host` + `.Time`. |
| J-5 | **Lobby handoff polish** | **DONE (S81)** — `menuStop()` + `pdguiMainMenuReset()` called on SVC_AUTH; menu stack cleared on lobby join. |

See [join-flow-plan.md](join-flow-plan.md) for full audit.

---

### Room Architecture (R-series) — See [room-architecture-plan.md](room-architecture-plan.md)

| Phase | Task | Details |
|-------|------|---------|
| R-1 | **Foundation (no protocol change)** | Hub slot pool stubs, `g_NetLocalClient = NULL` for dedicated server, IP scrub (B-28/29/30). **DONE (S52)** — needs server build test. |
| R-2 | **Room lifecycle** | Expand `HUB_MAX_ROOMS=16`, `HUB_MAX_CLIENTS=32`. Add `leader_client_id`, `room_id`. On-demand room creation. Remove permanent room 0. |
| R-3 | **Room sync (protocol)** | `SVC_ROOM_LIST 0x75`, `SVC_ROOM_UPDATE 0x76`, `SVC_ROOM_ASSIGN 0x77`. `CLC_ROOM_JOIN 0x0A`, `CLC_ROOM_LEAVE 0x0B`. |
| R-4 | **Match start (room-scoped)** | `CLC_ROOM_SETTINGS 0x0C`, `CLC_ROOM_KICK 0x0D`, `CLC_ROOM_TRANSFER 0x0E`, `CLC_ROOM_START 0x0F`. |
| R-5 | **Server GUI redesign** | New Players + Rooms panel layout. No raw IP anywhere. Replace Hub tab with Rooms panel. |

---

### Lobby / Room / Match UX Flow (L-series) — See [lobby-flow-plan.md](lobby-flow-plan.md)

> L-series = client-facing UI only. Depends on R-2 + R-3 complete before L-1/L-2 can be wired.

| Phase | Task | Details |
|-------|------|---------|
| L-1 | **Social Lobby** | Rewrite `pdgui_menu_lobby.cpp`: strip game mode selection, add room list with Join buttons and Create Room. Depends on R-3. |
| L-2 | **Room Create/Join** | Wire Create Room + Join Room buttons to `CLC_ROOM_JOIN`. Handle `SVC_ROOM_ASSIGN`. Depends on R-3. |
| L-3 | **Room Interior + Mode Selection** | New `pdgui_menu_room.cpp`. Leader: mode buttons + room player list. Non-leader: read-only. Depends on R-4. |
| L-4 | **Combat Sim Setup** | Extend `pdgui_menu_matchsetup.cpp` with network path. Depends on R-4. |
| L-5 | **Campaign + Counter-Op Setup** | New `pdgui_menu_campaign_setup.cpp`. Depends on R-4. |
| L-6 | **Drop-In / Drop-Out** | Allow joining ROOM_STATE_MATCH rooms. Depends on L-4/L-5. |

---

### Match Startup Pipeline — See [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md)

> Unified design merging B-12 Phase 3, R-2/R-3, J-3, C-series, and mod distribution. 8-phase pipeline: Gather→Manifest→Check→Catalog→Transfer→Ready Gate→Load→Sync.

| Phase | Task | Details |
|-------|------|---------|
| A | **Protocol messages** | **DONE (S84)** — `SVC_MATCH_MANIFEST (0x62)`, `CLC_MANIFEST_STATUS (0x0E)`, `SVC_MATCH_COUNTDOWN (0x63)` opcodes + structs + read/write stubs in netmsg.c; `match_manifest_t` in netmanifest.h; `ROOM_STATE_PREPARING`/`CLSTATE_PREPARING` added; NET_PROTOCOL_VER=24 |
| B | **Server side manifest build** | **DONE (S85)** — `port/src/net/netmanifest.c`: `manifestBuild()`, `manifestComputeHash()`, `manifestClear()`, `manifestAddEntry()`, `manifestLog()`. Wired into `netmsgClcLobbyStartRead` before `netServerStageStart()`. Logs manifest on every match start. Both targets build clean. |
| C | **Client manifest processing** | **DONE (S86)** — `manifestCheck()` in netmanifest.c; `g_ClientManifest` global; `netmsgSvcMatchManifestRead()` stores + checks; server broadcasts manifest + logs; both dispatches wired in net.c |
| C.5 | **Full game catalog registration** | **PARTIAL (S87)** — SP bodies/heads from g_HeadsAndBodies[152] registered in assetcatalog_base.c (~12 new entries). Stages already covered by s_BaseStages[]. **Remaining**: navmesh spawn fallback for SP maps, UI grouping (MP/SP/Unlockable categories in character picker), MPFEATURE_* unlock gating in selection UI. |
| D | **Mod transfer** | **DONE (S88)** — `netmsgClcManifestStatusRead()` resolves missing hashes via catalog, queues via `netDistribServerHandleDiff`. No-op when all clients READY. |
| E | **Ready gate** | **DONE (S88)** — `s_ReadyGate` bitmask tracker, `CLSTATE_PREPARING` transitions, 30s timeout, per-client status handling (READY/NEED_ASSETS/DECLINE), broadcasts SVC_MATCH_COUNTDOWN on state changes. Replac