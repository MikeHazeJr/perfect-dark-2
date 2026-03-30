# Menu System & Asset Loading Audit

> Deep audit of menu architecture, accessibility, controller support, and asset loading pipeline.
> Scope: every ImGui menu file, menumgr, hotswap system, assetcatalog, and the catalog loading plan.
> Back to [index](README.md)

---

## SUMMARY (60 lines)

### Critical Findings

**[1] Lobby Disconnect is a one-button accident waiting to happen.**
In `pdgui_menu_lobby.cpp:375`, both `Escape` and `GamepadFaceRight` (B button) call `netDisconnect()` directly with no confirmation. The B button is the universal "back" input in every other menu. Users who press B to navigate back will silently disconnect from the server.

**[2] Asset loading still bypasses the catalog entirely.**
The catalog (assetcatalog.c) is a rich registry with 23 asset types, load-state tracking, and mod resolution — but C-4 through C-7 (the four gateway intercepts: fileLoadToNew, texLoad, animLoadFrame, sndStart) are ALL PENDING. Every actual ROM load ignores the catalog. The catalog is decoration, not infrastructure, for now.

**[3] Hotswap covers ~8 dialogs out of 240. 232 remain unregistered.**
The hotswap system has 8 ImGui dialog registrations + 14 forced-native locks. The type-based fallback covers DANGER and SUCCESS types generically. But 218+ dialogs have no registration at all and always render via PD native. The replacement plan has not advanced past session 48.

**[4] Warning dialog can't handle complex menu items.**
`pdgui_menu_warning.cpp` renders MENUITEMTYPE_LIST and MENUITEMTYPE_DROPDOWN as `[label]` placeholder text. Any danger/success dialog with dropdown or list items is partially broken under hotswap.

**[5] No loading screen or async loading exists.**
Stage transitions are fully synchronous and blocking. No progress indicator. No incremental diff load (C-9 is pending C-4 through C-7). Loading a heavy stage freezes the process until done.

**[6] Controller navigation works well but has a head selector gap.**
Agent Create's body selector has explicit D-pad + L1/R1 bindings. The head selector has only ArrowButton widgets — controller-click only via ImGui focus, no D-pad shortcut. Minor but inconsistent.

### Positive Findings
- menumgr cooldown is time-based (100ms via SDL_GetTicks) — correct at all framerates.
- The two-phase hotswap render (GBI queue → ImGui draw) is architecturally sound.
- Agent Select has full controller parity: D-pad nav, A/B/X/Y/D/RB all bound.
- Pause menu correctly uses menuPush/Pop for cooldown integration.
- The type-based fallback system is elegant — covers dozens of dialogs automatically.
- Handel Gothic font embedded in binary, scales with display height automatically.
- Asset catalog infrastructure (hash table, load states, ref counts, CRC32) is complete and ready for the intercepts.

### Next High-Value Actions
1. Add disconnect confirmation in lobby (B/Escape → confirm dialog, not immediate call)
2. Add D-pad binding to head selector in Agent Create (copy the body selector pattern)
3. Fix warning dialog: skip rendering MENUITEMTYPE_LIST/DROPDOWN items if they have no ImGui equivalent yet (don't show broken `[label]` placeholder)
4. Implement C-4 (fileLoadToNew catalog intercept) — unlocks all 16 file-load callers
5. Add a simple "Loading..." overlay for stage transitions

---

## 1. Menu Stack (menumgr)

**File**: `port/src/menumgr.c` + `port/include/menumgr.h`

### Architecture
- Stack-based, max depth 8 (`MENU_STACK_MAX`).
- States: NONE, MAIN, PAUSE, LOBBY, JOIN, SETTINGS, BOT_CONFIG, ENDGAME, PROFILE, MODDING, MATCHSETUP (11 values).
- Cooldown: 100ms time-based via `SDL_GetTicks()` — correct at all framerates.
- `menuMgrTick()` is a **no-op** (cooldown is time-based). The function exists for API compatibility only.
- Dedup: `menuPush` silently succeeds without pushing if the same menu is already on top.
- `menuPopAll()` exists; clears all and triggers cooldown.

### Gaps
- **No queuing mechanism.** If two systems try to `menuPush()` simultaneously in the same frame, the second push wins (if different state) or is silently ignored (if same). There is no "pending menu" queue.
- **No notification on pop.** Callers that need cleanup on close must manage their own state (e.g., `pdguiPauseMenuOpen/Close` manually calls `mpSetPaused`). There is no pop callback.
- **menuGetCurrent() vs. stack truth.** The entire system is read-based: callers poll `menuGetCurrent()`. No observer pattern. If a subsystem needs to know "I was popped," it has to detect the state change itself.
- **BOT_CONFIG, ENDGAME, PROFILE are declared but unused.** No ImGui renderer or game code currently pushes these states.

### ESC/Back at Each Level
| Menu State | ESC/B Button Behavior |
|------------|----------------------|
| MENU_NONE | Not consumed by menumgr; game receives input normally |
| MENU_PAUSE | B = resume (pdguiPauseMenuClose), Start = resume |
| MENU_LOBBY | **B = netDisconnect() — no confirmation** |
| MENU_JOIN | B = menuPopDialog() |
| MENU_MAIN | No explicit ESC handler found in main menu renderer (F11 storyboard has its own) |
| MENU_MODDING | Not connected to menumgr yet |

---

## 2. Hot-Swap System (pdgui_hotswap)

**File**: `port/fast3d/pdgui_hotswap.cpp`

### Architecture
Two-phase render required by the GBI + ImGui ordering constraint:
1. **GBI phase**: `menuRenderDialog()` → `pdguiHotswapCheck()` → queue dialog, return 1 (suppress native)
2. **ImGui phase**: `pdguiRender()` → `pdguiHotswapRenderQueued()` → call registered renderFn

Per-entry override system: 0=follow global toggle, 1=force NEW, -1=force OLD.

Type-based fallback: `pdguiHotswapRegisterType(dialogType, fn)` covers all dialogs of a given type automatically, without individual registration. Max 8 types (currently type 2=DANGER and type 3=SUCCESS are covered).

### Registry State (as of S61)
| Registration | Dialog/Type | Renderer |
|-------------|-------------|----------|
| Hotswap | g_FilemgrFileSelectMenuDialog | Agent Select |
| Hotswap | g_CiMenuViaPcMenuDialog | Main Menu |
| Hotswap | g_CiMenuViaPauseMenuDialog | Main Menu (pause variant) |
| Hotswap | g_FilemgrEnterNameMenuDialog | Agent Create |
| Hotswap | g_NetMenuDialog | Multiplayer / Network |
| Hotswap | g_MatchSetupMenuDialog | Match Setup (local) |
| Type fallback | MENUDIALOGTYPE_DANGER (2) | Warning/Danger renderer |
| Type fallback | MENUDIALOGTYPE_SUCCESS (3) | Success renderer |
| Forced PD native | g_NetJoinAddressDialog | NULL (keyboard input) |
| Forced PD native | g_NetJoiningDialog | NULL (status monitoring) |
| Forced PD native | g_NetCoopHostMenuDialog | NULL |
| Forced PD native | g_FilemgrRenameMenuDialog | NULL (keyboard input) |
| Forced PD native | g_FilemgrDuplicateNameMenuDialog | NULL |
| Forced PD native | g_FilemgrFileSavedMenuDialog | NULL |
| Forced PD native | g_MpPlayerNameMenuDialog | NULL (keyboard input) |
| Forced PD native | g_MpSaveSetupNameMenuDialog | NULL (keyboard input) |
| Forced PD native | g_MpEndscreenConfirmNameMenuDialog | NULL (keyboard input) |
| Forced PD native | g_MpChangeTeamNameMenuDialog | NULL (keyboard input) |
| Forced PD native | g_MpReadyMenuDialog | NULL |
| Forced PD native | g_StatusOkDialog | NULL |
| Forced PD native | g_StatusErrorDialog | NULL |
| Forced PD native | g_DeleteSetupDialog | NULL |

**Total registered:** 22 entries. `pdguiHotswapTotalMenuCount()` returns 113 (hardcoded storyboard count). ~91 unregistered.

### F8 Toggle Behavior
- F8 = `pdguiHotswapToggle()` → flips `s_HotswapActive` global.
- RS-click (right stick click on controller) also triggers toggle.
- Per-entry overrides take precedence; global toggle only affects entries with `override == 0`.
- If you're in a legacy PD menu and press F8: the dialog will render as ImGui on the next frame (if registered). Transition is clean — no mid-frame state.
- If in ImGui menu and press F8: next frame falls through to PD native render.
- `s_HotswapMenuWasActive` persists from render phase through next frame's input phase to bridge the gap.

### Known Limitations
- `HOTSWAP_MAX_QUEUED = 8`: at most 8 dialogs can be hot-swapped per frame. The legacy system may render more in one frame (e.g., "other" + "curdialog"). Deduplication is implemented but the hard limit could still be hit if many dialogs are active.
- `HOTSWAP_MAX_ENTRIES = 128`: sufficient for current usage (22/128 slots used).
- **Type pool is a fixed static array.** `s_TypeEntryPool[HOTSWAP_MAX_QUEUED]` cycles with modulo. If more than 8 type-based dialogs are queued in one frame, entries overwrite each other. In practice one danger dialog at a time is fine.

---

## 3. ImGui Backend (pdgui_backend)

**File**: `port/fast3d/pdgui_backend.cpp`

### Input Routing
Events are processed in `pdguiProcessEvent()`:
1. Cooldown check: ALL key/button events consumed during 100ms cooldown (forwarded to ImGui for state tracking only).
2. F8 → hotswap toggle (consumed).
3. F11 → storyboard toggle (consumed).
4. F12 → debug overlay toggle (consumed).
5. `ImGui_ImplSDL2_ProcessEvent()` always called (ImGui tracks state).
6. Consumption decision based on: `overlayActive || hotswapActive || pauseActive`.
   - Mouse events: consume if ImGui `WantCaptureMouse`.
   - Keyboard events: consume ALL if any of the three conditions.
   - Gamepad events: **ALWAYS consumed** when any ImGui surface is active.

**Critical note on gamepad consumption (line 555-558):** When ANY ImGui surface is active (hotswap, overlay, or pause), all `SDL_CONTROLLERBUTTONDOWN/UP`, `SDL_CONTROLLERAXISMOTION`, `SDL_JOYBUTTONDOWN/UP`, `SDL_JOYHATMOTION` are consumed by ImGui. The game never sees gamepad input while a menu is open. This is correct behavior.

### Mouse Management
On overlay open: saves SDL relative mode + cursor visibility, releases grab, warps cursor to window center. On overlay close: restores saved state. This allows seamless transition from mouselook gameplay to cursor-based menu and back.

### Font Scaling
- Handel Gothic Regular, 24pt atlas base.
- `FontGlobalScale` set to `pdguiScaleFactor()` every frame (proportional to display height, baseline 720p = 1.0).
- At 800×600: scale ≈ 0.83 (≈20pt effective). At 1440p: scale ≈ 2.0 (≈48pt effective).
- This is automatic — no user font scale slider. If a user wants larger text, they increase resolution.
- `TexGlyphPadding = 2` prevents descender clipping.

### NavFlags
```cpp
io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
```
Both keyboard and gamepad ImGui navigation are enabled globally.

---

## 4. Individual Menu Screens

### 4.1 Main Menu (`pdgui_menu_mainmenu.cpp`)

**Replaces**: `g_CiMenuViaPcMenuDialog`, `g_CiMenuViaPauseMenuDialog`
**Entered via**: PD native menu push (triggered from title screen or pause) → hotswap picks it up
**Exited via**: Navigating away (selecting game mode pushes a new dialog or transitions)

**Layout — Play Tab:**
| Button | Action |
|--------|--------|
| Solo Missions | Calls `menuhandlerMainMenuSoloMissions(MENUOP_SET, ...)` |
| Combat Simulator | Calls `menuhandlerMainMenuCombatSimulator` → opens match setup |
| Co-Op | Calls `menuhandlerMainMenuCooperative` |
| Counter-Op | Calls `menuhandlerMainMenuCounterOperative`; disabled if <2 controllers |
| Network Game | Opens network menu dialog → `menuPushDialog(&g_NetMenuDialog)` |
| Change Agent | Calls `menuhandlerChangeAgent(MENUOP_SET, ...)` |
| Modding Hub | `pdguiModdingHubShow()` |

**Layout — Settings Tab (6 sub-tabs):**
- **Video**: Fullscreen, fullscreen mode, resolution picker, center/maximize window, VSync, FPS limit, MSAA, texture filter (3D + 2D), detail textures, FPS counter display.
- **Audio**: 4-layer volume sliders (Master, Music, Gameplay, UI). Legacy music volume passthrough.
- **Controls**: Player 0 key bindings for all control keys. Tab-separated columns. Capture mode (press key to rebind). Controller name display. Separate n64-style reset.
- **Game**: Tick rate div, muzzle flash toggle, music-on-death toggle, HUD center toggle, screen shake, skip intro, FOV + FOV zoom, mouse enable, mouse speed XY, mouse lock mode, look inversion, crouch mode, etc.
- **Updates**: Inline version picker (`pdguiUpdateRenderSettingsTab()`). Auto-triggers check on first view.
- **Debug**: mempool diagnostics, mempPCValidate call, allocation counts.

**Input**: ImGui keyboard/gamepad nav. No explicit D-pad shortcut code — relies on `NavEnableGamepad`.

**State read/write**: Reads/writes video, audio, and input config via port API functions. Reads `g_PlayerExtCfg[]`. Calls `configSave("pd.ini")` on some changes.

**Known issues / incomplete features**:
- Counter-Op button is disabled if `joyGetConnectedControllers() < 2`. On PC this is always true unless running two controllers. This makes Co-Op/Counter-Op always appear grayed out for most players.
- Match config init (`matchConfigInit()`) is called when opening Combat Sim. This is only for local play — network match setup is in the Room interior.

---

### 4.2 Network / Multiplayer Menu (`pdgui_menu_network.cpp`)

**Replaces**: `g_NetMenuDialog`
**Entered via**: Main Menu → "Network Game" → `menuPushDialog(&g_NetMenuDialog)`
**Exited via**: "Back" button or B/Escape → `menuPopDialog()`

**Shows**:
- Server browser (list of recent servers with online status, Lobby/In Game, player counts)
- Direct connect field: accepts IP:port or connect code
- "Refresh" button calls `netQueryRecentServers()`
- On connect: decodes connect code → `netStartClient(addr)` → pushes `g_NetJoiningDialog` (native)

**Input**: Full keyboard (Tab to move between fields, Enter to connect). Controller: GamepadFaceRight/Escape = Back. Text field is InputText widget — gamepad text entry not possible without OS keyboard popup.

**State read/write**: Reads `g_NetLastJoinAddr` on first appear. Writes `g_NetJoinAddr` on connect attempt.

**Known issues**:
- Connect code decode builds IP address with byte order issue: `%u.%u.%u.%u` with `ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF` — octets are reversed from network byte order. This is likely intentional if `connectCodeDecode` returns host byte order, but should be verified.
- No server browser in the true sense (no master server) — only shows "recent servers" from a local cache. The "Refresh" button re-queries the cached list.
- Raw IP addresses are not accepted (correct by design — all joins require connect codes).

---

### 4.3 Social Lobby (`pdgui_menu_lobby.cpp`)

**Not hotswap.** Rendered as overlay from `pdguiLobbyRender()` → `pdguiLobbyScreenRender()` when client is in `CLSTATE_LOBBY` and `!s_InRoom`.

**Shows**:
- Left panel: connected players (name, character, status badge)
- Right panel: active rooms (name, state color, player count, disabled Join button)
- "Create Room" button → `pdguiSetInRoom(1)` transitions to Room interior
- Footer: server chat stub ("coming soon") + Disconnect button

**Input**: Disconnect button is triggered by `ImGui::Button("Disconnect")` OR `ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)` OR `ImGui::IsKeyPressed(ImGuiKey_Escape)`. **This means B button = immediate disconnect with no confirmation. Critical UX bug.**

**Join buttons**: All disabled pending R-3 (SVC_ROOM_JOIN protocol). Tooltip says "Coming soon (R-3)".

**Connect code display**: Only shown to `NETMODE_SERVER` users. Static code generated once; "Copy" button calls `SDL_SetClipboardText`.

**State read**: `lobbyGetPlayerCount()`, `lobbyGetPlayerInfo()`, `roomGetByIndex()`, `roomStateName()`.

**Known bug**: Disconnect on B is dangerous — see Critical Finding [1].

---

### 4.4 Room Interior (`pdgui_menu_room.cpp`)

**Not hotswap.** Rendered from `pdguiRoomScreenRender()` when `s_InRoom == true`.

**Shows**:
- Title bar with tab selector: "Combat Sim" | "Campaign" | "Counter-Op"
- Left panel: tab-specific match settings (editable by leader; read-only for followers)
- Right panel: room player list (name, leader crown, character, state)
- Bottom bar: "Start Match" (leader only) + "Leave Room"

**Combat Sim tab**:
- Arena picker (17 hardcoded arenas by stagenum — NOT from asset catalog)
- Scenario picker (6 scenarios)
- Score / time limit sliders
- Options toggles (bitmask)
- Bot list with "+ Add Bot" button and per-bot modal (body, difficulty)
- Weapon set picker (reads from mpGetWeaponSet/SetWeaponSet)
- Spawn weapon dropdown

**Campaign tab**:
- Mission picker (17 hardcoded missions by stagenum)
- Difficulty picker (Agent / Special Agent / Perfect Agent)
- Friendly fire toggle

**Counter-Op tab**:
- Mission picker
- Difficulty picker
- "Counter-Op Player" selector (picks who plays the counter role)

**Start Match**:
- Combat Sim: `netLobbyRequestStartWithSims(GAMEMODE_MP, stagenum, 0, numBots, simType)`
- Campaign: `netLobbyRequestStart(GAMEMODE_COOP, stagenum, difficulty)`
- Counter-Op: `netLobbyRequestStart(GAMEMODE_ANTI, stagenum, difficulty)`

**Leave Room**: `pdguiSetInRoom(0)` → returns to social lobby.

**Controller**: `ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)` bound to Leave Room in footer. ImGui nav handles tab switching if focus is on tab buttons.

**State sync**: Only leader can edit. Non-leader sees read-only settings. Full settings sync deferred to CLC_ROOM_SETTINGS (R-4, not yet implemented). Connect code display for server host.

**Known incomplete**: Arena list hardcoded in s_Arenas[] — not pulled from asset catalog or stageTableGetEntry(). If new arenas are added via mods they won't appear here until s_Arenas is updated.

---

### 4.5 Match Setup — Local (`pdgui_menu_matchsetup.cpp`)

**Replaces**: `g_MatchSetupMenuDialog`
**Entered via**: Main Menu → "Combat Simulator" → pushes dialog → hotswap picks it up
**Exited via**: "Cancel" → `menuPopDialog()`, or "Start" → `matchStart()` → game begins

**Shows**: Full local match configuration — same settings as Room interior Combat Sim tab but for offline play. Character slots (up to 32), weapon set, bot config via `assetcatalog` + `botvariant`, arena from the catalog-backed `modmgrGetArena()`.

**Notable**: This is the only ImGui screen that calls `modmgrGetArena()` / `catalogGetNumBodies()` etc. — it's partially catalog-aware for the arena picker. However, the bot body/head selection uses `catalogGetNumBodies/Heads()` which is the legacy model catalog, not the asset catalog.

**State**: Reads/writes `g_MatchConfig` directly. Calls `matchConfigAddBot()` / `matchConfigRemoveSlot()` / `matchStart()`.

---

### 4.6 Pause Menu + Scorecard (`pdgui_menu_pausemenu.cpp`)

**Not hotswap.** Rendered independently via `pdguiPauseMenuRender()` + `pdguiScorecardRender()`.

**Pause Menu**:
- Entered via `pdguiPauseMenuOpen()` — called from legacy `bondmove` → `mpPushPauseDialog` path.
- Registers with menumgr via `menuPush(MENU_PAUSE)`.
- 3 tabs: Rankings (scorecard), Settings (read-only match info), End Game.
- End Game has confirm step (half-width Confirm/Cancel buttons).
- Resume: "Resume" button, or Start/Escape, or B button.
- B button context-sensitive: in End Game confirm → cancel; else → close pause.
- Red palette (`pdguiSetPalette(2)`) during pause.
- Single-player: calls `mpSetPaused(MPPAUSEMODE_PAUSED/UNPAUSED)`.
- Network: End Game → `netDisconnect()`.
- **B-14 fix**: `s_PauseJustOpened` flag prevents open+close in one tick.
- **B-16 fix**: B button correctly navigates back.

**Scorecard Overlay**:
- Tab (keyboard) or GamepadBack held = show.
- Polled via `SDL_GetKeyboardState()` + `ImGui::IsKeyDown(ImGuiKey_GamepadBack)`.
- `NoInputs` flag — purely visual, pass-through, doesn't steal input.
- Only shown during active gameplay (`pdguiPauseGetNormMplayerIsRunning()`).
- Hidden during GAMEOVER mode and when pause menu is open.

---

### 4.7 Agent Select (`pdgui_menu_agentselect.cpp`)

**Replaces**: `g_FilemgrFileSelectMenuDialog`

**Controller navigation**:
| Input | Action |
|-------|--------|
| D-pad Up/Down | Navigate list |
| Arrow Up/Down | Navigate list (MKB) |
| A / Enter | Load selected agent |
| X / C | Copy with confirmation |
| Y / Delete | Delete with confirmation |
| D / RB (R1) | Toggle as default agent |
| A / Enter | Confirm inline prompt |
| B / Escape | Cancel inline prompt |

**Features**: Character preview FBO thumbnail for selected slot. Inline confirmation prompts (no nested ImGui::Begin). Auto-load default agent on first appear. Default saved to `pd.ini` via `configSave`.

**State**: Reads `g_FileLists[0]` for save files. Writes `g_GameFileGuid`. Calls `filemgrSaveOrLoad()`, `filemgrDeleteCurrentFile()`, `filemgrPushSelectLocationDialog()`.

---

### 4.8 Agent Create (`pdgui_menu_agentcreate.cpp`)

**Replaces**: `g_FilemgrEnterNameMenuDialog`
**Entered via**: Agent Select → "+ New Agent..." → `menuPushDialog(&g_FilemgrEnterNameMenuDialog)`
**Exited via**: "Create" (saves + pops) or "Cancel" / B/Escape (pops without saving)

**Controller navigation**:
| Input | Action |
|-------|--------|
| D-pad Left/L1 | Previous body |
| D-pad Right/R1 | Next body |
| Arrow buttons on head | ImGui click only — **no D-pad binding** |
| B / Escape | Cancel |
| A (via ImGui nav) | Click focused button |

**Bug**: Head selector uses `ImGui::ArrowButton("##head_prev")` without explicit D-pad handling. The body selector has `ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft/Right, false)` explicitly. This inconsistency means head selection requires mouse or tabbing to the arrow buttons via keyboard nav.

**Features**: Name input auto-focused on appear. Body/head auto-sync (body change resets head to matched default). Live 3D portrait preview. "Auto" reset button if head was manually overridden. Create button disabled until name non-empty. Enter key from name field also triggers create.

---

### 4.9 Warning / Typed Dialogs (`pdgui_menu_warning.cpp`)

**Covers via type-based fallback**:
- `MENUDIALOGTYPE_DANGER (2)`: Red palette, error sound, yellow title.
- `MENUDIALOGTYPE_SUCCESS (3)`: Green palette, success chime, yellow title.

**How it works**: Reads `menudialogdef` dynamically at render time. Iterates `menuitem` array, renders:
- `MENUITEMTYPE_LABEL` → centered text, word-wrapped
- `MENUITEMTYPE_SEPARATOR` → ImGui Separator
- `MENUITEMTYPE_SELECTABLE` → Button; calls `item->handler(MENUOP_SET, item, &hd)` on click
- Other types → `ImGui::TextDisabled("[%s]", label)` — **placeholder, non-interactive**

**Bug**: `MENUITEMTYPE_LIST` (0x02) and `MENUITEMTYPE_DROPDOWN` (0x0c) fall through to `default:` and render as a greyed-out `[label]`. If any DANGER/SUCCESS dialog in the legacy system contains a list or dropdown, it breaks silently under hotswap.

**Back navigation**: B/Escape always calls `menuPopDialog()` — fallback dismiss works universally.

---

### 4.10 Update UI (`pdgui_menu_update.cpp`)

**Not hotswap.** Rendered every frame from `pdguiUpdateRender()` via `pdguiRender()`.

**Components**:
- Notification banner (top of screen) — appears when update available, auto-dismissed after "Update Now" click.
- Version picker dialog — floating window, sortable columns, per-row Download/Rollback/Switch/Cancel.
- Download progress overlay — centered, blocking, shows bytes + progress bar + Cancel.
- Restart prompt — asks to restart now or later.
- Version watermark — bottom-right corner, 35% opacity. Only visible when hotswap active or pause menu open.

**Channel selector**: Stable / Dev — live combo, triggers async re-check on change.

**State**: Standalone; reads/writes `s_ShowNotification`, `s_DownloadActive`, etc. Purely overlay; no menumgr integration.

---

## 5. Menu Coverage: Replaced vs. Remaining

### Replaced (ImGui active)
| Category | Files/Dialogs | Status |
|----------|--------------|--------|
| Agent Select | g_FilemgrFileSelectMenuDialog | ✅ Done |
| Agent Create | g_FilemgrEnterNameMenuDialog | ✅ Done |
| Main Menu (both variants) | g_CiMenuViaPcMenu + g_CiMenuViaPauseMenu | ✅ Done |
| Network Menu | g_NetMenuDialog | ✅ Done |
| Match Setup (local) | g_MatchSetupMenuDialog | ✅ Done |
| Danger dialogs | All MENUDIALOGTYPE_DANGER | ✅ Type coverage |
| Success dialogs | All MENUDIALOGTYPE_SUCCESS | ✅ Type coverage |

### Not Hotswap (ImGui but independent)
| Screen | Mechanism |
|--------|----------|
| Social Lobby | Overlay via pdguiLobbyRender() |
| Room Interior | Overlay via pdguiRoomScreenRender() |
| Pause Menu | Independent via pdguiPauseMenuRender() |
| Scorecard Overlay | Independent via pdguiScorecardRender() |
| Modding Hub | Standalone via pdguiModdingHubRender() |
| Update UI | Always-on overlay via pdguiUpdateRender() |
| Debug Menu (F12) | Overlay via pdguiDebugMenuRender() |
| Storyboard (F11) | Overlay via pdguiStoryboardRender() |

### Forced PD Native (explicitly blocked from ImGui)
14 dialogs registered with `NULL` renderFn (keyboard input / custom rendering):
`g_NetJoinAddressDialog`, `g_NetJoiningDialog`, `g_NetCoopHostMenuDialog`, `g_FilemgrRenameMenuDialog`, `g_FilemgrDuplicateNameMenuDialog`, `g_FilemgrFileSavedMenuDialog`, `g_MpPlayerNameMenuDialog`, `g_MpSaveSetupNameMenuDialog`, `g_MpEndscreenConfirmNameMenuDialog`, `g_MpChangeTeamNameMenuDialog`, `g_MpReadyMenuDialog`, `g_StatusOkDialog`, `g_StatusErrorDialog`, `g_DeleteSetupDialog`.

### Remaining (legacy PD native, no registration)
All dialogs from the 240-dialog inventory not listed above. Per the replacement plan:
- Group 1: Solo Mission Flow (11 menus) — NOT started
- Group 2: End Screens (13 menus) — NOT started
- Group 3: Options & Settings (11 menus) — Partially covered by main menu Settings tab
- Group 4: Multiplayer Setup (68 menus) — Partially covered by match setup / room interior
- Group 5: Multiplayer In-Game (15 menus) — NOT started
- Group 6: File Manager remaining (25-2=23 menus) — NOT started (Agent Select + Create done)
- Group 7: Training Mode (24 menus) — NOT started
- Group 8: Cheats (9 menus) — NOT started
- Group 9: Misc / Error / 4MB (remaining) — Partially covered by typed dialog renderer

---

## 6. Controller Support Audit

### Per-Screen Assessment

| Screen | Controller Nav | D-pad | A/B | Special |
|--------|---------------|-------|-----|---------|
| Main Menu | ✅ Full ImGui nav | Via ImGui | ✅ | L1/R1 for tab cycling |
| Agent Select | ✅ Explicit D-pad | ✅ | ✅ A=Load, B=cancel confirm | X/Y/D/RB for actions |
| Agent Create | ⚠️ Partial | Body: ✅ L1/R1; Head: ❌ arrow only | ✅ B=cancel | Head selector needs D-pad |
| Warning Dialogs | ✅ B=dismiss | Via ImGui | ✅ | Handler called on button press |
| Network Menu | ✅ B=Back | Via ImGui | ✅ | Text field needs OS keyboard |
| Lobby | ⚠️ B=**Disconnect** | N/A (no list nav) | ❌ B disconnects immediately | CRITICAL BUG |
| Room Interior | ✅ ImGui nav | Via ImGui | ✅ B=Leave Room | Tab nav available |
| Match Setup (local) | ✅ ImGui nav | Via ImGui | ✅ | Bot modal via ImGui |
| Pause Menu | ✅ Full | Via ImGui | ✅ B=context back | Start=resume |
| Scorecard | N/A (no input) | N/A | N/A | Back held = show |
| Update UI | ✅ ImGui nav | Via ImGui | ✅ | No special binds |

### Mouse-Only Interactions
None identified. All primary actions have button equivalents. Text input fields (server address, agent name) require physical keyboard — this is expected and unavoidable without an on-screen keyboard implementation.

### Keyboard Shortcuts
- Tab navigation: works globally via `NavEnableKeyboard`.
- Enter: works as confirm in InputText fields and as "click focused button".
- Escape: explicitly wired in most screens; also consumed by menumgr cooldown.
- Arrow keys: wired in Agent Select explicitly (separate from D-pad bindings).

### ImGui NavEnableGamepad Gap
`NavEnableGamepad` lets the left stick and D-pad navigate ImGui focus. However:
- Focus order is layout-order (top-left to bottom-right by ImGui default).
- The `ArrowButton` widgets for head selection in Agent Create are not in natural tab order relative to the body carousel (different visual regions). The user has to Tab several times to reach them with keyboard nav.
- This won't be noticeable on controller if the player doesn't know to look for it.

---

## 7. Accessibility Assessment

### Font Readability
- Handel Gothic 24pt at 720p (scale 1.0); scales automatically to display height.
- Text is white on dark blue backgrounds — high contrast by default.
- No user-configurable font scale slider. Users wanting larger text must increase resolution.

### Color Usage
Primary palette colors used for status communication:
| Color | Used for |
|-------|---------|
| Cyan/light blue | Section headers, "Connected to server" |
| Green | Local player, online servers, positive states |
| Yellow/gold | Important labels (local player in rankings, title text) |
| Red | Danger dialogs, pause menu frame |
| Orange | Servers "In Game", loading states |
| Purple | POSTGAME room state |
| Grey | Inactive/disabled items |

**Colorblind concern**: Team colors in scorecard use 8 colors including red/green pair (teams 1 and 3) which is problematic for protanopia/deuteranopia. No alternative visual indicator (pattern, letter) for team identity. Names are shown as "T1", "T2" etc. — the number suffix is the accessible fallback.

### Screen Reader / Accessibility API
Not implemented. ImGui does not support system accessibility APIs (MSAA, IAccessible, etc.) as of v1.91.8. This is a known limitation of ImGui-based UIs.

### Language / Localization
- All ImGui menu text is hardcoded English strings.
- Arena names, scenario names, difficulty names, bot type names: all hardcoded English in local arrays.
- Agent names and weapon names go through `langGet()` (localized) — these are correct.
- The legacy PD system is fully localized; all ImGui replacements are English-only.
- Impact: any non-English speaker loses localization in replaced menus.

---

## 8. Asset Loading Pipeline Audit

### Current Architecture

```
ROM DMA (legacy path — still active):
  fileLoadToNew() → numeric filenum → ROM segment → caller memory

Asset Catalog (registry — not yet the load path):
  assetCatalogRegister() → hash table → metadata only
  assetCatalogResolve() → entry pointer or NULL
  *** does NOT trigger actual load ***
```

### What the Catalog Tracks (as of S46a)
| Asset Type | Count Registered | Source |
|-----------|-----------------|--------|
| ASSET_MAP / stages | 87 | Base game ROM + mod folders |
| ASSET_BODY | 63 | g_MpBodies[] base game |
| ASSET_HEAD | 75 | g_MpHeads[] base game |
| ASSET_WEAPON | 47 | Base game MPWEAPON_* constants |
| ASSET_PROP | 8 | Sampling of PROPTYPE_* |
| ASSET_GAMEMODE | 6 | MPSCENARIO_* constants |
| ASSET_HUD | 6 | HUD element types |
| ASSET_ANIMATION | ~? | S46b TODO — full table pending |
| ASSET_TEXTURE | ~? | S46b TODO — from ROM metadata |
| ASSET_AUDIO | ~? | S46b TODO — 1545 SFX table |

### Four Gateway Functions — Current Status
| Gateway | Intercept Phase | Status |
|---------|----------------|--------|
| `fileLoadToNew` / `fileLoadToAddr` | C-4 | ❌ PENDING |
| `texLoad*` | C-5 | ❌ PENDING |
| `animLoadFrame` / `animLoadHeader` | C-6 | ❌ PENDING |
| `sndStart` | C-7 | ❌ PENDING |

**All four intercepts are pending.** This means:
- Every file load, texture decompression, animation load, and sound start goes through numeric IDs and legacy ROM lookup.
- Mod overrides for textures, animations, and audio are NOT active — only body/head/stage model mods work (via the modmgr accessor layer, D3R-5 done).
- `assetCatalogResolve()` is called from some UI code but not from any loader.
- CATALOG_CRITICAL logging is installed at load points (SPF Audit Phase 1, S49) but the catalog is not consulted for the actual decision.

### Load State Tracking
MEM-1 is coded: `asset_entry_t` has `load_state`, `loaded_data`, `data_size_bytes`, `ref_count`. But:
- MEM-2 (`assetCatalogLoad()` / `assetCatalogUnload()`) is PENDING.
- MEM-3 (ref_count acquire/release + eviction policy) is PENDING.
- Bundled assets are conceptually `ASSET_STATE_LOADED` with `ref_count = ASSET_REF_BUNDLED = 0x7FFFFFFF` (sentinel for "never evict") but this isn't set in the registration code — it's documented as the intent.

### Stage Transition Loading
Currently: `lv.c` loads all stage assets synchronously at stage start. No catalog query. No incremental diff.

Planned (C-9):
1. Query catalog for all assets needed by new stage.
2. Diff against currently loaded assets.
3. Unload no-longer-needed assets (free memory).
4. Load new-needed assets.
5. Skip already-loaded assets (no-op).

C-9 depends on C-4 through C-7. Blocked.

### Mod System Interaction with Catalog
- D3R-5 done: all 6 modmgr accessor functions are catalog-backed.
- When a mod overrides a stage/body/head, `catalogResolveBodyIndex()` / `catalogResolveStageIndex()` return the runtime index of the mod asset.
- This works because the accessor layer was migrated — the callers didn't change.
- But textures, animations, and audio in mods are still not routed through the catalog for actual loading.

### Failure Handling
- `assetCatalogResolve()` returns NULL if not found or not enabled.
- From the catalog loading plan: "If not in catalog: log error, return NULL (never silently load uncataloged assets)" — this is the target behavior, not current.
- Currently, loading failures are handled per-gateway (file.c NULL return checks, texLoad fallback, etc.) with CATALOG_CRITICAL logging installed at key points.
- User-visible failure: only if the game crashes or shows visual corruption. There is no "asset not found" error screen.

### Load Queue / Async Loading
**There is none.** All loading is synchronous on the main thread. The updater system uses async download threads, but game asset loading is fully blocking.

### What Would Need to Change for Async Loading
1. C-4 through C-7 must be in place (catalog is the load gate).
2. `assetCatalogLoad()` / `assetCatalogUnload()` must be implemented (MEM-2).
3. A separate loader thread with a request queue.
4. Main thread checks load state each frame (`ASSET_STATE_LOADING` → `ASSET_STATE_LOADED` transition).
5. Stage entry deferred until all required assets are `ASSET_STATE_LOADED`.
6. Loading screen to show during the wait.

---

## 9. Cross-Cutting Concerns

### Network Disconnection During Menus
- **Pause menu** End Game branch: handles gracefully (calls `netDisconnect()` then closes pause).
- **Lobby** disconnect: immediate via B/Escape — no protection.
- **What happens when the server drops the client?** This is handled in `net.c` / `netlobby.c` (not audited here). Evidence in menumgr: `menuPopAll()` exists. If net.c calls `menuPopAll()` on disconnect, all menus clear. If not, stale menu state remains.
- **No documentation** of what `menuPopAll()` call sites exist outside this audit.

### Stage Transition / Loading Screen
No loading screen. No progress indicator. Stages load synchronously. The planned async diff-load (C-9) is the future solution. Currently players stare at a frozen frame during stage load.

### Error Handling (User Visible)
| Error scenario | User sees |
|---------------|-----------|
| ROM file missing | Crash or PD native error dialog |
| Asset catalog miss | Log line; load continues with legacy numeric path |
| Mod asset missing after mount | CATALOG_CRITICAL log; load continues with base game fallback |
| Network connection refused | g_NetJoiningDialog (native PD dialog) |
| Save file corrupt | PD native danger dialog |
| Download failure | Update UI red error text |

No unified error screen. Errors surface via legacy PD dialogs, CATALOG_CRITICAL log lines, or visual corruption.

---

## 10. Bugs and Issues Identified

| ID | Severity | Description | Location |
|----|----------|-------------|----------|
| B-LOBBY-DISCONNECT | High | B/Escape immediately disconnects — no confirmation | pdgui_menu_lobby.cpp:373-377 |
| B-HEAD-DPAD | Low | Head selector in Agent Create has no D-pad binding | pdgui_menu_agentcreate.cpp:436-475 |
| B-WARNING-COMPLEX | Medium | List/dropdown items render as `[label]` in typed dialogs | pdgui_menu_warning.cpp:300-304 |
| B-COUNTER-OP-GRAYED | Medium | Counter-Op always disabled on PC (no second controller) | pdgui_menu_mainmenu.cpp:~line 92 |
| B-NO-LOADING-SCREEN | Medium | Stage transitions blocking with no visual feedback | Architecture gap |
| B-ARENA-HARDCODED | Low | Room interior arena list hardcoded; won't reflect mod arenas | pdgui_menu_room.cpp:195-215 |
| B-CATALOG-NOT-WIRED | Critical | C-4 through C-7 intercepts PENDING; catalog is not load path | catalog-loading-plan.md |
| B-LANG-IMGUI | Low | All ImGui replacement menus are English-only | All pdgui_menu_*.cpp |

---

*Created: 2026-03-27, Session 62 (deep audit — no code changes)*
