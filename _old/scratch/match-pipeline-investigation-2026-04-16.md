# Match / Mission Pipeline — End-to-End Investigation (2026-04-16)

> **Scope**: Full pipeline audit from title screen through gameplay back to main menu.
> Covers Solo, MP (Combat Simulator), Co-op, Counter-Op where they diverge.
> **Research only — no code changes.** All fix proposals are recommendations, not implementations.
>
> **Method**: Four parallel Explore-style agent dispatches (entry, in-match, exit, cross-cutting input/controller). Line numbers quoted are snapshots — verify before touching a site.
>
> **Status keys used below**: SOLID = default-focus + Esc/B + controller-nav all present · PARTIAL = functional with gaps · FLAG = controller-only user disadvantaged · DEAD = controller-only user cannot proceed.

---

## 1. Executive Summary

The pipeline works end-to-end on MKB. Controller-only parity is close but has ~15 fixable defects. Three findings are high-priority:

1. **GAP-1 / SP-13 hazard** — `menutick.c` Deep Sea co-op next-mission path jumps to `mainChangeToStage()` without `manifestClear(&g_ClientManifest)`. Matches the exact crash pattern documented in SP-13 (Bug A). Online co-op campaign advancement is a latent AV.
2. **C-1 Challenges menu** — opens with zero ImGui focus on a list-driven screen. Controller user cannot enter the list without an extra keypress to establish focus.
3. **Bug C (MP endscreen body invisible)** — still unresolved; root cause unknown. Instrumentation on the `renderMpEndscreen` early-returns is pending and should precede any structural change.

Medium findings cluster around two patterns:

- **Missing `SetWindowFocus()` on appear** in ~10 secondary dialogs (handicap, select tunes, team names, control diagram, cheats warning/unlock, team setup, playerconfig load-dialogs, moddinghub). The pattern is identical and amenable to a single sweep.
- **Pause / End-Game asymmetry** between solo and MP. Solo pause pushes `g_CtxImGuiMenu` via legacy push path; MP pause pushes dedicated `g_CtxPauseMenu`. Different priority IMCs, different pause flag behavior. Not a crash, but architectural drift.

Low-priority: text-entry with controller (systemic — no on-screen keyboard), controls sub-tab cycling (cannot swap KB vs Controller binding tabs with gamepad), occasional missing `SetItemDefaultFocus`, SP-14 client-side `g_NetMatchRoomId` not explicitly reset on match end.

---

## 2. Pipeline State Machine

### 2.1 Entry — Solo Mission

```
TitleScreen
  |
  v
[g_CiMenuViaPcMenuDialog]  renderMainMenu()
  pdgui_menu_mainmenu.cpp:2460
  default focus: SetKeyboardFocusHere(0) via s_NeedsFocus first appear
  B/Esc at view 0: menuPopDialog + restore game state
  B/Esc at view 1..5: s_MenuView = 0
  |
  +-- "Solo Play" (s_MenuView=1)
  |     |
  |     +-- "Solo Missions" ---> menuPushDialog(g_SelectMissionMenuDialog)
  |     |     |
  |     |     v
  |     |   [g_SelectMissionMenuDialog]  renderMissionSelect()
  |     |   pdgui_menu_solomission.cpp:629
  |     |   two-panel: left list / right detail
  |     |   default: auto-selects first accessible mission (s_DetailPanelFocus=false)
  |     |   controller: manual D-pad via s_DetailFocusIdx — NO ImGui nav focus (G-1)
  |     |     |
  |     |     v
  |     |   "Start Mission" action-bar button
  |     |     -> SM_SET_DIFFICULTY, lvSetDifficulty
  |     |     -> menuhandlerAcceptMission(MENUOP_SET)
  |     |     -> mainChangeToStage(g_MissionConfig.stagenum)
  |     |     -> inputCtxPopDeferred(&g_CtxImGuiMenu)
  |     |     -> menuStop()
  |     |
  |     +-- "Combat Simulator" --> pdguiSoloRoomOpen() -> Room screen (see 2.2)
  |
  +-- "Online Play" (s_MenuView=4)   [see 2.2 MP Entry]
  |
  +-- "Cinema / Settings / etc."

[Legacy chain, still registered but bypassed by new two-panel screen]:
  g_SoloMissionDifficultyMenuDialog -> renderDifficulty   (line 1268)
  g_SoloMissionBriefingMenuDialog   -> renderBriefing
  g_AcceptMissionMenuDialog         -> renderAcceptMission
```

### 2.2 Entry — Multiplayer / Combat Sim

```
MainMenu ("Online Play") -> renderOnlineView (inline in renderMainMenu)
  |
  +-- address InputText + server list  [g_NetMenuDialog: renderMultiplayerMenu, pdgui_menu_network.cpp:93]
  |     default focus: SetWindowFocus() only — InputText NOT auto-focused (G-6)
  |     Connect: connectCodeDecode -> netStartClientWithHolePunch
  |       -> menuPushDialog(g_NetJoiningDialog)
  |
  v (after connected, server sends SVC_ROOM_ASSIGN etc)
[Social Lobby, standalone ImGui]  pdgui_lobby.cpp / pdguiLobbyRender
  |
  v  (Enter room)
[pdguiRoomScreenRender]  pdgui_menu_room.cpp:~2180
  - On appear: SetWindowFocus(); inputCtxPush(g_CtxImGuiMenu) if not active
  - Tabs: Combat Simulator | Campaign | Counter-Op | Level Editor  (LB/RB cycles)
  - Left panel: bot list, scenario, arena, settings (leader editable)
  - Right panel: players
  - Sub-dialogs: handicaps, team setup, select tunes  [pushed via menuPushDialog]
  |
  +-- "Start Match" button (leader only)
        |
        +-- Solo (s_IsSoloMode): pdguiSoloRoomClose -> matchStart() [matchsetup.c:750]
        |     -> mpStartMatch, menuStop, inputCtxPopDeferred
        |
        +-- Network: netLobbyRequestStartWithSims() -> CLC_LOBBY_START
              |
              v
           SERVER ready gate [netmsg.c:4135]
              - ROOM_STATE_LOADING, SVC_MATCH_COUNTDOWN broadcast
              - wait for all CLC_READY or 30s timeout
              - mainChangeToStage() + netServerStageStart()
              - broadcast SVC_STAGE_START
              |
              v
           CLIENT netmsgSvcStageStartRead() [netmsg.c:~1328]
              - mpParticipantsFromLegacyChrslots
              - mpStartMatch, scenarioInitProps
              - memset g_MatchCountdownState   (dismiss countdown overlay)
              - menuStop
              - inputCtxPopDeferred(&g_CtxImGuiMenu)
              - CLC_STAGE_READY -> server
              |
              v
           FIRST GAMEPLAY FRAME
```

### 2.3 In-Match — Solo

```
[Gameplay] --(Start/Esc)--> playerPause(MENUROOT_MAINMENU)
  |  pausemode = PAUSEMODE_PAUSING
  v
menuPushRootDialog(&g_SoloMissionPauseMenuDialog, MENUROOT_MAINMENU)
  legacy-menu-push: inputCtxPush(&g_CtxImGuiMenu)   <-- NOT g_CtxPauseMenu (Gap 8)
  v
[g_SoloMissionPauseMenuDialog] renderPauseMenu()  pdgui_menu_solomission.cpp:2537
  default focus: s_PauseSelectIdx=0 = Resume (on IsWindowAppearing)
  D-pad up/down with wrap; B/Esc -> menuPopDialog (resume)
  |
  +-- Resume
  +-- Restart Mission -> s_RestartConfirm=true overlay (default Cancel)
  +-- Inventory --> renderInventory (line 2264)        <-- Gap 3: no per-item D-pad nav
  +-- Options --> renderOptions (line 3277)            <-- Gap 4: no LB/RB tab switch
  +-- Abort! --> renderAbortMission (default Cancel)
```

### 2.4 In-Match — MP / Combat Sim

```
[Gameplay] --(actionPressed ACTION_PAUSE)--> c1buttonsthisframe|=START_BUTTON  (bondmove.c:996,1077)
  |
  v
mpPushPauseDialog() [ingame.c:871]
  guard: g_MpSetup.paused != GAMEOVER && !g_MainIsEndscreen
  v
pdguiPauseMenuOpen() [pdgui_menu_pausemenu.cpp:223]
  - 100ms double-press cooldown
  - inputCtxPush(&g_CtxPauseMenu)   <-- sets s_GamePaused=1, activates g_ImcMenu + g_ImcPauseMenu
  - mpSetPaused(PAUSED) if NETMODE_NONE   (network game does NOT pause)
  v
pdguiPauseMenuRender  [standalone ImGui window "##PdPauseMenu"]
  Tabs: Rankings (0) | Settings (1) | End Game (inline danger button)
  default: Rankings tab.  NO D-pad tab switch (Gap 1 - in-match audit)
  Esc / GamepadStart -> close.  B -> cancel End-Game confirm or close.
  |
  +-- End Game -> "Confirm?" modal
        |
        +-- NETMODE_CLIENT: netDisconnect()  [bypasses endscreen — Gap 3 - exit]
        |     netDisconnect wasingame:
        |       mainEndStage + manifestClear + titleSetNextStage(CITRAINING)
        |       -> mainChangeToStage(CITRAINING)  -> main menu direct
        |
        +-- Offline: mainEndStage() -> natural endscreen path
```

### 2.5 Exit — Solo Success → Next Mission

```
Objectives complete -> mainEndStage()
  v
menutick.c prevmenuroot == -6 branch
  v
endscreenPushCoop() OR endscreenPushSolo()   [endscreen.c:1774 / 1847]
  -> menuPushRootDialog(&g_SoloMissionEndscreenCompletedMenuDialog,
                        MENUROOT_COOPCONTINUE)
  v (hotswap)
soloCompletedRender [pdgui_menu_endscreen.cpp:1073] -> renderSoloEndscreen(true)
  - inputCtxPush(g_CtxImGuiMenu) on first appear
  - 3-frame debounce (prevents A-through from skip-cutscene)
  - "Next Mission" (default, if any) | "Retry Mission"
  - B/Esc -> pdguiEndscreenExitToMainMenu  <-- Gap 7: B exits all instead of retry
  |
  +-- Next Mission -> pdguiEndscreenNextMission [pdgui_bridge.c:786]
        -> endscreenAdvance() [endscreen.c:478]
              g_MissionConfig.stageindex++
              clamp: if >= NUM_SOLOSTAGES -> NUM_SOLOSTAGES-1
              missionSetStageByCatalog(catalog_id)
        -> menuhandlerAcceptMission -> mainChangeToStage(stagenum)
        -> inputCtxPopDeferred
```

Deep Sea (final campaign stage): `pdguiEndscreenHasNextMission` returns false, "Next Mission" hidden, layout shows "Retry Mission" | "Main Menu". Correct.

### 2.6 Exit — Solo Fail → Retry / Quit

```
Bond dies or objectives fail -> menutick.c -6 branch
  -> endscreenPushSolo  (objectiveIsAllComplete=false)
  -> hotswap soloFailedRender [line 1080]
  -> renderSoloEndscreen(false)
      "Retry Mission" (default) | "Main Menu"

Retry   -> pdguiEndscreenStartMission  [pdgui_bridge.c:765]
            L1-4: null ncl->player / ncl->config (coop/anti)
            menuhandlerAcceptMission -> mainChangeToStage(same stagenum)
            inputCtxPopDeferred
            (does NOT call manifestClear — SP assets rebuilt by manifestSPTransition)

Main    -> pdguiEndscreenExitToMainMenu  [pdgui_bridge.c:801]
            configSave("pd.ini")
            manifestClear(&g_ClientManifest)   [F-0.4]
            L1-4 null coop ncl
            pdguiSoloMissionReset
            inputCtxPopDeferred
            func0f0f8120() (drain legacy menu stack)
            -> STAGE_CITRAINING
```

### 2.7 Exit — MP Match End → Lobby

```
Server:                                Client:
netServerStageEnd [net.c:~860]         ENet delivers SVC_STAGE_END
  g_NetLocalClient->state = LOBBY        v
  netmsgSvcStageEndWrite                netmsgSvcStageEndRead [netmsg.c:1394]
    -> per-client state=LOBBY,            for room clients: state=DISCONNECTED,
       player/config=NULL                 player/config=NULL
  broadcast SVC_STAGE_END                 g_NetLocalClient->state = LOBBY
  g_NetMatchRoomId = 0xFF                 g_NumReasonsToEndMpMatch = 1
  sessionCatalogTeardown                  mainEndStage()
                                          sessionCatalogTeardown [SA-1]
                                          manifestClear           [L1-1]
                                          g_NetLocalBotAuthority = false
                                          g_NetMatchRoomId NOT reset  <-- GAP-4 / SP-14
                                        v
                                        menutick.c -6 branch
                                          mpPushEndscreenDialog
                                        v (hotswap)
                                        mpGameOverIndRender [pdgui_menu_endscreen.cpp:1094]
                                        renderMpEndscreen(NULL, 0)
                                          every-frame guard:
                                            if !inputCtxIsActive(g_CtxImGuiMenu)
                                              inputCtxPush
                                          5-frame debounce
                                          Rankings / Awards / Stats
                                          "Return to Room" | "Disconnect"
                                          default focus: MISSING (GAP-2)

Return  -> pdguiEndscreenExitToMainMenu -> pdguiSetInRoom(1) -> Room UI
Disc    -> netDisconnect -> CITRAINING
```

### 2.8 Exit — Crash / Disconnect / Kicked

```
ENet DISCONNECT event  -> netClientEvDisconnect [net.c:1321]
  logs netGetDisconnectReason()
  -> netDisconnect()
       wasingame branch:
         mainEndStage
         mpSetPaused(UNPAUSED)
         g_MpSetup.chrslots = 1
         mplayerisrunning / normmplayerisrunning = false
         titleSetNextStage(CITRAINING), setNumPlayers(1)
         manifestClear(&g_ClientManifest)   [Bug A fix]
         mainChangeToStage(CITRAINING)
       -- no endscreen shown, no warning dialog (UX gap, not correctness)
```

---

## 3. Full Menu Inventory (Entry + In-Match + Exit)

Legend: DF=default focus on open · ESC=Esc closes · GB=gamepad B closes · MMB=middle-click closes · X=title-bar close · CTRL=full controller nav.

### 3.1 Entry Path

| Menu | File:Line | DF | ESC | GB | MMB | X | CTRL | Status / Notes |
|------|-----------|----|-----|----|----|---|------|----------------|
| Main menu root | pdgui_menu_mainmenu.cpp:2460 | YES (`s_NeedsFocus`) | YES | YES | YES | NO | YES | SOLID |
| CI Settings redirect | pdgui_menu_mainmenu.cpp (##ci_settings_redirect) | YES | YES | YES | YES | NO | YES | SOLID |
| CI dead P2 | pdgui_menu_mainmenu.cpp (##ci_dead_p2) | YES | YES | YES | YES | NO | YES | SOLID |
| Cinema list | pdgui_menu_mainmenu.cpp (##cinema_list) | YES | YES | YES | YES | NO | YES | SOLID |
| Agent Select | pdgui_menu_agentselect.cpp | YES | YES | YES | YES | NO | YES | SOLID (S267 focus trap fixed) |
| Agent Create | pdgui_menu_agentcreate.cpp | YES (name InputText focused) | YES | YES | YES | NO | PARTIAL | Controller cannot type name (C-15) |
| Multiplayer menu | pdgui_menu_network.cpp:93 | YES (window only, NOT InputText) | YES | YES | YES | NO | PARTIAL | **C-2 DEAD** — IP join InputText: controller cannot type address |
| Social Lobby | pdgui_menu_lobby.cpp | YES | YES | YES | YES | NO | YES | SOLID; "Server Chat (coming soon)" static text |
| Room screen | pdgui_menu_room.cpp:~2180 | YES | NO (room has Leave button, Esc doesn't close) | NO (same) | YES | NO | YES | 4-tab LB/RB OK; Esc-to-leave intentionally absent |
| Room Bot Settings modal | pdgui_menu_room.cpp:~2773 | Auto (modal) | YES (global) | NO explicit | YES | NO | PARTIAL | **C-10** — no B inside modal body |
| Room Save Scenario modal | pdgui_menu_room.cpp | Auto | NO | NO | YES | NO | PARTIAL | **C-11** |
| Room Load Scenario modal | pdgui_menu_room.cpp | Auto | NO | NO | YES | NO | PARTIAL | **C-11** |
| Room bot context popup | pdgui_menu_room.cpp ##bot_ctx | Auto | YES (ImGui popup) | — | YES | NO | PARTIAL | Acceptable |
| MP Setup | pdgui_menu_mpsetup.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| MP Settings | pdgui_menu_mpsettings.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| MP Handicap (hotswap full panel) | pdgui_menu_mpsettings.cpp:406 | **NO** | YES | YES | YES | NO | PARTIAL | **C-6** — missing `SetWindowFocus` |
| Select Tunes (hotswap) | pdgui_menu_mpsettings.cpp:604 | **NO** | YES | YES | YES | NO | PARTIAL | **C-6** |
| Team Names (hotswap) | pdgui_menu_mpsettings.cpp:988 | **NO** | YES | YES | YES | NO | PARTIAL | **C-6** + InputText |
| MP Advanced | pdgui_menu_mpadvanced.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| MP Pre-pause settings | pdgui_menu_mppause.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| Bot Setup | pdgui_menu_botsetup.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| Player Config main | pdgui_menu_playerconfig.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| Player Config load-settings sub | pdgui_menu_playerconfig.cpp:1020 | **NO** | YES | YES | YES | NO | PARTIAL | **C-7** |
| Player Config load-preset sub | pdgui_menu_playerconfig.cpp:1117 | **NO** | YES | YES | YES | NO | PARTIAL | **C-7** |
| Player Config load-player sub | pdgui_menu_playerconfig.cpp:1208 | **NO** | YES | YES | YES | NO | PARTIAL | **C-7** |
| Team Setup (hotswap) | pdgui_menu_teamsetup.cpp:207 | **NO** | YES | YES | YES | NO | PARTIAL | **C-3** |
| Auto-Team (hotswap) | pdgui_menu_teamsetup.cpp | **NO** | YES | YES | YES | NO | PARTIAL | **C-3** |
| Solo Mission Select | pdgui_menu_solomission.cpp:629 | YES (window) + auto-scroll | YES | YES | YES | NO | YES | **G-1**: custom nav cursor, no ImGui SelectDefault (visible highlight only) |
| Difficulty (legacy) | pdgui_menu_solomission.cpp:1268 | YES | YES | YES | YES | NO | YES | Bypassed by two-panel select |
| Coop Anti Difficulty | pdgui_menu_solomission.cpp:1641 | YES | YES | YES | YES | NO | YES | SOLID |
| Coop Anti Options | pdgui_menu_solomission.cpp:1927 | YES | YES | YES | YES | NO | YES | SOLID |
| Mission Briefing | pdgui_menu_solomission.cpp:2215 | YES | YES | YES | YES | NO | YES | SOLID |
| Accept Mission | pdgui_menu_solomission.cpp:~2426 | YES + `s_AcceptSelectIdx=0` | YES | YES | YES | NO | YES | **G-2**: no `SetItemDefaultFocus` on Accept button (visual gap) |
| Cheats hub | pdgui_menu_cheats.cpp | YES | YES | YES | YES | NO | YES | 4-tab LB/RB SOLID |
| Cheats warning | pdgui_menu_cheats.cpp:742 | **NO** | YES | YES | YES | NO | PARTIAL | **C-5** |
| Cheats unlock confirm | pdgui_menu_cheats.cpp:820 | **NO** | YES | YES | YES | NO | PARTIAL | **C-5** |
| Challenges | pdgui_menu_challenges.cpp:141 | **NO** | YES | YES | YES | NO | PARTIAL | **C-1 HIGH** — list-driven, no focus, controller dead-in |
| Control diagram | pdgui_menu_controldiagram.cpp:143 | **NO** | YES | YES | YES | NO | PARTIAL | **C-4** |
| Training | pdgui_menu_training.cpp | YES (inputCtxPush + SetWindowFocus) | YES | YES | YES | NO | YES | SOLID |
| Modding Hub outer/inner | pdgui_menu_moddinghub.cpp:~1226 | **NO** | YES | YES | YES | NO | PARTIAL | **C-8** — 8-tool LB/RB works once inside |
| Mod Manager | pdgui_menu_modmgr.cpp | **NO** (embedded) | YES | YES | YES | NO | PARTIAL | inherits hub focus |
| Mod Manager Validation popup | pdgui_menu_modmgr.cpp:826 | Auto modal | NO explicit | NO | YES | NO | PARTIAL | **C-9** |
| Theme Editor modal | pdgui_menu_theme_editor.cpp | Auto | YES | YES | YES | YES | YES | SOLID |
| Stats Viewer | pdgui_menu_stats.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| Warning (typed) | pdgui_menu_warning.cpp | YES + SetKeyboardFocusHere(-1) on confirm | YES | YES | YES | NO | YES | SOLID |
| MP End-Game dialog | pdgui_menu_warning.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| Filemgr PC | pdgui_menu_warning.cpp | YES | YES | YES | YES | NO | YES | SOLID |
| Update banner | pdgui_menu_update.cpp | NoFocusOnAppearing (toast) | N/A | N/A | N/A | NO | N/A | Correct |
| Download progress | pdgui_menu_update.cpp | NO | NO | NO | YES | NO | POOR | Info-only, auto-closes |
| Update Ready | pdgui_menu_update.cpp | NO | NO | NO | YES | NO | PARTIAL | **C-12** |
| Update Manager | pdgui_menu_update.cpp:580 | NO | NO | NO | YES | YES | PARTIAL | **C-13** — gamepad can't reach X (NavEnableGamepad=off) |
| Log Viewer | pdgui_menu_logviewer.cpp | NO | NO | NO | YES | NO | POOR | Dev tool; **C-14** |

### 3.2 In-Match Menus

| Menu | File:Line | DF | ESC | GB | MMB | CTRL | Context push | Issues |
|------|-----------|----|-----|----|----|----|--------------|--------|
| MP Pause | pdgui_menu_pausemenu.cpp:589 (render), ingame.c:871 (entry) | PARTIAL (tab 0 selected, no D-pad tab nav) | YES | YES | YES | YES | `g_CtxPauseMenu` | **in-match Gap 1**: tabs switchable only by mouse |
| MP End-Game / Game-Over | pdgui_menu_pausemenu.cpp pdguiGameOverRender | PARTIAL | YES | YES | YES | PARTIAL | `g_CtxImGuiMenu` (every-frame guard on IsWindowAppearing) | **in-match Gap 2** verified — context push works, visual polish TBD |
| Solo Pause | pdgui_menu_solomission.cpp:2537 | YES (Resume) | YES | YES | YES | YES | `g_CtxImGuiMenu` (NOT `g_CtxPauseMenu`) | **in-match Gap 8** architectural asymmetry |
| Solo Inventory | pdgui_menu_solomission.cpp:2264 | none | YES | YES | YES | PARTIAL | inherits | **in-match Gap 3** — no per-item D-pad nav |
| Solo Options (in-mission) | pdgui_menu_solomission.cpp:3277 | tab 0 | YES | YES | YES | PARTIAL | inherits | **in-match Gap 4** — tab nav uses PageUp/Q/E, not LB/RB |
| Abort Mission | pdgui_menu_solomission.cpp renderAbortMission | Cancel (safe) | YES | YES | YES | YES | inherits | OK |
| Restart Mission inline confirm | pdgui_menu_solomission.cpp:2768 | Cancel | YES | YES | YES | YES | inherits | OK |
| MP Scorecard overlay | pdgui_menu_pausemenu.cpp:758 | n/a (HUD) | — | — | — | hold ACTION_SCORECARD | no push (NoInputs) | Correct HUD overlay |
| Control Style (solo mid-mission) | pdgui_menu_solomission.cpp (NULL renderFn) | none | broken | broken | — | broken | none | **TRAP** — OG forced, not reachable from current pause UI list; reachable via legacy push only |
| 2P Pause H/V | mainmenu.c OG unregistered | OG | OG | OG | — | OG legacy | none | D5.7 dead zone (single local player constraint makes this unreachable on PC) |
| Chat (in-match) | **NOT IMPLEMENTED** | — | — | — | — | — | — | Lobby stub only; **in-match Gap 5** |
| Team / Role switch (in-match) | **NOT IMPLEMENTED** | — | — | — | — | — | — | **in-match Gap 6**; S263/v36 design exists |
| Debug overlay (F12) | pdgui_backend.cpp:717 | YES | F12 toggle | — | — | Limited | `g_CtxDebugOverlay` priority 20 | Dev |

### 3.3 Exit Path

| Screen | File:Line | DF | ESC/B behavior | Cleanup performed | Issues |
|--------|-----------|----|----|-------------------|--------|
| Solo Completed | soloCompletedRender pdgui_menu_endscreen.cpp:1073 | Next Mission (or Retry if last) | `pdguiEndscreenExitToMainMenu` (configSave, manifestClear, ncl null, drain, pop) | Complete | **GAP-7**: Esc skips Retry/Next — always exits |
| Solo Failed | soloFailedRender pdgui_menu_endscreen.cpp:1080 | Retry Mission | `pdguiEndscreenExitToMainMenu` | Complete | Appropriate for Failed |
| Solo 2P H/V variants | same render path, registered at 1153/1157 | same | same | same | Untested for 2P |
| MP Individual Game Over | mpGameOverIndRender pdgui_menu_endscreen.cpp:1094 | **none explicit** | Return/Disconnect; Enter -> ExitToMainMenu + SetInRoom(1) | manifestClear (via SVC_STAGE_END L1-1), sessionCatalog, ncl null | **GAP-2 / Bug C** body invisible; no `SetItemDefaultFocus` |
| MP Team Game Over | mpGameOverTeamRender:1101 | none | same | same | Same as ind |
| Challenge Completed | mpChallengeCompletedRender:1108 | none | same | same | Same |
| Challenge Cheated | mpChallengeCheatedRender:1115 | none | same | same | Same |
| Challenge Failed | mpChallengeFailedRender:1122 | none | same | same | Same |
| Co-op endscreen | endscreenPushCoop endscreen.c:1774 | per solo template | same | L1-4 ncl clear in ExitToMainMenu | **GAP-5** SP-1 hazard at 1790 |
| Counter-Op endscreen | endscreenPushAnti endscreen.c:1886 | per 2P template | same | same | **GAP-5** SP-1 hazard at 1901 |
| Pause → Game Over state | pdgui_menu_pausemenu.cpp ~1370 | Return to Lobby | ExitToMainMenu + SetInRoom(1) / SoloRoomReturn | manifestClear via ExitToMainMenu | OK |

---

## 4. Transitions — Menus-torn-down → first gameplay frame

| Event | Key call site | Notes |
|-------|---------------|-------|
| Solo mission load | `menuhandlerAcceptMission` -> `mainChangeToStage(stagenum)` then caller `inputCtxPopDeferred(&g_CtxImGuiMenu)` | `on_pop` restores SDL relative mouse |
| Solo Combat Sim load | `matchStart()` matchsetup.c:750 -> `mpStartMatch` -> `menuStop` -> `inputCtxPopDeferred` | Room already closed via `pdguiSoloRoomClose()` |
| Network MP load (client) | `netmsgSvcStageStartRead` netmsg.c:~1328 -> `mpStartMatch` -> `scenarioInitProps` -> `memset g_MatchCountdownState` -> `menuStop` -> `inputCtxPopDeferred` at ~1342 -> send `CLC_STAGE_READY` | Countdown overlay dismissed synchronously |
| Network MP load (server) | ready gate fires after all CLC_READY or timeout -> `mainChangeToStage` + `netServerStageStart` | Server broadcasts SVC_STAGE_START |
| Mouse capture flip | solely via `inputCtxSyncMouseMode()` called at push/pop/endFrame. `g_CtxGameplay` resurrect restores relative mouse. | Constraint clean — no direct SDL calls in menu render paths |
| gameplayInputSuppressed flip | `actionmap.cpp` — predicate: top context != `g_CtxGameplay`, OR window focus lost, OR 50 ms focus-regain settle window. Equivalent to `pdguiIsActive()` at dispatch site. | Phase 1 complete (S250); Phase 2 (menu pool) queued |

---

## 5. Gap List (consolidated, prioritized)

### HIGH

**GAP-1** — SP-13 hazard: Deep Sea co-op → next-mission path skips manifestClear
- Location: `src/game/menutick.c:~598` in the `g_Vars.stagenum == STAGE_DEEPSEA` branch
- Risk: online co-op advancing past Deep Sea is AV-prone under the same pattern that caused Bug A
- Proposed fix (do not implement): insert `manifestClear(&g_ClientManifest);` immediately before `mainChangeToStage(g_MissionConfig.stagenum)` in that branch. Mirror existing F-0.4 / netmsg.c:1419 / netDisconnect patterns.

**C-1** — Challenges menu has no default focus on open
- Location: `pdgui_menu_challenges.cpp:141` (after `Begin()`) and `:203` (Selectable loop)
- Symptom: controller-only user opens Challenges, sees nothing selected, nav presses do nothing until focus is established
- Proposed fix: on `IsWindowAppearing()` call `SetWindowFocus()`; introduce `s_FocusPending` flag that triggers `SetItemDefaultFocus()` on the auto-selected Selectable on the next render.

**GAP-6** — MP endscreen body invisible (Bug C, unresolved)
- Location: `port/fast3d/pdgui_menu_endscreen.cpp:755,829` (`renderMpEndscreen`)
- Status: partial mitigation via every-frame context push and `contentH` clamp. Six hypotheses in `scratch/archive/2026-04-13/session-state-endgame-crash.md` §4c.
- Proposed next step: instrument each early-return with `sysLogPrintf(LOG_WARNING, "ENDSCREEN: Begin=false sf=%.3f w=%.1f h=%.1f", sf, menuW, menuH)` before the early return at line ~756, plus instrumentation on the `contentH` clamp at line ~829. Do NOT attempt another structural change until log evidence distinguishes the six hypotheses.

### MEDIUM — Missing SetWindowFocus-on-appear (identical fix pattern)

Add `if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();` after the relevant `Begin()`/`BeginChild()` block:

- **C-3** `pdgui_menu_teamsetup.cpp:207` (`##team_setup`, `##auto_team`)
- **C-4** `pdgui_menu_controldiagram.cpp:143` (`beginControlDiagramWindow`)
- **C-5** `pdgui_menu_cheats.cpp:742` (`##cheats_warning`), `:820` (`##cheats_unlock_confirm`)
- **C-6** `pdgui_menu_mpsettings.cpp:406` (handicap), `:604` (Select Tunes), `:988` (Team Names)
- **C-7** `pdgui_menu_playerconfig.cpp:1020, :1117, :1208` (load-settings / load-preset / load-player)
- **C-8** `pdgui_menu_moddinghub.cpp:~1226` (inner child after `BeginChild("##modhub_inner")`)

### MEDIUM — Asymmetric exit / state reset

**GAP-3** — Pause → End Game (online client) bypasses endscreen entirely
- Location: `pdgui_menu_pausemenu.cpp:656-661`
- Current behavior: `netDisconnect` tears down + returns to CITRAINING without rankings
- Option A: add a comment declaring this asymmetry intentional
- Option B: show abbreviated endscreen for 2 s then disconnect — symmetric with natural end-of-match

**GAP-4** — Client-side `g_NetMatchRoomId` not reset after match end (SP-14 follow-up)
- Location: `port/src/net/netmsg.c:1394` in `netmsgSvcStageEndRead`
- Fix: set `g_NetMatchRoomId = 0xFF` after per-mode cleanup, matching server reset at `net.c:876`

**GAP-10** — SVC_MATCH_CANCELLED doesn't call `pdguiCountdownReset`
- Location: `port/src/net/netmsg.c:~5468` in `netmsgSvcMatchCancelledRead`
- Fix: add `pdguiCountdownReset()` call; matches the B-139 pattern on lobby mode→NONE transition.

**in-match Gap 8** — Solo pause does not push `g_CtxPauseMenu`
- Solo pause uses `g_CtxImGuiMenu` via legacy push path; `g_ImcPauseMenu` (priority 11) never activates for solo; `s_GamePaused` never set.
- Fix: have `renderPauseMenu` push `g_CtxPauseMenu` on `IsWindowAppearing()`, pop on close. Architectural cleanup, Phase 2 scope.

### MEDIUM — Controller parity gaps

**in-match Gap 1** — MP pause tab buttons not D-pad navigable
- Location: `pdgui_menu_pausemenu.cpp:628-668`
- Fix: add `ImGuiKey_GamepadDpadLeft/Right` (or LB/RB per D5 spec) checks to decrement/increment `s_PauseTab` with wrapping before the tab render loop.

**in-match Gap 3** — Solo Inventory has no D-pad navigation
- Location: `pdgui_menu_solomission.cpp:2264`
- Fix: add `s_InvSelectIdx`, manual D-pad up/down with wrap, draw selection highlight.

**in-match Gap 4** — Solo Options tabs switch on PageUp/Q/E, not LB/RB
- Location: `pdgui_menu_solomission.cpp:3315`
- Fix: add `ImGuiKey_GamepadL1` / `ImGuiKey_GamepadR1` checks alongside existing PageUp/Down.

**C-2** — Controller-only user cannot enter join-by-IP address
- Location: `pdgui_menu_network.cpp:237`
- Fix options: on-screen keyboard (systemic fix C-15), or document as MKB-only and rely on server browser / recent-servers list.

**Controls sub-tab gap** — inner Controls tab bar (KB+Mouse vs Controller) cannot be cycled with gamepad
- Location: `pdgui_menu_mainmenu.cpp:1091` `pdguiUpdateRenderSettingsTab`
- Outer 7-tab Settings bar consumes LB/RB before the inner 2-tab bar can see it. Controller-only user cannot switch between binding tabs.
- Fix: add a nested pending-tab handler, or use a distinct action (e.g., triggers L2/R2) for the inner tab.

### LOW — Polish / convenience

**G-1** — Mission Select has no ImGui nav focus on list rows (custom highlight only). `pdgui_menu_solomission.cpp:719`. Fix: `SetScrollHereY` + initial `SetKeyboardFocusHere(-1)` after inserting selected row.

**G-2** — Accept Mission button has no `SetItemDefaultFocus`. `pdgui_menu_solomission.cpp:~2499`. Add `if (s_AcceptSelectIdx == 0) ImGui::SetItemDefaultFocus();`.

**G-6** — Network menu address field not auto-focused. `pdgui_menu_network.cpp:120-132`. Add `SetKeyboardFocusHere(0)` before `InputText("##address")` on IsWindowAppearing.

**GAP-5** — SP-1 hazard at `endscreenPushCoop`/`Anti`. `src/game/endscreen.c:1790, 1901`. Add `AVOID_UB` guard or assertion on `g_MpPlayerNum < MAX_LOCAL_PLAYERS` before `g_Menus[g_MpPlayerNum]` writes.

**GAP-7** — Esc on solo completed screen exits to main menu. `pdgui_menu_endscreen.cpp:691-695`. Consider mapping Esc to Retry and requiring Start for Main Menu, matching N64 convention.

**GAP-8** — Legacy `MENUROOT_ENDSCREEN` branch exits to STAGE_TITLE, not CITRAINING. `src/game/menutick.c:671-676`. Audit reachability; if dead, `assert(0)`; if live, change to CITRAINING.

**C-9** — Mod Manager Validation popup has no B/Esc inside modal body. `pdgui_menu_modmgr.cpp:854` (before `EndPopup`). Add `if (IsKeyPressed(Esc) || IsKeyPressed(GamepadFaceRight)) CloseCurrentPopup();`.

**C-10** — Room Bot Settings modal lacks B/Esc handler inside. `pdgui_menu_room.cpp:~2773`. Same fix pattern.

**C-11** — Save/Load Scenario modals: same pattern, same fix.

**C-12** — Update Ready dialog: add B/Esc → dismiss.

**C-13** — Update Manager window: add B/Esc → close (gamepad can't reach title-bar X with NavEnableGamepad=off).

**C-14** — Log Viewer: dev tool, low priority; could add B/Esc for consistency.

**C-15** — Systemic: text-entry menus (Agent Create name, join IP, team names, room name, mod/skin save names) require a keyboard. No on-screen keyboard exists. Separate design initiative.

**Middle-click bridge — Skin Editor canvas pan** — middle-drag canvas pan conflicts with global middle-click → Esc mapping. `pdgui_backend.cpp:337-343`. Gate: `if (!ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) inject Escape`.

**in-match Gap 5** — No in-match chat implementation. Lobby stub only. QoL gap for MP.

**in-match Gap 6** — No in-match team / role switch UI. S263/v36 design exists but not implemented.

**Gap 7 (in-match audit)** — OG traps in Control Style and 2P pause/options menus. Largely dead code under the "single local player" constraint, but `g_SoloMissionControlStyleMenuDialog` is still registered with NULL renderFn; reachability via legacy push should be audited or the dialog removed.

---

## 6. Constraint Compliance Check

| Constraint | Status | Evidence |
|-----------|--------|----------|
| "No menu may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly" | CLEAN | No direct calls in any `pdgui_menu_*.cpp` render function. `pdgui_backend.cpp:512` is the B-92 deferred-flush path (not a render); `gfx_sdl2.cpp:261-263` guarded by `pdguiIsActive()`; vendored `imgui_impl_sdl2.cpp` is the platform backend, not our code. |
| "ImGui is the sole menu system" | OK with caveat | All rendering is ImGui. Legacy `menuPush`/`menuPop` stack is retained as plumbing — ImGui dialogs still push `menudialogdef` entries. A few OG-forced dialogs (`g_SoloMissionControlStyleMenuDialog`, 2P pause H/V) remain with NULL renderFn and fall through to the generic DEFAULT handler. |
| "All asset references use catalog ID strings" | OK | `g_MissionConfig.stage_id` catalog-first pattern verified in entry and exit paths. `matchStart()` resolves catalog → stagenum at final handoff. |
| "manifestClear before mainChangeToStage during MP teardown" | MOSTLY OK | Present at `pdgui_bridge.c:~799`, `netmsg.c:~1419`, `netDisconnect` (Bug A fix). **GAP-1** — `menutick.c:~598` Deep Sea co-op path missing. |
| "Ready gate lifetime bound to its room" | OK | `roomLeave()` hooks `netReadyGateOnClientLeft` / `netReadyGateAbortForRoom` (S254). Countdown overlay dismissed in `netmsgSvcStageStartRead`. |
| "Room-settings mutations broadcast via end-of-frame dirty flag" | OK | `s_RoomSettingsDirty` / `s_PlaylistDirty` pattern present in `pdgui_menu_room.cpp` and `pdgui_menu_mpsettings.cpp`. |
| "S276 universal middle-click → Esc bridge" | OK (with one edge case) | Implemented in `pdguiDriveImGuiNav()` `pdgui_backend.cpp:337-343`. Edge case: Skin Editor middle-drag canvas pan conflicts. |
| "v36 Counter-Op anti-role explicit on wire" | OK | Verified via CLC_LOBBY_START / SVC_STAGE_START fields per audit. |

---

## 7. Recommended Remediation Order (no implementation)

1. **Instrument Bug C first** — do not modify endscreen render logic until the six hypotheses are narrowed by log evidence.
2. **GAP-1** — single-line fix with high crash-prevention value. Mirror the well-established SP-13 pattern.
3. **C-1** — restores list-driven menu usability for controller. Small and local.
4. **"Sweep missing SetWindowFocus"** — C-3, C-4, C-5, C-6, C-7, C-8 share one fix pattern. Bundle as a single PR.
5. **In-match audit Gaps 1/3/4** — small D-pad / LB-RB additions. Bundle with the sweep.
6. **GAP-4 + GAP-10** — client-side `g_NetMatchRoomId` reset + `pdguiCountdownReset` on cancel. Defensive hygiene.
7. **GAP-3** — decide whether Pause→End Game online should show an abbreviated endscreen or stay asymmetric. Design call.
8. **In-match Gap 8** — Solo pause architectural alignment (`g_CtxPauseMenu`). Phase 2 alongside the already-queued menu-pool work.
9. **Modal B/Esc polish** (C-9, C-10, C-11, C-12, C-13). Low-risk.
10. **Systemic text-entry / on-screen keyboard** (C-15). Separate design initiative. Defer until v0.2+.

---

## 8. Still-Open Items (pre-existing, flagged by this investigation)

- **Bug C** MP endscreen invisible body — unresolved, instrumentation pending.
- **Bug D** invisible networked bots on Chicago — may have cleared in S253 drop; fresh repro needed.
- **Airbase no-response on Start Match** — manifest OK, no SVC_STAGE_START in logs; needs post-drop repro.
- **Input Authority Phase 2** (menu pool) — queued, ADR at `designs/input-authority-and-menu-pool-2026-04-13.md`.
- **FIX-B.1** deep manifest scanner for cinematics + AI scripts.
- **Manifest gap follow-ups** — title/menu stage has no manifest (Skin Editor mod chars silently missing); SP pre-scan timing; cutscene cinema models never in manifest.
- **Counter-Op / Team role-switch in-match UI** — S263/v36 design exists, not implemented.
- **In-match chat** — not implemented.

---

## 9. Agent Attribution

This report synthesizes four parallel agent investigations:

- Entry path (main menu → first gameplay frame), agent `a2c94c92f455e48f8`
- In-match menus (pause / options / scoreboard / chat), agent `a4168b612bcb85f6c`
- Exit path (endscreen → main menu, cleanup), agent `a4c0eab8bd0566ebe`
- Cross-cutting input + controller + defaults audit, agent `ab776a262012655cb`

Line numbers are snapshots from the 2026-04-16 worktree; verify before touching a site. No code modifications were made during this investigation.
