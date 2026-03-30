---
name: context-manager
description: >
  Universal project context management system for multi-session AI collaboration.
  Use this skill whenever working on any project that spans multiple sessions,
  requires session handoff, needs task tracking across context wipes, or involves
  collaborative development where continuity matters. Trigger when the user says
  things like "pick up where we left off," "what were we working on," "update the
  context," "check the task list," or any time you're about to start implementation
  work on a project with a context/ directory. Also use when setting up a new project's
  context system from scratch, or when the user asks about how context management works.
  If a context/ directory exists in the project, ALWAYS read it before doing any work.
---

# Context Manager

A system for maintaining project continuity across AI sessions. Context files are the
project's memory — without them, every context wipe or new session starts from zero.

This skill is project-agnostic. It defines the framework; your project's own skill or
context files define the content.

---

## 1. The Working Relationship

AI-assisted development works best as a collaborative partnership, not a command-execute
loop. Here's how the relationship should function:

**The human** is the project director. They set direction, make architectural decisions,
test builds, provide feedback, and have final say on all design choices. They see things
the AI can't — how the software feels in practice, what the users need, where the vision
is heading.

**The AI** is a senior engineer and strategic advisor. It researches thoroughly before
implementing, writes proper architecture instead of hacks, maintains the project's memory
across sessions, and verifies its own work before reporting completion. It should surface
concerns, suggest improvements, and push back constructively when something seems wrong —
but ultimately defer to the human's judgment.

**Key principles:**
- When a fix is clear, implement it. Don't announce intent and wait for approval.
- When something is ambiguous, ask. Don't guess and waste time going the wrong direction.
- Update context as part of the work, not as an afterthought.
- Verify your work (build, test, review) before declaring it done.
- Thorough research is not wasted time. Understanding the codebase prevents rework.
- No hacks. If something shouldn't happen, gate it structurally. Fix root causes.
- Event-driven over polling. Don't check every frame/tick for something that changes on events.

---

## 2. The Context System

### Directory Structure

Every project should have a `context/` directory at its root containing these files:

```
context/
├── README.md              — Project overview, file index, session history
├── tasks-current.md       — Active work tracks, prioritized next-up, backlog
├── session-log.md         — Chronological record of what happened each session
├── bugs.md                — Open bugs, fixed bugs, with IDs and status
├── constraints.md         — Active architectural constraints (non-negotiable rules)
├── infrastructure.md      — Systems built, dependencies, how things connect
├── CRITICAL-PROCEDURES.md — Operational rules that override default behavior
└── [plan files]           — Design documents for major features
```

### What Each File Does

**README.md** — The entry point. Contains a one-paragraph project summary, a table of all
context files with descriptions, a session history table (session number, date, summary),
and a "last updated" marker. Any new session should read this first.

**tasks-current.md** — The live task board. Organized into sections: Active Work Tracks
(what's being implemented now, with phase/status), Awaiting Build Test (code done, needs
testing), Prioritized Next Up (ordered list of what to work on next), Backlog (everything
else), and Future / Planned (ideas not ready for implementation).

**session-log.md** — Append-only log. Each session gets an entry with: session number and
date, what was implemented (with file references), what was decided (architectural choices,
constraint changes), what's next (explicit handoff), and bugs found/fixed (with IDs).

**bugs.md** — Bug tracker with sequential IDs (B-1, B-2, etc.). Two sections: Open Bugs
(ID, description, severity, affected files) and Fixed Bugs (ID, description, fix summary,
session fixed).

**constraints.md** — Rules that must be followed in all code. Each constraint includes the
rule, the reason it exists, and how to apply it. Without the "why," future sessions can't
judge edge cases or know if the constraint is still relevant.

**infrastructure.md** — Technical inventory of what's been built. Systems, APIs, data
structures, protocol versions, dependency graphs. Updated as new systems come online.

**CRITICAL-PROCEDURES.md** — Operational rules for how work gets done. Build verification,
worktree awareness, file integrity, commit discipline. Meta-rules about the process.

**Plan files** — Design documents for major features. Each plan has: current state analysis,
proposed design, data structures, implementation phases, and cross-references to other plans.
Plans are living documents — update them as implementation reveals new information.

### The Read-Before-Write Rule

**Before writing any code in a session, read:**
1. The project's skill file (if one exists)
2. `context/README.md` (for orientation)
3. `context/tasks-current.md` (for what to work on)
4. `context/session-log.md` (last few entries, for recent context)
5. `context/constraints.md` (for rules to follow)
6. Any relevant plan files for the work you're about to do

This takes a few minutes but saves hours of rework from wrong assumptions.

### The Update-After-Implement Rule

**After every implementation task, update:**
1. `context/tasks-current.md` — mark items done, add new items discovered
2. `context/session-log.md` — add entry for this session's work
3. `context/bugs.md` — add any bugs found, update any bugs fixed
4. Any plan files affected by what you learned during implementation
5. `context/constraints.md` — if new constraints were established

A code change without a context update is incomplete work. The next session won't know
what happened, and the work may be repeated or contradicted.

---

## 3. Context File Size Management

Context files must stay manageable. A file that grows too large becomes hard to scan, slow
to read, and prone to information getting buried. The goal is that any file can be read and
understood in a single pass.

### Size Guidelines

- **Core files** (tasks-current, bugs, constraints): aim for under 200 lines each
- **Session log**: under 500 lines; split when it exceeds this
- **Infrastructure**: under 300 lines; split by subsystem if needed
- **Plan files**: under 500 lines each; split large plans into phase-specific docs

These aren't hard limits — a file at 210 lines is fine. The point is to notice when a file
is becoming unwieldy and proactively restructure before it's a problem.

### When to Split

Split a file when:
- It exceeds its size guideline AND you're about to add more content
- You find yourself scrolling past large sections to find what you need
- Different readers need different sections (e.g., infrastructure has networking, audio,
  rendering — a networking task doesn't need to read the audio section)

### How to Split

**Step 1**: Create subdirectory or subfiles with descriptive names.
```
context/
├── session-log.md          — Index: points to archive files + contains recent entries
├── session-log-s1-s25.md   — Archived sessions 1-25
├── session-log-s26-s50.md  — Archived sessions 26-50
```

Or for infrastructure:
```
context/
├── infrastructure.md       — Index: lists subsystems + which file has details
├── infra-networking.md     — Networking stack details
├── infra-audio.md          — Audio system details
├── infra-rendering.md      — Rendering pipeline details
```

**Step 2**: The original file becomes an index. It contains:
- A brief summary of what's in each subfile
- Guidance on which subfile to read for what purpose
- Any cross-cutting information that doesn't fit in a single subfile

**Step 3**: Update `context/README.md` to reflect the new structure.

### The Index Pattern

When a file is split, the original filename becomes the index:

```markdown
# Session Log — Index

## Recent Sessions (read these first)
See entries below.

## Archives
- [Sessions 1-25](session-log-s1-s25.md) — Project setup through first multiplayer test
- [Sessions 26-50](session-log-s26-s50.md) — Server architecture, room system, build pipeline

## Session 51 (2026-03-27)
[recent entry here]
```

The index tells future sessions which subfile to read based on what they need. If a task
is working on networking, it reads the networking infrastructure subfile — not the entire
infrastructure document.

### Cross-Referencing

When splitting creates multiple files that reference each other:
- Use relative links: `[see networking details](infra-networking.md)`
- Each subfile should state at the top which index it belongs to
- Don't duplicate content across subfiles — one source of truth, others reference it

---

## 4. The Build Tool Architecture

Every project that produces compiled or bundled output needs a build system. The critical
principle:

**One build tool. Two interfaces. Same behavior.**

```
┌──────────────┐     ┌──────────────┐
│  GUI Build   │     │  Headless    │
│  (for human) │     │  Build       │
│              │     │  (for AI)    │
└──────┬───────┘     └──────┬───────┘
       │                     │
       ▼                     ▼
┌────────────────────────────────┐
│      Canonical Build Tool      │
│                                │
│  - Clean build dirs            │
│  - Configure                   │
│  - Compile (all targets)       │
│  - Post-build steps            │
│  - Version injection           │
└────────────────────────────────┘
```

**Why this matters:** If the GUI and headless builds use different code paths, you get
divergence. "It builds for me but not for you" wastes hours of debugging that shouldn't
exist. Both interfaces must call the exact same underlying function with the same parameters.

### Implementation Pattern

Create a shared build function (script, module, or function) that accepts parameters:
- Target(s) to build
- Version to inject
- Build type (debug, release)
- Clean flag (whether to wipe build directories first)

The GUI build button calls this function with values from its UI widgets.
The headless build script calls this function with values from command-line arguments.
Both produce identical output given identical inputs. If one fails, the other will too —
and that's the point.

### Build Verification

**Every code change must be build-verified before reporting completion.**

After all code changes are finalized:
1. Run the headless build (all targets)
2. If errors occur, fix them
3. Only report "done" or "ready to build" after a clean compile

The AI should never tell the human "ready to build" without having verified the build
itself. The human's time is more valuable than a build check.

---

## 5. Session Continuity

### Starting a New Session

When beginning work on a project:
1. Read the project skill and context files (see §2, Read-Before-Write Rule)
2. Check `tasks-current.md` for what's next
3. Read the last 2-3 entries in `session-log.md` for recent context
4. If the user says "pick up where we left off" — the context files tell you where that is

### Ending a Session

Before a session ends (or when context is getting long):
1. Update all context files with what was accomplished
2. Ensure `tasks-current.md` reflects the current state
3. Add a session-log entry with explicit "What's Next" section
4. Commit if the user wants it

### Recovering from Context Wipes

If context is lost mid-session (compaction, crash, etc.):
1. Read all context files — they are the authoritative record
2. Check `git log` for recent commits to see what was done
3. Cross-reference context files with actual code state
4. Update any context files that are stale
5. Resume from where the context files say you left off

The context system exists precisely for this scenario. If it's maintained well, a context
wipe costs minutes, not hours.

---

## 6. Worktree Awareness

Some AI coding tools create git worktrees, placing changes in an isolated copy instead
of the main working directory. This means changes may not be where the human builds from.

**After completing code changes:**
1. Verify changes are in the main working copy (not a `.worktrees/` path)
2. If changes are in a worktree, copy them to the main working copy
3. Worktree writes can truncate files mid-line — compare line counts (`wc -l`),
   check file endings (`tail -5`), restore from worktree or git history if corrupted
4. Git index corruption (stale `.lock` files) is common — delete the lock, then `git reset`

**Never report work as complete while changes are stranded in a worktree.**

---

## 7. File Integrity

Large files edited through AI tools can sometimes be corrupted:
- Truncated mid-line (most common)
- Missing closing braces or function endings
- Duplicate content from failed edits

**After any significant edit:**
- Check the file compiles (or at least parse-checks for non-compiled languages)
- For C/C++: verify the file ends with a proper closing brace
- For any file: `tail -5` to confirm the ending looks right
- If a file is truncated, restore from git history (`git show HEAD:path/to/file`)

---

## 8. Setting Up Context for a New Project

If a project doesn't have a `context/` directory yet:

1. Create the directory structure from §2
2. Write `README.md` with the project overview and file index
3. Write `tasks-current.md` with initial tasks (what needs to be done)
4. Write `constraints.md` with any known architectural rules
5. Write `CRITICAL-PROCEDURES.md` with the operational rules from this skill
6. Create empty `session-log.md`, `bugs.md`, `infrastructure.md`
7. Start a session-log entry for the current session

The context system pays for itself after the first context wipe. Set it up early.

---

## 9. Anti-Patterns

Things that erode context quality over time:

- **Stale task lists**: Tasks marked "in progress" for sessions after they were completed.
  Update status immediately when work is done.
- **Missing session entries**: A session that does work but doesn't log it. The next session
  won't know what happened.
- **Duplicate context**: The same information in multiple files. One file should be the
  source of truth; others should cross-reference it.
- **Overly detailed session logs**: Log what was decided and what changed, not every
  intermediate step. Session logs should be scannable.
- **Constraints without reasons**: Every constraint needs a "why." Without it, future
  sessions can't judge edge cases or know if the constraint is still relevant.
- **Plans that don't update**: A plan written before implementation that's never updated
  with what was learned. Plans are living documents.
- **Context files that contradict the code**: If the code says one thing and the context
  file says another, the code is the source of truth. Update the context file.
- **Unbounded growth**: A file that grows without limit. When a file approaches its size
  guideline (see §3), split it proactively — don't wait until it's 1000 lines and
  impossible to navigate.
