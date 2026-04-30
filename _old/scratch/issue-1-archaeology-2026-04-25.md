# Issue 1 — git archaeology (B-246, 2026-04-26)

Mike's report: weapon visible but not animating, fire input dead, wrong placement.
Affects BOTH SP campaign and MP combat sim. Working ~2026-04-15; broken now.

## Search-narrowing constraints (per Mike)

- **SP+MP shared scope**: regression must be in code paths that run in both
  SP campaign and MP, not MP-only. Eliminates connectivity / mpsetup / netmsg /
  matchsetup / botmgr-only commits.
- **Player-FP-only scope**: bot path works in both modes; regression specific to
  player FP rig. Eliminates bot tick / chrTick-shared-by-bot commits.
- **Intentional-but-incidental**: breaking commit's stated purpose may be
  unrelated; fallout to FP rig is the unintended consequence.

## WORKING_REF identified

`f6f41308` (2026-04-15, "chore: pre-release commit v0.0.103"). Latest commit on
04-15 with all gameplay paths in their stable pre-batch shape. Mike's
"a week and a half ago" maps cleanly to this date.

## Suspect-path commit survey (53 commits since WORKING_REF)

Filtering out: B-246 instrumentation (downstream of bug), auto-commits,
pre-release commits, Grid/Forge feature commits (introduce new paths, don't
refactor existing FP).

### Real intentional changes touching player-FP / input shared paths

| commit | date | scope | verdict |
|--------|------|-------|---------|
| `4a6382cf` | 04-17 | N64 legacy strip (IS4MB / IS8MB / fourmeg2player / STAGE_4MBMENU) — bondgun.c -32 / lv.c -57 / player.c -58 lines | **Semantic-preserving**. IS4MB() = compile-time 0 on PC, IS8MB() = compile-time 1. All `IS4MB() && X` reduces to false; all `IS8MB() \|\| X` reduces to true. Net no-op for PC behaviour. |
| `7838d847` | 04-17 | S320/S323 dead code purge (empty stub `playerResetLoResIf4Mb`, unused `g_BgunGunMemBaseSize4Mb2P`) | **Dead-code only**. Zero live consumers. |
| `75240823` | 04-19 | fileLoad migration (Phase 3 AP) | Touched bondgun.c via fileLoad call sites, but the migration kept identical semantics. Ruled out by call-graph trace. |
| `c960d7b8` | 04-19 | hold X = interact, tap X = reload — adds `actionHeldForMs` / `actionWasTap` / `actionConsumeHold` helpers + bondmove `BMOVE_USE_HOLD_THRESHOLD_MS` | **Additive**. Adds new helpers, doesn't change FIRE_PRIMARY path. ACTION_USE / X_BUTTON only. |
| `bd9a07d7` | 04-20 | PC input + interact prompt — B-209 numsamples clamp, B-203 D-pad fire-mode binds, S397 ImGui interact-prompt overlay gate | **Localized fixes**. B-209 fixes a starvation bug (`numsamples=0` blocked reload/use), doesn't break fire. B-203 cleans up a dual-bound D-pad VK; doesn't touch FIRE_PRIMARY. |
| `fb5edd6d` | 04-20 | menu input docs + hold ring + hoverbike USE tap/hold | UI docs + hoverbike-specific. Doesn't gate FP fire. |
| `d3247e7e` | 04-21 | B-217..B-222 stability batch | Touches lv.c bot tick + activemenu + interact prompt. None gate FP-render. |
| `acb1baa4` | 04-25 | Priority J-1 IMC plumbing (Mission + CombatSim IMCs) | **Pure additive per commit message** ("nothing activates the new IMCs in this commit"). The activation came in J-1d (`5dc362c2`). Both gameplay and Mission/CombatSim IMCs are active simultaneously; `fireVk` finds FIRE_PRIMARY in gameplay IMC. Should still work. |
| `872e4907` | 04-25 | B-259 cursor authority (NoMouseCursorChange) | **Cursor-only**. Doesn't gate input. |
| `72ab582a` | 04-16 | Phase 2 input-authority menupool dedup | Adds menupool init in `inputCtxInit`. Doesn't change `gameplayInputSuppressed` or `actionHeld` semantics. |

### Files unchanged in shared paths since WORKING_REF

- `playerRenderHud` body — IDENTICAL at WORKING_REF and HEAD (apart from
  round-5 diag). cameramode early-return logic unchanged.
- `lvRender` cascade — `lockscreen` / `var8009dfc0` / `playerRenderHud` chain
  IDENTICAL at WORKING_REF and HEAD.
- `var8009dfc0` writers — unchanged.
- `cameramode` writers — unchanged.
- `gameplayInputSuppressed` body — unchanged.
- `bgunRender` body — unchanged apart from B-246 instrumentation block.
- `bgunTickGameplay2` body — unchanged.
- `actionHeld` body — unchanged.

## Cross-reference vs. round-5 diagnostic targets

The round-5 diag (commit `292e87ea`) instruments three choke points:
1. `lvRender` cascade decision (lockscreen / menu-render / normal-with-playerRenderHud).
2. `playerRenderHud` branch (THIRDPERSON early-return / EYESPY skip / fp_render path).
3. `bondmove` c1buttons gather — `actionHeld(0, FIRE_PRIMARY)` value vs `c1buttons & shootbuttons`.

If archaeology had surfaced a clear single-commit revert candidate, we'd revert
+ verify. It did not. None of the surveyed commits show a polarity flip,
condition removal, or refactor that would explain `bgunRender` becoming silent
or `actionHeld(FIRE_PRIMARY)` returning 0 in MP.

The round-5 diagnostic IS the next step. One playtest log will pinpoint the
exact gate without further static-analysis speculation.

## Spawn-weapon assignment chain — NOT broken

Verified the weapon-assignment chain is intact in current build:
- `MATCHSETUP: spawn weapon ... → weaponnum=N` ✓
- `bgunEquipWeapon2 enter player=0 hand=0 req_wpn=N` ✓
- `bgunEquipWeapon2 exit ... switchto=N` ✓
- State machine completes: `R(state=0 sm=0 cnt=N inuse=1)` ✓
- `gunctrl_wpn=N` post-switch ✓

Mike's secondary concern ("what weapons were actually being sent") is
already-fixed in current build. The remaining bug is FP-render + fire-input.

## Hypothesis space (post-archaeology, pre-round-5-results)

Three failure-mode candidates for `bgunRender` silent + `triggeron=0`:

**H1 — `cameramode` stuck at THIRDPERSON.** PlayerTick TICKMODE_NORMAL
branch sets it to DEFAULT, but maybe a different path sets THIRDPERSON
late. round-5 `playerRenderHud branch=` will show cameramode value at
render time.

**H2 — `var8009dfc0` stuck at 1.** Menu render path stealing the
gameplay frame. Round-5 `lvRender cascade=` will show which branch fires.

**H3 — `actionHeld(FIRE_PRIMARY)` returning 0 despite Mike pressing
fire.** Either `gameplayInputSuppressed` is true in a way the DIAG poll
doesn't capture, or the gameplay IMC binding is being shadowed by a
higher-priority IMC. Round-5 `fire-input pi=0 fire_held=` will show
the actionHeld return value directly.

## Recommendation

**Don't revert anything.** Archaeology surfaced no clear revert candidate.
The round-5 diagnostic (already shipped at `292e87ea`) is the right next
step. Mike's next playtest log produces three new diag lines per second;
the gate becomes visible in 5-10 seconds of MP gameplay. Round-6 ships
the targeted fix once the gate is identified.

If Mike's next playtest log shows `playerRenderHud branch=fp_render`
firing AND `bgunRender enter` STILL silent, the gate is inside `bgunRender`
itself or its caller `bgunTickGameplay2` — re-instrument those.

If the log shows `playerRenderHud branch=thirdperson_skip`, hypothesis H1
confirmed and the fix is pinning `cameramode = DEFAULT` more aggressively
in MP-and-SP gameplay paths.

If the log shows `lvRender cascade=` other than "normal", the gate is
upstream — `var8009dfc0` stuck (menu state leak) or lockscreen stuck.

If `fire-input pi=0 fire_held=0` while Mike is actively pressing fire,
the gate is in the action map / IMC layer (`gameplayInputSuppressed` or
the gameplay-IMC suppression in `fireVk`).
