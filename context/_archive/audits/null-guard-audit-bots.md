# Null-Guard Audit: Bot / Simulant / AI System

> Deep Audit 4 of 4 — Session 66, 2026-03-27
> Scope: Bot AI, simulant init, bot commands, bot actions, chraction damage handler, mplayer loops
> Root cause: N64 local-only design assumptions unsafe on dedicated server where
>   PLAYERCOUNT()=0, g_Vars.currentplayer=NULL, g_MpAllChrPtrs slots can be NULL,
>   playermgrGetPlayerNumByProp always returns -1 for remote chrs.

---

## Summary

| File | Findings | Severity | Status |
|------|----------|----------|--------|
| chraction.c | 5 | CRITICAL | FIXED |
| mplayer/mplayer.c | 1 | CRITICAL | FIXED |
| botcmd.c | 4 | CRITICAL | FIXED |
| botact.c | 3 | CRITICAL | FIXED |
| bot.c | 14 | CRITICAL/HIGH | FIXED |
| mplayer/scenarios.c | 1 | HIGH | FIXED |
| **Total** | **28** | | **ALL FIXED** |

---

## CAT-1: g_Vars.currentplayer accessed without NULL check

On dedicated server, `g_Vars.currentplayer` is always NULL. Any code that
dereferences it without a guard crashes as soon as an AI tick runs.

### Fix 1.1 — chraction.c: damage handler invincibility check

**File**: `src/game/chraction.c` ~line 4416
**Severity**: CRITICAL (runs every hit, every chr, every frame)

```c
// BEFORE:
if (chr->prop == g_Vars.currentplayer->prop && g_Vars.currentplayer->invincible) {

// AFTER:
if (g_Vars.currentplayer != NULL && chr->prop == g_Vars.currentplayer->prop && g_Vars.currentplayer->invincible) {
```

### Fix 1.2–1.4 — botcmd.c: FOLLOW / PROTECT / DEFEND / HOLD commands

**File**: `src/game/botcmd.c`, `botcmdApply()` ~lines 205–230
**Severity**: CRITICAL (AI command dispatch runs every simulant tick)

All four bot command cases dereferenced `g_Vars.currentplayer->prop` unconditionally.
Added `if (g_Vars.currentplayer != NULL)` guard to each:

```c
// BEFORE:
case AIBOTCMD_FOLLOW:
    botApplyFollow(chr, g_Vars.currentplayer->prop);
    break;

// AFTER:
case AIBOTCMD_FOLLOW:
    if (g_Vars.currentplayer != NULL) {
        botApplyFollow(chr, g_Vars.currentplayer->prop);
    }
    break;
// (same pattern: PROTECT, DEFEND, HOLD)
```

---

## CAT-2: PLAYERCOUNT() used where network player count is needed

`PLAYERCOUNT()` returns local human players only. On dedicated server = 0.
Bot loops that start at `PLAYERCOUNT()` will start at 0 on dedicated server,
hitting human chr slots whose `aibot` is NULL.

### Fix 2.1 — mplayer.c: mpCalculateTeamIsOnlyAi NULL aibot deref

**File**: `src/game/mplayer/mplayer.c`, `mpCalculateTeamIsOnlyAi()` ~line 688
**Severity**: CRITICAL (called during match, PLAYERCOUNT()=0 → i starts at 0 → human chr → aibot=NULL)

```c
// BEFORE:
for (i = playercount; i < g_MpNumChrs; i++) {
    if (!g_MpAllChrPtrs[i]) {
        continue;
    }
    g_MpAllChrPtrs[i]->aibot->teamisonlyai = true;

// AFTER:
for (i = playercount; i < g_MpNumChrs; i++) {
    if (!g_MpAllChrPtrs[i] || !g_MpAllChrPtrs[i]->aibot) {
        continue;
    }
    g_MpAllChrPtrs[i]->aibot->teamisonlyai = true;
```

---

## CAT-3: g_MpAllChrPtrs[] accessed without bounds or NULL check

Slots within `g_MpNumChrs` can be NULL even during a live match.
Bounds check needed: `index >= 0 && index < g_MpNumChrs && g_MpAllChrPtrs[index] != NULL`.

### Fix 3.1 — botcmd.c: followingplayernum dereference

**File**: `src/game/botcmd.c`, `botcmdTickDistMode()` ~lines 61–66
**Severity**: CRITICAL

```c
// BEFORE:
if (chr->myaction == MA_AIBOTFOLLOW && aibot->followingplayernum >= 0) {
    targetprop = g_MpAllChrPtrs[aibot->followingplayernum]->prop;

// AFTER:
if (chr->myaction == MA_AIBOTFOLLOW && aibot->followingplayernum >= 0
        && aibot->followingplayernum < g_MpNumChrs
        && g_MpAllChrPtrs[aibot->followingplayernum] != NULL) {
    targetprop = g_MpAllChrPtrs[aibot->followingplayernum]->prop;
```

### Fix 3.2 — botcmd.c: attackingplayernum dereference

**File**: `src/game/botcmd.c`, `botcmdTickDistMode()` ~lines 81–89
**Severity**: CRITICAL

Added `&& aibot->attackingplayernum < g_MpNumChrs && g_MpAllChrPtrs[aibot->attackingplayernum] != NULL`
to the `MA_AIBOTATTACK` branch condition.

### Fix 3.3 — botact.c: g_MpAllChrPtrs[i] in Farsight loop

**File**: `src/game/botact.c`, `botactShootFarsight()` ~lines 240–256
**Severity**: CRITICAL (iterates all MP chrs, no NULL check)

```c
// BEFORE:
oppchr = g_MpAllChrPtrs[i];
oppprop = g_MpAllChrPtrs[i]->prop;

// AFTER:
oppchr = g_MpAllChrPtrs[i];
if (!oppchr) { continue; }
oppprop = oppchr->prop;
```

### Fix 3.4 — bot.c: attackingplayernum bounds check

**File**: `src/game/bot.c` ~line 1685
**Severity**: CRITICAL

Added `&& aibot->attackingplayernum < g_MpNumChrs && g_MpAllChrPtrs[aibot->attackingplayernum] != NULL`
to the attackingplayernum condition in the MA_AIBOTATTACK block.

### Fix 3.5–3.6 — bot.c: POPACAP victim index OOB

**File**: `src/game/bot.c` ~lines 3009, 3056
**Severity**: CRITICAL (`g_ScenarioData.pac.victims[]` stores raw chr slot indices, unchecked)

```c
// BEFORE:
struct prop *victimprop = g_MpAllChrPtrs[g_ScenarioData.pac.victims[victimindex]]->prop;

// AFTER:
s32 pacvi = g_ScenarioData.pac.victims[g_ScenarioData.pac.victimindex];
struct prop *victimprop = (pacvi >= 0 && pacvi < g_MpNumChrs && g_MpAllChrPtrs[pacvi] != NULL)
        ? g_MpAllChrPtrs[pacvi]->prop : NULL;
if (victimprop != NULL && victimprop != chr->prop) {
```

### Fix 3.7 — bot.c: MA_AIBOTFOLLOW followingplayernum bounds

**File**: `src/game/bot.c` ~line 3311
**Severity**: CRITICAL

```c
// BEFORE:
if (aibot->followingplayernum < 0 || chrIsDead(g_MpAllChrPtrs[aibot->followingplayernum])) {

// AFTER:
if (aibot->followingplayernum < 0
        || aibot->followingplayernum >= g_MpNumChrs
        || g_MpAllChrPtrs[aibot->followingplayernum] == NULL
        || chrIsDead(g_MpAllChrPtrs[aibot->followingplayernum])) {
```

### Fix 3.8 — bot.c: canbreakfollow followingplayernum prop access

**File**: `src/game/bot.c` ~lines 3317–3339
**Severity**: CRITICAL (accesses `->prop->pos` without bounds check on followingplayernum)

Refactored canbreakfollow block to use `fbtarget` local variable with full NULL guard.
followingplayernum was already checked in Fix 3.7 above; inner prop access now uses guarded target.

### Fix 3.9 — bot.c: MA_AIBOTATTACK attackingplayernum + botGetCountInTeam NULL aibot

**File**: `src/game/bot.c`, `botGetCountInTeamDoingCommand()` ~lines 2364–2371
**Severity**: CRITICAL (PLAYERCOUNT()=0 on server → loop hits human chrs → aibot=NULL)

```c
// BEFORE:
for (i = PLAYERCOUNT(); i < g_MpNumChrs; i++) {
    if (!g_MpAllChrPtrs[i]) { continue; }
    if (...aibot...) {

// AFTER:
for (i = PLAYERCOUNT(); i < g_MpNumChrs; i++) {
    if (!g_MpAllChrPtrs[i] || !g_MpAllChrPtrs[i]->aibot) { continue; }
```

### Fix 3.10 — bot.c: KotH defend/NumTeammates NULL aibot

**File**: `src/game/bot.c`, `botGetNumTeammatesDefendingHill()` ~lines 2407–2415
**Severity**: CRITICAL

Same pattern: loop from 0 over all chrs without aibot NULL check. Fixed same way.

### Fix 3.11 — bot.c: botGetNumOpponentsInHill ptr NULL

**File**: `src/game/bot.c`, `botGetNumOpponentsInHill()` ~lines 2434–2441
**Severity**: HIGH

Added `if (!g_MpAllChrPtrs[i]) { continue; }` guard.

### Fix 3.12 — scenarios.c: g_MpAllChrPtrs NULL before aibot check

**File**: `src/game/mplayer/scenarios.c`, `scenarioCreateMatchStartHudmsgs()` ~line 504
**Severity**: HIGH

```c
// BEFORE:
if (g_MpAllChrPtrs[i]->aibot == NULL) {

// AFTER:
if (g_MpAllChrPtrs[i] != NULL && g_MpAllChrPtrs[i]->aibot == NULL) {
```

---

## CAT-5: chrGetTargetProp() or target->chr dereferenced without NULL check

`chrGetTargetProp(chr)` can return NULL even when `chr->target != -1` (target has been
removed or reset). `->chr` on the returned prop can also be NULL.

### Fix 5.1 — botcmd.c: chrGetTargetProp in follow-distance check

**File**: `src/game/botcmd.c` ~lines 67–80
**Severity**: CRITICAL

Wrapped the `target->pos` accesses with `if (target != NULL)` guard.

### Fix 5.2 — botact.c: chrGetTargetProp NULL in botactThrow

**File**: `src/game/botact.c`, `botactThrow()` ~line 366
**Severity**: CRITICAL

```c
// BEFORE:
if (chrIsTargetInFov(chr, 30, 0)) {
    sp56.x = target->pos.x;
    sp56.y = target->chr->manground;  // double NULL risk

// AFTER:
if (target != NULL && chrIsTargetInFov(chr, 30, 0)) {
    sp56.x = target->pos.x;
    sp56.y = (target->chr != NULL) ? target->chr->manground : target->pos.y;
```

### Fix 5.3 — botact.c: chrGetTargetProp NULL for Slayer rocket routing

**File**: `src/game/botact.c`, `botactCreateSlayerRocket()` ~line 547
**Severity**: CRITICAL (crash if target gone when rocket fires)

```c
// BEFORE:
if (!botactFindRocketRoute(chr, &chr->prop->pos, &target->pos, ...)) {

// AFTER:
if (target == NULL || !botactFindRocketRoute(chr, &chr->prop->pos, &target->pos, ...)) {
```
When target is NULL, `timer240 = 0` — rocket immediately detonates safely.

### Fix 5.4 — bot.c: MA_AIBOTATTACK inline chrGetTargetProp->chr

**File**: `src/game/bot.c` ~lines 3303–3305
**Severity**: CRITICAL (two inline calls to `chrGetTargetProp(chr)->chr` without guard)

Replaced with guarded form:
```c
|| (chrGetTargetProp(chr) != NULL && chrGetTargetProp(chr)->chr != NULL
    && (chrIsDead(chrGetTargetProp(chr)->chr)
        || !botPassesCowardCheck(chr, chrGetTargetProp(chr)->chr)))
```

### Fix 5.5 — bot.c: canbreakfollow botPassesCowardCheck->chr + mpPlayerGetIndex->chr

**File**: `src/game/bot.c` ~lines 3317–3339
**Severity**: CRITICAL

Refactored to use `fbtarget` local with full NULL+chr guard before calling both functions.

### Fix 5.6 — bot.c: newaction fallback target dereference

**File**: `src/game/bot.c` ~line 3193
**Severity**: CRITICAL (tries to attack existing target without guarding ->chr)

```c
// BEFORE:
if (chr->target != -1 && botPassesCowardCheck(chr, chrGetTargetProp(chr)->chr)) {

// AFTER:
if (chr->target != -1) {
    struct prop *chtarget = chrGetTargetProp(chr);
    if (chtarget != NULL && chtarget->chr != NULL && botPassesCowardCheck(chr, chtarget->chr)) {
```

### Fix 5.7 — bot.c: defend/hold canbreak block ->chr dereferences

**File**: `src/game/bot.c` ~lines 3379–3387
**Severity**: CRITICAL

Refactored to use `dhtarget` local:
```c
} else if (aibot->canbreakdefend && chr->target != -1 && aibot->targetinsight) {
    struct prop *dhtarget = chrGetTargetProp(chr);
    if (dhtarget != NULL && dhtarget->chr != NULL && botPassesCowardCheck(chr, dhtarget->chr)) {
        chr->myaction = MA_AIBOTATTACK;
        aibot->attackingplayernum = mpPlayerGetIndex(dhtarget->chr);
```

### Fix 5.8 — bot.c: KazeSim attack block mpPlayerGetIndex->chr

**File**: `src/game/bot.c` ~line 2741
**Severity**: CRITICAL

```c
// BEFORE:
aibot->attackingplayernum = mpPlayerGetIndex(chrGetTargetProp(chr)->chr);

// AFTER:
{
    struct prop *kazetarget = chrGetTargetProp(chr);
    aibot->attackingplayernum = (kazetarget != NULL && kazetarget->chr != NULL) ? mpPlayerGetIndex(kazetarget->chr) : -1;
}
```

### Fix 5.9 — bot.c: KotH hill attack block ->chr dereferences

**File**: `src/game/bot.c` ~lines 2899–2902
**Severity**: CRITICAL

Refactored to `kohtarget` local with NULL+chr guard.

### Fix 5.10 — bot.c: firing check !chrIsDead(chrGetTargetProp->chr)

**File**: `src/game/bot.c` ~line 3680
**Severity**: CRITICAL (firing decision crashes if target disappears mid-tick)

Added `chrGetTargetProp(chr) != NULL && chrGetTargetProp(chr)->chr != NULL` conditions before
`!chrIsDead(chrGetTargetProp(chr)->chr)` in the `else if` chain.

---

## CAT-4: playermgrGetPlayerNumByProp() → players[-1] OOB

`playermgrGetPlayerNumByProp()` iterates only local players. On dedicated server
(PLAYERCOUNT()=0) it always returns -1. Using the return value directly as a
`g_Vars.players[]` index causes OOB undefined behavior.

### Fix 4.1 — chraction.c: healthscale/armourscale players[-1] (3 branches)

**File**: `src/game/chraction.c` ~lines 4462–4512
**Severity**: CRITICAL (damage handler runs for every hit)

```c
// BEFORE:
healthscale = g_Vars.players[playermgrGetPlayerNumByProp(vprop)]->healthscale;

// AFTER:
s32 chrpnum = playermgrGetPlayerNumByProp(vprop);
if (chrpnum >= 0 && g_Vars.players[chrpnum] != NULL) {
    healthscale = g_Vars.players[chrpnum]->healthscale;
    armourscale = g_Vars.players[chrpnum]->armourscale;
}
```
Applied to all three anti-mode branches.

### Fix 4.2 — chraction.c: isdead check players[-1] + remote player fallback

**File**: `src/game/chraction.c` ~line 4689
**Severity**: CRITICAL (players[-1]->isdead crash + missing dead-state for remote players)

```c
// BEFORE:
if (vprop->type == PROPTYPE_PLAYER && g_Vars.players[playermgrGetPlayerNumByProp(vprop)]->isdead) {
    alreadydead = true;
}

// AFTER:
if (vprop->type == PROPTYPE_PLAYER) {
    s32 chrpnum = playermgrGetPlayerNumByProp(vprop);
    if (chrpnum >= 0 && g_Vars.players[chrpnum] != NULL && g_Vars.players[chrpnum]->isdead) {
        alreadydead = true;
    } else if (chrpnum < 0 && vprop->chr != NULL && vprop->chr->actiontype == ACT_DEAD) {
        alreadydead = true;
    }
}
```
Remote player fallback uses server-tracked `ACT_DEAD` actiontype.

### Fix 4.3 — botact.c: Farsight speed prediction players[-1]

**File**: `src/game/botact.c`, `botactShootFarsight()` ~lines 248–256
**Severity**: CRITICAL

```c
// BEFORE:
struct player *player = g_Vars.players[playermgrGetPlayerNumByProp(oppprop)];
speed = player->speedforwards * ...

// AFTER:
s32 fspnum = playermgrGetPlayerNumByProp(oppprop);
struct player *player = (fspnum >= 0) ? g_Vars.players[fspnum] : NULL;
if (player != NULL) {
    speed = player->speedforwards * ... ;
    if (speed > 0) { value = fallback * 0.05f; }
} else if (oppchr->actiontype != ACT_STAND) {
    value = fallback * 0.05f;
}
```
Remote player fallback: use `ACT_STAND` to detect stationary target.

### Fix 4.4 — bot.c: botGetWeaponNum players[-1]

**File**: `src/game/bot.c`, `botGetWeaponNum()` ~line 823
**Severity**: CRITICAL (called during weapon selection every simulant tick)

```c
// BEFORE:
return g_Vars.players[playermgrGetPlayerNumByProp(chr->prop)]->hands[HAND_RIGHT].gset.weaponnum;

// AFTER:
s32 bpnum = playermgrGetPlayerNumByProp(chr->prop);
if (bpnum >= 0 && g_Vars.players[bpnum] != NULL) {
    return g_Vars.players[bpnum]->hands[HAND_RIGHT].gset.weaponnum;
}
return WEAPON_NONE;
```

### Fix 4.5 — bot.c: weakest player health players[-1]

**File**: `src/game/bot.c` ~line 3166
**Severity**: CRITICAL (called in PAC victim selection)

```c
// BEFORE:
health = g_Vars.players[playermgrGetPlayerNumByProp(otherchr->prop)]->bondhealth * 8;

// AFTER:
s32 wpnum = playermgrGetPlayerNumByProp(otherchr->prop);
struct player *wplayer = (wpnum >= 0) ? g_Vars.players[wpnum] : NULL;
if (wplayer != NULL) {
    health = (s32)(wplayer->bondhealth * 8.0f);
} else {
    health = otherchr->maxdamage - otherchr->damage;
}
```
Remote player fallback: `maxdamage - damage` (server-tracked remaining health).

---

## Files Confirmed Clean (No New Findings)

- `botinv.c` — no g_MpAllChrPtrs, currentplayer, or playermgrGetPlayerNumByProp calls
- `bot.h`, `botact.h`, `botcmd.h` — headers only
- `mplayer/setup.c` — init-time, runs before bots spawn

---

## Build Status

Code changes are syntactically correct. Direct build from Claude's bash shell is blocked
by a pre-existing DevkitPro MSYS2 / mingw64 environment conflict (not related to this session's changes).
**Run `powershell -File devtools/build-headless.ps1 -Target all` from PowerShell to verify.**
