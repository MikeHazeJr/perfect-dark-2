# Bug Tracker

2026-09-23 T-MENUS-003 Forge Remove All previously called `botmgrRemoveAll`,
which clears bot roster pointers and count without retiring live character
props. Those characters continued in the world and occupied the character
roster even though the Bots tab reported zero. Forge now retires its tracked
characters through `chrRemove`/prop teardown, compacts both MP rosters, then
releases only its config slots; non-Forge roster entries remain. Add checks
actual roster publication and cleans a partial legacy allocation before it
reports success. Propagation check: stage reset still uses the old
`botmgrRemoveAll` contract, while both Forge exit-play and Bots-tab Remove All
use live retirement. Final isolated client/tests builds pass; ordinary play
validation remains pending.

The same Forge review found Spawn Near Me passed the high config-slot index
returned by `s_spawnBot` to `s_teleportBotNearPlayer`, which indexes the
sequential `g_MpBotChrPtrs` roster. A newly added bot could therefore miss the
teleport. `s_spawnBot` now returns the published roster index. Ordinary
placement and room-membership behavior still need a client walkthrough.

2026-09-23 T-MENUS-003 reachable Room Level Editor is a data-only shell. The
Room tab's "Launch Level Editor" makes `s_LEActive` true, "Spawn at Camera"
only appends to `s_LESpawned`, and the overlay shows a fixed stub camera with
free-fly "in development" text. `s_LESpawned` is referenced only inside the
same UI renderer, so no world object is created. This is an advertised dead
creator action in a reachable multiplayer room, not validated Forge behavior.
Mike chose to retire this data-only Room tab and keep Forge as the creator
route. The tab, launch action, overlay, and their private 518-line shell have
been removed; the three live Room tabs retain their order and Back behavior.
Propagation check: searched all `s_LESpawned` references and the separate Forge
entry; no production bridge was found. Fixed-source `menu0923` r7 client and
focused menu tests pass; Room walkthrough remains pending.

2026-09-23 T-INPUT-008 smoke-runner isolation: `run.ps1` globally reaped every
`PerfectDark.exe --smoke` process on entry and exit. The 14:32:29Z menu Agent
run exited -1 during extraction when a separately queued multiplayer runner
started at 14:32:32Z. Both runners used valid but different coordination
resources. Cleanup is now restricted to process IDs launched by this runner
and their fault children; a source regression guard rejects global smoke-path
matching. Propagation check: both single-client and multi-process cleanup
sites were inspected. Parser and mocked two-runner process isolation passed;
fixed-source r7 `[b927]` passed 21/21. Ordinary runner behavior remains
blocked at foreground ownership, independent of cleanup;
the failed receipt is `.claude/smoke-verify-runs/results-20260923T143229Z.json`.

2026-09-23 T-ASSETS-001/T-EXTRACTION-001: Isolated NTSC-final extract-only
boot produced 476/476 regional language sources and registered all 476, but
then stopped on 221 catalog registration failures. All 84 `.pdhead`, 68
`.pdbody`, and 63 `.pdcharacter` archives failed admission. One traced example,
`base:head_carrington`, contains `mesh.pdmesh` with an older public export
than `meshes/base_model_cheadcarrington.pdmesh`; both claim
`base:model_cheadcarrington`. Head/body/character fast caches use only their
own stamp/entry presence, so a changed top-level dependency does not
invalidate its nested copy. The catalog correctly rejects divergent public
sources with the same ID. Repair the extraction dependency lifecycle without
overwriting user edits, then prove clean and upgraded installs both boot with
zero catalog failures. Propagation check: head, body, and character extractors
all embed dependent typed archives and share this stale-cache pattern.
Initial failure receipt: `.claude/session-builds/asset0923lang/logs/game client/pd-client.3.log`.
Upgrade retry refreshed all 84/68/63 and cleared those 215 failures. The
remaining six were obsolete renderer-effect placeholder archives emitted by an
older extractor but absent from a clean current install; their old shader IDs
have no visual consumer. A strict provenance-and-public-digest migration now
retains only those exact untouched archives under effects/_retired and refuses
edited/unknown source. The migration client build passes; upgraded boot and
edited-source negative receipts now pass: final client fresh and upgraded
installs each register 5,562/5,562 active archives with zero failures, while
the edited legacy archive retains its exact SHA-256 and is rejected loudly.
The initial 221-failure boot is superseded for this migration scope. Full
fresh conformance covers 9,018 root / 10,019 recursive archives in 27
families. Propagation audit also found
embedded typed sources in weapon, arena, and metadata extractors; their
dependency invalidation policy still needs a separate source-consumer audit.
Follow-up 2026-09-23: weapon mesh/animation/audio and arena/mission scenario
parents now check current public dependencies before warm reuse. Four edited
top-level sources refreshed 11 weapons, one arena and one mission; 5/5 sampled
nested copies match and full catalog registration remained 5,562/5,562.
Three directly edited parents failed loudly and retained exact SHA-256. This
closes the identified warm-cache dependency class for these five parent
families, while the broader emitter inventory and gameplay source use remain
open. Receipt: `.claude/session-builds/asset0923deps/dependency-batch-receipt.json`.

2026-09-23 T-MENUS-003 social Player Profile displayed a 3D head/body preview
placeholder that could never resolve: `share_profile_t` carries only statistics,
with no appearance/catalog IDs. The preview promise has been removed from the
reachable modal; actual received stats and public-mod actions remain. Propagation
check found no other `charpreview` use in the social UI. Client build and
ordinary social-device walkthrough is still pending for this edit. The isolated
client build and broad input/menu/settings gate pass on this source.

2026-09-23 T-MENUS-003 Forge Bots tab had stale status text: `forgeRuntimeTick`
already consumes Add/Remove requests, but the UI called the botmgr wire a
future pass. The core incremented desired active/frozen counts on request and
runtime incremented them again after successful spawn. Source repair removes
runtime double increments, rolls back failed requests, and lets Remove All
cancel pending Adds; the UI now labels requested counts and pending commands.
The follow-up now maps Smart Aggression to native bot difficulty, applies the
selected default difficulty for Any/Near-Me spawns, freezes added Frozen bots
by slot, and frees Forge config slots on Remove All so later Adds can use them.
The Bots tab uses a validated difficulty combo and disables Add at capacity.
This follow-up compiles in fixed-source `menu0923` r7 client and remains
unexercised in ordinary play because foreground ownership is unavailable.

2026-09-23 T-MENUS-004: The generic ImGui typed-dialog renderer treated every
SELECTABLE `handler` as a callable function. For `MENUITEMFLAG_SELECTABLE_OPENSDIALOG`
the game ABI stores a dialog-definition pointer there, and for
`MENUITEMFLAG_SELECTABLE_CLOSESDIALOG` the handler can be null. Thus reachable
fallback buttons could call data as code or leave Done inert. The renderer now
defers the selected action until after `ImGui::End`, applies close/open flags
through menu-graph push/replace/pop edges, and gives Cancel precedence for a
simultaneous press. Propagation check: inspected the purpose-built ImGui dialog
registrations and the legacy `menuitemSelectableTick` contract; remaining
generic selectable handlers require current ordinary-client coverage. Isolated
client and tests build, focused `[menu_graph]` PASS846/846 and broad
`[input],[menu],[settings]` PASS9732/9732; controller/MKB runtime remains open.

2026-09-22 19:18 ET: T-NETWORKING-011 ordinary listen-host multiplayer start
can reject a valid Random weapon selection. On the short-path real-peer
distribution fixture, `matchConfigSelectWeaponSet(6)` resolved to Random set 13;
the lobby writer transmitted the menu index and exact slots, but the server
reader tested index 6 against the resolved Random constants, re-rolled slots,
and rejected `weapon set metadata and exact slots disagree` four times before
ready/transfer. Two-path `netmsg.c`/lifecycle guard correction applied, not yet
built or runtime-verified. Propagation: surveyed all `mpPrepareWeaponSet` callers;
writer and match setup already distinguish requested and resolved domains.
Retain RED35/67 receipt `results-20260922T231327Z.json`; no F12 runtime verdict.

2026-09-22 18:59 ET: T-NETWORKING-011/F12 transfer BEGIN previously allowed
512 MiB while the sender capped expanded archives at 50 MiB and END rejected
above 256 MiB. Its 200 MiB session check combined received compressed bytes
with incoming expanded bytes, without reserving pending transfers. The lobby
progress bar divided compressed bytes by expanded bytes. Source6 now has one
expanded-size budget and bounded compressed receive geometry; native budget4,
client/tests builds and source guard pass on full3623 SHA4463E90D. Propagation
check covered sender/BEGIN/CHUNK/END/progress paths. Consent and pacing remain
separate open defects. First two-process smoke stopped before any transfer on
an `ASSET.SOURCE_ONLY` fatal: the long per-test host install path exceeded all
private animation cache path budgets. This is a source-observed path limitation
in `modasset_compiler.c:4533`, not proof that F12 distribution failed or passed
at runtime. Retain RED20/44; the shorter install path cleared this first
blocker and exposed the separate lobby-start defect above.

2026-09-22 18:19 ET: T-NETWORKING-011 synthetic FT socket peer initially
treated a preceding presence ping as an FT frame and exited before acknowledging
the game's valid INIT. First ordinary run stopped red at 4/6; the game sender
then abandoned after no INIT ACK. Root cause was the fixture peer assuming all
datagrams on the shared presence socket were FT. The peer now filters non-PDFTX
frames and expects the production `file` kind / `files` folder. Propagation
check: all peer receive paths use the common filtered `received()` function.
Corrected r2 on the unchanged client passes signed bidirectional UDP, saved
file/sidecar and ACK replay (6/6, one publication). Retain r1 red and r2 green
receipts under `.claude/mp0908-repairs/followups/ft-storage/`.

2026-09-22 17:52 ET: T-NETWORKING-011/F04/F12 receiver completion repair is
component-verified on frozen full2149 SHA8337DB9E: isolated client/tests builds
0, native storage5 exact5/0 failures, native asset source guard0. The earlier
menu client build stopped first red because the removed filename sanitizer still
served the local music-to-mod converter. Propagation search found one remaining
caller; source6 r2 restores that local helper while remote inbox naming uses the
new safe peer/digest helper. Current combined client build passes. Real signed
socket delivery, sidecar JSON parse, receiver restart and UI/WAN remain open.
Receipts: .claude/mp0908-repairs/followups/ft-storage/{build-r2-client-exit,
build-r2-tests-exit,storage5-exit,storage5-name-audit,native-guard-r2-exit}.json.

2026-09-08 23:06 batch22 connected texture/source pipeline ACCEPTED within scope (source35).
Full2145 24440303AB0D8D1EB47802A8BA52AA3D71F7D10FCFB818CFDCDB1135EE097BD2;
changed488 BAF7FC71; client6D4FE54E/tests233ECA5C unchanged through final audits.
Client/tests build0, native26 PASS (unchanged helper inputs), affected source-contract1
PASS, source guard0, warm installed24 PASS35/35 in39.1s using completed fresh
extraction20260909T025106Z. Actual texLoad pixels, descriptor edit, stage modes,
GBI tile fields, rejection/reset, sparse reverse index, custom menu texture and
suppressed/enabled base-overlay image/material selection all pass. Audit PASS all
3503 original texture material records,169 stage assignments,87 standalone+45 nested
scenario graphs,132 unchanged nonmaterial graph comparisons and coherent hashes.
All110 public command IDs/593 commands exact, no source/fixup failures.
Additive atomic upgrades preserve edited descriptors/images/graph topology and
versioned omissions; incomplete archive views reject rewrite. All12 occupied-count
index-bound mistakes fixed or removed across8 extraction/loading paths. Suppressed
base overlays now select public base sources through shared image/material policy.
Earlier compile/link reds, initial legacy-DMA crash, safe rejections and120s extraction
timeout remain in batch22 receipts; timeout allowance is now bounded240s. This is
native/source acceptance, not rendered appearance, full graph gameplay or all-family
parity. Full equipped model-generation ownership/graph publication, palette/mipmap
parity, larger textures and requested pack remain open. No staging/commit/push.
Evidence: .claude/asset0905-continuation/batch22-r9-{client-build,tests-build,
affected1,native-guard,texture-pipeline-installed24}-exit.json; batch22-combined26
JUnit/name audit; batch22-r9-{texture-material-audit,base-command-coverage,
post-runtime-freeze}.json; results-20260909T030500Z.json. Root queues finished;
source holds released. MP storage remains private/paused; menu native work idle.


2026-09-08 22:59 source33 settled r8: all12 occupied-count/index-bound mistakes repaired or removed across8 loading/extraction paths. Shared ordinary texture selection now chooses public base sources when overlays are suppressed; image/material policies match, cache tracks suppression mode. Installed24 fixture checks sparse reverse index before texLoad and exercises custom/menu source plus enabled/suppressed base overlay pixels/properties. Client/guard/warm24 next; native26 inputs unchanged. No full equipped-graph or rendered acceptance.


2026-09-08 22:57 batch22 r7 installed warm24 TERMINAL RED32/35: custom slot now safely rejected, not legacy DMA crash. Actual sparse catalog root cause:12 index loops used occupied count instead of pool high-water index; valid later rows skipped after removal. Corrected full class across8 sources (root additionally owns base_extended/langmanifest/romextract_pdmeta), each already checks occupied rows. Fixture enables its texture and next asserts actual reverse-index binding before texLoad. Native26 unchanged; source33 pending combined client/guard/installed24. Prior r3/r4/r5/r6/r7 reds preserved.


2026-09-08 22:45 batch22 r3 client/tests build0, combined native26 PASS, source guard0. Installed24 TERMINAL RED32/35 (98.9s): new ordinary custom texture case reached legacy DMA after g_NotLoadMod suppressed its public source. Exact addr2line traces texLoad:2361 from harnessTextureRuntime:839. Corrected custom-versus-base-overlay suppression and added a custom-slot boundary before any legacy directory lookup. Source30 now settled for r4 client/guard/installed24; native26 inputs unaffected, no unchanged rerun. Original red receipts retained; full texture/material acceptance pending.


2026-09-08 22:30 source-confirmed: archive readers intentionally filter unsupported/encrypted/unsafe entries. Rewriting a filtered view silently loses members. modArchiveCanRewriteSources now records complete original entry names/count/comment and blocks partial-view rewrites in generic replacement and texture/stage migrations. Source28 applied; native rejection/preservation cases and combined validation pending.

2026-09-08 21:58 texture generation/source-properties component ACCEPTED (batch21).
Source20 r2 83677B33; full2136 CC3BAE06/changed4731F8F2D7F/clientE23B7EEF/
tests5D62ADA3 unchanged. Client/tests build0, native texture7 PASS, source guard0,
fresh installed23 PASS33/33 in75.3s with DummyAudio. Actual catalog acquisitions
retain image+descriptor hashes, pixels/native identities and tile/surface data;
image-only and property-only edits coexist; retirement/reset/empty pool preserve
old users; actual model G_SETTIMG/G_SETTILE use selected retained generations.
Malformed image, bad/duplicate/wrong-ID properties and incomplete bundled legacy
metadata reject; final release permits reuse. Base110 command sources/593 commands
exact, no missing/source/fixup failures. Receipts batch21-* in asset0905-continuation;
install 20260909T015641Z-weapon_mesh_ingress_smoke, results-20260909T015757Z.json.
All root queues finished and source holds released. MP storage6 stays private,
paused and excluded. Base texture metadata migration, ordinary/stage override
source authority, palette/mipmap parity, larger images, complete equipped graph
and requested pack remain open. This proves native consumers, not rendered output.

Earlier findings and preparation history follow; current status above wins.

## 2026-09-08 File-transfer completion precedes durable save and loses retries

T-NETWORKING-011/F04/F12, source-confirmed after wire7. handleChunk ACKs the final
chunk before completeReceiver checks SHA and writes the inbox file. Sender frees
on that ACK; receiver frees after completion, so a lost final ACK cannot be
answered on retransmission. Data publication uses fopen wb and deletes the
destination on a short write, risking a previous file. buildInboxPath inserts
the remote Agent name directly into a directory; sidecar JSON interpolates names
without escaping. Next repair must use bounded safe peer paths, verified atomic
publication, final saved/rejected receipt and bounded completed-receipt replay.
An in-memory completed cache alone does not prevent receiver-restart replay.
The Sep22 source6 r2 component gate above verifies receiver commit behavior;
actual socket proof remains open. Native wire7 is separate.

## 2026-09-08 Signed file-transfer frame and chunk bounds are inconsistent

T-NETWORKING-011/F04/F12, source-confirmed21:01. FT_FRAME_LEN is1280 while
FT_SIG_OFFSET1256 + FT_SIG_LEN64 requires1320 bytes. Sender stack packets allocate
1280 and signFrame writes64 signature bytes at1256: a40-byte overrun, followed by
a truncated signature on the wire. Receiver storage is larger but accepts only
1280 bytes and verifies64 signature bytes, including40 bytes not received.
This prevents a trustworthy signed-file-transfer path; current activation probes
exercise the installed-package queue, not this socket/frame path.

handleInit also allocates chunk_present from unchecked advertised chunk_total,
without requiring ceil(file_size/1024). handleChunk computes the final copy size
from file_size minus offset without capping it to the received1024-byte payload.
A too-small chunk count can therefore make the copy read beyond the packet.
ACK/REJECT lookup is transfer-ID-only and does not bind the signing friend to
the transfer; chunk handlers ignore recipient/echoed count. Repair wire sizes,
geometry and peer/recipient admission with real signature/packet-boundary tests
before claiming received-content transport acceptance. At21:19 wire8 applied
under a fresh source grant (manifest72C4260E), validation pending. A shared
capacity-checked v2 wire helper and presence dispatch now own these boundaries;
native7 passed2898 assertions at21:24, client/tests/guard0 with no drift on full
438CF4A4/client4DDA994D/tests6A2856DE. Actual socket/receiver commit acceptance
remains open. The legacy sender's250ms delay after each ACK is also removed; only retries
wait. Distinguish all-chunks-acknowledged from verified/saved/installed receipt.

## 2026-09-08 Model source metadata and retained texture dependencies

T-MODDING-002. Model skeleton read private `_meta/manifest.json` before public
mesh.ini, while loose OBJ/glTF sidecars were ignored because metadata paths
required an archive separator. Correction applied with explicit compiler input
callbacks; verified in model-inputs installed22 (22 cases, 31 assertions). Geometry, public
metadata, external buffers and image bytes can now be supplied as one captured
read set. No immutable equipment-model acceptance yet.

The audit also found texture tile offsets/mask reductions still read from
`g_Textures` in texWriteTileFromDefinition, and tile/sound/surface attributes are
not emitted by romextract_pdtexture.c into texture.ini. Collision/sound callers
also consume texture numeric identities. A private model texture pool alone
cannot preserve those semantics if it reuses local IDs. Next owned texture/model
work must expose the missing public metadata, retain native texture identity and
pixel lifetime, and cover renderer/collision consumers. Do not bypass this with
zero defaults for extracted textures or claim geometry-only ownership as complete.

Propagation audit 21:03: mpStartMatch also hardcodes 169 surface/sound assignments
across 90 native texture identities according to stage ID. These stage semantics
must become public stage-owned overrides keyed by exact texture catalog ID, with
defined precedence and transition reset; copying the ROM texture table alone is
insufficient. Existing extractor cache-kind mismatch rewrites entire archives;
the metadata migration must preserve edited images, descriptors and extra members.
Inventory: .claude/asset0908-drafts/texture-generation/stage-properties-audit.json
and migration-review.md. No extraction/runtime fix for these findings applied yet.

## 2026-09-08 Digital action holds lack per-source ownership

T-INPUT-008; source-confirmed20:40, no runtime reproduction yet. fireVk resolves
each press/release independently and toggles one ActionState.held. Two distinct
bindings to one action can release each other. The per-frame wheel cleanup also
clears every held action that has a wheel binding, regardless of which physical
source holds it. This extends the raw Btn23/axis alias release problem to ordinary
multi-binding. The future coherent VK/dispatch repair needs per-source retained
ownership, source-specific wheel pulse retirement, and flush/smoke ownership
integration. Merely widening VKs cannot fix this class. Sep22 source candidate
now retains the physical source's original action winner and aggregates owners;
wheel cleanup retires only actual wheel pulses. Focus, device, context, and
binding boundaries retire matching owners. Sep22 isolated client/updater/tests
builds, direct owner3/26, smoke-owned source1/53 and actionmap regression15/481
pass. First peer FT compile red and two stale source-contract reds are retained.
Ordinary menu/runtime and hardware proof remain open; high-button/axis VK
separation and glyph truth remain open.

## 2026-09-08 Archive rescan resets loaded baseline before activation preparation

T-MENUS-006; source-confirmed20:36, runtime reproduction pending. Installation
calls modmgrRescanDirectory before activation preparation. The rescan restores
all archive rows with loaded=0 after closing handles, even if previously loaded.
A subsequent requires-restart plan therefore cannot recover that earlier loaded
baseline. The retained-plan repair preserves its own preparation baseline across
retries; it does not yet preserve pre-install archive generations/state. Needs
coordinated catalog/activation lifecycle work and an existing-archive regression.
The current discovered-folder smoke must not be described as this proof.

## 2026-09-08 Installed activation retry can skip a failed persistence step

T-MENUS-006; MP read-only propagation finding, menu owns correction. In
modmgrEnableInstalledChecked, disabled activation publishes enabled1 before
persistent Apply. Failed component/config save leaves enabled1/dirty1; retry
then takes the already-enabled sync-only branch and can report success without
retrying persistence. A fresh Apply plan after late failure can also lose the
original requires-restart loaded baseline. Do not wire FT retries to the current
helper. Repair needs retained operation intent/baseline plus explicit completion
or discard semantics. Sep8 20:31 correction applied: caller-owned persistent
activation plan, install-only API, importer Retry/Discard and departure guard.
MP owns FT migration. Actual changed-selection failure/retry validation pending.
Core Apply2 proves SAME owned-plan retry only and does not cover this helper.

Sep8 21:15 updated: activation core2 PASS35/35 in79.5s now verifies the replacement
helper's new/already-enabled persistence retry and real late-load retry preserving
original loaded1 restart baseline. FT actual retained queue/retry/explicit discard
also passed. Receipt menu0908/activation-core2-exit.json, client811BE779. The prior
helper retry defect is corrected; rendered UI/Agent-switch and the separate
pre-install archive-rescan baseline gap remain open.


## 2026-09-08 Received-mod activation retry can skip failed persistence

T-NETWORKING-011/F04/F15 propagation finding, source-confirmed20:11; actual
failure/retry reproduction pending. modmgrEnableInstalledChecked first publishes
enabled1, then its persistent prepared Apply may fail component/config saving.
It frees that plan while leaving the in-memory enabled flag. The next call takes
the already-enabled branch into modmgrSyncCatalogToRegistryChecked, which rebuilds
without saving, and may report success with unsaved selection. Menu owns helper
repair; N-0168 records exact paths/lines and direct handoff. MP file-transfer caller
also ignores Apply results and clears its prompt on failure; its UI void-casts
acceptance and dismisses. Repair both checked retry semantics and caller/UI state,
then prove saved selection and actual activation under injected failure/retry.
21:16 update: menu retained-plan API and MP FT caller/UI repair are applied.
Public Mods7 source-contract cases and actual core FT save-failure/retry/discard
passed; ft-activation-runtime-review.json checks exact events/current FT4 against
Core2 full049E558F/client811BE779. Retry saved selection and loaded the package;
discard cleared the queue while preserving enabled1/dirty1. This is installed
queue/core proof, not actual download, ordinary UI or Agent-switch acceptance.

Related source ownership gap: file_transfer.c pending activation rows contain
sender/mod identity but no local Agent owner; fileTransferInit/Shutdown do not
clear that queue. Agent rebind can therefore retain another Agent's prompt and
transfer state. MP follow-up must bind pending plans/prompts to local Agent and
stable mod ID, cancel owned in-flight state on rebind, retain failures for retry,
and avoid claiming a partially enabled package was kept disabled on dismissal.
20:52 update: pending rows now own local Agent + stable mod ID and a persistent
plan. Shutdown/rebind free plans and old transfer buffers; discard reports no
rollback. An actual Agent-switch reproduction remains pending.

## 2026-09-08 Voice burst negotiation can discard speech onset

T-NETWORKING-011/F09/F10. Source review: a new PTT/VAD burst sends HELLO, and
broadcastVoiceFrame drops encoded media until the peer returns its challenge.
Fresh challenge admission prevents replay, but the initial speech can be lost
over the handshake round trip. Preserve fresh-session security while evaluating
bounded outgoing pre-roll or consent-scoped pre-negotiation. The accepted native
transport tests answer HELLO before media and do not prove onset quality. VAD is
now source-applied and native17/component-verified on frozen full2156 59011833,
but actual sockets, audible onset and physical-device acceptance remain open.
The VAD threshold does not itself solve negotiation pre-roll.

## 2026-09-08 Audio generation and effective pitch ownership

T-MODDING-002: sound leaf slots were reused on catalog reset while native command
owners could still retain their IDs. Per-play source reopening also selected
new bytes after an edit. Applied generation adapter owns decoded source and
slot; each playing voice receives independent PCM. Shared file/PCM voice start
also rejects finite pitch factors whose product is nonfinite, which could
otherwise leave an infinite cursor in the loop mixer. Batch18 audio36 and installed20 dummy-mixer checks passed: retained PCM/slots,
playing voice after release, loop timing and overflow-allocation rejection.
Client isfinite and menu linkage build failures remain separately recorded;
r3 clientCF294A6D/full80E30B41 is accepted. Full command/graph integration is open.

## 2026-09-08 Interface Delete leaves stale catalog after partial file removal

T-MENUS-006. interfaceDeleteModDir removes known manifests before attempting
rmdir; remaining files can make rmdir fail after mod.json was already removed.
The UI previously skipped catalog refresh in this case and reported success
after unchecked refresh on complete deletion. Sep8 19:37 one-file source repair
reconciles actual files after either outcome (including theme directories),
consumes checked sync and offers Retry refresh without repeating deletion.
Status preserves partial-file truth, wraps long errors, and gives Back priority
over retry. Batch18 r2 exposed local extern declarations acquiring C++ linkage
in the extracted helper; moved both to the global extern C block under root's
sole-source correction grant. Settled manifest interface-delete-linkage-applied.json.
Batch18 r3 clientCF294A6D/full80E30B41 passed with no drift (menu receipt
interface-delete-client-postcheck.json). Actual UI/input verification pending.
Filesystem deletion scope is unchanged; both earlier client reds remain retained.


## 2026-09-08 Alternate Mod Manager callers discard activation failures

T-MENUS-006; source-confirmed propagation of main Apply failure loss. Reload
cleared dirty and transitioned stage after failed rebuild; archive enable returned
an applied-success index after void Apply/sync; Weapon Save completed its modal
after void Apply. Sep8 19:27 caller4 source repair adds checked reload/sync and
installed activation, verifies actual enabled publication and carries failures
to archive out_error and Weapon Save status. Restart deferrals get truthful
wording. Manifest .claude/menu0908/apply-complete-review/callers/applied-four.json.
Client/tests build0 and affected Weapon source contract1/100 assertions plus
guard PASS on fullFBCC1DBA, no drift. Actual core/UI runtime remains pending.
Interface Delete, file transfer, Agent transient
Apply and session retirement are still explicit propagation follow-ups.


## 2026-09-08 Main Apply loses failure and cannot retry its own publication

T-MENUS-006; source-confirmed. Main UI mutated component runtime before saving,
called void Apply, refreshed away desired choices and displayed success after
ignored save/rebuild failure. Its old baseline also rejected retry after its
own successful registry publication. Sep8 18:56 source repair adds prepared
intent, checked Apply/rebuild, actual publication verification, retained Agent/
restart state and post-attempt baseline. Main UI keeps failure/choices and guards
departure until checked success or explicit discard. Applied-ten.json in
.claude/menu0908/apply-complete-review. Catalog-plan4/51 and settled apply-r3
main5/139 assertions, client/tests builds and guard PASS with no source drift.
Actual core/UI failure journeys remain pending. Other void Apply/reload/sync/install callers remain
the propagation follow-up; no full transaction rollback is claimed.


## 2026-09-08 Metadata-only import rollback changes lifecycle state

T-MODDING-002 installed batch15 failed all three new folder/loose/typed late
replacement cases despite valid dependency admission and logged rollback.
`catalogActivationLedgerRestoreRetiredSnapshot` unconditionally promoted every
enabled REGISTERED row to ENABLED, even when it never owned a payload. Exact
row restoration was therefore impossible for inert admitted command sources.
Fix preserves prior REGISTERED/ENABLED state for unowned metadata, while still
clearing retired loaded/active pointers, reference counts and runtime state.
Actual-ledger regression and retired-ownership regression passed2. Corrected
installed batch16 passed18/18 inner23/23 outer72.3s; folder/loose/typed late and
cyclic replacements restore exact rows, native pointers and provider counts. The shared helper
covers all import rollback callers; do not normalize away this fixture failure.
Batch15 receipts remain failed15/18 inner,15/23 outer98.599s, source unchanged.

## 2026-09-08 Social group and voice state lack active-Agent ownership

T-NETWORKING-011/F07/F15, source-confirmed lifecycle gap; live reproduction
pending. agentSessionActivate -> prefsAgentPublishActive rebinds the social handle,
but socialHubBringOnline returns immediately once online. socialRebindToActiveAgent
only changes name/handle/code; no group/voice reset. The only source caller of
groupSessionShutdown is socialHubGoOffline, which has no production call sites.
Transport disconnect preserves peers while suppressing automatic rejoin. The group
snapshot has no local Agent owner, and voice signs using the current handle while
retaining prior PTT/peer state. Accepted audience admission must bind the session
to its Agent and revoke queued audio/routes on identity change; filtering only by
current group peers is insufficient. Sep8 19:11 source repair binds group state to
the local Agent, prevents an identity switch from adopting a live ENet session,
and retires old peer probes/routes after transport teardown. Voice now gates send
and receive by accepted current-owner membership, resets on Agent changes, and
clears revoked playback. Seven-path manifest source-owner7-settled.json SHA774288F4
was frozen for the coordinated client/native6 checkpoint. Native6 passed40
assertions on tests09DD35F0/fullD9048BFF with no drift, and clientE69F3AF4 built
with Opus. Actual Agent/voice runtime verdicts remain pending;
transport/replay/mixing/VAD remain open.

## 2026-09-08 Bot-variant scanning hides admission failures

T-MENUS-006; source-confirmed. ScanBotVariants returned0 for overlong paths,
unreadable/non-directory roots and silently skipped parse/registration errors;
aggregate ScanComponents skipped overlong children. Sep8 18:29 exact-owner
repair returns negative failure while retaining admitted counts; only optional
directory absence is zero. No atomic rollback is claimed. Source manifest
.claude/menu0908/scanner-status-review/applied-two.json; build and actual
filesystem/client validation pending. Full Apply must consume these statuses.

## 2026-09-08 Movement decoder accepts nonfinite fields and unwired stack data

T-NETWORKING-011/F05, source-confirmed. Eleven ordinary floats and conditional
zoom reached movement state without finite validation; weapon_id was never
initialized by the legacy wire decoder. Sep8 18:25 numeric7 extracts the actual
codec, zero-initializes a local candidate, rejects NaN/Inf transactionally and
publishes only a complete record. Both ingress scratch records initialize before
optional-force-field inspection. Native4 plus affected inventory contract passed5/362 at18:36 on A832459C/
fullD0FE92DF; matching client compiled, movement runtime remains pending. Wire layout and weapon clamp unchanged. Both receive
paths share the correction; acknowledgement/force policy and actual movement
simulation remain open, so this is not server-authoritative movement acceptance.

## 2026-09-08 Save-only authoring reports persistence success after failure

18:24: native six production-executor cases/79 assertions PASS, tests build0
and source guard0/no drift (save-status6-exit.json, save-status-native-guard-exit.json).
Tests use controlled destination callbacks; actual adapter/UI/client proof
remains pending. Silent enable refusal is checked in all three save-only paths.

T-MENUS-006; source-confirmed. Audio import and chrome authoring used the void
modmgrSaveConfig path, which ignored configSave failure and merely logged Agent
failure. Audio success sound preceded saving; missing registry matches also
fell through to success. Sep8 18:16 source repair adds checked destination and
Agent-identity results, connects both audio paths and chrome, and moves audio
success after persistence. Source manifest .claude/menu0908/save-status-review/
applied-seven.json. Six orchestration cases and matching client/UI verification
are pending. Propagation check found full Apply still needs end-to-end status;
this repair does not claim complete Apply or cross-file rollback.

## 2026-09-08 Animation command namespaces and failed admission (T-MODDING-002)

17:54 bounded FIX VERIFIED: exact native command namespaces and category/order
repairs pass installed15/18 outer58.904s. Public-source coverage independently
matches110 unique base command sources to593 native commands, zero missing IDs
or fixup failures. Batch14 receipts preserve source408ABDB/full20965BC6/clientD988.
Original failed runs below remain evidence. Full mod-import transactional order,
immutable slot retirement and graph gameplay still require implementation.


17:45 installed fixture15/17 outer passes, but base-loading acceptance is RED:
loaderWalkerMarkBaseArchiveEntry overwrites semantic category with base. The new
compilation pass consequently skipped all110 base command sources; native pool
logged animations=0/guncmds=0. Restore the animation category after provenance
marking and reject category disagreement, plus require110 native base commands
in the installed fixture. Original batch13 pass is retained as fixture evidence
only; supplemental review records the failed base-content coverage.

17:24 installed RED: the first strict-reader client stops boot with61 failed
command admissions. The emitter writes trigger=z with an optional zero slot;
the reader omitted that supported shape. The walker also compiles commands
before all animation rows and audio FileProviders have been registered.
Retained batch12-command-installed15-exit/result (7/17 outer,97s), unchanged
source4036A3E/full2093EA1D/clientB56D. Repair registration order and source parity;
do not weaken dependency validation or claim this installed run passed.

Public command references such as mod_a:reload and mod_b:reload were stripped to
reload by lookup and deferred fixups. The bare-name registry could bind a different
mod's animation. The base walker additionally ignored source parser failure and
logged success; missing character-clip references did not set its rollback flag.
New strict public reader and catalog-ID loader entry point are integrated in the
walker, local scanner, and distribution caller. Complete typed scalar/reference
preflight precedes pool writes; exact qualified IDs survive lookup/fixup/replacement.
Six reader cases passed; the combined8 gate stopped on an unrelated obsolete
glTF-buffer prohibition assertion (retained JUnit, no source drift). That contract
now follows the actual shared buffer loader. Indirect/self command include cycles
are rejected before publication, with installed rollback proof staged. Revised
affected contracts and installed proof are pending. Immutable dependency leases and removal
of the remaining fixed command backing pool are still separate required work.

## 2026-09-08 remote lobby roster omits the connected listen host (T-NETWORKING-011)

Open, verified16:49 in the installed two-client pair-r2 capture. The observer
authenticates as client1 and sees host client0's Crispy Monkey room with1/4
capacity, but Connected Players shows only its own Agent and Players1/32.
The server-side listen-host row fix is insufficient for remote roster views.
Trace complete authoritative player identity/state replication and the separate
selected-room membership projection before claiming lobby acceptance. Evidence:
`.claude/mp0908-repairs/runtime/room-list-pair-r2-visual-review.json`.

17:19 private roster18 draft addresses this through explicit presentation
snapshots and room-filtered shared views. It is uncompiled/unapplied; seven native
cases and the SDL Join pair are unrun. Review also found the Counter-Op preview
holding a pointer to a block-local player view after its lifetime, and selection
following a compacted row instead of client identity. Both are corrected only in
the reviewed roster18 unit, integrated at17:27 with all settled hashes matching.
Provisional reconnect publication is explicitly excluded to preserve the existing
restoration transaction. At17:42 native7 and21 affected cases are accepted;
client and SDL Join verification were pending. At17:59 Join-r2 shows correct
global/room rosters but remains FAILED35/37 due one initial malformed0x7b warning.
Empty body/head IDs were incorrectly sized2 vs writer3; corrected canonical
lobby_roster_wire.c and two focused regressions passed the affected native9 at
18:12 (331490B9/fullC9A6A2C9). Alternate zero-length empty decoding remains
compatible; undersized output is atomic. Propagation found existing room-wire
already handles both encodings. Matching-client Join-r3 passed37/37 at18:50 on6C12FA9B/full639A68A8,
with both initial packets accepted and global2/joined-room2 visible. Initial
roster omission/empty-ID rejection now has native and installed proof; distinct
profile mapping, third-peer isolation and full multiplayer acceptance remain open. See repair audit and roster9-empty-r3-exit.json.

## 2026-09-08 Empty weapon visibility list executes terminator (T-MODDING-002)

Severity: invalid memory read / unintended model visibility mutation. Found while
preparing owned graph equipment. `bgunExecuteGunVisCommands` tests END only after
executing the current row and advancing. `loader_pool.c:parseGunvisArray` can
legitimately materialize a terminator-only list, which the consumer executes and
then reads past. The new equipped builder returns NULL for an empty list, but the
consumer must also honor END before dispatch. Propagation search found this one
consumer; menu part visibility already checks its sentinel before execution.
Consumer now checks END before dispatch. Installed harness calls null and a single
END row with null hand/model storage; the explicit required log line is present.
Client build and native-source guard pass. Installed weapon_mesh_ingress_smoke
passes14 cases/16 outer assertions in54.0s on client980529BE/source399078794E7
(full native20908B2CFF2A), including required EMPTY_VISIBILITY proof. Receipt:
.claude/asset0905-continuation/batch11-visibility-installed14-result.json.

## 2026-09-08 primary Lounge invalidates authoritative room lists (T-NETWORKING-011)

Source/native corrected16:28; remote room-list reception verified16:49 through
pair-r2's31 assertions and both observer images. Full player-roster acceptance
remains separate and open above.
`roomsInit` previously opened permanent room0
with zero capacity and a zero-initialized creator. `netmsgSvcRoomListRead`
correctly rejects zero capacity transactionally, so that one entry prevents
every room in the list from being published. `hubTick` updates only state.
Initialize capacity to HUB_MAX_CLIENTS and creator to the unowned sentinel;
preserve valid capacity through permanent-room reset. Add a direct fresh
initialization/reset case, then verify room-list reception in the next client.
The earlier host Create18 receipt proves assignment, not list reception.

## 2026-09-08 catalog toggle uses enabled-only identity lookup (T-MENUS-006)

16:30: actual catalog10, affected lifecycle1 and native guard PASS with no
drift on source2087E5AC; test build green. Direct tests prove re-enable and
rejected-teardown restoration. Matching client build remains pending; full
typed runtime and Apply UI acceptance are separate and still open.

16:23 update (N-0130): seven paths applied/frozen, receipt
`.claude/menu0908/modmgr-apply-review/catalog-applied-seven.json`. Both resolver
sites repaired; checked mutation outcomes and ten actual-catalog tests added.
Compilation and execution remain pending the MP-owned joint build.

Open, high impact. At 16:19 root traced both publication and rollback in
`assetCatalogSetEnabled` to `s_resolveLocked`, which returns only enabled rows.
Re-enabling a disabled row therefore aborts at its transaction check, and a
failed disable cannot restore its enabled bit after it was set to zero.
Use the existing `s_resolveAnyLocked` at both mutation sites while keeping
ordinary gameplay resolution enabled-only. Both repairs are prepared in
`.claude/menu0908/modmgr-apply-review/catalog-fragments.json`, not applied or
tested yet; ten actual-catalog tests include re-enable and rejected teardown.

> Live one-off defects and release regression gates only. Recurring architectural
> classes belong in [systemic-bugs.md](systemic-bugs.md). Release priority and
> ownership come from the canonical Workbench, not from this file.
>
> The complete pre-consolidation ledger is preserved verbatim at
> [_old/bugs/2026/bugs-through-2026-08-12-pre-v1-consolidation.md](_old/bugs/2026/bugs-through-2026-08-12-pre-v1-consolidation.md)
> with SHA-256
> `80A567D268C22901B6117AF310BE206336EC5CC9A9E61B221172F20296093D4E`.

Back to [index](README.md).

## 2026-09-08 Configured sound identities exceed reverse table (T-ASSETS-046)

Critical, corrected and installed-verified15:50. Public audio
installed6 stops0/6,3/14 outer on unchanged371/F0A104AC/client21C5C296. The
admitted base:sfx_unlabeled_avrm source has correct public controls but packed
sound identity32928 exceeds assetcatalog_load.c's4096-entry reverse table.
The table now covers the complete unsigned16-bit admission domain; installed
checks require the exact packed identity and reject negative/wider inputs.
Corrected installed6 passes14/14 outer on371/9D551466/client5A39CAD6 in70.1s;
guard and client build pass. Receipt batch8-packed-audio-installed6-exit/result.json.
Evidence: batch8-public-audio-installed6-exit/result.json. Propagation audit
also finds sndStart and Scenario audio helpers collapse configured references
to leaf IDs; preserving configured source identity through those callers is
separate open work, alongside PCM indexing/migration/chains. No playback parity
claim is made by extending catalog resolution alone.

## 2026-09-08 Base audio seed has no reverse identity (T-ASSETS-046)

Critical, fixed and installed-verified15:37. Base audio registration initialized ext.audio.sound_id and
runtime_index but leaves source_soundnum at -1. The source-authority replacement
transaction correctly preserves existing native identity, thereby preserving an
unset reverse route. Installed diagnostic192216Z confirms SFX candidates=0 while
voice-file and music targets are admitted; no audio identity cases can begin.
Configured aliases also omit their packed reference and MP3 file provenance.
The two-path correction initializes identity at base seeding, preserving
source replacement rules. Applied15:29; focused8, guard and installed8/15 outer
pass on unchanged371/F0A104AC/client21C5C296. Prepared patch in
`.claude/asset0908-drafts/audio-seed-identity/`. Native PCM extraction indexing is
a separate defect; this fix does not establish waveform parity or migration.
Evidence: batch8-audio-identity-preflight-exit/result.json, clientFA930431 and
unchanged360/7EB79621. Public-audio6 and weapon14 are unrun after this first-red.
Applied-source receipt: `.claude/asset0905-continuation/batch8-audio-seed-applied.json`.
Acceptance: batch8-audio-seed-installed8-exit/result.json and focused8 JUnit/name
audit. Actual source file handles, repeated replacements, custom slots and
folder/received late rejection rollback pass; no audible or native waveform
indexing parity is claimed by this identity repair.

## 2026-09-08 Agent Create control overflow / focus transition (T-MENUS-003, N-0126)

15:29 update: clientFA930431 builds with the layout and diagnostics. One
changed-source desktop run remained at AgentSelect because supported window
activation failed twice;9/22,0events/no captures, unchanged source7EB79621.
No layout/navigation/provenance pass. Startup retries stopped; see
`.claude/menu0908/agent-layout-provenance-exit.json` and the menu audit.

Layout repair applied, unvalidated. Desktop observation after real window
activation showed long body/head names and reset text extending into the black
preview. Source manually drew names outside a fixed reserved width and left
the controls group unconstrained. AgentCreate23AF9A96 contains controls in a
scrollable NavFlattened child and wraps labels with separate count/reset rows.
Matching client/visual/navigation gates remain pending. Preview request was
body `base:dark_combat`, head `base:head_dark_combat`; black rendering remains
an independent asset-owner handoff, not solved by the layout change.

Input cause remains open: desktop1's real focus gain was followed by a global
Agent Select Accept before the readiness stability interval completed. Fixture
events stayed0/10, result11/22; no scripted route or GL-capture acceptance.
ActionMap9872D522 adds verbose-only VK edge, prior-held and focus diagnostics
to establish provenance. No guessed input behavior change or further unchanged
startup retry. Evidence is in the menu audit and `.claude/menu0908/` receipts.

## 2026-09-08 Mod Manager malformed state publication (T-MENUS-006, N-0125)

16:04: matching clientE36F3211 buildPASS, source387 unchanged and native guard
reports ok. Bounded source/build/direct helper-IO repair is complete; ordinary
UI/restart and related full Apply/component-state defects remain open.

16:02: focused production persistence14 PASS/256 assertions with zero source
or binary drift, receipt `.claude/menu0908/modmgr-persistence14-exit.json`.
The source defect is repaired and direct helper/file-IO validation passes.
Matching client build/native guard and ordinary UI/restart proof are pending.

15:56: four product/test paths applied after explicit release and frozen with
exact hashes in `modmgr-state-review/applied-four.json`. Shared CMake/build
pending; all fourteen cases and runtime remain unverified. Prior findings below
describe the repaired source baseline, not a validated current runtime result.

15:50 follow-up: the paired writer also inserts unescaped IDs into JSON and
directly truncates the destination. A five-path strict-load/ordered-publication
and escaped atomic-save candidate with fourteen tests (15:52 revision) is prepared in
`.claude/menu0908/modmgr-state-review/manifest.json`, UNAPPLIED/UNRUN pending
the shared source window. This does not close checked Apply or component state.

Open; high impact on saved mod selection. Source review in the menu session
found `modmgrLoadModsEnabledJson` disables live registry rows before validating
its input, accepts EOF in place of a closing array, ignores invalid/non-string
tokens, and reports successful load. The shared tokenizer accepts unterminated
strings. A truncated or malformed `mods-enabled.json` can therefore publish a
partial/empty selection and suppress fallback. This has not been runtime-tested.

Required repair: validate a complete ordered candidate before any live flag or
order change; reject malformed strings, overlength IDs, invalid separators and
trailing content, preserving prior live state on rejection. Cover the actual
production reader and publication path. The related disabled-state iterator,
late-registration replay, ordered restart persistence and checked Apply defects
remain open in the menu audit. No product changes during the joint source freeze.

## 2026-09-08 menu dedicated-target configure failure (T-MENUS-003)

OSK context correction passed;87 further cases pass (296 total), including all
OSK cases. The Agent popup contract then failed a stale whole-function count
(four suppressions versus three) because the new Back branch also suppresses
activation. The test now checks suppression between each actual popup open and
its body submission, preserving ownership without prohibiting other guarded
transitions. Product unchanged;54 failed/unrun remain. Receipt:
`.claude/menu0908/current-menu-context141-exit.json`.

Pointer geometry correction passed;11 further cases passed (209 total). The
OSK replacement-context section then failed because CreateContext restores the
previous current context when one already exists. The test never switched to
its replacement. It now explicitly selects that context, verifies the switch,
and uses scope cleanup to restore the original even on assertion failure.
Product unchanged;141 failed/unrun cases remain after rebuild. Receipt:
`.claude/menu0908/current-menu-geometry152-exit.json`.

First client compile stopped at MainMenu's new read-only tab producer: its
ImGuiWindow/SkipItems observation needed imgui_internal.h. Added the missing
declaration include only. No client linked or ran; retry requires refreeze.
Receipt: `.claude/asset0905-continuation/batch6-hand-client-compile-error.log`.

Next152 remaining: the first two Settings keyboard/controller cases passed;

Diagnostic established the pointer cause: expected Actions NavId was correct,
but NavRectRel produced center(212,48), above the current Actions row. Native
HoveredId was0 and the press activated a different ID. Test pointer geometry now
uses the native cursor position where the component submits its first Button;
the press must activate the exact Button ID before release. Propagated to Editor
Close's row rectangle. Product unchanged. Diagnostic receipt:
`.claude/menu0908/pointer-diagnostic-junit.xml`. Coordinate correction unrun.
pointer Actions stopped with its synthetic movement and press sharing one
frame. The precise pointer failure remains under investigation; both cases submit an
ordinary hover frame before press/release (Actions and Editor Close). Product
unchanged; the cadence hypothesis is unverified. Retain198 current passes. Receipt:
`.claude/menu0908/current-menu-remainder154-exit.json`.

The hover-frame retry failed the same pointer case with zero new passes, so
cadence is not established as the cause. Added focused native pointer position,
hovered window/item and active-ID diagnostics before release. Next gate is the
single failed case under this diagnostic build, not a broader unchanged retry.
Receipt: `.claude/menu0908/current-menu-remainder152-exit.json`.

The210 remainder passed56, then the cold Settings helper harness failed opening
Actions despite matching NavId. ImGui's FocusApi explicitly leaves nav visibility
unchanged; a cold context has it hidden and cannot activate buttons. The test
fixture now applies the production visible-focus policy when requesting native
focus and asserts that prerequisite. No product change. Current retained total
is196 exact passes;154 failed/unrun cases remain after the test-only rebuild.
Receipt: `.claude/menu0908/current-menu-remainder-exit.json`.

The third shared build passed. Current menu gate then passed140/350 cases and
stopped at the old capture-only scroll source assertion. The integrated OSK
correctly adds its neutral-release gate to scroll ownership. Updated only
test_right_stick_scroll.cpp to require the shared sampled axis and both owner
guards; propagation search found one stale expression. Product unchanged.
Retain140 exact current-unit passes and run the210 failed/unrun cases after the
test-only rebuild. Receipt: `.claude/menu0908/current-menu-exit.json`.

The corrected configure passed; the next build stopped linking the dedicated
Firing Range target. Windows COFF retained reference-pointer sections for
unrelated training dialogs despite function/data section GC. The existing
PDGUI_TRAINING_RENDER_TEST define now excludes unrelated renderer bodies and
registration while retaining the exact weapon-list renderer and shared helpers.
Normal client compilation still includes all dialogs. This is a test-link
correction, not a game behavior change; another frozen build is pending.
Link error: `.claude/asset0905-continuation/batch6-hand-build-tests-menu-link-error.log`.

The first shared build stopped before compilation at CMakeLists.txt:1332:
`target_compile_features no known features for C compiler GNU version 15.2.0`.
The supported headless wrapper forces compiler identification and does not
populate this feature table. The new dedicated menu targets redundantly queried
it despite project-wide C11/C++20 settings. Removed that one redundant call;
both targets now inherit the same standards as pd-tests. Propagation search
found no other target_compile_features call. Correction awaits a fresh queued
build; no test or runtime pass is implied. Error receipt:
`.claude/asset0905-continuation/batch6-hand-build-tests-configure-error.log`.

## 2026-09-08 multiplayer source audit

T-NETWORKING-010 records 18 prioritized source findings in
[the multiplayer audit](audits/2026/multiplayer-audit-2026-09-08.md).
The main defect classes are listen-host lobby dispatch, nontransactional room
membership, stale room settings/playlist replay, cross-channel distribution
completion and approval, receive allocation failure, client-position movement
authority, chat endpoint/delivery, and voice audience/mixing. All remain open.
No repair or new build/runtime/visual/controller acceptance is claimed.

## 2026-09-06 menu batch source findings

T-MENUS-006 source review confirms archive import can lose the previous file:
`modmgrCopyFileAtomic` unlinks the destination before rename, then deletes the
candidate if rename fails. Hub import and received archive installation reach
this helper. A separate scratch correction will use the existing checked
atomic replacement API; no current product edit or multi-file transaction claim.
Component-state persistence also iterates only enabled entries while its writer
requires disabled entries, and rebuild replays state before package loading can
re-register rows. Truthful Apply failure reporting must include refused catalog
retirement and mount/admission outcomes; changing only the UI result is insufficient.

T-MENUS-003 propagation review at14:01 ET confirms Room character preview can
redirect a selected public body/head mesh through a native filenum alias:
`pdguiCharPreviewRequest` -> `menuRenderModel` -> `modeldefLoadFromHandle`
replaces the supplied FileProvider handle with the first external filenum
match. The body/head manager path retains the selected source, so its passing
load alone cannot establish preview correctness. Asset root owns the narrow
`src/game/modeldef.c` explicit-source preservation and real selected-triangle
witness, alongside native primary alias admission and both-order hand/private
collision rejection. No preview/runtime proof yet; menu.c remains unchanged.

Tracked under T-MENUS-003/T-INPUT-006/T-MENUS-006: reachable Extended Key
Bindings generated 21 rows over 19 actions, unchecked saves dismissed capture
on failure, and missing device bindings could overwrite slot zero. The current
batch adds row guards/count correction, checked persistence and bounded device
slot lookup. Shared interface deletion also lacked Back and opened its status
popup under the closing popup's ID. Affected focused cases now pass within302;
the client builds, but ordinary menu verification is blocked in asset boot.

The remaining caller audit additionally establishes source defects under
T-MENUS-003: Social polls raw Back around editors and bypasses shared ownership;
Agent/Solo/Cinema/Main Menu/Results allow competing forward and Back outcomes;
Firing Range can push through both global Accept and native selection and
override its focused Back footer; Cinema clamps away its valid Back index on
the next frame. Exact scopes and source hashes are in
`.claude/menu0906-next/remaining-audit/README.md`. These repairs are a subsequent
scratch unit, not part of the previously accepted 235 cases. Mod Manager core
Apply/save status and enabled-only component-state enumeration also need a
separate review; UI staging cannot establish disk/runtime rollback.

Historical propagation review found the underlying Main Menu could handle Back
while the Modding Hub overlay was visible. Current source checks Hub visibility
before `ImGui::Begin`, makes the parent window non-interactive, and returns
before Main Menu Back/Social/body actions. The focused production-source
contract in `tests/test_menu_graph.cpp` pins that ordering. The source gap is
corrected; ordinary controller/MKB and visual navigation proof remains open.

T-INPUT-008 source audit also confirms raw joystick buttons23-32 (zero-based
slots22-31) share virtual keys with synthetic stick directions/triggers.
`input.h` allocates both meanings inside the same32-slot player stride, and
`actionmap.cpp` sends raw button and axis events to those same keys. They cannot
be independently bound; labels such as Btn32/Axis6+ reveal the ambiguity but do
not repair dispatch. This is source-confirmed, with no physical reproduction.
Sep22 source correction keeps all old values/names as compatibility aliases and
appends distinct precise button/axis keys. Capture, dispatch, saved names,
Settings and glyph candidates now use the same conversion; the selected
physical-source winner is retained through release. Scoped client/test builds
and direct converter/resolver/glyph cases pass. The broader menu suite remains
red on four unrelated source-contract assertions, and an ordinary raw joystick
plus physical/visual proof has not run. Old ambiguous profiles remain ambiguous
until rebound; see `.claude/menu0906-next/raw-vk-audit` for the compatibility
design and `.claude/menu0922/raw-vk-final-junit.xml` for the retained broad red.
Follow-up source review found a legacy alias could be selected by one physical
source after its other source was shadowed by a precise bind, while the generic
alias label still listed both. The glyph callback now carries the resolver's
actual winning physical VK; focused source and production-helper cases pass
12/957 on rebuilt client/tests. Ordinary device/visual proof and the retained
broad test red keep T-INPUT-008 partial.

---

## Routing rules

Sep6 asset continuation: T-ASSETS-049 also owns music sync cursor defects found
during real Vorbis integration. NaN playback rate reached a float-to-integer
cursor cast; an oversized seek narrowed before its frame bound and retained the
original huge fractional offset. Applied ordered-rate handling and clamped the
floating seek before narrowing, preserving a bounded fraction. A real PCM mixer
regression is prepared; build and runtime validation are pending. T-ASSETS-048
also requires transactional registration of character body/head dependency edges;
source policies alone cannot make embedded custom children usable.

- A confirmed current defect stays in **Active 1.0 defects** until its root cause,
  propagation audit, implementation, and required runtime proof pass.
- A historical fix that only needs broad release revalidation belongs in a
  **Release regression cluster**, not as a separate active task.
- Product or infrastructure gaps belong in Workbench tasks or validations. They
  are not duplicated here as bugs.
- Post-1.0 defects remain visible but are never selected by the default 1.0
  prompt unless Mike promotes them.
- The next unused numeric bug ID is **B-1108**. Never reuse an archived ID. If an
  old unnumbered symptom recurs, assign a new B-number with current evidence.

Status terms:

- `confirmed`: current source or a current production repro proves the defect.
- `repro_required`: a credible report exists, but the current build still needs
  a bounded reproduction before code changes.
- `regression_gate`: implementation exists; a broader release receipt must prove
  the behavior remains fixed.
- `post_1.0`: retained, but outside D-004 release scope.

---

## Active 1.0 defects

The 2026-09-06 menu audit under T-MENUS-003 found shared input ownership
defects: the ImGui bridge overwrites physical arrow keys with action-map state
while its broad keyboard-capture gate swallows navigation bindings; glyph
lookup uses an incomplete private context list; menu scrolling reads gameplay
aim axes that are correctly suppressed. T-MENUS-004 repairs destructive modal
Accept overriding visibly focused Cancel in shared confirmation, End Match and
Agent Copy/Delete. T-MENUS-005 repairs binding capture leaking into parent
navigation and missing controller-accessible clearing. These are confirmed
source defects; behavioral and ordinary-client acceptance are pending.

The same audit's propagation pass confirms additional T-MENUS-004/003 defects:
Solo Restart, Agent Copy/Delete and Room Change Character open popups under a
different ImGui ID scope from their renderers; Agent and Room parent shortcuts
ignore child owners; native combo Back can close both combo and parent screen.
These remain unrepaired at the first shared verification freeze. The menu audit
records exact production locations and the stable-owner repair boundary.
The first actual ImGui gate additionally proved that forced default focus kept
the navigation cursor hidden, preventing Enter/controller activation. The
bounded 11:10 ET repair makes that focus visible across the affected dialogs and
also removes Cheats Unlock Everything's global Accept override with No priority.
Its matching build and remaining behavioral gate are pending.

The active user-directed asset lane has three newly source-confirmed classes
recorded by Workbench without reusing historical bug IDs: T-ASSETS-047 owns
missing bot-profile manifest unlock metadata; T-ASSETS-048 owns fixed-head-only
character consumers rejecting native default/random/integrated-head templates;
the current asset audit records legacy base MP3 framing/sample limits, reverse
mapping loss on generic audio replacement, and advertised Vorbis entrypoints
that only decode WAV. The initially reported missing private MP3 decoder wiring
was an incomplete trace and is explicitly withdrawn in T-ASSETS-046.

The 2026-09-05 user-directed asset audit also tracks confirmed source-reference,
typed graph value, glTF buffer, and animation-tail defects in T-ASSETS-039 and
T-ASSETS-040. Exact producers, consumers, and remaining architectural gaps are
recorded in the [current asset contract audit](audits/2026/asset-source-runtime-contract-2026-09-05.md).
B-1107's replacement implementation and verification run under T-ASSETS-038.

| ID | Severity | Status | Current truth | Workbench route | Closure gate |
|----|----------|--------|---------------|-----------------|--------------|
| B-919 | Low | confirmed | The collision hit-sound condition in `propobj.c` compares `WEAPON_COMBATKNIFE` twice. The intended second family is not proven, so this must not be fixed by guesswork. | V-010 | Verify intended base behavior from authoritative source, correct the shared mapping, add a focused propagation test, and prove combat-knife plus bolt-family impacts in an ordinary client. |
| B-1106 | Critical | source_connected | Exact frozen client `36EA05D1...` completed clean 733/733 `.pdmesh` extraction, then `base:sp_body_118` / nested `base:model_cchicrob` failed the runtime render-stream contract and rolled back 64 Main Menu loads. The exact first mismatch is 33 corners across 11 faces in Type-3 groups `node_2_dl`, `node_6_dl`, and `node_10_dl`: the producer resets geometry knowledge between consecutive opaque/translucent lists while retaining the live RSP vertex cache, but flattened schema v2 had no typed cache provenance and the consumer conflated each vertex's earlier `G_VTX` load-time snapshot with later triangle-time geometry. Current schema v3 records every ordered `vertex_load`, every triangle's three exact `vertex_cache_slots`, one Type-3 `vertex_scope` that retains the bounded 64-slot table, and one non-Type-3 `vertex_cache_reset` that clears it. C/Python validators simulate the same table, reject unloaded/false snapshots, malformed arrays, and matrix indices above 32766, and require every flattened load to have a restorable draw-state domain. The compiler's deterministic Type-3 baseline owns inherited lighting and texture-generation state but not caller-owned fog, which remains fail-closed. Schema v2 remains strict; schema-v3 load/draw divergence requires explicit load provenance. Export/cache v25/v28 plus generated-modeldef cache v12 invalidate predecessors. Luna xhigh identified the retained-slot and parser/bounds gaps; Sol integrated the exact source, validators, conformance cases, and propagation contracts. Exact source-frozen client `7EF396C9...` / tests `4D14886D...` pass focused 143/8, complete 67,451/1,231, all conformance/workflow/source/native guards, and zero source overlap. No replacement extraction or visual receipt is claimed yet. | V-010 | Run one clean 733/733 extraction and ordinary 3440-wide smoke proving the Main Menu manifest commits, `base:model_cchicrob` builds, both door models activate exactly once, and Carrington/Campaign/Combat Simulator retain correct visuals plus V-009. |
| B-1107 | Critical | implemented | Metadata colmesh success bypassed typed runtime activation/hydration, and colmesh unload omitted adapter detach. T-ASSETS-038 now prepares private geometry, activates and hydrates adapters, publishes once, rolls back acquired state on rejection, and detaches before geometry release. The 2026-09-05 ordinary client passes all 19 Scenario transaction cases and match start with strict readiness retained. The corrected V-006 fixture now passes 32/32 including clean exit on unchanged client `2097E1C8...`; source manifest `44186EA3...` is unchanged across the continuation run. | T-ASSETS-038 / V-006 | Retain the original timeout receipt and `.claude/asset0905-continuation/v006-result.json` (32/32 pass). T-TESTS-003 corrected both affected fixture roots to timeout_seconds=125. |
| B-1105 | High | regression_gate | The shared ordered-material planner, request-owned failed-load latch, and SP-74 typed source binder now serve top-level and nested `.pdmesh` ingress with one canonical alias order. Exact client `395D48A6...` passes final focused 132/14, full 67,152/1,223, native-source guard, two-archive verification, fixture validation, 23/23 unchanged source hashes, and strict fresh-cache ordinary receipt `results-20260826T152922Z.json` at 54/54 in 33.8 seconds. The embedded Mauler source owns exactly one 13-material compiler/activation/Bond-gun/render chain, gameplay fires 11 shots, no runtime native/ROM boundary or endscreen occurs, and all four reviewed gameplay captures retain correct silver/green placement and the V-009 no-obstruction gate. | V-010 | Retain the final automation, exact binary hashes, strict receipt, and four reviewed captures. Revalidate this generic source/material contract through representative public mesh families and the V-010/V-013 release matrix; never credit the superseded `D1788982...` client or earlier rejected receipts as final closure. |
| B-249 | High | repro_required | A prior playtest reported bot-versus-bot deaths credited to player 0. Static tracing found no hardcoded player-0 write and the existing diagnostic must identify the upstream attacker attribution first. | V-010 | Run a controlled bot-only Combat Simulator match with no player damage, capture attribution diagnostics, fix the authoritative damage/death source, and prove player 0 remains at zero kills. |
| B-242 | Medium | repro_required | Players have previously spawned intersecting walls. Existing pad and ray validation does not prove a complete capsule fits at the committed spawn. | V-010 | Add one shared bounded capsule-placement validator for players and bots, search safe radial candidates or the next pad without infinite retry, then prove representative campaign and Combat Simulator maps. |
| B-174 | High | repro_required | Car Park previously stacked bots at one spawn and then congregated them at one destination. A mitigation and diagnostics exist, but no current ordinary-client closure receipt does. | V-010 | Reproduce with the original map and roster, prove distinct valid spawns and continued AI decisions, fix the shared spawn/path state if it recurs, and retain a timed gameplay artifact. |
| B-183 | High | confirmed | Character bodies or heads previously showed disconnected triangles or holes. The frozen ordinary two-client logs now reproduce a concrete assembly gap: `body0f02ce8c` for bodynum 86/headnum 4 has no `MODELPART_CHR_HEADSPOT`, so `body.c` safely skips the separate head attachment. This is current source evidence, but no capture yet proves the visible result or whether the exact public body source lost the attachment node. | V-010 | Trace exact catalog IDs for body 86/head 4, restore or deliberately classify the authored head attachment, and capture that pair with no warning, complete head/neck geometry, and exact source identity. Then retain representative human, Skedar, integrated-head, mixed body/head, campaign, and Combat Simulator captures. Current logs: `.claude/smoke-verify-install/logs/game client/pd-host.log:8153` and `pd-client.log:9654`. |
| B-1064 | High | regression_gate | The reconnect slice is production-verified at protocol v57, including endpoint-scoped retry identity, exact world/inventory restoration, resumed authority-accepted fire, clean exit, and corrected retained-log verification at 98/98; the immutable raw receipt remains rejected at 92/95 because it used the superseded B-1100 verifier. The Campaign/player-init unit prepares private player/network candidates before global publication, validates exact runtime-to-typed body/head identity and source modeldefs before player-prop allocation, propagates typed stage failure to disconnect/reset/title rollback, and reverses chr/model/weapon/fireslot attachments on every late chrbody failure. Frozen Air Base 25/25 and full Campaign plus restart 23/23 pass on client `2fc37b38...`. The rejected Combat Simulator run was independently traced to B-1101's legacy animation-frame decoder spin, not the candidate-first transaction. B-1101/B-1102 now pass frozen focused 2,057/27, full 65,073/1,186, the native-source guard, zero-mismatch 2,717/402-file manifests, and exact-client Combat Simulator 56/56 on `133A55C6...` with both starts, real CMP150 fire/hit, stats/awards, Play Again, clean exit, zero decoder/crash matches, and no leaked process. | T-ENGINE-004 | Retain the accepted reconnect, Campaign, Air Base, B-1101/B-1102 automation/runtime, source-manifest, and B-801 guard receipts without rerunning them; broader T-ENGINE-004 lifecycle work remains separately partial. |
| B-1065 | High | regression_gate | `matchOptionsForceBit` now records only option bits the engine actually adds, while one user-view query and shared replace/restore lifecycle preserve user-owned choices across match end, cancel, disconnect, room-settings replacement, and the next start. Frozen focused/full automation passes, and the exact-client two-cycle Combat Simulator receipt starts both matches with `user_options=0x00200001 forced=0`, reaches both results flows, uses Play Again, and exits cleanly. | T-ENGINE-004 | Retain the ownership/lifecycle tests and accepted `.claude/smoke-verify-runs/results-20260826T024226Z.json`; revalidate only through the broader T-ENGINE-004/release matrix. |
| B-1066 | High | regression_gate | One narrow `playerInitStageTransientDefaults` boundary establishes all seven stage-only weapon/input defaults for fresh allocation and `bgunReset`: `weaponnum`, `prevweaponnum`, `prevwasdualwielding`, `wantammo`, `passivemode`, `wantsjump`, and `jumpconsumed`. It preserves `client`, `isremote`, `ucmd`, queued-load ownership, and persistent preferences, and respawn does not use it. Frozen focused/full automation and Campaign transition/restart receipts pass; the exact-client replacement Combat Simulator receipt proves two starts, preserved user/weapon setup, real CMP150 fire/hit in the first cycle, both result flows, Play Again, and clean exit. | T-ENGINE-004 | Retain the focused transition contracts, accepted Campaign receipts, and `.claude/smoke-verify-runs/results-20260826T024226Z.json`; revalidate only through the broader lifecycle matrix. |
| B-1067 | High | regression_gate | The prepared lobby roster no longer relies on mutable lobby slots: source restores declined clients to their exact pre-transaction binding, compacts one accepted roster, derives remote configuration from validated client settings, preserves authoritative teams and the prepared typed mission ID, and orders session-catalog delivery ahead of the manifest. The same transaction aborts before publishing a preparing participant's valid settings change and reverse-unwinds every committed stage player if a later co-op/Counter-Op reset or spawn fails. Current-v58 ordinary-client receipts pass co-op 96/96, Counter-Op 98/98, later-player rollback 60/60, and settings rollback 43/43 on exact client `5DA75BE6...`, with epoch-correct release, clean exits, and no NPC mismatch/resync/rejection loop. | T-ENGINE-004 | Retain the four current-v58 receipts and source-linked transaction contracts; revalidate only through the broader T-ENGINE-004/release matrix. |
| B-1068 | High | confirmed | `scenarioLoad` resets and mutates live match/player/bot state before the file is completely validated. A present invalid typed arena, scenario, weapon, spawn weapon, bot profile, body, or head can fall through to a legacy integer or leave a partial prior/default value; arena loading also writes a stage-table index into the stagenum domain, and numeric casts can wrap. This contradicts V-006's validated fail-closed claim. | V-006 | Parse v3 into a complete local candidate with exact typed bindings and bounded values; allow v1/v2 numeric migration only when the typed field is absent and maps unambiguously; preflight every bot/profile and weapon; publish once; preserve byte-exact live state on every rejection. Re-run corrupt/truncated/mixed-validity load receipts plus a successful v1/v2-to-v3 migration and ordinary-client save/load/start smoke. |
| B-1069 | Critical | regression_gate | The network branch of `mpStartMatch` is now an immutable consumer: roster, teams, bots, options, weapons, spawn weapon, handicaps, typed stage, and RNG are finalized and validated before `SVC_STAGE_START`, while random/quick-team/challenge/backstop mutations remain offline-only. Frozen source contracts pin the post-freeze boundary. Both ordinary D-003 authority-role receipts consume the same session catalog, manifest, ready gate, two-player roster, typed stage, and spawns that the listen host serialized. | T-ENGINE-004 | Retain immutable-consumer tests and both current-product authority-role receipts indexed by `context/evidence/2026-08-14-d003-current-two-role-production.md`; revalidate through co-op/Counter-Op and release lifecycle gates. |
| B-1070 | Critical | regression_gate | Exact mode-typed stage resolution now accepts only the requested `ASSET_MAP` or `ASSET_ARENA` domain while preserving map-only campaign callers. The frozen ordinary listen-host/client receipt passes the valid typed Felicity arena through lobby preparation and the full match lifecycle. | T-ENGINE-004 | Keep the wrong-domain negative tests and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json` (56/56; binary `7261880C...`) in the release matrix; close with the remaining T-ENGINE-004 mode and rollback gates. |
| B-1071 | Critical | regression_gate | Protocol v55 removes the positional lobby handicap cache and transactionally transports each authenticated client's exact body/head IDs, team, nonzero handicap, options, FOV, and name before roster preparation. The frozen ordinary two-client receipt accepts both settings snapshots and consumes the compacted authoritative roster through spawn and endscreen. | T-ENGINE-004 | Retain pure roundtrip/rejection coverage and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json` (56/56; source `f86d23bb...`, binary `7261880C...`); close with reconnect and failure-rollback receipts that prove settings never drift or partially publish. |
| B-1072 | Critical | regression_gate | `mpPlayerSetDefaults` now clears exactly `gunfuncs[6]`, preserving neutral handicap and adjacent pointer state. A Luna xhigh propagation audit found no second live sibling, and the frozen ordinary two-client handshake retains handicap 128 plus exact typed identities through both spawns and match end. | T-ENGINE-004 | Keep the focused boundary test, propagation result, and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json` in the release matrix; close with the broader player-init transition and reconnect gates. |
| B-1073 | Critical | regression_gate | The shared lobby writer and both production callers now serialize the one canonical no-bot form, `sim_type=0`. The frozen ordinary listen host replays its own packet successfully, starts both clients, spawns both players, replicates scores, reaches the time-limit end, and shows the endscreen. | T-ENGINE-004 | Keep the 25-assertion canonicalization proof and accepted `.claude/smoke-verify-runs/results-20260813T091851Z.json`; close with repeated Combat Simulator start/return/restart coverage. |
| B-1074 | High | regression_gate | `mpCalculateAwards` iterated active participant `k` while excluding suicide-column `i`; `i` was stale from an earlier player loop, so KillMaster totals could include one participant's suicides, omit another participant's kills, and award the medal to the wrong player. Current source uses `k` for the suicide column and `MAX_PLAYERS` for human awards, with a verifier domain compile-checked against all 40 `MAX_MPCHRS` slots. The frozen ordinary two-cycle Combat Simulator receipt commits controlled non-tied totals in both cycles and correctly awards KillMaster to bot slot 8. | T-ENGINE-004 | Retain focused participant-domain and award-boundary tests plus `.claude/smoke-verify-runs/results-20260813T121222Z.json` as the ordinary production-path regression receipt; close with the remaining T-ENGINE-004 lifecycle matrix. |
| B-1075 | Critical | regression_gate | Manifest categories remain compact on v55, but exact catalog lookup now preserves the concrete map/arena/audio-family type through validation, diff, load, release, and disabled-row teardown. Transitions prevalidate and load every entering root, roll back new acquisitions in reverse on failure, release old roots only after success, and publish the new baseline last. Focused/related/full automation and the native-source guard pass on frozen `f86d23bb...`; the accepted ordinary two-client receipt contains one exact Felicity manifest load per process and zero mismatch, skip, rejection, rollback, crash, or fatal signatures. | T-ENGINE-004 | Keep atomic failure-path coverage and `.claude/smoke-verify-runs/results-20260813T091851Z.json` (56/56; binary `7261880C...`) in the release matrix; close with stage transition, reconnect, and friend-play manifest lifecycle receipts. |
| B-1076 | High | regression_gate | `mpStartMatch` is now the single Combat Simulator stage-request owner. It executes `menuStop` plus `SCENE_STAGE_TRANSITION_RELEASE_MENU_POOL` before publishing the next stage for offline Room/challenge, listen-authority, and receiving-client starts; caller-side post-request cleanup is removed. Exact client `04DB220E...` passes the ordinary title -> Agent Select -> Main Menu/Play graph -> Room -> gameplay -> endscreen -> real Play Again -> second gameplay/endscreen route 55/55 with one balanced Agent Select owner, two balanced Room owners, two stable typed gameplay/results barriers, no watchdog repair, clean exit, and intact allocations. The retained first run is rejected at 51/55 only because its fixture expected `pass` instead of the harness's emitted `satisfied`; product behavior completed both cycles. The same client preserves the invitee-authority D-003 route at 170/170 with one listen host, one signed typed match-server route, one join, and no probe/relay handoff. | T-ENGINE-004 | Retain both accepted receipts, the rejected verifier-calibration receipt, centralized-owner propagation tests, and V-009 as the visual regression gate in future gameplay captures. Revalidate through the broader base-game/release lifecycle matrix. |
| B-1077 | High | regression_gate | The deterministic near-player bot hook mixed floor and actor-root domains, then ordinary non-absolute CHRINFO regeneration overwrote the desired root height. Floor relocation now commits the desired root and cached ground together; the model records a persistent non-absolute root-height bias across animation init/frame regeneration while preserving authored absolute translation. No visibility, cull, or hit state is synthesized. The source-frozen two-cycle ordinary client held root y=159 for 15 seconds with zero failures, then produced two natural hits and completed both matches. | T-ENGINE-004 | Retain focused root/animation contracts, the bounded diagnostic `.claude/smoke-verify-runs/results-20260813T152854Z.json`, and the accepted 56/56 release receipt `.claude/smoke-verify-runs/results-20260813T153846Z.json`; close with the remaining T-ENGINE-004 transition/reconnect/network matrix. |
| B-1078 | High | regression_gate | The delayed legacy menu autosave queue still routed normal Combat Simulator match-end persistence through `filemgrSaveOrLoad`. On PC, the missing Controller Pak covered the healthy MP endscreen, so Play Again stopped receiving input. The queue now dispatches Agent Profile and MP-player writes to the canonical atomic PC-native JSON writers, strips the legacy player-name newline, consumes invalid sentinel indexes safely, and reports background save results without opening Pak UI. The frozen ordinary two-cycle receipt reaches both endscreens, uses the real Play Again accept edge, and returns without Pak UI or watchdog repair. | T-ENGINE-004 | Keep the 23-assertion focused contract and `.claude/smoke-verify-runs/results-20260813T121222Z.json` plus its native `SAVE.QUEUE` log evidence in the release matrix; close with the remaining T-ENGINE-004 lifecycle gates. |
| B-1079 | High | regression_gate | A direct/bootstrap Combat Simulator start could seed and consume `g_MatchConfig` before the Room renderer's private initialization latch was set. On Play Again, the first Room frame treated that production config as a cold entry and reset Chicago, the bot, and user options. Solo Room return now explicitly adopts the existing configuration and rebuilds only derived selectors. The frozen ordinary two-cycle receipt contains exactly one cold `MATCHSETUP` initialization, exactly one Room adoption, and starts cycle two with Chicago, two slots, and `user_options=0x00200001` intact. | T-ENGINE-004 | Retain the focused reset-versus-adopt ownership contract and `.claude/smoke-verify-runs/results-20260813T121222Z.json`; close with the remaining T-ENGINE-004 lifecycle gates. |
| B-1080 | Critical | regression_gate | Generated character clones share source rodata/GDL ownership, but identity-only provenance once rebuilt a direct generated pointer as a legacy segmented offset and let the hit walker cross mapped memory. Generated ownership now recognizes clones through shared root rodata, exposes exact remaining spans for simple, hierarchy, relocated GUNDL, and attached-head payloads, validates terminal `G_ENDDL` and command domains, preserves pointer width, and bounds character/object opaque/translucent walkers while retaining a native-only corruption fuse. Pure/focused/full automation and the native-source guard pass; the accepted ordinary client produced two natural hits with no `MODEL.GDL.REJECT` or crash. | T-ENGINE-004 | Retain the exact-span and generated-source contracts, full 59,094/1,117 suite, native-source guard, bounded diagnostic `.claude/smoke-verify-runs/results-20260813T152854Z.json`, and 56/56 ordinary receipt `.claude/smoke-verify-runs/results-20260813T153846Z.json`; close with the remaining T-ENGINE-004 matrix. |
| B-1081 | High | regression_gate | Smoke diagnostics are now fixture-owned and finite: every fixture assigns jump logging on or off; generated render steps emit once per owner/stage; the Needler witness arms once per owner; camera-only first-person forcing cannot enable weapon logging; and explicit/generated-audit weapon traces use an atomic 2,048-line budget plus one suppression witness. The exact audit client passed 13/13 with 33 render steps, zero capsule leakage, exactly 2,048 weapon lines plus one suppression line, a natural hit, and clean exit. The ordinary release client then passed 56/56 with all three diagnostic streams at zero. | T-ENGINE-004 | Retain focused contracts, `.claude/smoke-verify-runs/results-20260813T152854Z.json`, and `.claude/smoke-verify-runs/results-20260813T153846Z.json`; close with the remaining T-ENGINE-004 matrix and preserve explicit finite diagnostic ownership in future fixtures. |
| B-1082 | High | regression_gate | The friend-play identity generator duplicated an incomplete Agent Profile v2 object and omitted `besttimes`, so D-005 correctly rejected both generated roles before presence startup. The generator now clones the canonical validated Agent fixture and changes only `name`; focused `[d-003]` proves both outputs are deep-equal except for that name and retain all 21 best times. Current-product ordinary clients pass initiator authority 214/214 and invitee authority 218/218 on exact client `D1C91583...`. | T-ENGINE-004 | Retain the canonical-template/no-inline-schema contract and the paired receipts indexed in `context/evidence/2026-08-14-d003-current-two-role-production.md`. |
| B-1083 | High | regression_gate | Machine-global `mods-enabled.json` could not survive exact Agent v2-to-v3 migration, so the original authority fixture lost its selected custom weapon. The authority-only validated preference sidecar now migrates that selection into the Agent-owned v3 document while the joiner remains clean. Both current-product authority roles now complete one election/listen start/join, package transfer, manifest readiness, two-player stage start, gameplay, and clean exit without player-init rollback. | T-ENGINE-004 | Retain the authority-only migration sidecar, clean-joiner contract, and paired current-product receipts. |
| B-1084 | High | regression_gate | The campaign-derived Chicago arena's authored intro still owned gameplay after `--debug-force-first-person`, leaving the typed weapon switch queued and suppressing fire/effects. Reusable typed readiness now requires a stable gameplay interval and uses a bounded balanced production `ACTION_SKIP_CUTSCENE` assist when needed. Both current-product authority roles pass stable gameplay, source-backed weapon/effect execution, and clean exit on exact client `D1C91583...`. | T-ENGINE-004 | Retain the typed stable-readiness/assist contracts, paired current-product receipts, and V-009 unobstructed-frame visual gate. |
| B-1085 | High | regression_gate | Focus lifecycle now reaches input authority before consumable UI dispatch and reconciles `SDL_WINDOW_INPUT_FOCUS` once per pump. The verifier resolves exactly one visible top-level HWND per PID, checks GUI-thread active/focus ownership, requires fresh source `focus GAINED -> LOST`, and anchors only explicitly tagged post-loss sequences. The final focused batch passed parsing, JSON, assertion self-tests, embedded C# compilation, and 402 assertions in 13 cases. Invitee authority then passed 218/218 at `.claude/smoke-verify-runs/results-20260814T030245Z.json`: exact post-loss owner acquisition, `fire_held=1`, source-backed effects, two scripted exits, zero operational failures/leaks, and unchanged product/test/tool fingerprint `2784d121...`; initiator authority remains accepted 214/214 on the same product binary `D1C91583...`. | T-ENGINE-004 | Retain named-anchor fail-closed coverage and the paired current-product receipts; do not replace fresh SDL/GUI-thread proof with launch order or foreground z-order. |
| B-1086 | High | source_connected | Historical 3440-wide Carrington evidence retains the solid-white far-right door obstruction; the bounded 1920 repro was not wide enough for closure. Schema v2 `model.render.json` preserves exact geometry set/clear order, per-corner load-time geometry and colour state, recursive 64-slot ownership, complete face/group ownership, and exact legacy-v1 compatibility. Current source also centralizes parameter-byte `G_VTX` decoding across extraction, fast3d, collision, and arena conversion, restores the complete star domain, shares top-level cache state only for Type-3 pairs, and makes room collision honor nonzero destination slots. Exact frozen client `36EA05D1...` passed focused 97/7, full 67,361/1,230, conformance/workflow/native-source gates, and unchanged manifests; production receipt `results-20260826T193629Z.json` proved clean 733/733 extraction and clean exit. Its 20/26 visual result remains rejected because B-1106 exposed the missing Type-3 list-scope representation before either door activated. B-1106's exact schema-v3 cache-provenance replacement is now automation-accepted on frozen client `7EF396C9...` but not yet production-verified. This remains distinct from fixed B-1054/B-1055/B-1056. | V-010 | Retain both frozen automation units, then prove exact door state plus Carrington/Campaign/Combat Simulator visual gates at true 3440-wide resolution. |
| B-1087 | High | regression_gate | Smoke preflight treated a typed catalog ID as an install-relative Windows `remove_paths` entry, so `GetFullPath` rejected the fixture before launch. The nonexistent path was removed from all three affected fixtures, and a corpus-wide static contract rejects empty, absolute, parent-traversing, or colon-bearing remove paths across all 130 smoke definitions. Both current authority-role smokes now start from clean per-process installs and pass. | T-ENGINE-004 | Retain the corpus-wide path lint and paired current-product receipts. |
| B-1088 | High | regression_gate | An instantaneous gameplay-ready sample let the authored intro retake control after the verifier advanced. The reusable wait policy now requires a measured continuous stable interval, permits at most one bounded production cutscene-skip assist while false, balances every press/release on success or failure, and validates the whole strict fixture document including incompatible fields and cross-wait holds. Both current-product authority roles pass two stable gameplay windows with terminal assist receipts, no repeated assist/readiness timeout, clean exits, and zero leaks. | T-ENGINE-004 | Retain the pure state-machine/schema coverage and paired current-product receipts; never substitute a one-frame readiness sample. |
| B-1089 | Critical | regression_gate | The cutscene stream introduced at protocol v56 owns one match-room-scoped reliable START/ACCEPT/END lifecycle; the current aggregate protocol is v58. Only server/offline authority mints generations; requests use exact authenticated client identity; acceptance is transactional/idempotent; receivers require the frozen roster mask and reject stale/duplicate/conflicting state; terminal END and stage end preserve prepare/send/commit ordering; teardown retires authority state idempotently. Product freeze `cc8791c1...` passed 60,824 assertions in 1,152 full cases and the native-source guard. Current initiator authority passes 214/214 and invitee authority passes 218/218 on exact client `D1C91583...`, including ordered generation 1 END then generation 2 START, accepted/published/applied skip, stable gameplay, and clean teardown. | T-ENGINE-004 | Retain the v56 cutscene codec/state-machine/teardown contracts inside current protocol v58 and the paired v57-product receipts until the required v58 friend-play rerun. |

| B-1090 | Critical | regression_gate | Global scenario camera/condition work inherited the remote player left selected by a prior per-player loop, so the receiver's real local player never advanced into the authoritative cutscene. The runtime now resolves committed receiver-local identity by player pointer plus `!isremote`, restores it before in-client global `lvTick`, keeps dedicated simulation policy separate, routes graph/native cutscene operations through explicit presentation APIs, and reports success only after authority commit. The current 214/214 initiator and 218/218 invitee receipts both require local player 0 body-ready/committed/active/in-progress, ordered generation transitions, accepted/published/applied skip, stable gameplay, source-backed effects, and clean exit on exact client `D1C91583...`. | T-ENGINE-004 | Retain explicit receiver-local presentation identity, fail-closed global-tick coverage, paired current-product receipts, and the separate V-009 visual regression gate. |
| B-1091 | High | regression_gate | The first B-1064 production-smoke attempt never launched: one regex used an invalid JSON escape, discovery logged and skipped the malformed explicitly requested fixture, and the zero-selection branch returned exit 0. Discovery now throws on malformed JSON, zero selection throws, all 131 definitions parse, focused correction passes 339 assertions in 6 cases, the explicit unknown-selection check exits nonzero, and the later exact reconnect process run produced a fresh named artifact. | T-ENGINE-004 | Retain SP-56's fail-closed discovery/selection contracts and require every release runner to name a nonzero executed test in a fresh result artifact. |
| B-1092 | Critical | regression_gate | ENet reports the remote disconnect datum as zero to the initiating sender's acknowledged-disconnect event. The server now latches its first authenticated disconnect intent before entering ENet and consumes it exactly once during authoritative teardown. The exact `0f377e3e...` ordinary reconnect run records reason 5 with `transport_reason=0 server_intent=1 retryable=1`, preserves one reservation and endpoint credential, authenticates one retry, commits once, resumes authority-accepted fire, and exits cleanly; corrected retained-log verification passes 98/98 on unchanged product `7437d77c...`. | T-ENGINE-004 | Retain SP-57 across every server disconnect caller, including terminal kick/ban/policy races; keep the exact reconnect artifact in the release matrix. |
| B-1093 | High | regression_gate | `smokeCaptureReadinessFacts()` hardcoded runtime player 0 for player presence, cutscene, and control instead of following SP-54's receiver-local identity rule. Current source resolves the committed local player through `playermgrGetLocalPlayerNum()` and makes every readiness fact fail closed when that identity is unavailable or ambiguous. Product/verification aggregates `cee25049...`/`eddd3c9...` remained frozen while the isolated build, focused 510/9, complete 61,302/1,159 suite, and native-source guard passed. The later exact-client receipt proved this ordinary client really occupied runtime slot 0, so B-1093 was a valid structural correction but not the cause of that run's stalled readiness barrier. | T-ENGINE-004 | Retain the canonical local-player projection and exercise it whenever an ordinary client maps itself to a nonzero runtime slot; do not infer runtime slot from stable client ID. |
| B-1094 | Critical | regression_gate | `playerEndCutscene()` served both authored cutscenes and the ordinary Combat Simulator opening swirl, so a broad client return trapped joined players in `TICKMODE_MPSWIRL`. Authority policy now lives at `playerSetTickMode`: authored CUTSCENE exits remain guarded while local MP swirl completion is permitted. The exact `0f377e3e...` reconnect run reaches NORMAL gameplay before and after timeout, commits exact replay state, resumes authority-accepted fire, and exits cleanly; corrected retained-log verification passes 98/98 on unchanged product `7437d77c...`. | T-ENGINE-004 | Retain SP-58's transition-specific authority boundary and both protected CUTSCENE and permitted MPSWIRL tests. |
| B-1095 | High | regression_gate | A host once began its peer-dependent stage deadline before the runner could launch the peer, making cold startup consume the connection window. The host fixture now requires typed `network_listen_ready` before beginning `network_stage_live`. In the exact accepted reconnect logs, listen readiness satisfies first at 41.855 seconds, the stage-live wait begins next, the client joins and reaches gameplay, then reconnect/commit/fire and both clean exits complete; product remains `7437d77c...`. | T-ENGINE-004 | Retain SP-59's causal prerequisite ordering and forbid fixed sleeps or cache-dependent timeout inflation. |
| B-1096 | Critical | regression_gate | The exact `33C8FDF8...` rerun at `.claude/smoke-verify-runs/results-20260814T082045Z.json` advances through the B-1095 listener barrier, ordinary gameplay, timeout preservation, endpoint-scoped credential retention, reconnect authentication, manifest acceptance, stage replay, post-load `CLC_STAGE_READY`, and room-reservation reclaim. Its first authority snapshot failed opaquely before PREPARE. Source tracing found a disconnect death-drop projectile without its required reverse object link. Current source restores that shared invariant, preserves fail-closed validation, returns the first typed component and subject, rolls the compound packet back atomically, and maps only authority-local stage/state write or send failures to retryable timeout teardown. Frozen product `701931d8...` / client `C497DD0F...` and verification `00ec5fb4...` / tests `61FAA307...` pass the isolated build, focused 586/12, full 61,386/1,162, and native-source guard. The next ordinary receipt at `.claude/smoke-verify-runs/results-20260814T090751Z.json` is retained but rejected at 55/93: the projectile-owner failure is gone and no false file mismatch occurs; typed rollback identifies `world_prop_state`, prop 4, after 118 attempted bytes. Allocation order maps sync IDs 1-2 to players and 3-4 to their post-start held weapons; disconnect death marks the departing player's original held prop 4 deleting before allocating its replacement drop. Replacement source treats terminal non-regenerating deletion as exact-set absence while preserving regenerating setup transitions, reports omitted terminal counts/first ID on PREPARE, names every included prop-state predicate with compact identity, validates reciprocal dual links, and retires an unannounced death-drop candidate if `objDrop()` cannot commit. Product `b3df3ed0...` / client `F2A25827...` and verification `38c558b2...` / tests `26861DA1...` remained frozen while the isolated all/tests build, focused 623/13, full 61,423/1,163, and native-source guard passed. Exact client `BD0F9DAC...` then passes isolated builds, focused 468/15, full 66,705/1,204, and the native-source guard on unchanged 2,734/410-file freezes. Its strengthened ordinary run at `results-20260826T104217Z.json` passes 101/105: dynamic Cyclone prop 3 is attached, enters `weaponDeleteFromChr` as deleting/non-regenerating before the real timeout, and every wider exact-world/inventory/commit/fire/teardown contract passes. Normal cleanup fully frees prop 3 before PREPARE, producing `terminal_absent=0`; the pristine reconnect stage has no preexisting dynamic counterpart, producing `removed=0` with `exact_set=1`. Luna xhigh confirms the four failed persistence/removal assertions inverted the desired lifecycle. Corrected source captures the selected sync ID and requires one read-only authority-pool absence witness. A 34-character event token then exposed and structurally closed a 31-character parser truncation boundary. The parser-safe refreeze passes focused 483/16, full 66,720/1,205, guard, and zero drift on client `5AEC7918...`. Its ordinary receipt `results-20260826T111558Z.json` is retained/rejected at 93/109: exact retirement/absence, pristine world, both inventories, and one commit pass before an `objTickPlayer` null-object fault. Source tracing shows snapshot `SVC_PLAYER_STATS` replays the historical dead bit through live `playerDieByShooter`, which records death, drops inventory, and retires held props after exact restore. The corrected frozen unit now passes isolated builds, focused 718/20, full 66,960/1,209, the native-source guard, and zero product/verifier drift. Exact unchanged client `8262681E...` passes `.claude/smoke-verify-runs/results-20260826T121959Z.json` 116/116 with production retirement/read-only absence, one retryable reconnect, snapshot `live_side_effects=0`, exclusive prop topology, exact world and both inventories, one commit, 30 real Cyclone shots accepted by the authority, final credential retirement, clean exits, and zero failures, operational failures, or leaked processes. | T-ENGINE-004 | Retain exact product `1C184C57...`, verifier `E0FA8E6A...`, client `8262681E...`, accepted focused/full/guard logs, the immutable rejected stale-binary and crash receipts, accepted 116/116 ordinary receipt, and SP-60/SP-72/SP-73 contracts. Revalidate through the broader T-ENGINE-004/release matrix; do not accept null guards, cleanup delays, wire/counter mutation, or stale receiver state. |
| B-1097 | Critical | regression_gate | The retained exact `F2A25827...` ordinary reconnect receipt at `.claude/smoke-verify-runs/results-20260814T094551Z.json` rejected a live Cyclone (`prop=4`, `parent=1`) with `attachment_pair`. Root cause was the per-character clone copying only the `modelnode *` vector while `modelGetPart()` bisects the packed sorted `s16` sidecar immediately after it; equip then published one attachment pointer before the corrupted hand lookup returned null. Current source allocates the modeldef, nodes, pointer vector, and sidecar as one transaction, bounds lookup to `0..numparts-1`, requires the hand node before ownership, and keeps reconnect fail-closed. Frozen product `10eab368...` / client `6E4AC13...` and verifier `628d2c66...` / tests `D839DA85...` pass the isolated all/tests builds, focused 655/14, full 61,455/1,164, native-source guard, and unchanged 2,713/400-file fingerprints. Exact client `BD0F9DAC...` now provides the missing production attachment witness: before retirement, stable client 1's Cyclone prop 3 has the reciprocal player parent, attached character model, and nonnull hand node; `weaponDeleteFromChr` then enters the validated deleting/non-regenerating lifecycle. The same 101/105 run completes exact reconnect world/inventories, one commit, authority fire, and clean teardown; rejection is isolated to B-1096's stale persistence/removal expectations. Corrected source retains that attachment witness and correlates its sync ID to a later read-only full-retirement check; verification is pending. Exact unchanged client `8262681E...` passes `.claude/smoke-verify-runs/results-20260826T121959Z.json` 116/116 with production retirement/read-only absence, one retryable reconnect, snapshot `live_side_effects=0`, exclusive prop topology, exact world and both inventories, one commit, 30 real Cyclone shots accepted by the authority, final credential retirement, clean exits, and zero failures, operational failures, or leaked processes. | T-ENGINE-004 | Retain the reciprocal attachment witness, SP-61 model-clone/equip contracts, accepted 116/116 ordinary reconnect receipt, and exact frozen manifests. Revalidate through the broader T-ENGINE-004/release matrix; never require a fully freed dynamic prop to remain resident or to exist in a freshly loaded client stage. |
| B-1098 | Critical | regression_gate | The exact B-1097 replacement receipt at `.claude/smoke-verify-runs/results-20260814T101348Z.json` crashed before ENet while hiding the valid `base:head_christ` sunglasses toggle because generated DISTANCE/TOGGLE relation targets escaped the private head clone. Current source owns and remaps DISTANCE/TOGGLE/REORDER topology, preflights every indexed record, drops foreign HEADSPOT children, recalculates the private layout, and fails closed. Frozen product `920a236d...` / client `2039d142...` and verifier `96601f67...` / tests `c3987ec6...` pass isolated builds, focused 1,541/16, full 63,731/1,165, and the native-source guard. The unchanged-client ordinary receipt `.claude/smoke-verify-runs/results-20260814T110358Z.json` reaches listener publication, two complete stage loads, exact reconnect world/inventory commit, and clean teardown without the former access violation, proving this crash boundary is repaired; its broader reconnect result remains rejected for separate B-1099. | T-ENGINE-004 | Retain the frozen B-1098 topology contracts and no-crash production path, keep SP-62, and preserve the V-009 character/Needler visual gate in future captures. |
| B-1099 | Critical | regression_gate | Disconnect lobby return left the ordinary client in CI's global `TICKMODE_CUTSCENE=6`, then a validated replay minted the new match latch before asynchronous reset and made the general authority guard preserve that stale prior-stage state forever. The explicit `lvReset` boundary now permits only a client with a validated new-match latch and idle authority tracker to retire prior-stage CUTSCENE through the scoped authoritative setter into GE_FADEIN; the general `HasMatch()` guard remains unchanged. Exact client `0f377e3e...` logs prove `previous_tickmode=6 next_tickmode=0 stale_cutscene_retired=1`, two NORMAL gameplay barriers, exact world and both inventories, five real MagSec shots, authority acceptance, scripted exits, and no leaks. Frozen product `7437d77c...` passed isolated builds, focused 1,746/19, full 63,936/1,168, native-source guard, and corrected retained-log verification 98/98. | T-ENGINE-004 | Retain the narrow stage-load boundary, active-authority exclusion, invalid-candidate tests, exact runtime witness, and V-009 visual gate in future gameplay captures. |
| B-1100 | High | regression_gate | The immutable B-1099 replacement receipt remains rejected at 92/95 because it required optional `LOG.WPN.DIAG` instrumentation and forbade correct final credential cleanup across the whole process lifetime. Verification source now requires bounded production `COMBAT: SHOT_FIRED`, orders exactly one `credential_retained=0` teardown after scripted success, and statically rejects both stale forms. The separately hashed offline artifact `.claude/session-builds/v1m1engine004/b1099-retained-log-revalidation.json` passes the exact retained host/client/aggregate logs 98/98; compiled `[b1100]` passes 61 assertions in 1 case. Product fingerprint `7437d77c...` and client `0f377e3e...` are unchanged, verifier is `9f6c126b...`, and no game was rerun. | T-ENGINE-004 | Retain SP-64's product-event and lifecycle-phase rules; never rewrite the original rejected receipt or substitute optional diagnostics for production truth. |
| B-1101 | Critical | regression_gate | A clean exact-client Combat Simulator run reproducibly became CPU-live with no frame/log progress after the first real CMP150 shot. Native live sampling captured 16/16 main-thread instruction pointers inside `modelasmReadFrameData`, with the caller chain through `modelasmIterateThings1`, `modelasm00018680`, `modelSetMatricesWithAnim`, `chrTick`, and `propsTickPlayer`. The legacy reader subtracted frame-data `t6ptr8` from animation-header end `t3ptr8`; when that unrelated span reached zero, `gp` stayed zero and `while (v1 > gp)` could not progress. The correction removes every unbounded production bit-reader entry point; optimized transforms, generic transforms, root-motion `ANIMFIELD_08`, and camera fields all use one bounded descriptor/payload contract. Explicit `bytesperframe`, header, field-width, part-capacity, and zero-byte/no-op checks reject malformed data with typed diagnostics. An allocated animation object with `animnum=0` remains a normal bind-pose state. Isolated source-frozen verification passes focused 2,007 assertions/33 cases, full 64,681/1,181, and the native-source guard; product `9592a63d...` (2,715 files), verifier `c76aadc8...` (401 files), client `A0F46D54...`, and tests `B4D51B51...` remain unchanged with zero manifest mismatches. The first frozen smoke escaped the zero-progress loop and exposed B-1102; after that framing/fallback correction, exact client `133A55C6...` passes the clean two-cycle fixture 56/56 with real CMP150 fire/hit, both results cycles, Play Again, clean exit, zero decoder/crash matches, and no leaked process. | T-ENGINE-004 | Retain the bounded-reader contracts, rejected diagnosis receipts and sampler artifacts, accepted `.claude/smoke-verify-runs/results-20260826T024226Z.json`, unchanged source manifests, and V-009 visual gate in future gameplay captures. |
| B-1102 | Critical | regression_gate | The frozen B-1101 client `A0F46D54...` reached player initialization in the replacement Combat Simulator smoke, then rejected animation 1 frame 0 as `invalid_flags` and crashed in the intended bind-pose fallback. The immutable receipt is `.claude/smoke-verify-runs/results-20260824T124625Z.json` at 16/56 with exit 1 and `ACCESS_VIOLATION`. Root one was schema-v5 `pd_special_parts`: the compiler stored each flags byte beside `header_hex` but copied only the header bytes, so runtime consumed field bytes as discriminants. Root two was fallback ownership: it cleared `model->anim` and called a generic CHRINFO consumer that unconditionally dereferenced it. Extracted flags, exact descriptor bytes, full-u32 frame values, declared part topology, and explicit zero-frame semantics are now parsed fail-closed; producer and runtime share the same exact part/stream measurements; optimized decoding verifies its actual header/bit advancement; malformed v7 animation caches are invalidated by animation-only compiler v8; completed streams reject trailing descriptor bytes before publication; and rejection re-enters the complete matrix core with an explicit null animation input and null-safe bind scale without mutating `model->anim`. Luna xhigh's completed integrated audit additionally found and Sol corrected noncanonical integer/boolean token prefixes, leading-zero raw values, zero-frame part-count collapse, and prefix-only stream validation; behavior tests exercise the production scalar parser rather than only source strings. Frozen product `e6287a9c...` (2,717 files) / client `133A55C6...` and verifier `ad7c38f9...` (402 files) / tests `C9D4511F...` pass isolated builds, focused 2,057/27, full 65,073/1,186, and the native-source guard with zero manifest mismatches. Exact client `133A55C6...` then passes `.claude/smoke-verify-runs/results-20260826T024226Z.json` 56/56 with both cycles, real CMP150 fire/hit, root hold, stats/awards, Play Again, clean exit, zero animation/crash rejection matches, and no leaked process. | T-ENGINE-004 | Retain the exact producer/consumer framing, canonical scalar and zero-topology tests, cache-version boundary, accepted automation/runtime receipts, unchanged source manifests, and V-009 visual gate in future gameplay captures. |
| B-1103 | Critical | regression_gate | After complete wire and identity validation, source detects a valid `CLC_SETTINGS` from the exact preparing participant, aborts and restores the active ready-gate transaction before publishing new lobby settings, then recomputes team policy. The smoke-only trigger uses ordinary `netClientSettingsChanged`. Current protocol-v58 isolated builds, final full tests 66,421/1,200, native-source guard, and frozen manifests pass. Exact-client settings rollback passes 43/43: host logs ready-gate and complete lobby-transaction rollback before settings publication, sends `SVC_MATCH_CANCELLED`, starts no stage, kicks no peer, and both clients exit cleanly. | T-ENGINE-004 | Retain the accepted current-v58 settings-rollback receipt and malformed-packet no-mutation coverage; revalidate only through the broader release matrix. |
| B-1104 | Critical | regression_gate | `lvReset` treats all selected players as one stage transaction, freezes authenticated client-ID order for reset/spawn/semantic roles, and reverse-unwinds the actual commit ledger. The NPC wire path shares one model-complete readiness predicate, bounded transactional appends, and validate-before-apply resync. Exact v57 runtime `results-20260826T055628Z.json` remains rejected after exposing old Carrington NPC state before `SVC_STAGE_START` and asynchronous mutable-state checksums. Protocol v58 replaces the ambiguous wait mask with explicit inactive/waiting/release/active phases, carries one nonzero START/READY epoch, requires separate authority and exact-peer post-load latches, and publishes one complete fresh baseline from dedicated reliable storage. Pending ownership clears only after successful room queue; a canonical sync-ID-sorted digest of exact serialized target/room fields follows the full resync it validates. Current v58 product `03a65922...` / client `5DA75BE6...` passes isolated builds, final full tests 66,421/1,200, native-source guard, and unchanged product manifests. Production receipts pass co-op 96/96, Counter-Op 98/98, later-player rollback 60/60, settings rollback 43/43, initiator authority 214/214, reconnect 99/99, and the focus-independent invitee-authority route 170/170. The immutable route receipt remains rejected at 169/170 solely because its old regex rejected valid epoch 1; corrected static coverage passes 108/2 and separately hashed retained exact-client logs pass 170/170. That proof establishes one invitee-elected in-client ENet listen authority, one separately signed typed match-server route, exactly one initiator join, no probe/relay endpoint handoff, epoch-correct baseline/ACTIVE publication, stable gameplay, and clean exits. The integrated 210/218 fixture remains unchanged and rejected only for B-1085's independent foreground-focus witness. | T-ENGINE-004 | Retain all seven current-v58 receipts, the immutable rejected raw route receipt, corrected retained-log proof, route-type separation, epoch/atomic-baseline contracts, and V-009 visual gate; revalidate through the broader T-ENGINE-004/release matrix. |

These forty-eight are the only live one-off 1.0 bugs. Any newly reproduced release defect is
added here immediately and linked to the owning Workbench milestone.

---

## Release regression clusters

The archived ledger contained many `FIXED-PENDING-PLAYTEST`, `MITIGATED`, and
partially diagnosed rows. They remain evidence, but they are consolidated below
so they do not masquerade as 100-plus independent active tasks.

| Cluster | Historical IDs | Current owner | Required 1.0 evidence |
|---------|----------------|---------------|-----------------------|
| Public-source and graph parity | B-385, B-769, B-772, B-801, B-855, B-935, B-938, B-941 | V-003, V-005, V-010, V-012 | Controlled edits for all 27 public families, deliberate production graph consumers, fail-closed selected source, representative visuals/audio/gameplay, and no ROM/native/loose fallback. |
| Campaign and Combat Simulator lifecycle | B-228b, B-316, B-339 through B-345, B-359, B-360, B-365, B-368 | T-TESTS-002, V-010, V-013 | Full campaign through Credits plus repeated Combat Simulator start, end, return, restart, and stage transitions with no crash, stale manifest, black screen, or missing scene. |
| Bot spawn, attribution, and long-session stability | B-19, B-112, B-126, B-142, B-174, B-204, B-205, B-249 | V-010, V-013 | Multi-map bot spawn and navigation, correct kill credit, 31-bot long-duration stress, bounded stack use, clean crash diagnostics, and no silent process exit. |
| Collision and movement | B-145, B-147, B-206, B-242, B-335, B-339, B-350, B-367 | V-010 | Shared capsule/world collision across floor, wall, ceiling, corner, airborne movement, props, and spawn placement on representative campaign and Combat Simulator stages. |
| Menus, physical input, and vehicles | B-152 through B-154, B-198, B-203, B-209, B-230, B-298, B-317, B-351, B-356, B-361 | V-004, T-VEHICLES-002, V-010 | Real MKB and controller navigation, rebinding, live glyph switching, text focus, pause/end screens, root close/reopen, and complete hoverbike operation without input leakage. |
| Rendering and character presentation | B-18, B-179 through B-184, B-253, B-346, B-362, B-366, B-390, B-395 | V-010, V-013 | Representative stage, character, weapon, sky, text, transparency, material, and first-person captures with exact asset identities and no malformed geometry or stale render state. |
| Save, protocol, distribution, and shutdown | B-267 through B-293, B-327, B-349, B-1041 through B-1053 | V-006, T-RELEASE-005, V-013 | Corrupt/truncated rejection without partial mutation, signed identity and manifest checks, atomic replacement/rollback, exact package lifecycle, clean disconnect, and clean shutdown. |

If a cluster fails, promote the exact reproduced symptom to a new active B-number
instead of reopening every historical row.

---

## Locked recent regression gates

| IDs | Gate | Owner |
|-----|------|-------|
| B-1054, B-1055, B-1056 | Future gameplay captures must show an unobstructed first-person view, a lower-right correctly scaled held weapon, authored GLTF material colour, and readable non-white effect colour. | V-012 and V-013 |
| B-1057, B-1058, B-1059 | Friend play must retain distinct identities, fresh signed authority claims, exactly one listen authority, a separately typed signed match-server route, one idempotent non-authority join, failure rollback, and no probe or relay descriptor promoted to `netStartClient`. | T-RELEASE-005 and V-013 |
| B-1060 | Keep canonical declared room domains authoritative across archive ingress, allocation, portal traversal, and all second-hop consumers. The focused Air Base to Air Force One canary passed 25/25, then the source-frozen 17-mission campaign, live Credits, clean exit, and restart verification passed 23/23 on 2026-08-12. | V-010 and V-013 |
| B-1061, B-1062 | Keep Agent Profile v3 as the sole per-agent campaign and preference authority. Accept only complete v3 or exact v2 plus an optional validated INI migration, prepare before commit, preserve live state on rejection, refuse active deletion, and reject invalid campaign CLI relationships before arming. Final ordinary-client receipts are `results-20260813T030531Z.json`, `results-20260813T030952Z.json`, `results-20260813T031306Z.json`, and `results-20260813T032640Z.json`. | T-TESTS-002, V-010, and V-013 |
| B-1063 | Keep private generated caches under path-checked `$H/mod-cache`, with checked `$S` and `$B` fallbacks, one 128-bit filename key, and the full source SHA-256 in descriptors. The original nested save-root migration smoke passed 24/24 at `results-20260813T030531Z.json` with 594 `$H` cache references and no `$S`, `$B`, or fatal fallback. | T-RUNTIME-001, P-001, V-010, and V-013 |

---

## Post-1.0 bugs

| ID | Status | Route | Reason deferred |
|----|--------|-------|-----------------|
| B-223 | post_1.0 | UI presentation follow-up | Possible compounding of independent full-viewport dim layers has no current release repro. Promote only with a captured failure. |
| B-248-old | post_1.0 | T-MODDING-005 | The Grid free-fly observer gravity issue belongs to deferred Forge/editor work. |
| B-333 | post_1.0 | T-BENCHMARKING-006 | GPU swarm prediction, dedicated-authority alternatives, compression, active-prefix transfer, and reliability are benchmark extensions outside D-004. |

---

## Bug workflow

1. Capture the exact production symptom, build/source fingerprint, log, and
   minimal reproduction.
2. Add the B-number here immediately and link it to one Workbench milestone or
   validation owner.
3. Find the root cause and audit the bug class across sibling call sites. Add a
   systemic pattern to `systemic-bugs.md` when the class can recur.
4. Implement at the shared architectural boundary with explicit ownership,
   lifecycle, failure, and rollback behavior.
5. Run focused tests, the relevant full suite, and an ordinary-client or
   real-peer proof proportional to the defect.
6. Reject evidence if relevant source changed or an exclusive receipt overlapped.
7. On closure, update Workbench evidence/status and remove the row from Active
   1.0 defects during the next dated archive snapshot. Do not grow a second fixed
   history table in this live file.

#### T-ASSETS-050 — public numeric source admission (2026-09-06)

High: asset_runtime integer fields passed strtof directly to s32, admitting fractions and undefined nonfinite/overflow narrowing; glTF _PD_ROOM omitted finite rejection before u32 conversion. Narrow production fixes applied with real source hydration/importer regressions prepared, no build/runtime verdict. Propagation across shared integer consumers addressed by the common helper. Older structural JSON key search, float grammar and string decoding remain separate open source-admission work; no complete metadata-parser claim.

#### T-ASSETS-046 — standard audio duration propagation (2026-09-06)

Medium: psGetDuration60 assumed all public voice bytes were24kbps MP3, yielding incorrect length for mod WAV/Vorbis and other MPEG rates. Root applied actual decoded-frame duration and pre-pointer channel bounds, with independent WAV final-frame and compressed-format/preserved-playback cases prepared. Three static pins and native guard now enforce source decode and ownership ordering. Unbuilt/unrun; no performance or audible claim.

Catalog growth source defect (2026-09-06, pending): s_registerLocked inserts its new unoccupied row before rehashTable, which skips unoccupied rows and loses the successful insertion at the growth threshold. Cached entry->id and metadata loaded_data=self also survive pool realloc incorrectly. Catalog worker owns bounded core/cache repair and real forced-growth probe; no validation yet.

Mod Manager disabled component persistence (T-MENUS-006, source-confirmed): the
writer uses enabled-only assetCatalogIterateByType while its callback writes
!enabled, so disabled selections are omitted. Rebuild also replays preferences
before final package registration can re-enable rows. At2026-09-08 17:45 a private
nine-path candidate supplies complete atomic file merging, actual including-disabled
catalog snapshots, checked replay and later ordering, with IO8/catalog6/family1
cases prepared. Not integrated, compiled or run. Existing Apply core/UI still
ignore complete failure status; no rollback, restart or physical input claim.
See .claude/menu0908/component-state-review/README.md and integration-fragments.json.

Character selector/wire gap (2026-09-06 12:32 ET, source-only, next batch): room template callback rejects empty head binding, excluding35random_gender and2integrated templates despite all63 activation proof. ASSET_CHARACTER unlock default bypasses component eligibility; stage-start client/bot writers/readers reject head_session0; matchConfig defaults replace empty integrated head with dark_combat. Catalog worker maps exact shared policy/consumer correction; no source edit or runtime proof yet.

Batch5 actual body/head parser failure (2026-09-06 12:40 ET): symbolic type table used declaration order with CASS before MAIAN, while native constants are MAIAN3/CASS4. Authored named types selected wrong native type/rig; first C++ gate46pass then fails expected maian_tall_neck with cass_neck. Root fixes symbolic mapping to explicit native constants and expands all-six-type regression; no test expectation relaxation. Source311/C65DF594 unchanged during failure.

Batch5 installed boot failures — 2026-09-06 13:13 ET: ordinary menu smoke on source312/46DAC86C client2E4448B3 times out before boot readiness after pervasive BODYHEAD.PUBLIC_SOURCE candidate nested mesh hash failures. Exact chain: body/head byte preflight succeeds against identical mesh source, then common binder rechecks with NULL bytes through canonical File hashing, which passes the nested :: path to an ordinary file opener. Extraction worker prepares a VFS-aware nested path branch plus actual first-owner/equal/divergent installed probe. A separate actual emitted base_sp_head_21 head.ini says HEADBODYTYPE_CASS although headdata_authored uses HEADBODYTYPE_MAIAN. loader_enum_reverse.c and loader_pool.c deliberately preserved an obsolete swapped numeric table; root owns correcting both to named native constants and proving real emitter-helper to public-parser roundtrip. No fixes applied yet. Generic sp_head21 admission failure is independently being traced. All seven asset probes remain unrun; menu failed before inputs/captures. Prior helper passes and client build are retained, not runtime acceptance.

Body/head unlock source omission (2026-09-06 13:15 ET, source-only next correction): romextract_pdhead/pdbody public INI omit requirefeature; bodyHeadSourceParseIni defaults it to0 and registerBodyHeadPublicIni overwrites catalog manager eligibility originally set from g_MpHeads/g_MpBodies. Thus fresh public admission can erase native unlock requirements. Record for the forthcoming character selection/extractor parity unit; not yet corrected or runtime-proved. The public source must carry actual base eligibility, while explicit author edits remain authoritative.

MP75/native21 correction applied 2026-09-06 13:18 ET: s_BaseHeads now includes MP75 with authored sp_head_21 identity, preserving actual native21, MP ordering and requirement0 while eliminating the synthetic base:head_75 duplicate. The cached SP selector row count reduces by one because the duplicate disappears. Loader enum export/legacy decoding use named native constants; existing body/head source regression now calls the actual extraction name helper for all six type roundtrips. Corrected build/direct runtime proof remain pending. Old cached author-edited base files were not rewritten.

SFX public source domain mismatch (2026-09-06 13:35 ET): fresh source registration rejects208 native SFX archives because catalog_audio_public_source.cpp treats key_min/key_max/velocity_min/velocity_max as ordered MIDI7-bit ranges. Native n_sndplayer.c uses keyMin bits0..4 for volume channel/bits6..7 for chain ID, keyMax flags+FX mix, velocityMin chain low byte, velocityMax delay. Example base:sfx_unlabeled_avrm public values5/0/176/16 are valid packed bytes. Dependent explosion effect adds one activation failure, making boot ASSETCHAIN209. Exact source-domain fix drafted, not applied. Chain IDs encoded numerically and actual chain/delay preservation remain open c3842/runtime follow-up; do not call the admission fix full sound behavior parity.

Batch5 SFX admission runtime result — Sep6 13:49 ET: all1768 SFX and effect dependency now register, source313/9CB05A8E client9C2057D9 unchanged. Installed20260906T174745Z body/head retry stops51.5s,3/9 outer assertions,0/7 cases on one first-pass body failure: sp_body108/model_ceyespy native mesh bridge collides. The second scan says68/68, but first scan67/68 is authoritative failure. Prior install first scan68/68; investigate ordering among legitimate model_ceyespy/_hi/_lo aliases sharing FILE_CEYESPY75 before changing ownership rules. No further gates; current cumulative77 helper cases and16 Python/guard/selftest pass remain separate. Next UV draft review found source-destructive whole-archive regeneration/cache bump and generator-label rejection; draft stays unapplied pending source-preserving design. SFX chain audit uncovered possible native sound1-based vs extractor array0-based source identity mismatch; bounded evidence/correction draft authorized before chain semantics implementation.

Asset runtime boundary Sep6 14:12 ET: body/head native alias retry on314/DAB3EDD8/client72FBED1D is terminal red100.5s,3/10 assertions,0/8 cases. Full owner scan exposes2 first-pass body hand conflicts (dark_combat and sp_body132 selecting model_combathandslod_hand) plus1 dependent character; second-pass summaries do not erase failures. Inventory of733 mesh rows finds31 aliased native symbols, with one hand group shared by those bodies. Worker owns exact selected hand catalog ID/provider propagation through actual Bondgun queue, UNARMED path and same-filnum cached hand reload. Draft only; no new gate until coherent repair/refreeze. Current five affected source cases/native guard/client builds pass, cumulative80 helper cases retained separately. Standard-source authoring guide now documents current selected audio formats/public descriptor authority, body/head editable sources and explicit pending graph/audio/hand semantics. Source files remain frozen; documentation changes only.

### T-ASSETS-048: later metadata scan replaces admitted body/head sources (2026-09-08)

Critical; correction implemented, installed validation pending. Specialized body/head walkers bind public descriptor-selected geometry and native scalar pools, then the generic metadata-family table registers the same families again and replaces those handles with private manifest container paths. Installed CamSpy diagnostics prove native file75 and dependency are correct but body primary differs from the selected mesh primary. Removed duplicate body/head metadata entries and private field writers; the existing installed walker case now executes the later metadata pass before actual geometry checks. Evidence: batch6-native-alias-diagnostic-exit/result.json and source-batch6-body-head-single-owner.json under .claude/asset0905-continuation. Arena has a related dual-owner design pending its own public-source admission repair; this fix does not close Arena.

T-ASSETS-048 validation update14:27: the duplicate-body/head-owner fix passes the real later-scan plus selected-geometry witness in all9 installed cases,12/12 outer assertions, source347/EFF4FE2C clientBB6F044D. Arena follow-up remains open.
## 2026-09-08 legacy enabled-mod list loses final exact-fit ID (T-MENUS-006)

Source-confirmed: modmgrBuildEnabledList rejects pos+needed equal to2047 even
though g_ModEnabledList has2048 bytes. With32 enabled63-byte IDs and31 commas,
it silently omits the final ID from the fallback config. Ordered JSON is already
complete. Candidate uses a checked complete builder before persistence, retaining
output on failure; direct exact-fit/capacity/session/order/invalid-registry tests
prepared. Applied16:50 after runtime release; four-path hashes in
.claude/menu0908/legacy-list-review/applied-four.json. At17:16 native4 PASS/26
assertions on tests189B1644/full native2093EA1D, zero drift; tests build green.
Native guard PASS exit0/unchanged source; matching client B56D8334 build0 verified
at17:26 with owned hashes unchanged. Installed asset admission red remains separate.
Receipt .claude/menu0908/legacy-list4-exit.json.
Propagation: only one builder/caller in modmgr.c; primary JSON has separate
maximum-domain roundtrip coverage. Full Apply failure propagation remains open.
