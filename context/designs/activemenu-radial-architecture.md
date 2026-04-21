# Active menu (weapon / gadget radial) — architecture

> **Created**: 2026-04-21  
> **Status**: LEGACY GBI (not ImGui)

---

## What this is

The **in-game weapon and gadget radial** (3×3 diamond layout, D-pad down / hold-open behaviour, `radialmenuspeed` in settings) is the **active menu** subsystem. It is **not** the small circular **hold-progress** ring used for “hold to interact” (`pdgui_hold_ring` — see [pdgui-hold-ring.md](pdgui-hold-ring.md)).

---

## Code map

| Piece | Role |
|-------|------|
| `src/game/activemenu.c` | State: `g_AmMenus[]`, `g_AmMapping[]` (weapon set → slot), **`amRender()`** — draws the diamond background (GBI tris), slot labels, selection pulse, co-op / bot overlays. |
| `src/game/activemenutick.c` | **Input**: mouse deltas, aim axes while open (`B-202`), stick-driven cursor for selection. |
| `src/include/game/activemenu.h` | Public `am*` APIs (`amOpen`, `amClose`, `amApply`, `amRender`, …). |
| `src/game/lv.c` | Gameplay render chain calls **`amRender(gdl)`** when the active menu should show. |
| `src/game/bondmove.c` | Suppresses normal aim while `activemenumode != AMMODE_CLOSED` so the radial owns look input. |
| `g_Vars.currentplayer->activemenumode` | `AMMODE_*` — closed / view / edit, etc. |

**Settings**: `Game.PlayerN.RadialMenuSpeed` → `g_PlayerExtCfg[].radialmenuspeed` (`port/src/main.c`, options UI).

---

## Replacing or unifying with a custom ImGui radial

Today there is **one** implementation path: **matching N64** via **Gfx** (`amRender`). A PC-native **ImGui** radial that shows the **same slots and labels** would need to:

1. **Read the same data** as `amGetSlotDetails` / slot iteration in `amRender` (weapon names, flags, focused slot, etc.), or call thin C accessors added for that purpose.
2. **Stay in sync with input** already handled in `activemenutick.c` (or refactor tick to drive a single selection model both UIs consume).
3. **Decide render policy**: overlay ImGui in `pdguiRender` when `activemenumode != CLOSED` **instead of** or **on top of** `amRender` (duplicating visuals is bad; turning off `amRender` for PC requires a clean gate).

This is a **feature-sized** port task, not a drop-in of `pdgui_hold_ring` (which only draws a progress arc, not a multi-slot wheel).

---

## Related

- [hud-layer-order.md](hud-layer-order.md) — gameplay vs menu draw ordering.
- [pdgui-hold-ring.md](pdgui-hold-ring.md) — **different** UI (hold-to-use progress).
