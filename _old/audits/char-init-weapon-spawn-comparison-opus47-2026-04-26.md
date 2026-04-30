# Character init + weapon spawn: PD2 vs fgsfdsfgs/perfect_dark upstream (Opus 4.7 pass)

**Audit date:** 2026-04-26
**Author session:** brave-panini-480d87 (research only, no code changes to PD2)
**Sibling doc:** [player-init-comparison-upstream-2026-04-26.md](player-init-comparison-upstream-2026-04-26.md) (prior session, narrower lens; cross-referenced where they overlap)

This audit compares PD2's character initialization and weapon spawn code against the upstream decomp PD2 forked from. It is framed against three architectural axes the prior session did not explicitly bucket:

1. **Catalog system** (`asset_entry_t` registry, runtime indices, mod-extensible asset registration; selectors filtered by unlock state)
2. **N64 decoupling** (memory layout assumptions, alignment, `IS4MB`, `PLATFORM_N64`, `fourmeg2player`)
3. **ROM version gate decoupling** (USA-only target; `#if VERSION >= VERSION_*` gates dropped, with at least two confirmed latent uninit-field incidents already fixed)

PD2 is pinned to `a30f3719` for this audit (the dev HEAD immediately before the Phase B `bgun0f0a5550` cache drop landed at `cafbd2ef`). At `a30f3719` the per-hand matrix cache is still active in PD2 and structurally byte-equivalent to upstream's, so any bug-shape signals in the rendering pipeline are still visible. The Phase A predicate `bgunMatrixCacheIsStale` lives in `port/src/bondgun_cache.c` but is not wired into `bgun0f0a5550`.

No fix proposals. Findings are framed as possibilities for Mike to triage.

---

## A. Upstream reference

- **Repo:** [github.com/fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)
- **HEAD commit:** `bed3bf52d0d5095d112940b1327ed6c256e54ea8`
- **HEAD date:** 2026-04-25
- **HEAD subject:** "Merge pull request #715 from Alex-LeTux/patch-1"
- **Clone:** shallow (`--depth 100`) at `/tmp/upstream_pd_decomp_opus47/`

The upstream is the C decomp of N64 Perfect Dark with PC port additions. PD2 forked from a much earlier point and has overlaid the AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon), netplay (ENet), the asset catalog migration, and the dedicated server. Upstream still tracks N64 ROM-version gates, the 4MB pool model, and the original chr/weapon authoring conventions.

## B. PD2 reference

- **Repo path:** `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike` (worktree `brave-panini-480d87` for this audit)
- **HEAD commit (audit pin):** `a30f3719` (Phase A merge, B-246 round-10)
- **HEAD date:** 2026-04-26 23:46 EDT
- **HEAD subject:** "merge: B-246 round-10 Phase A bondgun-cache predicate + tests"
- **Branch:** `dev` (worktree branch `claude/brave-panini-480d87`)

Total scope reviewed (after CRLF normalize):

| File | Upstream | PD2 @ a30f3719 | Delta |
|------|----------|----------------|-------|
| `src/game/playermgr.c` | 854 | 879 | +25 |
| `src/game/playerreset.c` | 482 | 829 | +347 |
| `src/game/player.c` | 5880 | 7027 | +1147 |
| `src/game/bondgun.c` | 13310 | 14068 | +758 |
| `src/game/bondgunreset.c` | 269 | 283 | +14 |
| `src/game/setup.c` | 2312 | 2856 | +544 |
| `src/game/bot.c` | 3666 | 4329 | +663 |
| `src/game/body.c` | 791 | 917 | +126 |
| `src/game/bodyreset.c` | (small) | (small) | minimal |
| `src/game/chr.c` | (large) | (large) | breadcrumb + N64 strip + chrInit guard |

Diff is tractable for hunk-by-hunk reasoning across this surface. No file is so divergent that side-by-side comparison breaks down.

---

## C. End-to-end path side-by-side

The mission-start to weapon-rendered path runs in this order in both codebases:

| Stage | Function | Upstream site | PD2 site | One-line role |
|-------|----------|---------------|----------|--------------|
| 1. Stage assets load | `setupLoadStage` / `setupLoadFiles` | [setup.c:1216-1457](src/game/setup.c) | [setup.c:1283-1564](src/game/setup.c) | Resolves stage file ids, allocates pools, loads setup + pads + bg |
| 2. Per-prop spawn (incl. weapons) | `setupCreateObject` / `setupPlaceWeapon` | [setup.c:417-803](src/game/setup.c) | [setup.c:417-803](src/game/setup.c) | Walks `g_StageSetup.props`, creates objs, calls `modelmgrLoadProjectileModeldefs` per pickup |
| 2b. MP weapon model batch preload (PD2-only) | (none) | [setup.c:2825-2851](src/game/setup.c) | Loads FP modeldef for every weapon in `g_MpSetup.weapons[]` + `spawnWeaponNum` after pickups so pickup-less arenas still work |
| 2c. MP B-181 fallback (PD2-only) | (none) | [setup.c:2745-2746](src/game/setup.c) | OR-sets `MPOPTION_SPAWNWITHWEAPON` and resolves a spawn weapon when world pickups are below threshold |
| 3. Per-player struct alloc | `playermgrAllocatePlayer` | [playermgr.c:402](src/game/playermgr.c) | [playermgr.c:413](src/game/playermgr.c) | Allocates `struct player` from stage pool; defaults `gunctrl`, vision, network state, `wantsjump`, `client`, `ucmd` |
| 4. Per-player gun reset | `bgunReset` | [bondgunreset.c:144](src/game/bondgunreset.c) | [bondgunreset.c:141](src/game/bondgunreset.c) | Allocates `gunmem` slab, zeros hand structs to defaults, sets `gunmemowner = CHRBODY` |
| 5. Catalog modeldef refresh | `bodiesReset` | [bodyreset.c:14](src/game/bodyreset.c) | [bodyreset.c:23](src/game/bodyreset.c) | Clears `modeldef` pointers per stage; PD2 routes through `catalogResetAllModeldefs()` |
| 6. Per-life init + INTROCMD walk | `playerReset` | [playerreset.c:110](src/game/playerreset.c) | [playerreset.c:116](src/game/playerreset.c) | Walks `g_StageSetup.intro` chain; INTROCMD_WEAPON adds intro inventory + populates `g_DefaultWeapons[]`; INTROCMD_SPAWN populates `g_SpawnPoints[]` |
| 6b. Spawn pool build (PD2-only) | (none) | [playerreset.c:680-770](src/game/playerreset.c) | L2 `spawnPoolBuildGlobal()` for MP / netmode |
| 7. chr-prop alloc + body bind | `chrInit` (called from `playerReset`) | [chr.c:1095](src/game/chr.c) | [chr.c:1137](src/game/chr.c) | Allocates `chr` from `g_ChrSlots[]`, binds to `prop`, body/head left at 0 (`playerChooseBodyAndHead` sets later) |
| 8. body+head modeldef bind | `bodyAllocateChr` (via `chrSetup`) | [body.c:393](src/game/body.c) | [body.c:506](src/game/body.c) | Calls `body0f02ce8c` for body, picks head, attaches to `chr->model` |
| 9. body0f02ce8c | (named-id model bind) | [body.c:170](src/game/body.c) | [body.c:178](src/game/body.c) | First-load probe + attach for body modeldef + head modeldef; PD2 routes through catalog accessors |
| 10. Per-life respawn | `playerSpawn` | [player.c:939](src/game/player.c) | [player.c:1613](src/game/player.c) | Inv reset; spawn-with-weapon application; `bgunEquipWeapon2` queues hand switch |
| 10b. Anti-player respawn | `playerSpawnAnti` | [player.c:822](src/game/player.c) | [player.c:1496](src/game/player.c) | Counter-Op anti suicide pill + unarmed equip |
| 11. Bot spawn | `botSpawn` | [bot.c:227](src/game/bot.c) | [bot.c:313](src/game/bot.c) | Bot equivalent of step 10; PD2 swaps direct `g_MpWeapons[]` for catalog API |
| 12. Master loader | `bgunTickMasterLoad` | [bondgun.c:4216](src/game/bondgun.c) | [bondgun.c:4242](src/game/bondgun.c) | Async hand+gun+carts state machine; allocates the 50-Mtxf cache slab on transition CARTS to LOADED |
| 13. Hand switch tick | `bgunTickSwitch2` | [bondgun.c](src/game/bondgun.c) | [bondgun.c](src/game/bondgun.c) | Drains `switchtoweaponnum` queued by step 10 |
| 14. Per-frame matrix update | `bgun0f0a5550` | [bondgun.c:7553](src/game/bondgun.c) | [bondgun.c:7971](src/game/bondgun.c) | Cache-fill on first IDLE; cache-broadcast every IDLE frame; `modelSetMatricesWithAnim` on FIRE / RELOAD |

PD2 holds the same overall ordering. The structural delta is on the periphery (catalog accessors at every asset lookup, defensive guards on every torn-modeldef path, an MP spawn-pool layer in front of `playerReset`, a manifest-driven preload after `setupCreateObject`). The core control flow is intact.

---

## D. Per-file delta catalog (axis-bucketed)

### D.1 `playermgr.c` (878 vs 854)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | playermgr.c:11 | playermgr.c:10 | added | `#include "net/net.h"` | OTHER |
| 2 | playermgr.c:30-34, 56-60 | playermgr.c:26-30 | rewritten | Per-MAX_PLAYERS init loop replaces fixed-4 array writes (`for (i=0; i<MAX_PLAYERS; ...)`) | OTHER (MAX_PLAYERS scaling) |
| 3 | playermgr.c:75-78 | (none) | added | `netPlayersAllocate()` invocation when `g_NetMode != NETMODE_NONE` and not on title / CI training | OTHER (net) |
| 4 | playermgr.c:90-94 | playermgr.c:74-77 | rewritten | View-size: collapsed `g_Vars.fourmeg2player` branch to a single `playermgrSetViewSize(...)` call | **N64-DECOUPLE** |
| 5 | playermgr.c:413 | playermgr.c:402 | rewritten | `visionmode = VISIONMODE_NORMAL` was inside `#if VERSION >= VERSION_JPN_FINAL ... #endif`; PD2 dropped the gate, init now unconditional | **ROM-GATE** |
| 6 | playermgr.c:418-426 | (none) | added | New unconditional `gunctrl.handmodeldef = NULL; cartmodeldef = NULL`. Comment cites B-246 round-7 stale heap garbage `0x7c7b663545bbcbd1` because `mempAlloc` does not zero-fill | **ROM-GATE class** (latent uninit surfaced by ROM-gate strip pattern) |
| 7 | playermgr.c:610 | playermgr.c:591 | changed | Target-set loop bound: `MAX_PLAYERS` literal replaced by `ARRAYCOUNT(g_Vars.players[index]->targetset)` | OTHER |
| 8 | playermgr.c:662-668 | (none) | added | `wantsjump=false; jumpconsumed=true; client=NULL; ucmd=(g_NetMode==NETMODE_SERVER)?UCMD_FL_FORCEMASK:0; isremote=false` | OTHER (net + jump-input) |
| 9 | playermgr.c:774-782 | playermgr.c:747-754 | removed | Eight `case WEAPON_PP9I/CC13/KL01313/KF7SPECIAL/ZZT/DMC/AR53/RCP45` arms removed from `playermgrGetChrModelForWeapon` | OTHER (GE legacy strip) |
| 10 | playermgr.c:825-830 | (none) | added | Player-shuffle early return when `g_NetMode != NETMODE_NONE` | OTHER (net) |

### D.2 `bondgunreset.c` (283 vs 269)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | bondgunreset.c:10 | bondgunreset.c:10-12 | rewritten | `#ifndef PLATFORM_N64 ... #endif` around `game/player.h` include dropped | **N64-DECOUPLE** |
| 2 | bondgunreset.c:15 | bondgunreset.c:14 | removed | `extern u32 g_BgunGunMemBaseSize4Mb2P;` extern dropped | **N64-DECOUPLE** |
| 3 | bondgunreset.c:144 | bondgunreset.c:144-148 | rewritten | `IS4MB() && PLAYERCOUNT() == 2` 4MB gunmem branch collapsed; PD2 always uses `ALIGN16(bgunCalculateGunMemCapacity())`. ALIGN16 macro left intact (per memory `feedback_macro_collapse`, this is the right call: ALIGN16 is multi-use so any collapse to no-op would be unsafe) | **N64-DECOUPLE** |
| 4 | bondgunreset.c:236-256 | (none) | added | LOG.WPN.DIAG bgunReset state dump for player 0 | OTHER (instrumentation) |

### D.3 `playerreset.c` (829 vs 482)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | playerreset.c:118 | playerreset.c:113 | changed | `RoomNum rooms[8]` initialized to `{-1, ..., -1}` instead of indeterminate stack values | OTHER (defensive) |
| 2 | (removed) | playerreset.c:130 | removed | `playerResetLoResIf4Mb()` call dropped | **N64-DECOUPLE** |
| 3 | playerreset.c:153-159 | (none) | added | `#if MAX_PLAYERS > 4` extension of `g_PlayersWithControl[]` to slots 4..7 | OTHER (player count scaling) |
| 4 | playerreset.c:175-194 | (none) | added | GAMELOOP.* per-mode logging at entry to `playerReset` | OTHER (instrumentation) |
| 5 | playerreset.c:198-203 | (none) | added | `introSafety > 10000` runaway watchdog on `cmd != INTROCMD_END` walk | OTHER (defensive) |
| 6 | playerreset.c:206 | playerreset.c:172 | changed | `INTROCMD_SPAWN` now bounds-checks `g_NumSpawnPoints < MAX_MPCHRS` before append | OTHER (defensive) |
| 7 | **playerreset.c:217-232** | (none) | **added** | **`INTROCMD_WEAPON` now skipped in MP when `g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)`** to prevent loop 1 (`playerReset` to `INTROCMD_WEAPON`) from racing loop 2 (`playerSpawn` to spawn-with-weapon) | OTHER (B-219 race fix; sets up the asymmetric guard analyzed in F-2 below) |
| 8 | playerreset.c:233 | playerreset.c:184 | changed | `g_Vars.currentplayer != g_Vars.anti` replaced with `PLAYER_IS_NOT_ANTI(g_Vars.currentplayer)` macro | OTHER (semantic-preserving) |
| 9 | playerreset.c:266-276 | (none) | added | INTROCMD_WEAPON anti-skip diag log | OTHER (instrumentation) |
| 10 | playerreset.c:312-315 | playerreset.c:241-243 | changed | INTROCMD_CREDITOFFSET while-loop gains a 1000-iteration safety counter | OTHER (defensive) |
| 11 | playerreset.c:332-413 | (none) | added | New large block: spawn-point fallback (waypoints + sequential pad scan) when `g_NumSpawnPoints == 0 && (g_NetMode != NETMODE_NONE \|\| g_Vars.normmplayerisrunning)`. Implements adaptive spacing (500/250/125/60/0). | OTHER (spawn pool L1) |
| 12 | playerreset.c:430-444 | (none) | added | Co-op telefrag warning when `<2` spawn pads | OTHER (diag) |
| 13 | playerreset.c:447-485 | (none) | added | L2 `spawnPoolBuildGlobal()` invocation after spawn-point fallback; uses `g_NetMatchSeed` from `SVC_STAGE_START` for netplay | OTHER (spawn pool L2) |
| 14 | playerreset.c:609-647 | playerreset.c:355-385 | removed | Eight cheat blocks for PP9I/CC13/KL01313/KF7SPECIAL/ZZT/DMC/AR53/RCP45 stripped wholesale (no migration to catalog API) | OTHER (GE legacy strip; the cheat hooks themselves are gone) |
| 15 | playerreset.c:649-727 | playerreset.c:404-411 | added | New `else if (mplayerisrunning && spawnPoolIsReady() && lvframe60 == 0)` branch defers initial MP placement to `mpOrchestrateMatchStartSpawns`; new `else if (mplayerisrunning)` fallback scans pads + 8-direction wall probe to face away from walls | OTHER (spawn pool integration) |
| 16 | playerreset.c:740-758 | (none) | added | B-242 capsule-clip check + radial sweep before `cdFindGroundInfoAtCyl` | OTHER (defensive) |

### D.4 `player.c` (7027 vs 5880)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | player.c:1320-1380 | player.c:660-680 | added | New post-`playerSpawn` instrumentation: dumps `currentplayer->visionmode`, `model00d4`, equipped weapon, etc. for player 0 | OTHER (B-246 instrumentation) |
| 2 | player.c:1496 | player.c:822 | shifted | `playerSpawnAnti` body identical apart from `g_Vars.currentplayer != g_Vars.anti` macroized | OTHER |
| 3 | player.c:1613 | player.c:939 | shifted | `playerSpawn` body up through anti-spawn `for` loop nearly identical (chr-distance sort, on-screen rejection, force-spawn fallback) | OTHER |
| 4 | (removed) | player.c:1102 | removed | `#ifndef PLATFORM_N64` wrapping the entire spawn-with-weapon block dropped | **N64-DECOUPLE** |
| 5 | **player.c:1790** | **player.c:1117** | **changed** | **Inner `g_Vars.normmplayerisrunning` guard removed.** Upstream: `if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON))`. PD2: `if (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)`. The branch now reaches in Co-Op and Counter-Op Bond-side too if the bit happens to be set | **CATALOG-side change** (the rewrite was driven by catalog migration of `g_MpWeapons[]` access; the guard drop is collateral) |
| 6 | player.c:1791-1808 | player.c:1118-1124 | rewritten | Spawn weapon resolution: PD2 reads `g_MatchConfig.spawnWeaponNum` first (preferred), then falls back to `g_MpSetup.weapons[0]`. Upstream reads only `g_MpSetup.weapons[0]` | **CATALOG** (PD2's `spawnWeaponNum` is the M0.1c primary identity field) |
| 7 | player.c:1810-1814 | player.c:1123 | rewritten | `g_MpWeapons[idx].weaponnum` direct read replaced by `catalogGetMpWeaponNum(idx)` | **CATALOG** |
| 8 | player.c:1815-1817 | player.c:1124 | rewritten | `mpweapon->priammotype` direct read replaced by `catalogGetMpWeaponPriAmmoType(idx)` | **CATALOG** |
| 9 | player.c:1820 | player.c:1126 | rewritten | `mpweapon->priammoqty` direct read replaced by `catalogGetMpWeaponPriAmmoQty(idx)` | **CATALOG** |
| 10 | player.c:1822 | (none) | added | Defensive `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` immediately before `invGiveSingleWeapon` (B-219 v2). | OTHER (B-219 fix) |
| 11 | player.c:1840-1845 | (none) | added | Else branch (`resolvedWeaponNum <= 0`) inside the SPAWNWITHWEAPON gate: equips `g_DefaultWeapons[HAND_LEFT/RIGHT]`. Upstream had no equivalent inner else; the outer else handled non-MP and "no spawn-with-weapon" cases together | OTHER (defensive fallback) |
| 12 | player.c:1849 | player.c:1142 | macroized | `g_Vars.currentplayer == g_Vars.anti` to `PLAYER_IS_ANTI` macro (semantic-preserving) | OTHER |

### D.5 `bondgun.c` (14068 vs 13310)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | bondgun.c:4242 | bondgun.c:4216 | identical | `bgunTickMasterLoad` cache fill: `unk0dd4 = -1; unk0dd8 = (Mtxf*)memloadptr; memloadptr += 50*sizeof(Mtxf)` is byte-equivalent to upstream | (no change) |
| 2 | bondgun.c:4282-4407 | (none) | added | LOG.WPN.DIAG transition logs at FLUX to HANDS, HANDS to GUN, GUN to CARTS, CARTS to LOADED, SHORTCUT to LOADED for player 0 | OTHER (instrumentation) |
| 3 | bondgun.c:7971 | bondgun.c:7553 | identical | `bgun0f0a5550` outer signature + per-frame guards identical to upstream | (no change) |
| 4 | bondgun.c:8017-8030 | (none) | added | LOG.WPN.DIAG enter dump for player 0 every frame | OTHER (instrumentation) |
| 5 | bondgun.c:8420-8442 | (none) | added | LOG.WPN.DIAG `a0_decision` log on edge-trigger | OTHER (instrumentation) |
| 6 | **bondgun.c:8446-8497** | **bondgun.c:7900-7938** | **identical** | **Cache USE branch byte-equivalent to upstream**: `if (player->hands[HAND_RIGHT].unk0dd4 == -1)` cold-fill, then re-broadcast `unk0dd8` over live `gunmodel.matrices` via `mtx00015be4` loop. PD2 added `cache_fill` diag log for player 0; logic is unchanged | (no change) |
| 7 | bondgun.c:8480-8488 | (none) | added | `cache_fill` LOG.WPN.DIAG dump (animnum, animframe, lvframenum) | OTHER (instrumentation) |

The matrix cache mechanism is alive at `a30f3719` and structurally identical to upstream. The reason PD2 sees a stale-T-pose IDLE while upstream does not is **animation authoring**, not C code. Per the prior session's Section G analysis: upstream IDLE is at anim 0 frame 0 (model rest), so cold-fill captures the rest pose; PD2 / AllInOne IDLE is at anim 236 frame 17 (a non-rest pose), so cold-fill captures whatever pose the gun was in when first rendered, which on a fresh load is T-pose. The fix landed at `cafbd2ef` is to drop the cache USE branch entirely and always run `modelSetMatricesWithAnim`.

The Phase A predicate `bgunMatrixCacheIsStale` was added in `port/src/bondgun_cache.c` at `a30f3719` but is not yet wired in. Phase B (post-`a30f3719`) replaces the cache-USE branch with an unconditional `modelSetMatricesWithAnim`.

### D.6 `bot.c` (4329 vs 3666)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | bot.c:329-336 | (none) | added | CHR.DIAG bot-spawn entry log | OTHER (instrumentation) |
| 2 | (removed) | bot.c:264 | removed | `#ifndef PLATFORM_N64` wrapping bot spawn-with-weapon block dropped | **N64-DECOUPLE** |
| 3 | **bot.c:506** | **bot.c:266** | **changed** | **Inner `g_Vars.normmplayerisrunning` guard removed**. Upstream: `if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON) && weapons[0] != NONE/DISABLED/SHIELD)`. PD2: `if (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)` only | **CATALOG-side change** (parallels D.4 #5) |
| 4 | bot.c:507-528 | bot.c:267-281 | rewritten | Spawn weapon resolution mirrors player.c (spawnWeaponNum first, weapons[0] fallback); catalog API replaces `g_MpWeapons[]` direct access | **CATALOG** |
| 5 | (none) | (none) | unchanged | bot.c does NOT call `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` per-bot (relies on the setup.c batch preload at item D.7 #2) | OTHER (asymmetry vs player.c) |
| 6 | bot.c:559-577 | (none) | added | CHR.DIAG post-spawn invisibility check (model NULL, rooms == -1, CHRHFLAG_HIDDEN) | OTHER (instrumentation) |

### D.7 `setup.c` (2856 vs 2312)

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | setup.c:193-202 | setup.c:179-184 | rewritten | Pool sizes: `g_MaxWeaponSlots 50->100; g_MaxHatSlots 10->20; g_MaxAmmoCrates 20->40; g_MaxDebrisSlots 15->30; g_MaxProjectiles IS4MB?20:100 -> 200; g_MaxEmbedments IS4MB?40:80 -> 160`. IS4MB ternaries gone | **N64-DECOUPLE** |
| 2 | setup.c:417-446 | (none) | added | `setupCreateObject` NULL `g_ModelStates[modelnum].modeldef` skip-and-log; force-OR `OBJFLAG3_WALKTHROUGH` on weapon / ammocrate / multiammocrate (B-146 regression fix) | OTHER (defensive) |
| 3 | setup.c:677-803 | setup.c:610-732 | unchanged | `setupPlaceWeapon` pre-pickup `modelmgrLoadProjectileModeldefs` call retained | (no change) |
| 4 | setup.c:1283-1297 | setup.c:1216-1227 | rewritten | `setupLoad` stage-table read replaced by `catalog_stage_result_t stage; catalogGetStageResultByIndex(stageindex, &stage);`. The dropped `#ifdef PLATFORM_N64 // bug?` LOADTYPE_LANG branch is also gone. `fileLoadToAddr` to `assetLoadRomToAddr` | **CATALOG + N64-DECOUPLE + ROM-GATE** |
| 5 | setup.c:1383-1450 | setup.c:1310-1330 | rewritten | `setupLoadFiles` catalog-ized for SP+MP setup-fileid + pads-fileid; `assetLoadToNew(stage.{mp,}setup_handle)` not `fileLoadToNew(g_Stages[..].setupfileid)`. Adds intro-validity heuristic | **CATALOG** |
| 6 | setup.c:1579-1583 | setup.c:1449-1457 | rewritten | `extra = IS4MB() ? 40 : 60` collapsed to `extra = 60`; stray `if (IS4MB());` line gone | **N64-DECOUPLE** |
| 7 | setup.c:1741-1837 | setup.c:1584-1668 | changed | OBJTYPE_CHR / KEY / HAT / AUTOGUN / LIFT / ESCASTEP gain a `!forgeIsCanvasMode()` gate; LIFT auto-registers against `g_Lifts[]` based on `liftnum` from pads. `setupMarkLiftDoors()` helper deleted | OTHER (forge + lift fix) |
| 8 | **setup.c:2745-2746** | **(none)** | **added** | **B-181 fallback OR-sets `MPOPTION_SPAWNWITHWEAPON` on both `g_MatchConfig.options` and `g_MpSetup.options` when `s_SetupMpCreatedWeaponCount < desiredPickups`**. User-pick preservation gates the override on `spawnWeaponNum == 0xFF \|\| 0` | OTHER (PD2-only invariant) |
| 9 | **setup.c:2825-2851** | **(none)** | **added** | **B-219 v3 / B-229 manifest-driven preload**: walks `g_MpSetup.weapons[]` and `spawnWeaponNum`, calling `modelmgrLoadProjectileModeldefs` for each so pickup-less arenas (Chicago CS) still have FP models loaded | OTHER (PD2-only invariant) |
| 10 | setup.c:2295-2305 | setup.c:2094-2102 | rewritten | `if (challengeIsFeatureUnlocked(MPFEATURE_8BOTS)) maxsimulants = MAX_BOTS; else 4` collapsed to unconditional `maxsimulants = MAX_BOTS` | OTHER (gating drop) |
| 11 | setup.c:2311-2335 | setup.c:2111-2126 | changed | Simulant slot test `g_MpSetup.chrslots & (1 << (slotnum + 4))` replaced by `mpIsParticipantActive(slotnum + MAX_PLAYERS)` (B-12 Phase 3) | OTHER (participant pool) |

### D.8 `body.c`, `bodyreset.c`, `chr.c`

| # | PD2 site | Upstream site | Delta type | Description | Axis |
|---|----------|---------------|------------|-------------|------|
| 1 | body.c:163-171 | body.c:158-166 | rewritten | `bodyLoad` rewritten: `g_HeadsAndBodies[bodynum].modeldef = modeldefLoadToNew(...)` to `catalogGetBodyModeldef(bodynum)`. Return-value contract subtly changed (returns true on every cache hit, not just first load) | **CATALOG** |
| 2 | body.c:178-184 | body.c:170-171 | added | `body0f02ce8c` adds bounds clamp `bodynum < 0 \|\| bodynum >= ARRAYCOUNT(g_HeadsAndBodies)` falling back to slot 0 | OTHER (defensive) |
| 3 | body.c:185-186 | body.c:172-173 | rewritten | `g_HeadsAndBodies[].scale / .animscale` to `catalogGetBodyScaleByIndex / catalogGetBodyAnimScale` | **CATALOG** |
| 4 | body.c:198-228 | (none) | added | Sanity gate on `bodymodeldef`: rejects NULL skel / rootnode / numparts <= 0 or > 500; clamps `scale <= 0` to 1.0 | OTHER (defensive) |
| 5 | body.c:232-292 | body.c:194-244 | rewritten | `if (g_Vars.normmplayerisrunning && !IS4MB())` head-load branch deleted; replaced by `catalogGetHeadModeldef(headnum)` with a `head_needs_offset` first-load guard | **N64-DECOUPLE + CATALOG** |
| 6 | body.c:354-385 | (none) | added | `bodyAllocateModel` adds `manifestEnsureLoaded()` calls for body / head canon-ID + diag logs | OTHER (manifest) |
| 7 | body.c:455-468 | (none) | added | `bodyAllocateChr` early-return when `forgeIsCanvasMode()` true | OTHER (forge) |
| 8 | body.c:514-522 | (none) | added | `bodyAllocateChr` registers body + head with manifest before continuing | OTHER (manifest) |
| 9 | bodyreset.c:23-26 | bodyreset.c:14-16 | rewritten | Clear-modeldef loop replaced by single `catalogResetAllModeldefs()` call | **CATALOG** (modeldef lifetime moves into catalog) |
| 10 | bodyreset.c:33-39 | (none) | added | `bodiesReset` early-return in `g_Vars.normmplayerisrunning` skips guard randomisation; also skips `var80062c80 = rngRandom() % g_NumBondBodies` initialization | OTHER (defensive but skips an init) |
| 11 | bodyreset.c:51-65 | bodyreset.c:33-46 | rewritten | Stage-id override table replaced by `strcmp(g_MissionConfig.stage_id, "base:infiltration")` | **CATALOG** (stage-id strings) |
| 12 | chr.c:1137-1142 | chr.c:1095-1097 | added | `chrInit` adds NULL-guard on `chr` after slot scan (would have NULL-deref'd in upstream if pool exhausted) | OTHER (defensive) |
| 13 | chr.c:1147-1150 | (none) | added | `chrInit` stamps `chr->generation = ++s_ChrGenerationCounter` (FIX-A.2 stale-pointer detection) | OTHER (FIX-A) |
| 14 | chr.c:1386-1389 | (none) | added | `chr0f020b14` ground-value sanity: substitute `pos->y` if `cdFindGroundInfoAtCyl` returned `< -4e9` | OTHER (defensive) |
| 15 | chr.c:2730-2766 | chr.c:2730-2766 | removed | `#ifndef PLATFORM_N64 // always interpolate animation` block deleted; PD2 always interpolates | **N64-DECOUPLE** |
| 16 | chr.c:6325-6500 | chr.c:6256-6499 | removed | Multiple `#ifdef PLATFORM_N64` paths in chr-render path stripped (gDPSetColorImage / OS_K0_TO_PHYSICAL framebuffer-blit branches) | **N64-DECOUPLE** |

---

## E. Catalog impact analysis

The catalog changes the read pattern from "compile-time fixed array indexed by enum" to "runtime accessor that walks the asset registry." Every catalog accessor in the chr-init / weapon-spawn path is a potential silent-failure surface if the registry is empty, partial, or out-of-sync at the moment of call.

| Site | Read replaced | Possible failure mode |
|------|---------------|----------------------|
| body.c:163 `bodyLoad` | `g_HeadsAndBodies[bodynum].modeldef` to `catalogGetBodyModeldef(bodynum)` | Catalog returns NULL silently. Upstream's direct read also returns NULL on first call (lazy load). PD2's caller (`body0f02ce8c`) re-checks NULL right after, so the immediate site is safe |
| body.c:185 `body0f02ce8c` | `g_HeadsAndBodies[].scale / .animscale` to `catalogGetBodyScaleByIndex / catalogGetBodyAnimScale` | If the catalog runtime index has not been registered for `bodynum`, PD2 falls through with whatever default the accessor returns (likely 0 or 1.0). A scale of 0 would zero out chr geometry and produce an invisible chr |
| body.c:235 `bodyCalculateHeadOffset` | `g_HeadsAndBodies[headnum].modeldef == NULL` test | Mixed read. PD2 still reads the raw field as the "first load" probe but the writes go through the catalog. If `catalogGetHeadModeldef` ever changes to bypass `g_HeadsAndBodies[].modeldef` (caching the modeldef in the catalog record instead), the `head_needs_offset` test silently breaks (offset never recomputed) |
| body.c:243 | `g_HeadsAndBodies[bodynum].canvaryheight` to `catalogGetBodyCanVaryHeight` | Same NULL-or-default risk as scale |
| body.c:418 | `g_HeadsAndBodies[bodynum].ismale` to `catalogGetBodyIsMale` | Default `false` if catalog miss; voicebox-by-gender wrongly chooses female for unregistered body |
| bodyreset.c:23 | clear-modeldef loop to `catalogResetAllModeldefs()` | If `catalogResetAllModeldefs` does not actually clear `g_HeadsAndBodies[].modeldef` (the field that `bodyCalculateHeadOffset` reads), the `head_needs_offset` test sees stale state across stages |
| bodyreset.c:51 | stage-table override to `strcmp(g_MissionConfig.stage_id, "base:infiltration")` | If `g_MissionConfig.stage_id` is empty or has a typo or unregistered ID, the override silently fails (no override applied; default head sets used) |
| player.c:1810-1820 | `mpweapon->weaponnum / priammotype / priammoqty` to catalog accessors | A failed catalog lookup at spawn time returns `0`. PD2 codepath: `resolvedWeaponNum == 0` falls through to the inner else (`bgunEquipWeapon2(g_DefaultWeapons[...])`). Combined with the `playerreset.c:217` skip-INTROCMD_WEAPON gate, `g_DefaultWeapons[]` is also `0`. Net: spawn unarmed. Per `feedback_correct_implementation`, this is a real B-219 v2 narrative ("user picked `base:remotemine` but saw Falcon (Silenced)") |
| bot.c:506-528 | parallel of player.c:1790 | Same as above for bots; bots additionally lack the per-spawn `modelmgrLoadProjectileModeldefs` defensive load (relies on setup.c:2825 batch preload) |
| setup.c:1283 | `g_Stages[stageindex].setupfileid` to `catalogGetStageResultByIndex(...).setupfileid` | If the catalog record for `stageindex` is empty (mod stage not registered before stage load), `setupfilenum` is 0 and `assetLoadRomToAddr(0, ...)` either returns NULL or loads the wrong asset. Stage load fails or loads wrong setup |
| setup.c:1383-1450 | `fileLoadToNew(setupfilenum)` to `assetLoadToNew(stage.setup_handle)` | `setup_handle` is opaque; if the catalog backend does not invalidate it across stages, stale data could be returned. Mike's S346 constraint added explicit guidance that `romProviderHandle` is internal and game code should use `assetLoadRomToNew` / `fileLoadToNew`; the migration here is consistent with that |
| setup.c:2825 | new batch `modelmgrLoadProjectileModeldefs` over `g_MpSetup.weapons[]` | Reads the live participant pool (B-12 Phase 3) and `g_MatchConfig.spawnWeaponNum`; if either is stale at `setupLoadStage` end (e.g. mid-match scenario change without rebuild), preload misses. `setup.c:2825-2851` is end-of-stage so generally safe |

The catalog axis carries the highest implicit-failure-mode count. Most catalog accessors in this path silently return defaults rather than asserting on miss. PD2 has added defensive guards in some places (body.c:198-228 for torn modeldefs; body.c:178-184 for OOB bodynum), but the pattern is uneven.

---

## F. N64 decoupling impact analysis

The PD2 strips of `IS4MB()`, `PLATFORM_N64`, `fourmeg2player`, `g_Is4Mb`, `g_BgunGunMemBaseSize4Mb2P`, the chr-anim distance-frame-skipping branch, and the chr-render N64 framebuffer-blit branches are all clean removes (no replacement code needed because modern hardware does not have the constraint). Each is listed below with a possibility frame:

| Strip | PD2 site | What was removed | Possible failure mode |
|-------|----------|-----------------|----------------------|
| Pool size IS4MB ternaries | setup.c:193-202 | `IS4MB() ? 20 : 100` collapsed to `200` etc. | None; ternary always took non-4MB branch on PC. Pool sizes increased unconditionally (good for PC) |
| Gun mem 4MB+2P branch | bondgunreset.c:144 | `IS4MB() && PLAYERCOUNT() == 2` removed | None; PD2 has no 4MB mode |
| `fourmeg2player` viewport | playermgr.c:90-94 | Field removed; `playermgrSetViewSize` is the sole path | None; collapsed correctly |
| 4MB head-load branch | body.c:232-248 | `if (normmplayerisrunning && !IS4MB())` reload-and-zero branch deleted | **POSSIBILITY**: upstream forced `g_FileInfo[..].loadedsize = 0` to defeat caching in MP. PD2 relies on `catalogResetAllModeldefs()` invalidating per stage. If catalog cache lifetime extends past stage transition (see E above), an MP head from a prior arena could be returned. Verify `catalogResetAllModeldefs()` clears the cache layer used by `catalogGetHeadModeldef` |
| `playerResetLoResIf4Mb()` | playerreset.c (removed) | Function call dropped | None on PC |
| Chr always-interpolate | chr.c:2730-2766 | N64 distance-frame-skip removed | None; PC always interpolates |
| Chr render N64 blits | chr.c:6325-6500 | Several `#ifdef PLATFORM_N64` framebuffer-blit branches | None; out of init scope |
| Spawn-with-weapon `#ifndef PLATFORM_N64` | player.c:1102, bot.c:264 | The wrapping `#ifndef PLATFORM_N64` around the spawn-with-weapon block was dropped | None; PLATFORM_N64 was 0 on PC, so the block always compiled in |

The N64 decoupling axis is the cleanest of the three. Findings are mostly "no risk" with one possibility (head modeldef cache lifetime) that depends on catalog-internal behavior.

---

## G. ROM version gate decoupling impact analysis

This is the axis with confirmed prior incidents. The two known gate-drops that surfaced uninitialized field bugs are `visionmode` (B-246-precursor finding cited in [weapon-system-deep-audit-2026-04-25.md](weapon-system-deep-audit-2026-04-25.md)) and `gunctrl.handmodeldef / cartmodeldef` (B-246 r7).

The pattern: a field's only assignment in upstream lived inside a `#if VERSION >= VERSION_*` block. PD2 dropped the gate, but no equivalent unconditional init was added. The field remained whatever `mempAlloc` returned, which on PD2's heap is not zero-fill (precedent confirmed by `visionmode=51503` field dump).

| Field | PD2 fix | Upstream gating |
|-------|--------|----------------|
| `currentplayer->visionmode` | playermgr.c:413 unconditional `= VISIONMODE_NORMAL` | `#if VERSION >= VERSION_JPN_FINAL` in upstream |
| `gunctrl.handmodeldef` | playermgr.c:418 unconditional `= NULL` | not present in upstream (defensive catch in PD2 only) |
| `gunctrl.cartmodeldef` | playermgr.c:419 unconditional `= NULL` | not present in upstream (defensive catch in PD2 only) |

**The class is not exhausted by these two fixes.** Any `struct player`, `struct gunctrl`, or `struct hand` field that was only initialized inside a version-gated block in upstream, and where PD2 dropped the gate without adding an unconditional init, is a candidate. A propagation pass over upstream's `#if VERSION >=` blocks intersected with PD2's unconditional code would surface every candidate.

| Possibility | Evidence | What MAY break |
|-------------|---------|----------------|
| Other `struct player` fields in same uninit class | visionmode + handmodeldef precedent | A field reads stale heap garbage; observable as random misbehavior at first frame after spawn (e.g., scope state, autoaim flag, vision filter mask). Hard to correlate with a single feature without instrumentation |
| Other `struct gunctrl` fields in same class | handmodeldef + cartmodeldef precedent | Same as above but in the gun pipeline; observable as wrong FP model, wrong cart particle, or NULL-deref in the master loader |
| Other `struct hand` fields | similar shape | Wrong hand-model attached, wrong recoil parameters, broken reload cycle |

---

## H. Hypotheses for what could be broken

Each hypothesis is framed as a possibility (MAY) with the architectural axis it springs from and a plausibility tier (LIVE / SPECULATIVE / RULED-OUT). No fixes proposed.

### H-1. Catalog miss at spawn time leaves player unarmed (CATALOG, LIVE)

**Possibility:** Spawn-with-weapon resolution (`player.c:1810-1820`, `bot.c:507-528`) MAY return `resolvedWeaponNum == 0` if the catalog has not finished registering the MP weapon entries by the time `playerSpawn` runs. The PD2 fallback inside the SPAWNWITHWEAPON gate equips `g_DefaultWeapons[HAND_LEFT/RIGHT]`, but the `playerreset.c:217` gate skips INTROCMD_WEAPON in the same condition, so `g_DefaultWeapons[]` is also `0`. Net: spawn unarmed. The B-219 v2 narrative ("user picked `base:remotemine` but saw Falcon (Silenced)") indicates this has fired in playtests. Plausibility LIVE.

### H-2. F-1 / F-5 / F-8 cluster (CATALOG-side change, LIVE)

**Possibility:** PD2 dropped the inner `g_Vars.normmplayerisrunning` guard on the spawn-with-weapon branch (`player.c:1790`, `bot.c:506`). Upstream restricts the branch to normal MP. PD2 only checks the `MPOPTION_SPAWNWITHWEAPON` bit. If the bit is set when entering Co-Op or Counter-Op (e.g. B-181 fallback at `setup.c:2745` ORs the bit on without checking mode), the spawn-with-weapon branch fires in those modes. Combined with the playerreset.c:217 gate keying on `normmplayerisrunning` (asymmetric), the player MAY end up with both INTROCMD_WEAPON intro inventory AND a spawn weapon equipped. Cross-references prior audit's F-1, F-5, F-8. Plausibility LIVE conditional on the bit ever leaking into Co-Op or Counter-Op.

### H-3. ROM-gate uninit class beyond visionmode + handmodeldef (ROM-GATE, LIVE)

**Possibility:** A propagation audit MAY find more `struct player` / `struct gunctrl` / `struct hand` fields that were only initialized inside a `#if VERSION >= VERSION_*` block in upstream and now read stale heap garbage in PD2. Observable as random misbehavior at first frame after `playermgrAllocatePlayer`. Plausibility LIVE; class is established by precedent, exhaustion of the class is unverified. Highest-leverage hardening pass.

### H-4. Catalog modeldef cache lifetime across stage transitions (CATALOG + N64-DECOUPLE, SPECULATIVE)

**Possibility:** Upstream's `if (normmplayerisrunning && !IS4MB())` in `body0f02ce8c` forced `g_FileInfo[..].loadedsize = 0` to defeat MP head caching. PD2 deleted that branch and relies on `catalogResetAllModeldefs()` (called from `bodiesReset`) to invalidate cached pointers. If the catalog backend caches modeldef data in a layer downstream of `g_HeadsAndBodies[].modeldef` (e.g., in the `asset_entry_t` ext block), and `catalogResetAllModeldefs` only nulls `g_HeadsAndBodies[].modeldef`, the head modeldef pointer is reloaded but the underlying bytes from the prior arena are reused. Observable as wrong head texture / animation on the second match. Plausibility SPECULATIVE; resolution depends on internal catalog caching shape. Worth a specific check on whether `catalogResetAllModeldefs` walks the asset registry vs only walking `g_HeadsAndBodies[]`.

### H-5. Stage-id strcmp miss in `bodiesReset` (CATALOG, SPECULATIVE)

**Possibility:** `bodyreset.c:51-65` replaced the upstream stage-table override (numeric stage indices for STAGE_INFILTRATION / RESCUE / ESCAPE) with `strcmp(g_MissionConfig.stage_id, "base:infiltration")`. If `g_MissionConfig.stage_id` is empty or differs from the literal (e.g., AllInOne mod uses a different namespace, ROM hashing causes a fallback ID), the override silently does not apply and the wrong head sets are used. Plausibility SPECULATIVE; the stage_id constraint says these strings are canonical (per `constraints.md` "Catalog ID strings at all interface boundaries"), but mods are an explicit boundary surface.

### H-6. body0f02ce8c silent fallback to bodynum=0 (CATALOG, SPECULATIVE)

**Possibility:** `body.c:178-184` clamps a bad `bodynum` (negative or >= ARRAYCOUNT) to slot 0 (DJ Bond) with only a log line. If a catalog miss ever returns a runtime index outside the registered range (e.g., a mod registered a body but its runtime_index was assigned post-spawn), the chr will silently appear as DJ Bond instead of crashing or rendering blank. Hard to attribute to a single bug without log inspection. Plausibility SPECULATIVE.

### H-7. chrInit NULL chr propagation (defensive guard, SPECULATIVE)

**Possibility:** `chr.c:1137-1142` adds a NULL-guard on `chr` after the slot scan. Upstream would have NULL-deref'd if the chr pool was exhausted. PD2 returns without setting `chr->chrnum` etc., leaving `prop->chr = NULL`. Any later prop-iteration that reads `prop->chr` without a NULL-check MAY now silently fail rather than crash. The crash was a clear signal; the silent fail is less so. Plausibility SPECULATIVE; depends on whether prop-iteration sites check NULL.

### H-8. B-181 fallback silently overrides user MP options (PD2-only invariant, SPECULATIVE)

**Possibility:** `setup.c:2745-2746` ORs `MPOPTION_SPAWNWITHWEAPON` onto both `g_MatchConfig.options` and `g_MpSetup.options` without recording the original user choice. If the user explicitly disabled spawn-with-weapon via the menu, the engine MAY override silently. Cross-reference prior audit F-7. Additional risk: if `g_MpSetup` is not reset between matches, the OR-set persists into the next match (where pickups might be sufficient). Plausibility SPECULATIVE; depends on `g_MpSetup` reset cadence.

### H-9. Bot-side asymmetry on per-bot modeldef preload (CATALOG asymmetry, SPECULATIVE)

**Possibility:** `bot.c` does not call `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` per-bot before `botinvSwitchToWeapon`, unlike `player.c:1822`. Relies entirely on the `setup.c:2825` batch preload. If a bot's resolved weapon ever ends up outside both the active set and the explicit `spawnWeaponNum` (e.g., per-bot loadouts in a future feature, or dynamic mid-match scenario change without rebuild), the model is missing. Plausibility SPECULATIVE; benign today, surveillance-worthy.

### H-10. GE-legacy weapon enum residual references (OTHER, SPECULATIVE)

**Possibility:** `playermgr.c:774-782` and `playerreset.c:609-647` removed eight cheat / model-mapping cases for PP9I / CC13 / KL01313 / KF7SPECIAL / ZZT / DMC / AR53 / RCP45. If any path still produces one of those `WEAPON_*` enum values at runtime (saved cheat config, live mod config, hardcoded constant elsewhere), the cheat is silently inert and the chr-prop side has no model mapping. Plausibility SPECULATIVE conditional on residual references existing; cross-reference prior audit F-9.

### H-11. Catalog stage_id integer derivation race (CATALOG, SPECULATIVE)

**Possibility:** `setup.c:1283` resolves `setupfilenum` from `catalogGetStageResultByIndex(stageindex, &stage)` not from `g_Stages[stageindex].setupfileid`. If the catalog has not registered the stage by the time `setupLoad` runs (mod load race, save-game restore with deferred catalog rebuild), `stage.setupfileid` is `0` and `assetLoadRomToAddr(0, ...)` either fails or loads the wrong asset. Plausibility SPECULATIVE; depends on catalog readiness invariant at stage-load entry.

### H-12. Body modeldef NULL checks in render path (CATALOG, SPECULATIVE)

**Possibility:** `body.c:198-216` rejects torn modeldefs at the `body0f02ce8c` entry (NULL skel, NULL rootnode, numparts <= 0 or > 500). Returns the `model` argument unchanged, which is NULL on the caller-allocated path. Callers that do not NULL-check the return MAY see NULL `chr->model` later. Plausibility SPECULATIVE; the immediate caller is `bodyAllocateChr` which itself returns model and is checked, but cascading callers are not all audited.

### H-13. Cache-fill diag overhead at IDLE-frame cadence (OTHER, RULED-OUT)

**Possibility:** `bondgun.c:8480-8488` runs a `LOG.WPN.DIAG cache_fill` `sysLogPrintf` on every cache cold-fill for player 0. RULED-OUT as a bug shape: cold-fill happens once per weapon switch, not per frame.

### H-14. Matrix cache stale T-pose (resolved at cafbd2ef) (animation authoring, RULED-OUT for code-side)

**Possibility:** `bondgun.c:8446-8497` cache USE branch byte-equivalent to upstream. RULED-OUT as a code-side divergence. The bug is in animation authoring (PD2 / AllInOne IDLE at anim 236 frame 17 vs upstream IDLE at anim 0 frame 0); the cache logic is correct under upstream's authoring convention and incorrect under PD2's. Phase B fix at `cafbd2ef` drops the cache USE branch and is the right answer for PD2. The Phase A predicate `bgunMatrixCacheIsStale` in `port/src/bondgun_cache.c` is a frozen specification for any future cache reintroduction.

### Hypothesis ranking by plausibility

| Tier | Hypothesis | Axis | Reason |
|------|------------|------|--------|
| LIVE-1 | H-1 catalog-miss-unarmed | CATALOG | B-219 v2 narrative is direct evidence |
| LIVE-2 | H-3 ROM-gate uninit class | ROM-GATE | Two prior incidents establish the class; exhaustion unverified |
| LIVE-3 | H-2 F-1 / F-5 / F-8 cluster | CATALOG-side guard drop | Asymmetric guards are a maintenance hazard regardless of current trigger |
| SPECULATIVE-1 | H-4 catalog modeldef cache lifetime | CATALOG + N64-DECOUPLE | Conditional on catalog-internal caching shape; testable with stage-transition log |
| SPECULATIVE-2 | H-8 B-181 silent options-mutation | PD2-only | Worth UX double-check; persists across matches if `g_MpSetup` not reset |
| SPECULATIVE-3 | H-12 body modeldef NULL cascade | CATALOG | Defensive guard returns NULL silently; cascading caller audit needed |
| SPECULATIVE-4 | H-5 stage-id strcmp miss | CATALOG | Conditional on stage_id format invariants |
| SPECULATIVE-5 | H-6 body0f02ce8c bodynum=0 fallback | CATALOG | Conditional on catalog returning OOB index |
| SPECULATIVE-6 | H-7 chrInit NULL chr propagation | defensive | Silent fail vs crash tradeoff |
| SPECULATIVE-7 | H-9 bot-side preload asymmetry | CATALOG | Benign today; surveillance |
| SPECULATIVE-8 | H-10 GE-legacy enum residuals | OTHER | Conditional on residual references |
| SPECULATIVE-9 | H-11 catalog stage_id race | CATALOG | Conditional on catalog readiness |
| RULED-OUT | H-13 diag cache-fill overhead | OTHER | Cadence is once-per-switch |
| RULED-OUT | H-14 matrix cache code-side | OTHER | Authoring problem, not code; Phase B fix lands at cafbd2ef |

---

## I. Cross-cuts (related subsystems potentially affected)

### I-1. Asset catalog readiness invariant

The CATALOG axis hypotheses (H-1, H-4, H-5, H-6, H-11, H-12) all reduce to "is the catalog fully populated and consistent at the moment this code runs?" Catalog readiness is implicit; it is not asserted at any chr-init or weapon-spawn entry point. Cross-cut subsystems:

- **Mod loader** (`port/src/modmgr.c`): registers mod-extensible entries; if a mod registration races stage load, `catalogGetMpWeaponNum` MAY return 0 for an entry that was selected from the menu. Verifiable via instrumentation at `setupLoadStage` end.
- **Net manifest** (`port/src/net/netmanifest.c`): ensures asset coverage for an MP match; constraint `manifestClear` before `mainChangeToStage` guards one race window. Other windows are open if `setupLoadStage` runs while `manifestEnsureLoaded` is mid-flight.
- **Save migration** (`SAVE_VERSION` framework): a saved match config could reference a catalog ID that no longer exists post-update. `catalogGetMpWeaponNum` returns 0; H-1 fires.

### I-2. mempAlloc zero-fill assumption

The ROM-GATE hypothesis H-3 depends on `mempAlloc` returning unzeroed memory. Cross-cut subsystems:

- **Memory pool architecture** (`memory-modernization.md`): if PD2's `MEMPOOL_STAGE` ever switches to zero-fill, all the H-3 candidates become latent-only. Whether to switch is a project-level decision, not a per-bug fix.
- **Test-suite coverage** (`tests/`): a unit test that allocs from `MEMPOOL_STAGE` and verifies non-zero bytes would tripwire the assumption.
- **Heap layout post-S346**: the romProvider catalog refactor changed allocation paths in some code. Any new path that bypasses the legacy `mempAlloc` path needs an explicit zero-fill audit.

### I-3. Manifest coverage vs catalog coverage

`bodyAllocateModel` (body.c:354-385) calls `manifestEnsureLoaded()` for body / head canon-IDs. The manifest and the catalog are separate registries:

- Manifest = "what IDs has this match committed to having"
- Catalog = "what IDs are registered globally"

A match can request a manifest-tracked body ID that has not been registered in the catalog (e.g., a remote client's mod is missing locally). The manifest path returns a torn modeldef; PD2's body.c:198-216 sanity gate catches it. But for fields not covered by the sanity gate (scale / animscale / canvaryheight / ismale), the catalog accessor returns a default and the chr renders with wrong proportions. Possibility: cross-validate manifest coverage against catalog coverage at match-start in any future MP-coverage hardening pass.

### I-4. Spawn pool integration with `playerSpawn`

The PD2-only L2 spawn pool (`playerreset.c:447-485`) builds a validated set of spawn positions before `playerSpawn` runs. If `spawnPoolBuildGlobal` aborts or returns empty, `playerSpawn` falls through to upstream's `scenarioChooseSpawnLocation`. Possibility: any failure in the spawn pool layer is invisible to `playerSpawn`'s positional logic; the upstream path runs as a fallback. Cross-cut: `spawn-system-architecture-2026-04-13.md` describes the full L1-L4 cascade.

### I-5. Asset loading API split (S346)

`assetLoadRomToNew` / `fileLoadToNew` / `assetLoadToNew` are now distinct entry points (per S346 constraint that game code must not call `romProviderHandle()` directly). The chr-init / weapon-spawn paths were migrated:
- `setup.c:1283` uses `assetLoadRomToAddr` (was `fileLoadToAddr`)
- `setup.c:1383` uses `assetLoadToNew(stage.setup_handle)` (was `fileLoadToNew(setupfileid)`)
- `body.c:163` reads through `catalogGetBodyModeldef`

If any new code is added in this surface, it must use the new APIs. The migration is consistent at `a30f3719` per S346 audit.

### I-6. Forge mode interactions

`bodyAllocateChr` (body.c:455-468), `setup.c:1741-1837` OBJTYPE_CHR / KEY / HAT / AUTOGUN / LIFT / ESCASTEP all gain `forgeIsCanvasMode()` early-return / gate. Forge mode disables chr / item allocation during edit so the editor camera can fly without the underlying world ticking. Possibility: any new chr-init site that does not check `forgeIsCanvasMode()` MAY allocate chrs during forge edit, leaving them present when the editor exits. Surface for surveillance.

---

## J. Summary

- **48+ deltas catalogued** across `playermgr.c`, `playerreset.c`, `player.c`, `bondgun.c`, `bondgunreset.c`, `setup.c`, `bot.c`, `body.c`, `bodyreset.c`, `chr.c`. Bucketed: ~18 CATALOG, ~10 N64-DECOUPLE, ~3 ROM-GATE, rest OTHER (instrumentation, defensive, manifest, spawn pool, forge, MAX_PLAYERS scaling, net hooks, B-12 participant pool migration).
- **14 hypotheses framed**, ranked LIVE / SPECULATIVE / RULED-OUT. Three LIVE: catalog-miss-unarmed (H-1), ROM-gate uninit class (H-3), F-1/F-5/F-8 cluster (H-2). Two RULED-OUT: matrix cache code-side (H-14, the bug is animation authoring), diag overhead (H-13).
- **Top three architectural risk surfaces**:
  1. Catalog readiness at chr-init / weapon-spawn entry points is implicit, not asserted. Catalog miss returns silent defaults rather than failing loudly. (CATALOG axis, H-1 + H-4 + H-5 + H-6 + H-11 + H-12 cluster)
  2. ROM-gate strip latent uninit class is established by two prior fixes (`visionmode`, `handmodeldef`) but not exhausted. Other `struct player` / `struct gunctrl` / `struct hand` fields whose only init lived in `#if VERSION >=` blocks in upstream are candidates. (ROM-GATE axis, H-3)
  3. Asymmetric MP guards (`g_Vars.normmplayerisrunning` dropped at the spawn-with-weapon branch but kept at the playerreset.c gate) create a maintenance hazard even if the current behavior is benign. (CATALOG-side change, H-2 cluster)
- **Code-side analysis confirms** the matrix-cache fix at `cafbd2ef` is structurally correct and the bug-shape is in animation authoring, not the C code. Upstream's cache logic works because IDLE is at anim 0 frame 0; PD2 / AllInOne authoring places IDLE at anim 236 frame 17, which makes the cache cold-fill capture a stale T-pose. Phase B drops the cache.
- **No fix proposals.** Findings are framed as possibilities for Mike to triage, prioritise, or surveil.
