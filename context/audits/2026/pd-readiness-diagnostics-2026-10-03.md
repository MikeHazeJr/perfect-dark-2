# Initial PD readiness diagnostics

Current checkpoint: 2026-10-03 13:44 UTC. Mike's temporary PD floor is applied at
200 GiB with unchanged 4 GiB build growth and 2 GiB safety reserve: effective
build admission is 206 GiB. Mandatory native preflight, queued client/updater
and explicit tests builds, and focused 1498 assertions / 49 cases passed.
Exact six-file diagnostic commit d37aee06 is pushed and fresh origin/dev verified.
Heavy released at 13:07:46 UTC; Git lane released, index empty, 31 peer byte
snapshots unchanged. Temporary policy and context remain outside that commit.
D-011 is decided; D-010 GUI approval and initial fidelity acceptance remain open.
Today's daytime eligibility ends 16:45 Eastern / 20:45 UTC; future days night-only.
Prepared non-GUI gates are complete; no unchanged passes were repeated. The next
ordinary gate is paused for D-010 app approval and D-012 managed-copy disposition.

The parent resumed the approved initial scope for October 2 22:00 Eastern through
October 3 07:00 Eastern (02:00-11:00 UTC). The daytime checkpoint remains history.
HEAD and fresh origin/dev were verified at
`0ca5f1ab471d2675ec14be2b9a900e84c3852efa`; all other edits are preserved.
Coordinator: `01a0f5c1-d031-77b0-a372-3391765df0d5`. N-0349 records current authority.

## Observed diagnosis

Actual Sol 6.1 read-only work completed under `codex-67674abd-4654ac32`, thread
`01a0ff89-4753-7792-8a75-c3b967674abd`, exit zero. The requested model is the
advertised `gpt-6.1-sol`; an earlier incorrect `gpt-6.1` request was rejected by
the service. Launcher/bootstrap/network failures are retained separately under
`.claude/pd-initial-integration/20261003-night/`, not treated as source work.
The failed sandbox worker was stopped with the published normalized identity
helper and its absence verified at 02:12:55 UTC. No game or GUI action occurred.

The retained failed client's log records focus LOST at 38.47 seconds, before
Agent Select opens at 43.51 seconds, with no later GAINED line. Source tracing
connects application focus to both keyboard readiness and ImGui ownership:
`port/src/smoke_harness.c:2067`, `port/src/smoke_readiness.c:220`,
`port/fast3d/pdgui_menu_readiness.cpp:50`, and `port/fast3d/gfx_sdl2.cpp:342`.
This supports the intended focus aperture and does not establish that the
Computer Use denial caused the timeout or exclude simultaneous ImGui blockers.

The canonical fixture waits for Agent Select before controller attachment.
The unchanged run timed out at 180082 ms with 0/17 events, 11/29 assertions and
zero captures. Existing component tests cover readiness prerequisites, stale
frames/popups/focus loss and first Accept activating a row. No product behavior
change is justified by this evidence.

## Bounded source unit

N-0351 delegates only read-only rejection diagnostics to the sole new Sol 6.1
source executor `codex-785df03e-ef90554c`, thread
`01a0ff8f-9455-7341-b6bf-0f96785df03e`, actual source task started at 02:20 UTC.
Exact files: `port/src/smoke_harness.c`, `port/include/smoke_readiness.h`,
`port/fast3d/pdgui_menu_readiness.cpp`, `port/include/pdgui_menu_readiness.h`,
`tests/test_pdgui_menu_readiness.cpp`, `tests/test_smoke_input_events_static.cpp`.
The before manifest records their clean HEAD bytes/hashes/line counts.

The unit exposes focus loss, settling, suppression, binding capture and ImGui
rejection reasons from the same observations used for admission. Predicates,
fixture, timing and stability are preserved. No focus/navigation mutation,
manufactured event or denied app call is authorized. Behavioral tests are part
of the candidate; no test/compiler/game operation is admitted to the worker.
Implementation attribution: `gpt-6.1-sol`; coordinator evidence: `gpt-6`.

Frozen at 02:25 UTC; executor exited zero at 02:27:09 UTC. Root independently
verified all six hashes/line counts/LF endings/newlines/footers, scoped diff and
public-source guard at 02:28:49 UTC. Exact native queries confirm all six owned
launcher PIDs absent at 02:29:06 UTC. N-0353 is the source handoff; N-0354 records
ownership returned to root. Added two ImGui behavioral cases and one static
production/log path case remain unrun. No compiler or client acceptance is claimed.

The observation has 13 independent rejection bits. An unavailable pointer ends
inspection, while observable simultaneous blockers remain in the mask. Zero mask
preserves the original ownership decision. Keyboard suppression/capture is `-1`
when an earlier predicate prevents evaluation: the binding query may cancel stale
capture, so preserving its short circuit matters. Wait logging reads these
captured fields rather than repeating the queries. The six appended log fields
fit the existing 2048-byte log buffer and retain the prior prefix/facts tokens.

## Validation and remaining gate

The earlier Astral allocation through 03:20 UTC and PD queued request are history.
Parent confirmed Astral's early release at 02:34 UTC and granted the next finite
30-minute PD turn, hard cleanup deadline 03:15 UTC. Root rechecked ownership,
queue and source hashes, then started heavy at 02:42:55.073 UTC. Its stricter
30-minute deadline was 03:12:55 UTC. N-0356 records the superseding allocation.

The queued isolated `pd1001init -Target all` command refused mandatory disk
preflight: free 285155368960 bytes (265.57 GiB), floor 322122547200 bytes (300 GiB),
pending build 4 GiB and growth reserve 2 GiB. Wrapper exit 1 / preflight exit 2;
compilation, tests, game and GUI were not started. Observed additional required
headroom is 43409629184 bytes (40.43 GiB). No policy, pin or budget changed;
no evidence was evicted and the previous 93 GiB cleanup was not repeated.

Native account query at 02:44:41.991 UTC verified zero owned session build/test
and managed game processes. Build FIFO was finished failed; heavy FIFO
`q_20261003021948054_99e2bbeb` was finished failed at 02:44:43.790 UTC and all
owned queues cleared. Astral was notified that its next turn is available.
D-011 records restoring approved headroom versus deferring native validation;
N-0357 records the actual outcome. The frozen six-file unit remains unbuilt,
untested and unpublished. HEAD remains `0ca5f1ab`; index empty. The latest parent
publication instruction permits only the six sources after supported checks;
mixed context, audit, Workbench and peer edits are excluded.

After approved storage headroom is restored and a fresh finite turn is granted,
the same candidate needs queued isolated `pd1001init -Target all`, then one
`run-pd-tests.ps1 -NoBuild` union of readiness/input/ImGui widget selectors.
No game, GUI or capture is included in that headless gate.

D-010 remains open. The exact actionable gate is valid user-managed approval
for the PerfectDark-specific Computer Use prompt, or explicit GUI deferral.
No live prompt or public pending request ID is confirmed. The new launcher warns
that `computer_use.windows.always_allowed_app_ids` is ignored; this is not a new
app approval. No settings/grants/config changes or denied action retry occurred.
After valid approval, ordinary preview also requires a fresh coordinated window.

Initial visual/input/fidelity acceptance remains incomplete. The graph inventory
retains its initial boundary (27 operations, 67 references, 53 definitions);
broader graph migration and multiplayer completion remain later work.
The original night-only 11:00 UTC stop is historical for October 3; the explicit
daytime authorization and today's 20:45 UTC pause below supersede it.

## Where to look

- `.claude/pd-initial-integration/20261003-night/readiness-diagnosis-4/`: actual
  read-only model response, bounded console, final diagnosis and exit receipt.
- `.claude/pd-initial-integration/20261003-night/resume/`: exact launchers/prompts
  and six-file before manifest.
- `.claude/pd-initial-integration/20261003-night/diagnostic-source-2/`: candidate
  executor control, bounded console, final response and zero-exit receipt.
- `.claude/pd-initial-integration/20261003-night/diagnostic-source/`: source freeze,
  handoff, root static/public-source guard proof, native worker closeout, actual
  window, build-storage refusal, native process proof and early heavy release.
- `.claude/pd-initial-integration/20261002-night/preview-execution-3/`: retained
  failed preview, actual app denial and corrected final release.

## Bounded storage investigation, 03:07 UTC

The parent authorized targeted metadata inspection only, with no deletion,
move, floor change or broad disk scan. N-0358/0359 record exact ownership.
The non-heavy `asset-evidence` FIFO completed after the 250,000-entry cap;
no resource remains held. There was no second scan or budget reset.

The 99 selected roots comprise 84 session-build children, known PD menu/smoke
outputs, managed working copies, eight recent September legacy run roots, and
the canonical `Build` directory for reporting only. Of those, 84 were complete;
15 are lower bounds, including five not reached at all. Zero bytes in those
five means uninspected, not empty. Reparse paths were skipped; enumeration
reported no errors. This is a bounded PD inventory, not a disk-use audit.

The observed total is 23.143 GiB of logical file lengths. Classification found
2.851 GiB of `.claude` compiler products beneath `CMakeFiles`: 9,455 individual
paths recorded in `compiler-scratch-metadata.json.gz`. This is metadata only,
with no hashes, allocation/hardlink measurement or active-use proof. Every
entry is ineligible for removal now. PD provenance in sampled CMake caches does
not establish exclusive ownership of historical peer build products.

| Pool | Observed logical size | Disposition |
|---|---:|---|
| Historical `.claude` compiler products | 2.504 GiB | Conditional review only; 8,294 exact paths, no removal authorized |
| Current `pd1001init` compiler products | 0.347 GiB | Keep all 1,161 paths for incremental retry |
| Canonical `Build` compiler products | 0.164 GiB | Report only, outside approved removal boundary |
| Exact binaries/ROMs across selected roots | 14.096 GiB | Retain witnesses; names/sizes do not prove duplication |
| Evidence, previews and configuration across selected roots | 2.686 GiB | Retain receipts, manual previews and source evidence |

The exact proposed removal set is empty: verified safe reclaimable bytes are
zero. Even hypothetical full recovery of historical compiler lengths would
leave 37.924 GiB of the observed 40.429 GiB admission deficit. Allocation,
compression and shared links remain unmeasured, so logical lengths are not a
promise of recovered free space. No sufficient safe set was found in the
inspected pools; uninspected pools have no reclamation claim.

Protected examples include `.claude/menu0927`'s 2.284 GiB of source snapshots
and distinct frozen binaries, plus the `virtual-create-open.bmp` manual preview
in `.claude/smoke-verify-install` (14,860,854 bytes). Managed state uses `pin`
and `full_bytes`: the October 1 preview and October 2 audio full copies are
pinned failed runs; `shared/client` is another failed copy. All three and their
receipts remain retained. Sixteen compacted installs are not full-copy removal
candidates; their reconstruction material remains protected. No existing
session lock was removed or assumed stale; an empty FIFO does not prove that
files are unlocked.

The completed September 30 93.063 GiB cleanup pool was excluded. Immutable
archive blobs and receipts were excluded from reclamation inspection. User
Steam changes were neither inspected nor credited to PD. No files were
deleted/moved and no retention budget, floor, pin, grant or setting changed.

Compact artifacts in `03night/storage-investigation/` (the full directory is
`.claude/pd-initial-integration/20261003-night/storage-investigation/`):
`proposal.json` records the empty proposal and exact compiler groups;
`managed-retention-summary.json` records the correct lifecycle/pin fields;
`targeted-metadata.json`, `metadata-summary.json` and the compressed individual
manifest preserve the bounded findings. Only these small reports and owned
checkpoints were written. Coordinator attribution is `gpt-6`.

D-011 remains the parent/operator headroom-versus-deferral decision. After
approved headroom is restored, request a fresh finite 30-minute headless turn
for queued isolated `pd1001init -Target all` and one focused test union. Until
then, the six-file Sol 6.1 unit remains frozen, unbuilt, untested and unpublished.
D-010 GUI approval is independent; no denied action was retried.

## October 3 daytime authorization and current gate

Parent relay cites Mike's explicit approval in Slack `#updates1791030991.918729`
to resume PD during today's day shift alongside Astral. N-0360 incorporates
that authority. It changes today's eligibility, not resource ownership,
storage policy or GUI approval. Stop/checkpoint at 16:45 Eastern / 20:45 UTC;
future days remain night-only unless Mike changes that instruction.

The one bounded read-only admission at 12:41:45.655 UTC reads canonical policy
and volume availability without traversing archive payloads or the disk tree.
Free space is 270630682624 bytes (252.044 GiB). The unchanged 300 GiB floor plus
4 GiB pending build and 2 GiB reserve requires 328564998144 bytes (306 GiB);
the current deficit is 57934315520 bytes (53.956 GiB). This supersedes the
earlier 40.429 GiB deficit as the current observation; no cause is attributed
to unrelated activity. Full archive admission was not run because the disk
prerequisite already fails. No build attempt or failed-build loop occurred.

The storage-admission FIFO completed and released. All six diagnostic hashes
still match; HEAD is `0ca5f1ab471d2675ec14be2b9a900e84c3852efa`, index empty.
No Group A cleanup is assumed: the last parent verification was zero deletions,
and direct approval would still be required. No cleanup, move, floor/budget/pin
change, user preview, Plex action, denied API retry or setting/grant change
occurred. The frozen diagnostics remain unbuilt, untested and unpublished.

N-0361 also owns a completed independent light evidence audit. Four exact
metadata files total 21,927 bytes; the asset-evidence FIFO is released. The
four-record operational ledger hash matches the retained summary's client
`7cb077e3...`, seed and identity. Seven retained native texture receipts span
five archives and agree on the recorded event hash and successful load/no-ROM
fallback fields in generation `4ae22fa3...`. Event payloads and binaries were
not rehashed or replayed. This proves retained metadata consistency only.

Draw/fidelity, audio listening, played poses/interpolation/root motion/loops,
ordinary UI/devices and full corpus acceptance remain pending. The oversized
3,853,383-byte contact preparation was skipped; existing pending cards remain
preparation. Zero archive/source payload bytes were read, no source changed,
and no new native acceptance was promoted.

Evidence is in `storage-investigation/daytime-admission-20261003.json`,
`daytime-resume-control.json` and `daytime-retained-evidence-20261003.json`.
Possible independent light work is bounded retained ledger/receipt integrity,
fixture/source mapping and labeled capture planning from existing metadata.
Native completion requires approved headroom and a fresh finite shared
30-minute headless build/test turn after ownership/FIFO checks. GUI acceptance
separately requires changed D-010 approval and its own coordinated window.

## Temporary floor, native validation and scoped publication

Mike explicitly chose a temporary 200 GiB PD free-space floor in Slack
`#perfect-dark1791031891.654889`. N-0362 claims the exact configuration and live
retention lines; D-011 records the decided option. Only `min_free_gib` changed
from 300 to 200 in `tools/smoke-verify/storage-policy.json`. Build growth remains
4 GiB and safety reserve 2 GiB, so the effective build threshold is 206 GiB.
The 8 GiB archive cap, three full installs within 8 GiB, pins, immutable evidence,
cleanup permissions and GUI approval remain unchanged. This is temporary and
PD-specific; no global machine/Astral rule or expiry was invented.

Canonical native `Assert-SmokeStoragePreflight -Build` passed at 12:58:39 UTC:
free 270624133120 bytes, pending 4294967296 bytes, floor 214748364800 bytes,
archive 842725830 bytes. The earlier 300 GiB refusals remain historical evidence.
The successful native process query found zero matching external heavy workers;
the query process itself was excluded. An earlier sandbox-denied query and
self-match did not establish idle state. Hub FIFO was next and claimed before
launch. No unrelated process or session was controlled.

Actual finite heavy start was 13:00:30.193 UTC, cleanup deadline 13:30:30.193 UTC.
Queued `pd1001init -Target all` passed client and updater builds. Its actual
semantics do not rebuild tests, so the old verifier was withheld and a separate
queued `-Target tests` build passed before running the focused union. Default
queues and 120-second no-output build watchdog were preserved. The exact native
union `[smoke][readiness],[smoke][input],[input][menus][imgui][widgets]` passed
1498 assertions in 49 cases against the fresh verifier, including the new two
ImGui behavior cases and static observation/log-path contract.

Client SHA256 is `68d9a501b09badd5eefbf659af5bf66f74a5acc04b13d6e9450686c34fd35f68`;
tests SHA256 is `8dfd3e058dba3af8f692fde1c210c5824386cbf59a426fb0ff851cf890f5a6d6`.
Native account query at 13:07:21 UTC confirms zero owned build/test/game processes.
Heavy was released early at 13:07:46.553 UTC (436 seconds elapsed), all operation
queues cleared, and Astral notified. No game, GUI or capture was launched.
The current build is retained as the unique new product/verifier witness; no
automatic deletion or repeated historical cleanup occurred.

After the explicit source guard and peer byte snapshot passed, the separate Git
FIFO was claimed before staging only the six frozen diagnostic files. The normal
tracked pre-commit guard passed. Commit `d37aee06e36009efdde260a2b0d415c22bd8a8fc`
contains 257 additions / 18 deletions across exactly six files. Normal origin/dev
push succeeded; fresh `ls-remote` at 13:14:27 UTC matches the exact commit.
Index empty, all 31 unrelated changed-file hash snapshots unchanged, Git lane
released. Temporary policy, retention/context, Workbench and peer edits were
excluded from this diagnostic publication. Policy remains applied locally and
durably recorded as Mike's temporary decision.

Bounded evidence is in `diagnostic-source/daytime-verification/`: policy before
and admission, native idle, actual window, separate preserved client/tests build
logs and receipts, 211-byte focused test report, native closeout/heavy release,
explicit guard, exact staged/committed peer proof and fresh publication result.
Implementation attribution is `gpt-6.1-sol`; verification/coordinator is `gpt-6`.
No ordinary preview/readiness timeout acceptance is inferred from this headless
gate. D-010 still prevents denied-action retry; draw, contact-sheet, listening,
animation playback and device/fidelity acceptance remain pending.

## Prepared ordinary gate checklist

No unrun independent non-GUI gate remains in the prepared initial cohort.
Broader strict audio/animation receipts and corpus/fidelity gaps stay pending;
the current native producer accepts one model and its declared textures. A new
family producer would require a separately scoped implementation unit. No such
expansion or repeated accepted run was used to fill this blocked interval.
Two active README passages were corrected to the actual temporary PD policy.

1. Mike starts a fresh user-initiated `@Computer` task in the ordinary ChatGPT
   desktop app, targeting visible PerfectDark only, and selects **Allow** on the
   actual PerfectDark permission prompt. **Always allow** is optional. No live
   prompt or pending request ID is confirmed here; absent a prompt, report that
   rather than infer approval. [Official approval flow](https://learn.chatgpt.com/docs/computer-use).
2. Resolve **D-012** before a new managed run. Current metadata has three full
   copies at the unchanged cap of three: two pinned failures and one completed,
   unpinned failed `shared/client`, ID
   `20261002T063150413966Z-menu_virtual_controller_agent_cancel-d8f83de6`.
   The existing seed path would retain that failure privately, then require
   guarded compaction of its full working copy to admit another copy. Current
   no-automatic-deletion authority prevents silently invoking this transition.
   Choose one explicitly verified rotation retaining all immutable bytes/recipes/
   receipts, or retain all three full copies and defer the new run. No move,
   deletion, unpinning or cap change occurred in this preparation.
3. From ordinary Windows PowerShell in this checkout, run the caller-only check
   `tools\smoke-verify\run-interactive-readiness.cmd`. A fresh ready result proves
   desktop prerequisites only. After real app approval and managed-copy
   disposition, obtain a fresh 15-minute parent/shared window and claim heavy,
   game-runtime and capture FIFOs with the supported native executor.
4. The tested client is `.claude/session-builds/pd1001init/PerfectDark.exe`,
   SHA256 `68d9a501b09badd5eefbf659af5bf66f74a5acc04b13d6e9450686c34fd35f68`,
   from published `d37aee06`; focused tests pass 1498/49. Conditional launch:

   ```powershell
   .\tools\smoke-verify\run.ps1 -Test menu_virtual_controller_agent_cancel `
     -SourceBinary .\.claude\session-builds\pd1001init\PerfectDark.exe `
     -SourceRom .\.claude\session-builds\pd1001init\pd.ntsc-final.z64
   ```

   This command was not executed. The canonical wrapper must bind a new immutable
   seed to this binary; old `28b2cf25...` contains the older `7cb077e3...` client
   and cannot attest the new diagnostic build. Preserve the canonical definition,
   180-second readiness, 250 ms stability, 240-second native cap and four original
   captures. No `PerTestInstall`, template, timeout or foreground override.
5. Require actual result/exit plus reviewed `virtual-before-select.bmp`,
   `virtual-create-open.bmp`, `virtual-after-editor-back.bmp` and
   `virtual-after-cancel.bmp`. Native source load does not prove drawing/fidelity.
   Keyboard follows only after preview pass in a separate window; Settings
   edit/save/restart and physical devices remain separate. Stop on a real gate
   failure; no denied-action retry or alternate UI/grant route.

The estimate is 809/900 seconds including setup, native run, finalizer and
release. Actual deadline is start + 15 minutes, never beyond today's 20:45 UTC
pause. PD run admission is 204 GiB (temporary 200 + 2 pending + 2 safety); build
admission remains 206 GiB. Exact machine-readable prerequisites, command and
identities are in `daytime-verification/next-ordinary-gate.json`. No worker,
game, GUI, capture or archive scan was launched; all resources remain released.

## 2026-10-03 15:08 UTC retained diagnostic path

Independent T-TOOLING-010 repairs the pre-existing hosted CI compiler-prefix
lookup. Its approved headless batch passes queued client/tests targets and
1,655 focused crypto/voice/readiness assertions in 50 cases; native zero-process
proof and early heavy release are retained in `toolchain-ci/`. No game/GUI ran.
Before reusing `pd1001init`, original client SHA `68d9a501...` and verifier
`8dfd3e05...` were copied and hash-checked under
`toolchain-ci/retained-diagnostic/`. The prepared ordinary gate's tested-binary
path and exact SourceBinary command now point to that retained `PerfectDark.exe`;
its hash/fixture/acceptance gates are unchanged. SourceRom still names the
existing `pd1001init/pd.ntsc-final.z64`. New local client SHA `b4929c0c...` is
separate native portability evidence. D-010/D-012 remain open and no new seed,
rotation, deletion, pin/cap or approval change occurred. N-0368 and
[portability audit](pd-mingw-ci-portability-2026-10-03.md) preserve the handoff.
