# Match Lifecycle Fix Plan

> **Date**: 2026-04-13, Session S226
> **Companion to**: [match-lifecycle-architecture-audit-2026-04-13.md](match-lifecycle-architecture-audit-2026-04-13.md)
> **Status**: Approved plan -- awaiting implementation
> **Recommendation**: Targeted fix set + focused co-op rework (NOT full top-down rework)

---

## Layered Execution Plan

Four layers, each independently shippable. Each layer produces a working build. Dependencies are strictly downward (Layer N depends on Layer N-1).

---

## Layer 1: Safety Fixes (No Protocol Changes)

**Goal**: Eliminate state leaks and sync gaps in the existing system. Zero protocol changes, zero wire format changes. Can ship immediately.

### L1-1: Clear g_ClientManifest on match end

**Subsystem**: Manifest lifecycle
**Symptom**: Stale manifest from match N persists in `g_ClientManifest` until match N+1's `SVC_MATCH_MANIFEST` arrives. If any code path triggers `mainChangeToStage()` for gameplay between matches without a new manifest, wrong assets get loaded.
**Change**: Add `manifestClear(&g_ClientManifest)` to `netmsgSvcStageEndRead()` after `sessionCatalogTeardown()`.
**File**: `port/src/net/netmsg.c` -- in `SVC_STAGE_END` handler, after line ~1391
**Risk**: LOW -- the manifest is fully rebuilt on next match start anyway. This just removes a latent hazard.
**Dependencies**: None

### L1-2: Periodic score broadcast

**Subsystem**: State sync (scores)
**Symptom**: Scores are event-driven only (mutated locally on death events). A lost `SVC_PLAYER_STATS` death packet causes permanent score divergence between server and client until reconnect.
**Change**: In `netEndFrame()` (net.c), add a periodic `SVC_PLAYER_SCORES` broadcast every 300 frames (5 seconds) when `g_Vars.mplayerisrunning`. Use the existing `netmsgSvcPlayerScoresWrite()` function.
**File**: `port/src/net/net.c` -- in `netEndFrame()`, after the existing periodic broadcast section (~line 1500+)
**Risk**: LOW -- `SVC_PLAYER_SCORES` already exists and is tested (reconnect path). Adding periodic sends is pure bandwidth cost (~200 bytes per broadcast).
**Dependencies**: None

### L1-3: Gate HUD during endscreen

**Subsystem**: HUD rendering
**Symptom**: `pdguiHudRender()` draws score panel, timer, and killfeed during the endscreen because `normmplayerisrunning` stays true. Occluded by endscreen backdrop but wasteful.
**Change**: Add `g_MainIsEndscreen` check to the HUD render guard. Change from `if (normmplayerisrunning)` to `if (normmplayerisrunning && !g_MainIsEndscreen)`.
**File**: `port/fast3d/pdgui_hud.cpp` -- render entry point (~line 168)
**Risk**: LOW -- purely visual. `g_MainIsEndscreen` is already used elsewhere as a guard.
**Dependencies**: None

### L1-4: Clear co-op player-netclient linkages at endscreen exit

**Subsystem**: Network state
**Symptom**: Server-side co-op/anti keeps `ncl->player` linkages alive after `SVC_STAGE_END` for endscreen display. These stale pointers persist until `playermgrReset()` in the next stage load.
**Change**: In the endscreen exit path (`pdguiEndscreenExitToMainMenu()` and `pdguiEndscreenStartMission()`), NULL out `ncl->player` and `ncl->config` for all netclients when in co-op/anti mode.
**File**: `port/fast3d/pdgui_bridge.c` -- in exit functions (~line 750-790)
**Risk**: LOW -- the linkages are already stale at this point. The endscreen is done with them.
**Dependencies**: None

### L1-5: Add NET_RESYNC_FLAG_SCORES to initial resync

**Subsystem**: State sync (scores)
**Symptom**: `net.c:738` sets `NET_RESYNC_FLAG_CHRS | NET_RESYNC_FLAG_PROPS` at match start but NOT `NET_RESYNC_FLAG_SCORES`. Late-joining clients (or clients that miss early kills) never get a score correction until they trigger a manual resync.
**Change**: Add `NET_RESYNC_FLAG_SCORES` to the initial resync flags at net.c:738.
**File**: `port/src/net/net.c` -- line ~738
**Risk**: LOW -- adds one extra reliable packet at match start.
**Dependencies**: None

---

## Layer 2: Co-op/Counter-Op Manifest Integration (Protocol Bump)

**Goal**: Route Co-op and Counter-Op through the manifest pipeline so mod content is verified before match start. Requires protocol version bump.

### L2-1: Build manifest for co-op/anti matches

**Subsystem**: Manifest construction
**Symptom**: `netServerCoopStageStart()` bypasses `manifestBuild()` entirely. Co-op clients receive no asset verification.
**Change**: Before `mainChangeToStage()` in the co-op/anti path (netmsg.c:4633-4641), call `manifestBuild()` to construct a manifest from the mission config (stage, player body/head, known NPC models, enabled mods). Then broadcast `SVC_MATCH_MANIFEST`.
**Files**: `port/src/net/netmsg.c` (CLC_LOBBY_START handler, co-op branch), `port/src/net/netmanifest.c` (new `manifestBuildMissionForNetwork()` function)
**Risk**: MEDIUM -- co-op manifest needs to include mission-specific assets (NPC bodies, setup props) which are only known from the setup file. Phase 1: include stage + player body/head + enabled mods. Phase 2: add NPC manifest from setup file scan (like SP manifest Phase 2 in manifest-architecture.md).
**Dependencies**: L1-1 (manifest clear)

### L2-2: Add ready gate to co-op/anti

**Subsystem**: Match startup
**Symptom**: Co-op/anti starts instantly -- server sends SVC_STAGE_START without waiting for clients to check assets. Clients with missing mods crash or get white textures.
**Change**: After sending `SVC_MATCH_MANIFEST` in L2-1, enter the same `s_ReadyGate` state machine used by CombatSim. Wait for all co-op clients to send `CLC_MANIFEST_STATUS(READY)` before calling `netServerCoopStageStart()`.
**Files**: `port/src/net/netmsg.c` (co-op branch of CLC_LOBBY_START handler)
**Risk**: MEDIUM -- the ready gate already works for CombatSim. Reusing it for co-op requires extracting the gate logic from the CS-specific code path into a shared function.
**Dependencies**: L2-1

### L2-3: Add CLC_STAGE_READY to co-op client

**Subsystem**: Match startup handshake
**Symptom**: Co-op client does not send `CLC_STAGE_READY` after loading. Server has no confirmation that the co-op client has loaded the stage.
**Change**: In the co-op branch of `netmsgSvcStageStartRead()` (netmsg.c:1143-1178), add `CLC_STAGE_READY` send after `mainChangeToStage()`, matching the CombatSim client path.
**File**: `port/src/net/netmsg.c` -- co-op SVC_STAGE_START handler
**Risk**: LOW -- the message type already exists. The server already handles it.
**Dependencies**: L2-2 (gate must exist to receive the ready)

### L2-4: Unify SVC_STAGE_START client handling

**Subsystem**: Match startup
**Symptom**: CombatSim and Co-op have separate handler paths in `netmsgSvcStageStartRead()` (netmsg.c:1143 vs 1179). The co-op path skips `mpStartMatch()`, `scenarioInitProps()`, and `CLC_STAGE_READY`. This creates ongoing divergence risk.
**Change**: Extract common post-load logic (context pop, menu stop, stage ready send) into a shared function. Keep mode-specific logic (participant setup for CS, mission config for co-op) separate. The co-op path should NOT call `mpStartMatch()` (that's correct -- it uses mission config), but should call the shared post-load function.
**Files**: `port/src/net/netmsg.c`
**Risk**: MEDIUM -- refactor of a critical code path. Must be tested across all three modes.
**Dependencies**: L2-3

### L2-5: Protocol version bump

**Subsystem**: Wire protocol
**Change**: Bump `NET_PROTOCOL_VER` for the extended `SVC_MATCH_MANIFEST` (mod requirements list) and extended `CLC_CATALOG_DIFF` (disabled mod list). Version 34 (current is 33).
**File**: `port/include/net/net.h`
**Risk**: LOW -- hard protocol break is acceptable for pre-1.0.
**Dependencies**: L2-1 through L2-4

---

## Layer 3: Transient Mod Enablement

**Goal**: Implement Mike's mod enablement spec: transiently enable disabled mods for match duration, restore prior state at match end.

### L3-1: mod_transient_session_t structure

**Subsystem**: Mod manager
**Change**: Add `mod_transient_session_t` struct (array of `{mod_id, was_enabled}` pairs + active flag) as a global. Provides save/restore API: `modTransientSave()`, `modTransientRestore()`.
**Files**: `port/include/modmgr.h`, `port/src/modmgr.c`
**Risk**: LOW -- pure data structure addition.
**Dependencies**: None (can be coded alongside Layer 2)

### L3-2: Extend CLC_CATALOG_DIFF with disabled mod list

**Subsystem**: Network distribution
**Symptom**: Server has no way to know which mods a client HAS but has DISABLED. Current diff only reports missing mods.
**Change**: In `CLC_CATALOG_DIFF` write path, append list of locally-present but disabled mod IDs. Server reads and stores per-client.
**Files**: `port/src/net/netdistrib.c` (write + read), `port/src/net/netmsg.c` (server-side storage)
**Risk**: MEDIUM -- extends a wire message. Must be behind the protocol bump from L2-5.
**Dependencies**: L2-5 (protocol bump), L3-1

### L3-3: Add action_hint to SVC_MATCH_MANIFEST

**Subsystem**: Manifest protocol
**Change**: For each mod entry in the manifest, include a per-client `action_hint` byte: 0=HAVE_AND_ENABLED, 1=HAVE_BUT_DISABLED (transient enable), 2=MISSING (need transfer). Server computes from client's catalog diff + disabled list.
**Files**: `port/src/net/netmsg.c` (SVC_MATCH_MANIFEST write), `port/src/net/netmanifest.c` (manifest build)
**Risk**: MEDIUM -- extends wire message. The hint is per-client, so the manifest is sent individually per client (not broadcast). This changes the send pattern from broadcast to per-client unicast.
**Dependencies**: L3-2

### L3-4: Client transient enable on manifest receive

**Subsystem**: Manifest + mod lifecycle
**Change**: When client receives `SVC_MATCH_MANIFEST` and finds mods with `action_hint=1` (HAVE_BUT_DISABLED):
1. Call `modTransientSave(mod_id)` to record prior disabled state
2. Call `modmgrSetEnabled(mod_id, true)` to enable for match
3. Set `g_ModTransientSession.active = true`
**File**: `port/src/net/netmsg.c` (SVC_MATCH_MANIFEST handler)
**Risk**: MEDIUM -- the enable call must trigger catalog re-registration of the mod's assets.
**Dependencies**: L3-1, L3-3

### L3-5: Restore prior mod state on match end / disconnect

**Subsystem**: Mod lifecycle
**Change**: In two locations, call `modTransientRestore()`:
1. `SVC_STAGE_END` handler in netmsg.c -- after sessionCatalogTeardown and manifestClear
2. `netDisconnect()` in net.c -- in the cleanup path
`modTransientRestore()` iterates `g_ModTransientSession.mods[]`, calls `modmgrSetEnabled(mod_id, false)` for each that `was_enabled == false`, then clears the session.
**Files**: `port/src/net/netmsg.c`, `port/src/net/net.c`, `port/src/modmgr.c`
**Risk**: LOW -- restore is a simple iteration + disable call.
**Dependencies**: L3-4

### L3-6: pd.ini crash-safe persistence

**Subsystem**: Config persistence
**Change**: On match start with transient enables, write `[TransientMods]` section to `pd.ini` listing mod IDs and prior states. On match end, remove section. On startup, if section exists, restore prior states and remove section.
**Files**: `port/src/modmgr.c`, `port/src/config.c`
**Risk**: LOW -- INI read/write is well-tested.
**Dependencies**: L3-4

---

## Layer 4: Polish

**Goal**: Quality-of-life improvements and edge case handling.

### L4-1: Periodic timer sync

**Subsystem**: State sync (timer)
**Symptom**: Match timer runs independently on each machine. Frame rate differences cause drift (~10 seconds over 10 minutes at 59 vs 60 fps).
**Change**: Add `SVC_TIMER_SYNC` message. Server broadcasts `g_StageTimeElapsed60` every 600 frames (10 seconds). Client applies with smoothing (gradual adjustment, not snap).
**Files**: `port/include/net/netmsg.h` (new message), `port/src/net/netmsg.c` (encode/decode), `port/src/net/net.c` (periodic send)
**Risk**: LOW -- new message, purely correctional.
**Dependencies**: None (independent of other layers)

### L4-2: Automatic .temp mod cleanup on match end

**Subsystem**: Mod distribution
**Symptom**: Mods extracted to `mods/.temp/` during match N persist on disk forever (until game restart + crash recovery prompt).
**Change**: In `SVC_STAGE_END` handler, after `modTransientRestore()`, walk `mods/.temp/` and remove directories for mods that were session-only (i.e., not in the next match's manifest).
**File**: `port/src/net/netdistrib.c`
**Risk**: MEDIUM -- filesystem operations during stage transition. Must be async or fast. Consider deferring to a background thread.
**Dependencies**: L3-5 (transient restore must run first)

### L4-3: Consolidate endscreen exit paths

**Subsystem**: Menu state management
**Symptom**: Multiple endscreen exit paths (solo rematch, networked return-to-room, quit-to-menu, disconnect) each independently manage input context pops, menu stack cleanup, and network state. Fragile -- adding a new exit path requires replicating all cleanup.
**Change**: Extract a single `endscreenCleanup()` function that handles: input context pop, `g_MainIsEndscreen = 0`, `normmplayerisrunning = false`, co-op linkage clear. All exit paths call this first, then their mode-specific logic.
**File**: `port/fast3d/pdgui_menu_endscreen.cpp`, `port/fast3d/pdgui_bridge.c`
**Risk**: LOW -- refactor of existing code.
**Dependencies**: L1-3, L1-4

### L4-4: Add manifest path for local CombatSim with mods

**Subsystem**: Manifest lifecycle
**Symptom**: Local/solo CombatSim (`s_IsSoloMode = true`) does not build or apply a manifest. If the local match uses mod content, the manifest system is not involved. Mod assets are loaded reactively via `manifestEnsureLoaded()` safety net.
**Change**: In `matchStart()` (matchsetup.c:487), after resolving all catalog IDs, call `manifestBuildForLocal()` to build a manifest from the match config. Apply via `manifestMPTransition()` during `mainChangeToStage()`.
**File**: `port/src/net/matchsetup.c`, `port/src/net/netmanifest.c`
**Risk**: LOW -- uses existing manifest infrastructure. The local manifest just ensures all mod assets are pre-loaded rather than reactively loaded at spawn.
**Dependencies**: None

---

## Summary Table

| ID | Item | Layer | Subsystem | Severity | Risk | Depends On |
|----|------|-------|-----------|----------|------|-----------|
| L1-1 | Clear g_ClientManifest on match end | 1 | Manifest | MEDIUM | LOW | -- |
| L1-2 | Periodic score broadcast | 1 | State sync | MEDIUM | LOW | -- |
| L1-3 | Gate HUD during endscreen | 1 | HUD | LOW | LOW | -- |
| L1-4 | Clear co-op netclient linkages at endscreen exit | 1 | Network | LOW | LOW | -- |
| L1-5 | Add scores to initial resync flags | 1 | State sync | MEDIUM | LOW | -- |
| L2-1 | Build manifest for co-op/anti | 2 | Manifest | HIGH | MEDIUM | L1-1 |
| L2-2 | Add ready gate to co-op/anti | 2 | Match startup | HIGH | MEDIUM | L2-1 |
| L2-3 | Add CLC_STAGE_READY to co-op client | 2 | Handshake | MEDIUM | LOW | L2-2 |
| L2-4 | Unify SVC_STAGE_START client handling | 2 | Match startup | MEDIUM | MEDIUM | L2-3 |
| L2-5 | Protocol version bump | 2 | Wire protocol | -- | LOW | L2-1:L2-4 |
| L3-1 | mod_transient_session_t structure | 3 | Mod manager | HIGH | LOW | -- |
| L3-2 | Extend CLC_CATALOG_DIFF with disabled list | 3 | Distribution | HIGH | MEDIUM | L2-5, L3-1 |
| L3-3 | Add action_hint to SVC_MATCH_MANIFEST | 3 | Manifest | HIGH | MEDIUM | L3-2 |
| L3-4 | Client transient enable on manifest receive | 3 | Mod lifecycle | HIGH | MEDIUM | L3-1, L3-3 |
| L3-5 | Restore prior mod state on match end | 3 | Mod lifecycle | HIGH | LOW | L3-4 |
| L3-6 | pd.ini crash-safe persistence | 3 | Config | MEDIUM | LOW | L3-4 |
| L4-1 | Periodic timer sync | 4 | State sync | LOW | LOW | -- |
| L4-2 | Automatic .temp mod cleanup | 4 | Distribution | MEDIUM | MEDIUM | L3-5 |
| L4-3 | Consolidate endscreen exit paths | 4 | Menu state | LOW | LOW | L1-3, L1-4 |
| L4-4 | Local manifest for solo + mods | 4 | Manifest | LOW | LOW | -- |

---

## Estimated Effort

| Layer | Sessions | Can Ship Independently? | Protocol Bump? |
|-------|----------|------------------------|---------------|
| Layer 1 | 1-2 | YES | NO |
| Layer 2 | 3-4 | YES (after L1) | YES (v34) |
| Layer 3 | 2-3 | YES (after L2) | Same bump (v34) |
| Layer 4 | 2-3 | YES (after L1) | L4-1 needs bump |

**Total**: ~8-12 focused sessions for complete implementation.

**Recommended execution order**: L1 (immediate) → L2 (next sprint) → L3 (after L2 stabilizes) → L4 (polish pass).
