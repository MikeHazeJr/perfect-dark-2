# Playtest Fixes — 2026-04-11 (Opus 1M session)

**Branch**: `claude/infallible-napier` (worktree)
**Base**: dev @ `bfbb19b9` (Batch 8 + Batch 6 head-preview polish merged)
**Log evidence**: Mike's 2026-04-11 playtest of v0.0.77 in Carrington Institute / main menu / Settings.

Six issues fixed together. The first four are UI/input bugs Mike hit in CI free-roam; the last two are feature gaps in the menu bumper and theme systems. Fixes are independent except for Issues 3/4 which share a root cause.

---

## Issue 1 — Double-menu / instant reclose on reopen after back-out (B-131)

### Symptom
After backing out of the main menu in Carrington Institute, the next open-press (Start / Escape / B) would open the menu and then immediately close it on the very next frame. User had to press a second time (400 ms later in the log) for the menu to stay open.

### Log excerpt
```
[01:49.42] MENU_IMGUI: main menu CLOSE via ESC/B (top-level -> CI free-roam)
[01:49.42] INPUTCTX: 'imgui_menu' marked for deferred removal
...
[01:51.26] MENU: ACTION_PAUSE detected ... lvframenum=3794
[01:51.27] INPUTCTX: syncMouseMode -- restored relative mode for gameplay
[01:51.28] ACTIONMAP: activated 'menu' (priority 10, depth 2)
[01:51.28] MENU_IMGUI: main menu OPEN
[01:51.29] MENU_IMGUI: main menu CLOSE via ESC/B (top-level -> CI free-roam)  <-- bug
[01:51.29] INPUTCTX: 'imgui_menu' marked for deferred removal
...
[01:51.71] MENU: ACTION_PAUSE detected                                        <-- 420ms later
[01:51.72] MENU_IMGUI: main menu OPEN                                         <-- stays open
```

### Root cause (diagnosis)
The close handler in `renderMainMenu()` (pdgui_menu_mainmenu.cpp) is guarded by `!ImGui::IsWindowAppearing()` — it only runs on the SECOND frame onward, to avoid processing the opening keypress as a close.

Two things break this guard:
1. **Grace period only covers SDL_KEYDOWN**: `inputCtxShouldSuppressKey()` in inputctx.c returns 1 only for `SDL_KEYDOWN` events within the grace window. `SDL_CONTROLLERBUTTONDOWN` (the gamepad B / Start button) is NOT grace-guarded, so the opening gamepad press reaches ImGui's event queue unconditionally.
2. **ImGui event queue timing slop**: pdgui_backend.cpp's `ImGui_ImplSDL2_ProcessEvent(ev)` queues the key event, and `ImGui::NewFrame()` consumes the queue and updates the IsKeyPressed edge. Depending on the order of poll → dispatch → NewFrame relative to `IsWindowAppearing`, the edge can land on frame N+1 instead of frame N, slipping past the `!IsWindowAppearing` guard.

### Fix
`port/fast3d/pdgui_menu_mainmenu.cpp`

Two-layer belt-and-braces:

1. **Timestamp grace guard**: new file-static `s_MainMenuOpenedTick` stamped via `SDL_GetTicks()` in the `IsWindowAppearing()` block. The ESC/B close handler now also checks `(now - s_MainMenuOpenedTick) >= MAIN_MENU_CLOSE_GRACE_MS` (150 ms) before running. Matches the `inputCtxShouldSuppressKey` 100 ms grace pattern, with a 50 ms cushion to cover ImGui's queue slop.

2. **Clear the ImGui key edge on appearing**: also in the `IsWindowAppearing()` block, explicitly call `ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false)` and `AddKeyEvent(ImGuiKey_GamepadFaceRight, false)` to force the down-state off. Covers the edge case where ImGui has the opening press latched but hasn't yet processed a release — the next frame sees prev=false cur=false instead of prev=true cur=false, so IsKeyPressed is impossible to report.

The 150 ms window is imperceptible to a user (~9 frames at 60 Hz, well under human reaction time for intentional menu-close).

### Related
- Supersedes / closes B-131 (OPEN since S198).
- The existing B-124 `push_tick` keyboard suppression in `inputctx.c` remains in place — the two guards complement each other.

---

## Issue 2 — Saved bindings populate in UI but don't apply to live input

### Symptom
Binds saved to pd.ini appear correctly in the Settings → Controls rebind UI, but the bound keys don't fire the corresponding actions during gameplay / in menus.

### Log evidence
```
ACTIONMAP: binds loaded from pd.ini into 6 IMCs   (at boot)
ACTIONMAP: binds loaded from pd.ini into 6 IMCs   (at 00:14.09)
ACTIONMAP: binds loaded from pd.ini into 6 IMCs   (at 01:30.36, after opening Settings again)
```

### Root cause (diagnosis)
`buildBindStr()` and `actionmapLoadBinds()` in `port/src/actionmap.cpp` had mismatched first-match semantics:

- **Save** (`buildBindStr`): iterates `s_AllImcs[]` (ordered [gameplay, vehicle, menu, pause_menu, debug_overlay, text_input]), writes triggers from the FIRST IMC that has `has_mapping[action]`, and `break;`s out. Shared actions like `ACTION_USE` / `ACTION_CANCEL_USE` / `ACTION_PAUSE` write from `g_ImcGameplay`.

- **Load** (`actionmapLoadBinds`): iterates `s_AllImcs[]` the same way, but calls `parseBindStr()` on EVERY IMC that has `has_mapping[a]` set. No `break;`.

Effect: on every load (boot + each time Settings → Controls is opened), shared actions' bind strings — which contain gameplay's triggers — get written into menu / pause_menu / text_input / debug_overlay too. The menu IMC's `ACTION_USE` (originally `Return` / `A`) gets overwritten with gameplay's `F` / `Y`. Pressing `A` in a menu no longer fires `ACTION_USE` because menu IMC's `ACTION_USE` is now bound to `F` / `Y` instead.

Why Mike saw "populate but not apply": the Settings rebind UI reads directly from `g_ImcGameplay.mappings[]` (line 864 `getBindsByType` reads from `&g_ImcGameplay.mappings[action]`), so the UI shows the correct gameplay triggers. Gameplay trigger dispatch works because `fireVk()` walks active IMCs by priority; during gameplay g_ImcGameplay is the only active IMC with these bindings, so the loaded values are reachable. But the menu-side shadow of those actions gets clobbered, breaking menu interaction for any keys shared across IMCs — a quieter failure Mike experienced as "bindings seem to populate properly, but not apply to the actual input being applied".

### Fix
`port/src/actionmap.cpp`

Added `break;` in `actionmapLoadBinds()` after `parseBindStr()` succeeds on an IMC. The load path now matches `buildBindStr()`'s first-match-wins semantics exactly. Menu / pause_menu / text_input / debug_overlay retain their compile-time defaults for shared actions.

Verified by trace: shared action `ACTION_USE` → save writes gameplay [F, Y] → load applies to gameplay only → menu keeps compile-time [Return, A]. Menu-only action `ACTION_MENU_UP` → save writes menu [UP, DPAD_UP] → load applies to menu (first-match) only. No cross-contamination.

### One-line diff
```diff
for (s32 ci = 0; ci < s_NumAllImcs; ci++) {
    if (s_AllImcs[ci]->has_mapping[a]) {
        parseBindStr(s_AllImcs[ci], p, (InputAction)a, s_BindStr[p][a]);
+       break; /* first IMC wins — matches buildBindStr */
    }
}
```

Full comment in the source explains why.

---

## Issue 3 — Theme Customizer window won't close (X / Close / click-out)

### Symptom
B-130. Title-bar X button, explicit Close button, and click-outside dismiss all do nothing. Editor stays on screen until the process exits.

### Log evidence
```
[01:39.15] Theme editor: opened
...
[01:54.94] PDGUI theme: shutdown   <-- process exit, no close log ever fires
```

None of the four S198 instrumentation log lines (Begin collapsed, X-after-End, Close button, InvisibleButton dismiss) fire. All three exit paths are simultaneously broken.

## Issue 4 — Color picker popup no longer works in Theme Customizer

### Symptom
Clicking a color swatch in the Theme Customizer either never opens the picker or opens it then immediately dismisses it.

### Shared root cause (Issues 3 and 4)

The S197a theme editor layout had **two** ImGui windows drawn per frame with a hand-rolled focus/z-order hack:

1. `##theme_editor_overlay` — full-screen InvisibleButton for click-outside, drawn first, with `ImGui::SetNextWindowFocus()` to force it to the top of the focus stack.
2. `Theme Editor##P5` — the editor content, drawn second, also with `ImGui::SetNextWindowFocus()` so it stays visually above the overlay.

Both windows re-claim focus every frame. This thrashes ImGui's focus/z-order tracking:

- **For Issue 3 (close paths)**: title-bar X button hit-tests require the window to be the actual `HoveredWindow` in ImGui's internal state. The per-frame focus thrash periodically strips that status from the editor just long enough for the click to fall into a no-op hit test. Same story for the overlay's InvisibleButton — clicks outside the editor land in a period where the overlay isn't marked as the hovered window, so the button never fires. Begin's `&open` is wired but can't fire without a click-registration path.

- **For Issue 4 (color picker)**: `ImGui::ColorEdit4()` opens its color picker via `OpenPopup()`. The new popup needs stable focus ordering to stay visible — it has to believe it's on top of the stack. Our per-frame overlay `SetNextWindowFocus()` steals that status on the very next frame, and the popup auto-closes (default ImGui behaviour: a popup that loses focus to a window claiming to be above it dismisses itself).

Both symptoms trace to the same fundamental problem: the overlay-plus-editor-plus-focus-hack pattern is fighting ImGui's built-in popup/modal system, which does all of this correctly when you just let it.

### Fix
`port/fast3d/pdgui_menu_theme_editor.cpp`

Full rewrite of the rendering path to use native ImGui modal popups:

1. `pdguiThemeEditorRender()` now simply forwards to `renderThemeEditor()`. No more custom overlay window, no more `SetNextWindowFocus()` calls.

2. `renderThemeEditor()` now:
   - Opens a named popup via `ImGui::OpenPopup("Theme Editor##Modal")` on first visible frame (idempotent — `IsPopupOpen` guards re-entry).
   - Centers the popup via `SetNextWindowPos(..., ImGuiCond_Appearing)`.
   - Uses `ImGui::BeginPopupModal(..., &open, flags)` — the modal backdrop dims the background automatically and blocks clicks to windows behind.
   - X button: `&open` pointer goes false when clicked, handled after `EndPopup()`.
   - Close button: direct `pdguiThemeEditorHide()` call + `wantClose` flag.
   - Click-outside detection: rect-test against window bounds, suppressed by `IsAnyItemActive() || IsAnyItemHovered()` so clicks inside the nested color picker don't incorrectly dismiss the modal.
   - Escape: ImGui's built-in modal behaviour closes on Escape automatically (via the &open pointer).

All three exit paths restored. Nested popups (color picker) work because modal popups stack with sub-popups in the conventional ImGui way — no focus thrash to fight.

### Verification plan
- Open Theme Customizer from Settings → Debug.
- Click X button → should close + log `"Theme editor: exit — title-bar X button (!open after EndPopup)"`.
- Click Close button → should close + log `"Theme editor: exit — Close button"`.
- Click any color swatch → picker should open and stay open.
- Click inside the picker to choose a colour → palette updates live.
- Click outside the modal rect → should close + log `"Theme editor: exit — click outside modal rect"`.
- Press Escape → should close (ImGui default modal behaviour, no explicit log).

---

## Issue 5 — Main menu controller bumpers (LB/RB) skip items within tab

### Symptom
In the Main Menu and Settings, gamepad LB/RB bumpers move selection up/down WITHIN the current tab instead of switching between top-level tabs.

### Root cause (diagnosis)
Two bumper handlers in `port/fast3d/pdgui_menu_mainmenu.cpp`:

- `renderSettingsView()` at line ~2166: `if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) { ... }`
- `renderMainMenu()` at line ~2424: `if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) { ... }`

Both checks use `ImGuiKey_GamepadL1` / `ImGuiKey_GamepadR1`. BUT:

- `pdgui_backend.cpp:211` sets `io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard` but NOT `NavEnableGamepad`. Per the comment block at line 296–302, this was an intentional B-124b fix: gamepad events are translated to ImGui KEYBOARD nav keys via `pdguiDriveImGuiNav()` (since "ImGui ignores all Gamepad* keys when that flag is off").

- `pdguiDriveImGuiNav()` at line 324–325 translates `ACTION_MENU_TAB_PREV` / `ACTION_MENU_TAB_NEXT` to `ImGuiKey_PageUp` / `ImGuiKey_PageDown` — NOT `ImGuiKey_GamepadL1` / `R1`.

So when the user pressed LB, the bumper check polled a key (`ImGuiKey_GamepadL1`) that nothing was injecting, and the actual injected key (`ImGuiKey_PageUp`) fell through to ImGui's default keyboard nav behaviour — which for `PgUp` inside a window with an item list is "move focus up by a page" (`ImGuiNavInput_KeyUp_` semantics). Net effect: bumpers moved focus within the tab instead of switching tabs, exactly matching Mike's report.

### Fix
`port/fast3d/pdgui_menu_mainmenu.cpp`

Swap `ImGuiKey_GamepadL1` → `ImGuiKey_PageUp` and `ImGuiKey_GamepadR1` → `ImGuiKey_PageDown` in both `renderSettingsView()` and `renderMainMenu()`'s top-level sub-view cycle.

This also gives keyboard users the ability to press PgUp/PgDn to switch tabs — a reasonable side benefit that matches the injected key's semantic.

The underlying action map binding (`addBind(imc, ACTION_MENU_TAB_PREV, JOY_BTN(0, JBTN_LB))` at `actionmap.cpp:1449`) is unchanged — the chain is gamepad LB → `ACTION_MENU_TAB_PREV` → `ImGuiKey_PageUp` → our `IsKeyPressed(PageUp)` check → tab cycle.

---

## Issue 6 — Saved custom themes don't populate in Theme selection

### Symptom
The Theme Customizer's "Save as Mod" button writes `mods/<slug>/theme.json` + `mods/<slug>/mod.json` to disk successfully, but the new theme never appears in:
- The Theme Customizer's "Load Theme" dropdown
- The Settings → Debug "UI Theme" selector grid

Only the 7 built-in themes are ever shown.

### Root cause (diagnosis)
Two gaps in `port/fast3d/pdgui_theme_loader.cpp`:

1. **No mod theme scan at init**: `pdguiThemeLoaderInit()` registers exactly 7 built-ins via `add_entry(k_BuiltinIds[i], ...)` inside a `for (int i = 0; i < 7; i++)` loop, then logs `"init — %d built-in themes registered"`. The comment at line 924 explicitly says "Theme JSON files from mods are discovered by the mod manager; here we just handle the built-in set. Mod themes register via `pdguiThemeLoadFromFile()` called from mod loading." But `pdguiThemeLoadFromFile()` only LOADS a theme (applies it to the live palette) — it never calls `add_entry`, so there's no path for mod themes to land in the registry.

2. **Hardcoded UI Theme selector**: `renderSettingsDebug()` in `port/fast3d/pdgui_menu_mainmenu.cpp` uses a fixed `s_ThemeNames[7]` array and a `for (int i = 0; i < PDGUI_NUM_THEMES; i++)` loop that calls `pdguiThemePaletteIndexToId(i)`. Even if the registry contained mod themes, this grid would never render them because it never iterates the registry at all.

### Fix
Three-piece fix across three files:

**`port/fast3d/pdgui_theme_loader.cpp`**
- New `scan_mods_for_themes()` function using `<dirent.h>` + `<sys/stat.h>`. Walks `mods/`, for each subdirectory checks if `theme.json` exists, extracts the display `"name"` field via a minimal string scan (fallback = slug with first-letter capitalised and `-` / `_` → space), and calls `add_entry("mod:<slug>", name, theme_path, /*palette_index*/ -1)`.
- `register_mod_theme_dir()` helper shared between init scan and runtime registration. Idempotent via `find_entry()` check.
- `pdguiThemeLoaderInit()` calls `scan_mods_for_themes()` after the 7 built-in registration loop.
- New public API `pdguiThemeRegisterModDir(slug, filepath)` that wraps `register_mod_theme_dir()` so the theme editor can register a newly saved theme immediately after `saveThemeAsMod()` succeeds — no restart required.

**`port/include/pdgui_theme_loader.h`**
- Added declaration for `pdguiThemeRegisterModDir()`.

**`port/fast3d/pdgui_menu_theme_editor.cpp`**
- `saveThemeAsMod()` now calls `pdguiThemeRegisterModDir(dirName, themeJsonPath)` on success so the newly written theme appears in the registry without a restart. The Theme Customizer's existing `pdguiThemeGetCount()`-driven "Load Theme" dropdown picks it up automatically.

**`port/fast3d/pdgui_menu_mainmenu.cpp`**
- `renderSettingsDebug()` theme selector grid rewritten to iterate `pdguiThemeGetCount()` / `pdguiThemeGetId()` / `pdguiThemeGetName()` instead of the hardcoded `s_ThemeNames[7]` loop.
- Built-in themes retain their pre-tinted `s_ThemeAccentColors[]` / `s_ThemeTextColors[]` styling via `pdguiThemeIdToPaletteIndex()` lookup.
- Mod themes use a neutral purple/gold tint (`k_ModAccent` / `k_ModText`) to visually distinguish them from the shipped set.
- A "Custom (from mods/)" header with spacing is injected once, right before the first mod-theme button, via `!isBuiltin && !modHeaderShown` check — cleanly groups built-ins and mods without requiring two separate loops.
- Row-break logic suppresses `SameLine()` right before the header so the mod section always starts on a new row.

### Verification plan
- Open Theme Customizer → save a new theme as "My Test Theme".
- Check log for `"PDGUI theme loader: registered mod theme 'mod:my-test-theme' (\"My Test Theme\") from mods/my-test-theme/theme.json"`.
- Close the editor, re-open it → "Load Theme" dropdown should show "My Test Theme (mod:my-test-theme)" alongside the built-ins.
- Navigate to Settings → Debug → UI Theme section.
- Should see the 7 built-in tinted buttons, a "Custom (from mods/)" label, and the mod theme button in the neutral purple tint.
- Click the mod theme button → should apply it + persist to pd.ini (via `pdguiThemeLoadFromCatalog("mod:my-test-theme")`).
- Restart the game → mod theme should still be in the registry (via `scan_mods_for_themes` at init) and the Settings selector.

---

## Files touched

| File | Change |
|------|--------|
| `port/fast3d/pdgui_menu_mainmenu.cpp` | Issue 1: 150 ms grace guard + ImGui key clear on appearing. Issue 5: bumper keys PageUp/PageDown x2 call sites. Issue 6: Settings UI Theme selector rewritten to iterate registry. |
| `port/src/actionmap.cpp` | Issue 2: `break;` in `actionmapLoadBinds()` inner loop + long comment. |
| `port/fast3d/pdgui_menu_theme_editor.cpp` | Issues 3/4: full rewrite using `BeginPopupModal` instead of hand-rolled overlay + focus hack. Issue 6: `saveThemeAsMod()` calls `pdguiThemeRegisterModDir()` post-save. |
| `port/fast3d/pdgui_theme_loader.cpp` | Issue 6: `scan_mods_for_themes()` + `register_mod_theme_dir()` + `pdguiThemeRegisterModDir()` public API. `<dirent.h>` / `<sys/stat.h>` includes. |
| `port/include/pdgui_theme_loader.h` | Issue 6: declared `pdguiThemeRegisterModDir()`. |

Zero legacy handler or action-map entry removed. Out-of-scope file `port/fast3d/pdgui_menu_solomission.cpp` untouched per Mike's direction.

## Build baseline
- Pre-fix dev @ bfbb19b9: client 49,636,082 bytes / server 22,789,968 bytes.
- Post-fix target: headless build of both targets to verify clean link, byte-count check.
