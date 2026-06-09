# Dev Window v2

Professional build tool for Perfect Dark 2. Independent from the original Dev Window (`devtools/_dev-window.ps1`).

## Debug log

UTF-8 append log (for troubleshooting auth, startup, fatal errors):

`devtools/dev-window-v2/dev-window-v2-debug.log`

To disable logging: `$env:PD_DEV_WINDOW_DEBUG = '0'` before launch (or in the user environment).

If auth lines stop after `GhAuth BeginInvoke`, the runspace is still running; new builds log **WARN** every 5s while waiting and **timeout** at 45s. **Window Closing** is logged on exit.

## Launch

Double-click `Dev Window v2.bat` or run:

```
powershell -ExecutionPolicy Bypass -File devtools\dev-window-v2\dev-window-v2.ps1
```

## Tabs

### Build
- **BUILD** button -- incremental build (client + server). Shift+Click or Ctrl+Shift+B for clean build.
- **RELEASE** button -- sets version, builds, packages, pushes to GitHub via `devtools/release.ps1`.
- **Version spinners** -- major/minor/patch with +/- buttons. Source of truth: `CMakeLists.txt`.
- **Stable checkbox** -- toggles between Dev (prerelease) and Stable release.
- **Auth status** -- same behavior as original Dev Window: background `gh auth status` (match `Logged in` in output). Click opens a **new PowerShell window** (`-NoExit -Command "gh auth login"`).
- **Push** -- stages pending changes, commits them with the Dev Window message, pushes the current branch, then refreshes status/version labels.
- **Check** button -- validates clean git state and runs `devtools/git-snapshot.sh`.
- **Copy Errors / Copy Log** -- appear after builds.
- **GitHub / Project Folder / Open Kanban / Start Kanban Server / Stop Kanban Server / Clean Build** -- utility buttons. `Open Kanban` opens a local tokenless Kanban board even when remote phone access is unavailable or token-gated; `Start Kanban Server` starts remote phone access for the Kanban board only and copies/shows the join link; `Stop Kanban Server` stops the tracked remote server and tunnel.

### Log
- Live scrolling build output with color-coded lines (red = error, orange = warning).
- Filter text box to search output.
- Auto-scroll toggle.
- Clear button.

### Docs
- Browse `context/` and `docs/` markdown files.
- Split view: file list on left, content on right.
- Read-only viewer.

### CLI
- **Open Claude CLI** -- opens the Claude Code CLI in a new console at the project root.
- **Open Claude CLI (ultracode)** -- same, but starts the session with ultracode mode on. Ultracode is a Claude Code session *setting* (not an `--effort` level or env var), so it launches with `--settings` pointed at a temp `{"ultracode": true}` JSON file.
- **Open Codex CLI** -- opens the Codex CLI as administrator in a new console at the project root.
- That's the whole tab: three launchers, nothing else. (The earlier prompt-composition workbench -- action wraps, kanban cards, prompt box, headless mode -- was removed.)

## Bottom Bar
- **RUN SERVER** / **RUN GAME** -- toggles (click again to stop).

## Status Bar
- Branch name, HEAD short hash, dirty file count, auth status, current version.

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+B | Build (incremental) |
| Ctrl+Shift+B | Clean Build |
| Ctrl+R | Release |
| Ctrl+G | Run Game |
| Ctrl+T | Run Server |
| Ctrl+L | Switch to Log tab |
| F5 | Refresh (git status, version) |

## Settings

Window size and position are saved to `devtools/dev-window-v2/settings.json` on close.

## Window scaling

The UI is laid out on a fixed 1480x900 design canvas and wrapped in a Viewbox
(`Stretch=Uniform`, `StretchDirection=DownOnly`). On a screen large enough it
renders 1:1 (the doubled-bold fonts are untouched); on a smaller screen the
**entire** UI scales down uniformly to fit instead of clipping or forcing the
window off the edge. The window's minimum size is 720x480, and a restored
size/position is clamped to the current screen work area at launch so a size
saved on a bigger monitor never opens larger than (or off the edge of) a
smaller one.

## Independence

This tool is completely independent of the original Dev Window. Both can coexist:
- v2 lives in `devtools/dev-window-v2/` -- delete the folder to remove it.
- v1 lives at `devtools/_dev-window.ps1` -- untouched.
- Shared resources: `devtools/release.ps1` (called as subprocess), `CMakeLists.txt` (version), `.dev-window-release-cache.json` (release cache).

## Requirements

- Windows 10+ (WPF ships with .NET Framework)
- PowerShell 5.1 (built-in)
- MSYS2/MinGW64 at `C:\msys64` (for cmake/make/gcc)
- GitHub CLI (`gh`) for auth and releases
