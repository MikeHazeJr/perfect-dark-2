#Requires -Version 7.0
<#
Milestone 5 recovery checks of the build queue spec, in the foreground against a
local bare remote (real clean builds; nothing reaches GitHub):
  1. kill the worker while a job is Building: on restart its folder is gone and
     the job reruns at the front of the queue, then lands;
  2. kill it during Pushing, after the push: on restart the job is marked Landed
     and there is exactly one commit for it (never pushed twice).
A reboot with a held lease is covered by Test-Pd2qLeases.ps1 (a changed boot
time frees the lease at once); this test does not restart the PC.
#>
param(
    [string] $Source = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike',
    [switch] $KeepFixture
)
$ErrorActionPreference = 'Stop'
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
function Start-Worker([hashtable] $Env = @{}) {
    $saved = @{}
    foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $Env[$k]) }
    try {
        return Start-Process -FilePath $pwsh -ArgumentList @('-NoProfile', '-File', "`"$service`"", '-Foreground', '-Remote', "`"$remote`"", '-IdleExitSeconds', '3', '-HubSession', 'pd2-queue-m5test') `
            -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root "worker-$([guid]::NewGuid().ToString('N').Substring(0,6)).log") -RedirectStandardError (Join-Path $root "worker-$([guid]::NewGuid().ToString('N').Substring(0,6)).err")
    } finally { foreach ($k in $Env.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) } }
}
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

    # 2. Killed during Pushing, after the push.
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

    # 3. An unexpected error while the job holds its hub slot (a test hook
    # throws after testing): the job fails, nothing is pushed, and the slot is
    # finished, not left running for everyone behind it.
    # Its edit comes from the dev clone the failed reset above left one landing
    # behind, so it takes a file the earlier jobs did not touch: a stale base
    # that does not conflict still applies (job 3 of Milestone 3 shows one that does).
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
} finally {
    if ($sleeper) { try { Stop-Process -Id $sleeper.Id -Force } catch { } }
    Remove-Item Env:PD2Q_ROOT, Env:PD2Q_SUBMODULE_SOURCE, Env:PD2Q_CANONICAL -ErrorAction SilentlyContinue
    if (-not $KeepFixture -and (Test-Path -LiteralPath $root)) {
        Start-Sleep -Seconds 2
        Get-ChildItem -LiteralPath $root -Recurse -Force -File -ErrorAction SilentlyContinue | ForEach-Object { $_.Attributes = 'Normal' }
        Remove-Item -LiteralPath $root -Recurse -Force
    }
}
"PASS: $($checks.Count) recovery checks."
exit 0
