# Held graph production seams, September 27

Implementation contract for T-MODDING-002 under the existing D-006A decision.
Source-traced during the shared menu/MP validation hold; no gameplay acceptance.
This supplements, and does not replace, the full September 23 closure plan.

## Newly confirmed admission and delivery gaps

`wgV2EquippedWeapon` deliberately exposes no default `functions[]`. Installing
that object alone cannot work: `weaponGetNumFunctions` counts those pointers,
`weaponGetFunction` returns them, and `bgun0f098ca0` returns -1 for a missing
function before `bgunTickIncIdle` admits an attack. A selected branch must not
be installed as an arbitrary default just to satisfy these callers.

The integration must distinguish mode availability and idle equipment/reload
policy from the selected firing action. Audit direct `weapon->functions[]`
reads as well as accessors. Idle queries must not evaluate graph events or
create a native shot. Multi-action graphs must retain their branch semantics.

`bgun0f09a6f8` sets `hand->firing` and debits ammunition during the native
attack lifecycle. Actual hit processing occurs later through `handsTickAttack`,
`handTickAttack`, `shotCreate`, and `shotCalculateHits`. Therefore executor
completion cannot discard or replace a selected action that still owns a
pending native firing delivery. Capture its immutable generation/action before
delivery and retain it through every consumer, including noise and damage.

## Required production seams

| Seam | Current producer/consumer | Required integration |
|---|---|---|
| Catalog prepare/publish | `assetcatalog_load.c:s_catalogActivateWeaponGraphRuntime` | Version-dispatch captured public source; prepare complete equipment and all dependencies; publish atomically with catalog lifecycle. |
| Catalog detach/replacement | `s_catalogClearWeaponGraphRuntime`, activation ledger retirement | Retire exact graph generation and hand bindings before releasing owned storage; failed replacement preserves old source. |
| Mode/idle metadata | `game_0b0fd0.c:weaponGetNumFunctions`, `weaponGetFunction`; `bondgun.c:bgun0f098ca0` | Separate idle policy from action selection. Never project the first branch into native defaults. |
| Physical press | `bondgun.c:bgunSetTriggerOn` | Per-hand monotonic press identity; queue once on edge and release pending input independently from active work. |
| Idle admission | `bondgun.c:bgunTickIncIdle` | Perform native switching first; one visit admits physical press or held-ready export, without legacy firing fallthrough. |
| Native action | `bgunTickIncAttack`, `bgunTickIncAttackingShoot`, `bgun0f09a6f8` | One native owner of animation, ammo debit, sound and firing; executor advancement must not double-tick the native state machine. |
| Deferred dispatch | `prop.c:handTickAttack`, `shotCreate`, `shotCalculateHits` | Retain the selected action through delivery and completion; reject stale delivery without substituting the current hand/weapon action. |
| Temporary gset copies | `gsetPopulateFromCurrentPlayer` callers in `bgunDecreaseNoiseRadius`, `shotCalculateHits`, `handTickAttack` | Explicit destination scopes and `wgV2GsetCopy`; close every exit. Preserve existing Mauler/Laser byte transformations for v1. |
| Selected reads | `gsetGetWeaponFunction`, `gsetGetWeaponFunction2`, `bgunGetHeldGraph` | Consult exact registered selected action for v2; no fallback to v1 or current-player guesses. |
| Hand/player retirement | `bgunFreeWeapon`, `bgunHandlePlayerDead`, player allocation/reset | Explicit owner generation; cancel, clear animation pointers, close action/copy leases, then retire source/model storage. |
| Native slot identity | weapon/model registry and `wg_v2_equipped_model` | Pin reusable numeric slots while any graph or copied action refers to them; modeldef generation retention alone does not prove file-slot retention. |
| Unsupported mode transitions | network, bot and Theater entry points | Check existing active selections as well as new preparation for the initial offline held profile. Later profiles must implement those modes fully. |

`gsetPopulateFromCurrentPlayer` has four calls across three consumer functions:
two noise snapshots, one hit-test snapshot and one attack-dispatch snapshot.
`chrDamage`'s local `gset2` is a separately initialized fallback when no gset
was supplied, not a copy of the selected shot. Do not attach a hand action to it.

## Required proof for the complete first unit

Base single-shot and edited branching mod archives must use ordinary catalog
activation, equip, physical input, native attack and actual hit processing.
Prove false gates cause zero debit/fire/audio; primary/secondary and both hands
remain independent; delayed copied damage stays tied to its original action;
reload, switch, death, source replacement and stage reset retire every lease.
Specifically test completion/branch advancement between native firing admission
and `handsTickAttack`, preventing stale action selection or duplicate debit.
Harness/API tests remain supporting evidence, not a substitute for those runs.

## Ownership and publication batch applied September 27

Selected gsets now retain their exact catalog entry as well as the native
action bundle. This keeps equipment, reload commands, model and eventual slot
leases alive through delayed copies even after catalog/hand retirement. Exact
source lookup returns that retained generation for subsequent ammo consumers.
Base overwrite, scope closure and owner retirement release the whole chain.

Complete archive preparation now prepares primary and optional secondary from
the same captured ZIP, shares one model lease, and transfers that lease only
after both candidates succeed. Complete mode publication validates every
candidate before mutation, replaces both slots before retirement callbacks,
and fences removed secondary modes against older candidates resurrecting them.
These APIs still need the production catalog/hand/shot wiring in the table;
this batch is not ordinary gameplay completion. Client/updater/tests builds,
108 combined graph/c3842 cases (4,018 assertions), and native-source guard pass.
No changes across 2,010 recorded source/header/test/CMake files. Receipt:
context/audits/2026/graph-ownership-2026-09-27.md.
