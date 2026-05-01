# Input + Menu System Audit (Phase 1)

> Status: **AUDIT, no code changes.** Phase 2 fixes ship sequentially after Mike weighs in on order.
> Date: 2026-05-01
> Pillar: joint Menu Stacking + Input System
> Predecessor docs: [input-universality-and-transitions-2026-04-27.md](../designs/input-universality-and-transitions-2026-04-27.md), [menu-stack-architecture.md](../designs/menu-stack-architecture.md), [input-authority-and-menu-pool-2026-04-13.md](../designs/input-authority-and-menu-pool-2026-04-13.md), [hud-layer-order.md](../designs/hud-layer-order.md)
> Related bugs: B-195 (ImGui kbd capture leak, partial fix shipped), B-298 (vehicle bindings unread, OPEN), B-302 (Grid editor visibility, fixed-pending-playtest)

This audit covers eight discrete topics from Mike's directive:

- **A** Per-menu controller accessibility inventory
- **B** Cross-panel navigation
- **C** Settings tabs specifically
- **D** Nested scroll behavior (right-stick under nested scrollboxes)
- **E** Press vs Hold time-threshold mechanics
- **F** Interaction cast geometry, parameters, consumers, visualisation
- **G** Black-box player-health HUD behind menus
- **H** Killfeed popup card width + duplicate OG path

Plus cross-cutting concerns (B-298 / B-195 / B-302 status), and a recommended fix order for Phase 2.

Methodology gates throughout: file:line evidence for every claim, hypothesis framing where the data is incomplete (possibilities, not near-conclusions), no em-dashes, hierarchical log channels reserved (`INPUT.LAYER.*`, `MENU.GRAPH.*`, `HUD.*`).

---

## A. Per-menu controller accessibility inventory

The action-map plus IMC stack plus menu pool from Cohorts 1 to 4 of [input-universality-and-transitions-2026-04-27.md](../designs/input-universality-and-transitions-2026-04-27.md) gives every menu a strong gamepad foundation. `pdgui_backend.cpp::pdguiDriveImGuiNav` ([port/fast3d/pdgui_backend.cpp:485-568](port/fast3d/pdgui_backend.cpp:485)) translates `ACTION_USE / CANCEL_USE / MENU_UP / DOWN / LEFT / RIGHT / TAB_PREV / TAB_NEXT` into `ImGuiKey_Enter / Escape / Arrow* / PageUp / PageDown` injection every frame. With `NavEnableKeyboard` on and `NavEnableGamepad` off (intentional, see backend comment at [pdgui_backend.cpp:367-368](port/fast3d/pdgui_backend.cpp:367)), every `IsKeyPressed(ImGuiKey_Enter / Escape / Arrow* / PageUp / PageDown)` site in a menu is a gamepad check by the same path.

That gives broad coverage by default. Five regression classes sit on top of it.

### A.1 BROKEN

No high-traffic menu is fully unreachable on gamepad: every menu pops or activates via Enter/Escape, both of which gamepad drives. The deepest gap is the **Skin Editor canvas** (described under PARTIAL) where painting is mouse-only.

### A.2 PARTIAL

| File | Site | Gap |
|------|------|-----|
| [pdgui_menu_room.cpp:2023, 2028](port/fast3d/pdgui_menu_room.cpp:2023) | `IsKeyPressed(ImGuiKey_GamepadFaceUp/Left)` | NEVER FIRES because `NavEnableGamepad=OFF`. Bot multi-select (Y) and bot-config context menu (X) only respond to right-click. |
| [pdgui_menu_mpsettings.cpp:868-871, 933-935](port/fast3d/pdgui_menu_mpsettings.cpp:868) | `IsKeyPressed(ImGuiKey_GamepadFaceLeft)` | Same dead path. Track-preview hotkey on row focus is gamepad-unreachable. |
| [pdgui_menu_network.cpp:394](port/fast3d/pdgui_menu_network.cpp:394) | `IsItemHovered() && IsMouseClicked(Right)` | Right-click pastes connect-code from clipboard. No controller path. `ACTION_TEXT_PASTE` (Cohort 1, id 71) exists on `g_ImcTextInput` but the menu does not consume it. |
| [pdgui_menu_agentselect.cpp:327, 337, 347](port/fast3d/pdgui_menu_agentselect.cpp:327) | `IsKeyPressed(ImGuiKey_C / Delete / D)` | Copy / Delete / Default are keyboard-only. Comments at lines 326, 336, 346 advertise gamepad bindings (X / Y / RB) but only the keyboard halves are wired. |
| [pdgui_menu_lobby.cpp:393](port/fast3d/pdgui_menu_lobby.cpp:393) | "Server Chat (coming soon)" stub | No input field, no history. Not a regression in itself but the panel is the natural destination for chat once shipped. |
| [pdgui_skin_editor.cpp:456-558](port/fast3d/pdgui_skin_editor.cpp:456) | Canvas zoom (`MouseWheel`), pan (`IsMouseDragging(Middle)`), paint (`IsMouseDown(Left/Right)`), tool select (`IsKeyPressed('[' / ']' / '1'..'5' / 'G' / 'U' / Ctrl+Z/Y/S`) | Painting workflow is mouse and keyboard only. Surrounding palette / tools panels work on gamepad. **Borderline BROKEN-for-purpose** since painting a skin is the editor's primary purpose. |
| [pdgui_menu_pausemenu.cpp:694-724](port/fast3d/pdgui_menu_pausemenu.cpp:694) | Four `PdPauseButton(...)` calls drive `s_PauseTab` integer with no `BeginTabBar` | The project-wide LB/RB → PageUp/PageDown bumper-cycle convention does not advance these. Buttons are gamepad-activatable; the tab-cycle affordance is missing. |
| [pdgui_menu_theme_editor.cpp:521, 673, 966](port/fast3d/pdgui_menu_theme_editor.cpp:521) | Two-column preview + palette without `NavFlattened`; click-outside dismiss only on mouse | Cross-column traversal is awkward on gamepad. Escape exit at line 945 covers gamepad. |

### A.3 FULL

23 menus are fully gamepad-traversable: `pdgui_menu_mainmenu`, `solomission`, `mpsetup`, `endscreen`, `warning`, `botsetup`, `modmgr`, `moddinghub`, `cheats`, `mpadvanced`, `mppause`, `playerconfig`, `challenges`, `controldiagram`, `stats`, `teamsetup`, `update`, `mpingame`, `agentcreate`, `audiomod`, `training`, `logviewer`, `pdgui_forge_editor`. Each follows the standard pattern: tab bar with bumper cycle (where applicable), `ImGuiChildFlags_NavFlattened` on side-by-side panels, `IsKeyPressed(ImGuiKey_Escape)` dismiss, `IsKeyPressed(ImGuiKey_Enter)` confirm. Forge editor is the most controller-aware single menu in the project, fully self-driven via `ACTION_FORGE_*`.

### A.4 N/A

[pdgui_menu_stack_debug.cpp](port/fast3d/pdgui_menu_stack_debug.cpp) (F9 dev overlay, dev build only). [pdgui_menu_forge.cpp](port/fast3d/pdgui_menu_forge.cpp) (thin shim into the editor).

### A.5 Cross-cutting patterns

1. **GamepadFace dead-key polls.** Three sites poll `ImGuiKey_GamepadFace*` which never fire because `NavEnableGamepad=OFF`: room.cpp lines 2023 / 2028, mpsettings.cpp lines 871 / 935. Defensive `nio.AddKeyEvent(...false)` writes at [mainmenu.cpp:4929, 4935](port/fast3d/pdgui_menu_mainmenu.cpp:4929) and [theme_editor.cpp:1272](port/fast3d/pdgui_menu_theme_editor.cpp:1272) are clearing state, not polling, so they are correct.
2. **Mouse-only paste on text fields.** Only network.cpp:394 today, but connect-code-on-controller is core to online play.
3. **Custom button rows masquerading as tabs.** Pause menu (4 PdPauseButton) and Modding Hub (9 PdButton tools, with bumper cycling explicitly added at [moddinghub.cpp:1332, 1346](port/fast3d/pdgui_menu_moddinghub.cpp:1332) because there is no real BeginTabBar). Pause is the more visible regression.
4. **Single-letter keyboard shortcuts not bound through actionmap.** agentselect (`C / D / Delete`), skin_editor (`[ ] 1 2 3 4 5 G U Ctrl+Z/Y/S`), [pdgui_friends.cpp:828](port/fast3d/pdgui_friends.cpp:828) (`V` for PTT, with explicit comment that V will move to actionmap "once Session B's input scope reopens"). On gamepad none are reachable.

---

## B. Cross-panel navigation

The codebase's standard primitive for cross-panel traversal is `ImGuiChildFlags_NavFlattened` on both panel `BeginChild` calls. When two NavFlattened sibling children sit side-by-side, ImGui treats them as one flat nav region: D-pad / arrow-key Left moves focus into the sibling on the left, Right into the sibling on the right. The migration is largely complete, marked by the "Priority L (2026-04-25)" comment at: solomission.cpp:769, 1066; room.cpp:3548-3562; modmgr.cpp:1189; lobby.cpp:244, 315; pausemenu.cpp:738; moddinghub.cpp:1303, 1437.

| Menu | Panel declarations | Today's traversal | Reach? | Lands sensibly? |
|------|--------------------|-------------------|--------|------------------|
| **Settings** | Tab strip [mainmenu.cpp:4163](port/fast3d/pdgui_menu_mainmenu.cpp:4163); per-tab content `BeginChild ##settings_scroll_*` lines 4182, 4193, 4204, etc. each `NavFlattened`. | LB/RB → `IsKeyPressed(PageUp/PageDown)` lines 4146, 4153. Within tab: standard Up/Down. | YES from any focused widget. | YES. `s_NeedsFocus` + `SetKeyboardFocusHere(0)` at lines 4185, 4196, 4207 lands on first widget. |
| **Settings → Controls** | Outer tab bar `##controls_imc_tabs` [mainmenu.cpp:3125](port/fast3d/pdgui_menu_mainmenu.cpp:3125); inner tab bar `##controls_device_tabs` line 3090. | Outer: bumper cycle. Inner: ImGui's own focus-on-hover. | **PARTIAL** | Inner KB&M / Controller swap requires reaching the inner tab strip with arrow keys; bumpers cycle the outer Settings strip (eat the inner intent). |
| **Bot Setup** | List view → detail/edit child shell. | Selectable + Enter transitions. | YES | YES. |
| **Room** | Tab bar + two columns: tab content + player panel. | LB/RB cycles tabs (lines 3502, 3508). Cross-column via NavFlattened-equivalent migration. | YES for tab cycle, cross-column. **Multi-select / bot context menu broken** (A.2). | YES for cross-column focus; broken for in-row controller actions. |
| **Modding Hub** | 9-tool toolbar + body child. | Bumper cycles tools (lines 1332, 1346). Skin Editor suppresses bumper-out at line 1331. | YES across tools. Skin Editor canvas-only gap (A.2). | YES. |
| **Forge / The Grid** | Sidebar (toggleable X / Tab) + tab area. | LB/RB → `ACTION_FORGE_TAB_PREV/NEXT`. D-pad UP/DOWN navigates rows. | YES (most controller-aware menu). | YES. |
| **Theme Editor** | Preview + palette columns; modal. | Mouse-friendly (click-outside dismiss). NEITHER child uses `NavFlattened`. | **PARTIAL** | OK with arrow key tabbing; awkward without a left/right swap-column shortcut. |
| **Skin Editor** | Canvas + tools panels. | Tools panels gamepad-clean. Canvas not. | **BROKEN-for-purpose** | Canvas unreachable on gamepad. |

---

## C. Settings tabs (highest-traffic multi-panel)

**File**: [pdgui_menu_mainmenu.cpp:4119-4290](port/fast3d/pdgui_menu_mainmenu.cpp:4119) (`renderSettingsView()`).

**Tab declaration**: `ImGui::BeginTabBar("##settings_tabs", tabFlags)` at line 4163, then 8 (or 7 in non-DEV builds) `BeginTabItem` calls at lines 4180 (Video), 4191 (Interface), 4202 (Audio), 4213 (Controls), 4224 (Game), 4235 (Updates), 4247 (Debug DEV-only), 4259 (Catalog).

**Tab switching**: polled via `IsKeyPressed(ImGuiKey_PageUp/PageDown)` at lines 4146, 4153. `pdguiDriveImGuiNav` writes those keys from `actionHeld(0, ACTION_MENU_TAB_PREV/NEXT)` so gamepad LB/RB drive directly. The implementation uses a one-frame `s_BumperPendingTab` flag at lines 4132, 4149, 4156, 4178 so `ImGuiTabItemFlags_SetSelected` is asserted exactly on the bumper-press frame, not held continuously (avoids "fight ImGui's tab click handling"). After switch, `s_NeedsFocus` is asserted (line 4150, 4157) and consumed at lines 4185, 4196, 4207 via `SetKeyboardFocusHere(0)`.

**Within a tab**: top-down list. D-pad UP/DOWN walks widgets cleanly (NavFlattened body child).

**Bumper hint glyph**: lines 4283-4289 render `"<glyph_prev> / <glyph_next> to switch tabs"` calling `pdguiGlyphGetActionLabel(ACTION_MENU_TAB_PREV/NEXT, ...)`. Auto-tracks active device.

**Known issue (A.2 inner-Controls)**: The Settings outer bumper handler runs at line 4146, OUTSIDE `renderSettingsControls()`, so when focus is in the inner KB&M / Controller tab strip, bumpers cycle the OUTER Settings strip. The implicit assumption (comment at lines 3123-3124) is that the user reaches the inner strip with D-pad and activates with Enter. In practice users want LB/RB to swap KB&M and Controller while inside the Controls tab.

---

## D. Nested scroll behavior

Mike's spec: **"Right stick up and down will scroll the deepest scrollbox it is in (if they are nested, and I am on a button inside a scrollbox that is inside another scrollbox, the innermost scrollbox should scroll)."**

### D.1 Today's mechanism is "scroll whatever ImGui's NavWindow points at"

Right-stick scroll injection lives in [pdgui_backend.cpp:528-567](port/fast3d/pdgui_backend.cpp:528) inside `pdguiDriveImGuiNav` (called every frame from `pdguiNewFrame`). Relevant body:

```cpp
ImGuiContext *ctx = ImGui::GetCurrentContext();
if (ctx && ctx->NavWindow && ctx->NavWindow->ScrollMax.y > 0.0f) {
    ImGuiWindow *w = ctx->NavWindow;
    f32 newY = w->Scroll.y + deltaY;
    ...
    ImGui::SetScrollY(w, newY);
}
```

Target window selection is one pointer dereference: `ctx->NavWindow`. No traversal, no walking ancestors, no descending into focused children. Whatever ImGui internally has assigned is what receives the delta. There is **no notion of "deepest" anywhere in the path.**

`NavWindow` is "Focused window for navigation" per [imgui_internal.h:2179](port/fast3d/imgui/imgui_internal.h:2179). It is reassigned in `imgui.cpp` when a `NavMoveRequest` resolves onto a window other than the current one ([imgui.cpp:13330-13334](port/fast3d/imgui/imgui.cpp:13330)) and inside `FocusWindow()` ([imgui.cpp:12270](port/fast3d/imgui/imgui.cpp:12270)).

### D.2 NavFlattened collapses NavWindow to the OUTER root, not the innermost

The dominant pattern in this codebase is `ImGuiChildFlags_NavFlattened` on both children when nesting scrollboxes (Settings tabs, Modding Hub, Mod Manager, Room columns, Level Editor panels). ImGui's own behavior ([imgui.cpp:6930-6934](port/fast3d/imgui/imgui.cpp:6930)) walks `RootWindowForNav` upward across NavFlattened parents, collapsing nested NavFlattened children into a single navigation root.

When the user is in such a NavFlattened child, `g.NavWindow` will, after the initial nav settle, point at the **outer non-flattened root**, not at the inner scroll child. The right-stick path then scrolls the outer root, which usually has no scroll itself, so nothing visible happens.

Concrete chains where this shows up:

- **Settings tabs.** [mainmenu.cpp:5269](port/fast3d/pdgui_menu_mainmenu.cpp:5269) opens `##main_settings_body` with NavFlattened. Per-tab `##settings_scroll_v / i / a / c / g / u / d / cat` (lines 4182, 4193, 4204, 4215, 4226, 4237, 4249, 4271) are also NavFlattened. ImGui collapses both into the parent root, so `NavWindow` points at the top-level main-menu window. The actual scrollable inner does not get the delta.
- **Modding Hub.** Outer `##modhub_inner` (NavFlattened, [moddinghub.cpp:1302](port/fast3d/pdgui_menu_moddinghub.cpp:1302)), inner per-tool body (NavFlattened, line 1435). Plus tools subdivide further (e.g. INI Editor `##ini_list` + `##ini_edit` at lines 500, 538 without NavFlattened).
- **Mod Manager.** `##modmgr_inner` (NavFlattened, [modmgr.cpp:1412](port/fast3d/pdgui_menu_modmgr.cpp:1412)) wraps `##modmgr_list` + `##modmgr_details` (lines 1174, 1188).
- **Room.** `##room_panel_outer` (NavFlattened+Borders, [room.cpp:1716](port/fast3d/pdgui_menu_room.cpp:1716)) wraps `##room_players_list` (no NavFlattened, no border, line 1810). The inner is the scroller.
- **Level Editor / The Grid.** `##le_left` (NavFlattened, [room.cpp:1046](port/fast3d/pdgui_menu_room.cpp:1046)) and `##le_right_outer` (NavFlattened+Borders, line 1202) wrap inner panels including `##le_spawned_list` (line 1212, AlwaysVerticalScrollbar).
- **Theme Editor.** `##theme_preview` (sibling, [theme_editor.cpp:521](port/fast3d/pdgui_menu_theme_editor.cpp:521)); `PaletteScroll` (line 673, true=border, no NavFlattened) is the actual scroller.

In nested layouts where the inner is **not** NavFlattened (Theme Editor `PaletteScroll`, Room `##room_players_list`, Friends `##pd2_chat_history`), the inner can become `NavWindow` once nav settles into it via a nav-move. Otherwise `NavWindow` settles on the outer root and the inner never receives stick scroll.

### D.3 NavWindow lag on NavFlattened windows

[imgui.cpp:13096](port/fast3d/imgui/imgui.cpp:13096) acknowledges:

> `// FIXME-NAV: On _NavFlattened windows, g.NavWindow will only be updated during subsequent frame. Not a problem currently.`

This is a vendored ImGui bug: when nav crosses a NavFlattened border (the dominant pattern here), `NavWindow` trails actual focus by a frame. The right-stick consumer reading `ctx->NavWindow` will at any nav-cross frame see the previously-focused window, not the freshly focused one.

### D.4 Test posture

[tests/test_right_stick_scroll.cpp](tests/test_right_stick_scroll.cpp) is a pure-math spec for `scrollDelta(stick_y, dt_seconds)`. It covers deadzone, monotonicity, sign flip, dt scaling, accumulator. **It does not assert anything about target-window selection, nesting, or NavWindow semantics.** That is the gap Mike's spec calls out.

Two parameter drifts between spec and runtime worth flagging while we're here: spec uses deadzone=0.15 / max=1200 px/s / exponent=1.7 (per-second framing); runtime uses deadzone=0.18 / max=28 px/frame@60Hz (~1680 px/s) / exponent=2.0 (square). Frame-rate independence: spec multiplies by `dt_seconds`; runtime multiplies by a constant `maxPxPerFrame` with no DeltaTime, so high-refresh displays scroll faster than 60Hz. Not a Mike-named bug, but related.

### D.5 Possibility framing: why "innermost" is hard with the current substrate

ImGui does not export a "deepest scrollable parent of a focused widget" query. To resolve "innermost scrollbox containing the focused widget" cleanly, the right-stick consumer would need to walk the focused widget's parent chain, looking for `Window->ScrollMax.y > 0`, and pick the nearest. That parent chain is `g.NavWindow->ParentWindowInBeginStack`. Because of the NavFlattened collapse and the one-frame lag, the chain may not always resolve sensibly mid-navigation.

A pragmatic alternative: maintain a small per-frame stack of "currently scrollable children" pushed/popped around `BeginChild` calls in the menus that need it, and have the right-stick consumer pick the topmost. That is intrusive across ~20 BeginChild sites but makes the behavior deterministic.

The cohort 2 test_right_stick_scroll spec is the natural place to add cases that lock down the nested behavior once chosen. New cases would need a synthetic ImGui-state mock, which the existing pure-C scroll-math test does not provide.

---

## E. Press vs Hold time-threshold

Mike's spec: **"press X, start timer, if it goes over a 'held threshold', it triggers the 'Held X' input, if it is released before that threshold is met, it triggers as Press X. This also means we can update the button held hud element properly."**

### E.1 The state machine matches Mike's spec at the storage layer

[port/include/actionmap.h:251-263](port/include/actionmap.h:251) defines `ActionState` with `down_time_ms`, `up_time_ms`, `hold_consumed`, plus visual-grace fields (`hold_pin_full_until_ms`, `hold_vis_grace_until_ms`, `hold_vis_last_down_progress`). State transitions are in [port/src/actionmap.cpp](port/src/actionmap.cpp):

- **On rising edge** (line 550-564): `held=1; pressed=1; value=1.0f; down_time_ms = SDL_GetTicks(); hold_consumed = 0`.
- **On falling edge** (line 565-573): `held=0; released=1; value=0; up_time_ms = SDL_GetTicks(); hold_vis_grace_until_ms = SDL_GetTicks()+100`. `hold_consumed` is **not** cleared on release.
- **On gameplay flush** (line 1316-1326, the Cohort 4 onCutscenePush hook): all fields zeroed including `hold_consumed=0`.

Query primitives:

- `actionHeldForMs(p, a, threshold_ms)` ([actionmap.cpp:1411-1422](port/src/actionmap.cpp:1411)): returns 1 iff `held && (SDL_GetTicks() - down_time_ms) >= threshold_ms`. Pure timer comparison.
- `actionWasTap(p, a, max_hold_ms)` ([actionmap.cpp:1424-1436](port/src/actionmap.cpp:1424)): returns 1 iff `released && !hold_consumed && (up_time_ms - down_time_ms) < max_hold_ms`. **Fires only on the release frame.** Cannot fire during the hold phase before threshold.
- `actionLastGestureHoldMs(p, a)` (line 1438-1449): returns `up_time_ms - down_time_ms` for the just-completed gesture.
- `actionConsumeHold(p, a)` (line 1451-1461): sets `hold_consumed=1` and pins the hold ring at full for 200ms.
- `actionHoldProgress(p, a, threshold_ms)` (line 1470-1508): three-phase fill (pinned full / live progress / grace decay).

The primitives Mike described **exist and are correct.** A clean (press → start timer → hold past threshold = Hold, release before = Tap) primitive is in the library.

### E.2 actionWasTap has zero callers in the entire codebase

Whole-tree grep for `actionWasTap` returns four hits: declaration ([actionmap.h:369](port/include/actionmap.h:369)), definition ([actionmap.cpp:1424](port/src/actionmap.cpp:1424)), header docstring example (line 356), and an archaeology note. **No production code ever calls it.**

This is the architectural anchor for Mike's intuition. The Tap primitive exists but every consumer rolls its own variant.

### E.3 Each consumer reimplements Tap-vs-Hold locally

| Site | Mechanism |
|------|-----------|
| [bondmove.c:1114-1118](src/game/bondmove.c:1114) | `actionHeldForMs(pi, ACTION_USE, useThreshMs) && !actionHoldConsumed(...)` synthesises legacy A_BUTTON. Canonical hold path. |
| [bondmove.c:1119-1124](src/game/bondmove.c:1119) | `actionHeld(...) && !actionHoldConsumed(...)` ALSO produces A_BUTTON before threshold. So Hold's effect fires on press, then again on long hold; consume only blocks the post-consume frame. |
| [bondmove.c:1130-1137](src/game/bondmove.c:1130) | Tap path: `actionReleased(...)` with no interact prompt synthesises X_BUTTON (legacy reload) iff `actionHoldConsumed(...) || actionLastGestureHoldMs(...) >= 80ms`. Ad-hoc 80ms gate, unrelated to `useThreshMs`. |
| [bondmove.c:2347-2356](src/game/bondmove.c:2347) | Hoverbike-mount: `actionReleased && !actionHoldConsumed && actionLastGestureHoldMs(...) < useThresh`. Yet another Tap variant. |
| [propobj.c:16511](src/game/propobj.c:16511) | `actionHoldConsumed(pi, ACTION_USE)` early-exit in the hoverbike tap-mount handler. |
| [player.c:2967-2978](src/game/player.c:2967) | Cutscene skip uses `actionPressed` (rising-edge) for nine actions including ACTION_SKIP_CUTSCENE. No tap/hold distinction. K.6 of the prior design doc is the rationale. |
| [pausemenu.cpp:1038](port/fast3d/pdgui_menu_pausemenu.cpp:1038) | Scorecard reads `actionHeldForMs(0, ACTION_SCORECARD_HOLD, SCORECARD_BACK_HOLD_MS)` directly. Pure hold, no tap counterpart. |
| [pdgui_forge_editor.cpp:1833-1840](port/fast3d/pdgui_forge_editor.cpp:1833) | Sidebar toggle and tab prev/next use `actionPressed` only. Press-edge events. |
| [pdgui_interact_prompt.cpp:88-122](port/fast3d/pdgui_interact_prompt.cpp:88) | `actionHoldProgress(ap, ACTION_USE, holdMs)` for ring fill, with fallback that recomputes from `actionHoldPressStartMs` when progress is stuck at 0 (lines 92-105), and forces ring to 1.0 while `actionHoldConsumed` is true (lines 106-108). Smoothed via `s_IpHoldRingSmoothed`. **Most disciplined consumer.** |

### E.4 The HUD hold-ring element

[port/fast3d/pdgui_hold_ring.cpp](port/fast3d/pdgui_hold_ring.cpp) (40 lines) is a pure renderer: `pdguiDrawHoldProgressRingAroundBox(dl, x, y, w, h, progress)` takes `progress` in [0,1]. Header [pdgui_hold_ring.h:6](port/include/pdgui_hold_ring.h:6) documents the contract: "actionmap (e.g. actionHoldProgress) or game-normalized values."

The **sole input-driven consumer** is `pdgui_interact_prompt.cpp:122` via `pdguiDrawActionPromptCenteredWithHold` (defined at [pdgui_glyphs.cpp:389](port/fast3d/pdgui_glyphs.cpp:389)). It receives the smoothed `s_IpHoldRingSmoothed` value from `actionHoldProgress(ACTION_USE, holdMs)` plus the down-time fallback plus the consumed override. **This matches `actionHoldProgress` (with smoothing).** Fires/dismisses correctly via the `hold_vis_grace_until_ms` (~100ms tail) plus the smoother's exponential decay.

### E.5 Architectural answer

There IS a coherent press-vs-hold contract at the actionmap layer. There is NOT coherent consumer use. Multiple subtly different definitions of "tap" exist in the codebase: one keyed on `< useThresh`, another keyed on `>= 80ms && (consumed || any hold)`, and `actionWasTap` itself defining a third. They agree in the typical case of a quick tap with no consume call but **diverge at boundaries**: a release exactly at the hold threshold can register as Tap in one consumer and Hold in another, and a hold force-consumed mid-press can produce a synthetic X_BUTTON reload via the `>= 80ms` branch even though `actionWasTap` would have rejected it.

Mike's directive ("update the button held hud element properly") is consistent with making `actionWasTap` the canonical Tap primitive and routing all consumers through it. The press-vs-hold timer architecture that already exists is the load-bearing piece; the migration is consumer-side.

---

## F. Interaction cast

### F.1 Geometry: it is a horizontal cone with vertical Y clamp, not a ray

The "USE-action picker" is a per-prop angle-and-range filter plus an optional LOS check, run every frame. There is **no raycast.**

- **Driver / dispatcher**: [propFindForInteract()](src/game/prop.c:1597) at src/game/prop.c:1597-1630. Called every tick from [lv.c:1547](src/game/lv.c:1547). Iterates `g_Vars.onscreenprops` near to far, dispatches to `objTestForInteract` for `PROPTYPE_OBJ` / `PROPTYPE_WEAPON` and `doorTestForInteract` for `PROPTYPE_DOOR`.
- **Pickup / hoverbike / terminals / weapons**: [objTestForInteract()](src/game/propobj.c:16369) at src/game/propobj.c:16369-16444. **The primary cast Mike feels is "too wide".**
- **Doors**: [doorTestForInteract()](src/game/propobj.c:21085) at src/game/propobj.c:21085-21133 plus geometry helper `func0f08f968` at lines 21006-21072 plus `door0f08f604` at line 20902.
- **Result variable**: `g_InteractProp` (declared [bss.h:97](src/include/bss.h:97), defined [prop.c:52](src/game/prop.c:52)).

### F.2 Object-cast geometry (objTestForInteract)

Origin: `playerprop->pos` (the **player chr feet**, NOT the camera).

1. **Range gate** (per modelnum lookup) [propobj.c:16409-16421](src/game/propobj.c:16409):
   - `MODEL_SK_SHUTTLE`: `range = 500`
   - `MODEL_TAXICAB`: `range = 300`
   - `MODEL_PRESCAPSULE`: `range = 280`
   - `OBJFLAG3_INTERACTSHORTRANGE` flagged: `range = 100`
   - All other props: `range = 200` (typical pickup / hoverbike / terminal)
2. **Cylinder distance test** (line 16423): `x*x + z*z < range*range && y < range && y > -range`. XZ-plane distance plus a vertical Y abs-clamp equal to `range`. The Y clamp is loose (a 200-unit prop allows +/- 200 vertical).
3. **Heading-cone test** (line 16424-16434):
   ```c
   angle = atan2f(x, z) - (360.0f - g_Vars.currentplayer->vv_theta) * M_BADTAU / 360.0f
   ...
   if (angle <= 0.3926365673542f) { ... claim prop ... }
   ```
   `0.3926365673542 rad = 22.5 degrees`. The cone half-angle is exactly pi/8, so the **full cone is 45 degrees wide** centred on the player's facing yaw `vv_theta`.
4. **LOS check** (line 16435-16438): `cdTestLos06(...)` if `OBJFLAG2_INTERACTCHECKLOS`. Doesn't change geometry, only rejects occluded candidates.

The cone is camera-aligned in **yaw only** (`vv_theta` is the camera/look heading), positionally rooted at `playerprop->pos`. It is **NOT pitched.** When the player looks down at a prop on the floor, the cast remains the same horizontal cone; the loose `+/- range` Y abs-clamp is the only vertical permissiveness.

### F.3 Door-cast geometry (different code path)

Per door:
- 200-unit near gate (`xdiff*xdiff + zdiff*zdiff < 40000` with `40000 = 200**2`) and `|ydiff| < 200` ([propobj.c:21101](src/game/propobj.c:21101)).
- Wider net `func0f06797c(&playerprop->pos, 150, door->base.pad)` (line 21104, radius 150 around door pad).
- Angle test computes door's left-edge / right-edge bearings via `door0f08f604` and accepts if both edges within `limit = 0.34901028871536f` rad ([propobj.c:21015](src/game/propobj.c:21015), and 21038-21042). That constant is **20 degrees per side (40 degrees total).** Extra logic for siblings and altcoordsystem.

Mike's "tighten the width" probably wants the **22.5-degree object cone** narrowed; doors are a different selector with different params.

### F.4 Magic-number summary

| What | Constant | File:line | Decimal meaning |
|---|---|---|---|
| Object cone half-angle | `0.3926365673542f` | [propobj.c:16434](src/game/propobj.c:16434) | 22.5 degrees (full 45-degree cone) |
| Object default range | `200` | [propobj.c:16420](src/game/propobj.c:16420) | 200 game units |
| Object short range | `100` | [propobj.c:16418](src/game/propobj.c:16418) | 100 units (`OBJFLAG3_INTERACTSHORTRANGE`) |
| Hoverbike model overrides | `500 / 300 / 280` | [propobj.c:16411-16416](src/game/propobj.c:16411) | per-model range table |
| Door angle limit | `0.34901028871536f` | [propobj.c:21015](src/game/propobj.c:21015) | 20 degrees per side (40 total) |
| Door near-gate radius | `40000` (squared) | [propobj.c:21101](src/game/propobj.c:21101) | 200 units in XZ |
| Door wider radius | `150` | [propobj.c:21104](src/game/propobj.c:21104) | 150 units |

These are pure literals: no constant names, no config / .ini hook, no actionmap-style override.

### F.5 Consumers of g_InteractProp

- [propobj.c:16514](src/game/propobj.c:16514) (`propobjPcHoverbikeTapMountOnUseRelease`): PC tap-mount on USE release path (B-221.3).
- [propobj.c:21017, 21040, 21043, 21065](src/game/propobj.c:21017): door selection helper writes to it.
- [prop.c:1723, 1760, 1793, 1808](src/game/prop.c:1723): `propInteractPromptLabel` / `propInteractPromptHoldThresholdMs` / `propInteractPromptPreferPressStyle` / `propGetActionUseHoldThresholdMs` for HUD pill text and per-target hold-threshold tuning.

The **press-vs-hold gate** for pickup-vs-mount is in [propobj.c:16500-16529](src/game/propobj.c:16500) (`propobjPcHoverbikeTapMountOnUseRelease`), which reads `g_InteractProp` (line 16514), skips if `actionHoldConsumed(pi, ACTION_USE)` (line 16511), mounts via `currentPlayerTryMountHoverbike` (line 16525), or grabs if `OBJFLAG3_GRABBABLE` (line 16528).

### F.6 Existing visualisation: there is none

Searches for `drawInteract`, `drawCapsule`, `drawCone`, `debugDraw*Cyl`, `InteractCast`, `pdguiDebugCast` against `port/fast3d/`, `port/src/`, and `src/game/` returned zero matches outside an unrelated player-init audit. The HUD prompt pill ([pdgui_interact_prompt.cpp](port/fast3d/pdgui_interact_prompt.cpp)) is the only visible affordance and is text-only.

The Debug menu ([port/fast3d/pdgui_debugmenu.cpp](port/fast3d/pdgui_debugmenu.cpp)) renders a single auto-resize ImGui window with five sections (`Perf` / `Network` / `Memory` / `Theme` / `Flags`). The Flags section at [pdgui_debugmenu.cpp:373-391](port/fast3d/pdgui_debugmenu.cpp:373) hosts a single boolean (`g_JumpLoggingEnabled`); it is the natural slot for an `g_InteractCastDebugDraw` toggle alongside it. Renderer is invoked from `pdguiDebugMenuRender(s32 winW, s32 winH)` at line 397.

For a world-space overlay there is **no existing 3D-line / cylinder / cone debug primitive.** The pattern used elsewhere (Forge HUD, etc.) is project-screen-coordinates onto the active camera matrix and use `ImGui::GetForegroundDrawList` to draw lines/triangles in NDC. The forge editor at [pdgui_forge_editor.cpp](port/fast3d/pdgui_forge_editor.cpp) is the closest precedent for "render a world-aware ImGui overlay."

---

## G. Cross-cutting bugs (B-298, B-195, B-302)

### G.1 B-298: vehicle bindings fire but no consumer reads them (OPEN)

[bondbike.c:834-1054](src/game/bondbike.c:834) (`bbikeTick`) does NOT read any `ACTION_VEHICLE_*` action. It reads exclusively `g_Vars.currentplayer->speedforwards / speedsideways / speedtheta` (lines 879-881, 887-890, 894-895). The legacy bondmove channel populates these via `bbikeApplyMoveData` upstream.

[bondbike.c:77-102](src/game/bondbike.c:77) (`bbikeExit`) is a parameterless tear-down. The trigger for bbikeExit is upstream (the existing `bbikeHandleActivate` double-tap window plus `currentPlayerTryMountHoverbike` paths). bbikeExit only clears `OBJHFLAG_MOUNTED` (line 84), applies momentum (line 91), drops `imcVehicleDismount()` (line 101).

Why current actionmap reads cannot deliver: when the vehicle IMC is on top of the stack (pushed by `imcVehicleMount` at [actionmap.cpp:786](port/src/actionmap.cpp:786)), `fireVk` at [actionmap.cpp:491-577](port/src/actionmap.cpp:491) walks contexts highest-priority-first; on `W` it matches `g_ImcVehicle.ACTION_VEHICLE_ACCELERATE` (binding at line 2286) and stops (`goto next_player` at line 575). Lower-priority IMCs are not consulted, so `g_ImcGameplay`'s `W -> ACTION_MOVE_FORWARD` never fires. The WASD synthesis block at lines 1085-1129 reads `s_State[0][ACTION_MOVE_FORWARD/...].held` (lines 1110-1113), all zero. `s_State[0][ACTION_AXIS_MOVE_X/Y].value = 0`. [bondmove.c:1019-1020](src/game/bondmove.c:1019) reads `c1stickx = actionValue(0, ACTION_AXIS_MOVE_X) * 127.0f = 0`, `c1sticky = 0`. `bbikeApplyMoveData` sees `data->canlookahead` true on PC mode at line 264 but `data->analogwalk = data->c1stickysafe = 0`. Bike stops.

`ACTION_PAUSE` works because it is bound in BOTH `g_ImcGameplay` AND `g_ImcVehicle` ([actionmap.cpp:2296-2297](port/src/actionmap.cpp:2296)) so it survives priority shadowing.

**Migration scope.** The full set of `g_Vars.currentplayer->*` legacy fields read inside `bbikeTick` excluding render/audio/animation: `bondprevpos`, `bondbreathing`, `bondvehiclemode`, `speedforwards`, `speedsideways`, `speedtheta`, `bondvehicleoffset`, `bondentert`, `bondentert2`, `bondentermtx`, `bondenterpos`, `bondenteraim`, `prop->pos`, `prop->rooms`, `floorroom`, `vv_verta360`, `headlook`, `headup`, `bond2.unk1c / unk28`, `speedverta`. Of those, only **three are vehicle-input channels** (`speedforwards`, `speedsideways`, `speedtheta`). The rest are pose / animation / timing state owned by the vehicle.

Migration shape: in `bbikeTick`, write `speedforwards / sideways / theta` from `actionValue(0, ACTION_VEHICLE_ACCELERATE / BRAKE / STEER_*)`. Five action reads, three writes. ~10-15 lines including an `actionPressed(0, ACTION_VEHICLE_EXIT)` check feeding `bbikeExit`.

### G.2 B-195: ImGui WantCaptureKeyboard leak (forge architectural fix outstanding)

`pdguiClearImGuiFocusAndNav` lives at [pdgui_backend.cpp:1490-1508](port/fast3d/pdgui_backend.cpp:1490). It calls `ImGui::FocusWindow(nullptr)` and `ImGui::ClearActiveID()`. Header at [pdgui.h:65](port/include/pdgui.h:65).

**The helper has exactly one caller**: [imguiMenuOnPop](port/src/inputctx.c:734-748) at port/src/inputctx.c:746. The on_pop hook of `g_CtxImGuiMenu`. The leak is patched **exclusively for the menu-pop boundary**.

Forge editor pushes **no InputContext.** Searches for `inputCtxPush`, `inputCtxPop`, `g_CtxImGuiMenu`, `g_CtxGameplay`, `g_CtxForge` against [src/game/forgemode.c](src/game/forgemode.c) and [pdgui_forge_editor.cpp](port/fast3d/pdgui_forge_editor.cpp) returned zero matches. Forge entry / exit paths use only **action-map IMCs** (`g_ImcForge` priority 7, `g_ImcForgeSession` priority 6). Those are the InputMappingContext layer (action priority shadowing inside `fireVk`), NOT the InputContext layer (the inputctx.c stack frame that owns event consumption + on_push/on_pop hooks).

[forgeTransitionToFreefly](src/game/forgemode.c:519-564) calls `imcActivate(&g_ImcForge)` at line 563. [forgeTransitionToNormal](src/game/forgemode.c:479-517) calls `imcDeactivate(&g_ImcForge)` at line 516. [forgeTransitionToInactive](src/game/forgemode.c:566-623) calls `imcDeactivate(&g_ImcForge)` and `imcDeactivate(&g_ImcForgeSession)` at lines 608-609. None touch `inputCtxPush` or `pdguiClearImGuiFocusAndNav`.

**Existing g_Ctx* values** at [port/src/inputctx.c](port/src/inputctx.c): `g_CtxGameplay` (line 713, bottom always-active), `g_CtxImGuiMenu` (line 785, shared menu context), `g_CtxPauseMenu` (line 831), `g_CtxDebugOverlay` (line 896). Declared `extern` in [port/include/inputctx.h:212-226](port/include/inputctx.h:212). **No `g_CtxForgeEditor` exists.**

Forge wants menu-style cursor + ImGui keyboard claim BUT also wants the freefly camera (sticks) live. A literal "push g_CtxImGuiMenu" would suppress gameplay axes (the predicate `gameplayInputSuppressed` is used by `fireVk` at [actionmap.cpp:511](port/src/actionmap.cpp:511) to skip both `g_ImcGameplay` and `g_ImcVehicle`), which forge does not want. A dedicated `g_CtxForgeEditor` whose on_push activates the cursor and clears nav-but-keeps-axes is the architecturally clean answer for the full B-195 fix.

### G.3 B-302: Grid editor visibility gate (FIXED-PENDING-PLAYTEST)

[pdgui_forge_editor.cpp:1904-1907](port/fast3d/pdgui_forge_editor.cpp:1904) gates `pdguiForgeEditorRender` on `forgeSessionIsActive()` and `forgeIsFreefly()` BEFORE the function calls `forgeCoreInit()`, `forgeSidebarHandleInput()`, or the outer `ImGui::Begin("The Grid -- Editor", ...)` (line 1934). `forgeSidebarDraw()` at lines 1865-1902 retains its own narrower `if (!s_SidebarVisible) return;` (line 1867); that one only governs whether the in-editor sidebar Child renders, not whether the editor window itself appears. There is also a defence-in-depth gate at line 2033. Fix is in place.

---

## H. Issue G: black-box player-health HUD behind menus

### H.1 Architecture: there is only ONE health HUD render path

The health HUD has only one render path: the legacy GBI [healthbarDraw()](src/game/healthbar.c:133) at src/game/healthbar.c:133. It is called from [playerRenderHealthBar()](src/game/player.c:3795) at src/game/player.c:3795. There is **no ImGui health bar.** [pdgui_hud.cpp](port/fast3d/pdgui_hud.cpp) handles only the score panel and timer (file header lines 1-12).

`playerRenderHealthBar` is invoked from two GBI-pass call sites:

1. [player.c:5750](src/game/player.c:5750) inside `playerRenderHud`, the gameplay HUD pass, called from [lvRender](src/game/lv.c:1600) at lv.c:1600.
2. [menu.c:5645](src/game/menu.c:5645) inside `menuRender`, but only when a legacy menu BG is active and not eyespy.

Render order at the engine level (per [gfx_pc.cpp:2928-2942](port/fast3d/gfx_pc.cpp:2928) and `port/fast3d/CLAUDE.md`): GBI scene + GBI HUD -> `end_frame` -> GL reset -> `pdguiNewFrame` + `pdguiRender` (ImGui) -> swap. **Legacy healthbar always renders BEFORE ImGui.**

### H.2 What healthbarDraw actually emits

[healthbarDraw](src/game/healthbar.c:194-619) at src/game/healthbar.c:194 builds vertex+colour geometry (three coloured arcs for shield, armour, trauma) then issues:

- `gDPSetRenderMode(G_RM_AA_XLU_SURF, G_RM_AA_XLU_SURF2)` (line 537) -- alpha blending ON.
- `gDPSetCombineMode(G_CC_SHADE, G_CC_SHADE)` (line 538) -- **shade only, no texture.**

Plus an "underbox" rectangle at line 534 via `text0f153a34(gdl, ..., undercol)` where `undercol = 0x00000000` (line 192, alpha 0). The healthbar itself is **purely vertex-coloured triangles**. No sprite atlas, no masked texture, no asset id.

### H.3 The black overlays inside playerRenderHud

There is a viewport-spanning translucent black rect drawn from inside `playerRenderHud` itself, gated on `g_Vars.currentplayer->mpmenuon`:

[player.c:5736-5745](src/game/player.c:5736):
```c
if (... && g_Vars.currentplayer->mpmenuon) {
    ...
    gdl = text0f153a34(gdl, a, b, c, d, 0x000000a0);
    ...
}
```

A second, structurally identical rect at [player.c:6045-6054](src/game/player.c:6045). `0x000000a0` = RGBA black with alpha = 160/255 (~63%). When this fires, the entire viewport is painted ~63% black; the healthbar then draws on top.

In this branch `mpmenuon` is **only ever set to false** ([playermgr.c:571](src/game/playermgr.c:571)). Reads (no writes): [bondgun.c:13926](src/game/bondgun.c:13926), [radar.c:278](src/game/radar.c:278), [pdgui_bridge.c:568](port/fast3d/pdgui_bridge.c:568), player.c:5736, 6045. **The OG legacy "darken-screen-when-MP-pause-menu-open" trigger is dead code in this branch**, but the GBI emitter still runs. If any code path ever re-enables `mpmenuon`, the entire screen turns 63% black.

### H.4 The legacy menu BG path

[menuRender](src/game/menu.c:5717) at src/game/menu.c:5717 is fired from [lvRender](src/game/lv.c:1391-1398) when `g_Vars.currentplayer->menuisactive`. `menuisactive` is set in [menutick.c:822-841](src/game/menutick.c:822) whenever any of: `nextbg != 255`, `bg != 0`, `unk5d5_05/unk5d4 != 0`, a `curdialog`, or `bannernum != -1`.

When `menuRender` fires, the legacy BG draws first via [menuRenderBackgroundLayer1](src/game/menu.c:5399) at src/game/menu.c:5399. For `MENUBG_BLACK` / `MENUBG_8` (lines 5416-5425):

```c
gdl = textSetPrimColour(gdl, colour);   // colour = 255 * frac (full opaque)
gDPFillRectangle(gdl++, 0, 0, viGetWidth(), viGetHeight());
```

**Solid opaque black covering the entire screen.** The legacy healthbar then draws on top of it at [menu.c:5645](src/game/menu.c:5645). This is the OG-intended "menu over black panel" look. The healthbar renders correctly with its alpha; the BLACK BEHIND IT is the menu BG itself.

### H.5 Lane verdicts

| Lane | Verdict | Evidence |
|------|---------|----------|
| 1. Z-order / draw-order | **PRIMARY HYPOTHESIS for "in menus"** | The healthbar renders on top of an intentional opaque-black menu BG layer (`menuRenderBackgroundLayer1` for `MENUBG_BLACK`), and on top of (potentially) a translucent black overlay from `playerRenderHud:5743` if `mpmenuon` is ever true. The "black box" Mike sees is the **layer beneath** the healthbar, not the healthbar itself. Render order is GBI-bg -> GBI-healthbar -> ImGui-menu, all stacking correctly per design, but the design itself paints black behind the healthbar. |
| 2. Texture load failure | **ELIMINATED** | The healthbar uses `G_CC_SHADE` (vertex-colour combine). No texture, no asset id, no sprite. Source: `healthbar.c:537-538`. |
| 3. Alpha state | **ELIMINATED for the curved geometry** | `G_RM_AA_XLU_SURF` (`healthbar.c:537`) enables alpha blending; vertex alpha goes through. Underbox uses `G_RM_XLU_SURF` + `G_CC_PRIMITIVE` with alpha 0 (transparent regardless). No alpha-test/blend issue. |
| 4. ImGui frame state | **ELIMINATED** | ImGui runs strictly after `gfx_rapi->end_frame()` and `gfx_opengl_reset_for_overlay` ([gfx_pc.cpp:2928-2942](port/fast3d/gfx_pc.cpp:2928)). ImGui cannot mutate the GBI pass output for the same frame. ImGui renders ON TOP of the GBI HUD via OpenGL state isolated by the reset call. |
| 5. Init failure | **ELIMINATED** | The healthbar has no init / asset; only per-frame vertex/colour allocation. Static colour overrides are baked literals at `healthbar.c:178-192`. |

### H.6 Expected fix shape

The "black behind health" is layered geometry working as designed. Either:

(a) Gate the OG menu-BG path so `MENUBG_BLACK / MENUBG_8` does not fire when an ImGui menu owns the chrome (the `imgui-menus-replace-legacy` policy). The legacy BG was OG console output; ImGui menus draw their own backgrounds and do not need a legacy BG underneath.

(b) Gate the `mpmenuon` overlay path ([player.c:5736-5745, 6045-6054](src/game/player.c:5736)) more tightly, since `mpmenuon` is dead code in this branch but the GBI emitter still runs every frame.

Either fix removes the perceived "opaque black where there should be alpha." The healthbar itself is correct; the surface under it is the bug.

---

## I. Issue H: killfeed popup card width + duplicate OG path

### I.1 Width is fixed at 340 px

Killfeed renderer is `pdguiMpIngameRender` in [pdgui_menu_mpingame.cpp:164-320](port/fast3d/pdgui_menu_mpingame.cpp:164). It is one ImGui window (`##killfeed_textbox`) at lower-left, newest-on-top. The window size is **fixed-width**:

- [pdgui_menu_mpingame.cpp:207](port/fast3d/pdgui_menu_mpingame.cpp:207): `const float boxW = pdguiScale(340.0f);`
- Line 235: height is dynamic from active-entry count: `const float boxH = padY * 2.0f + lineH * (float)activeCount;`
- Lines 239-240: `SetNextWindowPos` + `SetNextWindowSize` with `ImGuiCond_Always`.

Each row is `ImGui::TextUnformatted(attackerName)` + 4px spacer + "killed" + spacer + victim name. Width is whatever the row text would be, but the host window forces 340px.

**To auto-fit content**: per active entry, `ImGui::CalcTextSize(attackerName)` + `pdguiScale(4)` + `CalcTextSize("killed")` + `pdguiScale(4)` + `CalcTextSize(victimName)` (for non-suicide) plus `padX * 2` window padding; take max across visible rows; clamp to a sane min/max. Then pass that as `boxW` to `SetNextWindowSize`. The font scale at lines 260-261 (`fontScale = fontSize / ImGui::GetFontSize()`) means `CalcTextSize` needs the same scale applied (`CalcTextSize(text) * fontScale` or push the font scale before measuring).

### I.2 Duplicate OG killfeed = legacy hudmsg path

There is **no separate OG killfeed widget**. The duplicate is the legacy [hudmsg.c](src/game/hudmsg.c) notification path. [mpstats.c](src/game/mpstats.c) emits BOTH:

**Legacy hudmsg** via `hudmsgCreate(text, HUDMSGTYPE_DEFAULT)`:
- [mpstats.c:176](src/game/mpstats.c:176): "Kill count: %d" (gated `g_Vars.normmplayerisrunning`).
- [mpstats.c:243](src/game/mpstats.c:243): "Died once" / "Died %d times" (gated `g_Vars.normmplayerisrunning`).
- [mpstats.c:265](src/game/mpstats.c:265): "Suicide count: %d" (gated `g_Vars.normmplayerisrunning`).
- [mpstats.c:389](src/game/mpstats.c:389): "Killed by %s" when victim is local player (gated `g_Vars.normmplayerisrunning && aplayernum >= 0`).

**ImGui killfeed** via `pdguiKillfeedPush(...)`:
- [mpstats.c:348](src/game/mpstats.c:348): suicide event (gated `g_Vars.mplayerisrunning && vmpchr`).
- [mpstats.c:419](src/game/mpstats.c:419): normal kill (gated `g_Vars.mplayerisrunning && vmpchr`).

Both fire today. `g_Vars.normmplayerisrunning` and `g_Vars.mplayerisrunning` overlap during a normal MP match; the ImGui path is gated on the broader `mplayerisrunning` (also fires in co-op / anti-op), and the legacy path on `normmplayerisrunning` (normal MP only).

`hudmsgsRender` is invoked from the legacy GBI HUD pass ([player.c:5662, 6056](src/game/player.c:5662) and three death-path locations 6408 / 6410 / 6451). With `HUDMSGALIGN_LEFT` + `HUDMSGALIGN_BOTTOM` (default `g_HudmsgTypes[0]` at [hudmsg.c:55](src/game/hudmsg.c:55)), they appear lower-left, overlapping the ImGui killfeed area.

### I.3 Disable points for the OG path

The cleanest disable: four contiguous calls at mpstats.c:176, 243, 265, 389. Removing or guarding these `hudmsgCreate(... HUDMSGTYPE_DEFAULT)` calls eliminates the legacy popups without touching the ImGui killfeed (same call sites, a few lines below, gated on the broader `mplayerisrunning`). One condition wrapping each call (e.g., `if (!pdguiKillfeedActive())` or unconditional removal) is sufficient.

The legacy `hudmsg.c` infrastructure should NOT be wholesale removed: `hudmsgsRender` is still used for non-killfeed messages (mission timer, "Weapon load failed" [player.c:1915](src/game/player.c:1915), eyespy text, "Critical mission personnel killed" calls in [src/setups/setup*.c](src/setups), "One minute left" [lv.c:2485](src/game/lv.c:2485), body-load fallback [body.c:190](src/game/body.c:190)). Only the four kill / death / suicide-counter `hudmsgCreate` calls in mpstats.c need to go.

---

## Cross-cutting findings (root causes shared across multiple gaps)

1. **NavWindow as the right-stick scroll target** ties Topic D to all the menus that nest scrollboxes (Settings tabs, Modding Hub, Mod Manager, Room, Level Editor). Same root cause; same fix mechanism if we change "scroll the NavWindow" to "scroll the innermost scrollable parent of the focused widget."

2. **Per-consumer Tap reimplementation** (Topic E.5) means E and F are coupled: tightening the interaction cast (F) does not affect the press-vs-hold gate at [propobj.c:16500](src/game/propobj.c:16500), but a Tap-canonicalisation pass would touch every consumer including the hoverbike-mount path where actionLastGestureHoldMs is read.

3. **No InputContext for forge** (G.2) is the architectural root of B-195 residual leak class. Forge is the only "non-menu" surface that legitimately wants menu-style cursor + ImGui keyboard claim. The cleanest answer is a new `g_CtxForgeEditor` context, which becomes a building block for any future "3D editor with overlay" surface.

4. **Legacy GBI HUD paths still fire in PC menu mode** (Topics H and I.2 share this). The OG `mpmenuon` overlay, the OG `MENUBG_BLACK` legacy BG, and the OG hudmsg killfeed entries are all instances of the same pattern: legacy console-era HUD code that still emits when ImGui menus own the chrome. The `imgui-menus-replace-legacy` policy from prior cohorts applies here too.

5. **GamepadFace dead-key polls** (A.5 pattern 1) and **mouse-only paste** (A.5 pattern 2) and **single-letter keyboard shortcuts not in actionmap** (A.5 pattern 4) are the same root: site-local input handling that bypasses the action map. Cohort 7 of the input-universality design doc names them as the migration sweep.

---

## Recommended Phase 2 fix order

Mike's directive provided an order recommendation; this audit does not see dependencies that change it. Recommended sequence below; each fix is a separate merge to dev per the standing rule.

| # | Topic | Priority | Why this order | Touches |
|---|-------|----------|----------------|---------|
| 1 | **E. Press vs Hold time-threshold** | High | Unblocks the button-held HUD element; canonicalises a contract that already half-exists; precondition for clean tap-vs-hold debug overlay in #6. | Each consumer site listed in E.3 routes through `actionWasTap`. The HUD ring already uses `actionHoldProgress` correctly. |
| 2 | **D. Right-stick = innermost scrollbox** | High | High-frequency Mike pain (every nested-scroll menu). Test cohort exists; extending it covers the new behavior. | `pdgui_backend.cpp:528-567` rework + new test cases in `tests/test_right_stick_scroll.cpp` for the "innermost scrollable parent" walker. |
| 3 | **B + C. Cross-panel + Settings inner-Controls** | Medium | Settings inner-Controls bumper override (C.5) is the most concrete cross-panel gap; theme editor cross-column shortcut is polish. | `pdgui_menu_mainmenu.cpp` Settings tab handler scoped per-tab; `pdgui_menu_theme_editor.cpp` palette / preview NavFlattened. |
| 4 | **A. Per-menu controller accessibility sweep** | Medium | Includes B-298 wire-up (G.1) since it is the canonical "vehicle bindings dead" gap, and the GamepadFace dead-key polls (A.5 #1) plus mouse-only paste (A.5 #2). Skin Editor canvas is the deepest gap and might be its own sub-task. | room.cpp (multi-select / context menu), mpsettings.cpp (preview), network.cpp (paste), agentselect.cpp (C/D/Delete), pausemenu.cpp (real tabs), bondbike.c (B-298). |
| 5 | **F. Tighten interaction cast width** | Medium | Numerical change to `0.3926365673542f` in propobj.c:16434 (object cone half-angle). Default narrower (e.g. 17 degrees -> half-angle `0.296706f` for a 34-degree cone). #6's debug overlay is prerequisite for empirical tuning. | `src/game/propobj.c` magic-number replacement (or hoist to a named const + Settings hook). |
| 6 | **F. Toggleable visible interaction cast** | Medium | Lets Mike empirically dial #5. Add `g_InteractCastDebugDraw` boolean to Settings -> Debug Flags section ([pdgui_debugmenu.cpp:373-391](port/fast3d/pdgui_debugmenu.cpp:373)); add a world-space cone overlay primitive in port/fast3d. | New cone/cylinder draw helper, debug menu boolean, hook in `propFindForInteract` or `lvRender`. |
| 7 | **H. Issue G: black-box HUD behind menus** | Medium | Mike's directive places this after #1 + #2. Fix shape from H.6: gate `MENUBG_BLACK` / `MENUBG_8` and `mpmenuon` overlay paths so they do not fire under ImGui menus. | `src/game/menu.c::menuRenderBackgroundLayer1` gate; `src/game/player.c:5736 + 6045` gate. |
| 8 | **I. Issue H: killfeed width + OG duplicate** | Low (any time after audit) | Width: `CalcTextSize`-driven per-row + max + clamp. OG duplicate: remove or guard the four `hudmsgCreate` calls in mpstats.c (I.3). | `port/fast3d/pdgui_menu_mpingame.cpp:207, 235, 239-240`; `src/game/mpstats.c:176, 243, 265, 389`. |
| 9 | **G.2: B-195 full architectural fix** | Low (architectural) | New `g_CtxForgeEditor` InputContext that activates cursor and clears nav-but-keeps-axes on push. Larger refactor. | `port/src/inputctx.c`, `port/include/inputctx.h`, `src/game/forgemode.c` push/pop sites. |

Each fix lands its own pd-tests cases alongside the invariant it enforces (per the methodology gate). Build via `devtools\build-session.ps1` (queued tool) only, per the standing rule.

---

## Open decisions for Mike

These items are architecturally significant and should land before code:

1. **F.5 default cone tightening.** This audit recommends going from 22.5 degrees half-angle to ~17 degrees (34-degree full cone). Mike may want a different number; the debug toggle in #6 lets the answer be empirical.
2. **D.5 scrollbox walker design.** Two viable approaches: ParentWindowInBeginStack walk vs. explicit per-frame scroll-target stack maintained by BeginChild call sites. Walker is non-intrusive but inherits the NavFlattened lag; explicit stack is intrusive but deterministic.
3. **G.2 g_CtxForgeEditor scope.** New context with cursor-on + axes-live, or extend `g_CtxImGuiMenu` with a "freefly variant" flag. Default proposal: new context.
4. **Skin Editor canvas controller story (A.2).** Canvas painting on gamepad is a significant feature, not a small fix. Could deliberately stay mouse-only in v0.1.0; or get an explicit "D-pad cursor" mode under the canvas controller story (Cohort 6 menu-graph work touches some of this).
5. **I.3 OG hudmsg removal vs guard.** Simple removal is cleaner; guard via a "is ImGui killfeed active" predicate is more defensive but adds a runtime branch. Default proposal: simple removal since the policy is `imgui-menus-replace-legacy`.

Phase 2 begins after Mike weighs in. Methodology gates and stop conditions remain in effect.
