# Dev Window v3

A clean, quick, efficient rebuild of the Perfect Dark 2 Dev Window. Launch it
with `Dev Window v3.bat` (or `powershell -File dev-window-v3.ps1`).

## What it does

- **Header**: current branch, version (parsed from `CMakeLists.txt`), and a
  dirty-file count. `Refresh` re-reads them. Git status runs on a background
  runspace so the window never stalls waiting on git.
- **BUILD (all)**: reaps stale `.claude/` transients (see below), git-syncs
  (add -A, commit only if staged, best-effort push), then shells out to
  `devtools/build-headless.ps1 -Target all`. Output streams live into the log;
  `Stop` kills the child process tree.
- **Run Game**: launches `Build/PerfectDark.exe` (working dir `Build/`),
  disabled until it exists.
- **Run Tests**: runs `devtools/run-pd-tests.ps1 -Session devwin`.
- **Workbench**: starts the repo-local Workbench server if needed and opens
  its Board/Graph/Timeline/Decisions/Assets/Validation/Performance/Notes/
  Activity/Hub views.
- **Release...**: confirm dialog, git-sync (push required), then
  `devtools/release.ps1 -Version X.Y.Z`.
- **Build Queue panel**: shows the active build and any queued requests from
  `.claude/session-builds/.queue/`, refreshed every 2s. `Clean stale builds`
  runs `build-session.ps1 -RemoveAll`, but only after confirming no build is
  active.
- **Log**: monospace, autoscroll toggle, `Copy All` / `Copy Errors`
  (errors = lines matching error/failed/fatal), `Clear`.
- Window size/position persist to `settings.json`.

## Transient-workspace reaping

Before every `git add -A`, BUILD/Release run
`devtools/clean-claude-workspace.ps1 -Quiet`, which removes stale regenerable
`.claude/` debris -- multi-GB smoke installs / extraction caches, per-session
build dirs, and ad-hoc `*.log` files (render/extract logs can top 200 MB). This
keeps two problems from recurring:

1. **Push rejection.** A 219 MB render log once got swept into a commit and
   blew past GitHub's 100 MB file limit, bouncing the whole push.
2. **Disk bloat.** The tree had grown ~2 GB of week-old logs.

The reaper works off an **explicit allowlist** and an mtime threshold (default
24h), so it only ever touches known-regenerable paths and leaves anything
touched inside the window alone. It never touches config (`settings.json`,
`skills/`, `launch.json`) or `.claude/worktrees/` (in-flight work). Run it by
hand any time: `-DryRun` to preview, `-MaxAgeHours 0` to reap everything
transient now.

## How it differs from v2

- **One build tool, honored structurally.** v2 kept its own `Get-BuildSteps`
  that duplicated the CMake configure/compile flags, so it could silently
  drift from `build-headless.ps1`. v3 defines **no** build steps at all -- every
  build path shells out to `build-headless.ps1` / `release.ps1` / `run-pd-tests.ps1`.
  There is nothing to keep in sync.
- **Lean.** ~630 lines vs ~5,300. One log pane, one queue panel, six actions.
  No worktree pruner, no docs tab, no embedded legacy tracker, no ninja-progress
  re-implementation (the headless build already prints progress; v3 just
  streams it).
- **Same proven internals.** WPF software rendering (S482 workaround),
  the single consolidated `Add-Type` compile for fast cold start, the
  `AsyncLineReader` streaming pattern, and a background runspace pool for all
  git/build/test work so the UI thread never blocks.

## Constraints preserved

- No CMake flags defined here (one build tool, two interfaces).
- Git sync before Build and Release.
- WPF `ProcessRenderMode = SoftwareOnly`.
- ASCII only, no em-dashes (PowerShell Windows-1252 truncation hazard).
- Clean-build semantics come from `build-headless.ps1`, unchanged.

## Status

v2 remains only for historical compatibility. v3 is the supported Dev Window,
and Workbench replaces v2's Kanban controls.
