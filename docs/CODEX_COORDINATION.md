# Perfect Dark 2 coordination hub

The coordination hub tracks active sessions and FIFO reservations for
exclusive resources. It is operational state, not durable project truth.
Durable tasks, decisions, risks, assets, validation, performance, evidence, and
handoffs live in `Tools/Workbench/data/`.

Command:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1
```

Live local-only state:

```text
.codex-coordination/state.json
```

The canonical state root is the main project copy, even when an agent works in a
git worktree. Set `PD2_CODEX_COORDINATION_ROOT` only for deliberate isolation.

## Session start

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 status
.\Tools\CodexCoordination\CodexCoordination.ps1 register `
  -SessionId "codex-asset-audit" `
  -Goal "Audit and close asset source/runtime parity" `
  -CurrentTask "Workbench migration" `
  -Eta "60m" `
  -Plan "1. Audit; 2. Implement; 3. Verify"
```

Heartbeat whenever the current task changes and at least every 20 minutes:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 heartbeat `
  -SessionId "codex-asset-audit" `
  -CurrentTask "Running conformance" `
  -Eta "15m"
```

## Exclusive resources

Queue builds, test runners, game/smoke runs, multiplayer runs, captures,
interactive editors, extraction/import jobs, and deployments.

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 queue `
  -SessionId "codex-asset-audit" `
  -Type test `
  -Resource test-runner `
  -Title "pdxxx focused tests" `
  -Eta "10m" `
  -Details "Asset archive and runtime source tests"
```

The queue is FIFO per resource. Wait for your turn:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 wait-turn `
  -SessionId "codex-asset-audit" `
  -QueueId "q_..." `
  -WaitTimeoutSeconds 1800
```

When next, claim the resource:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 start `
  -SessionId "codex-asset-audit" `
  -QueueId "q_..."
```

Release it immediately after the operation:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 finish `
  -SessionId "codex-asset-audit" `
  -QueueId "q_..." `
  -Result done `
  -Summary "Focused tests passed" `
  -Evidence ".claude/session-builds/asset-audit/test-results.log"
```

For builds, the coordination queue and `devtools/build-session.ps1` queue are
both required. Coordination prevents cross-surface collisions; the build
wrapper remains the low-level build safety mechanism. Do not pass `-NoQueue`.

## Stale and failed work

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 health
.\Tools\CodexCoordination\CodexCoordination.ps1 flag-stale -QueueId "q_..." -Message "owner is gone"
.\Tools\CodexCoordination\CodexCoordination.ps1 clear-stale -SessionId "codex-audit" -QueueId "q_..." -Summary "validated stale"
```

Use `health -Fix` only when the reported runner or lock is provably stale.

## Operational chat and durable handoffs

Short live message:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 chat `
  -SessionId "codex-asset-audit" `
  -Message "I own extractor and conformance surfaces."
```

Durable handoffs must be Workbench notes attached to the affected permanent
item IDs. Coordination chat can expire and must not be the only record.

## Completion

Finish every live queue item. Update Workbench status and evidence. Then archive
only substantial operational completion:

```powershell
.\Tools\CodexCoordination\CodexCoordination.ps1 complete `
  -SessionId "codex-asset-audit" `
  -Title "Asset archive parity lane" `
  -Summary "Implementation and durable proof completed" `
  -Evidence "V-0001, V-0002"
```
