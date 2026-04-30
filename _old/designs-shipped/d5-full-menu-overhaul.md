# D5 Full Menu System Overhaul — Master Design Document

> **Status**: DESIGN — requires game director review before implementation
> **Created**: 2026-04-06 (S157)
> **Scope**: Input context stack, full menu roster port, controller navigation, theme system, character/arena selection UX, planned features
> **Estimated effort**: 15–25 sessions across 5 phases

---

## Executive Summary

This document covers the complete replacement of the legacy N64 menu and input system with a modern, controller-aware, theme-driven ImGui system. The work spans five major phases:

1. **Input Context Stack** — replace binary `INPUTMODE_MENU`/`INPUTMODE_GAMEPLAY` with a priority-based pushdown automaton
2. **Full Menu Roster Port** — port all 120 screens to ImGui (53 done, 17 stubs, 50 remaining)
3. **Controller Navigation** — D-pad wrap, A/B accept/cancel, left-stick scrolling, device detection
4. **Theme System** — auto-extract ROM textures, mod themes selectable in settings, debug menu rebuild
5. **Planned Features** — player portraits, lobby scene rendering, character preview in selection

---

## Phase 1: Input Context Stack

### Problem

The current system has a binary `g_InputMode` (MENU vs GAMEPLAY) with ~20 scattered `pdmainSetInputMode()` calls across netmsg.c, matchsetup.c, bridge code, pause menu, mission start, etc. Manual `inputLockMouse()` calls are sprinkled everywhere. This causes:

- B-92: Mouse not captured on mission start
- B-115: Post-game mouse unresponsive
- Controller events unconditionally consumed by ImGui when ANY overlay is active (line 670 in pdgui_backend.cpp)
- No input buffering across transitions

### Architecture

**Input Context Stack (Pushdown Automaton)** — industry standard pattern from Unreal Enhanced Input System.

```c
/* port/include/inputctx.h */

#define INPUTCTX_MAX_STACK 16

typedef struct InputContext {
    const char *name;               /* "gameplay", "pause_menu", "settings" */
    int priority;                    /* Higher = processes first */
    bool (*can_consume)(SDL_Event *); /* Does this context want this event? */
    void (*on_event)(SDL_Event *);   /* Handle the consumed event */
    void (*on_push)(void);           /* Lifecycle: show cursor, etc. */
    void (*on_pop)(void);            /* Lifecycle: hide cursor, etc. */
    void (*on_poll)(void);           /* Per-frame polling (continuous input) */
    bool marked_for_removal;
    bool active;
} InputContext;

/* Stack API */
void inputCtxInit(void);
void inputCtxPush(InputContext *ctx);
void inputCtxPopDeferred(InputContext *ctx); /* Marks for removal at frame end */
void inputCtxDispatch(SDL_Event *event);     /* Walk stack, consume event */
void inputCtxPollFrame(void);               /* Per-frame continuous input */
void inputCtxEndFrame(void);                /* Pop marked contexts */
InputContext *inputCtxGetTop(void);

/* Built-in contexts */
extern InputContext g_CtxGameplay;
extern InputContext g_CtxImGuiMenu;
extern InputContext g_CtxPauseMenu;
extern InputContext g_CtxTextInput;
```

**Dispatch flow** (every frame):
1. `SDL_PollEvent()` → collect events
2. For each event: walk stack top-to-bottom, first `can_consume()` match gets it
3. Mark event consumed; lower contexts never see it
4. After all events: `inputCtxPollFrame()` for continuous input (movement sticks)
5. `inputCtxEndFrame()` — actually remove marked-for-removal contexts

**Mouse capture is automatic**: `on_push`/`on_pop` handles SDL relative mouse mode. No manual `inputLockMouse()` calls anywhere.

**Frame-boundary safety**: Contexts are never popped mid-frame. `inputCtxPopDeferred()` sets `marked_for_removal = true`; actual removal happens in `inputCtxEndFrame()`. This eliminates the one-frame-of-wrong-input bug class.

### What Gets Replaced

- `g_InputMode` (binary enum) → `InputContextStack`
- `pdmainSetInputMode()` (~20 call sites) → `inputCtxPush()` / `inputCtxPopDeferred()`
- Scattered `inputLockMouse()` calls → automatic in context lifecycle
- The switch in `pdgui_backend.cpp:646-674` → context-based dispatch
- Manual `ImGui_ImplSDL2_ProcessEvent()` routing → ImGui context in stack like any other

### Files

| File | Purpose |
|------|---------|
| `port/include/inputctx.h` | Public API |
| `port/src/inputctx.c` | Stack implementation (~300 LOC) |
| `port/fast3d/pdgui_backend.cpp` | Rewrite event filter to use context stack |
| `port/src/pdmain.c` | Remove `g_InputMode`, `pdmainSetInputMode()` |
| ~20 callers | Migrate to push/pop |

### Sessions: 2–3

---

## Menu UX Guidelines (Binding — All Phases Must Follow)

> Researched from Halo Infinite, Destiny 2, Persona 5, Monster Hunter World, Celeste, Hollow Knight, Dead Cells. These are non-negotiable design rules for every menu screen.

### Navigation
- **D-pad for menus, always.** Both D-pad AND left stick bound to nav, treated as digital press (not analog range).
- **Circular wrapping always enabled** — top↔bottom, left↔right. Never a dead-end navigation state.
- **LB/RB for tab switching** between major sections (categories, screen tabs).
- **A=confirm, B=back, X=secondary action, Y=tertiary/context.** Consistent across every screen.
- **Maximum 2-3 menu levels deep.** If deeper is needed, redesign the hierarchy.
- **5-9 items per screen** without scrolling. More = categorize into tabs or sub-sections.

### Layout Patterns for PD2
- **Main menu**: Vertical list, ≤5 items (Play, Customize, Settings, Modding, Profile)
- **Character select**: Grid (3x4 or 4x4), D-pad 4-way, detail panel on right with live 3D preview
- **Arena select**: Grid with thumbnails, categorized tabs (Dark, Classic, Solo, Bonus, Mods) via LB/RB
- **Match settings**: Split-panel — settings list on left (sliders, toggles), live preview on right
- **Bot config**: Expandable list, each row opens inline drawer or modal
- **Dropdowns**: Labels left, control right. Items alphabetized within collapsible categories.

### Visual Feedback
- **Multi-layered focus indicator**: outline + color shift + slight scale. Not outline alone.
- **Audio cue on EVERY selection change.** Non-negotiable for good controller feel.
- **150-300ms smooth transitions** between selections.
- **Focus indicator visible on any background color** — must contrast against both light and dark.
- **Hover (mouse) vs Focus (controller)**: Same visual weight, different trigger. Last input device wins.

### Hybrid Input
- **Last input device wins.** Controller → hide cursor, show focus. Mouse → show cursor, suppress focus highlight.
- **Button prompts change dynamically** based on last device (gamepad icons vs keyboard labels).
- **Mouse click selects freely**; D-pad navigates focus grid.
- **500ms debounce** on device switching to prevent flickering.

### UI Scaling (Binding Rules)

**Reference resolution**: 1080p. All sizes defined at 1080p, scaled proportionally at runtime.

**Formula**: `scale = viewport_height / 1080.0f`

| Element | 1080p (px) | 1440p | 4K | % of viewport height |
|---------|-----------|-------|-----|---------------------|
| Menu heading | 36 | 48 | 72 | 3.3% |
| Body text / menu items | 24 | 32 | 48 | 2.2% |
| Small UI labels | 16 | 21 | 32 | 1.5% |
| Button height | 64 | 85 | 128 | 5.9% |
| Min click/focus target | 48 | 64 | 96 | 4.4% (gamepad-friendly) |
| Window padding | 16 | 21 | 32 | 1.5% |
| Item spacing | 8 | 11 | 16 | 0.7% |
| Scrollbar width | 14 | 19 | 28 | 1.3% |

**Font loading**: Load fonts at scaled pixel size BEFORE building the ImGui font atlas. Do NOT use `FontGlobalScale` — it blurs. `AddFontFromFileTTF("font.ttf", 24.0f * scale)` is crisp.

**ImGui style scaling**: Apply scale to all style vars:
```c
float scale = viewport_height / 1080.0f;
style.WindowPadding = ImVec2(16 * scale, 12 * scale);
style.FramePadding = ImVec2(8 * scale, 6 * scale);
style.ItemSpacing = ImVec2(12 * scale, 8 * scale);
style.ScaleAllSizes(scale);
```

**Ultrawide (21:9, 32:9)**: Clamp menu width to max 2560px, center on screen. 3D world expands; menus don't stretch. Menu never narrower than 60% or wider than 90% of viewport width.

**Safe Areas** (binding — all UI elements must respect these):

```
Outer Safe Area (decorative only):  5% inset from each edge
Inner Safe Area (interactive):     10% inset from each edge (ultrawide)
                                    5% inset from each edge (16:9 / 16:10)
```

| Aspect Ratio | Viewport | Horizontal Margin | Vertical Margin | Safe Content Width |
|-------------|----------|-------------------|-----------------|-------------------|
| 16:9 (1920×1080) | 1920×1080 | 96px (5%) | 54px (5%) | 1728×972 |
| 16:9 (3840×2160) | 3840×2160 | 192px (5%) | 108px (5%) | 3456×1944 |
| 21:9 (3440×1440) | 3440×1440 | 344px (10%) | 72px (5%) | 2752×1296 |
| 32:9 (5120×1440) | 5120×1440 | 512px (10%) | 72px (5%) | 4096×1296 (clamped to 2560 for menus) |

**Rules**:
- ALL interactive elements (buttons, sliders, text inputs, lists) must be inside the inner safe area
- HUD elements (health, ammo, minimap, killfeed) anchor to safe area corners, not viewport corners
- Decorative elements (background haze, scanlines, ambient particles) can extend to full viewport
- Menu dialogs: max width = min(2560px, safe_area_width). Centered horizontally within safe area.
- Each edge is independently configurable via pd.ini: `Video.SafeAreaTop`, `Video.SafeAreaBottom`, `Video.SafeAreaLeft`, `Video.SafeAreaRight` (float, 0.0–0.25, default auto-detected from aspect ratio).
- **Defaults**: 16:9 → all edges 5%. Ultrawide (>2.0 ratio) → left/right 10%, top/bottom 5%. Player overrides persist.
- **TV overscan**: Some TVs crop 3-5% of each edge unevenly. Per-edge control lets players compensate for their specific display.
- **Settings UI**: Video settings shows a visual safe area preview — a rectangle overlay on the game view that the player resizes per-edge with sliders or D-pad. They see exactly what they're adjusting.

**Implementation**: `pdguiGetSafeArea()` returns a rect `{ x, y, w, h }` representing the usable content area. ALL menu positioning, dialog sizing, and HUD anchoring references this rect — never the raw viewport. The safe area IS the coordinate space for UI layout.

```c
typedef struct {
    float x, y, w, h;  /* Usable content area in viewport pixels */
} PdSafeArea;

PdSafeArea pdguiGetSafeArea(void);

/* Per-edge margins (0.0–0.25 of viewport dimension) */
void pdguiSetSafeAreaMargins(float top, float bottom, float left, float right);
```

The safe area defines WHERE menus live. Menu layout math uses `safeArea.x` as origin, `safeArea.w` as available width. A menu that says "center horizontally" means center within the safe area, not the viewport. HUD health bar anchored to "bottom-left" means bottom-left of the safe area.

**Window size / resolution changes**: Re-load font atlas and re-apply style scaling when resolution changes at runtime. Current `pdguiScaleFactor()` must use this formula.

**User-facing scale slider**: Video settings should offer a UI Scale slider (75-200%) that multiplies the base scale. Saved to `pd.ini` via `configRegisterFloat("Video.UIScale", ...)`.

### Accessibility
- **Minimum 16px text at 1080p** (small labels); 24px for menu items; 36px for headings
- **Text contrast ratio**: 4.5:1 minimum (WCAG AA). PD blue palette white-on-navy already meets this.
- **HUD scale slider** (75-200%) — separate from menu scale
- **Never color-only distinction** — always pair with icon or text
- **Remappable controls** for all menu actions
- **Auto-focus first logical item** when any menu opens
- **Viewing distance**: Designed for 2-3 feet (monitor). At 3 feet, 24px at 1080p is comfortable minimum.

### Avoid
- Grids larger than 5x5 (tedious with controller)
- Analog-only menu navigation (D-pad is required)
- Inconsistent button mapping between screens
- No scroll indicators on scrollable content
- Deeply nested trees (>3 levels)
- Focus indicator that's invisible on colored backgrounds

---

## Phase 2: Controller Navigation

### Requirements

- **D-pad navigation**: Up/Down moves between menu items, Left/Right adjusts values (sliders, dropdowns)
- **Wrapping**: Top wraps to bottom, bottom wraps to top, left wraps to right
- **A button**: Accept / confirm / click focused element
- **B button**: Cancel / back / close menu (pop context)
- **Start button**: Open/close pause menu (toggle)
- **Left stick**: Scroll content in scrollable areas; dead zone 0.15 for noise, 0.3 for movement
- **Right stick**: Reserved for camera in gameplay; ignored in menus
- **Triggers**: Page up/down in long lists (optional)
- **Device detection**: Track last-used device (KB/M vs gamepad) with 500ms debounce. Switch UI prompts.
- **Input buffer for cheat codes**: Rolling circular buffer of recent button presses (last 20 inputs with timestamps) in the gameplay context's `on_event` callback. When a sequence matches a known cheat code and cheats are enabled in solo/online options, fire the cheat action. Buffer only active when cheats are enabled. Event-driven (not polling). Supports both keyboard and controller sequences.

### Input System as Single Source of Truth (Binding — Future Phase)

The input context stack will become the **sole authority for ALL input**, replacing both the game input layer (`input.c` CK_* mappings) and ImGui's built-in gamepad nav (`imgui_impl_sdl2.cpp` hardcoded mappings). One system, one binding config, zero parallel paths.

**Tap / Hold / Double-Tap Recognition:**

Controller has ~14 buttons vs keyboard 100+. Modifier actions triple the effective action space:
- **Tap** (default, <300ms): single quick press. E.g., tap X = reload.
- **Hold** (>500ms threshold): press and hold. E.g., hold X = open door / interact.
- **Double-tap** (<400ms window): two quick presses. E.g., double-tap Y = quick-switch weapon.

The context stack resolves raw button presses into recognized actions BEFORE routing to context callbacks. Contexts receive resolved actions (`ACTION_RELOAD`, `ACTION_INTERACT`), not raw buttons.

**Per-Context Action Maps:**

Each context declares its own action map:
```
Gameplay context:
  ACTION_SHOOT        = Right Trigger (tap)
  ACTION_AIM          = Left Trigger (hold)
  ACTION_RELOAD       = X (tap)
  ACTION_INTERACT     = X (hold, 0.5s)
  ACTION_JUMP         = A (tap)
  ACTION_CROUCH       = B (hold)
  ACTION_MELEE        = B (tap)
  ACTION_QUICKSWITCH  = Y (double-tap)
  ACTION_WEAPON_NEXT  = Y (tap)
  ACTION_PAUSE        = Start (tap)

Menu context:
  ACTION_ACCEPT       = A (tap)
  ACTION_CANCEL       = B (tap)
  ACTION_SECONDARY    = X (tap)
  ACTION_DELETE       = X (hold, 1.0s — safety delay)
  ACTION_TAB_PREV     = LB (tap)
  ACTION_TAB_NEXT     = RB (tap)
  ACTION_NAV_UP       = D-pad Up / Left Stick Up
  ACTION_NAV_DOWN     = D-pad Down / Left Stick Down
```

**Fully rebindable**: Player sees "Hold X — Open Door" in the rebind UI. They can rebind `ACTION_INTERACT` to any button + any modifier. The action map stores the binding; the recognition system applies it.

**Replaces:**
- `input.c` CK_* static mappings → action map bindings
- `inputmodes.c` INPUTMODE_SINGLE/DOUBLETAP/HOLD → integrated into context stack dispatch
- ImGui's `imgui_impl_sdl2.cpp` hardcoded gamepad→ImGuiKey mappings → our action map drives ImGui nav

**Existing code**: `port/src/inputmodes.c` already has tap/hold/double-tap with timing windows and pd.ini persistence. This infrastructure gets absorbed into the context stack, not thrown away.

### ImGui Integration

ImGui has built-in gamepad nav via `ImGuiConfigFlags_NavEnableGamepad`. Currently the flag is set but gamepad events are unconditionally consumed at line 670 of pdgui_backend.cpp. With the input context stack, the ImGui context's `can_consume()` will check `io.WantCaptureKeyboard` / `io.WantCaptureMouse` / navigation state.

**Custom navigation** for PD2-specific UIs (character select, arena list) that go beyond ImGui's default nav:

```c
/* D-pad wrap logic */
if (dpad_down && focused_index == item_count - 1) {
    focused_index = 0;  /* Wrap to top */
}
if (dpad_up && focused_index == 0) {
    focused_index = item_count - 1;  /* Wrap to bottom */
}
```

### UX Rules

- **Labels on left of controls**: All dropdowns, sliders, toggles have their label flush-left
- **Dropdowns alphabetized + collapsible categories**: Body/head dropdowns sorted by source (base game → mod name), then by sub-category (male, female, alien), then alphabetical within
- **Visual representations**: Character selection shows 3D head/body combo updating live as options change
- **Arena list**: Same category+alpha pattern — Dark, Classic, Solo Missions, Bonus, Mods

### Sessions: 2–3

---

## Phase 3: Full Menu Roster Port

### Current State (from menu-inventory.md)

| Status | Count |
|--------|-------|
| ImGui (complete) | 53 |
| ImGui (standalone) | 4 |
| Stub (registered, incomplete) | 17 |
| OG (forced, NULL renderFn) | 3 |
| OG (native text/confirm) | 13 |
| OG (unregistered, legacy tickFn) | 28 |
| Type-based ImGui | 2 |
| **Total** | **120** |

### Remaining Work: 61 screens

**Priority A — Gameplay-blocking (must port first):**
- Solo Pause Menu: Inventory + Objectives (B-93, B-98) — D5.2
- Mission Select: two-panel redesign with unlock filter (B-90, B-91, B-96) — D5.3
- Solo Control Style — D5.7
- Solo end-game screens (7 stubs) — D5.4
- MP end-game screens (9 stubs + 3 challenge stubs) — D5.4

**Priority B — Online flow:**
- Join Address dialog — D5.7
- Joining Game progress — D5.7
- Co-op/Counter-Op dialogs (4 OG screens) — D5.7

**Priority C — Settings & Options (28 OG screens):**
- CI Options tree (audio/video/display/control) — D5.6
- 2P split-screen variants (pause/options/display/control/abort/end) — D5.7
- Cheats menu — D5.7
- Cinema viewer — D5.7
- PD Mode Settings — D5.7

**Priority D — File management / utility:**
- Rename File, Duplicate Name Error, File Saved Confirmation — D5.7
- Save Setup Name, Player Name, Team Name — D5.7
- Ready/Status/Delete confirmations — D5.7

### UX Standards for All Ported Screens

1. **Labels left, controls right** — consistent layout across all screens
2. **Dropdowns**: Alphabetized items within collapsible categories
3. **Character selection**: Categorized drawer sliding in from left (base game / mod → male/female/alien → alphabetical). Live 3D preview of head+body combo.
4. **Arena selection**: Same drawer pattern — Dark/Classic/Solo/Bonus/Mods categories, alphabetical within
5. **Solo Combat Sim and Online Combat Sim share the same lobby UI** — already done via room.cpp's `s_IsSoloMode`
6. **No scrollbox-in-scrollbox** — constraint from menu stack architecture
7. **Scale with resolution** — all layouts relative, zero hardcoded pixel offsets

### Sessions: 8–12 (batches of related screens)

---

## Phase 4: Theme System

### Current State

- `pdgui_theme.cpp` has full N64 texture decode pipeline (RGBA16, IA16, IA8, IA4, CI8, CI4)
- `pdgui_style.cpp` has 7 palettes, shimmer, button glow, text glow
- D5.0 commit added: haze overlay, CRT scanlines, multi-palette support, TGA loader
- `mods/base-ui/` mod with 13 texture entries in mod.json
- Procedural fallback textures when TGA files are missing

### Problem: Texture Extraction

The log shows:
```
ERROR: fsFileLoad: could not find file: .../mods/base-ui/textures/ui_bg_haze.tga
WARNING: PDGUI theme: failed to load 'mods/base-ui/textures/ui_bg_haze.tga'
```

**Root cause**: The TGA files are generated by `pdguiThemeExtractRomTextures()`, which only runs when `--extract-ui-textures` CLI flag is passed. The updater doesn't create new directories, so machines updated from older versions don't have `mods/base-ui/textures/`.

**Fix**: In `pdguiThemeCheckExtract()` (called each frame until done), replace the CLI flag check with an existence check:

```c
void pdguiThemeCheckExtract(void)
{
    static bool s_checked = false;
    if (s_checked) return;
    if (!g_TexGeneralConfigs) return;
    s_checked = true;

    /* Auto-extract if base-ui textures don't exist yet */
    if (!s_baseUiTexturesExist()) {
        sysLogPrintf(LOG_NOTE, "PDGUI: base-ui textures missing, extracting from ROM...");
        fsCreateDir("mods/base-ui");
        fsCreateDir("mods/base-ui/textures");
        pdguiThemeExtractRomTextures();
    }

    /* Keep CLI flags for manual re-extract / modern UI generation */
    if (sysArgCheck("--extract-ui-textures")) {
        pdguiThemeExtractRomTextures();
    }
    if (sysArgCheck("--generate-modern-ui")) {
        s_generateModernUiTextures();
    }
}
```

### Theme Selection in Settings

**Menu Themes selectable in Video/Graphics settings:**
- List populated with: "Default (ImGui)" + any mod themes from `mods/*/theme.json`
- The `base-ui` mod is the native PD theme
- `pd-modern-ui` is a procedural alternative
- Users can create themes by making a mod with a `theme.json` and texture overrides
- Selected theme persisted in `pd.ini` via `configRegisterString("Video.Theme", ...)`
- Debug menu theme selector rebuilt to use the same list

### Mod Theme Format

```json
{
    "name": "PD Classic",
    "author": "Base Game",
    "palette": "blue",
    "background": "base:ui_bg_haze",
    "scanlines": true,
    "scanline_alpha": 0.16,
    "tint_strength": 0.3,
    "text_glow": true
}
```

### Sessions: 2–3

---

## Phase 5: Planned Features

### 5A: Player Portrait System

Player creates their portrait by:
1. Select head + body combination
2. Choose a pose (idle, action, dramatic — from existing animation catalog)
3. System renders a snapshot to texture
4. Saved as player image (TGA in save directory)
5. Used as avatar in lobby, scoreboard, kill feed

**Implementation**: Render the selected character model to an offscreen framebuffer, read back pixels, save as TGA. The fast3d renderer already supports offscreen rendering for the 3D character preview in bot setup.

### 5B: Lobby Scene with Connected Players

Game lobby room background populates with 3D models of all connected players:
- Characters use their selected body/head
- Idle pose (breathing animation)
- Agent names labeled above each character
- When a player is hovered/selected in the players list, their 3D model highlights (subtle glow effect)
- Models positioned in a semi-circle or line formation

**Implementation**: Extend the 3D preview pipeline (`pdguiCharPreview`) to render multiple characters. Position using a layout algorithm (equal spacing, facing camera). Highlight via additive blend on the selected model.

### Sessions: 3–4 (after core infrastructure is complete)

---

## Legacy Input System — What Gets Stripped

### Legacy Menu Input Path (menumgr.c / menutick.c / menu.c / menuitem.c)

The original N64 menu system processes input through:
1. `menuTick()` — called every frame when menus are active
2. N64 controller buttons mapped via `contGetButton()` / `contGetStickX/Y()`
3. `menuPush()` / `menuPop()` for stack management
4. `menuhandler*` callbacks for per-item behavior (checkbox, slider, list, etc.)
5. `menuitemTick()` for individual item rendering + input handling

**Files involved** (24 files reference menuTick/Push/Pop):
- `src/game/menutick.c` — main menu tick loop, cursor rendering
- `src/game/menu.c` — menu rendering, item layout, scrolling
- `src/game/menuitem.c` — individual item types (slider, list, dropdown, etc.)
- `src/game/mainmenu.c` — all dialog definitions + handler callbacks
- `src/game/menumgr.c` — push/pop stack, depth tracking
- `src/game/activemenu.c` — active menu state tracking

**These are all deprecated.** ImGui menus already bypass this system entirely. The legacy code remains only because some OG screens still render through it. Once Phase 3 ports all screens, the entire legacy menu tick/render path can be stripped.

### What We Keep

- Menu dialog definitions (`menudialogdef` structs in mainmenu.c) — used as hotswap registration targets
- Handler callbacks that contain GAME LOGIC (not rendering) — e.g., `menuhandlerAcceptMission()` triggers mission start
- CK_* key constants — used by both legacy and new input system

### What We Strip

- `menuTick()` / `menutickMain()` — the entire legacy per-frame menu processing
- `menuRender()` / `menuitemRender()` — legacy N64-style rendering
- `menuUpdateCursor()` — legacy cursor positioning
- `contGetButton()` for menu input — replaced by SDL events through input context stack
- The `g_Menus[]` / `g_AmMenus[]` arrays — per-player active menu state

---

## Implementation Plan — Session Breakdown

### Phase 1: Input Context Stack (Sessions 1–3)

| Session | Focus | LOC |
|---------|-------|-----|
| S1.1 | `inputctx.h` + `inputctx.c` — stack struct, push/pop/dispatch, built-in contexts | ~350 |
| S1.2 | Rewrite `pdgui_backend.cpp` event filter to use context stack; wire into SDL loop | ~200 |
| S1.3 | Migrate all `pdmainSetInputMode()` callers (~20 sites); remove `g_InputMode` | ~150 |

### Phase 2: Controller Navigation (Sessions 4–6)

| Session | Focus | LOC |
|---------|-------|-----|
| S2.1 | ImGui gamepad nav wiring, D-pad wrap logic, A/B accept/cancel | ~250 |
| S2.2 | Device detection (last-used switching), UI prompt icons | ~200 |
| S2.3 | Custom nav for character/arena drawers — categorized, alphabetized, collapsible | ~300 |

### Phase 3: Full Menu Roster Port (Sessions 7–18)

| Session | Focus | Screens | LOC |
|---------|-------|---------|-----|
| S3.1 | Solo Pause: Inventory + Objectives (B-93, B-98) | 2 | ~400 |
| S3.2 | Mission Select redesign (B-90, B-91, B-96) | 5 | ~500 |
| S3.3 | Solo end-game screens (7 stubs) | 7 | ~350 |
| S3.4 | MP end-game screens (12 stubs + challenges) | 12 | ~500 |
| S3.5 | Co-op / Counter-Op flow (4 OG) | 4 | ~250 |
| S3.6 | Options tree — Audio/Video/Display/Control (8 OG) | 8 | ~400 |
| S3.7 | 2P split-screen variants (12 OG) | 12 | ~500 |
| S3.8 | CI options + control style (6 OG) | 6 | ~300 |
| S3.9 | Cheats + Cinema + PD Mode + Exit (4 OG) | 4 | ~200 |
| S3.10 | File management + text input dialogs (9 OG native) | 9 | ~350 |
| S3.11 | Join/Joining/Co-op host network dialogs (3 OG) | 3 | ~200 |
| S3.12 | Final audit — zero OG screens remaining, full regression | — | ~100 |

### Phase 4: Theme System (Sessions 19–21)

| Session | Focus | LOC |
|---------|-------|-----|
| S4.1 | Auto-extract textures on startup; fix late-init; `s_baseUiTexturesExist()` check | ~100 |
| S4.2 | Theme selection in Video settings; mod theme format (`theme.json`); debug menu rebuild | ~300 |
| S4.3 | Theme creation interface — in-game palette/texture preview; save as mod | ~350 |

### Phase 5: Planned Features (Sessions 22–25)

| Session | Focus | LOC |
|---------|-------|-----|
| S5.1 | Player portrait system — offscreen render, pose selection, TGA save | ~400 |
| S5.2 | Lobby scene — multi-character 3D rendering, idle poses, name labels | ~450 |
| S5.3 | Lobby scene — hover highlight, selection sync, layout algorithm | ~300 |
| S5.4 | Polish pass — transitions, animations, edge cases | ~200 |

**Total: ~25 sessions, ~6,500 LOC**

---

## Acceptance Criteria

- [ ] Zero legacy `menuTick()` render paths active — all 120 screens in ImGui
- [ ] Controller navigates all menus: D-pad wrap, A/B confirm/cancel, stick scroll
- [ ] Input context stack: no input bleed between gameplay and menu transitions
- [ ] Mouse capture/release automatic — zero manual `inputLockMouse()` calls
- [ ] Theme system: base-ui textures auto-extracted, mod themes selectable in settings
- [ ] Character selection: categorized drawer with live 3D preview
- [ ] Arena selection: categorized drawer matching character selection UX
- [ ] Solo Combat Sim and Online Combat Sim share identical lobby UI
- [ ] All constraints respected: catalog IDs everywhere, no legacy integer identity

---

## Dependencies

- **D5.0 visual layer** — DONE (S157). Theme draw functions available.
- **D5.1 input boundary** — DONE (S136). Will be replaced by Phase 1 context stack.
- **Catalog ID migration** — ongoing. Menu screens must use catalog IDs for all asset references.
- **Lobby Unification** — DONE (S153). Solo/online room already shared.

---

## Risk Register

| Risk | Mitigation |
|------|-----------|
| Legacy menu handlers contain game logic mixed with rendering | Audit each handler; extract logic into standalone functions before stripping render code |
| Controller nav conflicts with ImGui's built-in nav | Custom nav layer on top of ImGui; disable ImGui nav for custom screens |
| Theme extraction fails on machines without ROM | Procedural fallbacks already work; server doesn't need textures |
| 120 screens is a lot to port | Batch by category; many are variants (H/V layouts) that share 80% code |
| Context stack mid-frame edge cases | Deferred pop + input buffering eliminates the class |
