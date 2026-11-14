# Perfect Dark approved continuation: Astra plan for Sol 6.1

Planning checkpoint: 2026-10-10, 20:30 UTC. Workbench **T-TOOLING-012**.
Owner: `codex-pd-astra-plan-20261010`. Requested planning model: Astra Ultra.
Execution model: Sol 6.1 in a fresh session; Astra retained for hard decisions
and integrated review. This document is a handoff, not a new task authority.
The canonical Workbench remains the sole live task/decision/evidence store.

## Authorization and finish line

Mike resumed only previously approved Perfect Dark implementation/acceptance,
then limited this Astra session to checkpoint inspection, safe coordination,
and a complete implementation plan. No implementation or heavy verification
was authorized for this planning pass. Other projects and reporting schedules
remain paused. No automation was resumed or created.

The October 10 Plex waiver expires at the end of **October 10 America/New_York**
(2026-10-11 04:00 UTC). Do not extrapolate it to later dates. Future daily
16:45-21:30 America/New_York protection remains unless Mike changes it.

Preserve the approved scope in D-004 and D-006: complete base game and public
source/graph production behavior, usable campaign/Combat Simulator Theater,
one small graph-backed custom-slot sample weapon, friend-play/performance,
then one qualified release candidate. Follow T-RELEASE-002 through -006.
Forge maps/map variants, broad creator/Studio tooling, Needler-specific polish,
Queue Match (D-001 deferred), and authority migration are not added by this plan.
The October 10 Claude audit is evidence to investigate, not scope authorization.

Initial playable handoff requires T-TOOLING-011 retained-profile integration,
the unchanged V-014 ordinary preview, actual review of the four images,
T-ASSETS-051 fidelity and T-MENUS-007 edit/save/restart evidence. This is smaller
than complete asset/graph or 1.0 acceptance. Never equate the two finish lines.

## Verified starting state

| Surface | Verified fact and limit |
|---|---|
| Checkout | Canonical `C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike`, branch `dev`; local HEAD and fresh `git ls-remote origin refs/heads/dev` both `e7624402dc15bab263f987ef3d43fda90f7406e7`; remote `https://github.com/MikeHazeJr/perfect-dark-2.git`. |
| Dirty work | 41 tracked dirty paths and existing untracked evidence/audits; index empty at inspection. Includes instructions/context/tooling, ten movement paths, and `tests/test_body_head_source.cpp`. All are preserved. |
| Workbench | `/api/meta` verified canonical=true, isolated=false, canonical project/data roots; 199 items before creating planning item T-TOOLING-012. No new targeted/GENERAL note at selection. N-0055 is a separate needs_clarification note for T-TOOLING-003. |
| Native isolation | Source `c6b3e2ece8b3e2855ed820534b03925ef799a7b9`, closeout `e7624402`; exact CI 37176234687 passed 1,835 cases/334,167 assertions. Local final 22 cases/321 assertions, actual locked capability query PS5.1/7.6, native/public-source guards passed. This does not prove an ordinary retained-profile game run. |
| Latest ordinary run | October 3 receipt: exit 1, 11/29 assertions, 1/17 events, zero captures. Physical Xbox occupied player 0. Window disappeared before app access check; this attempt was not an app denial. |
| Earlier app denial | October 2 `get_window_state`: Computer Use was not approved to use perfectdark. D-010 later records consent, but actual app access remains unverified. Do not retry a denied route without appropriate new approval. |
| Never run | Prepared immediate ownership watcher; retained-profile ordinary integration; required four-frame review; physical-controller and Settings keyboard/edit/save/restart gates. |
| Current protocol | `port/include/net/net.h:13` is protocol **61**, not README's historical v59. Preserve payload compatibility in the pending movement unit. |
| New peer builds | `pd1010` client/updater/tests compile exit receipts are 0 at 19:59:11/12/21 UTC October 10. Its HEAD-only build metadata does not bind the dirty snapshot. No fresh test-execution/gameplay verdict was established by the bounded inspection. |

The old N-0420 quiescence was October 4 history. Live coordination still shows
the former coordinator as blocked with an expired Astral reservation; this
contradicts the relayed completed label. Do not revive that reservation or edit
another session's operational state merely to normalize it. The user has
authorized replacing the inactive coordinator for continuation.

Current coordination has **claude-fable-pd-finish-20261010**, registered active
at 18:34:13 UTC, planning baseline builds, the in-flight networking commit,
runtime proof and per-front work. The header changed today at 19:56:56 UTC.
Planning sent a targeted ownership message at 20:26:55 UTC; no reply was visible
at this checkpoint. No Workbench item presently lists that Claude identity.
This is an ownership discrepancy, not permission to overwrite its changes.

Planning owns only this new document and T-TOOLING-012. It did not take source,
shared context, Git, build, runtime or capture ownership. Narrow process-name
inspection found no PerfectDark/pd/pd-tests/cmake/ninja/MinGW compiler process;
this is not comprehensive wrapper/lease or future runtime admission proof.
No native desktop-control tool is available in this planning environment.

## Evidence and exact retained identities

Current hashes below were read on October 10, without executing these files:

| Object | SHA-256 |
|---|---|
| `.claude/session-builds/pd1001init/PerfectDark.exe` | `26aefa01b2b5d1a3723f6433c5cacd82d27a2855e195bc6b732567d738a96660` |
| `.claude/smoke-storage/shared/client/PerfectDark.exe` | `43a60276400fe2be1db40279195b4fb7fc6168fb2dff527d092610c75b1ea145` |
| `tools/smoke-verify/tests/menu_virtual_controller_agent_cancel.json` | `146068674869531c5178328f7a6426b464f576a9d0c638af56eaa5201d3ba0ca` |
| `tools/smoke-verify/run.ps1` | `8c8a2da068e5f46ab08a56736e2c0f52b66b736a723ed4ee80ac11a1cb4dd352` |
| `tools/smoke-verify/lib/Reuse-Runtime.ps1` | `a5f2fd1ca4347c39bd5bec0a618ee67efd88bc04303ecc7719f9919abe2f8370` |
| Historical `renewed-admission-1/publish-game-ready.ps1` | `1f6a616a46d53eb90b74986f25efb9aace3adafbc52468664c34f27757450227` |

Retained base ID is
`20261003T225125084026Z-menu_virtual_controller_agent_cancel-439f0cfe`;
seed `a7fdf399c01712c44c8e8a5576f3b86b90fd73f25ecf133974ec3ed004459242`.
Registry records completed, unpinned, passed=false, path shared/client.
Two other completed failed full copies remain pinned. These are metadata
observations, not a fresh complete-base integrity/lease admission result.

Primary receipts:

- [Natively isolated reuse](../../audits/2026/pd-native-reuse-isolation-2026-10-04.md), N-0419/N-0420.
- `.claude/pd-initial-integration/20261004-night/native-write-isolation/{final-handoff.json,source-freeze-4.json,policy-query-final-ps51.json,policy-query-final-ps76.json,ci-c6b3-success.json}`.
- `.claude/pd-initial-integration/20261003-night/renewed-admission-1/runtime-result.json` and `publish-game-ready.ps1`.
- [Graph cutover evidence](../../audits/2026/graph-production-cutover-2026-09-29.md), [held production seams](../modding/held-graph-production-seams-2026-09-27.md).
- [Approved asset/graph closure plan](../catalog/asset-graph-goal-closure-plan-2026-09-23.md), [strict native source evidence](../../audits/2026/pd-native-consumer-publication-2026-10-02.md).
- [Pending movement audit](../../audits/2026/multiplayer-movement-ordering-2026-09-27.md), `.claude/mp0927-movement/{native-result.json,source-before.json}`, N-0249.

## First Sol 6.1 assignment: repair the retained-profile ownership cue

**Goal:** make the canonical runner publish promptly the exact process it owns
so an authorized desktop executor can identify the live game. Keep this one
bounded tooling repair under T-TOOLING-011/T-TOOLING-008. It is not a native
isolation rewrite and cannot by itself pass V-014.

**Verified defect:** the prepared historical watcher hardcodes shared/client,
its logs/smoke-process-ownership directory, its PerfectDark.exe and its
.pd-storage-owner.json. Current `run.ps1:2160-2168` instead creates a private
reuse runtime root and uses a separately locked consumer executable. Around
2394 it writes ownership into that private root. The old watcher cannot find
or validate that layout. Preserve the old script as historical evidence.

**Before edits:** obtain Claude's exact file/item/source-freeze/Git handoff.
Reuse the new session's hook identity (or prescribed status/register fallback),
verify canonical API metadata, claim exact source surfaces, process targeted
notes and snapshot current bytes/diff/index. Do not inherit a prior identity.
If Claude has not replied, read-only preparation may continue, but no shared
source edits or tests/builds proceed on assumed ownership.

**Proposed edit boundary:** `tools/smoke-verify/run.ps1`,
`lib/Owned-Process.ps1`, `lib/Reuse-Runtime.ps1` only if needed,
`test-owned-process.ps1`, `test-storage-reuse.ps1`, a focused new cue test if
needed, and the currently dirty README only through isolated owned hunks.
Claim a new helper file explicitly if extraction improves the existing seam.
No port/src, input/controller, native write policy, fixture or storage-policy
change is required by this diagnosis.

**Implementation contract:**

1. Add an optional canonical-runner cue output using validated runner state.
   Default behavior stays unchanged when absent. Emit after lease acquisition,
   consumer/fixture locks and exact `New-SmokeProcessOwnership` establishment,
   before entering readiness wait. Do not create another process-search path.
2. Bind consumer absolute path/hash, canonical fixture/hash, base install ID,
   seed/base executable hash, private profile/reuse ID, actual PID/creation/
   parent/token identity, actor and current bounded run context. Keep base,
   running executable and output root distinct. Use existing identity helpers.
3. Atomically publish only to a fresh session-owned evidence destination;
   reject existing/stale outputs, alias/reparse escape, mismatched identity,
   expired/cancelled run context and ambiguous ownership. Never scan unrelated
   processes or install trees to guess a match.
4. Cue means **owned process only**. It does not mean app permission, HWND,
   foreground, free controller slot, menu readiness or passing gameplay.
   The receiver must revalidate exact process identity and unexpired run
   context immediately before use; publication-time validity cannot exclude
   later exit or PID reuse.
5. On cue failure after launch, use existing exact-owned-process closeout;
   retain evidence and lease until exit/console drainage/base checks succeed.
   No orphan, premature zero-process claim or global termination.
6. Correct the README's stale unconditional runtime-refusal paragraph for the
   already implemented native adapter, preserving peer hunks and unsupported
   fixture refusal. Record exactly what remains unqualified.

**Automated acceptance:** PS5.1 and PS7 tests exercise the real split layout,
success, stale destination, wrong hash/profile/fixture, PID reuse, expired or
cancelled context, alias/reparse escape, unchanged behavior with no cue option,
early process exit and output failure after launch. Include
actual hidden, exactly owned tiny fixture processes plus pure negative cases.
Use `test-owned-process.ps1 -EvidenceDirectory <fresh-owned-dir>` and the
affected storage/reuse/console aggregate; inspect scripts first for forbidden
cleanup behavior under this session's no-deletion instruction. An aggregate
with unconditional deletion needs an existing retention option or explicitly
reviewed fixture-retention adjustment; otherwise record it as blocked. No real
game is needed for these tooling tests. Record exact checks rather than target
counts. Do not rebuild native binaries for a PowerShell-only change. If
diagnosis requires C/C++ or files outside this unit, checkpoint the finding
and reconcile the revised file/item boundary before implementation.

**Done for this batch:** independent review, proportional aggregate passes,
durable source/evidence hashes, scoped publication and exact-commit CI. Keep
T-TOOLING-011 partial until the following actual game gate. If denied access or
overlapping ownership prevents proof, retain the candidate and report that
specific blocker; do not broaden into device/OS/security tooling.

## Retained-profile ordinary acceptance, immediately after the cue unit

Use the retained consumer above only while its exact source/binary identity is
still applicable. Do not silently replace it with Claude's pd1010 build. First
reconcile that newer build's frozen source and tests if choosing it instead.

Illustrative canonical invocation, **not executed by this plan** (run only
after fresh admission; add the newly reviewed cue option):

```powershell
.\tools\smoke-verify\run.ps1 -Test menu_virtual_controller_agent_cancel `
  -ReuseInstallId '20261003T225125084026Z-menu_virtual_controller_agent_cancel-439f0cfe' `
  -ReuseSeed 'a7fdf399c01712c44c8e8a5576f3b86b90fd73f25ecf133974ec3ed004459242' `
  -ReuseBinarySha256 '43a60276400fe2be1db40279195b4fb7fc6168fb2dff527d092610c75b1ea145' `
  -ReuseConsumerBinary 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\.claude\session-builds\pd1001init\PerfectDark.exe' `
  -ReuseConsumerSha256 '26aefa01b2b5d1a3723f6433c5cacd82d27a2855e195bc6b732567d738a96660'
```

Admission must establish: supported interactive executor and legitimate app
access; user-managed free player-0 availability followed by game-level proof;
fresh exact-owned-process query capability; unchanged verified base/receipts;
storage budget and exclusive reuse lease; and a fresh finite coordinated
runtime/capture window. Initialize executor readiness before allocating the
game window. Do not launch merely to rediscover a known controller blocker.
PnP interface presence is not slot assignment. No controller takeover, global
input, grant editing or retry of denied access is authorized by a cue repair.
If the supported app API needs a live window, identify it from the new cue and
stop on denial; absence before launch is not proof permission has been granted.

Use the existing 15-minute window protocol with enough setup/closeout reserve
(previous protocol required 468 seconds plus setup). Recompute timestamps;
never reuse October grants. Keep native timeout 240 seconds, initial stable
readiness 180000 ms/250 ms, 17 events and all 29 assertions unchanged.

Pass requires all 29 assertions and 17 events; exactly one create and cancel;
two stable Agent Select and two stable Agent Create witnesses; player-0 virtual
attach and neutral/removed detach; scripted exit 0; no timeout/fatal/input
failure; all four nonempty valid GL captures retained and inspected:
`virtual-before-select.bmp`, `virtual-create-open.bmp`,
`virtual-after-editor-back.bmp`, `virtual-after-cancel.bmp`.
Require new profile result/receipt, correct generated-write locations, unchanged
base/prior failed verdict, console drain, verified actor absence, then release
leases/FIFOs. Failure remains failure and retains all artifacts.

This validates software-controller cancellation and retained-profile plumbing.
It does not validate physical devices, MKB, Settings persistence, all character
fidelity or the asset corpus. Review the four images for panel bounds, character
body/head/material/projection and correct editor/screen Back ownership. Assign
any observed root cause to the existing asset/menu lane before further edits.

## Ordered implementation and acceptance batches

Each row is a coherent future unit, not permission for simultaneous writers.
Within a row, enumerate exact files before implementation and stop at its
receipt. Revalidate current source; do not reimplement historical diagnoses.

| Order / existing Workbench | Bounded work | Acceptance and dependencies |
|---|---|---|
| 1. T-TOOLING-011, V-014 | Cue repair and retained-profile gate above. | Tooling unit first, then real gate only after independent admission. Unblocks trustworthy live observations; does not close initial scope alone. |
| 2. T-ASSETS-051, T-MENUS-007, T-MENUS-003, V-004 | Initial character preview fidelity and Settings input profile edit/save/restart; fix only observed defects. Seams: `pdgui_charpreview.c`, `gfx_pc.cpp`, `pdgui_input_profile_state.h`, Settings/profile persistence and their focused tests. | Same-run source/render attribution, four reviewed images; preserve all 16 device rules across actual UI edit, save, process restart and reload. MKB and physical-controller journeys, focus/modal/Back ownership, bindings and device-correct glyphs need separate receipts. Keyboard/mutating fixtures are not admitted by current reuse adapter: do not relax it to force this gate. |
| 3. T-MODDING-002, T-RUNTIME-001 | One base single-shot plus existing custom offline graph through actual world-hit lifecycle. Inspect base equipment/slot policies before changing the custom-slot guard in `weapon_graph_v2_runtime.cpp:189-195`; preserve immutable selection through `bondgun.c`, `prop.c`, catalog loader/scanner. | Ordinary catalog -> equip -> input -> attack -> actual hit/render. Authored branch/damage changes outcome; false gate causes no shot/debit/audio. Primary/secondary and both hands isolate state. Delayed hit/noise retain selected generation across source replacement; reload/switch/death/stage reset retire it. Preserve custom regression and unsupported network/Theater/bot rejection. Installed 41/41 alone is insufficient. |
| 4. T-TOOLING-009, T-ASSETS-046/049 | One representative SFX exact public-member-to-playback closure through `catalog_audio_generation.c` and native mixer. | Same-run archive/member hash, catalog identity, decoder generation and playback. Controlled waveform/loop edit changes PCM/playback; retained old voice finishes on original source; malformed replacement rolls back. Actual listening has its own verdict. No all-format claim. |
| 5. T-TOOLING-009, T-RUNTIME-001 | One animation source-to-played-pose closure through `catalog_animation_generation.c`, compiler and normal consumer. | Exact consumed members/clip generation; edited channel changes actual pose, selected interpolation/loop boundary and retirement; labeled ordinary before/after frames. Decoder count is not played-pose proof. Other clips/root-motion remain explicit. |
| 6. T-MODDING-002/-010 | After row 3, one automatic cadence profile, then a separate burst unit using the approved operation inventory. | Preserve native sole debit, cadence/fractional remainder, dual-hand timing, trigger release and delayed completion. Source rate/burst edit changes firing; cancellation/replacement cannot duplicate shots or strand effects. Beam/charge/throw/melee/device/projectile/entity remain separate staged units. |
| 7. T-ASSETS-042 | One already extracted locale's ordinary selection/persistence; separate Japanese codec/glyph unit later. | Edited selected strings reach menu/HUD; unselected locale does not bind; atomic banks/fonts, fallback/null/empty semantics, restart persistence. Latin-1 locales do not prove Japanese packed-code or glyph fidelity. |
| Parallel only after explicit non-overlap: T-NETWORKING-011 | Review and qualify the existing ten-file movement ordering unit plus today's header compatibility change. Do not rewrite peer work. | Fresh C/C++ ABI review, current frozen networking aggregate, unchanged v61 wire layout; duplicate/stale/half-range rejection leaves full state untouched and cannot trigger teleport/fire effects. Accepted ACK serial contract, zero-init and helper wrap cases. Then exact ordinary co-op/counter-op stage transaction and content-decline gates. Full F05 authority/prediction remains open. |

Full approved asset/graph closure remains governed by the September 23 plan:
27-family source/runtime/edit/failure/fidelity coverage, lossless field-level
extraction and dependency refresh, complete accepted graph vocabulary and
base/custom parity. Repeat small family units until every explicit gap has
evidence. A consumer receipt, archive count or preview card cannot substitute
for the relevant production behavior. Strict CCTV evidence presently covers
five archives/seven traces/four texture joins, not full corpus fidelity.

## Remaining release dependencies and acceptance

Live V-010 dependency calculation is **3 validated, 9 partial, 4 missing**
across 16 leaves (18.75%). This is a status count, not estimated project effort.
Validated: T-ENGINE-004, T-TESTS-002, V-009. Partial: A-ASSETS-001,
T-EXTRACTION-001, T-RUNTIME-001, T-MODDING-002, T-TOOLING-003,
V-003, V-004, V-006, T-ENGINE-005. Missing: T-VEHICLES-002,
T-MODINFRASTRUCTURE-003, V-005, V-008. V-010 itself is missing/not_run.

| Gate | Remaining contract and execution boundary |
|---|---|
| V-003/V-005/V-006/V-008 | All 27 public families feed native production; controlled edits observably change consumers; extraction/load/save failures reject atomically without ROM fallback; custom mode/bot identities survive save/listen-host. Preserve old failures and prove selected catalog identity, not only descriptor presence. |
| T-ENGINE-005 | Qualify existing bot attribution, capsule-safe spawn/recovery and body/head rig cohort. Current test is in CMake; do not treat the old never-built item as today's build truth. Execute affected cases and `combat_sim_defect_cohort` on frozen current source, including base:dark_combat + base:head_dark_combat. |
| T-VEHICLES-002 | Full ordinary hoverbike operation, enter/leave/control/collision/damage plus current protocol authority parity; historical v59 source or compilation is insufficient. |
| T-MODINFRASTRUCTURE-003 | Clean base-game authority excludes bundled AllInOne content; any retained content is optional typed packages with correct identity/enable/disable and clean-install/save/manifest/distribution proof. Under current no-deletion instruction, do not remove files; first produce exact migration/removal scope for Mike's decision. |
| V-010/T-TOOLING-003 | Every required campaign/Combat Simulator coverage cell needs executable fixtures and durable ordinary receipts. Reconcile N-0055 before coverage edits. Carrington/B-1106 six-frame/render proof is one regression, not complete base-game or graph closure. |
| T-RELEASE-003: T-THEATER-001/V-011 | Versioned bounded persistent reader/storage, missing event coverage, then world reconstruction and play/pause/seek/stop/cameras. Save, restart process, list/load/watch one campaign and one Combat Simulator session with coherent objectives/cutscenes/props/projectiles/effects/weapons/scores; interrupted/corrupt/oversized/version failures are fail-closed. Add listen-host replay if capture differs. Current recorder does not imply playback. Resolve graph-in-Theater integration before lifting its guard. |
| T-RELEASE-004: T-MODDING-008/V-012 | One minimal enabled-by-default catalog-identified graph weapon in supported custom slots; observable burst or alternate projectile; save/load/local/listen-host, malformed source and readability/obstruction gates. No Needler polish expansion. |
| T-RELEASE-005 | Complete D-003 networking leaves, real WAN/NAT and content-consent/return/reconnect matrix on the current wire; retain typed signed match authority separate from probe/relay routes. P-001 requires explicit approved cold/warm/extraction/cache/load budgets and measured artifacts. Never fabricate budget numbers. |
| T-RELEASE-006/V-013 | Only after earlier milestones: one source-frozen candidate, complete aggregate tests and release matrix, ordinary/game-director acceptance, no unresolved 1.0 blockers. Reject receipts with relevant source/binary/fixture drift. |

T-NETWORKING-011's F05 authoritative simulation/prediction is a distinct
architectural unit. Current host movement follows reported position; replay
history is absent. A future scoped design must specify raw-intent bounds,
consumed-input ACKs, replay history, expiry/one-shot behavior, movement modes
and latency reconciliation. The movement ordering repair does not close it.
Use Astra for that design decision and Mike for scope/architecture changes.

## Preservation, validation and publication protocol for every Sol batch

1. Re-read AGENTS/scoped context, canonical API metadata, affected item/notes
   and live coordination. Obtain exact handoff from Claude and any relevant
   former owner; record dependencies and file/hunk ownership. No worktrees.
2. Capture HEAD, staged state, working diffs/line counts and SHA-256 manifest of
   the owned unit plus peer preservation witnesses. Do not stage all, stash
   broadly, reset, copy a checkout over this one or touch Git locks.
3. Public editable source stays native through catalog/provider. Generated
   engine products are source-hashed cache only; catalog IDs are authored
   identity. No opaque dumps, duplicate authored runtime payloads or runtime
   ROM fallback. Preserve the explicit valid glTF-declared buffer exception.
4. Implement a coherent unit, independently review it, then source-freeze.
   Run cheap proportional sanity checks during editing; avoid repeated builds.
   Use FIFO queue/wait/start/finish for tests, builds, game/capture/extraction
   and Git serialization. No heavy operation without fresh resource admission.
5. Use `devtools/build-session.ps1 -Session <owned-id> -Target client` (or all
   for client/updater), **and explicit `-Target tests`** when tests must rebuild.
   Inspection/receipts show all alone did not rebuild tests. Never use shared
   Build, -NoQueue or disable watchdog. Preserve exit124/failure logs.
6. Run affected aggregate plus required full suite on exact frozen binaries
   through the test wrapper; retain selectors, executed counts, exit/JUnit/log
   and hash witnesses. Graph aggregate includes `[graph-v2-draft]`,
   `[graph-v2-equipped]`, `[modding][pdxxx][c3842]` plus affected model/weapon
   slot/body/head cases. Networking uses its current affected aggregate.
   Asset code changes require `python tools/asset_native_source_guard.py`.
7. For changed write-policy/storage seams, retain Python native-write/storage/
   reuse checks, PS5.1/7 adapter checks and actual linked policy query; use
   configured `native_write_policy_guard.py --compile-commands <owned-build>/compile_commands.json`
   only when applicable. Respect native Windows CPython for storage tests.
8. Ordinary proof uses fresh exact actor/fixture/base/consumer/profile identity,
   supported permissions and finite lease. No UI or OS access workaround.
   Preserve the 200 GiB temporary PD floor plus current reserves (run 204,
   build 206 GiB under checked policy); current capacity must be measured.
   Three full installs/two pins are not permission to rotate/delete a copy.
9. Finish only after verified owned-process absence; then release resources.
   Unknown metadata is unknown, not zero. Do not release a running lease or
   kill global processes. No cleanup, deletion, unpin, cap/floor change or
   OS/Plex/security change is included. Keep unique binaries/receipts/recipes.
10. Update Workbench and relevant owned context with four separate outcomes:
    passed/completed, failed, never run, and blocked/manual acceptance. Use
    implemented only for connected production behavior; validated requires
    durable passing evidence/model attribution and validation verdict/artifacts.
11. Current Mike authorization is scoped commit/push. Serialize Git, stage only
    reviewed owned paths/hunks, inspect staged diff, preserve peer hashes,
    commit and normal-push origin/dev. Check fresh remote HEAD and terminal CI
    whose headSha equals that commit. PD2 CI cancels superseded branch runs,
    so coordinate pushes until the exact run finishes. Compilation alone or
    another commit's green run cannot satisfy this step. Record cancellation
    as unqualified, not pass. No repeated permission request within scope.

## Corrections and unresolved handoff conditions

Do not silently rewrite today's Claude audit or shared peer context. Its
blanket code-green/complete-extraction/single-runtime-wall claims exceed the
evidence. Current scenario loading already parses, builds a plan and commits
(`scenario_save.c:946/955/962`); audit B-1068 from that code, not a superseded
mutation diagnosis. General AI graph coverage cannot be assumed zero:
`scenario_source_runtime.c` contains source-graph action/condition execution.
Historic HWND=0/memory readings are not current machine admission results.

The ten movement files are listed in the September 27 audit. Eight still match
its source snapshot; `net.c` and `net_player_move_wire.h` do not. Current header
SHA `c6d56c00da4da41b1ef3eebcccfcf3a2d7d437d1f64a8f449c07d15e8cc51695`
contains today's legacy four-byte bool/types.h repair. Current net.c SHA
`760dc8242f9cb9f7f75dab4232649fe8d53a479e10f106789641e0f5475dd3c1`.
Do not claim whole-simulation wrap safety: downstream tick comparisons and
zero-as-absent behavior remain outside that helper's proof.

Blocking before source execution: explicit live Claude ownership reconciliation.
Blocking before ordinary acceptance: native-capable executor, actual appropriate
app access, free game player-0 availability, fresh admission and runtime window.
If those remain blocked, Sol may complete the independently reviewed cue unit
after ownership handoff and then take a clearly non-overlapping approved source
batch; it must not relabel ordinary acceptance or start speculative features.

This planning pass ran no build, test suite, game, capture, extraction or device
probe. It changed no product source and performed no Git stage/commit/push.
Its deliverable is this plan plus Workbench ownership/handoff. Static document
validation and independent plan review are recorded in T-TOOLING-012 closeout.

End of approved continuation plan.
