# Perfect Dark 2 - Project Context

> **Live as of 2026-08-24.** The repo-local Workbench is durable project truth.
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

- **Wire protocol**: v57 (per [constraints.md](constraints.md) and `port/include/net/net.h`).
- **Save format**: SAVE_VERSION=2, MPSETUP_VERSION=3.
- **Build**: v0.0.175+ (per recent release tags). Build via `.\devtools\build-session.ps1 -Session <id> -Target all`; standalone `pd-server` is removed/deprecated, so use listen-host in the client.
- **Active session range**: see [session-log.md](session-log.md).
- **Critical path**: the repo-local Workbench is authoritative for active work,
  decisions, evidence, and gates. The default route is T-RELEASE-002 through
  T-RELEASE-006 in order. The former Kanban and `historical-cut` Workbench items
  are retained history, not live queues. See [tasks.md](tasks.md) for the concise
  milestone summary.
- **Current Milestone 1 lane**: T-ENGINE-004 remains partial at 2 validated,
  8 partial, and 5 missing dependencies. Candidate-first player initialization
  retains its accepted Campaign evidence, and B-1101's bounded decoder retains
  its frozen automation receipt. The exact replacement Combat Simulator run
  then rejected at 16/56 under B-1102: schema-v5 reconstruction omitted every
  per-part flags discriminant, and the rejection fallback invalidated animation
  ownership before entering a CHRINFO consumer. Current source now gives the
  extractor/compiler/runtime one exact descriptor contract, rejects trailing
  descriptor bytes, parses versioned scalar tokens canonically across the full
  unsigned frame-value domain, preserves declared topology even for zero-frame
  placeholders, invalidates malformed v7 animation caches through animation
  compiler v8, validates exact optimized-consumer advancement, and renders bind
  pose with a null-safe scale without mutating `model->anim`. One isolated
  source-frozen build and consolidated verification batch now pass focused
  2,057/27, full 65,073/1,186, and the native-source guard on unchanged product
  `e6287a9c...` (2,717 files) and verifier `ad7c38f9...` (402 files). Exact client
  `133A55C6...` then passes the sole replacement two-cycle Combat Simulator
  receipt 56/56 with real fire/hit, both stats/award cycles, Play Again, clean
  exit, zero decoder/crash matches, and no leaked process. B-1101/B-1102 are
  regression gates; accepted Campaign, D-003, and reconnect receipts were not
  rerun. T-ENGINE-004 remains partial while its next broader closure gap is
  audited.
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
| Save / wire format | [pillars/save-wire-format.md](pillars/save-wire-format.md) | SAVE_VERSION=2, MPSETUP_VERSION=3, NET_PROTOCOL_VER=57, migration framework |
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
