# Retained equipment native lifetime batch

T-MODDING-002 / T-RUNTIME-001; Codex (GPT-6), September29,2026.
Full asset/graph closure goal remains partial.

Equipment preparations can now carry the exact leased public-source modeldef,
including bindings without a legacy file number. Source closure hashes exclude
native pointer/slot integers. bgunQueueRetainedModelLoad validates the real
compiled generation and takes an idempotent stage pin before request mutation.
The ordinary bgunTickGunLoad consumes it before mutable catalog checks. Failure
preserves a previously queued request. Legacy queue, body-hand queue, failure
and player reset clear the retained-request field. This native loader seam is
connected and exercised; automatic v2 weapon equip/load routing remains open.

Custom equipped generations pin their exact allocated weapon ID/slot. Slot
reset drops catalog reservations and disables MP rows but preserves live
identity pins. Snapshot rollback validates all live identities before changing
any ID, reservation or MP row; it cannot rebind/move a pinned identity and
never restores reference counts. Same-ID resurrection restores a reservation;
last release frees an unreserved slot. Overlong IDs reject instead of truncate.
Deferred selected gsets retain the whole entry and its equipment/weapon pin.
A completed hand may now clear selection without allocation or closing its
explicit lifetime; independent copies keep their action/source until retirement.

Optional public pd.weapon_settings.v2 equipped.modes declares exactly two
null or {ammo_slot:-1|0|1} records. Primary is required when declared. Neutral
native functions have type NONE, no firing animation, and silent noise; they
supply idle/reload metadata without choosing an authored graph branch. Ammo
must exist when referenced, the compiled mode must have metadata, and complete
archive preparation rejects declared availability differing from actual graph
modes. Omitted metadata remains supported only for older source preparation;
the forthcoming production adapter must require explicit idle metadata and a
real direct model binding. Native candidate preparation alone is not gameplay.

Validation in .claude/session-builds/asset0923lang/:

- Final client/tests builds exit0: equipment0929-build-exits.json. Three test
  compile reds are retained: inherited constants-before-types OS macro ordering,
  fixture types inside extern C vs MinGW sprintf overload, and broad data.h OS
  globals C linkage. Source order corrected; fixture imports only the needed MP
  table declaration. Timestamped logs and build-{first,second,third}-red.json
  preserve the failures; no build bypass.
- Combined graph-v2 draft/equipped, required c3842, real model/weapon allocator
  and dependent body/head source cohort:143cases,4,754assertions,zero failures/
  errors. equipment0929-native.xml, equipment0929-test-result.json; guard exit0.
- Installed weapon_mesh_ingress_smoke39/39,88.6s,exit0. Result:
  .claude/smoke-verify-runs/results-20260930T010305Z.json. Fresh public fixtures
  queue the red old model, edit its image to blue, load the new same-ID catalog
  model, retire caller references, then invoke the real native load tick. Required
  MODEL.QUEUE.GENERATION exact_retained_source PASS, MODEL.SLOT.GENERATION
  retained_after_catalog_reset PASS, MODEL.GENERATION stage_retirement PASS and
  nested25/25 PASS appear in installed logs/game client/pd-client.log. The image
  decode error on the final generation witness is the intentional malformed
  source rejection, not a successful-load error.
- Frozen2,012source/fixture paths,drift0 after build/tests/installed run:
  equipment0929-source-before.json and equipment0929-drift-result.json.
  Client SHA25617F9F70BAEB326DC79A51FC942CC285E198072FB4A5A1F254F5ADC4389DA188A.
  Normal memory guard:free commit10,084MB/free RAM17,349MB; no override.

Propagation: existing model generation pins already protect model identities;
weapon slots now share the separation of reservation versus retained lifetime.
Body/head retained identity needs its own audit. No peer MP/menu code changed.
The client compiles their paused candidates, so this focused gate does not
supply their missing broader runtime/controller/menu acceptance.

Still required: captured archive v2 scanner/catalog admission and transactional
production publication, ordinary equipped/hand selection, physical input and
idle admission, native action host and scoped noise/shot gsets, delayed hit wake,
owner death/respawn/source replacement teardown, unsupported-use transitions,
then real played graph, network, controller and creator-pack evidence. All27
asset families and complete extraction/roundtrip coverage remain separate open
finish conditions. Do not mark the full goal implemented or validated.
