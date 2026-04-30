# Perfect Dark 2 - Project Context

> **Live as of 2026-04-30 (rebuild Phase 2B Step 9).** All pillar docs in place, designs sub-bucketed, session-log cut to active rolling window, retention rules captured.

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

Then load the pillar doc(s) for whatever you are touching.

---

## Live state at a glance

- **Wire protocol**: v46 (per [pillars/save-wire-format.md](pillars/save-wire-format.md) and `port/include/net/net.h:12`).
- **Save format**: SAVE_VERSION=2, MPSETUP_VERSION=2.
- **Build**: v0.0.175+ (per recent release tags). Build via `.\devtools\build-session.ps1 -Session <id> -Target all`.
- **Active session range**: see [session-log.md](session-log.md).
- **Critical path**: see [tasks.md](tasks.md). Post-rebuild queue: Catalog Weapons F11-F13 (retire `g_Weapons[]`), then Catalog Gate 3 migration (heads/bodies/arenas/audio + Manager + .pdbase pattern), then Input Controller Support (Branch 2 Cohorts 5-8 + menus + full controller).
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
| Save / wire format | [pillars/save-wire-format.md](pillars/save-wire-format.md) | SAVE_VERSION=2, MPSETUP_VERSION=2, NET_PROTOCOL_VER=46, migration framework |
| Server / hosting | [pillars/server.md](pillars/server.md) | Listen vs dedicated, participant pool, RCON, bans, room passwords |
| Build / dev tooling | [pillars/build-dev-tooling.md](pillars/build-dev-tooling.md) | CMake + MSYS2, build-headless / build-session, release pipeline, updater |
| Tests | [pillars/tests.md](pillars/tests.md) | pd-tests, Catch2, 35 test files, pure mirrors, scope aliases |
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

[audits/](audits/) holds point-in-time assessments within a 14-day window. Older audits live in `_old/audits/` per [retention.md](retention.md).

Currently active:
- [audits/infrastructure-pillars-status-2026-04-27.md](audits/infrastructure-pillars-status-2026-04-27.md)
- [audits/codebase-architecture-rating-2026-04-27.md](audits/codebase-architecture-rating-2026-04-27.md)
- [audits/catalog-universality-sweep-2026-04-27.md](audits/catalog-universality-sweep-2026-04-27.md)
- [audits/post-implementation-audit-2026-04-25.md](audits/post-implementation-audit-2026-04-25.md)
- [audits/sp-stage-mp-readiness-2026-04-24.md](audits/sp-stage-mp-readiness-2026-04-24.md)
- [audits/flat-menu-navigation-audit-2026-04-25.md](audits/flat-menu-navigation-audit-2026-04-25.md)
- [audits/pdmod-verification-matrix-2026-04-25.md](audits/pdmod-verification-matrix-2026-04-25.md)

---

## Bug ledgers

- [bugs.md](bugs.md) - open one-off bugs with severity, status, fix, verify command.
- [systemic-bugs.md](systemic-bugs.md) - architectural bug pattern catalog (SP-1 through SP-15).

---

## Sessions and history

- [session-log.md](session-log.md) - rolling chronological log, last ~100 sessions.
- `_old/session-log/` - older session tiers preserved.

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
