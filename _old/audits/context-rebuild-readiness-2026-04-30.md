# Context Rebuild Readiness Brief

> **Date**: 2026-04-30
> **Phase**: 2B Step 10 (stop-gate before Phase 3B destruction).
> **What this is**: a snapshot of the new context tree as built, plus an explicit list of what Phase 3B will move to `_old/`. Mike's call required before anything is destroyed.
> **No em-dashes anywhere.** PowerShell hygiene preserved.

---

## TL;DR

Phase 2B is complete. The new structure is live in `context/` alongside the old residue. `_old/` does not exist yet (except for the session-log archive `_old/session-log/sessions-S281-S480.md` cut in Step 8).

**What's new and ready to be the source of truth (35 files, ~3500 lines):**

- `context/README.md` (164 lines): cold-start orientation, navigation hub.
- `context/retention.md` (95 lines): the rules that prevent re-accretion.
- `context/procedures.md` (241 lines): build verify, git safety, worktree, truncation guard.
- `context/working-preferences.md` (carry-forward, 70 lines).
- `context/constraints.md` (carry-forward, 119 lines): active + removed invariants.
- `context/roadmap.md` (990 lines): ported from `designs/full-release-roadmap-2026-04-27.md` with v44 -> v46 refresh + Current Critical Path section.
- `context/tasks.md` (155 lines): razor-thin punch list, 3-lane critical path.
- `context/session-log.md` (4299 lines): rolling window S481-S590 (S281-S480 archived).
- `context/bugs.md` (carry-forward, 237 lines): open bug ledger.
- `context/systemic-bugs.md` (carry-forward, 463 lines): SP-1 to SP-15 patterns.
- `context/pillars/` (11 docs, ~2000 lines total): live state per architectural subsystem.
- `context/designs/<bucket>/` (28 active designs in 7 pillar folders).
- `context/audits/` (7 active audits + 2 meta from this session).

**What needs Mike's review before destruction:**

1. The new tree (Section A below) is what the project would carry forward.
2. The pending-archive list (Section B below) is what would move to `_old/` in Phase 3B Step 7.
3. Any item in the pending-archive list that Mike wants to keep instead, or any item in the new tree he wants to revise, needs flagging here.

After Mike approves, Phase 3B executes:
- Step 7: move all pending-archive items to `_old/`.
- Step 8: repoint `CLAUDE.md`, `AGENTS.md`, and `.cursor/skills/context-session-start/` references.
- Step 9: self-archive the cleanup plan + rebuild proposal + this readiness brief once rebuild is fully landed.

---

## Section A: The new tree (what stays live)

### A.1 Root files (10, all live)

| File | Lines | Source |
|------|-------|--------|
| [README.md](../README.md) | 164 | Authored fresh (Step 1 skeleton, Step 9 finalized) |
| [constraints.md](../constraints.md) | 119 | Carry-forward (no rewrite needed; current and rich) |
| [procedures.md](../procedures.md) | 241 | Authored fresh (merged CRITICAL-PROCEDURES + working-preferences operational bits) |
| [working-preferences.md](../working-preferences.md) | 70 | Carry-forward |
| [retention.md](../retention.md) | 95 | Authored fresh (the no-re-accretion rules) |
| [roadmap.md](../roadmap.md) | 990 | Ported from `designs/full-release-roadmap-2026-04-27.md` (cp + targeted v44 -> v46 edits + Current Critical Path section) |
| [tasks.md](../tasks.md) | 155 | Authored fresh (razor-thin from current open lanes; drops 46 stale headers) |
| [session-log.md](../session-log.md) | 4299 | Cut to S481-S590 (S281-S480 -> `_old/session-log/`) + new header |
| [bugs.md](../bugs.md) | 237 | Carry-forward |
| [systemic-bugs.md](../systemic-bugs.md) | 463 | Carry-forward (SP-1..SP-15 well-organized, no drift) |

### A.2 Pillars (11, all live)

| File | Lines | Drawn from |
|------|-------|------------|
| [pillars/catalog.md](../pillars/catalog.md) | 201 | ADR-003 + infra-pillars-status Section 1 + designs/catalog-full-pipeline-weapons + constraints + code spot-check |
| [pillars/input.md](../pillars/input.md) | 194 | infra-pillars-status Section 2 + constraints + code spot-check at actionmap.h, inputctx.h, inputlayer.h |
| [pillars/menus.md](../pillars/menus.md) | 171 | infra-pillars-status Section 3 + constraints + code spot-check at menupool.h, menugraph.h, pdgui_nav.h |
| [pillars/modding.md](../pillars/modding.md) | 188 | infra-pillars-status Section 4 + component-mod-architecture + constraints + code spot-check at modmgr.c, modarchive.c, netmanifest.c, netdistrib.c |
| [pillars/connectivity.md](../pillars/connectivity.md) | 217 | infra-pillars-status Section 5 + constraints + code spot-check at net.h (v46), p2p*.c, presence.c, voice.c, connectcode.c |
| [pillars/save-wire-format.md](../pillars/save-wire-format.md) | 144 | infra-pillars-status Section 8 + constraints + code spot-check at savefile.h, mpsetups.h, savemigrate.c, savebuffer.c |
| [pillars/server.md](../pillars/server.md) | 176 | infra-pillars-status + constraints + code spot-check at hub.h (HUB_MAX_ROOMS=4), room.h, participant.h |
| [pillars/build-dev-tooling.md](../pillars/build-dev-tooling.md) | 185 | infra-pillars-status Section 7 + code spot-check at build-headless.ps1, build-session.ps1, release.ps1, updater.c |
| [pillars/tests.md](../pillars/tests.md) | 198 | infra-pillars-status Section 6 + tests/ inventory + run-pd-tests.ps1 scope aliases |
| [pillars/rendering.md](../pillars/rendering.md) | 120 | codebase-architecture-rating + code spot-check at gfx_pc.cpp, gfx_rendering_api.h, pdgui_theme.cpp |
| [pillars/physics-collision.md](../pillars/physics-collision.md) | 118 | code spot-check at capsule.c, bondwalk.c, constants.h MOVEMODE enums |

### A.3 Designs (28 active, sub-bucketed)

Each is git-mv'd from the old flat location. History preserved.

```
designs/catalog/      catalog-full-pipeline-weapons.md
designs/connectivity/ connectivity-and-modern-main-menu.md
                      hosting-modes-listen-vs-dedicated.md
                      interest-management-replication.md
                      manifest-architecture.md
                      pd-server-plugin-abi.md
designs/in-flight/    gpu-swarm-and-test-scenarios.md
                      issue-10-rigging-aware-body-head-linkage.md
                      player-init-architectural-fixes.md
                      testing-framework.md
designs/input/        contextual-input-schemes.md
                      input-authority-methodology.md
                      input-mapping-menu-rebuild.md
                      input-universality-and-transitions.md
                      menu-controller-input-constraints.md
designs/menus/        activemenu-radial-architecture.md
                      flat-menu-navigation.md
                      hud-layer-order.md
                      input-authority-and-menu-pool.md
                      menu-inventory.md
                      menu-stack-architecture.md
                      pdgui-hold-ring.md
designs/modding/      forge-level-editor.md
                      mod-enablement-policy.md
                      pdmod-format.md
                      theme-bundle-and-per-agent-settings.md
designs/platform/     studio-platform.md
                      visual-scripting-node-taxonomy.md
```

### A.4 Audits (7 active, kept at root level)

These remain at `context/audits/` per Section D.1 of the cleanup plan. The 14-day retention window per `retention.md` will eventually move them, but they are still actively cited.

```
audits/infrastructure-pillars-status-2026-04-27.md   (the live drift reference)
audits/codebase-architecture-rating-2026-04-27.md    (objective rating)
audits/catalog-universality-sweep-2026-04-27.md      (sweep status)
audits/post-implementation-audit-2026-04-25.md       (still cited)
audits/sp-stage-mp-readiness-2026-04-24.md           (B-228 reopened)
audits/flat-menu-navigation-audit-2026-04-25.md      (companion to design)
audits/pdmod-verification-matrix-2026-04-25.md       (.pdmod verification)
```

### A.5 Audits (this session's meta, self-archive after rebuild)

```
audits/context-cleanup-plan-2026-04-30.md       (470 lines)
audits/context-rebuild-proposal-2026-04-30.md   (398 lines)
audits/context-rebuild-readiness-2026-04-30.md  (this file)
```

### A.6 _old/ contents so far

Only the session-log archive cut in Step 8:

```
_old/session-log/sessions-S281-S480.md   (7548 lines)
```

Phase 3B Step 7 will populate `_old/` further per Section B.

---

## Section B: Pending archive (what Phase 3B Step 7 moves to `_old/`)

These items are still at their old paths in `context/`. They need to move to `_old/` to complete the rebuild.

### B.1 Top-level root files (24 files to archive)

| File | Why archive |
|------|-------------|
| `context/4-20-CRITICAL-STABILITY-BUGS.md` | Snapshot from 2026-04-20-21 playtest; ID's live in bugs.md |
| `context/ADR-001-lobby-multiplayer-architecture-audit.md` | Mislabeled audit (not a real ADR) |
| `context/ADR-002-component-filesystem-decomposition.md` | D3R-1 shipped long ago |
| `context/ADR-003-asset-catalog-core.md` | Folded into pillars/catalog.md |
| `context/ADR-004-dev-window.md` | Dev window v1; superseded by v2 |
| `context/CRITICAL-PROCEDURES.md` | Folded into procedures.md |
| `context/INDEX.md` | Replaced by README.md (was truncated) |
| `context/QUICKSTART.md` | Folded into README.md cold-start section |
| `context/PD2_FixPlan_420Bugs.docx` | Word doc; per H.4 prune (or archive) |
| `context/_docx_extract/` | XML noise from .docx unzip; per H.4 prune |
| `context/b12-participant-system.md` | Folded into pillars/server.md per H.2 |
| `context/build.md` | Folded into pillars/build-dev-tooling.md |
| `context/collision.md` | Folded into pillars/physics-collision.md |
| `context/component-mod-architecture.md` | Folded into pillars/modding.md |
| `context/config-pd-ini-audit.md` | Folded into pillars/build-dev-tooling.md |
| `context/imgui.md` | Folded into pillars/menus.md + pillars/rendering.md |
| `context/infrastructure.md` | Replaced by pillars/ + roadmap.md |
| `context/memory-modernization.md` | D-MEM done; reference in pillars/build-dev-tooling.md |
| `context/movement.md` | Folded into pillars/physics-collision.md |
| `context/network-architecture.md` | Folded into pillars/connectivity.md |
| `context/network-system-audit.md` | 2026-03-27 audit (claims v27); folded into pillars/connectivity.md |
| `context/networking.md` | Folded into pillars/connectivity.md |
| `context/pd2-codex-onboarding-prompt.md` | Replaced by README.md |
| `context/qc-tests.md` | Stale; per H.6 dropped (not folded) |
| `context/server-architecture.md` | Folded into pillars/server.md |
| `context/tasks-current.md` | Replaced by tasks.md (razor-thin) |
| `context/update-system.md` | Folded into pillars/build-dev-tooling.md |

### B.2 Designs at flat path (10 files to archive as shipped/superseded)

| File | Why archive |
|------|-------------|
| `context/designs/d5-full-menu-overhaul.md` | D5 Phases shipped; "DESIGN" status stale |
| `context/designs/d5-ui-polish-plan.md` | D5 phases shipped |
| `context/designs/direct-file-access-design-2026-04-17.md` | Asset Provider Phases 1-3 shipped; pillar carries gap notes |
| `context/designs/full-release-roadmap-2026-04-27.md` | Now at root as roadmap.md |
| `context/designs/implementation-plan-mods-and-d5.md` | P1-P10 + mods all shipped |
| `context/designs/match-startup-pipeline.md` | MSP all phases shipped |
| `context/designs/n64-legacy-audit-2026-04-17.md` | Reference doc, work mostly done |
| `context/designs/nat-traversal-architecture.md` | Said "not yet implemented" while code has 6-tier P2P |
| `context/designs/session-catalog-and-modular-api.md` | SA-1..SA-7 shipped |
| `context/designs/spawn-system-architecture-2026-04-13.md` | L1-L4 shipped |

### B.3 Audits at root level (28 daily / one-shot to archive)

These get moved to `_old/audits/<year>/` per the 14-day retention rule.

```
2026-04-19-catalog-netplay.md           (subsystem audit, superseded)
2026-04-19-full.md                      (daily, superseded)
2026-04-19-menus-input.md               (subsystem audit)
2026-04-19-rendering.md                 (subsystem audit)
2026-04-19-server-security-scaling.md   (subsystem audit)
2026-04-20-full.md                      (daily, superseded)
2026-04-21-full.md                      (daily, superseded)
2026-04-21-resolution-prompts.md        (decisions captured)
2026-04-23-full.md                      (daily, superseded)
2026-04-24-full.md                      (daily, superseded)
foundation-pass-2026-04-23.md           (one-shot batch)
systematic-pass-2026-04-23.md           (one-shot batch)
evening-decisions-2026-04-23.md         (decisions captured)
resume-report-2026-04-23.md             (daily summary)
connectivity-libopus-decision-2026-04-25.md   (decision shipped)
connectivity-phase1-decisions.md        (Phase 1 shipped)
input-authority-discipline-2026-04-25.md      (companion to design which has K-decisions resolved)
debug-shortcuts-audit-2026-04-26.md     (one-shot)
rom-gate-uninit-fields-2026-04-26.md    (one-shot)
pdmod-migration-2026-04-25.md           (migration done)
weapon-system-deep-audit-2026-04-25.md  (B-246 resolved)
allinone-cull-audit-2026-04-26.md       (Phase 1 done; Phase 2 in flight; ASK Mike per H)
catalog-migration-bodies-2026-04-26.md  (shipped)
catalog-migration-heads-2026-04-26.md   (shipped)
catalog-migration-maps-2026-04-26.md    (shipped)
char-init-weapon-spawn-comparison-opus47-2026-04-26.md  (multi-model audit; design captured)
char-init-weapon-spawn-comparison-sonnet46-2026-04-26.md  (same)
player-init-comparison-upstream-2026-04-26.md  (findings captured in design draft)
```

### B.4 Existing _archive/ (folds into _old/)

The current `context/_archive/` (60+ files) is already correctly archived. Phase 3B Step 7 moves it as a unit to `_old/_archive/` (or `_old/` flat, preserving structure).

### B.5 daily-logs/

Two existing entries (2026-04-12, 2026-04-17) carry forward to live `daily-logs/` per H.6 (heavy-day-only convention). They move out of live tree at 30-day retention; for now they stay.

### B.6 scratch/

The 13 markdown scratch files + 4 crash log directories archive to `_old/scratch/<date>/` per H.5.

---

## Section C: Spot-check sample readings

Per the rebuild proposal Section 6 Step 6: Mike samples each pillar doc to verify content fidelity.

### Sample 1: pillars/catalog.md
- Three-layer architecture description matches code at [port/src/assetcatalog.c](../../port/src/assetcatalog.c) and [port/src/catalog_mgr_weapons.c](../../port/src/catalog_mgr_weapons.c).
- 28 asset types match the enum at [port/include/assetcatalog.h:77](../../port/include/assetcatalog.h:77).
- Typed identity helpers list matches constraints.md S487 entry.
- Known gaps (41 vs 86 slot, scaffold-only loader, EYESPY mutator, const-cast) all sourced from infrastructure-pillars-status-2026-04-27.md Section 1.

### Sample 2: pillars/connectivity.md
- Protocol changelog v27 -> v46 matches [port/include/net/net.h:12](../../port/include/net/net.h:12) header comment.
- 6-tier P2P escalation matches actual file structure (p2p_lan.c, p2p_direct.c, p2p_stun.c, p2p_upnp.c, p2p_ice.c, p2p_turn.c all present).
- S486 in-client shipping pivot referenced from constraints.md.
- Known gaps (ICE peer candidate exchange not wired, STUN reflexive not transmitted, kbps placeholder, UPnP partial port mapping, TURN no public fallback, netholepunch parallel) all sourced from infrastructure-pillars-status Section 5.

### Sample 3: pillars/server.md
- HUB_MAX_ROOMS = 4, HUB_MAX_CLIENTS = 32, NET_MAX_CLIENTS = 32 verified from headers.
- MAX_MPCHRS = 40 verified from [port/include/pdgui_constants.h:23](../../port/include/pdgui_constants.h:23).
- Identity cookie / persistent bans / admin RCON / room password invariants sourced from constraints.md MASTER-C2 / C3 / SEC-14 entries.

### Sample 4: tasks.md
- 3-lane critical path matches Mike's directive 2026-04-30 (Catalog F11-F13 -> Catalog Gate 3 -> Input Cohorts 5-8).
- F11 scope items (loaderPdbaseScan filesystem walk, weapons.pdbase archive, 86 invitem_* struct move) match catalog full-pipeline weapons design.
- B-179/B-182/B-183 torn-modeldef class still listed as open (no resolution since the audit).

---

## Section D: Diff summary (rough)

**Lines added in Phase 2B:**
- 11 pillar docs: ~2000 lines
- README finalized: ~164 lines
- retention.md, procedures.md, tasks.md: ~500 lines
- roadmap.md ported: ~990 lines (was at designs/full-release-roadmap; net new at root)
- session-log archive: 7548 lines moved to _old/

**Lines removed in Phase 2B:**
- session-log.md cut: ~7549 lines (S281-S480 sections)
- README.md: replaced (was 142 truncated lines)

**Net change**: roughly +3700 lines added in `context/` for the new structure, with 7548 moved to `_old/session-log/`. The pending Phase 3B archive moves another roughly 60K lines to `_old/`.

**Final live `context/` size after Phase 3B**: target ~12K lines (down from ~88K). Pillars total ~2000, root files ~3000, designs sub-bucketed ~4000, audits 7 active ~3000.

---

## Section E: Stop-gate

**Mike's call required before Phase 3B Step 7 destruction.**

To approve: confirm that

1. Section A (the new tree) is what should carry forward.
2. Section B.1 through B.6 (the pending archive) is what should move to `_old/`.
3. Spot-check samples in Section C are accurate enough to trust the rest.
4. No item in B.1 / B.2 / B.3 needs to be promoted back to the live tree.
5. No item in A needs to be revised before Phase 3B runs.

If any item needs reclassification, flag it here and I will adjust before destruction.

After approval:

- **Phase 3B Step 7**: move all Section B items to `_old/` paths preserving structure. Single docs-only commit. Auto-merge to dev.
- **Phase 3B Step 8**: repoint references in `CLAUDE.md`, `AGENTS.md`, `.cursor/skills/context-session-start/`, and any devtools script that grep's old context paths.
- **Phase 3B Step 9**: self-archive `audits/context-cleanup-plan-2026-04-30.md`, `audits/context-rebuild-proposal-2026-04-30.md`, `audits/context-rebuild-readiness-2026-04-30.md` to `_old/audits/`.

After Phase 3B lands, the queue is **Catalog - Weapons F11-F13** -> **Catalog - Gate 3 Migration** -> **Input - Controller Support (Cohorts 5-8)** per Mike's directive.

---

## Section F: What this brief deliberately does not do

- **Doesn't move anything.** Phase 3B Step 7 is the destructive step; this brief surfaces what would happen.
- **Doesn't change pillar content.** If the spot-check samples reveal an error, fix in a separate commit before Phase 3B.
- **Doesn't pre-judge audit retention.** Mike may want to keep some currently-flagged-for-archive audits longer; flag them and I will revise.
- **Doesn't touch _archive/.** The existing `context/_archive/` is internally well-organized; it folds into `_old/_archive/` as a unit.
- **Doesn't repoint references yet.** That is Step 8 after destruction is complete.

---

## Where to look

- For the original cleanup plan: [context-cleanup-plan-2026-04-30.md](context-cleanup-plan-2026-04-30.md).
- For the rebuild proposal Mike approved: [context-rebuild-proposal-2026-04-30.md](context-rebuild-proposal-2026-04-30.md).
- For the new tree's entry point: [../README.md](../README.md).
- For the retention rules going forward: [../retention.md](../retention.md).
