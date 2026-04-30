# Perfect Dark 2 - Project Context

> **Skeleton, Phase 2B Step 1.** Pillar links and design index fill in as those files land. Final pass at Phase 2B Step 5.

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

- **Wire protocol**: v46 (per [pillars/save-wire-format.md](pillars/save-wire-format.md) [TBD] and `port/include/net/net.h:12`).
- **Save format**: SAVE_VERSION=2, MPSETUP_VERSION=2.
- **Build**: v0.0.175+ (per recent release tags). Build via `.\devtools\build-session.ps1 -Session <id> -Target all`.
- **Active session range**: see [session-log.md](session-log.md).
- **Critical path**: see [tasks.md](tasks.md). Post-rebuild queue: Catalog Weapons F11-F13 (retire `g_Weapons[]`), then Catalog Gate 3 migration (heads/bodies/arenas/audio + Manager + .pdbase pattern), then Input Controller Support (Branch 2 Cohorts 5-8 + menus + full controller).
- **Long-term roadmap**: [roadmap.md](roadmap.md) [TBD].

---

## Pillars (live state per architectural subsystem)

Each pillar doc captures the live state, current invariants, and the code that owns the pillar. Updated with the code that touches it.

| Pillar | Doc | When to load |
|--------|-----|--------------|
| Catalog system | [pillars/catalog.md](pillars/catalog.md) [TBD] | Asset identity, catalog IDs, manager pattern |
| Input system | [pillars/input.md](pillars/input.md) [TBD] | Action map, IMC stack, layer system, dispatch |
| Menus / UI / UX | [pillars/menus.md](pillars/menus.md) [TBD] | ImGui menu work, menu pool, menu graph |
| Modding | [pillars/modding.md](pillars/modding.md) [TBD] | `.pdmod` format, scanner, distribution, registry |
| Connectivity | [pillars/connectivity.md](pillars/connectivity.md) [TBD] | ENet, P2P 6-tier, presence, voice, room/lobby |
| Save / wire format | [pillars/save-wire-format.md](pillars/save-wire-format.md) [TBD] | Versioning, migration framework, bit-pack primitives |
| Server / hosting | [pillars/server.md](pillars/server.md) [TBD] | Listen vs dedicated, participant pool, RCON, bans |
| Build / dev tooling | [pillars/build-dev-tooling.md](pillars/build-dev-tooling.md) [TBD] | CMake, MSYS2, build-session, release pipeline |
| Tests | [pillars/tests.md](pillars/tests.md) [TBD] | pd-tests, Catch2, pure mirrors, scope aliases |
| Rendering | [pillars/rendering.md](pillars/rendering.md) [TBD] | fast3d GBI translator, ImGui backend, theme system |
| Physics / collision | [pillars/physics-collision.md](pillars/physics-collision.md) [TBD] | Capsule sweep, jump, ground detection |

---

## Active design references

Sub-bucketed by pillar. Designs that have shipped move to `_old/designs-shipped/` per [retention.md](retention.md). [TBD list, fills in at Step 4.]

- `designs/input/` - input universality, mapping rebuild, contextual schemes, authority methodology, controller constraints
- `designs/catalog/` - full-pipeline weapons, asset provider future phases
- `designs/menus/` - menu stack, flat navigation, inventory, hold ring, radial, HUD layer order
- `designs/modding/` - `.pdmod` format, mod enablement policy, theme bundle + per-agent settings, Forge level editor
- `designs/connectivity/` - modern main menu, plugin ABI, interest management, hosting modes, manifest architecture
- `designs/platform/` - Studio platform, visual scripting node taxonomy
- `designs/in-flight/` - GPU swarm + test scenarios, player init architectural fixes, issue-10 rigging-aware body/head linkage

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
