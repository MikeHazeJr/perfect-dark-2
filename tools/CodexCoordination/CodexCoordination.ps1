param(
    [Parameter(Position = 0)]
    [ValidateSet("status", "register", "heartbeat", "chat", "queue", "start", "finish", "flag-stale", "clear-stale", "complete", "cleanup", "prune-chat", "prompt", "wait-turn", "health")]
    [string]$Command = "status",

    [string]$SessionId,
    [string]$Goal,
    [string]$Plan,
    [string]$CurrentTask,
    [string]$Eta,
    [ValidateSet("active", "idle", "waiting_approval", "blocked", "done")]
    [string]$Status = "active",
    [string]$BlockedOn,

    [string]$Message,
    [string]$ToSessionId,
    [string]$ReplyTo,

    [ValidateSet("build", "test", "smoke", "multiplayer", "capture", "editor", "asset-import", "deployment", "custom")]
    [string]$Type = "custom",
    [string]$Resource = "build",
    [string]$Title,
    [string]$Details,
    [string]$QueueId,
    [ValidateSet("done", "failed", "cancelled", "abandoned")]
    [string]$Result = "done",
    [switch]$Archive,
    [string]$Summary,
    [string]$Evidence,

    [int]$ChatKeep = 30,
    [int]$DeadAfterMinutes = 360,
    [int]$SessionKeep = 24,
    [int]$MaxMessageChars = 600,
    [int]$MaxFieldChars = 1200,
    [int]$TempArtifactMaxAgeMinutes = 60,
    [int]$BackupArtifactMaxAgeDays = 3,
    [long]$MaxBackupArtifactBytes = 52428800,

    [int]$WaitTimeoutSeconds = 1800,
    [switch]$Fix
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$CanonicalProjectRoot = "C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike"
$StateRoot = if ($env:PD2_CODEX_COORDINATION_ROOT) { $env:PD2_CODEX_COORDINATION_ROOT } else { Join-Path $CanonicalProjectRoot ".codex-coordination" }
$StatePath = Join-Path $StateRoot "state.json"
$CompletedPath = Join-Path $StateRoot "completed-tasks.md"
$QueueResultsPath = Join-Path $StateRoot "queue-results.md"
$MutexName = "PerfectDark2CodexCoordination"

function Get-UtcStamp {
    return [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")
}

function New-CoordinationId([string]$Prefix) {
    $stamp = [DateTime]::UtcNow.ToString("yyyyMMddHHmmssfff")
    $suffix = [Guid]::NewGuid().ToString("N").Substring(0, 8)
    return "${Prefix}_${stamp}_${suffix}"
}

function Test-HasProperty($Object, [string]$Name) {
    if ($null -eq $Object) { return $false }
    return ($Object.PSObject.Properties.Name -contains $Name)
}

function Ensure-StateRoot {
    if (-not (Test-Path -LiteralPath $StateRoot)) {
        New-Item -ItemType Directory -Path $StateRoot | Out-Null
    }
    if (-not (Test-Path -LiteralPath $CompletedPath)) {
        "# Completed Codex Coordination Tasks`r`n`r`nSubstantial completed work is archived here by the coordination manager. Keep minutia in chat or session notes, not here.`r`n" |
            Set-Content -LiteralPath $CompletedPath -Encoding UTF8
    }
    if (-not (Test-Path -LiteralPath $QueueResultsPath)) {
        "# Codex Queue Results`r`n`r`nEvery finished queue item is logged here before it is removed from the live queue. This keeps test/build/capture summaries available while the next queued item runs.`r`n" |
            Set-Content -LiteralPath $QueueResultsPath -Encoding UTF8
    }
    Remove-StaleStateArtifacts
}

function Remove-StaleStateArtifacts {
    if (-not (Test-Path -LiteralPath $StateRoot)) { return }

    $root = (Resolve-Path -LiteralPath $StateRoot).Path
    $now = [DateTime]::UtcNow
    $tmpAge = [TimeSpan]::FromMinutes([Math]::Max(1, $TempArtifactMaxAgeMinutes))
    $backupAge = [TimeSpan]::FromDays([Math]::Max(1, $BackupArtifactMaxAgeDays))
    $backupPatterns = @("state*.bak*.json", "state.pre-*.json", "state.*backup*.json", "state.json.*.json")
    $candidates = @()
    $candidates += @(Get-ChildItem -LiteralPath $root -Filter "state.*.tmp" -File -ErrorAction SilentlyContinue)
    foreach ($pattern in $backupPatterns) {
        $candidates += @(Get-ChildItem -LiteralPath $root -Filter $pattern -File -ErrorAction SilentlyContinue)
    }

    foreach ($file in @($candidates | Sort-Object FullName -Unique)) {
        if ($file.Name -eq "state.json") { continue }
        if ($file.Name -eq "queue-results.md" -or $file.Name -eq "completed-tasks.md") { continue }

        $resolved = (Resolve-Path -LiteralPath $file.FullName).Path
        if (-not $resolved.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) { continue }

        $age = $now - $file.LastWriteTimeUtc
        $remove = $false
        if ($file.Name -like "state.*.tmp") {
            $remove = $age -ge $tmpAge
        } else {
            $remove = ($age -ge $backupAge) -or (($file.Length -gt $MaxBackupArtifactBytes) -and ($age -ge $tmpAge))
        }

        if ($remove) {
            Remove-Item -LiteralPath $resolved -Force
        }
    }
}

function New-State {
    return [pscustomobject][ordered]@{
        version = 1
        projectRoot = $CanonicalProjectRoot
        updatedUtc = Get-UtcStamp
        settings = [pscustomobject][ordered]@{
            chatKeepPerSession = $ChatKeep
            deadAfterMinutes = $DeadAfterMinutes
            sessionKeep = $SessionKeep
            maxMessageChars = $MaxMessageChars
            maxFieldChars = $MaxFieldChars
            tempArtifactMaxAgeMinutes = $TempArtifactMaxAgeMinutes
            backupArtifactMaxAgeDays = $BackupArtifactMaxAgeDays
            maxBackupArtifactBytes = $MaxBackupArtifactBytes
        }
        sessions = @()
        chat = @()
        queue = @()
        nextQueueSequence = 1
    }
}

function Normalize-State($State) {
    if (-not (Test-HasProperty $State "version")) { Add-Member -InputObject $State -NotePropertyName version -NotePropertyValue 1 }
    if (-not (Test-HasProperty $State "projectRoot")) { Add-Member -InputObject $State -NotePropertyName projectRoot -NotePropertyValue $CanonicalProjectRoot }
    if (-not (Test-HasProperty $State "updatedUtc")) { Add-Member -InputObject $State -NotePropertyName updatedUtc -NotePropertyValue (Get-UtcStamp) }
    if (-not (Test-HasProperty $State "settings")) {
        Add-Member -InputObject $State -NotePropertyName settings -NotePropertyValue ([pscustomobject][ordered]@{})
    }
    if (-not (Test-HasProperty $State.settings "chatKeepPerSession")) { Add-Member -InputObject $State.settings -NotePropertyName chatKeepPerSession -NotePropertyValue $ChatKeep }
    if (-not (Test-HasProperty $State.settings "deadAfterMinutes")) { Add-Member -InputObject $State.settings -NotePropertyName deadAfterMinutes -NotePropertyValue $DeadAfterMinutes }
    if (-not (Test-HasProperty $State.settings "sessionKeep")) { Add-Member -InputObject $State.settings -NotePropertyName sessionKeep -NotePropertyValue $SessionKeep }
    if (-not (Test-HasProperty $State.settings "maxMessageChars")) { Add-Member -InputObject $State.settings -NotePropertyName maxMessageChars -NotePropertyValue $MaxMessageChars }
    if (-not (Test-HasProperty $State.settings "maxFieldChars")) { Add-Member -InputObject $State.settings -NotePropertyName maxFieldChars -NotePropertyValue $MaxFieldChars }
    if (-not (Test-HasProperty $State.settings "tempArtifactMaxAgeMinutes")) { Add-Member -InputObject $State.settings -NotePropertyName tempArtifactMaxAgeMinutes -NotePropertyValue $TempArtifactMaxAgeMinutes }
    if (-not (Test-HasProperty $State.settings "backupArtifactMaxAgeDays")) { Add-Member -InputObject $State.settings -NotePropertyName backupArtifactMaxAgeDays -NotePropertyValue $BackupArtifactMaxAgeDays }
    if (-not (Test-HasProperty $State.settings "maxBackupArtifactBytes")) { Add-Member -InputObject $State.settings -NotePropertyName maxBackupArtifactBytes -NotePropertyValue $MaxBackupArtifactBytes }
    if (-not (Test-HasProperty $State "sessions")) { Add-Member -InputObject $State -NotePropertyName sessions -NotePropertyValue @() }
    if (-not (Test-HasProperty $State "chat")) { Add-Member -InputObject $State -NotePropertyName chat -NotePropertyValue @() }
    if (-not (Test-HasProperty $State "queue")) { Add-Member -InputObject $State -NotePropertyName queue -NotePropertyValue @() }
    if (-not (Test-HasProperty $State "nextQueueSequence")) { Add-Member -InputObject $State -NotePropertyName nextQueueSequence -NotePropertyValue 1 }

    $maxOrder = 0
    foreach ($item in @($State.queue | Sort-Object requestedUtc, id)) {
        if ($item.status -eq "granted") {
            $item.status = "queued"
            $item.managerNote = "Migrated from granted to queued; start only when this item is next."
        }
        if (-not (Test-HasProperty $item "resource") -or [string]::IsNullOrWhiteSpace($item.resource)) {
            if (-not (Test-HasProperty $item "resource")) { Add-Member -InputObject $item -NotePropertyName resource -NotePropertyValue "build" }
            else { $item.resource = "build" }
        }
        if (-not (Test-HasProperty $item "order")) {
            Add-Member -InputObject $item -NotePropertyName order -NotePropertyValue ([int]$State.nextQueueSequence)
            $State.nextQueueSequence = [int]$State.nextQueueSequence + 1
        }
        if (-not (Test-HasProperty $item "staleFlaggedBy")) { Add-Member -InputObject $item -NotePropertyName staleFlaggedBy -NotePropertyValue "" }
        if (-not (Test-HasProperty $item "staleFlaggedUtc")) { Add-Member -InputObject $item -NotePropertyName staleFlaggedUtc -NotePropertyValue "" }
        if (-not (Test-HasProperty $item "staleReason")) { Add-Member -InputObject $item -NotePropertyName staleReason -NotePropertyValue "" }
        if ([int]$item.order -gt $maxOrder) { $maxOrder = [int]$item.order }
    }
    if ([int]$State.nextQueueSequence -le $maxOrder) {
        $State.nextQueueSequence = $maxOrder + 1
    }
    return $State
}

function Limit-Text($Value, [int]$MaxChars) {
    if ($null -eq $Value) { return "" }
    $text = [string]$Value
    if ($MaxChars -le 0 -or $text.Length -le $MaxChars) { return $text }
    return $text.Substring(0, [Math]::Max(0, $MaxChars - 24)) + "... [truncated in state]"
}

function Set-LimitedProperty($Object, [string]$Name, [int]$MaxChars) {
    if ($null -eq $Object -or -not (Test-HasProperty $Object $Name)) { return }
    $Object.$Name = Limit-Text $Object.$Name $MaxChars
}

function Get-UtcDateOrMin($Value) {
    if ($null -eq $Value -or [string]::IsNullOrWhiteSpace([string]$Value)) { return [DateTime]::MinValue }
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    try { return [DateTime]::Parse([string]$Value).ToUniversalTime() }
    catch { return [DateTime]::MinValue }
}

function Compact-State($State) {
    if ($null -eq $State) { return $State }

    $State.settings.chatKeepPerSession = $ChatKeep
    $State.settings.deadAfterMinutes = $DeadAfterMinutes
    $State.settings.sessionKeep = $SessionKeep
    $State.settings.maxMessageChars = $MaxMessageChars
    $State.settings.maxFieldChars = $MaxFieldChars
    $State.settings.tempArtifactMaxAgeMinutes = $TempArtifactMaxAgeMinutes
    $State.settings.backupArtifactMaxAgeDays = $BackupArtifactMaxAgeDays
    $State.settings.maxBackupArtifactBytes = $MaxBackupArtifactBytes

    foreach ($session in @($State.sessions)) {
        Set-LimitedProperty $session "goal" $MaxFieldChars
        Set-LimitedProperty $session "currentTask" $MaxFieldChars
        Set-LimitedProperty $session "blockedOn" $MaxFieldChars
        Set-LimitedProperty $session "eta" 120
        if ((Test-HasProperty $session "plan") -and $null -ne $session.plan) {
            $session.plan = @($session.plan | ForEach-Object { Limit-Text $_ 240 })
        }
    }

    foreach ($message in @($State.chat)) {
        Set-LimitedProperty $message "message" $MaxMessageChars
        Set-LimitedProperty $message "toSessionId" 120
        Set-LimitedProperty $message "replyTo" 120
    }
    Prune-Chat $State "" $ChatKeep

    $liveStatuses = @("queued", "granted", "running")
    $State.queue = @(@($State.queue) | Where-Object { $_.status -in $liveStatuses } | Sort-Object resource, order, requestedUtc)
    foreach ($item in @($State.queue)) {
        Set-LimitedProperty $item "title" 240
        Set-LimitedProperty $item "details" $MaxFieldChars
        Set-LimitedProperty $item "eta" 120
        Set-LimitedProperty $item "summary" $MaxFieldChars
        Set-LimitedProperty $item "managerNote" 360
        Set-LimitedProperty $item "staleReason" 360
    }

    $queueOwners = @(@($State.queue) | ForEach-Object { $_.sessionId } | Sort-Object -Unique)
    $protected = @(@($State.sessions) | Where-Object {
        $_.status -in @("active", "blocked", "waiting_approval") -or
        $queueOwners -contains $_.sessionId
    })
    $protectedSessionIds = @($protected | ForEach-Object { $_.sessionId })
    $recent = @(@($State.sessions) | Where-Object {
        $protectedSessionIds -notcontains $_.sessionId
    } | Sort-Object @{ Expression = { Get-UtcDateOrMin $_.lastSeenUtc }; Descending = $true } | Select-Object -First $SessionKeep)

    $seen = @{}
    $rolled = @()
    foreach ($session in @($protected + $recent)) {
        if ($null -eq $session -or [string]::IsNullOrWhiteSpace([string]$session.sessionId)) { continue }
        if ($seen.ContainsKey($session.sessionId)) { continue }
        $seen[$session.sessionId] = $true
        $rolled += $session
    }
    $State.sessions = @($rolled | Sort-Object @{ Expression = { Get-UtcDateOrMin $_.lastSeenUtc } })

    return $State
}

function Load-State {
    Ensure-StateRoot
    if (-not (Test-Path -LiteralPath $StatePath)) {
        return New-State
    }
    $raw = Get-Content -LiteralPath $StatePath -Raw
    if ([string]::IsNullOrWhiteSpace($raw)) {
        return New-State
    }
    return Normalize-State ($raw | ConvertFrom-Json)
}

function Save-State($State) {
    $State = Compact-State $State
    $State.updatedUtc = Get-UtcStamp
    $tmp = Join-Path $StateRoot ("state.{0}.tmp" -f ([Guid]::NewGuid().ToString("N")))
    $State | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $StatePath -Force
    Remove-StaleStateArtifacts
}

function With-Lock([scriptblock]$Body) {
    $mutex = [System.Threading.Mutex]::new($false, $MutexName)
    $taken = $false
    try {
        $taken = $mutex.WaitOne([TimeSpan]::FromSeconds(30))
        if (-not $taken) {
            throw "Timed out waiting for coordination lock. Try again in a few seconds."
        }
        & $Body
    }
    finally {
        if ($taken) { $mutex.ReleaseMutex() | Out-Null }
        $mutex.Dispose()
    }
}

function Get-Session($State, [string]$Id) {
    $matches = @($State.sessions | Where-Object { $_.sessionId -eq $Id } | Select-Object -First 1)
    if ($matches.Count -eq 0) { return $null }
    return $matches[0]
}

function Get-QueueItem($State, [string]$Id) {
    $matches = @($State.queue | Where-Object { $_.id -eq $Id } | Select-Object -First 1)
    if ($matches.Count -eq 0) { return $null }
    return $matches[0]
}

function Upsert-Session($State, $Session) {
    $State.sessions = @($State.sessions | Where-Object { $_.sessionId -ne $Session.sessionId }) + $Session
}

function Assert-SessionId {
    if ([string]::IsNullOrWhiteSpace($SessionId)) {
        throw "SessionId is required for this command."
    }
}

function Split-Plan([string]$Text) {
    if ([string]::IsNullOrWhiteSpace($Text)) { return @() }
    return @($Text -split "\s*;\s*" | Where-Object { -not ([string]::IsNullOrWhiteSpace($_)) })
}

function Get-ChatSortUtc($Message) {
    if ($null -eq $Message -or $null -eq $Message.utc) { return [DateTime]::MinValue }
    if ($Message.utc -is [DateTime]) { return $Message.utc.ToUniversalTime() }
    return [DateTime]::Parse([string]$Message.utc).ToUniversalTime()
}

function Prune-Chat($State, [string]$OnlySessionId, [int]$Keep) {
    $kept = @()
    $ids = @($State.chat | ForEach-Object { $_.sessionId } | Sort-Object -Unique)
    foreach ($id in $ids) {
        $messages = @($State.chat | Where-Object { $_.sessionId -eq $id } | Sort-Object @{ Expression = { Get-ChatSortUtc $_ } }, @{ Expression = { $_.id } })
        if (($OnlySessionId -eq "") -or ($id -eq $OnlySessionId)) {
            $messages = @($messages | Select-Object -Last $Keep)
        }
        $kept += $messages
    }
    $State.chat = @($kept | Sort-Object @{ Expression = { Get-ChatSortUtc $_ } })
}

function Append-QueueResult($Item, [string]$FinishingSession, [string]$QueueResult, [string]$QueueSummary, [string]$QueueEvidence) {
    Ensure-StateRoot
    $stamp = Get-UtcStamp
    $title = if (Test-HasProperty $Item "title") { $Item.title } else { "(untitled queue item)" }
    $entry = @()
    $entry += "## $stamp - $title"
    $entry += ""
    $entry += "- QueueId: $($Item.id)"
    $entry += "- Owner: $($Item.sessionId)"
    $entry += "- ClearedBy: $FinishingSession"
    $entry += "- Result: $QueueResult"
    $entry += "- Resource: $($Item.resource)"
    $entry += "- Type: $($Item.type)"
    if ((Test-HasProperty $Item "requestedUtc") -and (-not ([string]::IsNullOrWhiteSpace([string]$Item.requestedUtc)))) { $entry += "- RequestedUtc: $($Item.requestedUtc)" }
    if ((Test-HasProperty $Item "startedUtc") -and (-not ([string]::IsNullOrWhiteSpace([string]$Item.startedUtc)))) { $entry += "- StartedUtc: $($Item.startedUtc)" }
    if (-not ([string]::IsNullOrWhiteSpace($QueueSummary))) { $entry += "- Summary: $QueueSummary" }
    if (-not ([string]::IsNullOrWhiteSpace($QueueEvidence))) { $entry += "- Evidence: $QueueEvidence" }
    if ((Test-HasProperty $Item "staleFlaggedBy") -and (-not ([string]::IsNullOrWhiteSpace([string]$Item.staleFlaggedBy)))) {
        $entry += "- StaleFlaggedBy: $($Item.staleFlaggedBy)"
        $entry += "- StaleFlaggedUtc: $($Item.staleFlaggedUtc)"
        if (-not ([string]::IsNullOrWhiteSpace([string]$Item.staleReason))) { $entry += "- StaleReason: $($Item.staleReason)" }
    }
    $entry += ""
    Add-Content -LiteralPath $QueueResultsPath -Value ($entry -join "`r`n") -Encoding UTF8
}

function Remove-QueueItem($State, [string]$Id) {
    $State.queue = @($State.queue | Where-Object { $_.id -ne $Id })
}

function Abandon-DeadEntries($State) {
    $now = [DateTime]::UtcNow
    $deadSessionIds = @()
    foreach ($session in @($State.sessions)) {
        if ($session.status -eq "waiting_approval") { continue }
        $lastSeen = if ($session.lastSeenUtc -is [DateTime]) {
            $session.lastSeenUtc.ToUniversalTime()
        } else {
            [DateTime]::Parse($session.lastSeenUtc).ToUniversalTime()
        }
        $ageMinutes = ($now - $lastSeen).TotalMinutes
        if ($ageMinutes -gt $DeadAfterMinutes) {
            $deadSessionIds += $session.sessionId
        }
    }

    foreach ($deadId in $deadSessionIds) {
        foreach ($item in @($State.queue | Where-Object { $_.sessionId -eq $deadId -and $_.status -in @("queued", "granted", "running") })) {
            $summaryText = "Abandoned by cleanup because the owning session was stale."
            Append-QueueResult $item "cleanup" "abandoned" $summaryText ""
            Remove-QueueItem $State $item.id
        }
    }

    if ($deadSessionIds.Count -gt 0) {
        $State.sessions = @($State.sessions | Where-Object { $deadSessionIds -notcontains $_.sessionId })
    }

    return $deadSessionIds
}

function Get-StaleFlaggedQueueItems($State) {
    return @($State.queue | Where-Object {
        $_.status -in @("queued", "running") -and
        (Test-HasProperty $_ "staleFlaggedBy") -and
        -not ([string]::IsNullOrWhiteSpace([string]$_.staleFlaggedBy))
    } | Sort-Object resource, order, requestedUtc)
}

function Resolve-StaleFlags($State, [string]$HeartbeatSessionId) {
    $flagged = @(Get-StaleFlaggedQueueItems $State)
    foreach ($item in $flagged) {
        $owner = Get-Session $State $item.sessionId
        if ($null -eq $owner) {
            $summaryText = "Cleared stale queue item because the owner session no longer exists."
            Append-QueueResult $item $HeartbeatSessionId "abandoned" $summaryText ""
            Remove-QueueItem $State $item.id
            Write-Host "Cleared stale queue item $($item.id): owner session no longer exists."
            continue
        }

        if ($owner.status -eq "waiting_approval") {
            $item.managerNote = "Stale flag reviewed on heartbeat; owner is waiting for approval, so the queue item is preserved."
            Write-Host "Preserved flagged queue item $($item.id): owner is waiting for approval."
            continue
        }

        if ($owner.status -in @("done", "idle")) {
            $summaryText = "Cleared stale queue item because owner session status is '$($owner.status)'."
            Append-QueueResult $item $HeartbeatSessionId "abandoned" $summaryText ""
            Remove-QueueItem $State $item.id
            Write-Host "Cleared stale queue item $($item.id): owner session is $($owner.status)."
            continue
        }

        if ($item.sessionId -eq $HeartbeatSessionId) {
            Write-Host "Your queue item $($item.id) is flagged as potentially stale by $($item.staleFlaggedBy): $($item.staleReason)"
            Write-Host "If it is stale, run finish -Result cancelled or clear-stale after validating. If it is still active, post chat/status so others know."
        } else {
            Write-Host "Queue item $($item.id) is flagged as potentially stale; owner $($item.sessionId) is still active."
        }
    }
}

function Get-QueuePlacement($State, $Item) {
    $running = @($State.queue | Where-Object { $_.resource -eq $Item.resource -and $_.status -eq "running" } | Sort-Object order, startedUtc)
    $queued = @($State.queue | Where-Object { $_.resource -eq $Item.resource -and $_.status -eq "queued" } | Sort-Object order, requestedUtc)
    $ahead = @()
    $position = 0

    if ($Item.status -eq "running") {
        return [pscustomobject][ordered]@{
            label = "running"
            position = 0
            ahead = @()
            canStart = $false
        }
    }

    for ($i = 0; $i -lt $queued.Count; $i++) {
        if ($queued[$i].id -eq $Item.id) {
            $position = $i + 1
            break
        }
    }

    if ($position -gt 1) {
        $ahead += @($queued | Select-Object -First ($position - 1))
    }
    $ahead += $running

    $canStart = ($position -eq 1 -and $running.Count -eq 0)
    $label = if ($canStart) { "next" } else { "position $position" }
    return [pscustomobject][ordered]@{
        label = $label
        position = $position
        ahead = $ahead
        canStart = $canStart
    }
}

function Parse-EtaMinutes([string]$Eta) {
    if ([string]::IsNullOrWhiteSpace($Eta)) { return 30 }
    $m = [regex]::Match($Eta, '(\d+(?:\.\d+)?)\s*(h|hr|hour|m|min)?')
    if (-not $m.Success) { return 30 }
    $val = [double]$m.Groups[1].Value
    if ($m.Groups[2].Value -match '^h') { return [int][Math]::Ceiling($val * 60) }
    return [int][Math]::Ceiling($val)
}

# Minutes a running item is past its grace budget (2x ETA, min 10). 0 = not overdue.
function Get-OverdueMinutes($Item) {
    if ($Item.status -ne "running") { return 0 }
    if ([string]::IsNullOrWhiteSpace([string]$Item.startedUtc)) { return 0 }
    $started = ([DateTime]::Parse([string]$Item.startedUtc)).ToUniversalTime()
    $budget = [Math]::Max(10, (Parse-EtaMinutes ([string]$Item.eta)) * 2)
    $over = ([DateTime]::UtcNow - $started).TotalMinutes - $budget
    if ($over -gt 0) { return [int]$over }
    return 0
}

# Can we observe a live process for this resource? Unknown resources report busy
# so the health sweep never auto-reaps a queue it cannot prove dead.
function Test-ResourceBusy([string]$ResourceName) {
    $patterns = switch -Regex ($ResourceName) {
        "^(build|compiler)$" { @("ninja", "make", "cmake", "cc1", "cc1plus", "gcc", "g++") }
        "^(test|test-runner)$" { @("pd-tests", "PerfectDark") }
        "^(game|smoke|multiplayer|capture|editor)$" { @("PerfectDark", "pd-tests", "WerFault") }
        "^(deployment|release)$" { @("git", "gh") }
        "^(asset-import|extractor)$" { @("PerfectDark", "pd-tests") }
        default { @() }
    }
    if ($patterns.Count -eq 0) { return $true }
    foreach ($name in $patterns) {
        if (Get-Process -Name $name -ErrorAction SilentlyContinue) { return $true }
    }
    return $false
}

# Detect (and with -Fix, reap) running items whose runner is provably dead:
# past 2x ETA AND no live process for the resource. Returns finding strings.
function Invoke-QueueHealth($State, [bool]$DoFix, [string]$Actor) {
    $findings = @()
    foreach ($item in @($State.queue)) {
        $over = Get-OverdueMinutes $item
        if ($over -le 0) { continue }
        if (Test-ResourceBusy ([string]$item.resource)) {
            $findings += "OVERDUE $($item.id) '$($item.title)': $over min past 2x ETA, but a $($item.resource) process IS alive (long run or foreign process) - verify manually."
            continue
        }
        if ($DoFix) {
            $reason = "AUTO-REAPED by $Actor - running $over min past 2x ETA with no $($item.resource) process alive"
            Append-QueueResult $item $Actor "abandoned" $reason ""
            Remove-QueueItem $State $item.id
            $findings += "REAPED $($item.id) '$($item.title)' ($reason). Queue unblocked for the next item."
        } else {
            if ([string]::IsNullOrWhiteSpace([string]$item.staleFlaggedBy)) {
                $item.staleFlaggedBy = "auto-eta"
                $item.staleFlaggedUtc = Get-UtcStamp
                $item.staleReason = "running $over min past 2x ETA; no $($item.resource) process detected"
            }
            $findings += "DEAD-RUNNER $($item.id) '$($item.title)': $over min past budget, no $($item.resource) process. Run 'health -Fix' (or wait-turn behind it) to reap."
        }
    }
    return $findings
}

function Write-QueuePlacement($State, $Item) {
    $placement = Get-QueuePlacement $State $Item
    if ($placement.canStart) {
        Write-Host ("Queue item {0} is next for resource '{1}'. Run start when you are ready to use it." -f $Item.id, $Item.resource)
        return
    }

    if ($Item.status -eq "running") {
        Write-Host ("Queue item {0} is running for resource '{1}'." -f $Item.id, $Item.resource)
        return
    }

    Write-Host ("Queue item {0} is queued for resource '{1}' at {2}." -f $Item.id, $Item.resource, $placement.label)
    if ($placement.ahead.Count -gt 0) {
        Write-Host "Ahead:"
        foreach ($ahead in $placement.ahead) {
            Write-Host ("  {0} [{1}] {2} owner={3}" -f $ahead.id, $ahead.status, $ahead.title, $ahead.sessionId)
        }
    }
}

function Append-Completed([string]$Session, [string]$TaskTitle, [string]$TaskSummary, [string]$TaskEvidence) {
    Ensure-StateRoot
    $stamp = Get-UtcStamp
    $entry = @()
    $entry += "## $stamp - $TaskTitle"
    $entry += ""
    $entry += "- Session: $Session"
    if (-not ([string]::IsNullOrWhiteSpace($TaskSummary))) { $entry += "- Summary: $TaskSummary" }
    if (-not ([string]::IsNullOrWhiteSpace($TaskEvidence))) { $entry += "- Evidence: $TaskEvidence" }
    $entry += ""
    Add-Content -LiteralPath $CompletedPath -Value ($entry -join "`r`n") -Encoding UTF8
}

function Write-StateSummary($State) {
    $activeSessions = @($State.sessions | Sort-Object lastSeenUtc)
    $queueItems = @($State.queue | Where-Object { $_.status -in @("queued", "running") } | Sort-Object resource, order, requestedUtc)
    $recentChat = @($State.chat | Sort-Object @{ Expression = { Get-ChatSortUtc $_ } }, @{ Expression = { $_.id } } | Select-Object -Last 12)

    Write-Host "Coordination state: $StatePath"
    Write-Host "Updated UTC: $($State.updatedUtc)"
    Write-Host ""
    Write-Host "Sessions:"
    if ($activeSessions.Count -eq 0) {
        Write-Host "  none"
    } else {
        foreach ($s in $activeSessions) {
            Write-Host ("  {0} [{1}] eta={2} last={3}" -f $s.sessionId, $s.status, $s.eta, $s.lastSeenUtc)
            if (-not ([string]::IsNullOrWhiteSpace([string]$s.goal))) { Write-Host ("    goal: {0}" -f $s.goal) }
            if (-not ([string]::IsNullOrWhiteSpace([string]$s.currentTask))) { Write-Host ("    task: {0}" -f $s.currentTask) }
        }
    }
    Write-Host ""
    Write-Host "Queue:"
    if ($queueItems.Count -eq 0) {
        Write-Host "  none"
    } else {
        foreach ($q in $queueItems) {
            $placement = Get-QueuePlacement $State $q
            Write-Host ("  {0} [{1}; {2}] {3}/{4} owner={5} eta={6}" -f $q.id, $q.status, $placement.label, $q.resource, $q.type, $q.sessionId, $q.eta)
            Write-Host ("    title: {0}" -f $q.title)
            if ((Test-HasProperty $q "staleFlaggedBy") -and (-not ([string]::IsNullOrWhiteSpace([string]$q.staleFlaggedBy)))) {
                Write-Host ("    stale: flagged by {0} at {1}; {2}" -f $q.staleFlaggedBy, $q.staleFlaggedUtc, $q.staleReason)
            }
        }
    }
    Write-Host ""
    Write-Host "Recent chat:"
    if ($recentChat.Count -eq 0) {
        Write-Host "  none"
    } else {
        foreach ($m in $recentChat) {
            $target = if ([string]::IsNullOrWhiteSpace($m.toSessionId)) { "all" } else { $m.toSessionId }
            Write-Host ("  {0} {1} -> {2}: {3}" -f $m.utc, $m.sessionId, $target, $m.message)
        }
    }
}

function Write-Prompt {
    @"
Before doing project work, use the Perfect Dark 2 coordination hub. The project is at C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike. Read AGENTS.md and docs/CODEX_COORDINATION.md. Run .\Tools\CodexCoordination\CodexCoordination.ps1 status, then register with your session id, goal, plan, current task, and ETA. Read the Workbench roadmap and process new notes affecting your lane. Queue build, test-runner, game, capture, editor, deployment, extraction, and other exclusive resources. The manager keeps FIFO order per resource. Do not start exclusive work until your item is next and you have run start. Finish immediately with durable evidence so the next session can proceed. Heartbeat on task changes and at least every 20 minutes. Use chat for short operational coordination and Workbench notes for durable handoffs.
"@ | Write-Output
}

switch ($Command) {
    "prompt" {
        Write-Prompt
        break
    }

    "status" {
        With-Lock {
            $state = Load-State
            $health = @(Invoke-QueueHealth $state $false "status-check")
            Save-State $state
            Write-StateSummary $state
            if ($health.Count -gt 0) {
                Write-Host ""
                Write-Host "Queue health:"
                foreach ($f in $health) { Write-Host "  ! $f" }
            }
        }
        break
    }

    "health" {
        # One-stop wedge sweep: dead runners and orphaned git index.lock.
        # Read-only by default; -Fix reaps/deletes what is provably dead.
        $actor = if ([string]::IsNullOrWhiteSpace($SessionId)) { "health-check" } else { $SessionId }
        With-Lock {
            $state = Load-State
            $findings = @(Invoke-QueueHealth $state $Fix.IsPresent $actor)
            Save-State $state
            if ($findings.Count -eq 0) { Write-Host "Queue: healthy (no overdue running items)." }
            else { foreach ($f in $findings) { Write-Host "! $f" } }
        }
        $gitLock = Join-Path $CanonicalProjectRoot ".git\index.lock"
        if (Test-Path -LiteralPath $gitLock) {
            $ageMin = [int]([DateTime]::UtcNow - (Get-Item -LiteralPath $gitLock).LastWriteTimeUtc).TotalMinutes
            $gitAlive = [bool](Get-Process -Name "git" -ErrorAction SilentlyContinue)
            if ($ageMin -ge 15 -and -not $gitAlive) {
                if ($Fix) {
                    Remove-Item -LiteralPath $gitLock -Force
                    Write-Host "! FIXED: removed orphaned .git/index.lock (age ${ageMin}m, no git.exe alive)."
                } else {
                    Write-Host "! ORPHANED .git/index.lock (age ${ageMin}m, no git.exe alive) - wedges all git writes. Run 'health -Fix'."
                }
            } else {
                Write-Host ".git/index.lock present (age ${ageMin}m, git.exe alive=$gitAlive) - likely a live operation; leaving it."
            }
        } else { Write-Host "git index.lock: none." }
        break
    }

    "wait-turn" {
        # Block (poll server-side) until the given queue item is next/running, gone, or timeout.
        # Saves sessions from burning their own polling loops. Read-only: never writes state.
        # Exit codes: 0 = next or already running (proceed), 2 = item gone, 3 = timeout.
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($QueueId)) { throw "wait-turn requires -QueueId." }
        $deadline = [DateTime]::UtcNow.AddSeconds([Math]::Max(30, $WaitTimeoutSeconds))
        while ($true) {
            $script:waitPlacementLabel = $null
            $script:waitVerdict = $null
            With-Lock {
                $state = Load-State
                # Waiters self-heal the queue: reap provably-dead running items blocking the head.
                $reaped = @(Invoke-QueueHealth $state $true ("wait-turn:" + $SessionId))
                if ($reaped.Count -gt 0) {
                    Save-State $state
                    foreach ($f in $reaped) { Write-Host "wait-turn health: $f" }
                }
                $item = @(Get-QueueItem $state $QueueId) | Select-Object -First 1
                if (-not $item) {
                    $script:waitVerdict = "gone"
                } elseif ($item.status -eq "running") {
                    $script:waitVerdict = "running"
                } else {
                    $placement = Get-QueuePlacement $state $item
                    if ($placement.canStart) { $script:waitVerdict = "next" }
                    $script:waitPlacementLabel = $placement.label
                }
            }
            $verdict = $script:waitVerdict
            $placementLabel = $script:waitPlacementLabel
            if ($verdict -eq "gone") {
                Write-Host "wait-turn: queue item $QueueId no longer exists (finished or cancelled)."
                exit 2
            }
            if ($verdict -eq "running") {
                Write-Host "wait-turn: queue item $QueueId is already running; proceed."
                exit 0
            }
            if ($verdict -eq "next") {
                Write-Host "wait-turn: queue item $QueueId is next for its resource. Run start, then begin."
                exit 0
            }
            if ([DateTime]::UtcNow -ge $deadline) {
                Write-Host ("wait-turn: timed out after {0}s; item is still {1}. Re-run wait-turn or check status." -f $WaitTimeoutSeconds, $placementLabel)
                exit 3
            }
            Start-Sleep -Seconds 15
        }
        break
    }

    "register" {
        Assert-SessionId
        With-Lock {
            $state = Load-State
            $existing = Get-Session $state $SessionId
            $now = Get-UtcStamp
            $started = if (($null -ne $existing) -and (-not ([string]::IsNullOrWhiteSpace([string]$existing.startedUtc)))) { $existing.startedUtc } else { $now }
            $session = [pscustomobject][ordered]@{
                sessionId = $SessionId
                status = $Status
                goal = $Goal
                plan = @(Split-Plan $Plan)
                currentTask = $CurrentTask
                eta = $Eta
                blockedOn = $BlockedOn
                startedUtc = $started
                lastSeenUtc = $now
            }
            Upsert-Session $state $session
            Save-State $state
            Write-Host "Registered session $SessionId."
        }
        break
    }

    "heartbeat" {
        Assert-SessionId
        With-Lock {
            $state = Load-State
            $session = Get-Session $state $SessionId
            if ($null -eq $session) {
                $session = [pscustomobject][ordered]@{
                    sessionId = $SessionId
                    status = $Status
                    goal = $Goal
                    plan = @(Split-Plan $Plan)
                    currentTask = $CurrentTask
                    eta = $Eta
                    blockedOn = $BlockedOn
                    startedUtc = Get-UtcStamp
                    lastSeenUtc = Get-UtcStamp
                }
            } else {
                $session.status = $Status
                if (-not ([string]::IsNullOrWhiteSpace($Goal))) { $session.goal = $Goal }
                if (-not ([string]::IsNullOrWhiteSpace($Plan))) { $session.plan = @(Split-Plan $Plan) }
                if (-not ([string]::IsNullOrWhiteSpace($CurrentTask))) { $session.currentTask = $CurrentTask }
                if (-not ([string]::IsNullOrWhiteSpace($Eta))) { $session.eta = $Eta }
                if (-not ([string]::IsNullOrWhiteSpace($BlockedOn))) { $session.blockedOn = $BlockedOn }
                $session.lastSeenUtc = Get-UtcStamp
            }
            Upsert-Session $state $session
            Resolve-StaleFlags $state $SessionId
            Save-State $state
            Write-Host "Heartbeat recorded for $SessionId."
        }
        break
    }

    "chat" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($Message)) { throw "Message is required for chat." }
        With-Lock {
            $state = Load-State
            $msg = [pscustomobject][ordered]@{
                id = New-CoordinationId "m"
                sessionId = $SessionId
                toSessionId = $ToSessionId
                replyTo = $ReplyTo
                utc = Get-UtcStamp
                message = $Message
            }
            $state.chat = @($state.chat) + $msg
            Prune-Chat $state $SessionId $ChatKeep
            Save-State $state
            Write-Host "Chat posted as $($msg.id)."
        }
        break
    }

    "prune-chat" {
        Assert-SessionId
        With-Lock {
            $state = Load-State
            Prune-Chat $state $SessionId $ChatKeep
            Save-State $state
            Write-Host "Pruned chat for $SessionId to the latest $ChatKeep messages."
        }
        break
    }

    "queue" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($Title)) { throw "Title is required for queue." }
        With-Lock {
            $state = Load-State
            $item = [pscustomobject][ordered]@{
                id = New-CoordinationId "q"
                sessionId = $SessionId
                type = $Type
                resource = $Resource
                title = $Title
                details = $Details
                eta = $Eta
                order = [int]$state.nextQueueSequence
                status = "queued"
                requestedUtc = Get-UtcStamp
                startedUtc = ""
                completedUtc = ""
                result = ""
                summary = ""
                managerNote = ""
                staleFlaggedBy = ""
                staleFlaggedUtc = ""
                staleReason = ""
            }
            $state.nextQueueSequence = [int]$state.nextQueueSequence + 1
            $state.queue = @($state.queue) + $item
            Save-State $state
            Write-QueuePlacement $state $item
        }
        break
    }

    "start" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($QueueId)) { throw "QueueId is required for start." }
        With-Lock {
            $state = Load-State
            $item = Get-QueueItem $state $QueueId
            if ($null -eq $item) { throw "Queue item not found: $QueueId" }
            if ($item.sessionId -ne $SessionId) { throw "Queue item $QueueId belongs to $($item.sessionId), not $SessionId." }
            if ($item.status -ne "queued") { throw "Queue item $QueueId is $($item.status), not queued." }
            $placement = Get-QueuePlacement $state $item
            if (-not $placement.canStart) {
                $aheadText = @($placement.ahead | ForEach-Object { "$($_.id) '$($_.title)' owner=$($_.sessionId)" }) -join "; "
                throw "Queue item $QueueId is not next for resource '$($item.resource)'. Ahead: $aheadText"
            }
            $item.status = "running"
            $item.startedUtc = Get-UtcStamp
            Save-State $state
            Write-Host "Started queue item $QueueId."
        }
        break
    }

    "finish" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($QueueId)) { throw "QueueId is required for finish." }
        With-Lock {
            $state = Load-State
            $item = Get-QueueItem $state $QueueId
            if ($null -eq $item) { throw "Queue item not found: $QueueId" }
            if ($item.sessionId -ne $SessionId) { throw "Queue item $QueueId belongs to $($item.sessionId), not $SessionId." }
            $item.status = if ($Result -eq "done") { "done" } else { $Result }
            $item.result = $Result
            $item.summary = $Summary
            $item.completedUtc = Get-UtcStamp
            Append-QueueResult $item $SessionId $Result $Summary $Evidence
            if ($Archive) {
                $archiveTitle = if ([string]::IsNullOrWhiteSpace($Title)) { $item.title } else { $Title }
                Append-Completed $SessionId $archiveTitle $Summary $Evidence
            }
            Remove-QueueItem $state $QueueId
            Save-State $state
            Write-Host "Finished queue item $QueueId as $Result and cleared it from the live queue."
        }
        break
    }

    "flag-stale" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($QueueId)) { throw "QueueId is required for flag-stale." }
        With-Lock {
            $state = Load-State
            $item = Get-QueueItem $state $QueueId
            if ($null -eq $item) { throw "Queue item not found: $QueueId" }
            if ($item.status -notin @("queued", "running")) { throw "Queue item $QueueId is $($item.status), not queued or running." }
            $reasonText = if (-not ([string]::IsNullOrWhiteSpace($Message))) { $Message } elseif (-not ([string]::IsNullOrWhiteSpace($Summary))) { $Summary } else { "Potentially stale or stuck." }
            $item.staleFlaggedBy = $SessionId
            $item.staleFlaggedUtc = Get-UtcStamp
            $item.staleReason = $reasonText
            $item.managerNote = "Flagged as potentially stale by $SessionId."
            Save-State $state
            Write-Host "Flagged queue item $QueueId as potentially stale. Heartbeats will surface and validate it."
        }
        break
    }

    "clear-stale" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($QueueId)) { throw "QueueId is required for clear-stale." }
        With-Lock {
            $state = Load-State
            $item = Get-QueueItem $state $QueueId
            if ($null -eq $item) { throw "Queue item not found: $QueueId" }
            if ([string]::IsNullOrWhiteSpace($item.staleFlaggedBy)) { throw "Queue item $QueueId has not been flagged stale." }
            $summaryText = if (-not ([string]::IsNullOrWhiteSpace($Summary))) { $Summary } else { "Cleared after stale validation." }
            Append-QueueResult $item $SessionId "abandoned" $summaryText $Evidence
            Remove-QueueItem $state $QueueId
            Save-State $state
            Write-Host "Cleared stale queue item $QueueId from the live queue."
        }
        break
    }

    "complete" {
        Assert-SessionId
        if ([string]::IsNullOrWhiteSpace($Title)) { throw "Title is required for complete." }
        With-Lock {
            $state = Load-State
            Append-Completed $SessionId $Title $Summary $Evidence
            $session = Get-Session $state $SessionId
            if ($null -ne $session) {
                $session.currentTask = ""
                $session.status = "idle"
                $session.lastSeenUtc = Get-UtcStamp
                Upsert-Session $state $session
            }
            Save-State $state
            Write-Host "Archived completed task: $Title"
        }
        break
    }

    "cleanup" {
        With-Lock {
            $state = Load-State
            $dead = @(Abandon-DeadEntries $state)
            Prune-Chat $state "" $ChatKeep
            Save-State $state
            if ($dead.Count -eq 0) {
                Write-Host "Cleanup complete. No dead sessions removed."
            } else {
                Write-Host ("Cleanup complete. Removed dead sessions: {0}" -f ($dead -join ", "))
            }
        }
        break
    }
}
