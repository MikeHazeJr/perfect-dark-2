# QC Test Checklist

> Items for in-game verification. Check off as tested. Add new items after each build.
> Back to [index](README.md)

## SPF-1 — Server Platform Foundation (hub/room/identity/phonetic)
**Added:** 2026-03-24  **Build:** commit `fb5450b` (dev)

### Hub Lifecycle

| # | Test | Expected | Status |
|---|------|----------|--------|
| 1 | Launch server (GUI mode), check log | `HUB: initialised, state=Lounge` in log | [ ] |
| 2 | Start a match from server GUI | Log shows `HUB: state Lounge -> Active`; Hub tab shows room 0 in "Match" state (blue) | [ ] |
| 3 | End match, return to lobby | Log shows `HUB: state Active -> Lounge`; Hub tab shows room 0 in "Lobby" state (green) | [ ] |
| 4 | Ctrl+C / close server window | Log shows `HUB: shutting down` before process exits | [ ] |

### Room System

| # | Test | Expected | Status |
|---|------|----------|--------|
| 5 | Open Hub tab in server GUI | Room table shows room 0 "Lounge" in Lobby state; 3 remaining rows show CLOSED or absent | [ ] |
| 6 | Start match, observe Hub tab | Room 0 transitions: LOBBY → MATCH during gameplay | [ ] |
| 7 | End match, observe Hub tab | Room 0 shows POSTGAME briefly (1 frame), then LOBBY | [ ] |
| 8 | Check log for room transitions | `ROOM 0: LOBBY -> MATCH`, `ROOM 0: MATCH -> POSTGAME`, `ROOM 0: POSTGAME -> LOBBY` sequence | [ ] |

### Player Identity

| # | Test | Expected | Status |
|---|------|----------|--------|
| 9  | Launch server fresh (no pd-identity.dat) | File `pd-identity.dat` created in home dir; default profile "Agent" created | [ ] |
| 10 | Launch server again (identity.dat exists) | Log shows identity loaded; no "default" message | [ ] |
| 11 | Corrupt pd-identity.dat (e.g. truncate it), relaunch | Server logs warning, rebuilds default identity, file overwritten with valid data | [ ] |

### Phonetic Encoding

| # | Test | Expected | Status |
|---|------|----------|--------|
| 12 | Server logs phonetic code on startup (if implemented in GUI) | Code displayed in format `XXXX-XXXX-XXXX-XXXX` (4 groups, each 4 chars) | [ ] |
| 13 | Verify encode→decode round-trip (unit: `phoneticEncode` then `phoneticDecode`) | Decoded IP + port match original input exactly | [ ] |

### Server GUI — Hub Tab

| # | Test | Expected | Status |
|---|------|----------|--------|
| 14 | Server GUI: verify two tabs in middle panel | "Server" tab and "Hub" tab both visible and clickable | [ ] |
| 15 | "Server" tab content intact | Player list + match controls unchanged from pre-SPF-1 behavior | [ ] |
| 16 | "Hub" tab: hub state label | Shows "Lounge" (blue) when idle, "Active" (green) when match running | [ ] |
| 17 | "Hub" tab: room table columns | Shows ID, Name, State, Players columns; room 0 row always present | [ ] |
| 18 | Log panel: HUB: prefix color | Lines starting with `HUB:` rendered in purple in the log panel | [ ] |

---

## B-12 Phase 2 — Participant API Migration
**Added:** 2026-03-24  **Build:** commit `94a2b1e` (dev)

| # | Test | Expected | Status |
|---|------|----------|--------|
| 1 | Open match setup, add 4 bots, start match | All 4 bots spawn, scores/names work, no crash | [ ] |
| 2 | Add bots, remove one, start match | Removed bot absent; remaining bots spawn correctly | [ ] |
| 3 | Save a bot setup (WAD save), quit, reload | Bot configuration restored correctly; names appear in setup UI | [ ] |
| 4 | Start match, end game, return to lobby | No stale participants; participant count resets to players only | [ ] |
| 5 | Challenge mode: enter a challenge | Correct bots populate for the challenge (sanity check function) | [ ] |
| 6 | Bot name generation with 3+ of same type (e.g. 3 MeatSims) | Names show "MeatSim:1", "MeatSim:2", "MeatSim:3" | [ ] |
| 7 | Copy simulant (add same sim type again) | Copy adds correctly; both show correct names | [ ] |
| 8 | Team assignment (Maximum Teams, Humans vs Sims) | Teams assigned correctly to all active participants | [ ] |

---

## 31-Bot Spawn Fix — Awaiting Test

**Build**: claude/affectionate-nash (merged to dev 2026-03-24)

| # | Test | Expected | Status |
|---|------|----------|--------|
| 1 | Open match setup lobby, add 1 player + 31 bots, start match | All 31 bots spawn and are active in-game | [ ] |
| 2 | Log check: `MATCHSETUP: chrslots=` after starting with 31 bots | `chrslots` shows bits 8-38 set (31 bots), botSlot=31 in log | [ ] |
| 3 | Start a match with 1 player + 24 bots (prior max) | 24 bots spawn, no regression | [ ] |
| 4 | Start a match with 8 players + 0 bots | All 8 players present, no crash | [ ] |
| 5 | Network: server host 1 player + 10 bots, client connects | Client receives correct chrslots (u64 v21), match starts | [ ] |

## D3R-10 Mod Pack export/import (S45a) — Awaiting Test

**Build:** main branch, 2026-03-24

| # | Test | Expected Result | Status |
|---|------|----------------|--------|
| 1 | Open Modding Hub → click "Mod Pack" tab | 4th tab appears, Export panel at top with Name/Author/Ver fields and output path | [ ] |
| 2 | Install ≥1 mod component; open Mod Pack tab | Component checklist shows installed components with category and type label | [ ] |
| 3 | Check 2–3 components, fill Name/Author/Version, set output path (e.g. `mods/test.pdpack`), click Export Pack | File created at output path; status line shows "Exported N component(s)" | [ ] |
| 4 | Export with no components selected | Export Pack button disabled (greyed out) | [ ] |
| 5 | Enter path to exported `.pdpack` in Import panel, click Preview | Manifest preview shows Pack/Author/Version and component list with [installed] or [new] badge | [ ] |
| 6 | Click Import Pack (permanent) | Components extracted to `mods/{category}/{id}/`; status shows "Imported N component(s). Use Apply Changes to reload." | [ ] |
| 7 | Click Import Pack with Session Only checked | Components land in `mods/.temp/{category}/{id}/` instead | [ ] |
| 8 | Try importing a `.pdpack` where all components already installed | All badges show [installed]; import still succeeds (overwrite) | [ ] |
| 9 | Enter a non-existent path and click Preview | Preview area stays hidden; no crash | [ ] |
| 10 | All/None buttons in export component list | All: selects all checkboxes; None: clears all | [ ] |

## D3R-11 Legacy Cleanup (S45b) — Awaiting Build + Test

**Pre-requisite**: `build-headless.ps1` must pass (blocked by TEMP env issue — run from normal PowerShell session).

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 1 | Launch game normally | No crash; title screen loads | [ ] |  |
| 2 | Start a GEX-mod multiplayer match (GEX stage) | Match loads; GEX textures have correct surface types (no footstep audio weirdness) | [ ] |  |
| 3 | Start a Kakariko multiplayer match | Match loads; Kakariko textures have correct surface types | [ ] |  |
| 4 | Start a GF64 multiplayer match (STAGE_EXTRA20-23) | Match loads; correct texture surface assignments | [ ] |  |
| 5 | Start a DarkNoon multiplayer match (STAGE_TEST_MP7) | Match loads; no crash (no texture overrides needed — default:break) | [ ] |  |
| 6 | Start a vanilla PD stage multiplayer match | Match loads; no regression to texture surface types | [ ] |  |
| 7 | Props spawn in GEX stage | Props use `g_ModelStates[].scale` (not GEX override). May need asset correction later. | [ ] |  |
| 8 | `mods/` dir with `mod.json` mod | Mod loads normally | [ ] |  |
| 9 | `mods/` dir WITHOUT `mod.json` | Dir silently skipped; log says "no mod.json, skipping" | [ ] |  |
| 10 | Dedicated server launches | No crash on startup (`modConfigLoad` stub removed) | [ ] |  |

---

## D3R-9 Network Distribution (S44) — Awaiting Test

**Setup needed**: two machines (or two instances) — one as server (has mod components), one as client (missing some components).

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 1 | Server hosts; client connects with same mod catalog | Client connects to lobby normally; no download prompt | [ ] |  |
| 2 | Server has extra mod component client lacks; client joins | Download prompt appears in lobby: "Server requires N mod component(s)" with Download / This Session / Skip buttons | [ ] |  |
| 3 | Click "Download" in prompt | Progress bar appears at bottom; component name + bytes shown; no crash | [ ] |  |
| 4 | Transfer completes | `DISTRIB_CSTATE_DONE`; `mods/{category}/{id}/` directory created; files present | [ ] |  |
| 5 | Restart → component persists | Component still in `mods/{category}/{id}/`; catalog has it | [ ] |  |
| 6 | Click "This Session" | Download proceeds; component lands in `mods/.temp/{id}/` | [ ] |  |
| 7 | Restart after session-only download | `mods/.temp/` present; crash state file present; no prompt on clean exit | [ ] |  |
| 8 | Click "Skip" | Download prompt closes; client joins without the component | [ ] |  |
| 9 | Kill feed: start match, get a kill | Kill feed entry appears top-right: "attacker > victim (weapon)" | [ ] |  |
| 10 | Kill feed headshot | Entry shows yellow text with "[HS]" indicator | [ ] |  |
| 11 | Crash recovery: force-kill process while `mods/.temp/` non-empty | On next launch, recovery prompt appears | [ ] |  |
| 12 | Recovery prompt → Keep | Temp components load normally | [ ] |  |
| 13 | Recovery prompt → Discard | `mods/.temp/` cleared; no components loaded | [ ] |  |

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

## Asset Catalog Expansion — 7 New Types (S46a) — Awaiting Test

**Build**: dev 28d5233 (2026-03-24)

| # | Test | Expected | Status |
|---|------|----------|--------|
| 26 | Startup log: weapon count | Log shows "assetcatalog: weapons: 47 entries" | [ ] |
| 27 | Startup log: prop count | Log shows "assetcatalog: props: 8 entries" | [ ] |
| 28 | Startup log: gamemode count | Log shows "assetcatalog: gamemodes: 6 entries" | [ ] |
| 29 | Startup log: hud count | Log shows "assetcatalog: hud elements: 6 entries" | [ ] |
| 30 | Startup log: extended total | Log shows "extended registration added N entries (N >= 67)" | [ ] |
| 31 | No crash on startup | Game reaches main menu without crash after asset catalog expansion | [ ] |
| 32 | Weapon INI scanner | `[weapon]` INI in mod `_components/weapons/` dir — registers without error in log | [ ] |

## Death Loop + Font Descenders + Dashboard (S42) — Awaiting Test

| # | Test | Expected Result | Status | Notes |
|---|------|----------------|--------|-------|
| 19 | Death loop — mod stage with no spawn pads | Load a mod stage with no INTROCMD_SPAWN. Player should spawn at a valid pad (room >= 0), not loop-die in void | [x] | We removed modded maps for now and will re-add them when we have a conversion tool for existing mod maps, and level editor for our own custom ones, native. |
| 20 | Death loop — log check | Console log shows "SPAWN: no spawn pads available, using pad N fallback (room=X)" with room >= 0 | [-] |  |
| 21 | Font descenders at 800x600 | Letters q, y, p, g fully visible in all menus — no bottom clipping | [ ] |  |
| 22 | Font descenders at 1080p | Same letters fully visible and proportionally sized at 1080p | [ ] |  |
| 23 | Dashboard commit button | Click Commit — window stays responsive during git add + commit. Spinner/status shows "Committing...", then "Committed" | [-] | No commit button, decided against it. Still, committing seems to work when we build / push. |
| 24 | Dashboard commit freeze (push) | Check "Push to GitHub" — window still responsive during push operation | [-] | We do not have a commit button, we opted not to. |
| 25 | QC notes TextChanged | Type a note in any QC row — save to disk immediately (not on focus-leave). Verify by closing + reopening dashboard | [x] | QC Note, here it is. |
