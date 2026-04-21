# Menu Stack Architecture

> **Status**: DESIGN (2026-04-19). Codifies invariants for the ImGui menu system.
> **Supersedes informal rules in**: `input-authority-and-menu-pool-2026-04-13.md` §6, `d5-full-menu-overhaul.md` Phase 1, `d5-ui-polish-plan.md` D5.1.
> **Related**: [menu-inventory.md](menu-inventory.md) (roster), [menu-controller-input-constraints.md](menu-controller-input-constraints.md) (gamepad + mouse progressive focus, modals, pointer sync), [input-authority-and-menu-pool-2026-04-13.md](input-authority-and-menu-pool-2026-04-13.md) (Phase 1 input gating + Phase 2 pool layer — already landed), [hud-layer-order.md](hud-layer-order.md) (render-layer ordering).
> **Do not implement from this doc** — it defines the target. Implementation tasks are in `tasks-current.md` under *Menu Stack Compliance*.

---

## 1. Motivation

ImGui menus are the sole menu system (constraint added 2026-04-02). The legacy `menuPush`/`menuPop` dialog stack is retained as plumbing for hotswap, but all visible rendering lives in `port/fast3d/pdgui_menu_*.cpp`.

The system has accumulated three independent ad-hoc patterns for the same problem ("which menu owns input right now?"):

1. **Legacy dialog stack** (`g_Menus[].layers[].siblings[]`) — F-3.1 runtime dedup.
2. **ImGui renderer file-static bools** (`s_*PushedCtx`) — each file tracks its own `inputCtxPush` ownership.
3. **Input context stack** (`inputctx.c`) — truth for mouse/keyboard routing, but context-agnostic to which menu pushed it.

S299 Phase 2 introduced `port/src/menupool.c` as the structural dedup layer (one slot per `menu_type_t`), replacing F-3.1 with a hard invariant. This doc extends that foundation into a full tree-stack specification — **the menu stack is the project's single source of truth for menu identity, ancestry, and input authority**.

---

## 2. The strict tree stack

### 2.1 Model

The active menu state is a **single linear chain** from root to leaf — a stack, not a graph and not a forest. At any time:

```
stack = [ Root, Child, GrandChild, ..., Leaf ]
```

- `Root` is always **Main Menu** (`MENU_TYPE_MAIN_MENU`).
- Each element is a `menu_type_t` (one instance per type, enforced by the pool).
- `Leaf` is the top-of-stack entry that owns input authority.
- `Parent(n) = stack[n-1]`. A menu's parent is determined at push time.
- Popping `Leaf` makes `stack[N-2]` the new leaf.
- Popping a non-leaf cascade-closes every descendant (§3).

### 2.2 Invariants

The tree stack enforces five invariants. Violations are bugs:

| # | Invariant | Enforcement |
|---|-----------|-------------|
| **I1** | **Single root** | Only `MENU_TYPE_MAIN_MENU` may be pushed with no parent. All other pushes require an active parent already on the stack. |
| **I2** | **One instance per type** | `menupoolAcquire(T)` returns 0 when `T` is already active — structural dedup. No type can appear twice in the stack. |
| **I3** | **Linear chain (no siblings)** | When a push happens while the stack leaf is `L`, the new entry becomes child of `L`. Pushing a second child of `L` while the first is still alive must cascade-close the first (the old branch dies before the new one is born). |
| **I4** | **Input authority at the leaf only** | Exactly one menu — the current leaf — receives input. All ancestors render but are inert. |
| **I5** | **Render top-down** | Stack entries render root-first, leaf-last, so leaf draws over ancestors. |

**Why "no siblings" matters**: Without I3, two menus descended from different branches can be alive at the same time. The player sees a stack like `[Main, Online, Room]`, opens a cheats confirm from Main (wrong), gets a visual collision and undefined focus. Forbidding sibling branches reduces the reachable state space to a single chain — the only thing the player can do is push deeper or pop back.

### 2.3 Push semantics

```c
s32 menupoolAcquire(menu_type_t type, const menudialogdef *def, InputContext *ctx);
```

- Returns `1` on fresh acquire (stack grows by one).
- Returns `0` on duplicate (no mutation; caller treats as "already open, refocus").
- Acquires optional ownership of an `InputContext` — the pool pushes it if not already active, and pops it on release. Multiple pool slots may name the same context (e.g. `g_CtxImGuiMenu` shared by Main Menu and Modding Hub); only the first acquire actually pushes, and only the last release actually pops. This is the correct behavior for a shared-context design.
- The pool is the **sole** mechanism for declaring "this menu is now open". Hand-rolled `inputCtxPush(&g_CtxImGuiMenu)` outside the pool is forbidden (violates I2 and defeats cascade close).

### 2.4 Pop semantics

```c
s32 menupoolRelease(menu_type_t type);
void menupoolReleaseAll(void);
```

- `menupoolRelease(T)` frees the slot and pops the owned context. Silent no-op if `T` wasn't active.
- `menupoolReleaseAll()` bulk teardown — called by stage transitions, match start, netmsg handlers, and `inputCtxShutdown()`. Never skips a level.

**Close paths** must invoke `menuPopDialog(...)` (legacy) or `menupoolRelease(...)` (pure-ImGui). Closing only via `ImGui::Begin(..., &open)`'s close button without calling through the pool leaves the pool slot claimed — the menu reappears stuck-closed next frame, or (worse) the input context stays pushed and the player is frozen.

---

## 3. Cascade close

### 3.1 The rule

**Popping a parent pops every descendant, in leaf-to-root order.**

Cascade close fires in five situations:

| Trigger | Mechanism |
|---------|-----------|
| User pops a non-leaf via B/Back (rare — usually only leaf has focus) | `menuPopDialog(parent)` walks downward-then-up |
| Stage transition (`mainChangeToStage`) | `menupoolReleaseAll()` in `pdgui_bridge.c` / endscreen exits / `matchsetup.c` match start |
| Match end → endscreen | Same path |
| Netmsg-driven stage change (`netmsgSvcStageEndRead`, `SVC_STAGE_START`) | Same path |
| Input-context nuclear reset (`inputCtxShutdown`) | Same path |

### 3.2 Why leaf-to-root order matters

Each menu's `on_pop` / release callback may reset global state (mouse mode, cursor, focus). If the order is root-first, the root's `on_pop` runs while a descendant still believes it owns input. The descendant's next frame then reads stale global state (cursor hidden, mouse relative) and either soft-locks or writes the wrong config.

The pool guarantees leaf-to-root order by walking `s_Pool[]` in reverse acquire order during `menupoolReleaseAll`. Individual `menupoolRelease(T)` calls don't guarantee this — callers must pop their own leaf first before popping an ancestor (which is usually what they do, since the leaf had focus).

### 3.3 Transitions that MUST cascade-close

These are the sites that own "the menu system must be empty after this point". Any new site that changes stage or forcibly exits a flow must replicate the pattern:

- `pdguiEndscreenExitToMainMenu` (`pdgui_bridge.c`) — after mission/match end
- `matchStart` / `matchsetup.c` — before loading stage assets
- `netmsgSvcStageEndRead`, `SVC_STAGE_START` handlers (`netmsg.c`) — server-driven transitions
- `netDisconnect` (`net.c`) — disconnect tear-down
- `inputCtxShutdown` — shutdown / fatal error recovery

Forgetting `menupoolReleaseAll()` in a new transition site leaves stale pool slots that prevent re-opening the menu on the next boot. B-144 class issues.

---

## 4. Input authority model

### 4.1 The leaf owns input

Only the current stack leaf receives input. Ancestors render but are inert. This is enforced by two mechanisms working together:

**Mechanism A — Input context stack** (`port/src/inputctx.c`):

- Each menu acquires an `InputContext*` through the pool (`ctx` parameter to `menupoolAcquire`).
- The context stack routes SDL events top-down by priority. The currently-pushed context wins.
- `inputCtxSyncMouseMode()` enforces mouse capture/cursor visibility from the top context's declared mode.

**Mechanism B — Action map gating** (`port/src/actionmap.cpp`):

- `gameplayInputSuppressed()` returns true iff top context != `&g_CtxGameplay`.
- `fireVk()` skips `g_ImcGameplay` / `g_ImcVehicle` when suppressed — gameplay-only VKs never write `s_State` while a menu is open.
- `actionPressed`/`actionHeld`/`actionReleased`/`actionValue` for gameplay-only actions return 0 when suppressed (belt + braces).

### 4.2 No menu may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly

Constraint (ledger 2026-04-02, enforced 2026-04-07 S170): mouse capture is driven by the context stack via `inputCtxSyncMouseMode()`. Individual menus declare their mode in the context definition, not by calling SDL.

### 4.3 Shared contexts are acceptable

Multiple pool slots may name the same `InputContext*`. The canonical shared context is `g_CtxImGuiMenu`, used by every top-level menu (Main, Solo, Online, Settings, etc.). The pool's ref-counted push/pop design makes this safe: only the first acquire pushes, only the last release pops. This is the correct pattern — do not invent per-menu input contexts without cause.

Dedicated contexts exist for:
- `g_CtxGameplay` — gameplay (mouse captured, gameplay IMC highest)
- `g_CtxPauseMenu` — solo pause (suppresses gameplay IMC, mouse visible)
- `g_CtxDebugOverlay` — F12 debug (very high priority, suppresses menu IMC)
- `g_CtxTextInput` — text-field focused (highest priority, suppresses all others)
- `g_CtxImGuiMenu` — all other menus

### 4.4 Focus management

Controller focus (the visual highlight) is a separate concern from input authority. Focus lives **within** a menu; input authority lives **across** menus.

Within a menu, focus rules:

- **Auto-focus first logical item on open**: when a menu becomes the leaf, focus lands on its primary interactive element (first mission row, first difficulty button, etc.).
- **Focus on modal open**: when a popup modal opens, focus lands on the confirm or cancel button (the safe default — Cancel for destructive dialogs, Confirm for benign).
- **Focus returns to invoker on close**: when a leaf pops, the new leaf (its parent) restores focus to the element that opened the child (the mission row, the list item, the menu button).
- **D-pad + stick both drive nav**, treated as digital press (no analog range in menus).
- **Circular wrapping** always on (top↔bottom, left↔right).

Implementation primitives: `ImGui::SetItemDefaultFocus()`, `ImGui::SetKeyboardFocusHere()`. The action map's menu IMC handles D-pad/stick → `ACTION_MENU_*` mapping.

---

## 5. Design constraints every menu respects

These are binding rules. Every new menu and every audited existing menu must comply.

### C1. Docked action buttons

Primary action buttons (Start, Confirm, Back, Cancel, Apply, Leave) render in a **docked footer** at the bottom of the window, never inside the scrolling content region.

**Required primitive**: `pdguiBeginActionBar("##id")` / `pdguiActionBarButton(label, isFocused, width)` / `pdguiEndActionBar()` (defined in `port/include/pdgui_layout.h`).

Hand-rolled button rows are forbidden unless the renderer is a standalone ImGui window (non-dialogdef), and even then the pattern should match: a `BeginChild` scroll region above, a fixed-height footer below. Buttons inside a scrollable `BeginChild` can scroll out of reach on short viewports — this is the class of bug that D5.6 polish flagged.

### C2. Visual previews are docked/fixed

Character model previews, mission briefing images, arena thumbnails, weapon stats panels — any widget that visually represents the selected item — render in a **sibling panel**, not inside the scrollable list region.

Layout pattern: two-column child layout (list on left, preview on right), or list above and preview-docked below, with the preview using `BeginChild(..., ImGuiChildFlags_None)` (no scroll). The preview size is fixed at the pdgui scale; the list scrolls beneath it.

### C3. Controls render within BG interior only

The PD nine-slice chrome (`pdguiDrawPdDialog` / `drawPdWindowFrame`) has an inset interior. All interactive widgets — buttons, sliders, text, list rows — must render strictly inside the interior rect, never overlapping the border artwork.

**Required pattern**: use `pdguiGetContentInset()` to compute the available rect, then `ImGui::SetCursorPos(...)` to the inset origin and constrain widget widths with `GetContentRegionAvail()`. Raw pixel offsets from `ImGui::GetWindowPos()` are forbidden — they drift when the chrome's corner/edge textures change.

S340 did a content-inset sweep; any new menu must follow that pattern.

### C4. Popup/confirm dialogs are child windows with darkened background

Confirmation prompts (End Game? / Delete? / Exit? / Abort Mission? / Restart?) render as **child popup modals over the parent menu**, not as full-screen pushes of a sibling dialog.

**Required primitive**:
```c
if (confirm_trigger) { ImGui::OpenPopup("Confirm##xyz"); }
if (ImGui::BeginPopupModal("Confirm##xyz", NULL, ImGuiWindowFlags_NoResize | ...)) {
    pdguiPopupDarkenBehind(0.65f);          // darken the parent
    ImGui::TextWrapped("Really end the game?");
    if (pdguiActionBarButton("No", 1, 0.5f))  { ImGui::CloseCurrentPopup(); }
    ImGui::SameLine();
    if (pdguiActionBarButton("Yes", 0, 0.5f)) { do_destructive(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}
```

Why a popup modal and not a `menuPushDialog`:
- The parent stays rendered and visually contextual — the player sees *what* they're about to destroy.
- Focus automatically trapping inside the popup prevents accidental activation of parent buttons.
- Cascade close on stage transition tears down the popup AND the parent in one `menupoolReleaseAll` call without needing an explicit "close sibling" step.
- The darken primitive (`pdguiPopupDarkenBehind`) signals urgency without the parent disappearing.

Reference implementations: `pdgui_menu_mainmenu.cpp` (Delete Agent), `pdgui_menu_warning.cpp` (generic DANGER/SUCCESS), `pdgui_menu_theme_editor.cpp`, `pdgui_menu_modmgr.cpp`.

### C5. Destructive actions always confirm

Any action that destroys state (leaving a match, deleting a save, quitting to desktop, aborting/restarting a mission, ending a game, clearing a scenario, disconnecting) **must** require a confirm-modal (C4). No-confirm destructive buttons are forbidden.

Edge case: "Back" / "Cancel" / "Close" on a menu that only has view state (no unsaved edits) does not need confirm. A mission-select "Back" button just pops the menu; it's not destructive. But anything that tears down gameplay, saves, or network state must confirm.

---

## 6. Mission select — progressive focus

The mission select flow is the canonical **progressive-focus** pattern. Other selection flows (match setup → bot → character, arena → weapons → tunes) follow the same shape.

### 6.1 Flow

```
Missions list (focus)
     │ A on "dataDyne Central"
     ▼
Mission detail (preview docked, difficulty buttons focused)
     │ A on "Perfect Agent"
     ▼
Start Mission button (focused)
     │ A
     ▼
Loading / mission start
```

At each step, **B** pops focus up one level. B on Missions list pops to Main Menu (menu leaf becomes Main).

### 6.2 Focus rules

- Each step **narrows** focus to a child control group without changing the menu leaf. The menu stays `MENU_TYPE_SOLO_MISSION` throughout; what changes is which focus group is active.
- Focus narrowing is implemented with a file-static `s_FocusGroup` enum in the renderer (e.g. `FOCUS_MISSION_LIST`, `FOCUS_DIFFICULTY`, `FOCUS_START`). Input routes through the same menu, but only the active group's items respond to A/B.
- Back from `FOCUS_DIFFICULTY` → `FOCUS_MISSION_LIST` (focus returns to the last-selected mission row).
- Back from `FOCUS_START` → `FOCUS_DIFFICULTY`.
- Back from `FOCUS_MISSION_LIST` → `menuPopDialog(&g_SelectMissionMenuDialog)` — now the stack shrinks.

### 6.3 Why narrow-within-menu rather than push-more-menus

Three reasons:
1. **Visual continuity**: the mission-detail panel stays on screen while the player picks difficulty. Pushing a new menu would tear down and re-render the preview.
2. **Stack hygiene**: a three-deep flow (mission → difficulty → start) would grow the menu stack by 2 for what is logically one selection act. Keeps the stack shallow (I5 render cost, I4 authority clarity).
3. **Pattern reuse**: match setup, room, training all have the same list → detail → confirm shape. Codifying it as "progressive focus inside one menu" gives every such screen the same code structure.

### 6.4 Other screens that use this pattern

- **Match Setup** — Arena list → Arena detail → weapons/limits/bots panels
- **Room** — Combat Sim / Campaign / Counter-Op tab → scenario → ready
- **Character Select** — Body list → head list → confirm
- **Training** (Firing Range, DarkSim, HoverBike) — Challenge list → details → start

---

## 7. Audit: current state of every ImGui menu

Source: full audit of `port/fast3d/pdgui_menu_*.cpp` 2026-04-19. See each file's parent inference and ownership style. Compliance columns:

- **Ctx**: uses `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` (canonical) vs direct `inputCtxPush` vs N/A
- **Docked Btn**: action buttons in `pdguiBeginActionBar` footer
- **Docked Prev**: preview panel outside scroll region
- **BG**: widgets render strictly inside nine-slice interior
- **Confirm**: destructive actions use `BeginPopupModal` + `pdguiPopupDarkenBehind`
- **Focus**: auto-focus on open / popup

### 7.1 Compliance table

| File | Canonical Dialog(s) | Parent | Ctx | Docked Btn | Docked Prev | BG | Confirm | Focus | Status |
|------|--------------------|--------|-----|------------|-------------|----|---------|-------|--------|
| pdgui_menu_mainmenu.cpp | g_CiMenuViaPc…, g_CiMenuViaPause…, g_ChangeAgent…, g_ExtendedVideo/Audio/Mouse… | Root | pool | Y | N/A | Y | **Y** (BeginPopupModal Delete) | Y | **GOLD** |
| pdgui_menu_warning.cpp | g_StatusOk/Error, MENUDIALOGTYPE_DANGER/SUCCESS, text-input fallback | Generic child | pool | Y | N/A | Y | **Y** (native BeginPopupModal) | Y | **GOLD** |
| pdgui_menu_theme_editor.cpp | (Theme Editor) | Settings | direct | Y | Y (palette) | Y | **Y** (BeginPopupModal root) | Y | **GOLD** |
| pdgui_menu_modmgr.cpp | (Mod Manager) | Modding Hub / Main | direct | PART | N/A | ? | **Y** (Unsaved Changes / Large Mod / Validation) | N | **GOLD-ish** (layout polish pending) |
| pdgui_menu_agentselect.cpp | g_FilemgrFileSelect… | MainMenu | pool | Y | Y (char preview) | Y | **N** — inline prompt for Delete/Copy | Y | **VIOLATION C5** (inline confirm, not modal) |
| pdgui_menu_agentcreate.cpp | g_FilemgrEnterName… | AgentSelect | pool | Y | Y (char preview) | Y | N/A | Y | OK |
| pdgui_menu_botsetup.cpp | g_MpSimulants… (+4) | Room | pool | Y | Y (preview) | Y | N/A | Y | OK |
| pdgui_menu_challenges.cpp | g_MpChallenges… (+2) | MainMenu → MP | none direct | Y | N/A | Y | N/A | Y | OK |
| pdgui_menu_cheats.cpp | g_CheatsMenuDialog (+8) | MainMenu | none direct | Y | N/A | Y | **N** — pushes `g_CheatsConfirmUnlockMenuDialog` sibling instead of popup modal | Y | **VIOLATION C4/C5** (wrong confirm pattern) |
| pdgui_menu_controldiagram.cpp | g_SoloMissionControlStyle…, g_MpControl… | Training / MP | none | PART | Y (diagram) | ? | N/A | Y | **VIOLATION C1** (no action bar) |
| pdgui_menu_endscreen.cpp | g_SoloMissionEndscreenCompleted… (+12) | Standalone (match-end) | direct | PART | N/A | Y | **N** — Quit exits without confirm (terminal state — may be intentional) | ? | **PARTIAL** (no confirm on Quit; button not action-bar) |
| pdgui_menu_forge.cpp | (entry shim) | MainMenu | N/A | N/A | N/A | N/A | N/A | N/A | Shim only |
| pdgui_menu_lobby.cpp | (Lobby custom window) | Network | none | PART | N/A | ? | N/A | N | **VIOLATION C1** (hand-rolled footer, no action bar) |
| pdgui_menu_logviewer.cpp | (Dev Window tab) | Standalone | N/A | N/A | N/A | N/A | N/A | N/A | Dev tool |
| pdgui_menu_moddinghub.cpp | (Modding Hub window) | MainMenu | none | PART | Y (model) | ? | N/A | N | **VIOLATION C1** (custom footer) |
| pdgui_menu_mpadvanced.cpp | g_MpAdvancedSetup… (+10) | MainMenu → MP | pool | Y | N/A | Y | N/A | Y | OK |
| pdgui_menu_mpingame.cpp | (no-op suppress) | InGame | N/A | N/A | N/A | N/A | N/A | N/A | Kill-ticker overlay |
| pdgui_menu_mppause.cpp | g_MpPauseControl… (+5) | PauseMenu (MP match) | pool | Y | N/A | Y | **N** — End Game pushes legacy `g_MpEndGameMenuDialog` sibling, not popup modal | Y | **VIOLATION C4/C5** (End Game sibling-push) |
| pdgui_menu_mpsettings.cpp | g_MpHandicaps… (+3) | Room / MP | pool | Y | N/A | Y | N/A | Y | OK |
| pdgui_menu_mpsetup.cpp | g_MpArena… (+13) | Room / MP | pool | Y | N/A | Y | N/A | Y | OK |
| pdgui_menu_network.cpp | g_NetMenuDialog | MainMenu | none direct | PART | N/A | ? | N/A | N | **VIOLATION C1** (no action bar) |
| pdgui_menu_pausemenu.cpp | (MP pause + scorecard overlay) | Standalone | direct (ctx push) | PART | N/A | Y | **N** — Quit button in scorecard body, no modal | N | **VIOLATION C1/C4** (button scrolls; no modal on Quit) |
| pdgui_menu_playerconfig.cpp | g_MpCharacter… (+4) | MP / Room | pool | Y | Y (3D preview) | Y | N/A | Y | OK |
| pdgui_menu_room.cpp | (Room screen custom) | Lobby | pool | Y | Y (char preview) | ? | PART (Bot Settings, Save/Load Scenario Y; Leave Room and scenario Delete ?) | PART | **PARTIAL** — verify Leave Room + Delete Scenario have modals |
| pdgui_menu_solomission.cpp | g_SelectMission… (+10) | MainMenu | pool | Y | Y (mission preview) | Y | **N** — Abort pushes full `g_MissionAbortMenuDialog` (red palette) instead of popup modal | Y | **VIOLATION C4/C5** (Abort sibling-push) |
| pdgui_menu_stats.cpp | (Stats Viewer) | MainMenu | none | PART | N/A | ? | N/A | N | **VIOLATION C1** (custom footer) |
| pdgui_menu_teamsetup.cpp | g_MpTeams…, g_MpAutoTeam… | Room | none direct | Y | N/A | Y | N/A | Y | OK |
| pdgui_menu_training.cpp | g_FrDifficulty… (+11) | Training | pool | Y | PART (some legacy dialogs keep 3D preview inside scroll) | Y | N/A | Y | **PARTIAL** (audit C2 on Bio/Hangar sub-screens) |
| pdgui_menu_update.cpp | (update banner) | Settings / overlay | none | PART | N/A | ? | N/A | N | **VIOLATION C1** (banner overlay, not a menu) — acceptable for banner role |
| pdgui_debugmenu.cpp | (F12 overlay) | Standalone | N/A | N/A | N/A | ? | N/A | N | Dev overlay |
| pdgui_menu_audiomod.cpp | (Modding Hub tab) | Modding Hub | N/A | N/A | N/A | ? | N/A | N | Embedded tab |

### 7.2 Parent tree (inferred from pushes)

```
(boot / game loop)
└── MENU_TYPE_MAIN_MENU  ──────────────────────────── root
    ├── MENU_TYPE_AGENT_SELECT                         — save file picker
    │   └── MENU_TYPE_AGENT_CREATE                     — name entry
    ├── MENU_TYPE_SOLO_MISSION                         — mission select (progressive focus: list → diff → start)
    │   └── MENU_TYPE_SOLO_MISSION_PAUSE               — in-mission pause
    │       └── MENU_TYPE_SOLO_OPTIONS                 — pause submenu
    ├── MENU_TYPE_TRAINING                             — CI training
    │   └── (sub-dialogs: FR / DT / HT / Bio / Hangar — all via menuPushDialog)
    ├── MENU_TYPE_NETWORK                              — join / host
    │   └── MENU_TYPE_ROOM                             — lobby room
    │       ├── MENU_TYPE_MP_SETUP                     — arena / scenario / weapons / limits (progressive focus)
    │       ├── MENU_TYPE_MP_SETTINGS                  — handicaps
    │       ├── MENU_TYPE_MP_SOUNDTRACK                — soundtrack hub
    │       │   └── MENU_TYPE_MP_TUNES                 — tunes picker
    │       ├── MENU_TYPE_MP_ADVANCED                  — advanced setup
    │       ├── MENU_TYPE_MP_PLAYER_CONFIG             — char / body / head
    │       ├── MENU_TYPE_MP_BOT_SETUP                 — bot simulants
    │       ├── MENU_TYPE_MP_TEAM_SETUP                — teams / auto-team
    │       └── MENU_TYPE_MP_TEAMNAMES                 — team name editor
    ├── MENU_TYPE_CHALLENGES                           — challenge list + details
    ├── MENU_TYPE_CHEATS                               — cheats unlock
    ├── MENU_TYPE_CI_OPTIONS                           — extended settings
    ├── MENU_TYPE_MODDING_HUB                          — mod manager + tools
    │   └── MENU_TYPE_THEME_EDITOR                     — theme editor (popup modal)
    ├── MENU_TYPE_STATS_PANEL                          — stats viewer
    ├── MENU_TYPE_CINEMA                               — cutscene list
    ├── MENU_TYPE_CONTROL_DIAGRAM                      — controller layout
    └── MENU_TYPE_WARNING_MODAL                        — DANGER/SUCCESS popups (generic fallback)

(in-match)
└── MENU_TYPE_PAUSE_MENU (g_CtxPauseMenu)              — MP pause / scorecard overlay
    └── MENU_TYPE_MP_PAUSE                             — in-match MP pause screen
        └── (settings / leave-match)

(debug)
└── MENU_TYPE_DEBUG_OVERLAY                            — F12
```

### 7.3 Non-compliance summary

**Wrong confirm pattern** (C4/C5 — push full dialog instead of popup modal):
- `pdgui_menu_mppause.cpp` — `End Game` → pushes `g_MpEndGameMenuDialog`
- `pdgui_menu_solomission.cpp` — `Abort Mission` → pushes `g_MissionAbortMenuDialog`
- `pdgui_menu_cheats.cpp` — `Confirm Unlock` → pushes `g_CheatsConfirmUnlockMenuDialog`
- `pdgui_menu_agentselect.cpp` — `Delete` / `Copy` → inline prompt in parent window

**Missing confirm entirely** (C5):
- `pdgui_menu_pausemenu.cpp` — `Quit` in scorecard — no modal
- `pdgui_menu_endscreen.cpp` — `Quit` — no modal (terminal state; may be acceptable)
- `pdgui_menu_room.cpp` — `Leave Room` / scenario `Delete` — unverified, likely no modal

**No action bar** (C1 — buttons hand-rolled, scroll-off risk):
- `pdgui_menu_pausemenu.cpp`
- `pdgui_menu_lobby.cpp`
- `pdgui_menu_network.cpp`
- `pdgui_menu_moddinghub.cpp`
- `pdgui_menu_stats.cpp`
- `pdgui_menu_update.cpp` (overlay; acceptable)
- `pdgui_menu_controldiagram.cpp`
- `pdgui_menu_endscreen.cpp` (partial — uses custom `PdEndButton` at computed Y)

**Preview inside scroll** (C2 — visual preview or detail panel in scroll region):
- `pdgui_menu_training.cpp` — Bio / Hangar sub-screens (some legacy dialogs keep 3D preview inside scroll)
- `pdgui_menu_moddinghub.cpp` — 22 `BeginChild` regions, model preview placement ambiguous
- `pdgui_menu_controldiagram.cpp` — diagram layout unverified
- `pdgui_menu_room.cpp` — 9 `BeginChild` + char preview; placement relative to scroll parent needs verification

**Canonical pool+ctx adoption is solid**: 10 files use `menupoolAcquireDialog(def, &g_CtxImGuiMenu)` (the reference pattern). Standalone windows (`pausemenu`, `endscreen`, `theme_editor`, `modmgr`, `moddinghub`, `lobby`, `network`, `stats`, `update`) use direct `inputCtxPush` or no ctx push; some of these are legitimate (standalone = not a dialog), some are migration debt.

---

## 8. Prioritized punch list

Sessions implementing this doc should pick items from the top of the list. Each row is an independent fix.

### Tier 1 — correctness bugs (destructive action confirms)

| ID | File | Fix |
|----|------|-----|
| M-1 | `pdgui_menu_mppause.cpp` | Replace `End Game` sibling-dialog push with inline `BeginPopupModal` + `pdguiPopupDarkenBehind(0.65f)`. Remove `g_MpEndGameMenuDialog` from the tree; retain the dialogdef only if legacy-stack compatibility requires it. |
| M-2 | `pdgui_menu_solomission.cpp` | Replace `Abort Mission` full-screen push with `BeginPopupModal` over the pause menu. Preserve the red/danger palette inside the modal body. Keep `g_MissionAbortMenuDialog` as a legacy shim if needed. |
| M-3 | `pdgui_menu_cheats.cpp` | Replace `Confirm Unlock` sibling-dialog push with `BeginPopupModal`. |
| M-4 | `pdgui_menu_agentselect.cpp` | Replace inline "Delete" / "Copy" prompt with `BeginPopupModal`. Focus on Cancel by default. |
| M-5 | `pdgui_menu_room.cpp` | Audit `Leave Room` and scenario `Delete` paths. Add `BeginPopupModal` confirms where missing. |
| M-6 | `pdgui_menu_pausemenu.cpp` | `Quit` in scorecard overlay — add `BeginPopupModal` confirm ("Quit to desktop?" or "Leave match?"). |

### Tier 2 — layout bugs (scroll-off / docked buttons)

| ID | File | Fix |
|----|------|-----|
| M-7 | `pdgui_menu_pausemenu.cpp` | Migrate to `pdguiBeginActionBar` / `pdguiActionBarButton` for footer buttons. Ensure scrollable content uses a `BeginChild` with footer outside. |
| M-8 | `pdgui_menu_lobby.cpp` | Same — migrate `Disconnect` / `Create Room` buttons to docked action bar. |
| M-9 | `pdgui_menu_network.cpp` | Migrate to action bar primitives. |
| M-10 | `pdgui_menu_moddinghub.cpp` | Migrate footer buttons to action bar; verify model preview is outside any scroll region (C2). |
| M-11 | `pdgui_menu_stats.cpp` | Migrate to action bar primitives. |
| M-12 | `pdgui_menu_controldiagram.cpp` | Migrate footer to action bar; verify diagram panel is docked outside scroll. |
| M-13 | `pdgui_menu_endscreen.cpp` | Replace custom `PdEndButton` Y-offset layout with `pdguiBeginActionBar`. |

### Tier 3 — preview-in-scroll audits (C2)

| ID | File | Fix |
|----|------|-----|
| M-14 | `pdgui_menu_training.cpp` | Audit Bio / Hangar sub-screens; extract 3D preview to a sibling docked panel rather than inside the text scroll region. |
| M-15 | `pdgui_menu_moddinghub.cpp` | Audit model preview placement across 22 `BeginChild` regions; ensure the preview is docked. |
| M-16 | `pdgui_menu_room.cpp` | Verify char preview is in sibling column, not inside player-list scroll. |
| M-17 | `pdgui_menu_controldiagram.cpp` | Verify diagram is outside any scroll region. |

### Tier 4 — progressive focus adoption

| ID | File | Fix |
|----|------|-----|
| M-18 | `pdgui_menu_solomission.cpp` | Formalize progressive-focus groups (list → difficulty → start). Currently implicit; codify `s_FocusGroup` enum and B-back behavior. |
| M-19 | `pdgui_menu_mpsetup.cpp` | Apply progressive focus to arena → weapons/limits → confirm. |
| M-20 | `pdgui_menu_room.cpp` | Apply progressive focus to scenario → ready. |
| M-21 | `pdgui_menu_training.cpp` | Apply progressive focus to challenge → details → start for FR / DT / HT. |

### Tier 5 — remaining standardization

| ID | File | Fix |
|----|------|-----|
| M-22 | all non-canonical | Migrate direct `inputCtxPush(&g_CtxImGuiMenu)` calls to `menupoolAcquire(type, def, &g_CtxImGuiMenu)` where the menu has a dialogdef. Standalone windows (no dialogdef) keep direct push but register their type in the pool. |
| M-23 | `pdgui_bridge.c`, `matchsetup.c`, `netmsg.c`, `net.c` | Verify every stage-transition / match-start / match-end / disconnect site calls `menupoolReleaseAll()`. Add missing sites. |
| M-24 | `menupool.h` | Consider adding an `ASSERT(parent_type)` parameter to `menupoolAcquire` — hard-enforce I1 (only MAIN has no parent) and I3 (push-while-another-branch-is-alive cascades closed). Opt-in during Tier 2-4 migration. |

---

## 9. Implementation guidance for sessions working on M-* items

**Do one tier at a time, one file at a time.** Each file is a small diff (~30-100 lines) and a playtest (verify the menu opens, the confirm fires, the cascade close works after stage transition). Batching multiple files in one session makes regressions hard to bisect.

**Before opening a file to modify**:
1. Read the file's top 80 lines to confirm its current ctx/dock/confirm pattern.
2. Read a gold-standard reference (`pdgui_menu_mainmenu.cpp` for Delete Agent modal, `pdgui_menu_warning.cpp` for DANGER modal, `pdgui_menu_theme_editor.cpp` for standalone + modal) to confirm the target pattern.
3. Check the audit row in §7.1 to confirm what's expected.

**Test checklist after each file**:
- [ ] Menu opens from its parent
- [ ] B/Back pops to parent (stack shrinks by one)
- [ ] Cascade close on stage transition (start a mission from the menu → menu disappears)
- [ ] Destructive action opens popup modal (if applicable); parent stays rendered underneath; Cancel dismisses; Confirm fires
- [ ] Controller focus auto-lands on primary element (and on Cancel for destructive modals)
- [ ] Scroll region handles long lists without button scroll-off
- [ ] No overlap with nine-slice border

**Do not** introduce new `s_*PushedCtx` bools, new hand-rolled `inputCtxPush(&g_CtxImGuiMenu)` outside the pool, or new sibling-dialog confirms. Those are the patterns this doc is eliminating.

---

## 10. Open questions / deferred

- **Multi-dialog progressive focus**: should `MENU_TYPE_SOLO_MISSION` remain one type across list/diff/start focus groups, or split into sub-types? Current decision: one type (§6.3). Revisit if scorekeeping (e.g. "did the player enter difficulty view?") needs per-group pool metadata.
- **Popup modals and the pool**: today popup modals are ImGui-native (`BeginPopupModal`) and NOT pool-registered. Should destructive-confirm modals acquire a `MENU_TYPE_CONFIRM_MODAL` slot so cascade close cleans them up? Currently not required — `BeginPopupModal` tears down with its parent on the next frame when the parent window closes — but if we add confirms that outlive their parent, we'd need pool representation.
- **Tier 5 M-24 assertion**: worth doing? Opinion: yes, but only after Tier 1-3 land so the failing cases are caught on the way in rather than as surprise violations. Opt-in flag `MENUPOOL_STRICT_TREE=1` at build time would be low-risk.

---

*End of document.*
