# PDGUI hold ring + per-target use hold

> **Created**: 2026-04-21  
> **Status**: IMPLEMENTED (shared ring + tuning hooks)

> **Not the weapon / gadget radial.** That is the **active menu** (`amRender` in `activemenu.c`, GBI). See [activemenu-radial-architecture.md](activemenu-radial-architecture.md).

---

## Per-target hold length (case-by-case)

**Yes.** Long-press duration for **use / interact** is intentionally layered:

1. **Global / per-action** — `actionmapGetEffectiveHoldMs(ACTION_USE)` (controller settings + optional per-action overrides in the actionmap layer, `pd.ini`).
2. **Per-interactable** — `propInteractPromptHoldThresholdMs()` in `src/game/prop.c`: starts from (1), then adds **`extra`** ms per `g_InteractProp` category (weapon / door / obj / terminal, etc.). All extras default to **0** until a feature needs a longer or shorter hold; adjust **only** in that function (or add data-driven fields later) so **bondmove** and UI stay aligned.

**Bondmove / UI sync** — `propGetActionUseHoldThresholdMs()` returns the per-target value when `g_InteractProp` and `propInteractPromptLabel()` are both active; otherwise the global effective ms only. The interact HUD (`pdgui_interact_prompt.cpp`) uses `propInteractPromptHoldThresholdMs()` for the same numbers.

---

## Shared radial ring (ImGui)

**Implementation:** `port/include/pdgui_hold_ring.h` + `port/fast3d/pdgui_hold_ring.cpp`

- **`pdguiDrawHoldProgressRingAroundBox(ImDrawList *dl, boxMinX, boxMinY, boxW, boxH, progress)`**  
  Dim full track + clockwise accent arc around a rectangle (typically the key glyph pill). `progress` is **0..1** (e.g. from `actionHoldProgress(player, ACTION_USE, holdMs)`).

**Current call sites**

- `pdguiDrawActionPromptCenteredWithHold()` in `pdgui_glyphs.cpp` (in-world interact prompt).

**Elsewhere in the port**

- Other HUD/menu surfaces that need the **same** look should call `pdguiDrawHoldProgressRingAroundBox` with their own `progress` and pill bounds. Horizontal `ImGui::ProgressBar` widgets (downloads, score bars, etc.) are **not** this control — they stay as bars unless product asks for a ring.

**Data**

- Ring drawing is **presentational only**; it does not read the actionmap. Callers pass normalized progress; hold **duration** comes from `propGetActionUseHoldThresholdMs()` / `actionHoldProgress` as above.

---

## Related

- [hud-layer-order.md](hud-layer-order.md) — interact prompt gated with gameplay HUD (`pdguiIsActive`).
