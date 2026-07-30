param(
    [string]$TaskName = "Perfect Dark 2 Codex Coordination Cleanup",
    [string]$At = "04:00"
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"

$ProjectRoot = "C:\Users\mikeh\Perfect-Dark-2\perfect_dark-mike"
$ScriptPath = Join-Path $ProjectRoot "Tools\CodexCoordination\CodexCoordination.ps1"

if (-not (Test-Path -LiteralPath $ScriptPath)) {
    throw "Coordination script not found at $ScriptPath"
}

$powershellPath = (Get-Command pwsh -ErrorAction SilentlyContinue).Source
if ([string]::IsNullOrWhiteSpace($powershellPath)) {
    $powershellPath = (Get-Command powershell.exe).Source
}

$argument = "-NoProfile -ExecutionPolicy Bypass -File `"$ScriptPath`" cleanup"

try {
    $action = New-ScheduledTaskAction -Execute $powershellPath -Argument $argument -WorkingDirectory $ProjectRoot -ErrorAction Stop
    $trigger = New-ScheduledTaskTrigger -Daily -At ([DateTime]::Parse($At)) -ErrorAction Stop
    $settings = New-ScheduledTaskSettingsSet -StartWhenAvailable -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 10) -ErrorAction Stop

    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings -Description "Removes stale Perfect Dark 2 Codex coordination sessions while preserving sessions awaiting approval." -Force -ErrorAction Stop | Out-Null
    Write-Host "Installed daily cleanup task '$TaskName' at $At."
}
catch {
    Write-Host "ScheduledTasks module failed; falling back to schtasks.exe. Reason: $($_.Exception.Message)"
    $taskRun = "`"$powershellPath`" $argument"
    & schtasks.exe /Create /F /TN $TaskName /SC DAILY /ST $At /TR $taskRun | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "schtasks.exe failed with exit code $LASTEXITCODE"
    }
    Write-Host "Installed daily cleanup task '$TaskName' at $At with schtasks.exe."
}
