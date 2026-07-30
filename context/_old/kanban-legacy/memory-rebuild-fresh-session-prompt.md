# Fresh Session Prompt: Apply Rebuilt PD2 Memories

Use this prompt in a fresh Codex session after clearing memory:

```text
I explicitly want you to update Codex memory for this project.

Workspace: C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike

Before doing any project work, read this file exactly:
C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\tools\kanban\memory-rebuild.md

That file is the rebuilt durable memory set for Perfect Dark 2. Apply each `## Task Group:` section in that file verbatim as the new project memory. Do not summarize, reinterpret, merge, or resurrect older conflicting memories. Exclude anything not present in that file. The expected result is 17 memory entries.

After applying the memories, report:
- The number of memory entries applied.
- The exact titles applied.
- Confirmation that the removed old memories were not restored.
- Confirmation that the key corrected rules are present: typed `*.pdxxx` asset archives, `.pdmod` transport-only, catalog as asset-reference source of truth, controller-first interaction surfaces, and Kanban stale-card checks.
```
