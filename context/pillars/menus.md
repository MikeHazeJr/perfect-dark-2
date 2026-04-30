# Menus / UI / UX

> ImGui is the sole menu system. Three architectural layers: input ownership stack, menu pool (structural dedup by type), menu graph (named edges). Layout primitives + nav helpers + theme system. Stack debug overlay for live introspection.

---

## What it is

The menu system is built entirely on Dear ImGui v1.91.8. The legacy N64 `menuPush` / `menuPop` dialog stack is retained as plumbing only; ImGui menus push dialogdefs into the legacy stack, the hotswap interceptor at render time routes everything to ImGui. Zero native rendering paths remain (P10 D5.7 complete, S184, 2026-04-08 per [constraints.md](../constraints.md)).

Code:

- 31 `pdgui_menu_*.cpp` files at [port/fast3d/](../../port/fast3d/) (one retired `.cpp.retired`).
- Menu pool: [port/include/menupool.h](../../port/include/menupool.h), [port/src/menupool.c](../../port/src/menupool.c) (758 lines).
- Menu graph: [port/include/menugraph.h](../../port/include/menugraph.h), [port/src/menugraph.c](../../port/src/menugraph.c) (578 lines).
- Layout primitives: [port/include/pdgui_layout.h](../../port/include/pdgui_layout.h), `pdgui_widgets.h`, `pdgui_nav.h`.
- Theme system: [port/fast3d/pdgui_theme.cpp](../../port/fast3d/pdgui_theme.cpp), [port/fast3d/pdgui_menu_theme_editor.cpp](../../port/fast3d/pdgui_menu_theme_editor.cpp).
- Stack debug: [port/fast3d/pdgui_menu_stack_debug.cpp](../../port/fast3d/pdgui_menu_stack_debug.cpp).

---

## Three architectural layers

The menu system resolved a three-source-of-truth problem documented at [port/include/menupool.h:33-39](../../port/include/menupool.h:33). Prior state had three competing trackers: legacy dialog stack + per-renderer `s_FooPushedCtx` booleans + input ctx stack. The pool resolved all three.

### Layer 1: Input ownership stack

Owned by [port/src/inputctx.c](../../port/src/inputctx.c). 16-slot pushdown stack of input contexts (`g_CtxGameplay`, `g_CtxImGuiMenu`, `g_CtxPauseMenu`, `g_CtxDebugOverlay`). See [pillars/input.md](input.md) for the full input authority story.

### Layer 2: Menu pool

`s_Pool[MENU_TYPE_COUNT]` keyed by `menu_type_t` ([port/include/menupool.h:70-148](../../port/include/menupool.h:70)). 60+ menu types currently registered (MAIN_MENU, SOLO_MISSION, MP_SETUP, ENDSCREEN_*, etc.). Each pool slot tracks active state, optional dialogdef pointer, optional ctx ownership.

Structural dedup at [menupool.c:255-277](../../port/src/menupool.c:255): a menu push for a type already active is denied without mutating `g_Menus[]`. `nextsibling` auto-open loop consults `menupoolIsDialogActive(sibling)` and skips siblings whose type is already active elsewhere.

Per-slot ctx push/pop pairing means `menupoolReleaseAll()` ([menupool.c:479-514](../../port/src/menupool.c:479)) atomically tears down all menus AND their input contexts on stage transitions. Force-close sites (endscreen exits, match start, stage handlers, `inputCtxShutdown` nuclear reset) call `menupoolReleaseAll`.

Migration is complete: every `.cpp` once owning `s_*PushedCtx` now has the removal comment, e.g. [pdgui_menu_agentselect.cpp:202](../../port/fast3d/pdgui_menu_agentselect.cpp:202), [pdgui_menu_cheats.cpp:201](../../port/fast3d/pdgui_menu_cheats.cpp:201), [pdgui_menu_mpadvanced.cpp:355](../../port/fast3d/pdgui_menu_mpadvanced.cpp:355).

### Layer 3: Menu graph

Named edges between menu types. [port/include/menugraph.h](../../port/include/menugraph.h) declares node types, edge types, `menuGraphNode`, `menuGraphEdge`, fire helpers (`menuGraphFirePushDialog`, `menuGraphFirePopOp`, `menuGraphFireSwitchSibling`, `menuGraphFireNetworkOp`, `menuGraphFireSceneOp`, `menuGraphFireProcessOp`, `menuGraphFireLocalOp`).

Migration is partial: `menuGraphFire*` appears in 11 files (62 usages); raw `menuPushDialog / menuPopDialog` appears in 15 files (127 usages). The validated transition graph is aspirational; not enforced by a completeness test.

---

## Layout primitives

[port/include/pdgui_layout.h](../../port/include/pdgui_layout.h) defines body / action-bar split with a diagram. Used helpers:

- `pdguiBeginActionBar / pdguiEndActionBar / pdguiActionBarButton` - 19 of 31 menu files.
- `pdguiPopupDarkenBehind` - standardized scrim for modals.
- Confirm-modal primitive at lines 196-222 - canonical 5-frame force-focus + 3-frame debounce pattern.
- `pdguiSetCursorBelowTitle / pdguiThemeGetContentInset / pdguiThemeApplyContentInset` - chrome-aware content inset (S297).

Modal focus pattern at [pdgui_layout.cpp:307-315](../../port/fast3d/pdgui_layout.cpp:307) `renderConfirmModal`: `SetKeyboardFocusHere(0)` for ENDGAME_FORCE_FOCUS_FRAMES (5) frames + `SetItemDefaultFocus`. Same pattern in MP End Game dialog at [pdgui_menu_warning.cpp:853-870](../../port/fast3d/pdgui_menu_warning.cpp:853).

---

## Widget helpers

[port/include/pdgui_widgets.h:5-8](../../port/include/pdgui_widgets.h:5) documents the label-left spec. `pdguiCheckbox / Combo / SliderInt / SliderFloat / InputText` used 78 times across 12 files.

19 menus still call `ImGui::Checkbox / Combo / SliderInt` directly with the right-side label default, violating the label-left spec. Static guard candidate in `test_menu_graph.cpp` style.

---

## Nav helpers

[port/include/pdgui_nav.h:42-64](../../port/include/pdgui_nav.h:42) exposes `pdguiMenuAcceptPressed`, `pdguiMenuCancelPressed`, `pdguiMenuTabPrev / Next`, `pdguiMenuListUp / Down`, `pdguiMenuSecondary / Tertiary / Delete`. All delegate to actionmap.

**No raw `ImGui::IsKeyPressed(ImGuiKey_*)` in any `pdgui_menu_*.cpp`.** [tests/test_menu_graph.cpp:66-83](../../tests/test_menu_graph.cpp:66) enforces this statically. 27 of 31 menu files use the helpers.

PageUp/PageDown bound as action-map defaults for `ACTION_MENU_TAB_PREV / NEXT` so keyboard tab cycling is preserved behind action-map authority. Q/E added as additional defaults (S574 era) for solo Options tab shortcuts.

---

## Theme system

[port/fast3d/pdgui_theme.cpp](../../port/fast3d/pdgui_theme.cpp) decodes ROM textures to RGBA32, uploads as GL textures. 24-element working palette with bundle support (palette + chrome style + font in one activation, S305 Theme Bundling).

Components:

- **Theme** = persistent visual identity (user-chosen, draws first).
- **Tint** = transient overlay (composites on top of theme, cleared on pop).
- **Menu Style** (formerly Nine-Slice) - chrome borders / corners.
- **Title Bar Style** - 5 procedural styles (Classic / Solid / Vertical Bars / Scanlines / Diagonal Stripes), persisted via `Video.UiTitleBarStyle`.
- **Font** - imported via Font Mod system, persisted via `Video.FontId`.

Theme Editor at [pdgui_menu_theme_editor.cpp](../../port/fast3d/pdgui_menu_theme_editor.cpp) supports grouped sections + per-row tooltips + auto-derive, plus Menu Style + Font bundle dropdowns.

UI texture mod overrides (S351): `pdguiThemeScanModUiTextures` / `pdguiThemeApplyEnabledModUiTextures` allow enabled mods to override `"type": "ui"` catalog textures at runtime.

---

## Stack debug overlay

[port/fast3d/pdgui_menu_stack_debug.cpp](../../port/fast3d/pdgui_menu_stack_debug.cpp) renders the live menu pool, legacy dialog chain, input context stack, and ctx-history without capturing input (`ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav` at lines 40-41). Bound on a debug hotkey for live introspection.

---

## Active invariants

Per [constraints.md](../constraints.md):

- **ImGui is the sole menu system.** P10 D5.7 complete S184. All menu work targets the ImGui layer.
- **Menu pool is the structural dedup layer.** Every dialog push consults the pool after the F-3.1 pointer-scan; denial is structural. Several dialogdefs may map to the same type (PC + Pause main menu variants -> `MENU_TYPE_MAIN_MENU`; arena/scenario/weapons/limits -> `MENU_TYPE_MP_SETUP`).
- **Mouse capture driven by input context stack.** No menu may call `SDL_SetRelativeMouseMode` or `SDL_ShowCursor` directly.
- **Room-settings mutations broadcast via end-of-frame dirty flag.** Leader-side mutations to shared room state set a file-static dirty flag; flush via `netSendRoomSettingsUpdate` / `netSendRoomPlaylistUpdate` at end-of-frame, gated on `g_NetMode == MPSETTINGS_NETMODE_CLIENT && lobbyIsLocalLeader()`. Do not broadcast inline (packet storm).

---

## What is done (per [audits/infrastructure-pillars-status-2026-04-27.md](../audits/infrastructure-pillars-status-2026-04-27.md) Section 3)

- 31 ImGui menu files cover every player-facing flow.
- Menu pool resolves the three-source-of-truth problem (legacy stack + s_PushedCtx + input ctx).
- Layout primitives shared across 19 of 31 files (action bar) and 12 of 31 files (widgets).
- Modal focus extracted to canonical primitive (`renderConfirmModal`).
- Theme system with bundle support, mod-supplied themes, mod UI texture overrides.
- Stack debug overlay for live introspection.
- Nav helpers eliminate raw `ImGui::IsKeyPressed` from menus (statically enforced).
- Menu graph substrate in place; 11 files migrated; pop / push / network-op / scene-op / process-op / sibling-switch / local-op helpers all available.
- Lobby portrait baking (S352) + hover preview / drop shadow / team border (S356).
- Theme editor + mod scanning + chrome style + font mod system (S280-S306 era).

---

## What is in flight

- **Menu graph completion.** 127 raw `menuPushDialog / menuPopDialog` calls in 15 files still bypass the graph. Migration is a Cohort 5-8 lane (per the input universality framework). No test asserts graph completeness yet.
- **Three deferred MENUITEM types.** [pdgui_menu_warning.cpp:1247-1258](../../port/fast3d/pdgui_menu_warning.cpp:1247) explicitly DEFERRED: `MENUITEMTYPE_LIST` (MP Pause Inventory, MP Character body/head, MP Load Settings/Preset/Player), `MENUITEMTYPE_PLAYERSTATS` (MP Pause Player Stats), `MENUITEMTYPE_RANKING` (MP Pause Player Ranking, MP Pause Team Rankings). Fallback at lines 622-629 outputs `[label]` placeholder text. The in-match inventory and ranking screens are placeholders.
- **Action bar adoption.** Used in 19 of 31 files. Remaining 12 (`solomission`, `training`, `mpsettings` partially, `mpadvanced`, `challenges`, `logviewer`, `audiomod`, `stats`, `theme_editor`, `modmgr`, `endscreen`, `mpsetup` partially) place CTAs in scroll body. UX inconsistency.
- **Widget helper adoption.** Used in 12 of 31 files. 19 menus still use raw ImGui widgets with default label-right.

---

## Known gaps

- **Menu graph node table not visible from header.** [port/include/menugraph.h](../../port/include/menugraph.h) declares `menuGraphNode`, `menuGraphEdge`, `menuGraphNodeCount` but the table is in `.c`. `test_menu_graph.cpp` checks no-raw-key-polling but not graph completeness (every node has at least one inbound and outbound edge).
- **Right-stick scroll spec decoupled from runtime.** Same gap as input system. [tests/test_right_stick_scroll.cpp:14-17](../../tests/test_right_stick_scroll.cpp:14) is the spec; runtime is `pdguiDriveImGuiNav` with no static link.
- **Single-slot unregistered fallback.** [menupool.c:370-393](../../port/src/menupool.c:370) tracks one unregistered dialog at a time via `s_UnregisteredOwnedDef / s_UnregisteredOwnedCtx`. Two concurrent unregistered dialogs would silently leak the first's ctx. Comment acknowledges design choice; still a real gap if mod dialogs register late.
- **`pdgui_menu_audiomod.cpp` is absent from primitive counts.** Did not appear in action bar, widget helper, or nav helper grep results. May be a stub or fully manual layout. Audit and migrate.

---

## Tests

Coverage at `tests/`: `test_menu_stack` (9 cases), `test_menu_reachability` (6 synthetic trees), `test_menu_graph` (static source guard), `test_right_stick_scroll` (math spec), `menupool_pure.c` (pure mirror).

---

## Active design references

- [designs/menus/menu-stack-architecture.md](../designs/menus/menu-stack-architecture.md) - target spec, 2026-04-19. Codifies invariants for the ImGui menu system.
- [designs/menus/flat-menu-navigation.md](../designs/menus/flat-menu-navigation.md) - system rule: focus traverses across panel containers transparently.
- [designs/menus/menu-inventory.md](../designs/menus/menu-inventory.md) - 120 screens roster.
- [designs/menus/pdgui-hold-ring.md](../designs/menus/pdgui-hold-ring.md) - per-target use-hold tuning + shared hold-progress ring API.
- [designs/menus/activemenu-radial-architecture.md](../designs/menus/activemenu-radial-architecture.md) - weapon/gadget radial = legacy GBI active menu, not ImGui.
- [designs/menus/hud-layer-order.md](../designs/menus/hud-layer-order.md) - HUD render ordering + context-aware gating.
- [designs/input/menu-controller-input-constraints.md](../designs/input/menu-controller-input-constraints.md) - controller + mouse UX contract for ImGui menus.

---

## Where to look

- For input authority and IMC stack: [pillars/input.md](input.md).
- For mod-supplied themes / chrome / fonts / UI textures: [pillars/modding.md](modding.md).
- For the catalog ID resolution behind menu screens: [pillars/catalog.md](catalog.md).
- For room / lobby / multiplayer menu flow: [pillars/connectivity.md](connectivity.md) + [pillars/server.md](server.md).
- For HUD rendering: [pillars/rendering.md](rendering.md).
