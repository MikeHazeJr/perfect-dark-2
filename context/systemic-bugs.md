# Systemic Bug Patterns - Architectural Issue Catalog

2026-11-18, N-0972. Check the complete archive inventory at both offer
entry points. The host and a proposing client now use the same typed
member check as the receiving side before requesting transfer consent.
Nested packages must have exact supported native member paths.

2026-11-18, N-0973 through N-0979. Reconstruct live placement edits
from the exact approved draft when applying geometry. Preserve each
eligible decoration's pad through reopen and check native starts, paths,
waypoints and links before moving it. Check the resident prop and its
supporting floor before swapping the prepared layout. The focused native
fixture verifies only the intended pad and bounds members changed.

2026-11-18, N-0968. Treat an unbound native position as a rigid transform.
The public hierarchy uses part -1 for a node that follows its parent
without an animation channel. The native compiler stores that convention
as unsigned 65535. The optimized animation matrix evaluator used it as a
decoded-frame index; the same character crashed in the installed and
temporarily mounted worlds. Keep the static local offset, and validate
both direct and mirrored channel indices before reading frame scratch.

2026-11-18, N-0955. Check the runtime model slot for typed placements.
A standalone original mesh can validate and compile while a native setup
prop still resolves to model -1. Check both installed and leased catalog
paths through native setup. The shared private model and source-file
binder now reserves both identities for exact standalone originals.
Managed receipt 653e2159 shows the placed mesh in different real slots on
host and guest, then after the guest's offline variant restart.

2026-11-18, T-NETWORKING-013. Check both bounds on archived data spans.
Extraction and enumeration share the archive's actual byte count. Validate
the full local header, compressed range, and central directory cursor before
reading or inflating. Use widened offsets or checked subtraction. Expansion
budgets limit output size and complement the input range checks. The Grid
review path applies both checks before probing received native source.

N-0829 Debug-only source guards are not production source enforcement. Inspect default gating and caller continuations: gun loading accepted non-file provider continuation while Debug.AssetSourceOnlyType was ASSET_NONE. Owned route now fails unsupported sources; global provider code unchanged. Dead legacy path cleanup and runtime proof remain open.

N-0826 Budgeted-loop cursor ownership: commit persistent progress before an early budget return; otherwise the last processed entry is revisited. bgunTickGunLoad TEXTURES corrected in source; exact-multiple completion timing preserved. Full-width pointer-union and callback reads must survive graph conversion; regression execution deferred until ALL conversion.

2026-10-26 N-0817: Pointer-sized transport types do not establish target-object width. Gun-loader uintptr_t* capacity aliases addressed native32-bit fields, permitting8-byte writes across adjacent state. Use actual pointee types (u8** cursor/u32* capacity), audit every producer/consumer and signed-corresponding alias, and retain actual-object neighbor regression cases. Source fix complete, tests UNRUN and runtime validation pending.

2026-10-26 N-0805: Donor-fixture scalar widening can conceal bitfield and signed-narrowing behavior. Check every modeled field against native declarations, including bit width; do not infer storage from bool-like names. This affected unrun switch fixtures and is corrected. Source review is not runtime acceptance.

## 2026-11-15 Scenario-source stages have no room render data; native BG hit tests on it go silent

Found with B-1187 (T-MODDING-008). When a stage's background comes from a
scenario source (`g_BgUsingScenarioSource`, which includes the multiplayer
arenas), `bgBuildScenarioSourceTables` creates every room with
`gfxdata = NULL`, `vtxbatches = NULL` and `loaded240 = 1`, and `bgLoadRoom`
only re-marks rooms loaded. Native code that finds background hits through
that render data gets "no hit" without any error: `bgTestHitInRoom` returns
false for every room, and `gfxdata`-guarded paths skip. Rooms off screen for
a while are still "unloaded" by `bgTickRooms`, so code with an unloaded-room
fallback works there only, which hides the problem on some surfaces.
Collision tiles and the world collision mesh (`g_WorldMesh`,
`meshRayCastWorld`) are built from the same source and do work.

Search: `grep -n "bgTestHitInRoom(\|gfxdata\b\|vtxbatches" src/game/*.c`

Audit (2026-11-15):
- `propobj.c` `func0f06cd00`, sticky projectiles: fixed (B-1187).
- `prop.c` hitscan (`shotCalculateHits`, `shotTestLos`): confirmed broken
  (MULTIPLAYER's logs: every player shot `MISS`, never `HIT_BG`); fixed
  (B-1189), Farsight x-ray untouched.
- `splat.c` blood splats, `spawnpool.c` spawn clearance, `explosions.c`
  scorch marks (its `gfxdata` guard): fixed (B-1189).
- Not changed: `artifact.c` (`gfxdata`/`loaded240` checks, lens artifacts),
  render-side code.
Fix pattern: on `bgUsesScenarioMeshHits()`, call `bgTestHitScenarioMesh`
(`shot` for hits that should stop at visible solids: floors and
`BLOCK_SHOOT`; not `shot` for clearance: every surface); keep the native path
for ROM stages. Do not use `meshRayCastWorld` for hits: it tests every
triangle, including movement-only clip walls, and ignores the grid.

## 2026-10-13 Validate the evaluated transform used by collision diagnostics

Visibility exclusion can remove an object from dependency-graph evaluation.
After setting a hidden target pose, evaluated_get may expose stale world data;
zero surface-overlap pairs then describe the wrong pose. Pistol V6 exposes10
such hidden rows while25visible rows match the expected rigid transform.
Record source local/basis and evaluated world transforms, compare with the
requested pose, and evaluate before excluding the object. Restore visibility
and the original indexed signature. A hidden pixel, manifold mesh, source
envelope or collision-pair count alone cannot establish concealed-solid sweep
clearance. Source, finite evaluated samples and continuous/native proof remain
separate. See the retained sampled-transform-audit-v1.json in the V6 packet.

## 2026-10-13 Validate the produced object representation at every strict schema boundary

A PowerShell Hashtable or OrderedDictionary is not a PSCustomObject. A strict
validator enumerating PSObject.Properties sees container properties instead of
the intended schema keys. The retained owner producer's two reference objects
were corrected and qualified, but the private native cohort later passed six
literal Hashtable fact records to the same strict DTO validator. Real native
choreography finished, then the first formal case failed before publication.
The original Unknown and checked failed closeout remain retained under N0687.

Keep the strict consumer contract. Explicitly produce the declared object
representation at the schema boundary, preserve scalar widths and exact fields,
and qualify the actual producer/consumer pair on selected PS51. Source grammar,
JSON-shaped appearance and an unrelated earlier passing DTO fixture do not
cover another producer. N0688 assigns the private one-boundary correction and
six positive/six legacy negative data-only cases; they are not native proof.

## 2026-10-13 PowerShell validation and collection conversion need actual host coverage

Parameter and local variable names share case-insensitive binding. A loop
variable named mode inherits a Mode parameter's ValidateSet restriction and
can fail only when the native operation has already started. Use a distinct
local name, and exercise its parsed binding under the real parameter
validation. On Windows PS51, @ over a generic List[object] can throw an
argument-type error; explicit ToArray preserves its ordered journal values.
Test the real expression and round-trip the resulting evidence on PS51.
Source-plan approval alone did not establish those runtime language paths.
The failed receipt test retained its original holder until checked physical
closeout, then released its queue as failed. N-0622 records the evidence.

## 2026-10-04 Native write guards must cover actual link routes and device visibility

MinGW client objects contain direct symbols and imported-pointer references;
wrapping one spelling does not establish coverage of the other or its POSIX
alias. A configured-source inventory caught real `rmdir` references in map
import/distribution after `_rmdir` was guarded. The common policy now inventories
64 routes; an actual link witness and behavioral tests separately prove them.
The known-API source guard binds reviewed exception call contexts and fails on
new known write/dynamic routes. Static library versions need separate audit.

Filesystem policy also intercepts SDL HID opens. Denied/unknown devices must
latch failure before controller admission and smoke success, since silent
enumeration loss could falsely report a free player slot. Preserve ordinary
OS failures such as missing optional Discord pipes. Synthetic policy/IPC tests
pass; physical device acceptance remains unverified. The wrapper locks both
consumer and canonical fixture against replacement through launch and release.
Inherited outputs require separate Win32 and CRT identity checks before boot
diagnostics; an already-open base handle otherwise bypasses path-open guards.
Output refusal keeps interception active until exit. Nonmutating debug-home
routing may name the declared root while disk mutations require descendants.
These cases pass the final22-case/321-assertion suite and both actual PS queries.
`build-session -Target all` currently builds client/updater, so invoke explicit
`-Target tests` for test rebuild evidence. T-TOOLING-011
[audit](audits/2026/pd-native-reuse-isolation-2026-10-04.md).

## 2026-10-03 Native JSON transport must define its byte encoding

PowerShell5 native pipelines can use the caller encoding despite a local
OutputEncoding assignment, and Python stdin's Windows locale decoder can
misread UTF8 BOMs or silently corrupt Unicode paths. CI37163228953 exposed the
first failure; actual tiny fixtures reproduced both. Storage-Harness now sends
UTF8 bytes through Process stdin and drains both outputs concurrently; child
Python uses UTF8 mode and storage.py decodes the byte request as utf-8-sig.
Do not change global console/env settings or rely on ASCII-only fixtures.
17Python cases and19checks eachPS5/7 cover ASCII/Unicode paths, BOM/noBOM and
caller ASCII/BOM encodings. tools/devtools pattern search found only storage.py.
[Evidence](audits/2026/pd-existing-base-reuse-2026-10-03.md). T-TOOLING-008.

## 2026-10-02 Probes must witness the requested native consumer and its real bounds

Loaded registration and compiled sequence buffers did not establish audio player
starts. Audio probes now require the exact public file-voice/MP3 owner or seqPlay,
then allow a normal scheduler window before exit. A fixed128part animation probe
manufactured high-part errors on a15part clip; full bounded descriptor counting
rejects malformed streams without partial counts and limits actual native decode.
Keep these results separate from listening, played poses and visual fidelity.
T-TOOLING-008 source/evidence: audits/2026/pd-initial-night-checkpoint-2026-10-02.md.

## 2026-10-02 Native console transport needs independent bounded drainage

Inherited stdout overflowed the execution transport before a canonical verdict.
The first run remains failed/pinned. Both single and multiple process launches
now drain stdout/stderr continuously into bounded first/final-byte logs; errors
fail the wrapper independently of SDK exit sentinels. The ordinary game log
remains authoritative. Synthetic dual1MiB pipes and read failures pass35checks
eachPS5.1/7; actual native audio drains710587stdoutbytes with131072retained and
exits cleanly. A deferred exit also needs recurring normal-frame polling after
the smoke watchdog; invoking it only once at startup cannot close a timed window.

## 2026-10-01 Descriptor references do not establish a native dependency closure

CCTV's descriptor selects material/hierarchy files; four texture catalog IDs live
in model.mtl. Treating the descriptor as the whole source graph silently produced
an empty dependency list. Bounded public INI/material/JSON/glTF scanning now records
references/incomplete scans and excludes _meta. Propagation covered these source
forms, native conversion and ledger promotion. Successful mesh compilation leaves
separately owned texture bytes/drawing pending; packaging hashes do not prove
consumption. Native callbacks capture actual reads rather than claiming every
inventoried member was consumed. T-TOOLING-009 source/contract evidence:
.claude/asset-inspection/20261001-native-emitter/. No broader graph migration.

## 2026-10-01 Validate domain propagation and test registration with native consumers

Widening a private texture carrier requires every bind-time range check as well
as packing, caches, prefixes, collision/hit reads and sentinels. Two residual4095
material checks passed unit tests but rejected widened public-model textures in
native loading; the downstream CCTV setup then dereferenced the absent model.
Fix the actual catalog/map binding domain before classifying a downstream crash
as an unrelated defect. The rebuilt source6/6, animation19/19, body/head13/13 and
character63/63 gates pass; no lifetime recycling or source fallback added.
CMake's game-source discovery did not imply test-source discovery: its explicit
test list omitted the new marker cases. Required12-case admission correctly
rejected a nine-case all-pass result; registration/rebuild produced12cases189649
assertions. Keep failed evidence and coverage requirements alongside pass counts.
Proof: .claude/pd-initial-integration/20261001-native-validation/summary.json.

## 2026-10-01 Serialize primitive witnesses, not PowerShell provider objects

Get-Content strings can carry provider/drive/path metadata. Deep PS5.1 JSON of
a failed assertion walked those properties and grew the owned receipt finalizer
to9.42GB; the backend had not started. Copy the text at the assertion boundary.
Propagation search in Test-Assertions.ps1 found the forbidden failure Line as
the only log-string field; other sequence/count fields are numeric. A safe
shallow metadata probe and ten actual failure/roundtrip/format checks in both
PS5.1 and7 establish bounded252-byte receipts. Preserve the active owned install
when containing a wrapper, then complete its exact receipt without relaunching.

## 2026-10-01 Retained source generations must fit the native identity domain

The all-character native probe exposed texture generation allocation failure
after35 passes. Source-hashed lifetimes protect data but still consume private
identity slots: the generation/custom-ID tail has593 slots below a12-bit4096
ceiling. Capacity, snapshots, stage pins, deduplication and every native packing
consumer need review together. Increasing only the array limit silently aliases
packed identities; prematurely freeing a pinned generation breaks retained model
consumers. Distinguish measured occupancy from a leak. Model/texture acquisition
and stage-pin release paths were read. At08:23, propagated PC16-bit marker/tex/
cache/prefix/collision/hit source repair and sentinel migration are implemented;
native rebuild/validation pending. Custom allocation must honor snapshot leases
after reset, as generation allocation already did. Snapshot/base-sized cohort
cases are prepared. Occupancy diagnostics distinguish retained generations from
custom/snapshot rows; no leak claim or premature release. Actual fbiguy failure
and36 operational witnesses remain historical evidence in the initial audit.
At09:53, the full propagated repair plus two residual material bind bounds has
rebuilt bounded native proof:12 texture cases/189649 assertions and all63 character
templates pass. Snapshot theft regression and base-sized retained cohorts pass;
live-stage/GPU release order remains intact. This proves capacity/domain behavior,
not a measured absence of leaks or whole-corpus fidelity.

## 2026-10-01 Preserve readiness contracts when bounding validation runs

A validation variant can preserve assertions and boot arguments while shortening
the precondition needed to reach them. Here a 30-second Agent Select wait expired
during weapon bootstrap, before main-menu arming. Compare complete input sequences
and overall timeouts with canonical fixtures, as well as assertions and boot args.
Bound the outer run using measured setup cost and canonical native caps. Check
bootstrap/readiness witnesses before attributing later missing input or capture
events to menu or OS focus defects. Propagation found preview and keyboard private
variants; both are corrected, failed evidence retained, and native retry pending.
The next plan budgets all four canonical caps and retains storage verification.

## 2026-10-01 Match interpreter path and filesystem semantics to the caller

Canonical MinGW/MSYS PATH order is intentional, but a Windows PowerShell caller
must not infer native Python semantics from the command name. Storage needs both
Windows absolute paths and native exclusive handles; translating the script path
alone would leave retention protection incorrect. Select a verified native
interpreter at this boundary without rewriting the build environment. Propagation
inspection found the new asset-ledger driver had the same absolute-path call; it
now reuses the resolver. Existing relative-path fixture generators remain outside
this fix and need review if their own path handling fails. Tiny storage/inventory
integration under MSYS-first PATH passes; native gameplay is a separate gate.

Sep29 T-MENUS-004: A renderer that also handles input must not run for a
background legacy transition dialog. Agent Select consumed Back while Create
owned the current menu. Shared menuRenderDialog now admits only menu->curdialog
to pdguiHotswapCheck. Propagation search: rg -n 'pdguiHotswapCheck\(' src port;
there is one production queue-admission caller. Keep background drawing and
interactive callback dispatch distinct. Current ordinary verification is
blocked by OS foreground access; do not infer it from native tests alone.

## 2026-09-29 Admission and conversion must select the same public descriptor

A permissive graph-host [mesh] read accepted a fixture that the canonical model
converter rejected because public mesh.ini requires [model]. Pure section parsing
was green and did not prove the complete asset chain. Use the same family contract
at both boundaries and retain installed prepare/activate/use evidence. Propagation:
`rg -n 'section="mesh"|ReadIniBytes' port/src tools/smoke-verify`; distinguish
intentional legacy/rejection fixtures from new supported source. Canonical section
negative coverage and installed26/26 prove this repair; world-hit/render remain
separate gates. See graph-production-cutover-2026-09-29.md.

## 2026-09-29 Source lifetime and private slot identity are separate leases

Retaining compiled public model bytes does not protect a reusable native
slot's identity. Catalog reset and snapshot rollback can otherwise rebind the
slot while native pointers or deferred actions still use its old meaning.
Generation owners must retain exact slot identity, reset must preserve those
leases, rollback must validate before mutation, and final release must retire
the pin without restoring historical reference counts. The model generation
and slot allocator now enforce this together; validation is recorded under
T-RUNTIME-001. Source-hashed storage and catalog IDs remain public authority.

Propagation searches: `rg -n -g 'assetcatalog_*_slots.c' 'ResetCustom|RestoreCustom|GenerationSlots|Pins' port/src`
and `rg -n -g 'catalog_*_generation*' 'Slot|Release|Retain' port/src`.
Weapon and body/head allocators still reset plain identity maps; audit their
actual retained consumers before applying equivalent ownership changes.
Audio, animation and texture use separate generation-slot reservations and
must be audited against those contracts rather than assuming model parity.
The graph seam matrix already requires weapon slot protection through gset
retirement. Model repair alone does not close that graph or all-family gap.

## 2026-09-23 Parent archive reuse must include embedded public dependencies

A typed archive that embeds another editable typed archive cannot be reused
solely because its own directory stamp and required member names are unchanged.
When the top-level public dependency changes, the nested copy can claim the
same catalog ID with different source bytes and break a warm install while a
fresh install passes. Search emitters with
`rg -n -g 'romextract_*.c' 'assetArchiveWriterAddPublicDisk|FastCacheCanSkip|s_existingArchiveHasEntry' port/src`.
Compare each embedded dependency with its current public source before reuse;
refresh only a parent whose public members still match recorded hashes, and
fail without overwriting edits otherwise. The 2026-09-23 head/body/character
upgrade fixed 215 failures and retained six obsolete unedited effect sources
outside the active catalog. The weapon, arena and mission propagation passed
an isolated build, 5,562/5,562 changed-source catalog admission, five nested
byte matches, and three edited-parent fail-closed negatives. Other
dependency-bearing emitters still need an audit.

## 2026-09-23 Scope smoke cleanup to the launching runner

Two independent runners may use different coordination resources while both
launch `PerfectDark.exe --smoke`. A runner-wide name, command-line, or path
sweep can terminate the peer's live client, producing a misleading early exit
with no game fatal log. Search runner cleanup with
`rg -n 'Stop-Process|--smoke|smoke-verify\*|KnownPids' tools/smoke-verify`.
Cleanup must use only process IDs launched by the current invocation and their
fault children. Verify a single-client run and a two-process run can overlap
without either reaping the other; the 2026-09-23 Agent/Needler overlap is the
regression case. Runtime recheck is pending.

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


2026-09-08 22:57 batch22 r7 installed warm24 TERMINAL RED32/35: custom slot now safely rejected, not legacy DMA crash. Actual sparse catalog root cause:12 index loops used occupied count instead of pool high-water index; valid later rows skipped after removal. Corrected full class across8 sources (root additionally owns base_extended/langmanifest/romextract_pdmeta), each already checks occupied rows. Fixture enables its texture and next asserts actual reverse-index binding before texLoad. Native26 unchanged; source33 pending combined client/guard/installed24. Prior r3/r4/r5/r6/r7 reds preserved.



## 2026-09-08 Preserve inert metadata state during rejected replacement

A shared rollback helper normalized enabled REGISTERED rows to ENABLED even
when no runtime payload had ever existed. Import admission can legitimately
produce such rows, so this changed state despite restoring source paths and
commands. Preserve exact REGISTERED/ENABLED state for snapshots with no payload,
pointer or references; retain mandatory pointer/count clearing for loaded or
active snapshots. Audit all RestoreRetiredSnapshot callers with
`rg -n "catalogActivationLedgerRestoreRetiredSnapshot" port`; test both inert
row equality and retired runtime ownership, then an actual late sibling failure.
Batch16 native2 and installed folder/loose/typed late/cycle cases passed. Do not
hide rollback failures by relaxing snapshot equality in an installed fixture.

## Enabled-only lookup reused for mutation and rollback (2026-09-08, T-MENUS-006)

The catalog has distinct identity and gameplay resolvers. The enabled-only
resolver is appropriate for ordinary gameplay admission; it cannot locate a
disabled entry for re-enable or restore its bit after a failed disable. Both
sites existed inside assetCatalogSetEnabled. Mutation/rollback must use the
existing including-disabled identity resolver under the lock, then validate
type and expected prior state explicitly. Do not weaken ordinary read filtering.

Audit: `rg -n 's_resolveLocked|s_resolveAnyLocked' port/src/assetcatalog.c`.
Check mutators and rollback paths separately, including the state immediately
after their own writes. Verify failed teardown restores the selected bit and
reports failure; verify disable then re-enable succeeds through real catalog
code. Current repair/tests are drafted in menu0908/modmgr-apply-review, UNRUN.
At 16:30 the repair is applied: actual catalog10, affected lifecycle1 and native
guard PASS on frozen2087E5AC. Matching client build and full runtime remain open.

> Recurring bug classes rooted in architectural mismatches between N64 assumptions and the PC port. These aren't individual bugs — they're *categories* that produce bugs wherever the pattern exists. Use this as an audit checklist.
>
> For one-off bugs, see [bugs.md](bugs.md).

---

## SP-46: Recovery watchdog substitutes for the owning menu close transaction

**Severity:** HIGH - a stale menu-pool slot or input context can survive a
dialog-stack transition, block the next open, or leave input routed to an owner
that no longer exists; a watchdog may hide the missing lifecycle edge by
bulk-releasing unrelated state.

**Pattern:** The legacy dialog stack and the PC menu pool describe the same
screen lifetime, but one path empties or bypasses the legacy stack without
closing the pool-owned slot and input context. A later consistency check sees
`legacy depth == 0 && active pool slots > 0` and repairs the symptom globally.
That recovery is a safety net, not a successful close transaction.

**2026-08-13 proof:** B-1076 reproduced the class in the accepted ordinary
listen-host smoke. At match startup the legacy stack was empty while one
non-optional `agent_select` legacy-stack slot remained active; the watchdog in
`menu.c` logged and released it. The match then passed, so a green gameplay
receipt alone would have hidden the menu ownership failure.

**Audit:** For every non-optional legacy-stack pool slot, trace acquire, legacy
push, input-context ownership, every pop/force-close/stage-change path, slot
release, and context pop as one transaction. Forbid watchdog-repair diagnostics
in lifecycle smokes and assert balanced acquire/release generations.

```powershell
rg -n "legacy-stack|menuPoolConsistencyCheck|menupoolAcquire|menupoolRelease|menuPush|menuPop" src/game port/src port/fast3d
rg -n "agent_select|MENU: watchdog|MENUPOOL:" tools/smoke-verify context
```

**Rule:** The owner that ends a menu lifetime must close its dialog, pool slot,
and input context together. A watchdog may prevent a stuck client, but it never
counts as lifecycle success and must remain forbidden in release receipts.

---

## SP-45: A many-to-one transport token is inverted as one concrete source type

**Severity:** CRITICAL - valid typed assets can be rejected or silently skipped
after transport while an independent legacy/runtime path hides the lifecycle
failure.

**Pattern:** Several concrete source types deliberately share one compact wire or
manifest category, but a consumer reconstructs a concrete type from that category
alone. The inverse does not exist: `map|arena -> stage` and
`audio|sfx|music -> audio` are many-to-one. Choosing the first historical type
causes typed load/release calls to disagree with the exact catalog row.

**2026-08-13 proof:** B-1075 found `netmanifest.c` emitting Felicity as
`MANIFEST_TYPE_STAGE`, then reconstructing `ASSET_MAP` even though the exact row
is `ASSET_ARENA`. Both ordinary-client logs reported
`expected=1 actual=14` and skipped the manifest load, while another stage-load
path let the match reach both spawns, scoring, end, and endscreen. The same
inverse existed for the three audio families. `screenmfst.c` had the same latent
reverse switch, although its two current registrations use only body, head, and
language types. `manifestEnsureLoaded` currently has only body/head/model callers.
The repaired frozen unit passes the strengthened ordinary two-client receipt
`results-20260813T091851Z.json` at 56/56: both exact Felicity loads occur and no
typed mismatch, load skip, transition rejection, or rollback appears.

**Audit:** For every compact category, session ID, enum projection, or tagged
union, write down whether the mapping is bijective. Search for reverse switches
that return one concrete value for a category fed by multiple source values.
Trace exact ID, concrete type, validation, load, retain, release, and disabled-row
teardown together; a successful parallel runtime path is not proof that the
typed lifecycle ran.

```powershell
rg -n "MANIFEST_TYPE_STAGE|MANIFEST_TYPE_AUDIO|ASSET_ARENA|ASSET_SFX|ASSET_MUSIC" port src tests
rg -n "switch .*type|CatalogAssetType|catalogLoadTypedAsset|catalogReleaseTypedAsset" port/src
```

**Rule:** A lossy token is a category, never concrete identity. Resolve the exact
catalog row by its authoritative string ID, preserve that row's concrete type,
and validate that the concrete type maps forward to the received category.
Generic tokens must additionally carry and match an explicit exact type tag.
The transition then loads every entering root before releasing any outgoing
root; on validation or load failure it releases newly acquired roots in reverse
order and leaves the prior global manifest unchanged.

---

## SP-44: A parent-array extent is reused as a nested-field bound

**Severity:** CRITICAL - an apparently local reset can overwrite adjacent
authoritative state in every element it initializes.

**Pattern:** Code iterates a fixed field inside one structure but uses
`ARRAYCOUNT` or a capacity constant from the outer structure array. The loop is
type-correct and often survives review because both dimensions are compile-time
constants, yet every iteration beyond the child field writes into following
members or the next object. A later initialization pass can hide the damage
until a new fail-closed boundary validates the corrupted field.

**2026-08-13 proof:** B-1072 found `mpPlayerSetDefaults` clearing
`gunfuncs[6]` with `ARRAYCOUNT(g_PlayerConfigsArray)` (16). Index 6 zeroed the
neutral handicap set earlier in the same function, and later indices reached
padding and the client pointer. The v55 settings transaction rejected the
ordinary client's resulting zero handicap before publishing its handshake.

The bounded propagation audit found no second live overwrite in player,
multiplayer, match, network, or reset paths. It confirmed the sibling
`gunfuncs` loop uses the nested field's own count and that kill-count copies use
matching destination extents. Runtime parent counts that index nested fields
still require a dominating capacity proof during future changes.

**Audit:** For every nested fixed-array write, derive the bound from the exact
field being indexed or use `sizeof(field)` for a whole-field clear. Search for
parent-array `ARRAYCOUNT` values inside per-element loops and compare every
source/destination field extent in `memcpy`, `memset`, and manual loops.

```powershell
rg -n "ARRAYCOUNT\(g_[A-Za-z0-9_]+\)|sizeof\(g_[A-Za-z0-9_]+\)" src/game src/lib port/src
rg -n "memset|memcpy|for \(" src/game/mplayer src/game/player.c port/src/net
```

**Rule:** A nested field owns its own bound. Never substitute the count of its
parent collection, a sibling field, or a wire roster capacity, even when the
current values happen to match.

---

## SP-43: Partial source projection replaces an authoritative index domain

**Severity**: CRITICAL - valid records can pass source admission and then index
past a runtime allocation sized from an incomplete sibling artifact

**Pattern:** A public format declares one canonical indexed domain, but a
runtime adapter sizes storage from only the records visible in a derivative
projection such as collision triangles, render meshes, active entities, or
currently populated rows. Valid empty or relation-only members at the end of
the canonical domain disappear. A second defect occurs when downstream graph
walkers read raw relationship endpoints and index storage without using the
same validated domain boundary.

**2026-08-12 proof:** B-1060 found Air Base declaring 147 rooms while its
collision mesh mentioned only rooms through 143 and portals legitimately used
rooms through 146. The typed-archive loader did not hydrate the declared count,
so the background adapter allocated 144 room rows and later wrote
`ROOMFLAG_STANDBY` to room 144. Second-hop room loading, lighting, explosion,
and portal-intersection walks also read endpoint fields directly. The repair
transports one checked count through extraction, descriptor/manifest
conformance, catalog admission, and allocation, then routes relationship walks
through shared portal-pair and other-room validators.

**2026-08-26 proof:** B-1105 found the same class in public model material
identity. `model.mtl` is the complete ordered index domain, while OBJ `usemtl`
lines are only a sparse encounter projection. The importer sized and ordered
runtime materials from that projection, dropping a valid unused Mauler slot and
shifting every later material before indexed `model.render.json` consumption.
The same pattern affected one head archive. The repair retains every declared
MTL slot and remaps used OBJ materials by name in both readable-cache validation
and runtime modeldef construction; duplicate declarations and undeclared face
materials fail closed.

**2026-08-12 validation:** The exact Air Base to Air Force One production-path
canary passed 25/25 with the declared 147-room domain and no memory-canary
corruption. The complete source-frozen campaign then passed 23/23 across all 17
missions, live Credits, clean exit, and second-process agent persistence. The
accepted receipts are
`.claude/smoke-verify-runs/results-20260812T224455Z.json` and
`.claude/smoke-verify-runs/results-20260812T224901Z.json`.

**Audit:** For every indexed public-source family, identify the authoritative
count and compare it with every allocation, cache, loop bound, and relationship
endpoint. Search for `max + 1` sizing from only one source projection and for
nested indexing where a relationship field enters another array directly.
Require empty and relation-only trailing members in focused fixtures.

```powershell
rg -n "max.*\+ 1|count.*Alloc|room_count|source_.*count" port/src src/game src/lib
rg -n "g_[A-Za-z0-9_]+\[g_[A-Za-z0-9_]+\[|roomnum1|roomnum2" port/src src/game src/lib
rg -n "newmtl|usemtl|material_index|material_count" port/src tools tests
```

**Rule:** Preserve the declared canonical domain at ingress and validate it
against every observed projection. Size runtime storage from that reconciled
domain. Relationship consumers use one checked boundary that validates the
record index, both endpoints, endpoint distinctness, and membership before any
dependent array access.

---

## SP-42: Persistence writes mutate the only good destination before success is known

**Severity**: CRITICAL - a disk, permission, encoding, serialization, flush, or
close failure can destroy the last valid save while the caller sees success

**Pattern:** A save path opens its final destination in truncate mode and then
streams fields directly into it. Validation, serialization, disk writes,
flush, and close can fail after the old bytes are already gone. A matching load
path may deserialize directly into live globals, so a rejected file can leave
partially updated runtime state. Checking only the initial open or a final
serializer return does not make either direction atomic.

**2026-08-12 proof:** B-1046 found all four PC JSON save writers and the Combat
Simulator scenario writer replacing their only destination before checking any
write result. Binary MP setup save did the same before its checked serializer,
and JSON/binary setup loads mutated live state before complete validation. A
shared sibling-candidate transaction now owns write, durable flush, atomic
replace, and candidate cleanup; setup loads validate into restorable or
temporary state and commit only after the complete document succeeds. The
production client rejects four corrupt-load cases with live state unchanged and
injects two post-write/pre-replace failures with exact prior bytes preserved;
the final V-006 persistence smoke passes 17/17.

**Audit:** Search every persisted writer for direct `fopen(..., "w")`,
`fopen(..., "wb")`, or `fsFileOpenWrite` against the final path. Trace every
writer, flush, close, and rename result. For loads, find `memset` or field
assignment to live globals before structural, version, identity, count, and
cross-reference validation completes. Test a failure after candidate bytes are
written, not only a failure to open the destination.

```powershell
rg -n 'fopen\([^\n]*"w|fsFileOpenWrite|fprintf|fwrite|fflush|fclose|rename' port/src src/game
rg -n 'Load|Deserialize|memset\(&g_|g_[A-Za-z0-9_]+\.' port/src/*save* src/game/*save*
```

**Rule:** Persisted state uses candidate then commit. A write failure leaves the
prior destination byte-for-byte intact and removes only its candidate. A load
failure leaves prior live state intact. Success is not reported until all
serialization, write, durable flush, close, and atomic replacement steps pass.

---

## SP-41: A derived runtime index overwrites the authoritative catalog identity

**Severity**: CRITICAL - peers can agree on content yet disagree on the session key used to admit it

**Pattern:** A boundary correctly resolves a public catalog ID into a legacy runtime number, then a later initialization pass reverse-resolves that number and overwrites the original ID. The reverse mapping is not bijective: maps, arenas, scenarios, aliases, and mod rows can share a runtime stage number. The replacement may look locally valid while no longer matching the manifest, session table, save record, or peer identity that authorized the transition.

**2026-08-12 proof:** B-1041 began with authoritative arena `base:arena_chicago`, derived its Chicago stagenum, then `mpStartMatch` used a map-only reverse lookup and replaced it with `base:chicago`. The server session catalog contained only the original arena identity, so stage-session serialization returned zero and the remote peer correctly failed closed.

**Audit:** Search assignments from numeric/runtime values back into fields named `*_id`, especially after match start, load, reset, random selection, or migration. For each, prove the original catalog row is preserved while it still resolves to the derived value. When a random/meta token genuinely changes the value, resolve through the same asset family and record the identity change explicitly.

```powershell
rg -n "IdBy|id.*stagenum|id.*runtime|strncpy\(.*_id" port/src src/game
rg -n "PRIMARY|DERIVED|DEPRECATED" port/include src/include
```

**Rule:** Catalog IDs are authoritative identities; numeric stage, weapon, animation, sound, and scenario values are derived runtime bindings. Never reverse-map a derived value over a still-valid public ID. If a meta selection changes the runtime value, resolve the replacement within the original typed family and fail closed when it is ambiguous or absent.

---

## SP-40: Fixed-window cache reads past an exact public-source allocation

**Severity**: CRITICAL - a valid source-backed asset can crash only when its
last partial cache window is consumed

**Pattern:** A retained ROM-era cache API accepts a logical byte count but
fills a larger fixed block internally. A migrated public source is loaded into
an exact heap allocation, so clamping only the caller's logical request does
not bound the cache's physical copy. ROM address space or oversized legacy
segments hide the over-read; an exact file, nested archive member, received
package, or allocator guard page exposes it at end-of-file.

**2026-08-12 proof:** B-1036 found that the MP3 decoder callback correctly
reduced its final logical read, while `admaExec` ignored that length and always
copied a 0x400-byte cache window. `mp3Dma` also issued an unconditional 0x400
prefetch at the current offset. The exact typed-public `.pdvoice` allocation
therefore crashed in `memcpy` when playback reached EOF. The source boundary
now owns one checked zeroed cache window, and both logical read and prefetch
paths stop at EOF without signed arithmetic underflow.

**Audit:** For every source-backed cache/import bridge, compare the public
buffer's allocated capacity with the deepest physical read, not only the
caller's requested length. Search fixed block sizes, alignment-down operations,
read-ahead, SIMD/vector loads, codec padding assumptions, and cache fills that
ignore a length parameter. Prove overflow-safe allocation sizing and EOF tests
at exact, partial, and already-at-end offsets.

```powershell
rg -n "memcpy|bcopy|DMA|prefetch|CACHE|ITEM_SIZE|ALIGN" src/lib port/src
rg -n "fsFileLoad|sysMemAlloc|sysMemRealloc" src/lib port/src
```

**Rule:** Any bridge that physically reads beyond the logical request must
either accept the authoritative source capacity and perform a bounded fill, or
require and verify source-owned zero padding large enough for its maximum read.
Logical clamping alone is not memory safety.

---

## SP-39: Optional metadata parser overwrites a caller-owned default on failure

**Severity**: CRITICAL — an absent optional field can silently become a valid
zero identity and hijack an unrelated runtime slot

**Pattern:** A parser accepts an output pointer, initializes it before proving
the requested field exists and is valid, then returns failure. Callers that
seed a semantic sentinel such as `-1`, a nonzero default range, or a prior
value correctly ignore the false return for optional metadata, but the helper
has already destroyed that state. Zero is often a valid runtime index, enum,
volume, key range, or identity, so the failure becomes plausible production
data rather than a loud rejection.

**2026-08-11 proof:** B-1035 found `loaderWalkerEnvelopeInt` unconditionally
writing zero before searching for the requested key. Weapon-animation
manifests intentionally omit character `source_index`; all such command graphs
therefore published `source_animnum=0`, and the last row hijacked character
animation zero. The same helper also erased intended audio defaults such as
key maximum, key base, pan, and volume when optional envelope fields were
absent.

**Audit:** Search helpers that both return success/failure and accept an output
pointer. On every failure branch, prove the destination is unchanged unless
the API explicitly documents a reset contract. Then audit callers for sentinel
and nonzero defaults and distinguish sibling schemas before publishing shared
runtime indices.

```powershell
rg -n "return 0|out_|\*out" port/src/*parser* port/src/*walker* port/src/*source*
rg -n "= -1|= 127|= 64|EnvelopeInt" port/src/loader_walker_*.c
```

**Rule:** A fallible parse helper either succeeds and commits the complete
value, or fails without mutating caller-owned output. Schema variants that
produce different runtime products must not share an index merely because
they share one catalog family enum.

---

## SP-32: Archive-qualified source paths silently truncate across catalog boundaries

**Severity**: CRITICAL — valid public `.pdxxx` sources can be registered under
a shortened provider path and then fail closed or resolve the wrong member

**Pattern:** Public source paths begin inside an INI value or archive envelope,
then grow as catalog/provider code qualifies them into `archive::member` chains.
A fixed 128-byte destination or unchecked `snprintf`/`strncpy` keeps a nonempty
prefix, so its caller treats an incomplete path as success. The final nested
archive/member name or extension can disappear before provider or runtime use.

**2026-08-08 proof:** B-985 exposed the class in UI source paths and B-996 in
font source paths. The propagation audit found 34 remaining 128-byte catalog
path fields, plus unchecked path copies/joins in the catalog scanner, shared
loader walkers, network distribution ingestion, runtime path mirrors, and
weapon-graph archive descriptors. Workbench `T-CATALOG-003` owns the systemic
Wave A/B/C implementation and boundary proof.

**2026-08-08 safe milestone:** Wave A is complete for all 34 fields and the
shared checked path-key/copy/join contract is connected across scanner,
walkers, weapon mirrors, network hot-registration, runtime, FileProvider, and
generated-model metadata. Network overflow restores the prior row or removes a
new candidate before dependency commit. Isolated builds, 399 focused
assertions, guard, and 27-family conformance pass. SP-32 remains open because
the new three-ingress representative client receipt does not substitute for
live all-family coverage or mixed-validity multi-descriptor network atomicity.
See
`context/evidence/2026-08-08-catalog-path-capacity-milestone.md`.

**2026-08-08 Wave C propagation:** B-1002 found three fail-open variants after
the initial capacity migration. Network ingress validated a joined path but
stored its unqualified relative input, so catalog/provider/runtime could
disagree without any overflow. Loose source values containing a directory
separator were incorrectly treated as already qualified. PDCA construction
and extraction also skipped individual hidden, unreadable, malformed, unsafe,
or over-capacity members and could report a partial archive as success. An
already-qualified `::` chain also bypassed parent-traversal rejection. The
shared qualifier now roots every safe relative path, rejects traversal across
filesystem and VFS delimiters, and network mutates the
candidate INI before row population, complete PDCA envelopes and destinations
preflight before writes, and traversal joins fail closed. The behavioral
matrix now covers all 34 affected field mappings across three ingress modes;
the real installed-client representative transport fixture and rollback after
post-preflight PDCA I/O/catalog rejection now pass. Comprehensive live
all-family coverage and mixed-validity multi-descriptor receive atomicity remain
the explicit boundary.

**2026-08-08 transactional receive propagation:** B-1006 proved that complete
preflight alone was insufficient. Direct writes still exposed a partial live
tree after a late open/write failure, and Windows path aliases could map two
lexically different members to one file. Received PDCA extraction now stages a
complete unique sibling tree, compares normalized Windows identities, rejects
ADS/dot/empty/traversal aliases, and publishes by rename with backup restore.
Crash-window backups are preserved and block ambiguous replacement rather than
being guessed at or deleted. This closes deterministic extraction rollback.

**2026-08-08 catalog-admission propagation:** B-1012 proved that atomic
filesystem publication was still too early a commit boundary. The receive path
deleted its recovery backup before typed scanner admission, so a later catalog
rejection could preserve rejected bytes and destroy the prior install. Received
publication is now an explicit two-phase transaction through catalog admission:
commit only after scanner success; otherwise remove the candidate, restore the
exact prior tree, and leave catalog/provider/runtime/dependency/received-count
state unchanged. A positive aggregate scanner count is not sufficient for a
mixed-validity multi-descriptor archive; that broader catalog transaction
remains T-CATALOG-003 residual work.

**2026-08-12 mixed-descriptor root cause:** B-1043 confirms the residual is an
admission-result and mutation-ownership defect, not another filesystem staging
failure. Loose descriptor rejection is represented by the same zero used for
an absent descriptor, while successful siblings increment a shared count.
Typed recursion preserves a negative result but does not own rollback of rows
published by earlier sibling scans. The network boundary therefore cannot infer
all-or-nothing catalog truth from `registered > 0`. The scanner transaction must
latch any recognized rejection independently of accepted count and restore all
rows and dependency edges it changed before the PDCA transaction can commit.

**2026-08-12 mixed-descriptor closure:** B-1043 now gives the complete external
layout scan one deep checkpoint spanning catalog rows, dependency edges,
FileProvider intern paths, every private runtime allocator, `g_Stages`, loader
animation/body/head pools, and body/head manager mirrors. A recognized sibling
rejection latches independently of the accepted count, restores the checkpoint,
and returns a negative result before the network layer decides whether to commit
its filesystem transaction. The installed-client `net_mixed` fixture proves
nine earlier registrations followed by one rejection restore all probed state;
the corrected three-ingress receipt passes 39/39. SP-32 remains open only for
the comprehensive live all-family boundary matrix, not mixed-descriptor
atomicity.

**2026-08-12 private-cache propagation:** B-1063 exposed the same capacity class
after public-source admission. A nested supported `--savedir` made one generated
animation descriptor path 259 characters on Windows. The normalized cache path
in the same directory was four characters shorter and succeeded, while the
descriptor open failed with `errno=2` and forced selected-source startup to fail
closed. Public path-field capacity does not by itself protect generated cache
roots, sanitized component expansion, digest suffixes, or platform filesystem
limits. Private cache path construction needs its own checked end-to-end budget
and a deliberate fallback that never weakens source-hash identity.

**2026-08-12 private-cache closure:** `modasset_compiler` now prefers the
path-checked user-data root `$H/mod-cache`, admits `$S/mod-cache` or
`$B/mod-cache` only when the expanded Windows CRT path fits, and uses one
128-bit digest key for descriptor and normalized animation names while retaining
the full source SHA-256 inside each descriptor. The original nested save-root
ordinary-client migration smoke passed 24/24 at
`.claude/smoke-verify-runs/results-20260813T030531Z.json`; its aggregate log has
594 `$H/mod-cache` references, no `$S` or `$B` fallback, and no fatal signature.
The native-source guard also passed. Keep this as a private-cache propagation
gate while SP-32 remains open for the broader all-family boundary matrix.

**Semantic boundary:** Widen and validate only fields that carry filesystem or
qualified archive-member paths. Do not widen IDs, names, descriptions,
archetypes, shader IDs, voice contexts, or other bounded metadata merely
because they also use 128-byte arrays. Do not globally change generic metadata
copy helpers. `weapon.shared_context` is a path and is included; the currently
rejected `theme.effect_archive` key is not evidence of a supported path.

**Audit/fix:** Replace the 34 catalog path fields and all production mirrors
with one explicit path-capacity contract. Add checked path-specific copy/join
APIs that reject overflow and incomplete archive chains atomically, including
provider admission. Prove exact catalog/provider/runtime equality for every
asset family through standalone typed archives, nested `.pdmod` archives, and
network distribution at 127, 128, maximum-supported, and over-capacity lengths.

```powershell
rg -n "\[(128|FS_MAXPATH)\]" port/include port/src
rg -n "snprintf|strncpy|s_copy|copyStr|IniValueCopy|ArchiveMemberPath" port/src
rg -n "archive::member|::" port/src/assetcatalog_scanner.c port/src/loader_walker_common.c port/src/net/netdistrib.c port/src/asset_runtime.c
```

**Rule:** A path copy or join succeeds only when the complete input and its NUL
terminator fit both the in-memory field and the target platform filesystem.
Nonempty truncated output is failure, and failed qualification or private-cache
publication must not partially register catalog, provider, dependency, or
runtime state.

---

## SP-31: Stage diff releases typed-asset references it did not acquire

**Severity**: CRITICAL — active public assets can lose dependency ownership
across an ordinary stage transition

**Pattern:** `catalogComputeStageDiff` classifies every loaded non-bundled row
as stage-owned by inspecting only `load_state`. It does not know which owner
acquired each reference. A transition can therefore call
`catalogReleaseTypedAsset` for a UI, editor, menu, or other explicit lifecycle
owner's reference.

**2026-08-08 proof:** T-ASSETS-030 activated `example:tri_theme` and its UI,
font, SFX, and music closure with one durable transaction. The ordinary client
then logged all five rows dropping from ref `1->0` at stage transition
(`pd-client.log` lines 4670-4674). Agent Select reapplied the same ID, loaded
the closure from zero, and the same-ID commit released the transaction's
phantom prior ownership back to zero. Render and playback happened to remain
usable from copied/registered consumer state, but dependency lifetime was no
longer truthful.

**Audit:** For every caller of typed load/release, identify the owner and prove
that only that owner can decrement its reference. Test overlapping stage,
menu, editor, network, and explicit parent/dependency-closure ownership across
transitions, replacement, rollback, and shutdown.

```powershell
rg -n "catalogLoadTypedAsset|catalogReleaseTypedAsset|load_state" port src
rg -n "catalogComputeStageDiff|catalogApplyStageDiff" port src tests
```

**Rule:** Stage diff may release only references acquired by stage loading.
Aggregate `load_state` or `ref_count` is not ownership evidence. Workbench
`T-CATALOG-002` owns the systemic fix and blocks T-ASSETS-030 validation.

**Implementation source-frozen 2026-08-08:** `asset_entry_t.stage_ref_count` and
the pure `catalog_stage_ownership` ledger distinguish the one stage-category
owner from aggregate typed lifecycle references. `lvReset` now uses the
owner-scoped stage load/release API, and the diff enumerates only ledger-owned
rows. This protects every typed family without changing its explicit owner.
The propagation audit also fixed weapon dependency release to balance children
on every parent decrement. B-1001 wires the explicit theme transaction into
production shutdown before theme/backend teardown. Source-frozen builds,
focused native tests (5/74), the full suite (901/48,165), and the native-source
guard pass. The final source-frozen ordinary-client receipt passes 33/33: the
theme plus four-child closure remains `ref=1 stage_ref=0` across the real
CI-to-credits transition, then logs five shutdown-specific pre-release rows and
five final `1->0` releases. SP-31 is fixed for the production stage-owner
boundary. Remaining typed-family replacement, editor, and network-owner stress
is validation work.

---

## SP-30: Public examples can pass archive structure while using runtime-ignored fields

**Severity**: HIGH — creator edits appear valid but cannot affect production

**Pattern:** A typed archive can be structurally valid and catalog-visible yet
teach creators keys that the production parser never reads. B-971's
`.pdtheme` `accent`/`chrome` example was the concrete instance.

**2026-08-08 propagation:** B-974 through B-978 confirmed the same class in
weapon, character, voice, and especially effect archives. Presence in a
descriptor, catalog row, generic runtime binding, compiled-but-flattened graph,
or conformance allow-list is not utilization. Conformance itself can also
contradict runtime, as with timeline-only `.pdeffect` archives that validate but
cannot activate.

The same propagation audit found B-979 and B-980: `.pdprop` shipped an empty,
never-executed behavior graph, while `.pdtheme` preserved five nested archive
paths without registering or consuming any of them and accepted many inert
fields. A non-empty example and recursive typed-dependency proof are now part
of this pattern's closure checklist.

**2026-08-08 dependency propagation:** B-987 confirms that preserving a nested
archive path is not dependency ownership. Each supported typed child must be
release-validated, role/type/catalog resolved, source-qualified, registered,
connected by a catalog edge, and balanced through load/retain/release on every
local, base, package, and network path. Parent registration must fail before
exposing partial dependency state when any declared child is invalid; inert
dependency slots should be rejected instead of registered without a consumer.

**2026-08-08 creator propagation:** B-982 exposed the creator-side equivalent:
an in-game tool can advertise a typed family while writing a parallel legacy
loose format and transient catalog row. Creator audits must inspect the exact
saved artifact, restart discovery, hot registration, network distribution,
field-level runtime consumption, and last-good-file behavior. A menu label such
as "Voice" is not evidence that the tool writes or reloads `.pdvoice`.

**2026-08-08 unsupported-schema propagation:** B-988 found weapon material and
grip binding files in path qualification, manifests, upgrade tooling, and a dev
mod despite zero held/world material or hand-attachment consumers. When a
truthful production contract is not ready, version the field out and reject it
at both archive validation and runtime registration rather than preserving an
aspirational source slot.

**2026-08-08 active-theme propagation:** B-989 found that parsing and even
calling a setter is insufficient when omitted values leak from the previous
theme, declared roles fall through to procedural/native output, effects never
reach the compositor, fonts wait for restart, or glyph pills bypass the active
palette. Theme changes require one preflighted transaction that replaces every
theme-owned consumer and clears omitted state; unsupported dependency classes
must be rejected rather than retained as decoration.

**Audit:** Cross-check every generated/example source key against its production
parser and downstream consumer, not only descriptor/manifest conformance.

```powershell
python tools/build_typed_pdxxx_examples.py
python tools/asset_archive_conformance.py --root examples/modding/typed-pdxxx-basic --require-all-families
rg -n "parse_.*json|strcmp\\(key" port src
```

**Rule:** Example generation tests must pin parser-recognized keys for any
structured file. A family remains `partial` until every advertised field has a
production consumer or an explicit validation error.

---

## SP-29: User-facing control surfaces drift from the action map

**Severity**: HIGH — controls remain in code but become undiscoverable,
mislabelled, or impossible to rebind

**Root cause**: the action enum, default IMC bindings, Settings binding rows,
runtime consumers, and glyph hints are maintained as independent lists. Adding
an action to one list does not require its appearance in the others. Fixed
keyboard/controller strings also go stale after rebinding or device changes.

**Fixed instances (B-967/B-968, 2026-07-30)**:

- Settings now surfaces every one of the 117 user-bindable actions. The four
  derived continuous axis channels are explicitly represented by the global
  stick-layout, sensitivity, inversion, and deadzone controls.
- Forge E/Q bindings live in the Forge IMC, not the always-active Gameplay IMC.
- Direct vehicle use has a default mapping and production activation consumer.
- Player-facing menu hints resolve current action glyphs instead of fixed
  Xbox/default-key labels.

**Correct approach**:

- Every new `InputAction` must be classified as user-bindable or a derived
  channel in focused coverage.
- A user-bindable action requires an owning IMC row, persistence, default or
  explicit unbound state, and a production consumer.
- Hints use `pdguiGlyphGetActionLabel` or `pdguiDrawActionPrompt`; prose must
  not claim a fixed binding.
- Duplicate physical bindings must be checked under the action map's
  first-winner rule and the relevant active IMC stack.

**Propagation search**:

```text
rg -n "ACTION_[A-Z0-9_]+|addBind\\(|s_BindableActions|Enter/Space|B/Esc|D-Pad" port src tests
```

---

## SP-28: Generic menu fallback omits interactive item types

**Severity**: HIGH — a reachable menu can render chrome while silently losing
its actual controls and content

**Root cause**: the generic typed-dialog renderer handled only a subset of the
legacy item enum and displayed a placeholder/default OK for unhandled items.
Dedicated later registrations hid the defect on common routes, leaving any
unregistered or mod-adjacent dialog incomplete.

**Fixed instance (B-966, 2026-07-30)**: LIST, CAROUSEL, PLAYERSTATS, and RANKING
now have functional generic renderers over the live handler/data ABI. Dedicated
renderers remain last-registration-wins for richer screens.

**Correct approach**:

- Exhaustively switch every interactive `MENUITEMTYPE_*` supported by active
  dialog definitions.
- Unknown interactive types fail visibly in diagnostics but must not be
  represented as successful/complete UX.
- Preserve handler ABI widths; never reinterpret 32-bit fields as pointers on
  this 64-bit port.
- Keep a focused test that compares supported enum types with generic or
  dedicated production renderers.

**Propagation search**:

```text
rg -n "MENUITEMTYPE_|DEFERRED|placeholder|default OK|unk04u32" src/game port/fast3d tests
```

---

## SP-27: Canonical Catalog IDs Are Shadowed by Writable Legacy Fields

**Severity**: HIGH — creator-selected identity can change or disappear after a
save, network, or configuration round trip

**Root cause**: a format writes both the canonical catalog ID and a deprecated
numeric mirror, then a reader applies both in file order. The later legacy
field silently wins despite the nominal migration policy. Keeping both writable
also creates two hand-maintained representations that can drift.

**Fixed instance (B-964, 2026-07-30)**:
- MP setup JSON now writes only `weapon_ids`, preferring the canonical
  `g_MatchConfig` strings so catalog-only weapons survive.
- The deprecated numeric `weapons` array is accepted only as read-only
  migration input when `weapon_ids` is absent, regardless of field order.
- Unresolved canonical IDs, invalid legacy numbers, and failed bot-profile
  reconstruction reject the whole load instead of reporting partial success.

**Correct approach**:
- Writers emit only canonical catalog IDs.
- Readers accept legacy numeric fields for migration only when the canonical
  field is absent.
- A nonempty unresolved canonical ID or failed required record makes the whole
  load fail; it must never be skipped or replaced silently.

**Propagation search**:
```text
rg -n "\".*_ids?\"|legacy|fallback|catalog.*id|weapons\\[" port/src src/game
```
For each persisted or wire object, identify one authoritative identity field,
verify deprecated fields are read-only migration inputs, and prove field-order
independence.

---

## SP-26: Catalog-Extensible Selectors Collapse Back to Native Indices

**Severity**: CRITICAL — advertised mod content appears in a selector but cannot be selected, persisted, or reconstructed

**Root cause**: a selector correctly iterates catalog entries, then converts the chosen entry back to a legacy `runtime_index`, `mp_index`, table offset, enum, or type/difficulty tuple as its authoritative result. Base rows appear to work because their catalog rows mirror native tables. Creator-added rows have no native slot, normally carry `-1`, or collide with an existing tuple, so their catalog identity is lost immediately after selection.

**Fixed instance (B-961, 2026-07-30)**:
- The legacy selector now returns and stores the full profile catalog ID; the modern Room UI exposes the same profile domain.
- `mpbotconfig`, `matchslot`, JSON and binary setup formats, manifests, and both match-start wire directions retain that ID.
- Type, difficulty, and default body are derived only from the active readable public binding. v0-v2 binary setup blocks migrate to v3 by deriving base IDs from their legacy traits.
- Focused static/contract coverage and the all-target build are green. A custom-profile MKB/controller/save/network/gameplay receipt remains the validation gate.

**Correct approach**:
- Keep the full catalog ID as authoritative selector result and runtime state.
- Persist and transmit the catalog ID through save/wire/manifest boundaries.
- Resolve any legacy numeric value only at the final private gameplay boundary.
- A custom catalog row must have an end-to-end test that selects it, survives save/reload and host/client reconstruction, and changes production behavior.

**Propagation search**:
```text
rg -n "IterateUnlockedByType|runtime_index|mp_index|result_.*num|GETOPTIONTEXT|MENUOP_SET" src port
```
For each catalog-backed selector, compare the identity used for display with the identity stored by `MENUOP_SET`; they must be the same catalog ID domain.

---

## SP-1: MAX_PLAYERS Array Indexed by Bot mpindex

**Severity**: CRITICAL — ACCESS_VIOLATION crash
**Root cause**: Arrays sized `MAX_PLAYERS` (8) indexed with bot mpindex values (8–31). N64 had max ~12 entities; PC has up to 36 (MAX_MPCHRS).

**Key arrays affected**:
- `g_Menus[MAX_PLAYERS]`, `g_AmMenus[MAX_PLAYERS]` (bss.h)
- `g_MpSelectedPlayersForStats[MAX_PLAYERS]` (bss.h)
- `g_BgunAudioHandles[MAX_PLAYERS]`, `g_LaserSights[MAX_PLAYERS]` (bss.h)
- `g_PlayerExtCfg[MAX_PLAYERS]` (data.h)
- `g_FileLists[MAX_PLAYERS]` (data.h)

**Fix strategy**: Bounds-check and SKIP for bots — never alias via modulo.

**Files fixed (S15)**: ingame.c, mplayer.c, bondview.c, menutick.c
**Files fixed (P6-A / Tier 6)**: menu.c (`currentPlayerIsMenuOpenInSoloOrMp`, `func0f0f8120`), activemenu.c (`amOpen`, `amOpenPickTarget`, `amRender`)
**Files still needing audit**: NONE as of 2026-07-04 (backlog wave 3). `player.c:5094` was refactored to a per-player action map (`actionHeld(slayerplayeridx, ...)`), not a MAX_PLAYERS array index; the `g_Menus[` / `g_AmMenus[` index sites are bounds-checked (see the `menu.c:3820` SP-1/SP-2 comment). **Audit CLEARED.**

**Search command**: `grep -rn 'g_MpPlayerNum\|% MAX_PLAYERS\|AVOID_UB' src/`

---

## SP-2: Modulo-Hack Bounds "Fix" (AVOID_UB)

**Severity**: HIGH — silent data corruption
**Root cause**: `% MAX_PLAYERS` used as "bounds clamp" silently aliases bot data onto wrong player. Bot index 11 → index 3 (`11 % 8 = 3`), corrupting player 3's state.

**Correct approach**: Bounds-check and skip, not modulo-alias.

**Files known affected**: bondview.c (fixed S15), mplayer.c:704/3754. 2026-07-04 (wave 3): a repo-wide grep for `% MAX_PLAYERS` / `% MAX_LOCAL` found NO live modulo-alias bounds hacks (only benign `#ifdef AVOID_UB` decomp array-size blocks). **Audit CLEARED.**

**Search command**: `grep -rn 'AVOID_UB\|% MAX_PLAYERS\|% MAX_LOCAL' src/`

---

## SP-3: g_PlayerExtCfg Beyond MAX_LOCAL_PLAYERS

**Severity**: MEDIUM — reads garbage for remote/bot players
**Root cause**: `g_PlayerExtCfg[MAX_PLAYERS]` meaningful only for local players (indices 0–3). Code indexes with values 4–7.

**Correct approach**: Use `MAX_LOCAL_PLAYERS` (4) as bound, or `PLAYER_EXTCFG()` macro (masks with `& 3`).

**Files known**: mplayer.c:704/3754, bondwalk.c:908. 2026-07-04 (wave 3): all live `g_PlayerExtCfg` indexers verified -- `player.c` fov getters (7714/7730) and `mplayer.c` extcontrols (766/4153) already bound by `MAX_LOCAL_PLAYERS`; the `bondwalk.c` jump-height read was hardened from `MAX_PLAYERS` to `MAX_LOCAL_PLAYERS`. **Audit CLEARED.**

---

## SP-8: prop->chr Accessed Without NULL Check

**Severity**: HIGH–CRITICAL — null pointer dereference crash
**Root cause**: Code checks `prop->type == PROPTYPE_CHR || PROPTYPE_PLAYER` before accessing `prop->chr`, but does NOT check that `chr` itself is non-NULL. For PROPTYPE_CHR, chr is almost always set at creation — but for PROPTYPE_PLAYER, chr can be NULL during stage load, match cleanup, or dedicated-server transitional states.

**When it happens**: Stage transitions (player prop exists before chr is bound), Co-op/Multiplayer late-join, dedicated server with no local player occupying slot 0.

**Pattern to audit**:
```c
if (prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) {
    prop->chr->anything   // DANGER: chr may be NULL for PROPTYPE_PLAYER
```

**Correct pattern**:
```c
if ((prop->type == PROPTYPE_CHR || prop->type == PROPTYPE_PLAYER) && prop->chr) {
    prop->chr->anything
```
or locally:
```c
struct chrdata *chr = prop->chr;
if (chr) { ... }
```

**Fixed (S65 — Audit 2 of 4)**: 7 critical instances in propobj.c, explosions.c, smoke.c. See `context/null-guard-audit-props.md`.
- `propobj.c:4455` — parent->chr->hidden in weapon drop (CRITICAL)
- `propobj.c:8392` — playerprop->chr->hidden in cctvTick (CRITICAL)
- `propobj.c:9334-9350` — hitchr in laser fence damage block (CRITICAL)
- `propobj.c:9462` — targetprop->chr in enemy autogun (HIGH)
- `explosions.c:1004` — chrDamageByExplosion in blast radius (HIGH)
- `explosions.c:379` — exproom OOB when rooms[0]=-1 (HIGH)
- `smoke.c:210` — rooms[0] OOB in roomGetFinalBrightnessForPlayer (HIGH)

**Remaining audit**: mplayer/*.c (Audit 4) -- spot-checked clean 2026-07-04. **2026-07-04 (wave 3): bot.c + botinv.c CLEARED** -- 7 unguarded `chrGetTargetProp(chr)->chr` / `target->chr` dereferences fixed (botinv.c chrsinsight x2 @540/557, chrdistances @890, crossbow blur @589, tranq blur @603; bot.c `botGetTargetsWeaponNum` @1276). `chrGetTargetProp` returns a valid non-NULL prop when `target != -1`, so only `->chr` (a player prop whose chr is unbound during load/late-join/cleanup) needed guarding; `botGetWeaponNum(NULL)` genuinely crashed at `chr->aibot`. Guards are behaviour-neutral when chr is non-NULL (the common case).

**Search command**: `grep -n "->chr->\|->chr\." src/game/*.c | grep -v "if.*chr\|chr =\|chr=\|NULL"`

---

## SP-6: PLAYERCOUNT() Iteration with Sparse Player Slots

**Severity**: HIGH — null pointer dereference crash
**Root cause**: `PLAYERCOUNT()` counts non-null entries in `g_Vars.players[]` but loops iterate by sequential index. If slot 0 is NULL and slot 1 is non-null, PLAYERCOUNT()=1 and the loop runs for i=0, accessing `g_Vars.players[0]->anything` → crash.

**When it happens**: During stage load (`lvReset`), player objects aren't spawned yet. After a match that ends without clean teardown, some slots may be non-null while others are null from cleanup.

**Pattern to audit**:
```c
for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
    g_Vars.players[i]->anything  // DANGER: players[i] may be NULL
```

**Correct pattern**: Always null-check `g_Vars.players[i]` in any such loop:
```c
for (i = 0; i < LOCALPLAYERCOUNT(); i++) {
    if (g_Vars.players[i] && g_Vars.players[i]->prop && ...) {
```

**Fixed (S63)**: `music.c:musicIsAnyPlayerInAmbientRoom` (B-36)
**Fixed (S64 — Audit 1 of 4)**:
- `lv.c:227` — `lvTick()` slayer rocket visionmode check (HIGH)
- `lv.c:482` — `lvReset()` player init loop during stage load (CRITICAL)
- `setup.c:1572` — `setupCreateProps()` invInit loop during stage load (CRITICAL)
- `camera.c:250,260,286,296` — 4 matrix lookup loops in cam0f0b53a8/cam0f0b53a4 (HIGH)
- `playermgr.c:700` — `playermgrGetPlayerNumByProp()` prop scan (HIGH)

**Remaining audit**: bondwalk.c/bondmove.c currentplayer early-return guards (Audit 2),
g_ChrSlots[] and g_MpAllChrPtrs[] (Audit 3). **2026-07-04 (wave 3): mplayer/*.c CLEARED**
-- participant loops over `g_Vars.players[i]` / `g_MpAllChrPtrs[i]` are all null-guarded
(mpspawn_orchestrate.c:106/396, scenarios.c) or bounded by `g_MpNumChrs` (live-slot
contract, entries non-NULL); no unguarded sparse-loop deref found.
See `context/null-guard-audit-players.md` for full findings.

**Search command**: `grep -rn "players\[i\]->\|players\[j\]->" src/game/`

---

## SP-4: Hardcoded Stage Index Domains

**Severity**: HIGH — OOB crashes with mod stages
**Root cause**: Three index domains entangled: stage table (87 entries), solo stages (21 entries), best times (21 entries). Mod stages get valid stage table indices (61–86) but are OOB for solo stages and best times.

**Status**: Phase 1 safety net complete (S23) — bounds checks at all known access points. Phase 2 (dynamic stage table) and Phase 3 (index domain separation with `soloStageGetIndex()`) designed, not coded.

**Guard added**: `if (stageindex >= NUM_SOLOSTAGES) return` in cheats.c, endscreen.c, training.c, mainmenu.c

**Constraint note**: See [constraints.md](constraints.md) — Index Domain Warning section.

---

## SP-5: Large Stack-Allocated Buffers

**Severity**: MEDIUM — stack overflow risk on PC threads
**Root cause**: N64 had single known stack. PC threads default to 1MB. Large buffers in deep call chains can overflow.

**Known dangerous buffers**: See [memory-modernization.md](memory-modernization.md) Phase M2.

---

## SP-7: Magic Number Allocation Sizes

**Severity**: LOW→MEDIUM — readability + silent breakage when constants change
**Root cause**: Bare hex/decimal literals for buffer sizes. When limits change (MAX_BOTS 8→24), hardcoded sizes don't update.

**Status**: Phase M1 of memory modernization — `memsizes.h` created with 30+ named constants. 8 high-priority files converted. ~100 ALIGN16 replacements remaining.

**Search command**: `grep -rn 'mempAlloc(0x\|mempAlloc([0-9]' src/`

---

## SP-9: File Truncation by Build/Edit Pipeline

**Severity**: HIGH — silent data loss; can corrupt context files, scripts, and source files
**Root cause**: The build and edit pipeline (PowerShell scripts, AI edit tools) silently truncates files under certain conditions. Content written beyond a threshold (encoding issue, buffer limit, or streaming flush failure) is discarded without error. The file is saved with fewer lines but the write is reported as successful.

**Known incidents**:
- `devtools/_dev-window.ps1`: 2311 → 2232 lines lost (encoding: em-dashes caused Windows-1252 truncation). Restored from commit `68c0b186`. **Mode A.**
- `port/src/actionmap.cpp`: 1624 → 1590 lines lost (AI output token limit mid-generation). Committed in `2ec0849e`, masked by auto-commit. Repaired by subsequent session. **Mode B.**
- ~19 files in an earlier session (pre-S140, large context): unrecovered forensically, likely Mode B.

**Two distinct failure modes** (see Deep Investigation section below):
- **Mode A** (encoding): PS script written through Windows-1252 code page; first non-ASCII byte silently terminates write. Mitigated by no-em-dash rule in `.ps1` files.
- **Mode B** (AI output limit): AI Edit/Write tool call truncated mid-character when session context is saturated; tool writes truncated content without error. **ONGOING RISK** -- safeguard catches this at commit time but not within-session.

**Safeguard** (**IMPLEMENTED S190**): Pre-commit `git diff HEAD --numstat` check in `devtools/build-headless.ps1`. Fires when net delta < -20 lines AND additions < 1/3 of deletions. Aborts auto-commit, names suspect files, prints restore command. Build continues from working copy. Tested: fires on -95 net (0+/95-); silent on -49 net with 41 additions (intentional rewrite -- correct no-fire).

**Audit checklist** (run after any AI-assisted edit session):
1. `git diff --stat` -- inspect line-count deltas before committing. Unexplained drops are truncation candidates.
2. `tail -5 <file>` after every significant edit. Truncated files end mid-word with no trailing newline.
3. In PowerShell scripts: use hyphens only (no em-dashes, no curly quotes). Save as UTF-8 with BOM if script contains non-ASCII.
4. Start fresh sessions before context grows large (SP-9 Mode B risk rises with session length).

**Fix strategy**: Restore from git (`git checkout <commit> -- <file>`), then re-apply the intended edit cleanly. Do NOT re-edit a truncated file -- the lost content may not be reconstructable from diff alone.

**Search command**: `git diff HEAD --numstat | awk '$2 > $1*3 && $2-$1 > 20 {print "SUSPECT:", $3, "(net", $1-$2, ")"}'`

---

## SP-11: Movement Collision Reading Render-Owned Buffers

**Severity**: CRITICAL — ACCESS_VIOLATION crash, nondeterministic movement collision, mod/Grid incompatibility
**Root cause**: Gameplay movement/capsule code reads transient render-owned buffers (`model->matrices`, display-list hit helpers, frame-local graphics state) as if they were authoritative collision data. Render matrices are only valid after render/update setup for the current frame and can be missing, stale, or indexed differently than collision needs. The B-339 Defection Perfect crash was the visible failure: Stage 2 movement collision called `propobj.c::func0f0849dc()`, which dereferenced `model->matrices[mtxindex]` during early NPC ground acquisition.

**Correct approach**:
- Terrain/rendered room geometry belongs in `meshcollision`'s static world mesh at stage load.
- Movement-solid props own local-space `prop->colmesh` data.
- Dynamic prop queries build transforms from stable object state (`prop->pos`, `defaultobj.realrot`, and explicit door/lift state helpers as needed), not render frame matrices.
- Pickups, zones, water/fog/holograms, Forge pass-through, and Forge projectile-only objects must not contribute movement collision.
- Weapon/projectile/object-hit paths may keep using legacy model hit helpers, with guards, because they are not movement ownership.

**Guardrail landed 2026-05-18 (B-339)**:
- `src/lib/capsule.c` static guard requires no `func0f0849dc`, no `capsuleRenderedPropRayCast`, and no direct `model->matrices` use.
- `meshcollision.c` owns `meshWorldAddRenderedRoom`, `meshRayCastWorld`, `meshRayCastDynamicProps`, `meshAttachModelToProp`, and `meshBuildPropTransform`.

**Search command**: `rg -n "func0f0849dc|model->matrices|gfxAllocate|g_Gfx|modelFindNodeMtx" src/lib src/game port/src port/fast3d`

### SP-9 Deep Investigation — 2026-04-10

**Conducted**: dreamy-goldberg worktree, investigation-only pass (no source changes).

#### Commit Catalog — Confirmed Truncation Incidents

| Commit | Timestamp | File | Before | After | Delta | Notes |
|--------|-----------|------|--------|-------|-------|-------|
| `2ec0849e` | 2026-04-10 00:09 | `port/src/actionmap.cpp` | 1624 | 1590 | -34 | **Committed truncated** — auto-commit masked the damage |
| Working copy only | 2026-04-10 ~00:xx | `devtools/_dev-window.ps1` | 2311 | 2232 | -79 | **Caught before commit** — restored from `68c0b186` |
| Pre-2026-04-05 | Unknown | ~19 files | Unknown | Unknown | Unknown | **Not forensically confirmed** — referenced in SP-9 summary and Apr-5 briefing; no detailed record survives |

actionmap.cpp was subsequently repaired by concurrent worktree sessions. At HEAD (`355326c1`) it is 1606 lines with a proper ending. Whether the difference from the original 1624 lines represents intentional rewriting or silent loss is unresolved.

#### Byte-Level Characterization

**actionmap.cpp (2ec0849e):**
- Truncated: 63,331 bytes. Original: 64,652 bytes. Lost: 1,321 bytes.
- `0xF763` — not a clean buffer boundary. Nearest: 32KB (30,563 away), 64KB (2,205 away).
- File ends mid-word: `    /* Populate default bindings (into IMC struct` (should be `structs, not yet active) */`).
- `\ No newline at end of file` confirmed — abrupt character-level termination, not line-level.
- Em-dash count at truncation point: zero. The em-dash line (`sysLogPrintf(LOG_NOTE, "ACTIONMAP: initialized — ...")`) appears 34 lines later in the lost tail. **Encoding is not the proximate cause for this incident.**
- File has CRLF line endings (0x0d0a confirmed at HEAD).

**_dev-window.ps1 (working copy only):**
- 79 lines lost. No UTF-8 BOM. File contained 4 UTF-8 em-dashes (U+2014, bytes 0xE2 0x80 0x94).
- Root cause confirmed in session log S190: "em dashes replaced with hyphens (Windows-1252 encoding issue)."
- When a PowerShell pipeline writes through Windows-1252, 0xE2 (first byte of UTF-8 em-dash) has no valid mapping, and the write terminates at that byte position. All content from that character onward is silently dropped.

#### Two Distinct Failure Modes

**Mode A — Encoding truncation (PowerShell/Windows-1252 pipeline)**
- Trigger: A file containing non-ASCII UTF-8 bytes (em-dashes, curly quotes) is written through a Windows PowerShell step that defaults to Windows-1252. The first non-representable byte silently terminates the write.
- Pattern: Cut at the first em-dash (or similar non-ASCII char). Position is deterministic for a given file.
- Files at risk: `.ps1` scripts; any file written by PowerShell without explicit `-Encoding UTF8`.
- Status: **Addressed for _dev-window.ps1** — em-dashes replaced with hyphens. Rule: no em-dashes or non-ASCII characters in `.ps1` files.

**Mode B — Response truncation (AI output token limit)**
- Trigger: A Claude Code session with a large accumulated context (long transcript, many prior tool calls) generates a Write or Edit tool call with a large `content` or `new_string` parameter. The model hits its output token limit mid-generation. The parameter value is truncated at that character. The tool writes the truncated content to disk without error.
- Pattern: File tail cut off mid-word, mid-statement, no newline at EOF. Truncation point is not aligned to any buffer boundary — it is wherever the model's output window closed.
- Confirmed evidence: The archive documents a `~19MB transcript size` causing 400 errors in the same approximate period. Both `_archive/session-briefing.md` and `_archive/briefing-2026-03-31.md` record "NEVER use Write tool — Write tool truncation has destroyed files before" as a standing rule.
- Even the Edit tool is vulnerable if `new_string` is large and the session context is saturated. The "Edit not Write" rule reduces risk but does not eliminate it.
- Status: **Not fully mitigated.** Rule exists but is not enforced mechanically.

#### Auto-Commit Masking Vector

The build pipeline auto-commit (`git add -A && git commit`) runs BEFORE each build with no pre-commit checks. This creates a window where:
1. A session leaves a file in a truncated state (tool call result was truncated).
2. The auto-commit runs and permanently records the truncated state in git history.
3. The next session sees the auto-committed version as HEAD and treats it as authoritative.
4. The original truncation is invisible via `git diff` — the damaged state IS the committed baseline.

This is exactly what happened with actionmap.cpp: truncation occurred between 68c0b186 (23:46) and 2ec0849e (00:09), and the auto-commit at 00:09 captured and committed the truncated file.

#### Pre-Commit Hooks

No active hooks in `.git/hooks/`. All files are `.sample` (inactive). Zero mechanical protection exists today.

#### Open Questions (require live repro or additional data)

1. **Is actionmap.cpp at HEAD (1606 lines) complete?** The HEAD version differs from the original 1624-line version. A diff of the tail of 68c0b186:port/src/actionmap.cpp vs HEAD would clarify whether the 18-line delta is intentional rewriting or residual data loss.
2. **What exactly were the 19 files?** No detailed forensic record survives. A git log sweep targeting pre-S140 commits (before 2026-04-04) looking for multi-file shrinkage patterns could identify the incident. Requires running the full shortstat sweep against the earlier date range.
3. **Can Mode B be confirmed via tool call logs?** If Claude Code writes tool call parameters to a log, the truncated `new_string` from the session that produced 2ec0849e would be visible there. Anthropic engineering would need to confirm whether such logs exist.

#### Recommended Safeguard (implementation sketch — not applied)

**Option 1 (recommended): pre-auto-commit line-count check in `build-headless.ps1`**

Insert immediately BEFORE the `git add -A && git commit` step:

```powershell
# SP-9 truncation guard
$numstatLines = (& git -C $ProjectDir diff HEAD --numstat 2>$null) -split "`n"
$flagged = @()
foreach ($line in $numstatLines) {
    if ($line -match '^(\d+)\s+(\d+)\s+(.+)$') {
        $added   = [int]$Matches[1]
        $deleted = [int]$Matches[2]
        $file    = $Matches[3].Trim()
        $net     = $added - $deleted
        # Flag: net shrinkage > 20 AND additions < 1/3 of deletions
        # (excludes legitimate large rewrites where both sides are large)
        if ($net -lt -20 -and $added -lt [Math]::Max(1, [Math]::Floor($deleted / 3))) {
            $flagged += "  $file  (net $net: +$added / -$deleted)"
        }
    }
}
if ($flagged.Count -gt 0) {
    Write-Host "[SP-9 GUARD] Auto-commit aborted — unexpected file shrinkage:" -ForegroundColor Red
    $flagged | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    Write-Host "Verify against HEAD. Restore: git checkout HEAD -- <file>" -ForegroundColor Cyan
    exit 1
}
```

Threshold `-20` catches all known incidents (34 and 79 lines lost) while ignoring normal edits. The `added < deleted/3` filter avoids false positives on large intentional rewrites. Adjust to `-10` for higher sensitivity.

**Option 2: post-merge verification step (add to CRITICAL-PROCEDURES.md)**

After any worktree merge touching source files, before committing:
```bash
git diff HEAD --numstat | awk '$2 > $1*3 && $2-$1 > 20 {print "SUSPECT SHRINKAGE:", $3, "(net", $1-$2, ")"}'
```
Visually inspect any flagged files before proceeding.

**What these safeguards do NOT catch**: A truncation that occurs within a single session before any intermediate commit, leaving no HEAD baseline to compare against. Defense for that case is procedural: the Edit-not-Write rule, session-length discipline (start fresh sessions before context gets large), and the existing `tail -5 <file>` sanity check after every significant edit.

---

## SP-12: Silent Process Death from Stack Canary SIGABRT

**Severity**: CRITICAL — crash with zero diagnostic output
**Root cause**: GCC's `-fstack-protector-strong` detects stack buffer overflows via canary values. When a canary is smashed, `__stack_chk_fail()` calls `abort()` which raises SIGABRT. On Windows/MinGW, the SIGABRT handler runs on the same (potentially corrupt) stack. If the handler's stack frame pushes the stack beyond its committed limit, the handler itself faults — and since VEH doesn't cover signals, the process dies with no output.

**Pattern**: Process terminates silently (no log, no crash dialog, no VEH output) after sustained heavy computation (many bots, deep AI chains, complex collision). Heartbeat timer stops firing. No core dump.

**Instances**: B-126 (silent crash ~8min MP), B-113 (was 2MB stack, expanded to 8MB but class not eliminated).

**Fix (S234 FIX-A)**:
1. SIGABRT handler rewritten with static buffers only, direct file writes, `_exit(3)` — no `sysLogPrintf` or `sysFatalError` which use too much stack
2. `SetUnhandledExceptionFilter` (UEF) + `AddVectoredExceptionHandler` (VEH) already installed as fallbacks
3. Stack watermark tracking per-chr in `chraTick` (`g_ChrTickMaxStackUsed`) — identifies which AI codepath consumes the most stack
4. Stack depth cap in `chraTick` — skips chr when remaining stack < 512KB

**Search command**: `grep -rn 'stack_chk_fail\|SIGABRT\|signal.*SIGABRT\|g_ChrTickMaxStackUsed\|g_ChrTickStackBase' port/src/crash.c src/game/chr.c src/game/chraction.c`

---

## SP-13: `manifestClear` Before `mainChangeToStage` During MP Teardown

**Severity**: CRITICAL — `0xc0000005` access violation
**Root cause**: `mainChangeToStage()` treats any `STAGE_IS_GAMEPLAY()` target as a
match-load, and if `g_ClientManifest.num_entries > 0` it takes the
`manifestMPTransition()` branch and attempts to diff the old manifest against
whatever is loading for the target stage. When the old manifest is a torn-down
MP match manifest and the target is lobby/room/CI-training, the diff walks dead
asset references and faults.

**When it happens**: Any code path that changes stage out of an MP match
without clearing the manifest first. Specifically, paths that end an MP match
and return to the hub — pause-menu "End Game", endscreen "Exit", and the
disconnect-and-clean-up flow.

**Correct pattern**:
```c
manifestClear(&g_ClientManifest);
mainChangeToStage(STAGE_CITRAINING);   // or any non-match target
```

**Known sites (ALL FIXED 2026-04-13)**:

| Site | Fix | Commit |
|------|-----|--------|
| `pdgui_bridge.c:799` — `pdguiEndscreenExitToMainMenu()` | F-0.4 | S233 |
| `netmsg.c:1419` — `netmsgSvcStageEndRead()` (SVC_STAGE_END path) | L1-1 | S235 |
| `net.c::netDisconnect` (before `mainChangeToStage(STAGE_CITRAINING)`) | Bug A | `d37e9677` |

**Audit checklist (before adding any new callsite)**:

1. Does this code call `mainChangeToStage()`?
2. Is `g_ClientManifest` potentially non-empty when this runs (i.e. was an MP
   match active in this flow)?
3. If both yes: insert `manifestClear(&g_ClientManifest);` immediately before
   the `mainChangeToStage()` call.
4. Reference F-0.4 / L1-1 / Bug A in the comment for cross-pattern
   traceability.

**Search command to audit new callsites**:
```
grep -n 'mainChangeToStage' port/src/net/*.c port/fast3d/*.cpp src/game/*.c
```
Then verify each non-menu-system hit is either already preceded by
`manifestClear` OR demonstrably cannot run with a populated `g_ClientManifest`.

**Active constraint**: [constraints.md](constraints.md) — "manifestClear before
mainChangeToStage during MP teardown".

---

## SP-14: Room-bound server state must be cleaned up on room teardown

**Severity**: HIGH — silent protocol desync, stuck UI, ghost stage loads
**Root cause**: Server-side state keyed on `room_id` (`s_ReadyGate`,
`g_NetMatchRoomId`, mod playlist state, match config snapshot, …) must
be torn down when the room it belongs to is destroyed. `roomDestroy()`
only clears the room slot itself — it does not notify downstream
subsystems. Any subsystem holding room-keyed state must hook into
`roomLeave()` / `roomDestroy()` (or run a "room still exists?" check on
each tick).

**When it happens**: host leaves room, last player leaves, host kicks
all, server drops client. Anything that empties a room.

**Canonical fix pattern** (Bug B, 2026-04-14):
1. Add a public "on client left" and "on room destroyed" wrapper that
   reads the subsystem's state and invokes its abort / clear path.
2. Call both wrappers from `roomLeave()` in `port/src/room.c` — once
   the leaver has been removed, and again (defensively) inside the
   `client_count == 0` branch before `roomDestroy()`.
3. Add a per-tick defensive check inside the subsystem's tick function
   that calls `roomGetById(room_id) == NULL` → abort.

**Known sites audited**:
- `s_ReadyGate` (Bug B, 2026-04-14) — fixed via `netReadyGateAbortForRoom`
  + `netReadyGateOnClientLeft` in `netmsg.c`.

**Audit command** (run when adding new room-keyed state):
```
grep -nE 'room_id|matchRoomId|g_NetMatchRoomId' port/src/net/*.c port/src/*.c
```
Then verify every static/global holding `room_id` has a hook in
`roomLeave()` or a defensive per-tick cleanup.

**Active constraint**: [constraints.md](constraints.md) — "Ready gate lifetime
is bound to its room's lifetime".

---

## SP-15: GL texture size and cache lifetime — upload-time caps, cache-scoped teardown

**Severity**: MED — GPU memory leak, possible driver-level allocation failure on large skin/theme assets
**Root cause**: Any code path that uploads a user-controlled image to a
`GLuint` texture must both (a) bound the upload dimensions against
`GL_MAX_TEXTURE_SIZE` (or enforce a compile-time cap below the
lowest-common-denominator driver limit), and (b) free the texture via
`glDeleteTextures` on the exact lifetime boundary of the cache that
owns it. A rescan that rebuilds the cache without deleting the old
GL names leaks textures on every reload.

**When it happens**:
- Theme / chrome style rescans (`pdguiThemeRescanChromeStyles`,
  `pdguiThemeRescanMods`) invoked on mod apply, dev hot-reload, and
  startup.
- Skin editor preview uploads (`s_DownrezPreview`) on character or
  quantization-level change.
- Any mod-supplied PNG loaded via `pdguiLoadTextureFromPNG` or similar.

**Canonical fix pattern** (S-6 / S-5, 2026-04-16):
1. Before uploading, clamp `w`/`h` to
   `min(GL_MAX_TEXTURE_SIZE_RUNTIME_CAP, compile_time_cap)`. If the
   asset exceeds the cap, downscale or reject with a log warning — do
   not pass the raw dimensions to `glTexImage2D`.
2. Before clearing a cache (e.g. `s_chromeStylesClear`), iterate the
   cache and `glDeleteTextures(1, &tex)` for each mod-owned entry.
   **Skip base entries owned by a different init path** (e.g.
   `"base:ui_chrome_frame"` owned by `pdguiThemeLateInit`). Erase from
   the cache map after deletion.
3. For reusable scratch buffers (downrez preview, quantization
   staging), track owner dimensions (`W`, `H`) alongside the pointer
   and `realloc` whenever the target dimensions change — do not reuse
   a buffer sized for a previous character.

**Known sites audited**:
- `pdguiThemeRescanChromeStyles()` (S-6, S294) — fixed via
  `s_chromeStylesFreeModTextures()` in `pdgui_theme.cpp`.
- `s_DownrezPreview` in `pdgui_skin_editor.cpp` (S-5, S294) — fixed
  via tracked `s_DownrezPreviewW`/`H` and realloc-on-resize.
- `modmgrApplyChanges()` (S-8, S294) — now also calls
  `pdguiThemeRescanChromeStyles()` so the cache is torn down on mod
  apply (previously only `pdguiThemeRescanMods()` was called).

**Audit command** (run when adding new GL texture uploads or caches):
```
grep -nE 'glTexImage2D|glGenTextures|s_ThemeTexCache|GL_MAX_TEXTURE_SIZE' port/fast3d/*.cpp port/src/*.c
```
Then verify every texture creation site has a matching
`glDeleteTextures` on its cache's teardown path, and every upload has
a dimension cap check.

---

## SP-16: Schema / emitter / parser field-name drift in per-asset envelopes

**Severity**: HIGH — silent zero-init on every record, downstream "loads but doesn't work" symptoms
**Root cause**: The per-asset envelope pipeline has three independently-edited surfaces -- the schema doc
([context/designs/catalog/universality-pivot-schemas.md](designs/catalog/universality-pivot-schemas.md)),
the per-kind emitter (`port/src/romextract_pd<kind>.c`), and the per-kind loader parser
(`port/src/loader_pool.c::parse<Kind>`). When a field is renamed in the schema (e.g. `filenum` -> `mesh`,
`handfilenum` -> `hand`) and one surface is updated but not the others, the runtime symptoms are silent:

- Emitter writes the new name -> `.pd<kind>` files have the new key.
- Parser still reads the old name -> the field's destination in `<kind>_data_t` stays `0` from memset.
- Catalog manager accessor returns the record with the zero field -> downstream code reads 0 and either
  silently falls back to a placeholder path or stalls indefinitely (e.g. master loader waiting for a
  filenum=0 file load that can never complete).

No error is logged unless an explicit "required field missing" guard exists (which `parseHead`/`parseBody`
do not have; missing keys are skip-and-continue).

**When it happens**:
- Schema lock-down rename without an accompanying parser update (B-328, 2026-05-15: `mesh`/`hand` renamed
  in the schema doc + emitter on 2026-05-03 BYOR completion; parser kept reading `filenum`/`handfilenum`
  for 12 days before the missing-field default-zero surfaced as the "weapon won't render" symptom).
- Adding a new envelope field without wiring it through both emitter and parser.
- Renaming an internal struct field and only updating one of the two parse-time codecs.

**Symptom signature** (use this to recognise the class):
- An asset record loads (the record's outer envelope parses; e.g. `LOADER.UNIVERSAL.OK: kind=body
  scanned=68 registered=68` is fine; `LOADER.POOL.BODY.OK: active=1 bodies=68` is fine).
- BUT a specific scalar field in the record's struct is 0 / NULL when read at runtime.
- AND there are no `RESOLVE_FAIL` warnings for that field on the load path.
- Downstream consumer (typically a state-machine waiting for a non-zero filenum or pointer) stalls
  forever without an error log.

**Canonical fix pattern**:
1. Make the parser accept BOTH names: `if (jstream_str_eq(&key, "mesh") || jstream_str_eq(&key, "filenum"))`.
2. Leave the emitter writing the canonical (schema) name.
3. Update the smoke test for that asset kind to assert the `LOADER.POOL.<KIND>.OK: active=1 <kinds>=<N>`
   and `LOADER.UNIVERSAL.OK: kind=<kind> scanned=<N> registered=<N>` count lines -- this catches the
   parser regression (returns 0 records when the only-key gates fail) immediately.

**Audit command** (run when renaming an envelope field or adding a new one):
```
grep -nE 'fprintf.*"(<field>|<old_field>)":' port/src/romextract_pd*.c
grep -nE 'jstream_str_eq\(&key, "(<field>|<old_field>)"' port/src/loader_pool.c
```
Then cross-check against [universality-pivot-schemas.md](designs/catalog/universality-pivot-schemas.md)
Section 2.x for the canonical key name.

**Known instances**:
- B-328 (2026-05-15): `mesh` / `hand` in `.pdhead` and `.pdbody` -- parser reading `filenum` /
  `handfilenum`. Fixed at `port/src/loader_pool.c::parseHead, parseBody`.

---

## SP-17: Per-FACE matrix capture collapses N64 weighted-vertex (multi-matrix) skinning

**Severity**: HIGH — silent geometry corruption on every animated generated chr body (NOT a crash; renders wrong)
**Root cause**: N64 chr bodies skin limbs by **weighted vertices** — the DL interleaves matrix loads and vertex loads so a single triangle's three verts are transformed by *different* bone matrices (e.g. helper matrix 17 + bone matrix 10 across a knee seam). The `.pdmesh` data model records **one matrix per FACE** (`romextract_pdmesh.c::s_objEmitTri`, `model.faces.json` `{face_index, matrix_index}`) and writes all 3 verts RAW under that single (active/last-loaded) matrix. The consume (`modasset_compiler.c` ~6463-6494) emits `gSPVertex(3)` per tri under one matrix. So every weighted *seam* triangle mis-binds the verts that belong to the other bone.

**Why it usually hides**: when the joint is near-straight (bind pose, idle/standing anims, small rotations) a bone and its half-angle helper matrix are nearly equal, so a mis-bound vert barely moves. It only becomes visible when a joint bends hard (large rotation), which makes the helper diverge from the bone and flings the mis-bound verts off-body. Pixel symptom: a faceted limb jumble under poses with sharp joint angles (B-942: dark_combat legs under cutscene anim 1157 bend ~90deg).

**Scale (measured on cdark_combat / Joanna, filenum 0x42, 601 tris)**: **225/601 (37%) are seam triangles** (verts span >1 matrix); 137 (23%) have v0 loaded under a different matrix than the tri's active one; 106 are leg seams spanning every leg joint (pelvis-hip-kneeHelper-thigh-foot, both sides). Affects all generated chr bodies port-wide.

**Instrumentation trap (why this was an impasse for ~4 sessions)**: the seam detectors `B942STITCH`/`B942DESYNC` were added ONLY to the `G_TRI1` handler. cdark_combat emits its triangles via **`G_TRI4`** (the 4-tri packed command, `romextract_pdmesh.c:1536`), which had NO seam check — so the probes reported 0 and the extraction looked "byte-faithful." **Audit rule: any per-vertex/per-tri invariant probe in the DL walk MUST cover both G_TRI1 and G_TRI4 (and G_TRI2 if present).**

**Fix strategy**: carry a **per-vertex matrix index** through the pipeline. Extractor: the per-slot `slots_mtx[]` already tracked in `s_exportGdlToObj` is the source — emit it as a parallel per-vertex array (e.g. a 5th `model.obj` v-token or `model.vtxmtx.json`). Consume: group each DL's verts by their per-vertex matrix and reproduce the N64 interleave — load matrix, `gSPVertex` that batch, then tris referencing the multi-batch vertex cache (`render.json` already preserves the matrix-load ORDER; what's missing is which verts bind to which load). Bump EXPORT_VERSION + FAST_CACHE_KIND.

**Fix LANDED (2026-06-25, B-942, structure-verified — human does final CI-menu render verify before commit)**: chose to extend `model.faces.json` (keyed by face_index, exactly how the consumer already iterates) rather than a new file. Each face record now carries an optional `"vtx_matrix":[m0,m1,m2]` triplet alongside the back-compat per-face `matrix_index`.
- Extractor (`romextract_pdmesh.c`): `s_objEmitTri` gained 3 per-corner matrix args (from `slots_mtx[]` at the G_TRI1/G_TRI4 call sites); writes the triplet to faces.json; added a `seam_face_count` stat (logged as `seamtris=` per model). Added the missing **G_TRI4** STITCH/DESYNC probes (were G_TRI1-only). Bumped `OBJ_EXPORT_VERSION_LABEL` + `FAST_CACHE_KIND` with `_vtxmtx` to force re-extraction.
- Consume (`modasset_compiler.c`): `obj_triangle_t` gained `s32 vtx_matrix[3]` (default -1); `parseFacesJson` reads the optional triplet; new `generatedTriComputeBatch()` groups a tri's 3 corners into contiguous equal-matrix runs (slot permutation + per-run matrix), `fillGeneratedTriVertices()` lays the verts in slot order, and both emit loops (render-stream + flat) now emit `gSPMatrix(LOAD)+gSPVertex` per run then one `gSPTri` over remapped cache slots. **Single-matrix / legacy `-1` tris collapse to run_count==1 = byte-identical pre-B942 emit** (props/hands/heads unchanged — verified: `head_dark_combat` matrices=1 emitted ZERO multibatch tris). gdl source buffer grown `tri_count*3 -> tri_count*7` (worst case 3 mtx + 3 vtx + 1 tri per seam tri). NOTE: the flat `buildGeneratedModeldefFromMesh` path (single matrix 0, no hierarchy) was intentionally left untouched — chr bodies route through the hierarchy/render-stream path.
- Verification: re-extract logged `seamtris=225` on cdark_combat (was hidden as 0); faces.json carries 601 vtx_matrix triplets, 225 seam (corner matrices differ), 0 malformed; G_TRI4 STITCH/DESYNC probes now fire. Consume: dark_combat + model_cdark_combat leg/limb DLs emit multi-batch loads (maxruns 2-3, never >3); 61 modeldefs compiled with no overflow/crash, clean shutdown. **Still pending: human verifies the actual CI-menu Joanna leg render (the money shot) under bent anim 1157 before commit.**

**Search command**: `rg -n "s_objEmitTri|slots_mtx|matrix_index|vtx_matrix" port/src/romextract_pdmesh.c`; consume side `rg -n "generatedTriComputeBatch|vtx_matrix|run_count" port/src/modasset_compiler.c`.

**Known instances**:
- B-942 (2026-06-25): dark_combat (Joanna) legs scramble under cutscene anim 1157. Root-caused to this pattern. **Fix landed + structure-verified (per-vertex matrix carried through extract+consume); awaiting human render verification.** The jointflags/type_hi work was a prior partial (computed the helper matrices but the seam verts were still mis-bound to one matrix).

## SP-18: AI ailist command returns "continue" without advancing `g_Vars.aioffset`

**Severity: HIGH (frame hang).** The ailist dispatcher (`src/game/chrai.c`, the
`while (g_Vars.ailist)` loop) executes `g_CommandPointers[type]()`; a return of 0 means
"continue processing this frame" and the command is REQUIRED to have advanced
`g_Vars.aioffset` itself. Any command whose early "handled" return path skips its advance
makes the loop re-dispatch the same command forever -> the whole frame hangs (watchdog
kill; no crash, MEMPC intact). Found as B-949 (2026-07-04): `aiSayCiStaffQuip` ->
`scenarioSourceAiGraphExecuteSayCiStaffQuip` returned 1 (audio unresolved / char-ptr fail
/ required<0) WITHOUT the `g_Vars.aioffset += 4` that its success path does; intermittent
because it only fires when the quip audio fails to resolve.

**Fix applied (the class, not the instance):** a no-progress guard in the dispatcher --
capture `prevoffset` before the call; if the command returns 0 and left `aioffset`
unchanged, force-advance by `chraiGetCommandLength`. Commands that intentionally move
aioffset (jump/goto/label) already differ and are untouched.

**Audit checklist:** any `scenarioSourceAiGraphExecute*` (or legacy `ai*`) handler that has
BOTH a `g_Vars.aioffset += N` success path AND early `return` paths -- confirm the early
returns either advance aioffset or return the "break" value (non-zero from the command).
Search: `grep -n "return 1;" port/src/scenario_source_runtime.c` near `aioffset +=` sites.
The dispatcher guard now backstops all of them, but the root handlers should still be
correct so the intent (skip vs re-run-next-frame) is explicit.

## SP-19: Recursive graph flood with a depth cap but no visited-set

**Severity: HIGH (load/frame hang).** A recursion that walks a graph (rooms via portals,
nodes via edges) and guards only with a DEPTH cap -- not a visited-set -- revisits the
same nodes via every distinct path. On a highly-connected graph the call count is
branching^depth, which for real data (e.g. `base:villa`, 304 portals, depth cap 20)
explodes into billions of calls = an effective hang, even though it is technically
"bounded". Found as B-948 villa (`func0f001c0c -> func0f00215c -> func0f002844` in
`src/game/dlights.c`): the light-flood recursed with `arg2 < 20` but no visited-set.

**Fix applied:** a total-call cap on the existing per-call counter (`DLIGHTS_FLOOD_CALL_CAP
= 3,000,000` on `var80061440`); normal levels finish far below it (291-683,952), the
pathological level caps + continues (approximate result + WARNING) instead of hanging.
The cleaner-but-bigger alternative is a real visited-set; the cap is the minimal,
behaviour-preserving guard for legacy N64 flood algorithms.

**Audit checklist:** any self-recursive function whose only recursion guard is a depth/
count compare on a parameter, walking geometry/graph data whose connectivity is
data-driven (portals, waypoints, AI links). Especially N64-era algorithms tuned for
small original levels but now fed larger/modded graphs. Search: `grep -rn "func.*(.*arg2
+ 1" src/` and look for recursive calls guarded only by `arg < MAX`.

---

## SP-20: Recycled pool slot inherits stale fields (partial re-init)

**Severity: HIGH (0xc0000005 use-after-stale-value).** An object is allocated from a
recycled pool slot (bump-allocated `MEMPOOL_STAGE` memory) and its init function sets
fields ONE BY ONE rather than zeroing the whole slot first. Every field the init MISSES
keeps the bytes of the PREVIOUS occupant of that slot. When a later read treats that
stale value as an index / tagnum / pointer, it dereferences freed or unrelated memory and
AVs. The bug is invisible at low reuse (slots happen to land on fresh-zeroed memory) and
only appears when reuse crosses into memory previously used by a different data structure
-- so it presents as count-/order-dependent and layout-sensitive, easy to misattribute.

**Known instances (same class, `chrInit` in `src/game/chr.c`):**
- **B-331:** `chr->myspecial` un-inited -> tagnum-shaped garbage -> `objFindByTagId` stale
  `tag->obj` deref -> AV at `chrCalculatePushPos` (fixed 2026-05-16 by adding
  myspecial/yvisang/teamscandist/convtalk/naturalanim defaults -- the INSTANCE).
- The whack-a-mole of adding one field per crash is the anti-pattern.

**Fix applied (the CLASS fix, 2026-07-05):** `memset(chr, 0, sizeof(*chr))` at the top of
`chrInit` (right after the slot is found/NULL-checked, before the explicit inits). Every
field now starts at 0; the existing explicit `-1`/sentinel inits still run and override
where a non-zero default is required. Safe because all callers (chr0f020b14, body.c MP/AI
paths, botmgr.c, playerreset.c) customize AFTER chrInit returns. Modern HW does not need
the N64's skip-the-memset micro-optimization (project rule: correctness > micro-opt).
Validated: combat_sim 15/15, swarm_cpu clean (no crash). NOTE: this did NOT fix B-952
(whose crash is a corrupt MODEL node tree, a different class) -- it prevents the whole
stale-chr-FIELD class going forward.

**Audit checklist:** any `*Init`/`*Reset`/`*Allocate` that (a) takes a slot from a pooled/
recycled array or `mempAlloc`'d region and (b) assigns fields individually instead of
`memset(0)`-first. If a new field is added to the struct but not to the init, it silently
inherits garbage. Prefer zero-then-set-sentinels over enumerate-every-field. Search:
`grep -rn "mempAlloc\|g_.*Slots\[" src/game/*.c` and check the matching init zeroes the
whole record before per-field assignment.

---

## SP-21: Sibling accessor functions with inconsistent NULL handling

**Severity: MEDIUM-HIGH (0xc0000005 on a legitimately-NULL argument).** A family of
related accessor/traversal functions shares a contract where one of them accepts a NULL
node/pointer as a valid "nothing here" input, but one member of the family forgot the
guard. Callers that legitimately produce NULL (from a finder that returns NULL by design)
pass it into whichever family member they need; the guarded members degrade gracefully,
the unguarded one dereferences NULL and AVs. Presents as content-specific (only fires when
a particular asset/scenario yields the NULL) and is easy to misattribute to the caller's
subsystem instead of the primitive.

**Known instance (B-952, `src/lib/model.c`):** `modelNodeFindMtxNode()` returns NULL by
design when a bbox node has no CHRINFO/POSITION/POSITIONHELD ancestor. Its result is fed
directly into the node-position family:
- `modelNodeGetModelRelativePosition` -- guarded (`while (node)`), NULL-safe.
- `modelFindNodeMtxIndex` / `modelFindNodeMtx` -- guarded (`while (node)`), NULL-safe.
- `modelNodeGetPosition` -- **NOT guarded**: bare `switch (node->type & 0xff)` -> AV on
  NULL. base:skedarruins parents a DROPTYPE_5 projectile to a bbox-less root object, so
  `objDrop` fed NULL in and crashed at 5.2s (deterministic). Fixed by adding a NULL guard
  that mirrors the function's own `default:` case (zero position, return), plus a
  caller-side guard in `objDrop` (propobj.c) that logs WHICH object is bbox-less.

**Fix strategy:** when one member of an accessor family treats NULL as valid input, ALL
members reachable from the same finder must. Guard the primitive to match the family's
weakest-precondition member, not just the crashing call site.

**Audit checklist:** for any finder that can return NULL by design (`*Find*` returning a
pointer), grep its call sites and confirm every consumer tolerates NULL. Search:
`grep -rn "modelNodeFindMtxNode\|modelFindBboxNode\|objFindBboxNode" src/` and verify each
result flows only into NULL-tolerant callees.

**Debugging-method note (why this cost a whole session):** the crash breadcrumb pointed at
`chrTick`/chrnum-5052 (the last chr ticked), and 8 hypotheses + 3 custom debug tools
(memp canary, DR0 hardware watchpoint, per-instance modeldef clone) were spent chasing the
CHR subsystem. A 1-step symbolize of the crash RETURN STACK immediately showed
`modelNodeGetPosition <- objDrop <- objDropRecursively <- objTickPlayer` -- an object drop,
not a chr. **Pull and symbolize the crash stack BEFORE trusting a domain breadcrumb**; a
breadcrumb tells you what ran last, not what crashed.

---

## SP-22: Public asset source fields stop at registration or silently fall back

**Severity: HIGH — author edits can be accepted and activated without affecting
production behavior.**

The typed-archive pipeline has several independently successful layers:
extraction, descriptor/manifest parsing, catalog registration, generic runtime
binding, and the final native gameplay/renderer/audio consumer. Tests that stop
at an accessible binding can report a family green even when no production
consumer reads the binding, only a subset of fields is read, or a missing
binding silently falls back to a parallel native/catalog table.

**Known instance (B-953, `.pdgamemode`):** all public fields reach
`asset_runtime_binding_t`, but `scenarioCtxAccepts` consumes only
`gamemode_team_based`. `min_players`, `max_players`, public name/description,
and the authored rules file do not control the scenario picker/rules path. The
test suite explicitly pinned min/max as unwired. A missing binding warns once
and uses `ext.gamemode.team_based`, so the source-chain failure is masked.

**Related sites requiring propagation audit:** bot-profile creation/setup falls
back to `g_BotProfiles`; theme registration falls back to a catalog
FileProvider path; the generic runtime layer supports 21 metadata families, but
direct `assetRuntimeFind*` production queries currently exist only for game
modes, bot profiles, and themes. Specialized catalog/compiler consumers may be
valid and must be credited separately; a binding with no consumer is not proof
of production use.

**Fix strategy:**

1. Inventory every public field for every typed family.
2. Trace each field through extractor, archive, scanner/walker, catalog,
   provider, runtime adapter/cache builder, and a real production consumer.
3. Add behavior-focused tests that change the public value and observe the
   production result; do not accept source-presence/static-string assertions as
   utilization proof.
4. Remove or clearly reject unsupported fields instead of advertising inert
   author controls.
5. Under public-source ownership, treat a missing post-extraction binding/source
   product as an asset-chain failure; do not silently substitute ROM or a
   hand-maintained native mirror.

**Search commands:**

`rg -n "assetRuntimeFind|RUNTIME_MISS|using g_|using ext\\." port src`

`rg -n "entry->ext\\.|binding->" port/src/asset_runtime.c <production-consumer>`

---

## SP-23: Aggregate operations log child failures but return success

**Severity: CRITICAL — partial asset sets can receive success receipts.**

A batch extractor, converter, loader, or registration walk tracks per-item
failures and may even suppress its success cache, but returns only the number
of successful writes/loads. Its caller therefore receives a nonnegative result
for a partial data set and can publish a false completion receipt.

**Known instance (B-958):** 12 typed-family emitter implementations and the
post-texture UI repair path.

**Fix strategy:**

1. Continue the batch far enough to report every child failure.
2. Return a negative aggregate result when any required child failed.
3. Return a success/skip count only when the failure count is zero.
4. Write fast-cache and completion receipts only after complete success.
5. Preserve the same contract in late repair and retry paths.
6. Enumerate every family in focused tests so a new emitter cannot silently
   reintroduce the pattern.

**Search command:**

`rg -n "failed|failures|return written|return .*_written" port/src/romextract_pd*.c port/fast3d/pdgui_theme*.cpp`

---

## SP-24: Placeholder public source presented as extracted content

**Severity: CRITICAL — creators can edit files that do not represent the game
state and that the game does not execute.**

An emitter creates a well-formed typed archive whose payload names an original
backend, uses empty arrays, default colors, generic tuning, or prose such as
`"source": "original_perfect_dark"`. The archive passes envelope and catalog
tests, but it neither contains the complete source state nor drives the native
production path. This is more dangerous than an absent format because the UI
and validation surface imply mod support that does not exist.

**Known instance (B-959):** prop behavior-graph execution and parts of
`.pdmission` remain open, along with residual partial fields in
weapon/character/voice/effect/theme. HUD, material, skin, vehicle, game-mode,
bot-profile, and prop core state now have structured source hydration, strict
schema validation, production consumers, and focused mutation/behavior proof;
live edited-source receipts are still required before those families are
validated.

**2026-08-08 through 2026-08-10 effect propagation (B-1004/B-1014/B-1015/
B-1016):** Retaining a complete executable `.pdeffect` program was still only
an intermediate representation. Wave8 routed the three complete base profile
libraries into native consumers. Wave11 adds the generic v1 production owner,
strict field schema, and all-consumer transaction: gameplay/audio,
screen/decal/light/beam/particle presentation, topology, named contexts,
targets, attachments, timeline intensity, priority, lifetime, and cleanup now
either reach a production consumer or reject at activation. The remaining
`V-009` boundary is live edited-source/peer proof, not disconnected code.

**2026-08-08 selected-source propagation (B-1005):** A typed public reference
is not optional merely because an older native default exists at its callsite.
Missing, corrupt, disabled, wrong-type, channel-incomplete, runtime-gated, or
conflicting selected effects must make the selection unavailable and reject
the owning activation transaction; only an actually empty reference may use an
explicit callsite default. Nested source registries also require durable owner
edges, complete authored-source identity (descriptor plus every program
member), final-owner cleanup, and growable dependency rollback. Apply this
audit rule to every remaining public typed reference: prove both positive
source-to-consumer behavior and negative no-fallback/no-stale-state behavior.

**2026-08-13 save-load propagation (B-1068):** The same rule applies to
versioned save migrations. A present typed field is authoritative: an invalid
value rejects the complete candidate and may not fall through to a numeric
legacy field. Numeric migration is legal only when the typed field is absent,
the old version permits it, and the mapping is unique in the target domain.
Parsing and migration must finish before any live setup, player, bot, RNG, or
option state is published.

**2026-08-13 launch-freeze propagation (B-1069):** Serialization is a commit
boundary for authoritative match state. After a launch packet is validated and
written, no shared launch consumer may reroll, normalize, unlock-filter,
reconfigure, reverse-resolve, or rebuild any serialized field or roster member.
Offline setup conveniences remain before the boundary; network launch consumes
the prepared state and performs only nonfallible stage-transition side effects.

**2026-08-08 lifecycle propagation (B-1008):** Reset truth has the same
ordering requirement as activation truth. Retire every live typed-owner
closure while catalog identities and dependency edges still resolve; clear
runtime adapters next; clear mod/all dependency edges next; only then remove
or reuse catalog IDs. Disabling a dependency also requires a reverse-owner
walk: retire/rebuild or fail every cached parent consumer before the disabled
child can be considered unreachable. The effect/weapon reset slice is
T-ASSETS-020, reverse dependency invalidation is T-ASSETS-033, and the
all-family reset transaction is T-CATALOG-004.

**2026-08-08 all-family closure (B-1009):** T-CATALOG-004 removes the
effect-only type filter. Disable and reset snapshot every selected family,
preflight all closures before the first mutation, retire a growable
parent-first order, detach family adapters outside the catalog mutex, and
rebuild pointer caches after mod rows are replaced. Mod reset preserves bundled
rows/edges/adapters and full reset clears process adapters before identity
reuse. This closes forward teardown; reverse-owner invalidation when a child is
disabled is implemented by T-ASSETS-033/B-1013 with a separate exact root-owner
ledger. Aggregate child refcounts cannot identify owners in shared diamonds.
Every root load/retain/release must update the ledger; reverse invalidation must
preflight every affected typed closure before mutation, retire exact root
counts, and keep failed reloads unreachable until a complete transaction passes.

**Fix strategy:**

1. Inventory the native state and behavior that each advertised family owns.
2. Extract every supported field into an editable, named, source-faithful
   schema; reject unsupported fields rather than filling placeholders.
3. Compile or apply that same public source into the existing production
   consumer, with source-hashed engine products only as private cache.
4. Remove generic-activation-only claims; a binding without a consumer is
   `partial`, not implemented.
5. Add change-the-source/observe-production behavior tests and live evidence.

**Search commands:**

`rg -n "slots.: \\[\\]|source.*original_perfect_dark|parity_backend|default.*material|no consumer yet|ignored" port/src/romextract_pdmeta.c port/src port/fast3d src/game`

`rg -n "assetRuntimeFind" port src -g "!asset_runtime.c" -g "!tests/**"`

---

## SP-25: Public-source failure gated only by optional debug mode

**Severity: CRITICAL — production can silently ignore a broken or edited
public archive.**

A consumer resolves a FileProvider/public typed source, attempts to compile or
play it, and only refuses the native fallback when `assetSourceDebugIsEnabled`
is true. Normal builds therefore accept a source-chain break that source-only
tests reject.

**Known instance (B-960):** font segments, music sequences, animation clips,
file-backed SFX/voice, and MP3 source.

**Fix strategy:**

- Once a typed public source is selected, failure is unconditional.
- Optional debug flags may add diagnostics, never determine correctness.
- Native/ROM/loose-cache paths are allowed only for a route that has not yet
  selected a public source and is explicitly outside the migrated family.
- Induced-failure tests must run without debug flags.

**Search command:**

`rg -n "assetSourceDebugIsEnabledFor|falling back|fallback to ROM|loose extracted|raw ROM bytes" port/src src/game src/lib`

---

## SP-33: Transaction staging mutates shared state or collapses repeated authored rows

**Severity: CRITICAL — a rejected public graph can leak state, or valid
repeated nodes can silently overwrite one another.**

A nominal two-phase consumer is not transactional when its node/stage callback
allocates a global registry row, starts output, or otherwise mutates shared
state before every consumer has prepared. Likewise, a committed projection
keyed only by the owning asset/instance cannot represent two valid authored
nodes under that owner; last-write-wins storage silently omits content.

**Known instances (B-1015):** v1 effect spark tint registration mutated the
global custom-spark registry during node staging, so a later presentation-lane
failure left a row behind. Particle snapshots keyed only by `instance_id`
collapsed every additional particle node in the same graph. The fix reserves
spark rows during prepare behind a rollback checkpoint and keys particle state
by instance plus stable node/execution identity; cleanup still removes the
complete owner set.

**Fix strategy:**

- Stage only lane-local, discardable descriptions.
- Put fallible allocations/resolution in prepare; checkpoint any shared
  reservation and restore it when any later lane fails.
- Keep commit infallible and synchronous after every lane prepares.
- Derive projection keys from the authored cardinality: owner plus stable child
  identity for one-to-many records, never owner alone.
- Test both a later-lane prepare failure with zero leaked mutation and two
  same-kind child nodes surviving commit and owner cleanup.

**Search commands:**

`rg -n "stage|prepare|commit|rollback|Register|Create|Append" port/src src/game`

`rg -n "instance_id ==|asset_id.*==|\[.*owner.*\]" port/src src/game`

---

## SP-34: Derived readiness flag permits a null production payload

**Severity: CRITICAL — a state machine can advertise ready before its actual
runtime object exists.**

Do not use aggregate load flags, memory-owner state, or a visibility latch as
a substitute for the pointer that the next line dereferences. Every readiness
gate must include the exact payload it consumes and remain safely retryable
while that payload is absent.

**Known instance (B-1017):** first-person weapon rendering set
`hand->visible` after the legacy load flags passed even though
`gunctrl.gunmodeldef` was null, then dereferenced `modeldef->nummatrices`.

**Propagation check:** search state/visibility gates immediately followed by
pointer dereferences, especially asynchronous model, texture, audio, and
catalog payload activation paths.

`rg -n "visible = true|loaded = true|state = .*LOADED|->nummatrices|->rootnode" src port`

---

## SP-35: Alias inventory mistaken for the complete runtime reference domain

**Severity: CRITICAL — a partial source inventory can pass extraction while
ordinary gameplay still reaches an unrepresented public source.**

Configured aliases, curated manifests, and currently observed leaf rows are
not substitutes for enumerating every reference form accepted by production.
For each source-only gateway, derive the complete input domain from the actual
runtime constants/tables and prove every reachable reference resolves to a
typed public archive and FileProvider handle.

**Known instance (B-1018):** `.pdvoice` extraction covered configured MP3
aliases but omitted direct `MP3_*` file constants. Mission scripts reached
`MP3_0408` / file 1032 through `psGetDuration60`, where the catalog correctly
found no public source and failed closed.

**Propagation check:** compare each extractor's enumerated source set against
all direct and aliased runtime references for that family; pin set equality,
not one representative member.

`rg -n "^#define MP3_|hasconfig|source_filenum|source_soundnum" src/include port/src`

---

## SP-36: Creator conformance is weaker than production source admission

**Severity: CRITICAL — a generated public archive can pass creator checks but
be rejected by the authoritative runtime parser.**

Archive layout and required-member checks do not prove that a public
descriptor obeys the exact runtime schema. A creator generator, conformance
tool, and production parser must share or independently pin the same allowed
field set, types, and version boundary. Unknown fields must fail before a
tracked example is published, not first during an installed-client run.

**Known instances (B-1019/B-1020):** the Needler generator copied node-only
`explosion_class` into `effect.ini`, then the next gate showed its projectile
`spark_ref` named the embedded texture identity rather than the executable
effect identity. Layout conformance verified the declared graph/timeline
members, while the authoritative parser and typed activation correctly
rejected both creator defects and prevented the complete nested weapon owner
from activating.

**Propagation check:** for every typed family, compare generator descriptor
keys and conformance validation against the authoritative scanner/parser; add
an unknown-field mutation to conformance self-tests wherever strict production
admission exists.

`rg -n "allowed|unknown field|validate_.*source_contract|\.ini" tools/asset_archive_conformance.py port/src tools`

---

## SP-37: Runtime owner activates without hydrating its production adapter

**Severity: CRITICAL — source validation and ownership can pass while ordinary
gameplay still reads a zeroed or stale family-specific runtime object.**

A typed asset is not production-connected merely because its graph or metadata
runtime is active. Every legacy/engine adapter still read by normal gameplay
must be hydrated inside the same admission boundary and retired on final owner
release. A default-zero adapter must never masquerade as a valid payload or
trigger a legacy shortcut.

**Known instance (B-1021):** custom `.pdweapon` activation registered the held,
projectile, and effect programs but did not build a loader-owned private weapon
slot from public `weapon.ini` plus the compiled held graphs. `weaponGetFileNum`
returned zero, the first-person loader declared the weapon loaded without a
model, and the B-1017 readiness guard correctly kept it invisible. Parsing the
private manifest would have hidden the symptom by creating a second behavior
source, so the fix instead derives and transactionally owns the legacy adapter
from the public source/runtime IR.

**Known instance (B-1211, 2026-11-15):** the same public weapon adapter zeroed
the weapon's bot preferences. Native bot AI reads a clear `haspriammogoal` /
`hassecammogoal` as "never use this function", so every bot armed with a mod
weapon went unarmed. The fix derives defaults from the compiled functions and
magazine (`s_publicWeaponBotPrefsBuild`). Other per-weapon native tables a
public weapon fills with defaults, to recheck when one misbehaves: aim settings
(`s_DefaultAim`), casing eject (none), reload animation (none), MP weapon-set
membership.

**Propagation check:** compare every typed activation/clear pair with the real
family manager, renderer, audio, collision, or gameplay object it supplies;
test active -> adapter populated -> final release clears -> reload rebuilds.
For the public weapon adapter, list each native per-weapon table gameplay
reads (`catalogManagerGetWeapon*`, `weaponGetAmmoByFunction`, bot preferences)
and confirm the adapter writes a usable value, not a zeroed default.

`rg -n "ACTIVATE|Register.*Runtime|Clear.*Runtime|loaderPoolParse|catalogManagerGet" port/src src/game`

---

## SP-38: Selected catalog identity re-derived through a secondary index domain

**Severity: CRITICAL — a valid selected asset can resolve to a different
runtime object after catalog rebuild or custom-slot allocation.**

Once a catalog row has an authoritative runtime identity, consumers must not
discard it and re-resolve through an MP slot, stage index, menu index, or other
secondary domain. Secondary IDs remain useful for UI/wire tables, but they are
not interchangeable with the gameplay runtime index.

**Known instance (B-1022):** specific-spawn match setup resolved the Needler
catalog row at runtime 86, then mapped its MP slot back to runtime 84. The
correct public adapter was live, but the player equipped a different zeroed
weapon slot.

**Propagation check:** for every selected catalog ID, trace the row's typed
runtime index to the final consumer; reject any second lookup through another
index table unless that conversion is the explicit protocol contract.

`rg -n "assetCatalogResolve.*ext\.|runtime_index|catalogGet.*Num" port/src src/game`

---

## SP-47: Diagnostic state leaks across tests or emits without a finite evidence budget

**Severity: HIGH — verification can hang or become dependent on a user's saved
debug preferences while production behavior is healthy.**

Every automated fixture must assign each diagnostic gate it owns, including the
off state. A fixture that merely enables an opt-in flag when requested inherits
persisted process/config state when the field is absent. High-frequency render,
physics, input, and network probes must also deduplicate by a stable evidence
identity or use a finite budget; per-frame logging is not a valid long-duration
proof contract.

**Known instances (B-1081):** non-opt-in Combat Simulator smokes inherited
`Debug.JumpLogging=1` and emitted 98,484 capsule lines before timing out;
generated-model step tracing logged every node/stage every frame and repeatedly
rearmed the Needler witness, making the ordinary client unresponsive before its
first endscreen; and the camera-only `--debug-force-first-person` flag implicitly
enabled the unbounded weapon diagnostic stream. Camera control now remains
separate, while explicit/generated-audit weapon tracing has an atomic line
budget and one suppression witness.

**Propagation check:** inspect every smoke/config bridge for enable-only writes,
and every tick/render/network diagnostic for an unbounded `sysLogPrintf` path.
Keep one-shot semantic witnesses separate from detailed traces so rate limiting
does not erase release evidence.

`rg -n "if \(.*logging.*\)|sysLogPrintf|Trace.*Step|debug-.*audit|configRegister.*Debug" port/src port/fast3d src tests tools/smoke-verify`

---

## SP-48: Test fixture generator duplicates a versioned production schema

**Severity: HIGH — a required production smoke can fail before reaching its
target boundary, or worse, keep exercising an obsolete accepted shape after
the real schema changes.**

A test helper must not hand-author a second copy of a versioned save, wire, or
typed-archive document when a canonical validated fixture or production encoder
already exists. Schema fields, defaults, migration rules, and completeness
masks otherwise drift independently.

**Known instance (B-1082):** `generate_friend_play_identity.py` duplicated a
partial Agent Profile v2 document and omitted `besttimes`. D-005 strengthened
the real loader to accept only complete current or exact known-v2 documents, so
both D-003 authority smokes failed closed during role activation. The generator
now clones the canonical valid Agent fixture and changes only the role name.
The pinned contract passes 94 assertions in 5 cases, and behavioral generation
for initiator and invitee remains deep-equal to the canonical profile apart
from that name while retaining all 21 required `besttimes` entries.

**Audit:** search fixture generators for inline `version` objects and compare
them with their production codec/emitter. Prefer invoking the production
encoder; otherwise clone one canonical validated template and pin that the
generator does not repeat schema fields.

`rg -n "[\"']version[\"']|version.*[0-9]" tools tests devtools -g "*.py" -g "*.ps1" -g "*.js"`

---

## SP-49: Test fixture seeds machine preference after activating an Agent owner

**Severity: HIGH — a production smoke can silently replace the staged machine
default before reaching its target behavior.**

Agent Profile activation owns personal theme, audio, gameplay, and enabled-mod
preferences. A fixture that writes only `mods-enabled.json` and then activates
an Agent is internally contradictory: the validated Agent transaction will
apply its own `enabled_mods` list and rebuild the catalog. Tests must seed the
preference at the same ownership boundary the production flow consumes.

**Known instance (B-1083):** both D-003 authority fixtures packed and globally
enabled Needler, but their migrated v2 role profiles carried the canonical
empty per-Agent mod list. Activation transiently unmounted Needler, so the
authority bound and published a valid route before transactional lobby start
correctly rejected the now-unavailable specific spawn weapon.

**Audit:** for every smoke using `--launch-load-agent`, compare machine-level
preference fixtures against fields the Agent transaction owns. When a legacy
v2 fixture needs nondefault preferences, stage the validated `prefs_<agent>.ini`
migration sidecar; do not mutate production state directly after activation.

`rg -n "launch-load-agent|mods-enabled.json|prefs_[A-Za-z0-9_-]+\\.ini" tools/smoke-verify/tests -g "*.json"`

---

## SP-50: Smoke-only state override substitutes for a production transition

**Severity: HIGH — a fixture can render a plausible frame while gameplay is
still owned by a cutscene, menu, countdown, or other transition state.**

Debug camera, placement, and render-audit hooks may observe or frame a loaded
stage, but they do not prove that ordinary input has acquired gameplay control.
Any smoke that needs to leave a production transition must use the same action
or lifecycle transaction as a player, then prove downstream gameplay behavior.

**Known instance (B-1084):** both D-003 authority fixtures used
`--debug-force-first-person` on Chicago and fired on a wall-clock schedule. The
hook temporarily forced camera mode zero, but Chicago's intro cutscene resumed,
kept `tickmode=6` and `MOVEMODE_CUTSCENE`, and prevented the queued Needler from
equipping. Repeated early/later/cold-load Skip waves were an intermediate
mitigation, not a durable transition proof. The fixtures now cross the typed
network-stage barrier and use one stable gameplay wait whose exact cutscene
aperture may own one bounded `ACTION_SKIP_CUTSCENE` assist before gameplay
pulses; the immutable network-launch receipt remains pinned.

**Audit:** for every gameplay smoke using a debug state override, identify the
real transition owner and require an ordinary input or lifecycle witness before
accepting fire, movement, save, endscreen, or rendering evidence.

`rg -n "debug-force|debug-place|ACTION_SKIP_CUTSCENE|SMOKE: action|tickmode|cameramode" tools/smoke-verify/tests port/src src tests`

---

## SP-51: Direct smoke input remains hidden behind a production focus gate

**Severity: HIGH — a correctly injected action can be logged as held while the
background ordinary client consumes no input, making multi-process proof depend
on which window the OS focused last.**

A smoke-only direct-input API must define both state mutation and read authority.
Writing the ordinary action state is insufficient when every public query still
passes through a production window-focus or menu-suppression predicate. The
focus-independent aperture must belong only to explicitly injected state; it
must not disable production focus suppression globally, bypass the typed input
layer's action set, or override observer/freefly restrictions.

**Known instance (B-1085):** the first-launched invitee-authority client lost
focus when the initiator process opened. `actionmapInjectStateForSmoke` reported
`held_after=1`, but `actionHeld` returned false through
`gameplayInputSuppressed()`, so a fully loaded Needler never fired. The D-003
route and match lifecycle passed 138/144; only gameplay/presentation effect
witnesses were absent. Correct ownership requires both halves of the boundary:
every physical writer must defer only for the exact injected owner, while public
reads must preserve typed apertures, menu/freefly authority, and ordinary focus
suppression. A production-linked pure decision seam now proves that focus loss
still denies an unowned gameplay action and admits only an exact owner under a
gameplay-capable context for focus loss/regain settling. Smoke presses cannot
seize already-held physical state, and unowned releases are no-ops.

The inverse-role production run exposed a second half of the same authority
boundary. Win32 foreground ownership changed successfully, but core input
authority emitted no focus edge because the SDL pump depended only on a queued
window event after general UI dispatch and had no reconciliation path. Window
focus is lifecycle state, not consumable UI input: the main-window edge must be
routed before `pdguiProcessEvent`, the event must still reach ImGui for its own
bookkeeping, and the idempotent authority state must be reconciled once per
pump against `SDL_WINDOW_INPUT_FOCUS`. This preserves ordinary menu/textbox
capture while making missed or coalesced focus events self-healing. Current
input contexts do not consume `SDL_WINDOWEVENT`, so reconciliation is the
evidenced runtime repair and pre-UI routing is structural lifecycle hardening.

Verifier control is part of the systemic fix. Launch order is not focus proof;
every named process must reach its readiness barrier, checked focus acquisition
must use the declared timeout, the losing process log must remain append-only,
and the target must still own GUI-thread active keyboard focus at the new
focus-loss witness. `GetForegroundWindow` equality proves z-order, not
`hwndActive`/`hwndFocus`; cross-process focus automation must verify both through
`GetGUIThreadInfo`. Presence checks are also insufficient: strict per-process
sequences must prove `focus LOST -> clean smoke owner acquisition -> gameplay
read/effect` on later lines. If the operational transition fails, every causal
sequence explicitly tied to it needs an unreachable named exact-line anchor so
older whole-log matches cannot remain credited inside a rejected receipt.
Sequences that prove startup or gameplay state before the focus handoff must
remain untagged and retain whole-log semantics; one process-wide anchor silently
invalidates otherwise valid evidence from an earlier phase.
The first controlled attempt correctly failed closed at 62/147 when the later
process still had no window. A later inverse-role attempt reached both windows
but moved focus while the elected authority was still inside a bounded public-
route probe, so its event loop could not emit the focus-loss witness before the
transition deadline. The fixture now uses the existing second-process
`gameplay_ready` launch barrier before changing focus. The current inverse run
reached stable gameplay on both peers and confirmed foreground transfer, then
failed closed because the core focus witness was still absent. After per-pump
reconciliation, the next run exposed the distinction precisely: each client
logged its initial focus loss, but the runner never established a fresh source
keyboard-focus gain before requesting loss, so its 15-second barrier killed both
clients before scripted exit. A launch/readiness barrier must cover the phase in
which focus proof is consumed, the production SDL boundary must expose the OS
transition, and the runner must prove active keyboard focus; launch order or
foreground ownership alone is not enough.

The next checked-GUI-thread receipt (`results-20260814T022303Z.json`) kept the
product path healthy—both peers reached stable gameplay and emitted owned fire
plus source-backed effects—but still produced no fresh focus edge and correctly
failed 203/218. `Process.MainWindowHandle` had not been proven to be SDL's
window, and ignored native return values made the 15-second failure opaque.
Cross-process control must therefore enumerate visible unowned top-level HWNDs
by exact PID and require one candidate, check every attach/foreground/active/
focus call, verify GUI-thread state after detach, and require two fresh complete
SDL log witnesses: source `focus GAINED`, then source `focus LOST` after the
target is focused. Persist the operational failure phase and native state in the
result JSON separately from gameplay assertions. Do not use an undocumented
task-switch primitive or accept native focus without the SDL witness.

The corrected unique-HWND activation path then reached both fresh SDL witnesses
in `.claude/smoke-verify-runs/results-20260814T024631Z.json`: the invitee logged
the new `focus GAINED`, then the exact `focus LOST` while the initiator retained
GUI-thread keyboard focus, followed by owned fire, gameplay read, source-backed
effects, both scripted exits, and zero operational failures. The receipt reached
215/218 but remains rejected because the runner applied that loss line to every
invitee sequence, relocating three valid cutscene/readiness sequences that had
completed before the handoff. Runner-observed boundaries must therefore be a
named map consumed only by explicitly tagged sequences. Missing named entries
fail closed; transition failure assigns only that name an unreachable sentinel;
untagged sequences continue to inspect the complete append-only log.

The consolidated correction passed parser/JSON checks, the scoped-anchor
behavior self-test, embedded Win32 compilation, and 402 assertions in 13
focused B-1085 cases. Final inverse receipt
`.claude/smoke-verify-runs/results-20260814T030245Z.json` then passed 218/218
with the same fresh gain/loss and post-loss owner/fire/effect chain, while all
three pre-focus sequences remained valid. Both clients exited by script, no
operational failure or process leak remained, and the product/test/tool
fingerprint stayed `2784d121...` before and after. This is the accepted reusable
contract: phase boundaries are named assertion inputs, never implicit global
state on an entire process receipt.

**Audit:** find every direct test or automation mutator, then trace the same
state through the public consumer query and all authority gates. Require an
explicitly scoped ownership bit with release, flush, and end-of-frame cleanup;
prove foreground and background processes consume the same scripted gesture.

`rg -n "InjectStateForSmoke|DebugInject|gameplayInputSuppressed|focus LOST|SDL_WINDOW_INPUT_FOCUS|inputCtxNotifyFocus|actionHeld|actionPressed" port/src port/include port/fast3d src tests tools/smoke-verify`

---

## SP-52: One readiness sample is mistaken for a completed transition

**Severity: HIGH - automation can advance through a transient valid frame and
inject gameplay while an authored cutscene, menu, countdown, or stage owner is
about to retake control.**

A transition receipt is state over time, not a one-frame Boolean. A wait that
guards gameplay, saving, input, capture, or authority handoff must declare how
long its target must remain continuously valid; any false sample resets that
window. If the transition needs an assist action, the action must have a
separate exact aperture, fire at most once, use the production input owner, and
release on hold expiry and every timeout/failure path. Real watchdog time must
continue while virtual script time is paused, and the exact deadline must beat
late success.

**Known instance (B-1088):** both D-003 peers briefly satisfied
`gameplay_ready` immediately after stage start, so the first repair advanced
without Skip. Chicago's authored intro then activated, retook cutscene control,
and caused both later gameplay waits to fail. The superseded one-shot source
passed focused/full automation because its predicate was internally correct;
only ordinary-client timing exposed that the predicate was the wrong proof.
The replacement is a pure stable-target/one-shot-assist policy integrated into
the generic typed wait, with explicit runtime ownership and terminal cleanup.
The terminal receipt must carry the measured stable duration, not only echo the
configured duration. The fixture boundary must also reject malformed/truncated
JSON, incompatible or duplicate event fields, and explicit input holds that
would cross a paused wait; otherwise a false receipt or unbounded synthetic
hold can hide behind an otherwise correct transition policy.

**Audit:** inspect every readiness wait and polling loop that gates downstream
side effects. Classify it as an instantaneous predicate or a transition; for
transitions, require a continuous stability window, reset coverage, exact
deadline ordering, and bounded ownership cleanup. Reject duplicated timing
waves and OR predicates that merely select whichever transient state appears
first.

`rg -n "wait_until|Readiness|ready.*(true|1)|ACTION_SKIP_CUTSCENE|timeout_ms|SDL_GetTicks" port src tests tools/smoke-verify`

---

## SP-53: A validated client command has no authoritative result path

**Severity: CRITICAL - the server can accept and apply a reliable client
command while the requesting peer never receives the accepted state change.**

A client-to-server command is not a complete authority protocol by itself.
After validating source identity, lifecycle state, and payload, the server must
publish an explicit authoritative result to every consumer whose runtime state
depends on the command. The result must use stable wire identity and map that
identity into each receiver's local runtime domain; server player-slot numbers
cannot be copied directly when clients place their own player in runtime slot
zero. A rejected or unpublishable command must not consume the local input or
leave a latent state transition.

**Known instance (B-1089):** protocol v55 accepts
`CLC_CUTSCENE_SKIP {playernum}` on the listen authority and replaces the
untrusted requested slot with `srccl->playernum`. In ordinary Combat Simulator,
however, no server message reports that acceptance back to peers because
`SVC_CUTSCENE` is co-op-only. The authority therefore exits the authored intro
while the requesting client remains in its own cutscene. The retained runtime
proves a 900 ms production hold, one balanced press/release, server acceptance
for client 1/player 1, and a requester timeout after authority shutdown.

**Current correction (source-connected, verification pending):** protocol v56
keeps the CLC as an untrusted request and publishes the accepted stable client
identity only after authenticated validation. The first authority role proved
that result path; the inverse role then exposed a second half of the same class:
clients still minted generations from local script counts and Combat Simulator
did not receive the co-op-only start/end state, so a legitimate generation 3
request was rejected against authority generation 2.

The replacement design uses the same immutable prepared roster instance to
serialize `SVC_STAGE_START` and commit the authority snapshot, then owns one
match-room-scoped reliable queue for ordered START, ACCEPT, and END events in
every network game mode. Only server/offline code mints generations; local
client presentation may predict START but holds a nonzero token only during an
authoritative ACTIVE phase, and START/END are queued before authoritative local
mutation. Failed sends retain the batch; pending START/ACCEPT retries discard
ordinary shared traffic until authority publication succeeds. The terminal END
and `SVC_STAGE_END` share one prepare/send/commit packet, so local
authority/lobby teardown waits for successful queueing, partial delivery
retries are receiver-idempotent, and every terminal frame discards prior shared
traffic before retry and cannot publish later gameplay or spectator traffic.
Receivers require the exact full stable-client mask, run one
stale/duplicate/conflict transition planner, map the frozen roster into
receiver-local slots, and replace the full local active mask. Disconnect, room
leave/switch, spectator promotion, stage end, and network teardown retire
pending authority state. This is current source truth, not yet build or runtime
proof.

**Consecutive-transition instance (B-1090):** the first replacement runtime
proved that publishing START/ACCEPT/END is still incomplete if END is queued
from only one named helper instead of the actual state-mutation boundary. The
listen authority left `TICKMODE_CUTSCENE` through another production path, kept
generation 1 ACTIVE for another 15 seconds, and rejected the next authored
generation 2 START as a conflict. Every authoritative transition out of the
cutscene tick mode must therefore preflight END before local mutation; a
same-frame next START must append after that END and mint the next generation.
Tests must cover consecutive transitions, not only one isolated lifecycle.

**Audit:** enumerate every reliable CLC command that mutates shared or
peer-visible state. For each, identify the server validation, authoritative
commit, SVC result or snapshot, receiver-side identity remap, malformed and
stale rejection, and retry/idempotence behavior. A server log alone is not an
end-to-end receipt.

`rg -n "#define CLC_|case CLC_|netmsgClc.*Read|#define SVC_|case SVC_|netmsgSvc.*Read" port/include/net port/src/net tests`

---

## SP-54: Global gameplay work inherits a replicated-player context

**Severity: CRITICAL - global scenario actions can mutate a remote replica
while the process's actual local player never receives presentation state.**

Legacy gameplay uses `g_Vars.currentplayer` as an ambient parameter. Per-player
tick and render loops intentionally select each runtime player and may finish
with the final remote replica still selected. Any later global level, scenario,
or AI work that assumes the ambient pointer means "this process's player" can
therefore start a camera, read a condition, apply animation overrun, warp, or
write other player-owned state into the wrong slot. Receiver-local runtime
slot numbers are not stable wire identity: a client commonly maps itself to
slot zero even when the server identifies it with another player number.

**Known instance (B-1090):** the invitee-authority friend-play smoke published
the correct ordered END generation 1 and START generation 2, but its local
player 0 remained at `cut_progress=0`. The authored camera action and
`if_in_cutscene` branch had run against the replicated player left selected by
the previous frame. The opposite authority role passed only because its first
action happened while player 0 was coincidentally ambient.

**Verifier instance (B-1093):** `smokeCaptureReadinessFacts()` independently
hardcoded slot 0 for player presence, cutscene, and control observations. That
violated the same identity rule even though the next exact-client receipt
proved this particular ordinary client really occupied runtime slot 0; stable
client ID 1 was not evidence of receiver-local slot 1. Verifier projections
must use the same canonical local identity resolver as production gameplay,
and failure diagnosis must not infer a runtime slot from wire identity.

**Canonical correction:** derive process-local identity from pointer equality
with the committed `g_NetLocalClient->player`, require `!isremote`, and restore
that runtime slot at the global gameplay boundary. Presentation-specific graph
APIs must also resolve the local player explicitly for start, condition, and
animation synchronization, then restore any valid caller context. Never use
the peer's wire `playernum` as a receiver-local array index. If allocation is
not complete or is ambiguous, an in-client network process must defer the
global tick and retry instead of falling through to the ambient replica.
Dedicated server-only simulation has no presentation player and retains an
explicit separate ambient simulation policy.

**Audit:** locate global work adjacent to per-player selection, every
current-player-dependent scenario/AI action, and every diagnostic or verifier
that indexes player state. Prove whether each operation is truly per-player or
process-local presentation before preserving ambient state or selecting an
array slot.

`rg -n "lvTick\\(|lvRender\\(|setCurrentPlayerNum|g_Vars\\.currentplayer|playerCurrentCutscene" port/src/pdmain.c src/game port/src/scenario_source_runtime.c tests`

---

## SP-55: A fixed fast-lookup window becomes the identity domain

**Severity: HIGH - valid long-lived entities silently stop participating once
their monotonic identity exceeds an optimization table's capacity.**

Stable wire IDs and bounded lookup caches are different domains. A fast array
may cover common low IDs, but runtime IDs can remain valid after that window is
exhausted. Keying dirty bits, ownership, deduplication, or admission directly by
the raw ID silently turns the cache bound into an undeclared lifetime limit.

**Known instance (B-1064):** initial prop sync IDs were allocated by active and
paused list scans, which omitted allocated parented inventory/held children,
and runtime IDs were later reused from an incomplete boundary. The prop dirty
heartbeat then indexed a 2,048-entry bitmap by raw sync ID, so sufficiently
late valid runtime props stopped waking the resync path. The source correction
derives initial allocation from the complete raw pool minus its validated free
list, assigns deterministic one-based slot IDs to every allocated prop, starts
runtime IDs after the maximum initial ID, keeps them monotonic, uses the fixed
map only as a fast path with validated linear fallback, and treats dirty state
as a bounded wake-up latch after confirming the ID resolves. The reconnect
inventory mask separately asserts a 1..32-client domain and uses an explicit
full-width branch, avoiding the undefined `1u << 32` validation expression that
the first successful client build exposed. Consolidated verification and
production reconnect proof are still pending.

**Audit:** whenever an external/stable/monotonic ID indexes an array or bitmap,
identify whether that storage is the authoritative domain or only an
optimization. Require either an explicit protocol bound with rejection or a
validated fallback that preserves every legal identity.

`rg -n "syncid|NextSyncId|FirstDynamicSyncId|PROP_MAP_SIZE|Dirty|\[[^]]*(id|index|slot)" port/src port/include src tests`

---

## SP-56: Test discovery converts invalid input into an empty successful run

**Severity: HIGH - malformed or misspelled verification input can produce exit
zero without executing the requested proof.**

A runner that logs and skips malformed definitions has already lost the user's
requested test. If its empty-selection branch then exits successfully, queue
automation cannot distinguish “all selected tests passed” from “nothing ran.”
The same class appears when requested names are silently ignored, unsupported
filters select zero cases, or a parser warning is not represented in the final
result artifact.

**Known instance (B-1091):** the first B-1064 production-smoke attempt contained
one invalid JSON regex escape. `Get-SmokeTests` warned and continued, explicit
selection found zero tests, and `run.ps1` returned exit 0. No processes launched
and no result artifact was produced. The correction makes malformed discovery
and zero selection terminal, pins both boundaries in focused automation, and
requires the caller to accept only a fresh result artifact for the named test.

Focused correction passes 339 assertions in 6 cases, the explicit unknown-test
selection exits nonzero, and the later exact reconnect launch produced a fresh
named process artifact. B-1091 is therefore a regression gate rather than an
open runtime dependency.

**Audit:** for every test, smoke, capture, migration, and deployment runner,
trace parse errors, unknown names/tags, empty discovery, zero selected cases,
skips, and result serialization to the process exit code. Require the result to
name the requested work and report a nonzero executed count. A stale prior
artifact or process exit 0 is never sufficient.

`rg -n "Failed to parse|continue|No tests matched|selected.Count|exit 0|skipped" tools devtools tests`

---

## SP-57: Transport disconnect metadata is assumed to be symmetric

**Severity: CRITICAL - opposite peers can apply incompatible identity and
reservation policy to the same disconnect.**

A transport's user data on a disconnect command is not necessarily echoed to
the initiating endpoint's local completion event. In the vendored ENet path,
the remote peer receives `command.disconnect.data`, but the sender's
acknowledged-disconnect callback is explicitly emitted with `event->data = 0`.
Using that local observation as the server's policy source makes the client
retain a retry credential while the server destroys the matching reservation.

**Known instance (B-1092):** the B-1064 host called
`netServerKick(client, DISCONNECT_TIMEOUT)`. The ordinary client received reason
5 and retained its endpoint-scoped cookie, while the listen server observed
reason 0, classified the loss as terminal, left the room without a reservation,
and made reconnect impossible. The source correction latches the first
server-issued reason on the authenticated `netclient` before entering ENet,
consumes it exactly once during authoritative teardown, and routes every
post-assignment GUI/admin kick or ban through that boundary. A server intent
wins over a racing transport timeout, preventing a later event from turning a
terminal ban into a reconnectable loss.

The exact `0f377e3e...` production run validates the asymmetric case directly:
the host records reason 5 with `transport_reason=0 server_intent=1 retryable=1`,
retains one reservation, accepts one reconnect/commit, receives authoritative
fire, and exits cleanly. Corrected retained-log verification passes 98/98 on
unchanged product `7437d77c...`.

**Audit:** separate local intent, remote command metadata, and transport-cause
events. Trace every server-side `enet_peer_disconnect*` call after peer-to-client
assignment; it must either pass through the authoritative intent owner or prove
that no game identity/reservation cleanup can follow. Test both directions and
the race where terminal intent is followed by transport timeout.

`rg -n "enet_peer_disconnect|netServerKick|ENET_EVENT_TYPE_DISCONNECT|disconnect.*reason" port/src port/fast3d port/include tests`

---

## SP-58: A mode-specific authority guard blocks a shared state helper

**Severity: CRITICAL - a valid local presentation state can become permanent
when a helper is guarded by subsystem role instead of the transition it makes.**

Legacy helpers often serve several tick modes despite having a narrow name.
`playerEndCutscene()`, for example, ends authored cutscenes and the ordinary
Combat Simulator opening swirl. A blanket client/server guard at helper entry
therefore blocks every caller, including transitions that do not publish or
consume cutscene authority. Policy belongs at the state-mutation boundary and
must examine the current and requested modes, authority lifetime, and whether
the transition is an authoritative apply.

**Known instance (B-1094):** the ordinary client reached Felicity, spawned both
players, sent `CLC_STAGE_READY`, and ticked continuously, but
`playerTickMpSwirl()` could never advance from `TICKMODE_MPSWIRL` because
`playerEndCutscene()` returned for every `NETMODE_CLIENT`. The client therefore
missed its initial stage-live barrier and never reached the intended reconnect
action. The correction routes every exit through `playerSetTickMode`, which
holds only a real `TICKMODE_CUTSCENE` exit while the match authority stream owns
it and permits the local MP swirl to finish normally.

The exact `0f377e3e...` production run reaches NORMAL gameplay both before and
after timeout, then commits and resumes authority-accepted fire. This validates
the permitted MPSWIRL sibling without weakening the protected CUTSCENE path.

**Audit:** enumerate every caller of a role-gated shared helper and classify
the concrete source and destination states. Move authority checks to the
smallest common mutation boundary, test at least one protected transition and
one permitted sibling transition, and reject fixes that directly write the
target state around the canonical setter.

`rg -n "g_NetMode == NETMODE_CLIENT|g_NetMode == NETMODE_SERVER|playerEndCutscene|playerSetTickMode|TICKMODE_" src/game port/src tests`

---

## SP-59: A peer-dependent timeout begins before the peer can exist

**Severity: HIGH - valid cold-start work can consume a causal deadline before
the operation being measured is even eligible to begin.**

Multi-process runners often gate a dependent process on an externally observed
prerequisite such as a listen socket. If the first process starts its own
peer-dependent wait at harness boot, that deadline races the runner: cold asset
initialization consumes the budget while the peer is deliberately not yet
launched. Increasing one timeout hides the topology error and remains sensitive
to machine and cache state. Sequence an exact production-state prerequisite
inside the owning process before starting the dependent deadline.

**Known instance (B-1095):** the reconnect host began its 150-second
`network_stage_live` wait at 00:01, but did not create the ENet listener until
01:23. The runner launched the client only after that correct marker. Its
72-second cold initialization reached `NET: connecting` at wall time 02:35,
four seconds after the host wait had expired and closed the server. B-1094 had
already advanced the client to normal gameplay presentation; the 23/93 receipt
failed solely because the stage-live budget preceded peer eligibility. The
correction adds a typed `network_listen_ready` barrier and starts the host's
stage-live budget only after that barrier commits.

In the exact accepted production logs, listen readiness satisfies at 41.855
seconds and only then begins the stage-live wait; the peer subsequently joins,
reconnects, commits, fires, and exits cleanly. B-1095 is now a regression gate.

**Audit:** draw the prerequisite graph for every multi-process timeout. Check
when each process starts, when the runner permits its peers to launch, and when
each in-process deadline starts. Require an exact readiness barrier for server
listen, authentication, stage publication, or other causal prerequisites; do
not use fixed startup sleeps or cache-dependent timeout inflation.

`rg -n "wait_for|wait_timeout_seconds|wait_until|timeout_ms|network_stage_live|created server" tools/smoke-verify port/src tests`

---

## SP-60: Compound serializer failure loses the owning substage and blames the peer

**Severity: CRITICAL - an authority-local state defect becomes opaque and can be
misreported as remote content corruption.**

Large protocol transactions often compose roster, world, inventory, movement,
score, and commit writers. Returning one undifferentiated nonzero result from
the composition boundary hides which invariant failed. Mapping that result
directly to a peer-facing policy reason compounds the defect: atomic rollback
may remain correct, but diagnostics point at the wrong owner and a retryable or
server-local condition can consume a valid credential as if the client sent
bad files. Each compound writer must return a typed substage/result, preserve
the first failure before rollback, and map transport policy separately from
serialization diagnostics.

**Known instance (B-1096):** the ordinary reconnect transaction passed endpoint
authentication, manifest validation, stage replay, post-load READY, and room
reservation reclaim. Disconnect death created a dropped CMP150 through the
shared projectile initializer, which populated `obj->projectile` but not the
required reverse link `projectile->obj`. The exact world writer correctly
rejected that inconsistent authority graph. `netmsgSvcReconnectStateWrite()`
then returned one generic failure before its PREPARE receipt,
`netServerSendReconnectState()` collapsed that into `state_write_failed`, and
the caller selected `DISCONNECT_FILES`. The ordinary client therefore displayed
"Your files differ" even though its manifest had already matched and the fault
belonged to server-owned runtime state.

Current source repairs the shared ownership invariant, keeps the exact-state
validator fail closed, returns the first typed component plus client/prop or
objective subject, restores the packet cursor/error on failure, and separates
authority-local write/send retry policy from peer-content rejection. Production
automation passes. The first ordinary receipt on that source is still rejected,
but it proves those boundaries work: the prior owner failure is gone, the close
is retryable rather than a file mismatch, and the next authority defect is
named `world_prop_state` on prop 4. Sync allocation maps 1-2 to the players and
3-4 to post-start held weapons; disconnect death marks the departing player's
original held prop 4 deleting before it allocates a replacement drop. Current
source defines terminal non-regenerating deletion as authoritative absence,
retains deleting `CANREGEN` setup objects for their future GONE/regeneration
transition, emits omission counts and the first omitted ID in PREPARE, and
reports each included prop-state predicate with compact identity. The common
death-drop publisher also retires a newly allocated candidate if `objDrop()`
cannot commit, so no half-transitioned dynamic prop is announced. Replacement
automation passes on frozen product `b3df3ed0...` and verification
`38c558b2...`: isolated all/tests build, focused 623/13, full 61,423/1,163, and
native-source guard. The class remains open until the ordinary reconnect receipt
proves exact PREPARE/commit/resumed play; the rejected receipt is not upgraded.

**Audit:** enumerate every subwriter and every local validation branch in a
compound transaction. Require a stable typed failure result, an owner/substage
receipt before rollback, and an explicit policy mapping at the caller. Verify
that content mismatch is reachable only from validated peer-controlled content,
not from authority-local allocation, topology, inventory, or serialization
state.

`rg -n "Write\(|state_write_failed|write_failed|DISCONNECT_FILES|return 1;|rollback" port/src/net port/include/net tests`

---

## SP-61: A deep copy preserves a pointer vector but drops its packed sidecar

**Severity: CRITICAL - lookups read beyond the clone and silently disconnect
semantic ownership from render or gameplay state.**

Legacy representations can place metadata immediately after a pointer array
and expose only the pointer-array field in the owning struct. A clone that
allocates `count * sizeof(pointer)` and remaps the pointers appears complete but
silently drops the adjacent keys, offsets, flags, or indices consumed by lookup
code. The clone must copy the full packed representation as one transaction and
tests must pin both the layout calculation and the sidecar contents.

**Known instance (B-1097):** `modeldef.parts` points at `numparts` model-node
pointers followed immediately by `numparts` sorted `s16` part numbers.
`modelGetPart()` bisects that trailing table. `modeldefCloneForChr()` remapped
only the pointers into an allocation sized for the pointer vector, so every
modular character clone read beyond its allocation for hand, hat, and other
part lookups. `chrEquipWeapon()` set `attachedtomodel` before the right-hand
lookup returned no node, producing the live one-sided Cyclone attachment named
by the B-1096 reconnect diagnostic. This is an owning model-graph clone defect,
not permission for the serializer to accept incoherent pointers.

Current B-1097 source allocates the modeldef, remapped nodes, pointer vector,
and sorted `s16` sidecar as one stage transaction. The lookup rejects
null/empty tables and searches only `0..numparts-1`; the old upper bound of
`numparts` could inspect one key past even a complete table. Weapon equip also
resolves and requires its hand node before replacing an old held slot or
publishing either attachment pointer or prop ownership. Frozen automation
passes. The replacement runtime reached the valid named part and exposed the
separate mutable-rodata alias class SP-62; it does not invalidate SP-61's
lookup correction or permit reconnect to accept one-sided ownership.

**Audit:** for every clone or snapshot of a pointer-bearing legacy struct,
trace every consumer of the source field and calculate the complete backing
allocation, including data reached by pointer arithmetic. Require one checked
allocation for the full representation, remap pointers, copy every sidecar in
the same index domain, and reject hybrid clones when a required allocation
fails.

`rg -n "= \*src|Clone|clone|parts\[.*numparts|partnums|sizeof\(.*\*\)" src port tests`

---

## SP-62: A node-tree clone shares rodata that owns mutable topology

**Severity: CRITICAL - traversal leaves the private tree and indexes runtime
state in the wrong model-relative domain.**

A model node's rodata is not uniformly immutable geometry. Several variants
carry `rwdataindex`, and DISTANCE, TOGGLE, and REORDER records also carry node
pointers that traversal writes back into `node->child` or relation state. A
clone that remaps only `parent`/`next`/`prev`/`child` while sharing these rodata
records is not a private topology. The next index or initialization pass can
replace a cloned edge with a cached-source node, after which ancestor walking
uses the wrong model root and resolves an otherwise valid index against the
wrong rwdata base.

**Known instance (B-1098):** hierarchy-generated heads initialize DISTANCE and
TOGGLE targets in cached source rodata. `modeldefCloneForChr()` copied private
nodes but shared that rodata. `modelCalculateRwDataIndexes()` then assigned the
shared index and replaced the cloned node's child with the cached target.
`modelInitRwData()` followed that target out of the attached head; because the
cached target's parent chain did not contain the current body HEADSPOT,
`modelGetNodeRwData()` used body-relative storage and corrupted the HEADSPOT's
attached-head rwdata route. Once B-1097 made the sunglasses part lookup valid,
`body.c` observed the corrupted null route and faulted before networking.

**Validated class correction (B-1098):** classify every
field in every cloned pointee as immutable payload, deterministic index, or
topology reference. Private-copy each DISTANCE/TOGGLE/REORDER record that owns
topology, shallow-copy its immutable payload fields, and remap every embedded
node target into the same clone. Other indexed rodata may remain shared only
when the canonical traversal makes its index identical across clones; preflight
every such record before recalculation. This deliberately preserves the shared
non-relation root-rodata provenance used to recognize generated model clones.
Collect from relation targets rather than mutable visibility children, discard
foreign HEADSPOT children, publish nodes/relations/packed parts in one
allocation, and never fall back to the shared definition. Exercise generated
DISTANCE -> DL and TOGGLE -> DISTANCE -> DL subtrees plus both random and catalog
head selection, because flat child-only fixtures cannot expose the domain
escape. Frozen product `920a236d...` / client `2039d142...` and verifier
`96601f67...` / tests `c3987ec6...` pass isolated builds, focused 1,541/16,
full 63,731/1,165, and the native-source guard. The exact replacement receipt
`results-20260814T110358Z.json` reaches both complete stage loads and reconnect
commit without the former generated-head access violation; B-1099 owns its
separate post-commit gameplay failure.

`rg -n "rwdataindex|\.target|unk18|unk1c|node->child = rodata|Clone|clone" src/lib src/game port/src tests`

---

## SP-63: A new authority latch captures stale presentation state from the previous stage

**Severity: CRITICAL - a validated stage can commit while the receiver remains
permanently trapped in a presentation mode owned by the stage it replaced.**

An authority guard often blocks local state-machine exits while an authoritative
session or phase exists. If a new session latch is published before stale
receiver-local presentation from the previous lobby/stage is retired, the guard
can reinterpret that stale mode as part of the new authority lifetime. Later
stage-reset code then cannot establish its own fade, swirl, camera, or gameplay
mode, even though all per-player flags and scene layers appear ready.

**Known instance (B-1099):** timeout teardown returns the ordinary client to CI
and its intro cutscene. Reconnect `SVC_STAGE_START` validates the full packet and
freezes a new cutscene-authority match before asynchronous `playerReset()`.
`playerReset()` first requests `TICKMODE_GE_FADEIN` and later
`TICKMODE_MPSWIRL`; both are departures from the stale
`TICKMODE_CUTSCENE`. The general client guard correctly rejects such departures
whenever `netmsgCutsceneAuthorityHasMatch()` is true, because locally predicted
authored cutscenes must await reliable END. No authoritative cutscene is active
in this replay, so no later END arrives. The exact reconnect transaction commits
world and inventory, while the global tick mode remains 6 forever.

**Validated class correction (B-1099):** the fully validated
stage-start roster mints the new match latch before asynchronous load. At the
real `lvReset` boundary, a network client may retire prior-stage CUTSCENE only
when that latch exists and its authority tracker is still idle. The transition
uses the same setter under a scoped authoritative application flag to establish
GE_FADEIN; ordinary player reset then enters MP swirl and NORMAL. The shared
`HasMatch()` guard remains unchanged, an active new-match cutscene is excluded,
invalid candidates cannot reach the latch, and smoke gameplay readiness still
requires NORMAL. Focused contracts pin latch-before-publication,
boundary-before-player-reset, and the exact reconnect witness. Frozen product
`7437d77c...` / client `0f377e3e...` and verifier `d5fd25c9...` / tests
`ef5bdc72...` pass isolated builds, focused 1,746/19, full 63,936/1,168, and
the native-source guard. The exact `0f377e3e...` replacement then proves the
`6 -> 0` boundary, two NORMAL gameplay epochs, exact reconnect commit,
authority-accepted fire, scripted exits, and no leaks. Corrected retained-log
verification passes 98/98 without changing product source.

**Audit:** for every new stage/session/room authority latch, enumerate global
presentation state inherited from the previous owner. Verify the old owner is
retired through an explicit trusted boundary before the new guard becomes
effective, and that a rejected candidate cannot mutate presentation. Exercise a
transition that begins while the receiver is in each guarded mode, not only a
clean title-screen start.

`rg -n "HasMatch|IsActive|BeginMatch|playerSetTickMode|TICKMODE_|StageStart|stage-start" src/game port/src/net tests`

---

## SP-64: A verifier treats optional diagnostics and final cleanup as product failure

**Severity: HIGH - a production-successful run is rejected because the proof
contract observes the wrong semantic layer or ignores lifecycle phase.**

Diagnostic lines are optional instrumentation, not authoritative product
events. Requiring one can reject real gameplay when diagnostics are disabled,
renamed, budget-suppressed, or legitimately absent. Likewise, a process-wide
forbidden pattern cannot represent state that must be absent during retry but
must appear during final shutdown. Verification must prefer the lowest stable
production event and express lifecycle-sensitive state as an ordered sequence.

**Known instance (B-1100):** the B-1099 replacement ordinary client emitted
five real MagSec `COMBAT: SHOT_FIRED` events and the listen authority logged
`server_accepted=1`, yet the fixture required the optional
`LOG.WPN.DIAG: fire-input` trace. The same fixture forbade every
`credential_retained=0` teardown, even though final scripted shutdown must
retire a completed reconnect credential. Those two stale assertions caused all
three failures in an otherwise complete 92/95 receipt.

**Validated class correction (B-1100):** require
bounded production `COMBAT: SHOT_FIRED` evidence after the injected press,
retain the host's authoritative acceptance witness, and require exactly one
terminal credential retirement strictly after `SMOKE: result=scripted_exit`.
Static coverage rejects both the obsolete diagnostic dependency and the
process-wide terminal-cleanup prohibition. The original receipt remains
immutable. Corrected verifier `9f6c126b...` passes all 98 assertions against
the separately hashed retained aggregate/host/client logs, and compiled
`[b1100]` passes 61 assertions in 1 case. Product `7437d77c...` and client
`0f377e3e...` remain unchanged; no game was rerun.

**Audit:** for every required assertion, classify the witness as product state,
diagnostic instrumentation, or runner bookkeeping. Prefer product state. For
every forbidden state, enumerate the lifecycle phases in which it is invalid
and any phase in which it is required; encode phase changes as ordered
sequences plus bounded counts instead of whole-log absence.

`rg -n "LOG\\.|DIAG|forbidden_patterns|required_sequences|credential_retained|result=scripted_exit" tools/smoke-verify tests`

---

## SP-65: A streaming decoder borrows an unrelated endpoint and can stop making progress

**Severity: CRITICAL - valid or malformed public data can trap the main thread
without a crash, timeout boundary, or typed rejection.**

A decoder must own the exact byte span of the payload it consumes. Pointer
subtraction is a valid remaining-length calculation only when both pointers are
within the same owning array. Borrowing an adjacent header, cache slot, or
allocation endpoint can produce arbitrary positive, negative, or zero lengths.
Any loop that depends on replenishing a bit/byte count must also prove each
iteration consumes input or returns a typed failure; a zero-length refill may
never leave the loop condition unchanged.

**Known instance (B-1101):** optimized character animation decoding kept
`t3ptr8` as the end of the animation header and `t6ptr8` as the current frame
payload pointer. `modelasmReadFrameData` subtracted those unrelated pointers to
choose a refill size. A clean Combat Simulator run reached a layout where the
calculated chunk was zero, `gp` remained zero, and `while (v1 > gp)` never
reduced `v1`. The process stayed CPU-live after a real CMP150 shot while frame
and log progress stopped. Sixteen of sixteen native main-thread samples landed
inside that loop; caller candidates trace through character animation tick.

**Current correction, automation verified; production smoke rejected:** frame setup carries the table's
exact `bytesperframe`; a pure bounded big-endian reader validates every field;
and descriptor walking validates header span, format flags, field widths, frame
payload, and the optimized scratch-part capacity. Optimized transforms and the
generic F32/S32/camera/scale path now share that admission boundary, while the
root-motion and camera readers also locate and consume fields through it. A
zero-width field over a zero-byte payload is an explicit no-op; any nonzero null
or truncated read fails. The normal allocated `animnum=0` bind-pose state
bypasses decoding without a false error. Focused tests pin cross-byte ordering,
exact boundaries, 32-bit and zero-bit reads, 08-plus-camera ordering,
conflicting flags, invalid widths, truncation, overflow-safe offsets,
part-capacity enforcement, and removal of the unrelated subtraction.

The source-frozen unit passes focused 2,007 assertions in 33 cases, the full
64,681 assertions in 1,181 cases, and the native-source guard with zero product
or verifier manifest mismatches. When host capacity recovered, the exact frozen
replacement smoke reached player initialization and exposed B-1102 instead of
closing B-1101. The public schema-v5 animation rebuild had omitted per-part
flags from its runtime header, so the bounded decoder correctly rejected a
shifted descriptor. The intended fail-closed fallback was itself unsafe: it
nulled `model->anim` and entered a generic CHRINFO matrix path that
unconditionally dereferenced it. The immutable receipt is
`results-20260824T124625Z.json` at 16/56 with an access violation. B-1101's
bounded-progress correction remains automation-verified, but production closure
now depends on B-1102's framed-stream and explicit bind-pose repair.

**Audit:** for every stream, bit reader, archive walker, and packet decoder,
identify the object that owns the start and length. Reject mixed-allocation
pointer arithmetic. For each loop, name the monotonic cursor/count and prove
every branch advances it or returns. Exercise zero bytes, zero-bit fields, exact
end, one-bit truncation, maximum field width, and overflowed offsets.

`rg -n "while .*remaining|while .*bits|endptr -|ptrend -|ReadBits|read.*bits|bitoffset|bytelen" src port tools tests`

---

## SP-66: A transformed typed stream is published without consumer-contract validation

**Severity: CRITICAL - valid public source can compile successfully into a
misframed runtime stream that only fails when production consumes it.**

When a public-source adapter reconstructs a tagged or length-delimited stream,
the tag or discriminant is part of the payload contract, not metadata beside
it. Producer-side byte counts and field ordering must come from the same bounded
layout primitive used by consumers, and the completed stream must pass that
primitive before cache publication or activation. Static presence tests for the
adapter do not prove byte-level compatibility.

**Known instance (B-1102):** schema-v5 `pd_special_parts` stores `flags` beside a
verbatim `header_hex` field descriptor. `gltfBuildNativeFromSpecialParts`
summed and copied only `header_hex`; unlike the standard builder, it neither
reserved nor emitted one flags byte per part. The resulting stream retained the
expected 11-byte frame width but lost all descriptor boundaries. Runtime read a
field-header byte as a flags byte and rejected the valid public
`base:animation_two_gun_hold` source before the first frame could render.

The same receipt exposed a second error-path rule: a typed rejection fallback
must be independently executable from the rejected dependency. Clearing a live
animation pointer and then calling a generic matrix consumer that requires that
pointer converts a safe rejection into an access violation. Bind-pose rendering
must have an explicit dependency-free entry point and preserve live animation
ownership unchanged.

**Current correction, source-connected but unverified:** one pure
`anim_bits` contract now measures individual descriptors and exact framed
streams for both producers and consumers, including rejection of unconsumed
trailing descriptor bytes. Schema-v5 parsing requires the known version,
canonical bounded integer/boolean tokens, contiguous declared part topology,
exact row shapes and frame counts, full unsigned 32-bit raw values, and coherent
explicit zero-frame markers. Zero-frame private no-op streams preserve the
declared part count instead of collapsing topology to one descriptor. The
compiler reserves and writes every flags byte, validates each completed stream
and frame width before publication, and uses animation-only cache version 8 to
retire malformed v7 products without rebuilding unrelated mesh/model caches.
The optimized runtime decoder records the shared descriptor/header-bit end and
verifies its actual advancement for every part. Decode rejection re-enters the
same complete matrix traversal with an explicit no-animation input, so live
`model->anim` ownership remains unchanged, CHRINFO has a real bind-pose branch,
and any residual translation path uses an explicit null-safe bind scale.
Focused/full/native-source automation and the replacement production
smoke are still pending; B-1102 remains confirmed until both gates pass.

**Audit:** for every public-source compiler, cache adapter, packet builder, and
save migrator, enumerate all discriminants, lengths, and tails. Require the
producer to use or validate against the consumer's bounded parser before
publication. For each failure fallback, list every dependency deliberately
invalidated and prove the fallback does not call a consumer that requires it.

`rg -n "header_len|descriptor|flags|tag|discriminant|payload|fallback|cache.*write|cache.*activate" src port tools tests`

---

## SP-67: A prepared transaction remains writable through its ordinary settings channel

**Severity: CRITICAL - the serialized launch can differ from the roster and
configuration that passed preparation.**

Freezing one launch structure is insufficient when a later serializer still
reads identity, options, team, handicap, catalog selections, or other fields
from mutable live owners. Once a client participates in a prepared transaction,
an otherwise valid settings change must either create a new candidate version or
abort the exact transaction before publication. It must never patch live state
under an older accepted snapshot.

**Known instance (B-1103):** `netmsgClcSettingsRead` accepts and publishes a
valid `CLC_SETTINGS` packet while the source client is `CLSTATE_PREPARING`.
`netStageStartWriteAppendClient` later combines those mutable settings with the
older ready-gate config, producing either an unexpected launch rejection or a
packet that no longer represents the accepted roster.

**Production-verified correction:** validate the complete settings candidate first;
if the exact source client is still in the ready-gate expected mask, abort and
roll back that transaction, then recompute lobby policy and publish the new
settings in restored lobby state. Malformed settings remain fail-closed and do
not abort or mutate the transaction. Exact protocol-v58 settings rollback passes
43/43 and remains a release regression gate.

**Audit:** after every prepare/freeze boundary, enumerate every field read by
the eventual serializer and every message/menu/console path that can mutate its
owner. Require versioning or abort-before-publish for all of them, including the
countdown interval.

`rg -n "PREPARING|ready.?gate|prepared|settings|StageStartWrite|serialize" port/src/net src tests`

---

## SP-68: Per-entity rollback is mistaken for a multi-entity transaction

**Severity: CRITICAL - a later participant failure leaves earlier authoritative
runtime objects published.**

Candidate-first initialization for one object does not make a loop atomic. A
stage, match, or roster that initializes several players/entities must record
every successful commit and unwind them in reverse if any later participant
fails. Pointer-table reset or eventual arena release is not rollback when
disconnect, logging, replication, or UI recovery can observe the intermediate
state.

**Known instance (B-1104):** each `playerReset` and `playerSpawn` call reverses
its own late failure, but `lvReset` returns immediately when player N fails.
Players 0..N-1 can still own scheduler props, chr slots/models, held weapons,
fire slots, Eyespy props, room registrations, gun memory, and MP chr/config
bindings when network teardown begins; `playermgrReset` clears only pointers.

**Production-verified correction:** make both player loops one stage transaction,
track successful reset commits, and unwind every tracked player in reverse
through the chr/prop/gunmem owners before `lvReset` returns false. The canonical
chr teardown supports both model-less reset commits and full spawned bodies.
Exact protocol-v58 co-op, Counter-Op, and later-player rollback receipts pass
96/96, 98/98, and 60/60 respectively.

**Audit:** for each loop of fallible commits, list the resources published by
one successful iteration, identify the reverse owner for each, and prove a
failure at the last iteration leaves no earlier publication observable. Include
the zero/partial/full resource states of each element.

`rg -n "for .*player|for .*client|for .*participant|return false|ROLLBACK|propAllocate|chrRemove" src/game port/src/net tests`

---

## SP-69: Runtime identity is mistaken for replication readiness

**Severity: CRITICAL - a structurally identified entity can enter a wire
serializer before the state required by that payload exists.**

An entity's broad role marker is not proof that every runtime owner needed by a
network payload has committed. Enumerators that select by prop type or role
alone can observe legal transitional slots between identity publication and
model/transform readiness. Any serializer that then dereferences those absent
owners can crash the authority; any receiver that trusts its local counterpart
to be equally ready can repeat the same failure on valid wire input.

**Known instance (B-1104):** Counter-Op creates the correct two-player roster
and both chr bodies, but the initial full NPC resync counts every
`PROPTYPE_CHR && !aibot` slot. At least one such slot has no complete model.
`netmsgSvcNpcResyncWrite` unconditionally calls `chrGetInverseTheta`, which
reaches `modelGetChrRotY` and dereferences that missing owner. Frozen exact-client
RVA `0x55e542` and its caller chain prove this production path.

**Current correction:** one exported replication-ready NPC predicate requires
reciprocal identity/prop ownership, model, definition, and root, plus CHRINFO
rodata and live rwdata when orientation consumes them. Schedulers, counts,
checksums, incremental/full writers, and readers share it. Both u16 count fields
reject overflow before writing; full resync verifies its emitted count; shared
reliable-buffer appends restore their prior write/error boundary on failure;
only successful resync bits are consumed; and the receiver validates one whole
snapshot pass before rewinding and applying a second pass. Frozen automation and
the exact protocol-v58 Counter-Op receipt pass 98/98 without mismatch, resync
loop, rejection storm, crash, or leaked process.

**Audit:** for every replicated entity family, list the owners read by each
payload, compare those owners with its enumeration predicate, and verify the
receiver consumes the complete packet before rejecting a locally unready
target. For bounded count fields, prove overflow rejection precedes every cast;
for queued compound messages, prove partial bytes roll back and the request
remains pending. Pin scheduler/count/write/read predicate identity in focused
tests.

`rg -n "Count\(|count =|for .*Slots|Write\(|Read\(|GetInverseTheta|SetLookAngle|model->definition" port/src/net src/game tests`

---

## SP-70: Endpoint-local player slots are mistaken for canonical match identity

**Severity: CRITICAL - deterministic stage setup can remove or mutate different
world entities on each endpoint before replication starts.**

Runtime player indices are presentation-local in network Campaign/Counter-Op:
the local human occupies slot zero, so the same semantic role can have a
different `playernum` on authority and client. Any deterministic initialization,
spawn assignment, takeover selection, rollback ledger, or RNG-consuming loop
ordered by that local index is not lockstep. `CLSTATE_GAME` is likewise only a
transport/session state; it does not prove the remote endpoint has crossed its
post-load `CLC_STAGE_READY` boundary.

**Known instance (B-1104):** the Counter-Op authority initializes Bond then Anti
while the ordinary client initializes Anti then Bond. The spawn orchestrator
sorts by local `playernum`, assigning pool 3 then pool 13 to opposite semantic
roles. `playerSpawnAnti` consequently removes body/head 94/10 on the authority
but 110/45 on the client. The authority also broadcasts NPC checksums/resyncs
while the client is still loading, producing a 256-message missing-syncid 208
storm even though all four fixtures reach their terminal assertions.

**Current correction:** one immutable stage-player order derives from stable
authenticated client IDs and owns reset, spawn, semantic-role orchestration,
and the actual reverse commit ledger. A real stage replication barrier captures
peer-backed match clients and releases ordinary gameplay/NPC/resync traffic only
after every surviving relevant peer sends `CLC_STAGE_READY`; pending resync
ownership remains live while blocked. The direct-send propagation audit keeps
control-plane messages independent but makes GPU-swarm entity snapshots observe
the same central read-only blocker as buffered gameplay, spectator, and cutscene
publication. All seven exact protocol-v58 ordinary-client paths now pass,
including both semantic-role authority cases and both rollback paths.

**Audit:** whenever network code orders players, classify the key as canonical
or endpoint-local and compare authority/client semantic-role mappings. Whenever
code treats `CLSTATE_GAME` as readiness, identify the content-load acknowledgement
that makes referenced world identities safe. Production smokes must reject
checksum/resync storms, not merely require clean process exit.

`rg -n "playernum|client->id|antiplayernum|coopplayernum|stage_ready|CLSTATE_GAME|SpawnAnti|Orchestrate" src/game port/src/net tests tools/smoke-verify/tests`

---

## SP-71: Network state is published without an explicit stage or sample epoch

**Severity: CRITICAL - valid data from the wrong stage or sampling instant can
be mistaken for authoritative current-stage convergence.**

A selected lobby mode is prospective configuration, not proof that the current
world is that mode's active authoritative stage. Likewise, a checksum of fields
that travel on independent reliable and unreliable cadences is not comparable
unless both endpoints bind it to the same committed snapshot. A wait-mask of
zero cannot represent both "no stage transaction exists" and "this stage is
released" without admitting stale-world publication.

**Known instance (B-1104):** `netLobbyStartCommit` publishes co-op/Counter-Op
mode while the listen host and ordinary client still run the Carrington lobby.
The server begins nine-NPC broadcast before `SVC_STAGE_START`, and the client
receives a tick-600 checksum from that old stage. The later post-load barrier
works and its full resync applies, but periodic `netNpcSyncChecksum` still hashes
mutable position/damage/action values that are not one atomic wire snapshot, so
healthy endpoints repeatedly request unnecessary resyncs.

**Production-verified correction (protocol v58):** stage publication
is now an explicit inactive, waiting, release, or active phase. START/READY
share a nonzero epoch; authority load and exact remote-peer load are separate
latches; release emits one fresh baseline before ACTIVE traffic begins; and
control traffic remains independently sendable. The baseline owns dedicated
packet storage and consumes no pending bit until its complete room-scoped send
queues, preventing a late control-plane reset from erasing accepted ownership.
Each full NPC resync is
followed immediately on the same reliable transaction by a canonical
sync-ID-sorted digest of its exact wire fields checked against that applied
snapshot. The old
standalone periodic mutable-state checksum is removed. Frozen complete automation
and all seven exact-client protocol-v58 paths prove no pre-stage entity
publication, stale READY acceptance, snapshot mismatch, or resync loop. Keep
this pattern as a release regression gate.

**Audit:** for every gameplay producer, identify the stage epoch that authorizes
it and prove prospective lobby settings cannot activate the current world. For
every checksum/digest, list each field's transport and sample boundary. If those
boundaries differ, use stable identity or an atomic versioned snapshot. Runtime
gates must forbid pre-stage entity packets and false resync loops.

`rg -n "g_NetGameMode|StageReplication|SVC_.*SYNC|Checksum|g_NetMsg|g_NetMsgRel|stage_ready" src port tests tools/smoke-verify/tests`

---

## SP-72: Snapshot restoration replays the live event that originally produced the state

**Severity: CRITICAL - an exact authoritative snapshot can mutate already
restored ownership, scores, inventory, or lifecycle state a second time.**

An ordinary replication message often represents both a durable state bit and
the live event transition that produced it. Reusing that handler inside a
restart/reconnect snapshot is unsafe when the live path also scores, drops
items, retires entities, emits UI, raises cleanup work, or updates metrics. A
snapshot must restore the authority's current state without replaying historical
causes. Put the classification in a pure planner, share only state/presentation
application, and keep event side effects behind an explicit live-event entry
point.

**Known instance (B-1096):** after reconnect restored an exact world and both
inventories, snapshot `SVC_PLAYER_STATS` carried the authority's dead bit through
`playerDieByShooter`. That live path recorded a death, dropped inventory, raised
owner cleanup, and marked held weapon props deleting. The replayed cleanup then
reached a separately corrupted prop topology (SP-73),
and the client faulted in `objTickPlayer` at `propobj.c:12824` before restored
fire. A null guard would hide the ownership violation and retain corrupted
lifecycle state.

**Production-verified correction:** the reconnect layer purely plans live death
versus snapshot death. Both paths share dead
presentation, but snapshot restoration omits score/killfeed, drop, owner
cleanup, menu/HUD retirement, and lifetime metrics. The ordered transaction
emits one explicit zero-live-side-effects witness before commit. Behavioral and
static tests pin the planner and production dispatch. Exact ordinary-client
receipt `results-20260826T121959Z.json` passes 116/116 with exactly one snapshot
application reporting zero drops, score, owner cleanup, and other live side
effects before one exact commit, resumed authority-accepted fire, and clean
exits.

**Audit:** for every save load, reconnect, checkpoint, Theater seek, rollback,
or replay restore, list each handler that normally emits gameplay events. Split
durable state application from scoring, spawning/dropping, deletion, UI/audio,
cleanup queues, achievements, telemetry, and metrics. Require idempotent restore
tests and a runtime witness that historical side effects remain zero.

`rg -n "Snapshot|snapshot|Restore|restore|Replay|replay|playerDie|RecordDeath|DropAll|CleanupPending" src port tests`

---

## SP-73: One intrusive link pair is published into multiple ownership domains

**Severity: CRITICAL - a child can remain scheduler-visible after its object is
freed, corrupt iteration, or splice unrelated ownership chains together.**

`struct prop` uses one `next/prev` pair for the active/paused scheduler and for a
parent's child chain. A prop must therefore be off-list before reparenting, and
an attached prop must have zero scheduler membership. Constructors and wire
decoders must not publish map, scheduler, room, or parent ownership until the
complete candidate is valid; commit exactly one topology and validate it at
authority serialization and receiver transaction boundaries.

**Known instance (B-1096):** `netmsgSvcPropSpawnRead` activated or paused a new
dynamic prop, then called `propReparent` when the wire supplied a parent. The
second operation silently reused the scheduler links for child ownership. A
later reconnect snapshot replayed live death cleanup (SP-72), freed the object,
and exposed the still scheduler-visible prop in `objTickPlayer`.

**Production-verified correction:** reconnect begin removes sync-ID-zero local
held-weapon children before authoritative adoption. Dynamic
spawn rows are constructed off-list, then commit exactly one attached, active,
or paused placement; the sync-ID map publishes last. A bounded scheduler walk
rejects foreign pointers, cycles, duplicate membership, parented membership, or
missing parentless membership on authority write, receiver publication, and
reconnect end. The transaction emits `topology=exclusive`. Exact ordinary-client
receipt `results-20260826T121959Z.json` passes 116/116 with one exclusive-
topology witness, exact world/inventory restoration, resumed authority-accepted
fire, and clean exits. A propagation audit of all 18 `propReparent` sites found
the network spawn reader as the direct
scheduler-before-reparent violation; common AI, graph, pickup, and tick-operation
paths delist first or use fresh off-list constructors.

**Audit:** before every `propReparent`, prove `propDelist` or fresh off-list
construction. Before every scheduler insertion, prove `parent == NULL`. For
wire/save/Theater reconstruction, parse and validate before publication, publish
one ownership domain, and test failure cleanup plus idempotent reapplication.

`rg -n -C 8 "propReparent|propActivate|propPause|propDelist|propDetach|next|prev" src port tests`

---

## SP-74: Dependency publication and source ownership diverge across registration order

**Severity: HIGH - a valid self-contained archive can pass admission while its
runtime consumer silently reads duplicate native or legacy bytes.**

Two registration orders expose the same defect class. A nested typed dependency
can reuse a legacy row without hydrating its source, or it can create a valid
FileProvider row without filenum provenance and then be erased by a later
legacy coverage pass that deduplicates only by filenum. A dependency edge is
not source ownership. Typed publication must atomically bind provider plus
provenance while preserving stable runtime identity, and every later legacy
registrar must preserve an already-valid typed owner by `(type, catalog ID)`.
Byte-identical shared sources keep the first canonical owner and add only edges;
divergent typed sources and filenum mismatches fail closed.

**Current contract (production-verified regression gate):** one strict planner
validates the complete optional machine-manifest JSON root while treating the
public INI geometry as authoritative. Top-level and nested readers consume one
exported canonical key order (`model_file`, `geometry_file`, `model`,
`geometry`, `file_path`). One binder compares duplicate typed archives by
canonical SHA-256, preserves the first identical FileProvider, rejects
divergence and unsupported/live replacement, and permits source hydration of a
`LOADED` row only for the historical bundled/no-payload sentinel. Nested
publication snapshots reused rows and FileProvider intern state; every later
legacy model pass resolves by typed ID/effective FileProvider, reports source
errors to the boot asset-chain gate, and never overwrites a valid typed owner.

Exact client `395D48A6...` passes focused 132/14, full 67,152/1,223, the
native-source guard, two-archive verification, fixture validation, and 23/23
unchanged production-source hashes. Strict fresh-cache receipt
`results-20260826T152922Z.json` passes 54/54: the one raw `GskpistolZ.bin` read
is ordered inside pre-catalog extraction, then the nested typed source owns one
13-material compiler/activation/Bond-gun/render chain, 11 real Mauler shots,
and clean shutdown with no runtime native/ROM boundary or endscreen. All four
reviewed gameplay captures retain authored silver/green projection and V-009's
no-obstruction gate. Earlier binaries and rejected receipts remain diagnostic
history only.

**Known instance (B-1105):** with the duplicate global Mauler `.pdmesh` absent,
`base:mauler` registered embedded `base:model_skpistol_hi` as a FileProvider
row, but did not project `_meta/manifest.json`'s `FILE_GSKPISTOL` provenance.
`assetCatalogRegisterWeaponModelFiles` later found no owner for filenum 889,
called last-write-wins `assetCatalogRegister`, and replaced the typed row with
the legacy `GskpistolZ.bin` route. Luna xhigh found 152 nested weapon meshes,
113 unique model IDs, byte identity with all global counterparts, no source-
symbol mismatch, and 14 shared-ID families, so ownership must be generic and
order-independent.

**Audit:** for each nested typed family, trace the existing-row path from
content comparison through dependency publication and actual consumer handle.
Require the same manifest adapter as top-level scanning, trace later legacy
registrars by both filenum and catalog ID, preserve only stable runtime identity,
and prove the effective nested source path in an ordinary client with the
duplicate global source absent. Never treat a dependency-edge log as source-
ownership proof.

`rg -n "existing = 1|NESTED.REGISTER|registerComponent|catalogSetPrimary|source_filenum" port/src tests`

---

## SP-75: Project `bool` changes size with include order

**Severity: HIGH - a shared struct can have two layouts in one program, with
no compiler warning, so files silently read and write each other's members at
the wrong addresses.**

Project `bool` is a macro, `#define bool s32` (`src/include/types.h:14`, again
in `src/include/data.h`, restored by `port/include/net/netenet.h` after ENet).
`<stdbool.h>` in C defines `bool` as the 1-byte `_Bool`. A C file that includes
`<stdbool.h>` after `types.h`, directly or through a project header, gets
1-byte `bool` for the rest of the file. Every project struct with `bool`
members parsed after that point is laid out differently there than in files
where `bool` is still `s32`. C++ files are not affected the same way: in C++
`bool` is a keyword (one byte), and `types.h`, when a C++ file includes it,
turns it into `s32` too. So a `bool` declaration that C and C++ files share
has one width only if every C++ includer has the types.h macro in force first;
a header that cannot promise that spells `s32`, or uses the scoped one-byte
pattern below.

**Known instance (B-1120, 2026-11-14):** `port/src/main.c` and
`port/src/smoke_harness.c` include `native_write_policy.h` (which includes
`<stdbool.h>`) just before `net/net.h`. They laid `struct netclient` out with
twelve 1-byte flags; `net.c`, `netmsg.c` and the rest used 4-byte ones, so
`&g_NetClients[1]` itself differed between files. Effect: the host autostart
hook never saw the joined client and every two-process match smoke failed;
the smoke harness's reconnect and timeout events addressed the wrong client.
Earlier instance (October 10, recorded in the Finish Tracker):
`net_player_move_wire.h` included `<stdbool.h>` and changed the size of four
`net.c` flags and one function; it now includes `types.h` instead.

**Second finding (B-1121, 2026-11-14):** the full audit found 28 C files that
flipped (sources: those four headers, seven C files with their own
`#include <stdbool.h>`, the fast3d `gfx_api.h`, and ENet, which
`net/netenet.h` undoes at once) and 291 header declarations seen with both
widths, including a real return-width mismatch between `netmsg.c` and
`net_manifest_type.c`.

**Root fix (B-1121, week-pd/multiplayer):** no project header or C file
includes `<stdbool.h>`.
- A header included by both C and C++ files spells boolean results and flags
  `s32` (0 or 1), never `bool`: `native_write_policy.h`,
  `net/net_manifest_type.h`, `bondgun_cache.h`, the net.h shared structs.
- A C file that needs `bool`, `true` and `false` without the game's
  precompiled header (test targets build without it) includes
  `port/include/pd_bool.h`, which defines the project `s32` bool in C only.
- A header whose structs the C++ renderer shares with C files and that must
  stay one byte in both languages (the five fast3d `gfx_*.h`) uses a scoped
  bool and restores the includer's meaning:
  `#pragma push_macro("bool")`, `#undef bool`, `#ifndef __cplusplus`
  `#define bool _Bool` `#endif`, declarations, `#pragma pop_macro("bool")`.
- Vendored code may include `<stdbool.h>` only behind a wrapper that restores
  `s32` before any project code (`net/netenet.h` for ENet).
Do not fix a symptom by reordering includes. When a file's own `bool` changes
width, review conversions into it: `_Bool b = 0.5f` is 1, `s32 b = 0.5f` is 0;
a value compared with `==`/`!=` must be 0 or 1. GCC 15 rejects pointer-to-int
conversion, so `bool b = ptr` fails to compile instead of truncating.

**Third finding (B-1123, 2026-11-14):** C++ files that avoid `types.h` and
declare game functions themselves see `bool` as the one-byte keyword:
`extern bool g_PlayersWithControl[]` in `pdgui_menu_mainmenu.cpp` indexed a
4-byte array with a 1-byte stride. A C++ file that declares a C function or
global itself spells the C type (`s32`), never `bool`.

**Guards:** `tools/bool_layout_audit.py` (CI step "Check bool layout", targets
`pd` and `pd-tests`) preprocesses every translation unit and fails on a header
declaration seen with two widths, a named function or global declared or
defined with two widths anywhere (header prototypes, definitions and local
`extern` declarations in source files, C and C++), a C file whose project code
runs with a flipped bool, or a push_macro scope that a header never pops; its
self-test is `tools/tests/test_bool_layout_audit.py`. `tests/test_bool_layout_static.cpp`
fails if the net.h shared structs gain a `bool` member, if any project header
or C file includes `<stdbool.h>`, if a gfx header loses its scope, or if the
`s32` headers say `bool` again.

**Search commands:**
- Includes: `git grep -n -E "^\s*#\s*include\s*[<\"]stdbool\.h[>\"]" -- "*.h" "*.c"`
- Whole program: `python tools/bool_layout_audit.py --compile-commands
  <build>/compile_commands.json --target pd` (and `--target pd-tests`).
- Proof for one file: preprocess it with its exact flags from
  `compile_commands.json` plus `-E -dD` and check which `#define bool` is in
  force where the struct is defined. GCC prints a push_macro as a blank line
  and a pop_macro as `#undef bool` without the restored definition, so read the
  pragmas from the source.

---

## SP-76: A reader takes a present but unreadable field for an absent one

**Severity: HIGH - the value silently becomes zero and nothing downstream can
tell, so a whole mechanic can be dead while every load line looks normal.**

Public source readers treat most fields as optional: `if (read(...)) { use }`,
with a zeroed default otherwise. That is right for a field that is absent. It
is wrong when the field is present and the read fails, because then the
author's data is dropped without a word. The failure mode needs no malformed
file: it is enough that the writer and the reader disagree about the shape of a
value they both consider valid.

**Known instance (B-1114, 2026-11-14):** the extractor writes a room light's
quad as four nested corners, `"bbox": [[x, y, z], [x, y, z], ...]`. The loader
added for B-943 read it with `s_jsonReadIntArrayInRange`, which expects a flat
array and returns 0 at the first inner `[`. Every one of the 3,398 campaign
lights kept an all-zero quad, so no light could be shot out. B-943 was accepted
on source pins that checked the member was written and a loader existed. The
defect was found by a cross-check between two independent tables (a light quad
must lie inside its room's authored box), not by any test of the reader.

**Rules:**

- A reader that makes a field optional must tell "absent" from "present but
  unreadable", and report the second (see `without_quad` in
  `scenarioSourceLoadRoomLightsForStage`).
- When an extractor and a loader are written for the same member, check one
  real extracted value end to end at runtime. A pin that the writer and the
  reader both exist proves nothing about their agreement.
- Prefer a load-time consistency check between independent authored tables over
  a count of rows read. `outside_room=` is the model: it cannot pass by
  accident.

**Audit:** for every optional read in a public source loader, ask what the
consumer does with the zero default and whether a present field can fail to
parse. Start with nested values and values that changed shape in the extractor.

`rg -n "if \(s_jsonRead\w+\(obj_open, obj_close" port/src/scenario_source_runtime.c`

`rg -n "\[%d, %d, %d\]|\[%\.6f, %\.6f, %\.6f\]" port/src/romextract_pdarena.c`

---

## SP-77: The scenario-source path stands in a derived value for an authored table

**Severity: HIGH - play differs from the original in every extracted stage, in
ways that look like content quirks rather than defects.**

The legacy stage loader (`bgBuildTables` in `src/game/bg.c`) fills its room
tables from the background file. The scenario-source loader
(`bgBuildScenarioSourceTables`) rebuilds the same tables from public source.
Where public source did not carry a table, the rebuild used something
derivable (or a constant), and the game ran on with it.

**Known instances (2026-11-14):**

- Room boxes derived from collision triangles instead of the authored boxes
  (B-1113): pads outside the tighter boxes got no room and 44 campaign props
  were never created, among them an objective object.
- Room origins left at zero while light quads are stored relative to them
  (B-1114): every light sat away from its fixture.
- Room brightness range set to the constant 255/255 instead of the authored
  128/255 (B-1115): a darkened room stayed about twice as bright.

All three now come from the scenario member `rooms.json`.

**Still open, same class (found by reading, 2026-11-14, not yet measured):**

- `g_BgCommands = NULL`: the background file's command list is not in public
  source (what it controls on this path is not yet measured).
- `g_BgStanThings = NULL`.
- Light glares and the sparks of a broken light: `src/game/artifact.c` needs
  the room graphics data (`gfxdata`) this path does not load, and returns
  early without it.
- Sky rooms: `roomPopulateMtx` (`src/game/room.c`) draws a handful of rooms
  relative to the camera using the room origin, which is zero on this path.

**Rule:** when the scenario-source loader has no source for a table the legacy
loader fills, that is a missing public member, not a default. Add the member to
the extractor, and until it exists log the stand-in at stage load so the gap is
visible in every run.

**Audit:** walk `bgBuildTables` table by table and find the line in
`bgBuildScenarioSourceTables` that fills the same field; anything filled with a
constant, a zero or a value computed from other source is a candidate. Do the
same for the setup and pad loaders.

`rg -n "g_Bg[A-Za-z]+ = NULL;|= 0xff;|memset\(g_Bg" src/game/bg.c`

---

## SP-78: One copy of a path rule is fixed, its sibling copies are not

**Severity: HIGH - isolation and save bugs that a fix "already closed" keep
happening through the copies nobody touched.**

The client spells some path rules in several places: the list of `mods/`
roots, and the name a config save is written to. When one copy was fixed the
others kept the old rule, and because the fixed copy is the one that gets
tested, the bug looks closed.

**Known instances (found 2026-11-14, B-1160):**

- B-1031 made an explicit `--moddir` the only mod root in modmgr. The font
  loader, both theme scanners and the migration bench kept their own copies
  of the discovery list and still walked `$E/../mods`, `./mods`, `$E/mods`
  and the base `mods`. Fixed: one list, `fsModRoots` in `port/src/fs.c`.
- The main menu's settings save was moved to `configSave(CONFIG_PATH)`, and
  a test forbids the bare name there; thirteen other menu saves kept
  `configSave("pd.ini")`, which resolves like a read and wrote the base
  folder. Fixed: every caller names `CONFIG_PATH`, `configSave` refuses a
  bare name, and a static test scans all of `port/` and `src/`.

**Rule:** a path rule lives in one function, and a test scans the whole tree
for a second spelling of it, not only the file that was fixed.

**Search:**
- `rg -n 'configSave\("[^$]' port src`
- `rg -n '"\$E/\.\./mods"|"\$E/\.\./" MODMGR_MODS_DIR' port src` (only `port/src/fs.c`)
- `rg -n 'fsFullPath\("[a-z]' port src`, then check each hit is a read, not a write
- `rg -n 'fsFileOpenWrite\(' port src`, then check each name has a `$` root or is absolute

---

## SP-79: A graph helper returns the failure logger's "handled" as "satisfied"

**Severity: MEDIUM - refusals that are logged but not honoured; mostly benign
today, occasionally a NULL or out-of-range value carried forward.**

In `port/src/scenario_source_runtime.c` the AI graph twins answer 1 for
"handled" (skip the native handler) and 0 for "not mine". The failure logger
`s_aiGraphRuntimeFailure` therefore returns 1, so a twin can write
`return s_aiGraphRuntimeFailure(...)` to refuse. The `s_aiGraphRequire*` and
`s_aiGraphResolve*` helpers use the opposite sense: 1 means "the requirement
holds, go on". Wherever such a helper writes `return
s_aiGraphRuntimeFailure(...)`, the warning is logged and then the caller is
told the requirement holds.

**Known instances (found 2026-11-14 by the pad-check audit, counted the same
day):** 73 such returns in 32 helpers, among them
`s_aiGraphRequireRuntimeCharacterPointer` (a NULL chr or a chr with no prop
"passes"), `s_aiGraphRequireRuntimePathPointer`, the non-loop branches of
`s_aiGraphRequireRuntimePathPads`, `s_aiGraphRequireRuntimeSceneRooms`, the
vehicle, chopper, player and gun-prop pointer helpers, and the `*Node`
helpers (a missing graph node "passes"). `s_aiGraphRequireRuntimePad` and
`s_aiGraphRequireRuntimePadTable` are correct: they log, then return 0.

**Why it is not fixed in one sweep:** making a helper return 0 changes what
each caller does next (some fall through to native, some refuse), and some
callers depend on today's behaviour (a background script's blank chr has no
prop and "passes" the character check, which matches what native does). Each
helper needs its callers read before its failure sense changes.

**Status (2026-11-15, B-1167): fixed.** All 90 such returns in 40 helpers
(the 73 counted above, 16 in the seven `s_ai*GraphReady` helpers and one in
`s_aiGraphRegisterSourceSpawnedChr`) now log and return 0, or -1 for the
three-way `s_aiGraphRequireTargetMovementNode`; the ten `if (!x)` callers of
the three -1 node helpers read -1 as a refusal. Two branches keep going on
purpose and say why: the character pointer's "not source-derived" (co-op AI
buddies and tiny-mode extras have no provenance yet) and the spawn registry's
full table (the chr is already spawned; falling to native would spawn it
twice). `tests/test_graph_helper_failures_static.cpp` fails on any new
`return s_aiGraphRuntimeFailure(` inside a requirement helper.

**Rule:** inside a requirement helper, log with `s_aiGraphRuntimeFailure(...)`
and then `return 0;` on its own line; reserve `return
s_aiGraphRuntimeFailure(...)` for the twins themselves.

**Search:**
- `awk` over the file tracking the enclosing function, counting `return
  s_aiGraphRuntimeFailure(` inside `s_aiGraphRequire*` / `s_aiGraphResolve*`
  (the count above).
- `rg -n 'return s_aiGraphRuntimeFailure\(' port/src/scenario_source_runtime.c`

---

## SP-86: An export version moves but the folder fast cache that skips it does not

**Severity: HIGH when it hits - the new extractor runs, reports success, and
writes nothing, so a base made with it silently keeps the old output.**

An extractor that stamps each archive with an export version and also keeps a
whole-folder fast cache (`romExtractPdFastCacheCanSkip` with its own
`*_FAST_CACHE_KIND`) checks the folder stamp first. If a change bumps only the
per-archive version, the folder skip still matches (same kind, same file
count, sizes and times) and the per-archive check never runs.

**Known instances:**

- B-1154 follow-up (fixed 2026-11-16, `week-pd/core`): the mesh export version
  gained `_texscale_b1154` but `ROMEXTRACT_PDMESH_FAST_CACHE_KIND` did not, so
  the first base 7 kept all 733 meshes at render schema v3 (its mesh census
  showed no texture rows). The kind is now built from the export version
  label, so they cannot drift again.
- Checked, not affected: arenas and scenarios share one skip that needs both
  kinds to match, so a scenario kind bump rewrites the arenas too; bodies,
  heads, characters and weapons have no folder skip and compare each nested
  mesh copy with the current mesh archive.

- B-1250 (fixed 2026-11-16, `week-pd/core`), the other direction: the
  pdscenario kind grew to 525 chars while the stamp readers read 511, so the
  stored kind never matched and every launch re-extracted every scenario. The
  readers now hold 2047 chars and the writer refuses a longer kind loudly.

**Rule:** a folder fast-cache kind must change whenever anything its skip
vouches for changes, and it must round-trip through its stamp unchanged. Build the kind from the version macros it depends on
instead of keeping two strings in step by hand, and after any extractor
change, check the census of what a fresh base actually holds, not just that
the run passed.

**Search:**
- `rg -n 'FAST_CACHE_KIND|EXPORT_VERSION' port/src/romextract_*.c`, then for
  each extractor with both, check that the kind changes with the version.

## How to Use

- Before starting any work that touches arrays, memory allocation, or stage indexing, scan this file for relevant patterns.
- When fixing a one-off bug, check if it's an instance of a pattern here. If so, do a propagation check (§3.6) on all files listed under that pattern.
- When discovering a new pattern class, add it here with severity, root cause, known sites, and search command.

## Specialized source admission overwritten by a later generic scan

Found under T-ASSETS-048 on2026-09-08: a valid public descriptor can feed the specialized registrar and pass focused tests, yet a later metadata pass replaces its provider/path from stale private metadata. Audit the entire boot sequence, not just each registrar independently. Compare specialized loaderWalkerScan calls with s_MetaFamilies; verify each public source has one registration owner. Required runtime proof runs the later scan and then loads selected geometry, with divergent private echoes and metadata-free public files. Body/head duplicate registration is removed; Arena remains a recorded separate source-authority gap. Search: rg -n 'loaderWalkerScan|s_MetaFamilies|catalogSetPrimary' port/src/loader_walker*.c (use rg -g 'loader_walker*.c' port/src on PowerShell).

### September29 retained equipment propagation - T-MODDING-002

Custom weapon identity now follows exact equipped generation pins; reset and
rollback never overwrite live consumer identities or restore operational
counts. Real allocator and deferred source tests cover MP reservation teardown,
atomic conflicting rollback, identity movement and final reuse. Queue retained
public modeldef directly before mutable source checks; file identity alone
cannot distinguish multiple versions of one ID. Native143/4754 and installed
39/39 pass. Body/head retained identity audit and v2 ordinary hand wiring remain
open. Search: rg -n 'ResetCustom.*Slots|RestoreCustom.*Slots' port/src; inspect
every retained native pointer/copy, its exact identity pin, snapshot semantics,
request publication and final cleanup. See equipment-native-lifetime audit.

### October1 public dependency discovery across source syntaxes - T-TOOLING-009

Descriptor-only scanning first missed material dependencies; broadening the file
list still missed standard space-separated MTL commands because parsing assumed
key/value syntax. Derive dependency edges from each public format's syntax and
prove the resulting inventory participates in a member-verified native parent/
leaf join. A dependency's existence or standalone load cannot prove the parent
bound that generation. Missing/incomplete discovery remains pending. Standard
MTL parsing and the joined ledger regression pass; native texture extension is
unbuilt/unrun. Search: rg -n 'material_dependencies|dependency_scan|dependency_bindings|selected_coverage' tools/asset-inspection-*.py.

## SP-80: Scenario setup assumes the arena brings its own placement data

**Severity: HIGH - a scenario can crash at match start or start without the
objects it is about, and a lifecycle-only smoke still passes.**

Multiplayer scenarios place their objects on positions the arena's setup data
provides: the intro command list (`INTROCMD_CASE`, `INTROCMD_CASERESPAWN`,
`INTROCMD_HILL`, read in `scenarioReset`) and setup objects (multiplayer ammo
crates and `WEAPON_MPLOCATION` weapons). The original code assumes every arena
has them, because every shipped arena did. Arenas that lack them (extracted
arenas today, mod arenas, solo maps used as arenas) reach code that divides by
the count, loops until it finds an unused entry, or places an object on pad -1
(`setupCreateObject` then creates it off-world).

**Known instances (2026-11-15):** B-1125 (Hacker Central: divide by zero in
`htmInitProps`, endless selection loop with too few pads, uplink created
off-world and re-created every tick); B-1126 (Capture the Case cases off-world,
King of the Hill fixed on the room of pad 0, Hold the Briefcase always on
pad 0). The data gap: the scenario source carries only `INTROCMD_SPAWN` rows
(`spawns.json`), so no arena has case or hill pads, and 16 of 18 arenas have no
setup objects.
- B-1253 (fixed 2026-11-16, `week-pd/core`), the other direction: a bridge for
  data the old arena setups lacked kept running after the source changed. The
  SP-in-MP overlay (`setup.c`, B-228) brought a campaign stage's lifts into its
  arena because the ROM's arena setups had none; on scenario source the arena
  loads the campaign scenario itself, so the main pass already had them and the
  overlay created every lift a second time (Infiltration 4 -> 8).

**Rules:**
- Never `% count` or index a bank without checking the count first; never
  `do ... while` an "unused entry" search without bounding the number of picks
  by the usable entries.
- Never place an object on a pad < 0 or on a pad with no room; check the pad,
  or use the result of `scenarioCreateObj`, which returns NULL in that case.
- When a scenario's own positions are too few, fill the bank with walkable
  pads through `scenarioFallbackPads` (`src/game/mplayer/scenarios.c`,
  picker `port/src/scenario_pad_fallback.c`) and log it, instead of falling
  back to pad 0 or refusing silently. Whether a fallback changes how a mode
  plays on such arenas is Mike's call per mode (approved for Hacker Central).
- A scenario smoke asserts that its objects were placed on real pads, not only
  that the match started and ended.
- When what an arena loads changes (ROM setup blob -> scenario source), re-check
  every bridge that fills in what the old data lacked: once the new data
  carries it, the bridge duplicates it. A smoke counts the objects the bridge
  is about.

**Search:**
- `grep -n "% *g_ScenarioData\|% *data->\|% *count\b" src/game/mplayer/scenarios/*.inc`
- `grep -n "homepad\|hillpads\|padnums\[\|tokenpad\|base.pad = " src/game/mplayer/scenarios/*.inc`
- `grep -n "while (padnum <= 0)\|while (.*== previndex)" src/game/mplayer/scenarios/*.inc`

**Audit checklist:** for each scenario, its init and respawn paths with (a) no
intro case or hill pads, (b) no multiplayer ammo crates, (c) both. All four are
fixed: Hacker Central (B-1125, 86de8e08), Capture the Case, King of the Hill
and Hold the Briefcase (B-1126, dbc660f1); local smokes on Complex assert
placement on real pads.

## SP-81: A client read handler rejects a message by stopping the packet

**Severity: MEDIUM - reliable messages are lost without a trace beyond one
warning, and whichever message follows the rejected one decides the damage.**

The client receive loop (`netClientEvReceive`, `port/src/net/net.c`) reads
messages while `rc == 0`. A handler that returns nonzero ends the packet, so
every later message in it is dropped. That is right for malformed bytes,
because the reader can no longer tell where the next message starts. But
many handlers also return 1 for a well-formed message they decline: a prop or
character this client does not have, or a message that arrives before the
client is in the game. A rejection that should cost one message costs the
rest of the packet.

**Known instances (2026-11-15):** B-1131. There are 14 handlers with
`return src->error ? src->error : 1;` in `netmsg.c`, plus plain `return 1;`
rejections. Before B-1129, the likely live trigger was `SVC_PROP_PICKUP` for
a weapon a simulant dropped. A client made that weapon itself, with sync id
0, so the host's sync id never resolved.

**Rules:**
- A read handler reads its whole message before it decides anything.
- After a complete read, a declined message returns 0 and logs why. Only
  `src->error`, or a message whose length cannot be known, stops the packet.
- A handler that returns early must not have read part of its message.
  `netmsgSvcPropSpawnRead` does both today: it returns 1 before reading its
  weapon fields, so it cannot be switched to return 0 as it is.

- Worse than a stop: a return of 0 before the last read leaves the tail to be
  read as the next messages (a desync). Four in-match readers did that
  (B-1131).
- A writer that sends a field only on a condition the reader cannot see
  (for example, a pointer being non-NULL) breaks the reader the same way.
  The condition has to be on the wire.
- The reconnect, auth and vehicle state readers fail closed on purpose: a
  nonzero return there triggers the dispatcher's terminal disconnect. Do not
  switch them to 0.
- A count on the wire is the number of rows that follow. A writer that writes
  a count and then skips rows (`continue`, `break`), or writes a short
  placeholder row the reader cannot tell apart, breaks the reader the same
  way. Count first, then write exactly those rows.
- A return inside a row loop is a return before the last read, even when it
  sits after the row's own reads.
- Several messages can share one packet on purpose: the resync transaction
  (`netServerPublishPendingResyncs`, `net.c`) sends character, prop, score
  and NPC resyncs, the NPC digest, stage flags, objective statuses and the
  scenario baseline together, so one short row garbles all that follow it.

**Search:**
- `grep -n "src->error ? src->error : 1\|return 1;" port/src/net/netmsg.c`
- For each hit, check that every field of the message was read before that
  return.
- `audit_readers.py` (MULTIPLAYER session scratchpad, 2026-11-15) lists every
  `netmsg*Read` with a return before its last buffer read: 27 readers, most
  on purpose or on a broken buffer. It does not see returns inside row loops;
  list those by looking for `return` at loop depth in each reader.
- `audit_count_writers.py` (same scratchpad) lists every `netmsg*Write` that
  writes a count and then has a `continue` or `break` in its row loop.

**Audit checklist:** for every client and server read handler, (a) does it
read the full message before any return, and (b) does a semantic rejection
return 0? Done: `netmsgSvcBotPickupRead` (B-1129); `netmsgSvcPlayerStatsRead`,
`netmsgSvcPropMoveRead` and `netmsgSvcChrDisarmRead` (with its writer) read
first; `netmsgSvcPropPickupRead`, `netmsgSvcPropUseRead` and
`netmsgSvcChrDamageRead` skip with 0 (B-1131); `netmsgSvcPropSpawnRead` parses
its whole row before it decides or allocates, skips outside a reconnect and
fails closed inside one, and its writer puts the projectile condition on the
wire (B-1131, second pass). Third pass (B-1131): the character resync writer
counts and writes whole rows only, the character and prop resync readers read
every row before skipping, the score writer counts the rows it sends, the NPC
resync declines an incomplete snapshot with 0, and `CLC_ADMIN` consumes a
rejected command's arguments and stops on an unknown subcode. Checked and left
on purpose: the catalog and distribution readers (reconnect control, fail
closed), `CLC_MANIFEST_STATUS` (always its own packet; security pin),
`CLC_BOT_MOVE` (security pin; stops without desync), the room list, room
settings and playlist readers. The only dispatched readers outside `netmsg.c`
are the two move readers in `net_player_move_ingress.c`; both read their whole
message first. Nothing open.

## SP-82: The N64 player count is written as a literal 4

**Severity: MEDIUM - wrong participant, silently; the score, target or team
goes to someone else or to nobody.**

The N64 game had four players, and the decompiled code often writes that
count as the number 4 instead of `MAX_PLAYERS`. The port has 8 players, and
its mpchrnums put players at 0 to `MAX_PLAYERS - 1` with simulants after
them (`MPCHR` in `constants.h`). Code that still splits players from
simulants at 4, or offsets simulants by 4, reads the wrong config: simulant 0
becomes simulant 4, and players 5 to 8 become simulants 0 to 3. Nothing
crashes, so the result just looks like "simulants score nothing" or "player 6
has someone else's points". Related to SP-1 (player-sized arrays indexed by
simulant numbers) and SP-2 (modulo aliasing), but the cause is a literal, not
an array size.

**Known instances (2026-11-15):** B-1136, `func0f18d0e8` (`mplayer.c`):
Pop a Cap and Hacker Central scores, JudgeSim targeting and Capture the Case
team bits. Fixed.

**Not fixed (lower impact, recorded 2026-11-15):**
- `mplayer.c` default player bodies: players 4 to 7 get player 0's default
  body (cosmetic).
- `mplayer.c` lock winner and loser (`mpGetPlayerRankings`, `chrnums[j] < 4`):
  only players 1 to 4 count for the "last winner" and "last loser" locks.
- `mplayer.c` local awards (`numplayers >= 4` chains): built for up to four
  local players.

**Fixed later the same day:** the team counts over 4 bits (B-1138): the
match-start orchestrator now has one sector per active team up to
`MAX_TEAMS`, and the respawn helper `playerMpResolveTeamSpawnParams` counts
every team bit and passes the team's place among the active teams as its
sector.

**Rules:**
- The player/simulant boundary in an mpchrnum is `MAX_PLAYERS`, and a
  simulant's config is `g_BotConfigsArray[mpchrnum - MAX_PLAYERS]`. Use
  `MPCHR()` where it fits.
- A loop over teams runs to `MAX_TEAMS`, or to `scenarioGetMaxTeams()` when
  the scenario limits them.

**Search:**
- `grep -rn "\- 4\]\|< 4)\|>= 4)\|i < 4; i++" src/game/mplayer src/game/bot*.c src/game/mplayer/scenarios`
- Most hits are 4-vertex or 4-corner loops; read each and keep only those
  that index players, participants or teams.

**Audit checklist:** for each hit, (a) is the 4 a player count, a
player/simulant boundary or a team count, and (b) would 8 players or 8 teams
reach past it? Done: `func0f18d0e8` (B-1136) and the two team spawn paths
(B-1138). Open: the three lower-impact `mplayer.c` sites above.

## SP-84: A process clears its own player link while its stage is still drawn

**Severity: HIGH - the process's whole global tick stops, and the world can
keep running from the render pass.**

Since B-1090, `mainTick` (`port/src/pdmain.c`) runs `lvTick`, and with it
`menuTick`, `scenarioTick`, music and the pause gate, only when
`playermgrRestoreLocalPlayerContext()` finds this process's player through
`g_NetLocalClient->player`. `lvRender`'s `propsTickPlayer` and the per-player
`lvTickPlayer` run either way, with the last `g_Vars.lvupdate240` that
`lvTick` computed. So a process that clears its own link while its stage is
still drawn loses its menu tick and pause gate, and when its last `lvTick`
was not paused, chrs, simulants and objects keep simulating at that rate.
The log shows `GLOBAL.TICK: deferred; receiver-local player context is not
committed` once.

**Known instances (2026-11-15):** B-1190 part 2 (a client's MP stage end,
`netmsgSvcStageEndRead`: its results waited for a disconnect) and B-1195 (the
listen host's `netmsgSvcStageEndCommit`: simulant AI ran under the results
and divided by zero after Continue). Both fixed: a process keeps its own link
at the stage end, and the next stage's player allocation replaces it.

**Safe only by order (recorded 2026-11-15):** `pdguiEndscreenExitToRoom`
clears every co-op and counter-op link, the local one too, so a networked
mission's Return to Room defers the global tick until the next stage. The
last `lvTick` ran under the paused results, so the kept update is 0 and the
mission stays frozen; a change that unpauses before that clear would run the
mission behind the Room.
That happened on 2026-11-16 through a disconnect (B-1201): with `g_NetMode`
reset, the client's global tick ran again, unpaused, and its suppression
of co-op NPC AI no longer applied. A disconnect now leaves such a stage
for CI and freezes it until CI loads.

**Not checked:** a spectator client (`CLFLAG_SPECTATOR`) is never given a
player; if it loads the stage, its global tick is deferred all match.

**When fixing an instance:** the deferred tick also hid code that runs
again once the link is kept. Restoring the host's tick (B-1195) brought back
`menuTick`'s offline results close, which loaded CI as a networked host and
rolled its player setup back to the title (B-1197). After restoring a
process's tick, check what `lvTick` and `menuTick` do in that state.

**Rules:**
- Do not clear `g_NetLocalClient->player` while this process's stage is still
  loaded and drawn; the next player allocation replaces it.
- Code that retires match state while the stage stays loaded (for example
  `mpClearRuntimeChrState` in the results exit, B-368) leaves whatever still
  ticks inert, not only paused (the `botTick` gate of B-1195).

**Search:**
- `grep -rn "player = NULL" port/src/net port/fast3d`, then check each loop
  over `g_NetClients` for an `ncl == g_NetLocalClient` skip.
- `grep -n "GLOBAL.TICK: deferred" pd-host.log pd-client.log`: one per stage
  start is normal; one after a stage end or a results exit is this pattern.

**Audit checklist:** for each hit, (a) can it clear this process's own link,
and (b) is the stage still loaded and drawn afterwards? Done: both MP stage
end paths (B-1190, B-1195). Open: the co-op and counter-op results exit (safe
while it stays paused), spectators.

## SP-85: A prop made after the stage start has a sync id only on the host

**Severity: HIGH - the client cannot see, place or pick up what the host
refers to, and nothing crashes, so it looks like a missing item or an
invisible enemy.**

`netSyncIdsAllocate` gives every prop that exists at the stage start the same
id on each machine (slot index + 1). After that, `propAllocate` gives each new
prop `g_NetNextSyncId++` on the host and 0 on a client (`prop.c`). A host
message that names such a prop reaches a client only if a spawn message named
it first (`SVC_PROP_SPAWN`, objects only) or the client gave its own copy the
same id. Otherwise the client logs `NET: prop with syncid N does not exist`
(or its reader's own skip line) every time.

**Known instances (2026-11-16):**
- B-1194: simulants pick up copies of the match's rolled spawn weapon that
  carry dynamic ids no client knows; a client skips every such pickup.
  Named and fixed in code: they are disarmed simulants' own held models
  (`botDisarm` drops the right-hand model after clearing `weapons_held`, so
  the held-slot announcement missed it), and the client's puppet ran the same
  disarm through `SVC_CHR_DAMAGE` and dropped an unnumbered copy. The chr drop
  block now announces every dynamic-id weapon the drop puts in the world,
  whatever path marked it, and the puppet retires its model instead.
- B-1196: a guard the host's AI clones or spawns (`try_spawn_clone2` 0x00ca,
  `spawn_chr_at_pad`/`_at_chr`) exists only on the host; the client got its
  `SVC_NPC_MOVE` 20 times a second and never saw it. Fixed in code: the host
  announces such a chr with `SVC_NPC_SPAWN` (its weapons and hat included)
  before its moves, and the client makes it under the same ids. Open: a
  reconnecting co-op client does not get chrs made before it rejoined.
- B-1206 (the removal case): a chr the host removes (`aiRemoveChr`, a faded
  or reaped corpse, anything through `chrTick`'s `CHRHFLAG_DELETING` branch)
  stayed on a co-op client, whose NPC AI and actions never run, and every NPC
  snapshot after that was refused for its count. Fixed in code: the host
  announces each removal (`SVC_NPC_REMOVE`, with whether the chr's items
  dropped) and the client removes its copy at once; a snapshot also removes
  any NPC of the client's that it does not name, for removals made while the
  client was still loading.
  Follow-up (SP-91): the message was written in `chrTick` and dropped on
  frames that discard the game tick's messages; it is now queued and sent
  from the ACTIVE entity broadcast.
- B-1209: scenario props the host makes during the match: Hacker Central's
  new uplink after each carrier's death (`htbCreateUplink` from `htmTick`)
  and Hold the Briefcase's new case (`htbCreateToken` from `htbTick`). A
  client runs neither, so it never saw them and skipped every simulant
  pickup of the uplink. Fixed in code: `scenarioNetAnnounceProp` sends
  `SVC_PROP_SPAWN` and `SVC_PROP_MOVE` right after the host makes one.

**Rules:**
- A prop the host creates at run time that clients must see or act on is
  announced with its id (a spawn message), or made by the client only from
  that announcement.
- A prop both machines create at run time (held models, effects) stays local
  on each, and is never handed to the world under an id; when it becomes a
  world item, the host announces it and the client retires its own copy.
- What the host alone decides to remove (a chr whose AI or actions run only
  on the host) is announced too; a full snapshot is the host's list, so a
  client drops what it does not name.

**Search:**
- `grep -n "first dynamic syncid\|does not exist\|client skipped" pd-*.log`:
  an id at or above the first dynamic id is this pattern.
- `grep -rn "propAllocate()\|chrGiveWeapon(\|weaponCreateForChr(" src/game`
  and check each run-time caller: who creates it, and how a client learns its
  id.

**Audit checklist:** for each run-time prop, (a) is it created on the host
only, on both, or on the client only, and (b) does any host message name it
by id? Done: player drops (`weaponCreateForPlayerDrop`), simulant inventory
drops (`botinvDrop`), every weapon a chr's drop block puts in the world (held
weapons through `chrBeginDeath`, a disarmed simulant's gun through `botDisarm`,
concealed items; B-1194, in code), AI chr spawns and clones with their weapons
and hats (B-1196, in code), chr removals through `chrTick` (B-1206, in code;
the counter-op takeover in `player.c` removes a guard outside it, covered only
by the snapshot sweep). Scenario props made during the match (B-1209, in code: Hacker
Central's uplink, Hold the Briefcase's case; Capture the Case makes its cases
only at the stage start). Note for new drop paths: the `chrTick` drop block
covers any weapon marked with `objSetDropped` that is still the chr's child,
but a path that creates and places a prop itself (as `botinvDrop` does with
`objDrop`) must announce it. Open: reconnect for B-1196's chrs; a client-side
run of another host-only effect through `SVC_CHR_DAMAGE` (`chrDamage` runs in
full on a client); whether a co-op client drops a guard's held weapons at its
death (not checked; the host's drop of an initial-id weapon names an id the
client already has, so its spawn is declined and only the move applies).

## SP-87: Per-frame work inside a pass that runs once per player

**Severity: HIGH in networked play - timers, AI and scores run N times as
fast with N players, and nothing looks broken offline (one player, one
pass).**

`lvRender` runs its per-player pass for every player of a networked match
(`if (g_NetMode) forcesingleplayer = false;`), as it does for each
split-screen viewport, so `propsTickPlayer` and everything it calls runs once
per player each frame. The original marks every prop `PROPFLAG_NOTYETTICKED`
on the frame's first pass (`currentplayerindex == 0`) and does per-frame work
only while the mark is set: `chrTick` and `objTickPlayer` clear it into their
`fulltick`. Work outside such a gate runs on every pass.

**Known instances (2026-11-16):**
- B-1202: `botTick` read `lvupdate240` alone (41431a3d), so a simulant's AI,
  turning, scenario points and pickups ran once per player. Fixed in code:
  the gate reads the mark again, before `chrTick` clears it.
- Not fixed, cosmetic: the surface-locomotion blend at the end of `chrTick`
  (`surface_blend_frames--`, `chrSurfaceLocoTick`) runs outside `fulltick`, so
  a chr that uses it blends N times as fast on a networked machine (CORE's
  area; left as it is).
- Fixed in code 2026-11-16 (option (b), CORE agreed; was open for Mike and CORE): the scheduler's own time accounting.
  `propsTickPlayer` adds the frame's `lvupdate240` to every prop's
  `propupdate240` and every slot's `slotupdate240`, and advances
  `runstateindex`, on every pass, and a background prop's tick spends the
  accumulated time. So a prop in no player's view gets N times the elapsed
  time (a simulant out of view acts twice as fast with two players; so does a
  co-op guard out of view on the host). The original split-screen game works
  the same way, so this is original behavior there, but a networked match
  runs a pass for every remote player as well. Options: (a) leave it;
  (b) in a networked match, add the time and advance the slot on the frame's
  first pass only, and skip background ticks on later passes, so every prop
  gets real time as offline (recommended; split screen unchanged); (c) the
  same for split screen too (departs from the original).
  Done as (b): `netrepeatpass` in `propsTickPlayer` (`[sp87]` pins it).

**Rules:**
- Per-frame work in a prop tick waits on `PROPFLAG_NOTYETTICKED` (or the
  tick's `fulltick`), read before anything in the same tick clears it.
- Per-player work (what one player sees, that player's HUD) is the only work
  that belongs on every pass.

**Search:**
- `grep -n "lvupdate240 != 0\|lvupdate60" src/game/*.c` inside functions that
  `propsTickPlayer` reaches, and check each against the mark.
- A networked log with `BOT.TICK: simulant N passed again in one frame` shows
  the passes; scores or timers that beat the clock (a Hold the Briefcase point
  in under 30 s held) point here.

---

## SP-88: A public key's name states a unit its consumer does not use

**Severity: LOW to MEDIUM - nothing crashes and the data validates, but an
author who trusts the name gets a value 4 or 60 times off.**

A public key (a graph param, a physics key, a parser alias) carries its unit in
its name (`_ticks60`, `_ticks240`, `time60`, `timer60`), and the code that reads
it meets a counter in another unit, or the value is not a time at all. Base
data carries the original's literal, so the game behaves as the original; only
a new author is misled.

**Known instances:**

- B-1230 (MODS's row, open, CORE's call): `physics.json` `boost_time_ticks60`
  caps `maxspeedtime240`, which adds `lvupdate240`, so the value is in 240 Hz
  ticks (the base hoverbike's 2400 is the original's 10 s ramp, `TICKS(2400)`).
- B-1178 (fixed 2026-11-16, `week-pd/core`): `entity.autogun`'s
  `beam_interval` is a shot count (`firecount % beaminterval`), but the graph
  compiler's name rule (`keyNeedsUnit`: any key containing `interval`,
  `timer`, `duration`, `cooldown` or `_time` needs a unit suffix) refused it,
  so authors had to write `beam_interval_ticks60`. The same trap from the
  other side: a name heuristic that decides a key is a time.
- Checked, not affected: every other `_ticks60` field in
  `weapon_graph_runtime.h` reaches a 60 Hz field (`timer60`, `recoverytime60`,
  `activatetime60`, `duration60`) or `weaponGraphTicks60To240`.

**Rule:** a key that names a unit is read in that unit and converted in one
place, and its test uses a value whose unit matters. When a name is wrong,
rename it in a schema version or refuse the old name; do not document the
trap. A rule that infers a unit from a key's name lists every key it catches
and each is checked against its reader (`timer_starts` and `beam_interval` are
the exemptions today). New public time values take `{value, unit}` (the weapon
module rule).

**Search:**
- `rg -o 'heldParam[A-Za-z]*\(ir, node, "[a-z0-9_]+"' port/src/weapon_graph_runtime.c`
  and keep the keys that match `keyNeedsUnit` without a unit suffix: each must
  be a time or exempt.
- `rg -n -o '[a-z_]*(ticks60|ticks240|time60|timer60)' port/include/*.h`, then
  for each public field `git grep -n -E '(->|\.)<field>\b' -- src port/src` and
  check the counter it meets (`lvupdate60`, `lvupdate240`, a frame or shot
  count).

---

## SP-89: A bulk writer forces every file to disk one at a time

(First numbered SP-80 by CORE; MULTIPLAYER had claimed SP-80 first, for
scenario setup that assumes the arena brings its own placement data, so
this one was renumbered on 2026-11-16 when dev brought theirs in.)

**Severity: MEDIUM - correct but slow; on a 7200 rpm disk a forced flush plus
a write-through rename costs about 100 ms per file, so a writer that
publishes thousands of files takes many minutes.**

The durable publish (write a temp file, `_commit` / `fsync` it, then
`MoveFileEx(... MOVEFILE_WRITE_THROUGH)`) is right for one file a person
made: a save, a settings file, an authored mod. Inside a loop over thousands
of files the game can rebuild, it makes the disk seek for every file. On NVMe
the same pattern costs about 2 ms per file, so it only shows on a hard disk.

**Known instances:**

- B-1110 (fixed 2026-11-15): first-launch extraction published about 5,600
  archives through `modArchiveFinish`. Now `modArchiveBeginDerived`, and one
  `modArchiveDerivedBarrier` per directory before its cache stamp.
- Open, MULTIPLAYER's area: `pdcaExtractArchiveBegin`
  (`port/src/pdca_extract_transaction.c`, called from `netdistrib.c`) syncs
  every member of a received mod archive, so a large transfer pays the same
  cost per member.

**Rule:** for rebuildable output, publish without the per-file flush and make
the set durable once, right before anything that vouches for it without
opening it (a cache stamp, an index). That record must never reach the disk
ahead of the files it describes.

**Search:**
- `rg -n '_commit\(|fsync\(|FlushFileBuffers|MOVEFILE_WRITE_THROUGH' port src`,
  then check whether each runs once per user action or once per file in a loop.

---

## SP-90: A tool launch that names no isolation falls back to the user's real data

**Severity: HIGH - a test run reads and rewrites the player's own settings,
bindings, statistics and mod cache, and a build that cannot parse what another
build wrote there crashes at boot.**

The game's defaults are the player's real folders: with neither `--portable`
nor `--savedir`, `fsInit` (`port/src/fs.c`) sets `$H` and the save dir to
`%APPDATA%/perfectdark`, and may migrate an install's `pd.ini` or `eeprom.bin`
into it. Every tool that launches the game must therefore name isolation on
every launch path, or it shares the player's profile with every other session
and build on the machine.

**Known instances (2026-11-16):**
- B-1124: multi-process fixtures on a shared install (no
  `separate_process_installs`, no `--portable`/`--savedir`). Fixed: seven
  fixtures pass `--portable`, the runner refuses such a fixture, and
  `tools/tests/test_smoke_fixture_isolation.py` checks every one.
- B-1203: the runner's single-process launch added `--savedir` only under
  `-ReuseInstallId`, so 81 single-process fixtures ran against the real
  profile. Fixed: `run.ps1` adds `--portable` when the fixture names neither
  flag (`test_runner_isolates_single_process` pins it).

**Rules:**
- A test or tool launch of the game names its save dir (`--savedir`) or runs
  portable from its own install; the runner adds it when the fixture does not.
- A check that a launch is isolated reads the launch code, not a comment or a
  bug record about it (B-1124's record said the single-process path was safe).
- Mike's own play launchers (`devtools/_dev-window.ps1`, the dev windows) use
  the real profile on purpose; they are not test launches.

**Search:**
- `grep -n "ProcessStartInfo\|Start-Process" tools/smoke-verify/*.ps1 tools/smoke-verify/lib/*.ps1`
  and check that every game launch reaches `--portable` or `--savedir`.
- In a run's game log, `save dir:` must name the run's install or profile
  folder, never `AppData/Roaming/perfectdark`.

---

## SP-91: A one-shot message written in the game tick is dropped by a frame that discards the shared buffers

**Severity: HIGH - a client never hears of an event the host made once (a
removal, a pickup, a stage flag, an objective status), and nothing logs the
loss.**

`netStartFrame` empties `g_NetMsg` and `g_NetMsgRel`; the game tick then writes
into them (pickups, prop spawns and moves, doors, alarms, stage flags,
objective status, chr damage, player stats: about 48 call sites in
`src/game`), and `netEndFrame` normally sends them. It empties both buffers
instead on three kinds of server frame: (1) the terminal stage-end frame;
(2) any frame while the stage replication is not ACTIVE (INACTIVE, WAITING;
RELEASE publishes only its baseline); (3) an ACTIVE frame with a cutscene
authority event pending (START or END: "Any authority event already pending
at this boundary supersedes ordinary game messages accumulated since
netStartFrame"). (1) is harmless, the stage is ending. (2) is covered when the
release baseline carries the state, and a producer can check
`netServerStageReplicationBlocked()`. (3) is not covered: no baseline follows,
and the authority packet and the shared reliable buffer both go reliably on
`NETCHAN_DEFAULT`, so the frame's messages could have been sent after the
authority packet in order instead of thrown away.

**Known instance (B-1206, 2026-11-16):** the host's `SVC_NPC_REMOVE` for Deep
Sea's intro actor was written on the frame that published the intro's cutscene
END; the client kept that chr for the rest of the stage. Fixed for that
message: removals are queued (`netServerNpcRemoveQueue`) and written by the
ACTIVE entity broadcast, after any authority packet.

**Open, not audited:** every other one-shot writer in the game tick is exposed
on cutscene START and END frames in co-op and counter-op, which are frames where
level scripts commonly act (end the intro, set objectives and stage flags, open
doors, remove actors). A class fix would keep the reliable game-tick bytes
across the authority publish and append them after the authority packet (same
reliable channel, so ENet keeps the order), discarding only the unreliable
buffer.

**Class fix (ded9b22f, 2026-11-16, checked in play on c35b35a2; CORE agreed the change and
gave it to MULTIPLAYER, week note 17:04 UTC):** on (3), `netEndFrame` holds
the tick's reliable bytes (`netServerAuthorityHoldReliable`) and, once the
authority packet is out, puts them first in the frame's reliable buffer
(`netServerAuthorityReleaseHeld`), ahead of the entity broadcast; a client
applies them after the cutscene state changed, as the host's tick saw them.
No client handler gates on cutscene state. While the publication retries
they stay held and later frames' bytes join them in order; the hold is
emptied only after what was put back went out
(`netServerAuthorityHeldSettle`), and dropped at a stage boundary or end. A
full hold (64 KB) drops with a `NET.SP91` warning. (1) and (2) are unchanged.
Log: `NET.SP91: held N game-tick bytes`, `NET.SP91: N held game-tick bytes go
out`. Pinned by `[sp91]` in `tests/test_net_mission_end_static.cpp`.
In play (c35b35a2, base 7): every cutscene authority frame held the
tick's bytes and sent them right after its packet (co-op Deep Sea end 34
and 106 bytes, co-op mission end 5 and 98, counter-op mission end 5).

**Rules:**
- A one-shot reliable message the host decides in the game tick is queued and
  written by `netEndFrame`'s ACTIVE broadcast, or its writer says what covers
  its loss before ACTIVE (the release baseline; `netServerStageReplicationBlocked()`).
- A frame rule that drops buffered messages names what resends each one.

**Search:**
- `grep -rn "g_NetMsgRel" src/game --include=*.c` lists the game-tick
  writers; for each, what restores the event if its frame is discarded?
- In a co-op log pair: a host line for a one-shot event with no client line,
  in the frame of `CUTSCENE.AUTHORITY: tick-mode START|END queued`.

**Audit checklist:** for each game-tick writer, (a) one-shot or periodic (a
periodic one is resent next frame), (b) covered by a baseline or snapshot,
(c) can it run on a frame that publishes authority. Done: `SVC_NPC_REMOVE`
(queued, B-1206); every reliable writer for (3) by the class fix. Open: (2),
a one-shot reliable writer that runs before ACTIVE and is not in the release
baseline.

---

## SP-92: A client applies a host rooms list without moving the prop's room registration

**Severity: HIGH - a crash, later and somewhere else: the first collision or
room query in an old room after the prop is freed.**

A prop has two records of where it is: `prop->rooms` (its list) and the room
prop lists (`g_RoomPropListChunks`, which `roomGetProps` reads for collision,
drawing and room queries). `propRegisterRooms` and `propDeregisterRooms` keep
them together, and both work from `prop->rooms`. Code that writes
`prop->rooms` directly, or deregisters and re-registers only under a
condition that does not match whether the prop is listed, leaves the prop
listed in rooms its list no longer names. While the prop lives that only adds
a candidate to some room queries. When it is removed, `chrRemove`, `objFree`
and `propFree` clear only the rooms in `prop->rooms`, and `propFree` keeps
`prop->type` but clears `prop->chr` (and so `prop->obj`): the old rooms still
list a chr or object prop with nothing behind it, and the next
`propIsOfCdType` or room walk there reads NULL. The game itself always moves
the two together; the network apply paths, written later, did not. The host
runs none of them, so only a client crashes.

**Known instance (B-1270, 2026-11-16):** `netmsgNpcResyncRecordApply` wrote
the host's rooms straight into every NPC's `prop->rooms` (the stage-start
snapshot included); Deep Sea's intro actor stayed listed in its old room, and
when the host removed it (SVC_NPC_REMOVE) the client crashed in the next NPC
move's ground query. Same write in `netmsgSvcChrResyncRead` (bots) and
`netmsgSvcPropResyncRead` (objects); `netmsgSvcPropMoveRead` and
`netmsgSvcPropLiftRead` moved the registration only when `prop->active`, and a
paused prop is listed too. All five now call `propSetRooms` (746d79a7).

**Search:**
```
grep -n "prop->rooms\[[a-z]*\] = \|roomsCopy([a-z_>.-]*, *[a-z_>.-]*prop->rooms)" port/src/net/*.c port/src/*.c
grep -n "if (prop->active) {" port/src/net/netmsg.c
```

**Rules:**
- A prop's rooms list changes through `propSetRooms` (moves whatever
  registration the prop has with the list, copies the list exactly) or
  through the game's own movers (`chrSetPos`, `chr0f0220ac`), never by a bare
  write.
- Whether to move the registration follows whether the prop is listed
  (`propSetRooms`: active, or listed in its first room), not `prop->active` alone: paused props are
  listed too.

**Audit checklist:**
- [x] NPC resync, bot resync, prop resync, SVC_PROP_MOVE, lift move
  (B-1270).
- [x] `netmsgSvcReconnectPropStateRead` (reconnect snapshot): moved the
  registration only when `was_active`, so a paused, listed prop kept its old
  rooms; now `propSetRooms` (2026-11-16, after ded9b22f).
- [x] SVC_PROP_SPAWN: `roomsCopy(rooms, prop->rooms)` runs before the prop is
  registered; no creator registers it first (`objInit` and `laptopDeploy`
  only allocate and initialise), and the placement block registers it once.
  Correct as it is.
- [-] `netmsgClcBotMoveRead` (bot-authority stub): a stub has no room
  registration; leave it.

---

## SP-93: An asset the match puts on the wire is missing from the match manifest, so clients read the default

**Severity: HIGH - silent: the client shows a different object (no weapon, a fallback model) and nothing logs it.**

Messages name assets by session catalog id, and the session catalog is built from the match manifest. A reader
that gets id 0 falls back to a default (`netReadWeaponRef` returns `WEAPON_UNARMED`, `netReadModelRef` 0) without a
word, and the writer sent 0 silently too. So every asset the match can name on the wire must be in the manifest,
not only the ones the lobby chose: a scenario's own items, what a script or a death can drop, what a simulant can
carry.

**Known instance (B-1272, 2026-11-16):** the Combat Simulator manifest's weapon pool took only weapons with an MP
weapon slot; Hacker Central's data uplink and the briefcase of Hold the Briefcase and Capture the Case have none,
so every message naming one reached clients as no weapon (B-1209's replacement uplink replayed as `weapon=1`).
Fixed by `scenarioTokenWeaponNum` and the lobby start's token closure.

**Second instance (B-1273, 2026-11-17), found by the new model warning:** the manifest listed weapons but not
the models their props are drawn with, so a dropped gun, the replacement uplink and a dropped briefcase
reached clients as model 0 (`MODEL_ROOFGUN`). Fixed by listing every manifest weapon's world model and the
token's own model; checked in play 2026-11-17 (five dropped guns and a replacement uplink, no warning).

**Search:**
```
grep -n "netWriteWeaponRef(\|netWriteModelRef(\|catalogWriteAssetRef(" port/src/net/netmsg.c
grep -n "s_manifestSkipMpWeaponSlot\|manifestAppendCatalogClosure(" port/src/net/netmanifest.c port/src/net/netmsg.c
```
In a run's host log: `NET: weapon N ('<id>') has no session catalog id`.

**Rules:**
- A new message field that names an asset by session id comes with the manifest entry that gives it one, for
  every value the field can carry.
- A writer that has no id for a real asset says so (once per asset); a silent 0 hides the gap.

**Audit checklist:**
- [x] Scenario tokens in the Combat Simulator manifest (B-1272).
- [ ] Weapons a co-op or counter-op stage can drop that its setup and AI-list scans do not list (watch the new
  warning in the co-op smokes).
- [x] `MPWEAPON_SHIELD` (`WEAPON_MPSHIELD`, a weapon prop, slot 5 of every original weapon set): both manifest
  weapon loops skip it, and the reconnect snapshot's reader writes each weapon prop's number from its session id
  (`netmsgSvcReconnectPropStateRead`), but it is covered: `matchConfigPrepareExactWeaponIds` gives every slot
  but `MPWEAPON_NONE` a catalog id, the shield included, and the lobby start adds each `weapon_ids` closure.
- [x] Model refs: `netWriteModelRef` warns the same way (`NET: model N ('<id>') has no session catalog id`),
  for SVC_PROP_SPAWN's weapon and object models; its first run found B-1273.
- [ ] Object models in SVC_PROP_SPAWN (thrown laptops and other spawned objects): watch the warning.

---

## SP-94: A struct read by its N64 word offset finds padding on 64-bit

**Severity: MEDIUM. The read gets padding or a neighbouring field, never the
value it wants, and nothing fails: a lookup returns "not found" every time.**

Code written for the N64 sometimes reads a struct's field as a fixed 32-bit
word of the raw record (`cmd[5]`, `((u32 *)p)[n]`) instead of through the
struct. On the N64 a pointer is one word, so the offsets agree. On this
64-bit port every pointer field is 8 bytes and 8-aligned, so it and every
field after it move: the fixed word lands on padding or on another field.
A cast like `(struct prop *)cmd[5]` also drops the pointer's upper half.

**Known instance (B-1259, 2026-11-17):** `setupGetCmdIndexByProp` compared
`cmd[5]` (padding on 64-bit) with the prop pointer, so it never matched: the
object events' record ids were empty and multiplayer's regeneration of an
object inside another looked up the wrong parent.

**Rules:**
- Read a native struct through its type (`((struct defaultobj *)cmd)->prop`),
  never by an N64 word offset.
- A raw ROM record (N64 layout, big-endian) is read with the N64 offsets on
  purpose: those readers name the N64 layout and byte-swap. Do not mix the two.

**Search:**
- `rg -n "\(struct [a-z_]+ \*\)\s*(PD_BE32\()?[a-z_]+\[[0-9]+\]" src port`
  then check each array is pointer-sized (`uintptr_t *`, `struct x *[]`), not `u32 *`.
- `rg -n "\(u32 \*\)[^;]*\)\[[0-9]+\]" src/game src/lib` for word reads of native structs.

---

## SP-83: A Gfx union member reads its second word from the wrong bytes on 64-bit

**Severity: HIGH — a decoder silently gets zeros or the wrong fields from every
command of that kind, and skips the work without a log line.**

On 64-bit builds `Gwords` holds two `uintptr_t` words, so w1 starts at byte 8.
A union member whose second-word fields are narrower than `uintptr_t` (a
4-byte `Tri`, 32-bit bitfields) packs them at bytes 4..7, the upper half of
w0, unless it carries a `PLATFORM_64BIT` pad. Members holding a `uintptr_t`
(`Gdma.addr`) are aligned to byte 8 and are fine.

**Known instance (B-1212, 2026-11-15):** `Gtri` had no pad, so every G_TRI1
index read 0. `Gtri4` was padded. Generated models draw with G_TRI1 only, so no
triangle of theirs could be hit, collided with in a rendered-room world mesh,
drawn in x-ray, or marked by `texLoadFromGdl`.

**Still unpadded, not read today:** `Gvtx.offset/seg`, `GunkC0.tile1/tile2/unk20`,
and the second words of `Gtexture`, `Gsettile`, `Gloadtile`, `Gsettilesize`,
`Gfillrect`, `Gsetcombine`, `Gsetcolor`, `Gline3D`. Game code reads only their
first-word fields (`vtx.unk08`, `unkc0.subcmd`, `texture.cmd`).

**Audit checklist:** before reading any Gfx union member's field, check which
word it lives in; a second-word field on 64-bit needs the member padded (or
read `words.w1` and shift). A unit test should decode a macro-built command
through the member (`tests/test_gdl_span.cpp` does this for G_TRI1 and G_TRI4).

`rg -n "(->|\.)(tri|tri4|vtx|line|texture|settile|loadtile|settilesize|fillrect|setcombine|setcolor|setimg|segment|unkc0)\.[a-z]" src port`

---

## SP-97: An operating system probe sits inside a loop that runs every frame

**Severity: MEDIUM — nothing breaks. The frame rate falls with the size of the
data, so a small base model hides the cost and a large mod model shows it.**

`modelRodataIsReadable` asks the operating system (`VirtualQuery`) whether a
range of memory can be read. It was written for about a hundred calls a tick,
one per model node, and its header estimated 100 ns a call. In this client a
call costs several microseconds. Inside a loop over display list commands or
vertex batches it becomes thousands of system calls per walk.

**Known instance (B-1234, 2026-11-17):** the shot hit test (`bgTestHitOnObj`,
`bgTestHitOnChr` in `src/game/bg.c`) probed every vertex batch, and on the
native path every command. The game runs that test every frame to find what
the player is looking at. A compiled mod model has one vertex batch per
triangle: 5,829 system calls for one test of the Maian vehicle, 30 to 48 ms of
every frame while its rider's view passed through it. The base hoverbike paid
about 3 ms a frame the same way and held 60 only because it is small.

**Fix shape:** keep the check and drop the repeated question. A walk carries a
`model_rodata_span_t` on its stack and calls `modelRodataIsReadableInSpan`,
which asks once per run of readable memory and answers the rest with two
compares. The span never outlives its walk: memory can be freed or
re-protected between walks, and a kept span would go on answering "readable".
Where the owner of the data knows its extent (the mod asset compiler's
generated display list spans), a bound check against that extent is better
still than a page probe.

**Left alone, not per frame:** `extractGfxTris` in `src/lib/meshcollision.c`
probes once per command while a prop's collision mesh is built (once per
prop). The model relation updates in `src/lib/model.c` probe once per node,
2% to 3% of the main thread's samples.

**Audit checklist:** for every probe call, read the loop it sits in and how
often that loop runs: per node is fine, per command, per vertex or per
triangle of a per-frame walk is this pattern. Measure before trusting a cost
estimate in a comment: a sampler on the running client found this in one run
(`rip_sampler.py`, `rip_report.py`, `rip_chain.py`, described in B-1234), after
reading the code had produced a wrong guess.

`rg -n "modelRodataIsReadable(InSpan)?\(" src port`

## Scripted pickup can clear the object-to-prop link

A scripted grant must hold the incoming prop through
`propPickupByPlayer` and pass it to the returned tick operation. For a
successful untagged weapon pickup without regeneration, `objFree` uses
`freeprop=false`, clears `obj->prop`, and leaves the original prop slot
for `TICKOP_FREE`. Touch pickup already carries the incoming pointer.

The Grid fixed-pickup case 5fe63674 reproduced a crash in
`propExecuteTickOperation` when `og.pickup.give` read the cleared link.
The Grid module now keeps the incoming prop. Source review found the
same expression in `aiGiveObjectToChr` and
`scenarioSourceAiGraphExecuteGiveObjectToChr`. Their production paths
need separate AI ownership and runtime verification.
