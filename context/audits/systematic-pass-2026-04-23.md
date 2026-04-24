# Systematic Pass - 2026-04-23

Session focus: work B-221 through B-233 + B-235 follow-up, oldest-to-newest,
per Mike's directive: "inspect, examine, research, verify, fix surgically,
move to next one. Coordinate and double-check your work at the end. Clean
summary when done."

B-234 was owned by a parallel code session and skipped per coordination
constraint (that session completed f4d8dc3a mid-pass; my worktree merged
its changes then continued).

Build verification: `ninja -C Build pd pd-server` from clean, 789/789 green.
Both `PerfectDark.exe` (~54 MB) and `PerfectDarkServer.exe` (~23 MB) produced.
All work copied into the main working tree for the build; worktree branch
`claude/infallible-germain-5c8b98` carries per-bug commits for surgical
reverts if needed.

---

## Per-bug summary

| ID    | Status now | Root cause (one line) | Fix (one line) | Commit | Build | Playtest notes |
|-------|-----------|----------------------|----------------|--------|-------|----------------|
| B-221 | Verified FIXED-PENDING-PLAYTEST | Input-edge race: JO_ACTION_ACTIVATE fired on frame 0 of USE on PC, beating the hold-threshold detector. Classic double-tap dismount applied even on PC single-tap. | State machine: defer ACTIVATE until hold consumes or USE release. PC dismount bypasses legacy double-tap window (`bondbike.c:169`, `propobj.c:16500`, `bondmove.c:2238`). Architectural. | existing | green | Playtest: short-tap mount/dismount, long-hold pickup, keyboard Escape parity. |
| B-217 | Verified FIXED-PENDING-PLAYTEST (v2) | v1 returned `TICKOP_NONE` on F6, which starved chrTick and made bots invisible. Per `bot.c` header, chrTick owns model/render/transform state. | v2 zeroes `speedmultforwards/sideways` then still calls chrTick so bots stay on screen; movement intent is the right layer for "freeze". | existing | green | Playtest: F6 pauses bot motion, bots remain visible, resume returns to normal. |
| B-218 | Verified FIXED-PENDING-PLAYTEST (v2) | Order-of-ops: `mpOrchestrateMatchStartSpawns` ran between `playerReset` and `playerSpawn` in lv.c, before bot props existed. The orchestrator skipped every bot silently, leaving `g_MpOrchestrateBotPoolIdx[]` full of -1. `botSpawnAll` then fell through to `scenarioChooseSpawnLocation` rapidly 32x → same pad. | `botSpawnAll` detects "no bot has a pool idx" and re-runs the orchestrator now that bots are live. Deterministic Hungarian allows re-application without player-position churn. | existing | green | Playtest: 32-bot Chicago CS initial spawn has XZ variance ≥ min_sep; respawn already clean. |
| B-219 | Verified FIXED-PENDING-PLAYTEST (v3) | FP model loading was coupled to `setupPlaceWeapon` (pickup markers). Pickup-less arenas + spawn-with-weapon → no FP model loaded → empty hands. v2 loaded at spawn site (wrong layer). | v3 moves load to end of `setupLoadStage` driven by match manifest: iterate `g_MpSetup.weapons[]` + explicit `spawnWeaponNum`. Idempotent. Catalog ID-aware. Architectural. | existing | green | Playtest: spawn-with-weapon on Chicago (markers=0) shows FP model and fires. |
| B-224 | Verified FIXED-PENDING-PLAYTEST (v2) | Interact prompt was itself a reason to run `pdguiNewFrame/Render`, so modal dimming compounded whenever a "Hold X" prompt showed. Also leaked onto CI boot fly-in because original gate only checked `var80087260` (return-from-MP-endscreen only). | v2 broadened `pdguiCiIntroBlocksInteractPrompt` with `pausemode != UNPAUSED`, `tickmode == TICKMODE_CUTSCENE`, `lvframenum <= 30`. Shares structural cluster with B-231 (same pausemode gate catches ImGui-ctx-push-lag). | existing | green | Playtest: fresh CI boot fly-in has zero prompt/darken. Gameplay prompt shows at normal brightness. |
| B-225 | **NEW FIX 2026-04-23** | `mpArenaIndexIsUsable` treated "has non-empty langbank name" as proxy for "stage data available". Stale AllInOne Kakariko (`stage_24` / L_MPMENU_319) and Dark Noon (`mp_grid7` / L_MPMENU_321) display names slipped through even though `FILE_BG_SEVX_*` / `FILE_BG_MP7_*` did not ship. | New `stagenumIsPlayableInMp` blacklist in `setup.c` + NULL `s_ArenaNames[55]/[56]` in `assetcatalog_base.c` (extends Paradox/STAGE_EXTRA25 precedent at index 70). | `bb805b37` | green | Playtest: Combat Sim Bonus tab lists only playable arenas; Paradox/Kakariko/Dark Noon absent. |
| B-226 | **NEW FIX 2026-04-23** | `g_MpBodies[]` (decompiled data) had wrong/shared langids: CONNERY/MOORE/DALTON/DJBOND all share `L_OPTIONS_070` "Dinner Jacket"; SKEDAR uses `L_OPTIONS_356` "Choose a head to load over:"; DRCAROLL uses `L_OPTIONS_355` "Need Space For Head". `mpGetBodyName` returned these junk strings verbatim. | Add `display_name[64]` to `asset_entry.ext.body` + `catalogSetBodyDisplayName` setter + `catalogGetBodyDisplayName(mpbodynum)` accessor. Narrowly populate for indices 57-62 from `s_BaseBodies[].desc` (preserves i18n for the other 57 bodies). `mpGetBodyName` prefers the catalog override. | `f645d33a` | green | Playtest: 4 Bond actors show as Connery/Moore/Dalton/Brosnan. Skedar and Dr. Caroll labeled correctly. |
| B-227 | **NEW FIX 2026-04-23 (same commit as B-226)** | Original report said "missing from list". Investigation showed they were present but labeled as junk UI strings so Mike could not find them by name. | Resolved by B-226's catalog display_name override. | `f645d33a` | green | Playtest: Set Character and Character Select both show "Skedar" and "Dr. Caroll" by name. |
| B-228 | **Deferred with analysis** | Likely: MP setup's `(obj->flags2 & diffflag) == 0` filter at `setup.c:1778` rejects lift props that have `EXCLUDE_2P/3P/4P` bits baked into their setup blobs. Lift auto-register (S310) handles registration; lift net sync path untouched and unverified. | **Not applied.** Per rabbit-hole protocol: proper fix needs a choice between per-objtype filter bypass (risks breaking intentional MP exclusions), binary setup-blob edits, or per-stage override table. Plus network sync validation. Too much for a surgical pass. | `c9f6e9de` (docs only) | green | N/A. Reproduction hook: add `sysLogPrintf` in `setupLoadStage` OBJTYPE_LIFT case to confirm lift skipping when PLAYERCOUNT=2. |
| B-229 | Verified FIXED-PENDING-PLAYTEST | B-181 fallback unconditionally overrode `g_MatchConfig.spawn_weapon_id` even when user had explicitly selected a weapon (e.g. `base:remotemine`). | `userPickedSpawnWeapon = spawnWeaponNum != 0xFF && != 0` guard at `setup.c:2512-2558`. Preserves user intent; fallback still fires for random/unset. | existing | green | Playtest: pick spawn = remote mine on Chicago (markers=0). Match start: remote mine equipped. |
| B-230 | Verified FIXED-PENDING-PLAYTEST | ImGui internal nav consumes the first `ImGuiKey_Escape` edge injected from `actionPressed(ACTION_CANCEL_USE)`. Renderer's `IsKeyPressed(ImGuiKey_Escape, false)` sees only the second press. Same shape as S306 X-button fix. | Third close channel in mainmenu: `actionPressed(0, ACTION_CANCEL_USE)` direct read, bypasses ImGui nav consumption (`pdgui_menu_mainmenu.cpp:4081`). Precise hardware-level edge. | existing | green | Playtest: controller B, mouse X, keyboard Escape all close main menu on first press. |
| B-231 | Verified FIXED-PENDING-PLAYTEST | `playerPause()` flips pausemode UNPAUSED→PAUSING immediately, but ImGui main-menu ctx pushes 1-2 frames later. During the gap `pdguiIsActive()` and CI-gates are both false → prompt renders. | Added `pausemode != UNPAUSED` to `pdguiCiIntroBlocksInteractPrompt`. Same fix as B-224 pausemode broadening. | existing | green | Playtest: CI free-roam near computer, Start press. No prompt flash as menu appears. |
| B-232 | Verified FIXED-PENDING-PLAYTEST (v2) | Round 1: gated scanlines on `pdguiIsActive()` so they only drew under menus. Mike's correction: scanlines SHOULD always be visible when enabled (gameplay + menus). Apparent "only when prompt visible" was just `pdguiRender` running under prompt reason. | Removed `pdguiIsActive()` gate. Added `pdguiThemeGetScanlineEnabled()` as a reason in `pdguiAnyStandardOverlayReason` so pdguiRender runs every frame. User-tunable `Video.ScanlineVerticalScale` with Settings slider. | existing | green | Playtest: CRT on during gameplay, CRT off = no scanlines, CRT slider tightens/widens stride. |
| B-233 | Verified FIXED-PENDING-PLAYTEST | CI-solo respawn guard lived inside `if (mplayerisrunning)` else-branch, testing `!mplayerisrunning` - unreachable dead code by definition. Solo CI death → `dostartnewlife` never flipped → player stuck dead forever. | Added the respawn to the real `else` branch: when `stagenum == STAGE_CITRAINING` set `CHRCFLAG_HIDDEN` and `dostartnewlife = true`. Removed the unreachable MP-else check with a breadcrumb comment. | existing | green | Playtest: CI free-roam, jump OOB → fade → respawn at scenario spawn with HUD + controls. |
| B-235 follow-up | **NEW FIX 2026-04-23** | Commit f4d8dc3a closed the President-for-Maian domain-confusion bug but by design gave every selected bot the same head (deterministic default). Mike's "Set Character + multi-select" workflow expects per-bot random heads from the body's valid head set. For HEAD_RANDOM_GENDER bodies (4 Bond actors, many SP NPCs) the default path returned -1 → head_id stayed stale or hardcoded dark_combat. | Move head-pick INSIDE the per-bot loop at both pdgui_menu_room.cpp Set Character sites: `mpDefaultHeadForBody(b)` per bot (deterministic for specific-head bodies, fresh random gender-pool pick for HEAD_RANDOM_GENDER). matchsetup.c: new `pickHeadIdForBody` helper wraps the same logic for `matchConfigAddBot` and `pickRandomBodyHead`. | `dc59a9d4` | green | Playtest: 10 Connery bots → random male heads. 10 Maian bots → uniform Maian head (data-model limit). `matchConfigAddBot(body=connery, head=NULL)` → random head, not dark_combat. |

---

## Cluster map

Fixes that share a root cause or structural area:

1. **Catalog ID as sole identity (M-class)**:
   - B-225 (stale arenas filtered via s_ArenaNames NULL + stagenum-blacklist in setup.c)
   - B-226 / B-227 (catalog-backed display_name for bodies with junk langids)
   - B-235 follow-up (mpDefaultHeadForBody as authoritative "head for body" picker)

   These three all advance the same rule: data identity and display flow through
   the catalog, not through a parallel langbank/g_MpBodies shadow table. Each
   made `s_BaseBodies[i].desc` or adjacent data more load-bearing and reduced
   the number of places where a display name or validity filter could drift.

2. **Pausemode / ImGui-ctx-push-lag (L-class, already landed)**:
   - B-224 (interact prompt dim compounding)
   - B-231 (interact prompt frame flash on menu open)

   Both use the same `pausemode != UNPAUSED` gate to cover the 1-2 frame window
   between `playerPause()` flipping state and the ImGui menu ctx actually
   pushing. `pdguiCiIntroBlocksInteractPrompt` now carries a name that's honest
   with its broader scope ("interact prompt should be suppressed").

3. **Input-edge decoupling (M-class, already landed)**:
   - B-221 (PC hoverbike tap vs hold)
   - B-230 (controller B first-press consumed by ImGui nav)

   Both replace "fire on press edge" with a precise secondary channel (USE
   release for hoverbike; direct `actionPressed` for CANCEL_USE). Mirrors S306
   (X-button needed two clicks) - third close-channel pattern.

4. **Manifest-driven resource loading (H-class, already landed)**:
   - B-219 v3 (FP models loaded end-of-setupLoadStage from g_MpSetup.weapons[] + spawnWeaponNum)
   - B-218 v2 (spawn orchestrator re-run in botSpawnAll when bot props become live)

   Both decouple a resource-loading step from a side-effecting setup flow and
   re-anchor it to the actual manifest data. The order-of-operations gap is
   the common root cause.

5. **Dead-code guards (one-off)**:
   - B-233 (solo CI respawn check was unreachable by branch structure)

   Class identifier: "guard conditional lives inside a branch that guarantees
   the guard's negation is unreachable." Class audit checklist: grep `!foo`
   inside an `else` for `if (foo)`.

---

## Foundation-work recommendations

Ordered by dependency / breadth of class-retirement.

### 1. Per-body valid-head set (scope: MED, effort: ~1 day)

**What.** Extend the catalog body struct with `valid_head_ids[N]` (or
`valid_head_category`) so bodies can declare more than one acceptable head.
Maian body right now has one paired head (MAIAN_S); adding `skedar_small`,
`skedar_warrior` etc. as valid heads for Skedar body and `maian_s`,
`maian_f` (hypothetical) for Maian body unlocks real per-bot variety from
the B-235 follow-up fix.

**Why.** The B-235 follow-up's data-model caveat is that deterministic
bodies yield uniform heads. Any future "random from valid set" work hits
the same wall. Making the set explicit in the catalog retires a whole
class of "how do I pick heads for body X" questions.

**Dependencies.** None in code. Data authoring only (extend `s_BaseBodies`
to carry lists; extend mod.json schema to support `valid_heads: [...]`).

**Risk.** Low. If no body declares a set, fall back to current default-head
logic.

### 2. Consolidate langbank + catalog display names (scope: SMALL, effort: 2-3 hours)

**What.** Deprecate `g_MpBodies[].name` as a langid read site. Make
`catalogGetBodyDisplayName` the primary accessor with langbank as fallback
only for bodies that have a non-stale langid. Short-term: extend B-226's
6-body override to cover ALL bodies whose desc is non-empty in
`s_BaseBodies`, with a clean opt-out if i18n is desired.

**Why.** The B-226 investigation found `s_BaseBodies[].desc` was a
documentation-only string never read at runtime - a classic split-brain
data channel. Promoting desc to load-bearing for ALL base bodies retires
"langid drift on a body" as a bug class.

**Dependencies.** Audit `mpGetBodyName` call sites to decide which retain
i18n (only the Character Select and Settings displays likely need it; the
in-match HUD tag probably benefits from English-canonical catalog names).

**Risk.** Medium for bodies like Joanna Dark where the desc ("Joanna Dark
(Combat)") differs from the langbank English ("Joanna Combat"). Need Mike's
preference call per name.

### 3. SP-stages-in-MP loader (scope: LARGER, effort: 1-2 days)

**What.** Close B-228 properly. Options:
- **a)** Runtime: at setup-load time on MP-mode + SP-class stage (CI,
  Chicago, Villa), clear `EXCLUDE_2P/3P/4P` for `OBJTYPE_LIFT` /
  `OBJTYPE_ESCASTEP` / door-chain props. Risks: any SP stage that
  INTENTIONALLY excluded a lift for perf (e.g. busy MP scene on SP geometry)
  loses that cut.
- **b)** Per-stage override table keyed on stagenum. Explicit.
- **c)** Setup-blob edits. Bakes into asset. Hardest to maintain.

Plus: validate lift sync over netmsg.c. Grid (MP-native) already syncs; CI
probably needs the same path but unverified for SP-loaded lifts.

**Why.** Closes B-228 and unblocks Chicago / CI / Villa as MP stages. Also
creates a "SP props that work in MP" discipline that future SP-in-MP stages
can follow.

**Dependencies.** B-228 reproduction hook (sysLogPrintf in OBJTYPE_LIFT case
to confirm the filter is the blocker). Net-sync validation on a test stage.

**Risk.** Higher - touches setup filter + networking. Need a playtest
matrix: (CI solo) + (CI 2P listen) + (CI 2P dedi) + (Grid 2P regression).

### 4. SP-stage MP-readiness audit (scope: MED, effort: 1 day)

**What.** After #3 lands, walk every SP-class stage used in MP, verify
lifts/stairs load and sync. Document per-stage exclude-bit state in
`context/designs/`.

**Why.** Prevents silent regressions. Provides a playtest checklist for
stage-load sanity.

**Dependencies.** #3.

**Risk.** None - documentation.

---

## Bugs deferred or couldn't fix

- **B-228** (MED, OPEN / DEFERRED). SP-class lift/escstep props skipped when
  an SP stage loads in MP context. Investigated the code path, identified
  the likely filter (`EXCLUDE_2P/3P/4P` in setup.c:1668-1676), but the fix
  requires a design decision between unconditional bypass, per-stage
  override, or binary setup-blob edits - plus networking-side validation.
  Out of scope for a surgical systematic pass. Analysis captured in
  `context/bugs.md` B-228 row with reproduction hook.

All other bugs in the systematic window closed (either verified or
newly-fixed).

---

## Parallel-session coordination note

Session `local_8d0240cc-...` committed f4d8dc3a (B-234 / B-235 Maian President
head fix) mid-pass. My worktree merged those changes at commit `428a7c26`
before continuing with B-226 / B-227 / B-228 / B-235 follow-up. No files
from the released ownership set were touched until the merge. B-235's own
follow-up piggy-backed cleanly on the per-bot-head-pick helper path the
B-234 fix established.

---

## Commit ledger (this pass)

| SHA       | Scope               | Files touched                                         |
|-----------|---------------------|-------------------------------------------------------|
| `bb805b37` | B-225               | `setup.c`, `assetcatalog_base.c`, `bugs.md`           |
| `428a7c26` | merge main           | (merge commit - 6 upstream files + bugs.md conflict)  |
| `f645d33a` | B-226 / B-227       | `assetcatalog.h`, `assetcatalog.c`, `assetcatalog_api.c`, `assetcatalog_base.c`, `mplayer.c`, `bugs.md` |
| `c9f6e9de` | B-228 deferral docs  | `bugs.md`                                             |
| `dc59a9d4` | B-235 follow-up      | `pdgui_menu_room.cpp`, `matchsetup.c`, `bugs.md`      |

Final clean build: `ninja -C Build pd pd-server` → 789/789, both exes produced.
