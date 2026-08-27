# Perfect Dark 2 - Project Context

> **Live as of 2026-08-26.** The repo-local Workbench is durable project truth.
> D-004 and T-RELEASE-001 define the canonical five-milestone path to 1.0:
> complete base game and graphs, Theater, sample weapon, friend-play/performance
> hardening, and one source-frozen release candidate.

---

## What this project is

Perfect Dark 2 is a PC port and modernization of Rare's N64 Perfect Dark, evolved into a modding platform with networked multiplayer, an asset catalog, a unified input system, an ImGui menu system, and an in-game level editor (The Grid / Forge). PC-only target (Windows x86_64 via MSYS2/MinGW + CMake). Single developer (Mike); AI sessions write code, Mike compiles and tests.

---

## Cold-start checklist

Read in this order before doing any work in this project:

1. [README.md](README.md) - this file. Orientation.
2. [working-preferences.md](working-preferences.md) - collaboration rules.
3. [constraints.md](constraints.md) - active and removed invariants. Check before any complex work.
4. [procedures.md](procedures.md) - build verify, git safety, worktree rules.
5. [tasks.md](tasks.md) - what is open right now.
6. [session-log.md](session-log.md) - last few sessions.
7. [retention.md](retention.md) - the rules that keep this tree clean.
8. `Tools/Workbench/data/roadmap.json` and `notes.jsonl` - durable live work,
   decisions, ownership, evidence, and feedback state.

Then load the pillar doc(s) for whatever you are touching.

The canonical context source is this `context/` tree. Parent-level briefing files are convenience mirrors only and must be treated as stale unless explicitly synced from this directory.

---

## Live state at a glance

- **Wire protocol**: v58 (per [constraints.md](constraints.md) and `port/include/net/net.h`).
- **Save format**: SAVE_VERSION=2, MPSETUP_VERSION=3.
- **Build**: v0.0.175+ (per recent release tags). Build via `.\devtools\build-session.ps1 -Session <id> -Target all`; standalone `pd-server` is removed/deprecated, so use listen-host in the client.
- **Active session range**: see [session-log.md](session-log.md).
- **Critical path**: the repo-local Workbench is authoritative for active work,
  decisions, evidence, and gates. The default route is T-RELEASE-002 through
  T-RELEASE-006 in order. The former Kanban and `historical-cut` Workbench items
  are retained history, not live queues. See [tasks.md](tasks.md) for the concise
  milestone summary.
- **Current Milestone 1 lane**: T-ENGINE-004 is validated, moving the milestone
  to 3 validated, 7 partial, and 5 missing dependencies (20 percent). Its
  durable closure matrix is
  [2026-08-26-t-engine-004-closure.md](evidence/2026-08-26-t-engine-004-closure.md).
  B-1101/B-1102 retain accepted frozen
  automation and Combat Simulator evidence. B-1067/B-1103/B-1104 are
  production-verified regression gates at protocol v58: one explicit
  inactive/waiting/release/active
  stage lifecycle requires the listen authority and every exact remote stage
  participant to cross the real post-load boundary; `SVC_STAGE_START` and
  `CLC_STAGE_READY` share a nonzero epoch; release publishes a fresh baseline;
  and NPC convergence is one reliable resync plus canonical sync-ID-sorted
  snapshot digest rather than a periodic mutable-state checksum. Frozen
  automation passes 66,421 assertions in 1,200 cases plus the native-source
  guard. Exact v58 client `5DA75BE6...` passes all seven ordinary-client paths,
  including initiator authority 214/214 and focus-independent invitee authority
  170/170 with one signed typed match-server route, one listen authority, one
  idempotent peer join, and no probe/relay handoff. B-1076 is also a production
  regression gate: `mpStartMatch` owns pre-publication Combat Simulator
  menu/input teardown for offline, listen-authority, and receiving-client starts.
  Exact client `04DB220E...` passes the ordinary Agent Select/Main Menu/Play/Room
  two-match/Play Again lifecycle 55/55 and preserves the invitee-authority route
  170/170 with no watchdog repair. The integrated 210/218
  B-1085/V-009 fixture remains separately rejected only because this desktop
  exposes no foreground HWND. The current closure unit is B-1096/B-1097. Its
  first frozen automation batch passes focused 468/15, complete 66,705/1,204,
  and the native-source guard, while the sole ordinary run is retained and
  rejected at 101/105. That run proved reciprocal Cyclone attachment, the real
  deleting/non-regenerating transition, and the wider reconnect/gameplay/
  teardown transaction, then disproved the gate's demand to keep the dead prop
  resident. A bounded Luna xhigh audit confirmed normal cleanup should fully
  retire the sync ID before PREPARE and a pristine receiver should remove zero
  dynamic counterparts. Corrected source now captures the selected sync ID,
  checks its completed authority-pool retirement read-only before reconnect,
  and requires `terminal_absent=0`, `removed=0`, exact world/inventories, one
  commit, and resumed authority fire. The first corrected batch passed 477/15,
  66,714/1,204, and the native-source guard, but its runtime attempt stopped
  before listener publication because the 34-character event name exposed an
  existing 31-character parser buffer. Shared schema/harness source now admits
  complete event tokens up to 63 characters and rejects longer tokens before
  copy. The parser-safe refreeze then passed focused 483/16, complete
  66,720/1,205, the native-source guard, and zero product/verifier drift on
  exact client `5AEC7918...`. Its ordinary receipt
  `results-20260826T111558Z.json` is retained and rejected at 93/109: it proves
  the selected authority prop absent before auth, pristine receiver
  `removed=0`, exact world and both inventories, and one reconnect commit, then
  the client crashes in `objTickPlayer` before restored fire. Source tracing
  shows reconnect snapshot stats replay the historical dead state through
  `playerDieByShooter`, which performs live score/drop/prop side effects after
  exact snapshot restoration. Current source now uses a pure typed planner to
  distinguish live player-state events from the ordered reconnect snapshot and
  shares dead-state presentation through a snapshot entry point that suppresses
  score/killfeed, item-drop, owner-cleanup, menu/HUD, and lifetime side effects.
  The same failure exposed a separate intrusive-link defect: dynamic spawn
  receive assigned `prop.next/prev` to the scheduler and then reused those links
  for a parent-child chain. A Luna xhigh audit confirmed the class. Current
  source prunes stale sync-ID-zero held weapons before authoritative adoption,
  constructs rows off-list, commits exactly one attached/active/paused topology,
  validates scheduler membership on writer, receiver, and transaction end, and
  emits `topology=exclusive`. Focused contracts and the ordinary fixture require
  both that topology witness and exactly one `live_side_effects=0` witness before
  commit. The isolated client/updater/tests builds and restarted frozen batch
  now pass focused 718/20, complete 66,960/1,209, and the native-source guard on
  exact product `1C184C57...` / client `8262681E...` and verifier `E0FA8E6A...`
  / tests `A9B1ADAA...`, with zero manifest drift. The earlier stale-test-binary
  receipt is retained and rejected. The unchanged-source ordinary reconnect
  receipt `results-20260826T121959Z.json` now passes 116/116 with one authority
  retirement/absence, snapshot `live_side_effects=0`, exclusive topology,
  exact world and both inventories, one commit, 30 authority-accepted Cyclone
  shots, clean exits, and no leaked process. B-1096/B-1097 are regression
  gates, completing T-ENGINE-004's finite section 9.10 lifecycle matrix. The
  active V-010 unit remains B-1086/B-1106. Its schema-v2 geometry stream, strict
  recursive ownership, canonical parameter-byte `G_VTX` decoder, full star
  domain, Type-3 pair cache continuity, room-collision destination ownership,
  legacy-v1 migration, and complete-cache gate are now automation-accepted on
  exact client `36EA05D1...`: focused 97/7, complete 67,361/1,230, every
  conformance/workflow/native-source gate, and unchanged 10-product/20-verifier
  source manifests. Ordinary-client receipt `results-20260826T193629Z.json`
  cleanly extracted all 733/733 public meshes and exited cleanly, accepting the
  former `vertex_load` and `triangle_slot` repair. The same receipt remains
  rejected at 20/26 for visual closure because confirmed B-1106 makes
  `base:sp_body_118` / nested `base:model_cchicrob` modeldef conversion fail,
  rolls back 64 Main Menu loads, and prevents both Carrington door sources from
  activating. Its six captures never reach Carrington and prove neither exact
  door state nor the V-009 no-obstruction gate. Exact tracing found the shared
  defect: Type-3 opaque/translucent lists retain vertex slots while resetting
  geometry knowledge, but schema v2 lacked that boundary. The source-connected
  schema-v3 repair now publishes every ordered `vertex_load`, every triangle's
  exact `vertex_cache_slots`, and a typed paired-list boundary: `vertex_scope`
  retains the bounded 64-slot table only for a unique Type-3 owner, while
  `vertex_cache_reset` clears it for every other render type. C and Python
  validators simulate that exact table, reject unloaded or false snapshots,
  malformed command arrays, and matrix indices above 32766, and prove each
  flattened load can restore triangle-time state. The deterministic Type-3
  baseline owns inherited lighting/texture-generation state but never
  caller-owned fog; an unrelocatable fog transition fails closed. Explicit load
  provenance, rather than boundary position, is what permits load-time state to
  differ from later draw state. Extractor v25/cache v28 plus modeldef cache v12
  invalidate predecessors. Exact frozen client `7EF396C9...` and tests
  `4D14886D...` now pass focused 143/8, complete 67,451/1,231, all
  conformance/workflow/source/native guards, and zero product or verification
  overlap. No replacement extraction, door activation, or visual evidence is
  claimed yet. Next run the clean true-3440 Carrington production proof, then
  retain representative Campaign and Combat Simulator propagation. Milestone 1
  remains partial at 20 percent.
- **Long-term roadmap**: [roadmap.md](roadmap.md).

---

## Pillars (live state per architectural subsystem)

Each pillar doc captures the live state, current invariants, and the code that owns the pillar. Updated with the code that touches it.

| Pillar | Doc | When to load |
|--------|-----|--------------|
| Catalog system | [pillars/catalog.md](pillars/catalog.md) | Asset identity, catalog IDs, manager pattern, .pdbase loader |
| Input system | [pillars/input.md](pillars/input.md) | Action map (103 actions), IMC stack, 7-layer system, suppression predicate |
| Menus / UI / UX | [pillars/menus.md](pillars/menus.md) | ImGui menus (31 files), menu pool, menu graph, theme system |
| Modding | [pillars/modding.md](pillars/modding.md) | `.pdmod` format, scanner, manifest, network distribution |
| Connectivity | [pillars/connectivity.md](pillars/connectivity.md) | ENet, P2P 6-tier (LAN/DIRECT/STUN/UPnP/ICE/TURN), presence, voice |
| Save / wire format | [pillars/save-wire-format.md](pillars/save-wire-format.md) | SAVE_VERSION=2, MPSETUP_VERSION=3, NET_PROTOCOL_VER=58, migration framework |
| Server / hosting | [pillars/server.md](pillars/server.md) | Listen vs dedicated, participant pool, RCON, bans, room passwords |
| Build / dev tooling | [pillars/build-dev-tooling.md](pillars/build-dev-tooling.md) | CMake + MSYS2, build-headless / build-session, release pipeline, updater |
| Tests | [pillars/tests.md](pillars/tests.md) | pd-tests, Catch2, 118 test files, production-linked pure modules, static architecture pins, scope aliases |
| Rendering | [pillars/rendering.md](pillars/rendering.md) | fast3d GBI -> OpenGL, vtable backend, ImGui v1.91.8 |
| Physics / collision | [pillars/physics-collision.md](pillars/physics-collision.md) | Capsule sweep over legacy primitives, jump, stair-step, movement modes |

---

## Active design references

Sub-bucketed by pillar. Designs that have shipped move to `_old/designs-shipped/` per [retention.md](retention.md).

**Catalog**
- [designs/catalog/catalog-full-pipeline-weapons.md](designs/catalog/catalog-full-pipeline-weapons.md) - F1-F10 shipped, F11+ data move design

**Input**
- [designs/input/input-universality-and-transitions.md](designs/input/input-universality-and-transitions.md) - Phase 1 design, Cohorts 5-8 in flight
- [designs/input/input-mapping-menu-rebuild.md](designs/input/input-mapping-menu-rebuild.md)
- [designs/input/contextual-input-schemes.md](designs/input/contextual-input-schemes.md) - IMC architecture (J-1/2/3 landed)
- [designs/input/input-authority-methodology.md](designs/input/input-authority-methodology.md)
- [designs/input/menu-controller-input-constraints.md](designs/input/menu-controller-input-constraints.md)

**Menus**
- [designs/menus/menu-stack-architecture.md](designs/menus/menu-stack-architecture.md) - target spec
- [designs/menus/flat-menu-navigation.md](designs/menus/flat-menu-navigation.md) - flat-traversal rule
- [designs/menus/menu-inventory.md](designs/menus/menu-inventory.md) - 120 screens roster
- [designs/menus/input-authority-and-menu-pool.md](designs/menus/input-authority-and-menu-pool.md) - Phase 1+2 implemented
- [designs/menus/pdgui-hold-ring.md](designs/menus/pdgui-hold-ring.md)
- [designs/menus/activemenu-radial-architecture.md](designs/menus/activemenu-radial-architecture.md) - legacy GBI radial
- [designs/menus/hud-layer-order.md](designs/menus/hud-layer-order.md)

**Modding**
- [designs/modding/pdmod-format.md](designs/modding/pdmod-format.md) - unified mod format spec
- [designs/modding/asset-archive-clean-formats.md](designs/modding/asset-archive-clean-formats.md) - frozen clean archive family layouts plus c3842 global rule that public editable files are the native client source and generated products are cache
- [designs/modding/weapon-archive-clean-format.md](designs/modding/weapon-archive-clean-format.md) - target clean `.pdweapon` authoring layout before extractor rebuild
- [designs/modding/weapon-behavior-graph-assets.md](designs/modding/weapon-behavior-graph-assets.md) - `.pdweapon`, `.pdprojectile`, `.pdentity` graph asset schema
- [designs/modding/weapon-graph-runtime-cutover-plan.md](designs/modding/weapon-graph-runtime-cutover-plan.md) - runtime cutover slices for weapon graph implementation
- [designs/modding/scenario-authoring-and-mission-graphs.md](designs/modding/scenario-authoring-and-mission-graphs.md) - c3841 target for textured DCC-openable scenario scenes that also feed runtime loading, collision overrides, generated navmesh, and mission/setup graphs
- [designs/modding/mod-enablement-policy.md](designs/modding/mod-enablement-policy.md)
- [designs/modding/theme-bundle-and-per-agent-settings.md](designs/modding/theme-bundle-and-per-agent-settings.md)
- [designs/modding/forge-level-editor.md](designs/modding/forge-level-editor.md) - Phase 0-1 shipped, future phases scoped

**Connectivity**
- [designs/connectivity/connectivity-and-modern-main-menu.md](designs/connectivity/connectivity-and-modern-main-menu.md) - Phase 1+2 shipped
- [designs/connectivity/pd-server-plugin-abi.md](designs/connectivity/pd-server-plugin-abi.md) - ADR (deferred per S486)
- [designs/connectivity/interest-management-replication.md](designs/connectivity/interest-management-replication.md) - P5-A scaling design
- [designs/connectivity/hosting-modes-listen-vs-dedicated.md](designs/connectivity/hosting-modes-listen-vs-dedicated.md) - threat model
- [designs/connectivity/manifest-architecture.md](designs/connectivity/manifest-architecture.md) - implemented

**Platform (future)**
- [designs/platform/studio-platform.md](designs/platform/studio-platform.md) - v0.5.0+
- [designs/platform/visual-scripting-node-taxonomy.md](designs/platform/visual-scripting-node-taxonomy.md) - v0.5.0+

**In-flight**
- [designs/in-flight/gpu-swarm-and-test-scenarios.md](designs/in-flight/gpu-swarm-and-test-scenarios.md) - Phase 1 design, Phase 2 gated on Mike
- [designs/in-flight/player-init-architectural-fixes.md](designs/in-flight/player-init-architectural-fixes.md)
- [designs/in-flight/issue-10-rigging-aware-body-head-linkage.md](designs/in-flight/issue-10-rigging-aware-body-head-linkage.md)
- [designs/in-flight/testing-framework.md](designs/in-flight/testing-framework.md) - Cohorts 1-2 shipped, Cohort 3 deferred

---

## Recent audits

[audits/](audits/) holds only current point-in-time assessments. Older audits live in `_old/audits/` per [retention.md](retention.md).

Currently active:
- [audits/migration-utilization-measurement-2026-06-10.md](audits/migration-utilization-measurement-2026-06-10.md) - measurement input for the c3849 utilization work. Waves 1-7 are now complete; use it as historical measurement context, not as the live route.

Older audits that are no longer active entry points are either retained only where a live design still cites them or moved to `_old/audits/2026/` per retention policy. Do not use an older audit as current direction without verifying it against live source and [tasks.md](tasks.md).

---

## Bug ledgers

- [bugs.md](bugs.md) - seven current one-off 1.0 defects, consolidated release
  regression clusters, and explicit post-1.0 bug routing. The complete former
  mixed ledger is hash-preserved under `_old/bugs/2026/`.
- [systemic-bugs.md](systemic-bugs.md) - architectural bug pattern catalog and
  propagation checklists.

---

## Sessions and history

- [session-log.md](session-log.md) - rolling chronological log focused on the active asset-parity window.
- `_old/session-log/` - older session tiers preserved, including the pre-2026-06-07 archive cut on 2026-06-11.

---

## How to make changes

1. Read [working-preferences.md](working-preferences.md) and [procedures.md](procedures.md).
2. Check [constraints.md](constraints.md) for any rule that touches your work.
3. Load the relevant pillar doc(s) and any active design doc.
4. Make the change in a worktree. Build verify with `.\devtools\build-session.ps1 -Session <id> -Target all`.
5. Update the pillar doc if the change shifts a live invariant. Update [bugs.md](bugs.md) if you fixed a bug or [systemic-bugs.md](systemic-bugs.md) if the bug reveals a pattern. Add a session-log entry.
6. Auto-merge to dev per the standing rule.

---

## Naming conventions

Work titles (sessions, design subjects, task labels) follow `Pillar - Goal` format:
- "Catalog - Weapons F11-F13 data move"
- "Input - Controller Support Cohort 5"
- "Menus - flat navigation enforcement"
- "Modding - pdmod INI delivery from archive"

File names stay human-readable and lowercase-with-hyphens. Folders provide pillar context, so `pillars/catalog.md` carries the pillar prefix structurally rather than in the filename.

---

## What is in `_old/`

`_old/` at repo root contains the prior context tree (everything that was in `context/` before the 2026-04-30 rebuild). It is preserved for git history and forensic reference; it is not the live source of truth.

Do not pull facts from `_old/` without verifying against current code or the live `context/` tree.
