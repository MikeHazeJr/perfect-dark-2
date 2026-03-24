# QC Test Checklist

> Items for in-game verification. Check off as tested. Add new items after each build.
> Back to [index](README.md)

## D3R-9 Network Distribution (S44) — Awaiting Test

**Setup needed**: two machines (or two instances) — one as server (has mod components), one as client (missing some components).

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 1 | Server hosts; client connects with same mod catalog | Client connects to lobby normally; no download prompt | [ ] | Baseline — no diff |
| 2 | Server has extra mod component client lacks; client joins | Download prompt appears in lobby: "Server requires N mod component(s)" with Download / This Session / Skip buttons | [ ] | |
| 3 | Click "Download" in prompt | Progress bar appears at bottom; component name + bytes shown; no crash | [ ] | |
| 4 | Transfer completes | `DISTRIB_CSTATE_DONE`; `mods/{category}/{id}/` directory created; files present | [ ] | Permanent download |
| 5 | Restart → component persists | Component still in `mods/{category}/{id}/`; catalog has it | [ ] | |
| 6 | Click "This Session" | Download proceeds; component lands in `mods/.temp/{id}/` | [ ] | Session-only |
| 7 | Restart after session-only download | `mods/.temp/` present; crash state file present; no prompt on clean exit | [ ] | |
| 8 | Click "Skip" | Download prompt closes; client joins without the component | [ ] | |
| 9 | Kill feed: start match, get a kill | Kill feed entry appears top-right: "attacker > victim (weapon)" | [ ] | |
| 10 | Kill feed headshot | Entry shows yellow text with "[HS]" indicator | [ ] | |
| 11 | Crash recovery: force-kill process while `mods/.temp/` non-empty | On next launch, recovery prompt appears | [ ] | Simulate crash |
| 12 | Recovery prompt → Keep | Temp components load normally | [ ] | |
| 13 | Recovery prompt → Discard | `mods/.temp/` cleared; no components loaded | [ ] | |

---

## D3R-8 Bot Customizer (S43) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 1 | Match setup → select bot slot → edit bot | Bot edit popup opens | [ ] |  |
| 2 | Bot edit popup has Advanced toggle | "Advanced" / "Simple" toggle button visible at bottom of popup | [ ] |  |
| 3 | Toggle Advanced → expanded section appears | Shows: Load Preset combo, Base Type combo, Accuracy/Reaction/Aggression sliders | [ ] |  |
| 4 | Adjust sliders (e.g. Accuracy 0.9) → Done | Slot shows updated bot config (visual feedback TBD) | [ ] |  |
| 5 | "Save as Preset…" button → name popup | Small popup with text input for preset name | [ ] |  |
| 6 | Type name → Save | No crash; log shows `botVariantSave: '{slug}' → '{path}'` | [ ] |  |
| 7 | Check filesystem after save | `{modsdir}/bot_variants/{slug}/bot.ini` exists with correct fields | [ ] |  |
| 8 | Preset appears immediately in Load Preset combo | New preset visible without restart (hot-register works) | [ ] |  |
| 9 | Restart game → preset still in combo | Scanner picks up `mods/bot_variants/` at startup | [ ] |  |
| 10 | Load Preset selects saved preset | Sliders update to saved accuracy/reaction/aggression values | [ ] |  |

## D3R-7 Build (S40) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 1 | Main menu shows "Modding..." button | Button visible below existing menu items | [ ] |  |
| 2 | Click "Modding..." opens Modding Hub | Hub window with 3 tool buttons: Mod Manager, INI Editor, Model Scale | [ ] |  |
| 3 | Mod Manager tab shows components | Base assets in collapsible section (collapsed by default), mod components with checkboxes | [ ] |  |
| 4 | Toggle component off → Apply Changes | Returns to title screen, component no longer loaded in next match | [ ] |  |
| 5 | .modstate persistence | Disable component, restart game, component stays disabled | [ ] |  |
| 6 | INI Editor opens for selected component | Shows fields from the .ini manifest in schema-driven forms | [ ] |  |
| 7 | Model Scale Tool | Character preview with XYZ scale sliders, rotating preview | [ ] |  |
| 8 | B/Escape back navigation | Works at every level: hub → main menu, tool → hub | [ ] |  |
| 9 | Controller navigation | D-pad navigates, A confirms, B goes back — all hub screens | [ ] |  |
| 10 | Validation button | Reports broken dependencies or missing files if any exist | [ ] |  |

## Previously Untested (from earlier sessions)

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 11 | Look inversion (S22) | Inverted Y-axis works in gameplay | [ ] |  |
| 12 | Updater diagnostics (S22) | Update tab shows version info | [ ] |  |
| 13 | B-13 Prop Scale Fix | Mod character props render at correct scale | [ ] |  |
| 14 | B-12 Participant System | Parallel participant pool syncs with chrslots (no visible change yet) | [ ] |  |

## UI Scaling (S41) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 15 | Menu scaling at 1080p | All menus (main, lobby, match setup, mod manager) fit within viewport with no overflow or clipping | [ ] |  |
| 16 | Ultrawide clamping (21:9 / 32:9) | Large menus cap at 70% viewport width; no edge-to-edge stretch | [ ] |  |
| 17 | Scroll indicators in tall content | Mod manager component list and update version list show visible scrollbars when content overflows | [ ] |  |
| 18 | Font scaling | Menu text remains legible at 720p baseline and larger; minimum 12px floor prevents invisible text | [ ] |  |

## Death Loop + Font Descenders + Dashboard (S42) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 19 | Death loop — mod stage with no spawn pads | Load a mod stage with no INTROCMD_SPAWN. Player should spawn at a valid pad (room >= 0), not loop-die in void | [ ] |  |
| 20 | Death loop — log check | Console log shows "SPAWN: no spawn pads available, using pad N fallback (room=X)" with room >= 0 | [ ] |  |
| 21 | Font descenders at 800x600 | Letters q, y, p, g fully visible in all menus — no bottom clipping | [ ] |  |
| 22 | Font descenders at 1080p | Same letters fully visible and proportionally sized at 1080p | [ ] |  |
| 23 | Dashboard commit button | Click Commit — window stays responsive during git add + commit. Spinner/status shows "Committing...", then "Committed" | [ ] |  |
| 24 | Dashboard commit freeze (push) | Check "Push to GitHub" — window still responsive during push operation | [ ] |  |
| 25 | QC notes TextChanged | Type a note in any QC row — save to disk immediately (not on focus-leave). Verify by closing + reopening dashboard | [ ] |  |
