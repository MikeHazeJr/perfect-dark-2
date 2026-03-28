# Null Guard & Init Order Audit — Stage Loading (Deep Audit 3 of 4)

> **Date**: 2026-03-27, Session 64
> **Scope**: Networked Combat Sim stage load — full sequence trace, order-of-operations analysis, critical bugs found and fixed.
> **Files audited**: `src/game/lv.c`, `src/game/mplayer/mplayer.c`, `src/game/setup.c`, `src/lib/main.c`, `src/game/playermgr.c`, `src/game/varsreset.c`, `port/src/net/netmsg.c`, `port/src/net/net.c`, `src/game/mplayer/participant.c`

---

## Full Stage Load Sequence — Networked Combat Sim

### Server Side (Dedicated)

**Frame N — CLC_LOBBY_START handler** (`netmsgClcLobbyStartRead`, `port/src/net/netmsg.c`):

| Step | Call | State Established |
|------|------|-------------------|
| 1 | CLC_LOBBY_START leader validation | Confirmed sender is leader |
| 2 | `g_MpSetup.*` populated | stagenum, scenario, chrslots, scorelimit, timelimit, options, weapons |
| 3 | Player loop `ci=0..NET_MAX_CLIENTS` | `ncl->playernum` assigned sequentially for all LOBBY/GAME clients |
| 4 | Bot slots added to chrslots | Bits 8..8+numSims-1 set |
| 5 | `mpStartMatch()` called | See mpStartMatch trace below |
| 6 | `menuStop()` | Menu teardown |

**`mpStartMatch()` trace** (`src/game/mplayer/mplayer.c`):

| Step | Call | State Established |
|------|------|-------------------|
| 1 | Server path: clears CHRSLOTS_PLAYER_MASK, removes player participants 0–7 | Participant pool cleared of human slots |
| 2 | *(DEDICATED)* **No phantom added at slot 0** (BUG-SL-1 fix) | Loop starts from `i=0` |
| 2a | *(LISTEN)* Phantom at slot 0 (PARTICIPANT_LOCAL), loop from `i=1` | Listen server occupies slot 0 |
| 3 | Remote clients loop: `mpAddParticipantAt(slot, PARTICIPANT_REMOTE, ...)` | All connected CLSTATE_LOBBY clients added |
| 4 | `g_MpSetup.stagenum` resolved if RANDOM (BUG-SL-2 fix) | `g_MpSetup.stagenum = resolved stagenum` |
| 5 | Texture surface type patched for stagenum | Surface type globals set |
| 6 | `mpConfigureQuickTeamSimulants()` | Bot team assignments |
| 7 | Handicap floor pass (0 → 0x80) | Prevents instant-death from 0-handicap |
| 8 | `numplayers = mpGetActivePlayerCount()` | Count of active participants |
| 9 | `titleSetNextStage(resolved_stagenum)` | g_TitleNextStage set |
| 10 | `mainChangeToStage(resolved_stagenum)` | **g_MainChangeToStageNum set — deferred** |
| 11 | `setNumPlayers(numplayers)` | Stored for next-frame allocation |
| 12 | `g_Vars.perfectbuddynum = 1` | Triggers mpReset in next-frame load |

**Frame N+1 — main loop detects `g_MainChangeToStageNum >= 0`** (`src/lib/main.c`):

| Step | Call | State Required | State Established |
|------|------|---------------|-------------------|
| 1 | `g_StageNum = g_MainChangeToStageNum; g_MainChangeToStageNum = -1` | — | g_StageNum = new stage |
| 2 | `mempResetPool, filesStop` | — | Stage memory cleared |
| 3 | `memaReset, langReset, playermgrReset` | — | Player slots NULLed |
| 4 | `playermgrAllocatePlayers(numplayers)` | numplayers set in Frame N | g_Vars.players[0..n-1] allocated |
| 4a | → `netPlayersAllocate()` | clients in CLSTATE_LOBBY | `ncl->playernum` re-assigned; `cl->player = g_Vars.players[cl->playernum]`; `cl->config = g_PlayerConfigsArray[cl->playernum]` |
| 5 | `mpReset()` (via `g_Vars.perfectbuddynum = 1`) | Participant pool populated (Frame N) | `g_Vars.normmplayerisrunning = true`; `g_Vars.mplayerisrunning = true`; `g_MpNumChrs` set; `g_PlayerConfigsArray[i].contpad1` configured |
| 6 | `gfxReset, joyReset, dhudReset, zbufReset` | — | Rendering state cleared |
| 7 | `lvReset(g_StageNum)` | See lvReset trace below | Full stage initialized |
| 8 | `viReset` | — | VI/display reset |

**`lvReset(stagenum)` trace** (`src/game/lv.c`):

| Step | Call | State Required | State Established |
|------|------|---------------|-------------------|
| 1 | `g_Vars.stagenum = stagenum` | — | g_Vars.stagenum = new stage |
| 2 | `assetCatalogActivateStage(stagenum)` | — | Mod stage catalog activated |
| 3 | Flag resets (restartlevel, aibuddiesspawned, totalkills, etc.) | — | Combat state zeroed |
| 4 | `musicReset()` | — | Music state cleared |
| 5 | `modelmgrSetLvResetting(true)` | — | Model manager load-guard set |
| 6 | `surfaceReset, texReset, textReset, hudmsgsReset` | — | Rendering caches cleared |
| 7 | `bgReset(g_Vars.stagenum)` | — | **g_StageIndex set** ← critical for setupLoadFiles |
| 8 | `bgBuildTables(g_Vars.stagenum)` | g_StageIndex set | BG geometry built |
| 9 | `skyReset(g_Vars.stagenum)` | — | Sky state initialized |
| 10 | `if (normmplayerisrunning) musicSetStageAndStartMusic(stagenum)` | normmplayerisrunning set in mpReset; players[i] may be NULL ← **S63 NULL guard** | Music started |
| 11 | `if (normmplayerisrunning) mpApplyLimits()` | normmplayerisrunning set | Time/score limits applied |
| 12 | **SERVER:** `netServerStageStart()` | g_MpSetup fully populated | **SVC_STAGE_START sent to all clients** |
| 12a | **CLIENT:** `netClientSyncRng()` | g_NetRngSeeds latched by SVC_STAGE_START handler | RNG seeds synchronized |
| 13 | `mpSetDefaultNamesIfEmpty, animsReset, objectivesReset, vtxstoreReset, modelmgrReset, psReset` | — | Subsystem state cleared |
| 14 | `setupLoadFiles(stagenum)` | g_StageIndex set (step 7); normmplayerisrunning set (mpReset) | Setup + pad files loaded; g_StageSetup pointers set |
| 15 | `scenarioReset()` | g_StageSetup.intro loaded | Intro commands processed |
| 16 | `varsReset()` | normmplayerisrunning ← **NOT cleared by varsReset** | Props pool allocated; numpropstates set (4 for MP, 7 for SP) |
| 17 | `propsReset, chrmgrReset, bodiesReset(stagenum)` | — | Prop/chr/body systems reset |
| 18 | `setupCreateProps(stagenum)` | g_StageSetup loaded; props pool ready | All stage props created (doors, weapons, spawn pads) |
| 19 | Player loop `for i < PLAYERCOUNT()` | g_Vars.players[i] allocated; props pool ready | — |
| 19a | `playerLoadDefaults()` | g_PlayerConfigsArray[i] configured (mpReset + netPlayersAllocate) | Player defaults applied |
| 19b | `playerReset()` | playerLoadDefaults done | Player state initialized |
| 19c | `playerSpawn()` | props pool ready; spawn pads in props list | **player prop created** |
| 19d | `bheadReset()` | player spawned | Head health initialized |
| 20 | `acousticReset, portalsReset, lightsReset` | props created | Audio/light system initialized |
| 21 | `if (lvmpbotlevel) mpCalculateTeamIsOnlyAi()` | normmplayerisrunning set | Bot team detection done |
| 22 | `modelmgrSetLvResetting(false)` | — | Model manager load-guard cleared |
| 23 | `if (g_NetMode) netSyncIdsAllocate()` | All props + player props exist | Sync IDs assigned; client swaps player/server prop syncids |

### Client Side

**Receiving SVC_STAGE_START** (`netmsgSvcStageStartRead`, `port/src/net/netmsg.c`):

| Step | Call | State Established |
|------|------|-------------------|
| 1 | g_MpSetup.* set from packet | stagenum, scenario, chrslots, etc. |
| 2 | `ncl->playernum` set for all clients | Playernum assignments from server |
| 3 | `ncl->state = CLSTATE_GAME`, `ncl->player = NULL` | All participants in game state |
| 4 | `g_PlayerConfigsArray[pnum].base.team` set | Team assignments applied |
| 5 | `g_NetNumClients` set | Player count known |
| 6 | `mpParticipantsFromLegacyChrslots(g_MpSetup.chrslots)` | Participant pool rebuilt from server's chrslots |
| 7 | `mpStartMatch()` → `mainChangeToStage(stagenum)` | g_MainChangeToStageNum set (deferred) |
| 8 | `menuStop()` | Menu torn down |

Client next frame: identical to server Frame N+1 except:
- In `netPlayersAllocate`: client swaps `g_NetLocalClient->playernum = 0` (local always at index 0)
- In `lvReset` step 12: calls `netClientSyncRng()` instead of `netServerStageStart()`

---

## Order-of-Operations Bugs Found

### BUG-SL-1 (CRITICAL) — Dedicated Server: `mpStartMatch` adds phantom at slot 0, skips g_NetClients[0]
**File**: `src/game/mplayer/mplayer.c:199`
**Status**: **FIXED (S64)**

**Root cause**: The server participant setup loop in `mpStartMatch` was written for listen-server mode where `g_NetLocalClient = &g_NetClients[0]` (server occupies slot 0). For listen server, starting the remote client loop from `i = 1` correctly skips the server's own slot.

For dedicated server (`g_NetDedicated = true`, `g_NetLocalClient = NULL`), slot 0 is the FIRST REAL REMOTE CLIENT, not the server. The original code:
1. Added a `PARTICIPANT_LOCAL` phantom at participant slot 0
2. Started the remote client loop from `i = 1`, skipping `g_NetClients[0]`

**Impact**: The first connecting client (`g_NetClients[0]`) was never added to the participant pool as `PARTICIPANT_REMOTE`. They ended up sharing slot 0 with the phantom (both get `playernum=0` via `netPlayersAllocate`). This works by accident for basic cases — the phantom serves as a surrogate — but is semantically wrong:
- `PARTICIPANT_LOCAL` on slot 0 means server-AI-controlled; the real client's CLC_MOVE is actually processed correctly (netclient → CLSTATE_GAME path), but the participant type is wrong
- When B-12 Phase 3 removes chrslots and uses participant type to drive control modes, this will cause slot 0 to behave as AI-controlled instead of remote-player-controlled
- Violates S50 constraint: "Server is not a player. Do not assume the server occupies any player slot."

**Fix**: Added `g_NetDedicated` guard. Dedicated path starts loop from `i=0`, no phantom. Listen-server path unchanged (`slot=1`, `startIdx=1`, phantom at slot 0).

---

### BUG-SL-2 (MEDIUM) — `g_MpSetup.stagenum` not updated after random stage resolution
**File**: `src/game/mplayer/mplayer.c:274`
**Status**: **FIXED (S64)**

**Root cause**: `mpStartMatch` resolves `STAGE_MP_RANDOM` / `STAGE_MP_RANDOM_MULTI` / etc. to an actual stagenum in the local variable `stagenum`, but never writes the resolved value back to `g_MpSetup.stagenum`. After the match loads, `g_MpSetup.stagenum` still holds the RANDOM token.

**Impact (current)**: Low — `SVC_STAGE_START` correctly uses `effectiveStage = g_StageNum` (the resolved value) for the wire stage field, not `g_MpSetup.stagenum`. `setupLoadFiles` uses `g_StageIndex` derived from `bgReset(g_Vars.stagenum)`, also the resolved value. No current crash.

**Impact (future)**: Any code reading `g_MpSetup.stagenum` to query "what stage are we on" during a running match would receive the RANDOM token, not the actual stage ID. Already a subtle hazard for the lobby-flow reconnect path.

**Fix**: `g_MpSetup.stagenum = (u8)stagenum` after resolution. Log line retains the `menu_stage=X, resolved_stage=Y` distinction via the existing PARTICIPANTS log line.

---

### BUG-SL-3 (HIGH, deferred) — `netmsgSvcStageStartWrite` mutates client state as a side effect
**File**: `port/src/net/netmsg.c:614`
**Status**: DOCUMENTED, not fixed

**Root cause**: Inside the serialization function `netmsgSvcStageStartWrite`, the code writes `ncl->state = CLSTATE_GAME` for each client it serializes. Serialization functions should not mutate game state. If the packet write fails or is abandoned mid-way, client states are left partially mutated.

**Current mitigation**: `netServerStageStart()` already transitions all `CLSTATE_LOBBY → CLSTATE_GAME` BEFORE calling the write function (lines 573–577 of net.c). So the mutation inside the write is redundant (clients already in GAME state). The only case where it matters is reconnecting clients (mid-game join path in CLC_AUTH handler), where the reconnecting client is in CLSTATE_LOBBY when write is called.

**Recommended fix**: Move the `ncl->state = CLSTATE_GAME` assignment to the call sites (`netServerStageStart` and the reconnect path in `netmsgClcAuthRead`) and remove it from the write function.

---

### BUG-SL-4 (MEDIUM, deferred) — `netPlayersAllocate` runs before `mpReset`, sets `cl->config` to unconfigured `g_PlayerConfigsArray`
**File**: `src/lib/main.c:1013` (order of calls)
**Status**: DOCUMENTED, not fixed

**Root cause**: In main.c's stage load sequence, `playermgrAllocatePlayers` → `netPlayersAllocate` is called at step 4, BEFORE `mpReset` at step 5. `netPlayersAllocate` sets `cl->config = &g_PlayerConfigsArray[cl->playernum]` and writes remote player settings (body, head, name, controlmode). `mpReset` then writes to the SAME array entries (contpad1, contpad2, title fields).

**No conflict currently**: These functions write to different fields of `g_PlayerConfigsArray`. `netPlayersAllocate` writes: `controlmode, mpbodynum, mpheadnum, name, options, team, handicap`. `mpReset` writes: `contpad1, contpad2, title, newtitle`. They don't overlap.

**Risk**: If either function is extended to write a field the other also writes, silent ordering bugs will appear. Should be documented as a fragile ordering dependency.

---

### BUG-SL-5 (FIXED, S63) — `musicIsAnyPlayerInAmbientRoom` NULL dereference during stage load
**File**: `src/game/music.c`
**Status**: **FIXED (S63)**

`musicSetStageAndStartMusic` is called at lvReset step 10, before `playerSpawn` (step 19c). At that point, `PLAYERCOUNT() > 0` but `g_Vars.players[i]->prop == NULL` (player struct allocated, prop not yet created). NULL check on `g_Vars.players[i]` was missing. Fixed in S63.

---

## State Guarantee Analysis

### Critical invariants verified:

| Invariant | When Set | First Used | Gap? |
|-----------|----------|------------|------|
| `g_MpSetup.*` populated | CLC_LOBBY_START handler (Frame N) | `mpStartMatch` (Frame N) | None |
| `g_MainChangeToStageNum` | `mpStartMatch → mainChangeToStage` (Frame N) | main loop check (Frame N+1) | None — by design |
| `g_StageNum` = new stage | main.c before lvReset (Frame N+1) | lvReset, netSyncIdsAllocate | None |
| `g_StageIndex` | `bgReset` (lvReset step 7) | `setupLoadFiles` (lvReset step 14) | None — bgReset before setupLoadFiles |
| `g_Vars.stagenum` | lvReset first line | Throughout lvReset | None |
| `normmplayerisrunning` | `mpReset` (before lvReset) | `setupLoadFiles`, music, mpApplyLimits | None — mpReset before lvReset |
| `normmplayerisrunning` survives `varsReset` | ✓ varsReset does NOT clear it | Player loop (lvReset line 499) | None — verified |
| `g_Vars.players[i]` allocated | `playermgrAllocatePlayers` (before lvReset) | Player loop in lvReset | None |
| `player->prop` created | `playerSpawn` (lvReset step 19c) | `netSyncIdsAllocate` (lvReset step 23) | None — playerSpawn before netSyncIdsAllocate |
| `ncl->player` linked | `netPlayersAllocate` (before lvReset) | `netSyncIdsAllocate` | prop=NULL window — mitigated by NULL check |

### Potential timing hazard:

`netServerStageStart()` is called at lvReset step 12 — BEFORE `setupLoadFiles`, `setupCreateProps`, and `playerSpawn`. The SVC_STAGE_START packet is sent while the server's stage is only half-loaded. This is **intentional pipelining** (client starts loading while server continues), but means:

- Client begins its own mpStartMatch → mainChangeToStage upon receiving the packet
- Server continues lvReset (load files, create props, spawn players)
- First network tick after lvReset completes — `netSyncIdsAllocate` has run, props have syncids, game is ready
- Client lvReset runs on its own timeline

No bug here, but if the server's `netServerStageStart` ever includes data that depends on post-load state (props, syncids), there would be a race. Currently the packet only contains pre-load setup data (g_MpSetup, playernums, seeds). ✓

---

## Propagation Check

The BUG-SL-1 phantom pattern was specific to the `mpStartMatch` server path in `mplayer.c`. Searched for similar `for (s32 i = 1; ...)` patterns in netclient iteration across the net subsystem:

- `netPlayersAllocate` (net.c:1443): iterates from `i=0`. ✓
- `netServerStageStart` (net.c:573): iterates from `ci=0`. ✓
- `netmsgSvcStageStartWrite` (netmsg.c:595): iterates from `i=0`. ✓
- CLC_LOBBY_START playernum loop (netmsg.c): iterates from `ci=0`. ✓

No other sites have the `i=1` skip issue. The bug was isolated to `mpStartMatch`.

---

## QC Test Recommendations

1. **Dedicated server, 1 player**: Start Combat Sim, confirm participant pool has slot 0 as PARTICIPANT_REMOTE (not PARTICIPANT_LOCAL phantom). Log: `MATCH: mpReset normmplay=1 mplay=1 chrslots=0x0000000000000001 hasSim=0 netmode=1` (single bit).

2. **Dedicated server, 2 players**: Start Combat Sim, confirm log shows `NET: mpStartMatch server chrslots=0x0000000000000003 players=2 (dedicated=1)` (bits 0 AND 1, no phantom). Both players should spawn at separate pads.

3. **Dedicated server, random stage**: Start Combat Sim with random stage selected. Confirm `g_MpSetup.stagenum` in subsequent log lines shows the resolved stage number, not the RANDOM token.

4. **Music on ambient-track stage**: Load Maian SOS, MBR, DD Tower in networked Combat Sim. Confirm no crash during `musicSetStageAndStartMusic` call (S63 regression test).
