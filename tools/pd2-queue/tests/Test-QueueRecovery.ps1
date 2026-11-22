#Requires -Version 7.0
<#
Milestone 5 recovery checks of the build queue spec, in the foreground against a
local bare remote (real clean builds; nothing reaches GitHub):
  1. kill the worker while a job is Building: on restart its folder is gone and
     the job reruns at the front of the queue, then lands;
  2. kill it during Pushing, after the push: on restart the job is marked Landed
     and there is exactly one commit for it (never pushed twice);
  3. an unexpected error while the job holds its hub slot: the job fails,
     nothing is pushed, and the slot is finished;
  4. a cancel while the job waits for the build slot another session holds;
  5. a job that waits for the build slot longer than its time limit still
     builds: the limit counts from admission, and it still stops a long build;
  6. a session writes to the dev clone while a job builds: the job lands, the
     edit is kept, and the queue pauses instead of resetting dev;
  7. the worker dies inside a job's ending, after its journal line: the next
     start finishes the ending once and frees the lease.
-Scenario runs some of them ('5', or '1,5'); the default runs all seven.
A reboot with a held lease is covered by Test-Pd2qLeases.ps1 (a changed boot
time frees the lease at once); this test does not restart the PC.
#>
param(
    [string] $Source = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike',
    [string] $Scenario = '1,2,3,4,5,6,7',  # a string: a -File argument "1,5" stays one
    [switch] $KeepFixture
)
$ErrorActionPreference = 'Stop'
$want = @($Scenario -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ } | ForEach-Object { [int]$_ })
if (-not $want -or @($want | Where-Object { $_ -lt 1 -or $_ -gt 7 })) { throw "-Scenario takes numbers from 1 to 7, not '$Scenario'" }
$pwsh = (Get-Process -Id $PID).Path
$here = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $here 'Pd2Queue.psm1') -Force
$git = Get-Pd2qGitExe
$cli = Join-Path $here 'pd2q.ps1'
$service = Join-Path $here 'Queue-Service.ps1'
$root = 'C:\PD2\m5-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$remote = Join-Path $root 'test-remote.git'
$checks = New-Object 'Collections.Generic.List[string]'
function Check([bool] $Ok, [string] $Label) { if (-not $Ok) { throw "Check failed: $Label" }; $checks.Add($Label); "  ok  $Label" }
function Q([string[]] $Arguments) {
    $ErrorActionPreference = 'Continue'
    $out = & $pwsh -NoProfile -File $cli @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
    return [pscustomobject]@{ Code = $LASTEXITCODE; Out = $out }
}
function Events([string] $Job) { @(Get-Content -LiteralPath (Join-Path $root "queue\journal\$Job.jsonl") | ForEach-Object { ($_ | ConvertFrom-Json).event }) }
function Submit([string] $Issue, [string] $Session, [int] $OwnerPid, [string] $Comment, [string] $File = 'port/src/gdl_span.c') {
    $r = Q @('claim', $Issue, '-Session', $Session, '-OwnerPid', $OwnerPid)
    if ($r.Out -notmatch 'lease (\d+) on') { throw "claim failed: $($r.Out)" }
    $lease = $Matches[1]
    $f = (Q @('edit', $File, '-Session', $Session)).Out.Trim()
    [IO.File]::WriteAllText($f, "/* $Comment */`n" + [IO.File]::ReadAllText($f))
    $r = Q @('submit', $Issue, '-Lease', $lease, '-Title', "Tooling - $($Issue): $Comment")
    $m = [regex]::Match($r.Out, 'q\d+-\d{8}')
    if (-not $m.Success) { throw "submit failed: $($r.Out)" }
    return $m.Value
}
function Start-Worker([hashtable] $Env = @{}, [string[]] $Extra = @()) {
    $saved = @{}
    foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $Env[$k]) }
    try {
        return Start-Process -FilePath $pwsh -ArgumentList (@('-NoProfile', '-File', "`"$service`"", '-Foreground', '-Remote', "`"$remote`"", '-IdleExitSeconds', '3', '-HubSession', 'pd2-queue-m5test') + $Extra) `
            -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root "worker-$([guid]::NewGuid().ToString('N').Substring(0,6)).log") -RedirectStandardError (Join-Path $root "worker-$([guid]::NewGuid().ToString('N').Substring(0,6)).err")
    } finally { foreach ($k in $Env.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) } }
}
$ps51 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$hubScript = Join-Path $Source 'Tools\CodexCoordination\CodexCoordination.ps1'
function HubCmd([string[]] $A) { $ErrorActionPreference = 'Continue'; (& $ps51 -NoProfile -ExecutionPolicy Bypass -File $hubScript @A 2>&1 | Out-String).Trim() }
function Get-Descendants([int] $RootPid) {
    $all = @(Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, CreationDate, Name)
    $found = @(); $frontier = @($RootPid)
    while ($frontier) {
        $next = @()
        foreach ($p in $frontier) { foreach ($c in @($all | Where-Object { $_.ParentProcessId -eq $p })) { $found += $c; $next += $c.ProcessId } }
        $frontier = $next
    }
    return $found
}
function Wait-Event([string] $Job, [string] $Event, [int] $Seconds) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) { if ((Events $Job) -contains $Event) { return $true }; Start-Sleep -Milliseconds 500 }
    return $false
}
# Scenarios 4 and 5 hold the hub's build slot under this session, as another
# session's build would.
$holder = 'pd2-queue-m5holder'
function Release-Holder([string] $QueueId) {
    [void](HubCmd @('finish', '-SessionId', $holder, '-QueueId', $QueueId, '-Result', 'done', '-Summary', 'Test-QueueRecovery.ps1 released its build-slot holder', '-Evidence', 'tools/pd2-queue/tests/Test-QueueRecovery.ps1'))
}

$sleeper = $null
$env:PD2Q_ROOT = $root
$env:PD2Q_SUBMODULE_SOURCE = Join-Path $Source '.git\modules'
$env:PD2Q_CANONICAL = Join-Path $root 'no-main-checkout'  # T-M5-* are not Workbench items
try {
    & $git clone --quiet --bare --no-hardlinks --branch dev --single-branch $Source $remote
    $r = & $pwsh -NoProfile -File $service -Setup -Remote $remote 2>&1 | Out-String
    Check ($r -match 'layout ready') 'setup'
    $sleeper = Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-Command', 'Start-Sleep 3600') -PassThru -WindowStyle Hidden

    # 1. Killed while Building.
    if ($want -contains 1) {
        $jA = Submit 'T-M5-001' 'm5-a' $sleeper.Id 'pd2q Milestone 5 kill during build'
        $w = Start-Worker
        Check (Wait-Event $jA 'building' 900) 'the job reached Building'
        Start-Sleep -Seconds 20  # well into the compile
        # Stop the worker alone, not its tree, as a crash would: its job object
        # must end the build it started before recovery touches the folder.
        $kids = @(Get-Descendants $w.Id)
        Check ($kids.Count -ge 2) "the build runs as the worker's child processes ($(($kids | ForEach-Object Name | Select-Object -Unique) -join ', '))"
        Stop-Process -Id $w.Id -Force; $w.WaitForExit()
        Start-Sleep -Seconds 3
        $alive = @($kids | Where-Object { $p = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue; $p -and [math]::Abs(($p.StartTime - $_.CreationDate).TotalSeconds) -lt 2 })
        Check ($alive.Count -eq 0) "stopping the worker alone ends its build processes too ($($alive.Count) left: $(($alive | ForEach-Object Name) -join ', '))"
        Check (Test-Path -LiteralPath (Join-Path $root "jobs\$jA")) 'a killed worker leaves the job folder behind'
        $w = Start-Worker
        $w.WaitForExit()
        $ev = Events $jA
        Check ($ev[-1] -eq 'landed') ("after the restart the job landed: " + ($ev -join ' > '))
        $requeue = @(Get-Content -LiteralPath (Join-Path $root "queue\journal\$jA.jsonl") | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { $_.event -eq 'queued' })
        Check ($requeue.Count -ge 1 -and $requeue[0].front -and $requeue[0].attempt -eq 2 -and $requeue[0].detail -like 'interrupted*') 'it was requeued at the front as attempt 2'
        Check (@(Get-ChildItem -LiteralPath (Join-Path $root 'jobs') -Force).Count -eq 0) 'the job folder is gone'
    }

    # 2. Killed during Pushing, after the push.
    if ($want -contains 2) {
        $jB = Submit 'T-M5-002' 'm5-b' $sleeper.Id 'pd2q Milestone 5 kill after push'
        $w = Start-Worker @{ PD2Q_TEST_CRASH_AFTER_PUSH = '1' }
        $w.WaitForExit()
        Check ($w.ExitCode -eq 97 -and (Events $jB)[-1] -eq 'pushing') 'the worker died after pushing, before the journal said so'
        $pushed = @(& $git -C $remote log dev --grep "PD2-Queue-Job: $jB" --format=%H)
        Check ($pushed.Count -eq 1) 'the commit reached the remote once'
        # The restart's dev reset after the landing fails (test hook): an alarm, and
        # the landing stands.
        $w = Start-Worker @{ PD2Q_TEST_FAIL_DEV_RESET = '1' }
        $w.WaitForExit()
        Check ((Events $jB)[-1] -eq 'landed') 'on restart the job is marked Landed'
        Check (@(Events $jB | Where-Object { $_ -in @('landed', 'failed', 'rejected', 'cancelled') }).Count -eq 1) 'with one ending only, though the dev reset after it failed'
        Check ((Get-Content -LiteralPath (Join-Path $root 'queue\alarms.json') -Raw) -match 'dev-reset') 'the failed reset is an alarm'
        Check (@(& $git -C $remote log dev --grep "PD2-Queue-Job: $jB" --format=%H).Count -eq 1) 'and it was not pushed twice'
        Check ((Get-Content -LiteralPath (Join-Path $root 'queue\service.log') -Raw) -match "recovery: $jB was pushed before the restart") 'the recovery is logged'
    }

    # 3. An unexpected error while the job holds its hub slot (a test hook
    # throws after testing): the job fails, nothing is pushed, and the slot is
    # finished, not left running for everyone behind it.
    # Its edit comes from the dev clone the failed reset above left one landing
    # behind, so it takes a file the earlier jobs did not touch: a stale base
    # that does not conflict still applies (job 3 of Milestone 3 shows one that does).
    if ($want -contains 3) {
        $jC = Submit 'T-M5-003' 'm5-c' $sleeper.Id 'pd2q Milestone 5 error after testing' 'port/src/fs_path.c'
        $w = Start-Worker @{ PD2Q_TEST_THROW_JOB = $jC }
        $w.WaitForExit()
        $lastC = Get-Content -LiteralPath (Join-Path $root "queue\journal\$jC.jsonl") | Select-Object -Last 1 | ConvertFrom-Json
        Check ($lastC.event -eq 'failed' -and $lastC.detail -match 'test hook: an unexpected error') "an unexpected error fails the job: $($lastC.detail)"
        Check (@(& $git -C $remote log dev --grep "PD2-Queue-Job: $jC" --format=%H).Count -eq 0) 'and nothing of it was pushed'
        $hubState = Get-Content -LiteralPath (Join-Path $Source '.codex-coordination\state.json') -Raw -Encoding utf8 | ConvertFrom-Json
        $running = @($hubState.queue | Where-Object { $_.sessionId -eq 'pd2-queue-m5test' -and $_.status -eq 'running' })
        Check ($running.Count -eq 0) "its hub slot was finished, not leaked ($($running.Count) running)"
        Check (@(Get-ChildItem -LiteralPath (Join-Path $root 'jobs') -Force).Count -eq 0) 'the jobs folder is empty'
    }

    # 4. Cancelled while it waits for the build slot, which another session
    # holds: the cancel gets through between short admission polls, the job
    # ends cancelled and its hub item is finished. Meanwhile the worker's
    # heartbeat keeps its hub session fresh (the hub drops a session's items
    # after 6 hours without one). Polls and heartbeats are shortened here.
    if ($want -contains 4) {
        $hq = ([regex]::Match((HubCmd @('queue', '-SessionId', $holder, '-Type', 'build', '-Resource', 'build', '-Title', 'Test-QueueRecovery.ps1: hold the build slot', '-Eta', '5m', '-Details', 'Milestone 5 cancel-while-waiting check')), 'q_[0-9a-f_]+')).Value
        Check ([bool]$hq) 'the test queued its own build-slot holder'
        [void](HubCmd @('wait-turn', '-SessionId', $holder, '-QueueId', $hq, '-WaitTimeoutSeconds', '1800'))
        [void](HubCmd @('start', '-SessionId', $holder, '-QueueId', $hq))
        try {
            $jD = Submit 'T-M5-004' 'm5-d' $sleeper.Id 'pd2q Milestone 5 cancel while waiting' 'port/src/fs_path.c'
            $w = Start-Worker @{ PD2Q_ADMISSION_POLL_SECONDS = '10'; PD2Q_HEARTBEAT_SECONDS = '5' }
            Check (Wait-Event $jD 'preparing' 300) 'job 4 is preparing while another session holds the build slot'
            $waitStart = [DateTime]::UtcNow
            Start-Sleep -Seconds 25  # into its admission wait
            $r = Q @('cancel', $jD, '-Reason', 'Milestone 5 test: cancel while waiting for the build slot')
            Check ($r.Code -eq 0) "the cancel is accepted for the running job`n$($r.Out)"
            $w.WaitForExit()
            $lastD = Get-Content -LiteralPath (Join-Path $root "queue\journal\$jD.jsonl") | Select-Object -Last 1 | ConvertFrom-Json
            Check ($lastD.event -eq 'cancelled' -and $lastD.detail -match 'cancelled while it waited for the build slot') "a cancel ends a job that waits for the build slot: $($lastD.event) $($lastD.detail)"
            $hubState = Get-Content -LiteralPath (Join-Path $Source '.codex-coordination\state.json') -Raw -Encoding utf8 | ConvertFrom-Json -DateKind String
            $left = @($hubState.queue | Where-Object { $_.sessionId -eq 'pd2-queue-m5test' -and $_.status -in @('queued', 'running') })
            Check ($left.Count -eq 0) "and its hub item is finished ($($left.Count) left)"
            $seen = @($hubState.sessions | Where-Object { $_.sessionId -eq 'pd2-queue-m5test' })[0].lastSeenUtc
            Check ([DateTimeOffset]::Parse([string]$seen).UtcDateTime -gt $waitStart.AddSeconds(10)) "the worker's heartbeat refreshed its hub session while it waited (last seen $seen)"
        } finally {
            Release-Holder $hq
        }
    }

    # 5. A job that waits for the build slot longer than its time limit still
    # gets its whole limit: the limit counts from admission, since the spec's
    # 20 minutes are for a hung build, not for queueing behind another session.
    # With a one-minute limit and the slot held for 80 seconds, the job is
    # admitted, builds, and is stopped as a timeout a minute later. A limit
    # counted from the job's start would stop the build at its first poll.
    if ($want -contains 5) {
        $hq = ([regex]::Match((HubCmd @('queue', '-SessionId', $holder, '-Type', 'build', '-Resource', 'build', '-Title', 'Test-QueueRecovery.ps1: hold the build slot', '-Eta', '3m', '-Details', 'Milestone 5 time-limit-from-admission check')), 'q_[0-9a-f_]+')).Value
        Check ([bool]$hq) 'the test queued a build-slot holder for job 5'
        [void](HubCmd @('wait-turn', '-SessionId', $holder, '-QueueId', $hq, '-WaitTimeoutSeconds', '1800'))
        [void](HubCmd @('start', '-SessionId', $holder, '-QueueId', $hq))
        $held = $true
        try {
            $jE = Submit 'T-M5-005' 'm5-e' $sleeper.Id 'pd2q Milestone 5 time limit from admission' 'port/src/fs_path.c'
            $w = Start-Worker @{ PD2Q_ADMISSION_POLL_SECONDS = '5' } @('-JobLimitMinutes', '1')
            Check (Wait-Event $jE 'preparing' 300) 'job 5 is preparing while another session holds the build slot'
            Start-Sleep -Seconds 80  # past its one-minute limit, still waiting
            Check ((Events $jE) -notcontains 'building') 'job 5 still waits for the build slot after 80 seconds'
            Release-Holder $hq
            $held = $false
            Check (Wait-Event $jE 'building' 120) 'job 5 builds once the slot is free'
            $w.WaitForExit()
            $lastE = Get-Content -LiteralPath (Join-Path $root "queue\journal\$jE.jsonl") | Select-Object -Last 1 | ConvertFrom-Json
            Check ($lastE.event -eq 'failed' -and $lastE.detail -match 'timeout: the job passed its 1 minute limit while building') "the limit still stops a long build: $($lastE.event) $($lastE.detail)"
            $row = @(Get-Content -LiteralPath (Join-Path $root 'queue\builds.jsonl') | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object { $_.job -eq $jE })[-1]
            Check ($row.seconds.build -ge 55 -and $row.seconds.build -le 90) "it built for its whole minute after admission, not stopped at once ($($row.seconds.build) s)"
            Check ($row.built -eq $false) 'its build row is marked unfinished, so the slow-build median leaves it out'
            $hubState = Get-Content -LiteralPath (Join-Path $Source '.codex-coordination\state.json') -Raw -Encoding utf8 | ConvertFrom-Json -DateKind String
            $left = @($hubState.queue | Where-Object { $_.sessionId -eq 'pd2-queue-m5test' -and $_.status -in @('queued', 'running') })
            Check ($left.Count -eq 0) "job 5's hub slot is finished ($($left.Count) left)"
            Check (@(Get-ChildItem -LiteralPath (Join-Path $root 'jobs') -Force).Count -eq 0) 'the jobs folder is empty after the timeout'
        } finally {
            if ($held) { Release-Holder $hq }
        }
    }

    # 6. A session writes to the dev clone while a job builds. Preparing found
    # the clone clean minutes before; after the landing the worker must not
    # reset dev over the edit: it keeps it, pauses the queue and raises the
    # dev-dirty alarm, as Preparing does.
    if ($want -contains 6) {
        $jF = Submit 'T-M5-006' 'm5-f' $sleeper.Id 'pd2q Milestone 5 dev edited during the build'
        $w = Start-Worker
        Check (Wait-Event $jF 'building' 900) 'job 6 is building'
        $devClone = Join-Path $root 'dev'
        $devFile = Join-Path $devClone 'README.md'
        $edit = "`n<!-- a session's edit in the dev clone, Milestone 5 scenario 6 -->`n"
        [IO.File]::AppendAllText($devFile, $edit)
        $w.WaitForExit()
        Check ((Events $jF)[-1] -eq 'landed') ("job 6 landed: " + ((Events $jF) -join ' > '))
        Check ([IO.File]::ReadAllText($devFile).EndsWith($edit)) "the session's edit in the dev clone is still there after the landing"
        Check ((& $git -C $devClone rev-parse HEAD) -ne (& $git -C $remote rev-parse dev)) 'dev was not reset to the new tip'
        $svc = Get-Content -LiteralPath (Join-Path $root 'queue\service.json') -Raw | ConvertFrom-Json
        Check ([string]$svc.paused -match 'dirty') "the queue is paused: $($svc.paused)"
        Check ((Get-Content -LiteralPath (Join-Path $root 'queue\alarms.json') -Raw) -match 'dev-dirty') 'with a dev-dirty alarm'
        # The look: put the clone back, then let the queue go on.
        & $git -C $devClone checkout --quiet -- README.md
        $r = Q @('resume', '-Reason', 'Milestone 5 scenario 6: the test reverted its own edit in the dev clone')
        Check ($r.Code -eq 0) "pd2q resume after the look`n$($r.Out)"
    }

    # 7. The worker dies inside a job's ending, after the journal line and
    # before the task, the cancel request and the lease (a test hook exits
    # there). Nothing else frees a queued lease, so the next start finishes the
    # ending, once. The job is cancelled right after Preparing, so no compile.
    if ($want -contains 7) {
        $jG = Submit 'T-M5-007' 'm5-g' $sleeper.Id 'pd2q Milestone 5 crash inside the ending' 'port/src/fs_path.c'
        $w = Start-Worker @{ PD2Q_TEST_CANCEL_PREPARED_JOB = $jG; PD2Q_TEST_CRASH_IN_ENDING = $jG }
        $w.WaitForExit()
        $leases = Get-Content -LiteralPath (Join-Path $root 'queue\leases.json') -Raw | ConvertFrom-Json -AsHashtable
        Check ($w.ExitCode -eq 98 -and (Events $jG)[-1] -eq 'cancelled' -and $leases.leases['T-M5-007'].job -eq $jG) "the worker died with job 7's ending journaled and its lease still held (exit $($w.ExitCode))"
        $w = Start-Worker
        $w.WaitForExit()
        $leases = Get-Content -LiteralPath (Join-Path $root 'queue\leases.json') -Raw | ConvertFrom-Json -AsHashtable
        Check (-not $leases.leases.ContainsKey('T-M5-007')) 'on restart its lease is freed'
        $task = Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-M5-007.json') -Raw | ConvertFrom-Json -AsHashtable
        $endNotes = @($task.notes | Where-Object { $_.agent -eq 'pd2q' -and $_.session -eq $jG })
        Check ($task.state -eq 'started' -and $endNotes.Count -eq 1 -and $endNotes[0].note -like 'cancelled*') "and its task got its ending once: $($task.state), $($endNotes.Count) note(s)"
        Check (-not (Test-Path -LiteralPath (Join-Path $root "queue\requests\cancel-$jG.json"))) 'its cancel request is gone'
        Check ((Get-Content -LiteralPath (Join-Path $root 'queue\service.log') -Raw) -match "recovery: $jG had ended cancelled but its lease") 'the recovery is logged'
        $r = Q @('claim', 'T-M5-007', '-Session', 'm5-g2', '-OwnerPid', $sleeper.Id)
        Check ($r.Code -eq 0 -and $r.Out -match 'lease \d+ on') "the issue can be claimed again`n$($r.Out)"
        $w = Start-Worker
        $w.WaitForExit()
        $task = Get-Content -LiteralPath (Join-Path $root 'queue\tasks\T-M5-007.json') -Raw | ConvertFrom-Json -AsHashtable
        Check (@($task.notes | Where-Object { $_.agent -eq 'pd2q' -and $_.session -eq $jG }).Count -eq 1) 'a later start adds no second ending'
    }
} finally {
    if ($sleeper) { try { Stop-Process -Id $sleeper.Id -Force } catch { } }
    Remove-Item Env:PD2Q_ROOT, Env:PD2Q_SUBMODULE_SOURCE, Env:PD2Q_CANONICAL -ErrorAction SilentlyContinue
    if (-not $KeepFixture -and (Test-Path -LiteralPath $root)) {
        Start-Sleep -Seconds 2
        Get-ChildItem -LiteralPath $root -Recurse -Force -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Attributes = 'Normal' }
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
"PASS: $($checks.Count) recovery checks (scenarios $($want -join ', '))."
exit 0
