# Git Pre-Op Snapshot Tools

Belt-and-suspenders tooling for risky git operations (merge, rebase, reset).
Opt-in — not enforced. Sessions that want extra safety can use these.

## Usage

**Before** a risky git operation:
```bash
bash devtools/git-snapshot.sh
```
Records HEAD SHA, `git diff --stat`, and line counts of all changed files
to `.claude/git-snapshots/snapshot-YYYYMMDD-HHMMSS.txt`.

**After** the operation:
```bash
bash devtools/git-verify-snapshot.sh
```
Compares current file line counts against the most recent snapshot.
Exits non-zero if any file shrank. Optionally pass a specific snapshot path:
```bash
bash devtools/git-verify-snapshot.sh .claude/git-snapshots/snapshot-20260413-120000.txt
```

## Output directory

Snapshots go to `.claude/git-snapshots/` which is in `.gitignore`.
They accumulate until manually cleaned — they're small text files.
