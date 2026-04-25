# Player FP weapon-model lifecycle trace -- 2026-04-25

**Goal:** find the first broken step in the player's FP weapon-model lifecycle. Bot weapons work (per Mike's clarification). Player FP rig is broken: no FP arms / weapon visible, fire dispatch arrives but produces nothing, can't pickup.

## Match-start init order (`lv.c:602-628`)

For each player slot:
1. `menuReset()`
2. `amReset()` — closes active menu
3. `invReset()` — inventory cleared
4. `bgunReset()` — gun state reset
5. `playerLoadDefaults()`
6. `playerReset()` — player state defaults

Then once for all:
7. `mpOrchestrateMatchStartSpawns()` (placement)

Then for each player slot:
8. `playerSpawn()` — auto-equips spawn weapon via `bgunEquipWeapon2(HAND_RIGHT, weaponnum)`
9. `bheadReset()`

The spawn log line `SPAWN: player 0 spawned with weapon 19 (Shotgun) -- auto-equipped to right hand` comes from the `bgunEquipWeapon2` call inside `playerSpawn`.

## Step 1: initialization (where FP weapon model is FIRST instantiated)

There is **no single "init the FP weapon" function**. The FP weapon model lives inside a shared **gunmem pool** (`player->gunctrl.gunmem`, allocated in `bgunReset` via `mempAlloc`). The pool is owned by one of:

- `GUNMEMOWNER_BONDGUN` — owned by the FP rig (loaded weapon model + hands)
- `GUNMEMOWNER_CHRBODY` — owned by the player's third-person chr body's hand-attached weapon
- `GUNMEMOWNER_INVMENU` — owned by the inventory menu's weapon preview
- `GUNMEMOWNER_FREE` — reclaimable
- `GUNMEMOWNER_3` — unused
- `GUNMEMOWNER_CHANGING` — mid-transition

After `bgunReset` (`bondgunreset.c:153`), `gunmemowner = GUNMEMOWNER_CHRBODY`. The pool is initially earmarked for the third-person body, NOT the FP rig.

## Step 2: set-to-specific-weapon (transition for FP rig)

`bgunEquipWeapon2(HAND_RIGHT, 19)` at spawn:
1. Calls `bgunEquipWeapon(weaponnum)` (`bondgun.c:5604`).
2. Sets `gunctrl.switchtoweaponnum = weaponnum`. Does NOT load the model. Just queues the request.

The actual load is driven by the per-frame state machines:

### Per-hand state machine: `bgunTickHand(handnum)` at `bondgun.c:3275`

**Critical gate at `bondgun.c:12083`:**
```c
if (g_Vars.tickmode == TICKMODE_NORMAL && g_Vars.lvupdate240 > 0) {
    bgunTickHand(HAND_RIGHT);
    bgunTickHand(HAND_LEFT);
    bgunTickSwitch();
    ...
}
```

**The hand state machine does NOT tick during `TICKMODE_MPSWIRL`** (the ~2-second match-start swirl intro). Per Mike's log timeline (`019dc2d5`), the player is in MPSWIRL for the first ~2.4s of the match. During that window, hand state stays at `HANDSTATE_IDLE` from `bgunInitHandAnims`.

Once MPSWIRL ends:
- `bgunTickHand` runs per frame.
- Hand state IDLE → CHANGEGUN (in `bgunTickIncIdle` at `bondgun.c:1227` because `bgunIsReadyToSwitch` returns true when `switchtoweaponnum >= 0`).
- CHANGEGUN progresses through stateminor stages 0 → 1 → 2 → CHANGEGUN_LOAD (count ≥ 3 required for the next gate).
- Both hands must reach CHANGEGUN_LOAD with count ≥ 3 for `bgunCanFreeWeapon` to return true (see `bondgun.c:2799-2811`).

### Switch state machine: `bgunTickSwitch2` at `bondgun.c:5457`

```c
if (ctrl->switchtoweaponnum >= 0) {
    if (bgunCanFreeWeapon(HAND_RIGHT) && bgunCanFreeWeapon(HAND_LEFT)) {
        ...
        bgunSetGunMemWeapon(ctrl->switchtoweaponnum);  // sets gunmemnew = weaponnum
        ctrl->weaponnum = ctrl->switchtoweaponnum;
        lefthand->inuse = true;     // <-- KEY: this is the "FP rig is alive" signal
        righthand->inuse = true;
    }
}
```

**`hand->inuse = true` is the key transition**: it's the signal that the FP rig has bound a weapon. Until this fires, `hand->visible` is forced false at the render gate (`bondgun.c:7708`):

```c
if (... || hand->inuse == false || ...) {
    hand->visible = false;
}
```

## Step 3: load (asset fetch)

After `bgunTickSwitch2` sets `gunmemnew = weaponnum`, the master load state machine takes over:

### `bgunTickMasterLoad` at `bondgun.c:4000`

```c
if ((player->gunctrl.gunmemowner == GUNMEMOWNER_BONDGUN
     || bgunChangeGunMem(GUNMEMOWNER_BONDGUN))
    && player->gunctrl.gunmemnew >= 0) {
```

Two preconditions:
1. **Pool ownership**: must be BONDGUN, OR `bgunChangeGunMem` must succeed in switching from CHRBODY → BONDGUN.
2. **Queued weapon**: `gunmemnew >= 0`.

`bgunChangeGunMem(GUNMEMOWNER_BONDGUN)` (`bondgun.c:3703`) takes 3 frames to complete the transition (gunlocktimer counts -1 → -2 → -3 with `gunmemowner = CHANGING` mid-state). The CHRBODY → BONDGUN unlock case at line 3733 fires `unlock = true` if `mplayerisrunning` is true OR `!haschrbody`. **For Mike's CS combat sim repro, `mplayerisrunning = true` so the unlock condition is satisfied immediately.**

After the transition, `bgunTickMasterLoad` runs through MASTERLOADSTATE_FLUX → MASTERLOADSTATE_HANDS → MASTERLOADSTATE_LOADED, loading the gun model + hands + textures incrementally. Once `bgunIsLoaded()` returns true and `hand->inuse == true`, the FP weapon should render.

## Step 4: fire chain

`bgunTickGameplay` at `bondgun.c:11879` (instrumented in B-246):
1. If `passivemode`, force `triggeron = false`.
2. If `tickmode == TICKMODE_CUTSCENE`, force `triggeron = false`.
3. Else `playertriggeron = triggeron`.
4. Per-hand processing: if `hand->triggeron && info->weaponnum != WEAPON_NONE`, fire (`bondgun.c:1316`).

Fire requires:
- `triggeron = true` (driven from `bondmove.c:2207` if `c1buttons & Z_TRIG` AND `!waitforzrelease` AND `pausemode == UNPAUSED`)
- `hand->weaponnum != WEAPON_NONE` (set by `bgunTickSwitch2` via `bgunSetGunMemWeapon`)
- `hand->state` not in a state that locks out fire (e.g. CHANGEGUN, RELOAD)
- `hand->inuse == true` (gates `hand->visible` AND many other branches)

## Where the chain could break (candidates for the playtest log to disambiguate)

1. **`switchtoweaponnum` never gets set.** Defends: the spawn log explicitly says `auto-equipped to right hand` so `bgunEquipWeapon2` ran. This step looks fine.
2. **Hand state machine stays in IDLE forever.** Would happen if `tickmode != TICKMODE_NORMAL` indefinitely. After MPSWIRL ends, tickmode = NORMAL per the log; but if some path resets tickmode back to MPSWIRL or to a different mode without restoring, the hand state machine freezes.
3. **One hand never reaches CHANGEGUN_LOAD with count ≥ 3.** Both hands gate `bgunTickSwitch2`. If the LEFT hand somehow gets stuck in a different state (because of `dualwielding` flags, an inherited state from the previous match, or a misordered cycle), CHANGEGUN_LOAD never fires.
4. **`bgunChangeGunMem(GUNMEMOWNER_BONDGUN)` never succeeds.** Would happen if `mplayerisrunning` is somehow false in CS (unlikely; the log shows `normmplay=1`). Or if some other case in the switch is hit.
5. **`bgunTickMasterLoad` loads but `hand->inuse` is reset elsewhere.** Some other path zeroes `inuse` after the switch fires.
6. **`gunmemnew` is set but immediately cleared.** Lines 4211 and 4218 of `bondgun.c` both set `gunmemnew = -1` — if one of those fires after `bgunTickSwitch2` sets it, the master load aborts.

## Why bot weapons work

Bots don't use the player's `gunctrl` system. Bot chrs get their weapon models attached via `playerTickChrBody` (`player.c:2348-2365`) which calls `weaponCreateForChr(chr, weaponmodelnum, weaponnum, 0, weaponobj, weaponmodeldef)`. In MP mode (`mplayerisrunning == true`), the modeldef passed in is NULL — but `weaponCreateForChr` likely uses a globally-cached or chr-side modeldef in that branch. Either way, this is a **completely separate code path** from the player's FP rig (`bgunTickHand` / `bgunTickSwitch2` / `bgunTickMasterLoad`). The fact that bots work proves only that the chr-side third-person attachment is fine, not that the player's FP path is fine.

## Disambiguation plan

The B-246 instrumentation already covers each gate. When Mike's playtest log arrives:

- `LOG.WPN.DIAG: bgunEquipWeapon2 enter ... cur_gunctrl_wpn=N switchto=N` — confirms step 2 fires.
- `LOG.WPN.DIAG: bgunRender enter ... R(wpn=19 visible=0/1 inuse=0/1 state=N)` for first 5 ticks — directly shows whether `inuse` ever flips to true in the first 5 ticks (it shouldn't yet during MPSWIRL; it should within seconds after).
- `LOG.WPN.DIAG: fire handler enter ... triggeron=N R(wpn=N inuse=N visible=N state=N)` — at the moment Mike presses fire, snapshots whether the FP rig is alive.

If `bgunRender` never logs because `playerRenderHud` early-exits (e.g. cameramode unexpectedly THIRDPERSON or EYESPY), that's a different break.

If `bgunRender` logs `inuse=0` indefinitely after MPSWIRL, the hand state machine isn't reaching CHANGEGUN_LOAD on both hands. Add a probe at `bgunTickSwitch2` entry to log `bgunCanFreeWeapon` results per hand for the first ~30 frames after MPSWIRL ends.

If `inuse=1` but `visible=0`, the masking gate (`!bgunIsLoaded() || gunmemtype == 0`) is the issue — the load never finished. Probe `bgunTickMasterLoad` state (`masterloadstate`, `gunloadstate`).

## Status

Static trace exhausted of high-leverage targets. The architecture is well-defined; each gate is reasonable. Without runtime evidence of which gate is failing, picking one to "fix" risks a fix that doesn't address the actual bug or introduces a regression. Mike's playtest of build `6a9a23d8` (B-246 instrumentation) will produce the log that pins this in one round.

If there is headroom and Mike wants speculative defensive hardening, the candidates with cleanest blast radius are:

- **Bypass MPSWIRL gating for the hand state machine on first match frame.** The 2.4s freeze is visually intentional (camera swirl) but the gun state machine doesn't need to be frozen; if hand state ticked during MPSWIRL, the FP weapon would be ready slightly sooner. Risk: changes load timing on every match start; low-but-not-zero chance of breaking the swirl visuals. Defer until evidence shows MPSWIRL is the bug.
- **Force `gunmemowner = GUNMEMOWNER_BONDGUN` at end of `bgunReset` for MP matches.** Removes the CHRBODY → BONDGUN transition cost. Risk: chr-body weapon-attach for the player's third-person mirror might lose its modeldef pool. Defer until evidence shows the transition is the bug.

**Neither speculative fix is recommended without the runtime log.** The instrumentation is the right next step.
