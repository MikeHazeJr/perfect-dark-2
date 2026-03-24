# Game Director Process Audit — Session 35

> **Date**: 2026-03-23
> **Scope**: Sessions 1–35, complete project history review
> **Assessor**: Engineering partner (AI), reviewing the game director's process, decisions, and workflow
> **Purpose**: Honest self-requested audit. What's working, what's not, what should change.

---

## Executive Summary

Across 35 sessions and roughly 6 days of active development, you've taken a decompiled N64 codebase and built it into something that has: a component-based mod architecture, a dedicated server with lobby system, an ImGui overlay with PD-authentic styling, 17 tracked bugs (10 fixed), 6 systemic pattern catalogs, 3 ADRs, a 16-phase roadmap, and a context management system that survives session loss. That's a significant body of work for a solo developer working with an AI engineering partner.

The verdict: your instincts as a director are strong. Your process discipline is above what most solo projects achieve. But there are specific patterns — mostly around pacing, closure, and release discipline — that are costing you momentum and creating silent risk.

---

## Part 1: What You're Doing Well

### 1.1 Architectural Decision Quality

Your big calls have been consistently good:

- **D3 → D3R redesign (S27)**: Killing the monolithic mod system and replacing it with component-based architecture was the right call at the right time. You didn't sunk-cost it. The three-layer design (filesystem → catalog → composition) is clean and scalable.
- **Name-based resolution as a hard constraint (S27)**: This eliminated an entire class of bugs (index-shift when mods change). B-13, B-17, and every future "wrong asset loaded" bug traces back to numeric lookups. Making this a project-wide constraint rather than a per-fix decision was architecturally mature.
- **Single-branch workflow (S8)**: Removing the release/dev split eliminated the branch divergence that caused S7's multi-bug crisis. Good pattern recognition — you saw one bad outcome and removed the structural cause.
- **Dedicated-server-only model (S3)**: Cutting host-based multiplayer simplified the network architecture substantially. One-way doors should be walked through deliberately, and you did.
- **D4 superseded (S22)**: Recognizing that the storybook/preview system was over-engineering the menu migration — and that menus could be built directly via hotswap — was a good course correction.

### 1.2 Context System Discipline

This is the single strongest aspect of your process. The context system has evolved from a monolithic `context.md` (pre-S14) into a well-organized modular structure:

- **README.md** as master index with load-on-demand guidance
- **Constraint ledger** with dated removals and rationale — this has prevented wasted work multiple times
- **Session log archives** properly split (1–6, 7–13, 14–21, 22+) to keep mandatory reads under ~12KB
- **Domain files** loaded only when relevant
- **CLAUDE.md standing orders** that codify the session protocol

You treat context files as project infrastructure, not documentation. That's the right mental model and it's rare.

### 1.3 Bug Discipline

The bug tracking system is well-structured:

- **One-off bugs** (bugs.md) with sequential IDs, severity, root cause, and fix session
- **Systemic patterns** (systemic-bugs.md) with search commands and audit checklists
- **Propagation checks** happening (S6 strncpy audit found 17 additional instances across 8 files)
- **Root cause analysis** consistently deep — B-03 traced from "instant death" through handicap initialization to BSS zero-init

### 1.4 Rabbit Hole Recognition

You've demonstrated the ability to stop when something is going sideways:

- **S34 map cycle test**: Crashed on 5th arena transition due to MEMPOOL_STAGE lifecycle conflict. You recognized this as a fundamental stage lifecycle issue, not a fixable bug, and removed the feature entirely.
- **D4 supersession**: Rather than building an elaborate preview system, you recognized the simpler path.
- The COWORK_START.md explicitly codifies "prefer `/clear` over `/compact`" — you've internalized that pushing through degraded context is worse than restarting clean.

### 1.5 Directive Clarity

When you give direction, it's clear and decisive:

- "Our validated dynamic catalog should be our single source of truth" (S32) — this one sentence drove the entire B-17 fix architecture.
- "The game itself is essentially a mod" (S28) — this framed the override behavior for the entire asset system.
- Option selection is fast: full decomposition (S29 Option 3), dynamic allocation with no cap (S28), dual-hash with no trade-off (S28).

You make decisions when decisions are needed, which keeps sessions productive.

---

## Part 2: What Needs Attention

### 2.1 The Build Test Queue Is Growing — CRITICAL

This is the most significant process risk in the project right now.

| Item | Coded | Sessions Since | Still Untested |
|------|-------|---------------|----------------|
| B-12 Phase 1 (Participant System) | S26 | 9 sessions | Yes |
| B-13 (Prop Scale Fix) | S26 | 9 sessions | Yes |
| D3R-5 Step 4 (Arena Registration) | S35 | 0 sessions | Yes |
| D13 (Update System) | S8–S11 | 24 sessions | Yes (blocked on libcurl) |
| D2b (Capsule Collision) | S15 | 20 sessions | Partially |
| Look Inversion | S22 | 13 sessions | Yes |
| Updater Diagnostics | S22 | 13 sessions | Yes |

You're coding faster than you're testing. The gap between B-12 Phase 1 (coded S26) and now (S35) is nine sessions of additional code layered on top of unverified foundations. If B-12 Phase 1 has a build error or behavioral bug, everything built since S26 that touches participant/chrslot code is potentially affected.

D13 is the most striking case: a complete update system was coded in sessions 8–11, and 24 sessions later, `libcurl` still hasn't been installed. That's a working feature sitting dormant for the entire project history since week one.

**Recommendation**: Establish a "no new features until the test queue is clear" rule. Before starting D3R-5 Tier 1 or any new phase work, build and test everything in the queue. This is the single highest-leverage process change you can make.

### 2.2 Session Intensity Without Recovery — HIGH

Sessions 7–35 (29 sessions) all occurred in roughly 3 days (March 21–23). That's approximately 10 sessions per day. Each session involves deep architectural reasoning, code review, and context management.

This pace has been productive — the volume of work is impressive — but it carries risks:

- **Decision fatigue**: Later sessions in a day may produce lower-quality decisions than earlier ones. You wouldn't notice this in the moment.
- **Build test debt compounds**: The build queue grows because you're generating code faster than you can switch context to compile and test.
- **Context system strain**: Even with good modular files, the session logs are accumulating faster than they can be validated against reality.

**Recommendation**: Consider a deliberate rhythm: 2–3 coding sessions, then 1 "build and test" session where the entire queue gets compiled, tested, and results recorded. Not as a rule that kills momentum — as a practice that prevents the test queue from becoming an archaeological dig.

### 2.3 CHANGES.md Is Empty — MEDIUM

You have two release notes files (v0.0.1 and v0.0.2) with good detail. But `CHANGES.md` — the active changelog template — is completely empty despite significant work since v0.0.2. None of the following are tracked in a release-oriented format:

- D3R-1 through D3R-4 (complete component mod foundation)
- Stage decoupling Phase 1
- B-12 Participant System Phase 1
- 7 bug fixes (B-04 through B-09b)
- CI corruption fix
- Pause menu system
- Build tool improvements (commit button, commit details)

The session log captures what was done in engineering terms. But when it comes time to release v0.0.3, you'll need to reconstruct the user-facing changelog from 13+ session logs across multiple archive files. That reconstruction is error-prone and time-consuming.

**Recommendation**: Update CHANGES.md as you go, the same way you update context files. One line per feature/fix, in user-facing language. When release time comes, the work is already done.

### 2.4 Stale Documentation Acknowledged But Not Fixed — LOW-MEDIUM

Several known documentation issues are tracked but haven't been addressed:

- **B-11**: rendering-trace.md stale header (claims no ImGui menus — false since S1)
- **menu-storyboard.md**: partially superseded (noted in tasks-current.md backlog)
- **COWORK_START.md**: References `context/tasks.md` (old filename), `Protocol version: 18` (now 19), `D3e — IMMEDIATE NEXT STEP` (superseded by D3R), `Active Bug: Font Corruption` (resolved). The "Active Task" section references a bug that was the focus around S1–S2.
- **roadmap.md**: Says "Last updated: 2026-03-20" but doesn't reflect D3R-1 through D3R-5, or the reordering that happened in S27.

None of these are blocking, but they represent a growing gap between what the files say and what's true. For a project that treats context as infrastructure, stale context is equivalent to stale code — it'll mislead a future session.

**Recommendation**: Dedicate one short session to a documentation sweep. Update or deprecate everything flagged. Add a "staleness check" to the session-end protocol: "Are any context files I loaded today out of date?"

### 2.5 Parallel Work Streams Without Closure — MEDIUM

You have several work streams open simultaneously:

| Stream | Status | Blocker |
|--------|--------|---------|
| D3R (Component Mods) | D3R-5 Step 4 coded, in progress | Build test |
| D-PART (Participant System) | Phase 1 coded | Build test |
| D-STAGE (Stage Decoupling) | Phase 1 done | Phase 2 needs design confirmation |
| D-MEM (Memory Modernization) | M0–M1 done | M2 not started |
| D13 (Update System) | All code written | libcurl installation |
| D9 (Dedicated Server) | Largely done | End-to-end playtest |
| D2b (Capsule Collision) | Implemented | Movement testing |

Seven partially-complete streams. Each one carries mental overhead and represents incomplete value — the work is done but the benefit isn't delivered until it's tested and integrated.

The risk isn't that any individual stream is poorly managed. It's that the aggregate cognitive load of tracking seven open fronts makes it harder to see the critical path clearly. And when streams depend on each other (B-12 Phase 2 depends on Phase 1 build test, D3R-5 Tier 1 depends on Step 4 build test), the dependency chain can only move as fast as the slowest blocker — which is usually "Mike needs to compile."

**Recommendation**: Explicitly prioritize closure over new starts. A completed, tested, verified phase is worth more than three phases at 70%.

---

## Part 3: Structural Risks

### 3.1 No Automated Testing

All testing is manual: Mike builds, runs the game, observes behavior, reports back. For a project with 17 tracked bugs, 6 systemic patterns, and 100+ files modified across 35 sessions, the absence of any automated regression testing is a growing risk.

Every future change could reintroduce a fixed bug, and the only way to catch it is a manual playtest that happens to exercise that specific code path. The B-03 handicap bug, for instance, would be trivially caught by a unit test that asserts `handicap != 0` at match start. The SP-1 MAX_PLAYERS indexing pattern could be caught by a bounds-checking test harness.

This isn't a recommendation to build a full test suite tomorrow. But a minimal smoke test — "does the executable start, does it reach the menu, does a 1-bot match start and end without crash" — would catch the most common regression class (build-breaking changes and immediate crashes).

**Recommendation**: Consider a simple batch script that launches the game, waits N seconds, and checks the exit code. Even that primitive check would catch the "crash on boot" regressions that ate sessions 12–13.

### 3.2 Single Point of Failure: The Build Environment

You are the only person who can compile this project. The AI cannot build. If your Windows/MSYS2 environment breaks, there is no backup. The build instructions exist in build.md but haven't been tested by a second person.

For a project that plans to distribute builds (D13 update system, GitHub releases), the bus factor of 1 is worth acknowledging.

**Recommendation**: Document the exact MSYS2 package list and build steps well enough that a second person could reproduce the environment. This also helps if you ever need to set up on a new machine.

### 3.3 The "Coded, Not Compiled" Gap

The AI writes code that it cannot verify compiles. Every session produces code that must survive its first encounter with the compiler. The project has developed good practices around this (type safety reviews, constraint checks, propagation checks), but there's an inherent fragility: the AI can reason about correctness but cannot prove it.

Sessions 12–13 are the canonical example: `catalogValidateAll()` was placed before subsystem initialization, causing 151 consecutive access violations. The logic was sound; the init ordering assumption was wrong. No amount of code review by the AI would have caught this — it required running the executable.

**Recommendation**: Continue the current practice of thorough code review before delivery, but treat the "build test" step as a first-class project activity, not a deferred chore. The code isn't done when it's written — it's done when it compiles and runs.

---

## Part 4: Process Improvement Recommendations (Prioritized)

| Priority | Recommendation | Effort | Impact |
|----------|---------------|--------|--------|
| 1 | **Clear the build test queue before new features** | Low | HIGH — unblocks 3+ dependency chains |
| 2 | **Install libcurl** (`pacman -S mingw-w64-x86_64-curl`) | 5 minutes | HIGH — unblocks entire D13 update system |
| 3 | **Adopt build-test-code rhythm** (2-3 code sessions, then 1 test session) | Low | HIGH — prevents queue accumulation |
| 4 | **Update CHANGES.md incrementally** | Low | MEDIUM — prevents release reconstruction |
| 5 | **Documentation sweep** (COWORK_START.md, roadmap.md, rendering-trace.md) | 1 session | MEDIUM — prevents context drift |
| 6 | **Prioritize closure over new starts** | Discipline | MEDIUM — reduces cognitive load |
| 7 | **Minimal smoke test script** | 1 session | MEDIUM — catches crash regressions |
| 8 | **Build environment documentation** | 1 session | LOW now, HIGH later |

---

## Part 5: What the Numbers Say

### Velocity

| Metric | Value |
|--------|-------|
| Total sessions | 35 |
| Calendar days | ~6 (March 17–23) |
| Phases fully complete | 4 (D1, D2a, D3a–d, D9 largely) |
| Phases in progress | 7 (D2b, D3R, D-MEM, D-STAGE, D-PART, D13, D4 ongoing) |
| Phases planned | 11 |
| Bugs found | 17 |
| Bugs fixed | 10 (59%) |
| Systemic patterns cataloged | 6 |
| ADRs written | 3 |
| New source files created | ~25+ |
| Context files maintained | 32 |

### Decision Turnaround

When presented with options (ADR-002 three options, ADR-003 three open questions, D3R design alternatives), you typically decide within the same session. No decision has been reversed after being made. This is fast and decisive without being reckless — the ADR format ensures trade-offs are visible before the decision.

### Constraint Ledger Effectiveness

12 removed constraints documented with date and rationale. At least 3 occasions where checking the ledger prevented work on a removed constraint (N64 micro-optimization, shared memory pools, monolithic mod structure). The constraint ledger is paying for itself.

---

## Part 6: The Honest Assessment

You asked to be audited, so here's the unvarnished version.

**As a game director**, you're strong. You have clear vision, you make decisions when they're needed, you're willing to kill work that isn't serving the project, and you treat your engineering partner as a collaborator rather than a tool. Your instinct for when something is a rabbit hole versus when it's worth pursuing has been reliable.

**As a project manager**, you have a gap. The build test queue, the parallel open streams, and the stale documentation all point to the same underlying pattern: you're more energized by designing and building than by closing and verifying. That's completely natural — the creative work is more engaging than the janitorial work. But in a solo project, you are also the QA team, the release manager, and the documentation maintainer. Those roles don't get to be deferred.

**As a technical decision-maker**, your batting average is high. The D3R redesign, the constraint ledger, the name-based resolution mandate, the single-branch workflow — these are all decisions that a senior engineer would be proud of. The one area where I'd push you is on testing infrastructure: you're building a system complex enough that manual testing alone won't scale.

The project is in a good place. The architecture is sound, the context system is mature, and the roadmap is well-prioritized. The highest-leverage thing you can do right now isn't to write more code — it's to build what you've already written, test it, fix what breaks, update the changelog, and give yourself a clean foundation to build the next phase on.

---

> *This audit covers the project state as of Session 35. It should be reviewed and updated if the process changes significantly. Filed in `context/` for future session reference.*
