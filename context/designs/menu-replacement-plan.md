# Menu Replacement Plan — Complete Legacy-to-ImGui Migration

> **Created**: 2026-04-09 (S188)
> **Purpose**: Complete inventory of every remaining legacy menu, classification, data source mapping, and batched implementation schedule.
> **Supersedes**: Previous `menu-replacement-plan.md` (archived S186)
>
> **Companion Documents** (do not duplicate — cross-reference):
> - [d5-full-menu-overhaul.md](d5-full-menu-overhaul.md) — Master design doc: UX guidelines (binding rules), input context stack spec, controller nav, theme system, Phase 1-5 session plan, UI scaling rules, safe area spec. **All new menus MUST follow the UX guidelines in that doc.**
> - [menu-inventory.md](menu-inventory.md) — Per-screen status snapshot (120 screens, S135). This plan extends and supersedes that inventory with 254 total dialog definitions found in code audit.
> - [d5-ui-polish-plan.md](d5-ui-polish-plan.md) — D5.0-D5.8 sub-phase breakdown.
>
> **Relationship to D5 Phases**: This plan covers **D5 Phase 3** (Full Menu Roster Port) from the master design doc. Phases 1, 2, 4, 5 are specified in d5-full-menu-overhaul.md and are not duplicated here.

---

## Executive Summary

**254 dialog definitions** exist across 8 source files. After filtering dead code (4MB dialogs, N64-only pak management, duplicate forward declarations), **~140 unique reachable dialogs** remain. Of these:

| Status | Count | Description |
|--------|-------|-------------|
| **ImGui Complete** | 53 | Full custom ImGui renderer, working |
| **ImGui Standalone** | 6 | Independent ImGui windows (lobby, room, modding hub, log viewer, update, theme editor) |
| **Noop/Suppressed** | 15 | Registered with renderNoop — parent dialog renders content |
| **NULL renderFn** | 12 | Registered but forces legacy renderer (3D models, GBI-dependent) |
| **Type-based fallback** | 4 | MENUDIALOGTYPE_DEFAULT/DANGER/SUCCESS/0 catch-all renderers |
| **OG Unregistered** | ~50 | No hotswap registration — need full ImGui port |
| **Dead/Irrelevant** | ~114 | 4MB mode, pak management, N64-only, or unreachable |

**Work remaining**: ~50 unregistered OG dialogs + 12 NULL-renderFn dialogs that need real ImGui renderers = **~62 screens to port**.

---

## Part 1: Complete Dialog Inventory

### Classification Key

- **DONE** — Complete ImGui renderer, working
- **NOOP** — Registered renderNoop; parent renders content (no work needed)
- **NULL-FN** — Registered with NULL renderFn; forces legacy fallback (needs real renderer)
- **TYPE-FB** — Caught by type-based fallback renderer (DEFAULT/DANGER/SUCCESS); functional
- **OG** — Not registered in hotswap; needs full ImGui port
- **DEAD** — 4MB mode, N64 pak management, or unreachable code; skip
- **STANDALONE** — Independent ImGui window, not hotswap-based

---

### 1.1 Main Navigation

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_CiMenuViaPcMenuDialog` | mainmenu.c:5060 | DONE | pdgui_menu_mainmenu.cpp | Main menu entry |
| `g_CiMenuViaPauseMenuDialog` | mainmenu.c:5069 | DONE | pdgui_menu_mainmenu.cpp | Pause variant |
| `g_FilemgrFileSelectMenuDialog` | filemgr.c:3432 | DONE | pdgui_menu_agentselect.cpp | Agent select |
| `g_FilemgrEnterNameMenuDialog` | filemgr.c:3403 | DONE | pdgui_menu_agentcreate.cpp | Agent create |
| `g_ChangeAgentMenuDialog` | mainmenu.c:3508 | DONE | pdgui_menu_mainmenu.cpp | Change agent |
| `g_ExitGameMenuDialog` | mainmenu.c:3554 | **OG** | — | Confirm quit; simple confirmation |
| `g_ChooseLanguageMenuDialog` | filemgr.c:137 | **DEAD** | — | N64 language select; PC uses system locale |

### 1.2 Solo Mission Flow

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_SelectMissionMenuDialog` | mainmenu.c:4868 | DONE | pdgui_menu_solomission.cpp | Mission list |
| `g_SoloMissionDifficultyMenuDialog` | mainmenu.c:1335 | DONE | pdgui_menu_solomission.cpp | Difficulty picker |
| `g_SoloMissionBriefingMenuDialog` | mainmenu.c:2280 | DONE | pdgui_menu_solomission.cpp | Mission briefing |
| `g_PreAndPostMissionBriefingMenuDialog` | mainmenu.c:719 | DONE | pdgui_menu_solomission.cpp | Pre/post briefing |
| `g_AcceptMissionMenuDialog` | mainmenu.c:906 | DONE | pdgui_menu_solomission.cpp | Accept + objectives |
| `g_PdModeSettingsMenuDialog` | mainmenu.c:1033 | **OG** | — | PD mode toggles; hidden feature |

### 1.3 Solo In-Game (Pause)

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_SoloMissionPauseMenuDialog` | mainmenu.c:4623 | DONE | pdgui_menu_solomission.cpp | Pause menu |
| `g_SoloMissionInventoryMenuDialog` | mainmenu.c:4285 | DONE | pdgui_menu_solomission.cpp | Inventory |
| `g_SoloMissionOptionsMenuDialog` | mainmenu.c:3792 | DONE | pdgui_menu_solomission.cpp | Options while paused |
| `g_SoloMissionControlStyleMenuDialog` | mainmenu.c:2425 | **OG** | — | Controller diagram; legacy 3D preview |
| `g_MissionAbortMenuDialog` | mainmenu.c:4481 | DONE | pdgui_menu_solomission.cpp | Abort confirmation |
| `g_FrWeaponsAvailableMenuDialog` | mainmenu.c:4298 | **NULL-FN** | pdgui_menu_solomission.cpp | FR weapons in-game; NULL forces OG |

### 1.4 Solo Options (In-Game Sub-Dialogs)

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_AudioOptionsMenuDialog` | mainmenu.c:2566 | **NULL-FN** | pdgui_menu_solomission.cpp | Blocks OG; inline in solo options |
| `g_VideoOptionsMenuDialog` | mainmenu.c:2738 | **NULL-FN** | pdgui_menu_solomission.cpp | Blocks OG; inline in solo options |
| `g_MissionControlOptionsMenuDialog` | mainmenu.c:3238 | **NULL-FN** | pdgui_menu_solomission.cpp | Blocks OG; inline in solo options |
| `g_MissionDisplayOptionsMenuDialog` | mainmenu.c:2848 | **NULL-FN** | pdgui_menu_solomission.cpp | Blocks OG; inline in solo options |

### 1.5 Solo End Game

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_SoloMissionEndscreenCompletedMenuDialog` | endscreen.c:1480 | DONE | pdgui_menu_endscreen.cpp | Mission complete |
| `g_SoloMissionEndscreenFailedMenuDialog` | endscreen.c:1489 | DONE | pdgui_menu_endscreen.cpp | Mission failed |
| `g_SoloEndscreenObjectivesCompletedMenuDialog` | endscreen.c:597 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |
| `g_SoloEndscreenObjectivesFailedMenuDialog` | endscreen.c:588 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |
| `g_RetryMissionMenuDialog` | endscreen.c:203 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |
| `g_NextMissionMenuDialog` | endscreen.c:256 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |
| `g_MissionContinueOrReplyMenuDialog` | endscreen.c:659 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |

### 1.6 Co-op / Counter-Op Flow

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_CoopMissionDifficultyMenuDialog` | mainmenu.c:1787 | **OG** | — | Co-op difficulty picker |
| `g_CoopOptionsMenuDialog` | mainmenu.c:1601 | **OG** | — | Co-op buddy/team options |
| `g_AntiMissionDifficultyMenuDialog` | mainmenu.c:1854 | **OG** | — | Counter-op difficulty |
| `g_AntiOptionsMenuDialog` | mainmenu.c:1715 | **OG** | — | Counter-op options |

### 1.7 2-Player Split-Screen

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_2PMissionOptionsHMenuDialog` | mainmenu.c:3819 | **OG** | — | 2P options (H layout) |
| `g_2PMissionOptionsVMenuDialog` | mainmenu.c:3828 | **OG** | — | 2P options (V layout) |
| `g_2PMissionBriefingHMenuDialog` | mainmenu.c:2289 | **OG** | — | 2P briefing (H) |
| `g_2PMissionBriefingVMenuDialog` | mainmenu.c:2298 | **OG** | — | 2P briefing (V) |
| `g_2PMissionControlStyleMenuDialog` | mainmenu.c:2390 | **OG** | — | 2P control style |
| `g_2PMissionPauseHMenuDialog` | mainmenu.c:4632 | **OG** | — | 2P pause (H) |
| `g_2PMissionPauseVMenuDialog` | mainmenu.c:4641 | **OG** | — | 2P pause (V) |
| `g_2PMissionAbortVMenuDialog` | mainmenu.c:4518 | **OG** | — | 2P abort (V) |
| `g_2PMissionAudioOptionsVMenuDialog` | mainmenu.c:2637 | **OG** | — | 2P audio (V) |
| `g_2PMissionVideoOptionsMenuDialog` | mainmenu.c:2747 | **OG** | — | 2P video |
| `g_2PMissionDisplayOptionsVMenuDialog` | mainmenu.c:2957 | **OG** | — | 2P display (V) |
| `g_2PMissionInventoryHMenuDialog` | ingame.c:587 | **OG** | — | 2P inventory (H) |
| `g_2PMissionInventoryVMenuDialog` | ingame.c:596 | **OG** | — | 2P inventory (V) |

### 1.8 2-Player End Screens

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_2PMissionEndscreenCompletedHMenuDialog` | endscreen.c:1730 | DONE | pdgui_menu_endscreen.cpp | Reuses solo renderer |
| `g_2PMissionEndscreenFailedHMenuDialog` | endscreen.c:1739 | DONE | pdgui_menu_endscreen.cpp | Reuses solo renderer |
| `g_2PMissionEndscreenCompletedVMenuDialog` | endscreen.c:1748 | DONE | pdgui_menu_endscreen.cpp | Reuses solo renderer |
| `g_2PMissionEndscreenFailedVMenuDialog` | endscreen.c:1757 | DONE | pdgui_menu_endscreen.cpp | Reuses solo renderer |
| `g_2PMissionEndscreenObjectivesCompletedVMenuDialog` | endscreen.c:615 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |
| `g_2PMissionEndscreenObjectivesFailedVMenuDialog` | endscreen.c:606 | NOOP | pdgui_menu_endscreen.cpp | Rendered in parent |

### 1.9 CI Options / Settings Tree

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_CiOptionsViaPcMenuDialog` | mainmenu.c:3801 | **OG** | — | Options root (from PC menu) |
| `g_CiOptionsViaPauseMenuDialog` | mainmenu.c:3810 | **OG** | — | Options root (from pause) |
| `g_CiControlOptionsMenuDialog` | mainmenu.c:3394 | **OG** | — | CI control settings |
| `g_CiControlOptionsMenuDialog2` | mainmenu.c:3316 | **OG** | — | CI secondary controls |
| `g_CiControlStyleMenuDialog` | mainmenu.c:2460 | **OG** | — | CI controller layout |
| `g_CiControlStylePlayer2MenuDialog` | mainmenu.c:2495 | **OG** | — | CI P2 controller layout |
| `g_CiDisplayMenuDialog` | mainmenu.c:3060 | **OG** | — | CI display settings |
| `g_CiDisplayPlayer2MenuDialog` | mainmenu.c:3161 | **OG** | — | CI P2 display |
| `g_CiControlPlayer2MenuDialog` | mainmenu.c:3471 | **OG** | — | CI P2 control config |
| `g_ExtendedMenuDialog` | (mainmenu.cpp) | DONE | pdgui_menu_mainmenu.cpp | PC-specific settings |
| `g_ExtendedVideoMenuDialog` | (mainmenu.cpp) | DONE | pdgui_menu_mainmenu.cpp | PC video tab |
| `g_ExtendedAudioMenuDialog` | (mainmenu.cpp) | DONE | pdgui_menu_mainmenu.cpp | PC audio tab |
| `g_ExtendedMouseMenuDialog` | (mainmenu.cpp) | DONE | pdgui_menu_mainmenu.cpp | PC mouse tab |

### 1.10 Cheats & Cinema

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_CheatsMenuDialog` | cheats.c:1654 | **OG** | — | Cheats root menu |
| `g_CheatsWarningMenuDialog` | cheats.c:478 | **TYPE-FB** | — | Caught by type-based renderer |
| `g_CheatsConfirmUnlockMenuDialog` | cheats.c:930 | **TYPE-FB** | — | Caught by type-based renderer |
| `g_CheatsFunMenuDialog` | cheats.c:1024 | **OG** | — | Fun cheats list |
| `g_CheatsGameplayMenuDialog` | cheats.c:1141 | **OG** | — | Gameplay cheats list |
| `g_CheatsSoloWeaponsMenuDialog` | cheats.c:1250 | **OG** | — | Solo weapon cheats |
| `g_CheatsClassicWeaponsMenuDialog` | cheats.c:1359 | **OG** | — | Classic weapon cheats |
| `g_CheatsWeaponsMenuDialog` | cheats.c:1468 | **OG** | — | Weapon cheats root |
| `g_CheatsBuddiesMenuDialog` | cheats.c:1553 | **OG** | — | Buddy cheats |
| `g_CinemaMenuDialog` | mainmenu.c:4847 | **OG** | — | Cutscene viewer |

### 1.11 Combat Simulator / Multiplayer Setup

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_CombatSimulatorMenuDialog` | setup.c:6454 | **OG** | — | Combat sim root (legacy; room.cpp replaces) |
| `g_NetMenuDialog` | (network.cpp) | DONE | pdgui_menu_network.cpp | MP menu (host/join/browse) |
| `g_MpHandicapsMenuDialog` | setup.c:3220 | DONE | pdgui_menu_mpsettings.cpp | Handicaps |
| `g_MpTeamsMenuDialog` | setup.c:4431 | DONE | pdgui_menu_teamsetup.cpp | Team control |
| `g_MpAutoTeamMenuDialog` | setup.c:4159 | DONE | pdgui_menu_teamsetup.cpp | Auto-team |
| `g_MpChallengeListOrDetailsMenuDialog` | setup.c:5005 | DONE | pdgui_menu_challenges.cpp | Challenge browser |
| `g_MpCompletedChallengesMenuDialog` | setup.c:2001 | DONE | pdgui_menu_challenges.cpp | Completed list |
| `g_MpReadyMenuDialog` | setup.c:3241 | **TYPE-FB** | — | Ready gate; caught by type fallback |
| `g_MpEndscreenConfirmNameMenuDialog` | ingame.c:1122 | NOOP | pdgui_menu_warning.cpp | Suppressed |

### 1.12 MP Setup Dialogs (Unregistered OG)

| Dialog | Source | Status | Data Source | Notes |
|--------|--------|--------|-------------|-------|
| `g_MpPlayerOptionsMenuDialog` | setup.c:1847 | **OG** | Player config | Player option settings |
| `g_MpControlMenuDialog` | setup.c:1980 | **OG** | Control config | MP control settings |
| `g_MpCharacterMenuDialog` | setup.c:2945 | **OG** | g_HeadsAndBodies[] | Character selection |
| `g_MpPlayerNameMenuDialog` | setup.c:2966 | **TYPE-FB** | Text input | Player name entry |
| `g_MpSaveSetupNameMenuDialog` | setup.c:1317 | **TYPE-FB** | Text input | Save setup name entry |
| `g_MpSaveSetupExistsMenuDialog` | setup.c:1388 | **TYPE-FB** | — | Overwrite confirmation |
| `g_MpSavePlayerMenuDialog` | setup.c:1286 | **TYPE-FB** | — | Save player confirmation |
| `g_MpChangeTeamNameMenuDialog` | setup.c:4804 | **TYPE-FB** | Text input | Team name entry |
| `g_MpPlayerStatsMenuDialog` | setup.c:2309 | **OG** | Player stats data | Player stats view |
| `g_MpLoadSettingsMenuDialog` | setup.c:3011 | **OG** | Saved setups | Load settings list |
| `g_MpLoadPresetMenuDialog` | setup.c:3040 | **OG** | Preset data | Load presets |
| `g_MpLoadPlayerMenuDialog` | setup.c:3069 | **OG** | Saved players | Load player |
| `g_MpArenaMenuDialog` | setup.c:3090 | **OG** | g_MpArenas[] | Arena selection |
| `g_MpLimitsMenuDialog` | setup.c:3151 | **OG** | Match config | Game limits (time, score) |
| `g_MpWeaponsMenuDialog` | setup.c:1701 | **OG** | g_MpWeapons[] | Weapon set selection |
| `g_MpSelectRandomWeaponsMenuDialog` | setup.c:1523 | **OG** | g_MpWeapons[] | Random weapon config |
| `g_MpQuickTeamWeaponsMenuDialog` | setup.c:1786 | **OG** | g_MpWeapons[] | Quick team weapons |
| `g_MpSelectTunesMenuDialog` | setup.c:4714 | **OG** | Music tracks | Music selection |
| `g_MpSoundtrackMenuDialog` | setup.c:4783 | **OG** | Music config | Soundtrack settings |
| `g_MpTeamNamesMenuDialog` | setup.c:4897 | **OG** | Team config | Team names list |
| `g_MpDropOutMenuDialog` | setup.c:105 | **TYPE-FB** | — | Confirm drop out |
| `g_MpAbortMenuDialog` | setup.c:6034 | **TYPE-FB** | — | Confirm abort MP |

### 1.13 MP Simulant (Bot) Setup

| Dialog | Source | Status | Data Source | Notes |
|--------|--------|--------|-------------|-------|
| `g_MpSimulantsMenuDialog` | setup.c:3847 | **OG** | Bot list/config | Bot roster |
| `g_MpAddSimulantMenuDialog` | setup.c:3623 | **OG** | g_HeadsAndBodies[] | Add bot |
| `g_MpChangeSimulantMenuDialog` | setup.c:3632 | **OG** | g_HeadsAndBodies[] | Change bot |
| `g_MpEditSimulantMenuDialog` | setup.c:3730 | **OG** | Bot config | Edit bot settings |
| `g_MpSimulantCharacterMenuDialog` | setup.c:3661 | **OG** | g_HeadsAndBodies[] | Bot character select |

### 1.14 MP Advanced/Quick Setup

| Dialog | Source | Status | Notes |
|--------|--------|--------|-------|
| `g_MpAdvancedSetupMenuDialog` | setup.c:6143 | **OG** | Advanced setup root |
| `g_MpAdvancedSetupViaAdvChallengeMenuDialog` | setup.c:6152 | **OG** | Via challenge path |
| `g_MpQuickGoMenuDialog` | setup.c:6199 | **OG** | Quick go root |
| `g_MpQuickTeamGameSetupMenuDialog` | setup.c:6348 | **OG** | Quick team setup |
| `g_MpQuickTeamMenuDialog` | setup.c:6409 | **OG** | Quick team root |
| `g_MpStuffMenuDialog` | setup.c:5893 | **OG** | MP stuff menu |
| `g_MpStuffViaAdvChallengeMenuDialog` | setup.c:5902 | **OG** | Via challenge path |
| `g_MpPlayerSetupViaAdvMenuDialog` | setup.c:5979 | **OG** | Player setup variant |
| `g_MpPlayerSetupViaAdvChallengeMenuDialog` | setup.c:5988 | **OG** | Player setup variant |
| `g_MpPlayerSetupViaQuickGoMenuDialog` | setup.c:5997 | **OG** | Player setup variant |
| `g_MpConfirmChallengeMenuDialog` | setup.c:5078 | **TYPE-FB** | Challenge confirmation |
| `g_MpConfirmChallengeViaListOrDetailsMenuDialog` | setup.c:4942 | **TYPE-FB** | Challenge confirm variant |
| `g_MpChallengesMenuDialog` | setup.c:5299 | **OG** | Challenge root menu |
| `g_MpChallengeListOrDetailsViaAdvChallengeMenuDialog` | setup.c:5024 | **OG** | Challenge variant |
| `g_ExtGameOptionsMenuDialog` | setup.c:6530 | **OG** | Extended game options |

### 1.15 MP Scenario Options

| Dialog | Source | Status | Data Source | Notes |
|--------|--------|--------|-------------|-------|
| `g_MpScenarioMenuDialog` | scenarios.c:970 | **OG** | g_MpScenarios[] | Scenario selection |
| `g_MpQuickTeamScenarioMenuDialog` | scenarios.c:991 | **OG** | g_MpScenarios[] | Quick team variant |
| `g_MpCombatOptionsMenuDialog` | combat.inc:103 | **OG** | Scenario config | Combat options |
| `g_CtcOptionsMenuDialog` | capturethecase.inc:108 | **OG** | Scenario config | Capture the Case options |
| `g_HtmOptionsMenuDialog` | hackthatmac.inc:114 | **OG** | Scenario config | Hack That Mac options |
| `g_HtbOptionsMenuDialog` | holdthebriefcase.inc:114 | **OG** | Scenario config | Hold the Briefcase options |
| `g_KohOptionsMenuDialog` | kingofthehill.inc:143 | **OG** | Scenario config | King of the Hill options |
| `g_PacOptionsMenuDialog` | popacap.inc:112 | **OG** | Scenario config | Pop a Cap options |

### 1.16 MP In-Game (Pause/Stats)

| Dialog | Source | Status | Notes |
|--------|--------|--------|-------|
| `g_MpEndGameMenuDialog` | ingame.c:249 | **OG** | MP end game root |
| `g_MpPauseControlMenuDialog` | ingame.c:429 | **OG** | Pause control settings |
| `g_MpPauseInventoryMenuDialog` | ingame.c:578 | **OG** | MP pause inventory |
| `g_MpPausePlayerStatsMenuDialog` | ingame.c:617 | **OG** | Pause player stats |
| `g_MpPausePlayerRankingMenuDialog` | ingame.c:647 | **OG** | Pause player ranking |
| `g_MpPauseTeamRankingsMenuDialog` | ingame.c:677 | **OG** | Pause team rankings |

### 1.17 MP End Screens

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_MpEndscreenIndGameOverMenuDialog` | ingame.c:1056 | DONE | pdgui_menu_endscreen.cpp + mpingame.cpp | Dual-registered |
| `g_MpEndscreenTeamGameOverMenuDialog` | ingame.c:1065 | DONE | pdgui_menu_endscreen.cpp + mpingame.cpp | Dual-registered |
| `g_MpEndscreenChallengeCompletedMenuDialog` | ingame.c:1074 | DONE | pdgui_menu_endscreen.cpp | Challenge complete |
| `g_MpEndscreenChallengeCheatedMenuDialog` | ingame.c:1083 | DONE | pdgui_menu_endscreen.cpp | Challenge cheated |
| `g_MpEndscreenChallengeFailedMenuDialog` | ingame.c:1092 | DONE | pdgui_menu_endscreen.cpp | Challenge failed |
| `g_MpEndscreenPlayerRankingMenuDialog` | ingame.c:656 | NOOP | pdgui_menu_endscreen.cpp + mpingame.cpp | Rendered in parent |
| `g_MpEndscreenTeamRankingMenuDialog` | ingame.c:686 | NOOP | pdgui_menu_endscreen.cpp + mpingame.cpp | Rendered in parent |
| `g_MpEndscreenPlayerStatsMenuDialog` | ingame.c:626 | NOOP | pdgui_menu_endscreen.cpp + mpingame.cpp | Rendered in parent |
| `g_MpEndscreenSavePlayerMenuDialog` | ingame.c:1159 | NOOP | pdgui_menu_mpingame.cpp | Auto-save via config |

### 1.18 Training / Carrington Institute

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| `g_FrDifficultyMenuDialog` | trainingmenus.c:951 | DONE | pdgui_menu_training.cpp | FR difficulty |
| `g_FrWeaponListMenuDialog` | trainingmenus.c:972 | **NULL-FN** | pdgui_menu_training.cpp | GBI custom rendering |
| `g_FrTrainingInfoPreGameMenuDialog` | trainingmenus.c:1158 | DONE | pdgui_menu_training.cpp | Pre-game info |
| `g_FrTrainingInfoInGameMenuDialog` | trainingmenus.c:1065 | DONE | pdgui_menu_training.cpp | In-game info |
| `g_FrCompletedMenuDialog` | trainingmenus.c:1279 | DONE | pdgui_menu_training.cpp | FR complete |
| `g_FrFailedMenuDialog` | trainingmenus.c:1405 | DONE | pdgui_menu_training.cpp | FR failed |
| `g_BioListMenuDialog` | trainingmenus.c:1482 | **NULL-FN** | pdgui_menu_training.cpp | Opaque structs |
| `g_BioProfileMenuDialog` | trainingmenus.c:1820 | **NULL-FN** | pdgui_menu_training.cpp | 3D character model |
| `g_BioTextMenuDialog` | trainingmenus.c:1857 | DONE | pdgui_menu_training.cpp | Bio text |
| `g_NowSafeMenuDialog` | trainingmenus.c:1519 | DONE | pdgui_menu_training.cpp | Safe room notification |
| `g_DtListMenuDialog` | trainingmenus.c:1878 | **NULL-FN** | pdgui_menu_training.cpp | Opaque structs |
| `g_DtDetailsMenuDialog` | trainingmenus.c:2208 | **NULL-FN** | pdgui_menu_training.cpp | 3D weapon model |
| `g_DtFailedMenuDialog` | trainingmenus.c:2261 | DONE | pdgui_menu_training.cpp | DT failed |
| `g_DtCompletedMenuDialog` | trainingmenus.c:2314 | DONE | pdgui_menu_training.cpp | DT complete |
| `g_HtListMenuDialog` | trainingmenus.c:2335 | DONE | pdgui_menu_training.cpp | HT list |
| `g_HtDetailsMenuDialog` | trainingmenus.c:2388 | **NULL-FN** | pdgui_menu_training.cpp | MENUITEMTYPE_MODEL |
| `g_HtFailedMenuDialog` | trainingmenus.c:2447 | DONE | pdgui_menu_training.cpp | HT failed |
| `g_HtCompletedMenuDialog` | trainingmenus.c:2506 | DONE | pdgui_menu_training.cpp | HT complete |
| `g_HangarListMenuDialog` | trainingmenus.c:2859 | **NULL-FN** | pdgui_menu_training.cpp | 3D vehicle models |
| `g_HangarVehicleHolographMenuDialog` | trainingmenus.c:2820 | **NULL-FN** | pdgui_menu_training.cpp | 3D hologram |
| `g_HangarVehicleDetailsMenuDialog` | trainingmenus.c:2829 | **NULL-FN** | pdgui_menu_training.cpp | Vehicle detail |
| `g_HangarLocationDetailsMenuDialog` | trainingmenus.c:2838 | **NULL-FN** | pdgui_menu_training.cpp | Location detail |

### 1.19 Network / Online Dialogs

| Dialog | Source | Status | ImGui File | Notes |
|--------|--------|--------|-----------|-------|
| Social Lobby | — | STANDALONE | pdgui_menu_lobby.cpp | Player list + room list |
| Room Interior | — | STANDALONE | pdgui_menu_room.cpp | Tab bar: scenarios |

### 1.20 File Management

| Dialog | Source | Status | Notes |
|--------|--------|--------|-------|
| `g_FilemgrRenameMenuDialog` | filemgr.c:1586 | **TYPE-FB** | Text input; caught by type fallback |
| `g_FilemgrDuplicateNameMenuDialog` | filemgr.c:1657 | **TYPE-FB** | Error dialog; caught by type fallback |
| `g_FilemgrFileSavedMenuDialog` | filemgr.c:1031 | **TYPE-FB** | Success dialog; caught by type fallback |
| `g_FilemgrConfirmDeleteMenuDialog` | filemgr.c:2981 | **TYPE-FB** | Danger confirmation |
| `g_FilemgrErrorMenuDialog` | filemgr.c:402 | **TYPE-FB** | Error dialog |
| `g_FilemgrSaveErrorMenuDialog` | filemgr.c:1084 | **TYPE-FB** | Save error |
| `g_FilemgrFileInUseMenuDialog` | filemgr.c:3030 | **TYPE-FB** | File in use warning |
| `g_FilemgrDeleteMenuDialog` | filemgr.c:3067 | **OG** | Delete confirmation (with operations) |
| `g_FilemgrCopyMenuDialog` | filemgr.c:3104 | **OG** | Copy file |
| `g_FilemgrOperationsMenuDialog` | filemgr.c:3382 | **OG** | File operations list |
| `g_FilemgrSelectLocationMenuDialog` | filemgr.c:2928 | **OG** | Location selection |
| `g_FilemgrFileLostMenuDialog` | filemgr.c:1140 | **TYPE-FB** | File lost warning |
| `g_FilemgrSaveElsewhereMenuDialog` | filemgr.c:1177 | **TYPE-FB** | Save elsewhere prompt |

### 1.21 N64-Only / Dead Dialogs (SKIP)

| Dialog | Source | Status | Reason |
|--------|--------|--------|--------|
| `g_PakRemovedMenuDialog` | menu.c:6002 | DEAD | N64 controller pak |
| `g_PakRepairSuccessMenuDialog` | menu.c:6046 | DEAD | N64 pak repair |
| `g_PakRepairFailedMenuDialog` | menu.c:6083 | DEAD | N64 pak repair |
| `g_PakAttemptRepairMenuDialog` | menu.c:6136 | DEAD | N64 pak repair |
| `g_PakDamagedMenuDialog` | menu.c:6395 | DEAD | N64 pak damage |
| `g_PakFullMenuDialog` | menu.c:6462 | DEAD | N64 pak full |
| `g_PakCannotReadGameBoyMenuDialog` | menu.c:6504 | DEAD | N64 Game Boy |
| `g_PakDataLostMenuDialog` | menu.c:6565 | DEAD | N64 pak data |
| `g_PakDeleteNoteMenuDialog` | filemgr.c:3143 | DEAD | N64 pak note |
| `g_PakGameNotesMenuDialog` | filemgr.c:3212 | DEAD | N64 pak notes |
| `g_PakChoosePakMenuDialog` | filemgr.c:3297 | DEAD | N64 pak select |
| `g_PakNotOriginalMenuDialog` | filemgr.c:1218 | DEAD | N64 pak auth |
| `g_AmPickTargetMenuDialog` | activemenu.c:220 | DEAD | In-game targeting HUD |
| All `*4Mb*` dialogs (12) | fmb.c | DEAD | 4MB mode removed |

### 1.22 Standalone ImGui Windows

| Window | File | Status | Notes |
|--------|------|--------|-------|
| Modding Hub | pdgui_menu_moddinghub.cpp | STANDALONE | 3 tabs: mods, INI, scale tool |
| Mod Manager | pdgui_menu_modmgr.cpp | STANDALONE | Sub-component of hub |
| Log Viewer | pdgui_menu_logviewer.cpp | STANDALONE | Dev tool |
| Update Screen | pdgui_menu_update.cpp | STANDALONE | Update overlay |
| Theme Editor | pdgui_menu_theme_editor.cpp | STANDALONE | Dev tool |
| Stats Display | pdgui_menu_stats.cpp | STANDALONE | Performance overlay |

---

## Part 2: Data Source Mapping

### What each unported menu reads from

| Data Source | Array/Struct | Menus That Use It | Catalog-Era Equivalent |
|-------------|-------------|-------------------|----------------------|
| **Stage/mission data** | `g_SoloStages[21]` | Co-op/Counter-op difficulty, 2P briefings | `catalogResolveStage(stage_id)` — already migrated for solo path |
| **Character bodies/heads** | `g_HeadsAndBodies[152]` | Bot character select, MP character select | Asset Catalog: `"base:body_joanna"` etc. — already used in match config |
| **Arena list** | `g_MpArenas[]` | Arena selection, quick team setup | Asset Catalog: `"base:arena_felicity"` etc. — already used in room.cpp |
| **Weapon sets** | `g_MpWeapons[NUM_MPWEAPONS]` | Weapon selection, random weapons | Asset Catalog: weapon_ids[6][64] in match config |
| **Scenario list** | `g_MpScenarios[]` | Scenario selection, scenario options | Asset Catalog: `scenario_id[64]` in match config |
| **Challenge data** | `g_MpChallenges[30]` | Challenge list, confirm challenge | Already ported in pdgui_menu_challenges.cpp |
| **Music tracks** | Music config | Tune selection, soundtrack | Direct — no catalog migration needed |
| **Player config** | `g_PlayerConfigsArray[]` | Name entry, control style, options | Identity profile + pd.ini config |
| **Cheat state** | `cheatIsUnlocked()` / cheat arrays | Cheats menu tree | Direct — no catalog migration needed |
| **Bot config** | `matchslot` struct | Bot setup, simulant edit | `body_id`/`head_id` strings (already primary) |
| **Match config** | `g_MissionConfig` | All setup screens | `stage_id`, `scenario_id`, `weapon_ids` (already migrated) |
| **Saved setups** | Save file data | Load settings, load presets | JSON save format |
| **Display/control prefs** | pd.ini config values | All options screens | `configRegisterInt/Float` — direct |
| **Team config** | Team arrays in mplayer | Team names, auto team | Direct |

### Key insight: Most data sources are already catalog-aware

The match config (`g_MissionConfig`) already stores `stage_id`, `scenario_id`, `weapon_ids[]`, `body_id`, `head_id` as catalog ID strings. The legacy menus write integer indices to `matchslot.mpbodynum` etc., but the modern path resolves from catalog strings. **New ImGui menus should read/write catalog ID strings directly and never touch the integer fields.**

---

## Part 3: Menu Tree & Dependencies

### Parent-Child Relationships (Critical for ordering)

```
MAIN MENU (g_CiMenuViaPcMenuDialog) [DONE]
├── Solo Missions
│   └── Mission Select (g_SelectMissionMenuDialog) [DONE]
│       ├── Solo Difficulty (g_SoloMissionDifficultyMenuDialog) [DONE]
│       │   └── Accept Mission (g_AcceptMissionMenuDialog) [DONE]
│       │       └── PD Mode Settings (g_PdModeSettingsMenuDialog) [OG]
│       ├── Co-op Difficulty (g_CoopMissionDifficultyMenuDialog) [OG]
│       │   └── Co-op Options (g_CoopOptionsMenuDialog) [OG]
│       │       └── Accept Mission [DONE]
│       └── Counter-Op Difficulty (g_AntiMissionDifficultyMenuDialog) [OG]
│           └── Counter-Op Options (g_AntiOptionsMenuDialog) [OG]
│               └── Accept Mission [DONE]
│
├── Combat Simulator → Room UI [STANDALONE, DONE]
│   └── (Legacy path: g_CombatSimulatorMenuDialog → setup.c tree)
│       ├── Quick Go (g_MpQuickGoMenuDialog) [OG]
│       ├── Advanced Setup (g_MpAdvancedSetupMenuDialog) [OG]
│       │   ├── Arena (g_MpArenaMenuDialog) [OG]
│       │   ├── Scenario (g_MpScenarioMenuDialog) [OG]
│       │   │   └── Scenario Options (per-type .inc) [OG x6]
│       │   ├── Weapons (g_MpWeaponsMenuDialog) [OG]
│       │   ├── Limits (g_MpLimitsMenuDialog) [OG]
│       │   ├── Simulants (g_MpSimulantsMenuDialog) [OG]
│       │   │   ├── Add/Change/Edit Simulant [OG x4]
│       │   │   └── Simulant Character [OG]
│       │   ├── Teams (g_MpTeamsMenuDialog) [DONE]
│       │   ├── Player Setup [OG variants x3]
│       │   └── Stuff (g_MpStuffMenuDialog) [OG]
│       └── Challenges (g_MpChallengesMenuDialog) [OG root; list DONE]
│
├── Training → Carrington Institute [most DONE]
│   ├── Firing Range [DONE except FR Weapons Available: NULL-FN]
│   ├── Bios [NULL-FN x2, text DONE]
│   ├── Device Training [NULL-FN x2, pass/fail DONE]
│   ├── Holotraining [NULL-FN x1, rest DONE]
│   └── Hangar [NULL-FN x4]
│
├── Settings
│   ├── Extended Settings [DONE, PC-specific]
│   └── CI Options root [OG]
│       ├── Audio/Video/Display/Control [OG x4+]
│       └── CI P2 variants [OG x4]
│
├── Cheats [OG, full tree of 9 dialogs]
├── Cinema [OG]
└── Modding [STANDALONE, DONE]

PAUSE MENU (g_CiMenuViaPauseMenuDialog) [DONE]
├── Solo Pause (g_SoloMissionPauseMenuDialog) [DONE]
│   ├── Inventory [DONE]
│   ├── Options [DONE; sub-dialogs NULL-FN x4]
│   ├── Control Style [OG]
│   └── Abort [DONE]
├── 2P Pause H/V [OG x2]
│   ├── 2P Options H/V [OG x2]
│   ├── 2P Audio/Video/Display [OG x3]
│   ├── 2P Control Style [OG]
│   ├── 2P Inventory H/V [OG x2]
│   └── 2P Abort V [OG]
└── MP Pause
    ├── Control [OG]
    ├── Inventory [OG]
    ├── Player Stats [OG]
    ├── Player Ranking [OG]
    └── Team Rankings [OG]
```

---

## Part 4: Implementation Batches

### Strategy

1. **Parents before children** — port a parent menu before its children so navigation flows work
2. **Shared data = shared batch** — group menus that read the same data source
3. **Complexity tiers**: Simple (list/toggles, ~50 LOC) | Medium (multi-panel, ~150 LOC) | Complex (data-heavy, 3D preview, ~300+ LOC)
4. **Type-based fallback screens need no work** — already functional via warning.cpp renderer
5. **NULL-renderFn screens need 3D preview solution** — either ImGui framebuffer display or simplified text-only version

---

### Batch 1: Simple Confirmations & Text Inputs (Low-Hanging Fruit)

**Effort**: 1 session | **Screens**: ~8 | **Complexity**: Simple
**Why first**: These are already partially handled by type-based fallback but could get dedicated renderers for consistency.

| Dialog | Current | Action | Complexity |
|--------|---------|--------|-----------|
| `g_ExitGameMenuDialog` | OG | New ImGui renderer — simple "Quit to Desktop?" confirmation | Simple |
| `g_PdModeSettingsMenuDialog` | OG | New ImGui renderer — toggle list for PD mode options | Simple |
| `g_MpEndGameMenuDialog` | OG | New ImGui renderer — end game options (continue/quit) | Simple |
| `g_FilemgrDeleteMenuDialog` | OG | New ImGui renderer — delete confirm with file info | Simple |
| `g_FilemgrCopyMenuDialog` | OG | New ImGui renderer — copy confirm | Simple |
| `g_FilemgrOperationsMenuDialog` | OG | New ImGui renderer — file ops list | Simple |
| `g_FilemgrSelectLocationMenuDialog` | OG | New ImGui renderer — location picker | Simple |

**Files to create/modify**: `pdgui_menu_mainmenu.cpp` (exit, pd mode), `pdgui_menu_mpingame.cpp` (end game), `pdgui_menu_filemgr.cpp` (new — file management)
**Data to wire**: None complex — direct config reads, confirmation callbacks

---

### Batch 2: Co-op & Counter-Op Flow (4 screens)

**Effort**: 1 session | **Screens**: 4 | **Complexity**: Medium
**Why second**: These are the remaining solo-adjacent menus and share data/flow with already-ported mission select.

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_CoopMissionDifficultyMenuDialog` | OG | Clone solo difficulty renderer, add co-op specific options | g_MissionConfig |
| `g_CoopOptionsMenuDialog` | OG | New ImGui renderer — buddy options, player 2 config | Player config |
| `g_AntiMissionDifficultyMenuDialog` | OG | Clone solo difficulty renderer, add anti-specific options | g_MissionConfig |
| `g_AntiOptionsMenuDialog` | OG | New ImGui renderer — counter-op team config | Player config |

**Files to modify**: `pdgui_menu_solomission.cpp` (extend existing renderers)
**Data to wire**: `g_MissionConfig.iscoop`, `g_MissionConfig.isanti`, difficulty handlers
**Transitions**: Push `g_AcceptMissionMenuDialog` (already DONE) after options

---

### Batch 3: CI Options Tree (9 screens)

**Effort**: 2 sessions | **Screens**: 9 | **Complexity**: Medium
**Why third**: Options screens are simple slider/toggle lists. Already have Extended Settings as a template.

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_CiOptionsViaPcMenuDialog` | OG | New ImGui — options hub with tabs | Config tree |
| `g_CiOptionsViaPauseMenuDialog` | OG | Same renderer, different entry context | Config tree |
| `g_CiControlOptionsMenuDialog` | OG | New ImGui — control settings (look, aim) | pd.ini config |
| `g_CiControlOptionsMenuDialog2` | OG | Merge into control settings as second tab | pd.ini config |
| `g_CiControlStyleMenuDialog` | OG | New ImGui — controller layout diagram (simplified) | Input config |
| `g_CiControlStylePlayer2MenuDialog` | OG | Clone P1, parameterized for P2 | Input config |
| `g_CiDisplayMenuDialog` | OG | New ImGui — display settings | pd.ini config |
| `g_CiDisplayPlayer2MenuDialog` | OG | Clone P1, parameterized for P2 | pd.ini config |
| `g_CiControlPlayer2MenuDialog` | OG | Clone P1 control, parameterized for P2 | pd.ini config |

**Files to create**: `pdgui_menu_options.cpp` (new — unified options renderer)
**Data to wire**: `configRegisterInt/Float` values from pd.ini, screen size, sound mode, control mappings
**Design note**: Modern PC port should merge CI Options + Extended Settings into ONE unified settings screen. Legacy N64 audio/video/display split is unnecessary — consolidate into Graphics, Audio, Controls, Accessibility tabs.

---

### Batch 4: Cheats & Cinema (10 screens)

**Effort**: 1-2 sessions | **Screens**: 10 | **Complexity**: Medium
**Why here**: Self-contained system with no external dependencies. Cheats read from cheat unlock arrays.

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_CheatsMenuDialog` | OG | New ImGui — cheats hub with category tabs | Cheat arrays |
| `g_CheatsFunMenuDialog` | OG | Tab content — fun cheats toggle list | cheatIsUnlocked() |
| `g_CheatsGameplayMenuDialog` | OG | Tab content — gameplay cheats toggle list | cheatIsUnlocked() |
| `g_CheatsSoloWeaponsMenuDialog` | OG | Tab content — solo weapon select | Weapon arrays |
| `g_CheatsClassicWeaponsMenuDialog` | OG | Tab content — classic weapons | Weapon arrays |
| `g_CheatsWeaponsMenuDialog` | OG | Tab content — weapons root | Weapon arrays |
| `g_CheatsBuddiesMenuDialog` | OG | Tab content — buddy selection | g_HeadsAndBodies[] |
| `g_CinemaMenuDialog` | OG | New ImGui — cutscene list with play button | Cinema data |

**Files to create**: `pdgui_menu_cheats.cpp` (new), modify `pdgui_menu_mainmenu.cpp` for cinema
**Data to wire**: `cheatIsUnlocked()`, `cheatSetEnabled()`, `g_CheatsActive`, cinema scene list
**Design note**: Consolidate 7 sub-menus into one tabbed cheats screen. Cinema is a simple scrollable list.

---

### Batch 5: MP Setup Core (Arena, Weapons, Scenario, Limits)

**Effort**: 2-3 sessions | **Screens**: ~14 | **Complexity**: Complex
**Why here**: These are the data-heavy setup screens. Room.cpp already handles the modern flow, but legacy combat sim path needs these for completeness.

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_MpArenaMenuDialog` | OG | New ImGui — categorized arena grid | Asset Catalog arenas |
| `g_MpScenarioMenuDialog` | OG | New ImGui — scenario list with descriptions | g_MpScenarios[] |
| `g_MpQuickTeamScenarioMenuDialog` | OG | Variant of scenario renderer | g_MpScenarios[] |
| `g_MpWeaponsMenuDialog` | OG | New ImGui — weapon set picker | Asset Catalog weapons |
| `g_MpSelectRandomWeaponsMenuDialog` | OG | Variant — random weapon config | Asset Catalog weapons |
| `g_MpQuickTeamWeaponsMenuDialog` | OG | Variant — quick team weapons | Asset Catalog weapons |
| `g_MpLimitsMenuDialog` | OG | New ImGui — sliders (time/score/teams) | Match config |
| `g_MpCombatOptionsMenuDialog` | OG | New ImGui — combat scenario options | Scenario config |
| `g_CtcOptionsMenuDialog` | OG | Scenario options variant | Scenario config |
| `g_HtmOptionsMenuDialog` | OG | Scenario options variant | Scenario config |
| `g_HtbOptionsMenuDialog` | OG | Scenario options variant | Scenario config |
| `g_KohOptionsMenuDialog` | OG | Scenario options variant | Scenario config |
| `g_PacOptionsMenuDialog` | OG | Scenario options variant | Scenario config |
| `g_ExtGameOptionsMenuDialog` | OG | Extended game options | Match config |

**Files to create**: `pdgui_menu_mpsetup.cpp` (new — unified MP setup), or extend `pdgui_menu_room.cpp`
**Data to wire**: Asset Catalog queries for arenas/weapons/scenarios, match config writes
**Design note**: In modern flow, room.cpp already has arena/scenario/weapon selection. This batch may be **absorbed into room.cpp** if the legacy combat sim entry point (`g_CombatSimulatorMenuDialog`) is retired.

---

### Batch 6: Bot (Simulant) Setup (5 screens)

**Effort**: 1-2 sessions | **Screens**: 5 | **Complexity**: Complex
**Why here**: Depends on character data from Batch 5. Bot setup is self-contained once data layer is ready.

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_MpSimulantsMenuDialog` | OG | New ImGui — bot roster list with add/edit/remove | matchslot array |
| `g_MpAddSimulantMenuDialog` | OG | New ImGui — add bot (character + difficulty + type) | Asset Catalog bodies/heads |
| `g_MpChangeSimulantMenuDialog` | OG | Variant of add — change existing bot | Asset Catalog bodies/heads |
| `g_MpEditSimulantMenuDialog` | OG | New ImGui — edit bot details (name, behavior, team) | Bot config |
| `g_MpSimulantCharacterMenuDialog` | OG | New ImGui — character picker for bot | Asset Catalog bodies/heads |

**Files to modify**: Extend room.cpp bot setup or create `pdgui_menu_botsetup.cpp`
**Data to wire**: `matchslot.body_id`, `matchslot.head_id`, bot difficulty enum, sim type enum
**Design note**: Room.cpp already has inline bot setup. This batch ensures legacy path works too.

---

### Batch 7: MP Advanced Setup & Quick Paths (11 screens)

**Effort**: 1-2 sessions | **Screens**: 11 | **Complexity**: Medium
**Why here**: These are navigation hubs that link to already-ported screens from Batches 5-6.

| Dialog | Current | Action | Notes |
|--------|---------|--------|-------|
| `g_CombatSimulatorMenuDialog` | OG | Redirect to room.cpp, or new ImGui hub | Consider retiring |
| `g_MpAdvancedSetupMenuDialog` | OG | New ImGui — setup hub linking to arena/scenario/etc. | Navigation menu |
| `g_MpAdvancedSetupViaAdvChallengeMenuDialog` | OG | Variant entry point | Navigation menu |
| `g_MpQuickGoMenuDialog` | OG | New ImGui — quick start with defaults | Match config |
| `g_MpQuickTeamGameSetupMenuDialog` | OG | Quick team setup hub | Match config |
| `g_MpQuickTeamMenuDialog` | OG | Quick team root | Navigation menu |
| `g_MpStuffMenuDialog` | OG | MP extras menu | Navigation menu |
| `g_MpStuffViaAdvChallengeMenuDialog` | OG | Variant entry point | Navigation menu |
| `g_MpPlayerSetupViaAdvMenuDialog` | OG | Player setup hub variant | Player config |
| `g_MpPlayerSetupViaAdvChallengeMenuDialog` | OG | Player setup hub variant | Player config |
| `g_MpPlayerSetupViaQuickGoMenuDialog` | OG | Player setup hub variant | Player config |

**Files to modify**: `pdgui_menu_room.cpp` or new `pdgui_menu_mpsetup.cpp`
**Design note**: Many of these "via" variants exist because N64 menus had different entry points to the same screen. In ImGui, a single parameterized renderer handles all variants.

---

### Batch 8: MP Pause & In-Game (6 screens)

**Effort**: 1 session | **Screens**: 6 | **Complexity**: Medium

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_MpPauseControlMenuDialog` | OG | New ImGui — control settings (shares with options) | pd.ini config |
| `g_MpPauseInventoryMenuDialog` | OG | New ImGui — current weapons/equipment list | Player weapon array |
| `g_MpPausePlayerStatsMenuDialog` | OG | New ImGui — live player stats table | Runtime stats |
| `g_MpPausePlayerRankingMenuDialog` | OG | New ImGui — live ranking list | Runtime stats |
| `g_MpPauseTeamRankingsMenuDialog` | OG | New ImGui — live team ranking | Runtime stats |
| `g_MpPlayerOptionsMenuDialog` | OG | New ImGui — player options during match | Player config |

**Files to modify**: `pdgui_menu_mpingame.cpp`
**Data to wire**: Live match stats from `mplayerGetScore()`, weapon inventory, team data

---

### Batch 9: 2-Player Split-Screen (13 screens)

**Effort**: 2 sessions | **Screens**: 13 | **Complexity**: Medium
**Why last**: Split-screen is low priority for PC port. Many screens are H/V layout variants that share 80% code.

| Dialog | Current | Action | Notes |
|--------|---------|--------|-------|
| `g_2PMissionOptionsHMenuDialog` | OG | Clone solo options, parameterized | H layout |
| `g_2PMissionOptionsVMenuDialog` | OG | Clone solo options, parameterized | V layout |
| `g_2PMissionBriefingHMenuDialog` | OG | Clone solo briefing, parameterized | H layout |
| `g_2PMissionBriefingVMenuDialog` | OG | Clone solo briefing, parameterized | V layout |
| `g_2PMissionControlStyleMenuDialog` | OG | Clone control style, parameterized | Shared |
| `g_2PMissionPauseHMenuDialog` | OG | Clone solo pause, parameterized | H layout |
| `g_2PMissionPauseVMenuDialog` | OG | Clone solo pause, parameterized | V layout |
| `g_2PMissionAbortVMenuDialog` | OG | Clone abort, parameterized | V layout |
| `g_2PMissionAudioOptionsVMenuDialog` | OG | Clone audio options, parameterized | V layout |
| `g_2PMissionVideoOptionsMenuDialog` | OG | Clone video options, parameterized | Shared |
| `g_2PMissionDisplayOptionsVMenuDialog` | OG | Clone display options, parameterized | V layout |
| `g_2PMissionInventoryHMenuDialog` | OG | Clone inventory, parameterized | H layout |
| `g_2PMissionInventoryVMenuDialog` | OG | Clone inventory, parameterized | V layout |

**Files to create**: `pdgui_menu_2player.cpp` (new — all 2P variants)
**Design note**: On PC with ImGui, H/V split distinction is less relevant. Could render all 2P menus full-screen with a "Player 1 / Player 2" tab switcher instead of actual split rendering.

---

### Batch 10: Training NULL-FN Screens (12 screens)

**Effort**: 2-3 sessions | **Screens**: 12 | **Complexity**: Complex
**Why last**: These require solving the 3D model preview problem — rendering N64 models into ImGui texture.

| Dialog | Current | Blocker | Solution |
|--------|---------|---------|----------|
| `g_FrWeaponListMenuDialog` | NULL-FN | GBI weapon rendering | Use `pdguiCharPreview` pipeline for weapons |
| `g_BioListMenuDialog` | NULL-FN | Opaque bio structs | Expose bio data to ImGui via accessor functions |
| `g_BioProfileMenuDialog` | NULL-FN | 3D character model | `pdguiCharPreview` — already works for bot setup |
| `g_DtListMenuDialog` | NULL-FN | Opaque DT structs | Expose device training data via accessors |
| `g_DtDetailsMenuDialog` | NULL-FN | 3D weapon model | Extend `pdguiCharPreview` for weapon models |
| `g_HtDetailsMenuDialog` | NULL-FN | MENUITEMTYPE_MODEL | Extend model preview pipeline |
| `g_HangarListMenuDialog` | NULL-FN | 3D vehicle models | Extend model preview for vehicles |
| `g_HangarVehicleHolographMenuDialog` | NULL-FN | 3D hologram render | Offscreen FBO → ImGui texture |
| `g_HangarVehicleDetailsMenuDialog` | NULL-FN | 3D model + GBI | Extend model preview |
| `g_HangarLocationDetailsMenuDialog` | NULL-FN | Location data | Text-based alternative acceptable |
| `g_SoloMissionControlStyleMenuDialog` | OG | Controller diagram | ImGui diagram (no 3D needed) |
| `g_MpControlMenuDialog` | OG | Control layout | ImGui diagram |

**Prerequisite**: `pdguiCharPreview` pipeline must be extended to support weapon and vehicle models, not just character bodies/heads.
**Files to modify**: `pdgui_menu_training.cpp` (replace NULL renderFns), `pdgui_charpreview.cpp` (extend)

---

### Batch 11: MP Player Config & Stats (5 screens)

**Effort**: 1 session | **Screens**: 5 | **Complexity**: Medium

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_MpCharacterMenuDialog` | OG | New ImGui — categorized character picker | Asset Catalog |
| `g_MpPlayerStatsMenuDialog` | OG | New ImGui — player lifetime stats | Stats arrays |
| `g_MpLoadSettingsMenuDialog` | OG | New ImGui — saved setup list | Save files |
| `g_MpLoadPresetMenuDialog` | OG | New ImGui — preset list | Preset data |
| `g_MpLoadPlayerMenuDialog` | OG | New ImGui — saved player list | Save files |

**Files to modify**: `pdgui_menu_mpsettings.cpp` or new `pdgui_menu_playerconfig.cpp`

---

### Batch 12: Music & Misc (4 screens)

**Effort**: 1 session | **Screens**: 4 | **Complexity**: Simple-Medium

| Dialog | Current | Action | Data Source |
|--------|---------|--------|------------|
| `g_MpSelectTunesMenuDialog` | OG | New ImGui — music track picker | Music track list |
| `g_MpSoundtrackMenuDialog` | OG | New ImGui — soundtrack config | Music config |
| `g_MpTeamNamesMenuDialog` | OG | New ImGui — team name list | Team arrays |
| `g_MpChallengesMenuDialog` | OG | Extend existing challenges renderer | g_MpChallenges[] |

**Files to modify**: `pdgui_menu_mpsettings.cpp`, `pdgui_menu_challenges.cpp`

---

### Implementation Rules (from d5-full-menu-overhaul.md — binding)

Every screen ported in any batch MUST follow these rules from the master design doc:

1. **UX Guidelines** (d5-full-menu-overhaul.md §Menu UX Guidelines): D-pad nav, circular wrapping, A/B confirm/cancel, LB/RB tabs, max 2-3 levels deep, 5-9 items per screen
2. **UI Scaling** (d5-full-menu-overhaul.md §UI Scaling): Reference 1080p, `scale = viewport_height / 1080.0f`, font loaded at scaled size, `pdguiGetSafeArea()` for all positioning
3. **Accessibility** (d5-full-menu-overhaul.md §Accessibility): Min 16px text, 4.5:1 contrast, never color-only
4. **Layout Patterns** (d5-full-menu-overhaul.md §Layout Patterns): Labels left controls right, dropdowns alphabetized with categories
5. **Hybrid Input** (d5-full-menu-overhaul.md §Hybrid Input): Last device wins, 500ms debounce
6. **Catalog-first** (constraints.md): All asset references use catalog ID strings, never raw indices
7. **Hotswap registration** (pdgui_hotswap.cpp): Register via `pdguiHotswapRegister(&dialogdef, renderFn, "name")`
8. **State transitions**: Extract game logic from legacy `menuhandler*` callbacks into standalone functions; ImGui renderers call those functions directly

---

## Part 5: Schedule Summary

| Batch | Focus | Screens | Sessions | Dependencies |
|-------|-------|---------|----------|-------------|
| **1** | Simple confirmations & file mgmt | 8 | 1 | None |
| **2** | Co-op / Counter-Op flow | 4 | 1 | Batch 1 (exit game) |
| **3** | CI Options tree | 9 | 2 | None |
| **4** | Cheats & Cinema | 10 | 1-2 | None |
| **5** | MP Setup Core (arena/weapon/scenario/limits) | 14 | 2-3 | None |
| **6** | Bot (Simulant) Setup | 5 | 1-2 | Batch 5 (character data) |
| **7** | MP Advanced/Quick paths | 11 | 1-2 | Batches 5-6 |
| **8** | MP Pause & In-Game | 6 | 1 | None |
| **9** | 2-Player Split-Screen | 13 | 2 | Batches 3, 8 (options, pause) |
| **10** | Training NULL-FN (3D preview) | 12 | 2-3 | Model preview pipeline |
| **11** | MP Player Config & Stats | 5 | 1 | None |
| **12** | Music & Misc | 4 | 1 | None |
| | **TOTAL** | **~101** | **~16-22** | |

**Note**: ~101 includes some screens that are already functional via type-based fallback but would benefit from dedicated renderers. The strict "needs work" count is ~62.

### Parallelizable Batches

These batches have no dependencies on each other and can be worked in any order:
- Batches 1, 3, 4, 5, 8, 11, 12 are all independent
- Batch 2 needs Batch 1 (trivially)
- Batches 6-7 need Batch 5
- Batch 9 needs Batches 3 and 8
- Batch 10 is standalone but complex (3D preview prerequisite)

### Recommended Order for Maximum Impact

1. **Batch 1** (simple wins, 1 session)
2. **Batch 3** (options — high visibility, 2 sessions)
3. **Batch 4** (cheats/cinema — self-contained, 1-2 sessions)
4. **Batch 2** (co-op/anti — completes solo flow, 1 session)
5. **Batch 5** (MP setup core — biggest batch, 2-3 sessions)
6. **Batch 6** (bots — builds on batch 5, 1-2 sessions)
7. **Batch 7** (advanced paths — navigation hubs, 1-2 sessions)
8. **Batch 8** (MP pause — standalone, 1 session)
9. **Batch 11** (player config, 1 session)
10. **Batch 12** (music/misc, 1 session)
11. **Batch 9** (2P split — low priority, 2 sessions)
12. **Batch 10** (training 3D — complex, 2-3 sessions)

---

## Part 6: Dead Code to Remove

After all batches complete, these files/systems can be stripped:

### Entire Files (candidates for deletion)
- `src/game/fmb.c` — 4MB mode dialogs (12 dialogs, all DEAD)
- N64 pak dialog code in `src/game/menu.c` (lines ~6000-6600) — 8 DEAD dialogs

### Code Sections to Strip
- `menuTick()` / `menutickMain()` — legacy per-frame menu processing
- `menuRender()` / `menuitemRender()` — legacy N64-style rendering (already P10 D5.7 removed)
- `menuUpdateCursor()` — legacy cursor positioning
- `g_Menus[]` / `g_AmMenus[]` per-player menu state — replace with ImGui menu stack
- Legacy `menuhandler*` functions that ONLY do rendering (not game logic)
- `contGetButton()` usage in menu code — replaced by SDL events

### Functions to Audit Before Removal
- `menuhandlerAcceptMission()` — contains game logic (triggers stage load)
- `menuhandlerSoloDifficulty()` — sets `g_MissionConfig.difficulty`
- `menuhandlerAbortMission()` — triggers stage exit
- `menuhandlerExitGame()` — triggers app quit
- All handlers that modify `g_MissionConfig` or trigger state transitions

**Rule**: Extract game logic from handlers into standalone functions callable from ImGui renderers. Then delete the handler.

---

## Part 7: Key Decisions Needed

1. **Retire legacy combat sim entry?** — `g_CombatSimulatorMenuDialog` (setup.c:6454) is the N64 path to MP setup. Room.cpp is the modern path. If we retire the legacy path, Batches 5-7 shrink dramatically (room.cpp already has arena/scenario/weapon selection).

2. **Merge CI Options + Extended Settings?** — On PC, having both "Options" (N64-era audio/video/display/control) and "Settings" (PC-era graphics/audio/mouse) is confusing. Recommendation: merge into one unified Settings screen.

3. **2P split-screen priority** — Is local split-screen a v0.1.0 requirement? If not, Batch 9 can defer.

4. **Training 3D preview scope** — NULL-FN screens require rendering 3D models (weapons, vehicles, characters) into ImGui textures. The `pdguiCharPreview` pipeline exists for characters. Extending it to weapons/vehicles is non-trivial. Alternative: text-only versions for v0.1.0, add 3D previews in v0.2.0.
