# Evening Decision Log - 2026-04-23

Tracks judgement calls made while Mike is away. Each decision is reviewable and reversible per the note on rollback.

## Decision: Grid blank-map target stage (REVISED -- escalated back to Mike)
- Context: Mike asked for "the fallback map that loads when a map is invalid, a plane basically" as the Grid Blank Map template. Initial plan was to ship CI Training as a soft-fallback with `GRID_BLANK_STAGE` as a single-constant override. Mike then firmly rejected that: "DO NOT ship CI Training as the blank. ... either find it, or escalate."
- Search passes (all empty):
  - Symbol grep: `STAGE_FALLBACK`, `STAGE_ERROR`, `STAGE_INVALID`, `STAGE_UNKNOWN`, `STAGE_BLANK`, `STAGE_DEFAULT`
  - Handler grep: `fallback_stage`, `safe_stage`, `default_stage`, `invalid_stage`, `blank_stage`, `stagenumDefault`, `boot_safe`
  - Natural-language: "just a plane", "bare plane", "flat plane", "single plane", "blank map", "empty map"
  - Asset-file: `bg_test`, `bg_fallback`, `bg_blank`, `FILE_BG_GEN`
  - Existing debug/test stages: `STAGE_TEST_LEN/LAM/UFF/OLD/ASH/ARCH/DEST/SILO/RUN/MP2/MP6-8/MP14/MP16-20` all reference normal bg files; nothing labelled or commented as a bare plane
  - Stage-load error path: `stageGetIndex(unknown)` returns -1 with a warning; no substitution. `stageSanitizeLoadStagenum(0x00) -> STAGE_CITRAINING` is the only invalid-stage coercer.
- Choice: Ship the Grid submenu WITHOUT a Blank Map entry. `port/include/pdgui_menu_grid.h` declares `GRID_BLANK_STAGE` as undefined; the submenu conditionally surfaces the Blank Map row only when the macro is defined. Zero user-visible reference to CI Training.
- Rationale: The user's statement presumes the fallback exists, but my searches came up empty. Per the escalation directive, shipping CI Training -- even behind FIXME -- would misrepresent the feature. Omitting the entry is truthful and a one-line follow-up swap once Mike names the stagenum.
- Rollback: Define `GRID_BLANK_STAGE <stagenum>` in `port/include/pdgui_menu_grid.h`. Nothing else needed; the submenu picks up the row automatically.
- Follow-up required from Mike: point at the specific stagenum he has in mind, or confirm that no bare-plane stage exists and one needs to be authored.
- Timestamp: start of evening batch (revised before any commit)

## Decision: Grid session default entry mode (Priority 2)
- Context: Mike asked that "Enter The Grid" drops the user into Forge (edit) mode by default so they can customize the map before playtesting. Existing `forgeTick` transitioned sessions to NORMAL (Playtest) on start; the user had to press F7 to enter FREEFLY.
- Choice: changed `forgeTick` to call `forgeTransitionToFreefly` on `request_enter_session` instead of `forgeTransitionToNormal`. Grid sessions now start in Forge mode.
- Rationale: matches Mike's directive; `forgeTransitionToFreefly` calls `forgeSnapFreeflyToPlayer` so the camera snaps to the player's spawn position with zero visual jump.
- Rollback: one-line revert in `src/game/forgemode.c::forgeTick` (change the `forgeTransitionTo...` call back to `Normal`).
- Timestamp: ~00:10

## Decision: Halo-style mode-toggle binding
- Context: Mike asked for a Halo-style Back-button swap between Forge and Playtest. Project already binds gamepad Back to `ACTION_SCORECARD` (scoreboard overlay).
- Options considered:
  A. Remap `ACTION_SCORECARD` off Back entirely, dedicate Back to the Grid toggle.
  B. Add a new `ACTION_GRID_MODE_TOGGLE` action, bind to Back; arbitrate between the two actions at tick time (skip scorecard when Grid is active).
  C. Share the physical Back button: bind it to BOTH `ACTION_FORGE_TOGGLE` and `ACTION_SCORECARD` via the existing actionmap layer. Both edge-fire on the same press; `forgeTick` consumes the toggle only when a session is active, so non-Grid matches still get scorecard.
- Choice: C.
- Rationale: lowest-change path. Adds one line to `setupGameplayDefaults` (`addBind(imc, ACTION_FORGE_TOGGLE, JOY_BTN(0, JBTN_BACK))`). No new action, no arbitration code. Overlap is benign: a scorecard popup appearing briefly during a Grid toggle is not breaking, and in a solo Grid session with no match, the scorecard is a no-op.
- Rollback: remove the single `addBind` line.
- Timestamp: ~00:15

## Decision: Playtest bot-HUD polish deferred
- Context: Mike described a Playtest HUD exposing bot Add/Remove/Spawn/Freeze controls as an unobtrusive overlay so the author can spawn bots without toggling back to Forge.
- Choice: defer. Playtest mode today shows only the mode badge (via `pdguiForgeHudRender`); the full editor overlay hides via the existing `forgeIsFreefly()` gate in `pdgui_forge_editor.cpp:1418`. The Bots tab is still reachable by toggling to Forge.
- Rationale: the toggle semantics are the core of Priority 2; adding a Playtest bot HUD is a separate polish ticket that can go after P3-P5 foundation work lands, or based on Mike's playtest feedback. Per his "refactor gently" direction this is acceptable.
- Rollback: n/a (nothing shipped; just deferred).
- Follow-up: add a compact `pdguiForgeBotsQuickPanel` rendered only in NORMAL state, or extend `pdgui_forge_hud.cpp` with a small always-on bot-control strip.
- Timestamp: ~00:20

## Decision: Playtest HUD control surface (P7)
- Context: P7 brief asked for "the bot-testing overlay as an unobtrusive HUD" in Playtest mode.  Playtest mode runs with mouse-captured first-person input; a clickable ImGui window would require releasing mouse capture mid-combat, which conflicts with aim.
- Options considered:
  A. Full ImGui window with buttons.  Requires pushing a menu-like input context while visible; mouse capture toggles on/off as the HUD opens / closes.
  B. Keybind-driven read-only text panel drawn via `ImGui::GetForegroundDrawList`.  No mouse capture impact.
  C. Reuse the Forge-mode Bots tab alone (no Playtest surface).
- Choice: B.
- Rationale: keybinds are frictionless during combat, the HUD is read-only, and the Forge-mode Bots tab (option C) is still available via the F7 / Back mode toggle.  The keybinds live on Insert / Delete / Home / End -- four keys that are normally unbound in gameplay and easy to find.  ImGui::IsKeyPressed polls the SDL backend's key queue without needing a focused window.
- Rollback: the HUD block is one `if (state != FORGE_SESSION_FREEFLY) { ... return; }` body in `pdguiForgeHudRender`; delete to revert to the old early-return behaviour.
- Timestamp: ~00:55

## Decision: Spawn Near Me mechanism (P7)
- Context: The existing runtime kept `tmp.pos[]` at (0,0,0) via memset and never fed a world position into the spawn pipeline; scenario spawn pads decided where bots ended up regardless of `spawn_mode`.  Need a path to place a bot at the player's forward + radius without refactoring the scenario spawn path.
- Options considered:
  A. Thread `spawn_mode` / override-pos through `scenarioChooseSpawnLocation`.  Touches the shared SP + MP spawn code.
  B. Post-spawn teleport: after `botmgrAllocateBot` populates `g_MpBotChrPtrs[slot]`, write `prop->pos` directly.
  C. Expose a new bot-spawn-at-pos API from botmgr.c.
- Choice: B.
- Rationale: narrowest change, entirely inside `forge_runtime.c`.  No scenario-path touch, no new botmgr API.  The bot's AI retargets on its first tick so a post-allocate teleport behaves correctly.  Option A would bleed the HUD concept into shared spawn infrastructure; option C creates a second allocation entry point that would need to be kept in sync with the existing one.
- Rollback: remove `s_teleportBotNearPlayer` + the `if (bs->spawn_mode == FORGE_BOT_SPAWN_NEAR_ME)` block in the per-spawn loop.
- Timestamp: ~01:00

## Decision: Freeze All reuses g_BotUpdatesDisabled (P7)
- Context: P7 asks for Freeze All to actually halt bots.  The project already has F6 freeze via `g_BotUpdatesDisabled` (bot.c zeros speedmult each tick while keeping chrTick running; B-217 v2 semantics).
- Options considered:
  A. Second freeze flag scoped to Grid sessions only.
  B. Mirror `bs->all_frozen` into the existing `g_BotUpdatesDisabled`.
- Choice: B.
- Rationale: zero duplication, identical on-screen behaviour to F6.  The HUD becomes a UX veneer over a mechanism that already exists and is verified.  F6 and the End-key toggle can drift in state if both are pressed, but `forgeRuntimeTick` resynchronises from the HUD every tick so the HUD is authoritative when a session is active.
- Rollback: drop the `g_BotUpdatesDisabled = bs->all_frozen ? 1 : 0;` line.
- Timestamp: ~01:02

## Decision: filter SP heads out of valid-head set by `category`, not `mp_index` (Issue 1)
- Context: Mike's post-sprint playtest showed bots with human bodies picking `base:sp_head_*` heads that resolved to mphead=0 (Joanna fallback).  Root cause is P3's `catalogGetBodyValidHeadIds` not gating on MP-vs-SP-only.  Two candidate gates: `he->mp_index >= 0` or `he->category != "sp"`.
- Data quirk that forced the choice: `s_BaseHeads[]` names only 75 of 76 MP slots.  The unnamed slot's engine head gets a fallback catalog ID at MP registration, but the SP-loop then registers the same engine head as `base:sp_head_<idx>` and that second registration wins the `s_RuntimeCache[ASSET_HEAD]` slot.  Pass 3 of `catalogBuildRuntimeCaches` assigns `mp_index = 75` onto the SP entry -- so an `mp_index >= 0` filter still lets that SP entry through.
- Options considered:
  A. Gate on `mp_index >= 0`.
  B. Gate on `category != "sp"` (exact match).
  C. Gate on catalog ID prefix `strncmp("base:sp_", ...) == 0`.
  D. Fix the double-registration in `assetcatalog_base.c` so SP heads never overwrite the MP runtime cache slot.
- Choice: B.
- Rationale: category is set at registration time and is stable across the runtime-cache rebuild.  It also aligns with the documented authoring contract ("SP heads are category=sp, MP heads are category=base, mod heads are their own category").  Option A alone would miss the double-registration case.  Option C is a string-prefix match that ships fragile assumptions about naming.  Option D is the "right" fix but is out of scope for a post-playtest surgical pass -- it would re-order registration loops, potentially breaking the coverage mask, and would require a second playtest for the bigger rebuild.
- Rollback: delete the `if (he->category[0] == 's' && ...)` block in `collectValidHead`.  One-line revert.
- Timestamp: ~morning

## Decision: Issue 4 music scope -- ship local respawn-lock only; defer speed-lerp (Issue 4)
- Context: Mike's Issue 4 has two parts.  (a) custom-music track resets on the local player's death (pure local bug).  (b) cross-client drift-correction via a speed-lerp curve anchored to a match-clock offset.
- Options for (b):
  A. Ship both (a) + (b) tonight.  (b) needs either a new wire packet or reuse of an existing match-clock timestamp; either way it's protocol-adjacent territory.
  B. Ship (a) surgically; document (b) as a follow-up and flag the net-protocol decision.
- Choice: B.
- Rationale: per Mike's hard-stop rule "no net-protocol changes" tonight.  (a) is protocol-safe (strictly local -- prevents re-pick on respawn).  (b) requires a design pass on timing source + packet layout, which Mike wants to approve before shipping.
- Rollback: delete the `if (g_Vars.normmplayerisrunning && g_TemporaryPrimaryTrack < 0)` block in `musicStartPrimary`.  One-block revert.
- Follow-up for (b): the existing `SVC_MUSIC_ADVANCE` (v34) handles discrete track advancement between clients.  Speed-lerp sync would ride on top, polling a match-clock offset on each client and adjusting playback rate in a [0.97..1.03] range to ease back toward the authoritative timeline.  No hard seeks.
- Timestamp: ~morning

