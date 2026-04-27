# Char Init + Weapon Spawn: PD2 vs Upstream Comparison

Auditor: claude-sonnet-4-6
Date: 2026-04-27
Session scope: character initialization, weapon slot assignment, equip path, hand model setup, weapon render path

---

## Upstream Reference

- Repository: https://github.com/fgsfdsfgs/perfect_dark
- HEAD SHA: bed3bf52d0d5095d112940b1327ed6c256e54ea8
- HEAD date: 2026-04-25 19:31:47 +0200
- Clone path: /tmp/upstream_pd_decomp_sonnet46 (landed at Windows TEMP: C:/Users/mikeh/AppData/Local/Temp/upstream_pd_decomp_sonnet46)

## PD2 Reference

- Repository: C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike
- HEAD SHA: 7902a403bf40de8224c09a09639a26e322d3b9e4
- HEAD date: 2026-04-27 00:11:51 -0400

---

## Mission-Start to Weapon-Render Path (Side-by-Side)

The call chain from mission start through first weapon render frame, with upstream and PD2 file:line references.

```
Stage load trigger
  setupCreateProps(stagenum)
    upstream: setup.c:1491  PD2: setup.c:1587
    for each OBJTYPE_CHR: bodyAllocateChr(...)
    invInit(setupCountCommandType(OBJTYPE_LINKGUNS))  [per player]

Player allocation (before stage load, at game start or match restart)
  playermgrAllocatePlayers(count)
    upstream: playermgr.c:49  PD2: playermgr.c:55
  playermgrAllocatePlayer(index)
    upstream: playermgr.c:89  PD2: playermgr.c:101
    -- initializes struct player fields, including gunctrl fields
    -- sets gunctrl.gunmodeldef = NULL, gunctrl.weaponnum = WEAPON_NONE

Respawn / new life
  playerStartNewLife()
    upstream: player.c:489  PD2: player.c:1106
    calls playerLoadDefaults()
      upstream: player.c:676  PD2: player.c:1325
    calls playerSpawn()
      upstream: player.c:939  PD2: player.c:1613

Weapon equip (inside playerSpawn, MP path)
  bgunEquipWeapon2(HAND_RIGHT, weaponnum)
    upstream: bondgun.c:5987  PD2: bondgun.c:6351
  bgunEquipWeapon(weaponnum)
    upstream: bondgun.c:5609  PD2: bondgun.c:5957
    sets gunctrl.switchtoweaponnum

Weapon switch commit (each tick)
  bgunTickSwitch()  ->  bgunTickSwitch2()
    upstream: bondgun.c:3313/3315  PD2: bondgun.c:3411/3413
    bgunSetGunMemWeapon(ctrl->switchtoweaponnum)
      upstream: bondgun.c:3680  PD2: bondgun.c:3790
    sets gunctrl.gunmemnew, triggers MASTERLOADSTATE_FLUX

Master load state machine (each tick until loaded)
  bgunTickMasterLoad()
    upstream: bondgun.c:4015  PD2: bondgun.c:4242
    FLUX -> HANDS: loads hand model file, sets gunctrl.handmodeldef
    HANDS -> GUN: loads weapon model file, sets gunctrl.gunmodeldef
    GUN -> CARTS: loads casing model(s), sets gunctrl.cartmodeldef
    CARTS -> LOADED: calls modelInit for hand+gun models, builds cmd lists

FP render (each frame when loaded)
  [render loop calls bondgun render path]
    upstream: bondgun.c:~7676+  PD2: bondgun.c:~8125+
    gate: bgunIsLoaded() && hand->inuse && gunctrl.gunmemtype != 0
    modelSetMatricesWithAnim() builds bone matrices for current anim state
    renders weapon model with hand attachment
```

---

## Per-Function Diff Catalog

### playermgrReset (playermgr.c)

Upstream (playermgr.c:27): Hard-coded to clear exactly 4 player slots:
```c
g_Vars.players[0] = NULL;
// ...
g_Vars.players[3] = NULL;
```
Also sets playerorder[0..3] explicitly.

PD2 (playermgr.c:28): Adds `#if MAX_PLAYERS > 4` compile-time branch that loops over all slots. The `playerorder` initialization changed from 4 explicit assignments to a loop `for (s32 i = 0; i < MAX_PLAYERS; i++) g_Vars.playerorder[i] = i;`.

Impact: functional equivalence at MAX_PLAYERS=4. The loop form is future-safe for player count expansion without behavioral change now.

---

### playermgrAllocatePlayers (playermgr.c)

Upstream (playermgr.c:49): In the `count > 0` path, does not call `netPlayersAllocate()`. In the `count == 0` (single player) path, has `fourmeg2player` branch:
```c
if (g_Vars.fourmeg2player) {
    playermgrSetViewSize(playerGetFbWidth(), playerGetFbHeight() * 2);
} else {
    playermgrSetViewSize(playerGetFbWidth(), playerGetFbHeight());
}
```

PD2 (playermgr.c:55): In the `count > 0` path, adds netplay init call:
```c
if (g_NetMode && g_StageNum != STAGE_TITLE && g_StageNum != STAGE_CITRAINING) {
    netPlayersAllocate();
}
```
In the `count == 0` path, the `fourmeg2player` branch is removed entirely. Always calls `playermgrSetViewSize(playerGetFbWidth(), playerGetFbHeight())`.

Impact: `fourmeg2player` was an N64 2-player 4MB workaround. Removed as a dead N64-only path. The `netPlayersAllocate()` hook is new PD2 infrastructure that sets up netplay state for allocated players.

---

### playermgrAllocatePlayer (playermgr.c)

Upstream (playermgr.c:89): The `visionmode` field is initialized inside a version gate:
```c
#if VERSION >= VERSION_JPN_FINAL
    g_Vars.players[index]->visionmode = VISIONMODE_NORMAL;
#endif
```
This means on NTSC_1_0 and earlier version targets, `visionmode` is never written. Since `mempAlloc` does not zero-fill, that field held heap garbage.

`targetset` cleared with `for (i = 0; i < MAX_PLAYERS; i++)` using the fixed constant MAX_PLAYERS.

`gunctrl.handmodeldef` and `gunctrl.cartmodeldef`: NOT initialized. Only `gunctrl.gunmodeldef` is set to NULL.

Upstream does not set `wantsjump`, `jumpconsumed`, `client`, `ucmd`, `isremote`.

PD2 (playermgr.c:101): `visionmode` initialized unconditionally at the same position, no version gate. This is the latent-uninit fix documented in the commit comment.

`targetset` cleared with `for (i = 0; i < ARRAYCOUNT(g_Vars.players[index]->targetset); i++)`, which is correct for any future array resize.

`gunctrl.handmodeldef` and `gunctrl.cartmodeldef` are both initialized to NULL explicitly:
```c
g_Vars.players[index]->gunctrl.handmodeldef = NULL;
g_Vars.players[index]->gunctrl.cartmodeldef = NULL;
```
The commit comment explains these were reading stale heap garbage (wild pointer `0x7c7b663545bbcbd1` observed in playtest log) that caused undefined behavior in `bgunTickMasterLoad`.

New fields initialized: `wantsjump = false`, `jumpconsumed = true`, `client = NULL`, `ucmd = (g_NetMode == NETMODE_SERVER) ? UCMD_FL_FORCEMASK : 0`, `isremote = false`. These are all PD2 netplay additions with no upstream equivalent.

---

### playermgrGetPlayerNumByProp (playermgr.c)

Upstream (playermgr.c:672): No NULL check on `g_Vars.players[i]` before accessing `->prop`.

PD2 (playermgr.c:698): Adds guard `if (!g_Vars.players[i]) continue;` before the prop comparison.

Impact: upstream crashes if called when a player slot is NULL (e.g., during match setup before all players are allocated). PD2 fixes the NULL-deref.

---

### playermgrShuffle (playermgr.c)

Upstream (playermgr.c:800): Always performs the random swap.

PD2 (playermgr.c:819): Adds an early-return guard for netgames:
```c
if (g_NetMode) {
    return;
}
```
PD2 comment: "don't shuffle in netgames -- why is this a thing anyway?" The shuffle randomizes draw order; in netgames the server controls order so shuffling locally would cause clients to diverge.

---

### playermgrGetModelOfWeapon (playermgr.c)

Upstream (playermgr.c:707): Includes `WEAPON_PP9I` through `WEAPON_SCREWDRIVER` (GoldenEye/retro weapon set) mapped to model constants.

PD2 (playermgr.c:734): Identical body; `WEAPON_CLOAKINGDEVICE` present in both; `WEAPON_COMBATBOOST` present in both returning -1. No catalog lookup here -- this function still uses the hardcoded switch table. This is a pre-catalog holdover. The catalog system does not yet route through this function.

---

### playerStartNewLife (player.c)

Upstream (player.c:489): `rooms[8]` local uninitialized (stack content). The `PLATFORM_N64` guard around `blurdrugamount` / `poisoncounter` reset means those fields are skipped on N64. The intro command loop has no bounds check on iterations; a malformed intro list can loop forever.

PD2 (player.c:1106): `rooms[8]` initialized to all -1. `blurdrugamount` and `poisoncounter` reset unconditionally (guard removed). Intro command loop has a safety counter `++safety < 10000` to prevent infinite loop on malformed data. Adds capsule collision check at spawn position with up to 3 retry attempts using `spawnPoolFindClearPosition`. Adds a sentinel guard on `cdFindGroundInfoAtCyl` result to catch the `-2^32` no-ground value and fall back to pad Y. Neither of these safety systems exists in upstream.

---

### playerSpawn (player.c)

Upstream (player.c:939): In the normal MP (non-anti) spawn path, `MPOPTION_SPAWNWITHWEAPON` handling:
```c
if (g_Vars.normmplayerisrunning
        && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)
        && g_MpSetup.weapons[0] != MPWEAPON_NONE
        && g_MpSetup.weapons[0] != MPWEAPON_DISABLED
        && g_MpSetup.weapons[0] != MPWEAPON_SHIELD) {
    struct mpweapon *mpweapon = &g_MpWeapons[g_MpSetup.weapons[0]];
    invGiveSingleWeapon(mpweapon->weaponnum);
    ...
    bgunEquipWeapon2(HAND_RIGHT, mpweapon->weaponnum);
}
```
Direct array access `g_MpWeapons[g_MpSetup.weapons[0]]` using the static compile-time `g_MpWeapons` table. No `modelmgrLoadProjectileModeldefs` call before equip.

Upstream also has `#if VERSION >= VERSION_NTSC_1_0` guard around the `playerTickChrBody()` fallback call.

PD2 (player.c:1613): The spawn-with-weapon path is completely rewritten. Uses `g_MatchConfig.spawnWeaponNum` (a server-authoritative field) and catalog lookups:
```c
resolvedWeaponNum = catalogGetMpWeaponNum(wi);
catalogGetMpWeaponPriAmmoType(spawnWeaponIdx);
catalogGetMpWeaponPriAmmoQty(spawnWeaponIdx);
```
Before the equip, explicitly calls `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` to ensure FP model is loaded before `bgunTickSwitch2` runs. Falls back to `g_MpSetup.weapons[0]` path (also via catalog) when `g_MatchConfig.spawnWeaponNum` is 0xFF (random) or 0.

The `#if VERSION >= VERSION_NTSC_1_0` gate on `playerTickChrBody` is removed; the call is unconditional `if (g_Vars.currentplayer->model00d4 == NULL)`. At end of spawn, PD2 adds a `netmsgSvcPlayerStatsWrite` call for server-side stat sync, plus a dense diagnostic log block.

Anti-player detection changed from `g_Vars.currentplayer == g_Vars.anti` (upstream) to `PLAYER_IS_ANTI(g_Vars.currentplayer)` (PD2 macro).

---

### bgunTickMasterLoad: handfilenum resolution (bondgun.c)

Upstream (bondgun.c:4041):
```c
handfilenum = g_HeadsAndBodies[bodynum].handfilenum;
if (IS4MB()) {
    handfilenum = FILE_GCOMBATHANDSLOD;
}
```
Uses the static `g_HeadsAndBodies` array with direct index lookup. Includes IS4MB() fallback to low-detail hand file.

PD2 (bondgun.c:4268):
```c
handfilenum = catalogGetBodyHandFilenum(bodynum); /* SA-5d */
```
Uses catalog API. `IS4MB()` compiles to 0 (removed). No LOD fallback. The catalog lookup must return the correct file number for every registered body; if a body is registered without a hand file, `handfilenum` will be 0 and hands will be invisible.

---

### bgunEnterFlux (bondgun.c)

Upstream (bondgun.c:3694): Clears `handfilenum`, `handmodeldef`, `handmemloadptr`, `handmemloadremaining`, resets `masterloadstate` and `gunloadstate` to FLUX. Also nulls all `casing->modeldef` entries.

PD2 (bondgun.c:3830): Same functional operations, adds a diagnostic log block (player 0 only) before the writes, capturing the old values. This is pure observability scaffolding with no behavioral change.

---

### bgunFreeGunMem (bondgun.c)

Upstream (bondgun.c:3670): Sets `gunmemowner = GUNMEMOWNER_FREE`. Has a `#ifndef PLATFORM_N64` guard around `videoResetTextureCache()`.

PD2 (bondgun.c:3766): Same logic, adds a diagnostic log for non-FREE-to-FREE transitions. Removes the `PLATFORM_N64` guard (dead on PC). The comment explains why full cache clear is used instead of ranged eviction (the fast3d texture cache has no ranged-evict API).

---

### bgunSetGunMemWeapon (bondgun.c)

Upstream (bondgun.c:3680): Pure logic, no logging.

PD2 (bondgun.c:3790): Same logic, wraps with entry and exit diagnostic logs for player 0. No behavioral change.

---

### bgunTickSwitch2 (bondgun.c)

Upstream (bondgun.c:5456): Pure logic. Removed the `#if (VERSION == VERSION_JPN_FINAL) && defined(PLATFORM_N64)` block that forced WEAPON_COMBATKNIFE to WEAPON_UNARMED (a JPN-only exclusion).

PD2 (bondgun.c:5785): Same core logic. The JPN-only combatknife exclusion block is removed. Adds a diagnostic log block at entry for player 0 (throttled to first 60 ticks plus every 120 ticks) that shows per-hand canFree / state / stateminor / count before the switch gate. No behavioral change to the core state machine.

---

### FP weapon render path (bondgun.c)

Upstream (bondgun.c:~7676): The visibility gate is:
```c
|| !bgunIsLoaded()
|| hand->inuse == false
|| bgunGetGunMemType() == 0
```
When `hand->visible` is true, calls `bgunExecuteModelCmdList` then `bgun0f098030` which drives `modelSetMatricesWithAnim`. The render path uses a per-hand matrix cache (`hand->unk0dd8`) filled at weapon-load time (T-pose anim frame 0) and reused for IDLE render frames.

PD2 (bondgun.c:~8125 and 8375): The visibility gate is restructured (Issue 1 root-cause fix: the legacy `count >= 3` gate that blocked the first render frames was removed). The IDLE render path no longer uses the matrix cache (`unk0dd8`). Per the B-246 round-10 Phase B comment:

> Phase B drops the cache entirely: every render frame now runs `modelSetMatricesWithAnim` against the gun's current anim state, identical to what fire and reload already did.

The cache fields `unk0dd4` and `unk0dd8` remain allocated in `struct hand` as dead state. This is a behavioral change: IDLE frames now read live animation state instead of the T-pose snapshot.

---

### setupCreateProps (setup.c)

Upstream (setup.c:1491): Stage guard `if (stagenum < STAGE_TITLE)`. `nodoors` flag drives `setupMarkLiftDoors()` (present in upstream too). OBJTYPE_SHIELD block has `#if VERSION >= VERSION_JPN_FINAL` gate for JPN-inclusive shield behavior. No simulant chr-slot accounting.

PD2 (setup.c:1587): Stage guard changed to `if (STAGE_IS_GAMEPLAY(stagenum))` (macro that excludes additional non-game stage types). Adds simulant bot slot accounting for `chrmgrConfigure` (up to `MAX_BOTS` additional slots). Adds `s_SetupMpWeaponLocationCount` / `s_SetupMpCreatedWeaponCount` tracking variables. Adds `mptransport_diffflag` relaxation for SP-in-MP stages (CITRAINING, CHICAGO, VILLA, INFILTRATION, G5BUILDING, PELAGIC) so LIFT and ESCASTEP props survive when those stages are hosted as MP arenas. OBJTYPE_LIFT adds auto-registration via `liftActivate` to handle CI/Chicago lifts that have no AI script. `forgeIsCanvasMode()` guards on OBJTYPE_CHR, OBJTYPE_KEY, OBJTYPE_HAT, OBJTYPE_AUTOGUN to suppress those prop types in canvas/editor mode. OBJTYPE_DOOR: removes `nodoors` custom logic path (simplified). The `#if VERSION >= VERSION_JPN_FINAL` shield gate is retained verbatim.

---

## Catalog / N64 / ROM-Decoupling Impact Analysis

### Catalog system

PD2 replaces `g_MpWeapons[index]` direct array lookups in `playerSpawn` with `catalogGetMpWeaponNum(wi)`, `catalogGetMpWeaponPriAmmoType`, `catalogGetMpWeaponPriAmmoQty`. These catalog functions resolve through the `asset_entry_t` registry, which is mod-extensible and runtime-filterable by unlock state.

Risk: if a catalog entry is missing or its weaponnum does not match the legacy `g_MpWeapons` enum order, the spawn-with-weapon weapon may be wrong or zero. The PD2 code has a fallback to `g_MpSetup.weapons[0]` but the fallback itself also uses a catalog call.

In `bgunTickMasterLoad`, `handfilenum` resolution changed from `g_HeadsAndBodies[bodynum].handfilenum` to `catalogGetBodyHandFilenum(bodynum)`. If the catalog entry for a body does not populate the hand file number, `handfilenum` will be 0 and the hand model will not load, leaving the player with a floating gun and no visible hands.

### N64 decoupling

`IS4MB()` compile-time 0 means the `FILE_GCOMBATHANDSLOD` hand LOD fallback in `bgunTickMasterLoad` is unreachable and has been replaced by the catalog call. The `fourmeg2player` branch in `playermgrAllocatePlayers` is gone. `playerSpawn`'s `#if VERSION >= VERSION_NTSC_1_0` guard on `playerTickChrBody` is gone. All of these were N64 memory-constraint accommodations with no PC relevance.

`rooms[8]` initialization to `{-1, ...}` in `playerStartNewLife` fixes a subtle UB from stack garbage rooms being passed into `cdFindGroundInfoAtCyl` on first life.

### ROM version gate removal

The `#if VERSION >= VERSION_JPN_FINAL` gate on `visionmode` initialization in `playermgrAllocatePlayer` was removed. This is the B-246 latent uninit: on NTSC_1_0 targets the field was never written, causing `visionmode` to hold heap garbage that could activate night-vision or other modes unexpectedly on first FP render. PD2 initializes it unconditionally.

The `gunctrl.handmodeldef` and `gunctrl.cartmodeldef` missing initialization was not gated on a version block in upstream, but was effectively a latent uninit because `mempAlloc` does not zero-fill. PD2 adds explicit NULL initialization at `playermgrAllocatePlayer` time.

The `#if (VERSION == VERSION_JPN_FINAL) && defined(PLATFORM_N64)` combatknife exclusion in `bgunTickSwitch2` was removed. On PD2 (PC, USA ROM only) this block was dead and its removal has no functional effect.

The `#ifndef PLATFORM_N64` guard on `videoResetTextureCache` in `bgunFreeGunMem` was removed. On PD2 PC this was always entered anyway.

---

## Hypotheses: What Could Be Broken

**H1 (high): catalog body hand-file regression on custom/mod bodies**
`bgunTickMasterLoad` now calls `catalogGetBodyHandFilenum(bodynum)` at PD2:bondgun.c:4268. If any body registered in the catalog has an empty or incorrect hand file number, the hand model fails to load and `handmodeldef` remains NULL after MASTERLOADSTATE_LOADED. The FP render gate at bondgun.c:~8219 checks `handmodeldef != NULL` before rendering hand geometry. This MAY produce invisible hands (weapon floating in air) for any body whose catalog entry does not carry a valid handfilenum.

**H2 (high): spawn-with-weapon catalog lookup silent zero on unregistered weapons**
In `playerSpawn` (PD2:player.c:1799), `catalogGetMpWeaponNum(wi)` is called in a loop over `g_MpWeapons` enum values. If a weapon is registered in `g_MpWeapons` but absent from the catalog (or vice versa), the loop may either match the wrong weapon or never match, leaving `resolvedWeaponNum = 0`. The code then falls through to the `g_MpSetup.weapons[0]` fallback, which itself calls `catalogGetMpWeaponNum(spawnWeaponIdx)`. If that also resolves to 0, the player spawns with no spawn weapon, silently, with only a log warning. This MAY break spawn-with-weapon for any weapon added to the MP weapon list that is not yet catalog-registered.

**H3 (medium): handmodeldef / cartmodeldef stale pointer on match restart without full playermgr reinit**
Upstream did not initialize `handmodeldef` or `cartmodeldef` at `playermgrAllocatePlayer` time. PD2 adds those NULL inits. However, if a match restart path calls `playermgrReset` and reassigns `players[i]` from an existing allocation (without calling `playermgrAllocatePlayer` again), those fields may carry the previous match's modeldef pointer. The `bgunEnterFlux` call at equip-time would null `handmodeldef` but not `cartmodeldef`. This MAY produce a stale `cartmodeldef` read in `bgunTickMasterLoad` around line 4317 (`if (casing->modeldef == player->gunctrl.cartmodeldef)`) comparing against a freed pointer from the previous match.

**H4 (medium): IDLE anim position shift from matrix cache drop (B-246 round-10)**
The per-hand matrix cache (`unk0dd8`) was filled at weapon-load T-pose and reused for all IDLE frames. Phase B drops this cache and always calls `modelSetMatricesWithAnim`. If the AllInOneMods weapon idle animations have a different bone-rest position than frame-0 of the base anim track, the first IDLE frame after a weapon equip MAY show the gun jumping to a new position when compared to the previous (cache-based) IDLE position. This is the intended behavioral fix (T-pose was wrong). The hypothesis is not that IDLE is wrong after the fix, but that the transition from equip-frame to first-IDLE-frame MAY show a position pop visible to the player if the model's anim-0 frame-0 was being used by other systems to anchor the gun at equip time.

**H5 (medium): orchestrated spawn pool vs `scenarioChooseSpawnLocation` divergence**
PD2 adds `spawnPoolFindClearPosition` with up to 3 retries around the `scenarioChooseSpawnLocation` call in `playerStartNewLife` (PD2:player.c:1157). The spawn pool is separate infrastructure not present upstream. If `spawnPoolIsReady()` returns false (e.g., pool not built yet on first match tick), `spawnPoolFindClearPosition` is a no-op and the base `scenarioChooseSpawnLocation` result is used unchanged. The risk is that `spawnPoolFindClearPosition` and `scenarioChooseSpawnLocation` use different geometries (spawn pool uses pad positions, the scenario chooser uses a different weighting), and in some edge cases the cleared-position check MAY reject a valid pad, causing all 3 attempts to fail and falling through to the last chosen position anyway, which may be inside geometry. The fallback behavior (use last chosen pos) matches upstream behavior, so this is a no-regression risk rather than a regression.

**H6 (medium): netPlayersAllocate called before players have chr props**
In `playermgrAllocatePlayers` (PD2:playermgr.c:75), `netPlayersAllocate()` is called immediately after the player struct allocation loop but before any `playerSpawn` or `bodyAllocateChr`. If `netPlayersAllocate` reads `player->prop->chr` or other fields that are not yet populated, it MAY crash or corrupt state. The guard `g_StageNum != STAGE_TITLE && g_StageNum != STAGE_CITRAINING` reduces the exposure but does not eliminate it for all stage contexts.

**H7 (low): version-gate combatknife removal (JPN-only path)**
`bgunTickSwitch2` upstream had `#if (VERSION == VERSION_JPN_FINAL) && defined(PLATFORM_N64)` that forced `WEAPON_COMBATKNIFE` to `WEAPON_UNARMED`. PD2 targets USA ROM and `PLATFORM_N64` is removed, so this block was dead even before removal. The risk that removing it affects behavior is essentially zero.

**H8 (low): rooms[8] stack init in playerStartNewLife**
Upstream left `rooms[8]` uninitialized. PD2 initializes to `{-1, -1, -1, -1, -1, -1, -1, -1}`. `cdFindGroundInfoAtCyl` and related functions use rooms as input/output; on first call with uninitialized data the function may return incorrect ground Y. PD2's fix is correct. The hypothesis is that the fix changes ground-Y resolution for the first spawn in edge cases where room data matters, which could shift spawn position slightly vs upstream. This is intentionally correct behavior, not a bug.

**H9 (low): MPOPTION_SPAWNWITHWEAPON modelmgrLoadProjectileModeldefs ordering**
PD2 adds `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` before `invGiveSingleWeapon` and `bgunEquipWeapon2` in the spawn-with-weapon path (PD2:player.c:1822). This call loads the weapon's projectile model into the model manager. If the model manager slot allocation fails at spawn time (pool full), the function MAY return without loading, leaving the projectile model undefined. Upstream never loaded the model here at all (it relied on `setupPlaceWeapon` in `setupCreateProps` to load it as a pickup). On maps with no weapon pickups (Chicago CS, for example) neither the old pickup path nor the new spawn path would have loaded the model in upstream, so PD2 adds a net improvement. The risk is a pool-full edge case only.

---

## Cross-Cuts: Related Subsystems

**netPlayersAllocate**: new call in `playermgrAllocatePlayers` (PD2:playermgr.c:75). Touches the net player registry. No upstream equivalent. Any regression in net player state at stage load would propagate to `player->client` and `player->isremote` being stale or wrong.

**spawnPool system**: `spawnPoolFindClearPosition`, `spawnPoolIsReady`, `spawnPoolGet`, `spawnPoolSelectTiered`, `spawnPoolLastResort` are all new PD2 subsystems with no upstream equivalents. They are coupled to `playerStartNewLife` and `playerApplyOrchestratedSpawnFromPool`. The spawn pool must be built (by a call to something like `spawnPoolBuild`) before it is queried; if it is not ready, every call is a no-op.

**assetcatalog.h / catalogGetMpWeaponNum etc.**: The catalog API (`catalogGetMpWeaponNum`, `catalogGetBodyHandFilenum`, `catalogGetBodyModeldef`, `catalogGetHeadModeldef`, `catalogGetBodyIsComplete`, `catalogGetBodyHeight`, `catalogGetHeadHeight`, `catalogGetPropFilenumByIndex`) replaces direct array lookups throughout `player.c` and `bondgun.c`. The catalog must be initialized before any of these calls fire. If initialization is deferred past `playermgrAllocatePlayers` or `bgunTickMasterLoad` on the first tick, calls will return invalid data.

**forgeIsCanvasMode**: new PD2 guard in `setupCreateProps` that suppresses chr, key, hat, and autogun creation in canvas/editor mode. This subsystem has no upstream equivalent and does not affect normal gameplay or MP sessions.

**Bond matrix cache (unk0dd8)**: The per-hand matrix cache is dead state after Phase B. The `bgunMatrixCacheIsStale` predicate in `port/src/bondgun_cache.c` remains as a test fixture. Any future reintroduction of caching must satisfy those tests.

**visionmode**: now always initialized at player alloc (PD2:playermgr.c:413). The `VISIONMODE_NORMAL = 0` value is safe for all rendering paths. The original latent uninit caused night-vision to activate spuriously on some N64 ROM version configurations; that class of bug is eliminated.
