# Theme Bundling + Per-Agent Settings (S305 design)

**Status**: superseded in part by D-005 option A (2026-08-12).
**Owner**: Mike + Claude.
**Date**: 2026-04-16.

## Motivation

Two user-requested capabilities that sit at the same architectural layer
(user-chosen visual identity + Agent profile state):

1. **Theme Bundles** — one mod activation applies Menu Style (nineslice
   chrome) + Color Theme + Font together, so modders can ship a complete
   look as a single drop-in (e.g. a "Pokemon" theme or "Final Fantasy"
   theme).

2. **Per-Agent Settings** — theme/menu-style/font/enabled-mods load
   automatically when an Agent profile is signed in, and swap on agent
   switch. All mods stay on disk; only the enablement + selections are
   per-agent.

Both share the question "which subsystem owns the user-facing
visual/preferences state, and how does it follow the active identity?".

---

## Theme Bundling (P4 — partial landing S305)

### Current shape (landed this session)

`struct theme_def` gained two id fields that can be set from `theme.json`:

```json
{
  "name": "Pokemon",
  "palette": { ... },
  "menuStyle": "user.pokemon-chrome.ui-chrome",
  "font": "user.PokemonFont.font"
}
```

- `menuStyle` (or legacy alias `chromeStyle`) → `pdguiThemeSetUiChromeStyleId`
  + `pdguiChromeSetEnabled(1)` + `pdguiSetPanelNineSlice(...)`.
- `font` → `pdguiFontModSetActiveId(...)`. ImGui's atlas is only built
  once per session, so font bundling takes effect on next app start —
  the rest of the bundle applies live.

Missing pieces (queued for a follow-up):

- **Theme Editor UI** doesn't yet let users pick a menuStyle/font when
  saving. Today a bundle has to be hand-edited in `theme.json` after
  Save-as-Mod.
- **Discovery resilience** — if the referenced chrome or font mod isn't
  installed, we log-and-continue (palette still applies). A UI warning
  would be friendlier.
- **Catalog pivoting** — bundle references are by catalog id string, so
  renaming a chrome/font mod breaks the link. Consider storing a
  display-name fallback.

### Minimum-viable finish

1. Theme Editor: two new dropdowns below "Name / Author" — "Menu Style
   (optional)" and "Font (optional)" populated from
   `pdguiThemeGetChromeStyleId/Name` and `pdguiFontModGetId/GetName`.
   Selection writes into the saved `theme.json`.
2. Load-time warning UI: if a bundle references a missing id, surface
   a status line in Settings → Debug → UI Theme.

---

## Per-Agent Settings (design only — no code this session)

### 2026-08-12 ownership decision

D-005 option A supersedes the sidecar storage proposal below. The one
Agent-specific current JSON document owns campaign state and all per-agent
preferences. `pd.ini` remains the machine-default source before sign-in.
Existing exact legacy v2 Agent JSON plus an optional `prefs_<agent>.ini` file is
a migration input only: both are parsed and validated into one candidate, the
current JSON is replaced atomically, and the sidecar is retired after commit.
Current profiles never read or write a sidecar. The older proposal remains
below as migration history, not as the target architecture.

### Problem

Today, visual preferences (theme id, chrome id, title bar style, font,
mod enablement) live in `pd.ini` at the global scope. When a user
switches agents, their preferences follow the machine rather than the
character. Mike wants:

> "If I have an Agent profile where I selected my custom Pokemon theme,
> it should load automatically when I sign that agent in. If I switch
> Agents to a different one with a Final Fantasy theme, the Final
> Fantasy one should then load instead."

### Constraints

- **Save file format compatibility** (CLAUDE.md): we can't break the
  existing agent save format. Solution: store per-agent visuals in a
  *sidecar* file keyed by agent slot, not in the legacy `filelist`.
- **Mod availability is global** (per Mike's spec): mods stay on disk;
  only the enable/disable selection and active-id choices are per-agent.
- **ImGui font atlas is built once per session** (backend constraint):
  switching agents can't rebuild the atlas without reinitialising the
  backend. For V1, font changes on agent switch show a "restart to
  apply" toast; the other three (theme palette, menuStyle, title bar)
  can swap live.

### Proposed architecture

1. **Storage**: `saves/agents/<agent_slot>/prefs.ini` per agent. Fields:
   ```
   [Theme]
   ActiveTheme = user.pokemon.theme

   [Video]
   UiChromeStyleId = user.pokemon-chrome.ui-chrome
   UiChromeEnabled = 1
   UiTitleBarStyle = 2
   FontId = user.PokemonFont.font

   [Mods]
   Enabled = slug1,slug2,slug3
   ```

2. **Agent switch hook**: extend `saveSetAgent(slot)` (or the login
   flow's equivalent) with a call to `prefsLoadForAgent(slot)`:
   - Read `saves/agents/<slot>/prefs.ini`.
   - Apply each key via the existing subsystem setters
     (`pdguiThemeLoadFromCatalog`, `pdguiThemeSetUiChromeStyleId`,
     `pdguiThemeSetTitleBarStyle`, `pdguiFontModSetActiveId`,
     `modmgrSetEnabled`).
   - Font change surfaces a status toast — doesn't take effect until
     restart (same UX as the new Font dropdown).

3. **Settings-screen writes**: when a user toggles a preference in
   Settings → Video → UI Theme, the change writes to BOTH the global
   `pd.ini` (so the machine default is remembered) AND the active
   agent's `prefs.ini`. If the user hasn't signed an agent in yet, only
   the global file is written.

4. **Migration**: first time an agent is loaded after this lands, copy
   the current `pd.ini` values into `prefs.ini` so the user's existing
   look isn't lost on first agent load.

### What stays global

- Resolution, fullscreen, audio volumes, gameplay bindings — these are
  per-machine, not per-agent. The existing `pd.ini` keeps owning them.

### Work estimate

- ~300-400 lines in a new `port/src/prefs_per_agent.c` + header
- ~50 lines of hook code in `save*`, `netUnset*`, and the Settings
  writers.
- Careful test matrix (switch mid-match? switch while settings menu
  open? guest agent w/ no save dir?).

Flag this as a dedicated future session — it's not a drive-by change.

---

## Dependencies + invariants

- **S305 Video.FontId persistence fix** (already shipped) is a
  prerequisite — without the pending-value replay, per-agent Video.*
  keys wouldn't survive the configInit-before-pdguiThemeInit ordering.
- **No integer asset identity on the wire / save** — per-agent prefs
  must use catalog id strings, same rule as matchslot `body_id`.
- **Mod enablement policy** (`context/designs/mod-enablement-policy.md`)
  needs an extension: today mod enablement is global, per-agent changes
  that.

## Open questions

- Do we support *guest* agents (no save slot) with custom prefs?
  Proposal: guest uses the global `pd.ini` only.
- How do Agent→Agent switches interact with the theme "first-sight"
  auto-apply? Proposal: first-sight only fires for the global default,
  not per-agent.
- Do we want a "Reset Visuals" button in the per-agent UI? Proposal:
  yes, to decouple from global defaults.
