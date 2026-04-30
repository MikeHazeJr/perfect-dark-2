# PD2 Implementation Plan: Mod System & D5 UI Completion

**Created:** 2026-04-04
**Status:** Active — guides all remaining infrastructure and UI work
**Scope:** Mod pipeline (menu → bot names → UI textures → themes → preview), D5 UI screens, OG menu removal, open bugs

---

## 1. Dependency Graph

```
                    ┌─────────────────────────┐
                    │   P1: Mod Menu           │
                    │   (gateway for all mods) │
                    └──────┬──────────────┬────┘
                           │              │
                ┌──────────▼───┐   ┌──────▼──────────────────┐
                │ P2: Bot Name │   │ P3: D5.0 Visual Theme   │
                │ Dictionary   │   │ (ROM textures, pdgui_   │
                │ Mod          │   │  theme, scan-line pass)  │
                └──────────────┘   └──┬─────────┬────────────┘
                                      │         │
                          ┌───────────▼──┐  ┌───▼──────────────────┐
                          │ P4: UI       │  │ P6: Agent Create     │
                          │ Texture Mod  │  │ Preview Panel        │
                          │ (9-slice,    │  │ (body+head render)   │
                          │  effects)    │  └──────────────────────┘
                          └──────┬───────┘
                                 │
                          ┌──────▼───────┐
                          │ P5: Theme    │
                          │ Creation     │
                          │ Interface    │
                          └──────────────┘

    ═══════════════════════════════════════════════════════
    After mod pipeline is proven, D5 UI screens proceed:
    ═══════════════════════════════════════════════════════

        ┌──────────┐   ┌──────────┐
        │ P7: D5.2 │   │ P8: D5.3 │   (parallel — independent screens)
        │ Mission  │   │ Pause    │
        │ Select   │   │ Menu     │
        └────┬─────┘   └────┬─────┘
             │              │
             ▼              ▼
        ┌──────────────────────┐
        │ P9: D5.4 Remainder   │
        │ + D5.6 Settings/QoL  │
        └──────────┬───────────┘
                   │
                   ▼
        ┌──────────────────────┐
        │ P10: D5.7 OG Menu    │
        │ Removal Pass         │
        │ (depends on ALL      │
        │  screens existing)   │
        └──────────────────────┘
```

### Dependency Rationale

- **P1 → P2, P4, P5**: The mod menu is the user-facing gateway. No mod can be loaded, enabled, or configured without it.
- **P1 → P3**: The mod menu doesn't technically block D5.0 at the code level, but D5.0's texture pipeline needs the catalog asset system that the mod menu exercises. Building them in sequence validates the pipeline.
- **P3 → P4**: UI Texture Mod requires the ROM texture extraction pipeline and GL rendering that D5.0 establishes.
- **P4 → P5**: Theme Creation builds on the 9-slice system, overlay effects, and font infrastructure from the UI Texture Mod.
- **P3 → P6**: Agent preview needs texture-to-GL rendering from D5.0 (the same spike work, extended to body/head models).
- **P7, P8 parallel**: Mission Select and Pause Menu are independent screens with no shared state. Both benefit from P3 (theming) but don't block each other.
- **P9 after P7+P8**: Mission complete screen and lobby tab gating depend on the patterns established in mission select and pause menu.
- **P10 last**: OG menu removal can only happen after every replacement screen exists.

---

## 2. Phase Details

---

### P1: Mod Menu — The Gateway

**Goal:** Users can browse, enable/disable, and inspect mods from the main menu. This proves the catalog pipeline works end-to-end.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_menu_mods.cpp` | Create | Full mod browser screen |
| `pdgui_menu_mods.h` | Create | Public API |
| `mod_manager.cpp/.h` | Create | Filesystem scan, enable/disable state, load order |
| `mod_manifest.h` | Create | Manifest schema: ID, version, base fallback, dependencies |
| `asset_catalog.cpp` | Modify | Hook mod loading into catalog resolution |
| `pdgui_menu_main.cpp` | Modify | Add "Mods" entry to main menu |

**Estimated LOC:** ~500 (250 UI, 150 mod_manager, 100 manifest/catalog glue)

**Key Design Decisions:**

1. **Mod folder structure:** Each mod is a directory under `<game>/mods/` containing a `mod.json` manifest and asset folders. The manifest declares `id`, `version`, `base_fallback` (required — constraint from game director), `dependencies[]`, and `assets{}` mapping catalog IDs to local paths.

2. **Enable/disable persistence:** A `mods-enabled.json` file in the user config directory. Simple ordered list of mod IDs. Load order = list order.

3. **Catalog integration:** When a mod is enabled, its assets are registered into the catalog with priority layering. Mod assets override base assets for the same catalog ID. When disabled, entries are removed. The catalog must support hot-reload for this.

4. **No creator locks** (constraint): Any mod can be opened in the editor, forked, or modified. The manifest has no lock field.

5. **Base fallback required** (constraint): Manifest validation rejects any mod without a `base_fallback` field. The fallback is the catalog ID that should be used if the mod's asset fails to load.

6. **Size policy:** No hard ceiling. If a mod exceeds a configurable threshold (default 50MB), show an approval prompt before enabling. Threshold stored in user settings.

**Acceptance Criteria:**
- [ ] Mods directory is scanned on menu open; results displayed in scrollable list
- [ ] Each mod shows: name, version, author, enabled/disabled toggle, description
- [ ] Toggle persists across restarts via mods-enabled.json
- [ ] Enabling a mod registers its assets in the catalog; disabling removes them
- [ ] Invalid manifests (missing base_fallback, malformed JSON) show error inline, don't crash
- [ ] Empty mods folder shows "No mods found" with path hint
- [ ] Menu integrates into push/pop stack with proper input context

---

### P2: Bot Name Dictionary Mod

**Goal:** The first real mod. Proves catalog asset round-trip: create → package → load → use → edit → save.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `mods/base_botnames/mod.json` | Create | Manifest for default bot names mod |
| `mods/base_botnames/botnames.json` | Create | First/second word pools as JSON |
| `pdgui_menu_botnames.cpp` | Create | Editor UI |
| `pdgui_menu_botnames.h` | Create | Public API |
| `botname_generator.cpp/.h` | Create or Modify | Load names from catalog instead of hardcoded |
| `pdgui_menu_mods.cpp` | Modify | Add "Edit" action for mods with editor support |

**Estimated LOC:** ~350 (200 editor UI, 100 generator refactor, 50 mod package)

**Key Design Decisions:**

1. **Asset format:** `botnames.json` with structure `{ "first_words": [...], "second_words": [...] }`. Human-readable, diffable, round-trippable.

2. **Catalog ID:** `base:botnames_default` — follows the `namespace:asset_name` convention.

3. **Append vs. Replace mode:** The editor has a toggle. Append mode merges the mod's word pools with the base pools. Replace mode uses only the mod's pools. Stored in the mod manifest as `"mode": "append" | "replace"`.

4. **Editor features:**
   - String editor for each pool (add/remove/reorder words)
   - "Generate Name" test button — picks random first+second, displays result
   - Combinatorial count display: `first.length × second.length = N possible names`
   - Save button writes back to the mod's asset file
   - Load button re-reads from disk (discard unsaved changes with confirmation)

**Acceptance Criteria:**
- [ ] `base:botnames_default` loads from mod package via catalog
- [ ] Editor opens from mod menu "Edit" action
- [ ] Append/Replace toggle changes merge behavior and persists
- [ ] Words can be added, removed, reordered in each pool
- [ ] "Generate Name" produces valid combinations from current pools
- [ ] Combinatorial count updates live as pools change
- [ ] Save writes valid JSON back to mod directory
- [ ] Bot name generation in-game uses catalog-loaded pools (not hardcoded)
- [ ] Mod can be disabled; game falls back to base names

---

### P3: D5.0 — Menu Visual Theme Layer

**Goal:** Establish the visual foundation all menus render on top of: ROM texture extraction pipeline, pdgui_theme system, optional scan-line post-processing.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_theme.cpp/.h` | Create | Theme system: color palettes, texture refs, tint separation |
| `rom_texture_loader.cpp/.h` | Create or Extend | ROM → decoded pixels → GL texture → ImTextureID (extends D5.0a spike) |
| `pdgui_scanline.cpp/.h` | Create | Optional scan-line/CRT post-process pass |
| `pdgui_renderer.cpp` | Modify | Integrate theme + scan-line into render pipeline |
| `asset_catalog.cpp` | Modify | Register ROM textures as catalog assets |

**Estimated LOC:** ~400 (150 theme system, 120 ROM loader extension, 80 scan-line, 50 renderer integration)

**Key Design Decisions:**

1. **Theme/tint separation** (constraint): The theme defines structural colors (backgrounds, borders, text). Tint is a per-element overlay applied on top. They must be independent — changing a tint doesn't change the theme, and vice versa.

2. **ROM texture pipeline:** Extends the D5.0a spike. ROM textures are decoded once at startup, uploaded to GL, and registered in the catalog as `base:tex_<name>`. The catalog ID is what menus reference — never raw texture pointers.

3. **Scan-line pass:** Optional post-process. Renders menu content to an FBO, applies horizontal scan-line darkening + slight bloom, composites to screen. Toggled in settings. Default: off.

4. **Theme as catalog asset:** The active theme is itself a catalog asset (`base:theme_default`). Mods can provide alternate themes. Theme files are JSON: palette definitions, texture references (by catalog ID), font selections.

**Acceptance Criteria:**
- [ ] ROM textures extracted and available as GL textures via catalog IDs
- [ ] pdgui_theme loads a theme definition and provides palette/texture accessors
- [ ] All existing pdgui menus render using theme colors (no hardcoded ImGui colors)
- [ ] Scan-line pass renders correctly when enabled, no visual artifacts
- [ ] Theme can be swapped at runtime (hot-reload from catalog)
- [ ] D5.0a spike functionality preserved and integrated (not duplicated)

---

### P4: UI Texture Mod

**Goal:** Full texture customization tooling. Extract OG N64 textures into a `base_ui` mod package. Provide an editor with 9-slice, overlay effects, and font override.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `mods/base_ui/mod.json` | Create | Manifest for base UI texture package |
| `mods/base_ui/textures/` | Create | Extracted ROM textures as PNGs |
| `pdgui_menu_texture_editor.cpp` | Create | 9-slice editor, effect overlay, font tools |
| `pdgui_menu_texture_editor.h` | Create | Public API |
| `nineslice_renderer.cpp/.h` | Create | 9-slice rendering engine |
| `overlay_effects.cpp/.h` | Create | Caustic/animated mask, border effect mask |
| `font_manager.cpp/.h` | Create or Extend | TTF loading, glow/shadow rendering |
| `rom_texture_extractor.cpp` | Create | Batch extract ROM textures to PNG (tool/offline) |

**Estimated LOC:** ~800 (250 9-slice, 200 effects, 150 editor UI, 100 font manager, 100 extractor)

**Key Design Decisions:**

1. **9-slice system:** Each texture has a 9-slice definition: 4 corner rects, 4 edge rects (stretch or tile, configurable per edge), 1 interior rect (stretch or tile). The ruler editor lets users drag dividers on a texture preview to define the slices. Stored as JSON alongside the texture.

2. **Overlay effects:**
   - **Caustic/animated mask:** A grayscale animation texture (spritesheet or sequence) multiplied over the base texture. Speed, opacity, blend mode configurable.
   - **Effect mask for borders:** A second image used as a mask — only the border regions (defined by 9-slice edges) receive the effect. Interior is untouched.
   - Both effects are optional and composited in order: base → 9-slice render → border effect → caustic overlay → text.

3. **Text rendering on top of effects:** Text is always the final compositing layer. Font override allows bundling a .ttf in the mod. Glow and shadow are rendered as blurred copies behind the text with configurable color, offset, and strength.

4. **Extraction tool:** A headless utility that reads the ROM, decodes all UI textures, and writes PNGs to `mods/base_ui/textures/`. Run once to bootstrap the base_ui mod. Not part of the runtime — build tool only.

**Acceptance Criteria:**
- [ ] ROM UI textures extracted to `base_ui` mod as PNGs with catalog IDs
- [ ] 9-slice ruler editor: drag dividers, see live preview of stretched/tiled rendering
- [ ] 9-slice definitions save/load as JSON per texture
- [ ] Caustic overlay animates correctly over base texture
- [ ] Border effect mask applies only to 9-slice edge/corner regions
- [ ] Text renders on top of all effects with correct z-order
- [ ] Font override loads bundled .ttf, applies glow/shadow with sliders
- [ ] All edits round-trip: save → close → reopen → identical state
- [ ] `base_ui` mod can be disabled; menus fall back to base theme textures

---

### P5: Theme Creation Interface

**Goal:** A user-facing color palette editor that works with the theme system from P3 and the texture tools from P4.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_menu_theme_editor.cpp` | Create | Palette editor UI |
| `pdgui_menu_theme_editor.h` | Create | Public API |
| `pdgui_theme.cpp` | Modify | Add per-element color mapping, live preview hooks |
| `pdgui_menu_settings.cpp` | Modify | Fix theme buttons not applying (D5.6 partial) |

**Estimated LOC:** ~300 (180 editor UI, 70 theme system extension, 50 settings fix)

**Key Design Decisions:**

1. **Per-element color mapping:** Each UI element type (button, panel, border, text, highlight, etc.) has a named color slot. The editor shows all slots with color pickers. Changes preview live — no "apply" step needed.

2. **Theme as mod asset:** Edited themes save as mod packages with a theme JSON file. Users can share themes as mods.

3. **Settings theme buttons fix:** The existing Settings screen has theme preset buttons that don't apply. Root cause: they set a theme name but don't trigger catalog reload. Fix: button handler calls `pdgui_theme_apply(catalog_id)` which reloads from catalog.

**Acceptance Criteria:**
- [ ] Color palette editor shows all UI element color slots
- [ ] Color pickers update live preview immediately
- [ ] Themes save as mod packages with valid manifests
- [ ] Themes load from catalog and apply correctly
- [ ] Settings screen theme buttons work (apply theme on click)
- [ ] Custom theme persists across restarts

---

### P6: Agent Create Preview Panel

**Goal:** Visual preview of the selected body + head combination in the agent/bot creation screen.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_menu_agent_create.cpp` | Modify | Add preview panel |
| `model_preview_renderer.cpp/.h` | Create | Render body+head model to FBO → ImTextureID |
| `pdgui_renderer.cpp` | Modify | Support model preview FBO in render pipeline |

**Estimated LOC:** ~250 (150 model renderer, 100 UI integration)

**Key Design Decisions:**

1. **Render approach:** Render the selected body+head model combination to an offscreen FBO at fixed resolution (256×512). Convert to ImTextureID. Display in an ImGui::Image widget alongside the selection dropdowns.

2. **Update trigger:** Re-render only when body or head selection changes (event-driven, not per-frame).

3. **Lighting:** Simple 3-point lighting setup, neutral background. No need for scene-accurate lighting — this is a preview, not gameplay.

**Acceptance Criteria:**
- [ ] Preview panel shows current body+head selection
- [ ] Preview updates when either dropdown changes
- [ ] No per-frame rendering cost when selection is static
- [ ] Models render with reasonable lighting and framing
- [ ] Panel integrates cleanly into existing agent create layout

---

### P7: D5.2 — Mission Select Redesign

**Goal:** Two-panel layout with mission list, briefing text, objectives, and difficulty selector. Fixes B-91, B-96, B-90.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_menu_mission_select.cpp` | Create or Rewrite | Full screen redesign |
| `pdgui_menu_mission_select.h` | Create | Public API |
| `mission_data.cpp/.h` | Modify | Expose briefing text, objectives, difficulty data |

**Estimated LOC:** ~350

**Key Design Decisions:**

1. **Two-panel layout:** Left panel = mission list (scrollable, selectable). Right panel = briefing text, objective checklist, difficulty dropdown, "Start" button. Panels use theme system from P3.

2. **Objective display:** Objectives shown as a checklist. Completed objectives (from save data) show checkmarks. Difficulty selector filters which objectives are shown (some are difficulty-specific).

**Bug Fixes Included:**
- **B-91:** Mission select layout/navigation issues → resolved by full redesign
- **B-96:** Difficulty selector not working → new implementation with proper data binding
- **B-90:** Briefing text missing/truncated → proper text wrapping and scroll in right panel

**Acceptance Criteria:**
- [ ] Two-panel layout renders correctly at all supported resolutions
- [ ] Mission list is scrollable, keyboard/gamepad navigable
- [ ] Selecting a mission populates briefing, objectives, and difficulty
- [ ] Difficulty selector filters objectives correctly
- [ ] "Start" button launches selected mission at selected difficulty
- [ ] B-91, B-96, B-90 verified fixed
- [ ] Screen integrates into push/pop menu stack

---

### P8: D5.3 — Pause Menu + Sub-Screens

**Goal:** In-game pause menu with Abort/Restart, objective checklist, and inventory display. Fixes B-93, B-98.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_menu_pause.cpp` | Create or Rewrite | Pause overlay and sub-menu navigation |
| `pdgui_menu_pause.h` | Create | Public API |
| `pdgui_menu_objectives.cpp` | Create | Objective checklist sub-screen |
| `pdgui_menu_inventory.cpp` | Create | Inventory display sub-screen |

**Estimated LOC:** ~300

**Key Design Decisions:**

1. **Overlay rendering:** Pause menu renders as a semi-transparent overlay on the frozen game frame. Uses theme system for colors.

2. **Sub-screen navigation:** Pause menu is a hub with buttons: Resume, Objectives, Inventory, Abort Mission, Restart Mission. Each sub-screen pushes onto the menu stack. Back returns to pause hub.

3. **Abort/Restart confirmation:** Both actions show a confirmation dialog before executing. No accidental aborts.

**Bug Fixes Included:**
- **B-93:** Pause menu navigation broken → full reimplementation
- **B-98:** Inventory not accessible from pause → new inventory sub-screen

**Acceptance Criteria:**
- [ ] Pause overlay renders correctly over frozen game frame
- [ ] All sub-screens accessible and navigable via gamepad/keyboard
- [ ] Abort and Restart show confirmation dialogs
- [ ] Objective checklist reflects current mission progress
- [ ] Inventory shows current weapons/items
- [ ] B-93, B-98 verified fixed
- [ ] Resume returns to gameplay with correct input context

---

### P9: D5.4 Remainder + D5.6 Settings/QoL

**Goal:** Mission complete screen, online lobby tab gating, settings text overlap fixes.

**Files to create/modify:**
| File | Action | Notes |
|---|---|---|
| `pdgui_menu_mission_complete.cpp` | Create | Mission complete/debrief screen |
| `pdgui_menu_lobby.cpp` | Modify | Tab gating for online lobby |
| `pdgui_menu_settings.cpp` | Modify | Text overlap fixes, theme button fix (if not done in P5) |

**Estimated LOC:** ~250 (150 mission complete, 50 lobby tabs, 50 settings fixes)

**Acceptance Criteria:**
- [ ] Mission complete screen shows stats, time, objectives completed
- [ ] Lobby tabs correctly gated based on connection/host status
- [ ] Settings text no longer overlaps at any resolution
- [ ] All screens use theme system consistently

---

### P10: D5.7 — OG Menu Removal Pass

**Goal:** Remove all legacy N64 menu code paths. Every screen now uses pdgui exclusively.

**Files to modify:**
| File | Action | Notes |
|---|---|---|
| 28+ unregistered OG screen files | Remove or Gut | Strip legacy rendering, keep data logic if needed |
| 3 forced OG screen references | Remove | Eliminate forced fallbacks |
| `menu_router.cpp` (or equivalent) | Modify | Remove OG menu dispatch paths |
| `CMakeLists.txt` | Modify | Remove OG menu source files from build |

**Estimated LOC:** ~200–400 (mostly deletion, some routing cleanup)

**Key Design Decisions:**

1. **Data preservation:** Some OG screens have data-loading logic intertwined with rendering. Extract any data logic into standalone functions before deleting the screen code.

2. **Phased removal:** Remove screens in groups of 5-7 per session, build-verify between groups. Don't delete everything at once — catch regressions incrementally.

3. **Forced fallback elimination:** The 3 forced OG screens are cases where the engine bypasses the menu router and directly calls OG code. Each needs its call site identified and redirected to the pdgui equivalent.

**Acceptance Criteria:**
- [ ] Zero OG menu screens remain in the build
- [ ] All menu navigation uses pdgui menu stack exclusively
- [ ] No dead code from OG menus in the source tree
- [ ] Build compiles cleanly with OG files removed
- [ ] Full playthrough test: main menu → mission select → gameplay → pause → complete → return — all pdgui

---

## 3. Session Breakdown

Each session targets ~30–60 minutes of focused implementation work. Sessions are grouped under their phase.

---

### Phase 1: Mod Menu (5 sessions)

| Session | Work | Output |
|---|---|---|
| **S1.1** | Mod manifest schema + mod_manager filesystem scan | `mod_manifest.h`, `mod_manager.cpp/.h` — scans `mods/`, parses `mod.json`, returns mod list |
| **S1.2** | Mod enable/disable state + persistence | `mods-enabled.json` read/write, enable/disable toggle logic, load ordering |
| **S1.3** | Catalog integration — mod asset registration/deregistration | `asset_catalog.cpp` changes: register mod assets on enable, remove on disable, priority layering |
| **S1.4** | Mod menu UI — list, toggle, info display | `pdgui_menu_mods.cpp/.h` — scrollable mod list, toggle buttons, detail panel, error display |
| **S1.5** | Main menu integration + error handling + end-to-end test | Hook into `pdgui_menu_main.cpp`, test with dummy mod, invalid manifest handling, size threshold prompt |

### Phase 2: Bot Name Dictionary Mod (3 sessions)

| Session | Work | Output |
|---|---|---|
| **S2.1** | Bot name data as catalog asset + generator refactor | `base:botnames_default` asset, `botname_generator.cpp` loads from catalog instead of hardcoded |
| **S2.2** | Bot name editor UI — pools, add/remove, test button | `pdgui_menu_botnames.cpp/.h` — string editor, append/replace toggle, generate button, combinatorial count |
| **S2.3** | Save/load round-trip + mod menu "Edit" action + integration test | Save writes JSON, load reads back, mod menu launches editor, disable mod → fallback to base names |

### Phase 3: D5.0 Visual Theme Layer (4 sessions)

| Session | Work | Output |
|---|---|---|
| **S3.1** | pdgui_theme system — palette definitions, color accessors | `pdgui_theme.cpp/.h` — theme JSON schema, load/parse, named color slot accessors |
| **S3.2** | ROM texture loader extension — batch extract, catalog registration | Extend D5.0a spike to full pipeline: all UI textures → GL → catalog as `base:tex_*` |
| **S3.3** | Theme application — retrofit existing menus to use theme colors | Update all `pdgui_menu_*.cpp` to use `pdgui_theme_color()` instead of hardcoded ImGui colors |
| **S3.4** | Scan-line post-process pass + theme hot-reload | `pdgui_scanline.cpp/.h`, FBO render path, settings toggle, theme swap at runtime |

### Phase 4: UI Texture Mod (6 sessions)

| Session | Work | Output |
|---|---|---|
| **S4.1** | ROM texture extractor tool (headless, batch PNG export) | `rom_texture_extractor.cpp` — reads ROM, decodes UI textures, writes PNGs to `base_ui` mod |
| **S4.2** | 9-slice data model + renderer | `nineslice_renderer.cpp/.h` — 9-slice definition (JSON), stretch/tile per region, render to ImGui |
| **S4.3** | 9-slice ruler editor UI | `pdgui_menu_texture_editor.cpp` — drag dividers on texture preview, live 9-slice preview |
| **S4.4** | Overlay effect system — caustic + animated mask | `overlay_effects.cpp/.h` — spritesheet animation, blend modes, per-frame compositing |
| **S4.5** | Border effect mask + text compositing layer | Effect mask applied to 9-slice edges only, text renders on top of all effects, font override |
| **S4.6** | Font manager — TTF loading, glow/shadow + integration test | `font_manager.cpp/.h` — load bundled .ttf, glow/shadow with color+strength, full round-trip test |

### Phase 5: Theme Creation Interface (2 sessions)

| Session | Work | Output |
|---|---|---|
| **S5.1** | Palette editor UI — color pickers for all element slots, live preview | `pdgui_menu_theme_editor.cpp/.h` — per-element color editing, immediate preview |
| **S5.2** | Theme save-as-mod + Settings theme button fix | Save theme as mod package, fix Settings buttons to call `pdgui_theme_apply()`, persistence test |

### Phase 6: Agent Create Preview Panel (2 sessions)

| Session | Work | Output |
|---|---|---|
| **S6.1** | Model preview renderer — FBO setup, body+head render, ImTextureID output | `model_preview_renderer.cpp/.h` — offscreen render, 3-point lighting, neutral background |
| **S6.2** | UI integration — preview panel in agent create screen, event-driven updates | Modify `pdgui_menu_agent_create.cpp`, re-render on selection change only |

### Phase 7: D5.2 Mission Select (3 sessions)

| Session | Work | Output |
|---|---|---|
| **S7.1** | Two-panel layout + mission list with scroll/navigation | Left panel: mission list from data, keyboard/gamepad navigation, selection highlight |
| **S7.2** | Right panel — briefing, objectives, difficulty selector | Briefing text with wrapping, objective checklist, difficulty dropdown filtering |
| **S7.3** | Data binding + bug verification (B-91, B-96, B-90) | Start button launches mission, verify all three bugs fixed, menu stack integration |

### Phase 8: D5.3 Pause Menu (3 sessions)

| Session | Work | Output |
|---|---|---|
| **S8.1** | Pause overlay + hub navigation | Semi-transparent overlay, Resume/Objectives/Inventory/Abort/Restart buttons |
| **S8.2** | Objective checklist + inventory sub-screens | Objectives from current mission state, inventory from player state, push/pop navigation |
| **S8.3** | Abort/Restart confirmation + bug verification (B-93, B-98) | Confirmation dialogs, verify bugs fixed, input context restoration on resume |

### Phase 9: D5.4 Remainder + D5.6 (2 sessions)

| Session | Work | Output |
|---|---|---|
| **S9.1** | Mission complete screen + lobby tab gating | Debrief stats display, lobby tab visibility based on connection state |
| **S9.2** | Settings text overlap fix + final QoL pass | Text layout fixes at all resolutions, theme button verification, general polish |

### Phase 10: D5.7 OG Menu Removal (3 sessions)

| Session | Work | Output |
|---|---|---|
| **S10.1** | Remove first batch of OG screens (10 files) + build verify | Delete/gut legacy screen files, update CMakeLists, verify build |
| **S10.2** | Remove second batch (10 files) + forced fallback elimination | Delete more screens, redirect the 3 forced OG call sites to pdgui |
| **S10.3** | Remove remaining screens + final cleanup + full playthrough test | Clear remaining 8+ files, dead code sweep, end-to-end navigation test |

---

## 4. Summary

| Phase | Sessions | Est. LOC | Bugs Fixed | Key Deliverable |
|---|---|---|---|---|
| P1: Mod Menu | 5 | ~500 | — | Mod pipeline gateway |
| P2: Bot Names | 3 | ~350 | — | First real mod, catalog round-trip proven |
| P3: D5.0 Theme | 4 | ~400 | — | Visual foundation for all menus |
| P4: UI Texture | 6 | ~800 | — | 9-slice, effects, font override |
| P5: Theme Editor | 2 | ~300 | — | User-facing theme creation |
| P6: Agent Preview | 2 | ~250 | — | Body+head visual preview |
| P7: D5.2 Mission | 3 | ~350 | B-91, B-96, B-90 | Mission select redesign |
| P8: D5.3 Pause | 3 | ~300 | B-93, B-98 | Pause menu + sub-screens |
| P9: D5.4+D5.6 | 2 | ~250 | — | Mission complete, settings QoL |
| P10: D5.7 OG | 3 | ~300 | — | Zero legacy menus remain |
| **Total** | **33** | **~3,800** | **5 HIGH** | **Full mod pipeline + all D5 UI** |

**Critical path:** P1 → P2 (validates pipeline) → P3 (visual foundation) → P7+P8 (parallel, biggest bug-fix impact) → P10 (cleanup).

**Parallelization opportunities:** P7 and P8 can run in parallel if two sessions are available. P6 can be done any time after P3. P5 can overlap with P6.

**Remaining MED/LOW bugs (14):** Not directly targeted in this plan. Many will be incidentally fixed by the screen rewrites. After P10, do a dedicated bug triage pass to assess which remain.

---

## 5. Open Questions for Game Director

1. **Mod manifest versioning:** Should mods declare a minimum PD2 engine version for compatibility? If yes, what's the version scheme?
2. **Mod conflicts:** If two enabled mods override the same catalog ID, should the later one in load order win silently, or should the UI warn about conflicts?
3. **Scan-line pass default:** Should the CRT scan-line effect default to on (for nostalgia) or off (for clean look)? Current plan: off.
4. **Theme preset shipping:** Should PD2 ship with 2-3 theme presets beyond default? If so, what aesthetic direction? (e.g., "N64 Classic," "Modern Dark," "High Contrast")
5. **Agent preview rotation:** Should the body+head preview allow mouse-drag rotation, or is a fixed front-facing view sufficient?
