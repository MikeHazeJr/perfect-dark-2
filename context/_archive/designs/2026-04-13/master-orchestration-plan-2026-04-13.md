# Master Orchestration Plan — 2026-04-13

> **Date**: 2026-04-13
> **Status**: Approved plan — awaiting execution
> **Author**: Claude Opus 4.6 (1M context), consolidation session
> **Source documents**:
>   1. [infrastructural-repair-plan-2026-04-13.md](infrastructural-repair-plan-2026-04-13.md) — Opus 1M bug-sweep (FIX-A through FIX-G)
>   2. [spawn-system-architecture-2026-04-13.md](spawn-system-architecture-2026-04-13.md) + [mod-map-import-pipeline-2026-04-13.md](mod-map-import-pipeline-2026-04-13.md) + [spawn-and-import-fix-plan-2026-04-13.md](spawn-and-import-fix-plan-2026-04-13.md)
>   3. [match-lifecycle-architecture-audit-2026-04-13.md](match-lifecycle-architecture-audit-2026-04-13.md) + [match-lifecycle-fix-plan-2026-04-13.md](match-lifecycle-fix-plan-2026-04-13.md)
>   4. [menu-input-architecture-audit-2026-04-13.md](menu-input-architecture-audit-2026-04-13.md) + [menu-input-fix-plan-2026-04-13.md](menu-input-fix-plan-2026-04-13.md)
>   5. [build-pipeline-improvements-2026-04-13.md](build-pipeline-improvements-2026-04-13.md) + [smoke-verify-2026-04-13.md](../../builds/smoke-verify-2026-04-13.md)

---

## Known Blockers

1. **Warm-ccache PCH regression** — S227 smoke-verify measured 30.7s warm build (expected 9.4s). 78% of TUs uncacheable due to `target_precompile_headers` in CMakeLists.txt (commit `955dffa2`).
   - **Try first**: `ccache --set-config sloppiness=pch_defines,time_macros` — if warm build drops below 12s, keep PCH.
   - **Fallback**: Remove `target_precompile_headers(pd PRIVATE ...)` block from CMakeLists.txt. Trades ~5s PCH benefit for 100% ccache hit rate. Net warm build returns to ~9.4s.
   - **Must resolve before any implementation session** — every session pays the 30s tax otherwise.

2. **pdguiThemeRegisterModDir server link verify** — The `pdguiThemeRegisterModDir()` stub added for pd-server target needs verification on a fresh both-targets build. If the server target fails to link, add a `#ifndef DEDICATED_SERVER` guard or a proper no-op stub in `pdgui_theme.cpp`.

---

## Fold-In Mapping: Infrastructure Classes → Work Tracks

The Opus 1M bug-sweep (FIX-A through FIX-G) doesn't exist in isolation — most classes fold into the four architecture audit tracks. This table shows where each class lands:

| Infra Class | Bug(s) | Description | Folds Into | Rationale |
|-------------|--------|-------------|------------|-----------|
| **Class A** | B-126, B-112 | Stack/memory corruption in chr tick | **Standalone FIX-A track** | Deep engine work (chr.c, chraction.c, crash.c), no overlap with other tracks |
| **Class B** | B-118, untracked "no enemies" | Manifest completeness | **Match Lifecycle L1-1 / L2** | L1-1 adds manifestClear; L2-1 builds co-op manifest. FIX-B.2 (graceful fallback) is standalone safety net |
| **Class C** | B-18, untracked "menu opacity" | Rendering state leakage | **Standalone FIX-C track** | GBI/ImGui rendering, no overlap with net or menu logic tracks |
| **Class D** | B-95, B-60, untracked "killfeed" | HUD/UI context gating | **Menu & Input F-0.3 / F-1** | FIX-D.1 (pdguiGetUIContext) is the infrastructure; F-0.3/F-1 consume it. B-60 and killfeed are trivial point fixes bundled here |
| **Class E** | B-19 | Spawn/pad selection | **Spawn System L1–L4** | Spawn architecture replaces FIX-E entirely with the 4-layer fallback chain |
| **Class F** | B-99 | Update system reliability | **Standalone FIX-F track** | Self-contained in updater.c, no dependencies |
| **Class G** | B-97 | Mission flow/category | **Standalone FIX-G track** | Self-contained UI in pdgui_menu_solomission.cpp |

**Items already resolved** (close in bugs.md during Layer 0):
- B-72 (SVC_LOBBY_STATE raw stagenum) — confirmed fixed by v27 refactor
- B-21 (menu double-press) — likely fixed by S124 Phase E + S208 grace guard; verify in next playtest

---

## Dependency-Ordered Layers

### Layer 0: Early Fixes (Parallel-Safe)

**Goal**: Clear the decks — resolve the build blocker, harden the crash path, fix the worst menu/manifest bugs. Everything here is independent; run all items in parallel.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **L0-BUILD** | Warm-ccache sloppiness tweak | smoke-verify | `CMakeLists.txt` | 0.25 | — | All L0 |
| **L0-LINK** | pdguiThemeRegisterModDir server link verify | smoke-verify | `port/fast3d/pdgui_theme.cpp`, `CMakeLists.txt` | 0.25 | — | All L0 |
| **FIX-A** | Chr tick isolation + lifetime hardening | infra-repair FIX-A | `src/game/chr.c`, `src/game/chraction.c`, `port/src/crash.c`, `src/game/mplayer/participant.c` | 2 | — | All L0 |
| **F-0.1** | Campaign language-bank shadow fix | menu-input F-0.1 | `port/fast3d/pdgui_menu_solomission.cpp` | 0.5 | — | All L0 |
| **F-0.2** | Context leak in training menu | menu-input F-0.2 | `port/fast3d/pdgui_menu_training.cpp` | 0.25 | — | All L0 |
| **F-0.3** | Context leak in game-over panels | menu-input F-0.3 | `port/fast3d/pdgui_menu_pausemenu.cpp` | 0.25 | — | All L0 |
| **F-0.4** | Stale manifest on return-to-room | menu-input F-0.4 | `port/src/pdmain.c` or `port/fast3d/pdgui_bridge.c` | 0.25 | — | All L0 |
| **L1-1** | Clear g_ClientManifest on match end | match-lifecycle L1-1 | `port/src/net/netmsg.c` | 0.25 | — | All L0 |
| **FIX-B.2** | Graceful fallback for missing models | infra-repair FIX-B.2 | `src/game/setup.c`, fileload (model loader) | 0.5 | — | All L0 |
| **CLOSE** | Close B-72, B-21 in bugs.md | infra-repair | `context/bugs.md` | 0.1 | — | All L0 |

**Collision notes**: L1-1 and F-0.4 both touch manifest clear paths — L1-1 is in `netmsg.c` (SVC_STAGE_END handler), F-0.4 is in `pdmain.c`/`pdgui_bridge.c`. No file collision. FIX-A touches `chr.c`/`chraction.c`/`crash.c` which nothing else in L0 touches.

**Estimated Layer 0 total**: 2–3 sessions (FIX-A is the long pole; everything else is < 0.5 session each).

**Smoke-verify checkpoint after Layer 0**: Full both-targets build. Verify warm ccache < 12s. Launch 31-bot match, confirm no crash in 10 minutes. Open Solo Missions, confirm campaign labels visible. Open training FR weapon list and back out, confirm no stuck input.

---

### Layer 1: Networking Safety Baseline

**Goal**: Eliminate score drift, HUD waste, stale co-op linkages, and resync gaps. Zero protocol changes. Can ship immediately after Layer 0.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **L1-2** | Periodic score broadcast (300 frames) | match-lifecycle L1-2 | `port/src/net/net.c` | 0.5 | — | L1-3, L1-4, L1-5, FIX-B.1 |
| **L1-3** | Gate HUD during endscreen | match-lifecycle L1-3 | `port/fast3d/pdgui_hud.cpp` | 0.25 | — | L1-2, L1-4, L1-5, FIX-B.1 |
| **L1-4** | Clear co-op netclient linkages at endscreen | match-lifecycle L1-4 | `port/fast3d/pdgui_bridge.c` | 0.25 | — | L1-2, L1-3, L1-5, FIX-B.1 |
| **L1-5** | Add NET_RESYNC_FLAG_SCORES to initial resync | match-lifecycle L1-5 | `port/src/net/net.c` | 0.25 | — | L1-2, L1-3, L1-4, FIX-B.1 |
| **FIX-B.1** | Deep manifest scanner (cinematics + AI scripts) | infra-repair FIX-B.1 | `port/src/net/netmanifest.c`, `src/game/setup.c` | 1.5 | FIX-B.2 (L0) | L1-2 through L1-5 |

**Collision notes**: L1-2 and L1-5 both touch `net.c` but at different locations (~line 738 vs ~line 1500+). Safe to do sequentially in the same session. L1-3 touches `pdgui_hud.cpp` (no other L1 item touches it). L1-4 touches `pdgui_bridge.c` (shared with F-0.4 in L0, but L0 is complete before L1 starts).

**Estimated Layer 1 total**: 1–2 sessions.

**Smoke-verify checkpoint after Layer 1**: 2-player online CombatSim — verify score panel stays in sync over 5 minutes. Load CI intro cutscene (B-118 test) — verify no crash (graceful fallback from FIX-B.2 + deep scanner from FIX-B.1). Verify HUD suppressed during endscreen.

---

### Layer 2: Spawn System

**Goal**: Universal spawn system with 4-layer fallback chain. Every map always spawns every player, no exceptions.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **S-1.1** | spawnpool.c/h type definitions | spawn-fix Phase 1 (1.1) | `src/game/spawnpool.c` (NEW), `src/include/game/spawnpool.h` (NEW) | 0.5 | — | S-1.6, S-1.7 |
| **S-1.2** | spawnPoolComputeAABB from g_BgRooms | spawn-fix Phase 1 (1.2) | `src/game/spawnpool.c` | 0.25 | S-1.1 | S-1.6, S-1.7 |
| **S-1.3** | L4 radial fallback | spawn-fix Phase 1 (1.3) | `src/game/spawnpool.c` | 0.25 | S-1.2 | — |
| **S-1.4** | spawnPoolBuild stub (L4 only) | spawn-fix Phase 1 (1.4) | `src/game/spawnpool.c` | 0.25 | S-1.3 | — |
| **S-1.5** | Wire into playerreset.c | spawn-fix Phase 1 (1.5) | `src/game/playerreset.c` | 0.25 | S-1.4 | — |
| **S-1.6** | Expand g_SpawnPoints[24→36] | spawn-fix Phase 1 (1.6) | `src/game/player.c`, `src/game/playerreset.c` | 0.25 | — | S-1.1, S-1.7 |
| **S-1.7** | match_seed in SVC_STAGE_START | spawn-fix Phase 1 (1.7) | `port/src/net/netmsg.c`, `port/include/net/net.h` | 0.25 | — | S-1.1, S-1.6 |
| **S-2.1** | spawnPointValidate (5-test validation) | spawn-fix Phase 2 (2.1) | `src/game/spawnpool.c` | 0.5 | S-1.1 | — |
| **S-2.2** | L3 grid sampling | spawn-fix Phase 2 (2.2) | `src/game/spawnpool.c` | 0.5 | S-1.2, S-2.1 | — |
| **S-2.3** | Integrate L3 into spawnPoolBuild | spawn-fix Phase 2 (2.3) | `src/game/spawnpool.c` | 0.25 | S-2.2 | — |
| **S-3.1** | Deterministic RNG (seeded from match_seed) | spawn-fix Phase 3 (3.1) | `src/game/spawnpool.c` | 0.25 | S-1.7 | — |
| **S-3.2** | Refactor waypoint sampling → spawnPoolL2 | spawn-fix Phase 3 (3.2) | `src/game/spawnpool.c`, `src/game/playerreset.c` | 0.5 | S-2.1, S-3.1 | — |
| **S-3.3** | Integrate L2 into spawnPoolBuild | spawn-fix Phase 3 (3.3) | `src/game/spawnpool.c` | 0.25 | S-3.2 | — |
| **S-4.1** | Farthest-point spawn selection (FFA) | spawn-fix Phase 4 (4.1) | `src/game/spawnpool.c` | 0.5 | S-1.1 | — |
| **S-4.2** | Team-aware sector partitioning | spawn-fix Phase 4 (4.2) | `src/game/spawnpool.c` | 0.5 | S-4.1 | — |
| **S-4.3** | Wire spawnPoolSelect into playerreset/bot | spawn-fix Phase 4 (4.3) | `src/game/playerreset.c`, `src/game/bot.c` | 0.25 | S-4.1 | — |

**Collision notes**: S-1.7 touches `netmsg.c` (match_seed in SVC_STAGE_START). This is a different message handler from anything in Layer 1. Safe. `playerreset.c` is touched by S-1.5, S-1.6, S-3.2, S-4.3 — these are sequential within the spawn track.

**Estimated Layer 2 total**: 3–4 sessions.

**Smoke-verify checkpoint after Layer 2**: Load Skedar Ruins with 31 bots — verify distributed spawns (not all stacked). Load a zero-pad test map — verify L4 radial generates spawns. 2-client online match — verify both clients produce identical spawn pools (deterministic RNG from match_seed).

**Spawn validation note**: The L1–L4 fallback chain now includes **raycast-budget validation at every tier**. Every candidate fires ~14 rays (6 cardinal + 8 diagonal); backface hit = reject (inside geometry); sum of ray distances must meet ≥ 10–20m threshold. L4 dilates its radial pattern until candidates pass; last resort = highest-budget candidates. Determinism seeded from `match_id ^ player_slot`. See [spawn-system-architecture-2026-04-13.md §3.0](spawn-system-architecture-2026-04-13.md) for the full algorithm.

---

### Layer 3: Mod Map Import Pipeline

**Goal**: Users can import external map directories into PD2's mod system and play matches on them.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **M-5.1** | mapimport.c/h + import_context_t | spawn-fix Phase 5 (5.1) | `port/src/mapimport.c` (NEW), `port/include/mapimport.h` (NEW) | 0.5 | — | — |
| **M-5.2** | mapImportNormalize (BG/pad validation) | spawn-fix Phase 5 (5.2) | `port/src/mapimport.c` | 0.5 | M-5.1 | — |
| **M-5.3** | mapImportGenerate (spawn pool + setup + mod.json) | spawn-fix Phase 5 (5.3) | `port/src/mapimport.c` | 1 | M-5.2, **Layer 2 complete** | — |
| **M-5.4** | mapImportEmit (atomic dir write) | spawn-fix Phase 5 (5.4) | `port/src/mapimport.c` | 0.5 | M-5.3 | — |
| **M-5.5** | mapImportValidate (smoke test loader) | spawn-fix Phase 5 (5.5) | `port/src/mapimport.c` | 1 | M-5.4 | — |
| **M-5.6** | mapImportRegister (modmgrReload trigger) | spawn-fix Phase 5 (5.6) | `port/src/mapimport.c` | 0.25 | M-5.5 | — |
| **M-6.1** | Import UI button in Modding Hub | spawn-fix Phase 6 (6.1) | `port/fast3d/pdgui_menu_moddinghub.cpp` | 0.25 | M-5.6 | — |
| **M-6.2** | Import dialog (dir select + progress + result) | spawn-fix Phase 6 (6.2) | `port/fast3d/pdgui_menu_moddinghub.cpp` | 1 | M-6.1 | — |
| **M-7.x** | Retroactive validation (all base + mod arenas) | spawn-fix Phase 7 | Various | 1 | Layer 2, M-5.5 | — |

**Collision notes**: `CMakeLists.txt` needs new source files added (mapimport.c, spawnpool.c). Batch these into one CMakeLists edit at the start of Layer 2.

**Estimated Layer 3 total**: 3–4 sessions.

---

### Layer 4: Menu & Input Consistency

**Goal**: Apply B-124 pattern uniformly, clean up SDL bypasses, ship the arena list upgrade and mission category headers.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **F-1.1** | Remove direct SDL_WarpMouseInWindow from pause | menu-input F-1.1 | `port/fast3d/pdgui_menu_pausemenu.cpp` | 0.25 | F-0.3 (L0) | F-1.2, F-1.4, F-2.1 |
| **F-1.2** | Add pdguiSoloMissionReset() | menu-input F-1.2 | `port/fast3d/pdgui_menu_solomission.cpp` | 0.25 | — | F-1.1, F-1.4, F-2.1 |
| **F-1.3** | Verify ad-hoc menus context (audit) | menu-input F-1.3 | Multiple (read-only audit) | 0.5 | — | All L4 |
| **F-1.4** | Clean up redundant SDL calls in inputctx.c | menu-input F-1.4 | `port/src/inputctx.c` | 0.25 | — | F-1.1, F-1.2, F-2.1 |
| **F-2.1** | Arena list alphabetized + collapsible sections | menu-input F-2.1 | `port/fast3d/pdgui_menu_room.cpp` | 1 | — | F-1.1, F-1.2, F-1.4 |
| **F-2.2** | Fix static init order in solo pause k_Btns | menu-input F-2.2 | `port/fast3d/pdgui_menu_solomission.cpp` | 0.25 | F-0.1 (L0) | F-2.1 |
| **FIX-D.1** | pdguiGetUIContext() centralized query | infra-repair FIX-D.1 | `port/fast3d/pdgui_hud.cpp` (or new header) | 0.5 | — | F-2.1 |
| **FIX-D.2** | B-60 stray 'g'+'s' text fix | infra-repair FIX-D.2 | `port/fast3d/pdgui_menu_settings.cpp` | 0.1 | — | All L4 |
| **FIX-D.3** | Killfeed show bot kills | infra-repair FIX-D.3 | `port/fast3d/pdgui_hud.cpp` | 0.25 | — | All L4 |
| **FIX-G** | Mission category headers (solo mission list) | infra-repair FIX-G | `port/fast3d/pdgui_menu_solomission.cpp` | 0.5 | F-0.1 (L0) | F-2.1 |
| **F-2.3** | Music restart robustness for CITRAINING | menu-input F-2.3 | `src/game/lv.c` | 0.25 | — | All L4 |

**Collision notes**: `pdgui_menu_solomission.cpp` is touched by F-1.2, F-2.2, and FIX-G — run these sequentially within the same session. `pdgui_hud.cpp` is touched by FIX-D.1 and FIX-D.3 — same session, sequential. `pdgui_menu_pausemenu.cpp` was touched by F-0.3 in L0; F-1.1 in L4 is safe (different section, L0 complete).

**Estimated Layer 4 total**: 2–3 sessions. Use Opus 1M + extended thinking (per feedback_d5_p3_model) for F-2.1 arena list work — it touches the arena data pipeline and needs careful verification.

**Smoke-verify checkpoint after Layer 4**: Open Combat Sim room, verify arena picker shows 3 collapsible sections (MP Arenas, Campaign Maps, Mod Maps), alphabetical within each. Open Solo Missions — verify category headers. Verify no stray 'g'/'s' in Settings. Kill a bot — verify killfeed shows it. Open and close every menu 10 times — verify no opacity stacking, no stuck input.

---

### Layer 5: Match Lifecycle Major

**Goal**: Co-op/Counter-Op manifest integration, protocol bump to v34, transient mod enablement, polish.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **L2-1** | Build manifest for co-op/anti matches | match-lifecycle L2-1 | `port/src/net/netmsg.c`, `port/src/net/netmanifest.c` | 1 | L1-1 (L0) | — |
| **L2-2** | Add ready gate to co-op/anti | match-lifecycle L2-2 | `port/src/net/netmsg.c` | 1 | L2-1 | — |
| **L2-3** | Add CLC_STAGE_READY to co-op client | match-lifecycle L2-3 | `port/src/net/netmsg.c` | 0.5 | L2-2 | — |
| **L2-4** | Unify SVC_STAGE_START client handling | match-lifecycle L2-4 | `port/src/net/netmsg.c` | 1 | L2-3 | — |
| **L2-5** | Protocol version bump (v33→v34) | match-lifecycle L2-5 | `port/include/net/net.h` | 0.1 | L2-1 through L2-4 | — |
| **L3-1** | mod_transient_session_t structure | match-lifecycle L3-1 | `port/include/modmgr.h`, `port/src/modmgr.c` | 0.5 | — | L2-1 through L2-4 |
| **L3-2** | Extend CLC_CATALOG_DIFF with disabled mod list | match-lifecycle L3-2 | `port/src/net/netdistrib.c`, `port/src/net/netmsg.c` | 0.5 | L2-5, L3-1 | — |
| **L3-3** | Add action_hint to SVC_MATCH_MANIFEST | match-lifecycle L3-3 | `port/src/net/netmsg.c`, `port/src/net/netmanifest.c` | 0.5 | L3-2 | — |
| **L3-4** | Client transient enable on manifest receive | match-lifecycle L3-4 | `port/src/net/netmsg.c` | 0.5 | L3-1, L3-3 | — |
| **L3-5** | Restore prior mod state on match end/disconnect | match-lifecycle L3-5 | `port/src/net/netmsg.c`, `port/src/net/net.c`, `port/src/modmgr.c` | 0.5 | L3-4 | — |
| **L3-6** | pd.ini crash-safe persistence | match-lifecycle L3-6 | `port/src/modmgr.c`, `port/src/config.c` | 0.5 | L3-4 | — |
| **L4-1** | Periodic timer sync (SVC_TIMER_SYNC) | match-lifecycle L4-1 | `port/include/net/netmsg.h`, `port/src/net/netmsg.c`, `port/src/net/net.c` | 0.5 | — | L3-1 through L3-6 |
| **L4-2** | Automatic .temp mod cleanup on match end | match-lifecycle L4-2 | `port/src/net/netdistrib.c` | 0.5 | L3-5 | — |
| **L4-3** | Consolidate endscreen exit paths | match-lifecycle L4-3 | `port/fast3d/pdgui_menu_endscreen.cpp`, `port/fast3d/pdgui_bridge.c` | 0.5 | L1-3, L1-4 (L1) | — |
| **L4-4** | Local manifest for solo CombatSim + mods | match-lifecycle L4-4 | `port/src/net/matchsetup.c`, `port/src/net/netmanifest.c` | 0.5 | — | All L5 |

**Collision notes**: `netmsg.c` is heavily touched throughout Layer 5 (L2-1 through L2-4, L3-2 through L3-5, L4-1). These are all sequential within the match lifecycle track — **do not parallelize items that touch netmsg.c**. `net.c` is touched by L3-5 and L4-1 — sequential. `modmgr.c` is touched by L3-1, L3-5, L3-6 — sequential.

**Protocol bump isolation**: L2-5 is the single commit that bumps `NET_PROTOCOL_VER` from 33 to 34. All wire format changes (L2-1 through L2-4, L3-2, L3-3, L4-1) must land before or with this commit. **Cut a playable build immediately before the protocol bump** so Mike has a v33-compatible binary for testing against existing servers.

**Estimated Layer 5 total**: 5–7 sessions.

**Smoke-verify checkpoint after Layer 5**: 2-player co-op online — verify manifest sent, ready gate works, stage loads on both clients. Host a match with a mod that client has disabled — verify transient enable, then restore after match end. Timer drift test: run 10-minute match, check client timer within 1 second of server.

---

### Layer 6: Rendering Polish

**Goal**: Eliminate render state leakage class entirely.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **FIX-C.1** | GBI frame-start state reset | infra-repair FIX-C.1 | `src/game/sky.c` or `src/game/lv.c` | 0.5 | — | FIX-C.3 |
| **FIX-C.2** | B-18 Skedar Ruins investigation | infra-repair FIX-C.2 | TBD (sky type/palette data) | 0.5 | FIX-C.1 + playtest | — |
| **FIX-C.3** | Haze overlay lifecycle fix (opacity stacking) | infra-repair FIX-C.3 | `port/fast3d/pdgui_*` (haze overlay) | 0.25 | — | FIX-C.1 |

**Estimated Layer 6 total**: 1 session.

**Note**: Layer 6 can start as early as after Layer 0 — it has no dependency on Layers 1–5. Placed here in the plan because rendering polish is lower priority than networking and spawn correctness. Mike can pull it earlier if B-18 is annoying.

---

### Layer 7: Supporting Fixes

**Goal**: Update system robustness and mission flow polish.

| ID | Title | Source | Primary Files | Est. Sessions | Prerequisites | Parallel-Safe With |
|----|-------|--------|---------------|---------------|---------------|-------------------|
| **FIX-F.1** | Rate limit detection + error classification | infra-repair FIX-F | `port/src/updater.c` | 0.5 | — | FIX-F.2, FIX-F.3, FIX-G |
| **FIX-F.2** | Path resolution via fsFullPath | infra-repair FIX-F | `port/src/updater.c` | 0.25 | — | FIX-F.1, FIX-F.3, FIX-G |
| **FIX-F.3** | Extraction verification (SHA-256 check) | infra-repair FIX-F | `port/src/updater.c` | 0.25 | — | FIX-F.1, FIX-F.2, FIX-G |
| **F-3.1** | Duplicate rejection in menuPushDialog | menu-input F-3.1 | `src/game/menu.c` | 0.25 | — | All L7 |
| **F-3.2** | Logging on menuPopDialog underflow | menu-input F-3.2 | `src/game/menu.c` | 0.1 | — | All L7 |

**Note**: FIX-G (mission category headers) is in Layer 4 with the other menu work — it shares `pdgui_menu_solomission.cpp` with F-0.1, F-1.2, and F-2.2.

**Estimated Layer 7 total**: 1 session.

**Note**: Layer 7 is fully independent of all other layers. Can run at any time. Good candidate for a "cool-down" session between major layers.

---

## Parallel-Safe Batches per Layer — Collision Map

### Hot Files (touched by multiple tracks)

| File | Touched By | Collision Risk |
|------|-----------|----------------|
| `port/src/net/netmsg.c` | L1-1 (L0), S-1.7 (L2), L2-1/L2-2/L2-3/L2-4 (L5), L3-2/L3-3/L3-4/L3-5 (L5), L4-1 (L5) | **HIGH** — all netmsg touches must be serialized within their layer |
| `port/src/net/net.c` | L1-2/L1-5 (L1), L3-5 (L5), L4-1 (L5) | **MEDIUM** — L1 items at different locations; L5 items sequential |
| `port/fast3d/pdgui_hud.cpp` | L1-3 (L1), FIX-D.1/FIX-D.3 (L4) | **LOW** — different layers, never concurrent |
| `port/fast3d/pdgui_menu_pausemenu.cpp` | F-0.3 (L0), F-1.1 (L4) | **LOW** — different layers |
| `port/fast3d/pdgui_menu_solomission.cpp` | F-0.1 (L0), F-1.2/F-2.2/FIX-G (L4) | **LOW** — different layers |
| `port/fast3d/pdgui_bridge.c` | F-0.4 (L0), L1-4 (L1), L4-3 (L5) | **LOW** — different layers |
| `CMakeLists.txt` | L0-BUILD (L0), spawnpool.c add (L2), mapimport.c add (L3) | **LOW** — batch all source-file additions into one edit at start of L2 |
| `port/src/modmgr.c` | L3-1/L3-5/L3-6 (L5) | **MEDIUM** — sequential within L5 |
| `src/game/playerreset.c` | S-1.5/S-1.6/S-3.2/S-4.3 (L2) | **MEDIUM** — sequential within L2 |

### Safe Parallel Batches

**Within Layer 0**: All 10 items are parallel-safe (no shared files).

**Within Layer 1**: L1-2/L1-5 share `net.c` — do in same session sequentially. L1-3, L1-4, FIX-B.1 are each in separate files — parallel-safe with each other and with L1-2/L1-5.

**Within Layer 2**: S-1.1/S-1.6/S-1.7 parallel (different files). Then S-1.2→S-1.3→S-1.4→S-1.5 sequential (spawnpool.c chain). Then S-2.1→S-2.2→S-2.3 sequential. Then S-3.1→S-3.2→S-3.3 sequential. Then S-4.1→S-4.2→S-4.3 sequential.

**Within Layer 4**: F-1.1/F-1.2/F-1.4/FIX-D.2/FIX-D.3/F-2.3 all parallel (different files). F-2.1 parallel with F-1.x (different file). F-2.2 and FIX-G sequential with F-1.2 (same file: solomission.cpp). FIX-D.1 and FIX-D.3 sequential (same file: pdgui_hud.cpp).

**Within Layer 5**: L2-1→L2-2→L2-3→L2-4→L2-5 strictly sequential (netmsg.c chain). L3-1 can start in parallel with L2 work (different file: modmgr.c). L3-2 through L3-6 sequential after L2-5. L4-1 sequential with L3-5 (net.c). L4-3 and L4-4 parallel with each other and with L3 work (different files). L4-2 after L3-5.

**Cross-layer parallelism**: Layers 6 and 7 can run during any other layer (no shared files). Layer 4 can start during Layer 2 (no shared files between spawn and menu tracks). Layer 3 depends on Layer 2 completion (M-5.3 needs spawnPoolBuild).

---

## Execution Cadence

### Session Model

Each "session" is one focused Opus 1M + extended thinking pass (per feedback_d5_p3_model). Typical output: 1–3 files changed, 50–300 lines delta. Build-verify at end of every session.

### Recommended Sequence

```
Session S230:  L0-BUILD + L0-LINK (build blocker resolution)
               Smoke-verify: warm build < 12s

Session S231:  FIX-A (chr tick isolation — the big one)
               This is a 2-session item; may span S231–S232

Session S232:  FIX-A continued + F-0.1 + F-0.2 + F-0.3 + F-0.4 + L1-1 + FIX-B.2 + CLOSE
               (All remaining L0 items — each is < 0.5 session)
               Smoke-verify: 31-bot stability + campaign labels + input context pops

Session S233:  L1-2 + L1-5 (net.c, same session) + L1-3 + L1-4
               Smoke-verify: score sync + HUD gating + co-op linkage clear

Session S234:  FIX-B.1 (deep manifest scanner)
               Smoke-verify: CI intro cutscene loads without crash

Session S235:  S-1.1 + S-1.6 + S-1.7 (parallel foundation)
               S-1.2 → S-1.3 → S-1.4 → S-1.5 (L4 fallback chain)
               CMakeLists.txt: add spawnpool.c + mapimport.c
               Smoke-verify: zero-pad map spawns via L4

  ┌─ Parallel track A ──────────────────────────────
  │
Session S236:  S-2.1 → S-2.2 → S-2.3 (L3 grid sampling)
Session S237:  S-3.1 → S-3.2 → S-3.3 (L2 waypoints + deterministic RNG)
Session S238:  S-4.1 → S-4.2 → S-4.3 (spawn selection)
  │
  ├─ Parallel track B (can run during S236–S238) ──
  │
Session S236b: F-1.1 + F-1.2 + F-1.4 + FIX-D.2 + FIX-D.3 + F-2.3 (L4 parallel batch)
Session S237b: F-2.1 (arena list — full session, Opus 1M + extended thinking)
Session S238b: F-2.2 + FIX-G + FIX-D.1 (solomission.cpp + hud.cpp sequential)
  │
  ├─ Parallel track C (can run during S236–S238) ──
  │
Session S236c: FIX-C.1 + FIX-C.3 (rendering polish, parallel)
Session S237c: FIX-F.1 + FIX-F.2 + FIX-F.3 + F-3.1 + F-3.2 (Layer 7, all parallel)
  │
  └─────────────────────────────────────────────────

               *** SMOKE-VERIFY CHECKPOINT ***
               Skedar Ruins 31-bot spawns distributed
               Arena picker 3 sections + alphabetical
               No render state leakage
               Updater error messages correct

    *** CUT PLAYABLE BUILD (v33 protocol) ***
    This is the last build before protocol bump.

Session S239:  M-5.1 → M-5.2 → M-5.3 (map import parse + normalize + generate)
Session S240:  M-5.4 → M-5.5 → M-5.6 (emit + validate + register)
Session S241:  M-6.1 → M-6.2 (import UI in modding hub)
Session S242:  M-7.x (retroactive validation — all arenas)

               Smoke-verify: import Chicago as MP arena, play 30s match

Session S243:  L2-1 → L2-2 (co-op manifest + ready gate)
Session S244:  L2-3 → L2-4 (co-op CLC_STAGE_READY + unify handlers)
Session S245:  L2-5 (protocol bump v34) + L3-1 (transient session struct)
               *** PROTOCOL BUMP — old clients incompatible ***

Session S246:  L3-2 → L3-3 → L3-4 (catalog diff + action hint + transient enable)
Session S247:  L3-5 + L3-6 + L4-2 (restore + persistence + temp cleanup)
Session S248:  L4-1 + L4-3 + L4-4 (timer sync + endscreen consolidation + local manifest)

               *** SMOKE-VERIFY CHECKPOINT ***
               Co-op manifest flow end-to-end
               Transient mod enable/restore
               Timer sync within 1s over 10 minutes

Session S249:  FIX-C.2 (B-18 Skedar Ruins investigation — needs playtest data)
Session S250:  F-1.3 (ad-hoc menu context audit — read-only pass)
               F-3.3 (B-92 deferred flush review — needs solo mission playtest)

               *** FINAL SMOKE-VERIFY ***
```

### When to Cut Playable Builds

1. **After Layer 0**: Stability checkpoint. 31-bot matches should survive 10+ minutes.
2. **After Layers 1+2+4** (parallel tracks complete): Feature-complete CombatSim build with working spawns, sorted arena list, category headers. **Last v33-protocol build.**
3. **After Layer 5**: Protocol v34 build with co-op manifest and transient mods.
4. **After all layers**: Release candidate.

### Protocol Bump Isolation

The v34 protocol bump (L2-5) is a hard break — old clients cannot connect to new servers and vice versa. All wire format changes must be batched:
- L2-1 through L2-4: co-op manifest + ready gate + stage ready + handler unification
- L3-2: extended CLC_CATALOG_DIFF
- L3-3: action_hint in SVC_MATCH_MANIFEST
- L4-1: SVC_TIMER_SYNC (new message type)

These all land in Sessions S243–S248. The bump itself is a single line change in `net.h` at Session S245.

---

## Full Item Table (All Tracks)

| ID | Title | Layer | Primary Files | Sessions | Prereqs | Parallel-Safe With |
|----|-------|-------|---------------|----------|---------|-------------------|
| L0-BUILD | Warm-ccache sloppiness tweak | 0 | CMakeLists.txt | 0.25 | — | All L0 |
| L0-LINK | pdguiThemeRegisterModDir verify | 0 | pdgui_theme.cpp | 0.25 | — | All L0 |
| FIX-A | Chr tick isolation + lifetime tokens | 0 | chr.c, chraction.c, crash.c, participant.c | 2 | — | All L0 |
| F-0.1 | Campaign language-bank shadow fix | 0 | pdgui_menu_solomission.cpp | 0.5 | — | All L0 |
| F-0.2 | Context leak: training menu | 0 | pdgui_menu_training.cpp | 0.25 | — | All L0 |
| F-0.3 | Context leak: game-over panels | 0 | pdgui_menu_pausemenu.cpp | 0.25 | — | All L0 |
| F-0.4 | Stale manifest on return-to-room | 0 | pdmain.c / pdgui_bridge.c | 0.25 | — | All L0 |
| L1-1 | Clear g_ClientManifest on match end | 0 | netmsg.c | 0.25 | — | All L0 |
| FIX-B.2 | Graceful fallback for missing models | 0 | setup.c, fileload | 0.5 | — | All L0 |
| CLOSE | Close B-72, B-21 in bugs.md | 0 | bugs.md | 0.1 | — | All L0 |
| L1-2 | Periodic score broadcast | 1 | net.c | 0.5 | — | L1-3, L1-4, FIX-B.1 |
| L1-3 | Gate HUD during endscreen | 1 | pdgui_hud.cpp | 0.25 | — | L1-2, L1-4, L1-5, FIX-B.1 |
| L1-4 | Clear co-op netclient linkages | 1 | pdgui_bridge.c | 0.25 | — | L1-2, L1-3, L1-5, FIX-B.1 |
| L1-5 | Add scores to initial resync flags | 1 | net.c | 0.25 | — | L1-3, L1-4, FIX-B.1 |
| FIX-B.1 | Deep manifest scanner | 1 | netmanifest.c, setup.c | 1.5 | FIX-B.2 | L1-2 through L1-5 |
| S-1.1 | spawnpool.c/h type definitions | 2 | spawnpool.c (NEW), spawnpool.h (NEW) | 0.5 | — | S-1.6, S-1.7 |
| S-1.2 | spawnPoolComputeAABB | 2 | spawnpool.c | 0.25 | S-1.1 | — |
| S-1.3 | L4 radial fallback | 2 | spawnpool.c | 0.25 | S-1.2 | — |
| S-1.4 | spawnPoolBuild stub (L4 only) | 2 | spawnpool.c | 0.25 | S-1.3 | — |
| S-1.5 | Wire spawnpool into playerreset | 2 | playerreset.c | 0.25 | S-1.4 | — |
| S-1.6 | Expand g_SpawnPoints[24→36] | 2 | player.c, playerreset.c | 0.25 | — | S-1.1, S-1.7 |
| S-1.7 | match_seed in SVC_STAGE_START | 2 | netmsg.c, net.h | 0.25 | — | S-1.1, S-1.6 |
| S-2.1 | spawnPointValidate (5-test) | 2 | spawnpool.c | 0.5 | S-1.1 | — |
| S-2.2 | L3 grid sampling | 2 | spawnpool.c | 0.5 | S-1.2, S-2.1 | — |
| S-2.3 | Integrate L3 into spawnPoolBuild | 2 | spawnpool.c | 0.25 | S-2.2 | — |
| S-3.1 | Deterministic RNG | 2 | spawnpool.c | 0.25 | S-1.7 | — |
| S-3.2 | Refactor waypoints → spawnPoolL2 | 2 | spawnpool.c, playerreset.c | 0.5 | S-2.1, S-3.1 | — |
| S-3.3 | Integrate L2 into spawnPoolBuild | 2 | spawnpool.c | 0.25 | S-3.2 | — |
| S-4.1 | Farthest-point spawn select (FFA) | 2 | spawnpool.c | 0.5 | S-1.1 | — |
| S-4.2 | Team-aware sector partitioning | 2 | spawnpool.c | 0.5 | S-4.1 | — |
| S-4.3 | Wire spawnPoolSelect | 2 | playerreset.c, bot.c | 0.25 | S-4.1 | — |
| M-5.1 | mapimport.c/h + import_context_t | 3 | mapimport.c (NEW), mapimport.h (NEW) | 0.5 | — | — |
| M-5.2 | mapImportNormalize | 3 | mapimport.c | 0.5 | M-5.1 | — |
| M-5.3 | mapImportGenerate | 3 | mapimport.c | 1 | M-5.2, Layer 2 | — |
| M-5.4 | mapImportEmit | 3 | mapimport.c | 0.5 | M-5.3 | — |
| M-5.5 | mapImportValidate | 3 | mapimport.c | 1 | M-5.4 | — |
| M-5.6 | mapImportRegister | 3 | mapimport.c | 0.25 | M-5.5 | — |
| M-6.1 | Import UI button | 3 | pdgui_menu_moddinghub.cpp | 0.25 | M-5.6 | — |
| M-6.2 | Import dialog | 3 | pdgui_menu_moddinghub.cpp | 1 | M-6.1 | — |
| M-7.x | Retroactive validation | 3 | Various | 1 | Layer 2, M-5.5 | — |
| F-1.1 | Remove SDL_WarpMouseInWindow | 4 | pdgui_menu_pausemenu.cpp | 0.25 | F-0.3 | F-1.2, F-1.4, F-2.1 |
| F-1.2 | pdguiSoloMissionReset() | 4 | pdgui_menu_solomission.cpp | 0.25 | — | F-1.1, F-1.4, F-2.1 |
| F-1.3 | Verify ad-hoc menus (audit) | 4 | Multiple (read-only) | 0.5 | — | All L4 |
| F-1.4 | Clean redundant SDL in inputctx.c | 4 | inputctx.c | 0.25 | — | F-1.1, F-1.2, F-2.1 |
| F-2.1 | Arena list alphabetized + sections | 4 | pdgui_menu_room.cpp | 1 | — | F-1.x |
| F-2.2 | Fix static init k_Btns | 4 | pdgui_menu_solomission.cpp | 0.25 | F-0.1 | F-2.1 |
| FIX-D.1 | pdguiGetUIContext() | 4 | pdgui_hud.cpp (or new) | 0.5 | — | F-2.1 |
| FIX-D.2 | B-60 stray text fix | 4 | pdgui_menu_settings.cpp | 0.1 | — | All L4 |
| FIX-D.3 | Killfeed bot kills | 4 | pdgui_hud.cpp | 0.25 | — | All L4 |
| FIX-G | Mission category headers | 4 | pdgui_menu_solomission.cpp | 0.5 | F-0.1 | F-2.1 |
| F-2.3 | Music restart robustness | 4 | lv.c | 0.25 | — | All L4 |
| L2-1 | Build co-op/anti manifest | 5 | netmsg.c, netmanifest.c | 1 | L1-1 | — |
| L2-2 | Add ready gate to co-op | 5 | netmsg.c | 1 | L2-1 | — |
| L2-3 | CLC_STAGE_READY for co-op | 5 | netmsg.c | 0.5 | L2-2 | — |
| L2-4 | Unify SVC_STAGE_START handling | 5 | netmsg.c | 1 | L2-3 | — |
| L2-5 | Protocol bump v33→v34 | 5 | net.h | 0.1 | L2-1:L2-4 | — |
| L3-1 | mod_transient_session_t | 5 | modmgr.h, modmgr.c | 0.5 | — | L2-1:L2-4 |
| L3-2 | Extend CLC_CATALOG_DIFF | 5 | netdistrib.c, netmsg.c | 0.5 | L2-5, L3-1 | — |
| L3-3 | action_hint in manifest | 5 | netmsg.c, netmanifest.c | 0.5 | L3-2 | — |
| L3-4 | Client transient enable | 5 | netmsg.c | 0.5 | L3-1, L3-3 | — |
| L3-5 | Restore mod state on end | 5 | netmsg.c, net.c, modmgr.c | 0.5 | L3-4 | — |
| L3-6 | pd.ini crash-safe persistence | 5 | modmgr.c, config.c | 0.5 | L3-4 | — |
| L4-1 | Periodic timer sync | 5 | netmsg.h, netmsg.c, net.c | 0.5 | — | L3-1:L3-6 |
| L4-2 | .temp mod cleanup | 5 | netdistrib.c | 0.5 | L3-5 | — |
| L4-3 | Consolidate endscreen exits | 5 | pdgui_menu_endscreen.cpp, pdgui_bridge.c | 0.5 | L1-3, L1-4 | L4-4 |
| L4-4 | Local manifest for solo+mods | 5 | matchsetup.c, netmanifest.c | 0.5 | — | L4-3 |
| FIX-C.1 | GBI frame-start state reset | 6 | sky.c / lv.c | 0.5 | — | FIX-C.3 |
| FIX-C.2 | B-18 Skedar Ruins investigation | 6 | TBD | 0.5 | FIX-C.1 + playtest | — |
| FIX-C.3 | Haze overlay lifecycle fix | 6 | pdgui overlay | 0.25 | — | FIX-C.1 |
| FIX-F.1 | Rate limit detection | 7 | updater.c | 0.5 | — | All L7 |
| FIX-F.2 | Path resolution (fsFullPath) | 7 | updater.c | 0.25 | — | All L7 |
| FIX-F.3 | Extraction verify (SHA-256) | 7 | updater.c | 0.25 | — | All L7 |
| F-3.1 | menuPushDialog duplicate rejection | 7 | menu.c | 0.25 | — | All L7 |
| F-3.2 | menuPopDialog underflow logging | 7 | menu.c | 0.1 | — | All L7 |

---

## Totals

| Layer | Items | Est. Sessions | Can Ship Independently? | Protocol Change? |
|-------|-------|---------------|------------------------|-----------------|
| 0 — Early Fixes | 10 | 2–3 | YES | NO |
| 1 — Net Safety | 5 | 1–2 | YES (after L0) | NO |
| 2 — Spawn System | 16 | 3–4 | YES (after L0) | NO |
| 3 — Map Import | 9 | 3–4 | YES (after L2) | NO |
| 4 — Menu & Input | 12 | 2–3 | YES (after L0) | NO |
| 5 — Match Lifecycle | 15 | 5–7 | YES (after L1) | YES (v34) |
| 6 — Rendering Polish | 3 | 1 | YES (after L0) | NO |
| 7 — Supporting | 5 | 1 | YES (anytime) | NO |
| **TOTAL** | **75** | **~20–25** | | |

**Grand total**: 75 items across 8 layers, estimated 20–25 focused sessions.

---

## Context Updates Required

When execution begins, update:
1. `context/infrastructure.md`: D7 → ABANDONED (ethical decision, proprietary SDK, 2026-04-13)
2. `context/systemic-bugs.md`: Add SP-10, SP-11, SP-12
3. `context/bugs.md`: Close B-72, B-21
4. `context/tasks-current.md`: Point to this plan as the active punch list

---

*End of master orchestration plan. 75 items, 8 layers, ~20–25 sessions. No code changes in this document.*
