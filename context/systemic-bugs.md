# Systemic Bug Patterns — Architectural Issue Catalog

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

**Propagation check:** compare every typed activation/clear pair with the real
family manager, renderer, audio, collision, or gameplay object it supplies;
test active -> adapter populated -> final release clears -> reload rebuilds.

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

## How to Use

- Before starting any work that touches arrays, memory allocation, or stage indexing, scan this file for relevant patterns.
- When fixing a one-off bug, check if it's an instance of a pattern here. If so, do a propagation check (§3.6) on all files listed under that pattern.
- When discovering a new pattern class, add it here with severity, root cause, known sites, and search command.
