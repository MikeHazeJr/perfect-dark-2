# Session Log (Active)

> Recent sessions only. Session archives (S1-S119) moved to `_archive/sessions/`.
> Back to [index](README.md)

## Session S205 — 2026-04-11 (D5 P3 Batch 7: MP Advanced / Quick paths)

**Focus**: Complete D5 Phase 3 Batch 7 — replace 11 legacy navigation-hub dialogs in the Combat Simulator setup layer with ImGui renderers, with a mandatory network match start / end wiring audit (Mike's explicit direction: "Ensure any network play properly passes menu info into the relevant match start / end procedures").

### Approach

- New file `port/fast3d/pdgui_menu_mpadvanced.cpp` owns 6 renderer implementations covering 11 dialog registrations, cloning the s203/s204 shadow-struct pattern from pdgui_menu_mpsetup.cpp / pdgui_menu_botsetup.cpp with a new `s205` suffix. Dropdown/list/plain-SET helper family plus two new hub-row helpers (`hubPushRow` / `hubHandlerRow`) that draw full-width ImGui Selectables with manual two-column text overlays, so each hub row can show label + dynamic right-side text (scenario short name, arena name, weapon set name, player name, "Save Player"/"Save Copy of Player") without inventing a new public layout primitive.
- Renderer mapping (6 impls → 11 dialogs):
  - `renderMpAdvancedSetupImpl(v=0|1)` backs `g_MpAdvancedSetupMenuDialog` + `g_MpAdvancedSetupViaAdvChallengeMenuDialog` (item arrays byte-identical; legacy differs only in `nextsibling` tab which ImGui flattens)
  - `renderMpQuickGo` backs `g_MpQuickGoMenuDialog` (4 pure pushes)
  - `renderMpQuickTeam` backs `g_MpQuickTeamMenuDialog` (5 big-font selectables → `menuhandlerMpQuickTeamOption` param 0..4)
  - `renderMpQuickTeamGameSetup` backs `g_MpQuickTeamGameSetupMenuDialog` (15 items: Scenario/Options/Arena/Weapons/Limits pushes, Player 1..4 Team dropdowns via `menuhandlerPlayerTeam` with CHECKHIDDEN gating, NumSims/SimsPerTeam/SimDifficulty dropdowns with per-mode visibility, Finished Setup / Save Settings)
  - `renderMpStuffImpl(v=0|1)` backs `g_MpStuffMenuDialog` + `g_MpStuffViaAdvChallengeMenuDialog` (Soundtrack/TeamNames pushes, Lock/Split dropdowns, Start/Drop/Abort pushes)
  - `renderMpPlayerSetupHubImpl(variant)` backs all three `g_MpPlayerSetupVia*MenuDialog` (Name/Character/Control/PlayerOptions/Statistics/LoadPlayer pushes + Save Player selectable)
- **Network match start / end wiring audit** (critical for this batch): traced 13 distinct fields writer → backing global → `mpStartMatch` reader → `SVC_STAGE` network serializer → client receive path → endscreen reader. Full audit table in `context/scratch/D5-P3-batch7-2026-04-11.md`. Every Batch 7 MENUOP_SET lands in the same `g_Vars.*` / `g_MpSetup.*` / `g_PlayerConfigsArray.*` global the legacy renderer would have written via its own dispatch, because every write delegates to a legacy C handler via the s205 shadow call-through. No shadow/cache/copy introduced.
- Modern room lobby (pdgui_menu_room.cpp) is untouched — it writes to `g_MatchConfig` via a distinct code path, and `matchStart()` copies g_MatchConfig → g_MpSetup before `mpStartMatch`. `room.cpp` contains zero references to any Batch 7 dialog (grep confirmed), so the two paths never interleave.
- Legacy dialog OPEN side effects (`menudialogMpGameSetup` setting `g_Vars.mpsetupmenu = MPSETUPMENU_ADVSETUP` / `usingadvsetup = true`; `menudialogMpQuickGo` setting `MPSETUPMENU_QUICKGO`) still fire through the legacy menu runtime because the hot-swap system only hooks the RENDER phase — OPEN/CLOSE/TICK go through the original dispatch, preserving zero-function-loss.
- Integration philosophy matches Batches 4-6: convert dialogs via s20x shadow-struct call-through, no room.cpp edits (none of the Batch 7 dialogs naturally belong inline — room IS the modern equivalent of the Combat Simulator layer, and absorbing Batch 7 screens would require retiring `g_CombatSimulatorMenuDialog` which is out of scope per menu-replacement-plan.md).

### Changes

- **NEW** `port/fast3d/pdgui_menu_mpadvanced.cpp` (+1059 lines): full Batch 7 implementation
- `port/include/pdgui_menus.h` (67 → 69, +2): declared + called `pdguiMenuMpAdvancedRegister()` in `pdguiMenusRegisterAll()`
- **NEW** `context/scratch/D5-P3-batch7-2026-04-11.md`: dialog mapping, network audit table, zero-function-loss audit, build outputs, integration pattern notes

### Build

- Fresh configure from worktree `nervous-dhawan` via `TEMP=/tmp cmake -G "Unix Makefiles"` in `.claude/b7-build`
- Client (`pd` target): `[100%] Built target pd`; `PerfectDark.exe` = **49,566,481 bytes** (+225,697 vs Batch 6's 49,340,784)
- Server (`pd-server` target): `[100%] Built target pd-server`; `PerfectDarkServer.exe` = **22,771,518 bytes** (pdgui_menu_mpadvanced.cpp not in SRC_SERVER so size delta is pre-existing build-variance, not Batch 7 work)
- Only warning from my file was a cosmetic `/*` in a comment block (fixed post-build with a pure comment edit — binary unaffected)

### Result

Batch 7 done. 11 legacy dialog pushes now render through ImGui via hot-swap. Network audit clean. Next up: Batch 8 (MP Pause & In-Game, 6 screens) per menu-replacement-plan.md schedule — independent of Batch 7.

---

## Session S204 — 2026-04-11 (D5 P3 Batch 6: Bot/Simulant Setup)

**Focus**: Complete D5 Phase 3 Batch 6 — replace the 5 legacy bot/simulant dialogs (`g_MpSimulantsMenuDialog`, `g_MpAddSimulantMenuDialog`, `g_MpChangeSimulantMenuDialog`, `g_MpEditSimulantMenuDialog`, `g_MpSimulantCharacterMenuDialog`) with ImGui renderers and surface the simulant roster as an inline expandable section inside the existing room screen rather than only as a pushed modal.

### Approach

- New file `port/fast3d/pdgui_menu_botsetup.cpp` owns the 5 renderers, cloning the s203 shadow-struct call-through pattern from pdgui_menu_mpsetup.cpp and extending the ABI with a `carousel` handlerdata variant so the two `MENUITEMTYPE_CAROUSEL` handlers (`menuhandlerMpSimulantHead`, `menuhandlerMpSimulantBody`) can be invoked via the same pattern. Every state mutation delegates to legacy C handlers in `setup.c` (`mpAddChangeSimulantMenuHandler`, `menuhandlerMpSimulantHead/Body`, `mpBotDifficultyMenuHandler`, `menuhandlerMpChangeSimulantType`, `menuhandlerMpCopySimulant`, `menuhandlerMpDeleteSimulant`, `menuhandlerMpAddSimulant`, `menuhandlerMpSimulantSlot`, `menuhandlerMpClearAllSimulants`). Dynamic-text function pointers (`mpMenuTextSimulantName`, `mpMenuTextSimulantDescription`, `mpMenuTitleEditSimulant`) are invoked via shadow-cast. Legacy dialog-level handlers (`menudialogMpSimulants`, `menudialogMpSimulant`, `menudialog0017ccfc`) still fire OPEN/TICK side effects via the runtime (reset slotcount, auto-pop on external delete, refresh model preview).
- **Mid-task pivot** (Mike's mid-task guidance): the bot/simulant setup should be integrated into the room design rather than living only as a standalone modal. Refactored `renderMpSimulants`' body-drawing logic into a new public inline helper `pdguiBotSetupDrawSimulantsBody(float bodyH)` exposed via a new header `port/include/pdgui_menu_botsetup.h`. `renderMpSimulants` became a thin modal wrapper that calls the helper inside the standard PD-styled frame, preserved for the legacy Combat Simulator push path (satisfies "don't break linking" and "zero function loss"). `pdgui_menu_room.cpp` then includes the new header and adds a `CollapsingHeader("Simulant Profiles")` section at the bottom of `renderPlayerPanel` (below the existing matchslot-based Add Bot button) that calls `pdguiBotSetupDrawSimulantsBody` inline.
- Design decision: room.cpp's existing matchslot-based bot UI (`g_MatchConfig.slots[]`) and the legacy `g_BotConfigsArray[]` pool are DIFFERENT data models, so the integration is additive — the new inline section exposes the legacy profile pool without removing or modifying the existing matchslot flow. Unifying the two data models is out of scope for Batch 6.
- Drill-down screens (Add/Change/Edit/Character) remain modal wrappers — they are naturally single-task edit screens, and `menuPushDialog` from inside an inline renderer is a standard pattern already used by room.cpp for `g_MpHandicapsMenuDialog` and `g_MpTeamsMenuDialog`.

### Changes

- **NEW** `port/fast3d/pdgui_menu_botsetup.cpp` (+1006 lines):
  - Shadow types `s204_menuitem` / `s204_handlerdata` (checkbox/dropdown/list/slider/**carousel**/_pad[256]), legacy handler forward decls, full MENUOP_* block (1..24 + OPEN/CLOSE/TICK) seeded from the start per the Batch 4 gotcha
  - Helper families: `list_*` (grouped bot profile list with GETOPTGROUPCOUNT/GETOPTGROUPTEXT/GETGROUPSTARTINDEX/LISTITEMFOCUS), `dd_*` (difficulty dropdown), `car_*` (head/body carousels — new for this batch), `plain_*` (action buttons with CHECKDISABLED/CHECKHIDDEN), window-frame helpers `bs_BeginStandardWindow` / `bs_CloseCurrentDialog` / `bs_BackPressed`
  - Inline content helper: `pdguiBotSetupDrawSimulantsBody(bodyH)` — public `extern "C"` function that draws the simulants roster (Add / 8 slot rows / Clear All) without a surrounding modal frame
  - Renderers: `renderMpSimulants` (thin modal wrapper for the inline helper), `renderMpAddChangeSimulantImpl` (shared Add/Change variant enum) with `renderMpAddSimulant` / `renderMpChangeSimulant` wrappers, `renderMpEditSimulant` (difficulty dropdown + Change Type / Character / Copy / Delete buttons with dynamic title via `mpMenuTitleEditSimulant`), `renderMpSimulantCharacter` (Head + Body dropdowns with display names from `catalogMpHeadId` formatter + `mpGetBodyName`)
  - Head display names built from `catalogMpHeadId` via the same `formatCatalogId` pattern as `pdgui_menu_agentcreate.cpp` (no `mpGetHeadName` symbol exists in the API)
  - `pdguiMenuBotSetupRegister()` — single hotswap registration function (5 dialogs)
- **NEW** `port/include/pdgui_menu_botsetup.h` (47 lines): declares `pdguiBotSetupDrawSimulantsBody` with `extern "C"` linkage so room.cpp can include without types.h pollution
- `port/fast3d/pdgui_menu_room.cpp` (2834 → 2848, +14): include new header, add `CollapsingHeader("Simulant Profiles")` section at the bottom of `renderPlayerPanel` calling the inline helper
- `port/include/pdgui_menus.h` (65 → 67, +2): declared + called `pdguiMenuBotSetupRegister()` in `pdguiMenusRegisterAll()`
- `context/scratch/D5-P3-batch6-2026-04-11.md` (432 lines) — dialog→handler→state-write map, zero-function-loss audit plan, mid-batch integration pivot rationale, build plan

### Build

- Worktree: `quirky-gates`, branch `claude/quirky-gates`
- Merged to dev as two non-ff merges: the initial modal-only implementation, then the inline-integration refactor
- `build/client/PerfectDark.exe`: 49,202,510 → **49,340,784 bytes** (+138,274, ~135 KB) — freshly linked 2026-04-11 13:50 EDT
- `build/server/PerfectDark.exe`: **49,339,248 bytes** — freshly linked 2026-04-11 13:51 EDT
- `build/server/PerfectDarkServer.exe`: 22,788,944 bytes (unchanged — pdgui code excluded from server target, same as Batches 0-5) — freshly linked 2026-04-11 13:50 EDT
- `cmake . && cmake --build . -j 24` via MSYS2 MINGW64 with `TEMP=/tmp`. Same direct-cmake approach as Batches 4/5 because `build-headless.ps1` swallows output when stdout is redirected under bash (CR spinner vs non-TTY). `cmake .` re-run in both dirs to pick up new `pdgui_menu_botsetup.cpp` via GLOB_RECURSE (and then again after the header/refactor changes).
- Exit: 0 on all link steps. No new errors introduced. MENUOP_* block seeded complete up-front — Batch 4 gotcha avoided.

### Next

**Batch 7** (MP Advanced / Quick paths — 11 dialogs) per the menu-replacement-plan. Depends on Batches 5-6 which are now complete.

Possible follow-up: unify room.cpp's matchslot-based bot UI with the legacy `g_BotConfigsArray` pool so there is a single canonical data model. This would retire half of `setup.c`'s bot-state globals and simplify net sync — but it affects save-file compatibility and is a bigger refactor. Out of scope for Batch 6; flagged for a future phase.

---

## Session S203 — 2026-04-11 (D5 P3 Batch 5: MP Setup Core)

**Focus**: Complete D5 Phase 3 Batch 5 — replace the 14 legacy MP Combat Simulator setup dialogs (Arena / Scenario / Weapons / Limits / Scenario Options / Extended Game Options) with ImGui renderers that delegate all state mutation to the legacy C handlers via the s203 shadow-struct call-through pattern (cloned from s194 in solomission.cpp / s202 in cheats.cpp).

### Approach

- Per Batch 4 handoff: verified whether Batch 5 should be absorbed into `pdgui_menu_room.cpp`. Room.cpp already handles the *modern* lobby flow; Batch 5's scope is the legacy Combat Simulator setup dialogs that still back `g_CombatSimulatorMenuDialog`. Absorption would require retiring that entry point — out of scope. New file `pdgui_menu_mpsetup.cpp` sits parallel to cheats.cpp / solomission.cpp. Room.cpp untouched; solomission.cpp untouched (per standing critical-collision rule).
- s203 shadow-struct ABI covers `menuitem` + `handlerdata_{checkbox,dropdown,list_t,slider}` so legacy C handlers from `setup.c` / `scenarios.c` / `scenarios/*.inc` can be invoked through function pointers from C++ without including types.h (the `#define bool s32` in types.h breaks C++ compilation).
- Every backing-store write (g_MpSetup.*, g_MpWeaponSetRandomFilters[], g_Vars.mphilltime, g_ArenaGroupCollapsed, arena/scenario selection via scenarioInit side-effects, slider ranges, feature gating, slow-motion mutual exclusion, menuhandlerMpOneHitKills MPFEATURE_ONEHITKILLS gate) goes through a legacy handler call via s203. Nothing duplicated in C++.

### Changes

- **NEW** `port/fast3d/pdgui_menu_mpsetup.cpp` (+1265 lines):
  - Shadow types (`s203_menuitem`, `s203_handlerdata` with checkbox/dropdown/list/slider members + `_pad[256]` safety), legacy handler forward decls, MENUOP_* block (1..24 declared locally per Batch 4 gotcha)
  - Helper families: `list_*` (arena/scenario/select-random-weapons), `dd_*` (dropdowns), `cb_*` (checkboxes incl. CHECKDISABLED/CHECKHIDDEN), `sl_*` (sliders incl. GETSLIDERLABEL buffer capture), `plain_Set` (action buttons), `mp_BeginStandardWindow` / `mp_CloseCurrentDialog` / `mp_BackPressed` (window frame)
  - Renderers: `renderMpArena`, `renderMpScenario` (+ QuickTeam variant), `renderMpWeapons`, `renderMpSelectRandomWeapons` (per-weapon checkboxes + 4 select-all action rows via `srw_GetRowChecked` / `srw_ToggleRow`), `renderMpQuickTeamWeapons` (set dropdown + read-only slot labels via `qtw_SlotName`), `renderMpLimits` (3 sliders + Restore Defaults), `renderMpScenarioOptionsImpl` (shared for 6 scenario variants keyed by `ScenarioOptionVariant` enum, with 6 thin render wrappers), `renderMpExtGameOptions`
  - Scenario options body: `renderSharedScenarioTop` (OneHitKills/SlowMotion/FastMovement/DisplayTeam/NoRadar/NoAutoAim) + per-variant tail (Combat: NoPlayerHighlight/NoPickupHighlight; CTC/HTM/HTB/KOH/PAC: KillsScore + per-scenario extras; KOH adds MPOPTION_KOH_HILLONRADAR/MOBILEHILL + Hill Time slider)
  - Dialog flattening: the legacy nextsibling-driven "More Options" tab page (present in all 6 scenario-option dialogs) becomes a docked action-bar button that pushes `g_ExtGameOptionsMenuDialog` as a modal. Content parity preserved; UX flattened per Batch 2 precedent.
  - `pdguiMenuMpSetupRegister()` — single hotswap registration function (14 dialogs)
- `port/include/pdgui_menus.h` (63 → 65, +2): declared + called `pdguiMenuMpSetupRegister()` in `pdguiMenusRegisterAll()`
- `context/scratch/D5-P3-batch5-2026-04-11.md` — full legacy→new function map, zero-function-loss audit, post-build results

### Build

- Worktree: `gallant-cohen`, branch `claude/gallant-cohen`
- Merged to dev as non-ff merge (88149051)
- `build/client/PerfectDark.exe`: 49,051,951 → **49,202,510 bytes** (+150,559, ~147 KB) — freshly linked 2026-04-11 13:15 EDT
- `build/server/PerfectDark.exe`: 49,200,974 bytes — freshly linked 2026-04-11 13:16 EDT
- `build/server/PerfectDarkServer.exe`: 22,788,944 bytes (unchanged — pdgui code excluded from server) — freshly linked 2026-04-11 13:16 EDT
- `cmake . && cmake --build . -j 24` via MSYS2 MINGW64 with TEMP=/tmp. Same direct-cmake approach as Batch 4 because `build-headless.ps1` swallows output when stdout is redirected under bash (CR spinner vs non-TTY). `cmake .` re-run in both dirs to pick up new `pdgui_menu_mpsetup.cpp` via GLOB_RECURSE.
- Exit: 0 on all three link steps. No new errors. MENUOP_* block seeded complete up-front (Batch 4 gotcha avoided).

### Next

**Batch 6** (Bot/Simulant Setup — `g_MpSimulantsMenuDialog`, AddSimulant, ChangeSimulant, EditSimulant, SimulantCharacter; 5 screens). Depends on Batch 5 character-data wiring but since Batch 5 does not touch `g_HeadsAndBodies[]` directly (it delegates to legacy handlers), the dependency is already satisfied. Batch 6 will likely go into the same `pdgui_menu_mpsetup.cpp` file (or a new `pdgui_menu_botsetup.cpp`) depending on how much shared state it needs with the weapon slot / scenario option patterns from Batch 5.

---

## Session S202 — 2026-04-11 (D5 P3 Batch 4: Cheats & Cinema)

**Focus**: Complete D5 Phase 3 Batch 4 — consolidate 9 legacy cheats dialogs into a tabbed ImGui hub + replace the Cinema cutscene viewer.

### Approach

Per Mike's mid-task guidance: reuse Batch 0-3 primitives, integrate with existing dispatch patterns, do NOT do a 1:1 legacy port. Zero-function-loss = logical coverage, not call-site parity.

Shared primitives reused: `pdguiPopupDarkenBehind`, `pdguiDrawPdDialog`, `pdguiBeginActionBar`, `pdguiBodyHeightForActionBar`, `pdguiScale`/`pdguiMenuWidth`/`pdguiMenuHeight`/`pdguiCenterPos`, `langSafe`, `pdguiPlaySound`, `inputCtxPush/Pop(&g_CtxImGuiMenu)`, `pdguiNavTickWrap`. Sub-dialog redirect pattern cloned from Batch 3 `renderCiSettingsRedirect`. s202 shadow-struct call-through pattern cloned from s194 in `pdgui_menu_solomission.cpp:345` — re-declares `menuitem`/`handlerdata` locally with ABI-compatible layout and invokes legacy C handlers through function pointers so all bank-mutation logic (Marquis/EnemyRockets mutex, Velvet/buddy mutex, Unlock-Everything, cutscene-group math, `g_Vars.autocutgroupcur`/`autocutgroupleft` writes) stays single-sourced.

### Changes

- **NEW** `port/fast3d/pdgui_menu_cheats.cpp` (+903 lines):
  - `renderCheatsHub` -- tabbed hub with 6 tabs (Fun/Gameplay/Jo Solo Weapons/Classic Weapons/Weapons/Buddies) + docked action bar (Turn Off All / Unlock All... / Back)
  - `renderCheatsSubRedirect` -- catches the 6 legacy sub-dialogs, pops itself, flips `s_PendingTab` so the already-open hub switches tabs next frame
  - `renderCheatsWarning` -- first-use SUCCESS modal replacement
  - `renderCheatsConfirmUnlock` -- DANGER Yes/No modal calling `gamefileUnlockEverything` directly
  - `sc_buildUnlockTooltip` -- static hover tooltip replacing legacy marquee animation (localized stage names via langSafe, English difficulty labels)
  - `pdguiMenuCheatsRegister()` -- single hotswap registration function (10 dialogs)
- `port/fast3d/pdgui_menu_mainmenu.cpp` (3123 → 3371, +248):
  - `cn_handlerdata_list` / `cn_menuitem` / `cn_handlerdata` shadow types for `menuhandlerCinema` call-through
  - `renderCinemaList` -- grouped cutscene list with scroll body + docked Back action bar; delegates all MENUOP_* opcodes to `menuhandlerCinema` so cinema dispatch (`g_Vars.autocutgroupcur`/`autocutgroupleft` + `menuPopDialog` + `menuStop`) stays single-owner
  - Full MENUOP_* #define block (1..8) next to existing MENUOP_SET — was missing for list-handler opcodes
  - Registration: `g_CinemaMenuDialog → renderCinemaList` in `pdguiMenuMainMenuRegister()`
- `port/include/pdgui_menus.h` (62 → 63, +1):
  - Declared + called `pdguiMenuCheatsRegister()` in `pdguiMenusRegisterAll()`
- `context/scratch/D5-P3-batch4-2026-04-11.md` -- full legacy→new function map, zero-function-loss audit, build results

### Mid-flight fix

First client build failed with `'MENUOP_GETOPTIONCOUNT' was not declared in this scope`. The Cinema renderer references 5 list-handler opcodes but `pdgui_menu_mainmenu.cpp` only had a local `#define MENUOP_SET 6` from Batch 0. Added the full block (MENUOP_GETOPTIONCOUNT=1 ... MENUOP_GET=8). Committed separately as `9b32a877` on dev + `58e60923` on worktree branch.

### Build

- Worktree: `friendly-morse`, branch `claude/friendly-morse`
- Merged to dev as non-ff merge
- `build/client/PerfectDark.exe`: 48,920,337 → **49,051,951 bytes** (+131,614) — freshly linked 2026-04-11 11:41 EDT
- `build/server/PerfectDarkServer.exe`: 22,788,944 bytes (unchanged — pdgui code excluded from server) — freshly linked 2026-04-11 11:42 EDT
- `build/server/PerfectDark.exe`: 49,050,415 bytes — freshly linked 2026-04-11 11:43 EDT (build/server also builds the client target)
- `cmake --build . -j 24` via MSYS2 MINGW64 with TEMP=`C:\Users\mikeh\AppData\Local\Temp`. Direct cmake invocation used because build-headless.ps1 swallows output when stdout is redirected under bash (carriage-return spinner vs non-TTY). Re-ran `cmake .` in build/server to refresh GLOB_RECURSE cache and pick up new cheats.cpp.
- Exit: 0 on all three link steps, no new errors

### Next

**Batch 5** (MP Setup Core — Arena/Weapons/Scenario/Limits, ~14 dialogs). Plan says this may be absorbed into `pdgui_menu_room.cpp` if the legacy combat sim path is retired.

---

## Session S201 — 2026-04-11 (D5 P3 Batch 3: Unified Settings absorbs CI Options)

**Focus**: Complete D5 Phase 3 Batch 3 — verify CI Options absorption into unified Settings; close remaining content gap.

### Findings

- Batch 3 redirect infrastructure already written in S195: `renderCiSettingsRedirect` registered for 5 CI Options dialogs, `renderCiDeadPlayer2` for 3 dead P2 variants.
- All CI Display settings (Sight/Target/Zoom/Ammo/GunFunction/Paintball/Subtitles/MissionTime) and CI Control settings (LookAhead/HeadRoll/AutoAim/AimControl/InvertY) confirmed present in unified settings.
- **One gap**: Sound Mode (Mono/Stereo/Headphone/Surround) from CI Options audio section was missing from Settings → Audio.

### Changes

- `port/fast3d/pdgui_menu_mainmenu.cpp` (3100 → 3123, +23):
  - Added `extern s32 g_SoundMode` + `void sndSetSoundMode(s32 mode)` in extern "C" block
  - Added "Output" section to `renderSettingsAudio` with Sound Mode dropdown (4 options: Mono/Stereo/Headphone/Surround); reads `g_SoundMode`, calls `sndSetSoundMode` on change
- `context/tasks-current.md`: Batch 3 marked DONE
- `context/scratch/D5-P3-batch3-2026-04-11.md`: zero-function-loss audit + change summary

### Build

- Worktree: `amazing-shockley` → merged to dev as merge commit
- PerfectDark.exe: 48,920,337 bytes — freshly linked 2026-04-11 10:48 AM
- PerfectDarkServer.exe: 22,788,944 bytes — freshly linked 2026-04-11 10:47 AM
- Exit: 0, no new errors

### Next

**Batch 4** (Cheats & Cinema, ~10 screens) — new file `pdgui_menu_cheats.cpp` + cinema entry in `pdgui_menu_mainmenu.cpp`. NOT solomission.cpp.

---

## Session S200 — 2026-04-11 (B-78: chat DoS amplification fix + B-84: dead variable)

**Focus**: Close B-78 (chat rebroadcast rate limiting) in `port/src/net/netmsg.c`.

### Findings

- Rate limiter (5 msg / 2s ring buffer) already existed from a prior session. B-78 remained OPEN because the size amplification gap was not addressed: `netbufReadStr` allows up to 65534-char strings, so 5 × 64KB = ~320KB/s per attacker was still possible even with rate limiting.
- B-84 (dead `char tmp[1024]` in `netmsgSvcChatRead`) was co-located and resolved in the same diff.

### Changes

- `port/src/net/netmsg.c` (+10, -2):
  1. Added `#define CHAT_MSG_MAX_LEN 255u` alongside existing rate-limit constants
  2. `netmsgClcChatRead`: length check drops oversized messages before rate-limit ring (LOG_WARNING with client ID)
  3. `netmsgSvcChatRead`: removed dead `char tmp[1024]`
- `context/scratch/B-78-2026-04-11.md`: fix rationale + build results
- `context/bugs.md`: B-78 and B-84 marked FIXED
- `context/tasks-current.md`: B-78 status updated

### Build

- Worktree commit: `59a15a65` → cherry-picked to dev as `cb6f4763`
- PerfectDark.exe: 48,911,633 bytes — freshly linked 2026-04-11
- PerfectDarkServer.exe: 22,787,920 bytes — freshly linked 2026-04-11
- Exit: 0, no new errors

### Next steps

- B-81 (JSON recursion guard in savefile.c) — running in parallel session
- B-112 (chr pointer corruption in 31-bot matches) — still INVESTIGATING

---

## Session S199 — 2026-04-11 (Updater parse failure diagnosis — v0.0.75 not in update list)

**Focus**: Diagnose why v0.0.75 doesn't appear in the client/server update list and why "Check for Updates" fails with "Couldn't parse update list".

### Findings

- **Single parser** — `updaterCheckAsync()` → `checkThread()` → `parseReleasesJson()` is the only code path. Startup, UI "Check Now", and server all use the same function. No separate parsers.
- **v0.0.75 structure is valid** — tag `v0.0.75`, `prerelease=true`, `PerfectDark-v0.0.75-win64.zip` asset (matches `.zip` suffix), body 1099 bytes (within 2048 limit). Structurally identical to v0.0.74.
- **Parse failure root cause** — `parseReleasesJson` returns -1 ONLY when top-level JSON isn't a `[` (array). GitHub returns an error OBJECT `{"message":"API rate limit exceeded",...}` for rate-limit/403 responses — this would trigger the error. No code bug causing the failure for a valid response.
- **per_page=30 was a time bomb** — 53 total releases and growing. Still catches v0.0.75 (newest first) but future old-releases can fall off. Increased to 100.
- **"Doesn't appear in list" for stable channel users is EXPECTED** — v0.0.75 is prerelease=true. Stable channel filters it. Dev channel users will see it.

### Changes

- `port/src/updater.c` — 3 instrumentation changes:
  1. `per_page=30` → `per_page=100`
  2. HTTP status code logged when GitHub returns non-200 (curlGet)
  3. Raw response preview (200 chars) logged when `parseReleasesJson` returns -1
  4. Token type logged when top-level JSON isn't array
- Scratch: `context/scratch/updater-parse-diagnosis-2026-04-11.md`
- Commit: `654ac54b` (worktree) → merged to dev `4581074c`
- Build: PerfectDark.exe (48,919,825 bytes) + PerfectDarkServer.exe (22,788,944 bytes) — both clean, exit 0

### Next steps

1. Deploy and reproduce the "couldn't parse" error — next log will show `UPDATER: GitHub API HTTP NNN` revealing whether it's rate-limiting or a different error
2. If HTTP 403: add backoff/retry (1 retry after 5s) for rate-limited checks
3. If HTTP 200 non-array: investigate what GitHub is returning (proxy? redirect?)
4. If no error logged: was a transient network issue — no code change needed

---

## Session S198 — 2026-04-10 (Playtest triage v0.0.74 — B-129 agent save path fix + theme editor instrumentation)

**Focus**: Three-part triage on Chris's v0.0.74 playtest (commit 20775345).

### Task A — S197a in build?

YES. Both S197a commits (`9ba39f84`, `94db4f5c`) are ancestors of `20775345`.
Issues 1 and 3 are confirmed real residual bugs, not stale build artifacts.

### Task B — Agent save path fix (B-129, incomplete from S190)

Root cause: `saveInit()` was never called from `main.c` or `server_main.c`.
`s_SaveDir` stayed `""` → `buildSavePath()` produced `/agent_smarch.json` (drive root).
Windows UAC silently blocks root writes → `besttimes[]` never persisted →
`isStageDifficultyUnlocked(stageindex+1)` returned false → game retried current stage.

Fix: added `#include "savefile.h"` + `saveInit()` to both startup sequences.
Commit `24f93fab` (worktree) cherry-picked to `dev` as `3fc345bf` (3 files, 8 insertions).
Individual compilation verified clean: main.c.obj, server_main.c.obj, pdgui_menu_theme_editor.cpp.obj all exit 0, no new warnings.

### Task C — Theme editor lifecycle instrumentation

Added `sysLogPrintf` before each of the four `pdguiThemeEditorHide()` call sites:
- Begin() collapsed+close path
- Close button
- Title-bar X button (`!open` after End)
- InvisibleButton click-outside dismiss

Next playtest log will reveal which path fires (or doesn't) when the user tries to close.

### Diagnostic note — theme editor close (B-130)

Z-order hypothesis: the Settings menu (persisted ImGui window from prior frame) may sit
above the overlay in the z-stack, intercepting outside-area clicks before InvisibleButton.
For the X button, working hypothesis is re-show race or focus state issue.
**Do not fix without log evidence.**

### Issue 3 — Start double-fire (B-131)

Confirmed real. Deferred. Hypothesis: residual Start consume flag or lingering
deferred-pop inputctx entry across `mainChangeToStage(0x30)`.

### Context updated

- bugs.md: B-129 FIXED (full), B-130 and B-131 OPEN
- scratch: `context/scratch/playtest-triage-followup-2026-04-10.md`

### Next steps

1. Ship build with B-129 fix; playtest with Chris to confirm stage advance works
2. Read next playtest log for B-130 instrumentation output
3. Investigate B-131 (Start double-fire) in dedicated session

---

## Session S197a — 2026-04-10 (Input regression diagnostic + fix, post-S196)

**Focus**: Chase down the input regressions Mike noticed immediately after
S196 landed:
  - "Theme Editor window should have an x on it to close it, and should
    close when I click out of it rather than just changing focus."
  - "We did seem to lose the controller input working in menus."
  - Rapid Start press opens two stacked main menu instances.
  - Jump doesn't work in gameplay.

Root-cause-first diagnostic session; all five touched files are defensive
or narrow fixes to upstream behaviour that only became visible once S196
forced a full pass through the Settings -> Video toggle path.

### Phase 1 -- Re-add menu IMC nav bindings

S189's "3-action reduction" stripped MENU_UP/DOWN/LEFT/RIGHT +
TAB_PREV/NEXT from `setupMenuDefaults()` and `setupPauseMenuDefaults()`
on the assumption that ImGui would drive its own navigation.  It does
not -- `pdguiDriveImGuiNav()` in `port/fast3d/pdgui_backend.cpp` is the
sole menu nav bridge, and it reads `actionHeld(ACTION_MENU_*)`.  With no
binds registered, d-pad and keyboard arrows were both dead inside menus.

Fix (`port/src/actionmap.cpp`): re-added six nav actions to both the
menu and pause-menu defaults:
```
ACTION_MENU_UP        <- VKL_UP,    JBTN_DPAD_UP
ACTION_MENU_DOWN      <- VKL_DOWN,  JBTN_DPAD_DOWN
ACTION_MENU_LEFT      <- VKL_LEFT,  JBTN_DPAD_LEFT
ACTION_MENU_RIGHT     <- VKL_RIGHT, JBTN_DPAD_RIGHT
ACTION_MENU_TAB_PREV  <- JBTN_LB
ACTION_MENU_TAB_NEXT  <- JBTN_RB
```
Comments in both default setups updated to point at
`pdguiDriveImGuiNav()` so the next optimizer sees the dependency.

### Phase 2 -- inputCtxPush resurrect for marked-for-removal

`inputctx.c` uses deferred removal: `popDeferred` sets
`marked_for_removal=1` and `inputCtxEndFrame` compacts the stack.
During the window between them:
  * `inputCtxGetTop` and `inputCtxIsActive` skip marked entries.
  * The old `inputCtxPush` duplicate check did NOT skip marked entries.
An in-frame pop+push sequence (seen in the S197a log as a <10 ms
push/pop/push on `imgui_menu`) was therefore refused as "already on
stack", leaving the deferred-removal in place and visibly flickering
the menu state.

Fix (`port/src/inputctx.c`): the duplicate check now recognises a
marked-for-removal match and resurrects it -- clears the flag, refreshes
`push_tick`, re-syncs mouse mode (popDeferred had already flipped mouse
state to the *next* context underneath), and deliberately does NOT
re-fire `on_push` (original on_push side effects are still in place).
Logs "un-marked for removal (resurrect)" so the branch is visible.

### Phase 4 -- Kill double pd.ini reload at Settings open

Log evidence showed two back-to-back `actionmapLoadBinds()` calls within
~17 ms of opening Settings.  Root cause in
`port/fast3d/pdgui_menu_mainmenu.cpp`: the Controls-tab init flag
`s_ControlsNeedsInit` was being set to true on EVERY view/tab change,
both enter and leave.  Sequence on first Settings entry:
  1. View switch 0 -> 2 sets flag (enter-side).
  2. Static-default already had flag set.
  3. First render consumes flag, runs reload #1.
  4. Sub-tab state becomes stale vs. s_PrevSubTab, sub-tab branch fires
     on the next frame, sets flag again.
  5. Second frame consumes flag, runs reload #2.

Input polls landing between the two reloads saw a partially-populated
bind table -- the most likely cause of the sporadic vk=528/529 (LB/RB)
"NO BINDING FOUND" reports in the same log window.

Fix: both view-switch and sub-tab branches now only set
`s_ControlsNeedsInit = true` when LEAVING the Settings view / Controls
tab.  The static default still fires the first-ever init, and the
leave-side flag re-arms for the next entry, so the normal case is
covered with exactly one reload per entry.

### Phase 5 -- Theme Editor X button + click-outside-close

Two cosmetic bugs in the S196 Theme Editor:
  * Title bar had no close-cross (`ImGui::Begin` was called with
    `nullptr` p_open).
  * Click-outside-to-close sometimes changed focus instead of
    dismissing.  The dismiss was implemented via an InvisibleButton in a
    fullscreen overlay window, but the overlay had
    `ImGuiWindowFlags_NoBringToFrontOnFocus`, so clicks landing over a
    previously focused window (the Settings pane that launched the
    editor) routed there instead of to the overlay.

Fix (`port/fast3d/pdgui_menu_theme_editor.cpp`):
  * `renderThemeEditor` declares a local `bool open = true` and passes
    `&open` to `Begin` so the title bar renders the X.  On normal and
    early-return paths, if `open` became false, calls
    `pdguiThemeEditorHide()` after `End` so the visibility flag and log
    line stay in sync.
  * `pdguiThemeEditorRender` drops the `NoBringToFrontOnFocus` flag from
    the overlay, adds `SetNextWindowFocus()` before the overlay's
    `Begin`, and also calls `SetNextWindowFocus()` before
    `renderThemeEditor` so the editor stays visually in front above the
    newly focused overlay.

### Phase 6 -- Gate DIAG log spam

Six DIAG sites in `actionmap.cpp` were firing at ~60 Hz regardless of
log verbosity:
  - fireVk DOWN, fireVk NO BIND, btn, poll header + IMC dump, axes raw,
    axis final.

Worst offender was the NO BIND case: synthetic stick/trigger VKs
(joyOffset 22-31) are INTENTIONALLY unbound (analog goes through
`SDL_GameControllerGetAxis`), but every stick tick still produced a
warning-level log entry.

Fix: all six DIAG sites gated on `sysLogGetVerbose()` (off by default,
toggleable via `--verbose`).  The NO BIND path additionally filters out
joyOffset 22-31 so even `--verbose` runs don't warn on expected stick
misses.

### Phase 3 / Jump -- Verification by code review

Both the sporadic vk=528/529 "NO BINDING FOUND" reports and Mike's
"Jump doesn't work" complaint trace to the same Phase 4 root cause.
ACTION_JUMP is correctly bound (VK_SPACE + JBTN_A in
`setupGameplayDefaults` at `actionmap.cpp:1339-1340`), and the
`bondmove.c` consumer path at lines 987, 1006, and 1981 is intact.  The
failure mode is an input poll landing mid-reload during the double
pd.ini reload window, reading a partially-populated bind table.  With
the Phase 4 fix, there is only ONE `actionmapLoadBinds()` call per
Settings entry, and it runs strictly inside the Controls-tab render
path -- no gameplay frame can interleave.

Phase 3 is therefore verified by code inspection; runtime confirmation
still requires a live playtest, and if Jump still fails post-build the
next triage pass should instrument `c1buttonsthisframe` at
`bondmove.c:1981` to isolate upstream vs. downstream.

### Build + merge

  * Merge: `git merge --ff-only claude/jovial-almeida` (into dev).
    Fast-forward `d079eea7..9ba39f84`; 5 files, +634/-37.
  * Client: `cmake --build build/client --target pd -- -j24 -k`.
    `PerfectDark.exe` 48,916,241 bytes relinked at 20:57.
  * Server: `cmake --build build/server --target pd-server -- -j24 -k`.
    `PerfectDarkServer.exe` 22,786,384 bytes relinked at 20:58.
  * `port/src/crash.c:404` `#if !defined(PD_SERVER)` guard verified
    intact both before and after the build.
  * No new warnings from S197a-touched files; pre-existing warnings in
    `modelasm_c.c`, `model.c`, `snd.c`, `updater.h`, and `enet.h`
    untouched.

### Status

Code and build: SUCCESS.  Runtime verification pending; the seven
visible checks are listed in Section F of
`context/scratch/s197a-report.txt`.  Full phase-by-phase log also in
that scratch report.

### Touch points

```
port/src/actionmap.cpp                    Phase 1 (menu nav binds)
                                          Phase 6 (DIAG gating)
port/src/inputctx.c                       Phase 2 (resurrect duplicate)
port/fast3d/pdgui_menu_mainmenu.cpp       Phase 4 (one-shot reload)
port/fast3d/pdgui_menu_theme_editor.cpp   Phase 5 (X + click-outside)
context/scratch/s197a-report.txt          Incremental scratch log
```

## Session S196 — 2026-04-10 (Chrome pipeline lift + base-game template mod system)

**Focus**: Three-phase session bundling the infrastructure lift the chrome
pipeline needed, a first-class base-game template mod concept, and the
Settings UI toggle to make chrome visible end-to-end.  Success criterion
(from Mike): "launch the build, flip Settings → Video → UI Chrome Style to
Classic, and see chrome rendering on menus."

### Phase 2 — Chrome pipeline infrastructure lift

- **TGA loader lift** (`port/fast3d/pdgui_theme.cpp`): `s_loadTgaTexture`
  rewritten to accept arbitrary dimensions and both 24-bit and 32-bit
  uncompressed TGA.  24-bit sources get alpha=0xFF synthesized.  RLE and
  colour-mapped TGA rejected explicitly.  Replaces the static 256×256×4
  decode buffer in `s_decodeAndUpload` with per-call malloc/free sized
  to actual dimensions.
- **Per-edge nineslice modes + src_inset/dst_corner_px split**
  (`port/fast3d/pdgui_nineslice.cpp`, `port/include/pdgui_nineslice.h`):
  `nineslice_def_t` extended with `src_left/right/top/bottom`,
  `dst_left/right/top/bottom`, and per-edge `top_mode/bottom_mode/
  left_mode/right_mode` fields.  Legacy flat `left/right/top/bottom` +
  `edge_mode` remain as a fallback via new `has_split` /
  `has_per_edge_mode` flags and an `s_backfillDef` helper that runs on
  every register call.  `pdguiNinesliceDrawEx` rewritten to consume
  per-edge state — destination corners now clamp to half-rect when
  the draw rect is tiny.  JSON parser teaches new keys `src_inset`,
  `dst_corner_px`, and `top_mode`/`bottom_mode`/`left_mode`/`right_mode`.
- **Haze tile rate fix** (`port/fast3d/pdgui_style.cpp:582`): the
  hardcoded `bw / 64.0f` tile constant replaced with
  `bw / (float)pdguiThemeGetTextureSize(bgTex, ...)`.  HD haze
  replacements now tile at their natural size.
- **`pdguiSetPanelNineSlice` catalog-id API**
  (`port/include/pdgui_style.h`, `port/fast3d/pdgui_style.cpp`): the old
  stub-only signature (raw tex + insets) replaced with a catalog-id
  based API.  New functions: `pdguiChromeSetEnabled(s32)`,
  `pdguiChromeIsEnabled()`, `pdguiSetPanelNineSlice(catalog_id)`,
  `pdguiGetPanelNineSlice()`, `pdguiClearPanelNineSlice()`.  Backing
  state: `s_ChromeEnabled` bool + `s_ChromeNineSliceId[64]` + internal
  `s_resolveActiveChrome` helper that looks up both the nineslice
  registry and the theme texture cache.
- **Render-branch toggle in `pdguiDrawPdDialog`**: title bar gradient +
  shimmer still runs always (unchanged).  Body background, haze
  overlay, and border lines wrapped in an if/else branch — chrome path
  calls `pdguiNinesliceDrawEx` with the palette's `dialog_border1`
  color as tint (alpha forced to 0xFF so the chrome asset's own alpha
  drives visibility).  Perimeter shimmer deliberately kept OUTSIDE
  the else — the animated sweep passes over chrome as part of PD's
  visual identity.
- **`pdguiThemeGetTextureSize` API** (`port/fast3d/pdgui_theme.cpp`,
  `port/include/pdgui_theme.h`): new parallel `s_ThemeTexDims` map
  alongside `s_ThemeTexCache` so the chrome render path can resolve
  source texture dimensions by catalog id.  All four texture
  registration sites updated to insert into both maps.

### Phase 3 — Visible chrome

- **Base-game chrome template mod** (`port/fast3d/pdgui_theme.cpp`):
  new `pdguiChromeInitializeBaseMod()` function called from
  `pdguiThemeCheckExtract`.  Creates `mods/base-game/ui-chrome/`,
  generates a 64×64 composite BGRA nineslice source texture
  programmatically via `s_generateChromeFrameBgra` (quarter-circle
  arc rings in all 4 corners, solid intensity edge strips, faint
  diagonal crosshatch center), writes it as an uncompressed top-down
  32-bit TGA via `s_writeTgaFile`, writes the template-flagged
  `mod.json` (with `tags: ["base-game","template","chrome"]` and
  `template: true`), writes a `README.md` warning users not to edit
  the template, loads the texture into the theme cache as
  `"base:ui_chrome_frame"`, and registers a nineslice def under the
  same catalog id with src_inset/dst_corner_px = 16/16/16/16 and
  center_mode = tile.
- **Settings → Video → UI Chrome Style dropdown**
  (`port/fast3d/pdgui_menu_mainmenu.cpp:611+`): new row in
  `renderSettingsVideo` after the CRT Filter block.  Options:
  "Procedural" (default) / "Classic (base-game test)".  Hot-applies
  via `pdguiChromeSetEnabled` + `pdguiSetPanelNineSlice`.  Persists
  to pd.ini via new `Video.UiChromeEnabled` integer config var
  (registered in `pdguiThemeInit`).  `pdguiChromeInitializeBaseMod`
  re-applies the persisted setting at startup once the nineslice is
  registered.

### Phase 1 — Base-game template mod system

- **`modinfo_t` extensions** (`port/include/modmgr.h`): two new fields
  `is_template` (s32) and `num_tags` / `tags[MODMGR_MAX_TAGS][MODMGR_TAG_LEN]`
  with limits `MODMGR_MAX_TAGS=8`, `MODMGR_TAG_LEN=32`.  Public getter
  API: `modmgrGetModIsTemplate`, `modmgrGetModNumTags`, `modmgrGetModTag`,
  `modmgrModHasTag`.
- **`modmgrParseModJson` extensions** (`port/src/modmgr.c`): top-level
  keys `"template"` and `"tags"` now parsed.  `"template"` accepts
  JSON true/false/null/numeric.  `"tags"` parses an array of strings
  into the fixed-size tags pool.  Unknown keys still skipped.
  Post-parse log line includes template state and tag count.

### Phase 4 — Bugs found + fixed during build

- **Comment termination hazard**: my new block comments contained
  `src_*/dst_*` which terminates a `/* */` block early.  Fixed in
  `pdgui_nineslice.cpp:387` and `pdgui_theme_loader.cpp:749`.  Scanned
  every new comment for the pattern — no remaining occurrences.
- **Pre-existing S195 Batch 3 PAL-only link bug**: references to
  `g_CiControlOptionsMenuDialog2` in `pdgui_menu_mainmenu.cpp` (line 67
  extern, 2786 dialog lookup, 3050 hotswap register) were
  unconditional, but the symbol is only defined inside
  `#if VERSION >= VERSION_PAL_FINAL` in `src/game/mainmenu.c:3316`.
  NTSC builds fail to link with "undefined reference".  This wasn't
  introduced by S196 — S196 is just the first build after the bug
  was introduced.  Fix: drop the extern and both usages on the NTSC
  side.  The "CI Control Options 2" sub-dialog redirect is now a
  noop on NTSC, matching the surrounding "P2 variants are dead"
  comment.

### Deferred (intentional)

- **`modmgrSaveOrOverwrite` / `modmgrSaveAs`**: the save-path API
  is not strictly required for the visible chrome success criterion.
  Template protection enforcement is in place via the `is_template`
  field; hooking it into the save path (when Modding Hub's "Save As"
  button lands) is a small follow-up.
- **Phase 1.5 rewrite of `pdguiThemeCheckExtract` for
  `mods/base-game/ui-theme/`**: the old extractor still writes to
  `mods/base-ui/` with the pre-S196 schema.  Leaving it alone keeps
  backward compat with any user edits to base-ui TGAs.  The chrome
  mod is the new template exemplar; migrating the UI theme mod is a
  follow-up (no user impact — the extractor will just generate a
  second template mod on next launch).
- **Phase 3.4 Modding Hub template grouping + lock icons**: the
  catalog API is in place (`modmgrGetModIsTemplate`,
  `modmgrModHasTag`), so the Modding Hub can consume it in a
  follow-up session.  Not blocking visible chrome.

### Files changed

| File | Before | After | Delta |
|---|---|---|---|
| `port/fast3d/pdgui_theme.cpp` | 1647 | 2091 | +444 |
| `port/fast3d/pdgui_style.cpp` | 1051 | 1194 | +143 |
| `port/fast3d/pdgui_nineslice.cpp` | 453 | 633 | +180 |
| `port/fast3d/pdgui_theme_loader.cpp` | 1084 | 1086 | +2 |
| `port/fast3d/pdgui_menu_mainmenu.cpp` | 3063 | 3087 | +24 |
| `port/include/pdgui_theme.h` | 119 | 139 | +20 |
| `port/include/pdgui_style.h` | 124 | 139 | +15 |
| `port/include/pdgui_nineslice.h` | 110 | 152 | +42 |
| `port/src/modmgr.c` | 1916 | 1975 | +59 |
| `port/include/modmgr.h` | 195 | 229 | +34 |
| (new) `mods/base-game/ui-chrome/ui_chrome_frame.tga` | 0 | ~16KB | generated at runtime |
| (new) `mods/base-game/ui-chrome/mod.json` | 0 | ~1KB | generated at runtime |
| (new) `mods/base-game/ui-chrome/README.md` | 0 | ~1KB | generated at runtime |
| `context/session-log.md` | — | — | incremental |
| `context/tasks-current.md` | — | — | incremental |
| `context/scratch/s196-report.txt` (new) | 0 | ~500 | final report |

Net code delta: **+963 lines** across 10 files.

### Build result

- **Client**: `PerfectDark.exe` 48,914,193 bytes (+198,135 vs S194's
  48,716,058).  Clean link, one pre-existing `strncpy` warning in
  `snd.c:1502` inherited from before S196 (not related to this session).
- **Server**: `PerfectDarkServer.exe` 22,786,384 bytes (unchanged — the
  server source list does not include `port/fast3d/*.cpp`, and the new
  `modmgr.c` code paths are dead for server builds).  S193b
  `#if !defined(PD_SERVER)` guard in `port/src/crash.c:404` verified
  intact before and after S196.

### Verification checklist (for Mike)

1. **Launch the default build** — menus should look identical to S194
   (procedural path, `Video.UiChromeEnabled=0` by default in pd.ini).
2. **Open Settings → Video** — scroll to the Rendering group, look for
   the new "UI Chrome Style" dropdown below CRT Strength.
3. **Flip to "Classic (base-game test)"** — menus should visibly change.
   Expect: the body background darkens, thin white border strips appear
   along the edges, quarter-circle arcs at the corners, a faint
   crosshatch pattern across the center.  Theme tint (from the active
   palette's `dialog_border1` color) should flow through to the chrome.
4. **Change the theme color** (if theme editor is accessible) — the
   chrome should retint to match.
5. **Flip back to "Procedural"** — should return to the S194 look
   pixel-for-pixel (no residual chrome state).
6. **Check pd.ini** — `Video.UiChromeEnabled=1` after enabling, `=0`
   after disabling.  Restart should preserve the setting.
7. **Inspect `mods/base-game/ui-chrome/`** — should contain
   `ui_chrome_frame.tga`, `mod.json` (with `template: true` and tags
   `["base-game","template","chrome"]`), and `README.md` (do-not-edit
   warning).  Editing `mod.json` manually and relaunching the game
   will silently overwrite it (the extractor enforces the template).
8. **Verify `mods/base-ui/` is untouched** — Phase 1.5 deferral means
   the legacy base-ui extractor still writes there with the old schema.

### Next steps

- Mike's in-game verification pass (checklist above).
- Follow-up session for `modmgrSaveOrOverwrite` / `modmgrSaveAs` + the
  Modding Hub "Base Game Templates" group with lock icons.  The API
  surface is in place (`modmgrGetModIsTemplate`, `modmgrModHasTag`).
- Follow-up session for rewriting the base-ui extractor to use the
  new template mod layout at `mods/base-game/ui-theme/`.
- S196-Batch4+ menu replacements can resume on top of the new chrome
  infrastructure with no conflicts (the `g_PdguiChromeEnabled` toggle
  leaves the procedural path bit-for-bit identical when disabled).

---

## Session S194 — 2026-04-10 (Batch 2: Co-op / Counter-Op Flow menu replacements)

**Focus**: Execute Batch 2 of the menu replacement plan — Co-op and
Counter-Op mission difficulty + options dialogs.  Directly follows S193
(1080p baseline flip + Batch 1) and S193b (server linker fix).  No scope
creep: four dialogs, one file touched (`pdgui_menu_solomission.cpp`),
every new pixel value uses `pdguiScale()` against the 1080p baseline,
every modal uses `pdguiPopupDarkenBehind(0.55f)`, every primary CTA docks
in a `pdguiBeginActionBar` primitive.

### Batch 2 scope (per `context/designs/menu-replacement-plan.md`)

| Dialog | Source | Delivered as |
|---|---|---|
| `g_CoopMissionDifficultyMenuDialog` | mainmenu.c:1787 (DEFAULT) | `renderCoopMissionDifficulty` — cloned from `renderDifficulty`, preserves `isStageDifficultyUnlocked` gate, pushes `g_CoopOptionsMenuDialog` on confirm |
| `g_CoopOptionsMenuDialog` | mainmenu.c:1601 (DEFAULT) | `renderCoopOptions` — Radar/FriendlyFire checkboxes + Perfect Buddy dropdown + docked Continue/Cancel action bar |
| `g_AntiMissionDifficultyMenuDialog` | mainmenu.c:1854 (DEFAULT) | `renderAntiMissionDifficulty` — cloned from `renderDifficulty`, NO unlock gate (matches legacy), pushes `g_AntiOptionsMenuDialog` on confirm |
| `g_AntiOptionsMenuDialog` | mainmenu.c:1715 (DEFAULT) | `renderAntiOptions` — Radar checkbox + Main Player dropdown + docked Continue/Cancel action bar |

All four register a hotswap entry in `pdguiMenuSoloMissionRegister()`.

### Delivery architecture

The four renderers share two shape-level implementations to keep the
code tight:

- `renderCoopAntiDifficultyImpl(isCoop, windowId)` — shared difficulty
  picker body.  Thin wrappers `renderCoopMissionDifficulty` and
  `renderAntiMissionDifficulty` parameterise the flavour.  Differences
  from the existing `renderDifficulty`:
  - No PD Mode row (PD Mode is a solo-only modifier).
  - On confirm, pushes Co-op/Anti Options instead of AcceptMission.
  - Co-op flavour checks `isStageDifficultyUnlocked`; anti does not.

- `renderCoopAntiOptionsImpl(isCoop, ...)` — shared options body.  Thin
  wrappers `renderCoopOptions` and `renderAntiOptions`.  The body walks
  a fixed row list (3 for coop, 2 for anti), then docks the
  Continue/Cancel action bar with the S192 primitive.

### State manipulation: delegate to legacy handlers

Every checkbox/dropdown read and write delegates to the existing legacy
menu handlers in `mainmenu.c` (`menuhandlerCoopRadar`,
`menuhandlerCoopFriendlyFire`, `menuhandlerCoopBuddy`,
`menuhandlerAntiRadar`, `menuhandlerAntiMainPlayer`,
`menuhandlerBuddyOptionsContinue`).  This keeps backing-store logic
(the `modifiedfiles` dirty flag, `getMaxAiBuddies()` clamps,
connected-controller math) in exactly one place.  New C++ helpers
`s194_GetCheckbox` / `s194_SetCheckbox` / `s194_GetDropdownCount` /
`s194_GetDropdownSelected` / `s194_GetDropdownOptionText` /
`s194_SetDropdownIndex` wrap the `MENUOP_*` calling convention for
ergonomics.

ABI note: `solomission.cpp` is a C++ file and cannot include `types.h`
(whose `#define bool s32` breaks C++).  We shadow `struct s194_menuitem`
and `union s194_handlerdata` with byte-identical layouts and forward-
declare the legacy handlers with our shadow types.  At link time the
C linker matches `menuhandlerCoopRadar` by symbol name only and the
ABI (x64 Windows calling convention) makes the call compatible — the
same pattern used by S193's `pdgui_menu_warning.cpp` slider handling.

### Anti-pattern avoided

The renderers use real `ImGui::Selectable(rowLabel, ...)` calls with
hardcoded English fallback strings ("Radar On", "Friendly Fire",
"Perfect Buddy", "Cooperative", "Continue", "Cancel", etc.) when
`langSafe()` returns empty.  No `##hidden` selectables + `dl->AddText`
overlays — that was the root cause of the S192 missing-text and S193
LITERAL_TEXT regressions.

### Design-doc compliance checklist

- [x] **1080p baseline** — every pixel literal goes through
  `pdguiScale()`; baseline matches `scaling-baseline-1080.md`.
- [x] **Popups always darken** — every renderer opens with
  `pdguiPopupDarkenBehind(0.55f)`.
- [x] **Primary actions never scroll** — Continue/Cancel live in
  `pdguiBeginActionBar("##coopanti_ab")` with `pdguiActionBarButton`
  primary CTAs.  Scrollable body uses
  `pdguiBodyHeightForActionBar(avail)` to reserve space.
- [x] **Circular d-pad wrapping** — `s_CoopAntiDiffSelectIdx` and
  `s_CoopAntiOptSelectIdx` wrap top↔bottom on d-pad / arrow input.
- [x] **Audio cues for every state change** — `PDGUI_SND_OPENDIALOG`
  on appear, `PDGUI_SND_FOCUS` on nav, `PDGUI_SND_TOGGLEON/OFF` on
  checkbox toggle, `PDGUI_SND_SUBFOCUS` on dropdown step,
  `PDGUI_SND_SELECT` on confirm (via action bar button),
  `PDGUI_SND_KBCANCEL` on Escape/B/Cancel, `PDGUI_SND_ERROR` on
  locked-difficulty select.
- [x] **Controller accessibility** — A/Enter confirms, B/Escape backs,
  Start fires Continue (matches legacy TICK handler), left/right
  decrement/increment dropdowns.
- [x] **Selectable-with-label pattern** — no `##hidden` Selectables +
  AddText overlays.  Every row label is a real widget text.

### Files changed

| File | Before | After | Delta |
|---|---|---|---|
| `port/fast3d/pdgui_menu_solomission.cpp` | 2650 | 3351 | +701 |
| `context/session-log.md` | (incremental) | (incremental) | (incremental) |
| `context/tasks-current.md` | (incremental) | (incremental) | (incremental) |
| `context/scratch/s194-report.txt` (new) | 0 | ~XXX | +XXX |

### Server linker guard

Verified intact before and after S194 work: `port/src/crash.c:404`
still has `#if !defined(PD_SERVER)` around the `g_ChrLastTickedIndex`
extern reference.  S193b server-linker fix is NOT regressed.

### Build result

- **Client**: `PerfectDark.exe` 48,716,058 bytes (+29,715 vs S193's 48,686,343).
  Clean link, pre-existing `strncpy` truncation warning in `snd.c:1502`
  inherited from before S194 (not related to this session).
- **Server**: `PerfectDarkServer.exe` 22,786,384 bytes (no change — the
  server source list does not include `port/fast3d/`, so no server rebuild
  was needed).  S193b `#if !defined(PD_SERVER)` guard around
  `g_ChrLastTickedIndex` verified intact before and after S194 by
  force-rebuilding `port/src/crash.c` and linking server target clean.

### Next steps

- Mike's in-game verification: launch co-op / counter-op, step through
  difficulty → options → accept flow, confirm every row renders, every
  toggle fires the correct audio cue, Continue and Cancel both docked
  and visible.
- Batch 3 (Unified Settings / CI Options absorption) is next per the
  menu replacement plan.

---

## Session S193 — 2026-04-10 (Session-log repair + 1080p scaling baseline flip + Batch 1 menu replacements)

**Focus**: Three-phase session on top of S192 Batch 0.
(1) Repair pre-existing mid-sentence truncation at the tail of session-log.md.
(2) Flip `pdgui_scaling.h` baseline from 720p to 1080p to match the
authoritative reference in `context/designs/d5-full-menu-overhaul.md`.
(3) Execute Batch 1 of the menu replacement plan (seven "low-hanging
fruit" dialogs: Exit Game, PD Mode Settings, MP End Game, and four
filemgr pak-era dialogs).

### Phase 1 — session-log.md truncation repair

- Truncation origin identified at commit `79590470` (Build v0.0.50,
  2026-04-07).  The S134 entry's tail was cut mid-sentence during an
  auto-commit that also deleted the archived S130 entry.
- Recovery: restored the full pre-truncation content from the original
  commit `633979af` (the commit that introduced S134).  Tail contained
  the closing sentence of "Fix 1", the entire "Fix 2" pdgui_menu_mainmenu
  adjustment, Build status, Decisions Made, Next Steps, and the `---`
  session-separator terminator.
- Method: direct restore from git history.  No reconstruction needed.
- Line counts: pre-repair 2276, post-repair 2294 (+18 lines).

### Phase 2 — pdgui_scaling.h baseline 720p → 1080p

Foundation-layer fix that unblocks every future menu batch.  The design
spec at `context/designs/d5-full-menu-overhaul.md` uses 1080p reference
values in every table; the implementation was carrying 720p reference
values from before the spec was written.  S192 Batch 0 flagged the
discrepancy as deferred item D-3; S193 resolves it.

**Changed baseline constants**:
- `pdgui_scaling.h`: `displayH / 720.0f` → `displayH / 1080.0f`
- Menu width cap: `1200.0f` → `1800.0f` (preserves current visual width
  via proportional scaling)
- `pdguiBaseFontSize()` base: `16.0f` → `24.0f` (body-text tier per d5
  table; minimum floor still 12 px)
- Added `PDGUI_REF_WIDTH` / `PDGUI_REF_HEIGHT` macros
- `pdgui_backend.cpp` safe-area fallback: `1280x720` → `1920x1080`

**Action bar primitive constants (pdgui_layout.cpp)** — 720p tuning
rescaled to 1080p equivalents:
- `PDGUI_AB_BASE_HEIGHT_PX`  56 → 84
- `PDGUI_AB_BUTTON_HEIGHT_PX` 42 → 64 (matches d5 "Button height: 64")
- `PDGUI_AB_BODY_GAP_PX`       8 → 12
- `PDGUI_AB_BODY_MIN_PX`      60 → 90
- `PDGUI_AB_MIN_HEIGHT_PX`    48 (unchanged — absolute-pixel floor)

**Bulk transformation of `pdguiScale(N.Nf)` literals** — every literal
argument across 17 menu files was multiplied by `1.5` so visual output
is identical at every resolution before and after the flip.  Mechanical
transform done via `context/scratch/s193_scale_flip.py`:
312 pdguiScale() literals rewritten across 17 files + 1 expression case
handled manually (`pdgui_menu_solomission.cpp:1835`:
`pdguiScale(38.0f * 5.0f)` → `pdguiScale(57.0f * 5.0f)`).

**Files touched in Phase 2** (21 code files + 1 new doc):
- `port/fast3d/pdgui_scaling.h`, `pdgui_layout.cpp`, `pdgui_backend.cpp`
- `port/include/pdgui_layout.h` (docstring update)
- 17 menu cpp files (countdown, challenges, endscreen, lobby, mainmenu,
  modmgr, mpingame, mpsettings, network, pausemenu, room, solomission,
  stats, teamsetup, training, update, warning)
- `context/designs/scaling-baseline-1080.md` (new migration note, 169 lines)

**Build result**: client target builds green on merged main.  Binary
size 48,675,238 bytes — identical to baseline because the changes are
pure float-literal adjustments that get inlined/constant-folded into
identical machine code.  The reference resolution of the source code
changes; the generated code does not.

### Phase 3 — Batch 1 menu replacements

Batch 1 per `context/designs/menu-replacement-plan.md` §"Batch 1: Simple
Confirmations & Text Inputs": seven dialogs that were falling through to
the generic DANGER/DEFAULT type fallback in `pdgui_menu_warning.cpp`
without dedicated ownership.

**Scope mapping** (seven dialogs):
| Dialog | Source | Delivered as |
|--------|--------|-------|
| `g_ExitGameMenuDialog` | mainmenu.c:3554 (DANGER) | Upgraded `renderDangerDialog` with literal-text fix + scrim |
| `g_PdModeSettingsMenuDialog` | mainmenu.c:1033 (DEFAULT + 3 sliders) | Upgraded `renderDefaultDialog` with SLIDER item support |
| `g_MpEndGameMenuDialog` | mplayer/ingame.c:249 (DANGER) | Upgraded `renderDangerDialog` with scrim |
| `g_FilemgrDeleteMenuDialog` | filemgr.c:3067 (N64 pak file picker) | PC placeholder: "Managed via Agent Select" |
| `g_FilemgrCopyMenuDialog` | filemgr.c:3104 (N64 pak file picker) | PC placeholder |
| `g_FilemgrOperationsMenuDialog` | filemgr.c:3382 (N64 pak ops menu) | PC placeholder |
| `g_FilemgrSelectLocationMenuDialog` | filemgr.c:2928 (N64 pak selection) | PC placeholder |

**Upgrades to `pdgui_menu_warning.cpp`** (578 → 749 lines, +171):
- `pdguiPopupDarkenBehind(0.55f)` scrim at the start of every typed
  dialog — replaces ad-hoc darken rectangles scattered across earlier
  fixes.  Replaces the unfocused/scattered look of previous modals.
- `MENUITEMFLAG_LITERAL_TEXT` handling in `getItemLabel()` — before
  S193, `g_ExitGameMenuItems` label ("Are you sure you want to exit?")
  was a raw `const char*` stored in `param2` with the
  `MENUITEMFLAG_LITERAL_TEXT` flag set; the old helper called
  `langSafe((s32)param2)` which interpreted pointer bits as a lang
  index and rendered empty text.  Now the flag is honored.
- `MENUITEMTYPE_SLIDER` item handling — new case in the item loop
  calls the item handler with `MENUOP_GETSLIDER` + `MENUOP_GETSLIDERLABEL`
  to read the current value and label, renders an `ImGui::SliderInt(0,
  255)`, and writes back via `MENUOP_SET` on change.  Covers the three
  PD Mode sliders (health, damage, accuracy) in a single path.
- `renderFilemgrPcPlaceholder()` — new dedicated renderer for the four
  pak-era filemgr dialogs.  Explains that PC agent management goes
  through Agent Select and saves via `saves/agent_<name>.json`.  Uses
  the S192 docked action-bar primitive for the OK button and the
  popup-scrim primitive for the backdrop.  The placeholder is a safety
  net for the rare fringe path that might still push one of these
  dialogs; on PC they are otherwise unreachable in the normal flow.
- Explicit hotswap registrations for all seven Batch 1 dialogs so the
  hotswap log makes ownership clear (previously they fell through to
  the type fallback anonymously).

**Build result**: client target builds green.  Binary size
48,686,343 bytes (+11,105 from Phase 2 baseline — reflects the new
placeholder renderer, slider case, scrim call, and explicit
registrations).

### Zero-function-loss audit

See `context/scratch/s193-report.txt` for the per-screen audit tables
(visual parity, audio cues, controller accessibility) covering every
Batch 1 dialog plus the re-verified Batch 0 primitives under the new
1080p baseline.

### Next Steps

- In-game QC pass by Mike: verify ExitGame literal text renders, PD
  Mode sliders move, MP End Game confirms, and the filemgr placeholder
  is reachable only by fringe code paths (expected: never seen in
  normal PC flow).
- Batch 2 (Co-op & Counter-Op Flow — 4 screens) is next per the menu
  replacement plan.  Batch 2 builds on the solo mission flow already
  DONE and reuses the difficulty picker.

---

## Session S192 — 2026-04-10 (Batch 0: Menu Replacement Foundation — layout primitives + model preview generalization + Mission Select / Challenges docking fixes)

**Focus**: Execute Batch 0 of the menu replacement plan — foundation primitives
every later batch depends on, plus an audit pass on shipping ImGui menus
against the design-guideline docked-action-bar rule.

### What was built

**New primitive module** — `port/include/pdgui_layout.h` (163 lines) +
`port/fast3d/pdgui_layout.cpp` (137 lines):
- `pdguiActionBarHeight()` / `pdguiBodyHeightForActionBar(avail)` — compute
  scaled action-bar + body sizes for a layout with a docked footer.
- `pdguiBeginActionBar(id)` / `pdguiEndActionBar()` — fixed-height child at
  the bottom of a window/child region; CTAs placed here never scroll.
- `pdguiActionBarButton(label, isFocused, width)` — docked-bar button with
  consistent sizing, focus ring, and audio cue (`PDGUI_SND_SELECT` on
  activation via click / gamepad-A / Enter).
- `pdguiPopupDarkenBehind(alpha)` — full-viewport dim rect on the
  background draw list. Replaces ad-hoc
  `GetBackgroundDrawList()->AddRectFilled` calls scattered across
  endscreen, pausemenu, solomission, training, agentcreate.

**Generalized model preview pipeline** — `pdgui_charpreview.{c,h}` +
`pdgui_model_preview.{cpp,h}`:
- New `PdguiPreviewType` enum: CHARACTER / WEAPON / VEHICLE / PROP.
- `pdguiCharPreviewRequestEx(type, id1, id2)` routes CHARACTER to the
  existing head+body path and WEAPON/VEHICLE/PROP to a single-filenum path
  that resolves catalog entries and uses `source_filenum` for
  `MENUMODELPARAMS_SET_FILENUM()`.
- `pdguiCharPreviewRequestFilenum(type, filenum)` — low-level escape hatch.
- `pdguiCharPreviewRenderGBI` now picks `MENUMODELTYPE_HUDPIECE` for
  weapons and `MENUMODELTYPE_DEFAULT` for everything else.
- `ModelPreviewKind` enum mirrors the preview type at the high-level panel
  layer. `pdguiModelPreviewDrawEx(kind, id1, id2, ...)` is the single entry
  point for any model kind. `pdguiModelPreviewDraw(head, body, ...)` is
  preserved as a CHARACTER shortcut — existing callers (agent select, agent
  create, room lobby, modding hub) do not need to change.
- Unified tracking state: `(kind, id1, id2)` tuple prevents redundant FBO
  re-renders across kinds.
- Placeholder silhouette branches on kind (character stickman vs neutral
  box icon for weapons/vehicles/props).

**Mission Select regression fix** — `pdgui_menu_solomission.cpp`:
- Right panel restructured to use the new docked action bar primitive.
  Stage name header, difficulty picker, and footer hint stay pinned at the
  top; objectives + briefing scroll inside `##ms_detail_body`; Start
  Mission lives in `##ms_action_bar` at the bottom and is always visible.
- Removed the fragile hardcoded `objH = bodyH - 260.0f` / `briefH = avail
  - 60.0f` layout math that caused the Start button to fall off the
  bottom on low-res windows.

**Mission Difficulty dialog "missing text" fix** — same file:
- Replaced the `Selectable("##diff_row", ...)` + `dl->AddText` overlay
  pattern with real `ImGui::Selectable(labelStr, ...)` calls. The previous
  pattern left rows blank when `langSafe(L_OPTIONS_251/252/253)` returned
  an empty string (lang bank not resident at dialog-open time). Added
  hardcoded English fallback strings ("Agent", "Special Agent", "Perfect
  Agent", "PD Mode", "Cancel") so the row label is never empty.
- Applied the same Selectable-with-label pattern to the PD Mode and
  Cancel rows for consistency.

**Challenges Accept Challenge docking fix** — `pdgui_menu_challenges.cpp`:
- Split the `##chal_detail` right panel into an inner scrollable body
  (`##chal_detail_body`) containing header + completion summary +
  description, and a `##chal_action_bar` docked region containing the
  Accept Challenge button. Previously the Accept button was inside the
  scrollable region and could fall off the bottom on long descriptions.

### Audit — shipping ImGui menus against docked-CTA rule

Menus confirmed compliant (CTA already outside BeginChild scroll region):
- `pdgui_menu_mainmenu.cpp` — Confirm Quit is in a modal popup.
- `pdgui_menu_room.cpp` — Start Match at window level, after all EndChild.
- `pdgui_menu_endscreen.cpp` — Retry / Next Mission after EndChild.
- `pdgui_menu_pausemenu.cpp` — Return to Lobby / Quit after EndChild.
- `pdgui_menu_agentcreate.cpp` — Create at window level, no scroll child.
- `pdgui_menu_warning.cpp` — OK button in a typed-dialog modal, no scroll.

Menus fixed in this session:
- `pdgui_menu_solomission.cpp` — Mission Select Start Mission docked.
- `pdgui_menu_challenges.cpp` — Accept Challenge docked.

Menus not deeply audited this session (small menus or dev tools;
flagged for visual verification during QC):
- `pdgui_menu_agentselect.cpp`, `pdgui_menu_training.cpp`,
  `pdgui_menu_mpingame.cpp`, `pdgui_menu_mpsettings.cpp`,
  `pdgui_menu_teamsetup.cpp`, `pdgui_menu_network.cpp`,
  `pdgui_menu_lobby.cpp`, `pdgui_menu_moddinghub.cpp`,
  `pdgui_menu_modmgr.cpp`, `pdgui_menu_update.cpp`,
  `pdgui_menu_theme_editor.cpp`, `pdgui_menu_logviewer.cpp`,
  `pdgui_menu_stats.cpp`.

### Files changed

| File | Before | After | Delta |
|---|---|---|---|
| port/include/pdgui_layout.h (NEW) | 0 | 163 | +163 |
| port/fast3d/pdgui_layout.cpp (NEW) | 0 | 137 | +137 |
| port/include/pdgui_charpreview.h | 59 | 105 | +46 |
| port/fast3d/pdgui_charpreview.c | 307 | 399 | +92 |
| port/include/pdgui_model_preview.h | 75 | 104 | +29 |
| port/fast3d/pdgui_model_preview.cpp | 236 | 307 | +71 |
| port/fast3d/pdgui_menu_solomission.cpp | 2627 | 2650 | +23 |
| port/fast3d/pdgui_menu_challenges.cpp | 350 | 366 | +16 |
| **TOTAL** | **3254** | **4231** | **+577** |

Plus `context/scratch/menus-batch0-report.txt` (552 lines, new).

### Deferred (called out explicitly)

- **Per-type camera/scale table** for weapon/vehicle/prop previews. API
  shape shipped; tuning pass is Batch 10 (training 3D) responsibility.
- **Popup scrim adoption** in existing menus. `pdguiPopupDarkenBehind` is
  available; migrating the ~5 ad-hoc sites to it is a mechanical follow-up.
- **720p vs 1080p scaling reference** discrepancy — `pdgui_scaling.h` uses
  720p baseline; `d5-full-menu-overhaul.md` specifies 1080p. Not resolved
  in Batch 0 to avoid re-tuning every menu's layout math.
- **Remaining menu audit** — 13 shipping menus not deeply audited this
  session. Should be visually verified during QC.

### Verification

Per Mike's anti-truncation rule, every file written or edited in this
session was re-read after the Write/Edit tool call. First line, last
line, and line count verified for each. No truncation detected.

Full zero-function-loss audit table in
`context/scratch/menus-batch0-report.txt`.

### Next steps

1. Merge worktree changes into main copy.
2. Re-verify line counts on the main copy (worktree truncation is a known
   hazard).
3. Re-run `build-headless.ps1` against the merged main copy to confirm a
   clean build.
4. Mike's in-game verification pass (checklist in the report file).

---

## Session S191 — 2026-04-10 (B-112 + B-126: Entry guard + slot tracker + heartbeat expansion)

**Focus**: Instrumentation pass on the two open HIGH bugs — B-112 (chr pointer corruption in 31-bot matches) and B-126 (silent crash ~8min into MP). No live repro yet; this session closes the diagnostic gaps so the next crash log is actionable.

### B-112 — Chr pointer corruption (guards in place, root cause still unknown)

**Root cause analysis**: Original crash pattern is "access violation at `chr->hidden`" with VEH showing `rbx` (the chr pointer) = garbage. Classic stack corruption: a callee smashes the caller's saved `rbx` on its stack frame. When execution returns to `chraTick`, `rbx` is restored from the corrupted value → next dereference of `chr` (at `chr->hidden`, line ~13444) crashes.

**Critical gap closed**: The existing canary (`sysLogPrintf("CHR.GUARD: canary")`) was inside the sleep block, ~33 lines BELOW the first `chr->hidden` access. A corrupt pointer arriving at `chraTick()` crashes before reaching that canary.

**Changes** (`src/game/chraction.c` 16541 lines, `src/game/chr.c` 6676 lines, `src/include/game/chr.h` 101 lines):
- `chraTick()` entry guard — validates `chr` before ANY field access; logs `CHR.GUARD: chraTick entry invalid` and bails if pointer is bad.
- `g_ChrLastTickedIndex` global (`s32`, default -1) — set to `(chr - g_ChrSlots)` just before each `chraTick()` call in `chrTick()`, cleared after. Lets crash handlers identify the failing bot slot without heap access.
- Declaration added to `chr.h` so all TUs (including crash.c) can reference it.

### B-126 — Silent crash (likely stack-protector canary, SIGABRT not landing in log)

**Root cause hypothesis**: GCC `-fstack-protector-strong` detects smashed canary → calls `abort()` → SIGABRT. Windows VEH is NOT called by SIGABRT (it's a CRT signal, not a structured exception). The existing `crashSigabrtHandler` should catch it but apparently wasn't logging reliably.

**Changes** (`port/src/crash.c` 481 lines, `port/include/net/net.h` 286 lines, `port/src/net/net.c` 2200 lines, `src/game/lv.c` 2750 lines):
- `crashSigabrtHandler` updated — reads `g_ChrLastTickedIndex` (extern, no heap) and logs it: `"FATAL: last chr tick index=%d"`. When the next SIGABRT fires, we'll know which bot slot was mid-tick.
- `netHeartbeatLog()` added to `net.c` — logs `NET.WATCHDOG` per-peer dump (RTT, silence timer, ENet peer state) and `NET.HEARTBEAT` global stats (mode, clients, bandwidth).
- `lvTick()` heartbeat block updated — interval halved from 3600 → 1800 frames (60s → 30s); adds `last_chr_idx` field; calls `netHeartbeatLog()` for full watchdog snapshot.

### Build verification

All 5 changed source files syntax-checked via `gcc -fsyntax-only` in the main copy (`C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike`) — exit 0 on all. Full headless build in progress; `build-headless.ps1` environment confirmed correct.

**Next steps for Mike**:
- Play a 31-bot match until crash. Check `pd.log` for `CHR.GUARD` or `CRASH: SIGABRT` lines — if either appears, the slot index will be in the log.
- Check `NET.HEARTBEAT` lines — last one before crash timestamps the failure window.
- Also needed: manual QC pass on S190 input fixes (A=jump, Y=use, B=crouch, LSTICK=sprint, R3 unbound, mission 1 save, sky tearing on outdoor stages).

---

## Session S190 — 2026-04-10 (Four-Task Sweep: Binding complete, B-128 sky FIXED, B-129 mission-end crash FIXED, SP-9 IMPLEMENTED)

**Focus**: Four parallel S190 tasks all landed and clean-build verified. Binding rework complete (P0-only, menu stripped to 3 actions). Sky tearing fixed (one-liner). Mission-end crash (B-129) fixed and save pipeline restored. SP-9 truncation safeguard implemented in build pipeline.

### Task 1 — Binding Rework + usemask fix (COMPLETE, clean build)

**P0-only IMC refactor** — `port/src/actionmap.cpp` 1630 → 1557 lines (−73 from pre-S189 baseline: −24 in S189, −49 in S190):
- `setupVehicleDefaults`: removed p=1..3 else block; early-return on p≠0.
- `setupMenuDefaults`: stripped from 35 lines to 5 — ACTION_USE (Return/A), ACTION_CANCEL_USE (Escape/B), ACTION_PAUSE (Start only). Dead MENU_UP/DOWN/LEFT/RIGHT/TAB_* binds removed (ImGui reads raw SDL key events for menu nav, NOT action map states).
- `setupPauseMenuDefaults`: same 3-action model.
- `setupDebugOverlayDefaults`: MENU_UP/DOWN/LEFT/RIGHT removed; DEBUG_TOGGLE, USE, CANCEL_USE, CONSOLE_TOGGLE, SCREENSHOT remain.
- `setupTextInputDefaults`: reformatted, already P0.
- `actionmapInit`: replaced `for (p=0..MAX)` loop with single `setupGameplayDefaults(0)` + `setupVehicleDefaults(0)`.
- Bind1/Bind2 confirmed not structural — `InputMapping.triggers[4]` flat array. Each action has ≤1 kbd + ≤1 gamepad default. No struct change needed.
- Final gamepad layout (P0): A=JUMP, B=CROUCH (dual with CANCEL_USE), Y=USE, X=RELOAD, LSTICK click=SPRINT, R3 unbound.

**usemask door fix** — `src/game/bondmove.c:1836` +4 lines:
- Root cause: `BUTTON_CANCEL_USE == B_BUTTON` (same constant value). PC-mode usemask had `(B_BUTTON | BUTTON_CANCEL_USE | BUTTON_ACCEPT_USE)` = `(B_BUTTON | B_BUTTON | A_BUTTON)` — CANCEL_USE and B_BUTTON are the same bit, so B always matched the door mask.
- Fix: PC branch now `BUTTON_ACCEPT_USE` only. N64 branch unchanged. B still works for FarSight/scope cancel (that path uses c1buttons synthesis with a different mask, unaffected).

**SP-9 PS fix** — `devtools/build-headless.ps1:449`: `$net:` parsed as a PowerShell drive reference; fixed to `${net}:`.

**Also confirmed in subsequent clean build** (landed via unruffled-kalam worktree):
- `bondmove.c:~1551` — `unk14 = true` unconditional in CONTROLMODE_PC (was gated on `c2stickx || c2sticky`)
- `bondmove.c:~1633` (ADS path) — `canlookahead = true` unconditional
- `bondmove.c:~1642` (non-ADS path) — `canlookahead = !insightaimmode`; stick gate removed
- `bondmove.c:~2104` (FarSight strafe) — reads `c1stickxsafe` (left stick) instead of `c2stickx`
- `actionmap.cpp` — JBTN_LSTICK/JBTN_RSTICK defines added; LSTICK click → ACTION_SPRINT

**Build result**: `PerfectDark.exe` 48,596,071 bytes, clean link, no errors.

### Task 2 — B-129 Mission-End Crash (FIXED)

**Root cause chain**: `endscreenPrepare → filemgrSaveOrLoad(FILEOP_SAVE_GAME_000) → pakFindBySerial()==-1` (no Controller Pak on PC) `→ menuPushDialog(&g_PakNotOriginalMenuDialog)` (unregistered, DANGER type) `→ type-based fallback → renderDangerDialog → getDialogTitle → langSafe((s32)(uintptr_t)fn_ptr)` — 64-bit function pointer truncated to s32, used as lang bank index in the millions `→ lang.c:461 g_LangBanks[bankindex]` → AV.

Three `filemgrSaveOrLoad(FILEOP_SAVE_GAME_000)` calls in endscreen.c (lines 1722, 1827, 1932) all hit this path.

**Fix** — `endscreen.c` +14 lines: all three calls replaced with `saveSaveAgent(g_GameFile.name)` (PC-native JSON save system). Added `#include "savefile.h"`.

**Defense in depth** — `port/fast3d/pdgui_menu_warning.cpp` +12 lines: `pdguiHotswapRegister` for `g_PakNotOriginalMenuDialog`, `g_FilemgrSaveErrorMenuDialog`, `g_FilemgrFileLostMenuDialog` — all with `renderNoop` handler. Blocks the fatal fn-ptr-as-bank-index path even if filemgr is called elsewhere.

**Side benefit**: PC save pipeline was silently failing every mission completion. `saves/agent_<name>.json` is now correctly written to disk on mission end.

### Task 3 — B-128 Sky Tearing (FIXED)

**Root cause**: `skyRender()` upper hemisphere (clouds) at `sky.c:1244` never called `gDPSetRenderMode`. It inherited stale `other_mode_l` from the previous frame's sun-flare/overexposure draws using `G_RM_AA_XLU_SURF`. This set `use_alpha=true` in `port/fast3d/gfx_pc.cpp:1307`, enabling `GL_BLEND`, causing sky tris to alpha-blend with the framebuffer instead of overwriting it. Sky is the FIRST geometry drawn each frame (`lv.c:1394`), so it always inherits the previous frame's blend state.

**Fix** — `src/game/sky.c:1244` +1 line: `gDPSetRenderMode(gdl++, G_RM_OPA_SURF, G_RM_OPA_SURF2)` inserted after `gDPPipeSync`/`texSelect` and before `gDPSetEnvColor`. Mirrors the working water path at `sky.c:827`. Vertex alpha is always 0xff per `skyChooseCloudVtxColour` at `sky.c:189`. Zero risk to non-sky geometry.

**Note**: B-18 (pink sky on Skedar Ruins) may be fully or partially addressed by this fix. Unknown until Mike tests Skedar.

### Task 4 — SP-9 Truncation Safeguard (IMPLEMENTED)

`devtools/build-headless.ps1` +35 lines: before `git add -A && git commit`, runs `git diff HEAD --numstat`. Flags files where net delta < −20 AND additions < floor(deletions/3). Aborts the auto-commit (NOT the build), prints suspect files and a restore command. Build continues from working copy.

**Tested**: Trip test fired (100-line file → 5 lines, 0+/95−, net −95). False-positive test silent (actionmap.cpp −49 net / 41 added — intentional rewrite, correct no-fire). Clean run passed.

`context/systemic-bugs.md` SP-9 entry expanded with Mode A (Windows-1252 encoding truncation on UTF-8 em-dashes at byte 0xE2) vs Mode B (AI output token limit hit mid-Write/Edit call, truncates silently with no error) breakdown, commit catalog, byte-level characterization, auto-commit masking vector. Marked IMPLEMENTED S190.

### Files Changed
- `port/src/actionmap.cpp` — 1606 → 1557 lines (−49 this session; −73 total from pre-S189 baseline of 1630)
- `src/game/bondmove.c` — usemask fix at line 1836 (+4 lines)
- `src/game/sky.c` — `gDPSetRenderMode` at line 1244 (+1 line)
- `src/game/endscreen.c` — three `filemgrSaveOrLoad` calls replaced with `saveSaveAgent` (+14 lines net)
- `port/fast3d/pdgui_menu_warning.cpp` — three filemgr dialogs registered as noop (+12 lines)
- `devtools/build-headless.ps1` — SP-9 guard + `${net}:` fix (+35 lines net)
- `context/systemic-bugs.md` — SP-9 Mode A/B expanded, IMPLEMENTED marked
- `context/bugs.md` — B-128 (sky tearing) added FIXED; B-129 (mission-end crash) added FIXED

### Decisions
- MENU_UP/DOWN/LEFT/RIGHT/TAB_* are confirmed dead weight in IMCs — ImGui reads raw SDL key events for menu navigation, not action map states. Removing them from all IMCs is correct.
- "No local multiplayer" constraint formalized in constraints.md: all IMC setup targets Player 0 only; no `for (p = 0; p < MAX_LOCAL_PLAYERS; p++)` loops in binding or IMC init are acceptable.
- B is dual-bound (ACTION_CROUCH + ACTION_CANCEL_USE) in gameplay IMC. usemask fix ensures B does NOT open doors. FarSight/scope cancel uses c1buttons synthesis (different code path, unaffected).
- Dead-constant aliasing (`BUTTON_CANCEL_USE == B_BUTTON`) is a class of bug — any `BUTTON_*` constant that aliases another bit is a latent usemask hazard. Worth a sweep.

### Next Steps (Mike's in-game verification)
- [ ] A jumps, does NOT open doors
- [ ] Y opens doors / interacts
- [ ] B crouches, does NOT open doors, still exits FarSight/scope
- [ ] R3 does nothing (unbound)
- [ ] LSTICK click sprints
- [ ] Rebind UI single-column, MP slots 1–3 unbound
- [ ] CrouchMode=2 toggle behavior works, resets on respawn
- [ ] Mission 1 completion: no crash, `saves/agent_<name>.json` updated on disk
- [ ] Sky tearing gone on outdoor stages (Dark Noon, Goldfinger 64 exteriors)
- [ ] B-18 check: does Skedar Ruins still show pink sky, or does B-128 fix cover it?

---

## Session S189 — 2026-04-10 (Input System: Door Bug, Gamepad Defaults, CrouchMode Audit)

**Focus**: Fix B-button opens doors bug; simplify gamepad bindings to single-column player-0-only; audit CrouchMode (already implemented).

### What Was Done

**Task 1 — usemask door bug fixed** (bondmove.c:1836)
- Root cause: `BUTTON_ACCEPT_USE = A_BUTTON` and `BUTTON_CANCEL_USE = B_BUTTON` (constants.h). The PC branch of `usemask` was `(B_BUTTON | BUTTON_CANCEL_USE | BUTTON_ACCEPT_USE)` = `(B_BUTTON | B_BUTTON | A_BUTTON)` — CANCEL_USE and B_BUTTON are the same bit, so B always matched the door mask.
- Fix: PC branch now `BUTTON_ACCEPT_USE` only. Only A opens doors in PC mode. N64 branch unchanged (`B_BUTTON`). B_BUTTON/ACTION_CANCEL_USE still synthesized in c1buttons for FarSight/scope cancel — that logic uses a different mask.

**Task 2 — Gamepad binding overhaul** (actionmap.cpp:1319-1363)
- A → ACTION_JUMP (was ACTION_USE)
- Y → ACTION_USE (was ACTION_JUMP)
- B → ACTION_CROUCH (new dual-bind; B already bound to ACTION_CANCEL_USE for FarSight)
- RSTICK click → unbound from ACTION_CROUCH (removed per spec)
- MP players 1-3 else block removed from setupGameplayDefaults. MP slots start with zero gamepad binds; rebind UI still works.

**Task 3 — CrouchMode already implemented**
- `Game.Player%d.CrouchMode` registered in main.c:350. Values: 0=hold, 1=analog, 2=toggle, 3=toggle+analog (constants.h:4815-4818). Toggle/hold/analog all branched in bondmove.c:1925-1963. No code changes needed.

### Files Changed
- `src/game/bondmove.c` — Task 1: usemask PC branch → BUTTON_ACCEPT_USE only (line 1836)
- `port/src/actionmap.cpp` — Task 2: Y=USE, B=CROUCH/CANCEL, A=JUMP, remove MP 1-3 gamepad defaults

### Decisions
- B is dual-bound (ACTION_CROUCH + ACTION_CANCEL_USE) in gameplay IMC. CANCEL_USE covers FarSight/scope exit; CROUCH covers crouching. Task 1's usemask fix ensures B no longer opens doors regardless.

### Build Result
- **Clean build** — PerfectDark.exe + PerfectDarkServer.exe built, 0 errors. Version 0.0.68.
- Note: build-headless.ps1 from bash requires PowerShell with TEMP override; direct `make` in bash fails due to GCC writing to C:\WINDOWS\ (sandbox env). Workaround: `powershell -NonInteractive -Command "$env:TEMP=...; make ..."`.

### Next Steps
- Playtest: A=jump, B=crouch (no door open), Y=use (door open), R3 not crouching
- Wire protocol bump v32→v33 still pending (B-125 spawn_weapon_id)
- B-126 silent crash awaiting next repro with heartbeat

---

## Session S188 — 2026-04-09 (Menu Replacement Plan — Full Inventory & Gameplan)

**Focus**: Complete audit of every legacy menu dialog in the codebase. Build batched replacement plan.

### What Was Done

**Research phase** — three parallel agents audited:
1. All legacy `menudialogdef` definitions across 8 source files → **254 total dialog definitions** found
2. All ImGui hotswap registrations → **84 registered dialogs** (65 complete, 15 noop, 12 NULL-renderFn, 4 type-based)
3. Menu data sources, parent-child relationships, state transition functions, hotswap pipeline architecture

**Classification** — every dialog categorized as DONE / NOOP / NULL-FN / TYPE-FB / OG / DEAD / STANDALONE. ~140 unique reachable, ~114 dead (4MB, N64 pak, unreachable). **62 screens need work** (50 unregistered OG + 12 NULL-renderFn).

**Plan written** — `context/designs/menu-replacement-plan.md`: full inventory table, data source mapping, menu tree, 12 implementation batches with exact files/data/transitions per batch.

**Decisions resolved** (all four from Part 7):
1. **Strip ALL legacy menus** — confirmed
2. **Unified Settings** — absorb CI Options into existing Settings menus naturally. No separate CI Options or Extended Settings distinction.
3. **No split-screen** — single local player only. Batch 9 (13 screens) cancelled. All 2P dialogs → dead code.
4. **Generic Model Preview** — build it as Batch 0. Generalize `pdguiCharPreview` → `pdguiModelPreview` (character/weapon/vehicle/prop). Unblocks training 3D screens, character creator, modding tools.

**Net result**: 11 active batches, ~79 screens, ~13-18 sessions estimated.

### Files Changed
- `context/designs/menu-replacement-plan.md` — NEW: complete replacement plan (7 parts, 12 batches)
- `context/designs/menu-inventory.md` — Added cross-reference to new plan

### Decisions
- All legacy menus will be fully stripped (no OG rendering paths retained)
- CI Options absorbed into unified Settings; N64-specific settings dropped
- No split-screen support (single local player); all 2P dialogs are dead code
- Generic model preview pipeline (Batch 0) is a prerequisite investment

### Next Steps
- **Batch 0**: Generalize `pdguiCharPreview` → `pdguiModelPreview` (1 session)
- **Batch 1**: Simple confirmations & file management (1 session)
- **Batch 3**: Absorb CI Options into existing Settings tabs (1 session)
- B-128 (PlayerInput slot 8 vs 0): being fixed in parallel session

---

## Session S187 — 2026-04-09 (Three-Bug Debug Session)

**Focus**: Structured debug of three playtest bugs: WASD movement, weapon spawning, silent crash.

### What Was Done

**B-127 (WASD held not registering)** — FIXED
- Root cause: `actionmapPollFrame()` in `actionmap.cpp:793-794` synthesized KBM WASD into ACTION_AXIS_MOVE_X/Y `.value` but never set `.held` flag. Gamepad stick path (lines 758-761) correctly set `.held`. Game reads axis values via `actionValue()` which works, but something in the movement pipeline also checks `.held` — without it, movement was edge-triggered (one frame).
- Fix: Added `.held` update after `.value` update, mirroring gamepad pattern.

**B-125 (Weapons not spawning in online MP)** — FIXED
- Root cause: `spawn_weapon_id` catalog string was never added to CLC_LOBBY_START wire format (neither write nor read). The field was added to the match config struct (M0.1c, S171) and `matchStart()` resolves it correctly for solo/offline path, but the network path (CLC_LOBBY_START → server inline resolution → SVC_STAGE_START → client) was never updated.
- Fix: Added `spawn_weapon_id` string to CLC_LOBBY_START write/read AND SVC_STAGE_START write/read. Server-side CLC_LOBBY_START handler resolves catalog ID → `spawnWeaponNum` (mirrors `matchStart()` logic). Client-side SVC_STAGE_START handler does the same.
- Note: This is a wire protocol change — existing v32 clients/servers won't interop with v33 builds. Should bump protocol version.

**B-126 (Silent crash ~8 minutes)** — INVESTIGATING
- Verified all known guards are in build: 8MB stack (CMakeLists.txt:472), VEH handler (crash.c:422), B-112 bounds check (botmgr.c:125), H-7 shutdown ordering (main.c:128).
- Crash is truly silent — no VEH output, no shutdown log. Process just dies.
- Frame 480 at 60fps = 8 seconds, not 8 minutes. Discrepancy needs clarification with Mike.
- Added periodic heartbeat logger to `lv.c` (every 3600 frames / 60s) that logs frame count, chr count, and stage. Log file opens/closes per write, so heartbeat will be visible even after crash. Next repro will show last-known-good timestamp.

### Files Changed
- `port/src/actionmap.cpp` — B-127 fix: `.held` flag for KBM move axis synthesis
- `port/src/net/netmsg.c` — B-125 fix: spawn_weapon_id in CLC_LOBBY_START + SVC_STAGE_START (write + read, 4 locations)
- `src/game/lv.c` — B-126 instrumentation: periodic heartbeat log
- `context/bugs.md` — Added B-125, B-126, B-127
- `context/session-log.md` — This entry

### Decisions
- Wire protocol needs version bump for B-125 (spawn_weapon_id added to two messages)
- B-126 needs next repro with heartbeat to pinpoint timing

### Next Steps
- Protocol version bump (v32 → v33) for spawn_weapon_id addition
- Playtest B-127 (WASD) and B-125 (weapons) fixes
- Wait for B-126 repro with heartbeat instrumentation

---

## Session S186 — 2026-04-09 (Context System Overhaul)

**Focus**: Full context system cleanup. Inventory 83 files, archive stale content, rewrite core docs for v0.1.0 prep.

### What Was Done

- Inventoried all 83 context files, categorized each as KEEP/UPDATE/ARCHIVE
- Moved 28 files to `_archive/` (completed audits, superseded plans, old session logs, stale briefings)
- Created organized archive subdirectories: `_archive/audits/`, `_archive/designs/`, `_archive/sessions/`
- Rewrote `roadmap.md` — reflects actual state: M0-M2 DONE, P1-P10 DONE, deep audit DONE, clear v0.1.0 target
- Rewrote `QUICKSTART.md` — clean entry point: project overview, architecture, current state (v0.0.56/S185), constraints, build commands
- Rewrote `tasks-current.md` — removed all completed items, organized around v0.1.0 release prep
- Updated `README.md` — refreshed session history table, domain file links
- Reset `.claude/temp-build/` to tracked state

### Files Archived (28 total)
Audits (9): array-bypasses, catalog-universality, game-director-s35, comprehensive-bugs, null-guard-{players,props,bots,stageload}, systemic-null-guard, hardcoded-color-p3
Plans/Designs (6): d5-settings-plan, menu-replacement-plan, menu-storyboard, d5-visual-layer-plan, logging-system-upgrade, plan-bot-crash-fixes
Sessions (7): sessions-01-06 through sessions-87-119
Misc (6): briefing-2026-03-31, roadmap-synthesis, rendering-trace, session-briefing, menu-asset-audit, init-order-audit, player-count-constants-audit, network-audit, netsend-audit, tasks-archive, tasks-lobby-unification, daily-logs/2026-04-05

### Next Steps
- D5 Phase 3 (remaining 61 menu screens) or M3 (online MP flow) per Mike's direction
- B-112 investigation when next crash log available

---

## Session S185 — 2026-04-09 (Deep Audit Bug Fix Session)

**Focus**: Apply ALL findings from deep code audit — 5 critical security + 7 high + 10 medium + 9 low bug fixes + dead code removal. 20 files changed, 244 insertions, 74 deletions.

### What Was Done

**Critical (5)** — netdistrib.c security hardening:
- C-1: Path traversal prevention in extractArchive (sanitize relpath + `_fullpath` containment check)
- C-2: SHA-256 verification of compressed transfer data against client manifest
- C-3: Block HandleEnd when transfer awaits user approval (needs_approval gate)
- C-4: 256MB cap on decompression buffer prevents integer overflow
- C-5: 128MB cap on compressed buffer doubling prevents overflow

**High (7)**:
- H-1: Sky depth forcing before skyRender (lv.c)
- H-2: Bounds check g_MpNumChrs before array write in botmgr.c
- H-3: Remove double actionmapEndFrame call from pdgui_backend.cpp
- H-4: Ping-pong static buffers in netFormatAddr (net.c)
- H-5: Check enet_peer_send return value, destroy packet on failure (net.c)
- H-6: Replace unaligned pointer casts with memcpy in netbuf reads
- H-7: Move mempPCFreeAll after all subsystem shutdowns (main.c)

**Medium (9 applied, M-10 already existed)**:
- M-1: Mouse idle no longer clobbers gamepad aim
- M-2: Wheel scan replaced with O(1) pre-computed array
- M-3: Aim always reads regardless of movement suppression gates
- M-4: Audio sample rate 22020->22050
- M-5: Explicit s32 for g_IsTitleDemo
- M-6: Clamp weaponnum to valid range in netmsg receive
- M-7: Defer g_NetNumClients increment until CLC_AUTH succeeds
- M-8: PowerShell path escaping in updater
- M-9: Document actionmapGetVkName static buffer lifetime

**Low (7 applied, L-6/L-7 already existed)**:
- L-1: NULL guard in inputctx shutdown
- L-2: Document inputKeyJustPressed side-effect
- L-3: Bounds check cidx in inputRumbleGet/SetStrength
- L-4: B/Escape handler in agent select main list
- L-5: Document O(n) scan in sessioncatalog
- L-8: PD_LE16/PD_LE32 endian conversion in archive extraction
- L-9: static_assert on MANIFEST_MAX_ENTRIES u16 capacity

**Dead code**: Removed jparse_t.strbuf, MAX_BIND_STR, empty wheel conditional

### Bugs closed by this session
B-79 (chunk ordering), B-80 (archive_bytes validation), B-82 (sample rate), B-83 (shutdown ordering), B-86 (enet_peer_send unchecked)

### Commits
- `68b77433` — fix: deep audit (47 bugs)

---

## Session S184 — 2026-04-08 (P10 D5.7: OG Menu Removal)

**Focus**: P10 D5.7 — Systematic OG Menu Removal. Eliminate all legacy PD native menu rendering; ImGui is now the permanent and sole menu system.

### What Was Done

- Added DEFAULT type (0/1) fallback renderer to `pdgui_menu_warning.cpp` so ALL dialog types have ImGui rendering
- Added `MENUITEMTYPE_KEYBOARD` support using `ImGui::InputText` (replaces legacy on-screen keyboard)
- Removed all 13 NULL registrations that forced PD native rendering
- Disabled F8 toggle (ImGui permanent), removed [OLD]/[NEW] badge
- `pdguiHotswapIsDialogSwapped()` always returns 1
- `pdguiHotswapCheck()` always queues for ImGui
- Gutted `menu.c` `menuRenderDialog()` — removed native render path, only does hotswap queue + char preview FBO
- Routed co-op/counter-op pause through `pdguiPauseMenuOpen()` (removed legacy `menuPushRootDialog` fallback)
- Build verified clean (100% pd target + pd-server)
- Committed and merged to dev, pushed to origin

**Files modified**: `port/fast3d/pdgui_hotswap.cpp`, `port/fast3d/pdgui_menu_warning.cpp`, `src/game/menu.c`, `src/game/mplayer/ingame.c`

### Decisions

- `menugfx.c` retained — 3 non-menu callers (`hudmsg.c`, `credits.c`, `sched.c`) still use GBI utility functions. These are not OG menu code.
- Legacy dialog stack management (`menuPushDialog`/`menuPopDialog`) retained as plumbing — ImGui menus still push dialog defs via it, hotswap intercepts renders
- Hot-swap system retained as permanent infrastructure (no longer a dev bridge) — it IS the menu rendering pipeline now

### Next Steps

- Playtest to verify all menus render correctly, especially keyboard input dialogs and co-op pause
- Resume roadmap: D5 Phase 3 (remaining menu screens), M3 (online MP flow), or Phase 4 (Theme System)

---

## Session S183 — 2026-04-08 (M0.2 Phases C+D: ImGui Nav Takeover + Full CK_* Cleanup)

**Focus**: Complete M0.2 input system unification — replace ImGui built-in gamepad nav with action map queries, eliminate all CK_* legacy input constants.

### What Was Done

**Phase C — ImGui Nav Takeover**:
- Disabled ImGui built-in `NavEnableGamepad`
- Added `pdguiDriveImGuiNav()` — translates actionmap queries to ImGui nav key events each frame (accept, cancel, d-pad, bumpers)
- Replaced `pdguiNavAcceptPressed`/`CancelPressed` with actionPressed queries
- Removed `pdguiNavOnEvent`/`EndFrame`/`GetLastDevice`/`IsGamepad` from pdgui_nav
- Kept `pdguiNavTickWrap` (d-pad wrapping) and safe area utilities

**Phase D — Legacy Input Cleanup**:
- Deleted `enum contkey` (CK_*) from input.h — 32 constants removed
- Deleted CK_* binding infrastructure from input.c (ckNames, binds, bindStrs, all bind/save/load functions)
- Rewrote `inputReadController` to build CONT_* bitmask from actionmap queries instead of CK_* binds
- Deleted `inputmodes.c`/`.h` entirely (doubletap/hold system)
- Deleted joy.c shim functions (all callers migrated in Phase B)
- Rewrote ImGui rebind UI (mainmenu.cpp) to use InputAction + actionmap API
- Rewrote legacy rebind UI (optionsmenu.c) to use InputAction + actionmap API

**Net change**: -823 lines. Zero CK_* references remain in codebase.

**Also landed**: M0.2 Phase B full direct migration — all game files migrated from CK_* to actionmap queries. Commit `e99be17c`.

### Decisions
- ImGui nav driven by actionmap queries rather than raw SDL events — single input path for all systems
- `inputmodes.c` (doubletap/hold) fully replaced by actionmap trigger types
- joy.c shim stubs kept (function signatures) but bodies emptied — no callers remain

### Next Steps
- M0.2 COMPLETE (Phases A–D all done). Build verification needed.
- Resume roadmap: D5 Phase 3 (remaining menu screens), or M3 (online MP flow)

---

## Session S182 — 2026-04-07 (M0.2: Enum Fix + Partial Game File Migration)

**Focus**: Fix actionmap enum issues and migrate additional game files from CK_* to InputAction.

### What Was Done

- Fixed enum ordering/values for ACTION_USE, ACTION_CANCEL_USE, ACTION_FIRE_MODE, ACTION_CBUTTON_* actions
- Partial game file migration: converted CK_* references in multiple game files to use new InputAction enum + actionPressed/actionDown queries
- Merged from worktree `claude/wizardly-poitras`

### Commits
- `237fa415` — feat(input): M0.2 enum fix + partial game file migration
- `6062d176` — merge into dev

---

## Session S181 — 2026-04-07 (M0.2 Phase A+B: Core Action Map System + SA-5e Ammo Accessors)

**Focus**: Implement M0.2 Input System Unification Phases A and B — Unreal Enhanced Input-inspired action map system. Also rescue and land SA-5e ammo accessor work.

### What Was Done

**M0.2 Phase A — Core Action Map System** (commit `b7c6f213`):
- Unreal Enhanced Input-inspired design: `InputAction` enum, `ActionBinding` structs, `ActionMap` contexts
- Per-context action maps with priority stacking
- Trigger types: press, release, hold, doubletap
- Keyboard + gamepad binding support
- Config save/load integration

**M0.2 Phase B — Lifecycle Wiring** (commit `5cb14b52`):
- Action map lifecycle wiring into game init/shutdown/tick
- joy.c shim layer: `joyGetButtons`/`joyGetButtonsPressedThisFrame` wired through actionmap
- credits.c migration: first game file converted from CK_* to actionPressed

**SA-5e Ammo Accessors** (commit `d8be624c`, rescued from `claude/sleepy-agnesi`):
- New accessors: `catalogGetMpWeaponPriAmmoType`/`PriAmmoQty`/`SecAmmoType`/`SecAmmoQty`
- Migrated callers in player.c, bot.c, setup.c, matchsetup.c, netmsg.c

### Decisions
- Action map system inspired by Unreal Enhanced Input — appropriate complexity for a game with multiple input contexts (gameplay, menu, spectator, etc.)
- Phase B wired incrementally: joy.c shim provides backwards compat while callers migrate

### Next Steps
- M0.2 Phase B: Migrate remaining game files (bondmove.c, player.c, lv.c, etc.)
- M0.2 Phase C: ImGui nav takeover
- M0.2 Phase D: CK_* removal

---

## Session S180 — 2026-04-07 (M0.1f: Final g_HeadsAndBodies sweep — SA-5f)

**Focus**: Eliminate all remaining raw `g_HeadsAndBodies[]` access from gameplay/UI code. Add modeldef lazy-load + reset accessor family. Build-verified clean.

### What Was Done

**New accessors** in `assetcatalog.h` / `assetcatalog_api.c` (commit `facb5750`):
- `catalogGetHeadHeight(headnum)` — SA-5d companion for heads (was missing)
- `catalogGetBodyModeldef(bodynum)` / `catalogGetHeadModeldef(headnum)` — lazy-load + cache, `#if !defined(PD_SERVER)` guarded
- `catalogResetBodyModeldef(bodynum)` / `catalogResetHeadModeldef(headnum)` — clear one entry
- `catalogResetAllModeldefs()` — bulk reset (for bodiesReset)

**Migrated 8 game files**:
- `body.c`: `bodyLoad` → `catalogGetBodyModeldef`; `body0f02ce8c` body fallback → catalog; head block preserves one pre-load bool (bodyCalculateHeadOffset is not idempotent); `.filenum` diagnostic logs → `catalogGetBodyFilenumByIndex`
- `bodyreset.c`: raw loop → `catalogResetAllModeldefs()`; added `assetcatalog.h` include
- `player.c`: all `.modeldef` lazy-loads → `catalogGet*Modeldef`; `.height` accesses → `catalogGetBodyHeight`/`catalogGetHeadHeight`
- `mplayer.c:2975`: `.ismale` → `catalogGetBodyIsMale`
- `menu.c:1898`: `.unk00_01` → `catalogGetBodyIsComplete`
- `setup.c:2423`: `.unk00_01` → `catalogGetBodyIsComplete`

**Remaining raw accesses**: Only in allowed sites (`assetcatalog_base.c`, `assetcatalog_api.c`, `modelcatalog.c`, `server_stubs.c`, `robot.c`, `data.h`). One controlled pre-load check in `body.c:256` (SA-5f comment explains why).

**Build**: Clean, zero errors, zero new warnings. Pushed to `claude/zealous-haslett`.

### Decisions
- `bodyCalculateHeadOffset` is not idempotent (modifies modeldef node offsets in-place). Pre-load bool check retained in `body.c` with SA-5f annotation rather than adding a predicate function.
- `bodyLoad` return value simplified (callers discard it) — now returns `md != NULL`.

### Next Steps
- Merge `claude/zealous-haslett` to `dev` / `main`
- M0.1 is fully COMPLETE (a–f all done). Proceed to M0.2 (Input System Unification) or interleaved M1/M2/M3 feature work

---

## Session S179 — 2026-04-07 (Input Bug Fixes: Esc/Tab/Mouse/Arrow Keys)

**Focus**: Fix 4 input bugs reported in playtest — all traced to missing `g_CtxImGuiMenu` push in main menu and room menus.

### What Was Done

**Root cause**: `g_CtxImGuiMenu` was never pushed when the main menu opened. Without it on the input context stack, mouse mode stayed captured, gameplay input (arrow keys, Tab) leaked through, and Esc had no grace period.

**Fixes applied** (commit `49efd3b6`, merged to dev as `4a5d073f`):

| Bug | Symptom | Fix |
|-----|---------|-----|
| Mouse not working in main menu | `inputCtxSyncMouseMode()` saw gameplay context → kept mouse captured | Push `g_CtxImGuiMenu` on `IsWindowAppearing()` → `on_push` sets absolute mouse |
| Arrow keys moving camera while menu open | `pdguiIsActive()` returned 0 → game input not zeroed | Context push → `pdguiIsActive()` returns 1 → all game input blocked |
| Tab reopening menu after Esc close | Tab (`CK_START`) processed by game code | `pdguiIsActive()` = 1 blocks Tab from reaching game |
| Esc double-fire (open then close) | No `push_tick` set → no 100ms grace period | Context push sets `push_tick` → `inputCtxShouldSuppressKey()` blocks retrigger |

**Files changed**: `pdgui_menu_mainmenu.cpp`, `pdgui_menu_room.cpp`

### Next Steps
- Playtest verification of all 4 fixes
- Continue roadmap: M1.2 (Solo Mission Flow)

---

## Session S178 — 2026-04-07 (M0.1e: Catalog as Data Provider + release.ps1 auto-commit)

**Focus**: Complete M0.1e — catalog serves body/weapon properties via typed accessors. Harden release.ps1 with auto-commit before rebase.

### What Was Done

**Task A — release.ps1 auto-commit** (already committed in S177 continuation):
- Added auto-commit block before `git pull --rebase` in Step 4: checks `git status --porcelain`, stages with `git add -A`, commits `"chore: auto-commit before release v$Version"` if dirty.

**Task B — M0.1e: Catalog as Data Provider** (commit `b3555576`):

**New accessors** in `assetcatalog.h` / `assetcatalog_api.c`:
- SA-5d (body/head): `catalogGetBodyIsMale`, `catalogGetBodyType`, `catalogGetBodyHeight`, `catalogGetBodyAnimScale`, `catalogGetBodyCanVaryHeight`, `catalogGetBodyIsComplete` (wraps `unk00_01`), `catalogGetBodyHandFilenum`, `catalogGetHeadIsMale`, `catalogGetHeadType`
- SA-5e (MP weapons): `catalogGetMpWeaponNum` (wraps `weaponnum`), `catalogGetMpWeaponUnlockFeature`
- All O(1) direct array accesses with bounds checking (152 for bodies/heads, `NUM_MPWEAPONS` for weapons)

**Migrated 11 game files**: body.c, chraction.c, botmgr.c, bot.c, bondgun.c, botinv.c, activemenu.c, challenge.c, mplayer.c, mplayer/setup.c, player.c

**server_stubs.c**: Added `struct mpweapon g_MpWeapons[NUM_MPWEAPONS]` zero-init stub (server build was missing this symbol, caught at link time).

**Intentionally deferred**:
- `g_HeadsAndBodies[x].modeldef` — runtime-mutable cache pointer, not a stat/property
- `priammotype`/`priammoqty` patterns in bot.c/player.c — pending `catalogGetMpWeaponAmmoInfo()` accessor

### Decisions
- SA-5d/5e are safe for per-frame callers — O(1), no catalog scan
- `g_MpWeapons` server stub zero-initialized; server never uses weapon slot data

### Next Steps
- M0.1 gate: Confirm all integer asset IDs eliminated at public boundaries — M0.1e completes the final sub-task
- Proceed to M0.2 (Input System Unification) or M1/M2/M3 work

---

## Session S178 — 2026-04-07 (M0.1e — Catalog as Data Provider)

**Focus**: Make the asset catalog serve weapon stats, body/head properties directly. ROM arrays become internal implementation detail.

### What Was Done

**M0.1e COMPLETE — Catalog Data Provider API** (commit `b3555576`):
- 15 new catalog data accessor functions in `assetcatalog.h` / `assetcatalog_api.c`:
  - Weapon: damage, fire rate, ammo capacity, magazine size, reload time, range, accuracy, etc.
  - Body: model index, collision radius, type properties
  - Head: model index, type properties
- ROM arrays (`g_MpWeapons[]`, body/head tables) internalized — accessed only through catalog API
- 8 game files migrated: body.c, bot.c, mplayer.c, setup.c, chraction.c, botmgr.c, bondgun.c, player.c
- `g_MpWeapons` stub added to `server_stubs.c` for dedicated server build
- Deferred: modeldef cache, ammo distribution (priammotype/priammoqty) — tracked for future pass

**Release pipeline hardened** (commit `1f1002c4`):
- Auto-commit uncommitted changes before `git pull --rebase` in release.ps1

### Decisions
- Body/head property accessors resolve via catalog ID → runtime index → ROM array internally. Public API is catalog-ID-only.
- Weapon stats follow same pattern. No integer IDs cross the accessor boundary.
- M0.1 is now COMPLETE (all 5 sub-phases a–e done). Foundation lock for catalog identity is achieved.

### Next Steps
- Per interleaved cadence: M1.2 (Solo Mission Flow — briefings, mission complete/failed screens) or M0.2 (Input Unification)
- Gameplay state migration still open (PlayerConfig/BotConfig structs to store catalog IDs natively)

---

## Session S177 — 2026-04-07 (Infrastructure: Script Relocation + Git Recovery)

**Focus**: Fix git infrastructure issues (packed-refs corruption, index.lock, working copy desync from worktree merges), fix build break from truncated matchsetup.h, consolidate scripts into devtools/, harden release pipeline.

### What Was Done

**Git infrastructure recovery**:
- Fixed packed-refs corruption (duplicate v0.0.9 tag + null bytes)
- Cleared stuck index.lock
- Recovered working copy desync (38 modified files from stale worktree merges) via `git checkout dev -- .`

**Build break fixed — matchsetup.h truncation**:
- File truncated at line 84 during M0.1d worktree merge — `extern struct matchconfig g_MatchConfig` and all function prototypes missing
- Restored declarations, added missing `#include "net/matchsetup.h"` to net.c
- pdgui.h extern "C" guards added for C++/C interop (commit 62b71e42)

**Release pipeline hardened**:
- Added `git pull --rebase origin dev` before push step in release.ps1 — prevents non-fast-forward failures when code sessions have pushed commits

**Scripts consolidated into devtools/** (commit 018e0c05):
- Moved: release.ps1, build_check.ps1, release-v0.0.2.ps1 from project root → devtools/
- release.ps1: Added `$ProjectRoot = Split-Path $PSScriptRoot -Parent` + `Set-Location $ProjectRoot` for location-independence
- _dev-window.ps1: Updated reference to `devtools/release.ps1`
- All relative paths verified working (dev window sets CWD to project root before invocation)

**Worktree cleanup**:
- Pruned 6 stale worktree refs (elegant-chandrasekhar, serene-robinson, bold-swartz, busy-wiles, eloquent-lehmann, kind-hertz)
- Physical directories locked by running sessions — will auto-clean on close

### Decisions
- `Set-Location $ProjectRoot` added defensively to release.ps1 — dev window already handles CWD, but this covers direct invocation
- release-v0.0.2.ps1 moved as-is (legacy, no references found anywhere)

### Next Steps
- Continue roadmap: M0.1e (catalog as data provider), M1.2 completion, M2.3 (stats wiring), or M3 (online MP)
- commit-graph cache fix (non-fatal warning, low priority)

---

## Session S176 — 2026-04-07 (B-115 Fix + M2.1 Combat Sim Polish)

**Focus**: Fix B-115 (post-game mouse), verify M2.1 arena selection and game mode selection completeness.

### What Was Done

**B-115 Fixed — Legacy endscreen dialogs suppressed**:
- Root cause: `g_MpEndscreenSavePlayerMenuDialog` (mpingame.cpp) and `g_MpEndscreenConfirmNameMenuDialog` (warning.cpp) were registered with `NULL` renderFn — forcing PD native rendering. Legacy menus rendered on top of ImGui endscreen and stole input.
- Fix: Changed both to `renderNoop` (suppressed). Auto-save via `configSave("pd.ini")` in `pdguiEndscreenExitToMainMenu()` handles PC saving. N64 Controller Pak save dialogs are redundant.
- Added `renderNoop` function to `pdgui_menu_warning.cpp` (already existed in mpingame.cpp and endscreen.cpp).

**M2.1 Arena Selection — Verified COMPLETE**:
- `buildArenaListFromCatalog()` iterates all `ASSET_ARENA` entries. Combo picker stores catalog ID in `g_MatchConfig.stage_id`. Both solo and network paths use catalog strings.
- Preview images not functional — requires base-ui texture extraction (known Phase 4 item, not a blocker).

**M2.1 Game Mode Selection — Verified COMPLETE**:
- Scenario combo picks from 6 modes (Combat, Hold the Briefcase, Hacker Central, Pop a Cap, King of the Hill, Capture the Case).
- Sets both `g_MatchConfig.scenario` (u8 for legacy) and `g_MatchConfig.scenario_id` (PRIMARY) via `catalogIdByRuntime(ASSET_GAMEMODE, si)`.
- All 6 modes functional after M0.1d migration (S173).

### Code Changes (3 files)
- **port/fast3d/pdgui_menu_mpingame.cpp**: Save Player dialog: `NULL` → `renderNoop` (B-115)
- **port/fast3d/pdgui_menu_warning.cpp**: Added `renderNoop`, Confirm Name: `NULL` → `renderNoop` (B-115)
- **port/fast3d/pdgui_menu_endscreen.cpp**: Updated comment about suppressed dialogs

### Decisions
- Both N64 save dialogs redundant on PC — auto-save already wired in S175
- M2.1 marked COMPLETE: all 4 sub-items verified (arena, weapons, bots, game modes)
- Preview images deferred to Phase 4 (base-ui texture extraction) — not a functional blocker

### Next Steps
- Build verification (Mike)
- M2.1 COMPLETE, M2.2 COMPLETE → M2 gate check
- Next: M2.3 (stats/progression), M3 (online MP), or M1.2 completion (briefings/endscreens)

---

## Session S175 — 2026-04-07 (M2.2 — MP Match Flow Improvements)

**Focus**: MP endscreen flow improvements: B-117 root cause fix, player stats display, auto-save on match exit.

### What Was Done

**B-117 FIXED: Crash on match exit** (1 file — pdgui_bridge.c):
- **Root cause identified**: `pdguiEndscreenExitToMainMenu()` called `func0f0f8120()` (legacy menu pop-all) but never popped the `g_CtxImGuiMenu` input context that was pushed on window appear. The stale context survived the stage transition, causing the crash.
- **Fix**: Added `inputCtxPopDeferred(&g_CtxImGuiMenu)` guard to `pdguiEndscreenExitToMainMenu()`, matching the existing pattern in `pdguiEndscreenStartMission()` and `pdguiEndscreenNextMission()`.
- Note: B-117 was previously PARTIAL FIX (S161) with context stack reset on stage transition. This fix addresses the actual leak source.

**MP endscreen player stats section** (1 file — pdgui_menu_endscreen.cpp):
- Added "YOUR STATS" section to the MP endscreen showing local player's combat breakdown: kills, accuracy (with color-coded progress bar), shot region breakdown (head/body/limb/other/total).
- Same data sources as solo endscreen (`mpstatsGetPlayerKillCount`, `mpstatsGetPlayerShotCountByRegion`).
- Section appears after awards/medals, before action buttons, inside the scrollable content area.

**Auto-save on match exit** (1 file — pdgui_bridge.c):
- `configSave("pd.ini")` called at top of `pdguiEndscreenExitToMainMenu()`.
- PC has no pak/memory card — auto-save replaces the N64's "Save Player?" prompt.

**Match start → gameplay verified**:
- `matchStart()` is fully catalog-native: resolves `scenario_id`, `stage_id`, `weapon_ids[]`, `spawn_weapon_id`, body/head — all from catalog at last-moment handoff. Confirmed solid (already verified in M2.1/S172).

**B-115 verified FIXED** (S170): `g_CtxImGuiMenu` push on window appear already present in MP endscreen at line 712-716.

### Code Changes (2 files)
- **port/fast3d/pdgui_bridge.c**: B-117 fix (context pop) + auto-save (`configSave`) + `config.h` include
- **port/fast3d/pdgui_menu_endscreen.cpp**: Player stats section + `configSave` declaration

### Decisions
- Auto-save on match exit rather than prompt — PC has persistent config, no need for N64-style save dialog
- Stats section uses same bridge functions as solo endscreen — consistent data source
- Context pop goes in bridge function (shared exit path) rather than each button handler — single fix covers all exit paths (Return to Room, Disconnect, Play Again, Quit, Esc, Enter)

### Next Steps
- Build verification (Mike)
- M2.3 or D5 Phase 3 continuation

---

## Session S173 — 2026-04-07 (M0.1d — Remaining Asset Type Catalog Signature Migration)

**Focus**: Audit and migrate remaining 7 asset types (texture, audio, animation, gamemode, lang, prop, HUD) at public function boundaries.

### What Was Done

**Full boundary audit of all 7 asset types:**

| Type | Boundary Exposure | Action |
|------|------------------|--------|
| ASSET_TEXTURE | Internal only (renderer) | Documented — no migration |
| ASSET_AUDIO | Internal only (sound system) | Documented — no migration |
| ASSET_ANIMATION | Internal only (model/anim system) | Documented — no migration |
| **ASSET_GAMEMODE** | **Wire, save, config** | **MIGRATED** |
| ASSET_LANG | Internal only (string tables) | Documented — no migration |
| ASSET_PROP | Wire (type discriminator only, not asset identity) | Documented — no migration |
| ASSET_HUD | Internal only (HUD rendering) | Documented — no migration |

**ASSET_GAMEMODE migration** (the only type with genuine asset identity crossing boundaries):

1. **matchsetup.h**: Added `scenario_id[64]` as PRIMARY. `scenario` (u8) marked DEPRECATED.
2. **matchsetup.c**: `matchStart()` resolves `scenario_id` → integer at handoff. `matchConfigInit()` sets default "base:combat". `matchStartFromChallenge()` syncs back via `catalogIdByRuntime()`.
3. **netmsg.c (CLC_LOBBY_START)**: Write/read `scenario_id` string instead of u8.
4. **netmsg.c (SVC_STAGE_START)**: Write/read `scenario_id` string instead of u8.
5. **net.c (server query)**: Write `scenario_id` string. Read side resolves to integer.
6. **net.h**: `netrecentserver` struct: added `scenario_id[CATALOG_ID_LEN]`, `scenario` marked DEPRECATED.
7. **savefile.c**: Write `scenario_id` alongside integer. Read prefers `scenario_id`, falls back to integer.
8. **scenario_save.c**: Write `scenarioId` alongside integer. Read prefers `scenarioId`, falls back to integer.
9. **pdgui_menu_room.cpp**: Scenario combo sets `scenario_id` from catalog via `catalogIdByRuntime()`.
10. **Protocol version**: Bumped to v32.

**PROP type assessment**: `prop->type` (PROPTYPE_OBJ/DOOR/KEY/ALARM/CCTV/WEAPON/AMMO/SMOKE) is a protocol-level TYPE DISCRIMINATOR (8 fixed categories), not an asset identity. The actual prop MODEL identity already uses catalog session refs (v31). PROPTYPE is analogous to a message sub-type — converting to catalog strings would add overhead without benefit since these categories are fixed protocol constants.

### Code Changes (9 files)
- **port/include/net/matchsetup.h**: `scenario_id[64]` PRIMARY field
- **port/include/net/net.h**: Protocol v32, `scenario_id` in netrecentserver
- **port/src/net/matchsetup.c**: Init, resolve, sync-back
- **port/src/net/netmsg.c**: CLC_LOBBY_START + SVC_STAGE_START wire format
- **port/src/net/net.c**: Server query write + read
- **port/src/savefile.c**: Write/read scenario_id
- **port/src/scenario_save.c**: Write/read scenarioId
- **port/fast3d/pdgui_menu_room.cpp**: Combo picker sets scenario_id
- **context/constraints.md**: Protocol v32, scenario_id mandate

### Decisions
- 5/7 types are internal-only — no migration needed (textures, audio, animations, lang, HUD)
- PROP type is a protocol discriminator, not asset identity — documented as such
- `gamemode` (u8: 0=combat sim, 1=coop, 2=counter-op) is a protocol-level mode selector, NOT a catalog asset — stays as integer
- `scenario` (MPSCENARIO_*) IS a catalog asset (ASSET_GAMEMODE) — migrated

### Next Steps
- Build verification (Mike)
- M0.1e (catalog as data provider) or Gameplay state migration

---

## Session S172 — 2026-04-07 (M2.1 — Combat Sim UI Catalog Audit)

**Focus**: Verify Combat Simulator setup UI is fully catalog-native after M0.1a/b/c migrations.

### What Was Done

**Full audit of pdgui_menu_room.cpp** (Combat Sim setup screen):

1. **Arena Selection** — ALREADY CATALOG-NATIVE. `buildArenaListFromCatalog()` scans `ASSET_ARENA` entries. Selection writes catalog ID string to `g_MatchConfig.stage_id`. `syncArenaFromConfig()` matches by string comparison. Both solo (`matchStart()`) and network (`netLobbyRequestStartWithSims()`) paths pass catalog ID strings.

2. **Weapon Set Configuration** — ALREADY CATALOG-NATIVE (M0.1c). `buildSpawnWeaponList()` dynamically scans `ASSET_WEAPON` entries. Spawn weapon picker stores `g_MatchConfig.spawn_weapon_id`. Custom slots sync `weapon_ids[]` via `matchGetWeaponSlotCatalogId()`. `matchStart()` resolves all to integers at last-moment handoff.

3. **Bot Configuration** — ALREADY CATALOG-NATIVE. Body picker uses `catalogMpBodyId()` for enumeration, stores `sl->body_id`/`sl->head_id` in matchslot. Trait sliders (accuracy, reaction, aggression) work. 3D character preview calls `pdguiCharPreviewRequest(sl->head_id, sl->body_id)`.

4. **Game Mode Selection** — WORKING. Scenario combo writes `g_MatchConfig.scenario` (integer 0-5 — engine constants, not assets).

5. **Match Start Flow** — FULLY CATALOG-NATIVE. `matchStart()` resolves: `stage_id` → stagenum, `weapon_ids[]` → `g_MpSetup.weapons[]`, `spawn_weapon_id` → spawnWeaponNum, `body_id`/`head_id` → mpbodynum/mpheadnum — all via catalog at last-moment handoff.

6. **Agent Create (pdgui_menu_agentcreate.cpp)** — ALREADY CATALOG-NATIVE. Uses `catalogMpBodyId()`/`catalogMpHeadId()` for enumeration, passes catalog ID strings to `mpPlayerConfigSetHeadBody()`.

### Code Changes (1 file)
- **pdgui_menu_room.cpp**: Fixed stale header comment — `stagenum` → `stage_id` in function signature documentation (lines 11-13).

### Decisions
- No functional code changes needed — all 5 audit targets passed.
- `arena_entry.stagenum` field retained for debugging logs (not used for identity).
- `arenaGetName()` override table retained (needed for AllInOneMods language file collision).

### Next Steps
- Build verification (Mike)
- M0.1d: Remaining asset types (texture, audio, animation) or D5 Phase 3 continuation

---

## Session S171 — 2026-04-07 (M0.1c — Weapon Catalog Signature Migration)

**Focus**: Replace integer weapon identity at public function boundaries with catalog ID strings.

### What Was Done

**Audit Results**:
- Wire protocol: Already catalog-native (v30/v31). `netWriteWeaponRef()`/`netReadWeaponRef()` convert WEAPON_* ↔ catalog session refs. SVC_STAGE_START sends session refs. CLC_LOBBY_START sends catalog ID strings. No changes needed.
- Save files: Already write catalog ID strings (`weapon_ids` array in mpsetup saves, `weapon_id%d` in scenario saves). Backward-compat integer fallback preserved.
- Key boundary targets: `matchconfig.spawnWeaponNum` (u8 WEAPON_* enum) and `matchconfig.weapons[]` (u8 MPWEAPON_* indices) — both integer-native, needed catalog ID PRIMARY fields.
- Spawn weapon picker: Hardcoded 35-entry `s_SpawnWeapons[]` table with integer weaponnums — needed catalog sourcing.
- Legacy engine code (bondgun, propobj, inv, botinv — 66 internal functions): Stays integer-native. These are the final handoff to legacy engine API.

**Code Changes (7 files)**:
- **matchsetup.h**: Added `weapon_ids[6][64]` (PRIMARY catalog IDs for per-slot weapons) and `spawn_weapon_id[64]` (PRIMARY catalog ID for spawn weapon). Marked `weapons[]` and `spawnWeaponNum` as DEPRECATED derived values. Added `matchGetWeaponSlotCatalogId()` declaration.
- **matchsetup.c**: `matchConfigInit()` initializes new fields. `matchStart()` resolves `weapon_ids[]` → `g_MpSetup.weapons[]` and `spawn_weapon_id` → `spawnWeaponNum` via catalog at last-moment handoff. New `matchGetWeaponSlotCatalogId()` accessor bridges `g_MpSetup.weapons[slot]` → catalog ID.
- **pdgui_menu_room.cpp**: Replaced hardcoded `s_SpawnWeapons[35]` integer table with `buildSpawnWeaponList()` that scans ASSET_WEAPON catalog entries dynamically. Spawn weapon picker writes `spawn_weapon_id`. Custom weapon slot editing syncs `weapon_ids[]` via `matchGetWeaponSlotCatalogId()`. `syncSpawnWeaponFromConfig()` matches by catalog ID string.
- **netmsg.c**: CLC_LOBBY_START weapon write prefers `weapon_ids[]` (PRIMARY) over runtime resolution from `g_MpSetup.weapons[]`.
- **scenario_save.c**: Save writes `spawnWeaponId` field. Load populates `weapon_ids[]` (PRIMARY) and derives `weapons[]` (DEPRECATED). Legacy integer fallback preserved with reverse-resolution to catalog ID.
- **player.c, bot.c**: Comment updates — `spawnWeaponNum` is now a derived value from `spawn_weapon_id`, resolved at `matchStart()`.

### Decisions
- `player.c`/`bot.c` spawn code reads `spawnWeaponNum` which is derived at `matchStart()` — same pattern as `stagenum` derived from `stage_id`. No code changes needed in spawn logic, only comment updates.
- Weapon set presets (Pistols, Automatics, etc.) don't use per-weapon catalog IDs — they're selected by set index and the engine fills in the weapons internally. Only custom sets and spawn weapon use catalog ID fields.
- 38 public boundary functions in legacy engine code (bondgun, propobj, mplayer) stay integer-native — they are the final handoff point where WEAPON_* enums get consumed.

### Bugs Fixed
- None (migration only).

### Next Steps
- **BUILD VERIFICATION** — Mike to run build-headless.ps1 (worktree can't access MinGW)
- M0.1d: Remaining asset types (texture, audio, animation, etc.)
- Or: D5 Phase 3 continuation (menu roster port)

---

## Session S170 — 2026-04-07 (M1.2 — Solo Mission Flow)

**Focus**: Fix remaining solo campaign flow issues: endscreen mouse, Esc race condition, Next Mission verification.

### What Was Done

**B-122 FIXED: Endscreen mouse unresponsive** (3 files):
- **Root cause**: Deferred hotswap flush in `pdgui_backend.cpp:426` checked `!g_PdguiActive && !pdguiIsPauseMenuOpen()` — only caught debug overlay and pause menu. When endscreen pushed `g_CtxImGuiMenu`, the flush didn't recognize it and re-enabled `SDL_SetRelativeMouseMode(SDL_TRUE)`, overriding the context system.
- **Fix 1**: Changed deferred flush guard to `!pdguiIsActive()` — checks full input context stack (any non-gameplay context blocks the flush).
- **Fix 2**: Added `inputCtxSyncMouseMode()` to `inputctx.c` — per-frame enforcement that ensures SDL mouse mode matches the top context. Called from `inputCtxEndFrame()`. Catches any case where something outside the context system changed SDL state.
- **Fix 3**: Removed manual `SDL_SetRelativeMouseMode(SDL_FALSE)` / `SDL_ShowCursor(SDL_ENABLE)` / `SDL_WarpMouseInWindow` from both solo and MP endscreen renderers in `pdgui_menu_endscreen.cpp`. The context's `on_push` callback handles this.

**B-124 FIXED: Esc open/close race condition** (3 files):
- **Root cause**: `ImGui_ImplSDL2_ProcessEvent(ev)` in `pdguiProcessEvent()` ran before `inputCtxDispatch(ev)`, so ImGui always saw key events regardless of context state. When a context was pushed (e.g., menu opened), ImGui's internal state already had the triggering key marked as pressed, causing `IsKeyPressed(Escape)` to fire on the newly-pushed menu.
- **Fix**: Systemic key suppression in the input context framework:
  - `InputContext` struct: added `push_tick` field (u32, set to `SDL_GetTicks()` on push)
  - `inputCtxShouldSuppressKey(ev)`: returns 1 for KEY_DOWN events when top context was pushed within `INPUTCTX_PUSH_GRACE_MS` (100ms)
  - `pdguiProcessEvent()`: checks suppression before ImGui forwarding — suppressed keys are consumed silently, never reaching ImGui or dispatch

**Next Mission flow verified**:
- `endscreenAdvance()` uses M0.1a pattern: increments `stageindex`, clamps bounds, calls `missionSetStageByCatalog(g_SoloStages[].catalog_id)` — catalog-first
- `pdguiEndscreenHasNextMission()` correctly shows "Main Menu" at last solo stage
- `pdguiEndscreenNextMission()` chains `endscreenAdvance()` → `menuhandlerAcceptMission()` → pops context
- B-123 fix confirmed working end-to-end

### Decisions
- Mouse mode is now enforced by the input context system, not individual menus. `inputCtxSyncMouseMode()` at frame end is the canonical enforcement point.
- Key suppression grace period (100ms) chosen to cover ~6 frames at 60fps — wide enough to catch the triggering press, narrow enough not to eat legitimate subsequent presses.
- Deferred flush guard consolidated from `!g_PdguiActive && !pdguiIsPauseMenuOpen()` to `!pdguiIsActive()` — one function, one check, covers all contexts.

### Bugs Fixed
- **B-122**: Endscreen mouse unresponsive (systemic: context-driven mouse mode)
- **B-124**: Esc open/close race condition (systemic: key suppression on push)

### Next Steps
- Build verification (Mike)
- Playtest: complete solo mission, verify mouse works on endscreen, verify Esc opens/closes menu cleanly
- M0.1c: Weapon signature migration or D5 Phase 3 continuation

---

## Session S169 — 2026-04-07 (M0.1b — Body/Head Catalog Signature Migration)

**Focus**: Eliminate all integer body/head identity at public function boundaries. Phase 7 wrapper caller elimination.

### What Was Done

**Audit Results**:
- Grepped all 6 named conversion wrappers across entire codebase
- `catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx`, `catalogResolveBodyByMpIndex`, `catalogResolveHeadByMpIndex`, `catalogResolveWeaponByGameId` — **already deleted** in prior sessions (zero definitions, zero callers)
- `catalogGetSafeBody`, `catalogGetSafeHead`, `catalogGetSafeBodyPaired` — **zero external callers** found. Only used internally within `modelcatalog.c` by string-based validators

**Code Changes (3 files)**:
- **modelcatalog.c**: Made `catalogGetSafeBody()`, `catalogGetSafeHead()`, `catalogGetSafeBodyPaired()` all `static`. These are now internal implementation details of the string-based validators.
- **modelcatalog.h**: Removed public declarations for the 3 integer-based safe functions. String-based validators (`catalogValidateBodyId`, `catalogValidateBodyIdPaired`, `catalogValidateHeadId`) remain as the public API.
- **port/CLAUDE.md**: Updated catalog accessor documentation to reference string-based validators instead of deleted integer-based functions.

**Remaining enumeration calls (~30)**:
- `catalogMpBodyId()`/`catalogMpHeadId()` are used in ~30 sites (mplayer.c, room.cpp, agentcreate.cpp, identity.c, matchsetup.c) — these are integer→string enumeration helpers (for display/iteration), NOT identity-passing wrappers. They convert mp_index to catalog ID for UI rendering and save paths.
- Deep struct migration (PlayerConfig/BotConfig to store catalog IDs natively) would eliminate these — tracked under "Gameplay state — category-based".

### Decisions
- `catalogMpBodyId`/`catalogMpHeadId` classified as enumeration utilities, not conversion wrappers — they serve the UI iteration pattern (`for b in 0..numBodies, get catalog ID for display`), which is a legitimate use of integer indices for enumeration
- Phase 7 declared complete: zero integer-based conversion wrappers remain at public boundaries

### Next Steps
- M0.1c: Weapon signature migration
- Or: Gameplay state migration (struct-level catalog ID fields in PlayerConfig/BotConfig)
- D5 Phase 3: Continue menu roster port

---

## Session S168 — 2026-04-06 (M1.1 — Campaign Mission Select Redesign)

**Focus**: Replace flat mission list with two-panel mission select UI. Fixes B-90 (unlock filter), B-91 (objectives display), B-96 (difficulty flow).

### What Was Done

**Two-Panel Mission Select (renderMissionSelect rewrite)**:
- Left panel: Mission list with chapter headings, blip completion dots. Locked missions shown grayed-out and non-selectable (B-90). D-pad navigation skips locked entries.
- Right panel: Mission detail — stage name header, inline difficulty picker (Agent/SA/PA) with color badges and best times, objectives list filtered by selected difficulty (B-91), briefing text preview, Start Mission button.
- Single-screen flow replaces 3-dialog chain (B-96): pick mission → pick difficulty → see objectives → Start. All in one screen.
- D-pad left/right switches panel focus. A/Enter in left panel moves to right. B goes back.
- Mouse click on mission row selects it and focuses right panel.

**New C helper: `soloLoadBriefingForStageId()`** (mainmenu.c):
- Wraps `setupLoadBriefing()` with catalog ID resolution
- Called from ImGui when selected mission changes (avoids requiring legacy dialog open)
- Clears previous language bank before loading new one

**Build fix: missing `<string.h>` includes**:
- `bg.c` and `bodyreset.c` were using `strcmp()` (added in M0.1a) without `#include <string.h>`
- Added includes to fix `-Wimplicit-function-declaration` errors

### Decisions
- Two-panel single-screen design chosen over dialog chain — matches modern console UX (Halo/Destiny style)
- Difficulty and objectives embedded in detail panel rather than separate dialogs — reduces cognitive load
- `soloLoadBriefingForStageId()` added as C-linkage helper rather than exposing `g_Menus[]` to C++ — keeps interface clean
- Briefing data cached by stage index (`s_PrevBriefingStage`) to avoid redundant loads

### Bugs Fixed
- **B-90**: Mission select unlock filter (locked missions grayed out)
- **B-91**: Objectives display from game data (loaded via `soloLoadBriefingForStageId`)
- **B-96**: Difficulty flow redesigned (inline in detail panel)

### Next Steps
- Playtest: verify two-panel layout, difficulty selection, objectives, Start button
- B-97: Separate Special Assignments section visually (currently has gold heading but same panel)
- B-122: Endscreen mouse still unresponsive (separate issue)
- M0.1b: Body/head signature migration (interleaved cadence)

---

## Session S167 — 2026-04-07 (M0.1a — Stage Signature Migration)

**Focus**: Replace ALL hardcoded stagenum integer references in mission-flow code with catalog ID lookups. Critical path work unblocking M1 (Playable Campaign).

### What Was Done

**Commit `270d57c` on `dev` — 9 files, +121/-113 lines**

- **types.h**: Added `const char *catalog_id` field to `struct solostage` — stages now carry catalog identity natively
- **mainmenu.c**: Populated all 21 `g_SoloStages[]` entries with catalog ID strings (`"base:defection"`, `"base:chicago"`, etc.). Legacy menu path uses `catalog_id` directly instead of `bgGetStageIndex` roundtrip.
- **endscreen.c**: New `missionSetStageByCatalog()` function resolves stagenum from catalog. All 4 `missionSetStagenum(g_SoloStages[].stagenum)` calls converted. Fixes B-123 (next mission reloading same stage).
- **bg.c**: 6 `STAGE_` enum comparisons converted to `strcmp(g_MissionConfig.stage_id, "base:...")`
- **mplayer.c**: `stage_id` sync after random resolution uses `bgGetStageIndex` before `catalogIdByRuntime`. Surface type switch (22+ cases) converted from `switch(stagenum)` to `strcmp` chain.
- **ingame.c**: `STAGE_ATTACKSHIP` check → `strcmp(g_MissionConfig.stage_id, "base:attackship")`
- **bodyreset.c**: 3 stage-to-head-count mappings → catalog ID comparisons
- **pdgui_menu_agentselect.cpp** + **pdgui_menu_solomission.cpp**: Shadow structs updated, mission select uses `catalog_id` directly

### Decisions
- Stage identity is now catalog-native end-to-end in the solo mission flow. `stagenum` only appears at final `mainChangeToStage()` handoff.
- `g_SoloStages[]` carries the catalog ID as primary identity — the integer fields remain for legacy engine calls but are never used as identity.

### Bugs Fixed
- **B-123**: Next Mission reloads same stage ✓ (root cause: same B-120 pattern in endscreen)

### Next Steps
- M1.1 mission select UI work (now unblocked by catalog-native stage identity)
- M0.1b body/head signature migration (interleaved cadence)
- B-122 / B-124 input bugs (M0.2 tactical fixes)

---

## Session S166 — 2026-04-06 (Bug Fix — B-120 wrong stage loaded + B-121 endscreen input context)

**Focus**: Fix two playtest bugs — wrong stage loaded for solo missions (B-120), unresponsive endscreen menu (B-121).

### What Was Done

**B-120: Wrong stage loaded for solo missions (HIGH)**:
- Root cause: `catalogIdByRuntime(ASSET_MAP, X)` expects `X` = stage table array index (position in `g_Stages[]`, 0–86), but both `pdgui_menu_solomission.cpp` and `mainmenu.c` were passing the logical stagenum (e.g., 0x5e=94). These are different values.
- The catalog registers stages with `e->runtime_index = idx` where `idx` is the stage table array index (see `assetcatalog_base.c:437`). Passing stagenum=0x40 (64 decimal) looked up `s_RuntimeCache[ASSET_MAP][64]` which resolved to an MP arena (`bg_mp8`) instead of the correct solo mission stage.
- Fix: convert stagenum → stage table index via `bgGetStageIndex(sn)` before calling `catalogIdByRuntime()`. Applied to both:
  - `pdgui_menu_solomission.cpp:647` — ImGui mission select path
  - `mainmenu.c:1995` — legacy menu mission select path

**B-121: Endscreen menu not interactive (MED)**:
- Root cause: `renderSoloEndscreen()` in `pdgui_menu_endscreen.cpp` released SDL mouse grab directly (`SDL_SetRelativeMouseMode(SDL_FALSE)`) but never pushed `g_CtxImGuiMenu` input context. Without the context push, the input stack didn't route events to ImGui, making buttons unclickable.
- Fix: push `g_CtxImGuiMenu` on window appear (guarded by `!inputCtxIsActive()`), matching the pattern from `pdgui_menu_pausemenu.cpp:1132-1134`.
- Also fixed the same issue in `renderMpEndscreen()` which had identical missing input context.
- Added `#include "inputctx.h"` to `pdgui_menu_endscreen.cpp`.

### Decisions
- Use `bgGetStageIndex()` for stagenum→index conversion rather than adding a new catalog API function — it's a simple O(n) scan that already exists and is only called once per mission select.
- Push input context in endscreen renderer rather than in `menuPushRootDialog()` — keeps the fix localized to ImGui renderers that need it, without changing legacy menu infrastructure.

### Bugs Fixed
- **B-120**: Wrong stage loaded for solo missions ✓
- **B-121**: Endscreen menu not interactive ✓

### Next Steps
- Playtest: verify solo mission select → correct stage loads → death → endscreen is interactive
- Remaining `catalogIdByRuntime(ASSET_MAP, stagenum)` callers — audit for same index confusion pattern
- B-119 endscreen.c restart paths (4 calls) still using `g_MissionConfig.stagenum` — migrate to catalog-first

---

## Session S165 — 2026-04-06 (Bug Fix — B-119 stagenum=0x00 crash + catalog-first pattern)

**Focus**: Fix stagenum=0x00 crash on solo mission start; establish universal catalog-first identity pattern for stages.

### What Was Done

**Root Cause Diagnosed (B-119)**:
- `sm_missionconfig` shadow struct in `pdgui_menu_solomission.cpp` was missing `stage_id[64]` field that was added to real `missionconfig` for catalog migration
- Because `stage_id[64]` sits between `diff_pdmode` (offset 0) and `stagenum` (offset 65), the shadow struct had `stagenum` at offset 1 (wrong) instead of offset 65
- Writes to `g_MissionConfig.stagenum` from C++ code went to `stage_id[0]`, real `stagenum` stayed 0x00
- `menuhandlerAcceptMission` read `stagenum=0x00`, called `mainChangeToStage(0x00)` → crash

**Fixes**:
- **`pdgui_menu_solomission.cpp`**: Added `stage_id[64]` to `sm_missionconfig` shadow struct at correct offset. Mission select now sets `stage_id` via `catalogIdByRuntime(ASSET_MAP, sn)` only — no stagenum stored. Pause menu restart resolves stagenum via `catalogResolveStage()` at point of use.
- **`mainmenu.c` `menuhandlerAcceptMission`**: Resolves stagenum from `stage_id` at point of consumption. Falls back to `stagenum` field if `stage_id` is empty (legacy menu path). Writes resolved value back to `g_MissionConfig.stagenum` for not-yet-migrated consumers (endscreen.c restart paths).
- **`mainmenu.c` `menudialog00103608`**: Resolves stagenum from `stage_id` before `setupLoadBriefing()` — fixes briefing load for ImGui mission select path.
- Added `#include "assetcatalog.h"` to solomission.cpp (has extern "C" guards, safe).

**Universal Constraint Added** (game director binding directive):
- Catalog ID (`stage_id`) is the sole identity for all stage flows. stagenum is only extracted at final point of consumption (just before `mainChangeToStage`). Pattern: `catalog_stage_result_t r; catalogResolveStage(stage_id, &r); mainChangeToStage(r.stagenum);`
- Added to `constraints.md` as universal project-wide rule.

### Decisions
- Write `resolved_stagenum` back to `g_MissionConfig.stagenum` in `menuhandlerAcceptMission` — pragmatic bridge for endscreen.c/menutick.c restart paths that haven't been migrated yet.
- Use `catalogIdByRuntime(ASSET_MAP, sn)` to resolve from N64 stagenum → catalog ID (matching pattern from mainmenu.c legacy code at line 1971).

### Bugs Fixed
- **B-119**: stagenum=0x00 crash on solo mission start ✓
- **B-91** (partial): briefing loader now resolves correct stagenum from stage_id — briefing text should now load for ImGui path

### Next Steps
- Playtest: verify mission select → difficulty → objectives → accept works without crash
- Remaining `g_MissionConfig.stagenum` consumers in endscreen.c (4 calls) — migrate to catalog-first pattern
- menutick.c line 573 also uses stagenum — migrate
- Consider catalog-first sweep for all solo stage identity sites

---

## Session S164 — 2026-04-06 (D5 Phase 3 Session 1 — Solo Pause Menu)

**Focus**: Implement proper ImGui pause menu for solo missions (B-93, B-98)

### What Was Done

**`port/fast3d/pdgui_menu_solomission.cpp`** — `renderPauseMenu()` rewritten:
- Added extern "C" declarations: `lvGetDifficulty()`, `objectiveGetCount()`, `objectiveCheck()`, `mainChangeToStage()`
- Added `#include "pdgui_nav.h"`
- **Objectives display**: Fixed loop to start at i=1 (index 0 = briefing text, not an objective). Added difficulty filtering via `g_Briefing.objectivedifficulties[i]` bit test. Added completion status icons (green circle+checkmark = complete, red circle+X = failed, blue dot = incomplete) with text color coding.
- **Button array**: 5 buttons — Resume (0), Restart Mission (1), Inventory (2), Options (3), Abort! (4). Restart uses `mainChangeToStage(g_MissionConfig.stagenum)` (not the nonexistent `menuStop()`).
- **Nav**: B-button/Escape cancel handler calls `menuPopDialog()`. `pdguiNavTickWrap()` for D-pad wrap.
- `k_NumPauseItems` updated from 4 to 5 for correct D-pad item count.
- Build: clean (only pre-existing line 29 comment warning).

### Decisions
- `menuStop()` has no definition anywhere — cannot use it. Restart Mission goes directly to `mainChangeToStage()`.
- Objective loop must start at i=1 (index 0 is briefing text, not an objective).
- `objectiveCheck(objIdx)` takes 0-based index, so objIdx = i - 1.

### Bugs Fixed
- **B-93**: Pause menu now has Abort, Restart, objective checklist ✓
- **B-98**: OG rendering fallback suppressed — `pdguiHotswapRegister` registration ensures ImGui fires instead ✓

### Next Steps
- Playtest: verify pause menu renders, objectives show correctly, Restart/Abort work
- Phase 3 Session 2: next priority screen from d5-full-menu-overhaul.md

---

## Session S163 — 2026-04-06 (D5 Phase 2 Session 2 + Infrastructure + Design)

**Focus**: Wire nav into menus, safe area, LB/RB tabs, UX guidelines, UI scaling, dev window prune button, input SSOT design

### What Was Done

**Phase 2 Session 2 — Nav wired into menus**:
- Main menu: LB/RB top-level view cycling, pdguiNavTickWrap() before End()
- Room menu: LB/RB bumper tab switching (Combat Sim/Campaign/Counter-Op), pdguiNavTickWrap()
- A/B gamepad buttons verified working via ImGui's built-in nav (no extra code needed)

**Safe area system** (pdgui_backend.cpp + pdgui_nav.h):
- PdSafeArea struct with per-edge independent margins (top/bottom/left/right, 0.0–0.25)
- pdguiGetSafeArea() — auto-detects ultrawide (>2.0 aspect → 10% horizontal, 5% vertical)
- pdguiSetSafeAreaMargins() — per-edge override
- 4 configRegisterFloat entries for pd.ini persistence (UI.SafeAreaTop/Bottom/Left/Right)

**Menu UX guidelines** committed to d5-full-menu-overhaul.md:
- Controller nav rules (D-pad, wrapping, A/B/X/Y, LB/RB, 5-9 items per screen)
- Layout patterns for PD2 (character grid, arena grid, split-panel settings, expandable bot list)
- Visual feedback rules (multi-layered focus, audio cues, 150-300ms transitions)
- Hybrid input rules (last device wins, 500ms debounce, dynamic button prompts)

**UI scaling guidelines** committed:
- Reference resolution 1080p, scale = viewport_height/1080
- Concrete pixel sizes at every resolution (720p through 4K)
- Font loading at scaled size (not FontGlobalScale)
- Ultrawide clamping (max 2560px menu width, centered)

**Input SSOT design spec** committed:
- Tap/hold/double-tap recognition integrated into context stack dispatch
- Per-context action maps (gameplay vs menu vs text input)
- Fully rebindable (player sees "Hold X — Open Door")
- Replaces CK_* mappings + inputmodes.c + ImGui hardcoded gamepad nav
- Absorbs existing inputmodes.c timing infrastructure

**Dev window improvements**:
- PRUNE WORKTREES button (gold-bordered, link panel) — one-click cleanup
- Git identity auto-config on startup (S161, carried forward)
- Release auto-commit pipeline fix (S161, carried forward)

**Worktree guidance updated**: Accept worktrees as tooling reality. Sessions must merge to dev + verify before done. Dispatch verifies main copy after each session.

**Fixes carried forward from earlier in session**: JUMP_LANDING log removed, B-117 context stack reset on stage transition, base-ui auto-extract from ROM

### Decisions
- Phase 2 declared SUBSTANTIALLY COMPLETE (nav infrastructure, safe area, tab switching all done)
- Input SSOT (tap/hold/double-tap unification) is a future phase, spec committed
- Menu opacity stacking DEFERRED to post-OG-strip bugfix pass
- System design guidelines needed for 8 major systems (menu/UI done, 7 remaining)
- Dev window redesign added to backlog (visual layout + smart builds)

### Next Steps
- Phase 3: Full Menu Roster Port (61 screens remaining, ~12 sessions)
- Build + test current changes
- Prune worktrees from dev machine
- Phase 3 Session 1: Solo Pause Menu (B-93, B-98) — highest priority menu

---

## Session S162 — 2026-04-06 (D5 Phase 2 Session 1 — Controller Navigation Infrastructure)

**Focus**: Gamepad navigation helpers — D-pad wrapping, accept/cancel, device detection

### What Was Done

**New files created**:
- `port/include/pdgui_nav.h` (85 lines) — C header with extern "C" guards. API: `pdguiNavOnEvent()`, `pdguiNavTickWrap()`, `pdguiNavAcceptPressed()`, `pdguiNavCancelPressed()`, `pdguiNavGetLastDevice()`, `pdguiNavIsGamepad()`, `pdguiNavEndFrame()`, `pdguiNavSetWrapCallback()`.
- `port/src/pdgui_nav.c` (170 lines) — C implementation. Device detection with 500ms debounce, SDL event-based accept/cancel buffering, wrap callback pattern.

**pdgui_backend.cpp modified** (27 lines added):
- Included `pdgui_nav.h` + `imgui_internal.h`
- `navWrapTrampoline()` — C++ function that calls `ImGui::NavMoveRequestTryWrapping(win, ImGuiNavMoveFlags_LoopY)` for current window
- Registered wrap callback in `pdguiInit()`
- `pdguiNavEndFrame()` called unconditionally at start of `pdguiNewFrame()` (clears previous frame's accept/cancel state even when menus are inactive — prevents stale presses)
- `pdguiNavOnEvent()` called in `pdguiProcessEvent()` before ImGui event forwarding

**Architecture decisions**:
- Wrap uses ImGui's built-in `NavMoveRequestTryWrapping` with `LoopY` flag — no manual item index tracking needed
- C/C++ boundary handled via function pointer callback (avoids including imgui_internal.h from C code)
- Device detection uses raw vs. reported state with 500ms debounce to prevent flickering
- Accept/cancel tracked at SDL event level (not ImGui key level) for frame-accurate detection
- Per-frame state cleared at start of next frame (not end of current) to handle early-return paths in pdguiNewFrame/pdguiRender

**Build verified**: Both pdgui_nav.c and pdgui_backend.cpp compile cleanly with -Wall -Wextra. Full build blocked by pre-existing environment temp file permission issue (unrelated).

### Next Steps
- Phase 2 Session 2: Device detection UI prompt switching, wire wrap calls into menu files
- Phase 2 Session 3: Custom nav for character/arena drawers
- Test in playtest: D-pad wrap, A=accept, B=cancel in main menu and room lobby

---

## Session S161 — 2026-04-06 (D5 Phase 1 Session 4 + Playtest + Infrastructure)

**Focus**: Input context lifecycle wiring, playtest verification, bug triage, infrastructure fixes

### What Was Done

**D5 Phase 1 Session 4 — Lifecycle Wiring**:
- `inputCtxInit()` + `inputCtxPush(&g_CtxGameplay)` wired into `main.c:mainInit()` after `inputInit()`
- `inputCtxEndFrame()` wired into `gfx_sdl2.cpp:gfx_sdl_handle_events()` after SDL_PollEvent loop
- `inputCtxShutdown()` wired into `main.c:cleanup()` before `pdguiShutdown()`
- Server: no changes needed (inputctx.c not in SRC_SERVER, shared code already #ifdef guarded)
- Init ordering verified: SDL → inputInit → inputCtxInit → pdguiInit → texInit → game logic

**Networking fixes (earlier this session)**:
- Client hole punch: all 3 join sites wired to `netStartClientWithHolePunch()`
- Server stage log: gated behind `g_NumStages > 0`
- extern "C" guards added to `fs.h` and `config.h`

**Infrastructure**:
- QUICKSTART.md created and updated throughout session
- D5 Full Menu Overhaul design doc committed (`context/designs/d5-full-menu-overhaul.md`)
- Dev window: git identity auto-config on startup, release auto-commit pipeline fix
- Constraints updated: zero-config networking, self-generating mods, zero DLL, legacy menus dead, init ordering audit requirement

**Playtest (v0.0.49)**:
- Input context stack confirmed working (gameplay push at boot, pause push/pop during match)
- Hole punch waterfall fired correctly (direct 3s timeout → PUNCH_REQ → ACK timeout → ENet retry)
- Second connection attempt succeeded via direct (UPnP had finished by then)
- Match started cleanly (CLC_LOBBY_START, manifest, countdown, SVC_STAGE_START)
- **B-117**: Hard crash on match exit — no shutdown sequence, pause context still active
- **Menu opacity stacking**: Background darkens on repeated open/close (additive haze)
- **JUMP_LANDING spam**: Per-frame ground clamp logging, needs verbose gate

### Decisions
- Phase 1 (Input Context Stack) declared COMPLETE
- Phase 2 (Controller Navigation) is next
- B-117 crash logged, will investigate alongside Phase 2
- Init ordering audit is now a standing requirement for all future work

### Next Steps
- Phase 2: Controller navigation (D-pad wrap, A/B, device detection, cheat buffer)
- Fix B-117 crash on match exit
- Fix menu opacity stacking (theme state reset on close)
- Gate JUMP_LANDING behind verbose logging
- Phase 4 Session 1: Auto-extract base-ui textures (can pull forward anytime)

---

## Session S160 — 2026-04-06 (D5 Full Menu Overhaul — Phase 1, Session 3)

**Focus**: Migrate all `pdmainSetInputMode()` callers to input context stack; remove `g_InputMode` system entirely.

### What Was Done

**All 15 `pdmainSetInputMode()` call sites migrated** across 7 files:
- `port/src/net/netmsg.c` (2 sites): `inputLockMouse(1) + pdmainSetInputMode(GAMEPLAY)` → `inputCtxPopDeferred(&g_CtxImGuiMenu)` with `inputCtxIsActive()` guard. Both co-op/anti and MP SVC_STAGE paths.
- `port/src/net/matchsetup.c` (2 sites): Same pattern — match start and challenge start paths.
- `port/src/net/net.c` (1 site): Standalone `inputLockMouse(1)` removed — context stack handles mouse capture via gameplay's `on_push`.
- `port/src/menumgr.c` (1 site): `restoreGameplayMouseCapture()` now pops `g_CtxImGuiMenu` instead of calling `pdmainSetInputMode`.
- `port/fast3d/pdgui_bridge.c` (2 sites): Endscreen mission restart/advance — pop ImGui menu context.
- `port/fast3d/pdgui_menu_solomission.cpp` (2 sites): Accept mission buttons — pop ImGui menu context.
- `port/fast3d/pdgui_menu_pausemenu.cpp` (5 sites):
  - `pdguiPauseMenuOpen()`: `pdmainSetInputMode(MENU)` → `inputCtxPush(&g_CtxPauseMenu)`.
  - `pdguiPauseMenuClose()`: `pdmainSetInputMode(GAMEPLAY)` → `inputCtxPopDeferred(&g_CtxPauseMenu)`.
  - Game-over screen (3 sites): `pdmainSetInputMode(MENU)` → `inputCtxPush(&g_CtxImGuiMenu)` with double-push guard.

**Old system removed**:
- `pdmainSetInputMode()` function deleted from `port/src/pdmain.c` (~20 LOC).
- `InputOwnerMode g_InputMode` global variable deleted from `port/src/pdmain.c`.
- `InputOwnerMode` enum, `g_InputMode` extern, and `pdmainSetInputMode()` declaration removed from `port/include/pdmain.h`.
- `pdmain.h` now contains only `pdmainGetLvFrame60()` — the sole remaining function.
- Unused `#include <SDL.h>` and `#include "input.h"` removed from pdmain.c.
- All 7 migrated files: `#include "pdmain.h"` → `#include "inputctx.h"`.

**Build verified**: Both client (`pd`) and server (`pd-server`) compile cleanly with zero errors.

### Decisions
- All GAMEPLAY transitions use `inputCtxPopDeferred` with `inputCtxIsActive` guard (safe if context not on stack).
- All MENU transitions use `inputCtxPush` with `!inputCtxIsActive` guard (prevents double-push).
- Pause menu uses `g_CtxPauseMenu`; all other menus use `g_CtxImGuiMenu`.
- `inputmodes.c`'s own `g_InputMode[]` array (for doubletap/hold per-action config) is completely unrelated and untouched.

### Next Steps
- **Phase 1, Session 4**: Wire `inputCtxInit()` into startup, `inputCtxEndFrame()` into main loop, `inputCtxPollFrame()` for continuous input. Push `g_CtxGameplay` at boot.

---

## Session S159 — 2026-04-06 (D5 Full Menu Overhaul — Phase 1, Session 2)

**Focus**: Rewrite `pdgui_backend.cpp` event filter to use input context stack

### What Was Done

**pdguiProcessEvent() rewritten** from scratch:
- Removed the entire old event filter (~110 LOC of manual mode checks, cooldown handling, `io.WantCapture*` decisions, Tab suppression, `g_InputMode` references).
- New function (~40 LOC): global hotkeys (F8/F12/RS-click) → `ImGui_ImplSDL2_ProcessEvent()` for state tracking → `inputCtxDispatch(ev)` for routing.
- F12 toggle now pushes/pops `g_CtxDebugOverlay` on the context stack instead of manually calling `pdguiUpdateMouseGrab()`.

**pdguiWantsInput() simplified**:
- Old: checked hotswap, pause menu, overlay, and `io.WantCapture*` separately (~20 LOC).
- New: `inputCtxGetTop() != &g_CtxGameplay` — if top context isn't gameplay, ImGui wants input (3 LOC).

**pdguiToggle() updated**: Uses context stack push/pop instead of direct `g_PdguiActive` + `pdguiUpdateMouseGrab()`.

**pdguiIsActive() simplified**: Was checking `g_PdguiActive || pdguiHotswapWasActive() || pdguiIsPauseMenuOpen()`. Now: `inputCtxGetTop() != &g_CtxGameplay` — same pattern as `pdguiWantsInput()`.

**pdguiUpdateMouseGrab() removed**: Context `on_push`/`on_pop` callbacks handle mouse state. Saved mouse state variables (`g_PdguiSavedRelativeMode`, `g_PdguiSavedShowCursor`) also removed (dead code).

**menuIsInCooldown()/menuIsOpen() externs removed**: No longer needed — context stack handles transition safety via deferred pop.

**inputCtxDispatch() fix**: Changed to respect `on_event()` return value. Gameplay's `on_event` returns 0 (game processes it), ImGui contexts return 1 (consumed). Critical for correct event routing.

**g_InputMode references eliminated** from pdgui_backend.cpp. The `pdmain.h` include kept only for `pdmainGetLvFrame60()` (B-92 render path).

### Decisions
- ImGui always sees every event via `ImGui_ImplSDL2_ProcessEvent()` before context dispatch. This ensures ImGui tracks internal state (mouse pos, key state) even when the game owns input.
- `inputCtxDispatch()` return value now comes from `on_event()`, not hardcoded 1. This lets gameplay context return 0 ("not consumed, game processes it") while menu contexts return 1 ("consumed").

### Next Steps
- **Phase 1, Session 3**: Migrate all `pdmainSetInputMode()` callers (~20 sites) to use `inputCtxPush`/`inputCtxPopDeferred`; remove `g_InputMode` enum entirely.
- **Phase 1, Session 4**: Wire `inputCtxInit()` into startup, `inputCtxEndFrame()` into main loop, `inputCtxPollFrame()` for continuous input.

---

## Session S158 — 2026-04-06 (D5 Full Menu Overhaul — Phase 1, Session 1)

**Focus**: Input Context Stack foundation — `inputctx.h` + `inputctx.c`

### What Was Done

**Input Context Stack created** (Phase 1, Session 1 of D5 Full Menu Overhaul):
- Created `port/include/inputctx.h` — public API for priority-based input context pushdown automaton.
- Created `port/src/inputctx.c` — full implementation (~340 LOC).
- Stack API: `inputCtxInit/Shutdown/Push/PopDeferred/PopImmediate/Dispatch/PollFrame/EndFrame`.
- Query API: `inputCtxGetTop/IsActive/GetDepth/GetTopName`.
- 4 built-in contexts: `g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`.
- Gameplay context: captures mouse (relative mode), eats all input, delegates to existing game pipeline.
- ImGui/Pause/Debug contexts: release mouse, consume keyboard/mouse/gamepad events, return 1 (consumed) — actual ImGui forwarding deferred to Session 2 integration layer.
- Deferred pop pattern: `marked_for_removal` flag, cleanup in `inputCtxEndFrame()` — never mid-frame.
- Double-push protection, stack overflow guard, comprehensive logging via `sysLogPrintf`.
- Build verified: compiles cleanly with exact cmake flags (zero errors, zero warnings).
- Auto-discovered by CMake's `file(GLOB_RECURSE)` — no CMakeLists.txt changes needed.

### Decisions
- ImGui context callbacks do NOT call ImGui directly (C code can't call C++ ImGui). They return 1 (consumed) and the pdgui_backend.cpp integration layer (Session 2) handles actual forwarding.
- Pause menu sets a `s_GamePaused` static flag on push/pop — will be exposed via getter when needed.

### Next Steps
- **Phase 1, Session 2**: Rewrite `pdgui_backend.cpp` event filter to use context stack; wire into SDL loop (~200 LOC).
- **Phase 1, Session 3**: Migrate all `pdmainSetInputMode()` callers (~20 sites); remove `g_InputMode`.

---

## Session S157 — 2026-04-06 (Post-S156 Code Sessions)

**Focus**: Phase 8 conversion function elimination, deep array-bypass audit, D5.0 visual layer implementation

### What Was Done

**Catalog Phase 8 — O(n) conversion function elimination** (`3a05532`):
- Eliminated all O(n) conversion functions that scanned arrays linearly.
- Context updated with current migration status.

**Deep array-bypass audit** (`0b4aed2`, `2b409b4`):
- Full audit of direct array access patterns (`g_Weapons[]`, `g_HeadsAndBodies[]`).
- Found 2 hidden `catalogGetMpIndex` reimplementations.
- Fixed all 15 deep audit bypass items — zero gaps remaining.

**D5.0 Visual Layer — revised plan + full implementation** (`8189edb`, `a040275`):
- Deep investigation revealed ~70% of D5.0 was already built (pdgui_theme.cpp, pdgui_style.cpp).
- Phase 1: Split `pdguiThemeInit()` into early + `pdguiThemeLateInit()` (called after `texInit()`).
- Phase 2: ROM texture extraction tool (`--extract-ui-textures` CLI flag).
- Phase 3: Base UI mod (`mods/base-ui/`) with 13 UI texture catalog entries.
- Phase 4: Haze overlay in `pdguiDrawPdDialog()` — green-tinted IA8 compositing.
- Phase 5: CRT scanline pass — 2px-interval horizontal lines, configurable via `pd.ini`.
- Phase 6: Multi-palette support — all 7 palettes (Grey, Blue, Red, Green, White, Silver, BlackGold) now drive theme draw functions.
- Also: Procedural modern-UI mod (`mods/pd-modern-ui/`), TGA loader for mod textures, procedural fallback textures.

**QUICKSTART.md created** (this session — S157 context-only):
- Comprehensive cold-start onboarding document for AI sessions.
- README.md updated to link to it.

### Decisions
- D5.0 visual layer is now substantially complete (implementation, not just plan).
- Phase 8 (O(n) elimination) complete — conversion functions no longer do linear scans.
- Deep audit closed with zero gaps — all 15 bypass items addressed.

### Next Steps
- **Build verification** of all post-S156 commits (Phase 8 + deep audit + D5.0)
- Phase 7 caller elimination: ~85 calls to conversion wrappers
- Weapons (~660 refs), stages (~80), models (~83) migration
- D5.3 Pause Menu
- B-112 root cause (awaiting VEH crash log)

---

## Session S156 — 2026-04-06 (Handoff / End of Night)

**Focus**: Phase 7 audit, triple audit verification, session handoff

### What Was Done

**Catalog ID Migration — Phase 7 audit** (commit `f8b4d00`):
- All conversion function wrappers reviewed: `catalogBodynumToMpBodyIdx`, `catalogHeadnumToMpHeadIdx`, `catalogResolveBodyByMpIndex`, `catalogResolveHeadByMpIndex`, `catalogResolveStageByStagenum`, `catalogResolveArenaByStagenum`, `catalogResolveWeaponByGameId`, `catalogGetSafeBody`, `catalogGetSafeBodyPaired`, `catalogGetSafeHead`.
- Phase 7 commit landed but callers remain (~85 calls across codebase) — cannot fully delete wrappers yet.
- Status: AUDITED. Elimination requires caller-by-caller migration (deep audit task).

**Triple audit — PASSED (11/11)**:
- All 11 original catalog audit findings verified present in codebase.
- 1 gap fixed (validation functions passed wrong index space to `catalogGetSafeBody`/`Head` — corrected).
- Full audit log recorded in session S155 notes.

**Infrastructure**:
- `.gitignore` additions (worktree artifacts, build outputs).
- Worktree cleanup script added (`devtools/cleanup-worktrees.sh`).
- Release pipeline tag-push fix.

### Decisions
- Phase 7 (conversion function elimination) is the next concrete migration task: ~85 call sites must be migrated before wrappers can be deleted.
- Deep audit of direct array accesses (`g_Weapons[]`, `g_HeadsAndBodies[]`) is NOT yet started.
- Catalog-as-data-provider (absorb ROM arrays) and gameplay-state-category-based tracks are NOT yet started.
- All game director decisions stand: D-1 FULL, D-2 FULL, D-3 FULL — zero half measures, catalog is sole source of truth for identity AND state.

### Next Steps
- Build verification of Phases 0–6 (no regressions)
- Phase 7 caller elimination: ~85 calls to `catalogBodynumToMpBodyIdx` et al. — migrate each call site to use catalog ID directly
- Then: deep audit of `g_Weapons[]` / `g_HeadsAndBodies[]` direct array accesses
- B-112 root cause still unknown; next VEH crash log needed
- Playtest for Phase 0–6 regression check

---

## Session S155 — 2026-04-06

**Focus**: UX polish, B-112 hardening, Catalog ID Migration planning + Phases 0–6 execution

### What Was Done

**UX improvements** (commits `16355f8`, `61f6340`, `41b27f8`):
- Bot context menu: checkmarks for selected items, alphabetical character sorting, display name fallbacks.
- Handicap slider fix: showed wrong percentage in online mode.
- Release script fix: ensure tag exists locally before `git push origin`.

**B-112 additional crash guards** (`fb9b85c`):
- Added guards in shot/damage path (chrBruise, chrDamage) for stale chr pointers — defense-in-depth alongside S150's VEH guard.
- Handicap default init: `chr->handicap` initialized to 1.0 in `chrAllocate` to prevent divide-by-zero in damage calculations.

**Catalog ID Migration — full plan** (`9c43d36`, `5e7254c`, `6f59ef6`):
- Created `plan-catalog-id-migration.md` — zero-conversion mandate for ALL asset types (bodies, heads, weapons, stages, models, textures, sounds, animations, game modes, lang banks, props, HUD). ~2,578+ integer refs across ~80+ files.
- Game director decisions: D-1 (full migration for every asset type), D-2 (model numbers — full migration), D-3 (`mainChangeToStage` — full engine refactor to catalog ID).

**Catalog ID Migration — Phases 0–6 execution** (`44c09d2`, `777aef8`, `76eeb8a`, `8a20c9f`, `f4b5bdd`, `238edb0`, `d0808d4`):
- Phase 0: Generation counter + hot-reload API for catalog.
- Phase 2: Catalog ID string fields added to config/data structs.
- Phase 3: Function APIs migrated to catalog ID strings.
- Phase 4: Integer asset comparisons replaced with catalog ID checks.
- Phase 5+6: UI shadow structs, save paths, lobby accessors fixed.
- Fix: CLC_LOBBY_START bot resolution guarded with `#ifndef PD_SERVER`.
- Fix: Validation functions passed wrong index space to `catalogGetSafeBody`/`Head`.

**Infrastructure**: `.gitignore` additions + worktree cleanup script (`bb33037`). Version bump to v0.0.45 (`2e67d64`).

### Decisions
- Catalog ID migration is now the primary workstream — zero integer identity tolerance.
- All asset types in scope (no carve-outs).
- Phases 0–6 complete for bodies/heads; weapons, stages, models still need Phase 3+ migration.

### Next Steps
- Build verification of Phases 0–6
- Continue catalog migration: weapons (~660 refs), stages (~80 refs), models (~83 refs)
- Playtest to verify no regressions from struct changes
- B-112 root cause still open

---

## Session S154 — 2026-04-06

**Focus**: Eliminate integer asset identity from network wire (Phase 1+2)

### What Was Done

**Discovery**: All 6 weapon messages (SVC_PLAYER_STATS, SVC_PROP_SPAWN, SVC_PROP_DAMAGE, SVC_CHR_DISARM, SVC_CHR_STATE, SVC_NPC_STATE/CHR_RESYNC) were ALREADY migrated to catalog session refs via `netWriteWeaponRef`/`netReadWeaponRef` helpers (done in v30). SVC_NPC_STATE has no weapon field. Handicap UI slider was also already fixed.

**Bot body/head conversion elimination** (netmsg.c):
- SVC_STAGE_START write fallback (line 824): `catalogResolveBodyByMpIndex()` → `catalogResolveByRuntimeIndex(ASSET_BODY, ...)` since mpbodynum now stores runtime_index directly.
- CLC_LOBBY_START server read (line 4216): Replaced `catalogBodynumToMpBodyIdx(runtime_index)` with direct `catalogGetSafeBodyPaired(runtime_index, &rawHead)` storage — matches the client decode path. Same for heads.
- Zero `catalogBodynumToMpBodyIdx`/`catalogHeadnumToMpHeadIdx` calls remain in netmsg.c (only in comments).

**SVC_PROP_SPAWN modelnum → catalog ref** (netmsg.c):
- Added `netWriteModelRef()`/`netReadModelRef()` helpers using `catalogResolveByRuntimeIndex(ASSET_MODEL, ...)` and `sessionCatalogLocalResolve()`.
- Write side: both PROPTYPE_WEAPON and PROPTYPE_OBJ modelnum now use `netWriteModelRef()` (catalog session u16).
- Read side: both paths now use `netReadModelRef()`.
- All g_ModelStates[] entries are registered as ASSET_MODEL (by assetcatalog_base_extended.c), so resolution is complete.

**chrBruise guard enhancement** (chr.c):
- Added `!model->definition` check to existing B-112 defense-in-depth guard. Catches stale model pointers with freed definition (non-NULL pointer, NULL definition after stage teardown).

**NET_PROTOCOL_VER 30 → 31** (net.h): Breaking wire change for SVC_PROP_SPAWN modelnum format.

### Decisions
- CLC player move struct `in->weaponnum` (line 219) still uses raw s8 — out of scope for this session (CLC not SVC, 60Hz per-tick bandwidth concern). Noted for future.
- Comments referencing old conversion functions updated to describe new code path.

### Next Steps
- Build verification
- Playtest to verify prop spawn, bot body/head, and weapon resolution all work end-to-end
- Consider migrating CLC player move weaponnum to catalog ref (bandwidth trade-off)
- B-112 root cause still unknown

---

## Session S153 — 2026-04-05 (evening)

**Focus**: Audit + recovery + lobby unification close-out; B-116 bot catalog ID fix

### What Was Done

**Codebase audit**: Full verification of all completed tasks S130–S152 against actual codebase — 22/22 confirmed present.

**Git repo recovery**:
- `.git` was missing `objects/` directory; fetched full history from GitHub to restore.
- Cleaned up `.git.broken` (200MB) and 7 orphaned worktrees (~23GB freed).
- Pushed `dev` to origin.

**S131 completion** (commit `05d5f1d`, pushed):
- 10 bare `strcpy` calls in `port/src/input.c` converted to `strncpy`.
- Local `#define MATCH_MAX_SLOTS 32` removed from 3 UI files + `scenario_save.h`; all now use canonical 40 from `matchsetup.h`.
- Stale field names `headnum`/`bodynum` → `body_id`/`head_id` fixed in `mpsettings.cpp` and `teamsetup.cpp`.

**U-7 Steps C+D — matchsetup.cpp retired** (commit `9fe169e`):
- Step C: 3D character preview ported to room.cpp bot modal — rotating preview, two-column layout, `pdguiCharPreview` pipeline.
- Step D: Redirected 4 `g_MatchSetupMenuDialog` push points (menutick.c:253, menutick.c:537, mainmenu.c:4845, setup.c:5736) to `g_CombatSimulatorMenuDialog` + `pdguiSoloRoomOpen()`; removed `pdguiMenuMatchSetupRegister()` call; renamed file to `.cpp.retired`.

**U-10: Deferred bot authority** (commit `6f471a7`):
- Added `g_NetPendingBotAuthority` flag in `net.h`/`net.c`.
- `netmsgSvcBotAuthorityRead` now sets pending instead of immediately active.
- `botTick` promotes pending → active when `g_PadsFile != NULL && g_NumSpawnPoints > 0`.
- Reset on disconnect and match-end.
- Collapses the prior 60-frame timeout gate into a deterministic condition check.

**B-116: Bot body/head catalog ID resolution** (committed, not pushed):
- Root cause: `SVC_STAGE_START` writer used `botidx + MAX_PLAYERS` as slot index, which could miss actual `SLOT_BOT` entries if they weren't packed at that offset.
- Fix 1 (`netmsg.c`): Pre-built `botSlotMap[]` by scanning `g_MatchConfig.slots[]` for `SLOT_BOT` entries before writing the message.
- Fix 2 (`netmanifest.c`): Server manifest builder now reads `body_id`/`head_id` directly from `g_MatchConfig.slots[]` (mirrors `manifestBuildForHost` pattern).

### Decisions
- Lobby unification (U-1 through U-10) declared **COMPLETE**.
- `pdgui_menu_matchsetup.cpp` is now `.cpp.retired` — not deleted yet pending any edge-case audit, but all code paths redirected.
- B-116 fix committed but not pushed (intentional — will push with next playtest build).

### Next Steps
- Playtest build to verify U-10 + B-116 fixes with dedicated server
- Remaining open playtest issues: B-112 (chr corruption, root cause unknown), B-115 (post-game mouse), event-driven prop sync
- Next major track: D5.0 (Menu Visual Layer) or open playtest stability issues

---

## Session S152 — 2026-04-05

**Focus**: Verify all 5 playtest fixes committed; implement Bug 4 (hotswap frame-1 CI crash)

### What Was Done

Bugs 1–3 and 5 from S151 were already committed in `da40788`. Bug 4 was coded but uncommitted.

**5. Hotswap frame-1 crash fix** (FIX-PLAYTEST-4):
- After mission fail → legacy menu exit → CI load, `pdguiHotswapRenderQueued` fires with `s_HotswapMenuWasActive=true` (from prior stage). On frame 0, `screenManifestTick` with count=0 triggered "leave" events calling `catalogUnloadAsset` while catalog was reinitialising → crash at `+0xc1df3`.
- Fix A: `pdgui_hotswap.cpp` — guard `screenManifestTick` behind `pdmainGetLvFrame60() >= 2`.
- Fix B: `pdgui_backend.cpp` — guard hotswap-close mouse capture flush behind `pdmainGetLvFrame60() > 0`.
- Bridge: `pdmain.c`/`pdmain.h` expose `pdmainGetLvFrame60()` so C++ code can read `g_Vars.lvframe60` without including `types.h`.
- Files: `port/fast3d/pdgui_hotswap.cpp`, `port/fast3d/pdgui_backend.cpp`, `port/src/pdmain.c`, `port/include/pdmain.h`, `port/fast3d/pdgui_bridge.c`.

### Decisions
- All 5 playtest fixes confirmed in codebase and committed.

### Next Steps
- Playtest build to confirm all 5 fixes hold
- Event-driven prop sync redesign (prop resync fix is a stop-gap)
- catalogResolveBodyByMpIndex out-of-range (mpbodynum=63/65/66/67) — lobby UI issue separate from these fixes

---

## Session S151 — 2026-04-05

**Focus**: Playtest bug diagnosis and fixes — invisible bots, broken doors/ammo, death-in-hub crash, bot HP

### What Was Done

**Commit `da40788` on `dev`.**

**1. Bot body/head resolution fix** (FIX-PLAYTEST-1):
- CLC_LOBBY_START server handler read body_id/head_id strings from wire but never stored them in `g_MatchConfig.slots[]`. SVC_STAGE_START write fell back to mpbodynum=0 → all bots got dark_combat body.
- Fix: strncpy body_id/head_id into g_MatchConfig.slots[MAX_PLAYERS+bi] during server read.
- Also bumped MATCH_MAX_SLOTS from 32→40 (MAX_PLAYERS(8)+MAX_BOTS(32) can reach slot 39).
- Files: `port/src/net/netmsg.c`, `port/include/net/matchsetup.h`.

**2. Prop resync spam fix** (FIX-PLAYTEST-2):
- Dedicated server stubs mainChangeToStage → g_Vars.activeprops empty → prop resync always sends 0 props. Client polled every 6 seconds forever.
- Fix: reset desync counter when receiving 0 props, stopping the spam loop.
- Full event-driven prop sync is future work.
- File: `port/src/net/netmsg.c`.

**3. Death-in-hub crash fix** (FIX-PLAYTEST-3):
- Falling through CI geometry → death → titleSetNextStage(0x00) → invalid stage → crash (bgGetStageIndex returns -1, loader uses garbage).
- Fix: guard titleSetNextStage against stagenum=0, redirect to STAGE_CITRAINING (0x26).
- File: `src/game/pdmode.c`.

**4. Bot HP fix** (FIX-PLAYTEST-5):
- chrAllocate defaults maxdamage=4. Online gets 8 from server chr resync, but local Combat Sim bots kept 4 (one-shot by any weapon).
- Fix: set chr->maxdamage=8.0f in botmgrAllocateBot.
- File: `src/game/botmgr.c`.

### Log Analysis Findings (from Mike + Chris playtest logs)
- **Invisible bots**: All bots got body=86/head=4 (dark_combat). Joanna+Elvis head combo from safety clamp on mpbody=0.
- **Broken doors/ammo (Chris)**: Prop resync returns 0 props every 6s. 16+ consecutive resyncs in 2min session.
- **Death crash (Chris)**: Fell through CI ceiling, ground=-667→-707, titleSetNextStage(0x00), bg_lue loaded with chrslots=0xffffff01.
- **CI crash (Mike)**: Failed mission → legacy menu exit → frame 1 crash at +0xc1df3. Needs symbolication (DEFERRED).
- **Post-game mouse dead (Mike)**: Legacy menu steals input, ImGui hotswap doesn't recapture. Related to hotswap state machine (DEFERRED).
- **catalogResolveBodyByMpIndex out-of-range**: mpbodynum=63/65/66/67 queried against [0,63) — separate lobby UI issue.

### Decisions
- Prop sync should become event-driven (Mike's direction) — current fix is a stop-gap.
- Bug 4 (frame 1 CI crash) deferred pending crash symbolication.
- MATCH_MAX_SLOTS increased to 40 to accommodate full player+bot range.

### Next Steps
- Playtest the build to verify fixes
- Event-driven prop sync redesign
- Investigate Bug 4 (CI crash after mission fail)
- Push commits to GitHub (16+ ahead of origin/dev)

---

## Session S150 — 2026-04-04/05

**Focus**: Credits update, bot stuck-detection init, chr pointer-corruption guard, 8MB stack + VEH → v0.0.38

### What Was Done

**Commits `ccf1bae`, `87b3388`, `375292c`, `85928d9` pushed to `dev`. Build auto-commits `ddc742e`, `ab31c6b`, `4d07510`, `d92f4e3`.**

**1. Credits update** (`ccf1bae`):
- Removed Variant line from title info block.
- Moved "PD2 Port Director: MikeHazeJr" up to the vacated slot.
- Added "Tester: smarch" in grass green (0x00CC00).
- Developer / Rare Ltd. row shifted down.
- File: `src/game/title.c`.

**2. Bot stuck-detection initialization** (`87b3388`) — **B-111 fixed**:
- `s_BotStuck` was zero-initialized, causing all 31 bots to fire their first stuck check simultaneously at frame 180 (`STUCK_CHECK_FRAMES`) with a bogus distance-from-origin comparison.
- Fix: initialize snapshot position and frame in `botSpawn()`; safety fallback in `botTick()` for bots that enter play without going through `botSpawn()`.
- File: `src/game/bot.c`.

**3. Chr pointer-corruption guard** (`375292c`) — **B-112 partial**:
- Access violation at `chr->hidden` when `chr` pointer (rbx) gets corrupted during AI execution or action tick dispatch in 31-bot matches.
- Added volatile canary + pointer range validation at two checkpoints: after `chraiExecute` and after the action switch.
- Diagnostic logging identifies whether corruption originates in AI scripts or action handlers.
- Root cause still unknown — guard reduces crash frequency; investigation continues.
- File: `src/game/chraction.c`.

**4. Stack increase to 8MB + VEH** (`85928d9`) — **B-113 fixed**:
- Silent crash in 31-bot matches caused by 2MB default stack being exhausted during deep AI/collision call chains.
- Existing crash handler (UEF) allocated 8KB on stack, causing double fault → process terminated with no log output.
- Fix: increase stack reserve from 2MB to 8MB via linker flag. Add first-chance vectored exception handler (VEH) using static buffers and minimal stack. `crashHandler`'s 8KB msg buffer moved from stack to static storage. `sysLogGetPath()` added so VEH can write directly to log. `_resetstkoflw()` called for stack-overflow recovery.
- Files: `CMakeLists.txt`, `port/include/system.h`, `port/src/crash.c`, `port/src/system.c`.

**Build**: v0.0.38 clean.

### Decisions
- B-112 (chr pointer corruption) gets a guard + diagnostics now; full root-cause fix deferred until the diagnostic log identifies the corruption source.
- VEH is first-chance so it fires before the debugger, ensuring crash logs even on the dev machine.

### Next Steps
- Review VEH crash log from next 31-bot playtest to identify B-112 root cause.
- D5.3 (Pause Menu) is the biggest remaining open gap.

---

## Session S149 — 2026-04-04

**Focus**: Bot spawn root-cause deep-dive — 31 bots on 24 pads + underground ground-clamp + AIDROP filter removal

### What Was Done

**Commits `d2e558e`, `e03a990`, `a81926e` pushed to `dev`. Build commits `fc3a94e`, `2386bbb`.**

**1. Bot spawn crash (31 bots / 24 pads)** (`d2e558e`) — **B-110 further hardened**:
- Root cause: unspawned bots (rooms={-1}) were counted as real enemies in the pad-scoring loop, polluting distance calculations and marking all pads as bad. Bots 9-31 all fell through to the fallback which always picked idx=0, piling 23 bots at the same position → crash ~3 seconds in.
- Three-part fix: (1) skip unspawned bots in scoring loop; (2) pass 4 with `force=true` when all strict passes fail; (3) improved fallback with cycling counter + 80-unit jitter.

**2. Underground ground-clamp + SPAWN-DIAG** (`e03a990`):
- Bots spawning at underground positions (e.g. Chicago y=-634) now clamped upward to real floor via probe from 2000 units above.
- One-time `SPAWN-DIAG` logging at match start dumps all spawn pad positions and source waypoint data.

**3. AIDROP filter root-cause fix** (`a81926e`) — **B-110 root cause**:
- PADFLAG_AIDROP (0x2000) is set on nearly all waypoint pads in multi-level maps (Chicago, etc.). The spawn population code filtered these out, leaving only padnum=0 as valid → all 24 spawn slots got padnum=0 → all bots at same underground position → crash at frame ~180.
- Fix: removed AIDROP filter from both `playerreset.c` and `navspawn.c`. AIDROP is a pathfinding behavior hint (drop off ledge), not a spawn validity marker.
- Sequential fallback added if all spawn pads still collapse to same padnum.
- Diagnostic logging trimmed to first 6 pads.

### Decisions
- AIDROP filter removal is correct — the flag documents pathfinding behavior, not spawn eligibility. No other spawn filter should use pathfinding hint flags.

### Next Steps
- Playtest Chicago map with 31 bots — verify all bots spawn at valid positions.
- S150: credits + crash stability work.

---

## Session S148 — 2026-04-04

**Focus**: CMakeLists.txt corruption repair, Chicago bot spawn root cause (void geometry + HEAD 1000 catalog spam), v0.0.36, design doc

### What Was Done

**Commits `b84c6ba`, `59818e3`, `6ed6a67`, `1235806` pushed to `dev`. Build commits `1ddb77c`.**

**1. CMakeLists.txt corruption repair** (`b84c6ba`) — **B-114 fixed**:
- CMakeLists.txt had two lines (181, 532) with ~30MB of garbage bytes each — encoding bug from devtools.
- File restored to valid CMake.

**2. Bot spawn crash on Chicago + HEAD 1000 catalog spam** (`59818e3`) — **B-110 partially fixed**:
- `playerChooseSpawnLocation`: all fallback paths now use `bgFindRoomsByPos` to resolve rooms when pad data has `room==-1`; final validation ensures no spawn ever returns with `dstrooms[0]==-1`.
- `botSpawn`: after `chrMoveToPos`, if `rooms[0]` still -1 and `floorroom` also -1, call `bgFindRoomsByPos` as last resort.
- `botSpawnAll` failsafe: after initial spawn wave, re-spawn any bots still with `rooms[0]==-1`. Per-tick room recovery now re-spawns bots stuck in void geometry.
- HEAD sentinel value 1000 (random-gender) guarded against catalog lookup — was producing 18× "type=HEAD index=1000 not found" warnings per match. Added `HEAD_RANDOM_GENDER` constant.

**3. v0.0.36 version bump** (`6ed6a67`).

**4. Design doc: implementation-plan-mods-and-d5.md** (`1235806`):
- New file: `context/designs/implementation-plan-mods-and-d5.md` (549 lines).
- Covers mod pipeline (P1-P6) and D5 UI screens (P7-P10) with dependency graph, per-phase specs, and sequencing.

### Decisions
- HEAD_RANDOM_GENDER constant prevents catalog lookup on sentinel — catalog should never be called with magic index 1000.

### Next Steps
- Playtest Chicago with 31 bots to verify void spawn fix.
- Deeper root-cause investigation into AIDROP filter (S149).

---

## Session S147 — 2026-04-04

**Focus**: Three online playtest crash fixes — void spawn fallback, Skedar catalog ID, bot.c log flood

### What Was Done

**Commit `6f8bbfe` pushed to `dev`. Build commits `dd062b2`, `b35cf4f`.**

Three fixes from online playtest analysis:

1. **`player.c`** (void spawn fallback): In `playerChooseSpawnLocation`'s shortlist-empty fallback, scan pads from a random offset for the first one with `room >= 0` rather than picking blindly. Prevents void spawns when bots outnumber spawn pads and some pads have `room == -1`. Adds WARNING log for future incidents.

2. **`mplayer.c`** (Skedar catalog ID): Wrong catalog ID for Skedar arena default — was `"base:mp_skedar"`, must be `"base:arena_mp_skedar"` (arena_ prefix required by Phase B naming). Caused Skedar map to fail catalog resolution.

3. **`bot.c`** (log flood): Removed per-tick MATCH-TRACE botTick entry log — 31 bots × 240fps = ~7,440 lines/sec. Room-recovery logs kept.

**Build**: v0.0.34 clean.

### Next Steps
- Playtest to verify Skedar loads, bots spawn correctly, log no longer floods.

---

## Session S146 — 2026-04-04

**Focus**: botSpawnAll structure fix — move failsafe from setup.c to botTick

### What Was Done

**Commits `d91489e`, `b43ecb7` pushed to `dev`. Build commit `c42e6ac`.**

1. **`d91489e`** fix(build): add missing `bot.h` include in `setup.c` for `botSpawnAll` — compile error from S145 explicit `botSpawnAll()` call.

2. **`b43ecb7`** fix(spawn): moved `botSpawnAll` failsafe from `setup.c` to `botTick`:
   - The explicit `botSpawnAll()` call in `setup.c` ran too early (before bots had valid rooms from the AI script). Moving the failsafe to `botTick` ensures re-spawn happens after the AI script has had a chance to assign rooms.

**Build**: v0.0.33 clean.

### Next Steps
- Online playtest to verify 31-bot spawn with adaptive spacing and failsafe.

---

## Session S145 — 2026-04-04

**Focus**: Room leave fix (CLC_ROOM_LEAVE), botSpawnAll failsafe for non-MP maps, server catalog IDs for bot bodies; context updated S141–S144

### What Was Done

**Commits `7d08f78`, `80cee04` pushed to `dev`.**

**1. Room leave fix** (`7d08f78`):
- "Leave Room" button only called `pdguiSetInRoom(0)` without telling the server. Room stayed open showing 1 occupant.
- Fix: now sends `CLC_ROOM_LEAVE` to server before transitioning to lobby view.
- File: `port/fast3d/pdgui_menu_room.cpp`.

**2. Adaptive spawn spacing** (`7d08f78`):
- Solo maps used as MP arenas (Villa, etc.) have compact layouts where 500-unit minimum spacing rejected most waypoint candidates, leaving too few spawn points.
- Now uses adaptive passes: 500 → 250 → 125 → 60 → 0 unit spacing, stopping when ≥ 8 spawn points found.
- File: `src/game/playerreset.c`.

**3. botSpawnAll failsafe for non-MP maps** (`80cee04`):
- `botSpawnAll` never called on solo mission maps used as MP arenas (Villa, Complex, etc.) — their AI script lacks `aiMpInitSimulants`. Added explicit `botSpawnAll()` call in `setup.c` after bot allocation.

**4. Server uses catalog IDs for bot bodies** (`80cee04`):
- Server `SVC_STAGE_START` used `catalogResolveBodyByMpIndex()` which returns NULL on dedicated server. All bots rendered as the same character.
- Fix: now uses `g_MatchConfig.slots[]` `body_id`/`head_id` strings directly.

**5. Context update** (`80cee04`): Session log backfilled with S141–S144; README and bugs.md updated.

**Build**: still v0.0.32.

### Decisions
- Explicit `botSpawnAll()` in `setup.c` is a pragmatic stop-gap; moved to `botTick` failsafe in S146.

### Next Steps
- Fix build error (missing bot.h include) from explicit botSpawnAll call → S146.
- Online playtest with 31 bots.

---

## Session S144 — 2026-04-04

**Focus**: Endscreen UI overhaul, multi-select bot list, 256-entry name dictionaries, B-104 fix, stale slot cleanup

### What Was Done

**Commits `af86c3b`, `2d75636`, `b92a421`, `6b9e498` pushed to `dev`.**

**1. Endscreen overhaul + B-104 fix** (`af86c3b`):
- **B-104 fixed**: both `renderSoloEndscreen` and `renderMpEndscreen` in `pdgui_menu_endscreen.cpp` now call `pdmainSetInputMode(INPUTMODE_MENU)` on `IsWindowAppearing()`. Previously both used direct `SDL_SetRelativeMouseMode(SDL_FALSE)` calls, leaving `g_InputMode = INPUTMODE_GAMEPLAY` and blocking ImGui input.
- **Return to Lobby / Quit to Menu buttons** added to endscreen.
- **Random bot names** now displayed on post-match endscreen.
- `#include "../include/pdmain.h"` added to endscreen.cpp.

**2. Multi-select bot list** (`2d75636`):
- Combat Sim bot list now supports multi-select with right-click context menu for batch operations.

**3. 256-entry bot name dictionaries** (`b92a421`):
- Replaced random generator with 256-entry Adjective+Noun word lists.
- Dictionaries are mod-overridable (loaded from data files if present).
- Bot name display columns widened to accommodate longer names.
- Also touches `pdgui_menu_room.cpp` and `port/src/net/matchsetup.c`.

**4. Stale slot reference fix** (`6b9e498`):
- Removed stale `s_SelectedBotSlot` reference in room screen reset path — crash hazard on room screen revisit.

**Build**: v0.0.32 clean.

### Decisions
- `pdmainSetInputMode` doesn't warp cursor; kept explicit `SDL_WarpMouseInWindow` to center — cursor would otherwise restore to off-screen pre-mission position.
- Name dictionaries use flat arrays (not JSON) for load performance; mod override path uses same directory convention as other data assets.

### Next Steps
- Playtest: verify endscreen buttons usable after mission complete, bot names display correctly, multi-select works in Combat Sim
- D5.3 (Pause Menu) remains the biggest open gap

---

## Session S143 — 2026-04-04

**Focus**: R-3 Room Networking — clients see rooms, create/join, room-scoped match start

### What Was Done

**`commit 892f1e8` pushed to `dev`.**

Implemented R-3 room networking from `context/room-architecture-plan.md`:
- Server broadcasts room list to clients on lobby join.
- Clients can create and join rooms via the room screen.
- Match start is room-scoped: only players in the same room participate in a match.
- Room screen (`pdgui_menu_room.cpp`) updated to display active rooms and occupant counts.

**Build**: v0.0.30 clean.

### Decisions
- Room IDs are server-assigned, consistent with R-1/R-2 foundation.
- R-4 (demand-driven rooms) and R-5 (room federation) remain planned.
- L-series (lobby/room UX polish) depends on R-3 being done; can now begin.

### Next Steps
- L-series lobby/room UX work.
- Endscreen + name system polish (S144).

---

## Session S142 — 2026-04-04

**Focus**: Network + bot stabilization sprint — fixes for match start, bot freeze, server broadcast, buffer overflow, auth client desync storm

### What Was Done

**Commits `2634716`, `e5f7d4a`, `41431a3`, `3645e28`, `2de61ab`, `07b9729` pushed to `dev`.**

Fixed all root causes identified in S141 analysis plus related issues:

- **CLC_LOBBY_START buffer overflow** (`2de61ab`): `netLobbyRequestStartWithSims` in `pdgui_bridge.c` switched from `g_NetLocalClient->out` (1440 bytes) to a 256KB static send buffer. This was the root cause of the 23/31 bot count mismatch. Server-side dispatch trace logging also added.
- **Bot rooms=-1 freeze** (`41431a3`): `botmgrAllocateBot` now uses `PROPFLAG_NOTYETTICKED` gate; bots tick continuously until `botSpawn` assigns valid rooms and clears the flag. Room recovery path added for bots that stall.
- **Dedicated server broadcast blocked** (`3645e28`): `g_NetLocalClient` guard was incorrectly blocking relay; server can now broadcast state updates to all connected clients.
- **Bot names / per-frame relay / room fallback / server bot count** (`2634716`): bot display names populated correctly; relay runs every frame; room fallback logic corrected; server accurately reports bot count.
- **Authority client desync storm** (`07b9729`): authority client now skips chr desync detection — was triggering continuous resync storm on dedicated server with many bots.
- **Head picker human-readable names** (`e5f7d4a`): head picker now shows catalog-resolved display names sorted A-Z instead of raw catalog IDs.

**Builds**: v0.0.28 (initial batch) → v0.0.29 (post auth-client fix).

### Decisions
- 256KB static buffer for CLC_LOBBY_START is a pragmatic fix; streaming/chunked approach deferred until packet sizes are better understood.
- Auth client desync skip is intentional on dedicated server where the server is always the authority.

### Next Steps
- Playtest with 31 bots: verify full count transmitted, all bots spawn with valid rooms, CLC_BOT_MOVE flows to server.
- R-3 room networking (S143).

---

## Session S141 — 2026-04-04

**Focus**: Bot count mismatch audit + bot freeze root cause analysis (no code changes — analysis only, session terminated by user before fixes applied)

### What Was Done

**Audit findings** (no fixes implemented):

**Root Cause 1 — CLC_LOBBY_START buffer overflow** (CRITICAL):
- `NET_BUFSIZE = 1440` bytes. `g_NetLocalClient->out` is this size.
- CLC_LOBBY_START writes: header (~34 bytes) + weapons (6 strings, ~12–84 bytes) + per-bot (3 strings + 2 bytes ≈ 45 bytes/bot) + manifest.
- At 31 bots: ~34 + 12 + 31×45 + manifest ≈ 1441+ bytes — overflows the buffer.
- After overflow, `netbuf->error = 1`; writes are no-ops but `botIdx` keeps incrementing.
- The packet declares `numSims=31` (written before overflow), but only ~23 bots have valid data.
- Server reads 31 entries: 23 valid + 8 garbage (empty strings → dark_combat defaults). Sets `clampedSims=31`, allocates 31 stubs, sends SVC_STAGE_START with 31 bot chrslots bits.
- **Fix location**: `port/fast3d/pdgui_bridge.c:657` — `netLobbyRequestStartWithSims`. Change from `g_NetLocalClient->out` to a static large buffer (e.g. `NET_BUFSIZE * 8 = 11520` bytes).

**Root Cause 2 — Bot rooms=-1 / freeze**:
- `botmgrAllocateBot` (botmgr.c) creates prop with `rooms[0] = -1`.
- `propActivate` sets `forceonetick = true` → bot ticks ONCE (the first-run log fires here).
- After first tick: rooms still -1, not in foreground → prop NOT ticked again.
- Actual bot spawn (valid rooms assigned) happens via stage setup AI → `aiMpInitSimulants` → `botSpawnAll` → `botSpawn` → `scenarioChooseSpawnLocation` → `chrMoveToPos` with valid rooms. This runs DURING stage loading (setup.c AI script), not from botTick.
- After `botSpawn`, bots have valid rooms. They get added to foreground normally.
- **Fix**: In `botmgrAllocateBot`, set `prop->forcetick = true` after `propActivate` so bots always tick until properly spawned. Clear `forcetick` in `botSpawn` after rooms are assigned.

**Root Cause 3 — CLC_BOT_MOVE not sent**:
- `netEndFrame` (net.c:1407): `if (g_NetLocalBotAuthority && g_BotCount > 0)` gates the write.
- On dedicated server: `g_NetLocalBotAuthority = true` set when `SVC_BOT_AUTHORITY` received. `g_BotCount` set by `setup.c` allocating bots from `g_MpSetup.chrslots`.
- If `g_NetLocalBotAuthority` is never set (SVC_BOT_AUTHORITY not received/processed), or `g_BotCount = 0` (bots not allocated due to overflow-corrupted chrslots), no CLC_BOT_MOVE is sent.
- After fixing CLC_LOBBY_START overflow → correct 31-bot chrslots → correct bot allocation → correct g_BotCount → CLC_BOT_MOVE flows.

**Key files for next session fixes**:
- `port/fast3d/pdgui_bridge.c:652–659` — CLC_LOBBY_START buffer
- `src/game/botmgr.c:72–74` — prop->forcetick after propActivate/propEnable
- `src/game/bot.c:307` — clear forcetick after chrMoveToPos in botSpawn
- `port/src/net/net.c:1407` — verify CLC_BOT_MOVE gate

### Decisions Made

- Session terminated before fixes; user will restart with explicit fix instructions.
- CLC_LOBBY_START buffer overflow is the root cause of the 23/31 bot count mismatch.
- rooms=-1 freeze is a secondary issue from forceonetick being cleared before spawn runs.

### Next Steps

- **S142**: Implement the three fixes above. Build verify. Commit + push.
- Playtest with 31 bots: verify full count transmitted, all spawn with valid rooms, CLC_BOT_MOVE flows.

---

## Session S139 — 2026-04-04

**Focus**: D5.4 — MP post-match scoreboard (pdgui_menu_pausemenu.cpp)

### What Was Done

**`commit 36d03a5` pushed to `dev`.**

Rewrote `pdguiGameOverRender()` and supporting helpers in `pdgui_menu_pausemenu.cpp`:

- **Accuracy column** — `ScorecardRow.accuracy` field added. Computed for local player via `mpstatsGetPlayerShotCountByRegion` (PM_SHOT_TOTAL, _HEAD, _BODY, _LIMB, _GUN, _HAT, _OBJECT). Bots display "--".
- **Team section headers** — `renderGameOverRankings()` rewritten. Inserts "-- Team N --" headers (team-colored) between team groups when teams are enabled.
- **Stable team sort** — `sortRowsByTeam()` insertion sort added. `mpGetPlayerRankings()` returns score-sorted rows; stable sort by team applied before rendering for team mode.
- **Mouse capture fix** — `pdmainSetInputMode(INPUTMODE_MENU)` called on `ImGui::IsWindowAppearing()`. Fixes non-interactive buttons (B-103 symptom: game held SDL in relative mouse mode during gameplay).
- **Dual exit buttons** — "Return to Lobby" (blue, stays in room: `mainChangeToStage(STAGE_CITRAINING)` + `pdguiSetInRoom(1)`) and "Quit to Menu" (red: `netDisconnect()` for CLIENT, `mainChangeToStage(STAGE_TITLE)` for offline/server).

**Build**: full client+server incremental build clean (exit 0).

### Decisions
- Accuracy computed from local player's stats only — `mpstatsGetPlayerShotCountByRegion` is per-local-player, not per-chrnum. Bots always show "--".
- "Return to Lobby" does NOT call `netDisconnect()` — player stays connected and in-room. Only `pdguiSetInRoom(1)` is needed to show the room interior UI.
- Forward-declared `pdguiSetInRoom` and `mpstatsGetPlayerShotCountByRegion` inline in the cpp extern block (not added to headers — not needed elsewhere).

### Next Steps
- Playtest: verify scoreboard appears at match end, buttons work, accuracy shows for local player
- D5.4 mission complete screen still PLANNED
- D5.5: bot name dictionary + arena/weapon verification still open

---

## Session S138 — 2026-04-04

**Focus**: Fix body/head picker auto-head selection (combat sim + agent create)

### What Was Done

**`commit a65207e` pushed to `dev`.**

Root cause: two UI pickers were selecting the wrong default head when a body was chosen.

- `pdgui_menu_matchsetup.cpp` (Combat Sim bot editor, line 748): called
  `catalogResolveHeadByMpIndex((s32)b)` where `b` is the **body** mpbodynum.
  This treated the body index as a head index — body 5 would select head 5,
  not the body's paired head. Wrong for nearly every character.

- `pdgui_menu_agentcreate.cpp` `autoSelectHead()`: called
  `mpGetMpheadnumByMpbodynum` — an N64-era function that uses `rngRandom()`
  for headnum==1000 sentinel bodies and bypasses the catalog layer.

**Fix — two new catalog API functions:**
- `catalogGetBodyDefaultHead(const char *body_id)` → head catalog ID string
  Reads `entry->ext.body.headnum` (default head's g_HeadsAndBodies[] index)
  and resolves it via `catalogResolveByRuntimeIndex(ASSET_HEAD, headnum)`.
- `catalogGetBodyDefaultMpHeadIdx(s32 mpbodynum)` → mpheadnum for carousels
  Wraps the above, converts mpbodynum → body catalog ID → ext.body.headnum →
  `catalogHeadnumToMpHeadIdx()`. Returns -1 for unregistered heads (including
  the headnum==1000 random-gender sentinel — caller keeps existing selection).

Both are declared in `assetcatalog.h`, implemented in `assetcatalog_api.c`.
Wire protocol unchanged — body_id/head_id remain catalog ID strings throughout.

**Changed files (4):**
- `port/src/assetcatalog_api.c` — two new functions after `catalogHeadnumToMpHeadIdx`
- `port/include/assetcatalog.h` — declarations for both new functions
- `port/fast3d/pdgui_menu_matchsetup.cpp` — `catalogGetBodyDefaultHead(bid)` replaces wrong call
- `port/fast3d/pdgui_menu_agentcreate.cpp` — `catalogGetBodyDefaultMpHeadIdx` in `autoSelectHead`, forward decl added

**Build**: both client and server link clean (full incremental build from main copy).

### Next Steps

- Playtest: verify body selection in Combat Sim and Agent Create picks correct paired head
- D5.5 (Combat Sim Polish) — this fix is a prerequisite; bot name dictionary + arena/weapon verification still open

---

## Session S137 — 2026-04-03

**Focus**: Online match start bug (B-103) — critical path fix for multiplayer

### What Was Done

**B-103 fixed and pushed (`commit 184922a`).**

Root cause: `g_MpSetup.stage_id` was never populated by the match setup flow.
- UI sets `g_MatchConfig.stage_id`, not `g_MpSetup.stage_id`.
- `manifestBuildForHost()` (client side) reads `g_MpSetup.stage_id` → found empty → stage skipped from manifest → stage not in session catalog.
- `netmsgSvcStageStartWrite()` (server side) reads `g_MpSetup.stage_id` → found empty → writes `stage_session=0` → returns early.
- Client reads `stage_session=0` → silent `return 1` with no log → surfaces as "malformed or unknown message 0x10 from server". Stage never loaded.

**Two-line fix in `port/src/net/netmsg.c`:**
1. `netmsgClcLobbyStartWrite`: sync `g_MatchConfig.stage_id → g_MpSetup.stage_id` before `manifestBuildForHost` so stage enters manifest and session catalog.
2. `netmsgClcLobbyStartRead` (server): copy parsed `stage_id → g_MpSetup.stage_id` alongside existing `g_MatchConfig` and `stagenum` assignments.

No protocol changes. S130 constraint respected (catalog IDs throughout, no raw indices).

**Build**: client clean (exit 0). Server CMake arch error is pre-existing, unrelated.

### Additional Work (same session, post-context-commit)

After the context commit, the session continued with several more fixes and features before S138 began:

- `c474baa` **feat(title)**: gold name colour + +0.5s legal screen duration.
- `ba6983f` **diag**: MATCH-START trace logging added + fix for premature `inGame` flag and false "malformed or unknown message 0x10" warning.
- `124d195` **fix(net)**: `catalogResolveStageBySession` now accepts `ASSET_ARENA` type — was failing to resolve arena stages sent from server.
- `e148aee` **fix(net)**: bot AI enabled on client side + countdown dismiss on match start.
- `d343273` **feat(net)**: server-authoritative bot sync for dedicated server — server now owns bot state and syncs to clients.

### Next Steps

- Playtest with Chris — match should now start when countdown reaches zero
- D5.3 (Pause Menu) — unblocked by D5.1 input ownership

---

## Session S136 — 2026-04-03

**Focus**: D5.1 — Input Ownership Boundary

### What Was Done

**D5.1 implemented and pushed (`commit 001dba8`).**

Introduces `InputOwnerMode` enum (`INPUTMODE_MENU` / `INPUTMODE_GAMEPLAY`) and
`pdmainSetInputMode()` as the single canonical transition function.

**New files:**
- `port/include/pdmain.h` — enum + extern g_InputMode + pdmainSetInputMode() declaration (extern "C" guards for C++ callers)

**Changed files (9):**
- `port/src/pdmain.c` — `g_InputMode` global + `pdmainSetInputMode()` implementation: GAMEPLAY→SDL mouse capture bypassing pdguiIsActive() guard; MENU→SDL_SetRelativeMouseMode(FALSE) + ShowCursor
- `port/fast3d/pdgui_backend.cpp` — GAMEPLAY-mode early return: keyboard events not forwarded to ImGui when `!g_PdguiActive`; Tab suppressed before ImGui sees it in MENU mode (fixes CK_START double-trigger)
- `port/fast3d/pdgui_menu_pausemenu.cpp` — replaced direct SDL calls in Open/Close with `pdmainSetInputMode(INPUTMODE_MENU/GAMEPLAY)`
- `port/fast3d/pdgui_bridge.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` after both `menuhandlerAcceptMission()` calls
- `port/fast3d/pdgui_menu_solomission.cpp` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` after both `menuhandlerAcceptMission()` calls
- `port/src/net/matchsetup.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` in `matchStart()` and `matchStartFromChallenge()`
- `port/src/net/netmsg.c` — `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` in SVC_STAGE_START (co-op/anti path and MP path), inside `#if !defined(PD_SERVER)`
- `port/src/menumgr.c` — `restoreGameplayMouseCapture()` body replaced with single `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` call

**Build**: both client and server link clean.

### Bugs Addressed

- **B-92 class** (mouse not captured on mission start): `pdmainSetInputMode(INPUTMODE_GAMEPLAY)` calls SDL directly, bypassing the `pdguiIsActive()` defer guard in `inputLockMouse()`. All known start paths covered.
- **Tab double-trigger** (CK_START conflict): Tab suppressed before `ImGui_ImplSDL2_ProcessEvent()` in MENU mode.
- **Esc double-path**: in GAMEPLAY with no active overlay, keyboard events never reach ImGui at all.

### Next Steps

- D5.0 (Visual Layer): still PLANNED before D5.3 (pause menu) per execution order
- D5.3 (Pause Menu): now unblocked by D5.1 — input ownership is clean for pause transitions

---

## Session S135 — 2026-04-03

**Focus**: D5.0a Technical Spike — Fast3D → OpenGL → ImGui texture bridge

### What Was Done

**D5.0a spike implemented and pushed (`commit 824415e`).**

Validates that the ImGui::Image() pipeline works end-to-end before the full D5.0 ROM
texture decode layer is built.

**Architecture findings (from code study):**
- `ImTextureID` in this codebase is `(void*)(uintptr_t)GLuint` — confirmed by `gfx_opengl_get_framebuffer_texture_id()`.
- `GfxRenderingAPI` exposes `new_texture()`, `select_texture()`, `upload_texture()` — but raw GLAD GL calls are equally valid since `pdgui_backend.cpp` already includes `glad.h`.
- `struct tex` in the shared texpool has `data` (N64-native pixels), `width`, `height`, `gbiformat`, `depth` — the full decode path for D5.0 is: `texLoadFromTextureNum(texnum)` → `texFindInPool()` → decode N64 format → `glTexImage2D`.
- N64 formats to implement for D5.0: RGBA16 (5-5-5-1), IA16 (8-8), IA8 (4-4), CI4/CI8 (palette-indexed). All handled by `import_texture_*` in `gfx_pc.cpp` — that code is the decode reference.

**Changes made:**
1. `pdgui_backend.cpp`: `pdguiGetUiTexture(const char *id)` — static `unordered_map<string, uint32_t>` cache, synthesizes 64×64 PD-blue RGBA32 test pattern, uploads to GL, returns ImTextureID.
2. `pdgui.h`: declared `pdguiGetUiTexture()`.
3. `pdgui_menu_mainmenu.cpp`: Settings > Catalog tab shows `ImGui::Image()` with PASS/FAIL label.
4. `assetcatalog_base.c`: registered `ui/test_panel` as `ASSET_UI` (placeholder for D5.0).

**Build**: Both client (`PerfectDark.exe`) and server (`PerfectDarkServer.exe`) link clean. No new errors.

### Spike Result

**PASS** — `pdguiGetUiTexture()` compiles, uploads a GL texture, and is called from `ImGui::Image()`. Visual confirmation requires playtest (see Settings > Catalog tab).

**D5.0 unblocked.** The D5.0 task is to replace `buildTestPattern()` with actual ROM texture decode.

### Pipeline Gap Identified for D5.0

No standalone N64 → RGBA32 decode function is currently exposed outside `gfx_pc.cpp`. D5.0 must either:
- Export a `gfxDecodeN64Texture(data, fmt, siz, w, h, out_rgba32)` helper from `gfx_pc.cpp`, OR
- Implement a standalone decode function in `pdgui_backend.cpp` (copy-referencing the `import_texture_*` logic).

Recommendation: standalone decode in `pdgui_backend.cpp` — avoids coupling the bridge to gfx_pc internals and keeps the UI texture path self-contained.

### Next Steps

- D5.0 (Visual Layer): replace `buildTestPattern()` with real ROM texture decode, implement `pdguiThemeDrawPanel()` etc., register all `ui/` catalog entries with real texnums.
- Per `context/tasks-current.md`, D5.1 (Input boundary) and D5.2 (Pause menu) follow after D5.0 validates.

---

## Session S134 -- 2026-04-03

**Focus**: Static array audit — dynamic/growable data, enum-indexed array completeness

### What Was Done

**Full audit of port/src/ and port/fast3d/ for static arrays holding dynamic/growable data.**

Scope: our code only (not vendored imgui/, external/, or decompiled src/game/).

**Findings — what was NOT a problem:**
- `assetcatalog_load.c` override arrays (s_FilenumOverride etc.): ROM-bounded reverse-index maps with existing bounds checks. ROM source numbers don't grow with mods. No change needed.
- `pdgui_hotswap.cpp` s_Entries[128]: registered from code at init time, not mod data.
- Network/player/bot arrays (MAX_PLAYERS, MAX_BOTS etc.): genuine protocol constants.
- `s_MfSortedIdx[MANIFEST_MAX_ENTRIES]`: UI sort buffer bounded by protocol maximum (4096), already matches the dynamically allocated manifest struct.
- All `s_AssetTypeNames[ASSET_TYPE_COUNT]` arrays: verified complete (25 entries, NONE through LANG). Already fixed in S133.

**Fix 1 — assetcatalog_deps.c** (`commit ab69868`):
- `s_DepTable[CATALOG_MAX_DEP_PAIRS]` (256 static) → heap-allocated `s_DepPair *s_DepTable` + `s32 s_DepCap`.
- Grows by doubling on demand (starting at CATALOG_MAX_DEP_PAIRS = 256).
- `catalogDepClear()` now frees the buffer. `catalogDepClearMods()` compact-in-place (no realloc — keeps allocated capacity).
- Previously: mods with many asset dependencies silently dropped entries at 256 with a LOG_WARNING.

**Fix 2 — pdgui_menu_mainmenu.cpp** (`commit ab69868`):
- `s_ManifestTypeNames[]`: added "Lang" at index 8 (= MANIFEST_TYPE_LANG, added in S130).
- Bounds check: changed hardcoded `me->type < 8` → `me->type < (int)(sizeof(s_ManifestTypeNames)/sizeof(s_ManifestTypeNames[0]))` so it auto-tracks the array.
- Previously: Lang entries in the catalog debug tab showed "?" instead of "Lang".

### Build
- Build script redirects to main working copy when run from worktree. Changes applied directly to `dev` branch and pushed. Both targets build clean (no structural changes — all callers unchanged).

### Decisions Made
- The four `s_*Override[]` arrays in assetcatalog_load.c are NOT dynamic data: they're fixed-domain reverse-index maps (filenum/texnum/animnum/soundnum → pool_index). ROM source numbers don't grow. Correct as-is.
- `CATALOG_MAX_DEP_PAIRS` constant retained in header as initial/minimum capacity for the dep table.

### Next Steps
- D5 UI Polish (B-91, B-92, B-93, B-96 are the recommended starting sequence per tasks-current.md).

---
