# Issue 1 — Cursor-author archaeology (B-246, 2026-04-26)

Mike's hypothesis: regression introduced by Cursor (his fallback AI tool when
Anthropic rate-limits). All commits in window authored by Mike per `git log
--author`; discriminator is the trailer (`Made-with: Cursor` vs
`Co-Authored-By: Claude`).

## Cursor-trailer commits in window (`f6f41308`..HEAD)

Five commits found via `git log --grep="Made-with: Cursor"`:

| commit | date | files touched in player-FP / input scope |
|--------|------|-------------------------------------------|
| `35a8daaf` | 2026-04-16 | matchsetup, scenario_save, modmgr, assetcatalog_scanner, room — MP-only |
| `bd9a07d7` | 2026-04-20 | bondmove.c, actionmap.cpp, pdgui_backend.cpp, pdgui_interact_prompt.cpp |
| `48ed7f4e` | 2026-04-20 | lv.c, spawnpool, activemenu, pdgui_menu_forge, pdgui_menu_moddinghub, pdgui_menu_mainmenu, modmgr |
| `fb5edd6d` | 2026-04-20 | bondmove.c, actionmap.cpp, propobj.c, prop.c, pdmain.c, pdgui_glyphs, pdgui_hold_ring, pdgui_interact_prompt, pdgui_menu_mainmenu |
| `d3247e7e` | 2026-04-21 | bondmove.c, playerreset.c, actionmap.cpp, propobj.c, bot.c, mpspawn_orchestrate, modmgr, pdgui_backend, pdgui_interact_prompt, pdgui_menu_mpingame |

## Per-commit ruling

### `35a8daaf` — MP bot cap and team defaults — RULED OUT

MP-only (matchsetup / scenario_save / room). SP+MP shared scope eliminates
this. SP campaign is unaffected.

### `bd9a07d7` — PC input + interact prompt + April 20 context — RULED OUT

Three localized fixes:
- B-209: `numsamples = 1` clamp on PC when `joyGetNumSamples()` returns 0.
  Fixes a starvation bug in reload/use; doesn't touch FIRE_PRIMARY path.
- B-203: D-pad fire-mode binding cleanup. Removed dual-bind on JBTN_DPAD_RIGHT
  (was `ACTION_DPAD_RIGHT` + `ACTION_FIRE_MODE`); kept `ACTION_FIRE_MODE` only.
  No effect on FIRE_PRIMARY.
- S397: `interactPrompt` added to `pdguiNewFrame` run-condition so the prompt
  draws when label is non-NULL. Activates ImGui; doesn't suppress input.

FIRE_PRIMARY binding unchanged. `triggeron` formula unchanged. `gameplayInputSuppressed`
unchanged. `actionIsGameplayOnly(FIRE_PRIMARY) == 1` unchanged.

### `48ed7f4e` — Apr20 stability batch (lv cap, spawn AABB, UI, mod catalog, Grid) — RULED OUT

`lv.c` change: caps `g_Vars.lvupdate240` at `LV_UPDATE240_CATCHUP_CAP` (= TICKS(8) = 32).
Cap only fires on extremely long catch-up frames (typical lvupdate240 = 2-8). Doesn't
affect normal gameplay's render/tick chain. spawnpool/activemenu/menu changes don't
touch FP-render or fire-input.

### `fb5edd6d` — menu input docs, hold ring, hoverbike USE tap/hold — RULED OUT

Major stick-tuning rewrite in actionmap.cpp (242 lines): per-stick deadzone +
sensitivity, radial 2D deadzone applied. Also added `propGetActionUseHoldThresholdMs()`
+ per-action hold overrides. None of these lines touch FIRE_PRIMARY / fireVk /
gameplayInputSuppressed / actionIsGameplayOnly. ACTION_USE tap/hold logic is
ACTION_USE-only; FIRE_PRIMARY uses a different code path.

`bondmove.c` change: ACTION_USE synthesizes A_BUTTON / X_BUTTON differently
on tap vs hold. ACTION_FIRE_PRIMARY -> Z_TRIG path at line 988 unchanged.

`propobj.c` / `prop.c` / `pdmain.c` changes: hoverbike-specific. Ruled out for
SP campaign FP-rig regression.

Adds `u8 pcinteractusekind` to `struct player`. Additive struct field at end;
no offset shifts to `cameramode` (offset 0x0000) or `haschrbody` (offset
0x19c8). Verified in `src/include/types.h` cumulative diff.

### `d3247e7e` — B-217 through B-222 stability batch — RULED OUT

Six bug fixes bundled:

- **B-217**: Bot tick freeze when `g_BotUpdatesDisabled`. Original returned
  TICKOP_NONE; superseded by B-217 v2 (2026-04-23) which zeroes
  `speedmultforwards/sideways` then still calls chrTick. Bot-only; doesn't
  touch player.
- **B-218**: Orchestrator pad cache duplicate detect at bot spawn. Bot-only.
- **B-219**: MP `INTROCMD_WEAPON` skip when `normmplayerisrunning &&
  SPAWNWITHWEAPON`. Gated on `normmplayerisrunning` -> SP campaign falls
  through to normal processing. SP unaffected.
- **B-220**: Modmgr fsFileSize guard before fsFileLoad. File-IO; not in
  scope.
- **B-221**: Hold ring + hoverbike tap/hold polish. ACTION_USE-only.
- **B-222**: pdgui_menu_mpingame killfeed gating. UI-only.

`actionmap.cpp` change: adds `hold_vis_grace_until_ms` /
`hold_pin_full_until_ms` / `hold_vis_last_down_progress` fields to
`ActionState`. Cleared in `actionmapFlushGameplayState` and `fireVk`.
These are pure visual-progress book-keeping for the hold ring; do not gate
input dispatch. ACTION_SCORECARD added to `actionIsGameplayOnly` shared
list (returns 0). Doesn't affect FIRE_PRIMARY (still gameplay-only).

`bondmove.c` change: 13-line block at line 2220 area for B-221.3
"classify short USE as tap on release, then mount/grab hoverbike". USE-only.

`playerreset.c` change: B-219 INTROCMD_WEAPON skip (MP-only, see above).

## Cumulative diff verification (WORKING_REF -> HEAD)

Specifically checked these critical paths in cumulative `git diff`:

- `bondmove.c::triggeron` formula (lines 2207-2213): IDENTICAL string-for-string.
- `actionmap.cpp::fireVk` gameplay-IMC suppression: IDENTICAL.
- `actionmap.cpp::actionHeld` body: IDENTICAL.
- `actionmap.cpp::gameplayInputSuppressed` body: UNCHANGED.
- `actionmap.cpp::actionIsGameplayOnly(FIRE_PRIMARY)`: UNCHANGED (returns 1).
- `lv.c::lvRender` lockscreen / var8009dfc0 / playerRenderHud cascade: UNCHANGED.
- `player.c::playerRenderHud` cameramode early-return: UNCHANGED.
- `player.c::playerTick` TICKMODE_NORMAL branch: UNCHANGED.
- `bondgun.c::bgunRender` body: UNCHANGED apart from B-246 instrumentation.
- `bondgun.c::bgunTickGameplay2` body: UNCHANGED apart from B-246 instrumentation.

## Verdict

**No smoking-gun Cursor commit identified.** All five Cursor commits in the
window are localized to ACTION_USE / hoverbike-tap-hold / hold-ring-visual /
MP-bot-cap / B-219-MP-loadout / lv-tick-cap. None of them touch the
FIRE_PRIMARY -> c1buttons -> triggeron path or the lvRender ->
playerRenderHud -> bgunRender path at the function-body level.

The cumulative bondmove.c diff vs WORKING_REF (5997 lines) is dominated by
CRLF-inflated line moves; the actual semantic changes are bounded to the
USE-tap-hold rewrite + B-209 numsamples clamp + B-221 hold-ring helpers.

Mike's hypothesis "Cursor introduced the regression" is not supported by the
five Cursor-trailer commits when read against the WORKING_REF baseline. If
the regression IS Cursor-introduced, it is via:
1. A subtle interaction between two Cursor commits and an interleaved Claude
   commit.
2. An init-order shift caused by the new struct field
   (`pcinteractusekind`) that's not visible in the obvious diff.
3. A non-Cursor commit downstream that built on a Cursor change and
   propagated the breakage.

## Recommendation

Same as the prior archaeology pass: **don't revert anything blind.** The
round-5 diagnostic shipped at `292e87ea` instruments the three suspect choke
points. Mike's next playtest log produces three new diag lines per second;
the gate becomes visible in 5-10 seconds of gameplay.

If the round-5 diag points at one of these Cursor commits' touched
subsystems (e.g. `playerRenderHud branch=thirdperson_skip` lining up with a
late ACTION_USE state mutation that drives cameramode), this archaeology
pass gives the candidate-revert list immediately.

If the round-5 diag points at code that NO Cursor commit touched (e.g. a
gate in lv.c upstream of the cascade that's stuck "true" for some other
reason), the regression isn't Cursor's fault and the search continues
elsewhere (init order, downstream Claude commits that build on Cursor
foundations).
