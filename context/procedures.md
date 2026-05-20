# Procedures

> Operational rules for any AI or Mike-driven session. Build verification, git safety, worktree handling, isolated build sessions, commit discipline.

These rules apply to all sessions working on this project, regardless of interface (Claude Code, Cursor, Codex, manual). They override default behavior.

---

## Build verification

Every code change must be build-verified before being reported as complete.

- After all changes are finalized, run a build covering the affected target(s).
- If build errors occur, fix them before reporting.
- Do not report "ready to build" or "done" until a clean compile is confirmed.
- **Use `.\devtools\build-session.ps1 -Session <short-id> -Target all`** for AI verification when parallel sessions may build. The wrapper routes each session to `.claude/session-builds/<short-session-id>/` so concurrent sessions do not corrupt shared `Build/`.
- The wrapper queues by default; sessions get queue position, ETA, and watchdog timeout (60s default per S582).
- Do not pass `-NoQueue` unless Mike explicitly asks for it.
- Reuse the same session id only inside the same active session. Parallel sessions must use different ids; the wrapper holds a lock and fails fast on accidental reuse.
- Cleanup: `.\devtools\build-session.ps1 -Remove -Session <short-session-id>` when done. `-List` to inspect old directories. `-RemoveAll` only when no session build is running.

For test-only verification, use `.\devtools\run-pd-tests.ps1 -Session <id> [-Scope <alias>|-Selector <selector>]`. See [pillars/tests.md](pillars/tests.md) for the alias list.

---

## Build environment invariants

Per the project's `CLAUDE.md`:

- `TEMP=C:\Users\mikeh\AppData\Local\Temp`
- `/c/msys64/mingw64/bin` prepended to `PATH`
- `CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime`
- `CCACHE_BASEDIR` set

Both `devtools/_build-env-prelude.ps1` and `devtools/build-env.sh` set these idempotently and are dot-sourced everywhere. **Do not rediscover env. Do not invent alternatives.**

From bash:

```bash
source devtools/build-env.sh && ninja -C Build pd pd-server
```

From PowerShell:

```powershell
.\devtools\build-headless.ps1   # self-configures env
```

---

## Build tool architecture

**One build tool, two interfaces.** The Dev Window v2 (`devtools/dev-window-v2.ps1`) and the headless script (`devtools/build-headless.ps1`) must produce identical builds from identical source. They share cmake flags, compiler, and post-build steps.

**Canonical cmake configure args:**

```
cmake -G "Unix Makefiles"
      -DCMAKE_MAKE_PROGRAM="C:\msys64\usr\bin\make.exe"
      -DCMAKE_C_COMPILER="C:/msys64/mingw64/bin/cc.exe"
      -B "<buildDir>"
      -S "<projectRoot>"
      -DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z
```

Optional stable channel (same scripts / `CMakeLists.txt`): append `-DPD_STABLE_RELEASE=ON` to omit `PD_DEV_BUILD` (dev hotkeys, Settings Debug tab). Default is dev/local with `PD_DEV_BUILD` enabled.

Rules:

- `build-headless.ps1` mirrors `Get-BuildSteps` from `dev-window.ps1` exactly.
- If you change cmake flags in one, change the other.
- Headless accepts `-Version "X.Y.Z"` or reads `CMakeLists.txt`.
- **Never run cmake directly from a worktree.** `build-headless.ps1` has a worktree guard that redirects to the main working copy.
- `$ProjectDir` always resolves to the real git working tree, never a `.claude/worktrees/` path.
- Always invoke: `powershell -File devtools/build-headless.ps1 -Target all` from the project root.

---

## Worktree awareness

Claude Code may create git worktrees, placing changes in an isolated copy instead of the main working directory.

- After completing code changes, verify they are in the main working copy.
- If changes landed in a `.claude/worktrees/` path, they are NOT in the build directory.
- Copy changed files from the worktree to the main working copy before declaring done.
- Worktree writes can truncate files mid-line. After any merge: compare line counts (`wc -l`), check file endings (`tail -5`), restore from worktree or git history if truncated.
- Git index corruption (`.git/index.lock`) is common; fix with: delete the lock file, then `git reset`.

---

## Solo-dev git before automated build / release

Dev Window v2 runs `git pull --rebase` inside `devtools/release.ps1` before pushing. A non-clean index (staged but uncommitted changes) makes that step fail with "cannot pull with rebase: Your index contains uncommitted changes".

- Dev Window v2 calls `Invoke-GitSyncBeforeBuild` at the start of Build and Release: `git add -A`, commit only if the index has staged changes (`git diff --cached --quiet`), then `git push` to the current branch.
- `release.ps1` uses the same `diff --cached` check before `git commit` (no longer relies on `git status --porcelain` alone, and does not swallow commit failures).

**Rule**: let the Dev Window perform the sync step; avoid leaving staged edits mid-pipeline.

---

## Git safety (worktree operations)

These rules prevent the class of bugs where git operations silently discard or truncate in-flight work.

**Commit-first discipline.** As soon as a session has a meaningful unit of work, commit it (WIP message fine) before running ANY git operation that touches the working tree (stash, rebase, reset, checkout, merge). Uncommitted work is unprotected work.

**No bare `git stash`.** Never run `git stash` or `git stash push` without explicit paths. If separating work, use `git stash push -- <path> <path>` with explicit targets only. A bare stash grabs everything in the working tree (including build noise and unrelated changes); the pop can silently corrupt or conflict.

**Pre-op snapshot for destructive operations.** Before any `git rebase`, `git reset`, or `git merge`:

1. Record current HEAD SHA: `git rev-parse HEAD`
2. Record line counts of all changed files: `git diff --stat`
3. After the operation, verify against the snapshot. Any file that shrank unexpectedly = halt and report to the user before continuing.

The optional helper `devtools/git-snapshot.sh` automates this.

**No `git reset --hard` without human instruction.** Never use `git reset --hard` in a session unless Mike explicitly instructs it. Use `git reset --soft` or `git reset --mixed` if you need to unstage.

**Post-merge verification.** After any merge, re-verify line counts of all changed files vs their worktree-pre-merge state. If any file shrank or disappeared that should not have, halt and report before continuing. This applies to worktree merges, branch merges, and rebase completions.

---

## Auto-merge (worktree work to dev)

Per Mike's working-preferences (2026-04-26):

- **Auto-merge worktree work to dev sequentially.** Do not ask first; just do.
- **Safely**: dry-run + line-count verify + build-verify-where-applicable. One merge at a time.
- Code sessions handle their own merge as part of completion.
- Dev working tree must be clean before the merge. If Mike has uncommitted in-flight work on dev, surface and let Mike commit before the merge.

For docs-only commits, build verify is a no-op; the line-count check still applies (post-merge `wc -l` vs pre-merge).

---

## Truncation guard discipline

Per [systemic-bugs.md](systemic-bugs.md) SP-9, two truncation modes affect this codebase:

- **Mode A (encoding)**: PowerShell scripts written through Windows-1252 silently terminate at the first non-ASCII byte. **No em-dashes anywhere in `.ps1` files**. (See also: no em-dashes anywhere in committed files in this project; PowerShell hygiene applies to docs as well.)
- **Mode B (AI output token limit)**: large Write or Edit calls can truncate mid-character when session context is saturated. The tool writes truncated content without error.

**Discipline for AI sessions:**

1. **Author in chunks** if a doc is going to exceed 500 lines. Sequence of `Write` calls or sequential `Edit` appends, each adding a section.
2. **Verify after every write.** Read the file back end-to-end; confirm the closing `---` / final newline / final paragraph is intact. Match against intended outline. If the last line ends mid-thought, re-write or append.
3. **Line-count gate.** Capture intended line count vs actual line count post-write; large drift = truncation event, redo.
4. **Sentinel marker at end of every authored doc** (e.g. a final "Where to look" section or known-stable footer). After each write, grep for the sentinel; absent = truncated.
5. **No single-call writes > 500 lines.** Hard rule. Split.

**Pre-commit `git diff HEAD --numstat` guard** in `devtools/build-headless.ps1` (S190) fires when net delta < -20 lines AND additions < 1/3 of deletions. Aborts auto-commit.

---

## Architecture principles

- **No hacks.** If something should not happen, gate it structurally; do not work around symptoms.
- **Event-driven over polling.** Do not check every frame for something that only changes on specific events.
- **Understand root causes.** Fix the why, not just the what.
- **Thorough research before implementation.** Reading the codebase is not wasted time; it prevents wrong assumptions and rework.

---

## Constraint check (before every significant change)

Before implementing anything complex, check [constraints.md](constraints.md):

- **Active Constraints**: things you must still respect (save format, protocol version, array limits, identity invariants).
- **Removed Constraints**: things abandoned. If the task's complexity comes from a removed constraint, **stop and propose the simpler approach**.
- **Index Domain Warning**: three index spaces that must not be confused (stage table, solo stage, stagenum).

---

## Pre-task sanity check

Before starting significant work, mentally run through:

1. **Constraint check**: does this assume a constraint that's been removed?
2. **Root cause check**: am I fixing a symptom or the underlying problem?
3. **Scope check**: is this the simplest approach for modern hardware?
4. **Cascade check**: will this conflict with things already modernized?
5. **Effort check**: is this proportional to its importance?

---

## Rabbit hole protocol

If mid-task you realize you are going deeper than expected: **stop. Do not push through.** Explain what is happening, present options (refactor vs. partial modernize vs. patch), recommend one, let Mike decide.

---

## Bug discipline

- **One-off bugs** -> [bugs.md](bugs.md) (ID, severity, root cause, fix, session, verify command).
- **Systemic patterns** (classes of bugs) -> [systemic-bugs.md](systemic-bugs.md) (with search commands and audit checklists).
- After fixing a bug, **always do a propagation check**: does this same problem exist anywhere else? Fix the class, not the instance.

---

## Session start checklist

Before writing any code or making any changes:

1. Read [README.md](README.md) (orientation).
2. Read [working-preferences.md](working-preferences.md) (collaboration rules).
3. Read [constraints.md](constraints.md) (active and removed invariants).
4. Read this file (procedures.md).
5. Read [tasks.md](tasks.md) (current punch list) and the relevant pillar(s) for whatever you are touching.
6. Check Kanban Decision Requests before selecting work: `python tools/kanban_evaluator.py list-decision-requests`. Read Mike's answered responses first unless the newest user message specifically directs the session to do something else.
7. Summarize to Mike: where we are, what's next, any blockers.
8. Confirm direction before starting.

---

## Session end / state save

When the user wraps up or a major task completes:

1. Update [session-log.md](session-log.md) with: focus, what was done, decisions, next steps.
2. Update [tasks.md](tasks.md) with current status and any new blockers.
3. Update any pillar doc whose live state shifted.
4. Brief summary to the user of what was recorded.

---

## Task conduct

- When a fix is clear, implement it; do not announce intent and wait for approval.
- Update context files as part of the implementation, not as a separate follow-up.
- Commit when asked or after significant milestones. Comprehensive commit messages.
- Do not push to remote unless explicitly asked (note: Dev Window v2 push and pre-build sync are operator-initiated and exempt from this rule).

---

## Where to look

- For collaboration mode + tone + decision authority: [working-preferences.md](working-preferences.md).
- For active and removed invariants: [constraints.md](constraints.md).
- For pillar live state: [pillars/](pillars/).
- For build wrapper internals: [pillars/build-dev-tooling.md](pillars/build-dev-tooling.md).
- For the SP-9 truncation deep-dive: [systemic-bugs.md](systemic-bugs.md).
