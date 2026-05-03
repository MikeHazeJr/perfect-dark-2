# Input - Menu Input Interaction Grammar (v2)

> Status: **CANONICAL REFERENCE** for all menu input handling work going forward.
> Date: 2026-05-03
> Pillar: joint Menu Stacking + Input.
> Source: Combat Simulator binding spec v2 (verbatim JSON at [combat-simulator-bindings-v2.json](combat-simulator-bindings-v2.json), 1085 lines, generatedAt 2026-05-03T07:33:49.545Z).
> Mike's authoritative decisions on Q1-Q5 (2026-05-03 host session) are folded in below; in two places (Rules 7 and 8) Mike's decisions INVERT the verbatim JSON's player-row cells, and the inversion is project-canonical. See Rule 7 and Rule 8 reconciliation blocks.
> Supersedes: any v1 draft (per Mike: "v1 superseded entirely; do not merge any v1 artifacts").
> Predecessor docs: [input-menu-system-audit-2026-05-01.md](../../audits/input-menu-system-audit-2026-05-01.md), [input-universality-and-transitions.md](../input/input-universality-and-transitions.md), [input-authority-and-menu-pool.md](../menus/input-authority-and-menu-pool.md), [menu-stack-architecture.md](../menus/menu-stack-architecture.md).

## Purpose

This document is the canonical contract for how a menu reads input. It is derived from the Combat Simulator binding spec v2 (the project's single largest controller-driven menu, modelled per-element across every gamepad input), promoted to a universal contract that **every existing and future menu must conform to**.

When a menu adds a binding, it cites a rule by number. When a binding feels wrong, the rule is the disagreement. When a new menu is built, it inherits this contract by default and any deviation is justified inline in the source-file header.

The relationship to the existing input layers:

- **Action map + IMC stack** ([port/include/actionmap.h](../../../port/include/actionmap.h), [port/src/actionmap.cpp](../../../port/src/actionmap.cpp)) is the cross-device translator. The grammar's "press A" means `IsKeyPressed(ImGuiKey_Enter)` on KB&M and `actionWasTap(ACTION_USE)` on gamepad, both produced by the existing translation layer in `pdguiDriveImGuiNav` ([port/fast3d/pdgui_backend.cpp:485-568](../../../port/fast3d/pdgui_backend.cpp:485)).
- **Layer stack** ([port/src/inputlayer.c](../../../port/src/inputlayer.c)) gates this grammar's applicability: every binding below applies when `LAYER_MENU` is the top of the layer stack. Gameplay-layer rules (B-298 vehicle, freefly camera) live in their own contexts and are out of scope.
- **Menu pool** ([port/src/menupool.c](../../../port/src/menupool.c)) deduplicates by menu type. Because all instances of a menu type share one binding contract, this grammar is per-menu-type, not per-instance.

The Combat Simulator per-element JSON manifest at [combat-simulator-bindings-v2.json](combat-simulator-bindings-v2.json) is the data; this document is the abstract pattern it implements; [combat-simulator-binding-doc.md](combat-simulator-binding-doc.md) is the per-element implementation spec with each cell of the verbatim JSON transcribed and reconciled against this grammar.

## Universal grammar (10 rules)

Every binding below applies to the menu layer (`LAYER_MENU` per `port/include/inputlayer.h`). KB&M produces equivalent semantics through the action map.

### Rule 1 -- D-pad + Left Stick: column traversal

Up / Down moves focus to the previous / next focusable widget within the current panel. Left / Right crosses to the adjacent panel at the row whose vertical centre is closest to the current focus row (verbatim from Mike's directive: "Go to control in other panel aligned with this one"). D-pad and Left Stick are interchangeable.

**Rationale.** The vertical axis of the focus graph is the user's mental model of "the list I am navigating"; wrap-at-boundary keeps the user in the panel they meant to be in. Left / Right as the cross-axis matches every console UX convention. Aligned-by-vertical-position lands the user near where their eye already is, so cross-panel traversal feels like sliding rather than teleporting.

**Scope.** Every focusable element in every menu.

**Combat Simulator examples (verbatim from JSON):**
- Arena Dropdown: D-pad Down -> "move to scenario dropdown control"; D-pad Right -> "move to right side, this one would go to the team sorting dropdown if teams are enabled".
- Teams Option: D-pad Down -> "move to time limit option"; D-pad Right -> "go to the control in the right panel aligned with this one (a bot, the team mode dropdown, or the add bot button)".
- Your Player Row (p.you): D-pad Down -> "move down to next character"; D-pad Left -> "go to the control in the left panel aligned with this one (Teams or Scenario selection)".

**Edge cases.**
- Cross-panel traversal requires `ImGuiChildFlags_NavFlattened` on both sibling `BeginChild` calls. Without it, Left / Right hits the panel boundary as a wall. The NavFlattened migration is mostly complete (audit B); finish the gaps (theme editor most notably -- audit A.2 row 7).
- When the target panel has no row at the same vertical level (e.g. left panel has 13 rows but right panel has 9 player rows + 1 Add Bot), ImGui's "closest by vertical centre" picks the nearest available row. The verbatim JSON's Score Limit Slider says "go to the control in the right panel aligned with this one (a bot, or the add bot button)" reflecting exactly this fallback.
- D-pad Up at the top of a panel and D-pad Down at the bottom should NOT wrap to the other end of the same panel (no menu in v2 wraps). Instead, the focus stays on the boundary widget. The Fast Movement Option's D-pad Down ("move to next button down in the panel (or if this is the last one, move to Start Match button)") is the canonical pattern for "step out of left panel into footer".

**Universal application.** Mandatory. ImGui already gives this for free with `NavEnableKeyboard`. The rule formalises that no menu may consume D-pad / L-stick for a non-focus purpose at the panel level. Audit any custom navigation handlers (forge_editor's `ACTION_FORGE_*` axes are an exception because the forge editor uses gameplay-context input -- see B-195 design notes).

### Rule 2 -- Right Stick: innermost-list scroll only

Right stick Up / Down scrolls the innermost scrollable container that contains current focus. Active on player rows for player-list scroll per the verbatim JSON ("scroll player list"); idle on every other element type. Right stick Left / Right is universally unbound.

**Rationale.** When focus is inside a nested scrollbox, the user's intent for "scroll" is the nearest enclosing scroll surface, not the page's outer scroll. The audit (D) catalogued every nested-scroll site in the codebase; the walker handles them uniformly.

**Scope.** Any focusable element whose containment chain includes a scrollable surface. Idle when there is no enclosing scroll container.

**Combat Simulator examples (verbatim).** Every player row (p.you, p.joanna, p.bot1 through p.bot6) sets `R-Stick Up` and `R-Stick Down` to "scroll player list". Every other element (tabs, sections, left-panel rows, dividers, Add Bot, Start Match, Back to Menu) leaves R-stick empty.

**Implementation reference.** `pdguiInnermostScrollableForNav` in [port/fast3d/pdgui_backend.cpp](../../../port/fast3d/pdgui_backend.cpp) (Phase 2 fix #2 walker). The walker performs depth-first descent from the current focus to find the innermost `ImGuiWindowFlags_AlwaysVerticalScrollbar` or scrollable child.

**Edge cases.**
- When focus is on a row that is NOT in a nested scroll surface (e.g. left-panel options in Combat Sim), R-stick is no-op rather than falling through to outer-page scroll. The verbatim JSON encodes this by leaving R-stick empty on left-panel rows.
- The walker descends through `NavFlattened` siblings; a flattened-pair where only one side has scroll still resolves correctly to the scrollable side.

**Universal application.** Mandatory. No menu may install a custom right-stick handler. The walker is the single authority.

### Rule 3 -- A: primary activate

A activates the focused widget: button presses, dropdowns open, sliders enter step-edit (or open preset modals -- see Combat Sim time / score sliders), list rows select / deselect, options toggle. Equivalent to `IsKeyPressed(ImGuiKey_Enter)` (cross-device alias for `ACTION_USE`, action-map id in [actionmap.h](../../../port/include/actionmap.h)).

**Rationale.** A is the universal "yes / proceed" button across console UX. Every menu in this project already maps Enter / A to confirm. This rule formalises the contract so that no menu silently re-binds A to anything other than activation.

**Scope.** Every focusable element.

**Combat Simulator examples (verbatim).**
- Arena Dropdown / Game Type Dropdown / Weapon Set Dropdown: A -> "open maps dropdown" or "open weapon loadout dropdown".
- Teams Option / Auto-Aim / Jump / Radar / One-Hit / Friendly Fire / Fast Movement: A -> "toggle this option".
- Time Limit Slider / Score Limit Slider: A -> "open modal with preset options (10, 20, 50, 100, no limit)".
- Player rows (p.you, p.joanna, p.bot1-6): A -> "select / deselect character".
- Add Bot Button: A -> "add bot if able".
- Start Match Button: A -> "start match".
- Back to Menu Link: A -> "bring up 'exit combat simulator' modal" (A+B convergence; see Rule 4).
- Team Sort Dropdown: A -> "Open drop down".

**Edge cases.**
- Compound activate: dropdowns open on first A, then row-selection-and-confirm on second A. Sliders enter edit mode on first A, then D-pad adjusts, then A confirms (or B reverts).
- A on a non-actionable element (section header, divider) is no-op since those are not focusable per CC1.

**Universal application.** Mandatory. No menu may re-bind A. New menus that add primary actions must use focus-and-A, never a non-focus shortcut.

### Rule 4 -- B: destructive-back (with A+B convergence on destructive links)

B raises the menu's destructive-back confirm modal whose default-focused option is **Cancel** (stay in the current menu). The modal's Confirm button performs the actual back / exit. B alone never closes a menu silently when state is at risk.

**A+B convergence on destructive links.** When an element's primary (A) action is itself destructive (e.g. Combat Sim's "Back to Menu" link whose A action is "bring up 'exit combat simulator' modal"), A and B converge on the same confirm modal. The link is just a visible anchor for the destructive action; B is the same action through the universal channel. The verbatim JSON encodes this on `footer.back` where both A Button and B Button resolve to "bring up 'exit combat simulator' modal".

**Rationale.** Silent backout is the single most common cause of "lost what I was doing" in menu pillar bug reports. The modal makes the intent explicit and recoverable on the same controller stroke. Mike's directive flagged this as non-negotiable. See also CC3 below.

**Scope.** Every menu that holds in-progress state. Combat Sim's verbatim JSON sets B to "bring up 'exit combat simulator' modal" on every focusable element (every left-panel row, every right-panel row, footer buttons, the link). Section headers and dividers leave B empty per CC1 (they are not focusable).

**Edge cases.**
- A menu with no destructive state (purely informational, no edits, no in-progress configuration) MAY B-close immediately. The default in doubt is the modal.
- Nested modals: B at the innermost modal closes the innermost without prompting; only outermost B prompts.
- A menu with explicit Save / Cancel buttons treats B as "Cancel with confirmation if dirty, immediate if clean".

**Universal application.** Mandatory for any menu that holds in-progress state. Audit existing menus that B-close silently and convert each to the modal pattern. The pause menu, settings, mod manager, theme editor, agent editor, skin editor, etc., all qualify.

### Rule 5 -- X: context menu (or feature shortcut)

X opens a small popup of secondary actions for the focused element. Two disambiguated forms based on element type:

**Per-row overflow** (the common case). Popup of element-local actions like "Change team / Set temp character / Bot settings / Remove bot" on a player row, or "Fill / Remove all" on Add Bot. Verbatim from JSON:
- Player rows (p.you, p.joanna, p.bot1-6): X -> "context menu, change team or set temp character, bot settings, remove bot".
- Add Bot: X -> "context menu (fill, remove all)".

**Screen-local feature shortcut** (rare). X on a footer button maps to a feature surface, e.g. X on Combat Sim's Start Match opens the Queue Match feature. Verbatim:
- Start Match: X -> "Queue Match (stores an array of match start settings and cycles them as matches end; host can either 'end game' or 'skip match' in their pause menu); display number of queued match settings."

The popup's default-focused option is the most-likely action. The same builder helper backs both right-click (mouse) and X (controller) so the inventories never drift (CC5).

**Empty cells in v2 are not violations.** The verbatim JSON leaves X empty on left-panel rows (Arena Dropdown, Game Type Dropdown, Teams Option, sliders, options, Team Sort, Weapon Set Dropdown). These are designed-empty for v2: those rows do not yet have defined per-row context menus. Future fill is welcome but not required for v2 conformance.

**Rationale.** Console UX precedent (Xbox dashboard, Steam Big Picture, every console RPG inventory). Frees the visible UI from button rows that exist only to host secondary actions; X is the discoverable channel for "what else can I do here?".

**Scope.** Any element with secondary actions OR any footer button with a designed feature shortcut.

**Edge cases.**
- Element with no secondary actions: X is no-op, do not consume the input.
- Element shared across mouse / controller paths must use the shared popup builder (CC5).

**Universal application.** Mandatory wherever X has a designed semantic. Audit A.2 entries to convert: room.cpp multi-select (right-click today, X tomorrow), mpsettings.cpp track preview, network.cpp paste (could go to a connect-code context menu), agentselect.cpp copy / delete / default.

### Rule 6 -- Y: Social menu (Main Menu + Pause Menu only per Q4 2026-05-03)

Y opens the Social overlay (friends, party invites, voice chat, recent players) as a top-of-stack modal that does not unwind the menu beneath it. A persistent "Y :: Social" glyph is rendered in the upper-right corner of the menu screen, docked to the bottom edge of the Online status indicator.

**Verbatim JSON SUPERSEDED by Q4 (2026-05-03):** the v2 JSON ships Y bound to "open social menu to allow for invites and whatnot" on every focusable element of Combat Sim. Mike's Q4 directive INVERTS this:

> "The Y-Social menu should be accessible from the Main Menu system and Pause Menu so players can always connect with one another. Glyph in upper right corner docked to the bottom of the Online status, which appears in the same locations (only) as stated above (main menu system and pause menu)"

Translation: Y-Social binding lives ONLY on Main Menu screens AND in-game Pause Menu. NOT universal across Combat Sim, Forge, Settings, modmgr, theme editor, agent editor, etc. The glyph follows the same scope (Main Menu + Pause Menu only) and is docked to the bottom of the Online status indicator (which appears in those same two locations only).

**Rationale.** Social is the one menu that should be one button away from anywhere PLAYERS LIKELY CONNECT. The Main Menu and Pause Menu are the natural connection-time touchpoints (pre-match queueing, mid-match recoordination); other in-flight menus (mid-Combat-Sim configuration, mid-Forge editing, mid-Settings adjustment) are focused-task surfaces where Y-as-social would be a stray modal interruption. Per Mike's Q4: keep Y-Social to where it actually serves users.

**Scope.** Main Menu screens AND in-game Pause Menu only. NOT universal across other menus (Combat Sim, Forge, Settings, etc.). On those other screens, Y is undefined -> per Rule 10 (skip-empty-bindings), focus does not stop on Y, no glyph hint is rendered, and Y is no-op at the screen level.

**Edge cases.**
- Overlay-on-overlay: Y while Social is already open is no-op.
- Closing Social (B / Esc) returns to the exact prior focus state in the menu beneath.
- Per-row Y bindings on non-Main / non-Pause screens (e.g. legacy bot-row multi-select Y in pdgui_menu_room.cpp ~line 2100) are pre-existing per-row Y reuses, NOT Rule 6 violations. Disambiguate during the per-element pass: either remove the per-row Y binding (favoring the universal undefined contract) or document the deviation inline. Tracked in c086 follow-up.
- B-double-press regression note: the B-button-related fix at commit `9b6d2a9c` (framerate persistence + B-double-press menu reopen) is contract-discipline-based, not test-suite-based. Per Mike's Q-E (2026-05-03) "Fix it, if we happen to get a regression later we will go with a deeper protection. It will only break during development, so we will try to avoid the bug by just following standards to prevent it and similar." Honor the documented IsWindowAppearing flush contract; if a regression surfaces, deeper protection is added then.

**Universal application.** Mandatory for Main Menu + Pause Menu. Forbidden as a screen-level binding for any other menu. The persistent glyph rollout (CC4) follows the same Main+Pause scope.

### Rule 7 -- LB / RB: previous / next tab (universal)

**Mike's Q1 decision (2026-05-03), CANONICAL:** LB / RB are the universal previous / next tab binding across every focusable element on every menu screen. They cycle the outermost tab strip. This preserves the existing project convention shipped across `pdgui_menu_mainmenu.cpp:1745-1746` (settings tabs IMC defaults), `pdgui_menu_mainmenu.cpp:4550-4552` (settings tab handler authority comment), `pdgui_menu_room.cpp:3661` (room tab switching), and the migration commits at solomission / room / modmgr / lobby / pausemenu / moddinghub.

**Reconciliation with the verbatim JSON:** the verbatim JSON file ships LB / RB on player rows as "previous / next team first player wrapping" (team-jump). Mike's Q1 decision INVERTS this: LB / RB on player rows is also previous / next tab, NOT team-jump. The team-jump affordance moves to LT / RT (see Rule 8). The verbatim JSON's player-row LB / RB cells reflect the v2 author's original intent; the project-canonical decision overrides those cells. The per-element binding doc shows the canonical resolution and annotates the divergence.

**Rationale.** The codebase already has `ACTION_MENU_TAB_PREV` ("LB / PageUp in settings tabs") and `ACTION_MENU_TAB_NEXT` ("RB / PageDown in settings tabs") declared in [pdgui_menu_mainmenu.cpp:1745-1746](../../../port/fast3d/pdgui_menu_mainmenu.cpp:1745). Mike's Q1 keeps the convention universal so that LB / RB always means "swap to a sibling top-level view" everywhere. Team-jump can move to triggers without disrupting tabs.

**Scope.** Every focusable element on every tabbed menu screen. Idle (no-op) on menus without a top-level tab strip.

**Combat Simulator application.** From any focused row (left panel, right panel, footer, link), LB cycles to the previous of `tab.combatsim / tab.campaign / tab.coop / tab.editor`; RB cycles to the next, wrapping. Verbatim JSON's player-row "team-jump" cells are overridden; team-jump moves to LT / RT per Rule 8.

**Action-map binding (existing).** `ACTION_MENU_TAB_PREV` and `ACTION_MENU_TAB_NEXT` already in `g_ImcMenu` defaults. Both gamepad LB / RB and KB&M PageUp / PageDown route to these actions ([pdgui_menu_mainmenu.cpp:4550-4552](../../../port/fast3d/pdgui_menu_mainmenu.cpp:4550) authority comment).

**Edge cases.**
- Single-tab menu (no strip): LB / RB no-op.
- Inner tab strips (Settings -> Controls inner KB&M / Controller tabs per audit C.5): outer LB / RB remains the authority. Inner tab swap binds to D-pad at the inner-tab focus level instead. This was already audit C.5's recommendation; v2 confirms.

**Universal application.** Mandatory. Player-list menus that previously used LB / RB for team-jump must move team-jump to LT / RT.

### Rule 8 -- LT / RT: skip-up / skip-down (within-panel grouping advance + page-jump fallback)

**Mike's Q2 decision (2026-05-03) + Q-A naming + Q-B walker + Q-C fallback (all 2026-05-03), CANONICAL:** LT / RT advances focus by the **next-larger-than-row grouping unit** available within the **focused panel**, with a **page-jump fallback** for flat lists. Action constants per Mike's Q-A: `ACTION_MENU_SKIPUP` (LT) and `ACTION_MENU_SKIPDOWN` (RT) -- the skip-noun semantic emphasises "skip past the next chunk" rather than "jump to a typed boundary" (per Mike's Q-A inversion of the SECTION_*/GROUP_* proposals). NOT a list-bounds (top / bottom) affordance.

**Within-panel only (per Q-C 2026-05-03):**

> "Page Jump, within the same panel, otherwise no-op."

LT / RT NEVER crosses panels. Cross-panel traversal stays on D-pad (Rule 1). LT / RT operates only inside the panel that owns the current focus. This contract is uniform: the focused panel is the operating frame; what's outside the focused panel is outside Rule 8's scope.

**Per-screen semantics (within the focused panel):**

| Panel structure | LT / RT semantic |
|--------------------|------------------|
| Player roster with team dividers | Jump to next / previous team's first player |
| Long flat list with section headers | Jump to next / previous section header |
| Paginated content | Advance by page |
| Flat scrollable list, no groups | Page-jump by visible-row-count within the panel scroll |
| Neither groups NOR scroll | LT / RT no-op (Rule 10 skip-empty: no glyph hint either) |

**Dynamic walker contract (per Q-B 2026-05-03):**

> "Dynamic walker is the only real choice as we have a fully dynamic system."

Each screen exposes a callback (or interface method) that, given the current focus, returns the next/previous skip target within the focused panel. No static-metadata fallback. Every screen owns its own walker. A default helper for fixed-metadata cases (e.g. arrays of `firstRowOfTeam[N]` indices computed during the row build pass) is OK as a convenience implementation, but the universal contract is the dynamic callback. The menu input layer wires the contract once; screens implement the walker as needed for `ACTION_MENU_SKIPUP` / `ACTION_MENU_SKIPDOWN`.

**Reconciliation with the verbatim JSON:** the verbatim JSON ships LT / RT on team.1 player rows as "top of players list" / "bottom of players list" (list bounds). Mike's Q2 INVERTS this: LT / RT on player rows is "previous / next team's first player" (team-jump, the affordance that LB / RB used to host per the v2 author's intent). Note that team.2 player rows in the verbatim already have empty LT / RT, an asymmetry that the canonical decision resolves (both teams get team-jump).

**Rationale.** Long lists are tedious one-row-at-a-time; a designed grouping-jump is a console UX standard. Generalising "skip past the next chunk" rather than "absolute bounds" means the same physical input does the right thing in every menu without a per-screen specialisation. The within-panel constraint (Q-C) prevents LT / RT from racing with D-pad cross-panel traversal -- each input owns one axis of motion.

**Scope.** Every focused panel. LT / RT are no-op for the screen ENTIRELY when neither the focused panel has groups nor the focused panel scrolls.

**Combat Simulator application (within each panel):**
- Tabs (4): not a panel. LT / RT no-op when focus is on the tab strip.
- Left-panel (12 interactive rows + 6 section headers): groups = section headers. LT / RT jumps to next / previous section header within the left panel (arena -> gametype -> limits -> weapons -> options). Cross-panel right side is NOT touched by LT / RT.
- Right-panel (Team Sort Dropdown above team.1 divider, then player rows split by team dividers, then Add Bot):
  - On Team Sort: no groups above the team.1 divider in the right panel. LT no-op; RT could optionally jump down to first team's first player as a courtesy (per-screen choice).
  - On Player rows: groups = team dividers. LT / RT jumps to previous / next team's first player WITHIN the right panel (the affordance the verbatim JSON had on LB / RB).
  - On Add Bot (below last team): LT goes back to last team's first player; RT no-op (boundary).
  - When teams are OFF: right panel is a flat scrollable list of players + bots. Page-jump fallback: LT / RT page by visible-row-count.
- Footer (Start Match, Back to Menu link): not a scrollable panel. LT / RT no-op.

**Action-map binding (NEW).** `ACTION_MENU_SKIPUP` and `ACTION_MENU_SKIPDOWN` added in `port/include/actionmap.h` (= 105 / 106) per Q-A. Bound to LT / RT (gamepad) and Home / End (kbd) on `g_ImcMenu` + `g_ImcPauseMenu` in `port/src/actionmap.cpp` (`setupMenuDefaults` + `setupPauseMenuDefaults`). Helpers `pdguiMenuSkipUpPressed` / `pdguiMenuSkipDownPressed` exposed in `port/include/pdgui_nav.h`.

**Edge cases.**
- Grouping unit changes between focus locations within the focused panel (focus is in a player row -> grouping = team; focus moves to Add Bot -> still in the same panel, walker decides). Per-frame computation of the grouping target is cheap and matches Mike's Q-B dynamic-walker contract.
- LT at the first group / RT at the last group: stays on boundary, does not wrap (consistent with Rule 1 D-pad U/D no-wrap).
- Q-C fallback: when the panel has no groups but DOES scroll, page-jump within the panel by visible-row-count. Boundary clamps to first / last row. Visible-row-count can be approximated (e.g. fixed N rows) -- the fallback is "approximate" by design; the no-wrap clamp guarantees it never lands outside the panel.
- Q-C absolute fallback: when the panel has neither groups NOR scroll, LT / RT is no-op AND Rule 10 (skip-empty-bindings) means no glyph hint is rendered for it on that screen.

**Universal application.** Mandatory. Any existing LT / RT bindings (currently rare in PC port) must align. The action-map additions are the single change point for the rollout. Existing menus that haven't yet implemented a walker should default to the page-jump fallback when their panel scrolls; otherwise no-op until a walker lands.

### Rule 9 -- Reserved-with-one-shortcut: stick clicks, Start, Select

The remaining four inputs are mostly reserved, with one designed exception:

**L-stick click, R-stick click, Select / Back: universally reserved.** Verbatim JSON confirms empty across every focusable element. No menu may bind. (Gameplay layer uses these for crouch / zoom; menu layer ignores.)

**Start: navigational shortcut to the menu's primary commit action where one exists.** Verbatim JSON binds Start to "move to Start Match button" on every right-panel element (Team Sort Dropdown, all player rows, Add Bot Button); empty on tabs, left-panel rows, footer buttons, and the Back to Menu link. The pattern: Start jumps focus to the menu's primary commit widget without pressing it (A still required to commit). Menus without a primary commit action leave Start unbound.

**Rationale.** Reserve future expansion surface; gameplay layer already uses sticks-click and Select for crouch / zoom / pause. Start has the designed semantic of "skip navigation, go to the go button" since the user's most common destination after configuring a menu is the commit action.

**Scope.** L-stick click, R-stick click, Select: every focusable element. Start: opt-in per menu, where a primary commit widget exists.

**Combat Simulator application:** Start jumps to Start Match Button from any right-panel focus. Left-panel rows and tabs leave Start empty (the user is expected to D-pad through the configuration before reaching for Start).

**Universal application.** Mandatory for stick clicks and Select. Start is opt-in: any menu with a clear primary commit action MAY register a Start handler that sets focus to that widget; menus without a primary commit MUST leave Start unbound.

### Rule 10 -- Skip empty bindings: focus does not stop on undefined-action elements

**Mike's directive (2026-05-03), CANONICAL:** "The things where I didn't put input should be skipped when navigating." A focusable element whose binding cells are empty for the active directional input MUST be SKIPPED during focus traversal -- the next focusable element in the traversal direction receives focus instead. Focus does not stop on a widget that has no defined action for the input the user just pressed.

**Why this is a universal rule, not a Rule 1 footnote.** Rule 1 names the column-traversal contract (D-pad / L-stick = focus up/down within panel, left/right = cross-panel). It does not say what happens when a widget has no defined action for that direction. The naive ImGui default is to LAND focus on every focusable widget regardless of binding completeness, producing dead stops where the user presses D-pad and the focused widget responds to nothing. Rule 10 closes that gap: the binding manifest is the source of truth for which elements participate in which directional flows. An element with no D-pad Up binding is invisible to upward traversal; an element with no LT/RT binding is invisible to section-jump traversal; etc.

**Scope.** Every focusable element. Every directional input class.

**Implementation strategies (pick one per element type, document inline):**

1. **Mark element non-focusable** when it has no bindings at all -- e.g. section headers, dividers, decorative labels (CC1 generalised). `ImGuiSelectableFlags_Disabled` or the shared `pdguiSectionHeader(...)` helper.
2. **Per-direction NoNav** when the element is focusable for SOME directions but not others -- the renderer suppresses focus arrival from a direction the element does not respond to. Implementation through an opt-out predicate the menu checks before `ImGui::SetItemDefaultFocus()` / before consuming a navigation event.
3. **Bindings manifest gate** when the element type has many instances and the empty cells are consistent across instances -- the menu's dynamic walker (Rule 8 Q-B) consults the manifest and routes traversal around empty cells.

**Combat Simulator examples (verbatim, post-Q1+Q2):**

- Section headers (sec.arena, sec.gametype, sec.limits, sec.weapons, sec.options, sec.players): every binding cell empty -> implementation strategy 1 (CC1 already covers this).
- Team dividers (team.1, team.2): every binding cell empty -> strategy 1 (CC1).
- Footer Back link: D-pad U/D/L/R all empty (it is the final destination) -> strategy 2 (the link is focusable for A/B/Y but absorbs no D-pad input). D-pad pressed while focused on the link is no-op rather than landing on a phantom successor.
- Add Bot Button when no bots are present and no team to step back into: LT empty -> strategy 3 (LT no-ops at the boundary). Currently encoded by Rule 8's "stays on boundary, does not wrap" clause.

**Edge cases.**

- An element with NO defined directional bindings AND no A action is degenerate; it should not be focusable at all (strategy 1 + remove from layout).
- A direction with EXPLICIT no-op semantics (Rule 8 boundary "stays on boundary, does not wrap") is not the same as "skip" -- the boundary case keeps focus on the current element. Rule 10 applies when the element does not own the input at all.
- Mouse focus / hover is unaffected -- Rule 10 is about controller / keyboard navigation only.

**Universal application.** Mandatory. New menus default to "if a cell is empty in the binding manifest for a focusable element, focus does not arrive on that element from that direction." Existing menus audit their focusable widgets against the binding manifest; gaps file as bugs in `context/bugs.md` per CC3 audit pattern.

**Relation to Rule 1.** Rule 1 names the traversal contract (where focus goes when the user presses a direction). Rule 10 names the participation contract (which elements receive focus from which directions). Together: Rule 1 is the path, Rule 10 is the membership predicate. A menu satisfies Rule 1 only after it satisfies Rule 10 -- otherwise traversal lands on dead nodes that violate Rule 1's "focus moves to a control that responds to the user's intent" semantic.

## Cross-cutting invariants

### CC1 -- Section headers and dividers are not focusable

Visual section headers (Arena Header, Game Type Header, Limits Header, Weapon Set Header, Options Header, Players in Room Header) and team dividers (Team 1, Team 2) must skip focus. Verbatim JSON confirms: every binding cell on every section / divider element is empty.

Implementation: `ImGuiSelectableFlags_Disabled` or a shared `pdguiSectionHeader(...)` helper that wraps the disabled-selectable pattern. The current pattern using a focusable-but-no-action `Button` is not acceptable.

**Why:** headers exist to chunk the visual layout; making them focusable forces the user to step through dead nodes that have no activation behaviour, and breaks the LT / RT section-jump semantic (jump SHOULD land on the first focusable widget below the header, not on the header itself).

### CC2 -- Cross-panel D-pad navigation requires NavFlattened

Two-panel layouts must mark both children `ImGuiChildFlags_NavFlattened`. Rule 1's "Left / Right crosses panels" depends on this. The NavFlattened migration is the existing infrastructure (audit B); finish the gaps. The theme editor (audit A.2 row 7) is the canonical remaining offender.

### CC3 -- B-as-confirm-modal is non-negotiable

Restated from Rule 4 because it is the most-violated invariant in the codebase today. Any menu that B-closes silently with in-progress state is a bug to file in `context/bugs.md` and convert.

### CC4 -- Y-Social glyph: Main Menu + Pause Menu only (Q4 2026-05-03)

Restated from Rule 6. Per Mike's Q4 directive (2026-05-03), Y-Social and its glyph live ONLY on Main Menu screens AND in-game Pause Menu. The glyph renders in the upper-right corner of those screens, docked to the bottom edge of the Online status indicator (Online status appears in the same two locations only). NOT universal across other menus -- on Combat Sim, Forge, Settings, modmgr, theme editor, etc., the glyph is NOT rendered AND Y is undefined.

Implementation:

- Shared `pdguiSocialGlyph()` helper renders the glyph; called only from `pdgui_menu_mainmenu.cpp` and `pdgui_menu_pausemenu.cpp` render entries.
- Online status indicator placement is defined in those two files; the glyph docks to its bottom edge (same horizontal position, glyph y = status_y + status_height).
- No opt-out flag needed: the helper is opt-in per the two screens.
- Rollout: kanban c087 (Main Menu Y-Social binding + glyph + status docking) and c088 (Pause Menu Y-Social binding + glyph + status docking), priority 2 each.

The verbatim JSON's Y-Button cell text on every row of Combat Sim mentioned this glyph explicitly ("should have a contextual glyph prompt in the lower right of screen"); Q4 SUPERSEDES that expansion -- the glyph and the binding both restrict to Main + Pause only.

### CC5 -- Same popup builder backs mouse right-click and controller X

Restated from Rule 5. Avoids inventory drift between input devices. Convert existing right-click handlers (room.cpp, mpsettings.cpp, network.cpp) to call the same shared popup builder that X uses.

## Conformance targets

Menus to bring into conformance with this grammar after v2 lands. Listed roughly in order of user-visibility / pain. Each has an existing audit entry (see [input-menu-system-audit-2026-05-01.md](../../audits/input-menu-system-audit-2026-05-01.md)) that names current state and required change.

| Menu | File | Status today | Required change |
|------|------|--------------|------------------|
| Combat Simulator | [pdgui_menu_mpsetup.cpp](../../../port/fast3d/pdgui_menu_mpsetup.cpp) | Foundation pass shipped (S609 trifecta) | Q1 (LB/RB tab cycle) inherited. Q2 / Q-A / Q-B / Q-C: SkipUp/SkipDown wired in player panel (team-jump when teamsOn, page-jump when teamsOff). Q4: Y is undefined here -- NOT bound to social. Per-element bindings (X context popups, left-panel section-jump walker, Start jump-to-Start-Match, A+B convergence on Back link) tracked in c086. |
| Pause menu | [pdgui_menu_pausemenu.cpp:694-724](../../../port/fast3d/pdgui_menu_pausemenu.cpp:694) | 4 PdPauseButton row, no real BeginTabBar (audit A.2 row 7) | Convert to BeginTabBar; inherit Rule 7 LB/RB tab cycle. **Bind Y-Social per Q4 (kanban c088, priority 2).** Render social glyph in upper-right docked to Online status. |
| Main menu | [pdgui_menu_mainmenu.cpp](../../../port/fast3d/pdgui_menu_mainmenu.cpp) | Top-level main menu surfaces | **Bind Y-Social per Q4 (kanban c087, priority 2).** Render social glyph in upper-right docked to Online status. |
| Settings | [pdgui_menu_mainmenu.cpp:4545+](../../../port/fast3d/pdgui_menu_mainmenu.cpp:4545) | Strong; LB/RB outer tab cycle live (line 4550-4552) | **Y-Social NOT bound here (Settings is not Main / Pause).** Add Rule 9 Start binding for Apply where applicable. Add Rule 8 LT/RT skip-up/skip-down within long lists. |
| Settings -> Controls (inner) | [pdgui_menu_mainmenu.cpp:3090, 3125](../../../port/fast3d/pdgui_menu_mainmenu.cpp:3090) | Audit C.5 known issue | Inner KB&M / Controller tab swap goes to D-pad at inner-tab focus. Outer LB / RB stays Settings authority. |
| Mod Manager | [pdgui_menu_modmgr.cpp](../../../port/fast3d/pdgui_menu_modmgr.cpp) | Strong; bumper tab cycle live | Add Rule 6 glyph. |
| Modding Hub | [pdgui_menu_moddinghub.cpp](../../../port/fast3d/pdgui_menu_moddinghub.cpp) | 9-tool toolbar with bumper cycle (audit A.5 #3) | Reconsider: 9 tools exceeds Combat Sim's 4-tab ceiling; consider grouping into 4 categories. Out of scope for this rollout. |
| Theme Editor | [pdgui_menu_theme_editor.cpp](../../../port/fast3d/pdgui_menu_theme_editor.cpp) | Audit A.2 row 7: cross-column NavFlattened missing | CC2: NavFlattened on preview + palette children. |
| Skin Editor | [pdgui_skin_editor.cpp](../../../port/fast3d/pdgui_skin_editor.cpp) | Audit A.2 row 5: canvas mouse-only | Out of scope (canvas controller is its own design problem); surrounding panels conform. |
| Agent Editor | [pdgui_menu_agentselect.cpp:327, 337, 347](../../../port/fast3d/pdgui_menu_agentselect.cpp:327) | C / Delete / D keyboard-only (audit A.2 row 4) | Rule 5 X-context-menu replaces those: X opens "Copy / Delete / Reset to default" popup. |
| Lobby / Room rosters | [pdgui_menu_room.cpp:2023, 2028](../../../port/fast3d/pdgui_menu_room.cpp:2023); [pdgui_menu_lobby.cpp](../../../port/fast3d/pdgui_menu_lobby.cpp) | Mouse-only multi-select + context menu (audit A.2 row 1, 5) | Rule 5 X-context-menu replaces right-click. Rule 7 LB/RB stays tab-cycle universal; Rule 8 LT/RT does team-jump in player lists. |
| Forge editor | [pdgui_forge_editor.cpp](../../../port/fast3d/pdgui_forge_editor.cpp) | Most controller-aware menu in project; uses `g_CtxForgeEditor` exemption per Phase 2 fix #9 | Verify CC4 glyph. |
| Weapon-loadout modal | (TBD location) | Standard conformance check | Verify all 9 rules. |
| Botsetup / mpsetup / mppause / endscreen / warning / training / logviewer | port/fast3d/pdgui_menu_*.cpp | Bulk default conformance | Audit pass + CC4 glyph rollout. |

## Phase 2 fix alignment (2026-04-27 through 2026-05-03)

The 9-fix Phase 2 sequence from [input-menu-system-audit-2026-05-01.md](../../audits/input-menu-system-audit-2026-05-01.md) aligns with this grammar.

| Phase 2 fix | Commit (dev) | Touches | Grammar tie-in |
|-------------|--------------|---------|------------------|
| #1 Press vs Hold (`actionWasTap`) | parallel session | [actionmap.cpp](../../../port/src/actionmap.cpp) | Underpins every "press X" rule (1-9). Any rule that says "press X" is `actionWasTap` semantics, not raw down-edge. |
| #2 Right-stick = innermost scrollable | parallel session | `pdguiInnermostScrollableForNav` in [pdgui_backend.cpp](../../../port/fast3d/pdgui_backend.cpp) | Rule 2 contract. The walker is the single authority. |
| #3 Cross-panel + Settings inner-Controls | parallel session | NavFlattened sweep across [pdgui_menu_*.cpp](../../../port/fast3d/) | Rule 1 (cross-panel) + Rule 7 reconciliation (outer Settings keeps LB/RB; inner Controls uses D-pad). |
| #4 Per-menu accessibility sweep (incl. B-298 vehicle wire-up) | 56ea32ac | [bondbike.c](../../../src/game/bondbike.c) | Outside menu layer (vehicle layer); rule applies to its own LayerType context. |
| #5 Cone tightening (interaction cast) | 10ad8ca1 | [propobj.c:16434](../../../src/game/propobj.c:16434), [prop.c](../../../src/game/prop.c) | Gameplay layer; the focus-and-activate analog for world objects. Default `propGetInteractCastHalfAngleRad()` = pi/12 (15 deg half / 30 deg full). |
| #6 Interaction cast debug visualisation | 10ad8ca1 | [pdgui_debugmenu.cpp](../../../port/fast3d/pdgui_debugmenu.cpp), [main.c](../../../port/src/main.c) | Settings -> Debug Flags toggle; supports empirical tuning of #5. |
| #7 Gate MENUBG_BLACK / MENUBG_8 under ImGui | 94aae1b7 | [menu.c:5616](../../../src/game/menu.c:5616) | HUD chrome; surrounds CC4 glyph rollout (do not double-render legacy + ImGui chrome). |
| #8 Killfeed delete OG + content-fit ImGui width | b163760c | [mpstats.c](../../../src/game/mpstats.c), [pdgui_menu_mpingame.cpp](../../../port/fast3d/pdgui_menu_mpingame.cpp) | HUD chrome consistency; same imgui-replaces-legacy pattern as #7. |
| #9 g_CtxForgeEditor architectural close of B-195 | 10f29f26 | [inputctx.c](../../../port/src/inputctx.c), [inputctx.h](../../../port/include/inputctx.h), [forgemode.c](../../../src/game/forgemode.c) | Hybrid input context: cursor + ImGui kbd capture + gameplay-axis exemption. The cleanest example of "menu-style focus + gameplay-axis suppression exemption". |
| Follow-up: framerate persistence + B-double-press menu reopen | 9b6d2a9c | [video.c:318](../../../port/src/video.c:318), [pdgui_menu_mainmenu.cpp](../../../port/fast3d/pdgui_menu_mainmenu.cpp) | Direct Rule 4 enforcement: B-double-press fix added `actionmapFlushActionSet({MENU_CANCEL, MENU_ACCEPT, USE, CANCEL_USE})` on `IsWindowAppearing` to prevent stale B-down events from re-closing the menu. |

## Deferred features (logged here, not in v2 implementation)

### Queue Match (Rule 5 on Start Match)

**Trigger:** X on Start Match in Combat Simulator.

**Description (verbatim from JSON):** "Stores an array of match start settings and cycles them as matches end; host can either 'end game' or 'skip match' in their pause menu); display number of queued match settings."

**Status:** P3 deferred. Tracked as kanban card c081 (Input pillar, backlog column).

**Design implications:** pause menu surface additions, multi-match state machine, queue-config persistence, host-side pause-menu controls ('end game', 'skip match'), UI badge counter. Net protocol may need a SVC_QUEUE_STATE message.

**Why deferred:** universal grammar conformance (Rules 1-9 across all menus + CC1-CC5) is the foundational platform spec; Queue Match is one feature surface that lands on top of that platform. Should not block grammar rollout.

## Resolved decisions (2026-05-03 trifecta + follow-up)

The following items were resolved by Mike's 2026-05-03 directives:

- **Q1 LB / RB:** Universal previous / next tab. Player-row team-jump moves to LT / RT. Verbatim JSON cells for player-row LB / RB are overridden by canonical decision.
- **Q2 LT / RT:** Within-panel skip-up / skip-down (was section / team / page-jump in Q2 framing; per Q-A renamed to `SKIPUP` / `SKIPDOWN`; per Q-C constrained to within-panel only). Player-row "list bounds" verbatim cells are overridden.
- **Q3 Action-map naming:** `ACTION_MENU_CONTEXT` (X) and `ACTION_MENU_SOCIAL` (Y) shipped as `#define` aliases over `ACTION_MENU_SECONDARY` / `ACTION_MENU_TERTIARY` (preserves test/binding compatibility, exposes the rule-named identifiers in source).
- **Q4 Y-Social scope (2026-05-03 follow-up, INVERTS v2 JSON):** Y-Social is restricted to Main Menu + in-game Pause Menu only. NOT universal across other menus. On other screens, Y is undefined per Rule 10 (no focus stop, no glyph hint). The verbatim JSON's per-element Y bindings on Combat Sim are SUPERSEDED.
- **Q5 Continue here:** Synthesis completed in S609 trifecta (2026-05-03).
- **Q-A (2026-05-03 follow-up) Action constant naming for LT/RT:** `ACTION_MENU_SKIPUP` / `ACTION_MENU_SKIPDOWN`. Skip-noun semantic (skip past the next chunk) chosen over SECTION_*/GROUP_* (jump to a typed boundary). Shipped at = 105 / 106.
- **Q-B (2026-05-03 follow-up) Group/section traversal contract:** Dynamic walker only ("the only real choice as we have a fully dynamic system"). Each screen exposes a callback (or interface method) that, given current focus, returns the next/previous skip target. No static-metadata fallback. Default helper for fixed-metadata cases (e.g. arrays of `firstRowOfTeam[N]`) is OK as a convenience implementation, but the universal contract is the dynamic callback.
- **Q-C (2026-05-03 follow-up) LT/RT on flat lists with no groups:** Page-jump within the same panel; no-op otherwise. LT/RT NEVER crosses panels (D-pad does cross-panel; LT/RT stays within the focused panel's scroll). Screens with neither groups NOR scroll: LT/RT is no-op AND Rule 10 means no glyph hint is rendered.
- **Q-D (covered above by Rule 6 update + CC4):** Y-Social glyph in upper-right corner, docked to the bottom of the Online status indicator. Online status + Y-Social glyph appear ONLY on Main Menu + Pause Menu.
- **Q-E (2026-05-03 follow-up) B-double-press regression cohort:** No preemptive deeper protection. Mike: "Fix it, if we happen to get a regression later we will go with a deeper protection. It will only break during development, so we will try to avoid the bug by just following standards to prevent it and similar." The 9b6d2a9c fix stays as-is. Contract-discipline-based, not test-suite-based. Captured in Rule 6 edge cases.

(Legacy "Decision A" through "Decision E" sub-sections were superseded by Q-A through Q-E above and removed in the 2026-05-03 trifecta follow-up; see the resolved-decisions list for the canonical answers.)

## Methodology

- No em-dashes (project-wide convention; verbatim JSON cells preserved as-is).
- Hierarchical log channels reserved: `INPUT.GRAMMAR.*` for any new diagnostics this rollout adds.
- Every binding cites a rule by number when added or changed in source.
- Per-menu deviation from a rule gets a source-file header comment naming the rule and the reason.
- `[CONTEXT STATE: turns=N, compactions=N, self-assessment=...]` annotation on every meaningful surface produced under this grammar.

## Cross-references

- Phase 1 audit: [input-menu-system-audit-2026-05-01.md](../../audits/input-menu-system-audit-2026-05-01.md)
- Phase 1 design (input universality): [input-universality-and-transitions.md](../input/input-universality-and-transitions.md)
- Menu pool architecture: [input-authority-and-menu-pool.md](../menus/input-authority-and-menu-pool.md)
- Menu stack architecture: [menu-stack-architecture.md](../menus/menu-stack-architecture.md)
- Action map source: [actionmap.h](../../../port/include/actionmap.h), [actionmap.cpp](../../../port/src/actionmap.cpp)
- IMC stack source: [inputlayer.c](../../../port/src/inputlayer.c)
- Menu pool source: [menupool.c](../../../port/src/menupool.c)
- Innermost-scrollable walker: `pdguiInnermostScrollableForNav` in [pdgui_backend.cpp](../../../port/fast3d/pdgui_backend.cpp)
- Settings tab handler authority: [pdgui_menu_mainmenu.cpp:4545-4552](../../../port/fast3d/pdgui_menu_mainmenu.cpp:4545)
- Settings IMC tab actions: [pdgui_menu_mainmenu.cpp:1745-1746](../../../port/fast3d/pdgui_menu_mainmenu.cpp:1745)
- Room tab switching: [pdgui_menu_room.cpp:3661](../../../port/fast3d/pdgui_menu_room.cpp:3661)
- v2 binding data (verbatim): [combat-simulator-bindings-v2.json](combat-simulator-bindings-v2.json)
- v2 per-element implementation spec (Q1+Q2 reconciled): [combat-simulator-binding-doc.md](combat-simulator-binding-doc.md)
