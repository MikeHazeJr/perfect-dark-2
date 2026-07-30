# Commit Message Standard

> Area: Tooling. Status: SHIPPED. Updated 2026-07-30 for the repo-local
> Workbench.

PD2 commits are anchored to permanent Workbench item IDs. The item must exist
before the commit so its ownership, dependencies, truth status, and evidence
can be reviewed independently of Git history.

## Format

```text
<Area> - <WorkbenchItemID>: <one-line summary>

<paragraph explaining what changed and why>

Refs: <WorkbenchItemID> [, B-NNN]
```

- `Area` must match the item's `area` field after ignoring spaces, hyphens, and
  underscores.
- `WorkbenchItemID` must exist in
  `Tools/Workbench/data/roadmap.json`. IDs are assigned by the Workbench API,
  permanent, and never reused.
- The full subject is at most 96 characters.
- The body explains the change and includes a `Refs:` trailer containing the
  same Workbench item ID.
- Merge, generated revert, and rebase autosquash subjects are exempt.

Example:

```text
Tooling - T-TOOLING-001: Replace Kanban with Workbench

The local Workbench now owns durable planning truth, decisions, evidence, and
note state. The legacy Kanban records remain immutable migration history.

Refs: T-TOOLING-001
```

## Enforcement

`.githooks/commit-msg.py` reads the Workbench roadmap and rejects:

- placeholder or malformed subjects;
- unknown Workbench IDs;
- an area that does not match the referenced item;
- missing explanatory body text;
- a missing or inconsistent `Refs:` trailer.

Install or revalidate the hooks:

```powershell
pwsh tools/install-githooks.ps1
```

The installer also runs `tools/asset_native_source_guard.py`, which now checks
the active asset-contract items in Workbench instead of frozen legacy card
records.

Bypass with `git commit --no-verify` only when Mike explicitly authorizes it.
Existing history is not rewritten.
