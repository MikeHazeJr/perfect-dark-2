# Debug Shortcuts Audit -- 2026-04-26

Source branch: claude/amazing-diffie-0d908b
Total raw-SDL shortcuts found: 11 (9 in pdguiProcessEvent + 2 fallback in gfx_sdl2.cpp's post-pdgui switch)
ActionMap debug-adjacent (Forge/Grid) bindings flagged separately: 5+ (see bottom).

## Summary table

| Combo | Description | Category | Gating | Stability |
|-------|-------------|----------|--------|-----------|
| Shift+F1 | Cycle backface cull mode (none -> back -> front) | Rendering | Always-on | Stable (B-253 follow-up) |
| Shift+F2 | Toggle wireframe overlay (glPolygonMode lines) | Rendering | Always-on | Stable (B-253 follow-up) |
| F6 | Toggle MP bot AI / movement freeze (spawn-layout inspection) | Diagnostics | PD_DEV_BUILD | Stable |
| F7 | Toggle player invincibility (solo/MP); suppressed during Grid FREEFLY (dual-bound; see below) | Cheats | PD_DEV_BUILD | Stable |
| F8 | Hot-swap toggle (flip rendering mode for ImGui menus) | Tooling | Always-on | Stable |
| Right-stick click (gamepad) | Same as F8 (hot-swap toggle) | Tooling | Always-on | Stable |
| F9 | Toggle read-only menu/input diagnostics overlay | Diagnostics | Always-on | Stable |
| F10 | Toggle mesh collision debug overlay | Diagnostics | Always-on | Stable |
| F12 | Toggle debug overlay (push/pop g_CtxDebugOverlay) | Developer | PD_DEV_BUILD | Stable |
| Alt+Enter | Toggle fullscreen (window) | Tooling | Always-on | Stable (not strictly debug, but raw global) |
| Backtick (\`) | Toggle dev console | Developer | Always-on | Stable |

## Detailed entries

### Shift+F1 -- Cycle backface cull mode
- Handler: `gfxDebugCullModeCycle()` (in `port/fast3d/gfx_opengl.cpp`)
- Source: `port/fast3d/pdgui_backend.cpp:1217`
- Gating: Always-on
- Category: Rendering
- Stability: Stable (B-253 follow-up, S470)
- Notes: Cycles none -> back -> front -> none. Logs `DEBUG.CULL: mode -> <name>` via sysLogPrintf. Useful in Grid for inspecting back-faces or confirming inverted winding.

### Shift+F2 -- Toggle wireframe
- Handler: `gfxDebugWireframeToggle()` (in `port/fast3d/gfx_opengl.cpp`)
- Source: `port/fast3d/pdgui_backend.cpp:1229`
- Gating: Always-on
- Category: Rendering
- Stability: Stable (B-253 follow-up)
- Notes: Toggles glPolygonMode lines on world render. Logs `DEBUG.WIREFRAME: on|off`.

### F6 -- Toggle bot AI / movement freeze
- Handler: `botToggleUpdatesDisabled()` (declared in `src/include/game/bot.h`)
- Source: `port/fast3d/pdgui_backend.cpp:1164`
- Gating: PD_DEV_BUILD
- Category: Diagnostics
- Stability: Stable
- Notes: Drives a HUD banner via `botGetUpdatesDisabled()` consumed in pdgui_backend.cpp:614. Used for spawn-layout inspection in MP.

### F7 -- Toggle player invincibility (DUAL-BOUND; see Anomalies)
- Handler: `playerToggleDevInvincibility()` (declared in `src/include/game/player.h`)
- Source: `port/fast3d/pdgui_backend.cpp:1181`
- Gating: PD_DEV_BUILD; additional runtime gate -- if `forgeIsFreefly()` returns true the cheat is bypassed (returns 1) and the actionmap's `ACTION_FORGE_TOGGLE` handles the press instead.
- Category: Cheats
- Stability: Stable
- Notes: Drives HUD banner via `playerDevInvincibilityHudActive()`. Issue 5b/Issue 6 (2026-04-24) specifically called out the F7 dual-bind: same physical key fires both `playerToggleDevInvincibility` here AND `ACTION_FORGE_TOGGLE` via the actionmap when in Grid mode. The freefly check resolves the conflict.

### F8 -- Hot-swap toggle
- Handler: `pdguiHotswapToggle()` (in `port/fast3d/pdgui_hotswap.cpp`)
- Source: `port/fast3d/pdgui_backend.cpp:1189`
- Gating: Always-on
- Category: Tooling
- Stability: Stable
- Notes: Also bound to `SDL_CONTROLLER_BUTTON_RIGHTSTICK` click at line 1193-1197. Flips rendering mode for ImGui menus (PD-authentic vs raw ImGui or similar).

### F9 -- Menu/input diagnostics overlay
- Handler: `pdguiMenuStackOverlayToggle()` (in `port/fast3d/pdgui_menu_stack_debug.cpp`)
- Source: `port/fast3d/pdgui_backend.cpp:1240`
- Gating: Always-on
- Category: Diagnostics
- Stability: Stable
- Notes: Read-only overlay -- does NOT push an input context. Comment explicitly says "no input-context push." Intended for inspecting menu/input state without disturbing it.

### F10 -- Mesh collision debug
- Handler: `meshDebugToggle()` (declared in `port/include/meshdebug.h`, implemented in `port/src/meshdebug.c`)
- Source: `port/fast3d/pdgui_backend.cpp:1245` (primary, consumed)
- Also at: `port/fast3d/gfx_sdl2.cpp:329` (fallback path -- only fires if pdguiProcessEvent did not consume the event; effectively dead-coded since the primary path always consumes it)
- Gating: Always-on
- Category: Diagnostics
- Stability: Stable
- Notes: Comment at pdgui_backend.cpp:1244 says "was F9 before F9 was reserved for menu diagnostics." See Anomalies for the duplicated handler.

### F12 -- Debug overlay context push/pop
- Handler: inline `inputCtxPush(&g_CtxDebugOverlay)` / `inputCtxPopDeferred(&g_CtxDebugOverlay)`
- Source: `port/fast3d/pdgui_backend.cpp:1202`
- Gating: PD_DEV_BUILD
- Category: Developer
- Stability: Stable (S295 F1 hardening)
- Notes: Authoritative state is the input context stack -- no mirror bool. Same logic in the public-API entrypoint `pdguiToggle()` at line 1344.

### F8 -- Right-stick click (gamepad alias)
- Handler: `pdguiHotswapToggle()`
- Source: `port/fast3d/pdgui_backend.cpp:1193`
- Gating: Always-on
- Category: Tooling
- Stability: Stable
- Notes: SDL_CONTROLLERBUTTONDOWN with SDL_CONTROLLER_BUTTON_RIGHTSTICK. Listed as a separate row because the user is gamepad-driven.

### Alt+Enter -- Fullscreen toggle
- Handler: `set_fullscreen(!fullscreen_state, true)` (inline in gfx_sdl2.cpp)
- Source: `port/fast3d/gfx_sdl2.cpp:326`
- Gating: Always-on
- Category: Tooling
- Stability: Stable
- Notes: Not strictly a "debug" shortcut but it IS a raw global hotkey not exposed via ActionMap. Worth surfacing because it's an unbindable convention dev should know.

### Backtick (\`) -- Dev console toggle
- Handler: `pdguiConsoleToggle()` (in `port/src/console.c`)
- Source: `port/fast3d/gfx_sdl2.cpp:331`
- Gating: Always-on
- Category: Developer
- Stability: Stable

## Pending / not in this branch

- **F2 (no modifier) -- one-shot test-fire after 1s delay**: round-9 instrumentation per user; lives on another worktree branch awaiting merge. NOT in this branch (only Shift+F2 wireframe exists at SDLK_F2 in pdgui_backend.cpp:1229, which strictly requires KMOD_SHIFT). Flag for inclusion in the Debug Shortcuts tab when the merge lands.

## ActionMap-bound debug-adjacent shortcuts (flagged separately)

These are user-rebindable in `Settings -> Controls -> Game Hotkeys`. They are NOT raw SDL handlers -- they go through the ActionMap layer (`port/src/actionmap.cpp`) and dispatch via `actionmapDispatch(ev)` in pdgui_backend.cpp:1308. Listed for completeness because they are debug-adjacent (Grid level-editor mode):

| Action | Default key | Default pad | Purpose |
|--------|-------------|-------------|---------|
| ACTION_FORGE_TOGGLE | F7 | Back | Toggle Normal <-> Freefly within a Forge/Grid session |
| ACTION_FORGE_ASCEND | E | RT | Freefly +Y |
| ACTION_FORGE_DESCEND | Q | LT | Freefly -Y |
| ACTION_FORGE_BOOST | LSHIFT | -- | Hold for 3x freefly speed |
| ACTION_FORGE_PRECISION | LCTRL | -- | Hold for 0.25x freefly speed |
| ACTION_FORGE_SIDEBAR_TOGGLE | TAB | X | Show/hide editor sidebar |
| ACTION_FORGE_SIDEBAR_UP | UP | DPAD_UP | Sidebar selection -1 |
| ACTION_FORGE_SIDEBAR_DOWN | DOWN | DPAD_DOWN | Sidebar selection +1 |
| ACTION_FORGE_SIDEBAR_ACTIVATE | RIGHT | DPAD_RIGHT | Activate focused sidebar row |
| ACTION_FORGE_TAB_PREV | PAGEUP (or Ctrl+Shift+Tab) | LB | Previous editor tab |
| ACTION_FORGE_TAB_NEXT | PAGEDOWN (or Ctrl+Tab) | RB | Next editor tab |

Default bindings are registered in `port/src/actionmap.cpp:2172-2300`. If the Debug Shortcuts tab is purely informational (read-only), include these with a "see Settings -> Controls -> Game Hotkeys" pointer; do not duplicate the rebind UI.

## Anomalies

1. **F7 dual-bind**: Same physical key triggers two handlers depending on game state. Raw `SDLK_F7` in pdgui_backend.cpp:1181 calls `playerToggleDevInvincibility()` UNLESS `forgeIsFreefly()` is true, in which case it returns 1 (consumes the event) and lets the ActionMap dispatch `ACTION_FORGE_TOGGLE` for the same key. This is intentional (Issue 5b/6 fix, 2026-04-24) but worth surfacing because a debug-shortcuts UI may want to display "F7 -- Invincibility (or Toggle Forge Mode in Grid FREEFLY)" rather than picking one.

2. **F10 duplicated handler**: `meshDebugToggle()` is wired in two places -- pdgui_backend.cpp:1245 (consumed and returns 1) AND gfx_sdl2.cpp:329 (fallback `else if`). The fallback only runs if pdguiProcessEvent did not consume the event, which it always does. The fallback path is effectively dead. Likely a pre-pdgui-backend remnant; fixing is a separate task.

3. **No central registry**: There is no debug-shortcut registration system. Every shortcut is a hard-coded `if (ev->key.keysym.sym == SDLK_xx)` block. The Debug Shortcuts pause-menu tab will need its own static table. Recommend defining it in pdgui-side data and keeping it in sync with the handler block by code review (or, longer-term, a shared header that the dispatcher and the UI both include).

4. **F8 PD_DEV_BUILD asymmetry**: F6/F7/F12 are gated PD_DEV_BUILD, but F8 (hotswap), F9 (menu diag), F10 (mesh debug), Shift+F1, Shift+F2, backtick, and Alt+Enter are always-on. Some of these (Shift+F1 cull, Shift+F2 wireframe, backtick console) are clearly diagnostics that perhaps should be PD_DEV_BUILD too, but that's a policy call -- noting for awareness.

5. **F11 unbound**: F11 has no raw handler. Fullscreen is Alt+Enter only; the conventional F11 fullscreen does nothing. Consider whether the Debug Shortcuts UI should call this out (or whether F11 should be wired to fullscreen as a friendlier alternative to Alt+Enter).
