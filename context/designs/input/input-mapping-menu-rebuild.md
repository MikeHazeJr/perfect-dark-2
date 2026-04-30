# Input Mapping Menu Rebuild (Priority P / B-252)

**Status:** DESIGN. Phase 1 only. Phase 2 (implementation) waits for Session B's
L pass to commit so the rebuild lands on the post-L menu layout standards.

**Author:** Session P, 2026-04-25.

**Companion:** `context/designs/contextual-input-schemes.md` (Priority J).
J defines the IMC inventory and activation rules. P uses J's IMCs as the
top-level organising axis of the rebuild.

---

## 1. Goal

The current Input Mapping menu (`renderSettingsControls` in
`port/fast3d/pdgui_menu_mainmenu.cpp`, lines 1497 to 2839) presents one flat
list of 51 actions, all read from `g_ImcGameplay`. Recent IMC work (J design,
forge IMC split, vehicle IMC) introduced four to five separate input contexts
that own different bindings, yet the UI still pretends only the gameplay IMC
exists.

The rebuild reorganises the menu so the user sees, per IMC mode, exactly which
actions are bound and what device input fires them. Hold versus tap variants
(Combat Sim Back tap vs Back hold for scorecard) become explicit sibling rows
with the hold threshold visible in the row label.

The structural change is the top-level grouping. The rebind capture flow,
right-click-to-clear, search filter, conflict highlighting, and reset-to-defaults
behaviours are preserved. Every existing convenience stays; only the
organisation and the per-tab scope change.

---

## 2. Current state audit

Source: `port/fast3d/pdgui_menu_mainmenu.cpp`, the "Input Mapping (S306 redesign
BATCH 1)" comment block.

| Aspect | Today |
|---|---|
| Outer container | Settings tab `Controls` |
| Sub-tabs | Two: `Keyboard & Mouse`, `Controller` |
| IMC scope | `g_ImcGameplay` only |
| Action set | 51 actions (4 axis actions excluded as analog-only) |
| Grouping | 9 groups: Movement, Aim, Combat, Weapons, Vehicle, Menu Nav, C-Buttons, D-Pad, System Hotkeys |
| Per-row layout | 3-column table: Action / Bind 1 / Bind 2 |
| Capture flow | Click bind cell, prompt asks for key/button, Esc cancels |
| Right-click | Clears that bind |
| Search | Live case-insensitive filter on display name |
| Conflict highlight | Red border when a VK appears in multiple actions in the same device column |
| Reset | Two buttons: Reset Keyboard & Mouse to Defaults / Reset Controller to Defaults; both write `g_ImcGameplay` only |

What is wrong with this for the post-J world:

1. Vehicle bindings live on `g_ImcVehicle`, not `g_ImcGameplay`. The current
   menu has Vehicle rows in its group list, but those rows read and write the
   gameplay IMC. The vehicle dispatch path (`g_ImcVehicle` activated on
   mount) reads its own bindings, so the menu has been rebinding a copy that
   nothing in vehicle mode actually consumes.
2. Forge actions live on `g_ImcForgeSession` and `g_ImcForge`. The current
   menu does not surface them at all (no group for forge). User can rebind
   `ACTION_FORGE_TOGGLE` only because it has an additional default on the
   gameplay IMC keyboard side (F7).
3. Menu navigation actions (`ACTION_MENU_*`) live on `g_ImcMenu` (and
   `g_ImcPauseMenu`). The current menu shows them under "Menu Navigation" but
   reads and writes `g_ImcGameplay`, where they have no defaults. The fact
   that arrow keys work in menus is owed to `setupMenuDefaults` populating
   `g_ImcMenu` directly; the menu's "Menu Navigation" rows have been a
   placebo.
4. Hold versus tap is invisible. `ACTION_USE` carries an interact-vs-reload
   discrimination via `actionmapGetEffectiveHoldMs(ACTION_USE)`; the user
   tunes the threshold under Per-action hold overrides, but the row itself
   does not signal that pressing the key short fires reload while holding it
   fires interact. After J-3 lands, `ACTION_SCORECARD_HOLD` will be a separate
   action; the menu must show it as a sibling row, not collapse it.

Audit summary: the menu has the right rebind UX but the wrong scope.

---

## 3. Target state, in one paragraph

The Controls tab gains an outer set of tabs labelled by IMC mode, in this
order: Mission, Combat Simulator, Vehicle, The Grid, Menu, System. Each outer
tab keeps the existing inner pair of sub-tabs, Keyboard & Mouse and
Controller. The action rows shown under any combination of outer-and-inner tab
are scoped to that IMC. Reset-to-defaults under any outer tab resets only that
IMC. Hold versus tap appears as two rows side by side with the hold threshold
in the row label, e.g. `Scorecard` and `Scorecard (hold 400ms)`. Search filter
applies to the active outer tab only. Conflict detection runs per outer tab so
a key bound to Move Forward in Mission does not falsely clash with the same
key bound to Accelerate in Vehicle.

---

## 4. IMC-to-tab mapping

The user-facing tab names match Mike's brief. The implementation maps tab
identity to the right `InputMappingContext` pointer so the action list and the
rebind calls target the correct IMC.

| Outer tab | Backing IMC(s) | Source of truth |
|---|---|---|
| Mission | `g_ImcMission` (post-J) or `g_ImcGameplay` (pre-J fallback) | `setupMissionDefaults` (post-J) / `setupGameplayDefaults` (pre-J) |
| Combat Simulator | `g_ImcCombatSim` (post-J) or `g_ImcGameplay` (pre-J fallback) | `setupCombatSimDefaults` (post-J) / `setupGameplayDefaults` (pre-J) |
| Vehicle | `g_ImcVehicle` | `setupVehicleDefaults` |
| The Grid | `g_ImcForgeSession` plus `g_ImcForge` (one tab, two IMC blocks) | `setupForgeSessionDefaults` plus `setupForgeDefaults` |
| Menu | `g_ImcMenu` plus `g_ImcPauseMenu` (one tab, two IMC blocks) | `setupMenuDefaults` plus `setupPauseMenuDefaults` |
| System | `g_ImcDebugOverlay` plus `g_ImcTextInput` plus the system-hotkey actions on `g_ImcGameplay` (read-only or noted as cross-IMC) | per-IMC defaults |

The three IMCs that get folded into one tab (The Grid: ForgeSession + Forge;
Menu: Menu + PauseMenu; System: DebugOverlay + TextInput + gameplay-system)
are surfaced as a section header inside the tab. This keeps the tab count
manageable (six) while still making the IMC source visible.

### Pre-J fallback

The worktree at the time of this design doc is pre-J-impl. `g_ImcGameplay`
exists; `g_ImcMission` and `g_ImcCombatSim` do not. The Phase 2
implementation is allowed to land on either side of J, but with a single rule:
the Mission and Combat Simulator tabs share `g_ImcGameplay` until J-1 splits
it. After J-1, the tab name and the backing pointer change without further
menu work; the menu does not need to know which side of the split it is on
beyond a single "current backing IMC" lookup table indexed by tab.

The lookup table is centralised so the post-J flip is a one-line change.

---

## 5. Per-IMC action sets

Each outer tab shows only the actions that the backing IMC has any reasonable
default or runtime use for. Listing every action under every IMC would
re-introduce the placebo problem (rows that do nothing).

The per-IMC action lists below are derived from the existing
`setupXxxDefaults` functions plus the FREEFLY-only actions on `g_ImcForge`.
Where a row is shown under a tab whose backing IMC has zero defaults for that
action, the bind cell is empty by default; the user can still bind it because
`actionmapBind` accepts any IMC and any action.

### Mission tab

Movement, Aim, Combat, Weapons, Menu Nav, C-Buttons, D-Pad, System Hotkeys
groups, same content as today's `s_BindableActions[]` minus the Vehicle group.

Hold-vs-tap rows under Combat:
- `Use / Interact` row pair (tap = reload, hold = interact). After J-3 the
  pair is two rows: `Use / Interact (tap, reload)` and
  `Use / Interact (hold 300ms, interact)`. Pre-J-3 it remains a single row
  with a tooltip that explains the hold split, and the existing Per-action
  hold overrides slider drives the threshold.

### Combat Simulator tab

Same baseline as Mission. Adds:
- `Scorecard` (tap, no-op in CS) and `Scorecard (hold 400ms)` (raises
  scoreboard) as two rows. The hold threshold is read from
  `actionmapGetEffectiveHoldMs(ACTION_SCORECARD_HOLD)` post-J-3. Pre-J-3,
  shows the existing single `Scorecard` row.

### Vehicle tab

Vehicle group only: Accelerate, Brake / Reverse, Steer Left, Steer Right,
Exit Vehicle. Plus Pause (already on `g_ImcVehicle`).

### The Grid tab

Two sections under one tab.

Section: Forge Session (active for the entire Grid session, NORMAL +
FREEFLY).
- Mode Toggle (FORGE_TOGGLE).

Section: Forge Editor (active in FREEFLY only).
- Camera Ascend / Descend (FORGE_ASCEND / DESCEND).
- Speed Boost (FORGE_BOOST).
- Speed Precision (FORGE_PRECISION).
- Sidebar Toggle / Up / Down / Activate.
- Tab Prev / Tab Next.

### Menu tab

Two sections.

Section: Menu (g_ImcMenu).
- Use / Accept (USE).
- Cancel / Back (CANCEL_USE).
- Pause (PAUSE).
- Menu Up / Down / Left / Right (MENU_UP/DOWN/LEFT/RIGHT).
- Menu Prev Tab / Next Tab (MENU_TAB_PREV/NEXT).

Section: Pause Menu (g_ImcPauseMenu). Same action list, separate IMC; the
two sections allow per-IMC overrides if a user wants a different Cancel
keybinding in pause.

### System tab

Three sections.

Section: System Hotkeys (g_ImcGameplay).
- Screenshot, Console Toggle, Debug Overlay, Enter Cheat. Read and write
  `g_ImcGameplay` because that is where these are actually dispatched
  during normal play.

Section: Debug Overlay (g_ImcDebugOverlay).
- Use, Cancel, Console Toggle, Debug Toggle, Screenshot. These are the
  in-overlay rebinds.

Section: Text Input (g_ImcTextInput).
- Use, Cancel, Cheat Enter. These fire only inside text fields.

---

## 6. Hold-vs-tap representation

Three principles:

1. Each hold or tap variant is a separate row.
2. The label carries the discrimination: `Scorecard` (tap) and
   `Scorecard (hold 400ms)`.
3. The threshold value comes from `actionmapGetEffectiveHoldMs(action)` so a
   user-changed threshold (Per-action hold overrides advanced section) is
   reflected immediately in the row label.

Implementation hook: the `BindableAction` struct gains an optional
`hold_label_action` field. When set, the row label appends
` (hold %d ms)` formatted from `actionmapGetEffectiveHoldMs`. The action
itself is whatever pure action the row binds. For the two cases known today:

- Use / Interact: `ACTION_USE` carries the hold qualifier today via the
  global `Use hold` slider. The row label reads
  `Use / Interact (hold %d ms, interact)` post-J-3 if the design picks
  Option 6.2 (parallel action, `ACTION_USE_HOLD` or similar). Pre-J-3 the
  row stays as today (tooltip explains the split).
- Scorecard: post-J-3, two parallel actions
  (`ACTION_SCORECARD` and `ACTION_SCORECARD_HOLD`).

The advanced "Per-action hold overrides" section currently inside the
Controller sub-tab moves to a collapsible section under each tab where any
action with a hold variant exists, so the user can tune thresholds in the
same context as the rebind.

---

## 7. Layout, focus, and navigation

### 7.1 Top-down layout

```
Settings tab: Controls
  +-----------------------------------------------------------+
  |  [Mission] [Combat Sim] [Vehicle] [The Grid] [Menu] [Sys] |  outer tabs
  +-----------------------------------------------------------+
  |  [Keyboard & Mouse]   [Controller]                        |  inner tabs
  +-----------------------------------------------------------+
  |  Search: [..............]  Clear                          |
  |                                                           |
  |  Movement                                                 |  group header
  |  +-----------------------------------------------------+  |
  |  | Action               | Bind 1     | Bind 2          |  |
  |  | Move Forward         | W          | LSTICK_UP       |  |
  |  | Move Backward        | S          | LSTICK_DOWN     |  |
  |  | ...                                                  |  |
  |  +-----------------------------------------------------+  |
  |                                                           |
  |  Combat                                                   |
  |  +-----------------------------------------------------+  |
  |  | Use / Interact                | F          | X      |  |
  |  | Use / Interact (hold 300ms)   | F          | X      |  |
  |  | ...                                                  |  |
  |  +-----------------------------------------------------+  |
  |                                                           |
  |  [Reset Mission to Defaults (Keyboard & Mouse)]           |
  +-----------------------------------------------------------+
```

The hold-row indents one level relative to its tap sibling so the visual
relationship is obvious at a glance.

Action labels live in the leftmost column. Bindings sit to the right of the
label. This keeps Mike's "labels above or to the LEFT, never to the right"
rule for the row level and matches the brief's "rebind UX flows the same as
current" pledge.

### 7.2 Focus and navigation contract

The brief's flat-menu rules apply:

- LB / RB cycle outer tabs.
- Inner tab cycling stays on its existing key (LB / RB also work because
  ImGui's tab bar consumes them; the conflict is resolved by the outer tab
  bar consuming first when the focus is at tab-bar level, then the inner
  bar when focus is below).
- D-pad and arrow keys traverse rows transparently across group containers
  (`ImGuiChildFlags_NavFlattened` already used in the current code).
- A acts on the focused row (begins capture).
- B exits at top, returning to the parent Settings menu.
- Right-click clears the focused bind (mouse only). Gamepad equivalent is
  X on the focused row, mirroring the visual mapper convention.

`ImGuiChildFlags_NavFlattened` is already applied to the bind tables, so
the cross-container traversal works out of the box once the outer tab
container also flattens.

### 7.3 Capture banner

When capture is active, the banner remains anchored at the top of the
active inner tab. The banner names the IMC (`Capturing for Mission ->
Move Forward (keyboard / mouse)`) so a user who triggered capture and
then accidentally switched outer tabs sees the mismatch and can press
Esc.

---

## 8. Rebind UX preservation

| Behaviour | Today | After rebuild |
|---|---|---|
| Click bind cell to begin capture | Yes | Yes |
| Esc cancels capture | Yes | Yes |
| Right-click clears bind | Yes | Yes |
| Live search filter | Yes (gameplay IMC) | Yes (active IMC) |
| Conflict highlighting | Per device column | Per device column, scoped to active IMC |
| Reset to Defaults | Two buttons (per device, all gameplay) | Two buttons per IMC tab (per device, that IMC only) |
| Per-action hold overrides | One section under Controller | Per-IMC, beside the rebind table |
| Visual controller mapper | Under Controller, gameplay IMC | Under Controller, ACTIVE IMC; X = sidebar (Forge) shows up only in The Grid tab, etc. |
| Keep "MKB-only reset" and "Controller-only reset" semantics | Yes | Yes per tab |

The visual controller mapper (`renderControllerVisualMapper`) consumes the
same `getBindsByType` path; making it IMC-aware is a one-line lookup.

---

## 9. Reset semantics

Each outer tab carries up to two reset buttons under its inner tabs:
- `Reset Mission Keyboard & Mouse to Defaults` (under Mission > KB&M).
- `Reset Mission Controller to Defaults` (under Mission > Controller).

The button writes the active IMC only. For tabs that fold two IMCs (The
Grid, Menu, System), the reset writes both. The button label shows the
plural ("Reset The Grid Keyboard & Mouse to Defaults") and the secondary
text below explains which IMCs are affected.

A tertiary `Reset All to Factory Defaults` button stays at the bottom of
the Controls tab for the nuclear option.

---

## 10. Implementation approach

### 10.1 Data restructure

Add an `InputMappingContext *imc` field to the per-tab descriptor and
move the `s_BindableActions[]` static into a per-tab table. The
descriptors live in a small file-scope array:

```c
struct ImcTabDesc {
    const char           *label;
    InputMappingContext **imcs;     /* one or two; NULL-terminated */
    s32                   numImcs;
    const BindableAction *actions;  /* per-tab action list */
    u32                   numActions;
    u32                   shownGroups;  /* bitmask of BindableGroup */
    const char           *resetLabel;
};
```

The current `s_BindableActions[]` is the Mission tab's action list
verbatim. Combat Sim differs by adding `Scorecard (hold)`. Vehicle has
five rows. The Grid has eight rows split across two sections. Menu has
about a dozen rows. System has roughly the same count.

A single descriptor per outer tab keeps the rendering loop simple: pick
descriptor by tab index, walk its action list, render rows that read and
write the descriptor's IMC.

### 10.2 Function signatures

Existing helpers parameterise on `&g_ImcGameplay` directly. The rebuild
adds one parameter to each:

```c
static void getBindsByType(InputMappingContext *imc, InputAction action,
                           u32 mkbVKs[2], s32 mkbSlots[2], s32 *mkbCount,
                           u32 ctrlVKs[2], s32 ctrlSlots[2], s32 *ctrlCount);
static s32 findFreeTriggerSlot(InputMappingContext *imc, InputAction action);
static s32 pickSlotForControllerBindColumn(InputMappingContext *imc,
                                            InputAction action, s32 wantCol);
static void clearControllerVkAtBindColumn(InputMappingContext *imc,
                                           u32 vk, s32 bindCol,
                                           u32 hideGroupMask,
                                           const BindableAction *actions,
                                           u32 numActions);
static void renderBindButton(InputMappingContext *imc, ...);
static void conflictMapRebuild(InputMappingContext *imc,
                                const BindableAction *actions, u32 numActions,
                                s32 filterCol);
static void renderBindTable(InputMappingContext *imc,
                             const BindableAction *actions, u32 numActions,
                             u32 hideGroupMask,
                             s32 filterCol, const char *tableId);
```

The capture state (`s_CaptureActive`, `s_CaptureAction`, etc.) gains one
field `s_CaptureImc` so the deferred capture handler writes into the
right IMC. Tab switches with capture active cancel capture (already
true), and the cancel path now also clears `s_CaptureImc`.

### 10.3 Diff size estimate

The Input Mapping section runs roughly 1340 lines today. The rebuild
adds about 400 lines (the descriptor table, the per-tab render loop, the
two new tabs' action lists, the IMC-scoped helper variants) and rewrites
about 600 lines (the existing helpers gain an `imc` parameter and the
top-level `renderSettingsControls` body changes shape). Net delta is
about +400 lines. The change is contained in
`port/fast3d/pdgui_menu_mainmenu.cpp`; no header changes; no actionmap
changes; no save-format changes.

### 10.4 Save format

`pd.ini` already keys binds by `ActionMap.P0.<action>` and the action's
canonical name covers all 68 actions in the enum. The IMC is implicit:
each IMC writes its own bind string for the same action. The current
`actionmapSaveBinds` walks every IMC and builds the bind string for the
union of triggers. After the rebuild, the menu writes into the right
IMC; `actionmapSaveBinds` continues to serialise every IMC's bindings.
No save-format change.

There is one wrinkle. The current behaviour ties all triggers for a
given action to a single ini key, regardless of which IMC owns them.
If two IMCs bind the same action to different keys (today rare but
possible after this rebuild because the user can rebind The Grid forge
toggle separately from the gameplay forge toggle), the ini round-trip
must distinguish them. Two options:

a. Extend the ini schema to `ActionMap.P0.<imc>.<action>`. Backward
   compatible if the loader falls back to the unprefixed key for
   actions that exist on only one IMC. The loader exists in
   `actionmapLoadBinds`; this is a one-function change.
b. Accept that per-IMC divergence is not persisted: the ini stores the
   union of all IMC triggers and `actionmapLoadBinds` distributes them
   back the same way as today. User-facing visibility of which IMC owns
   which trigger is preserved in the menu, but rebinding is effectively
   global per action.

Recommendation: option (a). The cost is a four-line schema addition
plus a backward-compat read path. The benefit is that the user-facing
mental model in the new menu (per-IMC binds) matches what actually
persists. Option (b) breaks the user's expectation the moment they bind
something different between Mission and CS.

### 10.5 Co-existence with Sessions A, B, C, D, E, F

Files this rebuild touches (Phase 2):
- `port/fast3d/pdgui_menu_mainmenu.cpp` (this menu).
- `port/src/actionmap.cpp` only if option (a) of section 10.4 is taken,
  and only the `actionmapSaveBinds` and `actionmapLoadBinds` functions.
  Both functions are pure ini serialisation and should not collide with
  Session B's input-context-stack work.
- `port/include/actionmap.h` only to expose IMC pointers if any are not
  already extern'd. All five known IMCs are already extern.

Files this rebuild does not touch:
- Any FP weapon code (Session A).
- Any menu state-machine code beyond the Controls tab (Session B).
- Any net code (Session C).
- Any mod loader or property handler (Session D).
- The render pipeline, Agent Creator, Character Select (Session E).
- Bot AI, spawn, or hygiene fixes (Session F).

The IMC binding tables in `actionmap.cpp` (`setupGameplayDefaults`,
`setupVehicleDefaults`, etc.) are the source of truth that the rebuild
reads from; the rebuild does not modify them. If J-1 lands and renames
`setupGameplayDefaults` to `setupMissionDefaults` plus adds
`setupCombatSimDefaults`, the menu's ImcTabDesc table updates two
pointers and the rebuild absorbs J-1 cleanly.

---

## 11. Phasing

### Phase P-1: scaffolding

Outer tab bar with six tabs all rendering the existing `renderSettingsControls`
body. Verify navigation, focus, and tab cycling work. No IMC scoping yet.

Exit criteria: outer tabs render, LB/RB cycle them, each tab content is
identical to today's flat list (every tab shows gameplay IMC).

### Phase P-2: IMC scoping

Thread the `InputMappingContext *` parameter through the helpers. Wire each
tab to its backing IMC. Reset buttons become per-IMC.

Exit criteria: clicking Vehicle tab > KB&M shows only the five vehicle
actions; rebinding Accelerate writes `g_ImcVehicle` and verifies via
`actionmapSaveBinds` that the ini key changed.

### Phase P-3: per-tab action lists

Replace the single `s_BindableActions[]` with per-tab tables. Add the Forge
actions table and Menu actions table.

Exit criteria: each tab's row count matches Section 5; The Grid tab shows
the eleven Forge actions; Menu tab shows the menu nav actions.

### Phase P-4: hold-vs-tap representation

Pre-J-3: add the `(hold %d ms)` label suffix to the existing Use / Interact
row when an action override is active. Single row per action still.

Post-J-3 (separate session): add the `Scorecard (hold)` and Use / Interact
hold sibling rows.

Exit criteria: the hold threshold appears in the row label and reflects the
live `actionmapGetEffectiveHoldMs` value when the user adjusts the
Per-action hold overrides slider.

### Phase P-5: per-IMC ini schema (optional)

Implement option (a) of section 10.4. Extend
`ActionMap.P0.<imc>.<action>` keys, add backward-compat read fallback,
update `actionmapLoadBinds` and `actionmapSaveBinds`.

Exit criteria: per-IMC divergence persists across restarts; backward-compat
test (load an old pd.ini) keeps working.

### Phase P-6: build verify and B-252 closure

Build `pd` and `pd-server`. Verify pd.ini round-trip. Mark B-252 RESOLVED in
`context/bugs.md`. Update `context/session-log.md`.

---

## 11.5 Verification rubric and triage methodology

Three principles from Mike scope the verification and debugging side of this
rebuild. The rubric below maps each principle to concrete checks the Phase 2
implementer must run before declaring the rebuild done. The triage matrix in
section 11.6 names the failure modes those checks catch.

### Principle 1: verify the fix actually delivers the cleaner UX

Pass criteria are not "tabs render" but "the user can clearly see what is
bound where, in seconds, without reading documentation". Concrete checks:

a. With a fresh `pd.ini`, open Settings -> Controls. Cycle every outer tab
   with LB / RB and confirm each tab's action list visibly differs. The
   Vehicle tab MUST show only five rows; The Grid tab MUST show eleven; the
   Mission and Combat Simulator tabs MUST show their full lists.
b. Bind `M` to Move Forward in Mission. Switch to Combat Simulator. The
   binding should display as `M` if the IMC is the same (pre-J-1) or as a
   different value if J-1 has split them. Either result is correct;
   confirm which world the build is in by reading
   `g_ImcMission.priority` (post-J) versus `g_ImcGameplay.name` (pre-J)
   from the actionmap source. The verification step is "the displayed
   value matches the IMC the menu claims to write".
c. Hold-vs-tap visibility: enable a custom 600 ms override on
   `ACTION_USE`. Reopen the Mission tab and confirm the Use / Interact
   row label updates to `Use / Interact (hold 600 ms, interact)`
   immediately on next frame. If the threshold is read at tab-enter time
   only (cached), the row goes stale; reject and require live reads.
d. Conflict highlight scoping: bind `R` to Reload in Mission and to
   Steer Right in Vehicle. Mission should NOT highlight `R` as
   conflicting with Steer Right (different IMC). The current
   `conflictMapRebuild` walks the active IMC only after the rebuild;
   verify by hovering the Reload bind and confirming the tooltip shows
   no conflict.
e. Reset isolation: rebind every Mission action to single keys. Click
   Reset Vehicle to Defaults. Mission's bindings MUST remain unchanged.

### Principle 2: bad-value triage matrix

When a tab shows wrong content, walk the matrix below in order. The first
match identifies the bug class, and each row points at the file region
that owns it.

| # | Symptom | Triage axis | Where to look first |
|---|---|---|---|
| 1 | Wrong action list under a tab | Interpretation (tab index to descriptor) | The `ImcTabDesc[]` indexing in `renderSettingsControls`. Off-by-one between ImGui tab index and descriptor array slot. |
| 2 | All tabs show identical bindings | Setting (IMC pointer threading) | `getBindsByType(imc, ...)` callsites. If `imc` parameter was added but a callsite still passes `&g_ImcGameplay` literally, every tab will read the same IMC. Grep for `g_ImcGameplay` inside the Input Mapping section. |
| 3 | Tab shows correct list at first switch but stale data after a rebind | Multiple sources (capture state + tab descriptor) | `s_CaptureImc` was not updated when capture began, OR the capture deferred-handler writes `g_ImcGameplay` instead of `s_CaptureImc`. Check `handleCaptureInput` first, then `renderBindButton` capture trigger. |
| 4 | Two tabs share a binding that should be independent | Multiple sources (J split state + ini schema) | If J-1 has shipped and ini schema is still option (b) global-union (section 10.4), Mission and Combat Sim diverge in memory but the next config save collapses them. Verify which schema is active by inspecting `actionmapSaveBinds`. |
| 5 | A binding written from the menu does not survive a restart | Overwrite (ini key collision) | `actionmapLoadBinds` reads ini keys back. If the menu wrote `ActionMap.P0.vehicle.MOVE_FORWARD` but the loader still reads `ActionMap.P0.MOVE_FORWARD`, the round-trip drops the IMC qualifier. |
| 6 | Bindings show but rebind capture fails silently | Ordering (capture handler runs before tab change is observed) | `handleCaptureInput` is called once at the top of `renderSettingsControls`. If the active tab changed this frame, the handler should not commit a half-captured key into the previous tab's IMC. Add an explicit "capture-cancel-on-tab-change" guard at the same site that already cancels on inner sub-tab change. |
| 7 | The Grid tab has Forge actions but rebinding writes nowhere | Setting + multiple sources | `g_ImcForge` is FREEFLY-only-active, but `actionmapBind` does not require an IMC to be active. Verify by inspecting `actionmapBind` and confirming inactive IMCs accept writes. If they reject, the menu must temporarily activate the IMC for the duration of the rebind, or the bind path must accept any IMC. |
| 8 | Hold threshold in row label is wrong | Interpretation + setting | `actionmapGetEffectiveHoldMs` honours the per-action override and falls back to the global `Use hold` value for `ACTION_USE`. For other actions, the global default is 0. Confirm the row label uses `actionmapGetEffectiveHoldMs(action)` and not the global slider directly. |

### Principle 3: up versus down the stack

When a binding does not visibly fire in-game even though the menu shows it
correctly, chase both directions:

- **Down the stack** from the menu: the menu writes `actionmapBind(imc,
  player, action, slot, vk)` and calls `actionmapSaveBinds` plus
  `configSave`. Verify by reopening pd.ini and reading the relevant
  `ActionMap.P0.<...>` key.
- **Up the stack** from the dispatch site: the action consumer reads
  `actionPressed(player, action)`. The dispatcher resolves which IMC owns
  the VK. If the IMC the menu wrote into is not active at consumption
  time, the binding is silently dead. Verify by enabling the actionmap
  diagnostic log (`ACTIONMAP_LOG_DISPATCH` if present) or by adding a
  one-line `sysLogPrintf` at the press site to confirm the action fires.

The two directions catch different bug classes:
- Wrong IMC chosen by the menu: caught down-the-stack via pd.ini key
  inspection.
- Right IMC, wrong activation lifetime (Forge IMC inactive when the user
  expects it): caught up-the-stack via dispatch-site logging.

A single-direction chase (only down) gives false confidence: pd.ini shows
the right binding, the user complains it does not fire, and the rebuild
ships with a dead Forge tab.

---

## 12. Open questions

- **Q1 (P).** Section 10.4 schema: option (a) per-IMC ini keys or option
  (b) global-union? Recommendation (a).
- **Q2 (P).** Should Combat Sim and Mission share a single tab with a
  collapsible "what differs in CS" sub-section, or separate tabs? Mike's
  brief says separate tabs; this design honours that. If post-J-1 the
  delta turns out to be a single action (Scorecard hold), a future
  refactor can collapse them. Not a blocker.
- **Q3 (P).** The System tab folds three IMCs plus gameplay-system
  hotkeys. Is the section divider clear enough, or does the user need
  three sub-tabs (Debug / Text Input / Hotkeys)? Recommendation: one
  tab with section headers; revisit if user feedback finds it confusing.
- **Q4 (P).** Pre-J-impl Phase 2 lands against `g_ImcGameplay`; the
  Mission tab and Combat Sim tab both write the gameplay IMC until J-1
  flips them. Is the duplicate write acceptable in the interim? Yes
  because both tabs end up writing the same ini keys, identical to
  today's behaviour.
- **Q5 (P).** Reset semantics for The Grid tab: should "Reset The Grid"
  reset both ForgeSession and Forge IMCs, or surface them separately?
  Recommendation: single button resets both, with secondary text
  explaining the scope. The two IMCs are operationally one feature
  surface.

---

## 13. Out of scope for this design

- Hooking the Combat Sim hold-Back-for-scorecard semantic itself
  (Priority J-3).
- Any new actions beyond what's already declared in the InputAction
  enum.
- Splitscreen / per-player rebind (port is single-seat).
- The Forge-aware gameplay suppression rules
  (`actionIsBlockedInFreefly`); that lives in `actionmap.cpp` and is
  Session B's territory.
- The visual controller diagram art update; existing silhouette art
  carries over unchanged.
