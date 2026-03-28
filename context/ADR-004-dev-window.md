# ADR-004: Dev Window — Unified Build & Playtest Tool

| Field        | Value                                      |
|--------------|--------------------------------------------|
| **Status**   | Accepted                                   |
| **Date**     | 2026-03-24                                 |
| **Authors**  | Mike Hays                                  |
| **Replaces** | `devtools/build-gui.ps1`, `devtools/playtest-dashboard.ps1` |
| **Output**   | `devtools/dev-window.ps1` + `devtools/Dev Window.bat` |

---

## 1. Context

We have two existing PowerShell WinForms tools:

- **build-gui.ps1** (2,779 lines) — build tool with version management, git integration, push releases, sound system, extraction tools.
- **playtest-dashboard.ps1** (1,303 lines) — QC test checklist with 80+ items, git commit, run buttons.

The playtest dashboard is nearly unusable because it creates 400+ individual WinForms controls (one Panel + ComboBox + Labels + TextBox per QC row). The build tool works but carries features we no longer need (extraction tools, multiple sound categories) and the user wants a cleaner, simpler layout.

A prior attempt to combine them into `devtools/dev-window.ps1` was rushed and has been plagued by recurring issues:

1. **PowerShell 5 syntax incompatibilities** — `= if (...)` is not valid; must use `= $(if (...) { ... } else { ... })`.
2. **Non-ASCII characters** (em dashes, en dashes) corrupting across encodings.
3. **String interpolation traps** — `($($var)s)` confuses the PS5 parser.
4. **Null reference exceptions** in Timer tick handlers.
5. **File truncation** from overlapping edits.
6. **Slow control loading** due to synchronous network calls on the UI thread.

---

## 2. Decision

Replace both tools with a single `devtools/dev-window.ps1` — a two-tab WinForms application that combines build/push/run functionality with a DataGridView-based QC playtest checklist.

Both original scripts are preserved as-is for fallback.

---

## 3. PowerShell Coding Rules

> **CRITICAL — every single line of the output file must follow these rules.**

| # | Rule | Reason |
|---|------|--------|
| 1 | **NEVER** use `= if (...)`. Always use `= $(if (...) { ... } else { ... })`. | PS5 parse error. |
| 2 | **NEVER** use em dashes, en dashes, or any character above U+007F. | Encoding corruption across editors/terminals. |
| 3 | **NEVER** use `($($var)...)` inside double-quoted strings. Use concatenation: `"text " + $var + " more"`. | PS5 parser confusion. |
| 4 | **EVERY** Timer `Add_Tick` handler must wrap its **entire** body in `try { ... } catch { }`. | Unhandled exceptions in tick handlers silently kill the timer and can crash the form. |
| 5 | **EVERY** function that accesses a UI control must null-check the control first. | Controls may not yet exist during init or may be disposed during shutdown. |
| 6 | All `Add-Type` blocks must be guarded with `if (-not ([System.Management.Automation.PSTypeName]'TypeName').Type)` checks. | Re-run safety; prevents "type already exists" errors. |
| 7 | File encoding: **UTF-8 without BOM**, no characters above 0x7F. | Consistent cross-tool behavior. |
| 8 | All `$ErrorActionPreference` changes must be restored in `finally` blocks. | Prevents global state leaks. |

---

## 4. File Architecture

The script is organized into 24 sequential sections. Each section is self-contained and depends only on sections above it. No forward references.

```
dev-window.ps1
|
+-- Section 1:  Assembly loading + console window hide
+-- Section 2:  C# helpers (AsyncLineReader, DarkMenuColorTable) -- guarded Add-Type
+-- Section 3:  Configuration (paths, state variables, all initialized)
+-- Section 4:  Font loading (Handel Gothic with fallback)
+-- Section 5:  Settings persistence (JSON load/save, .dev-window-settings.json)
+-- Section 6:  Color palette (all $script:Color* constants)
+-- Section 7:  Sound system (simplified: success + failure only)
+-- Section 8:  Utility functions (New-UIFont, Classify-Line, etc.)
+-- Section 9:  Form creation (main form, menu bar, tab control, bottom bar)
+-- Section 10: Build tab controls
+-- Section 11: Playtest tab controls (DataGridView, filter, summary)
+-- Section 12: QC file management (Load-QcFile, Save-QcFile, Populate-QcGrid)
+-- Section 13: Version management (Get/Set-ProjectVersion, GitHub release cache)
+-- Section 14: Git operations (Update-GitChangeCount, Auto-Commit, Start-ManualCommit)
+-- Section 15: Build pipeline (Start-Build, Start-Build-Step, timer tick handler)
+-- Section 16: Push pipeline (Start-PushRelease)
+-- Section 17: Game launch + status monitoring
+-- Section 18: Settings dialog
+-- Section 19: Button event handlers (all wired up)
+-- Section 20: Resize handler (Invoke-FormResize)
+-- Section 21: Main timer (2s interval: game status, git count, pending builds)
+-- Section 22: Initialization (Add_Shown with deferred loading)
+-- Section 23: Cleanup (Add_FormClosing)
+-- Section 24: Application::Run entry point with error handler
```

### Section Details

#### Section 1 — Assembly Loading + Console Hide

Load `System.Windows.Forms` and `System.Drawing`. Use `user32.dll` P/Invoke to hide the console window. Guard the `Add-Type` for the P/Invoke wrapper.

#### Section 2 — C# Helpers (Guarded Add-Type)

Two C# classes compiled via `Add-Type`:

- **AsyncLineReader** — Wraps a `System.Diagnostics.Process`, reads stdout/stderr on background threads, and enqueues lines into a `ConcurrentQueue<string>`. The UI timer drains the queue without blocking.
- **DarkMenuColorTable** — Extends `ProfessionalColorTable` to theme the `MenuStrip` dark.

Both are wrapped in type-existence guards:

```powershell
if (-not ([System.Management.Automation.PSTypeName]'PD2Dev.AsyncLineReader').Type) {
    Add-Type -TypeDefinition @"
    ...
    "@
}
```

#### Section 3 — Configuration

All `$script:` scoped variables initialized here. Nothing left uninitialized.

Key variables:

| Variable | Type | Purpose |
|----------|------|---------|
| `$script:ProjectRoot` | string | Resolved path to repo root (parent of devtools/) |
| `$script:BuildDir` | string | `$ProjectRoot/build` |
| `$script:ClientBuildDir` | string | `$BuildDir/client` |
| `$script:ServerBuildDir` | string | `$BuildDir/server` |
| `$script:QcFilePath` | string | `$ProjectRoot/context/qc-tests.md` |
| `$script:SettingsPath` | string | `$ProjectRoot/.dev-window-settings.json` |
| `$script:BuildProcess` | object | Current build process (or $null) |
| `$script:BuildStepQueue` | ArrayList | Queued build steps |
| `$script:CurrentBuildTarget` | string | "client" or "server" |
| `$script:ClientErrors` | ArrayList | Error lines from client build |
| `$script:ServerErrors` | ArrayList | Error lines from server build |
| `$script:ClientBuildResult` | string | "SUCCESS" / "FAILED" / $null |
| `$script:ServerBuildResult` | string | "SUCCESS" / "FAILED" / $null |
| `$script:ClientBuildTime` | int | Seconds elapsed |
| `$script:ServerBuildTime` | int | Seconds elapsed |
| `$script:IsBuilding` | bool | Lock to prevent concurrent builds |
| `$script:IsPushing` | bool | Lock to prevent concurrent pushes |
| `$script:GameProcess` | object | Running game process (or $null) |
| `$script:ServerProcess` | object | Running server process (or $null) |
| `$script:GitChangeCount` | int | Uncommitted file count |
| `$script:QcDirtyRows` | Hashtable | Row indices with unsaved notes |
| `$script:GhAuthOk` | bool | GitHub CLI authenticated |
| `$script:ReleaseCachePath` | string | `.dev-window-release-cache.json` |
| `$script:LatestRelease` | object | Cached release info from GitHub API |

#### Section 4 — Font Loading

Load Handel Gothic from a `.ttf` file in the repo via `PrivateFontCollection`. If the file is missing or loading fails, fall back to `"Segoe UI"`. Store the family in `$script:TitleFontFamily`.

#### Section 5 — Settings Persistence

- `Load-Settings` — reads `.dev-window-settings.json` via `Get-Content | ConvertFrom-Json`. Returns defaults if file missing or corrupt.
- `Save-Settings` — writes current settings via `ConvertTo-Json | Set-Content`.

Settings schema:

```json
{
  "GitHubRepo": "string",
  "FontSize": 10,
  "EnableSounds": true
}
```

#### Section 6 — Color Palette

All colors defined as `$script:Color*` constants using `[System.Drawing.Color]::FromArgb()`. No hex strings, no named colors.

Key colors (dark theme matching existing tools):

| Constant | RGB | Usage |
|----------|-----|-------|
| `$script:ColorBg` | 30, 30, 30 | Form/tab background |
| `$script:ColorBgAlt` | 40, 40, 40 | Panel/card background |
| `$script:ColorBgInput` | 50, 50, 50 | Text field background |
| `$script:ColorText` | 220, 220, 220 | Primary text |
| `$script:ColorTextDim` | 140, 140, 140 | Secondary text |
| `$script:ColorGold` | 218, 165, 32 | Accent, section headers, push button |
| `$script:ColorGreen` | 0, 180, 0 | Success, build button, run game |
| `$script:ColorRed` | 220, 50, 50 | Errors, failures |
| `$script:ColorOrange` | 255, 140, 0 | Run server button |
| `$script:ColorBlue` | 80, 140, 220 | Links, info |
| `$script:ColorBorder` | 60, 60, 60 | Panel borders |
| `$script:ColorProgress` | 0, 180, 0 | Progress bar fill |

#### Section 7 — Sound System

Simplified from build-gui.ps1. Only two sounds:

- `Play-SuccessSound` — plays a success WAV if sounds enabled.
- `Play-FailureSound` — plays a failure WAV if sounds enabled.

Uses `System.Media.SoundPlayer`. If WAV files are missing, silently skip (no errors).

#### Section 8 — Utility Functions

| Function | Purpose |
|----------|---------|
| `New-UIFont($size, $bold)` | Returns a `System.Drawing.Font` using the loaded family. |
| `Classify-Line($line)` | Returns "error", "warning", or "normal" based on regex matching of compiler output. |
| `Format-ElapsedTime($seconds)` | Returns human-readable elapsed time string. |
| `Get-ExePath($name)` | Resolves path to a built executable. |
| `Test-ExeExists($name)` | Returns $true if the exe file exists. |

#### Section 9 — Form Creation

Creates the top-level `Form`, `MenuStrip`, `TabControl`, and bottom bar `Panel`.

**Form:**
- Size: 900 x 700 (minimum 700 x 500)
- StartPosition: CenterScreen
- BackColor: `$script:ColorBg`
- Text: "Perfect Dark 2 - Dev Window"
- Icon: loaded from repo if present

**MenuStrip** (File menu only):
- File > GitHub Repository (opens browser)
- File > Project Folder (opens Explorer)
- File > Settings
- File > Exit

**TabControl:**
- Two tabs: "Build" and "Playtest"
- Dock: Fill
- DrawMode: OwnerDrawFixed (for dark theme tab painting)
- DrawItem handler for custom colors

**Bottom Bar Panel:**
- Dock: Bottom
- Height: 58
- Contains: Run Server button (left 50%, orange) + Run Game button (right 50%, green)
- Both buttons: 14pt bold font, FlatStyle.Flat
- Anchored to resize with form

#### Section 10 — Build Tab Controls

Layout matches the wireframe from the decision context. Three logical rows:

**Row 1 — Hero Buttons (top 40% of tab):**

| Control | Properties |
|---------|------------|
| `$script:BtnBuild` | Text: "BUILD", green FlatAppearance border, large font (18pt+), Anchor: Top+Left+Right, Width: 48% of tab |
| `$script:BtnPush` | Text: "PUSH", gold FlatAppearance border, large font (18pt+), Anchor: Top+Right, Width: 48% of tab |

Both are tall buttons (~200px) filling the top portion of the tab, side by side with a small gap.

**Row 2 — Status Area (middle):**

Left side:
- `$script:LblClientStatus` — "client: SUCCESS (12s)" or "client: --"
- `$script:LblServerStatus` — "server: SUCCESS (8s)" or "server: --"
- `$script:ProgressBar` — custom-drawn progress bar, visible only during builds
- `$script:BtnStop` — "STOP" button, red, Visible = $false, shown only during builds
- `$script:BtnCopyErrors` — "Copy Errors", Visible = $false, shown only when errors exist
- `$script:BtnCopyLog` — "Copy Log", Visible = $false, shown only when build complete

Right side:
- Version display: three TextBox controls for major.minor.patch, with [-][+] buttons and "per" label
- `$script:LblAuthStatus` — "auth ok" or "auth: --" (GitHub CLI status)

**Row 3** is the shared bottom bar (Section 9).

#### Section 11 — Playtest Tab Controls

**Header Panel** (top, ~40px):
- Filter ComboBox: items = "All", "Pending", "Pass", "Fail", "Skip"
- Summary labels: "Pass: N  Fail: N  Skip: N  Pending: N"
- Refresh button (reloads QC file)
- Reset button (sets all statuses to Pending after confirmation)

**DataGridView** (`$script:QcGrid`):
- Dock: Fill (fills remaining space below header)
- Columns:

| Column | Type | Width | ReadOnly | Notes |
|--------|------|-------|----------|-------|
| `#` | TextBox | 40 | Yes | Row number |
| `Status` | ComboBox | 90 | No | Items: Pending, Pass, Fail, Skip |
| `Test` | TextBox | 300* | Yes | Test description (* auto-fill) |
| `Expected` | TextBox | 200 | Yes | Expected result |
| `Notes` | TextBox | 150* | No | Tester notes (* auto-fill) |

- Properties:
  - AllowUserToAddRows = $false
  - AllowUserToDeleteRows = $false
  - SelectionMode = FullRowSelect
  - BackgroundColor = `$script:ColorBg`
  - GridColor = `$script:ColorBorder`
  - DefaultCellStyle: dark bg, light text
  - RowTemplate.Height = 28
  - ColumnHeadersDefaultCellStyle: gold text on dark bg

- Section header rows: gold text, darker background, span full width, read-only, no combo box interaction.

- Event wiring:
  - `CurrentCellDirtyStateChanged` — calls `CommitEdit()` to immediately commit ComboBox changes.
  - `CellValueChanged` — if Status column, save immediately; if Notes column, start/reset the debounce timer.
  - `CellPainting` — custom paint for section rows.

#### Section 12 — QC File Management

**`Load-QcFile`**
- Reads `context/qc-tests.md`
- Parses markdown table rows and section headers
- Returns an array of objects: `@{ Type="section"|"test"; Section="..."; Test="..."; Expected="..."; Status="Pending"; Notes="" }`
- Handles missing file gracefully (returns empty array + warning)

**`Save-QcFile`**
- Writes back to `context/qc-tests.md`
- Preserves the original markdown format
- Only updates Status and Notes columns
- Called on immediate combo change and on debounced notes save

**`Populate-QcGrid`**
- Clears and repopulates the DataGridView from the parsed QC data
- Applies section header styling
- Updates summary counts
- Applies current filter

**`Update-QcSummary`**
- Counts Pass/Fail/Skip/Pending across all rows
- Updates the summary labels in the playtest header

#### Section 13 — Version Management

**`Get-ProjectVersion`**
- Reads `CMakeLists.txt`, extracts `set(PD2_VERSION_MAJOR ...)` etc.
- Returns `@{ Major=0; Minor=0; Patch=3 }`

**`Set-ProjectVersion($major, $minor, $patch)`**
- Writes version numbers back into `CMakeLists.txt` via regex replace.

**`Load-ReleaseCache`**
- Reads `.dev-window-release-cache.json` from disk.
- Returns cached release data or $null.

**`Save-ReleaseCache($data)`**
- Writes release data to `.dev-window-release-cache.json`.

**`Fetch-LatestRelease`** (called from background runspace)
- Runs `gh api repos/{owner}/{repo}/releases/latest`
- Parses JSON, extracts tag + name + date
- Saves to cache file
- Updates UI on main thread via `$script:Form.Invoke()`

#### Section 14 — Git Operations

**`Update-GitChangeCount`**
- Runs `git status --porcelain` in the project root
- Counts lines, updates `$script:GitChangeCount`
- Updates any UI label showing change count

**`Auto-Commit`**
- If `$script:GitChangeCount -gt 0`:
  - `git add -A`
  - `git commit -m "Auto-commit before build"`
- Returns $true if commit succeeded or nothing to commit

**`Start-ManualCommit`**
- Opens a simple input dialog for commit message
- Runs `git add -A` then `git commit -m "$message"`
- Updates change count after

#### Section 15 — Build Pipeline

**`Start-Build`**
- Guards: if `$script:IsBuilding`, return
- Sets `$script:IsBuilding = $true`
- Calls `Auto-Commit`
- Clears previous results
- Populates `$script:BuildStepQueue` with four steps:
  1. CMake configure client
  2. Make client
  3. CMake configure server
  4. Make server
- Calls `Start-Build-Step` for the first step
- Makes stop button visible, hides hero buttons or disables them
- Starts `$script:BuildTimer`

**`Start-Build-Step`**
- Dequeues next step from `$script:BuildStepQueue`
- Creates `System.Diagnostics.Process` with:
  - RedirectStandardOutput = $true
  - RedirectStandardError = $true
  - UseShellExecute = $false
  - CreateNoWindow = $true
- Wraps in `AsyncLineReader` for non-blocking reads
- Records start time

**Build Timer Tick Handler** (100ms):
```powershell
$script:BuildTimer.Add_Tick({
    try {
        # Drain output queue
        # Update progress bar
        # Classify lines, track errors
        # If process exited:
        #   Record result + elapsed time
        #   If more steps in queue: Start-Build-Step
        #   Else: build complete, play sound, update UI
    }
    catch {
        # Log error, do not rethrow
    }
})
```

**Post-build:**
- `Copy-AddinFiles` — copies `data/` directory from `post-batch-addin` into the build output.
- Updates client/server status labels with result and elapsed time.
- Shows error buttons if errors were captured.
- Plays success or failure sound.

#### Section 16 — Push Pipeline

**`Start-PushRelease`**
- Guards: if `$script:IsPushing`, return
- Reads version from UI fields
- Calls `Set-ProjectVersion` to write to CMakeLists.txt
- Commits the version change
- Creates git tag `v$major.$minor.$patch`
- Invokes `release.ps1` (existing script) to push
- On completion: increments patch version, refreshes release cache

#### Section 17 — Game Launch + Status Monitoring

**`Start-Server`**
- Resolves server exe path via `Get-ExePath`
- If already running (`$script:ServerProcess` alive), kill it first
- Start new process, store in `$script:ServerProcess`
- Update Run Server button text to "STOP SERVER"

**`Start-Game`**
- Resolves game exe path via `Get-ExePath`
- Start new process, store in `$script:GameProcess`
- Update Run Game button text to "STOP GAME"

**`Update-RunButtons`**
- Called by main timer
- Checks if processes are still alive
- Toggles button text between "RUN X" / "STOP X"
- Enables/disables based on whether exe exists

#### Section 18 — Settings Dialog

A modal `Form` with:
- GitHub Repository text field (pre-filled from settings)
- Font Size text field (pre-filled, validated 8-16)
- Enable Sounds checkbox (pre-filled)
- Save + Cancel buttons

On Save: calls `Save-Settings`, applies font size change, closes dialog.

#### Section 19 — Button Event Handlers

All `Add_Click` handlers wired here. Each handler is a thin dispatcher:

```powershell
$script:BtnBuild.Add_Click({ Start-Build })
$script:BtnPush.Add_Click({ Start-PushRelease })
$script:BtnStop.Add_Click({ Stop-Build })
$script:BtnCopyErrors.Add_Click({ Copy-ErrorsToClipboard })
$script:BtnCopyLog.Add_Click({ Copy-LogToClipboard })
$script:BtnRunServer.Add_Click({ Toggle-Server })
$script:BtnRunGame.Add_Click({ Toggle-Game })
$script:BtnQcRefresh.Add_Click({ Refresh-QcGrid })
$script:BtnQcReset.Add_Click({ Reset-QcStatuses })
```

#### Section 20 — Resize Handler

**`Invoke-FormResize`**
- Recalculates hero button heights (40% of tab height)
- Adjusts status area layout
- Adjusts version field positions
- All using anchoring where possible; manual calc only where anchoring is insufficient

Wired to `$script:Form.Add_Resize({ Invoke-FormResize })`.

#### Section 21 — Main Timer

Interval: 2000ms. Entire body wrapped in `try { ... } catch { }`.

Tick actions:
1. Check game process alive, update button
2. Check server process alive, update button
3. Update git change count (throttled: only if not currently building)
4. Check for pending server build trigger (if applicable)

#### Section 22 — Initialization (Add_Shown)

```powershell
$script:Form.Add_Shown({
    try {
        # Immediate (UI thread, fast):
        Refresh-VersionDisplay
        Load-ReleaseCache | Update-ReleaseCacheUI
        Update-RunButtons

        # Deferred 50ms (one-shot timer):
        #   Load-QcFile
        #   Populate-QcGrid

        # Deferred 100ms (one-shot timer):
        #   Update-GitChangeCount

        # Background (runspace):
        #   gh auth status -> update $script:GhAuthOk
        #   Fetch-LatestRelease -> update UI via Invoke
    }
    catch {
        # Log but do not crash
    }
})
```

One-shot timers self-dispose after firing:
```powershell
$deferredTimer = New-Object System.Windows.Forms.Timer
$deferredTimer.Interval = 50
$deferredTimer.Add_Tick({
    try {
        $this.Stop()
        $this.Dispose()
        # ... deferred work ...
    }
    catch { }
})
$deferredTimer.Start()
```

#### Section 23 — Cleanup (Add_FormClosing)

- Stop all timers
- Kill build process if running
- Save any dirty QC rows
- Dispose fonts, timers, processes
- No `$ErrorActionPreference` leaks (finally blocks)

#### Section 24 — Entry Point

```powershell
try {
    [System.Windows.Forms.Application]::EnableVisualStyles()
    [System.Windows.Forms.Application]::Run($script:Form)
}
catch {
    [System.Windows.Forms.MessageBox]::Show(
        "Fatal error: " + $_.Exception.Message,
        "Dev Window Error",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error
    )
}
finally {
    # Final cleanup if not done in FormClosing
}
```

---

## 5. Build Tab Wireframe

```
+----------------------------------------------------------+
|  [          BUILD          ]  [          PUSH          ]  |
|  |                         |  |                        |  |
|  |      (green border)     |  |    (gold border/fill)  |  |
|  |                         |  |                        |  |
|  [_________________________]  [________________________]  |
|                                                           |
|  client: SUCCESS (12s)        version: [0].[0].[3]       |
|  server: SUCCESS (8s)                  [-][+] per        |
|  Building... ================           auth ok          |
|  [Copy Errors] [Copy Log]                                |
+----------------------------------------------------------+
|  [    RUN SERVER    ]  [      RUN GAME      ]            |
+----------------------------------------------------------+
```

- Hero buttons fill top ~40% of the tab, side by side at 48% width each with a gap.
- Status area fills the middle. Left column: build results + progress. Right column: version controls + auth.
- Stop button appears in place of progress bar during builds (Visible toggle).
- Error/log buttons appear only when relevant (Visible toggle).

---

## 6. Playtest Tab Wireframe

```
+----------------------------------------------------------+
|  Filter: [All v]   Pass:5  Fail:2  Skip:1  Pend:72      |
|                                       [Refresh] [Reset]  |
+----------------------------------------------------------+
|  #  | Status  | Test              | Expected    | Notes  |
|  ---+---------+-------------------+-------------+--------|
|     | SECTION | === SPF-1 Tests ===              |       |
|  1  | Pending | Hub initialization | No crash   |        |
|  2  | Pass    | Room creation...   | Room 0...  | ok     |
|  3  | Fail    | Client connect...  | Join OK    | hang   |
|  ...                                                      |
+----------------------------------------------------------+
|  [    RUN SERVER    ]  [      RUN GAME      ]            |
+----------------------------------------------------------+
```

- DataGridView replaces the 400+ individual controls.
- Section headers rendered as styled read-only rows (gold text, dark background).
- Filter dropdown shows/hides rows by status.
- Summary counts update live as statuses change.

---

## 7. Deferred Initialization Sequence

| Order | Timing | Thread | Action |
|-------|--------|--------|--------|
| 1 | Immediate | UI | Refresh version display from CMakeLists.txt |
| 2 | Immediate | UI | Load release cache from disk |
| 3 | Immediate | UI | Update run button states (check exe existence) |
| 4 | +50ms | UI (one-shot timer) | Load QC file + populate DataGridView |
| 5 | +100ms | UI (one-shot timer) | Run `git status --porcelain`, update change count |
| 6 | Background | Runspace | `gh auth status` -- update auth label |
| 7 | Background | Runspace | `gh api .../releases/latest` -- update release cache + UI |

This ensures the form appears instantly with basic state, then fills in data progressively.

---

## 8. Timer Summary

| Timer | Interval | Lifecycle | Purpose |
|-------|----------|-----------|---------|
| `$script:BuildTimer` | 100ms | Start on build, stop on complete | Drain async output queue, update progress, detect completion |
| `$mainTimer` | 2000ms | Start in Add_Shown, stop in FormClosing | Game/server status, git count, background checks |
| `$script:NotesSaveTimer` | 500ms | One-shot, reset on each keystroke | Debounced save of QC notes to file |
| Init timers (x2) | 50ms, 100ms | One-shot, self-disposing | Deferred load of QC data and git status |

All timer tick handlers are wrapped in `try { ... } catch { }` with no exceptions propagating.

---

## 9. What Is Excluded

These features from the existing tools are intentionally **not** carried forward:

| Feature | Source | Reason |
|---------|--------|--------|
| ROM extraction tools | build-gui.ps1 | Not needed for daily dev workflow |
| Sound extraction | build-gui.ps1 | Not needed for daily dev workflow |
| Model extraction | build-gui.ps1 | Not needed for daily dev workflow |
| Multiple sound categories | build-gui.ps1 | Simplified to success/failure only |
| Console output panel | build-gui.ps1 | Errors captured and copyable; full log not needed on-screen |
| Separate "Clean Build" button | build-gui.ps1 | Build always cleans; no separate action needed |
| Individual WinForms controls per QC row | playtest-dashboard.ps1 | Replaced by DataGridView |

---

## 10. File Artifacts

| File | Purpose |
|------|---------|
| `devtools/dev-window.ps1` | Main application script |
| `devtools/Dev Window.bat` | Launcher: `powershell -ExecutionPolicy Bypass -File "%~dp0dev-window.ps1"` |
| `.dev-window-settings.json` | User settings (gitignored) |
| `.dev-window-release-cache.json` | Cached GitHub release info (gitignored) |
| `context/qc-tests.md` | QC test file (unchanged format, shared with playtest-dashboard.ps1) |

---

## 11. Consequences

- Both `build-gui.ps1` and `playtest-dashboard.ps1` remain untouched for fallback.
- New settings file (`.dev-window-settings.json`) is separate from old `.build-settings.json`.
- QC file format is unchanged; the playtest dashboard can still read/write the same file.
- The DataGridView approach reduces control count from 400+ to ~10 (grid + headers + buttons), eliminating the performance problem.
- Single-file architecture avoids module import complexity in PowerShell 5.

---

## 12. Action Items

- [ ] Write complete `devtools/dev-window.ps1` following this spec exactly
- [ ] Write `devtools/Dev Window.bat` launcher
- [ ] Verify file parses without errors in PowerShell 5 (`powershell -Command "& { . .\dev-window.ps1 }"` dry parse)
- [ ] Test build pipeline end-to-end
- [ ] Test QC grid load/save round-trip
- [ ] Commit to dev branch
