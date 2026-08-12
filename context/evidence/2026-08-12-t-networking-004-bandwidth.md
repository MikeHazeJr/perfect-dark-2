# T-NETWORKING-004 passive bandwidth evidence

## Goal and reason

Replace the hardcoded local `0 kbps` authority input with a passive, durable
measurement from actual ENet traffic. Authority and player-hosted relay
selection need real comparable evidence; a configured value or test-only
injection would leave the production decision disconnected.

## Production contract

- Sample cumulative `ENetHost.totalSentData` over completed two-second windows.
- Require at least 4096 real sent bytes; quiet windows do not fabricate data.
- Persist the best fresh observed lower bound and its wall-clock timestamp for
  up to 30 days.
- Carry the estimate in signed presence v4 bytes 84-87; the frame remains 184
  bytes and the signed body remains 120 bytes.
- Expire remote group reports after 90 seconds.
- Elect highest fresh kbps, then the match initiator on exact/no-data ties,
  then the smallest public handle.
- Remove TURN candidates when reports expire or peers fail, drop, or shut down.

## Verification

- Client and updater build: PASS.
- Explicit tests build: PASS.
- `[t-networking-004]`: PASS, 37 assertions / 5 cases.
- Complete `pd-tests`: PASS, 56,664 assertions / 1,036 cases; retained log
  `context/evidence/2026-08-12-t-networking-004-full-suite.log`.
- Ordinary two-process listen-host/client smoke: PASS, 23/23 assertions,
  `.claude/smoke-verify-runs/results-20260812T151813Z.json`.
- Host log: real match traffic produced `64 kbps`; host `pd.ini` persisted
  `UploadKbpsEstimate=64` with a nonzero measurement timestamp.
- Client log: real match traffic produced `32 kbps`; client `pd.ini` persisted
  `UploadKbpsEstimate=32` with a nonzero measurement timestamp.
- Both processes reached the same match and exited cleanly.
- `git diff --check`: PASS.
- Frozen production/test/smoke manifest fingerprint:
  `4feb049dc393528a09fd67255d9a390100ff0f3495033fa4e0d5cfbeb4c28f43`.

## Truth boundary

This closes measurement, signed distribution, freshness, deterministic
election input, and TURN candidate selection. It does not claim automatic live
listen-host migration or completion of the broader P2P-to-ENet handoff; those
remain T-NETWORKING-008/T-NETWORKING-006 work.
