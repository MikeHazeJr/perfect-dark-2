#Requires -Version 7.0
<#
.SYNOPSIS
    The PD2 build queue worker: one job at a time, each a full clean build of a
    submitted patch on the latest dev, tested, then pushed to dev or reported.

.DESCRIPTION
    -Setup           create <root>\{dev,queue,jobs,scratch,reports} and the clean
                     dev clone (from GitHub, or from -Remote), then exit
    -Foreground      run in this console (Milestone 3; the service host runs it too)
    -Remote <url>    fetch/push target instead of GitHub (Milestone 3: a local
                     bare repository); only refs/heads/dev is ever pushed
    -Once            run at most one job, then exit (tests)
    -IdleExitSeconds n   exit after n seconds with nothing to do (tests)

    Every arrow of a job is written to its journal before the step it leads to
    begins. Startup recovers jobs a crash or reboot interrupted.
#>
param(
    [switch] $Setup,
    [switch] $Foreground,
    [string] $Remote = '',
    [switch] $Once,
    [int] $IdleExitSeconds = 0,
    [int] $JobLimitMinutes = 20,
    [string] $HubSession = 'pd2-queue'
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Pd2Queue.psm1') -Force
$paths = Initialize-Pd2qLayout
$git = Get-Pd2qGitExe
$ps51 = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
# pwsh hands its own module folders to child processes. Windows PowerShell then
# finds PowerShell 7's Microsoft.PowerShell.Utility first and loses cmdlets such
# as Get-FileHash, which the build wrapper's storage preflight needs. pwsh drops
# those folders itself only when it runs powershell.exe as a native command,
# not for Start-Process, so job steps get the stripped path explicitly.
$ps7ModuleDirs = @((Join-Path $PSHOME 'Modules'), (Join-Path $env:ProgramFiles 'PowerShell\Modules'),
    (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PowerShell\Modules')) | ForEach-Object { $_.TrimEnd('\') }
$ps51ModulePath = @($env:PSModulePath -split ';' | Where-Object { $_ -and ($ps7ModuleDirs -notcontains $_.TrimEnd('\')) }) -join ';'
$githubUrl = 'https://github.com/MikeHazeJr/perfect-dark-2.git'
$canonical = 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
$hub = Join-Path $canonical 'Tools\CodexCoordination\CodexCoordination.ps1'
$slowSeconds = 152  # Mike's 2:32 for a cold, clean, full build of client, updater and tests

function Git([string] $Dir, [string[]] $GitArgs, [switch] $AllowFailure) {
    $out = & $git -C $Dir @GitArgs 2>&1
    $code = $LASTEXITCODE
    if ($code -ne 0 -and -not $AllowFailure) { throw "git $($GitArgs -join ' ') in $Dir failed ($code): $(($out | Out-String).Trim())" }
    return [pscustomobject]@{ Code = $code; Out = ($out | Out-String).Trim() }
}

function Get-OriginUrl {
    if ($Remote) { return $Remote }
    $r = Git $paths.Dev @('remote', 'get-url', 'origin') -AllowFailure
    if ($r.Code -eq 0 -and $r.Out) { return $r.Out }
    return $githubUrl
}

function Initialize-DevCheckout {
    if (Test-Path -LiteralPath (Join-Path $paths.Dev '.git')) { return }
    $url = if ($Remote) { $Remote } else { $githubUrl }
    # Objects are borrowed from the canonical repository while cloning, then
    # copied in (--dissociate): an independent clone of GitHub's dev, not a
    # worktree, without downloading what this PC already has.
    $seed = if ($env:PD2Q_SEED_REPO) { $env:PD2Q_SEED_REPO } else { $canonical }
    $cloneArgs = @('clone', '--branch', 'dev', '--single-branch')
    if (-not $Remote -and (Test-Path -LiteralPath (Join-Path $seed '.git'))) { $cloneArgs += @('--reference-if-able', $seed, '--dissociate') }
    Write-Pd2qLog "setup: cloning dev from $url into $($paths.Dev)"
    $out = & $git @cloneArgs $url $paths.Dev 2>&1
    if ($LASTEXITCODE -ne 0) { throw "clone failed: $(($out | Out-String).Trim())" }
    # The bgfx submodules come from GitHub at depth 1, as CI fetches them. The
    # main checkout's copies cannot seed them: they are shallow, and git
    # refuses a shallow repository as --reference.
    Update-DevSubmodules
}

function Get-SubmodulePaths([string] $Dir) {
    $file = Join-Path $Dir '.gitmodules'
    if (-not (Test-Path -LiteralPath $file)) { return @() }
    # The three bgfx submodules CI fetches (tools/recomp is not built).
    return @(Select-String -LiteralPath $file -Pattern '^\s*path\s*=\s*(third_party/bgfx/\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
}

function Update-DevSubmodules {
    $subs = Get-SubmodulePaths $paths.Dev
    if (-not $subs) { return }
    $gitArgs = @('submodule', 'update', '--init', '--depth', '1', '--') + $subs
    if ($env:PD2Q_SUBMODULE_SOURCE) {
        # Tests (and an offline setup) take the pinned bgfx commits from a local
        # repository's .git\modules instead of GitHub.
        foreach ($s in $subs) { [void](Git $paths.Dev @('config', "submodule.$s.url", (Join-Path $env:PD2Q_SUBMODULE_SOURCE $s))) }
        $gitArgs = @('-c', 'protocol.file.allow=always', 'submodule', 'update', '--init', '--') + $subs
    }
    $r = Git $paths.Dev $gitArgs -AllowFailure
    if ($r.Code -ne 0) { throw "submodule update in dev failed: $($r.Out)" }
}

function Set-ServiceState([hashtable] $Change) {
    Invoke-Pd2qLocked {
        $s = Read-Pd2qJson $paths.Service @{ schema = 'pd2q.service.v1' }
        foreach ($k in $Change.Keys) { $s[$k] = $Change[$k] }
        $s.heartbeatUtc = Format-Pd2qTime (Get-Pd2qNow)
        Write-Pd2qJson $paths.Service $s
    }
}

function Invoke-Hub([string[]] $HubArgs) {
    try {
        $out = & $ps51 -NoProfile -ExecutionPolicy Bypass -File $hub @HubArgs 2>&1 | Out-String
        return $out.Trim()
    } catch {
        Write-Pd2qLog "hub warning: $($_.Exception.Message)"
        return ''
    }
}

function Enter-HubBuildSlot([string] $JobId, [string] $Title) {
    # Anything still using the hub sees the build slot as busy while a job runs.
    if (-not (Test-Path -LiteralPath $hub)) { Write-Pd2qLog 'hub warning: coordination script not found; going on'; return $null }
    $out = Invoke-Hub @('queue', '-SessionId', $HubSession, '-Type', 'build', '-Resource', 'build', '-Title', "pd2q $JobId`: $Title", '-Eta', '5m', '-Details', 'build queue job')
    $id = ([regex]::Match($out, 'q_[0-9a-f_]+')).Value
    if (-not $id) { Write-Pd2qLog "hub warning: no queue id ($out); going on"; return $null }
    [void](Invoke-Hub @('wait-turn', '-SessionId', $HubSession, '-QueueId', $id, '-WaitTimeoutSeconds', '7200'))
    [void](Invoke-Hub @('start', '-SessionId', $HubSession, '-QueueId', $id))
    return $id
}

function Clear-HubStaleSlots {
    # A killed or rebooted worker leaves its hub slot running, and the hub only
    # calls an owner dead after 6 hours. Items of this hub session belong to an
    # earlier instance of this worker: finish them at startup.
    $state = Join-Path $canonical '.codex-coordination\state.json'
    if (-not (Test-Path -LiteralPath $state)) { return }
    try { $hubState = Get-Content -LiteralPath $state -Raw -Encoding utf8 | ConvertFrom-Json } catch { return }
    foreach ($item in @($hubState.queue | Where-Object { $_.sessionId -eq $HubSession })) {
        Write-Pd2qLog "startup: finishing stale hub item $($item.id) ($($item.status)) of an earlier worker"
        [void](Invoke-Hub @('finish', '-SessionId', $HubSession, '-QueueId', $item.id, '-Result', 'abandoned', '-Summary', 'pd2q worker restarted; the job is recovered from its journal', '-Evidence', 'C:\PD2\queue\service.log'))
    }
}

function Exit-HubBuildSlot([string] $QueueId, [string] $Summary) {
    if (-not $QueueId) { return }
    [void](Invoke-Hub @('finish', '-SessionId', $HubSession, '-QueueId', $QueueId, '-Result', 'done', '-Summary', $Summary, '-Evidence', 'C:\PD2\reports'))
}

function Test-CancelRequested([string] $JobId) {
    return Test-Path -LiteralPath (Join-Path $paths.Requests "cancel-$JobId.json")
}

function Invoke-JobStep([string] $JobId, [string] $Exe, [string[]] $Arguments, [string] $WorkDir, [string] $Log, [datetime] $Deadline, [hashtable] $Env = @{}) {
    # Runs one step as a child process tree. Cancel requests and the job's time
    # limit stop the whole tree. Started by full .exe path only.
    # Output goes through pipes, never straight to a file: pd-tests' native
    # write policy rejects an inherited disk handle, and 14 of its cases fail
    # when stdout is a file. .NET copies each pipe to its log (CopyToAsync), so
    # no PowerShell scriptblock runs on a thread without a runspace. The child's
    # environment is set on the child alone; the worker's own stays as it is.
    $psi = [Diagnostics.ProcessStartInfo]::new($Exe)
    foreach ($a in $Arguments) { $psi.ArgumentList.Add($a) }
    $psi.WorkingDirectory = $WorkDir
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    foreach ($k in $Env.Keys) { $psi.Environment[$k] = [string]$Env[$k] }
    if ($Exe -eq $ps51) { $psi.Environment['PSModulePath'] = $ps51ModulePath }
    $outFile = [IO.File]::Create($Log)
    $errFile = [IO.File]::Create($Log + '.err')
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $why = $null
    try {
        $proc = [Diagnostics.Process]::Start($psi)
        $copies = [Threading.Tasks.Task[]]@($proc.StandardOutput.BaseStream.CopyToAsync($outFile), $proc.StandardError.BaseStream.CopyToAsync($errFile))
        while (-not $proc.WaitForExit(2000)) {
            if (Test-CancelRequested $JobId) { $why = 'cancelled'; break }
            if ((Get-Date) -gt $Deadline) { $why = 'timeout'; break }
        }
        if ($why) { try { $proc.Kill($true) } catch { } }
        $proc.WaitForExit()
        $code = $proc.ExitCode
        # A pipe ends when the last process holding it exits: a stray
        # grandchild gets 30 seconds, then the log is closed without it.
        try { [void][Threading.Tasks.Task]::WaitAll($copies, 30000) } catch { }
    } finally {
        $outFile.Dispose(); $errFile.Dispose()
    }
    $errText = [IO.File]::ReadAllText($Log + '.err')
    if ($errText) { [IO.File]::AppendAllText($Log, "`n--- stderr ---`n" + $errText) }
    return [pscustomobject]@{ Code = $(if ($why) { -1 } else { $code }); Seconds = [math]::Round($watch.Elapsed.TotalSeconds, 1); Stopped = $why; Log = $Log }
}

function Get-FirstErrors([string[]] $Files, [int] $Max = 40) {
    $lines = @()
    foreach ($f in $Files) {
        if (-not (Test-Path -LiteralPath $f)) { continue }
        $lines += @(Select-String -LiteralPath $f -Pattern '(error[: ]|FAILED|undefined reference|fatal:|Cannot validate|refused)' -CaseSensitive:$false | Select-Object -First $Max | ForEach-Object { $_.Line.Trim() })
    }
    if (-not $lines) {
        # Nothing looked like an error: the step's own last lines say what happened.
        foreach ($f in $Files) { if (Test-Path -LiteralPath $f) { $lines += @(Get-Content -LiteralPath $f -Tail 30); break } }
    }
    return @($lines | Select-Object -First $Max)
}

function Write-JobReport([string] $JobId, [hashtable] $Request, [string] $Result, [hashtable] $Info) {
    $lines = @("# $JobId $Result", '', "- Issue: $($Request.issue)", "- Title: $($Request.title)", "- Session: $($Request.session) ($($Request.agent))",
        "- Dev commit it ran on: $($Info.devCommit)")
    if ($Info.pushed) { $lines += "- Pushed commit: $($Info.pushed)" }
    if ($Info.seconds) {
        $lines += "- Step times (s): " + (($Info.seconds.GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name) $($_.Value)" }) -join ', ')
    }
    if ($Info.testSummary) { $lines += "- pd-tests: $($Info.testSummary)" }
    if ($Info.slow) { $lines += "- SLOW: $($Info.slowReason)" }
    if ($Info.detail) { $lines += "- $($Info.detail)" }
    if ($Info.errors) { $lines += '', '## First errors', '', '```'; $lines += @($Info.errors); $lines += '```' }
    if ($Info.testOutput) { $lines += '', '## Failing tests (up to 200 lines)', '', '```'; $lines += @($Info.testOutput); $lines += '```' }
    $file = Join-Path $paths.Reports "$JobId.md"
    [IO.File]::WriteAllText($file, ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    # Reports: the newest 200 are kept.
    @(Get-ChildItem -LiteralPath $paths.Reports -Filter 'q*.md' -File | Sort-Object LastWriteTimeUtc -Descending | Select-Object -Skip 200) |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
    return $file
}

function Complete-Job([string] $JobId, [hashtable] $Request, [string] $Result, [hashtable] $Info) {
    # Endings: folder deleted, hub slot finished, lease freed, report written.
    $jobDir = Join-Path $paths.Jobs $JobId
    try { Remove-Pd2qTree $jobDir @($paths.Jobs) } catch { Write-Pd2qLog "job folder $jobDir not removed: $($_.Exception.Message)"; Add-Pd2qAlarm 'job-folder' "$jobDir left after $Result" }
    Exit-HubBuildSlot $Info.hubId "pd2q $JobId $Result"
    $report = Write-JobReport $JobId $Request $Result $Info
    Invoke-Pd2qLocked {
        Add-Pd2qJournal $JobId @{ event = $Result; attempt = $Info.attempt; devCommit = $Info.devCommit; detail = $Info.detail; commit = $Info.pushed; report = $report }
        $l = Read-Pd2qLeases
        if ($l.leases[$Request.issue] -and $l.leases[$Request.issue].job -eq $JobId) { $l.leases.Remove($Request.issue); Write-Pd2qJson $paths.Leases $l }
        $task = Get-Pd2qTask $Request.issue
        if ($task) {
            $task.state = if ($Result -eq 'landed') { 'finished' } else { 'started' }
            $task.notes = @($task.notes) + @(@{ atUtc = Format-Pd2qTime (Get-Pd2qNow); agent = 'pd2q'; session = $JobId; note = "$Result$(if ($Info.detail) { ': ' + $Info.detail }) (report $report)" })
            Save-Pd2qTask $task
        }
        if ($Result -eq 'landed') {
            Add-Pd2qJsonLine $paths.Board @{ schema = 'pd2q.board.v1'; issue = $Request.issue; commit = $Info.pushed; job = $JobId; title = $Request.title
                session = $Request.session; agent = $Request.agent; landedUtc = Format-Pd2qTime (Get-Pd2qNow) }
        }
        Remove-Item -LiteralPath (Join-Path $paths.Requests "cancel-$JobId.json") -Force -ErrorAction SilentlyContinue
    }
    if ($Info.seconds) {
        Add-Pd2qJsonLine $paths.Builds @{ schema = 'pd2q.build.v1'; job = $JobId; devCommit = $Info.devCommit; clean = $true; result = $Result
            seconds = $Info.seconds; total = $Info.seconds.total; slow = [bool]$Info.slow; slowReason = $Info.slowReason; atUtc = Format-Pd2qTime (Get-Pd2qNow) }
        $all = @(Read-Pd2qJsonLines $paths.Builds)
        if ($all.Count -gt 500) { $keep = $all | Select-Object -Last 500; [IO.File]::WriteAllText($paths.Builds, (($keep | ForEach-Object { $_ | ConvertTo-Json -Compress -Depth 10 }) -join "`n") + "`n") }
    }
    if ($Result -eq 'landed') {
        [void](Git $paths.Dev @('fetch', 'origin') -AllowFailure)
        [void](Git $paths.Dev @('reset', '--hard', 'origin/dev'))
        Update-DevSubmodules
        Send-WorkbenchNote $Request $JobId $Info
    }
    Write-Pd2qLog "job $JobId $Result$(if ($Info.detail) { ': ' + $Info.detail })"
}

function Send-WorkbenchNote([hashtable] $Request, [string] $JobId, [hashtable] $Info) {
    # Milestone 4 option: note the landing on the issue's Workbench item when the
    # canonical Workbench runs.
    if ($Remote) { return }
    try {
        $meta = Invoke-RestMethod -Uri 'http://127.0.0.1:8378/api/meta' -TimeoutSec 3
        if (-not ($meta.canonical -eq $true -and $meta.isolated -eq $false)) { return }
        $body = @{ target = $Request.issue; author = 'pd2-queue'; text = "Landed by the build queue: $JobId, commit $($Info.pushed) on dev (built from $($Info.devCommit)); $($Request.title). Session $($Request.session) ($($Request.agent))." } | ConvertTo-Json
        [void](Invoke-RestMethod -Uri 'http://127.0.0.1:8378/api/notes' -Method Post -ContentType 'application/json' -Body $body -TimeoutSec 5)
        # The item itself carries the pushed commit as evidence.
        $roadmap = Invoke-RestMethod -Uri 'http://127.0.0.1:8378/api/roadmap' -TimeoutSec 5
        $item = @($roadmap.items | Where-Object { $_.id -ceq $Request.issue })[0]
        if ($item) {
            $evidence = @(@($item.evidence) | Where-Object { $_ }) + @("dev $($Info.pushed.Substring(0, 12)) landed by the build queue ($JobId): $($Request.title)")
            $update = @{ id = $Request.issue; by = 'pd2-queue'; reason = "landed by the build queue ($JobId)"; patch = @{ evidence = $evidence } } | ConvertTo-Json -Depth 6
            [void](Invoke-RestMethod -Uri 'http://127.0.0.1:8378/api/roadmap/update' -Method Post -ContentType 'application/json' -Body $update -TimeoutSec 5)
        }
    } catch { Write-Pd2qLog "workbench note for $JobId skipped: $($_.Exception.Message)" }
}

function Requeue-Front([string] $JobId, [int] $Attempt, [string] $Why) {
    Invoke-Pd2qLocked { Add-Pd2qJournal $JobId @{ event = 'queued'; front = $true; attempt = $Attempt; detail = $Why } }
    Write-Pd2qLog "job $JobId back to the front (attempt $Attempt): $Why"
}

function Invoke-Job($Job) {
    $jobId = $Job.Id
    $request = $Job.Request
    $attempt = [int]$Job.Attempt
    $deadline = (Get-Date).AddMinutes($JobLimitMinutes)
    $info = @{ attempt = $attempt; seconds = [ordered]@{}; devCommit = ''; hubId = $null }
    $jobDir = Join-Path $paths.Jobs $jobId
    $src = Join-Path $jobDir 'src'
    $total = [Diagnostics.Stopwatch]::StartNew()

    # ---- Preparing
    Invoke-Pd2qLocked { Add-Pd2qJournal $jobId @{ event = 'preparing'; attempt = $attempt } }
    Set-ServiceState @{ state = 'preparing'; job = $jobId }
    $dirty = (Git $paths.Dev @('status', '--porcelain')).Out
    if ($dirty) {
        # A session wrote to the dev checkout: stop taking jobs, change nothing.
        Set-ServiceState @{ paused = "dev checkout is dirty: $(($dirty -split "`n" | Select-Object -First 3) -join '; ')" }
        Add-Pd2qAlarm 'dev-dirty' "git status --porcelain in $($paths.Dev) is not empty; nothing reset. pd2q resume after a look."
        Requeue-Front $jobId $attempt 'dev checkout dirty; queue paused'
        return
    }
    [void](Git $paths.Dev @('fetch', 'origin'))
    [void](Git $paths.Dev @('reset', '--hard', 'origin/dev'))
    Update-DevSubmodules
    $devCommit = (Git $paths.Dev @('rev-parse', 'HEAD')).Out
    $info.devCommit = $devCommit
    $leaseOk = Invoke-Pd2qLocked { $le = (Read-Pd2qLeases).leases[$request.issue]; [bool]($le -and $le.job -eq $jobId) }
    if (-not $leaseOk) { $info.detail = 'its lease is gone (broken or freed)'; Complete-Job $jobId $request 'rejected' $info; return }
    if (Test-Path -LiteralPath $jobDir) { Remove-Pd2qTree $jobDir @($paths.Jobs) }
    [IO.Directory]::CreateDirectory($jobDir) | Out-Null
    $prep = [Diagnostics.Stopwatch]::StartNew()
    $out = & $git clone --shared --quiet $paths.Dev $src 2>&1
    if ($LASTEXITCODE -ne 0) { throw "clone into $src failed: $(($out | Out-String).Trim())" }
    $info.seconds.prepare_clone = [math]::Round($prep.Elapsed.TotalSeconds, 1)
    [void](Git $src @('remote', 'set-url', 'origin', (Get-OriginUrl)))
    $subs = Get-SubmodulePaths $src
    foreach ($s in $subs) { [void](Git $src @('config', "submodule.$s.url", (Join-Path $paths.Dev ".git\modules\$s"))) }
    if ($subs) {
        $r = Git $src (@('-c', 'protocol.file.allow=always', 'submodule', 'update', '--init', '--') + $subs) -AllowFailure
        if ($r.Code -ne 0) { throw "submodules in the job folder failed: $($r.Out)" }
    }
    $info.seconds.prepare_submodules = [math]::Round($prep.Elapsed.TotalSeconds - $info.seconds.prepare_clone, 1)
    $patch = Join-Path $paths.Journal "$jobId.patch"
    $r = Git $src @('apply', '--3way', '--whitespace=nowarn', $patch) -AllowFailure
    if ($r.Code -ne 0) {
        $info.detail = "the patch no longer applies to dev $($devCommit.Substring(0, 12)); re-claim, refresh scratch from dev, resubmit"
        $info.errors = @($r.Out -split "`n" | Select-Object -First 40)
        Complete-Job $jobId $request 'rejected' $info; return
    }
    $info.seconds.prepare = [math]::Round($prep.Elapsed.TotalSeconds, 1)
    $info.hubId = Enter-HubBuildSlot $jobId $request.title

    # ---- Building (clean, ccache off, the job folder's own scripts)
    Invoke-Pd2qLocked { Add-Pd2qJournal $jobId @{ event = 'building'; attempt = $attempt; devCommit = $devCommit } }
    Set-ServiceState @{ state = 'building'; job = $jobId }
    $logDir = Join-Path $jobDir 'logs'
    [IO.Directory]::CreateDirectory($logDir) | Out-Null
    $session = Join-Path $src 'devtools\build-session.ps1'
    # -Target full (one ninja call, dev dda2ce38 onward) when this tree's own
    # wrapper offers it; an older tree builds in two calls.
    $hasFull = (Get-Content -LiteralPath $session -Raw) -match 'ValidateSet\([^)]*"full"'
    $envOff = @{ CCACHE_DISABLE = '1' }
    # A list, not an if-expression: PowerShell would unroll a one-step array
    # into its words and pass -Target alone.
    $buildSteps = [Collections.Generic.List[object]]::new()
    if ($hasFull) { $buildSteps.Add([string[]]@('-Target', 'full', '-Clean')) }
    else { $buildSteps.Add([string[]]@('-Target', 'all', '-Clean')); $buildSteps.Add([string[]]@('-Target', 'tests')) }
    $buildSeconds = 0.0
    foreach ($step in $buildSteps) {
        $res = Invoke-JobStep $jobId $ps51 (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $session, '-Session', 'q') + $step) $src (Join-Path $logDir "build-$($step[1]).log") $deadline $envOff
        $buildSeconds += $res.Seconds
        if ($res.Stopped -or $res.Code -ne 0) {
            $info.seconds.build = [math]::Round($buildSeconds, 1)
            $result = if ($res.Stopped -eq 'cancelled') { 'cancelled' } else { 'failed' }
            $info.detail = if ($res.Stopped -eq 'timeout') { "timeout: the job passed its $JobLimitMinutes minute limit while building" } elseif ($res.Stopped) { 'cancelled while building' } else { "the build failed (exit $($res.Code))" }
            $info.errors = Get-FirstErrors @($res.Log, (Join-Path $src '.claude\session-builds\q\_build-session.err.log'), (Join-Path $src '.claude\session-builds\q\_build-session.out.log'))
            $info.seconds.total = [math]::Round($total.Elapsed.TotalSeconds, 1)
            Complete-Job $jobId $request $result $info; return
        }
    }
    $info.seconds.build = [math]::Round($buildSeconds, 1)
    $prior = @(Read-Pd2qJsonLines $paths.Builds | Where-Object { $_.clean -and $_.seconds -and $_.seconds.build } | Select-Object -Last 20 | ForEach-Object { [double]$_.seconds.build } | Sort-Object)
    $median = if ($prior) { $prior[[int][math]::Floor($prior.Count / 2)] } else { 0 }
    if ($buildSeconds -gt $slowSeconds) { $info.slow = $true; $info.slowReason = "clean build $([math]::Round($buildSeconds)) s is over $slowSeconds s (Mike's 2:32)" }
    elseif ($median -gt 0 -and $buildSeconds -gt 1.5 * $median) { $info.slow = $true; $info.slowReason = "clean build $([math]::Round($buildSeconds)) s is over 1.5 times the median of the last 20 ($median s)" }

    # ---- Testing
    Invoke-Pd2qLocked { Add-Pd2qJournal $jobId @{ event = 'testing'; attempt = $attempt; devCommit = $devCommit; seconds = $info.seconds.build } }
    Set-ServiceState @{ state = 'testing'; job = $jobId }
    $testArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $src 'devtools\run-pd-tests.ps1'), '-Session', 'q', '-NoBuild')
    if ($request.tests -and $request.tests.scope) { $testArgs += @('-Scope', [string]$request.tests.scope) }
    $res = Invoke-JobStep $jobId $ps51 $testArgs $src (Join-Path $logDir 'tests.log') $deadline
    $info.seconds.tests = $res.Seconds
    # Catch2's closing line ("All tests passed (N assertions in M test cases)")
    # goes in the report: a landed job shows how much ran.
    $summary = @(Select-String -LiteralPath $res.Log -Pattern '(All tests passed|test cases:?\s)' | Select-Object -Last 1 | ForEach-Object { $_.Line.Trim() })
    if ($summary) { $info.testSummary = $summary[0] }
    if ($res.Stopped -or $res.Code -ne 0) {
        $result = if ($res.Stopped -eq 'cancelled') { 'cancelled' } else { 'failed' }
        $info.detail = if ($res.Stopped -eq 'timeout') { "timeout while testing" } elseif ($res.Stopped) { 'cancelled while testing' } else { "pd-tests failed (exit $($res.Code))" }
        $testLines = @(Get-Content -LiteralPath $res.Log -ErrorAction SilentlyContinue)
        $failIdx = @(0..([math]::Max(0, $testLines.Count - 1)) | Where-Object { $testLines[$_] -match 'FAILED' })
        $info.testOutput = if ($failIdx) { @($testLines[[math]::Max(0, $failIdx[0] - 20)..([math]::Min($testLines.Count - 1, $failIdx[0] + 180))]) } else { @($testLines | Select-Object -Last 200) }
        $info.seconds.total = [math]::Round($total.Elapsed.TotalSeconds, 1)
        Complete-Job $jobId $request $result $info; return
    }
    if ($request.tests -and $request.tests.smoke) {
        $info.detail = 'built and tested; -Smoke is for milestone finalization and is run with tools/smoke-verify/run.ps1 -Prepare/-Execute, not by the queue yet'
    }

    # ---- Pushing
    Invoke-Pd2qLocked { Add-Pd2qJournal $jobId @{ event = 'pushing'; attempt = $attempt; devCommit = $devCommit; seconds = $info.seconds.tests } }
    Set-ServiceState @{ state = 'pushing'; job = $jobId }
    $name = (Git $paths.Dev @('config', 'user.name') -AllowFailure).Out
    $email = (Git $paths.Dev @('config', 'user.email') -AllowFailure).Out
    if (-not $name) { $name = (& $git -C $canonical config user.name) }
    if (-not $email) { $email = (& $git -C $canonical config user.email) }
    $msgFile = Join-Path $jobDir 'message.txt'
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add([string]$request.title); $lines.Add('')
    if ($request.ContainsKey('body') -and $request.body) { $lines.Add([string]$request.body); $lines.Add('') }
    $lines.Add("Built and tested by the PD2 build queue: job $jobId on dev $($devCommit.Substring(0, 12)), clean build $($info.seconds.build) s, pd-tests $($info.seconds.tests) s."); $lines.Add('')
    foreach ($t in @("Refs: $($request.issue)", "PD2-Issue: $($request.issue)", "PD2-Queue-Job: $jobId", "PD2-Session: $($request.session)", "PD2-Agent: $($request.agent)")) { $lines.Add($t) }
    if ($request.ContainsKey('coAuthor') -and $request.coAuthor) { $lines.Add("Co-Authored-By: $($request.coAuthor)") }
    $message = $lines -join "`n"
    [IO.File]::WriteAllText($msgFile, $message + "`n", [Text.UTF8Encoding]::new($false))
    # Commit exactly what `git apply --3way` staged: the patch. Anything the
    # build changed in the work tree (generated files) never goes in.
    [void](Git $src @('-c', "user.name=$name", '-c', "user.email=$email", 'commit', '-q', '--no-verify', '-F', $msgFile))
    $commit = (Git $src @('rev-parse', 'HEAD')).Out
    $touched = @((Git $src @('diff', '--name-only', $devCommit, $commit)).Out -split "`n" | Where-Object { $_ })
    $declared = @($request.files)
    $extra = @($touched | Where-Object { $declared -notcontains $_ })
    if ($declared -and $extra) {
        $info.detail = "the commit would touch files outside the patch: $($extra -join ', ')"
        $info.seconds.total = [math]::Round($total.Elapsed.TotalSeconds, 1)
        Complete-Job $jobId $request 'failed' $info; return
    }
    [void](Git $src @('fetch', '--quiet', 'origin', 'dev'))
    $remoteDev = (Git $src @('rev-parse', 'FETCH_HEAD')).Out
    if ($remoteDev -ne $devCommit) {
        Exit-HubBuildSlot $info.hubId "pd2q $jobId requeued (dev moved)"; $info.hubId = $null
        if ($attempt -ge 3) { $info.detail = 'dev-churn: dev moved under the job three times'; $info.seconds.total = [math]::Round($total.Elapsed.TotalSeconds, 1); Complete-Job $jobId $request 'failed' $info; return }
        Remove-Pd2qTree $jobDir @($paths.Jobs)
        Requeue-Front $jobId ($attempt + 1) "dev moved from $($devCommit.Substring(0, 12)) to $($remoteDev.Substring(0, 12)) during the job"
        return
    }
    $target = 'refs/heads/dev'
    Assert-Pd2qPushTarget $target  # hard-coded target; anything else throws
    $push = Git $src @('push', '--quiet', 'origin', "HEAD:$target") -AllowFailure
    if ($push.Code -ne 0) {
        # Someone pushed in the same second: treated as dev moved.
        Exit-HubBuildSlot $info.hubId "pd2q $jobId requeued (push rejected)"; $info.hubId = $null
        if ($attempt -ge 3) { $info.detail = "dev-churn: push rejected: $($push.Out)"; $info.seconds.total = [math]::Round($total.Elapsed.TotalSeconds, 1); Complete-Job $jobId $request 'failed' $info; return }
        Remove-Pd2qTree $jobDir @($paths.Jobs)
        Requeue-Front $jobId ($attempt + 1) "push rejected: $($push.Out)"
        return
    }
    if ($env:PD2Q_TEST_CRASH_AFTER_PUSH -eq '1') {
        # Milestone 5 recovery test only: die between the push and its journal line.
        Write-Pd2qLog "test hook: exiting right after pushing $jobId"
        [Environment]::Exit(97)
    }
    $info.pushed = $commit
    $info.seconds.total = [math]::Round($total.Elapsed.TotalSeconds, 1)
    Complete-Job $jobId $request 'landed' $info
}

function Invoke-Recovery {
    # Jobs a crash, kill or reboot interrupted, and leftovers.
    foreach ($job in @(Get-Pd2qJobs | Where-Object { $_.State -in @('preparing', 'building', 'testing', 'pushing') })) {
        $jobDir = Join-Path $paths.Jobs $job.Id
        if ($job.State -eq 'pushing') {
            [void](Git $paths.Dev @('fetch', 'origin') -AllowFailure)
            $found = (Git $paths.Dev @('log', 'origin/dev', '-n', '200', "--grep=PD2-Queue-Job: $($job.Id)", '--format=%H') -AllowFailure).Out
            if ($found) {
                $commit = ($found -split "`n")[0].Trim()
                Write-Pd2qLog "recovery: $($job.Id) was pushed before the restart ($commit); marked landed, never pushed twice"
                Complete-Job $job.Id $job.Request 'landed' @{ attempt = $job.Attempt; devCommit = [string]$job.Last.devCommit; pushed = $commit; detail = 'recovered after a restart during Pushing'; hubId = $null }
                continue
            }
        }
        $interrupted = @($job.Lines | Where-Object { $_.event -eq 'queued' -and $_.ContainsKey('detail') -and "$($_.detail)" -like 'interrupted*' }).Count
        try { Remove-Pd2qTree $jobDir @($paths.Jobs) } catch { Write-Pd2qLog "recovery: $jobDir not removed: $($_.Exception.Message)" }
        if ($interrupted -ge 2) {
            Complete-Job $job.Id $job.Request 'failed' @{ attempt = $job.Attempt; devCommit = [string]$job.Last.devCommit; detail = 'interrupted: stopped mid-job three times'; hubId = $null }
        } else {
            Requeue-Front $job.Id ($job.Attempt + 1) "interrupted during $($job.State) (service stopped or PC restarted)"
        }
    }
    # Job folders with no running job are leftovers.
    $live = @(Get-Pd2qJobs | Where-Object { $_.State -in @('preparing', 'building', 'testing', 'pushing') } | ForEach-Object Id)
    foreach ($dir in @(Get-ChildItem -LiteralPath $paths.Jobs -Directory -ErrorAction SilentlyContinue)) {
        if ($live -notcontains $dir.Name) { Write-Pd2qLog "recovery: removing leftover job folder $($dir.FullName)"; Remove-Pd2qTree $dir.FullName @($paths.Jobs) }
    }
    # Finished journals older than 14 days go.
    $cut = (Get-Date).AddDays(-14)
    foreach ($job in @(Get-Pd2qJobs | Where-Object { $_.State -in @('landed', 'rejected', 'failed', 'cancelled') })) {
        $file = Join-Path $paths.Journal "$($job.Id).jsonl"
        if ((Get-Item -LiteralPath $file).LastWriteTime -lt $cut) {
            Remove-Item -LiteralPath $file -Force
            Remove-Item -LiteralPath (Join-Path $paths.Journal "$($job.Id).patch") -Force -ErrorAction SilentlyContinue
        }
    }
}

# ---------------------------------------------------------------- main

if ($Setup) {
    Initialize-DevCheckout
    Write-Output ("pd2q layout ready at {0}; dev at {1}" -f $paths.Root, (Git $paths.Dev @('rev-parse', '--short=12', 'HEAD')).Out)
    exit 0
}
if (-not $Foreground) { throw 'Run with -Foreground (the service host passes it), or -Setup.' }

$instance = [Threading.Mutex]::new($false, ((Get-Pd2qMutexName) + '-Service'))
$haveInstance = $false
try { $haveInstance = $instance.WaitOne(0) } catch [Threading.AbandonedMutexException] { $haveInstance = $true }
if (-not $haveInstance) { throw 'Another queue service is already running for this root.' }
$wake = [Threading.EventWaitHandle]::new($false, 'AutoReset', ((Get-Pd2qMutexName) + '-Wake'))
try {
    Initialize-DevCheckout
    if ($Remote) { [void](Git $paths.Dev @('remote', 'set-url', 'origin', $Remote)) }
    $self = Get-Process -Id $PID
    Set-ServiceState @{ pid = $PID; pidStartUtc = (Get-Pd2qProcessStartUtc $PID); startedUtc = Format-Pd2qTime (Get-Pd2qNow); state = 'starting'; job = $null; remote = (Get-OriginUrl) }
    if (Test-Path -LiteralPath $hub) {
        [void](Invoke-Hub @('register', '-SessionId', $HubSession, '-Goal', 'PD2 build queue service: one clean build per submitted patch', '-CurrentTask', 'waiting for jobs', '-Eta', 'always', '-Plan', 'pd2q jobs'))
        Clear-HubStaleSlots
    }
    Invoke-Recovery
    [void](Invoke-Pd2qLocked { Update-Pd2qBoardFromGit })
    $idleSince = Get-Date
    while ($true) {
        [void](Invoke-Pd2qLocked { Update-Pd2qLeases })
        $service = Read-Pd2qJson $paths.Service @{}
        $next = if ($service.ContainsKey('paused') -and $service.paused) { $null } else { Invoke-Pd2qLocked { Get-Pd2qNextJob } }
        if ($next) {
            Set-ServiceState @{ state = 'running'; job = $next.Id }
            try { Invoke-Job $next }
            catch {
                $err = $_.Exception.Message
                Write-Pd2qLog "job $($next.Id) error: $err"
                try { Complete-Job $next.Id $next.Request 'failed' @{ attempt = $next.Attempt; devCommit = ''; detail = "queue error: $err"; hubId = $null } } catch { Write-Pd2qLog "could not close $($next.Id): $($_.Exception.Message)" }
            }
            Set-ServiceState @{ state = 'idle'; job = $null }
            $idleSince = Get-Date
            if ($Once) { break }
            continue
        }
        Set-ServiceState @{ state = $(if ($service.ContainsKey('paused') -and $service.paused) { 'paused' } else { 'idle' }); job = $null }
        if ($IdleExitSeconds -gt 0 -and ((Get-Date) - $idleSince).TotalSeconds -ge $IdleExitSeconds) { break }
        [void]$wake.WaitOne([TimeSpan]::FromSeconds(30))  # woken by pd2q submit/cancel/resume
    }
} finally {
    Set-ServiceState @{ state = 'stopped'; job = $null }
    $wake.Dispose()
    if ($haveInstance) { $instance.ReleaseMutex() }
    $instance.Dispose()
}
