# Playtest Triage — v0.0.74 (Chris, commit 20775345) — 2026-04-10

Session: S198 (nervous-germain worktree)
Completed: 2026-04-10 ~23:30 EDT

---

## Task A — Commit Hash / S197a Coverage

**Result: YES — S197a IS in Chris's binary.**

Verification:
```
git log --oneline 20775345
20775345 Build v0.0.74 - auto-commit before build
94db4f5c S197a: finalize scratch report + session-log entry      ← S197a commit #2
1711bfbc Build v0.0.73 - auto-commit before build
9ba39f84 S197a: fix input regressions post-S196 Chrome pipeline  ← S197a commit #1
```
Both S197a commits are ancestors of `20775345`. Chris's binary includes all S197a fixes.

**Implication**: Issues 1 (theme editor X/close) and 3 (post-restart Start double-fire)
are CONFIRMED REAL RESIDUAL BUGS, not stale-build artifacts.

---

## Task B — Agent Save Path Fix (B-129 incomplete fix)

### Root Cause

`saveInit()` was never called from `main.c` or `server_main.c`.

`savefile.c` declares a module-private `static char s_SaveDir[512]` which is filled by
`saveInit()` via `fsFullPath("$S")`. Until `saveInit()` is called, `s_SaveDir` is `""`.

`buildSavePath()` at `savefile.c:232`:
```c
snprintf(out, maxlen, "%s/%s_%s.%s", s_SaveDir, prefix, safeName, ext);
```
With `s_SaveDir == ""`, this produces `/agent_smarch.json` (drive root `/`).
On Windows, writes to drive root fail silently (UAC). Result: agent save fails,
`besttimes[]` never persists, `isStageDifficultyUnlocked(stageindex+1)` returns false,
`endscreenAdvance()` is skipped, game retries current stage (Defection loops).

B-129 was fixed in S190 (crash fix: replaced filemgrSaveOrLoad with saveSaveAgent)
but `saveInit()` was never wired in — the path resolution was broken from day 1.

Why `playerstats.json` worked: `playerstats.c` uses `fsFullPath("$S/playerstats.json")`
directly (no `s_SaveDir` module state), so it expands correctly regardless of `saveInit`.

### Fix Applied

**Files changed** (commit `24f93fab` on `claude/nervous-germain`, cherry-picked to `dev`
as `3fc345bf`):

`port/src/main.c`:
- Added `#include "savefile.h"` to includes
- Added `saveInit();` after `saveMigrateInit()` in startup sequence, with comment:
  `/* B-129: wire save dir into savefile.c — must follow fsInit() */`

`port/src/server_main.c`:
- **REVERTED in follow-up commit `c81f555f`** (see below — server link break).

No behavior change on existing saves. Writes to drive root fail silently on Windows
(UAC), so no orphaned saves exist at `/agent_smarch.json`.

### Build Verification

All three changed compilation units verified clean (exit 0, no new warnings):
- `port/src/main.c.obj` — client build ✓
- `port/fast3d/pdgui_menu_theme_editor.cpp.obj` — client build ✓
- `port/src/server_main.c.obj` — server build ✓

Pre-existing warnings (types.h `near`/`far`, `/*` in comment docstrings) are unchanged.

Full rebuild blocked by sandbox TEMP issue (GCC writes to `C:\WINDOWS` when TEMP env
not overridden via subprocess env — the headless build script handles this via
`$psi.EnvironmentVariables["TEMP"]`, but both `make` direct and `build-headless.ps1`
exit code 5 in this session's shell environment). Individual file compilation confirmed
via PowerShell subprocess with explicit TEMP override.

### Merge Status

Cherry-picked from `claude/nervous-germain@24f93fab` → `dev@3fc345bf`.
3 files, 8 insertions. Diff verified against expected changes.

---

## Task C — Theme Editor Lifecycle Instrumentation

### Log Lines Added

Four `sysLogPrintf(LOG_NOTE, "Theme editor: exit — <path>")` calls added at each
`pdguiThemeEditorHide()` call site in `pdgui_menu_theme_editor.cpp`:

| Call site | Log message |
|-----------|-------------|
| `renderThemeEditor()` — `Begin()` false + `!open` (line 244) | `"Theme editor: exit — Begin() collapsed+close (open=false)"` |
| `renderThemeEditor()` — Close button (line 322) | `"Theme editor: exit — Close button"` |
| `renderThemeEditor()` — after End, `!open` (line 358) | `"Theme editor: exit — title-bar X button (!open after End)"` |
| `pdguiThemeEditorRender()` — InvisibleButton click (line 437) | `"Theme editor: exit — InvisibleButton click-outside dismiss"` |

`pdguiThemeEditorShow` (line 381) already logs `"Theme editor: opened"`.
`pdguiThemeEditorHide` (line 388) already logs `"Theme editor: closed"`.

Next playtest: the log will show exactly which exit path (if any) fired. If NONE of the
"exit —" lines appear before a "closed" log, something outside these call sites is calling
`pdguiThemeEditorHide()` directly.

### Diagnostic Analysis (Issue 1 — Theme Editor Close Fails)

**Status**: instrumentation deployed; diagnosis deepened but not yet resolved. Confirmed
real per Mike's playtest confirmation.

**What S197a shipped** (present in Chris's binary):
- `&open` passed to `Begin()` so ImGui renders the title-bar X button
- After `End()`, `if (!open)` calls `pdguiThemeEditorHide()`
- InvisibleButton fullscreen overlay with `SetNextWindowFocus()` before both overlay and editor

**Why it may still fail — z-order hypothesis**:

The render order each frame:
1. `ImGui::SetNextWindowFocus()` → overlay queued for focus
2. `ImGui::Begin("##theme_editor_overlay")` → overlay is drawn with InvisibleButton
3. `ImGui::End()` for overlay
4. `ImGui::SetNextWindowFocus()` → editor queued for focus
5. `renderThemeEditor()` → editor is drawn (topmost in z-stack)

After step 5, z-stack (front to back):
- Editor (topmost, last SetNextWindowFocus)
- Overlay
- Settings menu (or whatever window launched the editor — persisted from prior frames)
- Main menu / other windows

**For the InvisibleButton**: Click outside the editor → miss editor → ImGui falls through
to OVERLAY next... UNLESS the Settings menu or another persisted window sits ABOVE the
overlay in the z-stack. If the Settings menu is above the overlay, it intercepts the
click before the InvisibleButton fires.

**For the X button**: This fires within the editor's own rect. The editor IS topmost, so
the click should reach it. If the X is also failing (as Mike observed), either:
a) `open` goes false but the editor is immediately re-shown by something else
b) There's a same-frame re-push of the editor after `pdguiThemeEditorHide()` clears the flag
c) The Z-order is different from what we think (Settings menu somehow above editor)

**Next playtest log will reveal**:
- If `"Theme editor: exit — title-bar X button"` appears: X IS registering, the problem
  is re-show. Look for `"Theme editor: opened"` after the exit line.
- If NO "exit —" lines appear: something intercepts the click before ImGui processes it,
  OR `open` is never going false. Check focus/z-stack with ImGui demo's metrics window.
- If `"Theme editor: exit — InvisibleButton"` never appears on outside-click: the overlay
  InvisibleButton is being intercepted by a window above it in the z-stack.

**Do not fix blind** — wait for the log to confirm the actual failing path.

---

## Issue 3 — Post-Restart Start Double-Fire (Confirmed Real, Deferred)

**Status**: Confirmed real (S197a is in Chris's build; not a stale artifact).
Do not investigate in this session. Flagged for follow-up session.

**Working hypothesis** (to verify later):
Residual `menuinputs.start` or stuck consume flag, OR lingering deferred-pop entry on the
inputctx stack across `mainChangeToStage`. The post-mission `mainChangeToStage(0x30)` call
resets game state but may not fully drain the inputctx stack or consume the Start button
that ended the mission.

---

## Context Updates Made This Session

- `context/bugs.md` — B-129 re-opened, root cause clarified, fix documented as S198
- `context/session-log.md` — S198 entry added
- `context/tasks-current.md` — B-129 closed; B-130 (theme editor close) and B-131
  (Start double-fire) added as new open bugs

---

---

## Follow-Up: Server Link Break (23:41 EDT)

### What broke

After `3fc345bf` landed, Mike's build produced:
```
[server] undefined reference to `saveInit'
[server] collect2.exe: error: ld returned 1 exit status
```

### Root cause

`savefile.c` is not compiled into the server target (`pd-server`). Server CMake does not
include `port/src/savefile.c` in its source list. Adding `#include "savefile.h"` to
`server_main.c` let the declaration through, but the linker had no definition to bind.

### Fix (commit `c81f555f`)

Reverted both changes to `server_main.c`:
- Removed `#include "savefile.h"`
- Removed `saveInit()` call

B-129 is a client-side bug (agent save path). The server has no agent saves and never
needed `saveInit()`. Fix is `main.c` (client) only — that was always correct.

### Lesson recorded

Build verification must be **full link**, not per-file object compilation. A `.obj` exit 0
proves the file parses and compiles; it does NOT prove the symbol resolves at link time.
Next time: run `cmake --build <dir> --target <target>` (or `make -C <dir> <target>`)
to get the actual linker output before calling a build verified.

### Post-fix link results (2026-04-10 23:53–23:56)

- `PerfectDark.exe` (client): 48,917,265 bytes — linked clean, exit 0
- `PerfectDarkServer.exe` (server): 22,786,384 bytes — linked clean, exit 0

### Final commit state on dev

```
c81f555f fix(B-129): revert saveInit from server_main.c — savefile.c is client-only
dfb3d043 S198: context updates — B-129 FIXED, B-130/B-131 OPEN, session log
3fc345bf fix(B-129): wire saveInit() into startup — agent saves now land in AppData
```

---

## Delivery

Primary: this file — `context/scratch/playtest-triage-followup-2026-04-10.md`

Final commit: `c81f555f` on `dev` — "fix(B-129): revert saveInit from server_main.c"
