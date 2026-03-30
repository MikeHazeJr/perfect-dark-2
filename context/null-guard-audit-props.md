# Null-Guard Audit — Prop & Object System

> **Audit**: Deep Audit 2 of 4 — Prop + Object System
> **Session**: S64 (2026-03-27)
> **Files audited**: prop.c, propobj.c, propsnd.c, propanim.c, explosions.c, smoke.c, bg.c, objectives.c
> **Note**: door.c, lift.c, weapon.c do not exist in src/game/ — door/lift/weapon logic lives in propobj.c.

---

## Files With Zero Issues

| File | Status | Notes |
|------|--------|-------|
| propanim.c | CLEAN | No prop->chr, g_Rooms[], or obj->prop access |
| propsnd.c | CLEAN | prop->prop field guarded; rooms array copies with -1 terminator only |
| objectives.c | CLEAN | All obj->prop accesses guarded with `obj && obj->prop` |
| bg.c | CLEAN | g_Rooms[roomnum] accesses receive roomnum from well-validated callers |

---

## Critical Bugs Fixed This Session

### C-1: propobj.c — `parent->chr->hidden` without NULL check
**File**: `src/game/propobj.c`
**Original line**: ~4455
**Pattern**: `parent->chr->hidden |= CHRHFLAG_DROPPINGITEM`
**Context**: Sticky/thrown weapon launch (grenade, mine, remote mine). `parent` is verified as PROPTYPE_CHR or PROPTYPE_PLAYER, but `parent->chr` is not checked for NULL.
**Risk**: CRITICAL — Any throw of a mine/grenade by a PROPTYPE_PLAYER prop with NULL chr (during stage transition or on dedicated server with partial state) would crash.
**Fix**: Added `if (parent->chr)` guard around the hidden flag write.

---

### C-2: propobj.c — `playerprop->chr->hidden` without NULL check (cctvTick)
**File**: `src/game/propobj.c`
**Original line**: ~8392
**Pattern**: `(playerprop->chr->hidden & CHRHFLAG_CLOAKED)` inside `cctvTick()`
**Context**: CCTV camera object checks player cloaked state every frame. `playerprop` is fetched from `g_Vars.bond->prop` / `g_Vars.coop->prop` with no chr NULL check.
**Risk**: CRITICAL — CCTV cameras run in any level with a security camera prop. If bond/coop chr is NULL (stage load race or Co-op with late join), crashes every tick.
**Fix**: Changed to `(playerprop->chr && (playerprop->chr->hidden & CHRHFLAG_CLOAKED))`.

---

### C-3: propobj.c — `hitchr` used without NULL check in remote-fire (laser fence / autogun beam)
**File**: `src/game/propobj.c`
**Original lines**: ~9334, ~9345, ~9349–9350
**Pattern**: `hitprop->chr` assigned to `hitchr` then used directly in:
- `chrCompareTeams(hitprop->chr, ...)` — used original ptr, not `hitchr`
- `hitchr->model && chrGetShield(hitchr)` — unconditional dereference
- `chrEmitSparks(hitchr, ...)` and `func0f0341dc(hitchr, ...)` — crash if NULL

**Context**: Laser fence / CCTV autogun targeting loop. Checks hitprop is PROPTYPE_CHR or PROPTYPE_PLAYER, but not that chr is non-NULL.
**Risk**: CRITICAL in multiplayer — laser fence firing at a player prop with NULL chr (stage load, dedicated server transitional state) crashes immediately.
**Fix**:
- `chrCompareTeams` call now uses `hitchr` (local copy) with `hitchr &&` guard
- `if (hitchr)` block wraps entire damage application section

---

### C-4: propobj.c — `targetprop->chr` passed to `chrDamageByImpact` without NULL check (enemy autogun)
**File**: `src/game/propobj.c`
**Original line**: ~9462
**Pattern**: `chrDamageByImpact(targetprop->chr, ...)` where `targetprop` is confirmed PROPTYPE_PLAYER but chr is not checked.
**Context**: Enemy autogun (e.g. DataDyne turrets) in solo mode hitting the player. If player prop exists but chr not yet set, crashes.
**Risk**: HIGH — Solo levels with autoguns; unlikely but possible during level transitions.
**Fix**: Wrapped with `if (targetprop->chr)`.

---

### C-5: explosions.c — `g_Rooms[exproom]` OOB when `rooms[0]` is -1
**File**: `src/game/explosions.c`
**Original line**: ~379 (expansion of the HUGE25 condition)
**Pattern**: `exproom = expprop->rooms[0]` then extensive `g_Rooms[exproom].bbmin/bbmax/numportals` access, all inside the `else { }` of `if (EXPLOSIONTYPE_HUGE25)`.
**Context**: Explosion creation. If caller passes empty rooms array (rooms[0] == -1, which can happen if the explosion position has no room assignment), exproom is -1, and `g_Rooms[-1]` is OOB.
**Risk**: HIGH — Networked explosions or explosions at map boundaries where room assignment fails could trigger this.
**Fix**: Changed condition from `if (exp->type == EXPLOSIONTYPE_HUGE25)` to `if (exp->type == EXPLOSIONTYPE_HUGE25 || exproom < 0)`, so invalid-room explosions get numbb = 0 (same as HUGE25 — no damage bounding box).

---

### C-6: explosions.c — `chrDamageByExplosion(chr, ...)` with possibly-NULL chr
**File**: `src/game/explosions.c`
**Original lines**: ~1004–1007
**Pattern**: `chr = prop->chr` (prop is PROPTYPE_CHR or PROPTYPE_PLAYER), then `chrDamageByExplosion(chr, ...)` and `chrDisfigure(chr, ...)` without NULL check.
**Context**: Explosion damage loop — applies blast damage to each chr/player prop in radius. For PROPTYPE_PLAYER with NULL chr (transitional state), both calls would crash.
**Risk**: HIGH — Explosion during stage load (when another player's prop exists but chr not yet initialized) would crash.
**Fix**: Added `if (chr)` guard wrapping both `chrDamageByExplosion` and `chrDisfigure` calls.

---

### C-7: smoke.c — `rooms[0]` passed to `roomGetFinalBrightnessForPlayer` without validity check
**File**: `src/game/smoke.c`
**Original line**: ~210
**Pattern**: `roomGetFinalBrightnessForPlayer(smoke->prop->rooms[0])` where `rooms[0]` could be -1 if smoke prop has no room assignment.
**Context**: Smoke rendering — adjusts color based on room brightness. `roomGetFinalBrightnessForPlayer` directly indexes `g_Rooms[roomnum]` without bounds checking, so -1 is OOB.
**Risk**: HIGH — Smoke created during stage transitions or at map seams where room assignment fails (rooms[0] = -1) would crash on first render tick.
**Fix**: Conditional expression: `(rooms[0] >= 0 ? roomGetFinalBrightnessForPlayer(rooms[0]) : 0)`. Zero brightness (black) is the safe fallback — smoke won't be visible but won't crash.

---

## Medium Risk — Not Fixed (Acceptable Risk Profile)

### M-1: prop.c:2990 — `chr = prop->chr` after PROPTYPE_CHR check, no NULL guard
**Lines**: ~2990–2995
**Pattern**: `struct chrdata *chr = prop->chr; if (chr->actiontype == ACT_DEAD ...)` — for PROPTYPE_CHR, chr should always be set at prop creation. No stage-transition scenario makes PROPTYPE_CHR chr NULL in normal play.
**Status**: Left as-is. PROPTYPE_CHR chr = NULL is a coding error, not a race condition. If this ever crashes, it indicates broken chr init, not a guard issue.

### M-2: prop.c:2008, 2042 — `g_Rooms[*rooms]` in prop tick scheduling
**Lines**: ~2008, 2042
**Pattern**: `g_Rooms[*rooms].flags` where `*rooms` is walked from `prop->rooms`. Loop terminates at -1, so -1 is never used as index. The concern is whether rooms could contain values ≥ g_NumRooms. In practice, rooms are assigned only from valid room indices by `propSetRooms()`.
**Status**: Low risk. Room assignment path is controlled; OOB room index would indicate a broader setup corruption.

### M-3: propobj.c — `g_Rooms[loopprop->rooms[i]]` in door brightness (line ~1451–1454)
**Pattern**: Door sibling loop iterates `loopprop->rooms[i]` as g_Rooms index without bounds check.
**Status**: Medium risk. Door room assignments come from stage setup data; corruption would require bad ROM data. Not urgent but worth fixing in a future pass.

---

## Propagation Check

**Class**: "prop->chr accessed for PROPTYPE_CHR or PROPTYPE_PLAYER without NULL check"

This is the same class of bug fixed in S63 (`music.c: musicIsAnyPlayerInAmbientRoom`). After this audit:

| Area | Result |
|------|--------|
| propobj.c | Fixed 4 instances (C-1, C-2, C-3, C-4) |
| explosions.c | Fixed 1 chr instance (C-6) |
| smoke.c | No chr issue; fixed rooms[0] OOB (C-7) |
| propsnd.c | No chr access in scope |
| propanim.c | No chr access |
| objectives.c | No unsafe chr access |

**Next propagation check recommended**: bot.c, botinv.c (bot target chr access — outside this audit's scope but same class).

---

## Files Modified

| File | Changes |
|------|---------|
| `src/game/propobj.c` | 4 fixes: C-1, C-2, C-3, C-4 |
| `src/game/explosions.c` | 2 fixes: C-5, C-6 |
| `src/game/smoke.c` | 1 fix: C-7 |
