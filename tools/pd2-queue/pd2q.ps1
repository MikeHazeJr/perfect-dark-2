#Requires -Version 7.0
<#
.SYNOPSIS
    pd2q: the one command sessions use to work through the PD2 build queue.

.DESCRIPTION
    check <issue>                       landed, claimed or queued? Run before any fix.
    claim <issue> -Session s [-OwnerPid n] [-Reopen "why"]
    renew <issue> -Lease n               heartbeat (every 3 minutes); saves the diff
    edit <path> -Session s [-Issue i] [-New] [-Delete]
                                         copy a dev file to scratch; prints the path
    checkpoint <issue> -Lease n -Note "..."
    validate <issue> -Lease n -Result keep|redo -Note "..."
    submit <issue> -Lease n -Title "<Area> - <ItemID>: <summary>" [-Body file] [-CoAuthor "Name <email>"]
                                         [-Tests scope] [-Smoke] [-Patch f]
    release <issue> -Lease n
    status | board [-Since date] | wait <job> [-TimeoutSeconds n]
    break <issue> -Reason "..."          free a lease or drop a queued job (logged)
    cancel <job> -Reason "..."           stop a job; its folder goes, its issue frees
    agent <claude|codex> <on|off> [-Until time] [-Reason "..."]
    resume -Reason "..."                 after a dirty-dev alarm, once someone looked

    The CLI never builds and never pushes; Queue-Service.ps1 does.
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0, Mandatory)]
    [ValidateSet('check', 'claim', 'renew', 'edit', 'checkpoint', 'validate', 'submit', 'release', 'status', 'board', 'wait', 'break', 'cancel', 'agent', 'resume')]
    [string] $Command,
    [Parameter(Position = 1)] [string] $Target = '',
    [Parameter(Position = 2)] [string] $Value = '',
    [string] $Session = '',
    [long] $Lease = 0,
    [int] $OwnerPid = 0,
    [string] $Reopen = '',
    [string] $Issue = '',
    [switch] $New,
    [switch] $Delete,
    [string] $Note = '',
    [ValidateSet('', 'keep', 'redo')] [string] $Result = '',
    [string] $Title = '',
    [string] $Body = '',
    [string] $CoAuthor = '',
    [string] $Tests = '',
    [switch] $Smoke,
    [string] $Patch = '',
    [string] $Since = '',
    [int] $TimeoutSeconds = 3600,
    [string] $Reason = '',
    [string] $Until = '',
    [ValidateSet('', 'claude', 'codex')] [string] $Agent = '',
    [switch] $Json
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'Pd2Queue.psm1') -Force
$paths = Initialize-Pd2qLayout

function Out-Pd2q($Object, [string] $Text) {
    if ($Json) { $Object | ConvertTo-Json -Depth 10 } else { $Text }
}

function Get-HeldLease([string] $IssueId, [long] $Number) {
    $l = Read-Pd2qLeases
    $current = $l.leases[$IssueId]
    if (-not $current) { throw "No live lease on $IssueId (it expired, was freed, or was never taken). Run: pd2q check $IssueId" }
    if ([long]$current.lease -ne $Number) {
        throw "Lease $Number on $IssueId is not current; the current lease is $($current.lease) (held by $($current.holder.session)). Re-claim and refresh scratch from dev."
    }
    return @{ All = $l; Lease = $current }
}

function Restore-TaskPatch($Task, [string] $SessionId) {
    # Puts a saved task's edits back into a new holder's scratch.
    $dir = Get-Pd2qScratchDir $SessionId
    Clear-Pd2qScratch $SessionId
    [IO.Directory]::CreateDirectory($dir) | Out-Null
    [IO.File]::WriteAllText((Join-Path $dir 'base.txt'), [string]$Task.base)
    foreach ($rel in @($Task.files)) {
        $content = Invoke-Pd2qGitBytes $paths.Dev @('show', "$($Task.base):$rel") -AllowFailure
        if ($null -ne $content) {
            $t = Join-Path $dir $rel
            [IO.Directory]::CreateDirectory((Split-Path -Parent $t)) | Out-Null
            [IO.File]::WriteAllBytes($t, $content)
        }
    }
    $patchFile = Join-Path $paths.Tasks "$($Task.issue).patch"
    $out = & (Get-Pd2qGitExe) -C $dir apply --whitespace=nowarn $patchFile 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Saved patch of $($Task.issue) did not restore into scratch: $(($out | Out-String).Trim())" }
    $deleted = @(Get-Content -LiteralPath $patchFile | Where-Object { $_ -match '^deleted file mode' }).Count
    if ($deleted) {
        # git apply removed them; remember them so the next patch deletes them too.
        $names = @(Select-String -LiteralPath $patchFile -Pattern '^diff --git a/(\S+) b/' | ForEach-Object { $_.Matches[0].Groups[1].Value } |
            Where-Object { -not (Test-Path -LiteralPath (Join-Path $dir $_)) })
        [IO.File]::WriteAllLines((Join-Path $dir '.pd2q-deleted'), $names)
    }
}

switch ($Command) {
    'check' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $landed = Find-Pd2qLanded $issueId
            $lease = (Read-Pd2qLeases).leases[$issueId]
            $task = Get-Pd2qTask $issueId
            $queued = @(Get-Pd2qJobs | Where-Object { $_.Request.issue -ceq $issueId -and $_.State -notin @('landed', 'rejected', 'failed', 'cancelled') })
            $state = if ($queued) { 'needs testing' } elseif ($lease) { 'started' } elseif ($task -and $task.state -eq 'started') { 'started (unowned)' } elseif ($landed) { 'finished' } else { 'free' }
            $lines = @("$issueId : $state")
            if ($landed) { $lines += "  landed: $($landed.commit.Substring(0, [Math]::Min(12, $landed.commit.Length))) $($landed.landedUtc) job $($landed.job) by $($landed.session)" }
            if ($lease) {
                $age = [int]((Get-Pd2qNow) - (ConvertFrom-Pd2qTime $lease.acquiredUtc)).TotalMinutes
                $left = [int]((ConvertFrom-Pd2qTime $lease.expiresUtc) - (Get-Pd2qNow)).TotalSeconds
                $lines += "  lease $($lease.lease) $($lease.state) by $($lease.holder.session) ($($lease.holder.agent)), $age min old, expires in $left s$(if ($lease.job) { ', job ' + $lease.job })"
            }
            foreach ($j in $queued) { $lines += "  job $($j.Id): $($j.State)" }
            if ($task -and $task.state -eq 'started' -and -not $lease) {
                $lines += "  saved work: $(@($task.files).Count) file(s), checkpoint $($task.checkpointUtc) by $($task.agent)/$($task.session)"
                # The last notes: the holder's own handover note usually sits
                # just before the automatic "lease freed" one.
                foreach ($n in @(@($task.notes) | Select-Object -Last 3)) { $lines += "  note $($n.atUtc) $($n.agent)/$($n.session): $($n.note)" }
            }
            Out-Pd2q @{ issue = $issueId; state = $state; landed = $landed; lease = $lease; jobs = @($queued | ForEach-Object Id); task = $task } ($lines -join "`n")
        }
    }
    'claim' {
        $issueId = $Target; Assert-Pd2qIssue $issueId; Assert-Pd2qSession $Session
        $owner = Resolve-Pd2qOwner $OwnerPid
        # A Codex session can run under node.exe, so the host name alone cannot
        # always tell the agent; -Agent says it outright.
        $agent = if ($Agent) { $Agent } else { Get-Pd2qAgentOf $owner.process }
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $agents = Get-Pd2qAgents
            if ($agents[$agent].state -eq 'off') { throw "Agent $agent is switched off ($($agents[$agent].reason)); claims are refused." }
            $landed = Find-Pd2qLanded $issueId
            if ($landed -and -not $Reopen) { throw "$issueId already landed ($($landed.commit.Substring(0, 12)), job $($landed.job)). Claim with -Reopen `"why`" to fix it again." }
            $l = Read-Pd2qLeases
            if ($l.leases[$issueId]) {
                $h = $l.leases[$issueId]
                throw "$issueId is $($h.state) by $($h.holder.session) (lease $($h.lease)$(if ($h.job) { ', job ' + $h.job }))."
            }
            # A session has one scratch folder, so one held lease at a time: a
            # second would share it, and either submit would carry both issues'
            # edits. A submitted (queued) lease has already cleared its scratch.
            $mine = @($l.leases.Values | Where-Object { $_.holder.session -ceq $Session -and $_.state -eq 'held' })
            if ($mine) { throw "$Session already holds lease $($mine[0].lease) on $($mine[0].issue), and a session has one scratch folder. Submit or release it before claiming $issueId." }
            $number = New-Pd2qNumber lease
            $now = Get-Pd2qNow
            $l.leases[$issueId] = @{
                schema = 'pd2q.lease.v1'; issue = $issueId; lease = $number
                holder = @{ session = $Session; agent = $agent; pid = $owner.pid; pidStartUtc = $owner.pidStartUtc; process = $owner.process; host = $env:COMPUTERNAME; bootUtc = Get-Pd2qBootUtc }
                acquiredUtc = Format-Pd2qTime $now; renewedUtc = Format-Pd2qTime $now
                expiresUtc = Format-Pd2qTime $now.AddMinutes(10); state = 'held'; job = $null
            }
            Write-Pd2qJson $paths.Leases $l
            $task = Get-Pd2qTask $issueId
            $restored = $false
            if ($task -and $task.state -eq 'started' -and $task.ContainsKey('patch') -and $task.patch) {
                Restore-TaskPatch $task $Session
                # Another session or agent must read and validate the work first;
                # the same session picking its own work back up need not.
                $task.validated = ($task.session -eq $Session -and $task.agent -eq $agent)
                $restored = $true
            } elseif (-not $task -or $task.state -ne 'started') {
                $task = @{ schema = 'pd2q.task.v1'; issue = $issueId; state = 'started'; notes = @(); validated = $true }
            }
            $task.state = 'started'; $task.agent = $agent; $task.session = $Session
            if ($Reopen) { $task.notes = @($task.notes) + @(@{ atUtc = Format-Pd2qTime $now; agent = $agent; session = $Session; note = 'reopened: ' + $Reopen }) }
            Save-Pd2qTask $task
            Write-Pd2qLog "claim $issueId lease $number by $Session ($agent pid $($owner.pid))$(if ($Reopen) { ' reopen: ' + $Reopen })$(if ($restored) { ' restored saved work' })"
            $text = "lease $number on $issueId (renew every 3 minutes; it expires 10 minutes after the last renew)"
            if ($restored -and -not $task.validated) { $text += "`nRestored the saved work of $(@($task.files).Count) file(s) into scratch. Read the notes (pd2q check $issueId) and run pd2q validate $issueId -Lease $number -Result keep|redo before submitting." }
            elseif ($restored) { $text += "`nRestored your saved work of $(@($task.files).Count) file(s) into scratch." }
            Out-Pd2q @{ issue = $issueId; lease = $number; restored = $restored; owner = $owner } $text
        }
    }
    'renew' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $h = Get-HeldLease $issueId $Lease
            if ($h.Lease.state -ne 'held') { throw "Lease $Lease on $issueId is $($h.Lease.state) (job $($h.Lease.job)); nothing to renew." }
            $now = Get-Pd2qNow
            $h.Lease.renewedUtc = Format-Pd2qTime $now
            $h.Lease.expiresUtc = Format-Pd2qTime $now.AddMinutes(10)
            Write-Pd2qJson $paths.Leases $h.All
            [void](Save-Pd2qCheckpoint $issueId $h.Lease.holder.session '' $h.Lease.holder.agent)
            Out-Pd2q @{ issue = $issueId; lease = $Lease; expiresUtc = $h.Lease.expiresUtc } "lease $Lease on $issueId renewed until $($h.Lease.expiresUtc)"
        }
    }
    'edit' {
        Assert-Pd2qSession $Session
        $rel = $Target.Replace('\', '/').TrimStart('/')
        if (-not $rel -or $rel -match '(^|/)\.\.(/|$)' -or $rel -match ':' -or $rel -cmatch '^(base\.txt|\.pd2q-deleted)$') { throw "Not a repository-relative path: '$Target'" }
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $mine = @((Read-Pd2qLeases).leases.Values | Where-Object { $_.holder.session -eq $Session -and $_.state -eq 'held' })
            if ($Issue) { $mine = @($mine | Where-Object { $_.issue -ceq $Issue }) }
            if ($mine.Count -ne 1) { throw "Session $Session must hold exactly one live lease$(if ($Issue) { ' on ' + $Issue }) to edit (it holds $($mine.Count)); pass -Issue or claim first." }
            $dir = Get-Pd2qScratchDir $Session
            [IO.Directory]::CreateDirectory($dir) | Out-Null
            $baseFile = Join-Path $dir 'base.txt'
            if (-not (Test-Path -LiteralPath $baseFile)) { [IO.File]::WriteAllText($baseFile, (Get-Pd2qDevHead)) }
            $base = ([IO.File]::ReadAllText($baseFile)).Trim()
            $dest = Join-Path $dir $rel
            if ($Delete) {
                Add-Content -LiteralPath (Join-Path $dir '.pd2q-deleted') -Value $rel
                if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Force }
                $text = "marked for deletion: $rel"
            } elseif (Test-Path -LiteralPath $dest) {
                $text = $dest
            } else {
                $content = Invoke-Pd2qGitBytes $paths.Dev @('show', "$($base):$rel") -AllowFailure
                if ($null -eq $content -and -not $New) { throw "$rel is not in dev at $($base.Substring(0, 12)); pass -New to create it." }
                [IO.Directory]::CreateDirectory((Split-Path -Parent $dest)) | Out-Null
                [IO.File]::WriteAllBytes($dest, $(if ($null -eq $content) { [byte[]]@() } else { $content }))
                $text = $dest
            }
            $task = Get-Pd2qTask $mine[0].issue
            if ($task) { $task.files = @(@($task.files) + @($rel) | Where-Object { $_ } | Select-Object -Unique); Save-Pd2qTask $task }
            Out-Pd2q @{ path = $dest; base = $base; issue = $mine[0].issue } $text
        }
    }
    'checkpoint' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $h = Get-HeldLease $issueId $Lease
            $task = Save-Pd2qCheckpoint $issueId $h.Lease.holder.session $Note $h.Lease.holder.agent
            Out-Pd2q $task "checkpoint of $issueId saved ($(@($task.files).Count) file(s)) at $($task.checkpointUtc)"
        }
    }
    'validate' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        if (-not $Result) { throw 'validate needs -Result keep or -Result redo, and -Note.' }
        if (-not $Note) { throw 'validate needs -Note: what you read and why keep or redo.' }
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $h = Get-HeldLease $issueId $Lease
            $task = Get-Pd2qTask $issueId
            if (-not $task -or $task.validated) { throw "$issueId has nothing to validate." }
            $applies = $null
            $patchFile = Join-Path $paths.Tasks "$issueId.patch"
            if ($Result -eq 'keep' -and (Test-Path -LiteralPath $patchFile)) {
                $applies = Test-Pd2qPatchApplies ([IO.File]::ReadAllBytes($patchFile)) $paths.Dev
                if (-not $applies.Ok) { throw "The saved patch no longer applies to dev; validate with -Result redo. $($applies.Detail)" }
            }
            if ($Result -eq 'redo') { Clear-Pd2qScratch $h.Lease.holder.session; $task.files = @(); $task.Remove('patch'); Remove-Item -LiteralPath $patchFile -Force -ErrorAction SilentlyContinue }
            $task.validated = $true
            $task.notes = @($task.notes) + @(@{ atUtc = Format-Pd2qTime (Get-Pd2qNow); agent = $h.Lease.holder.agent; session = $h.Lease.holder.session; note = "validate $($Result): $Note" })
            Save-Pd2qTask $task
            Out-Pd2q $task "$issueId validated ($Result)"
        }
    }
    'submit' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        $titleProblem = Test-Pd2qCommitTitle $Title
        if ($titleProblem) { throw $titleProblem }
        # The commit body: what changed and why, as in every commit here. The
        # queue writes the trailers itself, so the body may not carry any.
        $bodyText = ''
        if ($Body) {
            $bodyText = ([IO.File]::ReadAllText([IO.Path]::GetFullPath($Body))).Replace("`r`n", "`n").Trim()
            if ($bodyText.Length -gt 8000) { throw "The body is $($bodyText.Length) characters; the limit is 8000." }
            if ($bodyText -match '(?im)^\s*(Refs|PD2-[A-Za-z-]+|Co-Authored-By)\s*:') { throw 'The body may not carry trailer lines (Refs, PD2-*, Co-Authored-By); the queue writes them.' }
        }
        if ($CoAuthor -and $CoAuthor -notmatch '^[^<>\r\n]+ <[^<>\s@]+@[^<>\s]+>$') { throw "-CoAuthor must look like 'Name <email>'." }
        # The repository's commit-msg check, now rather than after a build: the
        # item must exist in the Workbench and match the title's area.
        $preview = Get-Pd2qCommitMessage @{ title = $Title; body = $bodyText; coAuthor = $CoAuthor; issue = $issueId; session = $Session; agent = 'claude' } 'q0-preview' 'Built and tested by the PD2 build queue.'
        $msgProblem = Test-Pd2qCommitMessage $preview
        if ($msgProblem) { throw "The commit message would fail the repository's commit-msg check:`n$msgProblem" }
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $h = Get-HeldLease $issueId $Lease
            if ($h.Lease.state -ne 'held') { throw "Lease $Lease on $issueId is $($h.Lease.state)." }
            $agents = Get-Pd2qAgents
            if ($agents[$h.Lease.holder.agent].state -eq 'off') { throw "Agent $($h.Lease.holder.agent) is switched off." }
            $task = Get-Pd2qTask $issueId
            if ($task -and $task.ContainsKey('validated') -and -not $task.validated) { throw "$issueId holds restored work that is not validated; run pd2q validate first." }
            if ($Patch) {
                $patchBytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($Patch))
                $base = Get-Pd2qDevHead
                $files = @(Select-String -LiteralPath $Patch -Pattern '^diff --git a/(\S+) b/' | ForEach-Object { $_.Matches[0].Groups[1].Value })
            } else {
                $made = New-Pd2qPatchFromScratch $h.Lease.holder.session
                $patchBytes = $made.Patch; $base = $made.Base; $files = $made.Files
            }
            $check = Test-Pd2qPatchApplies $patchBytes $paths.Dev
            if (-not $check.Ok) { throw "The patch does not apply to current dev ($((Get-Pd2qDevHead).Substring(0, 12))); refresh scratch from dev and resubmit. $($check.Detail)" }
            $number = New-Pd2qNumber job
            $jobId = 'q{0}-{1}' -f $number, (Get-Pd2qNow).ToString('yyyyMMdd')
            [IO.File]::WriteAllBytes((Join-Path $paths.Journal "$jobId.patch"), $patchBytes)
            $sha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($patchBytes)).ToLowerInvariant()
            Add-Pd2qJournal $jobId @{ schema = 'pd2q.job.v1'; event = 'submitted'; job = $jobId; issue = $issueId; lease = $Lease
                session = $h.Lease.holder.session; agent = $h.Lease.holder.agent; title = $Title; body = $bodyText; coAuthor = $CoAuthor
                baseCommit = $base; patchSha256 = $sha; files = @($files); tests = @{ scope = $Tests; smoke = [bool]$Smoke }; attempt = 1 }
            $h.Lease.state = 'queued'; $h.Lease.job = $jobId
            Write-Pd2qJson $paths.Leases $h.All
            if (-not $task) { $task = @{ schema = 'pd2q.task.v1'; issue = $issueId; notes = @() } }
            [IO.File]::WriteAllBytes((Join-Path $paths.Tasks "$issueId.patch"), $patchBytes)
            $task.state = 'needs testing'; $task.job = $jobId; $task.patch = "$issueId.patch"; $task.files = @($files); $task.base = $base
            Save-Pd2qTask $task
            Clear-Pd2qScratch $h.Lease.holder.session
            Write-Pd2qLog "submit $issueId lease $Lease as $jobId by $($h.Lease.holder.session): $Title"
            Send-Pd2qWake
            Out-Pd2q @{ job = $jobId; issue = $issueId; files = @($files) } "$jobId queued ($(@($files).Count) file(s)); pd2q wait $jobId"
        }
    }
    'release' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $h = Get-HeldLease $issueId $Lease
            if ($h.Lease.state -ne 'held') { throw "Lease $Lease on $issueId is $($h.Lease.state) (job $($h.Lease.job)); cancel the job instead." }
            $h.All.leases.Remove($issueId)
            Write-Pd2qJson $paths.Leases $h.All
            Clear-Pd2qScratch $h.Lease.holder.session
            $task = Get-Pd2qTask $issueId
            if ($task) { $task.state = 'released'; Save-Pd2qTask $task }
            Write-Pd2qLog "release $issueId lease $Lease by $($h.Lease.holder.session)"
            Out-Pd2q @{ issue = $issueId; released = $Lease } "lease $Lease on $issueId released; scratch cleared"
        }
    }
    'status' {
        Invoke-Pd2qLocked {
            [void](Update-Pd2qLeases)
            $now = Get-Pd2qNow
            $service = Read-Pd2qJson $paths.Service $null
            $jobs = @(Get-Pd2qJobs)
            $running = @($jobs | Where-Object { $_.State -in @('preparing', 'building', 'testing', 'pushing') })
            $queued = @($jobs | Where-Object { $_.State -in @('submitted', 'queued') } | Sort-Object Sequence)
            $leases = @((Read-Pd2qLeases).leases.Values)
            $alarms = Read-Pd2qJson $paths.Alarms @{ alarms = @() }
            $builds = @(Read-Pd2qJsonLines $paths.Builds) | Select-Object -Last 5
            $jobsBytes = Get-Pd2qTreeBytes $paths.Jobs
            $lines = @()
            $alive = $service -and $service.pid -and (Get-Pd2qProcessStartUtc ([int]$service.pid)) -eq $service.pidStartUtc
            $lines += 'service: ' + $(if ($alive) { "running (pid $($service.pid), $($service.state)$(if ($service.paused) { ', PAUSED: ' + $service.paused }))" } else { 'not running' })
            foreach ($j in $running) {
                $since = [int]($now - (ConvertFrom-Pd2qTime $j.Last.atUtc)).TotalSeconds
                $lines += "running: $($j.Id) $($j.Request.issue) $($j.State) for $since s (attempt $($j.Attempt)): $($j.Request.title)"
            }
            foreach ($j in $queued) { $lines += "queued: $($j.Id) $($j.Request.issue)$(if ($j.Last.ContainsKey('front') -and $j.Last.front) { ' (front)' }): $($j.Request.title)" }
            foreach ($le in $leases) {
                $age = [int]($now - (ConvertFrom-Pd2qTime $le.acquiredUtc)).TotalMinutes
                $beat = [int]($now - (ConvertFrom-Pd2qTime $le.renewedUtc)).TotalSeconds
                $left = [int]((ConvertFrom-Pd2qTime $le.expiresUtc) - $now).TotalSeconds
                $lines += "lease $($le.lease) $($le.issue) $($le.state) by $($le.holder.session) ($($le.holder.agent)): age $age min, last renew $beat s ago, $(if ($le.state -eq 'queued') { 'held by job ' + $le.job } else { "$left s left" })"
            }
            foreach ($b in $builds) { if ($b.slow) { $lines += "SLOW: $($b.job) $($b.total) s ($($b.slowReason))" } }
            foreach ($a in @($alarms.alarms)) { $lines += "ALARM $($a.kind) $($a.atUtc): $($a.detail)" }
            $lines += 'jobs folder: {0:N1} MB{1}; scratch: {2:N1} MB' -f ($jobsBytes / 1MB), $(if ($jobsBytes -gt 0 -and -not $running) { ' (ALARM: no job is running)' } else { '' }), ((Get-Pd2qTreeBytes $paths.Scratch) / 1MB)
            $agents = Get-Pd2qAgents
            $agentText = foreach ($n in @('claude', 'codex')) {
                "$n $($agents[$n].state)$(if ($agents[$n].ContainsKey('until') -and $agents[$n].until) { ' until ' + $agents[$n].until })"
            }
            $lines += 'agents: ' + ($agentText -join ', ')
            Out-Pd2q @{ service = $service; running = @($running | ForEach-Object { @{ job = $_.Id; state = $_.State } }); queued = @($queued | ForEach-Object Id)
                leases = $leases; alarms = $alarms.alarms; jobsBytes = $jobsBytes; builds = @($builds) } ($lines -join "`n")
        }
    }
    'board' {
        $entries = @(Get-Pd2qBoard)
        if ($Since) { $cut = ConvertFrom-Pd2qTime $Since; $entries = @($entries | Where-Object { (ConvertFrom-Pd2qTime $_.landedUtc) -ge $cut }) }
        Out-Pd2q $entries (($entries | ForEach-Object { "$($_.landedUtc) $($_.issue) $($_.commit.Substring(0, 12)) $($_.job) $($_.session): $($_.title)" }) -join "`n")
    }
    'wait' {
        $jobId = $Target
        if ($jobId -notmatch '^q\d+-\d{8}$') { throw "Not a job id: '$jobId'" }
        $file = Join-Path $paths.Journal "$jobId.jsonl"
        if (-not (Test-Path -LiteralPath $file)) { throw "No such job: $jobId" }
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        $watcher = [IO.FileSystemWatcher]::new($paths.Journal, "$jobId.jsonl")
        try {
            while ($true) {
                $last = @(Get-Pd2qJournal $jobId)[-1]
                if ($last.event -in @('landed', 'rejected', 'failed', 'cancelled')) {
                    $report = Join-Path $paths.Reports "$jobId.md"
                    Out-Pd2q @{ job = $jobId; result = $last.event; report = $report; detail = $last.detail } "$jobId $($last.event): $report"
                    if ($last.event -ne 'landed') { exit 1 }
                    break
                }
                if ((Get-Date) -gt $deadline) { throw "$jobId still $($last.event) after $TimeoutSeconds s." }
                [void]$watcher.WaitForChanged([IO.WatcherChangeTypes]::Changed, 5000)  # event, with a 5 s backstop
            }
        } finally { $watcher.Dispose() }
    }
    'break' {
        $issueId = $Target; Assert-Pd2qIssue $issueId
        if (-not $Reason) { throw 'break needs -Reason.' }
        $who = if ($Session) { $Session } else { $env:USERNAME }
        Invoke-Pd2qLocked {
            $l = Read-Pd2qLeases
            $lease = $l.leases[$issueId]
            $dropped = @()
            foreach ($j in @(Get-Pd2qJobs | Where-Object { $_.Request.issue -ceq $issueId -and $_.State -in @('submitted', 'queued') })) {
                Add-Pd2qJournal $j.Id @{ event = 'cancelled'; detail = "break by $who`: $Reason" }
                $dropped += $j.Id
            }
            if ($lease) {
                if ($lease.state -eq 'queued' -and $lease.job -and -not ($dropped -contains $lease.job)) { throw "$issueId belongs to running job $($lease.job); use pd2q cancel." }
                $l.leases.Remove($issueId); Write-Pd2qJson $paths.Leases $l
                # Only a held lease owns its session's scratch. A queued one saved
                # its patch at submit, and that scratch may now hold the session's
                # next issue, which a checkpoint here would file under this one.
                if ($lease.state -eq 'held') {
                    $saved = $false
                    try { [void](Save-Pd2qCheckpoint $issueId $lease.holder.session ("lease broken by $who`: $Reason") $lease.holder.agent); $saved = $true }
                    catch { Add-Pd2qAlarm 'checkpoint-failed' "lease $($lease.lease) on $issueId broken by $who, but its edits could not be saved: $($_.Exception.Message). Scratch kept at $(Get-Pd2qScratchDir $lease.holder.session)." }
                    if ($saved) { Clear-Pd2qScratch $lease.holder.session }
                }
            }
            if (-not $lease -and -not $dropped) { throw "$issueId has no lease and no queued job." }
            Write-Pd2qLog "break $issueId by $who ($Reason): lease $(if ($lease) { $lease.lease } else { 'none' }), dropped $($dropped -join ',')"
            Out-Pd2q @{ issue = $issueId; lease = $lease; dropped = $dropped } "broke $issueId (lease $(if ($lease) { $lease.lease } else { 'none' }); dropped jobs: $(if ($dropped) { $dropped -join ', ' } else { 'none' }))"
        }
    }
    'cancel' {
        $jobId = $Target
        if (-not $Reason) { throw 'cancel needs -Reason.' }
        Invoke-Pd2qLocked {
            $job = @(Get-Pd2qJobs | Where-Object { $_.Id -eq $jobId })[0]
            if (-not $job) { throw "No such job: $jobId" }
            if ($job.State -in @('landed', 'rejected', 'failed', 'cancelled')) { throw "$jobId already ended ($($job.State))." }
            if ($job.State -in @('submitted', 'queued')) {
                Add-Pd2qJournal $jobId @{ event = 'cancelled'; detail = "cancelled before it ran: $Reason" }
                $l = Read-Pd2qLeases
                if ($l.leases[$job.Request.issue] -and $l.leases[$job.Request.issue].job -eq $jobId) { $l.leases.Remove($job.Request.issue); Write-Pd2qJson $paths.Leases $l }
                $text = "$jobId cancelled before it ran"
            } else {
                Write-Pd2qJson (Join-Path $paths.Requests "cancel-$jobId.json") @{ schema = 'pd2q.cancel.v1'; job = $jobId; reason = $Reason; by = $(if ($Session) { $Session } else { $env:USERNAME }); atUtc = Format-Pd2qTime (Get-Pd2qNow) }
                Send-Pd2qWake
                $text = "cancel of running $jobId requested; the service stops its process tree and deletes its folder"
            }
            Write-Pd2qLog "cancel $jobId ($Reason)"
            Out-Pd2q @{ job = $jobId } $text
        }
    }
    'agent' {
        $agentName = $Target; $state = $Value
        if ($state -notin @('on', 'off')) { throw 'pd2q agent <claude|codex> <on|off> [-Until time] [-Reason why]' }
        Invoke-Pd2qLocked {
            Set-Pd2qAgent $agentName $state $Until $Reason $(if ($Session) { $Session } else { $env:USERNAME })
            if ($state -eq 'off') { [void](Update-Pd2qLeases) }  # its held leases checkpoint and free now
            Out-Pd2q (Get-Pd2qAgents) "agent $agentName $state$(if ($Until) { ' until ' + $Until })"
        }
    }
    'resume' {
        if (-not $Reason) { throw 'resume needs -Reason: what was checked in the dev checkout.' }
        Invoke-Pd2qLocked {
            $service = Read-Pd2qJson $paths.Service @{ schema = 'pd2q.service.v1' }
            $service.paused = $null
            Write-Pd2qJson $paths.Service $service
            Write-Pd2qLog "resume by $(if ($Session) { $Session } else { $env:USERNAME }): $Reason"
            Send-Pd2qWake
            Out-Pd2q $service 'queue resumed'
        }
    }
}
