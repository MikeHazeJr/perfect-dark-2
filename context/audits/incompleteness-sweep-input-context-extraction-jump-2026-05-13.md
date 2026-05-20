# Incompleteness Sweep -- Input / Context / File Extraction + Archive / Jump Collision

Date: 2026-05-13
Auditor: Claude main-checkout session (per Mike's goal directive)
Scope: four targeted tracks. Static read-only investigation; no code modified by this audit.
Status: published; awaiting Mike's prioritisation pass.

---

## Executive summary

Four investigation tracks were run in parallel against the current dev tip (HEAD `4080cea3`, post c036 s036-08 slice). Each track was tasked with finding incomplete work; each returned file:line evidence and a severity grade. The headline findings:

1. **Input pillar is structurally clean.** No raw-input call sites remain outside documented bypass seams. The only large open lane is **s036-08 menu graph completion** with **~75 remaining raw `menuPopDialog()` / `menuPushDialog()` sites** (the 2026-05-13 audit's "~35" undercount; my line-anchored grep finds more). 15-25+ sessions at the documented 2-4-edges-per-session cadence to finish. The `gameplayInputSuppressed()` predicate is still a transitional wrapper pending verification playtest. One ledger-hygiene item: **B-298** (vehicle IMC consumers) is closed in code (`src/game/bondbike.c:236-272`, Phase 2 fix #4) but still labeled OPEN in `bugs.md:43`. (Audit-v1 also flagged "B-195 forge editor leak" -- correction: B-195 is actually a separate Complex stage overflow fix; the historical "B-195 leak class" tag in code comments and warning strings refers to a generalised input-focus leak class whose ledger entries are B-196 (WASD-after-menu-close, FIXED 2026-04-19) and B-302 (forge toggle + focus, FIXED-PENDING-PLAYTEST 2026-05-01) -- both correctly labeled, no ledger drift.)

2. **Context system has systemic retention drift.** Nine of eleven pillar docs are 13+ days stale (last touched 2026-04-30 rebuild); `context/README.md`'s "Live state at a glance" cites wire protocol v46 when live is v47, build v0.0.175+ when live is v0.0.197+, and a critical path that has been complete for 10+ days. 19 audits sit beyond the 14-day retention window. `bugs.md` has 90+ `FIXED-PENDING-PLAYTEST` entries that should have been promoted to `FIXED` with commit SHAs. The `.claude/sprint-reports/archive/` directory does not exist -- a small but real orchestrator-flow hole. **HIGH severity** for README drift; **MED** for the rest. Fixable in one focused retention-pass session.

3. **Extraction is structurally complete; external accessibility is not.** All 13 emitter kinds write files; **only 4 of 13** are practically modder-accessible (`.pdwpn`, `.pdhead`, `.pdbody`, `.pdarena` JSON; plus the narrow `.pdui` TGA slice; plus `.pdanim` weapon mnemonics). The remaining 8 kinds (`.pdmesh`, `.pdanim` chr, `.pdsfx`, `.pdvoice`, `.pdsong`, `.pdscenario`, `.pdfont`, `.pdlang`) ship **raw N64 ROM bytes inside a ZIP envelope** -- accessibility-wise equivalent to pre-pivot loose `.bin` dumps. No PC-side decoders, no Blender/Audacity round-trip tools, no `.pdtex` kind for textures, and no in-game UI tool to pack a folder of `.pd<ext>` files into a `.pdmod`. Several `.pdmod`-content classes still cannot be delivered via the archive path (per `pillars/modding.md`). **HIGH severity** for the modder-accessibility gap; the "universality pivot" achieves structural extraction without unlocking the actual modder workflow Mike's design intent calls for.

4. **Jump collision uses a strictly simpler substrate than the Laptop Gun.** The jump's vertical sweep uses `capsuleSweep()` -- 16-step sampling around `cdTestVolume()`, which collects only `GEOFLAG_WALL`-flagged BG tiles + AABB prop blocks + chr cylinders. **It never touches the actually rendered display-list triangles.** The Laptop Gun's sticky placement query (`bgTestHitInRoom` over `vtxbatches`) **does** walk every `G_TRI1`/`G_TRI4` triangle. Mike's two options map cleanly: option (a) = jump today; option (b) = laptop today. The known wall-jump glitch (kanban `c038`, mis-pillared as "vehicles", no B-NNN, undiagnosed) is **structurally explained** by this gap. The capsule sweep also returns an "approximate" hit normal (`-move/|move|`) rather than a real surface normal -- load-bearing for any future surface-aware logic (Skedar Slice 5, wall slide, scary-jump landing). Swapping jump to a two-stage filter (`cdTestVolume` pre-cull then `bgTestHitInRoom` per-triangle validate) is the actual fix shape and unlocks real surface normals. **HIGH severity.**

### Top action items (prioritised)

| Pri | Item | Effort | Tag |
|-----|------|--------|-----|
| 1 | **Audit retention sweep** -- update README + 9 pillar docs to current state (NET_PROTOCOL_VER 47, build v0.0.197+, current critical path), move 19 audits to `_old/audits/2026/`, retire shipped designs from `designs/in-flight/`, cut oldest ~80 session-log entries, create `.claude/sprint-reports/archive/` dir, promote 90+ FIXED-PENDING-PLAYTEST bugs to FIXED with commit SHAs from git history | 1 session | context-retention |
| 2 | **Jump collision two-stage upgrade** -- introduce `bgTestHitInRoom`-based per-triangle pass behind `cdTestVolume` pre-cull in `capsuleSweep`. Returns real surface normals. Closes c038 wall-jump glitch + unblocks Skedar Slice 5. | 1-2 sessions | physics-collision |
| 3 | **Modder accessibility decoders** -- ship at least `.pdmesh` → `.gltf`/`.obj` + `.pdsfx`/`.pdvoice` → `.wav` (with ALADPCMBook embedded in manifest). Per asset kind; can ship one at a time. Also add `.pdtex` emitter for non-UI textures. | 3-5 sessions | modding |
| 4 | **In-game `.pdmod` packer UI** -- expose `modpackPdmodFromFolder` in Modding Hub so users can pack a folder of `.pd<ext>` into a valid `.pdmod` from inside the game. Today only reachable via auto-migration code path. | 1 session | modding |
| 5 | **Menu graph s036-08 continuation** -- continue 2-4 edges/session. 75 sites remaining. Top targets by hot-path frequency: `pdgui_menu_solomission.cpp` (15 sites), `pdgui_menu_cheats.cpp` (9 sites), `pdgui_menu_mainmenu.cpp` (4 sites). Defer `pdgui_menu_training.cpp` (35 sites; static flow, low hot-path value). | 15-25 sessions | input |
| 6 | **Ledger-hygiene close-out** -- mark B-298 (vehicle IMC, fixed in `src/game/bondbike.c:236-272` Phase 2 fix #4) as FIXED-PENDING-PLAYTEST with its actual commit SHA. Confirm B-196 and B-302 are still correctly labeled (no action expected; just verify against today's state). | 30 min | bugs |
| 7 | **Diagnose c038 wall-jump glitch.** Now that the structural cause is known (jump uses cdTestVolume substrate; rendered tris with no WALL-flagged tile pass through), file as B-NNN, re-pillar from "vehicles" to "physics-collision", attach action item 2 as the fix shape. | 30 min ledger | bugs |
| 8 | **`gameplayInputSuppressed()` retirement playtest** -- per L.59, retire the transitional wrapper once verification passes. Schedule the playtest checkpoint Mike committed to in L.59 (mission transitions, cutscene skip/continue, menus, vehicle, observer/freefly, focus loss/regain). | 1 playtest + 1 session | input |

---

## Track 1 -- Input pillar incompleteness findings

### 1.1 Action map raw-input residue

Surface scan across `port/fast3d/`, `port/src/`, `src/game/`, `src/lib/` for `ImGui::IsKeyPressed` / `SDL_GetKeyboardState` / `SDL_GameControllerGetButton` / `SDL_GameControllerGetAxis` / direct `ImGuiKey_` reads.

- **No unmigrated production sites of `ImGui::IsKeyPressed` outside ImGui internals.** All matches in `port/fast3d/pdgui_friends.cpp:958`, `port/include/actionmap.h:167`, `port/src/actionmap.cpp:2757` are historical comments documenting the removed S483b raw Tab bypass. Severity: none.
- **`port/src/actionmap.cpp:1179-1182` calls `SDL_GameControllerGetAxis` directly.** Legitimate -- this is the canonical actionmap analog poller. Documented at file head. Severity: none.
- **`port/src/input.c:1068` `SDL_GetKeyboardState(NULL)`** kept under documented `DEPRECATED` `inputKeyPressed` for VK_ESCAPE rebind capture and VK_MOUSE_LEFT. Joy path retired in s036-04 (commented at `input.c:1040-1058`). Severity: LOW; only legitimate callers remain (optionsmenu.c rebind, menu.c legacy mouse).
- **`port/fast3d/pdgui_menu_mainmenu.cpp:1289-1291` and `pdgui_menu_moddinghub.cpp:5353-5361`** call `nio.AddKeyEvent(ImGuiKey_*)`. These are *write* injections into ImGui's input queue, used as ImGui-nav synthesizers. Legitimate per L.52. Severity: LOW.
- **`port/fast3d/pdgui_style.cpp:664-665`** synthesizes `ImGuiKey_Escape` keydown/keyup pair, documented at line 194. Legitimate.
- **`port/fast3d/pdgui_backend.cpp:550-554`** `drivePressed`/`driveHeld` -> `ImGuiKey_*` bindings inside `pdguiDriveImGuiNav`. Canonical action -> ImGui-nav bridge per B-124. Legitimate.

**Total unmigrated production sites: 0.** All raw-input residue is documented bypass at audited seams. Surface is clean.

### 1.2 Menu graph completion (s036-08)

Real call sites in `port/fast3d` (line-anchored grep, excludes `.retired` and comment refs):

- **Raw `menuPopDialog()` real call sites: 56**.
- **Raw `menuPushDialog(&...)` real call sites: 19**.
- **Combined: 75 raw graph-bypass call sites** across 10 files.

Per-file breakdown (includes a small handful of comment refs that inflate raw counts):

| File | Pop sites | Push sites |
|---|---|---|
| pdgui_menu_training.cpp | 24 | 11 |
| pdgui_menu_solomission.cpp | 12 | 3 |
| pdgui_menu_cheats.cpp | 8 | 1 |
| pdgui_menu_mainmenu.cpp | 4 | 0 |
| pdgui_menu_controldiagram.cpp | 4 | 1 |
| pdgui_menu_mpadvanced.cpp | 1 | 2 |
| pdgui_menu_botsetup.cpp | 1 | 2 |
| pdgui_menu_mpsetup.cpp | 1 | 1 |
| pdgui_menu_mpsettings.cpp | 1 | 1 |
| pdgui_menu_playerconfig.cpp | 1 | 0 |

Note: the 2026-05-13 audit and s036-08 notes claim "~30-35 remaining"; the line-anchored count says **75**. The s036-08 note in `state.json` admits its own count is fuzzy. 15-25+ more sessions at the documented 2-4-edges/session cadence. Severity: LOW (no functional regression, graph-completeness debt).

### 1.3 C-button + controller surface

- **C-button group hidden on visual mapper:** `port/fast3d/pdgui_menu_mainmenu.cpp:2937-2940` strips `BG_CBUTTONS` from `mapperGroups` before `renderControllerVisualMapper`. Constraint honored.
- **Bind table still surfaces C-Up/Down/Left/Right (lines 1673-1677)** intentionally for keyboard/mod bindings. Matches the active constraint.
- **No C-button-equivalent actions on the visual mapper.** Compliant.

### 1.4 Layer / IMC migration leftovers

Open shims documented in L.55+ and known-gaps:

1. **`gameplayInputSuppressed()` transitional wrapper** (L.59) -- intentionally retained pending verification playtest. See 1.5. Severity: MED.
2. **Layer-stack IMC pointers deferred wiring** at `port/src/inputlayer.c:210, 237, 282-289`. `g_LayerGameplay.imc = &g_ImcGameplay` and `g_LayerMenu.imc = &g_ImcMenu` are declarative metadata only; lifecycle is still owned by `inputctx.c` per s036-01 notes. `g_LayerVehicleTurret.imc = NULL` (greenfield). Severity: LOW.
3. **`LAYER_VEHICLE_TURRET` declared but unwired** (`inputlayer.c:281-292`, no callers). Reserved for future. Severity: LOW.
4. **Two PageUp/PageDown raw reads in `renderMainMenu()`** flagged in L.52 ("Remove them when the remaining tab sites and backend PageUp injection are retired together"). PageUp/PageDown backend injection is retired (L.52); the `renderMainMenu` transitional drain still needs final sweep. Severity: LOW.
5. **Cutscene legacy compatibility-shim globals** (`g_InCutscene`, etc.) -- retired in L.51 actually, but worth re-confirming on every slice. Severity: LOW.

### 1.5 `gameplayInputSuppressed()` retirement

Still in code at **`port/src/inputctx.c:716-744`** (definition). Live call sites:

- `port/src/actionmap.cpp:578` (`fireVk` skip)
- `port/src/actionmap.cpp:1135` (`menuActive` flag)
- `port/src/actionmap.cpp:1448` (in `actionLayerAllows` fallback)
- `port/fast3d/pdgui_backend.cpp:594` (drive guard)
- `port/fast3d/pdgui_spectator.cpp:166` (handleKeyboard, migrated from raw WantCaptureKeyboard per s036-05)
- `src/game/forgemode.c:404, 726` (forge transition gating)

Per L.59 (2026-04-28): explicitly retained as transitional wrapper "until verification passes." No verification playtest checkpoint is recorded post-L.59. Severity: **MED**. The architecture intent (Cohort 8) is for this predicate to become a 1-line layer-type check; today it's a 3-condition predicate with focus-settle side effects. Maintenance debt + design-intent gap.

### 1.6 B-298 (vehicle IMC consumers) -- closed in code, open in ledger

**Closed-in-code but not closed-in-ledger.** `src/game/bondbike.c:236-272` ("Phase 2 fix #4 (input-menu pillar, B-298, 2026-05-01)") implements:
- `actionPressed(0, ACTION_VEHICLE_EXIT)` -> `bmoveSetMode(MOVEMODE_WALK)` (dismount).
- `actionValue(0, ACTION_VEHICLE_ACCELERATE/BRAKE/STEER_LEFT/STEER_RIGHT)` mapping to `analogwalk` / `analogstrafe`.

`context/bugs.md:43` still lists B-298 as **OPEN** as of 2026-05-01 entry; audit `2026-05-08-full.md:44` carries it forward as "still deferred". Severity: **LOW** (ledger drift only).

### 1.7 Forge editor input-focus leak class -- closed (correction to audit-v1)

**Correction note**: audit-v1 cited "B-195" as the forge editor focus leak ledger entry. Live `context/bugs.md:149` shows B-195 is actually a separate HIGH bug ("FATAL: overflow when trying to preprocess a bg room", Complex stage `preprocessBgRoom` 8× scratch buffer fix). The historical "B-195 leak class" tag persists in code comments and a rate-limited `pdguiProcessEvent` warning string per B-196's note ("WARNING text retains the B-195 tag from the original filing -- harmless, just a bug-id mismatch in the log string").

The actual ledger entries for the input-focus leak class:

- **B-196** (`bugs.md:148`): WASD/Space stuck after menu close. **FIXED -- PLAYTEST CONFIRMED 2026-04-19** (`brave-bouman-13bd68`). Introduced `pdguiClearImGuiFocusAndNav()` helper in `port/fast3d/pdgui_backend.cpp` + `imguiMenuOnPop` invocation in `port/src/inputctx.c`. Diagnostic warning retains the historical "B-195" tag.
- **B-302** (`bugs.md:39`): Grid editor toggle visibility + focus leak. **FIXED-PENDING-PLAYTEST 2026-05-01** (S594, `infallible-mestorf-8463b9`). Promoted `s_SidebarVisible` -> `s_EditorVisible`, gated the entire `pdguiForgeEditorRender` body, added defensive `pdguiClearImGuiFocusAndNav()` calls in `forgeTransitionToNormal` / `forgeTransitionToInactive`.

`port/src/inputctx.c:917-933` does introduce `g_CtxForgeEditor` (hybrid context with visible cursor + ImGui kbd claim but gameplay-axis passthrough); comment may reference "B-195 architectural close" as a historical tag.

Both B-196 and B-302 are correctly labeled in the ledger. **No drift.** Severity: LOW.

### 1.8 c086 + c081 cards

**c086** (state.json) "B-317 follow-up: per-element CS bindings + left-panel SkipUp/SkipDown walker": `column=backlog`, `priority=3`. Foundation landed via c084 + c089 (Q-A/Q-B/Q-C/Q4 spec resolutions, ACTION_MENU_SKIPUP/SKIPDOWN, LT/RT bindings, page-jump fallback) -- those two are DONE. c086 itself covers every per-element binding: per-row X context popups, left-panel SkipUp/SkipDown walker, Start jump-to-Start-Match, A+B convergence on Back-to-Menu, CC2 NavFlattened theme-editor audit, CC5 shared popup builder, Rule 10 skip-empty traversal validation, test_actionmap pin. **Status: foundation shipped, downstream implementation 0% complete.**

**c081** (state.json) "Queue Match feature": `column=backlog`, `priority=4`. Stores array of match-start settings, cycles as matches end. Includes pause-menu "end game"/"skip match", UI badge for queued count, multi-match state machine, queue-config persistence, possible `SVC_QUEUE_STATE` net message. **Status: design-only, no implementation; deferred until c086 + universal grammar conformance.**

### 1.9 Net assessment for Input pillar

Pillar is in good shape. Biggest open lanes:
1. Menu graph completion (75 raw sites; multi-session).
2. `gameplayInputSuppressed()` transitional wrapper retirement (awaiting playtest verification).

One ledger hygiene item: B-298 closed in code but still labeled OPEN in `bugs.md`. (Audit-v1 incorrectly added B-195; that was a bug-id confusion -- corrected in section 1.7.)

---

## Track 2 -- Context system incompleteness findings

### 2.1 context/README.md staleness -- HIGH

`context/README.md` was last touched 2026-05-01; today is 2026-05-13. "Live as of 2026-04-30" header at line 3 is ~13 days stale. The "Live state at a glance" block at lines 29-36 is materially wrong against current code:

| Claim (README.md) | Live code | Drift |
|---|---|---|
| Wire protocol v46 (line 31) | `NET_PROTOCOL_VER 47` in `port/include/net/net.h:12` | HIGH -- v47 shipped 2026-05-01 |
| Build v0.0.175+ (line 33) | `VERSION_SEM_PATCH 197` in `CMakeLists.txt:129` | HIGH -- 22 patch revs behind |
| SAVE_VERSION=2, MPSETUP_VERSION=2 (line 32) | `SAVE_VERSION 2` in `port/include/savefile.h:41`; `MPSETUP_VERSION 2` in `port/include/mpsetups.h:19` | OK |
| pillars/save-wire-format.md says NET_PROTOCOL_VER=46 | live=47 | HIGH -- pillar doc also stale |
| "Critical path: Catalog Weapons F11-F13 ... then Catalog Gate 3 ... then Input Controller Support" (line 35) | tasks.md shows F11-F13 closed 2026-04-30, Gate 3 closed 2026-05-02, Catalog Universality COMPLETE 2026-05-03; current critical path is c036 menu-graph migration | HIGH -- sentence obsolete |

### 2.2 Pillar doc coverage gaps -- MED

All 11 named pillars exist as files. Last-touched dates:

- `catalog.md` 2026-05-03 -- fresh
- `build-dev-tooling.md` 2026-05-11 -- fresh
- `input.md` 2026-04-30 -- **13d stale** (cohorts 5-8 shipped 2026-05-12, c036 lane active)
- `menus.md` 2026-04-30 -- **13d stale** (s036-08 migrations 2026-05-12/13)
- `modding.md` 2026-04-30 -- **13d stale**
- `connectivity.md` 2026-04-30 -- **13d stale**
- `save-wire-format.md` 2026-04-30 -- **13d stale**; still asserts NET_PROTOCOL_VER=46
- `server.md` 2026-04-30 -- **13d stale**
- `tests.md` 2026-04-30 -- **13d stale**
- `rendering.md` 2026-04-30 -- **13d stale**
- `physics-collision.md` 2026-04-30 -- **13d stale**

Nine of eleven pillar docs frozen at the 2026-04-30 rebuild; the project shipped 22 patch revs and several major arcs (Catalog Universality, BYOR Completion, Startup Acceleration Phases 4-5, Smoke Verify Gate, Dev Window v2, Daily-Flow Orchestrator, Decision-Request) without touching pillar docs. Retention rule "Update with the code... pillar docs do not get separate maintenance passes; they are part of the change" is being violated systemically.

### 2.3 Active audits beyond retention window -- MED

`context/retention.md:15` sets a 14-day window for `audits/` (today=2026-05-13, cutoff=2026-04-29). 19 audits have authorship dates older than 14 days:

- `flat-menu-navigation-audit-2026-04-25.md`
- `pdmod-verification-matrix-2026-04-25.md`
- `post-implementation-audit-2026-04-25.md`
- `sp-stage-mp-readiness-2026-04-24.md`
- `catalog-universality-sweep-2026-04-27.md`
- `codebase-architecture-rating-2026-04-27.md`
- `infrastructure-pillars-status-2026-04-27.md`
- `rom-extraction-audit-2026-04-30.md`
- `ui-asset-pipeline-investigation-2026-04-30.md`
- `2026-05-01-full.md`
- `catalog-universality-sweep-2026-05-01.md`
- `catalog-gate3-heads-data-2026-05-01.md`
- `catalog-coverage-audit-2026-05-01.md`
- `input-menu-system-audit-2026-05-01.md`

Most should move to `_old/audits/2026/` per retention.md:31 unless cited from `tasks.md` or an active design. tasks.md:316 cites `infrastructure-pillars-status-2026-04-27.md` (keep). Most others have no exemption.

### 2.4 Designs in-flight that have shipped -- MED

Six `context/designs/in-flight/` docs. Two carry SHIPPED/IN-PROGRESS markers:

- `skedar-surface-normal-locomotion.md:3` -- "Status: IN-PROGRESS. Slices 1, 2, 3 SHIPPED 2026-05-01 PM". Slices already on wire (v47 ships surface-normal locomotion). Candidate for retention sweep once slice arc closes.
- `gpu-swarm-bot-pipeline.md` and `gpu-swarm-and-test-scenarios.md` -- Phase 1 design SHIPPED per bugs.md B-311; gated on Mike per tasks.md.
- `player-init-architectural-fixes.md:2` -- "status: draft -- pending Mike's greenlight". 13 days, no movement.
- `testing-framework.md` -- README.md:104 says "Cohorts 1-2 shipped, Cohort 3 deferred"; mostly-shipped. Could move to `_old/designs-shipped/tests/`.
- `issue-10-rigging-aware-body-head-linkage.md:3` -- "SCOPED, NOT IMPLEMENTED" 2026-04-24, 19 days of zero activity.

### 2.5 session-log.md size and tier policy -- LOW

`wc -l context/session-log.md` = 8225 lines, 179 `## Session` headings. retention.md:18 caps at "last ~100 sessions". Earliest visible session header is S481 from 2026-04-27; newest is 2026-05-13. File is **79% over the active rolling window**; retention.md:34 says oldest ~50 are cut to a new tier in `_old/session-log/` when window passes ~120. Cut is overdue.

### 2.6 bugs.md vs systemic-bugs.md drift -- HIGH

`context/bugs.md` is 273 lines, 99 entries flagged `FIXED-PENDING-PLAYTEST`. **Zero entries with status `FIXED <date> commit-SHA`** in the visible file -- the "compact reference below with commit SHAs" promised at line 11 does not exist; everything is FIXED-PENDING-PLAYTEST. tasks.md:306 says "B-280 through B-290 are all FIXED-PENDING-BUILD as of S575" -- not promoted since 2026-04-28. Examples >30d in pending state:

- B-208 (FIXED-PENDING-PLAYTEST 2026-04-20)
- B-210, B-214, B-215, B-220 (2026-04-20/21)
- B-167 through B-178 (2026-04-18/19)

Older still-open per tasks.md:310: **B-179, B-182, B-183** ("modeldef defensive guards landed S312; root-cause fix not yet identified") -- open without verify command since before 2026-04-13.

### 2.7 constraints.md removed-constraints drift -- MED

Spot-checked 5 active entries against code:

1. **MPSETUP_VERSION=2** (line 15) -- `port/include/mpsetups.h:19` -- OK.
2. **NET_PROTOCOL_VER v46** (line 21) -- live is **v47**. The constraint body describes v46 features as current; v47 (surface-normal locomotion) shipped 2026-05-01 and is undocumented in constraints.md -- STALE.
3. **DEFAULT_BASEDIR_NAME** -- not in constraints.md; the "ROM data files" entry (line 27) doesn't note the 2026-05-03 flat-install change. `port/src/fs.c:41` confirms `"."`. Indirect drift.
4. **SPAWNPOOL_CAPSULE_RADIUS=30.0f** (line 58) -- `src/game/spawnpool.c:302` uses the constant; OK.
5. **`manifestClear` before `mainChangeToStage`** (line 60) -- file:line reference is **broken**: cites `pdgui_bridge.c:799` which no longer exists in the tree (function moved to `port/fast3d/pdgui_menu_mpingame.cpp`). Also cites `netmsg.c:1419` -- that function exists at `port/src/net/netmsg.c:6114, 6119`. Line numbers stale.

### 2.8 tasks.md staleness -- LOW

tasks.md was touched 2026-05-13 (today). Active critical-path lanes (catalog F11-F13, Gate 3, Universality Pivot, BYOR Completion, post-pivot triage, B-323, Walker reorder, Smoke Verify Gate, Dev Window v2, Daily-Flow Orchestrator, Decision-Request) all show SHIPPED with dates 2026-04-30 through 2026-05-12. Disclaimer at line 5 says "If a lane has stalled for > 14 days, ask whether it is genuinely active." Lane 3 (s036-08 menu graph completion) is the only live lane and is being worked. **Build/Test/Modding/Connectivity/Save follow-up sections** (lines 314-374) have not changed since at least 2026-04-27/30 audit cites. Modding follow-ups cite the same 4 known gaps for 16+ days.

### 2.9 MEMORY.md staleness -- MED

`C:\Users\mikeh\.claude\projects\C--Users-mikeh-Perfect-Dark-2-perfect-dark-mike\memory\MEMORY.md` line 7 `project_status.md` summary says "(last: 2026-05-03 Engine Startup Phases 4 + 5 SHIPPED ... STARTUP-ACCELERATION ARC COMPLETE...)". 10 days behind today's c036 menu-graph slice and the Smoke Verify Gate Phase 1 ship (2026-05-11), Daily-Flow Orchestrator (2026-05-11), Decision-Request Mechanism (2026-05-12), Dev Window v2 CLI panel + refinements (2026-05-12), boot black-screen deadlock fix (commit 3529116f 2026-05-13). MEMORY index lists no entries for Smoke Verify Gate, Daily-Flow Orchestrator, Decision-Request Mechanism, or sprint-report contract.

### 2.10 Sprint reports archive flow -- HIGH

`.claude/sprint-reports/` (active) contains 9 .md files; `.claude/sprint-reports/archive/` does not exist (Glob returned no files). Orchestrator contract (tasks.md:218) says reports move to `archive/` once verified.

By filename / mtime (today=2026-05-13):

| File | mtime | Age |
|---|---|---|
| `sprint-c125-demo.md` | 2026-05-12 15:13 | 1d (demo, may be exempt) |
| `sprint-c126-hooks.md` | 2026-05-12 15:41 | 1d |
| `sprint-c127-cli-refinements.md` | 2026-05-12 16:03 | 1d |
| `sprint-2026-05-12T204110.md` | 2026-05-12 17:39 | 1d |
| `sprint-2026-05-12T213933.md` | 2026-05-12 17:40 | 1d |
| `sprint-2026-05-13T134125.md` | 2026-05-13 09:42 | <1d |
| `sprint-2026-05-13T152938.md` | 2026-05-13 11:30 | <1d |
| `sprint-2026-05-13T175619.md` | 2026-05-13 13:57 | <1d |
| `sprint-2026-05-14T013236.md` | 2026-05-13 21:35 | <1d |

No report >7d old. But: **archive directory itself doesn't exist** -- the Daily-Flow Orchestrator launcher (tasks.md:218 "creates both directories on startup") either hasn't run a "verified + archive" pass yet or the path predicate is wrong. HIGH because if the orchestrator runs against missing archive dir, reports accumulate silently.

### 2.11 Cross-cutting summary for Context

**Pattern: context root files (README, constraints, pillars) froze at the 2026-04-30 rebuild commit and have not been kept in sync with the post-2026-05-01 ship arc.** session-log.md and tasks.md (touched every slice) are current; everything that requires a "promotion" step (pillar live-state, constraints constants, bugs FIXED→commit-SHA, audit retention sweep, in-flight design retire, sprint archive move) is behind. The fix is a single retention pass.

---

## Track 3 -- File extraction + archive creation + external accessibility findings

### 3.1 Per-kind accessibility table

Of 13 emitter outputs, **4 are plain JSON** (modder-editable in any text editor) and **9 are ZIP envelopes carrying `manifest.json` plus binary payloads that are RAW N64 ROM bytes** (geometry display lists, ALADPCM samples, anim frames, font segments, lang banks, stage segments). The single exception that ships decoded payload is `.pdui`, which writes uncompressed 32-bit TGA.

| `pd_kind` | Container | Payload | External accessibility | Severity |
|---|---|---|---|---|
| `weapon` (.pdwpn) | JSON only | n/a (text record) | ACCESSIBLE -- human-readable JSON with symbolic refs (`FILE_GFALCON2`, `ANIM_GUN_*`, `SFX_*`) (`port/src/romextract_pdwpn.c:497-545`) | LOW |
| `head` (.pdhead) | JSON only | n/a | ACCESSIBLE -- plain JSON with symbolic mesh ref (`port/src/romextract_pdhead.c:75-89`) | LOW |
| `body` (.pdbody) | JSON only | n/a | ACCESSIBLE -- plain JSON with mesh+hand symbolic refs (`port/src/romextract_pdbody.c:81-101`) | LOW |
| `arena` (.pdarena) | JSON only | n/a | ACCESSIBLE -- plain JSON, references a `.pdscenario` by catalog ID (`port/src/romextract_pdarena.c:182-196`) | LOW |
| `animation/weapon` (.pdanim) | JSON only | mnemonic opcode arrays | ACCESSIBLE -- JSON with mnemonic opcodes (`port/src/romextract_pdanim.c:53-66`); modders can hand-edit | LOW |
| `mesh` (.pdmesh) | ZIP | `geometry.bin` = RAW N64 model bytes (display lists + Vtx tables) | OPAQUE -- byte-for-byte N64 model file; no decoded vertices/UVs/triangles; no documented decoder ships (`port/src/romextract_pdmesh.c:155-211`) | **HIGH** |
| `animation/chr` (.pdanim) | ZIP | `frames.bin` = packed N64 frame bytes | OPAQUE -- raw frame stream; metadata gives byte counts but no decoder ships (`port/src/romextract_pdanim_chr.c:181-247`) | **HIGH** |
| `sfx` (.pdsfx) | ZIP | `sample.bin` = ALADPCM or PCM16 raw | PARTIAL -- ALADPCM is N64-specific; PCM16 raw is replayable with effort; no WAV header; needs PD-specific decoder + ALADPCMBook (`port/src/romextract_pdsfx.c:248-478`) | **HIGH** |
| `voice` (.pdvoice) | ZIP | `sample.bin` = ALADPCM/PCM16 raw | PARTIAL -- same envelope as SFX with actor/transcript placeholder fields | **HIGH** |
| `song` (.pdsong) | ZIP | `data.bin` = compressed or raw N64 ALSEQ stream | OPAQUE -- ALSEQ binary; manifest distinguishes ALSEQ vs ALSEQ_ZIP but no PC-side decoder ships | **HIGH** |
| `scenario` (.pdscenario) | ZIP | `geometry.bin` + `tiles.bin` + `pads.bin` + `setup.bin` + `mpsetup.bin` -- all RAW N64 stage segments | OPAQUE -- all five payloads are raw N64 bytes (`port/src/romextract_pdarena.c:242-280`); manifest gives names + sha256 sidecars only | **HIGH** |
| `ui` (.pdui) | ZIP | `texture.tga` = decoded RGBA32 top-down TGA | ACCESSIBLE -- 32-bit uncompressed TGA per `port/fast3d/pdgui_theme.cpp:2411-2431`. Only 14 textures (`k_PduiEntries[]` at `pdgui_theme.cpp:1832-1847`). Editable in GIMP/Photoshop/Krita | LOW (for the 14 covered) |
| `font` (.pdfont) | ZIP | `data.bin` = raw N64 font segment (pre-preprocessFont) | OPAQUE -- raw font bytes including N64-encoded glyph atlases (`port/src/romextract_pdfont.c:115-198`) | MED |
| `lang` (.pdlang) | ZIP | `data.bin` = raw N64 lang bank pre-preprocessLangFile | PARTIAL -- binary with documented byte-swap step; round-trippable but not human-readable; English-only on NTSC ship; PAL/JPN deferred (`port/src/romextract_pdlang.c:67-263, 320-324`) | MED |

### 3.2 Mesh UVs accessibility -- HIGH

`port/src/romextract_pdmesh.c:155-211` writes the ZIP as `manifest.json` + a literal byte-copy of `data/<romid>/files/<sanitized_rom_name>.bin` named `geometry.bin`. The manifest carries no vertex count, triangle count, UV stride, materials, or attach points. Schema 2.5 (`context/designs/catalog/universality-pivot-schemas.md:305-340`) lists `materials`, `attach_points`, `skeleton`, `collision` as optional fields, comment says "today empty". The walker (`port/src/loader_walker_mesh.c:23-32`) ignores the manifest contents and only registers a catalog row -- it does not decode the geometry. A modder cannot drop a `.pdmesh` into Blender without writing a custom N64 GBI display-list parser plus the model-node graph at `MODELNODETYPE_DL` / `MODELNODETYPE_GUNDL` / `MODELNODETYPE_BBOX`. No documented decoder ships in `tools/` or `devtools/`.

### 3.3 Animation accessibility -- HIGH

`port/src/romextract_pdanim_chr.c:181-247` writes `manifest.json` + `frames.bin` (byte slice from the in-memory animations segment, `headerlen + numframes * bytesperframe`). Manifest carries `frame_count`, `bytes_per_frame`, `header_len`, `framelen`, `flags`, `source_offset`. Frame bytes themselves are the N64-packed compressed form parsed at runtime by `src/lib/anim.c::animLoadFrame`. No documented decoder or sample-export tool. Modders cannot inspect bone transforms; they can only swap blob-for-blob.

### 3.4 Audio accessibility -- HIGH

`port/src/romextract_pdsfx.c:248-478` writes `sample.bin` as the raw byte range from `sfxtbl` segment. Manifest carries `"format": "ALADPCM"` or `"format": "PCM16"`. ALADPCM requires the N64 codebook (`ALADPCMBook`) at decode time (`wt->waveInfo.adpcmWave.book` at `:284`) plus an ALADPCM decoder. **The codebook is NOT in the .pdsfx ZIP** -- the manifest does NOT carry book/predictor coefficients. Even a PCM16 stream lacks a WAV header. No `tools/` script extracts the codebook or wraps `sample.bin` as `.wav`. Audacity cannot open `sample.bin` as-is. `port/src/preprocess/segaudio.c:117-138` shows the engine-side ALADPCMBook layout but lives in the build pipeline, not modder-facing.

### 3.5 Texture accessibility -- HIGH for non-UI

Only 14 UI textures (`k_PduiEntries[]`) are decoded to TGA. Per-weapon, per-character, per-stage textures are embedded inside `.pdmesh` and `.pdscenario` `geometry.bin` payloads as N64 RGBA16 / IA8 / IA4 / CI8 byte streams referenced by GBI `setTImg` commands. **No `.pdtex` emitter exists.** The `pdgui_theme.cpp` `decodeRgba16/decodeIa16/decodeIa8/decodeIa4` decoders at `:2480-2495` are used ONLY for those 14 UI textures. The legacy texture-replacement layer (`textures/0049.bin` hex-keyed CI8/RGBA16 dumps -- `docs/MOD_CONVERSION_GUIDE.md:31-34`) is the prior modder workflow but has no universality-pivot equivalent.

### 3.6 Documentation for modders -- HIGH gap

- `docs/MOD_CONVERSION_GUIDE.md` (2026-03-23) covers the LEGACY monolithic mod system. Zero hits for `.pdwpn`, `.pdmesh`, `.pdanim`, `pd_kind`.
- `docs/MOD_LOADER_PLAN.md` (pre-pivot) covers `mod.json` schema but not per-asset `.pd<ext>` shapes.
- `context/designs/catalog/universality-pivot-schemas.md` is the de facto spec but lives in `context/` (developer-internal). Not user-facing.
- `context/pillars/modding.md:23-31` documents `.pdmod` format and references modarchive.h.
- `tools/pdmod_prophandler/README.md` -- shell extension only.
- No README in `mods/` or `data/` walks a modder through editing a `.pdmesh` payload.

### 3.7 Round-trip authoring -- MED

Walker code (`loader_walker_mesh.c:23-32`, `loader_walker_anim.c:26-69`) resolves catalog rows from manifests but does NOT consume the binary payloads inside ZIPs (mesh `geometry.bin`, anim chr `frames.bin`). The runtime still loads those via legacy file-load paths (comment at `loader_walker_mesh.c:10-13`: "geometry.bin payload remains the source of record on disk"). A modder who edits the manifest but cannot regenerate matching `geometry.bin` cannot actually swap mesh data. SHA-256 sidecars (`geometry.bin.sha256`) are written but the self-heal at Pass D (per `tests/test_romextract_passd.cpp`) only validates the byte payload against the digest -- it does not validate manifest-payload consistency. No live test confirms round-trip of edited `.pdmesh` payload through walker registration back to render.

### 3.8 Archive creation entry points -- MED gap

Three writers exist:

- `port/src/modpack_pdmod.c:136-174` `modpackPdmodWriteSingle` -- writes one component bundle.
- `port/src/modpack_pdmod.c:248+` `modpackPdmodFromFolder` -- takes a folder, makes a `.pdmod`. Called ONLY by `port/src/modmigrate.c:158` (legacy folder-to-pdmod auto-migration).
- `port/src/modarchive.c:685-925` low-level ZIP writer.

In-game UI:
- "Mod Pack" tool at `port/fast3d/pdgui_menu_moddinghub.cpp:1010-1131` writes **`.pdpack`** (PDPK container, NOT `.pdmod`) via `modpackExport`. Different format with `PDPK` magic + INI manifest, not a zip.
- Theme editor (`pdgui_menu_theme_editor.cpp:325, 446`) calls `modpackPdmodWriteSingle` to write theme `.pdmod` files only.
- Skin Editor, Audio Mod, Map Import, Font Mod tabs (`pdgui_menu_moddinghub.cpp:1340-1342`) -- no skin/audio → `.pdmod` pipeline surfaced.

**There is NO user-callable "pack a folder of `.pdmesh` + `.pdwpn` + `mod.json` into a `.pdmod`" tool in the UI.** The `modpackPdmodFromFolder` helper is reachable only via the auto-migration path. CLI tooling: none. Modders must rely on standard zip tools (7-Zip can produce a valid `.pdmod` since it's deflate zip), but the defensive zip-comment mirror at `modpack_pdmod.c:108-132` (writes JSON name/creator/version into EOCD comment) will be absent in hand-zipped archives -- Property Handler DLL won't surface fields.

### 3.9 Incomplete emitter kinds -- MED (was HIGH, mitigated)

B-318 (post-pivot triage 2026-05-03): original universality pivot had a chicken-and-egg deadlock; pool-dependent emitters early-returned on `loaderPoolIsActive()`. **Fixed 2026-05-03 (gallant-booth-6f996f)**: gates removed, emitters drive directly from authoring tables. B-320 closed the audio parent-dir Windows bug. State: "fix-pending-verification".

Outstanding emitter holes:

- **pdvoice**: per `tools/bugs/state.json:70` note, `pdvoice skipped=1545` (every sound classified is_voice=0). The c106 fix in `romextract_pdsfx.c:218-241` unpacks `union soundnumhack`. Marked fix-pending-verification.
- **pdlang**: NTSC English only on initial ship (`romextract_pdlang.c:320-324`). PAL/JPN locales deferred.
- **pdscenario**: arenas with no stagetable entry (Random meta arenas) skip the scenario emit (`romextract_pdarena.c:205-211`).
- **pdmesh material/skeleton/collision fields**: schema 2.5 reserves these as "today empty".
- **per-stage textures, props, AI scripts**: not in the 13 lockdown kinds. No `.pdtex`, `.pdprop`, `.pdai` exists. AI lists are documented at `docs/ailists.md` but not emitted.

### 3.10 External user pain points -- HIGH

Realistic workflow "I want to replace the Falcon's mesh with a custom Blender model in a `.pdmod`":

1. **BYOR step works**: place ROM at install root, boot game, watch logs for `romextract pdmesh: written=N`. Locate `data/<romid>/meshes/base_falcon2_hi.pdmesh`.
2. **Open the .pdmesh with 7-Zip**: works (deflate zip). See `manifest.json` (parseable JSON) and `geometry.bin` + `geometry.bin.sha256`.
3. **BLOCKER 1**: `geometry.bin` is raw N64 GBI display lists referencing texture banks by relative offset, with `Vtx` records inlined. No PC-side `.gltf` / `.obj` / `.fbx` exporter ships. The schema's `materials`/`attach_points`/`skeleton` manifest fields are documented as "today empty".
4. **BLOCKER 2**: To author a new mesh, a modder must produce a byte-identical N64 GBI display list. `tools/assetmgr/` Python scripts target the build-time ROM-rebuild pipeline -- they're not for runtime `.pdmesh` authoring.
5. **BLOCKER 3**: No `.pdtex` kind. Texture replacement still requires legacy `textures/<hex>.bin` flow, incompatible with the universality-pivot taxonomy. Skin Editor writes to legacy texture path, not `.pdmesh`-embedded textures.
6. **BLOCKER 4 (.pdmod packaging, partially superseded 2026-05-20)**: After authoring, no UI tool packs loose `.pdwpn` + `.pdmesh` files into a `.pdmod`. Modder must hand-write `mod.json` and use 7-Zip -- but they will miss the EOCD comment mirror, so Windows Explorer Property Handler won't surface fields. The `modpackExport` UI writes `.pdpack` (different format), not `.pdmod`.
7. **BLOCKER 5 (load path, superseded 2026-05-20)**: Even with a correctly packed `.pdmod`, `.pdmesh` files inside archive mods may not register. Historical note: the old scanner used `fopen / stat / opendir`, expecting on-disk files; archive-mounted `.pdmesh` files would not appear in the catalog.

**2026-05-20 update**: Blocker 5 is resolved by the c3809 archive scanner/VFS path, including typed `*.pdxxx` descriptors inside `.pdmod` transport archives. Blocker 4 is partially resolved by `modpackPdmodFromFolder()` validation and packaging; the remaining gate is runtime Public Mods transfer/install behavior.

**Effective state**: a modder can replace `.pdwpn` JSON (stats, animation refs, lang IDs) end-to-end with a text editor and zip tool. They can replace `.pdarena` JSON. They can replace the 14 `.pdui` TGA textures. **They cannot meaningfully edit mesh geometry, character animations, raw audio samples, songs, fonts, lang strings, or stage geometry** without writing a custom N64 codec on the side. The "universality" extraction is structurally complete (13 of 13 kinds emit files); the "accessibility" promise is fulfilled only for 4-5 of the 13.

---

## Track 4 -- Jump collision system findings

### 4.1 Jump system entry point

Input flow:

- Button capture (network input apply): `src/game/bondmove.c:214-221` -- when `UCMD_JUMP` arrives, sets `pl->wantsjump = true` if `jumpconsumed` is false.
- Local PC input fallback: `src/game/bondmove.c:2121-2141` -- `c1buttonsthisframe & BUTTON_JUMP` sets `wantsjump`.
- Jump impulse application: `src/game/bondwalk.c:902-970` (function `bwalkUpdateVertical` at `bondwalk.c:849`). Applies `FIXED_JUMP_IMPULSE 8.2f` to `bdeltapos.y`.
- Airborne integration / gravity / landing: same `bwalkUpdateVertical`, the orchestrator (lines 849-1600+).
- Upward step movement: `bwalkTryMoveUpwards()` `src/game/bondwalk.c:258`.

### 4.2 Jump collision query -- actual call path

The jump uses a **hybrid stack of legacy primitives + a thin "swept" wrapper**, NOT a full geometric test.

For each frame of vertical motion the path is:

1. `bwalkUpdateVertical` (`bondwalk.c:849`) computes `verticalDelta`.
2. **Pre-move capsule sweep** `bondwalk.c:1241-1280` calls `capsuleSweep(&sweep)` at `bondwalk.c:1255`.
3. `capsuleSweep` (`src/lib/capsule.c:37`) internally iterates 16 sample positions and **calls `cdTestVolume(...)` at each** (`capsule.c:73`). It is a stepped sampling wrapper around the legacy primitive.
4. **Pre-move ceiling probe** `bondwalk.c:1290-1342` -- 5 samples of `cdFindCeilingRoomYColourFlagsAtPos`.
5. Final translate via `bwalkTryMoveUpwards()` -> single `cdTestVolume(...)` call (`bondwalk.c:288`).

`cdTestVolume` is defined `src/lib/collision.c:2402`. It collects only `GEOFLAG_WALL` (line 2407) -- so the test substrate is **wall-flagged BG tiles + AABB/cylinder prop blocks + chr cylinders**, not the actual rendered triangles.

Ground re-acquisition uses `cdFindGroundInfoAtCyl` (`bondwalk.c:1001`), which collects only `GEOFLAG_FLOOR1|FLOOR2` (`collision.c:2201`).

Capsule fallback (`capsuleFindFloor` at `capsule.c:135`) is also a binary search using `cdTestVolume` -- same wall-only substrate.

**The jump never touches `bgTestHitInRoom` (the renderer geometry).** Severity HIGH (wall-jump glitch class is explainable from this gap).

### 4.3 Wall-jump glitch root cause -- status

- **No B-NNN entry exists in `context/bugs.md`.** Propagation check across `bugs.md`, `tasks.md`, and `parked-and-bugs.md` finds no diagnosed root cause.
- Only formal tracking is **kanban card `c038`** in `tools/kanban/state.json:1065-1078`: "Vehicles: wall-jump glitch (deferred)", pillar = vehicles (mis-categorised; this is physics/collision), priority 5, column = backlog. Description: "Wall-jump glitch: deferred, not on critical path."
- Session reference: `context/session-log.md:3308, 3352` (S594, 2026-05-01) -- noted "deferred per Mike", untouched.

**Status: OPEN, undiagnosed, mis-pillared.** Severity MED (not a crash but player-visible exploit).

### 4.4 Laptop Gun's collision function

The Laptop Gun is `WEAPON_LAPTOPGUN`. Placement = throw-then-stick.

- Throw entry: `bgunCreateThrownProjectile2` -> `laptopDeploy(modelnum, gset, chr)` at `src/game/propobj.c:18919`. Allocates the prop -- no placement validation here.
- Launch sets `PROJECTILEFLAG_AIRBORNE` and calls `projectileSetSticky(prop)` at `src/game/bondgun.c:4764` and `src/game/propobj.c:1163-1177`, flipping `PROJECTILEFLAG_STICKY`.
- Per-tick projectile motion: `src/game/propobj.c:7195-7199` branches on `PROJECTILEFLAG_STICKY`:
  - **Sticky path (laptop): `cdresult = func0f06cd00(obj, &sp5dc, &sp5e8, &sp5f4)` at `propobj.c:3530`.**
  - Non-sticky path: `func0f06d37c` at `propobj.c:3675`.
- `func0f06cd00` (`propobj.c:3575`) calls `bgTestHitInRoom(&prop->pos, &sp1c4, spcc[i], &hitthing)` per traversed room.
- `bgTestHitInRoom` (`src/game/bg.c:4471`) walks `g_Rooms[roomnum].vtxbatches[]` (`bg.c:4512`) and calls `bgTestHitInVtxBatch` (`bg.c:4162`), which iterates GBI `G_TRI1`/`G_TRI4` commands and tests the actual rendered triangles (`bg.c:4209-4262`). `vtxbatches` are populated from BG `G_VTX` display lists in `bgPopulateVtxBatchType` (`bg.c:3295`).

**The Laptop Gun's stick query is per-triangle against the actually-rendered BG display lists.** It also falls back to `cdExamLos09` for unloaded rooms (`propobj.c:3597`), and prop collisions via `projectileFindCollidingProp` at `propobj.c:3629`. Note `propobj.c:7222-7226`: "Thrown laptops can stick to the BG but not props" -- sticky filter is intentional.

### 4.5 Comparison -- the coverage gap

| Surface | Jump (bwalkUpdateVertical) | Laptop (sticky projectile) |
|---|---|---|
| BG tiles (`geotilei` floors, walls) | YES via `cdTestVolume` + `cdFindGroundInfoAtCyl` -- **flagged geometry only** | YES, walks actual rendered triangles |
| Prop AABBs / float floors | YES via `cdTestVolume` for `GEOFLAG_WALL`-flagged blocks | NO -- props excluded from stick (`propobj.c:7222-7226`) |
| Chr cylinders | YES via `cdTestVolume` (`CDTYPE_ALL`) | NO (filtered) |
| **Rendered display-list triangles** | **NO** | **YES (`bgTestHitInRoom`)** |
| Display-list geometry NOT in `vtxbatches` (`G_VTX` blocks only) | Never queried | Per-triangle covered |

**The laptop is the only system that operates on Mike's "full normal rendered geometry."** The jump operates on the simpler **flagged-tile + AABB collider substrate**. This is the gap: any geometry rendered as a triangle but authored without a matching `GEOFLAG_WALL` tile (missing or thin collision skin around a visual mesh) is **invisible to `cdTestVolume`** -- the player capsule sweep walks through it -- but **visible to `bgTestHitInRoom`** because that function walks the actual `G_TRI1`/`G_TRI4` triangles.

Severity HIGH for explaining the wall-jump glitch. Mike's instinct (option b: "use the Laptop Gun's colliders") is exactly correct -- the laptop's `bgTestHitInRoom` is the renderer-faithful per-triangle test.

### 4.6 Capsule sweep coverage (`src/lib/capsule.c`)

`capsuleSweep` (line 37) uses **only `cdTestVolume`** (line 73). Through `cdCollectGeoForCyl`:

Covered:
- BG `geotilei` / `geotilef` tiles flagged `GEOFLAG_WALL`.
- Prop `geoblock` AABBs flagged `GEOFLAG_WALL`.
- Prop auto-generated floor tiles (PC extension, `propobj.c::objBuildTopFaceTile`).
- Other characters' `geocyl` cylinders (when `cdtypes & CDTYPE_CHR`).

NOT covered:
- **Rendered display-list triangles** with no matching `GEOFLAG_WALL` tile.
- `GEOFLAG_SLOPE` and `GEOFLAG_BLOCK_SHOOT`-only surfaces are also filtered out.
- Doors / lifts / breakable props that lack a `WALL`-flagged collision geometry.
- Pillar doc (`physics-collision.md:94, 101`) confirms two known gaps: **slope-AABB adaptation** and **ceiling-jump-through** are deferred.

Also missing: the `hitnormal` returned by capsule sweep at `capsule.c:122-124` is **just the negated movement direction** ("approximate" per the comment at line 113-118). It is not a real surface normal -- `cdTestVolume` does not provide one. Load-bearing limitation for any future wall-slide / surface-normal-aware logic (e.g. Skedar surface locomotion).

Severity HIGH.

### 4.7 Surface-normal jump interaction (c3738 Skedar)

The jump system assumes **world-Y gravity everywhere**:

- `bondwalk.c:907` `FIXED_JUMP_IMPULSE 8.2f` is applied directly to `bdeltapos.y`.
- `bondwalk.c:1226-1229` gravity integrates the Y delta with no reference to `surface_up`.
- The capsule sweep takes `sweep.move.y = verticalDelta` (`bondwalk.c:1250`) and classifies floor/ceiling by Y component (`capsule.c:94-110`).

Per `session-log.md:3099-3120` (S594h-B Slice 3, 2026-05-01), Slices 4 (aim) and 5 (gravity flip + wall transitions + scary-jump + landing-normal) are **deferred**. Slice 5 specifically would replace world-Y gravity with `surface_up` gravity for surface-loco chrs. Until Slice 5 lands, a Skedar wall-walking cannot jump perpendicular to a wall -- the jump impulse goes world-up, not surface-up.

Severity MED (gated behind a feature that itself is deferred).

### 4.8 Open jump-related TODOs / FIXMEs / B-NNNs

- **No TODO/FIXME/XXX comments** in `capsule.c` or `bondwalk.c` (clean code, but absence is also a sign of undocumented gaps -- wall-jump glitch is in-code but uncommented).
- `bondwalk.c:1054-1077`: PC capsule fallback explicitly skips during strong upward jump motion ("Only skip during strong upward movement (jump ascent)") -- conscious gap; jump ascent does not get the prop-surface probe.
- `bondwalk.c:1282-1289` comment: "the capsule sweep only tests WALL geometry (via cdTestVolume). FLOOR1|FLOOR2-only ceiling surfaces are invisible to it." Acknowledged limitation.
- `bondwalk.c:1011-1024` (B-49 reference): documents a prior B-49 bug where `cdFindGroundInfoAtCyl` missed `GEOFLAG_WALL`-only prop floors. Fix was a `cdTestVolume` probe -- still inside the same flagged-tile-only substrate.
- `physics-collision.md:90-103`: D2c bot-jump AI not started, slope-AABB deferred, ceiling-jump-through deferred.
- `roadmap.md:80, 437, 930`: capsule polish has unknown depth.
- Kanban `c038`: wall-jump glitch deferred, no B-ID, mis-pillared as "vehicles".
- `bugs.md` B-134: related class -- railing fell inside capsule radius. Spawn pool uses capsule radius now; jump still has the gap.

Severity MED.

### 4.9 Modernization opportunity -- swap to laptop-style query

What it would take:

1. **API mismatch.** `bgTestHitInRoom` is a **ray test** (point A to point B, returning a single hit). The capsule sweep is a **volumetric** sweep. To use the laptop's geometry, the call site needs N rays per sweep step (one per capsule "skin" point) -- e.g. center + 4 lateral offsets at top/mid/bottom = 15 rays per step * 16 steps = 240 ray casts per frame for a single jump. Modern hardware: trivially cheap (`bgTestHitInVtxBatch` is already SIMD-friendly tri tests).
2. **Coverage parity.** The laptop's sticky path only tests **BG triangles + props via `projectileFindCollidingProp`**. To match `cdTestVolume`'s coverage of chr cylinders, the new sweep would also need a chr-iteration pass. Not hard; `cdCollectGeoForCyl` already does it.
3. **Surface normal.** `bgTestHitInRoom` returns `hitthing.unk0c` (the face normal from `cdGetObstacleNormal` / `bgTestHitInVtxBatch`). This is a **real per-triangle normal**, not the "approximate" hack at `capsule.c:122`. Unlocks proper wall-slide, scary-jump landing, and Skedar surface_up sampling.
4. **Per-frame cost.** Laptop's path runs per **projectile per frame** today. Player jump runs once per frame. Cost dominated by `vtxbatches` BVH walk -- `var800a6538`-tracked top-2 batches by distance per room -- already O(numbatches) bounded, ~10-100 batches per room. On x86_64: <50us per sweep estimated.
5. **Compatibility.** No struct changes needed in `capsulecast` -- the existing `cast->hitnormal`, `cast->hitfrac`, `cast->hitprop`, `cast->hitgeoflags` fields all map cleanly. `hitgeoflags` would need a "FROM_RENDERED" bit or a synthetic FLOOR/WALL classification by normal.y dot world-up.

**Recommended path: keep `cdTestVolume` as the fast pre-filter (BG cull + prop AABB cull), add a `bgTestHitInRoom`-based per-triangle pass at the colliding step for the true safefrac.** Two-stage = no per-frame regression on the common no-collision case.

Severity HIGH (this is the actual fix shape for the wall-jump glitch).

### 4.10 Lift handling

The jump's ground reacquisition (`bondwalk.c:1001-1004`):

```
ground = cdFindGroundInfoAtCyl(&testpos, ..., &g_Vars.currentplayer->floorflags, ..., &newinlift, &lift);
```

returns `newinlift` and `lift` directly. The "in lift" decision happens **inside** `cdFindGroundInfoAtCyl` via `pad.liftnum` lookup against `g_Lifts[]`, gated on `constraints.md:65` ("Lifts must be registered in `g_Lifts[]`"). If `liftnum` is non-zero AND `liftFindByPad` finds a registered lift, `newinlift = 1`.

When a player jumps **onto** a lift:
- The capsule sweep at `bondwalk.c:1255` will detect collision with the lift's `geotilef` -- but **only if the lift tile is flagged `GEOFLAG_WALL`**. Most lift tiles are flagged FLOOR1|FLOOR2 only, so the upward sweep does NOT see them.
- The downward landing path uses `cdFindGroundInfoAtCyl` which DOES collect lift float tiles. Landing on a lift works.
- The state transition into "inlift" happens at `bondwalk.c:1080-1115` (entering, remaining, exiting). Looks correct.

Potential issue: jumping **off** a lift while it is moving upward -- the lift's vertical velocity does not transfer to `bdeltapos.y` in the impulse calculation. Player jumps with `FIXED_JUMP_IMPULSE = 8.2f`, ignoring the lift's frame-Y delta. Inherited N64 behavior, not a port regression.

For c038 wall-jump: lifts are not the cause. Lift registration (S310, 2026-04-17) is orthogonal. Severity LOW for lift correctness.

### 4.11 Bottom line for Mike's question

The jump uses **(a) flagged-tile/AABB colliders** (`cdTestVolume` substrate via 16-step `capsuleSweep` + `cdFindGroundInfoAtCyl`). The Laptop Gun's sticky stick uses **(b) full rendered display-list triangles** (`bgTestHitInRoom` over `vtxbatches`). Mike's two options map cleanly. Swapping jump to use `bgTestHitInRoom`-style queries (or layering as a second-stage validator on top of the existing cheap filter) is the actual fix shape for c038. It also unlocks real surface normals for Slice 5 Skedar surface-up gravity (currently stubbed by the `-move/|move|` approximation at `capsule.c:122`).

Files of interest:

- `src/game/bondwalk.c` (258, 849-1600, 1255)
- `src/game/bondmove.c` (214, 2121)
- `src/lib/capsule.c` (37, 73, 122, 156)
- `src/lib/collision.c` (2201, 2402-2407)
- `src/game/propobj.c` (1163, 3530, 7195, 18919)
- `src/game/bondgun.c` (4764, 4832)
- `src/game/bg.c` (3295, 4162, 4471)
- `context/pillars/physics-collision.md`
- `tools/kanban/state.json` (c038)

---

## Cross-cutting themes

### CT-1 Structural completeness without surface completeness

Three of the four tracks show the same pattern: the **deeper architectural work is shipped, but the surface layer that touches the user is incomplete**.

- **Input**: the action map + layer stack + scene events are structurally complete; menu graph migration (the touch-the-user layer that makes transitions auditable) is at ~50% of the call sites.
- **Extraction**: 13 of 13 emitter kinds write files (structural); 4-5 of 13 are practically modder-accessible (surface).
- **Jump collision**: the capsule sweep replaces the legacy `cdTestVolume/cdFindGroundInfoAtCyl` hacks (structural), but still operates on the same flagged-tile substrate (surface). The renderer-faithful per-triangle test exists in the codebase (Laptop Gun) but isn't reused.

The pattern suggests that "Step 5 / Cohort 7-8 / Phase 3" surfacework gets deprioritised once the structural arc lands. This is a generalised cross-pillar tendency, not a per-pillar miss.

### CT-2 Context-as-code drift is the meta-pattern

The Context findings (Track 2) show that the project's own knowledge base is the most under-maintained surface. README citing v46 wire when live is v47, pillars frozen at 2026-04-30, 90+ bugs stuck in FIXED-PENDING-PLAYTEST -- all because there's no automated "promote / retire / sweep" step in the daily-flow. The Daily-Flow Orchestrator (c118, shipped 2026-05-11) is the natural place to add these passes; it already touches state daily and could carry the retention sweep as a weekly task.

### CT-3 Modder-accessibility was never a Phase 5 ship requirement

The Universality Pivot Steps 0-5 (2026-04-30 through 2026-05-03) closed on "all 13 kinds emit files with manifests and SHA-256 sidecars." Modder-facing accessibility (decoders, packers, documentation) was not a Step 5 acceptance criterion. The pivot succeeded against its own success criteria; the user-facing promise of "equal-footing mods" (per CLAUDE.md and the Catalog as SoT design) is unfulfilled. This is a definition-of-done gap, not an execution failure.

### CT-4 c038 is the wall-jump glitch's only ledger entry

The wall-jump glitch is a kanban card (c038) without a B-NNN, mis-pillared as "vehicles". The root cause (jump uses cdTestVolume substrate; rendered triangles without WALL-flagged tiles pass through) is now documented in this audit. The fix shape (two-stage sweep with bgTestHitInRoom validator) is grounded in code that already exists. **The next step is filing a real B-NNN, re-pillaring c038 to physics-collision, and either spawning a fix card or attaching the fix shape to c038's notes.**

---

## Recommended next sprints (rollup)

| Sprint | Cards | Effort | Unblocks |
|--------|-------|--------|----------|
| **Context Retention Pass** | NEW (proposed c132) | 1 session | Removes stale claims everywhere; promotes 90+ FIXED-PENDING bugs to FIXED; sets up archive flow. |
| **Jump Collision Two-Stage** | c038 (re-pillared) + NEW B-NNN | 1-2 sessions | Closes wall-jump glitch; unlocks real hit normals; unblocks Skedar Slice 5; sets up surface-aware logic. |
| **Modder Accessibility Decoders** | NEW (proposed c133) | 3-5 sessions, one kind at a time | Unlocks meaningful mod authoring for meshes/audio/textures. |
| **In-Game .pdmod Packer UI** | NEW (proposed c134) | 1 session | Closes the "no packer in UI" gap so modders can build .pdmod from inside the game. |
| **Menu Graph s036-08 Continuation** | c036 / s036-08 | 15-25 sessions | Finishes the menu-graph migration lane. |
| **Ledger Hygiene** | B-298, B-195, c038 ledger updates | 30 min | Closes the in-code-but-not-in-ledger drift. |
| **`gameplayInputSuppressed()` Retirement** | NEW (proposed c135) | 1 playtest + 1 session | Retires the L.59 transitional wrapper once verification passes. |

---

## Verification notes for the orchestrator

When this audit is consumed:

1. Cross-check the executive summary's "Top action items" against the working kanban after Mike's prioritisation pass. If any item is rejected, mark the audit as partially-applied.
2. The four track sections are intentionally verbose with file:line evidence so a future code session can pick up any single item without re-investigating. Treat each section as a self-contained brief.
3. Verbatim agent output was lightly edited for tone and consistency; no facts were invented. All file:line references were grounded by the investigator agents reading the live tree on 2026-05-13.
4. **Do not archive this audit until the action items have been triaged.** Unlike sprint reports (which archive after a single verification), audits stay in the active `context/audits/` directory until either fully addressed or moved per retention.md.
5. This audit itself is past the 14-day retention window's *intended* turn around -- it should be reviewed within a week, not allowed to drift into the same staleness it documents.

---

## Where to look (sentinel)

- Track 1 (Input): `context/pillars/input.md`, `context/designs/input/input-universality-and-transitions.md` L.55-L.60.
- Track 2 (Context): `context/README.md`, `context/retention.md`, `context/pillars/*.md`, `context/bugs.md`, `.claude/sprint-reports/`.
- Track 3 (Extraction): `port/src/romextract_*.c`, `port/src/modpack_pdmod.c`, `port/src/modarchive.c`, `context/pillars/modding.md`, `context/designs/catalog/universality-pivot-schemas.md`.
- Track 4 (Jump collision): `src/game/bondwalk.c`, `src/lib/capsule.c`, `src/lib/collision.c`, `src/game/propobj.c`, `src/game/bg.c`, `context/pillars/physics-collision.md`, `tools/kanban/state.json` c038.

End of audit.
