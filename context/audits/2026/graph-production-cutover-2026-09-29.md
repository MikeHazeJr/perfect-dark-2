# Executable graph production cutover, 2026-09-29

Owner: codex-f9571c12-6af53c63. Workbench: T-MODDING-002 / T-RUNTIME-001.
Status: first offline custom single-shot profile connected and validated at
native hand lifecycle boundaries; full graph and asset goal remain partial.

The scanner and typed weapon activation dispatch captured public
pd.weapon_graph.v2 archives away from v1 scalar projection. The client-thread
host prepares both modes, equipped metadata, selected public mesh, commands,
audio and per-ammo casing models before publication. A second whole-archive
read rejects source edits during preparation. Explicit player/hand lifetimes
retain immutable entries; neutral idle metadata never chooses an action.
Native firing alone debits ammunition. Scoped gset copies retain source/action
through noise and hit calculation. Pending delivery survives skipped drawing.
Catalog replacement cancels old hosts before leases retire. Player roots retire
before stage pins. Casing creation selects the firing hand's exact source.
Held-model generation replacement forces the native model queue to refresh.
Death retires active hands while deferred copies retain their accepted source.

## Passing evidence

Baseline HEAD28228996b55b37b38ebf033b308dbb0af987415a; combined menu source
was included in the freeze. Isolated build session asset0923lang, queued Jobs2:
client0 and tests0, logs _build-headless-20260929-221805-compile-client.out.log
and _build-headless-20260929-221811-compile-tests.out.log. Latest receipts are
under .claude/session-builds/asset0923lang/.

- graph0929-native.xml: 148 cases,4941 assertions,zero errors/failures. Cohort:
  graph-v2-draft/equipped,modding pdxxx c3842,model/weapon slots,body-head source.
- graph0929-source-guard.out.log: asset_native_source_guard.py passes.
- graph0929-source-before.json / source-after.json:3408 source/fixture files,
  zero drift across build/native/installed gates.
- graph0929-installed-pass.json / installed-pass.log: client exit0; SHA256
  873C547AE0E5827CCF56F9C8A7B4C98740404C5BDFE26F6DB2ECD7D34B48D381.
- .claude/smoke-verify-runs/results-20260930T022115Z.json:41/41 assertions,
  26/26 nested cases,84.58 seconds. GRAPH.V2.PRODUCTION.HARNESS reaches
  retirement PASS after real native hand firing, independent dual-hand state,
  graph-state ammo switching, selected damage3/7, skipped delivery without
  duplicate ammo/noise, rejected secondary edit preserving old source,
  valid edit selecting damage6 while old deferred data stays7, and owner death
  retaining deferred data. Native shots are acknowledged explicitly in this
  harness; it does not exercise actual world hits or rendered appearance.

## Retained red evidence and repair

First compile rejected a missing stdint include; second test-target link
rejected the isolated catalog mutation fixture's missing reset double. Both
were repaired before the green build. First two installed attempts passed the
existing25 cases but rejected the new fixture before firing. Its mesh.ini used
[mesh]; production modelSourceResolvePath requires [model]. Host alternate
section tolerance concealed the fixture mismatch until conversion. Host and
fixture now share canonical [model], with a negative strict-reader regression.
No shared converter change or ROM fallback was introduced. Red build JSONs,
installed-first-red.json and installed-second-red.json remain alongside final
receipts; second installed red client log is preserved. Earlier FIFO native
summary4935 was an error; actual preceding receipt was4940, latest is4941.

## Explicit remaining boundaries

held_single_shot.v1 currently admits offline custom weapon slots only. Network,
bot and Theater entry reject unsupported execution. Base-number special policy
seams must migrate before v2 base overrides. Retained executable slots cannot
silently demote to v1. Automatic/burst/beam,charge/throw/melee/device and
projectile/entity execution remain separate pending lifecycle units. Full
27-family/regional extraction parity, standard-format semantics, creator pack,
Modding Hub and distribution workflows remain in the original closure plan.

Next: base single-shot source/runtime parity plus actual ordinary world-hit
and model/animation delivery; extend profile families only after that complete
lifecycle. Actual world hit, render, audio hardware, controller and human visual
acceptance are separate open gates. The first two runs overlapped an old menu
run because resource names differed; no focus evidence is claimed from them.
The final run reserved game-client and game together. Peer source/tests/notes
and aggregate Workbench changes are preserved outside this scoped checkpoint.
