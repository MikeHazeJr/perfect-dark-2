# Rescue of 2026-11-20

Made 2026-11-21 04:04 UTC by `claude-pd-integration-tooling-20261120` on Mike's order of 2026-11-20 (step 1 of the
Integration and Tooling brief), before the scratch cleanup. Base: dev `3cc332b7`. Workbench item
T-TOOLING-017.

These files lived only outside the repository (two Codex task folders under `Documents\Codex` and one
folder under `%LOCALAPPDATA%\Temp`) or only in the canonical checkout's working copy. This branch keeps
them on origin. It is a store, not a merge candidate: never merge it into dev. A lane that needs a
draft copies or applies it from here through its own reviewed change.

Every file keeps its exact bytes (`.gitattributes` sets `-text` here), so the SHA-256 values that the
receipts record still match. `MANIFEST.sha256` lists the SHA-256 of every file as it was found.

## Found and not found

| Asked for | Found | Where it was | Here |
|---|---|---|---|
| Grid placement drafts (N-1138, claim N-1082) | yes | `Documents\Codex\2026-11-19\task-2\grid-placement-copy` | `grid-task-2/grid-placement-copy/`: `grid-native-placement-complete-copy.patch` (2ac393c4edd5), `grid-native-drag-placement-only.patch` (cc2855140c8f), the superseded and CRLF-rejected copies, baseline, recovered, source and apply-check trees, `PLACEMENT-CHECKPOINT.md` |
| Grid Save As drafts (N-1138, N-1023) | yes | `...\task-2\grid-save-stage-copy` | `grid-task-2/grid-save-stage-copy/`: `grid-save-as-stage-identity.patch` (27a4f65300e3), the superseded copy, source tree, syntax logs |
| task-2 receipts (N-1138) | yes | `...\task-2` | all of `task-2` in `grid-task-2/` (126 files), including `grid-exact-candidate/client-E947F55F-receipt.json` and `grid-exact-candidate/verified-native-07b97527/` (the 162-case native runs). The browser, model-stage and model-UV copies are receipts of work already on dev (973decb7, 2f75be44, 71eb950f). |
| Swarm patches of N-1062 | yes, all three | `%LOCALAPPDATA%\Temp\pd2-skedar-swarm-6aff5927` | GPU immutable snapshot `followup-gpu/swarm-gpu-immutable-snapshot-after-n1055.patch` (378d9e42, matches), scheduler and attack tokens `followup-scheduler/swarm-encounter-scheduler-reviewed-92200bbd.patch` (a1c3e5c9, matches), acceptance checker `followup-acceptance/swarm-encounter-acceptance-final-92200bbd.patch` (7c958d1c, matches), post-baseline checkpoint zip (0a0c92aa, matches) |
| Swarm patches of N-1081 | yes, both | same folder | capsule sweep `native-prep-geometry/swarm-capsule-sweep-92200bbd.patch` (15dbb79e, matches), online observation `native-prep-online/swarm-online-evidence-92200bbd.patch` (fece816c, matches), native readiness checkpoint zip (6bb78998, matches) |
| Also in that folder | yes | same folder | the N-1055 baseline patch `swarm-native-baseline-final-92200bbd.patch` (3f25a29d, already on dev as 7a54e87d), the retained-client and native-build checkpoints (N-1139, N-1140), the rolling brief and the offline receipts, 84 files in `swarm-6aff5927/` |
| `sample-release-copies` with `INTEGRATION.md`, `STARTUP-AB.md` | yes | `Documents\Codex\2026-11-19\task\sample-release-copies` | `sample-release-copies/` (10 files): `INTEGRATION.md` (cc02cddfafb4), `STARTUP-AB.md` (44ab9da88040), `AGENT-REGRESSIONS.md`, the staging and shortcut scripts and their tests |

Not included on purpose: `swarm-6aff5927/library-transfer-current/library_file_transfer.py`, Codex's own
file-transfer helper (not PD2 work, and this repository is public). Not on this disk at all: the
copies those chats kept in their cloud library; the local downloads above are the same files (the zip
SHA-256 values match the notes).

## The canonical checkout's parked working copy

`parked-working-copy/parked-vs-dev-3cc332b7.patch` is `git diff --binary 3cc332b7` of every tracked file the
canonical checkout had modified before the step-4 reset, except the week note and the Workbench data,
which are committed to dev as records. The D-052 files in it are also committed on
`week-pd/movement-d052`. Apply with `git apply --3way`.

The untracked parked files were checked byte for byte against `week-pd/inflight-20261120`
(`68a98015`) and are all there, so they are listed rather than copied:

| Path | Blob | In inflight-20261120 |
|---|---|---|
| `port/include/gameplay_graph_bitrep.h` | `f436e586384e` | same |
| `port/include/gameplay_graph_divshift.h` | `82d95e76ed02` | same |
| `port/include/gameplay_graph_values.h` | `7b825bf0cf0d` | same |
| `port/include/graph_persistent_actor.h` | `917cb2aec666` | same |
| `port/include/maian_vehicle_mod.h` | `2dbe85e19edd` | same |
| `port/include/swarm_audit_pose.h` | `16a5cbeae0b4` | same |
| `port/src/gameplay_graph_bitrep.cpp` | `a3dd43926998` | same |
| `port/src/gameplay_graph_divshift.cpp` | `131974fd89a3` | same |
| `port/src/gameplay_graph_values.cpp` | `61b25b750634` | same |
| `port/src/graph_persistent_actor.cpp` | `236640468dc5` | same |
| `port/src/maian_vehicle_mod.cpp` | `3da8605a2ce3` | same |
| `port/src/swarm_audit_pose.c` | `f6a4b587e764` | same |
| `tests/support/native_hand_lifecycle_oracle.c` | `6d06d1992aa1` | same |
| `tests/support/native_hand_lifecycle_oracle.h` | `bc17a67c64b3` | same |
| `tests/support/native_shooting_flow_oracle.c` | `88b9ab2a4d8f` | same |
| `tests/support/native_shooting_flow_oracle.h` | `3bf2d1b33182` | same |
| `tests/test_gameplay_graph_bitrep.cpp` | `077ab150d2bc` | same |
| `tests/test_gameplay_graph_divshift.cpp` | `65fa3689bda8` | same |
| `tests/test_gameplay_graph_values.cpp` | `aa4cdc5c69d1` | same |
| `tests/test_graph_persistent_actor.cpp` | `3fa90a7c1ced` | same |
| `tests/test_gun_load_target_storage.cpp.inflight-20261117` | `55c26201c27e` | same |
| `tests/test_maian_vehicle_mod.cpp` | `9df85b676163` | same |
| `tools/smoke-verify/tests/complex_ledge_player_smoke.json` | `0bbb5f30d4b0` | same |
| `tools/smoke-verify/tests/complex_ledge_simulant_smoke.json` | `3088762f22f1` | same |
| `tools/smoke-verify/tests/villa_tabletop_player_smoke.json` | `43119b52ad96` | same |
| `tools/smoke-verify/tests/villa_tabletop_simulant_smoke.json` | `88f017b97c42` | same |
