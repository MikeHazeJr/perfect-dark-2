# Exact launch identities for native smoke cancellation. No UI/input APIs.
function ConvertTo-SmokeProcessPath {
    param([Parameter(Mandatory)][string]$Path)
    $windowsPath = $Path.Replace('/', '\')
    if (-not [IO.Path]::IsPathRooted($windowsPath)) { throw 'Process path must be absolute' }
    return [IO.Path]::GetFullPath($windowsPath)
}

function ConvertTo-SmokeProcessUtc {
    param([Parameter(Mandatory)]$Value)
    if ($Value -is [DateTimeOffset]) { return $Value.ToUniversalTime() }
    if ($Value -is [DateTime]) { return [DateTimeOffset]$Value.ToUniversalTime() }
    if ([string]$Value -notmatch '(Z|[+-]\d{2}:\d{2})$') {
        throw 'Serialized process time must include an explicit UTC offset'
    }
    return [DateTimeOffset]::Parse([string]$Value, [Globalization.CultureInfo]::InvariantCulture).ToUniversalTime()
}

function New-SmokeProcessOwnership {
    param(
        [Parameter(Mandatory)][Diagnostics.Process]$Process,
        [Parameter(Mandatory)][string]$ExpectedExecutable,
        [Parameter(Mandatory)][string]$CommandLineToken,
        [int]$ParentProcessId = $PID
    )
    # The actual Process returned by Start supplies identity, rather than a
    # later time-window guess or a process-name sweep.
    [void]$Process.Handle
    $expected = ConvertTo-SmokeProcessPath $ExpectedExecutable
    if (-not [string]::Equals((ConvertTo-SmokeProcessPath $Process.Path), $expected,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Launched executable identity mismatch' }
    return [pscustomobject]@{
        schema = 1
        process_id = $Process.Id
        executable_path = $expected
        creation_utc = $Process.StartTime.ToUniversalTime().ToString('o')
        parent_process_id = $ParentProcessId
        command_line_token = $CommandLineToken
    }
}

function Write-SmokeProcessOwnership {
    param([Parameter(Mandatory)]$Ownership, [Parameter(Mandatory)][string]$InstallDir)
    $directory = Join-Path $InstallDir 'logs/smoke-process-ownership'
    $marker = Join-Path $InstallDir '.pd-storage-owner.json'
    if (Test-Path -LiteralPath $marker) {
        $installOwner = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        $Ownership | Add-Member -NotePropertyName storage_install_id -NotePropertyValue $installOwner.id -Force
    }
    [void][IO.Directory]::CreateDirectory($directory)
    $path = Join-Path $directory ("owned-{0}.json" -f $Ownership.process_id)
    [IO.File]::WriteAllText($path, ($Ownership | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))
    return $path
}

function Test-SmokeProcessOwnership {
    param([Parameter(Mandatory)]$Ownership, [Parameter(Mandatory)]$ProcessInfo)
    foreach ($property in @('ProcessId', 'ExecutablePath', 'CreationDate', 'ParentProcessId', 'CommandLine')) {
        if ($null -eq $ProcessInfo.PSObject.Properties[$property] -or
                $null -eq $ProcessInfo.$property -or [string]$ProcessInfo.$property -eq '') {
            return [pscustomobject]@{ status = 'unknown'; reason = "Missing process metadata: $property" }
        }
    }
    $reason = $null
    if ([int]$ProcessInfo.ProcessId -ne [int]$Ownership.process_id) { $reason = 'PID mismatch' }
    elseif (-not [string]::Equals((ConvertTo-SmokeProcessPath $ProcessInfo.ExecutablePath),
            (ConvertTo-SmokeProcessPath $Ownership.executable_path), [StringComparison]::OrdinalIgnoreCase)) {
        $reason = 'Executable mismatch'
    } elseif ([int]$ProcessInfo.ParentProcessId -ne [int]$Ownership.parent_process_id) { $reason = 'Parent mismatch' }
    elseif ([math]::Abs(((ConvertTo-SmokeProcessUtc $ProcessInfo.CreationDate) -
            (ConvertTo-SmokeProcessUtc $Ownership.creation_utc)).TotalMilliseconds) -gt 1) { $reason = 'Creation time mismatch (PID reuse)' }
    elseif (([string]$ProcessInfo.CommandLine).Replace('/', '\').IndexOf(
            ([string]$Ownership.command_line_token).Replace('/', '\'), [StringComparison]::OrdinalIgnoreCase) -lt 0) {
        $reason = 'Fixture/command token mismatch'
    }
    if ($reason) { return [pscustomobject]@{ status = 'mismatch'; reason = $reason } }
    return [pscustomobject]@{ status = 'matched'; reason = $null }
}

function Get-SmokeOwnedProcessState {
    param([Parameter(Mandatory)]$Ownership)
    if ($Ownership.schema -ne 1 -or [int]$Ownership.process_id -le 0 -or
            [int]$Ownership.process_id -eq $PID -or -not $Ownership.command_line_token) {
        throw 'Invalid or self-targeted process ownership record'
    }
    # Query failure is unknown, never an empty/zero-process result.
    $processInfo = Get-CimInstance Win32_Process -Filter ("ProcessId = {0}" -f [int]$Ownership.process_id) -ErrorAction Stop
    if ($null -eq $processInfo) { return [pscustomobject]@{ status = 'absent'; reason = $null } }
    $state = Test-SmokeProcessOwnership -Ownership $Ownership -ProcessInfo $processInfo
    if ($state.status -ne 'matched') { throw "Owned process cannot be verified: $($state.reason)" }
    return $state
}

function Stop-SmokeOwnedProcess {
    param([Parameter(Mandatory)]$Ownership, [ValidateRange(1, 30000)][int]$WaitMilliseconds = 10000)
    $state = Get-SmokeOwnedProcessState -Ownership $Ownership
    $requested = $false
    if ($state.status -eq 'matched') {
        $process = $null
        try {
            try { $process = Get-Process -Id ([int]$Ownership.process_id) -ErrorAction Stop }
            catch {
                if ((Get-SmokeOwnedProcessState $Ownership).status -ne 'absent') { throw }
            }
            if ($process) {
                # Open the specific process handle, then recheck identity before
                # terminating it. A recycled PID must not select another client.
                [void]$process.Handle
                if (-not [string]::Equals((ConvertTo-SmokeProcessPath $process.Path),
                        (ConvertTo-SmokeProcessPath $Ownership.executable_path), [StringComparison]::OrdinalIgnoreCase) -or
                        [math]::Abs(((ConvertTo-SmokeProcessUtc $process.StartTime) -
                        (ConvertTo-SmokeProcessUtc $Ownership.creation_utc)).TotalMilliseconds) -gt 1) {
                    throw 'Process handle no longer matches launch identity'
                }
                $process.Kill()
                $requested = $true
                if (-not $process.WaitForExit($WaitMilliseconds)) { throw 'Owned process did not exit before cleanup deadline' }
            }
        } finally { if ($process) { $process.Dispose() } }
    }
    if ((Get-SmokeOwnedProcessState $Ownership).status -ne 'absent') { throw 'Owned process remains; retain resource leases' }
    return [pscustomobject]@{
        status = 'exited'
        process_id = [int]$Ownership.process_id
        termination_requested = $requested
        verified_utc = [DateTime]::UtcNow.ToString('o')
    }
}
