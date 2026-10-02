# Graph operation inventory - October 1, 2026

Workbench: T-MODDING-010, dependent on T-TOOLING-008 and T-MODDING-002.
Owner: codex-0e8900ca-467b81e8 / graph_inventory. Model: gpt-6.1-sol.
This is the approved initial inventory and reference-checking unit. No production
C/C++ behavior was changed and no graph migration/parity completion is claimed.

## Deliverables and interpretation

[The machine-readable ledger](graph-operation-inventory-2026-10-01.json) contains
27 operation boundaries: 22 native behavior/lifecycle boundaries and five
explicit domain dependencies. Each row records proposed module names, typed
parameters and units, inputs/outputs, state ownership, phases, cancellation,
timing, authority/determinism requirements, catalog references, generation and
versioning requirements, source evidence, migration status and unresolved
semantics. `observed`, `proposed` and `unresolved` parameter classifications
distinguish current source facts from candidate authoring contracts. No unknown
unit or base-user parameter value is inferred from a field name.

The ledger reuses [Weapon Graph Module Parameters](weapon-graph-module-parameters.md)
and [Held Graph Production Seams](held-graph-production-seams-2026-09-27.md).
Its native source snapshot covers 67 references to 53 distinct function
definitions in seven source files. It stores current line anchors, lexical
globals/state fields/timing tokens and callers found in those reviewed files.
Those counts describe the reviewed inventory, not total game functions or graph
nodes, behavioral coverage percentages, or an exhaustive call graph.

| Cohort | Boundaries | Principal native seam |
| --- | ---: | --- |
| Base firing and owner/source lifetime | 7 | `bgunSetTriggerOn`, `bgunTickIncIdle`, `bgun0f09a6f8`, `handTickAttack` |
| Ammo/reload/pickup | 2 | `bgunTickIncReload`, `ammoHandlePickup`, `propPickupByPlayer` |
| Fired/thrown carriers and deployed entities | 4 | `bgunCreateFiredProjectile`, `bgunCreateThrownProjectile`, `weaponTick`, `autogunTick` |
| Motion/contact/guidance/timers | 4 | `projectileTick`, `rocketTickFbw`, `projectilesUnrefOwner` |
| Melee/special/devices | 3 | `bgunTickIncAttackingMelee`, `handInflictMeleeDamage`, `currentPlayerSetDeviceActive` |
| Presentation | 2 | `bgunStartAnimation`, gunvis commands, `bgunCreateFx`, recoil/noise |
| Additional domain dependencies | 5 | Menu, AI, world, networking, Theater |

Four rows have `v2_custom_offline_partial` status: input admission, native fire
commit, deferred hit delivery, and lifecycle retirement. Four are `native_only`:
charge, beam, reload, reserve/pickup. Fourteen have existing native/v1 parameter
adaptation (`v1_parameter_adapter`), and five are dependency seams. A v1 adapter
does not establish executable topology authority. These are operation-boundary
classifications, not a promise that every parameter in a row has an adapter.

## Current production boundary

`held_single_shot.v1` consumes captured public v2 source and executes authored
routes for offline private custom weapon slots. Base numeric-slot policy still
needs migration. Network/bot/Theater use and unsupported variables/shared context/
presentation bindings are rejected. Automatic/burst, beam, charge, thrown,
melee, device and entity profiles have no complete executable-v2 parity proof.

The previous production receipt is retained in
[the September 29 cutover audit](../../audits/2026/graph-production-cutover-2026-09-29.md):
148 native cases/4,941 assertions and installed native gate 41/41. That gate
acknowledges native shots explicitly; it does not prove ordinary world hits or
rendered/controller gameplay. This inventory did not rerun those receipts.

Reusable named native operations are valid graph modules. Their graph routes
must choose behavior and own accepted lifecycle transitions; converting each C
function automatically into a node or exposing only scalars does not meet D-006A.
One C function may contain several operations and hidden policies, while one
operation may span input, hand state, deferred delivery and prop cleanup.

## Actionable migration dependencies

1. Resolve base held admission/equipment policies and preserve idle versus
   selected-action identity. Extend the existing immutable action/gset/source
   lifetime contracts into ordinary world-hit proof before claiming base parity.
2. Normalize cadence/burst/charge/beam and ammo operations separately. Preserve
   the native sole debit, fractional-shot remainder, integer charge-step debit,
   incremental reload, dual-hand timing and trigger-release continuation.
3. Separate carrier spawning from motion/contact and entity transition. Define
   authored authority, owner/target generation, timer sentinel states, pickup,
   attachment, remote signals and deployed-owner cleanup. Fired speed conversion
   and unnamed friction/recoil fields are unresolved semantic work, not defaults.
4. Map melee/devices and presentation across all consumers. Device activation
   also comes from menus/movement/AI; noise affects AI; camera/overlay/loop audio
   outlive a single hand command. Shared contexts and authored presentation
   bindings need supported versioned contracts rather than hidden side effects.
5. Expand menu, AI/world/mission, network and Theater operation inventories.
   Complete base behavior needs these domains; they are dependencies, not scope
   exclusions. Define authoritative ordering, prediction, RNG streams, protocol
   identity and restart-safe replay source closure before admitting those modes.

Every migration unit needs its own public-source-to-production test and native
behavior/parity fixtures, including cancel/switch/death/replacement/stage reset,
dual-hand and copied payload consumers. Keep cross-asset identities as real
catalog strings and generated runtime products as rebuildable source-hashed
cache. This ledger is engineering evidence, not an additional authored runtime
representation or a public typed archive.

## Maintaining the inventory

The read-only checker is [tools/graph-operation-inventory.py](../../../tools/graph-operation-inventory.py).

```powershell
python -B tools/graph-operation-inventory.py
python -B tools/graph-operation-inventory.py --scan
python -B -m unittest discover -s tools/tests -p test_graph_operation_inventory.py -v
```

The default check validates required contracts, typed parameter metadata,
operation IDs/migration statuses, repository-relative source paths, uniquely
identified definitions and reviewed identifier anchors. `--scan` emits current
JSON evidence to stdout without changing source or the ledger. Refresh the
dated snapshot only after reviewing semantic changes; the checker does not
decide module boundaries or implement generated nodes. Use the coordination
test FIFO for verification runs.

The extractor masks comments/literals and preserves offsets/lines. It includes
all textual preprocessor arms without evaluating a build configuration; only
`handInflictMeleeDamage` currently needs its explicitly flagged column-zero end
fallback because alternative branches have unequal lexical braces. It does not
resolve indirect/function-pointer calls, macros, cross-file callers outside the
seven reviewed files, actual build reachability, or the semantics of numeric
flags. A reference PASS proves source anchors exist, not gameplay parity.

## Verification receipt

Final corrected test-runner FIFO `q_20261001033352643_63ca48b5`:
11 synthetic Python tests pass (0.016 seconds); the source-reference check exits
0 with 27 operations/67 references/seven files, and the bounded scan succeeds.
Tests cover comments/literals, moved line anchors, missing reviewed tokens and
definitions, path escape/absolute paths, duplicate IDs/definitions, malformed
units/statuses, C++ extern definitions, preprocessor alternatives and else-if
boundaries. `asset_native_source_guard.py` independently exits 0 in the preceding
FIFO. Earlier reference failures were repaired; its second FIFO's inaccurate
PASS summary is superseded by this actual final output. No native build, game,
capture, extraction, broad hashing, Git or publication operation ran.

Remaining work is full domain inventory and executable graph migration/parity,
not a permission or tooling blocker for this initial inventory unit.

### October 2 publication verification

The resumed session `01a0f5c1-d031-77b0-a372-3391765df0d5` (model gpt-6)
rechecked this unit in lightweight-test FIFO
`q_20261002020141913_64957e86`: all 11 tests pass, the source checker passes
27 boundaries/67 references/seven files, and the native-source guard passes.
Logs are retained under `.claude/pd-initial-integration/20261002-night/`.
This verifies the inventory and its drift checker; executable graph migration
and native parity retain their separate gates.
