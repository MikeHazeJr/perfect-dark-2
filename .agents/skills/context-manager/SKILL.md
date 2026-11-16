---
name: context-manager
description: >
  Maintain Perfect Dark 2 canonical context, task summaries, decisions, bug records,
  and durable handoffs when the user requests context upkeep or implementation
  changes relevant project state. Use the scoped project startup for ordinary
  continuation; do not load all context or create a second live task system.
---

# Perfect Dark 2 context manager

Follow `AGENTS.md` and `context/README.md` for current startup, ownership, user authority, and canonical routing. `context/procedures.md` owns build/Git/truncation safety; `context/working-preferences.md` owns collaboration rules. This skill maintains context rather than duplicating those operational rules.

## Canonical records

- `context/README.md`: navigation and scoped startup. Dated summaries retain their original scope; current state comes from Workbench and relevant recent handoffs.
- Canonical Workbench: live items, immutable IDs, dependencies, decisions, notes, ownership, evidence, and gates. Verify `GET /api/meta` before mutations and use its validated API. Process targeted new notes before implementation.
- `context/tasks.md`: concise narrative milestone/lane summary consistent with Workbench, not another authoritative task board.
- `context/session-log.md`: chronological resumable checkpoints with focus, changed surfaces, decisions, evidence, blockers, and next unmet gates.
- `context/constraints.md`: active/removed invariants and rationale. Check before significant changes; preserve index-domain distinctions.
- `context/bugs.md` and `context/systemic-bugs.md`: one-off defects and propagated bug-class records.
- `context/pillars/` and relevant domain/design documents: load and update only when the assigned task needs them.
- `context/retention.md`: preserve evidence/source-freezes and follow it before artifact maintenance. Context upkeep is not cleanup authorization.

`context/` in the canonical checkout wins over parent mirrors. Refresh a mirror only for an identified consumer/handoff after canonical updates, with source/date recorded. Historical receipts, hashes, and commands must not be silently rewritten as current proof.

## Read before writing

1. Follow the scoped README startup and current user assignment. Read the relevant existing record before editing it; do not initialize a parallel context tree.
2. Reuse the trusted hook's injected identity. Claim exact file/item ownership and respect peer source-freezes, the Git lane, and resource queues.
3. Check actual source/evidence when a claim has changed. Keep hypotheses distinct from observed results.
4. Reconcile current content, preserve unrelated history, and append a dated checkpoint when it aids recovery. Workbench status changes require its evidence/model contract.

## Update and verify

- Record decisions, changed constraints, discovered bugs, ownership, and useful evidence as they change. Keep a recoverable checkpoint before compaction; ordinary context saves need no extra approval.
- Update relevant pillar/task summaries in the same coherent implementation unit, and leave a useful session-log handoff at completion or pause. Do not mechanically edit every context file.
- Batch verification around coherent source-frozen units. Code changes use the isolated queued `devtools/build-session.ps1` workflow for affected targets, required automation/runtime gates, and applicable asset guards. Use the test wrapper for the correct runtime PATH. Documentation-only edits use static/diff/link checks.
- Follow procedures for explicit stash paths, pre-operation snapshots, protected owned work, serialized integration, post-merge checks, and file integrity. Never delete a live Git lock, reset to repair a lock, or overwrite the canonical checkout with a worktree copy.
- Worktrees are disabled unless Mike explicitly re-enables them. Preserve user/peer edits in any future integration.

## Keep records navigable

Retain the existing soft size guidance: core task/bug/constraint files around 200 lines, session logs and plans around 500, infrastructure around 300. These are readability guidelines, not deletion triggers. If a separately scoped maintenance task splits a record, keep the original filename as an index, update relative links and README navigation, and preserve dated history. Follow canonical retention rules; this correction does not initiate that restructuring.

## Resume and recovery

For normal startup or compaction, read the index, assigned Workbench lane, relevant recent handoff, and needed constraints/procedures. Verify the current files before resuming. Full-depth `context-recovery` is for explicitly requested recovery or genuinely missing/inconsistent state, not an automatic reread of all project history.

If the task expands into an architectural/product decision, explain options and let Mike choose. Continue tactical work already delegated. Completion reports distinguish implementation, automated/runtime evidence, and remaining human acceptance gates.
