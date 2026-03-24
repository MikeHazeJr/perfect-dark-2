# QC Test Checklist

> Items for in-game verification. Check off as tested. Add new items after each build.
> Back to [index](README.md)

## D3R-7 Build (S40) — Awaiting Test

| # | Test | Expected Result | Status |
|---|------|----------------|--------|
| 1 | Main menu shows "Modding..." button | Button visible below existing menu items | ⬜ |
| 2 | Click "Modding..." opens Modding Hub | Hub window with 3 tool buttons: Mod Manager, INI Editor, Model Scale | ⬜ |
| 3 | Mod Manager tab shows components | Base assets in collapsible section (collapsed by default), mod components with checkboxes | ⬜ |
| 4 | Toggle component off → Apply Changes | Returns to title screen, component no longer loaded in next match | ⬜ |
| 5 | .modstate persistence | Disable component, restart game, component stays disabled | ⬜ |
| 6 | INI Editor opens for selected component | Shows fields from the .ini manifest in schema-driven forms | ⬜ |
| 7 | Model Scale Tool | Character preview with XYZ scale sliders, rotating preview | ⬜ |
| 8 | B/Escape back navigation | Works at every level: hub → main menu, tool → hub | ⬜ |
| 9 | Controller navigation | D-pad navigates, A confirms, B goes back — all hub screens | ⬜ |
| 10 | Validation button | Reports broken dependencies or missing files if any exist | ⬜ |

## Previously Untested (from earlier sessions)

| # | Test | Expected Result | Status |
|---|------|----------------|--------|
| 11 | Look inversion (S22) | Inverted Y-axis works in gameplay | ⬜ |
| 12 | Updater diagnostics (S22) | Update tab shows version info | ⬜ |
| 13 | B-13 Prop Scale Fix | Mod character props render at correct scale | ⬜ |
| 14 | B-12 Participant System | Parallel participant pool syncs with chrslots (no visible change yet) | ⬜ |
