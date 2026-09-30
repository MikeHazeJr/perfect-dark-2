# Retained public model slot identity

T-RUNTIME-001; Codex (GPT-6), September 29, 2026. Full asset/graph goal partial.

The private custom model allocator reset or restored its identity map without
accounting for users retaining compiled public models. A slot could therefore
acquire a different catalog identity before its old generation retired.

Each published custom model generation now pins its exact allocated ID/slot.
Source-hash dedup does not add a second pin; another source generation owns
another pin. Dedup rejects conflicting native binding metadata. Final model
destruction releases its pin. Catalog reservations and generation pins are
separate: reset drops reservations but preserves live identity; final release
frees a slot whose reservation was dropped. Snapshot rollback preflights every
pinned identity, rejects rebinding or movement atomically, and never restores
operational reference counts. Overlong catalog IDs fail instead of truncating.

Six native cases cover multiple leases, reset/final reuse, repeated empty
rollback, restored reservations, conflicting identity with atomic rejection,
retained identity movement, invalid/foreign pins and balanced releases. The
installed generation harness now uses a real custom slot through ordinary
catalog loading, public image edits, malformed source rejection and retained
old/new model storage. It resets the slot map, proves another ID cannot take
the retained slot, retires stage pins twice and checks the surviving owner.

Validation in .claude/session-builds/asset0923lang/:

- First client build failed at legacy constants/OS declaration header ordering;
  modelslot0929-build-first-red.json retained. Include order corrected.
- Final client/tests build exits 0: modelslot0929-build-exits.json.
- Explicit graph/c3842/model-slot cohort: 123 cases, 4,163 assertions, zero
  failures/errors; guard exit0: modelslot0929-test-result.json and native.xml.
- Existing dependent body/head source/transaction suite: 8 cases, 322 assertions,
  zero failures/errors: modelslot0929-body-result.json and body-source.xml.
  Initial combined selector used an unmatched body tag; this explicit gate
  supplies that missing coverage without claiming it ran in the first cohort.
- Installed weapon_mesh_ingress_smoke passes38/38 in85.2s, exit0:
  .claude/smoke-verify-runs/results-20260930T002245Z.json. Required exact
  MODEL.SLOT.GENERATION retained_after_catalog_reset PASS and MODEL.GENERATION
  stage_retirement PASS appear in installed pd-client.log; nested25/25 passes.
  Client SHA256 FADF7776C0A1337F546AD892C0DA98C7173A092F91FA78783AE69F6D05C5F398.
- All2,011 recorded source/fixture files match: modelslot0929-drift-result.json.
  After build, only smoke auto-selection paths were extended to include the
  allocator h/c; compiled source stayed unchanged and the updated fixture was
  captured before runtime. No runtime memory-guard override used.

Propagation review recorded in context/systemic-bugs.md: weapon/body/head
identity-map resets need their own retained-consumer audit; audio/animation/
texture use separate generation slot reservations. No peer source was changed.

This is model identity/storage lifetime proof, not complete played graph proof.
Same-ID source versions still share a private slot; v2 equipment's queued model
load must consume its retained generation directly instead of resolving the
mutable catalog again. Weapon slots, full graph cutover and all-family scope
remain open. Where to continue: held-graph-production-seams-2026-09-27.md.
