# PD2 build queue: Milestones 1 to 6 (T-TOOLING-020)

Run 2026-11-22 by claude-pd-integration-20261121 on Shadowbane, with the spec's paths on `C:\PD2` (Mike: "Builds on C:").
Milestone 0, the clean build timing, is in `context/evidence/2026-11-22-pd2q-m0-build-timing.md`.
The per-milestone records are in `C:\PD2\reports\m<n>-*.md`.

## Milestone 1: layout and dev checkout

`Queue-Service.ps1 -Setup` at 01:56 UTC, 175 s (record: `C:\PD2\reports\m1-layout.md`). Every check passed:
- `C:\PD2\{dev,queue,jobs,scratch,reports}` exist.
- `git -C C:\PD2\dev status --porcelain` prints nothing.
- HEAD, origin/dev and GitHub's dev are all `832fa082`.
- The clone was seeded from the main checkout's objects (`--reference-if-able`, then `--dissociate`), so it has no alternates and shares nothing with it.
- The three bgfx submodules are at their pinned commits. They came from GitHub at depth 1, as CI fetches them: the main checkout's copies are shallow, and git refuses a shallow repository as `--reference` (the first attempt failed on that).
- The main checkout's HEAD (`week-pd/integration-tooling` `efeb7007`), its index entries and its `git status --porcelain` hash were the same before and after.
- The clone takes 2.49 GiB; C: had 165.2 GiB free afterwards.

## Milestone 2: lease store and CLI

`tools/pd2-queue/tests/Test-Pd2qLeases.ps1`: 80/80, in CI on every push. It builds nothing. It checks:
- claim then renew keeps a lease;
- a lease frees after 10 minutes without a renew, after a changed boot time, after its owner exits, and when the PID is reused with another start time;
- a submit with an older lease number is rejected, naming both numbers;
- a claim on a landed issue refuses without `-Reopen`;
- break frees the lease and logs it;
- tasks: a checkpoint survives a killed session; another agent's claim restores the patch; it cannot submit before `validate`;
- `agent codex off` refuses Codex claims, and `-Until` turns the agent back on by itself;
- the push target: only `refs/heads/dev`; main, `Dev`, `dev2`, a tag and `HEAD:refs/heads/main` are refused;
- the commit message: a body carrying its own trailer line is refused, and so is a co-author without an email;
- where the main checkout's Workbench is present, submit runs `.githooks/commit-msg.py` and refuses an item the Workbench does not know;
- `Refs` names the title's item and the bug id when they differ;
- one session holds one lease at a time, since it has one scratch folder, with names compared without case, as Windows compares folders; a name ending with a dot or naming a device (CON, NUL, COM1) is refused; breaking a queued lease leaves the session's next issue in its scratch;
- reverting every edit clears the saved patch;
- a file deleted with `edit -Delete` and then edited again is edited: the patch carries the new text and no deletion;
- checkpoint and validate need a held lease;
- a job cancelled before it runs (cancel, or a break of its queued lease) gives its patch back to the task, and the next claim restores it;
- a checkpoint that fails as a lease frees moves the scratch to `scratch\_kept` and raises an alarm naming it, and the same session's reclaim restores the older saved patch without touching the kept edits;
- the lease owner: in this session, claim records the session's own `claude.exe`.

## Milestone 3: the worker against a local bare remote

`tools/pd2-queue/tests/Test-QueueWorker.ps1`: 20/20 (02:02 to 02:09 UTC; record `C:\PD2\reports\m3-worker.md`), and 31/31 after the review fixes (05:54 to 06:05 UTC; the last 9 re-checked on the kept fixture). It passed 31/31 again in one run on d00c3201, the head that was merged (08:55 to 09:06 UTC).
- The test makes a local bare remote from the main checkout's dev, and the worker runs in the foreground against it. Builds are real and clean with ccache off, pd-tests are real, and nothing reached GitHub.
- **Job 1** (a comment in `port/src/gdl_span.c`) landed.
  - Its clean build took 135.7 s, under Mike's 152 s. pd-tests took 22.4 s: "All tests passed (718811 assertions in 2618 test cases)".
  - The commit is exactly the patch. Its message has the submitted body, then `Refs`, `PD2-Issue`, `PD2-Queue-Job`, `PD2-Session` and `PD2-Agent`, with the co-author last.
  - `pd2q check` shows the issue finished; a new claim on it refuses.
- **Job 2** (a line that is not C) failed. Its report holds the three gdl_span.c compile errors and "Compile [Full] FAILED".
- **Job 3** (README.md) was rejected before building, after the same line changed on the remote: "Applied patch to 'README.md' with conflicts".
- After the review fixes, **job 4** is cancelled during its tests and never pushed. **Job 5** moves the bx submodule to a revision that does not exist, and fails before building. **Job 6** moves bx to its pin's parent, a real upstream revision the depth-1 seed lacks; the worker fetches it from bx's own remote, after git submodule sync, and checks it out. A test hook then cancels the job before it builds.
- The jobs folder was empty after the jobs, `builds.jsonl` has the step times, and the dev clone was reset to the new tip.

The earlier runs failed on the first two Windows pitfalls below, both fixed before the passing run.

## Milestone 4: the first real job to GitHub dev

This file is that job. It went through the queue: `pd2q claim`, then `edit -New`, `submit -Body -CoAuthor`, and the worker in the foreground. See the Workbench item T-TOOLING-020 for its commit, its CI run and its landed evidence.

## Milestone 5: recovery

`tools/pd2-queue/tests/Test-QueueRecovery.ps1`: 11/11 on its first run (01:50 to 02:01 UTC). After the review fixes it is 25/25 (05:03 to 05:19 UTC). Scenario 5, added for the review of 38bfcfe0, passed 10/10 on its own (`-Scenario 5`, 06:25 to 06:29 UTC). Scenarios 6 and 7, added for the review of 84f0dcc1, passed with scenario 2: 22/22 (`-Scenario 2,6,7`, 07:03 to 07:15 UTC, on d00c3201). On d00c3201, the head that was merged, every scenario passed: also 1 and 2 (15/15, 09:07 to 09:17 UTC) and 3 to 5 (20/20, 09:19 to 09:28 UTC). A full run that began at 08:46 stopped at scenario 2's first check (the worker did not exit 97 after its push) while other lanes were building and testing; its fixture and logs were not kept, and scenario 2 passed in both runs since. It uses a local bare remote with real clean builds, and nothing reached GitHub.
- **The worker stopped alone while Building,** about 20 s into the compile (Stop-Process on the worker, not its tree). Its kill-on-close job object ended every build process with it: powershell, ninja, cmake, the compilers and as. The job folder was left behind.
  - The restarted worker finished the dead worker's hub build slot and removed the folder. It requeued the job at the front as attempt 2 ("interrupted during building").
  - The job then landed: submitted > preparing > building > queued > preparing > building > testing > pushing > landed. Its report says "All tests passed (718811 assertions in 2618 test cases)".
- **Killed right after the push,** before the journal recorded it (a test hook exits with 97 there). The commit was on the remote once.
  - On restart the worker found the job's `PD2-Queue-Job` commit on dev and marked the job landed. It did not push again, and logged "was pushed before the restart ... never pushed twice".
  - That restart's dev reset was made to fail (a test hook): the job keeps one ending, landed, and a `dev-reset` alarm is raised.
- **An unexpected error after testing,** while the job holds its hub slot (a test hook throws). The job fails with that error, nothing is pushed, and its hub slot is finished rather than left running.
- **Cancelled while waiting for the build slot,** which the test held as another hub session: the cancel got through between one-minute admission polls (shortened for the test), the job ended cancelled and its hub item was finished. The worker's heartbeat refreshed its hub session during the wait.
- **Waited past its time limit for the build slot** (scenario 5): the test held the slot for 80 s against a one-minute `-JobLimitMinutes`. The job was still waiting at 80 s, then built for 62.2 s after admission and was stopped as a timeout ("the job passed its 1 minute limit while building"). The limit starts at admission and still stops a long build. Its `builds.jsonl` row says `built: false`, so the slow-build median leaves it out.
- **A session edited the dev clone during a build** (scenario 6): the test appended a line to the clone's README.md while job 6 was building. Job 6 landed. The edit was still there, dev was not reset to the new tip, and the queue was paused ("dev checkout is dirty: M README.md") with a `dev-dirty` alarm. After the test reverted its edit, `pd2q resume` worked.
- **Stopped inside an ending** (scenario 7): a test hook stopped the worker (exit 98) right after job 7's "cancelled" journal line, with its task, cancel request and lease not yet updated. The restart freed the lease, gave the task one ending note and removed the cancel request, and its log names the recovery. A new claim restored the saved patch, and a later start added no second note.
- Afterwards the jobs folder was empty.

Not run here, for Mike:
- installing the worker as a scheduled task at logon (`tools/pd2-queue/Install-QueueService.ps1`);
- a real reboot with a held lease. The lease test already frees a lease when the boot time changes.
- The spec's Recycle Bin check covers test installs inside a job folder. The queue runs no smokes yet (`-Smoke` is recorded only), so job folders hold only regenerable build output, which the spec deletes outright.

## Milestone 6: switch-over and disk report

- AGENTS.md has the queue section. Until the worker runs as a service, the session-build flow stays the default.
- Disk report (`C:\PD2\reports\m6-disk.md`). None of these is a git checkout, and nothing was removed:
  - `D:\PD2-wt\bgfx-parity-evidence` 387 MiB, kept;
  - `D:\PD2-wt\shared-base-14` 405 MiB, until base 15 exists;
  - `.claude\session-builds\cw` 503 MiB, Core Weapons' live build folder;
  - `.claude\smoke-storage` 3.4 GiB, kept;
  - `.claude\worktrees` is absent.

## Three Windows pitfalls the tests found

- **The module path.** pwsh 7 passes its module folders to a `powershell.exe` it starts with `Start-Process`. Windows PowerShell then loses `Get-FileHash`, and the build wrapper's storage preflight fails at once (build step 0.5 s, exit 1). The worker gives Windows PowerShell steps a module path without the PowerShell 7 folders.
- **pd-tests output.** With stdout redirected to a file, 14 native write policy cases fail (exit 14). Job steps now write through pipes, and .NET copies each pipe to its log.
- **Where git.exe is.** GitHub's Windows runner has no `C:\Program Files\Git\mingw64\bin\git.exe`, so PR 41's first CI run stopped. `Get-Pd2qGitExe` now tries Git for Windows' known paths, then `git.exe` on PATH, and still starts only an .exe by its full path.

## Review findings fixed before landing (Codex, PR 41)

- One held lease per session: a second would share the session's one scratch folder.
- A scratch reverted to its base clears the saved patch.
- A failed checkpoint as a lease frees moves the scratch to `scratch\_kept` and raises an alarm. `checkpoint` and `validate` need a held lease.
- Hub admission is checked. A hub that answers and refuses sends the job back to the front, and the worker waits; an unreachable hub is still only a warning, as the spec says.
- The admitted hub slot is finished on every exit path, unexpected errors included.
- A cancel that arrives during the tests stops the job before it pushes.
- `break` on a queued lease no longer checkpoints or clears its session's scratch, which may hold the next issue.
- A failed dev reset after a landing is an alarm; the landing keeps its one ending.
- Before committing, the worker runs the staged asset guard and `.githooks/commit-msg.py` (against the live Workbench), the checks a normal commit's hooks run. `submit` runs the message check early.
- A job cancelled or dropped before it runs puts its task back to started, so the next claim restores the submitted patch.
- Session names compare without case, and names Windows would not keep as their own folder (a trailing dot, a device name) are refused.
- The worker runs in a kill-on-close job object, so a worker that dies alone takes its build with it.
- A patch that moves a submodule pointer is built with the revision it publishes, or fails before building.
- `Queue-Service.ps1 -Root` picks the queue root, and the installer passes it.
- The worker heartbeats the hub every five minutes, and a cancel reaches a job that waits for the build slot.
- Patch paths come from git (git apply --numstat -z, plus each rename's old side), so renames and names with spaces survive submit and handoff.
- Workbench writes need the main checkout's Workbench (both roots), and the push boundary compares verbatim, NUL-separated paths.
- The worker brings the dev clone to origin/dev at startup, after each landing and every five idle minutes, while it is clean.
