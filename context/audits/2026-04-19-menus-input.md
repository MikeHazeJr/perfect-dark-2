# Deep Project Audit Report — Perfect Dark 2

> **Scope**: Subsystem audit — Menu Systems + Input Authority + UI/HUD.
> **Date**: 2026-04-19.
> **Branch**: `claude/reverent-goldstine-edf1af` (worktree).
> **Trigger**: Post-M-1..M-23 menu-stack-compliance sweep verification (S385–S390).
> **Auditor**: Claude (super-audit skill, references `menu-stack-architecture.md`).

The 23-item compliance sweep (M-1..M-23) has landed on `dev`. This audit verifies whether each item achieved its design goal, surfaces gaps in scope coverage, and adds first-class findings on input-authority discipline, HUD layer policy, and Phase 2.5 layout/upstream-shape hazards inside the menus + input + HUD perimeter.

---

## 1) Inferred Project Goal & Intended Outcomes

### Inferred Purpose
- The menu/input subsystem is the player's contract with the engine. It must (a) own input authority deterministically (only the leaf menu reads input; gameplay never reads while a menu is open), (b) cascade-close on every transition (stage change, match start/end, disconnect), (c) gate every destructive action behind a confirm modal that survives controller and keyboard alike, and (d) keep gameplay HUD elements (radar / score / killfeed / interact prompt) suppressed when a menu is up so they don't bleed through.
- Codified in `context/designs/menu-stack-architecture.md` (§1–§9). The 5 invariants (I1: single root; I2: one instance per type; I3: linear chain — no siblings; I4: leaf owns input; I5: render top-down) and 5 design constraints (C1: docked action buttons; C2: docked previews; C3: chrome-interior only; C4: popup-modal confirms; C5: destructive actions always confirm) are the contract.

### Likely Player & Contributor Types
- PC players (sole local target — Single-local-player constraint, Removed local MP).
- Controller users + keyboard/mouse users (S385 patterns must support both transparently — `pdguiDriveImGuiNav` translates `ACTION_USE`/`ACTION_CANCEL_USE`/`ACTION_MENU_*` into ImGui Enter/Escape/arrow events).
- Mod authors (Modding Hub, Theme Editor, Nine-Slice Chrome, Font Import, Skin Editor — all menu-pool participants).
- Speedrunners (M-18 progressive focus matters: Mission List → Difficulty → Start should be 4 button presses with zero preparatory nav).
- Network clients + dedicated-server hosts (Lobby + Room screens; menu-stack must cascade-close on disconnect).

### Core Workflows / Gameplay Loops (Inferred)
1. **Solo flow**: Main Menu → Solo Mission (progressive focus) → in-mission Pause → optional Abort confirm.
2. **Multiplayer flow**: Main Menu → Online → Network menu (server browser + direct connect) → Lobby → Room → Match → Endscreen → Main Menu.
3. **Combat Sim local**: Main Menu → Solo Play → CS Room → Match → Endscreen.
4. **Modding flow**: Main Menu → Mods (Modding Hub) → Skin / Theme / Audio / Font / Nine-Slice tools → save → Apply → restart-as-needed.
5. **Forge flow**: Main Menu → The Grid → in-stage Freefly toggle (F7) → catalog placement / properties tabs → save back to mod.
6. **Endscreen review**: Match end (solo or MP) → Mission Complete / Game Over screen with stats + objectives + actions (Next / Retry / Disconnect / Play Again / Quit / Main Menu).

### Upstream Lineage Observations
- ImGui (Dear ImGui v1.91.8) is wholly PD2-authored on top of `fgsfdsfgs/perfect_dark` `port-net`. The menu-stack-architecture doc itself is a PD2 artefact that does not exist upstream.
- Legacy `menuPushDialog` / `menuPopDialog` / `g_Menus[].layers[].siblings[]` plumbing is from `n64decomp/perfect_dark` and the port; it is preserved as a hotswap dispatcher only — all visible rendering lives in `port/fast3d/pdgui_menu_*.cpp`.
- Action map (`InputAction` enum, `g_ImcGameplay` etc.) is PD2-authored (M0.2 phase, replacing the legacy CK_* table from the port).
- Killfeed renderer (`pdgui_menu_mpingame.cpp::pdguiKillfeedPush`) is PD2-authored.
- Achievement toast renderer (`pdgui_achievement_toast.cpp`) is PD2-authored (D6 Phase 3, S378).
- Interact prompt renderer (`pdgui_interact_prompt.cpp`) is PD2-authored (S311 + B-189 fix).

### Pillar Observations (Catalog / Mod Parity / Modding Pipeline / Grid / Online Mode / Dedicated Server)
- **Catalog SOT**: Menus correctly route asset references through catalog accessors (`catalogMpBodyId`, `catalogResolveStage`, etc.); no raw filenum lookups in menu code observed within scope.
- **Mod / native parity**: Mods participate equally — Modding Hub renders identically whether mods exist or not; theme/font/chrome dropdowns enumerate mod entries via `modmgrScanDirectory` per the discovery contract.
- **Modding pipeline**: Out of scope for this audit.
- **Grid trust**: Out of scope (covered separately in The Grid editor, not menus).
- **Online-mode boundary**: The Lobby screen / Room screen / Network menu only render under `netGetMode() != NETMODE_NONE`. The cascade-close site in `netDisconnect` (S388 M-23) now releases the menu pool, but only when `wasingame == true`. **Lobby-only disconnect path is not cascade-closed** — see Finding M-23-A below.
- **Dedicated-server boundary**: Server tier renders only the Server Status panel + lobby (`pdgui_lobby.cpp::pdguiServerFrame`). No menu pool involvement on server side. Clean.
- **Scaling Ceiling**: Multiple HUD/menu C++ files duplicate the `MAX_PLAYERS=8`, `MAX_BOTS=32`, `MAX_MPCHRS=40`, `MAX_TEAMS=8` constants as file-defines (because types.h `bool s32` macro can't cross into C++). Drift hazard — see Finding L-1 below.

### Assumptions / Unknowns
- The S385–S390 work was not playtested with controller in every Tier-1 modal — focus latching is a 5-frame timing dance and may behave differently under different ImGui frame pacing.
- The double hotswap registration of `g_CheatsConfirmUnlockMenuDialog` (warning fallback + cheats-specific) relies on a register-order invariant that's not asserted in code. If init ordering is ever rearranged, the wrong renderer wins silently.
- Hotswap dispatch never being called for a "background sibling" is asserted by the legacy `g_Menus[].layers[].siblings[]` design but not verified at runtime against the popup-modal patterns. Fragility unverified without runtime trace.
- The mpchrconfig + mpplayerconfig + mpbotconfig structs in `src/include/types.h` carry stale byte-offset comments; whether any consumer (logger, debugger, mod loader) reads them and computes pointer arithmetic from those literal offsets is unverified.

---

## 2) Design Intent & Gameplay / Multiplayer Fit Review

### System & Workflow Validation Findings

#### Critical Issues

##### M-23-A — `netDisconnect` cascade-close is gated on `wasingame`; Lobby-only disconnect leaks pool slots
- Severity: **High** (escalated from Medium because it directly invalidates the M-23 contract).
- Confidence: Confirmed (code evidence).
- Location: `port/src/net/net.c:1075-1117` (the `if (wasingame && !g_AppQuitting) { ... }` block).
- Lineage: Authored-in-PD2 (S388 M-23 fix).
- Pillar Impact: Online-mode Boundary, Scaling Ceiling indirect.
- Defect Class: Trust-Boundary (cascade-close invariant).
- Intended Outcome: Every disconnect site empties the menu pool so the player returns to a clean main menu with no stale pool slots blocking re-open.
- Current Behavior: When the user clicks **Disconnect** from the Social Lobby (`pdgui_menu_lobby.cpp:386` → `netDisconnect()`), `wasingame` is `(g_NetLocalClient->state >= CLSTATE_GAME)`. In the lobby, state == `CLSTATE_LOBBY` (3 < 4 == CLSTATE_GAME), so `wasingame` is false and the M-23 `menupoolReleaseAll()` block at line 1111 is skipped entirely.
- Gap / Failure Mode: Whatever pool slots were acquired during the lobby session — `MENU_TYPE_NETWORK` (pushed when the user opened Network from main menu), and any sibling pool slots from sub-flows reached before joining a room — survive across the network teardown. The next time the player opens the same menu, `menuPushDialog` is rejected with `MENUPOOL: pool slot already active` and the menu silently fails to open. (Same class as B-160, B-194.)
- Why It Matters: Defeats the M-23 design intent ("every disconnect site cascade-closes"). Reproducible whenever a player browses to the lobby and disconnects without joining a room.
- Risk If Not Fixed: Stuck-closed Network menu after first lobby exit; user has to restart the game to reopen it. Same systemic class as the S304 leak (B-160).
- Recommendation: Move `menupoolReleaseAll()` + `inputCtxPopDeferred(&g_CtxImGuiMenu)` OUT of the `if (wasingame && !g_AppQuitting)` block. The pool release is safe to fire unconditionally — `menupoolReleaseAll()` is idempotent and only releases what's active. Place it just after the `g_NetMatchRoomId` reset at line 1071, BEFORE the `wasingame` check, so it runs for both lobby and in-game disconnects.
- Cross-System Changes Required: No (single-file fix; ~3 LOC).
- Blocks Intended Outcome?: Yes — the Lobby disconnect path is the most common in normal play.

##### M-5-A — Lobby `Disconnect` button is unconfirmed
- Severity: **High**.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_lobby.cpp:386-409`.
- Lineage: Authored-in-PD2 (S387 M-8 ported `Disconnect` to the action bar but did not add a confirm).
- Pillar Impact: Online-mode Boundary.
- Defect Class: None (UX/destructive-action).
- Intended Outcome: Per `menu-stack-architecture.md` C5: "Any action that destroys state (leaving a match, deleting a save, **quitting to desktop, aborting/restarting a mission, ending a game, clearing a scenario, disconnecting**) must require a confirm-modal."
- Current Behavior: Clicking `Disconnect` (or pressing Esc anywhere on the social lobby) calls `netDisconnect()` immediately. There is no `BeginPopupModal` confirm.
- Gap / Failure Mode: A misclick / stray Esc loses the connection without warning. Especially relevant on controller — Esc maps to `ACTION_CANCEL_USE` → `ImGuiKey_Escape` → unconditional disconnect at line 405.
- Why It Matters: M-5's stated scope was "audit `Leave Room` + scenario `Delete` paths". The S390 implementation correctly added confirms for those two but did not extend to the Lobby `Disconnect`, which is a sibling destructive action in the same screen family.
- Risk If Not Fixed: Player loses connection on accidental input; reconnect requires re-entering connect code.
- Recommendation: Add the canonical S385 modal pattern (`BeginPopupModal` + `pdguiPopupDarkenBehind(0.65f)` + 5-frame `SetKeyboardFocusHere(0)` on Cancel + 3-frame input debounce + red Confirm button). Title: "Disconnect from Server?". Body: "Disconnect from the server and return to the main menu?". This is a one-file change matching M-5's pattern.
- Cross-System Changes Required: No.
- Blocks Intended Outcome?: Partially (the S390 commit message claims M-5 closes Tier 1; this finding shows a sibling C5 violation that the S390 scope didn't cover).

##### M-6-A — Solo Pause `Restart Mission` confirm is inline ImGui window, not popup modal
- Severity: **High**.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_solomission.cpp:2935-3018` (`if (s_RestartConfirm) { ImGui::Begin("##restart_confirm", ...) }`).
- Lineage: Authored-in-PD2 (P8 era — predates the C4 contract).
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: C4: "Confirmation prompts (End Game? / Delete? / Exit? / Abort Mission? / **Restart?**) render as child popup modals over the parent menu."
- Current Behavior: Restart Mission uses an inline `ImGui::Begin("##restart_confirm", ...)` with `s_RestartSelectIdx` left/right toggle for Cancel / Restart selection. No `BeginPopupModal`, no `pdguiPopupDarkenBehind`, no 5-frame focus latch, no 3-frame input debounce. Cancel/Restart buttons read `ImGui::IsKeyPressed(ImGuiKey_Enter, false)` directly. This is the same pre-S385 pattern that M-1..M-4 retired.
- Gap / Failure Mode: (a) Enter press that opened the confirm can immediately fire Restart on the next frame because no debounce. (b) Controller focus is keyed off `s_RestartSelectIdx` instead of ImGui's NavId, so the highlight visual doesn't always match what Enter actually fires (action bar buttons elsewhere have the same hazard — see L-2 below).
- Why It Matters: The S390 entry tagged Tier 1 as "fully complete (M-1 through M-6)" but Restart was not in the M-1..M-6 scope. It deserves the same C4 pattern.
- Risk If Not Fixed: Accidental restart on Enter press; lost mission progress. C5 violation.
- Recommendation: Apply the canonical S385 pattern: `BeginPopupModal("Restart Mission?##restart")` + scrim + force-focus + debounce. Replace `s_RestartSelectIdx` with native ImGui nav.
- Cross-System Changes Required: No.
- Blocks Intended Outcome?: Yes (C4/C5 invariant).

##### M-6-B — Endscreen `Disconnect` / `Quit` / `Main Menu` are unconfirmed
- Severity: **High**.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_endscreen.cpp:1211-1232` (MP `Disconnect`, MP `Quit`); also `1228-1232` for solo `Quit`. Solo failed-mission `Main Menu` button (also red palette) at the same call site.
- Lineage: Authored-in-PD2 (S387 M-13 migrated to action bar but did not add a confirm).
- Pillar Impact: Online-mode Boundary (Disconnect), None (Quit).
- Defect Class: None.
- Intended Outcome: C5 — disconnecting and quitting are destructive.
- Current Behavior: Both buttons fire `netDisconnect() + pdguiEndscreenExitToMainMenu()` (or just `pdguiEndscreenExitToMainMenu()`) immediately on click. The action-bar primitive's `pdguiActionBarButton(label, isFocused=0, ...)` plus the `inputSuppressed` 5-frame debounce from S385 protects against the open-frame bleed but offers no actual confirm.
- Gap / Failure Mode: Accidental click on the red destructive button with no recovery. The `Main Menu` button on the failed-mission solo endscreen has the same shape — it abandons the mission with no second-press protection beyond the post-fresh-entry debounce.
- Why It Matters: The audit doc §7.1 originally tagged endscreen Quit as "may be acceptable" because it's a terminal state, but the **Disconnect** specifically tears down the network session AFTER the user already saw the rankings — a destructive transition the user might not want immediately, especially while reviewing stats. Similarly, the failed-mission `Main Menu` skips any retry flow.
- Risk If Not Fixed: Lost network session on accidental input; lost retry opportunity.
- Recommendation: Apply the canonical S385 pattern to `Disconnect` (MP) and consider it for `Quit` / `Main Menu` if Mike wants strict C5. The simpler half-step is to keep the existing `inputSuppressed` 5-frame debounce but extend it to a 30-frame "press twice within 1s" pattern — no modal, but two presses required.
- Cross-System Changes Required: No.
- Blocks Intended Outcome?: Partially (depends on Mike's interpretation of "terminal state" exception).

##### M-Q-A — Main Menu `Quit Game` uses inline button-toggle confirm, not popup modal
- Severity: **Medium** (visible to every player; the game's primary exit path).
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_mainmenu.cpp:3552-3587`.
- Lineage: Authored-in-PD2 (S289 D5 era — predates C4).
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: C5 — quitting the game is destructive (loses unsaved per-agent prefs; abandons match if connected).
- Current Behavior: `Quit Game` button toggles `s_QuitConfirm` bool; on next frame the same button shows "Confirm Quit" and a "Cancel" sibling. Click `Confirm Quit` → `SDL_PushEvent(SDL_QUIT)`.
- Gap / Failure Mode: This is the M-3 sibling-push pattern that the architecture doc retired, just inlined. No `BeginPopupModal`, no scrim, no force-focus on Cancel. A controller user mashing A on the main menu could trip both clicks within a few frames because there's no debounce.
- Why It Matters: The audit table tagged `pdgui_menu_mainmenu.cpp` as **GOLD** (Delete Agent uses BeginPopupModal correctly). But the **Quit Game** confirm uses the older inline-toggle pattern that the architecture doc explicitly retires. Inconsistency within the same file undercuts the doc's stated invariants.
- Risk If Not Fixed: Accidental quit on controller mash; same class as M-3 (Cheats Confirm Unlock).
- Recommendation: Replace with the canonical S385 modal pattern. Title: "Quit Game?". Body: "Quit Perfect Dark? Any unsaved settings will be lost.". Default focus on Cancel; red Confirm.
- Cross-System Changes Required: No.
- Blocks Intended Outcome?: Partially (C4/C5 spirit).

#### High Priority Findings

##### F-Cheats-Warning — Cheats first-use Warning modal violates C4
- Severity: **Medium**.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_cheats.cpp:713-779` (`renderCheatsWarning`).
- Lineage: Authored-in-PD2 (S195 Batch 4).
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: C4 — confirmation prompts use `BeginPopupModal`.
- Current Behavior: `renderCheatsWarning` opens a regular `ImGui::Begin("##cheats_warning", ...)` standalone window. Both OK and Cancel buttons fire `wantClose = true` (identical effect). `pdguiPopupDarkenBehind(0.55f)` is called, but no popup-modal focus trap.
- Gap / Failure Mode: (a) Cancel doesn't actually cancel anything — it has the same effect as OK (warning dismissed, cheats hub remains open). The button label is misleading. (b) Without `BeginPopupModal`, focus can escape to the parent cheats hub.
- Why It Matters: This is a one-time educational popup, not strictly destructive, but the C4 contract applies to any confirmation prompt. The Cancel button is misleading and should be removed or wired to back-out of the cheats hub entirely.
- Recommendation: Either (a) migrate to `BeginPopupModal` with a single `OK / Got it` button (no Cancel), OR (b) delete the renderer and rely on the pure `renderDangerDialog` fallback that gives the standard pattern. Document the warning's design (acknowledgment vs. confirmation) so the buttons match.
- Cross-System Changes Required: No.

##### F-IP-Browser — Server Browser displays raw IP addresses (constraints.md violation)
- Severity: **High**.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_network.cpp:217-224` (Server Browser row label).
- Lineage: Authored-in-PD2.
- Pillar Impact: Online-mode Boundary; Privacy.
- Defect Class: Trust-Boundary.
- Intended Outcome: Per `constraints.md:34`: "**No raw IP in any UI surface.** Players join via 4-word sentence connect codes only. No UI element may display or accept a raw IP address. ... Future server history must encode stored IPs back to connect codes for display."
- Current Behavior: The Server Browser fetches `addr` (a raw "X.X.X.X:port" string) via `netRecentServerGetInfo` and renders it as a Selectable label. Click a row → IP copied into the address textbox below.
- Gap / Failure Mode: Raw IP exposed in the UI. Both as the row label and as the value placed into the address box on click. Players who screenshot or stream the menu inadvertently dox other servers' IPs.
- Why It Matters: Direct violation of the Active Constraint added in Session 49.
- Risk If Not Fixed: IP exposure of remote players (constraint says recent-server IP storage is internal-only); doxxing surface in stream / screenshot uploads.
- Recommendation: Encode each `addr` to a connect code via `connectCodeEncode(ip, buf, ...)` before display. Show only the connect code; if the user wants to copy/share it, the same sentence is what they share. The address textbox should accept connect codes only (which it already does — `connectCodeDecode`).
- Cross-System Changes Required: No (single file, plus the existing `connectCodeEncode` API).
- Blocks Intended Outcome?: Yes (constraint violation).

##### F-Hotswap-Dup — Duplicate hotswap registration of `g_CheatsConfirmUnlockMenuDialog`
- Severity: **Low**.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_warning.cpp:1241-1243` AND `port/fast3d/pdgui_menu_cheats.cpp:1040`.
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: A dialogdef has a single owning renderer.
- Current Behavior: Both files register the same dialogdef. Per `pdgui_hotswap.cpp:147-152`, duplicate registration does last-wins and logs `pdgui_hotswap: Updated '...'`. Order is fixed by `pdgui_menus.h::pdguiMenusRegisterAll` which calls `pdguiMenuWarningRegister` (line 53) BEFORE `pdguiMenuCheatsRegister` (line 62), so the M-3 cheats renderer wins.
- Gap / Failure Mode: Order-dependent. If the registration list is ever reordered, the wrong renderer wins silently. Boot log emits an extra "Updated 'Cheats Confirm Unlock'" line that may confuse triage.
- Why It Matters: The fix is simple — remove the warning.cpp registration. The current code relies on undocumented init ordering.
- Recommendation: Remove the `g_CheatsConfirmUnlockMenuDialog` registration from `pdgui_menu_warning.cpp:1241-1243`. The cheats.cpp registration is the canonical one. Consider adding an assert or a `MENUPOOL_REGISTRATION_OWNER` table that detects double-registration at init time.
- Cross-System Changes Required: No.

#### Medium Priority Findings

##### F-Killfeed-Gating — Killfeed renders during pause / menu / endscreen
- Severity: Medium.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_mpingame.cpp:165-320` (`pdguiMpIngameRender`).
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: Per `context/designs/hud-layer-order.md` §44 row 8: "Killfeed → context gate: GAMEPLAY, not GAMEOVER".
- Current Behavior: `pdguiMpIngameRender` checks `pdguiPauseGetNormMplayerIsRunning()` and `MPPAUSEMODE_GAMEOVER_TICKER`, but does NOT check `pdguiIsActive()` (i.e., whether a menu is open). When the user opens the MP pause menu mid-match, the killfeed continues rendering on top of the pause menu.
- Gap / Failure Mode: Visual clutter overlapping the pause menu's Rankings/Settings tabs. The MP pause menu's own Rankings tab shows the same kill counts.
- Why It Matters: HUD layer policy explicitly gates killfeed on "GAMEPLAY, not GAMEOVER" — the pause-menu state is GAMEPLAY (`normmplayerisrunning` stays true while paused). Compare to the interact prompt (B-189 fix S379) which DOES gate on `pdguiIsActive()`.
- Recommendation: Add a `pdguiIsActive()` early-return at the top of `pdguiMpIngameRender`, mirroring the interact prompt at `pdgui_interact_prompt.cpp:57`. New events still queue (`pdguiKillfeedPush` is unaffected) and the buffer expires naturally; the renderer just doesn't draw while a menu is up.
- Cross-System Changes Required: No.

##### F-Achievement-Toast-Gating — Achievement toasts render over any menu
- Severity: Low.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_achievement_toast.cpp:110-211` (`pdguiAchievementToastRender`).
- Lineage: Authored-in-PD2 (S378).
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: Toasts pop on the endscreen and during gameplay HUD. Should not render over the main menu / settings / lobby.
- Current Behavior: Toast queue is currently only pushed at endscreen entry (`pdgui_menu_endscreen.cpp:443, 881`). Lifetime is 270 frames (~4.5 s @60 Hz). If a toast is queued just before the user mashes through the endscreen and back to the main menu, the toast continues rendering on top of subsequent menus (it draws on the foreground draw list with no gate).
- Gap / Failure Mode: Visual oddity (toast slides over main menu post-match). Not a game-breaking bug, but inconsistent with the HUD layer policy.
- Recommendation: Either (a) gate `pdguiAchievementToastRender` on a "current screen accepts toasts" predicate (e.g., `pdguiPauseGetNormMplayerIsRunning() || pdguiIsActive() && current_menu_is_endscreen`), OR (b) clear the queue on stage transition so any leftover toasts don't carry over. Option (b) is simpler — call `s_NumToasts = 0` on the same stage-transition sites that call `menupoolReleaseAll()`.
- Cross-System Changes Required: No.

##### F-Forge-HUD-Gating — Forge HUD badge / reticle / catalog ghost render over menus
- Severity: Low.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_forge_hud.cpp:82-150` (`pdguiForgeHudRender`).
- Lineage: Authored-in-PD2 (S307 / S313).
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: Forge HUD is a gameplay overlay (mode badge + freefly reticle + ghost preview).
- Current Behavior: Renders if `forgeSessionIsActive()` regardless of menu state.
- Gap / Failure Mode: When the player has Forge active and opens the Esc menu, the THE GRID / FREEFLY badge in the top-left corner stays visible over the menu. Reticle / ghost preview also continue to draw.
- Recommendation: Gate on `pdguiIsActive()` (same as interact prompt). The badge rendering at top-left could optionally remain (it's an inert label; could be informational), but the reticle + ghost should suppress.
- Cross-System Changes Required: No.

##### F-Action-Bar-Enter-Race — `pdguiActionBarButton` Enter activation fires regardless of which window has focus
- Severity: Medium.
- Confidence: Probable.
- Location: `port/fast3d/pdgui_layout.cpp:117-126` (`pdguiActionBarButton`).
- Lineage: Authored-in-PD2 (S368 Batch 0).
- Pillar Impact: None.
- Defect Class: Trust-Boundary (input authority within ImGui).
- Intended Outcome: An action bar button should activate on Enter ONLY when its own focus is set (the caller's `isFocused=1`).
- Current Behavior:
  ```cpp
  bool doActivate = (isFocused != 0) && ImGui::IsKeyPressed(ImGuiKey_Enter, false);
  if (clicked || doActivate) { return 1; }
  ```
  `isFocused` is the file-static visual flag passed by the caller (e.g., `pdguiActionBarButton("Resume", 1, ...)`). It's NOT ImGui's NavId. Enter fires for *every* action bar button on screen with `isFocused=1`, regardless of where ImGui's actual nav focus is.
- Gap / Failure Mode: If the user has focus on a body widget (e.g., editing a text field, scrolling a list) and presses Enter to confirm something local, every action bar with a `isFocused=1` button could fire as well. In practice only one menu is rendered at a time, so this is rarely observed — but the scenario where ImGui's NavWindow is on a popup/textbox and the parent has an action bar with a focused Resume button could double-fire.
- Why It Matters: Couples the visual highlight (caller's hard-coded `isFocused`) and the activation gate. ImGui's IsKeyPressed is global per-frame — it doesn't ask "does my window have focus?".
- Recommendation: Either (a) only treat `isFocused=1` as a *hint* and require ImGui's actual NavId to match, OR (b) gate the activation on the bar's own window/window-focus check (e.g., `ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)`). Likely option (b): Enter only counts when this action bar's parent window has focus.
- Cross-System Changes Required: No.

##### F-Pause-Menu-X — Title-bar X dismisses pause menu without confirm in unsaved scenario
- Severity: Low.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_pausemenu.cpp:816-823`.
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- Defect Class: None.
- Intended Outcome: Per C5, the pause menu's End Game and Restart need confirms. Resume (close and continue) does not need a confirm.
- Current Behavior: `pdguiConsumeTitleClose() || ImGui::IsKeyPressed(ImGuiKey_Escape)` calls `pdguiPauseMenuClose()` (Resume). This is correct — closing the pause menu is non-destructive (the match continues). Good.
- Gap: Just confirming the current state is correct here. The earlier S390 added `endgamePopupWasOpen` gate to prevent the End Game popup's Esc from also closing the pause menu — that fix is verified in place.

#### Low Priority Findings

##### F-Hardcoded-Player-Caps — Local file-defines duplicate MAX_PLAYERS / MAX_TEAMS / MAX_MPCHRS
- Severity: Low (currently consistent with canonical; drift hazard).
- Confidence: Confirmed.
- Location: 
  - `port/fast3d/pdgui_menu_room.cpp:107` — `#define MAX_PLAYERS 8`.
  - `port/fast3d/pdgui_menu_mpingame.cpp:51` — `#define MAX_MPCHRS_TICKER 40`. Team color palette `s_KfTeamColors[8]` at line 110.
  - `port/fast3d/pdgui_hud.cpp:46-47` — `MAX_MPCHRS_HUD 40`, `MAX_TEAMS_HUD 8`.
  - `port/fast3d/pdgui_menu_pausemenu.cpp:89-91` — `MAX_PLAYERS_PM 8`, `MAX_BOTS_PM 32`, `MAX_MPCHRS_PM 40`. Team color array `s_TeamColors[8]` at line 424.
  - `port/fast3d/pdgui_menu_endscreen.cpp` — `ES_MAX_MPCHRS`, `ES_MAX_PLAYERS` (similar mirror).
- Lineage: Authored-in-PD2 (mirrors necessary because C++ TUs can't include `types.h`).
- Pillar Impact: Scaling Ceiling.
- Defect Class: Layout (drift hazard) + Scaling.
- Intended Outcome: PD2's stated philosophy (per skill cache) is "P2P session size scales with host compute and bandwidth" — fixed 8-player ceilings are findings.
- Current Behavior: Each C++ file keeps its own copy of the player-count constants. The team color palette is sized to 8 directly. None of these are derived from `constants.h` (compile-time impossible due to `bool s32` macro).
- Gap / Failure Mode: If `MAX_PLAYERS` or `MAX_BOTS` ever changes in `src/include/constants.h`, all five mirror files silently desync. Local ranking buffers (`struct ranking_pm rankings[MAX_MPCHRS_PM]`) would under/overflow vs. the canonical engine API. Scaling-ceiling-fix attempts would fail half-silently — buffers correct on engine side, wrong on render side.
- Why It Matters: The audit prompt explicitly tags hardcoded fixed-count literals in HUD/scoreboard as findings ("Lobby UI, scoreboard, team-assignment, spectator layouts that visually or structurally assume ≤8").
- Recommendation: (a) Centralize the values in a small C-only header (e.g., `port/include/pdgui_constants.h`) that both C and C++ can include. (b) Add a `static_assert(MAX_PLAYERS_PM == MAX_PLAYERS)` (in C, conditional behind `__cplusplus` guard) wired through `mpchrconfig` to fail the build if the mirror drifts. (c) Replace fixed-sized `s_TeamColors[8]` with a `MAX_TEAMS`-sized static, and switch the sites that index by team into a bounded loop.
- Cross-System Changes Required: Maybe (touches 5 files but the mechanical fix is centralization, not refactoring).

##### F-StaleStructComments — types.h `mpchrconfig` byte-offset comments are stale by ~128 bytes
- Severity: Low (documentation-only; layout currently correct).
- Confidence: Confirmed.
- Location: `src/include/types.h:4017-4036` (`mpchrconfig`), `4038-4067` (`mpplayerconfig`), `4069-4074` (`mpbotconfig`).
- Lineage: Inherited-from-port (legacy struct), modified-in-PD2 (`head_id[64]` and `body_id[64]` inserted).
- Pillar Impact: None directly; supports the layout-fragile mirror code in pdgui_menu_pausemenu.cpp etc.
- Defect Class: Layout.
- Intended Outcome: Byte-offset comments accurately reflect actual field offsets.
- Current Behavior:
  ```c
  struct mpchrconfig {
      /*0x00*/ char name[15];
      char head_id[64];
      char body_id[64];
      /*0x0f*/ u8 mpheadnum;     // ACTUALLY at offset 0x8F
      /*0x10*/ u8 mpbodynum;     // ACTUALLY at offset 0x90
      /*0x11*/ u8 team;          // ACTUALLY at offset 0x91
      /*0x14*/ u32 displayoptions; // ACTUALLY at offset 0x94
      ...
  };
  ```
  Same shift propagates through `mpplayerconfig` (`/*0x44*/ u8 controlmode` is wrong by ~128 bytes) and `mpbotconfig` (`/*0x44*/ u8 unk44[3]` likewise).
- Gap / Failure Mode: The compiler ignores comments, so the actual layout matches the field declaration order regardless. Hazard is in maintainer-eyes-only:
  - A future contributor reading `/*0x14*/ displayoptions` could trust the offset to insert a new field "between 0x13 and 0x14" — and silently break layout.
  - Debug log lines that print "field X at offset Y" using these comments would print misleading values.
  - The mirror struct in `pdgui_menu_pausemenu.cpp:96-116` carries the SAME stale comments AND adds explicit `_pad0[2]` / `_pad1` alignment (correct for compiler auto-pad) — but if anyone "fixes" the stale comments to match the actual offsets and the explicit pads disappear, the layout silently shifts.
- Why It Matters: This is exactly the data-layout-drift class the audit prompt is sensitive to: "**This is the exact class of bug PD2 has already seen** — every Vec3 / Vec4 / matrix declaration must be verified for stride consistency." The mirror code passes today, but the stale documentation is a trap waiting for the next struct edit.
- Recommendation: Audit every byte-offset comment in `src/include/types.h` for fields after the catalog ID string insertions. Either correct them or strip them entirely (compiler-derived layout doesn't need comments). Add a `static_assert(offsetof(mpplayerconfig, controlmode) == EXPECTED)` for the load-bearing offsets to fail the build on future drift. Same pass should hit `pdgui_menu_pausemenu.cpp:96-116` and any other layout mirrors.
- Cross-System Changes Required: No (header + comments only; mechanical sweep).

### Missing / Incomplete Features Blocking Success
- None blocking gameplay. The three `M-`-tagged Tier 1 gaps (M-23-A, M-5-A, M-6-A) are correctness gaps within the design's stated invariants but don't block any feature from working today.
- M-24 (`MENUPOOL_STRICT_TREE` opt-in build flag) is intentionally deferred per the design doc and remains undone — this is the right call until Tier 1-4 has bake time.

### Positive Observations
- **Tier 1 (M-1..M-6) executed cleanly**. Every modal verified in-source uses the canonical S385 pattern (5-frame `SetKeyboardFocusHere(0)`, 3-frame input debounce, `pdguiPopupDarkenBehind(0.65f)`, red destructive Confirm button, `[Enter/Space/(A)] Confirm` / `[Esc/(B)] Cancel` hint footer, default focus on Cancel). The pattern is consistent across `pdgui_menu_warning.cpp::renderMpEndGameDialog`, `pdgui_menu_cheats.cpp::renderCheatsConfirmUnlock`, `pdgui_menu_solomission.cpp::renderAbortMission`, `pdgui_menu_agentselect.cpp` Delete/Copy modal, `pdgui_menu_room.cpp` Leave Room + Scenario Delete modals, `pdgui_menu_pausemenu.cpp` End Match modal.
- **Tier 2 docked action bars** correctly use `pdguiBeginActionBar` / `pdguiActionBarButton` / `pdguiBodyHeightForActionBar` and the `NavFlattened` child flag so D-pad nav crosses transparently between body and bar (S368 invariant).
- **Tier 3 preview-dock invariants** are pinned with `ImGuiWindowFlags_NoScrollbar | NoScrollWithMouse` on every panel that draws absolute-coord previews (training, moddinghub model preview, controldiagram). Correctly identifies the only at-risk site (`##scale_right` in moddinghub).
- **Tier 4 progressive focus**: the `MissionFocusGroup` enum in `solomission.cpp` is the right shape per §6 of the design doc. Tier-aware Escape correctly steps back one focus group.
- **Tier 5 M-22**: pool-owned ctx push for endscreen renderers + Pause menu correctly migrated. The renderer signature change (taking `struct menudialog *dialog` to thread the dialogdef through) is clean.
- **Tier 5 M-23**: `netDisconnect` cascade-close site added. Correct in scope as written; the `wasingame` gate is the gap (Finding M-23-A above) but the rest of the patch is right.
- **Input authority gating** (`gameplayInputSuppressed()` predicate, `actionIsGameplayOnly` classification, dispatch-site skip in `fireVk` lines 437-444) is a thorough belt-and-braces system that covers the menu-active, focus-lost, and focus-regain-settle cases. The classification table is correct: menu nav + USE/CANCEL_USE/PAUSE/SCREENSHOT/CONSOLE_TOGGLE/DEBUG_TOGGLE/CHEAT_ENTER are the right exemptions.
- **Menu pool watchdog** (`menuPoolConsistencyCheck` in `src/game/menu.c:1848`) is a smart leak-class detector that emits WARNINGs and auto-recovers — the kind of late-binding safety net that defends against the same class of bug B-160 was.
- **B-189 fix** (`pdgui_interact_prompt.cpp:57` adding `pdguiIsActive()` gate) is the canonical pattern that the killfeed / achievement toast / forge HUD findings should follow.

---

## 3) Code Quality Review

### Critical Issues
- None within scope (the M-23-A fix is small, M-5-A is single-file).

### High Priority Findings

##### Q-Backend-Event-Order — `pdguiProcessEvent` runs `actionmapDispatch` BEFORE `ImGui_ImplSDL2_ProcessEvent`
- Severity: Medium.
- Confidence: Probable (subtle ordering concern; not directly demonstrated as a bug today).
- Area: Code Quality.
- Location: `port/fast3d/pdgui_backend.cpp:879-902`.
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- What We Found: The flow is: (1) WantCaptureKeyboard gate computed from PRIOR frame's ImGui state, (2) actionmapDispatch fires (writes ActionState), (3) ImGui_ImplSDL2_ProcessEvent forwards event to ImGui (updates current frame's WantCaptureKeyboard). On the FIRST frame a textbox gains focus, ImGui's WantCaptureKeyboard is still false (from prior frame), so a key event leaks into actionmap. The mitigation is the `B-154` gate (`imguiEatsKey` early-return at line 893), but `actionmapDispatch` was already called by then.
- Why It Matters: Primary keypress on a freshly-focused textbox can fire BOTH text input AND its bound action. E.g., typing "E" into a mod-name field on the same frame the field was clicked: actionmapDispatch fires `ACTION_USE` (E key bind) before ImGui registers the focus, which is then routed to the menu IMC under the menu context — generally suppressed because the menu is on top, but the action state pressed=1 lingers for a frame and is observable by `actionPressed(0, ACTION_USE)`.
- Risk If Not Fixed: One-frame bleed-through on textbox focus changes; observable as "first character of typing also accepted dialog Yes button" when a popup modal opens at the same instant.
- Recommendation: Reorder to: (1) ImGui_ImplSDL2_ProcessEvent first, (2) Compute WantCaptureKeyboard, (3) actionmapDispatch only if not eaten. Bonus: drop the explicit `imguiEatsKey` early-return at line 893 since dispatch is now gated upstream.
- Cross-System Changes Required: No.

### Medium Priority Findings

##### Q-FrameCount-Wraparound — `s32 (s_*OpenFrame)` vs `ImGui::GetFrameCount()` wraparound after ~2.27 hours
- Severity: Low.
- Confidence: Confirmed.
- Location: All M-1..M-6 modals — e.g., `pdgui_menu_warning.cpp:731 (ENDGAME_FORCE_FOCUS_FRAMES)`, `pdgui_menu_room.cpp:809 (s_LeaveConfirmOpenFrame)`, etc.
- Lineage: Authored-in-PD2 (S385 pattern).
- Pillar Impact: None.
- Defect Class: None.
- What We Found: `ImGui::GetFrameCount()` returns `int` (32-bit signed). At 60 fps it wraps (signed overflow → UB) at frame `INT32_MAX = 2147483647 ≈ 994 days`, but `framesOpen = curFrame - openFrame` can produce a wrong sign if `openFrame` is set near the end of the session and `curFrame` wraps. ImGui internally treats the frame count as monotonic, so wrap is unlikely in a single session, but a long-running dev session could conceivably hit it. The comparison `framesOpen < N` would break on overflow.
- Why It Matters: Practical risk is near-zero (sessions don't typically run >34 days), but the pattern is fragile.
- Recommendation: Either (a) cast to unsigned for the subtraction, OR (b) use `SDL_GetTicks()` ms-based timing (already used elsewhere). Low priority.

##### Q-Cascade-In-Coroutine — `inputCtxShutdown` calls `menupoolReleaseAll` BEFORE iterating the stack
- Severity: Low.
- Confidence: Confirmed.
- Location: `port/src/inputctx.c:65-89`.
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- Defect Class: None.
- What We Found: The shutdown sequence is correct per the comment (Phase 2: release pool slots first, then iterate stack). But the iteration calls `on_pop` for each remaining context. If a pool slot's release callback indirectly triggers another `inputCtxPush` (recursive open), the new ctx would be missed by the subsequent stack iteration. This is theoretical — no current renderer pushes during pop — but worth noting.
- Recommendation: Add a one-line assertion that `s_Depth == 0` after `menupoolReleaseAll` returns, to catch any future leak.

### Low Priority Findings

##### Q-Hud-Bg-Behind-Menu — `pdguiHudRender` does not gate on `pdguiIsActive()` either
- Severity: Low.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_hud.cpp:174-181`.
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- Defect Class: None.
- What We Found: Score panel + timer render whenever `normmplayerisrunning && !g_MainIsEndscreen`. They render OVER the pause menu (the pause menu is centered; HUD is top-right docked under radar — they don't visually collide). But the ImGui overhead of drawing the panel each frame while the user is paused is wasted work.
- Recommendation: Optional gate on `pdguiIsActive()`. The HUD overlap with pause menu is mostly OK — but the killfeed (Finding F-Killfeed-Gating) is the more visible case.

### Positive Observations
- **`pdguiClearImGuiFocusAndNav` (B-195 fix)** at `pdgui_backend.cpp:933-951` is a precise root-cause fix for the WantCaptureKeyboard leak class. Calls `ImGui::FocusWindow(nullptr)` + `ClearActiveID` on every menu-context pop — exactly the right pair.
- **`menupoolDialogDef` C accessor** in `port/src/menupool.c:379-388` cleanly bridges C++ renderers (which can't include types.h) and the C struct layout. The pattern is a good example of avoiding the layout-drift hazard by exposing a getter rather than a mirror.
- **`actionmapFlushGameplayState` synthetic-released-edge** at `actionmap.cpp:1064-1067` (`st->released = wasHeld ? 1 : st->released;`) correctly preserves an existing released signal when wasHeld is 0, ensuring no consumer sees a "stuck pressed" state across the flush.
- **Stick threshold bookkeeping** (`s_StickHeld[p][i] = 0` on flush at line 1075) prevents spurious release events on the next axis poll. Good attention to a subtle edge.
- **Input-context grace-period key suppression** (`INPUTCTX_PUSH_GRACE_MS = 100` at `inputctx.h:108`) handles the open/close race where the same Esc opens and closes a menu. Correct mitigation for B-124.

---

## 4) Security, Trust & Privacy Review

### Critical Issues
- None within scope. The Server Browser IP exposure (Finding F-IP-Browser above) is the closest, escalated to High in §2.

### High Priority Findings

##### S-Killfeed-Spoof — Killfeed accepts arbitrary attacker/victim names from server with no length/charset validation
- Severity: Medium.
- Confidence: Probable.
- Area: Security.
- Location: `port/fast3d/pdgui_menu_mpingame.cpp:145-159` (`pdguiKillfeedPush` callee) and `port/src/net/netdistrib.c` (caller, out of scope but caller is server-trusted).
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- What We Found: `copyName` at `pdgui_menu_mpingame.cpp:126-134` does length-bounded copy and stops at `\n` or `\0`. The killfeed entry's `attackerName[16]` and `victimName[16]` are bounded, no buffer overflow. However, no charset filtering: a server-side attacker could push names with embedded ANSI escapes, ImGui-meaningful characters (`%s` in printf-style usage), or arbitrary glyphs that disrupt rendering.
- Why It Matters: A malicious dedicated server could flood the killfeed with crafted entries to spoof rivalry / harassment. Names are passed through `ImGui::TextUnformatted` (line 278/297/307) which is `%s`-safe, but display-glyph injection is still possible.
- Risk If Not Fixed: Killfeed used as a chat channel by malicious server operators; visual spam.
- Recommendation: Strip non-printable chars in `copyName`. Optional: cap repeat-frequency from the server side.
- Cross-System Changes Required: Maybe (server/client agreement on charset).

### Medium Priority Findings

##### S-RecentServers-Persistence — `g_NetLastJoinAddr` and `g_NetRecentServers` store raw IPs internally
- Severity: Low.
- Confidence: Confirmed.
- Area: Security.
- Location: `port/fast3d/pdgui_menu_network.cpp:125` (`g_NetLastJoinAddr`).
- Lineage: Authored-in-PD2.
- Pillar Impact: Online-mode boundary.
- What We Found: Per `constraints.md:34`, raw IPs are allowed internally but must never be exposed in UI. The `g_NetLastJoinAddr` is restored into the textbox at line 127 — visible in the UI. (Same defect as F-IP-Browser but for the saved address.)
- Why It Matters: Same constraint violation; smaller surface (only the user's own last join, not a recent-server list).
- Recommendation: Same fix as F-IP-Browser — encode to connect code before placing in the textbox. The textbox should already accept connect codes, so the round-trip is OK.

### Low Priority Findings

##### S-Connect-Code-Validation — Connect code decode in network menu has no length pre-check
- Severity: Low.
- Confidence: Confirmed.
- Location: `port/fast3d/pdgui_menu_network.cpp:303` (`connectCodeDecode(s_JoinAddress, &ip)`).
- Lineage: Authored-in-PD2.
- Pillar Impact: None.
- What We Found: User input is passed straight to `connectCodeDecode`. If `connectCodeDecode` itself doesn't bound its parsing, a long input could overflow internal buffers.
- Why It Matters: Defensive depth; not currently demonstrated as a bug.
- Recommendation: Verify `connectCodeDecode` is length-safe (out of scope to audit here). If not, add a `strlen(s_JoinAddress) <= MAX_REASONABLE_CODE` gate.

### Positive Observations
- The input authority predicate (`gameplayInputSuppressed`) is the right central truth source. Routing all four query API functions (`actionPressed`/`actionHeld`/`actionReleased`/`actionValue`) through it (`actionmap.cpp:1091, 1099, 1107, 1115, 1129`) means a single truth check protects against the "menu open but action fires anyway" class.
- The `actionmapFlushGameplayState` on push to non-gameplay contexts (`inputctx.c:156-158`) prevents the "W held during menu-open continues moving player" class. Paired with the focus-loss flush (`inputctx.c:481-484`).
- `inputCtxShouldSuppressKey` (`inputctx.c:408-431`) handles the same-key-opens-and-closes race window cleanly.

---

## 5) Cross-Cutting Risks & Architecture Concerns

- **Cascade-close gating consistency** (M-23-A): The S388 fix put `menupoolReleaseAll()` behind `wasingame`, but cascade-close should be unconditional on disconnect — the whole menu state is suspect after a network teardown regardless of state. Recommend: hoist the release outside the `wasingame` gate. Apply the same audit to any future "transition site" — the rule should be "every disconnect / stage transition releases the pool" with no carve-outs. → **Fix the gate.**

- **Confirm pattern coverage** (M-5-A, M-6-A, M-6-B, M-Q-A): The M-1..M-6 sweep covered the explicit menu-stack-architecture punch list but missed sibling destructive actions on the same screens — Lobby Disconnect, Solo Pause Restart, Endscreen Disconnect/Quit, Main Menu Quit Game. The doc's C5 invariant ("destructive actions ALWAYS confirm") is broader than the punch list. Recommend: walk every Tier-1/2/3 file with C5 in mind and surface any remaining inline-toggle, no-debounce, or no-modal destructive paths. → **One sweep over the seven files identified above.**

- **HUD layer-policy uniformity** (F-Killfeed-Gating, F-Achievement-Toast-Gating, F-Forge-HUD-Gating): Three HUD overlays render unconditionally over menus while the interact prompt is correctly gated. The `hud-layer-order.md` policy documents the gates but doesn't enforce them in code. Recommend: every renderer in `pdguiRender` after the menu render section should consult `pdguiIsActive()` (or a shared helper like `pdguiIsGameplayHudVisible()`) unless its explicit purpose is to overlay menus (countdown popup, scrim). → **Add `pdguiIsActive()` gates to killfeed / toast / forge HUD; consider a centralized helper.**

- **Action-bar Enter activation race** (F-Action-Bar-Enter-Race): The `pdguiActionBarButton` global Enter check decouples visual focus from activation. With multiple modals stacked or with body-widget focus, false activations are possible. Recommend: gate Enter activation on the bar's parent-window focus. → **Single primitive change in `pdgui_layout.cpp`.**

- **Layout-drift documentation hazard** (F-StaleStructComments + F-Hardcoded-Player-Caps): Multiple C++ HUD files mirror engine structs and constants because of the `bool s32` macro that prevents `types.h` inclusion. The mirrors carry stale offset comments and hardcoded `MAX_PLAYERS=8`. Currently consistent, but every future struct change risks silent drift. Recommend: (a) centralize player/team caps in a C-only `pdgui_constants.h`, (b) sweep every byte-offset comment in `types.h` after the catalog ID string insertion, (c) add `static_assert(offsetof(...) == EXPECTED)` at load-bearing sites. → **One header + one sweep.**

- **Hotswap double-registration** (F-Hotswap-Dup): The cheats unlock dialog is registered twice. Currently order-dependent last-wins. Recommend: remove the duplicate, OR add a hotswap-side warning at second-registration time so future double-registers surface in logs. → **Remove the warning.cpp registration.**

---

## 6) Verification Limits (Static Analysis vs Runtime)

### Confirmed by code evidence
- All M-1..M-6 modals use the canonical S385 pattern (verified file-by-file).
- `g_CheatsConfirmUnlockMenuDialog` is double-registered (grep confirmed).
- `netDisconnect` cascade-close is gated on `wasingame` (line 1075 inspection).
- Server Browser displays raw `addr` as Selectable label (line 218-224 inspection).
- Solo Pause Restart confirm uses `ImGui::Begin` not `BeginPopupModal` (line 2950 inspection).
- Killfeed / achievement toast / forge HUD render functions don't call `pdguiIsActive()` (file-wide grep).
- Mirror `MAX_PLAYERS` / `MAX_MPCHRS` defines exist in 5+ C++ files (grep + read).
- types.h `mpchrconfig` byte-offset comments are stale (visual diff against actual layout).

### Probable issues inferred from patterns
- Q-Backend-Event-Order: actionmapDispatch runs before ImGui_ImplSDL2_ProcessEvent — first-frame textbox focus could leak. Not directly observed at runtime.
- F-Action-Bar-Enter-Race: Global IsKeyPressed(Enter) decoupled from visual focus. The most likely false-fire scenario (multiple stacked action bars in the same frame) is unusual but not blocked by code.
- Q-FrameCount-Wraparound: Theoretical — needs >2 hours of session uptime; not observed.

### Unverified risks requiring runtime testing / network captures / environment access
- Whether the killfeed bleed-through over a paused match causes visible visual collision during gameplay (likely — one playtest needed to confirm severity).
- Whether the achievement toast carries over to the main menu post-endscreen (depends on the timing between toast push and stage transition; needs 1 in-game playtest with achievement unlock).
- Whether the lobby-only disconnect bug (M-23-A) actually blocks Network menu reopen on the next attempt (needs in-game test: connect → lobby → disconnect → re-open Network).
- Whether the action-bar Enter race fires false activations in real play (needs multi-modal test).

### What would be needed to fully validate behavior
- Multi-player test session (≥2 clients on a dedicated server) to verify killfeed / endscreen / disconnect flows.
- Controller (Xbox/DualSense) test rig for every Tier-1 modal — Xbox A on Cancel, B on Cancel, etc.
- Long-running session (≥48 hr uptime) to surface frame-count wraparound (low priority).
- Server-attacker fuzzer to validate killfeed name handling (S-Killfeed-Spoof).

---

## 7) Summary Scorecard

**Design / Gameplay Fit Score**: **8/10** — The M-1..M-23 sweep landed cleanly and the canonical S385 modal pattern is genuinely good (5-frame focus latch + 3-frame debounce + scrim is a robust pattern). The four destructive-action confirms missed (M-5-A, M-6-A, M-6-B, M-Q-A) are sibling sites that would close the C5 invariant fully. The Lobby disconnect cascade gap (M-23-A) is the highest-impact correctness miss.

**Code Quality Score**: **7/10** — Mirror-struct discipline is layout-fragile but currently correct. Action bar primitive has a subtle Enter-race. Hotswap double-registration is a (now-noisy) latent issue. Otherwise the menu/input layer is well-instrumented (DIAG logs, watchdogs, force-close site documentation).

**Security & Trust Score**: **6/10** — Server Browser IP exposure is a direct constraint violation (constraints.md:34). Killfeed name spoofing is a moderate inbound risk. Otherwise trust boundaries are sound (input authority predicate, menu authority via context stack).

**Architectural Discipline Score** (Catalog SOT, mod parity, pillar integrity): **8/10** — Within scope: catalog SOT preserved (no raw filenum reads in menus), mod parity preserved (Modding Hub treats mods identically), online-mode boundary mostly clean (M-23-A is the local exception). HUD layer policy documented but not centrally enforced.

### Total Findings by Severity
- Critical: 0
- High: 5 (M-23-A, M-5-A, M-6-A, M-6-B, F-IP-Browser)
- Medium: 6 (M-Q-A, F-Cheats-Warning, F-Killfeed-Gating, F-Action-Bar-Enter-Race, Q-Backend-Event-Order, S-Killfeed-Spoof)
- Low: 8 (F-Hotswap-Dup, F-Achievement-Toast-Gating, F-Forge-HUD-Gating, F-Pause-Menu-X, F-Hardcoded-Player-Caps, F-StaleStructComments, Q-FrameCount-Wraparound, Q-Cascade-In-Coroutine, Q-Hud-Bg-Behind-Menu, S-RecentServers-Persistence, S-Connect-Code-Validation)

### Total Findings by Confidence
- Confirmed: 16
- Probable: 3 (Q-Backend-Event-Order, F-Action-Bar-Enter-Race, S-Killfeed-Spoof)
- Unverified: 0

### Total Findings by Lineage
- Authored in PD2: 19
- Inherited from port / port-net: 0 (within scope; the legacy `g_Menus[]` plumbing is touched but not the source of any finding)
- Inherited from allinone / n64decomp: 0
- Unknown: 0

### Total Findings by Pillar Impact
- Catalog SOT: 0
- Mod-native Parity: 0
- Modding Pipeline: 0
- Grid Trust: 0
- Online-mode Boundary: 4 (M-23-A, M-5-A, M-6-B, F-IP-Browser, S-RecentServers-Persistence)
- Dedicated-server Boundary: 0
- Scaling Ceiling: 1 (F-Hardcoded-Player-Caps)

### Top Scaling Ceilings Identified
- `pdgui_hud.cpp:46-47`, `pdgui_menu_pausemenu.cpp:89-91`, `pdgui_menu_endscreen.cpp` (ES_MAX_*), `pdgui_menu_mpingame.cpp:51`, `pdgui_menu_room.cpp:107`: hardcoded `MAX_PLAYERS=8` / `MAX_MPCHRS=40` / `MAX_TEAMS=8` mirrored in 5+ render-layer files. **Scaling bottleneck**: UI / scoreboard arrays (would underflow if engine-side caps grow). **Lineage**: PD2-authored mirrors necessary because of the `bool s32` macro in types.h.
- `pdgui_menu_mpingame.cpp:110` `s_KfTeamColors[8]` palette: hard-fixed at 8 teams; if `MAX_TEAMS` ever grew, killfeed colors would underflow.

### Top Data-Layout Integrity Breaches (Phase 2.5)
- `src/include/types.h:4017-4036` (struct `mpchrconfig`): 9+ stale byte-offset comments after the catalog ID string insertion (`head_id[64]` + `body_id[64]`). All offsets after `name[15]` are off by ~128 bytes. **Persisted? No** (struct is in-memory only; not serialized). **Downstream impact**: future maintainer trusting the comments inserts a field at a wrong offset, silently shifting layout for every consumer including the C++ mirror in `pdgui_menu_pausemenu.cpp:96-116`.
- `src/include/types.h:4038-4067` (struct `mpplayerconfig`): same; `/*0x44*/ controlmode` and every subsequent comment are off by the size of the `head_id+body_id` insertion in the embedded `base`.
- `src/include/types.h:4069-4074` (struct `mpbotconfig`): same.
- `port/fast3d/pdgui_menu_pausemenu.cpp:96-116` (mirror of `mpchrconfig` for C++): correctly compensates with explicit `_pad0[2]` / `_pad1` but carries the same stale offset comments. If anyone "corrects" the comments without removing the explicit pads, layout breaks silently.

### Top Catalog / Mod-parity Breakages
- None within scope.

### Top 5 Action Items (Highest Impact First)
1. **Hoist `menupoolReleaseAll()` outside the `wasingame` gate in `netDisconnect()`** — fixes the most impactful menu-reopen-after-disconnect class (M-23-A). One-line edit + an `inputCtxIsActive` guard.
2. **Encode raw IPs to connect codes in the Server Browser display** — fixes the constraint violation (F-IP-Browser) and the matching `g_NetLastJoinAddr` textbox restore (S-RecentServers-Persistence). Single file: `pdgui_menu_network.cpp`.
3. **Apply the canonical S385 modal pattern to the four remaining destructive-action sites** — Lobby Disconnect (M-5-A), Solo Pause Restart (M-6-A), MP Endscreen Disconnect (M-6-B), Main Menu Quit Game (M-Q-A). Mechanically similar; one PR.
4. **Add `pdguiIsActive()` gate to killfeed, achievement toast, and forge HUD renderers** — closes the HUD layer policy gap (F-Killfeed-Gating + F-Achievement-Toast-Gating + F-Forge-HUD-Gating). Three one-line edits matching the B-189 pattern.
5. **Centralize `MAX_PLAYERS / MAX_MPCHRS / MAX_TEAMS` in a C/C++ co-includable header** and sweep `types.h` byte-offset comments after the catalog ID insertion — closes the Phase 2.5 layout-drift hazard (F-Hardcoded-Player-Caps + F-StaleStructComments). One new header + a documentation sweep.

### Must-Fix Before Public Release
- M-23-A (Lobby disconnect cascade-close): fixes a reproducible "menu won't reopen" class.
- F-IP-Browser (Server Browser raw IP display): direct constraint violation; bad-look in any stream.
- M-5-A (Lobby Disconnect confirm): C5 destructive-action invariant.
- M-Q-A (Main Menu Quit Game): every player sees this; the inline toggle pattern is inconsistent with the rest of the menu system.

### Estimated Effort to Remediate Critical / High Issues
- M-23-A: **15 minutes** (move 5 lines outside the `if` block, add active-check guard, rebuild + smoke-test). Single file: `port/src/net/net.c`.
- F-IP-Browser + S-RecentServers-Persistence: **30 minutes** (use existing `connectCodeEncode` API in two display sites). Single file: `port/fast3d/pdgui_menu_network.cpp`.
- M-5-A + M-6-A + M-6-B + M-Q-A: **2-3 hours** for all four sites combined (each is a ~80-line modal block matching the S385 pattern; mechanical copy-paste-adapt). Files: `pdgui_menu_lobby.cpp`, `pdgui_menu_solomission.cpp`, `pdgui_menu_endscreen.cpp`, `pdgui_menu_mainmenu.cpp`.
- F-Killfeed-Gating + F-Achievement-Toast-Gating + F-Forge-HUD-Gating: **15 minutes** each (one-line gate; verify in playtest). Three files.
- F-Cheats-Warning: **15 minutes** to migrate to BeginPopupModal or to delete the renderer.
- F-Hotswap-Dup: **5 minutes** to delete duplicate registration.
- F-Hardcoded-Player-Caps + F-StaleStructComments: **2-4 hours** for the centralization header + comment sweep + a few static_asserts.

**Total estimated effort for Critical / High remediation**: **~6-8 hours** of engineering work + playtest cycles. None of these require cross-system architectural changes.

---

*End of audit report.*
