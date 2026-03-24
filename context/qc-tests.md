# QC Test Checklist

> Items for in-game verification. Check off as tested. Add new items after each build.
> Back to [index](README.md)

## D3R-7 Build (S40) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 1 | Main menu shows "Modding..." button | Button visible below existing menu items | ⬜ | |
| 2 | Click "Modding..." opens Modding Hub | Hub window with 3 tool buttons: Mod Manager, INI Editor, Model Scale | ⬜ | |
| 3 | Mod Manager tab shows components | Base assets in collapsible section (collapsed by default), mod components with checkboxes | ⬜ | |
| 4 | Toggle component off → Apply Changes | Returns to title screen, component no longer loaded in next match | ⬜ | |
| 5 | .modstate persistence | Disable component, restart game, component stays disabled | ⬜ | |
| 6 | INI Editor opens for selected component | Shows fields from the .ini manifest in schema-driven forms | ⬜ | |
| 7 | Model Scale Tool | Character preview with XYZ scale sliders, rotating preview | ⬜ | |
| 8 | B/Escape back navigation | Works at every level: hub → main menu, tool → hub | ⬜ | |
| 9 | Controller navigation | D-pad navigates, A confirms, B goes back — all hub screens | ⬜ | |
| 10 | Validation button | Reports broken dependencies or missing files if any exist | ⬜ | |

## Previously Untested (from earlier sessions)

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 11 | Look inversion (S22) | Inverted Y-axis works in gameplay | ⬜ | |
| 12 | Updater diagnostics (S22) | Update tab shows version info | ⬜ | |
| 13 | B-13 Prop Scale Fix | Mod character props render at correct scale | ⬜ | |
| 14 | B-12 Participant System | Parallel participant pool syncs with chrslots (no visible change yet) | ⬜ | |

## UI Scaling (S41) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 15 | Menu scaling at 1080p | All menus (main, lobby, match setup, mod manager) fit within viewport with no overflow or clipping | ⬜ | |
| 16 | Ultrawide clamping (21:9 / 32:9) | Large menus cap at 70% viewport width; no edge-to-edge stretch | ⬜ | |
| 17 | Scroll indicators in tall content | Mod manager component list and update version list show visible scrollbars when content overflows | ⬜ | |
| 18 | Font scaling | Menu text remains legible at 720p baseline and larger; minimum 12px floor prevents invisible text | ⬜ | |
