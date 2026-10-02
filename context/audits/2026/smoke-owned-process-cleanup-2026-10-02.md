# Exact owned-process cleanup and initial-scope review

Owner: `01a0f5c1-d031-77b0-a372-3391765df0d5`. Model: gpt-6.

The canonical Agent Select preview launched after mandatory default private
workspace verification. Sky then denied PD app access: `Computer Use was not
approved to use perfectdark`. The worker's deeply escaped path/time checks failed
to stop its process and incorrectly reported zero games when releasing queues
at06:33:13UTC. The unchanged game ended its180082ms readiness timeout:0/17events,
11/29assertions,exit1,0captures. Verified final zero/release is06:36:04.511UTC.
The failed receipt and correction remain preserved; no denied API was retried.

Both canonical wrapper launch paths now persist an exact process identity under
the private install's `logs/smoke-process-ownership/owned-<pid>.json`: executable,
PID, UTC creation stamp, parent, fixture token and managed install identity.
`tools/smoke-verify/stop-owned-process.ps1 -OwnershipFile <absolute-record-path>`
uses this record for cancellation instead of an ad hoc nested shell command.
It accepts only the matching private client install and current owner marker.

The shared helper uses literal separator normalization and explicit UTC/offset
conversion, rejects other installs/parents/fixtures and reused PIDs, and treats
missing metadata or failed CIM queries as uncertainty. It opens and rechecks the
process handle before stopping it, then confirms absence with a successful query.
Cleanup errors propagate; callers must retain leases until exit is verified.
The wrapper also uses these identities for ordinary owned-process cleanup and
restricts related fault reporter command matching to the explicit `-p` argument.

Validation:22checks pass each on PowerShell5.1.26100.9549 and7.6.6. Hidden
disposable shells exercised real Windows CIM metadata, slash/case/UTC JSON
round trips, incorrect identities, query denial, exact termination, idempotence
and preservation of another process using the same executable. All disposables
exited. No game, desktop/input API or permission setting was used. Source/parser
and scoped whitespace checks pass; the native asset source guard passes.
A real PD cancellation using the new helper awaits a permitted runtime window.

Evidence lives under `.claude/pd-initial-integration/20261002-night/cleanup-guard/`
(`ps51/result.json`, `ps7/result.json`). The original attempt is retained under
`preview-execution-3/`, including its actual denial, failed result and corrected
release. D-010 remains open; this tooling correction does not validate the UI.

Independent light review reads only seven retained trace JSON files (12844bytes),
the seven graph-ledger source files, and current Settings source. Four texture
catalog/closure/native-slot joins remain consistent in their original
client4ae22fa3/source46807f6b generation. Observed native texture dimensions are
B1x1, SH64x32, TA64x32 and TB64x64; RGBA byte counts and hashes are recorded per
asset. These are native consumer facts, not source-image or rendering fidelity.
Draw/contact-sheet/fidelity acceptance remains pending. An invalid review export
caused by PowerShell's automatic Matches variable is retained and explicitly
superseded by the corrected export with required-field checks.

The current graph checker passes27operations/67references/53distinct definitions
across seven files. This is lexical inventory proof, not executable graph
migration or gameplay parity. Settings source matches HEAD:16device rules,
3104-byte serialized capacity including NUL. Prior10/82,246/10033 and14/236 native
test receipts remain separate from blocked ordinary UI/restart/device acceptance.
Review artifacts are in `20261002-night/retained-review/`. No public source archive,
blob, corpus, game or UI was accessed by this review.

For the morning approval handoff: the supported ChatGPT desktop Computer Use app
prompt offers **Allow** for the current request and **Always allow** for future
tasks. Choose **Allow** when the normal prompt names PerfectDark. No public pending
approval request ID was surfaced or preserved by the denied, now-closed CLI worker;
its window ID is not an approval ID. A live host approval prompt has not been
confirmed. Do not invent a request ID or retry the denied API to bypass approval.
[Official Computer Use instructions](https://developers.openai.com/codex/computer-use)
document the prompt and Settings > Computer use review controls.
[Auto-review documentation](https://developers.openai.com/codex/sandboxing/auto-review)
confirms that app approvals remain a separate user interaction. No grant/config
changes were made. Initial visual/input/full asset fidelity goals remain open.
