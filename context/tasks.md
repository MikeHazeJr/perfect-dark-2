# Tasks

> Live punch list only. The canonical Workbench is durable project truth for
> status, dependencies, ownership, notes, and evidence. Completed narratives
> belong in [session-log.md](session-log.md).
>
> The pre-consolidation task ledger is preserved verbatim at
> [_old/tasks/2026/tasks-through-2026-08-12-pre-v1-consolidation.md](_old/tasks/2026/tasks-through-2026-08-12-pre-v1-consolidation.md)
> with SHA-256
> `2DDF4541E7B2FD753F296158EB8991831BF4572834D38C00A40545868087F9DF`.

Last updated: 2026-08-26

---

## Canonical 1.0 session route

D-004 defines 1.0 as a complete, stable base game plus usable Theater for
Campaign and Combat Simulator, with one minimal graph-backed sample weapon in
custom Match Setup slots. Forge maps/map variants, broad creator and modding
pipeline tooling, Studio, Needler-specific polish, Queue Match, GPU swarm
extensions, and authority migration are post-1.0.

The canonical trigger is:

> goal: Let's get to 1.0. While working, state which milestone you are working
> on, it's current progress, and ensure you are working to build things
> structurally / infrastructurally instead of hacking things together. We are
> seeking long term extensibility and seamless function, rather than just
> 'technically it works'.

When a session receives that prompt:

1. Complete the repository, context-manager, Workbench, and coordination
   preflight before editing.
2. Read D-004 and T-RELEASE-001, then recompute dependency progress for
   T-RELEASE-002 through T-RELEASE-006 from the canonical Workbench.
3. Select the lowest-numbered incomplete milestone unless it is blocked or Mike
   explicitly changes priority. Claim one unowned dependency that materially
   advances that milestone.
4. Begin every progress message with one plain statement containing the
   milestone, current progress, what is being done, the task goal, and why it
   matters.
5. Implement at shared architectural boundaries with explicit ownership,
   versioning, lifecycle, rollback, and reusable production consumers. A local
   patch that only makes one smoke pass does not satisfy the milestone.
6. Keep statuses partial until their exact completion contracts pass. Reject
   overlapped or source-drifted receipts and record durable evidence as truth
   changes.
7. Never route this prompt to `post-1.0` or `historical-cut` items without Mike's
   explicit scope change.

The exact user instruction is also stored as incorporated Workbench note
N-0036 on T-RELEASE-001.

---

## Milestone progress

Progress below was recomputed from the canonical Workbench on 2026-08-23.
Recompute it at the start of each session rather than repeating stale numbers.

| Order | Workbench item | Status | Current dependency progress | Completion |
|-------|----------------|--------|-----------------------------|------------|
| 1 | T-RELEASE-002 | partial | V-010 has 2 validated, 8 partial, and 5 missing leaf gates out of 15 | V-010 validated/pass |
| 2 | T-RELEASE-003 | partial | 0 of 2 closed; T-THEATER-001 partial, V-011 missing | Theater implementation plus V-011 validated/pass |
| 3 | T-RELEASE-004 | missing | 0 of 2 closed | T-MODDING-008 and V-012 complete |
| 4 | T-RELEASE-005 | partial | 2 closed, 4 partial, and 3 missing out of 9 | Friend-play/NAT/co-op/performance dependencies closed |
| 5 | T-RELEASE-006 | missing | V-013 not run | One source-frozen V-013 release candidate validated/pass |

T-RELEASE-001 closes only through D-004 and T-RELEASE-006. It is not a parallel
sixth work queue.

---

## Milestone 1: complete base game and graphs

Current completed unit: T-ENGINE-004/B-1076. `mpStartMatch` is now the single
Combat Simulator stage-request owner and releases the complete menu/input pool
before stage publication for offline, listen-authority, and receiving-client
starts. Exact client `04DB220E...` passes the ordinary title -> Agent Select ->
Main Menu/Play graph -> Room -> two gameplay/endscreen cycles with real Play
Again 55/55, balanced Agent Select/Room ownership, no watchdog repair, and clean
exit. The same binary preserves the invitee-authority D-003 route at 170/170:
one listen host, one signed typed match-server route, one join, and no
probe/relay handoff. The first ordinary receipt remains rejected at 51/55 only
for its stale `pass` versus emitted `satisfied` verifier strings; both product
cycles completed. B-1076 is now a regression gate.

The preceding completed unit was T-ENGINE-004/B-1067/B-1103/B-1104. The prepared-roster,
ready-gate settings rollback, multi-player stage unwind, and model-complete NPC
wire boundaries now share the protocol-v58 inactive/waiting/release/active
lifecycle. START/READY carry one nonzero epoch, the listen authority and exact
remote peers own separate post-load latches, RELEASE queues one complete fresh
baseline from dedicated reliable storage, and ACTIVE alone admits incrementals.
The exact full NPC resync carries its immediate canonical digest; periodic
mutable-state checksums are gone. Current source-frozen automation is accepted:
all isolated targets build, exact product `03a65922...` (2,719 files) and
verifier `f5f9ba01...` (408 files) remain frozen, final full tests pass
66,421/1,200, and the native-source guard passes. Exact client is `5DA75BE6...`
and the automation-build tests were `9E56A336...`. Current-v58 production
receipts now accept co-op 96/96, Counter-Op 98/98, later-player rollback 60/60,
settings rollback 43/43, initiator authority 214/214, reconnect 99/99, and the
focus-independent invitee-authority route 170/170. The route proof uses the
same exact v58 client and establishes one elected in-client listen authority,
one separately signed typed match-server route, one idempotent initiator join,
and no probe or relay descriptor handoff. Its immutable raw receipt remains
rejected at 169/170 because the original verifier rejected valid single-digit
epoch 1; the corrected definition and separately hashed retained logs pass all
170 assertions, and the compiled route/reconnect contract passes 108 assertions
in 2 cases. Final verifier aggregate `726a2c96...` covers 409 files and exact
tests binary `38C37DC4...`; its only pre-route delta is the corrected route
fixture and static contract, while product `03a65922...` has zero drift. The
reconnect verifier now selects typed `base:cyclone` and expects its actual
weapon 11 instead of coupling a random spawn to stale weapon 5; its focused
compiled contract passes 63 assertions. The integrated invitee-authority run
reaches 210/218 and proves the route/start/gameplay path, but is rejected because
this execution desktop exposes no foreground HWND for B-1085's fresh SDL focus
witness; that independent focus/visual fixture remains unchanged and is no
longer the D-003 route gate. B-1104 is now a production regression gate, while
T-ENGINE-004 remains partial for its broader base-game lifecycle audit.
Milestone 1 therefore remains 2 validated, 8 partial, and 5 missing.
The next default focus is the broader T-ENGINE-004 base-game lifecycle gap
audit; no further D-003 source or smoke work is pending. B-1101/B-1102 remain
regression gates on accepted frozen
automation and Combat Simulator evidence, and accepted Campaign, D-003, and
reconnect receipts remain retained without rerun.
T-TESTS-002 remains validated
after D-005 option A, B-1061/B-1062/B-1063, and the 17-mission restart gate.

Decided Workbench item D-005 owns the architecture: one Agent Profile v3 JSON is
the sole per-agent source for campaign state and preferences. The global
`pd.ini` remains the machine-default source before sign-in. Legacy v2 Agent JSON
and its optional `prefs_<agent>.ini` overlay enter one validated candidate and
atomic current-document replacement; current documents never consult a
sidecar. Create, copy, activate, save, and delete share this lifecycle, and an
active Agent cannot be deleted without a future explicit sign-out transaction.

Asset source invariant for this milestone: Public typed archives are the game-facing source.
Engine-ready products are source-hashed cache only.
Runtime ROM/RomProvider fallback after extraction is an asset-chain failure tracked by c3844 and T-RUNTIME-001.

| Item | Baseline | Required outcome |
|------|----------|------------------|
| A-ASSETS-001 | partial | Current-tree audit of all 27 family rows and every retained field, backed by T-ASSETS-001 cross-family work. |
| T-EXTRACTION-001 | partial | Lossless editable source for every production semantic, with versioned fail-closed emitters. |
| T-RUNTIME-001 | partial | Every accepted public source and graph drives production with no hidden native, loose, or ROM fallback. |
| T-MODDING-002 | partial | Every accepted base weapon/projectile/entity graph deliberately selects its production behavior with parity proof. |
| T-MODINFRASTRUCTURE-003 | missing | Remove AllInOne content from base authority and retain it only as optional typed mods. |
| T-ENGINE-004 | partial | Accepted regression gates retain ordinary listen-host, generated-audit, prior two-cycle Combat Simulator, D-003 rollback, both authority-first friend-play roles (214/214 initiator and 218/218 invitee), and the B-1099 ordinary reconnect production path. Exact reconnect product `7437d77c...` / client `0f377e3e...` retains its 98/98 corrected log proof. Candidate-first player/network allocation, exact Campaign identity and complete-body preflight, typed stage abort, reversible chrbody creation, scheduler-correct Eyespy ownership, and shared transient defaults retain Air Base 25/25 and full Campaign plus restart 23/23 on client `2fc37b38...`. B-1101's bounded decoder and explicit-target followup retain one isolated source-frozen build, focused 2,007/33, full 64,681/1,181, and the native-source guard on product `9592a63d...` (2,715/0 mismatches), verifier `c76aadc8...` (401/0), client `A0F46D54...`, and tests `B4D51B51...`. The exact replacement smoke rejected at 16/56 under B-1102 after schema-v5 reconstruction omitted all per-part flags and the fallback cleared animation ownership before CHRINFO. B-1102 now writes every flags discriminant, validates exact complete streams and rejects trailing bytes, parses canonical range-checked integer/boolean tokens across the full-u32 frame domain, preserves declared topology through explicit zero-frame placeholders, bumps only animation caches to v8, verifies optimized-consumer advancement, and re-enters bind pose with a null-safe scale without mutating `model->anim`. Luna's completed integrated audit found and Sol corrected all four residual edge gaps; production-linked pure JSON fixtures exercise scalar behavior rather than only source strings. The isolated source-frozen client/updater/tests build, focused 2,057/27, full 65,073/1,186, native-source guard, and unchanged product `e6287a9c...` / verifier `ad7c38f9...` manifests pass. Exact client `133A55C6...` then passes the sole replacement two-cycle Combat Simulator receipt 56/56 with real fire/hit, both stats/award cycles, Play Again, clean exit, zero decoder/crash matches, and no leaked process. B-1101/B-1102 are regression gates. B-1076 now centralizes pre-publication Combat Simulator menu/input teardown in `mpStartMatch`; exact client `04DB220E...` passes the ordinary two-cycle graph/Play Again lifecycle 55/55 and the invitee-authority route regression 170/170, with focused 275/4, full 66,690/1,204, native-source guard, and isolated builds retained. T-ENGINE-004 remains partial pending its next broader lifecycle closure audit. |
| T-TESTS-002 | validated | D-005 option A is production-proven: exact v2 plus optional INI migration to v3, complete-schema rejection, activation rollback, active-delete rejection, ordinary Agent Select, fail-closed invalid CLI, all 17 missions through live Credits, and second-client v3 persistence. Final full automation passes 57,277 assertions in 1,070 cases. |
| T-VEHICLES-002 | missing | Complete hoverbike operation across input, physics, lifecycle, and applicable authority paths. |
| T-TOOLING-003 | missing | Durable indexed release evidence instead of transient build-folder claims. |
| V-003 | partial | All-family catalog/provider production use. |
| V-004 | partial | Real MKB/controller navigation, rebinding, glyph switching, and gameplay input. |
| V-005 | missing | One controlled public-source edit changes production behavior for each of 27 families. |
| V-006 | partial | B-1068 reopened scenario JSON loading: candidate-first typed validation, bounded v1/v2 migration, exact rollback, and ordinary-client save/load/start proof remain. Prior extraction and MP setup receipts stay valid. |
| V-008 | missing | Custom game-mode and bot-profile identity through save and listen-host networking. |
| V-009 | validated/pass | Preserve edited effect/weapon gameplay, distribution, lifecycle, and visual proof. |

Current T-ENGINE-004 checkpoint: B-1067/B-1103/B-1104 production gates pass at
protocol v58. The prepared roster aborts before a preparing
participant's valid settings publication, and a later co-op/Counter-Op player
failure reverse-unwinds every committed runtime owner in authenticated
client-ID order. The rejected v57-era runtime batch drove an explicit
inactive/waiting/release/active stage-publication lifecycle: START/READY share a
nonzero epoch, the listen authority and exact remote peers own separate
post-load latches, release publishes only a fresh baseline, and ordinary
gameplay begins on the next ACTIVE frame. NPC convergence is one reliable full
resync followed immediately by a canonical sync-ID-sorted digest of that exact
applied snapshot; periodic asynchronous checksum traffic is removed. The
baseline has dedicated packet storage, consumes its complete pending mask only
after a successful room queue, and cannot release with a missing or partially
built transaction. The digest uses the exact serialized target and
sentinel-terminated room fields. Fixtures assert semantic endpoint roles with
seed-independent pools and baseline-before-ACTIVE ordering. Exact v58 product
`03a65922...` and verifier `f5f9ba01...` pass isolated builds, complete tests
66,421/1,200, and the native-source guard with unchanged post-run manifests.
Accepted ordinary-client receipts cover co-op 96/96, Counter-Op 98/98,
later-player rollback 60/60, settings rollback 43/43, initiator authority
214/214, reconnect 99/99, and the focus-independent invitee-authority route
170/170. The immutable raw route receipt remains rejected at 169/170 for its
stale two-digit epoch regex; corrected static coverage passes 108/2 and the
same retained exact-client logs pass 170/170 without product changes. B-1104 is
a regression gate. Milestone totals do not change until the broader
T-ENGINE-004 closure review.

B-1076 is also a regression gate. `mpStartMatch` owns one pre-publication
`menuStop` plus `RELEASE_MENU_POOL` boundary for offline, listen-authority, and
receiving-client Combat Simulator starts. Exact client `04DB220E...` passes the
ordinary Agent Select/Main Menu/Play/Room/two-match/Play Again fixture 55/55 and
the invitee-authority route fixture 170/170. The first ordinary 51/55 receipt is
retained and rejected only for stale verifier wording; both product cycles ran.
Milestone totals remain 2 validated, 8 partial, and 5 missing because the
broader T-ENGINE-004 base-game lifecycle audit is still open.

V-010 also owns the consolidated regression matrix from [bugs.md](bugs.md),
including campaign and Combat Simulator lifecycle, bot spawn and attribution,
collision, physical input, vehicles, character geometry, long-session stress,
and rendering. The forty-seven live one-off 1.0 bugs are B-919, B-249, B-242, B-174,
B-183, B-1064, B-1065, B-1066, B-1067, B-1068, B-1069, B-1070, B-1071,
B-1072, B-1073, B-1074, B-1075, B-1076, B-1077, B-1078, B-1079, B-1080,
B-1081, B-1082, B-1083, B-1084, B-1085, B-1086, B-1087, B-1088, B-1089, B-1090, B-1091, B-1092, B-1093, B-1094, B-1095, B-1096, B-1097, B-1098, B-1099, B-1100, B-1101, B-1102, B-1103, B-1104, and B-1105. B-1061, B-1062, and B-1063 are locked release
regression gates.

---

## Milestone 2: usable Theater

- **T-THEATER-001, partial:** Replace the participant-only `.pdth` path with a
  versioned, bounded, fail-closed authoritative recording format and lifecycle.
  Record enough match identity, configuration, state, events, and checkpoints
  to reconstruct Campaign and Combat Simulator sessions after process restart.
- **V-011, missing:** Record, stop, restart, list, load, play, pause, seek, change
  camera, stop, and return cleanly for one Campaign session and one Combat
  Simulator session. Cover complete, interrupted, corrupt, truncated,
  oversized, and version-mismatched files. Include listen-host capture when its
  authoritative path differs.

Theater must be watchable replay, not only ghost participant transforms.

---

## Milestone 3: sample graph-backed weapon

- **T-MODDING-008, missing:** Ship one small public-source base-gun variant,
  enabled by default, with a three-round burst or alternate projectile.
- **V-012, missing:** Prove discovery by permanent catalog ID, every supported
  custom weapon slot, setup save/reload, local gameplay, two-client
  listen-host parity, malformed-source rejection, and clean shutdown.

Needler is not the release sample. Every gameplay capture must still preserve
B-1054/B-1055/B-1056: no huge white first-person obstruction, correct held
placement/scale/material colour, and readable effect colour.

---

## Milestone 4: friend play and performance

D-003 and T-NETWORKING-004 are closed. The social/group topology remains
peer-to-peer, but each match has exactly one elected in-client listen authority.
Presence v5 carries a separately typed, signed, fresh match-server route. Every
non-authority performs one idempotent join. LAN/STUN/UPnP/ICE probes and relay
descriptors are auxiliary and must never reach `netStartClient` as routes.

Remaining 1.0 work:

- T-NETWORKING-001: signed peer ICE-candidate exchange and real-NAT proof.
- T-NETWORKING-002: distinct signed STUN reflexive candidate transport.
- T-NETWORKING-003: correct UPnP mapping for required probe transports.
- T-NETWORKING-005: configurable, authenticated public TURN fallback.
- T-NETWORKING-006: one tier/candidate owner after candidate semantics are
  production-correct, without reintroducing probe-to-client handoff.
- T-NETWORKING-007: co-op continuity, drop-out/reconnect, and Combat Simulator
  start/return behavior.
- P-001: explicit cold, warm, validation, rebuild, activation, creator, stage,
  and match timing budgets with raw artifacts.

Host authority migration after authority departure remains post-1.0 under
T-NETWORKING-008 unless Mike promotes it.

---

## Milestone 5: source-frozen release candidate

V-013 is the only closure gate. From one frozen revision it must prove:

- clean install, upgrade, failed-update rollback, and save migration;
- full automated suites, native-source guard, and archive conformance;
- complete campaign and representative Combat Simulator/base-game paths;
- real friend play across the required NAT matrix;
- Campaign and Combat Simulator Theater record/restart/load/playback;
- sample-weapon custom slots and local/network gameplay;
- clean disconnect and shutdown with no crash, fatal, or forbidden fallback;
- final visual captures retaining the B-1054/B-1055/B-1056 regression gate.

No receipt counts if relevant source changed during the run or an exclusive
resource receipt overlapped.

---

## Explicit post-1.0 work

- T-MODDING-005: Forge maps and map variants.
- T-MODDING-001, T-MODINFRASTRUCTURE-001/002, V-007: broad packer, Studio,
  creator, editor, and pipeline tooling.
- T-MODDING-003: Needler-specific feature polish.
- T-INPUT-002 and deferred D-001: Queue Match.
- T-BENCHMARKING-006: GPU swarm benchmark and scalability program.
- T-NETWORKING-008: authority migration and richer friend-play continuity.

Items marked `historical-cut` retain their permanent IDs and evidence but are
not live routing targets. They were consolidated into the milestone gates above
with Mike's authority on 2026-08-12.

---

## Verification discipline

- Use isolated queued builds and the required coordination FIFO for tests,
  builds, game runs, smokes, captures, and deployments.
- Record the relevant source fingerprint before a long receipt and compare it
  afterward. Reject drifted or overlapped evidence.
- Static tests prove contracts, not gameplay. Ordinary-client or real-peer
  evidence is required where the completion contract names runtime behavior.
- Update Workbench status/evidence and this concise task summary as truth changes.
- Update the relevant pillar, constraints, bugs/systemic-bugs, session log, and
  `UNRELEASED.md` in the same source-frozen unit when implementation changes.
