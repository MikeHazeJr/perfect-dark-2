# Menu accessibility and functionality

## Current verification - 2026-09-23

Final bounded pause unit on fixed HEAD `842663b4` compiled the Forge live-bot
retirement and Near-Me roster-index correction. Isolated client SHA-256
`16A3CF0E328A8FAB141BF9DFD68A3E5D38E57C653B5F31012A18479E90A83E3F`
and tests SHA-256
`BECCD4FCEBFF4F11405CBC065721F3A809C8DB7ED54D81AEA42F0881932495E3`
match pre/post manifests under `.claude/session-builds/menu0923/` (23 changed
production/test/runner files, zero hash or HEAD drift). The final receipts
pass menu graph 846/846, adjacent menu/tooling/cutscene 89/89, and broad
input/menu/settings 9,732/9,732. Full `full-final-junit.xml` remains RED
20/131,286: the earlier five menu/tooling/network source-contract failures
are cleared, while 18 asset/catalog and two engine/character source
contracts remain outside this lane. No Forge bot runtime or ordinary menu
journey ran on this final binary. Mike requested pause after this validation;
the reachable-screen matrix remains open.

Post-r7 Forge lifecycle source now retires live bot props on Remove All and
exit-play, preserving other MP roster entries, and checks whether Add actually
published a bot. The r7 binary predates this change. User requested a bounded
validation/commit checkpoint followed by pause; no wider ordinary menu journey
will start until resume.
The Forge source edit overlapped the MP owner's 15:24Z smoke source hold. That
owner rejected the otherwise passing MP receipt for source drift and started
a fresh exact-source rebuild/run; menu code was then held. This overlap is not
usable runtime evidence for either acceptance claim.

Replacement fixed-source `menu0923` r7 on HEAD `0176cfda` compiled the Room
Level Editor retirement, Forge native bot behavior, and scoped smoke cleanup.
The client SHA-256 is `23540AA7A9F0521A4E986C360B59AC5F1F9E08C6A83E02C42F37A91FCC5A3161`;
the test SHA-256 is `B3159214FBD6F10C185E2DB6ABD12F651C5BFC15B3C5094CE60EDD30EDFCC4A8`.
Pre/post source manifests `preclient-r7-manifest.json`,
`postclient-r7-manifest.json`, and `posttests-r7-manifest.json` show zero drift.
`menu-graph-r7-junit.xml` passed 846 assertions, broad
`input-menu-settings-r7-junit.xml` passed 9,732, and
`smoke-cleanup-r7-junit.xml` passed 21. Full `full-r7-junit.xml` remains RED:
25 failed cases in 131,208 assertions. Most are asset and networking source
contracts reported to their active owners. Three menu/tooling/cutscene guards
were stale after prepared Mod Manager Apply, action-map glyph lookup, and the
separate single-client smoke launch; current-source corrections are awaiting
a stable-source rebuild and replacement full result. Keep the full red receipt.

Ordinary virtual-Agent r5/r6 reached the Agent Select pool but never acquired
Windows foreground ownership: the live probe found a visible/enabled target
HWND with game-thread active/focus while `GetForegroundWindow` returned 0 and
`SetForegroundWindow` failed. No scripted input event fired, so there is no
controller, MKB, or visual acceptance from those runs. The probe receipt is
`.claude/session-builds/menu0923/virtual-agent-focus-probe-r2-console.log`;
the original failing receipts remain in `.claude/smoke-verify-runs/`. A
process-local SDL controller exclusion kept the user's physical assignment
unchanged. Physical controller must be tested separately with it removed.

On fixed HEAD `55ca686c34b76451cb6d285774bd0fafe58b48d9`, isolated
`menu0923` client SHA-256 `BE50884F8751F57AA88508093B7AB134FB21187F12093CC6C28AD6D371FD6772`
and tests SHA-256 `543371AB0F79FC5C007EA553AAEB9358B57301EF5E5F7ADBDD3C0D169544DDF0`
compiled after the generic dialog graph repair, social placeholder removal,
and Forge count repair. Source hashes were unchanged during the client build;
only context/Workbench state moved during the tests build. Focused
`[menu_graph]` passed 846/846 assertions, and current-source broad
`[input],[menu],[settings]` passed 9,732/9,732 assertions across 234 cases.
Receipts are under `.claude/session-builds/menu0923/` as
`menu-graph-r2-junit.xml`, `input-menu-settings-broad-junit.xml`, and the
pre/post source manifests. The first client compile failure (missing nav-input
include) and first focused static-guard failure (1/844) remain in the build
log and `menu-graph-junit.xml`. The 2026-09-22 broad RED4 is superseded for
this current source by the fresh broad pass; it is still historical evidence,
not a runtime claim. Ordinary journeys and device/visual proof remain open.

The first 2026-09-23 virtual-Agent rerun accidentally installed a stale
non-session binary and cannot be counted. The exact `BE50884F` run reached the
CI intro but its 90-second Agent readiness wait expired before the cutscene;
the fixture now waits up to 180 seconds for Agent readiness without changing
events or assertions. The next exact-binary run was terminated during
extraction by another session's smoke runner: `run.ps1` swept all `--smoke`
clients globally even though the sessions had different FIFO resources. That
cleanup is now scoped to PIDs launched by each runner, with parser, mocked
process isolation, and `[b927]` PASS21/21. Replacement ordinary menu results
are pending. Red receipts remain under `.claude/smoke-verify-runs/` for
`results-20260923T142548Z.json`, `results-20260923T143003Z.json`, and
`results-20260923T143229Z.json`.

## Live source matrix refresh - 2026-09-23 (in progress)

The 2026-04-03 `context/designs/menus/menu-inventory.md` is a historical
replacement plan, not a current rendering or acceptance verdict: its OG,
forced OG, and Stub labels predate the ImGui hotswap and current menu graph.
The table below is seeded from the **current** `port/src/menugraph.c` node/edge
registry and `pdguiHotswapRegister` calls. Entries are routes, not a claim
that every child popup or widget has passed. `Open` means the full focus order,
scroll, editor, glyph, persistence, failure, and two-device checks remain to
be recorded. A node with no direct graph parent may still be pushed by legacy
game flow or a named ImGui renderer; registration alone is not reachability.

| Reachable route family | Live entry and exit | Action/widget surface to walk | Current gate |
| --- | --- | --- | --- |
| Main Menu, Solo view, Stats view, Grid | Main root Play/Stats/Grid; Back or scene/quit edge | Top-level cards, subview return focus, close/quit | Open: controller/MKB and rendered sizes |
| Settings, CI Options | Main root Settings or pause Settings; Back | Tabs, every binding slot, profiles, save failure/retry/leave, sliders, combo, glyphs | Open: restart, failed save, physical input |
| Agent Select/Create | Main Change Agent or startup picker; Back/Create/Save/Cancel | Profile list/context menu/delete confirm, name editor, body/head selection | Open: virtual and physical journeys |
| Solo mission, pause, inventory, options, result | Play > Solo Missions; gameplay pause; continue/retry/menu | Mission/difficulty/briefing, abort confirm, inventory, options, results | Open: ordinary transitions and scale captures |
| Room, MP setup/settings/team/bot/player config | Play > Combat Simulator or host/join room; Back/Leave/Start | Lobby slots, modes, weapons, map, team, bot, match start/leave | Open: MP owner coordination and two-process flow; data-only Room Level Editor retired per Mike, three live tabs need rebuild/walkthrough |
| MP pause, soundtrack/tunes/team names, advanced, results | Match pause or room setup; Resume/End/Done/Back | In-game controls, music lists, team text, advanced panels, result return | Open: room/result journey |
| Social shell/lobby, network/joining | Main Social or Network; Back/Disconnect | Friends, invitations, lobby, host/join/reconnect and errors | Open: MP owner coordination and failures |
| Cheats and generic warning/success dialogs | Main Cheats or contextual push; Done/Cancel/Confirm | Category selection, checkboxes, modal flags, focused choice | Client + static gate pass; ordinary modal journey open |
| Training, Firing Range, Device/Help, Hangar | Training routes from main and contextual lists; Back/Begin/Continue | List selection, difficulty, info, details, result return | Open: focus/scroll and ordinary flow |
| Mod Manager, Modding Hub, Forge | Main Mods/Hub/creator entry; Back/Apply/Discard/Retry | Installed mods and partial failures, all editor tabs/text/numeric inputs, export | Open: Agent switch, restart and asset-owner coordination |

The [per-node matrix](menu-node-matrix-2026-09-23.csv) lists all 45 current
graph nodes, exact graph actions, inbound graph edges, and the still-open
widget/focus/scroll/edit/glyph/save/failure/runtime checks. Source registration
also covers typed dialogs and popups outside those nodes. The
[registration matrix](menu-dialog-matrix-2026-09-23.csv) lists 145 named
hotswap dialog definitions and marks reachability and controls unverified;
registration alone is not a player route. The old local 2-player
split-screen routes and debug-only screens remain outside player-facing
acceptance. The matrix must be split down to individual reachable child
screens and completed with focus order, glyph, save, and failure observations
before T-MENUS-003 can be marked validated. The current-source broad pass above
replaces the prior broad RED4 test verdict; it cannot replace runtime evidence.

## Pause checkpoint - 2026-09-22

Mike requested a scoped commit and push, then a pause while other sessions
continue. The queued virtual Agent menu smoke was cancelled before start;
there is no new controller runtime result. The last connected menu0922 build
and focused raw-key/menu tests passed (12 cases, 957 assertions), while the
earlier broad input/menu/settings run remains red on four stale source-contract
assertions; the vehicle protocol assertion still belongs to its owner. The
physical Xbox controller occupied player 0 in the last ordinary run, so the
virtual attachment was correctly refused before any button event. Controller,
MKB, rendered, and complete menu-journey acceptance remain open. Resume from
the T-INPUT-008 virtual Agent smoke with a process-local SDL exclusion only
after coordination and fresh source/binary checks; do not repeat the same
blocked fixture unchanged.

## Raw high-button and axis binding repair - 2026-09-22

T-INPUT-008 now has a connected additive VK candidate. Values 0-646 and old
saved names remain unchanged; new captures use distinct Btn23-32 and axis
direction keys. Every physical edge resolves its precise key plus old alias
once, retaining the chosen action through release. The live glyph resolver
tests real physical candidates, so a legacy alias is shown only when a
present button or axis would still dispatch it. Settings accepts and labels
new keys, including precise controller-diagram axes. Four default left-stick
digital movement binds were removed to retain smooth analog movement.

Isolated menu0922 client/updater/tests compile. Initial direct converter/
resolver tests pass 836 assertions; input/actionmap/glyph/custom regressions
pass 1,582. The final broad input/menu/settings gate is red, 4 failures in
9,679 assertions, on unrelated source-contract literals: vehicle protocol,
catalog reset, Agent save, and delete-status OK. No raw-key or profile-codec
case failed in that run. The red receipt is `.claude/menu0922/raw-vk-final-junit.xml`;
The follow-up now carries the precise physical candidate that won a legacy
alias into the glyph label, so a shadowed button is not named as the axis's
prompt. Rebuilt client/updater/tests compile and focused raw-key/menu contracts
pass 12 cases/957 assertions in
`.claude/menu0922/raw-vk-glyph-menu-final-junit.xml`. The catalog reset, checked
Agent save, and retry-aware delete-status tests now pin current production
flow; the earlier broad red remains retained and its vehicle protocol literal
still needs owner resolution before an exact broad rerun. Ordinary raw
joystick/profile/hardware/visual proof is still needed. Existing ambiguous
aliases are not migrated by guessing.

## Physical-source owner and ordinary virtual-controller boundary - 2026-09-22

T-INPUT-008 physical-source ownership is focused-accepted on exact menu0922
client/tests: owner3/26, smoke-owned contract1/53, actionmap regression15/481,
and isolated builds pass. `N-0195` and `.claude/menu0922/accepted-source.json`
record source/binary hashes. Raw high-button/axis VK separation, glyph winner,
and full ordinary input acceptance remain open.

The exact `menu_virtual_controller_agent_cancel` ordinary-client smoke first
stopped 11/29 at Agent Select readiness after the desktop took input focus.
An opt-in `-MaintainForeground` runner path then acquired the unique game HWND.
Its first attempt stopped immediately on an empty screenshot-schedule argument;
the corrected runner's second attempt satisfied real `agent_select_ready`
after 250ms stable focus/menu ownership at 86.73s. It then stopped 11/29
before any virtual button: the attached Xbox One Controller was already player0,
so the harness rejected virtual attach as designed. Preserve all three results
(`results-20260922T221435Z.json`, `results-20260922T222131Z.json`,
`results-20260922T222357Z.json`) and leave the user's hardware assignment
untouched. No virtual-controller, physical-button, or graphical acceptance is
claimed. A later controlled run needs a vacant player0 before controller attach.

## Activation core2 PASS - 2026-09-08 21:15 ET

Actual disposable runtime PASS35/35 in79.5s, exit0, full049E558F/client811BE779
unchanged. Receipt .claude/menu0908/activation-core2-exit.json; console and outer
logs retain exact actual API witnesses. Real discovered restart package:
disabled activation first-save failure retained enabled1/dirty1; same owned-plan
retry saved components and JSON/machine mask3, confirmed through the real JSON
reader. Already-enabled activation also persisted after an injected save failure.
A real archive move caused package-load failure after saves/unload; restoring it
and retrying retained the original loaded1 restart-folder baseline (desired0,
loaded1,pending_restart1). FT's real pending queue retained failure/error/plan;
retry cleared it only after saved activation; explicit decline freed the retry
without rolling back enabled1/dirty1. Native12 exact union, client build and guard
passed. Prior source-contract reds and core1 setup red remain preserved.

All menu source/runtime holds released; menu stays native QUIESCENT for root's
separate installed22. Core2 does not prove importer/FT rendered navigation,
Agent-switch cleanup, physical controller/MKB behavior, actual network download,
process-restart completion, or the pre-install existing-archive rescan gap.
Raw VK collision/general multi-source hold ownership remains the next input unit.

## Activation runtime setup correction - 2026-09-08 21:11 ET

All12 focused cases accepted through preserved7+3+2 receipts; exact union is
activation-focused12-acceptance.json. Source guard PASS. Actual activation-core1
ran70.3s/18of35 assertions: original baseline real save failure/retry passed,
then new manifest setup failed before changed-selection/late/FT probes. Its
directory exists but no mod.json; no activation product failure is established.
Receipt activation-core1-exit.json/source3B43D205/client4A00D186 unchanged.
Smoke-only correction replaces fopen wbx with explicit exclusive CRT open and
stage/errno diagnostics. Source049E558F; native12/tests38BED245 carry. New
client/guard/corrected runtime pending; root/MP source held, downstream root
installed22 queue cancelled before run. All previous static reds retained.

## Activation combined checkpoint - 2026-09-08 20:49 ET

21:02 R3: fresh client4A00D186/tests41D5DBCA builds PASS, full69AED357 unchanged.
Exact delta was root test plus menu pending-departure early return and MP stale
modal closure. Remaining2 again stopped in universal walker, now exact multiline
arena distribSetPrimaryFromFile assertion at13216; texture1unrun. Accepted10
retained, no guard/runtime. Root owns formatting/line-ending class diagnosis
before another test-only correction; menu/MP QUIESCENT. Receipt
activation-r3-two-exit.json/JUnit preserves this third distinct assertion red.

20:52 R2: only MP test changed, verified delta; tests-only build PASS, fullE4050262
and client212B6654 stable. Failed+unrun5 gave3PASS then root universal-walker
blanket `(void)file_path` assertion failed in loader_walker_anim.c; texture
lifecycle1 unrun. Total10 cases accepted, two outstanding. Receipt
activation-r2-five-exit.json/JUnit. Root owns exact diagnosis/correction;
menu/MP remain held. Guard/runtime still unrun; both original reds retained.

Client and tests builds passed with full2125 source3D302BEE unchanged, client
212B6654/tests31EEE27F. Focused12 stopped at a stale Public Mods source assertion
for the removed bool loader declaration (test_public_mods_static.cpp:186):
7 passed, 1 failed, 4 unrun. Receipt menu0908/activation12-exit.json and JUnit.
Guard and actual runtime were not run. Holds released for MP test-only repair;
root/menu remain QUIESCENT. Preserve accepted7 and matching client; if only the
test changes, rebuild tests and run failed+unrun5 before guard/core runtime.
Final owned source activation-six-r4-settled.json (5D233205) adds real FT queue
failure/retained retry/explicit discard probe alongside early and late Apply.
All behavioral/ordinary-input claims remain pending this checkpoint.

## Installed activation repair - 2026-09-08 20:31 ET

Core/API/importer source applied; verification pending. Activation now returns
a caller-owned persistent plan prepared before enabling, carries publication
status, and retries with the original loaded/restart baseline. Already-enabled
activation also persists. Install API only installs. Importer retains failed
operations and offers Retry/Discard; tool changes and close routes are guarded.
MP owns FT/public-mods migration. A real discovered-package regression is being
added to the actual core smoke; ordinary input/human-visual proof remains open.

20:41 final six-path source manifest is menu0908/activation-six-r3-settled.json
(E327DD1A). The helper rejects session-only rows. Actual core fixture additionally
creates an archive, captures a loaded restart-folder baseline, moves the archive
to cause a real package-load failure after saves/unload, restores it and retries
the same plan. Saved enabled JSON is parsed back with the production reader.
Source only until the common gate: no late-failure acceptance claimed yet.

## Core Apply2 actual runtime PASS - 2026-09-08 20:13 ET

Fixture-only deadline correction passed16/16 in64.1s on unchanged clientC2A2A4B4
and fresh full67A85250; pre/post drift empty. Actual log witnesses .modstate
commit failure at62.75s before runtime/config writes, registry/file unchanged,
dirty1. Retry of SAME owned plan completed component save + JSON/machine mask3,
runtime rebuild and dirty clear. Receipts .claude/menu0908/core-apply2-exit.json,
core-apply2-console.log and core-apply2-outer.out.log. Native2/15+builds+guard were
carried unchanged; initial60s/0events admission red retained. All holds released.
This baseline fixture changes no selection and proves no UI/input, late failure,
Agent switch or restart deferral. MP separately found installed helper retries
can skip failed persistence; menu owns the still-unapplied semantics correction.

## Core Apply1 runtime admission RED - 2026-09-08 20:06 ET

ClientC2A2A4B4/testsEEFE751E builds0, native2/15 assertions and guard0 passed on
fullCE83924A with no drift. Runtime65.6s passed7/16 assertions, events0/3. First
post-boot readiness observation was at63.918s (boot1, stable0), exceeding the
fixture60s wait deadline. No Apply/probe/atomic-save calls executed. Preserve
core-apply1-exit.json, console log, and core-apply1-fixture.json. All holds
released. Fixture-only correction uses90s wait/120s outer timeout from measured
startup64s; a new peer-quiescent snapshot is required before the next runtime.
No unchanged build/native/guard rerun is warranted if native hashes stay fixed.

## Prepared Apply core probe7 integrated - 2026-09-08 20:00 ET

Root batch18 explicitly released and granted the reviewed probe unit. Added
actual-core prepared-plan save-failure/same-plan retry probe, explicit smoke
event and separate core_boot_ready condition with two focused schema/readiness
cases. Existing Agent input readiness is unchanged. Manifest:
.claude/menu0908/apply-complete-review/core-probe/applied-seven.json; isolated
fixture in its fixture/menu_modmgr_prepared_retry.json. Build/native2/guard and
one actual portable runtime pending. Menu quiescent; awaiting explicit root/MP
acknowledgment before fresh full snapshot. This baseline probe does not change
selection or establish UI, late-failure, restart, Agent-change or input proof.

## InterfaceDelete1 applied - 2026-09-08 19:37 ET

19:55 matching clientCF294A6D PASS after r2 exposed two C++-mangled local extern
declarations in the new static helper. Sole-source correction moved both into
global extern C. Settled menu hashD6C5C524 matches root full80E30B41, all2121
inputs unchanged. Receipt .claude/menu0908/interface-delete-client-postcheck.json
and root batch18-client-r3-receipt.json. Earlier audio compile and menu link reds
retained; source/build acceptance does not prove the actual delete/retry UI.
Root native guard also passed with sourceUnchanged=true at23:54:58, receipt
.claude/asset0905-continuation/batch18-native-guard-exit.json. Its installed20
sound run exit0 is separate asset evidence, not a menu input/renderer gate.

19:48 shared batch18 client stopped at audio.c:986 and
catalog_audio_generation.c:105 implicit isfinite declaration. No menu error
reported, but matching client acceptance remains unproven. Root owns correction;
menu source stays quiescent. Preserve batch18-client-build-exit.json and log.

Exact mainmenu.cpp delete state/helper/renderer and include updated. Both full
and partial removal now reconcile actual remaining files through checked sync.
Failure keeps an error and Retry refresh, which cannot delete again or dismiss
on the same activation; Back wins and status wraps. Theme directory deletions
also reconcile catalog entries. Manifest .claude/menu0908/interface-delete-applied.json.
Menu explicitly quiescent for root sound cohort client/guard. No new mirrored
source test added; actual renderer/SDL failure-retry proof still needed.

## Caller4 and voice5 checkpoint PASS - 2026-09-08 19:31 ET

FullFBCC1DBA remained unchanged through clientFB499E55/tests30D27547 builds0,
Weapon source contract1/100 assertions, MP actual adapter5/55 assertions and
native guard0. MP exact five JUnit names matched; controlled transport/SDL and
real Opus/signatures do not prove audible/device runtime. Receipts:
.claude/menu0908/callers-{client-postcheck,tests-postcheck,weapon1-exit,native-guard-exit}.json
and .claude/mp0908-repairs/followups/voice/voice-transport5-exit.json/name-audit.json.
All holds explicitly released. Prior policy6/chat9 were carried without rerun.
Caller source repair is connected and compiled; actual reload/install/Weapon
failure journeys and full ordinary input acceptance remain unproven.

## Checked alternate caller4 integrated - 2026-09-08 19:27 ET

Root batch17 released and granted exact caller unit. Four paths applied from
reviewed hunks; source manifest in apply-complete-review/callers/applied-four.json.
Checked reload/sync preserve errors; reload does not clear dirty or transition
stage on failed rebuild. Installed activation verifies actual selection and
checked Apply/sync before reporting success. Weapon Save keeps error/editor on
failed Apply and distinguishes restart-deferred success. No source-file rollback
is claimed. Compilation/affected contract/guard and actual runtime pending.
Menu explicitly QUIESCENT; MP transport7 also quiescent for fresh shared cohort.

## Apply r3 settled checkpoint PASS - 2026-09-08 19:19 ET

All three owners explicitly quiescent before fresh full manifest D9048BFF.
Client E69F3AF4 and tests09DD35F0 built successfully; full-source checks show
no drift. Remaining menu5 passed all5/139 assertions; MP group policy6 passed
all6/40 assertions; native-source guard exit0. Receipts are
.claude/menu0908/apply-r3-{client-postcheck,tests-postcheck,main5-exit,policy6-exit,native-guard-exit}.json.
Previously accepted actual catalog-plan4/51 assertions was not repeated.
Earlier main5 source-drift preflight red remains preserved below; it is now
superseded for remaining-case execution by this coherent checkpoint.
All source/CMake/build/test holds explicitly released to root generation14.
Main Apply core/ImGui failure and retry journeys, alternate checked callers,
ordinary input and physical-controller acceptance remain open. Native and
source-contract passes do not constitute those runtime journeys.

## Apply r3 settled checkpoint PASS - 2026-09-08 19:19 ET

All three owners explicitly quiescent before fresh full manifest D9048BFF.
Client E69F3AF4 and tests09DD35F0 built successfully; full-source checks show
no drift. Remaining menu5 passed all5/139 assertions; MP group policy6 passed
all6/40 assertions; native-source guard exit0. Receipts are
.claude/menu0908/apply-r3-{client-postcheck,tests-postcheck,main5-exit,policy6-exit,native-guard-exit}.json.
Previously accepted actual catalog-plan4/51 assertions was not repeated.
Earlier main5 source-drift preflight red remains preserved below; it is now
superseded for remaining-case execution by this coherent checkpoint.
All source/CMake/build/test holds explicitly released to root generation14.
Main Apply core/ImGui failure and retry journeys, alternate checked callers,
ordinary input and physical-controller acceptance remain open. Native and
source-contract passes do not constitute those runtime journeys.

## Apply checkpoint partial: external source drift - 2026-09-08 19:02 ET

Client03D01E6C build0/9s and tests727D97D4 build0/24s passed on frozen full
027B0178. Actual catalog-plan4 passed23:01:19 with zero pre/post drift on
catalogA009EC7C; receipt apply-catalog-plan4-exit.json. Main5 then aborted
preflight23:01:45 before any test execution because group_session.h/.c changed
at23:01:35/38 during the explicitly held source window. No main5, guard or
runtime execution followed. Receipt apply-main5-exit.json preserves the red.
MP and root directly notified; ownership/source-settle diagnosis required before
a fresh manifest/build. Do not repeat unchanged catalog4; five cases remain
unrun. This is not a menu product failure or complete Apply acceptance.

## Main checked Apply/retry integrated - 2026-09-08 18:56 ET

Ten canonical paths applied after MP Join-r3 terminal release. Manifest:
.claude/menu0908/apply-complete-review/applied-ten.json. A prepared plan owns
component intent, Agent identity, package identities and original loaded states
for restart deferrals. Main UI confirms actual registry publication, persists
component/config choices before runtime rebuild, consumes checked retirement/
scanner/package/replay failures, and retains plan/intent for Retry. Desired
registry bits are restored after failed rebuild; dirty state is not cleared.
The UI records its actual post-attempt baseline, guards departure even if the
registry already matches desired intent, and warns Discard does not undo earlier
saves/runtime effects. Editing waits for Retry/Discard after incomplete Apply.

Compilation, actual catalog-plan4, selection-retry2 and affected UI/focus/family3
are pending a coordinated source freeze. Main5 and catalog4 case lists live in
the review directory. This is connected source, not accepted runtime behavior.
Other Apply/reload/sync/install/authoring callers still need checked propagation;
ordinary loose/archive failure/retry and real ImGui/controller proof remain open.
Private preparation scripts are now historical and must not be rerun against
already-integrated anchors. Initial patch attempt failed on PowerShell/Python
Unicode output before mutation; ASCII JSON transport preserved the exact text
for the successful atomic patch. No source content was lost or replaced wholesale.

## Full Apply private preparation / runtime boundary - 2026-09-08 18:39 ET

Private actual code fragments in .claude/menu0908/apply-complete-review now
prepare an owned component plan and a post-attempt installed-selection baseline.
Four actual-catalog/file draft cases and two selection retry draft cases are
written but unrun. Canonical product sources unchanged during root hold.
The plan saves desired component intent before runtime mutation; retry keeps
owned IDs/path after catalog retirement. UI needs an explicit incomplete-Apply
guard even when registry state already matches desired selection. Core wrapper,
checked rebuild/Apply status and full ImGui integration remain to be written.
README records integration and remaining proof; this is not implemented Apply.

Independently inspected root installed result results-20260908T223845Z.json:
FAIL98.6s, inner15/18, outer15/23, command folder late rejection case failed.
Observed game PID32108 live at18:38 then missing; no restart performed. Root
owns diagnosis. This is not ordinary menu/Apply evidence. Matching client and
prior component15/save-status6/native guards remain bounded historical proof.

## Matching client verified - 2026-09-08 18:32 ET

Batch15 client build exit0, binary B52B721F, full native manifest3B8304A3.
Independently checked all16 unique paths from component9/save-status7/scanner2
against the manifest: all present/current, no full-source drift. Receipt
.claude/menu0908/batch15-client-postcheck.json links the root client exit receipt.
This compiles the production core adapters and real audio/chrome UI code.
Prior component15 and save-status6 remain separate direct native acceptance;
actual authoring error presentation, complete Apply/retry and controller paths
remain unverified. Scanner status has client compilation, not filesystem proof.
No unchanged native reruns requested. Root tests/runtime and MP Join-r3 hold
remain active; no further product writes until explicit release.

## Scanner failure status integrated - 2026-09-08 18:29 ET

Root explicitly released ScanBotVariants and the aggregate ScanComponents path
overflow branch. These now report negative failure rather than silent success.
Optional absent bot_variants remains0; invalid/open/read/stat/path/parse or
registration failures return -(admitted + 1), preserving partial-admission
information without rollback claims. Header contract updated; root batch and
archive helpers preserved. Manifest .claude/menu0908/scanner-status-review/
applied-two.json. Compilation and actual filesystem/client cases pending.
Root owns the next coherent tests/client/runtime batch; full checked Apply
canonical integration is deferred until that batch is terminal.

## Checked save-status6 PASS - 2026-09-08 18:24 ET

Native six cases/79 assertions pass on tests0C1AEA5F and frozen full manifest
1BB2179A, with no missing/failed cases or source/binary drift. One tests build
passed in4s; native source guard also passes exit0/no drift. Receipts:
.claude/menu0908/save-status6-exit.json, its JUnit, save-status-tests-postcheck.json,
and save-status-native-guard-exit.json. The production executor is tested with
controlled callbacks: sequencing, phase failures, confirmed prior destinations,
absent Agent, mid-save identity changes and aliased retry identity. This does
not execute modmgr.c's actual config/Agent adapters or the authoring UI. Matching
client and ordinary failure/retry remain pending root's coherent import cohort.
Source hold released promptly; MP owns exact next movement CMake entries.

## Checked save-only status integrated - 2026-09-08 18:16 ET

Seven paths in .claude/menu0908/save-status-review/applied-seven.json add a
production save orchestrator and checked modmgrSaveConfig adapter. It returns
the failing phase, confirmed destinations and owned Agent identity; stops on
JSON/config/Agent failure; no active Agent is inapplicable; expected identity
supports retries without saving another Agent. Six direct orchestration tests
are added but not compiled/run yet. Actual config and Agent adapters normalize
their opposite success conventions. No cross-file rollback is promised.

Two audio import paths and chrome save now report saved asset files but incomplete
settings after persistence failure, reject absent registry rows, and withhold
success on those paths. Audio import success sound moved after checked persistence.
These are integrated source changes, not real UI/runtime acceptance. Full Mod
Manager Apply still calls the compatibility save wrapper and lacks complete
failure/retry propagation; scanner/retirement and desired-selection retry remain
required. Root owns the separate modmgrLoadMod archive branch and next common build.

## Component persistence focused15 PASS - 2026-09-08 18:10 ET

One coordinated asset0905b tests build passed in37s. IO8, actual catalog6,
and affected complete-family1 all pass with no missing/failed cases or source/
binary drift. Full manifest component-full-source.json SHA C9A6A2C9;
pd-tests331490B9, dedicated catalog1C803756. Receipts in .claude/menu0908:
component-io8-exit.json, component-catalog6-exit.json, component-family1-exit.json
and their JUnit files. Required native source guard also passes, exit0 and no
source drift (component-native-guard-exit.json). These directly exercise file
validation/merge/atomic failure and actual catalog persistence/replay behavior.
Matching client compilation and ordinary loose/archive/UI acceptance remain
pending: root authorized tests only while its import batch is incomplete.
No full Apply/retry or controller acceptance is inferred. MP received the same
frozen test binary for codec9; its terminal result releases the source hold.

## Component persistence nine paths integrated - 2026-09-08 18:03 ET

Applied the prepared six helper/header/test files and exact modmgr.c, CMake,
and complete-family contract fragments after MP explicitly released Join r2.
Receipt: .claude/menu0908/component-state-review/applied-nine.json. Shared
peer edits preserved; diff whitespace check passes. Compilation and IO8,
actual catalog6, affected family1 execution are pending the next coordinated
source freeze. Matching client and native source guard also remain pending.
The historical candidate notes below describe its design, not current status.
Full checked Apply, UI failure/retry, and ordinary input/runtime acceptance
remain open; these persistence adapters do not close the menu goal.

## Component persistence candidate ready - 2026-09-08 17:45 ET

Private nine-path candidate in .claude/menu0908/component-state-review: six new
file/catalog helper headers, implementations and test files; exact modmgr.c
checked-wrapper/replay-order fragments; the existing complete-family source
contract updated to the adapter and registration-before-replay; CMake IO2 and
dedicated-catalog4 entries. No canonical product integration or execution yet.
Verification lists: IO8, actual catalog6, affected family1. Dedicated runner
catalog-component option uses the existing real catalog/reset-planner target.

The adapter copies including-disabled facts under lock without re-entering it,
uses bundled/temporary/current session-source ownership, preserves unresolved
preferences and reports checked refusal plus earlier effects. The generation
guard is not a general concurrency transaction; caller owner-thread discipline
remains required. Existing Apply core/UI still lack complete failure/retry flow.
After root/MP release, refresh prepare-integration.ps1 against the current shared
baselines, apply exact fragments preserving their other edits, and run the15
focused cases plus matching build/guard. Ordinary installed loose/archive and
UI/controller acceptance remains separate and required.

## Legacy enabled-list4 PASS - 2026-09-08 17:16 ET

The actual production builder passes all four cases/26 assertions on tests
189B1644 and frozen full native2093/EA1D2F1E. Exact2047 bytes plus NUL retains
all32 maximum-length IDs; short capacity preserves output then retries; disabled
and session rows are excluded while order remains; invalid registries reject
without publishing output. Zero pre/post source or binary drift. Receipt:
.claude/menu0908/legacy-list4-exit.json and JUnit. Native guard independently
confirmed exit0/unchanged source at17:17 in batch12-native-guard-exit.json;
matching client B56D8334 verified at17:26 with build exit0 and unchanged historical
EA1D source receipt; current binary and all four owned hashes match. This closes
the bounded repair's build/test/guard checks. Installed weapon15 stopped on61
command admissions; no ordinary menu or gameplay acceptance follows from this build.
Root's earlier unrelated source-contract red remains retained; corrected affected2
passed before this menu gate. No unchanged passing case was rerun.

Component-state candidate8 remains scratch/uncompiled/unintegrated. Full Apply
failure/retry UI, disabled component persistence and full input acceptance remain open.

## Catalog mutation direct gates PASS - 2026-09-08 16:30 ET

Joint test build passes on frozen2087-input manifestE5AC3C2D. The dedicated
actual-catalog10 passes on binary0CF0DC7E; affected lifecycle1 passes on
pd-tests055E7589. Both have zero before/after drift and complete exact-case
coverage. Native source guard also exits0 with unchanged sources. Receipts:
`.claude/menu0908/catalog-mutation10-exit.json`, `catalog-lifecycle1-exit.json`,
`catalog-native-guard-exit.json`. Dedicated tests cover re-enable, rejected
disable restoration, checked partial failure and clear behavior using production
catalog/reset code with controlled lifecycle dependencies. These are not
installed typed-lifecycle or full Apply UI tests.

All menu FIFOs finished immediately. Seven paths remain frozen while root/MP
coordinate the next matching client build, possibly including root equipped3
first. No unchanged menu rerun is planned. Full Apply still needs checked
component persistence, complete rebuild admission, caller/UI retry propagation
and ordinary input/visual proof. Two concise menu release-note bullets added.

## Independent Create Room acceptance review - 2026-09-08 16:26 ET

Root verified all retained hashes in the MP visual-review receipt and read the
same-run log:9/9 SDL events, exit0, room1 creation committed. Viewed the Create
modal and final assigned Room PNGs. The final screen shows Agent (you, leader)
and the room settings. This extends local SDL graphical proof beyond open/cancel
on unchanged clientE36F/source3876C79; no remote-peer, match-start or physical
controller claim. Evidence: `.claude/mp0908-repairs/runtime/host-create-accept-exit.json`,
`host-create-accept-visual-review.json`, `mp-host-room-assigned.png`.

New visual gap: the right roster instruction clips after “X for” at the panel
edge, hiding part of the interaction guidance; the host thumbnail is black.
Both observations sent to the MP owner; wrap/hint repair is for its next source
window, and the black preview remains separate asset/runtime evidence.

## Checked catalog mutation and toggle repair applied - 2026-09-08 16:23 ET

N-0130/T-MENUS-006: both SetEnabled publication and rollback used enabled-only
lookup, preventing re-enable and restoration of a just-disabled row. Both now
use existing including-disabled identity lookup, preserving ordinary gameplay
filtering. New checked toggle/clear APIs report actual failure branches;
negative results can follow partial dependent changes and do not imply rollback.
Old void wrappers remain. A dedicated target compiles actual assetcatalog.c and
the real reset planner with controlled lifecycle adapters; ten cases cover the
real catalog control flow, not full typed asset runtime. One existing lifecycle
source test now anchors the checked implementation with its assertions intact.

Seven paths are APPLIED/FROZEN, UNCOMPILED/UNRUN, receipt
`.claude/menu0908/modmgr-apply-review/catalog-applied-seven.json`. MP owns the
next joint queued tests build after its room-default propagation correction;
menu receives the matching native10 and affected source-contract1 test window.
Checked full Apply/persistence/rebuild/UI retry remains open as described in
`modmgr-apply-review/README.md`; this lower API alone does not close that flow.

## Persistence unit build/guard closeout - 2026-09-08 16:04 ET

The matching game client build exits0; root independently checks all387
manifest entries with zero drift and retains clientE36F3211 in
`.claude/menu0908/modmgr-state-review/client-postcheck.json`. The shared
`batch9-native-guard.log` reports `asset-native-source guard ok`; the combined
audio-conformance receipt exits0 with source unchanged. Persistence14 remains
the exact direct passing gate (no unchanged rerun). This closes source repair,
compilation and focused helper/file-IO verification for the bounded unit.
Ordinary installed restart/UI, component-state persistence and checked full
Apply remain open; the overall menu goal stays active and partial.

## Mod Manager direct persistence gate PASS - 2026-09-08 16:02 ET

All fourteen exact production-helper cases pass (256 assertions), exit0 and
zero before/after source or binary drift. Source387 manifest6C79C9F1 and
tests56796CE5 include the four menu paths and coordinator-owned CMake wiring.
Receipt `.claude/menu0908/modmgr-persistence14-exit.json`, JUnit and exact
passed-name list are retained. This verifies real ordered file load, strict
rejection without mutation, session rows, escaped/max-length IDs and atomic
commit failure preserving previous bytes then successful retry. The checked
missing-file result is distinct from rejected existing files.

The matching client build and native-source guard are pending. This is direct
production helper/IO evidence, not ordinary Mod Manager UI, installed restart,
disabled-component persistence or full checked Apply acceptance. Tests FIFO
finished done immediately; all product sources remain frozen for coordinator.

## Mod Manager four paths applied - 2026-09-08 15:56 ET

After explicit source release, root applied the four non-CMake paths with
baseline and exact candidate hash checks; whitespace checks pass. Receipt:
`.claude/menu0908/modmgr-state-review/applied-four.json` (modmgr985DC7F4,
header9A231D43, helper799FB495, testsE39CA2D1). All four are frozen for the
asset coordinator's shared CMake insertion and joint build. Fourteen focused
cases remain UNRUN; compilation and production runtime remain unverified.

## Independent MP menu evidence review - 2026-09-08 15:56 ET

Root reviewed the peer's retained `host-create-cancel-exit.json`, fixture and
game log; all four retained artifact hashes match the receipt. The log proves
8/8 scripted events and exit0. Root also viewed all three PNGs: Social Lobby
shows Agent (you), 1/32 players and host port27320; SDL mouse opens Create Room;
Escape closes it and returns to the connected lobby with no disconnect prompt.
This is retained SDL graphical open/cancel evidence on client21C5C296 and
sourceF0A104AC, not actual room creation, remote-peer or physical-controller
acceptance. Paths: `.claude/mp0908-repairs/runtime/host-create-cancel-exit.json`,
`host-create-cancel-retained.log`, `mp-host-lobby-fixed.png`,
`mp-host-create-open.png`, `mp-host-create-cancelled.png` in the same directory.

## Mod Manager persistence candidate - 2026-09-08 15:50 ET

T-MENUS-006/N-0125 now has a five-path private candidate in
`.claude/menu0908/modmgr-state-review/manifest.json`: strict full-document
validation before registry publication, saved-order restoration with session
rows pinned, and an escaped JSON writer using the real atomic-save primitive.
The production reader permits CSV fallback only when the primary file is absent;
malformed/unreadable existing files retain the current selection. The writer
previously interpolated raw IDs and could corrupt JSON containing quotes or
backslashes. Twelve focused cases are drafted, including real-file roundtrip,
failed atomic commit preserving old bytes, cleanup and retry. At 15:52 the
candidate expanded to fourteen cases: production file IO is also in the helper,
with missing-vs-rejected results and all 32 maximum-length escaped IDs across
read buffers. All fourteen cases are UNRUN.

All five product/build/test paths remain UNAPPLIED and UNCOMPILED during the
asset owner's source hold. The candidate is handed off for the next sequential
edit/CMake window. This is single-file persistence; disabled-component state,
runtime rebuild failure, checked multi-destination Apply and ordinary UI error
presentation remain open. Do not equate helper tests with complete Apply proof.

## Changed-source terminal boundary - 2026-09-08 15:29 ET

Joint client build PASS: FA930431 on unchanged source3607EB79621 includes the
two menu changes and the asset owner's diagnostic. ONE desktop Agent fixture
with only verbose0->1 then finished RED9/22, events0/10, no GL captures.
Source/client/fixtureBAA7BFD1 remained unchanged. Supported Computer Use found
the exact game window but activation returned `failed to activate captured
window`; one refreshed-target retry returned the same error. No focus-gained
or INPUT.MENU.EDGE witness occurred. AgentSelect/pool/menu ownership and boot
were ready; keyboard/ImGui readiness remained false. Thus the new layout and
input provenance remain runtime-unverified, despite the successful client build.

Receipts: `.claude/menu0908/agent-layout-provenance-exit.json`, outer logs,
process record and `.claude/smoke-verify-runs/results-20260908T192758Z.json`.
Root stopped startup retries and released source/build/runtime for the asset
seed correction and MP14 window. Keep the full menu goal open, including
ordinary routes, save/restart/failure UI, actual layout/preview, physical pads,
Mod Manager core persistence/Apply and raw high-button alias repair. Historical
350+11+4 and the later three MP-affected contracts are separate direct evidence.

## Two-path source repair applied - 2026-09-08 15:21 ET

After explicit asset-owner handoff, root applied the reviewed Agent Create
controls containment/wrapping and verbose-only Accept/Cancel edge diagnostics.
The layout uses a scrollable NavFlattened controls child, actual frame-width
arrows, wrapped names, and separate count/reset rows. It retains all existing
selection, save and Cancel callbacks. No input dispatch behavior changed.
Receipt: `.claude/menu0908/agent-layout-review/applied-two-paths.json`;
Agent Create23AF9A96, ActionMap9872D522. Candidate equivalence and whitespace
checks pass; both files are frozen for the asset owner's one joint client build.
Compilation, actual layout/navigation and input provenance remain unverified.
No full350 rerun is claimed or planned for this layout/diagnostic unit.

## Desktop focus diagnostic and next source unit - 2026-09-08 15:19 ET

The sandbox focus attempt finished9/22 with zero events/activation; supported
Computer Use could not enumerate its window. Scoped process inspection proved
it ran as CodexSandboxOffline. Receipt: `agent-focus1-exit.json`, with the
separate not-performed intervention record in `.claude/menu0908/`.

A reviewed desktop-account launch made the exact test window targetable.
ONE supported activation at19:09:18UTC produced real focus gain at129.33s.
Agent Select stability began129.38s, but the global Accept branch fired Create
in the same frame before250ms elapsed. The fixture itself fired zero events,
so Create is an independent transition, not a passing scripted input. Terminal
result11/22, source/client/fixture unchanged, no fixture GL captures:
`.claude/menu0908/agent-desktop1-exit.json` and results-20260908T190940Z.json.
Keyboard and Create ImGui readiness were true at terminal. Which physical event
supplied Accept remains unproven; no guessed behavioral fix has been applied.

The supported observed Create screen showed long carousel/reset labels
overlapping the preview and a black preview for body `base:dark_combat` / head
`base:head_dark_combat`. Source confirms unbounded manually centered label
drawing and an unconstrained controls group. The one-path layout candidate in
`.claude/menu0908/agent-layout-review/` contains/wraps controls; separate
`input-provenance.patch` logs verbose Accept/Cancel VK edges and focus state
without changing dispatch. Both are uncompiled, unrun and unapplied, awaiting
the next coordinated source edit window. Black preview is handed to the asset
owner separately; this observation does not supersede its specialized tests.

## Agent admission and focus boundary - 2026-09-08 14:59 ET

ONE revised150s/200s Agent fixture finished RED9/22 in151.6s, zero inputs and
no captures, with unchanged source360F43883E4/client1B938609/fixtureEEE7DFBC.
Receipt: `.claude/menu0908/agent-admission150-exit.json`; results:
`.claude/smoke-verify-runs/results-20260908T185859Z.json`.
Agent Select appeared58.41s; real input focus was lost53.35s. Terminal facts
show boot1, menu_input1, agent_select1, agent_select_pool1 and menu_top1,
but menu_key_ready0 and agent_select_imgui0. Thus the previous preboot barrier
is cleared on this unit, while the focus/readiness boundary remains unpassed.

The distinct next diagnostic retains the fixture and all readiness predicates,
and uses supported Computer Use to activate the exact observed game window
once after Agent Select appears. It must observe genuine focus gain and retain
the actual subsequent SDL gestures/captures. No synthetic focus facts, injected
menu state or physical controller replacement. FIFO remains mandatory; the
multiplayer capture is ahead. This diagnostic is queued, not yet executed.

## Multiplayer integration menu boundary - 2026-09-08 14:50 ET

The MP owner's affected49 gate passed on tests DD5D1749/source360 F43883E4.
It includes the three directly affected retained menu contracts: Social Lobby
graph create/disconnect, catalog-native Combat Sim limits/custom weapons, and
the c036 ImGui surface raw-stack-call prohibition. Root verified all three
JUnit cases and retained `.claude/menu0908/mp-affected-menu3-accepted.json`.
No full350 rerun or ordinary-client acceptance is implied. The revised Agent
fixture remains prepared/unrun pending matching client/runtime handoff.

## Mod Manager persistence follow-up - 2026-09-08 14:49 ET

N-0125 records a current-source defect in `modmgrLoadModsEnabledJson`:
it disables live registry rows before validating the document, accepts EOF
as a closing array boundary, skips commas without grammar validation, ignores
other/error tokens, and reports success. The shared tokenizer also accepts an
unterminated string as STRING. Truncated or malformed saved lists can therefore
publish partial/empty enabled intent and suppress config fallback. This is
source evidence; no malformed-file runtime test has run.

The checked persistence unit must parse a complete ordered candidate first,
validate strings/lengths/separators/closing boundary/trailing content, then
publish enable state and order. A rejected document must leave live state
unchanged. Preserve the existing unresolved-ID requirements. Also reconfirmed
the disabled-only writer uses the enabled-only catalog iterator, state replay
precedes later mod registration, and UI Apply still treats the void core call
as success. Settings' checked config serializer is now available, but these
Mod Manager paths remain unfixed. Product sources remain held for joint gates.

## Ordinary runtime boundary - 2026-09-08 14:30 ET

Specialized body/head/hand9 passed on clientBB6F044D, but the ONE unchanged
ordinary Agent fixture timed out its90s readiness wait before menu admission:
boot0,0of10 events,no captures,8of22 assertions. Normal startup was still
activating weapon metadata at92.12s. Source347EFF4FE2C, client and fixtureEF129FCD
were unchanged. `.claude/menu0908/current-agent-exit.json` and
`.claude/smoke-verify-runs/results-20260908T183022Z.json` retain this result.
The specialized debug asset probe exits before the ordinary menu admission
barrier, so its success cannot establish the latter. Direct350+save11+FR4 remain
accepted. Root released the common freeze for promised MP20 integration; menu
edits and later Settings/virtual journeys wait for the next coordinated unit.

## Accepted direct checkpoint - 2026-09-08 14:24 ET

All350 distinct selected source/native regression cases pass across retained
cohorts140+56+2+11+87+54, with exact selection equality and no duplicate/missing
names. The dedicated real Settings/config save target passes11/11 and the
actual Firing Range renderer target passes4/4. Every run records unchanged
source and binary hashes. `.claude/menu0908/accepted350.json` is the current
case-to-receipt map; the final common347 source is EFF4FE2C. Prior302 is not
reused for this unit. These passes include all Settings helper and controller
keyboard cases, real failed-write preservation/retry, and native FR dispatch.

The asset owner now holds the released build/runtime lane for its specialized
body/head metadata ownership repair, required guard, client build and installed
nine-case retry. Root menu sources/tests remain frozen. Root's next ordinary
gate is the unchanged Agent Create/Cancel fixture, then Settings keyboard and
virtual controller routes. Full runtime, visual, hardware and remaining core
ModManager/input alias acceptance stay open; Workbench items remain partial.

## Current checkpoint - 2026-09-08 14:20 ET

The reviewed37-path next unit is applied and the client builds. Current350-case
selection retains296 exact passes;54 are pending after a stale Agent popup
assertion was corrected to verify each opening boundary. All8 Settings helper
and15 controller-keyboard cases pass. Dedicated real-config save11 and actual
Firing Range renderer4 cases are compiled but unrun. The controller fixture and
readiness-based Settings keyboard journey are authored and unrun.

`.claude/menu0908/accepted296-partial.json` maps retained tests to exact receipts;
outstanding54.txt preserves the remaining selection. Test corrections addressed
hidden focus setup, stale pointer navigation rectangles, and selecting the
replacement ImGui context. These did not change product behavior. ClientA05B
linked after MainMenu/asset-witness declaration fixes; diagnostic client96DE
also builds. The installed asset probe now completes boot and passes its first
case, then stops at a native-alias identity mismatch. This is not ordinary menu
journey, full asset, visual, or hardware acceptance. Workbench T-MENUS-003 remains
partial; its full scope below is unchanged. The Sep6 receipts below are history.

## Historical Sep6 checkpoints

Latest gate,13:05 ET: all302 selected cases accepted across108+68 retained and
126 corrected/unrun passes, with the affected earlier native case superseded.
The readiness helper takes persistent application focus explicitly; ImGui's
transient AppFocusLost cannot establish that fact after EndFrame. Final312-path
46DAC86C / testsEA58CA28 has no source/binary drift or missing/extra cases.
Evidence: `.claude/menu0906-next/accepted302.json` and referenced receipts.
Shared client2E4448B3 builds in37s. The revised ordinary Agent smoke stops
red8/22 during asset boot:90s readiness deadline,0/10 events, repeated typed
body/head nested-mesh hash failures. No menu inputs or captures ran. Source,
binary and fixture remain unchanged; `next-agent-smoke-exit.json` and
`results-20260906T170953Z.json` retain evidence. The asset owner diagnoses this
loader failure before retry; no ordinary-menu or physical-hardware pass.

T-MENUS-003 preserves the full user goal: functional, sensible menus across
mouse/keyboard and controllers, every relevant action accessible, accurate
current glyphs, and correct input ownership through every transition.

## Current source repair batch

| Boundary | Finding | Owner / proof required |
|---|---|---|
| ImGui navigation | Keyboard arrows and translated controller actions write the same ImGui keys; broad WantCaptureKeyboard also swallows mapped menu keys | Root; actual headless ImGui keyboard/controller/text tests, then ordinary client |
| Confirmation | Accept overrides Cancel focus in shared modal, End Match, and Agent Copy/Delete | T-MENUS-004; production modal behavior and source propagation |
| Settings binding | Capture has no modal ownership; clear is mouse-only; Escape cannot be bound | T-MENUS-005; capture/review/cancel/clear and parent isolation |
| Glyph resolution | Private context roster omits active schemes and can advertise shadowed bindings | Binding worker; same production winner resolver for dispatch and hints |
| Controller scroll | Backend reads suppressed gameplay aim axis | Binding worker/root; dedicated menu-axis authority and normal scroll outcome |

## Full acceptance still required

- Title and Agent Select, create/copy/delete, Main Menu and Play, Campaign
  selection/briefing/inventory/cheats/training, Combat Simulator room/setup,
  online social/connect/modals, pause, results and replay/Theater routes.
- Settings tabs, all input actions, bind capture/clear/conflicts, profiles,
  save/restart/reload, text entry, sliders/combos, and safe reset/teardown.
- Mod Manager and creator menus, context actions, modal ownership, scrolling,
  long content, scaled layouts, and recoverable error surfaces.
- Keyboard-only navigation, mouse focus/click/wheel/back, controller
  focus/confirm/back/scroll, hold/tap, live device switching and glyph updates.
- Physical controller and human visual acceptance remain distinct from
  synthetic SDL, headless ImGui, static and build evidence. V-004 retains
  its historical receipts and outstanding hardware acceptance.

Known next findings: Settings text fields lack software keyboard access;
Mod Manager context actions and modal Back need production repair. Source-only
tests and event-emission receipts cannot prove these user-facing outcomes.

## First verification boundary

The isolated `menu0906` pd-tests target compiled successfully (88 seconds),
including actual ImGui core and production modal/navigation modules. The first
focused batch stopped at 17 cases: 16 passed / 1 failed, 1,076 passing assertions.
The failure in `test_vehicle_observer_layer.cpp` demanded a private Observer
entry in the old glyph context roster. Its exact assertion now checks the
authoritative active-binding API; the replacement gate is pending.

The initial 201-path manifest includes independent asset files that changed
during compilation, plus Workbench event logs mistakenly included by a
case-insensitive path filter. It is retained as an audit artifact, not accepted
as an aggregate source-frozen build. The 25 menu-owned product/test/CMake paths
are now explicitly frozen in `.claude/menu0906-evidence/menu-refreeze.json`
(`E1839608...`). Asset root coordinates the next common product freeze and
shared build; both lanes hold source until their focused gates finish.

The shared 203-path compile completed unchanged. Its menu gate passed 116 cases
and 2,059 assertions before a stale `test_right_stick_scroll.cpp` source check
failed: the check still required the old gameplay-suppression condition. Only
that expectation changed to the dedicated menu-axis/capture/time-scaled path.
A read-only Catch listing identifies 235 selected cases in declaration order:
the first 116 passes are retained, and the corrected case plus 118 unrun cases
are in `.claude/menu0906-evidence/menu-remaining-cases.txt`.
The asset lane also corrected an old audio verifier; final shared tests binary
`0D20EA66...` matches the unchanged 204-path manifest `97B3BFF9...`.
The replacement menu gate waits for the asset gate. An asset wrapper argument
binding error occurred before test execution and requires no source rebuild.

The asset gate subsequently accepted 65 cases across retained and corrected
continuations. On shared 205-path `BD2F74FB...` / binary `38C97FB3...`, the menu
continuation passed the corrected scroll case, then failed its first actual
ImGui confirmation case: Cancel had the expected NavId but was not actionable
with Enter. ImGui's FocusApi deliberately preserves a hidden navigation cursor;
manual activation requires that cursor visible. This is a production readiness
defect, not fixed by waiting more frames.

At 11:10 ET, the agreed eight-path correction made forced default focus visible
in shared confirmation and its warning/Agent/pause/Room/solo/Cheats variants.
Cheats' remaining global Accept override was removed, and No now wins a
simultaneous activation. The actual test asserts visible readiness; its action
bar fixture explicitly models an already navigated body control. The new common
206-path freeze is `.claude/menu0906-evidence/confirmation-refreeze.json`
(`944587A7...`). A matching shared incremental tests build and the remaining
118 menu cases are pending; 117 prior menu cases and 65 asset cases are retained.

The unapplied next-unit drafts live under `.claude/menu0906-next/` (root,
navigation, profiles, glyphs). They are not compiled implementation or passing
evidence. Navigation's original hashes now predate this focus correction and
must be reconciled without overwriting it. Glyphs still need the additional
search for an available later controller trigger; the responsible worker hit a
usage limit before completing that follow-up. Profiles also require review of
legacy optionsmenu callers that ignore checked save failures. The software
keyboard and Mod Manager transaction remain unimplemented.

## Next coherent repair unit

Current accepted checkpoint at 11:23 ET: all **235 distinct selected cases pass**
across retained cohorts 116 + 1 + 50 + 53 + 15. The case-name union equals the
original selector exactly, with no missing or unexpected names. Catch JUnit
section rows are not testcase counts; quoted comma names also require escaping.
The latter eight omitted names were explicitly run before accepting the gate.
Receipts: `.claude/menu0906-evidence/menu-accepted235.json` and
`menu-accepted235-cases.txt`. Final shared freeze: 207 paths `7BC9FB68...`, tests
binary `DD6141B8...`. This supersedes the intermediate pending/red descriptions
above. The client/updater build and ordinary-client outcomes remain pending.

The first shared client build stopped at a Vorbis compile include conflict:
game `src/include/math.h` hid standard double-precision declarations required
by the pinned decoder. Updater passed. Asset root owns the bounded CMake compile
isolation correction; menu sources remain frozen, and no menu case rerun is
justified by that unrelated target include repair alone.

1. **Binding persistence and profile transactions.** `buildBindStr` writes only
   the first context owning an action, so Menu Accept loses to Gameplay Use.
   Normal startup does not load `Input.ActiveProfile`; empty serialized rows are
   skipped and defaults return. `parseBindStr` truncates the trigger count without
   clearing vacated slots, and automatic insertion scans those stale slots.
   Build a zeroed complete candidate per context, preserve explicit empty rows,
   migrate older contextless data, and commit profile label/state only on a
   successful complete load. Verify replacement, clear, conflict and real
   save/restart/reload behavior through the same production parser.
2. **Controller text entry.** The audit found 97 direct text input calls across
   15 UI files. The retired native GBI keyboard is not a live ImGui facility.
   A shared text-entry owner must handle normal text fields and ImGui temporary
   numeric input, preserve validation/buffer callbacks, and offer keyboard plus
   controller entry without rewriting per-field semantics. Controller slider
   tweak versus numeric entry also needs actual widget coverage.
3. **Device identity and glyphs.** Observe meaningful input separately from
   action dispatch so captured typing updates prompts. Track controller family
   and instance, ignore stale releases/noise, clear disconnected identities,
   use words for PlayStation shapes, and preserve custom/accessibility labels.
   Nintendo mappings must honor SDL's current USE_BUTTON_LABELS hint: its
   default already follows printed labels. Alternate-device hints must identify
   the required device and not imply absent hardware is immediately usable.
4. **Mod Manager ownership.** Parent Back ignores child modals; context actions
   are mouse-only; Hub tab shortcuts consume the embedded manager's tab inputs.
   Outer Close/title/outside/tool-switch can bypass pending-change handling.
   Installed-mod toggles/reorders mutate registry state before Discard, so the
   close path cannot truly discard. A manager-owned staged transaction should
   unify every exit and apply/discard path. Fixed 512-entry/64-category/128-error
   UI arrays also silently omit content and need complete dynamic storage.
5. **Remaining shared transitions.** Main Menu/Hub opening suppression clears
   the previous virtual FaceDown but must account for the new virtual input key.
   Middle-click Back currently reaches ImGui cancellation, while modal/menu Back
   wrappers read action-map edges; unify that semantic route. Include these in
   the next frozen input/navigation cohort rather than claiming closure now.

## Additional confirmation and popup propagation findings

These source-confirmed findings are pending the next coherent source unit:

- Cheats' global Accept override and missing No priority were corrected in the
  bounded 11:10 ET source repair; ordinary-client acceptance remains pending.
- Solo Restart opens under `PushID(b)` at `pdgui_menu_solomission.cpp:2944`,
  while popup checks/rendering at 2975/2987 occur outside that scope. Parent
  Accept can also return before modal rendering.
- Agent Copy/Delete opens inside the Agent window or its context popup
  (`pdgui_menu_agentselect.cpp:411/618/626`) and renders after `End()` at 706.
  The context popup is excluded from parent ownership at 369, so Accept/Back
  can load an Agent or pop the parent behind it.
- Room Change Character opens inside its row context popup at
  `pdgui_menu_room.cpp:2614` and renders outside that scope at 3100. Room Back
  at 4399 ignores Bot Settings/Save/Load child owners and can arm Leave.
- Team Setup Back at `pdgui_menu_teamsetup.cpp:402` can pop the screen after
  ImGui already consumed the same edge to close a native combo during NewFrame.

Use deferred popup requests at a stable rendering scope and retain child input
ownership for the whole frame, including native popups dismissed by NewFrame.
Shared-modal helper tests alone do not validate these actual menu call sites.

## 11:42 ET client checkpoint and next batch

The shared client build passes after asset root's bounded Vorbis PCH correction.
Client SHA-256 `C3A64691C58EB63C935BBD43DF93299E3674A8C3B990380B41328085626E73C9`
and 207-path manifest `02A5C91A9A97E7EC84C7102DF1901432C683B2B3EA5434B1F6466236C0FBD863`
are recorded in `.claude/asset0905-continuation/batch4-binary-hashes.json` and
`source-batch4-vorbis-pch-before-build.json`. Asset-specific audio and character
smokes pass; those are not menu evidence.

The ordinary SDL keyboard `menu_agent_create_cancel` fixture terminates red,
8/13 assertions. Its 50s Select preceded Agent Select acquire at 53.10s; Escape
at 60s correctly fired Agent Select Back instead of the intended Create Cancel.
No Create edge occurred. Receipt `.claude/menu0906-evidence/menu-agent-smoke-exit.json`
has empty pre/post drift. All three 1920x1080 BMP captures were inspected: 48s
shows the CI scene before the picker, 55s shows the picker with New Agent and
keyboard prompts, 64s shows the scene after Back. The fixture names do not
describe achieved states. Do not credit this run as Create/Cancel or full menu
proof; do not rerun unchanged. Add an actual menu readiness barrier next batch.

Further source review found native Back+Select can close then reactivate a
slider/combo in the same ImGui NewFrame; C-wrapper filtering alone is insufficient.
The root scratch navigation draft now covers native ownership transitions.
InputText's simultaneous validation/cancel policy and opening held-key ownership
also require explicit coverage. Agent Create independently executes doCreate
and doCancel and needs cancel precedence. Legacy Extended Key Bindings is still
reachable and has 21 generated rows for a 19-entry menu array, plus a missing
device binding fallback to slot zero and unchecked save failures. These bounded
repairs are being drafted with the persistence batch.

Glyph capability review adds predicate lookup across every authoritative binding
instead of falling back after the first missing control. Drafts under
`.claude/menu0906-next/{root,navigation,profiles,glyphs,capability}` remain source
proposals until integrated and checked. The 235-case result applies to the prior
accepted sources, not these drafts. Root released its freeze after the runtime
receipt; the next build requires both roots to establish a new common freeze.

At 11:51 ET, root has applied the navigation five-path draft, profiles eight-path
draft, capability resolver and glyph identity drafts, and actual backend device
observation wiring through normal edit tools. Shared interface deletion also
has owned Back/default Cancel and outer-scope result reopening. Root helper,
menu readiness and Mod Manager drafts are still pending. No next-batch build or
tests have run. Every status remains partial; source integration is not passing
evidence. The controller text keyboard is still a separate open implementation.

At 12:11 ET, native widget/opening ownership, InputText cancel precedence,
Agent Create cancel precedence, completed-frame menu-readiness facts and seven
test-target source additions are integrated. CMake ownership returned to the
asset root. No next-batch build/tests have run, and no common freeze is claimed.
The revised readiness fixture uses owned Agent Select/Create states rather than
fixed-time assumptions, with native GL captures following each state barrier.

The remaining caller source audit is `.claude/menu0906-next/remaining-audit/README.md`
with an eleven-source hash manifest. Social raw Back ownership, competing
forward/Back dispatch in Agent/Solo/Cinema/Main Menu/Results, Firing Range's
duplicate selection authorities, and Cinema's invalid footer clamp remain open.
Social and the bounded list/selection repairs are scratch-only next-unit work;
controller text keyboard is also scratch-only. Mod Manager staging draft is
under review and has no core disk/runtime rollback proof.

At 12:17 ET the corrected five-path Mod Manager draft is integrated. It stages
Installed edits, checks the entire stable-ID baseline before publication, guards
Hub departures, preserves native popup/Back ownership and visible defaults,
and removes UI collection caps. Root updated the former success-color source
contract for the real modal; asset root appended its two CMake lines. Ten new
helper/caller cases are unrun. Void backend Apply/save status and enabled-only
component-state enumeration remain open dependencies. There is still no new
common freeze or verification result.

At 12:19 ET menu product/test/CMake sources are held ready for the asset root's
common freeze. Remaining caller propagation additionally traces Main Menu Back
through guarded Hub Hide followed by unconditional parent subview change; the
Hub renders later in the backend. This owner conflict is in the subsequent
scratch caller batch, alongside idle paired input and forced-focus propagation.

At 12:29 ET both roots accepted the common 311-path freeze in
`.claude/asset0905-continuation/source-batch5-before-gates.json`, SHA-256
`B7F793B7FF0AB55CB40565E3A819D840189445B9F51FD102414D81810B6FDDE0`.
Root independently read every source hash with zero drift and checked inclusion
of all new menu production/test modules. Asset source checks, native-source
guard and conformance self-tests pass in `batch5-contracts-exit.json`; these
are source gates, not menu runtime proof. Shared isolated tests build is active.
All menu product/test/CMake sources remain frozen until a coordinated release.

Subsequent scratch drafts now cover caller navigation, results dispatch and the
controller keyboard. OSK review caught disabled-field, same-depth popup identity,
held-input transition and pointer-release ownership gaps; corrections are still
scratch only. The remaining Settings source audit also records mouse-only custom
theme actions, missing advertised Color Editor Back, hidden/retried save errors,
Menu Style's 31/32 mismatch and conditional narrow-layout overflow. See
`.claude/menu0906-next/settings-remaining-audit/README.md`. No new visual or
physical-controller acceptance is implied by these source reports.

The first shared tests build stopped red after 37s with unchanged311/B7F793B7;
no C++ tests executed. The error attributed to imgui_internal.h was caused by
our use of its context-dependent NavGamepadActivate/Cancel macros in global
key arrays and two assertions without a local `g`. Root corrected only
`pdgui_nav_input.cpp` and `test_pdgui_widget_navigation.cpp`, resolving aliases
in the current ImGui context. Exact replacement hashes are in
`.claude/menu0906-next/build-alias-correction.json`; all other309 inputs remain
unchanged. Those two paths are refrozen for the coordinated incremental build.
The vendor header and asset/CMake sources were not changed; existing source
guard/Python passes are retained. Original build log and terminal receipt are
`batch5-build-tests-first-red.log` / `batch5-build-tests-exit.json` under the
asset continuation directory.
# 2026-09-08 16:46 ET - Catalog matching client verified; legacy boundary candidate

17:07 continuation: legacy4 applied hashes remain unchanged; no compiler process
or newer build receipt observed, shared handoff still pending. Separate candidate
.claude/menu0908/component-state-review contains dynamic complete-file reader,
merged atomic writer, public header, eight direct filesystem test cases and an
integration checklist. All are scratch/uncompiled/unrun, not game-connected.
Reviewed actual saveAtomic zero-success convention and corrected the draft.
Confirmed main.c already has startup replay; Apply/rebuild replay is too early.
Next integration must preserve authoritative session ownership, copy under-lock
iterator facts without re-entry, propagate checked save/replay failures, and test
real late registration plus UI failure/retry. No full-goal acceptance change.

16:52 independent pair-r2 review: verified every retained runtime/PNG artifact
hash and receipt exit0/no source drift. Viewed mp-room-list-r2-observer-stable.png:
Connected to host, Crispy Monkey [Lobby]1/4 with Join is visible, while Connected
Players includes only the observer and totals1/32. This corroborates MP's open
T-NETWORKING-011 roster defect;31/31 scripted checks do not prove complete lobby
acceptance. No Join action, physical input, human visual or gameplay acceptance.

Root independently verified batch11 client980529BE and all2090 native sources
against 8B2CFF2A, zero drift. The selected_id diagnostic follow-up is compiled.
Catalog10/lifecycle1 passing behavior evidence remains on the original cohort;
no unchanged rerun. Full Apply and ordinary menu/controller acceptance stay open.

Next owned four-path candidate extracts the actual legacy enabled-list builder
into the existing enabled-state helper, accepts all2047 legal bytes plus NUL,
and rejects insufficient capacity without publishing a truncated list. Four
native cases prepared in .claude/menu0908/legacy-list-review/candidate.patch;
Applied16:50 after shared runtime release; applied-four.json records hashes.
Native4 and matching build/guard pending; N-0134 incorporated.
16:57 checkpoint: shared build handoff still pending; unstarted reservation
d1d8434e cancelled so it cannot obstruct another owner. Four sources remain
frozen. Resume with exact4 after common tests build, then verify matching client
and guard. No native/build failure or pass is implied by the cancelled reservation.
