# Input - Combat Simulator Binding Spec (v2)

> Status: **CANONICAL** per-element implementation spec.
> Date: 2026-05-03
> Pillar: joint Menu Stacking + Input.
> Implements: [menu-input-interaction-grammar.md](menu-input-interaction-grammar.md).
> Source: verbatim JSON at [combat-simulator-bindings-v2.json](combat-simulator-bindings-v2.json) (1085 lines, generatedAt 2026-05-03T07:33:49.545Z).
> Visual reference: rendered menu screenshot pending upload reach (`365307cc-image.png`); embed will be added when reachable.
> Supersedes: any v1 binding doc.

## Q1+Q2 reconciliation note (READ FIRST)

The verbatim JSON ships with two semantics that Mike's 2026-05-03 directive INVERTS:

1. **Player-row LB / RB** in verbatim = "previous / next team first player wrapping" (team-jump). **Project-canonical per Mike Q1** = previous / next tab (universal). This binding doc shows the canonical resolution; the verbatim cells are preserved for reference only.
2. **Player-row LT / RT** in verbatim = "top / bottom of players list" (list bounds). **Project-canonical per Mike Q2** = section / team / page jump (the team-jump affordance moves here from LB / RB). Again, this binding doc shows the canonical resolution.

Quoting Mike's directive: "Source JSON shipped with LB/RB=team-jump, LT/RT=bounds; project-canonical decision per Mike 2026-05-03 INVERTS this -- LB/RB=tab cycle (universal), LT/RT=section/team/page jump."

Every other cell in the binding doc transcribes the verbatim JSON faithfully. Where a cell text below differs from the verbatim, this section explains why.

## Visual reference

[Pending: embed `combat-simulator-rendered-2026-05-03.png` once the screenshot upload reaches the worktree. The screenshot shows annotated hotspot numbers on every interactive element.]

## Element inventory (from verbatim JSON)

The verbatim JSON enumerates 30 controls in 8 categories:

- **4 tabs** (top of screen): `tab.combatsim`, `tab.campaign`, `tab.coop`, `tab.editor`.
- **6 section headers** (non-focusable per CC1): `sec.arena`, `sec.gametype`, `sec.limits`, `sec.weapons`, `sec.options`, `sec.players`.
- **2 team dividers** (non-focusable per CC1): `team.1`, `team.2`.
- **13 left-panel interactive rows**: `row.arena` (dropdown), `row.gametype` (dropdown), `opt.teams` (option), `row.time` (slider), `row.score` (slider), `row.weapons` (dropdown), `opt.autoaim` (option), `opt.jump` (option), `opt.radar` (option), `opt.onehit` (option), `opt.friendlyfire` (option), `opt.fastmove` (option). (12 here; the 13th is `row.teamsort` which sits in the right panel above the team.1 divider.)
- **1 right-panel sort dropdown**: `row.teamsort`.
- **8 player rows**: `p.you`, `p.joanna`, `p.bot1`, `p.bot2`, `p.bot3` (team.1, 5 rows); `p.bot4`, `p.bot5`, `p.bot6` (team.2, 3 rows). (Verbatim has 8 player rows total; the v2 paraphrase referenced "9" but the verbatim count is authoritative.)
- **1 add bot button**: `row.addbot`.
- **2 footer elements**: `footer.start` (button), `footer.back` (link).

## Per-element binding tables

Cells below are CANONICAL (post-Q1+Q2-reconciliation). Where the canonical resolution differs from the verbatim, the verbatim text appears in italics in a footnote-style note attached to the affected row. Universal rules (Rule N) are cited where applicable.

### Tabs (4)

The four top tabs (`tab.combatsim`, `tab.campaign`, `tab.coop`, `tab.editor`) all have completely empty bindings in the verbatim JSON. They are the targets of LB / RB universal cycle from any focused element on the screen, not focus owners with their own bindings. Tab strip handler at [pdgui_menu_mainmenu.cpp:4545+](../../../port/fast3d/pdgui_menu_mainmenu.cpp:4545) is the canonical reference for tab cycle implementation.

When the focus IS on a tab strip header (entered via D-pad Up from below or by Start where applicable):
- A: activate tab (switch to its content)
- B: Rule 4 exit modal
- Y: Rule 6 social
- LB / RB: Rule 7 cycle (no-op since already on tab strip; or: cycle to sibling tab)
- All other inputs: idle per verbatim

### Section headers (6, non-focusable per CC1)

| ID | Type | Focusable |
|----|------|-----------|
| sec.arena | section | NO |
| sec.gametype | section | NO |
| sec.limits | section | NO |
| sec.weapons | section | NO |
| sec.options | section | NO |
| sec.players | section | NO |

All bindings empty per verbatim. Implementation: `pdguiSectionHeader(...)` helper or `ImGuiSelectableFlags_Disabled`.

### Team dividers (2, non-focusable per CC1)

| ID | Type | Focusable |
|----|------|-----------|
| team.1 | divider | NO |
| team.2 | divider | NO |

All bindings empty per verbatim. Same implementation pattern as section headers.

### Left-panel interactive rows (13)

All 13 rows share the same shape (D-pad / L-stick column traversal + cross-panel; A activates type-specific; B Rule 4 modal; Y Rule 6 social; LB / RB Rule 7 tab cycle (canonical, NOT in verbatim cells which are empty); LT / RT Rule 8 section-jump (canonical, NOT in verbatim cells); R-stick / sticks-click / Select universally idle; Start universally empty for left-panel rows).

| ID | Type | A (verbatim) | D-pad U | D-pad D | D-pad R (cross-panel) |
|----|------|--------------|---------|---------|------------------------|
| row.arena | dropdown | open maps dropdown | (empty) | move to scenario dropdown control | move to right side, this one would go to the team sorting dropdown if teams are enabled |
| row.gametype | dropdown | open maps dropdown | move to map select option | move to teams option | move to right side, this one would go to the team sorting dropdown if teams are enabled |
| opt.teams | option | toggle this option | move to scenario dropdown control | move to time limit option | go to the control in the right panel aligned with this one (a bot, the team mode dropdown, or the add bot button) |
| row.time | slider | open modal with preset options (10, 20, 50, 100, no limit) | move to teams option | move to score option | go to the control in the right panel aligned with this one (a bot, the team mode dropdown, or the add bot button) |
| row.score | slider | open modal with preset options (10, 20, 50, 100, no limit) | move to time limit option | move to weapon loadout dropdown | go to the control in the right panel aligned with this one (a bot, or the add bot button) |
| row.weapons | dropdown | open weapon loadout dropdown | move to score option | move to auto-aim option | go to the control in the right panel aligned with this one (a bot, or the add bot button) |
| opt.autoaim | option | toggle this option | move to weapon loadout dropdown | move to jump option | go to the control in the right panel aligned with this one (a bot, or the add bot button) |
| opt.jump | option | toggle this option | move to auto-aim option | move to radar option | go to the control in the right panel aligned with this one (a bot, or the add bot button) |
| opt.radar | option | toggle this option | move to jump option | move to one-hit kills option | go to the control in the right panel aligned with this one (a bot, or the add bot button) |
| opt.onehit | option | toggle this option | move to radar option | move to friendly fire option | go to the control in the right panel aligned with this one (a bot, or the add bot button) |
| opt.friendlyfire | option | toggle this option | move to one-hit kills option | move to fast movement option | go to the control in the right panel aligned with this one (the add bot button) |
| opt.fastmove | option | toggle this option | move to friendly fire option | move to next button down in the panel (or if this is the last one, move to Start Match button) | go to the control in the right panel aligned with this one (the add bot button) |

For all 12 rows above:

- **B**: bring up 'exit combat simulator' modal (Rule 4)
- **X**: empty in verbatim. Rule 5 designates X for context menus where applicable; left-panel rows have no designed context menu in v2. **Future fill welcome, not required for v2 conformance.**
- **Y**: open social menu to allow for invites and whatnot (should have a contextual glyph prompt in the lower right of screen) (Rule 6 + CC4)
- **L-stick U/D/L/R**: mirror D-pad (Rule 1, verbatim text identical to D-pad cells)
- **R-stick U/D/L/R**: empty in verbatim (Rule 2: no enclosing scroll surface for left-panel rows; correct as designed)
- **L-stick click / R-stick click**: empty (Rule 9 reserved)
- **LB / RB**: empty in verbatim. **CANONICAL per Mike Q1: Rule 7 previous / next tab cycle.**
- **LT / RT**: empty in verbatim. **CANONICAL per Mike Q2: Rule 8 previous / next section header (arena -> gametype -> limits -> weapons -> options -> players).**
- **Start**: empty in verbatim. Rule 9: left-panel rows do not have the Start jump-to-primary affordance; user is expected to D-pad through configuration before reaching for Start.
- **Select / Back**: empty (Rule 9 reserved)

### Right-panel sort dropdown (1)

`row.teamsort` is the only right-panel element above the team.1 divider:

| Input | Verbatim cell |
|-------|---------------|
| D-pad Up | (empty) |
| D-pad Down | move to first character in players list |
| D-pad Left | go to the control in the left panel aligned with this one |
| D-pad Right | (empty) |
| L-stick (mirror) | as D-pad |
| R-stick (all) | (empty) |
| L-stick / R-stick click | (empty) |
| A | Open drop down |
| B | bring up 'exit combat simulator' modal (Rule 4) |
| X | empty in verbatim. No designed context menu in v2. |
| Y | open social menu... (Rule 6) |
| LB | empty in verbatim. **CANONICAL: Rule 7 previous tab cycle.** |
| RB | empty in verbatim. **CANONICAL: Rule 7 next tab cycle.** |
| LT | empty in verbatim. **CANONICAL per Q2: above team.1 divider so LT no-op (no grouping above row.teamsort in right panel).** |
| RT | empty in verbatim. **CANONICAL per Q2: jump to team.1 first player (same as D-pad Down semantically; can keep no-op if D-pad already covers this).** |
| Start | move to Start Match button (Rule 9 right-panel pattern) |
| Select / Back | (empty) |

### Player rows (8) -- WHERE Q1+Q2 INVERSION APPLIES

Player rows are the only element type where Mike's Q1+Q2 inversion meaningfully changes the cells. Each player row has an active LB / RB and LT / RT in verbatim that gets reassigned per canonical.

#### Verbatim cells (preserved for reference, NOT canonical)

| Player row | Verbatim LB | Verbatim RB | Verbatim LT | Verbatim RT |
|------------|-------------|-------------|-------------|-------------|
| p.you | previous team first player (or roll back to last team) | next team first player (or roll over to first team) | top of players list | bottom of players list |
| p.joanna | this team first player (or roll back to last team) | next team first player (or roll over to first team) | top of players list | bottom of players list |
| p.bot1 | this team first player (or roll back to last team) | next team first player (or roll over to first team) | top of players list | bottom of players list |
| p.bot2 | this team first player (or roll back to last team) | next team first player (or roll over to first team) | top of players list | bottom of players list |
| p.bot3 | this team first player (or roll back to last team) | next team first player (or roll over to first team) | top of players list | bottom of players list |
| p.bot4 (team.2) | this team first player (or roll back to last team) | next team first player (or roll over to first team) | (empty) | (empty) |
| p.bot5 (team.2) | this team first player (or roll back to last team) | next team first player (or roll over to first team) | (empty) | (empty) |
| p.bot6 (team.2) | this team first player (or roll back to last team) | next team first player (or roll over to first team) | (empty) | (empty) |

Note that team.2 player rows in the verbatim already have empty LT / RT, an asymmetry that the canonical decision resolves (both teams get team-jump on LT / RT; both teams get tab-cycle on LB / RB).

#### Canonical cells (post-Q1+Q2)

For all 8 player rows:

| Input | Canonical |
|-------|-----------|
| LB | **Rule 7: previous tab cycle** (universal across screen; was team-jump in verbatim) |
| RB | **Rule 7: next tab cycle** (universal across screen; was team-jump in verbatim) |
| LT | **Rule 8: previous team's first player** (the team-jump affordance Mike moved here from LB; was "top of players list" in verbatim) |
| RT | **Rule 8: next team's first player** (the team-jump affordance Mike moved here from RB; was "bottom of players list" in verbatim) |

#### Cells that remain unchanged from verbatim

For all 8 player rows:

| Input | Verbatim cell (canonical, unchanged) |
|-------|---------------------------------------|
| D-pad Up | move up to player above (or "move up to team setting dropdown" for first player in a team) |
| D-pad Down | move down to next character (or "move down to Add Bot button" for last player) |
| D-pad Left | go to the control in the left panel aligned with this one (Teams or Scenario selection for p.you; specific aligned target for others) |
| D-pad Right | (empty) |
| L-stick U/D/L/R | mirror D-pad |
| R-stick Up | scroll player list (Rule 2) |
| R-stick Down | scroll player list (Rule 2) |
| R-stick Left / Right | (empty) |
| L-stick / R-stick click | (empty) (Rule 9 reserved) |
| A | select / deselect character (Rule 3) |
| B | bring up 'exit combat simulator' modal (Rule 4) |
| X | context menu, change team or set temp character, bot settings, remove bot (Rule 5 per-row overflow) |
| Y | open social menu... (Rule 6 + CC4) |
| Start | move to Start Match button (Rule 9 right-panel pattern) |
| Select / Back | (empty) (Rule 9 reserved) |

### Add Bot Button (1)

`row.addbot` sits below the last team's last player.

| Input | Verbatim cell | Canonical (where different) |
|-------|---------------|-----------------------------|
| D-pad Up | move up to last member of player list | (unchanged) |
| D-pad Down | move down to Start Match / Back options | (unchanged) |
| D-pad Left | go to the control in the left panel aligned with this one | (unchanged) |
| D-pad Right | (empty) | (unchanged) |
| L-stick (mirror) | as D-pad | (unchanged) |
| R-stick (all) | (empty) | (unchanged) (no enclosing scroll for the button itself) |
| L-stick / R-stick click | (empty) | (unchanged, Rule 9 reserved) |
| A | add bot if able (Rule 3) | (unchanged) |
| B | bring up 'exit combat simulator' modal (Rule 4) | (unchanged) |
| X | context menu (fill, remove all) (Rule 5 per-row overflow) | (unchanged) |
| Y | open social menu... (Rule 6 + CC4) | (unchanged) |
| LB | (empty) | **CANONICAL: Rule 7 previous tab cycle.** |
| RB | (empty) | **CANONICAL: Rule 7 next tab cycle.** |
| LT | (empty) | **CANONICAL per Q2: previous group = jump to last team's first player.** |
| RT | (empty) | **CANONICAL per Q2: next group = no-op (Add Bot is below the last team).** |
| Start | move to Start Match button (Rule 9 right-panel pattern) | (unchanged) |
| Select / Back | (empty) | (unchanged, Rule 9 reserved) |

### Footer (2)

#### Start Match Button (`footer.start`)

| Input | Verbatim cell |
|-------|---------------|
| D-pad Up | move up to bottom control in the left panel |
| D-pad Down | (empty) |
| D-pad Left | (empty) |
| D-pad Right | move to Back to Menu button |
| L-stick (mirror) | as D-pad |
| R-stick (all) | (empty) |
| L-stick / R-stick click | (empty) |
| A | start match (Rule 3) |
| B | bring up 'exit combat simulator' modal (Rule 4) |
| **X** | **Queue Match (stores an array of match start settings and cycles them as matches end; host can either 'end game' or 'skip match' in their pause menu); display number of queued match settings.** (Rule 5 screen-local feature shortcut; deferred P3 feature -- see grammar doc Deferred section + kanban c081) |
| Y | open social menu... (Rule 6 + CC4) |
| LB | (empty) -> **CANONICAL: Rule 7 previous tab cycle.** |
| RB | (empty) -> **CANONICAL: Rule 7 next tab cycle.** |
| LT | (empty) -> **CANONICAL: Rule 8 no-op (footer below all groupings).** |
| RT | (empty) -> **CANONICAL: Rule 8 no-op (footer below all groupings).** |
| Start | (empty) -> Rule 9: Start Match IS the primary commit; Start jumping to itself would be no-op. Mike to confirm whether Start should bind here (a "press to commit" shortcut) or stay empty. |
| Select / Back | (empty) (Rule 9 reserved) |

#### Back to Menu Link (`footer.back`)

| Input | Verbatim cell |
|-------|---------------|
| D-pad U/D/L/R | (all empty -- final destination) |
| L-stick (all) | (all empty) |
| R-stick (all) | (all empty) |
| L-stick / R-stick click | (empty) |
| **A** | **bring up 'exit combat simulator' modal (Rule 4 A+B convergence -- the link's primary action IS destructive, so A and B converge on the same modal)** |
| **B** | **bring up 'exit combat simulator' modal (Rule 4 A+B convergence)** |
| X | (empty) -- no per-row context menu for the back link |
| Y | open social menu... (Rule 6 + CC4) |
| LB | (empty) -> **CANONICAL: Rule 7 previous tab cycle.** |
| RB | (empty) -> **CANONICAL: Rule 7 next tab cycle.** |
| LT | (empty) -> **CANONICAL: Rule 8 no-op.** |
| RT | (empty) -> **CANONICAL: Rule 8 no-op.** |
| Start | (empty) (Rule 9; Back to Menu is not the primary commit) |
| Select / Back | (empty) (Rule 9 reserved) |

This is the canonical example of A+B convergence on destructive links (Rule 4 special case).

## Source-of-truth provenance

- **Verbatim JSON**: [combat-simulator-bindings-v2.json](combat-simulator-bindings-v2.json) (1085 lines, generatedAt 2026-05-03T07:33:49.545Z; ingested 2026-05-03 via worktree drop after Cowork upload paths proved unreachable from host).
- **Universal grammar abstract patterns**: [menu-input-interaction-grammar.md](menu-input-interaction-grammar.md).
- **Mike's Q1-Q5 directive**: 2026-05-03 host session message; Q1 inverts player-row LB/RB to universal tab cycle; Q2 inverts player-row LT/RT to universal section/team/page jump; Q3 confirms `ACTION_MENU_CONTEXT` / `ACTION_MENU_SOCIAL` action-map naming.
- **Screenshot**: pending; will save to `combat-simulator-rendered-2026-05-03.png` in this directory once reachable.

## Conformance notes (implementation gaps)

The Combat Simulator menu source lives in [pdgui_menu_mpsetup.cpp](../../../port/fast3d/pdgui_menu_mpsetup.cpp) and related files. After this spec lands, an implementation pass should:

1. Verify every cell above against current implementation; file gaps as bugs in `context/bugs.md`.
2. Add Rule 7 LB / RB tab cycle to all left-panel rows + right-panel rows + footer + Back to Menu link (currently inferred from outer-tab-handler, may not extend to player rows).
3. Add Rule 8 LT / RT section / team / page jump (NEW action-map entries: `ACTION_MENU_SECTION_PREV` / `ACTION_MENU_SECTION_NEXT`).
4. Add Rule 6 CC4 social glyph chrome (`pdguiSocialGlyph()` helper).
5. Add Rule 5 X-context-menu builder + register per-row inventories for player rows + Add Bot.
6. Audit B-double-press regression coverage for the exit-modal flow (regression test recommended).

Implementation pass is OUT OF SCOPE for this doc. Mike noted: "I'll spawn the codebase-sweep follow-up as a fresh session to migrate any places in code that currently inline LB/RB or LT/RT semantics -- but that work can wait until the spec is canonical."

## Cross-references

- Universal grammar abstract: [menu-input-interaction-grammar.md](menu-input-interaction-grammar.md)
- v2 verbatim JSON: [combat-simulator-bindings-v2.json](combat-simulator-bindings-v2.json)
- Phase 1 audit: [input-menu-system-audit-2026-05-01.md](../../audits/input-menu-system-audit-2026-05-01.md)
- Settings tab handler authority: [pdgui_menu_mainmenu.cpp:4545-4552](../../../port/fast3d/pdgui_menu_mainmenu.cpp:4545)
- ACTION_MENU_TAB_PREV/NEXT IMC declarations: [pdgui_menu_mainmenu.cpp:1745-1746](../../../port/fast3d/pdgui_menu_mainmenu.cpp:1745)
- Innermost-scrollable walker: `pdguiInnermostScrollableForNav` in [pdgui_backend.cpp](../../../port/fast3d/pdgui_backend.cpp)
- Queue Match deferred feature: kanban card c081 (Input pillar, backlog)
