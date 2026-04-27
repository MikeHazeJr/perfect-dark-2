# Player init + spawn-with-weapon: PD2 vs. fgsfdsfgs/perfect_dark upstream

**Audit date:** 2026-04-26
**Author session:** elegant-lederberg-edcb44 (research only, no code changes to PD2)

This audit compares PD2's player initialisation and spawn-with-weapon code against the upstream decomp it was forked from. The trigger is Mike's hypothesis that the original port handled spawn-with-weapon differently and PD2 may have broken something during the heavy modifications across master loader / catalog migration / version-gate strips.

A parallel fix for the per-hand matrix cache (`bgun0f0a5550` `unk0dd8`) is landing on `dev` in commits [`1cfc055b`](../../../../) (predicate + tests) and [`cafbd2ef`](../../../../) (cache drop). Section G cross-cuts that fix against the upstream pattern. **No code changes to PD2 in this session** -- findings only, framed as possibilities for Mike to triage.

---

## A. Upstream reference

- **Repo:** [github.com/fgsfdsfgs/perfect_dark](https://github.com/fgsfdsfgs/perfect_dark)
- **HEAD commit:** `bed3bf52d0d5095d112940b1327ed6c256e54ea8` (2026-04-25, "Merge pull request #715 from Alex-LeTux/patch-1")
- **Clone:** shallow (`--depth 100`), location external to PD2 worktree
- **Working dir during audit:** `/tmp/upstream_pd_decomp` (resolves to `C:/Users/mikeh/AppData/Local/Temp/upstream_pd_decomp`)

The upstream is the C decomp of N64 Perfect Dark with PC port additions. It is PD2's structural ancestor. Files are in the same paths and roughly the same shape; PD2 has overlaid AllInOneMods (GEX, Kakariko, Goldfinger 64, Dark Noon), netplay (ENet), the asset catalog migration, and the dedicated server.

## B. PD2 reference

- **Repo path:** `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike` (worktree `elegant-lederberg-edcb44` for this audit)
- **HEAD commit:** `ca23a3d2f35a5572d940afaee25b1c378d55efac` ("merge: B-246 round-10 Phase B fix(bondgun) drop matrix cache")
- **Branch:** `claude/elegant-lederberg-edcb44`
- **Base of current dev work:** matrix-cache fix was just merged in `a30f3719` + `1cfc055b` + `cafbd2ef`

Total scope reviewed (merge-relevant lines):

| File | Upstream | PD2 | Delta |
|------|----------|-----|-------|
| [player.c](src/game/player.c) | 5880 | 7027 | +1147 |
| [playermgr.c](src/game/playermgr.c) | 854 | 879 | +25 |
| [bondgun.c](src/game/bondgun.c) | 13310 | 13966 | +656 |
| [bondgunreset.c](src/game/bondgunreset.c) | 269 | 283 | +14 |
| [bondmove.c](src/game/bondmove.c) | 2585 | 3133 | +548 |
| [setup.c](src/game/setup.c) | 2312 | 2856 | +544 |
| [playerreset.c](src/game/playerreset.c) | 482 | 829 | +347 |
| [bot.c](src/game/bot.c) | (similar) | (similar) | new spawn-weapon path + diag |

PD2 is roughly 12% larger across this surface. Diff is tractable for side-by-side analysis.

---

## C. Player init flow side-by-side

| Stage | Function | Upstream site | PD2 site | Behaviour summary |
|-------|----------|---------------|----------|-------------------|
| Per-player struct alloc | `playermgrAllocatePlayer` | [playermgr.c](src/game/playermgr.c) | [playermgr.c](src/game/playermgr.c) | Allocates `struct player`; defaults gunctrl fields, vision, network state |
| Per-player gun reset | `bgunReset` | [bondgunreset.c](src/game/bondgunreset.c:bgunReset) | [bondgunreset.c](src/game/bondgunreset.c:bgunReset) | Allocates gunmem; init hand structs to defaults; sets gunmemowner |
| Stage init (per-life) | `playerReset` | [playerreset.c:110](src/game/playerreset.c) | [playerreset.c:116](src/game/playerreset.c) | Walks `g_StageSetup.intro` INTROCMD chain; loads default weapons + ammo; sets `g_DefaultWeapons[]` |
| Per-life respawn | `playerSpawn` | [player.c:939](src/game/player.c) | [player.c:1613](src/game/player.c) | Inv/shield reset; spawn-with-weapon application; chr-body tick |
| Counter-Op anti-respawn | `playerSpawnAnti` | [player.c:822](src/game/player.c) | [player.c:1496](src/game/player.c) | Suicide pill + unarmed equip for the Counter-Op anti-player |
| Bot spawn | `botSpawn` | [bot.c:265](src/game/bot.c) | [bot.c:506](src/game/bot.c) | Bot version of spawn-with-weapon (uses `botinv*`) |
| Stage load | `setupLoadStage` | [setup.c](src/game/setup.c) | [setup.c](src/game/setup.c) | Stage-load orchestrator; pickups; new in PD2: SPAWNWITHWEAPON fallback + manifest-driven model preload |
| Master loader cache fill | `bgunTickMasterLoad` | [bondgun.c:4216](src/game/bondgun.c) | [bondgun.c:4486](src/game/bondgun.c) | Sets `unk0dd4 = -1`, allocates 50-Mtxf cache slab `unk0dd8` |
| Master loader cache use | `bgun0f0a5550` | [bondgun.c:7899](src/game/bondgun.c) | [bondgun.c:8340](src/game/bondgun.c) | Per-frame matrix update; pre-fix used cache, PD2 now skips cache (B-246 r10) |
| Weapon switch | `bgunEquipWeapon2` | [bondgun.c](src/game/bondgun.c) | [bondgun.c](src/game/bondgun.c) | Queues a hand-state switch; `bgunTickSwitch2` later applies it |

Both codebases share the same overall initialisation order:

```
playermgrAllocatePlayer (per-player) -> bgunReset (per-player gun)
playerReset (per-life, runs INTROCMDs incl. WEAPON, populates g_DefaultWeapons)
playerSpawn (per-life, applies SPAWNWITHWEAPON or g_DefaultWeapons via bgunEquipWeapon2)
bgunTickSwitch2 (later in main loop -- master loader runs CARTS->LOADED, then bgunTickGameplay can render)
```

## D. Spawn-with-weapon path -- end-to-end trace

### Upstream

**Source-of-truth:** `g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON` (set by menu checkbox at [mplayer/setup.c:6160-6167](src/game/mplayer/setup.c)).

**Spawn weapon resolution:** `g_MpSetup.weapons[0]` (the first slot in the active weapon set).

**Modeldef readiness path:** per-pickup `setupPlaceWeapon` calls `modelmgrLoadProjectileModeldefs` whenever a map pickup marker is laid down. Maps with no markers (i.e. CS arenas) implicitly cannot rely on this.

**Application site:** [player.c:1117-1139](src/game/player.c) inside `playerSpawn`:

```c
if (g_Vars.normmplayerisrunning
        && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)
        && g_MpSetup.weapons[0] != MPWEAPON_NONE
        && g_MpSetup.weapons[0] != MPWEAPON_DISABLED
        && g_MpSetup.weapons[0] != MPWEAPON_SHIELD) {
    struct mpweapon *mpweapon = &g_MpWeapons[g_MpSetup.weapons[0]];
    invGiveSingleWeapon(mpweapon->weaponnum);
    const s32 ammotype = (g_MpSetup.weapons[0] == MPWEAPON_COMBATBOOST)
        ? AMMOTYPE_BOOST : mpweapon->priammotype;
    if (ammotype) {
        s32 startammo = mpweapon->priammoqty / 2;
        if (startammo == 0) startammo = 1;
        bgunSetAmmoQuantity(ammotype, startammo);
    }
    bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
    bgunEquipWeapon2(HAND_RIGHT, mpweapon->weaponnum);
} else {
    bgunEquipWeapon2(HAND_LEFT, g_DefaultWeapons[HAND_LEFT]);
    bgunEquipWeapon2(HAND_RIGHT, g_DefaultWeapons[HAND_RIGHT]);
}
```

Outer guard: this whole branch lives inside `if (g_Vars.mplayerisrunning) { ... else (non-anti-player) { ... } }`. The inner `g_Vars.normmplayerisrunning` further restricts to **normal MP** (i.e. excludes Co-Op and Counter-Op).

`mpweapon` is a direct pointer into the static `g_MpWeapons[]` array indexed by `MPWEAPON_*` enum. Lookup is compile-time fixed.

### PD2

**Source-of-truth (extended):**
1. `g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON` (same flag, additional setters)
2. `g_MatchConfig.spawnWeaponNum` (u8, **PD2-only**) -- explicit weapon-num override resolved from [setup.c at matchStart](src/game/setup.c) by F.6 / M0.1c
3. `g_MatchConfig.spawn_weapon_id[]` (string catalog ID, **PD2-only**) -- catalog id of the user-selected spawn weapon

**Setters that flip MPOPTION_SPAWNWITHWEAPON ON:**
- [mplayer/setup.c:6542](src/game/mplayer/setup.c) -- the menu checkbox (parity with upstream)
- [setup.c:2745-2746](src/game/setup.c) -- B-181 fallback that **forces SPAWNWITHWEAPON ON when world pickups < threshold** to keep matches armed (PD2-only)

**Modeldef readiness path (extended):**
- per-pickup `setupPlaceWeapon` (parity with upstream)
- [setup.c:2825-2851](src/game/setup.c) -- B-219 v3 / B-229 manifest-driven preload that walks `g_MpSetup.weapons[]` and `g_MatchConfig.spawnWeaponNum`, calling `modelmgrLoadProjectileModeldefs` for each (PD2-only)
- [player.c:1822](src/game/player.c) -- per-spawn defensive `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` immediately before `invGiveSingleWeapon` (PD2-only)

**Pre-application gate (PD2-only):** [playerreset.c:225-232](src/game/playerreset.c) -- when `g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)`, the entire `INTROCMD_WEAPON` case in `playerReset`'s intro walk is **skipped**. Comment cites B-219: prevents loop 1 (`playerReset` -> `INTROCMD_WEAPON`) from racing loop 2 (`playerSpawn` -> spawn-with-weapon -> `bgunEquipWeapon2`) and ending up with conflicting equip state vs HUD.

**Application site:** [player.c:1790-1850](src/game/player.c):

```c
if (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON) {
    s32 resolvedWeaponNum = 0;
    s32 spawnWeaponIdx = -1;
    if (g_MatchConfig.spawnWeaponNum != 0xFF && g_MatchConfig.spawnWeaponNum != 0) {
        resolvedWeaponNum = (s32)g_MatchConfig.spawnWeaponNum;
        for (wi = MPWEAPON_FALCON2; wi < NUM_MPWEAPONS; wi++) {
            if (catalogGetMpWeaponNum(wi) == resolvedWeaponNum) {
                spawnWeaponIdx = wi;
                break;
            }
        }
    } else if (g_MpSetup.weapons[0] != MPWEAPON_NONE
            && g_MpSetup.weapons[0] != MPWEAPON_DISABLED
            && g_MpSetup.weapons[0] != MPWEAPON_SHIELD) {
        spawnWeaponIdx = g_MpSetup.weapons[0];
        resolvedWeaponNum = catalogGetMpWeaponNum(spawnWeaponIdx);
    }
    if (resolvedWeaponNum > 0) {
        modelmgrLoadProjectileModeldefs(resolvedWeaponNum);
        invGiveSingleWeapon(resolvedWeaponNum);
        if (spawnWeaponIdx >= 0) {
            const s32 ammotype = (spawnWeaponIdx == MPWEAPON_COMBATBOOST)
                ? AMMOTYPE_BOOST : catalogGetMpWeaponPriAmmoType(spawnWeaponIdx);
            if (ammotype) {
                s32 startammo = catalogGetMpWeaponPriAmmoQty(spawnWeaponIdx) / 2;
                if (startammo == 0) startammo = 1;
                bgunSetAmmoQuantity(ammotype, startammo);
            }
        }
        bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE);
        bgunEquipWeapon2(HAND_RIGHT, resolvedWeaponNum);
    } else {
        bgunEquipWeapon2(HAND_LEFT, g_DefaultWeapons[HAND_LEFT]);
        bgunEquipWeapon2(HAND_RIGHT, g_DefaultWeapons[HAND_RIGHT]);
    }
} else {
    bgunEquipWeapon2(HAND_LEFT, g_DefaultWeapons[HAND_LEFT]);
    bgunEquipWeapon2(HAND_RIGHT, g_DefaultWeapons[HAND_RIGHT]);
}
```

Outer guard: same `if (g_Vars.mplayerisrunning) { ... else { ... } }` as upstream. **The inner `g_Vars.normmplayerisrunning` guard is dropped.**

Lookups:
- `catalogGetMpWeaponNum(idx)` -- runtime catalog API (replaces direct `g_MpWeapons[idx].weaponnum`)
- `catalogGetMpWeaponPriAmmoType(idx)` -- runtime catalog API (replaces direct `g_MpWeapons[idx].priammotype`)
- `catalogGetMpWeaponPriAmmoQty(idx)` -- runtime catalog API (replaces direct `g_MpWeapons[idx].priammoqty`)

---

## E. Diff catalog (per-function summary)

| File | Function | PD2 added | PD2 removed | PD2 changed |
|------|----------|-----------|-------------|-------------|
| [player.c](src/game/player.c) | `playerSpawn` | `g_MatchConfig.spawnWeaponNum` override path; `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` defensive preload at line 1822; explicit fallback `bgunEquipWeapon2` to `g_DefaultWeapons[]` when SPAWNWITHWEAPON is set but resolution fails; LOG.WPN.DIAG end-of-spawn dump for player 0 | (none) | Inner `g_Vars.normmplayerisrunning` guard dropped on the SPAWNWITHWEAPON branch; switched from direct `g_MpWeapons[idx]` array access to catalog API; replaced literal `g_Vars.currentplayer == g_Vars.anti` with `PLAYER_IS_ANTI(g_Vars.currentplayer)` macro |
| [playerreset.c](src/game/playerreset.c) | `playerReset` | New INTROCMD_WEAPON skip-gate when `normmplayerisrunning && SPAWNWITHWEAPON`; mode-tagged GAMELOOP.* logging; intro-loop runaway watchdog (10000 iter cap); 8-player `g_PlayersWithControl[]` extension | (none structural) | Anti-player counter-op INTROCMD_WEAPON handling rewritten through `PLAYER_IS_NOT_ANTI` macro |
| [bondgun.c](src/game/bondgun.c) | `bgun0f0a5550` (cache use) | LOG.WPN.DIAG enter logging; bone-snapshot diagnostic on IDLE<->FIRE transitions; B-246 r10 Phase B comment block | The `if (a0)` cache-reuse branch (cache **USE** path only; cache fill at `bgunTickMasterLoad` is preserved) | Always runs `modelSetMatricesWithAnim` against live anim state |
| [bondgun.c](src/game/bondgun.c) | `bgunTickMasterLoad` (cache fill) | LOG.WPN.DIAG transition logging at CARTS->LOADED and SHORTCUT->LOADED | (none) | (none structural -- cache fill path identical) |
| [bondgun.c](src/game/bondgun.c) | `bgunReset` | LOG.WPN.DIAG bgunReset state dump for player 0 | `IS4MB() 2P` branch + `g_BgunGunMemBaseSize4Mb2P` extern; `PLATFORM_N64` ifndef around `player.h` include | Always uses `bgunCalculateGunMemCapacity()` |
| [playermgr.c](src/game/playermgr.c) | `playermgrAllocatePlayer` | **B-246 r7: `gunctrl.handmodeldef = NULL` and `gunctrl.cartmodeldef = NULL` defensive inits**; `wantsjump = false`, `jumpconsumed = true`; `client = NULL`, `ucmd`, `isremote = false`; unconditional `visionmode = VISIONMODE_NORMAL` (drops `VERSION >= VERSION_JPN_FINAL` gate); `MAX_PLAYERS > 4` generic loops; `netPlayersAllocate()` call after per-player alloc; NULL guard `if (!g_Vars.players[i]) continue` in prop->player lookup | `g_Vars.fourmeg2player` viewport branch; GoldenEye legacy-weapon -> chr-model mappings (PP9I/CC13/KL01313/KF7SPECIAL/ZZT/DMC/AR53/RCP45) | (none other than the macro modernisations) |
| [bondgunreset.c](src/game/bondgunreset.c) | `bgunReset` | LOG.WPN.DIAG init dump | `IS4MB() && PLAYERCOUNT() == 2` 4MB branch | Always uses `ALIGN16(bgunCalculateGunMemCapacity())` |
| [setup.c](src/game/setup.c) | `setupLoadStage` | B-181 fallback that forces `MPOPTION_SPAWNWITHWEAPON` on when world pickups < threshold; user-pick preservation (do not override explicit spawnWeaponNum); B-219 v3 / B-229 manifest-driven `modelmgrLoadProjectileModeldefs` preload over `g_MpSetup.weapons[]` and `g_MatchConfig.spawnWeaponNum`; SETUP.* logging | (none -- additive) | (additive only) |
| [bot.c](src/game/bot.c) | `botSpawn` | `g_MatchConfig.spawnWeaponNum` override path mirroring player.c; CHR.DIAG post-spawn invisibility check; stuck-detection snapshot init | (none) | catalog API replaces direct `g_MpWeapons[idx]` access; **does NOT call `modelmgrLoadProjectileModeldefs` per-bot** (relies on setup.c batch preload) |

---

## F. Suspicious-divergence list

Each entry: PD2 site, upstream parallel, what PD2 does differently, plausibility framing (LIVE / SPECULATIVE / RULED-OUT) for whether it could plausibly contribute to a current bug. **No fixes proposed**; Mike triages.

### F-1. `g_Vars.normmplayerisrunning` inner guard removed on SPAWNWITHWEAPON branch

- **PD2:** [player.c:1790](src/game/player.c) -- entry condition is `if (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON)` only. Outer `if (g_Vars.mplayerisrunning)` still in place at line 1652.
- **Upstream:** [player.c:1117](src/game/player.c) -- entry condition is `if (g_Vars.normmplayerisrunning && (g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON))`.
- **Difference:** PD2 reaches the spawn-with-weapon branch in **Co-Op** (`g_Vars.coopplayernum >= 0`) and **Counter-Op Bond side** (`g_Vars.antiplayernum >= 0` with `currentplayer != anti`), where upstream restricts to normal MP combat sim.
- **Plausibility:** SPECULATIVE. Whether this is a real divergence depends on whether `g_MpSetup.options` carries `MPOPTION_SPAWNWITHWEAPON` into Co-Op / Counter-Op modes. If `g_MpSetup` is reset between mode transitions (likely per the SP-1 manifest hardening pattern in [systemic-bugs.md](../systemic-bugs.md)), this is fine -- the bit is never set in Co-Op. If `g_MpSetup.options` survives a transition, Co-Op players would suddenly receive a Combat Sim spawn weapon instead of the mission-INTRO loadout. **Worth a propagation check**: search every `g_MpSetup.options =` and confirm it's reset on mode entry.

### F-2. PD2's spawn-with-weapon path replaces direct array access with catalog API lookups

- **PD2:** uses `catalogGetMpWeaponNum(idx)`, `catalogGetMpWeaponPriAmmoType(idx)`, `catalogGetMpWeaponPriAmmoQty(idx)` to derive the weapon num and ammo metadata.
- **Upstream:** uses `g_MpWeapons[idx].weaponnum`, `.priammotype`, `.priammoqty` -- direct compile-time array access.
- **Difference:** the catalog lookup is a runtime indirection. If the catalog is not fully populated when `playerSpawn` runs (early-load race, late mod registration, partial manifest), the lookup returns 0 / -1 and `resolvedWeaponNum` stays 0. PD2's branch then falls through to `bgunEquipWeapon2(HAND_LEFT, g_DefaultWeapons[HAND_LEFT])` -- but `g_DefaultWeapons` is 0 because the playerreset.c:225 gate skipped INTROCMD_WEAPON in MP. Result: spawn unarmed.
- **Plausibility:** SPECULATIVE. Currently unconfirmed whether catalog-lookup races exist in normal play; the asset catalog is supposed to be ready by `playerSpawn`. But the SETUP log line `"SETUP: random spawn weapon resolved from set: id='%s' num=%d"` indicates Mike has hit cases where catalogGetMpWeaponNum returned an unexpected value (the B-219 v2 fix was specifically for "user selected `base:remotemine` but saw Falcon (Silenced)"). **Worth a propagation check** for catalog-readiness invariants vs spawn timing.

### F-3. Bot path lacks per-bot defensive `modelmgrLoadProjectileModeldefs`

- **PD2:** [bot.c:506-549](src/game/bot.c) calls `botinvGiveSingleWeapon` + `botinvSwitchToWeapon` but has no `modelmgrLoadProjectileModeldefs(resolvedWeaponNum)` immediately before, unlike [player.c:1822](src/game/player.c).
- **Upstream:** [bot.c:265-281](src/game/bot.c) also does not call `modelmgrLoadProjectileModeldefs` (parity at this point).
- **Difference:** PD2 added the defensive load to `playerSpawn` per B-219 v3 but did not mirror it into `botSpawn`. The setup.c:2825-2851 batch preload covers `g_MpSetup.weapons[]` and `g_MatchConfig.spawnWeaponNum` so most cases are fine. The asymmetry only matters if a bot's resolved weapon ends up outside both -- which is unusual but possible if bots can have their own loadouts in the future, or if `g_MatchConfig.spawnWeaponNum` is mutated mid-match without re-running setup batch.
- **Plausibility:** SPECULATIVE. Probably benign today. Worth flagging for surveillance if bot spawn-with-weapon is ever expanded to per-bot loadouts.

### F-4. `gunctrl.handmodeldef` and `gunctrl.cartmodeldef` defensive NULL inits in PD2 only

- **PD2:** [playermgr.c at line ~417](src/game/playermgr.c) defensively initialises `gunctrl.handmodeldef = NULL` and `gunctrl.cartmodeldef = NULL` per B-246 r7. Comment cites round-6 playtest log showing `handmodeldef=0x7c7b663545bbcbd1` (stale heap garbage, same value across all spawns).
- **Upstream:** has no such inits. Relies on the implicit zero-init pattern.
- **Difference:** the bug report says PD2's `mempAlloc` does not zero-fill (per the `visionmode=51503` finding in [weapon-system-deep-audit-2026-04-25.md](weapon-system-deep-audit-2026-04-25.md)). The same allocator path likely applies in upstream too -- so upstream may carry a latent bug here that simply did not manifest in their playtests, or they test with a zero-fill heap.
- **Plausibility:** LIVE risk for **other gunctrl fields** that PD2 has not yet defensively-initialised. The visionmode incident pattern says "any field that depends on zero-fill is a latent bug." A propagation check across `struct player` and `struct gunctrl` would surface every uninitialised field. **Worth a survey pass** as Mike triages: enumerate every `gunctrl.*` field and `player->*` field, check whether `playermgrAllocatePlayer` writes it before any read, mark each LIVE / DEFENSIVE-INIT-PRESENT / SAFE-BY-DESIGN.

### F-5. PD2 has DUAL spawn-with-weapon-aware sites (player.c + playerreset.c gate)

- **PD2:** spawn-with-weapon flow involves [playerreset.c:225 gate](src/game/playerreset.c) (skip INTROCMD_WEAPON when MP+SPAWNWITHWEAPON) AND [player.c:1790](src/game/player.c) (apply spawn weapon). Two pieces must agree.
- **Upstream:** spawn-with-weapon is a single decision at [player.c:1117](src/game/player.c). INTROCMD_WEAPON in [playerreset.c:182](src/game/playerreset.c) runs unconditionally (apart from anti-player guard). Upstream relies on the fact that MP missions don't have `INTROCMD_WEAPON` entries in their intro chain, so the question of conflict doesn't arise.
- **Difference:** PD2's gate at playerreset.c:225 keys on `g_Vars.normmplayerisrunning`. PD2's apply at player.c:1790 dropped `g_Vars.normmplayerisrunning` (see F-1). **The two checks are not symmetric.** If `g_Vars.normmplayerisrunning` is false but `g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON` is true (e.g. residual flag in Co-Op), playerreset.c **will** run INTROCMD_WEAPON and populate `g_DefaultWeapons[]`, AND player.c **will** also fire the spawn-with-weapon branch. Both apply. The `bgunEquipWeapon2(HAND_LEFT, WEAPON_NONE) + bgunEquipWeapon2(HAND_RIGHT, resolvedWeaponNum)` calls override the INTROCMD-derived defaults, so the **final** equip is the spawn-with-weapon, but the inventory has BOTH the INTROCMD weapons AND the spawn weapon -- player carries an unintended kit.
- **Plausibility:** LIVE if the F-1 condition triggers. Even if F-1 is benign in practice, the asymmetric guard structure is a maintenance hazard: a future change to one site that doesn't mirror to the other reintroduces the B-219 conflict pattern.

### F-6. Cache-fill site (`bgunTickMasterLoad`) byte-for-byte identical, but cache USE site diverges

- **PD2:** [bondgun.c:4486](src/game/bondgun.c) cache-fill is identical to upstream -- `unk0dd4 = -1`, allocate 50-Mtxf slab into `unk0dd8`, transition to `MASTERLOADSTATE_LOADED`.
- **Upstream:** [bondgun.c:4216](src/game/bondgun.c) -- same shape.
- **Difference:** PD2's [bondgun.c:8340-8413](src/game/bondgun.c) (cache USE site) drops the `if (a0)` cache-reuse branch and always runs `modelSetMatricesWithAnim`. Upstream [bondgun.c:7899-7938](src/game/bondgun.c) keeps it.
- **Plausibility:** RESOLVED. PD2's fix lands as `cafbd2ef`. The cache divergence is intentional and correct (see Section G).

### F-7. SETUP.c B-181 fallback can silently flip MPOPTION_SPAWNWITHWEAPON ON

- **PD2:** [setup.c:2745-2746](src/game/setup.c) sets `g_MatchConfig.options |= MPOPTION_SPAWNWITHWEAPON` and `g_MpSetup.options |= MPOPTION_SPAWNWITHWEAPON` when `s_SetupMpCreatedWeaponCount < desiredPickups` (too few world pickups).
- **Upstream:** has no equivalent setter. The flag is only set via the menu checkbox.
- **Difference:** PD2 silently enables spawn-with-weapon at stage-load time if pickups are too sparse. The user did not request it; the engine forces it. The user-pick preservation logic at [setup.c:2748-2794](src/game/setup.c) makes this less invasive (an explicit user pick is preserved), but the OR-set on `g_MpSetup.options` mutates the user's chosen MP options as a side effect. If the user had explicitly DISABLED spawn-with-weapon via the menu, PD2 would silently override.
- **Plausibility:** SPECULATIVE. Probably intentional (matches must be playable). The "silent override of an explicit user choice" framing is the part worth double-checking -- is there a UX expectation that "spawn-with-weapon OFF + low pickups + match starts anyway" is a valid configuration? If so, the OR-set is too aggressive.

### F-8. Inventory dual-add when fallback fires after SPAWNWITHWEAPON application

- **PD2:** [player.c:1840-1841](src/game/player.c) -- the `else (resolvedWeaponNum <= 0)` branch INSIDE the SPAWNWITHWEAPON gate equips `g_DefaultWeapons[HAND_LEFT/RIGHT]`. But because the playerreset.c:225 gate is keyed on `normmplayerisrunning` (see F-5), Co-Op or Counter-Op with `MPOPTION_SPAWNWITHWEAPON` set would have `g_DefaultWeapons[]` populated by INTROCMD_WEAPON. The fallback equip then runs INTRO weapons -- **but the inventory already received** `WEAPON_UNARMED` from line 1640's earlier `invGiveSingleWeapon(WEAPON_UNARMED)` and the INTROCMD_WEAPON pass already added the mission weapons.
- **Upstream:** same `invGiveSingleWeapon(WEAPON_UNARMED)` at line 966; INTROCMD_WEAPON adds intro weapons; spawn fallthrough re-equips intro defaults. No conflict because INTROCMD_WEAPON path is the ONLY weapon-add path in upstream.
- **Plausibility:** SPECULATIVE. Conditional on F-1 + F-5 firing. Worth checking once F-1 propagation has been audited.

### F-9. `playermgr.c` removed model mappings for GoldenEye legacy weapon enums

- **PD2:** [playermgr.c](src/game/playermgr.c) -- removed cases for `WEAPON_PP9I -> MODEL_CHRWPPK`, `WEAPON_CC13 -> MODEL_CHRTT33`, `WEAPON_KL01313 -> MODEL_CHRSKORPION`, `WEAPON_KF7SPECIAL -> MODEL_CHRKALASH`, `WEAPON_ZZT -> MODEL_CHRUZI`, `WEAPON_DMC -> MODEL_CHRMP5K`, `WEAPON_AR53 -> MODEL_CHRM16`, `WEAPON_RCP45 -> MODEL_CHRFNP90`.
- **Upstream:** has them.
- **Difference:** PD2 stripped these lines as part of the GE-mod removal / catalog migration. If any of these `WEAPON_*` enums are still produced by spawn-with-weapon resolution (e.g. user picks `base:pp9i` from a GoldenEye mod), the chr-prop side has no model lookup and the prop side falls to default fallthrough.
- **Plausibility:** SPECULATIVE. Only a problem if a runtime path can still produce one of those enums. Catalog migration was supposed to retire the GoldenEye weapon ids, but if any save-game / config / mod re-introduces them, this is a silent miss. **Worth a propagation check**: grep for `WEAPON_PP9I|WEAPON_CC13|WEAPON_KL01313|WEAPON_KF7SPECIAL|WEAPON_ZZT|WEAPON_DMC|WEAPON_AR53|WEAPON_RCP45` across PD2 to confirm nothing else still references them.

### F-10. PD2 stripped 4MB-mode special cases from gun memory allocation

- **PD2:** [bondgunreset.c at ~line 144](src/game/bondgunreset.c) -- `IS4MB() && PLAYERCOUNT() == 2` branch removed; always uses `ALIGN16(bgunCalculateGunMemCapacity())`.
- **Upstream:** [bondgunreset.c](src/game/bondgunreset.c) -- 4MB-mode branch sets `i = ALIGN16(g_BgunGunMemBaseSize4Mb2P)`.
- **Difference:** N64 4MB-only behaviour. PD2 (PC, no 4MB constraint) correctly removed it. **No semantic risk** to spawn-with-weapon.
- **Plausibility:** RULED-OUT. Listed for completeness.

---

## G. Matrix-cache cross-cut

The current dev branch has `cafbd2ef` (cache drop) just landed; this section answers whether upstream had the same problem.

### Question

Does `bgun0f0a5550` (the per-frame matrix update for the FP gun) in upstream have the same `unk0dd8` cache mechanism as PD2, and if so, does it have the same staleness pattern that prompted PD2's fix?

### Findings

**Cache fill is structurally identical.** Upstream [bondgun.c:4216-4224](src/game/bondgun.c) and PD2 [bondgun.c:4486-4494](src/game/bondgun.c) both:

```c
hand = &player->hands[0];
hand->unk0dd4 = -1;                 // cache cold flag

if (player->gunctrl.memloadremaining > 50 * sizeof(Mtxf)) {
    hand->unk0dd8 = (Mtxf *) player->gunctrl.memloadptr;   // 50-Mtxf cache slab
    player->gunctrl.memloadptr += 50 * sizeof(Mtxf);
    player->gunctrl.memloadremaining -= 50 * sizeof(Mtxf);
} else {
    hand->unk0dd8 = NULL;
}
```

**Cache USE differs.** Upstream [bondgun.c:7899-7938](src/game/bondgun.c) keeps the `if (a0) { cache reuse } else { fresh anim }` pattern:

```c
if (a0) {
    if (player->hands[HAND_RIGHT].unk0dd4 == -1) {
        // cold-fill into unk0dd8 via modelSetMatricesWithAnim
        modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);
        player->hands[HAND_RIGHT].unk0dd4 = 1;
    }
    // re-broadcast cached unk0dd8 onto live gun matrices via mtx00015be4
    spc8 = player->hands[HAND_RIGHT].unk0dd8;
    spc4 = hand->gunmodel.matrices;
    for (spcc = 0; spcc < hand->gunmodel.definition->nummatrices; spcc++) {
        mtx00015be4(&sp2c4, spc8, spc4);
        spc8++;
        spc4++;
    }
} else {
    // FIRE / RELOAD path: fresh anim, no cache
    modelSetMatricesWithAnim(&renderdata, &hand->gunmodel);
}
```

PD2's [bondgun.c:8340-8413](src/game/bondgun.c) has the entire `if (a0)` structure replaced by a single unconditional `modelSetMatricesWithAnim` per the B-246 r10 Phase B comment block.

### Why upstream's cache works correctly

The upstream cache-cold-fill at line 7915 captures the model's matrices at whatever animation state the gun is in **the very first time `bgun0f0a5550` is called for this weapon**. The "stale T-pose" failure mode in PD2 is contingent on the IDLE animation being authored at a non-canonical pose (PD2 / AllInOne uses anim 236 frame 17 for FALCON2 IDLE).

Original Perfect Dark gun authoring convention places IDLE at `anim 0 frame 0` (model T-pose), so the cache-cold-fill captures essentially the rest pose, and re-broadcasting it on every IDLE frame produces the correct visual state. The per-frame `mtx00015be4` then composes the live root matrix with the cached local-space matrices and the gun renders correctly.

PD2 / AllInOne idle authoring deviated from this convention. The cache captured a stale T-pose, IDLE rendered the gun at the model's local origin, FIRE rendered it at the live anim position. Mike observed the IDLE-vs-FIRE positional jump.

### Cross-cut conclusion

**Upstream's cache is correct under the original PD authoring convention. PD2's cache was incorrect under PD2 / AllInOne authoring. The B-246 r10 Phase B fix in PD2 is structurally correct and the bug was PD2-introduced via different anim authoring, not inherited from upstream.**

The fix did not need to land upstream because upstream's authoring did not exercise the divergence. If upstream ever adopts modded gun anim sets that put IDLE off `anim 0 frame 0`, they would need the same fix.

The Phase A predicate `bgunMatrixCacheIsStale` and its [pd-tests cases](port/src/bondgun_cache.c) remain in PD2 as a frozen specification for any future cache reintroduction.

---

## H. Known-good comparisons (PD2 matches upstream)

Functions where PD2 is structurally identical to upstream apart from PC-only modernisation strips (PLATFORM_N64, IS4MB, fourmeg2player). These are listed for completeness so the audit reader knows the comparison was thorough on these:

- `playerResetBond` -- [PD2 player.c:1892](src/game/player.c) vs [upstream player.c:1157](src/game/player.c). Per-life position/orientation reset, identical.
- `playersTickAllChrBodies` -- [PD2 player.c:1913](src/game/player.c) vs [upstream player.c:1178](src/game/player.c). Loop body identical.
- `playerSpawn` body up through the anti-spawn `for` loop (lines 1660-1770 PD2 / 985-1095 upstream). The chr-distance sort, on-screen rejection, force-spawn fallback are identical.
- `bgunTickMasterLoad` cache-fill site -- already covered in G.
- `bgunReset` mempAlloc + struct hand init defaults -- identical apart from the 4MB strip.
- `playerSpawnAnti` -- mostly identical capsule + spawn-pos lookup; the anti-side has no spawn-with-weapon path in either codebase.
- `playerInitEyespy` (in playerreset.c) -- identical apart from `STAGEINDEX_*` macro vs literal stage numbers.
- INTROCMD_SPAWN, INTROCMD_CASE, INTROCMD_AMMO, INTROCMD_OUTFIT, INTROCMD_WATCHTIME, INTROCMD_CREDITOFFSET handlers in `playerReset` -- structurally identical.

## I. Hypotheses for surveillance

These are flagged for Mike's awareness; not "bugs to fix today," but places where PD2's divergence could plausibly hide a latent issue. Mike picks which to chase, if any.

1. **Mode-leak of `g_MpSetup.options & MPOPTION_SPAWNWITHWEAPON`** (F-1 + F-5 + F-8 cluster). Audit every site that writes `g_MpSetup.options` and confirm the bit is cleared on transitions out of normal MP. If any path leaves it set going into Co-Op or Counter-Op, F-1 fires and F-5 / F-8 cascade.

2. **`mempAlloc` zero-fill assumption -- propagation pass on `struct player` and `struct gunctrl`** (F-4). The `visionmode=51503` and `handmodeldef=0x7c7b663545bbcbd1` precedents say PD2's mempAlloc returns un-zeroed memory. Every field of these structs that is read before being written is a latent bug. PD2 has fixed two so far (visionmode, handmodeldef/cartmodeldef). The other ~hundred-plus fields warrant a survey.

3. **Catalog-readiness invariant at `playerSpawn` time** (F-2). Confirm the asset catalog is fully populated for `MPWEAPON_*` enums by the time `playerSpawn` runs in any mode. The B-219 v2 fix narrative ("user selected `base:remotemine` but saw Falcon (Silenced)") is evidence that the catalog has returned wrong values in the past. A compile-time `_Static_assert` on `MPWEAPON_FALCON2 == 1` plus a runtime sanity check at `playerSpawn` entry would tripwire any future regression.

4. **B-181 fallback's silent options-mutation** (F-7). The OR-set on `g_MpSetup.options` mutates user state. Two specific concerns: (a) the user's explicit "spawn-with-weapon OFF" preference is silently overridden, (b) the mutation persists into the next match if `g_MpSetup` isn't reset on match end. **Worth checking** whether end-of-match cleanup includes resetting the options bit if it was forced ON by the fallback.

5. **GE-legacy weapon enums residual references** (F-9). Grep confirmation that no PD2 path can still produce `WEAPON_PP9I` etc. as a `weaponnum` value at the `playermgr.c` switch site.

6. **Bot-side modeldef preload asymmetry** (F-3). Surface for surveillance only; benign today. Worth flagging if bots are ever given per-bot loadouts.

---

## Summary

- **27 functions / sites compared** across player.c, playermgr.c, playerreset.c, bondgun.c, bondgunreset.c, setup.c, bot.c.
- **10 suspicious-divergence entries** (F-1 through F-10): 1 RESOLVED (F-6, matrix cache), 1 RULED-OUT (F-10, 4MB strip), 8 SPECULATIVE/LIVE.
- **Cross-cut on matrix cache (G)**: upstream has the same `unk0dd8` cache mechanism but its authoring convention (IDLE at anim 0 frame 0) makes the cache implicitly correct. PD2's authoring deviates, the cache becomes stale, and PD2's drop-the-cache fix (`cafbd2ef`) is the right answer. The bug was PD2-introduced, not upstream-inherited.
- **Top 3 hypotheses worth Mike's attention**, ranked by structural-suspicion (Mike can re-rank by his own priorities):
  1. **F-1 + F-5 + F-8 cluster** (mode-leak of MPOPTION_SPAWNWITHWEAPON): two asymmetric guards keying on different conditions; worst case is double-equip in Co-Op + Counter-Op.
  2. **F-4 + I-2** (mempAlloc zero-fill propagation): the `visionmode=51503` and `handmodeldef=garbage` precedents indicate a class of bugs; a survey pass over `struct player` / `struct gunctrl` is a high-leverage hardening opportunity.
  3. **F-7** (silent options-mutation in B-181 fallback): user explicitly set spawn-with-weapon OFF, engine flips it ON, persists across matches; a surveyable UX-vs-correctness issue.

No code changes proposed. Findings are framed as possibilities for Mike to triage.
