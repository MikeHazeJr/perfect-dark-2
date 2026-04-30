# Open Bugs + Build Recipe Snapshot - 2026-04-23

Timestamp: 2026-04-23 (read-only pass, no files modified outside this scratch)
Scope: (1) canonical build-for-testing recipe with iterative error-fix workflow, (2) top 15 most recent unresolved bug entries from `context/bugs.md`.

---

## 1. Build-for-Testing Recipe (verbatim)

### Canonical source: `perfect_dark-mike/CLAUDE.md` (project root)

Under the heading **"Build environment - every session, before any build"** (lines 17-33):

> **Do not rediscover TEMP or PATH. Do not invent alternatives.**
>
> From bash:
> ```bash
> source devtools/build-env.sh && ninja -C Build pd pd-server
> ```
>
> From PowerShell:
> ```powershell
> .\devtools\build-headless.ps1   # self-configures env
> ```
>
> `build-env.sh` sets `TEMP`, `TMP`, and prepends `/c/msys64/mingw64/bin` to `PATH`.
> `build-headless.ps1` dot-sources `devtools/_build-env-prelude.ps1` (idempotent).

### Quoted from `context/QUICKSTART.md`, section 7 "Build and Test" (lines 117-131)

> **Mike builds.** AI does NOT compile. AI writes code, Mike verifies.
>
> ```powershell
> # Headless build (for AI build-check):
> powershell -File devtools/build-headless.ps1 -Target all
> ```
>
> ```bash
> # Manual build:
> export PATH="/c/msys64/mingw64/bin:$PATH"
> cmake -G Ninja -B Build -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
> ninja -C Build pd pd-server
> ```
>
> **Outputs**: `Build/PerfectDark.exe` (~43-46 MB), `Build/PerfectDarkServer.exe` (~21-23 MB).

### The "recursively fix" rule - `context/CRITICAL-PROCEDURES.md`, section "Build Verification" (lines 30-36)

> **Every code change must be build-verified before being reported as complete.**
>
> - After all changes are finalized, run a build (all targets)
> - If build errors occur, fix them before reporting
> - Do not report "ready to build" or "done" until a clean compile is confirmed

### Exact commands, where, runtime, meaning

- **Command (PowerShell, canonical)**: `powershell -File devtools/build-headless.ps1 -Target all`
  - Accepts `-Target client` / `-Target server` for faster per-target loops.
  - `-Clean` forces a full wipe; default is the script's smart-incremental path.
- **Command (bash, manual)**: `source devtools/build-env.sh && ninja -C Build pd pd-server`
- **Command (AI-focused alternate)**: `.\devtools\cursor-build.ps1` - thin wrapper around `build-headless.ps1` that pins `-OutputDir "Cursor Build"` so AI builds stay out of the main `Build/` directory. Same flags as `build-headless.ps1` plus `-CommitPush`/`-UseNextVersion` aliases.
- **Where to run**: project root `C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike` on Windows. PowerShell 5.1+ or an MSYS2 MinGW bash shell. Never from a `.claude\worktrees\...` path - `build-headless.ps1` has a worktree guard that redirects to the main working copy.
- **Env requirement**: MSYS2 at `C:\msys64`, MinGW64 toolchain on `PATH` (prelude handles this), `TEMP`/`TMP` set to a writable directory (prelude handles this). Tools validated in the script: `ninja`, `cc.exe`, `cmake`, `python3`, `ccache`.
- **Expected runtime**: full clean build lands around 45-90 s per target on Mike's box (session logs reference "Build clean 775/775" style summaries). Warm incremental `pd` with ccache is typically well under 15 s (S231 target: warm `pd` under 12 s). Per-target loops are the fastest way to stay in a tight fix-build-fix cycle.
- **What "recursively fix" means here**: the project does NOT ship a script that loops build-until-green automatically. The cycle is manual per CRITICAL-PROCEDURES.md: run `build-headless.ps1`, read the errors it surfaces (`Is-ErrorLine` filter writes compiler/linker errors to the console in red in non-verbose mode), fix each in source, re-run, repeat until `Result: SUCCESS` prints. The script's built-in support for this loop is ccache warm-paths, Ninja incremental rebuild, `-Target client` / `-Target server` for tighter loops, and a structured error summary with CMake error-log tails on configure failure.
- **Primary recipe file**: `CLAUDE.md` (project root). Supporting files: `context/QUICKSTART.md` section 7, `context/CRITICAL-PROCEDURES.md` section "Build Verification", `devtools/build-headless.ps1`, `devtools/cursor-build.ps1`.

### Sanity-check vs memory notes

The system-prompt memory notes reference `build-environment-recipe` and `build-verify-before-done`. Those align with what the project-current docs say: same entry point (`build-headless.ps1`), same env-prelude discipline, same "no report until clean" rule. No drift detected. Note: both CLAUDE.md and `context/build.md` say Mike is the human who builds and tests; AI runs the headless script as a build-check only, and S196's `AI builds via build-headless.ps1` line in CLAUDE.md is the authorization for AI-side build verification, not a license for AI to claim playtest sign-off.

---

## 2. Top 15 Most Recent Unresolved Bugs

Definition applied: status is not CLOSED / FIXED / RESOLVED. `FIXED-PENDING-PLAYTEST` counts as unresolved. Ordered by date descending (newest first).

| # | ID | Title | Status | Date |
|---|------|-------|--------|------|
| 1 | B-233 | Solo CI death stays stuck dead, no respawn after OOB fall | FIXED-PENDING-PLAYTEST | 2026-04-23 |
| 2 | B-232 | CRT scanline overlay screen darkens; added tunable vertical scale | FIXED v2 PENDING-PLAYTEST | 2026-04-23 |
| 3 | B-234 | MP character pick still resolves via legacy integer index; aliens regress to Joanna | FIXED v2 PENDING-PLAYTEST | 2026-04-23 |
| 4 | B-231 | Interact prompt flashes for a frame as Main Menu opens in CI | FIXED-PENDING-PLAYTEST | 2026-04-23 |
| 5 | B-230 | Controller B needs two presses to close Main Menu (kb Esc and mouse X work first press) | FIXED-PENDING-PLAYTEST | 2026-04-23 |
| 6 | B-229 | User-picked spawn weapon overridden by B-181 fallback on pickup-less arenas | FIXED-PENDING-PLAYTEST | 2026-04-23 |
| 7 | B-228 | SP maps in MP skip elevators / fire-escape stairs; no sync | OPEN / NEW | 2026-04-23 |
| 8 | B-227 | Dr Carroll and Skedar bodies missing from Character Select list | OPEN / NEW | 2026-04-23 |
| 9 | B-226 | Wrongly labeled characters in bot Character Select | OPEN / NEW | 2026-04-23 |
| 10 | B-225 | Stale base-catalog Bonus arenas (Kakariko Village, Dark Noon) listed but unplayable | OPEN / NEW | 2026-04-23 |
| 11 | B-224 | Interact prompt compounds dim with menu; leaks over CI boot fly-in | FIXED v2 PENDING-PLAYTEST | 2026-04-23 |
| 12 | B-219 | MP spawn with weapon: FP hands empty, fire does nothing on pickup-less arenas | FIXED v3 PENDING-PLAYTEST | 2026-04-23 |
| 13 | B-218 | All bots pile up at one spawn point on initial MP match frame (orchestrator skip) | FIXED v2 PENDING-PLAYTEST | 2026-04-23 |
| 14 | B-217 | F6 freeze toggle made bots invisible; now keep chrTick, zero speedmult instead | FIXED v2 PENDING-PLAYTEST | 2026-04-23 |
| 15 | B-221 | Hoverbike tap dismount parity on PC (mount path fixed earlier; dismount still double-tap) | FIXED-PENDING-PLAYTEST | 2026-04-23 |

Notes:
- Every bug in the top 15 carries a 2026-04-23 timestamp on its latest update. Earlier open rows (B-222 2026-04-21, B-223 deferred, B-220 2026-04-21, B-216 through B-195) sit just outside the window.
- Playtest sign-off is the critical path per the resume report: six fixes landed in code between Sunday and today but none have been run on Windows, so the FIXED-PENDING-PLAYTEST pile keeps growing.
- B-223 is the only pure OPEN/DEFERRED row in this slice; it is the dim-architecture follow-on to B-222 and blocks only after B-222 playtest proves compounding still occurs.

Sources: `context/bugs.md` (Open Bugs table), cross-checked against `context/audits/resume-report-2026-04-23.md` for Cursor-authored batches B-217..B-222 and B-234 propagation.
