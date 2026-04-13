# Solo-Online Parity Audit — 2026-04-13

Three low-severity wasted-work gaps found during parity audit. All three fixed in a single commit.

| GAP | Severity | File | Fix | Status |
|-----|----------|------|-----|--------|
| **GAP-1** | LOW | `netmanifest.c:1355` | `manifestSPRescanSetup()` early-exits when `g_NetMode != NETMODE_NONE` — skips pointless SP manifest rescan in MP (manifest already populated by network path). Saves ~1ms per MP stage load. | **CLOSED** `270028d7` |
| **GAP-2** | LOW | `netmsg.c:1023` | `SVC_STAGE_START` mod track handler resolves catalog entry before accepting track ID. If audio file unavailable (late joiner missed ready-gate transfer), logs warning and skips instead of failing silently. Graceful degrade, not a file-transfer fix. | **CLOSED** `270028d7` |
| **GAP-3** | LOW | `mpstats.c:456` | `mpstatsRecordDeath()` score resync flag gated on `g_NetMode == NETMODE_SERVER`. In solo the flag was set every kill but never consumed — wasted write eliminated. | **CLOSED** `270028d7` |

## Merge

- Commit: `270028d7`
- Merge: `0f282221` (--no-ff to dev)
- Build: Client + server clean, no new warnings
- Line counts post-merge: netmanifest.c 1632, netmsg.c 5829, mpstats.c 464 (total 7925)
