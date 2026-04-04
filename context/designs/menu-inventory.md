# Menu Inventory — All Reachable Screens

> **Last updated**: 2026-04-03 (S135)
> **Purpose**: Complete catalog of every screen, dialog, and popup the player can reach.
> Use this as the definitive checklist for D5.7 OG removal and for regression testing.
>
> **Implementation key**:
> - `ImGui` — custom ImGui renderFn registered via `pdguiHotswapRegister()`
> - `ImGui (standalone)` — dedicated ImGui window, not a `menudialogdef` (no hotswap)
> - `OG (forced)` — registered with NULL renderFn; legacy N64 rendering runs (no ImGui)
> - `OG (native)` — registered NULL in `pdgui_menu_warning.cpp`; uses native type-based OG render (text inputs, confirmations)
> - `OG` — not registered in hotswap at all; legacy `tickFn` rendering only
> - `Type-based` — matched by `MENUDIALOGTYPE_*`; shared ImGui renderer for all dialogs of that type
> - `Stub` — registered with ImGui renderFn but renderer is incomplete/placeholder
> - `Done` in D5 Phase = screen is complete and not targeted for change

---

## Main Navigation

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Main Menu / CI Menu (via PC) | ImGui | `pdgui_menu_mainmenu.cpp` / `g_CiMenuViaPcMenuDialog` | D5.6 | Entry point; hosts solo/mp/training/settings/modding |
| Main Menu / CI Menu (via Pause) | ImGui | `pdgui_menu_mainmenu.cpp` / `g_CiMenuViaPauseMenuDialog` | D5.6 | Pause variant; same renderer, different entrypoint |
| Agent Select ("Choose Your Reality") | ImGui | `pdgui_menu_agentselect.cpp` / `g_FilemgrFileSelectMenuDialog` | Done | Save file picker; create/delete/select agents |
| Agent Create (Name Entry) | ImGui | `pdgui_menu_agentcreate.cpp` / `g_FilemgrEnterNameMenuDialog` | Done | New agent name; keyboard entry |
| Change Agent | ImGui | `pdgui_menu_mainmenu.cpp` / `g_ChangeAgentMenuDialog` | Done | Switch save file without returning to title |
| Exit Game | OG | `src/game/mainmenu.c` / `g_ExitGameMenuDialog` | D5.7 | Confirm quit to desktop |

---

## Solo Mission Flow

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Mission Select | ImGui | `pdgui_menu_solomission.cpp` / `g_SelectMissionMenuDialog` | D5.3 | All missions shown regardless of unlock; no briefing image; D5.3 rebuilds with two-panel layout |
| Solo Difficulty | ImGui | `pdgui_menu_solomission.cpp` / `g_SoloMissionDifficultyMenuDialog` | D5.3 | Difficulty selection after mission chosen |
| Solo Briefing | ImGui | `pdgui_menu_solomission.cpp` / `g_SoloMissionBriefingMenuDialog` | D5.3 | Pre-mission briefing text |
| Pre/Post Mission Briefing | ImGui | `src/game/mainmenu.c` + `pdgui_menu_solomission.cpp` / `g_PreAndPostMissionBriefingMenuDialog` | D5.3 | Shown before and after mission; reuses briefing renderer |
| Accept Mission | ImGui | `pdgui_menu_solomission.cpp` / `g_AcceptMissionMenuDialog` | D5.3 | Confirm + show objectives; D5.3 makes it a confirmation step only |
| PD Mode Settings | OG | `src/game/mainmenu.c` / `g_PdModeSettingsMenuDialog` | D5.7 | Classic mode toggle; hidden feature |

---

## Solo In-Game (Pause)

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Solo Pause Menu | ImGui | `pdgui_menu_solomission.cpp` / `g_SoloMissionPauseMenuDialog` | D5.2 | Inventory/Objectives sub-screens currently broken |
| Solo Inventory | OG (forced) | `pdgui_menu_solomission.cpp` / `g_SoloMissionInventoryMenuDialog` | D5.2 | NULL renderFn; **TRAPPING** (B-98); no working exit; D5.2 adds real renderFn |
| Solo Options (in-game) | ImGui | `pdgui_menu_solomission.cpp` / `g_SoloMissionOptionsMenuDialog` | Done | Options while paused; works correctly |
| Solo Control Style | OG (forced) | `pdgui_menu_solomission.cpp` / `g_SoloMissionControlStyleMenuDialog` | D5.7 | NULL renderFn; controller diagram; legacy 3D preview; audit Back path |
| Abort Mission (confirm) | ImGui | `pdgui_menu_solomission.cpp` / `g_MissionAbortMenuDialog` | D5.2 | Danger confirmation; wired but needs D5.2 review |

---

## Solo End Game

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Mission Complete | Stub | `pdgui_menu_endscreen.cpp` / `g_SoloMissionEndscreenCompletedMenuDialog` | D5.4 | Registered; incomplete renderer |
| Mission Failed | Stub | `pdgui_menu_endscreen.cpp` / `g_SoloMissionEndscreenFailedMenuDialog` | D5.4 | Registered; incomplete renderer |
| Objectives Complete | Stub | `pdgui_menu_endscreen.cpp` / `g_SoloEndscreenObjectivesCompletedMenuDialog` | D5.4 | Registered; stub |
| Objectives Failed | Stub | `pdgui_menu_endscreen.cpp` / `g_SoloEndscreenObjectivesFailedMenuDialog` | D5.4 | Registered; stub |
| Retry Mission | Stub | `pdgui_menu_endscreen.cpp` / `g_RetryMissionMenuDialog` | D5.4 | Registered; stub |
| Next Mission | Stub | `pdgui_menu_endscreen.cpp` / `g_NextMissionMenuDialog` | D5.4 | Registered; stub |
| Mission Continue or Replay | Stub | `pdgui_menu_endscreen.cpp` / `g_MissionContinueOrReplyMenuDialog` | D5.4 | Registered; stub |

---

## Co-op / Counter-Operative Flow

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Co-op Difficulty | OG | `src/game/mainmenu.c` / `g_CoopMissionDifficultyMenuDialog` | D5.7 | OG difficulty picker for co-op |
| Co-op Options | OG | `src/game/mainmenu.c` / `g_CoopOptionsMenuDialog` | D5.7 | Co-op player options |
| Counter-Op Difficulty | OG | `src/game/mainmenu.c` / `g_AntiMissionDifficultyMenuDialog` | D5.7 | OG difficulty picker for counter-op |
| Counter-Op Options | OG | `src/game/mainmenu.c` / `g_AntiOptionsMenuDialog` | D5.7 | Counter-op player options |

---

## 2-Player Split-Screen

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| 2P Options (Horizontal) | OG | `src/game/mainmenu.c` / `g_2PMissionOptionsHMenuDialog` | D5.7 | OG split-screen options (H layout) |
| 2P Options (Vertical) | OG | `src/game/mainmenu.c` / `g_2PMissionOptionsVMenuDialog` | D5.7 | OG split-screen options (V layout) |
| 2P Briefing (Horizontal) | OG | `src/game/mainmenu.c` / `g_2PMissionBriefingHMenuDialog` | D5.7 | OG briefing H layout |
| 2P Briefing (Vertical) | OG | `src/game/mainmenu.c` / `g_2PMissionBriefingVMenuDialog` | D5.7 | OG briefing V layout |
| 2P Control Style | OG | `src/game/mainmenu.c` / `g_2PMissionControlStyleMenuDialog` | D5.7 | OG controller layout |
| 2P Pause (Horizontal) | OG | `src/game/mainmenu.c` / `g_2PMissionPauseHMenuDialog` | D5.7 | OG pause H layout |
| 2P Pause (Vertical) | OG | `src/game/mainmenu.c` / `g_2PMissionPauseVMenuDialog` | D5.7 | OG pause V layout |
| 2P Abort (Vertical) | OG | `src/game/mainmenu.c` / `g_2PMissionAbortVMenuDialog` | D5.7 | OG abort confirmation V layout |
| 2P Mission Complete (H) | Stub | `pdgui_menu_endscreen.cpp` / `g_2PMissionEndscreenCompletedHMenuDialog` | D5.4 | Registered; stub |
| 2P Mission Failed (H) | Stub | `pdgui_menu_endscreen.cpp` / `g_2PMissionEndscreenFailedHMenuDialog` | D5.4 | Registered; stub |
| 2P Mission Complete (V) | Stub | `pdgui_menu_endscreen.cpp` / `g_2PMissionEndscreenCompletedVMenuDialog` | D5.4 | Registered; stub |
| 2P Mission Failed (V) | Stub | `pdgui_menu_endscreen.cpp` / `g_2PMissionEndscreenFailedVMenuDialog` | D5.4 | Registered; stub |
| 2P Objectives Complete (V) | Stub | `pdgui_menu_endscreen.cpp` / `g_2PMissionEndscreenObjectivesCompletedVMenuDialog` | D5.4 | Registered; stub |
| 2P Objectives Failed (V) | Stub | `pdgui_menu_endscreen.cpp` / `g_2PMissionEndscreenObjectivesFailedVMenuDialog` | D5.4 | Registered; stub |

---

## Options / Settings

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| CI Options (via PC) | OG | `src/game/mainmenu.c` / `g_CiOptionsViaPcMenuDialog` | D5.6 | OG options tree root (entered from PC menu) |
| CI Options (via Pause) | OG | `src/game/mainmenu.c` / `g_CiOptionsViaPauseMenuDialog` | D5.6 | OG options tree root (entered from pause) |
| Audio Options | OG | `src/game/mainmenu.c` / `g_AudioOptionsMenuDialog` | D5.6 | OG; PC audio settings are in Extended Settings |
| Video Options | OG | `src/game/mainmenu.c` / `g_VideoOptionsMenuDialog` | D5.6 | OG; PC video settings are in Extended Settings |
| Mission Display Options | OG | `src/game/mainmenu.c` / `g_MissionDisplayOptionsMenuDialog` | D5.6 | OG display prefs |
| Mission Control Options | OG | `src/game/mainmenu.c` / `g_MissionControlOptionsMenuDialog` | D5.6 | OG control prefs |
| 2P Audio Options (V) | OG | `src/game/mainmenu.c` / `g_2PMissionAudioOptionsVMenuDialog` | D5.7 | OG 2P audio V layout |
| 2P Video Options | OG | `src/game/mainmenu.c` / `g_2PMissionVideoOptionsMenuDialog` | D5.7 | OG 2P video |
| 2P Display Options (V) | OG | `src/game/mainmenu.c` / `g_2PMissionDisplayOptionsVMenuDialog` | D5.7 | OG 2P display V layout |
| CI Control Options | OG | `src/game/mainmenu.c` / `g_CiControlOptionsMenuDialog` | D5.7 | OG CI-mode control settings |
| CI Control Options 2 | OG | `src/game/mainmenu.c` / `g_CiControlOptionsMenuDialog2` | D5.7 | OG CI-mode secondary controls |
| CI Control Style | OG | `src/game/mainmenu.c` / `g_CiControlStyleMenuDialog` | D5.7 | OG CI-mode controller layout |
| CI Control Style P2 | OG | `src/game/mainmenu.c` / `g_CiControlStylePlayer2MenuDialog` | D5.7 | OG CI-mode P2 controller layout |
| CI Display Menu | OG | `src/game/mainmenu.c` / `g_CiDisplayMenuDialog` | D5.7 | OG CI-mode display settings |
| CI Display P2 | OG | `src/game/mainmenu.c` / `g_CiDisplayPlayer2MenuDialog` | D5.7 | OG CI-mode P2 display |
| CI Control P2 | OG | `src/game/mainmenu.c` / `g_CiControlPlayer2MenuDialog` | D5.7 | OG CI-mode P2 control config |
| Extended Settings (PC) | ImGui | `pdgui_menu_mainmenu.cpp` / `g_ExtendedMenuDialog` | Done | PC-specific: graphics, audio, controls, mouse |
| Extended Video | ImGui | `pdgui_menu_mainmenu.cpp` / `g_ExtendedVideoMenuDialog` | Done | PC video settings tab |
| Extended Audio | ImGui | `pdgui_menu_mainmenu.cpp` / `g_ExtendedAudioMenuDialog` | Done | PC audio settings tab |
| Extended Mouse | ImGui | `pdgui_menu_mainmenu.cpp` / `g_ExtendedMouseMenuDialog` | Done | PC mouse settings tab |
| Cheats Menu | OG | `src/game/mainmenu.c` / `g_CheatsMenuDialog` | D5.7 | OG cheats unlock screen |
| Cinema / Cinematics Viewer | OG | `src/game/mainmenu.c` / `g_CinemaMenuDialog` | D5.7 | OG cutscene replay viewer |

---

## Combat Simulator / Multiplayer

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Multiplayer Menu | ImGui | `pdgui_menu_network.cpp` / `g_NetMenuDialog` | Done | Host / join / browse; online/local selection |
| Match Setup | ImGui | `pdgui_menu_matchsetup.cpp` / `g_MatchSetupMenuDialog` | D5.2, D5.5 | Bot head/body mismatch bug; generic bot names; D5.2 fixes sync, D5.5 polishes names |
| Player Handicaps | ImGui | `pdgui_menu_mpsettings.cpp` / `g_MpHandicapsMenuDialog` | Done | Per-player handicap sliders |
| Team Control | ImGui | `pdgui_menu_teamsetup.cpp` / `g_MpTeamsMenuDialog` | Done | Manual team assignment |
| Auto Team | ImGui | `pdgui_menu_teamsetup.cpp` / `g_MpAutoTeamMenuDialog` | Done | Auto-balance team assignment |
| MP Challenge List | ImGui | `pdgui_menu_challenges.cpp` / `g_MpChallengeListOrDetailsMenuDialog` | Done | Two-panel challenge browser |
| MP Completed Challenges | ImGui | `pdgui_menu_challenges.cpp` / `g_MpCompletedChallengesMenuDialog` | Done | Completed challenge list |
| MP Ready | OG (native) | `pdgui_menu_warning.cpp` / `g_MpReadyMenuDialog` | D5.7 | Ready-up gate; native OG text |
| Setup OK | OG (native) | `pdgui_menu_warning.cpp` / `g_StatusOkDialog` | D5.7 | Setup confirmed |
| Setup Error | OG (native) | `pdgui_menu_warning.cpp` / `g_StatusErrorDialog` | D5.7 | Setup validation error |
| Delete Setup (confirm) | OG (native) | `pdgui_menu_warning.cpp` / `g_DeleteSetupDialog` | D5.7 | Confirm saved setup deletion |
| Save Setup Name | OG (native) | `pdgui_menu_warning.cpp` / `g_MpSaveSetupNameMenuDialog` | D5.7 | Name text input for saving a setup |
| MP Player Name | OG (native) | `pdgui_menu_warning.cpp` / `g_MpPlayerNameMenuDialog` | D5.7 | Player name text input |
| Change Team Name | OG (native) | `pdgui_menu_warning.cpp` / `g_MpChangeTeamNameMenuDialog` | D5.7 | Team name text input |

---

## MP In-Game / End Game

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| MP Individual Game Over | Stub | `pdgui_menu_mpingame.cpp` / `g_MpEndscreenIndGameOverMenuDialog` | D5.4 | Registered in both endscreen + mpingame; stub |
| MP Team Game Over | Stub | `pdgui_menu_mpingame.cpp` / `g_MpEndscreenTeamGameOverMenuDialog` | D5.4 | Registered in both; stub |
| MP Player Ranking | Stub | `pdgui_menu_mpingame.cpp` / `g_MpEndscreenPlayerRankingMenuDialog` | D5.4 | Registered in both; stub |
| MP Team Ranking | Stub | `pdgui_menu_mpingame.cpp` / `g_MpEndscreenTeamRankingMenuDialog` | D5.4 | Registered in both; stub |
| MP Player Stats | Stub | `pdgui_menu_mpingame.cpp` / `g_MpEndscreenPlayerStatsMenuDialog` | D5.4 | Registered in both; stub |
| MP Save Player | Stub | `pdgui_menu_mpingame.cpp` / `g_MpEndscreenSavePlayerMenuDialog` | D5.4 | Registered in mpingame; stub |
| MP Confirm Name (post-match) | OG (native) | `pdgui_menu_warning.cpp` / `g_MpEndscreenConfirmNameMenuDialog` | D5.7 | Text input for post-match name save |
| MP Challenge Completed | Stub | `pdgui_menu_endscreen.cpp` / `g_MpEndscreenChallengeCompletedMenuDialog` | D5.4 | Registered in endscreen; stub |
| MP Challenge Cheated | Stub | `pdgui_menu_endscreen.cpp` / `g_MpEndscreenChallengeCheatedMenuDialog` | D5.4 | Registered in endscreen; stub |
| MP Challenge Failed | Stub | `pdgui_menu_endscreen.cpp` / `g_MpEndscreenChallengeFailedMenuDialog` | D5.4 | Registered in endscreen; stub |

---

## Network / Online (Dedicated Server)

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Social Lobby | ImGui (standalone) | `pdgui_lobby.cpp` + `pdgui_menu_lobby.cpp` | D5.4 | Player list (left) + room list (right) + chat stub + Disconnect |
| Room Interior | ImGui (standalone) | `pdgui_menu_room.cpp` | D5.4 | Tab bar: Combat Sim / Campaign / Counter-Op; Co-Op and Counter-Op tabs active but unsupported (D5.4 gates them) |
| Join Address | OG (native) | `pdgui_menu_warning.cpp` / `g_NetJoinAddressDialog` | D5.7 | IP/connect-code text input dialog |
| Joining Game | OG (native) | `pdgui_menu_warning.cpp` / `g_NetJoiningDialog` | D5.7 | Connecting progress dialog |
| Co-op Host | OG (native) | `pdgui_menu_warning.cpp` / `g_NetCoopHostMenuDialog` | D5.7 | Co-op host setup; native OG |

---

## File Management Dialogs

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Rename File | OG (native) | `pdgui_menu_warning.cpp` / `g_FilemgrRenameMenuDialog` | D5.7 | Save file rename text input |
| Duplicate Name Error | OG (native) | `pdgui_menu_warning.cpp` / `g_FilemgrDuplicateNameMenuDialog` | D5.7 | Name already taken error |
| File Saved Confirmation | OG (native) | `pdgui_menu_warning.cpp` / `g_FilemgrFileSavedMenuDialog` | D5.7 | Save confirmed notification |

---

## Training / Carrington Institute

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Firing Range Difficulty | ImGui | `pdgui_menu_training.cpp` / `g_FrDifficultyMenuDialog` | Done | FR difficulty picker |
| FR Pre-Game Info | ImGui | `pdgui_menu_training.cpp` / `g_FrTrainingInfoPreGameMenuDialog` | Done | Instructions before FR begins |
| FR In-Game Info | ImGui | `pdgui_menu_training.cpp` / `g_FrTrainingInfoInGameMenuDialog` | Done | Tips shown during FR |
| FR Completed | ImGui | `pdgui_menu_training.cpp` / `g_FrCompletedMenuDialog` | Done | FR success; score display |
| FR Failed | ImGui | `pdgui_menu_training.cpp` / `g_FrFailedMenuDialog` | Done | FR failure |
| FR Weapon List | ImGui | `pdgui_menu_training.cpp` / `g_FrWeaponListMenuDialog` | Done | Weapons available for current FR |
| FR Weapons Available (in-mission) | OG (forced) | `pdgui_menu_solomission.cpp` / `g_FrWeaponsAvailableMenuDialog` | D5.7 | NULL renderFn; audit Back path before removal |
| Bio List | ImGui | `pdgui_menu_training.cpp` / `g_BioListMenuDialog` | Done | Character biography list |
| Bio Profile | ImGui | `pdgui_menu_training.cpp` / `g_BioProfileMenuDialog` | Done | Character profile card |
| Bio Text | ImGui | `pdgui_menu_training.cpp` / `g_BioTextMenuDialog` | Done | Biography full text |
| DarkSim Challenge List | ImGui | `pdgui_menu_training.cpp` / `g_DtListMenuDialog` | Done | Dark simulant challenge list |
| DarkSim Details | ImGui | `pdgui_menu_training.cpp` / `g_DtDetailsMenuDialog` | Done | DarkSim challenge details |
| DarkSim Failed | ImGui | `pdgui_menu_training.cpp` / `g_DtFailedMenuDialog` | Done | DarkSim failure screen |
| DarkSim Completed | ImGui | `pdgui_menu_training.cpp` / `g_DtCompletedMenuDialog` | Done | DarkSim success screen |
| HoverBike Challenge List | ImGui | `pdgui_menu_training.cpp` / `g_HtListMenuDialog` | Done | HoverBike challenge list |
| HoverBike Details | ImGui | `pdgui_menu_training.cpp` / `g_HtDetailsMenuDialog` | Done | HoverBike challenge details |
| HoverBike Failed | ImGui | `pdgui_menu_training.cpp` / `g_HtFailedMenuDialog` | Done | HoverBike failure screen |
| HoverBike Completed | ImGui | `pdgui_menu_training.cpp` / `g_HtCompletedMenuDialog` | Done | HoverBike success screen |
| Now Safe | ImGui | `pdgui_menu_training.cpp` / `g_NowSafeMenuDialog` | Done | Safe-room cleared notification |
| Hangar Vehicle List | ImGui | `pdgui_menu_training.cpp` / `g_HangarListMenuDialog` | Done | Vehicle browser |
| Hangar Vehicle Holograph | ImGui | `pdgui_menu_training.cpp` / `g_HangarVehicleHolographMenuDialog` | Done | Vehicle hologram viewer |
| Hangar Vehicle Details | ImGui | `pdgui_menu_training.cpp` / `g_HangarVehicleDetailsMenuDialog` | Done | Vehicle detail panel |
| Hangar Location Details | ImGui | `pdgui_menu_training.cpp` / `g_HangarLocationDetailsMenuDialog` | Done | Location detail panel |

---

## Generic Danger / Success Dialogs (Type-Based)

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Generic Danger Dialog | Type-based ImGui | `pdgui_menu_warning.cpp` / `MENUDIALOGTYPE_DANGER` | D5.7 | All unregistered DANGER-type dialogs; shared confirmation renderer |
| Generic Success Dialog | Type-based ImGui | `pdgui_menu_warning.cpp` / `MENUDIALOGTYPE_SUCCESS` | D5.7 | All unregistered SUCCESS-type dialogs; shared confirmation renderer |

---

## PC-Only / Developer Tools

| Screen Name | Implementation | File / Dialog | D5 Phase | Notes |
|-------------|---------------|---------------|----------|-------|
| Update Screen | ImGui (standalone) | `pdgui_menu_update.cpp` | D5.6 | Update available / downloading; B-95 gate fix in D5.6 |
| Log Viewer | ImGui (standalone) | `pdgui_menu_logviewer.cpp` | None | Debug log browser; dev-only; not player-facing |
| Modding Hub | ImGui (standalone) | `pdgui_menu_moddinghub.cpp` | None | Three tabs: Mod Manager, INI Editor, Model Scale Tool |
| Mod Manager (tab) | ImGui | `pdgui_menu_modmgr.cpp` | None | Sub-component of Modding Hub; mod list + enable/disable |

---

## Summary Counts

| Implementation | Count |
|---------------|-------|
| ImGui (full custom renderer) | 53 |
| ImGui (standalone window) | 4 |
| Stub (registered, incomplete) | 17 |
| OG (forced, NULL renderFn) | 3 |
| OG (native, text/confirm) | 13 |
| OG (unregistered, legacy tickFn) | 28 |
| Type-based ImGui | 2 |
| **Total screens** | **120** |

> Counts are approximate — some dialogs appear in multiple variants (H/V layouts, via-PC/via-Pause).
> The OG total (28) is the primary target for D5.7.
