# Milestone 1 base-content coverage

`tools/base_content_coverage.py` derives the base-content coverage matrix from
the live source arrays and constants.  The generated matrix is versioned and
fingerprinted; it preserves the stage-table, solo-stage, and `stagenum` index
domains instead of collapsing them into one guessed ID.

`source.revision` is the latest committed revision affecting the exact matrix
source paths, not repository `HEAD`.  This keeps the checked matrix's
canonical fingerprint reproducible when the tool and matrix themselves are
committed afterward; the source byte manifest still detects source drift.

The matrix contains 417 ordinary-gameplay cells:

- 21 solo stages × Agent/Special Agent/Perfect Agent (63)
- 18 standard Combat Simulator arenas × 6 scenarios (108)
- 30 challenges × 1/2/3/4 players (120)
- 21 network stages × co-op/counter-op × three campaign difficulties (126)

Perfect Dark is listed separately as a 21-row derivation and does not satisfy
the 63-cell solo gate.  All 47 authored arena rows remain in `arena_inventory`;
Bonus and Random rows carry an explicit exclusion reason and are never silently
dropped.

## Matrix, deterministic work plan, resume, and merge

```powershell
python tools/base_content_coverage.py plan `
  --out tools/base-content-coverage/matrix-v1.json

python tools/base_content_coverage.py shard `
  --matrix tools/base-content-coverage/matrix-v1.json `
  --shard-index 0 --shard-count 7 `
  --out tools/base-content-coverage/shard-00-of-07.json

python tools/base_content_coverage.py resume `
  --matrix tools/base-content-coverage/matrix-v1.json `
  --receipts evidence/base-content-coverage/receipts `
  --artifact-base evidence/base-content-coverage/run-root `
  --out evidence/base-content-coverage/resume.json

python tools/base_content_coverage.py merge `
  --matrix tools/base-content-coverage/matrix-v1.json `
  --receipts evidence/base-content-coverage/receipts `
  --artifact-base evidence/base-content-coverage/run-root `
  --out evidence/base-content-coverage/aggregate.json
```

The checked shard artifact is a deterministic coverage work plan, not an
executable gameplay plan: its status is `not_executable`, every cell has
`runner_fixture_status: missing` and
`execution_status: not_executable_until_fixture_exists`, and
`gameplay_commands` is empty.  No per-cell fixture or runtime-driving command
is claimed here.  Fixture generation and runtime-driving infrastructure is a
remaining dependency before any cell can launch through `run.ps1`.

Each work-plan entry has a receipt-conversion command only.  Replace its
portable `<run-root>` placeholder with the retained run directory; all
observation, log, and evidence paths are resolved relative to that root.  The
ordinary client smoke/evidence runner remains `tools/smoke-verify/run.ps1`; an
observation JSON records its result, log/evidence paths, exit code, timeout,
and crash state before the receipt command fingerprints the exact client
binary.  Receipt conversion commands must not be read as gameplay commands.

An observation has this small contract:

```json
{
  "runner": "tools/smoke-verify/run.ps1",
  "execution_mode": "ordinary-client",
  "proof_class": "ordinary-gameplay",
  "status": "pass",
  "exit_code": 0,
  "assertions_total": 1,
  "assertions_met": 1,
  "timed_out": false,
  "crashed": false,
  "log_path": "run/pd-client.log",
  "evidence_paths": ["..."]
}
```

The runner value must be exactly `tools/smoke-verify/run.ps1`; assertions must
be positive and exactly met; and both `log_path` and nonempty retained
`evidence_paths` are required real readable files under `<run-root>`.
Receipt records embed normalized relative paths, byte sizes, and SHA-256
fingerprints for the observation JSON, log, and evidence artifacts.  Any
resume or merge operation that uses an accepted ordinary-gameplay receipt must
provide `--artifact-base`; the tool re-reads and fingerprints every retained
file and rejects missing or mutated artifacts.  Rejected/static receipts may
still be structurally inspected without an artifact base.  Shard receipt
commands use the portable `python` command token so the plan is not tied to
one user's interpreter path.

Release matrix and receipt creation fail closed if the exact source-scoped
revision cannot be obtained.  The revision is the latest committed revision
affecting the matrix source paths, while the source byte manifest detects
uncommitted source drift.

Static and accelerated observations are retained and indexed, but aggregation
never promotes them to ordinary gameplay validation.  A timeout or crash is a
durable classification and remains pending for resume.  Duplicate cell IDs,
matrix/source fingerprint drift, embedded-cell drift, and mixed-source
receipts are rejected.
