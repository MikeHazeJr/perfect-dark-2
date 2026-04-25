# Contextual Input Schemes -- IMC Architecture Formalization

**Status:** DESIGN PROPOSAL, no implementation. Mike reviews before any code lands.
**Author:** AI session 2026-04-24 (S456) on Mike's verbatim spec.
**Implementation queue:** Priority J (this design) is read-only this batch. Phase 1 implementation lands as Priority J-impl in a future batch after Mike's review.

---

## 1. The methodology, restated

> "I think what makes sense is multiple input schemes. One for menu interaction, mission gameplay, combat simulator gameplay (same as mission but has things like hold back for scorecard), vehicle control, and The Grid. They get applied contextually." -- Mike, 2026-04-24

The core idea: each scene / mode has its own input scheme. The scheme is composed of bindings. The active scheme is selected by the current scene state. There is no `if (scene == X)` ad-hoc logic anywhere downstream; the IMC dispatcher's priority + activate/deactivate flow is the *only* mechanism for context switching.

The codebase already has the right substrate -- `g_ImcGameplay`, `g_ImcMenu`, `g_ImcVehicle`, `g_ImcForge`, `g_ImcForgeSession` all exist. What's missing is the discipline to use them consistently and to avoid the "Combat Sim is mission with extras" temptation that ends in `if (g_GameMode == COMBAT_SIM)` conditionals scattered across input handling.

---

## 2. The IMC inventory (after this design)

| IMC | Priority | Lifetime / activation | Purpose |
|---|---|---|---|
| `g_ImcMission` | 0 | Active when in solo / co-op campaign play, no menu on top, not in vehicle, not in Grid | Mission gameplay. Movement + combat + interact + N64 C-button look. |
| `g_ImcCombatSim` | 0 | Active when in CS / MP gameplay, no menu on top, not in vehicle, not in Grid | Combat Sim gameplay. Same baseline as Mission but adds **hold-Back-for-scorecard** semantic. Mutually exclusive with `g_ImcMission`. |
| `g_ImcVehicle` | 5 | Pushed on mount, popped on dismount | Vehicle control (throttle / steer / weapon / exit). Shadows the gameplay IMC for vehicle-relevant VKs. |
| `g_ImcForgeSession` | 6 | Active for the entire Grid session (NORMAL + FREEFLY) | Grid mode toggle (Back -> FORGE_TOGGLE). Whole-session because the toggle must work in both Playtest and Forge directions. (Already shipped, S456 472faddd.) |
| `g_ImcForge` | 7 | Active in FREEFLY only | Editor-specific bindings (camera triggers / boost / precision / sidebar / tab cycle). (Already shipped.) |
| `g_ImcMenu` | 10 | Pushed on menu open, popped on close | Menu navigation. Up / Down / Left / Right / TabPrev / TabNext / Use / Cancel. |
| `g_ImcPauseMenu` | 11 | Pushed when pause menu opens over a session | Pause-specific (close pause vs close in-game menu). |
| `g_ImcDebugOverlay` | 20 | Pushed when F12 debug opens | Debug overlay nav. |
| `g_ImcTextInput` | 30 | Pushed for chat, rebind, text fields | Captures all keys for raw text. |

What this changes from today:

- `g_ImcGameplay` (priority 0) splits into `g_ImcMission` and `g_ImcCombatSim`. The split is mutually exclusive; never both active.
- `g_ImcVehicle` already exists at priority 5 but its activation lifecycle is currently tied to scene loads, not vehicle mount/dismount. Tighten that.
- The forge IMCs already have correct lifecycle (S456 work). Document them as the canonical pattern others should follow.

---

## 3. Activation rules (state machine)

Each IMC has exactly one activation site and exactly one deactivation site. No `setInputCtx`-style ad-hoc calls.

```
Scene-load events:

   solo_mission_load -> imcActivate(&g_ImcMission)
   solo_mission_unload -> imcDeactivate(&g_ImcMission)

   combat_sim_load -> imcActivate(&g_ImcCombatSim)
   combat_sim_unload -> imcDeactivate(&g_ImcCombatSim)

   coop_anti_match_load -> imcActivate(&g_ImcMission)   (co-op runs the mission scheme)
   coop_anti_match_unload -> imcDeactivate(&g_ImcMission)

Mount / dismount:

   player_mount_vehicle -> imcActivate(&g_ImcVehicle)
   player_dismount_vehicle -> imcDeactivate(&g_ImcVehicle)

Grid session:

   forge_session_start -> imcActivate(&g_ImcForgeSession)   (already shipped)
   forge_freefly_enter -> imcActivate(&g_ImcForge)          (already shipped)
   forge_freefly_exit -> imcDeactivate(&g_ImcForge)         (already shipped)
   forge_session_end -> imcDeactivate(&g_ImcForgeSession + &g_ImcForge)  (already shipped)

Menu push / pop:

   menupool_acquire(any) -> imcActivate(&g_ImcMenu)
                           (and deeper modal IMC if applicable -- see section 4)
   menupool_release(last) -> imcDeactivate(&g_ImcMenu)

Pause:

   pause_open -> imcActivate(&g_ImcPauseMenu)
   pause_close -> imcDeactivate(&g_ImcPauseMenu)

Debug:

   F12_press -> imcActivate(&g_ImcDebugOverlay)
   F12_close -> imcDeactivate(&g_ImcDebugOverlay)

Text input:

   text_field_focus -> imcActivate(&g_ImcTextInput)
   text_field_blur -> imcDeactivate(&g_ImcTextInput)
```

### Invariants

1. **Mission and Combat Sim are mutually exclusive scenes.** A scene-load event for one MUST deactivate the other if it was active. Document this with an assertion at scene-load time (see section 7).
2. **The vehicle IMC stacks on top of whichever gameplay scene is active.** Mount during Mission: `g_ImcMission` stays active, `g_ImcVehicle` (priority 5) shadows on top. Dismount: `g_ImcVehicle` deactivates, `g_ImcMission` resumes ownership of those VKs.
3. **The Grid IMCs stack on top of whichever scene loaded the session.** A Grid session can be entered from either Mission or Combat Sim contexts (less common but not blocked). The Grid IMCs activate; the underlying gameplay IMC remains active underneath but is shadowed.
4. **The menu IMC always wins over gameplay IMCs.** Priority 10 ensures menu nav captures Up / Down / Left / Right regardless of any gameplay binding. (Existing behaviour; preserve.)
5. **At any moment, exactly one IMC is the "owning" IMC for a given VK.** This is the dispatcher's existing single-winner-per-IMC + priority model. Section 5 covers this.

---

## 4. Suppression vs deactivation

When a higher-priority IMC activates, what happens to the lower one? Two semantics in tension:

### Option A: lower IMC stays active, higher IMC shadows it

The dispatcher walks IMCs top-down. The higher IMC's binding wins for a given VK; the lower IMC's binding for the same VK never fires. But VKs the higher IMC does NOT bind fall through to the lower IMC.

This is the **current behaviour** of `g_ImcForge` (priority 7) over `g_ImcGameplay` (priority 0): in FREEFLY, X = sidebar (forge wins) but movement keys WASD = move (gameplay binding falls through because forge doesn't bind WASD).

### Option B: lower IMC gets deactivated when the higher one activates

The lower IMC is removed from the active stack. Its bindings are unreachable. If the higher IMC doesn't bind a given VK, no binding fires.

This is what S456's first FREEFLY pass attempted via `actionIsBlockedInFreefly` -- a runtime predicate that suppressed individual actions. The cleaner version of this is to deactivate the IMC entirely.

### Per-IMC choice

**Mission / Combat Sim:** Option A (shadow). Vehicle and Grid stack on top via priority; gameplay bindings still serve the underlying scene. Mutually exclusive scene activation rule (section 3 invariant 1) means the OTHER scene's IMC is deactivated, not just shadowed.

**Vehicle:** Option A (shadow). On mount, vehicle bindings shadow the gameplay scene's bindings for steering / accelerate / brake / weapon / exit. Anything the vehicle IMC does NOT bind falls through to the gameplay scene (e.g. PAUSE, scoreboard hold).

**Forge IMCs:** Option A (shadow), already designed this way. (S456.)

**Menu IMCs (`g_ImcMenu`, `g_ImcPauseMenu`):** **Option B (deactivate gameplay)** behaviour at the read-time gate via `gameplayInputSuppressed()`. Today the gate sits in `actionPressed` / `actionAxis` etc. and zeros out gameplay-only action reads when a menu is on top. That's effectively Option B applied at the read site; the IMC stack still has gameplay underneath (because that's how the dispatcher is wired) but its outputs are suppressed.

Document this distinction explicitly so future contributors don't conflate "menu shadows gameplay" with "menu deactivates gameplay" -- the second statement is the correct user-visible model, but the implementation uses a hybrid.

---

## 5. The mutual-exclusion invariant (Mission xor Combat Sim)

The scene loader is the single arbiter:

```
On scene change:
    1. Deactivate every gameplay-scope IMC (Mission, CombatSim).
    2. Activate exactly one based on the new scene's kind.
    3. Assert:  exactly_one_of(Mission.active, CombatSim.active) == true
                (until next scene change deactivates both).
```

Drift symptom if violated: both IMCs active, dispatcher picks the lower-enum action on tie, scoreboard-on-hold breaks in CS or fires in Mission. The assertion catches the drift at activation time.

---

## 6. The hold-vs-tap discrimination -- mechanism

> "Combat simulator gameplay (same as mission but has things like hold back for scorecard)" -- Mike

This is the canonical example of "two schemes share a baseline; one adds a semantic." Three implementation options:

### Option 6.1: HOLD_DURATION_MS qualifier on the binding

Extend the `InputMapping` struct with an optional `hold_ms` field. A binding with `hold_ms > 0` only fires when the key has been held for that long; otherwise the next-lower-priority binding wins.

Pros: data-driven, no new actions. Cons: changes the binding struct shape; rebind UI needs to surface the qualifier.

### Option 6.2: parallel `ACTION_SCORECARD_HOLD` action

Add a new action constant that fires only on hold. CS IMC binds Back to `SCORECARD_HOLD`; Mission IMC binds nothing (Back has no use in Mission today, so no shadow).

Pros: minimal infra change; reuses existing `actionHeldForMs()` accessor. Cons: doubles the action enum's growth pattern -- every "tap vs hold" pair gets two actions.

### Option 6.3: caller-side hold detection

The CS scoreboard widget reads `actionHeld(0, ACTION_SCORECARD)` itself, checks `actionHeldForMs(0, ACTION_SCORECARD, threshold_ms)`, decides what to do.

Pros: zero infra change. Cons: the discrimination is implicit in caller code, not in the binding scheme. Future "hold X for sprint cancel" or similar would each ad-hoc this.

### Recommendation: **Option 6.2** (parallel actions)

The growth concern is over-stated -- each genuine hold-vs-tap distinction adds one action, and the codebase only has a handful of such distinctions today (USE-tap-vs-hold for reload-vs-interact is the existing example, currently using Option 6.3). Option 6.2 keeps the IMC layer in charge: the answer to "what does Back do in CS?" is "the binding table says SCORECARD_HOLD."

Implementation note: the `actionmap_dispatch` already supports per-action `down_time_ms` tracking; adding a hold-qualified accessor is a thin wrapper.

---

## 7. Migration plan: gameplay -> mission + combat sim

### What moves into both new IMCs

Most of `g_ImcGameplay`'s bindings are shared between Mission and CS:

- Movement: WASD, left stick, sprint, crouch, jump
- Aim: right stick, mouse, C-buttons
- Combat: fire, secondary, fire-mode, reload, throw weapon
- Weapon select: prev/next, weapon 1-6
- Interact: USE, CANCEL_USE
- Vehicle binds: stay in `g_ImcVehicle` (already separated)
- D-pad: stay in gameplay scope but in BOTH new IMCs

A "shared baseline" can be expressed two ways:

#### 7.1 Duplicate the bindings

Each IMC holds its own copy. `setupMissionDefaults` and `setupCombatSimDefaults` both call addBind for every shared key. Cheap, less elegant, more drift risk.

#### 7.2 Inheritance / fallback IMC

`g_ImcGameplayBase` (priority -1) holds shared bindings. Both `g_ImcMission` and `g_ImcCombatSim` activate alongside it (with higher priority). A VK lookup hits the specific IMC first; on miss, falls through to the base. This is the same priority-fallthrough mechanism the dispatcher uses today.

Pros: single source of truth for shared bindings. Cons: introduces a base IMC concept that's never deactivated; the activation lifecycle gets a third element (gameplay base, scene IMC, plus shadows).

### Recommendation: **7.1 duplicate** for phase 1

Drift risk is small because both IMC defaults live in the same file (`actionmap.cpp`) and the rebind UI re-renders both side-by-side. A diff between the two functions during code review catches drift instantly.

Switch to 7.2 if the divergence list grows past 3-4 actions. For now: Mission has ZERO unique bindings; CS has ONE (`SCORECARD_HOLD`). Duplication is fine.

---

## 8. Stack invariant: collapse to one stack

The audit-flagged complaint is that the codebase has TWO parallel stacks:

1. The visual menu stack (`menupool` -- which dialog is visible).
2. The input-context stack (`inputctx` + IMCs).

These can drift. A menu can be visible without input authority, or vice versa. The fix proposed in K's framing: collapse them into ONE stack so `menuPush` / `menuPop` are the SOLE mechanisms for changing input authority.

This design doc captures the principle but defers the structural collapse to **Priority K** (input-authority discipline audit + methodology + drift-site fixes), which Mike has flagged as a separate batch. The collapse is THE structural fix; the IMC inventory and activation rules above are necessary but not sufficient until the stack collapse lands.

K's scope (per Mike's directive):
- Audit current `menuPush` / `menuPop` discipline.
- Identify drift sites (visual without input ctx, ad-hoc setInputCtx, etc.).
- Collapse to one stack.
- Add invariant assertion in push / pop functions.
- Verify Issue 3 (pause-menu input failure) closes by consequence.
- Document the methodology as a standalone doc.

This doc (J) defines the schemes; K enforces the discipline. They land sequentially.

---

## 9. Phasing

Sequential, each phase build-verifies and commits independently.

### Phase J-1 -- split gameplay into mission + combat sim

**Files touched:**
- `port/include/actionmap.h` -- add `g_ImcMission` + `g_ImcCombatSim` extern decls, deprecate `g_ImcGameplay`.
- `port/src/actionmap.cpp` -- rename `setupGameplayDefaults` to `setupMissionDefaults`; add `setupCombatSimDefaults`; activation rule in scene-load wherever `g_ImcGameplay` was activated; assert mutual-exclusion.
- Scene-load callers (`src/lib/main.c` or wherever `mainChangeToStage` lands) -- swap the activation call to the right IMC based on scene kind.

**Exit criteria:** mission-load activates Mission IMC, CS-load activates CombatSim IMC, never both. `g_ImcGameplay` removed (or deprecated as alias of Mission with a logged warning).

### Phase J-2 -- vehicle IMC lifecycle tightening

**Files touched:**
- `port/src/actionmap.cpp` -- existing `g_ImcVehicle` already has bindings.
- `src/game/bondbike.c` (or wherever mount/dismount happens) -- `imcActivate` on mount, `imcDeactivate` on dismount.

**Exit criteria:** mounting hoverbike activates vehicle IMC (steering keys reach vehicle); dismounting deactivates it cleanly (steering keys revert to gameplay).

### Phase J-3 -- hold-vs-tap discrimination

**Files touched:**
- `port/include/actionmap.h` -- add `ACTION_SCORECARD_HOLD` (or whatever the design picks for option 6.2).
- `port/src/actionmap.cpp` -- bind in `setupCombatSimDefaults`.
- Scoreboard widget in `port/fast3d/pdgui_*` -- reads `actionPressed(0, ACTION_SCORECARD_HOLD)` instead of conditional.

**Exit criteria:** Back-tap in CS does nothing; Back-hold (>500ms) raises scoreboard. Mission scene's Back-tap and Back-hold both no-op. No conditional in the scoreboard handler.

### Phase J-4 -- documentation + assertions

**Files touched:**
- `context/designs/input-authority-methodology.md` -- the standalone methodology doc K's framing references.
- `port/src/actionmap.cpp` -- assertion in `imcActivate` that checks the mutual-exclusion invariant for the gameplay-scope IMCs.

**Exit criteria:** future contributor reads the methodology doc, finds the activation rules and the IMC inventory; assertion fires if anyone activates Mission while CS is active.

---

## 10. Open questions

- **Q1 (J).** Hold-vs-tap implementation: option 6.2 (parallel action) or 6.3 (caller-side detection)? Recommendation 6.2 above; flagged for Mike's call.
- **Q2 (J).** Shared baseline: duplicate (7.1) or inheritance IMC (7.2)? Recommendation 7.1 for phase 1.
- **Q3 (J).** Co-op / anti-counter-op: do they run on Mission IMC (campaign-style) or CombatSim IMC (MP-style)? Spec implies Mission; confirm.
- **Q4 (J).** Vehicle priority 5 vs Grid forge priority 6/7: are vehicle binds reachable in a Grid session? (Answer: probably not -- you don't drive the hoverbike inside The Grid -- so the priority order is harmless either way. Confirm.)
- **Q5 (J).** Does `g_ImcGameplay` remove cleanly, or are there callers that should stay on a "shared gameplay" alias? Audit during phase J-1.

---

## 11. Out of scope for this design

- The stack-collapse work itself (Priority K).
- The connectivity / friend-play presence channels (Priority I, separate design doc).
- Adding new scene kinds beyond Mission / CS (Forge has its own IMCs already; no other scenes pending).
- Per-player IMCs for split-screen MP. PD2 is single-seat.
