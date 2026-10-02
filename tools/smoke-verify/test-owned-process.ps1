# Uses hidden disposable shells only. No game, desktop, app permission or input.
param([Parameter(Mandatory)][string]$EvidenceDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'lib/Owned-Process.ps1')
[void][IO.Directory]::CreateDirectory($EvidenceDirectory)
$script:checks = 0
function Assert-Check([bool]$Passed, [string]$Name) {
    if (-not $Passed) { throw "FAIL: $Name" }
    $script:checks++
}
function Assert-Throws([scriptblock]$Action, [string]$Message) {
    $caught = $false
    try { & $Action | Out-Null } catch {
        if ($_.Exception.Message.IndexOf($Message, [StringComparison]::OrdinalIgnoreCase) -lt 0) { throw }
        $caught = $true
    }
    Assert-Check $caught "Expected failure: $Message"
}
function Copy-Record($Record) { return ($Record | ConvertTo-Json -Depth 5 | ConvertFrom-Json) }
$identity = [pscustomobject]@{
    schema=1; process_id=38316; parent_process_id=25708
    executable_path='C:/Users/mikeh/Perfect-Dark-2/perfect_dark-mike/.claude/smoke-storage/shared/client/PerfectDark.exe'
    creation_utc='2026-10-02T06:31:51.1209220Z'
    command_line_token='C:/fixture/menu_virtual_controller_agent_cancel.json'
}
$info = [pscustomobject]@{
    ProcessId=38316; ParentProcessId=25708
    ExecutablePath='C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\.claude\smoke-storage\shared\client\PerfectDark.exe'
    CreationDate=[DateTimeOffset]::Parse('2026-10-02T02:31:51.120922-04:00')
    CommandLine='"C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike\.claude\smoke-storage\shared\client\PerfectDark.exe" --smoke C:\fixture\menu_virtual_controller_agent_cancel.json'
}
Assert-Check ((Test-SmokeProcessOwnership $identity $info).status -eq 'matched') 'Real Windows slash/offset regression'
$case = Copy-Record $info; $case.ExecutablePath=$case.ExecutablePath.ToUpperInvariant()
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'matched') 'Windows case-insensitive identity'
$case = Copy-Record $info; $case.ExecutablePath='C:/other/PerfectDark.exe'
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'mismatch') 'Other install excluded'
$case = Copy-Record $info; $case.ProcessId=38317
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'mismatch') 'Other PID excluded'
$case = Copy-Record $info; $case.ParentProcessId=25709
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'mismatch') 'Other parent excluded'
$case = Copy-Record $info; $case.CreationDate='2026-10-02T06:32:51.1209220Z'
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'mismatch') 'Recycled PID excluded'
$case = Copy-Record $info; $case.CommandLine='--smoke C:/fixture/other.json'
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'mismatch') 'Other fixture excluded'
$case = Copy-Record $info; $case.ExecutablePath=$null
Assert-Check ((Test-SmokeProcessOwnership $identity $case).status -eq 'unknown') 'Unreadable metadata is unknown'
Assert-Throws { ConvertTo-SmokeProcessPath 'relative/PerfectDark.exe' } 'absolute'
Assert-Throws { ConvertTo-SmokeProcessUtc '2026-10-02T06:31:51' } 'explicit UTC offset'
if (Test-Path Function:Get-CimInstance) { throw 'Unexpected pre-existing CIM mock' }
try {
    function Get-CimInstance { [CmdletBinding()]param($ClassName,$Filter) throw 'fixture query denied' }
    Assert-Throws { Get-SmokeOwnedProcessState $identity } 'fixture query denied'
} finally { Remove-Item -LiteralPath Function:Get-CimInstance -ErrorAction SilentlyContinue }

$helper = Join-Path ([IO.Path]::GetFullPath($EvidenceDirectory)) ('disposable-'+[guid]::NewGuid().ToString('N')+'.ps1')
[IO.File]::WriteAllText($helper, "[void][Console]::ReadLine()`r`n", (New-Object Text.UTF8Encoding($false)))
$hostExecutable = (Get-Process -Id $PID).Path
$children = @()
try {
    foreach ($ordinal in @(1,2)) {
        $start = New-Object Diagnostics.ProcessStartInfo
        $start.FileName=$hostExecutable
        $start.Arguments='-NoProfile -NonInteractive -File "'+$helper+'"'
        $start.UseShellExecute=$false; $start.CreateNoWindow=$true; $start.RedirectStandardInput=$true
        $child=[Diagnostics.Process]::Start($start)
        $children += $child
        Assert-Check (-not $child.WaitForExit(300)) "Disposable $ordinal remains alive"
    }
    $owned = New-SmokeProcessOwnership -Process $children[0] -ExpectedExecutable $hostExecutable.Replace('\','/') -CommandLineToken $helper.Replace('\','/')
    $path = Write-SmokeProcessOwnership -Ownership $owned -InstallDir $EvidenceDirectory
    $owned = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    Assert-Check ((Get-SmokeOwnedProcessState $owned).status -eq 'matched') 'Live CIM metadata and JSON UTC round trip'
    $wrong = Copy-Record $owned; $wrong.executable_path='C:/unrelated/pwsh.exe'
    Assert-Throws { Stop-SmokeOwnedProcess $wrong } 'Executable mismatch'
    Assert-Check (-not $children[0].HasExited -and -not $children[1].HasExited) 'Mismatch stops neither disposable'
    $wrong = Copy-Record $owned; $wrong.creation_utc=([DateTimeOffset]::Parse([string]$owned.creation_utc).AddSeconds(10)).ToString('o')
    Assert-Throws { Stop-SmokeOwnedProcess $wrong } 'Creation time mismatch'
    $stopped = Stop-SmokeOwnedProcess -Ownership $owned
    Assert-Check ($stopped.status -eq 'exited' -and $stopped.termination_requested) 'Exact owned disposable terminated and absence verified'
    Assert-Check (-not $children[1].HasExited) 'Same-executable unowned disposable preserved'
    Assert-Check ((Get-SmokeOwnedProcessState $owned).status -eq 'absent') 'Exited owned PID absent via successful query'
    Assert-Check (-not (Stop-SmokeOwnedProcess $owned).termination_requested) 'Already exited cancellation is idempotent'
    $children[1].StandardInput.WriteLine('exit')
    Assert-Check ($children[1].WaitForExit(5000)) 'Unowned disposable exits cooperatively'
    $proof=[ordered]@{verdict='pass';checks=$script:checks;ps_version=$PSVersionTable.PSVersion.ToString();observed_utc=[DateTime]::UtcNow.ToString('o');disposable_pids=@($children|ForEach-Object Id);owned_process_count=0;game_launches=0;ui_calls=0;identity_file=$path}
    $proof|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'result.json')
    [pscustomobject]$proof|Select-Object verdict,checks,ps_version,owned_process_count,game_launches,ui_calls|ConvertTo-Json
} finally {
    foreach ($child in $children) {
        try { if (-not $child.HasExited) { $child.StandardInput.WriteLine('exit'); if (-not $child.WaitForExit(3000)) { $child.Kill(); [void]$child.WaitForExit(5000) } } }
        finally { $child.Dispose() }
    }
}
