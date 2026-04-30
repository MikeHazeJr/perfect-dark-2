# Dev Window v2 -- Design Document

| Field        | Value                                          |
|--------------|-------------------------------------------------|
| **Status**   | Accepted                                        |
| **Date**     | 2026-04-13                                      |
| **Authors**  | Mike Hays                                       |
| **Location** | `devtools/dev-window-v2/`                       |
| **Existing** | `devtools/_dev-window.ps1` (untouched)          |

---

## 1. Goal

Independent fork of the Dev Window with a professional, clean look.
Removes the Playtest tab. Adds a Log tab, status bar, and keyboard shortcuts.
Uses WPF (XAML) for DPI-aware rendering with PD accent colors used sparingly.

The existing Dev Window remains fully functional and untouched.

---

## 2. Tabs

### Kept (from v1)

| Tab | Content | Parity Notes |
|-----|---------|--------------|
| **Build** | BUILD button, RELEASE button, version spinners, Stable checkbox, client/server status, progress bar, copy errors/log, Open GitHub, Open Folder, Clean Build | Same cmake flags, same release.ps1 invocation |
| **Docs** | SplitContainer: file list (docs/ + context/) + markdown viewer | Same file discovery, read-only viewer |

### Removed

| Tab | Reason |
|-----|--------|
| **Playtest** | Spec says remove it. QC grid stays in v1 for fallback. |

### New

| Tab | Content |
|-----|---------|
| **Log** | Scrollable build output with line-level coloring (error=red, warning=orange, normal=dim). Filter textbox. Clear button. Auto-scroll toggle. Shows last build output. |

---

## 3. Color Palette

Professional dark theme inspired by PD Blue palette. Accents used sparingly.

| Name | Hex | RGB | Usage |
|------|-----|-----|-------|
| `Bg` | `#1E1E1E` | 30,30,30 | Window/panel background |
| `BgAlt` | `#282828` | 40,40,40 | Cards, alt rows |
| `BgInput` | `#323232` | 50,50,50 | Text field background |
| `BgHeader` | `#252530` | 37,37,48 | Status bar, tab header bg |
| `Text` | `#DCDCDC` | 220,220,220 | Primary text |
| `TextDim` | `#8C8C8C` | 140,140,140 | Secondary/disabled text |
| `Accent` | `#0090D0` | 0,144,208 | PD Blue-derived primary accent (buttons, active tab) |
| `AccentGlow` | `#00C8FF` | 0,200,255 | PD cyan accent (hover, selection highlight) |
| `Gold` | `#DAA520` | 218,165,32 | Section headers, version labels |
| `Green` | `#00B400` | 0,180,0 | Success, BUILD button |
| `Red` | `#DC3232` | 220,50,50 | Errors, failures |
| `Orange` | `#FF8C00` | 255,140,0 | Warnings, Run Server |
| `Blue` | `#508CDC` | 80,140,220 | Info, links |
| `Border` | `#3C3C3C` | 60,60,60 | Panel borders |

Rationale: Uses PD Blue palette's cyan (#00F0FF) toned down to #00C8FF and #0090D0 for professional restraint. Gold kept for hierarchy. No neon.

---

## 4. Technology

**PowerShell + WPF (XAML)**. Rationale:
- WPF is DPI-aware out of the box (WinForms is not without Per-Monitor DPI manifest hacks)
- XAML separates layout from logic cleanly
- Still PowerShell 5.1 compatible (WPF ships with .NET Framework on all Win10+)
- No new external runtime deps
- Inline XAML in the .ps1 file (single-file deployment, matching v1 pattern)

Font: Segoe UI at 10pt base (WPF TextElement.FontSize="13.33" = 10pt).
DPI: WPF handles automatically.

---

## 5. Auth Parity

Identical to v1:
- Background runspace calls `gh auth status` on startup
- Status shown as clickable label: "auth: ok" (green) or "auth: --" (dim)
- Click launches `gh auth login` in new PowerShell window when not authenticated
- No credentials stored by the tool

---

## 6. Versioning Parity

Identical to v1:
- Source of truth: `CMakeLists.txt` (`VERSION_SEM_MAJOR/MINOR/PATCH`)
- Version spinners read/write same fields with same regex
- Release pipeline invokes `devtools/release.ps1` with same arguments
- Release cache: `.dev-window-release-cache.json` (same file, shared)
- Stable/Dev toggle, same confirmation dialog
- Same cmake flags (SYNC RULE applies)

---

## 7. Enhancements

| Enhancement | Location | Description |
|-------------|----------|-------------|
| **Log tab** | New tab | Scrollable, filterable build output with color-coded lines |
| **Status bar** | Bottom of window | Branch name, dirty file count, HEAD short hash |
| **Pre-build Check** | Build tab button | Validates clean git state, runs `devtools/git-snapshot.sh` if present |
| **Keyboard shortcuts** | Global | Ctrl+B = Build, Ctrl+Shift+B = Clean Build, Ctrl+R = Release, Ctrl+G = Run Game, Ctrl+S = Run Server, Ctrl+L = Switch to Log tab, F5 = Refresh |
| **Window memory** | Settings | Size/position saved to `devtools/dev-window-v2/settings.json` |

---

## 8. File Structure

```
devtools/dev-window-v2/
  dev-window-v2.ps1        -- Main application (WPF + inline XAML)
  Dev Window v2.bat         -- Launcher
  settings.json             -- Window state (size, position) -- gitignored
  README.md                 -- Usage, tabs, shortcuts, independence from v1
```

---

## 9. Independence

- v2 has zero imports from v1
- v2 has its own settings file in its own directory
- v2 shares only: `devtools/release.ps1` (invoked as subprocess), CMakeLists.txt (version source), `.dev-window-release-cache.json` (release cache)
- Both tools can be open simultaneously without conflict
- Deleting `devtools/dev-window-v2/` removes v2 completely with no effect on v1
