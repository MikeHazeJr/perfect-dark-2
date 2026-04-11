# Active Tasks -- Current Punch List

> Razor-thin: only what needs doing. Completed work archived in `_archive/tasks-archive.md`.
> For phase status, see [infrastructure.md](infrastructure.md). For bugs, see [bugs.md](bugs.md).
> Back to [index](README.md)

---

## v0.1.0 "Foundation" Release Prep

### Input System

#### Done (S189 + pre-S189)

| Item | Status | Detail |
|------|--------|--------|
| **B-button opens doors** | FIXED (S189) | bondmove.c:1836 — PC usemask = BUTTON_ACCEPT_USE only. BUTTON_CANCEL_USE==B_BUTTON was included, causing B to trigger door open. |
| **Gamepad layout: A=jump, Y=use, B=crouch** | FIXED (S189) | actionmap.cpp: Y→USE, A→JUMP, B→CROUCH (dual with CANCEL_USE). RSTICK unbound from crouch. |
| **MP 1-3 default gamepad binds removed** | DONE (S189) | setupGameplayDefaults else block removed. MP slots start unbound, rebind UI still works. |
| **CrouchMode** | ALREADY DONE (pre-S189) | Game.Player%d.CrouchMode: 0=hold (default), 1=analog, 2=toggle, 3=toggle+analog. bondmove.c:1925. |

#### Completed in S190 (confirmed in clean build)

| Item | Status | Detail |
|------|--------|--------|
| **unk14 gate removed** | DONE (S190) | bondmove.c:~1551 — `unk14 = true` unconditional in CONTROLMODE_PC. Was gated on c2stick, breaking left-stick strafe when right stick idle. |
| **canlookahead gate removed (ADS)** | DONE (S190) | bondmove.c:~1633 — `canlookahead = true` unconditional in ADS block. Was c2stick-gated. |
| **canlookahead gate removed (non-ADS)** | DONE (S190) | bondmove.c:~1642 — `canlookahead = !insightaimmode` only; stick gate removed. |
| **FarSight strafe — left stick** | DONE (S190) | bondmove.c:~2104 — FarSight strafe reads `c1stickxsafe` (left stick), not `c2stickx` (aim stick). |
| **LSTICK=Sprint define + bind** | DONE (S190) | actionmap.cpp — `JBTN_LSTICK`/`JBTN_RSTICK` defines added; LSTICK click → ACTION_SPRINT. |
| **_dev-window.ps1 restored** | DONE (pre-S190) | Restored from 68c0b186 after truncation (2311→2232 lines). Em-dashes → hyphens to prevent re-truncation. |

#### Completed in S189

| Item | Status | Detail |
|------|--------|--------|
| **usemask cleanup** | DONE (S189) | bondmove.c:1836 — PC usemask = `BUTTON_ACCEPT_USE` only. Confirmed: BUTTON_CANCEL_USE == B_BUTTON, both excluded. |
| **P0-only binding refactor** | DONE (S189) | All setup*Defaults: Player 0 only, no p=0..3 loops. Init loop replaced with single `setupGameplayDefaults(0)` / `setupVehicleDefaults(0)`. |
| **Bind 1 / Bind 2 collapse** | DONE (S189) | Not structural — flat `triggers[4]` array. No struct change needed. Each action now has 1 kbd + 1 gamepad default max. Dead MENU_UP/DOWN/LEFT/RIGHT binds removed from menu IMCs. |
| **Menu IMC 3-action reduction** | DONE (S189) | g_ImcMenu + g_ImcPauseMenu: ACTION_USE (Return/A), ACTION_CANCEL_USE (Escape/B), ACTION_PAUSE (Start only). 5 triggers total, player 0 only. |
| **crouch_mode in pd.ini** | ALREADY DONE (pre-S189) | `Game.Player%d.CrouchMode`: 0=hold (default). bondmove.c:1925 handles toggle/hold/analog. No changes needed. |

#### TODO — Verification Pass (Mike, post-reset)

| Item | Priority | Detail |
|------|----------|--------|
| **A jumps, does NOT open doors** | HIGH | Verify A=JUMP binding and usemask fix. B should crouch, not open doors. |
| **Y opens doors / interacts** | HIGH | Y=USE is the new interact button. |
| **B crouches, does NOT open doors, exits FarSight/scope** | HIGH | Dual-bind CROUCH + CANCEL_USE. usemask fix ensures B excluded from door mask. |
| **R3 does nothing** | HIGH | RSTICK click is unbound. |
| **LSTICK click sprints** | HIGH | New LSTICK → ACTION_SPRINT binding. |
| **Rebind UI: single-column, MP slots 1–3 unbound** | HIGH | No Bind1/Bind2 split; MP players 1–3 have no default gamepad binds. |
| **CrouchMode=2 toggle works, resets on respawn** | MED | `Game.Player0.CrouchMode=2` in pd.ini. bondmove.c:1925 handles it. |
| **Mission 1 completion: no crash, JSON save written** | HIGH | B-129 fix. `saves/agent_<name>.json` must be updated on mission end. |
| **Sky tearing gone on outdoor stages** | HIGH | B-128 fix. Test on Dark Noon, Goldfinger 64 exteriors. |
| **B-18 check on Skedar Ruins** | MED | Does sky still show pink? B-128 fix may or may not cover B-18. Report result. |

### Must-Have

| Item | Priority | Status | Detail |
|------|----------|--------|--------|
| **D5 Phase 3 -- Remaining menu screens** | HIGH | IN PROGRESS | Full audit complete (S188): 254 dialogs found, 79 screens across 11 batches, ~13-18 sessions. Plan in `context/designs/menu-replacement-plan.md`. No split-screen (2P cancelled). CI Options absorbed into unified Settings. **Batch 6 DONE (S204/2026-04-11)**: Bot/Simulant Setup (5 dialogs) -- `g_MpSimulantsMenuDialog`, `g_MpAddSimulantMenuDialog`, `g_MpChangeSimulantMenuDialog`, `g_MpEditSimulantMenuDialog`, `g_MpSimulantCharacterMenuDialog`. NEW `port/fast3d/pdgui_menu_botsetup.cpp` (+1006 lines) using the s204 shadow-struct call-through pattern (extends s203 with a `carousel` variant for the two MENUITEMTYPE_CAROUSEL head/body handlers). Every state mutation delegates to legacy handlers in setup.c: `mpAddChangeSimulantMenuHandler` (grouped profile list with GETOPTGROUPCOUNT/GETOPTGROUPTEXT/GETGROUPSTARTINDEX/LISTITEMFOCUS), `menuhandlerMpSimulantHead/Body` (carousels → `mpCharacterHeadMenuHandler`/`mpCharacterBodyMenuHandler`), `mpBotDifficultyMenuHandler`, `menuhandlerMpChangeSimulantType`, `menuhandlerMpCopySimulant/DeleteSimulant/AddSimulant/SimulantSlot/ClearAllSimulants`. Dynamic-text function pointers (`mpMenuTextSimulantName`, `mpMenuTextSimulantDescription`, `mpMenuTitleEditSimulant`) invoked via shadow-cast. Mid-task pivot: per Mike's guidance, refactored the Simulants roster body into a public `pdguiBotSetupDrawSimulantsBody` inline helper (NEW header `port/include/pdgui_menu_botsetup.h`, 47 lines) and added a `CollapsingHeader("Simulant Profiles")` section at the bottom of `renderPlayerPanel` in `pdgui_menu_room.cpp` (+14 lines) that calls the helper inline — surfaces the legacy `g_BotConfigsArray` pool inside the room screen alongside the existing matchslot-based bot UI (additive, no removal). `renderMpSimulants` remains as a thin modal wrapper for the legacy Combat Simulator push path so linking + zero-function-loss are preserved. Client 49,202,510 → 49,340,784 (+138,274 bytes, ~135 KB); server unchanged 22,788,944. `pdgui_menus.h` +2. **Batch 6 polish DONE (S207/2026-04-11)**: live 3D head/body preview restored in `renderMpSimulantCharacter` via the reusable `pdguiModelPreviewDraw` widget (Batch-0 infra, also used by `pdgui_menu_agentcreate.cpp`). Two-column layout — 300x340 preview panel on the left (selection resolved to catalog ID strings via `catalogMpHeadId`/`catalogMpBodyId`), Head+Body dropdowns on the right, dialog size bumped from 0.55x0.58 to 0.62x0.62. Idle turntable rotation at 0.4 rad/s, instant swap on dropdown change, silhouette placeholder on first frame. Zero legacy function loss: the preview is produced by `menuRenderModel` (legacy), routed to a 256x256 FBO via the existing `pdguiCharPreviewRenderGBI` hook called from `src/game/menu.c:3754`. `menudialog0017ccfc::MENUOP_TICK` still fires via runtime and writes `g_Menus[0].menumodel.newparams` — our per-frame `pdguiCharPreviewRequest` overwrites with the same value so no conflict. Only file touched: `port/fast3d/pdgui_menu_botsetup.cpp` 1006→1074 (+68 lines). No header / other-file edits. Ran in parallel with Batch 8 on worktree `busy-lalande`, rebased onto dev after Batch 8 merged. Approach notes in `context/scratch/B6-head-preview-2026-04-11.md`. **Batch 0 DONE (S192)**: pdgui_layout primitive (docked action bar + popup scrim), pdguiModelPreview generalized to CHARACTER/WEAPON/VEHICLE/PROP, Mission Select Start Mission docked, Mission Difficulty dialog text-missing regression fixed, Challenges Accept Challenge docked. **Batch 1 DONE (S193)**: seven "low-hanging fruit" dialogs — ExitGame (literal-text fix), PdModeSettings (slider support in fallback), MpEndGame (scrim upgrade), and four filemgr pak-era placeholders redirecting to Agent Select.  1080p scaling baseline flipped in same session (foundation-layer fix). **Batch 2 DONE (S194)**: Co-op / Counter-Op flow — `g_CoopMissionDifficultyMenuDialog`, `g_CoopOptionsMenuDialog`, `g_AntiMissionDifficultyMenuDialog`, `g_AntiOptionsMenuDialog`.  New renderers in `pdgui_menu_solomission.cpp` (2650→3351, +701): shared `renderCoopAntiDifficultyImpl` (cloned from `renderDifficulty`, no PD Mode row, pushes Options instead of AcceptMission) + shared `renderCoopAntiOptionsImpl` (scrim + docked action bar primitive, Radar/FriendlyFire checkbox rows, Perfect Buddy / Main Player dropdown row, delegates all state to legacy menuhandler* functions so getMaxAiBuddies / modifiedfiles dirty flag / connected-controller math stays in one place).  Client builds green 48,716,058 bytes, server builds green 22,786,384 bytes.  Foundation primitives in place for Batches 3-12. **Batch 3 DONE (S195/2026-04-11)**: Unified Settings absorbs all CI Options content. Redirect plumbing: `renderCiSettingsRedirect` registered for 5 CI dialogs (Options/PC, Options/Pause, ControlOptions, ControlStyle, Display) → routes to unified Settings on matching sub-tab. Dead P2 variants (ControlStyleP2, DisplayP2, ControlP2) get `renderCiDeadPlayer2` (auto-pop + notice). Content absorption: all CI Display items and CI Control items already in unified settings. New: Sound Mode dropdown added to Settings → Audio. `pdgui_menu_mainmenu.cpp` 3100→3123 (+23). **Batch 4 DONE (S202/2026-04-11)**: Cheats & Cinema (10 dialogs). NEW `port/fast3d/pdgui_menu_cheats.cpp` (+903 lines) consolidating 9 legacy cheats dialogs into a tabbed ImGui hub: root `renderCheatsHub` (Fun/Gameplay/Jo Solo Weapons/Classic Weapons/Weapons/Buddies), 6 sub-dialog `renderCheatsSubRedirect` (pops legacy push, flips tab), `renderCheatsWarning`, `renderCheatsConfirmUnlock`. ALL state writes delegate to legacy `cheatCheckboxMenuHandler` / `cheatMenuHandleBuddyCheckbox` / `cheatMenuHandleTurnOffAllCheats` / `gamefileUnlockEverything` via the s202 shadow-struct call-through pattern (cloned from s194 in solomission.cpp). `cheatMenuHandleDialog` OPEN/CLOSE side effects (func0f14a52c HUD darken + piracy check + GAMEFILEFLAG_USED_TRANSFERPAK) preserved automatically via menu runtime. Cinema: `renderCinemaList` in `pdgui_menu_mainmenu.cpp` (+248 lines) delegates to `menuhandlerCinema` via `cn_*` shadow pattern for all MENUOP_* opcodes. `pdgui_menus.h` +1 line. Client 48,920,337→49,051,951 (+131,614 bytes); server unchanged 22,788,944. **Batch 5 DONE (S203/2026-04-11)**: MP Setup Core (14 dialogs) -- Arena / Scenario (+QuickTeam variant) / Weapons / SelectRandomWeapons / QuickTeamWeapons / Limits / 6 Scenario-Options (Combat/CTC/HTM/HTB/KOH/PAC) / ExtGameOptions. NEW `port/fast3d/pdgui_menu_mpsetup.cpp` (+1265 lines) using the s203 shadow-struct call-through pattern (ABI covers checkbox/dropdown/list/slider handlerdata variants). Every state mutation delegates to legacy handlers in setup.c / scenarios.c / scenarios/*.inc: `mpArenaMenuHandler`, `scenarioScenarioMenuHandler`, `menuhandlerMpWeaponSetDropdown`, `menuhandlerMpWeaponSlot`, `menuhandlerMpSelectRandomWeapons`, `menuhandlerMpAutoRandomWeapon`, `mpSelectRandomWeaponListHandler`, `menuhandlerMpTimeLimitSlider`, `menuhandlerMpScoreLimitSlider`, `menuhandlerMpTeamScoreLimitSlider`, `menuhandlerMpRestoreScoreDefaults`, `menuhandlerMpCheckboxOption`, `menuhandlerMpDisplayTeam`, `menuhandlerMpOneHitKills`, `menuhandlerMpSlowMotion`, `menuhandlerMpHillTime`. The 6 scenario-option dialogs collapse to one shared `renderMpScenarioOptionsImpl` dispatched by a variant enum; their legacy `nextsibling` → `g_ExtGameOptionsMenuDialog` tab page flattens into a docked "More Options..." action-bar button (Batch 2 precedent). `pdgui_menus.h` +2 lines. `pdgui_menu_solomission.cpp` and `pdgui_menu_room.cpp` untouched. Client 49,051,951 → 49,202,510 (+150,559 bytes, ~147 KB); server unchanged 22,788,944. MENUOP_* block seeded complete up-front so the Batch 4 mid-flight gotcha did not repeat. **Batch 7 DONE (S205/2026-04-11)**: MP Advanced/Quick paths (11 dialogs) -- `g_MpAdvancedSetupMenuDialog` + `ViaAdvChallenge` variant, `g_MpQuickGoMenuDialog`, `g_MpQuickTeamGameSetupMenuDialog`, `g_MpQuickTeamMenuDialog`, `g_MpStuffMenuDialog` + `ViaAdvChallenge` variant, `g_MpPlayerSetupViaAdv`/`ViaAdvChallenge`/`ViaQuickGoMenuDialog`. NEW `port/fast3d/pdgui_menu_mpadvanced.cpp` (+1059 lines) using the s205 shadow-struct call-through pattern (cloned from s203/s204, dropdown/list/slider/checkbox helpers + new `hubPushRow`/`hubHandlerRow` helpers that draw a full-width selectable with a manual two-column text overlay for hubs showing label + dynamic right-side text). Six renderer impls backing 11 dialog registrations: `renderMpAdvancedSetup`/`...ViaChallenge` share an impl (item lists byte-identical); `renderMpQuickGo` (4 pure pushes); `renderMpQuickTeam` (5 big-font selectables calling `menuhandlerMpQuickTeamOption`); `renderMpQuickTeamGameSetup` (15 items — Scenario/Options/Arena/Weapons/Limits pushes, Player 1..4 Team dropdowns via `menuhandlerPlayerTeam` param=player#, NumSims/SimsPerTeam/SimDifficulty dropdowns via CHECKHIDDEN gating, Finished Setup calling `menuhandlerMpFinishedSetup` → `func0f17f428` → `mpConfigureQuickTeamPlayers`); `renderMpStuff`/`...ViaChallenge` share impl (Soundtrack/TeamNames pushes, Lock/Split dropdowns via `menuhandlerMpLock`/`menuhandlerScreenSplit`, Start/Drop/Abort pushes — split is PC-dead but retained for zero-function-loss parity); `renderMpPlayerSetupHubImpl` used by all three Via* variants, dynamic text from `mpGetCurrentPlayerName` / `mpMenuTextSavePlayerOrCopy` invoked with nullptr (bodies ignore item and read globals). **Network match start/end wiring audit** (Mike's requirement): 13 fields traced writer → backing global → start reader (`mpStartMatch` / `mpConfigureQuickTeamPlayers` / `mpConfigureQuickTeamSimulants` / `SVC_STAGE`) → end reader (`g_Vars.playerstats` / `g_BotConfigsArray` / endscreen). All WIRED or N/A — no shadow/cached copy introduced; all writes land in g_Vars/g_MpSetup/g_PlayerConfigsArray exactly where the legacy renderer already landed them. Modern room lobby (room.cpp) is untouched and has no references to any Batch 7 dialog, so no interleaving risk. Full audit table + per-row verdicts in `context/scratch/D5-P3-batch7-2026-04-11.md`. Menu dialog OPEN side effects (`menudialogMpGameSetup` setting `g_Vars.mpsetupmenu = MPSETUPMENU_ADVSETUP` / `usingadvsetup = true`; `menudialogMpQuickGo` setting `MPSETUPMENU_QUICKGO`) fire through legacy menu runtime because hot-swap hooks only RENDER. Client 49,340,784 → 49,566,481 (+225,697 bytes, ~220 KB); server unchanged (pdgui_menu_mpadvanced not in SRC_SERVER). `pdgui_menus.h` +2 lines. `pdgui_menu_room.cpp` untouched. **Batch 8 DONE (S206/2026-04-11)**: MP Pause & In-Game (6 dialogs) -- `g_MpPauseControlMenuDialog`, `g_MpPauseInventoryMenuDialog`, `g_MpPausePlayerStatsMenuDialog`, `g_MpPausePlayerRankingMenuDialog`, `g_MpPauseTeamRankingsMenuDialog`, `g_MpPlayerOptionsMenuDialog`. NEW `port/fast3d/pdgui_menu_mppause.cpp` (+1210 lines) using the s206 shadow-struct call-through pattern (cloned from s205). Six renderer impls: `renderMpPauseControl` (11-item hub — challenge/scenario/limit labels with CHECKHIDDEN, live match-time readout via `menutextMatchTime`, PC-dead pause toggle via `menuhandlerMpPause`, netplay-only team dropdown via `menuhandlerNetTeamSwitch`, netplay-only Controls push-row via `menuhandlerNetPauseControls`, End Game push to `g_MpEndGameMenuDialog`); `renderMpPauseInventory` (LIST via `menuhandlerInventoryList` MENUOP_GETOPTIONCOUNT/TEXT/SET/GETSELECTEDINDEX with `unk04=0` to match legacy equip semantics, marquee description via `mpMenuTextWeaponDescription`); `renderMpPausePlayerStats` (Stats-For dropdown via `mpStatsForPlayerDropdownHandler`, kills-vs-deaths ImGui table over `mpGetPlayerRankings` / `mpchr->killcounts[]` with selected player's suicides as header); `renderMpPausePlayerRanking` (ImGui ranking table over `mpGetPlayerRankings`); `renderMpPauseTeamRankings` (ImGui team table over `mpGetTeamRankings` + new bridge accessor `pdguiMppGetTeamName` in `pdgui_bridge.c`); `renderMpPlayerOptions` (4 checkboxes via `menuhandlerMpDisplayOptionCheckbox` with MPDISPLAYOPTION_* param3 masks, writes `g_PlayerConfigsArray[g_MpPlayerNum].base.displayoptions`). **Network match start/end wiring audit** (Mike's requirement): 9 fields traced, only ONE propagates on the wire (in-match team switch → `g_NetLocalClient->settings.team` + `netClientSettingsChanged()` → `CLC_SETTINGS` serializer → server's authoritative client record → broadcast to other clients). All other writes are local (displayoptions per-player read by scenarios.c/radar.c per-frame; stats-for-player view state; inventory equip uses same gun-cycle path as normal input; pause toggle PC-dead). Full audit table + per-row verdicts in `context/scratch/D5-P3-batch8-2026-04-11.md`. Two-word edit in `src/game/mplayer/ingame.c`: removed `static` from `menuhandlerNetTeamSwitch` + `menuhandlerNetPauseControls` so the C++ renderer can call them through the s206 function-pointer delegate (they have no other callers, grepped clean). `pdgui_bridge.c` +14 lines for the team-name accessor. `pdgui_menus.h` +2 lines. `pdgui_menu_room.cpp`, `pdgui_menu_solomission.cpp`, and `pdgui_menu_mpingame.cpp` (kill-ticker overlay) untouched. Client 49,566,481 → 49,726,176 (+159,695 bytes, ~156 KB); server 22,771,518 → 22,773,054 (+1,536 bytes, build-order variance — mppause.cpp / bridge / ingame.c are all outside SRC_SERVER whitelist). |
| **1080p scaling baseline flip** | HIGH | DONE (S193) | `pdgui_scaling.h` now uses `displayH / 1080.0f` (was 720p).  312 `pdguiScale()` literals across 17 menu files rewritten ×1.5 to preserve visual output.  Action-bar metrics in pdgui_layout.cpp updated to 1080p values (64px button, 84px bar).  `pdgui_backend.cpp` safe-area fallback flipped to 1920×1080.  Migration note in `context/designs/scaling-baseline-1080.md`.  Client builds green. |
| **Mission Select "Start Mission" scrolls off-screen** | HIGH | FIXED (S192) | Root cause: Start button rendered inside `##ms_right` BeginChild with fragile hardcoded layout math. Fix: split right panel into pinned header + difficulty picker + scroll body + docked action bar using new `pdguiBeginActionBar` primitive. Start button always visible regardless of scroll state. |
| **Mission Difficulty dialog "text missing" regression** | HIGH | FIXED (S192) | Root cause: Selectable used invisible `##diff_row` label and drew text via `dl->AddText` overlay; when `langSafe()` returned "" the row appeared blank. Fix: real `ImGui::Selectable(labelStr, ...)` with hardcoded English fallbacks ("Agent", "Special Agent", "Perfect Agent", "PD Mode", "Cancel"). |
| **Challenges "Accept Challenge" scrolls off-screen** | MED | FIXED (S192) | Same class of bug as Mission Select. Fix: split `##chal_detail` into inner scroll body + `##chal_action_bar` docked region. |
| **D5 Phase 4 -- Theme System** | HIGH | IN PROGRESS (S196) | Auto-extract base-ui textures at runtime (no CLI flag) — DONE pre-S196. S196 added: base-game template mod concept (`is_template` + `tags` in modinfo_t + modmgr parser), nineslice pipeline infrastructure lift (TGA loader uncapped, per-edge modes, src_inset/dst_corner_px split), hand-authored test chrome mod at `mods/base-game/ui-chrome/`, Settings → Video → UI Chrome Style dropdown (Procedural/Classic), render-branch toggle in `pdguiDrawPdDialog`. **Visible chrome rendered: YES** (pending Mike's in-game verification). Deferred follow-ups: `modmgrSaveAs` / Modding Hub template grouping / base-ui extractor migration to template layout. |
| **B-112 root cause** | HIGH | INVESTIGATING (S191) | Chr pointer corruption in 31-bot matches. S191: entry guard added at top of chraTick (CHR.GUARD channel); g_ChrLastTickedIndex slot-tracker in chr.c; SIGABRT handler reads index. Guards in place; awaiting next 31-bot crash log with chr slot ID. |
| **B-126 silent crash** | HIGH | INVESTIGATING (S191) | Silent crash ~8min into MP. S191: heartbeat 60s→30s; NET.WATCHDOG per-peer dump via netHeartbeatLog(); SIGABRT handler logs chr index. Awaiting next repro to confirm SIGABRT vs other kill path. |
| **B-129 mission-end AV crash** | HIGH | FIXED (S190 + S198) | S190: `endscreen.c` called `filemgrSaveOrLoad` → no Pak on PC → fn-ptr cast to lang index → AV. Replaced with `saveSaveAgent()`; three filemgr dialogs registered as noop. S198: `saveInit()` was never called from `main.c`/`server_main.c`, so saves landed at drive root and `besttimes[]` never persisted. Wired `saveInit()` into both startup sequences (commit `3fc345bf`). Saves now write to AppData. |
| **D13 -- Update System parse diagnosis** | MED | IN PROGRESS (S199) | v0.0.75 not in update list; "couldn't parse" on Check for Updates. Instrumented: HTTP code + raw response preview logged on failure, per_page 30→100. Root cause likely GitHub rate-limit returning 403 object (not array). Next step: reproduce and check log for `UPDATER: GitHub API HTTP NNN`. See `context/scratch/updater-parse-diagnosis-2026-04-11.md`. |
| **Build verification + QC pass** | MED | PLANNED | Clean build on dev, all QC tests from qc-tests.md passing, no known crash bugs. |

### Should-Have

| Item | Priority | Status | Detail |
|------|----------|--------|--------|
| **M3 -- Online MP flow** | MED | PLANNED | Lobby polish, room list UX, leader election, Quick Play button. R-3 done unblocks this. |
| **Prop sync event-driven** | MED | PLANNED | Current CRC polling. Should fire on pickup/door events per game director. |
| **B-78 chat rate limiting** | MED | FIXED (2026-04-11) | `CHAT_MSG_MAX_LEN 255` + length check added to `netmsgClcChatRead`. Dead `tmp[1024]` (B-84) also removed. Commit cb6f4763 on dev. |
| **B-81 JSON recursion guard** | MED | FIXED (S200) | `S_MAX_DEPTH 64` depth guard in `s_skip_value` + `SAVE_MAX_FILE_BYTES 256KB` file size cap. Both attack vectors closed. |
| **B-118 CI intro cutscene crash** | MED | OPEN | 56 models missed by SP manifest pre-scan. |

---

## Open Bugs (by severity)

### HIGH
| Bug | Description | File |
|-----|-------------|------|
| **B-112** | Chr pointer corruption in 31-bot matches (guards in place, root cause unknown) | chraction.c, chr.c |
| **B-126** | Silent crash ~8min into MP — process dies silently, no VEH log. Heartbeat instrumentation added (S187); awaiting next repro. | lv.c (heartbeat), crash.c |

### FIXED S190/S198 (awaiting visual confirmation from Mike)
| Bug | Description | Fix |
|-----|-------------|-----|
| **B-128** | Sky tearing / transparent sky tris on outdoor stages | sky.c:1244 `gDPSetRenderMode(OPA_SURF)` — inheriting previous frame blend state was root cause |
| **B-129** | AV on mission end — filemgrSaveOrLoad + no Pak + fn-ptr cast to lang index → crash, plus saves landing at drive root | S190: endscreen.c +14 lines, pdgui_menu_warning.cpp +12 lines. S198: saveInit() wired into main.c + server_main.c (commit 3fc345bf); saves now land in AppData. |

### NEW OPEN BUGS (S198)
| Bug | Description | File |
|-----|-------------|------|
| **B-130** | Theme editor X / Close / click-outside dismiss all ineffective; lifecycle instrumentation deployed S198, awaiting next playtest log | pdgui_menu_theme_editor.cpp |
| **B-131** | Post-restart Start button double-fire after Mission 1 loops back to start; deferred to follow-up | inputctx.c, bondmove.c |

### MEDIUM
| Bug | Description | File |
|-----|-------------|------|
| **B-18** | Pink sky on Skedar Ruins — may be resolved by B-128 sky fix; needs Skedar playtest | sky rendering |
| **B-19** | Bot spawn stacking on Skedar Ruins (partial fix S125) | player.c |
| ~~**B-78**~~ | ~~Chat rebroadcast without rate limiting -- DoS amplification~~ | FIXED 2026-04-11 |
| ~~**B-81**~~ | ~~JSON tokenizer unbounded recursion -- crafted save crash~~ | FIXED S200 |
| **B-99** | Updater extraction may fail -- needs retest | updater.c |
| **B-118** | CI intro cutscene crash -- 56 models missed by manifest | netmanifest.c |
| Menu opacity stacking | Main menu BG gets more opaque after repeated open/close | pdgui haze overlay |
| Prop sync not event-driven | CRC polling instead of event-driven | net sync |
| Some maps don't spawn enemies | Navmesh/pad coverage gaps post-AIDROP fix | various |
| Killfeed only shows player kills | Bot kills not in killfeed | netdistrib.c |

### LOW
| Bug | Description | File |
|-----|-------------|------|
| **B-60** | Stray 'g'+'s' behind Video/Audio tabs | pdgui_menu_mainmenu.cpp |
| **B-72** | SVC_LOBBY_STATE raw stagenum (display-only) | netmsg.c |
| **B-95** | Update banner persists during gameplay | pdgui_menu_update.cpp |
| **B-97** | Special Assignments not separated from missions | pdgui_menu_solomission.cpp |
| JUMP_LANDING log spam | Every frame during pause logs ground clamp | movement |

---

## Backlog (Post v0.1.0)

| Item | Target | Detail |
|------|--------|--------|
| **D5 Phase 5 -- Lobby scene** | v0.1.0+ | Player portraits, connected player avatars, character preview |
| **B-12 Phase 3 -- Remove chrslots** | v0.2.0 | Protocol bump, participant system replaces bitmask entirely |
| **D14a -- Counter-Op mode** | v0.6.0 | NPC possession mechanic |
| **D15 -- Map Editor / Forge** | v0.5.0 | Level editor, character creator, skin system |
| **D16 -- Master Server** | v0.4.0 | Server registry, heartbeat, browser |
| **D6 -- Persistent Stats** | v1.0.0 | JSON/SQLite stats, post-game scorecard, lifetime viewer |
| **D7 -- Discord Rich Presence** | v1.0.0 | Activity API, join button |
| **D10 -- Spectator Mode** | v0.6.0 | Free-cam, follow-cam, HUD overlay |

---

## Design Guidelines (Planned)

| System | Status |
|--------|--------|
| Menu/UI | DONE (in d5-full-menu-overhaul.md) |
| Input | IMPLEMENTED (M0.2 action maps). Guidelines doc PLANNED. |
| Networking | PLANNED |
| Mod System | PLANNED |
| Audio | PLANNED |
| Visual/Theme | PLANNED |
| Collision/Physics | PLANNED |
| Level Editor (Forge) | PLANNED |
