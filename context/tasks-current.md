# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. For completed work, see [tasks-archive.md](tasks-archive.md).
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## Critical Blockers (v0.1.0 "Foundation")

> All critical blockers cleared. v0.1.0 pending QC pass and playtest of items in "Awaiting Build Test / Playtest" section.

> **UI Scaling** — DONE (S97). `videoGetUiScaleMult()`/`videoSetUiScaleMult()` added to `video.c`. `pdguiScaleFactor()` now multiplies by user setting. "UI Scale" slider (50–200%) in Video settings tab. Persists as `Video.UIScaleMult`.
> B-49 (toilet freeze): **VERIFIED FIXED S81**. B-38 (setupCreateProps crash): **CLOSED S80 — FALSE ALARM** (all hypotheses verified safe).

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

## Recently Completed (S109 — 2026-04-01)

| Item | Status |
|------|--------|
| **Credits/storyboard removal** (S109, f5cf0da) | **DONE** — Debug > About updated. "PD2 Port Director: MikeHazeJr" in intro credits. F11 storyboard + menu builder system fully removed (pdgui_storyboard.cpp/h, pdgui_menubuilder.cpp/h). 719/719 clean. |
| **Updater download freeze fix** (S109, 3b593ed) | **DONE** — UPDATER_ASSET_ZIP_SUFFIX ".zip", double SDL_LockMutex deadlock fixed. v0.0.20→v0.0.21 update path now works. |
| **Mission select UX redesign** (S109) | **DONE** — Flat blip-dot list, per-difficulty record times, objective tooltips, chapter header counts. Removed dead stubs from S106. |
| **v0.0.22 released to GitHub** (S109) | **DONE** — Full distribution ZIP (26.1 MB, EXEs + data/ + .sha256). Manual gh CLI release (release.ps1 hung). |
| **B-56 / B-57 / B-58 fixed** (S109) | **DONE** — Arena dropdown duplicate ID, scenario save weapon persistence, catalogResolveByRuntimeIndex assert all resolved. |

---

## Manifest Lifecycle Sprint

> **Goal**: Remove numeric alias bloat, make manifest speak human-readable catalog IDs natively.
> **Entry point**: Catalog investigation in S109 revealed ~7k real assets + ~7k alias duplicates = 14k entries. Aliases are waste.

| Phase | Task | Status |
|-------|------|--------|
| **Phase 0** | Remove numeric alias entries from catalog; manifest uses human-readable IDs ("base:falcon2" not "weapon_46") | **DONE (S110, commit 4476d00)** |
| **Phase 1** | Manifest-diff transitions — diff old vs new manifest, load delta, unload stale | **DONE (S110, commit dd04701)** — catalogLoadAsset/catalogUnloadAsset wired in manifestApplyDiff + manifestEnsureLoaded; manifestMPTransition() added; mainChangeToStage() routes SP vs MP. Needs SP playtest (two consecutive missions) + MP playtest (match load → verify MANIFEST-MP: log lines). |
| **Phase 2** | Dependency graph — character → body + head + anims + textures | NOT STARTED |
| **Phase 3** | Language bank manifesting — menu screens declare lang dependencies | NOT STARTED |
| **Phase 4** | Pre-validation pass — verify all entries exist before committing | NOT STARTED |
| **Phase 5** | Proper unload/cleanup — targeted ref-counted unloads | NOT STARTED |
| **Phase 6** | Menu/UI asset manifesting — screens register mini-manifests | NOT STARTED |

---

## Recently Completed (S106 — 2026-04-01)

| Item | Status |
|------|--------|
| **Solo Campaign mission select redesign + NULL crash fix** (S106) | **BUILD VERIFIED** — Tree-based chapter/mission hierarchy, all 21 missions with A/S/P completion badges, per-diff checkpoint rows, reward tooltip stubs. Fixed 11 `langGet()→langSafe()` crash sites (0xc0000005 on mission select). Needs playtest. |

---

## Recently Completed (S105 — 2026-04-01)

| Item | Status |
|------|--------|
| **Level Editor tab foundation** (S105) | **BUILD VERIFIED** — Tab 3 "Level Editor" in room screen. Catalog browser (type/search), spawned object list, property editor (scale/collision/interaction/texture), floating overlay, "Launch Level Editor" footer button. Needs playtest. Free-fly camera + empty level load are stubs. |

---

## Recently Completed (S103 — 2026-04-01)

| Item | Status |
|------|--------|
| **Group 6 Training Mode dialogs** | **BUILD VERIFIED** — `pdgui_menu_training.cpp`: 12 ImGui renderers (FR difficulty/info/stats, bio text, DT result, HT list+result, now-safe) + 10 NULL (3D model/GBI screens). Zero warnings. Needs playtest. |

---

## Recently Completed (S100 — 2026-04-01)

| Item | Status |
|------|--------|
| **Group 2 End Screens — registration wired** (9a376eb) | **DONE** — `pdguiMenuEndscreenRegister` now called from `pdguiMenusRegisterAll`. SP solo/2P/MP end screens active. `pdgui_menu_endscreen.cpp` already in 795ff96. Needs playtest. |

---

## Recently Completed (S99 — 2026-04-01)

| Item | Status |
|------|--------|
| **Group 1 Solo Mission Flow** (30a1d9e) | **DONE** — 8 ImGui replacements + 3 legacy-preserved. `pdgui_menu_solomission.cpp`. Needs playtest. |

---

## Recently Completed (S97-S98 — 2026-04-01)

| Item | Status |
|------|--------|
| **Kill plane void death** (9ff6daa) | **DONE** — adaptive Y threshold from `g_WorldMesh` bounds; falls back to Y < -10000. Force-kills + normal respawn. `src/game/player.c`. |
| **Catalog Settings tab** (1aa0c93) | **DONE** — Settings > Catalog: summary (entry counts by type, loaded/mod counts), entry browser (type filter + search), stage manifest view. `port/fast3d/pdgui_menu_mainmenu.cpp`. |
| **Universal numeric aliases** (1c801a3, 8f6de5e, 554759e) | **DONE** — `body_%d`, `head_%d`, `weapon_%d`, `stage_0x%02x`, `arena_%d`, `prop_%d`, `gamemode_%d`, `hud_%d`, `model_%d`, `anim_%d` (1207), `tex_%d` (3503), `sfx_%d` (1545). Mods can now override any asset by numeric ID. |
| **Arena list from catalog** (9a698c3) | **DONE** — GEX stages removed. "Mods" catch-all group added. Mod arenas auto-appear in Combat Sim dropdown. |
| **Boot crash / match start crash** (c7bfa43, c5486ee, f355ff6) | **DONE** — runtime_index space mismatch fixed; `sessionCatalogGetId` direct; stage_0x alias + hash backfill. |
| **Manifest chokepoints** (990d512, 6e1addc) | **DONE** — `manifestEnsureLoaded` wired in `bodyAllocateModel` (all spawn paths) and `setupLoadModeldef` (all model loads). Obj1 crash fixed. |
| **SA-2/SA-3/SA-4** (4945ff3, af6036b, 574f7b6) | **DONE** — modular catalog API, wire protocol migration, persistence migration. All completed in unlogged sessions (S91–S92). |

---

## Awaiting Build Test / Playtest

| Item | Status |
|------|--------|
| **SA-6/S97 SP manifest completeness** | **BUILD VERIFIED (S97)** — needs SP playtest: (1) two consecutive missions, verify Joanna stays in `to_keep`; (2) Counter-Op mode, check log for `MANIFEST-SP:` lines showing anti-player body/head added. |
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
| [B-58](bugs.md) `catalogResolveByRuntimeIndex` assert: type=16, index=103 | HIGH | **FIXED (S109)** — bounds checks against catalogGetNumHeads/Bodies. |
| [B-57](bugs.md) Scenario save: weaponset index only, not individual weapon picks | MED | **FIXED (S109)** — serializes g_MpSetup.weapons[] alongside weaponset. |
| [B-56](bugs.md) ImGui duplicate ID in arena dropdown (Room screen) | LOW | **FIXED (S109)** — PushID()/PopID() added around arena selector. |
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
| SA-2 | **Modular catalog API** | Per-type resolution functions (`catalogResolveBody/Head/Stage/Weapon`) + wire helper structs. **DONE (S91–S92, commit 4945ff3)** — completed in unlogged session between S91 and S92. |
| SA-3 | **Network session catalog wire migration** | Replaced raw N64 indices in SVC_*/CLC_* with u16 session IDs. ~180 call sites, ~20 message types. **DONE (S91–S92, commit af6036b)** |
| SA-4 | **Persistence migration** | Session IDs in savefile, identity, and scenario save. **DONE (S91–S92, commit 574f7b6)** — NOTE: scenario save weapon persistence (B-57) and type=16 assert (B-58) identified as follow-up bugs. |
| SA-5 | **Load path migration + deprecation pass** | SA-5a through SA-5f all complete. All `g_HeadsAndBodies[].filenum/.scale` and `g_Stages[].bgfileid` etc. load-path accesses migrated. `catalogGetBodyScaleByIndex` legacy fallback fixed. Deprecated-attribute audit confirmed zero external callers on 3 old modelcatalog functions. **DONE (S92-S93)**. |
| SA-6 | **SP load manifest (diff-based lifecycle)** | Full implementation + S96 follow-up: counter-op body/head added to `manifestBuildMission`; `manifestEnsureLoaded()` runtime safety net added; `bodyAllocateChr` wired. **DONE (S94/S96) — needs SP playtest: two consecutive missions (verify Joanna stays in to_keep) + Counter-Op mode (verify anti-player body/head in manifest log).** |
| SA-7 | **Consolidation cleanup** | modelcatalog.c kept (unique VEH/validation logic). g_ModBodies/Heads/Arenas confirmed absent. Removed dead accessors: `catalogGetEntry`, `catalogGetBodyByMpIndex`, `catalogGetHeadByMpIndex`. All 6 audits passed clean. port/CLAUDE.md updated with catalog-first rules. **DONE (S95) — 711/711 clean.** |

> **Status**: **ALL DONE (S97)**. SA-1 (S91), SA-2/3/4 (unlogged S91–S92), SA-5 (S93), SA-6 (S94/S96), SA-7 (S95). The full SA catalog migration track is complete.
> **Follow-up bugs**: B-57 (scenario save weapon persistence), B-58 (catalogResolveByRuntimeIndex type=16 assert on save path).
> **Dependencies**: Match Startup Pipeline (Phases B–F) consumed SA-2. R-series room sync depends on SA-1 (done). These dependencies are now unblocked.

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