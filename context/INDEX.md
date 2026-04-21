# Context system — navigation

**Primary hub:** [README.md](README.md) (read this first every session).

**Cold start:** [QUICKSTART.md](QUICKSTART.md) → README → [tasks-current.md](tasks-current.md) → [session-log.md](session-log.md) → [constraints.md](constraints.md).

---

## Read order (before coding)

| Step | File | Purpose |
|------|------|---------|
| 1 | [README.md](README.md) | Orientation, links, session history table |
| 2 | [tasks-current.md](tasks-current.md) | What to do now |
| 3 | [session-log.md](session-log.md) | Last sessions (**S281–S361**) |
| 4 | [constraints.md](constraints.md) | Non-negotiable rules |
| 5 | [CRITICAL-PROCEDURES.md](CRITICAL-PROCEDURES.md) | Build verify, git, worktrees |

Domain work: see README § “Domain Files” and § “Plan / Design Files”.

**Menu input UX (controller + mouse):** [designs/menu-controller-input-constraints.md](designs/menu-controller-input-constraints.md) (includes Settings → Controls sanity notes).

---

## Where session history lives

| Range | Location |
|-------|----------|
| **S281–S361** (active, rolling) | [session-log.md](session-log.md) |
| **S280 → S241** (archived S316) | [_archive/session-log-archive-S280-and-older.md](_archive/session-log-archive-S280-and-older.md) |
| **S240 → S157** (archived block) | [_archive/session-log-archive-S240-and-older.md](_archive/session-log-archive-S240-and-older.md) |
| **S1–S119** (chunked) | [_archive/sessions/](_archive/sessions/) |

---

## Other buckets

| Kind | Path |
|------|------|
| Scratch / dated handoffs | [scratch/](scratch/) |
| Superseded designs & audits | [_archive/](_archive/) |
| Daily notes | [daily-logs/](daily-logs/) |

---

## Maintenance (context-manager)

- Keep **session-log.md** under **~500 lines** (rolling window; archive older tiers).
- **One source of truth**