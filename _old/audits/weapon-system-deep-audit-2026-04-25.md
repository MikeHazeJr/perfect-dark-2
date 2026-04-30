# Weapon system deep audit -- 2026-04-25

**Worktree:** `claude/charming-noether-7b69b3` (base dev `549ffdd2`)
**Trigger:** B-246 round-5 playtest still showing no FP hands + weapon jump on fire despite master-loader fix landing in `f83ca230`. Mike approved a comprehensive instrumentation + struct-corruption audit.
**Scope:** every weapon-related path in player + render + load pipeline; Mission, Combat Simulator, Vehicle, Forge / The Grid, FreeFly, observer / Playtest. Plus root-cause investigation of `visionmode=51503` anomaly seen in the round-5 log.

This document captures findings, tracks instrumentation sites, and lists the candidate failure causes with the runtime evidence that would rule each one in or out.

---

## 1. Reproduction window from the round-5 playtest log

Source log: `~/Downloads/Perfect Dark 2.0/pd-client.log` (29 345 lines, 768 `LOG.WPN.DIAG` events).

In-match firing window is stage 4: `01:31.41` spawn through `01:57.44` end of log.

Stage-4 facts:
- `playerSpawn done`: `chr=0x...39a5570`, `model00d4=0x...397f508`, `haschrbody=1`, `switchto=22`. First stage in the log with `haschrbody=1`.
- `playerChooseBodyAndHead normmp`: `resolved_body=86 ('base:dark_combat')`, `resolved_head=4 ('base:head_dark_combat')`, both rigs `human_female_neck_standard`. 21 calls in a 0.09s burst (`01:33.36 -> 01:33.45`).
- `bgunEquipWeapon2 enter/exit`: hand=0 set `switchto=22` at `01:31.41`. No `bgunTickSwitch2` log lines for `switchto=22`, but by `01:34.97` `gunctrl_wpn=22` so the switch did complete.
- `playerRenderHud branch`: `thirdperson_skip` at `lvframenum=23/83/143`, then `fp_render` continuously from 203 onward.
- `fire handler`: 9 events between `01:34.97` and `01:53.76`. All show `R(wpn=22 inuse=1 visible=1 state=0|4) L(wpn=0 inuse=0 visible=0 state=0)`. Right hand visible=1 throughout firing.
- `bgunTickGameplay enter`: gunctrl_wpn=22, R inuse=1, L inuse=0 across all sampled frames in stage 4.
- `visibility-gate-fail`: zero events in stage 4 (5 in stage 2 for wpn=3 during loading).
- `LOAD-mode6`: zero events in stage 4 (5 in stage 2).
- `bgunRender enter`: zero events. Diagnostic gate is `lvframe60 < 5`; in stage 4 the cameramode is THIRDPERSON during those frames so `playerRenderHud` early-returns at [src/game/player.c:5515](src/game/player.c:5515) and never calls `bgunRender`.

Mike's three observations from the playtest:
1. He could fire the weapon (confirmed by 9 fire-handler events).
2. No hands visible.
3. Weapon jumped from a wrong spot to a correct spot when firing, then back.

---

## 2. visionmode=51503 -- root cause

### Finding

The diagnostic at [src/game/player.c:5504](src/game/player.c:5504) prints `visionmode=51503` for the first 8 sampled frames (`00:01.54 -> 00:05.83`), then drops to `visionmode=0` from frame 503 onward.

`visionmode` is `u16` at offset 0x0010 of `struct player` per [src/include/types.h:2384](src/include/types.h:2384). 51503 = 0xC92F; valid u16, not a valid `VISIONMODE_*` enum value (0=NORMAL, 1=XRAY, 2=SLAYERROCKET, 3=SLAYERROCKETSTATIC per [src/include/constants.h:4411-4414](src/include/constants.h:4411)).

The only field-init for `visionmode` in player allocation is at [src/game/playermgr.c:413-415](src/game/playermgr.c:413):

```c
#if VERSION >= VERSION_JPN_FINAL
    g_Vars.players[index]->visionmode = VISIONMODE_NORMAL;
#endif
```

Build configuration sets `VERSION=2 (VERSION_NTSC_FINAL)` per [CMakeLists.txt:86](CMakeLists.txt:86) when `ROMID="ntsc-final"`. The init does not compile in. `mempAlloc` at [src/lib/memp.c:213](src/lib/memp.c:213) does not zero memory before returning.

Net effect: `g_Vars.players[index]->visionmode` is read from uninitialised heap memory until something writes a valid value. The 51503 -> 0 transition aligns with the first call to `bgunTickGameplay2` after the cutscene ends, which writes `VISIONMODE_NORMAL` at [src/game/bondgun.c:8439](src/game/bondgun.c:8439) or [bondgun.c:8445](src/game/bondgun.c:8445).

### Implication

This is a real latent bug. By the time the firing window of stage 4 arrives, `visionmode` has been written to 0, so the missing-hands and weapon-jump symptoms are NOT caused by `visionmode` corruption at the moment of firing. But the early-cutscene window has undefined behaviour: if the heap garbage happens to equal `VISIONMODE_XRAY` (1), `bgunRender` at [bondgun.c:11170](src/game/bondgun.c:11170) early-returns and the FP rig does not render at all. With arbitrary u16 garbage the chance is 1/65536, but 1 is a common allocator-byte value and a freshly allocated pool can plausibly produce it.

### Other JPN_FINAL-gated init paths surveyed

`grep -nE "VERSION >= VERSION_JPN_FINAL"` across player/gun/init files returns 10 hits. Only one is a struct-field initialiser ([playermgr.c:413](src/game/playermgr.c:413)). The others are screen-mode helpers, ammo clamp, hud text alignment, etc. -- not field initialisers. So `visionmode` is the unique case.

### Status

**Fixed in this audit.** Per Mike's policy update 2026-04-25 (PD2 targets USA ROM only; legacy `VERSION_*` gates are not load-bearing), the `#if VERSION >= VERSION_JPN_FINAL` wrapper was removed at [playermgr.c:413](src/game/playermgr.c:413). The init is now unconditional: `g_Vars.players[index]->visionmode = VISIONMODE_NORMAL`.

A project-wide sweep of `VERSION_*` preprocessor gates is queued for after stability work completes (spawned as a separate task; not in scope this session).

Instrumentation in this audit also logs `visionmode` through the cutscene window so the next playtest captures any remaining wild values.

---

## 3. Catalog and weapon-flag verification for the stage-4 reproduction

### Body 86 has a configured hand-model file

`g_HeadsAndBodies[0x56]` at [src/game/modeldata/robot.c:161](src/game/modeldata/robot.c:161):

```c
{ /*0x0056*/ 0, 0, 0, HEADBODYTYPE_FEMALE, 159, FILE_CDARK_COMBAT, 1, 0.95305162668228, 0, FILE_GCOMBATHANDSLOD },
```

`handfilenum = FILE_GCOMBATHANDSLOD`. `catalogGetBodyHandFilenum(86)` at [port/src/assetcatalog_api.c:902](port/src/assetcatalog_api.c:902) returns this filenum.

So when stage 4 calls `bgunTickMasterLoad`, `handfilenum` resolves to a valid file. This rules out the "body has no hands registered" candidate from the prior triage.

### Weapon 22 (FARSIGHT) carries WEAPONFLAG_HASHANDS

[src/game/invitems.c:3653](src/game/invitems.c:3653) flags include `WEAPONFLAG_HASHANDS`. So the `hashands = true` branch at [bondgun.c:4099](src/game/bondgun.c:4099) takes effect during master load and the hand-file load path runs.

### So why aren't the hands visible?

The rendering gate that decides whether to draw the FP arms is [bondgun.c:11367-11378](src/game/bondgun.c:11367):

```c
if (player->gunctrl.handmodeldef && renderhand) {
    s32 prevcolour = renderdata.envcolour;
    hand->handmodel.matrices = hand->gunmodel.matrices;
    modelUpdateRelations(&hand->handmodel);
    renderdata.envcolour = colour;
    modelRender(&renderdata, &hand->handmodel);
    renderdata.envcolour = prevcolour;
}
```

`renderhand` is a function-local `static bool renderhand = true` at [bondgun.c:11137](src/game/bondgun.c:11137); no `renderhand =` write sites exist in the codebase. So the gate reduces to `gunctrl.handmodeldef != NULL`.

Possible reasons `handmodeldef` could be NULL during stage-4 firing despite a valid catalog entry:
1. `bgunTickMasterLoad` never advanced past `MASTERLOADSTATE_HANDS` because the hand-file load returned early.
2. The hand-file load path completed but hit the `handfilenum != gunctrl.handfilenum` short-circuit at [bondgun.c:4138](src/game/bondgun.c:4138) and never started the load.
3. `bgunEnterFlux` at [bondgun.c:3744](src/game/bondgun.c:3744) cleared `handmodeldef = NULL` during a weapon-switch and the master loader never refilled it.
4. `bgunChangeGunMem` parked `gunmemowner` in `GUNMEMOWNER_CHRBODY` indefinitely, blocking master-load progression.
5. Hand model loaded successfully but `modelRender(&hand->handmodel)` produces no output (matrix degeneracy from the gun-model matrix aliasing at [bondgun.c:11371](src/game/bondgun.c:11371)).

Current diagnostics cannot disambiguate. The instrumentation added in this audit (Section 5) addresses each of these.

---

## 4. Weapon-jump-on-fire candidates

Two structurally different gun-matrix paths exist depending on `a0` at [bondgun.c:8025-8147](src/game/bondgun.c:8025):

- `animmode == HANDANIMMODE_IDLE` (and a few other idle gates): `a0 = true`. Matrices computed by `mtx00015be4` against the cached `unk0dd8` buffer, which is filled exactly once when `unk0dd4 == -1` at [bondgun.c:8091-8120](src/game/bondgun.c:8091). `unk0dd4` is reset to `-1` only during `MASTERLOADSTATE_CARTS` handling at [bondgun.c:4262](src/game/bondgun.c:4262), i.e. on a fresh weapon load.
- Otherwise (fire or any non-idle anim): `a0 = false`. Fresh `modelSetMatricesWithAnim(&hand->gunmodel)` each frame at [bondgun.c:8138](src/game/bondgun.c:8138) or [bondgun.c:8146](src/game/bondgun.c:8146).

Plausible mechanisms for "wrong spot -> correct spot on fire -> back to wrong":

| # | Hypothesis | Evidence path |
|---|---|---|
| A | `unk0dd8` cache filled while animation was not in true rest pose. Idle path uses stale cache; fire path forces fresh anim eval. | Log `unk0dd4` value entering the per-frame matrix function; log `animmode` and the first matrix entry of `unk0dd8` after fill. |
| B | `hand->useposrot` is false during idle (so `posoffset / rotxoffset` get zeroed at [bondgun.c:7913-7918](src/game/bondgun.c:7913)) but true during fire, and the underlying base pose has an additive bug that recoil-time offsets coincidentally correct. | Log `hand->useposrot`, `hand->posoffset`, `hand->rotxoffset` per frame at the bgun0f0a5550 entry. |
| C | `damplook` / `dampup` interpolators at [bondgun.c:7920-7922](src/game/bondgun.c:7920) advance differently during fire vs idle, producing visible camera-relative gun position drift. | Log `hand->damplook`, `hand->dampup` per frame around fire transitions. |
| D | Animation asset for FARSIGHT idle vs shoot have keyed root-bone positions that differ. Asset / authoring mismatch, not code. | Inspect FARSIGHT idle / shoot animations in the asset definition. |

Tier 2 instrumentation samples A/B/C every frame around fire boundaries. D is a separate asset-side investigation if A/B/C all rule out.

---

## 5. Instrumentation sites added in this audit

Each site emits at least one `LOG.WPN.DIAG: <function-tag>: <state>` line. Entry vs exit naming uses `enter` / `exit` suffixes when both fire on a single call. Throttling notes follow the pattern in [Section 6](#6-throttling-pattern).

| Tier | File | Function | Throttle | Sites added |
|---|---|---|---|---|
| 1 | `bondgun.c` | `bgunTickMasterLoad` | first call + every state-transition + every 60th tick while !LOADED | TBD |
| 1 | `bondgun.c` | `bgunRender` | first 5 frames per stage AND every 60th frame thereafter | widen existing |
| 1 | `bondgun.c` | `bgunChangeGunMem` | every call (low-frequency) | TBD |
| 1 | `bondgun.c` | `bgunSetGunMemWeapon` | every call | TBD |
| 1 | `bondgun.c` | `bgunFreeWeapon` | every call | TBD |
| 1 | `bondgun.c` | `bgunEnterFlux` | every call | TBD |
| 2 | `bondgun.c` | `bgunSetState` | on state-change | TBD |
| 2 | `bondgun.c` | `bgunTickInc` | on prevstate != newstate | TBD |
| 2 | `bondgun.c` | `bgunCanFreeWeapon` | first false-to-true and true-to-false transition only | TBD |
| 2 | `bondgun.c` | `bgunTickGunLoad` | on gunloadstate transition | TBD |
| 3 | `bondgunreset.c` | `bgunReset` | every call (per-player init) | TBD |
| 3 | `bondgun.c` | `bgunInitHandAnims` | every call | TBD |
| 3 | `bondgun.c` | `bgunDisarm` | every call | TBD |
| 3 | `bondgun.c` | `bgunHandlePlayerDead` | every call | TBD |
| 3 | `bondgun.c` | `bgunSetPassiveMode` | every call | TBD |
| 3 | `bondgun.c` | `bgunAutoSwitchWeapon` | every call | TBD |
| 3 | `bondgun.c` | `bgunEquipWeapon` | every call | TBD |
| 3 | `player.c` | `playerLoadDefaults` | every call | TBD |
| 3 | `player.c` | `playerRemoveChrBody` | every call | TBD |
| 3 | `player.c` | `playerTickChrBody` | on haschrbody change | TBD |
| 4 | `forgemode.c` | freefly chr-swap, observer<->playtest | every call | TBD |
| 4 | `mplayer/setup.c` | MP setup hooks | every call | TBD |

Site-by-site `file:line` table populated as each lands. Final tally ledger in [Section 7](#7-final-instrumentation-ledger).

---

## 6. Throttling pattern

A single helper macro is used wherever a per-frame call site needs throttling. All other sites log unconditionally because they are low-frequency by construction.

```c
/* In bondgun.c top-of-file or shared header. */
#define WPN_DIAG_FIRST_N_OR_PERIODIC(first_n, periodic_n, body) do { \
    static s32 _diag_count = 0;                                       \
    static s32 _diag_lastlv60 = -1;                                   \
    if (g_Vars.currentplayernum == 0) {                               \
        if (_diag_count < (first_n)                                   \
                || ((s32)g_Vars.lvframe60 != _diag_lastlv60            \
                        && (s32)g_Vars.lvframe60 % (periodic_n) == 0)) { \
            _diag_count++;                                             \
            _diag_lastlv60 = (s32)g_Vars.lvframe60;                    \
            body                                                        \
        }                                                              \
    }                                                                  \
} while (0)
```

For state-change sites, the pattern is:

```c
static u32 _last_state = 0xFFFFFFFFu;
u32 _now_state = (u32)<combined-state-tuple>;
if (_now_state != _last_state) {
    _last_state = _now_state;
    sysLogPrintf(LOG_NOTE, "LOG.WPN.DIAG: <tag>: %s ...", reason, ...);
}
```

This keeps the log grep-able while still capturing every transition the next playtest might need to expose.

---

## 7. Final instrumentation ledger

All sites tagged `LOG.WPN.DIAG: <tag>`. Player 0 only unless noted. Round-6 sites are the additions from this audit; round-2 / round-3 / round-5 sites pre-existed and are listed for completeness.

### Round-6 additions (this audit)

| Tier | File:line | Tag | Fields printed | Throttle |
|---|---|---|---|---|
| 1 | bondgun.c:3775 | `bgunFreeGunMem enter` | owner_was, gunmemtype, gunmemnew, masterload | every call |
| 1 | bondgun.c:3801 | `bgunSetGunMemWeapon enter` | wpn, owner, gunmemnew_was, gunmemtype, masterload_was, gunloadstate_was | every call |
| 1 | bondgun.c:3821 | `bgunSetGunMemWeapon exit` | wpn, owner, gunmemnew, masterload, gunloadstate, gunlocktimer | every call |
| 1 | bondgun.c:3844 | `bgunEnterFlux enter` | handfilenum_was, handmodeldef_was, gunmodeldef_was, masterload_was, owner | every call |
| 1 | bondgun.c:3882 | `bgunChangeGunMem enter` | cur_owner, new_owner, gunlocktimer, gunmemnew, gunmemtype, haschrbody, mp | every call where owner != newowner |
| 1 | bondgun.c:3905 | `bgunChangeGunMem exit branch=timer_complete` | result=1, owner_now, gunlocktimer | only on timer-complete branch |
| 1 | bondgun.c:3956 | `bgunChangeGunMem exit (post-switch)` | result=0, owner_now, gunlocktimer, unlock | every post-switch exit |
| 1 | bondgun.c:3968 | `bgunChangeGunMem exit branch=timer_pending` | result, owner_now, gunlocktimer | timer-pending exit path |
| 1 | bondgun.c:4252 | `bgunTickMasterLoad enter` | newwpn, filenum, bodynum, handfilenum, hashands_flag, masterload, gunloadstate, gunmemowner, gunmemtype, handmodeldef, gunmodeldef | one log per (newwpn, masterload) tuple change |
| 1 | bondgun.c:4311 | `bgunTickMasterLoad transition FLUX->HANDS` | newwpn, hashands, handfilenum, cur_handfilenum | every transition |
| 1 | bondgun.c:4348 | `bgunTickMasterLoad transition HANDS->GUN` | newwpn, hashands, handfilenum, handmodeldef | every transition |
| 1 | bondgun.c:4371 | `bgunTickMasterLoad transition GUN->CARTS` | newwpn, gunmodeldef, handmodeldef | every transition |
| 1 | bondgun.c:4474 | `bgunTickMasterLoad transition CARTS->LOADED` | newwpn, gunmodeldef, handmodeldef, sum, unk0dd4, unk0dd8 | every transition |
| 1 | bondgun.c:4492 | `bgunTickMasterLoad transition SHORTCUT->LOADED` | newwpn | every shortcut-LOADED hit |
| 1 | bondgun.c:11508 | `bgunRender enter` (widened) | frame, lvframe60, visionmode, R(wpn/visible/inuse/state/sm), L(...), gunctrl_wpn, switchto, passive, gunmodeldef, handmodeldef, R_handmodel_def, R_gunmodel_def, masterload, gunmemowner | first 5 frames per stage + every 60th frame thereafter |
| 2 | bondgun.c:2855 | `bgunCanFreeWeapon` | hand, result, state, sm, throwing | per-hand transition (true<->false) |
| 2 | bondgun.c:3364 | `bgunSetState` | hand, prev->next, valid, wpn, inuse | every accepted transition + rejected CHANGEFUNC |
| 2 | bondgun.c:8003 | `bgun0f0a5550 enter` | wpn, frame, state, sm, animmode, useposrot, visible, inuse, posoffset, rotxoffset, damplook, dampup, unk0dd4 | every 30 frames OR on (animmode/state) change (RIGHT hand) |
| 2 | bondgun.c:8420 | `bgun0f0a5550 a0_decision` | a0, wpn, animmode, state, sm, ejectstate, unk0dd4 | on a0 transition (RIGHT hand) |
| 2 | bondgun.c:8466 | `bgun0f0a5550 cache_fill` | wpn, animmode, animnum, animframe, frame | every cache fill (one-shot per weapon load) |
| 3 | bondgun.c:3445 | `bgunInitHandAnims` | R(state/animmode/animload), L(...) | every call |
| 3 | bondgun.c:5706 | `bgunFreeWeapon enter` | hand, inuse_was, state, sm, wpn, gunmemtype | every call |
| 3 | bondgun.c:5748 | `bgunFreeWeapon exit` | hand, inuse, wpn | every call |
| 3 | bondgun.c:5937 | `bgunEquipWeapon enter` | wpn, cur_wpn, cur_switchto, shortcircuit | every call |
| 3 | bondgun.c:6217 | `bgunAutoSwitchWeapon enter` | cur_wpn, switchto, tickmode | every call |
| 3 | bondgun.c:6650 | `bgunHandlePlayerDead` | wpn, switchto, R(inuse), L(inuse), gunmemtype | every call |
| 3 | bondgun.c:6711 | `bgunDisarm` | wpn, switchto, net, attackerprop | every call |
| 3 | bondgun.c:12716 | `bgunSetPassiveMode` | players_p0_was, enable | every call (all players) |
| 3 | bondgunreset.c:243 | `bgunReset` | gunmem, gunmemowner, gunmemtype, gunmemnew, masterload, gunloadstate, switchto, handfilenum, handmodeldef, gunmodeldef, loadall | every call |
| 3 | player.c:1335 | `playerLoadDefaults enter` | visionmode, cameramode, haschrbody, gunctrl_wpn, switchto, gunmemowner | every call |
| 3 | player.c:2471 | `playerTickChrBody haschrbody` | prev->now, gunmemowner, gunctrl_wpn, switchto, model00d4 | only on haschrbody flip |
| 3 | player.c:2504 | `playerRemoveChrBody` | haschrbody_was, removed, mp, model00d4 | every call |
| 4 | forgemode.c:497 | `forgeTransitionToNormal` | reason, wpn, switchto, gunmemowner, masterload, handmodeldef, haschrbody | every call |
| 4 | forgemode.c:541 | `forgeTransitionToFreefly` | reason, wpn, switchto, gunmemowner, masterload, handmodeldef, haschrbody | every call |

### Pre-existing sites (rounds 2 / 3 / 5)

| Round | File:line | Tag | Purpose |
|---|---|---|---|
| 2 | bondgun.c:5770 | `bgunTickSwitch2` | logs queued-switch wedge with per-hand canFree state |
| 2 | bondgun.c:6336 / 6362 | `bgunEquipWeapon2 enter / exit` | logs equip request and switchto outcome |
| 2 | bondgun.c:12475 | `bgunTickGameplay enter` | logs per-tick gameplay state |
| 2 | bondgun.c:12505 / 12515 | `fire handler enter / hands` | logs trigger rising-edge |
| 3 | bondgun.c:8178 | `visibility-gate-fail` | logs which gate forced visible=false |
| 3 | bondgun.c:3019 | `LOAD-mode6` | logs hand mode 6 progression conditions |
| 5 | bondgun.c:11508 | `bgunRender enter` | (now widened in round-6) |
| 5 | player.c:5552 | `playerRenderHud branch` | logs which render path was taken |
| 5 | player.c:1871 | `playerSpawn done` | logs spawn outcome |
| 5 | player.c:2012 | `playerChooseBodyAndHead normmp` | logs body/head resolution for MP |
| 5 | lv.c:1594 | `lvRender cascade` | logs render-cascade entry to playerRenderHud |
| 5 | prop.c:2718 | `pickup probe` | logs pickup gates |
| 5 | bondmove.c:1001 | `fire-input pi` | logs fire input edge (sampled) |

Total `LOG.WPN.DIAG` sites: 50+ across `bondgun.c`, `bondgunreset.c`, `player.c`, `forgemode.c`, `lv.c`, `prop.c`, `bondmove.c`.

---

## 8. Candidate failure avenues with rule-in / rule-out criteria for the next playtest log

This is the core debug deliverable. Each candidate is paired with the specific log line and value pattern that would rule it in or out. Mike's next playtest log will be triaged against this matrix.

| ID | Candidate | Rules IN if | Rules OUT if |
|---|---|---|---|
| H-NULL-A | `gunctrl.handmodeldef == NULL` at render time because master loader never advanced past MASTERLOADSTATE_HANDS for body 86 + weapon 22 | `LOG.WPN.DIAG: bgunTickMasterLoad: hands_load handfilenum=N hashands=1` followed indefinitely by `state=hands` and never `state=gun` or `state=loaded`. | `LOG.WPN.DIAG: bgunTickMasterLoad: ... state=loaded handmodeldef=0xNNN` with non-NULL pointer before the first fire-handler entry. |
| H-NULL-B | `bgunEnterFlux` zeroed `handmodeldef` and master loader did not refill before fire | `LOG.WPN.DIAG: bgunEnterFlux: ... handmodeldef=0->NULL` with no subsequent `handmodeldef=NULL->0xNNN` log line before fire. | A `handmodeldef` non-NULL log line appears between the most recent `bgunEnterFlux` and the first fire-handler entry. |
| H-NULL-C | `bgunChangeGunMem` parked `gunmemowner` in CHRBODY indefinitely | `LOG.WPN.DIAG: bgunChangeGunMem: enter from=CHRBODY to=BONDGUN unlock=0` repeating across many frames. | Single `bgunChangeGunMem: enter from=CHRBODY to=BONDGUN unlock=1` line. |
| H-NULL-D | Hand model loaded but `handmodel.definition` is NULL (load returned without populating modeldef) | `LOG.WPN.DIAG: bgunRender: ... handmodeldef=0xNNN handmodel_def=NULL` at render time. | `handmodel_def` is non-NULL throughout. |
| H-MTX-A | `unk0dd8` cache stale; idle uses cache, fire uses fresh anim. | `LOG.WPN.DIAG: bgun0f0a5550: enter ... a0=1` on idle frames AND `a0=0` on fire frames AND visible position-jump correlates with `a0` flips. | Position-jump persists when `a0` is constant. |
| H-MTX-B | `hand->useposrot` toggling causes additive offsets only during fire | `LOG.WPN.DIAG: bgun0f0a5550: enter ... useposrot=0` idle vs `useposrot=1` fire AND posoffset values explain the jump magnitude. | `useposrot` constant; jump persists. |
| H-MTX-C | `damplook` / `dampup` divergence | Per-frame `damplook` / `dampup` values diverge between idle and fire frames in a way that explains the jump. | These vectors are stable across the boundary. |
| H-VIS-A | Early-cutscene visionmode garbage equalled XRAY | `LOG.WPN.DIAG: playerRenderHud branch=fp_render visionmode=1 ...` on any frame in the first 5s of stage 4. | visionmode is 0 throughout the in-match window. (Already partially confirmed: log shows 0 throughout firing window.) |
| H-INPUT-A | Fire input is dispatched but the fire chain is dead because of left-hand state corruption | `LOG.WPN.DIAG: fire handler: ... L(state != 0)` on fire frames. | L state is 0 (idle) throughout (already confirmed). |
| H-CTX-A | Forge / FREEFLY toggle nukes the FP rig and the master loader does not re-establish it on session re-entry | `LOG.WPN.DIAG: forgeTransitionToNormal ... handmodeldef=NULL` on Forge -> Normal transition followed by no `bgunTickMasterLoad transition CARTS->LOADED ... handmodeldef=0xNNN` before the next fire-handler entry. | Either no Forge transitions during the in-match window, OR a `CARTS->LOADED` log line lands between the Forge exit and the first fire-handler. |
| H-CTX-B | Stage-4 spawn occurs while gunmem pool is still owned by CHRBODY and the unlock flow stalls | `LOG.WPN.DIAG: bgunChangeGunMem enter cur_owner=CHRBODY new_owner=BONDGUN haschrbody=1 mp=1` followed indefinitely by `result=0 unlock=0` log lines. | Single `unlock=1` line shortly after the spawn. |
| H-CTX-C | bgunReset on stage 4 leaves gunmemowner in a stale state because of the static-hand-init pattern | `LOG.WPN.DIAG: bgunReset player=0 gunmemowner != GUNMEMOWNER_CHRBODY` (per code, must be CHRBODY at exit; any other value is a regression) | gunmemowner == 1 (CHRBODY) at every bgunReset exit. |
| H-CTX-D | playerLoadDefaults fires with a non-zero visionmode, indicating the JPN_FINAL fix did not apply or another wild writer is active | `LOG.WPN.DIAG: playerLoadDefaults enter ... visionmode != 0` on any stage spawn after the round-6 fix lands. | visionmode == 0 at every playerLoadDefaults entry. |
| H-FLUX-A | A weapon-switch path calls `bgunEnterFlux` after stage-4 spawn and the master loader does not refill `handmodeldef` before fire | A `bgunEnterFlux enter handmodeldef_was=0xNNN` log line in stage 4 not followed by a `CARTS->LOADED handmodeldef=0xNNN` line before the first fire-handler. | No `bgunEnterFlux enter` log lines in stage 4 after spawn. |

This matrix is appended to as instrumentation lands.

---

## 10. Round-6 deliverables summary

1. **visionmode latent uninit fixed.** Removed `#if VERSION >= VERSION_JPN_FINAL` gate from [src/game/playermgr.c:413](src/game/playermgr.c:413). Field is now unconditionally initialised to `VISIONMODE_NORMAL` for our NTSC_FINAL build.

2. **Project-wide VERSION-gate strip queued.** Spawned task chip captures the future cleanup pass; not in scope this session.

3. **Comprehensive instrumentation landed across 4 tiers.** Tier 1 covers the load / pool surface (master loader, change-gun-mem, set-gun-mem-weapon, free-weapon, enter-flux, free-gun-mem). Tier 2 covers state-machine transitions and the matrix-jump candidate paths (a0 decision, cache fill, useposrot / damplook / dampup at bgun0f0a5550 entry). Tier 3 covers per-player init / disarm / dead / passive / auto-switch / equip / playerLoadDefaults / playerRemoveChrBody / playerTickChrBody. Tier 4 covers forgemode FREEFLY / NORMAL transitions.

4. **Build verified clean.** Both `pd` (PerfectDark.exe) and `pd-server` (PerfectDarkServer.exe) link with the round-6 changes applied.

5. **Candidate-cause matrix populated** with 13 distinct hypotheses spanning H-NULL-* (handmodeldef NULL paths), H-MTX-* (matrix divergence), H-VIS-* (visionmode corruption), H-INPUT-* (input authority), H-CTX-* (cross-mode context wedge), H-FLUX-* (flux-without-refill). Each carries a specific log signature the next playtest will produce.

### What Mike does next

Run a playtest of the round-6 build on the Stage 4 reproduction (CS Combat Sim, FARSIGHT, dark_combat body). Capture `pd-client.log`. The matrix in Section 8 maps each log signature to the candidate it rules in or out. Triage against the matrix.

The fastest expected path: the new `bgunTickMasterLoad transition CARTS->LOADED ... handmodeldef=0xNNN` line either appears or it doesn't. If it appears with a non-NULL `handmodeldef` and the FP arms still don't render, we shift focus to H-NULL-D (handmodel.definition NULL) and H-MTX-* (matrix divergence). If it does not appear, we walk back through HANDS->GUN, FLUX->HANDS, and the bgunChangeGunMem unlock chain to find where progression stalls.

---

## 9. Build verification log

Each instrumentation pass must build both `pd` and `pd-server` cleanly.

### Round-6 build (this audit)

| Field | Value |
|---|---|
| Branch | `claude/charming-noether-7b69b3` (worktree) |
| Base | dev `549ffdd2` |
| Build env | `source devtools/build-env.sh` (ninja + mingw64 + ccache) |
| Configure | `cmake -G Ninja -B Build -S . -DROMID=ntsc-final` -- 5.5s, clean |
| Build cmd | `ninja -C Build pd pd-server` |
| Result | exit 0, 461 build steps |
| `Build/PerfectDark.exe` | 56 197 458 bytes (2026-04-26 14:19) |
| `Build/PerfectDarkServer.exe` | 23 272 107 bytes (2026-04-26 14:19) |

Build hit one missing-include error on first attempt (`bondgunreset.c` did not pull in `system.h` for `sysLogPrintf` / `LOG_NOTE`). Fixed by adding `#include "system.h"` to the includes block. Second build was clean.

No new warnings introduced by the round-6 instrumentation. Pre-existing `-Wmaybe-uninitialized` warnings in `chraction.c` are unrelated.

---

*Audit doc opened 2026-04-25. Updates appended in-place as instrumentation lands.*

---

## 11. Round-7 -- no-hands repro caught the loop driver (2026-04-26)

### Mike's playtest, verbatim

> "I just had a playthrough where spawn weapon was set to random and I spawned with what it said was a Falcon 2, but no hands or weapon. Probably some failed init with the random spawn weapon option. I think it may also occur with random weapon set options also, as a not for later. But as I spawned with no usable weapon, I could not fire."

### Log statistics (in-match window 00:41 -> 01:01, ~20 s)

| Diagnostic | Round-5 count | Round-6 (no-fire) | Round-7 (no-hands) |
|---|---|---|---|
| `bgunTickMasterLoad enter` | 0 | 12 | **7301** |
| `bgunTickMasterLoad transition` | 0 | 12 | **7301** (1825 each of FLUX->HANDS / HANDS->GUN / GUN->CARTS / CARTS->LOADED, plus 1 SHORTCUT) |
| `bgunEnterFlux enter` | n/a | 46 | **5477** |
| `bgunChangeGunMem enter/exit` | n/a | 4366 each | **5765 each** |
| `playerChooseBodyAndHead normmp` | 21 | 21 | **34659** |
| `bgunRender enter` | 0 | 42 | 70 (all stage-2 samples show `R(wpn=2 visible=1 inuse=1 state=5 sm=2)`, `handmodeldef=0x0000023a42b6cec0` non-NULL, `masterload=4 LOADED`) |

The master loader is running 1825 full FLUX -> HANDS -> GUN -> CARTS -> LOADED cycles for `newwpn=2` (FALCON2) in the in-match window. At ~91 cycles/sec on a 60 Hz tick, this is multiple cycles per frame. Hand state machine is stuck at `HANDSTATE_CHANGEGUN, sm=HANDSTATEMINOR_CHANGEGUN_LOAD`.

### Root cause

`bgunChangeGunMem enter` shows three dominant patterns repeating 1824 times each:

1. `cur_owner=0 (BONDGUN) new_owner=1 (INVMENU) gunlocktimer=0 gunmemnew=-1 gunmemtype=2`
2. `cur_owner=10 (CHANGING) new_owner=1 gunlocktimer=-1 gunmemnew=2 gunmemtype=-1`
3. `cur_owner=10 new_owner=0 (BONDGUN) gunlocktimer=-2 gunmemnew=2 gunmemtype=-1`

Pattern 1 is the BONDGUN-out side effect of [bondgun.c:3787-3796](src/game/bondgun.c:3787): `gunmemnew = gunmemtype` (preserve current weapon for reload), `gunmemtype = -1`, `bgunEnterFlux()` (clear `handmodeldef`, set masterload=FLUX), `unlock=true`, `gunlocktimer=-1`, `gunmemowner=CHANGING`. The 3-frame timer is loaded.

Pattern 3 is the master loader at [bondgun.c:4232](src/game/bondgun.c:4232): `if ((gunmemowner == BONDGUN || bgunChangeGunMem(BONDGUN)) && gunmemnew >= 0)`. When `gunmemowner=CHANGING` and the timer is at `-2`, this call ticks it to `-3` and flushes -> `gunmemowner=BONDGUN` (the master loader's `newowner`, NOT the original requester's).

Pattern 2 is the loser of the race -- charpreview's INVMENU request, called after the master loader on the timer-flush frame, sees the new BONDGUN owner and triggers a fresh BONDGUN-out cycle.

The fight runs every frame because:
- Charpreview at [pdgui_charpreview.c:602-607](port/fast3d/pdgui_charpreview.c:602) calls `bgunChangeGunMem(GUNMEMOWNER_INVMENU)` while `mm->allocstart` is `NULL`.
- The function early-returns at [pdgui_charpreview.c:641](port/fast3d/pdgui_charpreview.c:641) when `mm->curparams == 0` without clearing `s_PreviewRequested`. Comment says "Keep s_PreviewRequested alive for next frame" -- intentional retry. But the next frame never succeeds because the master loader keeps preempting.
- The master loader's auto-acquire at [bondgun.c:4232](src/game/bondgun.c:4232) wins every timer flush because it runs first in `playerTick` while charpreview runs later in the render path.
- The BONDGUN-out side effect at [bondgun.c:3789-3791](src/game/bondgun.c:3789) re-arms `gunmemnew = gunmemtype`, so when the master loader gets BONDGUN, it has work to do (load the same weapon) and runs a full cycle.

Result: 1825 full master-load cycles in 20 seconds. `gunctrl.handmodeldef` toggles between NULL (FLUX phase, ~85% of frames) and `0x0000023a42b6cec0` (HANDS-onward phases, ~15%). The hand-render gate at [bondgun.c:11368](src/game/bondgun.c:11368) sees `NULL` most frames -> no hands rendered. The weapon model fares similarly because `bgun0f0a5550`'s matrix setup runs against the perpetually-resetting model and never settles.

### How the random spawn weapon connects

The CS setup screen with "spawn weapon = random" likely leaves a `pdguiCharPreviewRequest` pending when the match starts. Without the round-7 guard, the renderer keeps calling `bgunChangeGunMem(INVMENU)` every frame in active gameplay, fighting the master loader. Different random selections (or different selected weapons) would all hit the same race, with stronger or weaker visible symptoms depending on which weapon and how often the user looks down.

### Mike's boundary observation (open follow-up)

> "I think it may also occur with random weapon set options also"

Filed as open question: does `random weapon set` (per-pickup randomisation) hit the same charpreview-vs-master-loader race when each pickup triggers a different weapon load? Plausible mechanism is identical -- a queued preview at the moment a different weapon spawns nearby would kick off the BONDGUN-out cycle and stall rendering until charpreview gives up. Not investigated this round; capture for the next time someone repros under that config.

### Round-7 fixes landed

1. **`playermgrCreatePlayer` handmodeldef / cartmodeldef latent uninit**. [src/game/playermgr.c:417-426](src/game/playermgr.c:417). The pointer fields were never zero-initialised at player creation; `mempAlloc` does not zero-fill. Round-6 saw `bgunReset` reading `handmodeldef=0x7c7b663545bbcbd1` (stale heap garbage, same value across all spawns) at every match start. First `bgunEnterFlux` cleared it harmlessly but the latent uninit was a real bug class. Same fix shape as the visionmode treatment: unconditional `NULL` init, no `VERSION` gate.

2. **Charpreview master-loader race (PRIMARY FIX for Mike's repro)**. [port/fast3d/pdgui_charpreview.c](port/fast3d/pdgui_charpreview.c). Two layers:
   - **Active-gameplay bail**: if `g_Vars.players[0]->haschrbody && g_Vars.mplayerisrunning`, drop `s_PreviewRequested` and return immediately. In active MP gameplay there is no legitimate consumer of in-match charpreview, so dropping the request is safe and breaks the fight loop.
   - **Bounded retry**: if pool acquisition fails for 30 consecutive frames (~0.5 s), give up by clearing `s_PreviewRequested`. Catches any path that bypasses the active-gameplay guard (e.g. SP cutscene overlay).

3. **Log throttling on the round-6 high-volume sites**. Round-6 produced 9201 / 4366 / 1453 entries from `playerRemoveChrBody` / `bgunChangeGunMem` / `bgunFreeGunMem` per match because they were logged every call. Round-7 throttles:
   - `playerRemoveChrBody`: log only when `was_haschrbody=1` (real work, not in-MP no-op).
   - `bgunChangeGunMem enter`: log only on `(cur_owner, new_owner)` pair change.
   - `bgunChangeGunMem exit`: log only on result=1 timer-complete with owner-change. Drop the per-call result=0 spam.
   - `bgunFreeGunMem`: log only when `gunmemowner != FREE` at entry.

4. **`gunmemnew` range-watch**. [src/game/bondgun.c bgunChangeGunMem](src/game/bondgun.c). Round-5 saw a transient `gunmemnew=-1314284637` at 00:50.85 (twice). Round-7 adds an explicit warn-log when `gunmemnew` is outside `[-1, 100]` at `bgunChangeGunMem` entry, deduped by value so a corrupted state doesn't flood. If the wild value reappears, the log captures cur_owner / new_owner / gunmemtype / gunmem to bracket the writer.

### Round-7 build verification

| Field | Value |
|---|---|
| Build cmd | `source devtools/build-env.sh && ninja -C Build pd pd-server` |
| Result | exit 0 |
| `Build/PerfectDark.exe` | 56 222 172 bytes (2026-04-26 15:18) |
| `Build/PerfectDarkServer.exe` | 23 272 107 bytes (2026-04-26 14:19, unchanged from round-6 -- none of round-7 changes are linked into the server target; verified via `ninja -t commands pd-server` returning no matches for `playermgr.c / bondgun.c / player.c / charpreview`) |

### Candidate matrix update

| ID | Status this round |
|---|---|
| H-NULL-A through H-NULL-D | All RULED-OUT for stage-2 in-match window with weapon=2 -- the load chain was completing 1825 times per match |
| H-MTX-A/B/C | Still NEEDS-DATA -- no fire frames in the no-hands log |
| H-INPUT-A | RULED-OUT for this repro -- Mike could not fire because there was no usable weapon, but the input chain itself was not implicated |
| H-CTX-A through H-CTX-D | All RULED-OUT or N/A (no Forge transitions in this log; visionmode round-6 fix held; bgunReset showed expected gunmemowner=2) |
| H-FLUX-A | EVOLVED -- bgunEnterFlux was firing 5477 times in the in-match window. Not the cause itself but the symptom of the charpreview race. The new candidate is the master-loader-vs-charpreview pool fight. |
| **H-RACE-A (NEW)** | **CONFIRMED** -- charpreview's INVMENU request perpetually preempted by master loader's auto-acquire BONDGUN call. Cite: 1824x each of pattern-1/2/3 in `bgunChangeGunMem enter` shapes; 1825x master load CARTS->LOADED cycles in 20 s. Round-7 active-gameplay bail breaks the loop. |

---

## 12. Round-8 -- bone-snapshot instrumentation for idle-vs-fire asymmetry (2026-04-26)

### Mike's observation that opened the round

> "The animations such as firing and reloading have the weapon in the proper place in the hand, but not when just holding the gun normally"

> "It was the case for base game weapons also, such as the Farsight when I tested earlier."

The asymmetry is universal: idle = wrong, fire / reload = right. Holds across stock PD weapons (FARSIGHT, DY357MAGNUM observed in playtest logs) and AllInOne imports. HASHANDS-set vs HASHANDS-unset both show the symptom.

### Round-7 fire-frame log evidence (Build/pd-client.log timestamped 17:05)

12 fire-handler events with `R(wpn=8 inuse=1 visible=1 state=0)` for DY357MAGNUM. Matrix-path branched cleanly at every fire transition:

| Phase | state / sm | animmode | a0 | useposrot | posoffset | rotxoffset |
|---|---|---|---|---|---|---|
| Idle pre-fire | 0 / 0 | 0 | 1 (cached) | 0 | (0, 0, 0) | 0 |
| Fire enter | 4 / 0 | 0 -> 2 | 1 -> 0 (fresh anim) | 0 | (0, 0, 0) | 0 |
| Fire mid (recoil) | 4 / 2 | 2 -> 0 | 0 -> 1 | 1 | (~0.04..0.08, 0, ~1.7..8.3) | ~5.85..6.19 |
| Fire decay | 4 / 2 | 0 | 1 | 0 | (0, 0, 0) | 0 |
| Idle post-fire | 0 / 0 | 0 | 1 (cached) | 0 | (0, 0, 0) | 0 |

Cache fill events both at `animnum=0 animframe=0.00` (true rest pose). `useposrot=1` recoil offsets are sub-unit-to-single-digit, too small to be the source of "weapon snaps to correct place" during fire. The correction must come from FIRE / RELOAD animation tracks producing different bone matrices than IDLE.

### Live candidate space (each survives until its own evidence rules in or out)

| ID | What "fits asymmetry" requires |
|---|---|
| H-MTX-A2 | IDLE rest-pose data wrong; cache faithfully reproduces wrong; fire-fresh-anim eval uses different keys |
| H-IDLE-POSE-A | Same shape as A2 but pinned at the asset-side (anim track wrong) rather than the runtime-side |
| H-HAND-RIG-A | Hand rig idle pose places hand bone wrong; gun rides along via shared matrix buffer ([bondgun.c:11371](src/game/bondgun.c:11371)) |
| H-PARENT-A | Mike's wrong-bone hypothesis. Fits ONLY IF PD's animation evaluator writes absolute bone matrices that bypass parent inheritance during animations. If the eval writes locals, parent miswiring would persist in animations -- contradicting the observed asymmetry |

### Round-8 diagnostic landed

[src/game/bondgun.c bgun0f0a5550](src/game/bondgun.c) -- a `bone-snapshot` log emitted on first IDLE-state frame after FIRE and first FIRE-state frame after IDLE. Player 0, RIGHT hand only. Captures:

- `gun_root_t` -- gun model matrix[0] translation (`m[3][0..2]`)
- `gun_root_r0` -- gun model matrix[0] first row (`m[0][0..3]`)
- `hand_root_t`, `hand_root_r0` -- hand model matrix[0] equivalents
- `gun_hand_idx` -- gun model's `MODELPART_HAND_RIGHT` modelnode matrix index (where the gun expects to attach)
- `gun_hand_t` -- the matrix at that index (what the gun model thinks the right-hand bone position is)
- `gun_def`, `hand_def` -- pointer values, validation
- `mtx_alias` -- 1 if gun and hand models share matrix buffer (expected per [bondgun.c:11371](src/game/bondgun.c:11371))
- `nummtx_gun`, `nummtx_hand` -- matrix array sizes per modeldef
- `useposrot`, `animmode`, `animnum`, `animframe`, `frame` -- cross-reference fields

### Read interpretation when Mike's playtest log arrives

| Pattern in log | Discriminator implication |
|---|---|
| `gun_root_t` and `hand_root_t` translation differs significantly between IDLE phase and FIRE phase | Anim tracks for the two phases produce different absolute bone positions -- consistent with H-IDLE-POSE-A or H-PARENT-A (anim absolute writes mask the parent issue) |
| `hand_root_t` shifts in lockstep with `gun_root_t` between phases | Hand rig itself moves; H-HAND-RIG-A consistent |
| `hand_root_t` stable while `gun_root_t` shifts | Gun-only issue; H-HAND-RIG-A demoted, H-IDLE-POSE-A or H-PARENT-A consistent |
| `gun_hand_t` (gun model's MODELPART_HAND_RIGHT) shifts between phases | Animation is repositioning the bone the gun is attached to -- consistent with H-PARENT-A IF the parent semantics work that way |
| `mtx_alias=1` confirmed | Expected; gun and hand share matrix buffer. If `gun_root_t != hand_root_t` while alias=1, they index different matrix slots within the shared buffer (different "roots" in their respective node trees) |
| `useposrot=0` at IDLE samples and `useposrot=1` at FIRE samples | Cross-references the round-7 evidence. Recoil offsets confirmed to be sub-unit. If gun_root_t difference between phases is much larger than the useposrot offset magnitudes, the correction comes from the anim track, not from posoffset/rotxoffset |

### Round-8 build verification

| Field | Value |
|---|---|
| Build cmd | `source devtools/build-env.sh && ninja -C Build pd pd-server` |
| Result | exit 0 |
| `Build/PerfectDark.exe` | 56 225 790 bytes (2026-04-26 17:33) -- size grew ~3.6 KB vs round-7 (56 222 172) consistent with the new diagnostic code |
| `Build/PerfectDarkServer.exe` | unchanged from round-7 (none of the round-8 changes are linked into the server target) |

### What this round does NOT do

- No fix candidates proposed.
- No animation-asset inspection. Discrimination relies on the runtime matrix dump.
- No second instrumentation layer. The `bone-snapshot` is intentionally targeted; if it does not fully discriminate the candidates, a follow-on round can dump bone matrix per node or instrument `modelSetMatricesWithAnim` directly.
- No SP / SP-cutscene exercise. The diagnostic only fires for player 0 RIGHT hand in active gameplay where `hand->visible` is true.

---

## 13. Round-10 -- headless characterisation via pd-tests (2026-04-26)

### Round-10 fire-frame playtest discriminator data

`Build/pd-client.log` 23:05 ran with F2 helper from round-9, captured 7 fire-handler events for FALCON2 (wpn=2), 15 round-8 bone-snapshot pairs at IDLE / FIRE transitions.

**Diagnostic limitations surfaced by the data:**

- `gun_hand_idx=-1` at every snapshot. `MODELPART_HAND_RIGHT` does not exist on the gun model -- the H-PARENT-A discriminator returns NULL.
- `mtx_alias=1` always. `gun_root_t` and `hand_root_t` read the same matrix slot; they cannot differ. The H-HAND-RIG-A lockstep discriminator is degenerate.
- `gun_root_t` deltas across IDLE / FIRE were sub-unit. Matrix index 0 is most likely a stable upstream transform, not the bone driving the visible weapon position.

**The smoking-gun column was elsewhere:**

| Diagnostic | animnum | animframe |
|---|---|---|
| `cache_fill wpn=2` (when `unk0dd8` was populated) | 0 | 0.00 |
| `bone-snapshot phase=IDLE wpn=2` (every IDLE sample, 8/8) | 236 | 17.00 |
| `bone-snapshot phase=FIRE wpn=2` (every FIRE sample, 7/7) | 236 | 1.00 |

The cache was populated when the gun was at anim 0 frame 0 -- the model's default-skeleton T-pose. Idle hold is at anim 236 frame 17. Fire is at anim 236 frame 1. **The cache encodes a different anim pose than every observed IDLE frame.** Idle path `a0=true` reads the cache (T-pose oriented by `sp2c4`). Fire path `a0=false` runs fresh `modelSetMatricesWithAnim` against the actual current anim. This produces the observed asymmetry.

### Phase A landed (2026-04-26)

Per Mike's "Option C, can you test this via our new test method?" directive, Phase A pivots from a wider playtest diag to a **headless pd-tests characterisation**.

Phase A introduces a pure predicate that the Phase B fix will wire into the matrix-cache decision in `bgun0f0a5550`. The predicate is locked by Catch2 tests; the wiring is deferred so this commit is behaviour-preserving.

**Files added:**
- [port/include/bondgun_cache.h](port/include/bondgun_cache.h) -- declares `bgunMatrixCacheIsStale`
- [port/src/bondgun_cache.c](port/src/bondgun_cache.c) -- defines the predicate. Currently returns true when (a) `cache_animnum != cur_animnum`, or (b) same animnum but frame drift >= half-frame epsilon. Otherwise false
- [tests/test_bondgun_cache.cpp](tests/test_bondgun_cache.cpp) -- 6 Catch2 cases (619 assertions, including a 0..60 anim-progress sweep) that lock the predicate spec. Tagged `[bondgun][matrix-cache][regression]`

**CMake:**
- `port/src/bondgun_cache.c` is auto-discovered by the existing `GLOB_RECURSE port/*.c` for the `pd` target
- Explicitly added to `SRC_TESTS` list for `pd-tests` target

**Build verification:**

| Target | Result | Bytes |
|---|---|---|
| `pd` | exit 0 | 56 267 522 |
| `pd-server` | exit 0 | 23 274 010 |
| `pd-tests` | exit 0 | 14 330 032 |
| `pd-tests` runtime | All tests passed (1389 assertions in 83 test cases) -- up from 770 / 77 pre-Phase-A. The new bondgun-cache filter `[bondgun]` reports 619 assertions in 6 test cases | n/a |

Phase A is observation-only at runtime: `bgunMatrixCacheIsStale` is not yet called from `bgun0f0a5550`. The bug Mike sees in playtest is unchanged. The predicate exists, ready for Phase B activation.

### Phase B fix proposal (surfaced for Mike's approval, not auto-merged)

**Wiring shape.** Two parts to the Phase B activation:

1. **Capture cache anim state at fill time.** In `bgun0f0a5550` cache-fill (the `unk0dd4 == -1` branch around bondgun.c:8463), record `cache_animnum = modelGetAnimNum(&hand->gunmodel)` and `cache_animframe = modelGetCurAnimFrame(&hand->gunmodel)`. Store somewhere persistent -- options:
   - Add two fields `s32 cached_animnum` and `f32 cached_animframe` to `struct hand` (player.hands[handnum]). Minimal struct surface change; matches the pattern of `unk0dd4` and `unk0dd8` already living on `struct hand`.
   - Add static globals indexed by player num. Less clean; rejected.

2. **Check predicate at the a0 decision.** Around bondgun.c:8341 where `bool a0 = true;` is initialised, add:
   ```
   if (a0 && handnum == HAND_RIGHT
           && bgunMatrixCacheIsStale(
               player->hands[HAND_RIGHT].cached_animnum,
               player->hands[HAND_RIGHT].cached_animframe,
               (s32)modelGetAnimNum(&hand->gunmodel),
               modelGetCurAnimFrame(&hand->gunmodel))) {
       a0 = false;
   }
   ```
   When the predicate returns true, the cache is stale and the fresh-anim path runs instead. Idle frames now produce the actual current-anim pose, matching what fire / reload already do.

**Scope.** All weapons. The fix is in the shared FP-render path so every weapon's idle animation will render its anim's actual current frame instead of the cached-at-fill-time pose.

**Side effects:**
- Per-frame `modelSetMatricesWithAnim` cost replaces the cached `mtx00015be4` per-matrix multiply on idle frames where the cache is stale. At full anim mismatch the cost is the same as fire frames (which already run fresh per-frame). At sub-half-frame matched state the cache is still used, preserving the perf for the common case where idle anim genuinely holds at one frame.
- `cached_animnum` / `cached_animframe` add 8 bytes to `struct hand`. The struct is already several KB; this is negligible.
- The `unk0dd8` buffer continues to be populated and used when the predicate returns false (cache is fresh). No code is dead-pathed.

**What we'd verify after Phase B lands:**
- Mike playtests with FALCON2 idle hold + a fire + a reload. The weapon stays in the same position across phases. No "snap to correct on fire, snap back to wrong on idle".
- The round-10 fire-frame playtest pattern (cache at anim 0/0, idle at anim 236/17) is no longer asymmetric -- bone-snapshot data should show idle and fire matrix positions converge.
- Other weapons (FARSIGHT, DY357MAGNUM) exhibit the same fix.
- pd-tests `[bondgun][matrix-cache]` cases continue to pass (the predicate wasn't changed, only its callers).

**Risks flagged for Mike's review:**
- Some weapon special case might depend on the cache being stable (e.g., a weapon-specific path that compares cached vs current matrix). I did not find such a path in `bgun0f0a5550`; flagging.
- The original PD code lived without this check. Possibly the IDLE animation in original PD was always anim 0 frame 0 (T-pose was the IDLE pose by authoring convention), and the cache was correct for that authoring choice. PD2 / AllInOne may have different idle-anim conventions where the cache became stale by accident. The Phase B fix is "drop a stale optimisation," not "delete a feature."

**Alternative considered: drop the cache entirely (always fresh).**
Equivalent symptom resolution; smaller diff (`bool a0 = false;` initial value, deletes the cache-multiply branch). Trades all idle frames' cached-multiply cost for fresh-eval cost. On modern hardware this is imperceptible. Slightly less surgical than the predicate approach but eliminates a class of staleness bugs entirely. Surfacing for Mike's call.

### What Phase A does NOT do (regression boundary)

- Phase A does NOT change runtime behaviour. The bug Mike sees in playtest is unchanged until Phase B activates the predicate.
- Phase A does NOT inspect anim 236's keyframe data. If H-MTX-A1 turns out to be wrong and the symptom persists post-Phase-B, the demoted candidates (H-MTX-A2 / H-IDLE-POSE-A / H-HAND-RIG-A / H-PARENT-A) come back as LIVE.
- Phase A is a pure-function regression test. The state-machine harness queued in the design doc Section F (cohort 2) remains queued. If Phase B's wiring needs additional state-machine coverage, that scope expands at Phase B time.
