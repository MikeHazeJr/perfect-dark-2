# ADR — Input Authority + Menu Pool (Architecture Fix)

> Status: **ACTIVE (Phase 1 implemented 2026-04-13, Phase 2 queued)**
> Date: 2026-04-13
> Branch: `input-authority-and-menu-pool`
> Trigger: Mike's 2026-04-13 playtest — Ctrl+V pasted into a server-name field in the
> Online menu and his background player jumped (Ctrl bound to ACTION_JUMP in the
> Gameplay IMC). Separate observation during the same session: existing menu-duplicate
> rejection (F-3.1) is a runtime guard, not an architectural barrier — a pool/stack
> that makes duplicates structurally impossible is wanted.

---

## 1. Problem statement

### 1.1 Input bleed-through (the hit-and-run)

> "There has to be a fool-proof way of ensuring we have MKB / Controller authority
>  in menus versus gameplay. It shouldn't even be possible."
>  — Mike, 2026-04-13

With the main menu (or any non-gameplay context) on top, keys bound **only** in
`g_ImcGameplay` still reach the gameplay state array `s_State[player][action]`.
Concretely:

1. `pdguiProcessEvent()` (`port/fast3d/pdgui_backend.cpp:683`) receives `SDL_KEYDOWN`
   for LCTRL while the main menu is open.
2. `inputCtxShouldSuppressKey()` returns 0 — the push-grace period is 100 ms and
   has long expired.
3. `actionmapDispatch()` runs unconditionally → `fireVk(LCTRL, 1)` walks IMCs
   top-down (`port/src/actionmap.cpp:417`). Priority order during menu is
   `g_ImcMenu (10)` → `g_ImcGameplay (0)`. `g_ImcMenu` has no LCTRL binding;
   `g_ImcGameplay` binds LCTRL → `ACTION_JUMP`. `s_State[0][ACTION_JUMP].pressed = 1`.
4. ImGui consumes the Ctrl+V for its paste. The input-context dispatch marks the
   event consumed so *game SDL path* doesn't see it, but the action state array
   was already poisoned at step 3.
5. Next game tick: `bondmove.c:974` reads `actionHeld(0, ACTION_USE)` and sibling
   gameplay actions; `actionHeld(0, ACTION_JUMP)` returns 1; player jumps.

**Prior fixes that almost-but-not-quite cover this:**

- `inputCtxShouldSuppressKey()` (100 ms push grace) — only fires during the race
  window around menu open/close. Long after open, it returns 0.
- `actionmapPollFrame()` zeros analog axes when top ctx != gameplay (B-124d). This
  covers gamepad sticks / mouse polling — NOT digital key dispatch.
- `actionmapDispatch()` itself is pure per-event, no context check.
- `actionPressed()`/`actionHeld()`/`actionReleased()`/`actionValue()` are naked
  reads of `s_State` — no authority check.
- F-3.1 duplicate-push-rejection in `menuPushDialog()` is for legacy menu layers,
  not the IMC / action-state layer.

The bleed-through is the read-site's problem (nobody asks "am I allowed to be
reading this right now?"). Retroactive filters at each consumer would multiply
the failure modes. The fix must be in the dispatch path **and** the read path —
defense in depth, both authoritative.

### 1.2 Focus-loss / alt-tab

Not handled at all. `gfx_sdl_handle_events()` (`port/fast3d/gfx_sdl2.cpp:307`)
switches only on `SDL_WINDOWEVENT_SIZE_CHANGED` and `SDL_WINDOWEVENT_CLOSE`.
`SDL_WINDOWEVENT_FOCUS_LOST` and `SDL_WINDOWEVENT_FOCUS_GAINED` pass through without
any action-state cleanup. Consequences:

- Alt-tab with `W` held → release `W` in the other window → return to game → `W` is
  still held in `s_State` (never received its KEYUP). Player walks forward on
  arrival.
- Alt-tab with LMB held → trigger a Windows UAC prompt / IME → same: fire
  still pressed when you come back.
- No "settle frame" on return — the first frame after focus regain is active
  immediately, regardless of which keys the user intends to be holding.

### 1.3 Menu-pool instance discipline (Phase 2 scope)

> "There should be a pool or stack which only allows one type of any given menu
>  to exist. Don't create, populate / instantiate."
>  — Mike, 2026-04-13

Today, `menuPushDialog()` (`src/game/menu.c:1507`) allocates a new layer/dialog
entry every open and rejects a duplicate `definition` pointer at runtime (F-3.1).
This is a **reactive** guard. Mike wants an **architectural** guard: one
pre-allocated instance per menu type, keyed by enum; open = lookup + populate +
activate; close = deactivate + clear. New instance for an already-open type is
structurally impossible.

This also applies to the ImGui-side menus (`port/fast3d/pdgui_menu_*.cpp`), which
mostly already use file-static state (effectively a one-instance-per-type
pool by convention, but not by contract). Unifying them under a named typed
registry means the menu system can no longer re-enter itself.

Out of scope for this session — see §6.

---

## 2. Current state (what we have now)

### 2.1 Input pipeline, end-to-end

```
SDL_PollEvent  (gfx_sdl_handle_events in gfx_sdl2.cpp)
  └─ pdguiProcessEvent(ev)  (pdgui_backend.cpp:683)
       ├─ Global hotkey check (F8 / F12 / RS-click)  — consumed early
       ├─ inputCtxShouldSuppressKey(ev)              — consumed if in grace period
       ├─ actionmapDispatch(ev)                      — writes s_State unconditionally
       ├─ ImGui_ImplSDL2_ProcessEvent(ev)            — ImGui state
       └─ inputCtxDispatch(ev)                       — game vs. menu routing

          [per-frame]
          inputCtxPollFrame()            — top-ctx poll
          actionmapPollFrame()           — stick/mouse sampling (menu-gated)
          <game tick — reads s_State via actionHeld/Pressed>
          actionmapEndFrame()            — clears pressed/released edges
          inputCtxEndFrame()             — processes deferred pops
```

### 2.2 IMC priority order

| IMC | Priority | Activated when |
|-----|----------|----------------|
| `g_ImcTextInput` | 30 | Text field focused |
| `g_ImcDebugOverlay` | 20 | F12 debug open (`g_CtxDebugOverlay` pushed) |
| `g_ImcPauseMenu` | 11 | `g_CtxPauseMenu` pushed |
| `g_ImcMenu` | 10 | `g_CtxImGuiMenu` or `g_CtxPauseMenu` or `g_CtxDebugOverlay` pushed |
| `g_ImcVehicle` | 5 | (future — vehicle entry) |
| `g_ImcGameplay` | 0 | Always active (bottom of stack) |

`fireVk()` (`port/src/actionmap.cpp:417`) walks `s_Active[]` top-down (priority
descending). First IMC that has `has_mapping[a]` and one of its triggers
matches `vk` wins and writes `s_State[player][a]`.

### 2.3 Gaps (what's missing)

| Gap | Location | Consequence |
|-----|----------|-------------|
| No authority check in `fireVk` | `actionmap.cpp:417` | Gameplay IMC fires even when menu is on top for VKs the menu IMC doesn't bind (LCTRL, LSHIFT, LALT, function keys, number row, etc.) |
| No authority check in `actionPressed/Held/Released/Value` | `actionmap.cpp:978-1010` | Game-side readers never ask "is gameplay authoritative right now?" |
| No `SDL_WINDOWEVENT_FOCUS_LOST/GAINED` handler | `gfx_sdl2.cpp:329` | Held keys survive alt-tab; re-focus sees stale held state |
| No flush on context push | `inputctx.c:58-115` | Holding W and hitting ESC leaves `ACTION_MOVE_FORWARD` held forever (until physical release — but the release event may be captured by menu IMC, which doesn't clear gameplay's s_State) |
| Duplicate-push rejection is runtime-reactive | `menu.c:1507, inputctx.c:58` | F-3.1 / `inputCtxPush` detect duplicates *after* allocation / at push time. Not an architectural pool. |

---

## 3. Proposed architecture

### 3.1 Layer: `g_GameplayInputSuppressed` authority predicate

Single global predicate exposed from `inputctx.h`. Returns non-zero iff
**gameplay is NOT allowed to receive input right now**. Authoritative condition:

```
gameplayInputSuppressed() =
      (inputCtxGetTop() != &g_CtxGameplay)
   || s_WindowFocusLost
   || s_FocusRegainSettlePending
```

- **(a) top context != gameplay** — covers all menu/overlay/pause scenarios.
- **(b) window focus lost** — set on `SDL_WINDOWEVENT_FOCUS_LOST`, cleared on
  `FOCUS_GAINED`.
- **(c) focus-regain settle** — one-frame flag cleared at the first frame end
  after `FOCUS_GAINED`. Semantically: "the user has not yet had a chance to
  re-confirm their intent; do not interpret anything." Combined with the flush
  below (§3.3) this forces a re-press.

### 3.2 Read-site gate (belt)

In `actionPressed/Held/Released/Value/Axis` (and in `actionmapDispatch`/`fireVk`),
consult `gameplayInputSuppressed()`. Scope: **actions that are gameplay-only**.
Actions that the menu layer legitimately consumes (ACTION_MENU_*, ACTION_USE,
ACTION_CANCEL_USE, ACTION_PAUSE, system hotkeys) are NOT gated at read — the
dispatch-site gate (§3.3) is their sole protection, which leaves menu-IMC
bindings intact.

Classification function (`actionmap.cpp`):

```c
static inline int actionIsGameplayOnly(InputAction a) {
    // Menu nav is not gameplay
    if (a >= ACTION_MENU_UP && a <= ACTION_MENU_TAB_NEXT) return 0;
    // System / shared actions (menu + gameplay both consume)
    switch (a) {
    case ACTION_USE:            // == ACTION_MENU_ACCEPT
    case ACTION_CANCEL_USE:     // == ACTION_MENU_CANCEL
    case ACTION_PAUSE:
    case ACTION_SCREENSHOT:
    case ACTION_CONSOLE_TOGGLE:
    case ACTION_DEBUG_TOGGLE:
    case ACTION_CHEAT_ENTER:
        return 0;
    default:
        return 1;
    }
}
```

### 3.3 Dispatch-site gate (braces)

In `fireVk()`, skip `g_ImcGameplay` and `g_ImcVehicle` when `gameplayInputSuppressed()`
is true. Menu-scope IMCs (Menu / Pause / Debug / TextInput) still resolve normally.
This is the authoritative barrier: gameplay-only bindings can never poison
`s_State` while a non-gameplay context owns input.

Combined with (§3.2), a gameplay-only VK during menu is rejected both on the way
IN (dispatch never writes state) and on the way OUT (read returns 0 even if some
path did write). Defense in depth.

### 3.4 Flush on transitions

Two flush points, both drain all gameplay-only `s_State` entries (held=0,
pressed=0, released=0, value=0):

1. **`g_CtxImGuiMenu` / `g_CtxPauseMenu` / `g_CtxDebugOverlay` on_push()** —
   any held gameplay key at menu-open is re-sent as released (s_State cleared).
   Guarantees the player stops moving/firing the moment the menu appears.

2. **`SDL_WINDOWEVENT_FOCUS_LOST`** — flush + set focus-lost flag.

A third, less-aggressive step on `FOCUS_GAINED`: set `s_FocusRegainSettlePending`
for one frame, cleared by `actionmapEndFrame()` so that the first full frame
after return is inert. The user must re-press to count.

### 3.5 Shared-action corner case

ACTION_USE, ACTION_CANCEL_USE, ACTION_PAUSE are shared between gameplay and menu
IMCs. During menu-open:

- `g_ImcMenu` binds A → ACTION_USE (menu accept). High priority. Writes s_State.
- Menu consumers read `actionPressed(0, ACTION_USE)` → 1. ✓ menu accept works.
- Background gameplay readers (e.g. `bondmove.c`) read `actionHeld(0, ACTION_USE)`
  → also 1, **because they share the same state array**.

For bondmove's door-open code, this is a minor leak: Mike pressing the menu
accept key while in a menu would fire "use" in the background. Accepted for
Phase 1; the fix for this is Phase 2's menu-pool refactor which will also
separate the state arrays per scope (or gate door-open on in-game presence).
Phase 1 solves the **loud** case (Ctrl+V → jump) completely; this is the
**quiet** case where a shared binding is intentionally pressed.

Mitigation available right now: when `gameplayInputSuppressed()` is true,
`g_GameplayInputSuppressed`'s effect on `fireVk` means the gameplay IMC's
ACTION_USE binding doesn't fire, but the menu IMC's does. bondmove reads the
state and sees menu-IMC's write. To fully suppress: bondmove-side wrapper
`gameplayActionHeld()` that checks `!gameplayInputSuppressed()` first. Deferred
to Phase 2 alongside the menu-pool per-scope state arrays.

---

## 4. Files touched (Phase 1)

| File | Change |
|------|--------|
| `port/include/inputctx.h` | Add `gameplayInputSuppressed()`, `inputCtxNotifyFocus(int)`. Add `inputctx_flush_gameplay_input()` hook. |
| `port/src/inputctx.c` | Implement predicate + focus-state tracking + flush callback plumbing. |
| `port/include/actionmap.h` | Add `actionmapFlushGameplayState()`, `actionmapOnFocusLost/Gained()`. |
| `port/src/actionmap.cpp` | Implement flushes. Gate `fireVk()` against gameplay/vehicle IMCs when suppressed. Gate `actionPressed/Held/Released/Value` for gameplay-only actions when suppressed. Gate `actionmapPollFrame()` via new predicate (replaces ad-hoc `topCtx != &g_CtxGameplay` check). Clear `ACTION_SCORECARD` too on flush. |
| `port/fast3d/gfx_sdl2.cpp` | Handle `SDL_WINDOWEVENT_FOCUS_LOST` / `FOCUS_GAINED` → call `inputCtxNotifyFocus()`. |
| `port/fast3d/pdgui_backend.cpp` | On non-gameplay context push (via inputctx callbacks), flush gameplay state. |

**Files parallel session is touching (DO NOT MODIFY):**
`pdgui_menu_lobby.cpp`, `pdgui_menu_room.cpp`, `pdgui_menu_moddinghub.cpp`,
`pdgui_theme_loader.cpp`, `pdgui_menu_combatsim.cpp`.

## 5. Migration + rollout (Phase 1)

1. Implement predicate + flushes behind internal API in `inputctx.*` and
   `actionmap.*`. No behaviour change until enabled.
2. Wire `fireVk` gate + read-site gate (defense in depth).
3. Wire focus-loss/gained handlers in `gfx_sdl2.cpp`.
4. Wire on_push flush for `g_CtxImGuiMenu` / `g_CtxPauseMenu` /
   `g_CtxDebugOverlay`.
5. Build verify (pd + pd-server). Runtime smoke test: repro-case scenarios
   pass (main menu open, press any gameplay bind → no player state change).
6. Commit, merge to dev with `--no-ff`, post-merge verify line counts.

### Acceptance criteria

| Scenario | Expected |
|----------|----------|
| Main menu open, press LCTRL (bound to ACTION_JUMP in gameplay) | Player does not jump |
| Main menu open, press SPACE (bound to ACTION_JUMP) | Player does not jump |
| Main menu open, hold W (bound to ACTION_MOVE_FORWARD) | Player does not advance |
| Main menu open, paste via Ctrl+V | Text pastes, no gameplay action fires |
| Pause menu open, press any gameplay key | Nothing |
| Debug overlay (F12), press any gameplay key | Nothing |
| Hold W, open menu, close menu without releasing W | Player stops moving the moment menu opens; does not resume until re-press |
| Hold W, alt-tab away, release W externally, alt-tab back | Player does not move on return; re-press required |
| Menu open, A pressed | Menu-accept fires; background gameplay's ACTION_USE held state may be 1 (known shared-action leak, Phase 2 scope) |
| Axis/stick behaviour during menu | Already zeroed (unchanged from B-124d fix); now routed via new predicate |

---

## 6. Phase 2 scope (queued, **not** in this session)

### 6.1 Menu-pool single-instance discipline

**Goal**: one instance per menu type, structurally impossible to duplicate.

Sketch:

- Define `menu_type_t` enum covering every menu: `MENU_PAUSE`, `MENU_MAIN`,
  `MENU_OPTIONS`, `MENU_COMBAT_SIM`, `MENU_MOD_HUB`, `MENU_THEME_EDITOR`,
  `MENU_SKIN_EDITOR`, `MENU_SOLO_MISSION`, `MENU_UPDATER`, etc.
- Static pool `s_MenuPool[MENU_COUNT]`, each entry holds `{active, populate_fn,
  state_blob}`.
- `menuOpen(MENU_PAUSE)`: if already active → no-op (or focus-refresh). Else
  activate + call populate.
- `menuClose(MENU_PAUSE)`: deactivate + depopulate.
- `menuPushDialog()` becomes a compatibility shim (legacy dialog layers) that
  eventually goes away.
- ImGui-side: `pdgui_menu_*.cpp` renderers look up their pool entry by type
  instead of using file-static `s_*` state directly. Duplicate-open → same
  instance.
- Shared-action leak (§3.5) fixes itself because menu-scope actions flow through
  the pooled menu's own state, not the global `s_State[ACTION_USE]`.

**Files that will be touched (estimate):**
`src/game/menu.c`, `port/fast3d/pdgui_menu_*.cpp` (~20 files), `port/include/pdgui_menus.h`.

Large. Requires its own session + branch.

### 6.2 Shared-action scope (follow-up to §3.5)

Decide: per-IMC state arrays, or `gameplayActionHeld()` wrappers for game-side
consumers, or explicit read-scope tag. Pick during Phase 2.

### 6.3 Flush tests

Write a minimal SDL-driven unit / integration test that drives pressdown →
context push → release and verifies s_State post-flush. Currently no automation
exists for the input layer; any regression would reappear the hard way.

---

## 7. Non-goals (explicit)

- Do **not** change the N64 `g_BondMoveInputs` / `g_Vars.currentplayer->bondmove`
  path. All reads feed through actionmap; that's the choke point.
- Do **not** touch the legacy `menuPush/menuPop` plumbing further — F-3.1's
  runtime guard stays until Phase 2 replaces it.
- Do **not** alter the input context push/pop semantics (deferred pop, grace
  period, mouse-mode sync). Those are correct and load-bearing.

---

## 8. Unresolved

- (§3.5) Shared-action leak is a known Phase 1 residual. Phase 2 addresses it.
- Audio skipping/pausing during gameplay (threading-suspected) is **not**
  input-related; logged as B-XXX in `context/bugs.md` for a separate
  investigation.
