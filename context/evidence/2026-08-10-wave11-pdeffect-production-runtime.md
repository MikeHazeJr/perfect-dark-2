# Wave 11 public `.pdeffect` production runtime receipt

## Scope

Wave 11 closes the generic v1 `.pdeffect` production-consumer gap tracked by
`T-ASSETS-032`, `T-ASSETS-034`, `T-ASSETS-035`, and `T-ASSETS-036`.

- `effect_instance_runtime` owns growable overlapping instances, topology,
  contexts, targets, attachments, timelines, lifetime, cancellation, and the
  all-consumer transaction.
- `effect_gameplay_runtime` stages and commits explosion, spark, smoke,
  source-backed particle, and typed catalog-audio output through real game
  callsites.
- `effect_presentation_runtime` and `effect_presentation_renderer` publish
  screen, decal, light/halo, beam, and source-textured particle output through
  the real ImGui, GBI, and world-render paths.
- Activation uses one strict per-opcode field schema. Unknown, mistyped,
  unsupported, behaviorally inert, or unresolved fields fail before a runtime
  instance or consumer output is published.

## Transaction and lifecycle guarantees

Every participating lane begins and stages before any prepare. Every lane then
preflights resources and native capacity before any infallible synchronous
commit. A failure rolls all lanes back with no gameplay, audio, renderer,
custom-spark, or particle residue. Instance cleanup is keyed by source and
entity lifetime; props are cancelled before pointer reuse and presentation
snapshots retain copied spatial state rather than raw entity pointers.

Public compiler and editor storage for contexts, subgraphs, and exports is
growable. Particle identity includes instance, node, and execution index, so
repeated authored rows remain distinct. Timeline activation accepts only the
currently consumed `intensity` property; unsupported properties are rejected
instead of being retained inertly.

## Source-frozen verification

Final production/test fingerprint before and after the receipt:
`605133665c2cc3666ade86886d678c9fae4016125537c209a0818edfc0dcb47c`
across 42 changed/new runtime, build, and test files.

- Isolated client, updater, and `pd-tests` compilation: PASS.
- `[t-assets-034]`: 149 assertions / 11 cases PASS.
- `[t-assets-035]`: 184 assertions / 8 cases PASS.
- `[t-assets-036]`: 525 assertions / 13 cases PASS.
- Complete suite: 54,175 assertions / 980 cases PASS.
- Archive scanner self-test: 16 parity cases, recursion, and 8 structured
  source contracts PASS.
- Checked-in examples: 28 root archives / 52 recursive archives / all 27
  public families PASS.
- `asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

Raw logs are retained under `.claude/session-builds/tassets034/` as
`wave11-final2-*.log`.

## Truth boundary

These tasks are implemented, not live-validated. `V-009` still owns ordinary
installed-client edited-source gameplay/render/audio/lifecycle proof and real
peer-network delivery. `V-006` retains induced negative transport/rollback
proof. No live behavior claim is inferred from the automated receipt.
