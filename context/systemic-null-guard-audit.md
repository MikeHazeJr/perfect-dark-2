# Systemic Null-Guard Audit — Player Pointer Dereferences

> Documented S64. See also [systemic-bugs.md](systemic-bugs.md) for the pattern classification.

---

## Root Cause

On the N64, players were always initialized before game code ran. On the PC port with networked stage loading, `g_Vars.players[i]` may be NULL when game loops execute — either because:

1. **Sparse slots**: `PLAYERCOUNT()` counts non-null entries. If `players[0]=NULL` and `players[1]!=NULL`, then `PLAYERCOUNT()==1` and a loop `for (i=0; i<PLAYERCOUNT(); i++)` runs with `i=0`, which is NULL → crash.

2. **Pre-spawn execution**: Functions called during `lvReset()` run before the player init loop at line 483. `bgReset()` → `roomsReset()` at line 364 is called before any player is spawned.

3. **Dangling pointers**: After `mempReset(MEMPOOL_STAGE)`, player pointers become dangling. `playermgrReset()` nulls all slots, but not all code paths call it before stage loops run.

## Pattern Classification

**PLAYERCOUNT loop without guard:**
```c
// DANGEROUS — crashes if players[i] is NULL
for (i = 0; i < PLAYERCOUNT(); i++) {
    g_Vars.players[i]->field = value;
}

// SAFE — guard added
for (i = 0; i < PLAYERCOUNT(); i++) {
    if (!g_Vars.players[i]) continue;
    g_Vars.players[i]->field = value;
}
```

**Deep dereference chain:**
```c
// DANGEROUS — prop or chr may also be NULL
g_Vars.players[i]->prop->chr->hidden

// SAFE — full chain guard
if (!g_Vars.players[i] || !g_Vars.players[i]->prop || !g_Vars.players[i]->prop->chr) continue;
```

---

## Audit Results

### CRITICAL (Pre-spawn execution path)

| File | Function | Line | Status |
|------|----------|------|--------|
| `src/game/roomreset.c` | `roomsReset()` | 33 | **FIXED S63/S64** — called via `bgReset()` at `lvReset:364`, before player init at `lvReset:483` |

### HIGH (PLAYERCOUNT loops without guards)

| File | Function | Lines | Status |
|------|----------|-------|--------|
| `src/game/lv.c` | `lvTick` (slayer rocket) | 227 | Already had guard (pre-existing) |
| `src/game/lv.c` | `lvReset` (player init) | 483 | Already had guard (pre-existing) |
| `src/game/lv.c` | `lvTick` (hasdotinfo) | 2184 | **FIXED S64** |
| `src/game/lv.c` | `lvTick` (joybutinhibit) | 2194 | **FIXED S64** |
| `src/game/lv.c` | `lvTick` (smart slowmo outer) | 2217 | **FIXED S64** — player + prop guard; 3-level dereference `->prop->rooms` |
| `src/game/lv.c` | `lvTick` (smart slowmo inner) | 2225 | **FIXED S64** — inner otherplayernum loop guard |
| `src/game/lv.c` | `lvTick` (numdying) | 2371 | **FIXED S64** |
| `src/game/bondgun.c` | `bgunSetPassiveMode` | ~12045 | **FIXED S63/S64** |
| `src/game/bondcutscene.c` | `bcutsceneInit` | 15 | **FIXED S64** |
| `src/game/bondgunstop.c` | `bgunStop` | 15 | **FIXED S64** |
| `src/game/chr.c` | `chrNoteLookedAtPropRemoved` | 1476 | **FIXED S64** |
| `src/game/chraction.c` | `chractionDestroyEyespy` | 3033 | **FIXED S64** |
| `src/game/chraction.c` | `chrIsOffScreen` | 5438 | **FIXED S64** — player + prop guard; `->prop->pos` and `->prop->rooms` |
| `src/game/chraicommands.c` | `aicommand_if_eyespy_at_pad` | 1952 | **FIXED S64** |
| `src/game/radar.c` | `radarRender` | 338 | **FIXED S64** — full 3-level chain guard: player + prop + chr |
| `src/game/camera.c` | `cam0f0b5320` | 250, 261 | Already had guards (pre-existing) |
| `src/game/camera.c` | `cam0f0b53a4` | 288, 299 | Already had guards (pre-existing) |
| `src/game/music.c` | `musicIsAnyPlayerInAmbientRoom` | 347 | **FIXED S63** |

### MEDIUM / Intentional (single-player contexts)

These access `g_Vars.players[specific_index]` directly but are called in contexts where that player is guaranteed to exist (e.g., HUD rendering for player 0 during active gameplay). Not fixed — guard would only add noise.

- `src/game/hud.c` — player 0 hud rendering
- `src/game/player.c` — called after player spawn is confirmed
- `src/game/am.c` — ammo management, called from player tick

---

## Remaining Scan Required

These files contain `g_Vars.players[` accesses but were not audited in this pass (all rated MEDIUM or lower risk based on call context). Run the following to find unguarded instances:

```
grep -n "g_Vars\.players\[" src/game/*.c | grep -v "if (!g_Vars" | grep "\->"
```

Any new files added to `src/game/` that loop over players must add the guard pattern.

---

## Future Prevention

**Rule**: Every new PLAYERCOUNT loop must begin with:
```c
if (!g_Vars.players[i]) continue;
```

**Rule**: Any access to `players[i]->prop` must first guard `->prop`:
```c
if (!g_Vars.players[i] || !g_Vars.players[i]->prop) continue;
```

**Rule**: The `->prop->chr` chain requires all three guards.

Add this to code review checklist for any PR touching `src/game/`.
