---
name: context-recovery
description: >
  Full-depth context recovery for multi-session projects. Rebuilds complete
  situational awareness after a lost session, archived conversation, or cold
  start. Trigger on: "I lost my session," "pick up where we left off," "what
  were we working on," "rebuild context," "where did we leave off," "catch me
  up," or starting a new conversation on an existing project with context files.
  Audits the entire context system — not just mandatory reads — maps the project
  file structure, cross-references for discrepancies, and produces a structured
  briefing for decision-making. Works standalone or as a Dispatch coordinator's
  first step before routing tasks to code sessions.
---

# Context Recovery — Full Project State Rebuild

You are rebuilding situational awareness for a developer who has lost their
session context. They may have archived a conversation, hit a compaction boundary,
or simply started a new chat on an existing project. The goal is to get them from
zero context to full decision-making capability in a single pass.

This is not a normal session start. A normal start reads 3–4 mandatory files and
summarizes. Context recovery reads *everything relevant* and synthesizes a
comprehensive briefing. The difference matters — after a normal start, you might
miss that there's a QC backlog of 80 items or that a critical bug was structurally
fixed but never tested. Recovery leaves no stone unturned.

**Dispatch compatibility:** This skill produces output structured for both human
decision-making and coordinator handoff. If you're operating as a coordinator in
a dispatch workflow, the briefing gives you everything you need to write task
prompts for code sessions — current state, constraints, open work fronts,
blockers, and build status.

---

## 1. Why This Exists

Multi-session projects accumulate state across many files: task trackers, session
logs, constraint ledgers, bug lists, roadmaps, QC checklists, domain-specific
documentation, architecture decision records, release notes. When a session is
lost, the developer has to manually re-read all of this to figure out where they
are — or worse, they skip it and start working from stale assumptions.

This skill automates the full recovery. It reads everything, cross-references it,
identifies discrepancies (like infrastructure trackers that are out of date with
session logs), and produces a briefing structured for decision-making.

---

## 2. Recovery Protocol

### Phase 1: Discover the Project Structure

Before reading anything in depth, map out what exists. Different projects organize
their context differently, so the first step is reconnaissance. The file tree is
part of the deliverable — the developer needs to see the shape of their project
at a glance.

**Step 1 — Map the project root.** List the top-level directory contents. Look for:

- `CLAUDE.md` or equivalent top-level rules/config file
- A `context/` directory (or `_context/`, `docs/`, `.claude/`, etc.)
- Any `.md` files at root level (README, ROADMAP, CHANGES, RELEASE_*, PROMPT, etc.)
- Build scripts, tool directories, configuration files
- Source directories and their organization (`src/`, `port/`, `lib/`, etc.)

**Step 2 — Find the index file.** Most structured projects have one:

- `context/README.md` or `context/INDEX.md`
- A master hub that links to everything else
- If no explicit index exists, the top-level `CLAUDE.md` often serves this role

**Step 3 — Enumerate all context files.** Use glob patterns to find every `.md`
file in the context directory and its subdirectories. Also check for `.json` task
files, `.yaml` configs, or any other structured metadata. Build a complete
inventory — this becomes part of your briefing.

**Step 4 — Map the development environment.** Identify:

- Source tree layout (where game code, port code, build output, tools live)
- Build system (scripts, output directories, artifact locations)
- Dev tools (dashboard GUIs, test harnesses, release scripts)
- Data directories (assets, mods, ROM data, post-build addins)
- Git repository state (branch, worktrees, remotes)

Do all of this exploration in parallel where possible — directory listings, glob
searches, and git commands are independent operations.

**Step 5 — Build the file map.** Produce a structured inventory of the project's
key directories and files. This serves as both a reference for the briefing and
a "Dev Window" — a quick-reference view of where everything lives:

```
project-root/
├── CLAUDE.md              — Standing orders, architecture, critical rules
├── context/               — Project context encyclopedia
│   ├── README.md          — Master index (read first)
│   ├── constraints.md     — Active/removed constraint ledger
│   ├── session-log.md     — Recent session history
│   ├── tasks-current.md   — Active punch list
│   ├── roadmap.md         — Phase plan + dependency graph
│   ├── bugs.md            — Open/fixed bug tracker
│   ├── qc-tests.md        — In-game verification checklist
│   ├── infrastructure.md  — Phase execution status
│   ├── [domain].md        — System-specific docs (load on demand)
│   └── _archive/          — Completed/deprecated items
├── src/                   — [Source code layout]
├── build/                 — [Build output]
├── devtools/              — [Development tools]
└── [other key dirs]
```

Adapt this tree to match whatever the project actually has. The point is giving
the developer a spatial map of their project so they can orient instantly. Include
one-line descriptions for each significant file or directory. Note which files
are stale, recently modified, or relevant to the current work front.

### Phase 2: Read in Priority Order

Read files in this order. The earlier files inform how you interpret the later ones.

**Tier 1 — Orientation (read first, always):**

| Priority | What to find | Why |
|----------|-------------|-----|
| 1 | Index / README (the master hub) | Tells you what files exist and what they're for |
| 2 | CLAUDE.md or equivalent rules file | Standing orders, architectural constraints, project identity |
| 3 | Constraint ledger | What's still active vs. what's been removed — prevents wasted work |
| 4 | Session log (most recent 3–5 entries) | What was last done, decisions made, stated next steps |
| 5 | Active task tracker | Current punch list, what's in progress, what's blocked |

**Tier 2 — Strategic picture (read next):**

| Priority | What to find | Why |
|----------|-------------|-----|
| 6 | Roadmap / milestone plan | Long-term direction, phase dependencies, release targets |
| 7 | Infrastructure / phase tracker | Execution status of all work streams |
| 8 | Bug tracker | Open issues, severity, root causes, fix status |
| 9 | QC test checklist | What needs in-game/in-app verification |
| 10 | Release notes (most recent) | What shipped, what's working, known issues |

**Tier 3 — Domain context (read selectively):**

Domain files (collision.md, networking.md, server-architecture.md, etc.) should
be scanned at headline level but only read in full if they're directly relevant
to the most recent work or the likely next task.

Architecture Decision Records (ADRs) — note their existence and one-line
summaries but don't read in full unless the user asks.

**Tier 4 — State verification:**

| What | How |
|------|-----|
| Git branch + recent commits | `git log --oneline -15`, `git branch`, `git status` |
| Uncommitted work | `git status --short` |
| Worktree branches | `git worktree list` (if applicable) |
| Build artifacts | Check build output dirs for timestamps, executables |
| Build state | Check for build scripts, recent build logs, last known build result |
| Dev tools state | Dashboard configs, release scripts, test harness state |

Read Tier 1 and Tier 2 files in parallel where there are no dependencies between
them. For example, the constraint ledger, session log, and task tracker can all
be read simultaneously after you've read the index. Git commands can run in
parallel with all file reads.

### Phase 3: Cross-Reference and Identify Discrepancies

This is what separates recovery from a normal session start. After reading
everything, actively look for inconsistencies:

- **Infrastructure tracker vs. session log**: Did the session log record
  completing work that the infrastructure tracker still shows as "next" or
  "in progress"? Flag these as needing updates.

- **Task tracker vs. session log**: Are there tasks marked "done" in the session
  log that are still listed as active in the task tracker? Or vice versa?

- **Constraint ledger vs. actual code state**: Did a session remove a constraint
  (noted in session log) but the constraint ledger wasn't updated?

- **QC backlog depth**: How many untested items have accumulated? Is the backlog
  growing faster than testing?

- **Bug status vs. fix status**: Are bugs marked "fixed" actually verified, or
  just coded?

- **Git state vs. session log**: Do the most recent commits match what the
  session log claims was done? Are there commits not reflected in the context
  files?

- **Stale file references**: Do any context files reference files, functions,
  or systems that have been renamed, moved, or removed?

Don't silently fix these discrepancies — report them in your briefing so the
developer can decide whether to update the files or whether the discrepancy
reveals a real issue.

### Phase 4: Synthesize the Briefing

Structure the output to support decision-making. The developer needs to quickly
understand where they are and choose what to do next.

**Briefing structure:**

1. **Project Identity** (2–3 sentences)
   What is this project? What's the tech stack? Who's involved?

2. **Project Structure** (the Dev Window)
   The file map from Phase 1. Key directories, context files, source tree,
   build system, tools. One-line descriptions. This gives the developer (and
   any future coordinator session) a spatial reference for the entire project.
   For a coordinator in a dispatch workflow, this section answers "where do I
   point code sessions to find things?"

3. **Where You Left Off** (the most recent session cluster)
   What was worked on, what was completed, what was left unfinished.
   Include build status — did the last session end with a clean build,
   an unverified build, or a broken build? If unverified, say so prominently.

4. **What's Done** (major milestones and completed tracks)
   High-level summary of completed work streams. Don't list every session —
   group by feature/phase. Include version/release if applicable.

5. **What's Next** (priority-ordered task list)
   Pull from the task tracker but cross-reference with session log "next steps."
   Include dependencies and blockers. For each item, note whether it's ready to
   start or waiting on something (like a build test).

6. **Key Constraints** (active constraints that affect upcoming work)
   Don't dump the entire ledger — highlight the ones that are relevant to
   the likely next tasks. Note recently removed constraints that might still
   trip someone up.

7. **Open Issues** (bugs, untested items, known problems)
   Severity-sorted. Note which are coded-but-untested vs. fully open vs.
   structurally-fixed-but-unverified.

8. **QC Backlog** (if applicable)
   How many untested items? Grouped by feature/session. Are they accumulating?
   This is often the thing that gets lost when sessions are archived.

9. **Discrepancies Found** (if any)
   Context files that are out of sync with each other. Stale status entries.
   Infrastructure trackers that don't reflect recent session completions.
   Recommend specific updates.

10. **Release / Milestone Status** (if applicable)
    Where does the project stand relative to its planned milestones?
    What's blocking the next release? What percentage of the milestone's
    features are complete vs. coded-but-untested vs. not started?

End with a clear prompt: "Where would you like to pick up?" or present the
active work fronts as options. The developer is the director — give them the
landscape and let them choose.

---

## 3. Practical Guidance

**Parallelism matters.** Context recovery involves reading a lot of files. Use
parallel reads wherever possible. Tier 1 files after the index can be read
simultaneously. Tier 2 files are all independent of each other. Git commands
can run in parallel with file reads. The goal is to minimize wall-clock time —
reading 15 files sequentially when 10 of them could be parallel wastes the
developer's time.

**Session logs can be huge.** If the session log exceeds readable limits, read
the most recent entries first (they're usually at the top or clearly dated).
Archive files exist for older sessions — note their existence but don't read
them unless the user asks.

**The index file is your map.** A well-maintained project has an index that tells
you what every file is for and when to read it. Trust the index. If there's no
index, the file listing from Phase 1 is your fallback — use filenames and
directory structure to infer purpose.

**Don't summarize what you haven't read.** If a file is too large to read in one
pass, say so and offer to read specific sections. Don't guess at contents based
on filenames alone.

**Build state is critical context.** A project where the last session ended with
"build unverified" is in a fundamentally different state than one with a clean
build. Always check this and surface it prominently. If there are build scripts,
note their locations.

**Git state tells the ground truth.** Context files can be out of date, but git
log shows exactly what was committed and when. Use it as a cross-check against
what the session log claims.

**File timestamps are informative.** When listing the project structure, note
which files were recently modified. A context file last touched weeks ago is
likely stale. A build artifact from today means someone compiled recently.

**Be thorough, then be concise.** Read everything, but don't dump everything.
The briefing should be comprehensive but scannable — the developer should be
able to read it in 3–5 minutes and have enough context to make a decision about
what to work on next. Use structure (headers, tables, lists) to make it
skimmable, but don't pad. Every sentence should carry information.

---

## 4. Dispatch Workflow Integration

This skill integrates with coordinator/code-session dispatch workflows. When a
coordinator session triggers context recovery, the briefing serves as the
coordinator's working memory for the session.

**As a coordinator's first move:**

1. Trigger context-recovery to get full project awareness
2. Use the briefing's "What's Next" section to identify the highest-priority task
3. Use the "Project Structure" section to identify which files a code session
   needs to read
4. Use the "Key Constraints" section to populate the "What NOT to do" section
   of the task handoff template
5. Use the "Discrepancies Found" section to decide if context files need
   updating before routing work

**The briefing's structure maps to the handoff template:**

| Briefing section | Handoff template field |
|-----------------|----------------------|
| Where You Left Off | "What was done (previous session)" |
| Project Structure | "Domain files to read" |
| Key Constraints | "Constraints active" + "What NOT to do" |
| What's Next | "What to do" |
| Open Issues | "What to do" (if fixing a bug) or "What NOT to do" (if avoiding one) |

---

## 5. Adaptation

This skill is designed for projects with structured context file systems — the
kind that maintain session logs, constraint ledgers, task trackers, and domain
files. But projects vary widely in how they organize this:

- Some use `context/` with a `README.md` index
- Some use `_context/` with an `INDEX.md`
- Some keep everything in root-level `.md` files
- Some use a single `CLAUDE.md` with everything inline
- Some have elaborate multi-tier systems with archives and ADRs

The discovery phase (Phase 1) handles this variation. Don't assume a specific
structure — explore first, then adapt the reading order to whatever exists.

If the project has *no* context files at all, say so clearly and offer to help
establish a context system. But that's a different workflow — this skill is for
recovery, not creation.

If the project uses a companion skill (like `game-port-director`) that has its
own session-start protocol, context-recovery supersedes the normal start protocol
for the initial recovery pass. Once the developer has full awareness and chooses
a task, the companion skill's normal task-flow protocol takes over.
