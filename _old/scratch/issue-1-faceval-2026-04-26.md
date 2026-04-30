# Issue 1 — face-value triage from round-5 playtest log (2026-04-26)

Methodology: enumerate candidate causes per observation BEFORE picking one.
Map each to actual log evidence as RULED-OUT or LIVE. Pre-cooked hypotheses
from earlier framings (FP-vs-TP, IMC, cameramode-stuck) are explicitly held
back from interpretation in this pass.

Log: `~/Downloads/Perfect Dark 2.0/pd-client.log`, 2026-04-26 13:27, 3.9 MB,
768 LOG.WPN.DIAG entries.

## Raw log data — observations only, no interpretation

### `lvRender cascade=` — 140 firings
- ALL value: `normal (playerRenderHud about to fire)`
- `var80075d60=2 lockscreen=0 var8009dfc0=0` consistent
- NO firings of any other branch (lockscreen / menu-render not taken)

### `playerRenderHud branch=` — 140 firings
Four unique state combos:
1. `fp_render cameramode=0 visionmode=0 haschrbody=0 tickmode=1 normmplay=0` (SP gameplay solo)
2. `fp_render cameramode=0 visionmode=0 haschrbody=1 tickmode=1 normmplay=1` (MP gameplay)
3. `thirdperson_skip cameramode=1 visionmode=51503 haschrbody=1 tickmode=4 normmplay=1` (MP swirl intro)
4. `thirdperson_skip cameramode=1 visionmode=51503 haschrbody=1 tickmode=6 normmplay=0` (SP cutscene/loading)

Counts: `fp_render` 78, `thirdperson_skip` 62.

In MP TICKMODE_NORMAL: branch is `fp_render` consistently. cameramode=0,
haschrbody=1, visionmode=0.

### `fire-input pi=0` — 78 firings
- Most: `fire_held=0 fire_pressed=0`
- 3 firings with `fire_held=1`: frames 509, 1529, 1589 (within MP gameplay)
- All: `allowc1=1`

### `fire handler enter` (rising-edge) — 18 firings
Two unique right-hand state combos in MP:
- `R(wpn=22 inuse=1 visible=1 state=0)` — pre-fire idle
- `R(wpn=22 inuse=1 visible=1 state=4)` — HANDSTATE_ATTACK during fire animation

`gunctrl_wpn=22` (WEAPON_FARSIGHT) consistently across all 18.
`triggeron=1, switchto=-1, passive=0, tickmode=1, bondmovemode=0, ctrl=1` consistently.

### `bgunTickGameplay enter` — 263 firings
In MP at 01:33+ (frames 148-1516+):
- `tickmode=1, lvupdate240=2, gunctrl_wpn=22, switchto=-1`
- R-hand cycles state=0 (idle) ↔ state=4 (attack) on fire presses
- `triggeron_in` toggles 0 ↔ 1 on fire presses

### `bgunRender enter` — 0 firings
Note: this diag is gated on `lvframe60 < 5` (per-stage frame), only fires
in first 5 frames of a stage. Silence here is consistent with the diag
gate being too restrictive, NOT necessarily with bgunRender being skipped.
The `branch=fp_render` evidence above implies `bgunRender(&gdl)` IS called
(it's the very next statement after `bgunTickGameplay2()` in playerRenderHud
when the EYESPY check passes).

### `visibility-gate-fail` — 5 firings
All in SP CI at 00:53.78-00:58.61 (frames 3011-3491) during weapon switch:
- `wpn=3 mode6=1 mode7=0 notLoaded=1 notInuse=0 memType0=1`
- `hand_mode=6 gunmemowner=10 gunmemtype=0 gunmemnew=3 masterload=0`
- `gunmemowner=10 = GUNMEMOWNER_CHANGING` (master loader transitioning ownership)

Resolved by 00:59. NOT firing in MP gameplay (no MP-side visibility gate failures sampled).

### `LOAD-mode6` — 5 firings
Same time window as visibility-gate-fail (00:53-00:58, SP CI weapon switch):
- `hand_mode=6 sm=2 state=5 cond_loaded=0 cond_gunmemnew_neg=0 bgun0f09bf44=0`

State machine briefly wedged in CHANGEGUN/LOAD/MODE_6 during SP weapon
switch. Resolved within the diag sampling window.

### `playerSpawn` — 4 firings
Last (01:31.41, MP entry): `haschrbody=1 gunctrl_wpn=0 switchto=22 R(wpn=0 inuse=0 visible=0)`

### `bgunEquipWeapon` — 16 firings
Multiple req_wpn values across SP+MP (1, 3, 22, 0).

### `bgunTickSwitch` — 7 firings
### `pickup probe` — 71 firings
### `playerChooseBodyAndHead` — 21 firings

## Mike's verbatim observations

1. "I could fire the weapon"
2. "no hands visible"
3. "the weapon seemed to jump from the wrong spot to the correct spot when firing then back to the wrong spot"

## Observation 1: "could fire the weapon" — what it tells us about subsystems

Candidate causes for fire-works:
- a. Input dispatch reaches fire handler → CONFIRMED LIVE (`fire handler enter` fires 18×, `triggeron_in=1` on fire frames in `bgunTickGameplay` diag).
- b. State machine transitions to HANDSTATE_ATTACK → CONFIRMED LIVE (`R(state=4)` observed in fire handler diag).
- c. Damage system fires → UNKNOWN from log (no chrDamage diag from Mike's fire visible in this log; he may not have hit anything).
- d. Sound trigger fires → UNKNOWN from log (no sound-trigger diag).
- e. Projectile spawn → UNKNOWN from log.

What "fire works" rules out: the entire input → trigger → state-machine path
is functional. Subsystems downstream of state=4 entry are either confirmed
working (a, b) or unknown (c, d, e — no diag evidence).

## Observation 2: "no hands visible" — candidate causes

Enumerated without picking:
- a. **FP arms model not loaded.** `player->gunctrl.handmodeldef == NULL` because `weaponHasFlag(weaponnum, WEAPONFLAG_HASHANDS)` returned false at master-load time, so `MASTERLOADSTATE_HANDS` skipped the model load (bondgun.c:4137).
- b. **FP arms loaded but draw call skipped.** `bgun0f0a5550` line 7886-7888 conditionally calls `bgunExecuteModelCmdList(hand->unk0dd0)` only when `player->gunctrl.handmodeldef != NULL`.
- c. **FP arms loaded and drawn but at zero scale or off-screen.** `mtxallocation` math producing degenerate transforms.
- d. **Camera looking the wrong direction so they're behind the camera.** vv_theta / vv_verta projecting them outside frustum.
- e. **Render order putting something opaque in front of them.** Z-buffer / blend ordering.
- f. **Condition flag (visibility, alpha=0, hidden bit) set.** `hand->visible=false` would also hide gun (which Mike can see), so this rules out itself for this observation. Per-hand `chrflags` or `hidden` on the chr-body's hand subnode.
- g. **Hands rendered correctly but transparent material.** Texture / material alpha.
- h. **`weaponnum == WEAPON_FARSIGHT` is hands-less by design** (FarSight may be a no-hands sniper-scope rendering).
- i. **`handfilenum` returns 0 from `catalogGetBodyHandFilenum(bodynum)`** (catalog returned no hand filenum for Mike's body 'base:dark_combat').
- j. **Confusion of FP-arms with chr-body's hands** — Mike's "hands" may refer to chr body's third-person hands (different rig from FP arms).

Map to log evidence:
- Mike's fire-handler diag shows `R(wpn=22 visible=1 state=0/4)`. `visible=1` rules OUT (f) for the gun-side gate. Doesn't rule out (b) since handmodel is a separate command list inside the same `if (visible)` block.
- `gunctrl_wpn=22` = WEAPON_FARSIGHT consistently in MP. (h) is a candidate worth direct verification.
- `playerSpawn ... model00d4=0x000277...7f508 haschrbody=1` shows third-person body model exists in MP. Doesn't say anything about FP arms.
- No diag captures `handmodeldef` value or `unk0dd0` command list state.

Live candidates (not ruled out by log): a, b, c, d, e, g, h, i, j.
Ruled out: (f) for the gun-side visible flag.

## Observation 3: "weapon jumps wrong → correct on fire → back to wrong" — candidate causes

Enumerated without picking:
- a. **Two different weapon-render code paths**, each running at different frequencies (default vs fire-event triggered).
- b. **One render path with a per-frame transform that resets between two values**, depending on a state flag that toggles with firing.
- c. **Animation track snapping between keyframes** (idle anim's bind-pose vs attack anim's first frame have very different transforms).
- d. **Multiple draw commands for the same weapon**, only one of which has the right transform; one is conditionally suppressed during fire.
- e. **A compositor swapping which view contributes to the final frame on fire events.**
- f. **`hand->adjustpos` or `hand->damppos` toggling between zero and non-zero** based on a fire-state flag, applied as additive offset to the per-frame position calc (bondgun.c:7749-7761).
- g. **`hand->firing && shootfunc->recoilsettings` random-offset block** at bondgun.c:7770-7774 only firing during attack state — but this adds a random jitter, not a discrete jump.
- h. **`weapondef->aimsettings->guntransX` direction-of-aim cross-screen offset** at bondgun.c:7776-7782 dependent on `crosspos2` which moves with aim.
- i. **`guncloseroffset` zoom-FOV offset** at bondgun.c:7763-7764 toggling on fire.
- j. **`bgunUpdateGangsta` modifier** at bondgun.c:7900 applying only during a specific weapon flag state (FarSight may have GANGSTA flag interaction).
- k. **`useposrot` branch** at bondgun.c:7903-7908 applying a different posrotmtx when set.
- l. **FarSight scope-toggle behavior**: FarSight's primary fire on PD activates the scope, which moves the weapon from hip-position to scope-position. Reverting to hip when scope deactivates. Could match "wrong → correct → wrong" if Mike's "fire" is actually the scope-toggle press, not the actual shot.
- m. **Two weapons rendered simultaneously at different positions** (chr body's hand-bone weapon AND FP viewmodel), with one becoming more visually prominent during fire animation.

Map to log evidence:
- `gunctrl_wpn=22 = WEAPON_FARSIGHT` consistently. (l) — FarSight scope-toggle behavior — is a direct candidate worth verifying because it WOULD match the symptom by design.
- Fire handler diag shows state cycling 0 ↔ 4. Rate-of-cycle aligns with Mike pressing fire repeatedly. Doesn't pin (a) vs (b) vs (l).
- `branch=fp_render` runs every frame consistently in MP — rules OUT (a) "two different render paths at different frequencies" if "different frequencies" means once-per-frame vs only-on-fire.
- No diag captures position values or per-frame `damppos` / `adjustpos` / `firing` / `useposrot` to pin (b, f, g, k).

Live candidates: a (in modified form — two paths SAME frequency but different conditions), b, c, d, e, f, g, h, i, j, k, l, m.
Ruled out: a-strict (different frequencies — render path is per-frame).

## Cross-observation note

Observation 2 (no hands) and Observation 3 (weapon position toggle) may be
INDEPENDENT phenomena or coupled. The log evidence does not pin either
direction. Specifically:
- If Mike's weapon is FarSight and FarSight is no-hands by design (cand 2h),
  AND FarSight scope-toggle moves the weapon (cand 3l), both observations
  reduce to "FarSight working as designed in MP" — which would mean Issue 1
  is actually closed for non-FarSight weapons but unverified.
- If Mike's "no hands" is a generic FP-arms missing across all weapons (cand
  2a/b/i), it's a separate bug from the position toggle.

Mike's earlier playtests (1.5 weeks ago = working) included FP arms visible
on FarSight per his statement that "spawn weapon was working a week and a
half ago." So if FarSight has always been hands-less by design, his prior
working state would also have been hands-less and his observation wouldn't
note it as a regression.

## Surface to user

Live candidates per observation, mapped above. No winner picked. No fix
proposed. No round-6 instrumentation proposed. Awaiting Mike's input on:
1. What weapon was equipped during the playtest (FarSight per `gunctrl_wpn=22`,
   but weapon-switch behavior may have been different earlier in his session).
2. Whether "no hands" was specifically NEW vs always-been-this-way for FarSight.
3. Whether the position toggle correlates with EVERY fire press, only first
   press of a burst, only on FarSight, or other weapons.

These three questions narrow the candidate list significantly without
needing more instrumentation.
