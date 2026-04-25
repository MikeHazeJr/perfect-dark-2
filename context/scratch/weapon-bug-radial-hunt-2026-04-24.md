# Weapon-bug radial-menu hunt -- 2026-04-24

**Build under test:** `5ee1b013`
**Symptom (Mike):** spawn-with-shotgun, UI says equipped, can ADS, can toggle FireMode (UI updates), but FP weapon invisible / no fire / no pickup.
**Hypothesis (Mike):** introduced when the custom Weapon/Gadget radial menu was added.

## What I confirmed from the log (`019dc2d5-pdclient.log`)

- Match starts at 32.97s, scenario `base:combat`, stage `base:arena_mp_warehouse`, scenario index `0`.
- Spawn weapon = base:shotgun (weaponnum 19), auto-equipped to right hand (line 3066-3067).
- Frames 0..~2.4s of match: `BMOVE: allowc1x=0 allowc1y=0 allowc1buttons=0`. This is the **TICKMODE_MPSWIRL intro phase** (player.c:4832-4836 calls `bmoveTick(0,0,0,1)` unconditionally during swirl).
- Frame ~2.4s onward: `BMOVE: allowc1x=1 allowc1y=1 allowc1buttons=1`, indicating tickmode → NORMAL and `g_PlayersWithControl[0] == true` (later confirmed by ACTION_PAUSE diag at 60s: `g_PlayersWithControl[0]=1`).
- At 44.57s Mike pressed FIRE_PRIMARY and the dispatcher logged it: `DIAG fireVk: vk=550 player=0 -> IMC 'gameplay' action=20(FirePrimary) DOWN` — so fire **does** reach `s_State[0][ACTION_FIRE_PRIMARY]`.
- `pausemode=0`, `lvIsPaused=0` throughout.

## Hypotheses ruled out (refuted by code or log)

1. **`bondmovemode = MOVEMODE_CUTSCENE` leaking into Campaign/CS** -- REFUTED. `playermgrAllocatePlayer` (playermgr.c:583) resets `bondmovemode = MOVEMODE_WALK` per match. State cannot survive `mainLoop` outer iteration.
2. **`forgeIsFreefly()` false-positive** -- REFUTED. Log shows only `GRID: init` (line 655); no `GRID: -> FREEFLY` event. Predicate is module-static state, zero-init = INACTIVE, returns `state == FORGE_SESSION_FREEFLY` only.
3. **`g_PlayersWithControl[0]` stuck false** -- REFUTED. Log MENU diag at 60s shows `=1`. `bmoveTick(1,1,arg0,0)` is reached.
4. **`joybutinhibit` stuck on** -- REFUTED. Self-clearing via `joybutinhibit = (joybutinhibit & 0) | (held & inhibited)` (bondmove.c:1064). When fire is held, only fire bits stay; on release, all clear.
5. **`gunctrl.passivemode` stuck on** -- IMPROBABLE. Set only in training.c (DD2 holotraining) and chraicommands.c (cutscene AI command). No path from Combat Sim. `playermgrAllocatePlayer` (playermgr.c:429) resets to false anyway.
6. **`g_PlayerInvincible` stuck on** -- REFUTED. `playerReset` (playerreset.c:162) resets to false at every match start.
7. **Radial intro commit `4fcc42ec` changes to bondmove/prop/activemenu** -- REVIEWED. All additive: `actionLastGestureHoldMs` helper, terminal hold-extra config, USE-tap-reload synthesis. None set a "skip FP" flag or hijack fire dispatch.
8. **`pdguiActiveMenuIsOpen` stuck true** -- IMPROBABLE. Pure-state predicate (`currentplayer + activemenumode != AMMODE_CLOSED`). `activemenumode` is zero-init = AMMODE_CLOSED; only `amOpen` writes AMMODE_VIEW; `amClose` writes back to AMMODE_CLOSED. `amReset` (lv.c:609) closes for every player at match start.

## What's NOT yet probed

The contradiction is sharp: log proves `allowc1buttons=1` AND fire arrives in dispatcher AND not paused, yet **fire effect doesn't manifest in-world**. Something downstream of `actionPressed/actionHeld(0, ACTION_FIRE_PRIMARY)` is silently dropping the trigger. Candidates I haven't verified:

- **`movedata.triggeron` value** (bondmove.c:2207) -- depends on `c1buttons & shootbuttons` AND `waitforzrelease == false`. Need a single-line printf in `bgunTickGameplay` entry to confirm whether `triggeron` is reaching it.
- **Hand state machine stuck mid-equip** -- if `bgunSetState(handnum, HANDSTATE_ATTACK)` (bondgun.c:1343) bails because `hand->stateflags` or some other sub-state hasn't transitioned out of "equipping". Spawn-with-weapon path does `bgunEquipWeapon2(HAND_RIGHT, weaponnum)`, but if the chr-body / model load path is starving the hand state machine of "draw complete" event during the MPSWIRL phase, the hand may end up in a state where ATTACK transition is rejected forever.
- **`bgunTickGameplay` itself is gated** -- only ever called from inside `bmoveProcessInput`; if `bmoveProcessInput` returned early before line 2325, fire never even reaches the gun system. There IS an early-return path at line 935-937 for `isremote || controlmode == CONTROLMODE_NA` -- need to confirm `controlmode` for solo. If somehow the solo CS match has `controlmode == CONTROLMODE_NA`, the entire downstream is skipped.
- **`controlmode == CONTROLMODE_NA` (= 0?)** -- this would be a config / options regression, not radial-menu-related. Worth one log-line to dump.

## Why the radial-introducing commit `4fcc42ec` looks innocent

I diffed all the files it touched. The substantive game-side changes were:

| File | Change | FP-weapon impact |
|------|--------|------------------|
| `src/game/activemenu.c` | Gate legacy GBI wheel on `pdguiActiveMenuShouldSkipLegacyWheel`; factor `amInitActiveMenuSelectionCoords` / `amSyncCommandingAibotForActiveMenu` / `amGetSlotVisualMode` helpers | None -- gating is identical to pre-commit (legacy or radial, not both). |
| `src/game/bondmove.c` | USE-release with no prompt → synthesize RELOAD (X_BUTTON) above 80ms hold | None for FIRE_PRIMARY / FIRE_SECONDARY. |
| `src/game/prop.c` | Terminal hold-extra ms config wired | Pickup gate (`bondmovemode != CUTSCENE`) untouched. |
| `port/fast3d/pdgui_activemenu_radial.cpp` (NEW) | ImGui radial render. Gated on `pdguiActiveMenuShouldSkipLegacyWheel` AND `alphaFrac > 0.01f`. | No game-state writes; pure render of game state. |
| `port/fast3d/pdgui_bridge.c` | Bridge query helpers + `pdguiActiveMenuIsOpen` predicate. Each helper does `g_AmIndex = g_Vars.currentplayernum` before reading `g_AmMenus[g_AmIndex]`. | Could be a problem in 4-local-player split where bridge clobbers `g_AmIndex` while game-side amTick expects a different player's slot, but solo single-player Mike's case has only player 0. |
| `port/fast3d/pdgui_backend.cpp` | NewFrame/Render gates expanded to include `menuStackDiag`. | None -- additive. |

The structural design of the radial replacement is sound: legacy and radial are mutually exclusive on the same `pdguiActiveMenuIsOpen` predicate; close path goes through `amClose` which sets `activemenumode = AMMODE_CLOSED` and `g_PlayersWithControl[currentplayernum] = 1`. No "stuck flag" pattern apparent.

## Recommended next probe (what would close this fast)

If you can drop one or two log lines into `bondmove.c::bmoveProcessInput` and rebuild:

1. **At the top, after `bmoveProcessInput` is entered:**
   `sysLogPrintf(LOG_NOTE, "BMOVE_ENTER: controlmode=%d isremote=%d", controlmode, (s32)g_Vars.currentplayer->isremote);`

2. **Just before line 2325 (`bgunTickGameplay`):**
   `sysLogPrintf(LOG_NOTE, "BMOVE_FIRE: triggeron=%d c1buttons=0x%x shootbuttons=0x%x waitforzrelease=%d pausemode=%d", (s32)movedata.triggeron, (u32)c1buttons, (u32)shootbuttons, (s32)g_Vars.currentplayer->waitforzrelease, (s32)g_Vars.currentplayer->pausemode);`

That single repro will collapse the search space:

- If `BMOVE_ENTER` shows `controlmode=0 (NA)` → config / options regression; not radial.
- If `BMOVE_ENTER` is missing entirely → bmoveTick is being skipped by a tickmode branch.
- If `BMOVE_FIRE` shows `triggeron=0` with `c1buttons=Z_TRIG (0x4000)` → `waitforzrelease` or `pausemode` is the culprit.
- If `BMOVE_FIRE` shows `triggeron=1` → break is **inside `bgunTickGameplay` or the hand state machine** -- not bondmove.

I'd recommend Mike do this single-shot probe rather than have me continue blind on static read.

## Status

- Task 1 (radial root-cause): blocked on runtime probe; static read exhausted of high-leverage angles.
- Tasks 2 (LAN/Tier-0 doc), 3 (crash log scan), 4 (Priority N capture as B-240): not started; can proceed in parallel while waiting for the probe rebuild.
