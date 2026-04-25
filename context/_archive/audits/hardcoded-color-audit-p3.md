# Hardcoded Color Audit — P3 Visual Theme Layer

**Date:** 2026-04-08
**Scope:** All `pdgui_menu_*.cpp` files
**Tool:** Grep for `IM_COL32()`, `ImVec4` color literals, `PushStyleColor()`

## Summary

- **360+ hardcoded color instances** across 14 menu files
- **19 migrated** to palette accessors in this pass (dark bg fill + title text)
- **~341 remaining** — classified below by category

## Migrated (P3)

| Pattern | Count | Replacement |
|---------|-------|-------------|
| `IM_COL32(8, 8, 16, 255)` (dark bg fill) | 9 files | `pdguiPalImU32(PDPAL_BODYBG, 255)` |
| `IM_COL32(255, 255, 255, 255)` (title text) | 10 files | `pdguiPalImU32(PDPAL_TITLEFG, 255)` |

## Remaining — Intentional (Do NOT migrate)

These are semantic colors that should stay hardcoded:

### Status/Feedback Colors
- **Error red**: `ImVec4(1.0f, 0.3f, 0.3f, 1)`, `IM_COL32(180, 60, 60, 200)` — error states
- **Success green**: `ImVec4(0.3f, 1.0f, 0.3f, 1)`, `IM_COL32(50, 190, 70, 230)` — success states
- **Warning yellow**: `ImVec4(1.0f, 0.8f, 0.2f, 1)` — warnings
- **Info blue**: `ImVec4(0.4f, 0.8f, 1.0f, 1.0f)` — info text

### Game-Specific Colors
- **Difficulty colors** (solomission.cpp): Agent green, Special Agent blue, Perfect Agent gold
- **Team colors** (pausemenu.cpp): Red, Blue, Green, Yellow, Orange, Purple, Grey, White — 8 fixed team colors
- **Medal colors** (endscreen.cpp): Gold, Silver, Bronze placement indicators
- **Update status** (update.cpp): Download progress, version comparison colors

### Overlay/Dim Colors
- `IM_COL32(0, 0, 0, 160)` / `IM_COL32(0, 0, 0, 140)` — screen dim overlays (endscreen, pause)
- These could potentially theme but are intentionally neutral black

## Remaining — Candidates for Future Migration (P5)

These represent theme-able surfaces that could use palette colors in a future pass:

### Panel/Window Backgrounds (per-file)
- `pdgui_menu_agentcreate.cpp:269` — `IM_COL32(10, 15, 30, 240)` panel bg
- `pdgui_menu_agentselect.cpp:441` — `IM_COL32(10, 15, 30, 240)` panel bg
- `pdgui_menu_room.cpp:2581` — `IM_COL32(20, 20, 30, 200)` custom panel bg

### Border/Accent Colors
- `pdgui_menu_agentcreate.cpp:271` — `IM_COL32(60, 100, 180, 200)` panel border
- `pdgui_menu_agentselect.cpp:448` — `IM_COL32(80, 120, 200, 200)` panel border
- `pdgui_menu_mainmenu.cpp:982-983` — `IM_COL32(80, 120, 200, 80)` blue accent

### PushStyleColor Overrides
- ~101 `PushStyleColor()` calls override ImGui style colors inline
- Most are button state colors (Button, ButtonHovered, ButtonActive)
- These automatically use palette colors via pdguiApplyPdStyle() when not overridden
- Migration would remove the overrides, letting the palette shine through

## Recommended Next Steps (P5: Theme Creation Interface)

1. Define semantic color roles beyond palette (e.g., `PDPAL_STATUS_ERROR`, `PDPAL_STATUS_SUCCESS`)
2. Migrate PushStyleColor calls for buttons that just darken/lighten palette colors
3. Leave team colors, difficulty colors, and medal colors as intentional constants
