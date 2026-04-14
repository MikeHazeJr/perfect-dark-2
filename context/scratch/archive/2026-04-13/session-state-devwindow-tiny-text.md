# Dev Window v2 — Font/Control Size Polish

**Session:** S248 (agitated-jackson worktree)
**Branch:** `dev-window-v2-font-polish` → merged to `dev` at `11fd1d5e`
**File:** `devtools/dev-window-v2/dev-window-v2.ps1` (1549 lines, unchanged count — all in-place)

## What was wrong

Five UI areas were rendering microscopic at normal seating distance / typical DPI:

| Area | Problem |
|------|---------|
| Hotkey hint row (title bar) | FontSize 10, foreground #1E3050 (near-invisible on dark bg) |
| Footer status bar | FontSize 11 for all tokens; too tight padding |
| Build status card | FontSize 11 for client/server/activity labels; BtnCheck had no explicit padding |
| Version panel | "V E R S I O N" + MAJ/MIN/PAT labels at FontSize 9; stepper buttons 22px wide (too small to click); auth/latest/dev labels FontSize 11 |
| ToolBtn base style | Padding 10,4 — all secondary buttons (Check, Copy, GitHub, Folder, Clean Build) too short |

## Changes made

### 1. Hotkey hint row (`line ~451`)
- `FontSize="10"` → `FontSize="12"`
- `Foreground="#1E3050"` → `Foreground="#304870"` (visible but still subtle)

### 2. Footer status bar (`line ~467`)
- Border `Padding="10,5"` → `Padding="10,7"`
- StatusVersion, StatusAuth, StatusBranch, StatusHash, StatusDirty: `FontSize="11"` → `FontSize="13"`

### 3. Build status card (`line ~566`)
- LblClientStatus, LblServerStatus, LblBuildActivity: `FontSize="11"` → `FontSize="13"`
- LblProgressText (inside progress bar): `FontSize="10"` → `FontSize="11"`
- BtnStop: `Padding="10,4"` → `Padding="10,7"` (explicit override, consistent with style bump)
- BtnCheck: added explicit `Padding="12,7"` (was inheriting ToolBtn default)

### 4. Version panel (`line ~604`)
- "V E R S I O N" label: `FontSize="9"` → `FontSize="11"`
- MAJ / MIN / PAT labels: `FontSize="9"` → `FontSize="11"`
- All 6 stepper buttons: `Width="22"` → `Width="28"`, `Padding="4,2"` → `Padding="4,5"` (≥24px touch target)
- TxtVerMajor / TxtVerMinor / TxtVerPatch: `Width="30"` → `Width="32"` (breathing room for wider steppers)
- ChkStable: `FontSize="11"` → `FontSize="13"`
- LblAuthStatus, LblLatestRelease, LblDevVersion: `FontSize="11"` → `FontSize="13"`

### 5. ToolBtn style (`line ~379`)
- `Padding="10,4"` → `Padding="10,7"` (all ToolBtn instances: GitHub, Project Folder, Clean Build, Copy Errors, Copy Log, Check)

## What was NOT touched
- BIG buttons: BUILD (FontSize 20), RELEASE (FontSize 14), RUN GAME / RUN SERVER (FontSize 14) — untouched per spec
- TabItem header style (BUILD / LOG / DOCS tabs) — FontSize 12, Padding 20,9 — fine as-is
- Log tab / Docs tab content — FontSize 11 Consolas — intentionally compact for log output
- All colors and layout structure unchanged

## Status
Done. No build verify needed (PowerShell/XAML only).
