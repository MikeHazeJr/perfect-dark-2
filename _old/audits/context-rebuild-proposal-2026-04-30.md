# Context Rebuild Proposal

> **Date**: 2026-04-30
> **Replaces**: Section I of [context-cleanup-plan-2026-04-30.md](context-cleanup-plan-2026-04-30.md). The 12-commit in-place mutation sequence is set aside; this proposal builds fresh from extracted-still-relevant content, then removes the old.
> **Mike's directive (verbatim)**: "I think, using the still-relevant / -valid stuff from the current context stuff, we build fresh in an organized and structured / traversible way, and remove the old context stuff entirely."
> **No em-dashes anywhere** (Windows PowerShell hygiene).
> **Stop gate**: nothing in the old `context/` is destroyed until Mike approves both this structure AND a follow-up "ready to replace" diff summary.

---

## 1. Design principles for the new context

1. **Orient cold readers in minutes, not hours.** A new AI session or Mike returning after time away should know the project's state, the current critical path, and where to look for any specific subsystem within five minutes of opening README.
2. **One source of truth per topic.** No more two files describing the participant pool, three files describing the network architecture, four files describing the menu system.
3. **Pillar-based organization.** The project has a small set of architectural pillars (catalog, input, menus, modding, connectivity, save/wire, rendering, build/dev tooling, tests). Each pillar gets exactly one live reference doc. Active design docs sit beside the pillars.
4. **Living vs frozen.** Living docs track current state and update with code. Frozen docs (audits, ADRs, shipped design plans) live in `_old/` once they pass their useful window.
5. **Built-in retention rules.** A `retention.md` file documents when content moves out of the live tree. The next cleanup is then trivial maintenance, not a 470-line audit.
6. **Concise.** Every file earns its keep. No file at root over 500 lines unless content density demands it; pillar docs target 200-400 lines each.

---

## 2. Proposed directory structure

```
context/
|
|-- README.md                            (cold-start orientation + master index)
|-- constraints.md                       (active + removed invariants ledger)
|-- procedures.md                        (build verify, git safety, worktree rules)
|-- working-preferences.md               (collaboration mode, tone, decision authority)
|-- retention.md                         (when files move from live tree to _old/)
|
|-- tasks.md                             (razor-thin punch list, rolling)
|-- session-log.md                       (rolling, last ~100 sessions)
|-- bugs.md                              (open one-off bugs)
|-- systemic-bugs.md                     (architectural bug classes)
|-- roadmap.md                           (master forward-looking plan to v1.0.0)
|
|-- pillars/                             (one doc per architectural pillar, live state)
|   |-- catalog.md
|   |-- input.md
|   |-- menus.md
|   |-- modding.md
|   |-- connectivity.md
|   |-- save-wire-format.md
|   |-- rendering.md
|   |-- build-dev-tooling.md
|   |-- tests.md
|   |-- physics-collision.md             (capsule, movement, jump)
|   |-- server.md                        (listen vs dedicated, hosting modes)
|
|-- designs/                             (active design references; shipped designs leave per retention.md)
|   |-- input/
|   |   |-- input-universality-and-transitions.md
|   |   |-- input-mapping-menu-rebuild.md
|   |   |-- contextual-input-schemes.md
|   |   |-- input-authority-methodology.md
|   |   |-- menu-controller-input-constraints.md
|   |-- catalog/
|   |   |-- catalog-full-pipeline-weapons.md
|   |   |-- catalog-asset-provider-future-phases.md
|   |-- menus/
|   |   |-- menu-stack-architecture.md
|   |   |-- flat-menu-navigation.md
|   |   |-- menu-inventory.md
|   |   |-- pdgui-hold-ring.md
|   |   |-- activemenu-radial-architecture.md
|   |   |-- hud-layer-order.md
|   |-- modding/
|   |   |-- pdmod-format.md
|   |   |-- mod-enablement-policy.md
|   |   |-- theme-bundle-and-per-agent-settings.md
|   |   |-- forge-level-editor.md
|   |-- connectivity/
|   |   |-- connectivity-and-modern-main-menu.md
|   |   |-- pd-server-plugin-abi.md
|   |   |-- interest-management-replication.md
|   |   |-- hosting-modes-listen-vs-dedicated.md
|   |   |-- manifest-architecture.md
|   |-- platform/
|   |   |-- studio-platform.md
|   |   |-- visual-scripting-node-taxonomy.md
|   |-- in-flight/
|   |   |-- gpu-swarm-and-test-scenarios.md
|   |   |-- player-init-architectural-fixes.md
|   |   |-- issue-10-rigging-aware-body-head-linkage.md
|
|-- audits/                              (recent audits, 14-day window before retention move)
|   |-- infrastructure-pillars-status-2026-04-27.md
|   |-- codebase-architecture-rating-2026-04-27.md
|   |-- catalog-universality-sweep-2026-04-27.md
|   |-- post-implementation-audit-2026-04-25.md
|   |-- sp-stage-mp-readiness-2026-04-24.md
|   |-- flat-menu-navigation-audit-2026-04-25.md
|   |-- pdmod-verification-matrix-2026-04-25.md
|
|-- daily-logs/                          (heavy-day summaries only; 30-day window)
```

The old tree (everything currently under `context/`) goes to `_old/` at repo root.

---

## 3. Per-surface description

### 3.1 Root files (orientation + invariants + ledgers)

| File | Purpose | Source content |
|------|---------|----------------|
| **README.md** | Cold-start orientation. Project overview, build/run, current state at a glance, links to every pillar and active design. The single doc a fresh reader opens first. | Authored fresh. Pulls structure from current README + QUICKSTART + INDEX (which are truncated and stale) but written from scratch with current state. |
| **constraints.md** | Active + removed invariants. The single most reliable doc in the current system; carries forward unchanged. | Direct carry-forward from current `constraints.md`. Spot-check for any stale entries during port. |
| **procedures.md** | Build verification, git safety, worktree rules, isolated build sessions. Operational rules for any AI or Mike-driven session. | Merged from `CRITICAL-PROCEDURES.md` + operational sections of `working-preferences.md` + the build-session.ps1 rule from recent sessions. |
| **working-preferences.md** | Collaboration mode, tone, decision authority, investigation discipline. | Direct carry-forward from current `working-preferences.md`. |
| **retention.md** | When content moves from live tree to `_old/`. The convention that prevents re-accretion. | New doc, authored as part of this rebuild. |
| **tasks.md** | Razor-thin punch list. ONE section per active lane. Completed slices move to session-log.md, not retained here. | Authored fresh from current open lanes (drop the 46-section bloat). |
| **session-log.md** | Rolling chronological session record. Active window: last ~100 sessions. Older tiers in `_old/`. | Carry forward + cut the active window. S281-S480 archive to `_old/`. |
| **bugs.md** | Open one-off bugs with severity, status, fix, verify command. | Direct carry-forward; light update to mark fixed-pending-build entries that have built clean. |
| **systemic-bugs.md** | Architectural bug pattern catalog (SP-1 to SP-15). | Direct carry-forward; well-organized, no drift. |
| **roadmap.md** | Master forward-looking plan to v1.0.0. The canonical "where we're going" doc. | Authored fresh by porting `designs/full-release-roadmap-2026-04-27.md` to live root level. Drop the old `roadmap.md` (stale: v37 / S361) and `infrastructure.md` (stale: same). |

### 3.2 `pillars/` (live state of each architectural pillar)

One doc per pillar. Each is 200-400 lines. Each describes the live state of that subsystem with file:line citations to current code. Each is updated when that pillar's code changes; updates are part of the change, not a separate task.

| Pillar | Source content for the new doc |
|--------|-------------------------------|
| **catalog.md** | `ADR-003-asset-catalog-core.md` content (rationale) + `infrastructure-pillars-status-2026-04-27.md` Section 1 (current state) + current code at `port/src/assetcatalog*.c`, `port/include/assetcatalog.h`, `port/src/catalog_mgr_weapons.c`. |
| **input.md** | `imgui.md` (status update) + `infrastructure-pillars-status-2026-04-27.md` Section 2 + code at `port/src/actionmap.cpp`, `inputctx.c`, `inputlayer.c`, `pdgui_backend.cpp`. |
| **menus.md** | `imgui.md` overlap + `infrastructure-pillars-status-2026-04-27.md` Section 3 + code at `port/fast3d/pdgui_menu_*.cpp`, `port/src/menupool.c`, `port/src/menugraph.c`. |
| **modding.md** | `component-mod-architecture.md` (large, current architecture) + `infrastructure-pillars-status-2026-04-27.md` Section 4 + code at `port/src/modmgr.c`, `port/src/modarchive.c`, `port/src/modvfs.c`, `port/src/net/netmanifest.c`, `port/src/net/netdistrib.c`. |
| **connectivity.md** | Replaces 4 current docs (network-architecture.md / network-system-audit.md / networking.md / nat-traversal-architecture.md). Sources: `infrastructure-pillars-status-2026-04-27.md` Section 5 + still-current bits from connectivity-and-modern-main-menu.md + current wire protocol changelog from `port/include/net/net.h:12`. |
| **save-wire-format.md** | `infrastructure-pillars-status-2026-04-27.md` Section 8 + constraint ledger entries on protocol versioning + code at `port/include/savefile.h`, `port/src/savemigrate.c`, `port/include/net/net.h`. |
| **rendering.md** | `imgui.md` rendering parts + code at `port/fast3d/gfx_pc.cpp`, `port/fast3d/pdgui_theme.cpp`. |
| **build-dev-tooling.md** | `build.md` + `update-system.md` + `config-pd-ini-audit.md` + `infrastructure-pillars-status-2026-04-27.md` Section 7 + code at `devtools/`. |
| **tests.md** | `designs/testing-framework-2026-04-26.md` + `infrastructure-pillars-status-2026-04-27.md` Section 6 + code at `tests/`, CMakeLists.txt:840-979. |
| **physics-collision.md** | Updated `collision.md` + `movement.md` content + code at `src/lib/capsule.c`, bondwalk.c. |
| **server.md** | `server-architecture.md` (has S486 note already) + `b12-participant-system.md` (folded in) + `designs/hosting-modes-listen-vs-dedicated.md` (still active). |

### 3.3 `designs/` (active design references)

Sub-bucketed by pillar so each design is colocated with its pillar context. Only designs that are still relevant (not yet shipped, or in-progress, or actively cited) live here. Shipped designs move to `_old/designs-shipped/` per `retention.md`.

The proposed sub-buckets and their contents are listed in Section 2's tree. A design moves out of `in-flight/` into the appropriate pillar bucket (or to `_old/`) once it ships or stalls; `in-flight/` is the staging area for active proposals and works-in-progress.

### 3.4 `audits/` (recent point-in-time assessments)

14-day window. The 7 currently-active audits per [context-cleanup-plan-2026-04-30.md Section D.1](context-cleanup-plan-2026-04-30.md) carry forward. Older audits go to `_old/audits/`.

### 3.5 `daily-logs/` (heavy-day summaries)

Convention: write a daily-log on any day with >= 3 worktree sessions OR a major feature line shipping. 30-day rolling window before archive. The two existing entries (2026-04-12, 2026-04-17) carry forward.

This is the recommendation per Section H.6 of the cleanup plan; if Mike picks differently the directory either disappears entirely or expands to mandatory daily.

### 3.6 `_old/` (everything not carried forward)

Out-of-tree archive. Path proposal options for Mike's call:
- **A. `_old/` at repo root.** Simple, clearly out of `context/`. Sibling to `port/`, `src/`, `tests/`.
- **B. `context-archive-2026-04-30/` at repo root.** Dated folder name signals a one-time event.
- **C. `context/_old/` nested.** Stays inside `context/` but prefixed `_` so tools that respect that convention skip it. Disadvantage: still in the live tree.

Recommendation: **A**. Cleanest separation; signals "this is no longer the live context."

---

## 4. What carries forward, what gets dropped

### 4.1 Direct carry-forward (no rewriting needed)

- `constraints.md`
- `systemic-bugs.md`
- `working-preferences.md`
- `bugs.md` (light touch-up only)
- The 7 currently-active audits per Section D.1
- The 27+ design docs marked KEEP in Section C.1 (sub-bucketed into pillar folders)
- `daily-logs/2026-04-12.md`, `daily-logs/2026-04-17.md`

### 4.2 Updated content folded into new pillar docs

- `infrastructure.md` -> dies; per-pillar status lives in `pillars/*.md`
- `roadmap.md` -> superseded by ported `full-release-roadmap-2026-04-27.md` content
- `imgui.md` -> folded into `pillars/menus.md` + `pillars/rendering.md`
- `collision.md` + `movement.md` -> folded into `pillars/physics-collision.md`
- `b12-participant-system.md` -> folded into `pillars/server.md` (or `pillars/catalog.md`, TBD)
- `component-mod-architecture.md` -> folded into `pillars/modding.md`
- `network-architecture.md` + `network-system-audit.md` + `networking.md` + `designs/nat-traversal-architecture.md` -> folded into `pillars/connectivity.md`
- `build.md` + `update-system.md` + `config-pd-ini-audit.md` -> folded into `pillars/build-dev-tooling.md`
- `server-architecture.md` -> folded into `pillars/server.md`
- `memory-modernization.md` -> folded into a shipped-phase reference paragraph in `pillars/build-dev-tooling.md` or dropped (D-MEM is done; no live content needed)
- `qc-tests.md` -> dropped (replaced by per-build checklist convention if Mike wants one; otherwise just dropped)
- `4-20-CRITICAL-STABILITY-BUGS.md` -> dropped (bugs.md is authoritative)
- `pd2-codex-onboarding-prompt.md` -> dropped (README.md replaces this role)
- `QUICKSTART.md` -> dropped (folded into README.md cold-start section)
- `INDEX.md` -> dropped (README.md is the index)
- ADR-001 / ADR-002 / ADR-004 -> dropped (per Section H.1 retire convention; ADR-003 content folds into `pillars/catalog.md`)
- `designs/full-release-roadmap-2026-04-27.md` -> ported to root `roadmap.md`
- `designs/testing-framework-2026-04-26.md` -> folded into `pillars/tests.md`
- `designs/n64-legacy-audit-2026-04-17.md` -> ARCHIVE (one-shot reference, work mostly done)

### 4.3 Dropped designs (shipped or superseded)

Per Section C.2 of the cleanup plan:
- `designs/match-startup-pipeline.md`
- `designs/session-catalog-and-modular-api.md`
- `designs/spawn-system-architecture-2026-04-13.md`
- `designs/d5-full-menu-overhaul.md`
- `designs/d5-ui-polish-plan.md`
- `designs/implementation-plan-mods-and-d5.md`
- `designs/direct-file-access-design-2026-04-17.md`
- `designs/pdmod-unified-mod-format.md` (folded into pillars/modding.md as live ref; original archived)

### 4.4 Pruned outright

- `_docx_extract/` directory
- `PD2_FixPlan_420Bugs.docx`
- `scratch/` (all 13 markdown files plus 4 crash directories archive to `_old/scratch/` rather than prune; per Section H.5 recommendation)

### 4.5 Already in `context/_archive/` today

The existing `context/_archive/` becomes part of `_old/_archive/` (preserves git history). No content reshuffling within.

---

## 5. retention.md (the no-re-accretion rule)

A draft of what `retention.md` will say:

```
# Retention Rules

## Purpose
Prevent the live context tree from re-accreting cruft over time. A doc that
captured value at a moment in time has a finite useful window. After that
window, it lives in _old/ for forensic value, not in the live tree.

## Per-surface windows

| Surface         | Active window                               | Where it goes           |
|-----------------|---------------------------------------------|-------------------------|
| audits/         | 14 days, OR while actively cited            | _old/audits/<year>/     |
| daily-logs/     | 30 days                                     | _old/daily-logs/<year>/ |
| designs/        | While not yet shipped, OR while in-flight   | _old/designs-shipped/   |
| session-log.md  | Last ~100 sessions in active rolling window | _old/session-log/sN-sM/ |
| tasks.md        | Active lanes only; completed slices delete  | session-log.md retains  |
| scratch/        | 14 days                                     | _old/scratch/<date>/    |
| pillars/*.md    | Live always; updated with code changes      | n/a                     |
| constraints.md  | Live always                                 | n/a                     |

## Triggers

- A design doc moves to `_old/designs-shipped/` within 7 days of the design's
  primary scope shipping. The pillar doc that absorbs the live invariant gets
  updated in the same commit.
- An audit moves to `_old/audits/<year>/` 14 days after authorship UNLESS it
  is referenced from tasks.md or an active design. A grep for the audit's
  filename across active context determines this.
- A daily-log moves to `_old/daily-logs/<year>/` 30 days after authorship.
- A scratch file moves to `_old/scratch/<date>/` 14 days after authorship.
- Session-log: when the active rolling window passes ~120 sessions, the oldest
  ~50 are cut to a new tier in `_old/session-log/`.

## Discipline

- AI sessions doing context updates check retention windows and move at-window
  files as part of their commit when they touch nearby content.
- Mike runs a quick retention pass monthly OR when context feels heavy.
```

---

## 6. Sequencing for execution (replaces Section I)

This is what Phase 2B / 3B looks like once Mike approves this proposal. Each step is a separate commit; auto-merge per standing rule. No code changes at any step.

### Step 1: Author retention.md + new README.md skeleton

Write `retention.md` first (rules captured before content moves). Author the new README.md skeleton with section headers but TBD pillar links. Commit.

### Step 2: Build pillars/ docs

Author each `pillars/*.md` doc:
- Read source content (current docs + audit + code).
- Write fresh, grounded in current code reality.
- File:line citations where claims need verification.

Commit per pillar doc (10-11 commits) so each is reviewable on its own. Order suggestion:
1. `pillars/catalog.md` (most cited; affects everything else)
2. `pillars/input.md`
3. `pillars/menus.md`
4. `pillars/modding.md`
5. `pillars/connectivity.md`
6. `pillars/save-wire-format.md`
7. `pillars/server.md`
8. `pillars/build-dev-tooling.md`
9. `pillars/tests.md`
10. `pillars/rendering.md`
11. `pillars/physics-collision.md`

### Step 3: Build root files

Author or carry-forward (one commit each):
- `roadmap.md` (port full-release-roadmap content)
- `procedures.md` (merge of CRITICAL-PROCEDURES + operational bits)
- `tasks.md` (razor-thin rebuild from current open lanes)
- `session-log.md` (cut to last ~100 sessions, archive older to staging)
- Carry-forward `constraints.md`, `systemic-bugs.md`, `working-preferences.md`, `bugs.md`

### Step 4: Sub-bucket designs/ and audits/

Move active design docs into pillar sub-buckets. Move 7 active audits to fresh `audits/`. Commit.

### Step 5: Finalize README.md

Update README skeleton with all live pillar/design/audit links. Final pass. Commit.

### Step 6: Surface "ready to replace" diff for Mike's review

Before destruction, write a brief at `context/audits/context-rebuild-readiness-2026-04-30.md`:
- New tree summary
- Diff of what carried forward vs dropped
- Spot-check sample readings from each pillar doc
- Proposed `_old/` location

**STOP GATE**: Mike reviews the new tree before any old content moves.

### Step 7: Move old to `_old/`

After Mike's greenlight on the new tree:
- Create `_old/` at repo root.
- Move every old context file into it preserving structure.
- Single commit. Build verify n/a (docs only).

### Step 8: Repoint references

- Update root `CLAUDE.md` references that point at old context paths.
- Update `AGENTS.md` if any references.
- Update `.cursor/skills/context-session-start/` if it references specific old files.
- Update any devtools script that grep's old context paths.

### Step 9: Self-archive these audit files

The cleanup plan and this rebuild proposal both move to `_old/audits/` once their work is done.

---

## 7. Decisions for Mike before Phase 2B starts

The 8 Section H decisions from the cleanup plan are mostly obviated by the fresh-build approach. What replaces them:

### 7.1 `_old/` location

Per Section 3.6, recommend `_old/` at repo root. Mike: any preferred alternative?

### 7.2 Pillar list completeness

Proposed pillars: catalog, input, menus, modding, connectivity, save/wire, rendering, build/dev tooling, tests, physics/collision, server. Mike: any pillar missing? Any that should merge or split?

### 7.3 Session-log cut threshold

Proposal: keep last ~100 sessions active, cut S281-S480 to `_old/session-log/`. Mike: ~100 right or different number?

### 7.4 Daily-logs convention

Per the cleanup plan Section H.6 recommendation: heavy-day-only, 30-day window. Mike: keep that or different?

### 7.5 ADR convention

Per the cleanup plan Section H.1 recommendation: retire entirely; ADR-003's content folds into `pillars/catalog.md`. Mike: confirm retire?

### 7.6 What to do about `pdmod-unified-mod-format.md`

Two options: fold its current content into `pillars/modding.md` (recommended; it's a real implementation reference now) and archive original; OR keep as a standalone design doc in `designs/modding/`. Mike: which?

### 7.7 Forge doc

Per the cleanup plan Section H.3 recommendation: update in place to mark Phase 0-1 done, capture future phases. The new structure puts it at `designs/modding/forge-level-editor.md`. Mike: confirm update path?

### 7.8 Naming convention application

Mike said work titles follow `[Pillar] - [Goal]` format ("Input - Controller Support", "Catalog - Gate 3 Asset Migration"). Proposal: apply to session titles, design doc titles, task labels. File names stay readable (`catalog.md`, `input.md`) since folders provide the pillar context. Mike: confirm scope?

---

## 8. What this proposal deliberately does not do

- **Doesn't draft pillar content yet.** Pillar docs are authored in Phase 2B Step 2 after Mike approves the structure.
- **Doesn't move any files.** Phase 3B Step 7 is the destructive step; gated on Mike's review of new tree at Step 6.
- **Doesn't change code.** All cleanup is markdown.
- **Doesn't pre-judge content fidelity.** The carry-forward / fold / drop categorizations in Section 4 are based on the Phase 1 audit; if Mike spot-checks one and disagrees, easy to reclassify before authoring.
- **Doesn't lock retention.md content.** Section 5 is a draft; Mike can revise the windows before it lands.

---

## 9. Approval gate

Mike approves Section 2 (structure), Section 3 (per-surface description), Section 4 (carry/fold/drop), Section 5 (retention rules draft), and Section 7 (decisions 7.1-7.8). On approval: I run Phase 2B (Steps 1-5) + surface readiness brief at Step 6 for a second-stop-gate review before Phase 3B (Step 7) destruction.

After context rebuild lands, the queue is **Input - Controller Support** (Branch 2 Cohorts 5-8, menus, full controller support), then **Catalog - Gate 3 Migration** (F11-F13 + Gate 3 catalog), per Mike's directive.
