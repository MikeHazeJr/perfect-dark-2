# Foundation pass summary - 2026-04-23 / 24

Session S453 on worktree `claude/tender-pascal-b0e63a`.  Five priorities landed (6 commits) plus one item escalated.  Evening work per Mike's "go as far as you can, make decisions yourself" directive after his sign-off; decisions captured in `context/audits/evening-decisions-2026-04-23.md`.

## Per-priority summary

### P1 - Grid menu flow (COMMITTED `6cb59cb8`)

**Reported problem.**  "The Grid" button on the Main Menu dropped the user straight into a Forge session on CI Training, skipping map and variant selection.

**What was done.**
- Repointed the button to a new sub-view (`s_MenuView == 6`) instead of the inline `pdguiForgeStartSession()` call.
- Built a Grid submenu with an arena picker (catalog-driven via `assetCatalogIterateByType(ASSET_ARENA)`), a variant editor (gametype combo from `s_BaseGameModes`, time / score limit sliders, teams / one-hit / slow-mo toggles), and an "Enter The Grid" button.
- New `pdguiForgeStartSessionOn(stagenum)` in `pdgui_menu_forge.cpp`.  Old `pdguiForgeStartSession()` becomes a thin wrapper on CI Training so any other caller keeps working.
- Header `port/include/pdgui_menu_grid.h` with `GRID_BLANK_STAGE` documented but intentionally undefined.

**What was deferred.**  The "Blank Map" entry is suppressed until Mike points at the correct invalid-map-fallback stage.  Exhaustive search turned up no bare-plane stage in the codebase; Mike rejected CI Training as a substitute.  When he confirms the target, the one-line fix is to define `GRID_BLANK_STAGE` in `pdgui_menu_grid.h` and the submenu surfaces the row automatically.  See the evening-decisions log for the full search surface.

**Build.**  Clean incremental link on main working copy (PerfectDark.exe 54 MB).

**Playtest.**  Open the Main Menu -> click "The Grid" -> verify: (a) submenu titled "The Grid" appears with a description, an arena ListBox, a variant editor, "Enter The Grid" and "Back" buttons; (b) clicking an arena changes the selection; (c) clicking Enter lands the user in a Forge session on the chosen arena; (d) Back returns to the Main Menu top-level.

### P2 - Halo-style Forge <-> Playtest toggle (COMMITTED `c1825e9c`)

**Spec.**  In-session Back-button swap.  Same stagenum, same bot roster, same variant.  Player drops in at the current Forge camera position; Forge camera re-attaches at the player's current position on swap back.  No reload.

**What was done.**
- `forgeTick`'s `request_enter_session` branch now calls `forgeTransitionToFreefly` on session start so the user lands in Forge (edit) mode by default.  `forgeSnapFreeflyToPlayer` snaps the camera to the player's spawn pos / yaw / pitch with zero visual jump.
- Added gamepad Back binding on `ACTION_FORGE_TOGGLE` in `setupGameplayDefaults`.  Keyboard F7 retained.  Back also fires `ACTION_SCORECARD` (pre-existing); overlap is benign in a solo Grid session -- decision-log entry explains.
- Verified the existing `forgemode.c` NORMAL <-> FREEFLY transitions already preserve position and orientation (FREEFLY writes camera pos / angles to the player prop every tick, so FREEFLY -> NORMAL leaves the player at the last freefly spot; NORMAL -> FREEFLY snaps the camera to the player's current spot via `forgeSnapFreeflyToPlayer`).

**What was deferred.**  A compact in-Playtest bot-control HUD (Add / Remove / Freeze) so authors can spawn bots without toggling back to Forge.  Playtest mode today shows only the mode badge via `pdguiForgeHudRender`; the full editor overlay hides per the `forgeIsFreefly()` gate.  Decision-log entry "Playtest bot-HUD polish deferred".

**Build.**  Clean incremental link.

**Playtest.**  Enter The Grid -> verify session starts in FREEFLY (mode badge reads "THE GRID -- FREEFLY"); press F7 or controller Back -> badge flips to "THE GRID -- NORMAL" and first-person combat takes over; press F7 / Back again -> Forge camera detaches at the player's current spot.

### P3 - Per-body valid-head set + randomization (COMMITTED `336096bd`)

**Spec.**  31 Maian bots should get 31 varied Maian heads instead of sharing one.  Today's B-235 fix only picked a deterministic pair for specific-type bodies.

**What was done.**
- `catalogGetBodyValidHeadIds(body_id, *out_count) -> const char *const *` enumerates every head whose `HEADBODYTYPE_*` is compatible with the body (same type; or DEFAULT+DEFAULT; or FEMALE <-> FEMALEGUARD cross-pair).
- `catalogPickRandomHeadIdForBody(body_id) -> const char *` wraps the list accessor with `rngRandom() %% count`.
- `pickHeadIdForBody` in `matchsetup.c` now calls the new picker; so all bot-creation paths (`matchConfigAddBot`, `pickRandomBodyHead`) get the broader set.
- Two Set Character sites in `pdgui_menu_room.cpp` (multi-select menu + individual-bot edit modal) replaced `mpDefaultHeadForBody(b) -> catalogMpHeadId` with `catalogPickRandomHeadIdForBody(bid)` inside the per-bot loop.

**Edge cases handled.**  Body with exactly one valid head -> deterministic (modulo-1 picks the only element).  Body with zero valid heads -> `LOG_WARNING` + fall back to `catalogGetBodyDefaultHead`.  No persisted-state change; `head_id` stays a catalog ID string.

**Build.**  Clean link.

**Playtest.**  Select Character -> pick Maian body -> verify the head varies on repeated picks.  Multi-select 31 bots -> Set Character -> Maian -> verify each bot gets a different Maian head.  Connery body (HEAD_RANDOM_GENDER male pool) -> verify varied male heads.

### P4 - Catalog display_name primary for all bodies (COMMITTED `a2b0fcf9`)

**Spec.**  Today's B-226 fix populated `ext.body.display_name` only for the 6 bodies with junk langids.  Mike's directive: catalog is PRIMARY, langbank is FALLBACK, for every body.

**What was done.**  Dropped the `if (idx >= 57 && idx <= 62)` gate around `catalogSetBodyDisplayName(e, s_BaseBodies[i].desc)` in `assetcatalog_base.c::assetCatalogRegisterBaseGame`.  All 63 base bodies now ship with their `desc` string as the catalog display name.  `mpGetBodyName` already preferred the catalog override when set, so this is a one-line change that retires the "langid drift on a body" bug class.  Langbank remains reachable as a fallback for any body that leaves `display_name` empty (mod bodies).

**Trade-off.**  Non-English locales lose langbank translation for the 63 English canonical names.  Flagged in the 2026-04-23 systematic-pass audit; Mike accepted English canonical.  Per-body revert is one line.

**Build.**  Clean link.

**Playtest.**  Verify every body in Character Select shows a sensible English name (no "Dinner Jacket" duplicates, no junk UI strings).

### P5 - SP-stages-in-MP loader, B-228 (COMMITTED `216fd27f`)

**Reported problem (B-228).**  When an SP-class stage (CI, Chicago, Villa, ...) is hosted as an MP arena, elevators and fire-escape step props don't load.  Root cause: SP-authored setup blobs set difficulty / player-count exclusion bits on transport props intended for the original campaign; the MP load path's `(obj->flags2 & diffflag) == 0` filter rejects them.

**What was done.**  Compute a second filter value `mptransport_diffflag` alongside the standard `diffflag` in `setupLoadStage`.  For SP-in-MP class stages (`STAGE_CITRAINING`, `STAGE_CHICAGO`, `STAGE_VILLA`, `STAGE_INFILTRATION`, `STAGE_G5BUILDING`, `STAGE_PELAGIC`) the transport filter is forced to 0 while `g_Vars.mplayerisrunning`.  `OBJTYPE_LIFT` and `OBJTYPE_ESCASTEP` cases use the relaxed filter; every other objtype keeps the standard diffflag so SP-only clutter stays filtered.  S310's `liftActivate` auto-register still populates `g_Lifts[]`.

**Protocol safety (Mike's hard stop).**  `pd-server` doesn't run `setupLoadStage` at all (server_stubs.c covers all game logic on the headless server).  Each client runs this code locally against identical setup blobs, so every client ends up with the same live lift set.  The existing server-auth `liftTick` + prop sync path handles position updates.  No new messages, no new fields, no protocol version bump.

**Diagnostic.**  `SETUP.LIFT: SP-in-MP stagenum=0x%02x -- relaxing LIFT/ESCASTEP exclude filter (diffflag 0x%x -> 0)` fires once per MP-on-SP-stage load.

**bugs.md row B-228 flipped from OPEN / DEFERRED to FIXED-PENDING-PLAYTEST.**

**Build.**  Clean link.

**Playtest.**  (1) CI via Combat Sim: elevator pads behave (call / open / travel / open).  (2) Chicago via Combat Sim: fire-escape stair area is traversable; lifts move.  (3) Native MP arena regression: Grid, Skedar, Temple, Ravine still lift / door as before; diagnostic log does NOT fire.

### P6 - SP-stage MP-readiness audit matrix (COMMITTED `b9e3dca3`)

**What was done.**  Walked every arena registered in the catalog and classified MP-readiness.  Matrix stored at `context/audits/sp-stage-mp-readiness-2026-04-24.md` with rubric: NATIVE-MP, SP-IN-MP P5-RELAXED, SP-IN-MP PENDING TEST, BLACKLISTED.  Verified `stagenumIsPlayableInMp` blacklist (3 stages) matches the data-absent entries; no erroneous whitelisting.

**Follow-ups surfaced.**  9 SP stages in the "PENDING TEST" bucket (defection, investigation, airbase, airforceone, crashsite, deepsea, defense, attackship, skedarruins).  If any fails the P5 diagnostic during playtest, add its stagenum to the relax list in `setupLoadStage`.

## Architectural deltas

| Subsystem                    | Before                                                                                                 | After                                                                                                    |
|------------------------------|--------------------------------------------------------------------------------------------------------|----------------------------------------------------------------------------------------------------------|
| Main Menu -> The Grid        | Single button dispatched straight into `pdguiForgeStartSession()` on CI Training.                      | Button opens a Grid submenu (map picker + variant + Enter); session launches via `pdguiForgeStartSessionOn(stagenum)`. |
| Grid session default mode    | Started in NORMAL (Playtest), required F7 to enter Forge.                                              | Starts in FREEFLY (Forge), Back / F7 swaps to Playtest in-place via existing transitions.                |
| Grid mode toggle binding     | Keyboard F7 only.                                                                                      | Keyboard F7 + gamepad Back (Halo convention).                                                            |
| Body -> head pairing         | `mpDefaultHeadForBody` returned a deterministic default for specific-type bodies.                      | `catalogPickRandomHeadIdForBody` returns a fresh random pick from the full HEADBODYTYPE-compatible set.   |
| Body display name            | Catalog override only for 6 bodies with junk langids (B-226).                                          | Catalog `display_name` is primary for all 63 base bodies; langbank is a fallback for mod bodies.         |
| SP-in-MP transport props     | Rejected by `setupLoadStage`'s `diffflag` filter on MP host.                                           | Relaxed filter for `OBJTYPE_LIFT` / `OBJTYPE_ESCASTEP` on the 6-stage SP-in-MP class list.               |
| `stagenumIsPlayableInMp`     | 3-stage blacklist (Kakariko, Dark Noon, Paradox).  Pre-pass audit revealed no erroneous entries.       | Unchanged -- audit confirmed no bugs.                                                                    |

## Remaining foundation work

1. **Blank Map stage.** `GRID_BLANK_STAGE` is undefined until Mike points at the right stagenum.  When he does, single-line fix.
2. **Playtest bot-HUD in Playtest mode.** Compact Add / Remove / Freeze panel so authors don't have to toggle back to Forge to spawn bots.  Small ImGui panel near the mode badge; no new actions needed.
3. **Lift sync drift validation.** P5 loads the lifts; whether two clients see the lift at the same position when one interacts is covered by existing server-auth replication but needs end-to-end playtest.
4. **PENDING-TEST SP stages.** 9 stages in the audit matrix waiting on playtest to confirm they need the P5 relax.
5. **Mod-authored stages (Bonus category).** Intentionally excluded from the P5 relax list; per-stage playtest to decide.
6. **Non-body catalog consolidation.** P4 promoted body display_name to primary; the same pattern should apply to heads, arenas, weapons, scenarios as a follow-up.
7. **Langbank drift audit.** Beyond bodies, confirm other display-name lookups don't have the same split-brain issue P4 addressed.

## Playtest priorities for Mike

In rough order of user-visible impact:

1. **The Grid submenu flow.** Click "The Grid" -> verify map picker + variant + Enter flow works end-to-end on at least three arenas (a native MP, an SP-in-MP, and a Bonus arena).
2. **Halo swap in-session.** Enter The Grid in Forge, F7 or Back -> Playtest, F7 or Back -> Forge.  Verify camera / player position handoff.
3. **Maian variety.** In Character Select, pick Maian body; observe the head varies on repeated selections.  Multi-select N bots -> Set Character -> Maian -> verify N varied heads.
4. **Body names.** Verify Character Select shows sensible English names for every body (Skedar, Dr. Caroll, Connery / Moore / Dalton / Brosnan distinct).
5. **CI / Chicago lifts.** Load CI and Chicago via Combat Sim and walk the elevators / fire-escape area.  Confirm the `SETUP.LIFT: SP-in-MP stagenum=...` log line appears once at match start.
6. **Regression sweep.** Native MP arenas (Grid, Skedar Ruins [MP], Temple, Complex, Felicity, Ravine) still behave.  The SP-in-MP diagnostic log line does NOT fire on these.

## Commit ledger

| SHA         | Scope                        | Files touched                                                                                      |
|-------------|------------------------------|----------------------------------------------------------------------------------------------------|
| `6cb59cb8`  | P1 Grid submenu              | pdgui_menu_forge.cpp, pdgui_menu_mainmenu.cpp, pdgui_menu_grid.h (new), evening-decisions, session-log |
| `c1825e9c`  | P2 Halo-style toggle         | forgemode.c, actionmap.cpp, evening-decisions, session-log                                         |
| `336096bd`  | P3 valid-head set            | assetcatalog.h, assetcatalog_api.c, matchsetup.c, pdgui_menu_room.cpp, session-log                 |
| `a2b0fcf9`  | P4 catalog display_name      | assetcatalog_base.c, session-log                                                                   |
| `216fd27f`  | P5 SP-in-MP loader (B-228)   | setup.c, bugs.md, session-log                                                                      |
| `b9e3dca3`  | P6 readiness audit           | sp-stage-mp-readiness-2026-04-24.md (new)                                                          |

## Builds

All commits are green against the main working copy's `Build/` (PerfectDark.exe clean link).  Warnings are pre-existing (`/*` inside comments).  No new warnings introduced.

A final clean build from `ninja -C Build pd` after the P6 commit is the close-out verification for this pass.

---

## Evening batch 2 - P7 + P8 (post-foundation-pass extension)

### P7 - Playtest bot HUD + runtime wire-up (COMMITTED `e560424a`)

Closes the gap left by P2's deferred bot-HUD polish.  The Forge-mode Bots tab's Add / Remove / Freeze / spawn-mode controls were log-only at runtime; now they do what the labels promise, and a new Playtest HUD surfaces the same controls via keybinds inside Playtest mode.

- `s_spawnBot` now returns the aibotnum (slot) so callers can reach `g_MpBotChrPtrs[slot]` right after `botmgrAllocateBot`.
- New `s_teleportBotNearPlayer(slot, radius)` writes the live bot's `prop->pos` to `player.pos + forward*radius` using `vv_theta` + `sinf/cosf`.  Clamped to 100..5000u.  Facing polish deferred (bot AI reorients on first tick).
- `forgeRuntimeTick` mirrors `bs->all_frozen` into `g_BotUpdatesDisabled` every tick (reuses the F6 freeze machinery; B-217 v2 semantics).  Spawn loops capture the slot, bump active_count / frozen_count, and teleport on `spawn_mode == FORGE_BOT_SPAWN_NEAR_ME`.
- `pdguiForgeHudRender` adds a NORMAL-mode panel top-right with BOTS / Freeze / Mode state + key legend, rendered via `GetForegroundDrawList` (read-only, no mouse capture conflict).
- Keybinds (Insert / Delete / End / Home) polled via `ImGui::IsKeyPressed`.  Keys chosen outside the gameplay actionmap.

### P8 - SP-stage MP-readiness re-audit (documentation + B-228 reopen)

Followed up on the P6 matrix with the promised per-stage audit.  Counted `lift(` / `lift_door(` / `escastep(` macros in every SP and MP setup C file.  Found:

- MP setups for SP-class stages are EMPTY of those macros.  CI, Chicago, Villa, Infiltration, G5, Pelagic, and all 9 PENDING-TEST stages register zero lifts / escasteps in MP mode.
- SP setups DO contain them.  Airbase SP has 5 lifts + 16 doors + 40 escasteps; Attackship SP has 6 + 16; Infiltration SP has 4 + 8.

**Consequence for P5.**  `mptransport_diffflag` is a NOOP on the current codebase -- the filter rejects nothing because nothing loads.  P5 is kept as structural insurance (a mod could ship an MP setup with lift + exclude bits, in which case the relax applies) but alone does not close B-228.

**B-228 reopened** with the corrected diagnosis + Option E sketch.  Option E: for SP-in-MP stages, additionally load the SP setup via `assetLoadToNew(stage.setup_handle, ...)`, iterate it, and run ONLY `OBJTYPE_LIFT` + `OBJTYPE_ESCASTEP` through the existing switch.  Pad references are stage-bound (same `padsfileid`) so they resolve cleanly.  No wire protocol change, no `pd-server` impact.  Deferred as a scoped follow-up; not implemented tonight.

**Readiness matrix at `context/audits/sp-stage-mp-readiness-2026-04-24.md`** extended with the SP-setup transport-prop inventory table and Option E impact ranking.

### P9 / P10 - deferred

Per the soft-stop rule, P9 (non-body catalog consolidation) and P10 (langbank drift audit) left for a fresh session.  Both are doc / refactor work that benefits from clear-headed morning review of the P8 diagnosis before committing to more catalog-layer moves.

### Extended commit ledger

| SHA         | Scope                          |
|-------------|--------------------------------|
| `6cb59cb8`  | P1 Grid submenu                |
| `c1825e9c`  | P2 Halo-style toggle           |
| `336096bd`  | P3 valid-head set              |
| `a2b0fcf9`  | P4 catalog display_name        |
| `216fd27f`  | P5 SP-in-MP loader (NOOP per P8; kept as insurance) |
| `b9e3dca3`  | P6 readiness audit             |
| `24a97db8`  | server-build fix + summary     |
| `e560424a`  | P7 Playtest bot HUD            |
| (this)      | P8 audit update + B-228 reopen |

### Updated playtest priorities (what Mike can verify in the morning)

1. **The Grid submenu flow** (P1) -- map picker + variant + Enter.
2. **Halo-style toggle** (P2) -- F7 / controller Back swaps Forge <-> Playtest in-place.
3. **Playtest bot HUD** (P7) -- Insert spawns, Delete clears, End freezes, Home cycles mode; Spawn Near Me places bot at player forward + radius.
4. **Maian head variety** (P3) -- 31 Maian bots should have 31 varied Maian heads.
5. **Body names** (P4) -- Character Select shows sensible English names.
6. **B-228 REOPENED** -- do not expect CI / Chicago elevators to work yet; P5 is a noop on the current codebase per P8's audit.  Option E implementation is the next step once approved.
