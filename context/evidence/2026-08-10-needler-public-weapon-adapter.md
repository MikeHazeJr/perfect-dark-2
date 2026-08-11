# Needler public weapon adapter and direct-audio verification

Date: 2026-08-10 ET

Scope: B-1018 through B-1022, Workbench `T-ASSETS-025` and `V-009`.

## Production result

- Direct mission-script `MP3_*` references now have editable `.pdvoice`
  archives and exact `source_filenum` bindings. The complete static domain is
  547 unique MP3 file symbols and 548 direct audio file references, in addition
  to configured voice aliases.
- Needler creator output now obeys the strict `.pdeffect` descriptor schema and
  names the embedded executable effect identity, not its texture, from
  `spark_ref`.
- Custom `.pdweapon` activation installs a loader-owned `struct weapon` adapter
  from public `weapon.ini` plus compiled held graphs. The private manifest is
  identity/provenance only and is not a second behavior source.
- Specific-spawn setup and lobby receive preserve the selected catalog row's
  authoritative `runtime_index`; they no longer map through the secondary MP
  index domain.

## Automated receipt

- client, updater, and `pd-tests` compilation: PASS in isolated session
  `tassets034`.
- B-1018: 554 assertions / 1 case PASS.
- B-1021: 20 assertions / 1 case PASS.
- B-1022: 10 assertions / 1 case PASS.
- Complete suite: 54,777 assertions / 983 cases PASS.
- Archive scanner selftest: 16 parity cases + recursion + 9 structured source
  contracts PASS.
- Needler recursive conformance: 1 root / 9 checked archives across
  `.pdeffect`, `.pdmesh`, `.pdprojectile`, `.pdtexture`, and `.pdweapon` PASS.
- Checked-in all-family conformance: 28 roots / 52 recursive archives / all 27
  families PASS.
- `tools/asset_native_source_guard.py`: PASS.
- `git diff --check`: PASS.

Primary logs:

- `.claude/session-builds/tassets034/b1022-full.log`
- `.claude/session-builds/tassets034/b1022-conformance-selftest.log`
- `.claude/session-builds/tassets034/b1022-needler-conformance.log`
- `.claude/session-builds/tassets034/b1022-all-family-conformance.log`
- `.claude/session-builds/tassets034/b1022-native-source-guard.log`

## Installed-client receipt

`needler_graph_runtime_visual_smoke` passed 43/43 and exited 0:

`.claude/smoke-verify-runs/results-20260811T030050Z.json`

The retained client log proves:

- nested source model ingest at file 2016;
- nested executable effect ingest as `mod_needler:pink_burst_effect`;
- custom slot defaults at runtime 86 / MP 41;
- public loader adapter install at runtime 86 with model 2016, two functions,
  and rocket-launcher tracking;
- exact `BONDGUN.SOURCE` load from
  `needler.pdweapon::dependencies/assets/models/weapon.pdmesh::model.gltf`;
- generated Needler model submission with 300 vertices / 100 triangles;
- primary/secondary scripted fire, including a generated nested needle model
  render; and
- clean scripted shutdown without ROM/provider fallback.

## Truth boundary

This is a partial validation receipt, not full `V-009` closure. Both captures
show the source-render proof badge, but the first-person frame is heavily white
and does not make the Needler silhouette visually unambiguous. The scenario
also runs with `--no-sound` and does not assert an effect instance, impact,
audio output, or presentation channel. A strengthened ordinary-client run must
prove recognizable held-model presentation plus actual edited effect
gameplay/audio/render output. Restart persistence, child disable/re-enable,
rollback/replacement, and real-peer distribution remain open.

## Where to look

- `port/src/romextract_pdsfx.c`
- `port/src/assetcatalog_load.c`
- `port/src/loader_pool.c`
- `port/src/net/matchsetup.c`
- `port/src/net/netmsg.c`
- `tools/build_needler_mod.py`
- `tools/asset_archive_conformance.py`
