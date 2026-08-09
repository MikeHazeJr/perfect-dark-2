# B-992 runtime slot capacity

**Workbench:** `T-ASSETS-022`  
**Session:** `luna-runtime-slots-20260809`  
**Status:** implementation complete; source-frozen build, focused automation,
native-source guard, and installed-client capacity proof passed

## Defects found

The ten-pair custom weapon reservation was exhausted by the complete base
catalog before a creator weapon could allocate. The allocator treated every
authored base runtime weapon without a matching multiplayer-table row as a new
custom weapon. Inventory items and devices therefore consumed private pairs
they did not need. A second hardcoded 96-row bound in the held-weapon graph
runtime would have made any later expanded runtime identity silently graph
inert.

Custom SFX used only 64 private IDs even though `union soundnumhack` reserves
11 bits for the sound identity. Rows after 64 could register but could not be
reached by the production playback router.

## Representation-safe implementation

- Base-only authored weapon rows retain their authored runtime identity and
  use `mp_index = -1`; they no longer spend a custom pair.
- The private MP range now occupies the remaining identities through index 63:
  23 pairs from `0x29` through `0x3f`. This preserves the existing persisted
  64-bit random-filter field exactly.
- Paired runtime weapon identities occupy `0x56` through `0x6c`. The maximum
  remains representable in legacy signed-s8 runtime fields.
- The catalog manager, loader pools, and held-weapon graph runtime cover all
  109 runtime identities. Compile-time assertions bind the duplicate domains.
- The random-filter pack/unpack mask is unsigned, making bit 63 defined.
- The private sound range occupies every remaining 11-bit identity from
  `0x60a` through `0x7ff`: 502 creator SFX/voice rows. Public identity remains
  the catalog ID; these integers remain private last-mile adapters.

No save or network schema changed. Catalog IDs remain authoritative at public,
save, wire, manifest, and creator-tool boundaries.

## Required production proof

The existing generated nested-capacity fixture now requires the real client to:

1. allocate `capacity:weapon_new` after the complete base catalog has booted,
   without borrowing a donor identity;
2. register 70 editable nested `.pdsfx` archives;
3. rebuild the production sound reverse index and start all 70 through
   `sndStart`, with row 69 above the former ceiling; and
4. preserve the existing 72-row/70-effect-edge acceptance and late-corruption
   rollback checks.

The smoke forbids both custom-slot failure diagnostics.

## Verification state

A supporting isolated compile at 2026-08-08 21:51 ET passed the updater but
the client stopped in an overlapping, in-flight `T-ASSETS-033` scanner edit:
`catalogReloadInvalidatedTypedAssets` lacked its declaration. No B-992 source
diagnostic was emitted. This run is not authoritative. Its log remains at
`.claude/session-builds/b992slots/_build-session.out.log` until the final
receipt is captured.

The first T-ASSETS-033 frozen interval then produced a matching-source isolated
client/updater/tests compile and focused capacity pass: 85 cases / 4,600
assertions across sound slots, weapon-manager bounds, the 64-bit random pool,
and spawn-weapon identity consumers. T-ASSETS-033 later thawed for its own live
activation correction, so this remains supporting rather than the final
combined frozen-source receipt.

The installed production client capacity smoke passed after that thaw at
`.claude/smoke-verify-runs/results-20260809T021136Z.json` (11/11 assertions,
harness 2/2, clean exit). After the complete base walk it allocated the new
catalog owner as runtime weapon 86 / MP weapon 41, registered the generated
70-SFX closure, and resolved and started every row through
`catalogResolveSound` plus `sndStart`. The last row,
`capacity:sfx_069`, played through private sound identity 1615, above the old
64-slot boundary, with no weapon/sound custom-slot failure. Because the
neighboring catalog source was not frozen for this run, this is durable live
behavioral evidence but not the final combined source-fingerprint receipt.

After T-ASSETS-033 refroze at its exact corrected source, final isolated
verification passed against one unchanged 38-file source/test/CMake/tool
fingerprint:

- before and after SHA-256:
  `810b88239496848eb2ce89b5d30bcb49c860b89a3cf993a003fe080a1acd6c92`;
- client and updater compile PASS, followed by the explicit `tests` target
  compile PASS at `.claude/session-builds/b992frozen/_build-session.out.log`;
- focused capacity bands PASS 4,600 assertions / 85 cases;
- `python tools/asset_native_source_guard.py` PASS; and
- frozen installed-client smoke PASS 11/11, harness 2/2, clean exit at
  `.claude/smoke-verify-runs/results-20260809T022556Z.json`.

The final client repeats the post-base allocation at runtime 86 / MP 41 and
starts all 70 public nested SFX through sound identity 1615 with no slot
failure. This proves the capacity boundary and real playback route only.

Ordinary edited-weapon gameplay, load/release/restart, repeated-owner behavior,
and network distribution remain `T-ASSETS-025`; the capacity harness must not
be cited as those proofs.

## Root combined Wave 10 receipt

After every neighboring lane refroze, the root `wave10final` build compiled the
client, updater, and tests. The complete suite passed 53,264 assertions in 949
cases; conformance passed 28 roots, 52 recursive archives, and all 27 families;
the native-source guard and diff check passed. Logs are retained under
`.claude/session-builds/wave10final/`.
