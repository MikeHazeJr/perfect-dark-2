# Menu controller + mouse input — design constraints

> **Status**: DESIGN (2026-04-20). UX contract for ImGui menus (`port/fast3d/pdgui_menu_*.cpp`).
> **Related**: [menu-stack-architecture.md](menu-stack-architecture.md) (I3/I4 leaf input, modals), [input-authority-and-menu-pool-2026-04-13.md](input-authority-and-menu-pool-2026-04-13.md), audit note on progressive focus in [../audits/2026-04-19-menus-input.md](../audits/2026-04-19-menus-input.md).

This document defines how **controller** and **mouse** should behave together so menus feel like one system: short paths, predictable back navigation, and no unnecessary cursor travel. Implementation may use ImGui focus APIs, action-map edges (`ACTION_USE`, `ACTION_CANCEL_USE`, menu-nav actions), and the existing `pdguiDriveImGuiNav()` bridge — **not** raw SDL mouse calls from menu code (see project constraint: input context stack owns mouse mode).

---

## 1. Goals

| Goal | Meaning |
|------|---------|
| **Progressive focus** | The primary confirm action (“Submit”) moves the player through a **short sequence** of meaningful controls (list → modal → options → final action) without cross-panel hunting. |
| **One mental model** | Controller Submit and mouse activation follow the **same stages**; hybrid users are not punished. |
| **Back is safe** | Cancel / Back closes the current layer and restores the parent with **selection preserved**. |
| **Defaults from saves** | When opening a detail layer, pre-select values derived from **save-backed progress** so repeat visits are faster. |

---

## 2. Must

1. **Leaf owns interaction** — While a modal or child panel is open, it receives confirm/cancel and nav input; the parent does not keep a separate “live” focus path (aligns with menu-stack **I4**). Visuals may still show the parent behind the modal.

2. **Submit advances the flow** — On screens that use staged navigation, **Submit** on a focused list item must either open the next layer **or** activate the primary action on that layer. Dead submits (no focus move, no default applied) are not allowed for primary controls.

3. **Cancel returns with context** — From a modal, **Back / Cancel** closes the modal, returns focus to the parent control that launched it, and keeps that item **selected/highlighted**. **Back** means `ACTION_CANCEL_USE` (default: **B** on controller, **Esc** on keyboard). The same focus restoration applies when closing via the **window title-bar close button** (the close glyph on the **window chrome**, not the controller face **X** or the **keyboard X** key). Any other explicit **Back** UI control on the modal should behave the same. Focus returns to the **invoking control** on the parent screen (e.g. **Player Handicaps** if that opened the modal), not a generic default.

4. **Pointer sync on programmatic focus** — When focus jumps to a control because of modal open, stage change, or default selection, **move the OS cursor** to that control so mouse users are not left pointing at an unrelated region. (Controller users rely on ImGui navigation focus.)

5. **Save-aware defaults** — When a modal presents a small set of peer options (e.g. difficulty), the **default focus** must match the best available progress signal (e.g. highest difficulty already completed for that mission), unless a stronger explicit rule exists.

6. **Input context discipline** — Menus adjust mouse mode only through the **input context stack**; no `SDL_ShowCursor` / `SDL_SetRelativeMouseMode` in menu code.

### 2.1 Sanity audit: Settings → Controls (`pdgui_menu_mainmenu.cpp`, 2026-04-20)

| Check | Result |
|-------|--------|
| **Raw SDL mouse APIs** | **None** on the Controls tab path. File uses `SDL_GetTicks` / `SDL_PushEvent(QUIT)` elsewhere only (timing, quit). Drag/drop rebinding uses ImGui hit-testing, not `SDL_WarpMouse` / `SDL_CaptureMouse`. |
| **Gamepad → menu nav** | Settings sub-tabs: **LB/RB** are translated by **`pdguiDriveImGuiNav()`** to **PageUp/PageDown**; `renderSettingsView` polls **`ImGuiKey_PageUp` / `PageDown`** (not raw `ImGuiKey_Gamepad*`, because `NavEnableGamepad` is off by design). |
| **Focus / children** | Controller tab uses **`ImGuiChildFlags_NavFlattened`** on scroll children and hold-override table so keyboard/gamepad nav crosses panels per prior work. |
| **Mouse mode** | Unchanged: **`inputCtxSyncMouseMode`** at frame end owns cursor/relative mode; menu code does not call SDL mouse mode APIs. |

7. **Locked missions stay out of lists** — Mission (and challenge) pickers **must** **filter out** locked entries entirely. Do not render locked items as visible grey/disabled rows; the player only sees missions they can launch, subject to each tab’s rules (see §5).

8. **Multi-column nav without panel stops** — On split layouts (e.g. Combat Simulator: options column + players/bots column), **child windows / panels are layout containers only**. Horizontal nav (**Left** / **Right**) **must** move focus between the **last/next real control** in the adjacent column. The player **must not** need to focus an empty panel or group header just to cross from left to right.

---

## 3. Should

1. **Short lists per tab** — Within each tab, keep the visible set small and actionable (e.g. campaign: completed + next; modded: apply pack/creator filters before building tiles). Progressive-focus behavior applies inside whichever tab is active.

2. **Peer options in one row** — When options are mutually exclusive and few (e.g. three difficulties), **should** lay them out **horizontally** so one D-pad left/right sweep compares them; reserve vertical space for briefing/objective text.

3. **Large hit targets for primary lists** — Campaign-style pickers **should** use large, centered controls (e.g. “Mission [n]: Title”) so gamepad nav and mouse both land reliably.

4. **Document the staged sequence per screen** — Each complex menu **should** have a short bullet sequence in code comments or this file’s reference sections so future edits do not break the flow.

5. **Contextual glyph hints** — When focus sits on a row that has **extra actions** (e.g. multi-select, context menu), **should** show **action glyphs + short labels** in a consistent overlay region (e.g. lower-right), using the project glyph helpers (`pdgui_glyphs` + action map primary bindings). Defaults in copy are examples — bind to the real `InputAction` values (e.g. multi-select vs menu-open) in code.

6. **Smooth scrolling** — List/dropdown scrollbars in menu UIs **should** use **smooth** scrolling (continuous motion), not chunky discrete steps — including Weapon Set, Arena/Scenario dropdowns, music lists, and other scrollable regions in match setup and related modals.

---

## 4. Must not

1. **No independent two-cursor experience** — Do not require the player to drag the mouse across unrelated panels when controller nav can complete the same task in a straight sequence (same stages for both devices).

2. **No orphan modal state** — Closing a modal must not leave an inconsistent selected index on the parent or a hidden focus target.

3. **No raw IP / wire concerns** — Unrelated to this doc; see `context/constraints.md`.

---

## 5. Reference example: Solo Mission Select (normative for this screen)

This section captures the agreed flow so implementations can be checked against it.

### 5.1 Top-level tabs and theme

Mission Select is organized by **tabs** (not a single monolithic list):

| Tab | Role |
|-----|------|
| **Campaign** (working name) | Main solo progression: horizontal mission strip + modal flow below. |
| **Challenges** | Separate challenge mission set; same input conventions as campaign where applicable. |
| **Modded missions** | Missions from mods; additional **sort/filter** by **mod pack** and/or **creator** so long mod lists stay scannable. |

- **Challenges tab — Gold highlight**: The active **Challenges** tab uses a **gold** accent for its selected/highlight state. **Gold** is a **new semantic theme color** (extend the theme palette JSON + loader + accessors in the same family as existing semantic tints, e.g. success/danger/info), not a one-off `IM_COL32` in this menu only.
- **Default tab on open**: **Challenges** is selected/highlighted **gold by default** when entering Mission Select (unless a stronger rule overrides, e.g. first-run tutorial — document in code if so).
- **Locked missions**: **Never appear** in any tab’s mission list; they are **filtered out**, not rendered as grey rows.

### 5.2 Data and layout (within the active tab)

- On load, populate mission metadata as today (briefing, objectives, etc.).
- **List content** (campaign tab): show **completed missions plus the next mission** only; **locked missions are filtered out**.
- **Modded tab**: apply pack/creator filters before building the same class of list (large tiles / horizontal strip — match campaign presentation unless challenge/mod content needs a variant).
- **Mission list presentation**: a **horizontal**, centered row of large entries labeled **`Mission [#]: [Mission Name]`** (subtitle-style). No split sidebar/detail layout for the initial pick — only missions for that tab’s filtered set.

### 5.3 Modal content

- **Submit** on a mission opens a **modal** for that mission.
- Modal header: mission name + info (briefing/objectives) at the top.
- **Difficulties**: three options **side by side** in a single row.
- **Default difficulty**: the **highest difficulty already beaten** for that mission (per save/progress). Focus and pointer move to that difficulty when the modal opens.

### 5.4 Staged actions

**Mouse (hybrid):**

- From the mission row, **two activations** from the list (e.g. select then confirm / double-click pattern per implementation) open the modal with the correct default difficulty and pointer on that difficulty, then flow focus toward **Start Mission** without cross-screen mouse travel.

**Controller:**

1. **Submit** on a mission → opens modal, default difficulty selected, pointer synced to that control.
2. **Submit** again → confirms difficulty choice and moves focus to **Start Mission** (implementation may treat one nav step as implicit if Submit on the default difficulty already maps to “commit row + move to Start”).
3. **Submit** on **Start Mission** → starts the mission at the chosen difficulty.

**Cancel:**

- From the modal, **Cancel** closes the modal, returns to the mission list with the same mission **still selected/highlighted**, and moves the cursor back to that mission control.

### 5.5 Consistency check

Any change to Mission Select should preserve: **tabs** (Campaign / Challenges / Modded) with **Challenges** gold themed and default-highlighted as specified, **locked missions filtered** (never listed), modded **pack/creator** filtering, short Submit path, save-backed default difficulty, horizontal difficulty row in the modal, modal back behavior, and pointer sync on open/close.

---

## 6. Reference example: Combat Simulator setup (normative for this screen)

Applies to the Combat Simulator / room match setup UI (primarily **`pdgui_menu_room.cpp`** solo room overlay and related **`pdgui_menu_mpsetup.cpp`** / **`pdgui_menu_mpadvanced.cpp`**, plus **`pdgui_menu_mpsettings.cpp`** for Select Tunes / playlist — presets, dropdowns, toggles, players/bots list, music, countdown).

### 6.1 Cross-panel navigation (controller)

- **Left column** (**Arena**, **Scenario**, **Weapon Set**, **Options** — see §6.7) and **right column** (players/bots list, Add Bot) behave as one logical form: **Right** from the last focused control in the left column moves to the appropriate control in the right column; **Left** from the first focusable in the right column returns to the left. **No separate “panel” focus** — only interactive widgets receive focus (aligns with §2.8).

### 6.2 Bots list — glyphs, Submit vs context

- When focus is on a **bot row**, show **glyph hints** (lower-right overlay, §3.5): e.g. multi-select (**Y** in default copy — wire to the real multi-select `InputAction`), and the **context-menu** binding (default controller face **X** / whatever `InputAction` opens the bot context menu — **not** window close or keyboard **X**).
- **Submit (A)** on a bot opens the **full bot settings / character flow** (dedicated window or main path), **not** the radial/context menu — the context menu is the **secondary** path (context-menu action).

### 6.3 Context menu copy

- Any item that **only randomizes the bot’s name** **must** read **“Random name”**, not “Re-roll” / “Reroll”, to match behavior.

### 6.4 Character / body picker — IDs, labels, grouping

- **No duplicate rows for the same selectable body** — duplicate display strings (e.g. “Dinner Jacket” ×4) **must** be fixed by stable **catalog-backed identity** for ImGui IDs and list contents so IDs stay unique and the list matches data (eliminates ImGui duplicate-ID errors).
- **No placeholder rows as characters** — strings such as **“Choose a head to load over”** or **“Need space for head”** **must not** appear as fake character names. Those flows **must** resolve to proper **catalog display names** (e.g. Skedar, Dr. Carroll) or be hidden until the UI can represent them correctly.
- **Grouping** — **Joanna** skin variants **should** appear under a single **Joanna** parent (submenu or tree), not as a flat alphabetical sprawl. **Maians**, **dataDyne**, and similar **should** group by **faction** or **base character** so related skins stay together.

### 6.5 Player handicaps

- Handicap choices **must** use **explicit outcome copy**, e.g. “Player takes **0%** damage”, “Player takes **400%** damage” (exact percentages/strings per design), not ambiguous labels without stating the effect.

### 6.6 Swat / Hardcore match option

- **Toggle** in match options (alongside other combat rules): when **on**, all match participants use **health-only** rules — **no shield** (or equivalent: shields off / non-regenerating shield treated as disabled per implementation). Same idea as “SWAT” modes in other FPS titles.
- **Must** be stored in **match configuration**, replicated to clients, and enforced **server-authoritatively** for dedicated/listen servers. Any **new wire field** requires a deliberate **`NET_PROTOCOL_VERSION` bump** and compatibility notes per `context/constraints.md` — track in implementation tasks, not in this UX doc alone.

### 6.7 Left panel organization (top-to-bottom)

The **left column** **must** follow this **order** and grouping:

1. **Arena**
2. **Scenario**
3. **Weapon Set**
4. **Options** (match toggles and derived controls)

Navigation order and screen reader / controller flow should follow this sequence unless a stronger accessibility rule requires otherwise.

### 6.8 Arena, Scenario, Weapon Set — dropdowns and lists

- **Weapon Set** **must** use the **same control pattern** as **Arena** and **Scenario**: a **dropdown** (or equivalent combo), not a separate preset list style if those two are dropdowns.
- **Weapon Set** entries **must** be **alphabetized** for scanning.
- **Scrolling** in these dropdowns (and elsewhere in this UI) **must** follow §3.6 — **smooth** scrolling, not chunky stepped scrollbars.

### 6.9 Teams — defaults, “together” vs “split”, colors, overrides

- When **Teams** is toggled **on**, **default** to **2 teams** (unless save/mode already specifies otherwise).
- Provide an additional control (pair of options or mode): **“Players Together”** vs **“Players Split”**:
  - **Players Together**: bot assignment / auto-sort tends to pit **bots against a united player side** (players on the same “human” coalition vs bots).
  - **Players Split**: bots are **distributed evenly across teams**, each **led by or anchored to a player** where applicable — when there are **more teams than players** (e.g. **3 teams**, **2 players**), **each player gets a team**, and the **remaining team(s)** receive **bots distributed evenly** across them.
- **Manual override**: players **must** be able to **freely change** any **bot’s team** and **their own team** in this menu; manual edits **override** the auto **Together/Split** behavior until the user resets or toggles the mode again (exact reset semantics in code).
- Team **labels in the UI** **must** use the **team color name** (or color + readable label derived from the color), **not** hard-coded **“Team 1”**, **“Team 2”**, etc.

### 6.10 Modal dismiss — focus return to parent

- Closing a **modal** or **child popup** via **Back** (`ACTION_CANCEL_USE` — B / Esc defaults) or the **title-bar close** on the **window** **must** return the user to the **match setup parent** with **focus and scroll position** restored to the **control that opened** the modal (e.g. back to **Player Handicaps** after closing that dialog). This does **not** refer to the controller face **X** or the **keyboard X** key. Aligns with §2.3.

### 6.11 Music / playlist UI (Select Tunes)

- **Source list**: populate from the **audio catalog**, including tracks from **enabled mods**. **Mod enablement** **should** be **persistent**; **new mods** **should** default to **enabled** when created (per existing mod-manager policy — keep consistent with catalog).
- **Row activation** (hover + click, or **A** / Submit on a **song row**): **adds the track to the playlist only** — **does not** start playback or preview.
- **Per-track Play** (dedicated play affordance beside the row): **may** play a **preview** **only until** one of: focus **leaves** the play control, the user **adds** that song to the playlist via the row action, or the user **closes** the modal / backs out.
- **Lifecycle**: when the music modal is **popped** (not visible), **no** preview/sample **may** continue — stop audio in `on_pop` / close path so a sample cannot outlive the menu (child must not leak sound).

### 6.12 Start Match — countdown (solo vs online)

- **Solo / offline Start Match**: start the standard **match start countdown** — e.g. **3… 2… 1… “Start”** — with **UX/UI audio** on the ticks (consistent with project SFX hooks).
- **Online Start Match**: **same** countdown **feel** and **timing** as solo — **not** a longer or divergent timer. **Must** fix current behavior where online uses a **much longer** countdown and feels **buggy**; solo and online should match unless net ready-gate requires a documented extra beat (if so, document in code — default is **parity**).

### 6.13 Consistency check

Changes to Combat Simulator / room / match setup should preserve: **§6.7** panel order; **Weapon Set** as alphabetized dropdown with **smooth** scroll; **Teams** defaults, **Together/Split**, color-based team names, manual overrides; **modal** close restoring focus to **invokers**; **music** list rules (catalog + enabled mods, row = playlist only, play = bounded preview, **no** sound after pop); **Start Match** countdown **parity** solo vs online with **UX audio**; plus earlier §6 rules (cross-panel nav, bots glyphs, character list, handicaps copy, **Swat/Hardcore**).

---

## 7. Applying this to other menus

When porting or refactoring a menu:

1. Identify the **stages** (e.g. list → confirm → destructive action).
2. Define **Submit** and **Cancel** for each stage and ensure **Cancel** restores the previous stage’s selection.
3. Define **defaults** from saves or mode rules before adding extra clicks.
4. If the screen mixes mouse and controller, **test pointer position** after every programmatic `SetKeyboardFocusHere` / nav jump.

Add new reference sections (§5, §6, …) as flows are agreed (e.g. MP lobby, Agent Select).
