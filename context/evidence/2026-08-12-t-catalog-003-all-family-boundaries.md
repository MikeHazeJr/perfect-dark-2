# T-CATALOG-003 — comprehensive all-family path boundary verification

Date: 2026-08-12

## Outcome

The shared public-source path contract is connected and verified through all
27 supported typed archive families. The installed client consumed loose typed
archives, nested `.pdmod` archives, and received PDCA installs at 128 and 1023
bytes and rejected every 1024-byte candidate before catalog, FileProvider,
runtime, dependency, or loader state could survive.

The comprehensive run exposed and fixed three final gaps:

- B-1047: `.pdeffect` source member mirrors truncated below `FS_MAXPATH`.
- B-1048: the checked-in editable example named an unsupported shader pipeline.
- B-1049: BODY/HEAD admission checked an assumed `model.obj` suffix instead of
  resolving the actual public mesh member inside the exact path buffer.

## Durable receipts

- Installed client: `.claude/smoke-verify-runs/results-20260812T120223Z.json`
  — PASS 43/43, exit 0.
- Accepted production paths: 162 family/ingress loads PASS.
- Identity equality: 171 exact catalog to FileProvider to runtime comparisons.
- Fail-closed boundary: 81/81 over-cap family/ingress rows are absent.
- Focused `[T-CATALOG-003]`: 3,654 assertions / 20 cases PASS.
- Focused `[b1047]`: 9 assertions / 1 case PASS.
- Full `pd-tests`: 56,562 assertions / 1,027 cases PASS.
- `asset_native_source_guard.py`: PASS.
- Conformance selftest: 16 parity cases, recursion, and 9 structured source
  contracts PASS.
- Recursive examples: 28 root archives / 52 checked archives / all 27 families
  PASS.
- Frozen source/test/tool/example manifest:
  `7a2d5a1fbf97e0eed1f81287823a267eb5d8d923d5f455f2933a52408427c3e7`.

The earlier timing-only receipt
`.claude/smoke-verify-runs/results-20260812T115931Z.json` is rejected because a
clean extraction exceeded the old 70-second scripted exit before any catalog
probe ran. The scenario now allows 110 seconds for a clean first launch; no
production assertion was weakened.
