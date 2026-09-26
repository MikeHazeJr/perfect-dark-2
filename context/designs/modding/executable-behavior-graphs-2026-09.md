# Executable modular behavior graphs

Status: Mike explicitly selected **D-006 option A: executable modular graphs**.
Implementation is in progress. The executor and native action/equipment preparation
exist; batch19 installed21 verifies the real command/audio dependency resolver and
retained source generations. Complete equipped publication, gameplay integration
and all-family parity remain open. The source survey below records the original
2026-09-05 findings and is historical where later implementation supersedes it.

## Goal and review boundary

The user wants complete extraction and production use of base-game assets, standard
editable sources for mods, and complete, sensible, modular behavior graphs. This
proposal addresses the graph execution portion of that whole goal. Completing the
first held-weapon slice below would not complete D-006, T-MODDING-002, extraction
parity, all-family asset loading, or gameplay validation.

Selected direction: **D-006A: executable modular graphs**. Authored events, connections, gates,
parameters, and shared state should determine production behavior. Existing native
routines can remain explicit named modules when they preserve the intended behavior;
they must not execute through a hidden identity-based bypass.

Mike explicitly authorized executable user-authored events, connections, gates and
state driving reusable native modules. That choice authorizes implementation;
no further architecture-choice confirmation is pending. Keep runtime evidence
separate from source/component verification.

The active [public-source constraint](../../constraints.md) applies throughout:
editable archive sources are authoritative, and compiled programs are source-hashed,
rebuildable cache. Do not introduce a second hand-maintained runtime representation.

## Verified current contracts

Paths and line numbers below describe the source inspected on 2026-09-05. Function
names are the durable lookup anchors if later changes move a line.

| Family | Producer and retained source | Current consumer and execution limit |
| --- | --- | --- |
| Held weapons | `port/src/romextract_pdweapon.c:2277`, `s_emitWeaponModeGraphFile`, emits `pd.weapon_graph.v1` with one trigger node, one action node, trigger-to-action edge, and an export pointing at the action. `:2329`, `s_buildWeaponSharedContextJson`, emits four contexts. | `port/src/weapon_graph_runtime.c:3937`, `weaponGraphRuntimeRegisterHeldIr`, selects exported/mode action nodes into one record per function. Authored event and gate traversal does not select execution. `src/game/bondgun.c:1805`, `bgunGetHeldGraph`, reads the projected record. |
| Projectiles | `romextract_pdweapon.c:1716`, `s_writeProjectileArchive`, emits `pd.projectile_graph.v1`: spawn state, motion, optional guidance/bounce nodes and edges. The `main` export points to motion or a guidance node, not spawn state. | `weapon_graph_runtime.c:4865`, `weaponGraphRuntimeRegisterProjectileIrOwned`, folds every node into one record. `src/game/propobj.c:1531`, `weaponGetCustomProjectileGraph`, excludes base weapon numbers to prevent duplicate native processing. |
| Entities | `romextract_pdweapon.c:1896`, `s_writeEntityArchive`, emits `pd.entity_graph.v1` with one `entity_behavior` node, no edges, and `main` exported to that node. | `weapon_graph_runtime.c:4934`, `weaponGraphRuntimeRegisterEntityIrOwned`, folds all nodes into one record. Entity lifecycle branches include custom-only guards; shared context does not form a general mutable runtime. |
| Effects | `pd.effect_graph.v1` graph/timeline sources and `pd.effect_graph.v2` native profile libraries retain different, intentional contracts. | `port/src/effect_graph_runtime.c:327`, `effectGraphRuntimeDispatch`, already dispatches retained programs. `effect_instance_runtime.c:819/:1135/:1243` select topology, spawn, and tick instances. Native profile libraries deliberately select native machinery. This family is not a reason to create a second effects executor. |
| Props | `pd.prop_behavior.v1` accepts spawn/tick events, an enabled condition, and enabled/health/collision/channel actions. | `port/src/prop_graph_runtime.c:214/:266`, `executeFrom`/`fireEvent`, traverse event-rooted edges and stop a branch at a false condition. Its synchronous traversal provides a useful reference, but does not supply asynchronous weapon lifecycle semantics. |

The implemented node catalog is `weapon_graph_runtime.c:s_modules` and the opcode
enum is `port/include/weapon_graph_runtime.h`. It contains three held trigger events,
three held gates, ammo consume/transfer, fire/spawn/melee/special/device/presentation
modules, thirteen projectile modules, nine entity modules, nine effect modules, and
seven prop modules. General reload, projectile-tick, entity-damaged, math, loop, and
join categories described in older design prose are not all implemented opcodes.
Do not present that prose as an existing executable contract.

`weapon_graph_runtime.c:1483`, `compileEdges`, stores only `from` and `to` node
indices. It does not implement the `node.exec` pin examples in the older
[graph asset design](weapon-behavior-graph-assets.md). Shared context, subgraphs,
and ownership are retained and hashed, but that alone does not execute them.

## Production hooks to connect

| Lifecycle | Current production seam | Required relationship to graph execution |
| --- | --- | --- |
| Per-hand input | `bondgun.c:13206`, `bgunSetTriggerOn`; calls after dual-wield arbitration at `:13593` | Capture real rising/falling transitions and held state once per simulation update. `triggerreleased` is a native latch; it is not itself a one-frame release event. |
| Attack admission | `bondgun.c:1373`, idle-to-attack branch in `bgunTickIncIdle` | An executable instance's routed action controls admission. A projected export must not start an action whose gates did not pass. |
| Shoot/attack lifecycle | `bondgun.c:2525`, `bgunTickIncAttackingShoot`; `:3068`, `bgunTickIncAttack` | Start, advance, and complete the explicit native module. Preserve animation and recovery phases. Do not fire immediately inside input handling. |
| Shot readiness and debit | `bondgun.c:2063`, `bgun0f09a3f8`; `:2201`, `bgun0f09a6f8` | Preserve native cadence and commit timing; establish one owner of ammunition consumption. |
| Thrown/fired creation | `bondgun.c:5396`, `bgunCreateThrownProjectile2`; `:5830`, `bgunCreateFiredProjectile` | Create child graph instances and transfer declared owner, damage-credit, source mode, and dependency bindings. |
| Motion/contact | `propobj.c:7732`, `projectileTick`, with current contact consumers near `:7710/:8601/:8800/:9028` | Deliver the actual motion/contact phase to the active module instance. Do not fold motion, impact, and timer nodes into unconditional booleans. |
| Timer/arming/detonation | `propobj.c:5181`, `weaponTick` | Replace one complete native branch at a time with mutually exclusive instance dispatch; preserve retries such as failed detonation remaining pending. |
| Damage and release | `propobj.c:17301`, `objDamage`; `:3189`, `objFree`; `:1136/:3150`, projectile freeing | Deliver the existing lifecycle notification and retire subscriptions/continuations before native storage can be reused. |
| Deployment | `bondgun.c:5448`, transition-to-Laptop branch; `propobj.c:20830`, `laptopDeploy` | Separate allocation timing from authored activation timing. Transfer ownership and enable entity behavior only at the declared lifecycle point. |
| Effect handoff | `port/src/effect_gameplay_runtime.c:806/:852`, explosion/spark bridges; callers in `propobj.c:5479/:5492/:5966/:9164` | Pass selected catalog references and context into the existing effect instance runtime. |
| Effect clock/teardown | `src/game/lv.c:3044/:3154` | Preserve simulation-driven ticking and stage cleanup; do not move gameplay effects into render-driven updates. |

## Verified migration hazards

1. **Mauler event naming does not match the native lifecycle.**
   `romextract_pdweapon.c:2224`, `s_weaponTriggerEventKind`, emits
   `event.trigger_released` for `fire.charge_release`. In contrast,
   `bondgun.c:9538`, `bgunTickMaulerCharge`, charges while secondary is selected
   with sufficient ammunition, independently of trigger release. Native attack
   admission occurs while the trigger is on at `:1373`. Executing the emitted
   event literally would change gameplay. The emitter and lifecycle module must
   be corrected together; do not give the old event name an undocumented meaning.
2. **Throws begin before the emitted release event.** The emitter labels throws
   `event.trigger_released`. `bgunTickIncAttackingThrow:2626` begins priming and
   animation earlier, uses release/animation state to advance, and can force a
   grenade throw on expiry. Starting that native routine only at release loses
   priming time and changes in-hand expiry behavior.
3. **Exports are not consistently lifecycle entries.** Held exports skip the
   event node. Projectile `main` exports at `romextract_pdweapon.c:1859` skip
   spawn state. Entity `main` identifies a persistent behavior module without
   declaring an event root. A universal “start at every export” rule is incorrect.
4. **Base guards prevent double handling.** `weaponGetCustomProjectileGraph`
   and `weaponGetEntityGraphForGameplay` at `propobj.c:1531/:1548` intentionally
   return null for base weapons. Native weapon-number branches remain live.
   Deleting those guards before replacing the complete branch can duplicate
   timers, impacts, effects, cleanup, or ownership transitions.
5. **The existing transition adapter instantiates early.** At
   `bondgun.c:5448`, custom transition records can enter `laptopDeploy` immediately;
   non-autogun transitions remain deferred. The source must distinguish early
   native allocation from actual deployed activation instead of treating every
   authored `when` value as equivalent.

These are source-traced facts, not gameplay test results. They justify versioned
migration and targeted parity tests, not arbitrary new event semantics.

## Proposed executable contract

The names and interfaces in this section are proposals, not existing APIs. Use a
new executable schema version for the affected weapon/projectile/entity graphs;
do not silently reinterpret v1. Keep existing graph/source files as the editable
authoring surfaces and avoid serializing a parallel authored runtime program.

### Immutable programs and module definitions

One immutable program belongs to a catalog ID, source digest, dependency digest,
schema version, and compiler version. It contains growable node, edge, typed
parameter, context, event-root, export, and subgraph tables, plus compiled adjacency
and validated module bindings. Preserve source node IDs for diagnostics. Do not
carry the old fixed 64-node/128-edge tables forward as a new authoring ceiling.

Each native module definition declares:

- its stable name/version and accepted source parameters, including optional-value semantics;
- context inputs/outputs and the lifecycle signals it observes;
- execution input/output ports and any state layout;
- validation, start, tick/event, cancel, and cleanup callbacks;
- whether it owns ammunition, native subscriptions, or a persistent object;
- which gameplay mutations must be prepared before commit.

Reuse intentional module names such as `og.fire.hitscan`, `og.projectile.homing`,
and `og.entity.autogun` in `weapon_graph_runtime.c:s_parity_modules`. A module name
must select a callable implementation with explicit parameters and lifecycle,
not merely annotate a record while a weapon-number branch executes elsewhere.

The new module table should also supply the editor's parameter/port descriptions
and validation capabilities. That prevents the authoring UI from advertising
accepted but inert controls. Module constants that users should adjust must be
extracted into supported source parameters or a single versioned public module
definition; prose such as “extract in runtime adapter” is not completed extraction.

### Per-instance state and events

Maintain a distinct runtime instance for each held hand/weapon instance, projectile,
and deployed entity. A proposed common interface is:

```c
/* Design sketch only; names and layouts are not implemented. */
graph_instance_id graphBind(program_id, host_binding, context_bindings);
graph_result graphDeliver(graph_instance_id, event_id, event_payload);
graph_result graphAdvance(graph_instance_id, simulation_delta);
void graphCancel(graph_instance_id, cancel_reason);
```

The instance retains a program generation, typed context values, event sequence,
pending continuations, and node-local state. Persistent charge, cooldown, target,
and ownership state must not live in catalog-global records. Primary and secondary
modes share only explicitly declared context; separate hands remain separate.

Bind the emitter's four current contexts (`owner_player`, `owner_team`,
`weapon_instance`, `damage_credit_player`) to real host values. The broader context
names in the module design are future contracts until their production binding is
implemented. Spawn and transition must explicitly copy or reference values with
the declared lifetime. Use lifetime-checked identities for native objects so a
reused prop address cannot become an old instance's owner or target.

Events originate at the traced gameplay seam, not by guessing from a module name.
Use the existing simulation deltas and native time conversions, including fractional
native timing where applicable. Rendering and wall-clock time must not advance
gameplay. Retain current authority rules: authoring a graph does not grant a
client permission to apply authoritative damage or deploy server-owned entities.

### Routing and asynchronous operations

Proposed new edges explicitly name ports, for example:

```json
{"from":"ammo_gate","output":"pass","to":"shot","input":"exec"}
```

The proposed default is stable authored edge order for fan-out and at most one
execution of a reached node per root-event activation. A false gate does not
activate its `pass` successors. Fan-in is not an implicit AND join; an explicit
join would need a declared module contract. This policy is part of the D-006A
review, not a claim about existing v1 pins.

A native module can complete, block, wait, or fail. Waiting retains a continuation;
its outgoing completion edges run when the native operation completes. Explicit
repeat/continuous modules own repetition. Ordinary graph cycles must not create
unbounded work within one simulation update. A failed gate does not secretly poll
again unless the authored event/module contract requests another activation.

### Mutation ownership and teardown

Validate modules, dependencies, and context bindings before making an instance
active. Prepare and reserve resources before irreversible gameplay work. Do not
promise generic rollback of damage or effects that have already committed.

There must be exactly one ammunition debit. `bgun0f09a6f8:2201` currently subtracts
loaded ammunition at shot commit. In the first native shoot slice, the explicit
native module retains that responsibility. Before standalone `ammo.consume`
composition is enabled, implement a reservation/commit contract and suppress the
native debit for that execution path. Applying both would double-charge ammunition.

Cancel on unequip/replacement, owner retirement, object free, source invalidation,
and stage teardown as appropriate to declared lifetimes. Cleanup must run once,
including proxy registration, native effect ownership, and pending reservations.
Initially cancel instances on source-generation change. Migrating live state across
edited programs requires a separate specified contract.

## Reuse the existing effect infrastructure carefully

Useful precedents are `effectInstanceProgramValidate`, `resolveContexts:693`,
`selectTopology:819`, the consumer prepare/commit phases, and source-generation
cancellation in `effectInstanceRuntimeTick:1243`. The prop runtime provides an
existing synchronous gate traversal.

The effect scheduler is not already a held-event virtual machine: effects can
select a root/subgraph, retain a timeline, and update their selected nodes. Preserve
that contract. Share program ownership, module registration, diagnostics, and
lifecycle facilities where practical; do not force effects into new weapon-event
semantics merely to use one implementation. Keep native profile-library execution
explicit and intact.

## v1 migration and extraction compatibility

1. Define the executable version, event roots, ports, module lifecycles, and debit
   ownership before activating the new execution path.
2. Update each affected emitter alongside its native adapter. Newly extracted base
   sources must contain real lifecycle roots and adjustable parameters, including
   corrected charge/throw behavior. Regenerate source-derived caches normally.
3. Keep old v1 documents under their declared compatibility contract during the
   staged migration. Identify compatibility use in diagnostics and tooling; do not
   call the old projected/inert graph path complete executable support.
4. Provide source migration with reviewable changes. Known extracted shapes can
   receive deterministic migration, but arbitrary user-edited graphs must not be
   silently rewritten or assigned guessed event semantics.
5. Select either the executable instance or the previous native branch for a
   lifecycle. Once the replacement is complete, base and custom assets use the
   same contract check rather than a custom-weapon-number threshold.
6. Remove superseded compatibility paths only after every corresponding base
   family has passing parity evidence. Do not retain a permanent hidden fallback
   to avoid implementing accepted graph behavior.

## First coherent executor unit

Start with a genuine single-shot held graph:

`trigger_pressed -> ammo_available -> cooldown_ready -> fire.hitscan`

The deliverable is a connected gameplay slice, not just a compiler or callback
mock. Proposed source ownership for that later unit includes a new common program/
instance module, `weapon_graph_runtime.c/.h`, scoped `bondgun.c` input/admission/
attack-record seams, the single-shot emitter, and focused executor/gameplay tests.
The concrete implementation should remain source-reviewable before architectural
shipping. Existing source is not changed by this document.

The unit must:

- retain the executable program and node-specific action records at registration;
- bind separate per-hand instances after normal input arbitration;
- route actual press events through authored ammo/cooldown gates;
- use the reached node's parameters when starting the native shoot lifecycle;
- preserve native start, animation, shot commit, ammunition, recovery, and completion;
- block action execution when its branch did not run;
- cancel and free the instance at the relevant hand/weapon lifecycle boundary;
- cover one verified extracted single-shot base family and a creator-authored
  branching fixture through production gameplay.

Do not export merely one selected action and then execute it regardless of edges.
Do not add target-lock, throw, charge, or autonomous entity behavior to this first
unit without the corresponding source trace and complete lifecycle implementation.
The remaining work stays explicit; this slice is not closure of the user's request.

## Subsequent coherent units

| Order | Unit | Completion boundary |
| --- | --- | --- |
| 1 | Single-shot held execution above | Actual event/gate/topology edits affect the native attack path with independent instances. |
| 2 | Automatic, burst, beam, explicit ammunition operations | Repetition and debit ownership are explicit; unchanged extracted timing/ammunition behavior has parity proof. |
| 3 | Charge, primed throws, melee, devices, held presentation | Correct emitter lifecycles and full native start/tick/cancel/complete behavior, including in-hand grenade expiry. |
| 4 | Projectile spawn, motion, contacts, timers, cleanup; then guidance variants | Per-projectile programs drive one mutually exclusive lifecycle path for base and custom sources. |
| 5 | Entity activation/transition, proxy/remote/timed behavior, autogun, recovery, owner cleanup | Declared activation timing, damage credit, authority, ownership and recovery are connected end to end. |
| 6 | Cross-family contexts and effect handoff | Context lifetime and ownership survive weapon-to-projectile-to-entity/effect transitions without duplicating effects infrastructure. |
| 7 | Remaining compatibility removal | All retained base graph semantics are deliberately consumed; edited-source, campaign, Combat Simulator, and network receipts close the remaining contracts. |

## Independent proof matrix

Assertions must observe actual production state or outcomes. A source search,
compiler hash, mocked callback, or isolated static test cannot stand in for gameplay
or human visual evidence. Prepare tests in one coherent batch and follow the normal
queued build/test/runtime boundary; this proposal itself ran none.

| Contract | Independent witness |
| --- | --- |
| Gate behavior | False gate produces zero attacks, ammunition debits and effects; true gate reaches the selected native action. |
| Editable topology | Disconnect/reconnect or route to another action changes actual gameplay, not only an IR digest. |
| Selected-node parameters | Two reachable alternatives with different damage/timing use the parameters of the executed node, not the first export or last folded node. |
| Independent instances | Two hands, two players, and multiple spawned objects have independent state; only declared primary/secondary context is shared. |
| Asynchronous completion | Waiting animation does not rerun start; shot/completion/cancel/cleanup happen exactly once. |
| Native single-shot parity | Untouched extracted source matches shot count, ammunition, damage, attack/recovery timing, recoil, sound, and animation observations. |
| Resource failure | Missing dependency or failed reservation causes no partial spawn/debit; retry behavior is explicit. |
| Lifecycle retirement | Unequip, death, disconnect, unload, source replacement and prop reuse leave no stale continuation or owner. |
| Source authority | Editing the same catalog source changes future execution; selected missing references fail before side effects. |
| Base/custom dispatch | Equivalent executable graphs operate under both identities without duplicate native execution. |
| Time | Simulation pause, zero delta, variable delta, and native timing boundaries preserve the authored cadence and completion contract. |
| Network authority | Clients cannot duplicate authoritative damage/deployment; owner and damage-credit values survive handoff. |
| Later special families | Campaign and Combat Simulator receipts exercise charge, throw, guidance, deployment, detonation and recovery. |
| Visual/audio parity | Separate ordinary-client and human-reviewed presentation evidence covers animation, muzzle/trail/effect appearance, audio and camera behavior. |

## Decisions and next action

D-006A was explicitly selected by Mike on September 8. No architecture choice
remains pending. Correcting emitted events to preserve source-traced base behavior
is an implementation obligation. Keep v1 compatibility explicit until the complete
replacement lifecycle has production parity evidence.

September 26 preparation contract: `wgV2EquippedCatalogPrepareArchive` consumes
one captured public `.pdweapon` ZIP, selecting the declared primary/secondary JSON
and settings from that same snapshot. It computes the canonical source hash,
prepares all native dependencies, and returns an unpublished owned candidate.
Model ownership transfers only on success. Existing active generations survive a
failed candidate. Unsupported v1 variable/context/presentation bindings reject
explicitly rather than disappearing during conversion. Actual catalog publication
and per-hand lifecycle callers are still required.

The equipped v2 `ammo` field accepts the original slot-zero object or exactly
`[slot_zero, slot_one]`, using `null` for an absent slot. Action and ammo-gate
references must resolve to a declared slot; `ammo_slot: -1` explicitly uses none.
This preserves native per-function ammo selection without fabricating a default
ammo record for missing source. Client/tests builds and the graph cohort pass
(59 cases/1,057 assertions). The native-source guard passes; the required c3842
cohort retains three static-contract failures. See the September 26 preparation
receipt. Mike requested pause at this boundary; resume only on his instruction.

After the current asset correctness batch reaches its validation boundary, prepare
the first executable held unit as concrete reviewable work, preserving the ownership
and source-freeze workflow. Keep the entire extraction/use/modding objective visible
until the subsequent family units and independent gameplay proofs are complete.
