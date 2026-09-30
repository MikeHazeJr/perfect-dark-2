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

## September 29 model lifetime follow-up

Ordinary held loading pins compiled model/texture storage until player/stage
reset. Custom model generations additionally pin their private slot's catalog
identity until their final release; reset and snapshot rollback cannot rebind
that live identity. Client/tests, native123/4163, dependent body/head8/322,
native guard and installed38/38 pass on 2,011 unchanged recorded sources.
Receipt: context/audits/2026/model-slot-lifetime-2026-09-29.md.

This does not solve the v2 queued-load source-version boundary: two retained
generations of the same catalog ID share the private identity slot. Resolving
that filenum through the mutable catalog can still select the current source
instead of the equipment's retained source. The v2 held adapter must route
directly to its retained model generation. Weapon slot classification lifetime
and the remaining ordinary catalog/idle/hand/deferred-hit seams remain open.

## September 29 retained equipment batch (validated seams, incomplete gameplay)

Equipment now carries an exact leased modeldef alongside its source identity
and closure hash. bgunQueueRetainedModelLoad validates the real generation,
takes a stage pin before request publication, and the ordinary bgunTickGunLoad
consumes that exact source before any mutable catalog handle check. Queued old
public image source versus newly loaded same-ID edited source is exercised in
the installed harness. Legacy queue/body-hand/reset paths clear this request.

Each custom equipped generation pins its exact allocated private weapon ID.
Reset retires MP reservations while retaining occupied identities; rollback
preflights all pinned identities and never rolls back live reference counts.
Deferred gset copies keep this pin until their final source release. Finished
hands can clear selection without allocating or destroying the registration.

Optional equipped.modes is an explicit two-element array of null or
{ammo_slot:-1|0|1}; primary metadata is required when the field is present.
Native idle functions have type NONE, silent noise, no fire animation, and no
selected action. Reload metadata is authored independently of graph branches.
Source-only older preparation may omit modes and the direct pointer; the
production v2 adapter must require both. No first-node scalar projection.

Remaining integration: captured-archive v2 scanner admission, immutable catalog
publication, ordinary weapon/hand equipment selection, input/idle/action host,
exact scoped shot/noise gsets and deferred native hit wake, owner teardown,
mode-transition guards, and actual played graph evidence. Do not classify
these new supported seams as full ordinary v2 gameplay acceptance.

Client/tests0, native143/4754, guard0, installed39/39, source2012 drift0.
Receipt: context/audits/2026/equipment-native-lifetime-2026-09-29.md.
Production idle admission must evaluate graph routes independently of neutral
reload metadata: a branch using another equipped ammo slot cannot be hidden
by the idle slot's empty-clip logic. Native action acceptance checks the actual
selected action ammo; debit stays in the existing native fire lifecycle.

Client/tests0, native143/4754, guard0, installed39/39, source2012 drift0.
Receipt: context/audits/2026/equipment-native-lifetime-2026-09-29.md.
Production idle admission must evaluate graph routes independently of neutral
reload metadata: a branch using another equipped ammo slot cannot be hidden
by the idle slot's empty-clip logic. Native action acceptance checks the actual
selected action ammo; debit stays in the existing native fire lifecycle.
