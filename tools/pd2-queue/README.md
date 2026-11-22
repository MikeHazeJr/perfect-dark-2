# pd2q: the PD2 no-worktree build queue

The queue implements Mike's "PD2 No-Worktree Build Queue" spec (T-TOOLING-020), with its paths on C:, because builds run on C:.

Sessions never build, and never write to the shared dev clone. They edit copies in their own scratch folder and submit a patch. One worker then builds each patch from scratch on the latest dev, tests it, and pushes it to dev. If anything fails, it writes a report instead.

```
C:\PD2\
  dev\                 clean clone of dev; only the queue writes here
  queue\               leases.json, counters.json, agents.json, journal\, tasks\,
                       builds.jsonl, board.jsonl, service.json, service.log
  jobs\<job>\          exists only while that job runs
  scratch\<session>\   each session's changed files and base.txt
  reports\             one Markdown report per job (newest 200 kept)
```

## A session's flow

```powershell
$q = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\tools\pd2-queue\pd2q.ps1'
pwsh -File $q check B-1277                         # landed? claimed? queued? Run it first.
pwsh -File $q claim B-1277 -Session claude-mp      # prints the lease number
pwsh -File $q edit port/src/net/net.c -Session claude-mp   # prints the scratch path to edit
pwsh -File $q renew B-1277 -Lease 418              # every 3 minutes; it also saves the diff
pwsh -File $q checkpoint B-1277 -Lease 418 -Note "reader fixed; writer next"
pwsh -File $q submit B-1277 -Lease 418 -Title "Networking - T-NETWORKING-007: Fix rejoin" `
    -Body .\why.txt -CoAuthor "Claude <noreply@anthropic.com>"
pwsh -File $q wait q97-20261122                    # report path and result
```

- **Owning a lease.** `claim` records the long-lived agent host above the shell: the session's own `claude.exe`, or `codex.exe` or `node.exe` (see `agent-hosts.txt`). It records the host's PID and start time, plus the PC's boot time. A session holds one lease at a time, because it has one scratch folder; a submitted lease does not count.
- **When a lease frees.** It frees the moment its owner exits, the PC reboots, or 10 minutes pass without a renew. If the task has edits, they are saved under `queue\tasks` first. If that save fails, the scratch moves to `scratch\_kept\<session>-<time>`, where no later claim can clear it or mix it into another patch, and a `checkpoint-failed` alarm says where. A scratch reverted to its base clears the saved patch. `checkpoint` and `validate` need a held lease: a queued one's patch was saved at submit.
- **Picking up another session's work.** Another session, or the other agent, can claim the task to get the saved work back. It runs `validate -Result keep|redo -Note ...` before it may submit.
- **Submit checks.** The title must follow the commit standard ("<Area> - <WorkbenchItemID>: <summary>"), and the message must pass `.githooks/commit-msg.py` against the live Workbench. The patch must apply to the current dev. A stale lease number is rejected.
- **The commit message.** `-Body` names a text file that says what changed and why, as every commit here does. It may not carry trailer lines; the queue writes `Refs` and the `PD2-*` trailers itself, and `-CoAuthor` adds a `Co-Authored-By` line last.
- **The board.** `board` lists landed fixes. `claim` refuses an issue that already landed unless it is given `-Reopen "why"`.
- **Other commands.** `break`, `cancel`, `agent codex off -Until <time>`, `resume` and `status` work as the spec describes. `break` saves and clears a session's scratch only for a held lease; a queued lease's patch was saved at submit, and its session may be editing its next issue there. `status` shows the running job, the queue, every lease with its age and time left, slow builds, alarms, and the size of the jobs folder, which is an alarm if anything is left there with no job running.

## The worker

```powershell
pwsh -File tools\pd2-queue\Queue-Service.ps1 -Setup        # layout and the dev clone
pwsh -File tools\pd2-queue\Queue-Service.ps1 -Foreground   # the worker
```

The worker brings the dev clone to origin/dev at startup, after each landing and every five idle minutes, so a session's scratch starts from a recent dev. It does this only while the clone is clean; a dirty clone pauses the queue and nothing is reset.

`-Root <folder>` picks the queue root; otherwise `PD2Q_ROOT`, else `C:\PD2`. `Install-QueueService.ps1 -Root` passes it to the scheduled worker.

A job runs through five states. Each step is written to the job's journal before it starts.

1. **Preparing.** The dev clone must be clean; if it is not, the queue pauses, raises an alarm and resets nothing. The worker fetches and resets dev to origin/dev, then checks the lease is still the job's. It makes `jobs\<job>\src` as a `git clone --shared`, with the bgfx submodules taken from dev's copies, and runs `git apply --3way` on the patch. If the patch moves a bgfx submodule pointer, the worker checks out that revision from the submodule's own remote, or fails the job before building: otherwise it would build the old dependency and publish the new one. Then it waits for the hub's `build` slot. A hub that answers but does not admit the job (its item gone three times, the wait timed out, or `start` refused) sends the job back to the front with the same attempt, and the worker waits a minute. An unreachable hub is only a warning, as the spec says. The wait is a series of one-minute `wait-turn` calls, so a cancel gets through. The worker also sends the hub a heartbeat every five minutes, idle and busy, because the hub drops the items of a session it has not heard from in 6 hours.
2. **Building.** The job folder's own `build-session.ps1 -Target full -Clean` runs with ccache off, so a patch to the build scripts is built with its own changes. The job's time limit for its build and tests, 20 minutes (`-JobLimitMinutes`), starts here, when the hub admits it: a wait while another session holds the build slot does not count. A build or test run still going at the limit is stopped with its whole process tree, and the job fails as a timeout.
3. **Testing.** `run-pd-tests.ps1 -NoBuild` runs, with `-Scope` if the submit named one.
4. **Pushing.** A cancel that came during the tests stops the job here. The worker runs what a normal commit's hooks run: the staged asset guard in the job folder, and `commit-msg.py` on the message. Then it commits exactly the staged patch: the title, the submitted body, then the trailers `Refs` (the title's item, plus the issue when it differs), `PD2-Issue`, `PD2-Queue-Job`, `PD2-Session`, `PD2-Agent` and any `Co-Authored-By`. It fetches again. If dev moved, the job goes back to the front of the queue, up to three attempts. Otherwise it pushes `HEAD:refs/heads/dev`; any other target is refused.
5. **Endings.** The job folder is deleted, the hub slot finished (on an unexpected error too), the lease freed and the report written. When a job lands, the worker resets dev to the new tip, adds a board line and notes it in the Workbench.

The worker runs inside a kill-on-close Windows job object. Every process a job starts (the build wrapper, ninja, the compilers, git) belongs to it, so a worker that exits, crashes or is stopped alone takes its build with it. Recovery never deletes a folder or frees a hub slot under a running build.

When the worker starts, it recovers interrupted jobs:
- A job stopped in Preparing, Building or Testing is requeued at the front. After two interruptions it fails.
- A job stopped in Pushing is marked landed if its `PD2-Queue-Job` commit is on dev, so nothing is pushed twice.
- Leftover job folders are removed.

Each step's output reaches its log through a pipe, never a file handle: pd-tests' native write policy refuses an inherited disk handle, and 14 cases fail otherwise. Windows PowerShell steps get a module path without PowerShell 7's folders; with them, `Get-FileHash` is missing and the build wrapper's storage preflight fails at once.

A clean build slower than 152 s (Mike's 2:32), or slower than 1.5 times the median of the last 20 that finished, is flagged as slow. Builds stopped by a compile error, a cancel or a timeout are left out of the median, since they would pull it down. Milestone 0 measured a cold, clean, full build at 128 s (`context/evidence/2026-11-22-pd2q-m0-build-timing.md`).

## Tests

- `tests\Test-Pd2qLeases.ps1` (Milestone 2): leases, CLI, tasks, agents and the push target. It builds nothing.
- `tests\Test-QueueWorker.ps1` (Milestone 3): three real jobs against a local bare remote. One lands, one fails to compile, and one is rejected after a conflicting change.
- `tests\Test-QueueRecovery.ps1` (Milestone 5): the worker killed while building, then killed right after a push. The first job reruns at the front and lands; the second is marked landed without a second push.

Both worker tests build for real (about 3 minutes a landed job) and hold the hub's build slot, so they are not in CI; the lease test is.
