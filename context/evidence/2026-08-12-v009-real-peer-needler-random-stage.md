# V-009 real-peer Needler and random-stage verification — 2026-08-12

## Outcome

Two final-source, two-process listen-host/client scenarios pass against the
same isolated `v009peer7` build.

- `needler_effect_real_peer_distribution_smoke`: **87/87 PASS**, exit 0.
  The client starts with no Needler files, authenticates from an independent
  install root, requests the missing typed component and package, verifies
  both SHA-256 identities, publishes them transactionally, admits the public
  weapon/effect/SFX closure, reaches READY, and joins the host's nonzero arena
  session. Both peers equip/render runtime weapon 86. Ordinary firing commits
  the public gameplay/audio transaction and the real world renderer consumes
  the `needler_pink_burst` presentation snapshots.
- `random_meta_real_peer_stage_smoke`: **34/34 PASS**, exit 0. The host selects
  the random multiplayer token, resolves it exactly once before manifest and
  session-catalog construction, publishes concrete `base:mp_skedar`, sends
  `stage_session=1`, and the client resolves that same identity to stagenum
  `0x32`. No token, zero session, identity mismatch, fallback, or fatal remains.

## Frozen receipts

- `.claude/smoke-verify-runs/results-20260812T061926Z.json`
- `.claude/smoke-verify-runs/results-20260812T060956Z.json`
- `.claude/session-builds/v009peer7/v009-full-tests-final.log`: **56,283
  assertions / 1,013 cases PASS**
- `.claude/session-builds/v009peer7/v009-final-guards.log`: native-source guard;
  16 parity + recursion + 9 structured conformance contracts; 28 root / 52
  recursive archives across all 27 families; Needler 1 root / 9 recursive
  archives — all PASS.
- `.claude/session-builds/v009peer7/v009-final-fingerprint-before.txt` and
  `...-after.txt`: 37-file fingerprint
  `746fa68f1d49738888a64a0248b8795a126e2caa4ffd7c6600bb536ba3b6cde6`,
  unchanged across the final Needler run.

## Closed defects

The final receipts close B-1034 through B-1042: package-versus-asset transfer
identity, animation-0 source ownership, typed MP3 EOF bounds, client
distribution initialization, Windows-safe session-only receive paths,
listen-host MP initialization, authoritative server-manifest activation,
arena/random-stage identity and session ordering, and stale lobby input
ownership. Earlier isolated receipts already closed B-1031 through B-1033.

## Truth boundary

This proves production behavior through exact runtime and renderer audit
markers, not a readable pixel comparison. V-009 remains partial while its
explicit readable peer-visual gate and partial T-CATALOG-003 dependency remain
open; this receipt does not overclaim those boundaries.
