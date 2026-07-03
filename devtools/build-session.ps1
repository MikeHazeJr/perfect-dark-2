#Requires -Version 5.1
<#
.SYNOPSIS
    Run a build in an isolated per-session build directory.

.DESCRIPTION
    Concurrent AI/code sessions must not share the root Build/ directory. This
    wrapper keeps the canonical build logic in build-headless.ps1, but gives
    each session its own CMake/Ninja directory under .claude/session-builds/.
    Post-build addin copy (B-321/B-326): ROM *.z64 and BYOR placeholder at
    install root; non-ROM files under data/ -- see build-headless.ps1.

.EXAMPLE
    .\devtools\build-session.ps1 -Session s500 -Target all
    .\devtools\build-session.ps1 -Session s500 -Target tests
    .\devtools\build-session.ps1 -Session s500 -Target client -Clean
    .\devtools\build-session.ps1 -Session s500 -Target all -BuildTimeoutSeconds 120
    .\devtools\build-session.ps1 -List
    .\devtools\build-session.ps1 -Tail -Session s500
    .\devtools\build-session.ps1 -Remove -Session s500
    .\devtools\build-session.ps1 -RemoveAll
#>

param(
    [ValidateSet("client", "updater", "tests", "probe", "all")]
    [string]$Target = "all",

    # Stable per-session identifier. Reuse it for incremental rebuilds inside
    # one session; choose a different value for simultaneous sessions.
    [string]$Session = "",

    [string]$Version = "",

    # Dev-mod selection passed through to build-headless.ps1 (copied into
    # <install>/mods). "" = manifest "dev":true; "all"/"none"/"id1,id2".
    [string]$DevMods = "",

    [switch]$Clean,
    [switch]$Verbose,
    [switch]$NoQueue,
    [int]$QueueStatusSeconds = 30,
    # Idle-based watchdog (2026-07-02): a queued build is treated as hung only
    # after it produces NO output (no log/ninja growth) for this many seconds --
    # NOT after this much total build time. 120s comfortably covers the slowest
    # single translation unit (e.g. the ~9.7k-line romextract_pdarena.c) while
    # still catching a genuinely wedged cc1. Use 0 for an intentional
    # no-watchdog run.
    [int]$BuildTimeoutSeconds = 120,

    # Maintenance modes.
    [switch]$SelfTest,
    [switch]$List,
    [switch]$Tail,
    [int]$TailLines = 80,
    [switch]$Follow,
    [switch]$Remove,
    [switch]$RemoveAll,

    # Only affects stale-lock cleanup for -Remove / -RemoveAll.
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Split-Path -Parent $ScriptDir
$SessionBuildRoot = Join-Path $ProjectDir ".claude\session-builds"
$LockDir = Join-Path $SessionBuildRoot ".locks"
$QueueDir = Join-Path $SessionBuildRoot ".queue"
$QueueLockPath = Join-Path $QueueDir "queue.lock"
$QueueActivePath = Join-Path $QueueDir "active.json"
$QueueDurationsPath = Join-Path $QueueDir "durations.json"
$QueueTimeoutExitCode = 124

function Write-Info([string]$text) { Write-Host $text -ForegroundColor Gray }
function Write-Warn([string]$text) { Write-Host $text -ForegroundColor Yellow }
function Write-Ok([string]$text)   { Write-Host $text -ForegroundColor Green }

function Format-DurationShort([double]$seconds) {
    $total = [int][math]::Max(0, [math]::Round($seconds))
    $ts = [TimeSpan]::FromSeconds($total)
    if ($ts.TotalHours -ge 1) {
        return "{0}h {1}m" -f [int][math]::Floor($ts.TotalHours), $ts.Minutes
    }
    if ($ts.TotalMinutes -ge 1) {
        return "{0}m {1}s" -f $ts.Minutes, $ts.Seconds
    }
    return "{0}s" -f $ts.Seconds
}

function Get-ObjectValue($obj, [string]$name, $defaultValue) {
    if ($null -eq $obj) { return $defaultValue }
    $prop = $obj.PSObject.Properties[$name]
    if ($null -eq $prop -or $null -eq $prop.Value) { return $defaultValue }
    return $prop.Value
}

function ConvertTo-UtcDateTime($value) {
    try {
        if ($value -is [DateTime]) {
            return ([DateTime]$value).ToUniversalTime()
        }
        return ([DateTime]::Parse(
            [string]$value,
            [System.Globalization.CultureInfo]::InvariantCulture,
            [System.Globalization.DateTimeStyles]::RoundtripKind)).ToUniversalTime()
    } catch {
        return [DateTime]::UtcNow
    }
}

function Test-PidAlive($pidValue) {
    try { $pidInt = [int]$pidValue } catch { return $false }
    if ($pidInt -le 0) { return $false }
    try {
        [void](Get-Process -Id $pidInt -ErrorAction Stop)
        return $true
    } catch {
        return $false
    }
}

function Get-ChildProcessIds([int]$parentPid) {
    $result = @()
    try {
        $children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$parentPid" -ErrorAction Stop)
    } catch {
        try {
            $children = @(Get-WmiObject Win32_Process -Filter "ParentProcessId=$parentPid" -ErrorAction SilentlyContinue)
        } catch {
            $children = @()
        }
    }

    foreach ($child in $children) {
        $childPid = [int]$child.ProcessId
        $result += $childPid
        $result += Get-ChildProcessIds $childPid
    }
    return $result
}

function Stop-ProcessTree([int]$rootPid) {
    if ($rootPid -le 0) { return }

    $descendants = @(Get-ChildProcessIds $rootPid)
    for ($i = $descendants.Count - 1; $i -ge 0; $i--) {
        $pidToStop = [int]$descendants[$i]
        if (-not (Test-PidAlive $pidToStop)) { continue }
        try { Stop-Process -Id $pidToStop -Force -ErrorAction Stop } catch {}
    }

    if (Test-PidAlive $rootPid) {
        try { Stop-Process -Id $rootPid -Force -ErrorAction Stop } catch {}
    }
}

function Show-ProcessTreeSnapshot([int]$rootPid) {
    if ($rootPid -le 0) { return }

    $ids = @($rootPid) + @(Get-ChildProcessIds $rootPid)
    $rows = @()
    foreach ($pidValue in $ids) {
        try {
            $proc = Get-CimInstance Win32_Process -Filter "ProcessId=$pidValue" -ErrorAction Stop
            $cmd = [string]$proc.CommandLine
            if ($cmd -and $cmd.Length -gt 180) {
                $cmd = $cmd.Substring(0, 177) + "..."
            }
            $rows += [PSCustomObject]@{
                Pid = [int]$proc.ProcessId
                Parent = [int]$proc.ParentProcessId
                Name = [string]$proc.Name
                Command = $cmd
            }
            continue
        } catch {}

        try {
            $fallback = Get-Process -Id ([int]$pidValue) -ErrorAction Stop
            $cmd = ""
            try { $cmd = [string]$fallback.Path } catch {}
            if ($cmd -eq "") { $cmd = "<command line unavailable>" }
            if ($cmd.Length -gt 180) {
                $cmd = $cmd.Substring(0, 177) + "..."
            }
            $rows += [PSCustomObject]@{
                Pid = [int]$fallback.Id
                Parent = -1
                Name = [string]$fallback.ProcessName
                Command = $cmd
            }
        } catch {}
    }

    Write-Host ""
    Write-Host "Build process tree at timeout:" -ForegroundColor Cyan
    if ($rows.Count -eq 0) {
        Write-Host "  (no live child process rows could be collected; the child may have exited during watchdog cleanup)" -ForegroundColor DarkGray
        return
    }
    $table = ($rows | Format-Table -AutoSize -Wrap | Out-String -Width 240)
    foreach ($line in ($table -split "`r?`n")) {
        if ($line.Trim() -ne "") {
            Write-Host $line
        }
    }
}

function Get-BuildStdoutLogPath([string]$buildDir) {
    return Join-Path $buildDir "_build-session.out.log"
}

function Get-BuildStderrLogPath([string]$buildDir) {
    return Join-Path $buildDir "_build-session.err.log"
}

function Get-HeadlessStepLogFiles([string]$buildDir) {
    if ($buildDir -eq "" -or -not (Test-Path -LiteralPath $buildDir)) { return @() }
    return @(Get-ChildItem -LiteralPath $buildDir -Filter "_build-headless-*.log" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending)
}

function Get-BuildProgressSignal([string]$buildDir) {
    # Progress-aware watchdog (2026-07-02): a build that is actively compiling
    # must NOT be killed just because it crossed an absolute wall-clock timeout.
    # This returns a cheap monotone "are we making progress" signal -- the sum
    # of output-log bytes plus the newest write time across the session logs,
    # ninja's build log, and the per-step headless logs. Any forward motion in
    # bytes or mtime resets the idle timer, so only a genuinely wedged build
    # (no output for the whole idle window) trips the watchdog.
    $bytes = [long]0
    $newestTicks = [long]0
    $paths = New-Object System.Collections.Generic.List[string]
    $paths.Add((Get-BuildStdoutLogPath $buildDir))
    $paths.Add((Get-BuildStderrLogPath $buildDir))
    $paths.Add((Join-Path $buildDir ".ninja_log"))
    foreach ($stepLog in (Get-HeadlessStepLogFiles $buildDir)) {
        $paths.Add($stepLog.FullName)
    }
    foreach ($path in $paths) {
        try {
            $info = Get-Item -LiteralPath $path -ErrorAction Stop
            $bytes += [long]$info.Length
            $ticks = [long]$info.LastWriteTimeUtc.Ticks
            if ($ticks -gt $newestTicks) { $newestTicks = $ticks }
        } catch {
            # Missing log (not created yet / rotated) contributes nothing.
        }
    }
    return [PSCustomObject]@{
        Bytes = $bytes
        NewestTicks = $newestTicks
    }
}

function Test-BuildProgressAdvanced($previous, $current) {
    if ($null -eq $previous) { return $true }
    if ($current.Bytes -gt $previous.Bytes) { return $true }
    if ($current.NewestTicks -gt $previous.NewestTicks) { return $true }
    return $false
}

function Show-HeadlessStepLogTail([string]$buildDir, [int]$lines = 40, [int]$maxFiles = 4) {
    $files = @(Get-HeadlessStepLogFiles $buildDir | Select-Object -First $maxFiles)
    if ($files.Count -eq 0) { return }

    Write-Host ""
    Write-Host "Recent build step logs:" -ForegroundColor Cyan
    foreach ($file in $files) {
        Write-Host ("  {0}" -f $file.FullName) -ForegroundColor DarkGray
        $content = @(Get-Content -LiteralPath $file.FullName -Tail $lines -ErrorAction SilentlyContinue)
        if ($content.Count -eq 0) {
            Write-Host "    (empty)" -ForegroundColor DarkGray
            continue
        }
        foreach ($line in $content) {
            Write-Host ("    {0}" -f $line)
        }
    }
}

function Write-JsonFile([string]$path, $value) {
    $value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $path -Encoding UTF8
}

function Ensure-QueueDir {
    if (-not (Test-Path -LiteralPath $QueueDir)) {
        New-Item -ItemType Directory -Path $QueueDir -Force | Out-Null
    }
}

function Enter-QueueStateLock {
    Ensure-QueueDir
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($true) {
        try {
            return [System.IO.File]::Open($QueueLockPath, [System.IO.FileMode]::OpenOrCreate, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        } catch [System.IO.IOException] {
            if ([DateTime]::UtcNow -ge $deadline) {
                throw "Timed out waiting for build queue state lock: $QueueLockPath"
            }
            Start-Sleep -Milliseconds 200
        }
    }
}

function Exit-QueueStateLock($stream) {
    if ($null -ne $stream) {
        try { $stream.Dispose() } catch {}
    }
}

function Read-QueueJson([string]$path) {
    try {
        if (-not (Test-Path -LiteralPath $path)) { return $null }
        return Get-Content -LiteralPath $path -Raw -ErrorAction Stop | ConvertFrom-Json
    } catch {
        return $null
    }
}

function Get-QueueRequestFiles {
    if (-not (Test-Path -LiteralPath $QueueDir)) { return @() }
    return @(Get-ChildItem -LiteralPath $QueueDir -Filter "*.request.json" -File -ErrorAction SilentlyContinue | Sort-Object Name)
}

function Get-BuildDurationEstimateSeconds([string]$target) {
    $defaults = @{
        client = 45
        tests  = 60
        all    = 60
    }
    $fallback = if ($defaults.ContainsKey($target)) { [double]$defaults[$target] } else { 60.0 }
    $history = Read-QueueJson $QueueDurationsPath
    if ($null -eq $history) { return $fallback }

    $items = @(@($history) |
        Where-Object {
            (Get-ObjectValue $_ "Target" "") -eq $target -and
            [int](Get-ObjectValue $_ "ExitCode" 1) -eq 0 -and
            [double](Get-ObjectValue $_ "DurationSeconds" 0) -gt 0
        } |
        Select-Object -Last 8)

    if ($items.Count -eq 0) { return $fallback }
    $sum = 0.0
    foreach ($item in $items) {
        $sum += [double](Get-ObjectValue $item "DurationSeconds" $fallback)
    }
    return [math]::Max(60.0, $sum / $items.Count)
}

function Get-QueueTimeoutSeconds($entry) {
    $timeoutSeconds = [int](Get-ObjectValue $entry "TimeoutSeconds" $BuildTimeoutSeconds)
    if ($timeoutSeconds -lt 0) { return 0 }
    return $timeoutSeconds
}

function Clear-StaleQueueState {
    foreach ($file in Get-QueueRequestFiles) {
        $entry = Read-QueueJson $file.FullName
        $wrapperPid = Get-ObjectValue $entry "WrapperPid" 0
        if ($null -eq $entry -or -not (Test-PidAlive $wrapperPid)) {
            Remove-Item -LiteralPath $file.FullName -Force -ErrorAction SilentlyContinue
        }
    }

    $active = Read-QueueJson $QueueActivePath
    if ($null -eq $active) {
        Remove-Item -LiteralPath $QueueActivePath -Force -ErrorAction SilentlyContinue
        return
    }

    $wrapperAlive = Test-PidAlive (Get-ObjectValue $active "WrapperPid" 0)
    $childPid = [int](Get-ObjectValue $active "ChildPid" 0)
    $childAlive = Test-PidAlive $childPid
    if (-not $wrapperAlive -and $childAlive) {
        $timeoutSeconds = Get-QueueTimeoutSeconds $active
        if ($timeoutSeconds -gt 0) {
            # Orphaned child (its wrapper died) but still alive: only reap it if
            # it has ALSO gone idle. A stateless idle check -- newest build-log
            # write vs now -- avoids killing a legitimate long build whose
            # wrapper crashed while ninja is still producing objects. Falls back
            # to StartedUtc when no logs exist yet.
            $buildDir = [string](Get-ObjectValue $active "BuildDir" "")
            $newestUtc = ConvertTo-UtcDateTime (Get-ObjectValue $active "StartedUtc" ([DateTime]::UtcNow.ToString("o")))
            if ($buildDir -ne "") {
                $signal = Get-BuildProgressSignal $buildDir
                if ($signal.NewestTicks -gt 0) {
                    $newestUtc = [DateTime]::new($signal.NewestTicks, [DateTimeKind]::Utc)
                }
            }
            $idle = ([DateTime]::UtcNow - $newestUtc).TotalSeconds
            if ($idle -ge $timeoutSeconds) {
                $session = Get-ObjectValue $active "Session" "unknown"
                Write-Warn ("Clearing stale build queue active record for '{0}' (owner gone, no output for {1}); stopping child process tree pid {2}." -f $session, (Format-DurationShort $idle), $childPid)
                Stop-ProcessTree $childPid
                Remove-Item -LiteralPath $QueueActivePath -Force -ErrorAction SilentlyContinue
                return
            }
        }
    }
    if (-not $wrapperAlive -and -not $childAlive) {
        $session = Get-ObjectValue $active "Session" "unknown"
        Write-Warn "Clearing stale build queue active record for '$session' (owner process is gone)."
        Remove-Item -LiteralPath $QueueActivePath -Force -ErrorAction SilentlyContinue
    }
}

function New-QueueSnapshot([string]$requestPath, [DateTime]$enqueuedUtc) {
    $now = [DateTime]::UtcNow
    $active = Read-QueueJson $QueueActivePath
    $entries = @(Get-QueueRequestFiles)
    $position = 0
    for ($i = 0; $i -lt $entries.Count; $i++) {
        if ($entries[$i].FullName -eq $requestPath) {
            $position = $i + 1
            break
        }
    }
    if ($position -eq 0) {
        throw "Build queue request disappeared: $requestPath"
    }

    $estimate = 0.0
    $activeSession = ""
    $activeTarget = ""
    $activeElapsed = 0.0
    if ($null -ne $active) {
        $activeSession = [string](Get-ObjectValue $active "Session" "")
        $activeTarget = [string](Get-ObjectValue $active "Target" "")
        $started = ConvertTo-UtcDateTime (Get-ObjectValue $active "StartedUtc" $now.ToString("o"))
        $activeElapsed = [math]::Max(0.0, ($now - $started).TotalSeconds)
        $estimate += [math]::Max(0.0, (Get-BuildDurationEstimateSeconds $activeTarget) - $activeElapsed)
    }

    for ($i = 0; $i -lt ($position - 1); $i++) {
        $entry = Read-QueueJson $entries[$i].FullName
        $queuedTarget = [string](Get-ObjectValue $entry "Target" "all")
        $estimate += Get-BuildDurationEstimateSeconds $queuedTarget
    }

    return [PSCustomObject]@{
        Position = $position
        QueueCount = $entries.Count
        WaitSeconds = ($now - $enqueuedUtc).TotalSeconds
        EstimatedWaitSeconds = $estimate
        ActiveSession = $activeSession
        ActiveTarget = $activeTarget
        ActiveElapsedSeconds = $activeElapsed
    }
}

function Write-QueueWaitStatus($snapshot) {
    $activeText = if ($snapshot.ActiveSession) {
        "$($snapshot.ActiveSession) ($($snapshot.ActiveTarget), elapsed $(Format-DurationShort $snapshot.ActiveElapsedSeconds))"
    } else {
        "none"
    }
    Write-Warn ("Build queue: waiting position {0}/{1}; active: {2}; estimated wait: {3}; waited: {4}." -f `
        $snapshot.Position,
        $snapshot.QueueCount,
        $activeText,
        (Format-DurationShort $snapshot.EstimatedWaitSeconds),
        (Format-DurationShort $snapshot.WaitSeconds))
}

function Enter-BuildQueue([string]$sessionName, [string]$target, [string]$buildDir, [int]$statusSeconds, [int]$timeoutSeconds) {
    if ($statusSeconds -lt 5) { $statusSeconds = 5 }
    if ($timeoutSeconds -lt 0) { $timeoutSeconds = 0 }
    Ensure-QueueDir

    $enqueuedUtc = [DateTime]::UtcNow
    $requestId = "{0}-{1}-{2}" -f $enqueuedUtc.Ticks, $PID, $sessionName
    $requestPath = Join-Path $QueueDir "$requestId.request.json"
    $request = [PSCustomObject]@{
        RequestId = $requestId
        Session = $sessionName
        Target = $target
        BuildDir = $buildDir
        WrapperPid = $PID
        EnqueuedUtc = $enqueuedUtc.ToString("o")
        Host = $env:COMPUTERNAME
        TimeoutSeconds = $timeoutSeconds
    }
    Write-JsonFile $requestPath $request
    Write-Info "Build queue: queued '$sessionName' for target '$target'. Do not use -NoQueue unless Mike explicitly asks."

    $lastStatusUtc = [DateTime]::MinValue
    while ($true) {
        $stateLock = $null
        try {
            $stateLock = Enter-QueueStateLock
            Clear-StaleQueueState
            $entries = @(Get-QueueRequestFiles)
            $active = Read-QueueJson $QueueActivePath
            $front = ($entries.Count -gt 0 -and $entries[0].FullName -eq $requestPath)

            if ($front -and $null -eq $active) {
                Remove-Item -LiteralPath $requestPath -Force -ErrorAction SilentlyContinue
                $now = [DateTime]::UtcNow
                $activeRecord = [PSCustomObject]@{
                    RequestId = $requestId
                    Session = $sessionName
                    Target = $target
                    BuildDir = $buildDir
                    WrapperPid = $PID
                    ChildPid = 0
                    StartedUtc = $now.ToString("o")
                    HeartbeatUtc = $now.ToString("o")
                    QueueWaitSeconds = ($now - $enqueuedUtc).TotalSeconds
                    TimeoutSeconds = $timeoutSeconds
                }
                Write-JsonFile $QueueActivePath $activeRecord
                Write-Ok ("Build queue: starting '{0}' after waiting {1}." -f $sessionName, (Format-DurationShort $activeRecord.QueueWaitSeconds))
                return [PSCustomObject]@{
                    RequestId = $requestId
                    StartedUtc = $now.ToString("o")
                    Session = $sessionName
                    Target = $target
                    TimeoutSeconds = $timeoutSeconds
                }
            }

            $snapshot = New-QueueSnapshot $requestPath $enqueuedUtc
        } finally {
            Exit-QueueStateLock $stateLock
        }

        if (([DateTime]::UtcNow - $lastStatusUtc).TotalSeconds -ge $statusSeconds) {
            Write-QueueWaitStatus $snapshot
            $lastStatusUtc = [DateTime]::UtcNow
        }
        # 2s poll (was 5s) so a queued build claims the freed slot faster; the
        # queue-state lock keeps this cheap and contention-safe.
        Start-Sleep -Seconds 2
    }
}

function Update-BuildQueueActive($queueToken, [int]$childPid, [string]$stdoutLog = "", [string]$stderrLog = "") {
    if ($null -eq $queueToken) { return }
    $stateLock = $null
    try {
        $stateLock = Enter-QueueStateLock
        $active = Read-QueueJson $QueueActivePath
        if ($null -eq $active) { return }
        if ([string](Get-ObjectValue $active "RequestId" "") -ne [string]$queueToken.RequestId) { return }
        $active | Add-Member -NotePropertyName HeartbeatUtc -NotePropertyValue ([DateTime]::UtcNow.ToString("o")) -Force
        if ($childPid -ge 0) {
            $active | Add-Member -NotePropertyName ChildPid -NotePropertyValue $childPid -Force
        }
        if ($stdoutLog -ne "") {
            $active | Add-Member -NotePropertyName StdoutLog -NotePropertyValue $stdoutLog -Force
        }
        if ($stderrLog -ne "") {
            $active | Add-Member -NotePropertyName StderrLog -NotePropertyValue $stderrLog -Force
        }
        Write-JsonFile $QueueActivePath $active
    } finally {
        Exit-QueueStateLock $stateLock
    }
}

function Add-BuildDurationRecord($queueToken, [int]$exitCode) {
    if ($null -eq $queueToken) { return }
    $started = ConvertTo-UtcDateTime (Get-ObjectValue $queueToken "StartedUtc" ([DateTime]::UtcNow.ToString("o")))
    $durationSeconds = [math]::Max(0.0, ([DateTime]::UtcNow - $started).TotalSeconds)
    $record = [PSCustomObject]@{
        Session = [string]$queueToken.Session
        Target = [string]$queueToken.Target
        DurationSeconds = [math]::Round($durationSeconds, 1)
        ExitCode = $exitCode
        CompletedUtc = [DateTime]::UtcNow.ToString("o")
    }

    $history = @(Read-QueueJson $QueueDurationsPath)
    if ($history.Count -eq 1 -and $null -eq $history[0]) { $history = @() }
    $history = @($history + $record) | Select-Object -Last 40
    Write-JsonFile $QueueDurationsPath $history
}

function Exit-BuildQueue($queueToken, [int]$exitCode) {
    if ($null -eq $queueToken) { return }
    $stateLock = $null
    try {
        $stateLock = Enter-QueueStateLock
        Add-BuildDurationRecord $queueToken $exitCode
        $active = Read-QueueJson $QueueActivePath
        if ($null -ne $active -and [string](Get-ObjectValue $active "RequestId" "") -eq [string]$queueToken.RequestId) {
            Remove-Item -LiteralPath $QueueActivePath -Force -ErrorAction SilentlyContinue
        }
    } finally {
        Exit-QueueStateLock $stateLock
    }
}

function Invoke-QueuedBuildChild([string[]]$childArgs, $queueToken, [int]$timeoutSeconds, [string]$buildDir) {
    if ($timeoutSeconds -lt 0) { $timeoutSeconds = 0 }
    if (-not (Test-Path -LiteralPath $buildDir)) {
        New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    }

    $stdoutLog = Get-BuildStdoutLogPath $buildDir
    $stderrLog = Get-BuildStderrLogPath $buildDir
    Set-Content -LiteralPath $stdoutLog -Value "" -Encoding UTF8
    Set-Content -LiteralPath $stderrLog -Value "" -Encoding UTF8

    Write-Info "Build output: $stdoutLog"
    Write-Info "Build errors: $stderrLog"
    $child = Start-Process -FilePath "powershell.exe" -ArgumentList $childArgs -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
    $startedUtc = [DateTime]::UtcNow
    # Idle-based watchdog: $timeoutSeconds is the max time with NO build output
    # (compile stalled / cc1 wedged), not a cap on total build time. A build
    # that keeps writing logs runs as long as it needs. See Get-BuildProgressSignal.
    $lastProgress = Get-BuildProgressSignal $buildDir
    $lastProgressUtc = [DateTime]::UtcNow
    Update-BuildQueueActive $queueToken $child.Id $stdoutLog $stderrLog
    # Poll every 2s (was 5s) for snappier completion detection; the cost is one
    # cheap file-stat sum per tick.
    while (-not $child.WaitForExit(2000)) {
        Update-BuildQueueActive $queueToken $child.Id $stdoutLog $stderrLog
        $progress = Get-BuildProgressSignal $buildDir
        if (Test-BuildProgressAdvanced $lastProgress $progress) {
            $lastProgress = $progress
            $lastProgressUtc = [DateTime]::UtcNow
        }
        $idle = ([DateTime]::UtcNow - $lastProgressUtc).TotalSeconds
        if ($timeoutSeconds -gt 0 -and $idle -ge $timeoutSeconds) {
            $totalElapsed = ([DateTime]::UtcNow - $startedUtc).TotalSeconds
            Write-Warn ("Build watchdog: session '{0}' target '{1}' produced no output for {2} (total elapsed {3}); treating as hung and stopping child process tree pid {4}." -f `
                (Get-ObjectValue $queueToken "Session" "unknown"),
                (Get-ObjectValue $queueToken "Target" "unknown"),
                (Format-DurationShort $idle),
                (Format-DurationShort $totalElapsed),
                $child.Id)
            Write-Warn "Build output log: $stdoutLog"
            Write-Warn "Build error log: $stderrLog"
            Show-ProcessTreeSnapshot $child.Id
            Stop-ProcessTree $child.Id
            try { [void]$child.WaitForExit(10000) } catch {}
            Show-HeadlessStepLogTail $buildDir 40 4
            Update-BuildQueueActive $queueToken $child.Id $stdoutLog $stderrLog
            return $QueueTimeoutExitCode
        }
    }
    Update-BuildQueueActive $queueToken $child.Id $stdoutLog $stderrLog
    return [int]$child.ExitCode
}

function ConvertTo-SafeSessionName([string]$value) {
    $name = $value.Trim()
    if ($name -eq "") {
        throw "Session name is empty."
    }

    $name = $name -replace '[^A-Za-z0-9._-]+', '-'
    $name = $name.Trim([char[]]".-_")
    if ($name.Length -gt 64) {
        $name = $name.Substring(0, 64).TrimEnd([char[]]".-_")
    }
    if ($name -eq "" -or $name -eq "." -or $name -eq "..") {
        throw "Session name '$value' does not contain any safe path characters."
    }

    $reserved = @("CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9")
    if ($reserved -contains $name.ToUpperInvariant()) {
        $name = "session-$name"
    }

    return $name
}

function Get-SessionName([bool]$requireStable) {
    if ($Session -ne "") {
        return ConvertTo-SafeSessionName $Session
    }
    if ($env:PD_BUILD_SESSION) {
        return ConvertTo-SafeSessionName $env:PD_BUILD_SESSION
    }
    if ($env:CODEX_SESSION_ID) {
        return ConvertTo-SafeSessionName $env:CODEX_SESSION_ID
    }
    if ($env:CLAUDE_SESSION_ID) {
        return ConvertTo-SafeSessionName $env:CLAUDE_SESSION_ID
    }
    if ($requireStable) {
        throw "Specify -Session <id> or set PD_BUILD_SESSION before removing a session build."
    }
    return ConvertTo-SafeSessionName "manual-$PID"
}

function Get-SafeChildPath([string]$childName) {
    $rootFull = [System.IO.Path]::GetFullPath($SessionBuildRoot).TrimEnd('\')
    $target = Join-Path $SessionBuildRoot $childName
    $targetFull = [System.IO.Path]::GetFullPath($target).TrimEnd('\')
    if (-not $targetFull.StartsWith($rootFull + "\", [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing path outside session build root: $targetFull"
    }
    return $targetFull
}

function Get-LockPath([string]$name) {
    return Join-Path $LockDir "$name.lock"
}

function Test-SessionLocked([string]$name) {
    return Test-Path -LiteralPath (Get-LockPath $name)
}

function Enter-SessionBuildLock([string]$name) {
    if (-not (Test-Path -LiteralPath $LockDir)) {
        New-Item -ItemType Directory -Path $LockDir -Force | Out-Null
    }

    $lockPath = Get-LockPath $name
    try {
        $stream = [System.IO.File]::Open($lockPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
        $bytes = [System.Text.Encoding]::UTF8.GetBytes("pid=$PID`nstarted=$([DateTime]::Now.ToString('s'))`nproject=$ProjectDir`n")
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush()
        return @{ Path = $lockPath; Stream = $stream }
    } catch [System.IO.IOException] {
        throw "Session build '$name' is already locked. Use a unique -Session for parallel builds, or remove stale lock '$lockPath' after confirming no build is running."
    }
}

function Exit-SessionBuildLock($lock) {
    if ($null -eq $lock) { return }
    try { $lock.Stream.Dispose() } catch {}
    try { Remove-Item -LiteralPath $lock.Path -Force -ErrorAction SilentlyContinue } catch {}
}

function Show-SessionBuilds {
    if (-not (Test-Path -LiteralPath $SessionBuildRoot)) {
        Write-Info "No session build directory exists yet: $SessionBuildRoot"
        return
    }

    $rows = @()
    Get-ChildItem -LiteralPath $SessionBuildRoot -Directory -Force |
        Where-Object { $_.Name -ne ".locks" -and $_.Name -ne ".queue" } |
        Sort-Object LastWriteTime -Descending |
        ForEach-Object {
            $rows += [PSCustomObject]@{
                Session = $_.Name
                Locked = if (Test-SessionLocked $_.Name) { "yes" } else { "no" }
                LastWrite = $_.LastWriteTime
                Path = $_.FullName
            }
        }

    if ($rows.Count -eq 0) {
        Write-Info "No session builds found under $SessionBuildRoot"
    } else {
        $rows | Format-Table -AutoSize
    }

    Show-BuildQueue
}

function Show-BuildQueue {
    Ensure-QueueDir
    $stateLock = $null
    try {
        $stateLock = Enter-QueueStateLock
        Clear-StaleQueueState
        $active = Read-QueueJson $QueueActivePath
        $entries = @(Get-QueueRequestFiles)

        Write-Host ""
        Write-Host "Build queue:" -ForegroundColor Cyan
        if ($null -ne $active) {
            $started = ConvertTo-UtcDateTime (Get-ObjectValue $active "StartedUtc" ([DateTime]::UtcNow.ToString("o")))
            $elapsed = ([DateTime]::UtcNow - $started).TotalSeconds
            $timeoutSeconds = Get-QueueTimeoutSeconds $active
            $timeoutText = if ($timeoutSeconds -gt 0) { Format-DurationShort $timeoutSeconds } else { "off" }
            $statusText = if ($timeoutSeconds -gt 0 -and $elapsed -ge $timeoutSeconds) { " status=over-timeout" } else { "" }
            Write-Host ("  Active: {0} target={1} elapsed={2} timeout={3} wrapperPid={4} childPid={5}{6}" -f `
                (Get-ObjectValue $active "Session" "unknown"),
                (Get-ObjectValue $active "Target" "unknown"),
                (Format-DurationShort $elapsed),
                $timeoutText,
                (Get-ObjectValue $active "WrapperPid" 0),
                (Get-ObjectValue $active "ChildPid" 0),
                $statusText) -ForegroundColor Yellow
            $buildDir = [string](Get-ObjectValue $active "BuildDir" "")
            $stdoutLog = [string](Get-ObjectValue $active "StdoutLog" "")
            $stderrLog = [string](Get-ObjectValue $active "StderrLog" "")
            if ($stdoutLog -eq "" -and $buildDir -ne "") {
                $stdoutLog = Get-BuildStdoutLogPath $buildDir
            }
            if ($stderrLog -eq "" -and $buildDir -ne "") {
                $stderrLog = Get-BuildStderrLogPath $buildDir
            }
            if (($stdoutLog -ne "" -and (Test-Path -LiteralPath $stdoutLog)) -or ($stderrLog -ne "" -and (Test-Path -LiteralPath $stderrLog))) {
                Write-Host ("          logs: out={0} err={1}" -f $stdoutLog, $stderrLog) -ForegroundColor DarkGray
            } elseif ($buildDir -ne "") {
                Write-Host "          logs: not captured (build was launched by an older wrapper)" -ForegroundColor DarkGray
            }
        } else {
            Write-Host "  Active: none" -ForegroundColor Gray
        }

        if ($entries.Count -eq 0) {
            Write-Host "  Waiting: none" -ForegroundColor Gray
        } else {
            $rows = @()
            for ($i = 0; $i -lt $entries.Count; $i++) {
                $entry = Read-QueueJson $entries[$i].FullName
                if ($null -eq $entry) { continue }
                $enqueued = ConvertTo-UtcDateTime (Get-ObjectValue $entry "EnqueuedUtc" ([DateTime]::UtcNow.ToString("o")))
                $rows += [PSCustomObject]@{
                    Position = $i + 1
                    Session = Get-ObjectValue $entry "Session" "unknown"
                    Target = Get-ObjectValue $entry "Target" "unknown"
                    Waiting = Format-DurationShort (([DateTime]::UtcNow - $enqueued).TotalSeconds)
                    Pid = Get-ObjectValue $entry "WrapperPid" 0
                }
            }
            if ($rows.Count -gt 0) { $rows | Format-Table -AutoSize }
        }
    } finally {
        Exit-QueueStateLock $stateLock
    }
}

function Show-BuildLogTail {
    if ($TailLines -lt 1) { $TailLines = 80 }

    $sessionName = ""
    $buildDir = ""
    $stdoutLog = ""
    $stderrLog = ""

    if ($Session -ne "") {
        $sessionName = Get-SessionName $true
        $buildDir = Get-SafeChildPath $sessionName
        $stdoutLog = Get-BuildStdoutLogPath $buildDir
        $stderrLog = Get-BuildStderrLogPath $buildDir
    } else {
        Ensure-QueueDir
        $active = Read-QueueJson $QueueActivePath
        if ($null -eq $active) {
            Write-Info "No active queued build to tail. Pass -Session <id> to tail a completed session log."
            return
        }
        $sessionName = [string](Get-ObjectValue $active "Session" "")
        $buildDir = [string](Get-ObjectValue $active "BuildDir" "")
        $stdoutLog = [string](Get-ObjectValue $active "StdoutLog" "")
        $stderrLog = [string](Get-ObjectValue $active "StderrLog" "")
        if ($stdoutLog -eq "" -and $buildDir -ne "") {
            $stdoutLog = Get-BuildStdoutLogPath $buildDir
        }
        if ($stderrLog -eq "" -and $buildDir -ne "") {
            $stderrLog = Get-BuildStderrLogPath $buildDir
        }
    }

    Write-Host ""
    Write-Host ("Build log tail: {0}" -f ($(if ($sessionName -ne "") { $sessionName } else { "unknown" }))) -ForegroundColor Cyan
    Write-Host ("  stdout: {0}" -f $stdoutLog) -ForegroundColor DarkGray
    Write-Host ("  stderr: {0}" -f $stderrLog) -ForegroundColor DarkGray

    $hasStdout = $stdoutLog -ne "" -and (Test-Path -LiteralPath $stdoutLog)
    $hasStderr = $stderrLog -ne "" -and (Test-Path -LiteralPath $stderrLog)
    if (-not $hasStdout -and -not $hasStderr) {
        Write-Warn "No captured build log exists for this session. It was probably launched before stdout/stderr capture was added."
        return
    }

    if ($hasStdout) {
        Write-Host ""
        Write-Host "stdout:" -ForegroundColor Cyan
        Get-Content -LiteralPath $stdoutLog -Tail $TailLines -ErrorAction SilentlyContinue
    }

    if ($hasStderr) {
        Write-Host ""
        Write-Host "stderr:" -ForegroundColor Cyan
        Get-Content -LiteralPath $stderrLog -Tail $TailLines -ErrorAction SilentlyContinue
    }

    Show-HeadlessStepLogTail $buildDir $TailLines 4

    if ($Follow) {
        if (-not $hasStdout) {
            Write-Warn "Cannot follow stdout because the stdout log is missing."
            return
        }
        Write-Host ""
        Write-Host "Following stdout log. Press Ctrl+C to stop." -ForegroundColor Cyan
        Get-Content -LiteralPath $stdoutLog -Tail 0 -Wait
    }
}

function Remove-SessionBuild([string]$name) {
    $targetFull = Get-SafeChildPath $name
    $lockPath = Get-LockPath $name

    if ((Test-Path -LiteralPath $lockPath) -and -not $Force) {
        Write-Warn "Skipping locked session '$name'. If no build is running, rerun with -Force."
        return
    }

    if (Test-Path -LiteralPath $targetFull) {
        Remove-Item -LiteralPath $targetFull -Recurse -Force
        Write-Ok "Removed session build: $targetFull"
    } else {
        Write-Info "Session build not found: $targetFull"
    }

    if ($Force -and (Test-Path -LiteralPath $lockPath)) {
        Remove-Item -LiteralPath $lockPath -Force
        Write-Ok "Removed stale lock: $lockPath"
    }
}

if ($SelfTest) {
    # Fast, build-free unit checks for the queue's pure helpers (2026-07-02).
    # Runs in well under a second, spawns nothing, touches only a temp dir.
    # Invoke: .\devtools\build-session.ps1 -SelfTest
    $failures = 0
    function Assert-QueueTrue([bool]$cond, [string]$name) {
        if ($cond) {
            Write-Host ("  [PASS] {0}" -f $name) -ForegroundColor Green
        } else {
            Write-Host ("  [FAIL] {0}" -f $name) -ForegroundColor Red
            $script:failures++
        }
    }

    Write-Host "build-session queue self-test" -ForegroundColor Cyan

    # ConvertTo-SafeSessionName: sanitization, truncation, reserved names.
    Assert-QueueTrue ((ConvertTo-SafeSessionName "a b/c") -eq "a-b-c") "session name spaces/slashes collapse"
    Assert-QueueTrue ((ConvertTo-SafeSessionName "..dots..") -eq "dots") "session name trims leading/trailing dots"
    Assert-QueueTrue ((ConvertTo-SafeSessionName "NUL") -eq "session-NUL") "reserved device name is prefixed"
    Assert-QueueTrue ((ConvertTo-SafeSessionName ("x" * 200)).Length -le 64) "session name capped at 64 chars"
    $threw = $false
    try { [void](ConvertTo-SafeSessionName "   ") } catch { $threw = $true }
    Assert-QueueTrue $threw "blank session name throws"

    # Get-QueueTimeoutSeconds: default, explicit, negative -> 0 (no watchdog).
    Assert-QueueTrue ((Get-QueueTimeoutSeconds ([PSCustomObject]@{})) -eq $BuildTimeoutSeconds) "timeout defaults to BuildTimeoutSeconds"
    Assert-QueueTrue ((Get-QueueTimeoutSeconds ([PSCustomObject]@{ TimeoutSeconds = 45 })) -eq 45) "explicit timeout honored"
    Assert-QueueTrue ((Get-QueueTimeoutSeconds ([PSCustomObject]@{ TimeoutSeconds = -1 })) -eq 0) "negative timeout means no watchdog"

    # Progress signal + advance detection (the watchdog's core).
    $tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("pdq-selftest-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $tmp -Force | Out-Null
    try {
        $sig0 = Get-BuildProgressSignal $tmp
        Assert-QueueTrue ($sig0.Bytes -eq 0) "empty build dir has zero progress bytes"
        Assert-QueueTrue (-not (Test-BuildProgressAdvanced $sig0 $sig0)) "identical signal is not advanced"
        Assert-QueueTrue (Test-BuildProgressAdvanced $null $sig0) "null previous always counts as advanced"

        $stdout = Get-BuildStdoutLogPath $tmp
        Set-Content -LiteralPath $stdout -Value "compiling..." -Encoding UTF8
        $sig1 = Get-BuildProgressSignal $tmp
        Assert-QueueTrue ($sig1.Bytes -gt $sig0.Bytes) "writing a log grows the byte signal"
        Assert-QueueTrue (Test-BuildProgressAdvanced $sig0 $sig1) "byte growth is detected as progress"

        Add-Content -LiteralPath $stdout -Value "more output" -Encoding UTF8
        $sig2 = Get-BuildProgressSignal $tmp
        Assert-QueueTrue (Test-BuildProgressAdvanced $sig1 $sig2) "appending more output is progress"
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host ""
    if ($failures -eq 0) {
        Write-Ok "build-session self-test: all checks passed"
        exit 0
    }
    Write-Warn ("build-session self-test: {0} check(s) failed" -f $failures)
    exit 1
}

if ($Tail) {
    Show-BuildLogTail
    exit 0
}

if ($List) {
    Show-SessionBuilds
    exit 0
}

if ($RemoveAll) {
    if (-not (Test-Path -LiteralPath $SessionBuildRoot)) {
        Write-Info "No session build directory exists yet: $SessionBuildRoot"
        exit 0
    }

    Get-ChildItem -LiteralPath $SessionBuildRoot -Directory -Force |
        Where-Object { $_.Name -ne ".locks" -and $_.Name -ne ".queue" } |
        ForEach-Object { Remove-SessionBuild $_.Name }
    exit 0
}

if ($Remove) {
    $nameToRemove = Get-SessionName $true
    Remove-SessionBuild $nameToRemove
    exit 0
}

$sessionName = Get-SessionName $false
$buildDir = Get-SafeChildPath $sessionName
$headless = Join-Path $ScriptDir "build-headless.ps1"
$gitForWindows = "C:\Program Files\Git\cmd"

if (-not (Test-Path -LiteralPath $headless)) {
    throw "Missing canonical build script: $headless"
}

Write-Host ""
Write-Host "  Perfect Dark PC Port - Session Build" -ForegroundColor Cyan
Write-Host "  Session:  $sessionName" -ForegroundColor Gray
Write-Host "  Target:   $Target" -ForegroundColor Gray
Write-Host "  BuildDir: $buildDir" -ForegroundColor DarkGray
Write-Host ""

$queueToken = $null
$lock = $null
$exitCode = 1
try {
    if (-not $NoQueue) {
        $queueToken = Enter-BuildQueue $sessionName $Target $buildDir $QueueStatusSeconds $BuildTimeoutSeconds
    } else {
        Write-Warn "Build queue bypassed by -NoQueue. This should only happen when Mike explicitly asks."
    }

    $lock = Enter-SessionBuildLock $sessionName

    $buildArgs = @(
        "-Target", $Target,
        "-OutputDir", $buildDir
    )
    if ($Version -ne "") { $buildArgs += @("-Version", $Version) }
    if ($DevMods -ne "") { $buildArgs += @("-DevMods", $DevMods) }
    if ($Clean) { $buildArgs += "-Clean" }
    if ($Verbose) { $buildArgs += "-Verbose" }

    # build-headless.ps1 intentionally calls exit; run it in a child process so
    # this wrapper can always release the per-session lock afterward.
    if (Test-Path -LiteralPath (Join-Path $gitForWindows "git.exe")) {
        $env:PATH = "$gitForWindows;$env:PATH"
    }
    $childArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $headless) + $buildArgs
    if ($NoQueue) {
        & powershell.exe @childArgs
        $exitCode = $LASTEXITCODE
    } else {
        $exitCode = Invoke-QueuedBuildChild $childArgs $queueToken $BuildTimeoutSeconds $buildDir
    }
} finally {
    Exit-SessionBuildLock $lock
    Exit-BuildQueue $queueToken $exitCode
}

Write-Host ""
Write-Host "  Session build cleanup:" -ForegroundColor Cyan
Write-Host "    .\devtools\build-session.ps1 -Remove -Session $sessionName" -ForegroundColor Gray

exit $exitCode
