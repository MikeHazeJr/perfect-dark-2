# Critical Procedures

> These rules apply to ALL sessions working on this project, regardless of interface.
> They are non-negotiable and override default behavior.

---

## Context Management

Context files in `context/` are the project's memory across sessions. Without them, every context wipe or new session starts from zero.

- **Before writing any code**: read the project skill and all files in `context/`
- **After every implementation task**: update `tasks-current.md`, `session-log.md`, and any other relevant context files
- **Context updates are higher priority than code changes.** A code fix without a context update is incomplete work.
- **Every session gets a session-log entry** documenting what was done, what was decided, and what's next

---

## Build Verification

**Every code change must be build-verified before being reported as complete.**

- After all changes are finalized, run a build (all targets)
- If build errors occur, fix them before reporting
- Do not report "ready to build" or "done" until a clean compile is confirmed

---

## §3 Build Tool Architecture

**One build tool, two interfaces.** The Dev Window (`devtools/dev-window.ps1`) and the headless script (`devtools/build-headless.ps1`) must produce identical builds from identical source. They share the same cmake flags, compiler, and post-build steps.

**Canonical cmake configure args** (from `dev-window.ps1 Get-BuildSteps`):
```
cmake -G "Unix Makefiles"
      -DCMAKE_MAKE_PROGRAM="C:\msys64\usr\bin\make.exe"
      -DCMAKE_C_COMPILER="C:/msys64/mingw64/bin/cc.exe"
      -B "<buildDir>"
      -S "<projectRoot>"
      -DVERSION_SEM_MAJOR=X -DVERSION_SEM_MINOR=Y -DVERSION_SEM_PATCH=Z
```

**Rules:**
- `build-headless.ps1` mirrors `Get-BuildSteps` from `dev-window.ps1` exactly
- If you change cmake flags in one, change the other
- Headless accepts `-Version "X.Y.Z"` or reads CMakeLists.txt (same as GUI `Get-ProjectVersion`)
- **NEVER run cmake directly from a worktree** — `build-headless.ps1` has a worktree guard that redirects to the main working copy
- `$ProjectDir` always resolves to the real git working tree, never a `.claude/worktrees/` path
- Always invoke: `powershell -File devtools/build-headless.ps1 -Target all` from the project root

---

## Worktree Awareness

Claude Code may create git worktrees, placing changes in an isolated copy instead of the main working directory.

- After completing code changes, verify they are in the main working copy
- If changes landed in a `.claude/worktrees/` path, they are NOT in the build directory
- Copy changed files from the worktree to the main working copy before declaring done
- Worktree writes frequently truncate files mid-line. After any merge: compare line counts (`wc -l`), check file endings (`tail -5`), restore from worktree or git history if truncated
- Git index corruption (`.git/index.lock`) is common — fix with: delete the lock file, then `git reset`

---

## Architecture Principles

- **No hacks.** If something shouldn't happen, gate it structurally, don't work around symptoms.
- **Event-driven over polling.** Don't check every frame for something that only changes on specific events.
- **Understand root causes.** Fix the "why," not just the "what."
- **Thorough research before implementation.** Reading the codebase is not wasted time — it prevents wrong assumptions and rework.

---

## Task Conduct

- When a fix is clear, implement it — don't announce intent and wait for approval
- Update context files as part of the implementation, not as a separate follow-up
- Commit when asked or after significant milestones. Comprehensive commit messages.
- Do not push to remote unless explicitly asked
