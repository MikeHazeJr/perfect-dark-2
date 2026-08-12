# V-009 Needler visual readability

## Goal

Prove that the editable Needler is readable in the normal client: its held
public model must not obstruct the world, and edited impact colours must remain
visibly distinct through the production renderer.

## Why

Earlier automated effect audits retained exact pink and cyan RGBA, but the
screen was dominated by a pale player-following sheet and bright-scene impacts
saturated to white. Numeric source propagation alone could not establish usable
player-facing output.

## Root causes and fixes

- B-1055: the creator authored the held model at scale 14 and too close to the
  first-person camera. It now uses scale 5 at `(30,-18,-45)`.
- B-1056: the GLTF compiler ignored standard
  `pbrMetallicRoughness.baseColorFactor`, leaving vertices white. Valid factors
  now project into runtime colours; malformed factors reject.
- B-1054: the Needler presentation pipeline additively blended both flare and
  core. The outer flare remains additive while the authored-colour core uses
  alpha blending.

## Evidence

- Held-model client result:
  `.claude/smoke-verify-runs/results-20260812T143544Z.json` — PASS 21/21,
  exit 0.
- Held-model captures:
  `.claude/smoke-verify-runs/screenshots/20260812T103519-needler_viewmodel_visibility_smoke/`.
  Both inspected frames show the pink source model confined to the lower-right
  with the crosshair and world unobstructed.
- Pink effect result:
  `.claude/smoke-verify-runs/results-20260812T144457Z.json` — PASS 46/46,
  exit 0.
- Cyan effect result:
  `.claude/smoke-verify-runs/results-20260812T144658Z.json` — PASS 46/46,
  exit 0.
- Effect captures:
  `.claude/smoke-verify-runs/screenshots/20260812T104258-baseline/` and
  `.claude/smoke-verify-runs/screenshots/20260812T104459-cyan/`.
  Inspected 700/900 ms frames show distinct pink/cyan fragments without a
  white bloom.
- Isolated client/updater/tests build PASS; focused Needler source/material
  contract 36/36; B-1054 11/11; native-source guard and recursive Needler
  conformance PASS.
- Corrected complete suite PASS: 56,611 assertions / 1,031 cases. The only
  rejected full attempt was a stale static pin for the renamed visual fixture;
  after that exact test correction, the full rerun passed.
- Archive scanner selftest PASS: 16 parity cases + recursion + 9 structured
  source contracts. All-family conformance PASS: 28 root / 52 recursive / all
  27 families. Needler conformance PASS: 1 root / 9 recursive.

V-009 stays partial only because T-CATALOG-003 remains a partial dependency;
the Needler readable-output gate itself is complete.
