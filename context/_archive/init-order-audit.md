# Init Order Audit — Networked Combat Sim Stage Load (Client)

> Audit 3 — Stage Load Initialization Order
> Written: 2026-03-27 (S65)
> Focus: Full client-side initialization sequence from SVC_STAGE_START receipt through first game tick.
> Status: CURRENT BUG documented in §4.2.

---

## Quick Reference

The complete sequence has **three distinct phases**:

| Phase | Where | Key Output |
|-------|-------|-----------|
| 0 — Network parse | `netmsgSvcStageStartRead` (netmsg.c) | g_MpSetup populated, participant pool built, async stage change queued |
| 1 — Frame boundary | `pdmain.c` main loop | Pool reset, players allocated, mpReset, lvReset called |
| 2 — Stage init | `lvReset` (lv.c) | All subsystems initialized, players spawned |

---

## 1. Complete Initialization Sequence

### Phase 0 — Network Message Receipt

**`netmsgSvcStageStartRead`** — `port/src/net/netmsg.c:621`

Entry point. Called during the game tick, inside `netStartFrame()` dispatch.

**Reads:**
- `srccl->state` — must be `CLSTATE_LOBBY` or `CLSTATE_GAME` (guard at line 623)

**Writes:**
- `g_NetTick` — syncs tick counter to server
- `g_NetRngSeeds[0/1]`, `g_NetRngLatch = true` — RNG seed latched for next `netClientSyncRng()` call
- `g_NetGameMode` — NETGAMEMODE_COMBAT or COOP/ANTI
- `g_MpSetup.stagenum`, `.scenario`, `.scorelimit`, `.timelimit`, `.teamscorelimit`, `.chrslots`, `.options`, `.weapons` — full match config from server
- `g_NetClients[id].playernum`, `.settings.*`, `.state = CLSTATE_GAME`, `.player = NULL` — per-client state from server
- `g_PlayerConfigsArray[playernum].base.team` — team assignments
- `g_NetNumClients = numplayers`

**NULL guards:** None needed at this stage — no player/prop/chr state is accessed.

**Calls (in order):**

1. **`mpParticipantsFromLegacyChrslots(g_MpSetup.chrslots)`** — `participant.c:387`
   - Reads `g_MpSetup.chrslots` (just populated above)
   - Populates `g_MpParticipants` pool (malloc-based, NOT MEMPOOL_STAGE — survives pool reset)
   - Bits 0–7: player slots → `PARTICIPANT_LOCAL`
   - Bits 8–39: bot slots → `PARTICIPANT_BOT`
   - **Critical ordering:** MUST run before `mpStartMatch`, which calls `mpConfigureQuickTeamSimulants`
   - **Critical ordering:** MUST run before `mpReset` (called from pdmain.c), which calls `mpIsParticipantActive`
   - **Critical ordering:** MUST run before `setupLoadFiles` (called from lvReset), which calls `mpHasSimulants()`

2. **`mpStartMatch()`** — `mplayer.c:193`
   - On `NETMODE_CLIENT`: **skips** server-only participant rebuild (lines 199–224 gated by `NETMODE_SERVER`)
   - Calls `mpConfigureQuickTeamSimulants()` — may modify chrslots (server-authoritative for bots)
   - Calls `titleSetNextStage(stagenum)` — sets g_TitleNextStage
   - **Calls `mainChangeToStage(stagenum)`** — sets `g_MainChangeToStageNum` (ASYNC: just sets a flag, does not load immediately)
   - Calls `setNumPlayers(numplayers)` — sets `g_NumPlayers`
   - Calls `titleSetNextMode(TITLEMODE_SKIP)`
   - Sets `g_Vars.perfectbuddynum = 1` — pdmain.c uses this to trigger `mpReset()` on next phase

3. **`menuStop()`** — stops active menus

---

### Phase 1 — Frame Boundary (pdmain.c main loop)

The main loop in `pdmain.c:599` polls `g_MainChangeToStageNum`. When it turns ≥ 0 (set by `mainChangeToStage`), the current stage ends and the next begins.

**`pdmain.c:611–592`** — the stage transition block (runs once per stage change)

**Step 1.1 — Tear down current stage:**
- `lvStop()` — tears down current stage state
- `mempDisablePool(MEMPOOL_STAGE)` — marks stage pool invalid
- `mempDisablePool(MEMPOOL_7)`
- `filesStop(4)` — closes open file handles
- `viBlack(true)` — black screen during load

**Step 1.2 — Pool reset:**
- `mempResetPool(MEMPOOL_7)` — **frees all MEMPOOL_7 allocations**
- `mempResetPool(MEMPOOL_STAGE)` — **frees all MEMPOOL_STAGE allocations**
  - ⚠️ Everything allocated with `mempAlloc(..., MEMPOOL_STAGE)` is now invalid
  - `g_MpParticipants.slots` is `malloc`-based — **survives this reset** ✓
- `g_StageNum = g_MainChangeToStageNum` — commit the stage number
- `g_MainChangeToStageNum = -1` — clear the pending flag

**Step 1.3 — Memory allocator reset:**
- `memaReset(mempAlloc(..., MEMPOOL_STAGE), size)` — game's own allocator reset

**Step 1.4 — Language reset:**
- `langReset(g_StageNum)` — loads language bank for new stage

**Step 1.5 — Player slot reset:**
- **`playermgrReset()`** — `playermgr.c:28`
  - Sets `g_Vars.players[0..3] = NULL` (or `[0..MAX_PLAYERS-1]`)
  - Sets `g_Vars.currentplayer = NULL`, `g_Vars.bond/coop/anti = NULL`
  - **After this, `PLAYERCOUNT()` returns 0** (counts non-null entries)
  - **After this, any code that dereferences `g_Vars.players[i]` without NULL guard crashes**

**Step 1.6 — Player allocation:**
- Determines `numplayers`: uses `getNumPlayers()` if ≥ 2, otherwise 1
  - `getNumPlayers()` returns `g_NumPlayers` — set by `setNumPlayers()` in `mpStartMatch`
  - **Dependency:** `setNumPlayers` must have been called before this (✓ done in mpStartMatch)
- **`playermgrAllocatePlayers(numplayers)`** — `playermgr.c:56`
  - Allocates `numplayers` player structs from MEMPOOL_STAGE
  - Sets `g_Vars.players[0..numplayers-1]` to valid non-null pointers
  - **After this, `PLAYERCOUNT()` returns `numplayers`** (but `->prop` is still NULL)
  - Calls `netPlayersAllocate()` (if NetMode set, not title stage)
  - Sets `g_Vars.bond = g_Vars.players[g_Vars.bondplayernum]`

**Step 1.7 — Multiplayer state init:**
- Since `g_Vars.perfectbuddynum == 1` (set by `mpStartMatch`), calls **`mpReset()`** — `mplayer.c:516`
  - Sets `g_Vars.mplayerisrunning = true`
  - Sets `g_Vars.normmplayerisrunning = true` (combat sim path: no coop/anti)
  - Iterates `mpIsParticipantActive(i)` for i=0..MAX_PLAYERS-1
    - **Dependency:** participant pool MUST be populated (✓ done by `mpParticipantsFromLegacyChrslots` in Phase 0, and pool survived MEMPOOL_STAGE reset)
  - Increments `g_MpNumChrs` for each active participant
  - Sets `g_Vars.lvmpbotlevel = true` if `mpHasSimulants()`
  - **After this, `g_Vars.normmplayerisrunning` is valid** — used throughout lvReset

**Step 1.8 — Graphics/input reset:**
- `gfxReset()`, `joyReset()`, `dhudReset()`, `zbufReset(g_StageNum)`

**Step 1.9 — Stage initialization:**
- **`lvReset(g_StageNum)`** — the main stage init (Phase 2 below)
- `viReset(g_StageNum)` — video system reset

---

### Phase 2 — Stage Initialization (lvReset)

**`lvReset(s32 stagenum)`** — `src/game/lv.c:243`

Players are allocated and non-null, but `->prop` is NULL until `playerSpawn()` at the end.

**Step 2.1 — Frame and timing reset:**
- `lvFadeReset()`, `g_Vars.stagenum = stagenum`
- `assetCatalogActivateStage(stagenum)`
- `cheatsReset()`
- Various `g_Vars.*` field resets (lvframenum, lvframe60, timers, flags)

**Step 2.2 — Subsystem pre-reset:**
- `musicReset()` — clears all active music state
- `modelmgrSetLvResetting(true)` — suppresses model loads during reset
- `surfaceReset()`, `texReset()`, `textReset()`, `hudmsgsReset()`

**Step 2.3 — Network RNG sync (client):**
```c
if (g_NetMode == NETMODE_CLIENT) {
    netClientSyncRng();  // applies g_NetRngSeeds latched in Phase 0
}
```
- **Dependency:** `g_NetRngLatch` and `g_NetRngSeeds` must be set (✓ done in Phase 0)

**Step 2.4 — Geometry reset:**
- `tilesReset()`
- **`bgReset(stagenum)`** — loads room geometry, calls `roomsReset()`
  - `roomsReset()` has a `PLAYERCOUNT()` loop — **guard added (S64)**
  - Players are allocated (non-null) so PLAYERCOUNT() > 0; guard prevents dereference
- `bgBuildTables(stagenum)` — builds room lookup tables
- `skyReset(stagenum)` — sky/atmosphere reset

**Step 2.5 — Music start:**
```c
if (g_Vars.normmplayerisrunning) {
    musicSetStageAndStartMusic(stagenum);  // starts primary + ambient tracks
}
```
- `musicSetStageAndStartMusic` → `musicStartAmbient` → **`musicIsAnyPlayerInAmbientRoom()`**
  - Reads `g_Vars.players[i]->prop->rooms` — players allocated (non-null), but `->prop` is NULL
  - **B-36 (FIXED S63):** Added NULL guard: `if (g_Vars.players[i] && g_Vars.players[i]->prop && ...)`
  - Now safely returns `false` (no player in ambient room) since no player is spawned yet
  - Ambient track will start naturally once player enters ambient room on first tick

**Step 2.6 — MP limits and player stats:**
- `mpApplyLimits()` — applies g_MpSetup constraints
- Player stats loop — resets `g_Vars.playerstats[i].*` for i=0..ARRAYCOUNT

**Step 2.7 — Anim/objective/model reset:**
- `mpSetDefaultNamesIfEmpty()`, `animsReset()`, `objectivesReset()`, `vtxstoreReset()`
- `modelmgrReset()`, `psReset()`

**Step 2.8 — Setup file load:**
**`setupLoadFiles(stagenum)`** — `src/game/setup.c:1293`

Critical setup: loads the stage's binary setup file and pads file.

- **Reads:** `g_Vars.normmplayerisrunning` (set by mpReset ✓), `g_StageIndex`
- **Reads:** `mpHasSimulants()` → checks participant pool (must be populated ✓)
- **Reads:** `g_MpSetup.chrslots` for bot count (bits 8–39)
- **Writes:**
  - `g_StageSetup.intro/props/paths/ailists/padfiledata` — pointers into loaded setup binary
  - `g_Vars.maxprops` — total prop pool size (objects + chrs + buffer)
  - `g_ModelStates[i].modeldef = NULL` for all slots
  - Model slot allocation via `modelmgrAllocateSlots(numobjs, numchrs)`
  - `g_NumGlobalAilists`, `g_NumLvAilists`
- For mp stages: loads mpsetupfileid (not setupfileid)
- Validates intro data to prevent OOB crashes from mod stage garbage offsets

**No NULL guards needed:** No player/prop/chr pointer access here.

**Step 2.9 — Scenario/vars/props reset:**
- `scenarioReset()` — resets scenario state
- **`varsReset()`** — `src/game/varsreset.c:13`
  - `mempAlloc(g_Vars.maxprops * sizeof(struct prop), MEMPOOL_STAGE)` → allocates prop array
  - **Dependency:** `g_Vars.maxprops` MUST be set by `setupLoadFiles` (✓)
  - Allocates room prop lists (uses `g_Vars.roomcount` from bgReset ✓)
- **`propsReset()`** — `src/game/setup.c:175`
  - Resets lift/weapon/hat/ammo slots, alarm/gas state, etc.
  - No player access

**Step 2.10 — Chr manager reset:**
**`chrmgrReset()`** — `src/game/chrmgr.c:13`
- Sets `g_ChrSlots = NULL`, `g_NumChrSlots = 0` — clears any stale pointer from prior stage
- `mempAlloc` of shield hits array (20 × sizeof shieldhit)
- Sets `g_NumChrs = 0`, `g_Chrnums = NULL`, `g_ChrIndexes = NULL`
- **No player access.** Safe.

**Step 2.11 — Bodies reset:**
**`bodiesReset(stagenum)`** — `src/game/bodyreset.c:15`
- `PLAYERCOUNT() >= 2` check at line 33 — **safe:** just count comparison, no dereference
  - Players are allocated non-null, so PLAYERCOUNT() returns correct count here
- Randomizes active head lists (male/female guard appearances)
- **No player pointer dereference.** Safe.

**Step 2.12 — Prop creation (CURRENT CRASH POINT):**
**`setupCreateProps(stagenum)`** — `src/game/setup.c:1508`

This is the current crash zone. Processes setup commands to create all prop instances.

Sub-step 2.12a — **`chrmgrConfigure(numchrs)`** — `src/game/chrmgr.c:48`
- `g_NumChrSlots = PLAYERCOUNT() + numchrs + 10`
  - Players allocated (non-null) → PLAYERCOUNT() correct
  - `numchrs` from setup command count + bot chrslots scan
- `mempAlloc(g_NumChrSlots * sizeof(struct chrdata), MEMPOOL_STAGE)` → allocates g_ChrSlots
- **Dependency:** `chrmgrReset()` MUST have run first (clears g_ChrSlots ✓)
- **Dependency:** `mpHasSimulants()` and `g_MpSetup.chrslots` must be valid for correct numchrs (✓)

Sub-step 2.12b — `invInit()` loop (guarded):
```c
for (j = 0; j < PLAYERCOUNT(); j++) {
    if (!g_Vars.players[j]) continue;  // guard present ✓
    setCurrentPlayerNum(j);
    invInit(setupCountCommandType(OBJTYPE_LINKGUNS));
}
```

Sub-step 2.12c — **Props iteration** (processes `g_StageSetup.props`):
- Iterates all prop commands from the stage setup binary
- Creates chr/weapon/door/etc. prop instances via `setupCreateCommand()`
- `setupCreateCommand()` allocates chr slots, loads models, creates prop structs
- **Potential crash:** prop creation may dereference `g_Vars.currentplayer->*` via various setup helpers
- **Potential crash:** if `g_ChrSlots` is too small (wrong numchrs) → chr slot OOB write

**Step 2.13 — Effects reset:**
- `tagsReset()`, `explosionsReset()`, `smokeReset()`, `sparksReset()`, `weatherReset()`
- `lvResetMiscSfx()`, `boltbeamsReset()`, `lasersightsReset()`, `shardsReset()`, `frReset()`

**Step 2.14 — Player initialization loop:**
```c
for (i = 0; i < PLAYERCOUNT(); i++) {
    if (!g_Vars.players[i]) continue;  // guard present ✓
    setCurrentPlayerNum(i);
    // invInit, bgunReset, etc.
    playerLoadDefaults();
    playerReset();      // full player state init
    playerSpawn();      // ← FIRST POINT WHERE players[i]->prop IS VALID
    bheadReset();
}
```
- After `playerSpawn()`: `g_Vars.players[i]->prop` is non-null
- `playermgrCalculateAiBuddyNums()` if teams enabled

**Step 2.15 — Room/audio reset:**
- `acousticReset()`, `portalsReset()`, `lightsReset()`
- `setCurrentPlayerNum(0)`

**Step 2.16 — Post-init:**
- `mpCalculateTeamIsOnlyAi()` (if bot level)
- `paksReset()`, `sndResetCurMp3()`
- `netSyncIdsAllocate()` (if NetMode)
- `modelmgrSetLvResetting(false)`

---

## 2. Order-of-Operations Dependencies

### Dependency Graph

```
[Phase 0: netmsgSvcStageStartRead]
  └─ mpParticipantsFromLegacyChrslots ──────────┐
  └─ mpStartMatch ───────────────────────────────┤
       └─ setNumPlayers(N)                        │
       └─ mainChangeToStage(X) [async]            │
       └─ g_Vars.perfectbuddynum = 1              │
                                                  │
[Phase 1: pdmain.c]                               │
  └─ mempResetPool(MEMPOOL_STAGE) [wipes allocs]  │
  └─ playermgrReset() [players[] = NULL]          │
  └─ playermgrAllocatePlayers(getNumPlayers())    │
       └─ setNumPlayers MUST have run ────────────┤
       └─ players[i] now non-null                 │
  └─ mpReset() [sets normmplayerisrunning]        │
       └─ mpIsParticipantActive ← pool must exist ┘ (malloc survives pool reset ✓)
       └─ g_Vars.normmplayerisrunning = true

[Phase 2: lvReset]
  └─ bgReset → roomsReset          [needs: players non-null for PLAYERCOUNT count]
  └─ musicSetStageAndStartMusic    [needs: players[i] guard (B-36 fixed)]
  └─ setupLoadFiles                [needs: normmplayerisrunning ✓, mpHasSimulants ✓]
       └─ g_Vars.maxprops set
  └─ varsReset                     [needs: g_Vars.maxprops ← setupLoadFiles must run first]
  └─ chrmgrReset                   [no deps, clears ChrSlots]
  └─ bodiesReset                   [needs: PLAYERCOUNT count only, no deref]
  └─ setupCreateProps              [needs: chrmgrReset ✓, varsReset ✓, mpHasSimulants ✓]
       └─ chrmgrConfigure          [needs: PLAYERCOUNT count, chrslots valid]
       └─ props iteration          [needs: g_ChrSlots allocated ← chrmgrConfigure]
  └─ playerSpawn() ← FIRST valid ->prop access
  └─ first lvTick()
```

### Critical Ordering Rules

| Rule | What breaks if violated |
|------|------------------------|
| `mpParticipantsFromLegacyChrslots` before `mpReset` | mpReset sees empty pool → `g_MpNumChrs = 0` → bots never spawn |
| `mpParticipantsFromLegacyChrslots` before `setupLoadFiles` | `mpHasSimulants()` returns false → numchrs under-counted → model slots too small → OOB writes during chr creation |
| `setNumPlayers` before `playermgrAllocatePlayers` | Wrong number of player structs allocated → PLAYERCOUNT mismatch |
| `mpReset` before `lvReset` | `normmplayerisrunning` false during lvReset → solo setup file loaded instead of mp setup file → wrong stage data |
| `setupLoadFiles` before `varsReset` | `g_Vars.maxprops` is 0 → prop array too small → OOB write on first prop spawn |
| `chrmgrReset` before `chrmgrConfigure` | Stale `g_ChrSlots` pointer from previous stage → double-free or double-alloc |
| `playerSpawn()` before any `->prop` access | `->prop` is NULL → crash |

---

## 3. N64 vs PC Port Differences

### N64 Load Path (original)
1. Player selects match from menu → menu code calls `mpStartMatch()` synchronously on game thread
2. `mpStartMatch` → `mainChangeToStage` → immediately calls `lvReset` (N64's `mainChangeToStage` is synchronous)
3. `mpReset` is called during the same frame transition, players and bots known at load time
4. No network involvement — all participants are local (up to 4 players)
5. `PLAYERCOUNT()` always ≤ 4, `MAX_BOTS` was effectively 0 for N64

### PC Port Network Path
1. Server sends `SVC_STAGE_START` → client receives during game tick (network thread or inline in netStartFrame)
2. `netmsgSvcStageStartRead` parses match config and calls `mpStartMatch` → `mainChangeToStage` (**async** — queues stage change for next frame)
3. Stage change happens next frame in pdmain.c main loop
4. **Gap:** Between `mpParticipantsFromLegacyChrslots` (Phase 0) and `mpReset` (Phase 1), the MEMPOOL_STAGE is reset — any stage-scoped data populated in Phase 0 is wiped
   - **This is why participant pool uses `malloc`, NOT `mempAlloc`**: to survive pool reset

### Assumptions That Break in Network Path

| N64 Assumption | Where It Breaks |
|----------------|----------------|
| Player count known synchronously | Player count set via `setNumPlayers` in netmsg handler (Phase 0), consumed in pdmain.c (Phase 1) — relies on no intervening frame |
| `mpReset` and `lvReset` happen in same call stack | Phase 1 (`mpReset`) and Phase 2 (`lvReset`) are separate frames if `mainChangeToStage` is async |
| No bots without local setup UI | chrslots bitmask arrives over wire — client must trust server's bot count exactly |
| `normmplayerisrunning` set before `lvReset` | Depends on `mpReset` running first (pdmain.c ordering, not guaranteed in original code) |
| PLAYERCOUNT() matches actual human players | Net path: PLAYERCOUNT reflects locally-allocated players, not server's view — can diverge |
| `musicSetStageAndStartMusic` only called with valid players | N64 always called it post-playerSpawn; PC calls it mid-lvReset before playerSpawn |

### Key PC-Only Invariants Required

These invariants don't exist in the N64 code — they were added or must be maintained for the PC network path:

1. **Participant pool lifecycle:** Pool allocated with `malloc`, not `mempAlloc`. Must be initialized before first use, must not be wiped by `mempResetPool`.
2. **`g_Vars.perfectbuddynum = 1` sentinel:** Set by `mpStartMatch` to signal pdmain.c to call `mpReset` during stage transition. N64 called `mpReset` from a different code path.
3. **`netClientSyncRng` timing:** RNG seeds latched in Phase 0, applied at start of Phase 2 (lvReset). Any code between that reads RNG will use pre-sync state.
4. **NULL guards in init-order functions:** `musicIsAnyPlayerInAmbientRoom`, `roomsReset`, and all PLAYERCOUNT() loops called before `playerSpawn()`.

---

## 4. Known Crash Points

### 4.1 B-36 — musicIsAnyPlayerInAmbientRoom (FIXED S63)

**Crash location:** `src/game/music.c:347` — `musicIsAnyPlayerInAmbientRoom()`

**Scenario:** Client receives SVC_STAGE_START for stage with ambient music (e.g., MBR, Maian SOS, Skedar Ruins). During `lvReset`, `musicSetStageAndStartMusic` is called with `normmplayerisrunning = true`. `musicStartAmbient` calls `musicIsAnyPlayerInAmbientRoom`, which iterates `LOCALPLAYERCOUNT()` and dereferences `g_Vars.players[i]->prop` without checking `players[i]` for NULL.

**Why:** `PLAYERCOUNT()` could return > 0 (players allocated by `playermgrAllocatePlayers`), but `->prop` is NULL until `playerSpawn()` — which hasn't run yet at this point in lvReset.

**Root cause class:** SP-6 — PLAYERCOUNT() loop before player props are valid.

**Fix (S63):** Added full NULL chain guard in `musicIsAnyPlayerInAmbientRoom`:
```c
if (g_Vars.players[i]
        && g_Vars.players[i]->prop
        && g_Vars.players[i]->prop->rooms
        && g_Vars.players[i]->prop->rooms[0] != -1) {
```
Now returns `false` during stage load (correct: no player is in any room yet). Ambient track starts naturally on first `lvTick` once player is spawned and in-room.

**Also fixed (S64):** `roomsReset()` PLAYERCOUNT() loop in `src/game/roomreset.c:33`.

---

### 4.3 B-43 FIXED — First Tick Crash: g_MpAllChrPtrs NULL Dereference (S70)

**Crash location:** `src/game/lv.c:2391` -- `for (i = 0; i < g_MpNumChrs; i++) { g_MpAllChrPtrs[i]->actiontype `

**Root cause:** `mpReset()` nulls `g_MpAllChrPtrs[0..MAX_MPCHRS-1]` entirely after counting player participants. Bot chrs fill indices PLAYERCOUNT..g_MpNumChrs-1 via `botmgrAllocateBot`. Player chr is written to `g_MpAllChrPtrs[playernum]` LAZILY by `playerTickChrBody()` (player.c:1683) which runs inside `propsTick`. On the first `lvTick`, `propsTick` has not run yet so `g_MpAllChrPtrs[0] == NULL`. Loop dereferences it, crash.

**Two more sites, same class:** `bot.c:2358 botGetTeamSize`, `mplayer.c:712 mpCalculateTeamIsOnlyAi`.

**Fix (S70):** NULL guard at all three sites. See bugs.md B-43.

**Invariant:** Any loop `for (i=0; i < g_MpNumChrs; i++)` accessing `g_MpAllChrPtrs[i]` must NULL-guard. Player slots are lazy-init; bot slots are eager.

---

# 4.2 FORMER CURRENT BUG — Crash After setupLoadFiles (RESOLVED)

**Crash location:** Somewhere in `chrmgrConfigure` / `bodiesReset` / `setupCreateProps` after the "setupLoadFiles done" log line.

**What the log shows:**
```
LOAD: setupLoadFiles done
LOAD: calling scenarioReset
LOAD: calling varsReset
LOAD: calling propsReset
LOAD: calling chrmgrReset
LOAD: calling bodiesReset stagenum=0x??
LOAD: calling setupCreateProps stagenum=0x?? normmplayerisrunning=1 chrslots=0x???? g_MpNumChrs=??
[CRASH]
```

**Hypotheses (ranked by likelihood):**

**H1 — `chrmgrConfigure` called with wrong numchrs** (HIGH)
- `setupCreateProps` re-counts numchrs from setup commands + chrslots scan
- If `g_MpSetup.chrslots` has bits set for bots that weren't in the setup file, `numchrs` is larger than `setupLoadFiles` expected
- If `numchrs` unexpectedly produces `g_NumChrSlots` that exceeds actual chr slot usage during prop creation → OOB write into adjacent MEMPOOL_STAGE allocation
- Look for the log line: `CHRSLOTS: chrmgrConfigure numchrs=%d PLAYERCOUNT=%d => g_NumChrSlots=%d`

**H2 — `setupCreateProps` prop iteration crashes on chr creation** (HIGH)
- When iterating `g_StageSetup.props`, `setupCreateCommand` for `OBJTYPE_CHR` allocates chr slots and loads models
- If numchrs from `setupLoadFiles` doesn't match numchrs from `setupCreateProps` (they independently count), model slot array may be smaller than needed → `modelmgrInstantiateModel` gets NULL modeldef → crash
- This was the B-20 pattern (mission 1 obj crash)

**H3 — `varsReset` → `varsResetRoomProps` crashes on bad roomcount** (MEDIUM)
- `g_Vars.roomcount` must be set by `bgReset` before `varsReset` runs
- `varsResetRoomProps` does `mempAlloc(g_Vars.roomcount * sizeof(s16), MEMPOOL_STAGE)`
- If `g_Vars.roomcount` is 0 or garbage → size 0 alloc → subsequent indexing into the array crashes

**H4 — `setupCreateProps` accesses g_Vars.currentplayer without guard** (MEDIUM)
- Various prop creation helpers may call `setCurrentPlayerNum(0)` then use `g_Vars.currentplayer->*`
- Players are allocated non-null, so this should be safe — but if any sub-path conditionally skips `setCurrentPlayerNum`, `g_Vars.currentplayer` may be NULL

**How to distinguish:** The log line `LOAD: calling setupCreateProps ... g_MpNumChrs=?? chrslots=0x????` should reveal if chrslots or MpNumChrs is suspicious. Add a log immediately after `chrmgrConfigure` call in `setupCreateProps`:
```c
sysLogPrintf(LOG_NOTE, "LOAD: chrmgrConfigure done, g_NumChrSlots=%d", g_NumChrSlots);
```
If that log never appears, the crash is in or before `chrmgrConfigure`. If it appears, the crash is later in the props iteration.

---

## 5. Recommendations

### 5.1 Immediate — Instrument the Crash Point

Add two log lines to `setupCreateProps` (`setup.c:1508`):

```c
// after chrmgrConfigure call (line ~1569):
sysLogPrintf(LOG_NOTE, "LOAD: chrmgrConfigure done numchrs=%d g_NumChrSlots=%d", numchrs, g_NumChrSlots);

// after the prop iteration loop:
sysLogPrintf(LOG_NOTE, "LOAD: props iteration done");
```

This will tell us whether the crash is in `chrmgrConfigure`, the `invInit` loop, or the props iteration.

### 5.2 Verify chrslots Consistency

Both `setupLoadFiles` and `setupCreateProps` independently count numchrs from chrslots. They MUST produce the same count. Add a consistency assertion:

In `setupCreateProps`, before calling `chrmgrConfigure`, log the count that `setupLoadFiles` computed (visible in `g_Vars.maxprops`) vs. what `setupCreateProps` is about to send to `chrmgrConfigure`. Any discrepancy is a bug.

### 5.3 Guard `g_Vars.roomcount` Before varsReset

Add a defensive check before `varsResetRoomProps`:
```c
if (g_Vars.roomcount <= 0) {
    sysLogPrintf(LOG_WARNING, "LOAD: varsReset called with roomcount=%d — bgReset may have failed", g_Vars.roomcount);
}
```

### 5.4 Ordering Rule: musicSetStageAndStartMusic After playerSpawn

Longer-term: consider moving `musicSetStageAndStartMusic` to after the player init loop (step 2.14). The ambient track check doesn't do anything useful when called mid-load because no player is in-room yet. The primary track can start pre-spawn, but ambient detection requires valid room membership.

Current workaround (B-36 fix) is correct and stable — this is a low-priority cleanup.

### 5.5 Document `g_Vars.perfectbuddynum` Dual Use

The flag `g_Vars.perfectbuddynum` is overloaded: it was originally a "Perfect buddy was present" flag, but is now also used as a "mpReset should run" trigger by `mpStartMatch` (sets to 1) / pdmain.c (checks it). This is fragile — it relies on mpStartMatch always setting it to 1 for combat sim.

A cleaner approach would be a separate `g_MpResetNeeded` flag, but this is low priority since the current behavior is consistent.

### 5.6 Audit: All Init Functions That Call PLAYERCOUNT() Before playerSpawn

From the SP-6 audit (S64), all known cases in `src/game/` have been guarded. The init functions called between `playermgrAllocatePlayers` and the player init loop that still need verification:

| Function | File | PLAYERCOUNT use | Status |
|----------|------|----------------|--------|
| `roomsReset` | roomreset.c | Line 33 | FIXED (S64) |
| `musicIsAnyPlayerInAmbientRoom` | music.c | Line 347 | FIXED (S63) |
| `setupCreateProps` invInit loop | setup.c:1572 | Has guard | SAFE |
| `bodiesReset` | bodyreset.c:33 | Count only | SAFE |
| `chrmgrConfigure` | chrmgr.c:52 | Count only | SAFE |

Any new init-order function added between playerSpawn and the player init loop should include the canonical guard:
```c
for (i = 0; i < PLAYERCOUNT(); i++) {
    if (!g_Vars.players[i]) continue;  // REQUIRED
    // ...
}
```

---

## Appendix: File Locations

| File | Key Functions |
|------|--------------|
| `port/src/net/netmsg.c:621` | `netmsgSvcStageStartRead` — network entry point |
| `port/src/pdmain.c:515` | Stage transition block — pool reset, player alloc, mpReset |
| `src/game/lv.c:243` | `lvReset` — complete stage init sequence |
| `src/game/setup.c:1293` | `setupLoadFiles` — loads setup/pads, sizes model slots |
| `src/game/setup.c:1508` | `setupCreateProps` — creates all prop instances |
| `src/game/mplayer/mplayer.c:193` | `mpStartMatch` — sets up match, queues stage change |
| `src/game/mplayer/mplayer.c:516` | `mpReset` — sets normmplayerisrunning, counts MpNumChrs |
| `src/game/mplayer/participant.c:387` | `mpParticipantsFromLegacyChrslots` — populates participant pool |
| `src/game/playermgr.c:28` | `playermgrReset` — nulls all player pointers |
| `src/game/playermgr.c:56` | `playermgrAllocatePlayers` — allocates player structs |
| `src/game/music.c:404` | `musicSetStageAndStartMusic` — starts tracks |
| `src/game/music.c:327` | `musicIsAnyPlayerInAmbientRoom` — B-36 fix here |
| `src/game/chrmgr.c:13` | `chrmgrReset` — clears chr slot pool |
| `src/game/chrmgr.c:48` | `chrmgrConfigure` — allocates chr slots |
| `src/game/bodyreset.c:15` | `bodiesReset` — randomizes guard appearances |
| `src/game/varsreset.c:13` | `varsReset` — allocates prop array |
