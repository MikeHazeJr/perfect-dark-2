# Player-Zero Refactor Plan

> **Status**: Draft — research-only, no code changes made
> **Author**: Claude, Session S189, 2026-04-09
> **Prerequisite reading**: `constraints.md`, `session-log.md` (S188: no splitscreen, single local player)

---

## 1. Background and Root Cause

### The N64 Design

The original Perfect Dark had these player config slots:

| Slots    | Count | Purpose                                   |
|----------|-------|-------------------------------------------|
| `0..7`   | 8     | MP player configs (one per remote/local player slot) |
| `8..15`  | 8     | Coop configs (`MAX_COOPCHRS = MAX_PLAYERS = 8`)      |

Slot 8 (`g_PlayerConfigsArray[MAX_PLAYERS]`) was designated as the **solo player config**. The rationale on N64: solo Joanna is a different "character" than any MP player slot, with different contpad wiring, and coop mode would swap these configs into the active MP slots.

### The PC Port Problem

In the PC port there is exactly one local player. The port already uses `g_PlayerConfigsArray[0]` everywhere for the local player in network multiplayer (`matchsetup.c`, `netmenu.c`, `net.c`). But when entering solo play, `lv.c:464-468` still does:

```c
// lv.c — the root cause
if (g_Vars.mplayerisrunning == false) {
    g_Vars.playerstats[0].mpindex = MAX_PLAYERS;   // = 8
    g_PlayerConfigsArray[MAX_PLAYERS].contpad1 = 0;
    g_PlayerConfigsArray[MAX_PLAYERS].contpad2 = 1;
}
```

This means the solo player's config lives in slot 8 instead of slot 0. Every downstream system that reads `g_Vars.currentplayerstats->mpindex` and uses it to index into `g_PlayerConfigsArray[]` then reads from slot 8 instead of slot 0.

### Why This Is Now a Problem

1. **Two config slots for one player**: Solo options (slot 8) are separate from MP options (slot 0). If you adjust a setting in MP, it doesn't carry into solo and vice versa.
2. **Slot 8 is never saved**: `savefile.c:saveLoadMpPlayer` bounds-checks `playernum < MAX_PLAYERS` (i.e., < 8). Slot 8 is never persisted. Solo options reset every time `lv.c` overwrites them.
3. **Slot 8 is never initialized**: `main.c:317-322` initializes slots 0..7 with `OPTION_FORWARDPITCH` default. Slot 8 starts zeroed.
4. **Caused B-128 (input bug)**: The `actionPlayer` variable in `bondmove.c` was accidentally derived from `mpindex` (= 8) instead of `contpad1` (= 0), breaking all solo input. Fixed by workaround; root cause not yet resolved.
5. **S188 decision**: No splitscreen support — single local player only. Coop/anti mode is dead code. The entire reason slot 8 exists (coop mode swap) is now moot.

### The Fix

Change the solo branch in `lv.c` to use slot 0:

```c
// After fix
if (g_Vars.mplayerisrunning == false) {
    g_Vars.playerstats[0].mpindex = 0;          // was MAX_PLAYERS (8)
    g_PlayerConfigsArray[0].contpad1 = 0;       // was [MAX_PLAYERS]
    g_PlayerConfigsArray[0].contpad2 = 1;       // was [MAX_PLAYERS]
}
```

All downstream consumers already handle `mpindex = 0` correctly because MP mode sets it to 0 (`mplayer.c:589`). This is a **one-site root cause fix with zero-change consumers**.

---

## 2. Complete Callsite Audit

### 2.1 Sites Where `mpindex` Is SET

| File | Line | Value | Mode | Notes |
|------|------|-------|------|-------|
| `src/game/lv.c` | 465 | `MAX_PLAYERS` (8) | Solo | **ROOT CAUSE — must change to 0** |
| `src/game/lv.c` | 466 | writes `g_PlayerConfigsArray[MAX_PLAYERS].contpad1 = 0` | Solo | **ROOT CAUSE — change to [0]** |
| `src/game/lv.c` | 467 | writes `g_PlayerConfigsArray[MAX_PLAYERS].contpad2 = 1` | Solo | **ROOT CAUSE — change to [0]** |
| `src/game/mplayer/mplayer.c` | 589 | `playerstats[0].mpindex = 0` | Coop | Dead code (S188: no splitscreen) — remove |
| `src/game/mplayer/mplayer.c` | 602 | `playerstats[1].mpindexu32 = 1` | Coop | Dead code — remove |
| `src/game/mplayer/mplayer.c` | 618 | `playerstats[mpindex].mpindex = i` | Normal MP | Correct — no change |

### 2.2 Sites Where `g_PlayerConfigsArray[MAX_PLAYERS]` Is Accessed Directly

| File | Lines | Purpose | Action |
|------|-------|---------|--------|
| `src/game/lv.c` | 465–467 | Solo player config init (writes to slot 8) | **Change to slot 0** |
| `src/game/mplayer/mplayer.c` | 580–586 | Coop mode swap: copies slot 8→0, slot 9→1 | **Remove (dead code)** |

### 2.3 Sites That Consume `mpindex` for Config Lookups (No Change Required)

These all pass `currentplayerstats->mpindex` into `g_PlayerConfigsArray[mpindex]` or equivalent. After the fix, mpindex will be 0 in solo mode, which is correct. **No changes needed in these files.**

#### `src/game/options.c` — All option getter/setter functions
All `optionsGet*(mpchrnum)` and `optionsSet*(mpchrnum, ...)` functions index `g_PlayerConfigsArray[mpchrnum]`. With `mpchrnum = mpindex = 0`, they correctly read from slot 0.

| Function | Line range | What it reads |
|----------|-----------|---------------|
| `optionsGetControlMode` | 27 | `controlmode` |
| `optionsSetControlMode` | 32 | `controlmode` |
| `optionsGetContpadNum1` | 37 | `contpad1` |
| `optionsGetContpadNum2` | 42 | `contpad2` |
| `optionsGetForwardPitch` | 47 | `options & OPTION_FORWARDPITCH` |
| `optionsGetAutoAim` | 52 | `options & OPTION_AUTOAIM` |
| `optionsGetLookAhead` | 57 | `options & OPTION_LOOKAHEAD` |
| `optionsGetAimControl` | 62 | `options & OPTION_AIMCONTROL` |
| `optionsGetSightOnScreen` | 67 | `options & OPTION_SIGHTONSCREEN` |
| `optionsGetAmmoOnScreen` | 72 | `options & OPTION_AMMOONSCREEN` |
| `optionsGetShowGunFunction` | 77 | `options & OPTION_SHOWGUNFUNCTION` |
| `optionsGetAlwaysShowTarget` | 82 | `options & OPTION_ALWAYSSHOWTARGET` |
| `optionsGetShowZoomRange` | 87 | `options & OPTION_SHOWZOOMRANGE` |
| `optionsGetPaintball` | 92 | `options & OPTION_PAINTBALL` |
| `optionsGetShowMissionTime` | 97 | `options & OPTION_SHOWMISSIONTIME` |
| `optionsGetHeadRoll` | 112 | `options & OPTION_HEADROLL` |
| `optionsSet*` | 117–234 | All set counterparts |

**All correct after fix** — mpindex 0 → slot 0 ✓

#### `src/game/bondmove.c`

| Line | Call | Current behavior | After fix |
|------|------|-----------------|-----------|
| 929 | `optionsGetControlMode(mpindex)` | reads slot 8 controlmode | reads slot 0 controlmode ✓ |
| 938 | `optionsGetContpadNum1(mpindex)` | reads slot 8 contpad1 (= 0) | reads slot 0 contpad1 (= 0) ✓ |
| 1102 | `optionsGetContpadNum2(mpindex)` | reads slot 8 contpad2 (= 1) | reads slot 0 contpad2 (= 1) ✓ |

Note: Line 944 correctly uses `contpad1` (not `mpindex`) for `actionPlayer`. This was the B-128 workaround. After the fix, this code remains correct and the comment becomes a note about past behavior.

#### `src/game/bondbike.c`

| Line | Call | After fix |
|------|------|-----------|
| 202 | `optionsGetContpadNum1(mpindex)` | slot 0 contpad1 ✓ |

#### `src/game/bondeyespy.c`

| Line | Call | After fix |
|------|------|-----------|
| 698 | `optionsGetContpadNum1(mpindex)` | slot 0 contpad1 ✓ |
| 755 | `optionsGetContpadNum2(mpindex)` | slot 0 contpad2 ✓ |

#### `src/game/bondview.c`

| Line | Call | After fix |
|------|------|-----------|
| 1255 | `optionsGetContpadNum1(mpindex)` | slot 0 contpad1 ✓ |

#### `src/game/bondwalk.c`

| Line | Call | After fix |
|------|------|-----------|
| 805 | `g_PlayerConfigsArray[mpindex].base.unk1c` | slot 0 unk1c ✓ |
| 828 | `g_PlayerConfigsArray[mpindex].base.unk1c` | slot 0 unk1c ✓ |

Only in `normmplayerisrunning` path (MP mode). In solo, `mplayerisrunning == false` and `normmplayerisrunning == false`, so these lines are not reached for solo play. No immediate impact, but they will be correct after the fix.

#### `src/game/bondgun.c`

| Line | Call | After fix |
|------|------|-----------|
| 221 | `optionsGetControlMode(mpindex)` | slot 0 ✓ |
| 266 | `optionsGetControlMode(mpindex)` | slot 0 ✓ |
| 11721 | `SETFUNCPRI()` macro → `g_PlayerConfigsArray[mpindex].gunfuncs` | slot 0 ✓ |
| 11722 | `SETFUNCSEC()` macro → `g_PlayerConfigsArray[mpindex].gunfuncs` | slot 0 ✓ |
| 11867 | `g_PlayerConfigsArray[mpindex].gunfuncs` | slot 0 ✓ |
| 12882 | `optionsGetShowGunFunction(mpindex)` | slot 0 ✓ |

The `gunfuncs` bitmask tracks which weapon function (primary/secondary) is selected per player. These are runtime state only, not persisted. After the fix they go to slot 0 — correct for the single local player.

#### `src/game/activemenu.c`

| Line | Code | After fix |
|------|------|-----------|
| 72 | `g_MpPlayerNum = mpindex` | `g_MpPlayerNum = 0` ✓ |
| 370 | `g_PlayerConfigsArray[mpindex].gunfuncs` | slot 0 ✓ |

`g_MpPlayerNum = 0` is correct. The menu system uses `g_MpPlayerNum % MAX_LOCAL_PLAYERS` which evaluates to `0 % 4 = 0`.

#### `src/game/activemenutick.c`

| Line | Call | After fix |
|------|------|-----------|
| 44 | `optionsGetControlMode(mpindex)` | slot 0 ✓ |

#### `src/game/menu.c`

| Line | Code | After fix |
|------|------|-----------|
| 3220 | `optionsGetContpadNum1(mpindex)` | slot 0 ✓ |
| 3227 | `optionsGetContpadNum2(mpindex)` | slot 0 ✓ |
| 3561 | `mpindex = g_MpPlayerNum % MAX_LOCAL_PLAYERS` | local var; `0 % 4 = 0` ✓ |

#### `src/game/mainmenu.c`

| Line | Code | After fix |
|------|------|-----------|
| 131 | `g_PlayerExtCfg[mpindex & 3].extcontrols = ...` | `g_PlayerExtCfg[0]` ✓ |
| 135 | `g_Menus[g_MpPlayerNum].main.mpindex = mpindex` | stores 0 ✓ |

#### `src/game/menutick.c`

| Line | Code | After fix |
|------|------|-----------|
| 492 | `playerstats[k].mpindex == i` | `0 == 0` when k=0, i=0 ✓ |

This loop iterates i over active MP player slots. In solo mode after the fix, mpindex=0, so `playerstats[0].mpindex == 0` matches `i=0`. Correct.

#### `src/lib/joy.c`

| Line | Code | Context | After fix |
|------|------|---------|-----------|
| 1079 | `*pad1 = playerstats[playernum].mpindex` | In `normmplayerisrunning` branch | Returns 0 ✓ |
| 1086 | `g_PlayerConfigsArray[playerstats[playernum].mpindex].controlmode` | In solo branch | reads slot 0 ✓ |

`joyGetContpadNumsForPlayer` has two branches:
- `normmplayerisrunning` (network MP): returns mpindex as pad1. After fix, mp mode still uses mpindex=0..7 (set by `mplayer.c:618`). Solo mode doesn't hit this branch.
- Solo branch (line 1084+): returns `playernum` (0) as pad1, regardless of mpindex. The mpindex lookup at line 1086 for `controlmode` will correctly use slot 0 after the fix.

### 2.4 Macro Definitions (No Change Required)

| Location | Macro | Current behavior | After fix |
|----------|-------|-----------------|-----------|
| `src/include/constants.h:128` | `FUNCISSEC()` | `g_PlayerConfigsArray[mpindex=8].gunfuncs` | `g_PlayerConfigsArray[mpindex=0].gunfuncs` ✓ |
| `src/include/data.h:562` | `PLAYER_EXTCFG()` | `g_PlayerExtCfg[8 & 3] = g_PlayerExtCfg[0]` | `g_PlayerExtCfg[0 & 3] = g_PlayerExtCfg[0]` ✓ (same result) |

`PLAYER_EXTCFG()` uses `mpindex & 3`. Both `8 & 3 = 0` and `0 & 3 = 0` evaluate to the same index. **No behavioral change for this macro.**

### 2.5 Port Code (Already Correct, No Change Needed)

The port code already uses `g_PlayerConfigsArray[0]` for the local player everywhere. This is already consistent with the proposed fix.

| File | Lines | Notes |
|------|-------|-------|
| `port/src/net/matchsetup.c` | 124–156 | Uses slot 0 for local player ✓ |
| `port/src/net/netmenu.c` | 159–177, 385–402, 849–905 | Uses slot 0 for local player ✓ |
| `port/src/net/net.c` | 339–368 | Uses `playernum` arg (0 for local) ✓ |
| `port/src/net/netmsg.c` | 785–4232 | Uses `ncl->playernum` ✓ |
| `port/fast3d/pdgui_bridge.c` | 48–150 | Uses `playernum` arg ✓ |
| `port/src/savefile.c` | 598–763 | Bounds-checks `playernum < MAX_PLAYERS` (0–7); slot 8 never saved ✓ |
| `port/src/main.c` | 317–322 | Initializes slots 0..7 only; slot 8 starts zeroed ✓ |

### 2.6 Dead Code (No Change Needed — Already Dead)

These are the coop/anti mode config swap and the associated mpindex assignments. S188 confirmed: no splitscreen, single local player, coop/anti mode is dead.

| File | Lines | Code | Status |
|------|-------|------|--------|
| `src/game/mplayer/mplayer.c` | 577–612 | Coop/anti config swap (slot 8↔0, slot 9↔1) | Dead — coop not in scope |
| `src/game/mplayer/mplayer.c` | 589, 602 | `playerstats[0].mpindex = 0`, `playerstats[1].mpindexu32 = 1` | Dead — coop branch |

These can be removed as part of broader dead-code cleanup, but are **not required** for this refactor.

---

## 3. Save Data Analysis

**No save format impact.** The save migration framework exists (`SAVE_VERSION` in `savefile.c`) but is not needed here because:

1. `saveSaveMpPlayer` / `saveLoadMpPlayer` bound-check `playernum < MAX_PLAYERS` (< 8). Slot 8 was **never saved**.
2. Solo player options (`contpad1/2`, `controlmode`, `options`) are runtime state only. They are re-initialized each time `lv.c` runs (before a mission). They are not persisted to disk.
3. The MP player profile JSON (slots 0–7) will still load correctly. After the fix, the solo player reads their options from slot 0, which is the same slot as their MP profile. This is the desired behavior.

**Config (pd.ini) impact**: None. `g_PlayerExtCfg[]` is registered with pd.ini (keys `Game.Player1..Player4`), not `g_PlayerConfigsArray[]`. The pd.ini config is indexed by local player number (0–3), not mpindex. `PLAYER_EXTCFG()` uses `mpindex & 3` which is `0 & 3 = 0` before and after the fix — same result.

---

## 4. Risk Assessment

### CRITICAL — The root cause site

| File | Lines | Risk | Why |
|------|-------|------|-----|
| `src/game/lv.c:464–468` | 3 lines | **LOW** | One-site fix. MP mode already sets mpindex=0 (`mplayer.c:589`). All consumers already handle 0 correctly. |

### HIGH — Coop mode swap (dead code, but structural)

| File | Lines | Risk | Why |
|------|-------|------|-----|
| `src/game/mplayer/mplayer.c:577–612` | ~36 lines | MEDIUM (if removing) | Swap assumes slot 8 has solo config. After Phase 1 fix, slot 8 no longer holds anything useful. Swap should be removed. But since coop mode is dead (S188), leaving it in place is also safe — it simply doesn't run. |

**Recommended**: Remove the coop swap in the same phase as the `lv.c` fix, with a comment explaining why. Do NOT leave it in place as a potential source of confusion for future developers.

### LOW — All consumers

All other callsites (bondmove.c, bondgun.c, joy.c, activemenu.c, etc.) consume mpindex but do not set it. After Phase 1, they receive mpindex=0 in solo mode. Since mpindex=0 is already the correct value in MP mode, and all these consumers have been working correctly in MP mode, the only risk is that slot 0 has different data than slot 8 currently does. The data in slot 0 (initialized by `main.c`, potentially loaded from MP profile save) is actually better than slot 8 (always overwritten by lv.c with hardcoded values and never saved).

### ZERO RISK — No-op sites

| File | Notes |
|------|-------|
| `data.h:562` `PLAYER_EXTCFG()` macro | `8 & 3 = 0` and `0 & 3 = 0` — identical result |
| `activemenu.c:72` `g_MpPlayerNum = mpindex` | `g_MpPlayerNum = 0` — already the correct value in solo |
| `menutick.c:492` comparison | `mpindex == 0` matches loop variable i=0 — correct |
| `menu.c:3561` local `mpindex` var | `g_MpPlayerNum % 4 = 0 % 4 = 0` — same result |

---

## 5. Phase Plan

### Phase 1 — Root Cause Fix (1 site, 3 lines, low risk)

**File**: `src/game/lv.c:464–468`

**Before**:
```c
if (g_Vars.mplayerisrunning == false) {
    g_Vars.playerstats[0].mpindex = MAX_PLAYERS;
    g_PlayerConfigsArray[MAX_PLAYERS].contpad1 = 0;
    g_PlayerConfigsArray[MAX_PLAYERS].contpad2 = 1;
}
```

**After**:
```c
if (g_Vars.mplayerisrunning == false) {
    g_Vars.playerstats[0].mpindex = 0;
    g_PlayerConfigsArray[0].contpad1 = 0;
    g_PlayerConfigsArray[0].contpad2 = 1;
}
```

**Verification**: Build and play a solo mission. Confirm:
- Player can move (WASD/gamepad)
- Weapons fire correctly
- Pause menu opens (START)
- B-128 comment in `bondmove.c:940-943` is now historically accurate — the fix is at root, not as workaround

### Phase 2 — Remove Coop Dead Code (optional cleanup, separate PR)

**File**: `src/game/mplayer/mplayer.c:577–612`

Remove the coop/anti branch that swaps config slots 8/9 into slots 0/1. This branch has been dead since S188 declared no splitscreen support. Its removal prevents future confusion about why slot 8 was ever special.

**Scope**:
- Remove `if (g_Vars.coopplayernum >= 0 || g_Vars.antiplayernum >= 0) { ... }` block (lines 577–613)
- The `else` block for normal MP (lines 615–629) stays — this sets up mp player slots 0..7 correctly

**Verification**: Network multiplayer match starts and player configs are assigned correctly.

### Phase 3 — Clean Up B-128 Comment (cosmetic, lowest priority)

**File**: `src/game/bondmove.c:940–943`

The comment explaining why `contpad1` is used instead of `mpindex` for `actionPlayer` (the B-128 workaround documentation) should be updated to reflect that the root cause is now fixed in `lv.c`. The code itself is still correct — using `contpad1` for action map indexing is the right pattern regardless of mpindex value.

**Scope**: Update comment text only, no code change.

---

## 6. What This Does NOT Change

This refactor is explicitly scoped to the single-line mpindex assignment. It does **not**:

- Change how MP mode assigns mpindex (mplayer.c:616–628 stays)
- Change the `g_PlayerConfigsArray` array size or `MAX_MPPLAYERCONFIGS`
- Affect save file format (slot 8 was never saved)
- Affect pd.ini config format (g_PlayerExtCfg is indexed differently)
- Change the `options*()` function signatures
- Affect network protocol (already uses slot 0 for local player)
- Address any other uses of `MAX_PLAYERS` as an array bound (those are intentional: `g_Vars.players[MAX_PLAYERS]`, `playerstats[MAX_PLAYERS]`, etc.)

---

## 7. Testing Checklist

After Phase 1:

- [ ] Build succeeds, no new warnings
- [ ] Solo mission: player moves with WASD and gamepad
- [ ] Solo mission: weapons fire with left-click/Z
- [ ] Solo mission: pause menu opens with ESC/Start
- [ ] Solo mission: options changes (control mode, etc.) take effect
- [ ] MP match (LAN): local player input still works
- [ ] MP match: second client player input still works
- [ ] No regressions in menu navigation (solo main menu → mission select)
- [ ] FoV and mouse sensitivity settings (g_PlayerExtCfg[0]) unchanged
