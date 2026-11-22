#Requires -Version 7.0
<#
PD2 no-worktree build queue (T-TOOLING-020; spec "PD2 No-Worktree Build Queue",
claude.ai artifact BkFQmsJc6uyEjvFeeVgPJw, paths on C: per Mike: "Builds on C:").

Sessions read a clean dev clone (<root>\dev), edit copies in <root>\scratch\<session>,
and submit patches. One worker (Queue-Service.ps1) builds each patch from scratch on
the latest dev in <root>\jobs\<job>, tests it, and pushes it to dev or reports why not.

State lives in <root>\queue and changes only under one named mutex. Every file is
UTF-8 JSON or JSON Lines with a schema field, written to a temp file and renamed.
Every program is started by full path (B-1283: never open anything by file type).
#>
Set-StrictMode -Version Latest

function Get-Pd2qGitExe {
    # Git for Windows' git.exe, started by its full path. Where it lives differs
    # (GitHub's Windows runners have no mingw64\bin copy), so the known paths
    # come first, then git.exe on PATH. Only an .exe is ever returned.
    foreach ($p in @('C:\Program Files\Git\mingw64\bin\git.exe', 'C:\Program Files\Git\cmd\git.exe', 'C:\Program Files\Git\bin\git.exe')) {
        if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    }
    $found = @(Get-Command -Name 'git.exe' -CommandType Application -ErrorAction SilentlyContinue | Where-Object { $_.Source -like '*.exe' })
    if ($found) { return $found[0].Source }
    throw 'git.exe was not found (Git for Windows).'
}
$script:Git = Get-Pd2qGitExe
$script:LeaseMinutes = 10
$script:IssuePattern = '^[A-Z][A-Z0-9-]{1,40}$'
$script:SessionPattern = '^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$'
$script:Agents = @('claude', 'codex')
$script:FinalStates = @('landed', 'rejected', 'failed', 'cancelled')
$script:BootUtc = $null

# ---------------------------------------------------------------- paths, time

function Get-Pd2qRoot {
    if ($env:PD2Q_ROOT) { return [IO.Path]::GetFullPath($env:PD2Q_ROOT).TrimEnd('\') }
    return 'C:\PD2'
}

function Get-Pd2qPaths {
    $root = Get-Pd2qRoot
    $queue = Join-Path $root 'queue'
    [pscustomobject]@{
        Root = $root; Dev = Join-Path $root 'dev'; Queue = $queue; Jobs = Join-Path $root 'jobs'
        Scratch = Join-Path $root 'scratch'; Reports = Join-Path $root 'reports'
        Journal = Join-Path $queue 'journal'; Tasks = Join-Path $queue 'tasks'; Requests = Join-Path $queue 'requests'
        Leases = Join-Path $queue 'leases.json'; Counters = Join-Path $queue 'counters.json'
        Agents = Join-Path $queue 'agents.json'; Board = Join-Path $queue 'board.jsonl'
        Builds = Join-Path $queue 'builds.jsonl'; ServiceLog = Join-Path $queue 'service.log'
        Alarms = Join-Path $queue 'alarms.json'; Service = Join-Path $queue 'service.json'
    }
}

function Initialize-Pd2qLayout {
    $p = Get-Pd2qPaths
    foreach ($dir in @($p.Root, $p.Queue, $p.Jobs, $p.Scratch, $p.Reports, $p.Journal, $p.Tasks, $p.Requests)) {
        [IO.Directory]::CreateDirectory($dir) | Out-Null
    }
    return $p
}

function Get-Pd2qNow {
    # Tests move the clock with PD2Q_TEST_NOW_UTC; production uses the real clock.
    if ($env:PD2Q_TEST_NOW_UTC) { return [datetime]::Parse($env:PD2Q_TEST_NOW_UTC, $null, 'AdjustToUniversal,AssumeUniversal') }
    return [datetime]::UtcNow
}

function Format-Pd2qTime([datetime] $Time) { return $Time.ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ') }
function ConvertFrom-Pd2qTime([string] $Text) { return [datetime]::Parse($Text, $null, 'AdjustToUniversal,AssumeUniversal') }

function Get-Pd2qBootUtc {
    # Tests fake a reboot with PD2Q_TEST_BOOT_UTC.
    if ($env:PD2Q_TEST_BOOT_UTC) { return Format-Pd2qTime (ConvertFrom-Pd2qTime $env:PD2Q_TEST_BOOT_UTC) }
    if (-not $script:BootUtc) {
        # The OS's own record, identical in every process (a TickCount-based
        # estimate drifts by sleep time and disagreed with it by 13 hours).
        $boot = (Get-CimInstance Win32_OperatingSystem -ErrorAction Stop).LastBootUpTime.ToUniversalTime()
        $script:BootUtc = Format-Pd2qTime $boot
    }
    return $script:BootUtc
}

function Assert-Pd2qIssue([string] $Issue) {
    if ($Issue -cnotmatch $script:IssuePattern) { throw "Issue id must match $($script:IssuePattern): '$Issue'" }
}
function Assert-Pd2qSession([string] $Session) {
    if ($Session -notmatch $script:SessionPattern) { throw "Session id must match $($script:SessionPattern): '$Session'" }
    # A session names its scratch folder, and Windows drops a trailing dot from
    # a folder name and treats CON, NUL, COM1 and the like as devices. (Case is
    # handled where sessions are compared: Windows ignores it in paths.)
    if ($Session.EndsWith('.')) { throw "Session id may not end with a dot: '$Session'" }
    if ($Session -match '^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\..*)?$') { throw "Session id '$Session' is a Windows device name." }
}

# ---------------------------------------------------------------- lock and files

function Get-Pd2qMutexName {
    $root = (Get-Pd2qRoot).ToLowerInvariant()
    if ($root -eq 'c:\pd2') { return 'Global\PD2Queue' }
    $sha = [Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($root))
    return 'Global\PD2Queue-' + [Convert]::ToHexString($sha).Substring(0, 12)
}

function Invoke-Pd2qLocked([scriptblock] $Action, [int] $TimeoutSeconds = 60) {
    $mutex = [Threading.Mutex]::new($false, (Get-Pd2qMutexName))
    $owned = $false
    try {
        try { $owned = $mutex.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds)) }
        catch [Threading.AbandonedMutexException] { $owned = $true }  # the last holder died; state is atomic files
        if (-not $owned) { throw "The PD2 queue lock ($(Get-Pd2qMutexName)) stayed busy for $TimeoutSeconds s." }
        & $Action
    } finally {
        if ($owned) { $mutex.ReleaseMutex() }
        $mutex.Dispose()
    }
}

function Read-Pd2qJson([string] $Path, $Default = $null) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $Default }
    # -DateKind String: times stay the exact strings written (PowerShell 7
    # otherwise turns ISO strings into local DateTime values).
    return [IO.File]::ReadAllText($Path, [Text.UTF8Encoding]::new($false)) | ConvertFrom-Json -AsHashtable -DateKind String
}

function Write-Pd2qJson([string] $Path, $Value) {
    $dir = Split-Path -Parent $Path
    [IO.Directory]::CreateDirectory($dir) | Out-Null
    $temp = Join-Path $dir ('.' + (Split-Path -Leaf $Path) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
    [IO.File]::WriteAllText($temp, ($Value | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $Path -Force
}

function Add-Pd2qJsonLine([string] $Path, $Value) {
    [IO.Directory]::CreateDirectory((Split-Path -Parent $Path)) | Out-Null
    $line = ($Value | ConvertTo-Json -Depth 20 -Compress) + "`n"
    $stream = [IO.File]::Open($Path, 'Append', 'Write', 'Read')
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($line)
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)  # on disk before the work it describes starts
    } finally { $stream.Dispose() }
}

function Read-Pd2qJsonLines([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return @() }
    return @(Get-Content -LiteralPath $Path -Encoding utf8 | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json -AsHashtable -DateKind String })
}

function Write-Pd2qLog([string] $Message) {
    $p = Get-Pd2qPaths
    [IO.Directory]::CreateDirectory($p.Queue) | Out-Null
    [IO.File]::AppendAllText($p.ServiceLog, ('{0} {1}' -f (Format-Pd2qTime (Get-Pd2qNow)), $Message) + "`n", [Text.UTF8Encoding]::new($false))
}

function Remove-Pd2qTree([string] $Path, [string[]] $AllowedUnder) {
    # Deletes only below the given queue folders, never through a junction or
    # symlink (spec: "Delete with a path check").
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $ok = $false
    foreach ($base in $AllowedUnder) {
        $b = [IO.Path]::GetFullPath($base).TrimEnd('\') + '\'
        if ($full.StartsWith($b, [StringComparison]::OrdinalIgnoreCase)) { $ok = $true }
    }
    if (-not $ok) { throw "Refusing to delete outside the queue folders: $full" }
    if (-not (Test-Path -LiteralPath $full)) { return }
    $cursor = $full
    while ($cursor -and $cursor.Length -gt 3) {
        $item = Get-Item -LiteralPath $cursor -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw "Refusing to delete through a link: $cursor" }
        $cursor = Split-Path -Parent $cursor
    }
    foreach ($item in @(Get-ChildItem -LiteralPath $full -Recurse -Force -Attributes ReparsePoint -ErrorAction SilentlyContinue)) {
        # A link inside is removed itself, never followed.
        [IO.Directory]::Delete($item.FullName) 2>$null
        if (Test-Path -LiteralPath $item.FullName) { Remove-Item -LiteralPath $item.FullName -Force }
    }
    Get-ChildItem -LiteralPath $full -Recurse -Force -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReadOnly } |
        ForEach-Object { $_.Attributes = $_.Attributes -band (-bnot [IO.FileAttributes]::ReadOnly) }
    Remove-Item -LiteralPath $full -Recurse -Force
}

function Get-Pd2qTreeBytes([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return [long]0 }
    $sum = [long]0
    foreach ($f in @(Get-ChildItem -LiteralPath $Path -Recurse -Force -File -ErrorAction SilentlyContinue)) { $sum += $f.Length }
    return $sum
}

# ---------------------------------------------------------------- processes

function Get-Pd2qProcessStartUtc([int] $ProcessId) {
    try { return Format-Pd2qTime ([Diagnostics.Process]::GetProcessById($ProcessId).StartTime.ToUniversalTime()) }
    catch { return $null }
}

function Get-Pd2qAgentHosts {
    $file = Join-Path $PSScriptRoot 'agent-hosts.txt'
    $hosts = @()
    if (Test-Path -LiteralPath $file) {
        $hosts = @(Get-Content -LiteralPath $file | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') } |
            ForEach-Object { ($_ -split '\s+')[0].ToLowerInvariant() })
    }
    if (-not $hosts) { $hosts = @('claude.exe', 'codex.exe', 'node.exe') }
    return $hosts
}

function Resolve-Pd2qOwner([int] $OwnerPid = 0) {
    # The CLI runs in a short-lived shell; the lease belongs to the long-lived
    # agent host above it (Milestone 2 found it: the session's own claude.exe).
    if ($OwnerPid -gt 0) {
        $start = Get-Pd2qProcessStartUtc $OwnerPid
        if (-not $start) { throw "Owner process $OwnerPid is not running." }
        $name = try { [Diagnostics.Process]::GetProcessById($OwnerPid).ProcessName + '.exe' } catch { '?' }
        return @{ pid = $OwnerPid; pidStartUtc = $start; process = $name.ToLowerInvariant() }
    }
    $hosts = Get-Pd2qAgentHosts
    $id = $PID
    for ($i = 0; $i -lt 32 -and $id -gt 0; $i++) {
        $proc = Get-CimInstance Win32_Process -Filter "ProcessId = $id" -ErrorAction SilentlyContinue
        if (-not $proc) { break }
        if ($hosts -contains ([string]$proc.Name).ToLowerInvariant()) {
            return @{ pid = [int]$proc.ProcessId; pidStartUtc = (Get-Pd2qProcessStartUtc ([int]$proc.ProcessId)); process = ([string]$proc.Name).ToLowerInvariant() }
        }
        $id = [int]$proc.ParentProcessId
    }
    throw "No agent host ($($hosts -join ', ')) above this process; pass -OwnerPid."
}

function Get-Pd2qAgentOf([string] $ProcessName) {
    if ($ProcessName -like 'codex*') { return 'codex' }
    return 'claude'
}

# ---------------------------------------------------------------- counters, agents, alarms

function New-Pd2qNumber([ValidateSet('lease', 'job')] [string] $Kind) {
    $p = Get-Pd2qPaths
    $c = Read-Pd2qJson $p.Counters @{ schema = 'pd2q.counters.v1'; lease = 0; job = 0 }
    $c[$Kind] = [long]$c[$Kind] + 1
    Write-Pd2qJson $p.Counters $c
    return [long]$c[$Kind]
}

function Get-Pd2qAgents {
    $p = Get-Pd2qPaths
    $a = Read-Pd2qJson $p.Agents @{ schema = 'pd2q.agents.v1'; claude = @{ state = 'on' }; codex = @{ state = 'off'; reason = 'Mike switched Codex off on 2026-11-20' } }
    $changed = $false
    foreach ($agent in $script:Agents) {
        if (-not $a.ContainsKey($agent)) { $a[$agent] = @{ state = 'on' }; $changed = $true }
        $entry = $a[$agent]
        if ($entry.state -eq 'off' -and $entry.ContainsKey('until') -and $entry.until -and (Get-Pd2qNow) -ge (ConvertFrom-Pd2qTime $entry.until)) {
            $a[$agent] = @{ state = 'on'; reason = 'back on by itself at ' + $entry.until }
            $changed = $true
        }
    }
    if ($changed) { Write-Pd2qJson $p.Agents $a }
    return $a
}

function Set-Pd2qAgent([string] $Agent, [ValidateSet('on', 'off')] [string] $State, [string] $Until = '', [string] $Reason = '', [string] $By = '') {
    if ($script:Agents -notcontains $Agent) { throw "Unknown agent '$Agent' (claude or codex)." }
    $p = Get-Pd2qPaths
    $a = Get-Pd2qAgents
    $entry = @{ state = $State; reason = $Reason; by = $By; atUtc = Format-Pd2qTime (Get-Pd2qNow) }
    if ($Until) { $entry.until = Format-Pd2qTime (ConvertFrom-Pd2qTime $Until) }
    $a[$Agent] = $entry
    Write-Pd2qJson $p.Agents $a
    Write-Pd2qLog "agent $Agent $State$(if ($Until) { ' until ' + $entry.until }) by $By ($Reason)"
}

function Add-Pd2qAlarm([string] $Kind, [string] $Detail) {
    $p = Get-Pd2qPaths
    $alarms = Read-Pd2qJson $p.Alarms @{ schema = 'pd2q.alarms.v1'; alarms = @() }
    $alarms.alarms = @($alarms.alarms) + @(@{ kind = $Kind; detail = $Detail; atUtc = Format-Pd2qTime (Get-Pd2qNow) })
    Write-Pd2qJson $p.Alarms $alarms
    Write-Pd2qLog "ALARM $Kind $Detail"
}

# ---------------------------------------------------------------- tasks and scratch

function Get-Pd2qTask([string] $Issue) {
    $p = Get-Pd2qPaths
    return Read-Pd2qJson (Join-Path $p.Tasks "$Issue.json") $null
}

function Save-Pd2qTask($Task) {
    $p = Get-Pd2qPaths
    Write-Pd2qJson (Join-Path $p.Tasks "$($Task.issue).json") $Task
}

function Get-Pd2qScratchDir([string] $Session) {
    Assert-Pd2qSession $Session
    return Join-Path (Get-Pd2qPaths).Scratch $Session
}

function Set-Pd2qTaskResumable([string] $Issue, [string] $Note) {
    # A job that ends without running (cancelled or dropped while queued) gives
    # its saved patch back to the task, as Complete-Job does for a running one:
    # submit cleared the scratch, so the next claim must restore the patch.
    $task = Get-Pd2qTask $Issue
    if (-not $task) { return }
    $task.state = 'started'
    $task.notes = @($task.notes) + @(@{ atUtc = Format-Pd2qTime (Get-Pd2qNow); agent = 'pd2q'; session = 'pd2q'; note = $Note })
    Save-Pd2qTask $task
}

function Clear-Pd2qScratch([string] $Session) {
    $p = Get-Pd2qPaths
    $dir = Get-Pd2qScratchDir $Session
    if (Test-Path -LiteralPath $dir) { Remove-Pd2qTree $dir @($p.Scratch) }
}

function Move-Pd2qScratchAside([string] $Session) {
    # A scratch whose edits could not be saved is the only copy of them. It moves
    # to scratch\_kept (no session name starts with "_"), so the session's next
    # claim can neither clear it nor mix it into another issue's patch. Returns
    # the kept path; if the move fails, the scratch stays where it is.
    $p = Get-Pd2qPaths
    $dir = Get-Pd2qScratchDir $Session
    if (-not (Test-Path -LiteralPath $dir)) { return $null }
    $keptRoot = Join-Path $p.Scratch '_kept'
    [IO.Directory]::CreateDirectory($keptRoot) | Out-Null
    $dest = Join-Path $keptRoot ('{0}-{1}' -f $Session, (Get-Pd2qNow).ToString('yyyyMMddTHHmmssfff'))
    try { [IO.Directory]::Move($dir, $dest); return $dest } catch { return $dir }
}

function Get-Pd2qScratchFiles([string] $Session) {
    $dir = Get-Pd2qScratchDir $Session
    if (-not (Test-Path -LiteralPath $dir)) { return @() }
    # Relative paths of every edited file; base.txt and .pd2q-deleted at the
    # scratch root are the queue's own bookkeeping.
    $rootLen = ([IO.Path]::GetFullPath($dir).TrimEnd('\') + '\').Length
    return @(Get-ChildItem -LiteralPath $dir -Recurse -Force -File |
        ForEach-Object { $_.FullName.Substring($rootLen).Replace('\', '/') } |
        Where-Object { $_ -cnotin @('base.txt', '.pd2q-deleted') })
}

function New-Pd2qPatchFromScratch([string] $Session) {
    # A throwaway git repository makes an exact patch: base versions committed,
    # scratch versions staged, `git diff --cached --binary`. Nothing is written
    # to the dev checkout (reads only: git show).
    $p = Get-Pd2qPaths
    $dir = Get-Pd2qScratchDir $Session
    $baseFile = Join-Path $dir 'base.txt'
    if (-not (Test-Path -LiteralPath $baseFile)) { throw "Scratch of '$Session' has no base.txt; use pd2q edit." }
    $base = ([IO.File]::ReadAllText($baseFile)).Trim()
    $edited = @(Get-Pd2qScratchFiles $Session)
    $deletedList = Join-Path $dir '.pd2q-deleted'
    $deleted = @(if (Test-Path -LiteralPath $deletedList) { Get-Content -LiteralPath $deletedList | Where-Object { $_.Trim() } })
    if (-not $edited -and -not $deleted) { throw "Scratch of '$Session' holds no edits." }
    $temp = Join-Path ([IO.Path]::GetTempPath()) ('pd2q-patch-' + [guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    try {
        [void](Invoke-Pd2qGit $temp @('init', '-q'))
        [void](Invoke-Pd2qGit $temp @('config', 'core.autocrlf', 'false'))
        [void](Invoke-Pd2qGit $temp @('config', 'user.name', 'pd2q'))
        [void](Invoke-Pd2qGit $temp @('config', 'user.email', 'pd2q@localhost'))
        foreach ($rel in @($edited) + @($deleted)) {
            $content = Invoke-Pd2qGitBytes $p.Dev @('show', "$($base):$rel") -AllowFailure
            if ($null -ne $content) {
                $target = Join-Path $temp $rel
                [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
                [IO.File]::WriteAllBytes($target, $content)
            }
        }
        [void](Invoke-Pd2qGit $temp @('add', '-A'))
        [void](Invoke-Pd2qGit $temp @('commit', '-q', '--allow-empty', '-m', 'base'))
        foreach ($rel in $edited) {
            $target = Join-Path $temp $rel
            [IO.Directory]::CreateDirectory((Split-Path -Parent $target)) | Out-Null
            Copy-Item -LiteralPath (Join-Path $dir $rel) -Destination $target -Force
        }
        foreach ($rel in $deleted) { $t = Join-Path $temp $rel; if (Test-Path -LiteralPath $t) { Remove-Item -LiteralPath $t -Force } }
        [void](Invoke-Pd2qGit $temp @('add', '-A'))
        $patch = Invoke-Pd2qGitBytes $temp @('diff', '--cached', '--binary', '--full-index')
        if (-not $patch -or $patch.Length -eq 0) { throw 'The scratch files equal their base versions; nothing to submit.' }
        return @{ Patch = $patch; Base = $base; Files = @($edited) + @($deleted) }
    } finally {
        Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Save-Pd2qCheckpoint([string] $Issue, [string] $Session, [string] $Note = '', [string] $Agent = 'claude') {
    # A task's scratch diff is kept in the queue, never only in a session's own
    # folders (spec: those lost work this week).
    $p = Get-Pd2qPaths
    $task = Get-Pd2qTask $Issue
    if (-not $task) { $task = @{ schema = 'pd2q.task.v1'; issue = $Issue; state = 'started'; notes = @() } }
    try {
        $made = New-Pd2qPatchFromScratch $Session
        [IO.File]::WriteAllBytes((Join-Path $p.Tasks "$Issue.patch"), $made.Patch)
        $task.base = $made.Base
        $task.files = @($made.Files)
        $task.patch = "$Issue.patch"
    } catch {
        $why = $_.Exception.Message
        if ($why -match 'holds no edits|nothing to submit') {
            # The scratch is back at its base: no work is in progress now, so an
            # older saved patch must not come back for the next holder.
            Remove-Item -LiteralPath (Join-Path $p.Tasks "$Issue.patch") -Force -ErrorAction SilentlyContinue
            if ($task.ContainsKey('patch')) { $task.Remove('patch') }
            $task.files = @()
        } elseif ($why -notmatch 'has no base.txt') { throw }
    }
    $task.state = 'started'
    $task.agent = $Agent
    $task.session = $Session
    $task.checkpointUtc = Format-Pd2qTime (Get-Pd2qNow)
    if ($Note) { $task.notes = @($task.notes) + @(@{ atUtc = $task.checkpointUtc; agent = $Agent; session = $Session; note = $Note }) }
    Save-Pd2qTask $task
    return $task
}

# ---------------------------------------------------------------- git

function Invoke-Pd2qGit([string] $Dir, [string[]] $GitArgs, [switch] $AllowFailure) {
    # git arguments always travel as one array: PowerShell would otherwise read
    # `-A` or `-m` as its own parameter names.
    $out = & $script:Git -C $Dir @GitArgs 2>&1
    if ($LASTEXITCODE -ne 0 -and -not $AllowFailure) {
        throw "git $($GitArgs -join ' ') in $Dir failed ($LASTEXITCODE): $(($out | Out-String).Trim())"
    }
    return ($out | Out-String).Trim()
}

function Invoke-Pd2qGitBytes([string] $Dir, [string[]] $GitArgs, [switch] $AllowFailure) {
    $allowFailure = [bool]$AllowFailure
    $args2 = $GitArgs
    $psi = [Diagnostics.ProcessStartInfo]::new($script:Git)
    $psi.ArgumentList.Add('-C'); $psi.ArgumentList.Add($Dir)
    foreach ($a in $args2) { $psi.ArgumentList.Add($a) }
    $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.UseShellExecute = $false
    $proc = [Diagnostics.Process]::Start($psi)
    $ms = [IO.MemoryStream]::new()
    $errTask = $proc.StandardError.ReadToEndAsync()
    $proc.StandardOutput.BaseStream.CopyTo($ms)
    $proc.WaitForExit()
    if ($proc.ExitCode -ne 0) {
        if ($allowFailure) { return $null }
        throw "git $($args2 -join ' ') in $Dir failed ($($proc.ExitCode)): $($errTask.Result.Trim())"
    }
    # The comma keeps the array whole: PowerShell unrolls a returned array, and
    # an empty one (an empty file, an empty diff) reached the caller as $null,
    # which reads as "git failed" (B-1307).
    return , $ms.ToArray()
}

function Get-Pd2qPatchPaths([string] $PatchFile) {
    # Every path a patch touches, both sides of a rename or copy included, as git
    # itself reads them: git apply --numstat -z writes them verbatim (no quoting,
    # spaces kept), and a rename's empty name field is followed by its old and
    # new paths.
    $bytes = Invoke-Pd2qGitBytes (Get-Pd2qPaths).Dev @('apply', '--numstat', '-z', [IO.Path]::GetFullPath($PatchFile))
    $tokens = [Text.Encoding]::UTF8.GetString($bytes).Split([char]0)
    $found = [Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $tokens.Count; $i++) {
        if (-not $tokens[$i]) { continue }
        $parts = $tokens[$i] -split "`t", 3
        if ($parts.Count -lt 3) { continue }
        if ($parts[2]) { $found.Add($parts[2]) }
        elseif ($i + 2 -lt $tokens.Count) { $found.Add($tokens[$i + 1]); $found.Add($tokens[$i + 2]); $i += 2 }
    }
    # git apply's numstat names only a rename's or copy's new side. The old side
    # is in the section's "rename from" / "copy from" line, the whole rest of the
    # line, C-quoted only when it holds special characters.
    foreach ($line in [IO.File]::ReadAllLines([IO.Path]::GetFullPath($PatchFile))) {
        if ($line -match '^(?:rename|copy) from (.+)$') { $found.Add((ConvertFrom-Pd2qGitQuoted $Matches[1])) }
    }
    return @($found | Select-Object -Unique)
}

function ConvertFrom-Pd2qGitQuoted([string] $Name) {
    # git quotes a path as "..." with C escapes (octal bytes for UTF-8) when it
    # holds special characters; anything else is the plain name.
    if ($Name.Length -lt 2 -or -not ($Name.StartsWith('"') -and $Name.EndsWith('"'))) { return $Name }
    $bytes = [Collections.Generic.List[byte]]::new()
    $s = $Name.Substring(1, $Name.Length - 2)
    for ($i = 0; $i -lt $s.Length; $i++) {
        $c = $s[$i]
        if ($c -ne '\' -or $i + 1 -ge $s.Length) { foreach ($b in [Text.Encoding]::UTF8.GetBytes([string]$c)) { $bytes.Add($b) }; continue }
        $n = $s[++$i]
        if ($n -match '[0-7]' -and $i + 2 -lt $s.Length) { $bytes.Add([Convert]::ToByte($s.Substring($i, 3), 8)); $i += 2; continue }
        $map = @{ 'n' = 10; 't' = 9; 'r' = 13; 'a' = 7; 'b' = 8; 'f' = 12; 'v' = 11 }
        if ($map.ContainsKey([string]$n)) { $bytes.Add([byte]$map[[string]$n]) } else { foreach ($b in [Text.Encoding]::UTF8.GetBytes([string]$n)) { $bytes.Add($b) } }
    }
    return [Text.Encoding]::UTF8.GetString($bytes.ToArray())
}

function Get-Pd2qDevHead {
    return Invoke-Pd2qGit (Get-Pd2qPaths).Dev @('rev-parse', 'HEAD')
}

function Test-Pd2qPatchApplies([byte[]] $Patch, [string] $Dir) {
    $file = Join-Path ([IO.Path]::GetTempPath()) ('pd2q-check-' + [guid]::NewGuid().ToString('N') + '.patch')
    [IO.File]::WriteAllBytes($file, $Patch)
    try {
        $out = & $script:Git -C $Dir apply --check $file 2>&1
        return @{ Ok = ($LASTEXITCODE -eq 0); Detail = ($out | Out-String).Trim() }
    } finally { Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue }
}

# ---------------------------------------------------------------- leases

function Read-Pd2qLeases {
    $p = Get-Pd2qPaths
    $l = Read-Pd2qJson $p.Leases @{ schema = 'pd2q.leases.v1'; leases = @{} }
    if (-not $l.ContainsKey('leases') -or $null -eq $l.leases) { $l.leases = @{} }
    return $l
}

function Test-Pd2qLeaseLive($Lease) {
    # Live only while not expired, no reboot since it was taken, and the owner
    # process still runs with the same start time. A queued lease belongs to its
    # job until the job ends, whatever happens to the session.
    if ($Lease.state -eq 'queued') { return @{ Live = $true; Reason = 'queued' } }
    $now = Get-Pd2qNow
    if ($now -gt (ConvertFrom-Pd2qTime $Lease.expiresUtc)) { return @{ Live = $false; Reason = "expired at $($Lease.expiresUtc) (no renew for $($script:LeaseMinutes) minutes)" } }
    if ($Lease.holder.bootUtc -ne (Get-Pd2qBootUtc)) { return @{ Live = $false; Reason = "the PC rebooted (lease boot $($Lease.holder.bootUtc), now $(Get-Pd2qBootUtc))" } }
    $start = Get-Pd2qProcessStartUtc ([int]$Lease.holder.pid)
    if (-not $start) { return @{ Live = $false; Reason = "owner process $($Lease.holder.pid) is gone" } }
    if ($start -ne $Lease.holder.pidStartUtc) { return @{ Live = $false; Reason = "process id $($Lease.holder.pid) now names another process (started $start)" } }
    $agents = Get-Pd2qAgents
    if ($agents[$Lease.holder.agent].state -eq 'off') { return @{ Live = $false; Reason = "agent $($Lease.holder.agent) is switched off" } }
    return @{ Live = $true; Reason = 'held' }
}

function Update-Pd2qLeases {
    # Call under the lock. Frees every dead lease at once, after saving a started
    # task's diff, and cleans that session's scratch. Returns what it freed.
    $p = Get-Pd2qPaths
    $l = Read-Pd2qLeases
    $freed = @()
    foreach ($issue in @($l.leases.Keys)) {
        $lease = $l.leases[$issue]
        $check = Test-Pd2qLeaseLive $lease
        if ($check.Live) { continue }
        $saved = $false
        try {
            $task = Save-Pd2qCheckpoint $issue $lease.holder.session ('lease freed: ' + $check.Reason) $lease.holder.agent
            if (-not ($task.ContainsKey('patch') -and $task.patch)) {
                # No edits were made: nothing is in progress, so the issue is free.
                $task.state = 'released'
                Save-Pd2qTask $task
            }
            $saved = $true
        } catch {
            # The scratch is then the only copy of the edits: keep it out of the
            # session's way and say where.
            $why = $_.Exception.Message
            $kept = Move-Pd2qScratchAside $lease.holder.session
            Add-Pd2qAlarm 'checkpoint-failed' "lease $($lease.lease) on $issue freed ($($check.Reason)), but its edits could not be saved: $why. Scratch kept at $kept."
        }
        if ($saved) {
            try { Clear-Pd2qScratch $lease.holder.session } catch { Write-Pd2qLog "scratch of $($lease.holder.session) not cleared: $($_.Exception.Message)" }
        }
        $l.leases.Remove($issue)
        $freed += @{ issue = $issue; lease = $lease.lease; session = $lease.holder.session; reason = $check.Reason }
        Write-Pd2qLog "lease $($lease.lease) on $issue freed: $($check.Reason)"
    }
    if ($freed) { Write-Pd2qJson $p.Leases $l }
    return , $freed
}

# ---------------------------------------------------------------- board

function Get-Pd2qBoard {
    return @(Read-Pd2qJsonLines (Get-Pd2qPaths).Board)
}

function Find-Pd2qLanded([string] $Issue) {
    return @(Get-Pd2qBoard | Where-Object { $_.issue -ceq $Issue }) | Select-Object -Last 1
}

function Update-Pd2qBoardFromGit {
    # Source of truth is git: every queue commit carries PD2-Issue and
    # PD2-Queue-Job trailers. Rebuilt at service startup.
    $p = Get-Pd2qPaths
    if (-not (Test-Path -LiteralPath (Join-Path $p.Dev '.git'))) { return 0 }
    $log = Invoke-Pd2qGit $p.Dev @('log', 'origin/dev', '--grep=PD2-Issue:', '--format=%H%x1f%cI%x1f%s%x1f%B%x1e') -AllowFailure
    $entries = @()
    foreach ($record in @($log -split [char]0x1e | Where-Object { $_.Trim() })) {
        $parts = $record.Trim() -split [char]0x1f
        if ($parts.Count -lt 4) { continue }
        $body = $parts[3]
        $issue = if ($body -match '(?m)^PD2-Issue:\s*(\S+)') { $Matches[1] } else { continue }
        $job = if ($body -match '(?m)^PD2-Queue-Job:\s*(\S+)') { $Matches[1] } else { '' }
        $session = if ($body -match '(?m)^PD2-Session:\s*(\S+)') { $Matches[1] } else { '' }
        $agent = if ($body -match '(?m)^PD2-Agent:\s*(\S+)') { $Matches[1] } else { '' }
        $entries += @{ schema = 'pd2q.board.v1'; issue = $issue; commit = $parts[0]; job = $job; title = $parts[2]
            session = $session; agent = $agent; landedUtc = Format-Pd2qTime (ConvertFrom-Pd2qTime $parts[1]) }
    }
    [array]::Reverse($entries)
    $temp = $p.Board + '.tmp'
    [IO.File]::WriteAllText($temp, (($entries | ForEach-Object { ($_ | ConvertTo-Json -Compress) }) -join "`n") + $(if ($entries) { "`n" } else { '' }), [Text.UTF8Encoding]::new($false))
    Move-Item -LiteralPath $temp -Destination $p.Board -Force
    return $entries.Count
}

# ---------------------------------------------------------------- jobs

function Get-Pd2qJournal([string] $JobId) {
    return @(Read-Pd2qJsonLines (Join-Path (Get-Pd2qPaths).Journal "$JobId.jsonl"))
}

function Add-Pd2qJournal([string] $JobId, [hashtable] $Event) {
    $Event.atUtc = Format-Pd2qTime (Get-Pd2qNow)
    Add-Pd2qJsonLine (Join-Path (Get-Pd2qPaths).Journal "$JobId.jsonl") $Event
}

function Get-Pd2qJobs {
    # Every job with its request (first line) and its last state.
    $p = Get-Pd2qPaths
    $jobs = @()
    foreach ($file in @(Get-ChildItem -LiteralPath $p.Journal -Filter '*.jsonl' -File -ErrorAction SilentlyContinue)) {
        $lines = @(Read-Pd2qJsonLines $file.FullName)
        if (-not $lines) { continue }
        $request = $lines[0]
        $last = $lines[-1]
        $front = @($lines | Where-Object { $_.event -eq 'queued' -and $_.ContainsKey('front') -and $_.front }).Count -gt 0
        $attempt = (@($lines | Where-Object { $_.ContainsKey('attempt') } | ForEach-Object { [int]$_.attempt }) + @(1) | Measure-Object -Maximum).Maximum
        $jobs += [pscustomobject]@{ Id = $request.job; Request = $request; State = $last.event; Last = $last; Lines = $lines
            Front = $front; Attempt = $attempt; Sequence = [long]($request.job -replace '^q(\d+)-.*$', '$1') }
    }
    return $jobs
}

function Get-Pd2qNextJob {
    # One at a time in submit order; a job sent back because dev moved, or
    # interrupted, goes to the front.
    $queued = @(Get-Pd2qJobs | Where-Object { $_.State -in @('submitted', 'queued') })
    if (-not $queued) { return $null }
    return @($queued | Sort-Object @{ Expression = { if ($_.State -eq 'queued' -and $_.Last.ContainsKey('front') -and $_.Last.front) { 0 } else { 1 } } }, Sequence)[0]
}

function Test-Pd2qCommitTitle([string] $Title) {
    # Queue commits follow the repository standard (.githooks/commit-msg.py):
    # "<Area> - <WorkbenchItemID>: <summary>", at most 96 characters.
    if ($Title.Length -gt 96) { return "Title is $($Title.Length) characters; the limit is 96." }
    if ($Title -notmatch '^[A-Za-z][A-Za-z0-9 _\-]*?\s+-\s+[A-Za-z][A-Za-z0-9_-]*(?:-[A-Za-z0-9_-]+)*\s*:\s+\S') {
        return "Title must be '<Area> - <WorkbenchItemID>: <summary>' (context/designs/commit-message-standard.md)."
    }
    return $null
}

function Get-Pd2qCanonical {
    # The main checkout: the hub, the live Workbench roadmap and the commit hooks.
    if ($env:PD2Q_CANONICAL) { return $env:PD2Q_CANONICAL }
    return 'C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike'
}

function Get-Pd2qPythonExe {
    # Python for the repository's commit checks, by full path. The Store's
    # python.exe alias under WindowsApps opens the Store, so it is never used.
    foreach ($p in @('C:\Python312\python.exe', 'C:\Python313\python.exe', 'C:\Python311\python.exe')) {
        if (Test-Path -LiteralPath $p -PathType Leaf) { return $p }
    }
    $found = @(Get-Command -Name 'python.exe', 'py.exe' -CommandType Application -ErrorAction SilentlyContinue |
        Where-Object { $_.Source -like '*.exe' -and $_.Source -notlike '*\WindowsApps\*' })
    if ($found) { return $found[0].Source }
    return $null
}

function Get-Pd2qTitleItem([string] $Title) {
    # The Workbench item in "<Area> - <ItemID>: <summary>".
    if ($Title -match '^[A-Za-z][A-Za-z0-9 _\-]*?\s+-\s+(?<item>[A-Za-z][A-Za-z0-9_-]*(?:-[A-Za-z0-9_-]+)*)\s*:') { return $Matches['item'].ToUpperInvariant() }
    return $null
}

function Get-Pd2qCommitMessage([hashtable] $Request, [string] $JobId, [string] $BuiltLine) {
    # Title, the submitted body, the queue's line, then the trailers. Refs names
    # the title's item and, when it differs, the issue (a bug id such as B-1277),
    # as .githooks/commit-msg.py asks.
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add([string]$Request.title); $lines.Add('')
    if ($Request.ContainsKey('body') -and $Request.body) { $lines.Add([string]$Request.body); $lines.Add('') }
    $lines.Add($BuiltLine); $lines.Add('')
    $item = Get-Pd2qTitleItem $Request.title
    $refs = if (-not $item -or $item -ceq $Request.issue) { $Request.issue } else { "$item, $($Request.issue)" }
    foreach ($t in @("Refs: $refs", "PD2-Issue: $($Request.issue)", "PD2-Queue-Job: $JobId", "PD2-Session: $($Request.session)", "PD2-Agent: $($Request.agent)")) { $lines.Add($t) }
    if ($Request.ContainsKey('coAuthor') -and $Request.coAuthor) { $lines.Add("Co-Authored-By: $($Request.coAuthor)") }
    return ($lines -join "`n") + "`n"
}

function Test-Pd2qCommitMessage([string] $Message) {
    # The repository's own commit-msg check against the live Workbench roadmap,
    # which only the main checkout holds (roadmap.json is not in git). Returns
    # $null when it passes, or when it cannot run here (no main checkout or
    # Python, as on CI); otherwise its complaint.
    $canonical = Get-Pd2qCanonical
    $hook = Join-Path $canonical '.githooks\commit-msg.py'
    $python = Get-Pd2qPythonExe
    if (-not (Test-Path -LiteralPath $hook -PathType Leaf) -or -not (Test-Path -LiteralPath (Join-Path $canonical 'Tools\Workbench\data\roadmap.json')) -or -not $python) { return $null }
    $file = Join-Path ([IO.Path]::GetTempPath()) ('pd2q-msg-' + [guid]::NewGuid().ToString('N') + '.txt')
    [IO.File]::WriteAllText($file, $Message, [Text.UTF8Encoding]::new($false))
    Push-Location -LiteralPath $canonical
    try {
        $out = & $python -B $hook $file 2>&1 | ForEach-Object { "$_" } | Out-String
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
        Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
    }
    if ($code -ne 0) { return $out.Trim() }
    return $null
}

function Test-Pd2qWorkbenchMeta($Meta, [string] $Canonical) {
    # AGENTS.md's gate before any Workbench write: canonical, not isolated, and
    # its project and data roots in the main checkout. "canonical" alone means
    # only that the server runs in its own main checkout, which may be another
    # clone's.
    if (-not $Meta -or $Meta.canonical -ne $true -or $Meta.isolated -ne $false) { return $false }
    $norm = { param($p) if (-not $p) { return '' }; ([IO.Path]::GetFullPath([string]$p)).TrimEnd('\', '/').ToLowerInvariant() }
    $root = & $norm $Canonical
    $project = & $norm $Meta.projectRoot
    $data = & $norm $Meta.dataDir
    return [bool]($root -and $project -eq $root -and $data -and $data.StartsWith($root + '\'))
}

function Assert-Pd2qPushTarget([string] $Ref) {
    # The queue pushes only to dev; main is never pushed, merged or rebased.
    if ($Ref -cne 'refs/heads/dev') { throw "pd2q pushes only refs/heads/dev; refused '$Ref'." }
}

function Send-Pd2qWake {
    # Event-driven: the worker waits on this event instead of polling.
    try {
        $evt = [Threading.EventWaitHandle]::new($false, 'AutoReset', ((Get-Pd2qMutexName) + '-Wake'))
        [void]$evt.Set(); $evt.Dispose()
    } catch { }
}

Export-ModuleMember -Function *-Pd2q*
