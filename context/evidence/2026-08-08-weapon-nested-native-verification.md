# T-ASSETS-022 nested weapon native verification — 2026-08-08

## Truthful result

The production-linked native client harness passes all three automated cases.
This closes B-990's scanner, resolution, and lifecycle defects. It does not
claim ordinary gameplay, physical-device, restart, or network proof. A real
client run also exposed B-992: the complete base catalog exhausts the private
custom-weapon slot range, so the exit-after-test harness borrows AR34's loaded
runtime indices. T-ASSETS-022 therefore remains `partial` until that capacity
defect and the ordinary live gate are closed.

## Production changes exercised

- `assetcatalog_scanner.c`: complete structural preflight, deterministic UI /
  audio / animation order, capacity reservation, transactional commit, and
  reverse rollback of created dependency edges and catalog rows.
- `loader_pool.c`: unresolved animation-command catalog audio rejects and
  restores animation, command, and fixup pools instead of falling back to 0.
- `assetcatalog_load.c`: direct weapon load owns its dependency closure;
  already-loaded audio and animation retain before activation; rollback and
  release balance the complete closure.
- `weapon_nested_runtime_harness.c`: calls the real catalog, scanner, loader,
  and lifecycle APIs from the production client after catalog construction.

## Durable receipt

- Source-frozen isolated client build: `context/evidence/2026-08-08-weapon-nested-native-build.log`
  reports `Result: SUCCESS`.
- Runtime log: `context/evidence/2026-08-08-weapon-nested-native-client.log`.
- Direct load/release: log lines 3202-3207 show every nested child and parent
  releasing from `ref=1` to zero.
- Manifest-style coexistence: lines 3216-3226 show parent release preserving
  explicit child ownership (`ref=2->1`) followed by explicit release to zero.
- Unresolved selected sound: line 3232 records
  `command.sound audio_id="harness_missing:missing_audio"`; the case passes
  only when registration rejects and no row or edge leaks.
- Final sentinel: line 3234 records
  `PDWEAPON.NESTED.HARNESS: passed=3 cases=3 result=PASS`; line 3235 records
  smoke result `weapon_nested_harness_pass`, process exit 0.
- The third case corrupts the late animation member after valid audio members;
  its pass condition requires complete rejection and absence of all expected
  nested IDs and owner edges.

## Findings discovered during proof

1. B-990: direct weapon dependency load/release asymmetry.
2. B-990: incremental nested registration leaked earlier rows and edges when a
   later member rejected.
3. B-990: unresolved animation-command audio silently compiled fallback 0.
4. B-990 propagation: repeated audio/animation load reset `ref_count` to one,
   losing a manifest reference; the type-agnostic loaded retain guard fixes the
   class.
5. B-992: a full ordinary catalog has no free private custom-weapon slot.
6. B-991: the same real-client run exposed seven strict base-theme envelope
   failures (`missing id`); this is a separate ordinary-boot blocker owned by
   T-ASSETS-030 and is not ignored by this receipt.

## Residual gates

- Implement non-colliding modern custom-weapon capacity and prove allocation
  after the complete base catalog (B-992).
- Capture ordinary-client edited nested audio and animation gameplay, corrupt
  rejection, restart, and network-distribution proof under T-ASSETS-025 and
  the validation gates.
