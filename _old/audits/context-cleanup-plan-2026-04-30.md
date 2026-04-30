# Context System Cleanup Plan

> **Date written**: 2026-04-30 (Mike's directive used the date 2026-04-27 in path; using today's actual date for accuracy. Cross-references kept to that string where relevant).
> **Mode**: Phase 1 audit + plan only. No file moves, no rewrites, no deletions in this commit.
> **Methodology**: Two-phase. Phase 1 inventories every file under `context/`, classifies it, surfaces decisions that need Mike's call. Phase 2 (separate, post-approval) executes the plan in bisectable groups.
> **Scope ceiling**: 162 files inventoried (excluding `_docx_extract/` XML artifacts and crash log/PNG binaries). 88,705 total markdown lines.
> **Cleanup principles** (per Mike's framing): Concise. Specific. Grounded in code reality. Living, not living forever. Single source of truth per topic.
> **No em-dashes anywhere** (Windows PowerShell hygiene).

---

## Executive Summary

The context system is bloated and has measurable drift against the code. The two recent audits ([infrastructure-pillars-status-2026-04-27.md](infrastructure-pillars-status-2026-04-27.md), [codebase-architecture-rating-2026-04-27.md](codebase-architecture-rating-2026-04-27.md)) already mapped the worst of it; this plan finishes the job.

**Root cause of bloat**: every plan, audit, and decision was committed forward with a shipped/superseded label or none at all, with no recurring archive cadence. `tasks-current.md` swelled from "razor-thin punch list" to 2619 lines of completed slice descriptions. `session-log.md` is 11554 lines. `infrastructure.md` froze at S361 / wire protocol v37 while live is S590+ / v46. Several design docs say "Status: Design (not yet implemented)" while the code shipped weeks ago.

**Top severity findings**:

1. **`README.md` is truncated mid-line at line 142** (last visible content: `| [designs/v`). This is the master index; its end is missing.
2. **`INDEX.md` is truncated mid-line at line 48** (last visible content: `**One source of truth**`). Same kind of damage.
3. **`infrastructure.md` is ~2 weeks and ~230 sessions behind code reality.** Header says S361 / wire v37 / v0.0.120; code is S590+ / v46 / v0.0.175+ ([port/include/net/net.h:12](port/include/net/net.h:12)).
4. **`designs/nat-traversal-architecture.md` says "Status: Design (not yet implemented)" 2026-03-30**, but code has full 6-tier P2P (LAN/DIRECT/STUN/UPnP/ICE/TURN) at [port/src/net/p2p.c](port/src/net/p2p.c:1) and tier files [port/src/net/p2p_lan.c](port/src/net/p2p_lan.c:1) through [port/src/net/p2p_turn.c](port/src/net/p2p_turn.c:1).
5. **`tasks-current.md` is bloated with completed slice logs.** 46 "Open" headers, oldest from 2026-04-13 covering work in `_archive/designs/2026-04-13/`. Header self-describes as "razor-thin: only what needs doing" but ~80% of body is shipped-work narrative.
6. **`network-architecture.md` says protocol v35 and "all non-local MP through dedicated server"** ([context/network-architecture.md:14, 36](network-architecture.md:14)). Live is v46, and S486 (2026-04-27) pivoted shipping scope to in-client listen-host (recorded in [constraints.md:16](../constraints.md:16)).
7. **`network-system-audit.md` is dated 2026-03-27, claims v27** ([context/network-system-audit.md:14](network-system-audit.md:14)). Now a historical artifact; live is v46.
8. **`b12-participant-system.md` says "Phase 3 pending"** but Phase 3 shipped S324 2026-04-17 with `chrslots` removed and protocol bumped to v37 (per [infrastructure.md:78](../infrastructure.md:78)).
9. **`imgui.md` says "Game menu migration (D3e+) not yet started"** ([imgui.md:4](../imgui.md:4)) but P10 D5.7 "ImGui sole menu system" shipped S184 2026-04-08 ([constraints.md:46](../constraints.md:46)).
10. **`component-mod-architecture.md` says "Status: Design phase, awaiting implementation"** but D3R-1 through D3R-11 all shipped per [infrastructure.md:119](../infrastructure.md:119).
11. **`_docx_extract/` directory contains 12 raw .docx XML files.** These are unpacked Office Open XML artifacts; not human-readable context. Pure noise.
12. **`scratch/crash-2026-04-13/` directory holds 16-day-old playtest logs and PNGs**, kept as forensic evidence for bugs already fixed. Move out of live tree.

---

## Section A: Methodology

### A.1 Classification taxonomy

- **KEEP**: file is current, accurate, and earns its keep. No action.
- **UPDATE**: intent is right, facts are stale. Bring in line with code reality without changing scope.
- **CONSOLIDATE**: overlaps another file. Merge target identified.
- **ARCHIVE**: shipped or superseded. Move to `context/_archive/...` to preserve history without polluting the live index.
- **PRUNE**: doesn't earn its keep at all. Delete.
- **ASK**: ambiguous. Mike's call.

### A.2 Evidence ground rules

Every classification carries either a file:line citation, a session-log reference, or a comparison between a stated status and a code observation. Possibility framing on subjective judgments (`appears to`, `looks like`, `flag for Mike`).

### A.3 Sequencing for Phase 2

When Mike approves, execute in this order, one commit per group:

1. Truncation repairs (README.md, INDEX.md) - urgent because they are entry points.
2. UPDATE batch (high-traffic canonical files: infrastructure, roadmap, QUICKSTART, network-architecture, etc.).
3. CONSOLIDATE batch (network docs, ADR scatter).
4. ARCHIVE batch (completed design plans, stale audits).
5. PRUNE batch (`_docx_extract/`, crash logs, etc.).

Each Phase 2 commit is docs-only; auto-merge per standing rule. Build verify is a no-op for docs-only commits.

---

## Section B: Top-level canonical files (`context/*.md`)

### B.1 Entry points and indexes

| File | Lines | Classification | Action | Justification |
|------|-------|---------------|--------|---------------|
| [README.md](../README.md) | 141 (truncated) | **UPDATE** (urgent) | Restore truncated tail (cuts at `[designs/v` mid-link, line 142). Refresh "Quick Status" + "Session History" tables for S590+ / v0.0.175+ / v46. Trim the heroic single-line "Last updated" paragraph (lines 3-4) which contains 8 sessions worth of inline content; move to a `_archive/changelog-summaries/` if the prose deserves preservation. | Master hub. Currently broken at the tail; visible drift in dates. |
| [INDEX.md](../INDEX.md) | 48 (truncated) | **UPDATE** (urgent) | Restore truncated tail (cuts at `**One source of truth**`). Update "S281-S361" range to current. Reconcile with README.md as single nav layer. | Secondary nav. Same truncation class. |
| [QUICKSTART.md](../QUICKSTART.md) | 163 | **UPDATE** | Section 1 wire `Protocol v32` -> `v46`. Section 4 "MAX_MPCHRS=36" was bumped to 40 per `participant.h` (verify). Section 6 "Current State (v0.0.56, S185)" -> current build/session. Section 6 open bug list (B-112 / B-118 / B-78 / B-81) all closed per current bugs.md. Last-update tag line 4 says 2026-04-14. | Cold-start onboarding doc. Stale facts make AI confidently wrong on first action. |
| [CRITICAL-PROCEDURES.md](../CRITICAL-PROCEDURES.md) | 101 | **KEEP** with one-line touch | "build-headless mirrors `Get-BuildSteps`" still accurate; recent build-session.ps1 / queued isolated builds (S504/S579/S582) deserve a one-liner pointer. | Mostly procedural and timeless. |
| [working-preferences.md](../working-preferences.md) | 70 | **KEEP** | Updated 2026-04-26 per file footer; matches current collaboration mode. No drift. | The terse preference doc is doing its job. |

### B.2 Rolling status / tracking files

| File | Lines | Classification | Action | Justification |
|------|-------|---------------|--------|---------------|
| [infrastructure.md](../infrastructure.md) | 451 | **UPDATE** (high priority) | Header says "Last updated: 2026-04-18, S361. Wire protocol at v37." Live is v46, S590+. The Phase Status table is broadly correct in shape but missing: S483 spawn-weapon mode, S486 in-client shipping pivot, S507 v46 distribution digest, S511 v46 cutscene, S484 catalog manager F1-F10, the entire menu graph migration, the testing framework as a phase, P4-A onwards on Asset Provider, S577/S578 catalog/provider ownership. Re-write to reflect current truth. | Identified by [infrastructure-pillars-status-2026-04-27.md:511](infrastructure-pillars-status-2026-04-27.md:511). |
| [roadmap.md](../roadmap.md) | (~199) | **UPDATE** | Header says "Build: v0.0.120 \| Protocol: v37 \| Sessions: 361+". Current build v0.0.175+ per recent release commits. Refresh Milestone Summary, "What Remains for v0.1.0" (B-112 / B-118 status both addressable - per audit, B-112 mitigated, B-78/B-81/B-99 closed). Move bullets that point at shipped work into a "completed in vN" table. | Roadmap doc, central to release planning. |
| [tasks-current.md](../tasks-current.md) | 2619 | **UPDATE** (heavy) + **CONSOLIDATE** | Header says "Razor-thin: only what needs doing". 46 "Open" sections, oldest 2026-04-13. ~80% of content is "Done this slice" / "Verification" recap from completed lanes. Rewrite to a 200-300 line punch list of genuinely open work. Move the "Done this slice" narratives into [session-log.md](../session-log.md) entries (or `_archive/tasks-archive.md`). The 2026-04-13 era "Open" headers (S293+) are entirely shipped work and should ARCHIVE; keep only the genuinely-open lanes for 2026-04-28 to 2026-04-29. | Mike said "razor-thin"; doc is everything except. |
| [session-log.md](../session-log.md) | 11554 | **CONSOLIDATE** + **ARCHIVE** | INDEX.md and README.md claim coverage S281-S361 but the file actually goes S281 to S582. Even the audit cited S590, so S583-S590 may be missing; need to either add the missing entries or be explicit about the cutoff. After verification, archive S281-S480 to `_archive/session-log-archive-S480-and-older.md` and keep ~last 100 sessions in the rolling active file. Standing rule in INDEX.md says "Keep session-log.md under ~500 lines"; current is 23x that. | The single largest doc; rolling-window discipline is being violated. |
| [bugs.md](../bugs.md) | 237 (long lines) | **KEEP** with light **UPDATE** | Most recent entries B-289 / B-290 from S575 (2026-04-28) are accurate. Older entries should be propagation-checked: any "fixed pending build" that has now built clean should be promoted to "fixed" with commit SHA. | Active bug ledger; widely cited. |
| [systemic-bugs.md](../systemic-bugs.md) | 463 | **KEEP** | SP-1 to SP-15 entries are well-organized, audit-friendly. SP-9 has the longest narrative but the deep-dive is justified. | Pattern catalog by design; not a tracker. |

### B.3 Domain references (loaded on demand)

| File | Lines | Classification | Action | Justification |
|------|-------|---------------|--------|---------------|
| [collision.md](../collision.md) | 146 | **UPDATE** | Says "Status: ACTIVE DEVELOPMENT". Capsule sweep system is in production per `port/src/capsule.c`. Re-state as "Status: implemented (capsule sweep canonical, slope-AABB and ceiling-jump-through deferred)". | Status drift; otherwise terse and useful. |
| [movement.md](../movement.md) | 72 | **UPDATE** | "Status: ACTIVE DEVELOPMENT" - movement systems are in production. Re-state. | Same drift class as collision.md. |
| [networking.md](../networking.md) | 256 | **UPDATE** | Says "COMPLETE (All Phases) + D3R-9 Distribution". Mostly true in shape but the wire-version cheatsheet (if any) needs a v46 update; no individual claims about specific protocol numbers should appear unless annotated. | Cheatsheet doc; should be small and current. |
| [imgui.md](../imgui.md) | 128 | **UPDATE** | "Status: FOUNDATION COMPLETE (D3d), POLISH ONGOING. Game menu migration (D3e+) not yet started." This is wrong: P10 D5.7 finished S184 (2026-04-08), all 254 dialogs ported. Rewrite Status. Document current ImGui v1.91.8 + theme bundle + chrome style + font mod system. | Significant drift - says migration unstarted while it is canonical menu system. |
| [build.md](../build.md) | 265 | **KEEP** with light **UPDATE** | Touched 2026-04-28 with build-session.ps1 details. Trim early pre-S504 narrative if any; current build flow is canonical. | Largely up to date. |
| [server-architecture.md](../server-architecture.md) | 167 | **KEEP** | Header has S486 shipping note. Current. | Has been refreshed for the in-client pivot. |
| [update-system.md](../update-system.md) | (~140) | **KEEP** with light verification | D13 work is shipped per S245 FIX-F (closed B-99). Verify any version-specific claims still hold. | Reference doc. |
| [memory-modernization.md](../memory-modernization.md) | 358 | **KEEP** with light **UPDATE** | Header status says "COMPLETE M0-M6". Current. Add a footnote about M4 partial revert in S384 (per [full-release-roadmap-2026-04-27.md:E8](designs/full-release-roadmap-2026-04-27.md:E8)). | Active phase tracker for D-MEM; complete. |
| [b12-participant-system.md](../b12-participant-system.md) | 336 | **UPDATE** or **ARCHIVE** | Says "Phase 2 COMPLETE - Phase 3 (remove chrslots) pending". Phase 3 shipped S324 2026-04-17. Either UPDATE the status header to "All 3 phases done" + capture the Phase 3 wire migration (v36 -> v37), or ARCHIVE the original architecture doc and replace with a concise reference in `constraints.md` (which already covers participant pool). | **ASK Mike**: keep as standalone domain doc or fold into constraints? |
| [component-mod-architecture.md](../component-mod-architecture.md) | 578 | **UPDATE** | Status says "Design phase - discussion complete, awaiting implementation". D3R-1 through D3R-11 all shipped. UPDATE to "Implemented; reference architecture for component mods". Validate the prose still describes the actual code. | The architectural reference is real; just has a years-stale status banner. |
| [config-pd-ini-audit.md](../config-pd-ini-audit.md) | 118 | **KEEP** with light **UPDATE** | Last update 2026-04-17 (S313 marathon). Probably has new entries since (Net.Server.Port etc). Touch up. | Useful reference for future config additions. |
| [pd2-codex-onboarding-prompt.md](../pd2-codex-onboarding-prompt.md) | 82 | **KEEP** with one-line **UPDATE** | "Wire format. NET_PROTOCOL_VER currently 45". Live is 46. One number bump. | External-facing onboarding; should match reality. |
| [qc-tests.md](../qc-tests.md) | (~140) | **UPDATE** or **CONSOLIDATE** | Earliest entry "Added: 2026-03-26". Many items predate the current QC cadence and are likely already invalidated. Either re-do for current build, or fold any still-relevant items into a per-build checklist and ARCHIVE the rest. | Stale by design - was never rotated. |
| [4-20-CRITICAL-STABILITY-BUGS.md](../4-20-CRITICAL-STABILITY-BUGS.md) | 97 | **ARCHIVE** | Self-describes as "Working snapshot of open / logged stability and UX issues from 2026-04-20-21 playtests" with note "Authoritative IDs live in bugs.md". The snapshot captured S431/S432 work; that work is done. | The snapshot did its job. Move to `_archive/`. |

### B.4 Architecture decision records (ADRs)

| File | Lines | Classification | Action | Justification |
|------|-------|---------------|--------|---------------|
| [ADR-001-lobby-multiplayer-architecture-audit.md](../ADR-001-lobby-multiplayer-architecture-audit.md) | 68 | **ARCHIVE** | Dated 2026-03-20, scope "Network message protocol audit". A one-shot audit, not an ADR proper. The findings have been propagated into bugs/constraints/network-system-audit. | Was an audit at the time, mislabeled ADR. |
| [ADR-002-component-filesystem-decomposition.md](../ADR-002-component-filesystem-decomposition.md) | (~200) | **ARCHIVE** | 2026-03-23, "Status: Proposed". D3R-1 long since shipped. | Captured the decision; decision is now reality and `component-mod-architecture.md` is the live ref. |
| [ADR-003-asset-catalog-core.md](../ADR-003-asset-catalog-core.md) | 331 | **KEEP** | The actual catalog ADR. Still cited from full-release-roadmap (`B.1 E1`). Keep the historical record at root level. Add a "Status: Implemented" note pointing at current code. | Real ADR; should stay as reference. |
| [ADR-004-dev-window.md](../ADR-004-dev-window.md) | 690 | **ARCHIVE** | Status: Accepted, 2026-03-24. The dev window v1 it describes was superseded by dev-window-v2 (S232+). Long historical narrative on a tool that has since been rewritten. | Tool history, not architecture. Move to `_archive/`. |

**Possibility note**: The four ADR files use the same name prefix but only one (ADR-003) is functioning as a live ADR. **Suggest Mike's call** on whether to keep the ADR-NNN convention going forward or retire it entirely. If retiring: rename ADR-003 to `asset-catalog-core.md` and put it at top level alongside other architecture docs.

### B.5 Word doc + extract

| File | Classification | Action | Justification |
|------|---------------|--------|---------------|
| [PD2_FixPlan_420Bugs.docx](../PD2_FixPlan_420Bugs.docx) | **ARCHIVE** or **PRUNE** | Microsoft Word 2007+ binary. Companion to `4-20-CRITICAL-STABILITY-BUGS.md`. Content has been propagated into bugs.md / session log. **ASK Mike**: prune entirely or move to `_archive/external/`. | Word docs do not belong in a markdown context system; they cannot be searched or grep'd. |
| `_docx_extract/` (directory, 12 XML files) | **PRUNE** | Office Open XML artifacts (`[Content_Types].xml`, `_rels/`, `docProps/`, `word/`). These are the unpacked binary; not human-authored content. Zero retention value. | Pure noise. |

---

## Section C: `context/designs/` (39 files)

### C.1 Active design references (KEEP)

| File | Status (claimed) | Status (code) | Action |
|------|------------------|---------------|--------|
| [designs/full-release-roadmap-2026-04-27.md](designs/full-release-roadmap-2026-04-27.md) | Authored 2026-04-27, 11 decisions resolved | Living - cited by onboarding prompt | **KEEP**. One spot-update: E16 says `NET_PROTOCOL_VER at v44`; live is 46. Minor. |
| [designs/testing-framework-2026-04-26.md](designs/testing-framework-2026-04-26.md) | ADR + execution plan, current | Cohorts shipping | **KEEP**. May need a Cohort-3+ status update. |
| [designs/input-universality-and-transitions-2026-04-27.md](designs/input-universality-and-transitions-2026-04-27.md) | APPROVED Phase 1 / Phase 2 in progress | Layer/scene infra in place | **KEEP**. |
| [designs/gpu-swarm-and-test-scenarios-2026-04-27.md](designs/gpu-swarm-and-test-scenarios-2026-04-27.md) | Phase 1 design / Phase 2 gated on Mike | Pending decisions | **KEEP**. Active design. |
| [designs/catalog-full-pipeline-weapons-2026-04-27.md](designs/catalog-full-pipeline-weapons-2026-04-27.md) | F1-F10 scaffold shipped, F11+ data move pending | Confirmed by recent commits + audit | **KEEP**. |
| [designs/player-init-architectural-fixes-2026-04-26.md](designs/player-init-architectural-fixes-2026-04-26.md) | Draft, pending Mike's greenlight | Awaiting review | **KEEP**. |
| [designs/contextual-input-schemes.md](designs/contextual-input-schemes.md) | J-1 / J-2 / J-3 LANDED | Confirmed - IMC architecture is live | **KEEP**. Could trim landed-section narrative. |
| [designs/input-mapping-menu-rebuild.md](designs/input-mapping-menu-rebuild.md) | Phase 1 design, Phase 2 waiting on L pass | Active | **KEEP**. |
| [designs/input-authority-methodology.md](designs/input-authority-methodology.md) | Methodology doc | Foundational policy | **KEEP**. |
| [designs/input-authority-and-menu-pool-2026-04-13.md](designs/input-authority-and-menu-pool-2026-04-13.md) | Phase 1 + 2 implemented | Confirmed at `port/src/menupool.c` | **KEEP** + verify. Status text already says "ACTIVE". |
| [designs/menu-stack-architecture.md](designs/menu-stack-architecture.md) | DESIGN target, 2026-04-19 | Aspirational; menu pool is in but graph migration partial | **KEEP**. Acts as the target spec. |
| [designs/menu-controller-input-constraints.md](designs/menu-controller-input-constraints.md) | UX contract | Active | **KEEP**. |
| [designs/menu-inventory.md](designs/menu-inventory.md) | Inventory of 120 screens | Implementation key | **KEEP** + audit if 120 number still accurate (31 menu .cpp files exist). |
| [designs/manifest-architecture.md](designs/manifest-architecture.md) | Implemented | Confirmed | **KEEP**. |
| [designs/connectivity-and-modern-main-menu.md](designs/connectivity-and-modern-main-menu.md) | DESIGN READY for Phase 1, 2026-04-24 | Phase 1 / Phase 2 have shipped per session log | **UPDATE**: status -> Phase 1 + 2 implemented; file remains the canonical design reference. |
| [designs/pdmod-unified-mod-format.md](designs/pdmod-unified-mod-format.md) | Design proposal 2026-04-24 | `.pdmod` is wired per [audits/pdmod-migration-2026-04-25.md](audits/pdmod-migration-2026-04-25.md) and netdistrib | **UPDATE**: status -> implemented. |
| [designs/hosting-modes-listen-vs-dedicated.md](designs/hosting-modes-listen-vs-dedicated.md) | Threat model, referenced from CLAUDE.md | Active | **KEEP**. |
| [designs/pd-server-plugin-abi-adr.md](designs/pd-server-plugin-abi-adr.md) | ADR P4-A doc; P4-B/C deferred per S486 | Live design, deferred work | **KEEP**. Status note about S486 deferral. |
| [designs/interest-management-replication.md](designs/interest-management-replication.md) | P5-A / SEC-8 / SEC-9 design draft | Future scaling work | **KEEP**. |
| [designs/visual-scripting-node-taxonomy.md](designs/visual-scripting-node-taxonomy.md) | v0.5.0+ planned | Future v0.5.0 | **KEEP**. |
| [designs/studio-platform-design.md](designs/studio-platform-design.md) | v0.5.0+ planned | Future | **KEEP**. |
| [designs/issue-10-rigging-aware-body-head-linkage.md](designs/issue-10-rigging-aware-body-head-linkage.md) | SCOPED, not implemented | Future | **KEEP**. |
| [designs/mod-enablement-policy.md](designs/mod-enablement-policy.md) | Active policy 2026-04-13 | Active | **KEEP**. |
| [designs/n64-legacy-audit-2026-04-17.md](designs/n64-legacy-audit-2026-04-17.md) | Reference doc | Reference | **KEEP**. |
| [designs/theme-bundle-and-per-agent-settings-2026-04-16.md](designs/theme-bundle-and-per-agent-settings-2026-04-16.md) | Partial (theme bundle stubbed S305) | Per-agent shipped S313 | **UPDATE** status to track the per-agent landing. |
| [designs/hud-layer-order.md](designs/hud-layer-order.md) | IMPLEMENTED | Confirmed | **KEEP**. |
| [designs/pdgui-hold-ring.md](designs/pdgui-hold-ring.md) | IMPLEMENTED | Confirmed | **KEEP** (small file). |
| [designs/activemenu-radial-architecture.md](designs/activemenu-radial-architecture.md) | LEGACY GBI documentation | Reference | **KEEP** (small file). |
| [designs/flat-menu-navigation.md](designs/flat-menu-navigation.md) | System rule, current | Active | **KEEP**. |

### C.2 Shipped designs (ARCHIVE)

These designs predate the current state. Each one's plan was followed; the work shipped; the doc is now historical. Move to `_archive/designs/` to preserve rationale.

| File | Status drift | Justification for ARCHIVE |
|------|--------------|---------------------------|
| [designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md) | "Design (not yet implemented)" 2026-03-30 | Code has 6-tier P2P at `port/src/net/p2p.c` + 7 tier files. Doc covers STUN+hole-punch only; misses ICE / TURN / presence / voice. Complete superseded design. |
| [designs/match-startup-pipeline.md](designs/match-startup-pipeline.md) | "Design - awaiting game director approval" 2026-03-30 | MSP all phases DONE per [infrastructure.md:75](../infrastructure.md:75). |
| [designs/session-catalog-and-modular-api.md](designs/session-catalog-and-modular-api.md) | "Design - awaiting game director approval" 2026-03-31 | SA-1 through SA-7 DONE. |
| [designs/spawn-system-architecture-2026-04-13.md](designs/spawn-system-architecture-2026-04-13.md) | "Design (no code changes)" 2026-04-13 | L1-L4 spawn pool DONE per `port/src/spawnpool.c`. |
| [designs/d5-full-menu-overhaul.md](designs/d5-full-menu-overhaul.md) | "DESIGN - requires game director review" 2026-04-06 | D5 all phases DONE per [infrastructure.md:62](../infrastructure.md:62). 254 dialogs ported. |
| [designs/d5-ui-polish-plan.md](designs/d5-ui-polish-plan.md) | "PLANNED" 2026-04-03 | All D5 phases shipped. |
| [designs/implementation-plan-mods-and-d5.md](designs/implementation-plan-mods-and-d5.md) | "Active - guides all remaining infrastructure" 2026-04-04 | P1-P10 + mod work all DONE. The plan was followed; it's history now. |
| [designs/forge-level-editor-2026-04-16.md](designs/forge-level-editor-2026-04-16.md) | "Design - not yet implemented" 2026-04-16 | F0 shipped S307; F1-F8+ shipped S310/S313/S314/S354/S356 per session log. **ASK Mike**: archive vs UPDATE to "Phase 0-1 implemented; Phase 2 (geometry edit, mission scripting) future". |
| [designs/direct-file-access-design-2026-04-17.md](designs/direct-file-access-design-2026-04-17.md) | "Design (pre-implementation)" 2026-04-17 | Asset Provider Phases 1-3 shipped; Phase 4 partial. Move to archive (or UPDATE with status). |

### C.3 Possible CONSOLIDATE

- **Network design cluster**: [designs/connectivity-and-modern-main-menu.md](designs/connectivity-and-modern-main-menu.md), [designs/nat-traversal-architecture.md](designs/nat-traversal-architecture.md), [network-architecture.md](../network-architecture.md), [network-system-audit.md](../network-system-audit.md), [networking.md](../networking.md). Five docs covering overlapping ground. Suggest:
  - `networking.md` stays as the protocol cheatsheet (current and brief).
  - `network-architecture.md` becomes the canonical living architecture doc; merge in the still-current pieces of `connectivity-and-modern-main-menu.md`.
  - `network-system-audit.md` (2026-03-27, v27) ARCHIVES; replaced by [audits/infrastructure-pillars-status-2026-04-27.md:Section 5](infrastructure-pillars-status-2026-04-27.md) Phase A Connectivity, which is current.
  - `nat-traversal-architecture.md` ARCHIVES (covered above).
  - `connectivity-and-modern-main-menu.md` UPDATES with shipped status and remains as design reference for any unfinished phase.

---

## Section D: `context/audits/` (35 files)

### D.1 Recent canonical audits (KEEP)

| File | Date | Action |
|------|------|--------|
| [audits/infrastructure-pillars-status-2026-04-27.md](audits/infrastructure-pillars-status-2026-04-27.md) | 2026-04-29 | **KEEP**. Just written; acts as the live drift reference for this very cleanup. |
| [audits/codebase-architecture-rating-2026-04-27.md](audits/codebase-architecture-rating-2026-04-27.md) | 2026-04-27 | **KEEP**. Recent objective rating. |
| [audits/catalog-universality-sweep-2026-04-27.md](audits/catalog-universality-sweep-2026-04-27.md) | 2026-04-27 | **KEEP** (or **UPDATE** to mark sweep status). |
| [audits/post-implementation-audit-2026-04-25.md](audits/post-implementation-audit-2026-04-25.md) | 2026-04-25 | **KEEP** for now; archive after a follow-up sweep. |
| [audits/sp-stage-mp-readiness-2026-04-24.md](audits/sp-stage-mp-readiness-2026-04-24.md) | 2026-04-24 | **KEEP**. B-228 still cited as reopened in tasks-current. |
| [audits/flat-menu-navigation-audit-2026-04-25.md](audits/flat-menu-navigation-audit-2026-04-25.md) | 2026-04-25 | **KEEP**. Still active companion to the matching design. |
| [audits/pdmod-verification-matrix-2026-04-25.md](audits/pdmod-verification-matrix-2026-04-25.md) | 2026-04-25 | **KEEP** unless `.pdmod` work has stabilised. |

### D.2 Older daily audits (ARCHIVE candidate)

These were one-shot snapshots; their findings have been propagated into bugs.md / constraints.md / session-log. Move to `_archive/audits/<date>/`.

| File | Date | Notes |
|------|------|-------|
| [audits/2026-04-19-full.md](audits/2026-04-19-full.md) | 2026-04-19 | Full super-audit; superseded by 2026-04-23-full and later. |
| [audits/2026-04-19-catalog-netplay.md](audits/2026-04-19-catalog-netplay.md) | 2026-04-19 | Subsystem audit. |
| [audits/2026-04-19-menus-input.md](audits/2026-04-19-menus-input.md) | 2026-04-19 | Subsystem audit. |
| [audits/2026-04-19-rendering.md](audits/2026-04-19-rendering.md) | 2026-04-19 | Subsystem audit. |
| [audits/2026-04-19-server-security-scaling.md](audits/2026-04-19-server-security-scaling.md) | 2026-04-19 | Subsystem audit. |
| [audits/2026-04-20-full.md](audits/2026-04-20-full.md) | 2026-04-20 | Daily; superseded. |
| [audits/2026-04-21-full.md](audits/2026-04-21-full.md) | 2026-04-21 | Daily; superseded. |
| [audits/2026-04-21-resolution-prompts.md](audits/2026-04-21-resolution-prompts.md) | 2026-04-21 | Decisions captured. |
| [audits/2026-04-23-full.md](audits/2026-04-23-full.md) | 2026-04-23 | Superseded by 2026-04-24-full. |
| [audits/2026-04-24-full.md](audits/2026-04-24-full.md) | 2026-04-24 | Superseded by infra-pillars-status. |
| [audits/foundation-pass-2026-04-23.md](audits/foundation-pass-2026-04-23.md) | 2026-04-23 | One-pass batch; shipped. |
| [audits/systematic-pass-2026-04-23.md](audits/systematic-pass-2026-04-23.md) | 2026-04-23 | One-pass batch; shipped. |
| [audits/evening-decisions-2026-04-23.md](audits/evening-decisions-2026-04-23.md) | 2026-04-23 | Decisions log; merged into design state. |
| [audits/resume-report-2026-04-23.md](audits/resume-report-2026-04-23.md) | 2026-04-23 | Daily summary; daily-log territory. |
| [audits/connectivity-libopus-decision-2026-04-25.md](audits/connectivity-libopus-decision-2026-04-25.md) | 2026-04-25 | Decision shipped (libopus integrated). |
| [audits/connectivity-phase1-decisions.md](audits/connectivity-phase1-decisions.md) | (~2026-04-24) | Phase 1 connectivity shipped. |
| [audits/input-authority-discipline-2026-04-25.md](audits/input-authority-discipline-2026-04-25.md) | 2026-04-25 | Companion to design; design has K.1-K.9 decided 2026-04-27. |
| [audits/debug-shortcuts-audit-2026-04-26.md](audits/debug-shortcuts-audit-2026-04-26.md) | 2026-04-26 | One-shot. |
| [audits/rom-gate-uninit-fields-2026-04-26.md](audits/rom-gate-uninit-fields-2026-04-26.md) | 2026-04-26 | One-shot. |
| [audits/pdmod-migration-2026-04-25.md](audits/pdmod-migration-2026-04-25.md) | 2026-04-25 | Migration done. |
| [audits/weapon-system-deep-audit-2026-04-25.md](audits/weapon-system-deep-audit-2026-04-25.md) | 2026-04-25 | Bug B-246 instrumentation; resolved. |
| [audits/allinone-cull-audit-2026-04-26.md](audits/allinone-cull-audit-2026-04-26.md) | 2026-04-26 | Phase 1 done; Phase 2 in flight. **ASK Mike**: keep until Phase 2 closes? |
| [audits/catalog-migration-bodies-2026-04-26.md](audits/catalog-migration-bodies-2026-04-26.md) | 2026-04-26 | Migration shipped per session log (S471/S472/S473). |
| [audits/catalog-migration-heads-2026-04-26.md](audits/catalog-migration-heads-2026-04-26.md) | 2026-04-26 | Same; shipped. |
| [audits/catalog-migration-maps-2026-04-26.md](audits/catalog-migration-maps-2026-04-26.md) | 2026-04-26 | Same; shipped. |
| [audits/char-init-weapon-spawn-comparison-opus47-2026-04-26.md](audits/char-init-weapon-spawn-comparison-opus47-2026-04-26.md) | 2026-04-26 | Multi-model audit; conclusions captured in player-init design. |
| [audits/char-init-weapon-spawn-comparison-sonnet46-2026-04-26.md](audits/char-init-weapon-spawn-comparison-sonnet46-2026-04-26.md) | 2026-04-26 | Same. |
| [audits/player-init-comparison-upstream-2026-04-26.md](audits/player-init-comparison-upstream-2026-04-26.md) | 2026-04-26 | Findings captured in design draft. |

**Possibility framing**: most of these are good archival material. They captured a moment-in-time decision or finding that is no longer current but has historical value. Move to `_archive/audits/2026-04-week-3/` and `_archive/audits/2026-04-week-4/` to retain forensic traceability without polluting the live audit folder.

### D.3 The cleanup plan itself

| File | Action |
|------|--------|
| [audits/context-cleanup-plan-2026-04-30.md](audits/context-cleanup-plan-2026-04-30.md) (this file) | **KEEP** while Phase 2 runs. After Phase 2 commits land, ARCHIVE this plan into `_archive/audits/` as a permanent record of the cleanup. |

---

## Section E: `context/scratch/` (13 files + crash dirs)

### E.1 Crash log directories

| Path | Action | Justification |
|------|--------|---------------|
| `scratch/crash-2026-04-13/` (PNG + 2 .log) | **PRUNE** or **ARCHIVE** | Forensic evidence from a 2026-04-13 crash; bug fixed long ago. **ASK Mike**: discard or move to `_archive/crashes/`. |
| `scratch/crash-2026-04-13-chicago/` (PNG + 2 .log) | **PRUNE** or **ARCHIVE** | Same. |
| `scratch/crash-2026-04-13-postgame/` (2 .log) | **PRUNE** or **ARCHIVE** | Same. |
| `scratch/playtest-2026-04-13-false-kills/` (1 .log) | **PRUNE** or **ARCHIVE** | Same; bug B-142 was closed S252. |

### E.2 Investigation notes

These are dated investigations that fed specific fixes; the findings have been propagated into bugs.md / session log / fix commits. ARCHIVE candidates.

| File | Date | Action |
|------|------|--------|
| [scratch/dispatch-briefing-2026-04-14.md](scratch/dispatch-briefing-2026-04-14.md) | 2026-04-14 | **ARCHIVE**. Briefing for one session. |
| [scratch/audit-s255-s292-2026-04-16.md](scratch/audit-s255-s292-2026-04-16.md) | 2026-04-16 | **ARCHIVE**. Bounded session audit. |
| [scratch/match-pipeline-investigation-2026-04-16.md](scratch/match-pipeline-investigation-2026-04-16.md) | 2026-04-16 | **ARCHIVE**. |
| [scratch/collision-spawning-investigation-2026-04-16.md](scratch/collision-spawning-investigation-2026-04-16.md) | 2026-04-16 | **ARCHIVE**. |
| [scratch/game-loop-sweep-2026-04-16.md](scratch/game-loop-sweep-2026-04-16.md) | 2026-04-16 | **ARCHIVE**. Cited from session log S303. |
| [scratch/matchsetup-config-audit-2026-04-23.md](scratch/matchsetup-config-audit-2026-04-23.md) | 2026-04-23 | **ARCHIVE**. |
| [scratch/open-bugs-snapshot-2026-04-23.md](scratch/open-bugs-snapshot-2026-04-23.md) | 2026-04-23 | **PRUNE**. Snapshot vs live bugs.md; not running. |
| [scratch/regression-investigation-2026-04-24.md](scratch/regression-investigation-2026-04-24.md) | 2026-04-24 | **ARCHIVE**. |
| [scratch/weapon-bug-radial-hunt-2026-04-24.md](scratch/weapon-bug-radial-hunt-2026-04-24.md) | 2026-04-24 | **ARCHIVE**. |
| [scratch/issue-1-archaeology-2026-04-25.md](scratch/issue-1-archaeology-2026-04-25.md) | 2026-04-25 | **ARCHIVE**. |
| [scratch/issue-1-cursor-archaeology-2026-04-25.md](scratch/issue-1-cursor-archaeology-2026-04-25.md) | 2026-04-25 | **ARCHIVE**. |
| [scratch/issue-1-faceval-2026-04-26.md](scratch/issue-1-faceval-2026-04-26.md) | 2026-04-26 | **ARCHIVE**. |
| [scratch/player-fp-weapon-trace-2026-04-25.md](scratch/player-fp-weapon-trace-2026-04-25.md) | 2026-04-25 | **ARCHIVE**. |

**Possibility framing**: `scratch/` is by convention ephemera. Anything older than ~7 days that isn't actively cited by tasks-current.md should be sweepable. **Suggest Mike** establish a rolling 7-14 day retention rule for `scratch/` going forward to prevent re-bloat.

---

## Section F: `context/daily-logs/` (2 files)

| File | Action |
|------|--------|
| [daily-logs/2026-04-12.md](daily-logs/2026-04-12.md) | **KEEP**. The Audio + Skin Editor + map-import day; massive activity captured uniquely here. Could move to `_archive/daily-logs/` if establishing a rolling rule (e.g. archive after 30 days). |
| [daily-logs/2026-04-17.md](daily-logs/2026-04-17.md) | **KEEP**. The Forge / Asset Provider / D-MEM / D7 day; same. |

**Possibility framing**: only two daily-log files exist. The convention isn't being applied consistently (lots of daily activity has no daily-log entry). Either commit to it (one daily-log per active day, archive after N days) or drop the convention and let session-log carry the load. **Mike's call.**

---

## Section G: `context/_archive/` (already-archived material, 60+ files)

The archive has its own sub-tree. Spot-check finds:

- `_archive/sessions/sessions-01-06.md` ... `sessions-87-119.md`: chunked old session logs. Consistent with current rolling-window policy. **KEEP**.
- `_archive/session-log-archive-S240-and-older.md`, `_archive/session-log-archive-S280-and-older.md`: tier 2 / tier 3 archives. **KEEP**.
- `_archive/designs/`: 12 superseded designs (audio-mod-menu, dev-window-v2, hud-score-panel, input-flow-chart, input-repair-plan, logging-system-upgrade, menu-replacement-plan, player-zero-refactor-plan, scaling-baseline-1080, skin-editor-design, state-transition-audit, plus the 2026-04-13 sub-dir of master-orchestration-plan). **KEEP**. Already archived correctly.
- `_archive/audits/`: 16 historical audits. **KEEP**.
- `_archive/plans/`: 8 superseded plans (catalog-activation-plan, catalog-loading-plan, join-flow-plan, lobby-flow-plan, master-server-plan, multiplayer-plan, plan-catalog-id-migration, room-architecture-plan). **KEEP**.
- `_archive/_deprecated-bug-patterns-old.md`, `_deprecated-tasks-old.md`: explicitly deprecated, prefixed `_`. **KEEP**.
- `_archive/2026-04-05.md`, `_archive/briefing-2026-03-31.md`, `_archive/session-briefing.md`: dated reference dumps. **KEEP**.

No action needed inside `_archive/`. It is doing its job.

---

## Section H: Decisions for Mike

These need explicit calls before Phase 2 execution. Each has a recommendation but is **ASK** until Mike approves.

### H.1 ADR convention going forward

**Question**: Keep the `ADR-NNN-...md` filename pattern, or retire it?
**Observation**: ADR-001 / 002 / 004 are not really ADRs (one is an audit, two captured one-time decisions). Only ADR-003 (asset catalog) functions as a living ADR.
**Recommendation**: ARCHIVE ADR-001/002/004. Rename ADR-003 to `architecture-asset-catalog.md` at the top level. Stop using the ADR-NNN convention; capture decisions in the relevant living doc + constraints.md, and snapshot rationale in audits where useful.

### H.2 b12-participant-system.md

**Question**: KEEP as a living architecture doc (UPDATE to "Phase 3 done") or ARCHIVE (since constraints.md already covers the participant pool invariants)?
**Recommendation**: ARCHIVE. The architecture is now the steady state and `constraints.md` carries the live invariants. Future-you should not have to choose between two source-of-truth docs.

### H.3 forge-level-editor-2026-04-16.md

**Question**: ARCHIVE (Phase 0-1 shipped; design did its job) or UPDATE (mark Phase 0-1 done; add Phase 2 scope for geometry edit / mission scripting)?
**Recommendation**: UPDATE. Forge has clear future phases and the doc is the natural home for them.

### H.4 Word doc + extract

**Question**: PD2_FixPlan_420Bugs.docx + `_docx_extract/` - prune entirely or archive somewhere?
**Recommendation**: PRUNE the `_docx_extract/` artifacts immediately (they are pure XML noise from a one-time extract). Keep `PD2_FixPlan_420Bugs.docx` if the original Word file has external value; otherwise PRUNE alongside the existing `4-20-CRITICAL-STABILITY-BUGS.md` archive move.

### H.5 Crash log retention

**Question**: 4 directories of crash logs / PNGs from 2026-04-13. Discard or archive?
**Recommendation**: ARCHIVE. Disk cost is trivial; forensic value is non-zero for class-of-bugs revisits. Move to `_archive/crashes/2026-04-13/` and add a 60-day retention rule.

### H.6 daily-logs/ convention

**Question**: Commit to it (one per active day, rolling archive) or drop it?
**Recommendation**: KEEP and formalize. Most active days deserve a daily log; the two existing entries proved their value. Rule: write a daily-log on any day with >= 3 sessions; archive when older than 30 days.

### H.7 audits/ archive cadence

**Question**: Retention threshold for daily-style audits in `audits/` vs. `_archive/audits/`?
**Recommendation**: 14 days at top level, archive after that. Special-case: keep "still cited from tasks-current.md or open-design status" entries even if older.

### H.8 README.md and INDEX.md trailing content

**Question**: Both files are truncated. The truncation is a pre-existing condition, not introduced by this audit. Restore from git history (assume earlier complete versions exist) or rewrite from current state?
**Recommendation**: Phase 2 first commit pulls each file's prior complete version from git, applies a current-state diff (S590+ / v46 etc.), and re-commits. If no prior complete version exists, rewrite from scratch with the new section structure.

---

## Section I: Phase 2 execution plan

Once Mike approves the plan above, execute as a sequence of bisectable docs-only commits:

### Commit 1: Truncation repairs
- Restore `README.md` trailing content (lines 142+).
- Restore `INDEX.md` trailing content (lines 49+).
- Refresh both files' cited status (S590+ / v46 / v0.0.175+).

### Commit 2: UPDATE batch (high-traffic canonical)
- `infrastructure.md`: header refresh (S590+ / v46) + Phase Status table additions for catalog manager / menu graph / testing framework / S486 in-client pivot / S577/S578 catalog ownership.
- `roadmap.md`: header refresh + completed-items table.
- `QUICKSTART.md`: protocol / session / build / open-bug list.
- `imgui.md`: status -> ImGui sole menu; D5.7 done.
- `collision.md`: status -> implemented.
- `movement.md`: status -> implemented.
- `networking.md`: protocol cheatsheet bump to v46.
- `component-mod-architecture.md`: status -> implemented (D3R-1..11).
- `pd2-codex-onboarding-prompt.md`: v45 -> v46.

### Commit 3: tasks-current trim
- Strip "Done this slice" narratives where the slice has shipped.
- Move 2026-04-13/04-16/04-17 era "Open" headers to ARCHIVE (entire slice is shipped).
- Keep 2026-04-28 to 2026-04-29 lanes only.
- Final size target: 200-300 lines.

### Commit 4: session-log archive cut
- Archive S281-S480 to `_archive/session-log-archive-S480-and-older.md`.
- Active log retains roughly the last 100 sessions.
- Verify (or add) S583-S590 entries before cutting.

### Commit 5: CONSOLIDATE (network docs)
- ARCHIVE `network-system-audit.md` to `_archive/audits/network-system-audit-2026-03-27.md`.
- ARCHIVE `designs/nat-traversal-architecture.md` to `_archive/designs/nat-traversal-architecture.md`.
- UPDATE `network-architecture.md`: protocol v35 -> v46, in-client shipping pivot, fold in still-current bits from connectivity-and-modern-main-menu.md.
- UPDATE `connectivity-and-modern-main-menu.md`: status -> Phase 1+2 implemented.

### Commit 6: ARCHIVE batch (designs)
- match-startup-pipeline.md -> `_archive/designs/`.
- session-catalog-and-modular-api.md -> `_archive/designs/`.
- spawn-system-architecture-2026-04-13.md -> `_archive/designs/`.
- d5-full-menu-overhaul.md -> `_archive/designs/`.
- d5-ui-polish-plan.md -> `_archive/designs/`.
- implementation-plan-mods-and-d5.md -> `_archive/designs/`.
- direct-file-access-design-2026-04-17.md -> `_archive/designs/`.
- pdmod-unified-mod-format.md UPDATE only (still useful as ref).
- forge-level-editor-2026-04-16.md UPDATE only (per H.3).

### Commit 7: ARCHIVE batch (audits)
- 17 daily / one-shot audits to `_archive/audits/2026-04-week-3/` and `_archive/audits/2026-04-week-4/` per Section D.2.

### Commit 8: ARCHIVE batch (scratch)
- All 13 scratch markdown files to `_archive/scratch/<date>/`.
- Crash log dirs to `_archive/crashes/2026-04-13/`.

### Commit 9: PRUNE batch
- Delete `_docx_extract/` directory.
- Per H.4, optionally delete `PD2_FixPlan_420Bugs.docx`.
- Delete `4-20-CRITICAL-STABILITY-BUGS.md` (after move to `_archive/`).

### Commit 10: ADRs and B.4 stragglers
- Per H.1, ARCHIVE ADR-001 / 002 / 004; rename or fold ADR-003.
- Per H.2, ARCHIVE b12-participant-system.md if Mike approves.

### Commit 11: README / INDEX final reconciliation
- All previous moves and renames reflected in the index.
- Trim README.md "Last updated" hero paragraph (currently ~3 paragraphs of single-line prose) into a normal paragraph.

### Commit 12: Self-archive
- Move this cleanup plan to `_archive/audits/context-cleanup-plan-2026-04-30.md` once Phase 2 finishes.

Each commit is doc-only; auto-merge per the standing rule. Build verification is a no-op (no code changed); the standard `git diff --check` and post-merge line-count verify still apply.

---

## Section J: Numbers

- **Total files inventoried**: 162 markdown / .docx / log / png + binary directories.
- **Total markdown lines**: 88,705.
- **Recommended classifications**:
  - **KEEP**: ~52 files (most domain references, recent designs, recent canonical audits, working-preferences, constraints, systemic-bugs).
  - **UPDATE**: ~16 files (the canonical entry/status files plus a handful of designs whose status drifted).
  - **CONSOLIDATE**: ~5 files (network cluster).
  - **ARCHIVE**: ~50 files (shipped designs, dated audits, scratch, ADRs).
  - **PRUNE**: ~16 files (the `_docx_extract/` XML cluster, optionally the .docx, the snapshot files).
  - **ASK**: 8 explicit decisions for Mike (Section H).

Numbers ignore the `_archive/` subtree (which is already correctly archived) and the binary `_docx_extract/` artifacts, but include them in the PRUNE count.

After Phase 2 runs, the live `context/` tree is targeted to drop from ~88K lines to ~30-35K lines. The reduction is mostly historical content moving into `_archive/`, plus the `tasks-current.md` and `session-log.md` rolling-window resets.

---

## Section K: What this plan deliberately does not do

- **No change to `_archive/`**. The archive is already organized; that work is done.
- **No code changes**. Cleanup is markdown-only.
- **No invention of new index files**. The existing README + INDEX are the indexes; they need refresh, not replacement.
- **No new conventions imposed without Mike's explicit ask**. Any retention-rule recommendations in Section H are flagged as Mike-decision items.
- **No prediction of what future docs should look like**. This plan inventories now and surfaces drift; the post-cleanup steady state should be evaluated separately.

---

## Section L: Approval gate

**Before Phase 2 runs**, this plan needs Mike's explicit yes on at minimum:

1. The classification taxonomy (Section A.1) and execution sequencing (Section A.3 + Section I).
2. Section H decisions (8 items).
3. The truncation repair approach for README.md / INDEX.md (Section H.8).
4. The session-log cut threshold (Section I commit 4).
5. The tasks-current trim threshold (Section I commit 3).

If any individual file classification looks wrong on review, Mike should mark it ASK and I will reclassify before executing.
