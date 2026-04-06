# Bot Crash & Bug Fix Plan — 31-Bot Multiplayer

**Date:** 2026-04-05
**Status:** All tasks complete. Crash fixes + both remaining bugs fixed and building clean.
**Branch:** dev
**Working directory:** `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike` (NO worktrees)

---

## Background

Running 31 bots + 1 player in online multiplayer caused persistent crashes. Multiple crash paths were identified and fixed over several iterations. The game now runs stable (clean shutdown confirmed), but two non-crash bugs remain.

---

## COMPLETED — Crash Fixes (already in codebase, do NOT redo)

These fixes are already applied and building cleanly. Listed for context only.

### 1. roomsCopy Stack Overflow
- **Files:** `src/game/prop.c`, `src/include/game/prop.h`, `src/game/chraction.c`
- **Fix:** Added `roomsCopySafe(src, dst, maxdst)` bounded variant. Replaced unbounded `roomsCopy` calls in `chrTickGoPos` and `chrGoToRoomPos` that wrote to stack-local `RoomNum nextrooms[8]`.

### 2. curindex Out-of-Bounds in chrTickGoPos
- **Files:** `src/game/chraction.c`
- **Fix:** Added bounds check at `chrTickGoPos` entry. Added `< MAX_CHRWAYPOINTS` guards on `curindex + 1` and `curindex + 2` waypoint lookahead accesses. Added safety reset in `chrGoPosAdvanceWaypoint`.

### 3. NULL Pointer Crashes in Pathfinding
- **Files:** `src/game/padhalllv.c`
- **Fix:** Added NULL checks after `waypointChooseNeighbour()` returns in `waypointFindRoute`, `waypointCollectLocal`, and after `waygroupChooseNeighbour()` in `waygroupFindRoute` and `navFindRoute`. These functions could return NULL when no matching neighbour existed, causing immediate NULL dereference.

### 4. groupnum Validation in navFindRoute
- **File:** `src/game/padhalllv.c`
- **Fix:** Added cached group count (with 4096 hard cap) to validate `frompoint->groupnum` and `topoint->groupnum` before indexing into the waygroup array. Also added NULL checks for `waypointFindSegmentIntoGroup` outputs and `waygroupChooseNeighbour` results.

### 5. Bounds Check in waypointFindClosestToPos
- **File:** `src/game/padhalllv.c`
- **Fix:** Bounded the room copy loop (`i < 29`) to prevent `allrooms[30]` stack overflow when source `rooms[]` lacks a `-1` terminator.

### 6. padUnpack Bounds Check
- **File:** `src/game/pad.c`
- **Fix:** Added `padnum < 0 || padnum >= g_PadsFile->numpads` guard. Returns zeroed pad with `room = -1` for invalid padnums instead of crashing.

### 7. Crash Handler Improvements
- **File:** `port/src/crash.c`
- **Fix:** Added SIGABRT handler for GCC's `-fstack-protector-strong`. Added `signal(SIGABRT, crashSigabrtHandler)` in Windows `crashInit` path so stack-protector canary smashes produce diagnostic output instead of silent death.

### 8. Build Config
- **File:** `CMakeLists.txt`
- **Fix:** 8MB stack (`-Wl,--stack,8388608`), `-fstack-protector-strong` for overflow detection.

---

## TASK 1: Fix Void Spawn Bug (Y = -4294967296) — COMPLETE

**Priority:** High
**Status:** Fixed 2026-04-05
**Symptom:** All 31 bots spawn at `(0, -4294967296, 0)` — the Y value is `-2^32` (0xFFFFFFFF00000000 as s64), indicating a 32-to-64 bit sign extension bug.

### Log Evidence
```
[01:46.23] WARNING: SPAWN: bot slot=30 in void at (0,-4294967296,0) — re-spawning
[01:46.23] WARNING: SPAWN: bot slot=29 in void at (0,-4294967296,0) — re-spawning
... (all 31 bots)
```
The bots get re-spawned by the void detection code, so they eventually work, but this is wasteful and masks a real bug.

### Key Files
- `src/game/bot.c` — `botSpawn()` at line ~279, void detection at lines ~1118-1146
- `src/game/mplayer/scenarios.c` — `scenarioChooseSpawnLocation()` at line ~805
- `src/game/player.c` — `playerChooseSpawnLocation()` at line ~231
- `src/game/pad.c` — `padUnpack()` at line ~18 (s16-to-f32 position reading)
- `src/game/chraction.c` — `chrMoveToPos()` ~15551, `chrAdjustPosForSpawn()` ~15178

### Investigation Path
1. The void detection log prints `prop->pos.y` as `%.0f`, and it shows `-4294967296` which is exactly `-2^32`. As an f32, this value can't be represented exactly — f32 max is ~3.4e38, but `-2^32` = `-4294967296` IS representable as f32.
2. The value `-4294967296` as a float suggests something like: an s32 value of `0` or `-1` is being cast through `u32` then to `s64` or `f64` before becoming f32, or a pointer-width calculation is involved.
3. Check `scenarioChooseSpawnLocation` — does it return a valid pad number? If it returns -1 (no valid spawn), what happens to the position?
4. Check `chrMoveToPos` — does it handle the case where the pad position is uninitialized?
5. Check if there's a `(u32)(s32)(-1)` → `0xFFFFFFFF` → cast to `f32` chain happening anywhere in the spawn path.
6. Check the network path — `SVC_CHR_MOVE` or `SVC_CHR_RESYNC` might transmit a position that gets corrupted in serialization.

### Acceptance Criteria
- Bots spawn at valid positions on their first attempt (no void re-spawn warnings)
- Build clean, test with 31 bots on both MP and SP maps

---

## TASK 2: Fix mpbodynum Out-of-Range Bug — COMPLETE

**Priority:** Medium
**Status:** Fixed 2026-04-05
**Symptom:** With 31 bots, some get `mpbodynum` values of 63-68, which are outside the valid range `[0, 63)`.

### Log Evidence
```
[01:18.79] WARNING: [CATALOG] catalogResolveBodyByMpIndex: mpbodynum=67 out of range [0,63)
[01:19.62] WARNING: [CATALOG] catalogResolveBodyByMpIndex: mpbodynum=68 out of range [0,63)
[01:19.87] WARNING: [CATALOG] catalogResolveBodyByMpIndex: mpbodynum=66 out of range [0,63)
```

### Root Cause (identified)
Two different index spaces are being conflated:
- **mpbodynum [0,62]:** Index into `g_MpBodies[63]` array (the menu/config index)
- **runtime_index [0,255+]:** Index into `s_CatalogBodies[]` / `g_HeadsAndBodies[]` (the internal body number)

### Bug Location
**File:** `port/src/net/netmsg.c` around line 1104-1107

```c
const asset_entry_t *be = sessionCatalogLocalResolve(body_session);
if (be && be->type == ASSET_BODY) {
    g_BotConfigsArray[botidx].base.mpbodynum = (u8)be->runtime_index;  // BUG
}
```

`runtime_index` is a `g_HeadsAndBodies[]` index (can be 64+), but `mpbodynum` must be a `g_MpBodies[]` index [0,63).

### The Fix
A conversion function already exists but isn't being used here:

**File:** `port/src/assetcatalog_api.c` lines 389-396
```c
s32 catalogBodynumToMpBodyIdx(s32 bodynum)
{
    for (s32 i = 0; i < 63; i++) {
        if ((s32)g_MpBodies[i].bodynum == bodynum) return i;
    }
    return -1;
}
```

Replace the direct assignment with:
```c
s32 mpbodyidx = catalogBodynumToMpBodyIdx((s32)be->runtime_index);
if (mpbodyidx >= 0) {
    g_BotConfigsArray[botidx].base.mpbodynum = (u8)mpbodyidx;
}
```

This pattern is already used correctly in `port/src/net/matchsetup.c` at lines 413, 467, 549, 589.

### Also Check
- **File:** `port/src/assetcatalog_base.c` line 472 — Base game body registration uses `e->runtime_index = g_MpBodies[idx].bodynum` which stores the `g_HeadsAndBodies[]` index, not the `g_MpBodies[]` index. This is correct for the catalog system but means any code reading `runtime_index` and treating it as an mpbodynum is wrong.
- Search for all assignments to `.mpbodynum` in `netmsg.c` to ensure they all go through the conversion.

### Acceptance Criteria
- No `mpbodynum out of range` warnings with 31 bots
- Bots display correct body models
- Build clean

---

## Build Instructions

```powershell
# From project root:
powershell -ExecutionPolicy Bypass -File devtools/build-headless.ps1 -Target client

# Or directly via cmake (needs MSYS2 environment):
export MSYSTEM=MINGW64
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"
export TEMP="C:/Users/mikeh/AppData/Local/Temp"
export TMP="C:/Users/mikeh/AppData/Local/Temp"
cmake --build build/client --target pd -- -j24 -k
```

## Test Procedure

1. Launch server: `build/server/PerfectDarkServer.exe`
2. Launch client: `build/client/PerfectDark.exe`
3. Host online match, add 31 bots (Combat Simulator → any arena map)
4. Start match, play for 60+ seconds
5. Check `build/client/pd-client.log` for:
   - No `FATAL` / `ACCESS_VIOLATION` / `STACK_OVERFLOW`
   - No `SPAWN: bot slot=X in void` warnings (Task 1)
   - No `mpbodynum out of range` warnings (Task 2)
   - Clean `shutdown` at end of log
