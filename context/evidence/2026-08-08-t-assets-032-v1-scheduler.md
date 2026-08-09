# T-ASSETS-032 v1 effect scheduler foundation — supporting receipt

Date: 2026-08-08
Workbench item: `T-ASSETS-032`
Bug: `B-1011`
Decision: `D-002`, Option A

## Truth boundary

This receipt supports a **partial** implementation milestone. Production
activation now rejects a retained v1 graph unless its schedule is a complete,
dependency-respecting permutation and every accepted opcode maps to the
permanent nine-slot dispatch contract. The public dispatch API preflights all
required handlers before beginning a fail-closed begin/commit/rollback
transaction.

No production gameplay, audio, renderer, timeline, target, context, or lifetime
caller invokes this API yet. Those consumers remain explicitly owned by
`T-ASSETS-034`, `T-ASSETS-035`, and `T-ASSETS-036`. Therefore neither this
receipt nor API availability is evidence that authored v1 effects execute in
ordinary gameplay, and `T-ASSETS-032` remains partial.

## Frozen implementation surfaces

- `port/include/effect_graph_runtime.h`
- `port/src/effect_graph_runtime.c`
- `tests/test_effect_graph_runtime.cpp`
- `tests/test_effect_graph_scheduler.cpp`
- `CMakeLists.txt` (one focused test registration)

## Supporting automated receipt

The first joint Wave 9 run used the same source fingerprint before and after
the client/updater phase:

`D42AE6EF2E36206BC1895284D43E58A7458A1B370AA544DD3AD99254F889A842`

- Source fingerprint captured: `2026-08-09T01:00:06Z`
- Queued all-target request started: `2026-08-09T01:00:13Z`
- Client/updater build interval: approximately
  `2026-08-09T01:01:10Z`–`2026-08-09T01:02:08Z`
- Post-build source fingerprint checked: approximately
  `2026-08-09T01:02:20Z`
- Queued tests-target interval: `2026-08-09T01:03:17Z`–`2026-08-09T01:04:11Z`
- Client, updater, and tests targets: PASS
- Focused `[t-assets-032]`: 67 assertions / 6 cases PASS
- Complete `[effect_graph]`: 638 assertions / 21 cases PASS
- Full suite: 52,718 assertions / 943 cases PASS
- Recursive conformance self-test: 16 parity/recursion checks plus 8 structured
  public-source contracts PASS
- Checked examples: 28 roots / 52 recursive archives / all 27 public families
  PASS
- `tools/asset_native_source_guard.py`: PASS
- `git diff --check`: PASS at implementation freeze

Build session: `.claude/session-builds/wave9joint`
Queue receipts: `.codex-coordination/queue-results.md`

This is intentionally a supporting receipt. `T-ASSETS-037` later corrected its
non-overlapping capacity fixture, so the root Wave 9 owner retains the final
source-frozen combined rerun and authoritative aggregate receipt.

## Authoritative combined Wave 9 receipt

After the T-ASSETS-037 fixture correction and complete three-lane freeze, root
rebuilt the client, updater, and tests in `.claude/session-builds/wave9final`.
The final fingerprint passes `[t-assets-032]` with 67 assertions / 6 cases and
the complete suite with 52,721 assertions / 943 cases. Archive conformance
self-test, 28-root/52-recursive all-27-family conformance, native-source guard,
and diff check also pass. The truth boundary above is unchanged: this remains
partial until a production consumer invokes the dispatch API.

## Behavioral coverage

Focused tests prove:

1. all nine accepted v1 node kinds map to exact permanent dispatch slots;
2. stable dependency order is used for a graph containing every accepted kind;
3. a missing handler rejects before `begin` or any node callback;
4. begin, node, and commit failures execute the rollback contract;
5. an unsupported future node kind rejects during public-source activation;
6. a corrupted retained schedule rejects before transaction start.

Ordinary-game edited-source execution remains unproven until the three consumer
lanes install real callbacks and call the dispatch API from production paths.
