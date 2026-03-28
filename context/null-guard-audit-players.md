# Null Guard Audit — Player & Character System

> **Audit 1 of 4** — Player + Character System
> **Date**: 2026-03-27 (Session 64)
> **Status**: COMPLETE — All CRITICAL and HIGH findings fixed.

---

## Root Cause (SP-6)

`PLAYERCOUNT()` counts non-null entries in `g_Vars.players[]` but loops iterate by sequential
index 0..count-1. If slots are **sparse** (e.g., slot 0 freed after a match, slot 1 still live),
`PLAYERCOUNT()=1` and the loop accesses `players[0]` which is NULL → crash.

This matches the crash in S63 (`music.c::musicIsAnyPlayerInAmbientRoom`), confirmed as a
**systemic class** (see `systemic-bugs.md` SP-6).

**Fix pattern** (all instances):
```c
for (i = 0; i < PLAYERCOUNT(); i++) {
    if (!g_Vars.players[i]) continue;   // ← added everywhere
    // ... safe to use players[i]
}
```

---

## Findings & Fixes

### CRITICAL — Runs During Stage Load

| # | File | Lines | Pattern | Status |
|---|------|-------|---------|--------|
| C-1 | `src/game/lv.c` | 482–502 | `PLAYERCOUNT()` loop in `lvReset()`: `setCurrentPlayerNum(i)` → `currentplayer->usedowntime=0` before playerSpawn | **FIXED S64** |
| C-2 | `src/game/setup.c` | 1572–1575 | `PLAYERCOUNT()` loop in `setupCreateProps()`: `setCurrentPlayerNum(j)` → `invInit()` uses currentplayer internally | **FIXED S64** |

**C-1 detail** (`lv.c`): `lvReset()` iterates players to call `playerReset()`/`playerSpawn()`.
Lines 484–485 access `currentplayer->usedowntime` and `invdowntime` immediately after
`setCurrentPlayerNum(i)`. If `players[i]` is NULL, `currentplayer` becomes NULL → crash on
next dereference. Fix: `if (!g_Vars.players[i]) continue;` at top of loop body.

**C-2 detail** (`setup.c`): `setupCreateProps()` calls `invInit()` for each player after chr
slots are configured. `invInit()` reads `g_Vars.currentplayer->` internally. Fix: same guard.

---

### HIGH — Per-Frame / Rendering

| # | File | Lines | Pattern | Status |
|---|------|-------|---------|--------|
| H-1 | `src/game/lv.c` | 227–231 | `PLAYERCOUNT()` loop in `lvTick()`: `players[i]->visionmode` without guard | **FIXED S64** |
| H-2 | `src/game/camera.c` | 250–258 | `cam0f0b53a8` if-branch: `players[i]->c_viewfmdynticknum` etc. | **FIXED S64** |
| H-3 | `src/game/camera.c` | 260–274 | `cam0f0b53a8` else-branch: `players[i]->c_prevviewfmdynticknum` etc. | **FIXED S64** |
| H-4 | `src/game/camera.c` | 286–294 | `cam0f0b53a4` if-branch: `players[i]->c_viewfmdynticknum` etc. | **FIXED S64** |
| H-5 | `src/game/camera.c` | 296–310 | `cam0f0b53a4` else-branch: `players[i]->c_prevviewfmdynticknum` etc. | **FIXED S64** |
| H-6 | `src/game/playermgr.c` | 700–704 | `playermgrGetPlayerNumByProp()`: `players[i]->prop` without guard | **FIXED S64** |

**camera.c detail**: Two functions (`cam0f0b53a8`, `cam0f0b53a4`) each have an if/else branch
each with a PLAYERCOUNT loop — 4 loops total. All dereference `players[i]->` fields used
for matrix lookup during the render pipeline. Running during stage transition with sparse slots
→ crash. Fix: `if (!g_Vars.players[i]) continue;` added to all 4 loop bodies.

**playermgr.c detail**: `playermgrGetPlayerNumByProp()` looks up which player owns a given prop.
Called from rendering, camera, and prop lifecycle callbacks. Fix: skip null slots (they can't
match a prop anyway), return -1 as before for not-found.

---

### ALREADY FIXED (prior sessions)

| File | Function | Fixed | Notes |
|------|----------|-------|-------|
| `src/game/music.c` | `musicIsAnyPlayerInAmbientRoom()` | S63 | B-36 fix — triggered this audit |

---

### NOT FOUND / OUT OF SCOPE

Files searched with no PLAYERCOUNT/players[] hazard found at audit time:
- `src/game/bondwalk.c` — uses `g_Vars.currentplayer->` directly (not a loop), called only when
  player is active (MOVEMODE_WALK), considered safe during normal gameplay. Not fixed in this
  pass; flag for Audit 2 (movement system).
- `src/game/bondmove.c` — same pattern as bondwalk.c. Per-frame input processing; game state
  gates ensure currentplayer is valid during input. Flag for Audit 2.
- `src/game/bondgun.c`, `bondview.c` — not in PLAYERCOUNT loop scope; uses currentplayer directly.
- `src/game/player.c`, `src/game/savebuffer.c` — no PLAYERCOUNT loop hazards found at this audit.
- `src/game/mplayer/*.c`, `src/game/chr/*.c` — no PLAYERCOUNT loop hazards; use g_ChrSlots which
  is a separate audit (Audit 2).

---

## Summary

| Risk Level | Found | Fixed This Session | Prior |
|------------|-------|--------------------|-------|
| CRITICAL   | 2     | 2                  | 0     |
| HIGH       | 6     | 5                  | 1 (music.c S63) |
| Total      | 8     | 7                  | 1     |

---

## Files Modified

| File | Change |
|------|--------|
| `src/game/lv.c` | `if (!g_Vars.players[i]) continue;` at lines 228 and 483 |
| `src/game/setup.c` | `if (!g_Vars.players[j]) continue;` at line 1573 |
| `src/game/camera.c` | `if (!g_Vars.players[i]) continue;` in 4 loop bodies (cam0f0b53a8 + cam0f0b53a4) |
| `src/game/playermgr.c` | `if (!g_Vars.players[i]) continue;` at line 701 |

---

## Remaining Work

- **Audit 2**: Movement system (`bondwalk.c`, `bondmove.c`, `bondgun.c`, `bondview.c`) — verify
  `g_Vars.currentplayer` early-return guards are in place at function entry points.
- **Audit 3**: Character/chr slots — `g_ChrSlots[i]` null checks, `g_MpAllChrPtrs[i]`.
- **Audit 4**: Multiplayer setup (`mplayer/setup.c`, `mplayer/*.c`) — participant system
  interactions during match start.
